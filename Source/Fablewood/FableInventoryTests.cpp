#include "FableInventorySnapshot.h"
#include "FableSaveGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
UFableSaveGame* InventoryFixture(int32 Version=8)
{
    auto P=NewObject<UFableSaveGame>();P->Version=Version;
    P->Gold=137;P->Ore=9;P->HealthPotions=2;P->ManaPotions=1;
    for(int32 I=0;I<3;++I){FFableItem Item;Item.Id=I+1;Item.HeroClass=EFableClass(I);P->Inventory.Add(Item);}
    FFableItem Armor;Armor.Id=4;Armor.Slot=EFableEquipmentSlot::Armor;Armor.Power=0;P->Inventory.Add(Armor);
    FFableItem Weapon;Weapon.Id=5;Weapon.Rarity=2;Weapon.Power=48;Weapon.Upgrade=3;
    if(Version>=5)
    {
        Weapon.SetId=EFableEquipmentSet::RootGuardian;FRandomStream Random(117);
        FableRPG::RollItem(Weapon,48,EFableDifficulty::Hard,Random);
    }
    if(Version>=6)Weapon.Durability=43;
    P->Inventory.Add(Weapon);P->NextItemId=6;P->EquippedWeaponId=5;
    if(Version>=6)
    {
        FFableItem Stored;Stored.Id=6;Stored.Rarity=4;Stored.Slot=EFableEquipmentSlot::Armor;
        FRandomStream Random(227);FableRPG::RollItem(Stored,22,EFableDifficulty::Normal,Random);
        Stored.Durability=0;Stored.Upgrade=2;P->StashItems.Add(Stored);P->NextItemId=7;
        P->Essence=17;P->RootCores=3;P->MoonCrystals=4;
    }
    return P;
}
TArray<FGuid> Identities(const UFableSaveGame* P)
{
    TArray<FGuid> Result;
    for(const FFableItem& I:P->Inventory)Result.Add(I.InstanceId);
    for(const FFableItem& I:P->StashItems)Result.Add(I.InstanceId);
    return Result;
}
TSharedPtr<FJsonObject> ReorderObjects(const TSharedPtr<FJsonObject>& Source);
TSharedPtr<FJsonValue> ReorderValue(const TSharedPtr<FJsonValue>& Source)
{
    if(Source->Type==EJson::Object)return MakeShared<FJsonValueObject>(ReorderObjects(Source->AsObject()));
    if(Source->Type==EJson::Array)
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        for(const auto& V:Source->AsArray())Values.Add(ReorderValue(V));
        return MakeShared<FJsonValueArray>(Values);
    }
    return Source;
}
TSharedPtr<FJsonObject> ReorderObjects(const TSharedPtr<FJsonObject>& Source)
{
    auto Result=MakeShared<FJsonObject>();TArray<FJsonObject::FStringType> Keys;Source->Values.GetKeys(Keys);
    // Reverse the actual insertion order; UE 5.8 stores keys as FSharedString.
    for(int32 I=Keys.Num()-1;I>=0;--I)
        Result->SetField(FStringView(*Keys[I],Keys[I].Len()),ReorderValue(Source->Values[Keys[I]]));
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableInventoryMigrationTest,"Fablewood.Inventory.V9IdentityMigrationAndStrictRejection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableInventoryMigrationTest::RunTest(const FString& Parameters)
{
    for(int32 Version=0;Version<=8;++Version)
    {
        auto P=InventoryFixture(Version);TArray<FString> Before;
        for(const auto& I:P->Inventory)Before.Add(FableRPG::GearIdentitySignature(I));
        const FGuid Existing(0x12345678,0xABCD4321,0x87654321,0xABCDEF01);
        P->Inventory[0].InstanceId=Existing;
        if(!TestTrue(FString::Printf(TEXT("Historical schema %d migrates"),Version),P->MigrateAndValidate()))continue;
        TestEqual(TEXT("Migration reaches current schema"),P->Version,9);
        TestTrue(TEXT("Existing identity is never reassigned"),P->Inventory[0].InstanceId==Existing);
        TestEqual(TEXT("No equipment is added"),P->Inventory.Num(),Before.Num());
        for(int32 I=0;I<Before.Num();++I)TestEqual(TEXT("Local IDs and all legacy performance are retained"),FableRPG::GearIdentitySignature(P->Inventory[I]),Before[I]);
        const auto Ids=Identities(P);TSet<FGuid> Seen;
        for(const auto& Id:Ids){TestTrue(TEXT("All migrated identities are nonempty and globally unique within the profile"),Id.IsValid()&&!Seen.Contains(Id));Seen.Add(Id);}
        TestTrue(TEXT("Migration is idempotent"),P->MigrateAndValidate()&&Identities(P)==Ids);
        TestTrue(TEXT("Original currency is retained"),P->Gold==137&&P->Ore==9);
        TestTrue(TEXT("Original equipment references are retained"),P->EquippedWeaponId==5&&P->EquippedArmorId==4);
        if(Version>=6)TestTrue(TEXT("Migration preserves broken stash, partial durability and materials"),P->StashItems.Num()==1&&P->StashItems[0].Durability==0&&P->Inventory[4].Durability==43&&P->Essence==17&&P->RootCores==3&&P->MoonCrystals==4);
    }
    auto Current=InventoryFixture();TestTrue(TEXT("Create current fixture"),Current->MigrateAndValidate());
    Current->Inventory[0].InstanceId.Invalidate();
    TestFalse(TEXT("Current missing UUID is rejected, not regenerated"),Current->MigrateAndValidate());
    TestFalse(TEXT("Rejected current UUID remains empty"),Current->Inventory[0].InstanceId.IsValid());
    Current=InventoryFixture();Current->MigrateAndValidate();Current->StashItems[0].InstanceId=Current->Inventory[4].InstanceId;
    TestFalse(TEXT("UUID duplicates across bag and stash are rejected"),Current->MigrateAndValidate());
    auto Legacy=InventoryFixture();Legacy->Inventory[0].InstanceId=Legacy->Inventory[1].InstanceId=FGuid::NewGuid();
    TestFalse(TEXT("An existing duplicate legacy UUID is not silently replaced"),Legacy->MigrateAndValidate());
    TestFalse(TEXT("Rejected migration does not fill other empty identities"),Legacy->Inventory[2].InstanceId.IsValid());
    Legacy=InventoryFixture();Legacy->Inventory[0].Id=0;
    TestFalse(TEXT("Legacy integer IDs are not repaired or renumbered"),Legacy->MigrateAndValidate());
    auto Future=InventoryFixture(10);TestFalse(TEXT("Unsupported schema ten is rejected"),Future->MigrateAndValidate());
    TestEqual(TEXT("Unsupported schema is unchanged"),Future->Version,10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableInventorySaveTest,"Fablewood.Inventory.UUIDAndTypedSnapshotMemoryRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableInventorySaveTest::RunTest(const FString& Parameters)
{
    auto P=InventoryFixture();if(!TestTrue(TEXT("Migrate source fixture"),P->MigrateAndValidate()))return false;
    const auto Before=FableInventorySnapshot::Make(P);const auto Ids=Identities(P);
    if(!TestTrue(TEXT("A complete typed snapshot can be built"),Before.IsValid()))return false;
    TArray<uint8> Bytes;if(!TestTrue(TEXT("Unreal saves UUID and equipment to memory"),UGameplayStatics::SaveGameToMemory(P,Bytes)))return false;
    auto Restored=Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if(!TestNotNull(TEXT("Unreal restores the memory profile"),Restored))return false;
    TestTrue(TEXT("Stored schema validates and UUIDs are byte-for-byte stable"),Restored->Version==9&&Restored->MigrateAndValidate()&&Identities(Restored)==Ids);
    TestTrue(TEXT("Typed all-field snapshot, array order and equipment references survive"),FableInventorySnapshot::Matches(Restored,Before));
    TestTrue(TEXT("Wallet and materials survive"),Restored->Gold==137&&Restored->Ore==9&&Restored->Essence==17&&Restored->RootCores==3&&Restored->MoonCrystals==4);
    TestTrue(TEXT("Legendary broken stash and partial equipped durability survive"),Restored->StashItems[0].Rarity==4&&Restored->StashItems[0].MaxDurability==150&&Restored->StashItems[0].Durability==0&&Restored->Inventory[4].Durability==43);
    // A move uses the original struct, just as DepositItem/WithdrawItem do; no creation/migration runs here.
    const auto Stored=Restored->StashItems[0];Restored->StashItems.Reset();Restored->Inventory.Add(Stored);
    TestTrue(TEXT("Withdrawal changes location while retaining identity and metadata"),Restored->IsValidProfile()&&Restored->Inventory.Last().InstanceId==Stored.InstanceId&&FableRPG::ItemPersistentSignature(Restored->Inventory.Last())==FableRPG::ItemPersistentSignature(Stored));
    Restored->StashItems.Add(Restored->Inventory.Last());Restored->Inventory.Pop();
    TestTrue(TEXT("Deposit round trip restores the exact typed snapshot"),FableInventorySnapshot::Matches(Restored,Before));
    const auto Frozen=FableInventorySnapshot::Make(Restored);
    Restored->Gold-=3;--Restored->Inventory[4].Durability;
    TestFalse(TEXT("A queued snapshot is an independent frozen copy"),FableInventorySnapshot::Matches(Restored,Frozen));
    TestTrue(TEXT("Prior queued body still matches the original profile"),FableInventorySnapshot::Matches(P,Frozen));
    AddInfo(TEXT("Memory-only serialization fixtures; no disk saves, HTTP, services or gameplay mutation were executed."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableInventoryWireTest,"Fablewood.Inventory.TypedWireSemanticEqualityAndFailClosed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableInventoryWireTest::RunTest(const FString& Parameters)
{
    auto P=InventoryFixture();if(!TestTrue(TEXT("Prepare valid v9 fixture"),P->MigrateAndValidate()))return false;
    const auto Valid=FableInventorySnapshot::Make(P);
    TestTrue(TEXT("Property order is immaterial at every nested object"),FableInventorySnapshot::Matches(P,ReorderObjects(Valid)));
    const auto Reject=[&](const TCHAR* Name,TFunctionRef<void(const TSharedPtr<FJsonObject>&)> Mutate)
    {const auto Changed=ReorderObjects(Valid);Mutate(Changed);TestFalse(Name,FableInventorySnapshot::Matches(P,Changed));};
    for(const TCHAR* Field:{TEXT("version"),TEXT("heroClass"),TEXT("nextLocalId")})
        Reject(TEXT("Every header scalar participates in equality"),[Field](const auto& J){J->SetNumberField(Field,J->GetNumberField(Field)+1);});
    for(const TCHAR* Field:{TEXT("localId"),TEXT("location"),TEXT("position"),TEXT("slot"),TEXT("heroClass"),TEXT("rarity"),TEXT("power"),TEXT("upgrade"),TEXT("rollVersion"),TEXT("quality"),TEXT("nominalPower"),TEXT("rollPercent"),TEXT("setId"),TEXT("durabilityVersion"),TEXT("durability"),TEXT("maxDurability")})
        Reject(TEXT("Every item scalar participates in equality"),[Field](const auto& J){auto Item=J->GetArrayField(TEXT("items"))[4]->AsObject();Item->SetNumberField(Field,Item->GetNumberField(Field)+1);});
    Reject(TEXT("Missing typed field is rejected"),[](const auto& J){J->RemoveField(TEXT("nextLocalId"));});
    Reject(TEXT("Unknown typed field is rejected"),[](const auto& J){J->SetNumberField(TEXT("unexpected"),1);});
    Reject(TEXT("Wrong JSON field type is rejected"),[](const auto& J){J->SetStringField(TEXT("nextLocalId"),TEXT("7"));});
    Reject(TEXT("A foreign UUID cannot replace an item"),[](const auto& J){J->GetArrayField(TEXT("items"))[0]->AsObject()->SetStringField(TEXT("instanceId"),FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));});
    Reject(TEXT("Equipped UUID must match the profile's bag item"),[](const auto& J){J->SetStringField(TEXT("equippedWeaponInstanceId"),J->GetStringField(TEXT("equippedArmorInstanceId")));});
    Reject(TEXT("Bag and stash array order is significant"),[](const auto& J){auto Items=J->GetArrayField(TEXT("items"));Items.Swap(0,1);J->SetArrayField(TEXT("items"),Items);});
    Reject(TEXT("Affix values are compared"),[](const auto& J){auto A=J->GetArrayField(TEXT("items"))[4]->AsObject()->GetArrayField(TEXT("affixes"))[0]->AsObject();A->SetNumberField(TEXT("value"),A->GetNumberField(TEXT("value"))+1);});
    Reject(TEXT("Affix stat is compared"),[](const auto& J){auto A=J->GetArrayField(TEXT("items"))[4]->AsObject()->GetArrayField(TEXT("affixes"))[0]->AsObject();A->SetNumberField(TEXT("stat"),99);});
    Reject(TEXT("Missing item is rejected"),[](const auto& J){auto Items=J->GetArrayField(TEXT("items"));Items.Pop();J->SetArrayField(TEXT("items"),Items);});
    Reject(TEXT("Null item is rejected without dereference"),[](const auto& J){auto Items=J->GetArrayField(TEXT("items"));Items[0]=MakeShared<FJsonValueNull>();J->SetArrayField(TEXT("items"),Items);});
    TestFalse(TEXT("Null typed snapshot is rejected"),FableInventorySnapshot::Matches(P,nullptr));
    auto Legacy=InventoryFixture();TestFalse(TEXT("No typed snapshot is minted from unvalidated legacy data"),FableInventorySnapshot::Make(Legacy).IsValid());
    return true;
}
#endif
