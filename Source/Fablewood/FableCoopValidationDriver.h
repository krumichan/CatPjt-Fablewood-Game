#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "FableRPGTypes.h"
#include "FableCoopValidationDriver.generated.h"
class AFableCharacter;
class AFableEnemy;
class AFablePlayerController;
class AFablePlayerState;
class FJsonObject;
/** Opt-in validation drives input and observes real enemy damage; no gameplay state injection. */
UCLASS()
class FABLEWOOD_API UFableCoopValidationDriver : public UActorComponent
{
    GENERATED_BODY()
public:
    UFableCoopValidationDriver();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function) override;
private:
    TWeakObjectPtr<AFablePlayerController> PC;
    FString Role,RunId,Directory,Scenario,Phase;
    int32 Step=0,Substep=0,BeforeCancel=0,BeforeCasts=0,BeforePotion=0,InitialSerial=0,InitialRecovery=0,AttackInputs=0;
    double StartedAt=0,StepAt=0,LastAction=-10,LastReport=-10,SubAt=0,Movement=0,HealthBeforeRevive=0;
    double CompletionQuietSince=-1,CompletionRevivePressedAt=-1,CompletionReviveRetryAt=0;
    bool bDone=false,bSolo=false,bAttemptedRange=false,bCompletionReviveHeld=false;
    bool bTerminalReportPending=false;
    FString TerminalReportText,TerminalReportStatus,TerminalReportReason;
    double TerminalReportStartedAt=0,NextTerminalReportAttempt=0;
    int32 TerminalReportAttempts=0;
    uint32 TerminalReportLastError=0;
    FVector PreviousLocation=FVector::ZeroVector,DownedOrigin=FVector::ZeroVector,RescueApproachPoint=FVector::ZeroVector;
    FString BeforeWipeSignature,BeforeReviveSignature;
    TMap<FString,bool> Checks;
    TArray<FKey> ReleaseKeys;
    TArray<FString> Screenshots;
    void Key(const FKey& Value);
    void Hold(const FKey& Value,bool bHeld);
    void Release();
    void Stop();
    bool WalkTo(const FVector& Location,float Radius=100);
    void Advance(int32 Value);
    void Capture(const FString& Name);
    void Finish(bool bSuccess,const FString& Reason);
    void Report(const FString& Status,const FString& Reason);
    FString SerializeReport(const FString& Status,const FString& Reason);
    void PublishTerminalReport();
    FString Signature(const AFablePlayerState* Profile,bool bIgnoreWear=false) const;
    TSharedPtr<FJsonObject> PeerReport() const;
    int32 PeerStep() const;
    AFableCharacter* Partner() const;
    AFableEnemy* NearestEnemy(bool bBossOnly=false,bool bPreferMelee=false) const;
    int32 EnemyCount() const;
    bool EnemiesAreFresh() const;
    bool PartyEligible(EFableRegion Region) const;
    void Fight();
    void TakeHits();
    void SelectQuest(EFableQuestId Quest);
    void EnterRegion(EFableRegion Region,bool bRetry);
    void TickRescue();
    void TickWipe();
    void TickTimeout();
    void TickSetup();
    void TickCompletion();
};