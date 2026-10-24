#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableRPGTypes.h"
#include "FableEnemyBolt.generated.h"
class USceneComponent;
class UStaticMeshComponent;

/** Server swept water shot. Separate from hero shots so friendly targeting cannot leak. */
/** 적이 발사하는 복제 투사체와 충돌 피해를 관리하는 액터. */
UCLASS()
class FABLEWOOD_API AFableEnemyBolt : public AActor
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableEnemyBolt();

    /** 적 발사체도 발사 당시의 원정 식별자를 유지한다. */
    FGuid GetExpeditionId() const
    {
        return ExpeditionId;
    }

    // Damage is the final server-scaled value; this projectile never applies difficulty twice.

    /** 적 투사체의 발사 위치·방향·피해 정보를 초기화한다. */
    void InitializeBolt(FVector Direction, float Damage, EFableRegion Region);

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    virtual void Tick(float DeltaSeconds) override;

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 전투가 속한 지역 값을 반환한다. */
    EFableRegion GetEncounterRegion() const
    {
        return EncounterRegion;
    }

    /** 투사체의 피해량 값을 반환한다. */
    float GetDamageAmount() const
    {
        return DamageAmount;
    } // Authoritative value, useful for server validation.

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

private:
    friend struct FFableExpeditionCombatTestAccess;

    UPROPERTY(Replicated)
    FGuid ExpeditionId;

    UPROPERTY()
    TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Droplet;
    UPROPERTY(Replicated)
    EFableRegion EncounterRegion = EFableRegion::Moonwell;
    UPROPERTY(ReplicatedUsing = OnRep_Impact)
    bool bImpacted = false;

    /** 복제된 충돌 변경을 화면과 관련 상태에 반영한다. */
    UFUNCTION()
    void OnRep_Impact();
    FVector Velocity = FVector::ZeroVector;
    float DamageAmount = 0;
    float RemainingRange = 1800;
    bool bInitialized = false;
};
