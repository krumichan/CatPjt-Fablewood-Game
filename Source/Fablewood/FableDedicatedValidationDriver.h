#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "FableDedicatedValidationDriver.generated.h"
class AFablePlayerController;
class AFablePlayerState;
class AFableEnemy;
class FJsonObject;

/** Opt-in observation/input harness. It never changes health, rewards or profiles directly. */
UCLASS()
class FABLEWOOD_API UFableDedicatedValidationDriver : public UActorComponent
{
    GENERATED_BODY()
public:
    UFableDedicatedValidationDriver();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
private:
    void TickServer();
    void TickClient();
    void Key(const FKey& Value);
    void Hold(const FKey& Value, bool bHeld);
    bool WalkTo(const FVector& Position, float Radius = 90.0f);
    void Stop();
    void Advance(int32 Value);
    void Fight();
    void Capture(const FString& Name);
    void Finish(bool bSuccess, const FString& Reason);
    void PublishPending();
    void WriteRunning();
    TSharedRef<FJsonObject> MakeReport(const FString& Status, const FString& Reason) const;
    TSharedRef<FJsonObject> ProfileReport(const AFablePlayerState* Profile) const;
    TSharedPtr<FJsonObject> ReadRole(const FString& OtherRole) const;
    FString ProfileSignature(const AFablePlayerState* Profile) const;
    bool PeerAtLeast(int32 Wanted);
    int32 LiveEnemies() const;
    int32 Projectiles() const;
    TWeakObjectPtr<AFablePlayerController> PC;
    FString Role, RunId, Directory, Phase;
    bool bServer = false, bDone = false, bPending = false, bTerminalSuccess = false;
    FString TerminalBytes, TerminalReason;
    double StartedAt = 0, StepAt = 0, LastAction = -10, LastSkill = -10, LastReportAt = -10;
    double QuietAt = -1, PendingAt = 0, NextPublishAt = 0, LastConnectionReportAt = -10;
    bool bQuitRequested = false;
    int32 Step = 0, PeerStepSeen = -1, PublicationAttempts = 0, AttackInputs = 0, SkillInputs = 0;
    int32 BeforeGold = 0, BeforePotions = 0, BeforeItems = 0, BeforeNotice = 0, NewItemId = 0;
    float BeforeHealth = 0;
    FString BeforeSignature;
    FVector InitialPosition = FVector::ZeroVector;
    float MaxMovement = 0, RemoteMovement = 0;
    TMap<TWeakObjectPtr<AActor>, FVector> RemoteStarts;
    TMap<TWeakObjectPtr<AFableEnemy>, float> EnemyHealth;
    TMap<FString,FVector> PlayerStarts;
    TMap<FString,float> PlayerMovement;
    TMap<FString,float> PlayerLastHealth;
    TMap<FString,int32> PlayerLastPotions;
    TMap<FString,bool> PlayerHitSeen, PlayerPotionSeen;
    bool bEnemyDamageSeen = false;
    TMap<FString,bool> Checks;
    TArray<FKey> Releases;
    TArray<FString> Screenshots;
};
