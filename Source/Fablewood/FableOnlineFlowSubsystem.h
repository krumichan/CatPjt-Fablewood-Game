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

/** 로그인부터 접속·안전 이탈까지의 온라인 화면 상태 값의 종류를 정의한다. */
UENUM()
enum class EFableOnlineState : uint8
{
    SignedOut,
    Authenticating,
    Refreshing,
    Character,
    Joining,
    Playing,
    Leaving,
    Disconnected,
    Error
};

/** 서버 선택 화면에 표시할 접속 대상 정보. */
struct FFableOnlineServer
{
    FString Id, Name, Address;
};

/** 서버에서 확인한 캐릭터의 표시용 요약. */
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
/** 계정·세션·서버 선택·온라인 진입과 안전 이탈을 관리하는 하위 시스템. */
UCLASS()
class FABLEWOOD_API UFableOnlineFlowSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** 온라인 연결 하위 시스템의 초기 상태를 준비한다. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** 온라인 연결 하위 시스템의 요청과 콜백을 정리한다. */
    virtual void Deinitialize() override;

    /** 입력한 계정 정보로 백엔드에 로그인 인증을 요청하고 세션 응답을 처리한다. */
    bool Authenticate(const FString& Name, const FString& Password);

    /** 입력한 계정 정보로 회원 가입을 요청한다. */
    bool Register(const FString& Name, const FString& Password, EFableClass InitialClass);

    /** 인증된 플레이어의 저장된 캐릭터 요약을 다시 조회한다. */
    bool RefreshCharacter();

    /** 목록에서 접속할 서버를 선택한다. */
    bool SelectServer(const FString& ServerId);

    /** 선택한 서버의 접속 티켓을 발급받아 게임 접속을 시작한다. */
    bool JoinSelectedServer();

    /** 미완료 저장을 보존하는 온라인 이탈 절차를 시작한다. */
    bool RequestLeave();

    /** 접속 종료에 따른 세션과 소유 상태의 정리를 처리한다. */
    bool Logout();

    /** 현재 오류 상태에서 허용된 온라인 요청을 다시 시작한다. */
    bool Retry();

    /** 게임 서버의 안전 이탈 결과를 온라인 화면과 연결 상태에 반영한다. */
    void OnOnlineLeaveResult(bool bSuccess, const FString& Reason);

    /** 접속 실패 이벤트를 처리한다. */
    void HandleConnectionFailure(bool bTravelFailure);

    /** 월드 로드가 끝나면 진행 중인 전환과 프로필 문맥을 갱신한다. */
    void OnWorldLoaded(UWorld* World);

    /** 플레이 중인 온라인 메뉴의 표시 상태를 전환한다. */
    void ToggleInGameMenu();

    /** 온라인 연결 상태를 정리하고 로컬 게임 모드로 전환한다. */
    void UseLocalMode();

    /** 현재 온라인 상태 값을 반환한다. */
    EFableOnlineState GetState() const
    {
        return State;
    }

    /** 온라인 상태의 표시 이름 값을 반환한다. */
    FString GetStateName() const;

    /** 온라인 화면의 현재 상태·오류 안내 문구를 반환한다. */
    const FString& GetMessage() const
    {
        return Message;
    }

    /** 온라인 서버에서 확인한 캐릭터 표시 정보를 반환한다. */
    const FFableOnlineCharacter& GetCharacter() const
    {
        return Character;
    }

    /** 접속 가능한 온라인 서버 목록을 반환한다. */
    const TArray<FFableOnlineServer>& GetServers() const
    {
        return Servers;
    }

    /** 현재 선택한 온라인 서버 식별자를 반환한다. */
    const FString& GetSelectedServerId() const
    {
        return SelectedServerId;
    }

    /** 로그인 세션에서 확인한 플레이어 식별자를 반환한다. */
    const FString& GetSessionPlayerId() const
    {
        return Character.PlayerId;
    }

    /** 현재 온라인 요청에 사용하는 백엔드 주소를 반환한다. */
    const FString& GetBackendUrl() const
    {
        return BackendUrl;
    }

    /** 마지막 HTTP 응답 코드 값을 반환한다. */
    int32 GetLastHttpStatus() const
    {
        return LastHttpStatus;
    }

    /** 서버 접속 시도 횟수 값을 반환한다. */
    int32 GetJoinAttemptCount() const
    {
        return JoinAttemptCount;
    }

    /** 온라인 화면의 상태 변경 번호 값을 반환한다. */
    uint32 GetViewRevision() const
    {
        return ViewRevision;
    }

    /** 로그인 세션의 존재 여부를 확인한다. */
    bool HasSession() const
    {
        return !AccessToken.IsEmpty();
    }

    /** 사용 가능 여부를 확인한다. */
    bool IsEnabled() const
    {
        return bEnabled;
    }

    /** 처리 중 여부를 확인한다. */
    bool IsBusy() const;

    /** 오버레이의 입력 차단 여부를 확인한다. */
    bool IsOverlayBlocking() const;

    /** 연결 관리 여부를 확인한다. */
    bool IsManagingConnection() const
    {
        return bManagedConnection || bAwaitingFrontEnd;
    }

    /** 한국어 표시용 UI 글꼴 값을 반환한다. */
    UFont* GetUIFont() const
    {
        return UIFont;
    }

private:
    using FReply = TFunction<void(int32, const TSharedPtr<FJsonObject>&)>;

    /** 현재 온라인 접속·게임 진입·저장·이탈 검증 단계를 진행한다. */
    bool TickFlow(float DeltaSeconds);

    /** 현재 온라인 상태 값을 설정한다. */
    void SetState(EFableOnlineState Value, const FString& Text);

    /** 온라인 API 요청의 JSON과 인증 상태를 구성해 비동기로 전송한다. */
    void SendJson(const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body, bool bBearer,
                  FReply Callback);

    /** 진행 중인 온라인 HTTP 요청의 콜백과 참조를 해제한다. */
    void CancelRequests();

    /** 로그인 토큰과 온라인 표시 상태를 정리한다. */
    void ClearSession();

    /** HTTP 오류 이벤트를 처리한다. */
    void HandleHttpError(int32 Code, const FString& Context);

    /** 인증된 세션으로 접속 가능한 서버 목록을 조회한다. */
    void FetchServers();

    /** 온라인 응답의 캐릭터 요약 필드를 검사해 화면 상태로 읽는다. */
    bool ReadCharacter(const TSharedPtr<FJsonObject>& Json);

    /** 온라인 진행·오류 상태에 맞게 오버레이 표시를 갱신한다. */
    void UpdateOverlay();

    /** 현재 온라인 상태에서 계정 인증 요청을 시작할 수 있는지 확인한다. */
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
    UPROPERTY(Transient)
    TObjectPtr<UFont> UIFont;
};
