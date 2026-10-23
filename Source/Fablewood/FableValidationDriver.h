#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "FableValidationDriver.generated.h"

class AFableCharacter;
class AFableEnemy;
class AFableGameState;
class APlayerController;
class FJsonObject;

/** Opt-in, non-shipping gameplay validation. Only supplies ordinary local input and reads game state. */
UCLASS()
class FABLEWOOD_API UFableValidationDriver : public UActorComponent
{
    GENERATED_BODY()
public:
    UFableValidationDriver();
    static bool IsRequested();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    struct FCooldownObservation
    {
        TWeakObjectPtr<AFableCharacter> Pawn;
        double AttackEnd = 0.0;
        double SkillEnd = 0.0;
        double DodgeEnd = 0.0;
        double MinimumAttackGap = 9999.0;
        int32 AttackCycles = 0;
        int32 SkillCycles = 0;
        int32 DodgeCycles = 0;
    };
    struct FCheckpoint
    {
        int32 Round = 0;
        int32 Defeated = 0;
        int32 Required = 0;
        bool bComplete = false;
        double Elapsed = 0.0;
    };

    TWeakObjectPtr<APlayerController> Player;
    TWeakObjectPtr<AFableCharacter> LastLocalPawn;
    TWeakObjectPtr<AFableCharacter> FirstRoundPawn;
    TWeakObjectPtr<AFableCharacter> LastRemotePawn;
    TWeakObjectPtr<AFableEnemy> Target;
    FVector LastLocalLocation = FVector::ZeroVector;
    FVector LastRemoteLocation = FVector::ZeroVector;
    TMap<int32, FCooldownObservation> ServerCooldowns;
    FCooldownObservation LocalCooldown;
    TArray<FKey> PulseKeysToRelease;
    TArray<FString> ScreenshotQueue;
    TArray<FString> SavedScreenshots;
    FString PendingScreenshot;
    FString Role;
    FString Directory;
    FString RunId;
    FString Phase = TEXT("loading");
    FString Failure;
    FString Status = TEXT("running");
    double StartedAt = 0.0;
    double ScenarioStartedAt = 0.0;
    double LastReportAt = 0.0;
    double NextPulseAt = 0.0;
    double LastSkillAttemptAt = -10.0;
    double LastDodgeAttemptAt = -10.0;
    double NextRestartAt = 0.0;
    double ScreenshotRequestedAt = 0.0;
    double FinishedAt = 0.0;
    double WarmupSeconds = 0.0;
    double AimStalledSeconds = 0.0;
    double LocalMovement = 0.0;
    double RemoteMovement = 0.0;
    double LastAimYaw = 0.0;
    int32 AttackInputs = 0;
    int32 SkillInputs = 0;
    int32 DodgeInputs = 0;
    int32 RestartInputs = 0;
    int32 AimFallbacks = 0;
    int32 MaximumPlayerCount = 0;
    int32 LastDodgeRound = 0;
    int32 LocalPlayerId = -1;
    bool bReady = false;
    bool bQuorumObserved = false;
    bool bWarmupComplete = false;
    bool bRoundOneComplete = false;
    bool bRoundTwoStarted = false;
    bool bRoundTwoComplete = false;
    bool bCombatScreenshotQueued = false;
    bool bQuitRequested = false;
    FCheckpoint RoundOne;
    FCheckpoint RoundTwoReset;
    FCheckpoint RoundTwo;

    void SetHeldKey(const FKey& Key, bool bDown);
    void PulseKey(const FKey& Key);
    void ReleaseInputs();
    void AimAt(const FVector& Location, float DeltaTime);
    void DriveCombat(AFableCharacter* Pawn, int32 Round, float DeltaTime);
    void ObserveMovement(AFableCharacter* Pawn);
    void ObserveCooldown(AFableCharacter* Pawn, FCooldownObservation& Observation, double ServerNow);
    bool ServerCooldownsPassed() const;
    void RecordCheckpoint(FCheckpoint& Checkpoint, const AFableGameState* State);
    void QueueScreenshot(const FString& Name);
    void ProcessScreenshots(double Now);
    bool HasScreenshot(const FString& Name) const;
    TSharedPtr<FJsonObject> ReadPeerReport() const;
    bool PeerFlag(const TSharedPtr<FJsonObject>& Peer, const FString& Name) const;
    void WriteReport();
    void Fail(const FString& Reason);
    void Finish(bool bPassed);
};
