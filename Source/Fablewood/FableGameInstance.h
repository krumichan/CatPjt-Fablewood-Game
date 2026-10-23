#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SaveGame.h"
#include "FableRPGTypes.h"
#include "FableGameInstance.generated.h"

/** A pointer to the latest profile, separate from every character save. */
UCLASS()
class FABLEWOOD_API UFableProfileIndexSave : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(SaveGame) int32 Version = 1;
    UPROPERTY(SaveGame) FString LatestProfilePrefix;
};

/** Local start-screen actions. The host remains authoritative for character saves. */
UCLASS()
class FABLEWOOD_API UFableGameInstance : public UGameInstance
{
    GENERATED_BODY()
public:
    virtual void Init() override;
    virtual void Shutdown() override;

    UFUNCTION(BlueprintCallable) bool StartNewGame(EFableClass HeroClass, bool bHost = true);
    UFUNCTION(BlueprintCallable) bool ContinueGame(bool bHost = true);
    UFUNCTION(BlueprintCallable) bool JoinGame(const FString& Address);
    UFUNCTION(BlueprintCallable) void ReturnToFrontEnd();
    UFUNCTION(BlueprintPure) bool HasContinueGame() const;
    UFUNCTION(BlueprintPure) FString GetContinueSummary() const;
    UFUNCTION(BlueprintPure) FString GetFrontEndError() const { return FrontEndError; }
    UFUNCTION(BlueprintPure) bool IsFrontEnd() const;
    UFUNCTION(BlueprintPure) bool IsTravelInProgress() const { return bTravelInProgress; }
    const FString& GetActiveProfilePrefix() const { return ActiveProfilePrefix; }

    /** No URL options, paths, shell characters or IPv6 forms are accepted. */
    static bool ValidateJoinAddress(const FString& Address, FString& NormalizedAddress);
    static bool IsValidProfilePrefix(const FString& Prefix);

private:
    void RefreshContinueState();
    bool OpenAdventure(const FString& Prefix, EFableClass HeroClass, bool bHost);
    bool CommitPendingProfile();
    bool TickFrontEnd(float DeltaSeconds);
    void OnWorldLoaded(UWorld* LoadedWorld);
    void HandleNetworkFailure(UWorld* FailedWorld, UNetDriver* Driver,
        ENetworkFailure::Type FailureType, const FString& ErrorString);
    void HandleTravelFailure(UWorld* FailedWorld, ETravelFailure::Type FailureType, const FString& ErrorString);
    void QueueFrontEndReturn(const FString& ErrorMessage);
    bool CanStartTravel();

    FString ProfileIndexSlot = TEXT("Fablewood_ProfileIndex_v1");
    FString ValidationRun;
    FString ActiveProfilePrefix;
    FString ContinueProfilePrefix;
    FString ContinueSummary;
    FString PendingProfilePrefix;
    FString FrontEndError;
    float PendingProfileSeconds = 0.0f;
    bool bInvalidValidationRun = false;
    bool bTravelInProgress = false;
    bool bReturnQueued = false;
    bool bRecoveringToFrontEnd = false;
    FTSTicker::FDelegateHandle FrontEndTicker;
    FDelegateHandle NetworkFailureHandle;
    FDelegateHandle TravelFailureHandle;
    FDelegateHandle WorldLoadedHandle;
};
