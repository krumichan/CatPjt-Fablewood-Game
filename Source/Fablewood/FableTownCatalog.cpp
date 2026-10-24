#include "FableTownCatalog.h"
#include "FableRPGTypes.h"
#include "Engine/World.h"

namespace
{
    /** 비정상 실수와 비어 있는 범위를 거절한다. / 非有限値と空の範囲を拒否する。 */
    bool FiniteVector(const FVector& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    /** 유한하며 폭이 있는 상자만 인정한다. / 有限で幅のある箱のみを認める。 */
    bool ValidBox(const FBox& Box)
    {
        return Box.IsValid && FiniteVector(Box.Min) && FiniteVector(Box.Max) && Box.Min.X < Box.Max.X &&
               Box.Min.Y < Box.Max.Y && Box.Min.Z < Box.Max.Z;
    }

    /** 경계 포함 여부를 유한 좌표로 검사한다. / 有限座標で境界包含を検査する。 */
    bool Contains(const FBox& Box, const FVector& Point)
    {
        return ValidBox(Box) && FiniteVector(Point) && Box.IsInsideOrOn(Point);
    }

    /** 하위 범위가 전체 영역 안에 있는지 검사한다. / 下位範囲の包含を検査する。 */
    bool ContainsBox(const FBox& Outer, const FBox& Inner)
    {
        return ValidBox(Inner) && Contains(Outer, Inner.Min) && Contains(Outer, Inner.Max);
    }

    /** 단위 축척·수평 회전의 유효 변환만 허용한다. / 単位尺度・水平回転のみ許可する。 */
    bool ValidTransform(const FTransform& Transform)
    {
        const FQuat Rotation = Transform.GetRotation();
        const FVector Scale = Transform.GetScale3D();
        return !Transform.ContainsNaN() && FiniteVector(Transform.GetTranslation()) && FiniteVector(Scale) &&
               Scale.Equals(FVector::OneVector, KINDA_SMALL_NUMBER) && Rotation.IsNormalized() &&
               Rotation.RotateVector(FVector::UpVector).Equals(FVector::UpVector, KINDA_SMALL_NUMBER);
    }

    /** 고정된 논리 앵커에 명시 슬롯만 넣는다. / 固定論理アンカーに明示スロットのみを追加する。 */
    void AddAnchor(FFableTownDefinition& Definition, const TCHAR* Name, const TCHAR* Floor,
                   const TArray<FVector>& Slots)
    {
        FFableTownAnchorDefinition Anchor;
        Anchor.AnchorId = FName(Name);
        Anchor.FloorId = FName(Floor);
        for (const FVector& Slot : Slots)
        {
            Anchor.LocalSlots.Add(FTransform(Slot));
        }
        Definition.Anchors.Add(MoveTemp(Anchor));
    }

    /** 후보 층의 조회 범위이며 실제 발판/연결 경로를 만들어 내지 않는다. / 候補範囲であり床や接続路を生成しない。 */
    FFableTownDefinition Candidate(EFableTownId Id, const TCHAR* Key, const FVector& Origin, const FVector& Minimum,
                                   const FVector& Maximum, const TArray<double>& Floors,
                                   const TArray<FVector>& Arrivals)
    {
        FFableTownDefinition Result;
        Result.TownId = Id;
        Result.LocalToWorld = FTransform(Origin);
        Result.LocalBounds = FBox(Minimum, Maximum);
        Result.GeometryLevel = TSoftObjectPtr<UWorld>(
            FSoftObjectPath(FString::Printf(TEXT("/Game/FablewoodVillagesV001/Runtime/Geometry/%s.%s"), Key, Key)));
        Result.bGeometryReady = false;
        for (int32 Index = 0; Index < Floors.Num(); ++Index)
        {
            FFableTownFloorDefinition Floor;
            Floor.FloorId = FName(*FString::Printf(TEXT("F%d"), Index));
            // 가까운 후보 층은 중간 경계에서 분리한다. / 近接する候補階は中間境界で分離する。
            double MinZ = Floors[Index] - 150.0;
            double MaxZ = Floors[Index] + 350.0;
            if (Index > 0)
            {
                MinZ = FMath::Max(MinZ, (Floors[Index - 1] + Floors[Index]) * 0.5 + 0.5);
            }
            if (Index + 1 < Floors.Num())
            {
                MaxZ = FMath::Min(MaxZ, (Floors[Index] + Floors[Index + 1]) * 0.5 - 0.5);
            }
            Floor.LocalBounds = FBox(FVector(Minimum.X, Minimum.Y, MinZ), FVector(Maximum.X, Maximum.Y, MaxZ));
            Result.Floors.Add(Floor);
        }
        AddAnchor(Result, TEXT("Entry"), TEXT("F0"), Arrivals);
        AddAnchor(Result, TEXT("Return"), TEXT("F0"), Arrivals);
        AddAnchor(Result, TEXT("Recovery.Ground"), TEXT("F0"), Arrivals);
        return Result;
    }
}

/** 기존 지형만 활성화한다. 미채택 네 마을에는 서비스/로드 성공을 부여하지 않는다. / 既存地形のみ有効化する。 */
UFableTownCatalog::UFableTownCatalog()
{
    FFableTownDefinition Legacy;
    Legacy.TownId = EFableTownId::LegacyVillage;
    Legacy.LocalBounds = FBox(FVector(-4500, -4500, -799), FVector(4499, 4500, 1499));
    Legacy.bGeometryReady = true;
    FFableTownFloorDefinition Ground;
    Ground.FloorId = FName(TEXT("Ground"));
    Ground.LocalBounds = Legacy.LocalBounds;
    Legacy.Floors.Add(Ground);
    AddAnchor(Legacy, TEXT("Entry"), TEXT("Ground"), {FVector(0, -1080, 110), FVector(240, -1080, 110)});
    AddAnchor(Legacy, TEXT("Return"), TEXT("Ground"), {FVector(0, -1080, 110), FVector(240, -1080, 110)});
    AddAnchor(Legacy, TEXT("Recovery.Ground"), TEXT("Ground"), {FVector(0, -1080, 110), FVector(240, -1080, 110)});
    AddAnchor(Legacy, TEXT("Service.Board"), TEXT("Ground"), {FableRPG::BoardLocation()});
    AddAnchor(Legacy, TEXT("Service.Forge"), TEXT("Ground"), {FableRPG::ForgeLocation()});
    AddAnchor(Legacy, TEXT("Service.Merchant"), TEXT("Ground"), {FableRPG::MerchantLocation()});
    AddAnchor(Legacy, TEXT("Portal.Expedition"), TEXT("Ground"), {FableRPG::PortalLocation()});
    Definitions.Add(MoveTemp(Legacy));

    // #128 소스 후보 외곽만 반영하며 미승인/비활성 상태를 유지한다. / #128候補外形のみ反映し未承認・無効を維持する。
    Definitions.Add(Candidate(EFableTownId::Sunbrook, TEXT("sunbrook"), FVector(0, -60000, 0),
                              FVector(-25000, -25000, -150), FVector(25000, 25000, 500), {0},
                              {FVector(0, -17000, 104), FVector(220, -17000, 104)}));
    Definitions.Add(Candidate(EFableTownId::Starbough, TEXT("starbough"), FVector(80000, -60000, 0),
                              FVector(-25000, -25000, -150), FVector(25000, 25000, 4000), {0, 1800, 3300, 3600},
                              {FVector(0, -17000, 104), FVector(220, -17000, 104)}));
    Definitions.Add(Candidate(EFableTownId::Snowbloom, TEXT("snowbloom"), FVector(0, -140000, 0),
                              FVector(-45000, -45000, -150), FVector(45000, 45000, 500), {2},
                              {FVector(0, -17000, 104), FVector(220, -17000, 104)}));
    Definitions.Add(Candidate(EFableTownId::Eldergrove, TEXT("eldergrove"), FVector(80000, -140000, 0),
                              FVector(-17158, -21128, -150), FVector(33133, 29175, 9500), {0, 3000, 6000, 9000},
                              {FVector(0, -6900, 104), FVector(200, -6900, 104)}));
}

/** 후보를 포함한 읽기 전용 정의를 반환한다. / 候補を含む読み取り専用定義を返す。 */
const TArray<FFableTownDefinition>& UFableTownCatalog::GetDefinitions() const
{
    return Definitions;
}

/** 명시 ID만 찾아 알 수 없는 값을 거절한다. / 明示IDのみを検索する。 */
const FFableTownDefinition* UFableTownCatalog::FindTown(EFableTownId TownId) const
{
    return Definitions.FindByPredicate(
        [TownId](const FFableTownDefinition& Definition)
        {
            return Definition.TownId == TownId;
        });
}

/** 전체 catalog가 유효하고 지형 활성 상태인지 조회한다. / 全定義の有効性と地形有効状態を照会する。 */
bool UFableTownCatalog::IsGeometryReady(EFableTownId TownId) const
{
    const FFableTownDefinition* Definition = FindTown(TownId);
    FString Error;
    return Definition && Definition->bGeometryReady && Validate(Error);
}

/** 定義を検証してから参照する。 / 정의를 검사한 뒤에만 조회한다. */
bool UFableTownCatalog::ValidateDefinition(const FFableTownDefinition& Definition, FString& OutError)
{
    OutError.Reset();
    if (Definition.TownId < EFableTownId::LegacyVillage || Definition.TownId > EFableTownId::Eldergrove ||
        Definition.CatalogVersion <= 0 || !ValidTransform(Definition.LocalToWorld) ||
        !ValidBox(Definition.LocalBounds) || Definition.Floors.IsEmpty())
    {
        OutError = TEXT("invalid-town-definition");
        return false;
    }

    TSet<FName> FloorIds;
    TArray<FBox> FloorBounds;
    for (const FFableTownFloorDefinition& Floor : Definition.Floors)
    {
        if (Floor.FloorId.IsNone() || FloorIds.Contains(Floor.FloorId) ||
            !ContainsBox(Definition.LocalBounds, Floor.LocalBounds))
        {
            OutError = TEXT("invalid-floor-definition");
            return false;
        }
        for (const FBox& Other : FloorBounds)
        {
            if (Other.Intersect(Floor.LocalBounds))
            {
                OutError = TEXT("overlapping-floor-bounds");
                return false;
            }
        }
        FloorIds.Add(Floor.FloorId);
        FloorBounds.Add(Floor.LocalBounds);
    }

    TSet<FName> ConnectorIds;
    for (const FFableTownConnectorDefinition& Connector : Definition.Connectors)
    {
        if (Connector.ConnectorId.IsNone() || ConnectorIds.Contains(Connector.ConnectorId) ||
            Connector.LocalPoints.Num() < 2 || !FMath::IsFinite(Connector.RadiusCm) || Connector.RadiusCm <= 0)
        {
            OutError = TEXT("invalid-connector-definition");
            return false;
        }
        for (int32 Index = 0; Index < Connector.LocalPoints.Num(); ++Index)
        {
            const FVector Point = Connector.LocalPoints[Index];
            if (!Contains(Definition.LocalBounds, Point) ||
                (Index > 0 && Point.Equals(Connector.LocalPoints[Index - 1], KINDA_SMALL_NUMBER)))
            {
                OutError = TEXT("invalid-connector-point");
                return false;
            }
        }
        ConnectorIds.Add(Connector.ConnectorId);
    }

    TSet<FName> AnchorIds;
    for (const FFableTownAnchorDefinition& Anchor : Definition.Anchors)
    {
        const FFableTownFloorDefinition* Floor = Definition.Floors.FindByPredicate(
            [&Anchor](const FFableTownFloorDefinition& Value)
            {
                return Value.FloorId == Anchor.FloorId;
            });
        if (Anchor.AnchorId.IsNone() || AnchorIds.Contains(Anchor.AnchorId) || !Floor || Anchor.LocalSlots.IsEmpty())
        {
            OutError = TEXT("invalid-anchor-definition");
            return false;
        }
        for (const FTransform& Slot : Anchor.LocalSlots)
        {
            if (!ValidTransform(Slot) || !Contains(Floor->LocalBounds, Slot.GetTranslation()))
            {
                OutError = TEXT("invalid-anchor-slot");
                return false;
            }
        }
        AnchorIds.Add(Anchor.AnchorId);
    }

    return true;
}

/** 전체 catalog의 중복 공간을 거절한다. / catalog全体の重複空間を拒否する。 */
bool UFableTownCatalog::Validate(FString& OutError) const
{
    OutError.Reset();
    if (Definitions.IsEmpty())
    {
        OutError = TEXT("empty-town-catalog");
        return false;
    }

    TSet<EFableTownId> Ids;
    TArray<FBox> WorldBounds;
    for (const FFableTownDefinition& Definition : Definitions)
    {
        if (!ValidateDefinition(Definition, OutError))
        {
            return false;
        }
        if (Ids.Contains(Definition.TownId))
        {
            OutError = TEXT("duplicate-town-id");
            return false;
        }
        const FBox WorldBoundsForTown = Definition.LocalBounds.TransformBy(Definition.LocalToWorld);
        for (const FBox& Other : WorldBounds)
        {
            if (Other.Intersect(WorldBoundsForTown))
            {
                OutError = TEXT("overlapping-town-bounds");
                return false;
            }
        }
        Ids.Add(Definition.TownId);
        WorldBounds.Add(WorldBoundsForTown);
    }

    return true;
}

/** 이 단계의 판정은 유한 정의 일치이며 바닥 물리 검사와 구분한다. / 有限定義との一致を判定し、床物理とは区別する。 */
bool UFableTownCatalog::ResolveDefinitionLocation(const FFableTownDefinition& Definition, const FVector& WorldPosition,
                                                  FFableTownLocationContext& OutContext, bool bRequireGeometryReady)
{
    OutContext = FFableTownLocationContext();
    FString Error;
    if (!FiniteVector(WorldPosition) || !ValidateDefinition(Definition, Error) ||
        (bRequireGeometryReady && !Definition.bGeometryReady))
    {
        return false;
    }

    const FVector Local = Definition.LocalToWorld.InverseTransformPosition(WorldPosition);
    if (!Contains(Definition.LocalBounds, Local))
    {
        return false;
    }

    FName FloorId = NAME_None;
    for (const FFableTownFloorDefinition& Floor : Definition.Floors)
    {
        if (Contains(Floor.LocalBounds, Local))
        {
            if (!FloorId.IsNone())
            {
                return false;
            }
            FloorId = Floor.FloorId;
        }
    }

    FName ConnectorId = NAME_None;
    if (FloorId.IsNone())
    {
        for (const FFableTownConnectorDefinition& Connector : Definition.Connectors)
        {
            bool bInside = false;
            for (int32 Index = 1; Index < Connector.LocalPoints.Num(); ++Index)
            {
                if (FMath::PointDistToSegmentSquared(Local, Connector.LocalPoints[Index - 1],
                                                     Connector.LocalPoints[Index]) <= FMath::Square(Connector.RadiusCm))
                {
                    bInside = true;
                    break;
                }
            }
            if (bInside)
            {
                if (!ConnectorId.IsNone())
                {
                    return false;
                }
                ConnectorId = Connector.ConnectorId;
            }
        }
    }

    if (FloorId.IsNone() && ConnectorId.IsNone())
    {
        return false;
    }

    OutContext.AreaKind = EFableAreaKind::Town;
    OutContext.TownId = Definition.TownId;
    OutContext.FloorId = FloorId;
    OutContext.ConnectorId = ConnectorId;
    OutContext.LocalPosition = Local;
    OutContext.CatalogVersion = Definition.CatalogVersion;
    OutContext.bGeometryReady = Definition.bGeometryReady;
    return true;
}

/** 모호하거나 미활성인 위치를 기본 경로에서 거절한다. / 曖昧・未有効な位置を通常経路で拒否する。 */
bool UFableTownCatalog::ResolveLocation(const FVector& WorldPosition, FFableTownLocationContext& OutContext,
                                        bool bRequireGeometryReady) const
{
    OutContext = FFableTownLocationContext();
    FString Error;
    if (!Validate(Error))
    {
        return false;
    }

    bool bFound = false;
    for (const FFableTownDefinition& Definition : Definitions)
    {
        FFableTownLocationContext CandidateContext;
        if (ResolveDefinitionLocation(Definition, WorldPosition, CandidateContext, bRequireGeometryReady))
        {
            if (bFound)
            {
                OutContext = FFableTownLocationContext();
                return false;
            }
            bFound = true;
            OutContext = CandidateContext;
        }
    }

    return bFound;
}

/** 지역 변환을 정확히 한 번 적용한다. / 地域変換を一度だけ適用する。 */
bool UFableTownCatalog::ToWorld(EFableTownId TownId, const FVector& LocalPosition, FVector& OutWorldPosition) const
{
    OutWorldPosition = FVector::ZeroVector;
    const FFableTownDefinition* Definition = FindTown(TownId);
    FString Error;
    if (!Definition || !FiniteVector(LocalPosition) || !Validate(Error))
    {
        return false;
    }

    OutWorldPosition = Definition->LocalToWorld.TransformPosition(LocalPosition);
    return FiniteVector(OutWorldPosition);
}

/** 역변환을 정확히 한 번 적용한다. / 逆変換を一度だけ適用する。 */
bool UFableTownCatalog::ToLocal(EFableTownId TownId, const FVector& WorldPosition, FVector& OutLocalPosition) const
{
    OutLocalPosition = FVector::ZeroVector;
    const FFableTownDefinition* Definition = FindTown(TownId);
    FString Error;
    if (!Definition || !FiniteVector(WorldPosition) || !Validate(Error))
    {
        return false;
    }

    OutLocalPosition = Definition->LocalToWorld.InverseTransformPosition(WorldPosition);
    return FiniteVector(OutLocalPosition);
}

/** 실제 활성화된 지형의 승인 슬롯만 기본 조회에 반환한다. / 有効地形の承認スロットのみ通常照会に返す。 */
bool UFableTownCatalog::ResolveAnchor(const FFableTownAnchorRef& Anchor, int32 SlotIndex, FTransform& OutWorldTransform,
                                      bool bRequireGeometryReady) const
{
    OutWorldTransform = FTransform::Identity;
    const FFableTownDefinition* Definition = FindTown(Anchor.TownId);
    FString Error;
    if (!Anchor.IsValid() || !Definition || Definition->CatalogVersion != Anchor.CatalogVersion || !Validate(Error) ||
        (bRequireGeometryReady && !Definition->bGeometryReady))
    {
        return false;
    }

    const FFableTownAnchorDefinition* Match = Definition->Anchors.FindByPredicate(
        [&Anchor](const FFableTownAnchorDefinition& Value)
        {
            return Value.AnchorId == Anchor.AnchorId;
        });
    if (!Match || !Match->LocalSlots.IsValidIndex(SlotIndex))
    {
        return false;
    }

    OutWorldTransform = Match->LocalSlots[SlotIndex] * Definition->LocalToWorld;
    return ValidTransform(OutWorldTransform);
}
