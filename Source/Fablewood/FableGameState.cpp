#include "FableGameState.h"
#include "Net/UnrealNetwork.h"

/** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
void AFableGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    // 복제 속성과 소유자별 수신 조건 처리
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

/** 파티 복구까지 남은 시간 값을 반환한다. */
float AFableGameState::GetPartyRecoveryTimeRemaining() const
{
    return bPartyRecoveryPending
               ? FMath::Max(0.0f, PartyRecoveryEndServerTime - static_cast<float>(GetServerWorldTimeSeconds()))
               : 0.0f;
}
