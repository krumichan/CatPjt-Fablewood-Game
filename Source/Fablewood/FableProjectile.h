#pragma once

#include "CoreMinimal.h"
#include "FableRPGTypes.h"
#include "GameFramework/Actor.h"
#include "FableProjectile.generated.h"

class AController;
class AFableEnemy;
class USceneComponent;
class UStaticMeshComponent;

/** A server-created shot. Clients render replicated movement, never decide hits. */
UCLASS()
class FABLEWOOD_API AFableProjectile : public AActor
{
    GENERATED_BODY()

public:
    AFableProjectile();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Trusted server code only: the class has no client RPC for damage or targets. */
    void InitializeShot(const FVector& Direction, float Damage, AController* DamageInstigator,
        bool bUseArrow, float ExplosionRadius = 0.0f, float Speed = 1200.0f, float MaximumRange = 1800.0f,
        int32 PierceCount = 0);
    void SetTrialCastContext(const FFableTrialCastContext& Context);
    /** Freeze a server-authored fire payload before the shot starts moving. */
    void SetBurnPayload(float DamagePerTick, int32 TickCount = 3);
    int32 GetUniqueHitCount() const { return UniqueEnemyHits; }
    bool IsPiercingShot() const { return bPiercing; }

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY() TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Orb;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ArrowShaft;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ArrowHead;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals) bool bArrow = false;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals) bool bExplosive = false;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals) bool bImpacted = false;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals) bool bPiercing = false;
    UPROPERTY(Replicated) uint8 UniqueEnemyHits = 0;
    UPROPERTY() TWeakObjectPtr<AController> SourceController;

    FVector FlightVelocity = FVector::ZeroVector;
    float ShotDamage = 0.0f;
    float BlastRadius = 0.0f;
    float BurnDamagePerTick = 0.0f;
    int32 BurnTickCount = 0;
    bool bBurnPayloadSet = false;
    float RangeRemaining = 0.0f;
    bool bInitialized = false;
    int32 PiercesRemaining = 0;
    TSet<TWeakObjectPtr<AFableEnemy>> HitEnemies;
    FFableTrialCastContext TrialContext;

    UFUNCTION() void OnRep_Visuals();
    void ApplyEnemyHit(AFableEnemy* Enemy);
    void ResolveImpact(const FVector& ImpactLocation, AFableEnemy* DirectTarget);
};

