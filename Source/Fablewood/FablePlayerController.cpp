#include "FablePlayerController.h"
#include "FableGameMode.h"
#include "FableGameInstance.h"
#include "FableOnlineFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformApplicationMisc.h"
#include "FableGameState.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#if !UE_BUILD_SHIPPING
#include "FableValidationDriver.h"
#include "FableRPGValidationDriver.h"
#include "FableFrontEndValidationDriver.h"
#include "FableCoopValidationDriver.h"
#include "FableDedicatedValidationDriver.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#endif
#if !UE_BUILD_SHIPPING
namespace
{
// Opt-in art review only. Combat validation uses its independent, unchanged driver.
void StartArtReview(APlayerController* PC)
{
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("FableArtReviewDirectory="), Directory) || FPaths::IsRelative(Directory)) return;
    IFileManager::Get().MakeDirectory(*Directory, true);
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    if (AHUD* HUD = PC->GetHUD()) HUD->bShowHUD = false;
    for (TActorIterator<AFableEnemy> It(PC->GetWorld()); It; ++It) It->SetActorTickEnabled(false);
    ACameraActor* Camera = PC->GetWorld()->SpawnActor<ACameraActor>();
    if (!Camera) return;
    PC->bAutoManageActiveCameraTarget = false;
    PC->SetViewTarget(Camera);
    const auto Shot = [PC, Camera, Directory](float At, const FString& Name, FVector Location, FVector Target, float Fov)
    {
        FTimerHandle PoseHandle;
        PC->GetWorldTimerManager().SetTimer(PoseHandle, [PC, Camera, Location, Target, Fov, Name]() mutable
        {
            if (!IsValid(Camera) || !IsValid(PC)) return;
            PC->SetViewTarget(Camera);
            if (AHUD* HUD = PC->GetHUD()) HUD->bShowHUD = false;
            for (TActorIterator<AFableEnemy> It(PC->GetWorld()); It; ++It) It->SetActorTickEnabled(false);
            if (Name == TEXT("hero") && PC->GetPawn())
            {
                Target = PC->GetPawn()->GetActorLocation() + FVector(0,0,10);
                Location = Target + FVector(285,470,65);
            }
            Camera->SetActorLocation(Location);
            Camera->SetActorRotation((Target - Location).Rotation());
            Camera->GetCameraComponent()->SetFieldOfView(Fov);
        }, At, false);
        FTimerHandle CaptureHandle;
        PC->GetWorldTimerManager().SetTimer(CaptureHandle, [Directory, Name]()
        {
            FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, Name + TEXT(".png")), false, false);
        }, At + 6.0f, false);
    };
    Shot(1.0f, TEXT("hero"), FVector(210,-560,160), FVector(0,-900,103), 46.0f);
    Shot(11.0f, TEXT("forest"), FVector(850,-1800,620), FVector(-250,1300,340), 67.0f);
    Shot(21.0f, TEXT("bridge"), FVector(-1380,-1180,365), FVector(-2310,-260,155), 62.0f);
    FTimerHandle ExitHandle;
    PC->GetWorldTimerManager().SetTimer(ExitHandle, [](){ FGenericPlatformMisc::RequestExit(false); }, 32.0f, false);
}
}
#endif
void AFablePlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    if (const UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance())) bFrontEndOpen = GI->IsFrontEnd();
    ApplyUIMode();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("FableArtReview"))) StartArtReview(this);
    FString ValidationRole;
    if (FParse::Value(FCommandLine::Get(), TEXT("FableDedicatedValidationRole="), ValidationRole))
    {
        UFableDedicatedValidationDriver* Driver = NewObject<UFableDedicatedValidationDriver>(this);
        Driver->RegisterComponent();
    }
    else if (FParse::Value(FCommandLine::Get(), TEXT("FableCoopValidationRole="), ValidationRole))
    {
        UFableCoopValidationDriver* Driver = NewObject<UFableCoopValidationDriver>(this);
        Driver->RegisterComponent();
    }
    else
    {
        if (FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRole="), ValidationRole))
        {
            UFableRPGValidationDriver* Driver = NewObject<UFableRPGValidationDriver>(this);
            Driver->RegisterComponent();
        }
        else if (FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), ValidationRole))
        {
            UFableValidationDriver* Driver = NewObject<UFableValidationDriver>(this);
            Driver->RegisterComponent();
        }
        if (FParse::Value(FCommandLine::Get(), TEXT("FableFrontEndValidationRole="), ValidationRole))
        {
            UFableFrontEndValidationDriver* Driver = NewObject<UFableFrontEndValidationDriver>(this);
            Driver->RegisterComponent();
        }
    }
#endif
}
namespace
{
enum class EFableService : uint8 { None, Board, Forge, Merchant, Portal, ReturnPortal, MoonSeal, StoryObject };
EFableRegion PawnRegion(const APlayerController* PC)
{
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (Pawn && FableRPG::IsInRegion(EFableRegion::Moonwell, Pawn->GetActorLocation())) return EFableRegion::Moonwell;
    if (Pawn && FableRPG::IsInRegion(EFableRegion::RootRuins, Pawn->GetActorLocation())) return EFableRegion::RootRuins;
    return EFableRegion::Village;
}
int32 NearbyMoonSeal(const APlayerController* PC)
{
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn || PawnRegion(PC) != EFableRegion::Moonwell) return INDEX_NONE;
    for (int32 Index=0; Index<3; ++Index)
    {
        const FVector Delta=Pawn->GetActorLocation()-FableRPG::MoonSealLocation(Index);
        if (Delta.SizeSquared2D()<=FMath::Square(330.f) && FMath::Abs(Delta.Z)<300.f) return Index;
    }
    return INDEX_NONE;
}
EFableService NearestService(const APlayerController* PC)
{
    const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn) return EFableService::None;
    const FVector Locations[] = {FableRPG::BoardLocation(), FableRPG::ForgeLocation(), FableRPG::MerchantLocation(),
        FableRPG::PortalLocation(), FableRPG::RegionEntrance(EFableRegion::RootRuins), FableRPG::RegionReturnPortal(EFableRegion::RootRuins),
        FableRPG::RegionEntrance(EFableRegion::Moonwell), FableRPG::RegionReturnPortal(EFableRegion::Moonwell),
        FableRPG::MoonSealLocation(0),FableRPG::MoonSealLocation(1),FableRPG::MoonSealLocation(2)};
    const EFableService Services[] = {EFableService::Board, EFableService::Forge, EFableService::Merchant,
        EFableService::Portal, EFableService::ReturnPortal, EFableService::ReturnPortal,
        EFableService::ReturnPortal, EFableService::ReturnPortal, EFableService::MoonSeal, EFableService::MoonSeal, EFableService::MoonSeal};
    float NearestSquared = TNumericLimits<float>::Max();
    EFableService Nearest = EFableService::None;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Locations); ++Index)
    {
        const float DistanceSquared = FVector::DistSquared2D(Pawn->GetActorLocation(), Locations[Index]);
        const float Radius = Services[Index] == EFableService::ReturnPortal ? 350.0f : 330.0f;
        if (DistanceSquared <= FMath::Square(Radius) && DistanceSquared < NearestSquared
            && FMath::Abs(Pawn->GetActorLocation().Z - Locations[Index].Z) < 300.0f)
        {
            NearestSquared = DistanceSquared;
            Nearest = Services[Index];
        }
    }
    for (int32 Index=0;Index<5;++Index)
    {
        const FVector Delta=Pawn->GetActorLocation()-FableRPG::StoryObjectLocation(Index);
        const float DistanceSquared=Delta.SizeSquared2D();
        if (DistanceSquared<=FMath::Square(330.f) && DistanceSquared<NearestSquared && FMath::Abs(Delta.Z)<300.f)
        { NearestSquared=DistanceSquared; Nearest=EFableService::StoryObject; }
    }
    return Nearest;
}
}

void AFablePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    InputComponent->BindAction("RestartTrial", IE_Pressed, this, &AFablePlayerController::RestartTrial);
    InputComponent->BindAction("ToggleCursor", IE_Pressed, this, &AFablePlayerController::ToggleCursor);
    InputComponent->BindAction("Interact", IE_Pressed, this, &AFablePlayerController::UIInteract);
    InputComponent->BindAction("Interact", IE_Released, this, &AFablePlayerController::UIStopRevive);
    InputComponent->BindAction("Inventory", IE_Pressed, this, &AFablePlayerController::UIToggleInventory);
    InputComponent->BindAction("QuestJournal", IE_Pressed, this, &AFablePlayerController::UIToggleQuests);
    InputComponent->BindAction("Help", IE_Pressed, this, &AFablePlayerController::UIToggleHelp);
    InputComponent->BindAction("SkillPanel", IE_Pressed, this, &AFablePlayerController::UIToggleSkills);
    InputComponent->BindAction("ChooseWarrior", IE_Pressed, this, &AFablePlayerController::ChooseWarrior);
    InputComponent->BindAction("ChooseMage", IE_Pressed, this, &AFablePlayerController::ChooseMage);
    InputComponent->BindAction("ChooseArcher", IE_Pressed, this, &AFablePlayerController::ChooseArcher);
    InputComponent->BindAction("PreviousItem", IE_Pressed, this, &AFablePlayerController::UISelectPrevious);
    InputComponent->BindAction("NextItem", IE_Pressed, this, &AFablePlayerController::UISelectNext);
    InputComponent->BindAction("EnhanceItem", IE_Pressed, this, &AFablePlayerController::EnhanceSelected);
    InputComponent->BindAction("SellItem", IE_Pressed, this, &AFablePlayerController::SellSelected);
    InputComponent->BindAction("HealthPotion", IE_Pressed, this, &AFablePlayerController::UIUseHealthPotion);
    InputComponent->BindAction("ManaPotion", IE_Pressed, this, &AFablePlayerController::UIUseManaPotion);
}

bool AFablePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    if (auto Flow=GetGameInstance()?GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>():nullptr; Flow && Flow->IsEnabled())
    {
        // Slate owns focused text fields and menu keys. Never send them to gameplay bindings.
        if (Flow->IsOverlayBlocking()) return false;
        if (Params.Key==EKeys::Escape && Params.Event==IE_Pressed && !IsAnyPanelOpen())
        { UIStopRevive(); UIClosePanels(); Flow->ToggleInGameMenu(); return true; }
    }
    if (bOnlineLeaveInputBlocked) return true;

    if (bReviveInputHeld)
    {
        const FKey Input = Params.Key;
        const bool bRelease = Input == EKeys::E && Params.Event == IE_Released;
        const bool bAction = Params.Event == IE_Pressed && (Input == EKeys::W || Input == EKeys::A || Input == EKeys::S || Input == EKeys::D
            || Input == EKeys::SpaceBar || Input == EKeys::LeftShift || Input == EKeys::RightShift
            || Input == EKeys::J || Input == EKeys::K || Input == EKeys::L || Input == EKeys::O || Input == EKeys::LeftMouseButton);
        if (bRelease || bAction) UIStopRevive();
    }
    if (bSkillsOpen && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
    {
        if (Params.Key == EKeys::Left || Params.Key == EKeys::Right)
        { UISelectSkillPanelTab(1-SkillPanelTab); return true; }
        if (Params.Event == IE_Pressed && Params.Key == EKeys::F)
        { UIRequestBuildReset(); return true; }
        if (Params.Event == IE_Pressed && Params.Key == EKeys::Escape && IsBuildResetConfirmationPending())
        { UICancelBuildReset(); return true; }
    }
    if (bWorkshopOpen && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
    {
        if (Params.Key == EKeys::Left || Params.Key == EKeys::Right)
        {
            const int32 Step = Params.Key == EKeys::Left ? -1 : 1;
            UISelectWorkshopTab(static_cast<EFableWorkshopTab>((int32(WorkshopTab) + Step + 5) % 5));
            return true;
        }
        if (Params.Event == IE_Pressed)
        {
            const FKey Keys[] = {EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four};
            for (int32 Index=0; Index<4; ++Index)
                if (Params.Key == Keys[Index])
                {
                    if (WorkshopTab == EFableWorkshopTab::Craft) UISelectCraftRecipe(Index+1,CraftSlot);
                    else if (WorkshopTab == EFableWorkshopTab::Reroll && Index<3) UISelectAffix(Index);
                    return true;
                }
            if (Params.Key == EKeys::F)
            {
                if (WorkshopTab == EFableWorkshopTab::Craft)
                    UISelectCraftRecipe(CraftRarity,CraftSlot==EFableEquipmentSlot::Weapon?EFableEquipmentSlot::Armor:EFableEquipmentSlot::Weapon);
                else if (WorkshopTab == EFableWorkshopTab::Stash) UISetStashWithdrawal(!bStashWithdrawal);
                return true;
            }
        }
    }
    if (Dialogue==EFableDialogue::Forge && Params.Key==EKeys::F && Params.Event==IE_Pressed)
    { UIOpenWorkshop(); return true; }
    if (IsPortalOpen() && (Params.Event == IE_Pressed || Params.Event == IE_Repeat)
        && (Params.Key == EKeys::Left || Params.Key == EKeys::Right))
    {
        UISelectDifficulty(Params.Key == EKeys::Left ? EFableDifficulty::Normal : EFableDifficulty::Hard);
        return true;
    }
    if (bInventoryOpen && Params.Event == IE_Pressed && Params.Key == EKeys::Tab)
    {
        UICycleInventorySort();
        return true;
    }
    if (bFrontEndOpen && bJoinAddressFocused && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
    {
        const FKey Key = Params.Key;
        const bool bCtrl = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
        if (Key == EKeys::Escape) { bJoinAddressFocused = false; return true; }
        if (Key == EKeys::Enter) { UIJoinGame(); return true; }
        if (bCtrl && Key == EKeys::A) { bAddressSelectAll = true; return true; }
        if (bCtrl && Key == EKeys::V)
        {
            FString Pasted; FPlatformApplicationMisc::ClipboardPaste(Pasted);
            UISetJoinAddress(Pasted.TrimStartAndEnd()); bAddressSelectAll = false; return true;
        }
        if (Key == EKeys::BackSpace || Key == EKeys::Delete)
        {
            if (bAddressSelectAll || Key == EKeys::Delete) JoinAddress.Empty();
            else JoinAddress.LeftChopInline(1);
            bAddressSelectAll = false; return true;
        }
        FString TypedCharacter;
        const FKey Digits[] = {EKeys::Zero,EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight,EKeys::Nine};
        const FKey NumDigits[] = {EKeys::NumPadZero,EKeys::NumPadOne,EKeys::NumPadTwo,EKeys::NumPadThree,EKeys::NumPadFour,EKeys::NumPadFive,EKeys::NumPadSix,EKeys::NumPadSeven,EKeys::NumPadEight,EKeys::NumPadNine};
        for (int32 I = 0; I < 10; ++I) if (Key == Digits[I] || Key == NumDigits[I]) TypedCharacter = FString::FromInt(I);
        if (Key == EKeys::Period || Key == EKeys::Decimal) TypedCharacter = TEXT(".");
        if (Key == EKeys::Semicolon) TypedCharacter = TEXT(":");
        if (Key == EKeys::Hyphen || Key == EKeys::Subtract) TypedCharacter = TEXT("-");
        const FString Name = Key.GetFName().ToString();
        if (Name.Len() == 1 && FChar::IsAlpha(Name[0])) TypedCharacter = Name.ToLower();
        if (!TypedCharacter.IsEmpty() && !bCtrl)
        {
            if (bAddressSelectAll) JoinAddress.Empty();
            bAddressSelectAll = false;
            if (JoinAddress.Len() < 128) JoinAddress += TypedCharacter;
            return true;
        }
        // Modifier and mouse events still reach the engine to keep key state and HUD clicks correct.
        if (!Key.IsMouseButton() && !Key.IsModifierKey()) return true;
    }
    return Super::InputKey(Params);
}

void AFablePlayerController::ApplyUIMode()
{
    if (auto Flow=GetGameInstance()?GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>():nullptr; Flow && Flow->IsEnabled())
    {
        if (Flow->IsOverlayBlocking())
        {
            UIStopRevive();
            // Remove only our old Canvas panel layer; the subsystem owns Slate focus and cursor.
            if (bUIInputBlocked) { SetIgnoreLookInput(false); SetIgnoreMoveInput(false); bUIInputBlocked=false; }
            return;
        }
    }

    const bool bWantsCursor = IsAnyPanelOpen() || bFreeCursor;
    if (bWantsCursor) UIStopRevive();
    // Ignore flags are counted. Release only the layer this controller added.
    if (bWantsCursor != bUIInputBlocked)
    {
        SetIgnoreLookInput(bWantsCursor);
        SetIgnoreMoveInput(bWantsCursor);
        bUIInputBlocked = bWantsCursor;
    }
    bShowMouseCursor = bWantsCursor;
    bEnableClickEvents = bWantsCursor;
    bEnableMouseOverEvents = bWantsCursor;
    if (bWantsCursor)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
    }
    else SetInputMode(FInputModeGameOnly());
}
void AFablePlayerController::UIClosePanels()
{
    ClearBuildResetConfirmation();
    if (bWorkshopOpen) DismissTransientNotice();
    UIStopRevive();
    bInventoryOpen = bWorkshopOpen = bQuestsOpen = bHelpOpen = bSkillsOpen = bFreeCursor = false;
    ClearDismantleConfirmation();
    Dialogue = EFableDialogue::None;
    bJoinAddressFocused = false;
    ApplyUIMode();
}
void AFablePlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (const auto Profile=GetPlayerState<AFablePlayerState>(); Profile && Profile->IsBackendClosing()) BlockOnlineLeaveInput();
    if (const AFablePlayerState* State=GetPlayerState<AFablePlayerState>())
        if (ObservedNoticeSerial!=State->NoticeSerial || ObservedNoticeText!=State->LastNotice)
        {
            ObservedNoticeSerial=State->NoticeSerial; ObservedNoticeText=State->LastNotice;
            NoticeObservedAt=GetWorld()?GetWorld()->GetTimeSeconds():0.0;
        }
    if (PendingDismantleItemId!=INDEX_NONE && !IsDismantleConfirmationPending()) ClearDismantleConfirmation();
    if (bBuildResetConfirmationPending && !IsBuildResetConfirmationPending()) ClearBuildResetConfirmation();
}
void AFablePlayerController::DismissTransientNotice()
{
    if (const AFablePlayerState* State=GetPlayerState<AFablePlayerState>())
    { DismissedNoticeSerial=State->NoticeSerial; DismissedNoticeText=State->LastNotice; }
}
bool AFablePlayerController::ShouldShowTransientNotice() const
{
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (!State || State->LastNotice.IsEmpty()) return false;
    // Save failures must remain visible until the authoritative save state recovers.
    if (!State->bLastSaveSucceeded) return true;
    if (DismissedNoticeSerial==State->NoticeSerial && DismissedNoticeText==State->LastNotice) return false;
    if (ObservedNoticeSerial!=State->NoticeSerial || ObservedNoticeText!=State->LastNotice) return true;
    return GetWorld() && GetWorld()->GetTimeSeconds()-NoticeObservedAt<8.0;
}
void AFablePlayerController::ClearDismantleConfirmation()
{
    PendingDismantleItemId=INDEX_NONE;
    PendingDismantleSignature.Empty();
}
void AFablePlayerController::UIOpenWorkshop()
{
    if (bFrontEndOpen || !IsNearForge()) return;
    const AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn());
    if (!Hero || !Hero->IsAlive()) return;
    DismissTransientNotice();
    UIClosePanels(); bWorkshopOpen=true;
    WorkshopItemId=GetSelectedWorkshopItemId();
    ApplyUIMode();
}
void AFablePlayerController::UISelectWorkshopTab(EFableWorkshopTab Tab)
{
    if (!bWorkshopOpen || uint8(Tab)>uint8(EFableWorkshopTab::Stash)) return;
    if (WorkshopTab!=Tab) DismissTransientNotice();
    WorkshopTab=Tab; SelectedAffixIndex=0; ClearDismantleConfirmation();
    WorkshopItemId=GetSelectedWorkshopItemId();
}
TArray<int32> AFablePlayerController::GetWorkshopDisplayIds() const
{
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (!State) return {};
    if (WorkshopTab!=EFableWorkshopTab::Stash || !bStashWithdrawal) return GetInventoryDisplayIds();
    TArray<int32> Ids;
    for (const FFableItem& Item:State->StashItems) Ids.Add(Item.Id);
    return Ids;
}
int32 AFablePlayerController::GetSelectedWorkshopItemId() const
{
    const TArray<int32> Ids=GetWorkshopDisplayIds();
    return Ids.Contains(WorkshopItemId)?WorkshopItemId:Ids.IsEmpty()?INDEX_NONE:Ids[0];
}
void AFablePlayerController::UISelectWorkshopItem(int32 ItemId)
{
    if (!bWorkshopOpen || !GetWorkshopDisplayIds().Contains(ItemId)) return;
    if (WorkshopItemId!=ItemId) { DismissTransientNotice(); SelectedAffixIndex=0; ClearDismantleConfirmation(); }
    WorkshopItemId=ItemId;
}
void AFablePlayerController::UISelectCraftRecipe(int32 Rarity,EFableEquipmentSlot Slot)
{
    if (!bWorkshopOpen || Rarity<1 || Rarity>4 || (Slot!=EFableEquipmentSlot::Weapon && Slot!=EFableEquipmentSlot::Armor)) return;
    if (CraftRarity!=Rarity || CraftSlot!=Slot) DismissTransientNotice();
    CraftRarity=Rarity; CraftSlot=Slot; ClearDismantleConfirmation();
}
void AFablePlayerController::UISelectAffix(int32 Index)
{
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    const FFableItem* Item=State?State->FindItem(GetSelectedWorkshopItemId()):nullptr;
    if (bWorkshopOpen && Item && Item->Affixes.IsValidIndex(Index))
    { if (SelectedAffixIndex!=Index) DismissTransientNotice(); SelectedAffixIndex=Index; ClearDismantleConfirmation(); }
}
void AFablePlayerController::UISetStashWithdrawal(bool bWithdraw)
{
    if (!bWorkshopOpen) return;
    if (bStashWithdrawal!=bWithdraw) DismissTransientNotice();
    bStashWithdrawal=bWithdraw; WorkshopItemId=INDEX_NONE; SelectedAffixIndex=0; ClearDismantleConfirmation();
    WorkshopItemId=GetSelectedWorkshopItemId();
}
FString AFablePlayerController::GetDismantleConfirmationSignature() const
{
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (!State) return FString();
    FString Signature=FString::Printf(TEXT("%d:%d:%d:%d:%d:%d:%d"),State->Gold,State->Ore,State->Essence,
        State->RootCores,State->MoonCrystals,State->EquippedWeaponId,State->EquippedArmorId);
    for (const FFableItem& Item:State->Inventory) Signature+=TEXT("|")+FableRPG::ItemPersistentSignature(Item);
    Signature+=TEXT("/stash/");
    for (const FFableItem& Item:State->StashItems) Signature+=TEXT("|")+FableRPG::ItemPersistentSignature(Item);
    return Signature;
}
bool AFablePlayerController::IsDismantleConfirmationPending() const
{
    return bWorkshopOpen && WorkshopTab==EFableWorkshopTab::Dismantle && PendingDismantleItemId!=INDEX_NONE
        && PendingDismantleItemId==GetSelectedWorkshopItemId() && IsNearForge()
        && GetWorkshopBlockReason().IsEmpty() && PendingDismantleSignature==GetDismantleConfirmationSignature();
}
FString AFablePlayerController::GetWorkshopBlockReason() const
{
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn());
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    if (!State || !State->bProfileLoaded) return TEXT("모험 정보를 불러오는 중입니다.");
    if (!Hero || !Hero->IsAlive()) return TEXT("회복한 뒤 공방을 이용하세요.");
    if (Adventure && Adventure->bPartyRecoveryPending) return TEXT("마을 구조가 끝난 뒤 이용하세요.");
    if (!IsNearForge()) return TEXT("대장장이 브람에게 가까이 다가가세요.");
    const int32 Id=GetSelectedWorkshopItemId();
    switch(WorkshopTab)
    {
    case EFableWorkshopTab::Craft: return State->GetCraftBlockReason(CraftRarity,CraftSlot);
    case EFableWorkshopTab::Dismantle: return State->GetDismantleBlockReason(Id);
    case EFableWorkshopTab::Reroll: return State->GetRerollBlockReason(Id,SelectedAffixIndex);
    case EFableWorkshopTab::Repair: return State->GetRepairBlockReason(Id);
    case EFableWorkshopTab::Stash: return bStashWithdrawal?State->GetWithdrawBlockReason(Id):State->GetDepositBlockReason(Id);
    default: return TEXT("공방 작업을 선택하세요.");
    }
}
void AFablePlayerController::UICraft()
{
    if (bFrontEndOpen) return;
    ClearDismantleConfirmation(); ServerCraft(CraftRarity,CraftSlot);
}
void AFablePlayerController::UIDismantle(int32 ItemId)
{
    if (!bWorkshopOpen || WorkshopTab!=EFableWorkshopTab::Dismantle || ItemId!=GetSelectedWorkshopItemId()) return;
    if (!GetWorkshopBlockReason().IsEmpty()) { ClearDismantleConfirmation(); return; }
    if (IsDismantleConfirmationPending()) { ClearDismantleConfirmation(); ServerDismantle(ItemId); }
    else { PendingDismantleItemId=ItemId; PendingDismantleSignature=GetDismantleConfirmationSignature(); }
}
void AFablePlayerController::UIRerollAffix(int32 ItemId,int32 AffixIndex)
{ if (!bFrontEndOpen) { ClearDismantleConfirmation(); ServerRerollAffix(ItemId,AffixIndex); } }
void AFablePlayerController::UIRepair(int32 ItemId)
{ if (!bFrontEndOpen) { ClearDismantleConfirmation(); ServerRepair(ItemId); } }
void AFablePlayerController::UIDeposit(int32 ItemId)
{ if (!bFrontEndOpen) { ClearDismantleConfirmation(); ServerDeposit(ItemId); } }
void AFablePlayerController::UIWithdraw(int32 ItemId)
{ if (!bFrontEndOpen) { ClearDismantleConfirmation(); ServerWithdraw(ItemId); } }
void AFablePlayerController::UIWorkshopAction()
{
    if (!bWorkshopOpen) return;
    const int32 Id=GetSelectedWorkshopItemId();
    switch(WorkshopTab)
    {
    case EFableWorkshopTab::Craft: UICraft(); break;
    case EFableWorkshopTab::Dismantle: UIDismantle(Id); break;
    case EFableWorkshopTab::Reroll: UIRerollAffix(Id,SelectedAffixIndex); break;
    case EFableWorkshopTab::Repair: UIRepair(Id); break;
    case EFableWorkshopTab::Stash: if(bStashWithdrawal)UIWithdraw(Id);else UIDeposit(Id); break;
    default: break;
    }
}
void AFablePlayerController::UIToggleInventory()
{
    if (bFrontEndOpen) return;
    const bool bOpen = !bInventoryOpen;
    UIClosePanels(); bInventoryOpen = bOpen;
    if (bOpen) SelectedItemId = GetSelectedItemId();
    ApplyUIMode();
}
void AFablePlayerController::UIToggleQuests()
{
    if (bFrontEndOpen) return;
    const bool bOpen = !bQuestsOpen;
    UIClosePanels(); bQuestsOpen = bOpen; ApplyUIMode();
}
void AFablePlayerController::UIToggleHelp()
{
    const bool bOpen = !bHelpOpen;
    UIClosePanels(); bHelpOpen = bOpen; ApplyUIMode();
}
void AFablePlayerController::UIToggleSkills()
{
    if (bFrontEndOpen) return;
    const bool bOpen = !bSkillsOpen;
    UIClosePanels(); bSkillsOpen = bOpen;
    SkillPanelTab = 0;
    SelectedSkillSlot = FMath::Clamp(SelectedSkillSlot, 0, 3);
    ApplyUIMode();
}
void AFablePlayerController::UISelectSkill(int32 Slot)
{
    if (!bSkillsOpen || Slot < 0 || Slot > 3) return;
    ClearBuildResetConfirmation(); SkillPanelTab=0; SelectedSkillSlot=Slot;
}
void AFablePlayerController::UISelectSkillPanelTab(int32 Tab)
{
    if (!bSkillsOpen || Tab < 0 || Tab > 1) return;
    if (SkillPanelTab!=Tab) ClearBuildResetConfirmation();
    SkillPanelTab=Tab;
}
void AFablePlayerController::UISelectTalent(int32 TargetTier, EFableTalentPath Path)
{
    if (!bSkillsOpen || (TargetTier!=2 && TargetTier!=3)
        || (Path!=EFableTalentPath::First && Path!=EFableTalentPath::Second)) return;
    ClearBuildResetConfirmation(); SkillPanelTab=1;
    SelectedTalentTier=TargetTier; SelectedTalentPath=Path;
}
void AFablePlayerController::UIChooseTalent()
{
    if (bFrontEndOpen || !bSkillsOpen || SkillPanelTab!=1) return;
    ClearBuildResetConfirmation();
    ServerSelectTalent(SelectedTalentTier,SelectedTalentPath);
}
void AFablePlayerController::ClearBuildResetConfirmation()
{
    bBuildResetConfirmationPending=false;
    PendingBuildResetRevision=INDEX_NONE;
}
void AFablePlayerController::UICancelBuildReset() { ClearBuildResetConfirmation(); }
bool AFablePlayerController::IsBuildResetConfirmationPending() const
{
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    return bBuildResetConfirmationPending && bSkillsOpen && State
        && State->HeroClass==PendingBuildResetClass && State->ClassBuildRevision==PendingBuildResetRevision
        && State->Gold==PendingBuildResetGold
        && State->GetCurrentClassRefundableSkillPoints()==PendingBuildResetRefund
        && State->GetBuildResetBlockReason().IsEmpty();
}
void AFablePlayerController::UIRequestBuildReset()
{
    if (bFrontEndOpen || !bSkillsOpen) return;
    ClearBuildResetConfirmation();
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (!State || !State->GetBuildResetBlockReason().IsEmpty()) return;
    PendingBuildResetClass=State->HeroClass;
    PendingBuildResetRevision=State->ClassBuildRevision;
    PendingBuildResetGold=State->Gold;
    PendingBuildResetRefund=State->GetCurrentClassRefundableSkillPoints();
    bBuildResetConfirmationPending=true;
}
void AFablePlayerController::UIConfirmBuildReset()
{
    if (!IsBuildResetConfirmationPending()) { ClearBuildResetConfirmation(); return; }
    const EFableClass ExpectedClass=PendingBuildResetClass;
    const int32 ExpectedRevision=PendingBuildResetRevision;
    ClearBuildResetConfirmation();
    ServerResetCurrentClassBuild(ExpectedClass,ExpectedRevision);
}
void AFablePlayerController::UIUpgradeSkill(int32 Slot)
{
    ClearBuildResetConfirmation();
    if (!bFrontEndOpen) ServerUpgradeSkill(Slot);
}
void AFablePlayerController::UIAdvanceClass()
{
    if (!bFrontEndOpen) ServerAdvanceClass();
}
void AFablePlayerController::ToggleCursor()
{
    if (IsAnyPanelOpen()) { UIClosePanels(); return; }
    bFreeCursor = !bFreeCursor; ApplyUIMode();
}
void AFablePlayerController::UISetInitialClass(EFableClass HeroClass)
{
    if (HeroClass == EFableClass::Warrior || HeroClass == EFableClass::Mage || HeroClass == EFableClass::Archer) InitialClass = HeroClass;
    bJoinAddressFocused = false;
}
void AFablePlayerController::UISetJoinAddress(const FString& Address) { JoinAddress = Address.Left(128); }
void AFablePlayerController::UIFocusJoinAddress() { bJoinAddressFocused = true; bAddressSelectAll = true; }
void AFablePlayerController::UIStartNewGame(bool bHost)
{
    if (!bFrontEndOpen) return;
    if (UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance())) GI->StartNewGame(InitialClass, bHost);
}
void AFablePlayerController::UIContinueGame(bool bHost)
{
    if (!bFrontEndOpen) return;
    if (UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance())) GI->ContinueGame(bHost);
}
void AFablePlayerController::UIJoinGame()
{
    if (!bFrontEndOpen) return;
    if (UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance())) GI->JoinGame(JoinAddress);
}
void AFablePlayerController::UIReturnToFrontEnd()
{
    const auto Profile=GetPlayerState<AFablePlayerState>();
    if (Profile && Profile->IsBackendEnabled())
    {
        if (auto Flow=GetGameInstance()?GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>():nullptr; Flow && Flow->IsEnabled()) Flow->RequestLeave();
        else UIRequestOnlineLeave();
        return;
    }
    UIStopRevive();
    if (auto GI=Cast<UFableGameInstance>(GetGameInstance())) GI->ReturnToFrontEnd();
}
void AFablePlayerController::BlockOnlineLeaveInput()
{
    if (!bOnlineLeaveInputBlocked)
    {
        bOnlineLeaveInputBlocked=true;
        SetIgnoreMoveInput(true);SetIgnoreLookInput(true);
        UIStopRevive();
    }
    if (auto Hero=Cast<AFableCharacter>(GetPawn()))
    {
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->GetCharacterMovement()->DisableMovement();
    }
}
void AFablePlayerController::UIRequestOnlineLeave()
{
    if (!IsLocalController() || bOnlineLeavePending) return;
    const auto Profile=GetPlayerState<AFablePlayerState>();
    if (!Profile || !Profile->IsBackendEnabled())
    {
        if (auto Flow=GetGameInstance()?GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>():nullptr)
            Flow->OnOnlineLeaveResult(false,TEXT("중앙 서버의 캐릭터 접속을 확인한 뒤 다시 시도해 주세요."));
        return;
    }
    UIClosePanels();BlockOnlineLeaveInput();
    bOnlineLeavePending=true;OnlineLeaveRequestId=FGuid::NewGuid();
    ServerRequestOnlineLeave(OnlineLeaveRequestId);
}
void AFablePlayerController::ServerRequestOnlineLeave_Implementation(FGuid RequestId)
{
    if (!HasAuthority() || !RequestId.IsValid() || !GetWorld()) return;
    if (bServerOnlineLeavePending)
    {
        if (RequestId!=ServerOnlineLeaveRequestId)
            ClientOnlineLeaveResult(RequestId,false,TEXT("기존 퇴장 요청을 확인하고 있습니다."));
        return;
    }
    const double Now=FPlatformTime::Seconds();
    if (Now-LastOnlineLeaveRequestTime<0.5)
    {ClientOnlineLeaveResult(RequestId,false,TEXT("잠시 뒤 퇴장 확인을 다시 요청해 주세요."));return;}
    LastOnlineLeaveRequestTime=Now;
    auto Profile=GetPlayerState<AFablePlayerState>();
    if (!Profile || !Profile->IsBackendEnabled() || !Profile->bProfileLoaded)
    {ClientOnlineLeaveResult(RequestId,false,TEXT("중앙 프로필 접속이 완료되지 않았습니다."));return;}
    bServerOnlineLeavePending=true;ServerOnlineLeaveRequestId=RequestId;
    if (auto Hero=Cast<AFableCharacter>(GetPawn())) Hero->CancelRevive();
    BlockOnlineLeaveInput();
    TWeakObjectPtr<AFablePlayerController> Weak(this);
    Profile->RequestBackendLeave([Weak,RequestId](bool bSuccess,const FString& Reason)
    {
        if (auto PC=Weak.Get(); PC && PC->ServerOnlineLeaveRequestId==RequestId)
        {
            PC->bServerOnlineLeavePending=false;
            PC->ServerOnlineLeaveRequestId.Invalidate();
            PC->ClientOnlineLeaveResult(RequestId,bSuccess,Reason);
        }
    });
}
void AFablePlayerController::ClientOnlineLeaveResult_Implementation(FGuid RequestId,bool bSuccess,const FString& Reason)
{
    if (!bOnlineLeavePending || RequestId!=OnlineLeaveRequestId) return;
    bOnlineLeavePending=false;OnlineLeaveRequestId.Invalidate();
    // Both success and failure keep gameplay frozen; only the flow subsystem may travel on a confirmed success.
    if (auto Flow=GetGameInstance()?GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>():nullptr; Flow && Flow->IsEnabled())
        Flow->OnOnlineLeaveResult(bSuccess,Reason);
    else if (bSuccess)
        if (auto GI=Cast<UFableGameInstance>(GetGameInstance())) GI->ReturnToFrontEnd();
}


TArray<int32> AFablePlayerController::GetInventoryDisplayIds() const
{
    TArray<int32> DisplayIds;
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (!State) return DisplayIds;
    for (const FFableItem& Item : State->Inventory) DisplayIds.Add(Item.Id);
    if (InventorySort == EFableInventorySort::Acquired) return DisplayIds;
    DisplayIds.Sort([State, this](int32 LeftId, int32 RightId)
    {
        const FFableItem* Left = State->FindItem(LeftId);
        const FFableItem* Right = State->FindItem(RightId);
        if (!Left || !Right) return LeftId < RightId;
        if (InventorySort == EFableInventorySort::Quality && Left->Quality != Right->Quality)
            return uint8(Left->Quality) > uint8(Right->Quality);
        if (InventorySort == EFableInventorySort::Quality && Left->Rarity != Right->Rarity)
            return Left->Rarity > Right->Rarity;
        if (Left->GetEffectivePower() != Right->GetEffectivePower())
            return Left->GetEffectivePower() > Right->GetEffectivePower();
        return LeftId < RightId;
    });
    return DisplayIds;
}
void AFablePlayerController::UICycleInventorySort()
{
    if (!bInventoryOpen) return;
    const int32 RetainedId = GetSelectedItemId();
    InventorySort = static_cast<EFableInventorySort>((uint8(InventorySort) + 1) % 3);
    SelectedItemId = RetainedId;
}
FString AFablePlayerController::GetInventorySortLabel() const
{
    return InventorySort == EFableInventorySort::Quality ? TEXT("품질 높은 순") :
        InventorySort == EFableInventorySort::Power ? TEXT("성능 높은 순") : TEXT("획득 순");
}
int32 AFablePlayerController::GetSelectedItemId() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (!State || State->Inventory.IsEmpty()) return INDEX_NONE;
    if (State->FindItem(SelectedItemId)) return SelectedItemId;
    const TArray<int32> DisplayIds = GetInventoryDisplayIds();
    return DisplayIds.IsEmpty() ? INDEX_NONE : DisplayIds[0];
}
void AFablePlayerController::UISelectItem(int32 ItemId)
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (bInventoryOpen && State && State->FindItem(ItemId)) SelectedItemId = ItemId;
}
void AFablePlayerController::UISelectPrevious()
{
    if (bWorkshopOpen)
    {
        if (WorkshopTab==EFableWorkshopTab::Craft) { UISelectCraftRecipe(FMath::Clamp(CraftRarity+(-1),1,4),CraftSlot); return; }
        const TArray<int32> Ids=GetWorkshopDisplayIds();
        if (!Ids.IsEmpty()) UISelectWorkshopItem(Ids[FMath::Clamp(Ids.IndexOfByKey(GetSelectedWorkshopItemId())+(-1),0,Ids.Num()-1)]);
        return;
    }

    if (bSkillsOpen)
    {
        if (SkillPanelTab==0) UISelectSkill(FMath::Max(0,SelectedSkillSlot-1));
        else { const int32 Row=FMath::Max(0,(SelectedTalentTier-2)*2+int32(SelectedTalentPath)-2); UISelectTalent(2+Row/2,static_cast<EFableTalentPath>(1+Row%2)); }
        return;
    }
    if (IsPortalOpen()) { UISelectDungeon(EFableRegion::RootRuins); return; }
    if (bQuestsOpen || Dialogue==EFableDialogue::Guide) { UISelectQuest(static_cast<EFableQuestId>(FMath::Max(0,int32(GetSelectedQuestId())-1))); return; }
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (!bInventoryOpen || !State || State->Inventory.IsEmpty()) return;
    const TArray<int32> DisplayIds = GetInventoryDisplayIds();
    const int32 Current = DisplayIds.IndexOfByKey(GetSelectedItemId());
    SelectedItemId = DisplayIds[FMath::Max(0, Current - 1)];
}
void AFablePlayerController::UISelectNext()
{
    if (bWorkshopOpen)
    {
        if (WorkshopTab==EFableWorkshopTab::Craft) { UISelectCraftRecipe(FMath::Clamp(CraftRarity+(1),1,4),CraftSlot); return; }
        const TArray<int32> Ids=GetWorkshopDisplayIds();
        if (!Ids.IsEmpty()) UISelectWorkshopItem(Ids[FMath::Clamp(Ids.IndexOfByKey(GetSelectedWorkshopItemId())+(1),0,Ids.Num()-1)]);
        return;
    }

    if (bSkillsOpen)
    {
        if (SkillPanelTab==0) UISelectSkill(FMath::Min(3,SelectedSkillSlot+1));
        else { const int32 Row=FMath::Min(3,(SelectedTalentTier-2)*2+int32(SelectedTalentPath)); UISelectTalent(2+Row/2,static_cast<EFableTalentPath>(1+Row%2)); }
        return;
    }
    if (IsPortalOpen()) { UISelectDungeon(EFableRegion::Moonwell); return; }
    if (bQuestsOpen || Dialogue==EFableDialogue::Guide) { UISelectQuest(static_cast<EFableQuestId>(FMath::Min(int32(EFableQuestId::Story),int32(GetSelectedQuestId())+1))); return; }
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (!bInventoryOpen || !State || State->Inventory.IsEmpty()) return;
    const TArray<int32> DisplayIds = GetInventoryDisplayIds();
    const int32 Current = DisplayIds.IndexOfByKey(GetSelectedItemId());
    SelectedItemId = DisplayIds[FMath::Min(DisplayIds.Num() - 1, Current + 1)];
}
bool AFablePlayerController::IsNearTownBoard() const { return NearestService(this) == EFableService::Board; }
bool AFablePlayerController::IsNearForge() const { return NearestService(this) == EFableService::Forge; }
bool AFablePlayerController::IsNearMerchant() const { return NearestService(this) == EFableService::Merchant; }
bool AFablePlayerController::IsNearTownService() const { return IsNearTownBoard() || IsNearForge(); }

int32 AFablePlayerController::GetNearbyStoryObjectIndex() const
{
    const APawn* HeroPawn=GetPawn();
    if (!HeroPawn) return INDEX_NONE;
    float Best=FMath::Square(330.f); int32 Selected=INDEX_NONE;
    for (int32 Index=0;Index<5;++Index)
    {
        const FVector Delta=HeroPawn->GetActorLocation()-FableRPG::StoryObjectLocation(Index);
        const float Distance=Delta.SizeSquared2D();
        if (Distance<=Best && FMath::Abs(Delta.Z)<300.f) { Best=Distance; Selected=Index; }
    }
    return Selected;
}
bool AFablePlayerController::GetNavigationTarget(FVector& OutLocation,FString& OutLabel) const
{
    OutLabel.Empty();
    const AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn());
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    if (!Hero || !State) return false;
    if (Adventure && Adventure->bPartyRecoveryPending) { OutLabel=TEXT("마을 구조를 기다리는 중"); return false; }
    if (!Hero->IsAlive()) { OutLabel=TEXT("동료의 구조를 기다리는 중"); return false; }
    const EFableRegion Region=PawnRegion(this);
    float Nearest=TNumericLimits<float>::Max(); const AFableCharacter* DownedAlly=nullptr;
    if (Adventure)
        for (APlayerState* Member:Adventure->PlayerArray)
            if (const AFableCharacter* Ally=Member?Cast<AFableCharacter>(Member->GetPawn()):nullptr)
                if (Ally!=Hero && Ally->IsDowned() && FableRPG::IsInRegion(Region,Ally->GetActorLocation()))
                {
                    const float Distance=FVector::DistSquared2D(Hero->GetActorLocation(),Ally->GetActorLocation());
                    if (Distance<Nearest) { Nearest=Distance; DownedAlly=Ally; }
                }
    if (DownedAlly) { OutLocation=DownedAlly->GetActorLocation(); OutLabel=TEXT("쓰러진 동료 구조"); return true; }
    const bool bCombat=Adventure && ((Region!=EFableRegion::Village && Adventure->ActiveRegion==Region
        && (Adventure->AdventureStage==EFableAdventureStage::Dungeon || Adventure->AdventureStage==EFableAdventureStage::Boss))
        || (Region==EFableRegion::Village && State->StoryChapter==2 && State->ForestQuest==EFableQuestStage::Active));
    if (bCombat)
    {
        Nearest=TNumericLimits<float>::Max(); const AFableEnemy* Target=nullptr;
        for (TActorIterator<AFableEnemy> It(GetWorld());It;++It)
            if (It->IsAlive() && FableRPG::IsInRegion(Region,It->GetActorLocation()))
            {
                const float Distance=FVector::DistSquared2D(Hero->GetActorLocation(),It->GetActorLocation());
                if (Distance<Nearest) { Nearest=Distance; Target=*It; }
            }
        if (Target) { OutLocation=Target->GetActorLocation(); OutLabel=TEXT("현재 전투의 적"); return true; }
        if (Region==EFableRegion::Moonwell && Adventure->ActiveSealIndex>=0 && Adventure->ActiveSealIndex<3
            && Adventure->AdventureStage==EFableAdventureStage::Dungeon)
        {
            OutLocation=FableRPG::MoonSealLocation(Adventure->ActiveSealIndex);
            OutLabel=FString::Printf(TEXT("봉인 %d · E 정화"),Adventure->ActiveSealIndex+1); return true;
        }
    }
    if (State->GetStoryTarget(OutLocation,OutLabel)) return true;
    OutLabel.Empty(); return false;
}
FString AFablePlayerController::GetContextActionText() const
{
    if (bWorkshopOpen) return TEXT("←→ 공방 탭 · ↑↓ 선택 · E 작업 · Esc 닫기");
    if (bSkillsOpen) return IsBuildResetConfirmationPending()?TEXT("E 현재 직업 초기화 확정 · Esc 취소"):
        SkillPanelTab==0?TEXT("←→ 탭 · ↑↓ 스킬 · E 강화 · F 초기화 · P / Esc 닫기"):
        TEXT("←→ 탭 · ↑↓ 특성 · E 선택 · F 초기화 · P / Esc 닫기");
    if (bInventoryOpen) return TEXT("↑↓ 선택 · Tab 정렬 · E 장착 · U 강화 · X 판매 · I / Esc 닫기");
    if (bQuestsOpen) return TEXT("↑↓ 의뢰 선택 · 수락과 보상은 로완에게 · Q / Esc 닫기");
    if (IsPortalOpen()) return TEXT("↑↓ 지역 · ←→ 난이도 · E / R 입장 · Esc 닫기");
    if (Dialogue==EFableDialogue::Guide) return TEXT("↑↓ 의뢰 선택 · E 수락 / 보고 · Esc 닫기");
    if (Dialogue==EFableDialogue::Forge) return TEXT("E 가방 / 강화 · F 공방 / 창고 · Esc 닫기");
    if (IsDialogueOpen()) return TEXT("버튼을 눌러 대화 선택 · Esc 닫기");
    const AFableGameState* Adventure = GetWorld() ? GetWorld()->GetGameState<AFableGameState>() : nullptr;
    const AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn());
    if (Adventure && Adventure->bPartyRecoveryPending) return GetRetryActionText();
    if (Hero && Hero->IsDowned()) return TEXT("쓰러졌습니다 · 동료가 가까이에서 E를 3초 누르면 구조합니다");
    if (Hero && Hero->IsDefeated()) return TEXT("전투 불능 · 잠시 후 마을로 구조됩니다");
    if (Hero && Hero->GetReviveTarget()) return TEXT("E를 계속 누르세요 · 이동 / 공격 / 피격 시 구조가 취소됩니다");
    if (!IsAnyPanelOpen() && GetNearbyReviveTarget()) return TEXT("E를 3초 동안 누르기 · 쓰러진 동료 구조");
    if (Adventure && Adventure->bRetryAvailable) return GetRetryActionText();
    const AFablePlayerState* StoryState=GetPlayerState<AFablePlayerState>();
    if (NearestService(this)==EFableService::StoryObject)
    {
        const int32 Index=GetNearbyStoryObjectIndex();
        if (Index>=0 && StoryState)
            return StoryState->CanUseStoryObject(Index)?TEXT("E ")+FableRPG::StoryObjectName(Index)+(Index<3?TEXT(" 수집"):TEXT(" 조사")):
                FableRPG::StoryObjectName(Index)+TEXT(" · Q 메인 이야기에서 다음 목표 확인");
    }
    const EFableRegion Region=PawnRegion(this);
    if (Region==EFableRegion::Moonwell && StoryState && StoryState->StoryChapter==4 && Adventure)
    {
        const int32 Seal=NearbyMoonSeal(this);
        if (Seal>=0 && (Adventure->MoonSealMask&(1<<Seal))!=0 && (StoryState->StorySealMask&(1<<Seal))==0)
            return FString::Printf(TEXT("E 정화된 봉인 %d 조사 · 내 이야기 진행에 기록"),Seal+1);
    }
    if (Region!=EFableRegion::Village)
    {
        if (Adventure && Adventure->AdventureStage==EFableAdventureStage::Cleared)
            return NearestService(this)==EFableService::ReturnPortal ? TEXT("E 마을로 귀환 · 로완에게 의뢰 보상 받기") : TEXT("귀환 포털로 이동해 E를 누르세요");
        if (Region==EFableRegion::Moonwell && Adventure && Adventure->ActiveSealIndex>=0 && Adventure->ActiveSealIndex<3
            && Adventure->AdventureStage==EFableAdventureStage::Dungeon)
        {
            const bool bReady=Adventure->RequiredEnemies>0 && Adventure->DefeatedEnemies>=Adventure->RequiredEnemies;
            return bReady ? FString::Printf(TEXT("봉인 %d에 다가가 E로 정화하세요"),Adventure->ActiveSealIndex+1)
                : FString::Printf(TEXT("달빛 수원 · 봉인 %d의 적 %d / %d 처치"),Adventure->ActiveSealIndex+1,Adventure->DefeatedEnemies,Adventure->RequiredEnemies);
        }
        return FableRPG::RegionName(Region)+TEXT("의 적을 처치하세요 · 주황색 공격 범위는 회피!");
    }
    switch (NearestService(this))
    {
    case EFableService::Board: return TEXT("E 로완과 의뢰 선택 · 1 전사 / 2 마법사 / 3 궁수");
    case EFableService::Forge: return TEXT("E 대장장이 브람 · 강화 / 제작 / 수리 / 창고");
    case EFableService::Merchant: return TEXT("E 상인 미라와 대화 · 물약과 장비 거래");
    case EFableService::Portal: return TEXT("E / R 원정 지역 선택 · 모든 동료가 해당 의뢰를 받아야 합니다");
    default: break;
    }
    if (const AFablePlayerState* State=GetPlayerState<AFablePlayerState>())
    {
        if (State->StoryChapter<5) return TEXT("메인 · ")+State->GetStoryObjective()+TEXT(" · Q 자세히");
        for (int32 Index=0;Index<=int32(EFableQuestId::Story);++Index)
            if (State->GetQuestStage(static_cast<EFableQuestId>(Index))==EFableQuestStage::Ready)
                return TEXT("마을의 로완에게 돌아가 의뢰 보상 또는 전직을 보고하세요");
        if (State->ForestQuest==EFableQuestStage::Active) return TEXT("마을 북쪽 숲의 수호자 3마리를 처치하세요");
        if (State->MoonwellQuest==EFableQuestStage::Active) return TEXT("동쪽 포털에서 달빛 수원을 선택해 입장하세요");
        if (State->DungeonQuest==EFableQuestStage::Active) return TEXT("동쪽 포털에서 뿌리 유적을 선택해 입장하세요");
        if (State->GetQuestStage(EFableQuestId::SecondAdvancement)==EFableQuestStage::Active
            || State->GetQuestStage(EFableQuestId::ThirdAdvancement)==EFableQuestStage::Active)
            return TEXT("전직 시험 진행 중 · 실제 적에게 스킬 적중 · Q에서 조건 확인");
    }
    return TEXT("로완에게 다가가 E로 모험 또는 전직 의뢰를 선택하세요");
}

void AFablePlayerController::UIInteract()
{
    if (bFrontEndOpen || bHelpOpen || bQuestsOpen) return;
    if (bWorkshopOpen) { UIWorkshopAction(); return; }
    if (bSkillsOpen)
    {
        if (IsBuildResetConfirmationPending()) UIConfirmBuildReset();
        else if (SkillPanelTab==1) UIChooseTalent();
        else UIUpgradeSkill(SelectedSkillSlot);
        return;
    }
    if (bInventoryOpen) { UIEquip(GetSelectedItemId()); return; }
    if (IsPortalOpen()) { UIEnterSelectedDungeon(); return; }
    if (Dialogue == EFableDialogue::Guide) { UIConfirmQuest(); return; }
    if (Dialogue == EFableDialogue::Forge) { UIToggleInventory(); return; }
    if (!IsDialogueOpen())
    {
        if (!bFreeCursor)
            if (AFableCharacter* Target = GetNearbyReviveTarget()) { UIStartRevive(Target); return; }
        ServerInteract();
    }
}
void AFablePlayerController::UIStartRevive(AFableCharacter* Target)
{
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    if (bFrontEndOpen || IsAnyPanelOpen() || bFreeCursor || !Hero || !Hero->IsAlive()) return;
    // The target is intentionally passed to the authoritative character for all validation.
    bReviveInputHeld = true;
    ServerStartRevive(Target);
}
void AFablePlayerController::UIStopRevive()
{
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    if (!bReviveInputHeld && (!Hero || !Hero->GetReviveTarget())) return;
    bReviveInputHeld = false;
    ServerCancelRevive();
}
AFableCharacter* AFablePlayerController::GetNearbyReviveTarget() const
{
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const AFableGameState* Adventure = GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    if (!Hero || !Hero->IsAlive() || !Adventure || Adventure->bPartyRecoveryPending) return nullptr;
    AFableCharacter* Nearest = nullptr;
    float Best = FMath::Square(230.f);
    for (APlayerState* Member : Adventure->PlayerArray)
    {
        AFableCharacter* Candidate = Member?Cast<AFableCharacter>(Member->GetPawn()):nullptr;
        if (!Candidate || Candidate==Hero || !Candidate->IsDowned()
            || (Candidate->GetReviver() && Candidate->GetReviver()!=Hero)) continue;
        const FVector Delta=Candidate->GetActorLocation()-Hero->GetActorLocation();
        if (Delta.SizeSquared2D()<=Best && FMath::Abs(Delta.Z)<=150.f)
        { Best=Delta.SizeSquared2D(); Nearest=Candidate; }
    }
    return Nearest;
}
void AFablePlayerController::UIRetryEncounter()
{
    if (bFrontEndOpen) return;
    UIStopRevive();
    ServerRetryEncounter();
    UIClosePanels();
}
bool AFablePlayerController::CanRetryEncounter() const
{
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    const AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn());
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (!Adventure || !Adventure->bRetryAvailable || Adventure->bPartyRecoveryPending || !Hero || !Hero->IsAlive() || !State || !State->bProfileLoaded) return false;
    const bool bForest=Adventure->RetryRegion==EFableRegion::Village;
    if (bForest && (!IsNearTownBoard() || State->ForestQuest!=EFableQuestStage::Active)) return false;
    if (!bForest && NearestService(this)!=EFableService::Portal) return false;
    if (Adventure->PlayerArray.IsEmpty()) return false;
    for (APlayerState* Member:Adventure->PlayerArray)
    {
        const AFablePlayerState* Profile=Cast<AFablePlayerState>(Member);
        const AFableCharacter* MemberHero=Member?Cast<AFableCharacter>(Member->GetPawn()):nullptr;
        if (!Profile || !Profile->bProfileLoaded || !MemberHero || !MemberHero->IsAlive()
            || (!bForest && !Profile->IsRegionEligible(Adventure->RetryRegion))) return false;
    }
    return true;
}
FString AFablePlayerController::GetRetryActionText() const
{
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    if (!Adventure) return FString();
    if (Adventure->bPartyRecoveryPending)
        return FString::Printf(TEXT("파티 구조 중 · 마을 복귀까지 %.0f초"),Adventure->GetPartyRecoveryTimeRemaining());
    if (!Adventure->bRetryAvailable) return FString();
    const bool bForest=Adventure->RetryRegion==EFableRegion::Village;
    const FString Region=bForest?TEXT("숲 순찰"):FableRPG::RegionName(Adventure->RetryRegion)+TEXT(" · ")+FableRPG::DifficultyName(Adventure->RetryDifficulty);
    if (CanRetryEncounter()) return TEXT("R ")+Region+TEXT(" 재도전 · 획득한 성장과 장비 유지");
    for (APlayerState* Member:Adventure->PlayerArray)
    {
        const AFablePlayerState* Profile=Cast<AFablePlayerState>(Member);
        const AFableCharacter* MemberHero=Member?Cast<AFableCharacter>(Member->GetPawn()):nullptr;
        if (!Profile || !Profile->bProfileLoaded || !MemberHero || !MemberHero->IsAlive())
            return Region+TEXT(" 재도전 · 모든 동료의 접속과 회복을 기다리세요");
    }
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (bForest && State && State->ForestQuest!=EFableQuestStage::Active)
        return TEXT("숲 순찰 재도전 · 진행 중인 의뢰를 가진 동료가 로완에게 R");
    if (!bForest && State && !State->IsRegionEligible(Adventure->RetryRegion))
        return Region+TEXT(" 재도전 · 먼저 로완에게 해당 의뢰를 준비하세요");
    return bForest?TEXT("전멸 후 재정비 · 로완 근처에서 R로 숲 순찰 재도전"):
        TEXT("전멸 후 재정비 · 모두 의뢰 준비 후 마을 포털에서 R로 ")+Region+TEXT(" 재도전");
}
EFableQuestId AFablePlayerController::GetSelectedQuestId() const
{
    if (bHasExplicitQuestSelection) return SelectedQuestId;
    return EFableQuestId::Story;
}
void AFablePlayerController::UISelectQuest(EFableQuestId QuestId)
{
    if (uint8(QuestId)>uint8(EFableQuestId::Story)) return;
    SelectedQuestId=QuestId; bHasExplicitQuestSelection=true;
}
void AFablePlayerController::UIConfirmQuest()
{
    if (bFrontEndOpen) return;
    ServerConfirmQuest(GetSelectedQuestId()); UIClosePanels();
}
void AFablePlayerController::UISelectDungeon(EFableRegion Region)
{
    if (Region==EFableRegion::RootRuins || Region==EFableRegion::Moonwell) SelectedDungeonId=Region;
}
void AFablePlayerController::UISelectDifficulty(EFableDifficulty Difficulty)
{
    if (bFrontEndOpen || IsDifficultySelectionLocked()) return;
    if (Difficulty == EFableDifficulty::Normal || Difficulty == EFableDifficulty::Hard) SelectedDifficulty = Difficulty;
}
EFableDifficulty AFablePlayerController::GetSelectedDifficulty() const
{
    const AFableGameState* Adventure = GetWorld() ? GetWorld()->GetGameState<AFableGameState>() : nullptr;
    if (Adventure && Adventure->ActiveRegion != EFableRegion::Village) return Adventure->ActiveDifficulty;
    return SelectedDifficulty;
}
bool AFablePlayerController::IsDifficultySelectionLocked() const
{
    const AFableGameState* Adventure = GetWorld() ? GetWorld()->GetGameState<AFableGameState>() : nullptr;
    return Adventure && (Adventure->bPartyRecoveryPending || Adventure->ActiveRegion != EFableRegion::Village);
}
void AFablePlayerController::UIEnterSelectedDungeon()
{
    if (bFrontEndOpen) return;
    ServerEnterRegion(SelectedDungeonId, GetSelectedDifficulty()); UIClosePanels();
}
bool AFablePlayerController::CanEnterDungeon(EFableRegion Region) const
{
    if (Region!=EFableRegion::RootRuins && Region!=EFableRegion::Moonwell) return false;
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    if (!Adventure || Adventure->bPartyRecoveryPending || Adventure->PlayerArray.IsEmpty() || NearestService(this)!=EFableService::Portal) return false;
    if (Adventure->ActiveRegion!=EFableRegion::Village)
    {
        const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
        const AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn());
        return Adventure->ActiveRegion==Region && State && State->bProfileLoaded && Hero && Hero->IsAlive()
            && (State->IsRegionEligible(Region) || Adventure->AdventureStage==EFableAdventureStage::Cleared);
    }
    for (const APlayerState* Other:Adventure->PlayerArray)
    {
        const AFablePlayerState* Profile=Cast<AFablePlayerState>(Other);
        const AFableCharacter* Hero=Other?Cast<AFableCharacter>(Other->GetPawn()):nullptr;
        if (!Profile || !Profile->bProfileLoaded || !Profile->IsRegionEligible(Region) || !Hero || !Hero->IsAlive()) return false;
    }
    return true;
}
FString AFablePlayerController::GetDungeonReadinessText(EFableRegion Region) const
{
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    const AFablePlayerState* State=GetPlayerState<AFablePlayerState>();
    if (!Adventure || !State || !State->bProfileLoaded) return TEXT("동료 정보를 불러오는 중입니다.");
    if (Adventure->bPartyRecoveryPending) return GetRetryActionText();
    if (Adventure->ActiveRegion!=EFableRegion::Village)
    {
        if (Adventure->ActiveRegion!=Region) return TEXT("동료가 다른 지역을 탐험 중입니다. 현재 원정을 마친 뒤 입장하세요.");
        if (Adventure->AdventureStage==EFableAdventureStage::Cleared) return TEXT("원정 완료 · 마을 복귀에 실패했다면 입장 버튼으로 귀환을 다시 시도하세요.");
        return State->IsRegionEligible(Region)?TEXT("동료가 이 지역을 탐험 중입니다. 입장 버튼으로 합류를 다시 시도하세요."):
            TEXT("동료가 이 지역을 탐험 중입니다. 로완에게 해당 의뢰를 받은 뒤 합류하세요.");
    }
    int32 Ready=0;
    bool bWaiting=false,bFallen=false;
    for (const APlayerState* Other:Adventure->PlayerArray)
    {
        const AFablePlayerState* Profile=Cast<AFablePlayerState>(Other);
        const AFableCharacter* Hero=Other?Cast<AFableCharacter>(Other->GetPawn()):nullptr;
        bWaiting|=!Profile || !Profile->bProfileLoaded || !Hero;
        bFallen|=Hero && !Hero->IsAlive();
        if (Profile && Profile->bProfileLoaded && Profile->IsRegionEligible(Region) && Hero && Hero->IsAlive()) ++Ready;
    }
    const FString Count=FString::Printf(TEXT("파티 준비 %d / %d"),Ready,Adventure->PlayerArray.Num());
    if (bWaiting) return Count+TEXT(" · 동료 접속을 기다리세요.");
    if (bFallen) return Count+TEXT(" · 쓰러진 동료가 회복해야 합니다.");
    if (!State->IsRegionEligible(Region))
    {
        if (Region==EFableRegion::RootRuins && State->ForestQuest!=EFableQuestStage::Completed)
            return Count+TEXT(" · 먼저 로완에게 숲 순찰 보상을 받으세요.");
        if (Region==EFableRegion::Moonwell && State->DungeonCompletions<1)
            return Count+TEXT(" · 먼저 로완에게 뿌리 유적 보상을 받으세요.");
        const EFableQuestId Quest=Region==EFableRegion::Moonwell?EFableQuestId::Moonwell:EFableQuestId::RootRuins;
        return Count+(State->GetQuestStage(Quest)==EFableQuestStage::Ready?TEXT(" · 로완에게 보상을 받고 의뢰를 다시 수락하세요."):TEXT(" · 로완에게 이 지역 의뢰를 수락하세요."));
    }
    if (Ready<Adventure->PlayerArray.Num()) return Count+TEXT(" · 아직 의뢰를 받지 않은 동료가 있습니다.");
    if (NearestService(this)!=EFableService::Portal) return Count+TEXT(" · 마을 포털에 더 가까이 다가가세요.");
    return Count+TEXT(" · 함께 입장할 수 있습니다.");
}
void AFablePlayerController::UIChooseClass(EFableClass HeroClass) { if (!bFrontEndOpen) ServerChooseClass(HeroClass); }
void AFablePlayerController::UIEquip(int32 Id) { if (!bFrontEndOpen) ServerEquip(Id); }
void AFablePlayerController::UIEnhance(int32 Id) { if (!bFrontEndOpen) ServerEnhance(Id); }
void AFablePlayerController::UIShopBuy(EFableShopOffer Offer) { if (!bFrontEndOpen) ServerShopBuy(Offer); }
void AFablePlayerController::UISell(int32 Id) { if (!bFrontEndOpen) ServerSell(Id); }
void AFablePlayerController::UIUseHealthPotion() { if (!bFrontEndOpen) ServerUsePotion(false); }
void AFablePlayerController::UIUseManaPotion() { if (!bFrontEndOpen) ServerUsePotion(true); }
void AFablePlayerController::ChooseWarrior() { if (bFrontEndOpen) UISetInitialClass(EFableClass::Warrior); else if (!IsAnyPanelOpen() || Dialogue == EFableDialogue::Guide) UIChooseClass(EFableClass::Warrior); }
void AFablePlayerController::ChooseMage() { if (bFrontEndOpen) UISetInitialClass(EFableClass::Mage); else if (!IsAnyPanelOpen() || Dialogue == EFableDialogue::Guide) UIChooseClass(EFableClass::Mage); }
void AFablePlayerController::ChooseArcher() { if (bFrontEndOpen) UISetInitialClass(EFableClass::Archer); else if (!IsAnyPanelOpen() || Dialogue == EFableDialogue::Guide) UIChooseClass(EFableClass::Archer); }
void AFablePlayerController::EnhanceSelected() { if (bInventoryOpen) UIEnhance(GetSelectedItemId()); }
void AFablePlayerController::SellSelected() { if (bInventoryOpen) UISell(GetSelectedItemId()); }

bool AFablePlayerController::AcceptRPGRequest()
{
    if (!HasAuthority() || !GetWorld()) return false;
    const AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();
    if (Mode && Mode->IsFrontEnd()) return false;
    const float Now = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now - LastRPGRequestTime < 0.12f) return false;
    LastRPGRequestTime = Now;
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    if (!State) return false;
    if (State->IsBackendEnabled() && (!State->IsBackendReady() || State->IsBackendSavePending()))
    { State->SetNotice(TEXT("서버 저장을 확인하고 있습니다. 잠시 뒤 다시 시도하세요.")); return false; }
    if (!Hero || !Hero->IsAlive()) { State->SetNotice(TEXT("회복한 뒤 이용할 수 있습니다.")); return false; }
    return true;
}
void AFablePlayerController::ServerStartRevive_Implementation(AFableCharacter* Target)
{
    if (!HasAuthority() || !GetWorld()) return;
    if (const auto Profile=GetPlayerState<AFablePlayerState>(); Profile && Profile->IsBackendEnabled() && !Profile->IsBackendReady()) return;
    const AFableGameMode* Mode=GetWorld()->GetAuthGameMode<AFableGameMode>();
    const AFableGameState* Adventure=GetWorld()->GetGameState<AFableGameState>();
    if (!Mode || Mode->IsFrontEnd() || (Adventure && Adventure->bPartyRecoveryPending)) return;
    if (AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn())) Hero->BeginRevive(Target);
}
void AFablePlayerController::ServerCancelRevive_Implementation()
{
    if (HasAuthority())
        if (AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn()))
            if (Hero->GetReviveTarget()) Hero->CancelRevive();
}
void AFablePlayerController::ServerRetryEncounter_Implementation()
{
    if (!AcceptRPGRequest()) return;
    if (AFableCharacter* Hero=Cast<AFableCharacter>(GetPawn())) Hero->CancelRevive();
    if (AFableGameMode* Mode=GetWorld()->GetAuthGameMode<AFableGameMode>()) Mode->RequestRetryEncounter(this);
}
void AFablePlayerController::ServerInteract_Implementation()
{
    if (!AcceptRPGRequest()) return;
    AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();
    switch (NearestService(this))
    {
    case EFableService::Board: ClientOpenDialogue(static_cast<uint8>(EFableDialogue::Guide)); break;
    case EFableService::Forge: ClientOpenDialogue(static_cast<uint8>(EFableDialogue::Forge)); break;
    case EFableService::Merchant: ClientOpenDialogue(static_cast<uint8>(EFableDialogue::Shop)); break;
    case EFableService::Portal: ClientOpenDialogue(static_cast<uint8>(EFableDialogue::Portal)); break;
    case EFableService::MoonSeal: if (Mode) Mode->RequestCleanseSeal(this,NearbyMoonSeal(this)); break;
    case EFableService::StoryObject: GetPlayerState<AFablePlayerState>()->RecordStoryObject(GetNearbyStoryObjectIndex()); break;
    case EFableService::ReturnPortal: if (Mode) Mode->RequestReturnVillage(this); break;
    default: GetPlayerState<AFablePlayerState>()->SetNotice(TEXT("주민이나 포털에 조금 더 가까이 다가가세요.")); break;
    }
}
void AFablePlayerController::ClientOpenDialogue_Implementation(uint8 Service)
{
    if (Service < 1 || Service > static_cast<uint8>(EFableDialogue::Portal)) return;
    UIClosePanels(); Dialogue = static_cast<EFableDialogue>(Service); ApplyUIMode();
}
void AFablePlayerController::ServerConfirmQuest_Implementation(EFableQuestId QuestId)
{
    if (!AcceptRPGRequest() || uint8(QuestId)>uint8(EFableQuestId::Story)) return;
    GetPlayerState<AFablePlayerState>()->InteractQuest(QuestId);
}
void AFablePlayerController::ServerEnterRegion_Implementation(EFableRegion Region, EFableDifficulty Difficulty)
{
    if (!AcceptRPGRequest() || (Region!=EFableRegion::RootRuins && Region!=EFableRegion::Moonwell)
        || (Difficulty!=EFableDifficulty::Normal && Difficulty!=EFableDifficulty::Hard)) return;
    if (NearestService(this)!=EFableService::Portal)
    { GetPlayerState<AFablePlayerState>()->SetNotice(TEXT("마을의 원정 포털 가까이에서 입장하세요.")); return; }
    if (AFableGameMode* Mode=GetWorld()->GetAuthGameMode<AFableGameMode>()) Mode->RequestEnterRegion(this,Region,Difficulty);
}
void AFablePlayerController::ServerChooseClass_Implementation(EFableClass HeroClass)
{
    if (!AcceptRPGRequest()) return;
    if (HeroClass != EFableClass::Warrior && HeroClass != EFableClass::Mage && HeroClass != EFableClass::Archer) return;
    GetPlayerState<AFablePlayerState>()->SelectClass(HeroClass);
}
void AFablePlayerController::ServerEquip_Implementation(int32 Id) { if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->EquipItem(Id); }
void AFablePlayerController::ServerEnhance_Implementation(int32 Id) { if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->EnhanceItem(Id); }
void AFablePlayerController::ServerCraft_Implementation(int32 Rarity,EFableEquipmentSlot Slot)
{ if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->CraftItem(Rarity,Slot); }
void AFablePlayerController::ServerDismantle_Implementation(int32 ItemId)
{ if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->DismantleItem(ItemId); }
void AFablePlayerController::ServerRerollAffix_Implementation(int32 ItemId,int32 AffixIndex)
{ if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->RerollAffix(ItemId,AffixIndex); }
void AFablePlayerController::ServerRepair_Implementation(int32 ItemId)
{ if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->RepairItem(ItemId); }
void AFablePlayerController::ServerDeposit_Implementation(int32 ItemId)
{ if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->DepositItem(ItemId); }
void AFablePlayerController::ServerWithdraw_Implementation(int32 ItemId)
{ if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->WithdrawItem(ItemId); }
void AFablePlayerController::ServerShopBuy_Implementation(EFableShopOffer Offer) { if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->BuyShopItem(Offer); }
void AFablePlayerController::ServerSell_Implementation(int32 Id) { if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->SellItem(Id); }
void AFablePlayerController::ServerUsePotion_Implementation(bool bMana) { if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->UsePotion(bMana); }
void AFablePlayerController::ServerUpgradeSkill_Implementation(int32 Slot)
{
    if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->UpgradeSkill(Slot);
}
void AFablePlayerController::ServerSelectTalent_Implementation(int32 TargetTier, EFableTalentPath Path)
{
    if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->SelectTalent(TargetTier,Path);
}
void AFablePlayerController::ServerResetCurrentClassBuild_Implementation(EFableClass ExpectedClass, int32 ExpectedRevision)
{
    if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->ResetCurrentClassBuild(ExpectedClass,ExpectedRevision);
}
void AFablePlayerController::ServerAdvanceClass_Implementation()
{
    if (AcceptRPGRequest()) GetPlayerState<AFablePlayerState>()->AdvanceClass();
}
void AFablePlayerController::RestartTrial()
{
    const AFableGameState* Adventure=GetWorld()?GetWorld()->GetGameState<AFableGameState>():nullptr;
    if (IsPortalOpen())
    {
        if (Adventure && Adventure->bRetryAvailable && Adventure->RetryRegion!=EFableRegion::Village) UIRetryEncounter();
        else UIEnterSelectedDungeon();
        return;
    }
    if (Adventure && Adventure->bRetryAvailable && (!IsAnyPanelOpen()
        || (Dialogue==EFableDialogue::Guide && Adventure->RetryRegion==EFableRegion::Village)))
    { UIRetryEncounter(); return; }
    if (IsAnyPanelOpen()) return;
    if (NearestService(this)==EFableService::Portal) ServerInteract();
    else ServerRestartTrial();
}
void AFablePlayerController::ServerRestartTrial_Implementation()
{
    if (!AcceptRPGRequest()) return;
    if (AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>()) Mode->RequestNewRound(this);
}
