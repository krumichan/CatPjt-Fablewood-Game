#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "FableRPGTypes.h"
#include "FableGameMode.generated.h"

class AFableEnemy;
class AFableDungeonWorld;
class AFableMoonwellWorld;

UCLASS()
class FABLEWOOD_API AFableGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AFableGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    bool IsFrontEnd() const { return bFrontEnd; }
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
        const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    virtual void PostLogin(APlayerController* NewPlayer) override;
    virtual void Logout(AController* Exiting) override;
    virtual void RestartPlayer(AController* NewPlayer) override;

    void CompleteBackendLogin(APlayerController* Player, bool bSuccess, const FString& Reason);
    void HandleEnemyDefeated(AFableEnemy* Enemy);
    void UpdateBossHealth(AFableEnemy* Enemy);
    void RequestNewRound(APlayerController* Requester);
    void RequestRetryEncounter(APlayerController* Requester);
    void RequestForestEncounter(APlayerController* Requester);
    void RequestEnterDungeon(APlayerController* Requester);
    void RequestEnterRegion(APlayerController* Requester, EFableRegion Region,
        EFableDifficulty Difficulty = EFableDifficulty::Normal);
    void RequestCleanseSeal(APlayerController* Requester, int32 SealIndex);
    void RequestReturnVillage(APlayerController* Requester);
    bool CanUseTownService(APlayerController* Requester, FVector Location, float Radius = 300.0f) const;

private:
    int32 GetConnectionSlot(AController* Player) const;
    void RejectBackendLogin(APlayerController* Player, const FString& Reason);
    void CheckBackendLogins();
    void SpawnEncounter();
    void ClearEncounter();
    void SpawnDungeonWave(int32 Wave);
    void SpawnBoss();
    void SpawnMoonSealGroup(int32 SealIndex);
    void CheckPartyState();
    void BeginPartyRecovery();
    void FinishPartyRecovery();
    void ClearRecoveryCombat();
    bool HasRetryQuest(EFableRegion Region) const;
    void ReturnPartyToVillage(bool bWipe);
    void NotifyParty(const FString& Notice) const;
    bool AllPlayersRegionEligible(EFableRegion Region) const;
    bool IsDungeonRunning() const;
    bool IsRequesterAlive(APlayerController* Requester) const;
    bool MoveParty(EFableRegion Region);
    bool MovePlayerToCheckpoint(AController* Player, EFableRegion Region, int32 Slot);

    TSet<TWeakObjectPtr<AFableEnemy>> ActiveEnemies;
    TWeakObjectPtr<AFableEnemy> ActiveBoss;
    UPROPERTY() TObjectPtr<AFableDungeonWorld> DungeonWorld;
    UPROPERTY() TObjectPtr<AFableMoonwellWorld> MoonwellWorld;
    FTimerHandle EncounterTimer;
    FTimerHandle PartyMonitor;
    double LastRestartTime = -100.0;
    bool bDedicatedMode = false;
    TMap<TWeakObjectPtr<APlayerController>, int32> ConnectionSlots;
    TMap<TWeakObjectPtr<APlayerController>, FString> PendingTickets;
    TMap<TWeakObjectPtr<APlayerController>, double> PendingLoginTimes;
    FTimerHandle BackendLoginMonitor;
    bool bLegacyMode = false;
    bool bFrontEnd = false;
    bool bReturnPending = false;
    bool bReturnAfterWipe = false;
    bool bRetryAfterRecovery = false;
    double NextRecoveryAttemptTime = 0.0;
    double NextIndividualRescueTime = 0.0;
};
