#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "FableRPGTypes.h"
#include "FableOnlineFlowSubsystem.generated.h"

class FJsonObject;
class SFableOnlineWidget;
class UFont;
class APlayerController;

UENUM()
enum class EFableOnlineState : uint8
{
    SignedOut, Authenticating, Refreshing, Character, Joining, Playing, Leaving, Disconnected, Error
};

struct FFableOnlineServer
{
    FString Id, Name, Address;
};

struct FFableOnlineCharacter
{
    FString Name, PlayerId, ProfileHash;
    int64 Revision = 0;
    EFableClass HeroClass = EFableClass::Warrior;
    int32 Level = 0, Experience = 0, ClassTier = 0;
    int32 Gold = 0, BagCount = 0, StashCount = 0, InventoryVersion = 0;
    bool bHasSummary = false, bNewCharacter = false;
    FString EquippedWeapon, EquippedArmor;
};

/** Owns only the local account session and presentation. Gameplay authority stays on the server. */
UCLASS()
class FABLEWOOD_API UFableOnlineFlowSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    bool Authenticate(const FString& Name, const FString& Password);
    bool Register(const FString& Name, const FString& Password, EFableClass InitialClass);
    bool RefreshCharacter();
    bool SelectServer(const FString& ServerId);
    bool JoinSelectedServer();
    bool RequestLeave();
    bool Logout();
    bool Retry();
    void OnOnlineLeaveResult(bool bSuccess, const FString& Reason);
    void HandleConnectionFailure(bool bTravelFailure);
    void OnWorldLoaded(UWorld* World);
    void ToggleInGameMenu();
    void UseLocalMode();

    EFableOnlineState GetState() const { return State; }
    FString GetStateName() const;
    const FString& GetMessage() const { return Message; }
    const FFableOnlineCharacter& GetCharacter() const { return Character; }
    const TArray<FFableOnlineServer>& GetServers() const { return Servers; }
    const FString& GetSelectedServerId() const { return SelectedServerId; }
    const FString& GetSessionPlayerId() const { return Character.PlayerId; }
    const FString& GetBackendUrl() const { return BackendUrl; }
    int32 GetLastHttpStatus() const { return LastHttpStatus; }
    int32 GetJoinAttemptCount() const { return JoinAttemptCount; }
    uint32 GetViewRevision() const { return ViewRevision; }
    bool HasSession() const { return !AccessToken.IsEmpty(); }
    bool IsEnabled() const { return bEnabled; }
    bool IsBusy() const;
    bool IsOverlayBlocking() const;
    bool IsManagingConnection() const { return bManagedConnection || bAwaitingFrontEnd; }
    UFont* GetUIFont() const { return UIFont; }

private:
    using FReply = TFunction<void(int32, const TSharedPtr<FJsonObject>&)>;
    bool TickFlow(float DeltaSeconds);
    void SetState(EFableOnlineState Value, const FString& Text);
    void SendJson(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, bool bBearer, FReply Callback);
    void CancelRequests();
    void ClearSession();
    void HandleHttpError(int32 Code, const FString& Context);
    void FetchServers();
    bool ReadCharacter(const TSharedPtr<FJsonObject>& Json);
    void UpdateOverlay();
    bool CanAuthenticate(const FString& Name, const FString& Password);

    // Session secrets never enter URLs, command lines, local saves or logging.
    FString AccessToken, PendingRegistrationPassword;
    FString AccountName, BackendUrl = TEXT("http://127.0.0.1:5292");
    FString Message, SelectedServerId;
    FFableOnlineCharacter Character;
    TArray<FFableOnlineServer> Servers;
    EFableOnlineState State = EFableOnlineState::SignedOut;
    bool bEnabled = false, bConfigurationValid = true, bGameMenuOpen = false, bManagedConnection = false;
    bool bAwaitingFrontEnd = false, bRefreshAfterReturn = false, bRetryJoin = false;
    double JoinStartedAt = 0, LeaveStartedAt = 0;
    int32 LastHttpStatus = 0, JoinAttemptCount = 0;
    uint32 RequestSerial = 0, ViewRevision = 0;
    TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;
    TSharedPtr<SFableOnlineWidget> Widget;
    TWeakObjectPtr<APlayerController> InputController;
    bool bLastInputBlocking = false;
    FTSTicker::FDelegateHandle Ticker;
    UPROPERTY(Transient) TObjectPtr<UFont> UIFont;
};
