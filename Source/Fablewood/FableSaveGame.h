#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "FableRPGTypes.h"
#include "FableSaveGame.generated.h"

UCLASS()
class FABLEWOOD_API UFableSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UFableSaveGame() { ClassProgress.Init(FFableClassProgress(), 3); }
    // Zero detects older files whose Version property matched their class default and was omitted.
    UPROPERTY(SaveGame) int32 Version = 0;
    UPROPERTY(SaveGame) TArray<FFableClassProgress> ClassProgress;
    UPROPERTY(SaveGame) int32 DungeonCompletions = 0;
    UPROPERTY(SaveGame) int32 MoonwellCompletions = 0;
    UPROPERTY(SaveGame) EFableQuestStage MoonwellQuest = EFableQuestStage::Available;
    UPROPERTY(SaveGame) EFableClass HeroClass = EFableClass::Warrior;
    UPROPERTY(SaveGame) int32 Level = 1;
    UPROPERTY(SaveGame) int32 Experience = 0;
    UPROPERTY(SaveGame) int32 Gold = 0;
    UPROPERTY(SaveGame) int32 Ore = 0;
    UPROPERTY(SaveGame) int32 Essence = 0;
    UPROPERTY(SaveGame) int32 RootCores = 0;
    UPROPERTY(SaveGame) int32 MoonCrystals = 0;
    UPROPERTY(SaveGame) TArray<FFableItem> StashItems;
    UPROPERTY(SaveGame) int32 HealthPotions = 3;
    UPROPERTY(SaveGame) int32 ManaPotions = 2;
    UPROPERTY(SaveGame) int64 PotionCooldownEndUtc = 0;
    UPROPERTY(SaveGame) TArray<FFableItem> Inventory;
    UPROPERTY(SaveGame) int32 EquippedWeaponId = 1;
    UPROPERTY(SaveGame) int32 EquippedArmorId = 4;
    UPROPERTY(SaveGame) EFableQuestStage ForestQuest = EFableQuestStage::Available;
    UPROPERTY(SaveGame) EFableQuestStage DungeonQuest = EFableQuestStage::Available;
    UPROPERTY(SaveGame) int32 QuestKills = 0;
    UPROPERTY(SaveGame) int32 NextItemId = 5;
    UPROPERTY(SaveGame) int32 StoryChapter = 0;
    UPROPERTY(SaveGame) int32 StoryObjectMask = 0;
    UPROPERTY(SaveGame) int32 StorySealMask = 0;

    void NormalizeLoadedVersion();
    // Validates the original schema before upgrading only this in-memory object.
    bool MigrateAndValidate();
    bool IsValidProfile() const;
};
