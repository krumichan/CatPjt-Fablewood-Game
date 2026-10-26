#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FableTownTypes.h"
#include "FableTownServiceTypes.h"
#include "FableTownCatalog.generated.h"

class UWorld;

/** 층의 유한 조회 볼륨. 실제 지원 바닥은 후속 물리 검증 대상이다. / 階の有限照会範囲。実床は別検証。 */
USTRUCT()
struct FABLEWOOD_API FFableTownFloorDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    FName FloorId = NAME_None;

    UPROPERTY()
    FBox LocalBounds = FBox(ForceInit);
};

/** 두 층 ID가 비어 있으면 기존 위치 조회 전용 회랑이다. / 両階IDが空なら従来の位置照会専用回廊。 */
USTRUCT()
struct FABLEWOOD_API FFableTownConnectorDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    FName ConnectorId = NAME_None;

    UPROPERTY()
    TArray<FVector> LocalPoints;

    UPROPERTY()
    double RadiusCm = 0.0;

    /** 첫 경로점이 속하는 출발층이며 다른 층과 묶어서 명시한다. / 始点の階を終点の階と対で明示する。 */
    UPROPERTY()
    FName FromFloorId = NAME_None;

    /** 마지막 경로점이 속하는 도착층이다. / 最後の経路点が属する到着階。 */
    UPROPERTY()
    FName ToFloorId = NAME_None;

    /** false이면 점 목록의 처음에서 마지막 방향만 안내한다. / falseなら点列の先頭から末尾だけを案内する。 */
    UPROPERTY()
    bool bBidirectional = true;
};

/** 논리 앵커는 하나 이상의 서버 선택 도착 슬롯을 가진다. / 論理アンカーの到着枠はサーバーが選択する。 */
USTRUCT()
struct FABLEWOOD_API FFableTownAnchorDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    FName AnchorId = NAME_None;

    UPROPERTY()
    FName FloorId = NAME_None;

    UPROPERTY()
    TArray<FTransform> LocalSlots;

    /** authored support의 component tag와 표면 높이를 명시한다. */
    UPROPERTY()
    FName ExpectedSupportTag = NAME_None;

    UPROPERTY()
    double ExpectedSupportLocalZ = 0.0;
};

/** 새 네 마을은 후보 데이터이며 활성 지형이 아니다. / 新四村は候補データであり有効な地形ではない。 */
USTRUCT()
struct FABLEWOOD_API FFableTownDefinition
{
    GENERATED_BODY()

    UPROPERTY()
    EFableTownId TownId = EFableTownId::Invalid;

    UPROPERTY()
    int32 CatalogVersion = 1;

    UPROPERTY()
    FTransform LocalToWorld = FTransform::Identity;

    UPROPERTY()
    FBox LocalBounds = FBox(ForceInit);

    UPROPERTY()
    TArray<FFableTownFloorDefinition> Floors;

    UPROPERTY()
    TArray<FFableTownConnectorDefinition> Connectors;

    UPROPERTY()
    TArray<FFableTownAnchorDefinition> Anchors;

    UPROPERTY()
    TSoftObjectPtr<UWorld> GeometryLevel;

    UPROPERTY()
    bool bGeometryReady = false;
};

/** Phase 1의 순수 정의/조회만 담당한다. 로드·이동·저장은 수행하지 않는다. / 定義照会のみ、ロード・移動・保存なし。 */
UCLASS()
class FABLEWOOD_API UFableTownCatalog : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UFableTownCatalog();

    const TArray<FFableTownDefinition>& GetDefinitions() const;
    const FFableTownDefinition* FindTown(EFableTownId TownId) const;
    bool IsGeometryReady(EFableTownId TownId) const;
    bool Validate(FString& OutError) const;
    bool ResolveLocation(const FVector& WorldPosition, FFableTownLocationContext& OutContext,
                         bool bRequireGeometryReady = true) const;
    bool ToWorld(EFableTownId TownId, const FVector& LocalPosition, FVector& OutWorldPosition) const;
    bool ToLocal(EFableTownId TownId, const FVector& WorldPosition, FVector& OutLocalPosition) const;
    bool ResolveAnchor(const FFableTownAnchorRef& Anchor, int32 SlotIndex, FTransform& OutWorldTransform,
                       bool bRequireGeometryReady = true) const;

    /** 기존 앵커에서만 서비스를 파생하며 후보 배치를 생성하지 않는다. / 既存アンカーだけからサービスを導く。 */
    bool ResolveService(const FFableTownServiceRef& Service, FFableTownServiceDescriptor& OutService,
                        bool bRequireGeometryReady = true) const;

    /** 표시/선택용 순수 범위 조회이며 서버 권한을 부여하지 않는다. / 表示・選択向けの範囲照会でサーバー権限を付与しない。 */
    bool ResolveNearbyService(const FVector& WorldPosition, EFableTownServiceId ServiceId,
                              FFableTownServiceDescriptor& OutService) const;

    /** 단일 정의의 순수 조회이며 Actor·접지·소유권 승인이 아니다. / 単一定義の照会でActor・接地・所有権の承認ではない。 */
    static bool ResolveDefinitionService(const FFableTownDefinition& Definition, const FFableTownServiceRef& Service,
                                         FFableTownServiceDescriptor& OutService, bool bRequireGeometryReady = true);

    /** 메모리 fixture도 동일한 순수 검사를 사용한다. / メモリーfixtureも同じ純粋検査を利用する。 */
    static bool ValidateDefinition(const FFableTownDefinition& Definition, FString& OutError);
    static bool ResolveDefinitionLocation(const FFableTownDefinition& Definition, const FVector& WorldPosition,
                                          FFableTownLocationContext& OutContext, bool bRequireGeometryReady = true);

private:
    UPROPERTY()
    TArray<FFableTownDefinition> Definitions;

#if WITH_DEV_AUTOMATION_TESTS
    friend struct FFableTownTestAccess;
#endif
};
