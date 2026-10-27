#include "FableTownOverlook.h"

#include "FableTownCatalog.h"

namespace
{
    /** 엔진 기본 Cube의 100cm 크기만 한 곳에서 변환한다. / 基本Cubeの100cm寸法を一箇所で変換する。 */
    FTransform CubeBox(const FVector& Center, const FVector& Size)
    {
        return FTransform(FQuat::Identity, Center, Size / 100.0);
    }

    /** 충돌과 화면이 함께 사용할 명시 배치를 추가한다. / 衝突と表示で共有する配置を追加する。 */
    void AddBox(TArray<FFableTownOverlookBox>& Boxes, FName SupportTag, const FVector& Center, const FVector& Size)
    {
        FFableTownOverlookBox Box;
        Box.SupportTag = SupportTag;
        Box.CubeTransform = CubeBox(Center, Size);
        Boxes.Add(MoveTemp(Box));
    }
}

/** 사용자 표시 규칙이 해석할 수 있는 상층 ID다. / 表示規則で解釈できる上階ID。 */
FName FableTownOverlook::FloorId()
{
    return TEXT("F1");
}

/** 서버 정본에서 왕복 경로를 식별한다. / サーバー定義の往復経路を識別する。 */
FName FableTownOverlook::ConnectorId()
{
    return TEXT("Legacy.Overlook.Stairs");
}

/** 계단의 실제 충돌 컴포넌트를 식별한다. / 階段の実衝突コンポーネントを識別する。 */
FName FableTownOverlook::StairSupportTag()
{
    return TEXT("OverlookStairs");
}

/** 상층 앵커의 지원면과 같은 태그다. / 上階アンカーの支持面と同じタグ。 */
FName FableTownOverlook::LandingSupportTag()
{
    return TEXT("OverlookLanding");
}

/** 안전벽을 이동용 바닥과 구분한다. / 安全壁を歩行面から区別する。 */
FName FableTownOverlook::GuardSupportTag()
{
    return TEXT("OverlookGuards");
}

/** 기존 Ground와 X축에서 겹치지 않는 상층 볼륨이다. / 従来GroundとX軸で重ならない上階範囲。 */
FBox FableTownOverlook::UpperFloorBounds()
{
    return FBox(FVector(StairEndX, CenterY - WidthCm * 0.5, SurfaceZ - 10.0),
                FVector(TownEastLimitX, CenterY + WidthCm * 0.5, 1499.0));
}

/** 저장·순간이동 허용과 별개인 표시 전용 전망대 위치다. / 保存・瞬間移動の許可とは別の表示位置。 */
FVector FableTownOverlook::OverlookAnchorPosition()
{
    return FVector(5600.0, CenterY, SurfaceZ + ReferencePawnCenterZ);
}

/** 각 단과 동일한 높이를 사용하는 pawn 중심 경로다. / 各段と同じ高さを使うpawn中心経路。 */
TArray<FVector> FableTownOverlook::ConnectorPoints()
{
    TArray<FVector> Points;
    Points.Reserve(StepCount + 2);
    Points.Add(FVector(StairStartX - 20.0, CenterY, ReferencePawnCenterZ));

    // Ground가 우선인 하단 계단과 실제 connector gap을 같은 점 목록으로 연결한다.
    // Ground優先の下段階段と実connector gapを同じ点列で接続する。
    for (int32 Index = 0; Index < StepCount; ++Index)
    {
        const double CenterX = StairStartX + (Index + 0.5) * StepRunCm;
        const double TopZ = (Index + 1) * StepRiseCm;
        Points.Add(FVector(CenterX, CenterY, TopZ + ReferencePawnCenterZ));
    }

    Points.Add(FVector(StairEndX + StepRunCm, CenterY, SurfaceZ + ReferencePawnCenterZ));
    return Points;
}

/** 계단·상층 발판·양측 및 끝 안전벽의 단일 배치 목록이다. / 階段・上床・両側と端の安全壁の単一定義。 */
TArray<FFableTownOverlookBox> FableTownOverlook::LayoutBoxes()
{
    TArray<FFableTownOverlookBox> Boxes;
    Boxes.Reserve(StepCount * 3 + 4);
    const double SideY = (WidthCm + GuardThicknessCm) * 0.5;

    // 각 단은 지면부터 채운 정적 블록이며 수직 단차는 MaxStepHeight보다 작다.
    // 各段は地面から満たした静的ブロックで段差はMaxStepHeight未満。
    for (int32 Index = 0; Index < StepCount; ++Index)
    {
        const double CenterX = StairStartX + (Index + 0.5) * StepRunCm;
        const double TopZ = (Index + 1) * StepRiseCm;
        AddBox(Boxes, StairSupportTag(), FVector(CenterX, CenterY, TopZ * 0.5), FVector(StepRunCm, WidthCm, TopZ));

        const double WallTopZ = TopZ + GuardHeightCm;
        for (double Side : {-1.0, 1.0})
        {
            AddBox(Boxes, GuardSupportTag(), FVector(CenterX, CenterY + Side * SideY, WallTopZ * 0.5),
                   FVector(StepRunCm, GuardThicknessCm, WallTopZ));
        }
    }

    // 상층 발판과 양옆·끝 벽은 기존 동쪽 경계 밖의 한정된 보행 공간만 연다.
    // 上床と両側・端壁で従来東境界外の限られた歩行空間だけを開く。
    const double LandingLength = LandingEndX - StairEndX;
    const double LandingCenterX = (StairEndX + LandingEndX) * 0.5;
    AddBox(Boxes, LandingSupportTag(), FVector(LandingCenterX, CenterY, SurfaceZ * 0.5),
           FVector(LandingLength, WidthCm, SurfaceZ));

    const double LandingWallTopZ = SurfaceZ + GuardHeightCm;
    for (double Side : {-1.0, 1.0})
    {
        AddBox(Boxes, GuardSupportTag(), FVector(LandingCenterX, CenterY + Side * SideY, LandingWallTopZ * 0.5),
               FVector(LandingLength, GuardThicknessCm, LandingWallTopZ));
    }
    AddBox(Boxes, GuardSupportTag(), FVector(LandingEndX + GuardThicknessCm * 0.5, CenterY, LandingWallTopZ * 0.5),
           FVector(GuardThicknessCm, WidthCm + GuardThicknessCm * 2.0, LandingWallTopZ));
    return Boxes;
}

/** 동쪽 기존 벽에서 통로 폭만 뺀 두 구간을 반환한다. / 既存東壁から通路幅だけを除いた二区間。 */
TArray<FTransform> FableTownOverlook::EastBoundarySections()
{
    constexpr double ExistingHalfLength = 3100.0;
    constexpr double ExistingCenterX = 3010.0;
    constexpr double ExistingCenterZ = 1000.0;
    constexpr double ExistingHeight = 2200.0;
    constexpr double ExistingThickness = 20.0;
    const double GapMinimumY = CenterY - WidthCm * 0.5;
    const double GapMaximumY = CenterY + WidthCm * 0.5;
    TArray<FTransform> Sections;
    Sections.Reserve(2);

    Sections.Add(CubeBox(FVector(ExistingCenterX, (-ExistingHalfLength + GapMinimumY) * 0.5, ExistingCenterZ),
                         FVector(ExistingThickness, GapMinimumY + ExistingHalfLength, ExistingHeight)));
    Sections.Add(CubeBox(FVector(ExistingCenterX, (GapMaximumY + ExistingHalfLength) * 0.5, ExistingCenterZ),
                         FVector(ExistingThickness, ExistingHalfLength - GapMaximumY, ExistingHeight)));
    return Sections;
}

/** 기존 Ground·Entry·서비스와 다른 네 후보를 변경하지 않고 확장한다. / Ground・Entry・サービス・四候補を保つ。 */
bool FableTownOverlook::ExtendLegacyDefinition(FFableTownDefinition& Definition)
{
    const FBox ExistingBounds(FVector(-4500.0, -4500.0, -799.0), FVector(4499.0, 4500.0, 1499.0));
    if (Definition.TownId != EFableTownId::LegacyVillage || Definition.CatalogVersion != 1 ||
        Definition.LocalBounds.Min != ExistingBounds.Min || Definition.LocalBounds.Max != ExistingBounds.Max ||
        Definition.Floors.Num() != 1 || Definition.Floors[0].FloorId != FName(TEXT("Ground")) ||
        Definition.Floors[0].LocalBounds.Min != ExistingBounds.Min ||
        Definition.Floors[0].LocalBounds.Max != ExistingBounds.Max || !Definition.Connectors.IsEmpty())
    {
        return false;
    }

    FFableTownDefinition Extended = Definition;
    Extended.LocalBounds.Max.X = TownEastLimitX;
    FFableTownFloorDefinition Upper;
    Upper.FloorId = FloorId();
    Upper.LocalBounds = UpperFloorBounds();
    Extended.Floors.Add(Upper);

    FFableTownConnectorDefinition Connector;
    Connector.ConnectorId = ConnectorId();
    Connector.LocalPoints = ConnectorPoints();
    Connector.RadiusCm = CorridorRadiusCm;
    Connector.FromFloorId = TEXT("Ground");
    Connector.ToFloorId = FloorId();
    Connector.bBidirectional = true;
    Extended.Connectors.Add(MoveTemp(Connector));

    // Entry 이외 앵커를 저장·순간이동 대상으로 허용하는 규칙은 추가하지 않는다.
    // Entry以外を保存・瞬間移動対象にする規則は追加しない。
    FFableTownAnchorDefinition Viewpoint;
    Viewpoint.AnchorId = TEXT("Waypoint.Overlook");
    Viewpoint.FloorId = FloorId();
    Viewpoint.LocalSlots.Add(FTransform(OverlookAnchorPosition()));
    Viewpoint.ExpectedSupportTag = LandingSupportTag();
    Viewpoint.ExpectedSupportLocalZ = SurfaceZ;
    Extended.Anchors.Add(MoveTemp(Viewpoint));

    FString Error;
    if (!UFableTownCatalog::ValidateDefinition(Extended, Error))
    {
        return false;
    }
    Definition = MoveTemp(Extended);
    return true;
}
