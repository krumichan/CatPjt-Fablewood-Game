#include "FableGameMode.h"
#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FablePartyEncounter.h"
#include "FablePartyManager.h"
#include "FablePlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

/** 独立Nativeワールドも現在ワールドの唯一のサーバー正本だけを使う。 */
AFableGameMode* AFableGameMode::Find(const UWorld* World)
{
    if (!World || World->GetNetMode() == NM_Client)
    {
        return nullptr;
    }
    if (AFableGameMode* Mode = World->GetAuthGameMode<AFableGameMode>())
    {
        return Mode->HasAuthority() ? Mode : nullptr;
    }

    AFableGameMode* Result = nullptr;
    for (TActorIterator<AFableGameMode> It(World); It; ++It)
    {
        if (It->HasAuthority() && !It->IsActorBeingDestroyed())
        {
            if (Result)
            {
                return nullptr;
            }
            Result = *It;
        }
    }
    return Result;
}

/** 古いPS・別アカウント・別ワールドの結果アクセスを拒否する。 */
bool AFableGameMode::OwnsResultProfile(const AFablePlayerState* Profile) const
{
    const APlayerController* Player = IsValid(Profile) ? Cast<APlayerController>(Profile->GetOwner()) : nullptr;
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    return HasAuthority() && Profile && Profile->GetWorld() == GetWorld() && Profile->bProfileLoaded &&
           !Profile->PartyPlayerKey.IsEmpty() && Player && Player->GetPlayerState<AFablePlayerState>() == Profile &&
           Parties && Parties->GetConnectedPlayer(Profile->PartyPlayerKey) == Player &&
           (!Profile->IsBackendEnabled() || Profile->IsBackendReady());
}

/** 公開キーを任意指定できず、現在の本人の結果だけを返す。 */
const FFablePersonalExpeditionResult* AFableGameMode::GetResultByExpedition(const AFablePlayerState* Recipient,
                                                                            FGuid ExpeditionId) const
{
    if (!OwnsResultProfile(Recipient) || !ExpeditionId.IsValid())
    {
        return nullptr;
    }
    const FFableCharacterExpeditionLedger* Ledger = ExpeditionResults.Find(Recipient->PartyPlayerKey);
    return Ledger ? Ledger->Results.Find(ExpeditionId) : nullptr;
}

/** 최신 종료 결과 확인을 요구해 미확인 기록이 누적되는 잠금을 예방한다. */
bool AFableGameMode::HasAcknowledgedLatestExpeditionResult(const AFablePlayerState* Recipient) const
{
    if (!OwnsResultProfile(Recipient))
    {
        return false;
    }
    const FFableCharacterExpeditionLedger* Ledger = ExpeditionResults.Find(Recipient->PartyPlayerKey);
    if (!Ledger)
    {
        return true;
    }
    for (int32 Index = Ledger->Order.Num() - 1; Index >= 0; --Index)
    {
        const FFablePersonalExpeditionResult* Result = Ledger->Results.Find(Ledger->Order[Index]);
        if (Result && Result->IsTerminal())
        {
            return Result->bAcknowledged;
        }
    }
    return true;
}

/** メモリ保存と実際の永続保存を区別して本人にのみ伝える。 */
void AFableGameMode::PublishExpeditionResult(FFablePersonalExpeditionResult& Result)
{
    Result.bSaveReady = true;
    Result.bAllGrantsCommitted = Result.bRequiresBackendCommit;
    for (const FFablePersonalGrantReceipt& Receipt : Result.Receipts)
    {
        Result.bSaveReady &= Receipt.SaveState != EFableExpeditionGrantSaveState::Queued;
        Result.bAllGrantsCommitted &= Receipt.SaveState == EFableExpeditionGrantSaveState::Committed;
    }

    AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    APlayerController* Player = Parties ? Parties->GetConnectedPlayer(Result.RecipientPublicKey) : nullptr;
    AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    if (OwnsResultProfile(Profile))
    {
        Profile->ExpeditionResult = Result;
        Profile->ForceNetUpdate();
    }
}

/** 再表示は既存の不変明細をコピーするだけで再支給しない。 */
bool AFableGameMode::RequestExpeditionResult(APlayerController* Requester, FGuid ExpeditionId)
{
    AFablePlayerState* Profile = IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr;
    const FFablePersonalExpeditionResult* Result = GetResultByExpedition(Profile, ExpeditionId);
    if (!Result || Profile->GetOwner() != Requester)
    {
        return false;
    }
    Profile->ExpeditionResult = *Result;
    Profile->ForceNetUpdate();
    return true;
}

/** 閲覧確認は保存ACKと別で、正確な終了版にだけ適用する。 */
bool AFableGameMode::AcknowledgeExpeditionResult(APlayerController* Requester, FGuid ExpeditionId, FGuid CompletionId,
                                                 int32 ExpectedResultRevision)
{
    AFablePlayerState* Profile = IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr;
    const FFablePersonalExpeditionResult* Existing = GetResultByExpedition(Profile, ExpeditionId);
    if (!Existing || Profile->GetOwner() != Requester || !Existing->IsTerminal() ||
        Existing->CompletionId != CompletionId || Existing->ResultRevision != ExpectedResultRevision)
    {
        return false;
    }
    FFablePersonalExpeditionResult& Result =
        ExpeditionResults.FindChecked(Profile->PartyPlayerKey).Results.FindChecked(ExpeditionId);
    Result.bAcknowledged = true;
    PublishExpeditionResult(Result);
    return true;
}

/** 同じ認証済みキャラクターの新PSに同じサーバーの最新結果を復元する。 */
void AFableGameMode::RestoreExpeditionResult(AFablePlayerState* Profile)
{
    if (!OwnsResultProfile(Profile))
    {
        return;
    }
    FFableCharacterExpeditionLedger* Ledger = ExpeditionResults.Find(Profile->PartyPlayerKey);
    if (!Ledger || Ledger->Order.IsEmpty())
    {
        return;
    }
    // 새 PS는 실제 확인된 경계만 상속하고 이전 실패 세션의 미저장을 주장하지 않는다.
    Profile->ExpeditionSnapshotGrantSequence =
        FMath::Max(Profile->ExpeditionSnapshotGrantSequence, Ledger->LastCommittedGrantSequence);
    FGuid Id = Profile->ActiveExpeditionId;
    if (!Ledger->Results.Contains(Id))
    {
        Id = Ledger->Order.Last();
    }
    if (FFablePersonalExpeditionResult* Result = Ledger->Results.Find(Id))
    {
        PublishExpeditionResult(*Result);
    }
}

/** 未確認・未保存を保護し、保存済みで確認済みの古い結果だけを消す。 */
bool AFableGameMode::EnsureResultCapacity(const FFableExpeditionTicket& Ticket)
{
    static constexpr int32 MaxResults = 16;
    for (const FString& Key : Ticket.MemberKeys)
    {
        FFableCharacterExpeditionLedger& Ledger = ExpeditionResults.FindOrAdd(Key);
        const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
        const APlayerController* Player = Parties ? Parties->GetConnectedPlayer(Key) : nullptr;
        AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        if (!HasAcknowledgedLatestExpeditionResult(Profile))
        {
            if (Profile)
            {
                Profile->SetNotice(TEXT("이전 원정 결과를 먼저 확인하세요."));
            }
            return false;
        }
        if (Profile && Profile->IsBackendEnabled() &&
            Profile->ExpeditionSnapshotGrantSequence < Ledger.LastGrantSequence)
        {
            return false;
        }
        while (Ledger.Results.Num() >= MaxResults)
        {
            int32 Removable = INDEX_NONE;
            for (int32 Index = 0; Index + 2 < Ledger.Order.Num(); ++Index)
            {
                const FFablePersonalExpeditionResult* Result = Ledger.Results.Find(Ledger.Order[Index]);
                if (Result && Result->IsTerminal() && Result->bAcknowledged && Result->bSaveReady)
                {
                    Removable = Index;
                    break;
                }
            }
            if (Removable == INDEX_NONE)
            {
                return false;
            }
            Ledger.Results.Remove(Ledger.Order[Removable]);
            Ledger.Order.RemoveAt(Removable);
        }
    }
    return true;
}

/** 入場成功後、元の不変名簿全員の個人台帳を作る。 */
void AFableGameMode::RegisterExpeditionResults(const FFableExpeditionTicket& Ticket)
{
    AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    for (const FString& Key : Ticket.MemberKeys)
    {
        FFableCharacterExpeditionLedger& Ledger = ExpeditionResults.FindOrAdd(Key);
        if (Ledger.Results.Contains(Ticket.ExpeditionId))
        {
            continue;
        }
        APlayerController* Player = Parties ? Parties->GetConnectedPlayer(Key) : nullptr;
        const AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        FFablePersonalExpeditionResult Result;
        Result.Ticket = Ticket;
        Result.RecipientPublicKey = Key;
        Result.bRequiresBackendCommit = Profile && Profile->IsBackendEnabled();
        Ledger.Order.Add(Ticket.ExpeditionId);
        FFablePersonalExpeditionResult& Added = Ledger.Results.Add(Ticket.ExpeditionId, MoveTemp(Result));
        PublishExpeditionResult(Added);
    }
}

/** 実際の死亡・証書・受領者・場所を検査して一度だけ支給する。 */
bool AFableGameMode::RecordExpeditionDefeat(AFablePartyEncounter* Encounter, AFableEnemy* Enemy,
                                            AFablePlayerState* Recipient)
{
    if (!HasAuthority() || !IsValid(Encounter) || Encounter->GetWorld() != GetWorld() || !Encounter->IsCommitted() ||
        !IsValid(Enemy) || Enemy->GetWorld() != GetWorld() || Enemy->IsAlive() ||
        Enemy->GetExpeditionId() != Encounter->GetExpeditionId() || !Enemy->GetSpawnId().IsValid() ||
        !Encounter->HasRecordedDefeat(Enemy->GetSpawnId()) || !OwnsResultProfile(Recipient) ||
        !Encounter->IsParticipant(Recipient) || !Recipient->IsExpeditionEligible(Encounter->GetTicket().Region))
    {
        return false;
    }
    const APlayerController* Player = Cast<APlayerController>(Recipient->GetOwner());
    const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
    if (!Pawn || !FableAdventure::For(Recipient).IsInRegion(Encounter->GetTicket().Region, Pawn->GetActorLocation()))
    {
        return false;
    }

    RegisterExpeditionResults(Encounter->GetTicket());
    FFableCharacterExpeditionLedger& Ledger = ExpeditionResults.FindChecked(Recipient->PartyPlayerKey);
    FFablePersonalExpeditionResult& Result = Ledger.Results.FindChecked(Encounter->GetExpeditionId());
    if (Recipient->ExpeditionSnapshotGrantSequence < Ledger.LastGrantSequence || Result.IsTerminal() ||
        Result.Receipts.ContainsByPredicate(
            [&](const FFablePersonalGrantReceipt& Receipt)
            {
                return Receipt.GrantId == Enemy->GetSpawnId();
            }))
    {
        return false;
    }

    // 支給helperは保存を呼ばず、明細の順番を発行するまでqueueしない。
    FFablePersonalGrantReceipt Receipt;
    Receipt.ExpeditionId = Encounter->GetExpeditionId();
    Receipt.GrantId = Enemy->GetSpawnId();
    Receipt.RecipientPublicKey = Recipient->PartyPlayerKey;
    Receipt.bBoss = Enemy->IsBoss();
    if (!Recipient->ApplyExpeditionReward(Encounter->GetTicket(), Enemy->IsBoss(), Receipt))
    {
        return false;
    }
    Receipt.Sequence = ++Ledger.LastGrantSequence;
    Recipient->ExpeditionSnapshotGrantSequence = Receipt.Sequence;
    Receipt.SaveState = Recipient->IsBackendEnabled() ? EFableExpeditionGrantSaveState::Queued
                                                      : EFableExpeditionGrantSaveState::MemoryOnly;
    Result.Receipts.Add(MoveTemp(Receipt));
    ++Result.ResultRevision;
    PublishExpeditionResult(Result);

    // 不変明細とsequenceの発行後、その境界を含むsnapshotを保存する。
    const bool bQueued = Recipient->SaveProfile();
    Recipient->SetNotice(bQueued ? TEXT("원정 전투 보상을 받았습니다. 의뢰 보고 보상은 별도입니다.")
                                 : TEXT("원정 보상 저장을 완료하지 못했습니다. 저장 상태를 확인하세요."));
    return true;
}

/** 終了IDは最初に一度だけ発行し全員の個人結果で共有する。 */
void AFableGameMode::CompleteExpeditionResult(AFablePartyEncounter* Encounter, EFableExpeditionOutcome Outcome)
{
    if (!HasAuthority() || !IsValid(Encounter) || Encounter->GetWorld() != GetWorld() || !Encounter->IsCommitted() ||
        Outcome == EFableExpeditionOutcome::Active)
    {
        return;
    }
    // 결과를 임의로 주입하지 않고 실제 진행·구조·귀환 경계와 일치해야 한다.
    if ((Outcome == EFableExpeditionOutcome::Cleared &&
         (Encounter->GetProgress().AdventureStage != EFableAdventureStage::Cleared ||
          !Encounter->GetProgress().bObjectiveComplete)) ||
        (Outcome == EFableExpeditionOutcome::Recovered && !Encounter->GetProgress().bPartyRecoveryPending &&
         !Encounter->bReturnAfterWipe) ||
        (Outcome == EFableExpeditionOutcome::Abandoned && !Encounter->bTransitionPending &&
         !Encounter->IsActorBeingDestroyed() && !Encounter->GetParticipants().IsEmpty()))
    {
        return;
    }

    RegisterExpeditionResults(Encounter->GetTicket());
    FGuid CompletionId;
    for (const FString& Key : Encounter->GetTicket().MemberKeys)
    {
        const FFablePersonalExpeditionResult& Result =
            ExpeditionResults.FindChecked(Key).Results.FindChecked(Encounter->GetExpeditionId());
        if (Result.IsTerminal())
        {
            CompletionId = Result.CompletionId;
            break;
        }
    }
    if (!CompletionId.IsValid())
    {
        CompletionId = FGuid::NewGuid();
    }
    for (const FString& Key : Encounter->GetTicket().MemberKeys)
    {
        FFablePersonalExpeditionResult& Result =
            ExpeditionResults.FindChecked(Key).Results.FindChecked(Encounter->GetExpeditionId());
        if (!Result.IsTerminal())
        {
            Result.Outcome = Outcome;
            Result.CompletionId = CompletionId;
            ++Result.ResultRevision;
            PublishExpeditionResult(Result);
        }
    }
}

/** snapshotが含む個人支給の累積境界を返す。 */
int64 AFableGameMode::GetExpeditionGrantSequence(const FString& PublicKey) const
{
    const FFableCharacterExpeditionLedger* Ledger = ExpeditionResults.Find(PublicKey);
    return HasAuthority() && Ledger ? Ledger->LastGrantSequence : 0;
}

/** 実際に直列化したhashと発行済みsequenceだけを登録する。 */
void AFableGameMode::TrackExpeditionSaveSnapshot(const FString& PublicKey, int64 IncludedGrantSequence,
                                                 const FString& Hash)
{
    FFableCharacterExpeditionLedger* Ledger = ExpeditionResults.Find(PublicKey);
    if (!HasAuthority() || !Ledger || Hash.Len() != 64 || IncludedGrantSequence < 0 ||
        IncludedGrantSequence > Ledger->LastGrantSequence)
    {
        return;
    }
    int64& Tracked = Ledger->SnapshotGrantSequences.FindOrAdd(Hash);
    Tracked = FMath::Max(Tracked, IncludedGrantSequence);
}

/** 検証済みbatchのhighwaterまでの明細だけを保存済みにする。 */
void AFableGameMode::NotifyExpeditionSaveCommitted(const FString& PublicKey, int64 IncludedGrantSequence,
                                                   int64 BackendRevision, const FString& ValidatedHash)
{
    FFableCharacterExpeditionLedger* Ledger = ExpeditionResults.Find(PublicKey);
    const int64* Tracked = Ledger ? Ledger->SnapshotGrantSequences.Find(ValidatedHash) : nullptr;
    if (!HasAuthority() || !Ledger || !Tracked || IncludedGrantSequence < 0 || BackendRevision <= 0 ||
        *Tracked < IncludedGrantSequence || IncludedGrantSequence > Ledger->LastGrantSequence)
    {
        return;
    }
    Ledger->LastCommittedGrantSequence = FMath::Max(Ledger->LastCommittedGrantSequence, IncludedGrantSequence);
    for (TPair<FGuid, FFablePersonalExpeditionResult>& Entry : Ledger->Results)
    {
        bool bChanged = false;
        for (FFablePersonalGrantReceipt& Receipt : Entry.Value.Receipts)
        {
            if (Receipt.Sequence <= IncludedGrantSequence &&
                Receipt.SaveState == EFableExpeditionGrantSaveState::Queued)
            {
                Receipt.SaveState = EFableExpeditionGrantSaveState::Committed;
                Receipt.CommittedRevision = BackendRevision;
                bChanged = true;
            }
        }
        if (bChanged)
        {
            ++Entry.Value.ResultRevision;
            PublishExpeditionResult(Entry.Value);
        }
    }
    for (auto It = Ledger->SnapshotGrantSequences.CreateIterator(); It; ++It)
    {
        if (It.Key() != ValidatedHash && It.Value() <= IncludedGrantSequence)
        {
            It.RemoveCurrent();
        }
    }
}

/** 終了結果と同じ名簿・現在接続・保存確認を再検査する。 */
bool AFableGameMode::CanPrepareExpeditionRetry(APlayerController* Requester, FGuid PartyId, FGuid OldExpeditionId,
                                               FGuid CompletionId) const
{
    const AFablePlayerState* Profile = IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr;
    const FFablePersonalExpeditionResult* Result = GetResultByExpedition(Profile, OldExpeditionId);
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    const FFablePartyRecord* Party = Parties && Profile ? Parties->GetPartyForMember(Profile->PartyPlayerKey) : nullptr;
    if (!Profile || Profile->GetOwner() != Requester || !Result || !Result->IsTerminal() ||
        Result->CompletionId != CompletionId || Result->RetryExpeditionId.IsValid() || !Party ||
        Party->PartyId != PartyId || Result->Ticket.PartyId != PartyId ||
        Party->Members.Num() != Result->Ticket.MemberKeys.Num() || !Result->bSaveReady ||
        Parties->Invitations.ContainsByPredicate(
            [&](const FFablePartyInvite& Invite)
            {
                return Invite.PartyId == PartyId;
            }))
    {
        return false;
    }
    for (const FFablePartyMember& Member : Party->Members)
    {
        APlayerController* Player = Parties->GetConnectedPlayer(Member.PlayerKey);
        const AFablePlayerState* Other = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        const FFablePersonalExpeditionResult* Personal = GetResultByExpedition(Other, OldExpeditionId);
        if (!Member.bConnected || !Result->Ticket.Contains(Member.PlayerKey) || !Personal || !Personal->IsTerminal() ||
            Personal->CompletionId != CompletionId || Personal->RetryExpeditionId.IsValid() || !Personal->bSaveReady ||
            !HasAcknowledgedLatestExpeditionResult(Other) || Other->IsBackendSavePending() ||
            !Other->IsExpeditionEligible(Result->Ticket.Region) ||
            !IsRetryPreparationParticipant(Other, OldExpeditionId, CompletionId))
        {
            return false;
        }
        // 완료 달샘의 개인 봉인 조사는 기존 귀환 정책과 동일하게 보존한다.
        const AFableCharacter* Hero = Player->GetPawn<AFableCharacter>();
        if (Result->Ticket.Region == EFableRegion::Moonwell && Other->StoryChapter == 4 && Other->StorySealMask != 7 &&
            Hero && Hero->IsAlive() &&
            FableAdventure::For(Other).IsInRegion(EFableRegion::Moonwell, Hero->GetActorLocation()))
        {
            return false;
        }
    }
    return true;
}

/** 再挑戦は村か本人の完了済み遠征内でだけ準備できる。 */
bool AFableGameMode::IsRetryPreparationParticipant(const AFablePlayerState* Profile, FGuid OldExpeditionId,
                                                   FGuid CompletionId) const
{
    const FFablePersonalExpeditionResult* Result = GetResultByExpedition(Profile, OldExpeditionId);
    const APlayerController* Player = Profile ? Cast<APlayerController>(Profile->GetOwner()) : nullptr;
    const AFableCharacter* Hero = Player ? Player->GetPawn<AFableCharacter>() : nullptr;
    if (!Result || !Result->IsTerminal() || Result->CompletionId != CompletionId ||
        Result->RetryExpeditionId.IsValid() || !Hero || !Hero->IsAlive() ||
        Hero->GetPlayerState<AFablePlayerState>() != Profile)
    {
        return false;
    }
    if (!Profile->ActiveExpeditionId.IsValid())
    {
        return FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation());
    }
    const AFablePartyEncounter* Encounter = FindExpedition(Profile->ActiveExpeditionId);
    return Profile->ActiveExpeditionId == OldExpeditionId && Result->Outcome == EFableExpeditionOutcome::Cleared &&
           Encounter && Encounter->IsParticipant(Profile) && !Encounter->IsReturning() &&
           Encounter->GetProgress().AdventureStage == EFableAdventureStage::Cleared &&
           (FableAdventure::For(Profile).IsInRegion(Result->Ticket.Region, Hero->GetActorLocation()) ||
            FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation()));
}

/** 新遠征の全員到着が確定した後だけ旧結果を消費する。 */
bool AFableGameMode::RequestRetryExpedition(APlayerController* Requester, FGuid PartyId,
                                            int32 ExpectedPreparationRevision, FGuid OldExpeditionId,
                                            FGuid CompletionId)
{
    AFablePlayerState* Profile = IsValid(Requester) ? Requester->GetPlayerState<AFablePlayerState>() : nullptr;
    AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    const FFablePartyRecord* Party = Parties && Profile ? Parties->GetPartyForMember(Profile->PartyPlayerKey) : nullptr;
    if (!CanPrepareExpeditionRetry(Requester, PartyId, OldExpeditionId, CompletionId) || !Party ||
        Party->LeaderKey != Profile->PartyPlayerKey || Party->PreparationRevision != ExpectedPreparationRevision ||
        Party->RetrySourceExpeditionId != OldExpeditionId || Party->RetrySourceCompletionId != CompletionId)
    {
        if (Profile)
        {
            Profile->SetNotice(TEXT("재도전할 결과와 전원의 새로운 준비를 확인하세요."));
        }
        return false;
    }
    // 완료 원정 내부 교체와 마을 재입장을 구분하고 기존 원정은 아직 유지한다.
    AFablePartyEncounter* Previous = FindExpedition(OldExpeditionId);
    if (Previous &&
        (Previous->IsReturning() || Previous->GetProgress().AdventureStage != EFableAdventureStage::Cleared))
    {
        return false;
    }
    if (!Previous && !CanUseTownService(Requester, FableRPG::PortalLocation(), 330))
    {
        Profile->SetNotice(TEXT("마을 포털 근처에서 재도전을 시작하세요."));
        return false;
    }
    // 전원 도착 실패 시 이 함수는 원래 위치·증서·결과를 그대로 보존한다.
    if (!StartPreparedExpedition(Requester, PartyId, ExpectedPreparationRevision, Previous))
    {
        return false;
    }
    const FGuid NewId = Profile->ActiveExpeditionId;
    const FFablePersonalExpeditionResult* NewResult = GetResultByExpedition(Profile, NewId);
    if (!NewId.IsValid() || !NewResult)
    {
        return false;
    }
    // 성공 후에만 이전 완료 ID를 소비해 중복 요청의 새 원정 생성을 막는다.
    const TArray<FString> Keys = NewResult->Ticket.MemberKeys;
    for (const FString& Key : Keys)
    {
        FFablePersonalExpeditionResult& OldResult =
            ExpeditionResults.FindChecked(Key).Results.FindChecked(OldExpeditionId);
        OldResult.RetryExpeditionId = NewId;
        ++OldResult.ResultRevision;
    }
    if (IsValid(Previous))
    {
        Previous->RetireAfterReplacement(NewId);
    }
    Parties->InvalidatePreparation(PartyId);
    return true;
}
