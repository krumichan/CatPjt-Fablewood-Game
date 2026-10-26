#include "FableTownNavigation.h"
#include "FableTownCatalog.h"
#include "FableRPGTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
    /** 회전된 세 층의 표시 fixture이며 실제 맵을 채택하지 않는다. / 回転三階の表示fixtureで実マップは採用しない。 */
    FFableTownDefinition NavigationGuideFixture()
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

        FFableTownAnchorDefinition Guide;
        Guide.AnchorId = TEXT("Service.Board");
        Guide.FloorId = TEXT("F2");
        Guide.LocalSlots.Add(FTransform(FVector(900, 100, 1000)));

        // 합성 서비스의 지지면 식별자이며 실제 지형 채택을 뜻하지 않는다. / 合成サービスの支持面識別で実地形の採用ではない。
        Guide.ExpectedSupportTag = TEXT("Fixture.Support.F2");
        Guide.ExpectedSupportLocalZ = 1000.0;
        Definition.Anchors.Add(Guide);

        FFableTownAnchorDefinition Merchant;
        Merchant.AnchorId = TEXT("Service.Merchant");
        Merchant.FloorId = TEXT("F0");
        Merchant.LocalSlots.Add(FTransform(FVector(50, 50, 0)));

        // 다른 층의 음성 대조도 유효한 정본 서비스에서 시작한다. / 別階の陰性対照も有効な正本サービスから始める。
        Merchant.ExpectedSupportTag = TEXT("Fixture.Support.F0");
        Merchant.ExpectedSupportLocalZ = 0.0;
        Definition.Anchors.Add(Merchant);

        for (int32 Index = 0; Index < 2; ++Index)
        {
            FFableTownConnectorDefinition Connector;
            Connector.ConnectorId = FName(*FString::Printf(TEXT("Internal_Stair_%d"), Index));
            Connector.FromFloorId = FName(*FString::Printf(TEXT("F%d"), Index));
            Connector.ToFloorId = FName(*FString::Printf(TEXT("F%d"), Index + 1));
            Connector.LocalPoints = {FVector((Index + 1) * 100.0, 0, Index * 500.0),
                                     FVector((Index + 1) * 100.0, 0, (Index + 1) * 500.0)};
            Connector.RadiusCm = 20.0;
            Definition.Connectors.Add(Connector);
        }
        return Definition;
    }

    /** 임의 표시 이름을 시험할 때 모든 참조를 같은 메모리 정의 안에서 바꾼다. / 表示名の検査では全参照を共に変更する。 */
    void RenameNavigationGuideFloor(FFableTownDefinition& Definition, FName Before, FName After)
    {
        for (auto& Floor : Definition.Floors)
        {
            if (Floor.FloorId == Before)
            {
                Floor.FloorId = After;
            }
        }
        for (auto& Anchor : Definition.Anchors)
        {
            if (Anchor.FloorId == Before)
            {
                Anchor.FloorId = After;
            }
        }
        for (auto& Connector : Definition.Connectors)
        {
            if (Connector.FromFloorId == Before)
            {
                Connector.FromFloorId = After;
            }
            if (Connector.ToFloorId == Before)
            {
                Connector.ToFloorId = After;
            }
        }
    }

    /** 실패 뒤 실제 서비스와 표시 안내점이 함께 비었는지 검사한다. / 失敗後のサービスと案内点の初期化を検査する。 */
    bool EmptyNavigationGuide(const FFableTownNavigationGuide& Guide)
    {
        return !Guide.bValid && Guide.Status != EFableTownRouteStatus::Ready && !Guide.Service.IsValid() &&
               !Guide.Service.Anchor.IsValid() && Guide.Service.WorldLocation.IsZero() &&
               Guide.WaypointWorld.IsZero() && Guide.TargetWorld.IsZero() && !Guide.bViaConnector &&
               Guide.ConnectorId.IsNone() && Guide.SourceFloorId.IsNone() && Guide.NextFloorId.IsNone() &&
               Guide.TargetFloorId.IsNone() && Guide.RemainingSteps == 0;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownGuideDestinations,
                                 "Fablewood.Native.TownNavigationGuide.DirectAndRoutedService",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 같은 층의 목적지는 그대로 두고 다른 층의 실제 서비스와 입구를 분리한다. */
bool FFableTownGuideDestinations::RunTest(const FString& Parameters)
{
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    FFableTownNavigationGuide Guide;
    TestTrue(
        TEXT("Legacy service keeps its exact direct destination and base label"),
        FableTownNavigation::ResolveServiceGuide(Catalog, FVector(-2300, -2300, 110), EFableTownServiceId::QuestGuide,
                                                 Guide) == EFableTownRouteStatus::Ready &&
            Guide.bValid && !Guide.bViaConnector && Guide.RemainingSteps == 0 && Guide.ConnectorId.IsNone() &&
            Guide.WaypointWorld.Equals(FableRPG::BoardLocation()) && Guide.TargetWorld.Equals(Guide.WaypointWorld) &&
            FableTownNavigation::GuideLabel(TEXT("안내인 로완"), Guide) == TEXT("안내인 로완") &&
            FableTownNavigation::RouteProgressText(Guide).IsEmpty());

    const FFableTownDefinition Definition = NavigationGuideFixture();
    const FVector Start = Definition.LocalToWorld.TransformPosition(FVector::ZeroVector);
    const FVector Entry = Definition.LocalToWorld.TransformPosition(FVector(100, 0, 0));
    const FVector Target = Definition.LocalToWorld.TransformPosition(FVector(900, 100, 1000));
    // 표시 안내도 기존 정본 서비스 조건을 먼저 만족해야 한다. / 表示案内も既存の正本サービス条件を先に満たす。
    FFableTownServiceRef CanonicalReference;
    CanonicalReference.TownId = Definition.TownId;
    CanonicalReference.ServiceId = EFableTownServiceId::QuestGuide;
    CanonicalReference.CatalogVersion = Definition.CatalogVersion;
    FFableTownServiceDescriptor CanonicalService;
    TestTrue(TEXT("Guide fixture satisfies canonical service support metadata before routing"),
             UFableTownCatalog::ResolveDefinitionService(Definition, CanonicalReference, CanonicalService, true) &&
                 CanonicalService.FloorId == FName(TEXT("F2")) && CanonicalService.WorldLocation.Equals(Target));

    TestTrue(TEXT("Rotated current floor receives the first entrance instead of the remote target"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::Ready &&
                 Guide.bValid && Guide.bViaConnector && Guide.WaypointWorld.Equals(Entry) &&
                 Guide.TargetWorld.Equals(Target) && Guide.Service.WorldLocation.Equals(Target) &&
                 !Guide.WaypointWorld.Equals(Guide.Service.WorldLocation) && Guide.RemainingSteps == 2 &&
                 Guide.SourceFloorId == FName(TEXT("F0")) && Guide.NextFloorId == FName(TEXT("F1")) &&
                 Guide.TargetFloorId == FName(TEXT("F2")) && Guide.Service.FloorId == Guide.TargetFloorId);

    FFableTownLocationContext EntryContext;
    TestTrue(TEXT("The rendered entry is on the physical current floor"),
             UFableTownCatalog::ResolveDefinitionLocation(Definition, Guide.WaypointWorld, EntryContext, true) &&
                 EntryContext.FloorId == Guide.SourceFloorId && EntryContext.ConnectorId.IsNone());
    FFableTownServiceDescriptor StrictService;
    TestTrue(TEXT("Existing service and NPC marker query still declines a different floor"),
             FableTownNavigation::ResolveDefinitionService(Definition, Start, EFableTownServiceId::QuestGuide,
                                                           StrictService) ==
                     EFableTownNavigationStatus::DifferentFloor &&
                 StrictService.WorldLocation.IsZero() && !StrictService.Anchor.IsValid());

    TestTrue(TEXT("Reusing the previous waypoint reference does not lose the source position"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Guide.WaypointWorld,
                                                                EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::Ready &&
                 Guide.WaypointWorld.Equals(Entry));
    TestTrue(TEXT("Arriving on the destination floor changes the same result into direct guidance"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Guide.TargetWorld,
                                                                EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::Ready &&
                 !Guide.bViaConnector && Guide.RemainingSteps == 0 && Guide.WaypointWorld.Equals(Target) &&
                 FableTownNavigation::RouteProgressText(Guide).IsEmpty());
    TestTrue(TEXT("Catalog-level requery preserves an aliased source outside prepared geometry"),
             FableTownNavigation::ResolveServiceGuide(Catalog, Guide.WaypointWorld, EFableTownServiceId::QuestGuide,
                                                      Guide) != EFableTownRouteStatus::Ready &&
                 EmptyNavigationGuide(Guide));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownGuideUnavailable,
                                 "Fablewood.Native.TownNavigationGuide.UnavailableAndClearedOutput",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 경로가 없거나 현재 층이 확정되지 않으면 이전 안내를 표시하지 않는다. */
bool FFableTownGuideUnavailable::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = NavigationGuideFixture();
    const FVector Start = Definition.LocalToWorld.TransformPosition(FVector::ZeroVector);
    const FVector Upper = Definition.LocalToWorld.TransformPosition(FVector(900, 100, 1000));
    FFableTownNavigationGuide Guide;
    TestTrue(TEXT("Fixture initially creates a valid routed guide"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::Ready);
    // 기존 유효 출력을 복사해 태그 누락 시 서비스 안내가 비워지는지 별도로 확인한다. / 有効出力の複製でタグ欠落時の初期化を別に確認する。
    FFableTownDefinition TaglessDefinition = Definition;
    FFableTownAnchorDefinition* TaglessBoard = TaglessDefinition.Anchors.FindByPredicate(
        [](const FFableTownAnchorDefinition& Anchor)
        {
            return Anchor.AnchorId == FName(TEXT("Service.Board"));
        });
    TestNotNull(TEXT("Guide fixture has a board for the missing-support negative control"), TaglessBoard);
    if (TaglessBoard)
    {
        TaglessBoard->ExpectedSupportTag = NAME_None;
        FFableTownNavigationGuide TaglessGuide = Guide;
        TestTrue(TEXT("Missing support metadata rejects the guide and clears its previous destination"),
                 FableTownNavigation::ResolveDefinitionServiceGuide(TaglessDefinition, Start,
                                                                    EFableTownServiceId::QuestGuide, TaglessGuide) ==
                         EFableTownRouteStatus::TargetUnavailable &&
                     EmptyNavigationGuide(TaglessGuide));
    }

    // 역방향 경로 음성 대조는 유효한 상인 앵커를 대상으로 한다. / 逆経路の陰性対照は有効な商人アンカーを対象にする。
    FFableTownServiceRef MerchantReference;
    MerchantReference.TownId = Definition.TownId;
    MerchantReference.ServiceId = EFableTownServiceId::Merchant;
    MerchantReference.CatalogVersion = Definition.CatalogVersion;
    FFableTownServiceDescriptor CanonicalMerchant;
    TestTrue(TEXT("Merchant fixture satisfies canonical support metadata before the one-way rejection"),
             UFableTownCatalog::ResolveDefinitionService(Definition, MerchantReference, CanonicalMerchant, true) &&
                 CanonicalMerchant.FloorId == FName(TEXT("F0")));

    Definition.bGeometryReady = false;
    TestTrue(TEXT("Unready geometry has a reason but no old service or guide data"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::GeometryUnavailable &&
                 EmptyNavigationGuide(Guide) && !FableTownNavigation::GuideLabel(TEXT("old target"), Guide).IsEmpty() &&
                 !FableTownNavigation::GuideLabel(TEXT("old target"), Guide).Contains(TEXT("old target")) &&
                 FableTownNavigation::RouteProgressText(Guide).IsEmpty());
    Definition.bGeometryReady = true;

    for (auto& Connector : Definition.Connectors)
    {
        Connector.bBidirectional = false;
    }
    TestTrue(TEXT("A one-way route cannot display a fabricated reverse entrance"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Upper, EFableTownServiceId::Merchant,
                                                                Guide) == EFableTownRouteStatus::NoRoute &&
                 EmptyNavigationGuide(Guide));
    Definition.Connectors.Empty();
    TestTrue(TEXT("Disconnected floors have no waypoint or progress text"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::NoRoute &&
                 EmptyNavigationGuide(Guide) && FableTownNavigation::RouteProgressText(Guide).IsEmpty());

    Definition = NavigationGuideFixture();
    const FVector Corridor = Definition.LocalToWorld.TransformPosition(FVector(100, 0, 250));
    TestTrue(TEXT("A corridor context explains that guidance resumes on the next floor"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Corridor, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::ConnectorUnavailable &&
                 EmptyNavigationGuide(Guide) &&
                 FableTownNavigation::UnavailableRouteText(Guide.Status).Contains(TEXT("다음 층")));
    TestTrue(TEXT("An invalid service is not replaced with the board"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::Invalid,
                                                                Guide) == EFableTownRouteStatus::TargetUnavailable &&
                 EmptyNavigationGuide(Guide));
    TestTrue(TEXT("NaN current position is rejected without retaining earlier output"),
             FableTownNavigation::ResolveDefinitionServiceGuide(
                 Definition, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0), EFableTownServiceId::QuestGuide,
                 Guide) == EFableTownRouteStatus::InvalidLocation &&
                 EmptyNavigationGuide(Guide));
    Definition.Connectors[0].ToFloorId = NAME_None;
    TestTrue(TEXT("Malformed connection metadata never creates a guide"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::InvalidDefinition &&
                 EmptyNavigationGuide(Guide));
    TestTrue(TEXT("Missing catalog keeps output empty"),
             FableTownNavigation::ResolveServiceGuide(nullptr, Start, EFableTownServiceId::QuestGuide, Guide) ==
                     EFableTownRouteStatus::InvalidDefinition &&
                 EmptyNavigationGuide(Guide));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownGuideLabels, "Fablewood.Native.TownNavigationGuide.LabelsAndCanonicalFloors",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 층 표시·통로 안내는 읽을 수 있는 문구를 사용하고 내부 ID를 노출하지 않는다. */
bool FFableTownGuideLabels::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = NavigationGuideFixture();
    const FVector Start = Definition.LocalToWorld.TransformPosition(FVector::ZeroVector);
    FFableTownNavigationGuide Guide;
    if (!TestTrue(TEXT("Routed guide exists for label checks"),
                  FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                     Guide) == EFableTownRouteStatus::Ready))
    {
        return false;
    }
    const FString Label = FableTownNavigation::GuideLabel(TEXT("안내인 로완"), Guide);
    const FString Progress = FableTownNavigation::RouteProgressText(Guide);
    TestTrue(TEXT("Guide names a passage entrance rather than the service being at the entrance"),
             Label == TEXT("안내인 로완 · 층간 통로 입구") && !Label.Contains(TEXT("Internal_Stair")));
    TestTrue(TEXT("Canonical F indices are displayed as one-based floor numbers and remaining passages"),
             Progress.Contains(TEXT("1층 → 2층")) && Progress.Contains(TEXT("목적지 3층")) &&
                 Progress.Contains(TEXT("남은 통로 2곳")) && !Progress.Contains(TEXT("F0")));

    RenameNavigationGuideFloor(Definition, TEXT("F0"), TEXT("Ground"));
    RenameNavigationGuideFloor(Definition, TEXT("F1"), TEXT("Internal_Mezzanine"));
    RenameNavigationGuideFloor(Definition, TEXT("F2"), TEXT("F999999999999999999999"));
    TestTrue(TEXT("Custom floor names still form a real route"),
             FableTownNavigation::ResolveDefinitionServiceGuide(Definition, Start, EFableTownServiceId::QuestGuide,
                                                                Guide) == EFableTownRouteStatus::Ready);
    const FString SafeProgress = FableTownNavigation::RouteProgressText(Guide);
    TestTrue(TEXT("Ground and unknown or overflowing IDs use readable context without leaking internal names"),
             SafeProgress.Contains(TEXT("지상층 → 다음층")) && SafeProgress.Contains(TEXT("목적지층")) &&
                 !SafeProgress.Contains(TEXT("Internal_Mezzanine")) && !SafeProgress.Contains(TEXT("999999")));

    FFableTownNavigationGuide Invalid = Guide;
    Invalid.bValid = false;
    TestTrue(TEXT("A stale validity flag prevents previous text from being presented"),
             FableTownNavigation::RouteProgressText(Invalid).IsEmpty() &&
                 FableTownNavigation::GuideLabel(TEXT("old target"), Invalid) != TEXT("old target · 층간 통로 입구"));
    Invalid = Guide;
    Invalid.Status = EFableTownRouteStatus::NoRoute;
    TestTrue(TEXT("Status must also be Ready before route progress is shown"),
             FableTownNavigation::RouteProgressText(Invalid).IsEmpty() &&
                 FableTownNavigation::GuideLabel(TEXT("old target"), Invalid) ==
                     FableTownNavigation::UnavailableRouteText(EFableTownRouteStatus::NoRoute));
    return true;
}
#endif
