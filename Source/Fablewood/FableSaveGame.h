#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FableRPGTypes.h"
#include "FableProfileSchema.h"
#include "FableSaveGame.generated.h"

/** 성장·퀘스트·재화·장비를 보존하고 저장 버전을 검사하는 프로필. */
UCLASS()
class FABLEWOOD_API UFableSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    UFableSaveGame()
    {
        ClassProgress.Init(FFableClassProgress(), 3);
    }
    // Zero detects older files whose Version property matched their class default and was omitted.
    UPROPERTY(SaveGame)
    int32 Version = 0;

    /** 저장하는 값은 서버 catalog의 논리 귀환지 세 필드뿐이다. */
    UPROPERTY(SaveGame)
    EFableTownId LastSafeTownId = EFableTownId::Invalid;

    UPROPERTY(SaveGame)
    FName LastSafeAnchorId = NAME_None;

    UPROPERTY(SaveGame)
    int32 TownCatalogVersion = 0;

    /** 좌표나 접속 세대 없이 저장된 논리 참조를 복원한다. */
    FFableTownAnchorRef GetLastSafeTownAnchor() const;
    UPROPERTY(SaveGame)
    TArray<FFableClassProgress> ClassProgress;
    UPROPERTY(SaveGame)
    int32 DungeonCompletions = 0;
    UPROPERTY(SaveGame)
    int32 MoonwellCompletions = 0;
    UPROPERTY(SaveGame)
    EFableQuestStage MoonwellQuest = EFableQuestStage::Available;
    UPROPERTY(SaveGame)
    EFableClass HeroClass = EFableClass::Warrior;
    UPROPERTY(SaveGame)
    int32 Level = 1;
    UPROPERTY(SaveGame)
    int32 Experience = 0;
    UPROPERTY(SaveGame)
    int32 Gold = 0;
    UPROPERTY(SaveGame)
    int32 Ore = 0;
    UPROPERTY(SaveGame)
    int32 Essence = 0;
    UPROPERTY(SaveGame)
    int32 RootCores = 0;
    UPROPERTY(SaveGame)
    int32 MoonCrystals = 0;
    UPROPERTY(SaveGame)
    TArray<FFableItem> StashItems;
    UPROPERTY(SaveGame)
    int32 HealthPotions = 3;
    UPROPERTY(SaveGame)
    int32 ManaPotions = 2;
    UPROPERTY(SaveGame)
    int64 PotionCooldownEndUtc = 0;
    UPROPERTY(SaveGame)
    TArray<FFableItem> Inventory;
    UPROPERTY(SaveGame)
    int32 EquippedWeaponId = 1;
    UPROPERTY(SaveGame)
    int32 EquippedArmorId = 4;
    UPROPERTY(SaveGame)
    EFableQuestStage ForestQuest = EFableQuestStage::Available;
    UPROPERTY(SaveGame)
    EFableQuestStage DungeonQuest = EFableQuestStage::Available;
    UPROPERTY(SaveGame)
    int32 QuestKills = 0;
    UPROPERTY(SaveGame)
    int32 NextItemId = 5;
    UPROPERTY(SaveGame)
    int32 StoryChapter = 0;
    UPROPERTY(SaveGame)
    int32 StoryObjectMask = 0;
    UPROPERTY(SaveGame)
    int32 StorySealMask = 0;

    /** 불러온 저장 버전에 필요한 기본값을 보정한다. */
    void NormalizeLoadedVersion();

    // Validates the original schema before upgrading only this in-memory object.

    /** 지원하는 이전 저장 형식을 변환한 뒤 전체 프로필의 유효성을 검사한다. */
    bool MigrateAndValidate(EFableTownBootstrapPolicy Policy = EFableTownBootstrapPolicy::LegacyVillageV1);

    /** 프로필 유효성 여부를 확인한다. */
    bool IsValidProfile() const;
};
