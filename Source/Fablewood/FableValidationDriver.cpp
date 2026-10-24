#include "FableValidationDriver.h"
#include "FableAdventure.h"

#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
UFableValidationDriver::UFableValidationDriver()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

/** 검증 요청 여부를 확인한다. */
bool UFableValidationDriver::IsRequested()
{
#if UE_BUILD_SHIPPING
    return false;
#else
    FString RequestedRole;
    return FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), RequestedRole);
#endif
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void UFableValidationDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
    return;
#else
    Player = Cast<APlayerController>(GetOwner());
    if (!IsRequested() || !Player.IsValid() || !Player->IsLocalController())
    {
        SetComponentTickEnabled(false);
        return;
    }
    StartedAt = FPlatformTime::Seconds();
    FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), Role);
    FParse::Value(FCommandLine::Get(), TEXT("FableValidationDirectory="), Directory);
    FParse::Value(FCommandLine::Get(), TEXT("FableValidationRunId="), RunId);
    Role.ToLowerInline();
    if ((Role != TEXT("host") && Role != TEXT("guest")) || Directory.IsEmpty() || FPaths::IsRelative(Directory))
    {
        UE_LOG(LogTemp, Error, TEXT("FABLE_VALIDATION invalid role or non-absolute output directory"));
        SetComponentTickEnabled(false);
        return;
    }
    FPaths::NormalizeDirectoryName(Directory);
    if (RunId.IsEmpty())
    {
        RunId = FPaths::GetCleanFilename(Directory);
    }
    if (!IFileManager::Get().MakeDirectory(*Directory, true))
    {
        UE_LOG(LogTemp, Error, TEXT("FABLE_VALIDATION cannot create output directory: %s"), *Directory);
        SetComponentTickEnabled(false);
        return;
    }
    // Each process must keep ticking when its window is not focused. No persistent config is changed.
    if (IConsoleVariable* Idle = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground")))
    {
        Idle->Set(0);
    }
    WriteReport();
#endif
}

/** 월드 이탈 시 등록한 이벤트와 진행 중인 작업을 정리한다. */
void UFableValidationDriver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ReleaseInputs();

    // JSON 계약과 검증 기록 처리
    if (!Directory.IsEmpty() && Status == TEXT("running") && StartedAt > 0.0)
    {
        Status = TEXT("failed");
        Failure = TEXT("World ended before all validation conditions completed.");
        WriteReport();
    }

    Super::EndPlay(EndPlayReason);
}

/** 유지 입력 키 값을 설정한다. */
void UFableValidationDriver::SetHeldKey(const FKey& Key, bool bDown)
{
    APlayerController* PC = Player.Get();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!PC || !PC->PlayerInput)
    {
        return;
    }
    // Re-check engine key state: a viewport losing focus may have flushed its pressed keys.

    // 접속 권한·저장 리비전·멱등 키 처리
    if (PC->IsInputKeyDown(Key) != bDown)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.0f : 0.0f, 0,
                                                         FInputDeviceId::CreateFromInternalId(0)));
    }
}

/** 단발 입력 키에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableValidationDriver::PulseKey(const FKey& Key)
{
    APlayerController* PC = Player.Get();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!PC || !PC->PlayerInput)
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    PC->InputKey(
        FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.0f, 0, FInputDeviceId::CreateFromInternalId(0)));
    PulseKeysToRelease.AddUnique(Key);
}

/** 해제 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableValidationDriver::ReleaseInputs()
{
    if (APlayerController* PC = Player.Get())
    {
        for (const FKey& Key : PulseKeysToRelease)
        {
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.0f, 0,
                                                             FInputDeviceId::CreateFromInternalId(0)));
        }
        PulseKeysToRelease.Empty();
        SetHeldKey(EKeys::W, false);
        SetHeldKey(EKeys::S, false);
    }
}

/** 조준 위치에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableValidationDriver::AimAt(const FVector& Location, float DeltaTime)
{
    APlayerController* PC = Player.Get();

    // 현재 월드와 플레이어 참조 조건 검사
    if (!PC || !PC->GetPawn())
    {
        return;
    }

    // 검증 입력과 키 유지 상태에 필요한 값 준비
    const double DesiredYaw = (Location - PC->GetPawn()->GetActorLocation()).Rotation().Yaw;
    const double CurrentYaw = PC->GetControlRotation().Yaw;
    const double Error = FMath::FindDeltaAngleDegrees(CurrentYaw, DesiredYaw);
    const double RawMouse = FMath::Clamp(Error * 0.25, -15.0, 15.0);
    FInputKeyEventArgs MouseEvent = FInputKeyEventArgs::CreateSimulated(
        EKeys::MouseX, IE_Axis, static_cast<float>(RawMouse), 1, FInputDeviceId::CreateFromInternalId(0));
    MouseEvent.DeltaTime = DeltaTime;
    PC->InputKey(MouseEvent);

    // 서버 시각과 재사용 대기시간 처리
    if (FMath::Abs(Error) > 12.0 && FMath::Abs(FMath::FindDeltaAngleDegrees(LastAimYaw, CurrentYaw)) < 0.03)
    {
        AimStalledSeconds += DeltaTime;
    }
    else
    {
        AimStalledSeconds = 0.0;
    }

    // 위치·방향·거리 계산 처리
    if (AimStalledSeconds > 1.0)
    {
        // Aiming is the sole fallback for platform mouse routing; movement/damage remain normal input.
        PC->SetControlRotation(FRotator(-14.0, DesiredYaw, 0.0));
        ++AimFallbacks;
        AimStalledSeconds = 0.0;
    }

    LastAimYaw = PC->GetControlRotation().Yaw;
}

/** 이동 변화를 관찰해 검증에 기록한다. */
void UFableValidationDriver::ObserveMovement(AFableCharacter* Pawn)
{
    // 위치·방향·거리 계산 처리
    if (Pawn)
    {
        const FVector Location = Pawn->GetActorLocation();
        if (LastLocalPawn.Get() == Pawn)
        {
            const double Step = FVector::Dist2D(Location, LastLocalLocation);
            if (Step < 250.0)
            {
                LocalMovement += Step;
            } // Do not count respawn teleports.
        }
        LastLocalPawn = Pawn;
        LastLocalLocation = Location;
    }

    AFableCharacter* Other = nullptr;

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        if (*It != Pawn && It->GetPlayerState())
        {
            Other = *It;
            break;
        }
    }

    // 위치·방향·거리 계산 처리
    if (Other)
    {
        const FVector Location = Other->GetActorLocation();
        if (LastRemotePawn.Get() == Other)
        {
            const double Step = FVector::Dist2D(Location, LastRemoteLocation);
            if (Step < 250.0)
            {
                RemoteMovement += Step;
            }
        }
        LastRemotePawn = Other;
        LastRemoteLocation = Location;
    }
}

/** 재사용 대기시간 변화를 관찰해 검증에 기록한다. */
void UFableValidationDriver::ObserveCooldown(AFableCharacter* Pawn, FCooldownObservation& Observation, double ServerNow)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Pawn)
    {
        return;
    }

    // 직업·스킬·특성 빌드 처리
    if (Observation.Pawn.Get() != Pawn || !Pawn->IsAlive())
    {
        // Respawning/new rounds intentionally reset cooldowns; exclude those transitions from interval checks.
        Observation.Pawn = Pawn;
        Observation.AttackEnd = Observation.SkillEnd = Observation.DodgeEnd = 0.0;
        if (!Pawn->IsAlive())
        {
            return;
        }
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const auto Observe = [ServerNow](float Remaining, double& PreviousEnd, int32& Cycles, double* MinimumGap)
    {
        if (Remaining <= 0.02f)
        {
            return;
        }
        const double End = ServerNow + Remaining;
        if (PreviousEnd == 0.0 || End > PreviousEnd + 0.10)
        {
            if (PreviousEnd > 0.0 && MinimumGap)
            {
                *MinimumGap = FMath::Min(*MinimumGap, End - PreviousEnd);
            }
            PreviousEnd = End;
            ++Cycles;
        }
    };
    Observe(Pawn->GetAttackCooldownRemaining(), Observation.AttackEnd, Observation.AttackCycles,
            &Observation.MinimumAttackGap);
    Observe(Pawn->GetSkillCooldownRemaining(), Observation.SkillEnd, Observation.SkillCycles, nullptr);
    Observe(Pawn->GetDodgeCooldownRemaining(), Observation.DodgeEnd, Observation.DodgeCycles, nullptr);
}

/** 서버가 관찰한 공격·스킬·회피의 대기시간 검증이 모두 통과했는지 확인한다. */
bool UFableValidationDriver::ServerCooldownsPassed() const
{
    // 서버 시각과 재사용 대기시간 조건 검사
    if (Role != TEXT("host"))
    {
        return true;
    }
    if (ServerCooldowns.Num() != 2)
    {
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const auto& Pair : ServerCooldowns)
    {
        const FCooldownObservation& Item = Pair.Value;
        if (Item.AttackCycles < 4 || Item.SkillCycles < 1 || Item.DodgeCycles < 1 || Item.MinimumAttackGap < 0.50 ||
            Item.MinimumAttackGap > 1000.0)
        {
            return false;
        }
    }

    return true;
}

/** 입력 진행 전투에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableValidationDriver::DriveCombat(AFableCharacter* Pawn, int32 Round, float DeltaTime)
{
    // 검증 입력과 키 유지 상태 조건 검사
    if (!Pawn || !Pawn->IsAlive())
    {
        SetHeldKey(EKeys::W, false);
        return;
    }

    AFableEnemy* Enemy = Target.Get();

    // 지역·적·전투 진행 처리
    if (!Enemy || !Enemy->IsAlive())
    {
        Enemy = nullptr;
        double BestSide = Role == TEXT("host") ? TNumericLimits<double>::Max() : -TNumericLimits<double>::Max();
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            if (!It->IsAlive())
            {
                continue;
            }
            const double Side = It->GetActorLocation().X;
            if (!Enemy || (Role == TEXT("host") ? Side < BestSide : Side > BestSide))
            {
                Enemy = *It;
                BestSide = Side;
            }
        }
        Target = Enemy;
    }

    // 지역·적·전투 진행 조건 검사
    if (!Enemy)
    {
        SetHeldKey(EKeys::W, false);
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const FVector Difference = Enemy->GetActorLocation() - Pawn->GetActorLocation();
    const double Distance = Difference.Size2D();
    AimAt(Enemy->GetActorLocation(), DeltaTime);
    const double HeadingError =
        FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetControlRotation().Yaw, Difference.Rotation().Yaw));
    SetHeldKey(EKeys::W, Distance > 155.0 && HeadingError < 35.0);
    const double Now = FPlatformTime::Seconds();

    // 위치·방향·거리 계산 조건 검사
    if (Now < NextPulseAt || Distance > 425.0 || HeadingError > 30.0)
    {
        return;
    }

    // 이 단계에서 입력 진행 전투에 필요한 값을 구성하거나 해당 검증 단계를 처리한다.
    if (!bCombatScreenshotQueued)
    {
        bCombatScreenshotQueued = true;
        QueueScreenshot(TEXT("combat"));
    }

    NextPulseAt = Now + 0.12;

    // 검증 입력과 키 유지 상태 처리
    if (LastDodgeRound != Round && Distance < 240.0 && Pawn->GetDodgeCooldownRemaining() <= 0.0f &&
        Now - LastDodgeAttemptAt > 0.8)
    {
        PulseKey(EKeys::LeftShift);
        ++DodgeInputs;
        LastDodgeAttemptAt = Now;
        // Confirm acceptance from the authoritative cooldown instead of assuming the input worked.
        return;
    }

    // 직업·스킬·특성 빌드 조건 검사
    if (Pawn->GetSkillCooldownRemaining() <= 0.0f && Now - LastSkillAttemptAt > 0.7)
    {
        PulseKey(EKeys::K);
        ++SkillInputs;
        LastSkillAttemptAt = Now;
        return;
    }

    // 검증 입력과 키 유지 상태 처리
    if (Distance <= 225.0)
    {
        // Faster than the 0.52s server cooldown, while slower than the 0.06s local request limiter.
        PulseKey(EKeys::J);
        ++AttackInputs;
    }
}

/** 복구 확인 지점 결과를 검증 이력에 기록한다. */
void UFableValidationDriver::RecordCheckpoint(FCheckpoint& Checkpoint, const FFableAdventureView& State)
{
    Checkpoint.Round = State->Round;
    Checkpoint.Defeated = State->DefeatedEnemies;
    Checkpoint.Required = State->RequiredEnemies;
    Checkpoint.bComplete = State->bObjectiveComplete;
    Checkpoint.Elapsed = FPlatformTime::Seconds() - ScenarioStartedAt;
}

/** 대기열 화면 캡처에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableValidationDriver::QueueScreenshot(const FString& Name)
{
    const FString Path = FPaths::Combine(Directory, Role + TEXT("-") + Name + TEXT(".png"));
    if (!SavedScreenshots.Contains(Path) && !ScreenshotQueue.Contains(Path) && PendingScreenshot != Path)
    {
        ScreenshotQueue.Add(Path);
    }
}

/** 화면 캡처의 존재 여부를 확인한다. */
bool UFableValidationDriver::HasScreenshot(const FString& Name) const
{
    return SavedScreenshots.Contains(FPaths::Combine(Directory, Role + TEXT("-") + Name + TEXT(".png")));
}

/** 처리 화면 캡처 목록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableValidationDriver::ProcessScreenshots(double Now)
{
    if (!PendingScreenshot.IsEmpty())
    {
        if (IFileManager::Get().FileSize(*PendingScreenshot) > 128)
        {
            SavedScreenshots.AddUnique(PendingScreenshot);
            PendingScreenshot.Empty();
        }
        else if (Now - ScreenshotRequestedAt > 10.0)
        {
            Fail(TEXT("Engine did not save the requested gameplay screenshot: ") + PendingScreenshot);
            return;
        }
    }
    if (PendingScreenshot.IsEmpty() && ScreenshotQueue.Num() > 0 && !FScreenshotRequest::IsScreenshotRequested())
    {
        PendingScreenshot = ScreenshotQueue[0];
        ScreenshotQueue.RemoveAt(0);
        ScreenshotRequestedAt = Now;
        FScreenshotRequest::RequestScreenshot(PendingScreenshot, true, false, false);
    }
}

/** 상대 클라이언트 보고서 값을 읽는다. */
TSharedPtr<FJsonObject> UFableValidationDriver::ReadPeerReport() const
{
    FString Content;
    const FString PeerRole = Role == TEXT("host") ? TEXT("guest") : TEXT("host");

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!FFileHelper::LoadFileToString(Content, *FPaths::Combine(Directory, PeerRole + TEXT(".json"))))
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> Object;

    // JSON 계약과 검증 기록 조건 검사
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Content), Object) || !Object.IsValid())
    {
        return nullptr;
    }

    FString PeerRun;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Object->TryGetStringField(TEXT("run_id"), PeerRun) || PeerRun != RunId)
    {
        return nullptr;
    }

    return Object;
}

/** 상대 클라이언트 확인 값에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
bool UFableValidationDriver::PeerFlag(const TSharedPtr<FJsonObject>& Peer, const FString& Name) const
{
    bool Value = false;
    return Peer.IsValid() && Peer->TryGetBoolField(Name, Value) && Value;
}

/** 검증 보고서 값을 기록한다. */
void UFableValidationDriver::WriteReport()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Directory.IsEmpty())
    {
        return;
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    const double Now = FPlatformTime::Seconds();
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("run_id"), RunId);
    Report->SetStringField(TEXT("role"), Role);
    Report->SetStringField(TEXT("status"), Status);
    Report->SetStringField(TEXT("phase"), Phase);
    Report->SetStringField(TEXT("failure"), Failure);
    Report->SetStringField(TEXT("reason"), Failure);
    const ENetMode Mode = GetWorld() ? GetWorld()->GetNetMode() : NM_Standalone;
    Report->SetStringField(TEXT("network_mode"),
                           Mode == NM_ListenServer
                               ? TEXT("listen_server")
                               : (Mode == NM_Client
                                      ? TEXT("client")
                                      : (Mode == NM_DedicatedServer ? TEXT("dedicated_server") : TEXT("standalone"))));
    Report->SetBoolField(TEXT("is_local_controller"), Player.IsValid() && Player->IsLocalController());
    Report->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
    Report->SetNumberField(TEXT("local_player_id"), LocalPlayerId);
    Report->SetNumberField(TEXT("startup_elapsed_seconds"), Now - StartedAt);
    Report->SetNumberField(TEXT("scenario_elapsed_seconds"), ScenarioStartedAt > 0 ? Now - ScenarioStartedAt : 0.0);
    Report->SetBoolField(TEXT("ready"), bReady);
    Report->SetBoolField(TEXT("quorum_observed"), bQuorumObserved);
    Report->SetNumberField(TEXT("maximum_player_count"), MaximumPlayerCount);
    Report->SetBoolField(TEXT("warmup_complete"), bWarmupComplete);
    Report->SetBoolField(TEXT("round_one_complete"), bRoundOneComplete);
    Report->SetBoolField(TEXT("round_two_started"), bRoundTwoStarted);
    Report->SetBoolField(TEXT("round_two_complete"), bRoundTwoComplete);
    Report->SetBoolField(TEXT("round_one_screenshot_saved"), HasScreenshot(TEXT("round1-complete")));
    Report->SetBoolField(TEXT("round_two_screenshot_saved"), HasScreenshot(TEXT("round2-complete")));
    Report->SetBoolField(TEXT("server_cooldown_checks_passed"), Role == TEXT("host") && ServerCooldownsPassed());
    Report->SetNumberField(TEXT("local_movement_cm"), LocalMovement);
    Report->SetNumberField(TEXT("observed_remote_movement_cm"), RemoteMovement);
    Report->SetNumberField(TEXT("attack_input_count"), AttackInputs);
    Report->SetNumberField(TEXT("skill_input_count"), SkillInputs);
    Report->SetNumberField(TEXT("dodge_input_count"), DodgeInputs);
    Report->SetNumberField(TEXT("restart_input_count"), RestartInputs);
    Report->SetNumberField(TEXT("aim_fallback_count"), AimFallbacks);
    Report->SetNumberField(TEXT("observed_local_attack_cycles"), LocalCooldown.AttackCycles);
    Report->SetNumberField(TEXT("observed_local_skill_cycles"), LocalCooldown.SkillCycles);
    Report->SetNumberField(TEXT("observed_local_dodge_cycles"), LocalCooldown.DodgeCycles);
    const auto CheckpointJson = [](const FCheckpoint& Point)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("round"), Point.Round);
        Object->SetNumberField(TEXT("defeated"), Point.Defeated);
        Object->SetNumberField(TEXT("required"), Point.Required);
        Object->SetBoolField(TEXT("complete"), Point.bComplete);
        Object->SetNumberField(TEXT("elapsed_seconds"), Point.Elapsed);
        return Object;
    };
    Report->SetObjectField(TEXT("round_one"), CheckpointJson(RoundOne));
    Report->SetObjectField(TEXT("round_two_reset"), CheckpointJson(RoundTwoReset));
    Report->SetObjectField(TEXT("round_two"), CheckpointJson(RoundTwo));
    TArray<TSharedPtr<FJsonValue>> Cooldowns;

    // 직업·스킬·특성 빌드 순회
    for (const auto& Pair : ServerCooldowns)
    {
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("player_id"), Pair.Key);
        Item->SetNumberField(TEXT("accepted_attack_cycles"), Pair.Value.AttackCycles);
        Item->SetNumberField(TEXT("accepted_skill_cycles"), Pair.Value.SkillCycles);
        Item->SetNumberField(TEXT("accepted_dodge_cycles"), Pair.Value.DodgeCycles);
        Item->SetNumberField(TEXT("minimum_attack_interval_seconds"), Pair.Value.MinimumAttackGap);
        Cooldowns.Add(MakeShared<FJsonValueObject>(Item));
    }

    // JSON 계약과 검증 기록 처리
    Report->SetArrayField(TEXT("server_cooldowns"), Cooldowns);
    TArray<TSharedPtr<FJsonValue>> Screenshots;

    // JSON 계약과 검증 기록 순회
    for (const FString& Path : SavedScreenshots)
    {
        Screenshots.Add(MakeShared<FJsonValueString>(Path));
    }

    // JSON 계약과 검증 기록 처리
    Report->SetArrayField(TEXT("screenshots"), Screenshots);
    FString Serialized;

    // 체력·마나·보호막·피해량 처리
    if (FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Serialized)))
    {
        const FString Destination = FPaths::Combine(Directory, Role + TEXT(".json"));
        const FString Temporary = Destination + TEXT(".tmp");
        if (FFileHelper::SaveStringToFile(Serialized, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            IFileManager::Get().Move(*Destination, *Temporary, true, false);
        }
    }

    LastReportAt = Now;
}

/** 실패 사유를 기록하고 현재 검증을 종료한다. */
void UFableValidationDriver::Fail(const FString& Reason)
{
    if (Status != TEXT("running"))
    {
        return;
    }
    Failure = Reason;
    Finish(false);
}

/** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
void UFableValidationDriver::Finish(bool bPassed)
{
    ReleaseInputs();
    Status = bPassed ? TEXT("passed") : TEXT("failed");
    Phase = bPassed ? TEXT("completed") : TEXT("failed");
    FinishedAt = FPlatformTime::Seconds();
    WriteReport();
    UE_LOG(LogTemp, Display, TEXT("FABLE_VALIDATION role=%s status=%s reason=%s"), *Role, *Status, *Failure);
}

/** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
void UFableValidationDriver::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if UE_BUILD_SHIPPING
    return;
#else
    APlayerController* PC = Player.Get();
    if (!PC || !PC->IsLocalController() || StartedAt <= 0.0 || Directory.IsEmpty())
    {
        return;
    }
    // Release a press on the following frame, allowing the ordinary input stack to consume it first.
    for (const FKey& Key : PulseKeysToRelease)
    {
        PC->InputKey(
            FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.0f, 0, FInputDeviceId::CreateFromInternalId(0)));
    }
    PulseKeysToRelease.Empty();
    const double Now = FPlatformTime::Seconds();
    const TSharedPtr<FJsonObject> Peer = ReadPeerReport();
    if (Status != TEXT("running"))
    {
        if (!bQuitRequested && Now - FinishedAt > 2.0)
        {
            bool bPeerFinished = false;
            FString PeerStatus;
            if (Peer.IsValid() && Peer->TryGetStringField(TEXT("status"), PeerStatus))
            {
                bPeerFinished = PeerStatus == TEXT("passed") || PeerStatus == TEXT("failed");
            }
            if (Role == TEXT("guest") || Status == TEXT("failed") || bPeerFinished || Now - FinishedAt > 15.0)
            {
                bQuitRequested = true;
                PC->ConsoleCommand(TEXT("quit"));
            }
        }
        return;
    }
    FString PeerStatus;
    if (Peer.IsValid() && Peer->TryGetStringField(TEXT("status"), PeerStatus) && PeerStatus == TEXT("failed"))
    {
        FString PeerFailure;
        Peer->TryGetStringField(TEXT("failure"), PeerFailure);
        Fail(TEXT("Peer validation failed: ") + PeerFailure);
        return;
    }
    if (ScenarioStartedAt == 0.0 && Now - StartedAt > 180.0)
    {
        Fail(TEXT("Timed out waiting 180 seconds for two local players and their peer report."));
        return;
    }
    if (ScenarioStartedAt > 0.0 && Now - ScenarioStartedAt > 95.0)
    {
        Fail(TEXT("Gameplay scenario exceeded its 95-second deadline."));
        return;
    }
    ProcessScreenshots(Now);
    if (Status != TEXT("running"))
    {
        return;
    }
    AFableCharacter* Pawn = Cast<AFableCharacter>(PC->GetPawn());
    // 초기 프로토타입은 사설 원정 없이 공유 필드의 두 라운드만 검증한다.
    const AFableGameState* RootState = GetWorld()->GetGameState<AFableGameState>();
    const FFableAdventureView State = FableAdventure::Field(GetWorld());
    if (RootState)
    {
        MaximumPlayerCount = FMath::Max(MaximumPlayerCount, RootState->PlayerArray.Num());
    }
    if (PC->PlayerState)
    {
        LocalPlayerId = PC->PlayerState->GetPlayerId();
    }
    if (!RootState || !State || !Pawn || !PC->PlayerInput)
    {
        ReleaseInputs();
        if (Now - LastReportAt > 0.25)
        {
            WriteReport();
        }
        return;
    }
    const ENetMode NetMode = GetWorld()->GetNetMode();
    if ((Role == TEXT("host") && NetMode != NM_ListenServer) || (Role == TEXT("guest") && NetMode != NM_Client))
    {
        Fail(TEXT("Validation role does not match the actual listen-server/client network mode."));
        return;
    }
    int32 PawnCount = 0;
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        if (It->GetPlayerState())
        {
            ++PawnCount;
        }
    }
    bReady = RootState->PlayerArray.Num() == 2 && PawnCount == 2 && Pawn->IsAlive();
    if (ScenarioStartedAt == 0.0)
    {
        Phase = TEXT("awaiting_quorum");
        if (bReady && PeerFlag(Peer, TEXT("ready")))
        {
            bQuorumObserved = true;
            ScenarioStartedAt = Now;
            FirstRoundPawn = Pawn;
            Phase = TEXT("cooldown_input_warmup");
            QueueScreenshot(TEXT("ready"));
            PC->SetControlRotation(FRotator(-14, -90, 0));
        }
        WriteReport();
        return;
    }
    ObserveMovement(Pawn);
    ObserveCooldown(Pawn, LocalCooldown, RootState->GetServerWorldTimeSeconds());
    if (Pawn->GetDodgeCooldownRemaining() > 0.1f)
    {
        LastDodgeRound = State->Round;
    }
    if (Role == TEXT("host") && GetWorld()->GetNetMode() == NM_ListenServer)
    {
        for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
        {
            if (const APlayerState* OwnerState = It->GetPlayerState())
            {
                ObserveCooldown(*It, ServerCooldowns.FindOrAdd(OwnerState->GetPlayerId()),
                                RootState->GetServerWorldTimeSeconds());
            }
        }
    }
    if (!bWarmupComplete)
    {
        Phase = TEXT("cooldown_input_warmup");
        if (Pawn->IsAlive())
        {
            WarmupSeconds += DeltaTime;
            // Walk away from the clearing briefly, then repeatedly swing into empty space.
            PC->SetControlRotation(FRotator(-14, -90, 0));
            SetHeldKey(EKeys::W, WarmupSeconds < 0.9);
            if (Now >= NextPulseAt)
            {
                PulseKey(EKeys::J);
                ++AttackInputs;
                NextPulseAt = Now + 0.12;
            }
        }
        else
        {
            SetHeldKey(EKeys::W, false);
        }
        if (WarmupSeconds >= 3.4 && AttackInputs >= 20 && LocalCooldown.AttackCycles >= 4)
        {
            bWarmupComplete = true;
            ReleaseInputs();
        }
    }
    else if (!PeerFlag(Peer, TEXT("warmup_complete")))
    {
        Phase = TEXT("awaiting_peer_warmup");
        ReleaseInputs();
    }
    else if (!bRoundOneComplete)
    {
        Phase = TEXT("round_one_combat");
        if (State->Round != 1)
        {
            Fail(TEXT("Round changed before both players observed the first completion."));
            return;
        }
        if (State->bObjectiveComplete && State->DefeatedEnemies == 3 && State->RequiredEnemies == 3)
        {
            bRoundOneComplete = true;
            RecordCheckpoint(RoundOne, State);
            ReleaseInputs();
            QueueScreenshot(TEXT("round1-complete"));
            WriteReport();
        }
        else
        {
            DriveCombat(Pawn, 1, DeltaTime);
        }
    }
    else if (!bRoundTwoStarted)
    {
        Phase = TEXT("awaiting_round_two_reset");
        ReleaseInputs();
        if (State->Round == 2 && State->DefeatedEnemies == 0 && State->RequiredEnemies == 3 &&
            !State->bObjectiveComplete && Pawn != FirstRoundPawn.Get() && Pawn->IsAlive())
        {
            bRoundTwoStarted = true;
            RecordCheckpoint(RoundTwoReset, State);
            Target.Reset();
            QueueScreenshot(TEXT("round2-reset"));
            WriteReport();
        }
        else if (Role == TEXT("host") && State->Round == 1 && State->bObjectiveComplete &&
                 PeerFlag(Peer, TEXT("round_one_complete")) && PeerFlag(Peer, TEXT("round_one_screenshot_saved")) &&
                 HasScreenshot(TEXT("round1-complete")) && Now >= NextRestartAt)
        {
            PulseKey(EKeys::R);
            ++RestartInputs;
            NextRestartAt = Now + 1.0;
        }
    }
    else if (!PeerFlag(Peer, TEXT("round_two_started")) || !HasScreenshot(TEXT("round2-reset")))
    {
        Phase = TEXT("awaiting_peer_round_two_reset");
        ReleaseInputs();
    }
    else if (!bRoundTwoComplete)
    {
        Phase = TEXT("round_two_combat");
        if (State->Round != 2)
        {
            Fail(TEXT("Unexpected round while validating the second encounter."));
            return;
        }
        if (State->bObjectiveComplete && State->DefeatedEnemies == 3 && State->RequiredEnemies == 3)
        {
            bRoundTwoComplete = true;
            RecordCheckpoint(RoundTwo, State);
            ReleaseInputs();
            QueueScreenshot(TEXT("round2-complete"));
            WriteReport();
        }
        else
        {
            DriveCombat(Pawn, 2, DeltaTime);
        }
    }
    else
    {
        // 접속 권한·저장 리비전·멱등 키 처리
        Phase = TEXT("awaiting_final_evidence");
        ReleaseInputs();
        const bool bLocalConditions = bQuorumObserved && LocalMovement >= 250.0 && RemoteMovement >= 150.0 &&
                                      AttackInputs >= 20 && SkillInputs >= 1 && DodgeInputs >= 1 &&
                                      LocalCooldown.AttackCycles >= 4 && LocalCooldown.SkillCycles >= 1 &&
                                      LocalCooldown.DodgeCycles >= 1 && HasScreenshot(TEXT("ready")) &&
                                      HasScreenshot(TEXT("combat")) && HasScreenshot(TEXT("round1-complete")) &&
                                      HasScreenshot(TEXT("round2-reset")) && HasScreenshot(TEXT("round2-complete"));
        const bool bPeerConditions =
            PeerFlag(Peer, TEXT("round_two_complete")) && PeerFlag(Peer, TEXT("round_two_screenshot_saved"));

        // 서버 시각과 재사용 대기시간 처리
        if (bLocalConditions && bPeerConditions && ServerCooldownsPassed())
        {
            // The guest waits for authoritative cadence checks; the host waits for the guest's final report.
            if (Role == TEXT("guest") && PeerFlag(Peer, TEXT("server_cooldown_checks_passed")))
            {
                Finish(true);
            }
            else if (Role == TEXT("host") && PeerStatus == TEXT("passed"))
            {
                Finish(true);
            }
        }
    }
    if (Now - LastReportAt > 0.25)
    {
        WriteReport();
    }
#endif
}
