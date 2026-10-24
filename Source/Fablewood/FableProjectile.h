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
/** 플레이어 원거리 공격의 이동·적중·관통 효과를 관리하는 복제 투사체. */
UCLASS()
class FABLEWOOD_API AFableProjectile : public AActor
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableProjectile();

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    virtual void Tick(float DeltaSeconds) override;

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** Trusted server code only: the class has no client RPC for damage or targets. */

    /** 플레이어 투사체의 소유자·방향·피해 정보를 초기화한다. */
    void InitializeShot(const FVector& Direction, float Damage, AController* DamageInstigator, bool bUseArrow,
                        float ExplosionRadius = 0.0f, float Speed = 1200.0f, float MaximumRange = 1800.0f,
                        int32 PierceCount = 0);

    /** 전직 시험 시전 문맥 값을 설정한다. */
    void SetTrialCastContext(const FFableTrialCastContext& Context);

    /** Freeze a server-authored fire payload before the shot starts moving. */

    /** 화상 적용 정보 값을 설정한다. */
    void SetBurnPayload(float DamagePerTick, int32 TickCount = 3);

    /** 중복을 제외한 투사체 적중 수 값을 반환한다. */
    int32 GetUniqueHitCount() const
    {
        return UniqueEnemyHits;
    }

    /** 관통 투사체 여부를 확인한다. */
    bool IsPiercingShot() const
    {
        return bPiercing;
    }

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Orb;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ArrowShaft;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> ArrowHead;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals)
    bool bArrow = false;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals)
    bool bExplosive = false;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals)
    bool bImpacted = false;
    UPROPERTY(ReplicatedUsing = OnRep_Visuals)
    bool bPiercing = false;
    UPROPERTY(Replicated)
    uint8 UniqueEnemyHits = 0;
    UPROPERTY()
    TWeakObjectPtr<AController> SourceController;

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

    /** 복제된 외형 변경을 화면과 관련 상태에 반영한다. */
    UFUNCTION()
    void OnRep_Visuals();

    /** 중복 적중을 막고 유효한 적에게 투사체 효과를 적용한다. */
    void ApplyEnemyHit(AFableEnemy* Enemy);

    /** 투사체 충돌의 결과와 표시 상태를 확정한다. */
    void ResolveImpact(const FVector& ImpactLocation, AFableEnemy* DirectTarget);
};
