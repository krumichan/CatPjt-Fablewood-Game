#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interfaces/MovementBaseInterface.h"
#include "FableTownTypes.h"
#include "FableTownTravelTypes.generated.h"

class AFablePlayerState;
class UPrimitiveComponent;

namespace FableTownTravel
{
    inline constexpr int64 MaxSafeSequence = 9007199254740991LL;
}

/** 서버 이동의 단계이며 취소와 성공을 구분한다. / 移動の状態を区別する。 */
UENUM()
enum class EFableTownTravelState : uint8
{
    None,
    AwaitServerGeometry,
    AwaitClientGeometry,
    Placing,
    Committed,
    Cancelled,
    Expired
};

UENUM()
enum class EFableTownTravelOperation : uint8
{
    Begin,
    Acknowledge,
    Cancel,
    ConfirmArrival
};

UENUM()
enum class EFableTownGeometryMode : uint8
{
    ExistingPersistent,
    StreamedLevel
};

UENUM()
enum class EFableTownTravelPurpose : uint8
{
    AnchorTravel,
    PrivateReturn,
    PrivateRecovery,
    PrivateAbandonReturn,
    ReconnectRestore
};

UENUM()
enum class EFableTownTravelEventKind : uint8
{
    BeginReceived,
    BeginAccepted,
    Prepared,
    AttemptReceived,
    AttemptRejected,
    Cancelled,
    Expired,
    PlacementStarted,
    Committed,
    OwnArrivalObserved,
    UiUnlocked,
    SourceLeaseReleased
};

/** 네트워크 시도마다 원인을 명시하며 정상 대기 상태와 분리한다. */
UENUM()
enum class EFableTownTravelError : uint8
{
    None,
    InvalidRequest,
    NotOwner,
    ProfileNotReady,
    BackendSavePending,
    UnknownTown,
    UnknownAnchor,
    StaleCatalog,
    DestinationNotReady,
    InvalidPresence,
    StalePresence,
    DepartureOutOfRange,
    DepartureFloorMismatch,
    PrivateExpeditionActive,
    PrivateAdmissionReserved,
    PartyInvitationPending,
    TravelBusy,
    RequestRateLimited,
    UnknownTransaction,
    WrongGeneration,
    WrongConnectionNonce,
    WrongChallengeNonce,
    StaleConnection,
    Expired,
    ClientLoadFailed,
    CancelledByOwner,
    AlreadyCancelled,
    AlreadyCommitted,
    AttemptIdConflict,
    GeometryLoadFailed,
    GeometryUnavailable,
    PreflightBlocked,
    NoWalkableFloor,
    CapsulePenetrating,
    ArrivalNotGrounded,
    ArrivalOutOfBounds,
    PlacementFailed,
    RollbackFailed,
    WorldEnded,
    RevisionExhausted,
    ArrivalUnconfirmed
};

UENUM()
enum class EFableTownGeometryLeasePurpose : uint8
{
    Presence,
    TargetPending,
    SourceRetention,
    ServerResident,
    // 同じWorldのlocal playerもserver tokenとは別の寿命を持つ。既存値は維持する。
    ClientPresence,
    ClientTargetPending,
    ClientSourceRetention
};

UENUM()
enum class EFableTownGeometryState : uint8
{
    Rejected,
    Loading,
    LoadedNotVisible,
    ReadyVisible,
    Unloading,
    Failed,
    Removed
};

UENUM()
enum class EFableTownGeometryFailure : uint8
{
    None,
    InvalidDescriptor,
    GeometryNotAdopted,
    DescriptorConflict,
    InvalidOwner,
    RemovingBusy,
    LoadFailed,
    WorldEnded
};

/** 서버 catalog가 발급한 고정 지형 설명이다. 클라이언트의 임의 경로/좌표가 아니다. */
USTRUCT()
struct FABLEWOOD_API FFableTownGeometryDescriptor
{
    GENERATED_BODY()

    UPROPERTY()
    EFableTownGeometryMode Mode = EFableTownGeometryMode::ExistingPersistent;
    UPROPERTY()
    EFableTownId TownId = EFableTownId::Invalid;
    UPROPERTY()
    int32 CatalogVersion = 0;
    UPROPERTY()
    TSoftObjectPtr<UWorld> GeometryLevel;
    UPROPERTY()
    FTransform LocalToWorld = FTransform::Identity;
    UPROPERTY()
    FName FixedInstanceName = NAME_None;
    UPROPERTY()
    FString ContentIdentity;
};

/** 전송되지 않는 신뢰된 lease 소유자. / RPCから受け取らない所有情報。 */
struct FABLEWOOD_API FFableTownGeometryLeaseOwner
{
    int64 OwnerConnectionGeneration = 0;
    TWeakObjectPtr<AFablePlayerState> OwnerPlayerState;
    FGuid TransactionId;
    EFableTownGeometryLeasePurpose Purpose = EFableTownGeometryLeasePurpose::Presence;
};

/** lease 수량 대신 정확한 토큰과 로드 세대를 사용한다. */
struct FABLEWOOD_API FFableTownGeometryLease
{
    FGuid LeaseId;
    EFableTownId TownId = EFableTownId::Invalid;
    int32 CatalogVersion = 0;
    int64 EntryEpoch = 0;

    /** 발급된 정확 토큰의 최소 형식만 검사한다. / 発行tokenの最小形式を検査する。 */
    bool IsValid() const
    {
        return LeaseId.IsValid() && TownId != EFableTownId::Invalid && CatalogVersion > 0 && EntryEpoch > 0;
    }
};

/** 후발 로드 실패도 조회와 이벤트에서 동일하게 관찰한다. */
struct FABLEWOOD_API FFableTownGeometryStatus
{
    EFableTownGeometryState State = EFableTownGeometryState::Rejected;
    int64 EntryEpoch = 0;
    bool bReadyVisible = false;
    EFableTownGeometryFailure Failure = EFableTownGeometryFailure::None;
};

/** 실제 현재 연결과 안전 앵커를 소유자에게만 복제한다. 영구 저장 형식은 아니다. */
USTRUCT()
struct FABLEWOOD_API FFableTownConnectionSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid WorldEpoch;
    UPROPERTY()
    int64 OwnerConnectionGeneration = 0;
    UPROPERTY()
    FGuid ConnectionNonce;
    UPROPERTY()
    int64 PresenceRevision = 0;
    UPROPERTY()
    FFableTownLocationContext PhysicalContext;
    UPROPERTY()
    FFablePublicAreaKey PublicArea;
    UPROPERTY()
    FFableTownAnchorRef SafeAnchor;
    UPROPERTY()
    int64 SafeCheckpointRevision = 0;
    UPROPERTY()
    FGuid LastCommittedTransactionId;
    UPROPERTY()
    int64 LastCommittedGeneration = 0;
    UPROPERTY()
    FTransform LastAcceptedTransform = FTransform::Identity;
};

/** 준비 통지는 소유 연결의 원래 만료 시각을 변경하지 않는다. */
USTRUCT()
struct FABLEWOOD_API FFableTownTravelPrepared
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid RequestId;
    UPROPERTY()
    FGuid TransactionId;
    UPROPERTY()
    int64 Generation = 0;
    UPROPERTY()
    int64 OwnerConnectionGeneration = 0;
    UPROPERTY()
    FGuid ConnectionNonce;
    UPROPERTY()
    FGuid ChallengeNonce;
    UPROPERTY()
    int64 SourcePresenceRevision = 0;
    UPROPERTY()
    FFablePublicAreaKey SourcePublicArea;
    UPROPERTY()
    FFableTownLocationContext SourceContext;
    UPROPERTY()
    FFableTownAnchorRef TargetAnchor;
    UPROPERTY()
    FFableTownGeometryDescriptor Geometry;
    UPROPERTY()
    double ExpiresAtServerTime = 0.0;
};

/** 성공 위치와 revision은 재전송해도 바뀌지 않는다. */
USTRUCT()
struct FABLEWOOD_API FFableTownTravelCommitReceipt
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid TransactionId;
    UPROPERTY()
    int64 Generation = 0;
    UPROPERTY()
    int64 CommitSequence = 0;
    UPROPERTY()
    FFableTownAnchorRef TargetAnchor;
    UPROPERTY()
    FTransform AcceptedTransform = FTransform::Identity;
    UPROPERTY()
    int64 PresenceRevision = 0;
    UPROPERTY()
    int64 SafeCheckpointRevision = 0;
    UPROPERTY()
    int64 GeometryEntryEpoch = 0;
    UPROPERTY()
    double CommittedAtServerTime = 0.0;
    UPROPERTY()
    int32 CommitCount = 0;
};

USTRUCT()
struct FABLEWOOD_API FFableTownTravelResponse
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid RequestId;
    UPROPERTY()
    FGuid AttemptId;
    UPROPERTY()
    EFableTownTravelOperation Operation = EFableTownTravelOperation::Begin;
    UPROPERTY()
    FGuid TransactionId;
    UPROPERTY()
    int64 Generation = 0;
    UPROPERTY()
    EFableTownTravelState State = EFableTownTravelState::None;
    UPROPERTY()
    EFableTownTravelError Error = EFableTownTravelError::None;
    UPROPERTY()
    int64 EventSequence = 0;
    UPROPERTY()
    FFableTownTravelCommitReceipt Commit;
    UPROPERTY()
    bool bOwnArrivalConfirmed = false;
};

USTRUCT()
struct FABLEWOOD_API FFableTownTravelEvent
{
    GENERATED_BODY()

    UPROPERTY()
    int64 Sequence = 0;
    UPROPERTY()
    EFableTownTravelEventKind Kind = EFableTownTravelEventKind::BeginReceived;
    UPROPERTY()
    FGuid RequestId;
    UPROPERTY()
    FGuid AttemptId;
    UPROPERTY()
    FGuid TransactionId;
    UPROPERTY()
    int64 Generation = 0;
    UPROPERTY()
    EFableTownTravelState State = EFableTownTravelState::None;
    UPROPERTY()
    EFableTownTravelError Error = EFableTownTravelError::None;
    UPROPERTY()
    double ServerTime = 0.0;
    UPROPERTY()
    bool bAuthority = true;
};

/** 텔레포트 직전 이동 상태만 캡처한다. 대기 중 실제 전투 결과는 되돌리지 않는다. */
struct FABLEWOOD_API FFableTownMovementRollbackSnapshot
{
    FTransform Transform = FTransform::Identity;
    FRotator ControlRotation = FRotator::ZeroRotator;
    FVector Velocity = FVector::ZeroVector;
    EMovementMode MovementMode = MOVE_None;
    uint8 CustomMovementMode = 0;
    FMovementBaseInterfaceData MovementBase;
    FName BaseBone = NAME_None;
    FFindFloorResult CurrentFloor;
    bool bJustTeleported = false;
    bool bForceNextFloorCheck = false;
};

/** 실제 물리 질의 결과만 기록한다. / 実際の物理照会のみ記録する。 */
struct FABLEWOOD_API FFableTownPlacementEvidence
{
    FTransform RequestedCandidate = FTransform::Identity;
    FTransform AcceptedTransform = FTransform::Identity;
    float ActualCapsuleRadius = 0.0f;
    float ActualCapsuleHalfHeight = 0.0f;
    int64 QuerySequence = 0;
    double QueryServerTime = 0.0;
    FFindFloorResult FreshFloor;
    bool bFreshCapsuleClear = false;
    bool bWalkingOnGround = false;
    bool bIntendedSupport = false;
    FName ActualSupportName = NAME_None;
    EFableTownTravelError Error = EFableTownTravelError::None;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFableTownGeometryChanged, const FFableTownGeometryLease&,
                                     const FFableTownGeometryStatus&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFableTownTravelPrepared, const FFableTownTravelPrepared&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFableTownTravelResponse, const FFableTownTravelResponse&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnFableTownTravelEvent, const FFableTownTravelEvent&);

/** 정상 관찰용 절대 전투 시각이며 이동 시 복원하지 않는다. */
struct FABLEWOOD_API FFableTownCombatState
{
    float NextAttackServerTime = 0;
    float NextSkillServerTime = 0;
    float NextSecondSkillServerTime = 0;
    float NextThirdSkillServerTime = 0;
    float NextDodgeServerTime = 0;
    float CombatBuildEndServerTime = 0;
    float ShieldAmount = 0;
    float ShieldEndServerTime = 0;
    float ActionLockEndServerTime = 0;
    FVector SafeSpawnLocation = FVector::ZeroVector;
    FRotator SafeSpawnRotation = FRotator::ZeroRotator;
};
