#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FableRPGTypes.h"
#include "FablePlayerState.generated.h"

class UFableSaveGame;
struct FFableBackendSession;

UCLASS()
class FABLEWOOD_API AFablePlayerState : public APlayerState
{
    GENERATED_BODY()
public:
    AFablePlayerState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableClass HeroClass = EFableClass::Warrior;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<FFableClassProgress> ClassProgress;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 DungeonCompletions = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 MoonwellCompletions = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableQuestStage MoonwellQuest = EFableQuestStage::Available;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Level = 1;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Experience = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Gold = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Ore = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 Essence = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 RootCores = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 MoonCrystals = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<FFableItem> StashItems;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 HealthPotions = 3;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 ManaPotions = 2;
    UPROPERTY(Replicated, BlueprintReadOnly) TArray<FFableItem> Inventory;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EquippedWeaponId = 1;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 EquippedArmorId = 4;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableQuestStage ForestQuest = EFableQuestStage::Available;
    UPROPERTY(Replicated, BlueprintReadOnly) EFableQuestStage DungeonQuest = EFableQuestStage::Available;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 QuestKills = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) FString LastNotice;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 NoticeSerial = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 StoryChapter = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 StoryObjectMask = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) int32 StorySealMask = 0;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bProfileLoaded = false;
    UPROPERTY(Replicated, BlueprintReadOnly) bool bLastSaveSucceeded = true;

    void InitializeProfile(bool bHost, const FString& ProfilePrefix = TEXT(""), EFableClass InitialClass = EFableClass::Warrior);
    bool SaveProfile();
    void InitializeBackendProfile(const FString& Ticket);
    void ReleaseBackendProfile();
    FString GetBackendPlayerId() const { return BackendPlayerId; }
    bool IsBackendEnabled() const { return bBackendEnabled; }
    bool IsBackendReady() const { return bBackendReady && bProfileLoaded && !bStorageBlocked; }
    bool IsBackendSavePending() const { return bBackendSavePending; }
    int64 GetBackendRevision() const { return BackendRevision; }
    int64 GetBackendAcknowledgedRevision() const { return BackendAcknowledgedRevision; }
    FString GetBackendAcknowledgedProfileHash() const { return BackendAcknowledgedProfileHash; }

    UPROPERTY(Replicated, BlueprintReadOnly) int32 ClassBuildRevision = 0;
    EFableTalentPath GetTalentChoice(int32 TargetTier) const;
    int32 GetTalentRank(EFableTalentPath Path) const;
    FString GetTalentSignature() const;
    FString GetTalentSelectBlockReason(int32 TargetTier, EFableTalentPath Path) const;
    FString GetBuildResetBlockReason() const;
    int32 GetCurrentClassRefundableSkillPoints() const;
    int32 GetBuildResetGoldCost() const { return 50; }
    void SelectTalent(int32 TargetTier, EFableTalentPath Path);
    void ResetCurrentClassBuild(EFableClass ExpectedClass, int32 ExpectedRevision);
    int32 GetSkillRank(int32 Slot) const;
    bool IsSkillUnlocked(int32 Slot) const;
    int32 GetAdvancementTier() const;
    FString GetAdvancedClassName() const;
    int32 GetAvailableSkillPoints() const;
    bool CanAdvanceClass() const;
    FString GetAdvancementRequirementText() const;
    void UpgradeSkill(int32 Slot);
    void AdvanceClass();
    void SelectClass(EFableClass NewClass);
    void InteractStory();
    void RecordStoryObject(int32 Index);
    void RecordStorySeal(int32 Index);
    bool IsStoryReady() const;
    bool CanUseStoryObject(int32 Index) const;
    FString GetStoryTitle() const;
    FString GetStoryDescription() const;
    FString GetStoryObjective() const;
    FString GetStoryRewardText() const;
    bool GetStoryTarget(FVector& OutLocation, FString& OutLabel) const;
    void InteractQuest();
    void InteractQuest(EFableQuestId Quest);
    FString GetQuestTitle(EFableQuestId Quest) const;
    FString GetQuestDescription(EFableQuestId Quest) const;
    EFableQuestStage GetQuestStage(EFableQuestId Quest) const;
    bool CanInteractQuest(EFableQuestId Quest) const;
    bool IsRegionEligible(EFableRegion Region) const;
    void RecordRegionKill(EFableRegion Region, bool bBoss, EFableDifficulty Difficulty = EFableDifficulty::Normal);
    int32 GetAdvancementTrialProgress(int32 TargetTier) const;
    bool IsAdvancementLegacyGranted(int32 TargetTier) const;
    FFableTrialCastContext CreateTrialCastContext(int32 Slot);
    void RecordTrialSkillHit(const FFableTrialCastContext& Context, bool bAppliedSlow = false);
    void EquipItem(int32 ItemId);
    void EnhanceItem(int32 ItemId);
    void BuyShopItem(EFableShopOffer Offer);
    void SellItem(int32 ItemId);
    void UsePotion(bool bMana);
    int32 GetSellPrice(int32 ItemId) const;
    float GetPotionCooldownRemaining() const;
    void RecordKill(bool bBoss);
    void SetNotice(const FString& Message);
    bool IsDungeonEligible() const;
    float GetAttackPower() const;
    float GetDefense() const;
    float GetMaxHealthBonus() const;
    float GetManaRegenRate() const;
    bool GetEquipmentPreview(int32 ItemId, FFableEquipmentStats& Before, FFableEquipmentStats& After) const;
    int32 GetEquippedSetCount(EFableEquipmentSet SetId, int32 ReplacementItemId = INDEX_NONE) const;
    int32 GetExperienceRequired() const { return Level < 20 ? Level * 100 : 0; }
    int32 GetUpgradeGoldCost(int32 ItemId) const;
    int32 GetUpgradeOreCost(int32 ItemId) const;
    const FFableItem* FindItem(int32 ItemId) const;
    const FFableItem* FindStashItem(int32 ItemId) const;
    FFableResourceCost GetCraftCost(int32 Rarity) const;
    FFableResourceCost GetDismantleYield(int32 ItemId) const;
    FFableResourceCost GetRerollCost(int32 ItemId) const;
    FFableResourceCost GetRepairCost(int32 ItemId) const;
    FString GetCraftBlockReason(int32 Rarity, EFableEquipmentSlot Slot) const;
    FString GetDismantleBlockReason(int32 ItemId) const;
    FString GetRerollBlockReason(int32 ItemId, int32 AffixIndex) const;
    FString GetRepairBlockReason(int32 ItemId) const;
    FString GetDepositBlockReason(int32 ItemId) const;
    FString GetWithdrawBlockReason(int32 ItemId) const;
    void CraftItem(int32 Rarity, EFableEquipmentSlot Slot);
    void DismantleItem(int32 ItemId);
    void RerollAffix(int32 ItemId, int32 AffixIndex);
    void RepairItem(int32 ItemId);
    void DepositItem(int32 ItemId);
    void WithdrawItem(int32 ItemId);
    void WearEquipment(EFableEquipmentSlot Slot, int32 Amount = 1);
    FString GetProfileSlotForValidation() const { return ProfileSlot; }
private:
    friend struct FFableBackendSession;
    UPROPERTY(Replicated) FString BackendPlayerId;
    UPROPERTY(Replicated) bool bBackendEnabled = false;
    UPROPERTY(Replicated) bool bBackendReady = false;
    UPROPERTY(Replicated) bool bBackendSavePending = false;
    UPROPERTY(Replicated) int64 BackendRevision = 0;
    UPROPERTY(Replicated) int64 BackendAcknowledgedRevision = 0;
    UPROPERTY(Replicated) FString BackendAcknowledgedProfileHash;
    bool bBackendLoading = false;
    bool bBackendClosing = false;
    TSharedPtr<FFableBackendSession> BackendSession;
    UFableSaveGame* MakeProfileSnapshot();
    void ApplyProfileSnapshot(const UFableSaveGame* Data);
    bool QueueBackendSnapshot(UFableSaveGame* Data);

#if WITH_DEV_AUTOMATION_TESTS
    friend struct FFableWorkshopTestAccess;
    friend struct FFableStoryTestAccess;
    friend struct FFableTalentTestAccess;
#endif
    UPROPERTY(Replicated) float NextPotionServerTime = 0.0f;
    FString ProfileSlot;
    int32 NextItemId = 5;
    bool bPersistenceEnabled = true;
    bool bStorageBlocked = false;
    double LastMutationTime = -100.0;
    double NextWeaponWearTime = -100.0;
    double NextArmorWearTime = -100.0;
    FString ClassBuildBlockReason() const;
    void BumpClassBuildRevision();
    bool CommitStoryReward(int32 ExpectedChapter);
    bool CanRecordStoryAt(EFableRegion Region, const FVector& Location) const;
    bool BeginWorkshopMutation();
    bool CanAfford(const FFableResourceCost& Cost) const;
    void SpendResources(const FFableResourceCost& Cost);
    FString WorkshopDataBlockReason() const;
    uint32 TrialGeneration = 1;
    uint32 NextTrialCastId = 0;
    TSet<uint32> CreditedTrialCasts;
    bool AllowMutation();
    void InvalidateTrialCasts();
    void RefreshTrialReadiness();
    bool AreTrialRequirementsMet(int32 TargetTier, const FFableAdvancementTrial& Trial) const;
    const FFableAdvancementTrial* FindTrial(int32 TargetTier) const;
    FFableAdvancementTrial* FindMutableTrial(int32 TargetTier);
    void InteractAdvancementTrial(int32 TargetTier);
    bool AtTownService(const FVector& Location) const;
    void AddExperience(int32 Amount);
    bool AddItem(EFableEquipmentSlot Slot, EFableClass ItemClass, int32 Rarity, int32 NominalPower,
        EFableEquipmentSet SetId = EFableEquipmentSet::None, EFableDifficulty Difficulty = EFableDifficulty::Normal);
    FFableItem RollItem(EFableEquipmentSlot Slot, EFableClass ItemClass, int32 Rarity, int32 NominalPower,
        EFableEquipmentSet SetId, EFableDifficulty Difficulty);
    FFableEquipmentStats CalculateEquipmentStats(int32 WeaponId, int32 ArmorId, EFableClass ForClass) const;
    void InitializeStarterItems();
    void Changed(const FString& Notice);
};
