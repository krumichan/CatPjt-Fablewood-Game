#include "FableSaveGame.h"

/** 불러온 저장 버전에 필요한 기본값을 보정한다. */
void UFableSaveGame::NormalizeLoadedVersion()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Version != 0)
    {
        return;
    }
    // Earlier releases omitted a Version equal to their class default.

    // 퀘스트와 이야기 진행 처리
    if (DungeonCompletions == 0 && DungeonQuest == EFableQuestStage::Completed)
    {
        DungeonCompletions = 1;
    }

    Version = 3;
}

/** 프로필 유효성 여부를 확인한다. */
bool UFableSaveGame::IsValidProfile() const
{
    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (Version < 1 || Version > FableProfileSchema::CurrentVersion || Level < 1 || Level > 20 || Experience < 0 ||
        (Level < 20 ? Experience >= Level * 100 : Experience != 0) || Gold < 0 || Gold > 1000000 || Ore < 0 ||
        Ore > 100000 || Inventory.Num() < 4 || Inventory.Num() > 32 || uint8(HeroClass) > uint8(EFableClass::Archer) ||
        uint8(ForestQuest) > uint8(EFableQuestStage::Completed) ||
        uint8(DungeonQuest) > uint8(EFableQuestStage::Completed) || QuestKills < 0 || QuestKills > 3)
    {
        return false;
    }
    if (Version == FableProfileSchema::CurrentVersion && !FableProfileSchema::IsSafeAnchor(GetLastSafeTownAnchor()))
    {
        return false;
    }

    if (Version >= 8 && !FableRPG::IsValidTalentProgress(ClassProgress))
    {
        return false;
    }
    if (Version >= 7 && !FableRPG::IsValidStoryProgress(StoryChapter, StoryObjectMask, StorySealMask, ForestQuest,
                                                        DungeonCompletions, MoonwellCompletions))
    {
        return false;
    }
    if (Version >= 2 && (HealthPotions < 0 || HealthPotions > 20 || ManaPotions < 0 || ManaPotions > 20 ||
                         PotionCooldownEndUtc < 0 || PotionCooldownEndUtc > 32503680000LL))
    {
        return false;
    }
    if (Version >= 3 && !FableRPG::IsValidClassProgress(ClassProgress, Level, ForestQuest, DungeonCompletions))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (Version >= 6 && (StashItems.Num() > 48 || Essence < 0 || Essence > 100000 || RootCores < 0 ||
                         RootCores > 100000 || MoonCrystals < 0 || MoonCrystals > 100000))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const auto IsValidItem = [this](const FFableItem& Item)
    {
        const bool bLegend = Version >= 6 && Item.Rarity == 4 && Item.RollVersion == 2;

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (Item.Id <= 0 || Item.Upgrade < 0 || Item.Upgrade > 5 || Item.Rarity < 0 ||
            Item.Rarity > (Version >= 6   ? 4
                           : Version >= 4 ? 3
                                          : 2) ||
            Item.Power < 0 || Item.Power > (bLegend ? 104 : 100) ||
            uint8(Item.Slot) > uint8(EFableEquipmentSlot::Armor) || uint8(Item.HeroClass) > uint8(EFableClass::Archer))
        {
            return false;
        }
        if (Version >= 5 && !FableRPG::IsValidItemRoll(Item))
        {
            return false;
        }
        if (Version >= 6 && (!FableRPG::IsValidItemDurability(Item) || (Item.Rarity == 4 && !bLegend)))
        {
            return false;
        }

        return true;
    };
    TSet<int32> Ids;
    TSet<FGuid> InstanceIds;
    const auto AcceptInstance = [this, &InstanceIds](const FFableItem& Item)
    {
        if (!Item.InstanceId.IsValid())
        {
            return Version < 9;
        }
        if (InstanceIds.Contains(Item.InstanceId))
        {
            return false;
        }
        InstanceIds.Add(Item.InstanceId);
        return true;
    };
    int32 MaxId = 0;
    bool bWeapon = false, bArmor = false;
    bool StarterItems[4] = {false, false, false, false};

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : Inventory)
    {
        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!IsValidItem(Item) || Ids.Contains(Item.Id) || !AcceptInstance(Item))
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        Ids.Add(Item.Id);
        MaxId = FMath::Max(MaxId, Item.Id);
        bWeapon |=
            Item.Id == EquippedWeaponId && Item.Slot == EFableEquipmentSlot::Weapon && Item.HeroClass == HeroClass;
        bArmor |= Item.Id == EquippedArmorId && Item.Slot == EFableEquipmentSlot::Armor;

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Item.Id >= 1 && Item.Id <= 3)
        {
            StarterItems[Item.Id - 1] =
                Item.Slot == EFableEquipmentSlot::Weapon && int32(Item.HeroClass) == Item.Id - 1;
        }
        else if (Item.Id == 4)
        {
            StarterItems[3] = Item.Slot == EFableEquipmentSlot::Armor;
        }
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (Version >= 6)
    {
        for (const FFableItem& Item : StashItems)
        {
            if (!IsValidItem(Item) || Item.Id <= 4 || Ids.Contains(Item.Id) || !AcceptInstance(Item))
            {
                return false;
            }
            Ids.Add(Item.Id);
            MaxId = FMath::Max(MaxId, Item.Id);
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWeapon || !bArmor || NextItemId <= MaxId || NextItemId >= 1000000)
    {
        return false;
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (bool bPresent : StarterItems)
    {
        if (!bPresent)
        {
            return false;
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Version < 4)
    {
        return true;
    }

    // 퀘스트와 이야기 진행 조건 검사
    if (uint8(MoonwellQuest) > uint8(EFableQuestStage::Completed) || MoonwellCompletions < 0 ||
        MoonwellCompletions > 1000000 || (MoonwellQuest != EFableQuestStage::Available && DungeonCompletions < 1) ||
        (MoonwellCompletions > 0 && DungeonCompletions < 1) ||
        (MoonwellQuest == EFableQuestStage::Completed && MoonwellCompletions < 1))
    {
        return false;
    }

    // 직업·스킬·특성 빌드 순회
    for (const FFableClassProgress& Entry : ClassProgress)
    {
        for (int32 TargetTier = 2; TargetTier <= 3; ++TargetTier)
        {
            const FFableAdvancementTrial& Trial = TargetTier == 2 ? Entry.SecondTrial : Entry.ThirdTrial;

            // 직업·스킬·특성 빌드 조건 검사
            if (uint8(Trial.Stage) > uint8(EFableQuestStage::Completed) || Trial.ValidSkillCasts < 0 ||
                Trial.ValidSkillCasts > 3)
            {
                return false;
            }

            const bool bGranted = Entry.Tier >= TargetTier;

            // 퀘스트와 이야기 진행 조건 검사
            if (bGranted != (Trial.Stage == EFableQuestStage::Completed))
            {
                return false;
            }

            // 직업·스킬·특성 빌드 처리
            if (Trial.bLegacyGranted)
            {
                if (!bGranted || Trial.ValidSkillCasts != 0)
                {
                    return false;
                }
                continue;
            }

            // 직업·스킬·특성 빌드 조건 검사
            if (Trial.Stage == EFableQuestStage::Available && Trial.ValidSkillCasts != 0)
            {
                return false;
            }
            if (TargetTier == 3 && Trial.Stage != EFableQuestStage::Available && Entry.Tier < 2)
            {
                return false;
            }

            // 직업·스킬·특성 빌드 처리
            if (Trial.Stage == EFableQuestStage::Ready || Trial.Stage == EFableQuestStage::Completed)
            {
                if (Trial.ValidSkillCasts != 3 || Level < (TargetTier == 2 ? 2 : 4) ||
                    ForestQuest != EFableQuestStage::Completed)
                {
                    return false;
                }
                if (TargetTier == 3 && (DungeonCompletions < 1 || MoonwellCompletions < 1))
                {
                    return false;
                }
            }
        }
    }

    return true;
}

/** 지원하는 이전 저장 형식을 변환한 뒤 전체 프로필의 유효성을 검사한다. */
bool UFableSaveGame::MigrateAndValidate(EFableTownBootstrapPolicy Policy)
{
    NormalizeLoadedVersion();
    // Never repair malformed arrays, invalid currency or unsupported versions into a writable profile.

    // 프로필과 저장 스냅숏 조건 검사
    if (!IsValidProfile())
    {
        return false;
    }

    // 직업·스킬·특성 빌드 처리
    if (Version < 4)
    {
        // 체력·마나·보호막·피해량 처리
        if (Version == 1)
        {
            HealthPotions = 3;
            ManaPotions = 2;
            PotionCooldownEndUtc = 0;
        }

        // 직업·스킬·특성 빌드 처리
        if (Version < 3)
        {
            ClassProgress.Init(FFableClassProgress(), 3);
            DungeonCompletions = DungeonQuest == EFableQuestStage::Completed ? 1 : 0;
        }

        // 퀘스트와 이야기 진행 처리
        MoonwellQuest = EFableQuestStage::Available;
        MoonwellCompletions = 0;

        // 직업·스킬·특성 빌드 순회
        for (FFableClassProgress& Entry : ClassProgress)
        {
            Entry.SecondTrial = FFableAdvancementTrial();
            Entry.ThirdTrial = FFableAdvancementTrial();
            if (Entry.Tier >= 2)
            {
                Entry.SecondTrial.Stage = EFableQuestStage::Completed;
                Entry.SecondTrial.bLegacyGranted = true;
            }
            if (Entry.Tier >= 3)
            {
                Entry.ThirdTrial.Stage = EFableQuestStage::Completed;
                Entry.ThirdTrial.bLegacyGranted = true;
            }
        }

        Version = 4;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (Version < 5)
    {
        // Existing item power, upgrades, rarity and identity remain byte-for-byte meaningful.
        // In particular, old dungeon items do not gain retroactive set bonuses or random options.
        for (FFableItem& Item : Inventory)
        {
            Item.RollVersion = 0;
            Item.Quality = EFableItemQuality::Unrated;
            Item.NominalPower = 0;
            Item.RollPercent = 100;
            Item.Affixes.Reset();
            Item.SetId = EFableEquipmentSet::None;
        }
        Version = 5;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (Version < 6)
    {
        // Validate the historical schema first; retain every existing equipment effect and identity.
        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (FFableItem& Item : Inventory)
        {
            Item.DurabilityVersion = 0;
            Item.MaxDurability = 100;
            Item.Durability = 100;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        Essence = 0;
        RootCores = 0;
        MoonCrystals = 0;
        StashItems.Reset();
        Version = 6;
    }

    // 퀘스트와 이야기 진행 처리
    if (Version < 7)
    {
        // A completed regional history skips its introductory story without paying it again.
        // Reaccepted repeat quests retain their permanent completion counts.
        StoryChapter = MoonwellCompletions > 0                      ? 5
                       : DungeonCompletions > 0                     ? 4
                       : ForestQuest == EFableQuestStage::Completed ? 3
                                                                    : 0;
        StoryObjectMask = StoryChapter >= 4 ? 31 : StoryChapter >= 3 ? 15 : 0;
        StorySealMask = StoryChapter == 5 ? 7 : 0;
        Version = 7;
    }

    // 직업·스킬·특성 빌드 처리
    if (Version < 8)
    {
        // Historical builds never selected talents. Preserve every existing rank,
        // trial, item and story receipt without granting a retroactive path.
        for (FFableClassProgress& Entry : ClassProgress)
        {
            Entry.SecondTalent = Entry.ThirdTalent = EFableTalentPath::None;
        }
        Version = 8;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (Version < 9)
    {
        // Validate all historical item fields before minting missing identities. Never
        // repair an invalid local ID, duplicate GUID, or a corrupt current schema.
        // 프로필과 저장 스냅숏 조건 검사
        if (!IsValidProfile())
        {
            return false;
        }

        TSet<FGuid> Used;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : Inventory)
        {
            if (Item.InstanceId.IsValid())
            {
                Used.Add(Item.InstanceId);
            }
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : StashItems)
        {
            if (Item.InstanceId.IsValid())
            {
                Used.Add(Item.InstanceId);
            }
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const auto AssignMissing = [&Used](TArray<FFableItem>& Items)
        {
            for (FFableItem& Item : Items)
            {
                if (Item.InstanceId.IsValid())
                {
                    continue;
                }
                do
                {
                    Item.InstanceId = FGuid::NewGuid();
                } while (!Item.InstanceId.IsValid() || Used.Contains(Item.InstanceId));
                Used.Add(Item.InstanceId);
            }
        };
        AssignMissing(Inventory);
        AssignMissing(StashItems);
        Version = 9;
    }

    // V9의 모든 기존 값을 검증한 뒤 논리 참조와 버전만 추가한다.
    if (Version == FableProfileSchema::ItemIdentityVersion)
    {
        if (!IsValidProfile())
        {
            return false;
        }
        const FFableTownAnchorRef Anchor = FableProfileSchema::BootstrapAnchor(Policy);
        if (!FableProfileSchema::IsSafeAnchor(Anchor))
        {
            return false;
        }
        LastSafeTownId = Anchor.TownId;
        LastSafeAnchorId = Anchor.AnchorId;
        TownCatalogVersion = Anchor.CatalogVersion;
        Version = FableProfileSchema::CurrentVersion;
    }

    return IsValidProfile();
}

/** 저장된 세 필드만 논리 참조로 읽는다. 지형 준비나 좌표는 추론하지 않는다. */
FFableTownAnchorRef UFableSaveGame::GetLastSafeTownAnchor() const
{
    FFableTownAnchorRef Anchor;
    Anchor.TownId = LastSafeTownId;
    Anchor.AnchorId = LastSafeAnchorId;
    Anchor.CatalogVersion = TownCatalogVersion;
    return Anchor;
}
