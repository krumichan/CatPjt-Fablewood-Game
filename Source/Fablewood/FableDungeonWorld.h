#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableDungeonWorld.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** A replicated anchor with deterministic, locally built town services and ruin geometry. */
UCLASS()
class FABLEWOOD_API AFableDungeonWorld : public AActor
{
    GENERATED_BODY()
public:
    AFableDungeonWorld();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
protected:
    virtual void BeginPlay() override;
private:
    void BuildEnvironment();
    void AddVillager(const FString& Id, FVector Location, FLinearColor Cloth, FLinearColor Hair);
    UInstancedStaticMeshComponent* MakeBatch(FName Name, UStaticMesh* Mesh, FLinearColor Color, bool bBlocking);
    void AddText(FName Name, const FString& Text, FVector Location, float Size, FColor Color);
    void AddLight(FName Name, FVector Location, FLinearColor Color, float Intensity, float Radius);
    static void Place(UInstancedStaticMeshComponent* Batch, FVector Location, FVector Scale, FRotator Rotation = FRotator::ZeroRotator);
    bool bBuilding = false;
    UPROPERTY() TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> CylinderMesh;
    UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> PaintMaterial;
};
