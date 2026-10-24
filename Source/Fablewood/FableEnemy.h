#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableRPGTypes.h"
#include "FableTownTypes.h"
#include "FableEnemy.generated.h"

class AFableCharacter;
class UCapsuleComponent;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** 적의 추적·공격 예고·회복 상태 값의 종류를 정의한다. */
UENUM()
enum class EFableEnemyState : uint8
{
    Idle,
    Chasing,
    Windup,
    Recovering,
    Defeated
};

/** 적이 사용하는 공격 패턴 값의 종류를 정의한다. */
UENUM()
enum class EFableEnemyAttack : uint8
{
    Stomp,
    RootLine,
    WaterBolt,
    MoonRing,
    Moonfall
};

/** Guardian and Ancient Warden: server-authoritative combat with replicated attack telegraphs. */
/** 서버의 추적·공격·상태 이상과 복제된 적 표현을 관리하는 액터. */
UCLASS()
class FABLEWOOD_API AFableEnemy : public AActor
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableEnemy();

    /** 서버가 최초 생성 시 고정한 원정 식별자를 반환한다. */
    FGuid GetExpeditionId() const
    {
        return ExpeditionId;
    }

    /** 서버의 최초 원정 바인딩만 허용하고 이후 적 재사용을 막는다. */
    void BindExpedition(FGuid InExpeditionId);

    /** 공개 필드 적은 서버 등록 실행 세대를 생성 시 한 번 고정한다. */
    void BindPublicField(FFablePublicAreaKey InPublicArea);

    /** 물리적 위치가 바뀌어도 원래 공개 실행의 키를 유지한다. */
    FFablePublicAreaKey GetPublicAreaKey() const
    {
        return PublicAreaKey;
    }

    /** 재생성된 동명 적도 구별하는 서버 발급 개체 식별자다. */
    FGuid GetSpawnId() const
    {
        return SpawnId;
    }

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    virtual void Tick(float DeltaSeconds) override;

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 적의 지역·난이도·보스 여부와 전투 수치를 설정한다. */
    void ConfigureEncounter(bool bAncientWarden, bool bInDungeon,
                            EFableDifficulty Difficulty = EFableDifficulty::Normal);

    /** 달샘 전투에 필요한 적의 추가 설정을 적용한다. */
    void ConfigureMoonwell(bool bMoonWarden, bool bWaterSpirit, EFableDifficulty Difficulty = EFableDifficulty::Normal);

    /** 권한과 생존 상태를 확인하고 보호막·방어력을 반영해 피해를 적용한다. */
    void ReceiveHit(float Damage, AController* InstigatorController);

    /** Trusted server-only movement slow; bosses resist both strength and duration. */

    /** 효과 조건을 확인하고 적의 둔화 배율과 만료 시각을 갱신한다. */
    bool ApplySlow(float Multiplier, float Duration, AController* InstigatorController = nullptr);

    /** 둔화 효과의 남은 시간 값을 반환한다. */
    float GetSlowTimeRemaining() const;

    /** 둔화에 적용할 속도 배율 값을 반환한다. */
    float GetSlowMultiplier() const;

    /** Trusted server-only damage over time, never a client-supplied hit. */

    /** 유효한 화상 피해의 출처·지속 시간·주기를 설정한다. */
    bool ApplyBurn(float DamagePerTick, int32 TickCount, AController* InstigatorController);

    /** 화상 효과의 남은 시간 값을 반환한다. */
    float GetBurnTimeRemaining() const;

    /** 화상 피해가 적용된 횟수 값을 반환한다. */
    int32 GetBurnTickCount() const
    {
        return BurnTicksApplied;
    }

    /** 화상으로 가한 누적 피해량 값을 반환한다. */
    float GetTotalBurnDamage() const
    {
        return TotalBurnDamage;
    }

    /** 생존 여부를 확인한다. */
    bool IsAlive() const
    {
        return Health > 0.0f && State != EFableEnemyState::Defeated;
    }

    /** 보스 여부를 확인한다. */
    bool IsBoss() const
    {
        return bBoss;
    }

    /** 보스 분노 여부를 확인한다. */
    bool IsEnraged() const
    {
        return bBoss && Health <= MaxHealth * 0.5f;
    }

    /** 현재 체력 값을 반환한다. */
    float GetHealth() const
    {
        return Health;
    }

    /** 최대 체력 값을 반환한다. */
    float GetMaxHealth() const
    {
        return MaxHealth;
    }

    /** 적의 현재 공격 패턴 값을 반환한다. */
    EFableEnemyAttack GetAttackPattern() const
    {
        return AttackPattern;
    }

    /** 적의 전투 상태 값을 반환한다. */
    EFableEnemyState GetEnemyState() const
    {
        return State;
    }

    /** 공격 예고가 끝나는 서버 시각 값을 반환한다. */
    float GetWindupEndServerTime() const
    {
        return WindupEndServerTime;
    }

    /** 공격 예고 영역의 위치 목록 값을 반환한다. */
    TArray<FVector> GetTelegraphLocations() const
    {
        return TelegraphLocations;
    }

    /** 전투가 속한 지역 값을 반환한다. */
    EFableRegion GetEncounterRegion() const
    {
        return EncounterRegion;
    }

    /** 전투에 적용된 난이도 값을 반환한다. */
    EFableDifficulty GetEncounterDifficulty() const
    {
        return EncounterDifficulty;
    }

    /** 적 공격의 피해 배율 값을 반환한다. */
    float GetDamageMultiplier() const
    {
        return EncounterDifficulty == EFableDifficulty::Hard ? 1.25f : 1.0f;
    }

    /** 원거리 공격 여부를 확인한다. */
    bool IsRanged() const
    {
        return bRanged;
    }

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

private:
    friend struct FFableExpeditionCombatTestAccess;

    /** 이동된 자기 원정의 실제 지역 안에 있는 좌표만 허용한다. */
    bool IsInsideEncounter(const FVector& Location) const;

    UPROPERTY(Replicated)
    FGuid ExpeditionId;
    UPROPERTY(Replicated)
    FFablePublicAreaKey PublicAreaKey;
    UPROPERTY(Replicated)
    FGuid SpawnId;
    bool bExpeditionBound = false;
    FGuid BurnExpeditionId;
    FFablePublicAreaKey BurnPublicAreaKey;

    /** 공격 가능한 플레이어 중 적의 추적 대상을 선택한다. */
    void FindTarget();

    /** 서버 시간에 따라 화상 피해를 적용하고 만료 상태를 정리한다. */
    void TickBurn();

    /** 화상의 누적 상태와 적용 대상을 해제한다. */
    void ClearBurn();

    /** 현재 온라인 상태 값을 설정한다. */
    void SetState(EFableEnemyState NewState);

    /** 적의 공격 패턴을 정하고 공격 예고를 시작한다. */
    void BeginAttack();

    /** 공격 예고 영역 안에 있는 유효한 대상에게 피해를 적용한다. */
    void ApplyAreaAttack();

    /** 적의 복제된 전투 상태를 자세·색상·예고 표시로 표현한다. */
    void TickPresentation(float DeltaSeconds);

    /** 적 공격의 예고 영역을 화면 표시용 기하로 갱신한다. */
    void UpdateWarningGeometry();

    /** 적의 현재 이동 속도 값을 반환한다. */
    float GetMoveSpeed() const;

    /** 공격 후 회복에 필요한 시간 값을 반환한다. */
    float GetRecoveryTime() const;

    /** 복제된 전투 외형 변경을 화면과 관련 상태에 반영한다. */
    UFUNCTION()
    void OnRep_CombatVisuals();

    /** 적중 피드백를 수신한 클라이언트에서 재생한다. */
    UFUNCTION(NetMulticast, Unreliable)
    void MulticastHitFeedback(float ActualDamage);

    /** 누적된 적중 피드백을 묶어 클라이언트에 전달한다. */
    void FlushHitFeedback(bool bForce = false);

    /** 복제된 시간 기준으로 보정한 서버 시각 값을 반환한다. */
    float GetSynchronizedServerTime() const;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UCapsuleComponent> Collision;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Crown;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<USceneComponent> WarningRoot;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UTextRenderComponent> DamageText;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> WarningSegments;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> BranchArms;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> RootLegs;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> MoonAntlers;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WaterLantern;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> WaterPetals;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CrownMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> BarkHighlightMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> CrownTipMaterial;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> WarningMaterial;

    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    float Health = 90.0f;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    float MaxHealth = 90.0f;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    EFableEnemyState State = EFableEnemyState::Idle;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    EFableEnemyAttack AttackPattern = EFableEnemyAttack::Stomp;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    bool bBoss = false;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    bool bDungeonGuardian = false;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    EFableRegion EncounterRegion = EFableRegion::Village;
    UPROPERTY(Replicated)
    EFableDifficulty EncounterDifficulty = EFableDifficulty::Normal;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    bool bRanged = false;
    UPROPERTY(ReplicatedUsing = OnRep_CombatVisuals)
    TArray<FVector> TelegraphLocations;
    UPROPERTY(Replicated)
    float WindupEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    float WindupDuration = 0.9f;
    UPROPERTY(Replicated)
    float SlowMultiplier = 1.0f;
    UPROPERTY(Replicated)
    float SlowEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    float BurnEndServerTime = 0.0f;
    UPROPERTY(Replicated)
    int32 BurnTicksApplied = 0;
    UPROPERTY(Replicated)
    float TotalBurnDamage = 0.0f;
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
