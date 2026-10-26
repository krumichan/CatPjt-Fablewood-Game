#include "FablePlayerState.h"
#include "FableSaveGame.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "FableGameMode.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

// Isolated native fixtures only: no profile initialization, save slots, file I/O or running game mutation.

/** 이야기 회귀 검증에 필요한 제한된 내부 접근 도우미. */
struct FFableStoryTestAccess
{
    /** 격리 검증에 필요한 초기 프로필·장비·성장 상태를 준비한다. */
    static void Prepare(AFablePlayerState* State)
    {
        State->bPersistenceEnabled = false;
        State->ProfileSlot.Empty();
        State->bProfileLoaded = true;
        State->bStorageBlocked = false;
    }

    /** 실제 이야기 보상 확정 경로를 호출해 예상한 장의 진행 결과를 검증한다. */
    static bool Commit(AFablePlayerState* State, int32 ExpectedChapter)
    {
        return State->CommitStoryReward(ExpectedChapter);
    }

    /** 저장 불가 경로 검증을 위해 영구 저장을 차단한다. */
    static void BlockStorage(AFablePlayerState* State, bool bBlocked)
    {
        State->bStorageBlocked = bBlocked;
    }

    /** 메모리 전용 여부를 확인한다. */
    static bool IsMemoryOnly(const AFablePlayerState* State)
    {
        return !State->bPersistenceEnabled && State->ProfileSlot.IsEmpty();
    }
};
namespace
{
    /** 이야기 프로필에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    UFableSaveGame* StoryProfile(int32 Version = 7)
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
        FFableItem Reward;
        Reward.Id = 5;
        Reward.Rarity = 1;
        Reward.Power = 32;
        Reward.Upgrade = 2;
        Profile->Inventory.Add(Reward);
        Profile->NextItemId = 6;

        // 아이템·장비 옵션·인벤토리와 창고 처리
        if (Version >= 6)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            FFableItem Stored;
            Stored.Id = 6;
            Stored.Rarity = 2;
            Stored.Power = 48;
            Stored.HeroClass = EFableClass::Archer;
            Stored.Durability = 27;
            Profile->StashItems.Add(Stored);
            Profile->NextItemId = 7;
            Profile->Essence = 17;
            Profile->RootCores = 3;
            Profile->MoonCrystals = 4;
            Profile->Inventory[4].Durability = 63;
        }

        return Profile;
    }

    /** 레벨과 현재 경험치를 누적 획득 경험치로 환산한다. */
    int32 TotalEarnedExperience(const AFablePlayerState* State)
    {
        return State->Level * (State->Level - 1) * 50 + State->Experience;
    }

    /** 실제 게임 상태를 격리해 구성하고 종료 시 해제하는 검증 월드. */
    struct FStoryTestWorld
    {
        UWorld* World = nullptr;
        TWeakObjectPtr<APlayerController> RegisteredController;

        /** 이야기 회귀 검증용 월드와 플레이어를 만들고 이전 실행 문맥을 보존한다. */
        FStoryTestWorld()
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
        ~FStoryTestWorld()
        {
            if (World)
            {
                // 이 검증이 명시적으로 등록한 소유 연결만 정리한다.
                if (APlayerController* Controller = RegisteredController.Get())
                {
                    World->RemoveController(Controller);
                }

                World->DestroyWorld(false);
            }
        }

        /** 생성 상태에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
        AFablePlayerState* MakeState()
        {
            AFablePlayerState* State = World ? World->SpawnActor<AFablePlayerState>() : nullptr;

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (State)
            {
                FFableStoryTestAccess::Prepare(State);
                State->Inventory = StoryProfile()->Inventory;
            }

            return State;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableStoryMigrationTest, "Fablewood.Story.V7MigrationAndStrictRejection",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 이야기 버전 이관 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableStoryMigrationTest::RunTest(const FString& Parameters)
{
    // 퀘스트와 이야기 진행 순회
    for (int32 Version = 0; Version <= 6; ++Version)
    {
        for (int32 History = 0; History < 4; ++History)
        {
            // 이 단계에서 이야기 버전 이관 동작과 저장 상태의 회귀 검증을 수행한다.
            if (History == 3 && Version < 4)
            {
                continue;
            }

            UFableSaveGame* Profile = StoryProfile(Version);

            // 퀘스트와 이야기 진행 처리
            if (History >= 1)
            {
                Profile->ForestQuest = EFableQuestStage::Completed;
                Profile->QuestKills = 3;
                Profile->Level = 2;
            }

            // 퀘스트와 이야기 진행 처리
            if (History >= 2)
            {
                Profile->DungeonQuest = EFableQuestStage::Completed;
                Profile->DungeonCompletions = 1;
                Profile->Level = 4;
            }

            // 퀘스트와 이야기 진행 처리
            if (History >= 3)
            {
                Profile->MoonwellQuest = EFableQuestStage::Completed;
                Profile->MoonwellCompletions = 1;
                Profile->Level = 6;
            }

            TArray<FString> GearBefore;

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (const FFableItem& Item : Profile->Inventory)
            {
                GearBefore.Add(FableRPG::GearIdentitySignature(Item));
            }

            TArray<FString> StashBefore;

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (const FFableItem& Item : Profile->StashItems)
            {
                StashBefore.Add(FableRPG::ItemPersistentSignature(Item));
            }

            // 기대값과 상태 보존 검증 처리
            if (!TestTrue(FString::Printf(TEXT("Historical version %d history %d migrates"), Version, History),
                          Profile->MigrateAndValidate()))
            {
                continue;
            }

            // 퀘스트와 이야기 진행에 필요한 값 준비
            const int32 ExpectedChapter = History == 0 ? 0 : History + 2;
            TestEqual(TEXT("Migration reaches current schema"), Profile->Version, FableProfileSchema::CurrentVersion);
            TestEqual(TEXT("Only existing completed chapters are skipped"), Profile->StoryChapter, ExpectedChapter);
            TestEqual(TEXT("Only prerequisite object bits are filled"), Profile->StoryObjectMask,
                      ExpectedChapter >= 4   ? 31
                      : ExpectedChapter >= 3 ? 15
                                             : 0);
            TestEqual(TEXT("Only completed moon history skips seals"), Profile->StorySealMask,
                      ExpectedChapter == 5 ? 7 : 0);
            TestEqual(TEXT("Migration grants no story gold"), Profile->Gold, 137);
            TestEqual(TEXT("Migration grants no story ore"), Profile->Ore, 9);
            TestEqual(TEXT("Migration grants no story experience"), Profile->Experience, 0);

            // 아이템·장비 옵션·인벤토리와 창고 순회
            for (int32 Index = 0; Index < GearBefore.Num(); ++Index)
            {
                TestEqual(TEXT("All original gear identities survive"),
                          FableRPG::GearIdentitySignature(Profile->Inventory[Index]), GearBefore[Index]);
            }

            // 아이템·장비 옵션·인벤토리와 창고 처리
            if (Version == 6)
            {
                TestEqual(TEXT("v6 worn gear is not repaired by story migration"), Profile->Inventory[4].Durability,
                          63);
                TestEqual(TEXT("v6 stash remains the same size"), Profile->StashItems.Num(), StashBefore.Num());
                for (int32 Index = 0; Index < StashBefore.Num(); ++Index)
                {
                    TestEqual(TEXT("v6 stash metadata survives exactly"),
                              FableRPG::ItemPersistentSignature(Profile->StashItems[Index]), StashBefore[Index]);
                }
                TestTrue(TEXT("Workshop materials survive story migration"),
                         Profile->Essence == 17 && Profile->RootCores == 3 && Profile->MoonCrystals == 4);
            }

            // 재화와 작업 비용 처리
            TestTrue(TEXT("Repeated migration is valid and grants nothing"), Profile->MigrateAndValidate());
            TestTrue(TEXT("Repeated migration retains receipt and balance"),
                     Profile->StoryChapter == ExpectedChapter && Profile->Gold == 137 && Profile->Ore == 9);
        }
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    UFableSaveGame* Repeat = StoryProfile(6);
    Repeat->ForestQuest = EFableQuestStage::Completed;
    Repeat->QuestKills = 3;
    Repeat->Level = 6;
    Repeat->DungeonCompletions = 2;
    Repeat->MoonwellCompletions = 2;
    Repeat->DungeonQuest = Repeat->MoonwellQuest = EFableQuestStage::Active;
    TestTrue(TEXT("Repeated active quests retain completed story history"),
             Repeat->MigrateAndValidate() && Repeat->StoryChapter == 5);

    const auto Reject = [&](const TCHAR* Label, TFunctionRef<void(UFableSaveGame*)> Corrupt)
    {
        UFableSaveGame* Profile = StoryProfile();
        Corrupt(Profile);
        TestFalse(Label, Profile->MigrateAndValidate());
    };
    Reject(TEXT("Unknown version 10 is preserved and rejected"),
           [](UFableSaveGame* P)
           {
               P->Version = FableProfileSchema::CurrentVersion + 1;
           });
    Reject(TEXT("Negative chapter rejected"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = -1;
           });
    Reject(TEXT("Chapter beyond the final receipt rejected"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 6;
           });
    Reject(TEXT("Unknown object bits rejected"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 1;
               P->StoryObjectMask = 32;
           });
    Reject(TEXT("Negative object mask rejected"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 1;
               P->StoryObjectMask = -1;
           });
    Reject(TEXT("Unknown seal bits rejected"),
           [](UFableSaveGame* P)
           {
               P->StorySealMask = 8;
           });
    Reject(TEXT("Unstarted chapter cannot claim collected objects"),
           [](UFableSaveGame* P)
           {
               P->StoryObjectMask = 1;
           });
    Reject(TEXT("Early chapter cannot claim a root rune"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 1;
               P->StoryObjectMask = 16;
           });
    Reject(TEXT("Early chapter cannot claim a moon seal"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 1;
               P->StorySealMask = 1;
           });
    Reject(TEXT("Chapter two requires all first chapter objects"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 2;
               P->StoryObjectMask = 7;
           });
    Reject(TEXT("Chapter three requires forest completion"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 3;
               P->StoryObjectMask = 15;
           });
    Reject(TEXT("Chapter four requires root completion"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 4;
               P->StoryObjectMask = 31;
               P->ForestQuest = EFableQuestStage::Completed;
           });
    Reject(TEXT("Final receipt requires moon completion"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 5;
               P->StoryObjectMask = 31;
               P->StorySealMask = 7;
               P->ForestQuest = EFableQuestStage::Completed;
               P->DungeonCompletions = 1;
           });
    Reject(TEXT("Final receipt requires all seal observations"),
           [](UFableSaveGame* P)
           {
               P->StoryChapter = 5;
               P->StoryObjectMask = 31;
               P->StorySealMask = 3;
               P->ForestQuest = EFableQuestStage::Completed;
               P->DungeonCompletions = P->MoonwellCompletions = 1;
           });
    AddInfo(TEXT(
        "Validated version 0..6 migration, completed/repeated history, no retroactive reward, v6 gear/material preservation, and strict v7 story masks."));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableStoryRewardTest, "Fablewood.Story.MonotonicRewardsAndMemoryRoundTrip",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 이야기 보상 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableStoryRewardTest::RunTest(const FString& Parameters)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    FStoryTestWorld Fixture;
    AFablePlayerState* State = Fixture.MakeState();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Memory-only story PlayerState"), State))
    {
        return false;
    }

    // 재화와 작업 비용 처리
    TestTrue(TEXT("No persistence or profile slot in native fixture"), FFableStoryTestAccess::IsMemoryOnly(State));
    TestFalse(TEXT("An unexpected chapter cannot claim a reward"), FFableStoryTestAccess::Commit(State, 1));
    const int32 ExpectedGold[] = {20, 60, 110, 180, 280};
    const int32 ExpectedOre[] = {0, 3, 6, 10, 15};
    const int32 ExpectedXP[] = {20, 80, 140, 220, 320};

    // 퀘스트와 이야기 진행 순회
    for (int32 Chapter = 0; Chapter < 5; ++Chapter)
    {
        // 기대값과 상태 보존 검증 처리
        if (Chapter > 0)
        {
            TestFalse(TEXT("Missing prerequisites prevent the next reward"),
                      FFableStoryTestAccess::Commit(State, Chapter));
        }

        // 퀘스트와 이야기 진행 처리
        if (Chapter == 1)
        {
            State->StoryObjectMask = 15;
        }

        // 퀘스트와 이야기 진행 처리
        if (Chapter == 2)
        {
            State->ForestQuest = EFableQuestStage::Completed;
        }

        // 퀘스트와 이야기 진행 처리
        if (Chapter == 3)
        {
            State->StoryObjectMask = 31;
            State->DungeonCompletions = 1;
        }

        // 퀘스트와 이야기 진행 처리
        if (Chapter == 4)
        {
            State->StorySealMask = 7;
            State->MoonwellCompletions = 1;
        }

        // 재화와 작업 비용 처리
        TestTrue(TEXT("A fully satisfied chapter receives its one reward"),
                 FFableStoryTestAccess::Commit(State, Chapter));
        TestFalse(TEXT("The exact same chapter receipt cannot pay twice"),
                  FFableStoryTestAccess::Commit(State, Chapter));
        TestEqual(TEXT("Chapter advances exactly once"), State->StoryChapter, Chapter + 1);
        TestEqual(TEXT("Exact cumulative story gold"), State->Gold, ExpectedGold[Chapter]);
        TestEqual(TEXT("Exact cumulative story ore"), State->Ore, ExpectedOre[Chapter]);
        TestEqual(TEXT("Exact cumulative story experience through level-ups"), TotalEarnedExperience(State),
                  ExpectedXP[Chapter]);
    }

    // 재화와 작업 비용 처리
    TestFalse(TEXT("Final chapter cannot pay again"), FFableStoryTestAccess::Commit(State, 5));
    State->StoryChapter = 0;
    State->StoryObjectMask = State->StorySealMask = 0;
    State->Gold = 999995;
    State->Ore = 100000;
    TestTrue(TEXT("Currency caps do not prevent story receipt"), FFableStoryTestAccess::Commit(State, 0));
    TestTrue(TEXT("Currency caps are respected"), State->Gold == 1000000 && State->Ore == 100000);
    UFableSaveGame* Profile = StoryProfile();
    Profile->StoryChapter = 1;
    Profile->StoryObjectMask = 5;
    TArray<uint8> Bytes;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Partial collection serializes in memory"), UGameplayStatics::SaveGameToMemory(Profile, Bytes)))
    {
        return false;
    }

    UFableSaveGame* Restored = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Partial story restores from bytes"), Restored))
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    TestTrue(TEXT("Restored partial v7 story validates"), Restored->MigrateAndValidate());
    TestTrue(TEXT("Partial chapter/masks survive without filling missing objects"),
             Restored->StoryChapter == 1 && Restored->StoryObjectMask == 5 && Restored->StorySealMask == 0);
    TestEqual(TEXT("Partial save receives no reward"), Restored->Gold, 137);
    TestEqual(TEXT("Worn stashed item survives memory serialization"),
              FableRPG::ItemPersistentSignature(Restored->StashItems[0]),
              FableRPG::ItemPersistentSignature(Profile->StashItems[0]));
    AddInfo(TEXT(
        "Native memory fixtures cover chapter receipt idempotency, exact 320 XP/280 gold/15 ore, resource caps and partial collection serialization; real E input is a separate runtime test."));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableStoryAuthorityTest, "Fablewood.Story.AuthoritativeObjectsSealsAndNavigation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 이야기 서버 권한 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableStoryAuthorityTest::RunTest(const FString& Parameters)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    FStoryTestWorld Fixture;
    AFablePlayerState* State = Fixture.MakeState();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Memory story state"), State))
    {
        return false;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFableCharacter* Hero =
        Fixture.World->SpawnActor<AFableCharacter>(FVector(0, 0, 110), FRotator::ZeroRotator, Spawn);
    AFableGameState* Adventure = Fixture.World->SpawnActor<AFableGameState>();
    AFableGameMode* Mode = Fixture.World->SpawnActor<AFableGameMode>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!TestNotNull(TEXT("Test controller"), Controller) || !TestNotNull(TEXT("Test character"), Hero) ||
        !TestNotNull(TEXT("Test adventure state"), Adventure) || !TestNotNull(TEXT("Test game mode"), Mode))
    {
        return false;
    }

    // 현재 월드와 플레이어 참조 처리
    Fixture.World->CopyGameState(Mode, Adventure);
    Controller->SetPlayerState(State);
    Controller->Possess(Hero);
    State->SetOwner(Controller);
    Fixture.World->AddController(Controller);
    Fixture.RegisteredController = Controller;

    // 기대값과 상태 보존 검증 조건 검사
    if (!TestTrue(TEXT("Fixture controller possesses the expected character and profile"),
                  Controller->GetPawn() == Hero && Hero->GetPlayerState<AFablePlayerState>() == State))
    {
        return false;
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    const auto MoveHero = [&](const FVector& Location)
    {
        Hero->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    };
    State->StoryChapter = 1;
    MoveHero(FableRPG::StoryObjectLocation(0) + FVector(0, 0, 110));
    State->RecordStoryObject(-1);
    State->RecordStoryObject(5);
    State->RecordStoryObject(MAX_int32);
    TestEqual(TEXT("Invalid object indices cannot mutate progress"), State->StoryObjectMask, 0);
    State->bProfileLoaded = false;
    State->RecordStoryObject(0);
    TestEqual(TEXT("An unloaded profile cannot collect"), State->StoryObjectMask, 0);
    State->bProfileLoaded = true;
    FFableStoryTestAccess::BlockStorage(State, true);
    State->RecordStoryObject(0);
    TestEqual(TEXT("A blocked original profile cannot collect"), State->StoryObjectMask, 0);
    FFableStoryTestAccess::BlockStorage(State, false);
    State->SetRole(ROLE_SimulatedProxy);
    TestTrue(TEXT("Client display query remains available"), State->CanUseStoryObject(0));
    State->RecordStoryObject(0);
    TestEqual(TEXT("Client role cannot award object credit"), State->StoryObjectMask, 0);
    State->SetRole(ROLE_Authority);
    MoveHero(FableRPG::StoryObjectLocation(0) + FVector(331, 0, 110));
    State->RecordStoryObject(0);
    TestEqual(TEXT("331 cm is outside authoritative object range"), State->StoryObjectMask, 0);
    MoveHero(FableRPG::StoryObjectLocation(0) + FVector(330, 0, 300));
    State->RecordStoryObject(0);
    TestEqual(TEXT("300 cm vertical separation is rejected"), State->StoryObjectMask, 0);
    MoveHero(FableRPG::StoryObjectLocation(0) + FVector(330, 0, 299));
    State->RecordStoryObject(0);
    TestEqual(TEXT("Exact 330 cm range and valid height are accepted"), State->StoryObjectMask, 1);
    const int32 Serial = State->NoticeSerial;
    State->RecordStoryObject(0);
    TestTrue(TEXT("Duplicate object has no credit or new notification"),
             State->StoryObjectMask == 1 && State->NoticeSerial == Serial);
    State->SetNotice(TEXT("same notice"));
    const int32 FirstSerial = State->NoticeSerial;
    State->SetNotice(TEXT("same notice"));
    TestTrue(TEXT("Identical notice text still gets a new serial"), State->NoticeSerial == FirstSerial + 1);
    State->StoryChapter = 0;
    State->StoryObjectMask = 0;
    State->RecordStoryObject(0);
    TestEqual(TEXT("Object cannot be collected before the story starts"), State->StoryObjectMask, 0);
    // 루트 룬과 봉인 검증도 실제로 명시 참가한 사용자의 공개 진행만 참조한다.
    const FVector PreviousLocation = Hero->GetActorLocation();
    const EFableQuestStage PreviousForestQuest = State->ForestQuest;
    State->ForestQuest = EFableQuestStage::Active;
    MoveHero(FableRPG::BoardLocation() + FVector(0, -100, 110));
    const bool bJoinedField = Mode->JoinPublicField(Controller);
    State->ForestQuest = PreviousForestQuest;
    MoveHero(PreviousLocation);
    if (!TestTrue(TEXT("Story root and seal fixture explicitly joins the public field"), bJoinedField))
    {
        return false;
    }

    State->StoryChapter = 3;
    State->StoryObjectMask = 15;
    State->ForestQuest = EFableQuestStage::Completed;
    State->DungeonQuest = EFableQuestStage::Active;
    FVector Target;
    FString Label;
    State->GetStoryTarget(Target, Label);
    TestTrue(TEXT("Root target from village points to the entrance portal"), Target.Equals(FableRPG::PortalLocation()));
    State->RecordStoryObject(4);
    TestEqual(TEXT("A root object cannot be collected from the village"), State->StoryObjectMask, 15);
    MoveHero(FableRPG::StoryObjectLocation(4) + FVector(331, 0, 110));
    State->RecordStoryObject(4);
    TestEqual(TEXT("Root object uses the same authoritative range"), State->StoryObjectMask, 15);
    MoveHero(FableRPG::StoryObjectLocation(4) + FVector(330, 0, 110));
    State->RecordStoryObject(4);
    TestEqual(TEXT("Root rune credits once in its region"), State->StoryObjectMask, 31);
    State->DungeonQuest = EFableQuestStage::Ready;
    State->GetStoryTarget(Target, Label);
    TestTrue(TEXT("A ready root quest routes through its return portal"),
             Target.Equals(FableRPG::RegionReturnPortal(EFableRegion::RootRuins)));
    State->StoryChapter = 4;
    State->DungeonCompletions = 1;
    State->MoonwellQuest = EFableQuestStage::Active;
    Adventure->FieldProgress.ActiveRegion = EFableRegion::Moonwell;
    State->RecordStorySeal(0);
    TestEqual(TEXT("A moon seal cannot be observed from root ruins"), State->StorySealMask, 0);
    MoveHero(FableRPG::MoonSealLocation(0) + FVector(0, 0, 110));
    State->RecordStorySeal(0);
    TestEqual(TEXT("An uncleaned server seal cannot award credit"), State->StorySealMask, 0);
    Adventure->FieldProgress.MoonSealMask = 7;
    State->RecordStorySeal(-1);
    State->RecordStorySeal(3);
    State->RecordStorySeal(MAX_int32);
    TestEqual(TEXT("Invalid seal indices never change progress"), State->StorySealMask, 0);
    MoveHero(FableRPG::MoonSealLocation(0) + FVector(331, 0, 110));
    State->RecordStorySeal(0);
    TestEqual(TEXT("Moon seal range is independently enforced"), State->StorySealMask, 0);
    MoveHero(FableRPG::MoonSealLocation(0) + FVector(330, 0, 300));
    State->RecordStorySeal(0);
    TestEqual(TEXT("Moon seal height is independently enforced"), State->StorySealMask, 0);
    MoveHero(FableRPG::MoonSealLocation(0) + FVector(330, 0, 110));
    State->SetRole(ROLE_SimulatedProxy);
    State->RecordStorySeal(0);
    TestEqual(TEXT("Client role cannot award seal credit"), State->StorySealMask, 0);
    State->SetRole(ROLE_Authority);
    State->RecordStorySeal(0);
    State->RecordStorySeal(0);
    TestEqual(TEXT("A nearby completed seal credits once"), State->StorySealMask, 1);
    State->StoryChapter = 3;
    MoveHero(FableRPG::MoonSealLocation(1) + FVector(0, 0, 110));
    State->RecordStorySeal(1);
    TestEqual(TEXT("Seal credit requires the exact moon story chapter"), State->StorySealMask, 1);
    State->StoryChapter = 4;
    Adventure->FieldProgress.bPartyRecoveryPending = true;
    State->RecordStorySeal(1);
    TestEqual(TEXT("Recovery blocks new seal credit"), State->StorySealMask, 1);
    Adventure->FieldProgress.bPartyRecoveryPending = false;
    Hero->ReceiveHit(10000.0f);

    // 퀘스트와 이야기 진행 처리
    if (TestFalse(TEXT("Real damage makes the fixture hero inactive"), Hero->IsAlive()))
    {
        State->RecordStorySeal(1);
        TestEqual(TEXT("Inactive hero cannot inspect a seal"), State->StorySealMask, 1);
        State->StoryChapter = 1;
        State->StoryObjectMask = 0;
        MoveHero(FableRPG::StoryObjectLocation(2) + FVector(0, 0, 110));
        State->RecordStoryObject(2);
        TestEqual(TEXT("Inactive hero cannot collect a story object"), State->StoryObjectMask, 0);
    }

    // 퀘스트와 이야기 진행 처리
    State->StoryChapter = 4;
    State->StoryObjectMask = 31;
    State->StorySealMask = 7;
    State->MoonwellQuest = EFableQuestStage::Ready;
    MoveHero(FableRPG::MoonSealLocation(0) + FVector(0, 0, 110));
    State->GetStoryTarget(Target, Label);
    TestTrue(TEXT("Ready moon quest routes through its return portal"),
             Target.Equals(FableRPG::RegionReturnPortal(EFableRegion::Moonwell)));
    AddInfo(TEXT(
        "Actual RecordStoryObject/RecordStorySeal paths reject invalid index, unloaded/blocked/client states, wrong chapter, duplicates, wrong region, distance, height, uncleaned seal, party recovery and inactive hero. Fixtures only; no save slots or real game state were modified."));

    return true;
}
#endif
