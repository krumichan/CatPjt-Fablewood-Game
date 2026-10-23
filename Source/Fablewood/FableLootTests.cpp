#include "FableRPGTypes.h"
#include "FableSaveGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"

namespace
{
UFableSaveGame* MakeLootProfile(int32 Version)
{
    UFableSaveGame* P=NewObject<UFableSaveGame>();P->Version=Version;
    P->Gold=17;P->Ore=3;P->HealthPotions=2;P->ManaPotions=1;
    for(int32 I=0;I<3;++I){FFableItem Item;Item.Id=I+1;Item.HeroClass=EFableClass(I);P->Inventory.Add(Item);}
    FFableItem Armor;Armor.Id=4;Armor.Slot=EFableEquipmentSlot::Armor;Armor.Power=0;P->Inventory.Add(Armor);
    return P;
}
FFableItem RollFixture(int32 Id,EFableEquipmentSlot Slot,EFableEquipmentSet Set,EFableDifficulty Difficulty,FRandomStream& Random)
{
    FFableItem Item;Item.Id=Id;Item.Slot=Slot;Item.Rarity=Set==EFableEquipmentSet::Moonlight?3:2;Item.SetId=Set;
    FableRPG::RollItem(Item,Item.Rarity==3?(Slot==EFableEquipmentSlot::Weapon?58:16):(Slot==EFableEquipmentSlot::Weapon?48:12),Difficulty,Random);
    return Item;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableLootRollTest,"Fablewood.Loot.RollBoundsAndDifficulty",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableLootRollTest::RunTest(const FString& Parameters)
{
    int32 Finest[2]={0,0};int32 QualityCounts[5]={0,0,0,0,0};TSet<int32> Powers;TSet<FString> Variants;
    for(int32 Mode=0;Mode<2;++Mode)
    {
        FRandomStream Random(74191+Mode);
        for(int32 I=0;I<2000;++I)
        {
            const EFableDifficulty Difficulty=Mode==0?EFableDifficulty::Normal:EFableDifficulty::Hard;
            const EFableEquipmentSlot Slot=I%2==0?EFableEquipmentSlot::Weapon:EFableEquipmentSlot::Armor;
            const FFableItem Item=RollFixture(5,Slot,EFableEquipmentSet::RootGuardian,Difficulty,Random);
            if(!TestTrue(TEXT("Generated item obeys the complete authoritative roll schema"),FableRPG::IsValidItemRoll(Item)))return false;
            TestEqual(TEXT("Stored base power matches its recorded percentage"),Item.Power,FMath::RoundToInt(Item.NominalPower*Item.RollPercent/100.f));
            int32 Min=0,Max=0;FableRPG::GetItemPowerRange(Item,Min,Max);
            TestTrue(TEXT("Base power lies inside its displayed quality range"),Item.Power>=Min && Item.Power<=Max);
            TestEqual(TEXT("Quality determines option count"),Item.Affixes.Num(),FableRPG::QualityAffixCount(Item.Quality));
            TSet<uint8> Seen;
            for(const FFableItemAffix& A:Item.Affixes){FableRPG::AffixValueRange(A.Stat,Min,Max);TestTrue(TEXT("Option type is unique and value is bounded"),!Seen.Contains(uint8(A.Stat)) && A.Value>=Min && A.Value<=Max);Seen.Add(uint8(A.Stat));}
            ++QualityCounts[int32(Item.Quality)];if(Item.Quality==EFableItemQuality::Finest)++Finest[Mode];
            if(Slot==EFableEquipmentSlot::Weapon){Powers.Add(Item.Power);Variants.Add(FableRPG::ItemPersistentSignature(Item));}
            FFableItem Enhanced=Item;Enhanced.Upgrade=5;
            TestTrue(TEXT("Enhancing preserves original roll validity"),FableRPG::IsValidItemRoll(Enhanced));
            TestEqual(TEXT("Enhancing adds only the fixed slot increment"),Enhanced.GetEffectivePower(),Item.Power+(Slot==EFableEquipmentSlot::Weapon?20:10));
            TestEqual(TEXT("Enhancing never rerolls affixes"),Enhanced.Affixes.Num(),Item.Affixes.Num());
        }
    }
    for(int32 Q=1;Q<=4;++Q)TestTrue(TEXT("All four quality grades were actually generated"),QualityCounts[Q]>0);
    TestTrue(TEXT("Same equipment has many different basic powers"),Powers.Num()>15);
    TestTrue(TEXT("Same equipment has many different option combinations"),Variants.Num()>500);
    TestTrue(TEXT("Hard difficulty improves finest-quality frequency in a reproducible sample"),Finest[1]>Finest[0]+100);
    for(int32 Rarity=1;Rarity<=3;++Rarity)for(int32 SlotIndex=0;SlotIndex<2;++SlotIndex)
    {
        FRandomStream Seed(100+Rarity*10+SlotIndex);FFableItem Item;Item.Id=5;Item.Rarity=Rarity;Item.Slot=EFableEquipmentSlot(SlotIndex);
        const int32 Nominal=Rarity==1?(SlotIndex==0?30:6):Rarity==2?(SlotIndex==0?48:12):(SlotIndex==0?58:16);
        TestTrue(TEXT("Shop, region and quest nominal tables produce valid equipment"),FableRPG::RollItem(Item,Nominal,EFableDifficulty::Normal,Seed));
        if(Rarity==1 && SlotIndex==0)TestTrue(TEXT("Forest quest nominal32 is supported"),FableRPG::RollItem(Item,32,EFableDifficulty::Normal,Seed));
    }
    FRandomStream A(314159),B(314159);
    const FFableItem One=RollFixture(5,EFableEquipmentSlot::Weapon,EFableEquipmentSet::RootGuardian,EFableDifficulty::Hard,A);
    const FFableItem Two=RollFixture(5,EFableEquipmentSlot::Weapon,EFableEquipmentSet::RootGuardian,EFableDifficulty::Hard,B);
    TestEqual(TEXT("A seed reproduces the production generator exactly"),FableRPG::ItemPersistentSignature(One),FableRPG::ItemPersistentSignature(Two));
    FFableItem Invalid=One;const FString Before=FableRPG::ItemPersistentSignature(Invalid);
    TestFalse(TEXT("Unknown difficulty is rejected"),FableRPG::RollItem(Invalid,48,EFableDifficulty(255),A));
    TestEqual(TEXT("Rejected roll does not alter the input item"),FableRPG::ItemPersistentSignature(Invalid),Before);
    TestFalse(TEXT("Impossible nominal power is rejected"),FableRPG::RollItem(Invalid,99,EFableDifficulty::Normal,A));
    TestEqual(TEXT("Rejected nominal leaves the item intact"),FableRPG::ItemPersistentSignature(Invalid),Before);
    AddInfo(FString::Printf(TEXT("4,000 production rolls: finest normal=%d/2000 hard=%d/2000; base powers=%d option variants=%d"),Finest[0],Finest[1],Powers.Num(),Variants.Num()));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableLootMigrationTest,"Fablewood.Loot.LegacyMigrationAndCorruptRejection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableLootMigrationTest::RunTest(const FString& Parameters)
{
    for(int32 Version=0;Version<=4;++Version)
    {
        UFableSaveGame* P=MakeLootProfile(Version);TArray<FString> Before;
        FFableItem OldRare;OldRare.Id=5;OldRare.Rarity=2;OldRare.Power=48;OldRare.Upgrade=3;P->Inventory.Add(OldRare);P->NextItemId=6;
        if(Version==4){FFableItem OldMoon;OldMoon.Id=6;OldMoon.Rarity=3;OldMoon.Slot=EFableEquipmentSlot::Armor;OldMoon.Power=16;OldMoon.Upgrade=2;P->Inventory.Add(OldMoon);P->NextItemId=7;}
        for(const FFableItem& Item:P->Inventory)Before.Add(FableRPG::ItemPersistentSignature(Item));
        TestTrue(TEXT("Supported legacy schema migrates"),P->MigrateAndValidate());TestEqual(TEXT("Migration reaches v9"),P->Version,9);
        TestEqual(TEXT("Legacy gold is preserved"),P->Gold,17);TestEqual(TEXT("Legacy ore is preserved"),P->Ore,3);
        TestEqual(TEXT("Migration does not grant extra items"),P->Inventory.Num(),Before.Num());
        for(int32 I=0;I<Before.Num();++I)TestEqual(TEXT("Legacy basic performance, upgrade, IDs and no-roll state are preserved"),FableRPG::ItemPersistentSignature(P->Inventory[I]),Before[I]);
        TestEqual(TEXT("Potion quantities follow their original schema"),P->HealthPotions,Version==1?3:2);
        TestEqual(TEXT("Mana potions follow their original schema"),P->ManaPotions,Version==1?2:1);
    }
    UFableSaveGame* Future=MakeLootProfile(10);TestFalse(TEXT("Future schema stays unsupported"),Future->MigrateAndValidate());
    UFableSaveGame* Duplicate=MakeLootProfile(6);Duplicate->Inventory[1].Id=1;TestFalse(TEXT("Duplicate item IDs are rejected"),Duplicate->MigrateAndValidate());
    FRandomStream Random(42);UFableSaveGame* Corrupt=MakeLootProfile(6);Corrupt->Inventory.Add(RollFixture(5,EFableEquipmentSlot::Weapon,EFableEquipmentSet::RootGuardian,EFableDifficulty::Normal,Random));Corrupt->NextItemId=6;
    Corrupt->Inventory.Last().Power+=1;TestFalse(TEXT("Forged base performance is rejected"),Corrupt->MigrateAndValidate());
    Corrupt=MakeLootProfile(6);Corrupt->Inventory.Add(RollFixture(5,EFableEquipmentSlot::Weapon,EFableEquipmentSet::RootGuardian,EFableDifficulty::Normal,Random));Corrupt->NextItemId=6;
    Corrupt->Inventory.Last().Affixes[0].Value=100000;TestFalse(TEXT("Forged option values are rejected"),Corrupt->MigrateAndValidate());
    Corrupt=MakeLootProfile(6);Corrupt->Inventory.Add(RollFixture(5,EFableEquipmentSlot::Weapon,EFableEquipmentSet::RootGuardian,EFableDifficulty::Normal,Random));Corrupt->NextItemId=6;
    Corrupt->Inventory.Last().Quality=EFableItemQuality(255);TestFalse(TEXT("Unknown quality is rejected"),Corrupt->MigrateAndValidate());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableLootSaveTest,"Fablewood.Loot.RandomEquipmentSaveRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFableLootSaveTest::RunTest(const FString& Parameters)
{
    FRandomStream Random(8675309);UFableSaveGame* P=MakeLootProfile(6);
    P->Inventory.Add(RollFixture(5,EFableEquipmentSlot::Weapon,EFableEquipmentSet::RootGuardian,EFableDifficulty::Hard,Random));
    P->Inventory.Add(RollFixture(6,EFableEquipmentSlot::Armor,EFableEquipmentSet::RootGuardian,EFableDifficulty::Normal,Random));
    P->Inventory.Last().Upgrade=2;P->NextItemId=7;P->EquippedWeaponId=5;P->EquippedArmorId=6;
    TestTrue(TEXT("Random-equipped profile validates before saving"),P->IsValidProfile());
    TArray<uint8> Bytes;if(!TestTrue(TEXT("The real Unreal serializer writes the fixture"),UGameplayStatics::SaveGameToMemory(P,Bytes)))return false;
    UFableSaveGame* Restored=Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if(!TestNotNull(TEXT("The real Unreal serializer restores the fixture"),Restored))return false;
    TestTrue(TEXT("Restored random profile validates"),Restored->MigrateAndValidate());
    if(!TestEqual(TEXT("Item count survives serialization"),Restored->Inventory.Num(),P->Inventory.Num()))return false;
    for(int32 I=0;I<P->Inventory.Num();++I)TestEqual(TEXT("Every original roll, affix and set field survives serialization"),FableRPG::ItemPersistentSignature(Restored->Inventory[I]),FableRPG::ItemPersistentSignature(P->Inventory[I]));
    TestEqual(TEXT("Equipped weapon is retained"),Restored->EquippedWeaponId,5);TestEqual(TEXT("Equipped armor is retained"),Restored->EquippedArmorId,6);
    return true;
}
#endif
