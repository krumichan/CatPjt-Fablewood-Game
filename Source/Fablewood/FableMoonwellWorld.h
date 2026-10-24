#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableMoonwellWorld.generated.h"
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;

/** Separate moonlit garden. Stable local components are valid movement bases on both peers. */
/** 달샘 지역의 배경·조명·연출을 구성하는 월드 액터. */
UCLASS()
class FABLEWOOD_API AFableMoonwellWorld : public AActor
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableMoonwellWorld();

    /** 액터 구성 시 배경과 표시 요소를 준비한다. */
    virtual void OnConstruction(const FTransform& Transform) override;

    /** 컴포넌트 등록이 끝나면 배경 구성을 확인한다. */
    virtual void PostRegisterAllComponents() override;

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    virtual void Tick(float DeltaSeconds) override;

    /** 최종 원점에서 생성한 달샘 배경을 해당 원정 UUID에 연결한다. */
    void BindExpedition(FGuid InExpeditionId);

    /** 이 배경을 소유하는 원정 UUID를 반환한다. */
    FGuid GetExpeditionId() const
    {
        return ExpeditionId;
    }

    /** 원정 식별자를 클라이언트의 봉인 연출에 복제한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

private:
    UPROPERTY(Replicated)
    FGuid ExpeditionId;

    /** 지역의 지형·건축물·식생·조명을 구성한다. */
    void BuildEnvironment();

    /** 같은 재질과 메시를 공유하는 배경 인스턴스 묶음을 만든다. */
    UInstancedStaticMeshComponent* MakeBatch(FName Name, UStaticMesh* Mesh, FLinearColor Color, bool bBlocking = false,
                                             UMaterialInterface* OverrideMaterial = nullptr);

    /** 지정한 위치·회전·크기로 배경 인스턴스를 배치한다. */
    static void Place(UInstancedStaticMeshComponent* Batch, FVector Location, FVector Scale,
                      FRotator Rotation = FRotator::ZeroRotator);

    /** 지역 연출에 필요한 광원을 배치한다. */
    void AddLight(FName Name, FVector Location, FLinearColor Color, float Intensity, float Radius);
    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> PaintMaterial;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> SealMaterials;
    bool bBuilding = false;
    int32 LastSealState = -1;
};
