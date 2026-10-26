#include "FableTownNavigation.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableGameMode.h"
#include "FableGameState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
    /** 실제 프로필 초기화·저장·맵 채택 없이 안내 조회만 실행하는 전용 월드다. / 案内照会だけの隔離ワールド。 */
    struct FRouteUiIntegrationWorld
    {
        UWorld* World = nullptr;
        AFableGameMode* Mode = nullptr;
        AFableGameState* Adventure = nullptr;
        AFableTownManager* Towns = nullptr;
        AFablePlayerController* Controller = nullptr;
        AFablePlayerState* State = nullptr;
        AFableCharacter* Hero = nullptr;
        bool bOwnsContext = false;
        bool bRegisteredController = false;

        /** 월드별 manager의 자체 카탈로그만 사용한다. / manager固有のカタログだけを使う。 */
        FRouteUiIntegrationWorld()
        {
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

            if (GEngine)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                bOwnsContext = true;
            }

            Mode = World->SpawnActor<AFableGameMode>();
            Adventure = World->SpawnActor<AFableGameState>();
            if (Mode && Adventure)
            {
                World->CopyGameState(Mode, Adventure);
            }
            Towns = AFableTownManager::Ensure(World);
            Controller = World->SpawnActor<AFablePlayerController>();
            State = World->SpawnActor<AFablePlayerState>();
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Hero = World->SpawnActor<AFableCharacter>(FVector(-2300, -2300, 110), FRotator::ZeroRotator, Spawn);
            if (Controller && State && Hero)
            {
                Controller->SetPlayerState(State);
                Controller->Possess(Hero);
                State->SetOwner(Controller);
                World->AddController(Controller);
                bRegisteredController = true;
                State->bProfileLoaded = true;
                State->StoryChapter = 0;
            }
        }

        /** 소유한 월드·등록만 해제하며 외부 실행 문맥은 변경하지 않는다. / 所有した文脈だけを解放する。 */
        ~FRouteUiIntegrationWorld()
        {
            if (World)
            {
                if (bRegisteredController)
                {
                    World->RemoveController(Controller);
                }
                World->DestroyWorld(false);
                if (GEngine && bOwnsContext)
                {
                    GEngine->DestroyWorldContext(World);
                }
            }
        }

        /** 검증 대상들이 같은 월드에 준비되었는지 확인한다. / 同じワールドの参照を確認する。 */
        bool IsReady() const
        {
            return World && Mode && Adventure && Towns && Controller && State && Hero && Towns->GetCatalog() &&
                   Towns->GetCatalog() != GetDefault<UFableTownCatalog>();
        }

        /** 테스트 월드의 로컬 카탈로그만 수정하고 전역 CDO에는 접근하지 않는다. */
        FFableTownDefinition* OwnLegacy() const
        {
            return Towns && Towns->GetCatalog()
                       ? const_cast<FFableTownDefinition*>(Towns->GetCatalog()->FindTown(EFableTownId::LegacyVillage))
                       : nullptr;
        }

        /** 기존 서비스는 유지하고 안내인만 합성 상층으로 옮긴다. / 合成上階へ案内人だけを置く。 */
        bool AddUpperGuide(bool bRotate)
        {
            FFableTownDefinition* Definition = OwnLegacy();
            if (!Definition)
            {
                return false;
            }

            Definition->Floors.Empty();
            FFableTownFloorDefinition Ground;
            Ground.FloorId = TEXT("Ground");
            Ground.LocalBounds = FBox(FVector(-4500, -4500, -799), FVector(4499, 4500, 300));
            Definition->Floors.Add(Ground);
            FFableTownFloorDefinition Upper;
            Upper.FloorId = TEXT("Upper");
            Upper.LocalBounds = FBox(FVector(-4500, -4500, 700), FVector(4499, 4500, 1499));
            Definition->Floors.Add(Upper);

            for (FFableTownAnchorDefinition& Anchor : Definition->Anchors)
            {
                if (Anchor.AnchorId == FName(TEXT("Service.Board")))
                {
                    Anchor.FloorId = TEXT("Upper");
                    Anchor.LocalSlots = {FTransform(FVector(0, -750, 900))};
                    Anchor.ExpectedSupportLocalZ = 900.0;
                }
            }

            FFableTownConnectorDefinition Stair;
            Stair.ConnectorId = TEXT("Guide.Stair");
            Stair.FromFloorId = TEXT("Ground");
            Stair.ToFloorId = TEXT("Upper");
            Stair.LocalPoints = {FVector(100, -750, 110), FVector(100, -750, 500), FVector(100, -750, 900)};
            Stair.RadiusCm = 25.0;
            Definition->Connectors = {Stair};
            if (bRotate)
            {
                Definition->LocalToWorld = FTransform(FRotator(0, 90, 0), FVector(15000, -10000, 300));
            }

            MoveLocal(FVector(-2300, -2300, 110));
            FString Error;
            return Towns->GetCatalog()->Validate(Error);
        }

        /** 합성 fixture 위치 변경이며 실제 이동·로드 검증은 아니다. / 合成位置であり実移動検証ではない。 */
        void MoveLocal(const FVector& Position)
        {
            if (Hero && OwnLegacy())
            {
                Hero->SetActorLocation(OwnLegacy()->LocalToWorld.TransformPosition(Position), false, nullptr,
                                       ETeleportType::TeleportPhysics);
            }
        }
    };

    /** 실패나 더 높은 우선순위 목표 뒤에 이전 경로가 남지 않는지 확인한다. */
    bool RouteUiGuideCleared(const FFableTownNavigationGuide& Guide)
    {
        return !Guide.bValid && !Guide.bViaConnector && Guide.ConnectorId.IsNone() && Guide.SourceFloorId.IsNone() &&
               Guide.NextFloorId.IsNone() && Guide.TargetFloorId.IsNone() && Guide.RemainingSteps == 0 &&
               Guide.WaypointWorld.IsZero() && Guide.TargetWorld.IsZero();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableRouteUiStoryAndController,
                                 "Fablewood.Native.TownRoutes.StoryAndControllerGuideConsistency",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 이야기와 컨트롤러가 같은 회전된 진입점을 공유하고 서비스 허가를 늘리지 않는다. */
bool FFableRouteUiStoryAndController::RunTest(const FString& Parameters)
{
    FRouteUiIntegrationWorld Fixture;
    if (!TestTrue(TEXT("Isolated world and own catalog are ready"), Fixture.IsReady()) ||
        !TestTrue(TEXT("Synthetic rotated two-floor catalog remains valid"), Fixture.AddUpperGuide(true)))
    {
        return false;
    }

    const int32 GoldBefore = Fixture.State->Gold;
    const int32 OreBefore = Fixture.State->Ore;
    const FVector StartBefore = Fixture.Hero->GetActorLocation();
    const FVector Entry = Fixture.OwnLegacy()->LocalToWorld.TransformPosition(FVector(100, -750, 110));
    const FVector ActualService = Fixture.OwnLegacy()->LocalToWorld.TransformPosition(FVector(0, -750, 900));
    FVector StoryTarget;
    FString StoryLabel;
    EFableRegion Region = EFableRegion::RootRuins;
    FFableTownNavigationGuide StoryGuide;
    TestTrue(TEXT("Story target uses the current floor stair entry in world coordinates"),
             Fixture.State->GetStoryTarget(StoryTarget, StoryLabel, &Region, &StoryGuide) &&
                 Region == EFableRegion::Village && StoryTarget.Equals(Entry) && StoryGuide.bValid &&
                 StoryGuide.bViaConnector && StoryGuide.Service.WorldLocation.Equals(ActualService));

    FVector Target;
    FString Label;
    FFableTownNavigationGuide Guide;
    TestTrue(TEXT("Controller returns the same selected waypoint, label and route identity"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Target.Equals(StoryTarget) &&
                 Label == StoryLabel && Guide.ConnectorId == StoryGuide.ConnectorId && Guide.RemainingSteps == 1);
    TestTrue(TEXT("Old two-argument callers keep working"),
             Fixture.Controller->GetNavigationTarget(Target, Label) && Target.Equals(Entry));

    FFableTownServiceDescriptor StrictService;
    TestTrue(TEXT("Strict service lookup still rejects another floor"),
             FableTownNavigation::ResolveService(Fixture.Towns->GetCatalog(), StartBefore,
                                                 EFableTownServiceId::QuestGuide,
                                                 StrictService) == EFableTownNavigationStatus::DifferentFloor);
    TestFalse(
        TEXT("Route display cannot grant remote service interaction"),
        Fixture.Towns->GetCatalog()->ResolveNearbyService(StartBefore, EFableTownServiceId::QuestGuide, StrictService));

    Fixture.State->StoryChapter = 5;
    TestTrue(TEXT("Selected-quest fallback also uses the current-floor waypoint after the main story"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Target.Equals(Entry) && Guide.bValid &&
                 Guide.bViaConnector);
    TestTrue(TEXT("Read-only guide requests preserve position, expedition and money"),
             Fixture.Hero->GetActorLocation().Equals(StartBefore) && !Fixture.State->ActiveExpeditionId.IsValid() &&
                 Fixture.State->Gold == GoldBefore && Fixture.State->Ore == OreBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableRouteUiSameFloorAndFailure,
                                 "Fablewood.Native.TownRoutes.SameFloorAndFailureGuideIsolation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 기존 같은 층 안내를 보존하고 경로·지형·프로필 실패의 이전 표시를 지운다. */
bool FFableRouteUiSameFloorAndFailure::RunTest(const FString& Parameters)
{
    FRouteUiIntegrationWorld Fixture;
    if (!TestTrue(TEXT("Same-floor fixture is ready"), Fixture.IsReady()))
    {
        return false;
    }

    FVector Target;
    FString Label;
    FFableTownNavigationGuide Guide;
    TestTrue(TEXT("Existing same-floor guide target and label remain unchanged"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) &&
                 Target.Equals(FableRPG::BoardLocation()) && Label == TEXT("안내인 로완") && Guide.bValid &&
                 !Guide.bViaConnector && Guide.RemainingSteps == 0);
    if (!TestTrue(TEXT("Two-floor fixture is ready"), Fixture.AddUpperGuide(false)))
    {
        return false;
    }
    TestTrue(TEXT("A valid route is available before checking failures"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Guide.bViaConnector);

    Fixture.OwnLegacy()->Connectors.Empty();
    TestFalse(TEXT("Disconnected floors do not reuse the previous waypoint"),
              Fixture.Controller->GetNavigationTarget(Target, Label, &Guide));
    TestTrue(TEXT("No-route response clears coordinates and optional metadata"),
             Target.IsZero() && !Label.IsEmpty() && RouteUiGuideCleared(Guide));

    if (!TestTrue(TEXT("Restore the valid route before geometry rejection"), Fixture.AddUpperGuide(false)))
    {
        return false;
    }
    TestTrue(TEXT("Geometry-negative control starts with populated metadata"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Guide.bValid);
    Fixture.OwnLegacy()->bGeometryReady = false;
    TestFalse(TEXT("Unready geometry has no stale guide"),
              Fixture.Controller->GetNavigationTarget(Target, Label, &Guide));
    TestTrue(TEXT("Unready geometry clears optional metadata"), Target.IsZero() && RouteUiGuideCleared(Guide));

    Fixture.OwnLegacy()->bGeometryReady = true;
    TestTrue(TEXT("Profile-negative control has a valid fallback route"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Guide.bValid);
    Fixture.State->bProfileLoaded = false;
    TestFalse(TEXT("Loading profiles cannot fall back to a valid service guide"),
              Fixture.Controller->GetNavigationTarget(Target, Label, &Guide));
    TestTrue(TEXT("Loading profile output is cleared"), Target.IsZero() && RouteUiGuideCleared(Guide));
    Fixture.State->bProfileLoaded = true;
    TestTrue(TEXT("Missing-pawn control starts with a populated guide"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Guide.bValid);
    Fixture.Controller->UnPossess();
    TestFalse(TEXT("A missing pawn does not reuse a previous guide"),
              Fixture.Controller->GetNavigationTarget(Target, Label, &Guide));
    TestTrue(TEXT("Missing-pawn output is cleared"), Target.IsZero() && RouteUiGuideCleared(Guide));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableRouteUiNonServicePriority,
                                 "Fablewood.Native.TownRoutes.NonServiceTargetClearsTownGuide",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 이야기 오브젝트가 선택되면 이전 마을 경로를 재조회하거나 섞지 않는다. */
bool FFableRouteUiNonServicePriority::RunTest(const FString& Parameters)
{
    FRouteUiIntegrationWorld Fixture;
    if (!TestTrue(TEXT("Story-priority fixture is ready"), Fixture.IsReady()) ||
        !TestTrue(TEXT("Story-priority two-floor definition is valid"), Fixture.AddUpperGuide(false)))
    {
        return false;
    }

    FVector Target;
    FString Label;
    FFableTownNavigationGuide Guide;
    TestTrue(TEXT("Town service first populates route metadata"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) && Guide.bViaConnector);

    Fixture.State->StoryChapter = 1;
    Fixture.State->StoryObjectMask = 0;
    EFableRegion Region = EFableRegion::RootRuins;
    TestTrue(TEXT("Story object uses its existing same-floor canonical contract"),
             Fixture.State->GetStoryTarget(Target, Label, &Region, &Guide) &&
                 Target.Equals(FableRPG::StoryObjectLocation(0)) && Region == EFableRegion::Village &&
                 RouteUiGuideCleared(Guide));
    TestTrue(TEXT("Controller keeps the story-object priority without stale town-route metadata"),
             Fixture.Controller->GetNavigationTarget(Target, Label, &Guide) &&
                 Target.Equals(FableRPG::StoryObjectLocation(0)) && RouteUiGuideCleared(Guide));
    return true;
}
#endif
