#include "FableOnlineValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
namespace
{
    /** 역할별 재접속 대기 지점의 좌표를 반환한다. */
    FVector SafeReconnectLocation(const AFableCharacter* Hero)
    {
        return Hero && Hero->GetHeroClass() == EFableClass::Archer ? FVector(6480, -1700, 100)
                                                                   : FVector(6300, -1580, 100);
    }
}

/** 일반 이동·회피 입력으로 재접속 검증의 안전 경로를 따라 이동한다. */
bool UFableOnlineValidationDriver::ReconnectWalkSafe(AFablePlayerController* PC)
{
    auto* Hero = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !Hero->IsAlive())
    {
        return false;
    }

    // 이 단계에서 일반 이동·회피 입력으로 재접속 검증의 안전 경로를 따라 이동한다.
    if (PC->IsAnyPanelOpen())
    {
        PC->UIClosePanels();
    }

    // 이동 입력과 속도 제한에 필요한 값 준비
    const auto Walk = [&](const FVector& Goal, float Radius)
    {
        // 이동 입력과 속도 제한에 필요한 값 준비
        const bool Reached = DungeonWalk(PC, Goal, Radius);
        const FVector Delta = Goal - Hero->GetActorLocation(), Velocity = Hero->GetVelocity();
        const double Time = FPlatformTime::Seconds();
        // Dodge is an ordinary input: authority checks ground state and cooldown.

        // 이동 입력과 속도 제한 처리
        if (!Reached && Delta.Size2D() > 600 && Velocity.Size2D() > 100 && Hero->GetDodgeCooldownRemaining() <= 0 &&
            Time - DungeonActionAt > 1.5 &&
            FVector::DotProduct(Delta.GetSafeNormal2D(), Velocity.GetSafeNormal2D()) > .95)
        {
            DungeonKey(PC, EKeys::LeftShift);
            DungeonActionAt = Time;
        }

        return Reached;
    };

    // 위치·방향·거리 계산 처리
    if (ReconnectWaypoint < 3)
    {
        // Keep moving around the perimeter until the short-range pursuit has broken.
        // Distinct inner/outer lanes avoid parking one pawn across the other's path.
        // 위치·방향·거리 계산에 필요한 값 준비
        const FVector MageRoute[] = {FVector(9200, -1300, 100), FVector(9200, 1200, 100), FVector(6500, 1200, 100)};
        const FVector ArcherRoute[] = {FVector(9500, -1650, 100), FVector(9500, 1500, 100), FVector(6800, 1500, 100)};
        const FVector Waypoint =
            Hero->GetHeroClass() == EFableClass::Archer ? ArcherRoute[ReconnectWaypoint] : MageRoute[ReconnectWaypoint];

        // 이 단계에서 일반 이동·회피 입력으로 재접속 검증의 안전 경로를 따라 이동한다.
        if (Walk(Waypoint, 80))
        {
            ++ReconnectWaypoint;
        }

        return false;
    }

    return Walk(SafeReconnectLocation(Hero), 60) && Hero->GetVelocity().Size2D() < 10;
}

/** 서버가 관찰한 전투·파티·저장 상태를 JSON으로 만든다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::ReconnectServerSnapshot(UWorld* World) const
{
    // 생존 상태와 부활 진행에 필요한 값 준비
    auto J = MakeShared<FJsonObject>();
    const auto* GS = World->GetGameState<AFableGameState>();
    J->SetNumberField(TEXT("encounterSerial"), GS ? GS->EncounterSerial : 0);
    J->SetNumberField(TEXT("wave"), GS ? GS->DungeonWave : 0);
    J->SetNumberField(TEXT("defeated"), GS ? GS->DefeatedEnemies : 0);
    J->SetNumberField(TEXT("required"), GS ? GS->RequiredEnemies : 0);
    TArray<AFableEnemy*> Living;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(World); It; ++It)
    {
        if (It->IsAlive() && It->GetEncounterRegion() == EFableRegion::RootRuins)
        {
            Living.Add(*It);
        }
    }

    // 지역·적·전투 진행 처리
    Living.Sort(
        [](const AFableEnemy& A, const AFableEnemy& B)
        {
            return A.GetName() < B.GetName();
        });
    TArray<TSharedPtr<FJsonValue>> Enemies;

    // 지역·적·전투 진행 순회
    for (const auto* Enemy : Living)
    {
        auto E = MakeShared<FJsonObject>();
        E->SetStringField(TEXT("id"), Enemy->GetName());
        E->SetNumberField(TEXT("health"), Enemy->GetHealth());
        Enemies.Add(MakeShared<FJsonValueObject>(E));
    }

    // 지역·적·전투 진행 처리
    J->SetArrayField(TEXT("enemies"), Enemies);
    TArray<TSharedPtr<FJsonValue>> Members;
    bool Saved = true, Safe = true;

    // 접속 권한·저장 리비전·멱등 키 순회
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        // 현재 월드와 플레이어 참조에 필요한 값 준비
        const auto* Controller = It->Get();
        const auto* P = Controller ? Controller->GetPlayerState<AFablePlayerState>() : nullptr;
        const auto* Hero = Controller ? Cast<AFableCharacter>(Controller->GetPawn()) : nullptr;

        // 이 단계에서 서버가 관찰한 전투·파티·저장 상태를 JSON으로 만든다.
        if (!P || !Hero)
        {
            Saved = Safe = false;
            continue;
        }

        // 접속 권한·저장 리비전·멱등 키 처리
        Saved &= P->IsBackendReady() && !P->IsBackendSavePending() && P->bLastSaveSucceeded &&
                 P->GetBackendRevision() == P->GetBackendAcknowledgedRevision();
        Safe &= Hero->IsAlive() && FableRPG::IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()) &&
                FVector::Dist2D(Hero->GetActorLocation(), SafeReconnectLocation(Hero)) < 220;

        // 지역·적·전투 진행 순회
        for (const auto* Enemy : Living)
        {
            Safe &= FVector::Dist2D(Hero->GetActorLocation(), Enemy->GetActorLocation()) > 1800;
        }

        Members.Add(MakeShared<FJsonValueObject>(Profile(P)));
    }

    // JSON 계약과 검증 기록 처리
    J->SetArrayField(TEXT("members"), Members);
    J->SetBoolField(TEXT("allSavedReady"), Saved && Members.Num() > 0);
    J->SetBoolField(TEXT("allSafe"), Safe && Members.Num() > 0);

    return J;
}

/** 같은 서버에서 이탈과 복귀 전후의 적·파티 상태를 기록한다. */
void UFableOnlineValidationDriver::ObserveReconnectServer(UWorld* World)
{
    const auto* GS = World->GetGameState<AFableGameState>();

    // 생존 상태와 부활 진행 조건 검사
    if (!GS || GS->ActiveRegion != EFableRegion::RootRuins || GS->DungeonWave != 1 || GS->DefeatedEnemies != 1)
    {
        return;
    }

    auto Snapshot = ReconnectServerSnapshot(World);

    // 프로필과 저장 스냅숏 조건 검사
    if (!Snapshot->GetBoolField(TEXT("allSavedReady")) || !Snapshot->GetBoolField(TEXT("allSafe")) ||
        Snapshot->GetArrayField(TEXT("enemies")).Num() != 2)
    {
        return;
    }

    const int32 Members = Snapshot->GetArrayField(TEXT("members")).Num();

    // 프로필과 저장 스냅숏 처리
    if (!ServerMidBefore && Members == 2)
    {
        ServerMidBefore = Snapshot;
        Checks.Add(TEXT("server_midrun_before"), true);
    }
    else if (ServerMidBefore && !ServerMidSolo && Members == 1)
    {
        ServerMidSolo = Snapshot;
        Checks.Add(TEXT("server_midrun_solo"), true);
    }
    else if (ServerMidSolo && !ServerMidRejoined && Members == 2)
    {
        TSet<FString> BeforeIds, AfterIds;

        // 같은 규칙을 적용할 대상을 순회한다.
        for (const auto& V : ServerMidBefore->GetArrayField(TEXT("members")))
        {
            BeforeIds.Add(V->AsObject()->GetStringField(TEXT("playerId")));
        }

        // 프로필과 저장 스냅숏 순회
        for (const auto& V : Snapshot->GetArrayField(TEXT("members")))
        {
            AfterIds.Add(V->AsObject()->GetStringField(TEXT("playerId")));
        }

        bool Same = BeforeIds.Num() == 2 && AfterIds.Num() == 2;

        // 같은 규칙을 적용할 대상을 순회한다.
        for (const auto& Id : BeforeIds)
        {
            Same &= AfterIds.Contains(Id);
        }

        // 프로필과 저장 스냅숏 처리
        if (Same && Snapshot->GetIntegerField(TEXT("encounterSerial")) ==
                        ServerMidBefore->GetIntegerField(TEXT("encounterSerial")))
        {
            ServerMidRejoined = Snapshot;
            Checks.Add(TEXT("server_midrun_same_id_rejoin"), true);
        }
    }
}

/** 보상 저장·클라이언트 중단·동일 캐릭터 복귀의 검증 단계를 진행한다. */
bool UFableOnlineValidationDriver::TickReconnect(AFablePlayerController* PC, AFablePlayerState* P,
                                                 AFableCharacter* Hero, double Now)
{
    auto* GS = Hero->GetWorld()->GetGameState<AFableGameState>();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!GS)
    {
        return false;
    }

    // 지역·적·전투 진행 처리
    if (ReconnectStarted == 0)
    {
        ReconnectStarted = Now;
        ReconnectSerial = GS->EncounterSerial;
        if (bResumeExpedition)
        {
            if (!Expected || Expected->GetIntegerField(TEXT("encounterSerial")) != ReconnectSerial)
            {
                Finish(false, TEXT("The rejoined client entered a different dungeon encounter."));
                return false;
            }
            ReconnectStep = 1;
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Now - ReconnectStarted > 300)
    {
        Finish(false, TEXT("Mid-dungeon reconnect validation timed out."));
        return false;
    }

    // 생존 상태와 부활 진행 처리
    if (GS->ActiveRegion != EFableRegion::RootRuins || GS->EncounterSerial != ReconnectSerial || GS->DungeonWave != 1 ||
        GS->DefeatedEnemies > 1 || GS->bPartyRecoveryPending || !Hero->IsAlive())
    {
        Finish(
            false,
            FString::Printf(
                TEXT(
                    "Reconnect checkpoint changed (region=%d serial=%d/%d wave=%d defeated=%d recovery=%d health=%.1f location=%s)."),
                int32(GS->ActiveRegion), GS->EncounterSerial, ReconnectSerial, GS->DungeonWave, GS->DefeatedEnemies,
                GS->bPartyRecoveryPending ? 1 : 0, Hero->GetHealth(), *Hero->GetActorLocation().ToCompactString()));
        return false;
    }

    TArray<AFableEnemy*> Living;

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(Hero->GetWorld()); It; ++It)
    {
        if (It->IsAlive() && It->GetEncounterRegion() == EFableRegion::RootRuins)
        {
            Living.Add(*It);
        }
    }

    // JSON 계약과 검증 기록 처리
    ReconnectProgress = MakeShared<FJsonObject>();
    ReconnectProgress->SetNumberField(TEXT("step"), ReconnectStep);
    ReconnectProgress->SetNumberField(TEXT("waypoint"), ReconnectWaypoint);
    ReconnectProgress->SetNumberField(TEXT("x"), Hero->GetActorLocation().X);
    ReconnectProgress->SetNumberField(TEXT("y"), Hero->GetActorLocation().Y);
    ReconnectProgress->SetNumberField(TEXT("health"), Hero->GetHealth());
    float Nearest = 100000;

    // 지역·적·전투 진행 순회
    for (const auto* Enemy : Living)
    {
        Nearest = FMath::Min(Nearest, float(FVector::Dist2D(Hero->GetActorLocation(), Enemy->GetActorLocation())));
    }

    // 지역·적·전투 진행 처리
    ReconnectProgress->SetNumberField(TEXT("nearestLivingEnemyCm"), Nearest);
    TArray<TSharedPtr<FJsonValue>> Positions;

    // 체력·마나·보호막·피해량 순회
    for (APlayerState* Member : GS->PlayerArray)
    {
        const auto* Pawn = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;

        // 이 단계에서 보상 저장·클라이언트 중단·동일 캐릭터 복귀의 검증 단계를 진행한다.
        if (!Pawn)
        {
            continue;
        }

        // 체력·마나·보호막·피해량에 필요한 값 준비
        auto Position = MakeShared<FJsonObject>();
        Position->SetNumberField(TEXT("heroClass"), int32(Pawn->GetHeroClass()));
        Position->SetNumberField(TEXT("x"), Pawn->GetActorLocation().X);
        Position->SetNumberField(TEXT("y"), Pawn->GetActorLocation().Y);
        Position->SetNumberField(TEXT("health"), Pawn->GetHealth());
        Position->SetNumberField(TEXT("goalDistanceCm"),
                                 FVector::Dist2D(Pawn->GetActorLocation(), SafeReconnectLocation(Pawn)));
        Positions.Add(MakeShared<FJsonValueObject>(Position));
    }

    ReconnectProgress->SetArrayField(TEXT("members"), Positions);

    // 생존 상태와 부활 진행 처리
    if (ReconnectStep == 0)
    {
        // 생존 상태와 부활 진행 조건 검사
        if (GS->DefeatedEnemies == 1 && Living.Num() == 2)
        {
            DungeonHold(PC, EKeys::W, false);
            DungeonHold(PC, EKeys::S, false);
            ReconnectStep = 1;
            return false;
        }
        if (Role == TEXT("client2"))
        {
            ReconnectWalkSafe(PC);
            return false;
        }

        // 지역·적·전투 진행 처리
        if (!ReconnectVictim.IsValid())
        {
            AFableEnemy* First = nullptr;
            for (auto* Enemy : Living)
            {
                if (!First || Enemy->GetActorLocation().X < First->GetActorLocation().X)
                {
                    First = Enemy;
                }
            }
            ReconnectVictim = First;
        }

        auto* Target = ReconnectVictim.Get();

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Target || !Target->IsAlive())
        {
            return false;
        }

        // 이 단계에서 보상 저장·클라이언트 중단·동일 캐릭터 복귀의 검증 단계를 진행한다.
        if (PC->IsAnyPanelOpen())
        {
            PC->UIClosePanels();
        }

        // 검증 입력과 키 유지 상태에 필요한 값 준비
        const float Distance = FVector::Dist2D(Hero->GetActorLocation(), Target->GetActorLocation());
        PC->SetControlRotation(
            FRotator(-12, (Target->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
        DungeonHold(PC, EKeys::W, Distance > 600);
        DungeonHold(PC, EKeys::S, false);

        // 검증 입력과 키 유지 상태 처리
        if (Distance < 1050 && Now - DungeonAttackAt > .7)
        {
            DungeonKey(PC, EKeys::J);
            DungeonAttackAt = Now;
        }

        return false;
    }

    // 생존 상태와 부활 진행 조건 검사
    if (GS->DefeatedEnemies != 1 || Living.Num() != 2)
    {
        Finish(false, TEXT("More than one guardian was credited before reconnection."));
        return false;
    }
    if (!ReconnectWalkSafe(PC))
    {
        return false;
    }

    bool AllFar = true;

    // 지역·적·전투 진행 순회
    for (const auto* Enemy : Living)
    {
        AllFar &= FVector::Dist2D(Hero->GetActorLocation(), Enemy->GetActorLocation()) > 1800;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!AllFar)
    {
        return false;
    }

    const bool Saved = P->IsBackendReady() && !P->IsBackendSavePending() && P->bLastSaveSucceeded &&
                       P->GetBackendRevision() == P->GetBackendAcknowledgedRevision();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!Saved)
    {
        return false;
    }
    if (P->Gold != 153 || P->Ore != 8 || P->RootCores != 0 || P->Level != 2 || P->Experience != 140 ||
        P->Inventory.Num() != 5 || P->DungeonQuest != EFableQuestStage::Active)
    {
        Finish(false, TEXT("The acknowledged midrun guardian reward changed or duplicated."));
        return false;
    }

    // 이 단계에서 보상 저장·클라이언트 중단·동일 캐릭터 복귀의 검증 단계를 진행한다.
    if (GS->PlayerArray.Num() == 1 && Role == TEXT("client1") && !bResumeExpedition)
    {
        if (!bReconnectPeerGone)
        {
            Capture(TEXT("midrun-solo"));
            Checks.Add(TEXT("midrun_peer_disconnected"), true);
            bReconnectPeerGone = true;
        }
        return false;
    }

    bool BothSafe = GS->PlayerArray.Num() == 2;

    // 현재 월드와 플레이어 참조 순회
    for (APlayerState* Member : GS->PlayerArray)
    {
        const auto* Other = Member ? Cast<AFableCharacter>(Member->GetPawn()) : nullptr;
        BothSafe &=
            Other && Other->IsAlive() && FVector::Dist2D(Other->GetActorLocation(), SafeReconnectLocation(Other)) < 220;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BothSafe)
    {
        return false;
    }

    // 지역·적·전투 진행 처리
    if (!MidRunReport)
    {
        MidRunReport = MakeShared<FJsonObject>();
        MidRunReport->SetNumberField(TEXT("encounterSerial"), ReconnectSerial);
        MidRunReport->SetNumberField(TEXT("wave"), 1);
        MidRunReport->SetNumberField(TEXT("defeated"), 1);
        MidRunReport->SetNumberField(TEXT("liveEnemies"), 2);
        MidRunReport->SetBoolField(TEXT("resumeExpedition"), bResumeExpedition);
        Checks.Add(TEXT("midrun_reward_saved_once"), true);
    }

    // 프로필과 저장 스냅숏 처리
    if (Role == TEXT("client2") && !bResumeExpedition)
    {
        Checkpoint = Profile(P);
        Capture(TEXT("midrun-before-disconnect"));
        Step = 30;
        StepAt = Now;
        LastAction = -1;
        return false;
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Role == TEXT("client1") && !bReconnectPeerGone && !bResumeExpedition)
    {
        return false;
    }

    Checks.Add(TEXT("midrun_same_encounter_rejoined"), true);

    return true;
}
