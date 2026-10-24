#include "FableCoopValidationDriver.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableEnemyBolt.h"
#include "FablePlayerController.h"
#include "FableAdventure.h"
#include "FablePlayerState.h"
#include "FablePartyManager.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "TimerManager.h"

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
UFableCoopValidationDriver::UFableCoopValidationDriver()
{
    PrimaryComponentTick.bCanEverTick = true;
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void UFableCoopValidationDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
#else
    PC = Cast<AFablePlayerController>(GetOwner());
    if (!PC.IsValid() || !PC->IsLocalController() ||
        !FParse::Value(FCommandLine::Get(), TEXT("FableCoopValidationRole="), Role) ||
        !FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRun="), RunId) ||
        !FParse::Value(FCommandLine::Get(), TEXT("FableCoopValidationDirectory="), Directory) ||
        !FParse::Value(FCommandLine::Get(), TEXT("FableCoopScenario="), Scenario) ||
        (Role != TEXT("host") && Role != TEXT("guest")) || FPaths::IsRelative(Directory))
    {
        SetComponentTickEnabled(false);
        return;
    }
    FParse::Value(FCommandLine::Get(), TEXT("FableCoopPhase="), Phase);
    if (Scenario == TEXT("disconnect") && Phase == TEXT("fresh") && Role == TEXT("guest") &&
        IFileManager::Get().FileExists(*FPaths::Combine(Directory, TEXT("guest-disconnect-approved.txt"))))
    {
        // A local level opened after menu disconnect must not restart this
        // opt-in scenario or overwrite its authoritative ready report.
        SetComponentTickEnabled(false);
        return;
    }
    bSolo = Scenario == TEXT("solo");
    IFileManager::Get().MakeDirectory(*Directory, true);
    StartedAt = StepAt = GetWorld()->GetTimeSeconds();
#endif
}

/** 검증 캐릭터에 단발 키 입력을 전달한다. */
void UFableCoopValidationDriver::Key(const FKey& Value)
{
    if (!PC.IsValid() || !PC->PlayerInput)
    {
        return;
    }
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value, IE_Pressed, 1, 0, FInputDeviceId::CreateFromInternalId(0)));
    ReleaseKeys.AddUnique(Value);
}

/** 검증에 필요한 키의 누름 상태를 유지한다. */
void UFableCoopValidationDriver::Hold(const FKey& Value, bool bHeld)
{
    if (PC.IsValid() && PC->PlayerInput && PC->IsInputKeyDown(Value) != bHeld)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value, bHeld ? IE_Pressed : IE_Released, bHeld ? 1 : 0, 0,
                                                         FInputDeviceId::CreateFromInternalId(0)));
    }
}

/** 검증에서 누른 키의 유지 상태를 해제한다. */
void UFableCoopValidationDriver::Release()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!PC.IsValid())
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 순회
    for (const FKey& Value : ReleaseKeys)
    {
        PC->InputKey(
            FInputKeyEventArgs::CreateSimulated(Value, IE_Released, 0, 0, FInputDeviceId::CreateFromInternalId(0)));
    }

    ReleaseKeys.Empty();
}

/** 검증 캐릭터의 이동 및 유지 입력을 해제한다. */
void UFableCoopValidationDriver::Stop()
{
    Hold(EKeys::W, false);
    Hold(EKeys::S, false);
    Hold(EKeys::A, false);
    Hold(EKeys::D, false);
}

/** 일반 이동 입력으로 검증 캐릭터를 목표 지점에 접근시킨다. */
bool UFableCoopValidationDriver::WalkTo(const FVector& Location, float Radius)
{
    const APawn* Pawn = PC.IsValid() ? PC->GetPawn() : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Pawn)
    {
        return false;
    }

    // 검증 입력과 키 유지 상태 처리
    Hold(EKeys::S, false);
    const FVector Delta = Location - Pawn->GetActorLocation();

    // 검증 입력과 키 유지 상태 조건 검사
    if (Delta.Size2D() <= Radius)
    {
        Hold(EKeys::W, false);
        return true;
    }

    // 검증 입력과 키 유지 상태 처리
    PC->SetControlRotation(FRotator(-12, Delta.Rotation().Yaw, 0));
    Hold(EKeys::W, true);

    return false;
}

/** 검증 상태를 다음 단계로 옮기고 단계 시작 시각을 갱신한다. */
void UFableCoopValidationDriver::Advance(int32 Value)
{
    // 검증 입력과 키 유지 상태 처리
    Stop();
    Hold(EKeys::E, false);
    Step = Value;
    StepAt = GetWorld()->GetTimeSeconds();
    Substep = 0;
    SubAt = StepAt;
    LastAction = -10;
    UE_LOG(LogTemp, Display, TEXT("FABLE_COOP_VALIDATION role=%s scenario=%s step=%d"), *Role, *Scenario, Step);
    Report(TEXT("running"), TEXT("Normal-input co-op recovery validation in progress."));
}

/** 현재 검증 단계의 화면을 캡처한다. */
void UFableCoopValidationDriver::Capture(const FString& Name)
{
    // 체력·마나·보호막·피해량에 필요한 값 준비
    const FString Path = FPaths::Combine(Directory, Role + TEXT("-") + Name + TEXT(".png"));
    const TWeakObjectPtr<UFableCoopValidationDriver> Weak(this);
    FTimerHandle Timer;
    GetWorld()->GetTimerManager().SetTimer(
        Timer,
        [Weak, Path]()
        {
            if (Weak.IsValid())
            {
                FScreenshotRequest::RequestScreenshot(Path, true, false);
            }
        },
        .35f, false);
    Screenshots.AddUnique(Path);
}

/** 상대 클라이언트 보고서에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
TSharedPtr<FJsonObject> UFableCoopValidationDriver::PeerReport() const
{
    // JSON 계약과 검증 기록에 필요한 값 준비
    FString Data;
    TSharedPtr<FJsonObject> Value;

    // JSON 계약과 검증 기록 처리
    if (FFileHelper::LoadFileToString(
            Data, *FPaths::Combine(Directory, Role == TEXT("host") ? TEXT("guest.json") : TEXT("host.json"))))
    {
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Value);
    }

    return Value;
}

/** 상대 클라이언트 단계 번호에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
int32 UFableCoopValidationDriver::PeerStep() const
{
    const TSharedPtr<FJsonObject> Value = PeerReport();
    double Number = 0;
    return Value.IsValid() && Value->TryGetNumberField(TEXT("step"), Number) ? int32(Number) : -1;
}

/** 같은 파티의 다른 플레이어 캐릭터를 찾는다. */
AFableCharacter* UFableCoopValidationDriver::Partner() const
{
    const APawn* Own = PC.IsValid() ? PC->GetPawn() : nullptr;

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        if (!FableAdventure::SameScope(PC.Get(), *It))
        {
            continue;
        }

        if (*It != Own && It->GetPlayerState<AFablePlayerState>())
        {
            return *It;
        }
    }

    return nullptr;
}

/** 검증 캐릭터 주변에서 가장 가까운 유효한 적을 찾는다. */
AFableEnemy* UFableCoopValidationDriver::NearestEnemy(bool bBossOnly, bool bPreferMelee) const
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const APawn* Hero = PC.IsValid() ? PC->GetPawn() : nullptr;
    const FFableAdventureView GS = FableAdventure::For(PC.Get());

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !GS)
    {
        return nullptr;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    AFableEnemy* Target = nullptr;
    float Closest = MAX_flt;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        // 자기 원정과 같은 UUID의 대상만 관찰한다.
        if (!FableAdventure::SameScope(PC.Get(), *It))
        {
            continue;
        }

        if (!It->IsAlive() || It->GetEncounterRegion() != GS->ActiveRegion || (bBossOnly && !It->IsBoss()))
        {
            continue;
        }
        const float Distance = FVector::DistSquared2D(Hero->GetActorLocation(), It->GetActorLocation()) +
                               (bPreferMelee && It->IsRanged() ? 100000000.f : 0.f);
        if (Distance < Closest)
        {
            Target = *It;
            Closest = Distance;
        }
    }

    return Target;
}

/** 적 개수에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
int32 UFableCoopValidationDriver::EnemyCount() const
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    int32 Count = 0;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        // 자기 원정과 같은 UUID의 대상만 관찰한다.
        if (!FableAdventure::SameScope(PC.Get(), *It))
        {
            continue;
        }

        if (It->IsAlive() && GS && It->GetEncounterRegion() == GS->ActiveRegion)
        {
            ++Count;
        }
    }

    return Count;
}

/** 현재 검증 전투의 적이 초기 체력과 개체 수를 유지하는지 확인한다. */
bool UFableCoopValidationDriver::EnemiesAreFresh() const
{
    // 지역·적·전투 진행 조건 검사
    if (EnemyCount() != 3)
    {
        return false;
    }

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        // 자기 원정과 같은 UUID의 대상만 관찰한다.
        if (!FableAdventure::SameScope(PC.Get(), *It))
        {
            continue;
        }

        if (It->IsAlive() && !FMath::IsNearlyEqual(It->GetHealth(), It->GetMaxHealth(), .1f))
        {
            return false;
        }
    }

    return true;
}

/** 파티원이 협동 검증의 입장 조건을 모두 충족하는지 확인한다. */
bool UFableCoopValidationDriver::PartyEligible(EFableRegion Region) const
{
    const FFableAdventureView GS = FableAdventure::For(PC.Get());

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!GS || GS.Players().Num() != (bSolo ? 1 : 2))
    {
        return false;
    }

    // 퀘스트와 이야기 진행 순회
    for (const APlayerState* Entry : GS.Players())
    {
        const AFablePlayerState* Profile = Cast<AFablePlayerState>(Entry);
        if (!Profile || !Profile->bProfileLoaded)
        {
            return false;
        }
        if (Region == EFableRegion::Village)
        {
            if (Profile->ForestQuest != EFableQuestStage::Active)
            {
                return false;
            }
        }
        else if (!Profile->IsRegionEligible(Region))
        {
            return false;
        }
    }

    return true;
}

/** 퀘스트 선택 상태를 갱신한다. */
void UFableCoopValidationDriver::SelectQuest(EFableQuestId Quest)
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    const double Now = GetWorld()->GetTimeSeconds();
    const FVector Board = FableRPG::BoardLocation() + FVector(Role == TEXT("host") ? -100 : 100, -120, 100);

    // 위치·방향·거리 계산 처리
    if (PC->IsAnyPanelOpen() && FVector::DistSquared2D(Hero->GetActorLocation(), Board) > 10000)
    {
        PC->UIClosePanels();
    }

    // 이동 입력과 속도 제한 조건 검사
    if (!WalkTo(Board, 85) || Hero->GetVelocity().Size2D() > 10)
    {
        LastAction = Now;
        return;
    }
    if (Now - LastAction < .7)
    {
        return;
    }

    // 퀘스트와 이야기 진행 처리
    if (PC->GetDialogue() == EFableDialogue::Guide)
    {
        PC->UISelectQuest(Quest);
        PC->UIConfirmQuest();
    }
    else
    {
        PC->UIClosePanels();
        Key(EKeys::E);
    }

    LastAction = Now;
}

/** 지역 입장에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableCoopValidationDriver::EnterRegion(EFableRegion Region, bool bRetry)
{
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    const double Now = GetWorld()->GetTimeSeconds();
    const FVector Destination =
        (Region == EFableRegion::Village ? FableRPG::BoardLocation() : FableRPG::PortalLocation()) +
        FVector(Role == TEXT("host") ? -90 : 90, -110, 100);

    // 이동 중에는 패널이 일반 이동 입력을 막지 않도록 닫는다.
    if (PC->IsAnyPanelOpen() && FVector::DistSquared2D(Hero->GetActorLocation(), Destination) > 10000)
    {
        PC->UIClosePanels();
    }
    if (!WalkTo(Destination, 95) || Hero->GetVelocity().Size2D() > 10)
    {
        LastAction = Now;
        return;
    }
    if (Now - LastAction < .8)
    {
        return;
    }

    // 숲 재도전은 마을 게시판의 기존 일반 입력을 유지한다.
    if (Region == EFableRegion::Village)
    {
        PC->UIClosePanels();
        Key(bRetry ? EKeys::R : EKeys::E);
        LastAction = Now;
        return;
    }

    AFablePlayerState* Profile = PC->GetPlayerState<AFablePlayerState>();
    AFablePartyManager* Manager = AFablePartyManager::Find(GetWorld());
    const FFableAdventureView State = FableAdventure::For(PC.Get());
    if (!Profile || !Manager || !State || State.Players().Num() != (bSolo ? 1 : 2))
    {
        LastAction = Now;
        return;
    }
    if (!PC->IsPartyExpeditionOpen())
    {
        PC->UIOpenPartyExpedition();
    }

    // 협동 검증도 실제 생성·초대·수락 요청으로 명부를 구성한다.
    const FFablePartyRecord* Party = Manager->GetPartyForMember(Profile->PartyPlayerKey);
    if (!Party)
    {
        if (const FFablePartyInvite* Invite = Manager->GetPendingInviteFor(Profile->PartyPlayerKey))
        {
            PC->UIRespondPartyInvite(Invite->InviteId, true);
        }
        else if (Role == TEXT("host") || bSolo)
        {
            PC->UICreateParty();
        }
        LastAction = Now;
        return;
    }

    const bool bLeader = Party->LeaderKey == Profile->PartyPlayerKey;
    if (Party->Members.Num() < (bSolo ? 1 : 2))
    {
        if (bLeader)
        {
            for (APlayerState* Entry : State.Players())
            {
                AFablePlayerState* Peer = Cast<AFablePlayerState>(Entry);
                if (Peer && Peer != Profile && !Peer->PartyPlayerKey.IsEmpty() &&
                    !Manager->GetPartyForMember(Peer->PartyPlayerKey) &&
                    !Manager->GetPendingInviteFor(Peer->PartyPlayerKey))
                {
                    PC->UIInviteParty(Peer->GetPlayerId());
                    break;
                }
            }
        }
        LastAction = Now;
        return;
    }

    // 구조된 참가자의 복귀는 새 원정의 동의와 구분한다.
    if (Profile->ActiveExpeditionId.IsValid())
    {
        if (!PC->IsPartyPreparationPending())
        {
            PC->UIEnterSelectedDungeon();
        }
        LastAction = Now;
        return;
    }
    if (PC->IsPartyPreparationPending())
    {
        LastAction = Now;
        return;
    }

    const EFableDifficulty Difficulty = bRetry ? State->RetryDifficulty : Party->SelectedDifficulty;
    if (Party->SelectedRegion != Region || Party->SelectedDifficulty != Difficulty)
    {
        if (bLeader)
        {
            PC->UIRequestPartySelection(Party->PartyId, Party->PreparationRevision, Region, Difficulty);
        }
        LastAction = Now;
        return;
    }
    if (bRetry && !Checks.FindRef(TEXT("retry_ui_captured")))
    {
        Capture(TEXT("retry-ready"));
        Checks.Add(TEXT("retry_ui_captured"), true);
        LastAction = Now;
        return;
    }

    // 각자 명시적으로 동의한 뒤 파티장만 서버 입장을 요청한다.
    if (!PC->IsPartyReady())
    {
        PC->UISetPartyReady(true);
    }
    else if (bLeader && PC->CanEnterPreparedParty())
    {
        PC->UIEnterPreparedParty();
    }
    LastAction = Now;
}

/** 일반 공격·스킬 입력으로 현재 전투 검증을 진행한다. */
void UFableCoopValidationDriver::Fight()
{
    // 지역·적·전투 진행에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFableEnemy* Target = NearestEnemy();

    // 이 단계에서 일반 공격·스킬 입력으로 현재 전투 검증을 진행한다.
    if (PC->IsAnyPanelOpen())
    {
        PC->UIClosePanels();
    }

    // 검증 입력과 키 유지 상태 조건 검사
    if (!Hero->IsAlive() || !Target)
    {
        Stop();
        return;
    }

    // 검증 입력과 키 유지 상태에 필요한 값 준비
    const float Distance = FVector::Dist2D(Hero->GetActorLocation(), Target->GetActorLocation());
    PC->SetControlRotation(FRotator(-12, (Target->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
    Hold(EKeys::W, Distance > 570);
    Hold(EKeys::S, Distance < 350);
    const double Now = GetWorld()->GetTimeSeconds();

    // 검증 입력과 키 유지 상태 처리
    if (Now - LastAction > .62 && Distance < 1050)
    {
        Key(EKeys::J);
        LastAction = Now;
        ++AttackInputs;
    }
}

/** 검증 캐릭터가 적의 일반 공격에 피격되도록 위치와 입력을 조절한다. */
void UFableCoopValidationDriver::TakeHits()
{
    // 이 단계에서 검증 캐릭터가 적의 일반 공격에 피격되도록 위치와 입력을 조절한다.
    if (PC->IsAnyPanelOpen())
    {
        PC->UIClosePanels();
    }

    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();

    // 검증 입력과 키 유지 상태 조건 검사
    if (!Hero->IsAlive())
    {
        Stop();
        return;
    }

    AFableEnemy* Enemy = NearestEnemy(false, true);

    // 지역·적·전투 진행 처리
    if (Enemy)
    {
        WalkTo(Enemy->GetActorLocation() + FVector(Role == TEXT("host") ? -80 : 80, -100, 0), 100);
    }
    else
    {
        Stop();
    }
}

/** 검증 대상 상태의 변경 여부를 비교할 서명 문자열을 만든다. */
FString UFableCoopValidationDriver::Signature(const AFablePlayerState* PS, bool bIgnoreWear) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!PS)
    {
        return TEXT("");
    }

    // 재화와 작업 비용에 필요한 값 준비
    FString Value = FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|potions:%d,%d"), uint8(PS->HeroClass),
                                    PS->Level, PS->Experience, PS->Gold, PS->Ore, PS->EquippedWeaponId,
                                    PS->EquippedArmorId, uint8(PS->ForestQuest), uint8(PS->DungeonQuest),
                                    PS->QuestKills, PS->HealthPotions, PS->ManaPotions);
    // Combat may wear carried equipment. Every other persistent field remains exact.

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : PS->Inventory)
    {
        FFableItem Copy = Item;
        if (bIgnoreWear)
        {
            Copy.Durability = Copy.MaxDurability;
        }
        Value += TEXT(";") + FableRPG::ItemPersistentSignature(Copy);
    }

    Value += FString::Printf(TEXT("|completion=%d,%d,%d"), PS->DungeonCompletions, uint8(PS->MoonwellQuest),
                             PS->MoonwellCompletions);

    // 직업·스킬·특성 빌드 순회
    for (const FFableClassProgress& Progress : PS->ClassProgress)
    {
        Value += TEXT("|tier=") + FString::FromInt(Progress.Tier);
        for (int32 Rank : Progress.Ranks)
        {
            Value += TEXT(",") + FString::FromInt(Rank);
        }
        Value += FString::Printf(TEXT("|trials=%d,%d,%d;%d,%d,%d"), uint8(Progress.SecondTrial.Stage),
                                 Progress.SecondTrial.ValidSkillCasts, Progress.SecondTrial.bLegacyGranted,
                                 uint8(Progress.ThirdTrial.Stage), Progress.ThirdTrial.ValidSkillCasts,
                                 Progress.ThirdTrial.bLegacyGranted);
    }

    Value += FString::Printf(TEXT("|v6:materials=%d,%d,%d|stash"), PS->Essence, PS->RootCores, PS->MoonCrystals);
    // Stored equipment cannot wear during combat, so even its current durability stays exact.

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : PS->StashItems)
    {
        Value += TEXT(";") + FableRPG::ItemPersistentSignature(Item);
    }

    // 직업·스킬·특성 빌드 처리
    Value += FString::Printf(TEXT("|v7:story=%d,%d,%d"), PS->StoryChapter, PS->StoryObjectMask, PS->StorySealMask);
    Value += TEXT("|v8:talents=") + PS->GetTalentSignature();

    return Value;
}

/** 협동 검증에 필요한 직업·퀘스트·전투 상태를 준비한다. */
void UFableCoopValidationDriver::TickSetup()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    if (!GS)
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    const EFableClass Wanted = Role == TEXT("host") ? EFableClass::Mage : EFableClass::Archer;
    const bool bRoot = Scenario == TEXT("root-boss-wipe"), bMoon = Scenario == TEXT("moon-seal-wipe");
    const EFableRegion Region = bRoot   ? EFableRegion::RootRuins
                                : bMoon ? EFableRegion::Moonwell
                                        : EFableRegion::Village;

    // 지역·적·전투 진행 처리
    if (Step == 0)
    {
        if (GS.Players().Num() != (bSolo ? 1 : 2))
        {
            return;
        }
        InitialRecovery = GS->PartyRecoveryCount;
        InitialSerial = GS->EncounterSerial;
        Advance(1);
        return;
    }

    // 퀘스트와 이야기 진행 처리
    if (Step == 1)
    {
        // 위치·방향·거리 계산 처리
        if (PS->HeroClass != Wanted)
        {
            const FVector Board = FableRPG::BoardLocation() + FVector(Role == TEXT("host") ? -100 : 100, -120, 100);
            if (WalkTo(Board, 90) && Now - LastAction > .8)
            {
                PC->UIChooseClass(Wanted);
                LastAction = Now;
            }
            return;
        }

        // 퀘스트와 이야기 진행에 필요한 값 준비
        const EFableQuestId Quest = bRoot   ? EFableQuestId::RootRuins
                                    : bMoon ? EFableQuestId::Moonwell
                                            : EFableQuestId::Forest;

        // 퀘스트와 이야기 진행 조건 검사
        if (PS->GetQuestStage(Quest) != EFableQuestStage::Active)
        {
            SelectQuest(Quest);
            return;
        }

        PC->UIClosePanels();

        // 지역·적·전투 진행 조건 검사
        if (!PartyEligible(Region))
        {
            return;
        }

        Advance(2);

        return;
    }

    // 지역·적·전투 진행 처리
    if (Step == 2)
    {
        // 지역·적·전투 진행 조건 검사
        if (Region != EFableRegion::Village && GS->ActiveRegion != Region)
        {
            EnterRegion(Region, false);
            return;
        }
        if (GS->RequiredEnemies < 1)
        {
            return;
        }

        // 지역·적·전투 진행 처리
        InitialSerial = GS->EncounterSerial;
        BeforeWipeSignature = Signature(PS, true);

        // 이 단계에서 협동 검증에 필요한 직업·퀘스트·전투 상태를 준비한다.
        if (bRoot)
        {
            Advance(20);
        }
        else if (bMoon)
        {
            Advance(30);
        }
        else if (Scenario == TEXT("revive") || Scenario == TEXT("disconnect"))
        {
            Advance(10);
        }
        else if (Scenario == TEXT("timeout"))
        {
            Advance(11);
        }
        else
        {
            Advance(40);
        }
    }
}

/** 동료 구조와 부활 시전의 검증 단계를 진행한다. */
void UFableCoopValidationDriver::TickRescue()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFableCharacter* Other = Partner();
    AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    if (!GS)
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds(), Wait = Now - StepAt;
    AFableCharacter* Fallen = Role == TEXT("host") ? Hero : Other;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Fallen)
    {
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Hero->GetReviveProtectionRemaining() > 0 || Fallen->GetReviveProtectionRemaining() > 0)
    {
        Checks.Add(TEXT("protection_seen"), true);
    }

    // 지역·적·전투 진행 처리
    if (Step == 10)
    {
        if (EnemyCount() == 0)
        {
            Finish(false, TEXT("Preparation killed the last enemy needed for real damage."));
            return;
        }
        if (EnemyCount() == 1)
        {
            Advance(11);
            return;
        }
        if (Role == TEXT("guest"))
        {
            Fight();
        }
        else
        {
            if (Substep == 0)
            {
                if (WalkTo(FVector(-750, -1500, 100), 90))
                {
                    Substep = 1;
                }
            }
            else
            {
                WalkTo(FVector(-750, -2100, 100), 90);
            }
        }
        return;
    }

    // 위치·방향·거리 계산 처리
    if (Step == 11)
    {
        if (Role == TEXT("guest"))
        {
            if (Substep == 0)
            {
                if (WalkTo(FVector(450, -1500, 100), 90))
                {
                    Substep = 1;
                }
            }
            else if (WalkTo(FVector(450, -2100, 100), 80))
            {
                Advance(12);
            }
        }
        else if (PeerStep() >= 12)
        {
            Advance(12);
        }
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Step == 12)
    {
        if (Fallen->IsDowned())
        {
            // 생존 상태와 부활 진행 처리
            Checks.Add(TEXT("downed_seen"), true);
            DownedOrigin = Fallen->GetActorLocation();
            RescueApproachPoint = DownedOrigin;

            // 생존 상태와 부활 진행 처리
            if (Role == TEXT("guest"))
            {
                if (AFableEnemy* Blocker = NearestEnemy())
                {
                    // Walk around the guard standing over the fallen ally.
                    // A straight line stops at its capsule before revive range.
                    // 생존 상태와 부활 진행에 필요한 값 준비
                    const FVector Axis = (Blocker->GetActorLocation() - DownedOrigin).GetSafeNormal2D();
                    const FVector Side(-Axis.Y, Axis.X, 0);
                    const FVector Middle = (Blocker->GetActorLocation() + DownedOrigin) * .5f;
                    FVector A = Middle + Side * 160, B = Middle - Side * 160;
                    A = DownedOrigin + (A - DownedOrigin).GetClampedToMaxSize2D(195);
                    B = DownedOrigin + (B - DownedOrigin).GetClampedToMaxSize2D(195);
                    RescueApproachPoint = FVector::DistSquared2D(Hero->GetActorLocation(), A) <
                                                  FVector::DistSquared2D(Hero->GetActorLocation(), B)
                                              ? A
                                              : B;
                }
            }

            // 직업·스킬·특성 빌드 처리
            Capture(TEXT("ally-downed"));
            BeforePotion = PS->HealthPotions;
            BeforeCasts = Hero->GetSkillCastCount(0);
            Advance(13);

            return;
        }
        if (Role == TEXT("host"))
        {
            // Lure the real guard into the open clearing before waiting for
            // its attacks. The quest board blocks a direct rescue approach
            // when TakeHits chases the guard back through the village services.
            if (Substep == 0)
            {
                if (WalkTo(FVector(-750, -1500, 100), 90))
                {
                    Substep = 1;
                }
            }
            else if (Substep == 1)
            {
                if (WalkTo(FVector(450, -1500, 100), 90))
                {
                    Substep = 2;
                }
            }
            else if (Substep == 2)
            {
                if (WalkTo(FVector(450, 0, 100), 75))
                {
                    Substep = 3;
                }
            }
            else
            {
                Stop();
            }
        }
        else
        {
            Stop();
        }
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Role == TEXT("host"))
    {
        Stop();

        // 직업·스킬·특성 빌드 처리
        if (Step == 13 && !Checks.FindRef(TEXT("downed_actions_blocked")))
        {
            if (Wait < .4)
            {
                Key(EKeys::J);
                Key(EKeys::K);
                PC->UIUseHealthPotion();
            }
            else if (Wait > .9)
            {
                if (Hero->GetSkillCastCount(0) != BeforeCasts || PS->HealthPotions != BeforePotion || Hero->IsAlive())
                {
                    Finish(false, TEXT("A downed hero attacked, cast or consumed a potion."));
                    return;
                }
                Checks.Add(TEXT("downed_actions_blocked"), true);
            }
        }

        // 생존 상태와 부활 진행 처리
        if (PeerStep() >= 16 && BeforeReviveSignature.IsEmpty())
        {
            BeforeReviveSignature = Signature(PS, true);
        }

        // 생존 상태와 부활 진행 조건 검사
        if (Scenario == TEXT("disconnect") && GS.Players().Num() == 1 && GS->PartyRecoveryCount > InitialRecovery &&
            Hero->IsAlive())
        {
            Checks.Add(TEXT("disconnect_recovered"), !Hero->GetReviver() && !Hero->GetReviveTarget());
            Capture(TEXT("disconnect-rescued"));
            Advance(60);
            return;
        }

        // 생존 상태와 부활 진행 처리
        if (Hero->IsAlive() && Hero->GetTimesRevivedCount() > 0)
        {
            // 생존 상태와 부활 진행 조건 검사
            if (FVector::Dist2D(Hero->GetActorLocation(), DownedOrigin) >= 80 ||
                Hero->GetHealth() > Hero->GetMaxHealth() * .41f || !Checks.FindRef(TEXT("protection_seen")))
            {
                Finish(false, TEXT("Revive did not restore 40 percent in place with protection."));
                return;
            }

            // 생존 상태와 부활 진행 처리
            Checks.Add(TEXT("revived_in_place"), true);
            Checks.Add(TEXT("revive_no_reward"),
                       !BeforeReviveSignature.IsEmpty() && Signature(PS, true) == BeforeReviveSignature);
            Capture(TEXT("revived-in-place"));
            Advance(60);

            return;
        }

        // 생존 상태와 부활 진행 처리
        if ((Scenario != TEXT("disconnect") || (GS.Players().Num() == 2 && !GS->bPartyRecoveryPending)) &&
            (Fallen->IsDefeated() || (Fallen->IsAlive() && Hero->GetTimesRevivedCount() == 0)))
        {
            Finish(false, TEXT("Revive window expired or automatic recovery replaced ally revival."));
        }

        return;
    }

    // 생존 상태와 부활 진행 조건 검사
    if (!Other || (!Fallen->IsDowned() && Step < 19))
    {
        Finish(false, TEXT("Fallen ally left the rescue window too soon."));
        return;
    }

    // 이동 입력과 속도 제한 처리
    if (Step == 13)
    {
        // 생존 상태와 부활 진행 조건 검사
        if (!bAttemptedRange && FVector::Dist2D(Hero->GetActorLocation(), Fallen->GetActorLocation()) > 350)
        {
            PC->UIStartRevive(Fallen);
            bAttemptedRange = true;
            SubAt = Now;
            return;
        }

        // 생존 상태와 부활 진행 처리
        if (bAttemptedRange && !Checks.FindRef(TEXT("range_rejected")))
        {
            if (Now - SubAt < .4)
            {
                return;
            }
            Checks.Add(TEXT("range_rejected"), Hero->GetReviveTarget() == nullptr);
        }

        // 이동 입력과 속도 제한 조건 검사
        if (!WalkTo(RescueApproachPoint, 35) || Hero->GetVelocity().Size2D() > 10)
        {
            SubAt = Now;
            return;
        }
        if (Now - SubAt < .45)
        {
            return;
        }

        // 생존 상태와 부활 진행 처리
        PC->SetControlRotation(
            FRotator(-12, (Fallen->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
        BeforeCancel = Hero->GetReviveCancelCount();
        HealthBeforeRevive = Hero->GetHealth();
        Capture(TEXT("revive-under-attack"));
        Advance(14);
        Hold(EKeys::E, true);

        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Step == 14)
    {
        Hold(EKeys::E, true);
        if (Hero->GetReviveCancelCount() > BeforeCancel && !Hero->GetReviveTarget() &&
            Hero->GetHealth() < HealthBeforeRevive)
        {
            Checks.Add(TEXT("damage_cancelled"), true);
            Hold(EKeys::E, false);
            Advance(15);
        }
        else if (Wait > 4)
        {
            Finish(false, TEXT("Enemy damage did not interrupt revival."));
        }
        return;
    }

    // 이동 입력과 속도 제한 처리
    if (Step == 15)
    {
        // 지역·적·전투 진행 조건 검사
        if (EnemyCount() > 0)
        {
            Fight();
            return;
        }

        Stop();

        // 이동 입력과 속도 제한 조건 검사
        if (Hero->GetVelocity().Size2D() > 10)
        {
            return;
        }

        // 생존 상태와 부활 진행 처리
        Advance(16);
        PC->UIStartRevive(Hero);

        return;
    }

    // 이동 입력과 속도 제한 처리
    if (Step == 16)
    {
        // 생존 상태와 부활 진행 조건 검사
        if (Wait < .4)
        {
            return;
        }
        if (Hero->GetReviveTarget())
        {
            Finish(false, TEXT("Self revival was accepted."));
            return;
        }

        Checks.Add(TEXT("self_rejected"), true);

        // 이동 입력과 속도 제한 조건 검사
        if (!WalkTo(Fallen->GetActorLocation(), 155) || Hero->GetVelocity().Size2D() > 10)
        {
            return;
        }

        // 생존 상태와 부활 진행 처리
        BeforeReviveSignature = Signature(PS, true);
        BeforeCancel = Hero->GetReviveCancelCount();
        Advance(17);
        Hold(EKeys::E, true);

        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    if (Step == 17)
    {
        Stop();

        // 검증 입력과 키 유지 상태 처리
        if (Substep < 2)
        {
            Hold(EKeys::E, true);
        }

        // 생존 상태와 부활 진행 처리
        if (Substep == 0 && Hero->GetReviveProgress() > .16f)
        {
            Capture(TEXT("revive-progress"));
            Substep = 1;
            SubAt = Now;
        }

        // 검증 입력과 키 유지 상태 처리
        if (Substep == 1 && Now - SubAt > .5)
        {
            Hold(EKeys::E, false);
            Substep = 2;
            SubAt = Now;
        }

        // 접속 권한·저장 리비전·멱등 키 처리
        if (Substep == 2 && Now - SubAt > .4)
        {
            if (Hero->GetReviveCancelCount() <= BeforeCancel || Hero->GetReviveTarget())
            {
                Finish(false, TEXT("E release did not cancel revival."));
                return;
            }
            Checks.Add(TEXT("release_cancelled"), true);
            BeforeCancel = Hero->GetReviveCancelCount();
            Advance(18);
        }

        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Step == 18)
    {
        if (Scenario == TEXT("disconnect"))
        {
            Advance(70);
            return;
        }
        if (Substep == 0)
        {
            Hold(EKeys::E, true);
            if (Hero->GetReviveProgress() > .1f)
            {
                Substep = 1;
                SubAt = Now;
                Hold(EKeys::S, true);
            }
        }
        else if (Substep == 1)
        {
            Hold(EKeys::E, true);
            Hold(EKeys::S, true);
            if (Hero->GetReviveCancelCount() > BeforeCancel)
            {
                Stop();
                Hold(EKeys::E, false);
                Checks.Add(TEXT("movement_cancelled"), true);
                Advance(19);
            }
            else if (Now - SubAt > 1)
            {
                Finish(false, TEXT("Movement did not cancel revival."));
            }
        }
        return;
    }

    // 이동 입력과 속도 제한 처리
    if (Step == 19)
    {
        if (!Fallen->IsDowned())
        {
            if (Fallen->IsAlive() && Hero->GetRevivesPerformedCount() > 0)
            {
                Checks.Add(TEXT("revive_completed"), true);
                Checks.Add(TEXT("revive_no_reward"), Signature(PS, true) == BeforeReviveSignature);
                Capture(TEXT("ally-revived"));
                Advance(60);
            }
            else
            {
                Finish(false, TEXT("Ally expired before the final revival."));
            }
            return;
        }
        if (!WalkTo(Fallen->GetActorLocation(), 150) || Hero->GetVelocity().Size2D() > 10)
        {
            return;
        }
        if (Substep == 0)
        {
            BeforeCancel = Hero->GetReviveCancelCount();
            Hold(EKeys::E, true);
            if (Hero->GetReviveProgress() > .1f)
            {
                Key(EKeys::Q);
                Substep = 1;
                SubAt = Now;
            }
        }
        else if (Substep == 1 && Now - SubAt > .4)
        {
            if (Hero->GetReviveCancelCount() <= BeforeCancel || Hero->GetReviveTarget())
            {
                Finish(false, TEXT("Opening the quest panel did not cancel revival."));
                return;
            }
            Checks.Add(TEXT("panel_cancelled"), true);
            Hold(EKeys::E, false);
            PC->UIClosePanels();
            Substep = 2;
            SubAt = Now;
        }
        else if (Substep == 2 && Now - SubAt > .5)
        {
            Hold(EKeys::E, true);
        }
        return;
    }

    // 체력·마나·보호막·피해량 처리
    if (Step == 71)
    {
        if (IFileManager::Get().FileExists(*FPaths::Combine(Directory, TEXT("guest-disconnect-approved.txt"))))
        {
            // Use the actual menu action so NetConnection closes normally; an
            // OS kill would only become visible after the UDP timeout.
            bDone = true;
            PC->UIReturnToFrontEnd();
        }
        return;
    }

    // 이동 입력과 속도 제한 처리
    if (Step == 70)
    {
        if (!WalkTo(Fallen->GetActorLocation(), 150) || Hero->GetVelocity().Size2D() > 10)
        {
            return;
        }
        Hold(EKeys::E, true);
        if (Hero->GetReviveProgress() > .2f)
        {
            Checks.Add(TEXT("ready_for_disconnect"), true);
            Report(TEXT("disconnect_ready"),
                   TEXT("Authorize this guest to leave through the actual start-menu action while revival is active."));
            Step = 71;
            StepAt = Now;
        }
    }
}

/** 구조 제한 시간 만료의 검증 단계를 진행한다. */
void UFableCoopValidationDriver::TickTimeout()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFableCharacter* Fallen = Role == TEXT("host") ? Hero : Partner();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    if (!GS)
    {
        return;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Fallen)
    {
        return;
    }

    // 위치·방향·거리 계산 처리
    if (Step == 11)
    {
        if (Role == TEXT("guest"))
        {
            if (Substep == 0)
            {
                if (WalkTo(FVector(450, -1500, 100), 90))
                {
                    Substep = 1;
                }
            }
            else if (WalkTo(FVector(450, -2100, 100), 80))
            {
                Advance(12);
            }
        }
        else if (PeerStep() >= 12)
        {
            Advance(12);
        }
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Step == 12)
    {
        if (Role == TEXT("host"))
        {
            TakeHits();
        }
        else
        {
            Stop();
        }
        if (Fallen->IsDowned())
        {
            BeforeWipeSignature = Signature(PC->GetPlayerState<AFablePlayerState>(), true);
            Checks.Add(TEXT("downed_seen"), true);
            Capture(TEXT("bleedout-wait"));
            Advance(13);
        }
        return;
    }

    Stop();

    // 생존 상태와 부활 진행 처리
    if (Fallen->IsDefeated())
    {
        Checks.Add(TEXT("bleedout_seen"), true);
    }

    // 지역·적·전투 진행 처리
    if (Fallen->IsAlive() && Checks.FindRef(TEXT("bleedout_seen")))
    {
        if (Fallen->GetActorLocation().Y >= -800 || GS->PartyRecoveryCount != InitialRecovery || EnemyCount() != 3 ||
            Signature(PC->GetPlayerState<AFablePlayerState>(), true) != BeforeWipeSignature)
        {
            Finish(false, TEXT("Timed-out ally rescue reset the surviving party or changed rewards."));
            return;
        }
        Checks.Add(TEXT("individual_rescue"), true);
        Capture(TEXT("individual-rescued"));
        Advance(60);
    }
}

/** 파티 전멸과 복구 절차의 검증 단계를 진행한다. */
void UFableCoopValidationDriver::TickWipe()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    if (!GS)
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();
    const bool bRoot = Scenario == TEXT("root-boss-wipe"), bMoon = Scenario == TEXT("moon-seal-wipe");
    const EFableRegion Region = bRoot   ? EFableRegion::RootRuins
                                : bMoon ? EFableRegion::Moonwell
                                        : EFableRegion::Village;

    // 지역·적·전투 진행 처리
    if (Step == 20)
    {
        if (GS->AdventureStage != EFableAdventureStage::Boss)
        {
            Fight();
            return;
        }
        if (AFableEnemy* Boss = NearestEnemy(true))
        {
            Stop();
            if (Boss->GetHealth() < Boss->GetMaxHealth() && Boss->GetHealth() > 0)
            {
                Checks.Add(TEXT("boss_damaged_before_wipe"), true);
                BeforeWipeSignature = Signature(PS, true);
                Advance(22);
                return;
            }
            if (Role == TEXT("host") && Now - LastAction > 1)
            {
                PC->SetControlRotation(
                    FRotator(-12, (Boss->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
                Key(EKeys::J);
                LastAction = Now;
                ++AttackInputs;
            }
        }
        return;
    }

    // 지역·적·전투 진행 처리
    if (Step == 30)
    {
        if (GS->MoonSealMask & 1)
        {
            Checks.Add(TEXT("seal_completed_before_wipe"), true);
            BeforeWipeSignature = Signature(PS, true);
            Advance(22);
            return;
        }
        if (EnemyCount() > 0)
        {
            Fight();
            return;
        }
        if (WalkTo(FableAdventure::For(PC.Get()).ToWorld(EFableRegion::Moonwell, FableRPG::MoonSealLocation(0)), 180) &&
            Now - LastAction > .8)
        {
            Key(EKeys::E);
            LastAction = Now;
        }
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Step == 22 || Step == 40)
    {
        // 이 단계에서 파티 전멸과 복구 절차의 검증 단계를 진행한다.
        if (GS->bPartyRecoveryPending && !Checks.FindRef(TEXT("wipe_countdown_seen")))
        {
            Checks.Add(TEXT("wipe_countdown_seen"), true);
            Capture(TEXT("party-wipe-countdown"));
        }

        // 생존 상태와 부활 진행 처리
        if (Hero->IsDowned())
        {
            Checks.Add(TEXT("downed_seen"), true);
        }

        // 지역·적·전투 진행 처리
        if (GS->PartyRecoveryCount > InitialRecovery && Hero->IsAlive() && GS->ActiveRegion == EFableRegion::Village)
        {
            // 지역·적·전투 진행 조건 검사
            if (Signature(PS, true) != BeforeWipeSignature || EnemyCount() != 0 || GS->BossHealth != 0 ||
                GS->MoonSealMask != 0 || !GS->bRetryAvailable || GS->RetryRegion != Region)
            {
                Finish(false, TEXT("Party rescue lost progression or kept old encounter state."));
                return;
            }

            Checks.Add(TEXT("wipe_preserved_profile"), true);
            Checks.Add(TEXT("runtime_reset"), true);
            Capture(TEXT("party-rescued"));
            Advance(24);

            return;
        }

        // 검증 입력과 키 유지 상태 처리
        if (!GS->bPartyRecoveryPending)
        {
            TakeHits();
        }
        else
        {
            Stop();
        }

        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Step == 24)
    {
        if (GS->EncounterSerial > InitialSerial && GS->ActiveRegion == Region && GS->RequiredEnemies == 3 &&
            EnemyCount() == 3)
        {
            if (GS->DefeatedEnemies != 0 || GS->MoonSealMask != 0 || GS->BossHealth != 0 || !EnemiesAreFresh())
            {
                Finish(false, TEXT("Retry did not start with fresh enemies, seals and boss state."));
                return;
            }
            Checks.Add(TEXT("retry_fresh"), true);
            Capture(TEXT("retry-fresh-encounter"));
            Advance(bRoot ? 25 : 60);
            return;
        }
        EnterRegion(Region, true);
        return;
    }

    // 지역·적·전투 진행 처리
    if (Step == 25)
    {
        if (PeerStep() < 25)
        {
            Stop();
            return;
        }
        if (GS->AdventureStage == EFableAdventureStage::Boss)
        {
            if (AFableEnemy* Boss = NearestEnemy(true))
            {
                if (!FMath::IsNearlyEqual(Boss->GetHealth(), Boss->GetMaxHealth(), .1f))
                {
                    Finish(false, TEXT("Retried boss retained previous damage."));
                    return;
                }
                Checks.Add(TEXT("boss_reset_full"), true);
                Capture(TEXT("retried-boss-full-health"));
                Advance(60);
            }
        }
        else
        {
            Fight();
        }
    }
}

/** 던전 완료와 보상 유지의 검증 단계를 진행한다. */
void UFableCoopValidationDriver::TickCompletion()
{
    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    if (!GS)
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    // 검증 입력과 키 유지 상태 처리
    if (Phase == TEXT("resume"))
    {
        Stop();
        Hold(EKeys::E, false);
        if (Now - StepAt > 1.5)
        {
            Finish(true, TEXT("The complete saved profile restored exactly in new processes."));
        }
        return;
    }

    // 생존 상태와 부활 진행에 필요한 값 준비
    const auto StopReviving = [&]()
    {
        Hold(EKeys::E, false);
        if (Hero->GetReviveTarget())
        {
            PC->UIStopRevive();
        }
        bCompletionReviveHeld = false;
    };
    const int32 ExpectedPlayers = (bSolo || Scenario == TEXT("disconnect")) ? 1 : 2;
    // Both observers must record fresh enemies / full boss health before cleanup can attack.

    // 검증 입력과 키 유지 상태 조건 검사
    if (GS.Players().Num() != ExpectedPlayers || (ExpectedPlayers == 2 && PeerStep() < 60))
    {
        CompletionQuietSince = -1;
        Stop();
        StopReviving();
        return;
    }

    // 지역·적·전투 진행 처리
    if (EnemyCount() > 0)
    {
        CompletionQuietSince = -1;
        StopReviving();
        // Ordinary attacks may award ordinary rewards and cause wear. Snapshot only afterwards.
        // No seal interaction is sent, so Moonwell stops after the current defenders.
        if (Hero->IsAlive() && !GS->bPartyRecoveryPending)
        {
            Fight();
        }
        else
        {
            Stop();
        }
        return;
    }

    // 지역·적·전투 진행 처리
    Stop();
    bool bHasEnemyBolt = false;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemyBolt> It(GetWorld()); It; ++It)
    {
        // 자기 원정과 같은 UUID의 대상만 관찰한다.
        if (!FableAdventure::SameScope(PC.Get(), *It))
        {
            continue;
        }

        bHasEnemyBolt = true;
        break;
    }

    // 지역·적·전투 진행 조건 검사
    if (bHasEnemyBolt || GS->bPartyRecoveryPending || !Hero->IsAlive())
    {
        CompletionQuietSince = -1;
        StopReviving();
        return;
    }

    AFableCharacter* Other = Partner();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (ExpectedPlayers == 2 && !Other)
    {
        CompletionQuietSince = -1;
        StopReviving();
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    if (Other && Other->IsDowned())
    {
        CompletionQuietSince = -1;

        // 이 단계에서 던전 완료와 보상 유지의 검증 단계를 진행한다.
        if (PC->IsAnyPanelOpen())
        {
            PC->UIClosePanels();
        }

        // 생존 상태와 부활 진행 조건 검사
        if (Hero->GetReviveTarget() == Other)
        {
            Hold(EKeys::E, true);
            return;
        }
        // A rejected/interrupted channel needs release and a fresh press, not a permanent hold.

        // 생존 상태와 부활 진행 처리
        if (bCompletionReviveHeld)
        {
            if (Now - CompletionRevivePressedAt > .6)
            {
                StopReviving();
                CompletionReviveRetryAt = Now + .4;
            }
            return;
        }

        // 이동 입력과 속도 제한 조건 검사
        if (!WalkTo(Other->GetActorLocation(), 175) || Hero->GetVelocity().Size2D() > 10)
        {
            CompletionReviveRetryAt = Now + .35;
            return;
        }
        if (Now < CompletionReviveRetryAt)
        {
            return;
        }

        // 생존 상태와 부활 진행 처리
        PC->SetControlRotation(FRotator(-12, (Other->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
        Hold(EKeys::E, true);
        bCompletionReviveHeld = true;
        CompletionRevivePressedAt = Now;

        return;
    }

    StopReviving();
    // Expired allies use the existing individual rescue. Never manufacture life/HP or a save.

    // 생존 상태와 부활 진행 조건 검사
    if ((Other && (!Other->IsAlive() || Other->GetReviveTarget() || Other->GetReviver())) || Hero->GetReviveTarget() ||
        Hero->GetReviver())
    {
        CompletionQuietSince = -1;
        return;
    }

    // 이 단계에서 던전 완료와 보상 유지의 검증 단계를 진행한다.
    if (CompletionQuietSince < 0)
    {
        CompletionQuietSince = Now;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Now - CompletionQuietSince < 6.0)
    {
        return;
    }

    Checks.Add(TEXT("final_safe_checkpoint"), true);
    Finish(
        true,
        TEXT(
            "Recovery assertions passed. The final checkpoint has no live enemies or hostile shots after six quiet seconds, including ordinary combat rewards and wear in the exact saved profile."));
}

/** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
void UFableCoopValidationDriver::TickComponent(float DeltaTime, ELevelTick TickType,
                                               FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime, TickType, Function);

    // JSON 계약과 검증 기록 조건 검사
    if (bDone)
    {
        return;
    }
    // Once a result is captured, only retry publication. Never resume scenario
    // input or replace its final snapshot with a periodic running report.
    if (bTerminalReportPending)
    {
        PublishTerminalReport();
        return;
    }
    if (!PC.IsValid())
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    Release();
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());

    // 프로필과 저장 스냅숏 조건 검사
    if (!Hero || !PS || !PS->bProfileLoaded || !GS)
    {
        return;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    // 위치·방향·거리 계산 처리
    if (!PreviousLocation.IsZero())
    {
        Movement += FVector::Dist2D(PreviousLocation, Hero->GetActorLocation());
    }

    PreviousLocation = Hero->GetActorLocation();

    // JSON 계약과 검증 기록 처리
    if (Now - LastReport > .5 && Step != 71)
    {
        Report(TEXT("running"), TEXT("Normal-input co-op recovery validation in progress."));
        LastReport = Now;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Now - StartedAt > 650 || Now - StepAt > 200)
    {
        Capture(TEXT("recovery-stuck"));
        Finish(false, FString::Printf(TEXT("Recovery scenario %s timed out at step %d."), *Scenario, Step));
        return;
    }
    if (Step == 60)
    {
        TickCompletion();
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (Phase == TEXT("resume"))
    {
        // 생존 상태와 부활 진행 조건 검사
        if (Now - StepAt < 4 || GS.Players().Num() != (bSolo ? 1 : 2))
        {
            return;
        }
        if (!Hero->IsAlive() || Hero->IsDowned() || Hero->IsDefeated() || Hero->GetReviveTarget() ||
            Hero->GetReviver() || Hero->GetDownedTimeRemaining() != 0 || Hero->GetReviveProgress() != 0 ||
            GS->bPartyRecoveryPending || GS->bRetryAvailable || GS->ActiveRegion != EFableRegion::Village)
        {
            Finish(false, TEXT("Temporary downed, revival or retry state leaked into a new session."));
            return;
        }

        Checks.Add(TEXT("runtime_not_persisted"), true);
        Capture(TEXT("restored-active"));
        Advance(60);

        return;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Step <= 2)
    {
        TickSetup();
        return;
    }

    // 서버 시각과 재사용 대기시간 처리
    if (Scenario == TEXT("timeout"))
    {
        TickTimeout();
    }
    else if (Scenario == TEXT("revive") || Scenario == TEXT("disconnect"))
    {
        TickRescue();
    }
    else
    {
        TickWipe();
    }
}

/** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
void UFableCoopValidationDriver::Finish(bool bSuccess, const FString& Reason)
{
    // JSON 계약과 검증 기록 조건 검사
    if (bDone || bTerminalReportPending)
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    Stop();
    Hold(EKeys::E, false);
    Release();
    TerminalReportStatus = bSuccess ? TEXT("passed") : TEXT("failed");
    TerminalReportReason = Reason;
    // Serialize once: retries must not recapture later gameplay, notices or wear.
    TerminalReportText = SerializeReport(TerminalReportStatus, TerminalReportReason);

    // JSON 계약과 검증 기록 처리
    if (TerminalReportText.IsEmpty())
    {
        bDone = true;
        UE_LOG(
            LogTemp, Error,
            TEXT(
                "FABLE_COOP_VALIDATION_REPORT_IO_FAILED role=%s scenario=%s phase=%s attempts=0 reason=serialization_failed"),
            *Role, *Scenario, *Phase);
        return;
    }

    // JSON 계약과 검증 기록 처리
    TerminalReportStartedAt = FPlatformTime::Seconds();
    NextTerminalReportAttempt = TerminalReportStartedAt;
    TerminalReportAttempts = 0;
    TerminalReportLastError = 0;
    bTerminalReportPending = true;
    PublishTerminalReport();
}

/** 종료 보고서 결과를 보고서에 기록한다. */
void UFableCoopValidationDriver::PublishTerminalReport()
{
    // JSON 계약과 검증 기록 조건 검사
    if (!bTerminalReportPending || bDone)
    {
        return;
    }

    // 서버 시각과 재사용 대기시간에 필요한 값 준비
    const double Now = FPlatformTime::Seconds();
    const FString Path = FPaths::Combine(Directory, Role + TEXT(".json"));

    // JSON 계약과 검증 기록 처리
    if (Now - TerminalReportStartedAt >= 30.0)
    {
        bTerminalReportPending = false;
        bDone = true;
        // No terminal RESULT is emitted without a confirmed native report write.
        UE_LOG(
            LogTemp, Error,
            TEXT(
                "FABLE_COOP_VALIDATION_REPORT_IO_FAILED role=%s scenario=%s phase=%s attempts=%d last_error=%u path=%s reason=publication_timeout"),
            *Role, *Scenario, *Phase, TerminalReportAttempts, TerminalReportLastError, *Path);
        return;
    }

    // JSON 계약과 검증 기록 조건 검사
    if (Now < NextTerminalReportAttempt)
    {
        return;
    }

    ++TerminalReportAttempts;

    // JSON 계약과 검증 기록 처리
    if (FFileHelper::SaveStringToFile(TerminalReportText, *Path))
    {
        bTerminalReportPending = false;
        bDone = true;
        UE_LOG(LogTemp, Display,
               TEXT("FABLE_COOP_VALIDATION_RESULT role=%s scenario=%s status=%s attempts=%d reason=%s"), *Role,
               *Scenario, *TerminalReportStatus, TerminalReportAttempts, *TerminalReportReason);
        return;
    }

    // JSON 계약과 검증 기록 처리
    TerminalReportLastError = FPlatformMisc::GetLastError();
    NextTerminalReportAttempt = Now + 0.2;

    // JSON 계약과 검증 기록 처리
    if (TerminalReportAttempts == 1 || TerminalReportAttempts % 25 == 0)
    {
        UE_LOG(
            LogTemp, Warning,
            TEXT("FABLE_COOP_VALIDATION_REPORT_RETRY role=%s scenario=%s phase=%s attempts=%d last_error=%u path=%s"),
            *Role, *Scenario, *Phase, TerminalReportAttempts, TerminalReportLastError, *Path);
    }
}

/** 현재 검증 상태와 수집한 근거를 보고서로 기록한다. */
void UFableCoopValidationDriver::Report(const FString& Status, const FString& Reason)
{
    // JSON 계약과 검증 기록 조건 검사
    if (bDone || bTerminalReportPending)
    {
        return;
    }

    const FString Text = SerializeReport(Status, Reason);
    // Nonterminal observations naturally refresh on the next periodic report.

    // 이 단계에서 현재 검증 상태와 수집한 근거를 보고서로 기록한다.
    if (!Text.IsEmpty())
    {
        FFileHelper::SaveStringToFile(Text, *FPaths::Combine(Directory, Role + TEXT(".json")));
    }
}

/** 현재 관찰 값과 검증 결과를 JSON 보고서로 직렬화한다. */
FString UFableCoopValidationDriver::SerializeReport(const FString& Status, const FString& Reason)
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (!PC.IsValid() || !GetWorld())
    {
        return FString();
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    const AFablePlayerState* PS = PC->GetPlayerState<AFablePlayerState>();
    const AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    const FFableAdventureView GS = FableAdventure::For(PC.Get());
    TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
    J->SetBoolField(TEXT("report_snapshot_frozen"), Status == TEXT("passed") || Status == TEXT("failed"));
    J->SetStringField(TEXT("run_id"), RunId);
    J->SetStringField(TEXT("role"), Role);
    J->SetStringField(TEXT("scenario"), Scenario);
    J->SetStringField(TEXT("phase"), Phase);
    J->SetStringField(TEXT("status"), Status);
    J->SetStringField(TEXT("reason"), Reason);
    J->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
    J->SetStringField(TEXT("network_mode"), GetWorld()->GetNetMode() == NM_ListenServer ? TEXT("listen_server")
                                            : GetWorld()->GetNetMode() == NM_Standalone ? TEXT("standalone")
                                                                                        : TEXT("client"));
    J->SetNumberField(TEXT("step"), Step);
    J->SetNumberField(TEXT("substep"), Substep);
    J->SetNumberField(TEXT("player_count"), GS ? GS.Players().Num() : 0);
    J->SetNumberField(TEXT("movement_cm"), Movement);
    J->SetNumberField(TEXT("attack_inputs"), AttackInputs);
    J->SetStringField(TEXT("profile_signature"), Signature(PS));
    J->SetBoolField(TEXT("save_succeeded"), PS && PS->bLastSaveSucceeded);
#if !UE_BUILD_SHIPPING
    J->SetStringField(TEXT("profile_slot"), PS ? PS->GetProfileSlotForValidation() : TEXT(""));
#endif

    // 재화와 작업 비용 처리
    if (PS)
    {
        J->SetStringField(TEXT("last_notice"), PS->LastNotice);
        J->SetNumberField(TEXT("gold"), PS->Gold);
        J->SetNumberField(TEXT("ore"), PS->Ore);
    }

    // 생존 상태와 부활 진행 처리
    if (Hero)
    {
        // 생존 상태와 부활 진행 처리
        J->SetNumberField(TEXT("life_state"), uint8(Hero->GetLifeState()));
        J->SetNumberField(TEXT("health"), Hero->GetHealth());
        J->SetNumberField(TEXT("max_health"), Hero->GetMaxHealth());
        J->SetNumberField(TEXT("downed_remaining"), Hero->GetDownedTimeRemaining());
        J->SetNumberField(TEXT("revive_progress"), Hero->GetReviveProgress());
        J->SetNumberField(TEXT("downed_count"), Hero->GetDownedCount());
        J->SetNumberField(TEXT("revives_performed"), Hero->GetRevivesPerformedCount());
        J->SetNumberField(TEXT("times_revived"), Hero->GetTimesRevivedCount());
        J->SetNumberField(TEXT("revive_cancels"), Hero->GetReviveCancelCount());
        J->SetBoolField(TEXT("has_revive_target"), Hero->GetReviveTarget() != nullptr);
        J->SetBoolField(TEXT("has_reviver"), Hero->GetReviver() != nullptr);
        J->SetStringField(TEXT("position"), Hero->GetActorLocation().ToString());
        J->SetStringField(TEXT("velocity"), Hero->GetVelocity().ToString());

        // 생존 상태와 부활 진행 처리
        if (const AFableCharacter* Other = Partner())
        {
            J->SetStringField(TEXT("partner_position"), Other->GetActorLocation().ToString());
            J->SetNumberField(TEXT("partner_distance"),
                              FVector::Dist2D(Hero->GetActorLocation(), Other->GetActorLocation()));
            J->SetNumberField(TEXT("partner_life_state"), uint8(Other->GetLifeState()));
        }
    }

    // 지역·적·전투 진행 처리
    if (GS)
    {
        J->SetNumberField(TEXT("active_region"), uint8(GS->ActiveRegion));
        J->SetNumberField(TEXT("adventure_stage"), uint8(GS->AdventureStage));
        J->SetNumberField(TEXT("enemy_count"), EnemyCount());
        J->SetNumberField(TEXT("seal_mask"), GS->MoonSealMask);
        J->SetNumberField(TEXT("boss_health"), GS->BossHealth);
        J->SetBoolField(TEXT("recovery_pending"), GS->bPartyRecoveryPending);
        J->SetBoolField(TEXT("retry_available"), GS->bRetryAvailable);
        J->SetNumberField(TEXT("recovery_count"), GS->PartyRecoveryCount);
        J->SetNumberField(TEXT("encounter_serial"), GS->EncounterSerial);
    }

    int32 EnemyBoltCount = 0;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemyBolt> It(GetWorld()); It; ++It)
    {
        // 자기 원정과 같은 UUID의 대상만 관찰한다.
        if (!FableAdventure::SameScope(PC.Get(), *It))
        {
            continue;
        }

        ++EnemyBoltCount;
    }

    // 지역·적·전투 진행 처리
    J->SetNumberField(TEXT("enemy_bolt_count"), EnemyBoltCount);
    J->SetNumberField(TEXT("final_quiet_seconds"),
                      CompletionQuietSince >= 0 ? FMath::Max(0.0, GetWorld()->GetTimeSeconds() - CompletionQuietSince)
                                                : 0.0);
    TSharedRef<FJsonObject> Flags = MakeShared<FJsonObject>();

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const auto& Item : Checks)
    {
        Flags->SetBoolField(Item.Key, Item.Value);
    }

    // JSON 계약과 검증 기록 처리
    J->SetObjectField(TEXT("checks"), Flags);
    TArray<TSharedPtr<FJsonValue>> Shots;

    // JSON 계약과 검증 기록 순회
    for (const FString& Path : Screenshots)
    {
        Shots.Add(MakeShared<FJsonValueString>(Path));
    }

    J->SetArrayField(TEXT("screenshots"), Shots);
    FString Text;

    // JSON 계약과 검증 기록 조건 검사
    if (!FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text)))
    {
        return FString();
    }

    return Text;
}
