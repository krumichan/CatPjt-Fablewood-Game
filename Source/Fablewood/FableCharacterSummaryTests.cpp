#include "FableCharacterSummary.h"
#include "FableSaveGame.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    /** 캐릭터 요약 검증 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    UFableSaveGame* SummaryFixture(EFableClass HeroClass = EFableClass::Warrior, int32 Tier = 1)
    {
        // Historical schema three supplies legitimate grandfathered advancement trials on migration.
        // 프로필과 저장 스냅숏에 필요한 값 준비
        auto Profile = NewObject<UFableSaveGame>();
        Profile->Version = 3;
        Profile->HeroClass = HeroClass;
        Profile->Level = 4;
        Profile->Experience = 39;
        Profile->ForestQuest = EFableQuestStage::Completed;
        Profile->QuestKills = 3;
        Profile->DungeonCompletions = 1;

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
        Profile->EquippedWeaponId = int32(HeroClass) + 1;
        Profile->ClassProgress[int32(HeroClass)].Tier = Tier;
        Profile->MigrateAndValidate();

        return Profile;
    }

    /** 저장 확인 응답에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    TSharedRef<FJsonObject> Receipt()
    {
        // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
        auto Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("revision"), 9);
        Result->SetStringField(TEXT("profileHash"),
                               TEXT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        Result->SetNumberField(TEXT("inventoryVersion"), 1);
        Result->SetNumberField(TEXT("summaryVersion"), 1);

        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableSummaryDerivationTest,
                                 "Fablewood.Backend.CharacterSummaryDerivationAndSaveRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 캐릭터 요약 요약 투영 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableSummaryDerivationTest::RunTest(const FString& Parameters)
{
    // 기대값과 상태 보존 검증 순회
    for (int32 ClassIndex = 0; ClassIndex < 3; ++ClassIndex)
    {
        for (int32 Tier = 1; Tier <= 3; ++Tier)
        {
            auto Profile = SummaryFixture(EFableClass(ClassIndex), Tier);

            // 기대값과 상태 보존 검증 조건 검사
            if (!TestTrue(TEXT("Valid migrated character fixture"),
                          Profile->Version == FableProfileSchema::CurrentVersion && Profile->IsValidProfile()))
            {
                return false;
            }

            const auto Summary = FableCharacterSummary::MakeSummary(Profile);

            // 기대값과 상태 보존 검증 조건 검사
            if (!TestTrue(TEXT("Summary derives from the selected class only"), Summary.IsValid()))
            {
                return false;
            }

            // 기대값과 상태 보존 검증 처리
            TestEqual(TEXT("Exactly five public fields"), Summary->Values.Num(), 5);
            TestTrue(TEXT("Class, level, XP and selected class tier match"),
                     Summary->GetNumberField(TEXT("version")) == 1 &&
                         Summary->GetNumberField(TEXT("heroClass")) == ClassIndex &&
                         Summary->GetNumberField(TEXT("level")) == 4 &&
                         Summary->GetNumberField(TEXT("experience")) == 39 &&
                         Summary->GetNumberField(TEXT("classTier")) == Tier);
            TArray<uint8> Bytes;

            // JSON 계약과 검증 기록 조건 검사
            if (!TestTrue(TEXT("Serialize real UE SaveGame in memory"),
                          UGameplayStatics::SaveGameToMemory(Profile, Bytes)))
            {
                return false;
            }

            // 기대값과 상태 보존 검증에 필요한 값 준비
            auto Restored = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
            TestTrue(TEXT("Same character card follows the serialized blob"),
                     Restored && Restored->IsValidProfile() && FableCharacterSummary::Matches(Restored, Summary));
            Profile->Level = 5;
            Profile->Experience = 57;
            TestFalse(TEXT("A queued summary stays frozen when live progression changes"),
                      FableCharacterSummary::Matches(Profile, Summary));
            TestTrue(TEXT("Frozen summary still matches the original blob"),
                     FableCharacterSummary::Matches(Restored, Summary));
        }
    }

    AddInfo(TEXT("Memory-only UE save fixtures; no slots, HTTP requests, databases or gameplay processes are used."));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableSummaryStrictTest,
                                 "Fablewood.Backend.CharacterSummaryRejectsMissingAlteredAndWrongTypes",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 캐릭터 요약 엄격한 계약 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableSummaryStrictTest::RunTest(const FString& Parameters)
{
    // 프로필과 저장 스냅숏에 필요한 값 준비
    auto Profile = SummaryFixture();
    const TCHAR* Fields[] = {TEXT("version"), TEXT("heroClass"), TEXT("level"), TEXT("experience"), TEXT("classTier")};

    // JSON 계약과 검증 기록 순회
    for (const TCHAR* Field : Fields)
    {
        auto Changed = FableCharacterSummary::MakeSummary(Profile);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Changed.IsValid())
        {
            return false;
        }

        // JSON 계약과 검증 기록 처리
        Changed->RemoveField(Field);
        TestFalse(TEXT("Every summary field is required"), FableCharacterSummary::Matches(Profile, Changed));
        Changed = FableCharacterSummary::MakeSummary(Profile);
        Changed->SetNumberField(Field, Changed->GetNumberField(Field) + 1);
        TestFalse(TEXT("A changed individual summary value cannot replace the decoded blob"),
                  FableCharacterSummary::Matches(Profile, Changed));
        Changed = FableCharacterSummary::MakeSummary(Profile);
        Changed->SetStringField(Field, TEXT("0"));
        TestFalse(TEXT("Numeric strings are not accepted as typed values"),
                  FableCharacterSummary::Matches(Profile, Changed));
        Changed = FableCharacterSummary::MakeSummary(Profile);
        Changed->SetField(Field, MakeShared<FJsonValueNull>());
        TestFalse(TEXT("Null field is distinct from zero"), FableCharacterSummary::Matches(Profile, Changed));
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    auto Extra = FableCharacterSummary::MakeSummary(Profile);
    Extra->SetNumberField(TEXT("extra"), 1);
    TestFalse(TEXT("Unknown summary shape is rejected"), FableCharacterSummary::Matches(Profile, Extra));
    TestFalse(TEXT("No summary is not a confirmed mirror"), FableCharacterSummary::Matches(Profile, nullptr));
    Profile->Experience = 0;
    TestTrue(TEXT("Explicit zero XP is legitimate"),
             FableCharacterSummary::Matches(Profile, FableCharacterSummary::MakeSummary(Profile)));
    Profile->Level = 20;
    TestTrue(TEXT("Level cap with zero XP is legitimate"), FableCharacterSummary::MakeSummary(Profile).IsValid());
    Profile->Experience = 1;
    TestFalse(TEXT("Invalid capped XP never creates a public character card"),
              FableCharacterSummary::MakeSummary(Profile).IsValid());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableSummaryProfileValidationTest,
                                 "Fablewood.Backend.CharacterSummaryRequiresCurrentValidatedProfile",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 캐릭터 요약 프로필 검증 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableSummaryProfileValidationTest::RunTest(const FString& Parameters)
{
    // 기대값과 상태 보존 검증에 필요한 값 준비
    auto Profile = SummaryFixture();
    TestFalse(TEXT("Missing profile is rejected"), FableCharacterSummary::MakeSummary(nullptr).IsValid());
    Profile->Version = 8;
    TestFalse(TEXT("Unmigrated profile cannot be committed with a current summary"),
              FableCharacterSummary::MakeSummary(Profile).IsValid());
    Profile->Version = FableProfileSchema::CurrentVersion;
    Profile->Inventory[0].InstanceId.Invalidate();
    TestFalse(TEXT("Malformed identity cannot be hidden by a plausible summary"),
              FableCharacterSummary::MakeSummary(Profile).IsValid());
    Profile = SummaryFixture();
    Profile->ClassProgress.Reset();
    TestFalse(TEXT("Missing advancement array is rejected without indexing it"),
              FableCharacterSummary::MakeSummary(Profile).IsValid());
    Profile = SummaryFixture();
    Profile->HeroClass = EFableClass(255);
    TestFalse(TEXT("Invalid class cannot index advancement data"),
              FableCharacterSummary::MakeSummary(Profile).IsValid());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableSummaryAcknowledgementTest,
                                 "Fablewood.Backend.CharacterCommitRequiresBothMirrorAcknowledgements",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 캐릭터 요약 저장 확인 응답 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableSummaryAcknowledgementTest::RunTest(const FString& Parameters)
{
    // 기대값과 상태 보존 검증에 필요한 값 준비
    const FString Hash = TEXT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    TestTrue(TEXT("Exact commit-character receipt is accepted"),
             FableCharacterSummary::IsCommitAcknowledgement(Receipt(), 9, Hash));

    // 접속 권한·저장 리비전·멱등 키 순회
    for (const TCHAR* Field : {TEXT("revision"), TEXT("inventoryVersion"), TEXT("summaryVersion")})
    {
        // JSON 계약과 검증 기록에 필요한 값 준비
        auto Changed = Receipt();
        Changed->RemoveField(Field);
        TestFalse(TEXT("Missing mirror ACK never completes login or leave"),
                  FableCharacterSummary::IsCommitAcknowledgement(Changed, 9, Hash));
        Changed = Receipt();
        Changed->SetStringField(Field, TEXT("1"));
        TestFalse(TEXT("Untyped ACK is rejected"), FableCharacterSummary::IsCommitAcknowledgement(Changed, 9, Hash));
        Changed = Receipt();
        Changed->SetNumberField(Field, 2);
        TestFalse(TEXT("Wrong revision or mirror version is rejected"),
                  FableCharacterSummary::IsCommitAcknowledgement(Changed, 9, Hash));
    }

    // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
    auto Changed = Receipt();
    Changed->SetStringField(TEXT("profileHash"),
                            TEXT("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    TestFalse(TEXT("Receipt for a different immutable body is rejected"),
              FableCharacterSummary::IsCommitAcknowledgement(Changed, 9, Hash));
    Changed = Receipt();
    Changed->SetNumberField(TEXT("revision"), 9.5);
    TestFalse(TEXT("Fractional revision is rejected"),
              FableCharacterSummary::IsCommitAcknowledgement(Changed, 9, Hash));
    TestFalse(TEXT("Absent response is not successful storage"),
              FableCharacterSummary::IsCommitAcknowledgement(nullptr, 9, Hash));
    TestFalse(TEXT("Invalid expected hash cannot acknowledge a commit"),
              FableCharacterSummary::IsCommitAcknowledgement(Receipt(), 9, TEXT("")));

    return true;
}
#endif
