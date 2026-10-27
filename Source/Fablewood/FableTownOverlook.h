#pragma once

#include "CoreMinimal.h"

struct FFableTownDefinition;

/** 같은 100cm Cube 배치를 화면과 실제 충돌 검증에서 공유한다. / 同じCube配置を表示と実衝突検証で共有する。 */
struct FABLEWOOD_API FFableTownOverlookBox
{
    FName SupportTag = NAME_None;
    FTransform CubeTransform = FTransform::Identity;
};

/** 기존 마을의 동쪽 전망대 한 구간만 정의한다. / 既存村の東側展望台一区間だけを定義する。 */
namespace FableTownOverlook
{
    inline constexpr int32 StepCount = 20;
    inline constexpr double StepRiseCm = 24.0;
    inline constexpr double StepRunCm = 100.0;
    inline constexpr double StairStartX = 3000.0;
    inline constexpr double StairEndX = StairStartX + StepCount * StepRunCm;
    inline constexpr double CenterY = -2000.0;
    inline constexpr double WidthCm = 500.0;
    inline constexpr double LandingEndX = 6100.0;
    inline constexpr double SurfaceZ = StepCount * StepRiseCm;
    inline constexpr double GuardHeightCm = 220.0;
    inline constexpr double GuardThicknessCm = 20.0;
    inline constexpr double TownEastLimitX = 6200.0;
    inline constexpr double CorridorRadiusCm = 240.0;
    inline constexpr double ReferencePawnCenterZ = 104.0;

    /** 사용자 표시 규칙이 해석할 수 있는 상층 ID다. / 表示規則で解釈できる上階ID。 */
    FABLEWOOD_API FName FloorId();

    /** 서버 정본에서 왕복 경로를 식별한다. / サーバー定義の往復経路を識別する。 */
    FABLEWOOD_API FName ConnectorId();

    /** 계단의 실제 충돌 컴포넌트를 식별한다. / 階段の実衝突コンポーネントを識別する。 */
    FABLEWOOD_API FName StairSupportTag();

    /** 상층 앵커의 지원면과 같은 태그다. / 上階アンカーの支持面と同じタグ。 */
    FABLEWOOD_API FName LandingSupportTag();

    /** 안전벽을 이동용 바닥과 구분한다. / 安全壁を歩行面から区別する。 */
    FABLEWOOD_API FName GuardSupportTag();

    /** 기존 Ground와 X축에서 겹치지 않는 상층 볼륨이다. / 従来GroundとX軸で重ならない上階範囲。 */
    FABLEWOOD_API FBox UpperFloorBounds();

    /** 저장·순간이동 허용과 별개인 표시 전용 전망대 위치다. / 保存・瞬間移動の許可とは別の表示位置。 */
    FABLEWOOD_API FVector OverlookAnchorPosition();

    /** 각 단과 동일한 높이를 사용하는 pawn 중심 경로다. / 各段と同じ高さを使うpawn中心経路。 */
    FABLEWOOD_API TArray<FVector> ConnectorPoints();

    /** 계단·상층 발판·양측 및 끝 안전벽의 단일 배치 목록이다. / 階段・上床・両側と端の安全壁の単一定義。 */
    FABLEWOOD_API TArray<FFableTownOverlookBox> LayoutBoxes();

    /** 동쪽 기존 벽에서 통로 폭만 뺀 두 구간을 반환한다. / 既存東壁から通路幅だけを除いた二区間。 */
    FABLEWOOD_API TArray<FTransform> EastBoundarySections();

    /** 기존 Ground·Entry·서비스와 다른 네 후보를 변경하지 않고 확장한다. / Ground・Entry・サービス・四候補を保つ。 */
    FABLEWOOD_API bool ExtendLegacyDefinition(FFableTownDefinition& Definition);
}
