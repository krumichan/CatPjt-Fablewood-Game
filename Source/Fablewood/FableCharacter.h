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
/** 플레이어의 이동·전투·부활과 직업별 외형을 관리하는 복제 캐릭터. */
UCLASS()
class FABLEWOOD_API AFableCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableCharacter();

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    virtual void Tick(float DeltaSeconds) override;

    /** 캐릭터의 이동·시점·전투 입력을 동작에 연결한다. */
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 현재 체력 값을 반환한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetHealth() const
    {
        return Health;
    }

    /** 최대 체력 값을 반환한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetMaxHealth() const
    {
        return MaxHealth;
    }

    /** 생존 여부를 확인한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    bool IsAlive() const
    {
        return LifeState == EFableLifeState::Active && Health > 0.0f;
    }

    /** 생존 상태 값을 반환한다. */
    EFableLifeState GetLifeState() const
    {
        return LifeState;
    }

    /** 행동 불능 여부를 확인한다. */
    bool IsDowned() const
    {
        return LifeState == EFableLifeState::Downed;
    }

    /** 쓰러짐 확정 여부를 확인한다. */
    bool IsDefeated() const
    {
        return LifeState == EFableLifeState::Defeated;
    }

    /** 행동 불능 상태의 남은 구조 시간 값을 반환한다. */
    float GetDownedTimeRemaining() const;

    /** 쓰러짐이 확정된 서버 시각 값을 반환한다. */
    float GetDefeatedServerTime() const
    {
        return DefeatedServerTime;
    }

    /** 동료 부활의 진행률 값을 반환한다. */
    float GetReviveProgress() const;

    /** 부활 중인 동료 값을 반환한다. */
    AFableCharacter* GetReviveTarget() const
    {
        return ReviveTarget.Get();
    }

    /** 부활을 시전하는 동료 값을 반환한다. */
    AFableCharacter* GetReviver() const
    {
        return Reviver.Get();
    }

    /** 부활 보호 효과의 남은 시간 값을 반환한다. */
    float GetReviveProtectionRemaining() const;

    /** 행동 불능 상태에 빠진 횟수 값을 반환한다. */
    int32 GetDownedCount() const
    {
        return DownedCount;
    }

    /** 동료를 부활시킨 횟수 값을 반환한다. */
    int32 GetRevivesPerformedCount() const
    {
        return RevivesPerformedCount;
    }

    /** 동료에게 부활받은 횟수 값을 반환한다. */
    int32 GetTimesRevivedCount() const
    {
        return TimesRevivedCount;
    }

    /** 부활 시전이 취소된 횟수 값을 반환한다. */
    int32 GetReviveCancelCount() const
    {
        return ReviveCancelCount;
    }

    /** 부활 보호 효과가 막은 피격 횟수 값을 반환한다. */
    int32 GetReviveProtectionBlockedHits() const
    {
        return ReviveProtectionBlockedHits;
    }

    /** Only the server can begin a channel; duplicate requests never reset it. */

    /** 서버에서 동료 부활 조건을 검사하고 중복 없이 시전을 시작한다. */
    bool BeginRevive(AFableCharacter* Target);

    /** Server cleanup; an owning client may also request cancellation. */

    /** 진행 중인 동료 부활의 취소를 처리하거나 서버에 요청한다. */
    void CancelRevive();

    /** 기본 공격의 남은 재사용 대기시간 값을 반환한다. */
    float GetAttackCooldownRemaining() const;

    /** 선택한 스킬의 남은 재사용 대기시간 값을 반환한다. */
    float GetSkillCooldownRemaining(int32 Slot = 0) const;

    /** 선택한 스킬의 전체 재사용 대기시간 값을 반환한다. */
    float GetSkillCooldownDuration(int32 Slot) const;

    /** 남은 보호막 흡수량 값을 반환한다. */
    float GetShieldAmount() const;

    /** 보호막의 남은 지속 시간 값을 반환한다. */
    float GetShieldTimeRemaining() const;

    /** 해당 스킬의 시전 횟수 값을 반환한다. */
    int32 GetSkillCastCount(int32 Slot) const;

    /** 보호막이 흡수한 누적 피해량 값을 반환한다. */
    float GetTotalShieldAbsorbed() const
    {
        return TotalShieldAbsorbed;
    }

    /** 사용형 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestActiveSkill(int32 Slot);

    /** 회피의 남은 재사용 대기시간 값을 반환한다. */
    float GetDodgeCooldownRemaining() const;

    /** 회피의 전체 재사용 대기시간 값을 반환한다. */
    float GetDodgeCooldownDuration() const;

    /** 현재 상태에서 허용되는 이동 속도 값을 반환한다. */
    float GetMovementSpeedLimit() const;

    /** 빌드 변경 후 남은 대기시간 값을 반환한다. */
    float GetBuildChangeCooldownRemaining() const;

    // Damage multiplier is relative to attack power after the existing skill-rank bonus.

    /** 스킬 피해 배율 값을 반환한다. */
    float GetSkillDamageMultiplier(int32 Slot) const;

    /** 스킬 적용 거리 값을 반환한다. */
    float GetSkillRange(int32 Slot) const;

    /** 스킬 보호막의 최대 흡수량 값을 반환한다. */
    float GetSkillShieldCapacity(int32 Slot = 2) const;

    /** 스킬 보호막의 지속 시간 값을 반환한다. */
    float GetSkillShieldDuration(int32 Slot = 2) const;

    /** 특성 화상으로 가한 누적 피해량 값을 반환한다. */
    float GetTalentBurnDamageDealt() const
    {
        return TalentBurnDamageDealt;
    }

    /** 특성 둔화를 적용한 횟수 값을 반환한다. */
    int32 GetTalentSlowHits() const
    {
        return TalentSlowHits;
    }

    /** 특성으로 동료에게 부여한 보호막 수 값을 반환한다. */
    int32 GetTalentAllyShieldsGranted() const
    {
        return TalentAllyShieldsGranted;
    }

    /** Trusted enemy DoT bookkeeping only; never exposed as a client RPC. */

    /** 서버에서 실제로 적용한 특성 화상 피해를 누적한다. */
    void RecordTalentBurnDamage(float ActualDamage);

    /** 현재 직업 값을 반환한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    EFableClass GetHeroClass() const;

    /** 현재 마나 값을 반환한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetMana() const
    {
        return Mana;
    }

    /** 최대 마나 값을 반환한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetMaxMana() const
    {
        return 100.0f;
    }

    /** 선택한 스킬의 마나 소모량 값을 반환한다. */
    UFUNCTION(BlueprintPure, Category = "Fablewood|Combat")
    float GetSkillManaCost(int32 Slot = 0) const;

    /** Server-only safe party travel and respawn checkpoint. */

    /** 서버에서 안전 복귀 위치와 회전·자원 회복 여부를 설정한다. */
    void SetCheckpoint(const FVector& Location, const FRotator& Rotation, bool bRestore = true);

    /** Server-only item recovery; full resources and defeated actors reject use. */

    /** 살아 있는 캐릭터의 체력 또는 마나를 허용량만큼 회복한다. */
    bool RestoreResource(bool bMana, float Amount);

    /** Only authoritative enemy/environment code may apply damage. */

    /** 권한과 생존 상태를 확인하고 보호막·방어력을 반영해 피해를 적용한다. */
    void ReceiveHit(float Damage);

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 월드 이탈 시 등록한 이벤트와 진행 중인 작업을 정리한다. */
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
    UPROPERTY(Replicated)
    float DownedEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    float DefeatedServerTime = -1.0f;
    UPROPERTY(Replicated)
    TObjectPtr<AFableCharacter> ReviveTarget;
    UPROPERTY(Replicated)
    TObjectPtr<AFableCharacter> Reviver;
    UPROPERTY(Replicated)
    float ReviveStartServerTime = -1.0f;
    UPROPERTY(Replicated)
    float ReviveProtectionEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    int32 DownedCount = 0;
    UPROPERTY(Replicated)
    int32 RevivesPerformedCount = 0;
    UPROPERTY(Replicated)
    int32 TimesRevivedCount = 0;
    UPROPERTY(Replicated)
    int32 ReviveCancelCount = 0;
    UPROPERTY(Replicated)
    int32 ReviveProtectionBlockedHits = 0;
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
    UPROPERTY(Replicated)
    float NextSecondSkillServerTime = 0.0f;
    UPROPERTY(Replicated)
    float NextThirdSkillServerTime = 0.0f;
    UPROPERTY(Replicated)
    float ShieldAmount = 0.0f;
    UPROPERTY(Replicated)
    float ShieldEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    float TotalShieldAbsorbed = 0.0f;
    UPROPERTY(Replicated)
    TArray<int32> SkillCastCounts;
    UPROPERTY(Replicated)
    float NextDodgeServerTime = 0.0f;

    UPROPERTY(Replicated)
    float CombatBuildEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    float TalentBurnDamageDealt = 0.0f;
    UPROPERTY(Replicated)
    int32 TalentSlowHits = 0;
    UPROPERTY(Replicated)
    int32 TalentAllyShieldsGranted = 0;

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

    /** 앞뒤 이동 입력을 캐릭터의 이동 방향에 반영한다. */
    void MoveForward(float Value);

    /** 좌우 이동 입력을 캐릭터의 이동 방향에 반영한다. */
    void MoveRight(float Value);

    /** 수평 시점 회전 입력을 반영한다. */
    void Turn(float Value);

    /** 수직 시점 회전 입력을 반영한다. */
    void LookUp(float Value);

    /** 현재 행동 조건을 확인하고 점프를 시작한다. */
    void StartJump();

    /** 점프 입력의 유지 상태를 해제한다. */
    void FinishJump();

    /** 기본 공격 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestAttack();

    /** 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestSkill();

    /** 두 번째 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestSecondSkill();

    /** 세 번째 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestThirdSkill();

    /** 회피 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestDodge();

    /** 동작 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestAction(uint8 Action);

    // No client-supplied target, damage, cooldown or position is trusted.

    /** 전투 동작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Unreliable)
    void ServerRequestAction(uint8 Action);

    /** 동료 부활 취소 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerCancelRevive();

    /** 전투 피드백를 수신한 클라이언트에서 재생한다. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastCombatCue(uint8 Cue);

    /** 서버가 전달한 확인 회피 결과를 소유 클라이언트에 반영한다. */
    UFUNCTION(Client, Unreliable)
    void ClientConfirmDodge(FVector_NetQuantizeNormal Direction);

    /** 복제된 현재 체력 변경을 화면과 관련 상태에 반영한다. */
    UFUNCTION()
    void OnRep_Health();

    /** 기본 공격의 사용 조건과 직업에 맞는 타격을 처리한다. */
    void PerformAttack(bool bSkill);

    /** 스킬 사용 조건을 검사하고 비용·대기시간·직업별 효과를 적용한다. */
    void PerformActiveSkill(int32 Slot);

    /** 스킬 범위 안의 유효한 적에게 피해와 부가 효과를 적용한다. */
    void ApplySkillArea(float Damage, float Range, bool bFrontal, bool bSlow,
                        const FFableTrialCastContext& TrialContext);

    /** 서버가 전달한 확인 스킬 이동 결과를 소유 클라이언트에 반영한다. */
    UFUNCTION(Client, Unreliable)
    void ClientConfirmSkillMove(FVector_NetQuantizeNormal Direction, float Speed);

    /** 현재 직업의 원거리 공격 투사체를 생성한다. */
    void PerformRangedAttack(EFableClass HeroClass, bool bSkill, float AttackPower, int32 SkillSlot,
                             const FFableTrialCastContext& TrialContext);

    /** 직업·장비·빌드 변경을 캐릭터의 전투 수치에 반영한다. */
    void SynchronizeStats();

    /** 전투에 적용할 특성 단계 값을 반환한다. */
    int32 GetCombatTalentRank(EFableTalentPath Path) const;

    /** 수호 특성의 조건을 만족하는 주변 동료에게 보호막을 나눈다. */
    void ShareGuardianShield(int32 Rank);

    /** 현재 직업에 맞는 무기와 의상 표시를 갱신한다. */
    void ApplyClassAppearance();

    /** 회피 사용 조건을 확인하고 이동과 재사용 대기시간을 적용한다. */
    void PerformDodge();

    /** 서버가 관리하는 안전 지점으로 캐릭터를 복귀시킨다. */
    void RespawnAtSafePoint();

    /** 거리·생존 상태·시전자를 확인해 부활 시전 유지 여부를 판단한다. */
    bool CanMaintainRevive(const AFableCharacter* Target) const;

    /** 시전자와 대상에 연결된 부활 진행 상태를 함께 해제한다. */
    void CancelReviveInternal(const TCHAR* Reason);

    /** 동료 부활을 완료하고 체력과 잠시 유지되는 보호 효과를 부여한다. */
    void CompleteRevive();

    /** 구조 가능한 행동 불능 상태로 전환하고 시전을 정리한다. */
    void EnterDowned();

    /** 구조 시간이 끝난 캐릭터의 쓰러짐을 확정한다. */
    void EnterDefeated(const TCHAR* Reason);

    /** 행동 불능 제한 시간과 부활 시전의 완료·취소를 갱신한다. */
    void TickLifeState();

    /** 이동·공격 상태를 캐릭터 부품의 자세에 반영한다. */
    void AnimateBody(float DeltaSeconds);

    /** 복제된 시간 기준으로 보정한 서버 시각 값을 반환한다. */
    float GetSynchronizedServerTime() const;
};
