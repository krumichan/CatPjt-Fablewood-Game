#include "FableRPGValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "FableSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"

namespace
{
    /** 이야기 검증에서 레벨 상승까지 반영한 누적 경험치를 구한다. */
    int32 StoryTotalXP(const AFablePlayerState* P)
    {
        return P->Level * (P->Level - 1) * 50 + P->Experience;
    }
}

/** 이야기 이벤트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableRPGValidationDriver::StoryEvent(const FString& Action)
{
    // 퀘스트와 이야기 진행에 필요한 값 준비
    const AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
    E->SetStringField(TEXT("action"), Action);
    E->SetNumberField(TEXT("chapter"), P->StoryChapter);
    E->SetNumberField(TEXT("objects"), P->StoryObjectMask);
    E->SetNumberField(TEXT("seals"), P->StorySealMask);
    E->SetNumberField(TEXT("gold"), P->Gold);
    E->SetNumberField(TEXT("ore"), P->Ore);
    E->SetNumberField(TEXT("total_xp"), StoryTotalXP(P));
    E->SetStringField(TEXT("signature"), AdventureSignature(P));

    // 재화와 작업 비용 처리
    if (StoryRequestedChapter >= 0)
    {
        E->SetNumberField(TEXT("reward_chapter"), StoryRequestedChapter);
        E->SetNumberField(TEXT("gold_before"), StoryGoldBefore);
        E->SetNumberField(TEXT("ore_before"), StoryOreBefore);
        E->SetNumberField(TEXT("xp_before"), StoryXPBefore);
    }

    StoryEvents.Add(MakeShared<FJsonValueObject>(E));
}

/** 이야기 보고서 항목을 최종 검증 JSON에 추가한다. */
void UFableRPGValidationDriver::AppendStoryReport(TSharedRef<FJsonObject> O) const
{
    // JSON 계약과 검증 기록에 필요한 값 준비
    const AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    O->SetBoolField(TEXT("story_enabled"), bStory);
    O->SetStringField(TEXT("story_reload_signature"), StoryReloadSignature);
    O->SetStringField(TEXT("workshop_expected_notice"), StoryExpectedNotice);
    O->SetArrayField(TEXT("story_events"), StoryEvents);
    TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();

    // JSON 계약과 검증 기록 순회
    for (const auto& E : StoryChecks)
    {
        C->SetBoolField(E.Key, E.Value);
    }

    O->SetObjectField(TEXT("story_checks"), C);

    // 서버 권한과 실행 위치 처리
    if (P)
    {
        // 퀘스트와 이야기 진행 처리
        O->SetNumberField(TEXT("story_chapter"), P->StoryChapter);
        O->SetNumberField(TEXT("story_object_mask"), P->StoryObjectMask);
        O->SetNumberField(TEXT("story_seal_mask"), P->StorySealMask);
        O->SetNumberField(TEXT("total_xp"), StoryTotalXP(P));
        int32 V = 0;

        // 서버 권한과 실행 위치 처리
        if (P->HasAuthority())
        {
            if (const UFableSaveGame* D =
                    Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromSlot(P->GetProfileSlotForValidation(), 0)))
            {
                V = D->Version;
            }
        }

        O->SetNumberField(TEXT("saved_schema_version"), V);
    }
}

/** 이야기 목표와 보상 저장의 검증 단계를 진행한다. */
bool UFableRPGValidationDriver::TickStory()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    AFableCharacter* H = PC->GetPawn<AFableCharacter>();
    AFableGameState* G = GetWorld()->GetGameState<AFableGameState>();
    const double Now = GetWorld()->GetTimeSeconds();
    const bool bPrologue = Phase == TEXT("prologue");
    const EFableClass Wanted =
        Role == TEXT("host") ? (bAdventureWarrior ? EFableClass::Warrior : EFableClass::Mage) : EFableClass::Archer;
    const auto Stop = [&]()
    {
        Hold(EKeys::W, false);
        Hold(EKeys::S, false);
    };
    const auto Fail = [&](const TCHAR* Reason)
    {
        Stop();
        Capture(TEXT("story-failure"));
        Finish(false, Reason);
        return true;
    };

    // JSON 계약과 검증 기록 처리
    if (Now - LastStoryReport > 2)
    {
        Report(TEXT("running"), TEXT("Normal-input main story and persistence validation."));
        LastStoryReport = Now;
    }

    // 퀘스트와 이야기 진행 처리
    if (Phase == TEXT("resume"))
    {
        if (Now - StartedAt < 4)
        {
            return true;
        }
        if (P->StoryChapter != 5 || P->StoryObjectMask != 31 || P->StorySealMask != 7)
        {
            return Fail(TEXT("Story progress was not retained in the completed saved profile."));
        }
        return false; // The established adventure reload also checks trials, gear and completion counts.
    }

    // 퀘스트와 이야기 진행 처리
    if (!bStoryReloadObserved)
    {
        if (Now - StartedAt < 4)
        {
            return true;
        }
        if (bPrologue)
        {
            if (P->StoryChapter != 0 || P->StoryObjectMask != 0 || P->StorySealMask != 0 || P->Level != 1)
            {
                return Fail(TEXT("Prologue did not start with a new story profile."));
            }
        }
        else
        {
            if (P->StoryChapter != 1 || P->StoryObjectMask != 1 || P->StorySealMask != 0 || P->HeroClass != Wanted)
            {
                return Fail(TEXT("The partial collection checkpoint was not restored."));
            }
            StoryReloadSignature = AdventureSignature(P);
            StoryChecks.Add(TEXT("partial_checkpoint_restored"), true);
            StoryEvent(TEXT("restored-partial-collection"));
            Capture(TEXT("restored-partial-story"));
        }
        bStoryReloadObserved = true;
    }

    // 검증 입력과 키 유지 상태 조건 검사
    if (Now < StoryResumeAt)
    {
        Stop();
        return true;
    }
    if (!H->IsAlive())
    {
        return false;
    }
    // Verify exact rewards at a safe village board. Requests use the normal quest UI and server validation.

    // 재화와 작업 비용 처리
    if (StoryRequestedChapter >= 0 && P->StoryChapter > StoryRequestedChapter)
    {
        // 재화와 작업 비용에 필요한 값 준비
        static const int32 XP[] = {20, 60, 60, 80, 100}, Gold[] = {20, 40, 50, 70, 100}, Ore[] = {0, 3, 3, 4, 5};
        const int32 C = StoryRequestedChapter;
        const bool bExact = P->StoryChapter == C + 1 && P->Gold == StoryGoldBefore + Gold[C] &&
                            P->Ore == StoryOreBefore + Ore[C] && StoryTotalXP(P) == StoryXPBefore + XP[C];
        StoryChecks.Add(FString::Printf(TEXT("chapter_%d_exact_reward"), C), bExact);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!bExact)
        {
            return Fail(TEXT("Main story reward delta differs from the fixed chapter reward."));
        }

        // 퀘스트와 이야기 진행 처리
        StoryEvent(TEXT("chapter-reported"));
        StoryRequestedChapter = -1;
        PC->UIClosePanels();
        LastAction = -10;
        Capture(FString::Printf(TEXT("story-chapter-%d"), P->StoryChapter));
        // A host sees its own commit immediately. Respect the same server mutation cooldown as a human's next action.
        StoryResumeAt = Now + .7;

        return true;
    }
    // Each collected or investigated object remains usable by the other player, with no per-use currency reward.

    int32 WantedObject = -1;

    // 퀘스트와 이야기 진행 처리
    if (P->StoryChapter == 1)
    {
        const int32 Last = bPrologue ? 1 : 4;
        for (int32 I = 0; I < Last; ++I)
        {
            if (!(P->StoryObjectMask & (1 << I)))
            {
                WantedObject = I;
                break;
            }
        }
    }

    // 퀘스트와 이야기 진행 처리
    if (P->StoryChapter == 3 && G->ActiveRegion == EFableRegion::RootRuins && !(P->StoryObjectMask & 16))
    {
        WantedObject = 4;
    }

    // 이동 입력과 속도 제한 처리
    if (StoryObject >= 0 || WantedObject >= 0)
    {
        // 퀘스트와 이야기 진행 처리
        if (StoryObject < 0)
        {
            StoryObject = WantedObject;
            StoryCheck = 0;
            StoryAt = Now;
            PC->UIClosePanels();
            LastAction = -10;
        }

        const int32 I = StoryObject;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - StoryAt > 80)
        {
            return Fail(TEXT("Normal E interaction did not complete the story object."));
        }

        // 이동 입력과 속도 제한 처리
        if (StoryCheck == 0)
        {
            // 이동 입력과 속도 제한 조건 검사
            if (!WalkTo(FableRPG::StoryObjectLocation(I) + FVector(Role == TEXT("host") ? -60 : 60, -90, 100), 90) ||
                H->GetVelocity().Size2D() > 10)
            {
                return true;
            }
            if (Now - LastAction < .8)
            {
                return true;
            }

            // 재화와 작업 비용 처리
            StoryGoldBefore = P->Gold;
            StoryOreBefore = P->Ore;
            StoryXPBefore = StoryTotalXP(P);
            Key(EKeys::E);
            LastAction = Now;
            StoryCheck = 1;

            return true;
        }

        // 재화와 작업 비용 처리
        if (StoryCheck == 1)
        {
            Stop();

            // 퀘스트와 이야기 진행 처리
            if (!(P->StoryObjectMask & (1 << I)))
            {
                if (Now - LastAction > 1)
                {
                    Key(EKeys::E);
                    LastAction = Now;
                }
                return true;
            }

            // 재화와 작업 비용 조건 검사
            if (I < 4 && (P->Gold != StoryGoldBefore || P->Ore != StoryOreBefore || StoryTotalXP(P) != StoryXPBefore))
            {
                return Fail(TEXT("An object interaction granted an unexpected repeatable reward."));
            }

            StoryChecks.Add(FString::Printf(TEXT("object_%d_observed"), I), true);
            StoryEvent(FString::Printf(TEXT("object-%d"), I));
            StoryBeforeSignature = AdventureSignature(P);
            LastAction = Now;
            StoryCheck = 2;
            Capture(FString::Printf(TEXT("story-object-%d"), I));

            return true;
        }

        // 검증 입력과 키 유지 상태 처리
        if (StoryCheck == 2)
        {
            Stop();
            if (Now - LastAction < .8)
            {
                return true;
            }
            Key(EKeys::E);
            LastAction = Now;
            StoryCheck = 3;
            return true;
        }

        Stop();

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - LastAction < .8)
        {
            return true;
        }
        // The root rune is beside a live encounter, so only the safe prologue checks the entire signature here.

        // 이 단계에서 이야기 목표와 보상 저장의 검증 단계를 진행한다.
        if (I < 4)
        {
            const bool bSame = StoryBeforeSignature == AdventureSignature(P);
            StoryChecks.Add(FString::Printf(TEXT("object_%d_duplicate_no_reward"), I), bSame);
            if (!bSame)
            {
                return Fail(TEXT("Repeating a collected story object changed persistent progress or rewards."));
            }
        }

        // 퀘스트와 이야기 진행 처리
        StoryObject = -1;
        StoryCheck = 0;
        LastAction = -10;
        PC->UIClosePanels();

        return true;
    }

    // SQL 명령과 조회 결과 처리
    if (bPrologue && P->StoryChapter == 1 && P->StoryObjectMask == 1)
    {
        Stop();

        // SQL 명령과 조회 결과 처리
        if (StoryCheck == 0)
        {
            PC->UIToggleQuests();
            PC->UISelectQuest(EFableQuestId::Story);
            Capture(TEXT("partial-story-journal"));
            StoryAt = Now;
            StoryCheck = 5;
            return true;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - StoryAt < 2)
        {
            return true;
        }

        StoryChecks.Add(TEXT("partial_checkpoint_saved"), P->bLastSaveSucceeded);
        Finish(
            P->bLastSaveSucceeded,
            TEXT(
                "One collectible and the invitation were saved through normal player actions before process restart."));

        return true;
    }

    const bool bReport = P->StoryChapter == 0 || (P->StoryChapter < 5 && P->IsStoryReady());

    // 이동 입력과 속도 제한 처리
    if (bReport)
    {
        // 퀘스트와 이야기 진행 조건 검사
        if (bPrologue && P->StoryChapter != 0)
        {
            return Fail(TEXT("Prologue advanced beyond its partial checkpoint."));
        }

        // 이동 입력과 속도 제한 처리
        if (P->StoryChapter == 0 && P->HeroClass != Wanted)
        {
            const FVector B = FableRPG::BoardLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100);
            if (WalkTo(B, 90) && H->GetVelocity().Size2D() < 10 && Now - LastAction > .8)
            {
                PC->UIChooseClass(Wanted);
                LastAction = Now;
            }
            return true;
        }

        // 재화와 작업 비용 처리
        if (StoryRequestedChapter < 0)
        {
            StoryRequestedChapter = P->StoryChapter;
            StoryGoldBefore = P->Gold;
            StoryOreBefore = P->Ore;
            StoryXPBefore = StoryTotalXP(P);
            StoryAt = Now;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - StoryAt > 80)
        {
            return Fail(TEXT("The main story chapter could not be reported to Rowan."));
        }

        AdventureQuest(EFableQuestId::Story);

        return true;
    }

    // 이동 입력과 속도 제한 처리
    if (P->StoryChapter == 4 && G->ActiveRegion == EFableRegion::Moonwell)
    {
        // A partner may cleanse a seal first. Read the real public seal state, then walk back and press E personally.
        for (int32 I = 0; I < 3; ++I)
        {
            if ((G->MoonSealMask & (1 << I)) && !(P->StorySealMask & (1 << I)))
            {
                PC->UIClosePanels();
                if (WalkTo(FableRPG::MoonSealLocation(I) + FVector(Role == TEXT("host") ? -60 : 60, -90, 100), 90) &&
                    H->GetVelocity().Size2D() < 10 && Now - LastAction > .8)
                {
                    Key(EKeys::E);
                    LastAction = Now;
                    Capture(FString::Printf(TEXT("story-seal-%d"), I));
                }
                return true;
            }
        }
        if (P->StorySealMask == 7)
        {
            StoryChecks.Add(TEXT("all_three_seals_personally_recorded"), true);
        }
    }

    // 퀘스트와 이야기 진행 처리
    if (P->StoryChapter == 5 && !StoryChecks.FindRef(TEXT("completed_report_no_reward")))
    {
        Stop();

        // 퀘스트와 이야기 진행 처리
        if (StoryCheck == 0)
        {
            StoryBeforeSignature = AdventureSignature(P);
            StoryNoticeSerial = P->NoticeSerial;
            PC->UISelectQuest(EFableQuestId::Story);
            PC->UIConfirmQuest();
            StoryAt = Now;
            StoryCheck = 6;
            return true;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - StoryAt < 1)
        {
            return true;
        }

        const bool bSame = StoryBeforeSignature == AdventureSignature(P) && P->NoticeSerial != StoryNoticeSerial &&
                           P->LastNotice.Contains(TEXT("숲 마을의 등불을 되살렸습니다."));
        StoryChecks.Add(TEXT("completed_report_no_reward"), bSame);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!bSame)
        {
            return Fail(TEXT("A completed main story paid its reward again."));
        }

        // 퀘스트와 이야기 진행 처리
        PC->UIClosePanels();
        Key(EKeys::Q);
        PC->UISelectQuest(EFableQuestId::Story);
        Capture(TEXT("main-story-complete"));
        StoryCheck = 7;
        StoryAt = Now;

        return true;
    }

    // 검증 입력과 키 유지 상태 처리
    if (StoryCheck == 7)
    {
        Stop();
        if (Now - StoryAt < 2)
        {
            return true;
        }
        PC->UIClosePanels();
        StoryCheck = 0;
    }

    // 이동 입력과 속도 제한 처리
    if (P->StoryChapter == 5 && Step >= 35 && StoryPostStage < 7)
    {
        // 이동 입력과 속도 제한 처리
        if (StoryPostStage == 0)
        {
            PC->UIClosePanels();

            // 이동 입력과 속도 제한 조건 검사
            if (!WalkTo(FableRPG::ForgeLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 90) ||
                H->GetVelocity().Size2D() > 10)
            {
                return true;
            }

            PC->UIOpenWorkshop();
            PC->UISelectWorkshopTab(EFableWorkshopTab::Craft);
            PC->UISelectCraftRecipe(4, EFableEquipmentSlot::Weapon);
            StoryExpectedNotice = P->GetCraftBlockReason(4, EFableEquipmentSlot::Weapon);

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (StoryExpectedNotice.IsEmpty())
            {
                return Fail(TEXT("The workshop notice check needs a naturally unaffordable recipe."));
            }

            StoryBeforeSignature = AdventureSignature(P);
            StoryNoticeSerial = P->NoticeSerial;
            StoryAt = Now;
            StoryPostStage = 1;

            return true;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - StoryAt > 20)
        {
            return Fail(TEXT("Workshop notice transition did not acknowledge normal UI requests."));
        }

        // 검증 입력과 키 유지 상태 처리
        if (StoryPostStage == 1)
        {
            Stop();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (Now - StoryAt < .8)
            {
                return true;
            }
            if (!PC->IsWorkshopOpen())
            {
                return Fail(TEXT("Workshop UI did not open at the forge."));
            }

            PC->UICraft();
            StoryAt = Now;
            StoryPostStage = 2;

            return true;
        }

        // 검증 입력과 키 유지 상태 처리
        if (StoryPostStage == 2)
        {
            Stop();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (P->NoticeSerial == StoryNoticeSerial)
            {
                return true;
            }

            const bool bVisible = PC->ShouldShowTransientNotice() && !StoryExpectedNotice.IsEmpty() &&
                                  P->LastNotice == StoryExpectedNotice && AdventureSignature(P) == StoryBeforeSignature;
            StoryChecks.Add(TEXT("workshop_error_visible"), bVisible);

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!bVisible)
            {
                return Fail(TEXT("The denied recipe request lost its error or changed the profile."));
            }

            Capture(TEXT("workshop-current-notice"));
            StoryAt = Now;
            StoryPostStage = 3;

            return true;
        }

        // 검증 입력과 키 유지 상태 처리
        if (StoryPostStage == 3)
        {
            Stop();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (Now - StoryAt < 1)
            {
                return true;
            }

            PC->UISelectWorkshopTab(EFableWorkshopTab::Repair);
            const bool bHidden = !PC->ShouldShowTransientNotice() && !StoryExpectedNotice.IsEmpty() &&
                                 P->LastNotice == StoryExpectedNotice && AdventureSignature(P) == StoryBeforeSignature;
            StoryChecks.Add(TEXT("workshop_stale_notice_hidden"), bHidden);

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!bHidden)
            {
                return Fail(TEXT("A previous workshop error remained visible after changing tabs."));
            }

            Capture(TEXT("workshop-clean-repair-tab"));
            StoryAt = Now;
            StoryPostStage = 4;

            return true;
        }

        // 검증 입력과 키 유지 상태 처리
        if (StoryPostStage == 4)
        {
            Stop();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (Now - StoryAt < 1)
            {
                return true;
            }

            PC->UISelectWorkshopTab(EFableWorkshopTab::Craft);
            StoryNoticeSerial = P->NoticeSerial;
            PC->UICraft();
            StoryAt = Now;
            StoryPostStage = 5;

            return true;
        }

        // 검증 입력과 키 유지 상태 처리
        if (StoryPostStage == 5)
        {
            Stop();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (P->NoticeSerial == StoryNoticeSerial)
            {
                return true;
            }

            const bool bVisible = PC->ShouldShowTransientNotice() && !StoryExpectedNotice.IsEmpty() &&
                                  P->LastNotice == StoryExpectedNotice && AdventureSignature(P) == StoryBeforeSignature;
            StoryChecks.Add(TEXT("workshop_repeated_error_visible"), bVisible);

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!bVisible)
            {
                return Fail(TEXT("An identical new workshop error stayed suppressed."));
            }

            Capture(TEXT("workshop-repeated-current-notice"));
            StoryAt = Now;
            StoryPostStage = 6;

            return true;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - StoryAt < 1)
        {
            return true;
        }

        PC->UIClosePanels();
        const bool bNoticeCleared = !PC->ShouldShowTransientNotice();
        StoryChecks.Add(TEXT("workshop_notice_hidden_after_close"), bNoticeCleared);

        // 이동 입력과 속도 제한 조건 검사
        if (!bNoticeCleared)
        {
            return Fail(TEXT("An old workshop error remained visible after leaving the panel."));
        }
        if (!WalkTo(FableRPG::BoardLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 90) ||
            H->GetVelocity().Size2D() > 10)
        {
            return true;
        }

        // SQL 명령과 조회 결과 처리
        Key(EKeys::Q);
        PC->UISelectQuest(EFableQuestId::Story);
        Capture(TEXT("story-final-journal"));
        StoryPostStage = 7;
        StoryAt = Now;

        return true;
    }

    // 검증 입력과 키 유지 상태 조건 검사
    if (StoryPostStage == 7 && Now - StoryAt < 2)
    {
        Stop();
        return true;
    }

    // 퀘스트와 이야기 진행 처리
    if (G->ActiveRegion == EFableRegion::Moonwell && Step == 23)
    {
        for (APlayerState* Member : G->PlayerArray)
        {
            const AFablePlayerState* Other = Cast<AFablePlayerState>(Member);
            if (!Other || Other->StorySealMask != 7)
            {
                Stop();
                return true;
            }
        }
    }
    // Region entry is a party action; both players finish their village story report before it starts.

    // 퀘스트와 이야기 진행 처리
    if (G->ActiveRegion == EFableRegion::Village && ((Step >= 9 && Step <= 10) || (Step >= 18 && Step <= 20)))
    {
        const int32 Minimum = Step <= 10 ? 3 : 4;
        for (APlayerState* Member : G->PlayerArray)
        {
            const AFablePlayerState* Other = Cast<AFablePlayerState>(Member);
            if (!Other || Other->StoryChapter < Minimum)
            {
                Stop();
                return true;
            }
        }
    }

    // 퀘스트와 이야기 진행 처리
    if (P->StoryChapter == 2 && Step <= 4)
    {
        for (APlayerState* Member : G->PlayerArray)
        {
            const AFablePlayerState* Other = Cast<AFablePlayerState>(Member);
            if (!Other || Other->StoryChapter < 2)
            {
                Stop();
                return true;
            }
        }
    }

    return false;
}
