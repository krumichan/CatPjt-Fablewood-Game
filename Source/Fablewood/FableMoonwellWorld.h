#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableMoonwellWorld.generated.h"
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;

/** Separate moonlit garden. Stable local components are valid movement bases on both peers. */
UCLASS()
class FABLEWOOD_API AFableMoonwellWorld : public AActor
{
    GENERATED_BODY()
public:
    AFableMoonwellWorld();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
private:
    void BuildEnvironment();
    UInstancedStaticMeshComponent* MakeBatch(FName Name,UStaticMesh* Mesh,FLinearColor Color,
        bool bBlocking=false,UMaterialInterface* OverrideMaterial=nullptr);
    static void Place(UInstancedStaticMeshComponent* Batch,FVector Location,FVector Scale,
        FRotator Rotation=FRotator::ZeroRotator);
    void AddLight(FName Name,FVector Location,FLinearColor Color,float Intensity,float Radius);
    UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> PaintMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> SealMaterials;
    bool bBuilding=false;
    int32 LastSealState=-1;
};
