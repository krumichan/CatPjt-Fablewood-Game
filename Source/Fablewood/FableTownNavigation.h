#pragma once

#include "CoreMinimal.h"
#include "FableTownServiceTypes.h"

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
}
