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
/** 의 일반 입력과 관찰 결과로 자동 검증을 진행하는 컴포넌트. */
UCLASS()
class FABLEWOOD_API UFableValidationDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    UFableValidationDriver();

    /** 검증 요청 여부를 확인한다. */
    static bool IsRequested();

    /** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 월드 이탈 시 등록한 이벤트와 진행 중인 작업을 정리한다. */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** 서버가 관찰한 재사용 대기시간 검증 결과. */
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

    /** 이동·전투 검증 중 수집한 단계별 관찰 값. */
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

    /** 유지 입력 키 값을 설정한다. */
    void SetHeldKey(const FKey& Key, bool bDown);

    /** 단발 입력 키에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void PulseKey(const FKey& Key);

    /** 해제 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void ReleaseInputs();

    /** 조준 위치에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void AimAt(const FVector& Location, float DeltaTime);

    /** 입력 진행 전투에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void DriveCombat(AFableCharacter* Pawn, int32 Round, float DeltaTime);

    /** 이동 변화를 관찰해 검증에 기록한다. */
    void ObserveMovement(AFableCharacter* Pawn);

    /** 재사용 대기시간 변화를 관찰해 검증에 기록한다. */
    void ObserveCooldown(AFableCharacter* Pawn, FCooldownObservation& Observation, double ServerNow);

    /** 서버가 관찰한 공격·스킬·회피의 대기시간 검증이 모두 통과했는지 확인한다. */
    bool ServerCooldownsPassed() const;

    /** 복구 확인 지점 결과를 검증 이력에 기록한다. */
    void RecordCheckpoint(FCheckpoint& Checkpoint, const AFableGameState* State);

    /** 대기열 화면 캡처에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void QueueScreenshot(const FString& Name);

    /** 처리 화면 캡처 목록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void ProcessScreenshots(double Now);

    /** 화면 캡처의 존재 여부를 확인한다. */
    bool HasScreenshot(const FString& Name) const;

    /** 상대 클라이언트 보고서 값을 읽는다. */
    TSharedPtr<FJsonObject> ReadPeerReport() const;

    /** 상대 클라이언트 확인 값에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool PeerFlag(const TSharedPtr<FJsonObject>& Peer, const FString& Name) const;

    /** 검증 보고서 값을 기록한다. */
    void WriteReport();

    /** 실패 사유를 기록하고 현재 검증을 종료한다. */
    void Fail(const FString& Reason);

    /** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
    void Finish(bool bPassed);
};
