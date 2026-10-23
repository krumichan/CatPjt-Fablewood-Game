#include "FableOnlineFlowSubsystem.h"
#include "FableOnlineWidget.h"
#include "FableGameInstance.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace
{
bool Number(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,int64& Value,int64 Min,int64 Max)
{
    double Raw=0;
    if(!Object.IsValid()||!Object->TryGetNumberField(Key,Raw)||!FMath::IsFinite(Raw)||Raw<Min||Raw>Max||Raw!=FMath::FloorToDouble(Raw))return false;
    Value=int64(Raw);return true;
}
bool IsSafeToken(const FString& Value)
{
    if(Value.Len()<16||Value.Len()>512)return false;
    for(TCHAR C:Value)if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='-'||C=='_'||C=='.'))return false;
    return true;
}
bool OnlineEnabled()
{
    if(IsRunningDedicatedServer()||IsRunningCommandlet()||FParse::Param(FCommandLine::Get(),TEXT("server"))||FParse::Param(FCommandLine::Get(),TEXT("FableLocal")))return false;
    FString Role;
    if(FParse::Value(FCommandLine::Get(),TEXT("FableOnlineValidationRole="),Role)||FParse::Param(FCommandLine::Get(),TEXT("FableOnline")))return true;
    const TCHAR* Flags[]={TEXT("FableValidationRole="),TEXT("FableRPGValidationRole="),TEXT("FableCoopValidationRole="),TEXT("FableDedicatedValidationRole="),TEXT("FableFrontEndValidationRole="),TEXT("FableFrontEndValidationRun=")};
    for(const TCHAR* Flag:Flags)if(FParse::Value(FCommandLine::Get(),Flag,Role))return false;
    return !FParse::Param(FCommandLine::Get(),TEXT("FableArtReview"));
}
bool ValidBackendUrl(const FString& Value)
{
    const bool Secure=Value.StartsWith(TEXT("https://"));
    if(!Secure&&!Value.StartsWith(TEXT("http://")))return false;
    const FString Authority=Value.Mid(Secure?8:7);
    if(Authority.Contains(TEXT("/"))||Authority.Contains(TEXT("@"))||Authority.Contains(TEXT("?"))||Authority.Contains(TEXT("#")))return false;
    FString Normalized;
    if(!UFableGameInstance::ValidateJoinAddress(Authority,Normalized))return false;
    return Secure||Normalized.StartsWith(TEXT("127.0.0.1:"))||Normalized.StartsWith(TEXT("localhost:"));
}
}

void UFableOnlineFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);bEnabled=OnlineEnabled();
    if(!bEnabled)return;
    FString Override;
    if(FParse::Value(FCommandLine::Get(),TEXT("FableOnlineBackendUrl="),Override))
    {
        Override.RemoveFromEnd(TEXT("/"));
        if(ValidBackendUrl(Override))BackendUrl=Override;
        else bConfigurationValid=false;
    }
    UIFont=LoadObject<UFont>(nullptr,TEXT("/Game/UI/Fonts/F_Korean.F_Korean"));
    SetState(bConfigurationValid?EFableOnlineState::SignedOut:EFableOnlineState::Error,bConfigurationValid?TEXT("계정으로 로그인해 모험을 이어가세요."):TEXT("계정 서버 주소 설정을 확인해 주세요. HTTPS 또는 이 PC의 로컬 주소만 사용할 수 있습니다."));
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UFableOnlineFlowSubsystem::TickFlow),.1f);
}
void UFableOnlineFlowSubsystem::Deinitialize()
{
    FTSTicker::RemoveTicker(Ticker);CancelRequests();ClearSession();
    if(Widget.IsValid()&&GetGameInstance()&&GetGameInstance()->GetGameViewportClient())GetGameInstance()->GetGameViewportClient()->RemoveViewportWidgetContent(Widget.ToSharedRef());
    Widget.Reset();Super::Deinitialize();
}
bool UFableOnlineFlowSubsystem::IsBusy() const
{
    return State==EFableOnlineState::Authenticating||State==EFableOnlineState::Refreshing||State==EFableOnlineState::Joining||State==EFableOnlineState::Leaving;
}
bool UFableOnlineFlowSubsystem::IsOverlayBlocking() const
{
    return bEnabled&&(State!=EFableOnlineState::Playing||bGameMenuOpen);
}
FString UFableOnlineFlowSubsystem::GetStateName() const
{
    switch(State)
    {
    case EFableOnlineState::SignedOut:return TEXT("SignedOut");case EFableOnlineState::Authenticating:return TEXT("Authenticating");
    case EFableOnlineState::Refreshing:return TEXT("Refreshing");case EFableOnlineState::Character:return TEXT("Character");
    case EFableOnlineState::Joining:return TEXT("Joining");case EFableOnlineState::Playing:return TEXT("Playing");
    case EFableOnlineState::Leaving:return TEXT("Leaving");case EFableOnlineState::Disconnected:return TEXT("Disconnected");
    default:return TEXT("Error");
    }
}
void UFableOnlineFlowSubsystem::SetState(EFableOnlineState Value,const FString& Text)
{
    State=Value;Message=Text;++ViewRevision;
}
void UFableOnlineFlowSubsystem::CancelRequests()
{
    ++RequestSerial;
    if(ActiveRequest.IsValid()){ActiveRequest->OnProcessRequestComplete().Unbind();ActiveRequest->CancelRequest();ActiveRequest->SetContentAsString(TEXT(""));ActiveRequest->SetHeader(TEXT("Authorization"),TEXT(""));ActiveRequest.Reset();}
    PendingRegistrationPassword.Reset();
}
void UFableOnlineFlowSubsystem::ClearSession()
{
    AccessToken.Reset();PendingRegistrationPassword.Reset();AccountName.Reset();Character=FFableOnlineCharacter();Servers.Reset();SelectedServerId.Reset();
}
void UFableOnlineFlowSubsystem::SendJson(const FString& Verb,const FString& Path,const TSharedPtr<FJsonObject>& Body,bool bBearer,FReply Callback)
{
    const uint32 Serial=++RequestSerial;
    auto Request=FHttpModule::Get().CreateRequest();ActiveRequest=Request;
    Request->SetURL(BackendUrl+Path);Request->SetVerb(Verb);Request->SetTimeout(15.f);Request->SetHeader(TEXT("Content-Type"),TEXT("application/json"));
    if(bBearer)Request->SetHeader(TEXT("Authorization"),TEXT("Bearer ")+AccessToken);
    if(Body.IsValid()){FString Payload;FJsonSerializer::Serialize(Body.ToSharedRef(),TJsonWriterFactory<>::Create(&Payload));Request->SetContentAsString(Payload);Payload.Reset();}
    const TWeakObjectPtr<UFableOnlineFlowSubsystem> Weak(this);
    Request->OnProcessRequestComplete().BindLambda([Weak,Serial,bBearer,Callback=MoveTemp(Callback)](FHttpRequestPtr Completed,FHttpResponsePtr Response,bool Success)mutable
    {
        Completed->SetContentAsString(TEXT(""));Completed->SetHeader(TEXT("Authorization"),TEXT(""));
        if(!Weak.IsValid()||Weak->RequestSerial!=Serial)return;
        auto* Self=Weak.Get();Self->ActiveRequest.Reset();const int32 Code=Success&&Response.IsValid()?Response->GetResponseCode():0;Self->LastHttpStatus=Code;
        if(Code==401&&bBearer){Self->ClearSession();Self->SetState(EFableOnlineState::SignedOut,TEXT("로그인 시간이 만료되었습니다. 비밀번호를 다시 입력해 주세요."));return;}
        TSharedPtr<FJsonObject> Json;
        if(Response.IsValid()&&Code>=200&&Code<300)FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),Json);
        Callback(Code,Json);
    });
    if(!Request->ProcessRequest())
    {
        Request->OnProcessRequestComplete().Unbind();Request->SetContentAsString(TEXT(""));Request->SetHeader(TEXT("Authorization"),TEXT(""));ActiveRequest.Reset();PendingRegistrationPassword.Reset();
        LastHttpStatus=0;HandleHttpError(0,TEXT("요청"));
    }
}
void UFableOnlineFlowSubsystem::HandleHttpError(int32 Code,const FString& Context)
{
    PendingRegistrationPassword.Reset();
    FString Text;
    if(Code==0)Text=TEXT("서버에 연결할 수 없습니다. 실행 상태를 확인한 뒤 다시 시도해 주세요.");
    else if(Code==401)Text=TEXT("계정 이름 또는 비밀번호를 확인해 주세요.");
    else if(Code==409)Text=TEXT("이미 사용 중인 계정이거나 다른 접속이 아직 종료되지 않았습니다. 잠시 후 다시 시도해 주세요.");
    else if(Code==429)Text=TEXT("요청이 많습니다. 잠시 기다린 뒤 다시 시도해 주세요.");
    else if(Code==400)Text=TEXT("입력 내용을 확인해 주세요. 계정은 영문·숫자·밑줄 3~32자, 비밀번호는 10~128자입니다.");
    else Text=Context+TEXT("을 완료하지 못했습니다. 잠시 후 다시 시도해 주세요.");
    SetState(EFableOnlineState::Error,Text);
}
bool UFableOnlineFlowSubsystem::CanAuthenticate(const FString& Name,const FString& Password)
{
    if(!bEnabled||!bConfigurationValid||IsBusy()||bManagedConnection||HasSession())return false;
    bool Valid=Name.Len()>=3&&Name.Len()<=32&&Password.Len()>=10&&Password.Len()<=128;
    for(TCHAR C:Name)Valid&=(C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_';
    if(!Valid)HandleHttpError(400,TEXT("로그인"));
    return Valid;
}
bool UFableOnlineFlowSubsystem::Authenticate(const FString& Name,const FString& Password)
{
    const FString Clean=Name.TrimStartAndEnd();if(!CanAuthenticate(Clean,Password))return false;
    CancelRequests();AccountName=Clean;bRetryJoin=false;
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("name"),Clean);Body->SetStringField(TEXT("password"),Password);
    SetState(EFableOnlineState::Authenticating,TEXT("로그인하고 있습니다…"));
    SendJson(TEXT("POST"),TEXT("/v1/sessions"),Body,false,[this](int32 Code,const TSharedPtr<FJsonObject>& Json)
    {
        FString Token;
        if(Code<200||Code>=300){HandleHttpError(Code,TEXT("로그인"));return;}
        if(!Json.IsValid()||!Json->TryGetStringField(TEXT("accessToken"),Token)||!IsSafeToken(Token)){HandleHttpError(502,TEXT("로그인 응답 확인"));return;}
        AccessToken=MoveTemp(Token);SetState(EFableOnlineState::Character,TEXT(""));RefreshCharacter();
    });return true;
}
bool UFableOnlineFlowSubsystem::Register(const FString& Name,const FString& Password,EFableClass InitialClass)
{
    const FString Clean=Name.TrimStartAndEnd();if(uint8(InitialClass)>2||!CanAuthenticate(Clean,Password))return false;
    CancelRequests();AccountName=Clean;PendingRegistrationPassword=Password;bRetryJoin=false;
    auto Body=MakeShared<FJsonObject>();Body->SetStringField(TEXT("name"),Clean);Body->SetStringField(TEXT("password"),Password);Body->SetNumberField(TEXT("initialClass"),int32(InitialClass));
    SetState(EFableOnlineState::Authenticating,TEXT("계정을 만들고 있습니다…"));
    SendJson(TEXT("POST"),TEXT("/v1/accounts"),Body,false,[this](int32 Code,const TSharedPtr<FJsonObject>& Json)
    {
        if(Code<200||Code>=300){HandleHttpError(Code,TEXT("가입"));return;}
        FString RegistrationPassword=MoveTemp(PendingRegistrationPassword);const FString RegistrationName=AccountName;
        SetState(EFableOnlineState::SignedOut,TEXT(""));Authenticate(RegistrationName,RegistrationPassword);RegistrationPassword.Reset();
    });return true;
}
bool UFableOnlineFlowSubsystem::ReadCharacter(const TSharedPtr<FJsonObject>& Json)
{
    if(!Json.IsValid())return false;
    FFableOnlineCharacter Result;FString Id;int64 Value=0;
    if(!Json->TryGetStringField(TEXT("playerId"),Id)||!Number(Json,TEXT("revision"),Value,0,9007199254740991LL))return false;
    FGuid Parsed;if(!FGuid::ParseExact(Id,EGuidFormats::DigitsWithHyphens,Parsed)||!Parsed.IsValid())return false;
    Result.PlayerId=Id.ToLower();Result.Revision=Value;Json->TryGetStringField(TEXT("name"),Result.Name);if(Result.Name.IsEmpty())Result.Name=AccountName;
    if(!Number(Json,TEXT("initialClass"),Value,0,2))return false;Result.HeroClass=EFableClass(Value);
    const TSharedPtr<FJsonValue> Hash=Json->TryGetField(TEXT("profileHash"));
    if(!Hash.IsValid()||(Hash->Type!=EJson::Null&&Hash->Type!=EJson::String))return false;
    if(Hash->Type==EJson::String)
    {
        Result.ProfileHash=Hash->AsString();if(Result.ProfileHash.Len()!=64)return false;
        for(TCHAR C:Result.ProfileHash)if(!((C>='0'&&C<='9')||(C>='a'&&C<='f')))return false;
    }
    Result.bNewCharacter=Hash->Type==EJson::Null;
    const TSharedPtr<FJsonValue> SummaryValue=Json->TryGetField(TEXT("summary"));
    if(!SummaryValue.IsValid()||(SummaryValue->Type!=EJson::Null&&SummaryValue->Type!=EJson::Object))return false;
    const TSharedPtr<FJsonObject>* Summary=nullptr;
    if(Json->TryGetObjectField(TEXT("summary"),Summary)&&Summary&&Summary->IsValid())
    {
        if(!Number(*Summary,TEXT("version"),Value,1,1)||!Number(*Summary,TEXT("heroClass"),Value,0,2))return false;Result.HeroClass=EFableClass(Value);
        if(!Number(*Summary,TEXT("level"),Value,1,20))return false;Result.Level=int32(Value);
        if(!Number(*Summary,TEXT("experience"),Value,0,MAX_int32))return false;Result.Experience=int32(Value);
        if(!Number(*Summary,TEXT("classTier"),Value,1,3))return false;Result.ClassTier=int32(Value);Result.bHasSummary=true;
        if((Result.Level<20?Result.Experience>=Result.Level*100:Result.Experience!=0)
            ||(Result.ClassTier>=2&&Result.Level<2)||(Result.ClassTier>=3&&Result.Level<4))return false;
    }
    else if(Result.bNewCharacter){Result.Level=1;Result.ClassTier=1;}
    const TSharedPtr<FJsonObject>* Wallet=nullptr;
    if(!Json->TryGetObjectField(TEXT("wallet"),Wallet)||!Wallet||!Number(*Wallet,TEXT("gold"),Value,0,1000000))return false;Result.Gold=int32(Value);
    if(!Number(Json,TEXT("inventoryVersion"),Value,0,1))return false;Result.InventoryVersion=int32(Value);
    const TSharedPtr<FJsonObject>* Inventory=nullptr;
    if(Result.InventoryVersion==1)
    {
        if(!Json->TryGetObjectField(TEXT("inventory"),Inventory)||!Inventory||!Inventory->IsValid())return false;
        const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;FString Weapon,Armor;
        if(!(*Inventory)->TryGetArrayField(TEXT("items"),Items)||!Items||!(*Inventory)->TryGetStringField(TEXT("equippedWeaponInstanceId"),Weapon)||!(*Inventory)->TryGetStringField(TEXT("equippedArmorInstanceId"),Armor))return false;
        for(const auto& Entry:*Items)
        {
            const auto Item=Entry.IsValid()?Entry->AsObject():nullptr;FString InstanceId;int64 Location=0,Power=0,Upgrade=0;
            if(!Item.IsValid()||!Item->TryGetStringField(TEXT("instanceId"),InstanceId)||!Number(Item,TEXT("location"),Location,0,1)||!Number(Item,TEXT("power"),Power,0,104)||!Number(Item,TEXT("upgrade"),Upgrade,0,5))return false;
            if(Location==0)++Result.BagCount;else ++Result.StashCount;
            if(InstanceId==Weapon)Result.EquippedWeapon=FString::Printf(TEXT("무기 +%d · 기본 공격 %d"),int32(Upgrade),int32(Power));
            if(InstanceId==Armor)Result.EquippedArmor=FString::Printf(TEXT("갑옷 +%d · 기본 방어 %d"),int32(Upgrade),int32(Power));
        }
    }
    if(Result.bNewCharacter&&(Result.Revision!=0||Result.InventoryVersion!=0||Result.bHasSummary))return false;
    if(Result.InventoryVersion==1&&(Result.EquippedWeapon.IsEmpty()||Result.EquippedArmor.IsEmpty()))return false;
    Character=MoveTemp(Result);return true;
}
bool UFableOnlineFlowSubsystem::RefreshCharacter()
{
    if(!bEnabled||!HasSession()||IsBusy()||bManagedConnection||bAwaitingFrontEnd)return false;
    bRetryJoin=false;SetState(EFableOnlineState::Refreshing,TEXT("캐릭터와 서버 목록을 확인하고 있습니다…"));
    SendJson(TEXT("GET"),TEXT("/v1/me/character"),nullptr,true,[this](int32 Code,const TSharedPtr<FJsonObject>& Json)
    {
        if(Code<200||Code>=300){HandleHttpError(Code,TEXT("캐릭터 조회"));return;}
        if(!ReadCharacter(Json)){HandleHttpError(502,TEXT("캐릭터 정보 확인"));return;}
        FetchServers();
    });return true;
}
void UFableOnlineFlowSubsystem::FetchServers()
{
    SendJson(TEXT("GET"),TEXT("/v1/servers"),nullptr,true,[this](int32 Code,const TSharedPtr<FJsonObject>& Json)
    {
        if(Code<200||Code>=300){HandleHttpError(Code,TEXT("서버 목록 조회"));return;}
        const TArray<TSharedPtr<FJsonValue>>* Entries=nullptr;
        if(!Json.IsValid()||!Json->TryGetArrayField(TEXT("servers"),Entries)||!Entries||Entries->Num()>100){HandleHttpError(502,TEXT("서버 목록 확인"));return;}
        TArray<FFableOnlineServer> Results;TSet<FString> Ids;
        for(const auto& Entry:*Entries)
        {
            const auto Item=Entry.IsValid()?Entry->AsObject():nullptr;FFableOnlineServer Server;FString Address;
            if(!Item.IsValid()||!Item->TryGetStringField(TEXT("id"),Server.Id)||Server.Id.IsEmpty()||Server.Id.Len()>128||Ids.Contains(Server.Id)
                ||!Item->TryGetStringField(TEXT("name"),Server.Name)||Server.Name.Len()>100||!Item->TryGetStringField(TEXT("address"),Address)
                ||!UFableGameInstance::ValidateJoinAddress(Address,Server.Address)){HandleHttpError(502,TEXT("서버 주소 확인"));return;}
            Ids.Add(Server.Id);Results.Add(MoveTemp(Server));
        }
        Servers=MoveTemp(Results);if(!Servers.ContainsByPredicate([this](const FFableOnlineServer& S){return S.Id==SelectedServerId;}))SelectedServerId=Servers.IsEmpty()?FString():Servers[0].Id;
        SetState(EFableOnlineState::Character,Servers.IsEmpty()?TEXT("입장 가능한 서버가 없습니다. 잠시 후 새로고침해 주세요."):TEXT("캐릭터와 서버를 확인한 뒤 입장하세요."));
    });
}
bool UFableOnlineFlowSubsystem::SelectServer(const FString& ServerId)
{
    if(State!=EFableOnlineState::Character||!Servers.ContainsByPredicate([&](const FFableOnlineServer& S){return S.Id==ServerId;}))return false;
    SelectedServerId=ServerId;++ViewRevision;return true;
}
bool UFableOnlineFlowSubsystem::JoinSelectedServer()
{
    auto* GI=Cast<UFableGameInstance>(GetGameInstance());
    if(!bEnabled||!HasSession()||IsBusy()||bManagedConnection||!GI||GI->IsTravelInProgress()||!GI->IsFrontEnd())return false;
    const FFableOnlineServer* Server=Servers.FindByPredicate([this](const FFableOnlineServer& S){return S.Id==SelectedServerId;});
    if(!Server){SetState(EFableOnlineState::Error,TEXT("입장할 서버를 먼저 선택해 주세요."));return false;}
    const FString Address=Server->Address;bRetryJoin=true;++JoinAttemptCount;JoinStartedAt=FPlatformTime::Seconds();
    SetState(EFableOnlineState::Joining,TEXT("접속권을 발급하고 서버에 입장하고 있습니다…"));
    SendJson(TEXT("POST"),TEXT("/v1/join-tickets"),nullptr,true,[this,Address](int32 Code,const TSharedPtr<FJsonObject>& Json)
    {
        if(Code<200||Code>=300){HandleHttpError(Code,TEXT("접속권 발급"));return;}
        FString Ticket;
        if(!Json.IsValid()||!Json->TryGetStringField(TEXT("ticket"),Ticket)||!IsSafeToken(Ticket)){HandleHttpError(502,TEXT("접속권 확인"));return;}
        auto* Instance=Cast<UFableGameInstance>(GetGameInstance());bManagedConnection=true;
        if(!Instance||!Instance->JoinOnlineServer(Address,Ticket)){bManagedConnection=false;Ticket.Reset();SetState(EFableOnlineState::Error,TEXT("게임 화면을 준비하지 못했습니다. 잠시 후 다시 입장해 주세요."));return;}
        Ticket.Reset();
    });return true;
}
bool UFableOnlineFlowSubsystem::RequestLeave()
{
    if(!bEnabled||!bManagedConnection||State==EFableOnlineState::Leaving)return false;
    auto* Controller=Cast<AFablePlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
    if(!Controller||!GetWorld()||GetWorld()->GetNetMode()!=NM_Client)return false;
    bGameMenuOpen=true;LeaveStartedAt=FPlatformTime::Seconds();SetState(EFableOnlineState::Leaving,TEXT("마지막 진행을 저장하고 안전하게 퇴장하고 있습니다…"));
    Controller->UIRequestOnlineLeave();return true;
}
void UFableOnlineFlowSubsystem::OnOnlineLeaveResult(bool bSuccess,const FString& Reason)
{
    if(!bEnabled||!bManagedConnection||(State!=EFableOnlineState::Leaving&&State!=EFableOnlineState::Error))return;
    if(!bSuccess){SetState(EFableOnlineState::Error,TEXT("저장을 확인하지 못해 게임에 남아 있습니다. 안전 퇴장을 다시 시도해 주세요."));return;}
    bAwaitingFrontEnd=true;bRefreshAfterReturn=true;bManagedConnection=false;bGameMenuOpen=false;
    SetState(EFableOnlineState::Refreshing,TEXT("저장을 마쳤습니다. 캐릭터 화면으로 돌아갑니다…"));
    if(auto* GI=Cast<UFableGameInstance>(GetGameInstance()))GI->ReturnToFrontEnd();
}
bool UFableOnlineFlowSubsystem::Logout()
{
    if(!bEnabled||IsBusy()||bManagedConnection||bAwaitingFrontEnd)return false;
    if(!HasSession()){ClearSession();SetState(EFableOnlineState::SignedOut,TEXT("로그아웃했습니다."));return true;}
    SetState(EFableOnlineState::Authenticating,TEXT("로그아웃하고 있습니다…"));
    SendJson(TEXT("POST"),TEXT("/v1/sessions/logout"),nullptr,true,[this](int32 Code,const TSharedPtr<FJsonObject>& Json)
    {
        ClearSession();SetState(EFableOnlineState::SignedOut,Code>=200&&Code<300?TEXT("로그아웃했습니다."):TEXT("이 기기의 로그인을 지웠습니다. 서버에 연결되지 않아 세션 취소는 확인하지 못했습니다."));
    });
    // The outgoing request owns the short-lived header; clear the reusable local session now.
    ClearSession();return true;
}
bool UFableOnlineFlowSubsystem::Retry()
{
    if(!bEnabled||IsBusy())return false;
    if(bManagedConnection)return RequestLeave();
    if(!HasSession()){SetState(EFableOnlineState::SignedOut,TEXT("계정 이름과 비밀번호를 다시 입력해 주세요."));return true;}
    return bRetryJoin?JoinSelectedServer():RefreshCharacter();
}
void UFableOnlineFlowSubsystem::HandleConnectionFailure(bool bTravelFailure)
{
    if(!bEnabled||bAwaitingFrontEnd)return;
    CancelRequests();bManagedConnection=false;bAwaitingFrontEnd=true;bRefreshAfterReturn=false;bGameMenuOpen=false;bRetryJoin=true;
    SetState(EFableOnlineState::Disconnected,bTravelFailure?TEXT("입장 화면을 불러오지 못했습니다. 새 접속권으로 다시 시도할 수 있습니다."):TEXT("서버 연결이 끊어졌습니다. 저장 상태를 확인하거나 다시 접속해 주세요."));
}
void UFableOnlineFlowSubsystem::OnWorldLoaded(UWorld* World)
{
    if(!bEnabled||!World||World->GetGameInstance()!=GetGameInstance())return;
    InputController.Reset();
    if(World->GetNetMode()==NM_DedicatedServer){bEnabled=false;return;}
    if(World->GetNetMode()!=NM_Client&&bAwaitingFrontEnd)
    {
        bAwaitingFrontEnd=false;
        if(bRefreshAfterReturn){bRefreshAfterReturn=false;SetState(EFableOnlineState::Character,TEXT(""));RefreshCharacter();}
    }
    // Explicit local adventure/direct-ticket launchers retain their existing flow.
    else if(!bManagedConnection&&!HasSession()&&(World->GetNetMode()==NM_Client||FString(World->URL.GetOption(TEXT("FablePlay="),TEXT("")))==TEXT("1")))bEnabled=false;
}
void UFableOnlineFlowSubsystem::ToggleInGameMenu()
{
    if(!bEnabled||State!=EFableOnlineState::Playing)return;bGameMenuOpen=!bGameMenuOpen;++ViewRevision;
}
void UFableOnlineFlowSubsystem::UseLocalMode()
{
    if(IsBusy()||bManagedConnection||HasSession())return;bEnabled=false;++ViewRevision;UpdateOverlay();
    // Recreate the original local front-end controller after its Canvas input was suppressed.
    if(auto* GI=Cast<UFableGameInstance>(GetGameInstance()))GI->ReturnToFrontEnd();
}
void UFableOnlineFlowSubsystem::UpdateOverlay()
{
    auto* GI=GetGameInstance();auto* Viewport=GI?GI->GetGameViewportClient():nullptr;
    if(!Viewport)return;
    if(!bEnabled)
    {
        if(Widget.IsValid())
        {
            Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());Widget.Reset();
            if(auto* Controller=GI->GetFirstLocalPlayerController()){Controller->SetInputMode(FInputModeGameAndUI());Controller->bShowMouseCursor=true;}
        }
        return;
    }
    if(!Widget.IsValid()){Widget=SNew(SFableOnlineWidget).Flow(TWeakObjectPtr<UFableOnlineFlowSubsystem>(this));Viewport->AddViewportWidgetContent(Widget.ToSharedRef(),100);}
    const bool Blocking=IsOverlayBlocking();Widget->SetVisibility(Blocking?EVisibility::Visible:EVisibility::Collapsed);
    auto* Controller=GI->GetFirstLocalPlayerController();
    if(Controller&&(InputController.Get()!=Controller||bLastInputBlocking!=Blocking))
    {
        InputController=Controller;bLastInputBlocking=Blocking;
        if(Blocking){Controller->FlushPressedKeys();FInputModeUIOnly Mode;Mode.SetWidgetToFocus(Widget);Controller->SetInputMode(Mode);Controller->bShowMouseCursor=true;}
        else {Controller->SetInputMode(FInputModeGameOnly());Controller->bShowMouseCursor=false;}
    }
}
bool UFableOnlineFlowSubsystem::TickFlow(float DeltaSeconds)
{
    if(!bEnabled){UpdateOverlay();return false;}
    UpdateOverlay();const double Now=FPlatformTime::Seconds();
    if(State==EFableOnlineState::Joining)
    {
        auto* Controller=GetGameInstance()->GetFirstLocalPlayerController();const auto* Profile=Controller?Controller->GetPlayerState<AFablePlayerState>():nullptr;
        if(GetWorld()&&GetWorld()->GetNetMode()==NM_Client&&Profile&&Profile->IsBackendReady())
        {
            if(Profile->GetBackendPlayerId()!=Character.PlayerId)
            {
                HandleConnectionFailure(false);if(auto* GI=Cast<UFableGameInstance>(GetGameInstance()))GI->ReturnToFrontEnd();
            }
            else {bGameMenuOpen=false;SetState(EFableOnlineState::Playing,TEXT("Esc: 온라인 메뉴 · 안전 퇴장"));}
        }
        else if(Now-JoinStartedAt>90)
        {
            HandleConnectionFailure(false);if(auto* GI=Cast<UFableGameInstance>(GetGameInstance()))GI->ReturnToFrontEnd();
        }
    }
    else if(State==EFableOnlineState::Leaving&&Now-LeaveStartedAt>35)
        SetState(EFableOnlineState::Error,TEXT("저장 응답을 기다리는 시간이 길어졌습니다. 게임에 남아 있으니 안전 퇴장을 다시 시도해 주세요."));
    return true;
}
