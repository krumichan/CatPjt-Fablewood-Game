#include "FableTownRoutes.h"
#include "FableTownCatalog.h"
#include "FableRPGTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
    /** 지형을 채택하지 않는 세 층 메모리 정의다. / 実地形を採用しない三階のメモリー定義。 */
    FFableTownDefinition RouteFixture()
    {
        FFableTownDefinition Definition;
        Definition.TownId = EFableTownId::Starbough;
        Definition.CatalogVersion = 7;
        Definition.bGeometryReady = true;
        Definition.LocalToWorld = FTransform(FRotator(0, 90, 0), FVector(20000, -30000, 2500));
        Definition.LocalBounds = FBox(FVector(-100, -100, -100), FVector(1100, 1100, 1100));

        for (int32 Index = 0; Index < 3; ++Index)
        {
            FFableTownFloorDefinition Floor;
            Floor.FloorId = FName(*FString::Printf(TEXT("F%d"), Index));
            Floor.LocalBounds =
                FBox(FVector(-100, -100, Index * 500.0 - 50.0), FVector(1100, 1100, Index * 500.0 + 100.0));
            Definition.Floors.Add(Floor);
        }

        FFableTownAnchorDefinition Entry;
        Entry.AnchorId = TEXT("Entry");
        Entry.FloorId = TEXT("F0");
        Entry.LocalSlots.Add(FTransform(FVector::ZeroVector));
        Definition.Anchors.Add(Entry);

        FFableTownAnchorDefinition Guide;
        Guide.AnchorId = TEXT("Service.Board");
        Guide.FloorId = TEXT("F2");
        Guide.LocalSlots.Add(FTransform(FVector(900, 100, 1000)));
        Definition.Anchors.Add(Guide);

        FFableTownConnectorDefinition First;
        First.ConnectorId = TEXT("StairA");
        First.FromFloorId = TEXT("F0");
        First.ToFloorId = TEXT("F1");
        First.bBidirectional = true;
        First.LocalPoints = {FVector(100, 0, 0), FVector(100, 0, 500)};
        First.RadiusCm = 20.0;
        Definition.Connectors.Add(First);

        FFableTownConnectorDefinition Second;
        Second.ConnectorId = TEXT("StairB");
        Second.FromFloorId = TEXT("F1");
        Second.ToFloorId = TEXT("F2");
        Second.bBidirectional = true;
        Second.LocalPoints = {FVector(200, 0, 500), FVector(200, 0, 1000)};
        Second.RadiusCm = 20.0;
        Definition.Connectors.Add(Second);

        // 가까운 입구라도 전체 선분 길이가 길면 선택하지 않아야 한다. / 入口が近くても全長を比較する。
        FFableTownConnectorDefinition Detour;
        Detour.ConnectorId = TEXT("NearButLong");
        Detour.FromFloorId = TEXT("F0");
        Detour.ToFloorId = TEXT("F2");
        Detour.bBidirectional = true;
        Detour.LocalPoints = {FVector(10, 0, 0), FVector(1000, 1000, 500), FVector(900, 100, 1000)};
        Detour.RadiusCm = 20.0;
        Definition.Connectors.Add(Detour);
        return Definition;
    }

    /** 요청 참조는 서버 정의의 버전과 이름만 사용한다. / 参照は定義の版と名前だけを使う。 */
    FFableTownAnchorRef RouteAnchor(const FFableTownDefinition& Definition, FName Name)
    {
        FFableTownAnchorRef Anchor;
        Anchor.TownId = Definition.TownId;
        Anchor.AnchorId = Name;
        Anchor.CatalogVersion = Definition.CatalogVersion;
        return Anchor;
    }

    /** 실패 뒤 이전 안내점·단계가 남지 않는지 검사한다. / 失敗後に前の案内が残らないことを検査する。 */
    bool EmptyRoute(const FFableTownRoute& Route)
    {
        return !Route.Target.IsValid() && Route.SourceFloorId.IsNone() && Route.TargetFloorId.IsNone() &&
               Route.TargetWorldPosition.IsZero() && Route.NextEntryWorldPosition.IsZero() && Route.Steps.IsEmpty() &&
               Route.EstimatedDistanceCm == 0.0;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRouteTransformed, "Fablewood.Native.TownRoutes.TransformedAnchorAndService",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 월드 변환·전체 통로 비용·서비스 앵커가 동일 경로로 연결되는지 검사한다. */
bool FFableTownRouteTransformed::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = RouteFixture();
    const FVector Start = Definition.LocalToWorld.TransformPosition(FVector::ZeroVector);
    const FVector Goal = Definition.LocalToWorld.TransformPosition(FVector(900, 100, 1000));
    FFableTownRoute Route;
    TestTrue(TEXT("Rotated and translated definition resolves the weighted two-connector path"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, RouteAnchor(Definition, TEXT("Service.Board")),
                                                      Route) == EFableTownRouteStatus::Ready &&
                 Route.Steps.Num() == 2 && Route.Steps[0].ConnectorId == FName(TEXT("StairA")) &&
                 Route.Steps[1].ConnectorId == FName(TEXT("StairB")));
    TestTrue(TEXT("First guide point is the current-floor entrance instead of the remote-floor target"),
             Route.SourceFloorId == FName(TEXT("F0")) && Route.TargetFloorId == FName(TEXT("F2")) &&
                 Route.NextEntryWorldPosition.Equals(Definition.LocalToWorld.TransformPosition(FVector(100, 0, 0))) &&
                 Route.TargetWorldPosition.Equals(Goal));
    const double ExpectedDistance =
        100.0 + 500.0 + 100.0 + 500.0 + FVector::Dist(FVector(200, 0, 1000), FVector(900, 100, 1000));
    TestTrue(TEXT("Estimate includes full polyline and same-floor joining distances in world centimeters"),
             FMath::IsNearlyEqual(Route.EstimatedDistanceCm, ExpectedDistance, 0.001));

    FFableTownServiceRef Guide;
    Guide.TownId = Definition.TownId;
    Guide.CatalogVersion = Definition.CatalogVersion;
    Guide.ServiceId = EFableTownServiceId::QuestGuide;
    FFableTownRoute ServiceRoute;
    TestTrue(TEXT("Service wrapper uses the canonical board anchor and the same route"),
             FableTownRoutes::ResolveDefinitionService(Definition, Start, Guide, ServiceRoute) ==
                     EFableTownRouteStatus::Ready &&
                 ServiceRoute.Target.AnchorId == FName(TEXT("Service.Board")) && ServiceRoute.Steps.Num() == 2 &&
                 ServiceRoute.NextEntryWorldPosition.Equals(Route.NextEntryWorldPosition));

    Guide.CatalogVersion -= 1;
    TestTrue(TEXT("Service wrapper rejects a stale reference and clears its previous route"),
             FableTownRoutes::ResolveDefinitionService(Definition, Start, Guide, ServiceRoute) ==
                     EFableTownRouteStatus::TargetUnavailable &&
                 EmptyRoute(ServiceRoute));

    // FName의 대소문자 무시를 독립 코어의 문자열 비교에도 보존한다. / FNameの大小文字規則を保つ。
    Definition.Connectors[0].FromFloorId = TEXT("f0");
    Definition.Connectors[0].ToFloorId = TEXT("f1");
    TestTrue(TEXT("Case variants of valid floor names do not disconnect the route"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, RouteAnchor(Definition, TEXT("service.board")),
                                                      Route) == EFableTownRouteStatus::Ready &&
                 Route.Steps.Num() == 2 && Route.Steps[0].FromFloorId == FName(TEXT("F0")));
    AddInfo(TEXT("Multi-floor geometry uses memory definitions; no new-town art or physical traversal was validated."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRouteDirection, "Fablewood.Native.TownRoutes.DirectionAndUnreachable",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 역방향·일방통행·이전 회랑을 구분하며 실제 캐릭터를 이동시키지 않는다. */
bool FFableTownRouteDirection::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = RouteFixture();
    const FVector Upper = Definition.LocalToWorld.TransformPosition(FVector(900, 100, 1000));
    const FFableTownAnchorRef Entry = RouteAnchor(Definition, TEXT("Entry"));
    FFableTownRoute Route;
    TestTrue(TEXT("Bidirectional connections resolve reverse order with authored names"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Upper, Entry, Route) ==
                     EFableTownRouteStatus::Ready &&
                 Route.Steps.Num() == 2 && Route.Steps[0].ConnectorId == FName(TEXT("StairB")) &&
                 Route.Steps[0].bReverse && Route.Steps[0].FromFloorId == FName(TEXT("F2")) &&
                 Route.Steps[0].ToFloorId == FName(TEXT("F1")) && Route.Steps[1].bReverse);

    for (FFableTownConnectorDefinition& Connector : Definition.Connectors)
    {
        Connector.bBidirectional = false;
    }
    TestTrue(TEXT("One-way connectors do not create a reverse route and clear the old result"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Upper, Entry, Route) ==
                     EFableTownRouteStatus::NoRoute &&
                 EmptyRoute(Route));

    for (FFableTownConnectorDefinition& Connector : Definition.Connectors)
    {
        Connector.FromFloorId = NAME_None;
        Connector.ToFloorId = NAME_None;
    }
    TestTrue(TEXT("Legacy location-only corridors never become implicit floor graph edges"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Upper, Entry, Route) ==
                     EFableTownRouteStatus::NoRoute &&
                 EmptyRoute(Route));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRouteUnavailable, "Fablewood.Native.TownRoutes.UnavailableAndClearedFailure",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 미준비·이전 버전·잘못된 위치는 마지막 성공 결과를 재사용하지 않는다. */
bool FFableTownRouteUnavailable::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = RouteFixture();
    const FVector Start = Definition.LocalToWorld.TransformPosition(FVector::ZeroVector);
    const FFableTownAnchorRef Target = RouteAnchor(Definition, TEXT("Service.Board"));
    FFableTownRoute Route;
    TestTrue(TEXT("Fixture first produces a nonempty route"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, Target, Route) ==
                 EFableTownRouteStatus::Ready);

    Definition.bGeometryReady = false;
    TestTrue(TEXT("Unready geometry returns no guide, even with valid graph metadata"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, Target, Route) ==
                     EFableTownRouteStatus::GeometryUnavailable &&
                 EmptyRoute(Route));
    Definition.bGeometryReady = true;

    FFableTownAnchorRef Stale = Target;
    Stale.CatalogVersion -= 1;
    TestTrue(TEXT("Stale anchor version does not select another target"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, Stale, Route) ==
                     EFableTownRouteStatus::TargetUnavailable &&
                 EmptyRoute(Route));
    Stale = Target;
    Stale.TownId = EFableTownId::LegacyVillage;
    TestTrue(TEXT("Another town does not use the current town anchor"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, Stale, Route) ==
                     EFableTownRouteStatus::TargetUnavailable &&
                 EmptyRoute(Route));
    Stale = RouteAnchor(Definition, TEXT("Missing"));
    TestTrue(TEXT("Missing target has no fallback"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, Stale, Route) ==
                     EFableTownRouteStatus::TargetUnavailable &&
                 EmptyRoute(Route));

    const FVector Corridor = Definition.LocalToWorld.TransformPosition(FVector(100, 0, 250));
    TestTrue(TEXT("Current corridor context is explicitly unsupported in this first stage"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Corridor, Target, Route) ==
                     EFableTownRouteStatus::ConnectorUnavailable &&
                 EmptyRoute(Route));
    TestTrue(TEXT("Nonfinite source position clears the output"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition,
                                                      FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), Target,
                                                      Route) == EFableTownRouteStatus::InvalidLocation &&
                 EmptyRoute(Route));

    Definition.Connectors[0].ToFloorId = TEXT("MissingFloor");
    TestTrue(TEXT("Invalid graph definition is rejected before route lookup"),
             FableTownRoutes::ResolveDefinitionAnchor(Definition, Start, Target, Route) ==
                     EFableTownRouteStatus::InvalidDefinition &&
                 EmptyRoute(Route));
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    FFableTownAnchorRef Legacy;
    Legacy.TownId = EFableTownId::LegacyVillage;
    Legacy.AnchorId = TEXT("Entry");
    Legacy.CatalogVersion = 1;
    TestTrue(TEXT("Production same-floor Legacy route preserves the canonical anchor"),
             FableTownRoutes::ResolveAnchor(Catalog, FVector(-200, -1080, 110), Legacy, Route) ==
                     EFableTownRouteStatus::Ready &&
                 Route.Steps.IsEmpty() && Route.NextEntryWorldPosition.Equals(FVector(0, -1080, 110)));

    TestTrue(TEXT("Refreshing with the previous output target does not erase the input reference"),
             FableTownRoutes::ResolveAnchor(Catalog, FVector(-200, -1080, 110), Route.Target, Route) ==
                     EFableTownRouteStatus::Ready &&
                 Route.Target.AnchorId == FName(TEXT("Entry")));

    TestTrue(TEXT("Reusing both previous output references preserves the exact source point"),
             FableTownRoutes::ResolveAnchor(Catalog, Route.NextEntryWorldPosition, Route.Target, Route) ==
                     EFableTownRouteStatus::Ready &&
                 Route.EstimatedDistanceCm == 0.0 && Route.Target.AnchorId == FName(TEXT("Entry")));

    FFableTownServiceRef Guide;
    Guide.TownId = EFableTownId::LegacyVillage;
    Guide.CatalogVersion = 1;
    Guide.ServiceId = EFableTownServiceId::QuestGuide;
    TestTrue(TEXT("Production service wrapper keeps the existing same-floor board destination"),
             FableTownRoutes::ResolveService(Catalog, FVector(-2300, -2300, 110), Guide, Route) ==
                     EFableTownRouteStatus::Ready &&
                 Route.Steps.IsEmpty() && Route.TargetWorldPosition.Equals(FableRPG::BoardLocation()));
    TestTrue(TEXT("Missing catalog clears a previously successful result"),
             FableTownRoutes::ResolveAnchor(nullptr, Start, Target, Route) ==
                     EFableTownRouteStatus::InvalidDefinition &&
                 EmptyRoute(Route));
    TestTrue(TEXT("Actual unadopted candidate stays unavailable"),
             FableTownRoutes::ResolveAnchor(Catalog, FVector(0, -77000, 104),
                                            RouteAnchor(*Catalog->FindTown(EFableTownId::Sunbrook), TEXT("Entry")),
                                            Route) == EFableTownRouteStatus::GeometryUnavailable &&
                 EmptyRoute(Route));
    return true;
}
#endif
