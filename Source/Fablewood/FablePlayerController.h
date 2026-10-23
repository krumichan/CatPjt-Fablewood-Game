#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FableRPGTypes.h"
#include "FablePlayerController.generated.h"

class AFableCharacter;

enum class EFableDialogue : uint8 { None, Guide, Forge, Shop, Portal };
enum class EFableInventorySort : uint8 { Acquired, Quality, Power };
enum class EFableWorkshopTab : uint8 { Craft, Dismantle, Reroll, Repair, Stash };

UCLASS()
class FABLEWOOD_API AFablePlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
    void UIInteract();
    void UIStartRevive(AFableCharacter* Target);
    void UIStopRevive();
    AFableCharacter* GetNearbyReviveTarget() const;
    void UIRetryEncounter();
    bool CanRetryEncounter() const;
    FString GetRetryActionText() const;
    void UIConfirmQuest();
    void UISelectQuest(EFableQuestId QuestId);
    EFableQuestId GetSelectedQuestId() const;
    void UISelectDungeon(EFableRegion Region);
    void UIEnterSelectedDungeon();
    void UISelectDifficulty(EFableDifficulty Difficulty);
    EFableDifficulty GetSelectedDifficulty() const;
    bool IsDifficultySelectionLocked() const;
    EFableRegion GetSelectedDungeonId() const { return SelectedDungeonId; }
    bool IsPortalOpen() const { return Dialogue == EFableDialogue::Portal; }
    bool CanEnterDungeon(EFableRegion Region) const;
    FString GetDungeonReadinessText(EFableRegion Region) const;
    void UIChooseClass(EFableClass HeroClass);
    void UIEquip(int32 ItemId);
    void UIEnhance(int32 ItemId);
    void UIShopBuy(EFableShopOffer Offer);
    void UISell(int32 ItemId);
    void UIUseHealthPotion();
    void UIUseManaPotion();
    void UIOpenWorkshop();
    bool IsWorkshopOpen() const { return bWorkshopOpen; }
    void UISelectWorkshopTab(EFableWorkshopTab Tab);
    EFableWorkshopTab GetWorkshopTab() const { return WorkshopTab; }
    void UISelectWorkshopItem(int32 ItemId);
    int32 GetSelectedWorkshopItemId() const;
    TArray<int32> GetWorkshopDisplayIds() const;
    void UISelectCraftRecipe(int32 Rarity, EFableEquipmentSlot Slot);
    int32 GetCraftRarity() const { return CraftRarity; }
    EFableEquipmentSlot GetCraftSlot() const { return CraftSlot; }
    void UISelectAffix(int32 Index);
    int32 GetSelectedAffixIndex() const { return SelectedAffixIndex; }
    void UISetStashWithdrawal(bool bWithdraw);
    bool IsStashWithdrawal() const { return bStashWithdrawal; }
    void UICraft();
    void UIDismantle(int32 ItemId);
    bool IsDismantleConfirmationPending() const;
    void UIRerollAffix(int32 ItemId, int32 AffixIndex);
    void UIRepair(int32 ItemId);
    void UIDeposit(int32 ItemId);
    void UIWithdraw(int32 ItemId);
    void UIWorkshopAction();
    FString GetWorkshopBlockReason() const;
    void UIToggleInventory();
    void UIToggleQuests();
    void UIToggleHelp();
    void UIToggleSkills();
    void UISelectSkill(int32 Slot);
    void UISelectSkillPanelTab(int32 Tab);
    int32 GetSkillPanelTab() const { return SkillPanelTab; }
    void UISelectTalent(int32 TargetTier, EFableTalentPath Path);
    int32 GetSelectedTalentStage() const { return SelectedTalentTier; }
    EFableTalentPath GetSelectedTalentChoice() const { return SelectedTalentPath; }
    void UIChooseTalent();
    void UIRequestBuildReset();
    void UIConfirmBuildReset();
    void UICancelBuildReset();
    bool IsBuildResetConfirmationPending() const;
    void UIUpgradeSkill(int32 Slot);
    void UIAdvanceClass();
    int32 GetSelectedSkillSlot() const { return SelectedSkillSlot; }
    bool IsSkillsOpen() const { return bSkillsOpen; }
    void UISelectItem(int32 ItemId);
    TArray<int32> GetInventoryDisplayIds() const;
    void UICycleInventorySort();
    FString GetInventorySortLabel() const;
    void UIClosePanels();
    void UISelectPrevious();
    void UISelectNext();
    void UIStartNewGame(bool bHost = false);
    void UIContinueGame(bool bHost = false);
    void UIJoinGame();
    void UISetJoinAddress(const FString& Address);
    void UIFocusJoinAddress();
    void UISetInitialClass(EFableClass HeroClass);
    void UIReturnToFrontEnd();
    bool IsFrontEndOpen() const { return bFrontEndOpen; }
    bool IsJoinAddressFocused() const { return bJoinAddressFocused; }
    const FString& GetJoinAddress() const { return JoinAddress; }
    EFableClass GetInitialClass() const { return InitialClass; }
    bool IsInventoryOpen() const { return bInventoryOpen; }
    bool IsQuestJournalOpen() const { return bQuestsOpen; }
    bool IsHelpOpen() const { return bHelpOpen; }
    bool IsDialogueOpen() const { return Dialogue != EFableDialogue::None; }
    bool IsShopOpen() const { return Dialogue == EFableDialogue::Shop; }
    EFableDialogue GetDialogue() const { return Dialogue; }
    bool IsAnyPanelOpen() const { return bFrontEndOpen || bInventoryOpen || bWorkshopOpen || bQuestsOpen || bHelpOpen || bSkillsOpen || IsDialogueOpen(); }
    int32 GetSelectedItemId() const;
    FString GetContextActionText() const;
    int32 GetNearbyStoryObjectIndex() const;
    bool GetNavigationTarget(FVector& OutLocation, FString& OutLabel) const;
    bool ShouldShowTransientNotice() const;
    bool IsNearTownBoard() const;
    bool IsNearForge() const;
    bool IsNearMerchant() const;
    bool IsNearTownService() const;
protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void PlayerTick(float DeltaTime) override;
private:
    bool bFrontEndOpen = false;
    bool bInventoryOpen = false;
    bool bWorkshopOpen = false;
    EFableWorkshopTab WorkshopTab = EFableWorkshopTab::Craft;
    int32 WorkshopItemId = INDEX_NONE;
    int32 CraftRarity = 1;
    EFableEquipmentSlot CraftSlot = EFableEquipmentSlot::Weapon;
    int32 SelectedAffixIndex = 0;
    bool bStashWithdrawal = false;
    int32 PendingDismantleItemId = INDEX_NONE;
    FString PendingDismantleSignature;
    bool bQuestsOpen = false;
    bool bHelpOpen = false;
    bool bSkillsOpen = false;
    int32 SelectedSkillSlot = 0;
    int32 SkillPanelTab = 0;
    int32 SelectedTalentTier = 2;
    EFableTalentPath SelectedTalentPath = EFableTalentPath::First;
    bool bBuildResetConfirmationPending = false;
    EFableClass PendingBuildResetClass = EFableClass::Warrior;
    int32 PendingBuildResetRevision = INDEX_NONE;
    int32 PendingBuildResetGold = 0;
    int32 PendingBuildResetRefund = 0;
    EFableQuestId SelectedQuestId = EFableQuestId::Story;
    bool bHasExplicitQuestSelection = false;
    EFableRegion SelectedDungeonId = EFableRegion::RootRuins;
    EFableDifficulty SelectedDifficulty = EFableDifficulty::Normal;
    EFableInventorySort InventorySort = EFableInventorySort::Acquired;
    bool bJoinAddressFocused = false;
    bool bAddressSelectAll = false;
    bool bFreeCursor = false;
    bool bReviveInputHeld = false;
    bool bUIInputBlocked = false;
    EFableDialogue Dialogue = EFableDialogue::None;
    EFableClass InitialClass = EFableClass::Warrior;
    FString JoinAddress = TEXT("127.0.0.1:7788");
    int32 SelectedItemId = INDEX_NONE;
    float LastRPGRequestTime = -10.0f;
    int32 ObservedNoticeSerial = INDEX_NONE;
    int32 DismissedNoticeSerial = INDEX_NONE;
    FString ObservedNoticeText;
    FString DismissedNoticeText;
    double NoticeObservedAt = -10.0;
    void DismissTransientNotice();
    void RestartTrial();
    void ToggleCursor();
    void ApplyUIMode();
    void ChooseWarrior();
    void ChooseMage();
    void ChooseArcher();
    void EnhanceSelected();
    void SellSelected();
    bool AcceptRPGRequest();
    void ClearDismantleConfirmation();
    void ClearBuildResetConfirmation();
    FString GetDismantleConfirmationSignature() const;
    UFUNCTION(Server, Reliable) void ServerStartRevive(AFableCharacter* Target);
    UFUNCTION(Server, Reliable) void ServerCancelRevive();
    UFUNCTION(Server, Reliable) void ServerRetryEncounter();
    UFUNCTION(Server, Reliable) void ServerRestartTrial();
    UFUNCTION(Server, Reliable) void ServerInteract();
    UFUNCTION(Server, Reliable) void ServerConfirmQuest(EFableQuestId QuestId);
    UFUNCTION(Server, Reliable) void ServerEnterRegion(EFableRegion Region, EFableDifficulty Difficulty);
    UFUNCTION(Server, Reliable) void ServerChooseClass(EFableClass HeroClass);
    UFUNCTION(Server, Reliable) void ServerEquip(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerEnhance(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerCraft(int32 Rarity, EFableEquipmentSlot Slot);
    UFUNCTION(Server, Reliable) void ServerDismantle(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerRerollAffix(int32 ItemId, int32 AffixIndex);
    UFUNCTION(Server, Reliable) void ServerRepair(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerDeposit(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerWithdraw(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerShopBuy(EFableShopOffer Offer);
    UFUNCTION(Server, Reliable) void ServerSell(int32 ItemId);
    UFUNCTION(Server, Reliable) void ServerUsePotion(bool bMana);
    UFUNCTION(Server, Reliable) void ServerUpgradeSkill(int32 Slot);
    UFUNCTION(Server, Reliable) void ServerSelectTalent(int32 TargetTier, EFableTalentPath Path);
    UFUNCTION(Server, Reliable) void ServerResetCurrentClassBuild(EFableClass ExpectedClass, int32 ExpectedRevision);
    UFUNCTION(Server, Reliable) void ServerAdvanceClass();
    UFUNCTION(Client, Reliable) void ClientOpenDialogue(uint8 Service);
};
