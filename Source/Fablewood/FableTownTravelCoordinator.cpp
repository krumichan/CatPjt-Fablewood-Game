#include "FableTownTravelCoordinator.h"
#include "FableTownManager.h"
#include "FableTownCatalog.h"
#include "FableTownStreamingSubsystem.h"
#include "FablePlayerState.h"
#include "FablePlayerController.h"
#include "FableCharacter.h"
#include "FablePartyManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

/** 時計・worldはowner managerと共有する。 */
UFableTownTravelCoordinator::UFableTownTravelCoordinator()
{
    PrimaryComponentTick.bCanEverTick = false;
}

/** authority만 만료 감시를 실행한다. */
void UFableTownTravelCoordinator::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner() && GetOwner()->HasAuthority() && GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(PollTimer, this, &UFableTownTravelCoordinator::PollTransactions, 0.1f,
                                               true);
    }
}

/** world 종료는 모든 정확 lease를 정리하며 새 연결을 변경하지 않는다. */
void UFableTownTravelCoordinator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bStopping = true;
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(PollTimer);
    }
    TArray<FGuid> EndingIds;
    Transactions.GetKeys(EndingIds);
    for (FGuid Id : EndingIds)
    {
        const auto Guard = Transactions.FindRef(Id);
        if (Guard)
        {
            ReleaseTargetAndSource(*Guard);
            ReleaseSourceRetention(*Guard);
        }
    }
    if (auto* Streaming = GetWorld() ? GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>() : nullptr)
    {
        for (auto& Pair : PresenceLeases)
        {
            Streaming->ReleaseGeometry(Pair.Value);
        }
    }
    PresenceLeases.Reset();
    Super::EndPlay(EndPlayReason);
}

/** 이미 존재하는 manager component만 반환한다. */
UFableTownTravelCoordinator* UFableTownTravelCoordinator::Find(const UObject* Context)
{
    AFableTownManager* Towns = Context ? AFableTownManager::Find(Context->GetWorld()) : nullptr;
    return Towns ? Towns->GetTravelCoordinator() : nullptr;
}

/** 소유 actor가 유일한 공간 manager인지 확인한다. */
AFableTownManager* UFableTownTravelCoordinator::Manager() const
{
    return Cast<AFableTownManager>(GetOwner());
}

/** 同期delegateでmapが増えてもrecordのアドレスを保持する。 */
UFableTownTravelCoordinator::FTransaction* UFableTownTravelCoordinator::FindTransaction(FGuid TransactionId) const
{
    const auto* Record = Transactions.Find(TransactionId);
    return Record ? Record->Get() : nullptr;
}

/** 인증된 현재 연결만 이동 요청을 소유한다. */
bool UFableTownTravelCoordinator::ValidateOwner(const APlayerController* Player,
                                                FFableTownConnectionSnapshot& OutSnapshot,
                                                EFableTownTravelError& OutError) const
{
    OutError = EFableTownTravelError::NotOwner;
    const AFablePlayerState* Profile = IsValid(Player) ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    const AFableTownManager* Towns = Manager();
    if (bStopping || !Towns || !Towns->HasAuthority() || !IsValid(Player) || Player->GetWorld() != GetWorld() ||
        !Profile || Profile->GetOwner() != Player || !Profile->bProfileLoaded ||
        !Towns->GetConnectionSnapshot(Profile, OutSnapshot))
    {
        return false;
    }
    if (Profile->IsBackendClosing() ||
        (Profile->IsBackendEnabled() && (!Profile->IsBackendReady() || Profile->GetBackendPlayerId().IsEmpty())))
    {
        OutError = EFableTownTravelError::ProfileNotReady;
        return false;
    }
    OutError = EFableTownTravelError::None;
    return true;
}

/** 위치는 항상 소유 pawn에서 읽고 실제 포털의 같은 층에서만 출발한다. */
bool UFableTownTravelCoordinator::ValidateDeparture(const APlayerController* Player,
                                                    const FFableTownConnectionSnapshot& Snapshot,
                                                    EFableTownTravelError& OutError) const
{
    const AFableTownManager* Towns = Manager();
    const AFableCharacter* Hero = Player ? Player->GetPawn<AFableCharacter>() : nullptr;
    FFableTownLocationContext Current;
    if (!Towns || !Hero || !Hero->IsAlive() || !Towns->ResolvePhysicalContext(Player, Current) || !Current.IsValid())
    {
        OutError = EFableTownTravelError::InvalidPresence;
        return false;
    }
    if (Current.TownId != Snapshot.PhysicalContext.TownId || Current.FloorId != Snapshot.PhysicalContext.FloorId ||
        Current.ConnectorId != Snapshot.PhysicalContext.ConnectorId)
    {
        OutError = EFableTownTravelError::StalePresence;
        return false;
    }
    FFableTownAnchorRef Portal;
    Portal.TownId = Current.TownId;
    Portal.AnchorId = TEXT("Portal.Expedition");
    Portal.CatalogVersion = Current.CatalogVersion;
    FTransform PortalTransform;
    FFableTownLocationContext PortalContext;
    if (!Towns->GetCatalog()->ResolveAnchor(Portal, 0, PortalTransform, true) ||
        !Towns->GetCatalog()->ResolveLocation(PortalTransform.GetLocation(), PortalContext, true) ||
        Current.FloorId.IsNone() || Current.FloorId != PortalContext.FloorId || !Current.ConnectorId.IsNone())
    {
        OutError = EFableTownTravelError::DepartureFloorMismatch;
        return false;
    }
    if (FVector::Dist(Hero->GetActorLocation(), PortalTransform.GetLocation()) > 350.0)
    {
        OutError = EFableTownTravelError::DepartureOutOfRange;
        return false;
    }
    return true;
}

/** 未採用contentを成功扱いせずLegacyの固定descriptorだけを発行する。 */
bool UFableTownTravelCoordinator::DescribeGeometry(const FFableTownAnchorRef& Anchor,
                                                   FFableTownGeometryDescriptor& OutDescriptor,
                                                   EFableTownTravelError& OutError) const
{
    const UFableTownCatalog* Catalog = Manager() ? Manager()->GetCatalog() : nullptr;
    const FFableTownDefinition* Definition = Catalog ? Catalog->FindTown(Anchor.TownId) : nullptr;
    if (!Definition)
    {
        OutError = EFableTownTravelError::UnknownTown;
        return false;
    }
    if (Anchor.CatalogVersion != Definition->CatalogVersion)
    {
        OutError = EFableTownTravelError::StaleCatalog;
        return false;
    }
    if (!Catalog->IsGeometryReady(Anchor.TownId))
    {
        OutError = EFableTownTravelError::DestinationNotReady;
        return false;
    }
    FTransform AnchorTransform;
    if (!Catalog->ResolveAnchor(Anchor, 0, AnchorTransform, true))
    {
        OutError = EFableTownTravelError::UnknownAnchor;
        return false;
    }
    // 새 content 채택은 후속 단위다. readiness만 메모리에서 바꿔도 승인 identity를 만들지 않는다.
    if (Anchor.TownId != EFableTownId::LegacyVillage)
    {
        OutError = EFableTownTravelError::DestinationNotReady;
        return false;
    }

    OutDescriptor.Mode = EFableTownGeometryMode::ExistingPersistent;
    OutDescriptor.TownId = Anchor.TownId;
    OutDescriptor.CatalogVersion = Anchor.CatalogVersion;
    OutDescriptor.GeometryLevel.Reset();
    OutDescriptor.LocalToWorld = Definition->LocalToWorld;
    OutDescriptor.FixedInstanceName = TEXT("FableTown_LegacyVillage_Geometry_v1");
    OutDescriptor.ContentIdentity = TEXT("LegacyPersistentV1");
    OutError = EFableTownTravelError::None;
    return true;
}

/** 확정/취소를 대기 상태와 구분한다. */
bool UFableTownTravelCoordinator::IsPending(const FTransaction& Transaction) const
{
    return Transaction.State == EFableTownTravelState::AwaitServerGeometry ||
           Transaction.State == EFableTownTravelState::AwaitClientGeometry ||
           Transaction.State == EFableTownTravelState::Placing;
}

/** 동일 attempt의 재전송은 동일 응답이며 다른 payload는 충돌이다. */
bool UFableTownTravelCoordinator::ReplayAttempt(APlayerController* Player, FGuid AttemptId, const FString& Fingerprint)
{
    FFableTownConnectionSnapshot Snapshot;
    EFableTownTravelError Error;
    if (!ValidateOwner(Player, Snapshot, Error))
    {
        return false;
    }
    const auto* OwnerAttempts = Attempts.Find(Player);
    const FAttempt* Attempt = OwnerAttempts ? OwnerAttempts->Find(AttemptId) : nullptr;
    if (!Attempt)
    {
        return false;
    }
    const FTransaction* Original = FindTransaction(Attempt->Response.TransactionId);
    if (Original && (Original->Player.Get() != Player ||
                     Original->Prepared.OwnerConnectionGeneration != Snapshot.OwnerConnectionGeneration ||
                     Original->Prepared.ConnectionNonce != Snapshot.ConnectionNonce))
    {
        return false;
    }
    if (Attempt->Fingerprint == Fingerprint)
    {
        Respond(Player, Attempt->Response, Fingerprint, false);
    }
    else
    {
        auto Response = Attempt->Response;
        Response.Error = EFableTownTravelError::AttemptIdConflict;
        Respond(Player, Response, Fingerprint, false);
    }
    return true;
}

/** last-response를 이벤트보다 먼저 게시한다. nonce는 로그로 출력하지 않는다. */
void UFableTownTravelCoordinator::Respond(APlayerController* Player, FFableTownTravelResponse Response,
                                          const FString& Fingerprint, bool bRememberAttempt)
{
    Response.EventSequence = FMath::Min(EventSequence + 1, FableTownTravel::MaxSafeSequence);
    FAttempt Attempt;
    Attempt.Fingerprint = Fingerprint;
    Attempt.Response = Response;
    Attempt.RetainUntil = (Manager() ? Manager()->GetServerTime() : 0.0) + 40.0;
    auto& OwnerAttempts = Attempts.FindOrAdd(Player);
    if (bRememberAttempt && (OwnerAttempts.Num() < 256 || OwnerAttempts.Contains(Response.AttemptId)))
    {
        OwnerAttempts.Add(Response.AttemptId, Attempt);
    }
    FFableTownTravelEvent Event;
    Event.Sequence = Response.EventSequence;
    EventSequence = Event.Sequence;
    Event.Kind = Response.Error == EFableTownTravelError::None ? EFableTownTravelEventKind::AttemptReceived
                                                               : EFableTownTravelEventKind::AttemptRejected;
    Event.RequestId = Response.RequestId;
    Event.AttemptId = Response.AttemptId;
    Event.TransactionId = Response.TransactionId;
    Event.Generation = Response.Generation;
    Event.State = Response.State;
    Event.Error = Response.Error;
    Event.ServerTime = Manager() ? Manager()->GetServerTime() : 0.0;
    QueueNotification(Player, Event, &Response);
}

/** 소유자 PC의 신뢰 채널만 사용한다. */
void UFableTownTravelCoordinator::SendResponse(APlayerController* Player, const FFableTownTravelResponse& Response)
{
    if (AFablePlayerController* FablePlayer = Cast<AFablePlayerController>(Player))
    {
        FablePlayer->ClientTownTravelResponse(Response);
    }
}

/** listen-local 응답 callback도 다음 이벤트의 순서를 앞지를 수 없게 한다. */
void UFableTownTravelCoordinator::QueueNotification(APlayerController* Player, const FFableTownTravelEvent& Event,
                                                    const FFableTownTravelResponse* Response)
{
    FQueuedNotification Notification;
    Notification.Player = Player;
    Notification.Event = Event;
    if (Response)
    {
        Notification.Response = *Response;
        Notification.bHasResponse = true;
    }
    Notifications.Add(MoveTemp(Notification));
    if (bDrainingNotifications)
    {
        return;
    }

    bDrainingNotifications = true;
    int32 Index = 0;
    while (Index < Notifications.Num() && !bStopping)
    {
        const FQueuedNotification Current = Notifications[Index++];
        if (Current.bHasResponse)
        {
            LastResponses.Add(Current.Player, Current.Response);
        }
        AuthorityEvents.Broadcast(Current.Event);
        if (bStopping)
        {
            break;
        }
        if (auto* FablePlayer = Cast<AFablePlayerController>(Current.Player.Get()))
        {
            FablePlayer->ClientTownTravelEvent(Current.Event);
        }
        if (Current.bHasResponse)
        {
            SendResponse(Current.Player.Get(), Current.Response);
        }
    }
    Notifications.Reset();
    bDrainingNotifications = false;
}

/** 불변 commit을 복사하고 해당 시도 ID를 붙인다. */
FFableTownTravelResponse UFableTownTravelCoordinator::MakeResponse(const FTransaction* Transaction, FGuid RequestId,
                                                                   FGuid AttemptId, EFableTownTravelOperation Operation,
                                                                   EFableTownTravelError Error) const
{
    FFableTownTravelResponse Response;
    Response.RequestId = RequestId;
    Response.AttemptId = AttemptId;
    Response.Operation = Operation;
    Response.Error = Error;
    if (Transaction)
    {
        Response.RequestId = Transaction->Prepared.RequestId;
        Response.TransactionId = Transaction->Prepared.TransactionId;
        Response.Generation = Transaction->Prepared.Generation;
        Response.State = Transaction->State;
        Response.Commit = Transaction->Commit;
        Response.bOwnArrivalConfirmed = Transaction->bOwnArrivalConfirmed;
    }
    return Response;
}

/** 구독자가 동기로 재진입해도 값 복사 이벤트는 유지된다. */
void UFableTownTravelCoordinator::Emit(FTransaction& Transaction, EFableTownTravelEventKind Kind, FGuid AttemptId,
                                       EFableTownTravelError Error)
{
    FFableTownTravelEvent Event;
    if (EventSequence >= FableTownTravel::MaxSafeSequence)
    {
        return;
    }
    Event.Sequence = ++EventSequence;
    Event.Kind = Kind;
    Event.RequestId = Transaction.Prepared.RequestId;
    Event.AttemptId = AttemptId;
    Event.TransactionId = Transaction.Prepared.TransactionId;
    Event.Generation = Transaction.Prepared.Generation;
    Event.State = Transaction.State;
    Event.Error = Error;
    Event.ServerTime = Manager() ? Manager()->GetServerTime() : 0.0;
    QueueNotification(Transaction.Player.Get(), Event, nullptr);
}

/** 新規要求だけを0.8秒で制限しACK/Cancelの順番を妨げない。 */
void UFableTownTravelCoordinator::BeginAnchorTravel(APlayerController* Requester, FGuid RequestId,
                                                    const FFableTownAnchorRef& Target, int64 ExpectedPresenceRevision)
{
    if (bStopping)
    {
        return;
    }
    const FString Fingerprint =
        FString::Printf(TEXT("B|%d|%s|%d|%lld"), static_cast<int32>(Target.TownId), *Target.AnchorId.ToString(),
                        Target.CatalogVersion, ExpectedPresenceRevision);
    if (ReplayAttempt(Requester, RequestId, Fingerprint))
    {
        return;
    }
    auto Reject = [&](EFableTownTravelError Error)
    {
        Respond(Requester, MakeResponse(nullptr, RequestId, RequestId, EFableTownTravelOperation::Begin, Error),
                Fingerprint);
    };
    FFableTownConnectionSnapshot Snapshot;
    EFableTownTravelError Error = EFableTownTravelError::None;
    AFableTownManager* Towns = Manager();
    if (!RequestId.IsValid() || !Towns || !Towns->RegisterOrRefreshConnection(Requester))
    {
        Reject(EFableTownTravelError::NotOwner);
        return;
    }
    Towns->RefreshPhysicalPresence(Requester);
    if (!ValidateOwner(Requester, Snapshot, Error))
    {
        Reject(Error);
        return;
    }
    const double Now = Towns->GetServerTime();
    if (EventSequence >= FableTownTravel::MaxSafeSequence)
    {
        Reject(EFableTownTravelError::RevisionExhausted);
        return;
    }
    if (const double* Last = LastBeginTime.Find(Requester); Last && Now - *Last < 0.8)
    {
        Reject(EFableTownTravelError::RequestRateLimited);
        return;
    }
    LastBeginTime.Add(Requester, Now);
    AFablePlayerState* Profile = Requester->GetPlayerState<AFablePlayerState>();
    if (PendingByPlayer.Contains(Requester))
    {
        Reject(EFableTownTravelError::TravelBusy);
        return;
    }
    bool bRetainedArrival = false;
    for (const auto& Pair : Transactions)
    {
        if (Pair.Value->Player.Get() == Requester &&
            (Pair.Value->bRollbackRecoveryRequired ||
             (Pair.Value->State == EFableTownTravelState::Committed && !Pair.Value->bOwnArrivalConfirmed &&
              Pair.Value->SourceLease.IsValid())))
        {
            bRetainedArrival = true;
        }
    }
    if (bRetainedArrival)
    {
        Reject(EFableTownTravelError::ArrivalUnconfirmed);
        return;
    }
    if (Profile->ActiveExpeditionId.IsValid())
    {
        Reject(EFableTownTravelError::PrivateExpeditionActive);
        return;
    }
    if (IsPrivateAdmissionReserved(Requester))
    {
        Reject(EFableTownTravelError::PrivateAdmissionReserved);
        return;
    }
    if (Profile->IsBackendSavePending())
    {
        Reject(EFableTownTravelError::BackendSavePending);
        return;
    }
    if (Snapshot.PresenceRevision != ExpectedPresenceRevision)
    {
        Reject(EFableTownTravelError::StalePresence);
        return;
    }
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    if (Parties && (Parties->GetPendingInviteFor(Profile->PartyPlayerKey) ||
                    Parties->Invitations.ContainsByPredicate(
                        [Profile, Now](const FFablePartyInvite& Invite)
                        {
                            return Profile->PartyId.IsValid() && Invite.PartyId == Profile->PartyId &&
                                   Invite.ExpiresAt > Now;
                        })))
    {
        Reject(EFableTownTravelError::PartyInvitationPending);
        return;
    }
    FFableTownGeometryDescriptor Descriptor;
    if (!ValidateDeparture(Requester, Snapshot, Error) || !DescribeGeometry(Target, Descriptor, Error))
    {
        Reject(Error);
        return;
    }
    int64& Generation = NextGeneration.FindOrAdd(Requester);
    if (Generation >= FableTownTravel::MaxSafeSequence)
    {
        Reject(EFableTownTravelError::RevisionExhausted);
        return;
    }
    auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
    if (!Streaming)
    {
        Reject(EFableTownTravelError::GeometryUnavailable);
        return;
    }
    FTransaction Transaction;
    Transaction.Player = Requester;
    Transaction.Profile = Profile;
    auto& Prepared = Transaction.Prepared;
    Prepared.RequestId = RequestId;
    Prepared.TransactionId = FGuid::NewGuid();
    Prepared.Generation = ++Generation;
    Prepared.OwnerConnectionGeneration = Snapshot.OwnerConnectionGeneration;
    Prepared.ConnectionNonce = Snapshot.ConnectionNonce;
    Prepared.ChallengeNonce = FGuid::NewGuid();
    Prepared.SourcePresenceRevision = Snapshot.PresenceRevision;
    Prepared.SourcePublicArea = Snapshot.PublicArea;
    Prepared.SourceContext = Snapshot.PhysicalContext;
    Prepared.TargetAnchor = Target;
    Prepared.Geometry = Descriptor;
    Prepared.ExpiresAtServerTime = Now + 20.0;
    Transaction.State = EFableTownTravelState::AwaitServerGeometry;

    FFableTownGeometryLeaseOwner Owner;
    Owner.OwnerConnectionGeneration = Snapshot.OwnerConnectionGeneration;
    Owner.OwnerPlayerState = Profile;
    Owner.TransactionId = Prepared.TransactionId;
    Owner.Purpose = EFableTownGeometryLeasePurpose::SourceRetention;
    FFableTownAnchorRef SourceAnchor;
    SourceAnchor.TownId = Snapshot.PhysicalContext.TownId;
    SourceAnchor.AnchorId = TEXT("Entry");
    SourceAnchor.CatalogVersion = Snapshot.PhysicalContext.CatalogVersion;
    FFableTownGeometryDescriptor SourceDescriptor;
    EFableTownGeometryFailure Failure;
    if (!DescribeGeometry(SourceAnchor, SourceDescriptor, Error) ||
        !Streaming->AcquireGeometry(SourceDescriptor, Owner, Transaction.SourceLease, Failure))
    {
        Reject(EFableTownTravelError::GeometryUnavailable);
        return;
    }
    Owner.Purpose = EFableTownGeometryLeasePurpose::TargetPending;
    if (!Streaming->AcquireGeometry(Descriptor, Owner, Transaction.TargetLease, Failure))
    {
        Streaming->ReleaseGeometry(Transaction.SourceLease);
        Reject(EFableTownTravelError::GeometryLoadFailed);
        return;
    }
    const FGuid Id = Prepared.TransactionId;
    Transactions.Add(Id, MakeShared<FTransaction>(MoveTemp(Transaction)));
    PendingByPlayer.Add(Requester, Id);
    Profile->bPendingTownTravel = true;
    Profile->ForceNetUpdate();
    const auto StoredGuard = Transactions.FindRef(Id);
    FTransaction& Stored = *StoredGuard;
    Respond(Requester,
            MakeResponse(&Stored, RequestId, RequestId, EFableTownTravelOperation::Begin, EFableTownTravelError::None),
            Fingerprint);
    Emit(Stored, EFableTownTravelEventKind::BeginAccepted, RequestId);
    TryPrepareOrCommit(Id);
}

/** 잘못된 tuple은 해당 시도만 거절한다. 올바른 loaded=false만 terminal 취소다. */
void UFableTownTravelCoordinator::AcknowledgeGeometry(APlayerController* Requester, FGuid AttemptId,
                                                      FGuid TransactionId, int64 Generation, FGuid ConnectionNonce,
                                                      FGuid ChallengeNonce, bool bLoaded)
{
    const FString Fingerprint = FString::Printf(TEXT("A|%s|%lld|%s|%s|%d"), *TransactionId.ToString(), Generation,
                                                *ConnectionNonce.ToString(), *ChallengeNonce.ToString(), bLoaded);
    if (ReplayAttempt(Requester, AttemptId, Fingerprint))
    {
        return;
    }
    const auto TransactionGuard = Transactions.FindRef(TransactionId);
    FTransaction* Transaction = TransactionGuard.Get();
    auto Reply = [&](EFableTownTravelError Error)
    {
        auto Response = MakeResponse(Transaction, FGuid(), AttemptId, EFableTownTravelOperation::Acknowledge, Error);
        if (!Transaction)
        {
            Response.TransactionId = TransactionId;
            Response.Generation = Generation;
        }
        Respond(Requester, Response, Fingerprint);
    };
    FFableTownConnectionSnapshot Snapshot;
    EFableTownTravelError Error;
    if (!AttemptId.IsValid() || !ValidateOwner(Requester, Snapshot, Error))
    {
        Reply(EFableTownTravelError::StaleConnection);
        return;
    }
    if (!Transaction || Transaction->Player.Get() != Requester ||
        Transaction->Profile.Get() != Requester->GetPlayerState<AFablePlayerState>())
    {
        Transaction = nullptr;
        Reply(EFableTownTravelError::UnknownTransaction);
        return;
    }
    if (Transaction->Prepared.Generation != Generation)
    {
        Reply(EFableTownTravelError::WrongGeneration);
        return;
    }
    if (Transaction->Prepared.ConnectionNonce != ConnectionNonce)
    {
        Reply(EFableTownTravelError::WrongConnectionNonce);
        return;
    }
    if (Transaction->Prepared.ChallengeNonce != ChallengeNonce)
    {
        Reply(EFableTownTravelError::WrongChallengeNonce);
        return;
    }
    if (!Manager()->IsCurrentConnection(Requester, Transaction->Prepared.OwnerConnectionGeneration, ConnectionNonce))
    {
        Reply(EFableTownTravelError::StaleConnection);
        return;
    }
    if (Transaction->State == EFableTownTravelState::Committed)
    {
        Reply(EFableTownTravelError::AlreadyCommitted);
        return;
    }
    if (Transaction->State == EFableTownTravelState::Cancelled)
    {
        Reply(EFableTownTravelError::AlreadyCancelled);
        return;
    }
    if (Transaction->State == EFableTownTravelState::Expired ||
        Manager()->GetServerTime() >= Transaction->Prepared.ExpiresAtServerTime)
    {
        Finish(*Transaction, EFableTownTravelState::Expired, EFableTownTravelError::Expired);
        Reply(EFableTownTravelError::Expired);
        return;
    }
    if (!bLoaded)
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::ClientLoadFailed);
        Reply(EFableTownTravelError::ClientLoadFailed);
        return;
    }
    if (Transaction->State != EFableTownTravelState::AwaitClientGeometry)
    {
        Reply(EFableTownTravelError::GeometryUnavailable);
        return;
    }
    Transaction->bClientLoaded = true;
    CommitTransaction(TransactionId);
    Transaction = FindTransaction(TransactionId);
    Reply(Transaction && Transaction->State == EFableTownTravelState::Committed
              ? EFableTownTravelError::None
              : (Transaction ? Transaction->TerminalError : EFableTownTravelError::UnknownTransaction));
}

/** 정확한 소유 연결의 취소만 대기를 끝내며 tombstone은 replay를 판정한다. */
void UFableTownTravelCoordinator::CancelAnchorTravel(APlayerController* Requester, FGuid AttemptId, FGuid TransactionId,
                                                     int64 Generation, FGuid ConnectionNonce)
{
    const FString Fingerprint =
        FString::Printf(TEXT("C|%s|%lld|%s"), *TransactionId.ToString(), Generation, *ConnectionNonce.ToString());
    if (ReplayAttempt(Requester, AttemptId, Fingerprint))
    {
        return;
    }
    const auto TransactionGuard = Transactions.FindRef(TransactionId);
    FTransaction* Transaction = TransactionGuard.Get();
    EFableTownTravelError Error = EFableTownTravelError::None;
    FFableTownConnectionSnapshot Snapshot;
    if (!AttemptId.IsValid() || !ValidateOwner(Requester, Snapshot, Error))
    {
        Error = EFableTownTravelError::StaleConnection;
    }
    else if (!Transaction || Transaction->Player.Get() != Requester)
    {
        Transaction = nullptr;
        Error = EFableTownTravelError::UnknownTransaction;
    }
    else if (Transaction->Prepared.Generation != Generation)
    {
        Error = EFableTownTravelError::WrongGeneration;
    }
    else if (Transaction->Prepared.ConnectionNonce != ConnectionNonce)
    {
        Error = EFableTownTravelError::WrongConnectionNonce;
    }
    else if (!Manager()->IsCurrentConnection(Requester, Transaction->Prepared.OwnerConnectionGeneration,
                                             ConnectionNonce))
    {
        Error = EFableTownTravelError::StaleConnection;
    }
    else if (Transaction->State == EFableTownTravelState::Committed)
    {
        Error = EFableTownTravelError::AlreadyCommitted;
    }
    else if (Transaction->State == EFableTownTravelState::Cancelled)
    {
        Error = EFableTownTravelError::AlreadyCancelled;
    }
    else if (Manager()->GetServerTime() >= Transaction->Prepared.ExpiresAtServerTime)
    {
        Finish(*Transaction, EFableTownTravelState::Expired, EFableTownTravelError::Expired);
        Error = EFableTownTravelError::Expired;
    }
    else
    {
        if (Transaction->State == EFableTownTravelState::Placing)
        {
            Transaction->bRollbackRecoveryRequired = true;
        }

        // 취소 종결 이벤트와 소유자 RPC 응답에 같은 사유를 전달한다.
        Error = Transaction->bRollbackRecoveryRequired ? EFableTownTravelError::RollbackFailed
                                                       : EFableTownTravelError::CancelledByOwner;
        Finish(*Transaction, EFableTownTravelState::Cancelled, Error);
    }
    auto Response = MakeResponse(Transaction, FGuid(), AttemptId, EFableTownTravelOperation::Cancel, Error);
    if (!Transaction)
    {
        Response.TransactionId = TransactionId;
        Response.Generation = Generation;
    }
    Respond(Requester, Response, Fingerprint);
}

/** サーバー実ジオメトリーがreadyになった時だけownerへ準備を通知する。 */
void UFableTownTravelCoordinator::TryPrepareOrCommit(FGuid TransactionId)
{
    const auto TransactionGuard = Transactions.FindRef(TransactionId);
    FTransaction* Transaction = TransactionGuard.Get();
    auto* Streaming = GetWorld() ? GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>() : nullptr;
    if (!Transaction || Transaction->State != EFableTownTravelState::AwaitServerGeometry || !Streaming)
    {
        return;
    }
    const auto Status = Streaming->GetGeometryStatus(Transaction->TargetLease);
    if (Status.State == EFableTownGeometryState::Failed || Status.State == EFableTownGeometryState::Rejected ||
        Status.State == EFableTownGeometryState::Removed)
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::GeometryLoadFailed);
        return;
    }
    if (!Streaming->IsGeometryReadyVisible(Transaction->TargetLease))
    {
        return;
    }
    Transaction->State = EFableTownTravelState::AwaitClientGeometry;
    Emit(*Transaction, EFableTownTravelEventKind::Prepared, FGuid());
    Transaction = FindTransaction(TransactionId);
    if (Transaction && Transaction->State == EFableTownTravelState::AwaitClientGeometry)
    {
        if (auto* Player = Cast<AFablePlayerController>(Transaction->Player.Get()))
        {
            Player->ClientPrepareTownGeometry(Transaction->Prepared);
        }
    }
}

/** 전투 상태는 대기 중 보존하고 이동 직전에만 이동 snapshot을 캡처한다. */
void UFableTownTravelCoordinator::CommitTransaction(FGuid TransactionId)
{
    const auto TransactionGuard = Transactions.FindRef(TransactionId);
    FTransaction* Transaction = TransactionGuard.Get();
    if (!Transaction || !IsPending(*Transaction))
    {
        return;
    }
    APlayerController* Player = Transaction->Player.Get();
    AFablePlayerState* Profile = Transaction->Profile.Get();
    auto* Towns = Manager();
    FFableTownConnectionSnapshot Before;
    EFableTownTravelError Error;
    if (!ValidateOwner(Player, Before, Error) ||
        !Towns->IsCurrentConnection(Player, Transaction->Prepared.OwnerConnectionGeneration,
                                    Transaction->Prepared.ConnectionNonce))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::StaleConnection);
        return;
    }
    Towns->RefreshPhysicalPresence(Player);
    if (!Towns->GetConnectionSnapshot(Profile, Before) ||
        Before.PresenceRevision != Transaction->Prepared.SourcePresenceRevision ||
        Before.PublicArea != Transaction->Prepared.SourcePublicArea)
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::StalePresence);
        return;
    }
    if (Profile->ActiveExpeditionId.IsValid() || IsPrivateAdmissionReserved(Player))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::PrivateAdmissionReserved);
        return;
    }
    if (Profile->IsBackendSavePending() || !ValidateDeparture(Player, Before, Error))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled,
               Profile->IsBackendSavePending() ? EFableTownTravelError::BackendSavePending : Error);
        return;
    }
    auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
    if (!Streaming || !Streaming->IsGeometryReadyVisible(Transaction->TargetLease) ||
        !Streaming->IsGeometryReadyVisible(Transaction->SourceLease))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::GeometryUnavailable);
        return;
    }
    if (Before.PresenceRevision >= FableTownTravel::MaxSafeSequence ||
        Before.SafeCheckpointRevision >= FableTownTravel::MaxSafeSequence ||
        CommitSequence >= FableTownTravel::MaxSafeSequence || EventSequence >= FableTownTravel::MaxSafeSequence)
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::RevisionExhausted);
        return;
    }
    AFableCharacter* Hero = Player->GetPawn<AFableCharacter>();
    FFableTownMovementRollbackSnapshot Movement;
    if (!Hero)
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::PlacementFailed);
        return;
    }
    Transaction->State = EFableTownTravelState::Placing;
    Emit(*Transaction, EFableTownTravelEventKind::PlacementStarted, FGuid());
    Transaction = FindTransaction(TransactionId);
    if (!Transaction || Transaction->State != EFableTownTravelState::Placing || Player->GetPawn() != Hero ||
        Hero->GetController() != Player ||
        !Towns->IsCurrentConnection(Player, Transaction->Prepared.OwnerConnectionGeneration,
                                    Transaction->Prepared.ConnectionNonce))
    {
        if (Transaction && Transaction->bRollbackRecoveryRequired)
        {
            // 이 callback 경계 전에는 아직 이동하지 않았다. callback의 정당한 이동을 복원하지 않는다.
            Transaction->bRollbackRecoveryRequired = false;
            ReleaseSourceRetention(*Transaction);
            Transaction->TerminalError = EFableTownTravelError::CancelledByOwner;
            Emit(*Transaction, EFableTownTravelEventKind::Cancelled, FGuid(), EFableTownTravelError::CancelledByOwner);
        }
        return;
    }

    // 동기 구독자가 이동·패배·저장·공개 세대를 바꿨을 수 있으므로 최종 조건을 다시 읽는다.
    Towns->RefreshPhysicalPresence(Player);
    if (!ValidateOwner(Player, Before, Error) || Hero->GetController() != Player ||
        !Towns->IsCurrentConnection(Player, Transaction->Prepared.OwnerConnectionGeneration,
                                    Transaction->Prepared.ConnectionNonce))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::StaleConnection);
        return;
    }
    if (!Hero->IsAlive())
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::InvalidPresence);
        return;
    }
    if (Towns->GetServerTime() >= Transaction->Prepared.ExpiresAtServerTime)
    {
        Finish(*Transaction, EFableTownTravelState::Expired, EFableTownTravelError::Expired);
        return;
    }
    if (Before.PresenceRevision != Transaction->Prepared.SourcePresenceRevision ||
        Before.PublicArea != Transaction->Prepared.SourcePublicArea)
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::StalePresence);
        return;
    }
    if (Profile->ActiveExpeditionId.IsValid() || IsPrivateAdmissionReserved(Player))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::PrivateAdmissionReserved);
        return;
    }
    if (Profile->IsBackendSavePending() || !ValidateDeparture(Player, Before, Error))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled,
               Profile->IsBackendSavePending() ? EFableTownTravelError::BackendSavePending : Error);
        return;
    }
    const AFablePartyManager* PartiesAtCommit = AFablePartyManager::Find(GetWorld());
    if (PartiesAtCommit && (PartiesAtCommit->GetPendingInviteFor(Profile->PartyPlayerKey) ||
                            PartiesAtCommit->Invitations.ContainsByPredicate(
                                [Profile, Towns](const FFablePartyInvite& Invite)
                                {
                                    return Profile->PartyId.IsValid() && Invite.PartyId == Profile->PartyId &&
                                           Invite.ExpiresAt > Towns->GetServerTime();
                                })))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::PartyInvitationPending);
        return;
    }
    FFableTownGeometryDescriptor RecheckedDescriptor;
    if (!DescribeGeometry(Transaction->Prepared.TargetAnchor, RecheckedDescriptor, Error) ||
        !Streaming->IsGeometryReadyVisible(Transaction->TargetLease) ||
        !Streaming->IsGeometryReadyVisible(Transaction->SourceLease))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::GeometryUnavailable);
        return;
    }
    if (!Towns->CanAdvancePresenceRevision() || !Hero->CaptureTownMovementState(Movement))
    {
        Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::RevisionExhausted);
        return;
    }
    Transaction->RollbackSnapshot = Movement;

    // catalog 고정 슬롯만 순회한다. 실패 후에는 이동 snapshot으로만 복원한다.
    bool bPlaced = false;
    for (int32 Slot = 0; Slot < 16; ++Slot)
    {
        FTransform Candidate;
        if (!Towns->GetCatalog()->ResolveAnchor(Transaction->Prepared.TargetAnchor, Slot, Candidate, true))
        {
            break;
        }
        const bool bArrived =
            Hero->TryPlaceAtGroundedTownAnchor(Transaction->Prepared.TargetAnchor, Candidate, Transaction->Placement);
        Transaction = FindTransaction(TransactionId);
        if (!Transaction || Transaction->State != EFableTownTravelState::Placing || Player->GetPawn() != Hero ||
            !Towns->IsCurrentConnection(Player, Transaction->Prepared.OwnerConnectionGeneration,
                                        Transaction->Prepared.ConnectionNonce))
        {
            const bool bRestored = Hero->RestoreTownMovementState(Movement);
            if (Transaction && bRestored)
            {
                Transaction->bRollbackRecoveryRequired = false;
                ReleaseSourceRetention(*Transaction);
                if (Transaction->State == EFableTownTravelState::Cancelled)
                {
                    Transaction->TerminalError = EFableTownTravelError::CancelledByOwner;
                    Emit(*Transaction, EFableTownTravelEventKind::Cancelled, FGuid(),
                         EFableTownTravelError::CancelledByOwner);
                }
            }
            return;
        }
        if (bArrived)
        {
            bPlaced = true;
            break;
        }
        if (!Hero->RestoreTownMovementState(Movement))
        {
            Transaction->bRollbackRecoveryRequired = true;
            Finish(*Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::RollbackFailed);
            return;
        }
    }
    if (!bPlaced)
    {
        const bool bRestored = Hero->RestoreTownMovementState(Movement);
        Transaction->bRollbackRecoveryRequired = !bRestored;
        Finish(*Transaction, EFableTownTravelState::Cancelled,
               bRestored ? Transaction->Placement.Error : EFableTownTravelError::RollbackFailed);
        return;
    }

    // target의presence lease를 먼저 확보한 뒤 안전 위치·공개 명부를 확정한다.
    FFableTownGeometryLeaseOwner Owner;
    Owner.OwnerConnectionGeneration = Before.OwnerConnectionGeneration;
    Owner.OwnerPlayerState = Profile;
    Owner.Purpose = EFableTownGeometryLeasePurpose::Presence;
    FFableTownGeometryLease NewPresenceLease;
    EFableTownGeometryFailure Failure;
    FFableTownConnectionSnapshot After;
    if (!Streaming->AcquireGeometry(Transaction->Prepared.Geometry, Owner, NewPresenceLease, Failure) ||
        !Towns->CommitAnchorPresence(Player, Transaction->Prepared.TargetAnchor,
                                     Transaction->Placement.AcceptedTransform, TransactionId,
                                     Transaction->Prepared.Generation, After))
    {
        const auto* ExistingPresence = PresenceLeases.Find(Profile);
        if (NewPresenceLease.IsValid() && (!ExistingPresence || ExistingPresence->LeaseId != NewPresenceLease.LeaseId))
        {
            Streaming->ReleaseGeometry(NewPresenceLease);
        }
        const bool bRestored = Hero->RestoreTownMovementState(Movement);
        Transaction->bRollbackRecoveryRequired = !bRestored;
        Finish(*Transaction, EFableTownTravelState::Cancelled,
               bRestored ? EFableTownTravelError::PlacementFailed : EFableTownTravelError::RollbackFailed);
        return;
    }
    if (const auto* Previous = PresenceLeases.Find(Profile); Previous && Previous->LeaseId != NewPresenceLease.LeaseId)
    {
        Transaction->PreviousPresenceLease = *Previous;
    }
    PresenceLeases.Add(Profile, NewPresenceLease);
    if (auto* Parties = AFablePartyManager::Find(GetWorld()); Parties && Profile->PartyId.IsValid())
    {
        Parties->InvalidatePreparation(Profile->PartyId);
    }

    auto& Commit = Transaction->Commit;
    Commit.TransactionId = TransactionId;
    Commit.Generation = Transaction->Prepared.Generation;
    Commit.CommitSequence = ++CommitSequence;
    Commit.TargetAnchor = Transaction->Prepared.TargetAnchor;
    Commit.AcceptedTransform = Transaction->Placement.AcceptedTransform;
    Commit.PresenceRevision = After.PresenceRevision;
    Commit.SafeCheckpointRevision = After.SafeCheckpointRevision;
    Commit.GeometryEntryEpoch = NewPresenceLease.EntryEpoch;
    Commit.CommittedAtServerTime = Towns->GetServerTime();
    Commit.CommitCount = 1;
    Finish(*Transaction, EFableTownTravelState::Committed, EFableTownTravelError::None);
}

/** terminal 취소도 source를 실제로 보존한 상태에서 정확 임시 토큰만 반환한다. */
void UFableTownTravelCoordinator::ReleaseTargetAndSource(FTransaction& Transaction)
{
    if (auto* Streaming = GetWorld() ? GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>() : nullptr)
    {
        if (Transaction.TargetLease.IsValid())
        {
            Streaming->ReleaseGeometry(Transaction.TargetLease);
        }
    }
    Transaction.TargetLease = FFableTownGeometryLease();
    if (!Transaction.bRollbackRecoveryRequired &&
        (Transaction.State != EFableTownTravelState::Committed || Transaction.bOwnArrivalConfirmed))
    {
        ReleaseSourceRetention(Transaction);
    }
}

/** 임시 목표 token과 보존해야 할 source를 구분해 terminal 처리한다. */
void UFableTownTravelCoordinator::Finish(FTransaction& Transaction, EFableTownTravelState State,
                                         EFableTownTravelError Error)
{
    if (!IsPending(Transaction))
    {
        return;
    }
    Transaction.State = State;
    Transaction.TerminalError = Error;
    Transaction.TerminalRetainUntil =
        FMath::Max(Transaction.Prepared.ExpiresAtServerTime, Manager()->GetServerTime()) + 20.0;
    if (const FGuid* Id = PendingByPlayer.Find(Transaction.Player); Id && *Id == Transaction.Prepared.TransactionId)
    {
        PendingByPlayer.Remove(Transaction.Player);
    }
    if (AFablePlayerState* Profile = Transaction.Profile.Get();
        Profile &&
        Manager()->IsCurrentConnection(Transaction.Player.Get(), Transaction.Prepared.OwnerConnectionGeneration,
                                       Transaction.Prepared.ConnectionNonce))
    {
        Profile->bPendingTownTravel = false;
        Profile->ForceNetUpdate();
    }
    ReleaseTargetAndSource(Transaction);
    Emit(Transaction,
         State == EFableTownTravelState::Committed
             ? EFableTownTravelEventKind::Committed
             : (State == EFableTownTravelState::Expired ? EFableTownTravelEventKind::Expired
                                                        : EFableTownTravelEventKind::Cancelled),
         FGuid(), Error);
}

/** 有効期限を延ばさず古い接続・presenceを継続監視する。 */
void UFableTownTravelCoordinator::PollTransactions()
{
    if (bStopping || !Manager() || !Manager()->HasAuthority())
    {
        return;
    }
    const double Now = Manager()->GetServerTime();
    TArray<FGuid> Remove;
    TArray<FGuid> Ids;
    Transactions.GetKeys(Ids);
    for (FGuid Id : Ids)
    {
        const auto TransactionGuard = Transactions.FindRef(Id);
        FTransaction* Found = TransactionGuard.Get();
        if (!Found)
        {
            continue;
        }
        FTransaction& Transaction = *Found;
        if (!IsPending(Transaction))
        {
            if (Transaction.bRollbackRecoveryRequired &&
                Manager()->IsCurrentConnection(Transaction.Player.Get(), Transaction.Prepared.OwnerConnectionGeneration,
                                               Transaction.Prepared.ConnectionNonce))
            {
                AFableCharacter* Hero = Transaction.Player->GetPawn<AFableCharacter>();
                auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
                FFableTownLocationContext RestoredContext;
                if (Hero && Streaming && Streaming->IsGeometryReadyVisible(Transaction.SourceLease) &&
                    Hero->RestoreTownMovementState(Transaction.RollbackSnapshot) &&
                    Manager()->ResolvePhysicalContext(Hero, RestoredContext) &&
                    RestoredContext.TownId == (Transaction.State == EFableTownTravelState::Committed
                                                   ? Transaction.Prepared.TargetAnchor.TownId
                                                   : Transaction.Prepared.SourceContext.TownId))
                {
                    Transaction.bRollbackRecoveryRequired = false;
                    if (Transaction.State != EFableTownTravelState::Committed)
                    {
                        ReleaseSourceRetention(Transaction);
                        Emit(Transaction, EFableTownTravelEventKind::Cancelled, FGuid(),
                             EFableTownTravelError::PlacementFailed);
                    }
                    continue;
                }
            }
            if (Now >= Transaction.TerminalRetainUntil && !Transaction.SourceLease.IsValid() &&
                !Transaction.PreviousPresenceLease.IsValid() && !Transaction.bRollbackRecoveryRequired)
            {
                Remove.Add(Id);
            }
            continue;
        }
        if (!Manager()->IsCurrentConnection(Transaction.Player.Get(), Transaction.Prepared.OwnerConnectionGeneration,
                                            Transaction.Prepared.ConnectionNonce))
        {
            Finish(Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::StaleConnection);
        }
        else if (Now >= Transaction.Prepared.ExpiresAtServerTime)
        {
            Finish(Transaction, EFableTownTravelState::Expired, EFableTownTravelError::Expired);
        }
        else
        {
            FFableTownConnectionSnapshot Snapshot;
            Manager()->RefreshPhysicalPresence(Transaction.Player.Get());
            if (!Manager()->GetConnectionSnapshot(Transaction.Profile.Get(), Snapshot) ||
                Snapshot.PresenceRevision != Transaction.Prepared.SourcePresenceRevision ||
                Snapshot.PublicArea != Transaction.Prepared.SourcePublicArea)
            {
                Finish(Transaction, EFableTownTravelState::Cancelled, EFableTownTravelError::StalePresence);
            }
            else
            {
                TryPrepareOrCommit(Id);
            }
        }
    }
    for (FGuid Id : Remove)
    {
        Transactions.Remove(Id);
    }
    for (auto OwnerIt = Attempts.CreateIterator(); OwnerIt; ++OwnerIt)
    {
        for (auto It = OwnerIt.Value().CreateIterator(); It; ++It)
        {
            if (Now >= It.Value().RetainUntil)
            {
                It.RemoveCurrent();
            }
        }
        if (!OwnerIt.Key().IsValid() && OwnerIt.Value().IsEmpty())
        {
            OwnerIt.RemoveCurrent();
        }
    }

    // 사라진 연결의 진단·제한 상태는 세션 수만큼 무한히 쌓지 않는다.
    for (auto It = LastBeginTime.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = NextGeneration.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = LastResponses.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
    for (auto It = PrivateReservations.CreateIterator(); It; ++It)
    {
        if (!It.Key().IsValid())
        {
            It.RemoveCurrent();
        }
    }
    if (auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>())
    {
        for (auto It = PresenceLeases.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid())
            {
                Streaming->ReleaseGeometry(It.Value());
                It.RemoveCurrent();
            }
        }
    }
}

/** 본인 연결의 현재 대기만 읽는다. */
bool UFableTownTravelCoordinator::GetPendingFor(const APlayerController* Requester,
                                                FFableTownTravelPrepared& OutPrepared) const
{
    const FGuid* Id = PendingByPlayer.Find(const_cast<APlayerController*>(Requester));
    const FTransaction* Transaction = Id ? FindTransaction(*Id) : nullptr;
    if (!Transaction || Transaction->Player.Get() != Requester || !IsPending(*Transaction))
    {
        return false;
    }
    OutPrepared = Transaction->Prepared;
    return true;
}

/** 이미 확정된 receipt 조회는 다시 이동시키지 않는다. */
bool UFableTownTravelCoordinator::GetCommittedFor(const APlayerController* Requester, FGuid TransactionId,
                                                  FFableTownTravelCommitReceipt& OutCommit) const
{
    const FTransaction* Transaction = FindTransaction(TransactionId);
    if (!Transaction || Transaction->Player.Get() != Requester ||
        Transaction->State != EFableTownTravelState::Committed)
    {
        return false;
    }
    OutCommit = Transaction->Commit;
    return true;
}

/** 이벤트 상관 ID와 대조할 마지막 응답을 반환한다. */
bool UFableTownTravelCoordinator::GetLastResponseFor(const APlayerController* Requester,
                                                     FFableTownTravelResponse& OutResponse) const
{
    const auto* Response = LastResponses.Find(const_cast<APlayerController*>(Requester));
    if (!Response)
    {
        return false;
    }
    OutResponse = *Response;
    return true;
}

/** 실제 새 물리 질의 결과를 관찰한다. */
bool UFableTownTravelCoordinator::GetPlacementEvidence(FGuid TransactionId,
                                                       FFableTownPlacementEvidence& OutEvidence) const
{
    const FTransaction* Transaction = FindTransaction(TransactionId);
    if (!Transaction)
    {
        return false;
    }
    OutEvidence = Transaction->Placement;
    return true;
}

/** authority 이벤트 구독 지점을 제공한다. */
FOnFableTownTravelEvent& UFableTownTravelCoordinator::OnAuthorityTravelEvent()
{
    return AuthorityEvents;
}

/** 全員を検証してから予約し本人のpending town移動だけを終わらせる。 */
bool UFableTownTravelCoordinator::TryReservePrivateAdmission(const TArray<APlayerController*>& ExactMembers,
                                                             FGuid ExpeditionId)
{
    if (bStopping || !Manager() || !Manager()->HasAuthority() || !ExpeditionId.IsValid() || ExactMembers.IsEmpty())
    {
        return false;
    }
    TSet<APlayerController*> Unique;
    for (APlayerController* Player : ExactMembers)
    {
        FFableTownConnectionSnapshot Snapshot;
        EFableTownTravelError Error;
        if (!Player || Unique.Contains(Player) || !Manager()->RegisterOrRefreshConnection(Player) ||
            !ValidateOwner(Player, Snapshot, Error) || PrivateReservations.Contains(Player))
        {
            return false;
        }
        for (const auto& Pair : Transactions)
        {
            if (Pair.Value->Player.Get() == Player &&
                (Pair.Value->bRollbackRecoveryRequired ||
                 (Pair.Value->State == EFableTownTravelState::Committed && !Pair.Value->bOwnArrivalConfirmed &&
                  Pair.Value->SourceLease.IsValid())))
            {
                return false;
            }
        }
        Unique.Add(Player);
    }
    for (APlayerController* Player : ExactMembers)
    {
        PrivateReservations.Add(Player, ExpeditionId);
        CancelForConnection(Player, EFableTownTravelError::PrivateAdmissionReserved);
    }
    return true;
}

/** 같은 원정이 예약한 항목만 해제한다. */
void UFableTownTravelCoordinator::ReleasePrivateAdmission(FGuid ExpeditionId)
{
    for (auto It = PrivateReservations.CreateIterator(); It; ++It)
    {
        if (It.Value() == ExpeditionId)
        {
            It.RemoveCurrent();
        }
    }
}

/** 다른 참가자의 예약을 현재 요청자에게 적용하지 않는다. */
bool UFableTownTravelCoordinator::IsPrivateAdmissionReserved(const APlayerController* Requester) const
{
    return PrivateReservations.Contains(const_cast<APlayerController*>(Requester));
}

/** 古いLogoutはそのcontrollerのtransactionだけを触る。 */
/** 성공 후 source는 실제 소유자 도착 확인이나 정확 연결 종료 전까지 유지한다. */
void UFableTownTravelCoordinator::ReleaseSourceRetention(FTransaction& Transaction)
{
    if (auto* Streaming = GetWorld() ? GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>() : nullptr)
    {
        if (Transaction.SourceLease.IsValid())
        {
            Streaming->ReleaseGeometry(Transaction.SourceLease);
        }
        if (Transaction.PreviousPresenceLease.IsValid())
        {
            Streaming->ReleaseGeometry(Transaction.PreviousPresenceLease);
        }
    }
    Transaction.SourceLease = FFableTownGeometryLease();
    Transaction.PreviousPresenceLease = FFableTownGeometryLease();
}

/** 클라이언트 XYZ 없이 정확 commit과 현재 서버 pawn을 재검사한다. */
void UFableTownTravelCoordinator::ConfirmOwnArrival(APlayerController* Requester, FGuid AttemptId, FGuid TransactionId,
                                                    int64 Generation, FGuid ConnectionNonce,
                                                    int64 ExpectedCommitSequence)
{
    const FString Fingerprint = FString::Printf(TEXT("R|%s|%lld|%s|%lld"), *TransactionId.ToString(), Generation,
                                                *ConnectionNonce.ToString(), ExpectedCommitSequence);
    if (ReplayAttempt(Requester, AttemptId, Fingerprint))
    {
        return;
    }
    const auto TransactionGuard = Transactions.FindRef(TransactionId);
    FTransaction* Transaction = TransactionGuard.Get();
    EFableTownTravelError Error = EFableTownTravelError::None;
    FFableTownConnectionSnapshot Snapshot;
    if (!AttemptId.IsValid() || !ValidateOwner(Requester, Snapshot, Error))
    {
        Error = EFableTownTravelError::StaleConnection;
    }
    else if (!Transaction || Transaction->Player.Get() != Requester)
    {
        Transaction = nullptr;
        Error = EFableTownTravelError::UnknownTransaction;
    }
    else if (Transaction->Prepared.Generation != Generation)
    {
        Error = EFableTownTravelError::WrongGeneration;
    }
    else if (Transaction->Prepared.ConnectionNonce != ConnectionNonce)
    {
        Error = EFableTownTravelError::WrongConnectionNonce;
    }
    else if (!Manager()->IsCurrentConnection(Requester, Transaction->Prepared.OwnerConnectionGeneration,
                                             ConnectionNonce))
    {
        Error = EFableTownTravelError::StaleConnection;
    }
    else if (Transaction->State != EFableTownTravelState::Committed ||
             Transaction->Commit.CommitSequence != ExpectedCommitSequence ||
             Snapshot.LastCommittedTransactionId != TransactionId || Snapshot.LastCommittedGeneration != Generation)
    {
        Error = EFableTownTravelError::ArrivalUnconfirmed;
    }
    else if (Requester->GetPlayerState<AFablePlayerState>()->ActiveExpeditionId.IsValid() ||
             Requester->GetPlayerState<AFablePlayerState>()->GetPublicFieldScopeId().IsValid() ||
             IsPrivateAdmissionReserved(Requester))
    {
        Error = EFableTownTravelError::ArrivalUnconfirmed;
    }
    else if (!Transaction->bOwnArrivalConfirmed)
    {
        AFableCharacter* Hero = Requester->GetPawn<AFableCharacter>();
        auto* Movement = Hero ? Hero->GetCharacterMovement() : nullptr;
        FFindFloorResult Floor;
        if (Movement)
        {
            Movement->FindFloor(Hero->GetActorLocation(), Floor, false);
        }
        const FVector Difference =
            Hero ? Hero->GetActorLocation() - Transaction->Commit.AcceptedTransform.GetLocation() : FVector(MAX_flt);
        if (!Hero || !Hero->IsAlive() || !Movement ||
            !Hero->IsGroundedAtCurrentTownAnchor(Transaction->Commit.TargetAnchor) || !Floor.IsWalkableFloor() ||
            Floor.FloorDist < 0.0f || Floor.FloorDist > 3.0f || FVector2D(Difference).Size() > 10.0f ||
            FMath::Abs(Difference.Z) > 10.0f)
        {
            Error = EFableTownTravelError::ArrivalUnconfirmed;
            FFableTownLocationContext CurrentTown;
            FFableTownMovementRollbackSnapshot MovementBefore;
            FFableTownPlacementEvidence Reconciled;
            if (Hero && Hero->IsAlive() && Manager()->ResolvePhysicalContext(Hero, CurrentTown) &&
                CurrentTown.TownId == Transaction->Commit.TargetAnchor.TownId &&
                Hero->CaptureTownMovementState(MovementBefore))
            {
                if (!Hero->TryPlaceAtGroundedTownAnchor(Transaction->Commit.TargetAnchor,
                                                        Transaction->Commit.AcceptedTransform, Reconciled))
                {
                    if (!Hero->RestoreTownMovementState(MovementBefore))
                    {
                        Transaction->bRollbackRecoveryRequired = true;
                        Transaction->RollbackSnapshot = MovementBefore;
                    }
                }
                Hero->ForceNetUpdate();
            }
            Requester->GetPlayerState<AFablePlayerState>()->SetNotice(
                TEXT("안전 지점의 실제 도착을 확인 중입니다. 위치 확인 뒤 다시 이동할 수 있습니다."));
        }
        else
        {
            Transaction->bOwnArrivalConfirmed = true;
            ReleaseSourceRetention(*Transaction);
        }
    }
    auto Response = MakeResponse(Transaction, FGuid(), AttemptId, EFableTownTravelOperation::ConfirmArrival, Error);
    if (!Transaction)
    {
        Response.TransactionId = TransactionId;
        Response.Generation = Generation;
    }
    Respond(Requester, Response, Fingerprint);
    Transaction = FindTransaction(TransactionId);
    if (Error == EFableTownTravelError::None && Transaction && Transaction->bOwnArrivalConfirmed)
    {
        Emit(*Transaction, EFableTownTravelEventKind::OwnArrivalObserved, AttemptId);
        Transaction = FindTransaction(TransactionId);
        if (Transaction)
        {
            Emit(*Transaction, EFableTownTravelEventKind::SourceLeaseReleased, AttemptId);
        }
    }
}

/** 정확한 이전 controller 수명만 종료한다. */
void UFableTownTravelCoordinator::CancelForConnection(const APlayerController* ExactOldController,
                                                      EFableTownTravelError Reason)
{
    if (!ExactOldController || bStopping)
    {
        return;
    }
    if (const FGuid* Id = PendingByPlayer.Find(const_cast<APlayerController*>(ExactOldController)))
    {
        const auto Guard = Transactions.FindRef(*Id);
        if (Guard && Guard->Player.Get() == ExactOldController)
        {
            Finish(*Guard, EFableTownTravelState::Cancelled, Reason);
        }
    }
    if (Reason == EFableTownTravelError::StaleConnection || Reason == EFableTownTravelError::WorldEnded)
    {
        PrivateReservations.Remove(const_cast<APlayerController*>(ExactOldController));
        TArray<FGuid> OldIds;
        Transactions.GetKeys(OldIds);
        for (FGuid OldId : OldIds)
        {
            const auto Guard = Transactions.FindRef(OldId);
            if (Guard && Guard->Player.Get() == ExactOldController)
            {
                Guard->bRollbackRecoveryRequired = false;
                ReleaseSourceRetention(*Guard);
            }
        }
        if (const auto* Profile = ExactOldController->GetPlayerState<AFablePlayerState>();
            Profile && Profile->GetOwner() == ExactOldController)
        {
            if (const auto* Lease = PresenceLeases.Find(const_cast<AFablePlayerState*>(Profile)))
            {
                if (auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>())
                {
                    Streaming->ReleaseGeometry(*Lease);
                }
                PresenceLeases.Remove(const_cast<AFablePlayerState*>(Profile));
            }
        }
    }
}
