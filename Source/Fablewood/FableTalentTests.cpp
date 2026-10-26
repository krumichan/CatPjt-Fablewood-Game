#include "FablePlayerState.h"
#include "FableSaveGame.h"
#include "FableCharacter.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

// Memory-only fixtures. Never initialize a profile, open a slot, or enable persistence.

/** 전직 특성 회귀 검증에 필요한 제한된 내부 접근 도우미. */
struct FFableTalentTestAccess
{
    /** 격리 검증에 필요한 초기 프로필·장비·성장 상태를 준비한다. */
    static void Prepare(AFablePlayerState* S)
    {
        S->bPersistenceEnabled = false;
        S->ProfileSlot.Empty();
        S->bStorageBlocked = false;
        S->bProfileLoaded = true;
        S->NextItemId = 5;
        Ready(S);
    }

    /** 검증 대상의 준비 상태를 설정한다. */
    static void Ready(AFablePlayerState* S)
    {
        S->LastMutationTime = -100;
    }

    /** 거부 경로 검증을 위해 변경 가능 상태를 제한한다. */
    static void Block(AFablePlayerState* S, bool Value)
    {
        S->bStorageBlocked = Value;
    }

    /** 검증 대상이 메모리 전용 저장 상태인지 확인한다. */
    static bool MemoryOnly(const AFablePlayerState* S)
    {
        return !S->bPersistenceEnabled && S->ProfileSlot.IsEmpty();
    }
};
namespace
{
    /** 특성 프로필에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    UFableSaveGame* TalentProfile(int32 Version = 8)
    {
        // 재화와 작업 비용에 필요한 값 준비
        UFableSaveGame* P = NewObject<UFableSaveGame>();
        P->Version = Version;
        P->Gold = 237;
        P->Ore = 17;
        P->HealthPotions = 4;
        P->ManaPotions = 3;

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

    /** 전직 회귀 검증에 사용할 직업 성장 단계를 준비한다. */
    void GrantTier(FFableClassProgress& P, int32 Tier)
    {
        P.Tier = Tier;

        // 퀘스트와 이야기 진행 처리
        if (Tier >= 2)
        {
            P.SecondTrial.Stage = EFableQuestStage::Completed;
            P.SecondTrial.bLegacyGranted = true;
        }

        // 퀘스트와 이야기 진행 처리
        if (Tier >= 3)
        {
            P.ThirdTrial.Stage = EFableQuestStage::Completed;
            P.ThirdTrial.bLegacyGranted = true;
        }
    }

    /** 직업 성장 상태를 변경 전후 비교용 서명으로 직렬화한다. */
    FString ProgressSignature(const FFableClassProgress& P)
    {
        // 직업·스킬·특성 빌드에 필요한 값 준비
        FString V =
            FString::Printf(TEXT("%d:%d,%d:%d,%d,%d;%d,%d,%d"), P.Tier, uint8(P.SecondTalent), uint8(P.ThirdTalent),
                            uint8(P.SecondTrial.Stage), P.SecondTrial.ValidSkillCasts, P.SecondTrial.bLegacyGranted,
                            uint8(P.ThirdTrial.Stage), P.ThirdTrial.ValidSkillCasts, P.ThirdTrial.bLegacyGranted);

        // 같은 규칙을 적용할 대상을 순회한다.
        for (int32 R : P.Ranks)
        {
            V += TEXT(",") + FString::FromInt(R);
        }

        return V;
    }

    /** 요청 거부 시 보존되어야 하는 프로필 상태를 직렬화한다. */
    FString ProtectedState(const AFablePlayerState* S)
    {
        // 재화와 작업 비용에 필요한 값 준비
        FString V = FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d"),
                                    uint8(S->HeroClass), S->Level, S->Experience, S->Ore, S->Essence, S->RootCores,
                                    S->MoonCrystals, S->HealthPotions, S->ManaPotions, S->EquippedWeaponId,
                                    S->EquippedArmorId, uint8(S->ForestQuest), uint8(S->DungeonQuest),
                                    uint8(S->MoonwellQuest), S->QuestKills, S->DungeonCompletions,
                                    S->MoonwellCompletions, S->StoryChapter, S->StoryObjectMask, S->StorySealMask);

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : S->Inventory)
        {
            V += TEXT("|i:") + FableRPG::ItemPersistentSignature(I);
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : S->StashItems)
        {
            V += TEXT("|s:") + FableRPG::ItemPersistentSignature(I);
        }

        // 직업·스킬·특성 빌드 순회
        for (int32 I = 0; I < S->ClassProgress.Num(); ++I)
        {
            FFableClassProgress P = S->ClassProgress[I];
            if (I == int32(S->HeroClass))
            {
                P.Ranks = {1, 1, 1, 0};
                P.SecondTalent = P.ThirdTalent = EFableTalentPath::None;
            }
            V += TEXT("|c:") + ProgressSignature(P);
        }

        return V;
    }

    /** 실제 게임 상태를 격리해 구성하고 종료 시 해제하는 검증 월드. */
    struct FTalentWorld
    {
        UWorld* World = nullptr;
        AFablePlayerState* State = nullptr;
        AFableCharacter* Hero = nullptr;
        AFableGameState* GS = nullptr;
        AFableGameMode* Mode = nullptr;
        APlayerController* Controller = nullptr;
        bool bControllerRegistered = false;

        /** 특성 회귀 검증용 월드와 플레이어를 만들고 이전 실행 문맥을 보존한다. */
        FTalentWorld()
        {
            // 현재 월드와 플레이어 참조에 필요한 값 준비
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

            // 현재 월드와 플레이어 참조 조건 검사
            if (!World)
            {
                return;
            }

            // 현재 월드와 플레이어 참조 처리
            State = World->SpawnActor<AFablePlayerState>();
            GS = World->SpawnActor<AFableGameState>();
            Mode = World->SpawnActor<AFableGameMode>();
            Controller = World->SpawnActor<APlayerController>();
            FActorSpawnParameters Spawn;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Hero = World->SpawnActor<AFableCharacter>(FableRPG::BoardLocation() + FVector(0, -100, 110),
                                                      FRotator::ZeroRotator, Spawn);

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!State || !GS || !Mode || !Controller || !Hero)
            {
                return;
            }

            // 현재 월드와 플레이어 참조 처리
            World->CopyGameState(Mode, GS);
            Controller->SetPlayerState(State);
            Controller->Possess(Hero);
            Hero->SetPlayerState(State);
            State->SetOwner(Controller);
            World->AddController(Controller);
            bControllerRegistered = true;
            FFableTalentTestAccess::Prepare(State);
            State->Inventory = TalentProfile()->Inventory;
            State->Level = 10;
            State->Gold = 500;
            State->Ore = 17;
            State->ForestQuest = EFableQuestStage::Completed;
            State->QuestKills = 3;
            State->DungeonQuest = State->MoonwellQuest = EFableQuestStage::Completed;
            State->DungeonCompletions = State->MoonwellCompletions = 2;
            State->StoryChapter = 5;
            State->StoryObjectMask = 31;
            State->StorySealMask = 7;
            State->Essence = 13;
            State->RootCores = 2;
            State->MoonCrystals = 3;

            // 직업·스킬·특성 빌드 순회
            for (FFableClassProgress& P : State->ClassProgress)
            {
                GrantTier(P, 3);
            }
        }

        /** 검증 월드와 소유 객체를 해제하고 이전 실행 문맥을 복구한다. */
        ~FTalentWorld()
        {
            if (World)
            {
                // 이 fixture가 추가한 연결만 월드 종료 전에 해제한다.
                if (bControllerRegistered && IsValid(Controller))
                {
                    World->RemoveController(Controller);
                }

                World->DestroyWorld(false);
            }
        }

        /** 실제 특성 선택 경로를 호출해 검증 결과를 반환한다. */
        void Select(int32 Tier, EFableTalentPath Path)
        {
            FFableTalentTestAccess::Ready(State);
            State->SelectTalent(Tier, Path);
        }

        /** 실제 빌드 초기화 경로를 호출해 검증 결과를 반환한다. */
        void Reset(EFableClass Class, int32 Revision)
        {
            FFableTalentTestAccess::Ready(State);
            State->ResetCurrentClassBuild(Class, Revision);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTalentMigrationTest, "Fablewood.Talents.V8MigrationAndStrictRejection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 특성 버전 이관 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableTalentMigrationTest::RunTest(const FString& Parameters)
{
    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 Version = 0; Version <= 7; ++Version)
    {
        // 직업·스킬·특성 빌드에 필요한 값 준비
        UFableSaveGame* P = TalentProfile(Version);
        TArray<FString> Before;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItem& I : P->Inventory)
        {
            Before.Add(FableRPG::ItemPersistentSignature(I));
        }

        // 기대값과 상태 보존 검증 처리
        if (!TestTrue(FString::Printf(TEXT("Historical schema %d migrates"), Version), P->MigrateAndValidate()))
        {
            continue;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        TestEqual(TEXT("Migration reaches current schema"), P->Version, FableProfileSchema::CurrentVersion);
        TestTrue(TEXT("Migration grants no currency, XP or equipment"),
                 P->Gold == 237 && P->Ore == 17 && P->Level == 1 && P->Experience == 0 && P->Inventory.Num() == 4);

        // 직업·스킬·특성 빌드 순회
        for (const FFableClassProgress& C : P->ClassProgress)
        {
            TestTrue(TEXT("Every migrated class has two unselected talents"),
                     C.SecondTalent == EFableTalentPath::None && C.ThirdTalent == EFableTalentPath::None);
        }

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 I = 0; I < Before.Num(); ++I)
        {
            TestEqual(TEXT("Starter metadata survives"), FableRPG::ItemPersistentSignature(P->Inventory[I]), Before[I]);
        }

        TestTrue(TEXT("Migration is repeatable without granting a choice"), P->MigrateAndValidate());
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const auto Reject = [&](const TCHAR* Label, TFunctionRef<void(UFableSaveGame*)> Change)
    {
        UFableSaveGame* P = TalentProfile();
        Change(P);
        TestFalse(Label, P->MigrateAndValidate());
    };
    Reject(TEXT("Unknown schema ten is rejected"),
           [](UFableSaveGame* P)
           {
               P->Version = FableProfileSchema::CurrentVersion + 1;
           });
    Reject(TEXT("Invalid second path is rejected"),
           [](UFableSaveGame* P)
           {
               P->ClassProgress[0].SecondTalent = EFableTalentPath(3);
           });
    Reject(TEXT("Invalid third path is rejected"),
           [](UFableSaveGame* P)
           {
               P->ClassProgress[2].ThirdTalent = EFableTalentPath(255);
           });
    Reject(TEXT("Tier one cannot claim a second talent"),
           [](UFableSaveGame* P)
           {
               P->ClassProgress[0].SecondTalent = EFableTalentPath::First;
           });
    Reject(TEXT("Third talent requires both third tier and second choice"),
           [](UFableSaveGame* P)
           {
               P->ClassProgress[0].ThirdTalent = EFableTalentPath::Second;
           });
    UFableSaveGame* Advanced = TalentProfile();
    Advanced->Level = 10;
    Advanced->ForestQuest = EFableQuestStage::Completed;
    Advanced->QuestKills = 3;
    Advanced->DungeonCompletions = Advanced->MoonwellCompletions = 2;
    Advanced->DungeonQuest = Advanced->MoonwellQuest = EFableQuestStage::Completed;
    Advanced->StoryChapter = 5;
    Advanced->StoryObjectMask = 31;
    Advanced->StorySealMask = 7;

    // 직업·스킬·특성 빌드 순회
    for (FFableClassProgress& C : Advanced->ClassProgress)
    {
        GrantTier(C, 3);
    }

    // 직업·스킬·특성 빌드 순회
    for (int32 First = 1; First <= 2; ++First)
    {
        for (int32 Second = 1; Second <= 2; ++Second)
        {
            Advanced->ClassProgress[0].SecondTalent = EFableTalentPath(First);
            Advanced->ClassProgress[0].ThirdTalent = EFableTalentPath(Second);
            TestTrue(TEXT("Both homogeneous and mixed paths are valid"), Advanced->IsValidProfile());
            TestEqual(TEXT("First path rank is the number of explicit choices"),
                      FableRPG::TalentRank(Advanced->ClassProgress[0], EFableTalentPath::First),
                      (First == 1 ? 1 : 0) + (Second == 1 ? 1 : 0));
            TestEqual(TEXT("None never receives an effect rank"),
                      FableRPG::TalentRank(Advanced->ClassProgress[0], EFableTalentPath::None), 0);
        }
    }

    // 직업·스킬·특성 빌드 처리
    Advanced->ClassProgress[0].SecondTalent = EFableTalentPath::None;
    TestFalse(TEXT("Even tier three cannot select its third talent before second"), Advanced->IsValidProfile());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTalentSelectionTest, "Fablewood.Talents.AuthoritativeSelectionAndStageGates",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 특성 선택 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableTalentSelectionTest::RunTest(const FString& Parameters)
{
    // 직업·스킬·특성 빌드에 필요한 값 준비
    FTalentWorld F;
    AFablePlayerState* S = F.State;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Memory-only PlayerState"), S) || !TestNotNull(TEXT("Fixture hero"), F.Hero) ||
        !TestNotNull(TEXT("Fixture game state"), F.GS))
    {
        return false;
    }

    // 재화와 작업 비용 처리
    TestTrue(TEXT("No disk persistence in fixture"), FFableTalentTestAccess::MemoryOnly(S));
    const int32 Gold = S->Gold, Points = S->GetAvailableSkillPoints();
    F.Select(3, EFableTalentPath::First);
    TestEqual(TEXT("Third choice waits for second choice"), int32(S->GetTalentChoice(3)), 0);

    // 직업·스킬·특성 빌드 순회
    for (int32 Tier : {0, 1, 4, MAX_int32})
    {
        F.Select(Tier, EFableTalentPath::First);
    }

    // 직업·스킬·특성 빌드 처리
    F.Select(2, EFableTalentPath::None);
    F.Select(2, EFableTalentPath(255));
    TestEqual(TEXT("Invalid target/path cannot select"), S->GetTalentRank(EFableTalentPath::First), 0);
    S->ClassProgress[0].Tier = 1;
    F.Select(2, EFableTalentPath::First);
    TestEqual(TEXT("Low tier rejects selection"), int32(S->GetTalentChoice(2)), 0);
    S->ClassProgress[0].Tier = 3;
    S->bProfileLoaded = false;
    F.Select(2, EFableTalentPath::First);
    S->bProfileLoaded = true;
    FFableTalentTestAccess::Block(S, true);
    F.Select(2, EFableTalentPath::First);
    FFableTalentTestAccess::Block(S, false);
    S->SetRole(ROLE_SimulatedProxy);
    F.Select(2, EFableTalentPath::First);
    S->SetRole(ROLE_Authority);
    TestEqual(TEXT("Unloaded, blocked and client roles cannot choose"), int32(S->GetTalentChoice(2)), 0);
    F.Hero->SetActorLocation(FableRPG::BoardLocation() + FVector(331, 0, 110));
    F.Select(2, EFableTalentPath::First);
    TestEqual(TEXT("Remote selection is rejected"), int32(S->GetTalentChoice(2)), 0);
    F.Hero->SetActorLocation(FableRPG::BoardLocation() + FVector(100, 0, 300));
    F.Select(2, EFableTalentPath::First);
    TestEqual(TEXT("Vertical boundary is rejected"), int32(S->GetTalentChoice(2)), 0);
    F.Hero->SetActorLocation(FableRPG::BoardLocation() + FVector(100, 0, 110));

    // 진행 중인 전투의 빌드 제한은 게시판에서 명시 등록한 본인에게 적용한다.
    const EFableQuestStage PreviousForestQuest = S->ForestQuest;
    S->ForestQuest = EFableQuestStage::Active;
    const bool bJoinedField = F.Mode && F.Mode->JoinPublicField(F.Controller);
    S->ForestQuest = PreviousForestQuest;
    if (!TestTrue(TEXT("Talent gate fixture explicitly joins the public field"), bJoinedField))
    {
        return false;
    }

    // 직업·스킬·특성 빌드 순회
    for (EFableAdventureStage Stage :
         {EFableAdventureStage::Forest, EFableAdventureStage::Dungeon, EFableAdventureStage::Boss})
    {
        F.GS->FieldProgress.AdventureStage = Stage;
        F.Select(2, EFableTalentPath::First);
        TestEqual(TEXT("An ongoing encounter rejects build changes"), int32(S->GetTalentChoice(2)), 0);
    }

    // 직업·스킬·특성 빌드 처리
    F.GS->FieldProgress.AdventureStage = EFableAdventureStage::Village;
    F.GS->FieldProgress.bPartyRecoveryPending = true;
    F.Select(2, EFableTalentPath::First);
    F.GS->FieldProgress.bPartyRecoveryPending = false;
    TestEqual(TEXT("Party recovery rejects selection"), int32(S->GetTalentChoice(2)), 0);
    F.Hero->SetActorLocation(FableRPG::BoardLocation() + FVector(330, 0, 299));
    F.Select(2, EFableTalentPath::First);
    TestEqual(TEXT("Exact valid range permits second choice"), int32(S->GetTalentChoice(2)), 1);
    const int32 Revision = S->ClassBuildRevision;
    F.Select(2, EFableTalentPath::First);
    F.Select(2, EFableTalentPath::Second);
    TestTrue(TEXT("Duplicate or replacement does not mutate choice/revision"),
             S->GetTalentChoice(2) == EFableTalentPath::First && S->ClassBuildRevision == Revision);
    F.Select(3, EFableTalentPath::Second);
    TestTrue(TEXT("Explicit mixed choice gives rank one to each path"),
             S->GetTalentRank(EFableTalentPath::First) == 1 && S->GetTalentRank(EFableTalentPath::Second) == 1);
    TestTrue(TEXT("Talent selection spends neither gold nor skill points"),
             S->Gold == Gold && S->GetAvailableSkillPoints() == Points);
    S->HeroClass = EFableClass::Mage;
    S->EquippedWeaponId = 2;
    F.Hero->ReceiveHit(10000);
    F.Select(2, EFableTalentPath::First);
    TestEqual(TEXT("Inactive hero cannot choose"), int32(S->GetTalentChoice(2)), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTalentResetTest, "Fablewood.Talents.CurrentClassResetAndStaleConfirmation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 특성 초기화 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableTalentResetTest::RunTest(const FString& Parameters)
{
    // 직업·스킬·특성 빌드에 필요한 값 준비
    FTalentWorld F;
    AFablePlayerState* S = F.State;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Memory reset fixture"), S))
    {
        return false;
    }

    // 직업·스킬·특성 빌드 처리
    S->ClassProgress[0].Ranks = {3, 2, 1, 2};
    S->ClassProgress[0].SecondTalent = S->ClassProgress[0].ThirdTalent = EFableTalentPath::First;
    S->ClassProgress[1].Ranks = {2, 1, 2, 1};
    S->ClassProgress[1].SecondTalent = EFableTalentPath::Second;
    S->ClassProgress[1].ThirdTalent = EFableTalentPath::First;
    const FString Protected = ProtectedState(S);
    const int32 BeforeGold = S->Gold, BeforePoints = S->GetAvailableSkillPoints(),
                Refund = S->GetCurrentClassRefundableSkillPoints();
    TestEqual(TEXT("Refund counts only current-class paid ranks"), Refund, 5);
    F.Reset(EFableClass::Mage, S->ClassBuildRevision);
    F.Reset(S->HeroClass, S->ClassBuildRevision + 1);
    TestTrue(TEXT("Stale class or revision cannot reset or charge"),
             S->Gold == BeforeGold && S->GetCurrentClassRefundableSkillPoints() == Refund);
    S->Gold = 49;
    F.Reset(S->HeroClass, S->ClassBuildRevision);
    TestEqual(TEXT("Insufficient gold is not partially charged"), S->Gold, 49);
    S->Gold = BeforeGold;
    F.Hero->SetActorLocation(FableRPG::BoardLocation() + FVector(331, 0, 110));
    F.Reset(S->HeroClass, S->ClassBuildRevision);
    TestEqual(TEXT("Remote reset does not charge"), S->Gold, BeforeGold);
    F.Hero->SetActorLocation(FableRPG::BoardLocation() + FVector(0, -100, 110));
    const int32 Revision = S->ClassBuildRevision;
    F.Reset(S->HeroClass, Revision);
    TestTrue(TEXT("Successful reset clears only current ranks and choices"),
             S->ClassProgress[0].Ranks == TArray<int32>({1, 1, 1, 0}) &&
                 S->GetTalentChoice(2) == EFableTalentPath::None && S->GetTalentChoice(3) == EFableTalentPath::None);
    TestEqual(TEXT("Reset charges exactly fifty gold"), S->Gold, BeforeGold - 50);
    TestEqual(TEXT("Current-class skill investment is refunded"), S->GetAvailableSkillPoints(), BeforePoints + Refund);
    TestEqual(TEXT("Class tiers, trials, other builds, equipment, materials and story are preserved"),
              ProtectedState(S), Protected);
    TestTrue(TEXT("Revision invalidates the previous confirmation"), S->ClassBuildRevision != Revision);
    F.Reset(S->HeroClass, Revision);
    F.Reset(S->HeroClass, S->ClassBuildRevision);
    TestEqual(TEXT("Stale or empty reset never charges again"), S->Gold, BeforeGold - 50);
    S->ClassProgress[0].Ranks[0] = 2;
    const int32 Current = S->Gold;
    F.Reset(S->HeroClass, S->ClassBuildRevision);
    TestTrue(TEXT("Skill-only investment remains resettable"),
             S->ClassProgress[0].Ranks[0] == 1 && S->Gold == Current - 50);
    S->HeroClass = EFableClass::Mage;
    S->EquippedWeaponId = 2;
    TestTrue(TEXT("Switching class reveals untouched independent choices"),
             S->GetTalentChoice(2) == EFableTalentPath::Second && S->GetTalentChoice(3) == EFableTalentPath::First);
    TestTrue(TEXT("Fixture never enables persistence"), FFableTalentTestAccess::MemoryOnly(S));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTalentMemoryTest, "Fablewood.Talents.ThreeClassChoicesAndFullMemoryRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 특성 메모리 저장 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableTalentMemoryTest::RunTest(const FString& Parameters)
{
    // 직업·스킬·특성 빌드에 필요한 값 준비
    UFableSaveGame* P = TalentProfile();
    P->Level = 10;
    P->Experience = 21;
    P->ForestQuest = EFableQuestStage::Completed;
    P->QuestKills = 3;
    P->DungeonQuest = P->MoonwellQuest = EFableQuestStage::Completed;
    P->DungeonCompletions = 3;
    P->MoonwellCompletions = 2;
    P->StoryChapter = 5;
    P->StoryObjectMask = 31;
    P->StorySealMask = 7;
    P->Essence = 19;
    P->RootCores = 2;
    P->MoonCrystals = 4;

    // 직업·스킬·특성 빌드 순회
    for (int32 I = 0; I < 3; ++I)
    {
        FFableClassProgress& C = P->ClassProgress[I];
        GrantTier(C, 3);
        C.Ranks = {2, 1, 1, 1};
        C.SecondTalent = I == 1 ? EFableTalentPath::Second : EFableTalentPath::First;
        C.ThirdTalent = I == 0 ? EFableTalentPath::First : EFableTalentPath::Second;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableItem Stored;
    Stored.Id = 5;
    Stored.Rarity = 2;
    Stored.Power = 48;
    Stored.Upgrade = 2;
    Stored.Durability = 17;
    P->StashItems.Add(Stored);
    P->NextItemId = 6;
    P->Inventory[0].Durability = 0;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Complete v8 fixture obeys production validation"), P->IsValidProfile()))
    {
        return false;
    }

    TArray<uint8> Bytes;

    // JSON 계약과 검증 기록 조건 검사
    if (!TestTrue(TEXT("Serialize to memory without disk slots"), UGameplayStatics::SaveGameToMemory(P, Bytes)))
    {
        return false;
    }

    UFableSaveGame* R = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Restore v8 bytes"), R))
    {
        return false;
    }

    // 기대값과 상태 보존 검증 처리
    TestTrue(TEXT("Restored profile validates without filling choices"), R->MigrateAndValidate());
    TestEqual(TEXT("Stored v8 migrates to current schema"), R->Version, FableProfileSchema::CurrentVersion);

    // 직업·스킬·특성 빌드 순회
    for (int32 I = 0; I < 3; ++I)
    {
        TestEqual(TEXT("Independent choices, paid ranks and trials are exact"), ProgressSignature(R->ClassProgress[I]),
                  ProgressSignature(P->ClassProgress[I]));
    }

    // 재화와 작업 비용 처리
    TestTrue(TEXT("Persistent progression/resources/receipts survive"),
             R->Level == P->Level && R->Experience == 21 && R->Gold == 237 && R->Ore == 17 && R->Essence == 19 &&
                 R->RootCores == 2 && R->MoonCrystals == 4 && R->DungeonCompletions == 3 &&
                 R->MoonwellCompletions == 2 && R->StoryChapter == 5 && R->StoryObjectMask == 31 &&
                 R->StorySealMask == 7);

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 I = 0; I < P->Inventory.Num(); ++I)
    {
        TestEqual(TEXT("Inventory including broken durability is exact"),
                  FableRPG::ItemPersistentSignature(R->Inventory[I]),
                  FableRPG::ItemPersistentSignature(P->Inventory[I]));
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    TestEqual(TEXT("Stashed metadata and partial durability are exact"),
              FableRPG::ItemPersistentSignature(R->StashItems[0]), FableRPG::ItemPersistentSignature(P->StashItems[0]));
    AddInfo(TEXT(
        "Native fixtures cover schema, authority, independent selections, reset guards/costs and memory serialization. Real UI input, two-process replication and combat effects are validated separately."));

    return true;
}
#endif
