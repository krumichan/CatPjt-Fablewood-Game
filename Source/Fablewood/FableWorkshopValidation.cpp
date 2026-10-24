#include "FableRPGValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
namespace
{
    /** 공방 아이템 JSON에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    TSharedRef<FJsonObject> WorkshopItemJson(const FFableItem& I)
    {
        // JSON 계약과 검증 기록에 필요한 값 준비
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("id"), I.Id);
        J->SetNumberField(TEXT("slot"), int32(I.Slot));
        J->SetNumberField(TEXT("class"), int32(I.HeroClass));
        J->SetNumberField(TEXT("rarity"), I.Rarity);
        J->SetNumberField(TEXT("power"), I.Power);
        J->SetNumberField(TEXT("upgrade"), I.Upgrade);
        J->SetNumberField(TEXT("roll_version"), I.RollVersion);
        J->SetNumberField(TEXT("quality"), int32(I.Quality));
        J->SetNumberField(TEXT("nominal_power"), I.NominalPower);
        J->SetNumberField(TEXT("roll_percent"), I.RollPercent);
        J->SetNumberField(TEXT("set_id"), int32(I.SetId));
        J->SetNumberField(TEXT("durability_version"), I.DurabilityVersion);
        J->SetNumberField(TEXT("durability"), I.Durability);
        J->SetNumberField(TEXT("max_durability"), I.MaxDurability);
        J->SetStringField(TEXT("signature"), FableRPG::ItemPersistentSignature(I));
        TArray<TSharedPtr<FJsonValue>> A;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItemAffix& V : I.Affixes)
        {
            TSharedRef<FJsonObject> K = MakeShared<FJsonObject>();
            K->SetNumberField(TEXT("stat"), int32(V.Stat));
            K->SetNumberField(TEXT("value"), V.Value);
            A.Add(MakeShared<FJsonValueObject>(K));
        }

        J->SetArrayField(TEXT("affixes"), A);

        return J;
    }

    /** 공방 잔액 JSON에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    TSharedRef<FJsonObject> WorkshopBalanceJson(const FFableResourceCost& B)
    {
        // 재화와 작업 비용에 필요한 값 준비
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("gold"), B.Gold);
        J->SetNumberField(TEXT("ore"), B.Ore);
        J->SetNumberField(TEXT("essence"), B.Essence);
        J->SetNumberField(TEXT("root_cores"), B.RootCores);
        J->SetNumberField(TEXT("moon_crystals"), B.MoonCrystals);

        return J;
    }

    /** 공방 잔액에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    FFableResourceCost WorkshopBalance(const AFablePlayerState* P)
    {
        // 재화와 작업 비용에 필요한 값 준비
        FFableResourceCost B;
        B.Gold = P->Gold;
        B.Ore = P->Ore;
        B.Essence = P->Essence;
        B.RootCores = P->RootCores;
        B.MoonCrystals = P->MoonCrystals;

        return B;
    }
}

/** 공방 스냅숏에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableRPGValidationDriver::WorkshopSnapshot()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    WorkshopBeforeBalance = WorkshopBalance(P);
    WorkshopBeforeSignature = Signature(P);
    WorkshopBeforeIds.Reset();

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& I : P->Inventory)
    {
        WorkshopBeforeIds.Add(I.Id);
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& I : P->StashItems)
    {
        WorkshopBeforeIds.Add(I.Id);
    }
}

/** 공방 비용 차감에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
bool UFableRPGValidationDriver::WorkshopPaid(const FFableResourceCost& C) const
{
    const AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    return P->Gold == WorkshopBeforeBalance.Gold - C.Gold && P->Ore == WorkshopBeforeBalance.Ore - C.Ore &&
           P->Essence == WorkshopBeforeBalance.Essence - C.Essence &&
           P->RootCores == WorkshopBeforeBalance.RootCores - C.RootCores &&
           P->MoonCrystals == WorkshopBeforeBalance.MoonCrystals - C.MoonCrystals;
}

/** 공방 기록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableRPGValidationDriver::WorkshopRecord(const FString& Action, int32 ItemId)
{
    // JSON 계약과 검증 기록에 필요한 값 준비
    const AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("action"), Action);
    J->SetNumberField(TEXT("item_id"), ItemId);
    J->SetNumberField(TEXT("step"), Step);
    J->SetObjectField(TEXT("before"), WorkshopBalanceJson(WorkshopBeforeBalance));
    J->SetObjectField(TEXT("after"), WorkshopBalanceJson(WorkshopBalance(P)));
    J->SetStringField(TEXT("item_before"),
                      WorkshopBeforeItem.Id > 0 ? FableRPG::ItemPersistentSignature(WorkshopBeforeItem) : TEXT(""));
    const FFableItem* I = P->FindItem(ItemId);

    // 이 단계에서 공방 기록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (!I)
    {
        I = P->FindStashItem(ItemId);
    }

    J->SetStringField(TEXT("item_after"), I ? FableRPG::ItemPersistentSignature(*I) : TEXT(""));

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (WorkshopBeforeItem.Id > 0)
    {
        J->SetObjectField(TEXT("item_before_data"), WorkshopItemJson(WorkshopBeforeItem));
    }

    // JSON 계약과 검증 기록 처리
    if (I)
    {
        J->SetObjectField(TEXT("item_after_data"), WorkshopItemJson(*I));
    }

    FFableResourceCost Expected;

    // 재화와 작업 비용 처리
    if (Action == TEXT("dismantle"))
    {
        Expected.Essence = -WorkshopYield;
    }
    else if (Action == TEXT("repair") || Action == TEXT("craft") || Action == TEXT("reroll") ||
             Action == TEXT("enhance_legendary"))
    {
        Expected = WorkshopCost;
    }

    // JSON 계약과 검증 기록 처리
    J->SetObjectField(TEXT("expected_cost"), WorkshopBalanceJson(Expected));
    WorkshopEvents.Add(MakeShared<FJsonValueObject>(J));
}

/** 제작·분해·옵션·수리·창고의 검증 단계를 진행한다. */
void UFableRPGValidationDriver::TickWorkshop()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFablePlayerState* P = PC->GetPlayerState<AFablePlayerState>();
    AFableCharacter* H = PC->GetPawn<AFableCharacter>();
    AFableGameState* G = GetWorld()->GetGameState<AFableGameState>();
    const double Now = GetWorld()->GetTimeSeconds(), Wait = Now - StepAt;
    const auto Fail = [&](const FString& Why)
    {
        PC->UIClosePanels();
        Capture(TEXT("workshop-failed"));
        Finish(false, Why);
    };
    const auto Go = [&](int32 N)
    {
        Advance(N);
    };

    // JSON 계약과 검증 기록 처리
    if (Now - LastWorkshopReport > 0.8)
    {
        Report(TEXT("running"), TEXT("Normal-input workshop, durability, grade and storage validation in progress."));
        LastWorkshopReport = Now;
    }

    // 생존 상태와 부활 진행 처리
    if (Phase == TEXT("resume"))
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 4)
        {
            return;
        }

        // 생존 상태와 부활 진행 처리
        if (!WorkshopChecks.Contains(TEXT("runtime_not_persisted")))
        {
            // 생존 상태와 부활 진행 조건 검사
            if (!H->IsAlive() || G->ActiveRegion != EFableRegion::Village || G->bPartyRecoveryPending ||
                G->bRetryAvailable || H->GetReviveTarget() || H->GetReviver() || PC->IsWorkshopOpen() ||
                PC->IsDismantleConfirmationPending())
            {
                Fail(TEXT("Workshop restart retained temporary interaction or combat state."));
                return;
            }
            if (P->StashItems.Num() != 1 || P->StashItems[0].Rarity != 3 ||
                P->GetEquippedSetCount(EFableEquipmentSet::Moonlight) != 2)
            {
                Fail(TEXT("Stored equipment or active set was lost."));
                return;
            }

            WorkshopChecks.Add(TEXT("runtime_not_persisted"), true);
            LootChecks.Add(TEXT("runtime_not_persisted"), true);

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (const FFableItem& I : P->Inventory)
            {
                if (I.Rarity == 4)
                {
                    WorkshopLegendId = I.Id;
                }
            }

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (WorkshopLegendId <= 0)
            {
                Fail(TEXT("The saved legendary item was lost."));
                return;
            }

            WorkshopStashId = P->StashItems[0].Id;
        }

        // 이동 입력과 속도 제한 처리
        if (WorkshopStage == 0)
        {
            // 이 단계에서 제작·분해·옵션·수리·창고의 검증 단계를 진행한다.
            if (PC->IsAnyPanelOpen())
            {
                PC->UIClosePanels();
            }

            // 이동 입력과 속도 제한 조건 검사
            if (!WalkTo(FableRPG::ForgeLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 90) ||
                H->GetVelocity().Size2D() > 10)
            {
                return;
            }

            PC->UIOpenWorkshop();
            PC->UISelectWorkshopTab(EFableWorkshopTab::Stash);
            PC->UISetStashWithdrawal(true);
            PC->UISelectWorkshopItem(WorkshopStashId);
            Capture(TEXT("restored-workshop-stash"));
            WorkshopAt = Now;
            WorkshopStage = 1;

            return;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - WorkshopAt < 1.1)
        {
            return;
        }

        // 이 단계에서 제작·분해·옵션·수리·창고의 검증 단계를 진행한다.
        if (WorkshopStage == 1)
        {
            PC->UISelectWorkshopTab(EFableWorkshopTab::Reroll);
            PC->UISelectWorkshopItem(WorkshopLegendId);
            Capture(TEXT("restored-workshop-legendary"));
            WorkshopStage = 2;
            WorkshopAt = Now;
            return;
        }

        Finish(
            true,
            TEXT(
                "The complete v6 equipment, durability, materials and personal storage restored exactly in new processes."));

        return;
    }

    // 지역·적·전투 진행 조건 검사
    if (!bWorkshopStarted)
    {
        TickLoot();
        return;
    }
    if (Wait > 220)
    {
        Fail(TEXT("Workshop step timed out."));
        return;
    }
    if (!H->IsAlive() || G->ActiveRegion != EFableRegion::Village)
    {
        Fail(TEXT("Workshop processing must take place alive in the village."));
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    switch (Step)
    {
    // 아이템·장비 옵션·인벤토리와 창고 처리
    case 100:
    {
        int32 Weapon = 0, Armor = 0;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->Inventory)
        {
            if (I.Durability < I.MaxDurability)
            {
                if (I.Slot == EFableEquipmentSlot::Weapon)
                {
                    Weapon = I.Id;
                }
                else
                {
                    Armor = I.Id;
                }
            }
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Weapon || !Armor)
        {
            Fail(TEXT("Actual dungeon hits did not wear both equipment slots."));
            return;
        }

        WorkshopChecks.Add(TEXT("actual_weapon_wear"), true);
        WorkshopChecks.Add(TEXT("actual_armor_wear"), true);
        WorkshopRepairIds = {Weapon, Armor};
        WorkshopRepairIndex = 0;
        WorkshopItemId = Weapon;
        WorkshopSnapshot();
        PC->UIRepair(WorkshopItemId);
        Go(101);
        break;
    }
    case 101:
        if (Wait < 0.8)
        {
            return;
        }
        if (Signature(P) != WorkshopBeforeSignature)
        {
            Fail(TEXT("Repair worked away from the forge."));
            return;
        }
        WorkshopChecks.Add(TEXT("remote_repair_rejected"), true);
        Go(102);
        break;
    case 102:
        if (PC->IsAnyPanelOpen())
        {
            PC->UIClosePanels();
        }
        if (WalkTo(FableRPG::ForgeLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 90) &&
            H->GetVelocity().Size2D() < 10)
        {
            PC->UIOpenWorkshop();
            Go(103);
        }
        break;
    case 103:
    {
        // 아이템·장비 옵션·인벤토리와 창고 처리
        WorkshopItemId = WorkshopRepairIds[WorkshopRepairIndex];
        const FFableItem* I = P->FindItem(WorkshopItemId);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!I || I->Durability >= I->MaxDurability)
        {
            Fail(TEXT("The worn item disappeared before repair."));
            return;
        }

        // 프로필과 저장 스냅숏 처리
        WorkshopBeforeItem = *I;
        WorkshopCost = P->GetRepairCost(I->Id);
        WorkshopSnapshot();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Repair);
        PC->UISelectWorkshopItem(I->Id);
        Capture(WorkshopRepairIndex == 0 ? TEXT("workshop-repair-before") : TEXT("workshop-armor-repair-before"));
        Go(104);
        break;
    }
    case 104:
        if (Wait > 1.0)
        {
            PC->UIRepair(WorkshopItemId);
            Go(105);
        }
        break;
    case 105:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        const FFableItem* I = P->FindItem(WorkshopItemId);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!I || I->Durability != I->MaxDurability || !WorkshopPaid(WorkshopCost))
        {
            Fail(TEXT("Repair result or resource cost mismatch."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Same = *I;
        Same.Durability = WorkshopBeforeItem.Durability;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (FableRPG::ItemPersistentSignature(Same) != FableRPG::ItemPersistentSignature(WorkshopBeforeItem))
        {
            Fail(TEXT("Repair altered original equipment fields."));
            return;
        }

        WorkshopRecord(TEXT("repair"), I->Id);
        WorkshopChecks.Add(TEXT("repair_preserves_item"), true);
        Capture(TEXT("workshop-repaired"));

        // 이 단계에서 제작·분해·옵션·수리·창고의 검증 단계를 진행한다.
        if (++WorkshopRepairIndex < WorkshopRepairIds.Num())
        {
            Go(106);
            break;
        }

        // 프로필과 저장 스냅숏 처리
        WorkshopSnapshot();
        PC->UIRepair(WorkshopItemId);
        Go(107);
        break;
    }
    case 106:
        if (Wait > 1.0)
        {
            Go(103);
        }
        break;
    case 107:
        if (Wait < 0.8)
        {
            return;
        }
        if (Signature(P) != WorkshopBeforeSignature)
        {
            Fail(TEXT("Full-durability repair spent resources."));
            return;
        }
        WorkshopChecks.Add(TEXT("full_repair_rejected"), true);
        PC->UISelectWorkshopTab(EFableWorkshopTab::Craft);
        PC->UISelectCraftRecipe(4, EFableEquipmentSlot::Weapon);
        WorkshopSnapshot();
        PC->UICraft();
        Go(108);
        break;
    case 108:
        if (Wait < 0.8)
        {
            return;
        }
        if (Signature(P) != WorkshopBeforeSignature)
        {
            Fail(TEXT("Crafting succeeded with insufficient materials."));
            return;
        }
        WorkshopChecks.Add(TEXT("insufficient_materials_rejected"), true);
        Capture(TEXT("workshop-craft-materials-needed"));
        Go(109);
        break;
    case 109:
        if (Wait > 1.0)
        {
            Go(110);
        }
        break;
    case 110:
    {
        // 재화와 작업 비용 처리
        if (P->Essence >= 130)
        {
            WorkshopGrade = 1;
            Go(120);
            break;
        }

        int32 Candidate = 0;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->Inventory)
        {
            if (P->GetDismantleYield(I.Id).Essence > 0 && !LootBossIds.Contains(I.Id) &&
                (!Candidate || (Role == TEXT("host") ? I.Id < Candidate : I.Id > Candidate)))
            {
                Candidate = I.Id;
            }
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Candidate)
        {
            Fail(TEXT("Not enough normally earned equipment to supply crafting materials."));
            return;
        }

        // 재화와 작업 비용 처리
        WorkshopItemId = Candidate;
        WorkshopBeforeItem = *P->FindItem(Candidate);
        WorkshopYield = P->GetDismantleYield(Candidate).Essence;
        WorkshopSnapshot();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Dismantle);
        PC->UISelectWorkshopItem(Candidate);
        PC->UIDismantle(Candidate);
        Go(111);
        break;
    }
    case 111:
        if (Wait < 0.8)
        {
            return;
        }
        if (!PC->IsDismantleConfirmationPending() || Signature(P) != WorkshopBeforeSignature)
        {
            Fail(TEXT("Dismantling did not require an unchanged confirmation step."));
            return;
        }
        if (!WorkshopChecks.FindRef(TEXT("dismantle_confirmation")))
        {
            WorkshopChecks.Add(TEXT("dismantle_confirmation"), true);
            Capture(TEXT("workshop-dismantle-confirm"));
            WorkshopAt = Now;
        }
        if (Now - WorkshopAt < 1.0)
        {
            return;
        }
        PC->UIDismantle(WorkshopItemId);
        Go(112);
        break;
    case 112:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        // 재화와 작업 비용에 필요한 값 준비
        FFableResourceCost C;
        C.Essence = -WorkshopYield;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (P->FindItem(WorkshopItemId) || !WorkshopPaid(C))
        {
            Fail(TEXT("Dismantle yield or removed item identity was incorrect."));
            return;
        }

        WorkshopRecord(TEXT("dismantle"), WorkshopItemId);
        WorkshopChecks.Add(TEXT("dismantle_yield"), true);
        Go(110);
        break;
    }
    case 120:
        if (WorkshopGrade > 4)
        {
            WorkshopChecks.Add(TEXT("all_grades_crafted"), true);
            Go(130);
            break;
        }
        WorkshopCost = P->GetCraftCost(WorkshopGrade);
        WorkshopBeforeItem = FFableItem();
        WorkshopSnapshot();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Craft);
        PC->UISelectCraftRecipe(WorkshopGrade,
                                EFableEquipmentSlot((WorkshopGrade + (Role == TEXT("host") ? 0 : 1)) % 2));
        PC->UICraft();
        Go(121);
        break;
    case 121:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const FFableItem* NewItem = nullptr;
        int32 Added = 0;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->Inventory)
        {
            if (!WorkshopBeforeIds.Contains(I.Id))
            {
                NewItem = &I;
                ++Added;
            }
        }

        const EFableEquipmentSlot ExpectedSlot =
            EFableEquipmentSlot((WorkshopGrade + (Role == TEXT("host") ? 0 : 1)) % 2);

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (Added != 1 || !NewItem || !WorkshopPaid(WorkshopCost) || NewItem->Rarity != WorkshopGrade ||
            NewItem->Slot != ExpectedSlot || NewItem->HeroClass != P->HeroClass ||
            NewItem->SetId != EFableEquipmentSet::None || !FableRPG::IsValidItemRoll(*NewItem) ||
            NewItem->RollVersion != (WorkshopGrade == 4 ? 2 : 1) || NewItem->DurabilityVersion != 1 ||
            NewItem->MaxDurability != FableRPG::GradeMaxDurability(WorkshopGrade) ||
            NewItem->Durability != NewItem->MaxDurability)
        {
            Fail(TEXT("Crafting did not spend exact costs for one correctly graded personal item."));
            return;
        }

        WorkshopItemId = NewItem->Id;
        WorkshopCraftIds.Add(NewItem->Id);

        // 이 단계에서 제작·분해·옵션·수리·창고의 검증 단계를 진행한다.
        if (WorkshopGrade == 4)
        {
            WorkshopLegendId = NewItem->Id;
        }

        // 이 단계에서 제작·분해·옵션·수리·창고의 검증 단계를 진행한다.
        if (WorkshopGrade == 3)
        {
            WorkshopStashId = NewItem->Id;
        }

        WorkshopRecord(TEXT("craft"), NewItem->Id);
        WorkshopChecks.Add(FString::Printf(TEXT("grade_%d_crafted"), WorkshopGrade), true);
        PC->UISelectWorkshopTab(EFableWorkshopTab::Reroll);
        PC->UISelectWorkshopItem(NewItem->Id);
        Capture(FString::Printf(TEXT("workshop-crafted-grade-%d"), WorkshopGrade));
        Go(122);
        break;
    }
    case 122:
        if (Wait > 1.0)
        {
            ++WorkshopGrade;
            Go(120);
        }
        break;
    case 130:
    {
        const FFableItem* I = P->FindItem(WorkshopLegendId);

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!I || I->Affixes.IsEmpty())
        {
            Fail(TEXT("The new legendary item has no rerollable option."));
            return;
        }

        // 프로필과 저장 스냅숏 처리
        WorkshopBeforeItem = *I;
        WorkshopCost = P->GetRerollCost(I->Id);
        WorkshopSnapshot();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Reroll);
        PC->UISelectWorkshopItem(I->Id);
        PC->UISelectAffix(0);
        Capture(TEXT("workshop-option-before"));
        Go(131);
        break;
    }
    case 131:
        if (Wait > 1.0)
        {
            PC->UIRerollAffix(WorkshopLegendId, 0);
            Go(132);
        }
        break;
    case 132:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        const FFableItem* I = P->FindItem(WorkshopLegendId);

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!I || I->Affixes.Num() != WorkshopBeforeItem.Affixes.Num() || !FableRPG::IsValidItemRoll(*I) ||
            !WorkshopPaid(WorkshopCost))
        {
            Fail(TEXT("Option reroll cost, count or validity changed incorrectly."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const bool Changed = I->Affixes[0].Stat != WorkshopBeforeItem.Affixes[0].Stat ||
                             I->Affixes[0].Value != WorkshopBeforeItem.Affixes[0].Value;
        FFableItem Same = *I;
        Same.Affixes[0] = WorkshopBeforeItem.Affixes[0];

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Changed ||
            FableRPG::ItemPersistentSignature(Same) != FableRPG::ItemPersistentSignature(WorkshopBeforeItem))
        {
            Fail(TEXT("Reroll changed fields outside the selected option, or returned the same option."));
            return;
        }

        WorkshopRecord(TEXT("reroll"), I->Id);
        WorkshopChecks.Add(TEXT("selected_option_only"), true);
        Capture(TEXT("workshop-option-rerolled"));
        WorkshopStage = 0;
        Go(133);
        break;
    }
    case 133:
        if (Wait > 1.0)
        {
            Go(140);
        }
        break;
    case 140:
        WorkshopSnapshot();
        if (WorkshopStage == 0)
        {
            PC->UIDismantle(P->EquippedWeaponId);
        }
        else if (WorkshopStage == 1)
        {
            PC->UIDeposit(1);
        }
        else if (WorkshopStage == 2)
        {
            PC->UIDeposit(P->EquippedWeaponId);
        }
        else if (WorkshopStage == 3)
        {
            PC->UIWithdraw(999999);
        }
        else
        {
            PC->UIRerollAffix(WorkshopLegendId, 99);
        }
        Go(141);
        break;
    case 141:
        if (Wait < 0.8)
        {
            return;
        }
        if (Signature(P) != WorkshopBeforeSignature)
        {
            Fail(TEXT("A protected or invalid workshop action changed the profile."));
            return;
        }
        if (++WorkshopStage < 5)
        {
            Go(140);
            break;
        }
        WorkshopChecks.Add(TEXT("protected_actions_rejected"), true);
        Go(150);
        break;
    case 150:
    {
        const FFableItem* I = P->FindItem(WorkshopLegendId);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!I)
        {
            Fail(TEXT("Legendary equipment disappeared before storage."));
            return;
        }

        // 프로필과 저장 스냅숏 처리
        WorkshopBeforeItem = *I;
        WorkshopSnapshot();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Stash);
        PC->UISetStashWithdrawal(false);
        PC->UISelectWorkshopItem(I->Id);
        PC->UIDeposit(I->Id);
        Go(151);
        break;
    }
    case 151:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const FFableItem* I = P->FindStashItem(WorkshopLegendId);
        FFableResourceCost Zero;

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (P->FindItem(WorkshopLegendId) || !I ||
            FableRPG::ItemPersistentSignature(*I) != FableRPG::ItemPersistentSignature(WorkshopBeforeItem) ||
            !WorkshopPaid(Zero))
        {
            Fail(TEXT("Storage deposit lost, duplicated or rerolled the item."));
            return;
        }

        // 프로필과 저장 스냅숏 처리
        WorkshopRecord(TEXT("deposit"), I->Id);
        WorkshopChecks.Add(TEXT("deposit_identity_preserved"), true);
        WorkshopSnapshot();
        PC->UIDeposit(WorkshopLegendId);
        Go(152);
        break;
    }
    case 152:
        if (Wait < 0.8)
        {
            return;
        }
        if (Signature(P) != WorkshopBeforeSignature)
        {
            Fail(TEXT("Repeated storage deposit duplicated or charged an item."));
            return;
        }
        WorkshopChecks.Add(TEXT("duplicate_deposit_rejected"), true);
        PC->UISetStashWithdrawal(true);
        PC->UISelectWorkshopItem(WorkshopLegendId);
        WorkshopBeforeItem = *P->FindStashItem(WorkshopLegendId);
        WorkshopSnapshot();
        PC->UIWithdraw(WorkshopLegendId);
        Go(153);
        break;
    case 153:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const FFableItem* I = P->FindItem(WorkshopLegendId);
        FFableResourceCost Zero;

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (P->FindStashItem(WorkshopLegendId) || !I ||
            FableRPG::ItemPersistentSignature(*I) != FableRPG::ItemPersistentSignature(WorkshopBeforeItem) ||
            !WorkshopPaid(Zero))
        {
            Fail(TEXT("Storage withdrawal lost, duplicated or rerolled the item."));
            return;
        }

        WorkshopRecord(TEXT("withdraw"), I->Id);
        WorkshopChecks.Add(TEXT("withdraw_identity_preserved"), true);
        PC->UISetStashWithdrawal(false);
        PC->UISelectWorkshopItem(WorkshopStashId);
        WorkshopBeforeItem = *P->FindItem(WorkshopStashId);
        WorkshopSnapshot();
        PC->UIDeposit(WorkshopStashId);
        Go(154);
        break;
    }
    case 154:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const FFableItem* I = P->FindStashItem(WorkshopStashId);
        FFableResourceCost Zero;

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (P->StashItems.Num() != 1 || !I || P->FindItem(WorkshopStashId) || !WorkshopPaid(Zero))
        {
            Fail(TEXT("The item intended for saved personal storage was not deposited."));
            return;
        }

        WorkshopRecord(TEXT("deposit_saved"), I->Id);
        PC->UISetStashWithdrawal(true);
        PC->UISelectWorkshopItem(I->Id);
        Capture(TEXT("workshop-stash-saved"));
        Go(155);
        break;
    }
    case 155:
        if (Wait > 1.0)
        {
            PC->UIClosePanels();
            Go(160);
        }
        break;
    case 160:
        WorkshopBeforeItem = *P->FindItem(WorkshopLegendId);
        WorkshopSnapshot();
        WorkshopCost = FFableResourceCost();
        WorkshopCost.Gold = P->GetUpgradeGoldCost(WorkshopLegendId);
        WorkshopCost.Ore = P->GetUpgradeOreCost(WorkshopLegendId);
        PC->UIEnhance(WorkshopLegendId);
        Go(161);
        break;
    case 161:
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 0.8)
        {
            return;
        }

        const FFableItem* I = P->FindItem(WorkshopLegendId);

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!I || I->Upgrade != WorkshopBeforeItem.Upgrade + 1 || !WorkshopPaid(WorkshopCost))
        {
            Fail(TEXT("Legendary enhancement did not apply its exact normal costs."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Same = *I;
        Same.Upgrade = WorkshopBeforeItem.Upgrade;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (FableRPG::ItemPersistentSignature(Same) != FableRPG::ItemPersistentSignature(WorkshopBeforeItem))
        {
            Fail(TEXT("Legendary enhancement changed quality, option or durability fields."));
            return;
        }

        WorkshopRecord(TEXT("enhance_legendary"), I->Id);
        WorkshopChecks.Add(TEXT("legendary_enhancement_preserved"), true);
        PC->UIToggleInventory();
        PC->UISelectItem(I->Id);
        Capture(TEXT("workshop-legendary-enhanced"));
        Go(162);
        break;
    }
    case 162:
        if (Wait > 1.0)
        {
            PC->UIClosePanels();
            Go(170);
        }
        break;
    case 170:
    {
        TSet<int32> Ids;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->Inventory)
        {
            if (Ids.Contains(I.Id))
            {
                Fail(TEXT("Duplicate inventory ID after workshop processing."));
                return;
            }
            Ids.Add(I.Id);
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->StashItems)
        {
            if (Ids.Contains(I.Id))
            {
                Fail(TEXT("An item exists in both inventory and storage."));
                return;
            }
            Ids.Add(I.Id);
        }

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (P->RootCores != 1 || P->MoonCrystals != 0 || WorkshopCraftIds.Num() != 4 || P->StashItems.Num() != 1 ||
            !P->bLastSaveSucceeded)
        {
            Fail(TEXT("Boss materials, crafted items or final persisted storage are incomplete."));
            return;
        }

        WorkshopChecks.Add(TEXT("boss_materials_spent_exactly"), true);
        WorkshopChecks.Add(TEXT("unique_storage_ids"), true);
        PC->UIOpenWorkshop();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Reroll);
        PC->UISelectWorkshopItem(WorkshopLegendId);
        Capture(TEXT("workshop-final-equipment"));
        Go(171);
        break;
    }
    case 171:
        if (Wait > 1.2)
        {
            Finish(
                true,
                TEXT(
                    "Four actual dungeons and v5 migration, equipment wear/repair, salvage, every crafted grade, one-option reroll, protected operations, personal storage and legendary enhancement passed using normal game inputs."));
        }
        break;
    default:
        Fail(TEXT("Unknown workshop validation step."));
        break;
    }
}

/** 공방 보고서 항목을 최종 검증 JSON에 추가한다. */
void UFableRPGValidationDriver::AppendWorkshopReport(TSharedRef<FJsonObject> J) const
{
    const AFablePlayerState* P = PC.IsValid() ? PC->GetPlayerState<AFablePlayerState>() : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!P)
    {
        return;
    }

    // JSON 계약과 검증 기록 처리
    J->SetBoolField(TEXT("workshop_enabled"), true);
    J->SetBoolField(TEXT("workshop_started"), bWorkshopStarted);
    J->SetNumberField(TEXT("workshop_stage"), WorkshopStage);
    J->SetStringField(TEXT("workshop_signature"), AdventureSignature(P));
    J->SetObjectField(TEXT("materials"), WorkshopBalanceJson(WorkshopBalance(P)));
    J->SetNumberField(TEXT("workshop_legendary_id"), WorkshopLegendId);
    J->SetNumberField(TEXT("workshop_stored_id"), WorkshopStashId);
    TSharedRef<FJsonObject> C = MakeShared<FJsonObject>();

    // JSON 계약과 검증 기록 순회
    for (const auto& Pair : WorkshopChecks)
    {
        C->SetBoolField(Pair.Key, Pair.Value);
    }

    // JSON 계약과 검증 기록 처리
    J->SetObjectField(TEXT("workshop_checks"), C);
    TArray<TSharedPtr<FJsonValue>> Stash;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& I : P->StashItems)
    {
        Stash.Add(MakeShared<FJsonValueObject>(WorkshopItemJson(I)));
    }

    // JSON 계약과 검증 기록 처리
    J->SetArrayField(TEXT("stash_items"), Stash);
    TArray<TSharedPtr<FJsonValue>> Initial;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& I : WorkshopInitialItems)
    {
        Initial.Add(MakeShared<FJsonValueObject>(WorkshopItemJson(I)));
    }

    // JSON 계약과 검증 기록 처리
    J->SetArrayField(TEXT("initial_v5_inventory"), Initial);
    TArray<TSharedPtr<FJsonValue>> Crafted;

    // JSON 계약과 검증 기록 순회
    for (int32 Id : WorkshopCraftIds)
    {
        Crafted.Add(MakeShared<FJsonValueNumber>(Id));
    }

    J->SetArrayField(TEXT("workshop_crafted_ids"), Crafted);
    J->SetArrayField(TEXT("workshop_events"), WorkshopEvents);
}
