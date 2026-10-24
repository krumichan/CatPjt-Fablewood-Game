#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "FableRPGTypes.h"
#include "FableRPGValidationDriver.generated.h"
class AFablePlayerController;
class AFablePlayerState;
class AFableCharacter;
class FJsonObject;
class FJsonValue;

/** RPG의 일반 입력과 관찰 결과로 자동 검증을 진행하는 컴포넌트. */
UCLASS()
class FABLEWOOD_API UFableRPGValidationDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    UFableRPGValidationDriver();

    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

private:
    TWeakObjectPtr<AFablePlayerController> PC;
    FString Role, RunId, Directory, Phase;
    int32 Step = 0;

    bool bTalents = false;
    int32 TalentCycle = 0, TalentProbe = 0, TalentNoticeBefore = 0, TalentGoldBefore = 0, TalentPointsBefore = 0;
    int32 TalentRevisionBefore = 0, TalentCastBefore = 0, TalentProbeRole = 0, TalentProbeDoneCycle = -1,
          TalentInitialRoot = 0, TalentPeerDoneCycleSeen = -1;
    float TalentMaximumSpeed = 0;
    double TalentAt = 0, LastTalentReport = -10, TalentQuietAt = -1, TalentDamageStableAt = -1, TalentRetryAt = -10;
    float TalentStableBossHealth = -1;
    bool bTalentCleanDamageWindow = false;
    TWeakObjectPtr<class AFableEnemy> TalentStableBoss;
    float TalentHealthBefore = 0, TalentBurnBefore = 0, TalentSlowBefore = 0, TalentAllyBefore = 0;
    FVector TalentPositionBefore = FVector::ZeroVector;
    EFableClass TalentInitialClass = EFableClass::Warrior;
    FString TalentInitialSignature, TalentBeforeReset, TalentOtherClasses;
    TWeakObjectPtr<class AFableEnemy> TalentTarget;
    TMap<FString, bool> TalentChecks;
    TArray<TSharedPtr<FJsonValue>> TalentEvents;

    /** 전직 특성 선택·초기화·전투 효과의 검증 단계를 진행한다. */
    void TickTalents();

    /** 특성 보고서 항목을 최종 검증 JSON에 추가한다. */
    void AppendTalentReport(TSharedRef<FJsonObject> Object) const;

    /** 특성 상대 클라이언트 정수에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    int32 TalentPeerInt(const FString& Field) const;

    /** 특성 기록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void TalentRecord(const FString& Action);

    /** 특성 전투 관찰에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool TalentCombatProbe();

    /** 특성 다음 관찰에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void TalentNextProbe(int32 Next);
    bool bTerminalPending = false;
    FString TerminalJson, TerminalStatus, TerminalReason;
    double TerminalStartedAt = 0, TerminalNextAttempt = 0;
    int32 TerminalAttempts = 0;

    /** 현재 관찰 값과 검증 결과를 JSON 보고서로 직렬화한다. */
    bool SerializeReport(const FString& Status, const FString& Reason, FString& OutText);

    /** 종료 보고서 결과를 보고서에 기록한다. */
    void PublishTerminalReport();

    bool bWorkshop = false, bWorkshopStarted = false;
    int32 WorkshopStage = 0, WorkshopItemId = 0, WorkshopGrade = 1, WorkshopLegendId = 0, WorkshopStashId = 0,
          WorkshopYield = 0, WorkshopRepairIndex = 0;
    double WorkshopAt = 0, LastWorkshopReport = -10;
    FString WorkshopBeforeSignature;
    FFableItem WorkshopBeforeItem;
    FFableResourceCost WorkshopBeforeBalance, WorkshopCost;
    TArray<int32> WorkshopRepairIds, WorkshopCraftIds;
    TArray<FFableItem> WorkshopInitialItems;
    TSet<int32> WorkshopBeforeIds;
    TMap<FString, bool> WorkshopChecks;
    TArray<TSharedPtr<FJsonValue>> WorkshopEvents;

    /** 제작·분해·옵션·수리·창고의 검증 단계를 진행한다. */
    void TickWorkshop();

    /** 공방 보고서 항목을 최종 검증 JSON에 추가한다. */
    void AppendWorkshopReport(TSharedRef<FJsonObject> Object) const;

    /** 공방 스냅숏에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void WorkshopSnapshot();

    /** 공방 비용 차감에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool WorkshopPaid(const FFableResourceCost& Cost) const;

    /** 공방 기록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void WorkshopRecord(const FString& Action, int32 ItemId);
    bool bLoot = false;
    int32 LootCycle = 0, LootInitialRoot = 0, LootInitialMoon = 0, LootStage = 0, LootProbeCycle = -1,
          LootCandidateId = 0;
    int32 LootRecoveryBefore = 0, LootSerialBefore = 0, LootGoldBefore = 0, LootOreBefore = 0, LootXPBefore = 0,
          LootShopId = 0, LootSellPrice = 0;
    double LastLootReport = -10, LootActionAt = 0;
    float LootLegacyAttack = 0, LootLegacyDefense = 0, LootBossHealth = 0;
    FString LootLegacySignature, LootBeforeWipe, LootBeforeRemote;
    FFableItem LootBeforeEnhance;
    FFableEquipmentStats LootPreview;
    TMap<FString, bool> LootChecks;
    TSet<int32> LootBeforeIds;
    TArray<int32> LootBossIds;
    TArray<TSharedPtr<FJsonValue>> LootEvents;

    /** 장비 난수·보상·착용 결과의 검증 단계를 진행한다. */
    void TickLoot();

    /** 이전 저장 형식과 비교할 장비의 기존 속성을 직렬화한다. */
    FString LegacyItemSignature(const AFablePlayerState* PS) const;

    /** 전리품 보고서 항목을 최종 검증 JSON에 추가한다. */
    void AppendLootReport(TSharedRef<FJsonObject> Object) const;

    /** 전리품 상대 클라이언트의 진행 차례에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    int32 LootPeerCycle(const FString& Field) const;

    /** 전리품 적 능력치에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool LootEnemyStats(EFableRegion Region, EFableDifficulty Difficulty) const;

    /** 전리품 장비 미리보기 일치에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool LootPreviewMatches() const;

    /** 전리품 중지에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void LootStop();

    bool bStory = false, bStoryReloadObserved = false;
    int32 StoryObject = -1, StoryCheck = 0, StoryRequestedChapter = -1, StoryPostStage = 0, StoryNoticeSerial = 0;
    int32 StoryGoldBefore = 0, StoryOreBefore = 0, StoryXPBefore = 0;
    double StoryAt = 0, LastStoryReport = -10, StoryResumeAt = 0;
    FString StoryBeforeSignature, StoryReloadSignature, StoryExpectedNotice;
    TMap<FString, bool> StoryChecks;
    TArray<TSharedPtr<FJsonValue>> StoryEvents;

    /** 이야기 목표와 보상 저장의 검증 단계를 진행한다. */
    bool TickStory();

    /** 이야기 이벤트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void StoryEvent(const FString& Action);

    /** 이야기 보고서 항목을 최종 검증 JSON에 추가한다. */
    void AppendStoryReport(TSharedRef<FJsonObject> Object) const;
    bool bAdventure = false, bAdventureWarrior = false;
    bool bSealAttempted = false, bMoonPortalCaptured = false;
    bool bTrialStarted = false, bPrematureTrialRejected = false, bMoonLocked = false, bSealLocked = false;
    bool bMoonRipple = false, bMoonRain = false, bMoonRanged = false, bMoonCleared = false;
    bool bNoRootCrossCredit = false, bTrialSwitchPreserved = false, bTrialDuplicateRejected = false;
    int32 ObservedSealMask = 0, TrialTierBefore = 0;
    double LastAdventureReport = 0, DodgeWindup = -1;
    double AdventureReviveStableAt = -1, AdventureRevivePressedAt = -1, AdventureReviveRetryAt = 0;
    FVector DodgeDestination = FVector::ZeroVector;

    /** 모험 진행과 지역 이동의 검증 단계를 진행한다. */
    void TickAdventure();

    /** 모험 중 쓰러짐과 안전 복귀의 검증 단계를 진행한다. */
    bool TickAdventureRecovery();

    /** 모험 전투에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void AdventureFight();

    /** 모험 퀘스트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void AdventureQuest(EFableQuestId Quest);

    /** 모험 이동문에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void AdventurePortal(EFableRegion Region);

    /** 모험 상태 서명에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    FString AdventureSignature(const AFablePlayerState* PS, bool bIgnoreWear = false) const;

    bool bSkills = false;
    bool SkillPhasesDone[4] = {false, false, false, false};
    int32 SkillPhase = -1, SkillCheck = 0, PointsBefore = 0, CastBefore[3] = {0, 0, 0}, MaxPiercingHits = 0;
    double SkillAt = 0, SkillPhaseAt = 0, LastAdvancedInput = -10, SecondDungeonAt = 0;
    float PassivePowerBefore = 0;
    FVector InputBefore = FVector::ZeroVector;
    FString WantedProgress;
    bool bPrematureAdvanceRejected = false, bLockedSkillsRejected = false, bLockedUpgradeRejected = false;
    bool bSkillPointsRejected = false, bSkillPanelBlocksInput = false, bPassiveUpgraded = false;
    bool bSecondAdvancement = false, bSkillRankThree = false, bThirdAdvancement = false;
    bool bDuplicateAdvanceRejected = false, bSkillCooldownRejected = false, bWarriorSkillsChecked = false;
    bool bClassSwitchPreserved = false, bAdvancementSurvivesRepeat = false, bSecondDungeonCompleted = false;
    bool bSlowObserved = false, bPiercingObserved = false, bSecondDungeon = false;

    /** 다음 스킬 검사에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void NextSkillCheck(int32 Value);

    /** 실행 스킬 검사 목록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool RunSkillChecks(int32 PhaseIndex);

    /** 스킬 변화를 관찰해 검증에 기록한다. */
    void ObserveSkills();

    /** 스킬 검증 항목 완료에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool SkillAssertionsComplete() const;

    /** 현재 선택한 직업의 전직·스킬 진행 상태를 반환한다. */
    FString SelectedProgress(const AFablePlayerState* PS) const;

    int32 PotionStep = 0, HealthBefore = 0, ManaBefore = 0, InventoryBefore = 0, ShopItemId = 0;
    float ResourceBefore = 0, MaxObservedResource = 0;
    double PotionAt = 0;
    bool bDialogueChecked = false, bHealthPotionChecked = false, bManaPotionChecked = false,
         bPotionCooldownChecked = false;
    bool bFullPotionRejected = false, bShopBought = false, bShopSold = false, bRemoteShopRejected = false,
         bProtectedSaleRejected = false, bPoorShopRejected = false;
    bool bBossCaptured = false;
    double StepAt = 0, StartedAt = 0, LastAction = -10, LastSkill = -10;
    int32 RareId = 0, EpicId = 0, GoldBefore = 0, OreBefore = 0, AttackInputs = 0, SkillInputs = 0;
    bool bDone = false, bSawWaveTwo = false, bSawBoss = false, bSawEnrage = false, bSawWarrior = false,
         bRejectedRemoteUpgrade = false, bRejectedInvalidItem = false, bRejectedWrongClass = false,
         bRejectedInsufficientFunds = false, bArmorEquipped = false;
    double Movement = 0, RemoteMovement = 0;
    FVector PreviousLocation = FVector::ZeroVector, PreviousRemote = FVector::ZeroVector;
    TArray<FKey> ReleaseKeys;
    TArray<FString> Screenshots;

    /** 검증 캐릭터에 단발 키 입력을 전달한다. */
    void Key(const FKey& Key);

    /** 검증에 필요한 키의 누름 상태를 유지한다. */
    void Hold(const FKey& Key, bool bHeld);

    /** 검증에서 누른 키의 유지 상태를 해제한다. */
    void Release();

    /** 일반 이동 입력으로 검증 캐릭터를 목표 지점에 접근시킨다. */
    bool WalkTo(const FVector& Location, float Radius = 140);

    /** 검증 상태를 다음 단계로 옮기고 단계 시작 시각을 갱신한다. */
    void Advance(int32 NewStep);

    /** 현재 검증 단계의 화면을 캡처한다. */
    void Capture(const FString& Name);

    /** 일반 공격·스킬 입력으로 현재 전투 검증을 진행한다. */
    void Fight();

    /** 물약 조건을 준비한다. */
    bool PreparePotions();

    /** 일반 상호작용 입력으로 검증 중인 퀘스트의 수락·보상 단계를 진행한다. */
    void QuestInteraction();

    /** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
    void Finish(bool bSuccess, const FString& Reason);

    /** 현재 검증 상태와 수집한 근거를 보고서로 기록한다. */
    void Report(const FString& Status, const FString& Reason);

    /** 검증 대상 상태의 변경 여부를 비교할 서명 문자열을 만든다. */
    FString Signature(const AFablePlayerState* PS, bool bIgnoreWear = false) const;
};
