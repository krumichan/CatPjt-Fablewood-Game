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
    /** JSON 필드가 허용한 정수 값인지 검사하고 읽는다. */
    bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int64& Value, int64 Min, int64 Max)
    {
        double Raw = 0;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Object.IsValid() || !Object->TryGetNumberField(Key, Raw) || !FMath::IsFinite(Raw) || Raw < Min ||
            Raw > Max || Raw != FMath::FloorToDouble(Raw))
        {
            return false;
        }

        Value = int64(Raw);

        return true;
    }

    /** 안전한 토큰 여부를 확인한다. */
    bool IsSafeToken(const FString& Value)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Value.Len() < 16 || Value.Len() > 512)
        {
            return false;
        }

        // 같은 규칙을 적용할 대상을 순회한다.
        for (TCHAR C : Value)
        {
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '-' || C == '_' ||
                  C == '.'))
            {
                return false;
            }
        }

        return true;
    }

    /** 현재 실행에서 온라인 검증을 요청했는지 확인한다. */
    bool OnlineEnabled()
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (IsRunningDedicatedServer() || IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("server")) ||
            FParse::Param(FCommandLine::Get(), TEXT("FableLocal")))
        {
            return false;
        }

        FString Role;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationRole="), Role) ||
            FParse::Param(FCommandLine::Get(), TEXT("FableOnline")))
        {
            return true;
        }

        const TCHAR* Flags[] = {TEXT("FableValidationRole="),         TEXT("FableRPGValidationRole="),
                                TEXT("FableCoopValidationRole="),     TEXT("FableDedicatedValidationRole="),
                                TEXT("FableFrontEndValidationRole="), TEXT("FableFrontEndValidationRun=")};

        // 같은 규칙을 적용할 대상을 순회한다.
        for (const TCHAR* Flag : Flags)
        {
            if (FParse::Value(FCommandLine::Get(), Flag, Role))
            {
                return false;
            }
        }

        return !FParse::Param(FCommandLine::Get(), TEXT("FableArtReview"));
    }

    /** 백엔드 URL의 스킴·호스트·경로가 허용된 접속 조건에 맞는지 검사한다. */
    bool ValidBackendUrl(const FString& Value)
    {
        const bool Secure = Value.StartsWith(TEXT("https://"));

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Secure && !Value.StartsWith(TEXT("http://")))
        {
            return false;
        }

        const FString Authority = Value.Mid(Secure ? 8 : 7);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Authority.Contains(TEXT("/")) || Authority.Contains(TEXT("@")) || Authority.Contains(TEXT("?")) ||
            Authority.Contains(TEXT("#")))
        {
            return false;
        }

        FString Normalized;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!UFableGameInstance::ValidateJoinAddress(Authority, Normalized))
        {
            return false;
        }

        return Secure || Normalized.StartsWith(TEXT("127.0.0.1:")) || Normalized.StartsWith(TEXT("localhost:"));
    }
}

/** 온라인 연결 하위 시스템의 초기 상태를 준비한다. */
void UFableOnlineFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bEnabled = OnlineEnabled();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bEnabled)
    {
        return;
    }

    FString Override;

    // 이 단계에서 온라인 연결 하위 시스템의 초기 상태를 준비한다.
    if (FParse::Value(FCommandLine::Get(), TEXT("FableOnlineBackendUrl="), Override))
    {
        Override.RemoveFromEnd(TEXT("/"));
        if (ValidBackendUrl(Override))
        {
            BackendUrl = Override;
        }
        else
        {
            bConfigurationValid = false;
        }
    }

    UIFont = LoadObject<UFont>(nullptr, TEXT("/Game/UI/Fonts/F_Korean.F_Korean"));
    SetState(bConfigurationValid ? EFableOnlineState::SignedOut : EFableOnlineState::Error,
             bConfigurationValid
                 ? TEXT("계정으로 로그인해 모험을 이어가세요.")
                 : TEXT("계정 서버 주소 설정을 확인해 주세요. HTTPS 또는 이 PC의 로컬 주소만 사용할 수 있습니다."));
    Ticker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UFableOnlineFlowSubsystem::TickFlow), .1f);
}

/** 온라인 연결 하위 시스템의 요청과 콜백을 정리한다. */
void UFableOnlineFlowSubsystem::Deinitialize()
{
    // HTTP 요청·응답과 연결 상태 처리
    FTSTicker::RemoveTicker(Ticker);
    CancelRequests();
    ClearSession();

    // 화면 표시와 사용자 안내 처리
    if (Widget.IsValid() && GetGameInstance() && GetGameInstance()->GetGameViewportClient())
    {
        GetGameInstance()->GetGameViewportClient()->RemoveViewportWidgetContent(Widget.ToSharedRef());
    }

    // 화면 표시와 사용자 안내 처리
    Widget.Reset();
    Super::Deinitialize();
}

/** 처리 중 여부를 확인한다. */
bool UFableOnlineFlowSubsystem::IsBusy() const
{
    return State == EFableOnlineState::Authenticating || State == EFableOnlineState::Refreshing ||
           State == EFableOnlineState::Joining || State == EFableOnlineState::Leaving;
}

/** 오버레이의 입력 차단 여부를 확인한다. */
bool UFableOnlineFlowSubsystem::IsOverlayBlocking() const
{
    return bEnabled && (State != EFableOnlineState::Playing || bGameMenuOpen);
}

/** 온라인 상태의 표시 이름 값을 반환한다. */
FString UFableOnlineFlowSubsystem::GetStateName() const
{
    switch (State)
    {
    case EFableOnlineState::SignedOut:
        return TEXT("SignedOut");
    case EFableOnlineState::Authenticating:
        return TEXT("Authenticating");
    case EFableOnlineState::Refreshing:
        return TEXT("Refreshing");
    case EFableOnlineState::Character:
        return TEXT("Character");
    case EFableOnlineState::Joining:
        return TEXT("Joining");
    case EFableOnlineState::Playing:
        return TEXT("Playing");
    case EFableOnlineState::Leaving:
        return TEXT("Leaving");
    case EFableOnlineState::Disconnected:
        return TEXT("Disconnected");
    default:
        return TEXT("Error");
    }
}

/** 현재 온라인 상태 값을 설정한다. */
void UFableOnlineFlowSubsystem::SetState(EFableOnlineState Value, const FString& Text)
{
    State = Value;
    Message = Text;
    ++ViewRevision;
}

/** 진행 중인 온라인 HTTP 요청의 콜백과 참조를 해제한다. */
void UFableOnlineFlowSubsystem::CancelRequests()
{
    ++RequestSerial;

    // HTTP 요청·응답과 연결 상태 처리
    if (ActiveRequest.IsValid())
    {
        ActiveRequest->OnProcessRequestComplete().Unbind();
        ActiveRequest->CancelRequest();
        ActiveRequest->SetContentAsString(TEXT(""));
        ActiveRequest->SetHeader(TEXT("Authorization"), TEXT(""));
        ActiveRequest.Reset();
    }

    PendingRegistrationPassword.Reset();
}

/** 로그인 토큰과 온라인 표시 상태를 정리한다. */
void UFableOnlineFlowSubsystem::ClearSession()
{
    AccessToken.Reset();
    PendingRegistrationPassword.Reset();
    AccountName.Reset();
    Character = FFableOnlineCharacter();
    Servers.Reset();
    SelectedServerId.Reset();
}

/** 온라인 API 요청의 JSON과 인증 상태를 구성해 비동기로 전송한다. */
void UFableOnlineFlowSubsystem::SendJson(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body,
                                         bool bBearer, FReply Callback)
{
    // HTTP 요청·응답과 연결 상태에 필요한 값 준비
    const uint32 Serial = ++RequestSerial;
    auto Request = FHttpModule::Get().CreateRequest();
    ActiveRequest = Request;
    Request->SetURL(BackendUrl + Path);
    Request->SetVerb(Verb);
    Request->SetTimeout(15.f);
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    // HTTP 요청·응답과 연결 상태 처리
    if (bBearer)
    {
        Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AccessToken);
    }

    // JSON 계약과 검증 기록 처리
    if (Body.IsValid())
    {
        FString Payload;
        FJsonSerializer::Serialize(Body.ToSharedRef(), TJsonWriterFactory<>::Create(&Payload));
        Request->SetContentAsString(Payload);
        Payload.Reset();
    }

    // HTTP 요청·응답과 연결 상태에 필요한 값 준비
    const TWeakObjectPtr<UFableOnlineFlowSubsystem> Weak(this);
    Request->OnProcessRequestComplete().BindLambda(
        [Weak, Serial, bBearer, Callback = MoveTemp(Callback)](FHttpRequestPtr Completed, FHttpResponsePtr Response,
                                                               bool Success) mutable
        {
            Completed->SetContentAsString(TEXT(""));
            Completed->SetHeader(TEXT("Authorization"), TEXT(""));

            // HTTP 요청·응답과 연결 상태 조건 검사
            if (!Weak.IsValid() || Weak->RequestSerial != Serial)
            {
                return;
            }

            // HTTP 요청·응답과 연결 상태에 필요한 값 준비
            auto* Self = Weak.Get();
            Self->ActiveRequest.Reset();
            const int32 Code = Success && Response.IsValid() ? Response->GetResponseCode() : 0;
            Self->LastHttpStatus = Code;

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (Code == 401 && bBearer)
            {
                Self->ClearSession();
                Self->SetState(EFableOnlineState::SignedOut,
                               TEXT("로그인 시간이 만료되었습니다. 비밀번호를 다시 입력해 주세요."));
                return;
            }

            TSharedPtr<FJsonObject> Json;

            // JSON 계약과 검증 기록 처리
            if (Response.IsValid() && Code >= 200 && Code < 300)
            {
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Json);
            }

            Callback(Code, Json);
        });

    // HTTP 요청·응답과 연결 상태 처리
    if (!Request->ProcessRequest())
    {
        Request->OnProcessRequestComplete().Unbind();
        Request->SetContentAsString(TEXT(""));
        Request->SetHeader(TEXT("Authorization"), TEXT(""));
        ActiveRequest.Reset();
        PendingRegistrationPassword.Reset();
        LastHttpStatus = 0;
        HandleHttpError(0, TEXT("요청"));
    }
}

/** HTTP 오류 이벤트를 처리한다. */
void UFableOnlineFlowSubsystem::HandleHttpError(int32 Code, const FString& Context)
{
    PendingRegistrationPassword.Reset();
    FString Text;

    // 이 단계에서 HTTP 오류 이벤트를 처리한다.
    if (Code == 0)
    {
        Text = TEXT("서버에 연결할 수 없습니다. 실행 상태를 확인한 뒤 다시 시도해 주세요.");
    }
    else if (Code == 401)
    {
        Text = TEXT("계정 이름 또는 비밀번호를 확인해 주세요.");
    }
    else if (Code == 409)
    {
        Text = TEXT("이미 사용 중인 계정이거나 다른 접속이 아직 종료되지 않았습니다. 잠시 후 다시 시도해 주세요.");
    }
    else if (Code == 429)
    {
        Text = TEXT("요청이 많습니다. 잠시 기다린 뒤 다시 시도해 주세요.");
    }
    else if (Code == 400)
    {
        Text = TEXT("입력 내용을 확인해 주세요. 계정은 영문·숫자·밑줄 3~32자, 비밀번호는 10~128자입니다.");
    }
    else
    {
        Text = Context + TEXT("을 완료하지 못했습니다. 잠시 후 다시 시도해 주세요.");
    }

    SetState(EFableOnlineState::Error, Text);
}

/** 현재 온라인 상태에서 계정 인증 요청을 시작할 수 있는지 확인한다. */
bool UFableOnlineFlowSubsystem::CanAuthenticate(const FString& Name, const FString& Password)
{
    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || !bConfigurationValid || IsBusy() || bManagedConnection || HasSession())
    {
        return false;
    }

    bool Valid = Name.Len() >= 3 && Name.Len() <= 32 && Password.Len() >= 10 && Password.Len() <= 128;

    // 같은 규칙을 적용할 대상을 순회한다.
    for (TCHAR C : Name)
    {
        Valid &= (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || (C >= '0' && C <= '9') || C == '_';
    }

    // HTTP 요청·응답과 연결 상태 처리
    if (!Valid)
    {
        HandleHttpError(400, TEXT("로그인"));
    }

    return Valid;
}

/** 입력한 계정 정보로 백엔드에 로그인 인증을 요청하고 세션 응답을 처리한다. */
bool UFableOnlineFlowSubsystem::Authenticate(const FString& Name, const FString& Password)
{
    const FString Clean = Name.TrimStartAndEnd();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CanAuthenticate(Clean, Password))
    {
        return false;
    }

    // JSON 계약과 검증 기록 처리
    CancelRequests();
    AccountName = Clean;
    bRetryJoin = false;
    auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("name"), Clean);
    Body->SetStringField(TEXT("password"), Password);
    SetState(EFableOnlineState::Authenticating, TEXT("로그인하고 있습니다…"));
    SendJson(TEXT("POST"), TEXT("/v1/sessions"), Body, false,
             [this](int32 Code, const TSharedPtr<FJsonObject>& Json)
             {
                 FString Token;

                 // JSON 계약과 검증 기록 조건 검사
                 if (Code < 200 || Code >= 300)
                 {
                     HandleHttpError(Code, TEXT("로그인"));
                     return;
                 }
                 if (!Json.IsValid() || !Json->TryGetStringField(TEXT("accessToken"), Token) || !IsSafeToken(Token))
                 {
                     HandleHttpError(502, TEXT("로그인 응답 확인"));
                     return;
                 }

                 AccessToken = MoveTemp(Token);
                 SetState(EFableOnlineState::Character, TEXT(""));
                 RefreshCharacter();
             });

    return true;
}

/** 입력한 계정 정보로 회원 가입을 요청한다. */
bool UFableOnlineFlowSubsystem::Register(const FString& Name, const FString& Password, EFableClass InitialClass)
{
    const FString Clean = Name.TrimStartAndEnd();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (uint8(InitialClass) > 2 || !CanAuthenticate(Clean, Password))
    {
        return false;
    }

    // JSON 계약과 검증 기록 처리
    CancelRequests();
    AccountName = Clean;
    PendingRegistrationPassword = Password;
    bRetryJoin = false;
    auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("name"), Clean);
    Body->SetStringField(TEXT("password"), Password);
    Body->SetNumberField(TEXT("initialClass"), int32(InitialClass));
    SetState(EFableOnlineState::Authenticating, TEXT("계정을 만들고 있습니다…"));
    SendJson(TEXT("POST"), TEXT("/v1/accounts"), Body, false,
             [this](int32 Code, const TSharedPtr<FJsonObject>& Json)
             {
                 if (Code < 200 || Code >= 300)
                 {
                     HandleHttpError(Code, TEXT("가입"));
                     return;
                 }
                 FString RegistrationPassword = MoveTemp(PendingRegistrationPassword);
                 const FString RegistrationName = AccountName;
                 SetState(EFableOnlineState::SignedOut, TEXT(""));
                 Authenticate(RegistrationName, RegistrationPassword);
                 RegistrationPassword.Reset();
             });

    return true;
}

/** 온라인 응답의 캐릭터 요약 필드를 검사해 화면 상태로 읽는다. */
bool UFableOnlineFlowSubsystem::ReadCharacter(const TSharedPtr<FJsonObject>& Json)
{
    // JSON 계약과 검증 기록 조건 검사
    if (!Json.IsValid())
    {
        return false;
    }

    FFableOnlineCharacter Result;
    FString Id;
    int64 Value = 0;

    // 접속 권한·저장 리비전·멱등 키 조건 검사
    if (!Json->TryGetStringField(TEXT("playerId"), Id) || !Number(Json, TEXT("revision"), Value, 0, 9007199254740991LL))
    {
        return false;
    }

    FGuid Parsed;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphens, Parsed) || !Parsed.IsValid())
    {
        return false;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    Result.PlayerId = Id.ToLower();
    Result.Revision = Value;
    Json->TryGetStringField(TEXT("name"), Result.Name);

    // 이 단계에서 온라인 응답의 캐릭터 요약 필드를 검사해 화면 상태로 읽는다.
    if (Result.Name.IsEmpty())
    {
        Result.Name = AccountName;
    }

    // JSON 계약과 검증 기록 조건 검사
    if (!Number(Json, TEXT("initialClass"), Value, 0, 2))
    {
        return false;
    }

    // JSON 계약과 검증 기록 처리
    Result.HeroClass = EFableClass(Value);
    const TSharedPtr<FJsonValue> Hash = Json->TryGetField(TEXT("profileHash"));

    // JSON 계약과 검증 기록 조건 검사
    if (!Hash.IsValid() || (Hash->Type != EJson::Null && Hash->Type != EJson::String))
    {
        return false;
    }

    // JSON 계약과 검증 기록 처리
    if (Hash->Type == EJson::String)
    {
        Result.ProfileHash = Hash->AsString();
        if (Result.ProfileHash.Len() != 64)
        {
            return false;
        }
        for (TCHAR C : Result.ProfileHash)
        {
            if (!((C >= '0' && C <= '9') || (C >= 'a' && C <= 'f')))
            {
                return false;
            }
        }
    }

    // JSON 계약과 검증 기록 처리
    Result.bNewCharacter = Hash->Type == EJson::Null;
    const TSharedPtr<FJsonValue> SummaryValue = Json->TryGetField(TEXT("summary"));

    // JSON 계약과 검증 기록 조건 검사
    if (!SummaryValue.IsValid() || (SummaryValue->Type != EJson::Null && SummaryValue->Type != EJson::Object))
    {
        return false;
    }

    const TSharedPtr<FJsonObject>* Summary = nullptr;

    // JSON 계약과 검증 기록 처리
    if (Json->TryGetObjectField(TEXT("summary"), Summary) && Summary && Summary->IsValid())
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Number(*Summary, TEXT("version"), Value, 1, 1) || !Number(*Summary, TEXT("heroClass"), Value, 0, 2))
        {
            return false;
        }

        Result.HeroClass = EFableClass(Value);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Number(*Summary, TEXT("level"), Value, 1, 20))
        {
            return false;
        }

        Result.Level = int32(Value);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Number(*Summary, TEXT("experience"), Value, 0, MAX_int32))
        {
            return false;
        }

        Result.Experience = int32(Value);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Number(*Summary, TEXT("classTier"), Value, 1, 3))
        {
            return false;
        }

        Result.ClassTier = int32(Value);
        Result.bHasSummary = true;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if ((Result.Level < 20 ? Result.Experience >= Result.Level * 100 : Result.Experience != 0) ||
            (Result.ClassTier >= 2 && Result.Level < 2) || (Result.ClassTier >= 3 && Result.Level < 4))
        {
            return false;
        }
    }
    else if (Result.bNewCharacter)
    {
        Result.Level = 1;
        Result.ClassTier = 1;
    }

    const TSharedPtr<FJsonObject>* Wallet = nullptr;

    // 재화와 작업 비용 조건 검사
    if (!Json->TryGetObjectField(TEXT("wallet"), Wallet) || !Wallet ||
        !Number(*Wallet, TEXT("gold"), Value, 0, 1000000))
    {
        return false;
    }

    Result.Gold = int32(Value);

    // JSON 계약과 검증 기록 조건 검사
    if (!Number(Json, TEXT("inventoryVersion"), Value, 0, 1))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    Result.InventoryVersion = int32(Value);
    const TSharedPtr<FJsonObject>* Inventory = nullptr;

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (Result.InventoryVersion == 1)
    {
        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!Json->TryGetObjectField(TEXT("inventory"), Inventory) || !Inventory || !Inventory->IsValid())
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        FString Weapon, Armor;

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!(*Inventory)->TryGetArrayField(TEXT("items"), Items) || !Items ||
            !(*Inventory)->TryGetStringField(TEXT("equippedWeaponInstanceId"), Weapon) ||
            !(*Inventory)->TryGetStringField(TEXT("equippedArmorInstanceId"), Armor))
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const auto& Entry : *Items)
        {
            // 위치·방향·거리 계산에 필요한 값 준비
            const auto Item = Entry.IsValid() ? Entry->AsObject() : nullptr;
            FString InstanceId;
            int64 Location = 0, Power = 0, Upgrade = 0;

            // 아이템·장비 옵션·인벤토리와 창고 조건 검사
            if (!Item.IsValid() || !Item->TryGetStringField(TEXT("instanceId"), InstanceId) ||
                !Number(Item, TEXT("location"), Location, 0, 1) || !Number(Item, TEXT("power"), Power, 0, 104) ||
                !Number(Item, TEXT("upgrade"), Upgrade, 0, 5))
            {
                return false;
            }

            // 위치·방향·거리 계산 처리
            if (Location == 0)
            {
                ++Result.BagCount;
            }
            else
            {
                ++Result.StashCount;
            }

            // 이 단계에서 온라인 응답의 캐릭터 요약 필드를 검사해 화면 상태로 읽는다.
            if (InstanceId == Weapon)
            {
                Result.EquippedWeapon = FString::Printf(TEXT("무기 +%d · 기본 공격 %d"), int32(Upgrade), int32(Power));
            }

            // 이 단계에서 온라인 응답의 캐릭터 요약 필드를 검사해 화면 상태로 읽는다.
            if (InstanceId == Armor)
            {
                Result.EquippedArmor = FString::Printf(TEXT("갑옷 +%d · 기본 방어 %d"), int32(Upgrade), int32(Power));
            }
        }
    }

    // 접속 권한·저장 리비전·멱등 키 조건 검사
    if (Result.bNewCharacter && (Result.Revision != 0 || Result.InventoryVersion != 0 || Result.bHasSummary))
    {
        return false;
    }
    if (Result.InventoryVersion == 1 && (Result.EquippedWeapon.IsEmpty() || Result.EquippedArmor.IsEmpty()))
    {
        return false;
    }

    Character = MoveTemp(Result);

    return true;
}

/** 인증된 플레이어의 저장된 캐릭터 요약을 다시 조회한다. */
bool UFableOnlineFlowSubsystem::RefreshCharacter()
{
    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || !HasSession() || IsBusy() || bManagedConnection || bAwaitingFrontEnd)
    {
        return false;
    }

    // JSON 계약과 검증 기록 처리
    bRetryJoin = false;
    SetState(EFableOnlineState::Refreshing, TEXT("캐릭터와 서버 목록을 확인하고 있습니다…"));
    SendJson(TEXT("GET"), TEXT("/v1/me/character"), nullptr, true,
             [this](int32 Code, const TSharedPtr<FJsonObject>& Json)
             {
                 if (Code < 200 || Code >= 300)
                 {
                     HandleHttpError(Code, TEXT("캐릭터 조회"));
                     return;
                 }
                 if (!ReadCharacter(Json))
                 {
                     HandleHttpError(502, TEXT("캐릭터 정보 확인"));
                     return;
                 }
                 FetchServers();
             });

    return true;
}

/** 인증된 세션으로 접속 가능한 서버 목록을 조회한다. */
void UFableOnlineFlowSubsystem::FetchServers()
{
    SendJson(TEXT("GET"), TEXT("/v1/servers"), nullptr, true,
             [this](int32 Code, const TSharedPtr<FJsonObject>& Json)
             {
                 // HTTP 요청·응답과 연결 상태 조건 검사
                 if (Code < 200 || Code >= 300)
                 {
                     HandleHttpError(Code, TEXT("서버 목록 조회"));
                     return;
                 }

                 const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;

                 // JSON 계약과 검증 기록 조건 검사
                 if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("servers"), Entries) || !Entries ||
                     Entries->Num() > 100)
                 {
                     HandleHttpError(502, TEXT("서버 목록 확인"));
                     return;
                 }

                 TArray<FFableOnlineServer> Results;
                 TSet<FString> Ids;

                 // 아이템·장비 옵션·인벤토리와 창고 순회
                 for (const auto& Entry : *Entries)
                 {
                     const auto Item = Entry.IsValid() ? Entry->AsObject() : nullptr;
                     FFableOnlineServer Server;
                     FString Address;

                     // 아이템·장비 옵션·인벤토리와 창고 조건 검사
                     if (!Item.IsValid() || !Item->TryGetStringField(TEXT("id"), Server.Id) || Server.Id.IsEmpty() ||
                         Server.Id.Len() > 128 || Ids.Contains(Server.Id) ||
                         !Item->TryGetStringField(TEXT("name"), Server.Name) || Server.Name.Len() > 100 ||
                         !Item->TryGetStringField(TEXT("address"), Address) ||
                         !UFableGameInstance::ValidateJoinAddress(Address, Server.Address))
                     {
                         HandleHttpError(502, TEXT("서버 주소 확인"));
                         return;
                     }

                     Ids.Add(Server.Id);
                     Results.Add(MoveTemp(Server));
                 }

                 Servers = MoveTemp(Results);

                 // 이 단계에서 인증된 세션으로 접속 가능한 서버 목록을 조회한다.
                 if (!Servers.ContainsByPredicate(
                         [this](const FFableOnlineServer& S)
                         {
                             return S.Id == SelectedServerId;
                         }))
                 {
                     SelectedServerId = Servers.IsEmpty() ? FString() : Servers[0].Id;
                 }

                 SetState(EFableOnlineState::Character,
                          Servers.IsEmpty() ? TEXT("입장 가능한 서버가 없습니다. 잠시 후 새로고침해 주세요.")
                                            : TEXT("캐릭터와 서버를 확인한 뒤 입장하세요."));
             });
}

/** 목록에서 접속할 서버를 선택한다. */
bool UFableOnlineFlowSubsystem::SelectServer(const FString& ServerId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (State != EFableOnlineState::Character || !Servers.ContainsByPredicate(
                                                     [&](const FFableOnlineServer& S)
                                                     {
                                                         return S.Id == ServerId;
                                                     }))
    {
        return false;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    SelectedServerId = ServerId;
    ++ViewRevision;

    return true;
}

/** 선택한 서버의 접속 티켓을 발급받아 게임 접속을 시작한다. */
bool UFableOnlineFlowSubsystem::JoinSelectedServer()
{
    auto* GI = Cast<UFableGameInstance>(GetGameInstance());

    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || !HasSession() || IsBusy() || bManagedConnection || !GI || GI->IsTravelInProgress() ||
        !GI->IsFrontEnd())
    {
        return false;
    }

    const FFableOnlineServer* Server = Servers.FindByPredicate(
        [this](const FFableOnlineServer& S)
        {
            return S.Id == SelectedServerId;
        });

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Server)
    {
        SetState(EFableOnlineState::Error, TEXT("입장할 서버를 먼저 선택해 주세요."));
        return false;
    }

    // 서버 시각과 재사용 대기시간에 필요한 값 준비
    const FString Address = Server->Address;
    bRetryJoin = true;
    ++JoinAttemptCount;
    JoinStartedAt = FPlatformTime::Seconds();
    SetState(EFableOnlineState::Joining, TEXT("접속권을 발급하고 서버에 입장하고 있습니다…"));
    SendJson(TEXT("POST"), TEXT("/v1/join-tickets"), nullptr, true,
             [this, Address](int32 Code, const TSharedPtr<FJsonObject>& Json)
             {
                 // HTTP 요청·응답과 연결 상태 조건 검사
                 if (Code < 200 || Code >= 300)
                 {
                     HandleHttpError(Code, TEXT("접속권 발급"));
                     return;
                 }

                 FString Ticket;

                 // JSON 계약과 검증 기록 조건 검사
                 if (!Json.IsValid() || !Json->TryGetStringField(TEXT("ticket"), Ticket) || !IsSafeToken(Ticket))
                 {
                     HandleHttpError(502, TEXT("접속권 확인"));
                     return;
                 }

                 // 체력·마나·보호막·피해량에 필요한 값 준비
                 auto* Instance = Cast<UFableGameInstance>(GetGameInstance());
                 bManagedConnection = true;

                 // 체력·마나·보호막·피해량 처리
                 if (!Instance || !Instance->JoinOnlineServer(Address, Ticket))
                 {
                     bManagedConnection = false;
                     Ticket.Reset();
                     SetState(EFableOnlineState::Error,
                              TEXT("게임 화면을 준비하지 못했습니다. 잠시 후 다시 입장해 주세요."));
                     return;
                 }

                 Ticket.Reset();
             });

    return true;
}

/** 미완료 저장을 보존하는 온라인 이탈 절차를 시작한다. */
bool UFableOnlineFlowSubsystem::RequestLeave()
{
    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || !bManagedConnection || State == EFableOnlineState::Leaving)
    {
        return false;
    }

    auto* Controller = Cast<AFablePlayerController>(GetGameInstance()->GetFirstLocalPlayerController());

    // 서버 권한과 실행 위치 조건 검사
    if (!Controller || !GetWorld() || GetWorld()->GetNetMode() != NM_Client)
    {
        return false;
    }

    // HTTP 요청·응답과 연결 상태 처리
    bGameMenuOpen = true;
    LeaveStartedAt = FPlatformTime::Seconds();
    SetState(EFableOnlineState::Leaving, TEXT("마지막 진행을 저장하고 안전하게 퇴장하고 있습니다…"));
    Controller->UIRequestOnlineLeave();

    return true;
}

/** 게임 서버의 안전 이탈 결과를 온라인 화면과 연결 상태에 반영한다. */
void UFableOnlineFlowSubsystem::OnOnlineLeaveResult(bool bSuccess, const FString& Reason)
{
    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || !bManagedConnection || (State != EFableOnlineState::Leaving && State != EFableOnlineState::Error))
    {
        return;
    }
    if (!bSuccess)
    {
        SetState(EFableOnlineState::Error,
                 TEXT("저장을 확인하지 못해 게임에 남아 있습니다. 안전 퇴장을 다시 시도해 주세요."));
        return;
    }

    // 체력·마나·보호막·피해량 처리
    bAwaitingFrontEnd = true;
    bRefreshAfterReturn = true;
    bManagedConnection = false;
    bGameMenuOpen = false;
    SetState(EFableOnlineState::Refreshing, TEXT("저장을 마쳤습니다. 캐릭터 화면으로 돌아갑니다…"));

    // 현재 월드와 플레이어 참조 처리
    if (auto* GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        GI->ReturnToFrontEnd();
    }
}

/** 접속 종료에 따른 세션과 소유 상태의 정리를 처리한다. */
bool UFableOnlineFlowSubsystem::Logout()
{
    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || IsBusy() || bManagedConnection || bAwaitingFrontEnd)
    {
        return false;
    }
    if (!HasSession())
    {
        ClearSession();
        SetState(EFableOnlineState::SignedOut, TEXT("로그아웃했습니다."));
        return true;
    }

    // JSON 계약과 검증 기록 처리
    SetState(EFableOnlineState::Authenticating, TEXT("로그아웃하고 있습니다…"));
    SendJson(
        TEXT("POST"), TEXT("/v1/sessions/logout"), nullptr, true,
        [this](int32 Code, const TSharedPtr<FJsonObject>& Json)
        {
            ClearSession();
            SetState(
                EFableOnlineState::SignedOut,
                Code >= 200 && Code < 300
                    ? TEXT("로그아웃했습니다.")
                    : TEXT("이 기기의 로그인을 지웠습니다. 서버에 연결되지 않아 세션 취소는 확인하지 못했습니다."));
        });
    // The outgoing request owns the short-lived header; clear the reusable local session now.
    ClearSession();

    return true;
}

/** 현재 오류 상태에서 허용된 온라인 요청을 다시 시작한다. */
bool UFableOnlineFlowSubsystem::Retry()
{
    // 체력·마나·보호막·피해량 조건 검사
    if (!bEnabled || IsBusy())
    {
        return false;
    }
    if (bManagedConnection)
    {
        return RequestLeave();
    }
    if (!HasSession())
    {
        SetState(EFableOnlineState::SignedOut, TEXT("계정 이름과 비밀번호를 다시 입력해 주세요."));
        return true;
    }

    return bRetryJoin ? JoinSelectedServer() : RefreshCharacter();
}

/** 접속 실패 이벤트를 처리한다. */
void UFableOnlineFlowSubsystem::HandleConnectionFailure(bool bTravelFailure)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bEnabled || bAwaitingFrontEnd)
    {
        return;
    }

    // 체력·마나·보호막·피해량 처리
    CancelRequests();
    bManagedConnection = false;
    bAwaitingFrontEnd = true;
    bRefreshAfterReturn = false;
    bGameMenuOpen = false;
    bRetryJoin = true;
    SetState(EFableOnlineState::Disconnected,
             bTravelFailure ? TEXT("입장 화면을 불러오지 못했습니다. 새 접속권으로 다시 시도할 수 있습니다.")
                            : TEXT("서버 연결이 끊어졌습니다. 저장 상태를 확인하거나 다시 접속해 주세요."));
}

/** 월드 로드가 끝나면 진행 중인 전환과 프로필 문맥을 갱신한다. */
void UFableOnlineFlowSubsystem::OnWorldLoaded(UWorld* World)
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (!bEnabled || !World || World->GetGameInstance() != GetGameInstance())
    {
        return;
    }

    InputController.Reset();

    // 서버 권한과 실행 위치 조건 검사
    if (World->GetNetMode() == NM_DedicatedServer)
    {
        bEnabled = false;
        return;
    }

    // 서버 권한과 실행 위치 처리
    if (World->GetNetMode() != NM_Client && bAwaitingFrontEnd)
    {
        bAwaitingFrontEnd = false;
        if (bRefreshAfterReturn)
        {
            bRefreshAfterReturn = false;
            SetState(EFableOnlineState::Character, TEXT(""));
            RefreshCharacter();
        }
    }
    // Explicit local adventure/direct-ticket launchers retain their existing flow.
    else if (!bManagedConnection && !HasSession() &&
             (World->GetNetMode() == NM_Client ||
              FString(World->URL.GetOption(TEXT("FablePlay="), TEXT(""))) == TEXT("1")))
    {
        bEnabled = false;
    }
}

/** 플레이 중인 온라인 메뉴의 표시 상태를 전환한다. */
void UFableOnlineFlowSubsystem::ToggleInGameMenu()
{
    if (!bEnabled || State != EFableOnlineState::Playing)
    {
        return;
    }
    bGameMenuOpen = !bGameMenuOpen;
    ++ViewRevision;
}

/** 온라인 연결 상태를 정리하고 로컬 게임 모드로 전환한다. */
void UFableOnlineFlowSubsystem::UseLocalMode()
{
    // 체력·마나·보호막·피해량 조건 검사
    if (IsBusy() || bManagedConnection || HasSession())
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    bEnabled = false;
    ++ViewRevision;
    UpdateOverlay();
    // Recreate the original local front-end controller after its Canvas input was suppressed.

    // 현재 월드와 플레이어 참조 처리
    if (auto* GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        GI->ReturnToFrontEnd();
    }
}

/** 온라인 진행·오류 상태에 맞게 오버레이 표시를 갱신한다. */
void UFableOnlineFlowSubsystem::UpdateOverlay()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    auto* GI = GetGameInstance();
    auto* Viewport = GI ? GI->GetGameViewportClient() : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Viewport)
    {
        return;
    }

    // 화면 표시와 사용자 안내 처리
    if (!bEnabled)
    {
        if (Widget.IsValid())
        {
            Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
            Widget.Reset();
            if (auto* Controller = GI->GetFirstLocalPlayerController())
            {
                Controller->SetInputMode(FInputModeGameAndUI());
                Controller->bShowMouseCursor = true;
            }
        }
        return;
    }

    // 화면 표시와 사용자 안내 처리
    if (!Widget.IsValid())
    {
        Widget = SNew(SFableOnlineWidget).Flow(TWeakObjectPtr<UFableOnlineFlowSubsystem>(this));
        Viewport->AddViewportWidgetContent(Widget.ToSharedRef(), 100);
    }

    // 화면 표시와 사용자 안내에 필요한 값 준비
    const bool Blocking = IsOverlayBlocking();
    Widget->SetVisibility(Blocking ? EVisibility::Visible : EVisibility::Collapsed);
    auto* Controller = GI->GetFirstLocalPlayerController();

    // 화면 표시와 사용자 안내 처리
    if (Controller && (InputController.Get() != Controller || bLastInputBlocking != Blocking))
    {
        InputController = Controller;
        bLastInputBlocking = Blocking;
        if (Blocking)
        {
            Controller->FlushPressedKeys();
            FInputModeUIOnly Mode;
            Mode.SetWidgetToFocus(Widget);
            Controller->SetInputMode(Mode);
            Controller->bShowMouseCursor = true;
        }
        else
        {
            Controller->SetInputMode(FInputModeGameOnly());
            Controller->bShowMouseCursor = false;
        }
    }
}

/** 현재 온라인 접속·게임 진입·저장·이탈 검증 단계를 진행한다. */
bool UFableOnlineFlowSubsystem::TickFlow(float DeltaSeconds)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bEnabled)
    {
        UpdateOverlay();
        return false;
    }

    // 서버 시각과 재사용 대기시간 처리
    UpdateOverlay();
    const double Now = FPlatformTime::Seconds();

    // 서버 권한과 실행 위치 처리
    if (State == EFableOnlineState::Joining)
    {
        auto* Controller = GetGameInstance()->GetFirstLocalPlayerController();
        const auto* Profile = Controller ? Controller->GetPlayerState<AFablePlayerState>() : nullptr;
        if (GetWorld() && GetWorld()->GetNetMode() == NM_Client && Profile && Profile->IsBackendReady())
        {
            if (Profile->GetBackendPlayerId() != Character.PlayerId)
            {
                HandleConnectionFailure(false);
                if (auto* GI = Cast<UFableGameInstance>(GetGameInstance()))
                {
                    GI->ReturnToFrontEnd();
                }
            }
            else
            {
                bGameMenuOpen = false;
                SetState(EFableOnlineState::Playing, TEXT("Esc: 온라인 메뉴 · 안전 퇴장"));
            }
        }
        else if (Now - JoinStartedAt > 90)
        {
            HandleConnectionFailure(false);
            if (auto* GI = Cast<UFableGameInstance>(GetGameInstance()))
            {
                GI->ReturnToFrontEnd();
            }
        }
    }
    else if (State == EFableOnlineState::Leaving && Now - LeaveStartedAt > 35)
    {
        SetState(EFableOnlineState::Error,
                 TEXT("저장 응답을 기다리는 시간이 길어졌습니다. 게임에 남아 있으니 안전 퇴장을 다시 시도해 주세요."));
    }

    return true;
}
