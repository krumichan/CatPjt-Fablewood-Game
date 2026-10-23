#include "FablePlayerState.h"
#include "FableBackend.h"
#include "FableSaveGame.h"
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
FString Json(const TSharedRef<FJsonObject>& J){FString T;FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&T));return T;}
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
    TSharedPtr<FJsonObject> LatestWallet,SentWallet;
    bool Pending=false,Closing=false,Failed=false,LoginComplete=false;
    FTimerHandle Retry,HeartbeatTimer;double LeaseDeadline=0;
    TSharedRef<FJsonObject> Identity() const {auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("playerId"),PlayerId);J->SetStringField(TEXT("leaseId"),LeaseId);return J;}
    void Mirror()
    {
        if(auto P=Player.Get()){P->BackendRevision=Revision+(Pending?1:0);P->BackendAcknowledgedRevision=Revision;
            P->bBackendSavePending=Pending;P->bLastSaveSucceeded=!Pending&&!Failed;P->ForceNetUpdate();}
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
    void Queue(const FString& Payload,const FString& Hash,const TSharedRef<FJsonObject>& W)
    {
        if(Failed)return;
        LatestPayload=Payload;LatestHash=Hash;LatestWallet=W;
        if(!Pending&&Hash==AcknowledgedHash){Mirror();return;}
        if(!Pending)Start();
    }
    void Start()
    {
        if(Failed||!World.IsValid()||!LatestWallet.IsValid())return;
        Pending=true;SentPayload=LatestPayload;SentHash=LatestHash;SentWallet=LatestWallet;OperationId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);Mirror();Send();
    }
    void Send()
    {
        if(Failed||!World.IsValid())return;
        if(FPlatformTime::Seconds()>LeaseDeadline){Failure(TEXT("저장 권한이 만료되어 진행을 중단했습니다."));return;}
        auto Body=Identity();Body->SetStringField(TEXT("operationId"),OperationId);Body->SetNumberField(TEXT("expectedRevision"),Revision);
        Body->SetStringField(TEXT("profile"),SentPayload);Body->SetObjectField(TEXT("wallet"),SentWallet);
        auto Self=AsShared();FFableBackend::Post(World.Get(),TEXT("/v1/server/commit"),Body,[Self](bool Ok,int32 Code,TSharedPtr<FJsonObject> J){
            if(Self->Failed||!Self->World.IsValid())return;
            double Next=-1;FString Hash;
            if(Ok&&J->TryGetNumberField(TEXT("revision"),Next)&&int64(Next)==Self->Revision+1
                &&J->TryGetStringField(TEXT("profileHash"),Hash)&&Hash.Equals(Self->SentHash,ESearchCase::IgnoreCase))
            {
                Self->Revision=int64(Next);Self->Pending=false;Self->AcknowledgedHash=Self->SentHash;
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
        FString Id,Lease,Payload,ExpectedHash;double Rev=-1,InitialClass=0;
        if(!Ok||!J.IsValid()||!J->TryGetStringField(TEXT("playerId"),Id)||!J->TryGetStringField(TEXT("leaseId"),Lease)
            ||!J->TryGetNumberField(TEXT("revision"),Rev)||Rev<0||!J->TryGetNumberField(TEXT("initialClass"),InitialClass)) {Fail();return;}
        P->BackendPlayerId=Id;P->InitializeProfile(true,TEXT(""),EFableClass(FMath::Clamp(int32(InitialClass),0,2)));
        P->ProfileSlot=TEXT("backend:")+Id;
        if(J->TryGetStringField(TEXT("profile"),Payload)&&!Payload.IsEmpty())
        {
            TArray<uint8> Bytes;
            if(!FBase64::Decode(Payload,Bytes)||Bytes.Num()>262144||!J->TryGetStringField(TEXT("profileHash"),ExpectedHash)||HashBytes(Bytes)!=ExpectedHash.ToLower()){Fail();return;}
            auto D=Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
            if(!D||!D->MigrateAndValidate()){Fail();return;}
            P->ApplyProfileSnapshot(D);
        }
        auto D=P->MakeProfileSnapshot();const TSharedPtr<FJsonObject>* W=nullptr;
        if(!J->TryGetObjectField(TEXT("wallet"),W)||!W||!W->IsValid()||Json(W->ToSharedRef())!=Json(Wallet(D)))
        {
            // JSON object order is not significant; compare each canonical number.
            bool Match=W&&W->IsValid();auto Expected=Wallet(D);
            if(Match)for(const auto& E:Expected->Values){double A=-1,B=-2;Match&=(*W)->TryGetNumberField(E.Key,A)&&E.Value->TryGetNumber(B)&&A==B;}
            if(!Match){Fail();return;}
        }
        P->bBackendLoading=false;P->bProfileLoaded=true;P->bLastSaveSucceeded=false;
        P->BackendSession=MakeShared<FFableBackendSession>();auto Session=P->BackendSession;
        Session->Player=P;Session->World=P->GetWorld();Session->PlayerId=Id;Session->LeaseId=Lease;Session->Revision=int64(Rev);Session->LeaseDeadline=FPlatformTime::Seconds()+50;
        P->GetWorld()->GetTimerManager().SetTimer(Session->HeartbeatTimer,[Session](){Session->Renew();},15.0f,true);
        Session->AcknowledgedHash=ExpectedHash.ToLower();P->BackendAcknowledgedProfileHash=Session->AcknowledgedHash;
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
    BackendSession->Queue(FBase64::Encode(Bytes),Hash,Wallet(Data));return !BackendSession->Failed;
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