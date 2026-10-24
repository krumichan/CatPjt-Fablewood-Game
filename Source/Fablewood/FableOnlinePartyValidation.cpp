#include "FableOnlineValidationDriver.h"

#include "FablePartyManager.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** 다른 실제 프로세스가 발행한 공개 검증 보고서만 읽는다. */
    TSharedPtr<FJsonObject> ReadPartyReport(const FString& Path)
    {
        FString Bytes;
        TSharedPtr<FJsonObject> Result;

        if (!FFileHelper::LoadFileToString(Bytes, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Bytes), Result))
        {
            return nullptr;
        }

        return Result;
    }

    /** 비동기 보고서에 이름 붙인 성공 조건이 존재하는지 검사한다. */
    bool PartyCheck(const TSharedPtr<FJsonObject>& Report, const FString& Name)
    {
        const TSharedPtr<FJsonObject>* Checks = nullptr;
        bool Passed = false;

        return Report && Report->TryGetObjectField(TEXT("checks"), Checks) &&
               (*Checks)->TryGetBoolField(Name, Passed) && Passed;
    }

    /** 명부에 접속 중인 구성원의 수를 계산한다. */
    int32 ConnectedPartyMembers(const FFablePartyRecord* Party)
    {
        int32 Connected = 0;

        if (Party)
        {
            for (const auto& Member : Party->Members)
            {
                Connected += Member.bConnected ? 1 : 0;
            }
        }

        return Connected;
    }
}

/** 권한 서버의 공개 명부를 캐릭터 식별자와 함께 기록한다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::PartyServerSnapshot(UWorld* World) const
{
    auto Result = MakeShared<FJsonObject>();
    auto* Manager = AFablePartyManager::Find(World);
    TArray<TSharedPtr<FJsonValue>> Parties;

    if (Manager)
    {
        for (const auto& Party : Manager->Parties)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("partyId"), Party.PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
            Row->SetStringField(TEXT("leaderKey"), Party.LeaderKey);
            Row->SetNumberField(TEXT("revision"), Party.Revision);
            TArray<TSharedPtr<FJsonValue>> Members;

            for (const auto& Member : Party.Members)
            {
                auto Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("playerKey"), Member.PlayerKey);
                Entry->SetNumberField(TEXT("playerId"), Member.PlayerId);
                Entry->SetBoolField(TEXT("connected"), Member.bConnected);
                Members.Add(MakeShared<FJsonValueObject>(Entry));
            }

            Row->SetArrayField(TEXT("members"), Members);
            Parties.Add(MakeShared<FJsonValueObject>(Row));
        }
    }

    Result->SetArrayField(TEXT("parties"), Parties);
    Result->SetNumberField(TEXT("controllers"), World ? World->GetNumPlayerControllers() : 0);
    return Result;
}

/** 구성 변경 후 최종 파티의 연결 종료와 같은 캐릭터 복귀를 서버에서 확인한다. */
void UFableOnlineValidationDriver::ObservePartyServer(UWorld* World)
{
    auto* Manager = AFablePartyManager::Find(World);
    auto Client = ReadPartyReport(FPaths::Combine(Directory, TEXT("client2.json")));
    const TSharedPtr<FJsonObject>* ClientParty = nullptr;
    FString FinalId;

    if (!Manager || !Client || !Client->TryGetObjectField(TEXT("partyStage1"), ClientParty) ||
        !(*ClientParty)->TryGetStringField(TEXT("finalPartyId"), FinalId) || FinalId.IsEmpty())
    {
        return;
    }

    for (const auto& Party : Manager->Parties)
    {
        if (Party.PartyId.ToString(EGuidFormats::DigitsWithHyphensLower) != FinalId || Party.Members.Num() != 2)
        {
            continue;
        }

        const int32 Connected = ConnectedPartyMembers(&Party);

        if (!ServerPartyBefore && Connected == 2)
        {
            ServerPartyBefore = PartyServerSnapshot(World);
            Checks.Add(TEXT("server_party_before_disconnect"), true);
        }

        if (ServerPartyBefore && !ServerPartySolo && Connected == 1)
        {
            ServerPartySolo = PartyServerSnapshot(World);
            Checks.Add(TEXT("server_party_disconnected_member_reserved"), true);
        }

        if (ServerPartySolo && !ServerPartyRejoined && Connected == 2)
        {
            ServerPartyRejoined = PartyServerSnapshot(World);
            Checks.Add(TEXT("server_party_same_character_restored"), true);
        }
    }
}

/** 파티 창의 일반 UI 함수로 요청하고 복제된 서버 상태와 상대 클라이언트를 기다린다. */
bool UFableOnlineValidationDriver::TickParty(AFablePlayerController* PC, AFablePlayerState* P, double Now)
{
    auto* World = PC ? PC->GetWorld() : nullptr;
    auto* Manager = AFablePartyManager::Find(World);

    if (!PC || !P || !Manager || !P->IsBackendReady() || P->IsBackendSavePending())
    {
        return false;
    }

    if (!PC->IsPartyOpen())
    {
        PC->UIToggleParty();
    }

    if (!PartyReport)
    {
        PartyReport = MakeShared<FJsonObject>();
        Capture(TEXT("party-panel"));

        // 同じフレームの完了キャプチャで初回の要求を上書きしない。
        return false;
    }

    PartySelfKey = P->PartyPlayerKey;

    const auto* Party = Manager->GetPartyForMember(P->GetPlayerId());
    const auto* Invite = Manager->GetPendingInviteFor(P->GetPlayerId());
    AFablePlayerState* Peer = nullptr;

    if (auto* GS = World->GetGameState())
    {
        for (APlayerState* State : GS->PlayerArray)
        {
            auto* Candidate = Cast<AFablePlayerState>(State);

            if (Candidate && Candidate != P && !Candidate->PartyPlayerKey.IsEmpty())
            {
                Peer = Candidate;
                PartyPeerKey = Peer->PartyPlayerKey;
                break;
            }
        }
    }

    const FString PeerRole = Role == TEXT("client1") ? TEXT("client2") : TEXT("client1");
    auto PeerReport = ReadPartyReport(FPaths::Combine(Directory, PeerRole + TEXT(".json")));
    const bool Both = Party && Party->Members.Num() == 2 && ConnectedPartyMembers(Party) == 2;
    const bool Singleton = Party && Party->Members.Num() == 1;
    const bool Leader = Party && Party->LeaderKey == P->PartyPlayerKey;

    PartyReport->SetNumberField(TEXT("step"), PartyStep);
    PartyReport->SetStringField(TEXT("selfKey"), PartySelfKey);
    PartyReport->SetStringField(TEXT("peerKey"), PartyPeerKey);
    PartyReport->SetStringField(TEXT("finalPartyId"), PartyFinalId);
    PartyReport->SetObjectField(TEXT("snapshot"), PartyServerSnapshot(World));

    auto Advance = [this](int32 Next)
    {
        PartyStep = Next;
        PartyActionAt = -1;
    };

    auto Send = [this, Now](auto&& Action)
    {
        if (PartyActionAt < 0 && Now - PartyLastSentAt > .5)
        {
            Action();
            PartyActionAt = Now;
            PartyLastSentAt = Now;
            return true;
        }

        return false;
    };

    // 같은 캐릭터의 재접속은 초대를 다시 수락하지 않고 동일한 파티로 복원되어야 한다.
    if (bResumeExpedition)
    {
        FString ExpectedParty;

        if (!Peer || PartySelfKey.IsEmpty() || PartyPeerKey.IsEmpty() || !Expected ||
            !Expected->TryGetStringField(TEXT("partyId"), ExpectedParty) || !Both || !Party ||
            Party->PartyId.ToString(EGuidFormats::DigitsWithHyphensLower) != ExpectedParty || Leader)
        {
            return false;
        }

        PartyFinalId = ExpectedParty;
        PartyReport->SetStringField(TEXT("finalPartyId"), PartyFinalId);
        PartyReport->SetBoolField(TEXT("restored"), true);
        const bool AlreadyReported = Checks.FindRef(TEXT("party_same_character_restored"));
        Checks.Add(TEXT("party_same_character_restored"), true);

        // 복귀자는 먼저 자신의 실제 복제 관측을 발행하고 남은 동료의 관측을 기다린다.
        if (!AlreadyReported)
        {
            Publish(TEXT("running"));
            LastReport = Now;
        }

        auto Survivor = ReadPartyReport(FPaths::Combine(FPaths::GetPath(Directory), TEXT("register/client1.json")));
        auto Server = ReadPartyReport(FPaths::Combine(FPaths::GetPath(Directory), TEXT("register/server.json")));

        if (!PartyCheck(Survivor, TEXT("party_same_character_restored")) ||
            !PartyCheck(Server, TEXT("server_party_same_character_restored")))
        {
            return false;
        }

        Checks.Add(TEXT("party_restore_handshake_complete"), true);
        PartyReport->SetBoolField(TEXT("completed"), true);
        Publish(TEXT("running"));
        LastReport = Now;
        return true;
    }

    if (Role == TEXT("client1"))
    {
        switch (PartyStep)
        {
        case 0:
            if (!Peer || PartySelfKey.IsEmpty())
            {
                return false;
            }

            Send(
                [PC]
                {
                    PC->UICreateParty();
                });

            if (Singleton && Leader)
            {
                Checks.Add(TEXT("party_created"), true);
                Advance(1);
            }
            break;

        case 1:
            if (Peer)
            {
                Send(
                    [PC, Peer]
                    {
                        PC->UIInviteParty(Peer->GetPlayerId());
                    });
            }

            if (PartyCheck(PeerReport, TEXT("party_invite_declined")))
            {
                Checks.Add(TEXT("party_invite_declined"), true);
                Advance(2);
            }
            break;

        case 2:
            if (Peer)
            {
                Send(
                    [PC, Peer]
                    {
                        PC->UIInviteParty(Peer->GetPlayerId());
                    });
            }

            if (Both)
            {
                Checks.Add(TEXT("party_invite_accepted"), true);
                Capture(TEXT("party-two-members"));
                Advance(3);
            }
            break;

        case 3:
            if (PartyCheck(PeerReport, TEXT("party_member_transfer_rejected")) &&
                PartyCheck(PeerReport, TEXT("party_duplicate_create_rejected")) &&
                PartyCheck(PeerReport, TEXT("party_invite_replay_rejected")))
            {
                Checks.Add(TEXT("party_invalid_requests_rejected"), true);
                Advance(4);
            }
            break;

        case 4:
            if (Peer)
            {
                Send(
                    [PC, Peer]
                    {
                        PC->UITransferPartyLeader(Peer->GetPlayerId());
                    });
            }

            if (Both && !Leader)
            {
                Checks.Add(TEXT("party_leader_transferred"), true);
                Advance(5);
            }
            break;

        case 5:
            Send(
                [PC]
                {
                    PC->UILeaveParty();
                });

            if (!Party)
            {
                Checks.Add(TEXT("party_left"), true);
                Advance(6);
            }
            break;

        case 6:
            if (Invite)
            {
                const FGuid Id = Invite->InviteId;
                Send(
                    [PC, Id]
                    {
                        PC->UIRespondPartyInvite(Id, true);
                    });
            }

            if (Both)
            {
                Checks.Add(TEXT("party_rejoined_after_leave"), true);
                Advance(7);
            }
            break;

        case 7:
            if (PartyCheck(PeerReport, TEXT("party_rejoined_after_leave")))
            {
                Send(
                    [PC]
                    {
                        PC->UILeaveParty();
                    });
            }

            if (!Party && PartyCheck(PeerReport, TEXT("party_dissolved")))
            {
                Checks.Add(TEXT("party_dissolved"), true);
                Advance(8);
            }
            break;

        case 8:
            if (Invite)
            {
                const FGuid Id = Invite->InviteId;
                Send(
                    [PC, Id]
                    {
                        PC->UIRespondPartyInvite(Id, true);
                    });
            }

            if (Both && !Leader)
            {
                PartyFinalId = Party->PartyId.ToString(EGuidFormats::DigitsWithHyphensLower);
                Checks.Add(TEXT("party_final_roster_ready"), true);
                Capture(TEXT("party-before-disconnect"));
                Advance(9);
            }
            break;

        case 9:
            if (Party && Party->Members.Num() == 2 && ConnectedPartyMembers(Party) == 1 && Leader)
            {
                Checks.Add(TEXT("party_disconnected_leader_reserved"), true);
                Checks.Add(TEXT("party_disconnect_leader_failover"), true);
                Capture(TEXT("party-disconnected"));
                Advance(10);
            }
            break;

        case 10:
        {
            auto Resumed =
                ReadPartyReport(FPaths::Combine(FPaths::GetPath(Directory), TEXT("register-resume/client2.json")));
            auto Server = ReadPartyReport(FPaths::Combine(Directory, TEXT("server.json")));

            if (Both && Leader && PartyCheck(Resumed, TEXT("party_same_character_restored")) &&
                PartyCheck(Server, TEXT("server_party_same_character_restored")))
            {
                const bool AlreadyReported = Checks.FindRef(TEXT("party_same_character_restored"));
                Checks.Add(TEXT("party_same_character_restored"), true);
                PartyReport->SetBoolField(TEXT("completed"), true);

                // 서로와 서버의 관측이 확정되기 전에는 안전 이탈 단계를 시작하지 않는다.
                if (!AlreadyReported)
                {
                    Publish(TEXT("running"));
                    LastReport = Now;
                }

                if (PartyCheck(Resumed, TEXT("party_restore_handshake_complete")))
                {
                    Checks.Add(TEXT("party_restore_handshake_complete"), true);
                    return true;
                }
            }
            break;
        }
        }
    }
    else
    {
        switch (PartyStep)
        {
        case 0:
            if (Invite)
            {
                const FGuid Id = Invite->InviteId;
                Send(
                    [PC, P, Id, this]
                    {
                        // 招待の消失だけで成功にせず、この要求に対する承認通知も確認する。
                        PartyNoticeBefore = P->NoticeSerial;
                        PC->UIRespondPartyInvite(Id, false);
                    });
            }

            if (PartyActionAt >= 0 && !Invite && !Party && P->NoticeSerial > PartyNoticeBefore &&
                P->LastNotice == TEXT("초대를 거절했습니다."))
            {
                Checks.Add(TEXT("party_invite_declined"), true);
                Advance(1);
            }
            break;

        case 1:
            if (Invite)
            {
                PartyAcceptedInvite = Invite->InviteId;
                Send(
                    [PC, this]
                    {
                        PC->UIRespondPartyInvite(PartyAcceptedInvite, true);
                    });
            }

            if (Both && !Leader)
            {
                Checks.Add(TEXT("party_invite_accepted"), true);
                Advance(2);
            }
            break;

        case 2:
        case 3:
        case 4:
            if (!Both || Leader || !Peer)
            {
                return false;
            }

            if (PartyActionAt < 0)
            {
                PartyNoticeBefore = P->NoticeSerial;
                Send(
                    [PC, Peer, this]
                    {
                        if (PartyStep == 2)
                        {
                            PC->UITransferPartyLeader(Peer->GetPlayerId());
                        }
                        else if (PartyStep == 3)
                        {
                            PC->UICreateParty();
                        }
                        else
                        {
                            PC->UIRespondPartyInvite(PartyAcceptedInvite, true);
                        }
                    });
            }

            if (PartyActionAt >= 0 && Now - PartyActionAt > .4 && P->NoticeSerial > PartyNoticeBefore && !Leader &&
                Both && Manager->Parties.Num() == 1 &&
                P->LastNotice == (PartyStep == 2   ? TEXT("파티장만 요청할 수 있습니다.")
                                  : PartyStep == 3 ? TEXT("이미 파티에 소속되어 있습니다.")
                                                   : TEXT("유효한 초대가 아닙니다.")))
            {
                Checks.Add(PartyStep == 2   ? TEXT("party_member_transfer_rejected")
                           : PartyStep == 3 ? TEXT("party_duplicate_create_rejected")
                                            : TEXT("party_invite_replay_rejected"),
                           true);
                Advance(PartyStep + 1);
            }
            break;

        case 5:
            if (Leader && Singleton)
            {
                Checks.Add(TEXT("party_leader_transferred"), true);
                Checks.Add(TEXT("party_member_departure_observed"), true);
                Advance(6);
            }
            break;

        case 6:
            if (Peer)
            {
                Send(
                    [PC, Peer]
                    {
                        PC->UIInviteParty(Peer->GetPlayerId());
                    });
            }

            if (Both && PartyCheck(PeerReport, TEXT("party_rejoined_after_leave")))
            {
                Checks.Add(TEXT("party_rejoined_after_leave"), true);
                Advance(7);
            }
            break;

        case 7:
            if (Singleton)
            {
                Send(
                    [PC]
                    {
                        PC->UILeaveParty();
                    });
            }

            if (PartyActionAt >= 0 && !Party)
            {
                Checks.Add(TEXT("party_dissolved"), true);
                Advance(8);
            }
            break;

        case 8:
            Send(
                [PC]
                {
                    PC->UICreateParty();
                });

            if (Singleton && Leader)
            {
                Advance(9);
            }
            break;

        case 9:
            if (Peer)
            {
                Send(
                    [PC, Peer]
                    {
                        PC->UIInviteParty(Peer->GetPlayerId());
                    });
            }

            if (Both && Leader && PartyCheck(PeerReport, TEXT("party_final_roster_ready")))
            {
                PartyFinalId = Party->PartyId.ToString(EGuidFormats::DigitsWithHyphensLower);
                PartyReport->SetStringField(TEXT("finalPartyId"), PartyFinalId);
                PartyReport->SetBoolField(TEXT("completed"), true);
                Checks.Add(TEXT("party_final_roster_ready"), true);
                return true;
            }
            break;
        }
    }

    return false;
}
