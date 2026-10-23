#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableRPGTypes.h"
#include "FableEnemy.generated.h"

class AFableCharacter;
class UCapsuleComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM()
enum class EFableEnemyState : uint8 { Idle, Chasing, Windup, Recovering, Defeated };

UENUM()
enum class EFableEnemyAttack : uint8 { Stomp, RootLine, WaterBolt, MoonRing, Moonfall };

/** Guardian and Ancient Warden: server-authoritative combat with replicated attack telegraphs. */
UCLASS()
class FABLEWOOD_API AFableEnemy : public AActor
{
    GENERATED_BODY()
public:
    AFableEnemy();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    void ConfigureEncounter(bool bAncientWarden, bool bInDungeon, EFableDifficulty Difficulty = EFableDifficulty::Normal);
    void ConfigureMoonwell(bool bMoonWarden, bool bWaterSpirit, EFableDifficulty Difficulty = EFableDifficulty::Normal);
    void ReceiveHit(float Damage, AController* InstigatorController);
    /** Trusted server-only movement slow; bosses resist both strength and duration. */
    bool ApplySlow(float Multiplier, float Duration);
    float GetSlowTimeRemaining() const;
    float GetSlowMultiplier() const;
    /** Trusted server-only damage over time, never a client-supplied hit. */
    bool ApplyBurn(float DamagePerTick, int32 TickCount, AController* InstigatorController);
    float GetBurnTimeRemaining() const;
    int32 GetBurnTickCount() const { return BurnTicksApplied; }
    float GetTotalBurnDamage() const { return TotalBurnDamage; }
    bool IsAlive() const { return Health > 0.0f && State != EFableEnemyState::Defeated; }
    bool IsBoss() const { return bBoss; }
    bool IsEnraged() const { return bBoss && Health <= MaxHealth * 0.5f; }
    float GetHealth() const { return Health; }
    float GetMaxHealth() const { return MaxHealth; }
    EFableEnemyAttack GetAttackPattern() const { return AttackPattern; }
    EFableEnemyState GetEnemyState() const { return State; }
    float GetWindupEndServerTime() const { return WindupEndServerTime; }
    TArray<FVector> GetTelegraphLocations() const { return TelegraphLocations; }
    EFableRegion GetEncounterRegion() const { return EncounterRegion; }
    EFableDifficulty GetEncounterDifficulty() const { return EncounterDifficulty; }
    float GetDamageMultiplier() const { return EncounterDifficulty == EFableDifficulty::Hard ? 1.25f : 1.0f; }
    bool IsRanged() const { return bRanged; }

protected:
    virtual void BeginPlay() override;

private:
    void FindTarget();
    void TickBurn();
    void ClearBurn();
    void SetState(EFableEnemyState NewState);
    void BeginAttack();
    void ApplyAreaAttack();
    void TickPresentation(float DeltaSeconds);
    void UpdateWarningGeometry();
    float GetMoveSpeed() const;
    float GetRecoveryTime() const;
    UFUNCTION() void OnRep_CombatVisuals();
    UFUNCTION(NetMulticast, Unreliable) void MulticastHitFeedback(float ActualDamage);
    void FlushHitFeedback(bool bForce = false);
    float GetSynchronizedServerTime() const;

    UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> Collision;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Crown;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> WarningRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> DamageText;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> WarningSegments;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> BranchArms;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> RootLegs;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> MoonAntlers;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> WaterLantern;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> WaterPetals;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CrownMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BarkHighlightMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CrownTipMaterial;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> WarningMaterial;

    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) float Health = 90.0f;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) float MaxHealth = 90.0f;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) EFableEnemyState State = EFableEnemyState::Idle;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) EFableEnemyAttack AttackPattern = EFableEnemyAttack::Stomp;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) bool bBoss = false;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) bool bDungeonGuardian = false;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) EFableRegion EncounterRegion = EFableRegion::Village;
    UPROPERTY(Replicated) EFableDifficulty EncounterDifficulty = EFableDifficulty::Normal;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) bool bRanged = false;
    UPROPERTY(ReplicatedUsing=OnRep_CombatVisuals) TArray<FVector> TelegraphLocations;
    UPROPERTY(Replicated) float WindupEndServerTime = 0.0f;
    UPROPERTY(Replicated) float WindupDuration = 0.9f;
    UPROPERTY(Replicated) float SlowMultiplier = 1.0f;
    UPROPERTY(Replicated) float SlowEndServerTime = 0.0f;
    UPROPERTY(Replicated) float BurnEndServerTime = 0.0f;
    UPROPERTY(Replicated) int32 BurnTicksApplied = 0;
    UPROPERTY(Replicated) float TotalBurnDamage = 0.0f;
    TWeakObjectPtr<AController> BurnInstigator;
    TWeakObjectPtr<AFableCharacter> BurnSourcePawn;
    float BurnDamagePerTick = 0.0f;
    float NextBurnServerTime = 0.0f;
    int32 BurnTicksRemaining = 0;

    static constexpr float AttackRadius = 230.0f;
    static constexpr float BossStompRadius = 380.0f;
    static constexpr float RootLineLength = 760.0f;
    static constexpr float RootLineHalfWidth = 135.0f;
    static constexpr float MoonRingInnerRadius = 270.0f;
    static constexpr float MoonRingOuterRadius = 900.0f;
    static constexpr float MoonfallRadius = 210.0f;

    TWeakObjectPtr<AFableCharacter> Target;
    float LastVisualHealth = 90.0f;
    float HitFlashRemaining = 0.0f;
    float DamageTextRemaining = 0.0f;
    float DisplayedDamage = 0.0f;
    float PendingFeedbackDamage = 0.0f;
    float NextHitFeedbackTime = 0.0f;
    float TargetSearchRemaining = 0.0f;
    float StateTime = 0.0f;
    float VisualStateTime = 0.0f;
    int32 AttackCount = 0;
    EFableEnemyState LastVisualState = EFableEnemyState::Idle;
};
