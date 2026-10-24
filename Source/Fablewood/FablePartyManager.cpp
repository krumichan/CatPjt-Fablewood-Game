#include "FablePartyManager.h"
#include "FableCharacter.h"
#include "FableGameState.h"
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
    CurrentConnections.Add(PublicKey, Player);
    ExpireEntries();
    for (FFablePartyRecord& Party : Parties)
    {
        for (FFablePartyMember& Member : Party.Members)
        {
            if (Member.PlayerKey == PublicKey)
            {
                Member.PlayerId = Profile->GetPlayerId();
                Member.bConnected = true;
                Member.ReconnectDeadline = 0;
                ++Party.Revision;
                Profile->SetNotice(TEXT("기존 파티에 다시 합류했습니다."));
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

/** 1단계에서는 기존 전투가 진행 중이면 명부 변경을 막는다. */
bool AFablePartyManager::CanEditMembership() const
{
    const AFableGameState* State = GetWorld()->GetGameState<AFableGameState>();
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
    if (!Profile || Profile->PartyPlayerKey.IsEmpty())
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
    if (!CanEditMembership())
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
