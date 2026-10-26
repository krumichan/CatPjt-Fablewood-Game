#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableEnemyBolt.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePartyEncounter.h"
#include "FablePartyManager.h"
#include "FablePlayerState.h"
#include "FableProjectile.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include <limits>

/** 파일·계정·네트워크 없이 실제 서버 핸들러의 경계만 호출한다. */
struct FFableTownTestAccess
{
    /** 메모리 fixture의 보상이 실제 사용자 저장으로 이어지지 않게 한다. */
    static void UseMemoryOnly(AFablePlayerState* Profile)
    {
        Profile->bPersistenceEnabled = false;
        Profile->ProfileSlot.Empty();
        Profile->bStorageBlocked = false;
    }

    /** 독립 월드에서도 게임 모드와 루트 상태를 같은 객체로 연결한다. */
    static void AttachState(AFableGameMode* Mode, AFableGameState* State)
    {
        Mode->GameState = State;
        Mode->GetWorld()->SetGameState(State);
    }

    /** Native 메모리 명부만 활성화하며 실제 레벨·충돌·접지 로드 성공을 뜻하지 않는다. */
    static bool EnableSyntheticTown(AFableTownManager* Manager, EFableTownId TownId)
    {
        auto* Catalog = Manager ? const_cast<UFableTownCatalog*>(Manager->GetCatalog()) : nullptr;
        if (!Catalog)
        {
            return false;
        }
        auto* Definition = Catalog->Definitions.FindByPredicate(
            [TownId](const FFableTownDefinition& Value)
            {
                return Value.TownId == TownId;
            });
        if (!Definition)
        {
            return false;
        }
        Definition->bGeometryReady = true;
        FString Error;
        return Catalog->Validate(Error);
    }

    /** 실제 등록 참가자 조회를 관찰한다. */
    static TArray<APlayerController*> FieldPlayers(const AFableGameMode* Mode)
    {
        return Mode->GetEncounterPlayers();
    }

    /** 생성·처치 등록도 실제 공유 숲 핸들러를 사용한다. */
    static void SpawnForest(AFableGameMode* Mode)
    {
        Mode->SpawnEncounter();
    }

    /** 실제 생존 판정과 구조 시작 경로를 호출한다. */
    static void Monitor(AFableGameMode* Mode)
    {
        Mode->CheckPartyState();
    }

    /** 벽시계 대기만 생략하고 실제 구조 완료 경로를 다시 호출한다. */
    static void CompleteRecovery(AFableGameMode* Mode, AFableGameState* State)
    {
        State->FieldProgress.PartyRecoveryEndServerTime = 0;
        Mode->NextRecoveryAttemptTime = 0;
        Mode->CheckPartyState();
    }

    /** 숲 구조 재시도와 개별 구조의 내부 예약 시각도 관찰만 한다. */
    static TArray<double> FieldRecoveryTimers(const AFableGameMode* Mode)
    {
        return {Mode->NextRecoveryAttemptTime, Mode->NextIndividualRescueTime};
    }

    /** 실제 전투 정리의 영향 범위를 검사한다. */
    static void ClearForest(AFableGameMode* Mode)
    {
        Mode->ClearRecoveryCombat();
    }
};

namespace
{
    /** 현재 서버가 소유하고 등록한 메모리 캐릭터를 묶는다. */
    struct FTownTestPlayer
    {
        APlayerController* Controller = nullptr;
        AFablePlayerState* Profile = nullptr;
        AFableCharacter* Hero = nullptr;

        /** 실제 pawn의 플레이어 상태 소유권도 확인한다. */
        bool IsValid() const
        {
            return ::IsValid(Controller) && ::IsValid(Profile) && ::IsValid(Hero) &&
                   Controller->GetPlayerState<AFablePlayerState>() == Profile &&
                   Hero->GetPlayerState<AFablePlayerState>() == Profile && Profile->GetOwner() == Controller;
        }
    };

    /** 메모리 전용 물리 월드는 충돌 경로만 제공하며 실제 지형 접지를 주장하지 않는다. */
    struct FTownTestWorld
    {
        UWorld* World = nullptr;
        AFableGameState* State = nullptr;
        AFableGameMode* Mode = nullptr;
        AFablePartyManager* Parties = nullptr;
        AFableTownManager* Towns = nullptr;
        TArray<TWeakObjectPtr<APlayerController>> Controllers;
        bool bOwnsContext = false;

        /** 네트워크·저장·실제 시간 진행 없이 actor query와 충돌을 준비한다. */
        FTownTestWorld()
        {
            if (!GEngine)
            {
                return;
            }

            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(true)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(true)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            if (!World)
            {
                return;
            }

            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            bOwnsContext = true;
            State = World->SpawnActor<AFableGameState>();
            Mode = World->SpawnActor<AFableGameMode>();
            Parties = World->SpawnActor<AFablePartyManager>();
            if (State && Mode)
            {
                FFableTownTestAccess::AttachState(Mode, State);
                Towns = AFableTownManager::Ensure(World);

                // 실제 Legacy 서비스의 지원 바닥만 합성한다. 후보 지형은 생성하지 않는다. / Legacyの支持床だけを合成する。
                AActor* GroundActor = World->SpawnActor<AActor>();
                UBoxComponent* Ground = NewObject<UBoxComponent>(GroundActor);
                GroundActor->SetRootComponent(Ground);
                Ground->SetBoxExtent(FVector(3000, 3000, 10));
                Ground->SetCollisionObjectType(ECC_WorldStatic);
                Ground->SetCollisionResponseToAllChannels(ECR_Block);
                Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                Ground->ComponentTags.Add(TEXT("Meadow"));
                Ground->RegisterComponent();
                GroundActor->SetActorLocation(FVector(0, 0, -10));
            }
        }

        /** 소유한 연결을 엔진 목록에서 제거한 뒤 fixture 월드를 해제한다. */
        ~FTownTestWorld()
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
            if (bOwnsContext && GEngine)
            {
                GEngine->DestroyWorldContext(World);
            }
        }

        /** 직접 연결한 owner·PS·pawn을 실제 파티 registry에 등록한다. */
        FTownTestPlayer AddPlayer(int32 Id, FVector Position)
        {
            FTownTestPlayer Player;
            if (!World || !State || !Parties || !Mode)
            {
                return Player;
            }

            Player.Controller = World->SpawnActor<APlayerController>();
            Player.Profile = World->SpawnActor<AFablePlayerState>();
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player.Hero = World->SpawnActor<AFableCharacter>(Position, FRotator::ZeroRotator, Parameters);
            if (!Player.Controller || !Player.Profile || !Player.Hero)
            {
                return Player;
            }

            FFableTownTestAccess::UseMemoryOnly(Player.Profile);
            Player.Profile->SetPlayerId(Id);
            Player.Profile->SetPlayerName(FString::Printf(TEXT("TownMemory%d"), Id));
            Player.Profile->bProfileLoaded = true;
            Player.Profile->SetOwner(Player.Controller);
            Player.Controller->SetPlayerState(Player.Profile);
            Player.Controller->Possess(Player.Hero);
            State->AddPlayerState(Player.Profile);
            World->AddController(Player.Controller);
            Controllers.Add(Player.Controller);
            Parties->RegisterPlayer(Player.Controller);

            // Legacy 양성 대조만 실제 capsule 높이로 접지한다. 합성 후보 문맥은 그대로 둔다. / Legacy陽性対照だけを接地する。
            FFableTownLocationContext Physical;
            if (Towns && Towns->ResolvePhysicalContext(Player.Controller, Physical) &&
                Physical.TownId == EFableTownId::LegacyVillage)
            {
                Position.Z = Player.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15f;
                Player.Hero->SetActorLocation(Position);
                Player.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            }
            if (Towns)
            {
                Towns->RegisterOrRefreshConnection(Player.Controller);
                Towns->RefreshPhysicalPresence(Player.Controller);
            }
            return Player;
        }

        /** 합성 Sunbrook 관찰자와 기존 게시판의 실제 숲 참가자를 분리한다. */
        bool InitializePublic(FTownTestPlayer& Participant, FTownTestPlayer& Observer)
        {
            if (!Towns || !FFableTownTestAccess::EnableSyntheticTown(Towns, EFableTownId::Sunbrook))
            {
                return false;
            }
            FVector TownPosition;
            if (!Towns->GetCatalog()->ToWorld(EFableTownId::Sunbrook, FVector(0, -17000, 104), TownPosition))
            {
                return false;
            }
            Participant = AddPlayer(901, FableRPG::BoardLocation() + FVector(-100, -80, 110));
            Observer = AddPlayer(902, TownPosition);
            if (!Participant.IsValid() || !Observer.IsValid())
            {
                return false;
            }
            Participant.Profile->ForestQuest = EFableQuestStage::Active;
            Observer.Profile->ForestQuest = EFableQuestStage::Active;
            if (!Towns->RegisterValidatedPresence(Observer.Controller) ||
                !Mode->JoinPublicField(Participant.Controller))
            {
                return false;
            }
            FFableTownTestAccess::SpawnForest(Mode);
            return Mode->GetPublicFieldScopeId().IsValid() && Towns->IsSharedForestParticipant(Participant.Profile) &&
                   !Towns->IsSharedForestParticipant(Observer.Profile);
        }

        /** 검증 대상은 실제 살아 있는 공유 숲의 적만 선택한다. */
        AFableEnemy* ForestEnemy() const
        {
            for (TActorIterator<AFableEnemy> It(World); It; ++It)
            {
                if (It->IsAlive() && !It->GetExpeditionId().IsValid())
                {
                    return *It;
                }
            }
            return nullptr;
        }

        /** 실제 초기화 API가 발사 순간의 문맥을 캡처하도록 한다. */
        AFableProjectile* SpawnShot(const FTownTestPlayer& Player, FVector Position, FVector Direction)
        {
            FActorSpawnParameters Parameters;
            Parameters.Owner = Player.Hero;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Shot = World->SpawnActor<AFableProjectile>(Position, Direction.Rotation(), Parameters);
            if (Shot)
            {
                Shot->InitializeShot(Direction, 12, Player.Controller, false);
            }
            return Shot;
        }
    };

    /** 다른 공간 캐릭터의 위치·자원·보상·안내가 바뀌지 않는지 검사한다. */
    struct FTownPlayerSnapshot
    {
        FTransform Transform;
        float Health = 0;
        float Mana = 0;
        TArray<int32> Counters;
        FString Notice;
        TArray<FFableItem> Items;

        /** 실제 관측 값만 보관하고 새로운 보상이나 저장을 만들지 않는다. */
        explicit FTownPlayerSnapshot(const FTownTestPlayer& Player)
        {
            Transform = Player.Hero->GetActorTransform();
            Health = Player.Hero->GetHealth();
            Mana = Player.Hero->GetMana();
            const auto* Profile = Player.Profile;
            Counters = {Profile->Gold,        Profile->Ore,           Profile->Experience,  Profile->QuestKills,
                        Profile->Level,       Profile->HealthPotions, Profile->ManaPotions, Profile->DungeonCompletions,
                        Profile->NoticeSerial};
            Notice = Profile->LastNotice;
            Items = Profile->Inventory;
        }

        /** 아이템은 인스턴스·품질·내구도·옵션까지 반사 필드 전체를 비교한다. */
        bool Equals(const FTownTestPlayer& Player) const
        {
            const FTownPlayerSnapshot Current(Player);
            if (!Transform.Equals(Current.Transform) || Health != Current.Health || Mana != Current.Mana ||
                Counters != Current.Counters || Notice != Current.Notice || Items.Num() != Current.Items.Num())
            {
                return false;
            }
            for (int32 Index = 0; Index < Items.Num(); ++Index)
            {
                if (!FFableItem::StaticStruct()->CompareScriptStruct(&Items[Index], &Current.Items[Index], 0))
                {
                    return false;
                }
            }
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownCatalogTest,
                                 "Fablewood.Native.Town.FiniteCatalogCoordinatesAndVerticalContext",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 순수 조회는 후보 좌표를 검사할 뿐 새 지형이나 안전한 보행을 승인하지 않는다. */
bool FFableTownCatalogTest::RunTest(const FString& Parameters)
{
    UFableTownCatalog* Catalog = NewObject<UFableTownCatalog>();
    if (!TestNotNull(TEXT("A pure catalog exists without loading content"), Catalog))
    {
        return false;
    }
    FString Error;
    TestTrue(TEXT("Every finite definition and town transform validates"), Catalog->Validate(Error));
    TestNull(TEXT("An unknown town is never interpreted as the old village"),
             Catalog->FindTown(static_cast<EFableTownId>(255)));
    TestNull(TEXT("An invalid town has no implicit default definition"), Catalog->FindTown(EFableTownId::Invalid));

    // 새 후보의 좌표를 볼 수 있어도 runtime 지형/앵커 사용 권한은 부여하지 않는다.
    const EFableTownId Candidates[] = {EFableTownId::Sunbrook, EFableTownId::Starbough, EFableTownId::Snowbloom,
                                       EFableTownId::Eldergrove};
    const FVector ExpectedOrigins[] = {FVector(0, -60000, 0), FVector(80000, -60000, 0), FVector(0, -140000, 0),
                                       FVector(80000, -140000, 0)};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Candidates); ++Index)
    {
        const auto* Town = Catalog->FindTown(Candidates[Index]);
        if (!TestNotNull(TEXT("Each declared candidate is present"), Town))
        {
            return false;
        }
        TestFalse(TEXT("An unadopted candidate does not claim ready geometry"),
                  Catalog->IsGeometryReady(Candidates[Index]));
        const FVector Local(123, -456, 104);
        FVector World, RoundTrip;
        TestTrue(TEXT("Local coordinates convert exactly once to the declared fixed origin"),
                 Catalog->ToWorld(Candidates[Index], Local, World) && World.Equals(ExpectedOrigins[Index] + Local));
        TestTrue(TEXT("The inverse restores the same local position"),
                 Catalog->ToLocal(Candidates[Index], World, RoundTrip) && RoundTrip.Equals(Local));
        FFableTownLocationContext Context;
        TestTrue(TEXT("Read-only candidate lookup retains an explicit unready context"),
                 Catalog->ResolveLocation(World, Context, false) && Context.TownId == Candidates[Index] &&
                     !Context.bGeometryReady);
        TestFalse(TEXT("The same location is refused as runtime-ready geometry"),
                  Catalog->ResolveLocation(World, Context, true));
        FFableTownAnchorRef Entry;
        Entry.TownId = Candidates[Index];
        Entry.AnchorId = FName(TEXT("Entry"));
        Entry.CatalogVersion = Town->CatalogVersion;
        FTransform Arrival;
        TestFalse(TEXT("An unready candidate cannot resolve a usable arrival anchor"),
                  Catalog->ResolveAnchor(Entry, 0, Arrival));
    }

    // 유효하지 않은 실수와 마을 밖 좌표를 기존 반평면으로 받아들이지 않는다.
    FFableTownLocationContext Outside;
    TestFalse(TEXT("An arbitrary distant point is outside all finite towns"),
              Catalog->ResolveLocation(FVector(-1e12, -1e12, 104), Outside, false));
    TestFalse(TEXT("Infinite coordinates are rejected"),
              Catalog->ResolveLocation(FVector(std::numeric_limits<double>::infinity(), 0, 104), Outside, false));
    TestFalse(TEXT("NaN coordinates are rejected"),
              Catalog->ResolveLocation(FVector(0, 0, std::numeric_limits<double>::quiet_NaN()), Outside, false));
    FVector Unused;
    TestFalse(TEXT("Unknown identifiers cannot convert coordinates"),
              Catalog->ToWorld(static_cast<EFableTownId>(255), FVector::ZeroVector, Unused));

    const auto* Elf = Catalog->FindTown(EFableTownId::Eldergrove);
    if (!TestNotNull(TEXT("Elf has an explicit finite definition"), Elf))
    {
        return false;
    }
    for (int32 Floor = 0; Floor < 4; ++Floor)
    {
        const FVector Local(3700, 0, Floor * 3000 + 104);
        FVector World;
        FFableTownLocationContext Context;
        TestTrue(TEXT("Every Elf deck has its own local floor identity"),
                 Catalog->ToWorld(EFableTownId::Eldergrove, Local, World) &&
                     Catalog->ResolveLocation(World, Context, false) &&
                     Context.FloorId == FName(*FString::Printf(TEXT("F%d"), Floor)) && Context.ConnectorId.IsNone());
    }

    // 실제 미채택 경사로 대신 명시적인 메모리 선분으로 유한 3D 연결 조회만 검증한다.
    FFableTownDefinition Synthetic = *Elf;
    FFableTownConnectorDefinition Connector;
    Connector.ConnectorId = FName(TEXT("MemorySlopeF0F1"));
    Connector.LocalPoints = {FVector(0, 0, 104), FVector(0, 2000, 3104)};
    Connector.RadiusCm = 100;
    Synthetic.Connectors.Add(Connector);
    TestTrue(TEXT("A finite synthetic connector definition validates"),
             UFableTownCatalog::ValidateDefinition(Synthetic, Error));
    const FVector SlopeMiddle = Synthetic.LocalToWorld.TransformPosition(FVector(0, 1000, 1604));
    FFableTownLocationContext SlopeContext;
    TestTrue(TEXT("A point between floors resolves through the explicit 3D connector"),
             UFableTownCatalog::ResolveDefinitionLocation(Synthetic, SlopeMiddle, SlopeContext, false) &&
                 SlopeContext.ConnectorId == Connector.ConnectorId && SlopeContext.FloorId.IsNone());
    TestFalse(
        TEXT("An unregistered gap between floors never becomes a floor by height alone"),
        UFableTownCatalog::ResolveDefinitionLocation(
            Synthetic, Synthetic.LocalToWorld.TransformPosition(FVector(10000, 1000, 1604)), SlopeContext, false));
    FFableTownDefinition Corrupt = Synthetic;
    Corrupt.LocalToWorld.SetScale3D(FVector(2));
    TestFalse(TEXT("A scale change cannot silently change movement and capsule dimensions"),
              UFableTownCatalog::ValidateDefinition(Corrupt, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownFieldIsolationTest,
                                 "Fablewood.Native.Town.PublicFieldParticipantsRewardsAndCleanup",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 다른 마을의 의뢰 자격이 같아도 등록되지 않은 캐릭터는 필드 보상·정리에서 제외한다. */
bool FFableTownFieldIsolationTest::RunTest(const FString& Parameters)
{
    FTownTestWorld Fixture;
    FTownTestPlayer Participant, Observer;
    if (!TestTrue(TEXT("Real owned controllers register only the intended forest participant"),
                  Fixture.InitializePublic(Participant, Observer)))
    {
        return false;
    }
    const FTownPlayerSnapshot Before(Observer);
    const auto Field = FableAdventure::For(Participant.Profile);
    const auto Town = FableAdventure::For(Observer.Profile);
    TestTrue(TEXT("Field Players contains only its explicitly registered profile"),
             Field && !Field.IsIdleTown() && Field.Players().Num() == 1 &&
                 Field.Players().Contains(Participant.Profile) && !Field.Players().Contains(Observer.Profile));
    TestTrue(TEXT("The other town has an independent idle view and no combat key"),
             Town && Town.IsIdleTown() && !Town.PublicArea().IsValid() &&
                 Town.PhysicalTown().TownId == EFableTownId::Sunbrook && !Town.Players().Contains(Participant.Profile));
    const auto Registered = FFableTownTestAccess::FieldPlayers(Fixture.Mode);
    TestTrue(TEXT("Actual game-mode participant enumeration excludes the other town"),
             Registered.Num() == 1 && Registered.Contains(Participant.Controller) &&
                 !Registered.Contains(Observer.Controller));
    TestFalse(TEXT("Different physical spaces never share a combat scope"),
              FableAdventure::SameScope(Participant.Hero, Observer.Hero));
    TestFalse(TEXT("Field damage cannot affect an idle-town character"),
              FableAdventure::CanAffect(Participant.Hero, Observer.Hero));

    AFableEnemy* Victim = Fixture.ForestEnemy();
    if (!TestNotNull(TEXT("The actual forest handler spawned a tracked enemy"), Victim))
    {
        return false;
    }
    const int32 Gold = Participant.Profile->Gold;
    const int32 Experience = Participant.Profile->Experience;
    Victim->ReceiveHit(100000, Participant.Controller);
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("The registered participant receives the actual forest kill"),
             !Victim->IsAlive() && Participant.Profile->QuestKills == 1 && Participant.Profile->Gold > Gold &&
                 Participant.Profile->Experience > Experience);
    TestTrue(TEXT("The eligible but unregistered town observer receives no reward or notice"), Before.Equals(Observer));
    TestEqual(TEXT("The actual field progress advances once"), Fixture.State->FieldProgress.DefeatedEnemies, 1);
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestEqual(TEXT("A repeated callback cannot advance progress again"), Fixture.State->FieldProgress.DefeatedEnemies,
              1);

    FFableTownTestAccess::ClearForest(Fixture.Mode);
    TestNull(TEXT("Actual cleanup removes the remaining tracked forest enemies"), Fixture.ForestEnemy());
    TestTrue(TEXT("Field cleanup preserves the other town's position, resources and full equipment"),
             Before.Equals(Observer));
    TestTrue(TEXT("Town idle progress never inherits the root forest stage"),
             FableAdventure::For(Observer.Profile)->AdventureStage == EFableAdventureStage::Village);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRecoveryTest,
                                 "Fablewood.Native.Town.PublicFieldRecoveryExcludesTownObservers",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 다른 마을의 생존자는 숲 전멸을 막지 않고 그 숲의 구조 이동에도 포함되지 않는다. */
bool FFableTownRecoveryTest::RunTest(const FString& Parameters)
{
    FTownTestWorld Fixture;
    FTownTestPlayer Participant, Observer;
    if (!TestTrue(TEXT("Recovery has one real forest participant and a separate synthetic town registry"),
                  Fixture.InitializePublic(Participant, Observer)))
    {
        return false;
    }
    const FTownPlayerSnapshot Before(Observer);
    AFableEnemy* Source = Fixture.ForestEnemy();
    if (!TestNotNull(TEXT("The hit source belongs to the actual forest generation"), Source))
    {
        return false;
    }
    Participant.Hero->ReceiveHit(100000, Source);
    TestTrue(TEXT("The registered participant is actually downed"), Participant.Hero->IsDowned());
    FFableTownTestAccess::Monitor(Fixture.Mode);
    TestTrue(TEXT("An alive outsider does not suppress the field's own wipe recovery"),
             Fixture.State->FieldProgress.bPartyRecoveryPending);
    const auto ObserverView = FableAdventure::For(Observer.Profile);
    TestTrue(TEXT("The other town has neither a recovery countdown nor field lock"),
             ObserverView && ObserverView.IsIdleTown() && !ObserverView->bPartyRecoveryPending &&
                 ObserverView.RecoveryRemaining() == 0);
    TestTrue(TEXT("Starting recovery changes nothing in the other town"), Before.Equals(Observer));

    // 실제 경과 시간을 주장하지 않고 기존 완료 핸들러의 이동 대상만 검사한다.
    FFableTownTestAccess::CompleteRecovery(Fixture.Mode, Fixture.State);
    TestTrue(TEXT("Actual recovery returns and restores its own participant"),
             Participant.Hero->IsAlive() && !Fixture.State->FieldProgress.bPartyRecoveryPending &&
                 Fixture.State->FieldProgress.PartyRecoveryCount == 1);
    TestTrue(TEXT("The other town never moves or spends resources during forest return"), Before.Equals(Observer));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownIdleVoidRecoveryTest,
                                 "Fablewood.Native.Town.IdleTownVoidRecoveryPreservesActiveForest",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 마을의 실제 낙하 패배는 본인 체크포인트에서만 복구하고 진행 중인 숲을 바꾸지 않는다. */
bool FFableTownIdleVoidRecoveryTest::RunTest(const FString& Parameters)
{
    FTownTestWorld Fixture;
    if (!TestNotNull(TEXT("The actual town registry exists in the memory world"), Fixture.Towns))
    {
        return false;
    }

    // 이미 확정된 기존 마을만 사용하며 새 후보의 지형 준비 상태는 변경하지 않는다.
    FTownTestPlayer Participant = Fixture.AddPlayer(921, FableRPG::BoardLocation() + FVector(-100, -80, 110));
    FTownTestPlayer Observer = Fixture.AddPlayer(922, FVector(2200, -2400, 110));
    if (!TestTrue(TEXT("Both controllers own their loaded profiles and possessed characters"),
                  Participant.IsValid() && Observer.IsValid()))
    {
        return false;
    }
    Participant.Profile->ForestQuest = EFableQuestStage::Active;
    Observer.Profile->ForestQuest = EFableQuestStage::Active;
    Observer.Hero->SetCheckpoint(Observer.Hero->GetActorLocation(), FRotator(0, 90, 0), true);
    const FTransform Checkpoint = Observer.Hero->GetActorTransform();
    if (!TestTrue(TEXT("Only the intended player joins the real public field"),
                  Fixture.Towns->RegisterValidatedPresence(Observer.Controller) &&
                      Fixture.Mode->JoinPublicField(Participant.Controller)))
    {
        return false;
    }
    FFableTownTestAccess::SpawnForest(Fixture.Mode);

    // 실제 처치로 진행·보상을 먼저 만든 뒤 구조가 이를 보존하는지 관찰한다.
    AFableEnemy* Victim = Fixture.ForestEnemy();
    if (!TestNotNull(TEXT("The field has a real tracked enemy before the town fall"), Victim))
    {
        return false;
    }
    Victim->ReceiveHit(100000, Participant.Controller);
    Fixture.Mode->HandleEnemyDefeated(Victim);
    if (!TestTrue(TEXT("A real kill leaves an active field with earned progress"),
                  Fixture.State->FieldProgress.DefeatedEnemies == 1 && Participant.Profile->QuestKills == 1 &&
                      Fixture.State->FieldProgress.AdventureStage == EFableAdventureStage::Forest))
    {
        return false;
    }
    AFableEnemy* Remaining = Fixture.ForestEnemy();
    if (!TestNotNull(TEXT("The positive-control field still has a living enemy"), Remaining))
    {
        return false;
    }
    const float RemainingHealth = Remaining->GetHealth();
    const FTownPlayerSnapshot ParticipantBefore(Participant);
    const FTownPlayerSnapshot ObserverBefore(Observer);
    const FFableAdventureProgress ProgressBefore = Fixture.State->FieldProgress;
    const TArray<double> RecoveryTimersBefore = FFableTownTestAccess::FieldRecoveryTimers(Fixture.Mode);
    const FFablePublicAreaKey ScopeBefore = Fixture.Towns->GetPublicFieldKey();

    // 시간 필드만 진행해 지연 경계를 검사한다. 벽시계 대기·타이머 실행·접지는 검증하지 않는다.
    Fixture.World->TimeSeconds = 100;
    Observer.Hero->SetActorLocation(FVector(Checkpoint.GetLocation().X, Checkpoint.GetLocation().Y, -900));
    Observer.Hero->Tick(.016f);
    if (!TestTrue(TEXT("The real authoritative void handler marks the idle-town character defeated"),
                  Observer.Hero->IsDefeated() && Observer.Hero->GetDefeatedServerTime() >= 0))
    {
        return false;
    }
    FFableTownLocationContext VoidContext;
    TestFalse(TEXT("The fallen actor is not misclassified as a valid physical town"),
              Fixture.Towns->ResolvePhysicalContext(Observer.Hero, VoidContext));
    FFableTownTestAccess::Monitor(Fixture.Mode);
    TestTrue(TEXT("Individual recovery does not bypass the three-second delay"), Observer.Hero->IsDefeated());
    Fixture.World->TimeSeconds = Observer.Hero->GetDefeatedServerTime() + 2.99;
    FFableTownTestAccess::Monitor(Fixture.Mode);
    TestTrue(TEXT("The individual rescue remains pending before its own delay expires"), Observer.Hero->IsDefeated());

    Fixture.World->TimeSeconds = Observer.Hero->GetDefeatedServerTime() + 3.01;
    FFableTownTestAccess::Monitor(Fixture.Mode);
    const auto Restored = FableAdventure::For(Observer.Profile);
    TestTrue(TEXT("The normal game-mode monitor restores only the owner's established checkpoint"),
             Observer.Hero->IsAlive() && Observer.Hero->GetActorTransform().Equals(Checkpoint) && Restored &&
                 Restored.IsIdleTown() && Restored.PhysicalTown().TownId == EFableTownId::LegacyVillage &&
                 !Observer.Profile->ActiveExpeditionId.IsValid() &&
                 !Observer.Profile->GetPublicFieldScopeId().IsValid());
    TestTrue(TEXT("Individual town rescue preserves every field progress and recovery timer value"),
             FFableAdventureProgress::StaticStruct()->CompareScriptStruct(&ProgressBefore,
                                                                          &Fixture.State->FieldProgress, 0) &&
                 FFableTownTestAccess::FieldRecoveryTimers(Fixture.Mode) == RecoveryTimersBefore);
    TestTrue(TEXT("The active participant keeps position, resources, earned rewards, equipment and notices"),
             ParticipantBefore.Equals(Participant));
    TestTrue(TEXT("The active forest generation and its remaining enemy stay untouched"),
             Fixture.Towns->GetPublicFieldKey() == ScopeBefore &&
                 Participant.Profile->GetPublicFieldScopeId() == ScopeBefore.ScopeId &&
                 Fixture.Towns->IsSharedForestParticipant(Participant.Profile) &&
                 !Fixture.Towns->IsSharedForestParticipant(Observer.Profile) && Remaining->IsAlive() &&
                 Remaining->GetHealth() == RemainingHealth);
    TestTrue(TEXT("The owner recovers health and mana at the established checkpoint"),
             Observer.Hero->GetHealth() == ObserverBefore.Health && Observer.Hero->GetMana() == ObserverBefore.Mana);
    TestTrue(TEXT("Town recovery spends none of the owner's gold, ore, experience or quest progress"),
             Observer.Profile->Gold == ObserverBefore.Counters[0] &&
                 Observer.Profile->Ore == ObserverBefore.Counters[1] &&
                 Observer.Profile->Experience == ObserverBefore.Counters[2] &&
                 Observer.Profile->QuestKills == ObserverBefore.Counters[3]);
    const auto Registered = FFableTownTestAccess::FieldPlayers(Fixture.Mode);
    TestTrue(TEXT("The rescued town observer never enters the field's living-player or cleanup roster"),
             Registered.Num() == 1 && Registered.Contains(Participant.Controller) &&
                 !Registered.Contains(Observer.Controller));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownPublicProjectileTest,
                                 "Fablewood.Native.Town.CapturedPublicProjectilesRejectScopeChanges",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 발사 순간의 공개 세대는 reset·이탈 뒤 소유자의 새 공간을 따라가지 않는다. */
bool FFableTownPublicProjectileTest::RunTest(const FString& Parameters)
{
    FTownTestWorld Fixture;
    FTownTestPlayer Participant, Observer;
    if (!TestTrue(TEXT("Projectile fixture uses owned registered participants"),
                  Fixture.InitializePublic(Participant, Observer)))
    {
        return false;
    }
    AFableEnemy* Enemy = Fixture.ForestEnemy();
    if (!TestNotNull(TEXT("A positive-control target belongs to the current field"), Enemy))
    {
        return false;
    }
    const FVector ShotPosition = Enemy->GetActorLocation() - FVector(300, 0, 0);
    AFableProjectile* Control = Fixture.SpawnShot(Participant, ShotPosition, FVector::ForwardVector);
    if (!TestNotNull(TEXT("A real initialized projectile exists"), Control))
    {
        return false;
    }
    const float ControlHealth = Enemy->GetHealth();
    Control->Tick(.3f);
    TestTrue(TEXT("Current-generation swept projectile really damages an eligible target"),
             Enemy->GetHealth() < ControlHealth && Control->GetUniqueHitCount() == 1);

    const FFablePublicAreaKey OldKey = Fixture.Towns->GetPublicFieldKey();
    AFableProjectile* OldShot = Fixture.SpawnShot(Participant, ShotPosition, FVector::ForwardVector);
    FActorSpawnParameters BoltParameters;
    BoltParameters.Owner = Enemy;
    BoltParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableEnemyBolt* OldBolt = Fixture.World->SpawnActor<AFableEnemyBolt>(
        Participant.Hero->GetActorLocation() - FVector(200, 0, 0), FRotator::ZeroRotator, BoltParameters);
    if (!TestNotNull(TEXT("A pre-reset player shot exists"), OldShot) ||
        !TestNotNull(TEXT("A pre-reset enemy shot exists"), OldBolt))
    {
        return false;
    }
    // 이 초기화는 공개 세대 캡처만 검사하며 숲의 정상 WaterBolt 공격을 주장하지 않는다.
    OldBolt->InitializeBolt(FVector::ForwardVector, 10, EFableRegion::Moonwell);
    TestTrue(TEXT("Both real initialization APIs capture the old public key with no private UUID"),
             OldShot->GetPublicAreaKey() == OldKey && OldBolt->GetPublicAreaKey() == OldKey &&
                 !OldShot->GetExpeditionId().IsValid() && !OldBolt->GetExpeditionId().IsValid());
    const FGuid NewGeneration = Fixture.Towns->RotatePublicFieldScope();
    TestTrue(TEXT("The real reset API re-registers current members into a fresh generation"),
             NewGeneration.IsValid() && NewGeneration != OldKey.ScopeId &&
                 Participant.Profile->GetPublicFieldScopeId() == NewGeneration &&
                 !Observer.Profile->GetPublicFieldScopeId().IsValid());

    FActorSpawnParameters TargetParameters;
    TargetParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableEnemy* CurrentTarget = Fixture.World->SpawnActor<AFableEnemy>(Enemy->GetActorLocation() + FVector(0, 700, 0),
                                                                        FRotator::ZeroRotator, TargetParameters);
    if (!TestNotNull(TEXT("A new-generation target exists"), CurrentTarget))
    {
        return false;
    }
    CurrentTarget->BindPublicField(Fixture.Towns->GetPublicFieldKey());
    OldShot->SetActorLocation(CurrentTarget->GetActorLocation() - FVector(300, 0, 0));
    const float NewTargetHealth = CurrentTarget->GetHealth();
    const float HeroHealth = Participant.Hero->GetHealth();
    OldShot->Tick(.3f);
    OldBolt->Tick(.3f);
    TestTrue(TEXT("Old initialized projectiles retain their key and retire before affecting the new field"),
             OldShot->GetPublicAreaKey() == OldKey && OldBolt->GetPublicAreaKey() == OldKey &&
                 OldShot->IsActorBeingDestroyed() && OldBolt->IsActorBeingDestroyed() &&
                 CurrentTarget->GetHealth() == NewTargetHealth && Participant.Hero->GetHealth() == HeroHealth);

    AFableProjectile* Fresh =
        Fixture.SpawnShot(Participant, CurrentTarget->GetActorLocation() - FVector(300, 0, 0), FVector::ForwardVector);
    if (!TestNotNull(TEXT("A fresh current-generation shot exists"), Fresh))
    {
        return false;
    }
    Fresh->Tick(.3f);
    TestTrue(TEXT("A current-generation positive control still damages the new target"),
             CurrentTarget->GetHealth() < NewTargetHealth && Fresh->GetUniqueHitCount() == 1);
    AFableProjectile* BeforeTravel =
        Fixture.SpawnShot(Participant, CurrentTarget->GetActorLocation() - FVector(300, 0, 0), FVector::ForwardVector);
    if (!TestNotNull(TEXT("A shot was really captured before leaving the field"), BeforeTravel))
    {
        return false;
    }
    TestTrue(TEXT("The normal leave API removes combat membership"),
             Fixture.Mode->LeavePublicField(Participant.Controller));
    Participant.Hero->SetActorLocation(Observer.Hero->GetActorLocation() + FVector(300, 0, 0));
    TestTrue(TEXT("A synthetic destination presence uses the normal owner registry"),
             Fixture.Towns->RegisterValidatedPresence(Participant.Controller));
    const float BeforeTravelHealth = CurrentTarget->GetHealth();
    BeforeTravel->Tick(.3f);
    TestTrue(TEXT("Leaving combat for an idle town cannot carry a shot into the owner's new scope"),
             FableAdventure::For(Participant.Profile).IsIdleTown() && BeforeTravel->IsActorBeingDestroyed() &&
                 CurrentTarget->GetHealth() == BeforeTravelHealth);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownPrivateContextTest,
                                 "Fablewood.Native.Town.PrivateTicketAndPhysicalTownStaySeparate",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 원정 입장 뒤 물리 마을을 별도로 조회해도 증서 소유권과 필드 격리를 유지한다. */
bool FFableTownPrivateContextTest::RunTest(const FString& Parameters)
{
    FTownTestWorld Fixture;
    if (!Fixture.Towns || !TestTrue(TEXT("Elf registry is synthetic and does not load any level"),
                                    FFableTownTestAccess::EnableSyntheticTown(Fixture.Towns, EFableTownId::Eldergrove)))
    {
        return false;
    }
    FTownTestPlayer Participant = Fixture.AddPlayer(911, FableRPG::PortalLocation() + FVector(-80, -100, 110));
    if (!TestTrue(TEXT("The private participant has a real owned profile and pawn"), Participant.IsValid()))
    {
        return false;
    }
    Participant.Profile->ForestQuest = EFableQuestStage::Completed;
    Participant.Profile->DungeonQuest = EFableQuestStage::Active;
    TestTrue(TEXT("The ordinary server party API creates the solo party"),
             Fixture.Parties->ExecuteAction(Participant.Controller, EFablePartyAction::Create));
    const FFablePartyRecord* Party = Fixture.Parties->GetPartyForMember(Participant.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("A real formal-party record exists"), Party))
    {
        return false;
    }
    if (!TestTrue(TEXT("The owner explicitly readies and enters through the actual admission gate"),
                  Fixture.Parties->SetExpeditionReady(Participant.Controller, Party->PartyId,
                                                      Party->PreparationRevision, true) &&
                      Fixture.Mode->RequestStartExpedition(Participant.Controller, Party->PartyId,
                                                           Party->PreparationRevision)))
    {
        return false;
    }
    const FGuid Id = Participant.Profile->ActiveExpeditionId;
    AFablePartyEncounter* Encounter = Fixture.Mode->GetExpeditionFor(Participant.Profile);
    const auto Private = FableAdventure::For(Participant.Profile);
    if (!TestTrue(TEXT("The actual admitted ticket resolves before any public context"),
                  Id.IsValid() && Encounter && Private && Private.ExpeditionId() == Id && !Private.IsIdleTown() &&
                      !Private.PublicArea().IsValid() && !Participant.Profile->GetPublicFieldScopeId().IsValid()))
    {
        return false;
    }
    AFableProjectile* PrivateShot =
        Fixture.SpawnShot(Participant, Participant.Hero->GetActorLocation(), FVector::ForwardVector);
    if (!TestNotNull(TEXT("A genuine private projectile captures its original UUID"), PrivateShot))
    {
        return false;
    }
    TestEqual(TEXT("Private projectile identity is captured by the real API"), PrivateShot->GetExpeditionId(), Id);
    FFableTownTestAccess::ClearForest(Fixture.Mode);
    TestTrue(TEXT("Public cleanup cannot remove a private projectile or its ticket"),
             !PrivateShot->IsActorBeingDestroyed() && Participant.Profile->ActiveExpeditionId == Id &&
                 Fixture.Mode->FindExpedition(Id) == Encounter);

    // 지역·보행·네트워크 이동을 주장하지 않고 private와 물리 context 조회만 분리한다.
    FVector SyntheticTownPosition;
    if (!TestTrue(TEXT("An explicit synthetic top-floor position exists"),
                  Fixture.Towns->GetCatalog()->ToWorld(EFableTownId::Eldergrove, FVector(3700, 0, 9104),
                                                       SyntheticTownPosition)))
    {
        return false;
    }
    Participant.Hero->SetActorLocation(SyntheticTownPosition);
    TestTrue(TEXT("A real owner presence may be observed separately from the ticket"),
             Fixture.Towns->RegisterValidatedPresence(Participant.Controller));
    const auto RescuedView = FableAdventure::For(Participant.Profile);
    TestTrue(
        TEXT("Physical Elf F3 does not clear, replace or reinterpret the private ticket"),
        RescuedView && RescuedView.ExpeditionId() == Id && !RescuedView.IsIdleTown() &&
            RescuedView.PhysicalTown().IsValid() && RescuedView.PhysicalTown().TownId == EFableTownId::Eldergrove &&
            RescuedView.PhysicalTown().FloorId == FName(TEXT("F3")) && Participant.Profile->ActiveExpeditionId == Id &&
            !Participant.Profile->GetPublicFieldScopeId().IsValid());
    TestFalse(TEXT("A private participant cannot join public combat from its physical town"),
              Fixture.Mode->JoinPublicField(Participant.Controller));
    Participant.Profile->ActiveExpeditionId = FGuid::NewGuid();
    TestFalse(TEXT("An unresolved owned private UUID fails closed despite a valid physical town"),
              static_cast<bool>(FableAdventure::For(Participant.Profile)));
    Participant.Profile->ActiveExpeditionId = Id;
    return true;
}

#endif
