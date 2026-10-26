#include "FablePlayerController.h"
#include "FableAdventure.h"
#include "FablePartyEncounter.h"
#include "FableGameMode.h"
#include "FableGameInstance.h"
#include "FableOnlineFlowSubsystem.h"
#include "FableOnlineServicesValidation.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformApplicationMisc.h"
#include "FableGameState.h"
#include "FablePlayerState.h"
#include "FablePartyManager.h"
#include "FableTownManager.h"
#include "FableTownCatalog.h"
#include "FableTownTravelCoordinator.h"
#include "FableTownStreamingSubsystem.h"

#include "TimerManager.h"
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

    /** 아트 확인용 모험 화면으로 전환을 요청한다. */
    void StartArtReview(APlayerController* PC)
    {
        FString Directory;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!FParse::Value(FCommandLine::Get(), TEXT("FableArtReviewDirectory="), Directory) ||
            FPaths::IsRelative(Directory))
        {
            return;
        }

        // 체력·마나·보호막·피해량 처리
        IFileManager::Get().MakeDirectory(*Directory, true);
        PC->SetIgnoreMoveInput(true);
        PC->SetIgnoreLookInput(true);

        // 이 단계에서 시작 아트 확인에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (AHUD* HUD = PC->GetHUD())
        {
            HUD->bShowHUD = false;
        }

        // 지역·적·전투 진행 순회
        for (TActorIterator<AFableEnemy> It(PC->GetWorld()); It; ++It)
        {
            It->SetActorTickEnabled(false);
        }

        ACameraActor* Camera = PC->GetWorld()->SpawnActor<ACameraActor>();

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Camera)
        {
            return;
        }

        // 지역·적·전투 진행 처리
        PC->bAutoManageActiveCameraTarget = false;
        PC->SetViewTarget(Camera);
        const auto Shot =
            [PC, Camera, Directory](float At, const FString& Name, FVector Location, FVector Target, float Fov)
        {
            FTimerHandle PoseHandle;
            PC->GetWorldTimerManager().SetTimer(
                PoseHandle,
                [PC, Camera, Location, Target, Fov, Name]() mutable
                {
                    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
                    if (!IsValid(Camera) || !IsValid(PC))
                    {
                        return;
                    }

                    PC->SetViewTarget(Camera);

                    // 이 단계에서 시작 아트 확인에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
                    if (AHUD* HUD = PC->GetHUD())
                    {
                        HUD->bShowHUD = false;
                    }

                    // 지역·적·전투 진행 순회
                    for (TActorIterator<AFableEnemy> It(PC->GetWorld()); It; ++It)
                    {
                        It->SetActorTickEnabled(false);
                    }

                    // 현재 월드와 플레이어 참조 처리
                    if (Name == TEXT("hero") && PC->GetPawn())
                    {
                        Target = PC->GetPawn()->GetActorLocation() + FVector(0, 0, 10);
                        Location = Target + FVector(285, 470, 65);
                    }

                    // 추적 카메라 설정 처리
                    Camera->SetActorLocation(Location);
                    Camera->SetActorRotation((Target - Location).Rotation());
                    Camera->GetCameraComponent()->SetFieldOfView(Fov);
                },
                At, false);
            FTimerHandle CaptureHandle;
            PC->GetWorldTimerManager().SetTimer(
                CaptureHandle,
                [Directory, Name]()
                {
                    FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory, Name + TEXT(".png")), false,
                                                          false);
                },
                At + 6.0f, false);
        };
        Shot(1.0f, TEXT("hero"), FVector(210, -560, 160), FVector(0, -900, 103), 46.0f);
        Shot(11.0f, TEXT("forest"), FVector(850, -1800, 620), FVector(-250, 1300, 340), 67.0f);
        Shot(21.0f, TEXT("bridge"), FVector(-1380, -1180, 365), FVector(-2310, -260, 155), 62.0f);
        FTimerHandle ExitHandle;
        PC->GetWorldTimerManager().SetTimer(
            ExitHandle,
            []()
            {
                FGenericPlatformMisc::RequestExit(false);
            },
            32.0f, false);
    }
}
#endif

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFablePlayerController::BeginPlay()
{
    Super::BeginPlay();

    // 서버 권한과 실행 위치 조건 검사
    if (!IsLocalController())
    {
        return;
    }

    // 현재 월드와 플레이어 참조 처리
    if (const UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        bFrontEndOpen = GI->IsFrontEnd();
    }

    ApplyUIMode();
#if !UE_BUILD_SHIPPING
    if (FParse::Param(FCommandLine::Get(), TEXT("FableArtReview")))
    {
        StartArtReview(this);
    }
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
    /** 마을에서 이용할 수 있는 서비스 종류 값의 종류를 정의한다. */
    enum class EFableService : uint8
    {
        None,
        Board,
        Forge,
        Merchant,
        Portal,
        ReturnPortal,
        MoonSeal,
        StoryObject
    };

    /** 캐릭터 위치가 속하는 게임 지역을 판정한다. */
    EFableRegion PawnRegion(const APlayerController* PC)
    {
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        const FFableAdventureView Adventure = FableAdventure::For(PC);

        // 지역·적·전투 진행 조건 검사
        if (Pawn && Adventure && Adventure.IsInRegion(EFableRegion::Moonwell, Pawn->GetActorLocation()))
        {
            return EFableRegion::Moonwell;
        }
        if (Pawn && Adventure && Adventure.IsInRegion(EFableRegion::RootRuins, Pawn->GetActorLocation()))
        {
            return EFableRegion::RootRuins;
        }

        return EFableRegion::Village;
    }

    /** 현재 캐릭터가 상호작용할 수 있는 가까운 달샘 봉인을 찾는다. */
    int32 NearbyMoonSeal(const APlayerController* PC)
    {
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        const FFableAdventureView Adventure = FableAdventure::For(PC);

        // 지역·적·전투 진행 조건 검사
        if (!Pawn || !Adventure || PawnRegion(PC) != EFableRegion::Moonwell)
        {
            return INDEX_NONE;
        }

        // 위치·방향·거리 계산 순회
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const FVector Delta =
                Pawn->GetActorLocation() - Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::MoonSealLocation(Index));
            if (Delta.SizeSquared2D() <= FMath::Square(330.f) && FMath::Abs(Delta.Z) < 300.f)
            {
                return Index;
            }
        }

        return INDEX_NONE;
    }

    /** 플레이어와 가장 가까운 마을 서비스 대상을 찾는다. */
    EFableService NearestService(const APlayerController* PC)
    {
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        const FFableAdventureView Adventure = FableAdventure::For(PC);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Pawn || !Adventure)
        {
            return EFableService::None;
        }

        float NearestSquared = TNumericLimits<float>::Max();
        EFableService Nearest = EFableService::None;

        // 네 마을 역할은 현재 town/catalog에서만 고른다. 이 UI 조회는 서버 권한을 대신하지 않는다. / 現在の村だけを選び権限検査は別に行う。
        const AFableTownManager* Towns = AFableTownManager::Find(PC->GetWorld());
        const UFableTownCatalog* Catalog = Towns ? Towns->GetCatalog() : nullptr;
        const EFableTownServiceId TownIds[] = {EFableTownServiceId::QuestGuide, EFableTownServiceId::Workshop,
                                               EFableTownServiceId::Merchant, EFableTownServiceId::ExpeditionPortal};
        const EFableService TownServices[] = {EFableService::Board, EFableService::Forge, EFableService::Merchant,
                                              EFableService::Portal};
        if (Catalog)
        {
            for (int32 Index = 0; Index < UE_ARRAY_COUNT(TownIds); ++Index)
            {
                FFableTownServiceDescriptor Service;
                if (Catalog->ResolveNearbyService(Pawn->GetActorLocation(), TownIds[Index], Service))
                {
                    const float DistanceSquared =
                        FVector::DistSquared2D(Pawn->GetActorLocation(), Service.WorldLocation);
                    if (DistanceSquared < NearestSquared)
                    {
                        NearestSquared = DistanceSquared;
                        Nearest = TownServices[Index];
                    }
                }
            }
        }

        // 사설 원정·봉인의 지역 좌표는 기존 경로를 유지한다. / 私有遠征・封印の地域座標を維持する。
        const FVector Locations[] = {
            Adventure.ToWorld(EFableRegion::RootRuins, FableRPG::RegionEntrance(EFableRegion::RootRuins)),
            Adventure.ToWorld(EFableRegion::RootRuins, FableRPG::RegionReturnPortal(EFableRegion::RootRuins)),
            Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::RegionEntrance(EFableRegion::Moonwell)),
            Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::RegionReturnPortal(EFableRegion::Moonwell)),
            Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::MoonSealLocation(0)),
            Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::MoonSealLocation(1)),
            Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::MoonSealLocation(2))};
        const EFableService Services[] = {EFableService::ReturnPortal, EFableService::ReturnPortal,
                                          EFableService::ReturnPortal, EFableService::ReturnPortal,
                                          EFableService::MoonSeal,     EFableService::MoonSeal,
                                          EFableService::MoonSeal};

        // 위치·방향·거리 계산 순회
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Locations); ++Index)
        {
            const float DistanceSquared = FVector::DistSquared2D(Pawn->GetActorLocation(), Locations[Index]);
            const float Radius = Services[Index] == EFableService::ReturnPortal ? 350.0f : 330.0f;
            if (DistanceSquared <= FMath::Square(Radius) && DistanceSquared < NearestSquared &&
                FMath::Abs(Pawn->GetActorLocation().Z - Locations[Index].Z) < 300.0f)
            {
                NearestSquared = DistanceSquared;
                Nearest = Services[Index];
            }
        }

        // 퀘스트와 이야기 진행 순회
        for (int32 Index = 0; Index < 5; ++Index)
        {
            const FVector Delta = Pawn->GetActorLocation() - Adventure.ToWorld(FableRPG::StoryObjectRegion(Index),
                                                                               FableRPG::StoryObjectLocation(Index));
            const float DistanceSquared = Delta.SizeSquared2D();
            if (DistanceSquared <= FMath::Square(330.f) && DistanceSquared < NearestSquared &&
                FMath::Abs(Delta.Z) < 300.f)
            {
                NearestSquared = DistanceSquared;
                Nearest = EFableService::StoryObject;
            }
        }

        return Nearest;
    }

    /** 로컬 추측 대신 서버가 복제한 자기 파티 명부를 조회한다. */
    const FFablePartyRecord* FindControllerParty(const AFablePlayerController* PC)
    {
        const AFablePartyManager* Manager = PC ? AFablePartyManager::Find(PC->GetWorld()) : nullptr;
        const AFablePlayerState* Profile = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        return Manager && Profile ? Manager->GetPartyForMember(Profile->GetPlayerId()) : nullptr;
    }

    /** 응답 전인 초대가 있는 동안에는 명부를 확정하지 않는다. */
    bool HasExpeditionInvitation(const AFablePartyManager* Manager, const FFablePartyRecord* Party)
    {
        if (!Manager || !Party)
        {
            return false;
        }

        const AGameStateBase* Clock = Manager->GetWorld()->GetGameState();
        const float Now = Clock ? Clock->GetServerWorldTimeSeconds() : Manager->GetWorld()->GetTimeSeconds();
        return Manager->Invitations.ContainsByPredicate(
            [&](const FFablePartyInvite& Invitation)
            {
                return Invitation.PartyId == Party->PartyId && Invitation.ExpiresAt > Now;
            });
    }

    /** 새 원정 준비가 가능한 마을의 공유 전투 상태를 확인한다. */
    bool IsExpeditionPreparationSafe(const AFablePlayerController* PC)
    {
        const FFableAdventureView Adventure = FableAdventure::For(PC);
        const AFablePlayerState* Profile = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        return Adventure && Profile && !Profile->ActiveExpeditionId.IsValid() &&
               Adventure->ActiveRegion == EFableRegion::Village && !Adventure->bPartyRecoveryPending &&
               !(Adventure->AdventureStage == EFableAdventureStage::Forest && !Adventure->bObjectiveComplete);
    }

    /** 종료 결과와 현재 명부가 같은 재도전 준비에서만 기존 원정 위치를 허용한다. */
    bool IsRetryPreparationSafe(const AFablePlayerController* PC)
    {
        const FFablePartyRecord* Party = FindControllerParty(PC);
        const AFablePlayerState* Profile = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        const auto* Result = PC ? PC->GetDisplayedExpeditionResult() : nullptr;
        if (!Party || !Profile || !Result || !Result->IsTerminal() || !Result->bSaveReady ||
            (Result->bRequiresBackendCommit && !Result->bAllGrantsCommitted) ||
            Party->RetrySourceExpeditionId != Result->Ticket.ExpeditionId ||
            Party->RetrySourceCompletionId != Result->CompletionId || Party->PartyId != Result->Ticket.PartyId)
        {
            return false;
        }
        if (!Profile->ActiveExpeditionId.IsValid())
        {
            return IsExpeditionPreparationSafe(PC);
        }

        const auto View = FableAdventure::For(PC);
        const auto* Encounter = View ? Cast<AFablePartyEncounter>(View.ReplicationOwner()) : nullptr;
        return Profile->ActiveExpeditionId == Party->RetrySourceExpeditionId && Encounter &&
               !Encounter->IsReturning() && View->AdventureStage == EFableAdventureStage::Cleared &&
               !View->bPartyRecoveryPending;
    }
}

/** 플레이어의 UI·상호작용·전투 입력을 등록한다. */
void AFablePlayerController::SetupInputComponent()
{
    // 접속 권한·저장 리비전·멱등 키 처리
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

/** 온라인 화면과 UI 초점 상태에 따라 플레이어의 키 입력을 처리한다. */
bool AFablePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    // 생존 상태와 부활 진행 처리
    if (auto Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>() : nullptr;
        Flow && Flow->IsEnabled())
    {
        // Slate owns focused text fields and menu keys. Never send them to gameplay bindings.
        if (Flow->IsOverlayBlocking())
        {
            return false;
        }
        if (Params.Key == EKeys::Escape && Params.Event == IE_Pressed && !IsAnyPanelOpen())
        {
            UIStopRevive();
            UIClosePanels();
            Flow->ToggleInGameMenu();
            return true;
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bOnlineLeaveInputBlocked)
    {
        return true;
    }

    // 파티 창은 기존 스킬 단축키와 겹치지 않는 G 키로 연다.
    if (Params.Key == EKeys::G && Params.Event == IE_Pressed && !bFrontEndOpen)
    {
        UIToggleParty();
        return true;
    }

    // 파티 탭과 결과 장비 선택 키는 창 내부에서만 소비한다.
    if (bPartyOpen && Params.Event == IE_Pressed && Params.Key == EKeys::Tab)
    {
        UISelectPartyTab(static_cast<EFablePartyPanelTab>((int32(PartyPanelTab) + 1) % 3));
        return true;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    if (bReviveInputHeld)
    {
        const FKey Input = Params.Key;
        const bool bRelease = Input == EKeys::E && Params.Event == IE_Released;
        const bool bAction =
            Params.Event == IE_Pressed &&
            (Input == EKeys::W || Input == EKeys::A || Input == EKeys::S || Input == EKeys::D ||
             Input == EKeys::SpaceBar || Input == EKeys::LeftShift || Input == EKeys::RightShift || Input == EKeys::J ||
             Input == EKeys::K || Input == EKeys::L || Input == EKeys::O || Input == EKeys::LeftMouseButton);
        if (bRelease || bAction)
        {
            UIStopRevive();
        }
    }

    // 직업·스킬·특성 빌드 처리
    if (bSkillsOpen && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
    {
        if (Params.Key == EKeys::Left || Params.Key == EKeys::Right)
        {
            UISelectSkillPanelTab(1 - SkillPanelTab);
            return true;
        }
        if (Params.Event == IE_Pressed && Params.Key == EKeys::F)
        {
            UIRequestBuildReset();
            return true;
        }
        if (Params.Event == IE_Pressed && Params.Key == EKeys::Escape && IsBuildResetConfirmationPending())
        {
            UICancelBuildReset();
            return true;
        }
    }

    // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
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
            const FKey Keys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four};
            for (int32 Index = 0; Index < 4; ++Index)
            {
                if (Params.Key == Keys[Index])
                {
                    if (WorkshopTab == EFableWorkshopTab::Craft)
                    {
                        UISelectCraftRecipe(Index + 1, CraftSlot);
                    }
                    else if (WorkshopTab == EFableWorkshopTab::Reroll && Index < 3)
                    {
                        UISelectAffix(Index);
                    }
                    return true;
                }
            }
            if (Params.Key == EKeys::F)
            {
                if (WorkshopTab == EFableWorkshopTab::Craft)
                {
                    UISelectCraftRecipe(CraftRarity, CraftSlot == EFableEquipmentSlot::Weapon
                                                         ? EFableEquipmentSlot::Armor
                                                         : EFableEquipmentSlot::Weapon);
                }
                else if (WorkshopTab == EFableWorkshopTab::Stash)
                {
                    UISetStashWithdrawal(!bStashWithdrawal);
                }
                return true;
            }
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Dialogue == EFableDialogue::Forge && Params.Key == EKeys::F && Params.Event == IE_Pressed)
    {
        UIOpenWorkshop();
        return true;
    }
    if ((IsPortalOpen() || IsPartyExpeditionOpen()) && (Params.Event == IE_Pressed || Params.Event == IE_Repeat) &&
        (Params.Key == EKeys::Left || Params.Key == EKeys::Right))
    {
        UISelectDifficulty(Params.Key == EKeys::Left ? EFableDifficulty::Normal : EFableDifficulty::Hard);
        return true;
    }
    if (bInventoryOpen && Params.Event == IE_Pressed && Params.Key == EKeys::Tab)
    {
        UICycleInventorySort();
        return true;
    }

    // 검증 입력과 키 유지 상태 처리
    if (bFrontEndOpen && bJoinAddressFocused && (Params.Event == IE_Pressed || Params.Event == IE_Repeat))
    {
        // 검증 입력과 키 유지 상태에 필요한 값 준비
        const FKey Key = Params.Key;
        const bool bCtrl = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Key == EKeys::Escape)
        {
            bJoinAddressFocused = false;
            return true;
        }
        if (Key == EKeys::Enter)
        {
            UIJoinGame();
            return true;
        }
        if (bCtrl && Key == EKeys::A)
        {
            bAddressSelectAll = true;
            return true;
        }

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (bCtrl && Key == EKeys::V)
        {
            FString Pasted;
            FPlatformApplicationMisc::ClipboardPaste(Pasted);
            UISetJoinAddress(Pasted.TrimStartAndEnd());
            bAddressSelectAll = false;
            return true;
        }

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Key == EKeys::BackSpace || Key == EKeys::Delete)
        {
            if (bAddressSelectAll || Key == EKeys::Delete)
            {
                JoinAddress.Empty();
            }
            else
            {
                JoinAddress.LeftChopInline(1);
            }
            bAddressSelectAll = false;
            return true;
        }

        FString TypedCharacter;
        const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two,   EKeys::Three, EKeys::Four,
                               EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
        const FKey NumDigits[] = {EKeys::NumPadZero,  EKeys::NumPadOne,  EKeys::NumPadTwo, EKeys::NumPadThree,
                                  EKeys::NumPadFour,  EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven,
                                  EKeys::NumPadEight, EKeys::NumPadNine};

        // 같은 규칙을 적용할 대상을 순회한다.
        for (int32 I = 0; I < 10; ++I)
        {
            if (Key == Digits[I] || Key == NumDigits[I])
            {
                TypedCharacter = FString::FromInt(I);
            }
        }

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Key == EKeys::Period || Key == EKeys::Decimal)
        {
            TypedCharacter = TEXT(".");
        }

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Key == EKeys::Semicolon)
        {
            TypedCharacter = TEXT(":");
        }

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Key == EKeys::Hyphen || Key == EKeys::Subtract)
        {
            TypedCharacter = TEXT("-");
        }

        const FString Name = Key.GetFName().ToString();

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (Name.Len() == 1 && FChar::IsAlpha(Name[0]))
        {
            TypedCharacter = Name.ToLower();
        }

        // 이 단계에서 키 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
        if (!TypedCharacter.IsEmpty() && !bCtrl)
        {
            if (bAddressSelectAll)
            {
                JoinAddress.Empty();
            }
            bAddressSelectAll = false;
            if (JoinAddress.Len() < 128)
            {
                JoinAddress += TypedCharacter;
            }
            return true;
        }
        // Modifier and mouse events still reach the engine to keep key state and HUD clicks correct.

        // 검증 입력과 키 유지 상태 조건 검사
        if (!Key.IsMouseButton() && !Key.IsModifierKey())
        {
            return true;
        }
    }

    return Super::InputKey(Params);
}

/** 열린 패널에 맞게 마우스 커서와 게임·UI 입력 모드를 조정한다. */
void AFablePlayerController::ApplyUIMode()
{
    // 생존 상태와 부활 진행 처리
    if (auto Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>() : nullptr;
        Flow && Flow->IsEnabled())
    {
        if (Flow->IsOverlayBlocking())
        {
            UIStopRevive();
            // Remove only our old Canvas panel layer; the subsystem owns Slate focus and cursor.
            if (bUIInputBlocked)
            {
                SetIgnoreLookInput(false);
                SetIgnoreMoveInput(false);
                bUIInputBlocked = false;
            }
            return;
        }
    }

    const bool bWantsCursor = IsAnyPanelOpen() || bFreeCursor;

    // 생존 상태와 부활 진행 처리
    if (bWantsCursor)
    {
        UIStopRevive();
    }
    // Ignore flags are counted. Release only the layer this controller added.

    // 이 단계에서 적용 UI 모드에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (bWantsCursor != bUIInputBlocked)
    {
        SetIgnoreLookInput(bWantsCursor);
        SetIgnoreMoveInput(bWantsCursor);
        bUIInputBlocked = bWantsCursor;
    }

    bShowMouseCursor = bWantsCursor;
    bEnableClickEvents = bWantsCursor;
    bEnableMouseOverEvents = bWantsCursor;

    // 이 단계에서 적용 UI 모드에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (bWantsCursor)
    {
        FInputModeGameAndUI Mode;
        Mode.SetHideCursorDuringCapture(false);
        Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        SetInputMode(Mode);
    }
    else
    {
        SetInputMode(FInputModeGameOnly());
    }
}

/** UI에서 선택한 열린 창 닫기 동작을 처리한다. */
void AFablePlayerController::UIClosePanels()
{
    ClearBuildResetConfirmation();

    // 이 단계에서 UI에서 선택한 열린 창 닫기 동작을 처리한다.
    if (bWorkshopOpen)
    {
        DismissTransientNotice();
    }

    // 생존 상태와 부활 진행 처리
    UIStopRevive();
    bInventoryOpen = bWorkshopOpen = bQuestsOpen = bHelpOpen = bSkillsOpen = bPartyOpen = bFreeCursor = false;
    ClearDismantleConfirmation();
    Dialogue = EFableDialogue::None;
    bJoinAddressFocused = false;
    ApplyUIMode();
}

/** 플레이어 입력 상태와 주변 상호작용 대상을 갱신한다. */
void AFablePlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    TickTownTravel();

    // 종료 결과의 자동 열기는 로컬 표시만 바꾸며 지급·확인 요청을 대신하지 않는다.
    const AFablePlayerState* ResultProfile = GetPlayerState<AFablePlayerState>();
    const auto* PersonalResult = GetDisplayedExpeditionResult();
    const auto* OnlineFlow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>() : nullptr;
    if (IsLocalController() && !bFrontEndOpen && !bOnlineLeaveInputBlocked && ResultProfile &&
        ResultProfile->bProfileLoaded && !ResultProfile->IsBackendClosing() && GetPawn() && PersonalResult &&
        PersonalResult->IsTerminal() && !PersonalResult->bAcknowledged &&
        !AutoOpenedCompletionIds.Contains(PersonalResult->CompletionId) &&
        (!OnlineFlow || !OnlineFlow->IsOverlayBlocking()))
    {
        AutoOpenedCompletionIds.Add(PersonalResult->CompletionId);
        SelectedResultItemIndex = 0;
        OpenExpeditionResultPanel();
    }

    // 파티원도 서버가 전원 입장 후 발급한 증서와 실제 도착을 확인해야 창을 닫는다.
    if (bPartyOpen)
    {
        const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
        const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
        const FFablePartyRecord* Party = FindControllerParty(this);
        if (Profile && Profile->ActiveExpeditionId.IsValid() && Profile->ActiveExpeditionId != PartyExpeditionAtOpen &&
            Hero && Party && FableAdventure::For(this).IsInRegion(Party->SelectedRegion, Hero->GetActorLocation()))
        {
            UIClosePanels();
        }
    }

    // 현재 월드와 플레이어 참조 처리
    if (const auto Profile = GetPlayerState<AFablePlayerState>(); Profile && Profile->IsBackendClosing())
    {
        BlockOnlineLeaveInput();
    }

    // 현재 월드와 플레이어 참조 처리
    if (const AFablePlayerState* State = GetPlayerState<AFablePlayerState>())
    {
        if (ObservedNoticeSerial != State->NoticeSerial || ObservedNoticeText != State->LastNotice)
        {
            ObservedNoticeSerial = State->NoticeSerial;
            ObservedNoticeText = State->LastNotice;
            NoticeObservedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
        }
    }

    // 이 단계에서 플레이어 입력 상태와 주변 상호작용 대상을 갱신한다.
    if (PendingDismantleItemId != INDEX_NONE && !IsDismantleConfirmationPending())
    {
        ClearDismantleConfirmation();
    }

    // 이 단계에서 플레이어 입력 상태와 주변 상호작용 대상을 갱신한다.
    if (bBuildResetConfirmationPending && !IsBuildResetConfirmationPending())
    {
        ClearBuildResetConfirmation();
    }
}

/** 현재 임시 안내를 닫고 확인한 알림 상태를 기록한다. */
void AFablePlayerController::DismissTransientNotice()
{
    if (const AFablePlayerState* State = GetPlayerState<AFablePlayerState>())
    {
        DismissedNoticeSerial = State->NoticeSerial;
        DismissedNoticeText = State->LastNotice;
    }
}

/** 아직 확인하지 않은 임시 안내를 표시할지 판단한다. */
bool AFablePlayerController::ShouldShowTransientNotice() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State || State->LastNotice.IsEmpty())
    {
        return false;
    }
    // Save failures must remain visible until the authoritative save state recovers.
    if (!State->bLastSaveSucceeded)
    {
        return true;
    }
    if (DismissedNoticeSerial == State->NoticeSerial && DismissedNoticeText == State->LastNotice)
    {
        return false;
    }
    if (ObservedNoticeSerial != State->NoticeSerial || ObservedNoticeText != State->LastNotice)
    {
        return true;
    }

    return GetWorld() && GetWorld()->GetTimeSeconds() - NoticeObservedAt < 8.0;
}

/** 장비 분해 확인 절차 상태를 해제한다. */
void AFablePlayerController::ClearDismantleConfirmation()
{
    PendingDismantleItemId = INDEX_NONE;
    PendingDismantleSignature.Empty();
}

/** UI에서 선택한 공방 열기 동작을 처리한다. */
void AFablePlayerController::UIOpenWorkshop()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEndOpen || !IsNearForge())
    {
        return;
    }

    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !Hero->IsAlive())
    {
        return;
    }

    DismissTransientNotice();
    UIClosePanels();
    bWorkshopOpen = true;
    WorkshopItemId = GetSelectedWorkshopItemId();
    ApplyUIMode();
}

/** UI에서 선택한 선택 공방 탭 동작을 처리한다. */
void AFablePlayerController::UISelectWorkshopTab(EFableWorkshopTab Tab)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWorkshopOpen || uint8(Tab) > uint8(EFableWorkshopTab::Stash))
    {
        return;
    }

    // 이 단계에서 UI에서 선택한 선택 공방 탭 동작을 처리한다.
    if (WorkshopTab != Tab)
    {
        DismissTransientNotice();
    }

    WorkshopTab = Tab;
    SelectedAffixIndex = 0;
    ClearDismantleConfirmation();
    WorkshopItemId = GetSelectedWorkshopItemId();
}

/** 공방에 표시할 아이템 식별자 목록 값을 반환한다. */
TArray<int32> AFablePlayerController::GetWorkshopDisplayIds() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return {};
    }
    if (WorkshopTab != EFableWorkshopTab::Stash || !bStashWithdrawal)
    {
        return GetInventoryDisplayIds();
    }

    TArray<int32> Ids;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : State->StashItems)
    {
        Ids.Add(Item.Id);
    }

    return Ids;
}

/** 공방에서 선택한 아이템 식별자 값을 반환한다. */
int32 AFablePlayerController::GetSelectedWorkshopItemId() const
{
    const TArray<int32> Ids = GetWorkshopDisplayIds();
    return Ids.Contains(WorkshopItemId) ? WorkshopItemId : Ids.IsEmpty() ? INDEX_NONE : Ids[0];
}

/** UI에서 선택한 선택 공방 아이템 동작을 처리한다. */
void AFablePlayerController::UISelectWorkshopItem(int32 ItemId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWorkshopOpen || !GetWorkshopDisplayIds().Contains(ItemId))
    {
        return;
    }

    // 이 단계에서 UI에서 선택한 선택 공방 아이템 동작을 처리한다.
    if (WorkshopItemId != ItemId)
    {
        DismissTransientNotice();
        SelectedAffixIndex = 0;
        ClearDismantleConfirmation();
    }

    WorkshopItemId = ItemId;
}

/** UI에서 선택한 선택 장비 제작 제작법 동작을 처리한다. */
void AFablePlayerController::UISelectCraftRecipe(int32 Rarity, EFableEquipmentSlot Slot)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWorkshopOpen || Rarity < 1 || Rarity > 4 ||
        (Slot != EFableEquipmentSlot::Weapon && Slot != EFableEquipmentSlot::Armor))
    {
        return;
    }

    // 이 단계에서 UI에서 선택한 선택 장비 제작 제작법 동작을 처리한다.
    if (CraftRarity != Rarity || CraftSlot != Slot)
    {
        DismissTransientNotice();
    }

    CraftRarity = Rarity;
    CraftSlot = Slot;
    ClearDismantleConfirmation();
}

/** UI에서 선택한 선택 장비 옵션 동작을 처리한다. */
void AFablePlayerController::UISelectAffix(int32 Index)
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const FFableItem* Item = State ? State->FindItem(GetSelectedWorkshopItemId()) : nullptr;

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (bWorkshopOpen && Item && Item->Affixes.IsValidIndex(Index))
    {
        if (SelectedAffixIndex != Index)
        {
            DismissTransientNotice();
        }
        SelectedAffixIndex = Index;
        ClearDismantleConfirmation();
    }
}

/** UI에서 선택한 세트 창고 창고 수령 동작을 처리한다. */
void AFablePlayerController::UISetStashWithdrawal(bool bWithdraw)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWorkshopOpen)
    {
        return;
    }

    // 이 단계에서 UI에서 선택한 세트 창고 창고 수령 동작을 처리한다.
    if (bStashWithdrawal != bWithdraw)
    {
        DismissTransientNotice();
    }

    bStashWithdrawal = bWithdraw;
    WorkshopItemId = INDEX_NONE;
    SelectedAffixIndex = 0;
    ClearDismantleConfirmation();
    WorkshopItemId = GetSelectedWorkshopItemId();
}

/** 분해 확인 대상의 상태 서명 값을 반환한다. */
FString AFablePlayerController::GetDismantleConfirmationSignature() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return FString();
    }

    // 재화와 작업 비용에 필요한 값 준비
    FString Signature =
        FString::Printf(TEXT("%d:%d:%d:%d:%d:%d:%d"), State->Gold, State->Ore, State->Essence, State->RootCores,
                        State->MoonCrystals, State->EquippedWeaponId, State->EquippedArmorId);

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : State->Inventory)
    {
        Signature += TEXT("|") + FableRPG::ItemPersistentSignature(Item);
    }

    Signature += TEXT("/stash/");

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : State->StashItems)
    {
        Signature += TEXT("|") + FableRPG::ItemPersistentSignature(Item);
    }

    return Signature;
}

/** 분해 확인 대기 여부를 확인한다. */
bool AFablePlayerController::IsDismantleConfirmationPending() const
{
    return bWorkshopOpen && WorkshopTab == EFableWorkshopTab::Dismantle && PendingDismantleItemId != INDEX_NONE &&
           PendingDismantleItemId == GetSelectedWorkshopItemId() && IsNearForge() &&
           GetWorkshopBlockReason().IsEmpty() && PendingDismantleSignature == GetDismantleConfirmationSignature();
}

/** 공방 거부 사유 값을 반환한다. */
FString AFablePlayerController::GetWorkshopBlockReason() const
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const FFableAdventureView Adventure = FableAdventure::For(this);

    // 프로필과 저장 스냅숏 조건 검사
    if (!State || !State->bProfileLoaded || !Adventure)
    {
        return TEXT("모험 정보를 불러오는 중입니다.");
    }
    if (!Hero || !Hero->IsAlive())
    {
        return TEXT("회복한 뒤 공방을 이용하세요.");
    }
    if (Adventure && Adventure->bPartyRecoveryPending)
    {
        return TEXT("마을 구조가 끝난 뒤 이용하세요.");
    }
    if (!IsNearForge())
    {
        return TEXT("대장장이 브람에게 가까이 다가가세요.");
    }

    const int32 Id = GetSelectedWorkshopItemId();

    // 이 단계에서 공방 거부 사유 값을 반환한다.
    switch (WorkshopTab)
    {
    case EFableWorkshopTab::Craft:
        return State->GetCraftBlockReason(CraftRarity, CraftSlot);
    case EFableWorkshopTab::Dismantle:
        return State->GetDismantleBlockReason(Id);
    case EFableWorkshopTab::Reroll:
        return State->GetRerollBlockReason(Id, SelectedAffixIndex);
    case EFableWorkshopTab::Repair:
        return State->GetRepairBlockReason(Id);
    case EFableWorkshopTab::Stash:
        return bStashWithdrawal ? State->GetWithdrawBlockReason(Id) : State->GetDepositBlockReason(Id);
    default:
        return TEXT("공방 작업을 선택하세요.");
    }
}

/** UI에서 선택한 장비 제작 동작을 처리한다. */
void AFablePlayerController::UICraft()
{
    if (bFrontEndOpen)
    {
        return;
    }
    ClearDismantleConfirmation();
    ServerCraft(CraftRarity, CraftSlot);
}

/** UI에서 선택한 장비 분해 동작을 처리한다. */
void AFablePlayerController::UIDismantle(int32 ItemId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWorkshopOpen || WorkshopTab != EFableWorkshopTab::Dismantle || ItemId != GetSelectedWorkshopItemId())
    {
        return;
    }
    if (!GetWorkshopBlockReason().IsEmpty())
    {
        ClearDismantleConfirmation();
        return;
    }

    // 이 단계에서 UI에서 선택한 장비 분해 동작을 처리한다.
    if (IsDismantleConfirmationPending())
    {
        ClearDismantleConfirmation();
        ServerDismantle(ItemId);
    }
    else
    {
        PendingDismantleItemId = ItemId;
        PendingDismantleSignature = GetDismantleConfirmationSignature();
    }
}

/** UI에서 선택한 옵션 재설정 동작을 처리한다. */
void AFablePlayerController::UIRerollAffix(int32 ItemId, int32 AffixIndex)
{
    if (!bFrontEndOpen)
    {
        ClearDismantleConfirmation();
        ServerRerollAffix(ItemId, AffixIndex);
    }
}

/** UI에서 선택한 내구도 수리 동작을 처리한다. */
void AFablePlayerController::UIRepair(int32 ItemId)
{
    if (!bFrontEndOpen)
    {
        ClearDismantleConfirmation();
        ServerRepair(ItemId);
    }
}

/** UI에서 선택한 창고 보관 동작을 처리한다. */
void AFablePlayerController::UIDeposit(int32 ItemId)
{
    if (!bFrontEndOpen)
    {
        ClearDismantleConfirmation();
        ServerDeposit(ItemId);
    }
}

/** UI에서 선택한 창고 수령 동작을 처리한다. */
void AFablePlayerController::UIWithdraw(int32 ItemId)
{
    if (!bFrontEndOpen)
    {
        ClearDismantleConfirmation();
        ServerWithdraw(ItemId);
    }
}

/** UI에서 선택한 공방 작업 동작을 처리한다. */
void AFablePlayerController::UIWorkshopAction()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bWorkshopOpen)
    {
        return;
    }

    const int32 Id = GetSelectedWorkshopItemId();

    // 이 단계에서 UI에서 선택한 공방 작업 동작을 처리한다.
    switch (WorkshopTab)
    {
    case EFableWorkshopTab::Craft:
        UICraft();
        break;
    case EFableWorkshopTab::Dismantle:
        UIDismantle(Id);
        break;
    case EFableWorkshopTab::Reroll:
        UIRerollAffix(Id, SelectedAffixIndex);
        break;
    case EFableWorkshopTab::Repair:
        UIRepair(Id);
        break;
    case EFableWorkshopTab::Stash:
        if (bStashWithdrawal)
        {
            UIWithdraw(Id);
        }
        else
        {
            UIDeposit(Id);
        }
        break;
    default:
        break;
    }
}

/** UI에서 선택한 표시 전환 인벤토리 동작을 처리한다. */
void AFablePlayerController::UIToggleInventory()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEndOpen)
    {
        return;
    }

    const bool bOpen = !bInventoryOpen;
    UIClosePanels();
    bInventoryOpen = bOpen;

    // 이 단계에서 UI에서 선택한 표시 전환 인벤토리 동작을 처리한다.
    if (bOpen)
    {
        SelectedItemId = GetSelectedItemId();
    }

    ApplyUIMode();
}

/** UI에서 선택한 표시 전환 퀘스트 목록 동작을 처리한다. */
void AFablePlayerController::UIToggleQuests()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEndOpen)
    {
        return;
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    const bool bOpen = !bQuestsOpen;
    UIClosePanels();
    bQuestsOpen = bOpen;
    ApplyUIMode();
}

/** UI에서 선택한 표시 전환 도움말 동작을 처리한다. */
void AFablePlayerController::UIToggleHelp()
{
    const bool bOpen = !bHelpOpen;
    UIClosePanels();
    bHelpOpen = bOpen;
    ApplyUIMode();
}

/** UI에서 선택한 표시 전환 스킬 동작을 처리한다. */
void AFablePlayerController::UIToggleSkills()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEndOpen)
    {
        return;
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const bool bOpen = !bSkillsOpen;
    UIClosePanels();
    bSkillsOpen = bOpen;
    SkillPanelTab = 0;
    SelectedSkillSlot = FMath::Clamp(SelectedSkillSlot, 0, 3);
    ApplyUIMode();
}

/** 파티 관리 창을 열거나 닫고 게임 입력 초점을 조정한다. */
void AFablePlayerController::UIToggleParty()
{
    // 시작 화면과 안전 이탈 중에는 게임 파티 UI를 열지 않는다.
    if (bFrontEndOpen || bOnlineLeaveInputBlocked)
    {
        return;
    }

    // 기존 창을 닫고 한 개의 관리 창에 입력 초점을 둔다.
    const bool bOpen = !bPartyOpen;
    UIClosePanels();
    bPartyOpen = bOpen;
    PartyPanelTab = EFablePartyPanelTab::Members;
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    PartyExpeditionAtOpen = Profile ? Profile->ActiveExpeditionId : FGuid();
    ApplyUIMode();
}

/** 파티 창의 구성·원정 탭을 전환한다. */
void AFablePlayerController::UISelectPartyTab(EFablePartyPanelTab Tab)
{
    if (!bPartyOpen || (Tab != EFablePartyPanelTab::Members && Tab != EFablePartyPanelTab::Expedition &&
                        Tab != EFablePartyPanelTab::Result))
    {
        return;
    }

    PartyPanelTab = Tab;
    if (Tab == EFablePartyPanelTab::Result)
    {
        UIOpenExpeditionResult();
    }
}

/** 포털과 G 키에서 공통으로 사용하는 원정 준비 창을 연다. */
void AFablePlayerController::UIOpenPartyExpedition()
{
    if (bFrontEndOpen || bOnlineLeaveInputBlocked)
    {
        return;
    }

    UIClosePanels();
    bPartyOpen = true;
    PartyPanelTab = EFablePartyPanelTab::Expedition;
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    PartyExpeditionAtOpen = Profile ? Profile->ActiveExpeditionId : FGuid();
    ApplyUIMode();
}

/** 서버가 복제한 선택에 대해 자기 준비 의사를 명시한다. */
void AFablePlayerController::UISetPartyReady(bool bReady)
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    UIRequestPartyReady(Party ? Party->PartyId : FGuid(), Party ? Party->PreparationRevision : 0, bReady);
}

/** 현재 서버 준비 명부로 파티장 출발을 요청한다. */
void AFablePlayerController::UIEnterPreparedParty()
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    if (Party && IsPartyRetryPreparation())
    {
        UIRequestRetryExpedition(Party->PartyId, Party->PreparationRevision, Party->RetrySourceExpeditionId,
                                 Party->RetrySourceCompletionId);
        return;
    }
    UIRequestPartyEntry(Party ? Party->PartyId : FGuid(), Party ? Party->PreparationRevision : 0);
}

/** 선택할 던전·난이도와 관찰한 파티 리비전을 서버에 전달한다. */
void AFablePlayerController::UIRequestPartySelection(FGuid PartyId, int32 PreparationRevision, EFableRegion Region,
                                                     EFableDifficulty Difficulty)
{
    // 동일한 소유 RPC를 일반 버튼과 지연된 화면 요청에서 공통으로 사용한다.
    if (!bPartyOpen || bOnlineLeaveInputBlocked || IsPartyPreparationPending())
    {
        return;
    }

    PendingPartyRequestId = FGuid::NewGuid();
    bPendingPartyEntry = false;
    bPartyPreparationSucceeded = false;
    PartyPreparationMessage = TEXT("서버에서 원정 선택을 확인하고 있습니다.");
    ServerSelectPartyExpedition(PendingPartyRequestId, PartyId, PreparationRevision, Region, Difficulty);
}

/** 준비할 파티 리비전과 자기 의사를 서버에 전달한다. */
void AFablePlayerController::UIRequestPartyReady(FGuid PartyId, int32 PreparationRevision, bool bReady)
{
    if (!bPartyOpen || bOnlineLeaveInputBlocked || IsPartyPreparationPending())
    {
        return;
    }

    PendingPartyRequestId = FGuid::NewGuid();
    bPendingPartyEntry = false;
    bPartyPreparationSucceeded = false;
    PartyPreparationMessage = TEXT("서버에서 준비 상태를 확인하고 있습니다.");
    ServerSetPartyReady(PendingPartyRequestId, PartyId, PreparationRevision, bReady);
}

/** 입장할 파티 리비전만 전달하고 목적지는 서버가 결정한다. */
void AFablePlayerController::UIRequestPartyEntry(FGuid PartyId, int32 PreparationRevision)
{
    if (!bPartyOpen || bOnlineLeaveInputBlocked || IsPartyPreparationPending())
    {
        return;
    }

    PendingPartyRequestId = FGuid::NewGuid();
    bPendingPartyEntry = true;
    bPartyPreparationSucceeded = false;
    PartyPreparationMessage = TEXT("서버에서 전원의 준비와 입장을 확인하고 있습니다.");
    ServerEnterPreparedParty(PendingPartyRequestId, PartyId, PreparationRevision);
}

/** 파티장이 원정 선택을 변경할 수 있는지 표시한다. */
bool AFablePlayerController::CanSelectPartyExpedition() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    return Party && Profile && !IsPartyRetryPreparation() && Party->LeaderKey == Profile->PartyPlayerKey &&
           !IsPartyPreparationPending() && IsExpeditionPreparationSafe(this);
}

/** 초대·접속·개인 자격상 준비 버튼을 누를 수 있는지 표시한다. */
bool AFablePlayerController::CanSetPartyReady() const
{
    const AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const FFablePartyRecord* Party = FindControllerParty(this);
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    if (!Party || !Profile || !Hero || !Hero->IsAlive() || !Profile->bProfileLoaded ||
        !Profile->IsExpeditionEligible(Party->SelectedRegion) || IsPartyPreparationPending() ||
        !(IsPartyRetryPreparation() ? IsRetryPreparationSafe(this) : IsExpeditionPreparationSafe(this)) ||
        HasExpeditionInvitation(Manager, Party) ||
        (Profile->IsBackendEnabled() && (!Profile->IsBackendReady() || Profile->IsBackendSavePending())))
    {
        return false;
    }

    return !Party->Members.ContainsByPredicate(
        [](const FFablePartyMember& Member)
        {
            return !Member.bConnected;
        });
}

/** 현재 선택 리비전에 대한 자기 준비 상태를 반환한다. */
bool AFablePlayerController::IsPartyReady() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const FFablePartyMember* Member = Party && Profile ? Party->Members.FindByPredicate(
                                                             [&](const FFablePartyMember& Entry)
                                                             {
                                                                 return Entry.PlayerKey == Profile->PartyPlayerKey;
                                                             })
                                                       : nullptr;
    return Member && Member->ReadyRevision == Party->PreparationRevision;
}

/** 현재 화면에서 파티장 출발 버튼을 사용할 수 있는지 표시한다. */
bool AFablePlayerController::CanEnterPreparedParty() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!Party || !Profile || Party->LeaderKey != Profile->PartyPlayerKey || !CanSetPartyReady())
    {
        return false;
    }
    if ((!IsPartyRetryPreparation() && !CanSelectPartyExpedition()) ||
        (!Profile->ActiveExpeditionId.IsValid() && NearestService(this) != EFableService::Portal))
    {
        return false;
    }

    return !Party->Members.IsEmpty() && !Party->Members.ContainsByPredicate(
                                            [&](const FFablePartyMember& Member)
                                            {
                                                return !Member.bConnected ||
                                                       Member.ReadyRevision != Party->PreparationRevision;
                                            });
}

/** 파티 원정의 현재 준비 상태를 설명한다. */
FString AFablePlayerController::GetPartyPreparationStatus() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    const AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!Party)
    {
        return TEXT("먼저 파티 구성 탭에서 파티를 만드세요. 혼자 만든 파티도 준비 후 출발할 수 있습니다.");
    }

    if (Profile && Profile->ActiveExpeditionId.IsValid() && !IsPartyRetryPreparation())
    {
        return CanSetAbandonConsent() ? GetAbandonConsentStatus()
                                      : TEXT("원정이 끝났습니다. 결과 탭에서 보상과 재도전을 확인하세요.");
    }

    if (HasExpeditionInvitation(Manager, Party))
    {
        return TEXT("보낸 초대의 응답을 기다리고 있습니다. 수락·거절 또는 만료 후 준비할 수 있습니다.");
    }

    if (!(IsPartyRetryPreparation() ? IsRetryPreparationSafe(this) : IsExpeditionPreparationSafe(this)))
    {
        return TEXT("마을의 안전한 상태에서 원정을 준비하세요. 다른 전투와 구조가 끝날 때까지 기다리세요.");
    }

    int32 ReadyCount = 0;
    for (const FFablePartyMember& Member : Party->Members)
    {
        if (!Member.bConnected)
        {
            return TEXT("연결이 끊긴 구성원의 재접속을 기다리고 있습니다.");
        }

        ReadyCount += Member.ReadyRevision == Party->PreparationRevision ? 1 : 0;
    }

    if (ReadyCount != Party->Members.Num())
    {
        return FString::Printf(
            TEXT("준비 %d / %d명 · 각자 준비 버튼을 누르세요. 선택이나 구성이 바뀌면 준비가 해제됩니다."), ReadyCount,
            Party->Members.Num());
    }

    if (IsPartyRetryPreparation())
    {
        return Profile && !Profile->ActiveExpeditionId.IsValid() && NearestService(this) != EFableService::Portal
                   ? TEXT("전원 재도전 준비 완료 · 파티장이 마을의 원정 포털 가까이에서 출발하세요.")
                   : TEXT("전원 재도전 준비 완료 · 파티장이 출발하면 새로운 원정으로 함께 이동합니다.");
    }

    return NearestService(this) == EFableService::Portal
               ? TEXT("전원 준비 완료 · 파티장이 출발하면 서버가 자격과 이동 가능 여부를 다시 확인합니다.")
               : TEXT("전원 준비 완료 · 파티장이 마을의 원정 포털 가까이에서 출발하세요.");
}

/** 새 파티 생성을 소유 플레이어의 서버에 요청한다. */
void AFablePlayerController::UICreateParty()
{
    if (bPartyOpen && !bOnlineLeaveInputBlocked)
    {
        ServerCreateParty();
    }
}

/** 접속한 대상 플레이어에게 파티 초대를 요청한다. */
void AFablePlayerController::UIInviteParty(int32 TargetPlayerId)
{
    if (bPartyOpen && !bOnlineLeaveInputBlocked && TargetPlayerId >= 0)
    {
        ServerInviteParty(TargetPlayerId);
    }
}

/** 선택한 초대장의 수락 또는 거절을 서버에 요청한다. */
void AFablePlayerController::UIRespondPartyInvite(FGuid InviteId, bool bAccept)
{
    if (bPartyOpen && !bOnlineLeaveInputBlocked && InviteId.IsValid())
    {
        ServerRespondPartyInvite(InviteId, bAccept);
    }
}

/** 현재 파티 탈퇴를 서버에 요청한다. */
void AFablePlayerController::UILeaveParty()
{
    if (bPartyOpen && !bOnlineLeaveInputBlocked)
    {
        ServerLeaveParty();
    }
}

/** 현재 파티의 파티장 위임을 서버에 요청한다. */
void AFablePlayerController::UITransferPartyLeader(int32 TargetPlayerId)
{
    if (bPartyOpen && !bOnlineLeaveInputBlocked && TargetPlayerId >= 0)
    {
        ServerTransferPartyLeader(TargetPlayerId);
    }
}

/** 소유 컨트롤러의 파티 생성 요청을 서버 파티 관리자에 전달한다. */
void AFablePlayerController::ServerCreateParty_Implementation()
{
    // RPC의 송신자 컨트롤러를 그대로 전달해 서버가 신원을 판정한다.
    if (AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld()))
    {
        Manager->ExecuteAction(this, EFablePartyAction::Create);
    }
}

/** 대상 ID만 전달하고 초대 권한과 연결 상태는 서버에서 판정한다. */
void AFablePlayerController::ServerInviteParty_Implementation(int32 TargetPlayerId)
{
    if (AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld()))
    {
        Manager->ExecuteAction(this, EFablePartyAction::Invite, TargetPlayerId);
    }
}

/** 초대 ID와 응답 의사를 전달하고 초대 소유 여부는 서버에서 판정한다. */
void AFablePlayerController::ServerRespondPartyInvite_Implementation(FGuid InviteId, bool bAccept)
{
    if (AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld()))
    {
        Manager->ExecuteAction(this, bAccept ? EFablePartyAction::Accept : EFablePartyAction::Decline, INDEX_NONE,
                               InviteId);
    }
}

/** 소유 컨트롤러의 현재 파티 탈퇴 요청을 서버에서 처리한다. */
void AFablePlayerController::ServerLeaveParty_Implementation()
{
    if (AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld()))
    {
        Manager->ExecuteAction(this, EFablePartyAction::Leave);
    }
}

/** 파티장 위임 권한과 대상의 현재 소속을 서버에서 검증한다. */
void AFablePlayerController::ServerTransferPartyLeader_Implementation(int32 TargetPlayerId)
{
    if (AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld()))
    {
        Manager->ExecuteAction(this, EFablePartyAction::TransferLeader, TargetPlayerId);
    }
}

/** 선택 요청은 요청자 소유권·파티장·현재 리비전을 서버에서 검증한다. */
void AFablePlayerController::ServerSelectPartyExpedition_Implementation(FGuid RequestId, FGuid PartyId,
                                                                        int32 PreparationRevision, EFableRegion Region,
                                                                        EFableDifficulty Difficulty)
{
    AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const int32 NoticeBefore = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess = Manager && Manager->SelectExpedition(this, PartyId, PreparationRevision, Region, Difficulty);
    ReplyPartyPreparation(RequestId, bSuccess, NoticeBefore);
}

/** 자기 준비 상태만 변경하며 다른 구성원의 준비를 대신 바꾸지 않는다. */
void AFablePlayerController::ServerSetPartyReady_Implementation(FGuid RequestId, FGuid PartyId,
                                                                int32 PreparationRevision, bool bReady)
{
    AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const int32 NoticeBefore = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess = Manager && Manager->SetExpeditionReady(this, PartyId, PreparationRevision, bReady);
    ReplyPartyPreparation(RequestId, bSuccess, NoticeBefore);
}

/** 서버가 전원 준비·이동을 검증한 뒤 출발 결과를 보낸다. */
void AFablePlayerController::ServerEnterPreparedParty_Implementation(FGuid RequestId, FGuid PartyId,
                                                                     int32 PreparationRevision)
{
    AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const int32 NoticeBefore = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess = Manager && Manager->StartExpedition(this, PartyId, PreparationRevision);
    ReplyPartyPreparation(RequestId, bSuccess, NoticeBefore);
}

/** 이미 발급된 자기 원정 참가 증서로 복귀하고 이동 결과를 확인한다. */
void AFablePlayerController::ServerResumePartyExpedition_Implementation(FGuid RequestId, FGuid PartyId,
                                                                        int32 PreparationRevision)
{
    AFableGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AFableGameMode>() : nullptr;
    AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const FFablePartyRecord* Party = FindControllerParty(this);
    const int32 NoticeBefore = Profile ? Profile->NoticeSerial : INDEX_NONE;
    bool bSuccess = false;

    // 새 입장과 달리 같은 캐릭터에 이미 발급된 참가 증서만 사용한다.
    if (Mode && Profile && Party && Party->PartyId == PartyId && Party->PreparationRevision == PreparationRevision &&
        Mode->IsExpeditionParticipant(Profile) && PawnRegion(this) == EFableRegion::Village &&
        NearestService(this) == EFableService::Portal)
    {
        const AFablePartyEncounter* Encounter = Mode->GetExpeditionFor(Profile);
        if (!Encounter)
        {
            ReplyPartyPreparation(RequestId, false, NoticeBefore);
            return;
        }
        const FFableExpeditionTicket Ticket = Encounter->GetTicket();
        Mode->RequestEnterRegion(this, Ticket.Region, Ticket.Difficulty);
        const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
        bSuccess = Mode->IsExpeditionParticipant(Profile) && Hero && Hero->IsAlive() &&
                   FableAdventure::For(Profile).IsInRegion(Ticket.Region, Hero->GetActorLocation());
    }
    else if (Profile)
    {
        Profile->SetNotice(TEXT("현재 원정의 참가자와 파티 정보를 다시 확인하세요."));
    }

    ReplyPartyPreparation(RequestId, bSuccess, NoticeBefore);
}

/** 서버 요청 후 명부의 실제 리비전과 안내를 응답으로 전달한다. */
void AFablePlayerController::ReplyPartyPreparation(FGuid RequestId, bool bSuccess, int32 NoticeBefore)
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const FFablePartyRecord* Party = FindControllerParty(this);
    const FString Reason = Profile && Profile->NoticeSerial != NoticeBefore ? Profile->LastNotice
                           : bSuccess
                               ? TEXT("서버에서 원정 요청을 확인했습니다.")
                               : TEXT("서버에서 원정 요청을 처리하지 못했습니다. 연결과 프로필 상태를 확인하세요.");
    ClientPartyPreparationResult(RequestId, bSuccess, Party ? Party->PreparationRevision : 0, Reason);
}

/** 해당 요청의 성공·거절 결과를 소유 클라이언트의 준비 화면에 반영한다. */
void AFablePlayerController::ClientPartyPreparationResult_Implementation(FGuid RequestId, bool bSuccess,
                                                                         int32 PreparationRevision,
                                                                         const FString& Reason)
{
    // 이전 창의 지연 응답은 현재 요청이나 다른 파티의 화면을 바꾸지 않는다.
    if (!PendingPartyRequestId.IsValid() || RequestId != PendingPartyRequestId)
    {
        return;
    }

    const bool bEntered = bPendingPartyEntry && bSuccess;
    PendingPartyRequestId.Invalidate();
    bPendingPartyEntry = false;
    bPartyPreparationSucceeded = bSuccess;
    PartyPreparationMessage = Reason;
    PartyPreparationResultRevision = PreparationRevision;

    // 전원의 이동을 서버가 확인한 응답만 원정 준비 창을 닫는다.
    if (bEntered && IsPartyExpeditionOpen())
    {
        UIClosePanels();
    }
}

/** 보상 조회 없이 결과 창에 입력 초점만 둔다. */
void AFablePlayerController::OpenExpeditionResultPanel()
{
    if (bFrontEndOpen || bOnlineLeaveInputBlocked)
    {
        return;
    }

    UIClosePanels();
    bPartyOpen = true;
    PartyPanelTab = EFablePartyPanelTab::Result;
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    PartyExpeditionAtOpen = Profile ? Profile->ActiveExpeditionId : FGuid();
    ApplyUIMode();
}

/** 본인에게 복제된 결과만 반환하고 잘못된 수신자 정보는 표시하지 않는다. */
const FFablePersonalExpeditionResult* AFablePlayerController::GetDisplayedExpeditionResult() const
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!Profile)
    {
        return nullptr;
    }

    const FFablePersonalExpeditionResult& Result = Profile->GetExpeditionResult();
    return Result.IsValid() && Result.RecipientPublicKey == Profile->PartyPlayerKey ? &Result : nullptr;
}

/** 화면 확인용 식별자는 자동 검증에서만 사용하고 플레이어에게 출력하지 않는다. */
FGuid AFablePlayerController::GetDisplayedResultCompletionId() const
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    return Result ? Result->CompletionId : FGuid();
}

/** 실제 개인 영수증을 조회하는 결과 창을 열고 이전 화면의 보상을 재계산하지 않는다. */
void AFablePlayerController::UIOpenExpeditionResult(FGuid ExpeditionId)
{
    OpenExpeditionResultPanel();
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    if (!ExpeditionId.IsValid())
    {
        ExpeditionId = Result ? Result->Ticket.ExpeditionId : Profile ? Profile->ActiveExpeditionId : FGuid();
    }
    if (ExpeditionId.IsValid())
    {
        UIRequestExpeditionResult(ExpeditionId);
    }
}

/** 별도 결과 요청의 상관 ID와 안내를 준비한다. */
bool AFablePlayerController::BeginExpeditionAction(const FString& Message)
{
    if (!bPartyOpen || bOnlineLeaveInputBlocked || IsExpeditionActionPending())
    {
        return false;
    }

    PendingExpeditionActionId = FGuid::NewGuid();
    bExpeditionActionSucceeded = false;
    ExpeditionActionMessage = Message;
    return true;
}

/** 현재 개인 결과를 명시한 원정 UUID로 다시 요청한다. */
void AFablePlayerController::UIRequestExpeditionResult(FGuid ExpeditionId)
{
    if (BeginExpeditionAction(TEXT("원정 기록을 확인하고 있습니다.")))
    {
        ServerRequestExpeditionResult(PendingExpeditionActionId, ExpeditionId);
    }
}

/** 현재 보이는 종료 결과를 읽었다는 확인만 서버에 보낸다. */
void AFablePlayerController::UIAcknowledgeExpeditionResult()
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    if (Result)
    {
        UIRequestAcknowledgeExpeditionResult(Result->Ticket.ExpeditionId, Result->CompletionId, Result->ResultRevision);
    }
}

/** 지연된 화면의 결과 확인도 같은 제품 RPC를 사용한다. */
void AFablePlayerController::UIRequestAcknowledgeExpeditionResult(FGuid ExpeditionId, FGuid CompletionId,
                                                                  int32 ResultRevision)
{
    if (BeginExpeditionAction(TEXT("결과 확인을 기록하고 있습니다.")))
    {
        ServerAcknowledgeExpeditionResult(PendingExpeditionActionId, ExpeditionId, CompletionId, ResultRevision);
    }
}

/** 현재 서버 선택이 명시적인 재도전 준비인지 반환한다. */
bool AFablePlayerController::IsPartyRetryPreparation() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    return Party && Party->RetrySourceExpeditionId.IsValid() && Party->RetrySourceCompletionId.IsValid();
}

/** 표시된 개인 결과로 재도전 준비를 시작할 수 있는지 안내한다. */
bool AFablePlayerController::CanPrepareExpeditionRetry() const
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    const FFablePartyRecord* Party = FindControllerParty(this);
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const auto View = FableAdventure::For(this);
    const auto* Encounter = View ? Cast<AFablePartyEncounter>(View.ReplicationOwner()) : nullptr;
    if (Profile && Profile->ActiveExpeditionId.IsValid() &&
        (!Result || Profile->ActiveExpeditionId != Result->Ticket.ExpeditionId || !Encounter ||
         Encounter->IsReturning() || View->AdventureStage != EFableAdventureStage::Cleared))
    {
        return false;
    }

    return Result && Result->IsTerminal() && Result->bAcknowledged && Result->bSaveReady &&
           (!Result->bRequiresBackendCommit || Result->bAllGrantsCommitted) && !Result->RetryExpeditionId.IsValid() &&
           Party && Profile && Hero && Hero->IsAlive() && Party->PartyId == Result->Ticket.PartyId &&
           (Party->LeaderKey == Profile->PartyPlayerKey ||
            (Party->RetrySourceExpeditionId == Result->Ticket.ExpeditionId &&
             Party->RetrySourceCompletionId == Result->CompletionId)) &&
           !IsPartyPreparationPending() && !IsExpeditionActionPending() &&
           (!Profile->IsBackendEnabled() || (Profile->IsBackendReady() && !Profile->IsBackendSavePending()));
}

/** 표시된 결과를 바탕으로 별도의 명시적 재도전 준비 창을 연다. */
void AFablePlayerController::UIBeginExpeditionRetry()
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    const FFablePartyRecord* Party = FindControllerParty(this);
    if (!Result || !Party)
    {
        return;
    }

    // 재도전 키 입력도 본인이 결과를 명시적으로 확인하기 전에는 준비 요청을 보내지 않는다.
    if (!Result->bAcknowledged)
    {
        OpenExpeditionResultPanel();
        ExpeditionActionMessage = TEXT("재도전하려면 먼저 E 결과 확인을 누르세요. 받은 보상은 다시 지급되지 않습니다.");
        return;
    }

    const FGuid SourceId = Result->Ticket.ExpeditionId, CompletionId = Result->CompletionId;
    const FGuid PartyId = Party->PartyId;
    const int32 Revision = Party->PreparationRevision;
    const bool bAlreadyPrepared =
        Party->RetrySourceExpeditionId == SourceId && Party->RetrySourceCompletionId == CompletionId;
    UIOpenPartyExpedition();
    if (!bAlreadyPrepared)
    {
        UIRequestPrepareExpeditionRetry(PartyId, Revision, SourceId, CompletionId);
    }
}

/** 이전 완료 원정과 현재 명부 리비전으로 재도전을 준비한다. */
void AFablePlayerController::UIRequestPrepareExpeditionRetry(FGuid PartyId, int32 PreparationRevision,
                                                             FGuid OldExpeditionId, FGuid CompletionId)
{
    if (!bPartyOpen || bOnlineLeaveInputBlocked || IsPartyPreparationPending())
    {
        return;
    }

    PartyPanelTab = EFablePartyPanelTab::Expedition;
    PendingPartyRequestId = FGuid::NewGuid();
    bPendingPartyEntry = false;
    bPartyPreparationSucceeded = false;
    PartyPreparationMessage = TEXT("파티의 재도전 준비를 확인하고 있습니다.");
    ServerPrepareExpeditionRetry(PendingPartyRequestId, PartyId, PreparationRevision, OldExpeditionId, CompletionId);
}

/** 새 출발도 기존 원정과 다른 증서가 발급될 때까지 기다린다. */
void AFablePlayerController::UIRequestRetryExpedition(FGuid PartyId, int32 PreparationRevision, FGuid OldExpeditionId,
                                                      FGuid CompletionId)
{
    if (!bPartyOpen || bOnlineLeaveInputBlocked || IsPartyPreparationPending())
    {
        return;
    }

    PendingPartyRequestId = FGuid::NewGuid();
    bPendingPartyEntry = true;
    bPartyPreparationSucceeded = false;
    PartyPreparationMessage = TEXT("모두의 준비와 재도전 입장을 확인하고 있습니다.");
    ServerRetryExpedition(PendingPartyRequestId, PartyId, PreparationRevision, OldExpeditionId, CompletionId);
}

/** 결과에 포함된 실제 지급 장비를 반환한다. */
TArray<FFableItem> AFablePlayerController::GetExpeditionResultItems() const
{
    TArray<FFableItem> Items;
    if (const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult())
    {
        for (const FFablePersonalGrantReceipt& Receipt : Result->Receipts)
        {
            if (Receipt.ExpeditionId == Result->Ticket.ExpeditionId &&
                Receipt.RecipientPublicKey == Result->RecipientPublicKey)
            {
                Items.Append(Receipt.GrantedItems);
            }
        }
    }
    return Items;
}

/** 장비 목록의 현재 행을 선택하고 지급 당시 옵션을 읽는다. */
void AFablePlayerController::UISelectResultItem(int32 Index)
{
    const TArray<FFableItem> Items = GetExpeditionResultItems();
    SelectedResultItemIndex = FMath::Clamp(Index, 0, FMath::Max(0, Items.Num() - 1));
}

/** 현재 선택한 결과 장비의 행을 반환한다. */
int32 AFablePlayerController::GetSelectedResultItemIndex() const
{
    return FMath::Clamp(SelectedResultItemIndex, 0, FMath::Max(0, GetExpeditionResultItems().Num() - 1));
}

/** 메모리 전용 기록을 영구 저장 완료라고 판정하지 않는다. */
bool AFablePlayerController::IsExpeditionResultSaved() const
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    return Result && Result->bRequiresBackendCommit && Result->bAllGrantsCommitted;
}

/** 해당 지급 영수증의 실제 저장 확인 상태를 표시한다. */
FString AFablePlayerController::GetExpeditionResultSaveText() const
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!Result)
    {
        return TEXT("기록 확인 중");
    }
    if (!Result->bRequiresBackendCommit)
    {
        return TEXT("임시 기록");
    }
    if (IsExpeditionResultSaved())
    {
        return TEXT("저장 완료");
    }
    if (Profile && !Profile->bLastSaveSucceeded && !Profile->IsBackendSavePending())
    {
        return TEXT("저장 오류");
    }
    return TEXT("저장 중");
}

/** 현재 완료 원정의 기존 포털 가까이 있는지 표시한다. */
bool AFablePlayerController::CanReturnFromExpeditionResult() const
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const auto View = FableAdventure::For(this);
    return Result && Result->Outcome == EFableExpeditionOutcome::Cleared && Profile && Hero && Hero->IsAlive() &&
           Profile->ActiveExpeditionId == Result->Ticket.ExpeditionId && View &&
           View->AdventureStage == EFableAdventureStage::Cleared &&
           NearestService(this) == EFableService::ReturnPortal && !IsExpeditionActionPending();
}

/** 결과에서 기존 귀환 포털 정책을 유지하며 이동 안내 또는 귀환을 요청한다. */
void AFablePlayerController::UIReturnFromExpeditionResult()
{
    const FFablePersonalExpeditionResult* Result = GetDisplayedExpeditionResult();
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!Result || !Profile || Profile->ActiveExpeditionId != Result->Ticket.ExpeditionId)
    {
        return;
    }
    if (CanReturnFromExpeditionResult())
    {
        UIRequestExpeditionReturn(Result->Ticket.ExpeditionId, Result->CompletionId);
        return;
    }

    // 포털을 건너뛰어 귀환하지 않고 기존 월드의 포털 위치로 안내한다.
    ResultPortalGuidanceExpeditionId = Result->Ticket.ExpeditionId;
    UIClosePanels();
}

/** 관찰한 원정·완료 식별자로 같은 귀환 제품 요청을 보낸다. */
void AFablePlayerController::UIRequestExpeditionReturn(FGuid ExpeditionId, FGuid CompletionId)
{
    if (BeginExpeditionAction(TEXT("함께 마을로 돌아갈 수 있는지 확인하고 있습니다.")))
    {
        ServerReturnExpeditionResult(PendingExpeditionActionId, ExpeditionId, CompletionId);
    }
}

/** 철수 버튼이 현재 진행에서 사용 가능한지 표시한다. */
bool AFablePlayerController::CanSetAbandonConsent() const
{
    const auto View = FableAdventure::For(this);
    const auto* Encounter = View ? Cast<AFablePartyEncounter>(View.ReplicationOwner()) : nullptr;
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    const auto* Hero = GetPawn<AFableCharacter>();
    return Encounter && !Encounter->IsReturning() && Profile && Hero && Hero->IsAlive() &&
           !View->bPartyRecoveryPending &&
           (View->AdventureStage == EFableAdventureStage::Dungeon ||
            View->AdventureStage == EFableAdventureStage::Boss) &&
           (!Profile->IsBackendEnabled() || (Profile->IsBackendReady() && !Profile->IsBackendSavePending())) &&
           !bOnlineLeaveInputBlocked && !IsExpeditionActionPending();
}

/** 현재 캐릭터가 서버에 철수 동의를 제출했는지 반환한다. */
bool AFablePlayerController::HasAbandonConsent() const
{
    const auto View = FableAdventure::For(this);
    const auto* Encounter = View ? Cast<AFablePartyEncounter>(View.ReplicationOwner()) : nullptr;
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    return Encounter && Profile && Encounter->GetDepartureVote().ConsentingMemberKeys.Contains(Profile->PartyPlayerKey);
}

/** 철수 연결 명부가 바뀌면 과거 동의 리비전을 사용하지 않는다. */
int32 AFablePlayerController::GetAbandonVoteRevision() const
{
    const auto View = FableAdventure::For(this);
    const auto* Encounter = View ? Cast<AFablePartyEncounter>(View.ReplicationOwner()) : nullptr;
    return Encounter ? Encounter->GetDepartureVote().Revision : 0;
}

/** 공개 참가 증서의 철수 동의 수와 연결 상태를 설명한다. */
FString AFablePlayerController::GetAbandonConsentStatus() const
{
    const auto View = FableAdventure::For(this);
    const auto* Encounter = View ? Cast<AFablePartyEncounter>(View.ReplicationOwner()) : nullptr;
    if (!Encounter)
    {
        return TEXT("현재 원정 정보를 기다리고 있습니다.");
    }
    const int32 Required = Encounter->GetTicket().MemberKeys.Num();
    return FString::Printf(
        TEXT(
            "철수 동의 %d / %d명 · 원래 참가자 전원이 동의하면 함께 마을로 돌아갑니다.\n동료의 연결이 바뀌면 동의가 해제됩니다."),
        Encounter->GetDepartureVote().ConsentingMemberKeys.Num(), Required);
}

/** 진행 중인 현재 원정의 철수 동의를 명시한다. */
void AFablePlayerController::UISetAbandonConsent(bool bConsent)
{
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    UIRequestAbandonConsent(Profile ? Profile->ActiveExpeditionId : FGuid(), GetAbandonVoteRevision(), bConsent);
}

/** 지연되거나 다른 원정의 동의도 서버에서 판정할 수 있는 공통 요청이다. */
void AFablePlayerController::UIRequestAbandonConsent(FGuid ExpeditionId, int32 DepartureRevision, bool bConsent)
{
    if (BeginExpeditionAction(TEXT("철수 동의를 확인하고 있습니다.")))
    {
        ServerSetAbandonConsent(PendingExpeditionActionId, ExpeditionId, DepartureRevision, bConsent);
    }
}

/** 보상 조회와 확인은 각각 읽기·확인 경로만 호출한다. */
void AFablePlayerController::ServerRequestExpeditionResult_Implementation(FGuid RequestId, FGuid ExpeditionId)
{
    AFableGameMode* Mode = AFableGameMode::Find(GetWorld());
    auto* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Before = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess = Mode && Mode->RequestExpeditionResult(this, ExpeditionId);
    if (Profile && Profile->NoticeSerial == Before)
    {
        Profile->SetNotice(bSuccess ? TEXT("모험 기록을 불러왔습니다.")
                                    : TEXT("본인의 모험 기록을 찾지 못했습니다. 원정 정보를 다시 확인하세요."));
    }
    ReplyExpeditionAction(RequestId, bSuccess, Before);
}

/** 結果確認は読んだ印だけを変更し、報酬を再適用しない。 */
void AFablePlayerController::ServerAcknowledgeExpeditionResult_Implementation(FGuid RequestId, FGuid ExpeditionId,
                                                                              FGuid CompletionId, int32 ResultRevision)
{
    AFableGameMode* Mode = AFableGameMode::Find(GetWorld());
    auto* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Before = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess = Mode && Mode->AcknowledgeExpeditionResult(this, ExpeditionId, CompletionId, ResultRevision);
    if (Profile && Profile->NoticeSerial == Before)
    {
        Profile->SetNotice(bSuccess ? TEXT("결과를 확인했습니다. 받은 보상은 다시 지급되지 않습니다.")
                                    : TEXT("모험 결과가 갱신되었습니다. 현재 기록을 다시 확인하세요."));
    }
    ReplyExpeditionAction(RequestId, bSuccess, Before);
}

/** 재도전 준비는 원래 완료 원정과 명부를 서버에서 검증한다. */
void AFablePlayerController::ServerPrepareExpeditionRetry_Implementation(FGuid RequestId, FGuid PartyId,
                                                                         int32 PreparationRevision,
                                                                         FGuid OldExpeditionId, FGuid CompletionId)
{
    auto* Manager = AFablePartyManager::Find(GetWorld());
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Before = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess =
        Manager && Manager->PrepareExpeditionRetry(this, PartyId, PreparationRevision, OldExpeditionId, CompletionId);
    ReplyPartyPreparation(RequestId, bSuccess, Before);
}

/** 전원의 명시적 동의로 새 원정에 입장한다. */
void AFablePlayerController::ServerRetryExpedition_Implementation(FGuid RequestId, FGuid PartyId,
                                                                  int32 PreparationRevision, FGuid OldExpeditionId,
                                                                  FGuid CompletionId)
{
    auto* Mode = AFableGameMode::Find(GetWorld());
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Before = Profile ? Profile->NoticeSerial : INDEX_NONE;
    const bool bSuccess =
        Mode && Mode->RequestRetryExpedition(this, PartyId, PreparationRevision, OldExpeditionId, CompletionId);
    ReplyPartyPreparation(RequestId, bSuccess, Before);
}

/** 원래 완료 원정의 기존 귀환 포털 검사를 사용한다. */
void AFablePlayerController::ServerReturnExpeditionResult_Implementation(FGuid RequestId, FGuid ExpeditionId,
                                                                         FGuid CompletionId)
{
    auto* Mode = AFableGameMode::Find(GetWorld());
    auto* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Before = Profile ? Profile->NoticeSerial : INDEX_NONE;
    auto* Encounter = Mode ? Mode->FindExpedition(ExpeditionId) : nullptr;
    const auto* Result = Mode && Profile ? Mode->GetResultByExpedition(Profile, ExpeditionId) : nullptr;
    bool bSuccess = false;

    // 원정 UUID와 개인 완료 결과를 먼저 묶고 기존 포털 거리·봉인 조사를 그대로 검사한다.
    if (Encounter && Profile && Result && Result->IsTerminal() && Result->CompletionId == CompletionId &&
        Profile->ActiveExpeditionId == ExpeditionId && Encounter->IsParticipant(Profile))
    {
        Encounter->RequestReturnVillage(this);
        const auto* Hero = GetPawn<AFableCharacter>();
        bSuccess = (IsValid(Encounter) && Encounter->IsReturning()) ||
                   (!Profile->ActiveExpeditionId.IsValid() && Hero && Hero->IsAlive() &&
                    FableAdventure::For(Profile).IsInRegion(EFableRegion::Village, Hero->GetActorLocation()));
    }
    else if (Profile)
    {
        Profile->SetNotice(TEXT("현재 완료 원정의 귀환 정보를 다시 확인하세요."));
    }
    ReplyExpeditionAction(RequestId, bSuccess, Before);
}

/** 철수는 원래 참가 증서의 전원 동의로만 처리한다. */
void AFablePlayerController::ServerSetAbandonConsent_Implementation(FGuid RequestId, FGuid ExpeditionId,
                                                                    int32 DepartureRevision, bool bConsent)
{
    auto* Mode = AFableGameMode::Find(GetWorld());
    auto* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Before = Profile ? Profile->NoticeSerial : INDEX_NONE;
    auto* Encounter = Mode ? Mode->FindExpedition(ExpeditionId) : nullptr;
    const bool bSuccess = Encounter && Encounter->SetAbandonConsent(this, ExpeditionId, DepartureRevision, bConsent);
    if (Profile && Profile->NoticeSerial == Before)
    {
        Profile->SetNotice(bSuccess
                               ? (bConsent ? TEXT("철수에 동의했습니다. 동료의 동의를 기다립니다.")
                                           : TEXT("철수 동의를 취소했습니다."))
                               : TEXT("철수 동의를 처리하지 못했습니다. 연결 상태와 현재 원정을 다시 확인하세요."));
    }
    ReplyExpeditionAction(RequestId, bSuccess, Before);
}

/** 현재 개인 결과 리비전과 서버 거절 사유를 응답한다. */
void AFablePlayerController::ReplyExpeditionAction(FGuid RequestId, bool bSuccess, int32 NoticeBefore)
{
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    const auto* Result = GetDisplayedExpeditionResult();
    const FString Message = Profile && Profile->NoticeSerial != NoticeBefore ? Profile->LastNotice
                            : bSuccess ? TEXT("모험 기록을 확인했습니다.")
                                       : TEXT("요청을 처리하지 못했습니다. 현재 모험 상태를 다시 확인하세요.");
    ClientExpeditionActionResult(RequestId, bSuccess, Result ? Result->ResultRevision : 0, Message);
}

/** 결과 요청도 현재 소유 요청과 일치하는 서버 응답만 반영한다. */
void AFablePlayerController::ClientExpeditionActionResult_Implementation(FGuid RequestId, bool bSuccess,
                                                                         int32 ResultRevision, const FString& Reason)
{
    if (!PendingExpeditionActionId.IsValid() || RequestId != PendingExpeditionActionId)
    {
        return;
    }
    PendingExpeditionActionId.Invalidate();
    LastExpeditionActionRequestId = RequestId;
    bExpeditionActionSucceeded = bSuccess;
    ExpeditionActionResultRevision = ResultRevision;
    ExpeditionActionMessage = Reason;
}

/** UI에서 선택한 선택 스킬 동작을 처리한다. */
void AFablePlayerController::UISelectSkill(int32 Slot)
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!bSkillsOpen || Slot < 0 || Slot > 3)
    {
        return;
    }

    // 직업·스킬·특성 빌드 처리
    ClearBuildResetConfirmation();
    SkillPanelTab = 0;
    SelectedSkillSlot = Slot;
}

/** UI에서 선택한 선택 스킬 창 탭 동작을 처리한다. */
void AFablePlayerController::UISelectSkillPanelTab(int32 Tab)
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!bSkillsOpen || Tab < 0 || Tab > 1)
    {
        return;
    }

    // 직업·스킬·특성 빌드 처리
    if (SkillPanelTab != Tab)
    {
        ClearBuildResetConfirmation();
    }

    SkillPanelTab = Tab;
}

/** UI에서 선택한 특성 선택 동작을 처리한다. */
void AFablePlayerController::UISelectTalent(int32 TargetTier, EFableTalentPath Path)
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!bSkillsOpen || (TargetTier != 2 && TargetTier != 3) ||
        (Path != EFableTalentPath::First && Path != EFableTalentPath::Second))
    {
        return;
    }

    // 직업·스킬·특성 빌드 처리
    ClearBuildResetConfirmation();
    SkillPanelTab = 1;
    SelectedTalentTier = TargetTier;
    SelectedTalentPath = Path;
}

/** UI에서 선택한 선택 특성 동작을 처리한다. */
void AFablePlayerController::UIChooseTalent()
{
    if (bFrontEndOpen || !bSkillsOpen || SkillPanelTab != 1)
    {
        return;
    }
    ClearBuildResetConfirmation();
    ServerSelectTalent(SelectedTalentTier, SelectedTalentPath);
}

/** 빌드 초기화 확인 상태를 해제한다. */
void AFablePlayerController::ClearBuildResetConfirmation()
{
    bBuildResetConfirmationPending = false;
    PendingBuildResetRevision = INDEX_NONE;
}

/** UI에서 선택한 빌드 초기화 취소 동작을 처리한다. */
void AFablePlayerController::UICancelBuildReset()
{
    ClearBuildResetConfirmation();
}

/** 빌드 초기화 확인 대기 여부를 확인한다. */
bool AFablePlayerController::IsBuildResetConfirmationPending() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    return bBuildResetConfirmationPending && bSkillsOpen && State && State->HeroClass == PendingBuildResetClass &&
           State->ClassBuildRevision == PendingBuildResetRevision && State->Gold == PendingBuildResetGold &&
           State->GetCurrentClassRefundableSkillPoints() == PendingBuildResetRefund &&
           State->GetBuildResetBlockReason().IsEmpty();
}

/** UI에서 선택한 빌드 초기화 요청 동작을 처리한다. */
void AFablePlayerController::UIRequestBuildReset()
{
    // 직업·스킬·특성 빌드 조건 검사
    if (bFrontEndOpen || !bSkillsOpen)
    {
        return;
    }

    // 현재 월드와 플레이어 참조 처리
    ClearBuildResetConfirmation();
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State || !State->GetBuildResetBlockReason().IsEmpty())
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    PendingBuildResetClass = State->HeroClass;
    PendingBuildResetRevision = State->ClassBuildRevision;
    PendingBuildResetGold = State->Gold;
    PendingBuildResetRefund = State->GetCurrentClassRefundableSkillPoints();
    bBuildResetConfirmationPending = true;
}

/** UI에서 선택한 빌드 초기화 확정 동작을 처리한다. */
void AFablePlayerController::UIConfirmBuildReset()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!IsBuildResetConfirmationPending())
    {
        ClearBuildResetConfirmation();
        return;
    }

    // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
    const EFableClass ExpectedClass = PendingBuildResetClass;
    const int32 ExpectedRevision = PendingBuildResetRevision;
    ClearBuildResetConfirmation();
    ServerResetCurrentClassBuild(ExpectedClass, ExpectedRevision);
}

/** UI에서 선택한 스킬 강화 동작을 처리한다. */
void AFablePlayerController::UIUpgradeSkill(int32 Slot)
{
    ClearBuildResetConfirmation();
    if (!bFrontEndOpen)
    {
        ServerUpgradeSkill(Slot);
    }
}

/** UI에서 선택한 전직 동작을 처리한다. */
void AFablePlayerController::UIAdvanceClass()
{
    if (!bFrontEndOpen)
    {
        ServerAdvanceClass();
    }
}

/** 플레이어의 마우스 커서 표시 상태를 전환한다. */
void AFablePlayerController::ToggleCursor()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (IsAnyPanelOpen())
    {
        UIClosePanels();
        return;
    }

    bFreeCursor = !bFreeCursor;
    ApplyUIMode();
}

/** UI에서 선택한 세트 시작 직업 동작을 처리한다. */
void AFablePlayerController::UISetInitialClass(EFableClass HeroClass)
{
    if (HeroClass == EFableClass::Warrior || HeroClass == EFableClass::Mage || HeroClass == EFableClass::Archer)
    {
        InitialClass = HeroClass;
    }
    bJoinAddressFocused = false;
}

/** UI에서 선택한 세트 접속 주소 동작을 처리한다. */
void AFablePlayerController::UISetJoinAddress(const FString& Address)
{
    JoinAddress = Address.Left(128);
}

/** UI에서 선택한 입력 초점 접속 주소 동작을 처리한다. */
void AFablePlayerController::UIFocusJoinAddress()
{
    bJoinAddressFocused = true;
    bAddressSelectAll = true;
}

/** UI에서 선택한 시작 신규 게임 동작을 처리한다. */
void AFablePlayerController::UIStartNewGame(bool bHost)
{
    if (!bFrontEndOpen)
    {
        return;
    }
    if (UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        GI->StartNewGame(InitialClass, bHost);
    }
}

/** UI에서 선택한 이어하기 게임 동작을 처리한다. */
void AFablePlayerController::UIContinueGame(bool bHost)
{
    if (!bFrontEndOpen)
    {
        return;
    }
    if (UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        GI->ContinueGame(bHost);
    }
}

/** UI에서 선택한 접속 게임 동작을 처리한다. */
void AFablePlayerController::UIJoinGame()
{
    if (!bFrontEndOpen)
    {
        return;
    }
    if (UFableGameInstance* GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        GI->JoinGame(JoinAddress);
    }
}

/** UI에서 선택한 복귀 목표 시작 화면 동작을 처리한다. */
void AFablePlayerController::UIReturnToFrontEnd()
{
    const auto Profile = GetPlayerState<AFablePlayerState>();

    // HTTP 요청·응답과 연결 상태 처리
    if (Profile && Profile->IsBackendEnabled())
    {
        if (auto Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>() : nullptr;
            Flow && Flow->IsEnabled())
        {
            Flow->RequestLeave();
        }
        else
        {
            UIRequestOnlineLeave();
        }
        return;
    }

    UIStopRevive();

    // 현재 월드와 플레이어 참조 처리
    if (auto GI = Cast<UFableGameInstance>(GetGameInstance()))
    {
        GI->ReturnToFrontEnd();
    }
}

/** 안전 이탈 중 이동·전투·UI 입력을 제한한다. */
void AFablePlayerController::BlockOnlineLeaveInput()
{
    if (!bOnlineLeaveInputBlocked)
    {
        bOnlineLeaveInputBlocked = true;
        SetIgnoreMoveInput(true);
        SetIgnoreLookInput(true);
        UIStopRevive();
    }
    if (auto Hero = Cast<AFableCharacter>(GetPawn()))
    {
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Hero->GetCharacterMovement()->DisableMovement();
    }
}

/** UI에서 선택한 온라인 안전 이탈 동작을 처리한다. */
void AFablePlayerController::UIRequestOnlineLeave()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!IsLocalController() || bOnlineLeavePending)
    {
        return;
    }

    const auto Profile = GetPlayerState<AFablePlayerState>();

    // 현재 월드와 플레이어 참조 처리
    if (!Profile || !Profile->IsBackendEnabled())
    {
        if (auto Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>() : nullptr)
        {
            Flow->OnOnlineLeaveResult(false, TEXT("중앙 서버의 캐릭터 접속을 확인한 뒤 다시 시도해 주세요."));
        }
        return;
    }

    // HTTP 요청·응답과 연결 상태 처리
    UIClosePanels();
    BlockOnlineLeaveInput();
    bOnlineLeavePending = true;
    OnlineLeaveRequestId = FGuid::NewGuid();
    ServerRequestOnlineLeave(OnlineLeaveRequestId);
}

/** 온라인 안전 이탈 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerRequestOnlineLeave_Implementation(FGuid RequestId)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !RequestId.IsValid() || !GetWorld())
    {
        return;
    }

    // HTTP 요청·응답과 연결 상태 처리
    if (bServerOnlineLeavePending)
    {
        if (RequestId != ServerOnlineLeaveRequestId)
        {
            ClientOnlineLeaveResult(RequestId, false, TEXT("기존 퇴장 요청을 확인하고 있습니다."));
        }
        return;
    }

    const double Now = FPlatformTime::Seconds();

    // HTTP 요청·응답과 연결 상태 조건 검사
    if (Now - LastOnlineLeaveRequestTime < 0.5)
    {
        ClientOnlineLeaveResult(RequestId, false, TEXT("잠시 뒤 퇴장 확인을 다시 요청해 주세요."));
        return;
    }

    // HTTP 요청·응답과 연결 상태 처리
    LastOnlineLeaveRequestTime = Now;
    auto Profile = GetPlayerState<AFablePlayerState>();

    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!Profile || !Profile->IsBackendEnabled() || !Profile->bProfileLoaded)
    {
        ClientOnlineLeaveResult(RequestId, false, TEXT("중앙 프로필 접속이 완료되지 않았습니다."));
        return;
    }

    // HTTP 요청·응답과 연결 상태 처리
    bServerOnlineLeavePending = true;
    ServerOnlineLeaveRequestId = RequestId;

    // 생존 상태와 부활 진행 처리
    if (auto Hero = Cast<AFableCharacter>(GetPawn()))
    {
        Hero->CancelRevive();
    }

    // HTTP 요청·응답과 연결 상태 처리
    BlockOnlineLeaveInput();
    TWeakObjectPtr<AFablePlayerController> Weak(this);
    Profile->RequestBackendLeave(
        [Weak, RequestId](bool bSuccess, const FString& Reason)
        {
            if (auto PC = Weak.Get(); PC && PC->ServerOnlineLeaveRequestId == RequestId)
            {
                PC->bServerOnlineLeavePending = false;
                PC->ServerOnlineLeaveRequestId.Invalidate();
                PC->ClientOnlineLeaveResult(RequestId, bSuccess, Reason);
            }
        });
}

/** 서버가 전달한 온라인 안전 이탈 결과 결과를 소유 클라이언트에 반영한다. */
void AFablePlayerController::ClientOnlineLeaveResult_Implementation(FGuid RequestId, bool bSuccess,
                                                                    const FString& Reason)
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!bOnlineLeavePending || RequestId != OnlineLeaveRequestId)
    {
        return;
    }

    // HTTP 요청·응답과 연결 상태 처리
    bOnlineLeavePending = false;
    OnlineLeaveRequestId.Invalidate();
    // Both success and failure keep gameplay frozen; only the flow subsystem may travel on a confirmed success.

    // 현재 월드와 플레이어 참조 처리
    if (auto Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>() : nullptr;
        Flow && Flow->IsEnabled())
    {
        Flow->OnOnlineLeaveResult(bSuccess, Reason);
    }
    else if (bSuccess)
    {
        if (auto GI = Cast<UFableGameInstance>(GetGameInstance()))
        {
            GI->ReturnToFrontEnd();
        }
    }
}

/** 정렬된 인벤토리 표시 목록 값을 반환한다. */
TArray<int32> AFablePlayerController::GetInventoryDisplayIds() const
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    TArray<int32> DisplayIds;
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return DisplayIds;
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : State->Inventory)
    {
        DisplayIds.Add(Item.Id);
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (InventorySort == EFableInventorySort::Acquired)
    {
        return DisplayIds;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    DisplayIds.Sort(
        [State, this](int32 LeftId, int32 RightId)
        {
            // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
            const FFableItem* Left = State->FindItem(LeftId);
            const FFableItem* Right = State->FindItem(RightId);

            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (!Left || !Right)
            {
                return LeftId < RightId;
            }
            if (InventorySort == EFableInventorySort::Quality && Left->Quality != Right->Quality)
            {
                return uint8(Left->Quality) > uint8(Right->Quality);
            }
            if (InventorySort == EFableInventorySort::Quality && Left->Rarity != Right->Rarity)
            {
                return Left->Rarity > Right->Rarity;
            }
            if (Left->GetEffectivePower() != Right->GetEffectivePower())
            {
                return Left->GetEffectivePower() > Right->GetEffectivePower();
            }

            return LeftId < RightId;
        });

    return DisplayIds;
}

/** UI에서 선택한 인벤토리 정렬 전환 동작을 처리한다. */
void AFablePlayerController::UICycleInventorySort()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bInventoryOpen)
    {
        return;
    }

    const int32 RetainedId = GetSelectedItemId();
    InventorySort = static_cast<EFableInventorySort>((uint8(InventorySort) + 1) % 3);
    SelectedItemId = RetainedId;
}

/** 현재 인벤토리 정렬 방식의 이름 값을 반환한다. */
FString AFablePlayerController::GetInventorySortLabel() const
{
    return InventorySort == EFableInventorySort::Quality ? TEXT("품질 높은 순")
           : InventorySort == EFableInventorySort::Power ? TEXT("성능 높은 순")
                                                         : TEXT("획득 순");
}

/** 선택한 아이템의 지역 식별자 값을 반환한다. */
int32 AFablePlayerController::GetSelectedItemId() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!State || State->Inventory.IsEmpty())
    {
        return INDEX_NONE;
    }
    if (State->FindItem(SelectedItemId))
    {
        return SelectedItemId;
    }

    const TArray<int32> DisplayIds = GetInventoryDisplayIds();

    return DisplayIds.IsEmpty() ? INDEX_NONE : DisplayIds[0];
}

/** UI에서 선택한 선택 아이템 동작을 처리한다. */
void AFablePlayerController::UISelectItem(int32 ItemId)
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (bInventoryOpen && State && State->FindItem(ItemId))
    {
        SelectedItemId = ItemId;
    }
}

/** UI에서 선택한 선택 이전 동작을 처리한다. */
void AFablePlayerController::UISelectPrevious()
{
    if (IsExpeditionResultOpen())
    {
        UISelectResultItem(GetSelectedResultItemIndex() + (-1));
        return;
    }

    // 검증 입력과 키 유지 상태 처리
    if (bWorkshopOpen)
    {
        if (WorkshopTab == EFableWorkshopTab::Craft)
        {
            UISelectCraftRecipe(FMath::Clamp(CraftRarity + (-1), 1, 4), CraftSlot);
            return;
        }
        const TArray<int32> Ids = GetWorkshopDisplayIds();
        if (!Ids.IsEmpty())
        {
            UISelectWorkshopItem(
                Ids[FMath::Clamp(Ids.IndexOfByKey(GetSelectedWorkshopItemId()) + (-1), 0, Ids.Num() - 1)]);
        }
        return;
    }

    // 직업·스킬·특성 빌드 처리
    if (bSkillsOpen)
    {
        if (SkillPanelTab == 0)
        {
            UISelectSkill(FMath::Max(0, SelectedSkillSlot - 1));
        }
        else
        {
            const int32 Row = FMath::Max(0, (SelectedTalentTier - 2) * 2 + int32(SelectedTalentPath) - 2);
            UISelectTalent(2 + Row / 2, static_cast<EFableTalentPath>(1 + Row % 2));
        }
        return;
    }

    // 퀘스트와 이야기 진행 조건 검사
    if (IsPortalOpen() || IsPartyExpeditionOpen())
    {
        UISelectDungeon(EFableRegion::RootRuins);
        return;
    }
    if (bQuestsOpen || Dialogue == EFableDialogue::Guide)
    {
        UISelectQuest(static_cast<EFableQuestId>(FMath::Max(0, int32(GetSelectedQuestId()) - 1)));
        return;
    }

    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!bInventoryOpen || !State || State->Inventory.IsEmpty())
    {
        return;
    }

    // 검증 입력과 키 유지 상태에 필요한 값 준비
    const TArray<int32> DisplayIds = GetInventoryDisplayIds();
    const int32 Current = DisplayIds.IndexOfByKey(GetSelectedItemId());
    SelectedItemId = DisplayIds[FMath::Max(0, Current - 1)];
}

/** UI에서 선택한 선택 다음 동작을 처리한다. */
void AFablePlayerController::UISelectNext()
{
    if (IsExpeditionResultOpen())
    {
        UISelectResultItem(GetSelectedResultItemIndex() + (1));
        return;
    }

    // 검증 입력과 키 유지 상태 처리
    if (bWorkshopOpen)
    {
        if (WorkshopTab == EFableWorkshopTab::Craft)
        {
            UISelectCraftRecipe(FMath::Clamp(CraftRarity + (1), 1, 4), CraftSlot);
            return;
        }
        const TArray<int32> Ids = GetWorkshopDisplayIds();
        if (!Ids.IsEmpty())
        {
            UISelectWorkshopItem(
                Ids[FMath::Clamp(Ids.IndexOfByKey(GetSelectedWorkshopItemId()) + (1), 0, Ids.Num() - 1)]);
        }
        return;
    }

    // 직업·스킬·특성 빌드 처리
    if (bSkillsOpen)
    {
        if (SkillPanelTab == 0)
        {
            UISelectSkill(FMath::Min(3, SelectedSkillSlot + 1));
        }
        else
        {
            const int32 Row = FMath::Min(3, (SelectedTalentTier - 2) * 2 + int32(SelectedTalentPath));
            UISelectTalent(2 + Row / 2, static_cast<EFableTalentPath>(1 + Row % 2));
        }
        return;
    }

    // 퀘스트와 이야기 진행 조건 검사
    if (IsPortalOpen() || IsPartyExpeditionOpen())
    {
        UISelectDungeon(EFableRegion::Moonwell);
        return;
    }
    if (bQuestsOpen || Dialogue == EFableDialogue::Guide)
    {
        UISelectQuest(
            static_cast<EFableQuestId>(FMath::Min(int32(EFableQuestId::Story), int32(GetSelectedQuestId()) + 1)));
        return;
    }

    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!bInventoryOpen || !State || State->Inventory.IsEmpty())
    {
        return;
    }

    // 검증 입력과 키 유지 상태에 필요한 값 준비
    const TArray<int32> DisplayIds = GetInventoryDisplayIds();
    const int32 Current = DisplayIds.IndexOfByKey(GetSelectedItemId());
    SelectedItemId = DisplayIds[FMath::Min(DisplayIds.Num() - 1, Current + 1)];
}

/** 마을 게시판 인접 여부를 확인한다. */
bool AFablePlayerController::IsNearTownBoard() const
{
    return NearestService(this) == EFableService::Board;
}

/** 공방 인접 여부를 확인한다. */
bool AFablePlayerController::IsNearForge() const
{
    return NearestService(this) == EFableService::Forge;
}

/** 상인 인접 여부를 확인한다. */
bool AFablePlayerController::IsNearMerchant() const
{
    return NearestService(this) == EFableService::Merchant;
}

/** 마을 서비스 인접 여부를 확인한다. */
bool AFablePlayerController::IsNearTownService() const
{
    return IsNearTownBoard() || IsNearForge();
}

/** 주변 이야기 오브젝트의 위치 번호 값을 반환한다. */
int32 AFablePlayerController::GetNearbyStoryObjectIndex() const
{
    const APawn* HeroPawn = GetPawn();
    const FFableAdventureView Adventure = FableAdventure::For(this);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!HeroPawn || !Adventure)
    {
        return INDEX_NONE;
    }

    float Best = FMath::Square(330.f);
    int32 Selected = INDEX_NONE;

    // 퀘스트와 이야기 진행 순회
    for (int32 Index = 0; Index < 5; ++Index)
    {
        const FVector Delta = HeroPawn->GetActorLocation() - Adventure.ToWorld(FableRPG::StoryObjectRegion(Index),
                                                                               FableRPG::StoryObjectLocation(Index));
        const float Distance = Delta.SizeSquared2D();
        if (Distance <= Best && FMath::Abs(Delta.Z) < 300.f)
        {
            Best = Distance;
            Selected = Index;
        }
    }

    return Selected;
}

/** 현재 목표의 안내 위치 값을 반환한다. */
bool AFablePlayerController::GetNavigationTarget(FVector& OutLocation, FString& OutLabel) const
{
    // 화면 표시와 사용자 안내 처리
    OutLabel.Empty();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const FFableAdventureView Adventure = FableAdventure::For(this);

    // 화면 표시와 사용자 안내 조건 검사
    if (!Hero || !State || !Adventure)
    {
        return false;
    }
    if (Adventure && Adventure->bPartyRecoveryPending)
    {
        OutLabel = TEXT("마을 구조를 기다리는 중");
        return false;
    }
    if (!Hero->IsAlive())
    {
        OutLabel = TEXT("동료의 구조를 기다리는 중");
        return false;
    }

    if (ResultPortalGuidanceExpeditionId.IsValid() && Adventure.ExpeditionId() == ResultPortalGuidanceExpeditionId &&
        Adventure->AdventureStage == EFableAdventureStage::Cleared &&
        Adventure.IsInRegion(Adventure->ActiveRegion, Hero->GetActorLocation()))
    {
        OutLocation = Adventure.ToWorld(Adventure->ActiveRegion, FableRPG::RegionReturnPortal(Adventure->ActiveRegion));
        OutLabel = TEXT("마을 귀환 포털");
        return true;
    }

    // 생존 상태와 부활 진행에 필요한 값 준비
    const EFableRegion Region = PawnRegion(this);
    float Nearest = TNumericLimits<float>::Max();
    const AFableCharacter* DownedAlly = nullptr;
    const AFablePartyManager* PartyManager = AFablePartyManager::Find(GetWorld());

    // 생존 상태와 부활 진행 처리
    if (Adventure)
    {
        for (APlayerState* Member : Adventure.Players())
        {
            if (const AFableCharacter* Ally = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr)
            {
                if (Ally != Hero && Ally->IsDowned() && FableAdventure::SameScope(this, Ally) &&
                    Adventure.IsInRegion(Region, Ally->GetActorLocation()) && PartyManager &&
                    PartyManager->ArePartyMembers(State, Cast<AFablePlayerState>(Member)))
                {
                    const float Distance = FVector::DistSquared2D(Hero->GetActorLocation(), Ally->GetActorLocation());
                    if (Distance < Nearest)
                    {
                        Nearest = Distance;
                        DownedAlly = Ally;
                    }
                }
            }
        }
    }

    // 생존 상태와 부활 진행 조건 검사
    if (DownedAlly)
    {
        OutLocation = DownedAlly->GetActorLocation();
        OutLabel = TEXT("쓰러진 동료 구조");
        return true;
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    const bool bCombat = Adventure && ((Region != EFableRegion::Village && Adventure->ActiveRegion == Region &&
                                        (Adventure->AdventureStage == EFableAdventureStage::Dungeon ||
                                         Adventure->AdventureStage == EFableAdventureStage::Boss)) ||
                                       (Region == EFableRegion::Village && State->StoryChapter == 2 &&
                                        State->ForestQuest == EFableQuestStage::Active));

    // 지역·적·전투 진행 처리
    if (bCombat)
    {
        // 지역·적·전투 진행 처리
        Nearest = TNumericLimits<float>::Max();
        const AFableEnemy* Target = nullptr;

        // 지역·적·전투 진행 순회
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            if (It->IsAlive() && FableAdventure::SameScope(this, *It) &&
                Adventure.IsInRegion(Region, It->GetActorLocation()))
            {
                const float Distance = FVector::DistSquared2D(Hero->GetActorLocation(), It->GetActorLocation());
                if (Distance < Nearest)
                {
                    Nearest = Distance;
                    Target = *It;
                }
            }
        }

        // 지역·적·전투 진행 조건 검사
        if (Target)
        {
            OutLocation = Target->GetActorLocation();
            OutLabel = TEXT("현재 전투의 적");
            return true;
        }
        if (Region == EFableRegion::Moonwell && Adventure->ActiveSealIndex >= 0 && Adventure->ActiveSealIndex < 3 &&
            Adventure->AdventureStage == EFableAdventureStage::Dungeon)
        {
            OutLocation =
                Adventure.ToWorld(EFableRegion::Moonwell, FableRPG::MoonSealLocation(Adventure->ActiveSealIndex));
            OutLabel = FString::Printf(TEXT("봉인 %d · E 정화"), Adventure->ActiveSealIndex + 1);
            return true;
        }
    }

    // 화면 표시와 사용자 안내 조건 검사
    EFableRegion TargetRegion = EFableRegion::Village;
    if (State->GetStoryTarget(OutLocation, OutLabel, &TargetRegion))
    {
        // 안내 함수의 공개 계약은 실제 월드 좌표로 통일한다.
        OutLocation = Adventure.ToWorld(TargetRegion, OutLocation);
        return true;
    }

    OutLabel.Empty();

    return false;
}

/** 현재 상호작용 대상의 안내 문구 값을 반환한다. */
FString AFablePlayerController::GetContextActionText() const
{
    if (IsExpeditionResultOpen())
    {
        return TEXT("↑↓ 지급 장비 · E 결과 확인 · R 재도전 준비 · Tab 탭 전환 · Esc 닫기");
    }
    if (bPartyOpen)
    {
        return IsPartyExpeditionOpen() ? TEXT("E 준비 / 철수 동의 · R 출발 · Tab 탭 전환 · Esc 닫기")
                                       : TEXT("버튼으로 파티 구성 · Tab 탭 전환 · G / Esc 닫기");
    }

    // 직업·스킬·특성 빌드 조건 검사
    if (bWorkshopOpen)
    {
        return TEXT("←→ 공방 탭 · ↑↓ 선택 · E 작업 · Esc 닫기");
    }
    if (bSkillsOpen)
    {
        return IsBuildResetConfirmationPending() ? TEXT("E 현재 직업 초기화 확정 · Esc 취소")
               : SkillPanelTab == 0              ? TEXT("←→ 탭 · ↑↓ 스킬 · E 강화 · F 초기화 · P / Esc 닫기")
                                                 : TEXT("←→ 탭 · ↑↓ 특성 · E 선택 · F 초기화 · P / Esc 닫기");
    }
    if (bInventoryOpen)
    {
        return TEXT("↑↓ 선택 · Tab 정렬 · E 장착 · U 강화 · X 판매 · I / Esc 닫기");
    }
    if (bQuestsOpen)
    {
        return TEXT("↑↓ 의뢰 선택 · 수락과 보상은 로완에게 · Q / Esc 닫기");
    }
    if (IsPortalOpen())
    {
        return TEXT("↑↓ 지역 · ←→ 난이도 · E / R 입장 · Esc 닫기");
    }
    if (Dialogue == EFableDialogue::Guide)
    {
        return TEXT("↑↓ 의뢰 선택 · E 수락 / 보고 · Esc 닫기");
    }
    if (Dialogue == EFableDialogue::Forge)
    {
        return TEXT("E 가방 / 강화 · F 공방 / 창고 · Esc 닫기");
    }
    if (IsDialogueOpen())
    {
        return TEXT("버튼을 눌러 대화 선택 · Esc 닫기");
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const FFableAdventureView Adventure = FableAdventure::For(this);
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());

    if (!Adventure)
    {
        return TEXT("원정 정보를 불러오는 중입니다.");
    }

    // 생존 상태와 부활 진행 조건 검사
    if (Adventure && Adventure->bPartyRecoveryPending)
    {
        return GetRetryActionText();
    }
    if (Hero && Hero->IsDowned())
    {
        return TEXT("쓰러졌습니다 · 동료가 가까이에서 E를 3초 누르면 구조합니다");
    }
    if (Hero && Hero->IsDefeated())
    {
        return TEXT("전투 불능 · 잠시 후 마을로 구조됩니다");
    }
    if (Hero && Hero->GetReviveTarget())
    {
        return TEXT("E를 계속 누르세요 · 이동 / 공격 / 피격 시 구조가 취소됩니다");
    }
    if (!IsAnyPanelOpen() && GetNearbyReviveTarget())
    {
        return TEXT("E를 3초 동안 누르기 · 쓰러진 동료 구조");
    }
    if (Adventure && Adventure->bRetryAvailable)
    {
        return GetRetryActionText();
    }

    const AFablePlayerState* StoryState = GetPlayerState<AFablePlayerState>();

    // 퀘스트와 이야기 진행 처리
    if (NearestService(this) == EFableService::StoryObject)
    {
        const int32 Index = GetNearbyStoryObjectIndex();
        if (Index >= 0 && StoryState)
        {
            return StoryState->CanUseStoryObject(Index)
                       ? TEXT("E ") + FableRPG::StoryObjectName(Index) + (Index < 3 ? TEXT(" 수집") : TEXT(" 조사"))
                       : FableRPG::StoryObjectName(Index) + TEXT(" · Q 메인 이야기에서 다음 목표 확인");
        }
    }

    const EFableRegion Region = PawnRegion(this);

    // 퀘스트와 이야기 진행 처리
    if (Region == EFableRegion::Moonwell && StoryState && StoryState->StoryChapter == 4 && Adventure)
    {
        const int32 Seal = NearbyMoonSeal(this);
        if (Seal >= 0 && (Adventure->MoonSealMask & (1 << Seal)) != 0 && (StoryState->StorySealMask & (1 << Seal)) == 0)
        {
            return FString::Printf(TEXT("E 정화된 봉인 %d 조사 · 내 이야기 진행에 기록"), Seal + 1);
        }
    }

    // 생존 상태와 부활 진행 처리
    if (Region != EFableRegion::Village)
    {
        if (Adventure && Adventure->AdventureStage == EFableAdventureStage::Cleared)
        {
            return NearestService(this) == EFableService::ReturnPortal ? TEXT("E 마을로 귀환 · 로완에게 의뢰 보상 받기")
                                                                       : TEXT("귀환 포털로 이동해 E를 누르세요");
        }
        if (Region == EFableRegion::Moonwell && Adventure && Adventure->ActiveSealIndex >= 0 &&
            Adventure->ActiveSealIndex < 3 && Adventure->AdventureStage == EFableAdventureStage::Dungeon)
        {
            const bool bReady =
                Adventure->RequiredEnemies > 0 && Adventure->DefeatedEnemies >= Adventure->RequiredEnemies;
            return bReady
                       ? FString::Printf(TEXT("봉인 %d에 다가가 E로 정화하세요"), Adventure->ActiveSealIndex + 1)
                       : FString::Printf(TEXT("달빛 수원 · 봉인 %d의 적 %d / %d 처치"), Adventure->ActiveSealIndex + 1,
                                         Adventure->DefeatedEnemies, Adventure->RequiredEnemies);
        }
        return FableRPG::RegionName(Region) + TEXT("의 적을 처치하세요 · 주황색 공격 범위는 회피!");
    }

    // 이 단계에서 현재 상호작용 대상의 안내 문구 값을 반환한다.
    switch (NearestService(this))
    {
    case EFableService::Board:
        return TEXT("E 로완과 의뢰 선택 · 1 전사 / 2 마법사 / 3 궁수");
    case EFableService::Forge:
        return TEXT("E 대장장이 브람 · 강화 / 제작 / 수리 / 창고");
    case EFableService::Merchant:
        return TEXT("E 상인 미라와 대화 · 물약과 장비 거래");
    case EFableService::Portal:
        return TEXT("E / R 원정 지역 선택 · 모든 동료가 해당 의뢰를 받아야 합니다");
    default:
        break;
    }

    // 퀘스트와 이야기 진행 처리
    if (const AFablePlayerState* State = GetPlayerState<AFablePlayerState>())
    {
        // 퀘스트와 이야기 진행 조건 검사
        if (State->StoryChapter < 5)
        {
            return TEXT("메인 · ") + State->GetStoryObjective() + TEXT(" · Q 자세히");
        }

        // 퀘스트와 이야기 진행 순회
        for (int32 Index = 0; Index <= int32(EFableQuestId::Story); ++Index)
        {
            if (State->GetQuestStage(static_cast<EFableQuestId>(Index)) == EFableQuestStage::Ready)
            {
                return TEXT("마을의 로완에게 돌아가 의뢰 보상 또는 전직을 보고하세요");
            }
        }

        // 퀘스트와 이야기 진행 조건 검사
        if (State->ForestQuest == EFableQuestStage::Active)
        {
            return TEXT("마을 북쪽 숲의 수호자 3마리를 처치하세요");
        }
        if (State->MoonwellQuest == EFableQuestStage::Active)
        {
            return TEXT("동쪽 포털에서 달빛 수원을 선택해 입장하세요");
        }
        if (State->DungeonQuest == EFableQuestStage::Active)
        {
            return TEXT("동쪽 포털에서 뿌리 유적을 선택해 입장하세요");
        }
        if (State->GetQuestStage(EFableQuestId::SecondAdvancement) == EFableQuestStage::Active ||
            State->GetQuestStage(EFableQuestId::ThirdAdvancement) == EFableQuestStage::Active)
        {
            return TEXT("전직 시험 진행 중 · 실제 적에게 스킬 적중 · Q에서 조건 확인");
        }
    }

    return TEXT("로완에게 다가가 E로 모험 또는 전직 의뢰를 선택하세요");
}

/** UI에서 선택한 주변 대상 상호작용 동작을 처리한다. */
void AFablePlayerController::UIInteract()
{
    // 파티 화면의 E 입력은 현재 탭에서만 처리하고 뒤의 주민·포털로 보내지 않는다.
    if (bPartyOpen)
    {
        if (IsExpeditionResultOpen())
        {
            const auto* Result = GetDisplayedExpeditionResult();
            if (Result && Result->IsTerminal() && !Result->bAcknowledged)
            {
                UIAcknowledgeExpeditionResult();
            }
            else
            {
                UIClosePanels();
            }
            return;
        }
        if (IsPartyExpeditionOpen())
        {
            const FFablePartyRecord* Party = FindControllerParty(this);
            const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
            if (Profile && Profile->ActiveExpeditionId.IsValid() && !IsPartyRetryPreparation())
            {
                if (CanSetAbandonConsent())
                {
                    UISetAbandonConsent(!HasAbandonConsent());
                }
                else
                {
                    UIEnterSelectedDungeon();
                }
            }
            else if (Party && Profile && Party->LeaderKey == Profile->PartyPlayerKey && IsPartyReady())
            {
                UIEnterPreparedParty();
            }
            else
            {
                UISetPartyReady(!IsPartyReady());
            }
        }
        return;
    }

    // 퀘스트와 이야기 진행 조건 검사
    if (bFrontEndOpen || bHelpOpen || bQuestsOpen)
    {
        return;
    }
    if (bWorkshopOpen)
    {
        UIWorkshopAction();
        return;
    }

    // 직업·스킬·특성 빌드 처리
    if (bSkillsOpen)
    {
        if (IsBuildResetConfirmationPending())
        {
            UIConfirmBuildReset();
        }
        else if (SkillPanelTab == 1)
        {
            UIChooseTalent();
        }
        else
        {
            UIUpgradeSkill(SelectedSkillSlot);
        }
        return;
    }

    // 퀘스트와 이야기 진행 조건 검사
    if (bInventoryOpen)
    {
        UIEquip(GetSelectedItemId());
        return;
    }
    if (IsPortalOpen())
    {
        UIEnterSelectedDungeon();
        return;
    }
    if (Dialogue == EFableDialogue::Guide)
    {
        UIConfirmQuest();
        return;
    }
    if (Dialogue == EFableDialogue::Forge)
    {
        UIToggleInventory();
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (!IsDialogueOpen())
    {
        if (!bFreeCursor)
        {
            if (AFableCharacter* Target = GetNearbyReviveTarget())
            {
                UIStartRevive(Target);
                return;
            }
        }
        ServerInteract();
    }
}

/** UI에서 선택한 동료 부활 시작 동작을 처리한다. */
void AFablePlayerController::UIStartRevive(AFableCharacter* Target)
{
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEndOpen || IsAnyPanelOpen() || bFreeCursor || !Hero || !Hero->IsAlive())
    {
        return;
    }
    // The target is intentionally passed to the authoritative character for all validation.

    // 생존 상태와 부활 진행 처리
    bReviveInputHeld = true;
    ServerStartRevive(Target);
}

/** UI에서 선택한 동료 부활 중지 동작을 처리한다. */
void AFablePlayerController::UIStopRevive()
{
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());

    // 생존 상태와 부활 진행 조건 검사
    if (!bReviveInputHeld && (!Hero || !Hero->GetReviveTarget()))
    {
        return;
    }

    // 생존 상태와 부활 진행 처리
    bReviveInputHeld = false;
    ServerCancelRevive();
}

/** 가까이 있는 부활 가능한 동료 값을 반환한다. */
AFableCharacter* AFablePlayerController::GetNearbyReviveTarget() const
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const FFableAdventureView Adventure = FableAdventure::For(this);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !Hero->IsAlive() || !Adventure || Adventure->bPartyRecoveryPending)
    {
        return nullptr;
    }

    AFableCharacter* Nearest = nullptr;
    float Best = FMath::Square(230.f);

    // 생존 상태와 부활 진행 순회
    for (APlayerState* Member : Adventure.Players())
    {
        AFableCharacter* Candidate = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;
        const AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
        if (!Candidate || Candidate == Hero || !Candidate->IsDowned() || !FableAdventure::SameScope(this, Candidate) ||
            !Manager ||
            !Manager->ArePartyMembers(GetPlayerState<AFablePlayerState>(), Cast<AFablePlayerState>(Member)) ||
            (Candidate->GetReviver() && Candidate->GetReviver() != Hero))
        {
            continue;
        }
        const FVector Delta = Candidate->GetActorLocation() - Hero->GetActorLocation();
        if (Delta.SizeSquared2D() <= Best && FMath::Abs(Delta.Z) <= 150.f)
        {
            Best = Delta.SizeSquared2D();
            Nearest = Candidate;
        }
    }

    return Nearest;
}

/** UI에서 선택한 전투 재도전 동작을 처리한다. */
void AFablePlayerController::UIRetryEncounter()
{
    const auto* RetryResult = GetDisplayedExpeditionResult();
    if (RetryResult && RetryResult->IsTerminal() && !RetryResult->RetryExpeditionId.IsValid())
    {
        UIBeginExpeditionRetry();
        return;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bFrontEndOpen)
    {
        return;
    }

    // 던전 재도전도 선택한 원정의 전원 준비 절차를 거치도록 안내한다.
    const FFableAdventureView Adventure = FableAdventure::For(this);
    if (!Adventure)
    {
        return;
    }
    if (Adventure && Adventure->RetryRegion != EFableRegion::Village)
    {
        UIOpenPartyExpedition();
        PartyPreparationMessage =
            TEXT("재도전할 원정과 난이도를 확인한 뒤 각자 준비하세요. 파티장이 출발할 수 있습니다.");
        bPartyPreparationSucceeded = false;
        if (CanEnterPreparedParty())
        {
            UIEnterPreparedParty();
        }
        return;
    }

    // 생존 상태와 부활 진행 처리
    UIStopRevive();
    ServerRetryEncounter();
}

/** 전투 재도전에 필요한 조건을 만족하는지 확인한다. */
bool AFablePlayerController::CanRetryEncounter() const
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const FFableAdventureView Adventure = FableAdventure::For(this);
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 프로필과 저장 스냅숏 조건 검사
    if (!Adventure || !Adventure->bRetryAvailable || Adventure->bPartyRecoveryPending || !Hero || !Hero->IsAlive() ||
        !State || !State->bProfileLoaded)
    {
        return false;
    }

    const bool bForest = Adventure->RetryRegion == EFableRegion::Village;

    // 퀘스트와 이야기 진행 조건 검사
    if (bForest && (!IsNearTownBoard() || State->ForestQuest != EFableQuestStage::Active))
    {
        return false;
    }
    if (!bForest && NearestService(this) != EFableService::Portal)
    {
        return false;
    }
    if (Adventure.Players().IsEmpty())
    {
        return false;
    }

    // 지역·적·전투 진행 순회
    for (APlayerState* Member : Adventure.Players())
    {
        const AFablePlayerState* Profile = Cast<AFablePlayerState>(Member);
        const AFableCharacter* MemberHero = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;
        if (!Profile || !Profile->bProfileLoaded || !MemberHero || !MemberHero->IsAlive() ||
            (!bForest && !Profile->IsRegionEligible(Adventure->RetryRegion)))
        {
            return false;
        }
    }

    return true;
}

/** 재도전 동작의 안내 문구 값을 반환한다. */
FString AFablePlayerController::GetRetryActionText() const
{
    const FFableAdventureView Adventure = FableAdventure::For(this);

    // 서버 시각과 재사용 대기시간 조건 검사
    if (!Adventure)
    {
        return FString();
    }
    if (Adventure->bPartyRecoveryPending)
    {
        return FString::Printf(TEXT("파티 구조 중 · 마을 복귀까지 %.0f초"), Adventure.RecoveryRemaining());
    }
    if (!Adventure->bRetryAvailable)
    {
        return FString();
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bForest = Adventure->RetryRegion == EFableRegion::Village;
    const FString Region = bForest ? TEXT("숲 순찰")
                                   : FableRPG::RegionName(Adventure->RetryRegion) + TEXT(" · ") +
                                         FableRPG::DifficultyName(Adventure->RetryDifficulty);

    // 지역·적·전투 진행 조건 검사
    if (CanRetryEncounter())
    {
        return TEXT("R ") + Region + TEXT(" 재도전 · 획득한 성장과 장비 유지");
    }

    // 지역·적·전투 진행 순회
    for (APlayerState* Member : Adventure.Players())
    {
        const AFablePlayerState* Profile = Cast<AFablePlayerState>(Member);
        const AFableCharacter* MemberHero = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;
        if (!Profile || !Profile->bProfileLoaded || !MemberHero || !MemberHero->IsAlive())
        {
            return Region + TEXT(" 재도전 · 모든 동료의 접속과 회복을 기다리세요");
        }
    }

    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 퀘스트와 이야기 진행 조건 검사
    if (bForest && State && State->ForestQuest != EFableQuestStage::Active)
    {
        return TEXT("숲 순찰 재도전 · 진행 중인 의뢰를 가진 동료가 로완에게 R");
    }
    if (!bForest && State && !State->IsRegionEligible(Adventure->RetryRegion))
    {
        return Region + TEXT(" 재도전 · 먼저 로완에게 해당 의뢰를 준비하세요");
    }

    return bForest ? TEXT("전멸 후 재정비 · 로완 근처에서 R로 숲 순찰 재도전")
                   : TEXT("전멸 후 재정비 · 모두 의뢰 준비 후 마을 포털에서 R로 ") + Region + TEXT(" 재도전");
}

/** 선택한 퀘스트 식별자 값을 반환한다. */
EFableQuestId AFablePlayerController::GetSelectedQuestId() const
{
    if (bHasExplicitQuestSelection)
    {
        return SelectedQuestId;
    }
    return EFableQuestId::Story;
}

/** UI에서 선택한 선택 퀘스트 동작을 처리한다. */
void AFablePlayerController::UISelectQuest(EFableQuestId QuestId)
{
    if (uint8(QuestId) > uint8(EFableQuestId::Story))
    {
        return;
    }
    SelectedQuestId = QuestId;
    bHasExplicitQuestSelection = true;
}

/** UI에서 선택한 퀘스트 확인 동작을 처리한다. */
void AFablePlayerController::UIConfirmQuest()
{
    if (bFrontEndOpen)
    {
        return;
    }
    ServerConfirmQuest(GetSelectedQuestId());
    UIClosePanels();
}

/** UI에서 선택한 선택 던전 동작을 처리한다. */
void AFablePlayerController::UISelectDungeon(EFableRegion Region)
{
    if (Region == EFableRegion::RootRuins || Region == EFableRegion::Moonwell)
    {
        const FFablePartyRecord* Party = FindControllerParty(this);
        if (Party)
        {
            UIRequestPartySelection(Party->PartyId, Party->PreparationRevision, Region, Party->SelectedDifficulty);
        }
        else
        {
            SelectedDungeonId = Region;
        }
    }
}

/** UI에서 선택한 선택 난이도 동작을 처리한다. */
void AFablePlayerController::UISelectDifficulty(EFableDifficulty Difficulty)
{
    if (bFrontEndOpen)
    {
        return;
    }
    if (Difficulty == EFableDifficulty::Normal || Difficulty == EFableDifficulty::Hard)
    {
        const FFablePartyRecord* Party = FindControllerParty(this);
        if (Party)
        {
            UIRequestPartySelection(Party->PartyId, Party->PreparationRevision, Party->SelectedRegion, Difficulty);
        }
        else
        {
            SelectedDifficulty = Difficulty;
        }
    }
}

/** 선택한 던전 난이도 값을 반환한다. */
EFableDifficulty AFablePlayerController::GetSelectedDifficulty() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    return Party ? Party->SelectedDifficulty : SelectedDifficulty;
}

/** 선택한 던전 식별자 값을 반환한다. */
EFableRegion AFablePlayerController::GetSelectedDungeonId() const
{
    const FFablePartyRecord* Party = FindControllerParty(this);
    return Party ? Party->SelectedRegion : SelectedDungeonId;
}

/** 난이도 선택 잠금 여부를 확인한다. */
bool AFablePlayerController::IsDifficultySelectionLocked() const
{
    return !CanSelectPartyExpedition();
}

/** UI에서 선택한 선택 던전 입장 동작을 처리한다. */
void AFablePlayerController::UIEnterSelectedDungeon()
{
    if (bFrontEndOpen)
    {
        return;
    }
    if (!IsPartyExpeditionOpen())
    {
        UIOpenPartyExpedition();
    }

    if (IsPartyRetryPreparation())
    {
        UIEnterPreparedParty();
        return;
    }

    // 진행 중인 원정의 참가자는 새 출발 대신 기존 서버 증서로만 복귀한다.
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const FFablePartyRecord* Party = FindControllerParty(this);
    if (Profile && Profile->ActiveExpeditionId.IsValid() && Party)
    {
        if (IsPartyPreparationPending() || bOnlineLeaveInputBlocked)
        {
            return;
        }

        PendingPartyRequestId = FGuid::NewGuid();
        bPendingPartyEntry = true;
        bPartyPreparationSucceeded = false;
        PartyPreparationMessage = TEXT("서버에서 현재 원정의 참가 증서와 복귀를 확인하고 있습니다.");
        ServerResumePartyExpedition(PendingPartyRequestId, Party->PartyId, Party->PreparationRevision);
        return;
    }

    UIEnterPreparedParty();
}

/** 던전 입장에 필요한 조건을 만족하는지 확인한다. */
bool AFablePlayerController::CanEnterDungeon(EFableRegion Region) const
{
    // 새 입장에는 파티장의 선택과 전원 준비가 필요하다.
    if (Region != GetSelectedDungeonId())
    {
        return false;
    }

    if (IsPartyRetryPreparation())
    {
        return CanEnterPreparedParty();
    }

    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());
    if (Profile && Profile->ActiveExpeditionId.IsValid())
    {
        return Hero && Hero->IsAlive() && PawnRegion(this) == EFableRegion::Village &&
               NearestService(this) == EFableService::Portal && !IsPartyPreparationPending();
    }

    return CanEnterPreparedParty();
}

/** 던전 입장 준비 상태 안내 값을 반환한다. */
FString AFablePlayerController::GetDungeonReadinessText(EFableRegion Region) const
{
    return Region == GetSelectedDungeonId() ? GetPartyPreparationStatus()
                                            : TEXT("파티장이 서버에 선택한 원정 지역을 확인하세요.");
}

/** UI에서 선택한 직업 선택 동작을 처리한다. */
void AFablePlayerController::UIChooseClass(EFableClass HeroClass)
{
    if (!bFrontEndOpen)
    {
        ServerChooseClass(HeroClass);
    }
}

/** UI에서 선택한 장비 착용 동작을 처리한다. */
void AFablePlayerController::UIEquip(int32 Id)
{
    if (!bFrontEndOpen)
    {
        ServerEquip(Id);
    }
}

/** UI에서 선택한 장비 강화 동작을 처리한다. */
void AFablePlayerController::UIEnhance(int32 Id)
{
    if (!bFrontEndOpen)
    {
        ServerEnhance(Id);
    }
}

/** UI에서 선택한 상점 구매 동작을 처리한다. */
void AFablePlayerController::UIShopBuy(EFableShopOffer Offer)
{
    if (!bFrontEndOpen)
    {
        ServerShopBuy(Offer);
    }
}

/** UI에서 선택한 아이템 판매 동작을 처리한다. */
void AFablePlayerController::UISell(int32 Id)
{
    if (!bFrontEndOpen)
    {
        ServerSell(Id);
    }
}

/** UI에서 선택한 체력 물약 사용 동작을 처리한다. */
void AFablePlayerController::UIUseHealthPotion()
{
    if (!bFrontEndOpen)
    {
        ServerUsePotion(false);
    }
}

/** UI에서 선택한 마나 물약 사용 동작을 처리한다. */
void AFablePlayerController::UIUseManaPotion()
{
    if (!bFrontEndOpen)
    {
        ServerUsePotion(true);
    }
}

/** 전사 선택을 적용한다. */
void AFablePlayerController::ChooseWarrior()
{
    if (bFrontEndOpen)
    {
        UISetInitialClass(EFableClass::Warrior);
    }
    else if (!IsAnyPanelOpen() || Dialogue == EFableDialogue::Guide)
    {
        UIChooseClass(EFableClass::Warrior);
    }
}

/** 마법사 선택을 적용한다. */
void AFablePlayerController::ChooseMage()
{
    if (bFrontEndOpen)
    {
        UISetInitialClass(EFableClass::Mage);
    }
    else if (!IsAnyPanelOpen() || Dialogue == EFableDialogue::Guide)
    {
        UIChooseClass(EFableClass::Mage);
    }
}

/** 궁수 선택을 적용한다. */
void AFablePlayerController::ChooseArcher()
{
    if (bFrontEndOpen)
    {
        UISetInitialClass(EFableClass::Archer);
    }
    else if (!IsAnyPanelOpen() || Dialogue == EFableDialogue::Guide)
    {
        UIChooseClass(EFableClass::Archer);
    }
}

/** 현재 선택한 장비의 강화를 요청한다. */
void AFablePlayerController::EnhanceSelected()
{
    if (bInventoryOpen)
    {
        UIEnhance(GetSelectedItemId());
    }
}

/** 현재 선택한 아이템의 판매를 요청한다. */
void AFablePlayerController::SellSelected()
{
    if (bInventoryOpen)
    {
        UISell(GetSelectedItemId());
    }
}

/** 서버 측 RPG 요청의 권한·상태·요청 간격을 확인한다. */
bool AFablePlayerController::AcceptRPGRequest()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld())
    {
        return false;
    }

    const AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Mode && Mode->IsFrontEnd())
    {
        return false;
    }

    const float Now = GetWorld()->GetTimeSeconds();

    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!FMath::IsFinite(Now) || Now - LastRPGRequestTime < 0.12f)
    {
        return false;
    }

    // HTTP 요청·응답과 연결 상태 처리
    LastRPGRequestTime = Now;
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn());

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return false;
    }
    if (State->IsBackendEnabled() && (!State->IsBackendReady() || State->IsBackendSavePending()))
    {
        State->SetNotice(TEXT("서버 저장을 확인하고 있습니다. 잠시 뒤 다시 시도하세요."));
        return false;
    }
    if (!Hero || !Hero->IsAlive())
    {
        State->SetNotice(TEXT("회복한 뒤 이용할 수 있습니다."));
        return false;
    }

    return true;
}

/** 동료 부활 시작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerStartRevive_Implementation(AFableCharacter* Target)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld())
    {
        return;
    }
    if (const auto Profile = GetPlayerState<AFablePlayerState>();
        Profile && Profile->IsBackendEnabled() && !Profile->IsBackendReady())
    {
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();
    const FFableAdventureView Adventure = FableAdventure::For(this);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    const AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const AFablePlayerState* TargetProfile = Target ? Target->GetPlayerState<AFablePlayerState>() : nullptr;
    if (!Mode || Mode->IsFrontEnd() || !Adventure || Adventure->bPartyRecoveryPending || !Target ||
        !FableAdventure::SameScope(this, Target) || !Manager ||
        !Manager->ArePartyMembers(GetPlayerState<AFablePlayerState>(), TargetProfile))
    {
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn()))
    {
        Hero->BeginRevive(Target);
    }
}

/** 동료 부활 취소 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerCancelRevive_Implementation()
{
    if (HasAuthority())
    {
        if (AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn()))
        {
            if (Hero->GetReviveTarget())
            {
                Hero->CancelRevive();
            }
        }
    }
}

/** 전투 재도전 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerRetryEncounter_Implementation()
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!AcceptRPGRequest())
    {
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (AFableCharacter* Hero = Cast<AFableCharacter>(GetPawn()))
    {
        Hero->CancelRevive();
    }

    // 지역·적·전투 진행 처리
    if (AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>())
    {
        Mode->RequestRetryEncounter(this);
    }
}

/** 주변 대상 상호작용 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerInteract_Implementation()
{
    FFableScopedServiceValidationObservation Observation(this, TEXT("interact"), EFableTownServiceId::Invalid);
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!AcceptRPGRequest())
    {
        return;
    }

    Observation.AcceptRpgGate();
    AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();

    // 기존 E 대상 선택은 유지하고 마을 대화만 서버 정본·현 소유자·지지면을 통과시킨다. / 村会話に現所有者と支持床の検査を適用する。
    const auto OpenTownDialogue =
        [this, Mode, &Observation](EFableTownServiceId ServiceId, EFableDialogue TargetDialogue)
    {
        Observation.SetService(ServiceId);
        const AFableTownManager* Towns = AFableTownManager::Find(GetWorld());
        FFableTownLocationContext Physical;
        FFableTownServiceDescriptor Service;
        FFableTownServiceRef Request;
        if (Towns && Towns->ResolvePhysicalContext(this, Physical))
        {
            Request.TownId = Physical.TownId;
            Request.ServiceId = ServiceId;
            Request.CatalogVersion = Physical.CatalogVersion;
        }

        if (Mode && Mode->CanUseTownService(this, Request, Service))
        {
            ClientOpenDialogue(static_cast<uint8>(TargetDialogue));
        }
        else if (AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>())
        {
            Profile->SetNotice(TEXT("현재 마을의 주민이나 포털에 조금 더 가까이 다가가세요."));
        }
    };

    // 퀘스트와 이야기 진행 처리
    switch (NearestService(this))
    {
    // HTTP 요청·응답과 연결 상태 처리
    case EFableService::Board:
        OpenTownDialogue(EFableTownServiceId::QuestGuide, EFableDialogue::Guide);
        break;
    case EFableService::Forge:
        OpenTownDialogue(EFableTownServiceId::Workshop, EFableDialogue::Forge);
        break;
    case EFableService::Merchant:
        OpenTownDialogue(EFableTownServiceId::Merchant, EFableDialogue::Shop);
        break;
    case EFableService::Portal:
        OpenTownDialogue(EFableTownServiceId::ExpeditionPortal, EFableDialogue::Portal);
        break;
    case EFableService::MoonSeal:
        if (Mode)
        {
            Mode->RequestCleanseSeal(this, NearbyMoonSeal(this));
        }
        break;
    case EFableService::StoryObject:
        GetPlayerState<AFablePlayerState>()->RecordStoryObject(GetNearbyStoryObjectIndex());
        break;
    case EFableService::ReturnPortal:
        if (Mode)
        {
            Mode->RequestReturnVillage(this);
        }
        break;
    default:
        GetPlayerState<AFablePlayerState>()->SetNotice(TEXT("주민이나 포털에 조금 더 가까이 다가가세요."));
        break;
    }
}

/** 서버가 전달한 열림 대화 결과를 소유 클라이언트에 반영한다. */
void AFablePlayerController::ClientOpenDialogue_Implementation(uint8 Service)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Service < 1 || Service > static_cast<uint8>(EFableDialogue::Portal))
    {
        return;
    }

    // 마을 포털과 G 키는 같은 원정 준비 화면을 사용한다.
    if (Service == static_cast<uint8>(EFableDialogue::Portal))
    {
        UIOpenPartyExpedition();
        return;
    }

    UIClosePanels();
    Dialogue = static_cast<EFableDialogue>(Service);
    ApplyUIMode();
}

/** 퀘스트 확인 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerConfirmQuest_Implementation(EFableQuestId QuestId)
{
    FFableScopedServiceValidationObservation Observation(this, TEXT("quest"), EFableTownServiceId::QuestGuide);
    if (!AcceptRPGRequest() || uint8(QuestId) > uint8(EFableQuestId::Story))
    {
        return;
    }
    Observation.AcceptRpgGate();
    GetPlayerState<AFablePlayerState>()->InteractQuest(QuestId);
}

/** 지역 입장 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerEnterRegion_Implementation(EFableRegion Region, EFableDifficulty Difficulty)
{
    // 지역·적·전투 진행 조건 검사
    if (!AcceptRPGRequest() || (Region != EFableRegion::RootRuins && Region != EFableRegion::Moonwell) ||
        (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard))
    {
        return;
    }
    if (NearestService(this) != EFableService::Portal)
    {
        GetPlayerState<AFablePlayerState>()->SetNotice(TEXT("마을의 원정 포털 가까이에서 입장하세요."));
        return;
    }

    // 지역·적·전투 진행 처리
    if (AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>())
    {
        Mode->RequestEnterRegion(this, Region, Difficulty);
    }
}

/** 직업 선택 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerChooseClass_Implementation(EFableClass HeroClass)
{
    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!AcceptRPGRequest())
    {
        return;
    }
    if (HeroClass != EFableClass::Warrior && HeroClass != EFableClass::Mage && HeroClass != EFableClass::Archer)
    {
        return;
    }

    GetPlayerState<AFablePlayerState>()->SelectClass(HeroClass);
}

/** 장비 착용 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerEquip_Implementation(int32 Id)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->EquipItem(Id);
    }
}

/** 장비 강화 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerEnhance_Implementation(int32 Id)
{
    FFableScopedServiceValidationObservation Observation(this, TEXT("enhance"), EFableTownServiceId::Workshop);
    if (AcceptRPGRequest())
    {
        Observation.AcceptRpgGate();
        GetPlayerState<AFablePlayerState>()->EnhanceItem(Id);
    }
}

/** 장비 제작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerCraft_Implementation(int32 Rarity, EFableEquipmentSlot Slot)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->CraftItem(Rarity, Slot);
    }
}

/** 장비 분해 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerDismantle_Implementation(int32 ItemId)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->DismantleItem(ItemId);
    }
}

/** 옵션 재설정 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerRerollAffix_Implementation(int32 ItemId, int32 AffixIndex)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->RerollAffix(ItemId, AffixIndex);
    }
}

/** 내구도 수리 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerRepair_Implementation(int32 ItemId)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->RepairItem(ItemId);
    }
}

/** 창고 보관 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerDeposit_Implementation(int32 ItemId)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->DepositItem(ItemId);
    }
}

/** 창고 수령 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerWithdraw_Implementation(int32 ItemId)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->WithdrawItem(ItemId);
    }
}

/** 상점 구매 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerShopBuy_Implementation(EFableShopOffer Offer)
{
    FFableScopedServiceValidationObservation Observation(this, TEXT("shop"), EFableTownServiceId::Merchant);
    if (AcceptRPGRequest())
    {
        Observation.AcceptRpgGate();
        GetPlayerState<AFablePlayerState>()->BuyShopItem(Offer);
    }
}

/** 아이템 판매 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerSell_Implementation(int32 Id)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->SellItem(Id);
    }
}

/** 물약 사용 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerUsePotion_Implementation(bool bMana)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->UsePotion(bMana);
    }
}

/** 스킬 강화 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerUpgradeSkill_Implementation(int32 Slot)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->UpgradeSkill(Slot);
    }
}

/** 특성 선택 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerSelectTalent_Implementation(int32 TargetTier, EFableTalentPath Path)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->SelectTalent(TargetTier, Path);
    }
}

/** 현재 직업 빌드 초기화 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerResetCurrentClassBuild_Implementation(EFableClass ExpectedClass,
                                                                         int32 ExpectedRevision)
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->ResetCurrentClassBuild(ExpectedClass, ExpectedRevision);
    }
}

/** 전직 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerAdvanceClass_Implementation()
{
    if (AcceptRPGRequest())
    {
        GetPlayerState<AFablePlayerState>()->AdvanceClass();
    }
}

/** 선택한 전직 시험을 다시 시작하도록 요청한다. */
void AFablePlayerController::RestartTrial()
{
    const auto* RetryResult = GetDisplayedExpeditionResult();
    if (!IsPartyExpeditionOpen() && RetryResult && RetryResult->IsTerminal() &&
        !RetryResult->RetryExpeditionId.IsValid())
    {
        UIBeginExpeditionRetry();
        return;
    }

    const FFableAdventureView Adventure = FableAdventure::For(this);

    if (!Adventure)
    {
        return;
    }

    // 원정 탭 안의 R 입력도 새 준비·입장 경로에서만 처리한다.
    if (IsPartyExpeditionOpen())
    {
        UIEnterSelectedDungeon();
        return;
    }

    // 지역·적·전투 진행 처리
    if (IsPortalOpen())
    {
        if (Adventure && Adventure->bRetryAvailable && Adventure->RetryRegion != EFableRegion::Village)
        {
            UIRetryEncounter();
        }
        else
        {
            UIEnterSelectedDungeon();
        }
        return;
    }

    // 지역·적·전투 진행 조건 검사
    if (Adventure && Adventure->bRetryAvailable &&
        (!IsAnyPanelOpen() || (Dialogue == EFableDialogue::Guide && Adventure->RetryRegion == EFableRegion::Village)))
    {
        UIRetryEncounter();
        return;
    }
    if (IsAnyPanelOpen())
    {
        return;
    }

    // 이 단계에서 전직 시험 재시작에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (NearestService(this) == EFableService::Portal)
    {
        ServerInteract();
    }
    else
    {
        ServerRestartTrial();
    }
}

/** 전직 시험 재시작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFablePlayerController::ServerRestartTrial_Implementation()
{
    if (!AcceptRPGRequest())
    {
        return;
    }
    if (AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>())
    {
        Mode->RequestNewRound(this);
    }
}

/** 논리 목적지와 현재 복제 revision만 전송한다. */
FGuid AFablePlayerController::UIRequestTownTravel(EFableTownId TargetTownId, FName AnchorId,
                                                  int32 ExpectedCatalogVersion)
{
    const FGuid RequestId = FGuid::NewGuid();
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!IsLocalController() || !Profile)
    {
        return RequestId;
    }
    bTownTravelUiLocked = true;
    ServerRequestTownTravel(RequestId, TargetTownId, AnchorId, ExpectedCatalogVersion,
                            Profile->GetTownConnectionSnapshot().PresenceRevision);
    return RequestId;
}

/** 취소를 보내는 즉시 queued 자동 작업을 막되 저수준 replay 전송은 막지 않는다. */
FGuid AFablePlayerController::UICancelTownTravel(FGuid TransactionId, int64 Generation)
{
    const FGuid AttemptId = FGuid::NewGuid();
    if (TownPrepared.TransactionId == TransactionId && TownPrepared.Generation == Generation)
    {
        bTownCancelRequested = true;
    }
    ServerCancelTownTravel(AttemptId, TransactionId, Generation, TownPrepared.ConnectionNonce);
    return AttemptId;
}

/** 명시적인 전송 경로는 잘못된 tuple/replay도 서버에서 판정하게 한다. */
FGuid AFablePlayerController::UIRequestTownTravelLoadAcknowledgement(FGuid TransactionId, int64 Generation,
                                                                     FGuid ConnectionNonce, FGuid ChallengeNonce,
                                                                     bool bLoaded)
{
    const FGuid AttemptId = FGuid::NewGuid();
    if (!bLoaded && TownPrepared.TransactionId == TransactionId && TownPrepared.Generation == Generation &&
        TownPrepared.ConnectionNonce == ConnectionNonce && TownPrepared.ChallengeNonce == ChallengeNonce)
    {
        // 정확한 현재 지형 실패 의도는 queued 자동 ready ACK를 즉시 막는다.
        bTownAutoAckSent = true;
    }
    ServerAcknowledgeTownGeometry(AttemptId, TransactionId, Generation, ConnectionNonce, ChallengeNonce, bLoaded);
    return AttemptId;
}

/** 현재 소유자의 준비 통지를 읽는다. */
const FFableTownTravelPrepared& AFablePlayerController::GetTownTravelPrepared() const
{
    return TownPrepared;
}

/** 가장 최근 응답의 상관 ID를 읽는다. */
const FFableTownTravelResponse& AFablePlayerController::GetTownTravelLastResponse() const
{
    return TownLastResponse;
}

/** 준비 통지 구독 지점을 반환한다. */
FOnFableTownTravelPrepared& AFablePlayerController::OnTownTravelPrepared()
{
    return TownPreparedEvents;
}

/** 매 시도 응답의 구독 지점을 반환한다. */
FOnFableTownTravelResponse& AFablePlayerController::OnTownTravelResponse()
{
    return TownResponseEvents;
}

/** authority/owner-client 구분을 포함한 진행 통지를 반환한다. */
FOnFableTownTravelEvent& AFablePlayerController::OnTownTravelProgress()
{
    return TownProgressEvents;
}

/** 여행 화면의 실제 확인 대기 상태를 읽는다. */
bool AFablePlayerController::IsTownTravelInputBlocked() const
{
    const auto* Profile = GetPlayerState<AFablePlayerState>();
    return bTownTravelUiLocked || (Profile && Profile->IsTownPersistenceBlocked());
}

/** 소유 pawn의 실제 연속 도착 샘플 수를 읽는다. */
int32 AFablePlayerController::GetTownArrivalSampleCount() const
{
    return TownArrivalSamples;
}

/** 실제 샘플 사이의 경과 시간을 읽는다. */
double AFablePlayerController::GetTownArrivalSampleSpan() const
{
    return TownArrivalSamples > 0 ? TownArrivalLastSampleTime - TownArrivalFirstSampleTime : 0.0;
}

/** RPG 입력 제한과 별개인 owner reliable 요청을 조정자에 전달한다. */
void AFablePlayerController::ServerRequestTownTravel_Implementation(FGuid RequestId, EFableTownId TargetTownId,
                                                                    FName AnchorId, int32 ExpectedCatalogVersion,
                                                                    int64 ExpectedPresenceRevision)
{
    AFableTownManager* Towns = AFableTownManager::Ensure(GetWorld());
    if (auto* Coordinator = Towns ? Towns->GetTravelCoordinator() : nullptr)
    {
        FFableTownAnchorRef Target;
        Target.TownId = TargetTownId;
        Target.AnchorId = AnchorId;
        Target.CatalogVersion = ExpectedCatalogVersion;
        Coordinator->BeginAnchorTravel(this, RequestId, Target, ExpectedPresenceRevision);
    }
}

/** ACK는 일반 RPG throttle와 분리해 정확 tuple을 전달한다. */
void AFablePlayerController::ServerAcknowledgeTownGeometry_Implementation(FGuid AckAttemptId, FGuid TransactionId,
                                                                          int64 Generation, FGuid ConnectionNonce,
                                                                          FGuid ChallengeNonce, bool bLoaded)
{
    if (auto* Coordinator = UFableTownTravelCoordinator::Find(this))
    {
        Coordinator->AcknowledgeGeometry(this, AckAttemptId, TransactionId, Generation, ConnectionNonce, ChallengeNonce,
                                         bLoaded);
    }
}

/** 취소는 소유자 세대가 같은 경우에만 서버에서 적용한다. */
void AFablePlayerController::ServerCancelTownTravel_Implementation(FGuid CancelAttemptId, FGuid TransactionId,
                                                                   int64 Generation, FGuid ConnectionNonce)
{
    if (auto* Coordinator = UFableTownTravelCoordinator::Find(this))
    {
        Coordinator->CancelAnchorTravel(this, CancelAttemptId, TransactionId, Generation, ConnectionNonce);
    }
}

/** 도착 확인도 arbitrary XYZ 없이 정확한 서버 receipt만 참조한다. */
void AFablePlayerController::ServerConfirmTownArrival_Implementation(FGuid AttemptId, FGuid TransactionId,
                                                                     int64 Generation, FGuid ConnectionNonce,
                                                                     int64 ExpectedCommitSequence)
{
    if (auto* Coordinator = UFableTownTravelCoordinator::Find(this))
    {
        Coordinator->ConfirmOwnArrival(this, AttemptId, TransactionId, Generation, ConnectionNonce,
                                       ExpectedCommitSequence);
    }
}

/** Prepared callback과 같은 tick의 정상 취소가 다음 tick 자동ACK보다 먼저 적용된다. */
void AFablePlayerController::ClientPrepareTownGeometry_Implementation(const FFableTownTravelPrepared& Prepared)
{
    if (!IsLocalController())
    {
        return;
    }
    if (TownPrepared.TransactionId == Prepared.TransactionId && TownPrepared.Generation == Prepared.Generation)
    {
        return;
    }
    ReleaseTownPendingGeometry(true);
    TownPrepared = Prepared;
    bTownCancelRequested = false;
    bTownAutoAckSent = false;
    bTownAutoAckEligible = false;
    bTownTerminal = false;
    bTownTravelUiLocked = true;
    TownArrivalSamples = 0;
    bTownArrivalConfirmationSent = false;
    bTownArrivalFailed = false;
    bTownPhysicalArrivalObserved = false;
    TownAwaitingArrival = FFableTownTravelCommitReceipt();
    TownPreparedEvents.Broadcast(TownPrepared);

    const FGuid TransactionId = Prepared.TransactionId;
    const int64 Generation = Prepared.Generation;
    const FGuid ConnectionNonce = Prepared.ConnectionNonce;
    GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(
        this,
        [this, TransactionId, Generation, ConnectionNonce]()
        {
            if (TownPrepared.TransactionId == TransactionId && TownPrepared.Generation == Generation &&
                TownPrepared.ConnectionNonce == ConnectionNonce && !bTownCancelRequested && !bTownTerminal)
            {
                bTownAutoAckEligible = true;
                TryAutoAcknowledgeTownGeometry();
            }
        }));
}

/** 別channelのPS複製がPreparedより遅ければ同じTTL内で待つ。 */
void AFablePlayerController::TryAutoAcknowledgeTownGeometry()
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!IsLocalController() || !GetWorld() || !Profile || bTownCancelRequested || bTownTerminal || bTownAutoAckSent ||
        !bTownAutoAckEligible || !TownPrepared.TransactionId.IsValid())
    {
        return;
    }
    const auto& Connection = Profile->GetTownConnectionSnapshot();
    if (Connection.OwnerConnectionGeneration != TownPrepared.OwnerConnectionGeneration ||
        Connection.ConnectionNonce != TownPrepared.ConnectionNonce || !Connection.WorldEpoch.IsValid())
    {
        return;
    }
    auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
    if (!Streaming || !AFableTownManager::Find(GetWorld()))
    {
        return;
    }
    FFableTownGeometryLeaseOwner GeometryOwner;
    GeometryOwner.OwnerConnectionGeneration = TownPrepared.OwnerConnectionGeneration;
    GeometryOwner.OwnerPlayerState = const_cast<AFablePlayerState*>(Profile);
    GeometryOwner.TransactionId = TownPrepared.TransactionId;
    EFableTownGeometryFailure Failure;
    if (!TownSourceLease.IsValid())
    {
        FFableTownGeometryDescriptor Source;
        Source.Mode = EFableTownGeometryMode::ExistingPersistent;
        Source.TownId = TownPrepared.SourceContext.TownId;
        Source.CatalogVersion = TownPrepared.SourceContext.CatalogVersion;
        Source.LocalToWorld = FTransform::Identity;
        Source.FixedInstanceName = TEXT("FableTown_LegacyVillage_Geometry_v1");
        Source.ContentIdentity = TEXT("LegacyPersistentV1");
        GeometryOwner.Purpose = EFableTownGeometryLeasePurpose::ClientSourceRetention;
        if (!Streaming->AcquireGeometry(Source, GeometryOwner, TownSourceLease, Failure))
        {
            if (Failure != EFableTownGeometryFailure::InvalidOwner)
            {
                bTownAutoAckSent = true;
                UIRequestTownTravelLoadAcknowledgement(TownPrepared.TransactionId, TownPrepared.Generation,
                                                       TownPrepared.ConnectionNonce, TownPrepared.ChallengeNonce,
                                                       false);
            }
            return;
        }
    }
    if (!TownTargetLease.IsValid())
    {
        GeometryOwner.Purpose = EFableTownGeometryLeasePurpose::ClientTargetPending;
        if (!Streaming->AcquireGeometry(TownPrepared.Geometry, GeometryOwner, TownTargetLease, Failure))
        {
            if (Failure != EFableTownGeometryFailure::InvalidOwner)
            {
                bTownAutoAckSent = true;
                UIRequestTownTravelLoadAcknowledgement(TownPrepared.TransactionId, TownPrepared.Generation,
                                                       TownPrepared.ConnectionNonce, TownPrepared.ChallengeNonce,
                                                       false);
            }
            return;
        }
    }
    const auto Status = Streaming->GetGeometryStatus(TownTargetLease);
    if (Status.State == EFableTownGeometryState::Failed || Status.State == EFableTownGeometryState::Removed)
    {
        bTownAutoAckSent = true;
        UIRequestTownTravelLoadAcknowledgement(TownPrepared.TransactionId, TownPrepared.Generation,
                                               TownPrepared.ConnectionNonce, TownPrepared.ChallengeNonce, false);
    }
    else if (Streaming->IsGeometryReadyVisible(TownTargetLease))
    {
        bTownAutoAckSent = true;
        UIRequestTownTravelLoadAcknowledgement(TownPrepared.TransactionId, TownPrepared.Generation,
                                               TownPrepared.ConnectionNonce, TownPrepared.ChallengeNonce, true);
    }
}

/** 応答を先に保存してから購読者へ通知する。 */
void AFablePlayerController::ClientTownTravelResponse_Implementation(const FFableTownTravelResponse& Response)
{
    TownLastResponse = Response;
    if (Response.TransactionId == TownPrepared.TransactionId && Response.Generation == TownPrepared.Generation)
    {
        if (Response.Operation == EFableTownTravelOperation::ConfirmArrival)
        {
            TownResponseEvents.Broadcast(Response);
            if (Response.Error == EFableTownTravelError::None && Response.bOwnArrivalConfirmed &&
                TownAwaitingArrival.TransactionId == Response.TransactionId &&
                TownAwaitingArrival.CommitSequence == Response.Commit.CommitSequence)
            {
                CompleteTownArrivalAfterConfirmation();
            }
            else
            {
                bTownArrivalConfirmationSent = false;
                TownArrivalSamples = 0;
                bTownPhysicalArrivalObserved = false;
                EmitLocalTownEvent(EFableTownTravelEventKind::AttemptRejected, Response.Error);
            }
            return;
        }
        else if (Response.State == EFableTownTravelState::Committed && Response.Commit.CommitCount == 1)
        {
            bTownTerminal = true;
            if (TownObservedArrivalId != Response.TransactionId &&
                TownAwaitingArrival.TransactionId != Response.TransactionId)
            {
                TownAwaitingArrival = Response.Commit;
                TownArrivalSamples = 0;
                bTownTravelUiLocked = true;
                bTownArrivalConfirmationSent = false;
                bTownArrivalFailed = false;
                bTownPhysicalArrivalObserved = false;
                TownArrivalDeadline = GetWorld()->GetTimeSeconds() + 5.0;
                if (!bTownMoveInputHeld)
                {
                    SetIgnoreMoveInput(true);
                    bTownMoveInputHeld = true;
                }
                if (auto* Hero = GetPawn<AFableCharacter>())
                {
                    Hero->GetCharacterMovement()->StopMovementImmediately();
                }
            }
        }
        else if (Response.State == EFableTownTravelState::Cancelled || Response.State == EFableTownTravelState::Expired)
        {
            bTownTerminal = true;
            bTownTravelUiLocked = Response.Error == EFableTownTravelError::RollbackFailed;
            if (Response.Error != EFableTownTravelError::RollbackFailed)
            {
                ReleaseTownPendingGeometry(true);
            }
        }
    }
    else if (Response.Operation == EFableTownTravelOperation::Begin && Response.Error != EFableTownTravelError::None &&
             !TownAwaitingArrival.TransactionId.IsValid() && (!TownPrepared.TransactionId.IsValid() || bTownTerminal))
    {
        bTownTravelUiLocked = false;
    }
    TownResponseEvents.Broadcast(Response);
}

/** server terminal event는 TTL UI를 정리하며 RPC 시도 응답과 섞지 않는다. */
void AFablePlayerController::ClientTownTravelEvent_Implementation(const FFableTownTravelEvent& Event)
{
    if (Event.TransactionId == TownPrepared.TransactionId && Event.Generation == TownPrepared.Generation &&
        (Event.State == EFableTownTravelState::Cancelled || Event.State == EFableTownTravelState::Expired))
    {
        bTownTerminal = true;
        bTownTravelUiLocked = Event.Error == EFableTownTravelError::RollbackFailed;
        if (Event.Error != EFableTownTravelError::RollbackFailed)
        {
            ReleaseTownPendingGeometry(true);
        }
    }
    TownProgressEvents.Broadcast(Event);
}

/** 정확 응답과 복제된 소유 pawn의 실제 도착을 함께 관찰한다. */
void AFablePlayerController::TickTownTravel()
{
    if (!IsLocalController())
    {
        return;
    }
    if (!bTownTerminal && !bTownCancelRequested && TownPrepared.TransactionId.IsValid())
    {
        TryAutoAcknowledgeTownGeometry();
    }
    if (!TownAwaitingArrival.TransactionId.IsValid() || bTownArrivalFailed)
    {
        return;
    }
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = GetPawn<AFableCharacter>();
    const auto* Movement = Hero ? Hero->GetCharacterMovement() : nullptr;
    if (Profile && TownLastResponse.Operation == EFableTownTravelOperation::ConfirmArrival &&
        TownLastResponse.Error == EFableTownTravelError::None && TownLastResponse.bOwnArrivalConfirmed &&
        TownLastResponse.TransactionId == TownAwaitingArrival.TransactionId &&
        Profile->IsTownAnchorDurable(TownAwaitingArrival.TransactionId))
    {
        CompleteTownArrivalAfterConfirmation();
        return;
    }
    const auto& Commit = TownAwaitingArrival;
    const auto* Connection = Profile ? &Profile->GetTownConnectionSnapshot() : nullptr;
    const FVector Delta = Hero ? Hero->GetActorLocation() - Commit.AcceptedTransform.GetLocation() : FVector(MAX_flt);
    const bool bArrived =
        Connection && Hero && Movement && Connection->LastCommittedTransactionId == Commit.TransactionId &&
        Connection->LastCommittedGeneration == Commit.Generation &&
        Connection->PresenceRevision == Commit.PresenceRevision &&
        Connection->SafeCheckpointRevision == Commit.SafeCheckpointRevision && FVector2D(Delta).Size() <= 10.0f &&
        FMath::Abs(Delta.Z) <= 10.0f && Movement->IsMovingOnGround();
    const double Now = GetWorld()->GetTimeSeconds();
    if (!bArrived)
    {
        TownArrivalSamples = 0;
        bTownPhysicalArrivalObserved = false;
    }

    // 실제 도착은 원래 5초 안에 확인한다. 저장 ACK 대기는 그 물리 제한과 구분한다.
    if (!bTownPhysicalArrivalObserved && Now >= TownArrivalDeadline)
    {
        bTownArrivalFailed = true;
        bTownTravelUiLocked = true;
        EmitLocalTownEvent(EFableTownTravelEventKind::AttemptRejected, EFableTownTravelError::ArrivalUnconfirmed);
        return;
    }
    if (!bArrived)
    {
        return;
    }

    if (!bTownPhysicalArrivalObserved)
    {
        if (TownArrivalSamples == 0)
        {
            TownArrivalFirstSampleTime = Now;
            TownArrivalLastSampleTime = Now;
            TownArrivalSamples = 1;
        }
        else if (Now - TownArrivalLastSampleTime >= 0.1)
        {
            ++TownArrivalSamples;
            TownArrivalLastSampleTime = Now;
        }
        if (TownArrivalSamples < 3 || Now - TownArrivalFirstSampleTime < 0.3)
        {
            return;
        }

        bTownPhysicalArrivalObserved = true;
        EmitLocalTownEvent(EFableTownTravelEventKind::OwnArrivalObserved, EFableTownTravelError::None);
    }

    // 同じ移動のACKが来るまでsourceと入力停止を保持し、確認RPCを連打しない。
    if (!bTownArrivalConfirmationSent && Profile->IsTownAnchorDurable(Commit.TransactionId))
    {
        bTownArrivalConfirmationSent = true;
        ServerConfirmTownArrival(FGuid::NewGuid(), Commit.TransactionId, Commit.Generation,
                                 TownPrepared.ConnectionNonce, Commit.CommitSequence);
    }
}

/** 서버 확인 응답 뒤에만 로컬 source lease와 UI 잠금을 해제한다. */
void AFablePlayerController::CompleteTownArrivalAfterConfirmation()
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!Profile || !TownAwaitingArrival.TransactionId.IsValid() ||
        !Profile->IsTownAnchorDurable(TownAwaitingArrival.TransactionId))
    {
        return;
    }
    const auto Commit = TownAwaitingArrival;
    auto* Streaming = GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>();
    if (!Streaming || !TownTargetLease.IsValid() || !Streaming->IsGeometryReadyVisible(TownTargetLease))
    {
        return;
    }
    FFableTownGeometryLeaseOwner GeometryOwner;
    GeometryOwner.OwnerConnectionGeneration = TownPrepared.OwnerConnectionGeneration;
    GeometryOwner.OwnerPlayerState = const_cast<AFablePlayerState*>(Profile);
    GeometryOwner.Purpose = EFableTownGeometryLeasePurpose::ClientPresence;
    FFableTownGeometryLease Presence;
    EFableTownGeometryFailure Failure;
    if (!Streaming->AcquireGeometry(TownPrepared.Geometry, GeometryOwner, Presence, Failure))
    {
        return;
    }
    if (TownPresenceLease.IsValid() && TownPresenceLease.LeaseId != Presence.LeaseId)
    {
        Streaming->ReleaseGeometry(TownPresenceLease);
    }
    TownPresenceLease = Presence;
    TownObservedArrivalId = Commit.TransactionId;
    bTownTravelUiLocked = false;
    if (bTownMoveInputHeld)
    {
        SetIgnoreMoveInput(false);
        bTownMoveInputHeld = false;
    }
    EmitLocalTownEvent(EFableTownTravelEventKind::UiUnlocked, EFableTownTravelError::None);
    ReleaseTownPendingGeometry(true);
    EmitLocalTownEvent(EFableTownTravelEventKind::SourceLeaseReleased, EFableTownTravelError::None);
    TownAwaitingArrival = FFableTownTravelCommitReceipt();
}

/** 정확히 보관한 임시 token만 반환한다. persistent Legacy에는 unload를 요청하지 않는다. */
void AFablePlayerController::ReleaseTownPendingGeometry(bool bReleaseSource)
{
    if (auto* Streaming = GetWorld() ? GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>() : nullptr)
    {
        if (TownTargetLease.IsValid())
        {
            Streaming->ReleaseGeometry(TownTargetLease);
        }
        if (bReleaseSource && TownSourceLease.IsValid())
        {
            Streaming->ReleaseGeometry(TownSourceLease);
        }
    }
    TownTargetLease = FFableTownGeometryLease();
    if (bReleaseSource)
    {
        TownSourceLease = FFableTownGeometryLease();
    }
}

/** 로컬 관측은 별도 sequence와 emitter로 발행한다. */
void AFablePlayerController::EmitLocalTownEvent(EFableTownTravelEventKind Kind, EFableTownTravelError Error)
{
    FFableTownTravelEvent Event;
    if (TownLocalEventSequence >= FableTownTravel::MaxSafeSequence)
    {
        return;
    }
    Event.Sequence = ++TownLocalEventSequence;
    Event.Kind = Kind;
    Event.RequestId = TownPrepared.RequestId;
    Event.TransactionId = TownPrepared.TransactionId;
    Event.Generation = TownPrepared.Generation;
    Event.State = EFableTownTravelState::Committed;
    Event.Error = Error;
    Event.ServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
    Event.bAuthority = false;
    TownProgressEvents.Broadcast(Event);
}

/** ローカルleaseの寿命をcontrollerと同じにする。 */
void AFablePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bTownMoveInputHeld)
    {
        SetIgnoreMoveInput(false);
        bTownMoveInputHeld = false;
    }
    ReleaseTownPendingGeometry(true);
    if (auto* Streaming = GetWorld() ? GetWorld()->GetSubsystem<UFableTownStreamingSubsystem>() : nullptr)
    {
        if (TownPresenceLease.IsValid())
        {
            Streaming->ReleaseGeometry(TownPresenceLease);
        }
    }
    Super::EndPlay(EndPlayReason);
}

/** 정상 인증 세션으로 새 ticket을 발급한다. 원본 profile 값은 클라이언트에서 설정하지 않는다. */
void AFablePlayerController::ClientRequireProfileReclaim_Implementation(int64 SeedRevision, const FString& OriginalHash)
{
    if (auto* GI = GetGameInstance())
    {
        if (auto* Flow = GI->GetSubsystem<UFableOnlineFlowSubsystem>())
        {
            Flow->HandleProfileReclaim(SeedRevision, OriginalHash);
        }
    }
}
