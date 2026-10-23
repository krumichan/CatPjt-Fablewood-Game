#include "FablePlayerState.h"
#include "FableBackend.h"
#include "FableSaveGame.h"
#include "FableInventorySnapshot.h"
#include "FableCharacterSummary.h"
#include "FableGameMode.h"
#include "FableCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"

namespace {
TSharedRef<FJsonObject> Wallet(const UFableSaveGame* D)
{
    auto W=MakeShared<FJsonObject>();W->SetNumberField(TEXT("gold"),D->Gold);W->SetNumberField(TEXT("ore"),D->Ore);
    W->SetNumberField(TEXT("essence"),D->Essence);W->SetNumberField(TEXT("rootCores"),D->RootCores);W->SetNumberField(TEXT("moonCrystals"),D->MoonCrystals);
    W->SetNumberField(TEXT("healthPotions"),D->HealthPotions);W->SetNumberField(TEXT("manaPotions"),D->ManaPotions);return W;
}
FString HashBytes(const TArray<uint8>& Bytes)
{
    uint8 Digest[SHA256_DIGEST_LENGTH];
    if (!SHA256(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Digest)) return TEXT("");
    return BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower();
}
}
/** Owns queued snapshots independently of the actor, so a disconnect can drain its final save. */
struct FFableBackendSession : TSharedFromThis<FFableBackendSession>
{
    TWeakObjectPtr<AFablePlayerState> Player;
    TWeakObjectPtr<UWorld> World;
    FString PlayerId,LeaseId;
    int64 Revision=0;
    FString LatestPayload,LatestHash,SentHash,SentPayload,OperationId,AcknowledgedHash;
    TSharedPtr<FJsonObject> LatestWallet,SentWallet,LatestInventory,SentInventory,LatestSummary,SentSummary,SentBody;
    bool InventoryAcknowledged=false,SummaryAcknowledged=false;
    bool Pending=false,Closing=false,Failed=false,LoginComplete=false;
    bool HeartbeatPending=false,ReleaseStarted=false,ReleasePending=false,Released=false;
    FTimerHandle Retry,HeartbeatTimer,LeaveTimeout;
    double LeaseDeadline=0;
    FString FailureReason;
    TFunction<void(bool,const FString&)> LeaveCompletion;

    TSharedRef<FJsonObject> Identity() const
    {
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("playerId"),PlayerId);J->SetStringField(TEXT("leaseId"),LeaseId);return J;
    }
    void Mirror()
    {
        if(auto P=Player.Get())
        {
            P->BackendRevision=Revision+(Pending?1:0);P->BackendAcknowledgedRevision=Revision;
            P->bBackendSavePending=Pending;
            P->bLastSaveSucceeded=InventoryAcknowledged&&SummaryAcknowledged&&!Pending&&!Failed;
            P->ForceNetUpdate();
        }
    }
    void CompleteLeave(bool bSuccess,const FString& Message)
    {
        if(World.IsValid())World->GetTimerManager().ClearTimer(LeaveTimeout);
        auto Completion=MoveTemp(LeaveCompletion);
        LeaveCompletion=nullptr;
        if(Completion)Completion(bSuccess,Message);
    }
    void AwaitLeave(TFunction<void(bool,const FString&)> Completion)
    {
        if(Released){Completion(true,TEXT("서버 저장과 안전 퇴장이 완료되었습니다."));return;}
        if(Failed){Completion(false,FailureReason);return;}
        LeaveCompletion=MoveTemp(Completion);
        auto Self=AsShared();
        if(!World.IsValid()){CompleteLeave(false,TEXT("서버 연결 상태를 확인할 수 없습니다."));return;}
        World->GetTimerManager().SetTimer(LeaveTimeout,[Self]()
        {
            // A timeout is not an acknowledgement. Drain/retry keeps the frozen body;
            // a later user request can wait again without resuming gameplay or creating a new save.
            Self->CompleteLeave(false,TEXT("저장 또는 퇴장 확인이 지연되고 있습니다. 접속을 유지하고 다시 확인해 주세요."));
        },30.0f,false);
    }
    void Failure(const FString& Message)
    {
        if(Failed)return;
        Failed=true;Pending=false;FailureReason=Message;
        if(World.IsValid())
        {
            World->GetTimerManager().ClearTimer(HeartbeatTimer);
            World->GetTimerManager().ClearTimer(Retry);
        }
        if(auto P=Player.Get())
        {
            P->bStorageBlocked=true;P->bBackendReady=false;P->bLastSaveSucceeded=false;P->bBackendSavePending=false;
            P->SetNotice(Message);P->ForceNetUpdate();
            // A failed voluntary close must keep the player on the error screen, never announce a saved exit.
            if(!Closing)
                if(auto M=World.IsValid()?World->GetAuthGameMode<AFableGameMode>():nullptr)
                    M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),false,Message);
        }
        CompleteLeave(false,Message);
    }
    void Renew()
    {
        if(Failed||Released||ReleaseStarted||HeartbeatPending||!World.IsValid())return;
        if(FPlatformTime::Seconds()>LeaseDeadline){Failure(TEXT("서버 저장 권한이 만료되어 진행을 중단했습니다."));return;}
        HeartbeatPending=true;
        auto Self=AsShared();
        FFableBackend::Post(World.Get(),TEXT("/v1/server/heartbeat"),Identity(),[Self](bool Ok,int32 Code,TSharedPtr<FJsonObject>)
        {
            Self->HeartbeatPending=false;
            // A response from before release may arrive after the lease was successfully released.
            if(Self->Failed||Self->Released||Self->ReleaseStarted)return;
            if(Ok)Self->LeaseDeadline=FPlatformTime::Seconds()+50;
            else if(Code>=400&&Code<500&&Code!=408&&Code!=429)
                Self->Failure(TEXT("서버 저장 권한을 갱신할 수 없습니다."));
        });
    }
    void Release()
    {
        if(Failed||Released||ReleasePending||Pending||!Closing||!World.IsValid())return;
        if(!InventoryAcknowledged||!SummaryAcknowledged||LatestHash!=AcknowledgedHash)
        {Failure(TEXT("최종 저장 확인 전에는 안전하게 퇴장할 수 없습니다."));return;}
        ReleaseStarted=true;ReleasePending=true;
        World->GetTimerManager().ClearTimer(HeartbeatTimer);
        World->GetTimerManager().ClearTimer(Retry);
        auto Self=AsShared();
        FFableBackend::Post(World.Get(),TEXT("/v1/server/release-confirmed"),Identity(),[Self](bool Ok,int32 Code,TSharedPtr<FJsonObject> J)
        {
            Self->ReleasePending=false;
            if(Self->Failed||Self->Released||!Self->World.IsValid())return;
            const auto* Receipt=J.IsValid()?J->Values.Find(TEXT("released")):nullptr;
            if(Ok&&Receipt&&Receipt->IsValid()&&(*Receipt)->Type==EJson::Boolean&&(*Receipt)->AsBool())
            {
                Self->Released=true;Self->Mirror();
                Self->CompleteLeave(true,TEXT("서버 저장과 안전 퇴장이 완료되었습니다."));
            }
            else if(!Ok&&(Code==0||Code>=500||Code==408||Code==429))
            {
                // The dedicated receipt route can acknowledge an already released lease without touching a new lease.
                Self->World->GetTimerManager().SetTimer(Self->Retry,[Self](){Self->Release();},1.0f,false);
            }
            else Self->Failure(TEXT("서버 퇴장 확인에 실패했습니다. 저장 완료로 표시하지 않으며 접속을 유지합니다."));
        });
    }
    void Queue(const FString& Payload,const FString& Hash,const TSharedRef<FJsonObject>& W,
        const TSharedRef<FJsonObject>& Inventory,const TSharedRef<FJsonObject>& Summary,bool bFinalSnapshot)
    {
        if(Failed||(Closing&&!bFinalSnapshot)||ReleaseStarted||Released)return;
        LatestPayload=Payload;LatestHash=Hash;LatestWallet=W;LatestInventory=Inventory;LatestSummary=Summary;
        if(InventoryAcknowledged&&SummaryAcknowledged&&!Pending&&Hash==AcknowledgedHash){Mirror();return;}
        if(!Pending)Start();
    }
    void Start()
    {
        if(Failed||ReleaseStarted||!World.IsValid()||!LatestWallet.IsValid()||!LatestInventory.IsValid()||!LatestSummary.IsValid())return;
        Pending=true;SentPayload=LatestPayload;SentHash=LatestHash;SentWallet=LatestWallet;
        SentInventory=LatestInventory;SentSummary=LatestSummary;
        OperationId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
        // All mirrors come from one SaveGame snapshot. Ambiguous retries retain this exact operation and body.
        SentBody=Identity();SentBody->SetStringField(TEXT("operationId"),OperationId);
        SentBody->SetNumberField(TEXT("expectedRevision"),Revision);SentBody->SetStringField(TEXT("profile"),SentPayload);
        SentBody->SetObjectField(TEXT("wallet"),SentWallet);SentBody->SetObjectField(TEXT("inventory"),SentInventory);
        SentBody->SetObjectField(TEXT("summary"),SentSummary);
        Mirror();Send();
    }
    void Send()
    {
        if(Failed||ReleaseStarted||!World.IsValid())return;
        if(FPlatformTime::Seconds()>LeaseDeadline){Failure(TEXT("저장 권한이 만료되어 진행을 중단했습니다."));return;}
        if(!SentBody.IsValid()){Failure(TEXT("중앙 저장 작업이 올바르지 않습니다."));return;}
        auto Self=AsShared();
        FFableBackend::Post(World.Get(),TEXT("/v1/server/commit-character"),SentBody.ToSharedRef(),[Self](bool Ok,int32 Code,TSharedPtr<FJsonObject> J)
        {
            if(Self->Failed||!Self->World.IsValid())return;
            if(Ok&&FableCharacterSummary::IsCommitAcknowledgement(J,Self->Revision+1,Self->SentHash))
            {
                ++Self->Revision;Self->Pending=false;Self->AcknowledgedHash=Self->SentHash;
                Self->InventoryAcknowledged=true;Self->SummaryAcknowledged=true;
                if(auto P=Self->Player.Get())P->BackendAcknowledgedProfileHash=Self->SentHash;
                if(Self->LatestHash!=Self->SentHash){Self->Start();return;}
                Self->Mirror();
                if(!Self->LoginComplete&&!Self->Closing)
                {
                    Self->LoginComplete=true;
                    if(auto P=Self->Player.Get())
                    {
                        P->bBackendReady=true;P->ForceNetUpdate();
                        if(auto M=Self->World->GetAuthGameMode<AFableGameMode>())
                            M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),true,TEXT(""));
                    }
                }
                if(Self->Closing)Self->Release();
            }
            else if(!Ok&&(Code==0||Code>=500||Code==408||Code==429))
                Self->World->GetTimerManager().SetTimer(Self->Retry,[Self](){Self->Send();},1.0f,false);
            else Self->Failure(TEXT("중앙 저장 확인에 실패했습니다. 진행을 중단하고 저장 원본을 보존합니다."));
        });
    }
};

void AFablePlayerState::InitializeBackendProfile(const FString& Ticket)
{
    if(!HasAuthority()||GetNetMode()!=NM_DedicatedServer||bProfileLoaded||bBackendEnabled)return;
    bBackendEnabled=true;bBackendReady=false;bBackendLoading=true;bPersistenceEnabled=false;bLastSaveSucceeded=false;
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("ticket"),Ticket);Body->SetStringField(TEXT("serverId"),FFableBackend::ServerId());
    TWeakObjectPtr<AFablePlayerState> Weak(this);TWeakObjectPtr<UWorld> LoginWorld(GetWorld());
    FFableBackend::Post(GetWorld(),TEXT("/v1/server/claim"),Body,[Weak,LoginWorld](bool Ok,int32 Code,TSharedPtr<FJsonObject> J){
        auto ReleaseClaim=[LoginWorld,J](){
            FString Id,Lease;
            if(LoginWorld.IsValid()&&J.IsValid()&&J->TryGetStringField(TEXT("playerId"),Id)&&J->TryGetStringField(TEXT("leaseId"),Lease))
            {auto B=MakeShared<FJsonObject>();B->SetStringField(TEXT("playerId"),Id);B->SetStringField(TEXT("leaseId"),Lease);
                FFableBackend::Post(LoginWorld.Get(),TEXT("/v1/server/release"),B,[](bool,int32,TSharedPtr<FJsonObject>){});}
        };
        auto P=Weak.Get();if(!P||!P->GetWorld()||P->bBackendClosing||!IsValid(P->GetOwner())){if(Ok)ReleaseClaim();return;}
        auto Fail=[P,ReleaseClaim](){ReleaseClaim();P->bStorageBlocked=true;P->bBackendReady=false;P->bLastSaveSucceeded=false;
            if(auto M=P->GetWorld()->GetAuthGameMode<AFableGameMode>())M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),false,TEXT("계정 인증 또는 중앙 프로필을 확인할 수 없습니다."));};
        FString Id,Lease,Payload,ExpectedHash;double Rev=-1,InitialClass=-1,InventoryVersion=-1;
        FGuid PlayerGuid,LeaseGuid;
        if(!Ok||!J.IsValid()||!J->TryGetStringField(TEXT("playerId"),Id)||!J->TryGetStringField(TEXT("leaseId"),Lease)
            ||!FGuid::ParseExact(Id,EGuidFormats::DigitsWithHyphens,PlayerGuid)||!PlayerGuid.IsValid()
            ||!FGuid::ParseExact(Lease,EGuidFormats::DigitsWithHyphens,LeaseGuid)||!LeaseGuid.IsValid()
            ||Id!=PlayerGuid.ToString(EGuidFormats::DigitsWithHyphensLower)
            ||Lease!=LeaseGuid.ToString(EGuidFormats::DigitsWithHyphensLower)
            ||!J->TryGetNumberField(TEXT("revision"),Rev)||!FMath::IsFinite(Rev)||Rev<0||Rev>9007199254740990.0||Rev!=double(int64(Rev))
            ||!J->TryGetNumberField(TEXT("initialClass"),InitialClass)||!FMath::IsFinite(InitialClass)
            ||InitialClass<0||InitialClass>2||InitialClass!=double(int32(InitialClass))
            ||!J->TryGetNumberField(TEXT("inventoryVersion"),InventoryVersion)||(InventoryVersion!=0&&InventoryVersion!=1)) {Fail();return;}
        if(!P->BackendPlayerId.IsEmpty()&&P->BackendPlayerId!=Id){Fail();return;}
        const auto* ProfileField=J->Values.Find(TEXT("profile"));
        const auto* InventoryField=J->Values.Find(TEXT("inventory"));
        const auto* SummaryField=J->Values.Find(TEXT("summary"));
        if(!ProfileField||!ProfileField->IsValid()||!InventoryField||!InventoryField->IsValid()
            ||!SummaryField||!SummaryField->IsValid()){Fail();return;}
        const bool bHasSummary=(*SummaryField)->Type==EJson::Object;
        if(!bHasSummary&&(*SummaryField)->Type!=EJson::Null){Fail();return;}
        if(bHasSummary&&InventoryVersion!=1){Fail();return;}
        const bool bHasStoredProfile=(*ProfileField)->Type==EJson::String;
        if((!bHasStoredProfile&&(*ProfileField)->Type!=EJson::Null)
            ||(bHasStoredProfile&&(!(*ProfileField)->TryGetString(Payload)||Payload.IsEmpty()))){Fail();return;}
        if(InventoryVersion==0&&(*InventoryField)->Type!=EJson::Null){Fail();return;}
        if(InventoryVersion==1&&(!bHasStoredProfile||(*InventoryField)->Type!=EJson::Object)){Fail();return;}
        if(!bHasStoredProfile&&Rev!=0){Fail();return;}

        // The temporary decoded profile must agree with every typed item and wallet field
        // before applying it. Missing typed rows are accepted only for an explicit legacy claim.
        UFableSaveGame* D=nullptr;
        if(bHasStoredProfile)
        {
            TArray<uint8> Bytes;
            if(!FBase64::Decode(Payload,Bytes)||Bytes.Num()>262144||!J->TryGetStringField(TEXT("profileHash"),ExpectedHash)
                ||HashBytes(Bytes)!=ExpectedHash.ToLower()){Fail();return;}
            D=Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
            if(!D||(InventoryVersion==1?D->Version!=9:D->Version>=9)||!D->MigrateAndValidate()){Fail();return;}
            if(InventoryVersion==1&&!FableInventorySnapshot::Matches(D,(*InventoryField)->AsObject())){Fail();return;}
            if(bHasSummary&&!FableCharacterSummary::Matches(D,(*SummaryField)->AsObject())){Fail();return;}
        }
        else
        {
            const auto* HashField=J->Values.Find(TEXT("profileHash"));
            if(!HashField||!HashField->IsValid()||(*HashField)->Type!=EJson::Null){Fail();return;}
        }
        P->BackendPlayerId=Id;
        P->InitializeProfile(true,TEXT(""),EFableClass(int32(InitialClass)));
        P->ProfileSlot=TEXT("backend:")+Id;
        if(!D)D=P->MakeProfileSnapshot();
        const TSharedPtr<FJsonObject>* W=nullptr;
        bool WalletMatches=J->TryGetObjectField(TEXT("wallet"),W)&&W&&W->IsValid();
        const auto Expected=Wallet(D);
        if(WalletMatches)
        {
            WalletMatches=(*W)->Values.Num()==Expected->Values.Num();
            for(const auto& E:Expected->Values)
            {
                const auto* Actual=(*W)->Values.Find(E.Key);
                WalletMatches&=Actual&&Actual->IsValid()&&(*Actual)->Type==EJson::Number
                    &&(*Actual)->AsNumber()==E.Value->AsNumber();
            }
        }
        if(!WalletMatches||P->BackendPlayerId!=Id){Fail();return;}
        P->ApplyProfileSnapshot(D);
        P->bBackendLoading=false;P->bProfileLoaded=true;P->bLastSaveSucceeded=false;
        P->BackendSession=MakeShared<FFableBackendSession>();auto Session=P->BackendSession;
        Session->Player=P;Session->World=P->GetWorld();Session->PlayerId=Id;Session->LeaseId=Lease;Session->Revision=int64(Rev);Session->LeaseDeadline=FPlatformTime::Seconds()+50;
        P->GetWorld()->GetTimerManager().SetTimer(Session->HeartbeatTimer,[Session](){Session->Renew();},15.0f,true);
        Session->AcknowledgedHash=ExpectedHash.ToLower();Session->InventoryAcknowledged=InventoryVersion==1;
        Session->SummaryAcknowledged=bHasSummary;
        P->BackendAcknowledgedProfileHash=Session->AcknowledgedHash;
        if(!P->QueueBackendSnapshot(D))Session->Failure(TEXT("중앙 프로필을 직렬화할 수 없습니다."));
        else if(!Session->Pending)
        {
            Session->LoginComplete=true;P->bBackendReady=true;Session->Mirror();
            if(auto M=P->GetWorld()->GetAuthGameMode<AFableGameMode>())M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),true,TEXT(""));
        }
    });
}
bool AFablePlayerState::QueueBackendSnapshot(UFableSaveGame* Data,bool bFinalSnapshot)
{
    if(bBackendLoading)return true;
    if(!BackendSession.IsValid()||(BackendSession->Closing&&!bFinalSnapshot)||!Data||!Data->IsValidProfile())return false;
    TArray<uint8> Bytes;if(!UGameplayStatics::SaveGameToMemory(Data,Bytes)||Bytes.Num()>262144)return false;
    const FString Hash=HashBytes(Bytes);if(Hash.IsEmpty())return false;
    const auto InventorySnapshot=FableInventorySnapshot::Make(Data);if(!InventorySnapshot.IsValid())return false;
    const auto Summary=FableCharacterSummary::MakeSummary(Data);if(!Summary.IsValid())return false;
    BackendSession->Queue(FBase64::Encode(Bytes),Hash,Wallet(Data),InventorySnapshot.ToSharedRef(),Summary.ToSharedRef(),bFinalSnapshot);
    return !BackendSession->Failed;
}
void AFablePlayerState::ReleaseBackendProfile()
{
    if(!HasAuthority()||!bBackendEnabled)return;
    const bool bAlreadyClosing=bBackendClosing;
    bBackendClosing=true;bBackendReady=false;ForceNetUpdate();
    if(!BackendSession.IsValid()||bAlreadyClosing)return;
    // Mark closing before any HTTP dispatch (even synchronous dispatch failure must not kick the player).
    // Only this first close may enqueue a final snapshot; later Logout/retry calls reuse it.
    BackendSession->Closing=true;
    if(!QueueBackendSnapshot(MakeProfileSnapshot(),true))
    {
        BackendSession->Failure(TEXT("최종 저장 자료를 준비할 수 없습니다. 안전 퇴장이 완료되지 않았습니다."));
        return;
    }
    if(!BackendSession->Pending)BackendSession->Release();
}
void AFablePlayerState::RequestBackendLeave(TFunction<void(bool,const FString&)> Completion)
{
    if(!HasAuthority()||!bBackendEnabled||!BackendSession.IsValid()||!BackendSession->LoginComplete)
    {Completion(false,TEXT("중앙 프로필 접속이 완료된 뒤 다시 시도해 주세요."));return;}
    ReleaseBackendProfile();
    BackendSession->AwaitLeave(MoveTemp(Completion));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableBackendSHA256Test,"Fablewood.Backend.SHA256KnownVectors",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableBackendSHA256Test::RunTest(const FString& Parameters)
{
    // Independent NIST vectors catch unsupported platform APIs and hashing the base64 text instead of raw bytes.
    TArray<uint8> Bytes;
    TestEqual(TEXT("Empty raw bytes"),HashBytes(Bytes),FString(TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")));
    Bytes={0x61,0x62,0x63};
    TestEqual(TEXT("Raw abc bytes"),HashBytes(Bytes),FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    const ANSICHAR* Multi="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Bytes.Reset();Bytes.Append(reinterpret_cast<const uint8*>(Multi),FCStringAnsi::Strlen(Multi));
    TestEqual(TEXT("Multi-block raw bytes"),HashBytes(Bytes),FString(TEXT("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")));
    return true;
}
#endif