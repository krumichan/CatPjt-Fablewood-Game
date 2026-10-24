#include "FableOnlineValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FablePartyManager.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FableAdventure.h"
#include "FablePartyEncounter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    /** 다른 실제 프로세스의 공개 보고서만 읽어 진행 순서를 맞춘다. */
    TSharedPtr<FJsonObject> ReadExpeditionReport(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> Report;
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Report))
        {
            return nullptr;
        }

        return Report;
    }

    /** 누락된 관찰값을 성공으로 간주하지 않는다. */
    bool ExpeditionCheck(const TSharedPtr<FJsonObject>& Report, const FString& Name)
    {
        const TSharedPtr<FJsonObject>* Values = nullptr;
        bool Value = false;
        return Report && Report->TryGetObjectField(TEXT("checks"), Values) && (*Values)->TryGetBoolField(Name, Value) &&
               Value;
    }

    /** 준비 단계가 끝난 뒤 입구에서 떨어진 두 대기 지점을 사용한다. */
    FVector ExpeditionSafeLocation(const AFableCharacter* Hero)
    {
        const auto View = FableAdventure::For(Hero);
        const FVector Canonical =
            Hero && Hero->GetHeroClass() == EFableClass::Archer ? FVector(6480, -1700, 100) : FVector(6300, -1580, 100);
        return View.ToWorld(EFableRegion::RootRuins, Canonical);
    }
}

/** 서버 참가 증서와 정확히 일치하는 두 플레이어의 실제 도착을 보존한다. */
void UFableOnlineValidationDriver::ObserveExpeditionServer(UWorld* World)
{
    if (ServerExpeditionArrival || !World || World->GetNetMode() != NM_DedicatedServer)
    {
        return;
    }

    const auto* ServerMode = World->GetAuthGameMode<AFableGameMode>();
    const AFablePartyEncounter* Encounter = nullptr;
    if (ServerMode)
    {
        for (const auto* Candidate : ServerMode->GetExpeditions())
        {
            if (IsValid(Candidate) && Candidate->IsCommitted() && Candidate->GetTicket().MemberKeys.Num() == 2)
            {
                Encounter = Candidate;
                break;
            }
        }
    }
    const auto Progress = FableAdventure::For(Encounter);
    if (!ServerMode || !Encounter || !Progress)
    {
        return;
    }

    const auto& Ticket = Encounter->GetTicket();
    if (!Ticket.IsValid() || Ticket.MemberKeys.Num() != 2 || Ticket.MemberKeys[0] == Ticket.MemberKeys[1] ||
        Progress->ActiveRegion != Ticket.Region || Progress->ActiveDifficulty != Ticket.Difficulty)
    {
        return;
    }

    TArray<TSharedPtr<FJsonValue>> Members;
    TSet<FString> Observed;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const auto* PC = It->Get();
        const auto* ProfileState = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        const auto* Hero = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;
        if (!ProfileState || !Hero || PC->IsLocalController() || !Hero->IsAlive() || !ProfileState->IsBackendReady() ||
            ProfileState->IsBackendSavePending() || !ProfileState->bLastSaveSucceeded ||
            ProfileState->GetBackendRevision() != ProfileState->GetBackendAcknowledgedRevision() ||
            ProfileState->ActiveExpeditionId != Ticket.ExpeditionId || ProfileState->PartyId != Ticket.PartyId ||
            !Ticket.Contains(ProfileState->PartyPlayerKey) ||
            !Progress.IsInRegion(Ticket.Region, Hero->GetActorLocation()))
        {
            return;
        }

        auto Member = Profile(ProfileState);
        Member->SetStringField(TEXT("playerKey"), ProfileState->PartyPlayerKey);
        Member->SetStringField(TEXT("expeditionId"),
                               ProfileState->ActiveExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
        Member->SetBoolField(TEXT("arrived"), true);
        Observed.Add(ProfileState->PartyPlayerKey);
        Members.Add(MakeShared<FJsonValueObject>(Member));
    }

    if (Members.Num() != 2 || Observed.Num() != 2 || !Observed.Contains(Ticket.MemberKeys[0]) ||
        !Observed.Contains(Ticket.MemberKeys[1]))
    {
        return;
    }

    ServerExpeditionArrival = MakeShared<FJsonObject>();
    ServerExpeditionArrival->SetStringField(TEXT("expeditionId"),
                                            Ticket.ExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    ServerExpeditionArrival->SetStringField(TEXT("partyId"),
                                            Ticket.PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
    ServerExpeditionArrival->SetNumberField(TEXT("region"), int32(Ticket.Region));
    ServerExpeditionArrival->SetNumberField(TEXT("difficulty"), int32(Ticket.Difficulty));
    ServerExpeditionArrival->SetNumberField(TEXT("preparationRevision"), Ticket.PreparationRevision);
    ServerExpeditionArrival->SetNumberField(TEXT("encounterSerial"), Progress->EncounterSerial);
    ServerExpeditionArrival->SetNumberField(TEXT("defeatedEnemies"), Progress->DefeatedEnemies);
    ServerExpeditionArrival->SetArrayField(TEXT("members"), Members);
    TArray<TSharedPtr<FJsonValue>> Keys;
    for (const auto& Key : Ticket.MemberKeys)
    {
        Keys.Add(MakeShared<FJsonValueString>(Key));
    }
    ServerExpeditionArrival->SetArrayField(TEXT("memberKeys"), Keys);
    Checks.Add(TEXT("server_exact_expedition_arrival"), true);
}

/** 실제 파티 UI와 준비 동의 절차를 거쳐 서버의 입장 결과를 관찰한다. */
bool UFableOnlineValidationDriver::TickExpeditionPreparation(AFablePlayerController* PC, AFablePlayerState* P,
                                                             AFableCharacter* Hero, double Now)
{
    auto* World = Hero ? Hero->GetWorld() : nullptr;
    auto* Manager = AFablePartyManager::Find(World);
    auto* RootState = World ? World->GetGameState<AFableGameState>() : nullptr;
    const auto Progress = FableAdventure::For(P);
    if (!PC || !P || !Hero || !Manager || !RootState || !Progress)
    {
        return false;
    }

    const bool Leader = Role == TEXT("client1"), Extended = Scenario == TEXT("party-entry");
    const FString PeerRole = Leader ? TEXT("client2") : TEXT("client1");
    auto PeerReport = ReadExpeditionReport(FPaths::Combine(Directory, PeerRole + TEXT(".json")));
    auto ServerReport = ReadExpeditionReport(FPaths::Combine(Directory, TEXT("server.json")));
    const auto PeerHas = [&](const FString& Name)
    {
        return ExpeditionCheck(PeerReport, Name);
    };
    const auto Advance = [&](int32 Next)
    {
        ExpeditionStep = Next;
        bExpeditionRequestSent = false;
        ExpeditionReport->SetNumberField(TEXT("step"), Next);
    };
    const auto Mark = [&](const FString& Name)
    {
        Checks.Add(Name, true);
        Publish(TEXT("running"));
    };

    // 검증도 일반 사용자처럼 공개 복제 명부에서 동료를 찾는다.
    AFablePlayerState* Peer = nullptr;
    for (APlayerState* Entry : RootState->PlayerArray)
    {
        if (auto* Candidate = Cast<AFablePlayerState>(Entry); Candidate && Candidate != P)
        {
            if (Peer)
            {
                Finish(false, TEXT("Preparation expected exactly two remote participants."));
                return false;
            }
            Peer = Candidate;
        }
    }
    if (!Peer || P->PartyPlayerKey.IsEmpty() || Peer->PartyPlayerKey.IsEmpty())
    {
        return false;
    }

    if (!ExpeditionReport)
    {
        ExpeditionStarted = Now;
        ExpeditionSelfKey = P->PartyPlayerKey;
        ExpeditionPeerKey = Peer->PartyPlayerKey;
        ExpeditionReport = MakeShared<FJsonObject>();
        ExpeditionReport->SetStringField(TEXT("selfKey"), ExpeditionSelfKey);
        ExpeditionReport->SetStringField(TEXT("peerKey"), ExpeditionPeerKey);
        ExpeditionReport->SetBoolField(TEXT("extendedNegatives"), Extended);
        ExpeditionReport->SetObjectField(TEXT("earnedBeforeEntry"), Profile(P));
        PC->UIClosePanels();
        PC->UIToggleParty();
        Capture(TEXT("party-preparation"));
        return false;
    }
    if (Now - ExpeditionStarted > 240)
    {
        Finish(false, FString::Printf(TEXT("Party preparation UI step %d timed out."), ExpeditionStep));
        return false;
    }

    // 새 원정 전에 미확인 종료 결과가 있으면 실제 조회·확인 RPC와 본인 복제를 모두 관찰한다.
    const auto& PreviousResult = P->GetExpeditionResult();
    if (PreviousResult.IsTerminal() && (!PreviousResult.bAcknowledged || ExpeditionResultStep > 0))
    {
        if (ExpeditionResultId != PreviousResult.Ticket.ExpeditionId ||
            ExpeditionCompletionId != PreviousResult.CompletionId)
        {
            ExpeditionResultStep = 0;
            ExpeditionResultId = PreviousResult.Ticket.ExpeditionId;
            ExpeditionCompletionId = PreviousResult.CompletionId;
        }
        if (PC->IsExpeditionActionPending())
        {
            return false;
        }
        const auto* Displayed = PC->GetDisplayedExpeditionResult();
        const bool Matches =
            Displayed && Displayed->IsTerminal() && Displayed->Ticket.ExpeditionId == ExpeditionResultId &&
            Displayed->CompletionId == ExpeditionCompletionId && Displayed->RecipientPublicKey == P->PartyPlayerKey;
        const bool ReplyArrived = PC->GetLastExpeditionActionRequestId().IsValid() &&
                                  PC->GetLastExpeditionActionRequestId() != ExpeditionPreviousActionId;
        if (ExpeditionResultStep == 0 && Now - ExpeditionLastSentAt >= .7)
        {
            ExpeditionPreviousActionId = PC->GetLastExpeditionActionRequestId();
            PC->UIOpenExpeditionResult(ExpeditionResultId);
            if (PC->IsExpeditionActionPending())
            {
                ExpeditionResultStep = 1;
                ExpeditionLastSentAt = Now;
            }
        }
        else if (ExpeditionResultStep == 1 && ReplyArrived)
        {
            if (PC->DidExpeditionActionSucceed() && Matches)
            {
                ExpeditionPreviousActionId = PC->GetLastExpeditionActionRequestId();
                PC->UIAcknowledgeExpeditionResult();
                if (PC->IsExpeditionActionPending())
                {
                    ExpeditionResultStep = 2;
                    ExpeditionLastSentAt = Now;
                }
            }
            else if (!PC->DidExpeditionActionSucceed())
            {
                ExpeditionResultStep = 0;
            }
        }
        else if (ExpeditionResultStep == 2 && ReplyArrived)
        {
            if (PC->DidExpeditionActionSucceed() && Matches && PreviousResult.bAcknowledged)
            {
                ExpeditionReport->SetStringField(TEXT("previousResultExpeditionId"),
                                                 ExpeditionResultId.ToString(EGuidFormats::DigitsWithHyphensLower));
                ExpeditionReport->SetStringField(TEXT("previousResultCompletionId"),
                                                 ExpeditionCompletionId.ToString(EGuidFormats::DigitsWithHyphensLower));
                Mark(TEXT("entry_previous_terminal_result_acknowledged"));
                ExpeditionResultStep = 0;
                PC->UIClosePanels();
            }
            else if (!PC->DidExpeditionActionSucceed())
            {
                ExpeditionResultStep = 0;
            }
        }
        return false;
    }

    const auto* Party = Manager->GetPartyForMember(P->GetPlayerId());
    const auto Both = [&]()
    {
        if (!Party || Party->Members.Num() != 2 || Party->PartyId != P->PartyId || Peer->PartyId != P->PartyId ||
            Party->LeaderKey != (Leader ? ExpeditionSelfKey : ExpeditionPeerKey))
        {
            return false;
        }
        TSet<FString> Keys;
        for (const auto& Member : Party->Members)
        {
            if (!Member.bConnected)
            {
                return false;
            }
            Keys.Add(Member.PlayerKey);
        }
        return Keys.Num() == 2 && Keys.Contains(ExpeditionSelfKey) && Keys.Contains(ExpeditionPeerKey);
    };
    const auto AllReady = [&]()
    {
        if (!Both())
        {
            return false;
        }
        for (const auto& Member : Party->Members)
        {
            if (Member.ReadyRevision != Party->PreparationRevision)
            {
                return false;
            }
        }
        return true;
    };
    const auto Fingerprint = [&]()
    {
        if (!Party)
        {
            return FString();
        }
        FString Value =
            FString::Printf(TEXT("%s|%s|%d|%d|%d|%d"), *Party->PartyId.ToString(), *Party->LeaderKey, Party->Revision,
                            Party->PreparationRevision, int32(Party->SelectedRegion), int32(Party->SelectedDifficulty));
        for (const auto& Member : Party->Members)
        {
            Value += FString::Printf(TEXT("|%s:%d:%d:%d"), *Member.PlayerKey, Member.PlayerId,
                                     Member.bConnected ? 1 : 0, Member.ReadyRevision);
        }
        return Value;
    };
    const auto Send = [&](TFunctionRef<void()> Request)
    {
        if (bExpeditionRequestSent || PC->IsPartyPreparationPending() || Now - ExpeditionLastSentAt < .7 ||
            !P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded ||
            !PeerHas(TEXT("entry_formal_party_two")))
        {
            return;
        }
        ExpeditionNoticeBefore = P->NoticeSerial;
        ExpeditionRequestFingerprint = Fingerprint();
        Request();
        bExpeditionRequestSent = true;
        ExpeditionLastSentAt = Now;
    };
    const auto Rejected = [&](const FString& ExpectedNotice)
    {
        return bExpeditionRequestSent && !PC->IsPartyPreparationPending() && !PC->DidPartyPreparationSucceed() &&
               PC->GetPartyPreparationMessage() == ExpectedNotice && P->NoticeSerial > ExpeditionNoticeBefore &&
               P->LastNotice == ExpectedNotice && Both() && Fingerprint() == ExpeditionRequestFingerprint &&
               PC->GetPartyPreparationResultRevision() == Party->PreparationRevision &&
               !P->ActiveExpeditionId.IsValid() && !Peer->ActiveExpeditionId.IsValid() &&
               Progress->ActiveRegion == EFableRegion::Village;
    };
    const FString Stale = TEXT("파티 준비 상태가 바뀌었습니다. 새 상태를 확인하세요.");
    const FString LeaderOnly = TEXT("파티장만 원정을 선택하거나 시작할 수 있습니다.");

    // 파티 생성과 초대도 제품 UI에서 사용하는 RPC로만 요청한다.
    switch (ExpeditionStep)
    {
    case 0:
        if (!PC->IsPartyOpen())
        {
            PC->UIToggleParty();
        }
        if (Leader && !Party && Now - ExpeditionLastSentAt > 1)
        {
            PC->UICreateParty();
            ExpeditionLastSentAt = Now;
        }
        if (Leader && Party)
        {
            Advance(1);
        }
        if (!Leader)
        {
            if (const auto* Invite = Manager->GetPendingInviteFor(P->GetPlayerId()))
            {
                if (Now - ExpeditionLastSentAt > 1)
                {
                    PC->UIRespondPartyInvite(Invite->InviteId, true);
                    ExpeditionLastSentAt = Now;
                }
            }
            if (Both())
            {
                Advance(2);
            }
        }
        break;
    case 1:
        if (Both())
        {
            Advance(2);
        }
        else if (!Manager->GetPendingInviteFor(Peer->GetPlayerId()) && Now - ExpeditionLastSentAt > 1)
        {
            PC->UIInviteParty(Peer->GetPlayerId());
            ExpeditionLastSentAt = Now;
        }
        break;
    case 2:
        if (!Both() || !P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded ||
            P->GetBackendRevision() != P->GetBackendAcknowledgedRevision())
        {
            break;
        }
        // 실제 입장 요청에는 제품 규칙대로 파티장의 포털 접근도 필요하다.
        PC->UIClosePanels();
        if (!DungeonWalk(PC, FableRPG::PortalLocation() + FVector(Leader ? -80 : 80, -100, 100), 90) ||
            Hero->GetVelocity().Size2D() > 10)
        {
            break;
        }
        ExpeditionPartyId = Party->PartyId;
        ExpeditionPreviousRevision = Party->PreparationRevision;
        ExpeditionReport->SetStringField(TEXT("partyId"),
                                         ExpeditionPartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
        PC->UIOpenPartyExpedition();
        Mark(TEXT("entry_formal_party_two"));
        Advance(Extended ? 3 : 11);
        break;
    case 3:
        if (Leader)
        {
            Send(
                [&]()
                {
                    PC->UIEnterPreparedParty();
                });
            if (Rejected(TEXT("모든 파티원이 현재 선택에 준비해야 합니다.")))
            {
                Mark(TEXT("entry_unready_rejected"));
                Advance(4);
            }
        }
        else if (PeerHas(TEXT("entry_unready_rejected")))
        {
            Mark(TEXT("entry_unready_rejected"));
            Advance(4);
        }
        break;
    case 4:
        if (!Leader)
        {
            Send(
                [&]()
                {
                    PC->UISelectDifficulty(EFableDifficulty::Hard);
                });
            if (Rejected(LeaderOnly) && Party->SelectedDifficulty == EFableDifficulty::Normal &&
                Party->PreparationRevision == ExpeditionPreviousRevision)
            {
                Mark(TEXT("entry_nonleader_selection_rejected"));
                Advance(5);
            }
        }
        else if (PeerHas(TEXT("entry_nonleader_selection_rejected")))
        {
            Mark(TEXT("entry_nonleader_selection_rejected"));
            Advance(5);
        }
        break;
    case 5:
        Send(
            [&]()
            {
                PC->UISetPartyReady(true);
            });
        if (AllReady() && !PC->IsPartyPreparationPending())
        {
            Mark(TEXT("entry_initial_both_ready"));
            Advance(6);
        }
        break;
    case 6:
        if (Leader && PeerHas(TEXT("entry_initial_both_ready")))
        {
            Send(
                [&]()
                {
                    PC->UISelectDifficulty(EFableDifficulty::Hard);
                });
        }
        if (Both() && Party->PreparationRevision > ExpeditionPreviousRevision &&
            Party->SelectedDifficulty == EFableDifficulty::Hard && !PC->IsPartyPreparationPending())
        {
            bool Cleared = true;
            for (const auto& Member : Party->Members)
            {
                Cleared &= Member.ReadyRevision == 0;
            }
            if (Cleared)
            {
                Mark(TEXT("entry_selection_resets_all_ready"));
                Advance(7);
            }
        }
        break;
    case 7:
        if (!Leader && PeerHas(TEXT("entry_selection_resets_all_ready")))
        {
            Send(
                [&]()
                {
                    PC->UIRequestPartyReady(ExpeditionPartyId, ExpeditionPreviousRevision, true);
                });
            if (Rejected(Stale) && !AllReady())
            {
                Mark(TEXT("entry_stale_ready_rejected"));
                Advance(8);
            }
        }
        else if (Leader && PeerHas(TEXT("entry_stale_ready_rejected")))
        {
            Mark(TEXT("entry_stale_ready_rejected"));
            Advance(8);
        }
        break;
    case 8:
        if (Leader)
        {
            Send(
                [&]()
                {
                    PC->UIRequestPartySelection(ExpeditionPartyId, ExpeditionPreviousRevision, EFableRegion::RootRuins,
                                                EFableDifficulty::Normal);
                });
            if (Rejected(Stale) && Party->SelectedDifficulty == EFableDifficulty::Hard)
            {
                Mark(TEXT("entry_stale_selection_rejected"));
                Advance(9);
            }
        }
        else if (PeerHas(TEXT("entry_stale_selection_rejected")))
        {
            Mark(TEXT("entry_stale_selection_rejected"));
            Advance(9);
        }
        break;
    case 9:
        if (!Leader)
        {
            Send(
                [&]()
                {
                    PC->UIEnterPreparedParty();
                });
            if (Rejected(LeaderOnly))
            {
                Mark(TEXT("entry_nonleader_start_rejected"));
                Advance(10);
            }
        }
        else if (PeerHas(TEXT("entry_nonleader_start_rejected")))
        {
            Mark(TEXT("entry_nonleader_start_rejected"));
            Advance(10);
        }
        break;
    case 10:
        if (Leader)
        {
            Send(
                [&]()
                {
                    PC->UIRequestPartyEntry(ExpeditionPartyId, ExpeditionPreviousRevision);
                });
            if (Rejected(Stale))
            {
                Mark(TEXT("entry_stale_start_rejected"));
                Advance(11);
            }
        }
        else if (PeerHas(TEXT("entry_stale_start_rejected")))
        {
            Mark(TEXT("entry_stale_start_rejected"));
            Advance(11);
        }
        break;
    case 11:
        if (Leader)
        {
            Send(
                [&]()
                {
                    PC->UIRequestPartySelection(ExpeditionPartyId, Party->PreparationRevision, EFableRegion::RootRuins,
                                                EFableDifficulty::Normal);
                });
        }
        if (Both() && Party->SelectedRegion == EFableRegion::RootRuins &&
            Party->SelectedDifficulty == EFableDifficulty::Normal && !PC->IsPartyPreparationPending() &&
            (!Extended || Party->PreparationRevision > ExpeditionPreviousRevision + 1))
        {
            Mark(TEXT("entry_final_selection_observed"));
            Advance(12);
        }
        break;
    case 12:
        if (!PeerHas(TEXT("entry_final_selection_observed")))
        {
            break;
        }
        Send(
            [&]()
            {
                PC->UISetPartyReady(true);
            });
        if (AllReady() && !PC->IsPartyPreparationPending())
        {
            ExpeditionReport->SetNumberField(TEXT("preparationRevision"), Party->PreparationRevision);
            ExpeditionReport->SetNumberField(TEXT("region"), int32(Party->SelectedRegion));
            ExpeditionReport->SetNumberField(TEXT("difficulty"), int32(Party->SelectedDifficulty));
            Mark(TEXT("entry_final_both_ready"));
            Capture(TEXT("party-all-ready"));
            Advance(13);
        }
        break;
    case 13:
        if (Leader && PeerHas(TEXT("entry_final_both_ready")))
        {
            Send(
                [&]()
                {
                    PC->UIEnterPreparedParty();
                });
        }
        if (Both() && Peer->GetPawn() && P->ActiveExpeditionId.IsValid() &&
            P->ActiveExpeditionId == Peer->ActiveExpeditionId && Progress->ActiveRegion == EFableRegion::RootRuins &&
            Progress->ActiveDifficulty == EFableDifficulty::Normal &&
            Progress.IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()) &&
            Progress.IsInRegion(EFableRegion::RootRuins, Peer->GetPawn()->GetActorLocation()))
        {
            ExpeditionReport->SetStringField(TEXT("expeditionId"),
                                             P->ActiveExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
            Mark(TEXT("entry_exact_two_arrived"));
            Advance(14);
        }
        break;
    case 14:
    {
        // 기존 실제 재접속 검증에서 사용한 외곽 이동·회피 경로로 추적을 끊는다.
        if (Extended)
        {
            if (Hero->GetHealth() < Hero->GetMaxHealth() * .45f && P->HealthPotions > 0 &&
                P->GetPotionCooldownRemaining() <= 0 && Now - ExpeditionLastSentAt > 1)
            {
                PC->UIUseHealthPotion();
                ExpeditionLastSentAt = Now;
            }
            if (!ReconnectWalkSafe(PC) ||
                FVector::Dist2D(Hero->GetActorLocation(), ExpeditionSafeLocation(Hero)) >= 100)
            {
                break;
            }
            for (TActorIterator<AFableEnemy> It(World); It; ++It)
            {
                if (It->IsAlive() && It->GetEncounterRegion() == EFableRegion::RootRuins &&
                    It->GetExpeditionId() == Progress.ExpeditionId() &&
                    FVector::Dist2D(Hero->GetActorLocation(), It->GetActorLocation()) < 1800)
                {
                    return false;
                }
            }
        }
        if (!P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded ||
            P->GetBackendRevision() != P->GetBackendAcknowledgedRevision())
        {
            break;
        }
        const TSharedPtr<FJsonObject>* Arrival = nullptr;
        if (!ServerReport || !ServerReport->TryGetObjectField(TEXT("serverExpeditionArrival"), Arrival) ||
            !ExpeditionCheck(ServerReport, TEXT("server_exact_expedition_arrival")) ||
            (*Arrival)->GetStringField(TEXT("expeditionId")) !=
                P->ActiveExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower))
        {
            break;
        }
        ExpeditionReport->SetBoolField(TEXT("safeSaved"), Extended);
        if (Extended)
        {
            Mark(TEXT("entry_safe_saved_server_ticket_observed"));
        }
        Mark(TEXT("entry_saved_server_ticket_observed"));
        Advance(15);
        break;
    }
    case 15:
        if (PeerHas(TEXT("entry_saved_server_ticket_observed")))
        {
            ExpeditionReport->SetBoolField(TEXT("peerAndServerObserved"), true);
            Mark(TEXT("entry_peer_and_server_handshake"));
            Advance(16);
        }
        break;
    case 16:
        if (PeerHas(TEXT("entry_peer_and_server_handshake")))
        {
            ExpeditionReport->SetStringField(TEXT("status"), TEXT("passed"));
            return true;
        }
        break;
    default:
        Finish(false, TEXT("Unknown party preparation UI step."));
        break;
    }

    return false;
}
