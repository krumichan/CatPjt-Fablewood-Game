#include "FablePlayerState.h"
#include "FableSaveGame.h"
#include "FableTownManager.h"
#include "FableTownTravelCoordinator.h"
#include "FableProfileSchema.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

/** 실제 공개된 현재 연결의 receipt를 다시 조회한 뒤에만 저장한다. */
bool AFablePlayerState::RecordCommittedTownAnchor(const FFableTownTravelCommitReceipt& Commit)
{
    auto* Player = Cast<APlayerController>(GetOwner());
    auto* Coordinator = UFableTownTravelCoordinator::Find(this);
    auto* Towns = AFableTownManager::Find(GetWorld());
    FFableTownTravelCommitReceipt Actual;
    FFableTownConnectionSnapshot Connection;
    if (!HasAuthority() || !Player || !Coordinator || !Towns || bStorageBlocked ||
        !Coordinator->GetCommittedFor(Player, Commit.TransactionId, Actual) || Actual.Generation != Commit.Generation ||
        Actual.CommitSequence != Commit.CommitSequence || Actual.CommitCount != 1 ||
        !FableProfileSchema::IsSafeAnchor(Actual.TargetAnchor) || !Towns->GetConnectionSnapshot(this, Connection) ||
        Connection.LastCommittedTransactionId != Actual.TransactionId ||
        Connection.LastCommittedGeneration != Actual.Generation)
    {
        return false;
    }

    if (TownAnchorSaveStatus.TravelTransactionId == Actual.TransactionId)
    {
        return TownAnchorSaveStatus.State != EFableTownAnchorSaveState::Failed;
    }
    if (AnchorSaveSequence >= FableTownTravel::MaxSafeSequence ||
        (TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Queued ||
         TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Sending))
    {
        return false;
    }

    LastSafeTownAnchor = Actual.TargetAnchor;
    TownAnchorSaveStatus = FFableTownAnchorSaveStatus();
    TownAnchorSaveStatus.TravelTransactionId = Actual.TransactionId;
    TownAnchorSaveStatus.AnchorSequence = ++AnchorSaveSequence;
    TownAnchorSaveStatus.Anchor = Actual.TargetAnchor;
    TownAnchorSaveStatus.State = EFableTownAnchorSaveState::Queued;

    // Queue 전에 증서를 식별할 상태를 먼저 등록한다. 실패한 공개 이동을 되돌리지는 않는다.
    const bool bSaved =
        bBackendEnabled && bAwaitingProfileRestore ? QueueAcknowledgedTownRestoreSnapshot() : SaveProfile();
    if (!bSaved)
    {
        TownAnchorSaveStatus.State = EFableTownAnchorSaveState::Failed;
        SetNotice(TEXT("안전 지점의 저장을 확인하지 못했습니다. 원본과 지형을 보존합니다."));
    }
    else if (!bBackendEnabled)
    {
        TownAnchorSaveStatus.State =
            bPersistenceEnabled ? EFableTownAnchorSaveState::Committed : EFableTownAnchorSaveState::MemoryOnly;
    }
    ForceNetUpdate();
    return bSaved;
}

/** 메모리 fixture의 통과는 DB 저장 성공과 구별한다. */
bool AFablePlayerState::IsTownAnchorDurable(FGuid TransactionId) const
{
    if (!TransactionId.IsValid() || TownAnchorSaveStatus.TravelTransactionId != TransactionId)
    {
        return false;
    }
    return TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Committed ||
           TownAnchorSaveStatus.State == EFableTownAnchorSaveState::VerifiedExistingSnapshot ||
           (!bBackendEnabled && !bPersistenceEnabled &&
            TownAnchorSaveStatus.State == EFableTownAnchorSaveState::MemoryOnly);
}

/** Sent 증서는 현재 다른 이동이나 재접속한 소유자를 완료시킬 수 없다. */
void AFablePlayerState::AcceptTownAnchorAcknowledgement(const FFableTownAnchorSaveWitness& Witness, int64 Revision,
                                                        bool bExisting)
{
    const auto& Connection = GetTownConnectionSnapshot();
    if (!HasAuthority() || !Witness.IsFor(BackendPlayerId, Witness.SnapshotHash) ||
        Witness.SnapshotHash != RegisteredTownSnapshotHash ||
        Witness.TravelTransactionId != TownAnchorSaveStatus.TravelTransactionId ||
        Witness.AnchorSequence != TownAnchorSaveStatus.AnchorSequence ||
        Witness.OwnerConnectionGeneration != Connection.OwnerConnectionGeneration ||
        Witness.ConnectionNonce != Connection.ConnectionNonce || Witness.WorldEpoch != Connection.WorldEpoch ||
        Witness.Anchor.TownId != TownAnchorSaveStatus.Anchor.TownId ||
        Witness.Anchor.AnchorId != TownAnchorSaveStatus.Anchor.AnchorId ||
        Witness.Anchor.CatalogVersion != TownAnchorSaveStatus.Anchor.CatalogVersion || Revision < 1 ||
        Revision > FableTownTravel::MaxSafeSequence ||
        (bExisting ? Revision != Witness.SentExpectedRevision : Revision != Witness.SentExpectedRevision + 1))
    {
        return;
    }

    TownAnchorSaveStatus.State =
        bExisting ? EFableTownAnchorSaveState::VerifiedExistingSnapshot : EFableTownAnchorSaveState::Committed;
    TownAnchorSaveStatus.AcknowledgedRevision = Revision;
    TownAnchorSaveStatus.AcknowledgedProfileHash = Witness.SnapshotHash;
    ForceNetUpdate();
}
