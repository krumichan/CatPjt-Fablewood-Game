#include "FableCharacter.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePartyManager.h"
#include "FablePlayerState.h"
#include "FablePlayerController.h"
#include "FableTownStreamingSubsystem.h"
#include "FableProjectile.h"
#include "FableTownManager.h"
#include "FableTownTravelCoordinator.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/EngineBaseTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

/** 저장·네트워크 없는 메모리 fixture에서만 내부 경계를 관찰한다. */
struct FFableTownTravelTestAccess
{
    /** 실제 클라이언트 지형 획득과 서버 ACK 경로를 메모리 월드에서 시작한다. */
    static void StartLocalGeometry(AFablePlayerController* PC)
    {
        PC->bTownAutoAckEligible = true;
        PC->TryAutoAcknowledgeTownGeometry();
    }

    /** 실제 controller poll을 호출한다. 저장소나 네트워크 시간을 대체하지 않는다. */
    static void PollArrival(AFablePlayerController* PC)
    {
        PC->TickTownTravel();
    }

    /** 로컬 lease와 잠금의 실제 상태만 관찰한다. */
    static bool IsWaitingWithSource(const AFablePlayerController* PC)
    {
        const auto* Streaming = PC->GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
        return Streaming && Streaming->IsGeometryReadyVisible(PC->TownSourceLease) &&
               Streaming->IsGeometryReadyVisible(PC->TownTargetLease) && PC->bTownTravelUiLocked &&
               PC->bTownMoveInputHeld && PC->IsMoveInputIgnored();
    }

    /** 실제 확인 응답 뒤 모든 임시 lease와 입력 잠금이 반환됐는지 관찰한다. */
    static bool IsFullyReleased(const AFablePlayerController* PC)
    {
        const auto* Streaming = PC->GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
        return Streaming && !PC->TownSourceLease.IsValid() && !PC->TownTargetLease.IsValid() &&
               Streaming->IsGeometryReadyVisible(PC->TownPresenceLease) &&
               !PC->TownAwaitingArrival.TransactionId.IsValid() && !PC->bTownTravelUiLocked &&
               !PC->bTownMoveInputHeld && !PC->IsMoveInputIgnored();
    }

    /** 실제 도착 실패 플래그를 관찰한다. */
    static bool ArrivalFailed(const AFablePlayerController* PC)
    {
        return PC->bTownArrivalFailed;
    }

    /** 원래 물리 deadline 안에 실제 세 표본을 확인했는지 관찰한다. */
    static bool PhysicalArrivalObserved(const AFablePlayerController* PC)
    {
        return PC->bTownPhysicalArrivalObserved;
    }

    /** 실제 commit에 대응하는 ACK callback 입력만 합성한다. DB 성공 증거는 아니다. */
    static FFableTownAnchorSaveWitness DelayCommittedSave(AFablePlayerState* P)
    {
        FFableTownAnchorSaveWitness W;
        W.TravelTransactionId = P->TownAnchorSaveStatus.TravelTransactionId;
        W.AnchorSequence = P->TownAnchorSaveStatus.AnchorSequence;
        W.Anchor = P->TownAnchorSaveStatus.Anchor;
        W.StablePlayerId = TEXT("native-delayed-save");
        W.SnapshotHash = FString::ChrN(64, 'b');
        W.SentExpectedRevision = 7;
        const auto& C = P->GetTownConnectionSnapshot();
        W.OwnerConnectionGeneration = C.OwnerConnectionGeneration;
        W.ConnectionNonce = C.ConnectionNonce;
        W.WorldEpoch = C.WorldEpoch;
        P->BackendPlayerId = W.StablePlayerId;
        P->RegisteredTownSnapshotHash = W.SnapshotHash;
        P->TownAnchorSaveStatus.State = EFableTownAnchorSaveState::Sending;
        return W;
    }

    /** 새 login 순서만 재현하며 backend ACK나 파일 저장을 주장하지 않는 memory profile이다. */
    static void ReadyMemoryRestore(AFablePlayerState* P)
    {
        P->LastSafeTownAnchor.TownId = EFableTownId::LegacyVillage;
        P->LastSafeTownAnchor.AnchorId = TEXT("Entry");
        P->LastSafeTownAnchor.CatalogVersion = 1;
        P->LoadedProfileVersion = 10;
        P->bLastSaveSucceeded = true;
        P->bAwaitingProfileRestore = true;
        P->ProfileRestoreError = EFableTownTravelError::None;
    }

    /** 실제 파일 변경 없이 알려진 미준비 V10 참조를 메모리 profile에 설정한다. */
    static void SetKnownUnreadyAnchor(AFablePlayerState* P)
    {
        P->LastSafeTownAnchor.TownId = EFableTownId::Sunbrook;
        P->LastSafeTownAnchor.AnchorId = TEXT("Entry");
        P->LastSafeTownAnchor.CatalogVersion = 1;
        P->LoadedProfileVersion = 10;
    }

    /** 실제 사용자 파일이나 HTTP 요청을 만들지 않는다. */
    static void MemoryProfile(AFablePlayerState* Profile)
    {
        Profile->bPersistenceEnabled = false;
        Profile->ProfileSlot.Empty();
        Profile->bStorageBlocked = false;
        Profile->bProfileLoaded = true;
    }

    /** HTTP 없이 현재 연결의 등록된 정확 snapshot witness를 설정한다. */
    static FFableTownAnchorSaveWitness BindWitness(AFablePlayerState* P)
    {
        FFableTownAnchorSaveWitness W;
        W.TravelTransactionId = FGuid::NewGuid();
        W.AnchorSequence = 3;
        W.Anchor = P->GetTownConnectionSnapshot().SafeAnchor;
        if (!W.Anchor.IsValid())
        {
            W.Anchor.TownId = EFableTownId::LegacyVillage;
            W.Anchor.AnchorId = TEXT("Entry");
            W.Anchor.CatalogVersion = 1;
        }
        W.StablePlayerId = TEXT("native-authority-player");
        W.SnapshotHash = FString::ChrN(64, 'a');
        W.SentExpectedRevision = 7;
        const auto& C = P->GetTownConnectionSnapshot();
        W.OwnerConnectionGeneration = C.OwnerConnectionGeneration;
        W.ConnectionNonce = C.ConnectionNonce;
        W.WorldEpoch = C.WorldEpoch;
        P->BackendPlayerId = W.StablePlayerId;
        P->RegisteredTownSnapshotHash = W.SnapshotHash;
        P->TownAnchorSaveStatus.TravelTransactionId = W.TravelTransactionId;
        P->TownAnchorSaveStatus.AnchorSequence = W.AnchorSequence;
        P->TownAnchorSaveStatus.Anchor = W.Anchor;
        P->TownAnchorSaveStatus.State = EFableTownAnchorSaveState::Sending;
        return W;
    }

    /** 실제 callback guard를 호출하며 DB ACK 자체를 주장하지 않는다. */
    static void Ack(AFablePlayerState* P, const FFableTownAnchorSaveWitness& W, int64 Revision, bool bExisting = false)
    {
        P->AcceptTownAnchorAcknowledgement(W, Revision, bExisting);
    }

    /** 독립 월드에서도 기존 루트 상태 하나만 연결한다. */
    static void Attach(AFableGameMode* Mode, AFableGameState* State)
    {
        Mode->GameState = State;
        Mode->GetWorld()->SetGameState(State);
    }

    /** 벽시계 대신 합성 서버 시각을 전진시켜 실제 만료 핸들러를 호출한다. */
    static void Advance(UWorld* World, UFableTownTravelCoordinator* Coordinator, float Seconds)
    {
        World->TimeSeconds += Seconds;
        Coordinator->PollTransactions();
    }

    /** 0인 기본값 비교를 피하고 실제 양수 전투 상태를 설정한다. */
    static TArray<float> SeedCombat(AFableCharacter* Hero)
    {
        const float Now = Hero->GetWorld()->GetTimeSeconds();
        Hero->Mana = 61.0f;
        Hero->NextAttackServerTime = Now + 8.0f;
        Hero->NextSkillServerTime = Now + 11.0f;
        Hero->NextSecondSkillServerTime = Now + 13.0f;
        Hero->NextThirdSkillServerTime = Now + 17.0f;
        Hero->NextDodgeServerTime = Now + 19.0f;
        Hero->ShieldAmount = 23.0f;
        Hero->ShieldEndServerTime = Now + 21.0f;
        Hero->ActionLockEndServerTime = Now + 0.15f;
        return Combat(Hero);
    }

    /** 상대 시간으로 변환하지 않고 원래 절대 deadline을 비교한다. */
    static TArray<float> Combat(const AFableCharacter* Hero)
    {
        return {Hero->Mana,
                Hero->NextAttackServerTime,
                Hero->NextSkillServerTime,
                Hero->NextSecondSkillServerTime,
                Hero->NextThirdSkillServerTime,
                Hero->NextDodgeServerTime,
                Hero->ShieldAmount,
                Hero->ShieldEndServerTime,
                Hero->ActionLockEndServerTime};
    }
};

namespace
{
    /** 실제 소유 PC·PS·pawn의 메모리 연결이다. */
    struct FTravelPlayer
    {
        APlayerController* Controller = nullptr;
        AFablePlayerState* Profile = nullptr;
        AFableCharacter* Hero = nullptr;
    };

    /** 이름만 합성한 패키지에 물리 상자를 둔다. 실제 ForestClearing 로드 증거가 아니다. */
    struct FTravelWorld
    {
        UWorld* World = nullptr;
        AFableGameState* State = nullptr;
        AFableGameMode* Mode = nullptr;
        AFablePartyManager* Parties = nullptr;
        AFableTownManager* Towns = nullptr;
        UFableTownTravelCoordinator* Travel = nullptr;
        UBoxComponent* Ground = nullptr;
        TArray<TWeakObjectPtr<APlayerController>> Controllers;

        /** 설치 엔진의 실제 physics scene과 기존 서버 정본만 구성한다. */
        explicit FTravelWorld(bool bInitializeForRpc = false)
        {
            if (!GEngine)
            {
                return;
            }

            UPackage* Package = CreatePackage(TEXT("/Game/Maps/ForestClearing"));
            const FName WorldName = MakeUniqueObjectName(Package, UWorld::StaticClass(), TEXT("TownTravelMemory"));
            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(true)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(true)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, WorldName, Package, true, ERHIFeatureLevel::Num, &Values);
            if (!World)
            {
                return;
            }

            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            State = World->SpawnActor<AFableGameState>();
            Mode = World->SpawnActor<AFableGameMode>();
            Parties = World->SpawnActor<AFablePartyManager>();
            if (State && Mode && Parties)
            {
                FFableTownTravelTestAccess::Attach(Mode, State);
                Towns = AFableTownManager::Ensure(World);
                Travel = Towns ? Towns->GetTravelCoordinator() : nullptr;
                Ground = AddBox(FVector(0, 0, -10), FVector(10000, 10000, 10), true);
                if (bInitializeForRpc)
                {
                    // 실제 RPC의 ProcessEvent는 초기화된 actor 월드에서만 실행된다. BeginPlay나 저장은 호출하지 않는다.
                    World->InitializeActorsForPlay(FURL());
                    // GameMode의 정상 PreInitializeComponents가 생성한 실제 root state를 사용한다.
                    if (auto* InitializedState = World->GetGameState<AFableGameState>())
                    {
                        AFableGameState* UninitializedState = State;
                        State = InitializedState;
                        if (UninitializedState != State)
                        {
                            UninitializedState->Destroy();
                        }
                    }
                }
            }
        }

        /** 이 fixture가 만든 컨트롤러와 월드만 해제한다. 패키지는 저장하지 않는다. */
        ~FTravelWorld()
        {
            if (!World)
            {
                return;
            }

            for (const auto& Entry : Controllers)
            {
                if (APlayerController* Controller = Entry.Get())
                {
                    World->RemoveController(Controller);
                }
            }

            World->DestroyWorld(false);
            if (GEngine)
            {
                GEngine->DestroyWorldContext(World);
            }
        }

        /** 엔진에 등록된 단순 충돌을 만든다. query 판정을 대체하지 않는다. */
        UBoxComponent* AddBox(const FVector& Location, const FVector& Extent, bool bAuthoredGround = false)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(Extent);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            if (bAuthoredGround)
            {
                // 실제 Legacy 지지 계약을 명시적으로 재현한 합성 상자다. 실제 맵 로드는 아니다.
                Box->ComponentTags.Add(TEXT("Meadow"));
            }
            Box->RegisterComponent();
            Actor->SetActorLocation(Location);
            return Box;
        }

        /** 파티 명부·실제 소유 관계를 거쳐 서버 연결 세대를 발급한다. */
        FTravelPlayer AddPlayer(int32 Id, FVector Position = FableRPG::PortalLocation(),
                                bool bLocalFableController = false)
        {
            FTravelPlayer Player;
            if (!World || !Towns || !Travel || !Parties)
            {
                return Player;
            }

            Player.Controller = bLocalFableController ? World->SpawnActor<AFablePlayerController>()
                                                      : World->SpawnActor<APlayerController>();
            if (bLocalFableController && Player.Controller)
            {
                // standalone 메모리 fixture의 로컬 PC를 엔진의 공개 API로 지정한다.
                Player.Controller->SetAsLocalPlayerController();
            }
            if (bLocalFableController && Player.Controller)
            {
                // 정상 controller 초기화가 만든 임시 PS는 제거하고 아래 메모리 profile 하나만 소유한다.
                if (auto* AutomaticProfile = Player.Controller->GetPlayerState<APlayerState>())
                {
                    Player.Controller->SetPlayerState(nullptr);
                    AutomaticProfile->Destroy();
                }
            }

            Player.Profile = World->SpawnActor<AFablePlayerState>();
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player.Hero =
                World->SpawnActor<AFableCharacter>(Position + FVector(0, 0, 110), FRotator::ZeroRotator, Parameters);
            if (!Player.Controller || !Player.Profile || !Player.Hero)
            {
                return Player;
            }

            FFableTownTravelTestAccess::MemoryProfile(Player.Profile);
            Player.Profile->SetPlayerId(Id);
            Player.Profile->SetPlayerName(FString::Printf(TEXT("TownTravelMemory%d"), Id));
            Player.Profile->SetOwner(Player.Controller);
            Player.Controller->SetPlayerState(Player.Profile);
            Player.Controller->Possess(Player.Hero);
            State->AddPlayerState(Player.Profile);
            World->AddController(Player.Controller);
            Controllers.Add(Player.Controller);
            Parties->RegisterPlayer(Player.Controller);

            const float HalfHeight = Player.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            Position.Z = HalfHeight + 2.15f;
            Player.Hero->SetActorLocation(Position);
            Player.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Towns->RegisterOrRefreshConnection(Player.Controller);
            Towns->RefreshPhysicalPresence(Player.Controller);
            return Player;
        }

        /** 각 테스트가 실제 발급된 접속·presence를 확인한 뒤 시작한다. */
        bool Valid(const FTravelPlayer& Player) const
        {
            FFableTownConnectionSnapshot Snapshot;
            return Travel && Towns && Player.Controller && Player.Profile && Player.Hero &&
                   Towns->GetConnectionSnapshot(Player.Profile, Snapshot) && Snapshot.ConnectionNonce.IsValid() &&
                   Snapshot.OwnerConnectionGeneration > 0 && Snapshot.PresenceRevision > 0;
        }

        /** 요청에는 논리 앵커와 현재 revision만 전달한다. */
        FFableTownTravelPrepared Begin(const FTravelPlayer& Player, FFableTownAnchorRef Target = Entry())
        {
            Towns->RefreshPhysicalPresence(Player.Controller);
            FFableTownConnectionSnapshot Snapshot;
            Towns->GetConnectionSnapshot(Player.Profile, Snapshot);
            Travel->BeginAnchorTravel(Player.Controller, FGuid::NewGuid(), Target, Snapshot.PresenceRevision);
            FFableTownTravelPrepared Prepared;
            Travel->GetPendingFor(Player.Controller, Prepared);
            return Prepared;
        }

        /** 정상 ACK도 실제 owning server 핸들러를 통과한다. */
        void Ack(const FTravelPlayer& Player, const FFableTownTravelPrepared& Prepared, bool bLoaded = true)
        {
            Travel->AcknowledgeGeometry(Player.Controller, FGuid::NewGuid(), Prepared.TransactionId,
                                        Prepared.Generation, Prepared.ConnectionNonce, Prepared.ChallengeNonce,
                                        bLoaded);
        }

        /** 실제 서버가 고정한 기존 마을 진입점만 사용한다. */
        static FFableTownAnchorRef Entry()
        {
            FFableTownAnchorRef Result;
            Result.TownId = EFableTownId::LegacyVillage;
            Result.AnchorId = TEXT("Entry");
            Result.CatalogVersion = 1;
            return Result;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelRejectedTargetsTest,
                                 "Fablewood.Native.TownTravel.RejectedCatalogAndDeparturePreserveSource",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 미채택·잘못된 앵커·오래된 catalog는 이동과 명부를 변경하지 않는다. */
bool FFableTownTravelRejectedTargetsTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1101);
    if (!TestTrue(TEXT("A current owned memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    const FTransform Before = Player.Hero->GetActorTransform();
    for (const EFableTownId Town :
         {EFableTownId::Sunbrook, EFableTownId::Starbough, EFableTownId::Snowbloom, EFableTownId::Eldergrove})
    {
        FFableTownAnchorRef Target = FTravelWorld::Entry();
        Target.TownId = Town;
        Fixture.Begin(Player, Target);
        FFableTownTravelResponse Response;
        TestTrue(TEXT("Every target has an actual response"),
                 Fixture.Travel->GetLastResponseFor(Player.Controller, Response));
        TestTrue(TEXT("Unadopted geometry is rejected"), Response.Error == EFableTownTravelError::DestinationNotReady);
        TestTrue(TEXT("Rejected target preserves the exact source transform"),
                 Before.Equals(Player.Hero->GetActorTransform()));
        FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);
    }

    FFableTownAnchorRef Unknown = FTravelWorld::Entry();
    Unknown.AnchorId = TEXT("Unknown.ClientAnchor");
    Fixture.Begin(Player, Unknown);
    FFableTownTravelResponse Response;
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("Unknown logical anchor fails closed"), Response.Error == EFableTownTravelError::UnknownAnchor);
    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);

    Unknown = FTravelWorld::Entry();
    Unknown.CatalogVersion = 999;
    Fixture.Begin(Player, Unknown);
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("Stale catalog has its own rejection"), Response.Error == EFableTownTravelError::StaleCatalog);
    TestTrue(TEXT("All rejected requests preserve source"), Before.Equals(Player.Hero->GetActorTransform()));

    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);
    Player.Hero->AddActorWorldOffset(FVector(900, 0, 0));
    const FTransform FarSource = Player.Hero->GetActorTransform();
    Fixture.Begin(Player);
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("An otherwise valid owner must actually be near the departure portal"),
             Response.Error == EFableTownTravelError::DepartureOutOfRange);
    TestTrue(TEXT("Out-of-range rejection cannot teleport back to a guessed source"),
             FarSource.Equals(Player.Hero->GetActorTransform()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelWrongTupleCancelTest,
                                 "Fablewood.Native.TownTravel.WrongTupleCancelAndExplicitReplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 잘못된 ACK는 pending을 보존하며 정상 취소 뒤 원래 ACK도 재실행되지 않는다. */
bool FFableTownTravelWrongTupleCancelTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1102);
    if (!TestTrue(TEXT("Current memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    const FTransform Before = Player.Hero->GetActorTransform();
    for (int32 Variant = 0; Variant < 3; ++Variant)
    {
        const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
        if (!TestTrue(TEXT("A fresh real pending transaction exists"), Prepared.TransactionId.IsValid()))
        {
            return false;
        }

        Fixture.Travel->AcknowledgeGeometry(Player.Controller, FGuid::NewGuid(), Prepared.TransactionId,
                                            Prepared.Generation + (Variant == 0 ? 1 : 0),
                                            Variant == 1 ? FGuid::NewGuid() : Prepared.ConnectionNonce,
                                            Variant == 2 ? FGuid::NewGuid() : Prepared.ChallengeNonce, true);
        FFableTownTravelResponse Response;
        Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
        const EFableTownTravelError Expected = Variant == 0   ? EFableTownTravelError::WrongGeneration
                                               : Variant == 1 ? EFableTownTravelError::WrongConnectionNonce
                                                              : EFableTownTravelError::WrongChallengeNonce;
        TestTrue(TEXT("The malformed attempt has its exact reason"), Response.Error == Expected);

        FFableTownTravelPrepared StillPending;
        TestTrue(TEXT("Malformed ACK cannot cancel the legitimate pending request"),
                 Fixture.Travel->GetPendingFor(Player.Controller, StillPending));
        TestEqual(TEXT("Malformed ACK cannot extend TTL"), StillPending.ExpiresAtServerTime,
                  Prepared.ExpiresAtServerTime);
        TestTrue(TEXT("Malformed ACK preserves transaction identity"),
                 StillPending.TransactionId == Prepared.TransactionId);

        // 정상 취소 응답 자체가 종결 사유와 정확한 시도·이동 식별자를 보존해야 한다.
        const FGuid CancelAttemptId = FGuid::NewGuid();
        Fixture.Travel->CancelAnchorTravel(Player.Controller, CancelAttemptId, Prepared.TransactionId,
                                           Prepared.Generation, Prepared.ConnectionNonce);
        Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
        TestTrue(TEXT("Owner cancel response preserves its authoritative terminal reason"),
                 Response.Operation == EFableTownTravelOperation::Cancel &&
                     Response.State == EFableTownTravelState::Cancelled &&
                     Response.Error == EFableTownTravelError::CancelledByOwner);
        TestTrue(TEXT("Owner cancel response matches the exact request and attempt"),
                 Response.RequestId == Prepared.RequestId && Response.AttemptId == CancelAttemptId &&
                     Response.TransactionId == Prepared.TransactionId && Response.Generation == Prepared.Generation);
        TestFalse(TEXT("Owner cancel removes only its pending transaction"),
                  Fixture.Travel->GetPendingFor(Player.Controller, StillPending));

        // 同じ取消要求の再送も同じ移動の取消結果を返し、待機や移動を再開しない。
        Fixture.Travel->CancelAnchorTravel(Player.Controller, CancelAttemptId, Prepared.TransactionId,
                                           Prepared.Generation, Prepared.ConnectionNonce);
        Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
        TestTrue(TEXT("Exact owner cancel replay preserves its terminal reason and identity"),
                 Response.Operation == EFableTownTravelOperation::Cancel &&
                     Response.State == EFableTownTravelState::Cancelled &&
                     Response.Error == EFableTownTravelError::CancelledByOwner &&
                     Response.AttemptId == CancelAttemptId && Response.TransactionId == Prepared.TransactionId);
        TestFalse(TEXT("Exact cancel replay cannot restore pending work"),
                  Fixture.Travel->GetPendingFor(Player.Controller, StillPending));
        TestTrue(TEXT("Exact cancel replay preserves source placement"),
                 Before.Equals(Player.Hero->GetActorTransform()));

        Fixture.Ack(Player, Prepared);
        Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
        TestTrue(TEXT("Explicit correct old ACK reaches an AlreadyCancelled tombstone"),
                 Response.Error == EFableTownTravelError::AlreadyCancelled);
        TestTrue(TEXT("Cancel and replay preserve source placement"), Before.Equals(Player.Hero->GetActorTransform()));
        FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelLoadedFalseTest,
                                 "Fablewood.Native.TownTravel.LoadFailureAndPrivateReservationPreempt",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 올바른 loaded=false와 private 예약은 준비된 마을 이동을 확정하지 않는다. */
bool FFableTownTravelLoadedFalseTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1103);
    if (!TestTrue(TEXT("Current memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    const FTransform Before = Player.Hero->GetActorTransform();
    const FFableTownTravelPrepared Failed = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Load-failure transaction was actually pending"), Failed.TransactionId.IsValid()))
    {
        return false;
    }
    Fixture.Ack(Player, Failed, false);
    FFableTownTravelResponse Response;
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("Correct loaded=false terminally cancels"),
             Response.Error == EFableTownTravelError::ClientLoadFailed);
    TestTrue(TEXT("Load failure never teleports"), Before.Equals(Player.Hero->GetActorTransform()));
    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);

    const FFableTownTravelPrepared Preempted = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Private preemption targets an actual pending request"), Preempted.TransactionId.IsValid()))
    {
        return false;
    }
    const FGuid Reservation = FGuid::NewGuid();
    TestTrue(TEXT("Private admission reserves the exact member"),
             Fixture.Travel->TryReservePrivateAdmission({Player.Controller}, Reservation));
    Fixture.Ack(Player, Preempted);
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("Preempted pending transaction cannot commit"), Response.State != EFableTownTravelState::Committed);
    TestTrue(TEXT("Reservation preserves actual source placement"), Before.Equals(Player.Hero->GetActorTransform()));
    Fixture.Travel->ReleasePrivateAdmission(Reservation);
    TestFalse(TEXT("Exact reservation can be released"), Fixture.Travel->IsPrivateAdmissionReserved(Player.Controller));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelGroundedCommitTest,
                                 "Fablewood.Native.TownTravel.GroundedCommitPreservesCombatAndObserver",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 물리 상자 지지에 배치하되 양수 전투 상태·기존 발사체·관찰자는 보존한다. */
bool FFableTownTravelGroundedCommitTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1104);
    const FTravelPlayer Observer = Fixture.AddPlayer(1105, FableRPG::PortalLocation() + FVector(240, 0, 0));
    if (!TestTrue(TEXT("Both current owned memory connections exist"),
                  Fixture.Valid(Player) && Fixture.Valid(Observer)))
    {
        return false;
    }

    TestTrue(TEXT("Owner joins the explicit public roster"),
             Fixture.Towns->RegisterSharedForestParticipant(Player.Controller));
    TestTrue(TEXT("Observer joins the explicit public roster"),
             Fixture.Towns->RegisterSharedForestParticipant(Observer.Controller));
    const auto CombatBefore = FFableTownTravelTestAccess::SeedCombat(Player.Hero);
    const auto ObserverCombat = FFableTownTravelTestAccess::SeedCombat(Observer.Hero);
    const FTransform ObserverBefore = Observer.Hero->GetActorTransform();
    const FTransform SourceBefore = Player.Hero->GetActorTransform();
    const FGuid Field = Fixture.Towns->GetPublicFieldScopeId();
    const int32 Gold = Player.Profile->Gold;

    FActorSpawnParameters ShotParameters;
    ShotParameters.Owner = Player.Hero;
    ShotParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableProjectile* Shot =
        Fixture.World->SpawnActor<AFableProjectile>(FVector(3000, -3000, 500), FRotator::ZeroRotator, ShotParameters);
    if (!TestNotNull(TEXT("A live pre-existing projectile exists"), Shot))
    {
        return false;
    }
    Shot->InitializeShot(FVector::ForwardVector, 12, Player.Controller, false);

    const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Untouched positive transaction was prepared"), Prepared.TransactionId.IsValid()))
    {
        return false;
    }
    Fixture.Ack(Player, Prepared);
    FFableTownTravelCommitReceipt Commit;
    if (!TestTrue(TEXT("Actual supported placement commits"),
                  Fixture.Travel->GetCommittedFor(Player.Controller, Prepared.TransactionId, Commit)))
    {
        return false;
    }

    FFableTownPlacementEvidence Placement;
    TestTrue(TEXT("The commit retains actual placement evidence"),
             Fixture.Travel->GetPlacementEvidence(Prepared.TransactionId, Placement));
    TestTrue(TEXT("Actual scaled capsule was used"),
             Placement.ActualCapsuleRadius > 0 && Placement.ActualCapsuleHalfHeight > 0);
    TestTrue(TEXT("Fresh physical floor is walkable"), Placement.FreshFloor.IsWalkableFloor());
    TestTrue(TEXT("Floor gap is within three centimetres"),
             Placement.FreshFloor.FloorDist >= 0 && Placement.FreshFloor.FloorDist <= 3.0f);
    TestTrue(TEXT("Actual capsule is clear and on ground"), Placement.bFreshCapsuleClear && Placement.bWalkingOnGround);
    TestTrue(TEXT("Owner physically moved"),
             FVector::Dist(SourceBefore.GetLocation(), Player.Hero->GetActorLocation()) > 500);
    TestTrue(TEXT("Positive cooldowns/shield/mana/action deadlines survive"),
             CombatBefore == FFableTownTravelTestAccess::Combat(Player.Hero));
    TestTrue(TEXT("A pre-existing projectile is retained"), IsValid(Shot) && !Shot->IsActorBeingDestroyed());
    TestTrue(TEXT("Observer placement remains exact"), ObserverBefore.Equals(Observer.Hero->GetActorTransform()));
    TestTrue(TEXT("Observer positive combat state remains exact"),
             ObserverCombat == FFableTownTravelTestAccess::Combat(Observer.Hero));
    TestEqual(TEXT("Travel grants or consumes no gold"), Player.Profile->Gold, Gold);
    TestFalse(TEXT("Only committed owner leaves public roster"),
              Fixture.Towns->IsSharedForestParticipant(Player.Profile));
    TestTrue(TEXT("Observer remains in the same live field"),
             Fixture.Towns->IsSharedForestParticipant(Observer.Profile, Field));

    Fixture.Ack(Player, Prepared);
    FFableTownTravelCommitReceipt Replay;
    Fixture.Travel->GetCommittedFor(Player.Controller, Prepared.TransactionId, Replay);
    TestEqual(TEXT("Duplicate ACK does not increment commit sequence"), Replay.CommitSequence, Commit.CommitSequence);
    TestEqual(TEXT("There is exactly one commit"), Replay.CommitCount, 1);

    Fixture.Travel->ConfirmOwnArrival(Player.Controller, FGuid::NewGuid(), Prepared.TransactionId, Prepared.Generation,
                                      Prepared.ConnectionNonce, Commit.CommitSequence);
    FFableTownTravelResponse Confirmation;
    Fixture.Travel->GetLastResponseFor(Player.Controller, Confirmation);
    TestTrue(TEXT("Current owning connection can confirm the actual supported server placement"),
             Confirmation.Operation == EFableTownTravelOperation::ConfirmArrival &&
                 Confirmation.Error == EFableTownTravelError::None && Confirmation.bOwnArrivalConfirmed);
    TestTrue(TEXT("Server confirmation also preserves positive combat deadlines and shield"),
             CombatBefore == FFableTownTravelTestAccess::Combat(Player.Hero));
    Fixture.Travel->GetCommittedFor(Player.Controller, Prepared.TransactionId, Replay);
    TestEqual(TEXT("Arrival confirmation cannot create a second commit"), Replay.CommitSequence, Commit.CommitSequence);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelBlockedRollbackTest,
                                 "Fablewood.Native.TownTravel.BlockedArrivalPreservesMovementAndCombat",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 모든 도착 슬롯이 막히면 이동·전투·참가 명부가 원래대로 남는다. */
bool FFableTownTravelBlockedRollbackTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1106);
    if (!TestTrue(TEXT("Current memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    Fixture.Towns->RegisterSharedForestParticipant(Player.Controller);
    const auto Combat = FFableTownTravelTestAccess::SeedCombat(Player.Hero);
    const FTransform Before = Player.Hero->GetActorTransform();
    Fixture.AddBox(FVector(120, -1080, 140), FVector(400, 300, 140));
    const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Blocked transaction was prepared"), Prepared.TransactionId.IsValid()))
    {
        return false;
    }
    Fixture.Ack(Player, Prepared);
    FFableTownTravelCommitReceipt Commit;
    TestFalse(TEXT("No occupied slot can commit"),
              Fixture.Travel->GetCommittedFor(Player.Controller, Prepared.TransactionId, Commit));
    TestTrue(TEXT("Blocked placement preserves original movement"), Before.Equals(Player.Hero->GetActorTransform()));
    TestTrue(TEXT("Blocked placement preserves positive combat state"),
             Combat == FFableTownTravelTestAccess::Combat(Player.Hero));
    TestTrue(TEXT("Failed owner remains a public participant"),
             Fixture.Towns->IsSharedForestParticipant(Player.Profile));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelExpiryAndPresenceTest,
                                 "Fablewood.Native.TownTravel.ServerExpiryAndSemanticPresence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 서버 Tick이 공간 변경을 미리 게시하고, 위치 흔들림과 만료 ACK는 엄격히 구분한다. */
bool FFableTownTravelExpiryAndPresenceTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1107);
    if (!TestTrue(TEXT("Current memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    // 실제 서버 참가 API 뒤 첫 이동 요청보다 먼저 소유자 복제 스냅샷이 갱신되어야 한다.
    const FTravelPlayer Observer = Fixture.AddPlayer(1207, FVector(200, 0, 0));
    if (!TestTrue(TEXT("Independent registered town observer exists"), Fixture.Valid(Observer)))
    {
        return false;
    }

    FFableTownConnectionSnapshot BeforeJoin;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, BeforeJoin);
    const int32 GoldBefore = Player.Profile->Gold;
    TestTrue(TEXT("Explicit public-field registration succeeds"),
             Fixture.Towns->RegisterSharedForestParticipant(Player.Controller));
    Fixture.Towns->Tick(0.1f);
    FFableTownConnectionSnapshot Published;
    TestTrue(TEXT("Periodic server observation retains current ownership"),
             Fixture.Towns->GetConnectionSnapshot(Player.Profile, Published));
    TestTrue(TEXT("Public membership is published before Begin"),
             Published.PublicArea == Fixture.Towns->GetPublicFieldKey());
    TestTrue(TEXT("Semantic membership advances the presence revision"),
             Published.PresenceRevision > BeforeJoin.PresenceRevision);
    TestEqual(TEXT("Owner replication contains the same current revision"),
              Player.Profile->GetTownConnectionSnapshot().PresenceRevision, Published.PresenceRevision);
    TestTrue(TEXT("Periodic observation does not replace the connection nonce"),
             Published.ConnectionNonce == BeforeJoin.ConnectionNonce);
    TestFalse(TEXT("Periodic observation does not enrol a town observer into public combat"),
              Fixture.Towns->IsSharedForestParticipant(Observer.Profile));
    TestEqual(TEXT("Periodic observation cannot change gold"), Player.Profile->Gold, GoldBefore);

    // 같은 층의 작은 이동은 별도의 세대를 만들지 않는다.
    FFableTownConnectionSnapshot Before;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, Before);
    Player.Hero->AddActorWorldOffset(FVector(3, 2, 0));
    Fixture.Towns->Tick(0.1f);
    FFableTownConnectionSnapshot After;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, After);
    TestEqual(TEXT("Walking jitter inside one floor is not a presence change"), After.PresenceRevision,
              Before.PresenceRevision);
    TestTrue(TEXT("Walking jitter keeps the current connection nonce"),
             After.ConnectionNonce == Before.ConnectionNonce);

    // 실제 조회 범위를 벗어나면 이전 정상 문맥을 지우고 복귀 시 새 세대를 게시한다.
    const FVector SupportedSource = Player.Hero->GetActorLocation();
    Player.Hero->AddActorWorldOffset(FVector(0, 0, 2000));
    Fixture.Towns->Tick(0.1f);
    FFableTownConnectionSnapshot Outside;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, Outside);
    TestFalse(TEXT("Periodic observation clears an invalid physical context"), Outside.PhysicalContext.IsValid());
    TestTrue(TEXT("Leaving the supported context advances its revision"),
             Outside.PresenceRevision > After.PresenceRevision);
    Player.Hero->SetActorLocation(SupportedSource);
    Fixture.Towns->Tick(0.1f);
    FFableTownConnectionSnapshot Returned;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, Returned);
    TestTrue(TEXT("Returning to a valid physical context is observed before Begin"),
             Returned.PhysicalContext.IsValid() && Returned.PresenceRevision > Outside.PresenceRevision);
    TestTrue(TEXT("Physical polling preserves explicit public membership"),
             Fixture.Towns->IsSharedForestParticipant(Player.Profile));

    // 오래된 클라이언트 세대는 계속 거절하며 최신 관찰을 게시하는 것으로만 동기화를 해결한다.
    Fixture.Travel->BeginAnchorTravel(Player.Controller, FGuid::NewGuid(), FTravelWorld::Entry(),
                                      BeforeJoin.PresenceRevision);
    FFableTownTravelResponse Stale;
    Fixture.Travel->GetLastResponseFor(Player.Controller, Stale);
    TestTrue(TEXT("An old expected presence revision still fails closed"),
             Stale.Error == EFableTownTravelError::StalePresence);
    FFableTownTravelPrepared StalePending;
    TestFalse(TEXT("A stale request cannot acquire a pending transaction"),
              Fixture.Travel->GetPendingFor(Player.Controller, StalePending));
    TestTrue(TEXT("A stale request cannot move the pawn"), SupportedSource.Equals(Player.Hero->GetActorLocation()));
    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);

    // 갱신 주기와 무관하게 원래 서버 TTL은 그대로 만료되어야 한다.
    const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Expiry transaction was prepared"), Prepared.TransactionId.IsValid()))
    {
        return false;
    }
    const FTransform Source = Player.Hero->GetActorTransform();
    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 20.1f);
    Fixture.Ack(Player, Prepared);
    FFableTownTravelResponse Response;
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("Original server TTL rejects the late correct ACK"),
             Response.Error == EFableTownTravelError::Expired);
    TestTrue(TEXT("Expired transaction preserves source placement"), Source.Equals(Player.Hero->GetActorTransform()));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelBeginReplayTest,
                                 "Fablewood.Native.TownTravel.BeginReplayCannotExtendLeaseOrChangeTarget",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Begin 재전송은 같은 결과만 재열람하며 다른 목적지를 끼워 넣지 못한다. */
bool FFableTownTravelBeginReplayTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1108);
    if (!TestTrue(TEXT("Current memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    FFableTownConnectionSnapshot Snapshot;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, Snapshot);
    const FGuid Request = FGuid::NewGuid();
    Fixture.Travel->BeginAnchorTravel(Player.Controller, Request, FTravelWorld::Entry(), Snapshot.PresenceRevision);
    FFableTownTravelPrepared Prepared;
    if (!TestTrue(TEXT("The original Begin really prepares"),
                  Fixture.Travel->GetPendingFor(Player.Controller, Prepared)))
    {
        return false;
    }
    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);
    Fixture.Travel->BeginAnchorTravel(Player.Controller, Request, FTravelWorld::Entry(), Snapshot.PresenceRevision);
    FFableTownTravelPrepared Replay;
    TestTrue(TEXT("Exact replay keeps the original pending request"),
             Fixture.Travel->GetPendingFor(Player.Controller, Replay));
    TestTrue(TEXT("Begin replay cannot allocate a second transaction"), Replay.TransactionId == Prepared.TransactionId);
    TestEqual(TEXT("Begin replay cannot extend the original TTL"), Replay.ExpiresAtServerTime,
              Prepared.ExpiresAtServerTime);
    TestEqual(TEXT("Begin replay cannot advance the travel generation"), Replay.Generation, Prepared.Generation);

    FFableTownAnchorRef Other = FTravelWorld::Entry();
    Other.AnchorId = TEXT("Return");
    Fixture.Travel->BeginAnchorTravel(Player.Controller, Request, Other, Snapshot.PresenceRevision);
    FFableTownTravelResponse Response;
    Fixture.Travel->GetLastResponseFor(Player.Controller, Response);
    TestTrue(TEXT("Reusing a correlation ID for a different target is rejected"),
             Response.Error == EFableTownTravelError::AttemptIdConflict);
    Fixture.Travel->GetPendingFor(Player.Controller, Replay);
    TestTrue(TEXT("Conflicting replay does not alter the legitimate pending target"),
             Replay.TargetAnchor.AnchorId == Prepared.TargetAnchor.AnchorId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelReplacementConnectionTest,
                                 "Fablewood.Native.TownTravel.ReplacementRejectsOldAckAndLateLogout",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 같은 캐릭터의 새 소유 연결은 이전 ACK와 늦은 Logout의 영향을 받지 않는다. */
bool FFableTownTravelReplacementConnectionTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1109);
    if (!TestTrue(TEXT("Original owned memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Old connection has an actual pending request"), Prepared.TransactionId.IsValid()))
    {
        return false;
    }
    FFableTownConnectionSnapshot Old;
    Fixture.Towns->GetConnectionSnapshot(Player.Profile, Old);
    APlayerController* Replacement = Fixture.World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("A separate replacement controller exists"), Replacement))
    {
        return false;
    }

    Fixture.World->AddController(Replacement);
    Fixture.Controllers.Add(Replacement);
    Player.Controller->UnPossess();
    Player.Profile->SetOwner(Replacement);
    Replacement->SetPlayerState(Player.Profile);
    Replacement->Possess(Player.Hero);
    Fixture.Parties->RegisterPlayer(Replacement);
    TestTrue(TEXT("Exact new owner registers a replacement generation"),
             Fixture.Towns->RegisterOrRefreshConnection(Replacement));
    Fixture.Towns->RefreshPhysicalPresence(Replacement);
    FFableTownConnectionSnapshot Current;
    if (!TestTrue(TEXT("Replacement snapshot can be observed"),
                  Fixture.Towns->GetConnectionSnapshot(Player.Profile, Current)))
    {
        return false;
    }
    TestTrue(TEXT("New owner gets a new nonce"), Current.ConnectionNonce != Old.ConnectionNonce);
    TestTrue(TEXT("New owner gets a later connection generation"),
             Current.OwnerConnectionGeneration > Old.OwnerConnectionGeneration);
    const FTransform Before = Player.Hero->GetActorTransform();

    Fixture.Ack(Player, Prepared);
    Fixture.Towns->UnregisterPlayer(Player.Controller);
    FFableTownConnectionSnapshot After;
    TestTrue(TEXT("Late old logout cannot erase the replacement"),
             Fixture.Towns->GetConnectionSnapshot(Player.Profile, After));
    TestTrue(TEXT("Late callbacks cannot rotate the replacement nonce"),
             After.ConnectionNonce == Current.ConnectionNonce);
    TestTrue(
        TEXT("The replacement retains exact authority"),
        Fixture.Towns->IsCurrentConnection(Replacement, Current.OwnerConnectionGeneration, Current.ConnectionNonce));
    TestTrue(TEXT("Old ACK cannot move the new pawn"), Before.Equals(Player.Hero->GetActorTransform()));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelReentrantCancelTest,
                                 "Fablewood.Native.TownTravel.PlacementEventCancellationCannotCommit",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 정상 동기 이벤트에서 취소되어도 바깥 배치 호출이 다시 commit하지 못한다. */
bool FFableTownTravelReentrantCancelTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1110);
    if (!TestTrue(TEXT("Current memory connection exists"), Fixture.Valid(Player)))
    {
        return false;
    }

    Fixture.Towns->RegisterSharedForestParticipant(Player.Controller);
    const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Reentrancy transaction is genuinely pending"), Prepared.TransactionId.IsValid()))
    {
        return false;
    }
    const FTransform Before = Player.Hero->GetActorTransform();
    bool bCancellationSent = false;
    const FDelegateHandle Handle = Fixture.Travel->OnAuthorityTravelEvent().AddLambda(
        [&](const FFableTownTravelEvent& Event)
        {
            if (Event.TransactionId == Prepared.TransactionId &&
                Event.Kind == EFableTownTravelEventKind::PlacementStarted)
            {
                bCancellationSent = true;
                Fixture.Travel->CancelAnchorTravel(Player.Controller, FGuid::NewGuid(), Prepared.TransactionId,
                                                   Prepared.Generation, Prepared.ConnectionNonce);
            }
        });
    Fixture.Ack(Player, Prepared);
    Fixture.Travel->OnAuthorityTravelEvent().Remove(Handle);

    FFableTownTravelCommitReceipt Commit;
    TestTrue(TEXT("The actual placement event invoked the normal cancel handler"), bCancellationSent);
    TestFalse(TEXT("Outer ACK cannot commit a transaction cancelled by its event"),
              Fixture.Travel->GetCommittedFor(Player.Controller, Prepared.TransactionId, Commit));
    TestTrue(TEXT("Cancelled placement event cannot move the pawn"), Before.Equals(Player.Hero->GetActorTransform()));
    TestTrue(TEXT("Cancelled placement event cannot unregister public participation"),
             Fixture.Towns->IsSharedForestParticipant(Player.Profile));

    FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);
    const FFableTownTravelPrepared MovedDuringEvent = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Late-departure transaction is freshly prepared"), MovedDuringEvent.TransactionId.IsValid()))
    {
        return false;
    }
    const FVector CallbackOffset(900, 0, 0);
    const FTransform ActualAfterCallback(Player.Hero->GetActorQuat(), Player.Hero->GetActorLocation() + CallbackOffset,
                                         Player.Hero->GetActorScale3D());
    bool bMovedDuringEvent = false;
    const FDelegateHandle MoveHandle = Fixture.Travel->OnAuthorityTravelEvent().AddLambda(
        [&](const FFableTownTravelEvent& Event)
        {
            if (Event.TransactionId == MovedDuringEvent.TransactionId &&
                Event.Kind == EFableTownTravelEventKind::PlacementStarted)
            {
                bMovedDuringEvent = true;
                Player.Hero->AddActorWorldOffset(CallbackOffset);
            }
        });
    Fixture.Ack(Player, MovedDuringEvent);
    Fixture.Travel->OnAuthorityTravelEvent().Remove(MoveHandle);
    TestTrue(TEXT("The actual synchronous event changed the departure position"), bMovedDuringEvent);
    TestFalse(TEXT("An owner that left the portal during its event cannot commit"),
              Fixture.Travel->GetCommittedFor(Player.Controller, MovedDuringEvent.TransactionId, Commit));
    TestTrue(TEXT("Rejection cannot restore a stale pre-event pose"),
             ActualAfterCallback.Equals(Player.Hero->GetActorTransform()));
    FFableTownTravelResponse MovedResponse;
    Fixture.Travel->GetLastResponseFor(Player.Controller, MovedResponse);
    TestTrue(TEXT("Departure eligibility is rechecked after synchronous callbacks"),
             MovedResponse.Error == EFableTownTravelError::DepartureOutOfRange);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTravelIntendedSupportTest,
                                 "Fablewood.Native.TownTravel.WalkableForeignOrWrongHeightSupportRejected",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 걸을 수 있는 표면이라도 catalog의 지지 태그·높이가 다르면 도착시키지 않는다. */
bool FFableTownTravelIntendedSupportTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const FTravelPlayer Player = Fixture.AddPlayer(1111);
    if (!TestTrue(TEXT("Current memory connection and synthetic ground exist"),
                  Fixture.Valid(Player) && Fixture.Ground))
    {
        return false;
    }

    const FTransform Source = Player.Hero->GetActorTransform();
    const auto Combat = FFableTownTravelTestAccess::SeedCombat(Player.Hero);
    const float HalfHeight = Player.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    for (int32 Variant = 0; Variant < 2; ++Variant)
    {
        Fixture.Ground->ComponentTags.Reset();
        if (Variant == 1)
        {
            Fixture.Ground->ComponentTags.Add(TEXT("Meadow"));
            Fixture.Ground->GetOwner()->SetActorLocation(FVector(0, 0, -30));
        }

        FFindFloorResult PhysicallyWalkable;
        Player.Hero->GetCharacterMovement()->FindFloor(FVector(0, -1080, HalfHeight + 2.15f), PhysicallyWalkable,
                                                       false);
        TestTrue(TEXT("Positive control: the wrong surface is physically walkable"),
                 PhysicallyWalkable.IsWalkableFloor());
        const FFableTownTravelPrepared Prepared = Fixture.Begin(Player);
        if (!TestTrue(TEXT("Wrong-support request genuinely reaches prepare"), Prepared.TransactionId.IsValid()))
        {
            return false;
        }
        Fixture.Ack(Player, Prepared);
        FFableTownTravelCommitReceipt Commit;
        TestFalse(TEXT("A physically walkable but unintended support cannot commit"),
                  Fixture.Travel->GetCommittedFor(Player.Controller, Prepared.TransactionId, Commit));
        TestTrue(TEXT("Wrong support preserves source placement"), Source.Equals(Player.Hero->GetActorTransform()));
        TestTrue(TEXT("Wrong support preserves positive combat state"),
                 Combat == FFableTownTravelTestAccess::Combat(Player.Hero));
        FFableTownTravelTestAccess::Advance(Fixture.World, Fixture.Travel, 1.0f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownAnchorWitnessTest,
                                 "Fablewood.Native.TownTravel.SentAnchorAckCannotCompleteLatestOrReplacement",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 PS callback이 다른 hash/revision/seq/연결의 ACK를 거절하는 메모리 검증. */
bool FFableTownAnchorWitnessTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    if (!TestNotNull(TEXT("Actual memory world"), Fixture.World))
    {
        return false;
    }
    const auto Owner = Fixture.AddPlayer(1301, FVector(0, -1080, 110));
    if (!TestNotNull(TEXT("Owned profile"), Owner.Profile))
    {
        return false;
    }
    Fixture.Towns->RegisterOrRefreshConnection(Owner.Controller);
    const auto Good = FFableTownTravelTestAccess::BindWitness(Owner.Profile);
    TestTrue(TEXT("Registered complete witness"), Good.IsFor(Good.StablePlayerId, Good.SnapshotHash));
    const TArray<TFunction<void(FFableTownAnchorSaveWitness&)>> Mutations = {
        [](FFableTownAnchorSaveWitness& W)
        {
            ++W.AnchorSequence;
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            W.TravelTransactionId = FGuid::NewGuid();
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            W.SnapshotHash = FString::ChrN(64, 'b');
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            ++W.OwnerConnectionGeneration;
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            W.ConnectionNonce = FGuid::NewGuid();
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            W.WorldEpoch = FGuid::NewGuid();
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            W.StablePlayerId = TEXT("foreign");
        },
        [](FFableTownAnchorSaveWitness& W)
        {
            W.Anchor.AnchorId = TEXT("Portal.Expedition");
        }};
    for (const auto& Mutate : Mutations)
    {
        auto Wrong = Good;
        Mutate(Wrong);
        FFableTownTravelTestAccess::Ack(Owner.Profile, Wrong, 8);
        TestFalse(TEXT("Wrong sent witness cannot unlock current"),
                  Owner.Profile->IsTownAnchorDurable(Good.TravelTransactionId));
    }
    FFableTownTravelTestAccess::Ack(Owner.Profile, Good, 9);
    TestFalse(TEXT("Wrong revision cannot unlock"), Owner.Profile->IsTownAnchorDurable(Good.TravelTransactionId));
    FFableTownTravelTestAccess::Ack(Owner.Profile, Good, 8);
    TestTrue(TEXT("Exact sent witness completes its own sequence"),
             Owner.Profile->IsTownAnchorDurable(Good.TravelTransactionId));
    TestEqual(TEXT("Actual callback revision recorded"), Owner.Profile->GetTownAnchorSaveStatus().AcknowledgedRevision,
              int64(8));

    FFableTownTravelTestAccess::BindWitness(Owner.Profile);
    FFableTownTravelTestAccess::Ack(Owner.Profile, Good, 8, true);
    TestFalse(TEXT("An older same hash cannot complete a different current transaction"),
              Owner.Profile->IsTownAnchorDurable(Owner.Profile->GetTownAnchorSaveStatus().TravelTransactionId));
    auto Existing = FFableTownTravelTestAccess::BindWitness(Owner.Profile);
    Existing.SentExpectedRevision = 8;
    FFableTownTravelTestAccess::Ack(Owner.Profile, Existing, 8, true);
    TestTrue(TEXT("Registered identical snapshot is honestly existing, not a new write"),
             Owner.Profile->GetTownAnchorSaveStatus().State == EFableTownAnchorSaveState::VerifiedExistingSnapshot &&
                 Owner.Profile->GetTownAnchorSaveStatus().AcknowledgedRevision == 8);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownUnavailableRestoreTest,
                                 "Fablewood.Native.TownTravel.KnownUnreadyRestoreCannotSpawnOrRewriteAnchor",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 알려진 미준비 저장 마을은 새 pawn 생성이나 Legacy 대체 이전에 명시적으로 거절한다. */
bool FFableTownUnavailableRestoreTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture;
    const auto Owner = Fixture.AddPlayer(1302);
    if (!TestTrue(TEXT("Valid owned connection positive control"), Fixture.Valid(Owner)))
    {
        return false;
    }
    FFableTownTravelTestAccess::SetKnownUnreadyAnchor(Owner.Profile);
    Owner.Controller->UnPossess();
    Owner.Hero->Destroy();
    Fixture.Mode->RestartPlayer(Owner.Controller);
    TestTrue(TEXT("Unavailable geometry never creates a replacement pawn"), Owner.Controller->GetPawn() == nullptr);
    TestTrue(TEXT("Explicit not-ready error and gameplay block"),
             Owner.Profile->IsProfileRestorePending() && Owner.Profile->IsTownPersistenceBlocked() &&
                 Owner.Profile->GetProfileRestoreError() == EFableTownTravelError::DestinationNotReady);
    TestTrue(TEXT("Stored known town remains unchanged"),
             Owner.Profile->GetLastSafeTownAnchor().TownId == EFableTownId::Sunbrook &&
                 Owner.Profile->GetLastSafeTownAnchor().AnchorId == FName(TEXT("Entry")) &&
                 Owner.Profile->GetLastSafeTownAnchor().CatalogVersion == 1);
    TestTrue(TEXT("No travel save was fabricated"),
             Owner.Profile->GetTownAnchorSaveStatus().State == EFableTownAnchorSaveState::None);
    // 実loginと同じく、pawnなしの接続を先に登録する。従来の完成済みpresence fixtureとは区別する。
    FTravelPlayer Ready;
    Ready.Controller = Fixture.World->SpawnActor<APlayerController>();
    Ready.Profile = Fixture.World->SpawnActor<AFablePlayerState>();
    if (!TestTrue(TEXT("Fresh pawnless owning connection can be constructed"), Ready.Controller && Ready.Profile))
    {
        return false;
    }
    FFableTownTravelTestAccess::MemoryProfile(Ready.Profile);
    FFableTownTravelTestAccess::ReadyMemoryRestore(Ready.Profile);
    Ready.Profile->SetPlayerId(1303);
    Ready.Profile->SetPlayerName(TEXT("NativeFreshRestore"));
    Ready.Profile->SetOwner(Ready.Controller);
    Ready.Controller->SetPlayerState(Ready.Profile);
    Fixture.State->AddPlayerState(Ready.Profile);
    Fixture.World->AddController(Ready.Controller);
    Fixture.Controllers.Add(Ready.Controller);
    Fixture.Parties->RegisterPlayer(Ready.Controller);
    FFableTownConnectionSnapshot BeforePawn;
    if (!TestTrue(TEXT("Actual pre-pawn registration has a revision but no physical town"),
                  Fixture.Towns->GetConnectionSnapshot(Ready.Profile, BeforePawn) && BeforePawn.PresenceRevision > 0 &&
                      BeforePawn.PhysicalContext.TownId == EFableTownId::Invalid &&
                      Ready.Controller->GetPawn() == nullptr))
    {
        return false;
    }

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Ready.Hero = Fixture.World->SpawnActor<AFableCharacter>(FableRPG::PortalLocation() + FVector(0, 0, 110),
                                                            FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Actual owned pawn is spawned after the connection"), Ready.Hero))
    {
        return false;
    }
    Ready.Controller->Possess(Ready.Hero);
    Ready.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);

    // RegisterOrRefresh만 다시 호출하면 Invalid의 옛revision을 전달해 내부Begin에서StalePresence가 된다.
    if (!TestTrue(TEXT("Ready login restore publishes actual holding presence before internal Begin"),
                  Fixture.Travel->BeginProfileRestore(Ready.Controller)))
    {
        return false;
    }
    FFableTownTravelPrepared Prepared;
    FFableTownConnectionSnapshot Holding;
    if (!TestTrue(TEXT("Ready restore owns a real pending geometry transaction"),
                  Fixture.Travel->GetPendingFor(Ready.Controller, Prepared) &&
                      Fixture.Towns->GetConnectionSnapshot(Ready.Profile, Holding)))
    {
        return false;
    }
    TestTrue(TEXT("Preparation uses the new physical revision rather than the pawnless one"),
             Holding.PresenceRevision > BeforePawn.PresenceRevision &&
                 Prepared.SourcePresenceRevision == Holding.PresenceRevision &&
                 Holding.PhysicalContext.TownId == EFableTownId::LegacyVillage &&
                 Holding.PhysicalContext.FloorId == FName(TEXT("Ground")));
    TestTrue(TEXT("Holding never fabricates a committed anchor or unlocks gameplay"),
             Ready.Profile->GetTownAnchorSaveStatus().State == EFableTownAnchorSaveState::None &&
                 Ready.Profile->IsProfileRestorePending() && Ready.Profile->IsTownPersistenceBlocked());

    Fixture.Ack(Ready, Prepared);
    FFableTownTravelCommitReceipt Commit;
    if (!TestTrue(TEXT("Exact ACK performs real grounded restore placement and one memory-only save"),
                  Fixture.Travel->GetCommittedFor(Ready.Controller, Prepared.TransactionId, Commit) &&
                      Ready.Profile->GetTownAnchorSaveStatus().State == EFableTownAnchorSaveState::MemoryOnly))
    {
        return false;
    }
    Fixture.Travel->ConfirmOwnArrival(Ready.Controller, FGuid::NewGuid(), Prepared.TransactionId, Prepared.Generation,
                                      Prepared.ConnectionNonce, Commit.CommitSequence);
    FFableTownTravelResponse Confirmation;
    Fixture.Travel->GetLastResponseFor(Ready.Controller, Confirmation);
    TestTrue(TEXT("Actual server confirmation completes only the ready owner's restore"),
             Confirmation.Error == EFableTownTravelError::None && Confirmation.bOwnArrivalConfirmed &&
                 !Ready.Profile->IsProfileRestorePending() && !Ready.Profile->IsTownPersistenceBlocked());
    TestTrue(TEXT("Ready positive control cannot rewrite the unavailable owner's reference"),
             Owner.Profile->GetLastSafeTownAnchor().TownId == EFableTownId::Sunbrook &&
                 Owner.Profile->GetProfileRestoreError() == EFableTownTravelError::DestinationNotReady);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownDelayedSaveArrivalTest,
                                 "Fablewood.Native.TownTravel.DelayedSaveAckKeepsBoundedPhysicalArrival",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 물리 도착과 저장 callback 대기를 구분하고 정확 ACK 뒤에만 source를 반환한다. */
bool FFableTownDelayedSaveArrivalTest::RunTest(const FString& Parameters)
{
    FTravelWorld Fixture(true);
    if (!TestTrue(TEXT("RPC fixture uses a normally initialized actor world"),
                  Fixture.World && Fixture.World->AreActorsInitialized()))
    {
        return false;
    }
    const FTravelPlayer Player = Fixture.AddPlayer(1113, FableRPG::PortalLocation(), true);
    auto* PC = Cast<AFablePlayerController>(Player.Controller);
    if (!TestTrue(TEXT("Actual local Fable controller and physical memory connection exist"),
                  Fixture.Valid(Player) && PC && PC->IsLocalController()))
    {
        return false;
    }

    Fixture.Towns->RegisterSharedForestParticipant(PC);
    const auto Prepared = Fixture.Begin(Player);
    if (!TestTrue(TEXT("Actual pending server preparation exists"), Prepared.TransactionId.IsValid()) ||
        !TestTrue(TEXT("The real ClientPrepare RPC delivered the exact owning preparation"),
                  PC->GetTownTravelPrepared().TransactionId == Prepared.TransactionId &&
                      PC->GetTownTravelPrepared().Generation == Prepared.Generation &&
                      PC->GetTownTravelPrepared().ConnectionNonce == Prepared.ConnectionNonce))
    {
        return false;
    }
    FFableTownTravelTestAccess::StartLocalGeometry(PC);
    FFableTownTravelCommitReceipt Commit;
    if (!TestTrue(TEXT("Actual client geometry ACK commits a supported server placement"),
                  Prepared.TransactionId.IsValid() &&
                      Fixture.Travel->GetCommittedFor(PC, Prepared.TransactionId, Commit)))
    {
        return false;
    }
    const auto Witness = FFableTownTravelTestAccess::DelayCommittedSave(Player.Profile);

    // 同じ実controllerを複数時刻でpollする。三つの物理標本を5秒内で得る。
    FFableTownTravelTestAccess::PollArrival(PC);
    Fixture.World->TimeSeconds += 0.11f;
    FFableTownTravelTestAccess::PollArrival(PC);
    Fixture.World->TimeSeconds += 0.22f;
    FFableTownTravelTestAccess::PollArrival(PC);
    TestTrue(TEXT("Actual grounded pawn was observed before the original deadline"),
             FFableTownTravelTestAccess::PhysicalArrivalObserved(PC));
    Fixture.World->TimeSeconds += 6.0f;
    FFableTownTravelTestAccess::PollArrival(PC);
    TestFalse(TEXT("Delayed save alone cannot permanently fail a timely physical arrival"),
              FFableTownTravelTestAccess::ArrivalFailed(PC));
    TestTrue(TEXT("Input and both actual geometry tokens remain held while save is pending"),
             FFableTownTravelTestAccess::IsWaitingWithSource(PC));

    auto Wrong = Witness;
    Wrong.TravelTransactionId = FGuid::NewGuid();
    FFableTownTravelTestAccess::Ack(Player.Profile, Wrong, 8);
    FFableTownTravelTestAccess::PollArrival(PC);
    TestTrue(TEXT("Another transaction ACK cannot release this source or input"),
             FFableTownTravelTestAccess::IsWaitingWithSource(PC));

    // 正確なproduction callback guardと、実server到着再検査/RPC応答を通す。
    FFableTownTravelTestAccess::Ack(Player.Profile, Witness, 8);
    FFableTownTravelTestAccess::PollArrival(PC);
    FFableTownTravelResponse Confirmed;
    Fixture.Travel->GetLastResponseFor(PC, Confirmed);
    TestTrue(TEXT("Late exact ACK permits the actual current server confirmation"),
             Confirmed.Operation == EFableTownTravelOperation::ConfirmArrival &&
                 Confirmed.Error == EFableTownTravelError::None && Confirmed.bOwnArrivalConfirmed);
    TestTrue(TEXT("The actual confirmation releases input and both temporary tokens, retaining presence"),
             FFableTownTravelTestAccess::IsFullyReleased(PC));

    // 別の実移動で物理到着を失えば、保存待ちを理由に元の5秒を延長しない。
    const FTravelPlayer Lost = Fixture.AddPlayer(1114, FableRPG::PortalLocation(), true);
    auto* LostPC = Cast<AFablePlayerController>(Lost.Controller);
    if (!TestTrue(TEXT("Second independent local physical connection exists"), Fixture.Valid(Lost) && LostPC))
    {
        return false;
    }
    Fixture.Towns->RegisterSharedForestParticipant(LostPC);
    const auto LostPrepared = Fixture.Begin(Lost);
    FFableTownTravelTestAccess::StartLocalGeometry(LostPC);
    if (!TestTrue(TEXT("Second physical placement really committed"),
                  Fixture.Travel->GetCommittedFor(LostPC, LostPrepared.TransactionId, Commit)))
    {
        return false;
    }
    const auto LostWitness = FFableTownTravelTestAccess::DelayCommittedSave(Lost.Profile);
    FFableTownTravelTestAccess::PollArrival(LostPC);
    Fixture.World->TimeSeconds += 0.11f;
    FFableTownTravelTestAccess::PollArrival(LostPC);
    Fixture.World->TimeSeconds += 0.22f;
    FFableTownTravelTestAccess::PollArrival(LostPC);
    Lost.Hero->AddActorWorldOffset(FVector(100, 0, 0));
    Fixture.World->TimeSeconds += 6.0f;
    FFableTownTravelTestAccess::PollArrival(LostPC);
    TestTrue(TEXT("Loss of exact physical arrival retains the original bounded deadline"),
             FFableTownTravelTestAccess::ArrivalFailed(LostPC));
    FFableTownTravelTestAccess::Ack(Lost.Profile, LostWitness, 8);
    FFableTownTravelTestAccess::PollArrival(LostPC);
    TestTrue(TEXT("An ACK alone cannot unlock a physically unconfirmed arrival"),
             FFableTownTravelTestAccess::IsWaitingWithSource(LostPC));
    return true;
}

#endif
