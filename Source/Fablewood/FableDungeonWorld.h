#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableDungeonWorld.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** A replicated anchor with deterministic, locally built town services and ruin geometry. */
/** 뿌리 유적 던전의 배경·안내·조명을 구성하는 월드 액터. */
UCLASS()
class FABLEWOOD_API AFableDungeonWorld : public AActor
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableDungeonWorld();

    /** 액터 구성 시 배경과 표시 요소를 준비한다. */
    virtual void OnConstruction(const FTransform& Transform) override;

    /** 컴포넌트 등록이 끝나면 배경 구성을 확인한다. */
    virtual void PostRegisterAllComponents() override;

    /** 최종 원점에서 생성한 사설 배경을 해당 원정 UUID에 연결한다. */
    void BindExpedition(FGuid InExpeditionId, bool bInPrivateDungeonOnly = true);

    /** 이 배경을 소유하는 원정 UUID를 반환한다. */
    FGuid GetExpeditionId() const
    {
        return ExpeditionId;
    }

    /** 결정된 원정 배경 설정을 클라이언트에 복제한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

private:
    /** 초기 복제 후 로컬 배경도 같은 사설·공용 구성으로 다시 만든다. */
    UFUNCTION()
    void OnRep_ExpeditionContext();

    UPROPERTY(ReplicatedUsing = OnRep_ExpeditionContext)
    FGuid ExpeditionId;

    UPROPERTY(ReplicatedUsing = OnRep_ExpeditionContext)
    bool bPrivateDungeonOnly = false;

    /** 지역의 지형·건축물·식생·조명을 구성한다. */
    void BuildEnvironment();

    /** 마을 주민의 부품과 안내 표시를 배치한다. */
    void AddVillager(const FString& Id, FVector Location, FLinearColor Cloth, FLinearColor Hair);

    /** 같은 재질과 메시를 공유하는 배경 인스턴스 묶음을 만든다. */
    UInstancedStaticMeshComponent* MakeBatch(FName Name, UStaticMesh* Mesh, FLinearColor Color, bool bBlocking);

    /** 월드 안내 문구의 위치·글꼴·색상을 구성한다. */
    void AddText(FName Name, const FString& Text, FVector Location, float Size, FColor Color);

    /** 지역 연출에 필요한 광원을 배치한다. */
    void AddLight(FName Name, FVector Location, FLinearColor Color, float Intensity, float Radius);

    /** 지정한 위치·회전·크기로 배경 인스턴스를 배치한다. */
    static void Place(UInstancedStaticMeshComponent* Batch, FVector Location, FVector Scale,
                      FRotator Rotation = FRotator::ZeroRotator);
    bool bBuilding = false;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> PaintMaterial;
};
