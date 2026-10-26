#pragma once

#include "CoreMinimal.h"
#include "FableTownServiceTypes.h"
#include "FableTownRoutes.h"

class AActor;
class UFableTownCatalog;
struct FFableTownDefinition;

/** 표시 불가 이유를 좌표와 분리한다. / 表示不可の理由を座標から分離する。 */
enum class EFableTownNavigationStatus : uint8
{
    Ready,
    InvalidLocation,
    GeometryUnavailable,
    ServiceUnavailable,
    DifferentFloor
};

/** 실제 서비스와 화면의 다음 안내점을 분리한다. / 実サービスと画面の次案内点を分ける。 */
struct FABLEWOOD_API FFableTownNavigationGuide
{
    bool bValid = false;
    EFableTownRouteStatus Status = EFableTownRouteStatus::InvalidLocation;
    FFableTownServiceDescriptor Service;
    FVector WaypointWorld = FVector::ZeroVector;
    FVector TargetWorld = FVector::ZeroVector;
    bool bViaConnector = false;
    FName ConnectorId = NAME_None;
    FName SourceFloorId = NAME_None;
    FName NextFloorId = NAME_None;
    FName TargetFloorId = NAME_None;
    int32 RemainingSteps = 0;
};

/** 안내는 정본 조회만 하며 상호작용·이동·저장을 승인하지 않는다. / 案内は定義照会のみで操作を承認しない。 */
namespace FableTownNavigation
{
    /** 현재 world의 정본을 읽고 manager가 없는 표시 fixture에는 기본 정본을 사용한다. */
    FABLEWOOD_API const UFableTownCatalog* CatalogFor(const AActor* Context);

    /** 거리와 무관하게 현재 마을·층의 서비스 앵커만 안내한다. / 距離によらず現在の村・階のアンカーだけを案内する。 */
    FABLEWOOD_API EFableTownNavigationStatus ResolveService(const UFableTownCatalog* Catalog,
                                                            const FVector& WorldPosition, EFableTownServiceId ServiceId,
                                                            FFableTownServiceDescriptor& OutService);

    /** 합성 정의도 production과 동일한 층·준비 검사를 사용한다. / 合成定義も同じ階・準備検査を使う。 */
    FABLEWOOD_API EFableTownNavigationStatus ResolveDefinitionService(const FFableTownDefinition& Definition,
                                                                      const FVector& WorldPosition,
                                                                      EFableTownServiceId ServiceId,
                                                                      FFableTownServiceDescriptor& OutService);

    /** 다른 마을·층·연결 통로의 표식을 같은 평면에 겹쳐 그리지 않는다. / 別の村・階・通路の印を重ねない。 */
    FABLEWOOD_API bool IsSameFloor(const UFableTownCatalog* Catalog, const FVector& FirstWorldPosition,
                                   const FVector& SecondWorldPosition);

    /** 안내 불가를 오래된 마을 좌표로 대체하지 않고 이유를 표시한다. / 古い村座標へ置き換えず理由を表示する。 */
    FABLEWOOD_API FString UnavailableText(EFableTownNavigationStatus Status);

    /** 정본 서비스 위치를 보존하면서 현재 층의 다음 안내점을 구한다. / 実サービス位置を保ち次案内点を求める。 */
    FABLEWOOD_API EFableTownRouteStatus ResolveDefinitionServiceGuide(const FFableTownDefinition& Definition,
                                                                      const FVector& WorldPosition,
                                                                      EFableTownServiceId ServiceId,
                                                                      FFableTownNavigationGuide& OutGuide);

    /** 전체 카탈로그와 현재 마을을 확인한 뒤 화면 안내만 계산한다. / 全カタログと現在の村を確認して案内だけを計算する。 */
    FABLEWOOD_API EFableTownRouteStatus ResolveServiceGuide(const UFableTownCatalog* Catalog,
                                                            const FVector& WorldPosition, EFableTownServiceId ServiceId,
                                                            FFableTownNavigationGuide& OutGuide);

    /** 같은 층 문구는 그대로 두고 다른 층은 통로 입구임을 표시한다. / 同階の文言を保ち別階は通路入口と示す。 */
    FABLEWOOD_API FString GuideLabel(const FString& BaseLabel, const FFableTownNavigationGuide& Guide);

    /** 내부 ID 대신 사람이 읽을 층과 남은 통로 수를 표시한다. / 内部IDではなく階と残る通路数を表示する。 */
    FABLEWOOD_API FString RouteProgressText(const FFableTownNavigationGuide& Guide);

    /** 실패 원인을 표시하며 이전 목적지나 가짜 진행률을 만들지 않는다. / 失敗理由だけを示し旧目標や偽進捗を作らない。 */
    FABLEWOOD_API FString UnavailableRouteText(EFableTownRouteStatus Status);
}
