#include "FablePartyManager.h"
#include "FableCharacter.h"
#include "FableAdventure.h"
#include "FablePartyEncounter.h"
#include "FableProjectile.h"
#include "FableEnemyBolt.h"
#include "FableEnemy.h"
#include "FableExpeditionResultTypes.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include "UObject/Class.h"

/** 실시간 대기 없이 서버 요청 간격과 만료 경계만 제어하는 검증 도우미. */
struct FFablePartyTestAccess
{
    /** 전투와 보상 경로도 실제 슬롯에 저장하지 않는 메모리 전용 프로필을 사용한다. */
    static void UseMemoryOnly(AFablePlayerState* Profile)
    {
        Profile->bPersistenceEnabled = false;
        Profile->ProfileSlot.Empty();
        Profile->bStorageBlocked = false;
    }

    /** 장비 마모나 처치 보상이 파일 저장으로 이어지지 않는지 확인한다. */
    static bool IsMemoryOnly(const AFablePlayerState* Profile)
    {
        return Profile && !Profile->bPersistenceEnabled && Profile->ProfileSlot.IsEmpty();
    }

    /** 검증 월드의 정식 상태를 서버 규칙 객체에 연결한다. */
    static void AttachState(AFableGameMode* Mode, AFableGameState* State)
    {
        Mode->GameState = State;
        Mode->GetWorld()->SetGameState(State);
    }

    /** 완료 상태의 거절 검사는 실제 진행 소유 Actor만 제어한다. */
    static void SetEncounterStage(AFablePartyEncounter* Encounter, EFableAdventureStage Stage)
    {
        Encounter->Progress.AdventureStage = Stage;
    }

    /** 실시간 대기 대신 원정의 실제 생존 감시를 호출한다. */
    static void MonitorEncounter(AFablePartyEncounter* Encounter)
    {
        Encounter->CheckPartyState();
    }

    /** 구조 타이머 경계만 앞당기고 실제 구조·귀환 경로를 검증한다. */
    static void CompleteRecovery(AFablePartyEncounter* Encounter)
    {
        Encounter->Progress.PartyRecoveryEndServerTime = 0;
        Encounter->NextRecoveryAttemptTime = 0;
        Encounter->CheckPartyState();
    }

    /** 벽시계 대기만 생략하고 실제 타이머 콜백과 같은 다음 무리 생성 경로를 호출한다. */
    static void AdvanceRootWaveBoundary(AFablePartyEncounter* Encounter)
    {
        Encounter->GetWorldTimerManager().ClearTimer(Encounter->EncounterTimer);
        if (Encounter->Progress.DungeonWave < 2)
        {
            Encounter->SpawnDungeonWave(2);
        }
        else
        {
            Encounter->SpawnBoss();
        }
    }

    /** 서로 다른 사용자 동작 사이의 요청 간격만 초기화한다. */
    static void AllowNextRequest(AFablePartyManager* Manager)
    {
        Manager->LastRequestTimes.Reset();
    }

    /** 서버 타이머와 같은 만료 정리 경로를 실행한다. */
    static void Expire(AFablePartyManager* Manager)
    {
        Manager->ExpireEntries();
    }

    /** 초대의 정확한 만료 시각 경계를 재현한다. */
    static void SetInviteDeadline(AFablePartyManager* Manager, FGuid InviteId, float Deadline)
    {
        for (FFablePartyInvite& Invite : Manager->Invitations)
        {
            if (Invite.InviteId == InviteId)
            {
                Invite.ExpiresAt = Deadline;
            }
        }
    }

    /** 오프라인 예약의 정확한 만료 시각 경계를 재현한다. */
    static void SetReconnectDeadline(AFablePartyManager* Manager, const FString& PlayerKey, float Deadline)
    {
        for (FFablePartyRecord& Party : Manager->Parties)
        {
            for (FFablePartyMember& Member : Party.Members)
            {
                if (Member.PlayerKey == PlayerKey)
                {
                    Member.ReconnectDeadline = Deadline;
                }
            }
        }
    }
};

/** 실제 전투 적용점의 권한을 격리 월드에서 호출하는 검증 접근자. */
struct FFableExpeditionCombatTestAccess
{
    /** 이동 확정 시에만 정리되어야 하는 전투·부활·체크포인트 상태를 보관한다. */
    struct FTravelState
    {
        FVector Checkpoint;
        FRotator CheckpointRotation;
        TArray<float> TimersAndResources;
        uint8 Combo = 0;
        bool bDodging = false;

        /** 부분 입장 실패가 기존 전투 상태를 변경하지 않았는지 비교한다. */
        bool Equals(const FTravelState& Other) const
        {
            return Checkpoint.Equals(Other.Checkpoint) && CheckpointRotation.Equals(Other.CheckpointRotation) &&
                   TimersAndResources == Other.TimersAndResources && Combo == Other.Combo && bDodging == Other.bDodging;
        }
    };

    /** 검증에서 읽는 값은 체크포인트 확정의 실제 부작용에 해당한다. */
    static FTravelState CaptureTravelState(const AFableCharacter* Hero)
    {
        FTravelState Result;
        Result.Checkpoint = Hero->SafeSpawnLocation;
        Result.CheckpointRotation = Hero->SafeSpawnRotation;
        Result.TimersAndResources = {Hero->Health,
                                     Hero->Mana,
                                     Hero->NextAttackServerTime,
                                     Hero->NextSkillServerTime,
                                     Hero->NextSecondSkillServerTime,
                                     Hero->NextThirdSkillServerTime,
                                     Hero->NextDodgeServerTime,
                                     Hero->ShieldAmount,
                                     Hero->ShieldEndServerTime,
                                     Hero->ReviveProtectionEndServerTime,
                                     Hero->DodgeEndServerTime,
                                     Hero->ActionLockEndServerTime,
                                     Hero->LastRequestServerTime,
                                     Hero->LastAttackServerTime,
                                     Hero->NextLocalRequestTime};
        Result.Combo = Hero->ComboIndex;
        Result.bDodging = Hero->bDodging;
        return Result;
    }

    /** 성공 시 초기화되는 서로 다른 값을 넣어 실패 경로의 잘못된 초기화를 드러낸다. */
    static void SeedTravelState(AFableCharacter* Hero)
    {
        Hero->SafeSpawnLocation = FVector(1300, -900, 110);
        Hero->SafeSpawnRotation = FRotator(0, 37, 0);
        Hero->NextAttackServerTime = 23;
        Hero->NextSkillServerTime = 29;
        Hero->NextSecondSkillServerTime = 31;
        Hero->NextThirdSkillServerTime = 37;
        Hero->NextDodgeServerTime = 41;
        Hero->ShieldAmount = 43;
        Hero->ShieldEndServerTime = 47;
        Hero->ReviveProtectionEndServerTime = 53;
        Hero->ComboIndex = 2;
    }

    /** 범위·시야·원정 조건을 포함한 실제 광역 스킬 경로를 호출한다. */
    static void Area(AFableCharacter* Hero, float Damage, bool bSlow = false)
    {
        Hero->ApplySkillArea(Damage, 600, false, bSlow, FFableTrialCastContext());
    }

    /** 같은 원정의 실제 동료 보호막 경로를 호출한다. */
    static void ShareShield(AFableCharacter* Hero)
    {
        Hero->ShareGuardianShield(1);
    }

    /** 피해 모양을 만족한 다른 파티도 적의 실제 광역 판정에서 제외한다. */
    static void EnemyArea(AFableEnemy* Enemy)
    {
        Enemy->AttackPattern = EFableEnemyAttack::Stomp;
        Enemy->ApplyAreaAttack();
    }

    /** 거리와 위치가 같은 적의 실제 추적 선택 결과를 반환한다. */
    static AFableCharacter* FindTarget(AFableEnemy* Enemy)
    {
        Enemy->FindTarget();
        return Enemy->Target.Get();
    }

    /** 실제 화상 시계 경계만 앞당기고 원정 소유권은 그대로 검사한다. */
    static void TickBurn(AFableEnemy* Enemy)
    {
        Enemy->NextBurnServerTime = -0.01f;
        Enemy->TickBurn();
    }

    /** 실제 부활 유지 조건을 변경 없이 확인한다. */
    static bool CanRevive(AFableCharacter* Hero, AFableCharacter* Target)
    {
        return Hero->CanMaintainRevive(Target);
    }

    /** 부활 요청 간격만 초기화하고 실제 생명 상태 감시를 호출한다. */
    static void TickLife(AFableCharacter* Hero)
    {
        Hero->TickLifeState();
    }
};

namespace
{
    /** 검증 전용 연결과 프로필을 함께 보관한다. */
    struct FPartyTestPlayer
    {
        APlayerController* Controller = nullptr;
        AFablePlayerState* Profile = nullptr;
        AFableCharacter* Hero = nullptr;

        /** 이후 검증에서 안전하게 사용할 수 있는 생성 결과인지 확인한다. */
        bool IsValid() const
        {
            return ::IsValid(Controller) && ::IsValid(Profile);
        }
    };

    /** 실제 계정·파일·네트워크를 사용하지 않는 파티 판정용 격리 월드. */
    struct FPartyTestWorld
    {
        UWorld* World = nullptr;
        AFableGameState* State = nullptr;
        AFablePartyManager* Manager = nullptr;
        AFableGameMode* Mode = nullptr;
        TArray<TWeakObjectPtr<APlayerController>> RegisteredControllers;
        bool bOwnsEngineContext = false;

        /** 오디오·저장을 끄고 입장 충돌 검사에 필요한 경우에만 물리 월드를 준비한다. */
        explicit FPartyTestWorld(bool bPhysics = false)
        {
            if (!GEngine)
            {
                return;
            }

            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(bPhysics)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(bPhysics)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            if (World)
            {
                // 접속 종료로 actor를 파괴할 때도 유효한 엔진 월드 문맥을 유지한다.
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                bOwnsEngineContext = true;
                State = World->SpawnActor<AFableGameState>();
                Manager = World->SpawnActor<AFablePartyManager>();
                World->SetGameState(State);
            }
        }

        /** 검증 객체와 등록된 연결을 월드와 함께 해제한다. */
        ~FPartyTestWorld()
        {
            if (World)
            {
                // 초기화되지 않은 격리 월드는 연결 목록도 명시적으로 정리한다.
                for (const TWeakObjectPtr<APlayerController>& Entry : RegisteredControllers)
                {
                    if (APlayerController* Controller = Entry.Get())
                    {
                        World->RemoveController(Controller);
                    }
                }

                World->DestroyWorld(false);

                // actor와 월드 정리가 끝난 뒤 이 fixture가 만든 문맥만 제거한다.
                if (GEngine && bOwnsEngineContext)
                {
                    GEngine->DestroyWorldContext(World);
                }

                World = nullptr;
            }
        }

        /** 인증 완료된 로컬 프로필을 만들고 실제 명부 등록 경로를 호출한다. */
        FPartyTestPlayer AddPlayer(int32 PlayerId, bool bLoaded = true, bool bRegister = true)
        {
            FPartyTestPlayer Player;
            if (!World || !Manager)
            {
                return Player;
            }

            Player.Controller = World->SpawnActor<APlayerController>();
            Player.Profile = World->SpawnActor<AFablePlayerState>();
            if (Player.IsValid())
            {
                Player.Profile->SetPlayerId(PlayerId);
                Player.Profile->SetPlayerName(FString::Printf(TEXT("PartyTest%d"), PlayerId));
                Player.Profile->bProfileLoaded = bLoaded;
                FFablePartyTestAccess::UseMemoryOnly(Player.Profile);
                Player.Profile->SetOwner(Player.Controller);
                Player.Controller->SetPlayerState(Player.Profile);
                State->AddPlayerState(Player.Profile);

                // PostInitializeComponents를 생략한 fixture도 실제 서버 조회 경로에 연결한다.
                World->AddController(Player.Controller);
                RegisteredControllers.Add(Player.Controller);

                if (bRegister)
                {
                    Manager->RegisterPlayer(Player.Controller);
                }
            }

            return Player;
        }

        /** 실제 서버의 입장 메서드를 파일이나 서버 연결 없이 검증한다. */
        bool AddMode()
        {
            Mode = World ? World->SpawnActor<AFableGameMode>() : nullptr;
            if (Mode && State)
            {
                FFablePartyTestAccess::AttachState(Mode, State);
            }

            return Mode && State;
        }

        /** 생존·위치·지역 자격 검사는 실제 캐릭터와 소유 프로필을 사용한다. */
        bool AddHero(FPartyTestPlayer& Player, const FVector& Location)
        {
            if (!World || !Player.IsValid())
            {
                return false;
            }

            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player.Hero = World->SpawnActor<AFableCharacter>(Location, FRotator::ZeroRotator, Parameters);
            if (!Player.Hero)
            {
                return false;
            }

            Player.Controller->Possess(Player.Hero);
            Player.Profile->ForestQuest = EFableQuestStage::Completed;
            Player.Profile->DungeonQuest = EFableQuestStage::Active;
            Player.Profile->MoonwellQuest = EFableQuestStage::Active;
            Player.Profile->DungeonCompletions = 1;
            return Player.Hero->IsAlive() && Player.Hero->GetPlayerState() == Player.Profile;
        }

        /** 준비 요청마다 최신 서버 선택을 사용한다. */
        bool Ready(const FPartyTestPlayer& Player, bool bReady = true)
        {
            const FFablePartyRecord* Party = Manager->GetPartyForMember(Player.Profile->PartyPlayerKey);
            return Party &&
                   Manager->SetExpeditionReady(Player.Controller, Party->PartyId, Party->PreparationRevision, bReady);
        }

        /** 캐시된 준비 리비전 대신 현재 서버 명부를 읽어 입장을 요청한다. */
        bool Enter(const FPartyTestPlayer& Player)
        {
            const FFablePartyRecord* Party = Manager->GetPartyForMember(Player.Profile->PartyPlayerKey);
            return Party && Mode &&
                   Mode->RequestStartExpedition(Player.Controller, Party->PartyId, Party->PreparationRevision);
        }

        /** 전체 필드에 생성된 실제 적의 수를 확인한다. */
        int32 EnemyCount() const
        {
            int32 Count = 0;
            for (TActorIterator<AFableEnemy> It(World); It; ++It)
            {
                Count += It->IsAlive() ? 1 : 0;
            }

            return Count;
        }

        /** 실제 서버 조회에 사용하는 연결 목록에 컨트롤러가 한 번만 등록됐는지 확인한다. */
        bool IsControllerRegistered(const APlayerController* Player) const
        {
            int32 Matches = 0;
            if (World && Player)
            {
                for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
                {
                    if (It->Get() == Player)
                    {
                        ++Matches;
                    }
                }
            }

            return Matches == 1;
        }

        /** 이전 사용자 동작과 분리된 요청을 서버에 전달한다. */
        bool Act(const FPartyTestPlayer& Player, EFablePartyAction Action, int32 TargetId = INDEX_NONE,
                 FGuid InviteId = FGuid())
        {
            FFablePartyTestAccess::AllowNextRequest(Manager);
            return Manager->ExecuteAction(Player.Controller, Action, TargetId, InviteId);
        }

        /** 초대를 받은 사용자가 일회성 초대장으로 파티에 참여한다. */
        bool Join(const FPartyTestPlayer& Leader, const FPartyTestPlayer& Member)
        {
            if (!Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()))
            {
                return false;
            }

            const FFablePartyInvite* Invite = Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
            return Invite && Act(Member, EFablePartyAction::Accept, INDEX_NONE, Invite->InviteId);
        }

        /** 실제 종료 콜백과 객체 해제를 순서대로 재현한다. */
        void Disconnect(FPartyTestPlayer& Player)
        {
            Manager->DisconnectPlayer(Player.Controller);
            World->RemoveController(Player.Controller);
            RegisteredControllers.Remove(Player.Controller);
            Player.Controller->SetPlayerState(nullptr);
            State->RemovePlayerState(Player.Profile);
            Player.Profile->Destroy();
            Player.Controller->Destroy();
            Player = FPartyTestPlayer();
        }

        /** 타이머를 진행하지 않고 같은 메모리 프로필의 실제 연결 객체만 교체한다. */
        bool ReplaceConnection(FPartyTestPlayer& Player)
        {
            APlayerController* Previous = Player.Controller;
            Manager->DisconnectPlayer(Previous);
            Previous->UnPossess();
            Previous->SetPlayerState(nullptr);
            World->RemoveController(Previous);
            RegisteredControllers.Remove(Previous);

            Player.Controller = World->SpawnActor<APlayerController>();
            if (!Player.Controller)
            {
                return false;
            }
            Player.Profile->SetOwner(Player.Controller);
            Player.Controller->SetPlayerState(Player.Profile);
            World->AddController(Player.Controller);
            RegisteredControllers.Add(Player.Controller);
            Player.Controller->Possess(Player.Hero);
            Manager->RegisterPlayer(Player.Controller);
            Previous->Destroy();
            return Manager->GetConnectedPlayer(Player.Profile->PartyPlayerKey) == Player.Controller;
        }
    };

    /** 두 파티의 진행 정본 전체가 그대로인지 비교한다. */
    bool SameProgress(const FFableAdventureProgress& A, const FFableAdventureProgress& B)
    {
        return A.DefeatedEnemies == B.DefeatedEnemies && A.RequiredEnemies == B.RequiredEnemies && A.Round == B.Round &&
               A.bObjectiveComplete == B.bObjectiveComplete && A.AdventureStage == B.AdventureStage &&
               A.DungeonWave == B.DungeonWave && A.ActiveRegion == B.ActiveRegion &&
               A.ActiveDifficulty == B.ActiveDifficulty && A.MoonSealMask == B.MoonSealMask &&
               A.ActiveSealIndex == B.ActiveSealIndex && A.BossHealth == B.BossHealth &&
               A.BossMaxHealth == B.BossMaxHealth && A.bPartyRecoveryPending == B.bPartyRecoveryPending &&
               A.PartyRecoveryEndServerTime == B.PartyRecoveryEndServerTime && A.bRetryAvailable == B.bRetryAvailable &&
               A.RetryRegion == B.RetryRegion && A.RetryDifficulty == B.RetryDifficulty &&
               A.PartyRecoveryCount == B.PartyRecoveryCount && A.EncounterSerial == B.EncounterSerial;
    }

    /** 실제 네 연결을 두 정식 파티와 독립 원정에 배치하는 메모리 전용 fixture. */
    struct FIsolationTestWorld
    {
        FPartyTestWorld Base{true};
        FPartyTestPlayer First, FirstPeer, Second, SecondPeer;
        AFablePartyEncounter* One = nullptr;
        AFablePartyEncounter* Two = nullptr;

        /** 일반 모드 입장은 서로 다른 원점, 효과 검증은 의도적으로 같은 원점을 사용한다. */
        bool Initialize(bool bOverlap = false, EFableRegion Region = EFableRegion::RootRuins)
        {
            First = Base.AddPlayer(701);
            FirstPeer = Base.AddPlayer(702);
            Second = Base.AddPlayer(703);
            SecondPeer = Base.AddPlayer(704);
            const FVector Portal = FableRPG::PortalLocation();
            if (!Base.AddMode() || !Base.AddHero(First, Portal + FVector(-150, -100, 110)) ||
                !Base.AddHero(FirstPeer, Portal + FVector(150, -100, 110)) ||
                !Base.AddHero(Second, Portal + FVector(-150, 200, 110)) ||
                !Base.AddHero(SecondPeer, Portal + FVector(150, 200, 110)) ||
                !Base.Act(First, EFablePartyAction::Create) || !Base.Join(First, FirstPeer) ||
                !Base.Act(Second, EFablePartyAction::Create) || !Base.Join(Second, SecondPeer))
            {
                return false;
            }

            // 요청은 실제 명부 리비전과 각자의 준비를 사용한다.
            for (const FPartyTestPlayer* Leader : {&First, &Second})
            {
                const FFablePartyRecord* Party = Base.Manager->GetPartyForMember(Leader->Profile->PartyPlayerKey);
                if (!Party ||
                    !Base.Manager->SelectExpedition(Leader->Controller, Party->PartyId, Party->PreparationRevision,
                                                    Region, EFableDifficulty::Normal))
                {
                    return false;
                }
            }
            if (!Base.Ready(First) || !Base.Ready(FirstPeer) || !Base.Ready(Second) || !Base.Ready(SecondPeer))
            {
                return false;
            }

            if (!bOverlap)
            {
                if (!Base.Enter(First) || !Base.Enter(Second))
                {
                    return false;
                }
                One = Base.Mode->GetExpeditionFor(First.Profile);
                Two = Base.Mode->GetExpeditionFor(Second.Profile);
                return One && Two;
            }

            // 신뢰한 fixture만 지형을 겹쳐 거리·벽 대신 UUID 거부를 검증한다.
            One = AdmitAtOrigin(First, FirstPeer, FVector(0, 30000, 0));
            Two = AdmitAtOrigin(Second, SecondPeer, FVector(0, 30000, 0));
            return One && Two;
        }

        /** 실제 준비 snapshot과 실제 입장을 사용하고 임의 소속을 주입하지 않는다. */
        AFablePartyEncounter* AdmitAtOrigin(const FPartyTestPlayer& Leader, const FPartyTestPlayer& Peer,
                                            FVector Origin)
        {
            const FFablePartyRecord* Party = Base.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
            FFableExpeditionTicket Ticket;
            if (!Party || !Base.Manager->BuildReadySnapshot(Leader.Controller, Party->PartyId,
                                                            Party->PreparationRevision, Ticket))
            {
                return nullptr;
            }

            AFablePartyEncounter* Encounter = Base.World->SpawnActor<AFablePartyEncounter>();
            if (!Encounter || !Encounter->InitializeExpedition(Ticket, Origin) ||
                !Encounter->Admit({Leader.Controller, Peer.Controller}))
            {
                return nullptr;
            }
            return Encounter;
        }

        /** 자기 원정에 실제 생성된 적 하나를 선택한다. */
        AFableEnemy* EnemyFor(const AFablePartyEncounter* Encounter) const
        {
            for (TActorIterator<AFableEnemy> It(Base.World); It; ++It)
            {
                if (It->IsAlive() && It->GetExpeditionId() == Encounter->GetExpeditionId())
                {
                    return *It;
                }
            }
            return nullptr;
        }

        /** 원정별 적 UUID와 체력 전체를 보존한다. */
        TMap<FGuid, float> EnemyHealth(const AFablePartyEncounter* Encounter) const
        {
            TMap<FGuid, float> Result;
            for (TActorIterator<AFableEnemy> It(Base.World); It; ++It)
            {
                if (It->IsAlive() && It->GetExpeditionId() == Encounter->GetExpeditionId())
                {
                    Result.Add(It->GetSpawnId(), It->GetHealth());
                }
            }
            return Result;
        }

        /** 적 개체 교체나 체력 변경도 진행 격리 실패로 판정한다. */
        bool HasEnemyHealth(const AFablePartyEncounter* Encounter, const TMap<FGuid, float>& Expected) const
        {
            const TMap<FGuid, float> Actual = EnemyHealth(Encounter);
            if (Actual.Num() != Expected.Num())
            {
                return false;
            }
            for (const auto& Entry : Expected)
            {
                const float* Value = Actual.Find(Entry.Key);
                if (!Value || *Value != Entry.Value)
                {
                    return false;
                }
            }
            return true;
        }

        /** 같은 원점의 범위 검증을 위해 새 서버 적을 명확한 위치에 생성한다. */
        AFableEnemy* SpawnEnemy(AFablePartyEncounter* Encounter, FVector Offset)
        {
            FActorSpawnParameters Parameters;
            Parameters.Owner = Encounter;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            const EFableRegion Region = Encounter->GetTicket().Region;
            AFableEnemy* Enemy = Base.World->SpawnActor<AFableEnemy>(FableRPG::RegionCenter(Region) +
                                                                         Encounter->GetWorldOrigin() + Offset,
                                                                     FRotator::ZeroRotator, Parameters);
            if (Enemy)
            {
                Enemy->BindExpedition(Encounter->GetExpeditionId());
                if (Region == EFableRegion::Moonwell)
                {
                    Enemy->ConfigureMoonwell(false, true);
                }
                else
                {
                    Enemy->ConfigureEncounter(false, true);
                }
            }
            return Enemy;
        }

        /** 기존 무리는 양쪽 사정거리 밖에 두고 특정 판정 대상만 사용한다. */
        void MoveExistingEnemiesAway()
        {
            for (TActorIterator<AFableEnemy> It(Base.World); It; ++It)
            {
                const auto Adventure = FableAdventure::For(*It);
                if (Adventure)
                {
                    It->SetActorLocation(
                        Adventure.ToWorld(Adventure->ActiveRegion,
                                          FableRPG::RegionCenter(Adventure->ActiveRegion) + FVector(-1600, 1300, 110)));
                }
            }
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyLifecycleTest, "Fablewood.Native.Party.ConsentAndLeaderAuthority",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 초대 수신 동의와 파티장 권한이 모든 실제 명부 변경의 조건인지 확인한다. */
bool FFablePartyLifecycleTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer Leader = Fixture.AddPlayer(101);
    FPartyTestPlayer Member = Fixture.AddPlayer(102);
    FPartyTestPlayer Stranger = Fixture.AddPlayer(103);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("All fixture connections exist"), Leader.IsValid() && Member.IsValid() && Stranger.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Lifecycle fixture controllers are visible to authoritative roster lookups"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Member.Controller) &&
                      Fixture.IsControllerRegistered(Stranger.Controller)))
    {
        return false;
    }

    TestEqual(TEXT("Fixture world has exactly three registered connections"), Fixture.World->GetNumPlayerControllers(),
              3);
    TestTrue(TEXT("A registered player creates a party"), Fixture.Act(Leader, EFablePartyAction::Create));
    const FGuid PartyId = Leader.Profile->PartyId;
    TestTrue(TEXT("Create synchronizes a valid server party ID"), PartyId.IsValid());
    TestFalse(TEXT("An existing member cannot create another party"), Fixture.Act(Leader, EFablePartyAction::Create));
    TestFalse(TEXT("A player without leader authority cannot invite"),
              Fixture.Act(Stranger, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    TestFalse(TEXT("A leader cannot invite itself"),
              Fixture.Act(Leader, EFablePartyAction::Invite, Leader.Profile->GetPlayerId()));
    TestFalse(TEXT("A nonexistent target is rejected"), Fixture.Act(Leader, EFablePartyAction::Invite, MAX_int32));

    TestTrue(TEXT("Leader sends an invitation"),
             Fixture.Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    const FFablePartyInvite* Pending = Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("The recipient observes its pending invitation"), Pending))
    {
        return false;
    }

    const FGuid DeclinedId = Pending->InviteId;
    TestFalse(TEXT("Knowing someone else's invitation UUID grants no membership"),
              Fixture.Act(Stranger, EFablePartyAction::Accept, INDEX_NONE, DeclinedId));
    TestFalse(TEXT("Another player cannot decline the recipient's invitation"),
              Fixture.Act(Stranger, EFablePartyAction::Decline, INDEX_NONE, DeclinedId));
    TestTrue(TEXT("The actual recipient can decline"),
             Fixture.Act(Member, EFablePartyAction::Decline, INDEX_NONE, DeclinedId));
    TestFalse(TEXT("A declined invitation cannot later be accepted"),
              Fixture.Act(Member, EFablePartyAction::Accept, INDEX_NONE, DeclinedId));
    TestFalse(TEXT("Declining never adds a party member"), Member.Profile->PartyId.IsValid());

    TestTrue(TEXT("A new invitation can be accepted"), Fixture.Join(Leader, Member));
    TestTrue(TEXT("Accepted membership synchronizes both profiles"), Member.Profile->PartyId == PartyId);
    TestTrue(TEXT("Explicit members are recognized as companions"),
             Fixture.Manager->ArePartyMembers(Leader.Profile, Member.Profile));
    TestFalse(TEXT("An unrelated connected player is not a companion"),
              Fixture.Manager->ArePartyMembers(Leader.Profile, Stranger.Profile));
    TestFalse(TEXT("An ordinary member cannot transfer leadership"),
              Fixture.Act(Member, EFablePartyAction::TransferLeader, Leader.Profile->GetPlayerId()));
    TestFalse(TEXT("Leadership cannot be transferred outside the party"),
              Fixture.Act(Leader, EFablePartyAction::TransferLeader, Stranger.Profile->GetPlayerId()));
    TestTrue(TEXT("The leader can transfer to its connected companion"),
             Fixture.Act(Leader, EFablePartyAction::TransferLeader, Member.Profile->GetPlayerId()));

    const FFablePartyRecord* Party = Fixture.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
    TestTrue(TEXT("The recipient becomes the sole party leader"),
             Party && Party->LeaderKey == Member.Profile->PartyPlayerKey);
    TestTrue(TEXT("A former leader may explicitly leave"), Fixture.Act(Leader, EFablePartyAction::Leave));
    TestFalse(TEXT("Explicit leave clears the departing profile"), Leader.Profile->PartyId.IsValid());
    TestTrue(TEXT("The remaining member retains the original party"), Member.Profile->PartyId == PartyId);
    TestTrue(TEXT("The final member can leave"), Fixture.Act(Member, EFablePartyAction::Leave));
    TestEqual(TEXT("An empty party is removed"), Fixture.Manager->Parties.Num(), 0);
    TestFalse(TEXT("A player cannot leave twice"), Fixture.Act(Member, EFablePartyAction::Leave));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyInviteRaceTest, "Fablewood.Native.Party.InvitationRacesAndCapacity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 중복·경합·재사용 요청으로 정원이나 한 사람의 소속이 늘어나지 않는지 확인한다. */
bool FFablePartyInviteRaceTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer First = Fixture.AddPlayer(201);
    FPartyTestPlayer Second = Fixture.AddPlayer(202);
    FPartyTestPlayer Target = Fixture.AddPlayer(203);
    FPartyTestPlayer Extra = Fixture.AddPlayer(204);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("Four isolated fixture profiles exist"),
                  First.IsValid() && Second.IsValid() && Target.IsValid() && Extra.IsValid()))
    {
        return false;
    }

    if (!TestTrue(
            TEXT("Race fixture controllers are visible to authoritative invitation lookups"),
            Fixture.IsControllerRegistered(First.Controller) && Fixture.IsControllerRegistered(Second.Controller) &&
                Fixture.IsControllerRegistered(Target.Controller) && Fixture.IsControllerRegistered(Extra.Controller)))
    {
        return false;
    }

    TestTrue(TEXT("First leader creates its party"), Fixture.Act(First, EFablePartyAction::Create));
    TestTrue(TEXT("Second leader creates another party"), Fixture.Act(Second, EFablePartyAction::Create));
    TestTrue(TEXT("First invitation reserves one consent request"),
             Fixture.Act(First, EFablePartyAction::Invite, Target.Profile->GetPlayerId()));
    const FFablePartyInvite* Pending = Fixture.Manager->GetPendingInviteFor(Target.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("First invitation exists"), Pending))
    {
        return false;
    }

    const FGuid AcceptedId = Pending->InviteId;
    TestFalse(TEXT("Duplicate invitation cannot stack"),
              Fixture.Act(First, EFablePartyAction::Invite, Target.Profile->GetPlayerId()));
    TestFalse(TEXT("A second party cannot overwrite an outstanding invitation"),
              Fixture.Act(Second, EFablePartyAction::Invite, Target.Profile->GetPlayerId()));
    TestEqual(TEXT("One recipient has one outstanding invitation"), Fixture.Manager->Invitations.Num(), 1);
    TestTrue(TEXT("The second open slot may have another pending invitation"),
             Fixture.Act(First, EFablePartyAction::Invite, Extra.Profile->GetPlayerId()));
    const FFablePartyInvite* ExtraInvite = Fixture.Manager->GetPendingInviteFor(Extra.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("Competing slot invitation exists"), ExtraInvite))
    {
        return false;
    }

    const FGuid LostRaceId = ExtraInvite->InviteId;
    TestTrue(TEXT("The first recipient claims the final slot once"),
             Fixture.Act(Target, EFablePartyAction::Accept, INDEX_NONE, AcceptedId));
    TestFalse(TEXT("An accepted invitation cannot be replayed"),
              Fixture.Act(Target, EFablePartyAction::Accept, INDEX_NONE, AcceptedId));
    TestFalse(TEXT("A competing acceptance cannot exceed party capacity"),
              Fixture.Act(Extra, EFablePartyAction::Accept, INDEX_NONE, LostRaceId));
    TestFalse(TEXT("A full party cannot issue another invitation"),
              Fixture.Act(First, EFablePartyAction::Invite, Extra.Profile->GetPlayerId()));
    const FFablePartyRecord* Full = Fixture.Manager->GetPartyForMember(First.Profile->PartyPlayerKey);
    TestEqual(TEXT("The final roster has exactly two members"), Full ? Full->Members.Num() : 0, 2);
    TestFalse(TEXT("The loser remains outside the full party"), Extra.Profile->PartyId.IsValid());

    TestTrue(TEXT("Another party invites the unassigned player"),
             Fixture.Act(Second, EFablePartyAction::Invite, Extra.Profile->GetPlayerId()));
    const FFablePartyInvite* Obsolete = Fixture.Manager->GetPendingInviteFor(Extra.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("Invitation before party dissolution exists"), Obsolete))
    {
        return false;
    }

    const FGuid ObsoleteId = Obsolete->InviteId;
    const FGuid OldPartyId = Second.Profile->PartyId;
    TestTrue(TEXT("A solo leader may dissolve the party"), Fixture.Act(Second, EFablePartyAction::Leave));
    TestTrue(TEXT("The same player creates a distinct new party"), Fixture.Act(Second, EFablePartyAction::Create));
    TestTrue(TEXT("Recreated party has a new server identity"), Second.Profile->PartyId != OldPartyId);
    TestFalse(TEXT("An invitation to the dissolved party cannot join its replacement"),
              Fixture.Act(Extra, EFablePartyAction::Accept, INDEX_NONE, ObsoleteId));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyReconnectTest,
                                 "Fablewood.Native.Party.DisconnectReservationAndStableRejoin",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 파티장 종료와 동일 캐릭터 복귀가 다른 사람의 자리나 권한을 바꾸지 않는지 확인한다. */
bool FFablePartyReconnectTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer Leader = Fixture.AddPlayer(301);
    FPartyTestPlayer Member = Fixture.AddPlayer(302);
    FPartyTestPlayer Stranger = Fixture.AddPlayer(303);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("Reconnect fixture profiles exist"), Leader.IsValid() && Member.IsValid() && Stranger.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Reconnect fixture controllers are visible to authoritative roster lookups"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Member.Controller) &&
                      Fixture.IsControllerRegistered(Stranger.Controller)))
    {
        return false;
    }

    if (!TestTrue(TEXT("A two-member party is formed"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }

    const FString OriginalKey = Leader.Profile->PartyPlayerKey;
    const FString MemberKey = Member.Profile->PartyPlayerKey;
    const FGuid OriginalParty = Leader.Profile->PartyId;
    FPartyTestPlayer PreviousConnection = Leader;
    Fixture.Manager->DisconnectPlayer(PreviousConnection.Controller);
    const FFablePartyRecord* Reserved = Fixture.Manager->GetPartyForMember(OriginalKey);
    TestTrue(TEXT("The disconnected member remains reserved during the grace period"), Reserved != nullptr);
    TestTrue(TEXT("A connected companion immediately receives leadership"),
             Reserved && Reserved->LeaderKey == MemberKey);
    TestFalse(TEXT("An offline reservation still occupies the final slot"),
              Fixture.Act(Member, EFablePartyAction::Invite, Stranger.Profile->GetPlayerId()));

    Leader = Fixture.AddPlayer(301);
    if (!TestTrue(TEXT("A new connection for the same local identity exists"), Leader.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("The restored connection is registered in the authoritative world list"),
             Fixture.IsControllerRegistered(Leader.Controller));
    TestTrue(TEXT("The server restores the original public stable key"), Leader.Profile->PartyPlayerKey == OriginalKey);
    TestTrue(TEXT("Rejoining restores the same party rather than creating another"),
             Leader.Profile->PartyId == OriginalParty);
    const FFablePartyRecord* Restored = Fixture.Manager->GetPartyForMember(OriginalKey);
    TestTrue(TEXT("Rejoining does not steal leadership back"), Restored && Restored->LeaderKey == MemberKey);
    TestEqual(TEXT("Rejoining never duplicates a reserved member"), Restored ? Restored->Members.Num() : 0, 2);

    // 이전 연결의 늦은 종료 알림이 이미 복귀한 새 연결을 오프라인으로 만들면 안 된다.
    Fixture.Manager->DisconnectPlayer(PreviousConnection.Controller);
    Restored = Fixture.Manager->GetPartyForMember(OriginalKey);
    const FFablePartyMember* RejoinedMember = Restored ? Restored->Members.FindByPredicate(
                                                             [&](const FFablePartyMember& Entry)
                                                             {
                                                                 return Entry.PlayerKey == OriginalKey;
                                                             })
                                                       : nullptr;
    TestTrue(TEXT("A delayed logout from the previous connection cannot evict the rejoined player"),
             RejoinedMember && RejoinedMember->bConnected && RejoinedMember->ReconnectDeadline == 0);
    APlayerController* PreviousController = PreviousConnection.Controller;
    Fixture.Disconnect(PreviousConnection);
    TestFalse(TEXT("A disconnected old controller is removed from the authoritative world list"),
              Fixture.IsControllerRegistered(PreviousController));

    // 두 사람이 모두 끊긴 뒤 두 번째 구성원이 먼저 돌아와도 온라인 리더를 확보한다.
    Fixture.Disconnect(Leader);
    Fixture.Disconnect(Member);
    Member = Fixture.AddPlayer(302);
    if (!TestTrue(TEXT("The second member can reconnect before the original leader"), Member.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("The first returning member retains its stable key"), Member.Profile->PartyPlayerKey == MemberKey);
    TestTrue(TEXT("The first returning member retains the original party"), Member.Profile->PartyId == OriginalParty);
    const FFablePartyRecord* FirstReturned = Fixture.Manager->GetPartyForMember(MemberKey);
    TestTrue(TEXT("The only connected member becomes leader after both connections were lost"),
             FirstReturned && FirstReturned->LeaderKey == MemberKey);

    Leader = Fixture.AddPlayer(301);
    if (!TestTrue(TEXT("The original leader reconnects after its companion"), Leader.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("The original leader still restores the same stable key"),
             Leader.Profile->PartyPlayerKey == OriginalKey);
    TestTrue(TEXT("The original leader still restores the same party"), Leader.Profile->PartyId == OriginalParty);
    Restored = Fixture.Manager->GetPartyForMember(OriginalKey);
    TestTrue(TEXT("The already-connected leader keeps authority when the original leader returns"),
             Restored && Restored->LeaderKey == MemberKey);
    TestEqual(TEXT("Both reconnects retain exactly the original two roster entries"),
              Restored ? Restored->Members.Num() : 0, 2);

    Fixture.Disconnect(Leader);
    FFablePartyTestAccess::SetReconnectDeadline(Fixture.Manager, OriginalKey, Fixture.World->GetTimeSeconds());
    FFablePartyTestAccess::Expire(Fixture.Manager);
    TestTrue(TEXT("A reservation expires at its deadline"), Fixture.Manager->GetPartyForMember(OriginalKey) == nullptr);
    TestTrue(TEXT("An expired reservation releases the slot"), Fixture.Join(Member, Stranger));
    Leader = Fixture.AddPlayer(301);
    if (!TestTrue(TEXT("A connection after reservation expiry exists"), Leader.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("Stable key survives reservation expiry"), Leader.Profile->PartyPlayerKey == OriginalKey);
    TestFalse(TEXT("An expired member does not rejoin an already-filled party"), Leader.Profile->PartyId.IsValid());

    const FString FinalMemberKey = Member.Profile->PartyPlayerKey;
    const FString StrangerKey = Stranger.Profile->PartyPlayerKey;
    Fixture.Disconnect(Member);
    Fixture.Disconnect(Stranger);
    FFablePartyTestAccess::SetReconnectDeadline(Fixture.Manager, FinalMemberKey, Fixture.World->GetTimeSeconds());
    FFablePartyTestAccess::SetReconnectDeadline(Fixture.Manager, StrangerKey, Fixture.World->GetTimeSeconds());
    FFablePartyTestAccess::Expire(Fixture.Manager);
    TestEqual(TEXT("A fully expired disconnected party is removed"), Fixture.Manager->Parties.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyGuardTest, "Fablewood.Native.Party.AuthenticationAuthorityAndCombatGuards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 인증·소유권·서버 권한·전투 상태를 우회한 명부 수정 요청을 거절한다. */
bool FFablePartyGuardTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestWorld OtherWorld;
    FPartyTestPlayer Leader = Fixture.AddPlayer(401);
    FPartyTestPlayer Unloaded = Fixture.AddPlayer(402, false);
    FPartyTestPlayer Unregistered = Fixture.AddPlayer(403, true, false);
    FPartyTestPlayer Stranger = Fixture.AddPlayer(404);
    FPartyTestPlayer Foreign = OtherWorld.AddPlayer(405);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestNotNull(TEXT("Combat fixture game state"), Fixture.State) ||
        !TestTrue(TEXT("Guard fixture profiles exist"), Leader.IsValid() && Unloaded.IsValid() &&
                                                            Unregistered.IsValid() && Stranger.IsValid() &&
                                                            Foreign.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Guard fixtures register their controllers in the appropriate world"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Unloaded.Controller) &&
                      Fixture.IsControllerRegistered(Unregistered.Controller) &&
                      Fixture.IsControllerRegistered(Stranger.Controller) &&
                      OtherWorld.IsControllerRegistered(Foreign.Controller)))
    {
        return false;
    }

    TestFalse(TEXT("A missing requester is rejected"),
              Fixture.Manager->ExecuteAction(nullptr, EFablePartyAction::Create));
    TestTrue(TEXT("An unloaded profile receives no stable party key"), Unloaded.Profile->PartyPlayerKey.IsEmpty());
    TestFalse(TEXT("An unloaded profile cannot create a party"), Fixture.Act(Unloaded, EFablePartyAction::Create));
    TestFalse(TEXT("A loaded but unregistered profile cannot create a party"),
              Fixture.Act(Unregistered, EFablePartyAction::Create));
    TestFalse(TEXT("A foreign-world controller is rejected"), Fixture.Act(Foreign, EFablePartyAction::Create));

    Fixture.Manager->SetRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("A client-side manager cannot create authoritative membership"),
              Fixture.Act(Leader, EFablePartyAction::Create));
    Fixture.Manager->SetRole(ROLE_Authority);
    TestEqual(TEXT("Rejected client request creates no party"), Fixture.Manager->Parties.Num(), 0);

    Stranger.Controller->SetPlayerState(Leader.Profile);
    TestFalse(TEXT("A controller cannot act through another controller's profile"),
              Fixture.Act(Stranger, EFablePartyAction::Create));
    Stranger.Controller->SetPlayerState(Stranger.Profile);
    TestTrue(TEXT("The legitimate owner still creates the party"), Fixture.Act(Leader, EFablePartyAction::Create));
    TestFalse(TEXT("An unknown action cannot change membership"),
              Fixture.Act(Leader, static_cast<EFablePartyAction>(255)));

    Stranger.Profile->PartyId = Leader.Profile->PartyId;
    TestFalse(TEXT("A forged profile PartyId is insufficient for companion authority"),
              Fixture.Manager->ArePartyMembers(Leader.Profile, Stranger.Profile));
    Stranger.Profile->PartyId.Invalidate();

    const FGuid OriginalParty = Leader.Profile->PartyId;
    Fixture.State->FieldProgress.ActiveRegion = EFableRegion::RootRuins;
    TestFalse(TEXT("Dungeon combat prevents explicit party leave"), Fixture.Act(Leader, EFablePartyAction::Leave));
    Fixture.State->FieldProgress.ActiveRegion = EFableRegion::Village;
    Fixture.State->FieldProgress.bPartyRecoveryPending = true;
    TestFalse(TEXT("Recovery countdown prevents explicit party leave"), Fixture.Act(Leader, EFablePartyAction::Leave));
    Fixture.State->FieldProgress.bPartyRecoveryPending = false;
    Fixture.State->FieldProgress.AdventureStage = EFableAdventureStage::Forest;
    TestFalse(TEXT("A village forest encounter also prevents party editing"),
              Fixture.Act(Leader, EFablePartyAction::Leave));
    TestTrue(TEXT("All rejected edits preserve membership"), Leader.Profile->PartyId == OriginalParty);
    Fixture.State->FieldProgress.AdventureStage = EFableAdventureStage::Village;
    TestTrue(TEXT("Returning to idle town allows explicit leave"), Fixture.Act(Leader, EFablePartyAction::Leave));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyDeadlineTest, "Fablewood.Native.Party.ServerDeadlinesAndPacketReplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 서버 시각의 초대 만료와 빠른 중복 패킷이 일회성 동의를 유지하는지 확인한다. */
bool FFablePartyDeadlineTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer Leader = Fixture.AddPlayer(501);
    FPartyTestPlayer Member = Fixture.AddPlayer(502);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("Deadline fixture profiles exist"), Leader.IsValid() && Member.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Deadline fixture controllers are visible to authoritative invitation lookups"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Member.Controller)))
    {
        return false;
    }

    TestTrue(TEXT("Initial packet creates exactly one party"),
             Fixture.Manager->ExecuteAction(Leader.Controller, EFablePartyAction::Create));
    TestFalse(TEXT("A rapid follow-up leave packet cannot undo the first action"),
              Fixture.Manager->ExecuteAction(Leader.Controller, EFablePartyAction::Leave));
    TestEqual(TEXT("Rate-limited packets preserve the party"), Fixture.Manager->Parties.Num(), 1);
    TestTrue(TEXT("A later independent action may invite"),
             Fixture.Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    const FFablePartyInvite* Pending = Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("An unexpired invitation is visible"), Pending))
    {
        return false;
    }

    const FGuid ExpiredId = Pending->InviteId;
    FFablePartyTestAccess::SetInviteDeadline(Fixture.Manager, ExpiredId, Fixture.World->GetTimeSeconds());
    TestTrue(TEXT("An invitation is hidden exactly at the server deadline"),
             Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey) == nullptr);
    Fixture.Manager->SetRole(ROLE_SimulatedProxy);
    FFablePartyTestAccess::Expire(Fixture.Manager);
    TestEqual(TEXT("A client timer cannot delete authoritative invitation state"), Fixture.Manager->Invitations.Num(),
              1);
    Fixture.Manager->SetRole(ROLE_Authority);
    TestFalse(TEXT("An expired acceptance is rejected even before the periodic timer runs"),
              Fixture.Act(Member, EFablePartyAction::Accept, INDEX_NONE, ExpiredId));
    TestEqual(TEXT("The server removes expired invitations"), Fixture.Manager->Invitations.Num(), 0);
    TestFalse(TEXT("Expired consent does not modify the target profile"), Member.Profile->PartyId.IsValid());

    TestTrue(TEXT("A fresh invitation remains available after expiry"), Fixture.Join(Leader, Member));
    TestTrue(TEXT("A renewed consent creates the expected companion relationship"),
             Fixture.Manager->ArePartyMembers(Leader.Profile, Member.Profile));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionConsentTest, "Fablewood.Native.Expedition.SelectionAndExplicitConsent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 같은 선택에 대한 각자의 동의와 오래된 요청의 거절을 검증한다. */
bool FFableExpeditionConsentTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(601);
    FPartyTestPlayer Member = Fixture.AddPlayer(602);
    if (!TestTrue(TEXT("Preparation fixture has live owned characters"),
                  Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110))) ||
        !TestTrue(TEXT("Preparation fixture forms a real two-member party"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }

    const FFablePartyRecord* Party = Fixture.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
    const FGuid PartyId = Party->PartyId;
    const int32 InitialRevision = Party->PreparationRevision;
    FFableExpeditionTicket Ticket;
    TestFalse(TEXT("A party without consent cannot produce an admission ticket"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestTrue(TEXT("The leader explicitly becomes ready"), Fixture.Ready(Leader));
    TestEqual(TEXT("A readiness update does not invalidate simultaneous peer consent"), Party->PreparationRevision,
              InitialRevision);
    TestFalse(TEXT("One ready member cannot stand in for the other"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestTrue(TEXT("The peer independently becomes ready for the same revision"), Fixture.Ready(Member));
    TestTrue(TEXT("The same readiness request is idempotent"), Fixture.Ready(Member));
    TestEqual(TEXT("Repeated readiness leaves the selection revision unchanged"), Party->PreparationRevision,
              InitialRevision);
    TestTrue(TEXT("The exact agreed roster produces a server ticket"),
             Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestEqual(TEXT("A ticket fixes exactly two stable member keys"), Ticket.MemberKeys.Num(), 2);
    TestTrue(TEXT("The ticket contains the authenticated leader and peer"),
             Ticket.Contains(Leader.Profile->PartyPlayerKey) && Ticket.Contains(Member.Profile->PartyPlayerKey));

    // 선택을 바꾸지 않은 재전송은 준비를 지우지 않으며 비리더의 변경은 거절한다.
    TestTrue(TEXT("Re-selecting the same destination preserves consent"),
             Fixture.Manager->SelectExpedition(Leader.Controller, PartyId, InitialRevision, EFableRegion::RootRuins,
                                               EFableDifficulty::Normal));
    TestEqual(TEXT("An identical selection retains its revision"), Party->PreparationRevision, InitialRevision);
    TestFalse(TEXT("A member cannot change the leader's choice"),
              Fixture.Manager->SelectExpedition(Member.Controller, PartyId, InitialRevision, EFableRegion::Moonwell,
                                                EFableDifficulty::Hard));
    TestTrue(TEXT("A real selection change succeeds for the leader"),
             Fixture.Manager->SelectExpedition(Leader.Controller, PartyId, InitialRevision, EFableRegion::RootRuins,
                                               EFableDifficulty::Hard));
    const int32 ChangedRevision = Party->PreparationRevision;
    TestTrue(TEXT("A new selection has a new revision"), ChangedRevision != InitialRevision);
    TestTrue(TEXT("A changed selection clears every old consent"),
             Party->Members[0].ReadyRevision == 0 && Party->Members[1].ReadyRevision == 0);
    TestFalse(TEXT("A stale ready packet cannot consent to the new selection"),
              Fixture.Manager->SetExpeditionReady(Member.Controller, PartyId, InitialRevision, true));
    TestFalse(TEXT("A guessed party identity cannot change readiness"),
              Fixture.Manager->SetExpeditionReady(Member.Controller, FGuid::NewGuid(), ChangedRevision, true));
    TestFalse(TEXT("A stale start snapshot cannot use the new choice"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestTrue(TEXT("Both members may consent again without changing the new revision"),
             Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestEqual(TEXT("Two fresh ready requests share one selection revision"), Party->PreparationRevision,
              ChangedRevision);
    TestTrue(TEXT("Explicit unready is accepted"), Fixture.Ready(Member, false));
    TestFalse(TEXT("Unready immediately blocks admission"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, ChangedRevision, Ticket));
    TestTrue(TEXT("Unready does not erase the other member's consent"),
             Party->Members.ContainsByPredicate(
                 [&](const FFablePartyMember& Entry)
                 {
                     return Entry.PlayerKey == Leader.Profile->PartyPlayerKey && Entry.ReadyRevision == ChangedRevision;
                 }));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionPresenceTest,
                                 "Fablewood.Native.Expedition.PreparationInvalidatesOnLeaderAndPresence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 위임·접속 종료·복귀 뒤에는 이전 구성의 동의가 남지 않아야 한다. */
bool FFableExpeditionPresenceTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(611);
    FPartyTestPlayer Member = Fixture.AddPlayer(612);
    if (!TestTrue(TEXT("Presence fixture has live participants"),
                  Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110))) ||
        !TestTrue(TEXT("Presence fixture creates its leader's party"), Fixture.Act(Leader, EFablePartyAction::Create)))
    {
        return false;
    }

    const FFablePartyRecord* Party = Fixture.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
    const FGuid PartyId = Party->PartyId;
    TestTrue(TEXT("A solo leader can confirm readiness"), Fixture.Ready(Leader));
    const int32 SoloRevision = Party->PreparationRevision;
    TestTrue(TEXT("The leader invites the second participant"),
             Fixture.Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    TestFalse(TEXT("An unresolved invitation rejects a fresh ready request on the server"), Fixture.Ready(Leader));
    FFableExpeditionTicket PendingTicket;
    TestFalse(
        TEXT("An outstanding invitation blocks a solo admission snapshot"),
        Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, Party->PreparationRevision, PendingTicket));
    const FFablePartyInvite* Invite = Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("Roster-change consent invitation exists"), Invite) ||
        !TestTrue(TEXT("The peer accepts the exact invitation"),
                  Fixture.Act(Member, EFablePartyAction::Accept, INDEX_NONE, Invite->InviteId)))
    {
        return false;
    }
    TestTrue(TEXT("Accepting a new member invalidates the solo leader's old readiness"),
             Party->PreparationRevision != SoloRevision && Party->Members[0].ReadyRevision == 0 &&
                 Party->Members[1].ReadyRevision == 0);
    if (!TestTrue(TEXT("Both members consent before transfer"), Fixture.Ready(Leader) && Fixture.Ready(Member)))
    {
        return false;
    }

    int32 Revision = Party->PreparationRevision;
    TestTrue(TEXT("An ordinary authority transfer succeeds"),
             Fixture.Act(Leader, EFablePartyAction::TransferLeader, Member.Profile->GetPlayerId()));
    TestTrue(TEXT("Leadership transfer invalidates old consent"), Party->PreparationRevision != Revision &&
                                                                      Party->Members[0].ReadyRevision == 0 &&
                                                                      Party->Members[1].ReadyRevision == 0);
    TestTrue(TEXT("Both may consent to the new leadership"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    Revision = Party->PreparationRevision;

    // 접속 객체는 유지하되 실제 서버의 접속 해제·복귀 콜백을 사용한다.
    Fixture.Manager->DisconnectPlayer(Member.Controller);
    TestTrue(TEXT("Disconnect invalidates all preparation"), Party->PreparationRevision != Revision &&
                                                                 Party->Members[0].ReadyRevision == 0 &&
                                                                 Party->Members[1].ReadyRevision == 0);
    TestFalse(TEXT("An offline member cannot submit ready"), Fixture.Ready(Member));
    FFableExpeditionTicket Ticket;
    TestFalse(TEXT("A disconnected reserved slot cannot be admitted"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, Party->PreparationRevision, Ticket));
    Revision = Party->PreparationRevision;
    Fixture.Manager->RegisterPlayer(Member.Controller);
    TestTrue(TEXT("Reconnect changes the preparation revision"), Party->PreparationRevision != Revision);
    TestTrue(TEXT("Reconnect restores the same membership without restoring old consent"),
             Member.Profile->PartyId == PartyId && Party->Members[0].ReadyRevision == 0 &&
                 Party->Members[1].ReadyRevision == 0);
    TestTrue(TEXT("Fresh consent after reconnect succeeds"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestTrue(TEXT("The current connected leader can build the restored ticket"),
             Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, Party->PreparationRevision, Ticket));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionEntryTest,
                                 "Fablewood.Native.Expedition.AuthoritativeEntryAndParticipantRewards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 입장·처치 보상·기존 진입점 모두 확정된 참가 명부만 사용해야 한다. */
bool FFableExpeditionEntryTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(621);
    FPartyTestPlayer Member = Fixture.AddPlayer(622);
    FPartyTestPlayer Outsider = Fixture.AddPlayer(623);
    if (!TestTrue(TEXT("Admission fixture has its real game mode and characters"),
                  Fixture.AddMode() && Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110)) &&
                      Fixture.AddHero(Outsider, FableRPG::PortalLocation() + FVector(0, 180, 110))) ||
        !TestTrue(TEXT("Admission party is formed"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }

    const FVector LeaderBefore = Leader.Hero->GetActorLocation();
    const FVector MemberBefore = Member.Hero->GetActorLocation();
    const FVector OutsiderBefore = Outsider.Hero->GetActorLocation();
    TestFalse(TEXT("A leader cannot bypass missing readiness"), Fixture.Enter(Leader));
    TestTrue(TEXT("A rejected start moves nobody and spawns no enemies"),
             Leader.Hero->GetActorLocation().Equals(LeaderBefore) &&
                 Member.Hero->GetActorLocation().Equals(MemberBefore) && Fixture.EnemyCount() == 0);
    TestTrue(TEXT("The exact roster becomes ready"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestFalse(TEXT("A ready peer still cannot start as leader"), Fixture.Enter(Member));
    TestTrue(TEXT("A ready leader atomically starts its expedition"), Fixture.Enter(Leader));

    AFablePartyEncounter* Encounter = Fixture.Mode->GetExpeditionFor(Leader.Profile);
    if (!TestNotNull(TEXT("Admission resolves its own progress actor"), Encounter))
    {
        return false;
    }
    const FFableExpeditionTicket Ticket = Encounter->GetTicket();
    TestTrue(TEXT("Admission creates a fresh server expedition identity"), Ticket.IsValid());
    TestTrue(TEXT("Only the agreed party is bound to that identity"),
             Leader.Profile->ActiveExpeditionId == Ticket.ExpeditionId &&
                 Member.Profile->ActiveExpeditionId == Ticket.ExpeditionId &&
                 !Outsider.Profile->ActiveExpeditionId.IsValid());
    TestTrue(TEXT("Only the ticket participants arrive in the selected region"),
             FableAdventure::For(Leader.Hero).IsInRegion(Ticket.Region, Leader.Hero->GetActorLocation()) &&
                 FableAdventure::For(Member.Hero).IsInRegion(Ticket.Region, Member.Hero->GetActorLocation()) &&
                 Outsider.Hero->GetActorLocation().Equals(OutsiderBefore));
    TestTrue(TEXT("The mode identifies exact participants"),
             Fixture.Mode->IsExpeditionParticipant(Leader.Profile) &&
                 Fixture.Mode->IsExpeditionParticipant(Member.Profile) &&
                 !Fixture.Mode->IsExpeditionParticipant(Outsider.Profile));
    TestTrue(TEXT("The first wave starts only after admission"), Fixture.EnemyCount() > 0);
    TestFalse(TEXT("A duplicate start cannot create another expedition"), Fixture.Enter(Leader));
    TestTrue(TEXT("A duplicate start retains the same identity"),
             Fixture.Mode->GetExpeditionFor(Leader.Profile) == Encounter);

    // 거리만 맞춘 비참가자도 보상을 받거나 기존 지역 진입점으로 합류할 수 없다.
    Fixture.Mode->RequestEnterRegion(Outsider.Controller, Ticket.Region, Ticket.Difficulty);
    TestTrue(TEXT("The legacy entry route never moves an outsider into this party"),
             Outsider.Hero->GetActorLocation().Equals(OutsiderBefore) &&
                 !Outsider.Profile->ActiveExpeditionId.IsValid());
    Outsider.Hero->SetActorLocation(FableRPG::RegionEntrance(Ticket.Region) + Encounter->GetWorldOrigin() +
                                    FVector(700, 0, 110));
    const int32 OutsiderGold = Outsider.Profile->Gold;
    const int32 LeaderGold = Leader.Profile->Gold;
    const int32 MemberGold = Member.Profile->Gold;
    AFableEnemy* Victim = nullptr;
    for (TActorIterator<AFableEnemy> It(Fixture.World); It; ++It)
    {
        if (It->IsAlive())
        {
            Victim = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("Admission spawned a real reward target"), Victim))
    {
        return false;
    }

    Victim->ReceiveHit(1000000, Leader.Controller);
    if (!TestFalse(TEXT("The authoritative hit commits enemy defeat"), Victim->IsAlive()))
    {
        return false;
    }
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("Both admitted participants receive personal kill credit"),
             Leader.Profile->Gold > LeaderGold && Member.Profile->Gold > MemberGold);
    TestEqual(TEXT("An eligible outsider standing inside the arena receives no party reward"), Outsider.Profile->Gold,
              OutsiderGold);
    const int32 RewardedGold = Leader.Profile->Gold;
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestEqual(TEXT("Replayed defeat notification cannot grant twice"), Leader.Profile->Gold, RewardedGold);
    TestTrue(TEXT("Combat reward fixtures remain memory-only"),
             FFablePartyTestAccess::IsMemoryOnly(Leader.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Member.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Outsider.Profile));

    FFablePartyTestAccess::SetEncounterStage(Encounter, EFableAdventureStage::Cleared);
    Outsider.Hero->SetActorLocation(FableRPG::RegionReturnPortal(Ticket.Region) + Encounter->GetWorldOrigin() +
                                    FVector(0, 0, 110));
    const FVector LeaderInDungeon = Leader.Hero->GetActorLocation();
    Fixture.Mode->RequestReturnVillage(Outsider.Controller);
    TestTrue(TEXT("An outsider at the return portal cannot return or clear another party"),
             Leader.Hero->GetActorLocation().Equals(LeaderInDungeon) &&
                 Fixture.Mode->GetExpeditionFor(Leader.Profile) == Encounter);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionBlockedEntryTest,
                                 "Fablewood.Native.Expedition.BlockedEntryPreservesAllParticipants",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 도착점이 막혔을 때 부분 이동·유령 원정·적 생성이 남지 않는지 확인한다. */
bool FFableExpeditionBlockedEntryTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(631);
    FPartyTestPlayer Member = Fixture.AddPlayer(632);
    if (!TestTrue(TEXT("Blocked admission fixture has live owned characters"),
                  Fixture.AddMode() && Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110))) ||
        !TestTrue(TEXT("Blocked admission fixture forms its party"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)) ||
        !TestTrue(TEXT("Both blocked admission members consent"), Fixture.Ready(Leader) && Fixture.Ready(Member)))
    {
        return false;
    }

    AActor* Obstacle = Fixture.World->SpawnActor<AActor>();
    UBoxComponent* Box = Obstacle ? NewObject<UBoxComponent>(Obstacle) : nullptr;
    if (!TestNotNull(TEXT("A real collision obstacle is available"), Box))
    {
        return false;
    }
    Obstacle->SetRootComponent(Box);
    // 보정된 도착점도 벗어날 수 없도록 유효한 유적 범위와 높이를 모두 막는다.
    Box->SetBoxExtent(FVector(5000, 5000, 3000));
    Box->SetWorldLocation(FableRPG::RegionCenter(EFableRegion::RootRuins) + FVector(0, 30000, 0));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();

    const FVector LeaderBefore = Leader.Hero->GetActorLocation();
    const FVector MemberBefore = Member.Hero->GetActorLocation();
    TestFalse(TEXT("A blocked destination rejects the entire admission"), Fixture.Enter(Leader));
    TestTrue(TEXT("Neither player is stranded after a failed preflight"),
             Leader.Hero->GetActorLocation().Equals(LeaderBefore) &&
                 Member.Hero->GetActorLocation().Equals(MemberBefore));
    TestTrue(TEXT("A failed admission leaves no active ticket or orphan progress actor"),
             Fixture.Mode->GetExpeditions().IsEmpty());
    TestTrue(TEXT("A failed admission leaves neither profile bound"),
             !Leader.Profile->ActiveExpeditionId.IsValid() && !Member.Profile->ActiveExpeditionId.IsValid());
    TestEqual(TEXT("A failed admission starts no encounter enemies"), Fixture.EnemyCount(), 0);

    // 같은 실제 입장 경로가 장애물을 해제한 뒤에는 성공해야 한다.
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("The party can confirm readiness after the failure"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestTrue(TEXT("Clearing the obstacle allows the unchanged real entry path to succeed"), Fixture.Enter(Leader));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableIsolationProgressTest,
                                 "Fablewood.Native.Isolation.IndependentProgressRewardsAndRecovery",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 두 파티의 입장·처치·전멸·귀환과 다음 처치가 서로의 정본을 바꾸지 않아야 한다. */
bool FFableIsolationProgressTest::RunTest(const FString& Parameters)
{
    FIsolationTestWorld Fixture;
    if (!TestTrue(TEXT("Four memory-only players form and enter two genuine parties"), Fixture.Initialize()))
    {
        return false;
    }

    const FGuid FirstId = Fixture.One->GetExpeditionId();
    const FGuid SecondId = Fixture.Two->GetExpeditionId();
    const FFableAdventureProgress FieldBefore = Fixture.Base.State->FieldProgress;
    const FFableAdventureProgress SecondBefore = Fixture.Two->GetProgress();
    const TMap<FGuid, float> SecondEnemies = Fixture.EnemyHealth(Fixture.Two);
    const FVector SecondLocation = Fixture.Second.Hero->GetActorLocation();
    TestTrue(TEXT("Two expeditions have distinct immutable identities and arena origins"),
             FirstId.IsValid() && SecondId.IsValid() && FirstId != SecondId &&
                 Fixture.One->GetWorldOrigin() != Fixture.Two->GetWorldOrigin());
    TestTrue(TEXT("Creating private progress never replaces the world's one canonical game state"),
             Fixture.Base.World->GetGameState() == Fixture.Base.State && Fixture.Base.State->PlayerArray.Num() == 4);
    TestEqual(TEXT("The server registry retains both simultaneous expeditions"),
              Fixture.Base.Mode->GetExpeditions().Num(), 2);
    TestTrue(TEXT("Each progress view lists only its own two participants"),
             FableAdventure::For(Fixture.First.Hero).Players().Num() == 2 &&
                 FableAdventure::For(Fixture.Second.Hero).Players().Num() == 2);
    TestTrue(TEXT("Private participants are excluded from public field participants"),
             FableAdventure::Field(Fixture.Base.World).Players().IsEmpty());

    AFableEnemy* Victim = Fixture.EnemyFor(Fixture.One);
    if (!TestNotNull(TEXT("First party has its own real spawned guardian"), Victim))
    {
        return false;
    }
    Victim->ReceiveHit(100000, Fixture.First.Controller);
    Fixture.Base.Mode->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("Both admitted first-party members receive their personal kill reward"),
             Fixture.First.Profile->Gold == 18 && Fixture.FirstPeer.Profile->Gold == 18 &&
                 Fixture.First.Profile->Ore == 1 && Fixture.FirstPeer.Profile->Ore == 1);
    TestTrue(TEXT("No first-party reward leaks into the second party"),
             Fixture.Second.Profile->Gold == 0 && Fixture.SecondPeer.Profile->Gold == 0 &&
                 Fixture.Second.Profile->Ore == 0 && Fixture.SecondPeer.Profile->Ore == 0);
    TestTrue(TEXT("A kill leaves every second-party progress field and enemy identity/health untouched"),
             SameProgress(SecondBefore, Fixture.Two->GetProgress()) &&
                 Fixture.HasEnemyHealth(Fixture.Two, SecondEnemies));

    // 다른 파티가 살아 있어도 첫 파티의 실제 전멸 감시는 자기 인원만 센다.
    AFableEnemy* Attacker = Fixture.EnemyFor(Fixture.One);
    if (!TestNotNull(TEXT("First party retains a live guardian for normal damage"), Attacker))
    {
        return false;
    }
    Fixture.First.Hero->ReceiveHit(100000, Attacker);
    Fixture.FirstPeer.Hero->ReceiveHit(100000, Attacker);
    FFablePartyTestAccess::MonitorEncounter(Fixture.One);
    TestTrue(TEXT("Only the wiped first party enters recovery"),
             Fixture.One->GetProgress().bPartyRecoveryPending && !Fixture.Two->GetProgress().bPartyRecoveryPending);
    TestTrue(TEXT("Recovery and own-enemy cleanup leave the other party fully unchanged"),
             SameProgress(SecondBefore, Fixture.Two->GetProgress()) &&
                 Fixture.HasEnemyHealth(Fixture.Two, SecondEnemies) &&
                 Fixture.Second.Hero->GetActorLocation().Equals(SecondLocation));
    FFablePartyTestAccess::CompleteRecovery(Fixture.One);
    TestTrue(TEXT("Only first-party members return to town and release their bindings"),
             !Fixture.First.Profile->ActiveExpeditionId.IsValid() &&
                 !Fixture.FirstPeer.Profile->ActiveExpeditionId.IsValid() && Fixture.First.Hero->IsAlive() &&
                 Fixture.FirstPeer.Hero->IsAlive() &&
                 FableRPG::IsInRegion(EFableRegion::Village, Fixture.First.Hero->GetActorLocation()) &&
                 Fixture.Second.Profile->ActiveExpeditionId == SecondId &&
                 Fixture.SecondPeer.Profile->ActiveExpeditionId == SecondId);
    TestTrue(TEXT("First-party return preserves the second ticket, progress, actors and position"),
             Fixture.Base.Mode->GetExpeditionFor(Fixture.Second.Profile) == Fixture.Two &&
                 SameProgress(SecondBefore, Fixture.Two->GetProgress()) &&
                 Fixture.HasEnemyHealth(Fixture.Two, SecondEnemies) &&
                 Fixture.Second.Hero->GetActorLocation().Equals(SecondLocation));
    TestFalse(TEXT("A destroyed first expedition cannot resolve as the public field"),
              bool(FableAdventure::For(Fixture.One)));
    TestTrue(TEXT("All private activity leaves the public field progress unchanged"),
             SameProgress(FieldBefore, Fixture.Base.State->FieldProgress) &&
                 Fixture.Base.World->GetGameState() == Fixture.Base.State);

    AFableEnemy* SecondVictim = Fixture.EnemyFor(Fixture.Two);
    if (!TestNotNull(TEXT("Second party still has a functioning independent target"), SecondVictim))
    {
        return false;
    }
    SecondVictim->ReceiveHit(100000, Fixture.Second.Controller);
    Fixture.Base.Mode->HandleEnemyDefeated(SecondVictim);
    TestTrue(TEXT("The second party can continue and both its members earn only its own reward"),
             Fixture.Second.Profile->Gold == 18 && Fixture.SecondPeer.Profile->Gold == 18 &&
                 Fixture.First.Profile->Gold == 18 && Fixture.FirstPeer.Profile->Gold == 18);
    TestTrue(TEXT("Every profile stayed memory-only throughout real rewards and recovery"),
             FFablePartyTestAccess::IsMemoryOnly(Fixture.First.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Fixture.FirstPeer.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Fixture.Second.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Fixture.SecondPeer.Profile));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableIsolationEffectsTest,
                                 "Fablewood.Native.Isolation.OverlappingActorsRejectForeignEffects",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 거리·높이·시야까지 만족한 두 파티도 UUID가 다르면 직접·광역·화상·부활을 공유하지 않는다. */
bool FFableIsolationEffectsTest::RunTest(const FString& Parameters)
{
    FIsolationTestWorld Fixture;
    if (!TestTrue(TEXT("Two real two-member expeditions deliberately share one test origin"), Fixture.Initialize(true)))
    {
        return false;
    }
    Fixture.MoveExistingEnemiesAway();
    const FVector Center = FableRPG::RegionCenter(EFableRegion::RootRuins) + Fixture.One->GetWorldOrigin();
    Fixture.First.Hero->SetActorLocation(Center + FVector(-300, 0, 110));
    Fixture.FirstPeer.Hero->SetActorLocation(Center + FVector(-300, 180, 110));
    Fixture.Second.Hero->SetActorLocation(Center + FVector(-300, -100, 110));
    Fixture.SecondPeer.Hero->SetActorLocation(Center + FVector(-300, -180, 110));
    AFableEnemy* Own = Fixture.SpawnEnemy(Fixture.One, FVector(0, 0, 110));
    AFableEnemy* Foreign = Fixture.SpawnEnemy(Fixture.Two, FVector(0, 120, 110));
    if (!TestNotNull(TEXT("Own effect target exists"), Own) ||
        !TestNotNull(TEXT("Foreign effect target exists"), Foreign))
    {
        return false;
    }
    TestTrue(
        TEXT("Both private views accept these deliberately overlapping coordinates"),
        FableAdventure::For(Own).IsInRegion(EFableRegion::RootRuins, Fixture.Second.Hero->GetActorLocation()) &&
            FableAdventure::For(Foreign).IsInRegion(EFableRegion::RootRuins, Fixture.First.Hero->GetActorLocation()));
    TestTrue(TEXT("Matching position does not grant matching scope"),
             FableAdventure::SameScope(Fixture.First.Hero, Own) &&
                 !FableAdventure::SameScope(Fixture.First.Hero, Foreign));

    const float ForeignHealth = Foreign->GetHealth();
    Foreign->ReceiveHit(10, Fixture.First.Controller);
    TestEqual(TEXT("The final direct-hit guard rejects another party"), Foreign->GetHealth(), ForeignHealth);
    TestFalse(TEXT("The final slow guard rejects another party"),
              Foreign->ApplySlow(.65f, 4, Fixture.First.Controller));
    TestFalse(TEXT("The final burn guard rejects another party"), Foreign->ApplyBurn(2, 3, Fixture.First.Controller));
    const float OwnHealth = Own->GetHealth();
    FFableExpeditionCombatTestAccess::Area(Fixture.First.Hero, 5, true);
    TestTrue(TEXT("The same real area skill damages and slows its own target"),
             Own->GetHealth() < OwnHealth && Own->GetSlowTimeRemaining() > 0);
    TestEqual(TEXT("The in-range foreign target gets neither area damage nor effect"), Foreign->GetHealth(),
              ForeignHealth);
    TestEqual(TEXT("Foreign target has no slow"), Foreign->GetSlowTimeRemaining(), 0.0f);
    TestTrue(TEXT("The own burn is accepted"), Own->ApplyBurn(2, 3, Fixture.First.Controller));
    FFableExpeditionCombatTestAccess::TickBurn(Own);
    TestEqual(TEXT("A valid source applies a scheduled burn tick"), Own->GetBurnTickCount(), 1);
    const float BeforeStaleBurn = Own->GetHealth();
    const FGuid FirstId = Fixture.First.Profile->ActiveExpeditionId;
    Fixture.First.Profile->ActiveExpeditionId = FGuid::NewGuid();
    TestFalse(TEXT("An unresolved owned UUID never falls back to public field progress"),
              bool(FableAdventure::For(Fixture.First.Hero)));
    FFableExpeditionCombatTestAccess::TickBurn(Own);
    TestEqual(TEXT("A stale source cannot continue damaging an old expedition"), Own->GetHealth(), BeforeStaleBurn);
    TestEqual(TEXT("The stale burn is cleared"), Own->GetBurnTimeRemaining(), 0.0f);
    Fixture.First.Profile->ActiveExpeditionId = FirstId;

    const float FirstHealth = Fixture.First.Hero->GetHealth();
    Fixture.First.Hero->ReceiveHit(10, Foreign);
    Fixture.First.Hero->ReceiveHit(10);
    TestEqual(TEXT("Foreign and source-free private damage change no hero health"), Fixture.First.Hero->GetHealth(),
              FirstHealth);
    Own->SetActorLocation(Center + FVector(-150, 0, 110));
    const float SecondHealth = Fixture.Second.Hero->GetHealth();
    FFableExpeditionCombatTestAccess::EnemyArea(Own);
    TestTrue(TEXT("Enemy area damage still affects an own participant"), Fixture.First.Hero->GetHealth() < FirstHealth);
    TestEqual(TEXT("Enemy area damage ignores an overlapping foreign participant"), Fixture.Second.Hero->GetHealth(),
              SecondHealth);
    Fixture.FirstPeer.Hero->SetActorLocation(Center + FVector(-300, 600, 110));
    Fixture.First.Hero->SetActorLocation(Center + FVector(-300, 200, 110));
    Fixture.Second.Hero->SetActorLocation(Own->GetActorLocation() + FVector(20, 0, 0));
    TestTrue(TEXT("AI ignores the closer foreign character and selects an own participant"),
             FFableExpeditionCombatTestAccess::FindTarget(Own) == Fixture.First.Hero);

    Fixture.FirstPeer.Hero->SetActorLocation(Fixture.First.Hero->GetActorLocation() + FVector(0, 180, 0));
    Fixture.SecondPeer.Hero->SetActorLocation(Fixture.First.Hero->GetActorLocation() + FVector(0, -180, 0));
    Own->SetActorLocation(Center + FVector(-150, 700, 110));
    FFableExpeditionCombatTestAccess::ShareShield(Fixture.First.Hero);
    TestTrue(TEXT("Guardian shield reaches the actual own companion"), Fixture.FirstPeer.Hero->GetShieldAmount() > 0);
    TestEqual(TEXT("Guardian shield reaches no other party"), Fixture.SecondPeer.Hero->GetShieldAmount(), 0.0f);
    Fixture.FirstPeer.Hero->ReceiveHit(100000, Own);
    Fixture.SecondPeer.Hero->ReceiveHit(100000, Foreign);
    TestTrue(TEXT("Both parties have an equally nearby genuinely downed target"),
             Fixture.FirstPeer.Hero->IsDowned() && Fixture.SecondPeer.Hero->IsDowned());
    TestFalse(TEXT("A same-distance foreign downed character cannot be revived"),
              FFableExpeditionCombatTestAccess::CanRevive(Fixture.First.Hero, Fixture.SecondPeer.Hero));
    TestTrue(TEXT("The own companion passes the same real revive checks"),
             FFableExpeditionCombatTestAccess::CanRevive(Fixture.First.Hero, Fixture.FirstPeer.Hero));
    TestTrue(TEXT("An own revive channel starts normally"), Fixture.First.Hero->BeginRevive(Fixture.FirstPeer.Hero));
    Fixture.First.Profile->ActiveExpeditionId = FGuid::NewGuid();
    FFableExpeditionCombatTestAccess::TickLife(Fixture.First.Hero);
    TestTrue(TEXT("Changing scope interrupts the existing revive channel"),
             !Fixture.First.Hero->GetReviveTarget() && !Fixture.FirstPeer.Hero->GetReviver());
    Fixture.First.Profile->ActiveExpeditionId = FirstId;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableIsolationProjectileTest,
                                 "Fablewood.Native.Isolation.SweptProjectilesAndStaleSources",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 sweep가 다른 파티를 관통해 자기 대상을 맞히며 재입장한 시전자의 옛 탄은 무효다. */
bool FFableIsolationProjectileTest::RunTest(const FString& Parameters)
{
    FIsolationTestWorld Fixture;
    if (!TestTrue(TEXT("Swept collision fixture has two real overlapping Moonwell parties"),
                  Fixture.Initialize(true, EFableRegion::Moonwell)))
    {
        return false;
    }
    Fixture.MoveExistingEnemiesAway();
    const FVector Center = FableRPG::RegionCenter(EFableRegion::Moonwell) + Fixture.One->GetWorldOrigin();
    Fixture.First.Hero->SetActorLocation(Center + FVector(-550, 0, 110));
    Fixture.FirstPeer.Hero->SetActorLocation(Center + FVector(-1500, 1000, 110));
    Fixture.Second.Hero->SetActorLocation(Center + FVector(-1500, -1000, 110));
    Fixture.SecondPeer.Hero->SetActorLocation(Center + FVector(-1500, -1300, 110));
    AFableEnemy* Own = Fixture.SpawnEnemy(Fixture.One, FVector(200, 0, 110));
    AFableEnemy* Foreign = Fixture.SpawnEnemy(Fixture.Two, FVector(-100, 0, 110));
    if (!TestNotNull(TEXT("Own swept target exists"), Own) ||
        !TestNotNull(TEXT("Foreign swept occluder exists"), Foreign))
    {
        return false;
    }
    FActorSpawnParameters ParametersForShot;
    ParametersForShot.Owner = Fixture.First.Hero;
    ParametersForShot.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableProjectile* Shot = Fixture.Base.World->SpawnActor<AFableProjectile>(Center + FVector(-400, 0, 110),
                                                                              FRotator::ZeroRotator, ParametersForShot);
    if (!TestNotNull(TEXT("A real server hero projectile exists"), Shot))
    {
        return false;
    }
    Shot->InitializeShot(FVector::ForwardVector, 5, Fixture.First.Controller, true, 0, 1000, 1200);
    const float OwnHealth = Own->GetHealth();
    const float ForeignHealth = Foreign->GetHealth();
    Shot->Tick(.8f);
    TestTrue(TEXT("A shot sweeps through the foreign occluder and hits its own enemy"), Own->GetHealth() < OwnHealth);
    TestEqual(TEXT("The foreign occluder takes no shot damage"), Foreign->GetHealth(), ForeignHealth);
    TestEqual(TEXT("Only one own enemy consumed a hit"), Shot->GetUniqueHitCount(), 1);
    TestTrue(TEXT("The projectile captured its immutable source UUID"),
             Shot->GetExpeditionId() == Fixture.One->GetExpeditionId());

    AFableProjectile* Stale = Fixture.Base.World->SpawnActor<AFableProjectile>(
        Center + FVector(-400, 0, 110), FRotator::ZeroRotator, ParametersForShot);
    if (!TestNotNull(TEXT("A second real shot exists before scope changes"), Stale))
    {
        return false;
    }
    Stale->InitializeShot(FVector::ForwardVector, 5, Fixture.First.Controller, true, 0, 1000, 1200);
    const FGuid Captured = Stale->GetExpeditionId();
    const float BeforeStale = Own->GetHealth();
    Fixture.First.Profile->ActiveExpeditionId = FGuid::NewGuid();
    Stale->Tick(.8f);
    TestTrue(TEXT("An old shot retains its UUID and is destroyed when its source changes scope"),
             Stale->GetExpeditionId() == Captured && Stale->IsActorBeingDestroyed());
    TestEqual(TEXT("The stale shot cannot damage the previous encounter"), Own->GetHealth(), BeforeStale);
    Fixture.First.Profile->ActiveExpeditionId = Captured;

    // 적 발사체도 앞의 다른 파티 캐릭터를 무시하고 자기 참가자만 맞힌다.
    Own->SetActorLocation(Center + FVector(-550, 0, 110));
    Foreign->SetActorLocation(Center + FVector(-1500, 1300, 110));
    Fixture.First.Hero->SetActorLocation(Center + FVector(200, 0, 110));
    Fixture.Second.Hero->SetActorLocation(Center + FVector(-100, 0, 110));
    FActorSpawnParameters ParametersForBolt;
    ParametersForBolt.Owner = Own;
    ParametersForBolt.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableEnemyBolt* Bolt = Fixture.Base.World->SpawnActor<AFableEnemyBolt>(Center + FVector(-400, 0, 110),
                                                                            FRotator::ZeroRotator, ParametersForBolt);
    if (!TestNotNull(TEXT("A real enemy bolt exists"), Bolt))
    {
        return false;
    }
    Bolt->InitializeBolt(FVector::ForwardVector, 10, EFableRegion::Moonwell);
    const float HeroHealth = Fixture.First.Hero->GetHealth();
    const float OtherHealth = Fixture.Second.Hero->GetHealth();
    Bolt->Tick(1.0f);
    TestTrue(TEXT("The enemy bolt sweeps through the foreign hero and damages its own participant"),
             Fixture.First.Hero->GetHealth() < HeroHealth);
    TestEqual(TEXT("The enemy bolt changes no foreign hero health"), Fixture.Second.Hero->GetHealth(), OtherHealth);
    TestTrue(TEXT("Enemy bolts capture the same immutable expedition identity"),
             Bolt->GetExpeditionId() == Fixture.One->GetExpeditionId());
    return true;
}

namespace
{
    /** 보상 검증은 실제 두 파티와 처치 경로를 사용하며 지연 시간만 가속한다. */
    struct FResultTestWorld : FIsolationTestWorld
    {
        /** 첫 파티의 실제 수호자 두 무리와 보스를 차례로 처치한다. */
        bool ClearFirst()
        {
            for (int32 Phase = 0; Phase < 3; ++Phase)
            {
                TArray<TWeakObjectPtr<AFableEnemy>> Targets;
                for (TActorIterator<AFableEnemy> It(Base.World); It; ++It)
                {
                    if (It->IsAlive() && It->GetExpeditionId() == One->GetExpeditionId())
                    {
                        Targets.Add(*It);
                    }
                }
                if (Targets.IsEmpty())
                {
                    return false;
                }

                for (const auto& Target : Targets)
                {
                    AFableEnemy* Enemy = Target.Get();
                    if (!Enemy)
                    {
                        return false;
                    }
                    Enemy->ReceiveHit(100000, First.Controller);
                    Base.Mode->HandleEnemyDefeated(Enemy);
                    if (Enemy->IsAlive())
                    {
                        return false;
                    }
                }

                if (Phase < 2)
                {
                    FFablePartyTestAccess::AdvanceRootWaveBoundary(One);
                }
            }
            return One->GetProgress().AdventureStage == EFableAdventureStage::Cleared;
        }

        /** 현재 서버 결과를 복사하여 나중의 조회·재도전과 비교한다. */
        FFablePersonalExpeditionResult Result(const FPartyTestPlayer& Player, FGuid ExpeditionId) const
        {
            const auto* Found = Base.Mode->GetResultByExpedition(Player.Profile, ExpeditionId);
            return Found ? *Found : FFablePersonalExpeditionResult();
        }

        /** 두 소유자가 각자 현재 종료 결과의 정확한 버전을 확인한다. */
        bool ConfirmFirstResults(FGuid ExpeditionId)
        {
            for (const FPartyTestPlayer* Player : {&First, &FirstPeer})
            {
                const auto Current = Result(*Player, ExpeditionId);
                if (!Current.IsTerminal() ||
                    !Base.Mode->AcknowledgeExpeditionResult(Player->Controller, ExpeditionId, Current.CompletionId,
                                                            Current.ResultRevision))
                {
                    return false;
                }
            }
            return true;
        }

        /** 같은 결과의 준비 요청에 정확한 현재 명부 리비전을 사용한다. */
        bool PrepareRetry(const FPartyTestPlayer& Leader, const FFablePersonalExpeditionResult& Previous)
        {
            const auto* Party = Base.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
            return Party &&
                   Base.Manager->PrepareExpeditionRetry(Leader.Controller, Party->PartyId, Party->PreparationRevision,
                                                        Previous.Ticket.ExpeditionId, Previous.CompletionId);
        }

        /** 전원 준비 이후에도 별도의 재도전 API만 새 원정을 시작한다. */
        bool Retry(const FPartyTestPlayer& Leader, const FFablePersonalExpeditionResult& Previous)
        {
            const auto* Party = Base.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
            return Party &&
                   Base.Mode->RequestRetryExpedition(Leader.Controller, Party->PartyId, Party->PreparationRevision,
                                                     Previous.Ticket.ExpeditionId, Previous.CompletionId);
        }
    };

    /** 보상 영수증 전체의 반사 필드를 비교해 옵션·내구도도 빠뜨리지 않는다. */
    bool SameReceipts(const FFablePersonalExpeditionResult& A, const FFablePersonalExpeditionResult& B)
    {
        if (A.Receipts.Num() != B.Receipts.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < A.Receipts.Num(); ++Index)
        {
            if (!FFablePersonalGrantReceipt::StaticStruct()->CompareScriptStruct(&A.Receipts[Index], &B.Receipts[Index],
                                                                                 0))
            {
                return false;
            }
        }
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePersonalResultTest,
                                 "Fablewood.Native.Results.PersonalReceiptsAndTerminalReadOnly",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 처치 지급과 조회를 분리하고 NPC 보고·개인 장비 인스턴스를 보존한다. */
bool FFablePersonalResultTest::RunTest(const FString& Parameters)
{
    FResultTestWorld Fixture;
    if (!TestTrue(TEXT("Result fixture enters two genuine parties"), Fixture.Initialize()) ||
        !TestTrue(TEXT("Actual guardian waves and boss are defeated"), Fixture.ClearFirst()))
    {
        return false;
    }

    const FGuid Id = Fixture.One->GetExpeditionId();
    const auto Before = Fixture.Result(Fixture.First, Id);
    const auto Peer = Fixture.Result(Fixture.FirstPeer, Id);
    TestTrue(TEXT("Both personal results have one immutable terminal completion"),
             Before.IsTerminal() && Peer.IsTerminal() && Before.CompletionId == Peer.CompletionId &&
                 Before.Outcome == EFableExpeditionOutcome::Cleared);
    TestEqual(TEXT("Six guardians and one boss create exactly seven receipts"), Before.Receipts.Num(), 7);
    TestEqual(TEXT("Actual run gold is granted once"), Fixture.First.Profile->Gold, 248);
    TestEqual(TEXT("Actual run ore is granted once"), Fixture.First.Profile->Ore, 14);
    TestEqual(TEXT("Actual run grants one root core"), Fixture.First.Profile->RootCores, 1);
    TestTrue(TEXT("Boss completion leaves the NPC report ready without claiming it"),
             Fixture.First.Profile->DungeonQuest == EFableQuestStage::Ready &&
                 Fixture.First.Profile->DungeonCompletions == 1);

    int32 Gold = 0, Ore = 0, Experience = 0, Cores = 0, Items = 0;
    int64 LastSequence = 0;
    TSet<FGuid> Defeats;
    for (const auto& Receipt : Before.Receipts)
    {
        TestTrue(TEXT("A receipt binds this exact recipient and actual defeat"),
                 Receipt.ExpeditionId == Id && Receipt.RecipientPublicKey == Fixture.First.Profile->PartyPlayerKey &&
                     Receipt.GrantId.IsValid() && !Defeats.Contains(Receipt.GrantId) &&
                     Receipt.Sequence > LastSequence && Fixture.One->HasRecordedDefeat(Receipt.GrantId));
        TestTrue(TEXT("Memory-only fixtures never claim a durable database commit"),
                 Receipt.SaveState == EFableExpeditionGrantSaveState::MemoryOnly && Receipt.CommittedRevision == 0);
        Defeats.Add(Receipt.GrantId);
        LastSequence = Receipt.Sequence;
        Gold += Receipt.ActualGold;
        Ore += Receipt.ActualOre;
        Experience += Receipt.ActualExperience;
        Cores += Receipt.ActualRootCores;
        Items += Receipt.GrantedItems.Num();
        for (const auto& Item : Receipt.GrantedItems)
        {
            TestTrue(TEXT("A receipt contains the actual awarded equipment identity"),
                     Item.InstanceId.IsValid() && Fixture.First.Profile->Inventory.ContainsByPredicate(
                                                      [&Item](const FFableItem& Owned)
                                                      {
                                                          return Owned.InstanceId == Item.InstanceId;
                                                      }));
        }
    }
    TestTrue(TEXT("Receipt sums are the exact run payout"),
             Gold == 248 && Ore == 14 && Experience == 460 && Cores == 1 && Items == 1);

    const int32 Bag = Fixture.First.Profile->Inventory.Num();
    TestTrue(TEXT("The owner can reopen its existing result"),
             Fixture.Base.Mode->RequestExpeditionResult(Fixture.First.Controller, Id));
    TestTrue(TEXT("An exact terminal confirmation is accepted"),
             Fixture.Base.Mode->AcknowledgeExpeditionResult(Fixture.First.Controller, Id, Before.CompletionId,
                                                            Fixture.Result(Fixture.First, Id).ResultRevision));
    TestTrue(TEXT("Reopening a confirmed result is still read-only"),
             Fixture.Base.Mode->RequestExpeditionResult(Fixture.First.Controller, Id));
    const auto After = Fixture.Result(Fixture.First, Id);
    TestTrue(TEXT("Lookup and confirmation never reroll, repay or replace completion"),
             SameReceipts(Before, After) && After.CompletionId == Before.CompletionId &&
                 Fixture.First.Profile->Gold == 248 && Fixture.First.Profile->Ore == 14 &&
                 Fixture.First.Profile->Inventory.Num() == Bag);
    TestFalse(TEXT("A foreign party cannot look up another player's expedition"),
              Fixture.Base.Mode->RequestExpeditionResult(Fixture.Second.Controller, Id));
    TestFalse(TEXT("A random completion cannot acknowledge the result"),
              Fixture.Base.Mode->AcknowledgeExpeditionResult(Fixture.First.Controller, Id, FGuid::NewGuid(),
                                                             After.ResultRevision));
    TestFalse(TEXT("A stale result version cannot acknowledge it"),
              Fixture.Base.Mode->AcknowledgeExpeditionResult(Fixture.First.Controller, Id, After.CompletionId,
                                                             After.ResultRevision - 1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableReceiptClampTest, "Fablewood.Native.Results.RewardClampAndIdentityGuards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 상한에서 명목 보상 대신 실제 증분을 기록하고 출처 재전송을 거부한다. */
bool FFableReceiptClampTest::RunTest(const FString& Parameters)
{
    FResultTestWorld Fixture;
    if (!TestTrue(TEXT("Clamped payout fixture has genuine participants"), Fixture.Initialize()))
    {
        return false;
    }
    Fixture.First.Profile->Gold = 999997;
    Fixture.First.Profile->Ore = 100000;
    Fixture.First.Profile->Level = 20;
    Fixture.First.Profile->Experience = 0;
    AFableEnemy* Victim = Fixture.EnemyFor(Fixture.One);
    if (!TestNotNull(TEXT("A real tracked source exists"), Victim))
    {
        return false;
    }

    const FGuid Id = Fixture.One->GetExpeditionId();
    Victim->ReceiveHit(100000, Fixture.First.Controller);
    Fixture.Base.Mode->HandleEnemyDefeated(Victim);
    const auto Before = Fixture.Result(Fixture.First, Id);
    if (!TestEqual(TEXT("Exactly one tracked defeat was recorded"), Before.Receipts.Num(), 1))
    {
        return false;
    }
    TestTrue(TEXT("Receipt records actual clamped amounts and capped XP"),
             Before.Receipts[0].ActualGold == 3 && Before.Receipts[0].ActualOre == 0 &&
                 Before.Receipts[0].ActualExperience == 0 && Fixture.First.Profile->Gold == 1000000 &&
                 Fixture.First.Profile->Ore == 100000 && Fixture.First.Profile->Experience == 0);
    Fixture.Base.Mode->HandleEnemyDefeated(Victim);
    Fixture.One->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("Duplicate callbacks cannot mint a second receipt or payout"),
             SameReceipts(Before, Fixture.Result(Fixture.First, Id)));

    AFableEnemy* Untracked = Fixture.SpawnEnemy(Fixture.One, FVector(500, 500, 110));
    if (!TestNotNull(TEXT("An untracked actor can present the same region and expedition"), Untracked))
    {
        return false;
    }
    Untracked->ReceiveHit(100000, Fixture.First.Controller);
    Fixture.Base.Mode->HandleEnemyDefeated(Untracked);
    TestTrue(TEXT("Same UUID without the actual registered defeat is not a grant source"),
             SameReceipts(Before, Fixture.Result(Fixture.First, Id)));
    TestFalse(TEXT("A live run has no terminal completion to acknowledge"),
              Fixture.Base.Mode->AcknowledgeExpeditionResult(Fixture.First.Controller, Id, FGuid::NewGuid(),
                                                             Before.ResultRevision));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableConsentedRetryTest, "Fablewood.Native.Expedition.ConsentedRetryAndFreshIdentity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 보스 의뢰를 보고하지 않아도 전원 새 동의로만 다음 원정을 시작한다. */
bool FFableConsentedRetryTest::RunTest(const FString& Parameters)
{
    FResultTestWorld Fixture;
    if (!TestTrue(TEXT("Retry fixture clears its genuine first expedition"),
                  Fixture.Initialize() && Fixture.ClearFirst()))
    {
        return false;
    }
    const FGuid OldId = Fixture.One->GetExpeditionId();
    const auto Previous = Fixture.Result(Fixture.First, OldId);
    const auto ForeignProgress = Fixture.Two->GetProgress();
    const auto ForeignEnemies = Fixture.EnemyHealth(Fixture.Two);
    const FGuid ForeignId = Fixture.Two->GetExpeditionId();
    const int32 Gold = Fixture.First.Profile->Gold;
    const int32 Ore = Fixture.First.Profile->Ore;
    const int32 Completions = Fixture.First.Profile->DungeonCompletions;

    TestFalse(TEXT("Unread terminal results cannot be abandoned into a new run"),
              Fixture.PrepareRetry(Fixture.First, Previous));
    TestTrue(TEXT("Both owners explicitly confirm their previous personal results"),
             Fixture.ConfirmFirstResults(OldId));
    TestFalse(TEXT("A peer cannot prepare the leader's retry"), Fixture.PrepareRetry(Fixture.FirstPeer, Previous));
    TestTrue(TEXT("Leader prepares the exact terminal result"), Fixture.PrepareRetry(Fixture.First, Previous));
    TestFalse(TEXT("Retry preparation clears earlier readiness"), Fixture.Retry(Fixture.First, Previous));
    TestTrue(TEXT("Only the leader has consented so far"), Fixture.Base.Ready(Fixture.First));
    TestFalse(TEXT("One ready member cannot move the party"), Fixture.Retry(Fixture.First, Previous));
    TestTrue(TEXT("The peer explicitly consents"), Fixture.Base.Ready(Fixture.FirstPeer));
    TestFalse(TEXT("Normal start cannot consume a retry selection"), Fixture.Base.Enter(Fixture.First));
    TestFalse(TEXT("A ready peer still cannot launch a retry"), Fixture.Retry(Fixture.FirstPeer, Previous));
    TestTrue(TEXT("Exact explicit retry creates the next expedition"), Fixture.Retry(Fixture.First, Previous));

    AFablePartyEncounter* Next = Fixture.Base.Mode->GetExpeditionFor(Fixture.First.Profile);
    if (!TestNotNull(TEXT("Both bindings resolve to the new committed actor"), Next))
    {
        return false;
    }
    TestTrue(TEXT("Replacement has a new UUID and retires only the old actor"),
             Next->GetExpeditionId() != OldId && Next->GetExpeditionId() != ForeignId &&
                 Fixture.FirstPeer.Profile->ActiveExpeditionId == Next->GetExpeditionId() &&
                 !Fixture.Base.Mode->FindExpedition(OldId));
    TestTrue(TEXT("Retry neither pays an NPC reward nor resets its ready quest"),
             Fixture.First.Profile->DungeonQuest == EFableQuestStage::Ready &&
                 Fixture.First.Profile->DungeonCompletions == Completions && Fixture.First.Profile->Gold == Gold &&
                 Fixture.First.Profile->Ore == Ore);
    TestTrue(TEXT("Original immutable receipts remain queryable"),
             SameReceipts(Previous, Fixture.Result(Fixture.First, OldId)));
    TestTrue(TEXT("A different party retains its exact actors and progress"),
             Fixture.Second.Profile->ActiveExpeditionId == ForeignId &&
                 SameProgress(ForeignProgress, Fixture.Two->GetProgress()) &&
                 Fixture.HasEnemyHealth(Fixture.Two, ForeignEnemies));
    TestFalse(TEXT("Consumed old completion cannot be replayed"), Fixture.Retry(Fixture.First, Previous));

    AFableEnemy* Victim = Fixture.EnemyFor(Next);
    if (!TestNotNull(TEXT("The new run contains genuine reward sources"), Victim))
    {
        return false;
    }
    Victim->ReceiveHit(100000, Fixture.First.Controller);
    Fixture.Base.Mode->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("A Ready quest still allows new-run combat rewards exactly once"),
             Fixture.First.Profile->Gold == Gold + 18 && Fixture.First.Profile->Ore == Ore + 1 &&
                 Fixture.Result(Fixture.First, Next->GetExpeditionId()).Receipts.Num() == 1 &&
                 Fixture.First.Profile->DungeonQuest == EFableQuestStage::Ready);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableRetryRollbackTest, "Fablewood.Native.Expedition.RetryPartialArrivalRollback",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 첫 도착 후 두 번째 캡슐이 충돌하는 실패도 기존 원정과 전투 상태를 보존한다. */
bool FFableRetryRollbackTest::RunTest(const FString& Parameters)
{
    FResultTestWorld Fixture;
    if (!TestTrue(TEXT("Rollback fixture has a genuinely cleared expedition"),
                  Fixture.Initialize() && Fixture.ClearFirst()))
    {
        return false;
    }
    const FGuid OldId = Fixture.One->GetExpeditionId();
    const auto Previous = Fixture.Result(Fixture.First, OldId);
    TestTrue(TEXT("Both old personal results are explicitly confirmed before retry"),
             Fixture.ConfirmFirstResults(OldId));
    if (!TestTrue(TEXT("Rollback party explicitly prepares and consents"),
                  Fixture.PrepareRetry(Fixture.First, Previous) && Fixture.Base.Ready(Fixture.First) &&
                      Fixture.Base.Ready(Fixture.FirstPeer)))
    {
        return false;
    }

    // 기존 원정에서는 충분히 떨어진 유효 캡슐을 사용한다.
    AFableCharacter* Leader = Fixture.First.Hero;
    AFableCharacter* Peer = Fixture.FirstPeer.Hero;
    Leader->GetCapsuleComponent()->SetCapsuleSize(240, 250);
    Peer->GetCapsuleComponent()->SetCapsuleSize(240, 250);
    Leader->SetActorLocation(Fixture.One->GetWorldOrigin() + FVector(7100, -500, 260));
    Peer->SetActorLocation(Fixture.One->GetWorldOrigin() + FVector(9000, -500, 260));
    FFableExpeditionCombatTestAccess::SeedTravelState(Leader);
    FFableExpeditionCombatTestAccess::SeedTravelState(Peer);
    Leader->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Peer->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Leader->GetCharacterMovement()->Velocity = FVector(71, 43, -17);
    Peer->GetCharacterMovement()->Velocity = FVector(-59, 31, -19);

    // 실제 allocator의 첫 빈 원정 원점을 찾는다.
    FVector NextOrigin;
    for (int32 Slot = 1; Slot <= 16; ++Slot)
    {
        const FVector Candidate(0, Slot * 30000.0f, 0);
        if (!Fixture.Base.Mode->GetExpeditions().ContainsByPredicate(
                [&Candidate](const AFablePartyEncounter* Encounter)
                {
                    return Encounter->GetWorldOrigin().Equals(Candidate);
                }))
        {
            NextOrigin = Candidate;
            break;
        }
    }
    if (!TestFalse(TEXT("A distinct destination arena is available"), NextOrigin.IsZero()))
    {
        return false;
    }

    // 도착 지형과 같은 높이의 바닥에서 각 빈 도착점은 개별적으로 통과해야 한다.
    AActor* Floor = Fixture.Base.World->SpawnActor<AActor>();
    UBoxComponent* FloorBox = Floor ? NewObject<UBoxComponent>(Floor) : nullptr;
    if (!TestNotNull(TEXT("A real destination floor exists"), FloorBox))
    {
        return false;
    }
    Floor->SetRootComponent(FloorBox);
    FloorBox->SetBoxExtent(FVector(1800, 1800, 25));
    FloorBox->SetWorldLocation(NextOrigin + FableRPG::RegionCenter(EFableRegion::RootRuins) + FVector(0, 0, -25));
    FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    FloorBox->SetCollisionObjectType(ECC_WorldStatic);
    FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
    FloorBox->RegisterComponent();

    const FRotator Arrival(0, 90, 0);
    FVector FirstSpot = FableRPG::RegionEntrance(EFableRegion::RootRuins) + NextOrigin + FVector(0, 0, 10);
    FVector SecondSpot = FirstSpot + FVector(240, 0, 0);
    if (!TestTrue(TEXT("Both empty destination preflights independently succeed"),
                  Fixture.Base.World->FindTeleportSpot(Leader, FirstSpot, Arrival) &&
                      Fixture.Base.World->FindTeleportSpot(Peer, SecondSpot, Arrival)))
    {
        return false;
    }

    // 먼저 도착한 캡슐 하나만으로 두 번째 실제 도착 허용 범위를 벗어나는지 확인한다.
    AActor* Occupancy = Fixture.Base.World->SpawnActor<AActor>();
    UCapsuleComponent* OccupiedCapsule = Occupancy ? NewObject<UCapsuleComponent>(Occupancy) : nullptr;
    if (!TestNotNull(TEXT("An occupancy probe uses the same real pawn capsule"), OccupiedCapsule))
    {
        return false;
    }
    Occupancy->SetRootComponent(OccupiedCapsule);
    OccupiedCapsule->SetCapsuleSize(240, 250);
    OccupiedCapsule->SetCollisionProfileName(TEXT("Pawn"));
    OccupiedCapsule->SetWorldLocation(FirstSpot);
    OccupiedCapsule->RegisterComponent();
    FVector OccupiedSecond = SecondSpot;
    const bool bOccupiedSpotFound = Fixture.Base.World->FindTeleportSpot(Peer, OccupiedSecond, Arrival);
    TestTrue(TEXT("First-arrival occupancy prevents a second in-tolerance arrival"),
             !bOccupiedSpotFound || FVector::DistSquared2D(OccupiedSecond, SecondSpot) > FMath::Square(150.0f) ||
                 FMath::Abs(OccupiedSecond.Z - SecondSpot.Z) > 180);
    OccupiedCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Occupancy->Destroy();

    FActorSpawnParameters ShotParameters;
    ShotParameters.Owner = Leader;
    ShotParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableProjectile* Shot = Fixture.Base.World->SpawnActor<AFableProjectile>(
        Leader->GetActorLocation() + FVector(0, 0, 200), FRotator::ZeroRotator, ShotParameters);
    if (!TestNotNull(TEXT("A still-valid old-expedition projectile exists"), Shot))
    {
        return false;
    }
    Shot->InitializeShot(FVector::ForwardVector, 10, Fixture.First.Controller, false);

    const auto LeaderState = FFableExpeditionCombatTestAccess::CaptureTravelState(Leader);
    const auto PeerState = FFableExpeditionCombatTestAccess::CaptureTravelState(Peer);
    const FTransform LeaderTransform = Leader->GetActorTransform();
    const FTransform PeerTransform = Peer->GetActorTransform();
    const FVector LeaderVelocity = Leader->GetVelocity();
    const FVector PeerVelocity = Peer->GetVelocity();
    const auto ForeignProgress = Fixture.Two->GetProgress();
    const auto ForeignEnemies = Fixture.EnemyHealth(Fixture.Two);

    TestFalse(TEXT("A real partial-arrival collision rejects the entire retry"),
              Fixture.Retry(Fixture.First, Previous));
    TestTrue(TEXT("Both original transforms and movement are restored"),
             Leader->GetActorTransform().Equals(LeaderTransform) && Peer->GetActorTransform().Equals(PeerTransform) &&
                 Leader->GetVelocity().Equals(LeaderVelocity) && Peer->GetVelocity().Equals(PeerVelocity) &&
                 Leader->GetCharacterMovement()->MovementMode == MOVE_Falling &&
                 Peer->GetCharacterMovement()->MovementMode == MOVE_Falling);
    TestTrue(TEXT("No checkpoint, cooldown, shield, protection or resource was committed"),
             LeaderState.Equals(FFableExpeditionCombatTestAccess::CaptureTravelState(Leader)) &&
                 PeerState.Equals(FFableExpeditionCombatTestAccess::CaptureTravelState(Peer)));
    TestTrue(TEXT("Failure retains old projectile, identity, result and live actor"),
             IsValid(Shot) && !Shot->IsActorBeingDestroyed() && Shot->GetExpeditionId() == OldId &&
                 Fixture.First.Profile->ActiveExpeditionId == OldId &&
                 Fixture.FirstPeer.Profile->ActiveExpeditionId == OldId &&
                 Fixture.Base.Mode->FindExpedition(OldId) == Fixture.One &&
                 SameReceipts(Previous, Fixture.Result(Fixture.First, OldId)) &&
                 !Fixture.Result(Fixture.First, OldId).RetryExpeditionId.IsValid());
    TestTrue(TEXT("Only the two previous expeditions remain and the foreign party is unchanged"),
             Fixture.Base.Mode->GetExpeditions().Num() == 2 &&
                 SameProgress(ForeignProgress, Fixture.Two->GetProgress()) &&
                 Fixture.HasEnemyHealth(Fixture.Two, ForeignEnemies));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableRetreatResultTest,
                                 "Fablewood.Native.Expedition.UnanimousRetreatAndResultRestore",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 진행 중에는 전원이 동의한 자기 파티만 귀환하고 같은 공개 키는 결과를 다시 읽는다. */
bool FFableRetreatResultTest::RunTest(const FString& Parameters)
{
    FResultTestWorld Fixture;
    if (!TestTrue(TEXT("Retreat fixture enters two independent expeditions"), Fixture.Initialize()))
    {
        return false;
    }
    const FGuid Id = Fixture.One->GetExpeditionId();
    const int32 Revision = Fixture.One->GetDepartureVote().Revision;
    const auto ForeignProgress = Fixture.Two->GetProgress();
    const auto ForeignEnemies = Fixture.EnemyHealth(Fixture.Two);
    TestFalse(TEXT("Foreign callers cannot consent for this ticket"),
              Fixture.One->SetAbandonConsent(Fixture.Second.Controller, Id, Revision, true));
    TestFalse(TEXT("A stale UUID cannot submit consent"),
              Fixture.One->SetAbandonConsent(Fixture.First.Controller, FGuid::NewGuid(), Revision, true));
    TestFalse(TEXT("A stale vote revision cannot submit consent"),
              Fixture.One->SetAbandonConsent(Fixture.First.Controller, Id, Revision - 1, true));
    TestTrue(TEXT("The first participant explicitly consents"),
             Fixture.One->SetAbandonConsent(Fixture.First.Controller, Id, Revision, true));
    TestTrue(TEXT("One vote leaves both players in their active expedition"),
             Fixture.First.Profile->ActiveExpeditionId == Id && Fixture.FirstPeer.Profile->ActiveExpeditionId == Id &&
                 Fixture.One->GetDepartureVote().ConsentingMemberKeys.Num() == 1);
    TestTrue(TEXT("Consent can be withdrawn without moving anybody"),
             Fixture.One->SetAbandonConsent(Fixture.First.Controller, Id, Revision, false) &&
                 Fixture.One->GetDepartureVote().ConsentingMemberKeys.IsEmpty());
    TestTrue(TEXT("The first participant consents before a rapid connection replacement"),
             Fixture.One->SetAbandonConsent(Fixture.First.Controller, Id, Revision, true));
    if (!TestTrue(TEXT("The same profile receives a different real connection without a monitor tick"),
                  Fixture.Base.ReplaceConnection(Fixture.FirstPeer)))
    {
        return false;
    }
    TestFalse(TEXT("The previous connection generation cannot consume a stored vote"),
              Fixture.One->SetAbandonConsent(Fixture.FirstPeer.Controller, Id, Revision, true));
    const int32 FreshRevision = Fixture.One->GetDepartureVote().Revision;
    TestTrue(TEXT("Connection replacement invalidates every earlier consent"),
             FreshRevision != Revision && Fixture.One->GetDepartureVote().ConsentingMemberKeys.IsEmpty());
    TestTrue(TEXT("Both current connections explicitly consent to the fresh vote"),
             Fixture.One->SetAbandonConsent(Fixture.First.Controller, Id, FreshRevision, true) &&
                 Fixture.One->SetAbandonConsent(Fixture.FirstPeer.Controller, Id, FreshRevision, true));
    const auto Saved = Fixture.Result(Fixture.First, Id);
    TestTrue(TEXT("Unanimity ends only this expedition with a fixed abandoned result"),
             Saved.IsTerminal() && Saved.Outcome == EFableExpeditionOutcome::Abandoned &&
                 !Fixture.First.Profile->ActiveExpeditionId.IsValid() &&
                 !Fixture.FirstPeer.Profile->ActiveExpeditionId.IsValid() &&
                 FableRPG::IsInRegion(EFableRegion::Village, Fixture.First.Hero->GetActorLocation()) &&
                 FableRPG::IsInRegion(EFableRegion::Village, Fixture.FirstPeer.Hero->GetActorLocation()));
    TestTrue(TEXT("The foreign party keeps its ticket, progress and exact enemies"),
             Fixture.Second.Profile->ActiveExpeditionId == Fixture.Two->GetExpeditionId() &&
                 SameProgress(ForeignProgress, Fixture.Two->GetProgress()) &&
                 Fixture.HasEnemyHealth(Fixture.Two, ForeignEnemies));

    // 실제 재인증을 주장하지 않는 메모리 fixture에서 PS 재생성 후 공개 키 소유권만 검증한다.
    const FString Key = Fixture.First.Profile->PartyPlayerKey;
    Fixture.Base.Disconnect(Fixture.First);
    FPartyTestPlayer Rejoined = Fixture.Base.AddPlayer(701, true, false);
    Rejoined.Profile->PartyPlayerKey = Key;
    Fixture.Base.Manager->RegisterPlayer(Rejoined.Controller);
    Fixture.Base.Mode->RestoreExpeditionResult(Rejoined.Profile);
    const auto Restored = Fixture.Result(Rejoined, Id);
    TestTrue(TEXT("A recreated owned profile reads its original terminal result without payment"),
             Restored.IsTerminal() && Restored.CompletionId == Saved.CompletionId && SameReceipts(Saved, Restored) &&
                 Rejoined.Profile->Gold == 0 && Rejoined.Profile->Ore == 0);
    TestTrue(TEXT("Owner-only published result contains that same completion"),
             Rejoined.Profile->GetExpeditionResult().CompletionId == Saved.CompletionId);
    TestFalse(TEXT("A retired actor's vote cannot be replayed"),
              IsValid(Fixture.One) && Fixture.One->SetAbandonConsent(Rejoined.Controller, Id, Revision, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableRetryEligibilityTest,
                                 "Fablewood.Native.Expedition.CompletedQuestRetryEligibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 퀘스트 보고 상태를 되돌리지 않고 전투 재입장 자격만 별도로 판단한다. */
bool FFableRetryEligibilityTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(801);
    FPartyTestPlayer Member = Fixture.AddPlayer(802);
    if (!TestTrue(TEXT("Eligibility fixture has two owned characters"),
                  Fixture.AddMode() && Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110)) &&
                      Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }
    Leader.Profile->DungeonQuest = EFableQuestStage::Ready;
    Member.Profile->DungeonQuest = EFableQuestStage::Completed;
    const int32 LeaderCompletions = Leader.Profile->DungeonCompletions;
    const int32 MemberCompletions = Member.Profile->DungeonCompletions;
    TestTrue(TEXT("Both Ready and Completed remain eligible for another combat run"),
             Leader.Profile->IsExpeditionEligible(EFableRegion::RootRuins) &&
                 Member.Profile->IsExpeditionEligible(EFableRegion::RootRuins));
    TestTrue(TEXT("Their genuine new party admission succeeds with explicit readiness"),
             Fixture.Ready(Leader) && Fixture.Ready(Member) && Fixture.Enter(Leader));
    AFablePartyEncounter* Encounter = Fixture.Mode->GetExpeditionFor(Leader.Profile);
    if (!TestNotNull(TEXT("A new run exists without changing quest state"), Encounter))
    {
        return false;
    }
    AFableEnemy* Victim = nullptr;
    for (TActorIterator<AFableEnemy> It(Fixture.World); It; ++It)
    {
        if (It->IsAlive() && It->GetExpeditionId() == Encounter->GetExpeditionId())
        {
            Victim = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("A tracked enemy exists in this new run"), Victim))
    {
        return false;
    }
    Victim->ReceiveHit(100000, Leader.Controller);
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("Combat pays each admitted participant without issuing NPC turn-in rewards"),
             Leader.Profile->Gold == 18 && Member.Profile->Gold == 18 &&
                 Leader.Profile->DungeonQuest == EFableQuestStage::Ready &&
                 Member.Profile->DungeonQuest == EFableQuestStage::Completed &&
                 Leader.Profile->DungeonCompletions == LeaderCompletions &&
                 Member.Profile->DungeonCompletions == MemberCompletions);
    Member.Profile->ForestQuest = EFableQuestStage::Active;
    TestFalse(TEXT("The original forest prerequisite is still enforced"),
              Member.Profile->IsExpeditionEligible(EFableRegion::RootRuins));
    Leader.Profile->DungeonCompletions = 0;
    TestFalse(TEXT("Moonwell keeps its completed root-dungeon prerequisite"),
              Leader.Profile->IsExpeditionEligible(EFableRegion::Moonwell));
    return true;
}

#endif
