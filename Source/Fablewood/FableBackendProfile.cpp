#include "FablePlayerState.h"
#include "FableBackend.h"
#include "FableSaveGame.h"
#include "FableInventorySnapshot.h"
#include "FableGameMode.h"
#include "FableCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
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
/** Owns queued copies independently of the PlayerState so disconnect can drain before releasing its lease. */
struct FFableBackendSession : TSharedFromThis<FFableBackendSession>
{
    TWeakObjectPtr<AFablePlayerState> Player;TWeakObjectPtr<UWorld> World;
    FString PlayerId,LeaseId;int64 Revision=0;
    FString LatestPayload,LatestHash,SentHash,SentPayload,OperationId,AcknowledgedHash;
    TSharedPtr<FJsonObject> LatestWallet,SentWallet,LatestInventory,SentInventory,SentBody;
    bool InventoryAcknowledged=false;
    bool Pending=false,Closing=false,Failed=false,LoginComplete=false;
    FTimerHandle Retry,HeartbeatTimer;double LeaseDeadline=0;
    TSharedRef<FJsonObject> Identity() const {auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("playerId"),PlayerId);J->SetStringField(TEXT("leaseId"),LeaseId);return J;}
    void Mirror()
    {
        if(auto P=Player.Get()){P->BackendRevision=Revision+(Pending?1:0);P->BackendAcknowledgedRevision=Revision;
            P->bBackendSavePending=Pending;P->bLastSaveSucceeded=InventoryAcknowledged&&!Pending&&!Failed;P->ForceNetUpdate();}
    }
    void Failure(const FString& Message)
    {
        if(Failed)return;Failed=true;Pending=false;
        if(World.IsValid()){World->GetTimerManager().ClearTimer(HeartbeatTimer);World->GetTimerManager().ClearTimer(Retry);}
        if(auto P=Player.Get())
        {
            P->bStorageBlocked=true;P->bBackendReady=false;P->bLastSaveSucceeded=false;P->bBackendSavePending=false;P->SetNotice(Message);
            if(auto M=World.IsValid()?World->GetAuthGameMode<AFableGameMode>():nullptr)M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),false,Message);
        }
    }
    void Renew()
    {
        if(Failed||Closing||!World.IsValid())return;
        if(FPlatformTime::Seconds()>LeaseDeadline){Failure(TEXT("서버 저장 권한이 만료되어 연결을 종료합니다."));return;}
        auto Self=AsShared();FFableBackend::Post(World.Get(),TEXT("/v1/server/heartbeat"),Identity(),[Self](bool Ok,int32 Code,TSharedPtr<FJsonObject> J){
            if(Ok)Self->LeaseDeadline=FPlatformTime::Seconds()+50;
            else if(Code>=400&&Code<500)Self->Failure(TEXT("서버 저장 권한을 갱신할 수 없습니다."));
        });
    }
    void Release()
    {
        if(!World.IsValid())return;
        World->GetTimerManager().ClearTimer(HeartbeatTimer);World->GetTimerManager().ClearTimer(Retry);
        FFableBackend::Post(World.Get(),TEXT("/v1/server/release"),Identity(),[](bool,int32,TSharedPtr<FJsonObject>){});
    }
    void Queue(const FString& Payload,const FString& Hash,const TSharedRef<FJsonObject>& W,const TSharedRef<FJsonObject>& Inventory)
    {
        if(Failed)return;
        LatestPayload=Payload;LatestHash=Hash;LatestWallet=W;LatestInventory=Inventory;
        if(InventoryAcknowledged&&!Pending&&Hash==AcknowledgedHash){Mirror();return;}
        if(!Pending)Start();
    }
    void Start()
    {
        if(Failed||!World.IsValid()||!LatestWallet.IsValid()||!LatestInventory.IsValid())return;
        Pending=true;SentPayload=LatestPayload;SentHash=LatestHash;SentWallet=LatestWallet;SentInventory=LatestInventory;
        OperationId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
        // Freeze all three mirrors in a single operation. An ambiguous response retries this exact body.
        SentBody=Identity();SentBody->SetStringField(TEXT("operationId"),OperationId);
        SentBody->SetNumberField(TEXT("expectedRevision"),Revision);SentBody->SetStringField(TEXT("profile"),SentPayload);
        SentBody->SetObjectField(TEXT("wallet"),SentWallet);SentBody->SetObjectField(TEXT("inventory"),SentInventory);
        Mirror();Send();
    }
    void Send()
    {
        if(Failed||!World.IsValid())return;
        if(FPlatformTime::Seconds()>LeaseDeadline){Failure(TEXT("저장 권한이 만료되어 진행을 중단했습니다."));return;}
        if(!SentBody.IsValid()){Failure(TEXT("중앙 저장 작업이 올바르지 않습니다."));return;}
        auto Self=AsShared();FFableBackend::Post(World.Get(),TEXT("/v1/server/commit-inventory"),SentBody.ToSharedRef(),[Self](bool Ok,int32 Code,TSharedPtr<FJsonObject> J){
            if(Self->Failed||!Self->World.IsValid())return;
            double Next=-1,InventoryVersion=-1;FString Hash;
            if(Ok&&J.IsValid()&&J->TryGetNumberField(TEXT("revision"),Next)&&Next==double(Self->Revision+1)
                &&J->TryGetNumberField(TEXT("inventoryVersion"),InventoryVersion)&&InventoryVersion==1
                &&J->TryGetStringField(TEXT("profileHash"),Hash)&&Hash.Equals(Self->SentHash,ESearchCase::IgnoreCase))
            {
                Self->Revision=int64(Next);Self->Pending=false;Self->AcknowledgedHash=Self->SentHash;Self->InventoryAcknowledged=true;
                if(auto P=Self->Player.Get())P->BackendAcknowledgedProfileHash=Self->SentHash;
                if(Self->LatestHash!=Self->SentHash){Self->Start();return;}
                Self->Mirror();
                if(!Self->LoginComplete)
                {
                    Self->LoginComplete=true;
                    if(auto P=Self->Player.Get())
                    {
                        P->bBackendReady=true;P->ForceNetUpdate();
                        if(auto M=Self->World->GetAuthGameMode<AFableGameMode>())M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),true,TEXT(""));
                    }
                }
                if(Self->Closing)Self->Release();
            }
            else if(!Ok&&(Code==0||Code>=500||Code==408||Code==429))
            {
                // Keep the exact operation ID, revision and body after an ambiguous
                // response. PostgreSQL returns the original receipt on a retry.
                Self->World->GetTimerManager().SetTimer(Self->Retry,[Self](){Self->Send();},1.0f,false);
            }
            else Self->Failure(TEXT("중앙 저장 확인에 실패해 연결을 종료합니다. 저장 원본은 보존됩니다."));
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
        if(!ProfileField||!ProfileField->IsValid()||!InventoryField||!InventoryField->IsValid()){Fail();return;}
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
        P->BackendAcknowledgedProfileHash=Session->AcknowledgedHash;
        if(!P->QueueBackendSnapshot(D))Session->Failure(TEXT("중앙 프로필을 직렬화할 수 없습니다."));
        else if(!Session->Pending)
        {
            Session->LoginComplete=true;P->bBackendReady=true;Session->Mirror();
            if(auto M=P->GetWorld()->GetAuthGameMode<AFableGameMode>())M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()),true,TEXT(""));
        }
    });
}
bool AFablePlayerState::QueueBackendSnapshot(UFableSaveGame* Data)
{
    if(bBackendLoading)return true;
    if(!BackendSession.IsValid()||!Data||!Data->IsValidProfile())return false;
    TArray<uint8> Bytes;if(!UGameplayStatics::SaveGameToMemory(Data,Bytes)||Bytes.Num()>262144)return false;
    const FString Hash=HashBytes(Bytes);if(Hash.IsEmpty())return false;
    const auto InventorySnapshot=FableInventorySnapshot::Make(Data);if(!InventorySnapshot.IsValid())return false;
    BackendSession->Queue(FBase64::Encode(Bytes),Hash,Wallet(Data),InventorySnapshot.ToSharedRef());return !BackendSession->Failed;
}
void AFablePlayerState::ReleaseBackendProfile()
{
    bBackendClosing=true;bBackendReady=false;
    if(!BackendSession.IsValid())return;
    SaveProfile();BackendSession->Closing=true;
    if(!BackendSession->Pending)BackendSession->Release();
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