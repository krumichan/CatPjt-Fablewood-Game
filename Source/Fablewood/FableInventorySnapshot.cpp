#include "FableInventorySnapshot.h"
#include "FableSaveGame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
FString Identity(const FGuid& Id) { return Id.ToString(EGuidFormats::DigitsWithHyphensLower); }

bool SameValue(const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual)
{
    if (!Expected.IsValid() || !Actual.IsValid() || Expected->Type != Actual->Type) return false;
    switch (Expected->Type)
    {
    case EJson::Number: return Expected->AsNumber() == Actual->AsNumber();
    case EJson::String: return Expected->AsString() == Actual->AsString();
    case EJson::Array:
    {
        const auto& A = Expected->AsArray(); const auto& B = Actual->AsArray();
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index) if (!SameValue(A[Index], B[Index])) return false;
        return true;
    }
    case EJson::Object:
    {
        const auto& A = Expected->AsObject(); const auto& B = Actual->AsObject();
        if (!A.IsValid() || !B.IsValid() || A->Values.Num() != B->Values.Num()) return false;
        for (const auto& Field : A->Values)
        {
            const auto* Other = B->Values.Find(Field.Key);
            if (!Other || !SameValue(Field.Value, *Other)) return false;
        }
        return true;
    }
    default: return false; // This wire schema has no booleans, nulls or optional fields.
    }
}
}

TSharedRef<FJsonObject> FableInventorySnapshot::MakeItem(const FFableItem& Item, int32 Location, int32 Position)
{
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("instanceId"), Identity(Item.InstanceId));
    J->SetNumberField(TEXT("localId"), Item.Id);
    J->SetNumberField(TEXT("location"), Location); J->SetNumberField(TEXT("position"), Position);
    J->SetNumberField(TEXT("slot"), int32(Item.Slot)); J->SetNumberField(TEXT("heroClass"), int32(Item.HeroClass));
    J->SetNumberField(TEXT("rarity"), Item.Rarity); J->SetNumberField(TEXT("power"), Item.Power);
    J->SetNumberField(TEXT("upgrade"), Item.Upgrade); J->SetNumberField(TEXT("rollVersion"), Item.RollVersion);
    J->SetNumberField(TEXT("quality"), int32(Item.Quality)); J->SetNumberField(TEXT("nominalPower"), Item.NominalPower);
    J->SetNumberField(TEXT("rollPercent"), Item.RollPercent); J->SetNumberField(TEXT("setId"), int32(Item.SetId));
    J->SetNumberField(TEXT("durabilityVersion"), Item.DurabilityVersion);
    J->SetNumberField(TEXT("durability"), Item.Durability); J->SetNumberField(TEXT("maxDurability"), Item.MaxDurability);
    TArray<TSharedPtr<FJsonValue>> Affixes;
    for (const FFableItemAffix& Affix : Item.Affixes)
    {
        auto A = MakeShared<FJsonObject>(); A->SetNumberField(TEXT("stat"), int32(Affix.Stat));
        A->SetNumberField(TEXT("value"), Affix.Value); Affixes.Add(MakeShared<FJsonValueObject>(A));
    }
    J->SetArrayField(TEXT("affixes"), Affixes);
    return J;
}

TSharedPtr<FJsonObject> FableInventorySnapshot::Make(const UFableSaveGame* P)
{
    if (!P || P->Version != 9 || !P->IsValidProfile()) return nullptr;
    auto Snapshot = MakeShared<FJsonObject>();
    Snapshot->SetNumberField(TEXT("version"), 1);
    Snapshot->SetNumberField(TEXT("heroClass"), int32(P->HeroClass));
    Snapshot->SetNumberField(TEXT("nextLocalId"), P->NextItemId);
    TArray<TSharedPtr<FJsonValue>> Items;
    const auto Append = [&Items, &Snapshot, P](const TArray<FFableItem>& Source, int32 Location)
    {
        for (int32 Position = 0; Position < Source.Num(); ++Position)
        {
            const FFableItem& Item = Source[Position];
            Items.Add(MakeShared<FJsonValueObject>(MakeItem(Item, Location, Position)));
            if (Location == 0 && Item.Id == P->EquippedWeaponId)
                Snapshot->SetStringField(TEXT("equippedWeaponInstanceId"), Identity(Item.InstanceId));
            if (Location == 0 && Item.Id == P->EquippedArmorId)
                Snapshot->SetStringField(TEXT("equippedArmorInstanceId"), Identity(Item.InstanceId));
        }
    };
    Append(P->Inventory, 0); Append(P->StashItems, 1);
    Snapshot->SetArrayField(TEXT("items"), Items);
    return Snapshot;
}

bool FableInventorySnapshot::Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Snapshot)
{
    const auto Expected = Make(Profile);
    return Expected.IsValid() && Snapshot.IsValid()
        && SameValue(MakeShared<FJsonValueObject>(Expected), MakeShared<FJsonValueObject>(Snapshot));
}
