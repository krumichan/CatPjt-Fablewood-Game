#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableRPGTypes.h"
#include "FableEnemyBolt.generated.h"
class USceneComponent;
class UStaticMeshComponent;

/** Server swept water shot. Separate from hero shots so friendly targeting cannot leak. */
UCLASS()
class FABLEWOOD_API AFableEnemyBolt : public AActor
{
    GENERATED_BODY()
public:
    AFableEnemyBolt();
    // Damage is the final server-scaled value; this projectile never applies difficulty twice.
    void InitializeBolt(FVector Direction, float Damage, EFableRegion Region);
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    EFableRegion GetEncounterRegion() const { return EncounterRegion; }
    float GetDamageAmount() const { return DamageAmount; } // Authoritative value, useful for server validation.
protected:
    virtual void BeginPlay() override;
private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Droplet;
    UPROPERTY(Replicated) EFableRegion EncounterRegion = EFableRegion::Moonwell;
    UPROPERTY(ReplicatedUsing=OnRep_Impact) bool bImpacted = false;
    UFUNCTION() void OnRep_Impact();
    FVector Velocity = FVector::ZeroVector;
    float DamageAmount = 0;
    float RemainingRange = 1800;
    bool bInitialized = false;
};
