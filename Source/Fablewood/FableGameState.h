#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FableRPGTypes.h"
#include "FableGameState.generated.h"

/** 파티와 전투 진행의 공유 상태를 클라이언트에 복제하는 게임 상태. */
UCLASS()
class FABLEWOOD_API AFableGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bFrontEnd = false;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 DefeatedEnemies = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 RequiredEnemies = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Round = 1;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bObjectiveComplete = false;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableAdventureStage AdventureStage = EFableAdventureStage::Village;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 DungeonWave = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableRegion ActiveRegion = EFableRegion::Village;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableDifficulty ActiveDifficulty = EFableDifficulty::Normal;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 MoonSealMask = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 ActiveSealIndex = -1;
    UPROPERTY(Replicated, BlueprintReadOnly)
    float BossHealth = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly)
    float BossMaxHealth = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bPartyRecoveryPending = false;
    UPROPERTY(Replicated, BlueprintReadOnly)
    float PartyRecoveryEndServerTime = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bRetryAvailable = false;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableRegion RetryRegion = EFableRegion::Village;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableDifficulty RetryDifficulty = EFableDifficulty::Normal;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 PartyRecoveryCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 EncounterSerial = 0;

    /** 파티 복구까지 남은 시간 값을 반환한다. */
    float GetPartyRecoveryTimeRemaining() const;

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
