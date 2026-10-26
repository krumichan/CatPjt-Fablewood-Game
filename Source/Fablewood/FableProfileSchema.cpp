#include "FableProfileSchema.h"
#include "UObject/Package.h"
#include "FableSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "FableTownCatalog.h"
#include "FableInventorySnapshot.h"
#include "FableCharacterSummary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"

/** UUID 도입 경계와 최신 저장 버전을 혼동하지 않는다. */
bool FableProfileSchema::CanProjectOriginalMirror(int32 Version)
{
    return Version == ItemIdentityVersion || Version == CurrentVersion;
}

/** 이전 형식은 전용 이전 경로에서만 읽는다. */
bool FableProfileSchema::IsWritableCurrentVersion(int32 Version)
{
    return Version == CurrentVersion;
}

/** 알려진 Entry 참조의 형식 검사는 실제 지형 채택과 독립적이다. */
bool FableProfileSchema::IsSafeAnchor(const FFableTownAnchorRef& Anchor)
{
    if (!Anchor.IsValid() || Anchor.AnchorId != FName(TEXT("Entry")) || Anchor.CatalogVersion != 1)
    {
        return false;
    }

    FTransform DeclaredSlot;
    return GetDefault<UFableTownCatalog>()->ResolveAnchor(Anchor, 0, DeclaredSlot, false);
}

/** Unit3에서는 Legacy 정책을 사용한다. 채택 정책 변경은 별도 릴리스 작업이다. */
FFableTownAnchorRef FableProfileSchema::BootstrapAnchor(EFableTownBootstrapPolicy Policy)
{
    FFableTownAnchorRef Anchor;
    Anchor.TownId =
        Policy == EFableTownBootstrapPolicy::AdoptedSunbrookV1 ? EFableTownId::Sunbrook : EFableTownId::LegacyVillage;
    Anchor.AnchorId = FName(TEXT("Entry"));
    Anchor.CatalogVersion = 1;
    return Anchor;
}

/** 원본의 hash·mirror 확인 이후 호출하며, 실패한 후보는 원본을 덮어쓰지 않는다. */
UFableSaveGame* FableProfileSchema::MakeCandidate(const UFableSaveGame* Original, EFableTownBootstrapPolicy Policy)
{
    if (!Original)
    {
        return nullptr;
    }

    UFableSaveGame* Candidate = DuplicateObject<UFableSaveGame>(Original, GetTransientPackage());
    return Candidate && Candidate->MigrateAndValidate(Policy) ? Candidate : nullptr;
}

/** 原本versionで照合し、移行candidateの値を原本mirrorの代わりに使わない。 */
bool FableProfileSchema::OriginalMirrorsMatch(const UFableSaveGame* Original, int32 InventoryVersion,
                                              const TSharedPtr<FJsonObject>& Wallet,
                                              const TSharedPtr<FJsonObject>& Inventory,
                                              const TSharedPtr<FJsonValue>& Summary, bool& bOutRequiresSummarySeed)
{
    bOutRequiresSummarySeed = false;
    if (!Original || !Wallet.IsValid() || Wallet->Values.Num() != 7 || !Summary.IsValid() ||
        (InventoryVersion != 0 && InventoryVersion != 1) ||
        (InventoryVersion == 1 ? !CanProjectOriginalMirror(Original->Version)
                               : Original->Version >= ItemIdentityVersion))
    {
        return false;
    }

    auto* Validation = DuplicateObject<UFableSaveGame>(Original, GetTransientPackage());
    Validation->NormalizeLoadedVersion();
    if (!Validation->IsValidProfile() ||
        (InventoryVersion == 1 && !FableInventorySnapshot::Matches(Original, Inventory)) ||
        (InventoryVersion == 0 && Inventory.IsValid()))
    {
        return false;
    }

    if (Summary->Type == EJson::Object)
    {
        if (InventoryVersion != 1 || !FableCharacterSummary::Matches(Original, Summary->AsObject()))
        {
            return false;
        }
    }
    else if (Summary->Type != EJson::Null || Original->Version == CurrentVersion)
    {
        return false;
    }

    const TMap<FString, int32> Expected = {{TEXT("gold"), Original->Gold},
                                           {TEXT("ore"), Original->Ore},
                                           {TEXT("essence"), Original->Essence},
                                           {TEXT("rootCores"), Original->RootCores},
                                           {TEXT("moonCrystals"), Original->MoonCrystals},
                                           {TEXT("healthPotions"), Original->HealthPotions},
                                           {TEXT("manaPotions"), Original->ManaPotions}};
    for (const auto& Field : Expected)
    {
        // JSON内部のshared-string map型へ依存せず公開field APIで厳密な数値を読む。
        const auto Value = Wallet->TryGetField(Field.Key);
        if (!Value.IsValid() || Value->Type != EJson::Number || Value->AsNumber() != Field.Value)
        {
            return false;
        }
    }

    bOutRequiresSummarySeed = Original->Version == ItemIdentityVersion && Summary->Type == EJson::Null;
    return true;
}

/** 숫자 문자열도 정확히 하나의 canonical 옵션이어야 한다. */
bool FableProfileSchema::HasCompatibleOptions(const FString& Options)
{
    if (Options.Len() > 4096)
    {
        return false;
    }

    const TMap<FString, FString> Expected = {{TEXT("FableProfileSchema"), TEXT("10")},
                                             {TEXT("FableTownCatalog"), TEXT("1")},
                                             {TEXT("FableTownProtocol"), TEXT("1")}};
    TMap<FString, int32> Counts;
    TArray<FString> Parts;
    Options.ParseIntoArray(Parts, TEXT("?"), true);
    for (const FString& Part : Parts)
    {
        FString Key, Value;
        if (!Part.Split(TEXT("="), &Key, &Value))
        {
            for (const auto& Field : Expected)
            {
                if (Part.Equals(Field.Key, ESearchCase::IgnoreCase))
                {
                    return false;
                }
            }
            continue;
        }
        for (const auto& Field : Expected)
        {
            if (Key.Equals(Field.Key, ESearchCase::IgnoreCase))
            {
                if (Key != Field.Key || Value != Field.Value || ++Counts.FindOrAdd(Field.Key) != 1)
                {
                    return false;
                }
            }
        }
    }

    return Counts.Num() == Expected.Num();
}

/** 보안 권한을 부여하지 않는 클라이언트 호환성 표식이다. */
FString FableProfileSchema::CompatibilityOptions()
{
    return TEXT("?FableProfileSchema=10?FableTownCatalog=1?FableTownProtocol=1");
}

/** raw原本は保持し、読み込んだV9と候補のV9投影を同じ現行serializerで照合する。 */
bool FableProfileSchema::PreservesOriginalNineBytes(const UFableSaveGame* Candidate, const TArray<uint8>& OriginalBytes)
{
    if (!Candidate || Candidate->Version != CurrentVersion || !Candidate->IsValidProfile() || OriginalBytes.IsEmpty() ||
        OriginalBytes.Num() > 262144)
    {
        return false;
    }
    auto* Original = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
    if (!Original || Original->Version != ItemIdentityVersion || !Original->IsValidProfile())
    {
        return false;
    }

    // 原本や送信candidateは変更しない。検証専用の別copyだけをV9へ投影する。
    auto* Historical = DuplicateObject<UFableSaveGame>(Candidate, GetTransientPackage());
    Historical->Version = ItemIdentityVersion;
    Historical->LastSafeTownId = EFableTownId::Invalid;
    Historical->LastSafeAnchorId = NAME_None;
    Historical->TownCatalogVersion = 0;
    // UE headerのengine/custom-version metadataではなく、現行形式で全既存保存フィールドを比較する。
    // 認証したraw bytes/hashは呼出側に残り、この比較で置換・再計算しない。
    TArray<uint8> OriginalCurrentBytes, HistoricalBytes;
    return UGameplayStatics::SaveGameToMemory(Original, OriginalCurrentBytes) &&
           UGameplayStatics::SaveGameToMemory(Historical, HistoricalBytes) && HistoricalBytes == OriginalCurrentBytes;
}
