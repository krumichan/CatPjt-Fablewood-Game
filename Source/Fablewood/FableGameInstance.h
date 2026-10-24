#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "GameFramework/SaveGame.h"
#include "FableRPGTypes.h"
#include "FableGameInstance.generated.h"

/** A pointer to the latest profile, separate from every character save. */
/** 시작 화면에서 이어갈 프로필을 선택하기 위한 저장 인덱스. */
UCLASS()
class FABLEWOOD_API UFableProfileIndexSave : public USaveGame
{
    GENERATED_BODY()

public:
    UPROPERTY(SaveGame)
    int32 Version = 1;
    UPROPERTY(SaveGame)
    FString LatestProfilePrefix;
};

/** Local start-screen actions. The host remains authoritative for character saves. */
/** 저장 프로필 선택과 시작 화면·게임 월드 사이의 전환을 관리하는 인스턴스. */
UCLASS()
class FABLEWOOD_API UFableGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    /** 게임 인스턴스의 저장·화면 전환 상태와 콜백을 초기화한다. */
    virtual void Init() override;

    /** 게임 인스턴스가 등록한 이벤트와 연결 상태를 정리한다. */
    virtual void Shutdown() override;

    /** 새 모험의 프로필과 시작 직업을 준비해 게임에 진입한다. */
    UFUNCTION(BlueprintCallable)
    bool StartNewGame(EFableClass HeroClass, bool bHost = true);

    /** 검증된 저장 프로필로 이전 모험을 이어간다. */
    UFUNCTION(BlueprintCallable)
    bool ContinueGame(bool bHost = true);

    /** 접속 주소를 검사한 뒤 해당 멀티플레이 게임에 접속한다. */
    UFUNCTION(BlueprintCallable)
    bool JoinGame(const FString& Address);

    /** 인증된 온라인 서버의 접속 문맥을 준비하고 월드를 전환한다. */
    bool JoinOnlineServer(const FString& Address, const FString& OneUseTicket);

    /** 게임 이탈과 저장 상태를 정리한 뒤 시작 화면으로 돌아간다. */
    UFUNCTION(BlueprintCallable)
    void ReturnToFrontEnd();

    /** 이어하기 게임의 존재 여부를 확인한다. */
    UFUNCTION(BlueprintPure)
    bool HasContinueGame() const;

    /** 이어하기에 표시할 저장 요약 값을 반환한다. */
    UFUNCTION(BlueprintPure)
    FString GetContinueSummary() const;

    /** 시작 화면의 오류 안내 값을 반환한다. */
    UFUNCTION(BlueprintPure)
    FString GetFrontEndError() const
    {
        return FrontEndError;
    }

    /** 시작 화면 여부를 확인한다. */
    UFUNCTION(BlueprintPure)
    bool IsFrontEnd() const;

    /** 월드 전환 중 여부를 확인한다. */
    UFUNCTION(BlueprintPure)
    bool IsTravelInProgress() const
    {
        return bTravelInProgress;
    }

    /** 현재 모험에 사용하는 저장 프로필 접두사를 반환한다. */
    const FString& GetActiveProfilePrefix() const
    {
        return ActiveProfilePrefix;
    }

    /** No URL options, paths, shell characters or IPv6 forms are accepted. */

    /** 접속 주소 값과 허용 조건을 검사한다. */
    static bool ValidateJoinAddress(const FString& Address, FString& NormalizedAddress);

    /** 저장 이름 접두사 유효성 여부를 확인한다. */
    static bool IsValidProfilePrefix(const FString& Prefix);

private:
    /** 이어하기 상태 상태를 다시 확인해 갱신한다. */
    void RefreshContinueState();

    /** 선택한 프로필과 직업으로 모험 월드를 연다. */
    bool OpenAdventure(const FString& Prefix, EFableClass HeroClass, bool bHost);

    /** 월드 전환 전에 남은 프로필 저장을 처리한다. */
    bool CommitPendingProfile();

    /** 시작 화면의 상태와 필요한 화면 전환을 갱신한다. */
    bool TickFrontEnd(float DeltaSeconds);

    /** 월드 로드가 끝나면 진행 중인 전환과 프로필 문맥을 갱신한다. */
    void OnWorldLoaded(UWorld* LoadedWorld);

    /** 네트워크 실패의 연결 상태와 사용자 안내를 갱신한다. */
    void HandleNetworkFailure(UWorld* FailedWorld, UNetDriver* Driver, ENetworkFailure::Type FailureType,
                              const FString& ErrorString);

    /** 월드 전환 실패를 정리하고 시작 화면 복귀를 준비한다. */
    void HandleTravelFailure(UWorld* FailedWorld, ETravelFailure::Type FailureType, const FString& ErrorString);

    /** 다음 안전한 갱신 시점에 시작 화면으로 복귀하도록 예약한다. */
    void QueueFrontEndReturn(const FString& ErrorMessage);

    /** 중복 전환과 현재 화면 상태를 확인해 월드 전환 허용 여부를 반환한다. */
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
