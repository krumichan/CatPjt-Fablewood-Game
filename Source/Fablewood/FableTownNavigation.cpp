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

namespace
{
    /** 실패에는 이유 외의 이전 결과를 남기지 않는다. / 失敗には理由以外の以前の結果を残さない。 */
    EFableTownRouteStatus ClearFailedGuide(EFableTownRouteStatus Status, FFableTownNavigationGuide& OutGuide)
    {
        OutGuide = FFableTownNavigationGuide();
        OutGuide.Status = Status;
        return Status;
    }

    /** 외부에서 조합한 불완전한 표시 값도 진행 문구에 사용하지 않는다. / 不完全な表示値も進捗文言に使わない。 */
    bool HasReadyGuide(const FFableTownNavigationGuide& Guide)
    {
        const auto FinitePoint = [](const FVector& Point)
        {
            return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y) && FMath::IsFinite(Point.Z);
        };
        if (!Guide.bValid || Guide.Status != EFableTownRouteStatus::Ready || !Guide.Service.IsValid() ||
            !Guide.Service.bGeometryReady || !FinitePoint(Guide.WaypointWorld) || !FinitePoint(Guide.TargetWorld) ||
            Guide.SourceFloorId.IsNone() || Guide.NextFloorId.IsNone() || Guide.TargetFloorId.IsNone() ||
            Guide.TargetFloorId != Guide.Service.FloorId || !Guide.TargetWorld.Equals(Guide.Service.WorldLocation))
        {
            return false;
        }

        if (Guide.bViaConnector)
        {
            return !Guide.ConnectorId.IsNone() && Guide.RemainingSteps > 0 &&
                   Guide.SourceFloorId != Guide.NextFloorId && Guide.SourceFloorId != Guide.TargetFloorId;
        }
        return Guide.ConnectorId.IsNone() && Guide.RemainingSteps == 0 && Guide.SourceFloorId == Guide.TargetFloorId &&
               Guide.NextFloorId == Guide.TargetFloorId && Guide.WaypointWorld.Equals(Guide.TargetWorld);
    }

    /** Ground와 정규 F숫자만 번역하고 임의 층 ID를 그대로 노출하지 않는다. / 正規階名だけを訳し内部IDを露出しない。 */
    FString GuideFloorText(FName FloorId, const TCHAR* Fallback)
    {
        if (FloorId == FName(TEXT("Ground")))
        {
            return TEXT("지상층");
        }

        const FString Name = FloorId.ToString();
        if (Name.Len() < 2 || FChar::ToUpper(Name[0]) != TCHAR('F') || (Name.Len() > 2 && Name[1] == TCHAR('0')))
        {
            return Fallback;
        }

        // 큰 식별자를 Atoi로 넘기지 않고 표시 가능한 정수 범위만 읽는다. / 大きなIDをAtoiへ渡さず範囲を確認する。
        int32 Index = 0;
        for (int32 Character = 1; Character < Name.Len(); ++Character)
        {
            const TCHAR DigitCharacter = Name[Character];
            if (DigitCharacter < TCHAR('0') || DigitCharacter > TCHAR('9'))
            {
                return Fallback;
            }
            const int32 Digit = DigitCharacter - TCHAR('0');
            if (Index > (MAX_int32 - 1 - Digit) / 10)
            {
                return Fallback;
            }
            Index = Index * 10 + Digit;
        }
        return FString::Printf(TEXT("%d층"), Index + 1);
    }
}

/** 서비스는 실제 목적지에 두고 표시용으로만 다음 통로 입구를 선택한다. / サービスを実位置に保ち表示だけに入口を選ぶ。 */
EFableTownRouteStatus FableTownNavigation::ResolveDefinitionServiceGuide(const FFableTownDefinition& Definition,
                                                                         const FVector& WorldPosition,
                                                                         EFableTownServiceId ServiceId,
                                                                         FFableTownNavigationGuide& OutGuide)
{
    // 직전 안내점을 입력으로 재사용해도 출력 초기화가 위치를 지우지 않게 한다. / 前の案内点を再利用しても入力を保つ。
    const FVector RequestedPosition = WorldPosition;
    OutGuide = FFableTownNavigationGuide();
    FString Error;
    if (!UFableTownCatalog::ValidateDefinition(Definition, Error))
    {
        return ClearFailedGuide(EFableTownRouteStatus::InvalidDefinition, OutGuide);
    }

    FFableTownLocationContext Physical;
    if (!UFableTownCatalog::ResolveDefinitionLocation(Definition, RequestedPosition, Physical, false))
    {
        return ClearFailedGuide(EFableTownRouteStatus::InvalidLocation, OutGuide);
    }
    if (!Physical.bGeometryReady)
    {
        return ClearFailedGuide(EFableTownRouteStatus::GeometryUnavailable, OutGuide);
    }

    FFableTownServiceRef Reference;
    Reference.TownId = Physical.TownId;
    Reference.ServiceId = ServiceId;
    Reference.CatalogVersion = Physical.CatalogVersion;
    FFableTownServiceDescriptor Service;
    if (!UFableTownCatalog::ResolveDefinitionService(Definition, Reference, Service, true))
    {
        return ClearFailedGuide(EFableTownRouteStatus::TargetUnavailable, OutGuide);
    }

    FFableTownRoute Route;
    const EFableTownRouteStatus Status =
        FableTownRoutes::ResolveDefinitionService(Definition, RequestedPosition, Reference, Route);
    if (Status != EFableTownRouteStatus::Ready)
    {
        return ClearFailedGuide(Status, OutGuide);
    }

    // 실제 서비스 정체성과 현재 층의 안내점을 각각 원래 정의에 다시 결합한다. / 目的地と現在階の案内点を別々に照合する。
    FFableTownLocationContext WaypointPhysical;
    if (Route.Target.TownId != Service.Anchor.TownId || Route.Target.AnchorId != Service.Anchor.AnchorId ||
        Route.Target.CatalogVersion != Service.Anchor.CatalogVersion || Route.SourceFloorId != Physical.FloorId ||
        Route.TargetFloorId != Service.FloorId || !Route.TargetWorldPosition.Equals(Service.WorldLocation) ||
        !UFableTownCatalog::ResolveDefinitionLocation(Definition, Route.NextEntryWorldPosition, WaypointPhysical,
                                                      true) ||
        WaypointPhysical.FloorId != Physical.FloorId || !WaypointPhysical.ConnectorId.IsNone())
    {
        return ClearFailedGuide(EFableTownRouteStatus::InvalidDefinition, OutGuide);
    }

    FFableTownNavigationGuide Resolved;
    Resolved.bValid = true;
    Resolved.Status = EFableTownRouteStatus::Ready;
    Resolved.Service = Service;
    Resolved.WaypointWorld = Route.NextEntryWorldPosition;
    Resolved.TargetWorld = Service.WorldLocation;
    Resolved.SourceFloorId = Route.SourceFloorId;
    Resolved.TargetFloorId = Route.TargetFloorId;
    Resolved.bViaConnector = !Route.Steps.IsEmpty();
    Resolved.RemainingSteps = Route.Steps.Num();
    Resolved.ConnectorId = Resolved.bViaConnector ? Route.Steps[0].ConnectorId : NAME_None;
    Resolved.NextFloorId = Resolved.bViaConnector ? Route.Steps[0].ToFloorId : Route.TargetFloorId;
    if (!HasReadyGuide(Resolved))
    {
        return ClearFailedGuide(EFableTownRouteStatus::InvalidDefinition, OutGuide);
    }

    OutGuide = MoveTemp(Resolved);
    return EFableTownRouteStatus::Ready;
}

/** 전체 정본과 유일한 현재 위치를 확인하고 동일한 표시 전용 검사를 사용한다. / 全定義と現在位置を確認する。 */
EFableTownRouteStatus FableTownNavigation::ResolveServiceGuide(const UFableTownCatalog* Catalog,
                                                               const FVector& WorldPosition,
                                                               EFableTownServiceId ServiceId,
                                                               FFableTownNavigationGuide& OutGuide)
{
    const FVector RequestedPosition = WorldPosition;
    OutGuide = FFableTownNavigationGuide();
    FString Error;
    if (!Catalog || !Catalog->Validate(Error))
    {
        return ClearFailedGuide(EFableTownRouteStatus::InvalidDefinition, OutGuide);
    }

    FFableTownLocationContext Physical;
    if (!Catalog->ResolveLocation(RequestedPosition, Physical, false))
    {
        return ClearFailedGuide(EFableTownRouteStatus::InvalidLocation, OutGuide);
    }
    const FFableTownDefinition* Definition = Catalog->FindTown(Physical.TownId);
    return Definition ? ResolveDefinitionServiceGuide(*Definition, RequestedPosition, ServiceId, OutGuide)
                      : ClearFailedGuide(EFableTownRouteStatus::InvalidDefinition, OutGuide);
}

/** 같은 층 안내 문구는 그대로 유지하고 통로가 있을 때만 입구임을 알린다. / 同階の文言を保ち入口だけを明示する。 */
FString FableTownNavigation::GuideLabel(const FString& BaseLabel, const FFableTownNavigationGuide& Guide)
{
    if (!HasReadyGuide(Guide))
    {
        return UnavailableRouteText(Guide.Status);
    }
    return Guide.bViaConnector ? (BaseLabel.IsEmpty() ? TEXT("층간 통로 입구") : BaseLabel + TEXT(" · 층간 통로 입구"))
                               : BaseLabel;
}

/** 층의 내부 식별자는 표시 이름으로 바꾸고 진행 상태만 짧게 알린다. / 内部IDを表示名へ変えて進行状態だけを示す。 */
FString FableTownNavigation::RouteProgressText(const FFableTownNavigationGuide& Guide)
{
    if (!HasReadyGuide(Guide) || !Guide.bViaConnector)
    {
        return FString();
    }
    const FString Source = GuideFloorText(Guide.SourceFloorId, TEXT("현재층"));
    const FString Next = GuideFloorText(Guide.NextFloorId, TEXT("다음층"));
    const FString Target = GuideFloorText(Guide.TargetFloorId, TEXT("목적지층"));
    const FString TargetCaption = Target == TEXT("목적지층") ? Target : TEXT("목적지 ") + Target;
    return FString::Printf(TEXT("%s → %s · %s · 남은 통로 %d곳"), *Source, *Next, *TargetCaption, Guide.RemainingSteps);
}

/** 이전 목표·가짜 이동률 대신 현재 안내를 막는 이유만 표시한다. / 旧目標や偽進捗ではなく現在の理由だけを表示する。 */
FString FableTownNavigation::UnavailableRouteText(EFableTownRouteStatus Status)
{
    switch (Status)
    {
    case EFableTownRouteStatus::GeometryUnavailable:
        return TEXT("현재 마을을 이용할 수 없습니다");
    case EFableTownRouteStatus::TargetUnavailable:
        return TEXT("현재 마을에는 이 목적지가 준비되지 않았습니다");
    case EFableTownRouteStatus::NoRoute:
        return TEXT("목적지 층으로 이어지는 통로가 없습니다");
    case EFableTownRouteStatus::ConnectorUnavailable:
        return TEXT("층간 통로 이동 중 · 다음 층에 도착하면 안내가 이어집니다");
    case EFableTownRouteStatus::InvalidDefinition:
        return TEXT("현재 마을의 길안내 정보를 확인할 수 없습니다");
    default:
        return TEXT("현재 위치에서 목적지를 안내할 수 없습니다");
    }
}
