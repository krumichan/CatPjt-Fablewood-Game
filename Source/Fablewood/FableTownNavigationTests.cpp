#include "FableTownNavigation.h"
#include "FableTownCatalog.h"
#include "FableRPGTypes.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>

namespace
{
    /** 현재 채택된 마을을 값 복사한 합성 정의이며 실제 새 맵 로드가 아니다. */
    FFableTownDefinition NavigationFixture()
    {
        return *GetDefault<UFableTownCatalog>()->FindTown(EFableTownId::LegacyVillage);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownNavigationLegacy,
                                 "Fablewood.Native.TownNavigation.LegacyLogicalDestinations",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 대화 범위 밖에서도 정본 앵커를 안내하지만 대화 가능 판정은 확장하지 않는다. */
bool FFableTownNavigationLegacy::RunTest(const FString& Parameters)
{
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    const FVector Here(-2300, -2300, 110);
    const EFableTownServiceId Roles[] = {EFableTownServiceId::QuestGuide, EFableTownServiceId::Workshop,
                                         EFableTownServiceId::Merchant, EFableTownServiceId::ExpeditionPortal};
    const FVector Expected[] = {FableRPG::BoardLocation(), FableRPG::ForgeLocation(), FableRPG::MerchantLocation(),
                                FableRPG::PortalLocation()};

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Roles); ++Index)
    {
        FFableTownServiceDescriptor Destination;
        TestTrue(TEXT("Logical destination is available from the same floor outside interaction range"),
                 FableTownNavigation::ResolveService(Catalog, Here, Roles[Index], Destination) ==
                     EFableTownNavigationStatus::Ready);
        TestTrue(TEXT("Role resolves the published Legacy anchor, floor and position"),
                 Destination.Service.TownId == EFableTownId::LegacyVillage &&
                     Destination.FloorId == FName(TEXT("Ground")) && Destination.WorldLocation.Equals(Expected[Index]));
        FFableTownServiceDescriptor Nearby;
        TestFalse(TEXT("Navigation does not extend the existing interaction range"),
                  Catalog->ResolveNearbyService(Here, Roles[Index], Nearby));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownNavigationUnavailable,
                                 "Fablewood.Native.TownNavigation.UnreadyMissingAndInvalid",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 미채택 후보·없는 역할·비유한 위치를 기본 마을 좌표로 대체하지 않는다. */
bool FFableTownNavigationUnavailable::RunTest(const FString& Parameters)
{
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    FFableTownServiceDescriptor Destination;
    Destination.WorldLocation = FableRPG::BoardLocation();
    TestTrue(TEXT("Unadopted Sunbrook is reported unavailable"),
             FableTownNavigation::ResolveService(Catalog, FVector(0, -77000, 104), EFableTownServiceId::QuestGuide,
                                                 Destination) == EFableTownNavigationStatus::GeometryUnavailable);
    TestTrue(TEXT("Unavailable destination clears all prior anchor and position data"),
             !Destination.Anchor.IsValid() && Destination.WorldLocation.IsZero());

    FFableTownDefinition Missing = NavigationFixture();
    Missing.Anchors.RemoveAll(
        [](const FFableTownAnchorDefinition& Anchor)
        {
            return Anchor.AnchorId == FName(TEXT("Service.Board"));
        });
    TestTrue(TEXT("Missing logical guide has no Legacy coordinate fallback"),
             FableTownNavigation::ResolveDefinitionService(Missing, FVector(0, -1080, 110),
                                                           EFableTownServiceId::QuestGuide, Destination) ==
                 EFableTownNavigationStatus::ServiceUnavailable);
    TestTrue(TEXT("Unknown service role is unavailable"),
             FableTownNavigation::ResolveService(Catalog, FVector(0, -1080, 110), EFableTownServiceId::Invalid,
                                                 Destination) == EFableTownNavigationStatus::ServiceUnavailable);
    TestTrue(TEXT("NaN position is invalid and keeps a zero output"),
             FableTownNavigation::ResolveService(Catalog, FVector(std::numeric_limits<double>::quiet_NaN(), 0, 0),
                                                 EFableTownServiceId::QuestGuide,
                                                 Destination) == EFableTownNavigationStatus::InvalidLocation &&
                 Destination.WorldLocation.IsZero() && !Destination.Anchor.IsValid());
    TestFalse(TEXT("Unavailable explanation is not empty"),
              FableTownNavigation::UnavailableText(EFableTownNavigationStatus::GeometryUnavailable).IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownNavigationFloor, "Fablewood.Native.TownNavigation.CurrentFloorAndTransform",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 회전·이동된 합성 마을에서도 같은 층만 안내하며 층을 평면 거리로 무시하지 않는다. */
bool FFableTownNavigationFloor::RunTest(const FString& Parameters)
{
    FFableTownDefinition Definition = NavigationFixture();
    Definition.TownId = EFableTownId::Starbough;
    Definition.LocalToWorld = FTransform(FRotator(0, 90, 0), FVector(25000, -35000, 0));
    Definition.LocalBounds = FBox(FVector(-4500, -4500, -799), FVector(4499, 4500, 3000));
    Definition.Floors[0].LocalBounds = FBox(FVector(-4500, -4500, -799), FVector(4499, 4500, 1499));
    FFableTownFloorDefinition Upper;
    Upper.FloorId = TEXT("Upper");
    Upper.LocalBounds = FBox(FVector(-4500, -4500, 1500), FVector(4499, 4500, 3000));
    Definition.Floors.Add(Upper);

    FFableTownServiceDescriptor Destination;
    const FVector GroundPosition = Definition.LocalToWorld.TransformPosition(FVector(-2300, -2300, 110));
    TestTrue(
        TEXT("Prepared synthetic rotated village uses its own transformed anchor"),
        FableTownNavigation::ResolveDefinitionService(Definition, GroundPosition, EFableTownServiceId::QuestGuide,
                                                      Destination) == EFableTownNavigationStatus::Ready &&
            Destination.Service.TownId == EFableTownId::Starbough &&
            Destination.WorldLocation.Equals(Definition.LocalToWorld.TransformPosition(FableRPG::BoardLocation())));
    const FVector UpperPosition = Definition.LocalToWorld.TransformPosition(FVector(-2300, -2300, 1800));
    TestTrue(TEXT("Different floor is explicit even when the planar position is identical"),
             FableTownNavigation::ResolveDefinitionService(Definition, UpperPosition, EFableTownServiceId::QuestGuide,
                                                           Destination) == EFableTownNavigationStatus::DifferentFloor &&
                 !Destination.Anchor.IsValid() && Destination.WorldLocation.IsZero());
    TestFalse(TEXT("Cross-floor explanation is not empty"),
              FableTownNavigation::UnavailableText(EFableTownNavigationStatus::DifferentFloor).IsEmpty());
    TestFalse(TEXT("Different towns never share a minimap floor"),
              FableTownNavigation::IsSameFloor(GetDefault<UFableTownCatalog>(), FVector(0, -1080, 110),
                                               FVector(0, -77000, 104)));

    AddInfo(TEXT("Rotated village and upper-floor checks use a memory definition, not an adopted new-town map."));
    return true;
}
#endif
