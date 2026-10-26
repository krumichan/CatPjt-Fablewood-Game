#include "FableInventorySnapshot.h"
#include "FableSaveGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    /** 인벤토리 검증 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    UFableSaveGame* InventoryFixture(int32 Version = 8)
    {
        // 재화와 작업 비용에 필요한 값 준비
        auto P = NewObject<UFableSaveGame>();
        P->Version = Version;
        P->Gold = 137;
        P->Ore = 9;
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
        FFableItem Weapon;
        Weapon.Id = 5;
        Weapon.Rarity = 2;
        Weapon.Power = 48;
        Weapon.Upgrade = 3;

        // 이 단계에서 인벤토리 검증 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Version >= 5)
        {
            Weapon.SetId = EFableEquipmentSet::RootGuardian;
            FRandomStream Random(117);
            FableRPG::RollItem(Weapon, 48, EFableDifficulty::Hard, Random);
        }

        // 이 단계에서 인벤토리 검증 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Version >= 6)
        {
            Weapon.Durability = 43;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        P->Inventory.Add(Weapon);
        P->NextItemId = 6;
        P->EquippedWeaponId = 5;

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Version >= 6)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            FFableItem Stored;
            Stored.Id = 6;
            Stored.Rarity = 4;
            Stored.Slot = EFableEquipmentSlot::Armor;
            FRandomStream Random(227);
            FableRPG::RollItem(Stored, 22, EFableDifficulty::Normal, Random);
            Stored.Durability = 0;
            Stored.Upgrade = 2;
            P->StashItems.Add(Stored);
            P->NextItemId = 7;
            P->Essence = 17;
            P->RootCores = 3;
            P->MoonCrystals = 4;
        }

        return P;
    }

    /** 인벤토리와 창고 아이템의 전역 식별자를 검증용 목록으로 구한다. */
    TArray<FGuid> Identities(const UFableSaveGame* P)
    {
        TArray<FGuid> Result;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->Inventory)
        {
            Result.Add(I.InstanceId);
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->StashItems)
        {
            Result.Add(I.InstanceId);
        }

        return Result;
    }

    /** 저장 계약의 의미를 바꾸지 않고 JSON 객체 속성 순서를 바꾼 검증 입력을 만든다. */
    TSharedPtr<FJsonObject> ReorderObjects(const TSharedPtr<FJsonObject>& Source);

    /** JSON 객체와 배열의 값을 재귀적으로 복사해 순서 독립성을 검증한다. */
    TSharedPtr<FJsonValue> ReorderValue(const TSharedPtr<FJsonValue>& Source)
    {
        // JSON 계약과 검증 기록 조건 검사
        if (Source->Type == EJson::Object)
        {
            return MakeShared<FJsonValueObject>(ReorderObjects(Source->AsObject()));
        }

        // JSON 계약과 검증 기록 처리
        if (Source->Type == EJson::Array)
        {
            TArray<TSharedPtr<FJsonValue>> Values;
            for (const auto& V : Source->AsArray())
            {
                Values.Add(ReorderValue(V));
            }
            return MakeShared<FJsonValueArray>(Values);
        }

        return Source;
    }

    /** 저장 계약의 의미를 바꾸지 않고 JSON 객체 속성 순서를 바꾼 검증 입력을 만든다. */
    TSharedPtr<FJsonObject> ReorderObjects(const TSharedPtr<FJsonObject>& Source)
    {
        // JSON 계약과 검증 기록에 필요한 값 준비
        auto Result = MakeShared<FJsonObject>();
        TArray<FJsonObject::FStringType> Keys;
        Source->Values.GetKeys(Keys);
        // Reverse the actual insertion order; UE 5.8 stores keys as FSharedString.

        // 같은 규칙을 적용할 대상을 순회한다.
        for (int32 I = Keys.Num() - 1; I >= 0; --I)
        {
            Result->SetField(FStringView(*Keys[I], Keys[I].Len()), ReorderValue(Source->Values[Keys[I]]));
        }

        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableInventoryMigrationTest,
                                 "Fablewood.Inventory.V9IdentityMigrationAndStrictRejection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 인벤토리 버전 이관 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableInventoryMigrationTest::RunTest(const FString& Parameters)
{
    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Version = 0; Version <= 8; ++Version)
    {
        auto P = InventoryFixture(Version);
        TArray<FString> Before;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const auto& I : P->Inventory)
        {
            Before.Add(FableRPG::GearIdentitySignature(I));
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const FGuid Existing(0x12345678, 0xABCD4321, 0x87654321, 0xABCDEF01);
        P->Inventory[0].InstanceId = Existing;

        // 기대값과 상태 보존 검증 처리
        if (!TestTrue(FString::Printf(TEXT("Historical schema %d migrates"), Version), P->MigrateAndValidate()))
        {
            continue;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestEqual(TEXT("Migration reaches current schema"), P->Version, FableProfileSchema::CurrentVersion);
        TestTrue(TEXT("Existing identity is never reassigned"), P->Inventory[0].InstanceId == Existing);
        TestEqual(TEXT("No equipment is added"), P->Inventory.Num(), Before.Num());

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 I = 0; I < Before.Num(); ++I)
        {
            TestEqual(TEXT("Local IDs and all legacy performance are retained"),
                      FableRPG::GearIdentitySignature(P->Inventory[I]), Before[I]);
        }

        const auto Ids = Identities(P);
        TSet<FGuid> Seen;

        // 기대값과 상태 보존 검증 순회
        for (const auto& Id : Ids)
        {
            TestTrue(TEXT("All migrated identities are nonempty and globally unique within the profile"),
                     Id.IsValid() && !Seen.Contains(Id));
            Seen.Add(Id);
        }

        // 재화와 작업 비용 처리
        TestTrue(TEXT("Migration is idempotent"), P->MigrateAndValidate() && Identities(P) == Ids);
        TestTrue(TEXT("Original currency is retained"), P->Gold == 137 && P->Ore == 9);
        TestTrue(TEXT("Original equipment references are retained"),
                 P->EquippedWeaponId == 5 && P->EquippedArmorId == 4);

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Version >= 6)
        {
            TestTrue(TEXT("Migration preserves broken stash, partial durability and materials"),
                     P->StashItems.Num() == 1 && P->StashItems[0].Durability == 0 && P->Inventory[4].Durability == 43 &&
                         P->Essence == 17 && P->RootCores == 3 && P->MoonCrystals == 4);
        }
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    auto Current = InventoryFixture();
    TestTrue(TEXT("Create current fixture"), Current->MigrateAndValidate());
    Current->Inventory[0].InstanceId.Invalidate();
    TestFalse(TEXT("Current missing UUID is rejected, not regenerated"), Current->MigrateAndValidate());
    TestFalse(TEXT("Rejected current UUID remains empty"), Current->Inventory[0].InstanceId.IsValid());
    Current = InventoryFixture();
    Current->MigrateAndValidate();
    Current->StashItems[0].InstanceId = Current->Inventory[4].InstanceId;
    TestFalse(TEXT("UUID duplicates across bag and stash are rejected"), Current->MigrateAndValidate());

    auto Legacy = InventoryFixture();
    Legacy->Inventory[0].InstanceId = Legacy->Inventory[1].InstanceId = FGuid::NewGuid();
    TestFalse(TEXT("An existing duplicate legacy UUID is not silently replaced"), Legacy->MigrateAndValidate());
    TestFalse(TEXT("Rejected migration does not fill other empty identities"),
              Legacy->Inventory[2].InstanceId.IsValid());
    Legacy = InventoryFixture();
    Legacy->Inventory[0].Id = 0;
    TestFalse(TEXT("Legacy integer IDs are not repaired or renumbered"), Legacy->MigrateAndValidate());

    auto Future = InventoryFixture(FableProfileSchema::CurrentVersion + 1);
    TestFalse(TEXT("Unsupported future schema eleven is rejected"), Future->MigrateAndValidate());
    TestEqual(TEXT("Unsupported schema is unchanged"), Future->Version, FableProfileSchema::CurrentVersion + 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableInventorySaveTest, "Fablewood.Inventory.UUIDAndTypedSnapshotMemoryRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 인벤토리 저장 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableInventorySaveTest::RunTest(const FString& Parameters)
{
    auto P = InventoryFixture();

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Migrate source fixture"), P->MigrateAndValidate()))
    {
        return false;
    }

    // 프로필과 저장 스냅숏에 필요한 값 준비
    const auto Before = FableInventorySnapshot::Make(P);
    const auto Ids = Identities(P);

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("A complete typed snapshot can be built"), Before.IsValid()))
    {
        return false;
    }

    TArray<uint8> Bytes;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Unreal saves UUID and equipment to memory"), UGameplayStatics::SaveGameToMemory(P, Bytes)))
    {
        return false;
    }

    auto Restored = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Unreal restores the memory profile"), Restored))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    TestTrue(TEXT("Stored schema validates and UUIDs are byte-for-byte stable"),
             Restored->Version == FableProfileSchema::CurrentVersion && Restored->MigrateAndValidate() &&
                 Identities(Restored) == Ids);
    TestTrue(TEXT("Typed all-field snapshot, array order and equipment references survive"),
             FableInventorySnapshot::Matches(Restored, Before));
    TestTrue(TEXT("Wallet and materials survive"), Restored->Gold == 137 && Restored->Ore == 9 &&
                                                       Restored->Essence == 17 && Restored->RootCores == 3 &&
                                                       Restored->MoonCrystals == 4);
    TestTrue(TEXT("Legendary broken stash and partial equipped durability survive"),
             Restored->StashItems[0].Rarity == 4 && Restored->StashItems[0].MaxDurability == 150 &&
                 Restored->StashItems[0].Durability == 0 && Restored->Inventory[4].Durability == 43);
    // A move uses the original struct, just as DepositItem/WithdrawItem do; no creation/migration runs here.
    const auto Stored = Restored->StashItems[0];
    Restored->StashItems.Reset();
    Restored->Inventory.Add(Stored);
    TestTrue(TEXT("Withdrawal changes location while retaining identity and metadata"),
             Restored->IsValidProfile() && Restored->Inventory.Last().InstanceId == Stored.InstanceId &&
                 FableRPG::ItemPersistentSignature(Restored->Inventory.Last()) ==
                     FableRPG::ItemPersistentSignature(Stored));
    Restored->StashItems.Add(Restored->Inventory.Last());
    Restored->Inventory.Pop();
    TestTrue(TEXT("Deposit round trip restores the exact typed snapshot"),
             FableInventorySnapshot::Matches(Restored, Before));
    const auto Frozen = FableInventorySnapshot::Make(Restored);
    Restored->Gold -= 3;
    --Restored->Inventory[4].Durability;
    TestFalse(TEXT("A queued snapshot is an independent frozen copy"),
              FableInventorySnapshot::Matches(Restored, Frozen));
    TestTrue(TEXT("Prior queued body still matches the original profile"), FableInventorySnapshot::Matches(P, Frozen));
    AddInfo(
        TEXT("Memory-only serialization fixtures; no disk saves, HTTP, services or gameplay mutation were executed."));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableInventoryWireTest, "Fablewood.Inventory.TypedWireSemanticEqualityAndFailClosed",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 인벤토리 전송 계약 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableInventoryWireTest::RunTest(const FString& Parameters)
{
    auto P = InventoryFixture();

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Prepare valid v9 fixture"), P->MigrateAndValidate()))
    {
        return false;
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    const auto Valid = FableInventorySnapshot::Make(P);
    TestTrue(TEXT("Property order is immaterial at every nested object"),
             FableInventorySnapshot::Matches(P, ReorderObjects(Valid)));
    const auto Reject = [&](const TCHAR* Name, TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Mutate)
    {
        const auto Changed = ReorderObjects(Valid);
        Mutate(Changed);
        TestFalse(Name, FableInventorySnapshot::Matches(P, Changed));
    };

    // JSON 계약과 검증 기록 순회
    for (const TCHAR* Field : {TEXT("version"), TEXT("heroClass"), TEXT("nextLocalId")})
    {
        Reject(TEXT("Every header scalar participates in equality"),
               [Field](const auto& J)
               {
                   J->SetNumberField(Field, J->GetNumberField(Field) + 1);
               });
    }

    // JSON 계약과 검증 기록 순회
    for (const TCHAR* Field :
         {TEXT("localId"), TEXT("location"), TEXT("position"), TEXT("slot"), TEXT("heroClass"), TEXT("rarity"),
          TEXT("power"), TEXT("upgrade"), TEXT("rollVersion"), TEXT("quality"), TEXT("nominalPower"),
          TEXT("rollPercent"), TEXT("setId"), TEXT("durabilityVersion"), TEXT("durability"), TEXT("maxDurability")})
    {
        Reject(TEXT("Every item scalar participates in equality"),
               [Field](const auto& J)
               {
                   auto Item = J->GetArrayField(TEXT("items"))[4]->AsObject();
                   Item->SetNumberField(Field, Item->GetNumberField(Field) + 1);
               });
    }

    // JSON 계약과 검증 기록 처리
    Reject(TEXT("Missing typed field is rejected"),
           [](const auto& J)
           {
               J->RemoveField(TEXT("nextLocalId"));
           });
    Reject(TEXT("Unknown typed field is rejected"),
           [](const auto& J)
           {
               J->SetNumberField(TEXT("unexpected"), 1);
           });
    Reject(TEXT("Wrong JSON field type is rejected"),
           [](const auto& J)
           {
               J->SetStringField(TEXT("nextLocalId"), TEXT("7"));
           });
    Reject(TEXT("A foreign UUID cannot replace an item"),
           [](const auto& J)
           {
               J->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(
                   TEXT("instanceId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
           });
    Reject(TEXT("Equipped UUID must match the profile's bag item"),
           [](const auto& J)
           {
               J->SetStringField(TEXT("equippedWeaponInstanceId"), J->GetStringField(TEXT("equippedArmorInstanceId")));
           });
    Reject(TEXT("Bag and stash array order is significant"),
           [](const auto& J)
           {
               auto Items = J->GetArrayField(TEXT("items"));
               Items.Swap(0, 1);
               J->SetArrayField(TEXT("items"), Items);
           });
    Reject(TEXT("Affix values are compared"),
           [](const auto& J)
           {
               auto A = J->GetArrayField(TEXT("items"))[4]->AsObject()->GetArrayField(TEXT("affixes"))[0]->AsObject();
               A->SetNumberField(TEXT("value"), A->GetNumberField(TEXT("value")) + 1);
           });
    Reject(TEXT("Affix stat is compared"),
           [](const auto& J)
           {
               auto A = J->GetArrayField(TEXT("items"))[4]->AsObject()->GetArrayField(TEXT("affixes"))[0]->AsObject();
               A->SetNumberField(TEXT("stat"), 99);
           });
    Reject(TEXT("Missing item is rejected"),
           [](const auto& J)
           {
               auto Items = J->GetArrayField(TEXT("items"));
               Items.Pop();
               J->SetArrayField(TEXT("items"), Items);
           });
    Reject(TEXT("Null item is rejected without dereference"),
           [](const auto& J)
           {
               auto Items = J->GetArrayField(TEXT("items"));
               Items[0] = MakeShared<FJsonValueNull>();
               J->SetArrayField(TEXT("items"), Items);
           });
    TestFalse(TEXT("Null typed snapshot is rejected"), FableInventorySnapshot::Matches(P, nullptr));
    auto Legacy = InventoryFixture();
    TestFalse(TEXT("No typed snapshot is minted from unvalidated legacy data"),
              FableInventorySnapshot::Make(Legacy).IsValid());

    return true;
}
#endif
