#pragma once

#include "CoreMinimal.h"
#include "FableRPGTypes.h"
#include "Engine/NetSerialization.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"
#include "FableCharacter.generated.h"

class UCameraComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class USpringArmComponent;
class UStaticMeshComponent;

/** Server-authoritative ranger with authored, articulated fairy-tale art. */
UCLASS()
class FABLEWOOD_API AFableCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AFableCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetHealth() const { return Health; }
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetMaxHealth() const { return MaxHealth; }
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    bool IsAlive() const { return LifeState == EFableLifeState::Active && Health > 0.0f; }
    EFableLifeState GetLifeState() const { return LifeState; }
    bool IsDowned() const { return LifeState == EFableLifeState::Downed; }
    bool IsDefeated() const { return LifeState == EFableLifeState::Defeated; }
    float GetDownedTimeRemaining() const;
    float GetDefeatedServerTime() const { return DefeatedServerTime; }
    float GetReviveProgress() const;
    AFableCharacter* GetReviveTarget() const { return ReviveTarget.Get(); }
    AFableCharacter* GetReviver() const { return Reviver.Get(); }
    float GetReviveProtectionRemaining() const;
    int32 GetDownedCount() const { return DownedCount; }
    int32 GetRevivesPerformedCount() const { return RevivesPerformedCount; }
    int32 GetTimesRevivedCount() const { return TimesRevivedCount; }
    int32 GetReviveCancelCount() const { return ReviveCancelCount; }
    int32 GetReviveProtectionBlockedHits() const { return ReviveProtectionBlockedHits; }

    /** Only the server can begin a channel; duplicate requests never reset it. */
    bool BeginRevive(AFableCharacter* Target);
    /** Server cleanup; an owning client may also request cancellation. */
    void CancelRevive();
    float GetAttackCooldownRemaining() const;
    float GetSkillCooldownRemaining(int32 Slot = 0) const;
    float GetSkillCooldownDuration(int32 Slot) const;
    float GetShieldAmount() const;
    float GetShieldTimeRemaining() const;
    int32 GetSkillCastCount(int32 Slot) const;
    float GetTotalShieldAbsorbed() const { return TotalShieldAbsorbed; }
    void RequestActiveSkill(int32 Slot);
    float GetDodgeCooldownRemaining() const;
    float GetDodgeCooldownDuration() const;
    float GetMovementSpeedLimit() const;
    float GetBuildChangeCooldownRemaining() const;
    // Damage multiplier is relative to attack power after the existing skill-rank bonus.
    float GetSkillDamageMultiplier(int32 Slot) const;
    float GetSkillRange(int32 Slot) const;
    float GetSkillShieldCapacity(int32 Slot = 2) const;
    float GetSkillShieldDuration(int32 Slot = 2) const;
    float GetTalentBurnDamageDealt() const { return TalentBurnDamageDealt; }
    int32 GetTalentSlowHits() const { return TalentSlowHits; }
    int32 GetTalentAllyShieldsGranted() const { return TalentAllyShieldsGranted; }
    /** Trusted enemy DoT bookkeeping only; never exposed as a client RPC. */
    void RecordTalentBurnDamage(float ActualDamage);

    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    EFableClass GetHeroClass() const;
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetMana() const { return Mana; }
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetMaxMana() const { return 100.0f; }
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetSkillManaCost(int32 Slot = 0) const;

    /** Server-only safe party travel and respawn checkpoint. */
    void SetCheckpoint(const FVector& Location, const FRotator& Rotation, bool bRestore = true);

    /** Server-only item recovery; full resources and defeated actors reject use. */
    bool RestoreResource(bool bMana, float Amount);

    /** Only authoritative enemy/environment code may apply damage. */
    void ReceiveHit(float Damage);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UCameraComponent> FollowCamera;
    UPROPERTY(VisibleAnywhere, Category = "Appearance")
    TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY()
    TObjectPtr<USceneComponent> LeftArm;
    UPROPERTY()
    TObjectPtr<USceneComponent> RightArm;
    UPROPERTY()
    TObjectPtr<USceneComponent> LeftForearm;
    UPROPERTY()
    TObjectPtr<USceneComponent> RightForearm;
    UPROPERTY()
    TObjectPtr<USceneComponent> LeftLeg;
    UPROPERTY()
    TObjectPtr<USceneComponent> RightLeg;
    UPROPERTY()
    TObjectPtr<USceneComponent> LeftKnee;
    UPROPERTY()
    TObjectPtr<USceneComponent> RightKnee;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Cape;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Torso;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> SwordBlade;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> VisualParts;
    UPROPERTY()
    TArray<FLinearColor> PartColors;
    UPROPERTY()
    TArray<float> PartRoughness;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> SkillPetals;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> TorsoMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> BladeMaterial;
    UPROPERTY()
    TObjectPtr<USceneComponent> BowRoot;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> SwordParts;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> StaffParts;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> BowParts;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> ClassClothMaterials;

    UPROPERTY(EditDefaultsOnly, ReplicatedUsing = OnRep_Health, Category = "Combat", meta = (ClampMin = "1.0"))
    float MaxHealth = 100.0f;
    UPROPERTY(ReplicatedUsing = OnRep_Health)
    float Health = 100.0f;
    UPROPERTY(ReplicatedUsing = OnRep_Health)
    EFableLifeState LifeState = EFableLifeState::Active;
    UPROPERTY(Replicated) float DownedEndServerTime = 0.0f;
    UPROPERTY(Replicated) float DefeatedServerTime = -1.0f;
    UPROPERTY(Replicated) TObjectPtr<AFableCharacter> ReviveTarget;
    UPROPERTY(Replicated) TObjectPtr<AFableCharacter> Reviver;
    UPROPERTY(Replicated) float ReviveStartServerTime = -1.0f;
    UPROPERTY(Replicated) float ReviveProtectionEndServerTime = 0.0f;
    UPROPERTY(Replicated) int32 DownedCount = 0;
    UPROPERTY(Replicated) int32 RevivesPerformedCount = 0;
    UPROPERTY(Replicated) int32 TimesRevivedCount = 0;
    UPROPERTY(Replicated) int32 ReviveCancelCount = 0;
    UPROPERTY(Replicated) int32 ReviveProtectionBlockedHits = 0;
    bool bLegacyRecovery = false;
    FVector ReviveStartLocation = FVector::ZeroVector;
    float LastReviveRequestServerTime = -10.0f;

    UPROPERTY(Replicated)
    float Mana = 100.0f;
    UPROPERTY(Replicated)
    bool bDodging = false;
    UPROPERTY(Replicated)
    float NextAttackServerTime = 0.0f;
    UPROPERTY(Replicated)
    float NextSkillServerTime = 0.0f;
    UPROPERTY(Replicated) float NextSecondSkillServerTime = 0.0f;
    UPROPERTY(Replicated) float NextThirdSkillServerTime = 0.0f;
    UPROPERTY(Replicated) float ShieldAmount = 0.0f;
    UPROPERTY(Replicated) float ShieldEndServerTime = 0.0f;
    UPROPERTY(Replicated) float TotalShieldAbsorbed = 0.0f;
    UPROPERTY(Replicated) TArray<int32> SkillCastCounts;
    UPROPERTY(Replicated)
    float NextDodgeServerTime = 0.0f;

    UPROPERTY(Replicated) float CombatBuildEndServerTime = 0.0f;
    UPROPERTY(Replicated) float TalentBurnDamageDealt = 0.0f;
    UPROPERTY(Replicated) int32 TalentSlowHits = 0;
    UPROPERTY(Replicated) int32 TalentAllyShieldsGranted = 0;

    float DodgeEndServerTime = 0.0f;
    float ActionLockEndServerTime = 0.0f;
    float LastRequestServerTime = -1.0f;
    float LastAttackServerTime = -10.0f;
    float NextLocalRequestTime = 0.0f;
    uint8 ComboIndex = 0;
    FVector SafeSpawnLocation = FVector::ZeroVector;
    FRotator SafeSpawnRotation = FRotator::ZeroRotator;
    FTimerHandle RespawnTimer;

    float BaseMaxHealth = 100.0f;
    EFableClass LastVisualClass = EFableClass::Warrior;
    bool bClassAppearanceInitialized = false;
    int32 LastVisualTier = 0;
    EFableClass LastServerClass = EFableClass::Warrior;
    uint8 SkillVisualCue = 3;
    float WalkPhase = 0.0f;
    float VisualSpeed = 0.0f;
    float AttackVisualRemaining = 0.0f;
    float AttackVisualDuration = 0.34f;
    float SkillVisualRemaining = 0.0f;
    float HurtVisualRemaining = 0.0f;
    uint8 VisualComboIndex = 0;

    void MoveForward(float Value);
    void MoveRight(float Value);
    void Turn(float Value);
    void LookUp(float Value);
    void StartJump();
    void FinishJump();
    void RequestAttack();
    void RequestSkill();
    void RequestSecondSkill();
    void RequestThirdSkill();
    void RequestDodge();
    void RequestAction(uint8 Action);

    // No client-supplied target, damage, cooldown or position is trusted.
    UFUNCTION(Server, Unreliable)
    void ServerRequestAction(uint8 Action);
    UFUNCTION(Server, Reliable)
    void ServerCancelRevive();
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastCombatCue(uint8 Cue);
    UFUNCTION(Client, Unreliable)
    void ClientConfirmDodge(FVector_NetQuantizeNormal Direction);
    UFUNCTION()
    void OnRep_Health();

    void PerformAttack(bool bSkill);
    void PerformActiveSkill(int32 Slot);
    void ApplySkillArea(float Damage, float Range, bool bFrontal, bool bSlow, const FFableTrialCastContext& TrialContext);
    UFUNCTION(Client, Unreliable) void ClientConfirmSkillMove(FVector_NetQuantizeNormal Direction, float Speed);
    void PerformRangedAttack(EFableClass HeroClass, bool bSkill, float AttackPower, int32 SkillSlot, const FFableTrialCastContext& TrialContext);
    void SynchronizeStats();
    int32 GetCombatTalentRank(EFableTalentPath Path) const;
    void ShareGuardianShield(int32 Rank);
    void ApplyClassAppearance();
    void PerformDodge();
    void RespawnAtSafePoint();
    bool CanMaintainRevive(const AFableCharacter* Target) const;
    void CancelReviveInternal(const TCHAR* Reason);
    void CompleteRevive();
    void EnterDowned();
    void EnterDefeated(const TCHAR* Reason);
    void TickLifeState();
    void AnimateBody(float DeltaSeconds);
    float GetSynchronizedServerTime() const;
};


