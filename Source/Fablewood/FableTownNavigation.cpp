#include "FableTownNavigation.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"
#include "GameFramework/Actor.h"

/** 현재 world의 정본 조회는 서버 상태를 변경하지 않는다. / 定義の照会はサーバー状態を変更しない。 */
const UFableTownCatalog* FableTownNavigation::CatalogFor(const AActor* Context)
{
    const AFableTownManager* Towns = Context ? AFableTownManager::Find(Context->GetWorld()) : nullptr;
    return Towns ? Towns->GetCatalog() : GetDefault<UFableTownCatalog>();
}

/** 서비스 거리 제한은 대화용이며 길안내는 같은 층 전체에서 가능하다. / 会話距離と同じ階全体の案内を分ける。 */
EFableTownNavigationStatus FableTownNavigation::ResolveDefinitionService(const FFableTownDefinition& Definition,
                                                                         const FVector& WorldPosition,
                                                                         EFableTownServiceId ServiceId,
                                                                         FFableTownServiceDescriptor& OutService)
{
    OutService = FFableTownServiceDescriptor();
    FFableTownLocationContext Physical;
    if (!UFableTownCatalog::ResolveDefinitionLocation(Definition, WorldPosition, Physical, false))
    {
        return EFableTownNavigationStatus::InvalidLocation;
    }

    // 미채택 지형은 위치 후보만 존재하며 플레이 목적지로 사용하지 않는다. / 未採用地形は目的地にしない。
    if (!Physical.bGeometryReady)
    {
        return EFableTownNavigationStatus::GeometryUnavailable;
    }

    FFableTownServiceRef Reference;
    Reference.TownId = Physical.TownId;
    Reference.ServiceId = ServiceId;
    Reference.CatalogVersion = Physical.CatalogVersion;
    FFableTownServiceDescriptor Resolved;
    if (!UFableTownCatalog::ResolveDefinitionService(Definition, Reference, Resolved, true))
    {
        return EFableTownNavigationStatus::ServiceUnavailable;
    }

    // 층간 경로가 채택되기 전에는 수평 화살표로 다른 층을 가리키지 않는다. / 階間経路なしで別階を指さない。
    if (Physical.FloorId != Resolved.FloorId || !Physical.ConnectorId.IsNone())
    {
        return EFableTownNavigationStatus::DifferentFloor;
    }

    OutService = Resolved;
    return EFableTownNavigationStatus::Ready;
}

/** 전체 정본의 유효성과 유일한 현재 마을을 확인한 뒤 논리 역할을 해석한다. / 定義全体と現在の村を確認する。 */
EFableTownNavigationStatus FableTownNavigation::ResolveService(const UFableTownCatalog* Catalog,
                                                               const FVector& WorldPosition,
                                                               EFableTownServiceId ServiceId,
                                                               FFableTownServiceDescriptor& OutService)
{
    OutService = FFableTownServiceDescriptor();
    FFableTownLocationContext Physical;
    if (!Catalog || !Catalog->ResolveLocation(WorldPosition, Physical, false))
    {
        return EFableTownNavigationStatus::InvalidLocation;
    }

    const FFableTownDefinition* Definition = Catalog->FindTown(Physical.TownId);
    return Definition ? ResolveDefinitionService(*Definition, WorldPosition, ServiceId, OutService)
                      : EFableTownNavigationStatus::InvalidLocation;
}

/** 정본 버전까지 같은 유효한 바닥만 동일 평면으로 표시한다. / 定義版まで同じ有効な床だけを同一平面とする。 */
bool FableTownNavigation::IsSameFloor(const UFableTownCatalog* Catalog, const FVector& FirstWorldPosition,
                                      const FVector& SecondWorldPosition)
{
    FFableTownLocationContext First;
    FFableTownLocationContext Second;
    return Catalog && Catalog->ResolveLocation(FirstWorldPosition, First, true) &&
           Catalog->ResolveLocation(SecondWorldPosition, Second, true) && First.TownId == Second.TownId &&
           First.CatalogVersion == Second.CatalogVersion && First.FloorId == Second.FloorId &&
           !First.FloorId.IsNone() && First.ConnectorId.IsNone() && Second.ConnectorId.IsNone();
}

/** 잘못된 좌표 대신 사용자가 다음 행동을 판단할 수 있는 짧은 상태를 반환한다. */
FString FableTownNavigation::UnavailableText(EFableTownNavigationStatus Status)
{
    switch (Status)
    {
    case EFableTownNavigationStatus::GeometryUnavailable:
        return TEXT("현재 마을을 이용할 수 없습니다");
    case EFableTownNavigationStatus::ServiceUnavailable:
        return TEXT("현재 마을에는 이 목적지가 준비되지 않았습니다");
    case EFableTownNavigationStatus::DifferentFloor:
        return TEXT("목적지가 다른 층에 있습니다 · 같은 층으로 이동하세요");
    case EFableTownNavigationStatus::InvalidLocation:
        return TEXT("현재 위치에서 목적지를 안내할 수 없습니다");
    default:
        return FString();
    }
}
