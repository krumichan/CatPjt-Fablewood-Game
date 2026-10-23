#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableWorld.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** A deterministic forest clearing with original art and identical static collision on every peer. */
UCLASS()
class FABLEWOOD_API AFableWorld : public AActor
{
    GENERATED_BODY()

public:
    AFableWorld();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;

protected:
    virtual void BeginPlay() override;

private:
    void BuildEnvironment();
    UInstancedStaticMeshComponent* MakeBatch(FName Name, UStaticMesh* Mesh, const FLinearColor& Color,
        bool bBlocking = false, UMaterialInterface* MaterialOverride = nullptr, float Roughness = 0.88f);
    static void Place(UInstancedStaticMeshComponent* Batch, const FVector& Location, const FVector& Scale,
        const FRotator& Rotation = FRotator::ZeroRotator);

    bool bBuildingEnvironment = false;

    UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> ShapeMaterial;
};
