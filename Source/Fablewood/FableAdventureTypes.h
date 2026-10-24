#pragma once

#include "CoreMinimal.h"
#include "FableRPGTypes.h"
#include "FableAdventureTypes.generated.h"

/** 필드와 각 원정이 독립적으로 복제하는 순수 진행 값. */
USTRUCT(BlueprintType)
struct FFableAdventureProgress
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    int32 DefeatedEnemies = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 RequiredEnemies = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 Round = 1;

    UPROPERTY(BlueprintReadOnly)
    bool bObjectiveComplete = false;

    UPROPERTY(BlueprintReadOnly)
    EFableAdventureStage AdventureStage = EFableAdventureStage::Village;

    UPROPERTY(BlueprintReadOnly)
    int32 DungeonWave = 0;

    UPROPERTY(BlueprintReadOnly)
    EFableRegion ActiveRegion = EFableRegion::Village;

    UPROPERTY(BlueprintReadOnly)
    EFableDifficulty ActiveDifficulty = EFableDifficulty::Normal;

    UPROPERTY(BlueprintReadOnly)
    int32 MoonSealMask = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 ActiveSealIndex = -1;

    UPROPERTY(BlueprintReadOnly)
    float BossHealth = 0;

    UPROPERTY(BlueprintReadOnly)
    float BossMaxHealth = 0;

    UPROPERTY(BlueprintReadOnly)
    bool bPartyRecoveryPending = false;

    UPROPERTY(BlueprintReadOnly)
    float PartyRecoveryEndServerTime = 0;

    UPROPERTY(BlueprintReadOnly)
    bool bRetryAvailable = false;

    UPROPERTY(BlueprintReadOnly)
    EFableRegion RetryRegion = EFableRegion::Village;

    UPROPERTY(BlueprintReadOnly)
    EFableDifficulty RetryDifficulty = EFableDifficulty::Normal;

    UPROPERTY(BlueprintReadOnly)
    int32 PartyRecoveryCount = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 EncounterSerial = 0;
};
