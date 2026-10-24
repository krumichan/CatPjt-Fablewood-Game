#pragma once

#include "CoreMinimal.h"
#include "FableTownTypes.generated.h"

/** 기존 마을과 미채택 후보를 구분한다. / 既存の村と未採用候補を区別する。 */
UENUM()
enum class EFableTownId : uint8
{
    Invalid,
    LegacyVillage,
    Sunbrook,
    Starbough,
    Snowbloom,
    Eldergrove
};

/** 물리 공간과 원정의 정체성을 분리한다. / 物理空間と遠征の識別を分離する。 */
UENUM()
enum class EFableAreaKind : uint8
{
    Invalid,
    Town,
    SharedForest,
    PrivateExpedition
};

/** 발사/등록 시 고정할 필드 실행 세대. / 発射・登録時に固定するフィールド実行世代。 */
USTRUCT()
struct FABLEWOOD_API FFablePublicAreaKey
{
    GENERATED_BODY()

    UPROPERTY()
    FName AreaId = NAME_None;

    UPROPERTY()
    FGuid ScopeId;

    /** 미초기화 식별자를 거절한다. / 未初期化の識別を拒否する。 */
    bool IsValid() const
    {
        return !AreaId.IsNone() && ScopeId.IsValid();
    }

    /** 이름과 세대가 모두 같아야 동일 범위다. / 名前と世代の両方を比較する。 */
    bool operator==(const FFablePublicAreaKey& Other) const
    {
        return AreaId == Other.AreaId && ScopeId == Other.ScopeId;
    }

    /** 서로 다른 범위를 구분한다. / 異なる範囲を区別する。 */
    bool operator!=(const FFablePublicAreaKey& Other) const
    {
        return !(*this == Other);
    }
};

/** 서버 catalog의 논리 앵커 참조이며 XYZ 권한이 아니다. / XYZ権限ではなくサーバー定義の参照。 */
USTRUCT()
struct FABLEWOOD_API FFableTownAnchorRef
{
    GENERATED_BODY()

    UPROPERTY()
    EFableTownId TownId = EFableTownId::Invalid;

    UPROPERTY()
    FName AnchorId = NAME_None;

    UPROPERTY()
    int32 CatalogVersion = 0;

    /** 미초기화 식별자를 거절한다. / 未初期化の識別を拒否する。 */
    bool IsValid() const
    {
        return TownId >= EFableTownId::LegacyVillage && TownId <= EFableTownId::Eldergrove && !AnchorId.IsNone() &&
               CatalogVersion > 0;
    }
};

/** 좌표 조회 결과이며 바닥/로드 성공 증명은 아니다. / 座標照会の結果であり床・ロード成功の証明ではない。 */
USTRUCT()
struct FABLEWOOD_API FFableTownLocationContext
{
    GENERATED_BODY()

    UPROPERTY()
    EFableAreaKind AreaKind = EFableAreaKind::Invalid;

    UPROPERTY()
    EFableTownId TownId = EFableTownId::Invalid;

    UPROPERTY()
    FName FloorId = NAME_None;

    UPROPERTY()
    FName ConnectorId = NAME_None;

    UPROPERTY()
    FVector LocalPosition = FVector::ZeroVector;

    UPROPERTY()
    int32 CatalogVersion = 0;

    UPROPERTY()
    bool bGeometryReady = false;

    /** 미초기화 식별자를 거절한다. / 未初期化の識別を拒否する。 */
    bool IsValid() const
    {
        return AreaKind == EFableAreaKind::Town && TownId >= EFableTownId::LegacyVillage &&
               TownId <= EFableTownId::Eldergrove && CatalogVersion > 0 &&
               (!FloorId.IsNone() || !ConnectorId.IsNone()) && !LocalPosition.ContainsNaN();
    }
};

namespace FableTown
{
    /** 기존 필드의 안정 명칭. / 既存フィールドの固定名。 */
    inline FName LegacyForestAreaId()
    {
        return FName(TEXT("LegacyForest"));
    }
}
