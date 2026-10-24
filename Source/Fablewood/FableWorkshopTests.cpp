#include "FableRPGTypes.h"
#include "FableSaveGame.h"
#include "FablePlayerState.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// This friend exists only in development automation builds. Never initialize a real profile,
// open a save slot, or enable persistence in these isolated unit fixtures.

/** 공방 회귀 검증에 필요한 제한된 내부 접근 도우미. */
struct FFableWorkshopTestAccess
{
    /** 격리 검증에 필요한 초기 프로필·장비·성장 상태를 준비한다. */
    static void Prepare(AFablePlayerState* State, int32 NextId)
    {
        State->bPersistenceEnabled = false;
        State->ProfileSlot.Empty();
        State->bStorageBlocked = false;
        State->bProfileLoaded = true;
        State->NextItemId = NextId;
    }

    /** 메모리 전용 여부를 확인한다. */
    static bool IsMemoryOnly(const AFablePlayerState* State)
    {
        return !State->bPersistenceEnabled && State->ProfileSlot.IsEmpty();
    }

    /** 다음 아이템 식별자 값을 설정한다. */
    static void SetNextId(AFablePlayerState* State, int32 NextId)
    {
        State->NextItemId = NextId;
    }
};

namespace
{
    /** 공방 장비 난수 생성에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    FFableItem WorkshopRoll(int32 Id, int32 Rarity, EFableEquipmentSlot Slot,
                            EFableEquipmentSet Set = EFableEquipmentSet::None, EFableClass Class = EFableClass::Warrior)
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Item;
        Item.Id = Id;
        Item.Rarity = Rarity;
        Item.Slot = Slot;
        Item.SetId = Set;
        Item.HeroClass = Class;
        FRandomStream Random(31579 + Id * 97 + Rarity * 31 + int32(Slot));
        FableRPG::RollItem(Item, FableRPG::CraftNominalPower(Rarity, Slot), EFableDifficulty::Normal, Random);

        return Item;
    }

    /** 공방 프로필에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    UFableSaveGame* WorkshopProfile(int32 Version = 6)
    {
        // 재화와 작업 비용에 필요한 값 준비
        UFableSaveGame* Profile = NewObject<UFableSaveGame>();
        Profile->Version = Version;
        Profile->Gold = 137;
        Profile->Ore = 9;
        Profile->HealthPotions = 2;
        Profile->ManaPotions = 1;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Index = 0; Index < 3; ++Index)
        {
            FFableItem Item;
            Item.Id = Index + 1;
            Item.HeroClass = EFableClass(Index);
            Profile->Inventory.Add(Item);
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Armor;
        Armor.Id = 4;
        Armor.Slot = EFableEquipmentSlot::Armor;
        Armor.Power = 0;
        Profile->Inventory.Add(Armor);

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Slot = 0; Slot < 2; ++Slot)
        {
            FFableItem Item;

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Version >= 5)
            {
                Item = WorkshopRoll(5 + Slot, 2, EFableEquipmentSlot(Slot), EFableEquipmentSet::RootGuardian);
            }
            else
            {
                Item.Id = 5 + Slot;
                Item.Rarity = 2;
                Item.Slot = EFableEquipmentSlot(Slot);
                Item.Power = Slot == 0 ? 48 : 12;
            }

            Item.Upgrade = Slot == 0 ? 3 : 2;

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Version < 6)
            {
                Item.DurabilityVersion = 0;
                Item.Durability = Item.MaxDurability = 100;
            }

            Profile->Inventory.Add(Item);
        }

        Profile->NextItemId = 7;

        return Profile;
    }

    /** 실제 게임 상태를 격리해 구성하고 종료 시 해제하는 검증 월드. */
    struct FWorkshopTestWorld
    {
        UWorld* World = nullptr;

        /** 공방 회귀 검증용 월드와 플레이어를 만들고 이전 실행 문맥을 보존한다. */
        FWorkshopTestWorld()
        {
            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(false)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(false)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
        }

        /** 검증 월드와 소유 객체를 해제하고 이전 실행 문맥을 복구한다. */
        ~FWorkshopTestWorld()
        {
            if (World)
            {
                World->DestroyWorld(false);
            }
        }

        /** 생성 상태에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
        AFablePlayerState* MakeState()
        {
            // 현재 월드와 플레이어 참조 조건 검사
            if (!World)
            {
                return nullptr;
            }

            AFablePlayerState* State = World->SpawnActor<AFablePlayerState>();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!State)
            {
                return nullptr;
            }

            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            const UFableSaveGame* Profile = WorkshopProfile();
            FFableWorkshopTestAccess::Prepare(State, Profile->NextItemId);
            State->Inventory = Profile->Inventory;
            State->EquippedWeaponId = 5;
            State->EquippedArmorId = 6;
            State->Gold = 5000;
            State->Ore = 1000;
            State->Essence = 1000;
            State->RootCores = 10;
            State->MoonCrystals = 10;

            return State;
        }
    };

    /** 공방 장비 옵션에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    FFableItemAffix WorkshopAffix(EFableItemStat Stat, int32 Value)
    {
        FFableItemAffix Affix;
        Affix.Stat = Stat;
        Affix.Value = Value;
        return Affix;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableWorkshopMigrationTest, "Fablewood.Workshop.V6MigrationAndStrictRejection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 공방 버전 이관 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableWorkshopMigrationTest::RunTest(const FString& Parameters)
{
    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Version = 1; Version <= 5; ++Version)
    {
        // 프로필과 저장 스냅숏에 필요한 값 준비
        UFableSaveGame* Profile = WorkshopProfile(Version);
        TArray<FString> Before;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& Item : Profile->Inventory)
        {
            Before.Add(FableRPG::GearIdentitySignature(Item));
        }

        // 기대값과 상태 보존 검증 처리
        if (!TestTrue(FString::Printf(TEXT("Valid schema v%d migrates"), Version), Profile->MigrateAndValidate()))
        {
            continue;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestEqual(TEXT("Migration reaches v9"), Profile->Version, 9);
        TestEqual(TEXT("Migration retains original gold"), Profile->Gold, 137);
        TestEqual(TEXT("Migration retains original ore"), Profile->Ore, 9);
        TestEqual(TEXT("Migration adds no equipment"), Profile->Inventory.Num(), Before.Num());

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Index = 0; Index < Before.Num(); ++Index)
        {
            const FFableItem& Item = Profile->Inventory[Index];
            TestEqual(TEXT("Legacy IDs, performance, upgrades, options and sets stay identical"),
                      FableRPG::GearIdentitySignature(Item), Before[Index]);
            TestEqual(TEXT("Migrated durability schema is legacy zero"), Item.DurabilityVersion, 0);
            TestEqual(TEXT("Migrated equipment starts at 100/100"), Item.Durability, 100);
            TestEqual(TEXT("Legacy maximum remains 100 at every rarity"), Item.MaxDurability, 100);
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestTrue(TEXT("New workshop currencies and stash start empty"),
                 Profile->Essence == 0 && Profile->RootCores == 0 && Profile->MoonCrystals == 0 &&
                     Profile->StashItems.IsEmpty());
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const auto Reject = [&](const TCHAR* Name, TFunctionRef<void(UFableSaveGame*)> Corrupt)
    {
        UFableSaveGame* Profile = WorkshopProfile();
        Corrupt(Profile);
        TestFalse(Name, Profile->MigrateAndValidate());
    };
    Reject(TEXT("v6 rejects negative durability"),
           [](UFableSaveGame* P)
           {
               P->Inventory[4].Durability = -1;
           });
    Reject(TEXT("v6 rejects durability above maximum"),
           [](UFableSaveGame* P)
           {
               P->Inventory[4].Durability = 101;
           });
    Reject(TEXT("v6 rejects forged grade maximum"),
           [](UFableSaveGame* P)
           {
               P->Inventory[4].MaxDurability = 99;
           });
    Reject(TEXT("v6 rejects unknown durability schema"),
           [](UFableSaveGame* P)
           {
               P->Inventory[4].DurabilityVersion = 2;
           });
    Reject(TEXT("Legendary equipment cannot claim old roll schema 1"),
           [](UFableSaveGame* P)
           {
               P->Inventory[4] = WorkshopRoll(5, 4, EFableEquipmentSlot::Weapon);
               P->Inventory[4].RollVersion = 1;
           });
    Reject(TEXT("Unsupported schema 10 is not normalized"),
           [](UFableSaveGame* P)
           {
               P->Version = 10;
           });
    Reject(TEXT("Inventory IDs must be unique"),
           [](UFableSaveGame* P)
           {
               P->Inventory[5].Id = 5;
           });
    Reject(TEXT("Inventory and stash share a single ID namespace"),
           [](UFableSaveGame* P)
           {
               P->StashItems.Add(P->Inventory[4]);
           });
    Reject(TEXT("Stash IDs must also be unique"),
           [](UFableSaveGame* P)
           {
               const FFableItem I = WorkshopRoll(7, 1, EFableEquipmentSlot::Weapon);
               P->StashItems = {I, I};
               P->NextItemId = 8;
           });
    Reject(TEXT("Starter items cannot reside in the stash"),
           [](UFableSaveGame* P)
           {
               P->StashItems.Add(P->Inventory[0]);
           });
    Reject(TEXT("Equipped items must remain in the inventory"),
           [](UFableSaveGame* P)
           {
               P->EquippedWeaponId = 5;
               P->StashItems.Add(P->Inventory[4]);
               P->Inventory.RemoveAt(4);
           });
    Reject(TEXT("Next ID must exceed stash IDs too"),
           [](UFableSaveGame* P)
           {
               P->StashItems.Add(WorkshopRoll(100, 1, EFableEquipmentSlot::Weapon));
           });
    Reject(TEXT("Next ID has an upper bound"),
           [](UFableSaveGame* P)
           {
               P->NextItemId = 1000000;
           });
    Reject(TEXT("Inventory rejects its 33rd item"),
           [](UFableSaveGame* P)
           {
               for (int32 I = 7; P->Inventory.Num() < 33; ++I)
               {
                   P->Inventory.Add(WorkshopRoll(I, 1, EFableEquipmentSlot::Armor));
               }
               P->NextItemId = 100;
           });
    Reject(TEXT("Stash rejects its 49th item"),
           [](UFableSaveGame* P)
           {
               for (int32 I = 7; I < 56; ++I)
               {
                   P->StashItems.Add(WorkshopRoll(I, 1, EFableEquipmentSlot::Armor));
               }
               P->NextItemId = 100;
           });
    Reject(TEXT("Gold cannot exceed its cap"),
           [](UFableSaveGame* P)
           {
               P->Gold = 1000001;
           });
    Reject(TEXT("Ore cannot exceed its cap"),
           [](UFableSaveGame* P)
           {
               P->Ore = 100001;
           });
    Reject(TEXT("Essence cannot exceed its cap"),
           [](UFableSaveGame* P)
           {
               P->Essence = 100001;
           });
    Reject(TEXT("Root cores cannot exceed their cap"),
           [](UFableSaveGame* P)
           {
               P->RootCores = 100001;
           });
    Reject(TEXT("Moon crystals cannot exceed their cap"),
           [](UFableSaveGame* P)
           {
               P->MoonCrystals = 100001;
           });
    Reject(TEXT("Negative workshop materials are rejected"),
           [](UFableSaveGame* P)
           {
               P->Essence = -1;
           });
    UFableSaveGame* Limits = WorkshopProfile();
    Limits->Gold = 1000000;
    Limits->Ore = Limits->Essence = Limits->RootCores = Limits->MoonCrystals = 100000;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    while (Limits->Inventory.Num() < 32)
    {
        Limits->Inventory.Add(WorkshopRoll(Limits->NextItemId++, 1, EFableEquipmentSlot::Armor));
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    while (Limits->StashItems.Num() < 48)
    {
        Limits->StashItems.Add(WorkshopRoll(Limits->NextItemId++, 1, EFableEquipmentSlot::Armor));
    }

    TestTrue(TEXT("Exact inventory, stash and currency caps remain valid"), Limits->MigrateAndValidate());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableWorkshopLegendSaveTest, "Fablewood.Workshop.LegendaryRollAndMemoryRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 공방 전설 장비 저장 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableWorkshopLegendSaveTest::RunTest(const FString& Parameters)
{
    FRandomStream Random(441305);
    TSet<int32> Qualities;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Index = 0; Index < 400; ++Index)
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Item;
        Item.Id = 5;
        Item.Rarity = 4;
        Item.Slot = EFableEquipmentSlot(Index % 2);

        // 기대값과 상태 보존 검증 조건 검사
        if (!TestTrue(TEXT("Production factory makes legendary equipment"),
                      FableRPG::RollItem(Item, Index % 2 == 0 ? 74 : 22, EFableDifficulty::Normal, Random)))
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestTrue(TEXT("Legendary roll and durability are valid"),
                 FableRPG::IsValidItemRoll(Item) && FableRPG::IsValidItemDurability(Item));
        TestEqual(TEXT("Legendary rarity is independent of quality"), Item.Rarity, 4);
        TestEqual(TEXT("Legendary items use roll schema 2"), Item.RollVersion, 2);
        TestTrue(TEXT("Legendary basic power is capped at 104"), Item.Power <= 104);
        TestEqual(TEXT("New legendary items start at grade maximum 150"), Item.Durability, 150);
        TestEqual(TEXT("New legendary maximum is 150"), Item.MaxDurability, 150);
        TestEqual(TEXT("New durability uses schema 1"), Item.DurabilityVersion, 1);
        Qualities.Add(int32(Item.Quality));
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    TestEqual(TEXT("All four qualities occur within legendary rarity"), Qualities.Num(), 4);
    FFableItem UpperBoundary = WorkshopRoll(5, 4, EFableEquipmentSlot::Weapon);
    UpperBoundary.Quality = EFableItemQuality::Finest;
    UpperBoundary.RollPercent = 140;
    UpperBoundary.Power = 104;
    UpperBoundary.Affixes = {WorkshopAffix(EFableItemStat::Attack, 6), WorkshopAffix(EFableItemStat::Defense, 4),
                             WorkshopAffix(EFableItemStat::MaxHealth, 20)};
    TestTrue(TEXT("The exact 104-power legendary upper boundary is valid"), FableRPG::IsValidItemRoll(UpperBoundary));
    FFableItem BeyondBoundary = UpperBoundary;
    BeyondBoundary.Power = 105;
    TestFalse(TEXT("Legendary power beyond the schema boundary is rejected"),
              FableRPG::IsValidItemRoll(BeyondBoundary));
    const int32 ExpectedMax[] = {60, 80, 100, 120, 150};

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Rarity = 0; Rarity <= 4; ++Rarity)
    {
        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestEqual(TEXT("Each rarity has its specified durability maximum"), FableRPG::GradeMaxDurability(Rarity),
                  ExpectedMax[Rarity]);
        FFableItem Item = Rarity == 0 ? FFableItem() : WorkshopRoll(5, Rarity, EFableEquipmentSlot::Weapon);

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Rarity == 0)
        {
            Item.Id = 1;
            Item.DurabilityVersion = 1;
            Item.MaxDurability = Item.Durability = 60;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestTrue(TEXT("Every new grade has valid full durability"), FableRPG::IsValidItemDurability(Item));
        TestEqual(TEXT("The factory initializes each new grade correctly"), Item.MaxDurability, ExpectedMax[Rarity]);
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    UFableSaveGame* Profile = WorkshopProfile();
    Profile->HeroClass = EFableClass::Mage;
    Profile->Level = 5;
    Profile->Experience = 37;
    Profile->Inventory[4] = UpperBoundary;
    Profile->Inventory[4].HeroClass = EFableClass::Mage;
    Profile->Inventory[4].Upgrade = 3;
    Profile->Inventory[4].Durability = 47;
    Profile->Inventory[5] = WorkshopRoll(6, 4, EFableEquipmentSlot::Armor);
    Profile->Inventory[5].Durability = 0;
    Profile->EquippedWeaponId = 5;
    Profile->EquippedArmorId = 6;
    Profile->StashItems = {
        WorkshopRoll(7, 3, EFableEquipmentSlot::Weapon, EFableEquipmentSet::Moonlight, EFableClass::Archer),
        WorkshopRoll(8, 1, EFableEquipmentSlot::Armor)};
    Profile->StashItems[0].Upgrade = 2;
    Profile->StashItems[0].Durability = 0;
    Profile->StashItems[1].Durability = 21;
    Profile->NextItemId = 9;
    Profile->Essence = 44;
    Profile->RootCores = 2;
    Profile->MoonCrystals = 4;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("The complete fixture is valid before serialization"), Profile->IsValidProfile()))
    {
        return false;
    }

    TArray<uint8> Bytes;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Unreal serializes the workshop profile to memory only"),
                  UGameplayStatics::SaveGameToMemory(Profile, Bytes)))
    {
        return false;
    }

    UFableSaveGame* Restored = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Unreal restores the in-memory workshop profile"), Restored))
    {
        return false;
    }

    // 기대값과 상태 보존 검증 처리
    TestTrue(TEXT("Restored v6 profile validates without changing its data"), Restored->MigrateAndValidate());
    TestEqual(TEXT("Supported v6 memory serialization migrates to v9"), Restored->Version, 9);

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!TestEqual(TEXT("Inventory length survives"), Restored->Inventory.Num(), Profile->Inventory.Num()) ||
        !TestEqual(TEXT("Stash length survives"), Restored->StashItems.Num(), Profile->StashItems.Num()))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Index = 0; Index < Profile->Inventory.Num(); ++Index)
    {
        TestEqual(TEXT("Inventory metadata, options and partial/broken durability survive exactly"),
                  FableRPG::ItemPersistentSignature(Restored->Inventory[Index]),
                  FableRPG::ItemPersistentSignature(Profile->Inventory[Index]));
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Index = 0; Index < Profile->StashItems.Num(); ++Index)
    {
        TestEqual(TEXT("Stash metadata, options and partial/broken durability survive exactly"),
                  FableRPG::ItemPersistentSignature(Restored->StashItems[Index]),
                  FableRPG::ItemPersistentSignature(Profile->StashItems[Index]));
    }

    // 재화와 작업 비용 처리
    TestTrue(TEXT("Class, level and experience survive"),
             Restored->HeroClass == Profile->HeroClass && Restored->Level == 5 && Restored->Experience == 37);
    TestTrue(TEXT("Every currency survives"), Restored->Gold == Profile->Gold && Restored->Ore == Profile->Ore &&
                                                  Restored->Essence == 44 && Restored->RootCores == 2 &&
                                                  Restored->MoonCrystals == 4);
    TestTrue(TEXT("Equipped IDs and next ID survive"),
             Restored->EquippedWeaponId == 5 && Restored->EquippedArmorId == 6 && Restored->NextItemId == 9);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableWorkshopStatsQueryTest, "Fablewood.Workshop.BrokenStatsPreviewAndQueries",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 공방 능력치 조회 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableWorkshopStatsQueryTest::RunTest(const FString& Parameters)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    FWorkshopTestWorld Fixture;
    AFablePlayerState* State = Fixture.MakeState();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("An isolated non-playing world provides a real PlayerState"), State))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    TestTrue(TEXT("The unit fixture cannot write any save slot"), FFableWorkshopTestAccess::IsMemoryOnly(State));
    State->Level = 5;
    State->ClassProgress[0].Ranks[3] = 1;
    FFableItem& Weapon = State->Inventory[4];
    FFableItem& Armor = State->Inventory[5];
    Weapon.Quality = Armor.Quality = EFableItemQuality::High;
    Weapon.RollPercent = Armor.RollPercent = 110;
    Weapon.Power = 53;
    Weapon.Upgrade = 1;
    Armor.Power = 13;
    Armor.Upgrade = 2;
    Weapon.Affixes = {WorkshopAffix(EFableItemStat::Attack, 4), WorkshopAffix(EFableItemStat::MaxHealth, 10)};
    Armor.Affixes = {WorkshopAffix(EFableItemStat::Defense, 3), WorkshopAffix(EFableItemStat::ManaRegen, 10)};

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("The exact-stat fixture obeys production roll validation"),
                  FableRPG::IsValidItemRoll(Weapon) && FableRPG::IsValidItemRoll(Armor)))
    {
        return false;
    }

    // 체력·마나·보호막·피해량에 필요한 값 준비
    const FString WeaponIdentity = FableRPG::GearIdentitySignature(Weapon),
                  ArmorIdentity = FableRPG::GearIdentitySignature(Armor);
    TestTrue(TEXT("Intact base, upgrade, affixes, level, passive and set all contribute"),
             State->GetAttackPower() == 78 && State->GetDefense() == 26 && State->GetMaxHealthBonus() == 52 &&
                 State->GetManaRegenRate() == 9);
    Weapon.Durability = 0;
    TestTrue(TEXT("Broken weapon loses power, its affixes and the two-piece set"),
             State->GetAttackPower() == 20 && State->GetDefense() == 23 && State->GetMaxHealthBonus() == 42 &&
                 State->GetManaRegenRate() == 9);
    TestEqual(TEXT("A broken set piece is not counted"), State->GetEquippedSetCount(EFableEquipmentSet::RootGuardian),
              1);
    Weapon.Durability = Weapon.MaxDurability;
    Armor.Durability = 0;
    TestTrue(TEXT("Broken armor loses defense, its affixes, rarity health and the set"),
             State->GetAttackPower() == 73 && State->GetDefense() == 3 && State->GetMaxHealthBonus() == 42 &&
                 State->GetManaRegenRate() == 8);
    Weapon.Durability = 0;
    TestTrue(TEXT("Both broken pieces leave only bare hands, level and class passive"),
             State->GetAttackPower() == 20 && State->GetDefense() == 3 && State->GetMaxHealthBonus() == 32 &&
                 State->GetManaRegenRate() == 8);
    FFableItem Candidate = Weapon;
    Candidate.Id = 7;
    Candidate.Durability = Candidate.MaxDurability;
    State->Inventory.Add(Candidate);
    FFableWorkshopTestAccess::SetNextId(State, 8);
    FFableEquipmentStats Before, After;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Preview accepts a repaired replacement"), State->GetEquipmentPreview(7, Before, After)))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    State->EquippedWeaponId = 7;
    TestTrue(TEXT("Preview matches actual replacement statistics"),
             Before.Attack == 20 && After.Attack == State->GetAttackPower() && After.Defense == State->GetDefense() &&
                 After.MaxHealth == State->GetMaxHealthBonus() && After.ManaRegen == State->GetManaRegenRate());
    State->EquippedWeaponId = 5;
    State->Inventory[4].Durability = 100;
    State->Inventory[5].Durability = 100;
    TestTrue(TEXT("Restoring durability restores exactly the original effects"),
             State->GetAttackPower() == 78 && State->GetDefense() == 26 && State->GetMaxHealthBonus() == 52 &&
                 State->GetManaRegenRate() == 9);
    TestEqual(TEXT("Break and restoration never alter weapon identity"),
              FableRPG::GearIdentitySignature(State->Inventory[4]), WeaponIdentity);
    TestEqual(TEXT("Break and restoration never alter armor identity"),
              FableRPG::GearIdentitySignature(State->Inventory[5]), ArmorIdentity);
    const int32 Costs[4][5] = {{80, 3, 8, 0, 0}, {150, 6, 16, 1, 0}, {240, 10, 24, 0, 1}, {500, 20, 50, 1, 2}};

    // SQL 명령과 조회 결과 순회
    for (int32 Rarity = 1; Rarity <= 4; ++Rarity)
    {
        const FFableResourceCost Cost = State->GetCraftCost(Rarity);
        const int32* Expected = Costs[Rarity - 1];
        TestTrue(TEXT("Craft query uses the agreed complete resource cost"),
                 Cost.Gold == Expected[0] && Cost.Ore == Expected[1] && Cost.Essence == Expected[2] &&
                     Cost.RootCores == Expected[3] && Cost.MoonCrystals == Expected[4]);
        TestTrue(TEXT("Funded valid craft is offered"),
                 State->GetCraftBlockReason(Rarity, EFableEquipmentSlot::Weapon).IsEmpty());
    }

    // 재화와 작업 비용 처리
    TestFalse(TEXT("Unsupported craft grade is blocked"),
              State->GetCraftBlockReason(0, EFableEquipmentSlot::Weapon).IsEmpty());
    State->Gold = 0;
    TestFalse(TEXT("Unfunded craft is blocked"), State->GetCraftBlockReason(4, EFableEquipmentSlot::Weapon).IsEmpty());
    State->Gold = 5000;
    TestFalse(TEXT("Starter dismantling is blocked"), State->GetDismantleBlockReason(1).IsEmpty());
    TestFalse(TEXT("Equipped dismantling is blocked"), State->GetDismantleBlockReason(5).IsEmpty());
    TestFalse(TEXT("Starter deposit is blocked"), State->GetDepositBlockReason(1).IsEmpty());
    TestFalse(TEXT("Equipped deposit is blocked"), State->GetDepositBlockReason(6).IsEmpty());
    const FFableResourceCost Yield = State->GetDismantleYield(7), Reroll = State->GetRerollCost(7);
    TestEqual(TEXT("Dismantling yields essence from rarity, quality and upgrade"), Yield.Essence, 14);
    TestTrue(TEXT("Reroll price is 110 gold and 7 essence for rarity2 upgrade1"),
             Reroll.Gold == 110 && Reroll.Essence == 7 && Reroll.Ore == 0 && Reroll.RootCores == 0 &&
                 Reroll.MoonCrystals == 0);
    TestTrue(TEXT("A funded valid affix can be rerolled"), State->GetRerollBlockReason(7, 0).IsEmpty());
    TestFalse(TEXT("A fixed starter has no affix to reroll"), State->GetRerollBlockReason(1, 0).IsEmpty());
    TestFalse(TEXT("An absent affix cannot be rerolled"), State->GetRerollBlockReason(7, 99).IsEmpty());
    State->Essence = 99986;
    TestTrue(TEXT("Dismantle yield may exactly reach the cap"), State->GetDismantleBlockReason(7).IsEmpty());
    State->Essence = 99987;
    TestFalse(TEXT("Dismantle cannot exceed the essence cap"), State->GetDismantleBlockReason(7).IsEmpty());
    State->Essence = 1000;
    State->Inventory[4].Durability = 75;
    FFableResourceCost Repair = State->GetRepairCost(5);
    TestTrue(TEXT("25 missing durability costs 75 gold and one ore"), Repair.Gold == 75 && Repair.Ore == 1);
    State->Inventory[4].Durability = 74;
    Repair = State->GetRepairCost(5);
    TestTrue(TEXT("Repair ore rounds 26 missing durability up to two"), Repair.Gold == 78 && Repair.Ore == 2);
    TestTrue(TEXT("Equipped damaged equipment remains repairable"), State->GetRepairBlockReason(5).IsEmpty());
    State->Inventory[4].Durability = 100;
    TestFalse(TEXT("Full durability cannot be repaired for an effect"), State->GetRepairBlockReason(5).IsEmpty());

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Id = 8; State->StashItems.Num() < 48; ++Id)
    {
        State->StashItems.Add(WorkshopRoll(Id, 1, EFableEquipmentSlot::Armor));
    }

    TestFalse(TEXT("Deposit is blocked at 48 stash items"), State->GetDepositBlockReason(7).IsEmpty());

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Id = 100; State->Inventory.Num() < 32; ++Id)
    {
        State->Inventory.Add(WorkshopRoll(Id, 1, EFableEquipmentSlot::Armor));
    }

    // 기대값과 상태 보존 검증 처리
    TestFalse(TEXT("Craft is blocked at 32 inventory items"),
              State->GetCraftBlockReason(1, EFableEquipmentSlot::Armor).IsEmpty());
    TestFalse(TEXT("Withdrawal is blocked at 32 inventory items"), State->GetWithdrawBlockReason(8).IsEmpty());
    AddInfo(TEXT(
        "Stats, preview and workshop queries use an isolated native PlayerState fixture; no NPC interaction, gameplay action or disk save is claimed."));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableWorkshopWearTest, "Fablewood.Workshop.ServerSlotWearCooldown",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 공방 내구도 소모 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableWorkshopWearTest::RunTest(const FString& Parameters)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    FWorkshopTestWorld Fixture;
    AFablePlayerState* State = Fixture.MakeState();

    // 서버 권한과 실행 위치 조건 검사
    if (!TestNotNull(TEXT("Wear fixture has an authoritative isolated PlayerState"), State))
    {
        return false;
    }
    if (!TestTrue(TEXT("Wear fixture has persistence disabled and no slot name"),
                  FFableWorkshopTestAccess::IsMemoryOnly(State) && State->HasAuthority()))
    {
        return false;
    }

    // 기대값과 상태 보존 검증 처리
    Fixture.World->TimeSeconds = 100;
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    State->WearEquipment(EFableEquipmentSlot::Armor);
    TestTrue(TEXT("First actual-damage notifications wear both slots independently"),
             State->FindItem(5)->Durability == 99 && State->FindItem(6)->Durability == 99);

    // 같은 규칙을 적용할 대상을 순회한다.
    for (int32 Hit = 0; Hit < 20; ++Hit)
    {
        State->WearEquipment(EFableEquipmentSlot::Weapon);
        State->WearEquipment(EFableEquipmentSlot::Armor);
    }

    // 기대값과 상태 보존 검증 처리
    TestTrue(TEXT("Area, multi-shot and repeated notifications cannot multiply wear"),
             State->FindItem(5)->Durability == 99 && State->FindItem(6)->Durability == 99);
    State->EquippedWeaponId = 1;
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    TestEqual(TEXT("Changing equipped item cannot reset the slot cooldown"), State->FindItem(1)->Durability, 100);
    State->EquippedWeaponId = 5;
    Fixture.World->TimeSeconds = 109.99;
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    TestEqual(TEXT("The ten-second boundary is exclusive before expiry"), State->FindItem(5)->Durability, 99);
    Fixture.World->TimeSeconds = 110;
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    State->WearEquipment(EFableEquipmentSlot::Armor);
    TestTrue(TEXT("Both slots can wear again exactly at ten seconds"),
             State->FindItem(5)->Durability == 98 && State->FindItem(6)->Durability == 98);
    Fixture.World->TimeSeconds = 120;
    State->Inventory[4].Durability = 1;
    State->WearEquipment(EFableEquipmentSlot::Weapon, 0);
    State->WearEquipment(EFableEquipmentSlot::Weapon, 2);
    State->WearEquipment(EFableEquipmentSlot(255));
    TestEqual(TEXT("Only one unit on a recognized slot is accepted"), State->FindItem(5)->Durability, 1);
    State->bProfileLoaded = false;
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    TestEqual(TEXT("Unloaded profiles cannot be worn"), State->FindItem(5)->Durability, 1);
    State->bProfileLoaded = true;
    const FString Identity = FableRPG::GearIdentitySignature(*State->FindItem(5));
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    TestEqual(TEXT("The last durability point reaches zero"), State->FindItem(5)->Durability, 0);
    Fixture.World->TimeSeconds = 1000;
    State->WearEquipment(EFableEquipmentSlot::Weapon);
    TestEqual(TEXT("Broken equipment never wears below zero"), State->FindItem(5)->Durability, 0);
    TestEqual(TEXT("Wear preserves rolled performance, options and upgrades"),
              FableRPG::GearIdentitySignature(*State->FindItem(5)), Identity);
    TestTrue(TEXT("The fixture remains memory-only after wear invokes SaveProfile"),
             FFableWorkshopTestAccess::IsMemoryOnly(State));
    AddInfo(TEXT(
        "Server wear notifications and world time are controlled unit fixtures. Actual weapon and armor wear after dungeon damage is observed by the separate gameplay runner; shield-only and miss comparisons are not claimed."));

    return true;
}
#endif
