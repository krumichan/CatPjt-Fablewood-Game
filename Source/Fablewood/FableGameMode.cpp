#include "FableGameMode.h"
#include "FableBackend.h"
#include "FableDedicatedValidationDriver.h"
#include "GameFramework/GameSession.h"
#include "FableCharacter.h"
#include "FableDungeonWorld.h"
#include "FableMoonwellWorld.h"
#include "FableEnemyBolt.h"
#include "FableProjectile.h"
#include "EngineUtils.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableGameMode::AFableGameMode()
{
    DefaultPawnClass = AFableCharacter::StaticClass();
    PlayerControllerClass = AFablePlayerController::StaticClass();
    PlayerStateClass = AFablePlayerState::StaticClass();
    GameStateClass = AFableGameState::StaticClass();
    HUDClass = AFableHUD::StaticClass();
    FString ValidationRole;
    bLegacyMode = FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), ValidationRole) ||
                  FParse::Param(FCommandLine::Get(), TEXT("FableArtReview"));
}

/** 월드 초기화 옵션에서 시작 화면·온라인 모드와 접속 설정을 준비한다. */
void AFableGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    // 서버 권한과 실행 위치 처리
    Super::InitGame(MapName, Options, ErrorMessage);
    FString ValidationRole;
    const bool bValidation = FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRole="), ValidationRole);
    bDedicatedMode = GetNetMode() == NM_DedicatedServer;
    bFrontEnd = !bDedicatedMode && !bLegacyMode && !bValidation &&
                UGameplayStatics::ParseOption(Options, TEXT("FablePlay")) != TEXT("1");

    // 이 단계에서 초기화 게임에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (bDedicatedMode && !FFableBackend::CanRunDedicated())
    {
        ErrorMessage = TEXT("Dedicated server backend is not configured.");
    }
}

/** 접속을 허용하기 전에 기본 연결 조건을 검사한다. */
void AFableGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                              FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);

    // 이 단계에서 접속을 허용하기 전에 기본 연결 조건을 검사한다.
    if (ErrorMessage.IsEmpty() && bDedicatedMode && !FFableBackend::CanRunDedicated())
    {
        ErrorMessage = TEXT("Dedicated server backend is not configured.");
    }

    // 이 단계에서 접속을 허용하기 전에 기본 연결 조건을 검사한다.
    if (ErrorMessage.IsEmpty() && bDedicatedMode)
    {
        const FString Ticket = UGameplayStatics::ParseOption(Options, TEXT("FableTicket"));
        if (Ticket.IsEmpty() || Ticket.Len() > 512)
        {
            ErrorMessage = TEXT("A valid one-time join ticket is required.");
        }
    }

    // 이 단계에서 접속을 허용하기 전에 기본 연결 조건을 검사한다.
    if (ErrorMessage.IsEmpty() && GetNumPlayers() >= 2)
    {
        ErrorMessage = TEXT("이 모험에는 최대 두 명까지 참가할 수 있습니다.");
    }
}

/** 로그인 요청을 처리하고 필요한 플레이어 연결 상태를 준비한다. */
APlayerController* AFableGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
                                         const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                         FString& ErrorMessage)
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (GetWorld()->GetNumPlayerControllers() >= 2)
    {
        ErrorMessage = TEXT("이 모험에는 최대 두 명까지 참가할 수 있습니다.");
        return nullptr;
    }

    FString Ticket;

    // 이 단계에서 로그인 요청을 처리하고 필요한 플레이어 연결 상태를 준비한다.
    if (bDedicatedMode)
    {
        Ticket = UGameplayStatics::ParseOption(Options, TEXT("FableTicket"));
        if (!FFableBackend::CanRunDedicated() || Ticket.IsEmpty() || Ticket.Len() > 512)
        {
            ErrorMessage = TEXT("Dedicated authentication is unavailable or the join ticket is invalid.");
            return nullptr;
        }
    }

    APlayerController* Player = Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);

    // 현재 월드와 플레이어 참조 처리
    if (Player && bDedicatedMode)
    {
        int32 Slot = 0;

        // 같은 규칙을 적용할 대상을 순회한다.
        for (const auto& Entry : ConnectionSlots)
        {
            if (Entry.Key.IsValid() && Entry.Value == 0)
            {
                Slot = 1;
                break;
            }
        }

        // 현재 월드와 플레이어 참조 처리
        ConnectionSlots.Add(Player, Slot);
        PendingTickets.Add(Player, MoveTemp(Ticket));
        PendingLoginTimes.Add(Player, GetWorld()->GetRealTimeSeconds());
    }

    return Player;
}

/** 새 연결을 파티와 백엔드 프로필 초기화 절차에 연결한다. */
void AFableGameMode::PostLogin(APlayerController* NewPlayer)
{
    AFablePlayerState* Profile = NewPlayer ? NewPlayer->GetPlayerState<AFablePlayerState>() : nullptr;

    // 프로필과 저장 스냅숏 처리
    if (bDedicatedMode)
    {
        // Base login dispatch remains intact, while RestartPlayer gates the async load.
        Super::PostLogin(NewPlayer);
        FString Ticket;
        if (!Profile || !PendingTickets.RemoveAndCopyValue(NewPlayer, Ticket))
        {
            RejectBackendLogin(NewPlayer, TEXT("Backend login was not initialized."));
            return;
        }
        Profile->InitializeBackendProfile(Ticket);
        return;
    }
    // Super::PostLogin calls HandleStartingNewPlayer -> RestartPlayer. Load the
    // profile first so a returning dungeon participant gets the right checkpoint.
    // UWorld::SpawnPlayActor has already called SetPlayer, so local role is valid.

    // 서버 권한과 실행 위치 처리
    if (Profile && !bFrontEnd)
    {
        const FString Prefix = UGameplayStatics::ParseOption(OptionsString, TEXT("FableProfile"));
        const int32 InitialClass = FCString::Atoi(*UGameplayStatics::ParseOption(OptionsString, TEXT("FableClass")));
        Profile->InitializeProfile(NewPlayer->IsLocalController(), Prefix,
                                   static_cast<EFableClass>(FMath::Clamp(InitialClass, 0, 2)));
    }

    Super::PostLogin(NewPlayer);

    // 지역·적·전투 진행 처리
    if (Profile && !bLegacyMode && IsDungeonRunning() &&
        !Profile->IsRegionEligible(GetGameState<AFableGameState>()->ActiveRegion))
    {
        Profile->SetNotice(TEXT("동료가 유적을 탐험하고 있습니다. 마을에서 의뢰를 준비하세요."));
    }
}

/** 접속 종료에 따른 세션과 소유 상태의 정리를 처리한다. */
void AFableGameMode::Logout(AController* Exiting)
{
    // 접속 권한·저장 리비전·멱등 키 처리
    if (!bLegacyMode && Exiting)
    {
        if (AFablePlayerState* Profile = Exiting->GetPlayerState<AFablePlayerState>())
        {
            if (bDedicatedMode)
            {
                Profile->ReleaseBackendProfile();
            }
            else
            {
                Profile->SaveProfile();
            }
        }
    }

    // 서버 시각과 재사용 대기시간 처리
    if (APlayerController* PC = Cast<APlayerController>(Exiting))
    {
        PendingTickets.Remove(PC);
        PendingLoginTimes.Remove(PC);
        ConnectionSlots.Remove(PC);
    }

    Super::Logout(Exiting);
    // The regular monitor handles a departed participant and an empty server.
}

/** 플레이어를 현재 진행에 맞는 시작 지점에 생성한다. */
void AFableGameMode::RestartPlayer(AController* NewPlayer)
{
    // 현재 월드와 플레이어 참조 처리
    if (bDedicatedMode)
    {
        const AFablePlayerState* Profile = NewPlayer ? NewPlayer->GetPlayerState<AFablePlayerState>() : nullptr;
        if (!Profile || !Profile->IsBackendReady())
        {
            return;
        }
    }

    Super::RestartPlayer(NewPlayer);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bLegacyMode || bFrontEnd || !NewPlayer)
    {
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = Cast<AFableCharacter>(NewPlayer->GetPawn());
    AFablePlayerState* Profile = NewPlayer->GetPlayerState<AFablePlayerState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero)
    {
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const AFableGameState* State = GetGameState<AFableGameState>();
    const bool bJoinDungeon = IsDungeonRunning() && State && Profile && Profile->IsRegionEligible(State->ActiveRegion);

    // 지역·적·전투 진행 처리
    if (!MovePlayerToCheckpoint(NewPlayer, bJoinDungeon ? State->ActiveRegion : EFableRegion::Village,
                                GetConnectionSlot(NewPlayer)) &&
        Profile)
    {
        Profile->SetNotice(bJoinDungeon ? TEXT("던전 입구가 막혀 있습니다. 마을의 파란 포털에서 E로 다시 시도하세요.")
                                        : TEXT("마을 도착 지점이 막혀 있습니다. 빈 공간으로 이동하세요."));
    }
}

/** 현재 연결이 차지한 파티 슬롯 값을 반환한다. */
int32 AFableGameMode::GetConnectionSlot(AController* Player) const
{
    // 서버 권한과 실행 위치 조건 검사
    if (!bDedicatedMode)
    {
        return Player && Player->IsLocalController() ? 0 : 1;
    }

    const APlayerController* PC = Cast<APlayerController>(Player);

    // 같은 규칙을 적용할 대상을 순회한다.
    for (const auto& Entry : ConnectionSlots)
    {
        if (Entry.Key.Get() == PC)
        {
            return Entry.Value;
        }
    }

    return 0;
}

/** 백엔드 접속 실패를 연결에 알리고 대기 상태를 해제한다. */
void AFableGameMode::RejectBackendLogin(APlayerController* Player, const FString& Reason)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!IsValid(Player))
    {
        return;
    }

    // 서버 시각과 재사용 대기시간 처리
    PendingTickets.Remove(Player);
    PendingLoginTimes.Remove(Player);
    // Never include tickets, service keys or HTTP payloads in a public log/reason.
    UE_LOG(LogTemp, Warning, TEXT("FABLE_BACKEND_LOGIN_REJECTED controller=%s"), *GetNameSafe(Player));
    const FText Message = FText::FromString(Reason.IsEmpty() ? TEXT("Backend authentication failed.") : Reason);

    // 이 단계에서 백엔드 접속 실패를 연결에 알리고 대기 상태를 해제한다.
    if (!GameSession || !GameSession->KickPlayer(Player, Message))
    {
        Player->ClientReturnToMainMenuWithTextReason(Message);
        Player->Destroy();
    }
}

/** 백엔드 인증 결과를 확인해 플레이어의 게임 진입을 완료한다. */
void AFableGameMode::CompleteBackendLogin(APlayerController* Player, bool bSuccess, const FString& Reason)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!bDedicatedMode || !HasAuthority() || !IsValid(Player) || Player->GetWorld() != GetWorld())
    {
        return;
    }
    // Lease loss or a durable-save conflict must disconnect an already admitted player too.
    if (!bSuccess)
    {
        RejectBackendLogin(Player, TEXT("Backend session or durable save failed."));
        return;
    }
    if (!PendingLoginTimes.Contains(Player))
    {
        return;
    }

    AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();

    // 프로필과 저장 스냅숏 조건 검사
    if (!Profile || !Profile->IsBackendReady() || Profile->GetBackendPlayerId().IsEmpty())
    {
        RejectBackendLogin(Player, TEXT("Backend profile authentication or initial save failed."));
        return;
    }

    // 현재 월드와 플레이어 참조 순회
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Other = It->Get();
        const AFablePlayerState* OtherProfile = Other ? Other->GetPlayerState<AFablePlayerState>() : nullptr;
        if (Other != Player && OtherProfile && OtherProfile->IsBackendReady() &&
            OtherProfile->GetBackendPlayerId() == Profile->GetBackendPlayerId())
        {
            RejectBackendLogin(Player, TEXT("This account is already connected."));
            return;
        }
    }

    // 서버 시각과 재사용 대기시간 처리
    PendingLoginTimes.Remove(Player);
    RestartPlayer(Player);
    UE_LOG(LogTemp, Display, TEXT("FABLE_BACKEND_LOGIN_READY player=%s slot=%d"), *Profile->GetBackendPlayerId(),
           GetConnectionSlot(Player));
}

/** 백엔드 프로필을 기다리는 연결의 완료·만료 상태를 확인한다. */
void AFableGameMode::CheckBackendLogins()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    TArray<TWeakObjectPtr<APlayerController>> Expired;
    const double Now = GetWorld()->GetRealTimeSeconds();

    // 서버 시각과 재사용 대기시간 순회
    for (const auto& Entry : PendingLoginTimes)
    {
        if (!Entry.Key.IsValid() || Now - Entry.Value >= 60.0)
        {
            Expired.Add(Entry.Key);
        }
    }

    // 서버 시각과 재사용 대기시간 순회
    for (const auto& Entry : Expired)
    {
        if (APlayerController* Player = Entry.Get())
        {
            RejectBackendLogin(Player, TEXT("Backend login timed out."));
        }
        else
        {
            PendingLoginTimes.Remove(Entry);
            PendingTickets.Remove(Entry);
            ConnectionSlots.Remove(Entry);
        }
    }
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableGameMode::BeginPlay()
{
    Super::BeginPlay();

    // 체력·마나·보호막·피해량 처리
    if (bDedicatedMode)
    {
        GetWorldTimerManager().SetTimer(BackendLoginMonitor, this, &AFableGameMode::CheckBackendLogins, 1.0f, true);
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("FableDedicatedValidation")))
        {
            UFableDedicatedValidationDriver* Driver = NewObject<UFableDedicatedValidationDriver>(this);
            Driver->RegisterComponent();
        }
#endif
    }

    // 지역·적·전투 진행 조건 검사
    if (bLegacyMode)
    {
        SpawnEncounter();
        return;
    }
    // One replicated static world actor supplies locally built, stable movement bases.

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    FActorSpawnParameters WorldParams;
    WorldParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    DungeonWorld = GetWorld()->SpawnActor<AFableDungeonWorld>(FVector::ZeroVector, FRotator::ZeroRotator, WorldParams);
    MoonwellWorld =
        GetWorld()->SpawnActor<AFableMoonwellWorld>(FVector::ZeroVector, FRotator::ZeroRotator, WorldParams);

    // 생존 상태와 부활 진행 처리
    if (AFableGameState* State = GetGameState<AFableGameState>())
    {
        State->bFrontEnd = bFrontEnd;
        State->AdventureStage = EFableAdventureStage::Village;
        State->ActiveRegion = EFableRegion::Village;
        State->ActiveDifficulty = EFableDifficulty::Normal;
        State->RetryDifficulty = EFableDifficulty::Normal;
        State->MoonSealMask = 0;
        State->ActiveSealIndex = -1;
        State->RequiredEnemies = 0;
        State->DefeatedEnemies = 0;
        State->ForceNetUpdate();
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEnd)
    {
        return;
    }

    GetWorldTimerManager().SetTimer(PartyMonitor, this, &AFableGameMode::CheckPartyState, 0.5f, true);
}

/** 월드 이탈 시 등록한 이벤트와 진행 중인 작업을 정리한다. */
void AFableGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(EncounterTimer);
    GetWorldTimerManager().ClearTimer(PartyMonitor);
    GetWorldTimerManager().ClearTimer(BackendLoginMonitor);
    Super::EndPlay(EndPlayReason);
}

/** 요청자 생존 여부를 확인한다. */
bool AFableGameMode::IsRequesterAlive(APlayerController* Requester) const
{
    // 서버 권한과 실행 위치 조건 검사
    if (bFrontEnd || !HasAuthority() || !IsValid(Requester) || Requester->GetWorld() != GetWorld())
    {
        return false;
    }

    const AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (State && State->bPartyRecoveryPending)
    {
        return false;
    }

    const AFableCharacter* Hero = Cast<AFableCharacter>(Requester->GetPawn());

    return Hero && Hero->IsAlive();
}

/** 살아 있는 요청자가 마을 서비스 범위 안에 있는지 확인한다. */
bool AFableGameMode::CanUseTownService(APlayerController* Requester, FVector Location, float Radius) const
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!IsRequesterAlive(Requester) || !FMath::IsFinite(Radius) || Radius <= 0)
    {
        return false;
    }

    const FVector PawnLocation = Requester->GetPawn()->GetActorLocation();

    return FVector::DistSquared2D(PawnLocation, Location) <= FMath::Square(Radius) &&
           FMath::Abs(PawnLocation.Z - Location.Z) < 300.0f;
}

/** 던전 진행 중 여부를 확인한다. */
bool AFableGameMode::IsDungeonRunning() const
{
    const AFableGameState* State = GetGameState<AFableGameState>();
    return State && !State->bPartyRecoveryPending &&
           (State->AdventureStage == EFableAdventureStage::Dungeon ||
            State->AdventureStage == EFableAdventureStage::Boss);
}

/** 접속한 파티원들에게 같은 진행 안내를 전달한다. */
void AFableGameMode::NotifyParty(const FString& Notice) const
{
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (APlayerController* PC = It->Get())
        {
            if (AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>())
            {
                Profile->SetNotice(Notice);
            }
        }
    }
}

/** 현재 전투가 소유한 적과 진행 상태를 정리한다. */
void AFableGameMode::ClearEncounter()
{
    GetWorldTimerManager().ClearTimer(EncounterTimer);

    // 지역·적·전투 진행 순회
    for (const TWeakObjectPtr<AFableEnemy>& Entry : ActiveEnemies)
    {
        if (AFableEnemy* Enemy = Entry.Get())
        {
            Enemy->Destroy();
        }
    }

    // 지역·적·전투 진행 처리
    ActiveEnemies.Empty();
    ActiveBoss.Reset();
    // Enemy shots belong to the single active expedition, never to the next wave.

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemyBolt> It(GetWorld()); It; ++It)
    {
        It->Destroy();
    }
}

/** 현재 지역과 난이도에 맞는 적 전투를 생성한다. */
void AFableGameMode::SpawnEncounter()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (const AFableGameState* State = GetGameState<AFableGameState>())
    {
        if (State->bPartyRecoveryPending)
        {
            return;
        }
    }

    // 지역·적·전투 진행 처리
    ClearEncounter();
    const FVector Locations[] = {FVector(-650, 350, 110), FVector(650, 400, 110), FVector(0, 1000, 110)};

    // 지역·적·전투 진행 순회
    for (const FVector& Location : Locations)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        if (AFableEnemy* Enemy = GetWorld()->SpawnActor<AFableEnemy>(Location, FRotator(0, -90, 0), Params))
        {
            ActiveEnemies.Add(Enemy);
        }
    }

    // 생존 상태와 부활 진행 처리
    if (AFableGameState* State = GetGameState<AFableGameState>())
    {
        // 지역·적·전투 진행 처리
        State->AdventureStage = EFableAdventureStage::Forest;
        State->EncounterSerial = State->EncounterSerial < MAX_int32 ? State->EncounterSerial + 1 : 1;
        State->bRetryAvailable = false;
        bRetryAfterRecovery = false;
        State->ActiveRegion = EFableRegion::Village;
        State->ActiveDifficulty = EFableDifficulty::Normal;
        State->ActiveSealIndex = -1;
        State->MoonSealMask = 0;
        State->RequiredEnemies = ActiveEnemies.Num();
        State->DefeatedEnemies = 0;
        State->bObjectiveComplete = false;
        State->DungeonWave = 0;
        State->BossHealth = State->BossMaxHealth = 0;
        State->ForceNetUpdate();
    }

    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_ENCOUNTER_STARTED enemies=%d"), ActiveEnemies.Num());
}

/** 숲 전투 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestForestEncounter(APlayerController* Requester)
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (bLegacyMode || !CanUseTownService(Requester, FableRPG::BoardLocation(), 330.0f))
    {
        return;
    }

    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 퀘스트와 이야기 진행 조건 검사
    if (!Profile || Profile->ForestQuest != EFableQuestStage::Active)
    {
        return;
    }
    if (bReturnPending)
    {
        Profile->SetNotice(TEXT("귀환 지점이 막혀 있습니다. 포털에서 E로 귀환을 다시 시도하세요."));
        return;
    }

    const AFableGameState* Adventure = GetGameState<AFableGameState>();

    // 지역·적·전투 진행 조건 검사
    if (IsDungeonRunning() || (Adventure && Adventure->ActiveRegion != EFableRegion::Village))
    {
        Profile->SetNotice(TEXT("숲 순찰을 시작하려면 진행 중인 던전 탐험을 마치고 귀환해야 합니다."));
        return;
    }
    if (!ActiveEnemies.IsEmpty())
    {
        Profile->SetNotice(TEXT("동료와 함께 숲의 수호자를 처치하세요."));
        return;
    }

    // 지역·적·전투 진행 처리
    SpawnEncounter();
    NotifyParty(TEXT("숲의 수호자 세 마리를 함께 처치하세요."));
}

/** 연결된 모든 파티원이 선택 지역의 입장 조건을 만족하는지 확인한다. */
bool AFableGameMode::AllPlayersRegionEligible(EFableRegion Region) const
{
    int32 Count = 0;

    // 지역·적·전투 진행 순회
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();

        // 이 단계에서 연결된 모든 파티원이 선택 지역의 입장 조건을 만족하는지 확인한다.
        if (!IsValid(PC))
        {
            continue;
        }

        // 현재 월드와 플레이어 참조 처리
        ++Count;
        const AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>();
        const AFableCharacter* Hero = Cast<AFableCharacter>(PC->GetPawn());

        // 지역·적·전투 진행 조건 검사
        if (!Profile || !Profile->bProfileLoaded || !Profile->IsRegionEligible(Region) || !Hero || !Hero->IsAlive())
        {
            return false;
        }
    }

    return Count > 0;
}

/** 플레이어를 서버가 정한 지역 체크포인트로 이동시킨다. */
bool AFableGameMode::MovePlayerToCheckpoint(AController* Player, EFableRegion Region, int32 Slot)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!IsValid(Player))
    {
        return false;
    }

    // 현재 월드와 플레이어 참조 처리
    if (bDedicatedMode)
    {
        const AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
        if (!Profile || !Profile->IsBackendReady())
        {
            return false;
        }
    }

    AFableCharacter* Hero = Cast<AFableCharacter>(Player->GetPawn());

    // 현재 월드와 플레이어 참조 처리
    if (!Hero && !Player->GetPawn())
    {
        // A failed initial pawn spawn must not leave recovery waiting forever.
        // Call the base implementation directly; this method applies the chosen safe destination.
        Super::RestartPlayer(Player);
        Hero = Cast<AFableCharacter>(Player->GetPawn());
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero)
    {
        return false;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bDungeon = Region != EFableRegion::Village;
    const FVector Destination =
        bDungeon ? FableRPG::RegionEntrance(Region) + FVector(Slot * 240, 0, 10) : FVector(Slot * 240, -1080, 110);
    // The entry lanes are clear of walls and portal posts. Try nearby alternatives
    // when another pawn occupies an arrival point; town recovery has extra room.
    const TArray<FVector> Offsets =
        bDungeon ? TArray<FVector>{FVector::ZeroVector, FVector(0, 200, 0), FVector(0, -200, 0)}
                 : TArray<FVector>{FVector::ZeroVector, FVector(0, 200, 0),  FVector(0, -200, 0),
                                   FVector(420, 0, 0),  FVector(-420, 0, 0), FVector(0, -420, 0)};

    // 지역·적·전투 진행 순회
    for (const FVector& Offset : Offsets)
    {
        const FVector Candidate = Destination + Offset;
        Hero->SetCheckpoint(Candidate, FRotator(0, 90, 0), true);
        const FVector Actual = Hero->GetActorLocation();
        const bool bCorrectRegion = FableRPG::IsInRegion(Region, Actual);
        if (Hero->IsAlive() && bCorrectRegion && FVector::DistSquared2D(Actual, Candidate) <= FMath::Square(150.0f) &&
            FMath::Abs(Actual.Z - Candidate.Z) <= 180.0f)
        {
            return true;
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("FABLEWOOD_PARTY_MOVE_BLOCKED player=%s destination=%s"), *GetNameSafe(Player),
           bDungeon ? TEXT("dungeon") : TEXT("village"));

    return false;
}

/** 파티 구성원을 해당 지역의 서버 체크포인트로 이동시킨다. */
bool AFableGameMode::MoveParty(EFableRegion Region)
{
    bool bAllMoved = true;
    int32 Index = 0;

    // 지역·적·전투 진행 순회
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();

        // 이 단계에서 파티 구성원을 해당 지역의 서버 체크포인트로 이동시킨다.
        if (!IsValid(PC))
        {
            continue;
        }
        // Do not short-circuit: every connected player gets a placement attempt.

        // 지역·적·전투 진행에 필요한 값 준비
        const AFableCharacter* Hero = Cast<AFableCharacter>(PC->GetPawn());
        const bool bAlreadyReturned = Region == EFableRegion::Village && bReturnPending && Hero && Hero->IsAlive() &&
                                      FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation());
        const bool bMoved =
            bAlreadyReturned || MovePlayerToCheckpoint(PC, Region, bDedicatedMode ? GetConnectionSlot(PC) : Index);
        ++Index;
        bAllMoved = bMoved && bAllMoved;
    }

    return bAllMoved;
}

/** 던전 입장 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestEnterDungeon(APlayerController* Requester)
{
    RequestEnterRegion(Requester, EFableRegion::RootRuins);
}

/** 지역 입장 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestEnterRegion(APlayerController* Requester, EFableRegion Region, EFableDifficulty Difficulty)
{
    // 지역·적·전투 진행 조건 검사
    if (bLegacyMode || !IsRequesterAlive(Requester) ||
        (Region != EFableRegion::RootRuins && Region != EFableRegion::Moonwell) ||
        (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard))
    {
        return;
    }

    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // HTTP 요청·응답과 연결 상태 처리
    if (!CanUseTownService(Requester, FableRPG::PortalLocation(), 330.0f))
    {
        if (Profile)
        {
            Profile->SetNotice(TEXT("마을의 파란 포털 근처에서 E를 누르세요."));
        }
        return;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bReturnPending)
    {
        ReturnPartyToVillage(bReturnAfterWipe);
        return;
    }

    AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    // 지역·적·전투 진행 처리
    if (IsDungeonRunning())
    {
        if (State->ActiveRegion == Region && Profile && Profile->IsRegionEligible(Region))
        {
            // Joining never modifies the difficulty chosen when this expedition began.
            const bool bJoined = MovePlayerToCheckpoint(Requester, Region, GetConnectionSlot(Requester));
            Profile->SetNotice(bJoined ? TEXT("진행 중인 탐험에 합류했습니다.")
                                       : TEXT("입구가 막혀 있습니다. 잠시 후 다시 시도하세요."));
        }
        else if (Profile)
        {
            Profile->SetNotice(TEXT("동료가 진행 중인 지역의 의뢰를 준비하거나 현재 탐험을 마쳐야 합니다."));
        }
        return;
    }

    // 지역·적·전투 진행 처리
    if (State->ActiveRegion != EFableRegion::Village || !ActiveEnemies.IsEmpty())
    {
        if (Profile)
        {
            Profile->SetNotice(TEXT("현재 전투를 마치고 모두 마을로 돌아와야 합니다."));
        }
        return;
    }

    // 지역·적·전투 진행 처리
    if (!AllPlayersRegionEligible(Region))
    {
        if (Profile)
        {
            Profile->SetNotice(TEXT("모두 살아 있는 상태로 해당 지역의 의뢰를 수락해야 입장할 수 있습니다."));
        }
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    // 서버 시각과 재사용 대기시간 조건 검사
    if (Now - LastRestartTime < 3.0)
    {
        return;
    }

    // 지역·적·전투 진행 처리
    LastRestartTime = Now;
    ClearEncounter();
    // Preserve destination during a partial transfer so a stranded player can retry returning.
    State->ActiveRegion = Region;

    // 지역·적·전투 진행 처리
    if (!MoveParty(Region))
    {
        ReturnPartyToVillage(true);
        if (!bReturnPending)
        {
            NotifyParty(TEXT("입구가 막혀 모두 마을로 돌아왔습니다. 포털에서 다시 시도하세요."));
        }
        return;
    }
    // Commit difficulty only after every party member actually arrives.

    // 지역·적·전투 진행 처리
    State->ActiveDifficulty = Difficulty;
    ++State->Round;
    State->EncounterSerial = State->EncounterSerial < MAX_int32 ? State->EncounterSerial + 1 : 1;
    State->bRetryAvailable = false;
    bRetryAfterRecovery = false;
    State->AdventureStage = EFableAdventureStage::Dungeon;
    State->DungeonWave = 1;
    State->MoonSealMask = 0;
    State->ActiveSealIndex = -1;
    State->bObjectiveComplete = false;
    State->BossHealth = State->BossMaxHealth = 0;
    State->ForceNetUpdate();

    // 지역·적·전투 진행 처리
    if (Region == EFableRegion::Moonwell)
    {
        NotifyParty(TEXT("달빛 수원: 봉인을 지키는 적을 처치하고 세 봉인 가까이에서 E로 정화하세요."));
        SpawnMoonSealGroup(0);
    }
    else
    {
        NotifyParty(TEXT("뿌리 유적: 두 차례의 습격을 막고 고대 수호자를 처치하세요."));
        SpawnDungeonWave(1);
    }
}

/** 선택한 달샘 봉인에 대응하는 수호 적 무리를 생성한다. */
void AFableGameMode::SpawnMoonSealGroup(int32 SealIndex)
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 지역·적·전투 진행 조건 검사
    if (!State || State->ActiveRegion != EFableRegion::Moonwell || !IsDungeonRunning() || SealIndex < 0 ||
        SealIndex > 2)
    {
        return;
    }

    // 지역·적·전투 진행 처리
    ClearEncounter();
    State->AdventureStage = EFableAdventureStage::Dungeon;
    State->ActiveSealIndex = SealIndex;
    State->DungeonWave = SealIndex + 1;
    const FVector Offsets[] = {FVector(-320, 250, 110), FVector(320, 250, 110), FVector(0, 510, 110),
                               FVector(0, -430, 110)};
    const int32 Count = SealIndex == 2 ? 4 : 3;

    // 지역·적·전투 진행 순회
    for (int32 I = 0; I < Count; ++I)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        AFableEnemy* Enemy = GetWorld()->SpawnActor<AFableEnemy>(FableRPG::MoonSealLocation(SealIndex) + Offsets[I],
                                                                 FRotator(0, -90, 0), Params);
        if (Enemy)
        {
            Enemy->ConfigureMoonwell(false, I == 2 || (SealIndex == 1 && I == 1), State->ActiveDifficulty);
            ActiveEnemies.Add(Enemy);
        }
    }

    // 생존 상태와 부활 진행 처리
    State->RequiredEnemies = ActiveEnemies.Num();
    State->DefeatedEnemies = 0;
    State->bObjectiveComplete = false;
    State->ForceNetUpdate();

    // 지역·적·전투 진행 처리
    if (ActiveEnemies.Num() != Count)
    {
        ReturnPartyToVillage(true);
        NotifyParty(TEXT("봉인을 준비하지 못했습니다. 의뢰를 유지한 채 마을로 귀환합니다."));
    }

    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_MOON_SEAL_STARTED seal=%d enemies=%d"), SealIndex, ActiveEnemies.Num());
}

/** 봉인 정화 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestCleanseSeal(APlayerController* Requester, int32 SealIndex)
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 지역·적·전투 진행 조건 검사
    if (bLegacyMode || !State || State->bPartyRecoveryPending || !IsRequesterAlive(Requester) || SealIndex < 0 ||
        SealIndex > 2 || State->ActiveRegion != EFableRegion::Moonwell ||
        !CanUseTownService(Requester, FableRPG::MoonSealLocation(SealIndex), 330.0f))
    {
        return;
    }

    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 프로필과 저장 스냅숏 조건 검사
    if (!Profile || !Profile->bProfileLoaded)
    {
        return;
    }

    // A later arrival can inspect an actually cleansed seal independently, including
    // after the boss has fallen. This never starts another wave or grants boss loot.

    // 퀘스트와 이야기 진행 처리
    if ((State->MoonSealMask & (1 << SealIndex)) != 0)
    {
        if (State->AdventureStage == EFableAdventureStage::Dungeon ||
            State->AdventureStage == EFableAdventureStage::Boss ||
            State->AdventureStage == EFableAdventureStage::Cleared)
        {
            Profile->RecordStorySeal(SealIndex);
        }
        return;
    }

    // 생존 상태와 부활 진행 조건 검사
    if (State->AdventureStage != EFableAdventureStage::Dungeon || State->ActiveSealIndex != SealIndex ||
        !Profile->IsRegionEligible(EFableRegion::Moonwell))
    {
        return;
    }
    if (!ActiveEnemies.IsEmpty() || State->RequiredEnemies <= 0 || State->DefeatedEnemies < State->RequiredEnemies)
    {
        Profile->SetNotice(TEXT("봉인을 지키는 적을 먼저 처치하세요."));
        return;
    }

    // 퀘스트와 이야기 진행 처리
    State->MoonSealMask |= 1 << SealIndex;
    State->ForceNetUpdate();
    // Commit the shared seal first; the profile independently verifies this bit,
    // chapter, life state and proximity before recording its personal investigation.
    Profile->RecordStorySeal(SealIndex);
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_MOON_SEAL_CLEANSED seal=%d mask=%d"), SealIndex, State->MoonSealMask);

    // 지역·적·전투 진행 처리
    if (SealIndex < 2)
    {
        NotifyParty(TEXT("봉인이 정화되었습니다. 다음 빛의 기둥으로 이동하세요."));
        SpawnMoonSealGroup(SealIndex + 1);
    }
    else
    {
        State->ActiveSealIndex = -1;
        NotifyParty(TEXT("세 봉인이 열렸습니다. 수원의 달빛 수호자가 깨어납니다."));
        SpawnBoss();
    }
}

/** 던전의 현재 웨이브에 맞는 적 무리를 생성한다. */
void AFableGameMode::SpawnDungeonWave(int32 Wave)
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 지역·적·전투 진행 조건 검사
    if (!State || !IsDungeonRunning() || State->ActiveRegion != EFableRegion::RootRuins)
    {
        return;
    }

    // 지역·적·전투 진행 처리
    ClearEncounter();
    State->AdventureStage = EFableAdventureStage::Dungeon;
    State->DungeonWave = Wave;
    const FVector Positions[] = {FVector(7350, 250, 110), FVector(8650, 250, 110), FVector(8000, 850, 110)};

    // 지역·적·전투 진행 순회
    for (const FVector& Position : Positions)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        if (AFableEnemy* Enemy = GetWorld()->SpawnActor<AFableEnemy>(Position, FRotator(0, -90, 0), Params))
        {
            Enemy->ConfigureEncounter(false, true, State->ActiveDifficulty);
            ActiveEnemies.Add(Enemy);
        }
    }

    // 생존 상태와 부활 진행 처리
    State->RequiredEnemies = ActiveEnemies.Num();
    State->DefeatedEnemies = 0;
    State->bObjectiveComplete = false;
    State->ForceNetUpdate();

    // 지역·적·전투 진행 처리
    if (ActiveEnemies.IsEmpty())
    {
        ReturnPartyToVillage(true);
        if (!bReturnPending)
        {
            NotifyParty(TEXT("유적을 준비하지 못했습니다. 마을 포털에서 다시 시도하세요."));
        }
    }

    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_DUNGEON_WAVE wave=%d enemies=%d"), Wave, ActiveEnemies.Num());
}

/** 던전 보스를 생성하고 파티 전투 진행에 연결한다. */
void AFableGameMode::SpawnBoss()
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State || !IsDungeonRunning())
    {
        return;
    }

    // 지역·적·전투 진행 처리
    ClearEncounter();
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const bool bMoonwell = State->ActiveRegion == EFableRegion::Moonwell;
    AFableEnemy* Boss = GetWorld()->SpawnActor<AFableEnemy>(
        FableRPG::RegionCenter(State->ActiveRegion) + FVector(0, 650, 190), FRotator(0, -90, 0), Params);

    // 지역·적·전투 진행 처리
    if (!Boss)
    {
        ReturnPartyToVillage(true);
        if (!bReturnPending)
        {
            NotifyParty(TEXT("수호자를 불러오지 못했습니다. 마을 포털에서 다시 시도하세요."));
        }
        return;
    }

    // 지역·적·전투 진행 처리
    if (bMoonwell)
    {
        Boss->ConfigureMoonwell(true, false, State->ActiveDifficulty);
    }
    else
    {
        Boss->ConfigureEncounter(true, true, State->ActiveDifficulty);
    }

    // 지역·적·전투 진행 처리
    ActiveEnemies.Add(Boss);
    ActiveBoss = Boss;
    State->AdventureStage = EFableAdventureStage::Boss;
    State->DungeonWave = 3;
    State->RequiredEnemies = 1;
    State->DefeatedEnemies = 0;
    State->BossHealth = Boss->GetHealth();
    State->BossMaxHealth = Boss->GetMaxHealth();
    State->ForceNetUpdate();
    NotifyParty(bMoonwell ? TEXT("달빛 수호자: 파문 안쪽 안전지대로 들어가고, 낙하 예고 원에서 벗어나세요.")
                          : TEXT("고대 수호자: 주황색 원과 전방의 뿌리 공격 예고를 피하세요."));
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_BOSS_STARTED health=%.0f"), Boss->GetHealth());
}

/** 보스 체력 상태를 갱신한다. */
void AFableGameMode::UpdateBossHealth(AFableEnemy* Enemy)
{
    if (!HasAuthority() || !Enemy || ActiveBoss.Get() != Enemy)
    {
        return;
    }
    if (AFableGameState* State = GetGameState<AFableGameState>())
    {
        State->BossHealth = Enemy->GetHealth();
        State->BossMaxHealth = Enemy->GetMaxHealth();
        State->ForceNetUpdate();
    }
}

/** 적 처치에 따른 보상·웨이브·보스 완료 상태를 처리한다. */
void AFableGameMode::HandleEnemyDefeated(AFableEnemy* Enemy)
{
    // Only a committed death of a member of this encounter can award once.
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsValid(Enemy) || Enemy->IsAlive() ||
        ActiveEnemies.Remove(TWeakObjectPtr<AFableEnemy>(Enemy)) == 0)
    {
        return;
    }

    AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    const bool bBoss = Enemy->IsBoss();

    // 생존 상태와 부활 진행 처리
    if (!bLegacyMode)
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            // 현재 월드와 플레이어 참조에 필요한 값 준비
            APlayerController* PC = It->Get();
            AFablePlayerState* Profile = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
            APawn* Pawn = PC ? PC->GetPawn() : nullptr;

            // 프로필과 저장 스냅숏 처리
            if (!Profile || !Profile->bProfileLoaded || !Pawn)
            {
                continue;
            }
            // A connected eligible participant still in the encounter shares personal
            // rewards even while Downed/Defeated. A member already rescued to town,
            // a failed arrival or an ineligible late join receives no remote loot.

            // 퀘스트와 이야기 진행에 필요한 값 준비
            const FVector PawnLocation = Pawn->GetActorLocation();
            const bool bForestCredit = State->AdventureStage == EFableAdventureStage::Forest &&
                                       FableRPG::IsInRegion(EFableRegion::Village, PawnLocation) &&
                                       Profile->ForestQuest == EFableQuestStage::Active;
            const bool bDungeonCredit = IsDungeonRunning() && FableRPG::IsInRegion(State->ActiveRegion, PawnLocation) &&
                                        Profile->IsRegionEligible(State->ActiveRegion);

            // 지역·적·전투 진행 처리
            if (bForestCredit)
            {
                Profile->RecordKill(bBoss);
            }
            else if (bDungeonCredit)
            {
                Profile->RecordRegionKill(State->ActiveRegion, bBoss, State->ActiveDifficulty);
            }
        }
    }

    State->DefeatedEnemies = FMath::Min(State->DefeatedEnemies + 1, State->RequiredEnemies);

    // 지역·적·전투 진행 처리
    if (bBoss)
    {
        State->BossHealth = 0;
        ActiveBoss.Reset();
    }

    // 지역·적·전투 진행 처리
    if (ActiveEnemies.IsEmpty())
    {
        for (TActorIterator<AFableEnemyBolt> It(GetWorld()); It; ++It)
        {
            It->Destroy();
        }
        if (bLegacyMode)
        {
            State->bObjectiveComplete = State->RequiredEnemies > 0;
        }
        else if (State->AdventureStage == EFableAdventureStage::Forest)
        {
            State->AdventureStage = EFableAdventureStage::ForestCleared;
            State->bObjectiveComplete = true;
            NotifyParty(TEXT("숲이 평온해졌습니다. 안내인에게 돌아가 의뢰 보상을 받으세요."));
        }
        else if (State->AdventureStage == EFableAdventureStage::Dungeon &&
                 State->ActiveRegion == EFableRegion::Moonwell)
        {
            NotifyParty(TEXT("봉인을 지키는 적이 사라졌습니다. 빛의 봉인 가까이에서 E를 누르세요."));
        }
        else if (State->AdventureStage == EFableAdventureStage::Dungeon)
        {
            const int32 NextWave = State->DungeonWave + 1;
            NotifyParty(NextWave <= 2 ? TEXT("3초 뒤 다음 수호자 무리가 나타납니다.")
                                      : TEXT("고대 수호자가 깨어나고 있습니다."));
            GetWorldTimerManager().SetTimer(
                EncounterTimer,
                [this, NextWave]()
                {
                    if (!IsDungeonRunning())
                    {
                        return;
                    }
                    if (NextWave <= 2)
                    {
                        SpawnDungeonWave(NextWave);
                    }
                    else
                    {
                        SpawnBoss();
                    }
                },
                3.0f, false);
        }
        else if (State->AdventureStage == EFableAdventureStage::Boss)
        {
            State->AdventureStage = EFableAdventureStage::Cleared;
            State->bObjectiveComplete = true;
            NotifyParty(TEXT("수호자를 처치했습니다! 귀환 포털에서 E를 누르고 안내인에게 해당 의뢰 보상을 받으세요."));
        }
    }

    // 생존 상태와 부활 진행 처리
    State->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_OBJECTIVE progress=%d/%d complete=%d"), State->DefeatedEnemies,
           State->RequiredEnemies, State->bObjectiveComplete);
}

/** 복구 전투 상태를 해제한다. */
void AFableGameMode::ClearRecoveryCombat()
{
    ClearEncounter();
    // A wipe discards residual hero shots and defeated enemy presentations too.
    // Destruction never invokes the kill/reward path.

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableProjectile> It(GetWorld()); It; ++It)
    {
        It->Destroy();
    }

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        It->Destroy();
    }
}

/** 재도전 퀘스트의 존재 여부를 확인한다. */
bool AFableGameMode::HasRetryQuest(EFableRegion Region) const
{
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        const AFablePlayerState* Profile = IsValid(PC) ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        if (!Profile || !Profile->bProfileLoaded)
        {
            continue;
        }
        if (Region == EFableRegion::Village ? Profile->ForestQuest == EFableQuestStage::Active
                                            : Profile->IsRegionEligible(Region))
        {
            return true;
        }
    }
    return false;
}

/** 파티 전멸 뒤 전투를 정리하고 복구 대기를 시작한다. */
void AFableGameMode::BeginPartyRecovery()
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bLegacyMode || bFrontEnd || !State || State->bPartyRecoveryPending)
    {
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bUnfinishedEncounter = State->AdventureStage == EFableAdventureStage::Forest || IsDungeonRunning();
    bRetryAfterRecovery = State->bRetryAvailable;

    // 퀘스트와 이야기 진행 처리
    if (bUnfinishedEncounter)
    {
        State->RetryRegion = State->ActiveRegion;
        State->RetryDifficulty = State->ActiveDifficulty;
        bRetryAfterRecovery = HasRetryQuest(State->RetryRegion);
    }

    // 현재 월드와 플레이어 참조 처리
    State->bRetryAvailable = false;
    State->bPartyRecoveryPending = true;
    State->PartyRecoveryEndServerTime = GetWorld()->GetTimeSeconds() + 3.0f;
    NextRecoveryAttemptTime = State->PartyRecoveryEndServerTime;
    // Latch recovery once announced. A newly connected hero arrives in town;
    // it cannot revive an already-discarded encounter during this countdown.
    ClearRecoveryCombat();
    State->RequiredEnemies = State->DefeatedEnemies = 0;
    State->BossHealth = State->BossMaxHealth = 0;
    State->DungeonWave = 0;
    State->MoonSealMask = 0;
    State->ActiveSealIndex = -1;
    State->bObjectiveComplete = false;
    State->ForceNetUpdate();
    NotifyParty(TEXT("활동 가능한 동료가 없습니다. 3초 뒤 모두 마을로 구조됩니다. 의뢰 진행과 장비는 보존됩니다."));
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_PARTY_RECOVERY_STARTED region=%d retry=%d serial=%d end=%.2f"),
           int32(State->RetryRegion), bRetryAfterRecovery, State->EncounterSerial, State->PartyRecoveryEndServerTime);
}

/** 복구 시간이 끝난 파티를 안전 지점으로 되돌린다. */
void AFableGameMode::FinishPartyRecovery()
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State || !State->bPartyRecoveryPending)
    {
        return;
    }

    ReturnPartyToVillage(true);

    // 현재 월드와 플레이어 참조 조건 검사
    if (bReturnPending)
    {
        // Dead players cannot press a retry key. Retry safe placement automatically,
        // leaving already-rescued peers free to move out of the arrival space.
        NextRecoveryAttemptTime = GetWorld()->GetTimeSeconds() + 1.0;
        return;
    }

    // 퀘스트와 이야기 진행 처리
    State->bPartyRecoveryPending = false;
    State->PartyRecoveryEndServerTime = 0;
    State->bRetryAvailable = bRetryAfterRecovery && HasRetryQuest(State->RetryRegion);
    State->PartyRecoveryCount = State->PartyRecoveryCount < MAX_int32 ? State->PartyRecoveryCount + 1 : 1;
    State->ForceNetUpdate();
    NotifyParty(
        State->bRetryAvailable
            ? (State->RetryRegion == EFableRegion::Village
                   ? TEXT(
                         "모두 마을로 구조되었습니다. 로완 근처에서 R로 숲 순찰을 다시 시작하세요. 기존 의뢰 진행은 유지됩니다.")
                   : TEXT(
                         "모두 마을로 구조되었습니다. 차원문 근처에서 R로 원정을 다시 시작하세요. 기존 의뢰 진행은 유지됩니다."))
            : TEXT("모두 마을로 구조되었습니다. 의뢰 진행과 장비, 획득한 보상은 그대로 유지됩니다."));
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_PARTY_RECOVERY_COMPLETE count=%d retry=%d region=%d serial=%d"),
           State->PartyRecoveryCount, State->bRetryAvailable, int32(State->RetryRegion), State->EncounterSerial);
}

/** 동료들의 생존·지역 상태를 확인해 파티 복구를 판단한다. */
void AFableGameMode::CheckPartyState()
{
    // 서버 권한과 실행 위치 조건 검사
    if (bLegacyMode || bFrontEnd || !HasAuthority())
    {
        return;
    }

    AFableGameState* State = GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    int32 ConnectedPlayers = 0;
    int32 LivingPlayers = 0;
    TArray<APlayerController*> Participants;

    // 현재 월드와 플레이어 참조 순회
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();

        // 이 단계에서 동료들의 생존·지역 상태를 확인해 파티 복구를 판단한다.
        if (!IsValid(PC))
        {
            continue;
        }

        // 현재 월드와 플레이어 참조 처리
        ++ConnectedPlayers;
        const AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>();
        // Startup controllers have neither a loaded profile nor a possessed pawn yet.

        // 프로필과 저장 스냅숏 처리
        if (!Profile || !Profile->bProfileLoaded || (bDedicatedMode && !Profile->IsBackendReady()))
        {
            continue;
        }

        // 현재 월드와 플레이어 참조 처리
        Participants.Add(PC);
        const AFableCharacter* Hero = Cast<AFableCharacter>(PC->GetPawn());

        // 이 단계에서 동료들의 생존·지역 상태를 확인해 파티 복구를 판단한다.
        if (Hero && Hero->IsAlive())
        {
            ++LivingPlayers;
        }
    }

    // 지역·적·전투 진행 처리
    if (ConnectedPlayers == 0)
    {
        if (State->bPartyRecoveryPending || State->ActiveRegion != EFableRegion::Village || !ActiveEnemies.IsEmpty())
        {
            ClearRecoveryCombat();
            ReturnPartyToVillage(true);
            State->bPartyRecoveryPending = false;
            State->PartyRecoveryEndServerTime = 0;
            State->bRetryAvailable = false;
            bRetryAfterRecovery = false;
            State->ForceNetUpdate();
        }
        return;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Participants.IsEmpty())
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    // 서버 시각과 재사용 대기시간 처리
    if (State->bPartyRecoveryPending)
    {
        if (Now >= State->PartyRecoveryEndServerTime && Now >= NextRecoveryAttemptTime)
        {
            FinishPartyRecovery();
        }
        return;
    }
    // Count the entire connected party, including players outside an arena or in
    // the void. One active teammate anywhere prevents a party-wide wipe.

    // 서버 시각과 재사용 대기시간 조건 검사
    if (LivingPlayers == 0)
    {
        BeginPartyRecovery();
        return;
    }
    if (Now < NextIndividualRescueTime)
    {
        return;
    }

    // 생존 상태와 부활 진행 순회
    for (APlayerController* PC : Participants)
    {
        // 생존 상태와 부활 진행에 필요한 값 준비
        AFableCharacter* Hero = Cast<AFableCharacter>(PC->GetPawn());
        const bool bMissingPawn = !PC->GetPawn();
        const bool bRescueDue = Hero && Hero->IsDefeated() && Hero->GetDefeatedServerTime() >= 0.0f &&
                                FMath::IsFinite(Hero->GetDefeatedServerTime()) &&
                                Now - Hero->GetDefeatedServerTime() >= 3.0;

        // 이 단계에서 동료들의 생존·지역 상태를 확인해 파티 복구를 판단한다.
        if (!bMissingPawn && !bRescueDue)
        {
            continue;
        }

        NextIndividualRescueTime = Now + 0.5;

        // 지역·적·전투 진행 처리
        if (MovePlayerToCheckpoint(PC, EFableRegion::Village, GetConnectionSlot(PC)))
        {
            if (AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>())
            {
                Profile->SetNotice(
                    IsDungeonRunning()
                        ? TEXT(
                              "마을로 구조되었습니다. 동료의 전투는 계속됩니다. 차원문에서 같은 지역을 선택해 합류하세요.")
                        : TEXT("마을로 구조되었습니다. 동료의 의뢰와 전투 진행은 유지됩니다."));
            }
            UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_INDIVIDUAL_RESCUED player=%s serial=%d"), *GetNameSafe(PC),
                   State->EncounterSerial);
        }
        else if (AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>())
        {
            Profile->SetNotice(TEXT("마을 구조 위치를 확보하고 있습니다. 자동으로 다시 시도합니다."));
        }
    }
}

/** 전투 재도전 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestRetryEncounter(APlayerController* Requester)
{
    AFableGameState* State = GetGameState<AFableGameState>();

    // 지역·적·전투 진행 조건 검사
    if (bLegacyMode || !State || !IsRequesterAlive(Requester) || !State->bRetryAvailable ||
        State->bPartyRecoveryPending || bReturnPending || State->ActiveRegion != EFableRegion::Village)
    {
        return;
    }

    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 프로필과 저장 스냅숏 조건 검사
    if (!Profile)
    {
        return;
    }

    // 퀘스트와 이야기 진행 처리
    if (State->RetryRegion == EFableRegion::Village)
    {
        if (!CanUseTownService(Requester, FableRPG::BoardLocation(), 330.0f))
        {
            Profile->SetNotice(TEXT("로완 근처에서 R을 눌러 숲 순찰에 다시 도전하세요."));
            return;
        }
        if (Profile->ForestQuest != EFableQuestStage::Active)
        {
            Profile->SetNotice(TEXT("진행 중인 숲 순찰 의뢰가 필요합니다. 완료한 의뢰는 먼저 보고하세요."));
            return;
        }
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            const APlayerController* PC = It->Get();

            // 이 단계에서 전투 재도전 요청의 조건을 확인하고 해당 동작을 진행한다.
            if (!IsValid(PC))
            {
                continue;
            }

            // 현재 월드와 플레이어 참조에 필요한 값 준비
            const AFablePlayerState* Member = PC->GetPlayerState<AFablePlayerState>();
            const AFableCharacter* Hero = Cast<AFableCharacter>(PC->GetPawn());

            // 프로필과 저장 스냅숏 조건 검사
            if (!Member || !Member->bProfileLoaded || !Hero || !Hero->IsAlive())
            {
                Profile->SetNotice(TEXT("모두 활동 가능한 상태가 되면 숲 순찰을 다시 시작할 수 있습니다."));
                return;
            }
        }
        RequestForestEncounter(Requester);
    }
    else if (State->RetryRegion == EFableRegion::RootRuins || State->RetryRegion == EFableRegion::Moonwell)
    {
        RequestEnterRegion(Requester, State->RetryRegion, State->RetryDifficulty);
    }
}

/** 현재 전투를 마무리하고 파티를 마을로 복귀시킨다. */
void AFableGameMode::ReturnPartyToVillage(bool bWipe)
{
    // 지역·적·전투 진행 처리
    ClearEncounter();
    bReturnAfterWipe = bWipe;
    bReturnPending = !MoveParty(EFableRegion::Village);

    // 생존 상태와 부활 진행 처리
    if (AFableGameState* State = GetGameState<AFableGameState>())
    {
        State->AdventureStage =
            bReturnPending && !bWipe ? EFableAdventureStage::Cleared : EFableAdventureStage::Village;

        // 지역·적·전투 진행 처리
        if (!bReturnPending)
        {
            State->ActiveRegion = EFableRegion::Village;
            State->ActiveDifficulty = EFableDifficulty::Normal;
        }

        // 지역·적·전투 진행 처리
        State->ActiveSealIndex = -1;
        State->MoonSealMask = 0;
        State->DungeonWave = 0;
        State->BossHealth = State->BossMaxHealth = 0;
        State->RequiredEnemies = 0;
        State->DefeatedEnemies = 0;
        State->bObjectiveComplete = !bWipe;
        State->ForceNetUpdate();
    }

    // 이 단계에서 현재 전투를 마무리하고 파티를 마을로 복귀시킨다.
    if (bReturnPending)
    {
        const AFableGameState* State = GetGameState<AFableGameState>();
        NotifyParty(State && State->bPartyRecoveryPending
                        ? TEXT("마을 구조 위치를 확보하고 있습니다. 자동으로 다시 시도합니다.")
                        : TEXT("전투가 끝났지만 귀환 지점이 막혀 있습니다. 포털에서 E로 귀환을 다시 시도하세요."));
        return;
    }

    NotifyParty(bWipe ? TEXT("모두 구조되어 마을로 돌아왔습니다. 의뢰는 유지됩니다. 포털에서 다시 도전하세요.")
                      : TEXT("마을로 돌아왔습니다. 안내인에게 완료한 의뢰 보상을 받으세요."));
}

/** 마을 복귀 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestReturnVillage(APlayerController* Requester)
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (bLegacyMode || !IsRequesterAlive(Requester))
    {
        return;
    }

    // HTTP 요청·응답과 연결 상태에 필요한 값 준비
    AFableGameState* State = GetGameState<AFableGameState>();
    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 지역·적·전투 진행 조건 검사
    if (!State || State->ActiveRegion == EFableRegion::Village)
    {
        return;
    }

    const bool bNearPortal = CanUseTownService(Requester, FableRPG::RegionEntrance(State->ActiveRegion), 350) ||
                             CanUseTownService(Requester, FableRPG::RegionReturnPortal(State->ActiveRegion), 350);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bNearPortal)
    {
        return;
    }

    // 프로필과 저장 스냅숏 처리
    if (State->AdventureStage != EFableAdventureStage::Cleared && !bReturnPending)
    {
        if (Profile)
        {
            Profile->SetNotice(TEXT("수호자를 처치하면 귀환 포털이 열립니다. 모두 쓰러지면 마을로 구조됩니다."));
        }
        return;
    }

    // 퀘스트와 이야기 진행 처리
    if (State->ActiveRegion == EFableRegion::Moonwell && State->AdventureStage == EFableAdventureStage::Cleared)
    {
        // Keep the completed encounter available until living participants have
        // personally inspected its seals. Older/completed stories and rescued
        // players outside this region never hold the expedition at the exit.
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            const APlayerController* Member = It->Get();
            const AFablePlayerState* MemberProfile = Member ? Member->GetPlayerState<AFablePlayerState>() : nullptr;
            const AFableCharacter* MemberHero = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;
            if (MemberProfile && MemberProfile->bProfileLoaded && MemberProfile->StoryChapter == 4 &&
                MemberProfile->StorySealMask != 7 && MemberHero && MemberHero->IsAlive() &&
                FableRPG::IsInRegion(EFableRegion::Moonwell, MemberHero->GetActorLocation()))
            {
                NotifyParty(TEXT("동료가 정화된 봉인을 E로 확인 중입니다. 세 봉인 조사를 마친 뒤 함께 귀환하세요."));
                return;
            }
        }
    }

    ReturnPartyToVillage(bReturnPending ? bReturnAfterWipe : false);
}

/** 새 전투 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableGameMode::RequestNewRound(APlayerController* Requester)
{
    // 지역·적·전투 진행 처리
    if (!bLegacyMode)
    {
        if (const AFableGameState* State = GetGameState<AFableGameState>())
        {
            if (State->bRetryAvailable)
            {
                RequestRetryEncounter(Requester);
                return;
            }
        }
        RequestEnterDungeon(Requester);
        return;
    }

    AFableGameState* State = GetGameState<AFableGameState>();

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsValid(Requester) || Requester->GetWorld() != GetWorld() || !State ||
        !State->bObjectiveComplete)
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    // 서버 시각과 재사용 대기시간 조건 검사
    if (Now - LastRestartTime < 3.0)
    {
        return;
    }

    // 서버 시각과 재사용 대기시간 처리
    LastRestartTime = Now;
    State->bObjectiveComplete = false;
    ++State->Round;
    TArray<APlayerController*> Players;

    // 현재 월드와 플레이어 참조 순회
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (APlayerController* PC = It->Get())
        {
            Players.Add(PC);
        }
    }

    // 현재 월드와 플레이어 참조 순회
    for (APlayerController* PC : Players)
    {
        APawn* OldPawn = PC->GetPawn();
        PC->UnPossess();
        if (OldPawn)
        {
            OldPawn->Destroy();
        }
        RestartPlayer(PC);
    }

    SpawnEncounter();
}
