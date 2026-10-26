#include "FableGameInstance.h"

#include "FableSaveGame.h"
#include "FableProfileSchema.h"
#include "FableProfileStorage.h"
#include "FableOnlineFlowSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "String/LexFromString.h"
#include "Misc/Parse.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    const FName AdventureMap(TEXT("/Game/Maps/ForestClearing"));
    const FString LegacyPrefix(TEXT("Fablewood_RPG"));

    /** ASCII 영숫자 여부를 확인한다. */
    bool IsASCIIAlnum(TCHAR Character)
    {
        return (Character >= TEXT('a') && Character <= TEXT('z')) ||
               (Character >= TEXT('A') && Character <= TEXT('Z')) || (Character >= TEXT('0') && Character <= TEXT('9'));
    }

    /** 격리 실행 이름 여부를 확인한다. */
    bool IsSafeRun(const FString& Value)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Value.IsEmpty() || Value.Len() > 40)
        {
            return false;
        }

        // 같은 규칙을 적용할 대상을 순회한다.
        for (TCHAR Character : Value)
        {
            if (!IsASCIIAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
            {
                return false;
            }
        }

        return true;
    }

    /** 원본 파일을 바꾸지 않고 메뉴 표시용 후보만 만든다. */
    UFableSaveGame* ReadProfileCandidate(const FString& Slot)
    {
        TArray<uint8> OriginalBytes;
        return FableProfileStorage::ReadCandidate(Slot, OriginalBytes);
    }

    /** 읽을 수 있는 저장 프로필 여부를 확인한다. */
    bool IsReadableProfile(UFableSaveGame* Profile)
    {
        return Profile && Profile->MigrateAndValidate();
    }

    /** 자동 플레이 검증 여부를 확인한다. */
    bool IsAutomationGameplay()
    {
        FString Role;
        return FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), Role) ||
               FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRole="), Role) ||
               FParse::Param(FCommandLine::Get(), TEXT("FableArtReview"));
    }
}

/** 게임 인스턴스의 저장·화면 전환 상태와 콜백을 초기화한다. */
void UFableGameInstance::Init()
{
    Super::Init();

    // 프로필과 저장 스냅숏 처리
    if (FParse::Value(FCommandLine::Get(), TEXT("FableFrontEndValidationRun="), ValidationRun))
    {
        if (IsSafeRun(ValidationRun))
        {
            ProfileIndexSlot = TEXT("Fablewood_ProfileIndex_Test_") + ValidationRun;
        }
        else
        {
            // A malformed test flag must never fall back to the production index.
            bInvalidValidationRun = true;
            ProfileIndexSlot = TEXT("Fablewood_ProfileIndex_Test_Invalid");
            FrontEndError = TEXT("시작 화면 검증 이름이 올바르지 않습니다. 일반 저장은 변경하지 않았습니다.");
        }
    }

    RefreshContinueState();

    // 이 단계에서 게임 인스턴스의 저장·화면 전환 상태와 콜백을 초기화한다.
    if (GEngine)
    {
        NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UFableGameInstance::HandleNetworkFailure);
        TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UFableGameInstance::HandleTravelFailure);
    }

    // 현재 월드와 플레이어 참조 처리
    WorldLoadedHandle =
        FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UFableGameInstance::OnWorldLoaded);
    FrontEndTicker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UFableGameInstance::TickFrontEnd), 0.25f);
}

/** 게임 인스턴스가 등록한 이벤트와 연결 상태를 정리한다. */
void UFableGameInstance::Shutdown()
{
    // 현재 월드와 플레이어 참조 처리
    CommitPendingProfile();
    FTSTicker::RemoveTicker(FrontEndTicker);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(WorldLoadedHandle);

    // 이 단계에서 게임 인스턴스가 등록한 이벤트와 연결 상태를 정리한다.
    if (GEngine)
    {
        GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
        GEngine->OnTravelFailure().Remove(TravelFailureHandle);
    }

    Super::Shutdown();
}

/** 저장 이름 접두사 유효성 여부를 확인한다. */
bool UFableGameInstance::IsValidProfilePrefix(const FString& Prefix)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Prefix == LegacyPrefix)
    {
        return true;
    }
    if (!Prefix.StartsWith(TEXT("Fablewood_RPG_"), ESearchCase::CaseSensitive) || Prefix.Len() > 80)
    {
        return false;
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (TCHAR Character : Prefix)
    {
        if (!IsASCIIAlnum(Character) && Character != TEXT('_') && Character != TEXT('-'))
        {
            return false;
        }
    }

    return true;
}

/** 시작 화면 여부를 확인한다. */
bool UFableGameInstance::IsFrontEnd() const
{
    const UWorld* World = GetWorld();
    // A joined client can retain its own start-screen URL during connection setup.

    // 서버 권한과 실행 위치 조건 검사
    if (World && World->GetNetMode() == NM_Client)
    {
        return false;
    }
    if (IsAutomationGameplay())
    {
        return false;
    }

    return !World || FString(World->URL.GetOption(TEXT("FablePlay="), TEXT(""))) != TEXT("1");
}

/** 이어하기 상태 상태를 다시 확인해 갱신한다. */
void UFableGameInstance::RefreshContinueState()
{
    // 프로필과 저장 스냅숏 처리
    ContinueProfilePrefix.Reset();
    ContinueSummary = TEXT("이어할 저장이 없습니다. 새 게임으로 모험을 시작하세요.");

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bInvalidValidationRun)
    {
        return;
    }

    // 프로필과 저장 스냅숏 처리
    if (UGameplayStatics::DoesSaveGameExist(ProfileIndexSlot, 0))
    {
        const UFableProfileIndexSave* Index =
            Cast<UFableProfileIndexSave>(UGameplayStatics::LoadGameFromSlot(ProfileIndexSlot, 0));
        if (Index && Index->Version == 1 && IsValidProfilePrefix(Index->LatestProfilePrefix) &&
            UGameplayStatics::DoesSaveGameExist(Index->LatestProfilePrefix + TEXT("_Host"), 0))
        {
            ContinueProfilePrefix = Index->LatestProfilePrefix;
        }
    }
    // Test runs never inspect or select the player's production profile.

    // 프로필과 저장 스냅숏 처리
    if (ContinueProfilePrefix.IsEmpty() && ValidationRun.IsEmpty() &&
        UGameplayStatics::DoesSaveGameExist(LegacyPrefix + TEXT("_Host"), 0))
    {
        ContinueProfilePrefix = LegacyPrefix;
    }

    // 프로필과 저장 스냅숏 조건 검사
    if (ContinueProfilePrefix.IsEmpty())
    {
        return;
    }

    UFableSaveGame* Profile = ReadProfileCandidate(ContinueProfilePrefix + TEXT("_Host"));

    // 재화와 작업 비용 처리
    if (IsReadableProfile(Profile))
    {
        const int32 Tier = Profile->Version >= 3 ? Profile->ClassProgress[int32(Profile->HeroClass)].Tier : 1;
        ContinueSummary = FString::Printf(TEXT("레벨 %d %s · 보유 골드 %d"), Profile->Level,
                                          *FableRPG::AdvancedClassName(Profile->HeroClass, Tier), Profile->Gold);
    }
    else
    {
        ContinueSummary = TEXT("저장 파일을 읽을 수 없습니다. 원본은 그대로 보존되어 있습니다.");
    }
}

/** 이어하기 게임의 존재 여부를 확인한다. */
bool UFableGameInstance::HasContinueGame() const
{
    return !bInvalidValidationRun && !ContinueProfilePrefix.IsEmpty() &&
           UGameplayStatics::DoesSaveGameExist(ContinueProfilePrefix + TEXT("_Host"), 0);
}

/** 이어하기에 표시할 저장 요약 값을 반환한다. */
FString UFableGameInstance::GetContinueSummary() const
{
    return HasContinueGame() ? ContinueSummary : TEXT("이어할 저장이 없습니다. 새 게임으로 모험을 시작하세요.");
}

/** 중복 전환과 현재 화면 상태를 확인해 월드 전환 허용 여부를 반환한다. */
bool UFableGameInstance::CanStartTravel()
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (bInvalidValidationRun)
    {
        FrontEndError = TEXT("검증 이름이 올바르지 않아 저장을 시작하지 않았습니다.");
        return false;
    }
    if (!GetWorld() || !GetFirstLocalPlayerController())
    {
        FrontEndError = TEXT("시작 화면을 준비하고 있습니다. 잠시 후 다시 선택해 주세요.");
        return false;
    }
    if (bTravelInProgress || bReturnQueued)
    {
        FrontEndError = TEXT("화면을 이동하고 있습니다. 잠시 기다려 주세요.");
        return false;
    }
    if (!IsFrontEnd())
    {
        FrontEndError = TEXT("새 모험은 시작 화면에서 선택해 주세요.");
        return false;
    }

    return true;
}

/** 선택한 프로필과 직업으로 모험 월드를 연다. */
bool UFableGameInstance::OpenAdventure(const FString& Prefix, EFableClass HeroClass, bool bHost)
{
    // 프로필과 저장 스냅숏 조건 검사
    if (!IsValidProfilePrefix(Prefix) || uint8(HeroClass) > uint8(EFableClass::Archer))
    {
        return false;
    }

    // 프로필과 저장 스냅숏 처리
    ActiveProfilePrefix = Prefix;
    FrontEndError.Reset();
    bTravelInProgress = true;
    bRecoveringToFrontEnd = false;
    FString Options = FString::Printf(TEXT("FablePlay=1?FableProfile=%s?FableClass=%d"), *Prefix, int32(HeroClass));

    // 이 단계에서 선택한 프로필과 직업으로 모험 월드를 연다.
    if (bHost)
    {
        Options += TEXT("?listen");
    }

    UGameplayStatics::OpenLevel(this, AdventureMap, true, Options);

    return true;
}

/** 새 모험의 프로필과 시작 직업을 준비해 게임에 진입한다. */
bool UFableGameInstance::StartNewGame(EFableClass HeroClass, bool bHost)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CanStartTravel())
    {
        return false;
    }
    if (uint8(HeroClass) > uint8(EFableClass::Archer))
    {
        FrontEndError = TEXT("전사, 마법사, 궁수 중 하나를 선택해 주세요.");
        return false;
    }

    FString Prefix;

    // 프로필과 저장 스냅숏 순회
    for (int32 Attempt = 0; Attempt < 4; ++Attempt)
    {
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        Prefix = ValidationRun.IsEmpty() ? TEXT("Fablewood_RPG_") + Id
                                         : TEXT("Fablewood_RPG_FrontTest_") + ValidationRun + TEXT("_") + Id.Left(8);
        if (!UGameplayStatics::DoesSaveGameExist(Prefix + TEXT("_Host"), 0) &&
            !UGameplayStatics::DoesSaveGameExist(Prefix + TEXT("_Guest"), 0))
        {
            break;
        }
        Prefix.Reset();
    }

    // 프로필과 저장 스냅숏 조건 검사
    if (Prefix.IsEmpty() || !IsValidProfilePrefix(Prefix))
    {
        FrontEndError = TEXT("새 저장 이름을 만들지 못했습니다. 기존 저장은 변경하지 않았습니다.");
        return false;
    }
    // Do not replace the continuation pointer until the new character actually saves.

    // 프로필과 저장 스냅숏 처리
    PendingProfilePrefix = Prefix;
    PendingProfileSeconds = 0;

    // 프로필과 저장 스냅숏 조건 검사
    if (!OpenAdventure(Prefix, HeroClass, bHost))
    {
        PendingProfilePrefix.Reset();
        return false;
    }

    return true;
}

/** 검증된 저장 프로필로 이전 모험을 이어간다. */
bool UFableGameInstance::ContinueGame(bool bHost)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CanStartTravel())
    {
        return false;
    }

    RefreshContinueState();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!HasContinueGame())
    {
        FrontEndError = TEXT("이어할 저장을 찾지 못했습니다. 새 게임을 선택해 주세요.");
        return false;
    }

    UFableSaveGame* Profile = ReadProfileCandidate(ContinueProfilePrefix + TEXT("_Host"));

    // 프로필과 저장 스냅숏 조건 검사
    if (!IsReadableProfile(Profile))
    {
        FrontEndError = TEXT("저장 파일을 읽을 수 없습니다. 원본을 덮어쓰지 않고 보존했습니다.");
        return false;
    }

    return OpenAdventure(ContinueProfilePrefix, Profile->HeroClass, bHost);
}

/** 접속 주소 값과 허용 조건을 검사한다. */
bool UFableGameInstance::ValidateJoinAddress(const FString& Address, FString& NormalizedAddress)
{
    NormalizedAddress.Reset();
    const FString Value = Address.TrimStartAndEnd();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Value.IsEmpty() || Value.Len() > 259)
    {
        return false;
    }

    int32 Colon = INDEX_NONE;

    // 같은 규칙을 적용할 대상을 순회한다.
    for (int32 I = 0; I < Value.Len(); ++I)
    {
        const TCHAR Character = Value[I];
        if (Character == TEXT(':'))
        {
            if (Colon != INDEX_NONE)
            {
                return false;
            }
            Colon = I;
        }
        else if (!IsASCIIAlnum(Character) && Character != TEXT('.') && Character != TEXT('-'))
        {
            return false;
        }
    }

    const FString Host = Colon == INDEX_NONE ? Value : Value.Left(Colon);
    const FString PortText = Colon == INDEX_NONE ? TEXT("7788") : Value.Mid(Colon + 1);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Host.IsEmpty() || Host.Len() > 253 || PortText.IsEmpty() || PortText.Len() > 5)
    {
        return false;
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (TCHAR Character : PortText)
    {
        if (Character < TEXT('0') || Character > TEXT('9'))
        {
            return false;
        }
    }

    int32 Port = 0;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!LexTryParseString(Port, *PortText) || Port < 1 || Port > 65535)
    {
        return false;
    }
    if (Host.StartsWith(TEXT(".")) || Host.EndsWith(TEXT(".")) || Host.Contains(TEXT("..")))
    {
        return false;
    }

    // 화면 표시와 사용자 안내에 필요한 값 준비
    TArray<FString> Labels;
    Host.ParseIntoArray(Labels, TEXT("."), false);
    bool bDigitsAndDots = true;

    // 같은 규칙을 적용할 대상을 순회한다.
    for (TCHAR Character : Host)
    {
        if (Character != TEXT('.') && (Character < TEXT('0') || Character > TEXT('9')))
        {
            bDigitsAndDots = false;
        }
    }

    // 화면 표시와 사용자 안내 처리
    if (bDigitsAndDots)
    {
        if (Labels.Num() != 4)
        {
            return false;
        }
        for (const FString& Label : Labels)
        {
            if (Label.IsEmpty() || Label.Len() > 3 || (Label.Len() > 1 && Label[0] == TEXT('0')))
            {
                return false;
            }
            int32 Octet = -1;
            if (!LexTryParseString(Octet, *Label) || Octet < 0 || Octet > 255)
            {
                return false;
            }
        }
    }
    else
    {
        for (const FString& Label : Labels)
        {
            if (Label.IsEmpty() || Label.Len() > 63 || !IsASCIIAlnum(Label[0]) || !IsASCIIAlnum(Label[Label.Len() - 1]))
            {
                return false;
            }
            for (TCHAR Character : Label)
            {
                if (!IsASCIIAlnum(Character) && Character != TEXT('-'))
                {
                    return false;
                }
            }
        }
    }

    NormalizedAddress = Host.ToLower() + TEXT(":") + FString::FromInt(Port);

    return true;
}

/** 접속 주소를 검사한 뒤 해당 멀티플레이 게임에 접속한다. */
bool UFableGameInstance::JoinGame(const FString& Address)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CanStartTravel())
    {
        return false;
    }

    FString Target;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!ValidateJoinAddress(Address, Target))
    {
        FrontEndError = TEXT("주소는 127.0.0.1:7788 또는 PC이름:포트 형식으로 입력해 주세요.");
        return false;
    }

    APlayerController* Controller = GetFirstLocalPlayerController();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Controller)
    {
        return false;
    }

    // 프로필과 저장 스냅숏 처리
    FrontEndError.Reset();
    ActiveProfilePrefix.Reset();
    PendingProfilePrefix.Reset();
    bTravelInProgress = true;
    bRecoveringToFrontEnd = false;
    Controller->ClientTravel(Target, TRAVEL_Absolute);

    return true;
}

/** 인증된 온라인 서버의 접속 문맥을 준비하고 월드를 전환한다. */
bool UFableGameInstance::JoinOnlineServer(const FString& Address, const FString& OneUseTicket)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CanStartTravel())
    {
        return false;
    }

    FString Target;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!ValidateJoinAddress(Address, Target) || OneUseTicket.Len() < 16 || OneUseTicket.Len() > 512)
    {
        return false;
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (TCHAR C : OneUseTicket)
    {
        if (!IsASCIIAlnum(C) && C != TEXT('-') && C != TEXT('_') && C != TEXT('.'))
        {
            return false;
        }
    }

    APlayerController* Controller = GetFirstLocalPlayerController();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Controller)
    {
        return false;
    }

    // 프로필과 저장 스냅숏 처리
    ActiveProfilePrefix.Reset();
    PendingProfilePrefix.Reset();
    FrontEndError.Reset();
    bTravelInProgress = true;
    bRecoveringToFrontEnd = false;
    // Only the short-lived one-use ticket travels; the account token/password stay in RAM.
    Controller->ClientTravel(Target + TEXT("?FableTicket=") + OneUseTicket + FableProfileSchema::CompatibilityOptions(),
                             TRAVEL_Absolute);

    return true;
}

/** 월드 전환 전에 남은 프로필 저장을 처리한다. */
bool UFableGameInstance::CommitPendingProfile()
{
    // 프로필과 저장 스냅숏 조건 검사
    if (PendingProfilePrefix.IsEmpty())
    {
        return true;
    }
    if (bInvalidValidationRun || !IsValidProfilePrefix(PendingProfilePrefix))
    {
        return false;
    }
    if (!UGameplayStatics::DoesSaveGameExist(PendingProfilePrefix + TEXT("_Host"), 0))
    {
        return false;
    }

    UFableSaveGame* SavedProfile = FableProfileStorage::ReadVerifiedCurrent(PendingProfilePrefix + TEXT("_Host"));

    // 프로필과 저장 스냅숏 조건 검사
    if (!IsReadableProfile(SavedProfile))
    {
        return false;
    }

    UFableProfileIndexSave* Index = nullptr;

    // 프로필과 저장 스냅숏 처리
    if (UGameplayStatics::DoesSaveGameExist(ProfileIndexSlot, 0))
    {
        Index = Cast<UFableProfileIndexSave>(UGameplayStatics::LoadGameFromSlot(ProfileIndexSlot, 0));
        if (!Index || Index->Version != 1)
        {
            FrontEndError = TEXT("새 모험은 저장했지만 기존 저장 목록을 읽지 못했습니다. 목록 원본은 보존했습니다.");
            PendingProfilePrefix.Reset();
            return false;
        }
    }
    else
    {
        Index = NewObject<UFableProfileIndexSave>();
    }

    Index->LatestProfilePrefix = PendingProfilePrefix;

    // 프로필과 저장 스냅숏 조건 검사
    if (!UGameplayStatics::SaveGameToSlot(Index, ProfileIndexSlot, 0))
    {
        FrontEndError = TEXT("새 모험의 이어하기 목록을 저장하지 못했습니다. 캐릭터 저장 파일은 보존했습니다.");
        return false;
    }

    // 프로필과 저장 스냅숏 처리
    UE_LOG(LogTemp, Display, TEXT("FABLE_FRONTEND_PROFILE_INDEX slot=%s prefix=%s"), *ProfileIndexSlot,
           *PendingProfilePrefix);
    PendingProfilePrefix.Reset();
    RefreshContinueState();

    return true;
}

/** 월드 로드가 끝나면 진행 중인 전환과 프로필 문맥을 갱신한다. */
void UFableGameInstance::OnWorldLoaded(UWorld* LoadedWorld)
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (!LoadedWorld || LoadedWorld->GetGameInstance() != this)
    {
        return;
    }

    bTravelInProgress = false;

    // 서버 권한과 실행 위치 처리
    if (IsFrontEnd())
    {
        ActiveProfilePrefix.Reset();
        bRecoveringToFrontEnd = false;
        bReturnQueued = false;
        RefreshContinueState();
    }
    else if (LoadedWorld->GetNetMode() != NM_Client)
    {
        const FString Prefix(LoadedWorld->URL.GetOption(TEXT("FableProfile="), TEXT("")));
        ActiveProfilePrefix = IsValidProfilePrefix(Prefix) ? Prefix : LegacyPrefix;
    }

    CommitPendingProfile();

    // 현재 월드와 플레이어 참조 처리
    if (auto* Online = GetSubsystem<UFableOnlineFlowSubsystem>())
    {
        Online->OnWorldLoaded(LoadedWorld);
    }
}

/** 다음 안전한 갱신 시점에 시작 화면으로 복귀하도록 예약한다. */
void UFableGameInstance::QueueFrontEndReturn(const FString& ErrorMessage)
{
    // 이 단계에서 대기열 시작 화면 복귀에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (!ErrorMessage.IsEmpty())
    {
        FrontEndError = ErrorMessage;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bRecoveringToFrontEnd)
    {
        return;
    }

    bRecoveringToFrontEnd = true;
    bReturnQueued = true;
    bTravelInProgress = true;
}

/** 게임 이탈과 저장 상태를 정리한 뒤 시작 화면으로 돌아간다. */
void UFableGameInstance::ReturnToFrontEnd()
{
    CommitPendingProfile();
    FrontEndError.Reset();
    QueueFrontEndReturn(FString());
}

/** 시작 화면의 상태와 필요한 화면 전환을 갱신한다. */
bool UFableGameInstance::TickFrontEnd(float DeltaSeconds)
{
    // 현재 월드와 플레이어 참조 처리
    if (bReturnQueued && GetWorld())
    {
        bReturnQueued = false;
        // Absolute local travel drops listen, address and profile URL options.
        // It never attempts to reconnect to the failed remote address.
        UGameplayStatics::OpenLevel(this, AdventureMap, true, FString());
        return true;
    }

    // 서버 시각과 재사용 대기시간 처리
    if (!PendingProfilePrefix.IsEmpty())
    {
        PendingProfileSeconds += DeltaSeconds;
        if (!CommitPendingProfile() && PendingProfileSeconds > 30.0f)
        {
            FrontEndError = TEXT("새 저장을 확인하지 못했습니다. 이전 이어하기 기록은 변경하지 않았습니다.");
            PendingProfilePrefix.Reset();
        }
    }

    return true;
}

/** 네트워크 실패의 연결 상태와 사용자 안내를 갱신한다. */
void UFableGameInstance::HandleNetworkFailure(UWorld* FailedWorld, UNetDriver* Driver,
                                              ENetworkFailure::Type FailureType, const FString& ErrorString)
{
    // 서버 권한과 실행 위치 조건 검사
    if (FailedWorld && FailedWorld->GetGameInstance() != this)
    {
        return;
    }
    // A guest leaving must not close the host's adventure.
    if (FailedWorld && FailedWorld->GetNetMode() == NM_ListenServer &&
        (FailureType == ENetworkFailure::ConnectionLost || FailureType == ENetworkFailure::ConnectionTimeout))
    {
        return;
    }
    if (bRecoveringToFrontEnd)
    {
        return;
    }
    if (auto* Online = GetSubsystem<UFableOnlineFlowSubsystem>();
        Online && Online->IsEnabled() && Online->IsManagingConnection())
    {
        Online->HandleConnectionFailure(false);
        QueueFrontEndReturn(TEXT("서버 연결이 끊어졌습니다. 온라인 화면에서 다시 접속할 수 있습니다."));
        return;
    }

    // 프로필과 저장 스냅숏 처리
    UE_LOG(LogTemp, Warning, TEXT("FABLE_FRONTEND_NETWORK_FAILURE type=%d detail=%s"), int32(FailureType),
           *ErrorString);
    CommitPendingProfile();
    QueueFrontEndReturn(
        TEXT("접속하지 못했거나 연결이 끊어졌습니다. 주소와 방 실행 상태를 확인해 주세요. 저장은 그대로 보존됩니다."));
}

/** 월드 전환 실패를 정리하고 시작 화면 복귀를 준비한다. */
void UFableGameInstance::HandleTravelFailure(UWorld* FailedWorld, ETravelFailure::Type FailureType,
                                             const FString& ErrorString)
{
    // 체력·마나·보호막·피해량 조건 검사
    if (FailedWorld && FailedWorld->GetGameInstance() != this)
    {
        return;
    }
    if (auto* Online = GetSubsystem<UFableOnlineFlowSubsystem>();
        Online && Online->IsEnabled() && Online->IsManagingConnection() && !bRecoveringToFrontEnd)
    {
        Online->HandleConnectionFailure(true);
        QueueFrontEndReturn(TEXT("게임 화면으로 이동하지 못했습니다. 온라인 화면에서 다시 시도해 주세요."));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("FABLE_FRONTEND_TRAVEL_FAILURE type=%d detail=%s"), int32(FailureType), *ErrorString);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bRecoveringToFrontEnd)
    {
        bReturnQueued = false;
        bTravelInProgress = false;
        FrontEndError = TEXT("시작 화면을 불러오지 못했습니다. 게임을 다시 실행해 주세요. 저장은 변경하지 않았습니다.");
        return;
    }

    // 프로필과 저장 스냅숏 처리
    CommitPendingProfile();
    QueueFrontEndReturn(TEXT("게임 화면으로 이동하지 못했습니다. 저장을 보존하고 시작 화면으로 돌아왔습니다."));
}
