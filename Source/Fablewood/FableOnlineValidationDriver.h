#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FableOnlineValidationDriver.generated.h"
class FJsonObject;
class FJsonValue;
class AFablePlayerState;
/** Opt-in UI workflow observer. Uses real account APIs and normal gameplay inputs. */
UCLASS()
class FABLEWOOD_API UFableOnlineValidationDriver : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
private:
    bool Tick(float Delta);
    void Finish(bool Success,const FString& Reason);
    void Capture(const FString& Name);
    void Publish(const FString& Status,const FString& Reason=TEXT(""));
    FString SerializeReport(const FString& Status,const FString& Reason) const;
    bool WriteReport(const FString& Bytes) const;
    void PublishTerminal();
    void ReleaseSkillInput();
    TSharedRef<FJsonObject> Profile(const AFablePlayerState* P) const;
    FString Role,Directory,Mode,AccountName,Password;
    int32 InitialClass=0,Step=0,BeforeManaPotions=0;
    bool bOfflineFailure=false,bCrashRole=false,bDone=false,bSkillSent=false,bReleaseSkill=false;
    bool bTerminalPending=false,bTerminalSuccess=false,bPublicationFailed=false;
    FString TerminalBytes,TerminalReason,MenuProfileHash;
    int64 MenuRevision=0;
    int32 PublicationAttempts=0;
    uint32 PublicationLastError=0;
    double PublicationStarted=0,NextPublication=0;
    double Started=0,StepAt=0,LastReport=0,LastAction=-1,FinishedAt=0;
    FVector MovementStart=FVector::ZeroVector;
    float Movement=0;
    TMap<FString,bool> Checks;
    TArray<FString> Screenshots;
    TArray<FString> ServerObservedPlayerIds;
    TSharedPtr<FJsonObject> Checkpoint;
    TSharedPtr<FJsonObject> Expected;
    FTSTicker::FDelegateHandle Ticker;
};