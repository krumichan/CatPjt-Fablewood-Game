#pragma once

#include "CoreMinimal.h"
#include "FableTownTypes.h"

/** 대화 역할을 배치 Actor와 분리한다. / 会話の役割を配置Actorから分離する。 */
enum class EFableTownServiceId : uint8
{
    Invalid,
    QuestGuide,
    Workshop,
    Merchant,
    ExpeditionPortal
};

/** 요청은 마을·서비스·정본 버전만 지정한다. / 要求は村・サービス・正本版だけを指定する。 */
struct FABLEWOOD_API FFableTownServiceRef
{
    EFableTownId TownId = EFableTownId::Invalid;
    EFableTownServiceId ServiceId = EFableTownServiceId::Invalid;
    int32 CatalogVersion = 0;

    /** 알 수 없는 마을과 역할을 기본 서비스로 바꾸지 않는다. / 未知の村と役割を既定値へ変換しない。 */
    bool IsValid() const
    {
        return TownId >= EFableTownId::LegacyVillage && TownId <= EFableTownId::Eldergrove &&
               ServiceId >= EFableTownServiceId::QuestGuide && ServiceId <= EFableTownServiceId::ExpeditionPortal &&
               CatalogVersion > 0;
    }
};

/** 서버 앵커에서 파생한 조회 결과이며 이동·저장 허가가 아니다. / サーバーアンカー由来の照会結果で移動・保存許可ではない。 */
struct FABLEWOOD_API FFableTownServiceDescriptor
{
    FFableTownServiceRef Service;
    FFableTownAnchorRef Anchor;
    FName FloorId = NAME_None;
    FVector WorldLocation = FVector::ZeroVector;
    double InteractionRadiusCm = 330.0;
    double VerticalRangeCm = 300.0;
    bool bGeometryReady = false;

    /** 호출자가 확장한 거리나 비유한 좌표는 사용하지 않는다. / 拡大された距離や非有限座標を使用しない。 */
    bool IsValid() const
    {
        return Service.IsValid() && Anchor.IsValid() && Service.TownId == Anchor.TownId &&
               Service.CatalogVersion == Anchor.CatalogVersion && !FloorId.IsNone() &&
               FMath::IsFinite(WorldLocation.X) && FMath::IsFinite(WorldLocation.Y) &&
               FMath::IsFinite(WorldLocation.Z) && FMath::IsFinite(InteractionRadiusCm) && InteractionRadiusCm > 0.0 &&
               InteractionRadiusCm <= 330.0 && FMath::IsFinite(VerticalRangeCm) && VerticalRangeCm > 0.0 &&
               VerticalRangeCm <= 300.0;
    }

    /** 순수 공간 검사 뒤에도 서버 소유권과 새 지지면 검사가 필요하다. / 純粋な空間検査後も所有権と新しい床検査が必要。 */
    bool IsInRange(const FFableTownLocationContext& Physical, const FVector& PawnLocation) const
    {
        return IsValid() && bGeometryReady && Physical.IsValid() && Physical.bGeometryReady &&
               Physical.TownId == Service.TownId && Physical.CatalogVersion == Service.CatalogVersion &&
               Physical.FloorId == FloorId && Physical.ConnectorId.IsNone() && FMath::IsFinite(PawnLocation.X) &&
               FMath::IsFinite(PawnLocation.Y) && FMath::IsFinite(PawnLocation.Z) &&
               FVector::DistSquared2D(PawnLocation, WorldLocation) <= FMath::Square(InteractionRadiusCm) &&
               FMath::Abs(PawnLocation.Z - WorldLocation.Z) < VerticalRangeCm;
    }
};
