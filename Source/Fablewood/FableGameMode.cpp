#include "FableGameMode.h"
#include "FablePartyManager.h"
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

#include "FableAdventure.h"
#include "FablePartyEncounter.h"

/** AFableGameMode는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** InitGame는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** PreLogin는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** Login는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** PostLogin는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

    if (Profile && !bFrontEnd)
    {
        if (AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld()))
        {
            Parties->RegisterPlayer(NewPlayer);
        }
    }

    // 재접속 공개 키를 먼저 복구해야 RestartPlayer가 같은 참가 증서를 찾는다.
    Super::PostLogin(NewPlayer);
}

/** Logout는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::Logout(AController* Exiting)
{
    // 인증 프로필을 해제하기 전에 파티의 재접속 자리를 예약한다.
    if (AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld()))
    {
        Parties->DisconnectPlayer(Cast<APlayerController>(Exiting));
    }

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

/** GetConnectionSlot는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** RejectBackendLogin는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** CompleteBackendLogin는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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
    if (AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld()))
    {
        Parties->RegisterPlayer(Player);
    }

    RestartPlayer(Player);
    UE_LOG(LogTemp, Display, TEXT("FABLE_BACKEND_LOGIN_READY player=%s slot=%d"), *Profile->GetBackendPlayerId(),
           GetConnectionSlot(Player));
}

/** CheckBackendLogins는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** BeginPlay는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::BeginPlay()
{
    Super::BeginPlay();

    // 명부는 서버만 생성하고 클라이언트는 복제된 관리자를 사용한다.
    if (!bFrontEnd && !bLegacyMode && !AFablePartyManager::Find(GetWorld()))
    {
        if (AFablePartyManager* Parties = GetWorld()->SpawnActor<AFablePartyManager>())
        {
            // 초기 로컬 접속은 월드 BeginPlay보다 먼저 완료될 수도 있다.
            for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
            {
                Parties->RegisterPlayer(It->Get());
            }
        }
    }

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
    if (AFableGameState* RootState = GetGameState<AFableGameState>())
    {
        FFableAdventureProgress* State = &RootState->FieldProgress;

        RootState->bFrontEnd = bFrontEnd;
        State->AdventureStage = EFableAdventureStage::Village;
        State->ActiveRegion = EFableRegion::Village;
        State->ActiveDifficulty = EFableDifficulty::Normal;
        State->RetryDifficulty = EFableDifficulty::Normal;
        State->MoonSealMask = 0;
        State->ActiveSealIndex = -1;
        State->RequiredEnemies = 0;
        State->DefeatedEnemies = 0;
        RootState->ForceNetUpdate();
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEnd)
    {
        return;
    }

    GetWorldTimerManager().SetTimer(PartyMonitor, this, &AFableGameMode::CheckPartyState, 0.5f, true);
}

/** EndPlay는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(EncounterTimer);
    GetWorldTimerManager().ClearTimer(PartyMonitor);
    GetWorldTimerManager().ClearTimer(BackendLoginMonitor);
    Super::EndPlay(EndPlayReason);
}

/** CanUseTownService는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** NotifyParty는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::NotifyParty(const FString& Notice) const
{
    for (APlayerController* PC : GetEncounterPlayers())
    {
        if (AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>())
        {
            Profile->SetNotice(Notice);
        }
    }
}

/** ClearEncounter는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::ClearEncounter()
{
    GetWorldTimerManager().ClearTimer(EncounterTimer);

    // 지역·적·전투 진행 순회
    for (const TWeakObjectPtr<AFableEnemy>& Entry : SpawnedEncounterEnemies)
    {
        if (AFableEnemy* Enemy = Entry.Get())
        {
            Enemy->Destroy();
        }
    }

    // 지역·적·전투 진행 처리
    ActiveEnemies.Empty();
    SpawnedEncounterEnemies.Empty();
    ActiveBoss.Reset();
    // Enemy shots belong to the single active expedition, never to the next wave.

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemyBolt> It(GetWorld()); It; ++It)
    {
        if (!It->GetExpeditionId().IsValid())
        {
            It->Destroy();
        }
    }
}

/** SpawnEncounter는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::SpawnEncounter()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (const AFableGameState* RootState = GetGameState<AFableGameState>())
    {
        const FFableAdventureProgress* State = &RootState->FieldProgress;
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
            SpawnedEncounterEnemies.Add(Enemy);
        }
    }

    // 생존 상태와 부활 진행 처리
    if (AFableGameState* RootState = GetGameState<AFableGameState>())
    {
        FFableAdventureProgress* State = &RootState->FieldProgress;

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
        RootState->ForceNetUpdate();
    }

    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_ENCOUNTER_STARTED enemies=%d"), ActiveEnemies.Num());
}

/** RequestForestEncounter는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::RequestForestEncounter(APlayerController* Requester)
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (bLegacyMode || !CanUseTownService(Requester, FableRPG::BoardLocation(), 330.0f))
    {
        return;
    }

    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 퀘스트와 이야기 진행 조건 검사
    if (!Profile || Profile->ActiveExpeditionId.IsValid() || Profile->ForestQuest != EFableQuestStage::Active)
    {
        return;
    }
    if (bReturnPending)
    {
        Profile->SetNotice(TEXT("귀환 지점이 막혀 있습니다. 포털에서 E로 귀환을 다시 시도하세요."));
        return;
    }

    const auto Adventure = FableAdventure::Field(GetWorld());

    // 지역·적·전투 진행 조건 검사
    if ((Adventure && Adventure->ActiveRegion != EFableRegion::Village))
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

/** AllPlayersRegionEligible는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
bool AFableGameMode::AllPlayersRegionEligible(EFableRegion Region) const
{
    int32 Count = 0;

    // 지역·적·전투 진행 순회
    for (APlayerController* PC : GetEncounterPlayers())
    {
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

/** MovePlayerToCheckpoint는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
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

/** MoveParty는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
bool AFableGameMode::MoveParty(EFableRegion Region)
{
    bool bAllMoved = true;
    int32 Index = 0;

    // 지역·적·전투 진행 순회
    for (APlayerController* PC : GetEncounterPlayers())
    {
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

/** HasRetryQuest는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
bool AFableGameMode::HasRetryQuest(EFableRegion Region) const
{
    for (APlayerController* PC : GetEncounterPlayers())
    {
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

/** BeginPartyRecovery는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::BeginPartyRecovery()
{
    AFableGameState* RootState = GetGameState<AFableGameState>();
    FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bLegacyMode || bFrontEnd || !State || State->bPartyRecoveryPending)
    {
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bUnfinishedEncounter = State->AdventureStage == EFableAdventureStage::Forest;
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
    RootState->ForceNetUpdate();
    NotifyParty(TEXT("활동 가능한 동료가 없습니다. 3초 뒤 모두 마을로 구조됩니다. 의뢰 진행과 장비는 보존됩니다."));
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_PARTY_RECOVERY_STARTED region=%d retry=%d serial=%d end=%.2f"),
           int32(State->RetryRegion), bRetryAfterRecovery, State->EncounterSerial, State->PartyRecoveryEndServerTime);
}

/** FinishPartyRecovery는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::FinishPartyRecovery()
{
    AFableGameState* RootState = GetGameState<AFableGameState>();
    FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;

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
    RootState->ForceNetUpdate();
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

/** CheckPartyState는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::CheckPartyState()
{
    // 서버 권한과 실행 위치 조건 검사
    if (bLegacyMode || bFrontEnd || !HasAuthority())
    {
        return;
    }

    AFableGameState* RootState = GetGameState<AFableGameState>();
    FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    int32 ConnectedPlayers = 0;
    int32 LivingPlayers = 0;
    TArray<APlayerController*> Participants;

    // 현재 월드와 플레이어 참조 순회
    for (APlayerController* PC : GetEncounterPlayers())
    {
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
            RootState->ForceNetUpdate();
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
    // 마을로 먼저 구조된 참가자도 생존 인원에 포함하되 다른 파티는 전멸 판정에 섞지 않는다.

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
                Profile->SetNotice(TEXT("마을로 구조되었습니다. 동료의 의뢰와 전투 진행은 유지됩니다."));
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

/** RequestRetryEncounter는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::RequestRetryEncounter(APlayerController* Requester)
{
    AFableGameState* RootState = GetGameState<AFableGameState>();
    FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;

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
        for (APlayerController* PC : GetEncounterPlayers())
        {
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

/** ReturnPartyToVillage는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::ReturnPartyToVillage(bool bWipe)
{
    // 지역·적·전투 진행 처리
    ClearEncounter();
    bReturnAfterWipe = bWipe;
    bReturnPending = !MoveParty(EFableRegion::Village);

    // 생존 상태와 부활 진행 처리
    if (AFableGameState* RootState = GetGameState<AFableGameState>())
    {
        FFableAdventureProgress* State = &RootState->FieldProgress;

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
        RootState->ForceNetUpdate();
    }

    // 이 단계에서 현재 전투를 마무리하고 파티를 마을로 복귀시킨다.
    if (bReturnPending)
    {
        const AFableGameState* RootState = GetGameState<AFableGameState>();
        const FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;
        NotifyParty(State && State->bPartyRecoveryPending
                        ? TEXT("마을 구조 위치를 확보하고 있습니다. 자동으로 다시 시도합니다.")
                        : TEXT("전투가 끝났지만 귀환 지점이 막혀 있습니다. 포털에서 E로 귀환을 다시 시도하세요."));
        return;
    }

    NotifyParty(bWipe ? TEXT("모두 구조되어 마을로 돌아왔습니다. 의뢰는 유지됩니다. 포털에서 다시 도전하세요.")
                      : TEXT("마을로 돌아왔습니다. 안내인에게 완료한 의뢰 보상을 받으세요."));
}

/** RequestNewRound는 인증 또는 공유 필드의 기존 규칙을 처리한다. */
void AFableGameMode::RequestNewRound(APlayerController* Requester)
{
    // 지역·적·전투 진행 처리
    if (!bLegacyMode)
    {
        if (const AFableGameState* RootState = GetGameState<AFableGameState>())
        {
            const FFableAdventureProgress* State = &RootState->FieldProgress;

            if (State->bRetryAvailable)
            {
                RequestRetryEncounter(Requester);
                return;
            }
        }
        RequestEnterDungeon(Requester);
        return;
    }

    AFableGameState* RootState = GetGameState<AFableGameState>();
    FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;

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

/** UUID가 정확히 일치하는 유효한 원정만 반환한다. */
AFablePartyEncounter* AFableGameMode::FindExpedition(FGuid Id) const
{
    const TObjectPtr<AFablePartyEncounter>* Found = Expeditions.Find(Id);
    return Found && IsValid(Found->Get()) && (*Found)->IsCommitted() ? Found->Get() : nullptr;
}

/** 단일 전역 원정 대신 실제 프로필의 증서 또는 예약된 명부를 조회한다. */
AFablePartyEncounter* AFableGameMode::GetExpeditionFor(const AFablePlayerState* Profile) const
{
    if (!IsValid(Profile) || Profile->GetWorld() != GetWorld() || !Profile->PartyId.IsValid() ||
        Profile->PartyPlayerKey.IsEmpty())
    {
        return nullptr;
    }
    for (AFablePartyEncounter* Encounter : GetExpeditions())
    {
        const FFableExpeditionTicket& Entry = Encounter->GetTicket();
        if (Entry.PartyId == Profile->PartyId && Entry.Contains(Profile->PartyPlayerKey) &&
            (!Profile->ActiveExpeditionId.IsValid() || Profile->ActiveExpeditionId == Entry.ExpeditionId))
        {
            return Encounter;
        }
    }
    return nullptr;
}

/** 확정된 원정 목록은 검증과 요청 라우팅에만 제공한다. */
TArray<AFablePartyEncounter*> AFableGameMode::GetExpeditions() const
{
    TArray<AFablePartyEncounter*> Result;
    for (const auto& Entry : Expeditions)
    {
        if (IsValid(Entry.Value) && Entry.Value->IsCommitted())
        {
            Result.Add(Entry.Value);
        }
    }
    return Result;
}

/** 해당 파티가 이미 소유한 원정만 구성 변경을 잠근다. */
bool AFableGameMode::IsPartyExpeditionActive(FGuid PartyId) const
{
    for (AFablePartyEncounter* Encounter : GetExpeditions())
    {
        if (Encounter->GetTicket().PartyId == PartyId)
        {
            return true;
        }
    }
    return PendingAdmissions.Contains(PartyId);
}

/** 현재 연결과 원정 증서가 일치하는 참가자를 확인한다. */
bool AFableGameMode::IsExpeditionParticipant(const AFablePlayerState* Profile) const
{
    const AFablePartyEncounter* Encounter = Profile ? FindExpedition(Profile->ActiveExpeditionId) : nullptr;
    return Encounter && Encounter->IsParticipant(Profile);
}

/** 공유 필드 처리에는 원정에 속하지 않은 현재 연결만 포함한다. */
TArray<APlayerController*> AFableGameMode::GetEncounterPlayers() const
{
    TArray<APlayerController*> Result;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Player = It->Get();
        const AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        if (IsValid(Player) && (!Profile || !Profile->ActiveExpeditionId.IsValid()))
        {
            Result.Add(Player);
        }
    }
    return Result;
}

/** 요청자는 자기 진행의 복구 상태만 검사한다. */
bool AFableGameMode::IsRequesterAlive(APlayerController* Requester) const
{
    if (bFrontEnd || !HasAuthority() || !IsValid(Requester) || Requester->GetWorld() != GetWorld())
    {
        return false;
    }
    const auto Adventure = FableAdventure::For(Requester);
    const auto* Hero = Requester->GetPawn<AFableCharacter>();
    return Adventure && !Adventure->bPartyRecoveryPending && Hero && Hero->IsAlive();
}

/** 현재 프로필의 불변 증서를 복구한 뒤 올바른 체크포인트에 생성한다. */
void AFableGameMode::RestartPlayer(AController* NewPlayer)
{
    AFablePlayerState* Profile = NewPlayer ? NewPlayer->GetPlayerState<AFablePlayerState>() : nullptr;
    if (bDedicatedMode && (!Profile || !Profile->IsBackendReady()))
    {
        return;
    }
    Super::RestartPlayer(NewPlayer);
    if (bLegacyMode || bFrontEnd || !NewPlayer || !NewPlayer->GetPawn<AFableCharacter>())
    {
        return;
    }
    AFablePartyEncounter* Encounter = GetExpeditionFor(Profile);
    if (Profile)
    {
        Profile->ActiveExpeditionId = Encounter ? Encounter->GetExpeditionId() : FGuid();
        Profile->ForceNetUpdate();
    }
    const bool bDungeon = Encounter && !Encounter->GetProgress().bPartyRecoveryPending && !Encounter->IsReturning();
    AFableCharacter* Hero = NewPlayer->GetPawn<AFableCharacter>();
    const int32 Slot = Encounter ? Encounter->GetTicket().MemberKeys.IndexOfByKey(Profile->PartyPlayerKey)
                                 : GetConnectionSlot(NewPlayer);
    if (bDungeon)
    {
        Hero->SetCheckpoint(FableRPG::RegionEntrance(Encounter->GetTicket().Region) + Encounter->GetWorldOrigin() +
                                FVector(Slot * 240, 0, 10),
                            FRotator(0, 90, 0));
    }
    else
    {
        MovePlayerToCheckpoint(NewPlayer, EFableRegion::Village, Slot);
    }
}

/** 기존 포털 진입점도 같은 준비와 원정 소유권을 따른다. */
void AFableGameMode::RequestEnterDungeon(APlayerController* Requester)
{
    RequestEnterRegion(Requester, EFableRegion::RootRuins);
}

/** 새 입장은 전원 동의를 요구하고 기존 참가자는 자기 증서로만 복귀한다. */
void AFableGameMode::RequestEnterRegion(APlayerController* Requester, EFableRegion Region, EFableDifficulty Difficulty)
{
    if (bLegacyMode || !IsRequesterAlive(Requester))
    {
        return;
    }
    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();
    if (!Profile || !CanUseTownService(Requester, FableRPG::PortalLocation(), 330))
    {
        return;
    }
    if (AFablePartyEncounter* Encounter = GetExpeditionFor(Profile))
    {
        if (Region == Encounter->GetTicket().Region)
        {
            Encounter->Resume(Requester);
        }
        return;
    }
    const auto* Parties = AFablePartyManager::Find(GetWorld());
    const auto* Party = Parties ? Parties->GetPartyForMember(Profile->PartyPlayerKey) : nullptr;
    if (!Party || Party->SelectedRegion != Region || Party->SelectedDifficulty != Difficulty)
    {
        Profile->SetNotice(TEXT("G 파티 창에서 목적지와 난이도를 선택하고 모두 준비하세요."));
        return;
    }
    RequestStartExpedition(Requester, Party->PartyId, Party->PreparationRevision);
}

/** 전원 동의와 배치 성공을 확인한 뒤 원정별 진행을 시작한다. */
bool AFableGameMode::RequestStartExpedition(APlayerController* Requester, FGuid PartyId,
                                            int32 ExpectedPreparationRevision)
{
    AFablePlayerState* Profile = IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr;
    AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    const FFablePartyRecord* Party = Parties && Profile ? Parties->GetPartyForMember(Profile->PartyPlayerKey) : nullptr;
    if (Party && Party->RetrySourceExpeditionId.IsValid())
    {
        Profile->SetNotice(TEXT("재도전 결과를 확인한 전용 입장 요청이 필요합니다."));
        return false;
    }
    return StartPreparedExpedition(Requester, PartyId, ExpectedPreparationRevision);
}

/** 신규 입장과 완료 원정 교체가 같은 전원 검증·도착 확정 경계를 사용한다. */
bool AFableGameMode::StartPreparedExpedition(APlayerController* Requester, FGuid PartyId,
                                             int32 ExpectedPreparationRevision, AFablePartyEncounter* ReplacedEncounter)
{
    auto* Profile = IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr;
    if (!Profile || Profile->GetOwner() != Requester || !IsRequesterAlive(Requester) || bLegacyMode)
    {
        return false;
    }
    if (PendingAdmissions.Contains(PartyId) || (IsPartyExpeditionActive(PartyId) && !ReplacedEncounter))
    {
        Profile->SetNotice(TEXT("해당 파티의 원정이 이미 진행 중입니다."));
        return false;
    }
    auto* Parties = AFablePartyManager::Find(GetWorld());
    FFableExpeditionTicket Ticket;
    if (!Parties || !Parties->BuildReadySnapshot(Requester, PartyId, ExpectedPreparationRevision, Ticket))
    {
        return false;
    }
    if (!ReplacedEncounter && !CanUseTownService(Requester, FableRPG::PortalLocation(), 330))
    {
        Profile->SetNotice(TEXT("파티장이 마을의 파란 포털 근처에서 입장을 요청해야 합니다."));
        return false;
    }
    TArray<APlayerController*> Players;
    if (!ValidateAdmission(Ticket, Players, ReplacedEncounter))
    {
        return false;
    }
    // 모든 참가자는 가장 최근 결과를 명시적으로 확인한 뒤 다음 원정을 시작한다.
    for (const FString& Key : Ticket.MemberKeys)
    {
        APlayerController* Member = Parties->GetConnectedPlayer(Key);
        AFablePlayerState* Other = Member ? Member->GetPlayerState<AFablePlayerState>() : nullptr;
        if (!HasAcknowledgedLatestExpeditionResult(Other))
        {
            Profile->SetNotice(TEXT("모든 파티원이 이전 원정 결과를 먼저 확인하세요."));
            if (Other && Other != Profile)
            {
                Other->SetNotice(TEXT("이전 원정 결과를 먼저 확인하세요."));
            }
            return false;
        }
    }
    if (!EnsureResultCapacity(Ticket))
    {
        Profile->SetNotice(TEXT("이전 보상 저장 또는 모험 기록 확인을 마친 뒤 다시 입장하세요."));
        return false;
    }

    // 파괴된 슬롯만 재사용하며 미확정 Actor를 다른 입장 요청에서 보이지 않게 한다.
    TSet<int32> UsedSlots;
    for (auto It = Expeditions.CreateIterator(); It; ++It)
    {
        if (!IsValid(It.Value()) || !It.Value()->IsCommitted())
        {
            It.RemoveCurrent();
        }
        else
        {
            UsedSlots.Add(FMath::RoundToInt(It.Value()->GetWorldOrigin().Y / 30000));
        }
    }
    int32 Slot = 1;
    while (UsedSlots.Contains(Slot) && Slot <= 16)
    {
        ++Slot;
    }
    if (Slot > 16)
    {
        Profile->SetNotice(TEXT("원정 공간이 가득 찼습니다. 잠시 후 다시 시도하세요."));
        return false;
    }
    PendingAdmissions.Add(PartyId);
    AFablePartyEncounter* Encounter = GetWorld()->SpawnActor<AFablePartyEncounter>();
    const bool bPrepared = Encounter && Encounter->InitializeExpedition(Ticket, FVector(0, Slot * 30000, 0));
    if (bPrepared)
    {
        Expeditions.Add(Ticket.ExpeditionId, Encounter);
    }
    if (ReplacedEncounter)
    {
        ReplacedEncounter->bTransitionPending = true;
    }
    const bool bAdmitted = bPrepared && Encounter->Admit(Players, ReplacedEncounter);
    if (IsValid(ReplacedEncounter))
    {
        ReplacedEncounter->bTransitionPending = false;
    }
    PendingAdmissions.Remove(PartyId);
    if (!bAdmitted)
    {
        if (IsValid(Encounter))
        {
            Encounter->Destroy();
        }
        Expeditions.Remove(Ticket.ExpeditionId);
        Parties->InvalidatePreparation(PartyId);
        Profile->SetNotice(TEXT("던전 도착 지점을 확보하지 못해 원래 위치로 돌아왔습니다. 다시 준비하세요."));
        return false;
    }
    return true;
}

/** 봉인 요청을 요청자 소유 원정에만 전달한다. */
void AFableGameMode::RequestCleanseSeal(APlayerController* Requester, int32 SealIndex)
{
    if (auto* Encounter =
            GetExpeditionFor(IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr))
    {
        Encounter->RequestCleanseSeal(Requester, SealIndex);
    }
}

/** 귀환 요청은 다른 파티의 진행에 영향을 주지 않는다. */
void AFableGameMode::RequestReturnVillage(APlayerController* Requester)
{
    if (auto* Encounter =
            GetExpeditionFor(IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr))
    {
        Encounter->RequestReturnVillage(Requester);
    }
}

/** 이동 직전 명부와 현재 소유 연결을 다시 확인한다. */
bool AFableGameMode::ValidateAdmission(const FFableExpeditionTicket& Ticket, TArray<APlayerController*>& OutPlayers,
                                       const AFablePartyEncounter* ReplacedEncounter) const
{
    // 확정 직전의 명부·선택을 다시 읽어 준비 단계와 이동 단계의 경계를 닫는다.
    OutPlayers.Reset();
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    const FFablePartyRecord* Party =
        Parties && !Ticket.MemberKeys.IsEmpty() ? Parties->GetPartyForMember(Ticket.MemberKeys[0]) : nullptr;
    if (!Ticket.IsValid() || !Party || Party->PartyId != Ticket.PartyId ||
        Party->PreparationRevision != Ticket.PreparationRevision || Party->Members.Num() != Ticket.MemberKeys.Num() ||
        Party->SelectedRegion != Ticket.Region || Party->SelectedDifficulty != Ticket.Difficulty)
    {
        return false;
    }

    // 전원 검사를 마치기 전에는 이동이나 프로필 binding을 변경하지 않는다.
    for (const FString& Key : Ticket.MemberKeys)
    {
        APlayerController* Player = Parties->GetConnectedPlayer(Key);
        const AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        const AFableCharacter* Hero = Player ? Player->GetPawn<AFableCharacter>() : nullptr;
        const FFablePartyMember* Member = Party->Members.FindByPredicate(
            [&](const FFablePartyMember& Entry)
            {
                return Entry.PlayerKey == Key;
            });
        if (!Player || !Profile || Profile->GetOwner() != Player || !Profile->bProfileLoaded ||
            (Profile->IsBackendEnabled() && !Profile->IsBackendReady()) || Profile->IsBackendSavePending() ||
            (Profile->ActiveExpeditionId.IsValid() &&
             (!ReplacedEncounter || Profile->ActiveExpeditionId != ReplacedEncounter->GetExpeditionId() ||
              !ReplacedEncounter->IsParticipant(Profile))) ||
            Profile->PartyId != Ticket.PartyId || !Hero || !Hero->IsAlive() || Hero->GetController() != Player ||
            Hero->GetPlayerState<AFablePlayerState>() != Profile || !Profile->IsExpeditionEligible(Ticket.Region) ||
            (!FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation()) &&
             (!ReplacedEncounter || !IsRetryPreparationParticipant(Profile, Party->RetrySourceExpeditionId,
                                                                   Party->RetrySourceCompletionId))) ||
            !Member || !Member->bConnected || Member->ReadyRevision != Ticket.PreparationRevision)
        {
            return false;
        }

        OutPlayers.Add(Player);
    }

    return OutPlayers.Num() == Ticket.MemberKeys.Num();
}

/** 적 UUID에 따라 독립 원정과 공유 필드의 처치 경로를 구분한다. */
void AFableGameMode::HandleEnemyDefeated(AFableEnemy* Enemy)
{
    if (Enemy && Enemy->GetExpeditionId().IsValid())
    {
        if (auto* Encounter = FindExpedition(Enemy->GetExpeditionId()))
        {
            Encounter->HandleEnemyDefeated(Enemy);
        }
        return;
    }
    // Only a committed death of a member of this encounter can award once.
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsValid(Enemy) || Enemy->IsAlive() ||
        ActiveEnemies.Remove(TWeakObjectPtr<AFableEnemy>(Enemy)) == 0)
    {
        return;
    }

    AFableGameState* RootState = GetGameState<AFableGameState>();
    FFableAdventureProgress* State = RootState ? &RootState->FieldProgress : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    const bool bBoss = Enemy->IsBoss();

    // 생존 상태와 부활 진행 처리
    if (!bLegacyMode)
    {
        for (APlayerController* PC : GetEncounterPlayers())
        {
            // 현재 월드와 플레이어 참조에 필요한 값 준비
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

            // 지역·적·전투 진행 처리
            if (bForestCredit)
            {
                Profile->RecordKill(bBoss);
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
            if (!It->GetExpeditionId().IsValid())
            {
                It->Destroy();
            }
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
    }

    // 생존 상태와 부활 진행 처리
    RootState->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_OBJECTIVE progress=%d/%d complete=%d"), State->DefeatedEnemies,
           State->RequiredEnemies, State->bObjectiveComplete);
}

/** 보스 체력도 해당 보스의 원정 UUID로만 전달한다. */
void AFableGameMode::UpdateBossHealth(AFableEnemy* Enemy)
{
    if (Enemy && Enemy->GetExpeditionId().IsValid())
    {
        if (auto* Encounter = FindExpedition(Enemy->GetExpeditionId()))
        {
            Encounter->UpdateBossHealth(Enemy);
        }
    }
}

/** 필드 구조에서는 빈 원정 UUID인 투사체만 제거한다. */
void AFableGameMode::ClearRecoveryCombat()
{
    ClearEncounter();
    for (TActorIterator<AFableProjectile> It(GetWorld()); It; ++It)
    {
        if (!It->GetExpeditionId().IsValid())
        {
            It->Destroy();
        }
    }
}
