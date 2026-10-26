#pragma once

#include "CoreMinimal.h"
#include "FableTownTypes.h"
#include "FableTownPersistenceTypes.generated.h"

/** 실제 저장 확인과 메모리 검증을 구분한다. */
UENUM()
enum class EFableTownAnchorSaveState : uint8
{
    None,
    Queued,
    Sending,
    Committed,
    VerifiedExistingSnapshot,
    Failed,
    MemoryOnly
};

/** 원본 검증과 첫 실제 저장 확인의 관찰값이며 프로필 파일에는 기록하지 않는다. */
USTRUCT()
struct FABLEWOOD_API FFableProfileReadStatus
{
    GENERATED_BODY()

    UPROPERTY()
    int32 OriginalVersion = -1;

    UPROPERTY()
    FString OriginalHash;

    UPROPERTY()
    int64 OriginalRevision = 0;

    UPROPERTY()
    bool bOriginalMirrorsValidated = false;

    UPROPERTY()
    bool bHistoricalFieldsCompared = false;

    UPROPERTY()
    // 同じ現行serializerによる既存保存フィールドの一致。原本header全体の同一性ではない。
    bool bHistoricalFieldsByteExact = false;

    UPROPERTY()
    int32 CurrentVersion = 0;

    UPROPERTY()
    FString FirstAcknowledgedHash;

    UPROPERTY()
    int64 FirstAcknowledgedRevision = 0;
};

/** 소유자에게만 전달하는 현재 이동의 영구 저장 확인 상태. */
USTRUCT()
struct FABLEWOOD_API FFableTownAnchorSaveStatus
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid TravelTransactionId;

    UPROPERTY()
    int64 AnchorSequence = 0;

    UPROPERTY()
    FFableTownAnchorRef Anchor;

    UPROPERTY()
    EFableTownAnchorSaveState State = EFableTownAnchorSaveState::None;

    UPROPERTY()
    FString AcknowledgedProfileHash;

    UPROPERTY()
    int64 AcknowledgedRevision = 0;
};

/** 전송 전 등록하고 Sent bytes와 함께 고정하는 서버 전용 증서. */
struct FABLEWOOD_API FFableTownAnchorSaveWitness
{
    FGuid TravelTransactionId;
    int64 AnchorSequence = 0;
    FFableTownAnchorRef Anchor;
    FString SnapshotHash;
    FString StablePlayerId;
    int64 SentExpectedRevision = 0;
    int64 OwnerConnectionGeneration = 0;
    FGuid ConnectionNonce;
    FGuid WorldEpoch;

    /** 빈 증서나 다른 bytes를 저장 성공으로 처리하지 않는다. */
    bool IsFor(const FString& PlayerId, const FString& Hash) const
    {
        return TravelTransactionId.IsValid() && AnchorSequence > 0 && Anchor.IsValid() &&
               OwnerConnectionGeneration > 0 && ConnectionNonce.IsValid() && WorldEpoch.IsValid() &&
               SnapshotHash.Len() == 64 && SnapshotHash == Hash && !StablePlayerId.IsEmpty() &&
               StablePlayerId == PlayerId;
    }
};
