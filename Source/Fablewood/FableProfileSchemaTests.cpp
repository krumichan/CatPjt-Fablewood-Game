#include "FableProfileSchema.h"
#include "FableProfileStorage.h"
#include "FableSaveGame.h"
#include "FableInventorySnapshot.h"
#include "FableCharacterSummary.h"
#include "FableTownCatalog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/ObjectVersion.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    /** raw V9의 재화·UUID·bag/stash 순서·파손 장비·UTC를 갖는 비단순 fixture. */
    UFableSaveGame* OriginalNine()
    {
        auto* P = NewObject<UFableSaveGame>();
        P->Version = 8;
        P->Level = 4;
        P->Experience = 39;
        P->Gold = 137;
        P->Ore = 9;
        P->Essence = 17;
        P->RootCores = 3;
        P->MoonCrystals = 4;
        P->HealthPotions = 2;
        P->ManaPotions = 1;
        P->PotionCooldownEndUtc = 1800000001;

        for (int32 I = 0; I < 3; ++I)
        {
            FFableItem Item;
            Item.Id = I + 1;
            Item.HeroClass = EFableClass(I);
            P->Inventory.Add(Item);
        }
        FFableItem Armor;
        Armor.Id = 4;
        Armor.Slot = EFableEquipmentSlot::Armor;
        Armor.Power = 0;
        P->Inventory.Add(Armor);

        FFableItem Weapon;
        Weapon.Id = 5;
        Weapon.Rarity = 2;
        Weapon.Upgrade = 3;
        FRandomStream WeaponRandom(117);
        FableRPG::RollItem(Weapon, 48, EFableDifficulty::Hard, WeaponRandom);
        Weapon.Durability = 43;
        P->Inventory.Add(Weapon);
        P->EquippedWeaponId = 5;

        FFableItem Stored;
        Stored.Id = 6;
        Stored.Rarity = 4;
        Stored.Slot = EFableEquipmentSlot::Armor;
        FRandomStream ArmorRandom(227);
        FableRPG::RollItem(Stored, 22, EFableDifficulty::Normal, ArmorRandom);
        Stored.Durability = 0;
        P->StashItems.Add(Stored);
        P->NextItemId = 7;

        if (!P->MigrateAndValidate())
        {
            return nullptr;
        }
        // 진짜 V9 형식의 양성 입력을 만든다. 이 메모리 fixture는 실플레이 이전 증거가 아니다.
        P->Version = FableProfileSchema::ItemIdentityVersion;
        P->LastSafeTownId = EFableTownId::Invalid;
        P->LastSafeAnchorId = NAME_None;
        P->TownCatalogVersion = 0;
        return P;
    }

    /** 새 세 필드와 Version을 제외한 모든 reflected 저장 필드의 bytes를 비교한다. */
    TArray<uint8> HistoricalBytes(const UFableSaveGame* P)
    {
        auto* Copy = DuplicateObject<UFableSaveGame>(P, GetTransientPackage());
        Copy->Version = FableProfileSchema::ItemIdentityVersion;
        Copy->LastSafeTownId = EFableTownId::Invalid;
        Copy->LastSafeAnchorId = NAME_None;
        Copy->TownCatalogVersion = 0;
        TArray<uint8> Bytes;
        UGameplayStatics::SaveGameToMemory(Copy, Bytes);
        return Bytes;
    }

    /** UE5.8のFSaveGameHeaderの順序で読み、engine changelist metadataだけを同じ長さで変更する。 */
    bool WithDifferentEngineHeader(const TArray<uint8>& OriginalRaw, TArray<uint8>& OutBytes)
    {
        FMemoryReader Reader(OriginalRaw, true);
        int32 FileTypeTag = 0, SaveGameFileVersion = 0;
        Reader << FileTypeTag;
        Reader << SaveGameFileVersion;
        // GameplayStatics.cpp: tag、header version3、FPackageFileVersion、FEngineVersionの順。
        if (Reader.IsError() || FileTypeTag != 0x53415647 || SaveGameFileVersion != 3)
        {
            return false;
        }

        FPackageFileVersion PackageVersion;
        Reader << PackageVersion;
        const int64 EngineVersionOffset = Reader.Tell();
        FEngineVersion EngineVersion;
        Reader << EngineVersion;
        const int64 EngineVersionEnd = Reader.Tell();
        if (Reader.IsError())
        {
            return false;
        }

        uint32 OtherChangelist = EngineVersion.GetChangelist() ^ 1U;
        if (EngineVersion.IsLicenseeVersion())
        {
            OtherChangelist = FEngineVersion::EncodeLicenseeChangelist(OtherChangelist);
        }
        FEngineVersion OtherVersion(EngineVersion.GetMajor(), EngineVersion.GetMinor(), EngineVersion.GetPatch(),
                                    OtherChangelist, EngineVersion.GetBranchDescriptor());
        OutBytes = OriginalRaw;
        FMemoryWriter Writer(OutBytes, true);
        Writer.Seek(EngineVersionOffset);
        Writer << OtherVersion;
        return !Writer.IsError() && Writer.Tell() == EngineVersionEnd && OutBytes.Num() == OriginalRaw.Num() &&
               OutBytes != OriginalRaw;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableProfileV10PreservationTest,
                                 "Fablewood.Profile.V9PreservesAllFieldsAddsLegacyAnchor",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 원본을 유지하고 UUID·옵션·순서·절대 UTC까지 모두 보존한다. */
bool FFableProfileV10PreservationTest::RunTest(const FString& Parameters)
{
    auto* Original = OriginalNine();
    if (!TestNotNull(TEXT("Valid nontrivial V9 fixture"), Original) ||
        !TestTrue(TEXT("Original validates as nine"), Original->IsValidProfile()))
    {
        return false;
    }
    const auto Before = HistoricalBytes(Original);
    const auto Inventory = FableInventorySnapshot::Make(Original);
    const auto Summary = FableCharacterSummary::MakeSummary(Original);
    auto* Candidate = FableProfileSchema::MakeCandidate(Original);
    if (!TestNotNull(TEXT("Separate V10 candidate"), Candidate))
    {
        return false;
    }

    TestTrue(TEXT("Original object and every old field remain unchanged"),
             Original->Version == 9 && Original->LastSafeTownId == EFableTownId::Invalid &&
                 HistoricalBytes(Original) == Before);
    TestTrue(TEXT("Every historical saved field survives upgrade"), HistoricalBytes(Candidate) == Before);
    TArray<uint8> OriginalRaw;
    if (!TestTrue(TEXT("Serialize original V9 fixture"), UGameplayStatics::SaveGameToMemory(Original, OriginalRaw)))
    {
        return false;
    }
    const TArray<uint8> OriginalRawBefore = OriginalRaw;
    const FString OriginalHash = FableProfileStorage::HashBytes(OriginalRaw);
    TestTrue(TEXT("Production guard compares all historical fields under the same current serializer"),
             FableProfileSchema::PreservesOriginalNineBytes(Candidate, OriginalRaw));

    TArray<uint8> OtherHeaderRaw;
    if (!TestTrue(TEXT("Change only serialized UE engine changelist metadata"),
                  WithDifferentEngineHeader(OriginalRaw, OtherHeaderRaw)))
    {
        return false;
    }
    const TArray<uint8> OtherHeaderRawBefore = OtherHeaderRaw;
    const FString OtherHeaderHash = FableProfileStorage::HashBytes(OtherHeaderRaw);
    auto* OtherHeaderOriginal = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(OtherHeaderRaw));
    if (!TestNotNull(TEXT("Different valid UE header still loads as V9"), OtherHeaderOriginal))
    {
        return false;
    }
    TestTrue(TEXT("Header variation preserves every historical saved field"),
             OtherHeaderOriginal->Version == 9 && OtherHeaderOriginal->IsValidProfile() &&
                 HistoricalBytes(OtherHeaderOriginal) == Before);
    TestTrue(TEXT("Actual raw bytes and hashes differ for the header variation"),
             OriginalRaw != OtherHeaderRaw && OriginalHash.Len() == 64 && OtherHeaderHash.Len() == 64 &&
                 OriginalHash != OtherHeaderHash);
    TestTrue(TEXT("Production guard accepts identical historical fields with a different valid UE header"),
             FableProfileSchema::PreservesOriginalNineBytes(Candidate, OtherHeaderRaw));
    ++Candidate->Gold;
    TestFalse(TEXT("Production guard rejects changed Gold with the original header"),
              FableProfileSchema::PreservesOriginalNineBytes(Candidate, OriginalRaw));
    TestFalse(TEXT("Production guard rejects changed Gold with a different valid UE header"),
              FableProfileSchema::PreservesOriginalNineBytes(Candidate, OtherHeaderRaw));
    --Candidate->Gold;
    TestTrue(TEXT("Comparison preserves authenticated original raw bytes and hash"),
             OriginalRaw == OriginalRawBefore && FableProfileStorage::HashBytes(OriginalRaw) == OriginalHash);
    TestTrue(TEXT("Comparison preserves different-header raw bytes and hash"),
             OtherHeaderRaw == OtherHeaderRawBefore &&
                 FableProfileStorage::HashBytes(OtherHeaderRaw) == OtherHeaderHash);
    TestTrue(TEXT("Exact legacy bootstrap"),
             Candidate->Version == 10 && Candidate->LastSafeTownId == EFableTownId::LegacyVillage &&
                 Candidate->LastSafeAnchorId == FName(TEXT("Entry")) && Candidate->TownCatalogVersion == 1);
    TestTrue(TEXT("Typed inventory and summary wires stay identical"),
             FableInventorySnapshot::Matches(Candidate, Inventory) &&
                 FableCharacterSummary::Matches(Candidate, Summary));
    TestTrue(TEXT("Repeated migration is idempotent"),
             Candidate->MigrateAndValidate() && HistoricalBytes(Candidate) == Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableProfileV10InvalidTest, "Fablewood.Profile.CurrentMissingUnknownAndFutureReject",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 유효10 양성에서 하나씩 변경하여 누락/미래/UUID 오류의 실제 경계를 검사한다. */
bool FFableProfileV10InvalidTest::RunTest(const FString& Parameters)
{
    auto* Current = FableProfileSchema::MakeCandidate(OriginalNine());
    if (!TestNotNull(TEXT("Valid ten positive control"), Current))
    {
        return false;
    }
    TestTrue(TEXT("Complete ten validates"), Current->IsValidProfile());
    const TArray<TFunction<void(UFableSaveGame*)>> Mutations = {[](UFableSaveGame* P)
                                                                {
                                                                    P->LastSafeTownId = EFableTownId::Invalid;
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->LastSafeTownId = EFableTownId(255);
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->LastSafeAnchorId = NAME_None;
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->LastSafeAnchorId = TEXT("Portal.Expedition");
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->TownCatalogVersion = 0;
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->TownCatalogVersion = 2;
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->Version = FableProfileSchema::CurrentVersion + 1;
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->Inventory[0].InstanceId.Invalidate();
                                                                },
                                                                [](UFableSaveGame* P)
                                                                {
                                                                    P->StashItems[0].InstanceId =
                                                                        P->Inventory[0].InstanceId;
                                                                }};
    for (int32 I = 0; I < Mutations.Num(); ++I)
    {
        auto* Bad = DuplicateObject<UFableSaveGame>(Current, GetTransientPackage());
        Mutations[I](Bad);
        TArray<uint8> Before, After;
        UGameplayStatics::SaveGameToMemory(Bad, Before);
        TestFalse(FString::Printf(TEXT("Invalid current/future mutation %d rejects without bootstrap"), I),
                  FableProfileSchema::MakeCandidate(Bad) != nullptr);
        UGameplayStatics::SaveGameToMemory(Bad, After);
        TestTrue(TEXT("Rejected original bytes are unchanged"), Before == After);
    }
    auto* Nine = OriginalNine();
    Nine->Inventory[0].InstanceId.Invalidate();
    TestFalse(TEXT("UUID introduction remains nine, no regeneration"), Nine->MigrateAndValidate());
    TestFalse(TEXT("Rejected nine UUID remains absent"), Nine->Inventory[0].InstanceId.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableProfileV10PolicyTest,
                                 "Fablewood.Profile.KnownUnreadyAndBootstrapPolicyPreserveCurrent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 합성 정책은 미술 채택 증거가 아니며 유효한 기존10을 바꾸지 않는다. */
bool FFableProfileV10PolicyTest::RunTest(const FString& Parameters)
{
    auto* Legacy = FableProfileSchema::MakeCandidate(OriginalNine());
    auto* Preserved = FableProfileSchema::MakeCandidate(Legacy, EFableTownBootstrapPolicy::AdoptedSunbrookV1);
    auto* Known = FableProfileSchema::MakeCandidate(OriginalNine(), EFableTownBootstrapPolicy::AdoptedSunbrookV1);
    if (!TestNotNull(TEXT("Legacy candidate"), Legacy) || !TestNotNull(TEXT("Preserved current"), Preserved) ||
        !TestNotNull(TEXT("Known unready format candidate"), Known))
    {
        return false;
    }
    TestTrue(TEXT("New policy never rewrites valid current Legacy"),
             Preserved->LastSafeTownId == EFableTownId::LegacyVillage);
    TestTrue(TEXT("Known unready is format valid"),
             Known->LastSafeTownId == EFableTownId::Sunbrook && Known->IsValidProfile());
    FTransform Actual;
    TestFalse(TEXT("Known metadata does not authorize actual ready geometry"),
              GetDefault<UFableTownCatalog>()->ResolveAnchor(Known->GetLastSafeTownAnchor(), 0, Actual, true));
    TestTrue(TEXT("Known reference remains unchanged"),
             Known->MigrateAndValidate() && Known->LastSafeTownId == EFableTownId::Sunbrook);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableProfileV10CompatibilityTest, "Fablewood.Profile.CompatibilityBeforeLease",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** old/future/malformed/duplicate는 정확한 새 tuple 양성과 구별한다. */
bool FFableProfileV10CompatibilityTest::RunTest(const FString& Parameters)
{
    const FString Good = FableProfileSchema::CompatibilityOptions();
    TestTrue(TEXT("Exact current tuple accepts"), FableProfileSchema::HasCompatibleOptions(Good));
    TestTrue(TEXT("Ticket does not alter schema permission"),
             FableProfileSchema::HasCompatibleOptions(TEXT("?FableTicket=opaque") + Good));
    const TArray<FString> Bad = {TEXT(""),
                                 TEXT("?FableProfileSchema=9?FableTownCatalog=1?FableTownProtocol=1"),
                                 TEXT("?FableProfileSchema=11?FableTownCatalog=1?FableTownProtocol=1"),
                                 TEXT("?FableProfileSchema=010?FableTownCatalog=1?FableTownProtocol=1"),
                                 TEXT("?FableProfileSchema=10?FableTownCatalog=1"),
                                 Good + TEXT("?FableProfileSchema=10"),
                                 Good + TEXT("?fabletownprotocol=1"),
                                 Good + TEXT("?FableTownCatalog=2")};
    for (const auto& Options : Bad)
    {
        TestFalse(TEXT("Unsupported tuple rejects"), FableProfileSchema::HasCompatibleOptions(Options));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableProfileV10StorageTest, "Fablewood.Profile.LocalRawBackupReadbackAndCollision",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** このテストが発行したUUID付きslot/backupだけを作成・削除する。 */
bool FFableProfileV10StorageTest::RunTest(const FString& Parameters)
{
    auto* Original = OriginalNine();
    auto* Candidate = FableProfileSchema::MakeCandidate(Original);
    if (!TestNotNull(TEXT("Valid migration candidate"), Candidate))
    {
        return false;
    }
    TArray<uint8> Raw, Readback;
    UGameplayStatics::SaveGameToMemory(Original, Raw);
    const FString Slot = TEXT("Fablewood_V10Native_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Backup = TEXT("Fablewood_V10Backup_") + FableProfileStorage::HashBytes(Raw);
    if (!TestFalse(TEXT("Unique backup is initially absent"), UGameplayStatics::DoesSaveGameExist(Backup, 0)))
    {
        return false;
    }
    TestTrue(TEXT("Write owned original fixture"), UGameplayStatics::SaveDataToSlot(Raw, Slot, 0));
    TestTrue(TEXT("Actual nine file cannot confirm a current continue index"),
             FableProfileStorage::ReadVerifiedCurrent(Slot) == nullptr);
    TArray<uint8> PreviewRaw;
    auto* Preview = FableProfileStorage::ReadCandidate(Slot, PreviewRaw);
    TestTrue(TEXT("Preview migrates memory only"),
             Preview && Preview->Version == 10 && PreviewRaw == Raw && !UGameplayStatics::DoesSaveGameExist(Backup, 0));

    const TArray<uint8> Collision = {1, 2, 3};
    UGameplayStatics::SaveDataToSlot(Collision, Backup, 0);
    TestFalse(TEXT("Wrong existing backup blocks before original write"),
              FableProfileStorage::WriteVerified(Slot, Candidate));
    TestTrue(TEXT("Original still exact after refused backup"),
             UGameplayStatics::LoadDataFromSlot(Readback, Slot, 0) && Readback == Raw);
    UGameplayStatics::DeleteGameInSlot(Backup, 0);

    TestTrue(TEXT("Backup then verified ten write succeeds"), FableProfileStorage::WriteVerified(Slot, Candidate));
    TestTrue(TEXT("Backup retains original raw bytes"),
             UGameplayStatics::LoadDataFromSlot(Readback, Backup, 0) && Readback == Raw);
    TestNotNull(TEXT("Verified actual ten file can confirm continue index"),
                FableProfileStorage::ReadVerifiedCurrent(Slot));
    TArray<uint8> CurrentRaw;
    auto* Current = FableProfileStorage::ReadCandidate(Slot, CurrentRaw);
    TestTrue(TEXT("Current slot actually reads ten and all old values"),
             Current && Current->Version == 10 && HistoricalBytes(Current) == HistoricalBytes(Original) &&
                 CurrentRaw != Raw);
    TestFalse(TEXT("Backend identifiers cannot become local paths"),
              FableProfileStorage::WriteVerified(TEXT("backend:untrusted"), Candidate));

    UGameplayStatics::DeleteGameInSlot(Slot, 0);
    UGameplayStatics::DeleteGameInSlot(Backup, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableProfileOriginalMirrorsTest,
                                 "Fablewood.Profile.OriginalMirrorsAndExactV9SummarySeedEligibility",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 原本全mirrorが正しいV9+明示null以外はseedで修復しない。 */
bool FFableProfileOriginalMirrorsTest::RunTest(const FString& Parameters)
{
    auto* Original = OriginalNine();
    if (!TestNotNull(TEXT("Original nine"), Original))
    {
        return false;
    }
    auto Wallet = MakeShared<FJsonObject>();
    Wallet->SetNumberField(TEXT("gold"), Original->Gold);
    Wallet->SetNumberField(TEXT("ore"), Original->Ore);
    Wallet->SetNumberField(TEXT("essence"), Original->Essence);
    Wallet->SetNumberField(TEXT("rootCores"), Original->RootCores);
    Wallet->SetNumberField(TEXT("moonCrystals"), Original->MoonCrystals);
    Wallet->SetNumberField(TEXT("healthPotions"), Original->HealthPotions);
    Wallet->SetNumberField(TEXT("manaPotions"), Original->ManaPotions);
    const auto Inventory = FableInventorySnapshot::Make(Original);
    const auto Summary = FableCharacterSummary::MakeSummary(Original);
    const TSharedPtr<FJsonValue> NullSummary = MakeShared<FJsonValueNull>();
    bool bSeed = false;
    TestTrue(TEXT("Exact valid original nine and explicit null is seed eligible"),
             FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory, NullSummary, bSeed) && bSeed);
    TestTrue(TEXT("Existing exact summary follows normal strict path"),
             FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory,
                                                      MakeShared<FJsonValueObject>(Summary), bSeed) &&
                 !bSeed);

    Wallet->SetNumberField(TEXT("gold"), Original->Gold + 1);
    TestFalse(TEXT("Wallet mismatch never seeds"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory, NullSummary, bSeed));
    Wallet->SetNumberField(TEXT("gold"), Original->Gold);
    TestFalse(TEXT("Missing summary property never seeds"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory, nullptr, bSeed));
    TestFalse(TEXT("Mistyped summary never seeds"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory,
                                                       MakeShared<FJsonValueString>(TEXT("null")), bSeed));
    TestFalse(TEXT("Original nine cannot lose typed inventory"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 0, Wallet, nullptr, NullSummary, bSeed));
    Summary->SetNumberField(TEXT("level"), Original->Level + 1);
    TestFalse(TEXT("Existing wrong summary cannot be repaired"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory,
                                                       MakeShared<FJsonValueObject>(Summary), bSeed));
    Inventory->SetNumberField(TEXT("nextLocalId"), Original->NextItemId + 1);
    TestFalse(TEXT("Inventory mismatch never seeds"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory, NullSummary, bSeed));
    Inventory->SetNumberField(TEXT("nextLocalId"), Original->NextItemId);

    auto* Current = FableProfileSchema::MakeCandidate(Original);
    TestFalse(TEXT("Valid ten cannot seed missing summary"),
              FableProfileSchema::OriginalMirrorsMatch(Current, 1, Wallet, Inventory, NullSummary, bSeed));
    Current->Version = 11;
    TestFalse(TEXT("Future cannot seed"),
              FableProfileSchema::OriginalMirrorsMatch(Current, 1, Wallet, Inventory, NullSummary, bSeed));
    Original->Inventory[0].InstanceId.Invalidate();
    TestFalse(TEXT("Missing original UUID is not repaired"),
              FableProfileSchema::OriginalMirrorsMatch(Original, 1, Wallet, Inventory, NullSummary, bSeed));
    return true;
}
#endif
