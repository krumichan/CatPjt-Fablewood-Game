#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FableTownTravelTypes.h"
#include "FableTownTravelCoordinator.generated.h"

class APlayerController;
class AFableTownManager;

/** 기존 TownManager에 붙는 서버 이동 조정자. 별도 월드·시계를 만들지 않는다. */
UCLASS()
class FABLEWOOD_API UFableTownTravelCoordinator : public UActorComponent
{
    GENERATED_BODY()

public:
    UFableTownTravelCoordinator();

    /** 서버 만료 감시를 시작한다. */
    virtual void BeginPlay() override;

    /** 종료 전에 자체 lease와 콜백을 정리한다. */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** 기존 manager의 조정자만 찾는다. */
    static UFableTownTravelCoordinator* Find(const UObject* Context);

    /** 논리 앵커와 현재 공간 revision으로만 이동을 시작한다. */
    void BeginAnchorTravel(APlayerController* Requester, FGuid RequestId, const FFableTownAnchorRef& Target,
                           int64 ExpectedPresenceRevision);

    /** 각 ACK를 정확 tuple과 현재 소유 연결로 검사한다. */
    void AcknowledgeGeometry(APlayerController* Requester, FGuid AttemptId, FGuid TransactionId, int64 Generation,
                             FGuid ConnectionNonce, FGuid ChallengeNonce, bool bLoaded);

    /** 정확한 owner 취소만 정상 pending을 끝낸다. */
    void CancelAnchorTravel(APlayerController* Requester, FGuid AttemptId, FGuid TransactionId, int64 Generation,
                            FGuid ConnectionNonce);

    /** 실제 소유자 도착을 서버 receipt와 재대조한다. */
    void ConfirmOwnArrival(APlayerController* Requester, FGuid AttemptId, FGuid TransactionId, int64 Generation,
                           FGuid ConnectionNonce, int64 ExpectedCommitSequence);

    /** 현재 연결 자신의 pending만 관찰한다. */
    bool GetPendingFor(const APlayerController* Requester, FFableTownTravelPrepared& OutPrepared) const;

    /** 변하지 않는 성공 receipt를 조회한다. */
    bool GetCommittedFor(const APlayerController* Requester, FGuid TransactionId,
                         FFableTownTravelCommitReceipt& OutCommit) const;

    /** 이벤트와 같은 상관 ID의 마지막 응답을 읽는다. */
    bool GetLastResponseFor(const APlayerController* Requester, FFableTownTravelResponse& OutResponse) const;

    /** 실제 물리 질의 증빙을 읽는다. */
    bool GetPlacementEvidence(FGuid TransactionId, FFableTownPlacementEvidence& OutEvidence) const;

    /** 서버 단계 이벤트의 구독 지점을 반환한다. */
    FOnFableTownTravelEvent& OnAuthorityTravelEvent();

    /** 정확 참가자만 예약하고 본인의 pending Town 이동을 선점한다. */
    bool TryReservePrivateAdmission(const TArray<APlayerController*>& ExactMembers, FGuid ExpeditionId);

    /** 같은 원정의 임시 예약만 해제한다. */
    void ReleasePrivateAdmission(FGuid ExpeditionId);

    /** 본인에게 연결된 private 예약만 조회한다. */
    bool IsPrivateAdmissionReserved(const APlayerController* Requester) const;

    /** 늦은 이전 Logout은 이전 연결의 임시 상태만 정리한다. */
    void CancelForConnection(const APlayerController* ExactOldController, EFableTownTravelError Reason);

private:
    /** pending와 terminal 모두 원래 소유 연결을 고정하여 보존한다. */
    struct FTransaction
    {
        TWeakObjectPtr<APlayerController> Player;
        TWeakObjectPtr<AFablePlayerState> Profile;
        FFableTownTravelPrepared Prepared;
        EFableTownTravelState State = EFableTownTravelState::None;
        EFableTownTravelError TerminalError = EFableTownTravelError::None;
        FFableTownGeometryLease TargetLease;
        FFableTownGeometryLease SourceLease;
        FFableTownGeometryLease PreviousPresenceLease;
        FFableTownTravelCommitReceipt Commit;
        FFableTownPlacementEvidence Placement;
        bool bClientLoaded = false;
        bool bOwnArrivalConfirmed = false;
        bool bRollbackRecoveryRequired = false;
        FFableTownMovementRollbackSnapshot RollbackSnapshot;
        double TerminalRetainUntil = 0.0;
    };

    struct FAttempt
    {
        FString Fingerprint;
        FFableTownTravelResponse Response;
        double RetainUntil = 0.0;
    };

    /** 同期owner callbackの再入でもsequence順を守る値コピー通知。 */
    struct FQueuedNotification
    {
        TWeakObjectPtr<APlayerController> Player;
        FFableTownTravelEvent Event;
        FFableTownTravelResponse Response;
        bool bHasResponse = false;
    };

    /** 소유 actor를 기존 manager로 확인한다. */
    AFableTownManager* Manager() const;

    /** callback 중 map이 늘어나도 안정된 기록을 찾는다. */
    FTransaction* FindTransaction(FGuid TransactionId) const;

    /** 현재 인증된 controller/PS/nonce를 대조한다. */
    bool ValidateOwner(const APlayerController* Player, FFableTownConnectionSnapshot& OutSnapshot,
                       EFableTownTravelError& OutError) const;

    /** 실제 포털 위치와 층을 재검사한다. */
    bool ValidateDeparture(const APlayerController* Player, const FFableTownConnectionSnapshot& Snapshot,
                           EFableTownTravelError& OutError) const;

    /** 채택된 catalog만 고정 descriptor로 발급한다. */
    bool DescribeGeometry(const FFableTownAnchorRef& Anchor, FFableTownGeometryDescriptor& OutDescriptor,
                          EFableTownTravelError& OutError) const;

    /** terminal 상태를 대기 상태와 구분한다. */
    bool IsPending(const FTransaction& Transaction) const;

    /** TTL과 현재 연결을 root 시계로 감시한다. */
    void PollTransactions();

    /** 서버 지형의 실제 가시 준비를 확인한다. */
    void TryPrepareOrCommit(FGuid TransactionId);

    /** 이동 snapshot과 전원 조건을 거쳐 한 번 확정한다. */
    void CommitTransaction(FGuid TransactionId);

    /** terminal 상태와 보존 수명을 기록한다. */
    void Finish(FTransaction& Transaction, EFableTownTravelState State, EFableTownTravelError Error);

    /** 성공/복구 정책에 맞는 임시 token만 반환한다. */
    void ReleaseTargetAndSource(FTransaction& Transaction);

    /** 실제 확인 또는 종료 이후 source token을 반환한다. */
    void ReleaseSourceRetention(FTransaction& Transaction);

    /** 값 복사한 이벤트로 재진입을 안전하게 통지한다. */
    void Emit(FTransaction& Transaction, EFableTownTravelEventKind Kind, FGuid AttemptId,
              EFableTownTravelError Error = EFableTownTravelError::None);

    /** 응답을 저장한 뒤 소유자와 관찰자에게 전한다. */
    void Respond(APlayerController* Player, FFableTownTravelResponse Response, const FString& Fingerprint,
                 bool bRememberAttempt = true);

    /** 같은 attempt 재전송의 payload 충돌을 검사한다. */
    bool ReplayAttempt(APlayerController* Player, FGuid AttemptId, const FString& Fingerprint);

    /** 소유 PC의 reliable 응답 채널만 사용한다. */
    void SendResponse(APlayerController* Player, const FFableTownTravelResponse& Response);

    /** 정상 callback 재진입의 응답/이벤트를 FIFO로 발행한다. */
    void QueueNotification(APlayerController* Player, const FFableTownTravelEvent& Event,
                           const FFableTownTravelResponse* Response);

    /** 요청마다 식별 가능한 응답 값을 만든다. */
    FFableTownTravelResponse MakeResponse(const FTransaction* Transaction, FGuid RequestId, FGuid AttemptId,
                                          EFableTownTravelOperation Operation, EFableTownTravelError Error) const;

    TMap<FGuid, TSharedPtr<FTransaction>> Transactions;
    TMap<TWeakObjectPtr<APlayerController>, FGuid> PendingByPlayer;
    TMap<TWeakObjectPtr<APlayerController>, double> LastBeginTime;
    TMap<TWeakObjectPtr<APlayerController>, int64> NextGeneration;
    TMap<TWeakObjectPtr<APlayerController>, FFableTownTravelResponse> LastResponses;
    TMap<TWeakObjectPtr<APlayerController>, TMap<FGuid, FAttempt>> Attempts;
    TMap<TWeakObjectPtr<AFablePlayerState>, FFableTownGeometryLease> PresenceLeases;
    TMap<TWeakObjectPtr<APlayerController>, FGuid> PrivateReservations;
    FOnFableTownTravelEvent AuthorityEvents;
    FTimerHandle PollTimer;
    int64 EventSequence = 0;
    int64 CommitSequence = 0;
    bool bStopping = false;
    bool bDrainingNotifications = false;
    TArray<FQueuedNotification> Notifications;

#if WITH_DEV_AUTOMATION_TESTS
    friend struct FFableTownTravelTestAccess;
#endif
};
