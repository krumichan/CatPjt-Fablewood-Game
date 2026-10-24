#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "FableRPGTypes.h"
#include "FablePlayerState.generated.h"

class UFableSaveGame;
struct FFableBackendSession;

/** 서버가 관리하는 성장·퀘스트·재화·장비와 저장 상태. */
UCLASS()
class FABLEWOOD_API AFablePlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFablePlayerState();

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableClass HeroClass = EFableClass::Warrior;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<FFableClassProgress> ClassProgress;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 DungeonCompletions = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 MoonwellCompletions = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableQuestStage MoonwellQuest = EFableQuestStage::Available;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Level = 1;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Experience = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Gold = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Ore = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 Essence = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 RootCores = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 MoonCrystals = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<FFableItem> StashItems;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 HealthPotions = 3;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 ManaPotions = 2;
    UPROPERTY(Replicated, BlueprintReadOnly)
    TArray<FFableItem> Inventory;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 EquippedWeaponId = 1;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 EquippedArmorId = 4;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableQuestStage ForestQuest = EFableQuestStage::Available;
    UPROPERTY(Replicated, BlueprintReadOnly)
    EFableQuestStage DungeonQuest = EFableQuestStage::Available;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 QuestKills = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FString LastNotice;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 NoticeSerial = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 StoryChapter = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 StoryObjectMask = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 StorySealMask = 0;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bProfileLoaded = false;
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bLastSaveSucceeded = true;

    /** 선택한 직업과 저장 슬롯을 확인하고 기존 진행을 불러온다. */
    void InitializeProfile(bool bHost, const FString& ProfilePrefix = TEXT(""),
                           EFableClass InitialClass = EFableClass::Warrior);

    /** 저장 가능 상태를 확인하고 현재 진행의 영구 저장을 요청한다. */
    bool SaveProfile();

    /** 서버가 확인한 백엔드 접속 권한과 영구 프로필을 적용한다. */
    void InitializeBackendProfile(const FString& Ticket);

    /** 게임 서버가 보유한 프로필 접속 권한을 반환한다. */
    void ReleaseBackendProfile();

    /** 미완료 저장의 확인을 기다린 뒤 안전한 접속 종료를 요청한다. */
    void RequestBackendLeave(TFunction<void(bool, const FString&)> Completion);

    /** 백엔드 플레이어 식별자 값을 반환한다. */
    FString GetBackendPlayerId() const
    {
        return BackendPlayerId;
    }

    /** 백엔드 사용 가능 여부를 확인한다. */
    bool IsBackendEnabled() const
    {
        return bBackendEnabled;
    }

    /** 백엔드 준비 완료 여부를 확인한다. */
    bool IsBackendReady() const
    {
        return bBackendReady && bProfileLoaded && !bStorageBlocked && !bBackendClosing;
    }

    /** 백엔드 종료 중 여부를 확인한다. */
    bool IsBackendClosing() const
    {
        return bBackendClosing;
    }

    /** 백엔드 저장 대기 중 여부를 확인한다. */
    bool IsBackendSavePending() const
    {
        return bBackendSavePending;
    }

    /** 백엔드 프로필의 현재 리비전 값을 반환한다. */
    int64 GetBackendRevision() const
    {
        return BackendRevision;
    }

    /** 백엔드가 저장 완료를 확인한 리비전 값을 반환한다. */
    int64 GetBackendAcknowledgedRevision() const
    {
        return BackendAcknowledgedRevision;
    }

    /** 백엔드가 저장 완료를 확인한 프로필 해시 값을 반환한다. */
    FString GetBackendAcknowledgedProfileHash() const
    {
        return BackendAcknowledgedProfileHash;
    }

    UPROPERTY(Replicated, BlueprintReadOnly)
    int32 ClassBuildRevision = 0;

    /** 선택한 전직 특성 값을 반환한다. */
    EFableTalentPath GetTalentChoice(int32 TargetTier) const;

    /** 적용할 특성 단계 값을 반환한다. */
    int32 GetTalentRank(EFableTalentPath Path) const;

    /** 현재 특성 선택의 상태 서명 값을 반환한다. */
    FString GetTalentSignature() const;

    /** 특성 선택 거부 사유 값을 반환한다. */
    FString GetTalentSelectBlockReason(int32 TargetTier, EFableTalentPath Path) const;

    /** 직업 빌드 초기화 거부 사유 값을 반환한다. */
    FString GetBuildResetBlockReason() const;

    /** 현재 직업 빌드를 초기화할 때 환급할 스킬 포인트 값을 반환한다. */
    int32 GetCurrentClassRefundableSkillPoints() const;

    /** 직업 빌드 초기화에 필요한 골드 값을 반환한다. */
    int32 GetBuildResetGoldCost() const
    {
        return 50;
    }

    /** 전직 단계와 선택 조건을 확인해 해당 특성을 확정한다. */
    void SelectTalent(int32 TargetTier, EFableTalentPath Path);

    /** 초기화 조건과 비용을 확인해 현재 직업의 스킬·특성을 되돌린다. */
    void ResetCurrentClassBuild(EFableClass ExpectedClass, int32 ExpectedRevision);

    /** 선택한 스킬의 강화 단계 값을 반환한다. */
    int32 GetSkillRank(int32 Slot) const;

    /** 스킬 해금 여부를 확인한다. */
    bool IsSkillUnlocked(int32 Slot) const;

    /** 현재 전직 단계 값을 반환한다. */
    int32 GetAdvancementTier() const;

    /** 현재 전직 단계의 직업 이름 값을 반환한다. */
    FString GetAdvancedClassName() const;

    /** 사용 가능한 스킬 포인트 값을 반환한다. */
    int32 GetAvailableSkillPoints() const;

    /** 전직에 필요한 조건을 만족하는지 확인한다. */
    bool CanAdvanceClass() const;

    /** 전직에 필요한 조건 안내 값을 반환한다. */
    FString GetAdvancementRequirementText() const;

    /** 사용 가능한 포인트와 스킬 조건을 확인해 강화 단계를 올린다. */
    void UpgradeSkill(int32 Slot);

    /** 전직 시험과 단계 조건을 확인해 다음 전직을 적용한다. */
    void AdvanceClass();

    /** 직업 변경 조건을 확인하고 현재 직업과 기본 장비를 갱신한다. */
    void SelectClass(EFableClass NewClass);

    /** 이야기 장의 수락·진행·보상 수령을 처리한다. */
    void InteractStory();

    /** 위치와 진행 조건이 맞는 이야기 오브젝트 상호작용을 기록한다. */
    void RecordStoryObject(int32 Index);

    /** 정화한 봉인의 결과를 이야기 목표 진행에 반영한다. */
    void RecordStorySeal(int32 Index);

    /** 이야기 준비 완료 여부를 확인한다. */
    bool IsStoryReady() const;

    /** 사용 이야기 오브젝트에 필요한 조건을 만족하는지 확인한다. */
    bool CanUseStoryObject(int32 Index) const;

    /** 이야기 장의 제목 값을 반환한다. */
    FString GetStoryTitle() const;

    /** 이야기 장의 설명 값을 반환한다. */
    FString GetStoryDescription() const;

    /** 이야기 장의 현재 목표 값을 반환한다. */
    FString GetStoryObjective() const;

    /** 이야기 보상의 안내 문구 값을 반환한다. */
    FString GetStoryRewardText() const;

    /** 이야기 목표의 위치 값을 반환한다. */
    bool GetStoryTarget(FVector& OutLocation, FString& OutLabel) const;

    /** 퀘스트의 수락·진행·보상 수령을 현재 단계에 맞게 처리한다. */
    void InteractQuest();

    /** 퀘스트의 수락·진행·보상 수령을 현재 단계에 맞게 처리한다. */
    void InteractQuest(EFableQuestId Quest);

    /** 퀘스트 제목 값을 반환한다. */
    FString GetQuestTitle(EFableQuestId Quest) const;

    /** 퀘스트 설명 값을 반환한다. */
    FString GetQuestDescription(EFableQuestId Quest) const;

    /** 퀘스트 진행 단계 값을 반환한다. */
    EFableQuestStage GetQuestStage(EFableQuestId Quest) const;

    /** 주변 대상 상호작용 퀘스트에 필요한 조건을 만족하는지 확인한다. */
    bool CanInteractQuest(EFableQuestId Quest) const;

    /** 지역 입장 자격 여부를 확인한다. */
    bool IsRegionEligible(EFableRegion Region) const;

    /** 처치가 발생한 지역과 난이도를 퀘스트 진행에 반영한다. */
    void RecordRegionKill(EFableRegion Region, bool bBoss, EFableDifficulty Difficulty = EFableDifficulty::Normal);

    /** 전직 시험의 진행도 값을 반환한다. */
    int32 GetAdvancementTrialProgress(int32 TargetTier) const;

    /** 기존 저장의 전직 인정 여부를 확인한다. */
    bool IsAdvancementLegacyGranted(int32 TargetTier) const;

    /** 실제로 시작한 서버 스킬의 전직 시험 식별 문맥을 발급한다. */
    FFableTrialCastContext CreateTrialCastContext(int32 Slot);

    /** 서버가 발급한 시전 문맥을 확인하고 전직 시험의 적중을 기록한다. */
    void RecordTrialSkillHit(const FFableTrialCastContext& Context, bool bAppliedSlow = false);

    /** 소유 여부와 착용 조건을 확인한 뒤 장비 슬롯을 변경한다. */
    void EquipItem(int32 ItemId);

    /** 장비 강화 조건과 비용을 확인하고 원래 옵션을 유지한 채 강화한다. */
    void EnhanceItem(int32 ItemId);

    /** 상점 이용 조건과 골드를 확인하고 구매 물품을 지급한다. */
    void BuyShopItem(EFableShopOffer Offer);

    /** 판매 가능한 소유 아이템을 제거하고 판매 대금을 지급한다. */
    void SellItem(int32 ItemId);

    /** 생존·잔량·대기시간을 확인하고 회복 성공 시 물약을 소모한다. */
    void UsePotion(bool bMana);

    /** 아이템 판매 가격 값을 반환한다. */
    int32 GetSellPrice(int32 ItemId) const;

    /** 물약의 남은 재사용 대기시간 값을 반환한다. */
    float GetPotionCooldownRemaining() const;

    /** 적 처치 경험치·재화·장비와 퀘스트 진행을 반영한다. */
    void RecordKill(bool bBoss);

    /** 안내 문구 값을 설정한다. */
    void SetNotice(const FString& Message);

    /** 던전 입장 자격 여부를 확인한다. */
    bool IsDungeonEligible() const;

    /** 공격력 값을 반환한다. */
    float GetAttackPower() const;

    /** 방어력 값을 반환한다. */
    float GetDefense() const;

    /** 장비가 추가하는 최대 체력 값을 반환한다. */
    float GetMaxHealthBonus() const;

    /** 초당 마나 회복량 값을 반환한다. */
    float GetManaRegenRate() const;

    /** 현재 장비와 선택 장비의 성능 비교 값을 반환한다. */
    bool GetEquipmentPreview(int32 ItemId, FFableEquipmentStats& Before, FFableEquipmentStats& After) const;

    /** 착용 중인 같은 세트 장비 수 값을 반환한다. */
    int32 GetEquippedSetCount(EFableEquipmentSet SetId, int32 ReplacementItemId = INDEX_NONE) const;

    /** 다음 레벨에 필요한 경험치 값을 반환한다. */
    int32 GetExperienceRequired() const
    {
        return Level < 20 ? Level * 100 : 0;
    }

    /** 장비 강화에 필요한 골드 값을 반환한다. */
    int32 GetUpgradeGoldCost(int32 ItemId) const;

    /** 장비 강화에 필요한 광석 값을 반환한다. */
    int32 GetUpgradeOreCost(int32 ItemId) const;

    /** 아이템 대상을 찾아 반환한다. */
    const FFableItem* FindItem(int32 ItemId) const;

    /** 창고 아이템 대상을 찾아 반환한다. */
    const FFableItem* FindStashItem(int32 ItemId) const;

    /** 제작에 필요한 재화 값을 반환한다. */
    FFableResourceCost GetCraftCost(int32 Rarity) const;

    /** 분해로 얻을 재료 값을 반환한다. */
    FFableResourceCost GetDismantleYield(int32 ItemId) const;

    /** 옵션 재설정에 필요한 재화 값을 반환한다. */
    FFableResourceCost GetRerollCost(int32 ItemId) const;

    /** 내구도 수리에 필요한 재화 값을 반환한다. */
    FFableResourceCost GetRepairCost(int32 ItemId) const;

    /** 장비 제작 거부 사유 값을 반환한다. */
    FString GetCraftBlockReason(int32 Rarity, EFableEquipmentSlot Slot) const;

    /** 장비 분해 거부 사유 값을 반환한다. */
    FString GetDismantleBlockReason(int32 ItemId) const;

    /** 옵션 재설정 거부 사유 값을 반환한다. */
    FString GetRerollBlockReason(int32 ItemId, int32 AffixIndex) const;

    /** 내구도 수리 거부 사유 값을 반환한다. */
    FString GetRepairBlockReason(int32 ItemId) const;

    /** 창고 보관 거부 사유 값을 반환한다. */
    FString GetDepositBlockReason(int32 ItemId) const;

    /** 창고 수령 거부 사유 값을 반환한다. */
    FString GetWithdrawBlockReason(int32 ItemId) const;

    /** 제작 조건과 재료를 확인해 새 식별자를 가진 장비를 만든다. */
    void CraftItem(int32 Rarity, EFableEquipmentSlot Slot);

    /** 분해 가능한 소유 장비를 제거하고 정해진 재료를 지급한다. */
    void DismantleItem(int32 ItemId);

    /** 장비 옵션 재설정 조건과 비용을 확인해 선택한 옵션을 다시 생성한다. */
    void RerollAffix(int32 ItemId, int32 AffixIndex);

    /** 수리 가능한 장비의 비용을 차감하고 내구도를 회복한다. */
    void RepairItem(int32 ItemId);

    /** 보관 조건을 확인해 인벤토리의 아이템을 창고로 옮긴다. */
    void DepositItem(int32 ItemId);

    /** 수령 조건을 확인해 창고 아이템을 인벤토리로 옮긴다. */
    void WithdrawItem(int32 ItemId);

    /** 허용된 전투 이벤트에 따라 착용 장비의 내구도를 감소시킨다. */
    void WearEquipment(EFableEquipmentSlot Slot, int32 Amount = 1);

    /** 검증에 사용하는 격리 저장 슬롯 값을 반환한다. */
    FString GetProfileSlotForValidation() const
    {
        return ProfileSlot;
    }

private:
    friend struct FFableBackendSession;
    UPROPERTY(Replicated)
    FString BackendPlayerId;
    UPROPERTY(Replicated)
    bool bBackendEnabled = false;
    UPROPERTY(Replicated)
    bool bBackendReady = false;
    UPROPERTY(Replicated)
    bool bBackendSavePending = false;
    UPROPERTY(Replicated)
    int64 BackendRevision = 0;
    UPROPERTY(Replicated)
    int64 BackendAcknowledgedRevision = 0;
    UPROPERTY(Replicated)
    FString BackendAcknowledgedProfileHash;
    bool bBackendLoading = false;
    UPROPERTY(Replicated)
    bool bBackendClosing = false;
    TSharedPtr<FFableBackendSession> BackendSession;

    /** 현재 진행·재화·장비를 저장 가능한 프로필로 복사한다. */
    UFableSaveGame* MakeProfileSnapshot();

    /** 검증된 프로필의 진행·재화·장비를 플레이어 상태에 적용한다. */
    void ApplyProfileSnapshot(const UFableSaveGame* Data);

    /** 현재 프로필을 백엔드 저장 대기열에 넣는다. */
    bool QueueBackendSnapshot(UFableSaveGame* Data, bool bFinalSnapshot = false);

#if WITH_DEV_AUTOMATION_TESTS
    friend struct FFableWorkshopTestAccess;
    friend struct FFableStoryTestAccess;
    friend struct FFableTalentTestAccess;
#endif
    UPROPERTY(Replicated)
    float NextPotionServerTime = 0.0f;
    FString ProfileSlot;
    int32 NextItemId = 5;
    bool bPersistenceEnabled = true;
    bool bStorageBlocked = false;
    double LastMutationTime = -100.0;
    double NextWeaponWearTime = -100.0;
    double NextArmorWearTime = -100.0;

    /** 직업 빌드를 변경할 수 없는 상태의 안내 문구를 구한다. */
    FString ClassBuildBlockReason() const;

    /** 전투에 적용되는 빌드 변경 번호를 증가시킨다. */
    void BumpClassBuildRevision();

    /** 이야기 보상과 장 진행을 함께 변경하고 저장한다. */
    bool CommitStoryReward(int32 ExpectedChapter);

    /** 기록 이야기 위치에 필요한 조건을 만족하는지 확인한다. */
    bool CanRecordStoryAt(EFableRegion Region, const FVector& Location) const;

    /** 공방 이용 거리·저장 준비·요청 간격을 확인해 상태 변경을 시작한다. */
    bool BeginWorkshopMutation();

    /** 현재 재화로 요청한 비용을 모두 지불할 수 있는지 확인한다. */
    bool CanAfford(const FFableResourceCost& Cost) const;

    /** 비용을 지불할 수 있는지 확인한 뒤 각 재화를 차감한다. */
    void SpendResources(const FFableResourceCost& Cost);

    /** 공방 작업에 필요한 장비 데이터의 거부 사유를 구한다. */
    FString WorkshopDataBlockReason() const;
    uint32 TrialGeneration = 1;
    uint32 NextTrialCastId = 0;
    TSet<uint32> CreditedTrialCasts;

    /** 서버 권한과 저장 준비 상태를 확인해 상태 변경 허용 여부를 판단한다. */
    bool AllowMutation();

    /** 이전 전직 시험 시전의 식별 문맥을 무효화한다. */
    void InvalidateTrialCasts();

    /** 현재 진행도를 바탕으로 전직 시험의 완료 가능 상태를 갱신한다. */
    void RefreshTrialReadiness();

    /** 전직 시험의 단계별 필수 조건이 모두 충족되었는지 확인한다. */
    bool AreTrialRequirementsMet(int32 TargetTier, const FFableAdvancementTrial& Trial) const;

    /** 전직 시험 대상을 찾아 반환한다. */
    const FFableAdvancementTrial* FindTrial(int32 TargetTier) const;

    /** 변경 가능한 전직 시험 대상을 찾아 반환한다. */
    FFableAdvancementTrial* FindMutableTrial(int32 TargetTier);

    /** 전직 시험의 시작 또는 완료를 현재 조건에 맞게 처리한다. */
    void InteractAdvancementTrial(int32 TargetTier);

    /** 플레이어가 요청한 마을 서비스의 이용 거리 안에 있는지 확인한다. */
    bool AtTownService(const FVector& Location) const;

    /** 경험치를 추가하고 레벨 상승 조건을 적용한다. */
    void AddExperience(int32 Amount);

    /** 새 아이템을 인벤토리에 추가하고 식별자 진행 상태를 갱신한다. */
    bool AddItem(EFableEquipmentSlot Slot, EFableClass ItemClass, int32 Rarity, int32 NominalPower,
                 EFableEquipmentSet SetId = EFableEquipmentSet::None,
                 EFableDifficulty Difficulty = EFableDifficulty::Normal);

    /** 정해진 성능 범위와 난수로 장비 품질·기본 성능·옵션을 생성한다. */
    FFableItem RollItem(EFableEquipmentSlot Slot, EFableClass ItemClass, int32 Rarity, int32 NominalPower,
                        EFableEquipmentSet SetId, EFableDifficulty Difficulty);

    /** 착용 장비의 기본 성능·옵션·세트 효과를 합산한다. */
    FFableEquipmentStats CalculateEquipmentStats(int32 WeaponId, int32 ArmorId, EFableClass ForClass) const;

    /** 시작 직업별 무기와 기본 방어구의 식별자·내구도를 초기화한다. */
    void InitializeStarterItems();

    /** 변경된 플레이어 상태를 저장하고 복제 갱신을 요청한다. */
    void Changed(const FString& Notice);
};
