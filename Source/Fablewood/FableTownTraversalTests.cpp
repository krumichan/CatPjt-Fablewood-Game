#include "FableTownCatalog.h"
#include "FableTownNavigation.h"
#include "FableTownOverlook.h"
#include "FableProfileSchema.h"
#include "FableWorld.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** 저장·계정 없이 제품 월드의 실제 정적 충돌만 구성한다. / 保存・アカウントなしで製品の静的衝突を構成する。 */
    struct FOverlookCollisionWorld
    {
        UWorld* World = nullptr;
        AFableWorld* Environment = nullptr;
        bool bOwnsContext = false;

        /** 메모리 월드에도 제품 AFableWorld와 같은 메시·배치·태그를 사용한다. */
        FOverlookCollisionWorld()
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
            Environment = World->SpawnActor<AFableWorld>();
        }

        /** 생성한 월드와 문맥만 해제하며 실행 중 게임에는 접근하지 않는다. */
        ~FOverlookCollisionWorld()
        {
            if (!World)
            {
                return;
            }

            World->DestroyWorld(false);
            if (bOwnsContext && GEngine)
            {
                GEngine->DestroyWorldContext(World);
            }
        }

        /** 실제 제품 컴포넌트의 수직 지지면을 조회한다. / 実製品コンポーネントの支持面を照会する。 */
        bool Support(double X, double Y, FHitResult& OutHit) const
        {
            return World && World->LineTraceSingleByChannel(OutHit, FVector(X, Y, 1200.0), FVector(X, Y, -100.0),
                                                            ECC_Visibility);
        }
    };

    /** 변형하지 않은 정본에서 전망대 표시 앵커만 만든다. / 正本の展望台表示アンカー参照だけを作る。 */
    FFableTownAnchorRef OverlookReference()
    {
        FFableTownAnchorRef Reference;
        Reference.TownId = EFableTownId::LegacyVillage;
        Reference.AnchorId = TEXT("Waypoint.Overlook");
        Reference.CatalogVersion = 1;
        return Reference;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTraversalCanonical,
                                 "Fablewood.Native.TownTraversal.CanonicalGeometryAndCatalog",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 상층 도입이 기존 지상·서비스·비활성 네 마을을 바꾸지 않는지 검증한다. */
bool FFableTownTraversalCanonical::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UFableTownCatalog> Catalog(NewObject<UFableTownCatalog>());
    FString Error;
    TestTrue(TEXT("The full production catalog validates with the actual overlook"), Catalog->Validate(Error));
    const FFableTownDefinition* Legacy = Catalog->FindTown(EFableTownId::LegacyVillage);
    TestNotNull(TEXT("Production legacy town remains present"), Legacy);
    if (!Legacy)
    {
        return false;
    }

    const FFableTownFloorDefinition* Ground = Legacy->Floors.FindByPredicate(
        [](const FFableTownFloorDefinition& Floor)
        {
            return Floor.FloorId == FName(TEXT("Ground"));
        });
    const FFableTownFloorDefinition* Upper = Legacy->Floors.FindByPredicate(
        [](const FFableTownFloorDefinition& Floor)
        {
            return Floor.FloorId == FableTownOverlook::FloorId();
        });
    TestTrue(TEXT("The original Ground volume is preserved including jump heights"),
             Ground && Ground->LocalBounds.Min.Equals(FVector(-4500, -4500, -799)) &&
                 Ground->LocalBounds.Max.Equals(FVector(4499, 4500, 1499)));
    TestTrue(TEXT("Upper floor is authored outside the old east volume with no overlap"),
             Ground && Upper && !Ground->LocalBounds.Intersect(Upper->LocalBounds) &&
                 Upper->LocalBounds.Min.Equals(FableTownOverlook::UpperFloorBounds().Min) &&
                 Upper->LocalBounds.Max.Equals(FableTownOverlook::UpperFloorBounds().Max));

    const UFableTownCatalog* Defaults = GetDefault<UFableTownCatalog>();
    const FFableTownDefinition* DefaultLegacy = Defaults->FindTown(EFableTownId::LegacyVillage);
    TestTrue(TEXT("Default and owned catalogs share exactly the same production floor and corridor layout"),
             DefaultLegacy && DefaultLegacy->Floors.Num() == Legacy->Floors.Num() &&
                 DefaultLegacy->Connectors.Num() == Legacy->Connectors.Num() &&
                 DefaultLegacy->LocalBounds.Max.Equals(Legacy->LocalBounds.Max));
    TestEqual(TEXT("The catalog still contains the original town and four candidates"), Catalog->GetDefinitions().Num(),
              5);
    for (EFableTownId Candidate :
         {EFableTownId::Sunbrook, EFableTownId::Starbough, EFableTownId::Snowbloom, EFableTownId::Eldergrove})
    {
        TestFalse(TEXT("An overlook extension never adopts a candidate town"), Catalog->IsGeometryReady(Candidate));
    }

    FFableTownServiceRef Service;
    Service.TownId = EFableTownId::LegacyVillage;
    Service.CatalogVersion = 1;
    for (EFableTownServiceId Id : {EFableTownServiceId::QuestGuide, EFableTownServiceId::Workshop,
                                   EFableTownServiceId::Merchant, EFableTownServiceId::ExpeditionPortal})
    {
        Service.ServiceId = Id;
        FFableTownServiceDescriptor Descriptor;
        TestTrue(TEXT("Existing services retain valid ground anchors and support metadata"),
                 Catalog->ResolveService(Service, Descriptor, true) && Descriptor.FloorId == FName(TEXT("Ground")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTraversalSupport,
                                 "Fablewood.Native.TownTraversal.ActualSupportAndBoundaryOpening",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 宣言だけでなく製品Actorの実衝突・各段高さ・狭い境界開口を検査する。 */
bool FFableTownTraversalSupport::RunTest(const FString& Parameters)
{
    FOverlookCollisionWorld Fixture;
    TestNotNull(TEXT("A physics-query world exists"), Fixture.World);
    TestNotNull(TEXT("The real product environment actor was constructed"), Fixture.Environment);
    if (!Fixture.World || !Fixture.Environment)
    {
        return false;
    }

    for (int32 Index = 0; Index < FableTownOverlook::StepCount; ++Index)
    {
        FHitResult Hit;
        const double X = FableTownOverlook::StairStartX + (Index + 0.5) * FableTownOverlook::StepRunCm;
        const bool bHit = Fixture.Support(X, FableTownOverlook::CenterY, Hit);
        const UPrimitiveComponent* Component = Hit.GetComponent();
        TestTrue(TEXT("Every authored tread has real walkable support at its declared height"),
                 bHit && Hit.GetActor() == Fixture.Environment && Component &&
                     Component->ComponentHasTag(FableTownOverlook::StairSupportTag()) &&
                     FMath::IsNearlyEqual(Hit.ImpactPoint.Z, (Index + 1) * FableTownOverlook::StepRiseCm, 0.5) &&
                     Hit.ImpactNormal.Z > 0.99);
    }

    FHitResult Landing;
    TestTrue(TEXT("The landing support tag and physical height match the canonical navigation anchor"),
             Fixture.Support(5600.0, FableTownOverlook::CenterY, Landing) && Landing.GetComponent() &&
                 Landing.GetComponent()->ComponentHasTag(FableTownOverlook::LandingSupportTag()) &&
                 FMath::IsNearlyEqual(Landing.ImpactPoint.Z, FableTownOverlook::SurfaceZ, 0.5));
    FHitResult Outside;
    TestFalse(TEXT("The extension does not create a broad invisible floor beyond the guard rails"),
              Fixture.Support(5600.0, FableTownOverlook::CenterY + 300.0, Outside));

    FHitResult Opening;
    TestFalse(TEXT("The old east wall is open only at the stair approach"),
              Fixture.World->LineTraceSingleByChannel(Opening, FVector(3001.0, FableTownOverlook::CenterY, 120.0),
                                                      FVector(3030.0, FableTownOverlook::CenterY, 120.0),
                                                      ECC_Visibility));
    FHitResult ClosedWall;
    TestTrue(TEXT("The east wall outside the stair opening still blocks movement"),
             Fixture.World->LineTraceSingleByChannel(ClosedWall, FVector(2990, -2400, 120), FVector(3030, -2400, 120),
                                                     ECC_Visibility) &&
                 ClosedWall.GetComponent() && ClosedWall.GetComponent()->ComponentHasTag(TEXT("PrototypeBoundary")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTraversalContexts,
                                 "Fablewood.Native.TownTraversal.CorridorWidthAndDistinctHeight",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 床の体積と通路幅を実配置と対照し、同じXYの低い場所を上階にしない。 */
bool FFableTownTraversalContexts::RunTest(const FString& Parameters)
{
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    FFableTownLocationContext Context;
    TestTrue(TEXT("Original ground stays ground before the preserved east edge"),
             Catalog->ResolveLocation(FVector(4300, FableTownOverlook::CenterY, 440), Context) &&
                 Context.FloorId == FName(TEXT("Ground")) && Context.ConnectorId.IsNone());

    for (double Offset : {-200.0, 0.0, 200.0})
    {
        TestTrue(TEXT("The walkable stair width remains a corridor between the nonoverlapping floors"),
                 Catalog->ResolveLocation(FVector(4700, FableTownOverlook::CenterY + Offset, 510), Context) &&
                     Context.FloorId.IsNone() && Context.ConnectorId == FableTownOverlook::ConnectorId());
    }
    TestFalse(TEXT("Coordinates beyond the physical corridor width are not a registered town context"),
              Catalog->ResolveLocation(FVector(4700, FableTownOverlook::CenterY + 300, 510), Context));
    TestFalse(TEXT("A ground-height point under the upper landing is not an upper-floor arrival"),
              Catalog->ResolveLocation(FVector(5600, FableTownOverlook::CenterY, 104), Context));
    TestTrue(TEXT("The actual supported upper position resolves the new floor"),
             Catalog->ResolveLocation(FableTownOverlook::OverlookAnchorPosition(), Context) &&
                 Context.FloorId == FableTownOverlook::FloorId() && Context.ConnectorId.IsNone());

    FFableTownServiceDescriptor Service;
    TestTrue(TEXT("An upper-floor player cannot use a ground service through XY proximity"),
             FableTownNavigation::ResolveService(Catalog, FableTownOverlook::OverlookAnchorPosition(),
                                                 EFableTownServiceId::QuestGuide,
                                                 Service) == EFableTownNavigationStatus::DifferentFloor);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownTraversalGuidance,
                                 "Fablewood.Native.TownTraversal.GuidanceAndSafeReconnectPolicy",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 上階からの下り案内と復帰時の初期化を確認し、表示アンカーを保存権限に変えない。 */
bool FFableTownTraversalGuidance::RunTest(const FString& Parameters)
{
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    FFableTownNavigationGuide Guide;
    TestTrue(TEXT("An upper-floor player receives the same canonical ground NPC through the down entrance"),
             FableTownNavigation::ResolveServiceGuide(Catalog, FableTownOverlook::OverlookAnchorPosition(),
                                                      EFableTownServiceId::QuestGuide,
                                                      Guide) == EFableTownRouteStatus::Ready &&
                 Guide.bValid && Guide.bViaConnector && Guide.ConnectorId == FableTownOverlook::ConnectorId() &&
                 Guide.SourceFloorId == FableTownOverlook::FloorId() && Guide.NextFloorId == FName(TEXT("Ground")) &&
                 Guide.TargetFloorId == FName(TEXT("Ground")) && Guide.RemainingSteps == 1);

    TestTrue(TEXT("Entering the real corridor clears the previous upper-floor entrance and progress"),
             FableTownNavigation::ResolveServiceGuide(Catalog, FVector(4700, FableTownOverlook::CenterY, 510),
                                                      EFableTownServiceId::QuestGuide,
                                                      Guide) == EFableTownRouteStatus::ConnectorUnavailable &&
                 !Guide.bValid && Guide.WaypointWorld.IsZero() &&
                 FableTownNavigation::RouteProgressText(Guide).IsEmpty());
    TestTrue(TEXT("Returning to ground restores direct NPC guidance without stale connector metadata"),
             FableTownNavigation::ResolveServiceGuide(Catalog, FVector(2800, FableTownOverlook::CenterY, 104),
                                                      EFableTownServiceId::QuestGuide,
                                                      Guide) == EFableTownRouteStatus::Ready &&
                 Guide.bValid && !Guide.bViaConnector && Guide.ConnectorId.IsNone() && Guide.RemainingSteps == 0 &&
                 FableTownNavigation::RouteProgressText(Guide).IsEmpty());

    const FFableTownAnchorRef DisplayAnchor = OverlookReference();
    FFableTownRoute Route;
    TestTrue(TEXT("Ground-to-overlook routing uses the same authored bidirectional connector"),
             FableTownRoutes::ResolveAnchor(Catalog, FVector(2800, FableTownOverlook::CenterY, 104), DisplayAnchor,
                                            Route) == EFableTownRouteStatus::Ready &&
                 Route.Steps.Num() == 1 && Route.Steps[0].ConnectorId == FableTownOverlook::ConnectorId());
    TestFalse(TEXT("A navigation-only overlook anchor never becomes a durable spawn or teleport permission"),
              FableProfileSchema::IsSafeAnchor(DisplayAnchor));
    TestTrue(TEXT("The established reconnect policy still uses the validated legacy Entry"),
             FableProfileSchema::IsSafeAnchor(
                 FableProfileSchema::BootstrapAnchor(EFableTownBootstrapPolicy::LegacyVillageV1)));
    return true;
}
#endif
