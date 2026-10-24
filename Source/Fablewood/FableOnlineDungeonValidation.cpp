#include "FableOnlineValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "FableInventorySnapshot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Dom/JsonObject.h"

/** 던전 입력 유지에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableOnlineValidationDriver::DungeonHold(AFablePlayerController* PC, const FKey& Key, bool Held)
{
    if (PC && PC->IsInputKeyDown(Key) != Held)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Held ? IE_Pressed : IE_Released, Held ? 1.f : 0.f, 0,
                                                         FInputDeviceId::CreateFromInternalId(0)));
    }
}

/** 던전 키에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableOnlineValidationDriver::DungeonKey(AFablePlayerController* PC, const FKey& Key)
{
    if (!PC)
    {
        return;
    }
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f, 0, FInputDeviceId::CreateFromInternalId(0)));
    DungeonReleaseKeys.AddUnique(Key);
}

/** 던전 해제 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableOnlineValidationDriver::DungeonReleaseInput()
{
    auto* PC =
        GetGameInstance() ? Cast<AFablePlayerController>(GetGameInstance()->GetFirstLocalPlayerController()) : nullptr;

    // 접속 권한·저장 리비전·멱등 키 처리
    if (PC)
    {
        for (const auto& Key : DungeonReleaseKeys)
        {
            PC->InputKey(
                FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f, 0, FInputDeviceId::CreateFromInternalId(0)));
        }
    }

    DungeonReleaseKeys.Reset();
}

/** 던전 검증의 목표 위치까지 일반 이동 입력을 전달한다. */
bool UFableOnlineValidationDriver::DungeonWalk(AFablePlayerController* PC, const FVector& Location, float Radius)
{
    auto* Hero = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !Hero->IsAlive())
    {
        return false;
    }

    // 검증 입력과 키 유지 상태에 필요한 값 준비
    const FVector Delta = Location - Hero->GetActorLocation();
    DungeonHold(PC, EKeys::S, false);

    // 검증 입력과 키 유지 상태 조건 검사
    if (Delta.Size2D() <= Radius)
    {
        DungeonHold(PC, EKeys::W, false);
        return true;
    }

    // 검증 입력과 키 유지 상태 처리
    PC->SetControlRotation(FRotator(-12, Delta.Rotation().Yaw, 0));
    DungeonHold(PC, EKeys::W, true);

    return false;
}

/** 던전 대상에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
AFableEnemy* UFableOnlineValidationDriver::DungeonTarget(AFableCharacter* Hero) const
{
    // 지역·적·전투 진행에 필요한 값 준비
    AFableEnemy* Target = nullptr;
    float Distance = MAX_flt;
    const auto* GS = Hero ? Hero->GetWorld()->GetGameState<AFableGameState>() : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!GS)
    {
        return nullptr;
    }

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(Hero->GetWorld()); It; ++It)
    {
        const float D = FVector::Dist2D(Hero->GetActorLocation(), It->GetActorLocation());
        if (It->IsAlive() && It->GetEncounterRegion() == GS->ActiveRegion && D < Distance)
        {
            Distance = D;
            Target = *It;
        }
    }

    return Target;
}

/** 던전 퀘스트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableOnlineValidationDriver::DungeonQuest(AFablePlayerController* PC, EFableQuestId Quest, double Now)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    auto* Hero = Cast<AFableCharacter>(PC->GetPawn());
    const FVector Board = FableRPG::BoardLocation() + FVector(Role == TEXT("client1") ? -100 : 100, -120, 100);

    // 위치·방향·거리 계산 처리
    if (PC->IsAnyPanelOpen() && FVector::DistSquared2D(Hero->GetActorLocation(), Board) > 10000)
    {
        PC->UIClosePanels();
    }

    // 이동 입력과 속도 제한 조건 검사
    if (!DungeonWalk(PC, Board, 85) || Hero->GetVelocity().Size2D() > 10)
    {
        DungeonActionAt = Now;
        return;
    }
    if (Now - DungeonActionAt < .8)
    {
        return;
    }

    // 퀘스트와 이야기 진행 처리
    if (PC->GetDialogue() == EFableDialogue::Guide)
    {
        PC->UISelectQuest(Quest);
        PC->UIConfirmQuest();
    }
    else
    {
        PC->UIClosePanels();
        DungeonKey(PC, EKeys::E);
    }

    DungeonActionAt = Now;
}

/** 던전 검증 대상에게 일반 전투 입력을 전달한다. */
void UFableOnlineValidationDriver::DungeonFight(AFablePlayerController* PC, double Now)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    auto* Hero = Cast<AFableCharacter>(PC->GetPawn());
    auto* P = PC->GetPlayerState<AFablePlayerState>();
    auto* Target = DungeonTarget(Hero);

    // 이 단계에서 던전 검증 대상에게 일반 전투 입력을 전달한다.
    if (PC->IsAnyPanelOpen())
    {
        PC->UIClosePanels();
    }

    // 검증 입력과 키 유지 상태 조건 검사
    if (!Hero || !Hero->IsAlive() || !Target)
    {
        DungeonHold(PC, EKeys::W, false);
        DungeonHold(PC, EKeys::S, false);
        return;
    }

    // 검증 입력과 키 유지 상태에 필요한 값 준비
    const float Distance = FVector::Dist2D(Hero->GetActorLocation(), Target->GetActorLocation());
    PC->SetControlRotation(FRotator(-12, (Target->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
    DungeonHold(PC, EKeys::W, Distance > 570);
    DungeonHold(PC, EKeys::S, Distance < 350);

    // 체력·마나·보호막·피해량 처리
    if (P && Hero->GetHealth() < Hero->GetMaxHealth() * .45f && P->HealthPotions > 0 &&
        P->GetPotionCooldownRemaining() <= 0 && Now - DungeonActionAt > 1)
    {
        PC->UIUseHealthPotion();
        DungeonActionAt = Now;
    }

    // 검증 입력과 키 유지 상태 처리
    if (Distance < 1050 && Now - DungeonAttackAt > .65)
    {
        DungeonKey(PC, EKeys::J);
        DungeonAttackAt = Now;
    }

    // 직업·스킬·특성 빌드 처리
    if (Distance < 850 && Now - DungeonSkillAt > 6.5 && Hero->GetMana() >= Hero->GetSkillManaCost(0))
    {
        DungeonKey(PC, EKeys::K);
        DungeonSkillAt = Now;
    }
}

/** 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다. */
bool UFableOnlineValidationDriver::TickDungeon(AFablePlayerController* PC, AFablePlayerState* P, AFableCharacter* Hero,
                                               double Now)
{
    auto* GS = Hero->GetWorld()->GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!GS)
    {
        return false;
    }
    if (Scenario == TEXT("reconnect") && bResumeExpedition)
    {
        return TickReconnect(PC, P, Hero, Now);
    }

    // 프로필과 저장 스냅숏 처리
    if (!DungeonInitial)
    {
        DungeonInitial = Profile(P);
        DungeonStepAt = Now;
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    const auto Advance = [&](int32 Next)
    {
        DungeonHold(PC, EKeys::W, false);
        DungeonHold(PC, EKeys::S, false);
        DungeonHold(PC, EKeys::E, false);
        PC->UIClosePanels();
        DungeonStep = Next;
        DungeonStepAt = Now;
        DungeonActionAt = -10;
    };
    const auto Party = [&](EFableQuestId Quest, EFableQuestStage Stage)
    {
        if (GS->PlayerArray.Num() != 2)
        {
            return false;
        }
        for (APlayerState* Entry : GS->PlayerArray)
        {
            const auto* Other = Cast<AFablePlayerState>(Entry);
            if (!Other || Other->GetQuestStage(Quest) != Stage)
            {
                return false;
            }
        }
        return true;
    };

    // 생존 상태와 부활 진행 조건 검사
    if (Now - DungeonStepAt > (Scenario == TEXT("reconnect") ? 360 : 180))
    {
        Finish(false, FString::Printf(TEXT("Online dungeon step %d timed out."), DungeonStep));
        return false;
    }
    if (GS->bPartyRecoveryPending || Hero->IsDefeated())
    {
        Finish(false, TEXT("The online dungeon party required automatic recovery."));
        return false;
    }

    auto* Target = DungeonTarget(Hero);

    // 퀘스트와 이야기 진행 처리
    switch (DungeonStep)
    {
    // 퀘스트와 이야기 진행 처리
    case 0:
        if (P->ForestQuest == EFableQuestStage::Active)
        {
            Advance(1);
            break;
        }
        DungeonQuest(PC, EFableQuestId::Forest, Now);
        break;
    case 1:
        if (Party(EFableQuestId::Forest, EFableQuestStage::Active))
        {
            Checks.Add(TEXT("both_forest_quests_active"), true);
            Advance(2);
        }
        break;
    case 2:
        if (P->ForestQuest == EFableQuestStage::Ready)
        {
            Advance(3);
            break;
        }
        DungeonFight(PC, Now);
        break;
    case 3:
        if (P->ForestQuest == EFableQuestStage::Completed)
        {
            Advance(4);
            break;
        }
        DungeonQuest(PC, EFableQuestId::Forest, Now);
        break;
    case 4:
        if (!P->IsBackendReady() || P->IsBackendSavePending())
        {
            break;
        }
        if (DungeonWeaponId == 0)
        {
            for (const auto& Item : P->Inventory)
            {
                if (Item.Rarity == 1 && Item.Slot == EFableEquipmentSlot::Weapon && Item.HeroClass == P->HeroClass)
                {
                    DungeonWeaponId = Item.Id;
                    PC->UIEquip(Item.Id);
                    break;
                }
            }
        }
        else if (P->EquippedWeaponId == DungeonWeaponId)
        {
            Checks.Add(TEXT("forest_reward_equipped"), true);
            Advance(5);
        }
        break;
    case 5:
        if (P->DungeonQuest == EFableQuestStage::Active)
        {
            Advance(6);
            break;
        }
        DungeonQuest(PC, EFableQuestId::RootRuins, Now);
        break;
    case 6:
    {
        // 지역·적·전투 진행 처리
        if (GS->ActiveRegion == EFableRegion::RootRuins &&
            FableRPG::IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()))
        {
            Checks.Add(TEXT("entered_root_ruins"), true);
            Capture(TEXT("dungeon-entry"));
            Advance(7);
            break;
        }

        // 퀘스트와 이야기 진행 처리
        if (!Party(EFableQuestId::RootRuins, EFableQuestStage::Active))
        {
            break;
        }

        const FVector Portal = FableRPG::PortalLocation() + FVector(Role == TEXT("client1") ? -80 : 80, -100, 100);

        // 위치·방향·거리 계산 처리
        if (PC->IsAnyPanelOpen() && FVector::DistSquared2D(Hero->GetActorLocation(), Portal) > 10000)
        {
            PC->UIClosePanels();
        }

        // 이동 입력과 속도 제한 처리
        if (!DungeonWalk(PC, Portal, 100) || Hero->GetVelocity().Size2D() > 10)
        {
            DungeonActionAt = Now;
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Now - DungeonActionAt < 1)
        {
            break;
        }

        // 지역·적·전투 진행 처리
        if (PC->IsPortalOpen())
        {
            PC->UISelectDungeon(EFableRegion::RootRuins);
            PC->UISelectDifficulty(EFableDifficulty::Normal);
            PC->UIEnterSelectedDungeon();
        }
        else
        {
            PC->UIClosePanels();
            DungeonKey(PC, EKeys::E);
        }

        DungeonActionAt = Now;
        break;
    }
    case 7:
        if (Scenario == TEXT("reconnect"))
        {
            return TickReconnect(PC, P, Hero, Now);
        }
        if (GS->DungeonWave >= 2)
        {
            Checks.Add(TEXT("second_wave"), true);
        }
        if (GS->AdventureStage == EFableAdventureStage::Boss)
        {
            Checks.Add(TEXT("boss_seen"), true);
            if (Target && Target->IsBoss() && Target->GetHealth() <= Target->GetMaxHealth() * .70f)
            {
                Capture(TEXT("boss-before-rescue"));
                Advance(8);
                break;
            }
        }
        DungeonFight(PC, Now);
        break;
    case 8:
    {
        AFableCharacter* Fallen = nullptr;

        // 현재 월드와 플레이어 참조 순회
        for (APlayerState* Entry : GS->PlayerArray)
        {
            auto* Other = Cast<AFablePlayerState>(Entry);
            if (Other && Other->HeroClass == EFableClass::Archer)
            {
                Fallen = Cast<AFableCharacter>(Other->GetPawn());
            }
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Fallen)
        {
            break;
        }

        // 생존 상태와 부활 진행 처리
        if (Fallen->IsDowned())
        {
            Checks.Add(TEXT("ally_downed"), true);
            Capture(TEXT("ally-downed"));
            Advance(9);
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (PC->IsAnyPanelOpen())
        {
            PC->UIClosePanels();
        }

        // 지역·적·전투 진행 처리
        if (Hero == Fallen)
        {
            if (Target)
            {
                DungeonWalk(PC, Target->GetActorLocation() + FVector(70, -90, 0), 80);
            }
        }
        else
        {
            DungeonWalk(PC, FableRPG::RegionEntrance(EFableRegion::RootRuins) + FVector(-350, -300, 100), 100);
        }

        break;
    }
    case 9:
        if (GS->AdventureStage != EFableAdventureStage::Cleared)
        {
            DungeonFight(PC, Now);
            break;
        }
        Checks.Add(TEXT("boss_defeated"), true);
        Advance(10);
        break;
    case 10:
    {
        AFableCharacter* Fallen = nullptr;

        // 현재 월드와 플레이어 참조 순회
        for (APlayerState* Entry : GS->PlayerArray)
        {
            auto* Other = Cast<AFablePlayerState>(Entry);
            if (Other && Other->HeroClass == EFableClass::Archer)
            {
                Fallen = Cast<AFableCharacter>(Other->GetPawn());
            }
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Fallen)
        {
            break;
        }

        // 생존 상태와 부활 진행 처리
        if (Fallen->IsAlive() && Fallen->GetTimesRevivedCount() > 0)
        {
            Checks.Add(TEXT("ally_revived"), true);
            Capture(TEXT("ally-revived"));
            Advance(11);
            break;
        }

        // 생존 상태와 부활 진행 처리
        if (Fallen->IsDefeated())
        {
            Finish(false, TEXT("The ally revive window expired."));
            break;
        }

        // 이동 입력과 속도 제한 처리
        if (Hero != Fallen)
        {
            if (!DungeonWalk(PC, Fallen->GetActorLocation(), 170) || Hero->GetVelocity().Size2D() > 10)
            {
                break;
            }
            if (Hero->GetReviveTarget() == Fallen)
            {
                DungeonHold(PC, EKeys::E, true);
            }
            else if (Now - DungeonActionAt > 1)
            {
                DungeonHold(PC, EKeys::E, false);
                PC->UIStartRevive(Fallen);
                DungeonActionAt = Now;
            }
        }

        break;
    }
    case 11:
        if (!P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded ||
            P->GetBackendRevision() != P->GetBackendAcknowledgedRevision())
        {
            break;
        }
        if (!Party(EFableQuestId::RootRuins, EFableQuestStage::Ready))
        {
            break;
        }
        if (P->Gold != 383 || P->Ore != 21 || P->RootCores != 1 || P->Level != 4 || P->Experience != 65 ||
            P->DungeonCompletions != 0 || P->Inventory.Num() != DungeonInitial->GetIntegerField(TEXT("bagCount")) + 2)
        {
            Finish(false, TEXT("Dungeon rewards do not match three forest kills, six guardians and one boss."));
            break;
        }
        Checks.Add(TEXT("server_reward_totals"), true);
        if (Scenario == TEXT("dungeon"))
        {
            return true;
        }
        if (Now - DungeonStepAt >= 2)
        {
            Advance(12);
        }
        break;
    case 12:
        if (GS->ActiveRegion == EFableRegion::Village &&
            FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation()))
        {
            Advance(13);
            break;
        }
        if (PC->IsAnyPanelOpen())
        {
            PC->UIClosePanels();
        }
        if (DungeonWalk(PC,
                        FableRPG::RegionReturnPortal(EFableRegion::RootRuins) +
                            FVector(Role == TEXT("client1") ? -80 : 80, 0, 100),
                        100) &&
            Hero->GetVelocity().Size2D() < 10 && Now - DungeonActionAt > 1)
        {
            DungeonKey(PC, EKeys::E);
            DungeonActionAt = Now;
        }
        break;
    case 13:
        if (P->DungeonQuest == EFableQuestStage::Completed && P->DungeonCompletions == 1)
        {
            // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
            if (P->IsBackendSavePending() || !P->bLastSaveSucceeded)
            {
                break;
            }

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (P->Gold != 533 || P->Ore != 29 || P->Level != 4 || P->Experience != 265 || P->Inventory.Num() != 7)
            {
                Finish(false, TEXT("The RootRuins quest reward was not granted exactly once."));
                break;
            }

            Checks.Add(TEXT("root_quest_reward_claimed"), true);
            Advance(14);
            break;
        }
        DungeonQuest(PC, EFableQuestId::RootRuins, Now);
        break;
    case 14:
    {
        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (P->IsBackendSavePending() || !P->bLastSaveSucceeded)
        {
            break;
        }

        int32 NewRolls = 0;
        TSet<FGuid> Identities;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const auto& Item : P->Inventory)
        {
            // 아이템·장비 옵션·인벤토리와 창고 조건 검사
            if (!Item.InstanceId.IsValid() || Identities.Contains(Item.InstanceId))
            {
                Finish(false, TEXT("A reward lost its unique item identity."));
                return false;
            }

            Identities.Add(Item.InstanceId);

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Item.Id > 4)
            {
                if (Item.RollVersion != 1 || !FableRPG::IsValidItemRoll(Item) || !FableRPG::IsValidItemDurability(Item))
                {
                    Finish(false, TEXT("A genuine quest/boss reward has invalid roll or durability data."));
                    return false;
                }
                ++NewRolls;
            }

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Item.Rarity == 2 && Item.Slot == EFableEquipmentSlot::Weapon && Item.HeroClass == P->HeroClass)
            {
                GrowthWeaponId = Item.Id;
            }

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Item.Rarity == 2 && Item.Slot == EFableEquipmentSlot::Armor &&
                Item.SetId == EFableEquipmentSet::RootGuardian)
            {
                GrowthArmorId = Item.Id;
            }
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (NewRolls != 3 || GrowthWeaponId <= 4 || GrowthArmorId <= 4 || GrowthWeaponId == GrowthArmorId)
        {
            Finish(false, TEXT("The three genuinely earned random equipment rewards are missing."));
            break;
        }

        Checks.Add(TEXT("random_reward_rolls_valid"), true);

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (P->EquippedWeaponId != GrowthWeaponId)
        {
            if (Now - DungeonActionAt > 1)
            {
                PC->UIEquip(GrowthWeaponId);
                DungeonActionAt = Now;
            }
            break;
        }

        Advance(15);
        break;
    }
    case 15:
        if (P->IsBackendSavePending() || !P->bLastSaveSucceeded)
        {
            break;
        }
        if (P->EquippedArmorId != GrowthArmorId)
        {
            if (Now - DungeonActionAt > 1)
            {
                PC->UIEquip(GrowthArmorId);
                DungeonActionAt = Now;
            }
            break;
        }
        Checks.Add(TEXT("new_reward_equipment"), true);
        if (!Checks.FindRef(TEXT("growth_equipment_captured")))
        {
            PC->UIToggleInventory();
            PC->UISelectItem(GrowthWeaponId);
            Capture(TEXT("growth-equipped"));
            Checks.Add(TEXT("growth_equipment_captured"), true);
            DungeonActionAt = Now;
            break;
        }
        if (Now - DungeonActionAt > .6)
        {
            Advance(16);
        }
        break;
    case 16:
        if (PC->IsAnyPanelOpen())
        {
            PC->UIClosePanels();
        }
        if (DungeonWalk(PC, FableRPG::ForgeLocation() + FVector(Role == TEXT("client1") ? -80 : 80, -100, 100), 90) &&
            Hero->GetVelocity().Size2D() < 10)
        {
            Advance(17);
        }
        break;
    case 17:
    {
        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Checks.FindRef(TEXT("normal_enhance_saved_once")))
        {
            if (Now - DungeonActionAt > .6)
            {
                Advance(18);
            }
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded)
        {
            break;
        }

        const auto* Item = P->FindItem(GrowthWeaponId);

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Item)
        {
            Finish(false, TEXT("The equipped random weapon disappeared."));
            break;
        }

        // 재화와 작업 비용 처리
        if (DungeonActionAt < 0)
        {
            // 재화와 작업 비용 처리
            GrowthBeforeItem = *Item;
            GrowthGoldBefore = P->Gold;
            GrowthOreBefore = P->Ore;
            GrowthGoldCost = P->GetUpgradeGoldCost(Item->Id);
            GrowthOreCost = P->GetUpgradeOreCost(Item->Id);

            // 재화와 작업 비용 처리
            if (Item->Upgrade != 0 || GrowthGoldCost != 30 || GrowthOreCost != 2)
            {
                Finish(false, TEXT("Unexpected first enhancement cost."));
                break;
            }

            // 재화와 작업 비용 처리
            GrowthReport = MakeShared<FJsonObject>();
            GrowthReport->SetNumberField(TEXT("goldAfterQuest"), P->Gold);
            GrowthReport->SetNumberField(TEXT("oreAfterQuest"), P->Ore);
            GrowthReport->SetNumberField(TEXT("enhanceGoldCost"), GrowthGoldCost);
            GrowthReport->SetNumberField(TEXT("enhanceOreCost"), GrowthOreCost);
            GrowthReport->SetObjectField(TEXT("enhanceBefore"), FableInventorySnapshot::MakeItem(*Item, 0, 0));
            PC->UIToggleInventory();
            PC->UISelectItem(Item->Id);
            Capture(TEXT("enhance-before"));
            DungeonActionAt = Now;
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Now - DungeonActionAt < .8)
        {
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Checks.FindRef(TEXT("enhance_request_sent")))
        {
            PC->UIEnhance(Item->Id);
            Checks.Add(TEXT("enhance_request_sent"), true);
            DungeonActionAt = Now;
            break;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Item->Upgrade == GrowthBeforeItem.Upgrade + 1)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            auto ExpectedItem = GrowthBeforeItem;
            ++ExpectedItem.Upgrade;

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Item->InstanceId != GrowthBeforeItem.InstanceId ||
                FableRPG::ItemPersistentSignature(*Item) != FableRPG::ItemPersistentSignature(ExpectedItem) ||
                P->Gold != GrowthGoldBefore - GrowthGoldCost || P->Ore != GrowthOreBefore - GrowthOreCost)
            {
                Finish(false, TEXT("Enhancement altered a roll/identity or charged an incorrect amount."));
                break;
            }

            // JSON 계약과 검증 기록 처리
            GrowthReport->SetObjectField(TEXT("enhanceAfter"), FableInventorySnapshot::MakeItem(*Item, 0, 0));
            Checks.Add(TEXT("normal_enhance_saved_once"), true);
            Capture(TEXT("enhance-after"));
            DungeonActionAt = Now;
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Now - DungeonStepAt > 12)
        {
            Finish(false, TEXT("The normal enhancement was not acknowledged."));
        }

        break;
    }
    case 18:
    {
        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (P->IsBackendSavePending() || !P->bLastSaveSucceeded)
        {
            break;
        }

        const auto* Item = P->FindItem(DungeonWeaponId);

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Item)
        {
            Finish(false, TEXT("The combat-worn forest weapon disappeared."));
            break;
        }

        const int32 Missing = Item->MaxDurability - Item->Durability;

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Missing <= 0 || !P->GetRepairBlockReason(Item->Id).IsEmpty())
        {
            Finish(false, TEXT("The genuinely combat-worn item cannot be repaired at the forge."));
            break;
        }

        // 재화와 작업 비용 처리
        GrowthBeforeItem = *Item;
        GrowthGoldBefore = P->Gold;
        GrowthOreBefore = P->Ore;
        const auto Cost = P->GetRepairCost(Item->Id);
        GrowthGoldCost = Cost.Gold;
        GrowthOreCost = Cost.Ore;

        // 재화와 작업 비용 처리
        if (Cost.Gold != Missing * (Item->Rarity + 1) || Cost.Ore != (Missing + 24) / 25 || Cost.Essence != 0 ||
            Cost.RootCores != 0 || Cost.MoonCrystals != 0)
        {
            Finish(false, TEXT("Repair cost differs from the visible durability contract."));
            break;
        }

        // 재화와 작업 비용 처리
        GrowthReport->SetNumberField(TEXT("repairGoldCost"), GrowthGoldCost);
        GrowthReport->SetNumberField(TEXT("repairOreCost"), GrowthOreCost);
        GrowthReport->SetObjectField(TEXT("repairBefore"), FableInventorySnapshot::MakeItem(*Item, 0, 0));
        Advance(19);
        PC->UIOpenWorkshop();
        PC->UISelectWorkshopTab(EFableWorkshopTab::Repair);
        PC->UISelectWorkshopItem(Item->Id);
        Capture(TEXT("repair-before"));
        break;
    }
    case 19:
    {
        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Checks.FindRef(TEXT("normal_repair_saved_once")))
        {
            if (Now - DungeonActionAt > .6)
            {
                Advance(20);
            }
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded)
        {
            break;
        }

        const auto* Item = P->FindItem(DungeonWeaponId);

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Item)
        {
            Finish(false, TEXT("The repair target disappeared."));
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Now - DungeonStepAt < .8)
        {
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (!Checks.FindRef(TEXT("repair_request_sent")))
        {
            PC->UIRepair(Item->Id);
            Checks.Add(TEXT("repair_request_sent"), true);
            DungeonActionAt = Now;
            break;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Item->Durability == Item->MaxDurability)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            auto ExpectedItem = GrowthBeforeItem;
            ExpectedItem.Durability = ExpectedItem.MaxDurability;

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Item->InstanceId != GrowthBeforeItem.InstanceId ||
                FableRPG::ItemPersistentSignature(*Item) != FableRPG::ItemPersistentSignature(ExpectedItem) ||
                P->Gold != GrowthGoldBefore - GrowthGoldCost || P->Ore != GrowthOreBefore - GrowthOreCost)
            {
                Finish(false, TEXT("Repair altered a roll/identity or charged an incorrect amount."));
                break;
            }

            // JSON 계약과 검증 기록 처리
            GrowthReport->SetObjectField(TEXT("repairAfter"), FableInventorySnapshot::MakeItem(*Item, 0, 0));
            Checks.Add(TEXT("normal_repair_saved_once"), true);
            Capture(TEXT("repair-after"));
            DungeonActionAt = Now;
            break;
        }

        // 이 단계에서 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다.
        if (Now - DungeonStepAt > 12)
        {
            Finish(false, TEXT("The normal repair was not acknowledged."));
        }

        break;
    }
    case 20:
        if (!P->IsBackendReady() || P->IsBackendSavePending() || !P->bLastSaveSucceeded ||
            P->GetBackendRevision() != P->GetBackendAcknowledgedRevision() || Now - DungeonStepAt < 1)
        {
            break;
        }
        if (P->Gold != 503 - GrowthReport->GetIntegerField(TEXT("repairGoldCost")) ||
            P->Ore != 27 - GrowthReport->GetIntegerField(TEXT("repairOreCost")) || P->Inventory.Num() != 7 ||
            P->Level != 4 || P->Experience != 265 || P->DungeonCompletions != 1 ||
            P->DungeonQuest != EFableQuestStage::Completed || P->EquippedWeaponId != GrowthWeaponId ||
            P->EquippedArmorId != GrowthArmorId)
        {
            Finish(false, TEXT("The final growth checkpoint differs from the earned and spent totals."));
            break;
        }
        GrowthReport->SetNumberField(TEXT("finalGold"), P->Gold);
        GrowthReport->SetNumberField(TEXT("finalOre"), P->Ore);
        GrowthReport->SetNumberField(TEXT("randomRewards"), 3);
        Checks.Add(TEXT("growth_final_saved"), true);
        PC->UIToggleInventory();
        PC->UISelectItem(GrowthWeaponId);
        return true;
    default:
        Finish(false, TEXT("Unknown online dungeon step."));
        break;
    }

    return false;
}
