#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FableRPGTypes.h"
#include "FableGameState.generated.h"

UCLASS()
class FABLEWOOD_API AFableGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    UPROPERTY(Replicated, BlueprintReadOnly) bool bFrontEnd = false;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 DefeatedEnemies = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 RequiredEnemies = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Round = 1;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bObjectiveComplete = false;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableAdventureStage AdventureStage = EFableAdventureStage::Village;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 DungeonWave = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableRegion ActiveRegion = EFableRegion::Village;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableDifficulty ActiveDifficulty = EFableDifficulty::Normal;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 MoonSealMask = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 ActiveSealIndex = -1;
    UPROPERTY(Replicated, BlueprintReadOnly) float BossHealth = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly) float BossMaxHealth = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bPartyRecoveryPending = false;
    UPROPERTY(Replicated, BlueprintReadOnly) float PartyRecoveryEndServerTime = 0.0f;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bRetryAvailable = false;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableRegion RetryRegion = EFableRegion::Village;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableDifficulty RetryDifficulty = EFableDifficulty::Normal;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 PartyRecoveryCount = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EncounterSerial = 0;
    float GetPartyRecoveryTimeRemaining() const;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
