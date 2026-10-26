#pragma once

#include "CoreMinimal.h"
#include "FableTownServiceTypes.h"

class UFableTownCatalog;
struct FFableTownDefinition;

/** 경로 조회 실패를 이동 성공과 구분한다. / 経路照会の失敗を移動成功と区別する。 */
enum class EFableTownRouteStatus : uint8
{
    Ready,
    InvalidDefinition,
    InvalidLocation,
    GeometryUnavailable,
    TargetUnavailable,
    ConnectorUnavailable,
    NoRoute
};

/** 한 연결통로의 방향과 양 끝 안내점이다. / 一つの接続通路の方向と両端の案内点。 */
struct FABLEWOOD_API FFableTownRouteStep
{
    FName ConnectorId = NAME_None;
    FName FromFloorId = NAME_None;
    FName ToFloorId = NAME_None;
    bool bReverse = false;
    FVector EntryWorldPosition = FVector::ZeroVector;
    FVector ExitWorldPosition = FVector::ZeroVector;
};

/** 표시용 논리 경로이며 실제 보행·충돌·이동 허가는 아니다. / 論理案内であり実歩行・衝突・移動許可ではない。 */
struct FABLEWOOD_API FFableTownRoute
{
    FFableTownAnchorRef Target;
    FName SourceFloorId = NAME_None;
    FName TargetFloorId = NAME_None;
    FVector TargetWorldPosition = FVector::ZeroVector;
    FVector NextEntryWorldPosition = FVector::ZeroVector;
    TArray<FFableTownRouteStep> Steps;
    double EstimatedDistanceCm = 0.0;
};

/** 서버 정본의 명시 연결만 읽는다. 로드·이동·저장·서비스 판정은 변경하지 않는다. */
namespace FableTownRoutes
{
    /** 단일 정의에서도 동일한 준비·버전·위치 검사를 적용한다. / 単一定義でも同じ検査を適用する。 */
    FABLEWOOD_API EFableTownRouteStatus ResolveDefinitionAnchor(const FFableTownDefinition& Definition,
                                                                const FVector& WorldPosition,
                                                                const FFableTownAnchorRef& Anchor,
                                                                FFableTownRoute& OutRoute);

    /** 전체 정본이 유효한 현재 마을의 앵커만 조회한다. / 全定義が有効な現在の村だけを照会する。 */
    FABLEWOOD_API EFableTownRouteStatus ResolveAnchor(const UFableTownCatalog* Catalog, const FVector& WorldPosition,
                                                      const FFableTownAnchorRef& Anchor, FFableTownRoute& OutRoute);

    /** 서비스는 정본 앵커로 해석하며 호출자의 좌표를 받지 않는다. / サービスは正本アンカーへ解決する。 */
    FABLEWOOD_API EFableTownRouteStatus ResolveDefinitionService(const FFableTownDefinition& Definition,
                                                                 const FVector& WorldPosition,
                                                                 const FFableTownServiceRef& Service,
                                                                 FFableTownRoute& OutRoute);

    /** 현재 마을 밖의 서비스나 오래된 버전을 대체하지 않는다. / 別の村や古い版を代替しない。 */
    FABLEWOOD_API EFableTownRouteStatus ResolveService(const UFableTownCatalog* Catalog, const FVector& WorldPosition,
                                                       const FFableTownServiceRef& Service, FFableTownRoute& OutRoute);
}
