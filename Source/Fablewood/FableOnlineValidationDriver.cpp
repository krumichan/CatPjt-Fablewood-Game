#include "FableOnlineValidationDriver.h"
#include "FableOnlineFlowSubsystem.h"
#include "FableGameInstance.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "FableInventorySnapshot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
namespace
{
    /** 다른 검증 프로세스가 기록한 온라인 보고서를 읽는다. */
    TSharedPtr<FJsonObject> ReadOnline(const FString& Path)
    {
        // JSON 계약과 검증 기록에 필요한 값 준비
        FString Text;
        TSharedPtr<FJsonObject> J;

        // JSON 계약과 검증 기록 조건 검사
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J))
        {
            return nullptr;
        }

        return J;
    }
}

/** 생성 하위 시스템에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
bool UFableOnlineValidationDriver::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    FString R;
    return FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationRole="), R) &&
           (R == TEXT("server") || R == TEXT("client1") || R == TEXT("client2"));
#endif
}

/** 온라인 연결 하위 시스템의 초기 상태를 준비한다. */
void UFableOnlineValidationDriver::Initialize(FSubsystemCollectionBase& Collection)
{
    // 서버 시각과 재사용 대기시간 처리
    Super::Initialize(Collection);
    Started = StepAt = FPlatformTime::Seconds();
    FString Context;
    FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationRole="), Role);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationDirectory="), Directory) ||
        FPaths::IsRelative(Directory))
    {
        return;
    }

    // 체력·마나·보호막·피해량 처리
    IFileManager::Get().MakeDirectory(*Directory, true);
    FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationScenario="), Scenario);
    Ticker = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UFableOnlineValidationDriver::Tick), .1f);

    // JSON 계약과 검증 기록 처리
    if (Role != TEXT("server"))
    {
        TSharedPtr<FJsonObject> J;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationContext="), Context) ||
            (J = ReadOnline(Context)) == nullptr)
        {
            Finish(false, TEXT("Private validation context unavailable."));
            return;
        }

        // JSON 계약과 검증 기록에 필요한 값 준비
        const TSharedPtr<FJsonObject>* Accounts = nullptr;
        const TSharedPtr<FJsonObject>* Account = nullptr;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!J->TryGetStringField(TEXT("mode"), Mode) || !J->TryGetObjectField(TEXT("accounts"), Accounts) ||
            !(*Accounts)->TryGetObjectField(Role, Account) ||
            !(*Account)->TryGetStringField(TEXT("name"), AccountName) ||
            !(*Account)->TryGetStringField(TEXT("password"), Password))
        {
            Finish(false, TEXT("Invalid private validation context; values suppressed."));
            return;
        }

        J->TryGetStringField(TEXT("scenario"), Scenario);

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Scenario != TEXT("connectivity") && Scenario != TEXT("dungeon") && Scenario != TEXT("growth") &&
            Scenario != TEXT("reconnect"))
        {
            Finish(false, TEXT("Unknown gameplay validation scenario."));
            return;
        }

        // JSON 계약과 검증 기록 처리
        InitialClass = (*Account)->GetIntegerField(TEXT("initialClass"));
        const TSharedPtr<FJsonObject>* E = nullptr;

        // 이 단계에서 온라인 연결 하위 시스템의 초기 상태를 준비한다.
        if ((*Account)->TryGetObjectField(TEXT("expected"), E))
        {
            Expected = *E;
        }

        J->TryGetBoolField(TEXT("resumeExpedition"), bResumeExpedition);
        J->TryGetBoolField(TEXT("offlineFailure"), bOfflineFailure);
        FString Crash;
        J->TryGetStringField(TEXT("crashRole"), Crash);
        bCrashRole = Role == Crash;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if ((Mode != TEXT("register") && Mode != TEXT("login")) || InitialClass < 0 || InitialClass > 2)
        {
            Finish(false, TEXT("Invalid validation mode/class."));
            return;
        }
    }
}

/** 온라인 연결 하위 시스템의 요청과 콜백을 정리한다. */
void UFableOnlineValidationDriver::Deinitialize()
{
    FTSTicker::RemoveTicker(Ticker);
    ReleaseSkillInput();
    DungeonReleaseInput();
    Password.Reset();
    Super::Deinitialize();
}

/** 프로필의 크기와 정규 Base64 인코딩을 검사해 바이트로 반환한다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::Profile(const AFablePlayerState* P) const
{
    auto J = MakeShared<FJsonObject>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!P)
    {
        return J;
    }

    // JSON 계약과 검증 기록 처리
    J->SetStringField(TEXT("playerId"), P->GetBackendPlayerId());
    J->SetNumberField(TEXT("heroClass"), int32(P->HeroClass));
    J->SetNumberField(TEXT("level"), P->Level);
    J->SetNumberField(TEXT("experience"), P->Experience);
    J->SetNumberField(TEXT("classTier"), P->GetAdvancementTier());
    J->SetNumberField(TEXT("revision"), double(P->GetBackendRevision()));
    J->SetNumberField(TEXT("acknowledgedRevision"), double(P->GetBackendAcknowledgedRevision()));
    J->SetStringField(TEXT("profileHash"), P->GetBackendAcknowledgedProfileHash());
    J->SetBoolField(TEXT("ready"), P->IsBackendReady());
    J->SetBoolField(TEXT("savePending"), P->IsBackendSavePending());
    auto W = MakeShared<FJsonObject>();
    W->SetNumberField(TEXT("gold"), P->Gold);
    W->SetNumberField(TEXT("ore"), P->Ore);
    W->SetNumberField(TEXT("essence"), P->Essence);
    W->SetNumberField(TEXT("rootCores"), P->RootCores);
    W->SetNumberField(TEXT("moonCrystals"), P->MoonCrystals);
    W->SetNumberField(TEXT("healthPotions"), P->HealthPotions);
    W->SetNumberField(TEXT("manaPotions"), P->ManaPotions);
    J->SetObjectField(TEXT("wallet"), W);
    TArray<TSharedPtr<FJsonValue>> Items;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 I = 0; I < P->Inventory.Num(); ++I)
    {
        Items.Add(MakeShared<FJsonValueObject>(FableInventorySnapshot::MakeItem(P->Inventory[I], 0, I)));
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 I = 0; I < P->StashItems.Num(); ++I)
    {
        Items.Add(MakeShared<FJsonValueObject>(FableInventorySnapshot::MakeItem(P->StashItems[I], 1, I)));
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    J->SetArrayField(TEXT("items"), Items);
    J->SetNumberField(TEXT("bagCount"), P->Inventory.Num());
    J->SetNumberField(TEXT("stashCount"), P->StashItems.Num());

    // JSON 계약과 검증 기록 처리
    if (const auto* Item = P->FindItem(P->EquippedWeaponId))
    {
        J->SetStringField(TEXT("equippedWeaponInstanceId"),
                          Item->InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    // JSON 계약과 검증 기록 처리
    if (const auto* Item = P->FindItem(P->EquippedArmorId))
    {
        J->SetStringField(TEXT("equippedArmorInstanceId"),
                          Item->InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
    }

    // 퀘스트와 이야기 진행 처리
    J->SetNumberField(TEXT("forestQuest"), int32(P->ForestQuest));
    J->SetNumberField(TEXT("dungeonQuest"), int32(P->DungeonQuest));
    J->SetNumberField(TEXT("dungeonCompletions"), P->DungeonCompletions);

    // 생존 상태와 부활 진행 처리
    if (const auto* Hero = Cast<AFableCharacter>(P->GetPawn()))
    {
        J->SetNumberField(TEXT("downedCount"), Hero->GetDownedCount());
        J->SetNumberField(TEXT("timesRevived"), Hero->GetTimesRevivedCount());
        J->SetNumberField(TEXT("revivesPerformed"), Hero->GetRevivesPerformedCount());
        J->SetNumberField(TEXT("health"), Hero->GetHealth());
    }

    return J;
}

/** 현재 검증 단계의 화면을 캡처한다. */
void UFableOnlineValidationDriver::Capture(const FString& Name)
{
    const FString Path = FPaths::Combine(Directory, Role + TEXT("-") + Name + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Path, true, false);
    Screenshots.Add(Path);
}

/** 현재 관찰 값과 검증 결과를 JSON 보고서로 직렬화한다. */
FString UFableOnlineValidationDriver::SerializeReport(const FString& Status, const FString& Reason) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Directory.IsEmpty())
    {
        return FString();
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    auto J = MakeShared<FJsonObject>();
    J->SetStringField(TEXT("role"), Role);
    J->SetStringField(TEXT("mode"), Mode);
    J->SetStringField(TEXT("status"), Status);
    J->SetStringField(TEXT("reason"), Reason);
    J->SetNumberField(TEXT("step"), Step);
    J->SetStringField(TEXT("scenario"), Scenario);
    J->SetNumberField(TEXT("dungeonStep"), DungeonStep);
    J->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
    J->SetNumberField(TEXT("movementCm"), Movement);
    auto C = MakeShared<FJsonObject>();

    // JSON 계약과 검증 기록 순회
    for (const auto& Pair : Checks)
    {
        C->SetBoolField(Pair.Key, Pair.Value);
    }

    // JSON 계약과 검증 기록 처리
    J->SetObjectField(TEXT("checks"), C);
    TArray<TSharedPtr<FJsonValue>> Shots;

    // JSON 계약과 검증 기록 순회
    for (const auto& Path : Screenshots)
    {
        Shots.Add(MakeShared<FJsonValueString>(Path));
    }

    // 서버 권한과 실행 위치 처리
    J->SetArrayField(TEXT("screenshots"), Shots);
    UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
    J->SetStringField(TEXT("networkMode"), !World                                      ? TEXT("none")
                                           : World->GetNetMode() == NM_DedicatedServer ? TEXT("dedicated")
                                           : World->GetNetMode() == NM_Client          ? TEXT("client")
                                                                                       : TEXT("standalone"));
    int32 Local = 0;
    TArray<TSharedPtr<FJsonValue>> Profiles;

    // 서버 권한과 실행 위치 처리
    if (World)
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            if (auto* PC = It->Get())
            {
                if (PC->IsLocalController())
                {
                    ++Local;
                }
                if (auto* P = PC->GetPlayerState<AFablePlayerState>())
                {
                    Profiles.Add(MakeShared<FJsonValueObject>(Profile(P)));
                }
            }
        }
    }

    // JSON 계약과 검증 기록 처리
    J->SetNumberField(TEXT("localControllerCount"), Local);
    J->SetArrayField(TEXT("profiles"), Profiles);

    // JSON 계약과 검증 기록 처리
    if (Role == TEXT("server"))
    {
        TArray<TSharedPtr<FJsonValue>> ObservedIds;

        // JSON 계약과 검증 기록 순회
        for (const FString& Id : ServerObservedPlayerIds)
        {
            ObservedIds.Add(MakeShared<FJsonValueString>(Id));
        }

        J->SetArrayField(TEXT("serverObservedPlayerIds"), ObservedIds);

        // JSON 계약과 검증 기록 처리
        if (ServerDungeonObservation)
        {
            J->SetObjectField(TEXT("serverDungeonObservation"), ServerDungeonObservation);
        }

        // JSON 계약과 검증 기록 처리
        if (ServerMidBefore)
        {
            J->SetObjectField(TEXT("serverMidBefore"), ServerMidBefore);
        }

        // JSON 계약과 검증 기록 처리
        if (ServerMidSolo)
        {
            J->SetObjectField(TEXT("serverMidSolo"), ServerMidSolo);
        }

        // JSON 계약과 검증 기록 처리
        if (ServerMidRejoined)
        {
            J->SetObjectField(TEXT("serverMidRejoined"), ServerMidRejoined);
        }
    }

    // JSON 계약과 검증 기록 처리
    if (Checkpoint)
    {
        J->SetObjectField(TEXT("checkpoint"), Checkpoint);
    }

    // JSON 계약과 검증 기록 처리
    if (GrowthReport)
    {
        J->SetObjectField(TEXT("growth"), GrowthReport);
    }

    // JSON 계약과 검증 기록 처리
    if (MidRunReport)
    {
        J->SetObjectField(TEXT("midrun"), MidRunReport);
    }

    // JSON 계약과 검증 기록 처리
    if (ReconnectProgress)
    {
        J->SetObjectField(TEXT("reconnectProgress"), ReconnectProgress);
    }

    J->SetBoolField(TEXT("report_snapshot_frozen"), Status == TEXT("passed") || Status == TEXT("failed"));

    // 접속 권한·저장 리비전·멱등 키 처리
    if (!MenuProfileHash.IsEmpty())
    {
        J->SetStringField(TEXT("menuProfileHash"), MenuProfileHash);
        J->SetNumberField(TEXT("menuRevision"), double(MenuRevision));
    }

    // JSON 계약과 검증 기록 처리
    if (auto* Flow = GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>())
    {
        J->SetStringField(TEXT("flowState"), Flow->GetStateName());
        J->SetStringField(TEXT("flowMessage"), Flow->GetMessage());
        J->SetBoolField(TEXT("hasSession"), Flow->HasSession());
        J->SetNumberField(TEXT("joinAttempts"), Flow->GetJoinAttemptCount());
        J->SetNumberField(TEXT("lastHttpStatus"), Flow->GetLastHttpStatus());
    }

    FString Text;

    // JSON 계약과 검증 기록 조건 검사
    if (!FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text)))
    {
        return FString();
    }

    return Text;
}

/** 검증 보고서 값을 기록한다. */
bool UFableOnlineValidationDriver::WriteReport(const FString& Bytes) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Bytes.IsEmpty() || Directory.IsEmpty())
    {
        return false;
    }

    const FString Path = FPaths::Combine(Directory, Role + TEXT(".json")), Temp = Path + TEXT(".writing");

    return FFileHelper::SaveStringToFile(Bytes, *Temp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
           IFileManager::Get().Move(*Path, *Temp, true, true, false, true);
}

/** 현재 검증 상태를 보고서 파일에 기록한다. */
void UFableOnlineValidationDriver::Publish(const FString& Status, const FString& Reason)
{
    if (bDone || bTerminalPending)
    {
        return;
    }
    // A failed running observation is retried by the next ordinary report tick.
    WriteReport(SerializeReport(Status, Reason));
}

/** 해제 스킬 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
void UFableOnlineValidationDriver::ReleaseSkillInput()
{
    // 접속 권한·저장 리비전·멱등 키 조건 검사
    if (!bReleaseSkill)
    {
        return;
    }

    bReleaseSkill = false;

    // 접속 권한·저장 리비전·멱등 키 처리
    if (auto* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController() : nullptr)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::K, IE_Released, 0.f, 0,
                                                         FInputDeviceId::CreateFromInternalId(0)));
    }
}

/** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
void UFableOnlineValidationDriver::Finish(bool Success, const FString& Reason)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bDone || bTerminalPending)
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    ReleaseSkillInput();
    Password.Reset();
    TerminalReason = Reason;
    bTerminalSuccess = Success;
    // Capture exactly once: retries never replace the final profile with a later observation.
    TerminalBytes = SerializeReport(Success ? TEXT("passed") : TEXT("failed"), Reason);
    PublicationStarted = NextPublication = FPlatformTime::Seconds();
    bTerminalPending = true;
    PublishTerminal();
}

/** 종료 상태와 검증 결과를 최종 보고서로 기록한다. */
void UFableOnlineValidationDriver::PublishTerminal()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bTerminalPending || bDone)
    {
        return;
    }

    const double Now = FPlatformTime::Seconds();

    // 이 단계에서 종료 상태와 검증 결과를 최종 보고서로 기록한다.
    if (TerminalBytes.IsEmpty() || Now - PublicationStarted >= 30)
    {
        bTerminalPending = false;
        bDone = true;
        bPublicationFailed = true;
        FinishedAt = Now;
        UE_LOG(LogTemp, Error, TEXT("FABLE_ONLINE_REPORT_IO_FAILED role=%s attempts=%d last_error=%u"), *Role,
               PublicationAttempts, PublicationLastError);
        return;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Now < NextPublication)
    {
        return;
    }

    ++PublicationAttempts;

    // JSON 계약과 검증 기록 처리
    if (WriteReport(TerminalBytes))
    {
        bTerminalPending = false;
        bDone = true;
        FinishedAt = Now;
        UE_LOG(LogTemp, Display, TEXT("FABLE_ONLINE_VALIDATION_RESULT role=%s status=%s attempts=%d"), *Role,
               bTerminalSuccess ? TEXT("passed") : TEXT("failed"), PublicationAttempts);
        return;
    }

    PublicationLastError = FPlatformMisc::GetLastError();
    NextPublication = Now + .2;

    // 이 단계에서 종료 상태와 검증 결과를 최종 보고서로 기록한다.
    if (PublicationAttempts == 1 || PublicationAttempts % 25 == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("FABLE_ONLINE_REPORT_RETRY role=%s attempts=%d last_error=%u"), *Role,
               PublicationAttempts, PublicationLastError);
    }
}

/** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
bool UFableOnlineValidationDriver::Tick(float Delta)
{
    const double Now = FPlatformTime::Seconds();

    // HTTP 요청·응답과 연결 상태 처리
    if (bDone)
    {
        if (Now - FinishedAt > 2)
        {
            FPlatformMisc::RequestExitWithStatus(false, bPublicationFailed ? 1 : 0);
        }
        return true;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bTerminalPending)
    {
        PublishTerminal();
        return true;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    ReleaseSkillInput();
    DungeonReleaseInput();
    auto* GI = GetGameInstance();
    UWorld* World = GI ? GI->GetWorld() : nullptr;

    // 현재 월드와 플레이어 참조 조건 검사
    if (!World)
    {
        return true;
    }

    // 생존 상태와 부활 진행 처리
    if (Role == TEXT("server"))
    {
        // 현재 월드와 플레이어 참조 처리
        if (Scenario == TEXT("reconnect"))
        {
            ObserveReconnectServer(World);
        }

        // 생존 상태와 부활 진행 처리
        if (!ServerDungeonObservation)
        {
            const auto* GS = World->GetGameState<AFableGameState>();
            if (GS && GS->ActiveRegion == EFableRegion::RootRuins &&
                GS->AdventureStage == EFableAdventureStage::Cleared)
            {
                TArray<TSharedPtr<FJsonValue>> Members;
                bool Reviver = false, Revived = false;
                for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
                {
                    // 현재 월드와 플레이어 참조에 필요한 값 준비
                    const auto* Controller = It->Get();
                    const auto* State = Controller ? Controller->GetPlayerState<AFablePlayerState>() : nullptr;
                    const auto* Hero = Controller ? Cast<AFableCharacter>(Controller->GetPawn()) : nullptr;

                    // 퀘스트와 이야기 진행 처리
                    if (!State || !Hero || !Hero->IsAlive() || !State->IsBackendReady() ||
                        State->IsBackendSavePending() || State->DungeonQuest != EFableQuestStage::Ready ||
                        !FableRPG::IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()))
                    {
                        continue;
                    }

                    // 생존 상태와 부활 진행 처리
                    Reviver |= Hero->GetRevivesPerformedCount() > 0;
                    Revived |= Hero->GetTimesRevivedCount() > 0;
                    Members.Add(MakeShared<FJsonValueObject>(Profile(State)));
                }
                if (Members.Num() == 2 && Reviver && Revived)
                {
                    ServerDungeonObservation = MakeShared<FJsonObject>();
                    ServerDungeonObservation->SetArrayField(TEXT("members"), Members);
                    ServerDungeonObservation->SetNumberField(TEXT("encounterSerial"), GS->EncounterSerial);
                    ServerDungeonObservation->SetNumberField(TEXT("dungeonWave"), GS->DungeonWave);
                    ServerDungeonObservation->SetBoolField(TEXT("bossDefeated"),
                                                           GS->bObjectiveComplete && GS->BossHealth == 0);
                    Checks.Add(TEXT("server_observed_dungeon_completion"), true);
                }
            }
        }
        // Retain one simultaneous server observation, even after both clients leave.
        // Client PlayerArray counts alone cannot prove two distinct authenticated owners.

        // 서버 권한과 실행 위치 처리
        if (ServerObservedPlayerIds.IsEmpty() && World->GetNetMode() == NM_DedicatedServer)
        {
            int32 Controllers = 0, LocalControllers = 0;
            TSet<FString> ReadyIds;
            for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            {
                if (const auto* ObservedPC = It->Get())
                {
                    ++Controllers;
                    if (ObservedPC->IsLocalController())
                    {
                        ++LocalControllers;
                    }
                    if (const auto* ObservedState = ObservedPC->GetPlayerState<AFablePlayerState>();
                        ObservedState && ObservedState->IsBackendReady() &&
                        !ObservedState->GetBackendPlayerId().IsEmpty())
                    {
                        ReadyIds.Add(ObservedState->GetBackendPlayerId());
                    }
                }
            }
            if (Controllers == 2 && LocalControllers == 0 && ReadyIds.Num() == 2)
            {
                ServerObservedPlayerIds = ReadyIds.Array();
                ServerObservedPlayerIds.Sort();
                Checks.Add(TEXT("server_observed_distinct_ready_players"), true);
            }
        }

        // JSON 계약과 검증 기록 처리
        if (Now - LastReport > 1)
        {
            Publish(TEXT("running"));
            LastReport = Now;
        }

        auto Control = ReadOnline(FPaths::Combine(Directory, TEXT("control.json")));
        bool Quit = false;

        // HTTP 요청·응답과 연결 상태 처리
        if (Control && Control->TryGetBoolField(TEXT("quitServer"), Quit) && Quit)
        {
            FGenericPlatformMisc::RequestExit(false);
        }

        return true;
    }

    auto* Flow = GI->GetSubsystem<UFableOnlineFlowSubsystem>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Flow || !Flow->IsEnabled())
    {
        return true;
    }
    if (Now - Started > (Scenario == TEXT("connectivity") ? 420 : 900))
    {
        Finish(false, TEXT("Online workflow timed out; private context suppressed."));
        return true;
    }

    // JSON 계약과 검증 기록 처리
    if (Now - LastReport > .5)
    {
        Publish(bCrashRole && Step == 30 ? TEXT("crash-ready") : TEXT("running"));
        LastReport = Now;
    }

    // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
    auto Advance = [this, Now](int32 Next)
    {
        Step = Next;
        StepAt = Now;
        LastAction = -1;
    };
    auto* PC = Cast<AFablePlayerController>(GI->GetFirstLocalPlayerController());
    auto* P = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
    auto* Hero = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;
    const bool Ready = P && P->IsBackendReady() && !P->IsBackendSavePending() && P->bLastSaveSucceeded &&
                       P->GetBackendRevision() == P->GetBackendAcknowledgedRevision();

    // HTTP 요청·응답과 연결 상태 처리
    switch (Step)
    {
    // HTTP 요청·응답과 연결 상태 처리
    case 0:
        if (Now - StepAt < 1)
        {
            return true;
        }
        if (LastAction < 0)
        {
            Capture(TEXT("login"));
            LastAction = Now;
            return true;
        }
        if (Now - LastAction < .5)
        {
            return true;
        }
        if (!Flow->Authenticate(AccountName, Password + TEXT("_wrong")))
        {
            Finish(false, TEXT("Wrong-password UI submission refused."));
            return true;
        }
        Advance(1);
        break;
    case 1:
        if (Flow->GetState() != EFableOnlineState::Error)
        {
            return true;
        }
        if (Flow->GetLastHttpStatus() != 401)
        {
            Finish(false, TEXT("Wrong password did not produce HTTP401."));
            return true;
        }
        if (LastAction < 0)
        {
            Checks.Add(TEXT("wrong_password_rejected"), true);
            Capture(TEXT("login-error"));
            LastAction = Now;
            return true;
        }
        if (Now - LastAction < .5)
        {
            return true;
        }
        if (!(Mode == TEXT("register") ? Flow->Register(AccountName, Password, static_cast<EFableClass>(InitialClass))
                                       : Flow->Authenticate(AccountName, Password)))
        {
            Finish(false, TEXT("Correct account UI submission refused."));
            return true;
        }
        Password.Reset();
        Advance(2);
        break;
    case 2:
        if (Flow->GetState() == EFableOnlineState::Error)
        {
            Finish(false, TEXT("Correct account authentication failed."));
            return true;
        }
        if (Flow->GetState() != EFableOnlineState::Character)
        {
            return true;
        }
        if (Flow->GetCharacter().PlayerId.IsEmpty() || Flow->GetServers().IsEmpty())
        {
            Finish(false, TEXT("Character/server list missing."));
            return true;
        }
        if (Mode == TEXT("register") && (!Flow->GetCharacter().bNewCharacter || Flow->GetCharacter().Level != 1 ||
                                         int32(Flow->GetCharacter().HeroClass) != InitialClass))
        {
            Finish(false, TEXT("New character preview is incorrect."));
            return true;
        }
        if (Expected && (Flow->GetCharacter().PlayerId != Expected->GetStringField(TEXT("playerId")) ||
                         Flow->GetCharacter().ProfileHash != Expected->GetStringField(TEXT("profileHash"))))
        {
            Finish(false, TEXT("Existing character preview does not match durable checkpoint."));
            return true;
        }
        Checks.Add(TEXT("character_preview"), true);
        Capture(TEXT("character"));
        Flow->SelectServer(Flow->GetServers()[0].Id);
        Advance(3);
        break;
    case 3:
        if (Now - StepAt < 1)
        {
            return true;
        }
        Flow->JoinSelectedServer();
        Advance(bOfflineFailure ? 4 : 6);
        break;
    case 4:
        if (Flow->GetState() != EFableOnlineState::Disconnected && Flow->GetState() != EFableOnlineState::Error)
        {
            return true;
        }
        if (!Flow->HasSession())
        {
            Finish(false, TEXT("Travel failure discarded the authenticated session."));
            return true;
        }
        Checks.Add(TEXT("connection_failure_visible"), true);
        Capture(TEXT("connection-error"));
        Advance(5);
        Publish(TEXT("waiting-server"));
        break;
    case 5:
    {
        auto Control = ReadOnline(FPaths::Combine(Directory, TEXT("control.json")));
        bool Join = false;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Control || !Control->TryGetBoolField(TEXT("allowJoin"), Join) || !Join)
        {
            return true;
        }
        // A failed network travel first returns to the local lobby. Do not treat
        // its expected in-flight map transition as a rejected user retry.
        if (const auto* FableGI = Cast<UFableGameInstance>(GI); FableGI && FableGI->IsTravelInProgress())
        {
            return true;
        }
        if (!Flow->Retry())
        {
            Finish(false, TEXT("Connection retry refused."));
            return true;
        }

        Advance(6);
        break;
    }
    case 6:
        if (Flow->GetState() == EFableOnlineState::Error)
        {
            Finish(false, TEXT("Online entry failed after server ready."));
            return true;
        }
        if (Flow->GetState() != EFableOnlineState::Playing || !Ready || !Hero)
        {
            return true;
        }
        if (P->GetBackendPlayerId() != Flow->GetSessionPlayerId())
        {
            Finish(false, TEXT("Wrong character owner entered the world."));
            return true;
        }
        if (!World->GetGameState() || World->GetGameState()->PlayerArray.Num() != 2)
        {
            return true;
        }
        Checks.Add(TEXT("two_distinct_remote_players"), true);
        MovementStart = Hero->GetActorLocation();
        BeforeManaPotions = P->ManaPotions;
        Advance(7);
        break;
    case 7:
        if (!Hero || !Ready)
        {
            return true;
        }
        Movement = FMath::Max(Movement, FVector::Dist2D(MovementStart, Hero->GetActorLocation()));
        if (Movement < 65)
        {
            Hero->AddMovementInput(FVector(0, -1, 0), 1);
            return true;
        }
        Hero->GetCharacterMovement()->StopMovementImmediately();
        Checks.Add(TEXT("normal_movement"), true);
        if (Mode == TEXT("login"))
        {
            if (Scenario == TEXT("reconnect") && bResumeExpedition)
            {
                Advance(100);
            }
            else
            {
                Checkpoint = Profile(P);
                Advance(30);
            }
            break;
        }
        PC->InputKey(
            FInputKeyEventArgs::CreateSimulated(EKeys::K, IE_Pressed, 1.f, 0, FInputDeviceId::CreateFromInternalId(0)));
        bReleaseSkill = true;
        bSkillSent = true;
        Advance(8);
        break;
    case 8:
        if (!Hero || !P || !Ready)
        {
            return true;
        }
        if (LastAction >= 0)
        {
            if (P->ManaPotions == BeforeManaPotions - 1 && Ready)
            {
                Checks.Add(TEXT("normal_potion_saved_once"), true);
                Checkpoint = Profile(P);
                Capture(TEXT("playing"));
                Advance(Scenario == TEXT("connectivity") ? 30 : 100);
            }
            return true;
        }
        if (Hero->GetMana() >= Hero->GetMaxMana() - 1)
        {
            return true;
        }
        PC->UIUseManaPotion();
        LastAction = Now;
        break;
    case 100:
        if (!P || !Hero || !PC)
        {
            return true;
        }
        if (TickDungeon(PC, P, Hero, Now))
        {
            Checkpoint = Profile(P);
            Capture(Scenario == TEXT("growth")      ? TEXT("growth-finished")
                    : Scenario == TEXT("reconnect") ? TEXT("reconnected-dungeon")
                                                    : TEXT("dungeon-cleared"));
            Advance(30);
        }
        break;
    case 30:
        if (bCrashRole)
        {
            return true;
        }
        if (Now - StepAt < 2)
        {
            return true;
        }
        if (!Flow->RequestLeave())
        {
            Finish(false, TEXT("Safe leave UI request refused."));
            return true;
        }
        Advance(31);
        break;
    case 31:
        if (Flow->GetState() == EFableOnlineState::Error || Flow->GetState() == EFableOnlineState::Disconnected)
        {
            Finish(false, TEXT("Safe leave failed; no save success claimed."));
            return true;
        }
        if (Flow->GetState() != EFableOnlineState::Character)
        {
            return true;
        }
        if (!Checkpoint)
        {
            Finish(false, TEXT("Acknowledged gameplay checkpoint missing."));
            return true;
        }
        {
            // 재화와 작업 비용에 필요한 값 준비
            const auto& C = Flow->GetCharacter();
            const auto Wallet = Checkpoint->GetObjectField(TEXT("wallet"));

            // 접속 권한·저장 리비전·멱등 키 조건 검사
            if (C.PlayerId != Checkpoint->GetStringField(TEXT("playerId")) ||
                int32(C.HeroClass) != Checkpoint->GetIntegerField(TEXT("heroClass")) ||
                C.Level != Checkpoint->GetIntegerField(TEXT("level")) ||
                C.Experience != Checkpoint->GetIntegerField(TEXT("experience")) ||
                C.ClassTier != Checkpoint->GetIntegerField(TEXT("classTier")) ||
                C.Gold != Wallet->GetIntegerField(TEXT("gold")) ||
                C.BagCount != Checkpoint->GetIntegerField(TEXT("bagCount")) ||
                C.StashCount != Checkpoint->GetIntegerField(TEXT("stashCount")) ||
                C.Revision < Checkpoint->GetNumberField(TEXT("acknowledgedRevision")) || C.ProfileHash.IsEmpty() ||
                !C.bHasSummary)
            {
                Finish(false, TEXT("Character screen differs semantically after acknowledged leave."));
                return true;
            }

            // 접속 권한·저장 리비전·멱등 키 처리
            MenuProfileHash = C.ProfileHash;
            MenuRevision = C.Revision;
        }
        Checks.Add(TEXT("save_release_ack_before_menu"), true);
        Capture(TEXT("saved-character"));
        Advance(32);
        break;
    case 32:
        if (Now - StepAt < 1)
        {
            return true;
        }
        Flow->Logout();
        Advance(33);
        break;
    case 33:
        if (Flow->GetState() != EFableOnlineState::SignedOut)
        {
            return true;
        }
        if (Flow->HasSession())
        {
            Finish(false, TEXT("Logout left a local access token."));
            return true;
        }
        Checks.Add(TEXT("logout_clears_session"), true);
        Capture(TEXT("logged-out"));
        Advance(34);
        break;
    case 34:
        if (Now - StepAt > 2)
        {
            Finish(true, TEXT("Account UI, character, online entry and acknowledged leave workflow completed."));
        }
        break;
    default:
        Finish(false, TEXT("Unknown online validation step."));
        break;
    }

    return true;
}
