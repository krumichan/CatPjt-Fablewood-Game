#include "FablePartyManager.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "FableGameMode.h"
#include "FableAdventure.h"
#include "FablePlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

/** 모든 참가자가 같은 서버 판정 결과를 관찰하도록 명부를 복제한다. */
AFablePartyManager::AFablePartyManager()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(10);
}

/** 현재 월드의 유일한 파티 명부를 조회한다. */
AFablePartyManager* AFablePartyManager::Find(const UWorld* World)
{
    if (World)
    {
        for (TActorIterator<AFablePartyManager> It(const_cast<UWorld*>(World)); It; ++It)
        {
            return *It;
        }
    }

    return nullptr;
}

/** 초대장과 파티 구성의 공개 정보만 복제한다. */
void AFablePartyManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFablePartyManager, Parties);
    DOREPLIFETIME(AFablePartyManager, Invitations);
}

/** 초대 및 재접속 유예의 만료는 서버 시각으로 처리한다. */
void AFablePartyManager::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        GetWorldTimerManager().SetTimer(ExpiryTimer, this, &AFablePartyManager::ExpireEntries, 1, true);
    }
}

/** 월드 수명보다 오래 남는 만료 콜백을 제거한다. */
void AFablePartyManager::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(ExpiryTimer);
    Super::EndPlay(Reason);
}

/** 인증 완료 전 연결과 다른 월드의 컨트롤러 요청을 거부한다. */
AFablePlayerState* AFablePartyManager::ValidatePlayer(APlayerController* Player) const
{
    if (!HasAuthority() || !IsValid(Player) || Player->GetWorld() != GetWorld())
    {
        return nullptr;
    }

    AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
    if (!IsValid(Profile) || Profile->GetOwner() != Player || Profile->GetWorld() != GetWorld() ||
        !Profile->bProfileLoaded || (Profile->IsBackendEnabled() && !Profile->IsBackendReady()))
    {
        return nullptr;
    }

    return Profile;
}

/** 서버 연결 목록에서만 초대·위임 대상을 찾는다. */
APlayerController* AFablePartyManager::FindPlayer(int32 PlayerId) const
{
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* Player = It->Get();
        const AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        if (Profile && Profile->GetPlayerId() == PlayerId)
        {
            return Player;
        }
    }

    return nullptr;
}

/** 복제된 프로필의 공개 키를 이용하며 비공개 백엔드 식별자는 노출하지 않는다. */
FString AFablePartyManager::GetPlayerKey(int32 PlayerId) const
{
    for (TActorIterator<AFablePlayerState> It(GetWorld()); It; ++It)
    {
        if (It->GetPlayerId() == PlayerId && !It->IsInactive())
        {
            return It->PartyPlayerKey;
        }
    }

    return FString();
}

/** 명부의 불변 공개 키로 소속을 찾는다. */
const FFablePartyRecord* AFablePartyManager::GetPartyForMember(const FString& PlayerKey) const
{
    if (PlayerKey.IsEmpty())
    {
        return nullptr;
    }

    return Parties.FindByPredicate(
        [&](const FFablePartyRecord& Party)
        {
            return Party.Members.ContainsByPredicate(
                [&](const FFablePartyMember& Member)
                {
                    return Member.PlayerKey == PlayerKey;
                });
        });
}

/** UI가 알고 있는 접속 ID를 서버 발급 키로 바꿔 소속을 조회한다. */
const FFablePartyRecord* AFablePartyManager::GetPartyForMember(int32 PlayerId) const
{
    return GetPartyForMember(GetPlayerKey(PlayerId));
}

/** 수신자와 유효 시간이 일치하는 초대만 반환한다. */
const FFablePartyInvite* AFablePartyManager::GetPendingInviteFor(const FString& PlayerKey) const
{
    const AGameStateBase* Clock = GetWorld()->GetGameState();
    const float Now = Clock ? Clock->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    return Invitations.FindByPredicate(
        [&](const FFablePartyInvite& Invite)
        {
            return !PlayerKey.IsEmpty() && Invite.TargetKey == PlayerKey && Invite.ExpiresAt > Now;
        });
}

/** 접속 ID 기반 UI에서도 같은 초대 수신자 규칙을 적용한다. */
const FFablePartyInvite* AFablePartyManager::GetPendingInviteFor(int32 PlayerId) const
{
    return GetPendingInviteFor(GetPlayerKey(PlayerId));
}

/** 파티 외 참가자는 동료로 취급하지 않는다. */
bool AFablePartyManager::ArePartyMembers(const AFablePlayerState* A, const AFablePlayerState* B) const
{
    if (!A || !B || !A->PartyId.IsValid() || A->PartyId != B->PartyId)
    {
        return false;
    }

    const FFablePartyRecord* Party = GetPartyForMember(A->PartyPlayerKey);
    return Party && Party->PartyId == A->PartyId && GetPartyForMember(B->PartyPlayerKey) == Party;
}

/** 실제 인증 식별자를 비공개 표에 보관하고 공개 키로 재접속 멤버십을 연결한다. */
void AFablePartyManager::RegisterPlayer(APlayerController* Player)
{
    AFablePlayerState* Profile = ValidatePlayer(Player);
    if (!Profile)
    {
        return;
    }

    // 온라인 캐릭터는 인증된 ID를 쓰며 로컬 실험은 해당 연결 범위로 제한한다.
    const FString Identity = Profile->IsBackendEnabled() ? TEXT("online:") + Profile->GetBackendPlayerId()
                                                         : TEXT("local:") + FString::FromInt(Profile->GetPlayerId());
    FString& PublicKey = PublicKeysByIdentity.FindOrAdd(Identity);
    if (PublicKey.IsEmpty())
    {
        PublicKey = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    }

    Profile->PartyPlayerKey = PublicKey;
    const TWeakObjectPtr<APlayerController>* PreviousConnection = CurrentConnections.Find(PublicKey);
    const bool bSameConnection = PreviousConnection && PreviousConnection->Get() == Player;
    CurrentConnections.Add(PublicKey, Player);
    ExpireEntries();
    for (FFablePartyRecord& Party : Parties)
    {
        for (FFablePartyMember& Member : Party.Members)
        {
            if (Member.PlayerKey == PublicKey)
            {
                const bool bChanged =
                    !bSameConnection || !Member.bConnected || Member.PlayerId != Profile->GetPlayerId();
                Member.PlayerId = Profile->GetPlayerId();
                Member.bConnected = true;
                Member.ReconnectDeadline = 0;
                if (bChanged)
                {
                    ++Party.Revision;
                    ResetReadiness(Party);
                    Profile->SetNotice(TEXT("기존 파티에 다시 합류했습니다."));
                }
            }
        }

        // 모두 끊겼다가 돌아온 첫 동료에게 권한을 주고 기존 온라인 리더는 유지한다.
        RepairLeader(Party);
    }

    Publish();
}

/** 온라인 파티장을 우선하며 모두 끊겼다면 유예 중 기존 명부를 보존한다. */
void AFablePartyManager::RepairLeader(FFablePartyRecord& Party)
{
    const FFablePartyMember* Leader = Party.Members.FindByPredicate(
        [&](const FFablePartyMember& Member)
        {
            return Member.PlayerKey == Party.LeaderKey && Member.bConnected;
        });
    if (Leader)
    {
        return;
    }

    const FFablePartyMember* Next = Party.Members.FindByPredicate(
        [](const FFablePartyMember& Member)
        {
            return Member.bConnected;
        });
    if (!Next && !Party.Members.IsEmpty())
    {
        Next = &Party.Members[0];
    }

    Party.LeaderKey = Next ? Next->PlayerKey : FString();
}

/** 접속 종료는 탈퇴와 구분하고 정원 자리를 제한 시간 동안 예약한다. */
void AFablePartyManager::DisconnectPlayer(APlayerController* Player)
{
    if (!HasAuthority() || !Player)
    {
        return;
    }

    AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
    if (!Profile)
    {
        return;
    }

    // 예전 연결의 늦은 Logout으로 새 연결의 온라인 상태를 덮어쓰지 않는다.
    const TWeakObjectPtr<APlayerController>* Current = CurrentConnections.Find(Profile->PartyPlayerKey);
    if (!Current || Current->Get() != Player)
    {
        return;
    }

    CurrentConnections.Remove(Profile->PartyPlayerKey);
    for (FFablePartyRecord& Party : Parties)
    {
        for (FFablePartyMember& Member : Party.Members)
        {
            if (Member.PlayerKey == Profile->PartyPlayerKey)
            {
                Member.bConnected = false;
                Member.PlayerId = INDEX_NONE;
                Member.ReconnectDeadline = GetWorld()->GetTimeSeconds() + ReconnectGrace;
                ++Party.Revision;
                ResetReadiness(Party);
            }
        }

        RepairLeader(Party);
    }

    Invitations.RemoveAll(
        [&](const FFablePartyInvite& Invite)
        {
            return Invite.TargetKey == Profile->PartyPlayerKey || Invite.InviterKey == Profile->PartyPlayerKey;
        });
    LastRequestTimes.Remove(Player);
    Publish();
}

/** 원정 참가 파티는 잠그고 마을에 남은 다른 파티의 편집은 허용한다. */
bool AFablePartyManager::CanEditMembership(const AFablePlayerState* Profile) const
{
    if (Profile && Profile->ActiveExpeditionId.IsValid())
    {
        return false;
    }

    const AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();
    if (Mode &&
        (Mode->IsPartyExpeditionActive(Profile ? Profile->PartyId : FGuid()) || Mode->IsExpeditionParticipant(Profile)))
    {
        return false;
    }

    const auto State = FableAdventure::Field(GetWorld());
    return !State || (State->ActiveRegion == EFableRegion::Village && !State->bPartyRecoveryPending &&
                      !(State->AdventureStage == EFableAdventureStage::Forest && !State->bObjectiveComplete));
}

/** 요청한 플레이어에게만 명확한 실패 사유를 알린다. */
bool AFablePartyManager::Reject(AFablePlayerState* Profile, const FString& Notice) const
{
    if (Profile)
    {
        Profile->SetNotice(Notice);
    }

    return false;
}

/** 만료·해산·권한 변경으로 더 이상 쓸 수 없는 초대를 제거한다. */
void AFablePartyManager::Publish()
{
    Invitations.RemoveAll(
        [&](const FFablePartyInvite& Invite)
        {
            const FFablePartyRecord* Party = Parties.FindByPredicate(
                [&](const FFablePartyRecord& Entry)
                {
                    return Entry.PartyId == Invite.PartyId;
                });
            return !Party || Party->LeaderKey != Invite.InviterKey || Party->Members.Num() >= MaxMembers ||
                   GetPartyForMember(Invite.TargetKey) != nullptr;
        });

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        AFablePlayerState* Profile = It->Get() ? It->Get()->GetPlayerState<AFablePlayerState>() : nullptr;
        if (Profile)
        {
            const FFablePartyRecord* Party = GetPartyForMember(Profile->PartyPlayerKey);
            Profile->PartyId = Party ? Party->PartyId : FGuid();
            Profile->ForceNetUpdate();
        }
    }

    ForceNetUpdate();
}

/** 서버 시각을 초과한 초대와 오프라인 슬롯을 정리한다. */
void AFablePartyManager::ExpireEntries()
{
    if (!HasAuthority())
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();
    bool bChanged = Invitations.RemoveAll(
                        [&](const FFablePartyInvite& Invite)
                        {
                            return Invite.ExpiresAt <= Now;
                        }) > 0;

    for (FFablePartyRecord& Party : Parties)
    {
        if (Party.Members.RemoveAll(
                [&](const FFablePartyMember& Member)
                {
                    return !Member.bConnected && Member.ReconnectDeadline <= Now;
                }) > 0)
        {
            ++Party.Revision;
            ResetReadiness(Party);
            RepairLeader(Party);
            bChanged = true;
        }
    }

    bChanged |= Parties.RemoveAll(
                    [](const FFablePartyRecord& Party)
                    {
                        return Party.Members.IsEmpty();
                    }) > 0;
    if (bChanged)
    {
        Publish();
    }
}

/** 초대 UUID만으로 권한을 얻을 수 없도록 요청자와 현재 명부를 매번 검사한다. */
bool AFablePartyManager::ExecuteAction(APlayerController* Requester, EFablePartyAction Action, int32 TargetPlayerId,
                                       FGuid InviteId)
{
    AFablePlayerState* Profile = ValidatePlayer(Requester);
    if (!Profile || Profile->PartyPlayerKey.IsEmpty() || GetConnectedPlayer(Profile->PartyPlayerKey) != Requester)
    {
        return false;
    }

    // 빠른 중복 패킷은 동일 명부를 반복 수정하지 못한다.
    const double Now = GetWorld()->GetTimeSeconds();
    if (const double* Last = LastRequestTimes.Find(Requester); Last && Now - *Last < .25)
    {
        return Reject(Profile, TEXT("잠시 뒤 다시 요청하세요."));
    }

    LastRequestTimes.Add(Requester, Now);
    ExpireEntries();
    if (!CanEditMembership(Profile))
    {
        return Reject(Profile, TEXT("마을에서 파티를 변경할 수 있습니다."));
    }

    const FString Key = Profile->PartyPlayerKey;
    FFablePartyRecord* Party = const_cast<FFablePartyRecord*>(GetPartyForMember(Key));
    if (Action == EFablePartyAction::Create)
    {
        if (Party)
        {
            return Reject(Profile, TEXT("이미 파티에 소속되어 있습니다."));
        }

        FFablePartyRecord Created;
        Created.PartyId = FGuid::NewGuid();
        Created.LeaderKey = Key;
        FFablePartyMember Member;
        Member.PlayerKey = Key;
        Member.DisplayName = Profile->GetPlayerName().Left(40);
        Member.PlayerId = Profile->GetPlayerId();
        Member.bConnected = true;
        Created.Members.Add(Member);
        Parties.Add(MoveTemp(Created));
        Profile->SetNotice(TEXT("파티를 만들었습니다. G 창에서 동료를 초대하세요."));
    }
    else if (Action == EFablePartyAction::Accept || Action == EFablePartyAction::Decline)
    {
        const FFablePartyInvite* Found = Invitations.FindByPredicate(
            [&](const FFablePartyInvite& Invite)
            {
                return Invite.InviteId == InviteId && Invite.TargetKey == Key && Invite.ExpiresAt > Now;
            });
        if (!Found)
        {
            return Reject(Profile, TEXT("유효한 초대가 아닙니다."));
        }

        const FFablePartyInvite Invite = *Found;
        if (Action == EFablePartyAction::Accept)
        {
            FFablePartyRecord* TargetParty = Parties.FindByPredicate(
                [&](const FFablePartyRecord& Entry)
                {
                    return Entry.PartyId == Invite.PartyId && Entry.LeaderKey == Invite.InviterKey;
                });
            if (Party || !TargetParty || TargetParty->Members.Num() >= MaxMembers)
            {
                return Reject(Profile, TEXT("파티가 가득 찼거나 초대 상태가 바뀌었습니다."));
            }

            FFablePartyMember Member;
            Member.PlayerKey = Key;
            Member.DisplayName = Profile->GetPlayerName().Left(40);
            Member.PlayerId = Profile->GetPlayerId();
            Member.bConnected = true;
            TargetParty->Members.Add(Member);
            ++TargetParty->Revision;
            ResetReadiness(*TargetParty);
        }

        Invitations.RemoveAll(
            [&](const FFablePartyInvite& Entry)
            {
                return Entry.InviteId == InviteId;
            });
        Profile->SetNotice(Action == EFablePartyAction::Accept ? TEXT("파티에 합류했습니다.")
                                                               : TEXT("초대를 거절했습니다."));
    }
    else if (Action == EFablePartyAction::Leave)
    {
        if (!Party)
        {
            return Reject(Profile, TEXT("소속된 파티가 없습니다."));
        }

        Party->Members.RemoveAll(
            [&](const FFablePartyMember& Member)
            {
                return Member.PlayerKey == Key;
            });
        ++Party->Revision;
        ResetReadiness(*Party);
        RepairLeader(*Party);
        Parties.RemoveAll(
            [](const FFablePartyRecord& Entry)
            {
                return Entry.Members.IsEmpty();
            });
        Profile->SetNotice(TEXT("파티에서 탈퇴했습니다."));
    }
    else if (Action == EFablePartyAction::Invite || Action == EFablePartyAction::TransferLeader)
    {
        if (!Party || Party->LeaderKey != Key)
        {
            return Reject(Profile, TEXT("파티장만 요청할 수 있습니다."));
        }

        APlayerController* Target = FindPlayer(TargetPlayerId);
        const AFablePlayerState* Other = ValidatePlayer(Target);
        if (!Other || Target == Requester || Other->PartyPlayerKey.IsEmpty())
        {
            return Reject(Profile, TEXT("현재 접속한 다른 모험가를 선택하세요."));
        }

        if (Action == EFablePartyAction::TransferLeader)
        {
            if (GetPartyForMember(Other->PartyPlayerKey) != Party)
            {
                return Reject(Profile, TEXT("같은 파티의 접속 중 동료에게만 위임할 수 있습니다."));
            }

            Party->LeaderKey = Other->PartyPlayerKey;
            ++Party->Revision;
            ResetReadiness(*Party);
            Profile->SetNotice(TEXT("파티장을 위임했습니다."));
        }
        else
        {
            if (Party->Members.Num() >= MaxMembers || GetPartyForMember(Other->PartyPlayerKey))
            {
                return Reject(Profile, TEXT("정원이 찼거나 상대가 이미 파티에 소속되어 있습니다."));
            }

            if (GetPendingInviteFor(Other->PartyPlayerKey))
            {
                return Reject(Profile, TEXT("상대에게 아직 유효한 초대가 있습니다."));
            }

            FFablePartyInvite Invite;
            Invite.InviteId = FGuid::NewGuid();
            Invite.PartyId = Party->PartyId;
            Invite.InviterKey = Key;
            Invite.TargetKey = Other->PartyPlayerKey;
            Invite.ExpiresAt = float(Now) + InviteLifetime;
            Invitations.Add(Invite);
            Profile->SetNotice(TEXT("초대를 보냈습니다."));
        }
    }
    else
    {
        return Reject(Profile, TEXT("알 수 없는 파티 요청입니다."));
    }

    Publish();
    return true;
}

/** 명부나 선택이 바뀌면 이전 선택에 대한 동의를 재사용하지 않는다. */
void AFablePartyManager::ResetReadiness(FFablePartyRecord& Party)
{
    Party.PreparationRevision = Party.PreparationRevision < MAX_int32 ? Party.PreparationRevision + 1 : 1;

    for (FFablePartyMember& Member : Party.Members)
    {
        Member.ReadyRevision = 0;
    }
}

/** 준비 상태 초기화는 서버의 원정 종료 경로만 호출한다. */
void AFablePartyManager::InvalidatePreparation(FGuid PartyId)
{
    if (!HasAuthority())
    {
        return;
    }

    for (FFablePartyRecord& Party : Parties)
    {
        if (Party.PartyId == PartyId)
        {
            ResetReadiness(Party);
            Publish();
            return;
        }
    }
}

/** 이전 접속 객체를 배제하고 실제로 인증된 현재 연결만 반환한다. */
APlayerController* AFablePartyManager::GetConnectedPlayer(const FString& PlayerKey) const
{
    const TWeakObjectPtr<APlayerController>* Current = CurrentConnections.Find(PlayerKey);
    APlayerController* Player = Current ? Current->Get() : nullptr;
    const AFablePlayerState* Profile = ValidatePlayer(Player);
    return Profile && Profile->PartyPlayerKey == PlayerKey ? Player : nullptr;
}

/** 클라이언트가 알고 있던 파티·선택 리비전을 서버 정본과 비교한다. */
FFablePartyRecord* AFablePartyManager::ValidatePreparation(APlayerController* Requester, FGuid PartyId,
                                                           int32 ExpectedRevision, bool bLeaderOnly)
{
    // 이전 접속 객체와 저장 중인 프로필은 새 동의를 제출하지 못한다.
    AFablePlayerState* Profile = ValidatePlayer(Requester);
    if (!Profile || Profile->PartyPlayerKey.IsEmpty() || GetConnectedPlayer(Profile->PartyPlayerKey) != Requester)
    {
        return nullptr;
    }
    if (Profile->IsBackendSavePending())
    {
        Reject(Profile, TEXT("캐릭터 저장을 마친 뒤 다시 요청하세요."));
        return nullptr;
    }

    // UUID와 준비 리비전 모두 서버의 현재 정본과 일치해야 한다.
    ExpireEntries();
    FFablePartyRecord* Party = const_cast<FFablePartyRecord*>(GetPartyForMember(Profile->PartyPlayerKey));
    if (!Party || !PartyId.IsValid() || Party->PartyId != PartyId || Profile->PartyId != PartyId)
    {
        Reject(Profile, TEXT("현재 파티 정보와 요청한 파티가 일치하지 않습니다."));
        return nullptr;
    }
    if (Party->PreparationRevision != ExpectedRevision)
    {
        Reject(Profile, TEXT("파티 준비 상태가 바뀌었습니다. 새 상태를 확인하세요."));
        return nullptr;
    }
    if (!CanEditMembership(Profile) || Profile->ActiveExpeditionId.IsValid())
    {
        Reject(Profile, TEXT("원정 중에는 파티 준비를 변경할 수 없습니다."));
        return nullptr;
    }
    if (bLeaderOnly && Party->LeaderKey != Profile->PartyPlayerKey)
    {
        Reject(Profile, TEXT("파티장만 원정을 선택하거나 시작할 수 있습니다."));
        return nullptr;
    }

    return Party;
}

/** 같은 선택을 반복한 요청은 다른 구성원의 동의를 지우지 않는다. */
bool AFablePartyManager::SelectExpedition(APlayerController* Requester, FGuid PartyId,
                                          int32 ExpectedPreparationRevision, EFableRegion Region,
                                          EFableDifficulty Difficulty)
{
    FFablePartyRecord* Party = ValidatePreparation(Requester, PartyId, ExpectedPreparationRevision, true);
    if (!Party)
    {
        return false;
    }
    if ((Region != EFableRegion::RootRuins && Region != EFableRegion::Moonwell) ||
        (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard))
    {
        return Reject(ValidatePlayer(Requester), TEXT("유효한 던전과 난이도를 선택하세요."));
    }

    if (Party->SelectedRegion != Region || Party->SelectedDifficulty != Difficulty)
    {
        Party->SelectedRegion = Region;
        Party->SelectedDifficulty = Difficulty;
        ResetReadiness(*Party);
    }

    ValidatePlayer(Requester)->SetNotice(TEXT("원정 목적지를 선택했습니다. 모두 준비를 확인하세요."));
    Publish();
    return true;
}

/** 준비 확인 자체는 선택 리비전을 변경하지 않는 멱등 요청이다. */
bool AFablePartyManager::SetExpeditionReady(APlayerController* Requester, FGuid PartyId,
                                            int32 ExpectedPreparationRevision, bool bReady)
{
    FFablePartyRecord* Party = ValidatePreparation(Requester, PartyId, ExpectedPreparationRevision, false);
    AFablePlayerState* Profile = ValidatePlayer(Requester);
    if (!Party || !Profile)
    {
        return false;
    }

    // 초대 수락으로 명부가 바뀔 수 있는 동안에는 새 동의를 확정하지 않는다.
    if (bReady && Invitations.ContainsByPredicate(
                      [&](const FFablePartyInvite& Invite)
                      {
                          return Invite.PartyId == PartyId;
                      }))
    {
        return Reject(Profile, TEXT("남아 있는 초대를 먼저 처리한 뒤 준비하세요."));
    }

    FFablePartyMember* Member = Party->Members.FindByPredicate(
        [&](const FFablePartyMember& Entry)
        {
            return Entry.PlayerKey == Profile->PartyPlayerKey;
        });
    const AFableCharacter* Hero = Requester->GetPawn<AFableCharacter>();
    if (!Member || !Member->bConnected || GetConnectedPlayer(Member->PlayerKey) != Requester ||
        (bReady &&
         (!Hero || !Hero->IsAlive() || Hero->GetController() != Requester ||
          Hero->GetPlayerState<AFablePlayerState>() != Profile || !Profile->IsRegionEligible(Party->SelectedRegion) ||
          !FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation()))))
    {
        return Reject(Profile, TEXT("마을에서 생존 상태와 해당 던전 의뢰를 확인한 뒤 준비하세요."));
    }

    Member->ReadyRevision = bReady ? Party->PreparationRevision : 0;
    Profile->SetNotice(bReady ? TEXT("현재 원정 선택에 준비했습니다.") : TEXT("원정 준비를 취소했습니다."));
    Publish();
    return true;
}

/** 완성된 명부의 모든 현재 연결과 동의를 한 번 더 검사한다. */
bool AFablePartyManager::BuildReadySnapshot(APlayerController* Requester, FGuid PartyId,
                                            int32 ExpectedPreparationRevision, FFableExpeditionTicket& OutTicket)
{
    // 완료되지 않은 초대와 정원을 먼저 검사해 입장 도중 명부가 늘어나지 않도록 한다.
    OutTicket = FFableExpeditionTicket();
    FFablePartyRecord* Party = ValidatePreparation(Requester, PartyId, ExpectedPreparationRevision, true);
    AFablePlayerState* Profile = ValidatePlayer(Requester);
    if (!Party || !Profile)
    {
        return false;
    }

    if (Party->Members.IsEmpty() || Party->Members.Num() > MaxMembers ||
        Invitations.ContainsByPredicate(
            [&](const FFablePartyInvite& Invite)
            {
                return Invite.PartyId == PartyId;
            }))
    {
        return Reject(Profile, TEXT("파티 명부와 남아 있는 초대를 먼저 확인하세요."));
    }

    // 파티 UI의 준비 표시는 신뢰하지 않고 전원의 현재 연결·의뢰·생존을 재검사한다.
    TSet<FString> UniqueKeys;
    for (const FFablePartyMember& Member : Party->Members)
    {
        APlayerController* Player = GetConnectedPlayer(Member.PlayerKey);
        const AFablePlayerState* Other = ValidatePlayer(Player);
        const AFableCharacter* Hero = Player ? Player->GetPawn<AFableCharacter>() : nullptr;
        if (!Member.bConnected || !Other || !Hero || !Hero->IsAlive() || Hero->GetController() != Player ||
            Hero->GetPlayerState<AFablePlayerState>() != Other || Other->PartyId != PartyId ||
            Other->IsBackendSavePending() || Other->ActiveExpeditionId.IsValid() ||
            !Other->IsRegionEligible(Party->SelectedRegion) ||
            !FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation()) || Member.PlayerKey.IsEmpty() ||
            UniqueKeys.Contains(Member.PlayerKey))
        {
            return Reject(Profile, TEXT("모든 파티원이 접속·생존·저장·던전 의뢰 조건을 만족해야 합니다."));
        }
        if (Member.ReadyRevision != Party->PreparationRevision)
        {
            return Reject(Profile, TEXT("모든 파티원이 현재 선택에 준비해야 합니다."));
        }

        UniqueKeys.Add(Member.PlayerKey);
        OutTicket.MemberKeys.Add(Member.PlayerKey);
    }

    // 전원 동의가 확인된 뒤에만 서버가 새 원정 UUID와 불변 키 명부를 발급한다.
    OutTicket.ExpeditionId = FGuid::NewGuid();
    OutTicket.PartyId = PartyId;
    OutTicket.Region = Party->SelectedRegion;
    OutTicket.Difficulty = Party->SelectedDifficulty;
    OutTicket.PreparationRevision = Party->PreparationRevision;
    return true;
}

/** 실제 서버 GameMode만 이동·입장 잠금·전투 시작을 확정한다. */
bool AFablePartyManager::StartExpedition(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision)
{
    if (!ValidatePlayer(Requester))
    {
        return false;
    }

    AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();
    return Mode ? Mode->RequestStartExpedition(Requester, PartyId, ExpectedPreparationRevision)
                : Reject(ValidatePlayer(Requester), TEXT("원정 서버가 아직 준비되지 않았습니다."));
}
