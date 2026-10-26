#include "FableTownCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
    /** 실제 후보 지형과 분리한 두 층 메모리 정의다. / 実候補地形と分けた二階のメモリー定義。 */
    FFableTownDefinition RouteDefinitionFixture()
    {
        FFableTownDefinition Definition;
        Definition.TownId = EFableTownId::Starbough;
        Definition.CatalogVersion = 2;
        Definition.LocalBounds = FBox(FVector(-1000, -1000, -50), FVector(1000, 1000, 400));
        Definition.bGeometryReady = true;

        FFableTownFloorDefinition Ground;
        Ground.FloorId = TEXT("Ground");
        Ground.LocalBounds = FBox(FVector(-1000, -1000, 0), FVector(1000, 1000, 100));
        Definition.Floors.Add(Ground);

        FFableTownFloorDefinition Upper;
        Upper.FloorId = TEXT("Upper");
        Upper.LocalBounds = FBox(FVector(-1000, -1000, 200), FVector(1000, 1000, 300));
        Definition.Floors.Add(Upper);

        FFableTownConnectorDefinition Connector;
        Connector.ConnectorId = TEXT("Stairs");
        Connector.LocalPoints = {FVector(0, 0, 50), FVector(0, 0, 150), FVector(0, 0, 250)};
        Connector.RadiusCm = 40;
        Definition.Connectors.Add(Connector);
        return Definition;
    }

    /** 추가되는 층 메타데이터만 명시하며 지형을 로드하지 않는다. / 階メタデータだけを指定し地形はロードしない。 */
    FFableTownDefinition RoutedDefinitionFixture()
    {
        FFableTownDefinition Definition = RouteDefinitionFixture();
        Definition.Connectors[0].FromFloorId = TEXT("Ground");
        Definition.Connectors[0].ToFloorId = TEXT("Upper");
        return Definition;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRouteConnectorCompatibility,
                                 "Fablewood.Native.TownRoutes.ConnectorMetadataCompatibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 기존 위치 전용 회랑과 명시한 양방향·단방향 연결의 유효성을 함께 보존한다. */
bool FFableTownRouteConnectorCompatibility::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = RouteDefinitionFixture();
    FString Error;
    TestTrue(TEXT("Legacy location-only corridor remains valid with both floor IDs empty"),
             UFableTownCatalog::ValidateDefinition(Definition, Error));
    TestTrue(TEXT("New direction metadata defaults to bidirectional"), Definition.Connectors[0].bBidirectional);

    FFableTownLocationContext Location;
    TestTrue(TEXT("Existing corridor location lookup still resolves the space between floors"),
             UFableTownCatalog::ResolveDefinitionLocation(Definition, FVector(0, 0, 150), Location) &&
                 Location.ConnectorId == FName(TEXT("Stairs")) && Location.FloorId.IsNone());
    TestTrue(TEXT("Legacy lookup never infers endpoint floor metadata"),
             Definition.Connectors[0].FromFloorId.IsNone() && Definition.Connectors[0].ToFloorId.IsNone());

    Definition = RoutedDefinitionFixture();
    TestTrue(TEXT("Explicit two-floor bidirectional connector is valid"),
             UFableTownCatalog::ValidateDefinition(Definition, Error));
    Definition.Connectors[0].bBidirectional = false;
    TestTrue(TEXT("Explicit forward-only connector is also valid"),
             UFableTownCatalog::ValidateDefinition(Definition, Error));
    TestFalse(TEXT("Validation preserves the authored forward-only direction"),
              Definition.Connectors[0].bBidirectional);

    const UFableTownCatalog* Published = GetDefault<UFableTownCatalog>();
    for (EFableTownId TownId :
         {EFableTownId::Sunbrook, EFableTownId::Starbough, EFableTownId::Snowbloom, EFableTownId::Eldergrove})
    {
        const FFableTownDefinition* Candidate = Published->FindTown(TownId);
        TestTrue(TEXT("Route fixtures do not activate any published candidate town"),
                 Candidate && !Candidate->bGeometryReady);
    }

    AddInfo(TEXT("Both floors and the corridor are memory definitions; no new-town map was loaded or adopted."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRouteConnectorFloorReferences,
                                 "Fablewood.Native.TownRoutes.ConnectorFloorReferences",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 반쪽 참조·없는 층·동일 층의 연결을 거절한다. / 不完全・存在しない・同じ階への接続を拒否する。 */
bool FFableTownRouteConnectorFloorReferences::RunTest(const FString& Parameters)
{
    const FName FromFloors[] = {NAME_None, TEXT("Ground"), TEXT("Missing"), TEXT("Ground"), TEXT("Ground")};
    const FName ToFloors[] = {TEXT("Upper"), NAME_None, TEXT("Upper"), TEXT("Missing"), TEXT("Ground")};

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(FromFloors); ++Index)
    {
        FFableTownDefinition Definition = RoutedDefinitionFixture();
        Definition.Connectors[0].FromFloorId = FromFloors[Index];
        Definition.Connectors[0].ToFloorId = ToFloors[Index];
        FString Error;
        TestFalse(FString::Printf(TEXT("Invalid endpoint floor reference %d is rejected"), Index),
                  UFableTownCatalog::ValidateDefinition(Definition, Error));
        TestEqual(TEXT("Rejection explains the floor reference problem"), Error,
                  FString(TEXT("invalid-connector-floor-reference")));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownRouteConnectorEndpoints,
                                 "Fablewood.Native.TownRoutes.ConnectorEndpointOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 각 끝점이 명시한 층 안에 있어야 하며 기존 점·반경 검사도 유지한다. */
bool FFableTownRouteConnectorEndpoints::RunTest(const FString& Parameters)
{
    FString Error;
    FFableTownDefinition Definition = RoutedDefinitionFixture();
    Definition.Connectors[0].FromFloorId = TEXT("Upper");
    Definition.Connectors[0].ToFloorId = TEXT("Ground");
    TestFalse(TEXT("Opposite floor names do not reverse the authored point sequence automatically"),
              UFableTownCatalog::ValidateDefinition(Definition, Error));
    TestEqual(TEXT("Swapped endpoint ownership is explicit"), Error, FString(TEXT("invalid-connector-endpoint")));

    Definition = RoutedDefinitionFixture();
    Definition.Connectors[0].LocalPoints[0].Z = 150;
    TestFalse(TEXT("Start point inside the town but outside its declared floor is rejected"),
              UFableTownCatalog::ValidateDefinition(Definition, Error));
    Definition = RoutedDefinitionFixture();
    Definition.Connectors[0].LocalPoints.Last().Z = 350;
    TestFalse(TEXT("End point inside the town but outside its declared floor is rejected"),
              UFableTownCatalog::ValidateDefinition(Definition, Error));

    Definition = RoutedDefinitionFixture();
    Definition.Connectors[0].LocalPoints[0].Z = 100;
    Definition.Connectors[0].LocalPoints.Last().Z = 200;
    TestTrue(TEXT("Finite first and last points on their declared floor boundaries are accepted"),
             UFableTownCatalog::ValidateDefinition(Definition, Error));

    Definition.Connectors[0].LocalPoints.Last().X = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Floor metadata never bypasses the original finite-point guard"),
              UFableTownCatalog::ValidateDefinition(Definition, Error));
    Definition = RoutedDefinitionFixture();
    Definition.Connectors[0].RadiusCm = 0;
    TestFalse(TEXT("Floor metadata never bypasses the original positive-radius guard"),
              UFableTownCatalog::ValidateDefinition(Definition, Error));

    return true;
}
#endif
