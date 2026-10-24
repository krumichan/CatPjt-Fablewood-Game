#include "FablePartyEncounter.h"
#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableDungeonWorld.h"
#include "FableEnemy.h"
#include "FableEnemyBolt.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FableMoonwellWorld.h"
#include "FablePartyManager.h"
#include "FablePlayerState.h"
#include "FableProjectile.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"

/** 진행 Actor는 다른 원정과 독립적으로 복제한다. */
AFablePartyEncounter::AFablePartyEncounter()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    SetNetUpdateFrequency(10);
}

/** 각 원정의 참가자 감시는 자기 타이머만 사용한다. */
void AFablePartyEncounter::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        GetWorldTimerManager().SetTimer(PartyMonitor, this, &AFablePartyEncounter::CheckPartyState, .5f, true);
    }
}

/** 파괴 시 다른 원정의 적과 배경에는 손대지 않는다. */
void AFablePartyEncounter::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(PartyMonitor);
    GetWorldTimerManager().ClearTimer(EncounterTimer);
    if (HasAuthority())
    {
        ClearEncounter();
        if (IsValid(Environment))
        {
            Environment->Destroy();
        }
    }
    Super::EndPlay(Reason);
}

/** 격리 fixture처럼 BeginPlay 전 파괴되는 원정도 자기 배경을 남기지 않는다. */
void AFablePartyEncounter::Destroyed()
{
    if (HasAuthority())
    {
        if (bCommitted && GetWorld() && !GetWorld()->bIsTearingDown)
        {
            if (AFableGameMode* Mode = AFableGameMode::Find(GetWorld()))
            {
                Mode->CompleteExpeditionResult(this, EFableExpeditionOutcome::Abandoned);
            }
        }
        ClearEncounter();
        if (IsValid(Environment))
        {
            Environment->Destroy();
        }
    }
    Super::Destroyed();
}

/** 공개 키만 담은 증서와 진행·원점을 함께 복제한다. */
void AFablePartyEncounter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFablePartyEncounter, DepartureVote);
    DOREPLIFETIME(AFablePartyEncounter, Ticket);
    DOREPLIFETIME(AFablePartyEncounter, Progress);
    DOREPLIFETIME(AFablePartyEncounter, WorldOrigin);
    DOREPLIFETIME(AFablePartyEncounter, bCommitted);
}

/** 증서는 생성 시 한 번만 설정하며 이후 명부 변경으로 다시 쓰지 않는다. */
bool AFablePartyEncounter::InitializeExpedition(const FFableExpeditionTicket& InTicket, FVector InOrigin)
{
    if (!HasAuthority() || Ticket.IsValid() || !InTicket.IsValid() || InOrigin.ContainsNaN())
    {
        return false;
    }
    Ticket = InTicket;
    WorldOrigin = InOrigin;
    return true;
}

/** 현재 증서와 프로필의 소속·공개 키·실제 연결을 함께 확인한다. */
bool AFablePartyEncounter::IsParticipant(const AFablePlayerState* Profile) const
{
    if (!IsValid(Profile) || Profile->GetWorld() != GetWorld() || !bCommitted ||
        Profile->ActiveExpeditionId != Ticket.ExpeditionId || Profile->PartyId != Ticket.PartyId ||
        !Ticket.Contains(Profile->PartyPlayerKey))
    {
        return false;
    }
    if (HasAuthority())
    {
        const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
        const APlayerController* Controller = Cast<APlayerController>(Profile->GetOwner());
        return Parties && Controller && Controller->GetPlayerState<AFablePlayerState>() == Profile &&
               Parties->GetConnectedPlayer(Profile->PartyPlayerKey) == Controller;
    }
    return true;
}

/** 실제 연결 목록에서 자기 참가자만 반환한다. */
TArray<APlayerController*> AFablePartyEncounter::GetParticipants() const
{
    TArray<APlayerController*> Result;
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    if (Parties)
    {
        for (const FString& Key : Ticket.MemberKeys)
        {
            APlayerController* Player = Parties->GetConnectedPlayer(Key);
            if (Player && IsParticipant(Player->GetPlayerState<AFablePlayerState>()))
            {
                Result.Add(Player);
            }
        }
    }
    return Result;
}

/** 원정의 불변 증서를 읽는다. */
const FFableExpeditionTicket& AFablePartyEncounter::GetTicket() const
{
    return Ticket;
}

/** 원정 진행의 읽기 전용 정본을 제공한다. */
const FFableAdventureProgress& AFablePartyEncounter::GetProgress() const
{
    return Progress;
}

/** 원정의 고정 UUID를 반환한다. */
FGuid AFablePartyEncounter::GetExpeditionId() const
{
    return Ticket.ExpeditionId;
}

/** 원정 지형의 좌표 원점을 반환한다. */
FVector AFablePartyEncounter::GetWorldOrigin() const
{
    return WorldOrigin;
}

/** 아직 이동 중인 원정은 외부에서 유효 진행으로 보이지 않는다. */
bool AFablePartyEncounter::IsCommitted() const
{
    return bCommitted;
}

/** 귀환 진행 중인 재접속자는 던전 대신 마을에 도착한다. */
bool AFablePartyEncounter::IsReturning() const
{
    return bReturnPending;
}

/** 참가자의 생존 상태와 구조 대기를 확인한다. */
bool AFablePartyEncounter::IsRequesterAlive(APlayerController* Requester) const
{
    const AFableCharacter* Hero = IsValid(Requester) ? Requester->GetPawn<AFableCharacter>() : nullptr;
    return HasAuthority() && Hero && Hero->IsAlive() && !Progress.bPartyRecoveryPending &&
           IsParticipant(Requester->GetPlayerState<AFablePlayerState>());
}

/** 이미 변환된 서비스 좌표와 실제 위치를 비교한다. */
bool AFablePartyEncounter::CanUseTownService(APlayerController* Requester, FVector Location, float Radius) const
{
    return IsRequesterAlive(Requester) && FMath::IsFinite(Radius) && Radius > 0 &&
           FVector::DistSquared2D(Requester->GetPawn()->GetActorLocation(), Location) <= FMath::Square(Radius) &&
           FMath::Abs(Requester->GetPawn()->GetActorLocation().Z - Location.Z) < 300;
}

/** 최종 원점을 적용한 뒤 해당 지역 배경만 생성한다. */
bool AFablePartyEncounter::CreateEnvironment()
{
    const FTransform Transform(FRotator::ZeroRotator, WorldOrigin);
    if (Ticket.Region == EFableRegion::RootRuins)
    {
        auto* Dungeon = GetWorld()->SpawnActorDeferred<AFableDungeonWorld>(
            AFableDungeonWorld::StaticClass(), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Dungeon)
        {
            return false;
        }
        Dungeon->BindExpedition(Ticket.ExpeditionId, true);
        UGameplayStatics::FinishSpawningActor(Dungeon, Transform);
        Environment = Dungeon;
    }
    else
    {
        auto* Moonwell = GetWorld()->SpawnActorDeferred<AFableMoonwellWorld>(
            AFableMoonwellWorld::StaticClass(), Transform, this, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Moonwell)
        {
            return false;
        }
        Moonwell->BindExpedition(Ticket.ExpeditionId);
        UGameplayStatics::FinishSpawningActor(Moonwell, Transform);
        Environment = Moonwell;
    }
    return IsValid(Environment);
}

/** 参加者全員の到着確認後にのみ進行とbindingを確定する。 */
bool AFablePartyEncounter::Admit(const TArray<APlayerController*>& Players,
                                 AFablePartyEncounter* ReplacedCompletedEncounter)
{
    if (!HasAuthority() || !Ticket.IsValid() || bCommitted || bAdmissionPending ||
        Players.Num() != Ticket.MemberKeys.Num())
    {
        return false;
    }

    // 교체는 같은 파티의 실제 완료 원정과 새 명시적 준비를 모두 재검사한다.
    AFableGameMode* Mode = AFableGameMode::Find(GetWorld());
    if (ReplacedCompletedEncounter)
    {
        TArray<APlayerController*> Verified;
        if (!Mode || ReplacedCompletedEncounter == this || !ReplacedCompletedEncounter->IsCommitted() ||
            ReplacedCompletedEncounter->GetWorld() != GetWorld() || ReplacedCompletedEncounter->IsReturning() ||
            ReplacedCompletedEncounter->GetTicket().PartyId != Ticket.PartyId ||
            ReplacedCompletedEncounter->GetProgress().AdventureStage != EFableAdventureStage::Cleared ||
            !Mode->ValidateAdmission(Ticket, Verified, ReplacedCompletedEncounter) || Verified.Num() != Players.Num())
        {
            return false;
        }
    }

    // public 서버 API를 직접 호출해도 증서 밖의 연결을 binding할 수 없다.
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    TSet<FString> UniqueKeys;
    for (APlayerController* Player : Players)
    {
        const AFablePlayerState* Profile = IsValid(Player) ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        const AFableCharacter* Hero = IsValid(Player) ? Player->GetPawn<AFableCharacter>() : nullptr;
        if (!Parties || !Profile || Profile->GetOwner() != Player || Profile->GetWorld() != GetWorld() ||
            !Profile->bProfileLoaded || (Profile->IsBackendEnabled() && !Profile->IsBackendReady()) ||
            Profile->IsBackendSavePending() ||
            (Profile->ActiveExpeditionId.IsValid() &&
             (!ReplacedCompletedEncounter ||
              Profile->ActiveExpeditionId != ReplacedCompletedEncounter->GetExpeditionId() ||
              !ReplacedCompletedEncounter->IsParticipant(Profile))) ||
            Profile->PartyId != Ticket.PartyId || !Ticket.Contains(Profile->PartyPlayerKey) ||
            UniqueKeys.Contains(Profile->PartyPlayerKey) ||
            Parties->GetConnectedPlayer(Profile->PartyPlayerKey) != Player || !Hero || !Hero->IsAlive() ||
            Hero->GetPlayerState<AFablePlayerState>() != Profile)
        {
            return false;
        }
        UniqueKeys.Add(Profile->PartyPlayerKey);
    }
    if ((Mode && !Mode->EnsureResultCapacity(Ticket)) || !CreateEnvironment())
    {
        return false;
    }
    TGuardValue<bool> Guard(bAdmissionPending, true);
    if (!MoveAdmissionParticipants(Players))
    {
        return false;
    }
    Progress.ActiveRegion = Ticket.Region;
    Progress.ActiveDifficulty = Ticket.Difficulty;
    Progress.AdventureStage = EFableAdventureStage::Dungeon;
    Progress.DungeonWave = 1;
    Progress.EncounterSerial = 1;
    bCommitted = true;
    for (APlayerController* Player : Players)
    {
        AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
        Profile->ActiveExpeditionId = Ticket.ExpeditionId;
        Profile->ForceNetUpdate();
    }
    RefreshDepartureConnections();
    if (Mode)
    {
        Mode->RegisterExpeditionResults(Ticket);
    }
    ForceNetUpdate();
    if (Ticket.Region == EFableRegion::Moonwell)
    {
        SpawnMoonSealGroup(0);
    }
    else
    {
        SpawnDungeonWave(1);
    }
    return IsValid(this) && bCommitted && Progress.ActiveRegion == Ticket.Region;
}

/** 同じ証書の現在接続だけを元の入口に戻す。 */
bool AFablePartyEncounter::Resume(APlayerController* Requester)
{
    if (!IsRequesterAlive(Requester) || !CanUseTownService(Requester, FableRPG::PortalLocation()))
    {
        return false;
    }
    if (bReturnPending)
    {
        ReturnPartyToVillage(bReturnAfterWipe);
        return false;
    }
    return MovePlayerToCheckpoint(Requester, Ticket.Region);
}

/** 정확한 명부 순서로 회복 없는 이동을 시도하고 실패하면 전원 원상복구한다. */

bool AFablePartyEncounter::MoveAdmissionParticipants(const TArray<APlayerController*>& Players)
{
    return MoveParticipantsAtomically(Players, Ticket.Region, WorldOrigin, false);
}

/** 실패한 임시 이동은 게임 전투 상태를 바꾸지 않고 이동 정보만 복원한다. */
bool AFablePartyEncounter::MoveParticipantsAtomically(const TArray<APlayerController*>& Players, EFableRegion Region,
                                                      FVector Origin, bool bRestore)
{
    // 전투 상태 대신 임시 이동에서 바뀔 수 있는 엔진 이동 정보만 보관한다.
    struct FTravelState
    {
        FTransform Transform;
        FRotator View;
        FVector Velocity;
        EMovementMode Mode = MOVE_None;
        uint8 CustomMode = 0;
        FMovementBaseInterfaceData Base;
        FName Bone;
        FFindFloorResult Floor;
        bool bTeleported = false;
        bool bForceFloor = false;
    };
    TArray<FTravelState> Before;
    TArray<FVector> Destinations;
    const FRotator Rotation(0, 90, 0);
    for (int32 Index = 0; Index < Players.Num(); ++Index)
    {
        AFableCharacter* Hero = Players[Index] ? Players[Index]->GetPawn<AFableCharacter>() : nullptr;
        UCharacterMovementComponent* Movement = Hero ? Hero->GetCharacterMovement() : nullptr;
        FVector Destination = Region == EFableRegion::Village
                                  ? FVector(Index * 240, -1080, 110)
                                  : FableRPG::RegionEntrance(Region) + Origin + FVector(Index * 240, 0, 10);
        if (!Hero || !Movement || !Hero->HasAuthority() || !GetWorld()->FindTeleportSpot(Hero, Destination, Rotation) ||
            !FableRPG::IsInRegion(Region, Destination - Origin))
        {
            return false;
        }
        FTravelState State;
        State.Transform = Hero->GetActorTransform();
        State.View = Players[Index]->GetControlRotation();
        State.Velocity = Movement->Velocity;
        State.Mode = Movement->MovementMode;
        State.CustomMode = Movement->CustomMovementMode;
        if (const FMovementBaseInterfaceData* Base = Hero->GetMovementBaseInterfaceData())
        {
            State.Base = *Base;
        }
        State.Bone = Hero->GetBasedMovement().BoneName;
        State.Floor = Movement->CurrentFloor;
        State.bTeleported = Movement->bJustTeleported;
        State.bForceFloor = Movement->bForceNextFloorCheck;
        Before.Add(State);
        Destinations.Add(Destination);
    }

    // 임시 단계에서는 SetCheckpoint를 호출하지 않아 cooldown·shield·투사체를 보존한다.
    bool bAllArrived = true;
    for (int32 Index = 0; Index < Players.Num(); ++Index)
    {
        AFableCharacter* Hero = Players[Index]->GetPawn<AFableCharacter>();
        if (!Hero || !Hero->TeleportTo(Destinations[Index], Rotation, false, false))
        {
            bAllArrived = false;
            break;
        }
        const FVector Actual = Hero->GetActorLocation();
        if (!Hero->IsAlive() || !FableRPG::IsInRegion(Region, Actual - Origin) ||
            FVector::DistSquared2D(Actual, Destinations[Index]) > FMath::Square(150.0f) ||
            FMath::Abs(Actual.Z - Destinations[Index].Z) > 180.0f)
        {
            bAllArrived = false;
            break;
        }
    }
    if (!bAllArrived)
    {
        // 원래 지형이 살아 있는 동안 이동 기반·floor·속도·시점까지 되돌린다.
        for (int32 Index = 0; Index < Players.Num(); ++Index)
        {
            AFableCharacter* Hero = Players[Index] ? Players[Index]->GetPawn<AFableCharacter>() : nullptr;
            if (!Hero)
            {
                continue;
            }
            UCharacterMovementComponent* Movement = Hero->GetCharacterMovement();
            FTravelState& State = Before[Index];
            Hero->SetActorTransform(State.Transform, false, nullptr, ETeleportType::TeleportPhysics);
            Movement->SetMovementMode(State.Mode, State.CustomMode);
            Hero->SetBase(&State.Base, State.Bone);
            Movement->CurrentFloor = State.Floor;
            Movement->Velocity = State.Velocity;
            Movement->bJustTeleported = State.bTeleported;
            Movement->bForceNextFloorCheck = State.bForceFloor;
            Players[Index]->SetControlRotation(State.View);
            Players[Index]->ClientSetRotation(State.View, true);
        }
        return false;
    }

    // 전원 도착 후에는 충돌검사를 다시 하지 않고 현재 위치에서 한 번만 확정한다.
    for (APlayerController* Player : Players)
    {
        Player->GetPawn<AFableCharacter>()->CommitCheckpointAtCurrentPosition(Rotation, bRestore);
    }
    return true;
}

/** 참가자 위치 이동은 원정의 고정 슬롯과 원점을 사용한다. */
bool AFablePartyEncounter::MovePlayerToCheckpoint(APlayerController* Player, EFableRegion Region, bool bRestore)
{
    AFablePlayerState* Profile = IsValid(Player) ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    if (!Profile || !IsParticipant(Profile))
    {
        return false;
    }
    AFableCharacter* Hero = Player->GetPawn<AFableCharacter>();
    if (!Hero && !Player->GetPawn())
    {
        if (auto* Mode = AFableGameMode::Find(GetWorld()))
        {
            Mode->RestartPlayer(Player);
        }
        Hero = Player->GetPawn<AFableCharacter>();
    }
    if (!Hero)
    {
        return false;
    }
    const int32 Slot = Ticket.MemberKeys.IndexOfByKey(Profile->PartyPlayerKey);
    const FVector Destination = Region == EFableRegion::Village
                                    ? FVector(Slot * 240, -1080, 110)
                                    : FableRPG::RegionEntrance(Region) + WorldOrigin + FVector(Slot * 240, 0, 10);
    const TArray<FVector> Offsets = {FVector::ZeroVector, FVector(0, 200, 0), FVector(0, -200, 0), FVector(420, 0, 0),
                                     FVector(-420, 0, 0)};
    for (const FVector& Offset : Offsets)
    {
        FVector Candidate = Destination + Offset;
        if (!GetWorld()->FindTeleportSpot(Hero, Candidate, FRotator(0, 90, 0)))
        {
            continue;
        }
        Hero->SetCheckpoint(Candidate, FRotator(0, 90, 0), bRestore);
        const FVector Actual = Hero->GetActorLocation();
        const FVector Canonical = Region == EFableRegion::Village ? Actual : Actual - WorldOrigin;
        if (Hero->IsAlive() && FableRPG::IsInRegion(Region, Canonical) &&
            FVector::DistSquared2D(Actual, Candidate) <= FMath::Square(150.f) &&
            FMath::Abs(Actual.Z - Candidate.Z) <= 180)
        {
            return true;
        }
    }
    return false;
}

/** 부분 귀환 실패 시 이미 도착한 참가자는 다시 회복하지 않는다. */
bool AFablePartyEncounter::MoveParty(EFableRegion Region)
{
    bool bAllMoved = true;
    for (APlayerController* Player : GetParticipants())
    {
        const AFableCharacter* Hero = Player->GetPawn<AFableCharacter>();
        const bool bAlreadyReturned = Region == EFableRegion::Village && bReturnPending && Hero && Hero->IsAlive() &&
                                      FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation());
        bAllMoved = (bAlreadyReturned || MovePlayerToCheckpoint(Player, Region)) && bAllMoved;
    }
    return bAllMoved;
}

/** 原征の戦闘がまだ進行中か確認する。 */
bool AFablePartyEncounter::IsDungeonRunning() const
{
    return bCommitted && !Progress.bPartyRecoveryPending &&
           (Progress.AdventureStage == EFableAdventureStage::Dungeon ||
            Progress.AdventureStage == EFableAdventureStage::Boss);
}

/** 同じ原征に再挑戦できる現在接続の依頼だけを調べる。 */
bool AFablePartyEncounter::HasRetryQuest() const
{
    for (APlayerController* Player : GetParticipants())
    {
        if (const auto* Profile = Player->GetPlayerState<AFablePlayerState>();
            Profile && Profile->IsExpeditionEligible(Ticket.Region))
        {
            return true;
        }
    }
    return false;
}

/** 終了した証書は現在接続のbindingだけを解除して破棄する。 */
void AFablePartyEncounter::FinishExpedition()
{
    if (!HasAuthority() || !Ticket.IsValid())
    {
        return;
    }
    if (AFableGameMode* Mode = AFableGameMode::Find(GetWorld()))
    {
        Mode->CompleteExpeditionResult(this, EFableExpeditionOutcome::Abandoned);
    }
    for (TActorIterator<AFablePlayerState> It(GetWorld()); It; ++It)
    {
        if (It->ActiveExpeditionId == Ticket.ExpeditionId && Ticket.Contains(It->PartyPlayerKey))
        {
            It->ActiveExpeditionId.Invalidate();
            It->ForceNetUpdate();
        }
    }
    bCommitted = false;
    if (auto* Parties = AFablePartyManager::Find(GetWorld()))
    {
        Parties->InvalidatePreparation(Ticket.PartyId);
    }
    Destroy();
}

/** NotifyParty는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::NotifyParty(const FString& Notice) const
{
    for (APlayerController* PC : GetParticipants())
    {
        if (AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>())
        {
            Profile->SetNotice(Notice);
        }
    }
}

/** ClearEncounter는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::ClearEncounter()
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
        if (It->GetExpeditionId() == Ticket.ExpeditionId)
        {
            It->Destroy();
        }
    }

    // 접속 종료 뒤 소유 pawn이 없어도 발사 시 고정한 UUID로 잔여 탄환을 제거한다.
    for (TActorIterator<AFableProjectile> It(GetWorld()); It; ++It)
    {
        if (It->GetExpeditionId() == Ticket.ExpeditionId)
        {
            It->Destroy();
        }
    }
}

/** SpawnMoonSealGroup는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::SpawnMoonSealGroup(int32 SealIndex)
{
    FFableAdventureProgress* State = &Progress;

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
        AFableEnemy* Enemy = GetWorld()->SpawnActor<AFableEnemy>(
            FableRPG::MoonSealLocation(SealIndex) + WorldOrigin + Offsets[I], FRotator(0, -90, 0), Params);
        if (Enemy)
        {
            Enemy->BindExpedition(Ticket.ExpeditionId);
            Enemy->ConfigureMoonwell(false, I == 2 || (SealIndex == 1 && I == 1), State->ActiveDifficulty);
            ActiveEnemies.Add(Enemy);
            SpawnedEncounterEnemies.Add(Enemy);
        }
    }

    // 생존 상태와 부활 진행 처리
    State->RequiredEnemies = ActiveEnemies.Num();
    State->DefeatedEnemies = 0;
    State->bObjectiveComplete = false;
    ForceNetUpdate();

    // 지역·적·전투 진행 처리
    if (ActiveEnemies.Num() != Count)
    {
        ReturnPartyToVillage(true);
        NotifyParty(TEXT("봉인을 준비하지 못했습니다. 의뢰를 유지한 채 마을로 귀환합니다."));
    }

    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_MOON_SEAL_STARTED seal=%d enemies=%d"), SealIndex, ActiveEnemies.Num());
}

/** RequestCleanseSeal는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::RequestCleanseSeal(APlayerController* Requester, int32 SealIndex)
{
    FFableAdventureProgress* State = &Progress;

    // 지역·적·전투 진행 조건 검사
    if (!bCommitted || !State || State->bPartyRecoveryPending || !IsRequesterAlive(Requester) || SealIndex < 0 ||
        SealIndex > 2 || State->ActiveRegion != EFableRegion::Moonwell ||
        !CanUseTownService(Requester, FableRPG::MoonSealLocation(SealIndex) + WorldOrigin, 330.0f))
    {
        return;
    }

    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 프로필과 저장 스냅숏 조건 검사
    if (!Profile || !Profile->bProfileLoaded || !IsParticipant(Profile))
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
    ForceNetUpdate();
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

/** SpawnDungeonWave는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::SpawnDungeonWave(int32 Wave)
{
    FFableAdventureProgress* State = &Progress;

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
        if (AFableEnemy* Enemy =
                GetWorld()->SpawnActor<AFableEnemy>(Position + WorldOrigin, FRotator(0, -90, 0), Params))
        {
            Enemy->BindExpedition(Ticket.ExpeditionId);
            Enemy->ConfigureEncounter(false, true, State->ActiveDifficulty);
            ActiveEnemies.Add(Enemy);
            SpawnedEncounterEnemies.Add(Enemy);
        }
    }

    // 생존 상태와 부활 진행 처리
    State->RequiredEnemies = ActiveEnemies.Num();
    State->DefeatedEnemies = 0;
    State->bObjectiveComplete = false;
    ForceNetUpdate();

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

/** SpawnBoss는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::SpawnBoss()
{
    FFableAdventureProgress* State = &Progress;

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
        FableRPG::RegionCenter(State->ActiveRegion) + WorldOrigin + FVector(0, 650, 190), FRotator(0, -90, 0), Params);

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
    Boss->BindExpedition(Ticket.ExpeditionId);
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
    SpawnedEncounterEnemies.Add(Boss);
    ActiveBoss = Boss;
    State->AdventureStage = EFableAdventureStage::Boss;
    State->DungeonWave = 3;
    State->RequiredEnemies = 1;
    State->DefeatedEnemies = 0;
    State->BossHealth = Boss->GetHealth();
    State->BossMaxHealth = Boss->GetMaxHealth();
    ForceNetUpdate();
    NotifyParty(bMoonwell ? TEXT("달빛 수호자: 파문 안쪽 안전지대로 들어가고, 낙하 예고 원에서 벗어나세요.")
                          : TEXT("고대 수호자: 주황색 원과 전방의 뿌리 공격 예고를 피하세요."));
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_BOSS_STARTED health=%.0f"), Boss->GetHealth());
}

/** UpdateBossHealth는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::UpdateBossHealth(AFableEnemy* Enemy)
{
    if (!HasAuthority() || !Enemy || Enemy->GetExpeditionId() != Ticket.ExpeditionId || ActiveBoss.Get() != Enemy)
    {
        return;
    }
    if (FFableAdventureProgress* State = &Progress)
    {
        State->BossHealth = Enemy->GetHealth();
        State->BossMaxHealth = Enemy->GetMaxHealth();
        ForceNetUpdate();
    }
}

/** HandleEnemyDefeated는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::HandleEnemyDefeated(AFableEnemy* Enemy)
{
    // Only a committed death of a member of this encounter can award once.
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bCommitted || bTransitionPending || !IsValid(Enemy) ||
        Enemy->GetExpeditionId() != Ticket.ExpeditionId || Enemy->IsAlive() ||
        ActiveEnemies.Remove(TWeakObjectPtr<AFableEnemy>(Enemy)) == 0)
    {
        return;
    }

    FFableAdventureProgress* State = &Progress;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    RecordedDefeats.Add(Enemy->GetSpawnId());
    const bool bBoss = Enemy->IsBoss();

    // 생존 상태와 부활 진행 처리
    if (bCommitted)
    {
        for (APlayerController* PC : GetParticipants())
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
                                       FableAdventure::For(this).IsInRegion(EFableRegion::Village, PawnLocation) &&
                                       Profile->ForestQuest == EFableQuestStage::Active;
            const bool bDungeonCredit = IsParticipant(Profile) && IsDungeonRunning() &&
                                        FableAdventure::For(this).IsInRegion(State->ActiveRegion, PawnLocation) &&
                                        Profile->IsExpeditionEligible(State->ActiveRegion);

            // 지역·적·전투 진행 처리
            if (bForestCredit)
            {
                Profile->RecordKill(bBoss);
            }
            else if (bDungeonCredit)
            {
                if (AFableGameMode* Mode = AFableGameMode::Find(GetWorld()))
                {
                    Mode->RecordExpeditionDefeat(this, Enemy, Profile);
                }
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
            if (It->GetExpeditionId() == Ticket.ExpeditionId)
            {
                It->Destroy();
            }
        }
        if (State->AdventureStage == EFableAdventureStage::Dungeon && State->ActiveRegion == EFableRegion::Moonwell)
        {
            NotifyParty(TEXT("봉인을 지키는 적이 사라졌습니다. 빛의 봉인 가까이에서 E를 누르세요."));
        }
        else if (State->AdventureStage == EFableAdventureStage::Dungeon)
        {
            const int32 NextWave = State->DungeonWave + 1;
            const FGuid ExpeditionId = Ticket.ExpeditionId;
            NotifyParty(NextWave <= 2 ? TEXT("3초 뒤 다음 수호자 무리가 나타납니다.")
                                      : TEXT("고대 수호자가 깨어나고 있습니다."));
            GetWorldTimerManager().SetTimer(
                EncounterTimer,
                [this, NextWave, ExpeditionId]()
                {
                    if (!IsDungeonRunning() || Ticket.ExpeditionId != ExpeditionId)
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
            if (AFableGameMode* Mode = AFableGameMode::Find(GetWorld()))
            {
                Mode->CompleteExpeditionResult(this, EFableExpeditionOutcome::Cleared);
            }
            NotifyParty(TEXT("수호자를 처치했습니다! 귀환 포털에서 E를 누르고 안내인에게 해당 의뢰 보상을 받으세요."));
        }
    }

    // 생존 상태와 부활 진행 처리
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_OBJECTIVE progress=%d/%d complete=%d"), State->DefeatedEnemies,
           State->RequiredEnemies, State->bObjectiveComplete);
}

/** ClearRecoveryCombat는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::ClearRecoveryCombat()
{
    ClearEncounter();
    // 원정 외 모험가의 마을 투사체는 구조 처리로 제거하지 않는다.
    for (TActorIterator<AFableProjectile> It(GetWorld()); It; ++It)
    {
        if (It->GetExpeditionId() == Ticket.ExpeditionId)
        {
            It->Destroy();
        }
    }
}

/** BeginPartyRecovery는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::BeginPartyRecovery()
{
    FFableAdventureProgress* State = &Progress;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bCommitted || !State || State->bPartyRecoveryPending)
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
        bRetryAfterRecovery = HasRetryQuest();
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
    ForceNetUpdate();
    NotifyParty(TEXT("활동 가능한 동료가 없습니다. 3초 뒤 모두 마을로 구조됩니다. 의뢰 진행과 장비는 보존됩니다."));
    UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_PARTY_RECOVERY_STARTED region=%d retry=%d serial=%d end=%.2f"),
           int32(State->RetryRegion), bRetryAfterRecovery, State->EncounterSerial, State->PartyRecoveryEndServerTime);
}

/** FinishPartyRecovery는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::FinishPartyRecovery()
{
    FFableAdventureProgress* State = &Progress;

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
    State->bRetryAvailable = bRetryAfterRecovery && HasRetryQuest();
    State->PartyRecoveryCount = State->PartyRecoveryCount < MAX_int32 ? State->PartyRecoveryCount + 1 : 1;
    ForceNetUpdate();
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
    FinishExpedition();
}

/** CheckPartyState는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::CheckPartyState()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!bCommitted || !HasAuthority() || bAdmissionPending)
    {
        return;
    }

    if (bTransitionPending)
    {
        return;
    }
    RefreshDepartureConnections();
    FFableAdventureProgress* State = &Progress;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    int32 ConnectedPlayers = 0;
    int32 LivingPlayers = 0;
    TArray<APlayerController*> Participants;

    // 현재 월드와 플레이어 참조 순회
    for (APlayerController* PC : GetParticipants())
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
        if (!Profile || !Profile->bProfileLoaded || (Profile->IsBackendEnabled() && !Profile->IsBackendReady()))
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
        // 유예 중인 전원 오프라인 원정은 새 접속자가 가로채거나 즉시 폐기하지 않는다.
        const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
        if (Ticket.IsValid() && Parties &&
            Ticket.MemberKeys.ContainsByPredicate(
                [&](const FString& Key)
                {
                    const FFablePartyRecord* Party = Parties->GetPartyForMember(Key);
                    return Party && Party->PartyId == Ticket.PartyId;
                }))
        {
            return;
        }

        ClearRecoveryCombat();
        FinishExpedition();
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
        if (MovePlayerToCheckpoint(PC, EFableRegion::Village))
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

/** ReturnPartyToVillage는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::ReturnPartyToVillage(bool bWipe)
{
    if (bTransitionPending)
    {
        return;
    }
    bReturnAfterWipe = bWipe;
    if (bWipe)
    {
        if (AFableGameMode* Mode = AFableGameMode::Find(GetWorld()))
        {
            Mode->CompleteExpeditionResult(this, EFableExpeditionOutcome::Recovered);
        }
    }
    // 지역·적·전투 진행 처리
    ClearEncounter();
    bReturnAfterWipe = bWipe;
    bReturnPending = !MoveParty(EFableRegion::Village);

    // 생존 상태와 부활 진행 처리
    if (FFableAdventureProgress* State = &Progress)
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
        ForceNetUpdate();
    }

    // 이 단계에서 현재 전투를 마무리하고 파티를 마을로 복귀시킨다.
    if (bReturnPending)
    {
        const FFableAdventureProgress* State = &Progress;
        NotifyParty(State && State->bPartyRecoveryPending
                        ? TEXT("마을 구조 위치를 확보하고 있습니다. 자동으로 다시 시도합니다.")
                        : TEXT("전투가 끝났지만 귀환 지점이 막혀 있습니다. 포털에서 E로 귀환을 다시 시도하세요."));
        return;
    }

    NotifyParty(bWipe ? TEXT("모두 구조되어 마을로 돌아왔습니다. 의뢰는 유지됩니다. 포털에서 다시 도전하세요.")
                      : TEXT("마을로 돌아왔습니다. 안내인에게 완료한 의뢰 보상을 받으세요."));

    const FFableAdventureProgress* State = &Progress;
    if (!State || !State->bPartyRecoveryPending)
    {
        FinishExpedition();
    }
}

/** RequestReturnVillage는 해당 원정의 진행과 참가자만 처리한다. */
void AFablePartyEncounter::RequestReturnVillage(APlayerController* Requester)
{
    if (bTransitionPending)
    {
        return;
    }
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!bCommitted || !IsRequesterAlive(Requester))
    {
        return;
    }

    // HTTP 요청·응답과 연결 상태에 필요한 값 준비
    FFableAdventureProgress* State = &Progress;
    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();

    // 지역·적·전투 진행 조건 검사
    if (!State || !IsParticipant(Profile) || State->ActiveRegion == EFableRegion::Village)
    {
        return;
    }

    const bool bNearPortal =
        CanUseTownService(Requester, FableRPG::RegionEntrance(State->ActiveRegion) + WorldOrigin, 350) ||
        CanUseTownService(Requester, FableRPG::RegionReturnPortal(State->ActiveRegion) + WorldOrigin, 350);

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
        for (APlayerController* Member : GetParticipants())
        {
            const AFablePlayerState* MemberProfile = Member ? Member->GetPlayerState<AFablePlayerState>() : nullptr;
            const AFableCharacter* MemberHero = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;
            if (MemberProfile && MemberProfile->bProfileLoaded && MemberProfile->StoryChapter == 4 &&
                MemberProfile->StorySealMask != 7 && MemberHero && MemberHero->IsAlive() &&
                FableAdventure::For(this).IsInRegion(EFableRegion::Moonwell, MemberHero->GetActorLocation()))
            {
                NotifyParty(TEXT("동료가 정화된 봉인을 E로 확인 중입니다. 세 봉인 조사를 마친 뒤 함께 귀환하세요."));
                return;
            }
        }
    }

    ReturnPartyToVillage(bReturnPending ? bReturnAfterWipe : false);
}

/** 실제 원정 처치에서 등록한 불변 SpawnId인지 조회한다. */
bool AFablePartyEncounter::HasRecordedDefeat(FGuid EnemySpawnId) const
{
    return bCommitted && EnemySpawnId.IsValid() && RecordedDefeats.Contains(EnemySpawnId);
}

/** 현재 진행 중 귀환 동의 스냅숏을 읽는다. */
const FFableExpeditionDepartureVote& AFablePartyEncounter::GetDepartureVote() const
{
    return DepartureVote;
}

/** 접속이 바뀌면 이전 연결이 제출한 귀환 동의를 전부 취소한다. */
void AFablePartyEncounter::RefreshDepartureConnections()
{
    if (!HasAuthority() || !bCommitted)
    {
        return;
    }
    TSet<FString> Connected;
    TMap<FString, TWeakObjectPtr<APlayerController>> Controllers;
    for (APlayerController* Player : GetParticipants())
    {
        const FString& Key = Player->GetPlayerState<AFablePlayerState>()->PartyPlayerKey;
        Connected.Add(Key);
        Controllers.Add(Key, Player);
    }

    // 같은 키가 감시 tick 사이에 재접속해도 이전 연결의 동의는 승계하지 않는다.
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    const FFablePartyRecord* Party =
        Parties && !Ticket.MemberKeys.IsEmpty() ? Parties->GetPartyForMember(Ticket.MemberKeys[0]) : nullptr;
    const bool bSameParty = Party && Party->PartyId == Ticket.PartyId;
    const int32 Revision = bSameParty ? Party->Revision : 0;
    const FString Leader = bSameParty ? Party->LeaderKey : FString();
    bool bSame = Connected.Num() == DepartureConnections.Num() && Controllers.Num() == DepartureControllers.Num() &&
                 Revision == DeparturePartyRevision && Leader == DepartureLeaderKey;
    for (const FString& Key : Connected)
    {
        const TWeakObjectPtr<APlayerController>* Previous = DepartureControllers.Find(Key);
        const TWeakObjectPtr<APlayerController>* Current = Controllers.Find(Key);
        bSame &= DepartureConnections.Contains(Key) && Previous && Current && *Previous == *Current;
    }
    if (!bSame)
    {
        DepartureConnections = MoveTemp(Connected);
        DepartureControllers = MoveTemp(Controllers);
        DeparturePartyRevision = Revision;
        DepartureLeaderKey = Leader;
        DepartureVote.ConsentingMemberKeys.Reset();
        DepartureVote.Revision = DepartureVote.Revision < MAX_int32 ? DepartureVote.Revision + 1 : 1;
        ForceNetUpdate();
    }
}

/** 같은 원래 증서 전원이 현재 연결에서 명시적으로 동의해야 철수한다. */
bool AFablePartyEncounter::SetAbandonConsent(APlayerController* Requester, FGuid ExpectedExpeditionId,
                                             int32 ExpectedDepartureRevision, bool bConsent)
{
    if (!HasAuthority() || bTransitionPending || ExpectedExpeditionId != Ticket.ExpeditionId || !IsDungeonRunning() ||
        bReturnPending || !IsRequesterAlive(Requester))
    {
        return false;
    }
    RefreshDepartureConnections();
    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();
    if (DepartureVote.Revision != ExpectedDepartureRevision || Profile->IsBackendSavePending() ||
        (Profile->IsBackendEnabled() && !Profile->IsBackendReady()))
    {
        return false;
    }
    if (bConsent)
    {
        DepartureVote.ConsentingMemberKeys.AddUnique(Profile->PartyPlayerKey);
    }
    else
    {
        DepartureVote.ConsentingMemberKeys.Remove(Profile->PartyPlayerKey);
    }
    ForceNetUpdate();
    if (!bConsent || DepartureVote.ConsentingMemberKeys.Num() != Ticket.MemberKeys.Num())
    {
        return true;
    }
    return CommitAbandon();
}

/** 전원 동의·현재 접속을 다시 검사한 뒤 전원 도착 시에만 원정을 종료한다. */
bool AFablePartyEncounter::CommitAbandon()
{
    const TArray<APlayerController*> Players = GetParticipants();
    if (Players.Num() != Ticket.MemberKeys.Num() || DepartureConnections.Num() != Ticket.MemberKeys.Num())
    {
        return false;
    }
    for (APlayerController* Player : Players)
    {
        const AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
        const AFableCharacter* Hero = Player->GetPawn<AFableCharacter>();
        if (!Profile || !Hero || !Hero->IsAlive() ||
            !DepartureVote.ConsentingMemberKeys.Contains(Profile->PartyPlayerKey) || Profile->IsBackendSavePending() ||
            (Profile->IsBackendEnabled() && !Profile->IsBackendReady()))
        {
            return false;
        }
    }
    TGuardValue<bool> Guard(bTransitionPending, true);
    if (!MoveParticipantsAtomically(Players, EFableRegion::Village, FVector::ZeroVector, false))
    {
        DepartureVote.ConsentingMemberKeys.Reset();
        DepartureVote.Revision = DepartureVote.Revision < MAX_int32 ? DepartureVote.Revision + 1 : 1;
        ForceNetUpdate();
        return false;
    }
    if (AFableGameMode* Mode = AFableGameMode::Find(GetWorld()))
    {
        Mode->CompleteExpeditionResult(this, EFableExpeditionOutcome::Abandoned);
    }
    ClearRecoveryCombat();
    NotifyParty(TEXT("전원이 동의하여 마을로 철수했습니다. 이미 받은 개인 보상은 보존됩니다."));
    FinishExpedition();
    return true;
}

/** 새 증서가 이미 모든 참가자에게 확정된 뒤 이전 원정 자원만 제거한다. */
void AFablePartyEncounter::RetireAfterReplacement(FGuid NewExpeditionId)
{
    if (!HasAuthority() || !NewExpeditionId.IsValid() || NewExpeditionId == Ticket.ExpeditionId ||
        Progress.AdventureStage != EFableAdventureStage::Cleared)
    {
        return;
    }
    const AFableGameMode* Mode = AFableGameMode::Find(GetWorld());
    const AFablePartyEncounter* Replacement = Mode ? Mode->FindExpedition(NewExpeditionId) : nullptr;
    if (!Replacement || Replacement->GetTicket().PartyId != Ticket.PartyId ||
        Replacement->GetTicket().MemberKeys.Num() != Ticket.MemberKeys.Num())
    {
        return;
    }
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    for (const FString& Key : Ticket.MemberKeys)
    {
        const APlayerController* Player = Parties ? Parties->GetConnectedPlayer(Key) : nullptr;
        const AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        if (!Profile || Profile->ActiveExpeditionId != NewExpeditionId)
        {
            return;
        }
    }
    bCommitted = false;
    ClearRecoveryCombat();
    Destroy();
}
