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
/** 협동 플레이의 일반 입력과 관찰 결과로 자동 검증을 진행하는 컴포넌트. */
UCLASS()
class FABLEWOOD_API UFableCoopValidationDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    UFableCoopValidationDriver();

    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;

private:
    TWeakObjectPtr<AFablePlayerController> PC;
    FString Role, RunId, Directory, Scenario, Phase;
    int32 Step = 0, Substep = 0, BeforeCancel = 0, BeforeCasts = 0, BeforePotion = 0, InitialSerial = 0,
          InitialRecovery = 0, AttackInputs = 0;
    double StartedAt = 0, StepAt = 0, LastAction = -10, LastReport = -10, SubAt = 0, Movement = 0,
           HealthBeforeRevive = 0;
    double CompletionQuietSince = -1, CompletionRevivePressedAt = -1, CompletionReviveRetryAt = 0;
    bool bDone = false, bSolo = false, bAttemptedRange = false, bCompletionReviveHeld = false;
    bool bTerminalReportPending = false;
    FString TerminalReportText, TerminalReportStatus, TerminalReportReason;
    double TerminalReportStartedAt = 0, NextTerminalReportAttempt = 0;
    int32 TerminalReportAttempts = 0;
    uint32 TerminalReportLastError = 0;
    FVector PreviousLocation = FVector::ZeroVector, DownedOrigin = FVector::ZeroVector,
            RescueApproachPoint = FVector::ZeroVector;
    FString BeforeWipeSignature, BeforeReviveSignature;
    TMap<FString, bool> Checks;
    TArray<FKey> ReleaseKeys;
    TArray<FString> Screenshots;

    /** 검증 캐릭터에 단발 키 입력을 전달한다. */
    void Key(const FKey& Value);

    /** 검증에 필요한 키의 누름 상태를 유지한다. */
    void Hold(const FKey& Value, bool bHeld);

    /** 검증에서 누른 키의 유지 상태를 해제한다. */
    void Release();

    /** 검증 캐릭터의 이동 및 유지 입력을 해제한다. */
    void Stop();

    /** 일반 이동 입력으로 검증 캐릭터를 목표 지점에 접근시킨다. */
    bool WalkTo(const FVector& Location, float Radius = 100);

    /** 검증 상태를 다음 단계로 옮기고 단계 시작 시각을 갱신한다. */
    void Advance(int32 Value);

    /** 현재 검증 단계의 화면을 캡처한다. */
    void Capture(const FString& Name);

    /** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
    void Finish(bool bSuccess, const FString& Reason);

    /** 현재 검증 상태와 수집한 근거를 보고서로 기록한다. */
    void Report(const FString& Status, const FString& Reason);

    /** 현재 관찰 값과 검증 결과를 JSON 보고서로 직렬화한다. */
    FString SerializeReport(const FString& Status, const FString& Reason);

    /** 종료 보고서 결과를 보고서에 기록한다. */
    void PublishTerminalReport();

    /** 검증 대상 상태의 변경 여부를 비교할 서명 문자열을 만든다. */
    FString Signature(const AFablePlayerState* Profile, bool bIgnoreWear = false) const;

    /** 상대 클라이언트 보고서에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    TSharedPtr<FJsonObject> PeerReport() const;

    /** 상대 클라이언트 단계 번호에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    int32 PeerStep() const;

    /** 같은 파티의 다른 플레이어 캐릭터를 찾는다. */
    AFableCharacter* Partner() const;

    /** 검증 캐릭터 주변에서 가장 가까운 유효한 적을 찾는다. */
    AFableEnemy* NearestEnemy(bool bBossOnly = false, bool bPreferMelee = false) const;

    /** 적 개수에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    int32 EnemyCount() const;

    /** 현재 검증 전투의 적이 초기 체력과 개체 수를 유지하는지 확인한다. */
    bool EnemiesAreFresh() const;

    /** 파티원이 협동 검증의 입장 조건을 모두 충족하는지 확인한다. */
    bool PartyEligible(EFableRegion Region) const;

    /** 일반 공격·스킬 입력으로 현재 전투 검증을 진행한다. */
    void Fight();

    /** 검증 캐릭터가 적의 일반 공격에 피격되도록 위치와 입력을 조절한다. */
    void TakeHits();

    /** 퀘스트 선택 상태를 갱신한다. */
    void SelectQuest(EFableQuestId Quest);

    /** 지역 입장에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void EnterRegion(EFableRegion Region, bool bRetry);

    /** 동료 구조와 부활 시전의 검증 단계를 진행한다. */
    void TickRescue();

    /** 파티 전멸과 복구 절차의 검증 단계를 진행한다. */
    void TickWipe();

    /** 구조 제한 시간 만료의 검증 단계를 진행한다. */
    void TickTimeout();

    /** 협동 검증에 필요한 직업·퀘스트·전투 상태를 준비한다. */
    void TickSetup();

    /** 던전 완료와 보상 유지의 검증 단계를 진행한다. */
    void TickCompletion();
};
