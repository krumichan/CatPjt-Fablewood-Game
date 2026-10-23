#include "FableGameState.h"
#include "Net/UnrealNetwork.h"

void AFableGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableGameState, bFrontEnd);
    DOREPLIFETIME(AFableGameState, DefeatedEnemies);
    DOREPLIFETIME(AFableGameState, RequiredEnemies);
    DOREPLIFETIME(AFableGameState, Round);
    DOREPLIFETIME(AFableGameState, bObjectiveComplete);
    DOREPLIFETIME(AFableGameState, AdventureStage);
    DOREPLIFETIME(AFableGameState, DungeonWave);
    DOREPLIFETIME(AFableGameState, ActiveRegion);
    DOREPLIFETIME(AFableGameState, ActiveDifficulty);
    DOREPLIFETIME(AFableGameState, MoonSealMask);
    DOREPLIFETIME(AFableGameState, ActiveSealIndex);
    DOREPLIFETIME(AFableGameState, BossHealth);
    DOREPLIFETIME(AFableGameState, BossMaxHealth);
    DOREPLIFETIME(AFableGameState, bPartyRecoveryPending);
    DOREPLIFETIME(AFableGameState, PartyRecoveryEndServerTime);
    DOREPLIFETIME(AFableGameState, bRetryAvailable);
    DOREPLIFETIME(AFableGameState, RetryRegion);
    DOREPLIFETIME(AFableGameState, RetryDifficulty);
    DOREPLIFETIME(AFableGameState, PartyRecoveryCount);
    DOREPLIFETIME(AFableGameState, EncounterSerial);
}

float AFableGameState::GetPartyRecoveryTimeRemaining() const
{
    return bPartyRecoveryPending ? FMath::Max(0.0f, PartyRecoveryEndServerTime - static_cast<float>(GetServerWorldTimeSeconds())) : 0.0f;
}
