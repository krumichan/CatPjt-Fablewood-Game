#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableWorld.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** A deterministic forest clearing with original art and identical static collision on every peer. */
/** 숲과 마을의 동화풍 배경을 구성하는 월드 액터. */
UCLASS()
class FABLEWOOD_API AFableWorld : public AActor
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableWorld();

    /** 액터 구성 시 배경과 표시 요소를 준비한다. */
    virtual void OnConstruction(const FTransform& Transform) override;

    /** 컴포넌트 등록이 끝나면 배경 구성을 확인한다. */
    virtual void PostRegisterAllComponents() override;

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

private:
    /** 지역의 지형·건축물·식생·조명을 구성한다. */
    void BuildEnvironment();

    /** 같은 재질과 메시를 공유하는 배경 인스턴스 묶음을 만든다. */
    UInstancedStaticMeshComponent* MakeBatch(FName Name, UStaticMesh* Mesh, const FLinearColor& Color,
                                             bool bBlocking = false, UMaterialInterface* MaterialOverride = nullptr,
                                             float Roughness = 0.88f);

    /** 지정한 위치·회전·크기로 배경 인스턴스를 배치한다. */
    static void Place(UInstancedStaticMeshComponent* Batch, const FVector& Location, const FVector& Scale,
                      const FRotator& Rotation = FRotator::ZeroRotator);

    bool bBuildingEnvironment = false;

    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> ShapeMaterial;
};
