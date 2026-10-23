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
UCLASS()
class FABLEWOOD_API UFableRPGValidationDriver : public UActorComponent
{
    GENERATED_BODY()
public:
    UFableRPGValidationDriver();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
private:
    TWeakObjectPtr<AFablePlayerController> PC;
    FString Role,RunId,Directory,Phase;
    int32 Step=0;

    bool bTalents=false;
    int32 TalentCycle=0,TalentProbe=0,TalentNoticeBefore=0,TalentGoldBefore=0,TalentPointsBefore=0;
    int32 TalentRevisionBefore=0,TalentCastBefore=0,TalentProbeRole=0,TalentProbeDoneCycle=-1,TalentInitialRoot=0,TalentPeerDoneCycleSeen=-1;
    float TalentMaximumSpeed=0;
    double TalentAt=0,LastTalentReport=-10,TalentQuietAt=-1,TalentDamageStableAt=-1,TalentRetryAt=-10;
    float TalentStableBossHealth=-1;
    bool bTalentCleanDamageWindow=false;
    TWeakObjectPtr<class AFableEnemy> TalentStableBoss;
    float TalentHealthBefore=0,TalentBurnBefore=0,TalentSlowBefore=0,TalentAllyBefore=0;
    FVector TalentPositionBefore=FVector::ZeroVector;
    EFableClass TalentInitialClass=EFableClass::Warrior;
    FString TalentInitialSignature,TalentBeforeReset,TalentOtherClasses;
    TWeakObjectPtr<class AFableEnemy> TalentTarget;
    TMap<FString,bool> TalentChecks;
    TArray<TSharedPtr<FJsonValue>> TalentEvents;
    void TickTalents();
    void AppendTalentReport(TSharedRef<FJsonObject> Object) const;
    int32 TalentPeerInt(const FString& Field) const;
    void TalentRecord(const FString& Action);
    bool TalentCombatProbe();
    void TalentNextProbe(int32 Next);
    bool bTerminalPending=false;
    FString TerminalJson,TerminalStatus,TerminalReason;
    double TerminalStartedAt=0,TerminalNextAttempt=0;
    int32 TerminalAttempts=0;
    bool SerializeReport(const FString& Status,const FString& Reason,FString& OutText);
    void PublishTerminalReport();

    bool bWorkshop=false, bWorkshopStarted=false;
    int32 WorkshopStage=0,WorkshopItemId=0,WorkshopGrade=1,WorkshopLegendId=0,WorkshopStashId=0,WorkshopYield=0,WorkshopRepairIndex=0;
    double WorkshopAt=0,LastWorkshopReport=-10;
    FString WorkshopBeforeSignature;
    FFableItem WorkshopBeforeItem;
    FFableResourceCost WorkshopBeforeBalance,WorkshopCost;
    TArray<int32> WorkshopRepairIds,WorkshopCraftIds;
    TArray<FFableItem> WorkshopInitialItems;
    TSet<int32> WorkshopBeforeIds;
    TMap<FString,bool> WorkshopChecks;
    TArray<TSharedPtr<FJsonValue>> WorkshopEvents;
    void TickWorkshop();
    void AppendWorkshopReport(TSharedRef<FJsonObject> Object) const;
    void WorkshopSnapshot();
    bool WorkshopPaid(const FFableResourceCost& Cost) const;
    void WorkshopRecord(const FString& Action,int32 ItemId);
    bool bLoot=false;
    int32 LootCycle=0,LootInitialRoot=0,LootInitialMoon=0,LootStage=0,LootProbeCycle=-1,LootCandidateId=0;
    int32 LootRecoveryBefore=0,LootSerialBefore=0,LootGoldBefore=0,LootOreBefore=0,LootXPBefore=0,LootShopId=0,LootSellPrice=0;
    double LastLootReport=-10,LootActionAt=0;
    float LootLegacyAttack=0,LootLegacyDefense=0,LootBossHealth=0;
    FString LootLegacySignature,LootBeforeWipe,LootBeforeRemote;
    FFableItem LootBeforeEnhance;
    FFableEquipmentStats LootPreview;
    TMap<FString,bool> LootChecks;
    TSet<int32> LootBeforeIds;
    TArray<int32> LootBossIds;
    TArray<TSharedPtr<FJsonValue>> LootEvents;
    void TickLoot();
    FString LegacyItemSignature(const AFablePlayerState* PS) const;
    void AppendLootReport(TSharedRef<FJsonObject> Object) const;
    int32 LootPeerCycle(const FString& Field) const;
    bool LootEnemyStats(EFableRegion Region,EFableDifficulty Difficulty) const;
    bool LootPreviewMatches() const;
    void LootStop();

    bool bStory=false, bStoryReloadObserved=false;
    int32 StoryObject=-1, StoryCheck=0, StoryRequestedChapter=-1, StoryPostStage=0, StoryNoticeSerial=0;
    int32 StoryGoldBefore=0, StoryOreBefore=0, StoryXPBefore=0;
    double StoryAt=0, LastStoryReport=-10, StoryResumeAt=0;
    FString StoryBeforeSignature, StoryReloadSignature, StoryExpectedNotice;
    TMap<FString,bool> StoryChecks;
    TArray<TSharedPtr<FJsonValue>> StoryEvents;
    bool TickStory();
    void StoryEvent(const FString& Action);
    void AppendStoryReport(TSharedRef<FJsonObject> Object) const;
    bool bAdventure=false, bAdventureWarrior=false;
    bool bSealAttempted=false, bMoonPortalCaptured=false;
    bool bTrialStarted=false, bPrematureTrialRejected=false, bMoonLocked=false, bSealLocked=false;
    bool bMoonRipple=false, bMoonRain=false, bMoonRanged=false, bMoonCleared=false;
    bool bNoRootCrossCredit=false, bTrialSwitchPreserved=false, bTrialDuplicateRejected=false;
    int32 ObservedSealMask=0, TrialTierBefore=0;
    double LastAdventureReport=0, DodgeWindup=-1;
    double AdventureReviveStableAt=-1, AdventureRevivePressedAt=-1, AdventureReviveRetryAt=0;
    FVector DodgeDestination=FVector::ZeroVector;
    void TickAdventure();
    bool TickAdventureRecovery();
    void AdventureFight();
    void AdventureQuest(EFableQuestId Quest);
    void AdventurePortal(EFableRegion Region);
    FString AdventureSignature(const AFablePlayerState* PS,bool bIgnoreWear=false) const;

    bool bSkills=false;
    bool SkillPhasesDone[4]={false,false,false,false};
    int32 SkillPhase=-1,SkillCheck=0,PointsBefore=0,CastBefore[3]={0,0,0},MaxPiercingHits=0;
    double SkillAt=0,SkillPhaseAt=0,LastAdvancedInput=-10,SecondDungeonAt=0;
    float PassivePowerBefore=0;
    FVector InputBefore=FVector::ZeroVector;
    FString WantedProgress;
    bool bPrematureAdvanceRejected=false,bLockedSkillsRejected=false,bLockedUpgradeRejected=false;
    bool bSkillPointsRejected=false,bSkillPanelBlocksInput=false,bPassiveUpgraded=false;
    bool bSecondAdvancement=false,bSkillRankThree=false,bThirdAdvancement=false;
    bool bDuplicateAdvanceRejected=false,bSkillCooldownRejected=false,bWarriorSkillsChecked=false;
    bool bClassSwitchPreserved=false,bAdvancementSurvivesRepeat=false,bSecondDungeonCompleted=false;
    bool bSlowObserved=false,bPiercingObserved=false,bSecondDungeon=false;
    void NextSkillCheck(int32 Value);
    bool RunSkillChecks(int32 PhaseIndex);
    void ObserveSkills();
    bool SkillAssertionsComplete() const;
    FString SelectedProgress(const AFablePlayerState* PS) const;

    int32 PotionStep=0, HealthBefore=0, ManaBefore=0, InventoryBefore=0, ShopItemId=0;
    float ResourceBefore=0, MaxObservedResource=0;
    double PotionAt=0;
    bool bDialogueChecked=false,bHealthPotionChecked=false,bManaPotionChecked=false,bPotionCooldownChecked=false;
    bool bFullPotionRejected=false,bShopBought=false,bShopSold=false,bRemoteShopRejected=false,bProtectedSaleRejected=false,bPoorShopRejected=false;
    bool bBossCaptured=false;
    double StepAt=0,StartedAt=0,LastAction=-10,LastSkill=-10;
    int32 RareId=0,EpicId=0,GoldBefore=0,OreBefore=0,AttackInputs=0,SkillInputs=0;
    bool bDone=false,bSawWaveTwo=false,bSawBoss=false,bSawEnrage=false,bSawWarrior=false,bRejectedRemoteUpgrade=false,bRejectedInvalidItem=false,bRejectedWrongClass=false,bRejectedInsufficientFunds=false,bArmorEquipped=false;
    double Movement=0,RemoteMovement=0;
    FVector PreviousLocation=FVector::ZeroVector,PreviousRemote=FVector::ZeroVector;
    TArray<FKey> ReleaseKeys;
    TArray<FString> Screenshots;
    void Key(const FKey& Key);
    void Hold(const FKey& Key,bool bHeld);
    void Release();
    bool WalkTo(const FVector& Location,float Radius=140);
    void Advance(int32 NewStep);
    void Capture(const FString& Name);
    void Fight();
    bool PreparePotions();
    void QuestInteraction();
    void Finish(bool bSuccess,const FString& Reason);
    void Report(const FString& Status,const FString& Reason);
    FString Signature(const AFablePlayerState* PS,bool bIgnoreWear=false) const;
};
