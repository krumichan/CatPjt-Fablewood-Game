#include "FableRPGValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "FableSaveGame.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
namespace
{
    /** 전리품 검증에서 레벨 상승까지 반영한 누적 경험치를 구한다. */
    int32 TotalLootExperience(const AFablePlayerState* PS)
    {
        return (PS->Level * (PS->Level - 1) / 2) * 100 + PS->Experience;
    }
}

/** 전리품 중지에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableRPGValidationDriver::LootStop()
{
    Hold(EKeys::W, false);
    Hold(EKeys::S, false);
}

/** 이전 저장 형식과 비교할 장비의 기존 속성을 직렬화한다. */
FString UFableRPGValidationDriver::LegacyItemSignature(const AFablePlayerState* PS) const
{
    // 재화와 작업 비용에 필요한 값 준비
    FString Value = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|potions:%d,%d"), int32(PS->HeroClass),
                                    PS->Level, PS->Experience, PS->Gold, PS->Ore, PS->EquippedWeaponId,
                                    PS->EquippedArmorId, int32(PS->ForestQuest), int32(PS->DungeonQuest),
                                    PS->QuestKills, PS->HealthPotions, PS->ManaPotions);

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : PS->Inventory)
    {
        if (!bWorkshop)
        {
            Value += FString::Printf(TEXT(";%d,%d,%d,%d,%d,%d"), Item.Id, int32(Item.Slot), int32(Item.HeroClass),
                                     Item.Rarity, Item.Power, Item.Upgrade);
        }
        else
        {
            Value += FString::Printf(TEXT(";%d,%d,%d,%d,%d,%d,r%d,q%d,n%d,p%d,s%d,a%d"), Item.Id, int32(Item.Slot),
                                     int32(Item.HeroClass), Item.Rarity, Item.Power, Item.Upgrade, Item.RollVersion,
                                     int32(Item.Quality), Item.NominalPower, Item.RollPercent, int32(Item.SetId),
                                     Item.Affixes.Num());
            for (const FFableItemAffix& A : Item.Affixes)
            {
                Value += FString::Printf(TEXT(",%d:%d"), int32(A.Stat), A.Value);
            }
        }
    }

    Value += FString::Printf(TEXT("|v3:completed=%d"), PS->DungeonCompletions);

    // 직업·스킬·특성 빌드 순회
    for (const FFableClassProgress& P : PS->ClassProgress)
    {
        Value += TEXT("|tier=") + FString::FromInt(P.Tier);
        for (int32 Rank : P.Ranks)
        {
            Value += TEXT(",") + FString::FromInt(Rank);
        }
    }

    return Value;
}

/** 전리품 상대 클라이언트의 진행 차례에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
int32 UFableRPGValidationDriver::LootPeerCycle(const FString& Field) const
{
    // JSON 계약과 검증 기록에 필요한 값 준비
    FString Data;
    TSharedPtr<FJsonObject> Object;
    double Number = -1;

    // JSON 계약과 검증 기록 처리
    if (FFileHelper::LoadFileToString(
            Data, *FPaths::Combine(Directory, Role == TEXT("host") ? TEXT("guest.json") : TEXT("host.json"))) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Object) && Object.IsValid())
    {
        Object->TryGetNumberField(Field, Number);
    }

    return int32(Number);
}

/** 전리품 적 능력치에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
bool UFableRPGValidationDriver::LootEnemyStats(EFableRegion Region, EFableDifficulty Difficulty) const
{
    int32 Count = 0;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        if (!It->IsAlive() || It->GetEncounterRegion() != Region)
        {
            continue;
        }
        ++Count;
        const float Base = Region == EFableRegion::Moonwell ? (It->IsBoss()     ? 720.f
                                                               : It->IsRanged() ? 100.f
                                                                                : 120.f)
                                                            : (It->IsBoss() ? 600.f : 110.f);
        if (It->GetEncounterDifficulty() != Difficulty ||
            !FMath::IsNearlyEqual(It->GetMaxHealth(), Base * (Difficulty == EFableDifficulty::Hard ? 1.45f : 1.f),
                                  .1f) ||
            !FMath::IsNearlyEqual(It->GetDamageMultiplier(), Difficulty == EFableDifficulty::Hard ? 1.25f : 1.f, .001f))
        {
            return false;
        }
    }

    return Count > 0;
}

/** 전리품 장비 미리보기 일치에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
bool UFableRPGValidationDriver::LootPreviewMatches() const
{
    const AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    return FMath::IsNearlyEqual(PS->GetAttackPower(), LootPreview.Attack, .01f) &&
           FMath::IsNearlyEqual(PS->GetDefense(), LootPreview.Defense, .01f) &&
           FMath::IsNearlyEqual(PS->GetMaxHealthBonus(), LootPreview.MaxHealth, .01f) &&
           FMath::IsNearlyEqual(PS->GetManaRegenRate(), LootPreview.ManaRegen, .01f);
}

/** 장비 난수·보상·착용 결과의 검증 단계를 진행한다. */
void UFableRPGValidationDriver::TickLoot()
{
    // 지역·적·전투 진행에 필요한 값 준비
    AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFableGameState* GS = GetWorld()->GetGameState<AFableGameState>();
    const double Now = GetWorld()->GetTimeSeconds(), Wait = Now - StepAt;
    const EFableRegion Region = LootCycle < 2 ? EFableRegion::RootRuins : EFableRegion::Moonwell;
    const EFableDifficulty Difficulty = LootCycle % 2 == 0 ? EFableDifficulty::Normal : EFableDifficulty::Hard;
    const EFableQuestId Quest = LootCycle < 2 ? EFableQuestId::RootRuins : EFableQuestId::Moonwell;
    const EFableEquipmentSet Set = LootCycle < 2 ? EFableEquipmentSet::RootGuardian : EFableEquipmentSet::Moonlight;
    const auto Go = [&](int32 Next)
    {
        PC->UIClosePanels();
        Advance(Next);
    };
    const auto Fail = [&](const TCHAR* Reason)
    {
        Capture(TEXT("loot-failure"));
        Finish(false, Reason);
    };

    // JSON 계약과 검증 기록 처리
    if (Now - LastLootReport > .5)
    {
        Report(TEXT("running"), TEXT("Normal-input random loot, sets and difficulty validation in progress."));
        LastLootReport = Now;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Wait > 260)
    {
        Fail(TEXT("Loot step timed out; inspect the reported position and phase."));
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Phase == TEXT("resume"))
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 4)
        {
            return;
        }

        LootStop();

        // 생존 상태와 부활 진행 조건 검사
        if (!Hero->IsAlive() || GS->ActiveRegion != EFableRegion::Village || GS->bPartyRecoveryPending ||
            GS->bRetryAvailable || Hero->GetReviveTarget() || Hero->GetReviver() ||
            GS->ActiveDifficulty != EFableDifficulty::Normal ||
            PS->GetEquippedSetCount(EFableEquipmentSet::Moonlight) != 2)
        {
            Fail(TEXT("Random equipment reload lost its active set or retained combat runtime."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : PS->Inventory)
        {
            if (!FableRPG::IsValidItemRoll(Item))
            {
                Fail(TEXT("A saved random roll changed or became invalid."));
                return;
            }
        }

        LootChecks.Add(TEXT("runtime_not_persisted"), true);

        // 이 단계에서 장비 난수·보상·착용 결과의 검증 단계를 진행한다.
        if (Screenshots.IsEmpty())
        {
            PC->UIToggleInventory();
            PC->UISelectItem(PS->EquippedWeaponId);
            Capture(TEXT("restored-random-equipment"));
            StepAt = Now;
        }
        else if (Wait > 5)
        {
            Finish(true, TEXT("All equipment rolls and set state restored using the real saved profile."));
        }

        return;
    }

    // 지역·적·전투 진행 처리
    if (Region == EFableRegion::Moonwell)
    {
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            if (It->IsBoss() && It->GetEnemyState() == EFableEnemyState::Windup)
            {
                bMoonRipple |= It->GetAttackPattern() == EFableEnemyAttack::MoonRing;
                bMoonRain |= It->GetAttackPattern() == EFableEnemyAttack::Moonfall;
            }
        }
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    switch (Step)
    {
    // 아이템·장비 옵션·인벤토리와 창고 처리
    case 0:
        if (Wait < 2)
        {
            return;
        }
        LootLegacySignature = LegacyItemSignature(PS);
        LootLegacyAttack = PS->GetAttackPower();
        LootLegacyDefense = PS->GetDefense();
        LootInitialRoot = PS->DungeonCompletions;
        LootInitialMoon = PS->MoonwellCompletions;
        for (const FFableItem& Item : PS->Inventory)
        {
            if ((!bWorkshop && Item.RollVersion != 0) || !FableRPG::IsValidItemRoll(Item) ||
                Item.DurabilityVersion != 0 || Item.Durability != 100 || Item.MaxDurability != 100)
            {
                Fail(TEXT("The legacy fixture was rerolled or damaged during migration."));
                return;
            }
        }
        if (bWorkshop)
        {
            WorkshopInitialItems = PS->Inventory;
            if (PS->Essence != 0 || PS->RootCores != 0 || PS->MoonCrystals != 0 || !PS->StashItems.IsEmpty())
            {
                Fail(TEXT("Migration retroactively granted materials or storage items."));
                return;
            }
            WorkshopChecks.Add(TEXT("v5_migrated"), true);
        }
        LootChecks.Add(TEXT("legacy_migrated"), true);
        Go(1);
        break;
    case 1:
        if (PS->GetQuestStage(Quest) == EFableQuestStage::Active)
        {
            Go(2);
            break;
        }
        AdventureQuest(Quest);
        break;
    case 2:
    {
        bool Ready = true;
        for (APlayerState* P : GS->PlayerArray)
        {
            const AFablePlayerState* Other = Cast<AFablePlayerState>(P);
            Ready &= Other && Other->IsRegionEligible(Region);
        }
        if (Ready)
        {
            Go(3);
        }
        break;
    }
    case 3:
        if (GS->ActiveRegion == Region && FableRPG::IsInRegion(Region, Hero->GetActorLocation()) &&
            GS->RequiredEnemies > 0)
        {
            Go(4);
            break;
        }
        if (!WalkTo(FableRPG::PortalLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 100) ||
            Hero->GetVelocity().Size2D() > 10)
        {
            LastAction = Now;
            return;
        }
        if (Now - LastAction < .8)
        {
            return;
        }
        if (PC->IsPortalOpen())
        {
            PC->UISelectDungeon(Region);
            PC->UISelectDifficulty(Difficulty);
            if (LootStage == 0)
            {
                Capture(FString::Printf(TEXT("portal-cycle-%d"), LootCycle));
                LootStage = 1;
                LastAction = Now;
                return;
            }
            PC->UIEnterSelectedDungeon();
        }
        else
        {
            PC->UIClosePanels();
            Key(EKeys::E);
        }
        LastAction = Now;
        break;
    case 4:
        LootStop();
        if (Wait < 1)
        {
            return;
        }
        if (GS->ActiveDifficulty != Difficulty || !LootEnemyStats(Region, Difficulty))
        {
            Fail(TEXT("Enemy health or damage multiplier does not match the selected difficulty."));
            return;
        }
        LootChecks.Add(Difficulty == EFableDifficulty::Hard ? TEXT("hard_enemy_stats") : TEXT("normal_enemy_stats"),
                       true);
        if (LootCycle == 1 && !LootChecks.FindRef(TEXT("hard_retry_fixed")))
        {
            LootBeforeWipe = AdventureSignature(PS, true);
            LootRecoveryBefore = GS->PartyRecoveryCount;
            LootSerialBefore = GS->EncounterSerial;
            Go(30);
            break;
        }
        Go(5);
        break;
    case 5:
        if (GS->AdventureStage == EFableAdventureStage::Boss)
        {
            Go(6);
            break;
        }
        AdventureFight();
        if (Region == EFableRegion::Moonwell && GS->AdventureStage == EFableAdventureStage::Dungeon &&
            GS->RequiredEnemies > 0 && GS->DefeatedEnemies >= GS->RequiredEnemies)
        {
            const int32 Seal = GS->ActiveSealIndex;
            if (Seal >= 0 && Seal < 3 &&
                WalkTo(FableRPG::MoonSealLocation(Seal) + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 110) &&
                Now - LastAction > .8)
            {
                Key(EKeys::E);
                LastAction = Now;
            }
        }
        if (GS->ActiveRegion == EFableRegion::Village)
        {
            Fail(TEXT("Party wiped while farming equipment."));
            return;
        }
        break;
    case 6:
        LootStop();
        if (Wait < 1.1 || LootPeerCycle(TEXT("step")) < 6 || LootPeerCycle(TEXT("loot_cycle")) < LootCycle)
        {
            return;
        }
        if (!LootEnemyStats(Region, Difficulty))
        {
            Fail(TEXT("Boss difficulty stats were scaled incorrectly."));
            return;
        }
        LootGoldBefore = PS->Gold;
        LootOreBefore = PS->Ore;
        LootXPBefore = TotalLootExperience(PS);
        LootBeforeIds.Reset();
        for (const FFableItem& Item : PS->Inventory)
        {
            LootBeforeIds.Add(Item.Id);
        }
        LootBossHealth = GS->BossMaxHealth;
        Capture(FString::Printf(TEXT("boss-cycle-%d"), LootCycle));
        Go(7);
        break;
    case 7:
        AdventureFight();
        if (GS->AdventureStage == EFableAdventureStage::Cleared && PS->GetQuestStage(Quest) == EFableQuestStage::Ready)
        {
            Go(8);
            break;
        }
        if (GS->ActiveRegion == EFableRegion::Village)
        {
            Fail(TEXT("Party wiped in the loot boss fight."));
            return;
        }
        break;
    case 8:
    {
        LootStop();

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 1.1)
        {
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        int32 Added = 0;
        const FFableItem* Drop = nullptr;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : PS->Inventory)
        {
            if (!LootBeforeIds.Contains(Item.Id))
            {
                ++Added;
                Drop = &Item;
            }
        }

        // 지역·적·전투 진행에 필요한 값 준비
        const float Multiplier = Difficulty == EFableDifficulty::Hard ? 1.5f : 1.f;
        const int32 Completed = Region == EFableRegion::RootRuins ? PS->DungeonCompletions : PS->MoonwellCompletions;
        const EFableEquipmentSlot ExpectedSlot =
            Completed % 2 == 0 ? EFableEquipmentSlot::Armor : EFableEquipmentSlot::Weapon;

        // 재화와 작업 비용 조건 검사
        if (Added != 1 || !Drop || !FableRPG::IsValidItemRoll(*Drop) || Drop->RollVersion != 1 || Drop->SetId != Set ||
            Drop->Slot != ExpectedSlot || Drop->HeroClass != PS->HeroClass ||
            PS->Gold - LootGoldBefore !=
                FMath::RoundToInt((Region == EFableRegion::RootRuins ? 140 : 180) * Multiplier) ||
            PS->Ore - LootOreBefore != FMath::RoundToInt((Region == EFableRegion::RootRuins ? 8 : 10) * Multiplier) ||
            TotalLootExperience(PS) - LootXPBefore !=
                FMath::RoundToInt((Region == EFableRegion::RootRuins ? 250 : 300) * Multiplier))
        {
            Fail(TEXT("Boss did not grant exactly one valid personal set piece and the correct difficulty rewards."));
            return;
        }

        // 지역·적·전투 진행 처리
        LootBossIds.Add(Drop->Id);
        LootCandidateId = Drop->Id;
        TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
        Event->SetNumberField(TEXT("cycle"), LootCycle);
        Event->SetNumberField(TEXT("region"), int32(Region));
        Event->SetNumberField(TEXT("difficulty"), int32(Difficulty));
        Event->SetNumberField(TEXT("boss_max_health"), LootBossHealth);
        Event->SetStringField(TEXT("item_signature"), FableRPG::ItemPersistentSignature(*Drop));
        LootEvents.Add(MakeShared<FJsonValueObject>(Event));
        LootBeforeRemote = AdventureSignature(PS);
        LootStage = 0;
        PC->UIEquip(Drop->Id);
        Capture(FString::Printf(TEXT("loot-cycle-%d"), LootCycle));
        Go(9);
        break;
    }
    case 9:
        LootStop();
        if (Wait < 1)
        {
            return;
        }
        if (AdventureSignature(PS) != LootBeforeRemote)
        {
            Fail(TEXT("Equipment, enhancement or trade was accepted remotely."));
            return;
        }
        if (LootStage == 0)
        {
            PC->UIEnhance(LootCandidateId);
            LootStage = 1;
            LootActionAt = Now;
            return;
        }
        if (Now - LootActionAt < .4)
        {
            return;
        }
        if (LootStage == 1)
        {
            PC->UIShopBuy(EFableShopOffer::Armor);
            LootStage = 2;
            LootActionAt = Now;
            return;
        }
        LootChecks.Add(TEXT("remote_mutation_rejected"), true);
        if (Role == TEXT("host"))
        {
            // A duplicate trusted server callback is a rejection probe only.
            // It must not change any hero's profile, roll or currency.
            for (APlayerState* P : GS->PlayerArray)
            {
                if (AFablePlayerState* Other = Cast<AFablePlayerState>(P))
                {
                    const FString Before = AdventureSignature(Other);
                    Other->RecordRegionKill(Region, true, Difficulty);
                    if (AdventureSignature(Other) != Before)
                    {
                        Fail(TEXT("A duplicate boss callback granted rewards again."));
                        return;
                    }
                }
            }
            LootProbeCycle = LootCycle;
            LootChecks.Add(TEXT("boss_reward_once"), true);
        }
        else if (LootPeerCycle(TEXT("loot_probe_cycle")) < LootCycle)
        {
            return;
        }
        LootChecks.Add(TEXT("boss_reward_once"), true);
        Go(10);
        break;
    case 10:
        if (FableRPG::IsInRegion(EFableRegion::Village, Hero->GetActorLocation()))
        {
            Go(11);
            break;
        }
        if (WalkTo(FableRPG::RegionReturnPortal(Region) + FVector(Role == TEXT("host") ? -80 : 80, 0, 100), 100) &&
            Now - LastAction > .8)
        {
            Key(EKeys::E);
            LastAction = Now;
        }
        break;
    case 11:
        if (PS->GetQuestStage(Quest) == EFableQuestStage::Completed)
        {
            LootStage = 0;
            Go(LootCycle % 2 == 1 ? 20 : 25);
            break;
        }
        AdventureQuest(Quest);
        break;

    // 이동 입력과 속도 제한 처리
    case 20:
    {
        // 이동 입력과 속도 제한 처리
        if (LootStage == 0)
        {
            // 이동 입력과 속도 제한 조건 검사
            if (!WalkTo(FableRPG::BoardLocation() + FVector(Role == TEXT("host") ? -100 : 100, -120, 100), 90) ||
                Hero->GetVelocity().Size2D() > 10)
            {
                return;
            }

            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            const FFableItem* Weapon = nullptr;
            const FFableItem* Armor = nullptr;

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (const FFableItem& Item : PS->Inventory)
            {
                if (Item.SetId == Set && Item.HeroClass == PS->HeroClass)
                {
                    if (Item.Slot == EFableEquipmentSlot::Weapon)
                    {
                        Weapon = &Item;
                    }
                    else
                    {
                        Armor = &Item;
                    }
                }
            }

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!Weapon || !Armor)
            {
                Fail(TEXT("Two personal clears did not yield both set slots."));
                return;
            }

            FFableEquipmentStats Before;

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!PS->GetEquipmentPreview(Weapon->Id, Before, LootPreview))
            {
                Fail(TEXT("Set weapon preview was unavailable."));
                return;
            }

            LootCandidateId = Weapon->Id;

            // 이 단계에서 장비 난수·보상·착용 결과의 검증 단계를 진행한다.
            if (!PC->IsInventoryOpen())
            {
                PC->UIToggleInventory();
            }

            PC->UISelectItem(LootCandidateId);
            Capture(LootCycle == 1 ? TEXT("root-set-weapon-preview") : TEXT("moon-set-weapon-preview"));
            LootActionAt = Now;
            LootStage = 1;

            return;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - LootActionAt < .85)
        {
            return;
        }
        if (LootStage == 1)
        {
            PC->UIEquip(LootCandidateId);
            LootActionAt = Now;
            LootStage = 2;
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (LootStage == 2)
        {
            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (PS->EquippedWeaponId != LootCandidateId || !LootPreviewMatches())
            {
                Fail(TEXT("Equipped weapon does not match its four-stat preview."));
                return;
            }

            const FFableItem* Armor = nullptr;

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (const FFableItem& Item : PS->Inventory)
            {
                if (Item.SetId == Set && Item.Slot == EFableEquipmentSlot::Armor)
                {
                    Armor = &Item;
                }
            }

            FFableEquipmentStats Before;

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!Armor || !PS->GetEquipmentPreview(Armor->Id, Before, LootPreview))
            {
                Fail(TEXT("Set armor preview was unavailable."));
                return;
            }

            LootCandidateId = Armor->Id;
            PC->UISelectItem(LootCandidateId);
            Capture(LootCycle == 1 ? TEXT("root-set-completion-preview") : TEXT("moon-set-completion-preview"));
            LootActionAt = Now;
            LootStage = 3;

            return;
        }

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (LootStage == 3)
        {
            PC->UIEquip(LootCandidateId);
            LootActionAt = Now;
            LootStage = 4;
            return;
        }
        if (PS->EquippedArmorId != LootCandidateId || !LootPreviewMatches() || PS->GetEquippedSetCount(Set) != 2)
        {
            Fail(TEXT("Two-piece set or predicted stats were not applied."));
            return;
        }

        LootChecks.Add(TEXT("equipment_preview_matches"), true);
        LootChecks.Add(LootCycle == 1 ? TEXT("root_set_equipped") : TEXT("moon_set_equipped"), true);
        Capture(LootCycle == 1 ? TEXT("root-set-equipped") : TEXT("moon-set-equipped"));
        Advance(24);
        break;
    }
    case 24:
        if (Wait > 1.1)
        {
            LootStage = 0;
            Go(25);
        }
        break;
    case 25:
        ++LootCycle;
        LootStage = 0;
        if (LootCycle < 4)
        {
            Go(1);
        }
        else
        {
            Go(50);
        }
        break;
    case 30:
    {
        // 검증 입력과 키 유지 상태 처리
        if (GS->bPartyRecoveryPending)
        {
            LootChecks.Add(TEXT("hard_wipe_seen"), true);
            LootStop();
            Go(31);
            break;
        }

        // 검증 입력과 키 유지 상태 조건 검사
        if (!Hero->IsAlive())
        {
            LootStop();
            return;
        }

        // 지역·적·전투 진행에 필요한 값 준비
        AFableEnemy* Closest = nullptr;
        float Distance = MAX_flt;

        // 지역·적·전투 진행 순회
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            if (It->IsAlive() && It->GetEncounterRegion() == Region)
            {
                const float D = FVector::Dist2D(Hero->GetActorLocation(), It->GetActorLocation());
                if (D < Distance)
                {
                    Distance = D;
                    Closest = *It;
                }
            }
        }

        // 검증 입력과 키 유지 상태 처리
        if (Closest)
        {
            WalkTo(Closest->GetActorLocation() + FVector(Role == TEXT("host") ? -70 : 70, -80, 0), 80);
        }
        else
        {
            LootStop();
        }

        break;
    }
    case 31:
        LootStop();
        if (GS->PartyRecoveryCount <= LootRecoveryBefore || !Hero->IsAlive() ||
            GS->ActiveRegion != EFableRegion::Village)
        {
            return;
        }
        if (GS->RetryDifficulty != EFableDifficulty::Hard || !GS->bRetryAvailable ||
            AdventureSignature(PS, true) != LootBeforeWipe)
        {
            Fail(TEXT("Hard wipe changed progression or lost its retry difficulty."));
            return;
        }
        Go(32);
        break;
    case 32:
        if (GS->EncounterSerial > LootSerialBefore && GS->ActiveRegion == Region &&
            FableRPG::IsInRegion(Region, Hero->GetActorLocation()))
        {
            Go(33);
            break;
        }
        if (LootPeerCycle(TEXT("step")) < 32)
        {
            LootStop();
            return;
        }
        if (WalkTo(FableRPG::PortalLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 100) &&
            Hero->GetVelocity().Size2D() < 10 && Now - LastAction > .8)
        {
            Key(EKeys::R);
            LastAction = Now;
        }
        break;
    case 33:
        LootStop();
        if (Wait < 1)
        {
            return;
        }
        if (GS->ActiveDifficulty != EFableDifficulty::Hard || !LootEnemyStats(Region, EFableDifficulty::Hard))
        {
            Fail(TEXT("R retry changed the hard encounter to another difficulty."));
            return;
        }
        LootChecks.Add(TEXT("hard_retry_fixed"), true);
        Capture(TEXT("hard-retry-preserved"));
        Go(4);
        break;
    case 49:
        LootStop();
        if (Wait > 1)
        {
            Go(51);
        }
        break;
    case 50:
    {
        LootStop();

        // 이 단계에서 장비 난수·보상·착용 결과의 검증 단계를 진행한다.
        if (!PC->IsInventoryOpen())
        {
            PC->UIToggleInventory();
        }

        PC->UISelectItem(PS->EquippedWeaponId);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Now - LootActionAt < .9)
        {
            return;
        }

        const FString Before = AdventureSignature(PS);
        PC->UICycleInventorySort();
        const TArray<int32> Display = PC->GetInventoryDisplayIds();
        TSet<int32> Unique;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Id : Display)
        {
            if (!PS->FindItem(Id) || Unique.Contains(Id))
            {
                Fail(TEXT("Sorted inventory lost or duplicated an item."));
                return;
            }
            Unique.Add(Id);
        }

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (Display.Num() != PS->Inventory.Num() || PC->GetSelectedItemId() != PS->EquippedWeaponId ||
            AdventureSignature(PS) != Before)
        {
            Fail(TEXT("Sorting changed selection or persistent inventory."));
            return;
        }

        Capture(FString::Printf(TEXT("inventory-sort-%d"), LootStage));
        LootActionAt = Now;

        // 이 단계에서 장비 난수·보상·착용 결과의 검증 단계를 진행한다.
        if (++LootStage >= 3)
        {
            LootChecks.Add(TEXT("inventory_sort_valid"), true);
            Advance(49);
        }

        break;
    }
    case 51:
        if (!WalkTo(FableRPG::ForgeLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 100), 90) ||
            Hero->GetVelocity().Size2D() > 10)
        {
            return;
        }
        if (const FFableItem* Weapon = PS->FindItem(PS->EquippedWeaponId))
        {
            LootBeforeEnhance = *Weapon;
            LootGoldBefore = PS->Gold;
            LootOreBefore = PS->Ore;
            LootXPBefore = PS->GetUpgradeGoldCost(Weapon->Id);
            LootSellPrice = PS->GetUpgradeOreCost(Weapon->Id);
            PC->UIEnhance(Weapon->Id);
            Go(52);
        }
        break;
    case 52:
    {
        LootStop();

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 1)
        {
            return;
        }

        const FFableItem* Weapon = PS->FindItem(LootBeforeEnhance.Id);

        // 재화와 작업 비용 조건 검사
        if (!Weapon || Weapon->Upgrade != LootBeforeEnhance.Upgrade + 1 || PS->Gold != LootGoldBefore - LootXPBefore ||
            PS->Ore != LootOreBefore - LootSellPrice)
        {
            Fail(TEXT("Real enhancement did not spend the expected resources."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Original = *Weapon;
        Original.Upgrade = LootBeforeEnhance.Upgrade;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (FableRPG::ItemPersistentSignature(Original) != FableRPG::ItemPersistentSignature(LootBeforeEnhance))
        {
            Fail(TEXT("Enhancement rerolled original performance or options."));
            return;
        }

        LootChecks.Add(TEXT("rolls_preserved_after_enhance"), true);
        PC->UIToggleInventory();
        PC->UISelectItem(Weapon->Id);
        Capture(TEXT("random-equipment-enhanced"));
        Advance(53);
        break;
    }
    case 53:
        if (Wait > 1)
        {
            Go(60);
        }
        break;
    case 60:
        if (!WalkTo(FableRPG::MerchantLocation() + FVector(Role == TEXT("host") ? -90 : 90, -130, 100), 85) ||
            Hero->GetVelocity().Size2D() > 10)
        {
            return;
        }
        LootBeforeIds.Reset();
        for (const FFableItem& Item : PS->Inventory)
        {
            LootBeforeIds.Add(Item.Id);
        }
        LootGoldBefore = PS->Gold;
        PC->UIShopBuy(EFableShopOffer::Armor);
        Go(61);
        break;
    case 61:
    {
        LootStop();

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Wait < 1)
        {
            return;
        }

        const FFableItem* Bought = nullptr;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : PS->Inventory)
        {
            if (!LootBeforeIds.Contains(Item.Id))
            {
                Bought = &Item;
            }
        }

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!Bought || Bought->RollVersion != 1 || !FableRPG::IsValidItemRoll(*Bought) ||
            Bought->SetId != EFableEquipmentSet::None || PS->Inventory.Num() != LootBeforeIds.Num() + 1 ||
            PS->Gold != LootGoldBefore - FableRPG::ShopPrice(EFableShopOffer::Armor))
        {
            Fail(TEXT("Shop purchase did not receive one valid random item."));
            return;
        }

        LootShopId = Bought->Id;
        LootChecks.Add(TEXT("shop_roll_received"), true);
        PC->UIToggleInventory();
        PC->UISelectItem(Bought->Id);
        Capture(TEXT("shop-random-equipment"));
        Advance(62);
        break;
    }
    case 62:
        if (Wait < 1)
        {
            return;
        }
        LootGoldBefore = PS->Gold;
        LootSellPrice = PS->GetSellPrice(LootShopId);
        PC->UISell(LootShopId);
        Go(63);
        break;
    case 63:
        if (Wait < 1)
        {
            return;
        }
        if (PS->FindItem(LootShopId) || LootSellPrice <= 0 || PS->Gold != LootGoldBefore + LootSellPrice)
        {
            Fail(TEXT("Random shop item sale did not preserve the remaining equipment."));
            return;
        }
        LootChecks.Add(TEXT("shop_sale_checked"), true);
        PC->UIToggleInventory();
        PC->UISelectItem(PS->EquippedWeaponId);
        Capture(TEXT("final-random-equipment"));
        Advance(70);
        break;
    case 70:
        LootStop();
        if (Wait > 2)
        {
            Finish(
                true,
                TEXT(
                    "Four real boss clears, random personal drops, normal/hard rewards, wipe retry, sets, comparison, enhancement, trade and persistent saving passed."));
        }
        break;
    default:
        Fail(TEXT("Unknown loot validation step."));
        break;
    }
}

/** 전리품 보고서 항목을 최종 검증 JSON에 추가한다. */
void UFableRPGValidationDriver::AppendLootReport(TSharedRef<FJsonObject> Object) const
{
    const AFablePlayerState* PS = PC.IsValid() ? PC->GetPlayerState<AFablePlayerState>() : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!PS)
    {
        return;
    }

    // 이동 입력과 속도 제한 처리
    if (const AFableCharacter* Hero = PC->GetPawn<AFableCharacter>())
    {
        Object->SetNumberField(TEXT("life_state"), int32(Hero->GetLifeState()));
        Object->SetNumberField(TEXT("health"), Hero->GetHealth());
        Object->SetNumberField(TEXT("max_health"), Hero->GetMaxHealth());
        Object->SetStringField(TEXT("velocity"), Hero->GetVelocity().ToString());
    }

    // JSON 계약과 검증 기록 처리
    Object->SetNumberField(TEXT("loot_action_stage"), LootStage);
    Object->SetBoolField(TEXT("loot_enabled"), true);
    Object->SetNumberField(TEXT("loot_cycle"), LootCycle);
    Object->SetNumberField(TEXT("loot_probe_cycle"), LootProbeCycle);
    Object->SetStringField(TEXT("loot_signature"), AdventureSignature(PS));
    Object->SetStringField(TEXT("legacy_signature"), LootLegacySignature);
    Object->SetNumberField(TEXT("legacy_attack"), LootLegacyAttack);
    Object->SetNumberField(TEXT("legacy_defense"), LootLegacyDefense);
    Object->SetNumberField(TEXT("initial_root_completions"), LootInitialRoot);
    Object->SetNumberField(TEXT("initial_moon_completions"), LootInitialMoon);
    Object->SetStringField(TEXT("profile_slot"), PS->GetProfileSlotForValidation());
    int32 Schema = 0;

    // 서버 권한과 실행 위치 처리
    if (PS->HasAuthority() && !PS->GetProfileSlotForValidation().IsEmpty())
    {
        if (const UFableSaveGame* Data =
                Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromSlot(PS->GetProfileSlotForValidation(), 0)))
        {
            Schema = Data->Version;
        }
    }

    // 체력·마나·보호막·피해량 처리
    Object->SetNumberField(TEXT("saved_schema_version"), Schema);
    Object->SetNumberField(TEXT("root_set_count"), PS->GetEquippedSetCount(EFableEquipmentSet::RootGuardian));
    Object->SetNumberField(TEXT("moon_set_count"), PS->GetEquippedSetCount(EFableEquipmentSet::Moonlight));
    Object->SetNumberField(TEXT("mana_regen"), PS->GetManaRegenRate());
    Object->SetNumberField(TEXT("max_health_bonus"), PS->GetMaxHealthBonus());

    // JSON 계약과 검증 기록 처리
    if (const AFableGameState* GS = GetWorld()->GetGameState<AFableGameState>())
    {
        Object->SetNumberField(TEXT("active_difficulty"), int32(GS->ActiveDifficulty));
        Object->SetNumberField(TEXT("retry_difficulty"), int32(GS->RetryDifficulty));
    }

    TSharedRef<FJsonObject> Checks = MakeShared<FJsonObject>();

    // JSON 계약과 검증 기록 순회
    for (const auto& Pair : LootChecks)
    {
        Checks->SetBoolField(Pair.Key, Pair.Value);
    }

    // JSON 계약과 검증 기록 처리
    Object->SetObjectField(TEXT("loot_checks"), Checks);
    TArray<TSharedPtr<FJsonValue>> Ids;

    // 지역·적·전투 진행 순회
    for (int32 Id : LootBossIds)
    {
        Ids.Add(MakeShared<FJsonValueNumber>(Id));
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    Object->SetArrayField(TEXT("loot_boss_ids"), Ids);
    Object->SetArrayField(TEXT("loot_events"), LootEvents);
    TArray<TSharedPtr<FJsonValue>> Items;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : PS->Inventory)
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("id"), Item.Id);
        J->SetNumberField(TEXT("slot"), int32(Item.Slot));
        J->SetNumberField(TEXT("class"), int32(Item.HeroClass));
        J->SetNumberField(TEXT("rarity"), Item.Rarity);
        J->SetNumberField(TEXT("power"), Item.Power);
        J->SetNumberField(TEXT("upgrade"), Item.Upgrade);
        J->SetNumberField(TEXT("roll_version"), Item.RollVersion);
        J->SetNumberField(TEXT("quality"), int32(Item.Quality));
        J->SetNumberField(TEXT("nominal_power"), Item.NominalPower);
        J->SetNumberField(TEXT("roll_percent"), Item.RollPercent);
        J->SetNumberField(TEXT("set_id"), int32(Item.SetId));
        J->SetNumberField(TEXT("durability_version"), Item.DurabilityVersion);
        J->SetNumberField(TEXT("durability"), Item.Durability);
        J->SetNumberField(TEXT("max_durability"), Item.MaxDurability);
        J->SetStringField(TEXT("signature"), FableRPG::ItemPersistentSignature(Item));
        TArray<TSharedPtr<FJsonValue>> Affixes;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItemAffix& Affix : Item.Affixes)
        {
            TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
            A->SetNumberField(TEXT("stat"), int32(Affix.Stat));
            A->SetNumberField(TEXT("value"), Affix.Value);
            Affixes.Add(MakeShared<FJsonValueObject>(A));
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        J->SetArrayField(TEXT("affixes"), Affixes);
        Items.Add(MakeShared<FJsonValueObject>(J));
    }

    Object->SetArrayField(TEXT("loot_inventory"), Items);
}
