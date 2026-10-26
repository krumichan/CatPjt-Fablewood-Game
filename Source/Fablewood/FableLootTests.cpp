#include "FableRPGTypes.h"
#include "FableSaveGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    /** 생성 전리품 프로필에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    UFableSaveGame* MakeLootProfile(int32 Version)
    {
        // 재화와 작업 비용에 필요한 값 준비
        UFableSaveGame* P = NewObject<UFableSaveGame>();
        P->Version = Version;
        P->Gold = 17;
        P->Ore = 3;
        P->HealthPotions = 2;
        P->ManaPotions = 1;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 I = 0; I < 3; ++I)
        {
            FFableItem Item;
            Item.Id = I + 1;
            Item.HeroClass = EFableClass(I);
            P->Inventory.Add(Item);
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Armor;
        Armor.Id = 4;
        Armor.Slot = EFableEquipmentSlot::Armor;
        Armor.Power = 0;
        P->Inventory.Add(Armor);

        return P;
    }

    /** 장비 난수 생성 검증 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    FFableItem RollFixture(int32 Id, EFableEquipmentSlot Slot, EFableEquipmentSet Set, EFableDifficulty Difficulty,
                           FRandomStream& Random)
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Item;
        Item.Id = Id;
        Item.Slot = Slot;
        Item.Rarity = Set == EFableEquipmentSet::Moonlight ? 3 : 2;
        Item.SetId = Set;
        FableRPG::RollItem(Item,
                           Item.Rarity == 3 ? (Slot == EFableEquipmentSlot::Weapon ? 58 : 16)
                                            : (Slot == EFableEquipmentSlot::Weapon ? 48 : 12),
                           Difficulty, Random);

        return Item;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableLootRollTest, "Fablewood.Loot.RollBoundsAndDifficulty",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 전리품 장비 난수 생성 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableLootRollTest::RunTest(const FString& Parameters)
{
    int32 Finest[2] = {0, 0};
    int32 QualityCounts[5] = {0, 0, 0, 0, 0};
    TSet<int32> Powers;
    TSet<FString> Variants;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Mode = 0; Mode < 2; ++Mode)
    {
        FRandomStream Random(74191 + Mode);
        for (int32 I = 0; I < 2000; ++I)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            const EFableDifficulty Difficulty = Mode == 0 ? EFableDifficulty::Normal : EFableDifficulty::Hard;
            const EFableEquipmentSlot Slot = I % 2 == 0 ? EFableEquipmentSlot::Weapon : EFableEquipmentSlot::Armor;
            const FFableItem Item = RollFixture(5, Slot, EFableEquipmentSet::RootGuardian, Difficulty, Random);

            // 기대값과 상태 보존 검증 조건 검사
            if (!TestTrue(TEXT("Generated item obeys the complete authoritative roll schema"),
                          FableRPG::IsValidItemRoll(Item)))
            {
                return false;
            }

            // 아이템·장비 옵션·인벤토리와 창고 처리
            TestEqual(TEXT("Stored base power matches its recorded percentage"), Item.Power,
                      FMath::RoundToInt(Item.NominalPower * Item.RollPercent / 100.f));
            int32 Min = 0, Max = 0;
            FableRPG::GetItemPowerRange(Item, Min, Max);
            TestTrue(TEXT("Base power lies inside its displayed quality range"),
                     Item.Power >= Min && Item.Power <= Max);
            TestEqual(TEXT("Quality determines option count"), Item.Affixes.Num(),
                      FableRPG::QualityAffixCount(Item.Quality));
            TSet<uint8> Seen;

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (const FFableItemAffix& A : Item.Affixes)
            {
                FableRPG::AffixValueRange(A.Stat, Min, Max);
                TestTrue(TEXT("Option type is unique and value is bounded"),
                         !Seen.Contains(uint8(A.Stat)) && A.Value >= Min && A.Value <= Max);
                Seen.Add(uint8(A.Stat));
            }

            ++QualityCounts[int32(Item.Quality)];

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Item.Quality == EFableItemQuality::Finest)
            {
                ++Finest[Mode];
            }

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Slot == EFableEquipmentSlot::Weapon)
            {
                Powers.Add(Item.Power);
                Variants.Add(FableRPG::ItemPersistentSignature(Item));
            }

            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            FFableItem Enhanced = Item;
            Enhanced.Upgrade = 5;
            TestTrue(TEXT("Enhancing preserves original roll validity"), FableRPG::IsValidItemRoll(Enhanced));
            TestEqual(TEXT("Enhancing adds only the fixed slot increment"), Enhanced.GetEffectivePower(),
                      Item.Power + (Slot == EFableEquipmentSlot::Weapon ? 20 : 10));
            TestEqual(TEXT("Enhancing never rerolls affixes"), Enhanced.Affixes.Num(), Item.Affixes.Num());
        }
    }

    // 기대값과 상태 보존 검증 순회
    for (int32 Q = 1; Q <= 4; ++Q)
    {
        TestTrue(TEXT("All four quality grades were actually generated"), QualityCounts[Q] > 0);
    }

    // 기대값과 상태 보존 검증 처리
    TestTrue(TEXT("Same equipment has many different basic powers"), Powers.Num() > 15);
    TestTrue(TEXT("Same equipment has many different option combinations"), Variants.Num() > 500);
    TestTrue(TEXT("Hard difficulty improves finest-quality frequency in a reproducible sample"),
             Finest[1] > Finest[0] + 100);

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Rarity = 1; Rarity <= 3; ++Rarity)
    {
        for (int32 SlotIndex = 0; SlotIndex < 2; ++SlotIndex)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            FRandomStream Seed(100 + Rarity * 10 + SlotIndex);
            FFableItem Item;
            Item.Id = 5;
            Item.Rarity = Rarity;
            Item.Slot = EFableEquipmentSlot(SlotIndex);
            const int32 Nominal = Rarity == 1   ? (SlotIndex == 0 ? 30 : 6)
                                  : Rarity == 2 ? (SlotIndex == 0 ? 48 : 12)
                                                : (SlotIndex == 0 ? 58 : 16);
            TestTrue(TEXT("Shop, region and quest nominal tables produce valid equipment"),
                     FableRPG::RollItem(Item, Nominal, EFableDifficulty::Normal, Seed));

            // 기대값과 상태 보존 검증 처리
            if (Rarity == 1 && SlotIndex == 0)
            {
                TestTrue(TEXT("Forest quest nominal32 is supported"),
                         FableRPG::RollItem(Item, 32, EFableDifficulty::Normal, Seed));
            }
        }
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FRandomStream A(314159), B(314159);
    const FFableItem One =
        RollFixture(5, EFableEquipmentSlot::Weapon, EFableEquipmentSet::RootGuardian, EFableDifficulty::Hard, A);
    const FFableItem Two =
        RollFixture(5, EFableEquipmentSlot::Weapon, EFableEquipmentSet::RootGuardian, EFableDifficulty::Hard, B);
    TestEqual(TEXT("A seed reproduces the production generator exactly"), FableRPG::ItemPersistentSignature(One),
              FableRPG::ItemPersistentSignature(Two));
    FFableItem Invalid = One;
    const FString Before = FableRPG::ItemPersistentSignature(Invalid);
    TestFalse(TEXT("Unknown difficulty is rejected"), FableRPG::RollItem(Invalid, 48, EFableDifficulty(255), A));
    TestEqual(TEXT("Rejected roll does not alter the input item"), FableRPG::ItemPersistentSignature(Invalid), Before);
    TestFalse(TEXT("Impossible nominal power is rejected"),
              FableRPG::RollItem(Invalid, 99, EFableDifficulty::Normal, A));
    TestEqual(TEXT("Rejected nominal leaves the item intact"), FableRPG::ItemPersistentSignature(Invalid), Before);
    AddInfo(FString::Printf(
        TEXT("4,000 production rolls: finest normal=%d/2000 hard=%d/2000; base powers=%d option variants=%d"),
        Finest[0], Finest[1], Powers.Num(), Variants.Num()));

    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableLootMigrationTest, "Fablewood.Loot.LegacyMigrationAndCorruptRejection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 전리품 버전 이관 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableLootMigrationTest::RunTest(const FString& Parameters)
{
    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Version = 0; Version <= 4; ++Version)
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        UFableSaveGame* P = MakeLootProfile(Version);
        TArray<FString> Before;
        FFableItem OldRare;
        OldRare.Id = 5;
        OldRare.Rarity = 2;
        OldRare.Power = 48;
        OldRare.Upgrade = 3;
        P->Inventory.Add(OldRare);
        P->NextItemId = 6;

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Version == 4)
        {
            FFableItem OldMoon;
            OldMoon.Id = 6;
            OldMoon.Rarity = 3;
            OldMoon.Slot = EFableEquipmentSlot::Armor;
            OldMoon.Power = 16;
            OldMoon.Upgrade = 2;
            P->Inventory.Add(OldMoon);
            P->NextItemId = 7;
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : P->Inventory)
        {
            Before.Add(FableRPG::ItemPersistentSignature(Item));
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestTrue(TEXT("Supported legacy schema migrates"), P->MigrateAndValidate());
        TestEqual(TEXT("Migration reaches current schema"), P->Version, FableProfileSchema::CurrentVersion);
        TestEqual(TEXT("Legacy gold is preserved"), P->Gold, 17);
        TestEqual(TEXT("Legacy ore is preserved"), P->Ore, 3);
        TestEqual(TEXT("Migration does not grant extra items"), P->Inventory.Num(), Before.Num());

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 I = 0; I < Before.Num(); ++I)
        {
            TestEqual(TEXT("Legacy basic performance, upgrade, IDs and no-roll state are preserved"),
                      FableRPG::ItemPersistentSignature(P->Inventory[I]), Before[I]);
        }

        // 체력·마나·보호막·피해량 처리
        TestEqual(TEXT("Potion quantities follow their original schema"), P->HealthPotions, Version == 1 ? 3 : 2);
        TestEqual(TEXT("Mana potions follow their original schema"), P->ManaPotions, Version == 1 ? 2 : 1);
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    UFableSaveGame* Future = MakeLootProfile(10);
    TestFalse(TEXT("Future schema stays unsupported"), Future->MigrateAndValidate());

    UFableSaveGame* Duplicate = MakeLootProfile(6);
    Duplicate->Inventory[1].Id = 1;
    TestFalse(TEXT("Duplicate item IDs are rejected"), Duplicate->MigrateAndValidate());

    FRandomStream Random(42);
    UFableSaveGame* Corrupt = MakeLootProfile(6);
    Corrupt->Inventory.Add(RollFixture(5, EFableEquipmentSlot::Weapon, EFableEquipmentSet::RootGuardian,
                                       EFableDifficulty::Normal, Random));
    Corrupt->NextItemId = 6;
    Corrupt->Inventory.Last().Power += 1;
    TestFalse(TEXT("Forged base performance is rejected"), Corrupt->MigrateAndValidate());
    Corrupt = MakeLootProfile(6);
    Corrupt->Inventory.Add(RollFixture(5, EFableEquipmentSlot::Weapon, EFableEquipmentSet::RootGuardian,
                                       EFableDifficulty::Normal, Random));
    Corrupt->NextItemId = 6;
    Corrupt->Inventory.Last().Affixes[0].Value = 100000;
    TestFalse(TEXT("Forged option values are rejected"), Corrupt->MigrateAndValidate());
    Corrupt = MakeLootProfile(6);
    Corrupt->Inventory.Add(RollFixture(5, EFableEquipmentSlot::Weapon, EFableEquipmentSet::RootGuardian,
                                       EFableDifficulty::Normal, Random));
    Corrupt->NextItemId = 6;
    Corrupt->Inventory.Last().Quality = EFableItemQuality(255);
    TestFalse(TEXT("Unknown quality is rejected"), Corrupt->MigrateAndValidate());

    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableLootSaveTest, "Fablewood.Loot.RandomEquipmentSaveRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 전리품 저장 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableLootSaveTest::RunTest(const FString& Parameters)
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FRandomStream Random(8675309);
    UFableSaveGame* P = MakeLootProfile(6);
    P->Inventory.Add(
        RollFixture(5, EFableEquipmentSlot::Weapon, EFableEquipmentSet::RootGuardian, EFableDifficulty::Hard, Random));
    P->Inventory.Add(
        RollFixture(6, EFableEquipmentSlot::Armor, EFableEquipmentSet::RootGuardian, EFableDifficulty::Normal, Random));
    P->Inventory.Last().Upgrade = 2;
    P->NextItemId = 7;
    P->EquippedWeaponId = 5;
    P->EquippedArmorId = 6;
    TestTrue(TEXT("Random-equipped profile validates before saving"), P->IsValidProfile());
    TArray<uint8> Bytes;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("The real Unreal serializer writes the fixture"), UGameplayStatics::SaveGameToMemory(P, Bytes)))
    {
        return false;
    }

    UFableSaveGame* Restored = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("The real Unreal serializer restores the fixture"), Restored))
    {
        return false;
    }

    TestTrue(TEXT("Restored random profile validates"), Restored->MigrateAndValidate());

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!TestEqual(TEXT("Item count survives serialization"), Restored->Inventory.Num(), P->Inventory.Num()))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 I = 0; I < P->Inventory.Num(); ++I)
    {
        TestEqual(TEXT("Every original roll, affix and set field survives serialization"),
                  FableRPG::ItemPersistentSignature(Restored->Inventory[I]),
                  FableRPG::ItemPersistentSignature(P->Inventory[I]));
    }

    // 기대값과 상태 보존 검증 처리
    TestEqual(TEXT("Equipped weapon is retained"), Restored->EquippedWeaponId, 5);
    TestEqual(TEXT("Equipped armor is retained"), Restored->EquippedArmorId, 6);

    return true;
}
#endif
