#include "FableTownRoutes.h"
#include "FableTownCatalog.h"
#include "FableTownRouteCore.h"
#include <utility>

namespace
{
    /** FName과 독립 코어가 같은 식별자 비교를 하도록 정규화한다. / FNameの比較規則へ揃える。 */
    std::string RouteKey(FName Name)
    {
        const FString Lower = Name.ToString().ToLower();
        return std::string(TCHAR_TO_UTF8(*Lower));
    }

    /** 변환 후에도 비유한 안내 좌표를 거절한다. / 変換後の非有限座標も拒否する。 */
    bool FiniteRoutePoint(const FVector& Position)
    {
        return FMath::IsFinite(Position.X) && FMath::IsFinite(Position.Y) && FMath::IsFinite(Position.Z);
    }

    /** UE 좌표의 월드 단위를 독립 코어에 유지한다. / ワールド単位を独立コアへ保つ。 */
    FableTownRouteCore::Point CorePoint(const FVector& Position)
    {
        return {Position.X, Position.Y, Position.Z};
    }

    /** 조회된 층의 정본 이름을 결과에 사용한다. / 結果には階の正本名を使う。 */
    FName CanonicalFloor(const FFableTownDefinition& Definition, FName FloorId)
    {
        const FFableTownFloorDefinition* Floor = Definition.Floors.FindByPredicate(
            [FloorId](const FFableTownFloorDefinition& Candidate)
            {
                return Candidate.FloorId == FloorId;
            });
        return Floor ? Floor->FloorId : NAME_None;
    }

    /** 준비 검사를 생략하는 플래그 없이 현재 층만 확정한다. / 準備検査を省略せず現在階を確定する。 */
    EFableTownRouteStatus ResolveStart(const FFableTownDefinition& Definition, const FVector& WorldPosition,
                                       FFableTownLocationContext& OutPhysical)
    {
        FString Error;
        if (!UFableTownCatalog::ValidateDefinition(Definition, Error))
        {
            return EFableTownRouteStatus::InvalidDefinition;
        }

        if (!UFableTownCatalog::ResolveDefinitionLocation(Definition, WorldPosition, OutPhysical, false))
        {
            return EFableTownRouteStatus::InvalidLocation;
        }

        if (!OutPhysical.bGeometryReady)
        {
            return EFableTownRouteStatus::GeometryUnavailable;
        }

        // 통로 내부의 다음 점 추적은 실제 이동 구현과 함께 별도로 검증한다. / 通路内の追跡は別段階とする。
        if (OutPhysical.FloorId.IsNone() || !OutPhysical.ConnectorId.IsNone())
        {
            return EFableTownRouteStatus::ConnectorUnavailable;
        }
        return EFableTownRouteStatus::Ready;
    }

    /** 전체 정본의 중복 공간·오류를 통과해야 현재 마을을 선택한다. / 全定義を検査してから現在の村を選ぶ。 */
    EFableTownRouteStatus CurrentDefinition(const UFableTownCatalog* Catalog, const FVector& WorldPosition,
                                            const FFableTownDefinition*& OutDefinition)
    {
        OutDefinition = nullptr;
        FString Error;
        if (!Catalog || !Catalog->Validate(Error))
        {
            return EFableTownRouteStatus::InvalidDefinition;
        }

        FFableTownLocationContext Physical;
        if (!Catalog->ResolveLocation(WorldPosition, Physical, false))
        {
            return EFableTownRouteStatus::InvalidLocation;
        }

        OutDefinition = Catalog->FindTown(Physical.TownId);
        return OutDefinition ? EFableTownRouteStatus::Ready : EFableTownRouteStatus::InvalidDefinition;
    }
}

/** 정본의 명시 통로를 월드 좌표로 변환한 뒤 안내 비용만 계산한다. / 明示通路を変換して案内費用だけを計算する。 */
EFableTownRouteStatus FableTownRoutes::ResolveDefinitionAnchor(const FFableTownDefinition& Definition,
                                                               const FVector& WorldPosition,
                                                               const FFableTownAnchorRef& Anchor,
                                                               FFableTownRoute& OutRoute)
{
    // 이전 결과의 Target을 다시 입력해도 출력 초기화가 요청을 지우지 않는다. / 前の結果からの再照会も保つ。
    const FFableTownAnchorRef RequestedAnchor = Anchor;
    const FVector RequestedPosition = WorldPosition;
    OutRoute = FFableTownRoute();
    FFableTownLocationContext Physical;
    const EFableTownRouteStatus StartStatus = ResolveStart(Definition, RequestedPosition, Physical);
    if (StartStatus != EFableTownRouteStatus::Ready)
    {
        return StartStatus;
    }

    if (!RequestedAnchor.IsValid() || RequestedAnchor.TownId != Definition.TownId ||
        RequestedAnchor.CatalogVersion != Definition.CatalogVersion)
    {
        return EFableTownRouteStatus::TargetUnavailable;
    }

    const FFableTownAnchorDefinition* Target = Definition.Anchors.FindByPredicate(
        [&RequestedAnchor](const FFableTownAnchorDefinition& Candidate)
        {
            return Candidate.AnchorId == RequestedAnchor.AnchorId;
        });
    if (!Target)
    {
        return EFableTownRouteStatus::TargetUnavailable;
    }

    // 도착 슬롯은 여기서 배정하지 않으며 정본 첫 슬롯을 안내점으로만 사용한다. / 最初の枠は案内点だけに使う。
    const FVector TargetWorld = Definition.LocalToWorld.TransformPosition(Target->LocalSlots[0].GetTranslation());
    if (!FiniteRoutePoint(TargetWorld))
    {
        return EFableTownRouteStatus::InvalidDefinition;
    }

    FableTownRouteCore::Request Request;
    Request.StartFloor = RouteKey(Physical.FloorId);
    Request.GoalFloor = RouteKey(Target->FloorId);
    Request.Start = CorePoint(RequestedPosition);
    Request.Goal = CorePoint(TargetWorld);
    for (const FFableTownConnectorDefinition& Connector : Definition.Connectors)
    {
        // 이전 위치 판정용 회랑은 명시된 층 연결이 없으므로 경로에 넣지 않는다. / 旧位置回廊を経路にしない。
        if (Connector.FromFloorId.IsNone() && Connector.ToFloorId.IsNone())
        {
            continue;
        }

        FableTownRouteCore::Connection Connection;
        Connection.Id = RouteKey(Connector.ConnectorId);
        Connection.FromFloor = RouteKey(Connector.FromFloorId);
        Connection.ToFloor = RouteKey(Connector.ToFloorId);
        Connection.Bidirectional = Connector.bBidirectional;
        for (const FVector& LocalPoint : Connector.LocalPoints)
        {
            const FVector WorldPoint = Definition.LocalToWorld.TransformPosition(LocalPoint);
            if (!FiniteRoutePoint(WorldPoint))
            {
                return EFableTownRouteStatus::InvalidDefinition;
            }
            Connection.Points.push_back(CorePoint(WorldPoint));
        }
        Request.Connections.push_back(std::move(Connection));
    }

    const FableTownRouteCore::Result Result = FableTownRouteCore::Find(Request);
    if (Result.State != FableTownRouteCore::Status::Ready)
    {
        return Result.State == FableTownRouteCore::Status::Unreachable ? EFableTownRouteStatus::NoRoute
                                                                       : EFableTownRouteStatus::InvalidDefinition;
    }

    FFableTownRoute Resolved;
    Resolved.Target = RequestedAnchor;
    Resolved.Target.AnchorId = Target->AnchorId;
    Resolved.SourceFloorId = CanonicalFloor(Definition, Physical.FloorId);
    Resolved.TargetFloorId = CanonicalFloor(Definition, Target->FloorId);
    Resolved.TargetWorldPosition = TargetWorld;
    Resolved.NextEntryWorldPosition = FVector(Result.NextEntry.X, Result.NextEntry.Y, Result.NextEntry.Z);
    Resolved.EstimatedDistanceCm = Result.TotalDistance;

    // コアの結果も元定義へ戻し、未知・逆方向・不連続の通路を公開しない。 / 결과를 원래 정본에 다시 결합한다.
    FName CurrentFloor = Resolved.SourceFloorId;
    for (const FableTownRouteCore::Traversal& Traversal : Result.Traversals)
    {
        const FFableTownConnectorDefinition* Connector = Definition.Connectors.FindByPredicate(
            [&Traversal](const FFableTownConnectorDefinition& Candidate)
            {
                return RouteKey(Candidate.ConnectorId) == Traversal.ConnectorId;
            });
        if (!Connector || (Traversal.Reverse && !Connector->bBidirectional))
        {
            return EFableTownRouteStatus::InvalidDefinition;
        }

        FFableTownRouteStep Step;
        Step.ConnectorId = Connector->ConnectorId;
        Step.FromFloorId =
            CanonicalFloor(Definition, Traversal.Reverse ? Connector->ToFloorId : Connector->FromFloorId);
        Step.ToFloorId = CanonicalFloor(Definition, Traversal.Reverse ? Connector->FromFloorId : Connector->ToFloorId);
        Step.bReverse = Traversal.Reverse;
        Step.EntryWorldPosition = Definition.LocalToWorld.TransformPosition(
            Traversal.Reverse ? Connector->LocalPoints.Last() : Connector->LocalPoints[0]);
        Step.ExitWorldPosition = Definition.LocalToWorld.TransformPosition(
            Traversal.Reverse ? Connector->LocalPoints[0] : Connector->LocalPoints.Last());
        if (Step.FromFloorId.IsNone() || Step.ToFloorId.IsNone() || Step.FromFloorId != CurrentFloor)
        {
            return EFableTownRouteStatus::InvalidDefinition;
        }
        CurrentFloor = Step.ToFloorId;
        Resolved.Steps.Add(Step);
    }

    const FVector ExpectedNext = Resolved.Steps.IsEmpty() ? TargetWorld : Resolved.Steps[0].EntryWorldPosition;
    if (CurrentFloor != Resolved.TargetFloorId || !FiniteRoutePoint(Resolved.NextEntryWorldPosition) ||
        !Resolved.NextEntryWorldPosition.Equals(ExpectedNext, KINDA_SMALL_NUMBER) ||
        !FMath::IsFinite(Resolved.EstimatedDistanceCm) || Resolved.EstimatedDistanceCm < 0.0)
    {
        return EFableTownRouteStatus::InvalidDefinition;
    }

    OutRoute = MoveTemp(Resolved);
    return EFableTownRouteStatus::Ready;
}

/** 전체 정본 조회와 단일 정의 조회가 같은 준비·위치 경로를 사용한다. / 同じ準備・位置検査を使う。 */
EFableTownRouteStatus FableTownRoutes::ResolveAnchor(const UFableTownCatalog* Catalog, const FVector& WorldPosition,
                                                     const FFableTownAnchorRef& Anchor, FFableTownRoute& OutRoute)
{
    const FFableTownAnchorRef RequestedAnchor = Anchor;
    const FVector RequestedPosition = WorldPosition;
    OutRoute = FFableTownRoute();
    const FFableTownDefinition* Definition = nullptr;
    const EFableTownRouteStatus Status = CurrentDefinition(Catalog, RequestedPosition, Definition);
    return Status == EFableTownRouteStatus::Ready
               ? ResolveDefinitionAnchor(*Definition, RequestedPosition, RequestedAnchor, OutRoute)
               : Status;
}

/** 서비스별 정본 앵커를 재사용하며 상호작용 거리나 허가를 확장하지 않는다. */
EFableTownRouteStatus FableTownRoutes::ResolveDefinitionService(const FFableTownDefinition& Definition,
                                                                const FVector& WorldPosition,
                                                                const FFableTownServiceRef& Service,
                                                                FFableTownRoute& OutRoute)
{
    const FVector RequestedPosition = WorldPosition;
    const FFableTownServiceRef RequestedService = Service;
    OutRoute = FFableTownRoute();
    FFableTownLocationContext Physical;
    const EFableTownRouteStatus StartStatus = ResolveStart(Definition, RequestedPosition, Physical);
    if (StartStatus != EFableTownRouteStatus::Ready)
    {
        return StartStatus;
    }

    FFableTownServiceDescriptor Descriptor;
    if (!UFableTownCatalog::ResolveDefinitionService(Definition, RequestedService, Descriptor, true))
    {
        return EFableTownRouteStatus::TargetUnavailable;
    }
    return ResolveDefinitionAnchor(Definition, RequestedPosition, Descriptor.Anchor, OutRoute);
}

/** 別の村への代替やロードを行わず、現在の正本サービスだけを案内する。 */
EFableTownRouteStatus FableTownRoutes::ResolveService(const UFableTownCatalog* Catalog, const FVector& WorldPosition,
                                                      const FFableTownServiceRef& Service, FFableTownRoute& OutRoute)
{
    const FVector RequestedPosition = WorldPosition;
    const FFableTownServiceRef RequestedService = Service;
    OutRoute = FFableTownRoute();
    const FFableTownDefinition* Definition = nullptr;
    const EFableTownRouteStatus Status = CurrentDefinition(Catalog, RequestedPosition, Definition);
    return Status == EFableTownRouteStatus::Ready
               ? ResolveDefinitionService(*Definition, RequestedPosition, RequestedService, OutRoute)
               : Status;
}
