#include "FableInventorySnapshot.h"
#include "FableSaveGame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    /** 식별자를 저장 계약의 정규 문자열 형식으로 변환한다. */
    FString Identity(const FGuid& Id)
    {
        return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
    }

    /** JSON 객체·배열·기본값을 재귀적으로 비교한다. */
    bool SameValue(const TSharedPtr<FJsonValue>& Expected, const TSharedPtr<FJsonValue>& Actual)
    {
        if (!Expected.IsValid() || !Actual.IsValid() || Expected->Type != Actual->Type)
        {
            return false;
        }
        switch (Expected->Type)
        {
        // JSON 계약과 검증 기록 처리
        case EJson::Number:
            return Expected->AsNumber() == Actual->AsNumber();
        case EJson::String:
            return Expected->AsString() == Actual->AsString();
        case EJson::Array:
        {
            const auto& A = Expected->AsArray();
            const auto& B = Actual->AsArray();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (A.Num() != B.Num())
            {
                return false;
            }

            // 같은 규칙을 적용할 대상을 순회한다.
            for (int32 Index = 0; Index < A.Num(); ++Index)
            {
                if (!SameValue(A[Index], B[Index]))
                {
                    return false;
                }
            }

            return true;
        }
        case EJson::Object:
        {
            const auto& A = Expected->AsObject();
            const auto& B = Actual->AsObject();

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!A.IsValid() || !B.IsValid() || A->Values.Num() != B->Values.Num())
            {
                return false;
            }

            // 같은 규칙을 적용할 대상을 순회한다.
            for (const auto& Field : A->Values)
            {
                const auto* Other = B->Values.Find(Field.Key);
                if (!Other || !SameValue(Field.Value, *Other))
                {
                    return false;
                }
            }

            return true;
        }
        default:
            return false; // This wire schema has no booleans, nulls or optional fields.
        }
    }
}

/** 아이템의 소유·품질·옵션·내구도를 전송용 JSON으로 만든다. */
TSharedRef<FJsonObject> FableInventorySnapshot::MakeItem(const FFableItem& Item, int32 Location, int32 Position)
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("instanceId"), Identity(Item.InstanceId));
    J->SetNumberField(TEXT("localId"), Item.Id);
    J->SetNumberField(TEXT("location"), Location);
    J->SetNumberField(TEXT("position"), Position);
    J->SetNumberField(TEXT("slot"), int32(Item.Slot));
    J->SetNumberField(TEXT("heroClass"), int32(Item.HeroClass));
    J->SetNumberField(TEXT("rarity"), Item.Rarity);
    J->SetNumberField(TEXT("power"), Item.Power);
    J->SetNumberField(TEXT("upgrade"), Item.Upgrade);
    J->SetNumberField(TEXT("rollVersion"), Item.RollVersion);
    J->SetNumberField(TEXT("quality"), int32(Item.Quality));
    J->SetNumberField(TEXT("nominalPower"), Item.NominalPower);
    J->SetNumberField(TEXT("rollPercent"), Item.RollPercent);
    J->SetNumberField(TEXT("setId"), int32(Item.SetId));
    J->SetNumberField(TEXT("durabilityVersion"), Item.DurabilityVersion);
    J->SetNumberField(TEXT("durability"), Item.Durability);
    J->SetNumberField(TEXT("maxDurability"), Item.MaxDurability);
    TArray<TSharedPtr<FJsonValue>> Affixes;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItemAffix& Affix : Item.Affixes)
    {
        auto A = MakeShared<FJsonObject>();
        A->SetNumberField(TEXT("stat"), int32(Affix.Stat));
        A->SetNumberField(TEXT("value"), Affix.Value);
        Affixes.Add(MakeShared<FJsonValueObject>(A));
    }

    J->SetArrayField(TEXT("affixes"), Affixes);

    return J;
}

/** 저장 프로필을 백엔드 계약에 맞는 인벤토리 JSON으로 만든다. */
TSharedPtr<FJsonObject> FableInventorySnapshot::Make(const UFableSaveGame* P)
{
    // 프로필과 저장 스냅숏 조건 검사
    if (!P || P->Version != 9 || !P->IsValidProfile())
    {
        return nullptr;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
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
            {
                Snapshot->SetStringField(TEXT("equippedWeaponInstanceId"), Identity(Item.InstanceId));
            }
            if (Location == 0 && Item.Id == P->EquippedArmorId)
            {
                Snapshot->SetStringField(TEXT("equippedArmorInstanceId"), Identity(Item.InstanceId));
            }
        }
    };
    Append(P->Inventory, 0);
    Append(P->StashItems, 1);
    Snapshot->SetArrayField(TEXT("items"), Items);

    return Snapshot;
}

/** 전달된 저장 계약이 현재 프로필의 투영 결과와 같은지 검사한다. */
bool FableInventorySnapshot::Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Snapshot)
{
    const auto Expected = Make(Profile);
    return Expected.IsValid() && Snapshot.IsValid() &&
           SameValue(MakeShared<FJsonValueObject>(Expected), MakeShared<FJsonValueObject>(Snapshot));
}
