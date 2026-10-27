#include "FableOnlineValidationDriver.h"

#include "FableCharacter.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"
#include "FableTownNavigation.h"
#include "FableTownOverlook.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace
{
    /** 공개 위치만 기록하며 클라이언트 좌표를 권한 요청으로 만들지 않는다. */
    TArray<TSharedPtr<FJsonValue>> WalkVector(const FVector& Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
                MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** 읽기 전용 공간 문맥이며 nonce·내부 소유 토큰을 공개하지 않는다. */
    TSharedRef<FJsonObject> WalkContext(const FFableTownLocationContext& Context)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("valid"), Context.IsValid());
        Result->SetNumberField(TEXT("areaKind"), int32(Context.AreaKind));
        Result->SetNumberField(TEXT("townId"), int32(Context.TownId));
        Result->SetStringField(TEXT("floorId"), Context.FloorId.ToString());
        Result->SetStringField(TEXT("connectorId"), Context.ConnectorId.ToString());
        Result->SetNumberField(TEXT("catalogVersion"), Context.CatalogVersion);
        Result->SetBoolField(TEXT("geometryReady"), Context.bGeometryReady);
        Result->SetArrayField(TEXT("localPosition"), WalkVector(Context.LocalPosition));
        return Result;
    }

    /** 同一実行フォルダーの観測だけを読み、他プロセスを操作しない。 */
    TSharedPtr<FJsonObject> ReadWalkReport(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> Result;
        return FFileHelper::LoadFileToString(Text, *Path) &&
                       FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result)
                   ? Result
                   : nullptr;
    }

    /** 실제 도착 뒤 양측의 동일 milestone 존재만 확인하며 서로 다른 clock을 비교하지 않는다. */
    bool HasWalkMilestone(const TSharedPtr<FJsonObject>& Report, const FString& Label, const FString& OwnPublicKey)
    {
        const TSharedPtr<FJsonObject>* Walk = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Milestones = nullptr;
        if (!Report || !Report->TryGetObjectField(TEXT("floorWalk"), Walk) ||
            !(*Walk)->TryGetArrayField(TEXT("milestones"), Milestones))
        {
            return false;
        }

        for (const auto& Item : *Milestones)
        {
            const auto Row = Item->AsObject();
            if (Row && Row->GetStringField(TEXT("name")) == Label &&
                !Row->GetStringField(TEXT("publicPlayerKey")).IsEmpty() &&
                Row->GetStringField(TEXT("publicPlayerKey")) != OwnPublicKey)
            {
                return true;
            }
        }
        return false;
    }
}

/** 권한·소유자·상대 복제를 구분해 실제 지지면과 현재 HUD 조회를 보존한다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::FloorWalkPawnSnapshot(const AFableCharacter* Hero,
                                                                            const AFablePlayerController* PC,
                                                                            bool bIncludeOwnerState) const
{
    auto Row = MakeShared<FJsonObject>();
    const auto* ProfileState = Hero ? Hero->GetPlayerState<AFablePlayerState>() : nullptr;
    UWorld* World = Hero ? Hero->GetWorld() : nullptr;
    const auto* CharacterMovement = Hero ? Hero->GetCharacterMovement() : nullptr;
    const auto* Capsule = Hero ? Hero->GetCapsuleComponent() : nullptr;
    const auto* Catalog = FableTownNavigation::CatalogFor(Hero);
    const auto* Manager = AFableTownManager::Find(World);
    const bool bOwnerProfile = ProfileState && bIncludeOwnerState && Hero &&
                               (Hero->HasAuthority() || (PC && PC->IsLocalController() &&
                                                         PC->GetPlayerState<AFablePlayerState>() == ProfileState));

    Row->SetStringField(TEXT("playerId"), bOwnerProfile ? ProfileState->GetBackendPlayerId() : FString());
    Row->SetStringField(TEXT("publicPlayerKey"), ProfileState ? ProfileState->PartyPlayerKey : FString());
    Row->SetBoolField(TEXT("ownerProfileIncluded"), bOwnerProfile);
    Row->SetStringField(TEXT("pawnName"), Hero ? Hero->GetName() : FString());
    Row->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
    Row->SetBoolField(TEXT("authority"), Hero && Hero->HasAuthority());
    Row->SetBoolField(TEXT("localController"), PC && PC->IsLocalController());
    Row->SetBoolField(TEXT("controllerOwnsPawn"), PC && PC->GetPawn() == Hero && ProfileState &&
                                                      PC->GetPlayerState<AFablePlayerState>() == ProfileState);
    Row->SetBoolField(TEXT("profileOwnerMatchesController"), ProfileState && PC && ProfileState->GetOwner() == PC);
    Row->SetBoolField(TEXT("alive"), Hero && Hero->IsAlive());
    Row->SetBoolField(TEXT("profileLoaded"), ProfileState && ProfileState->bProfileLoaded);
    Row->SetBoolField(TEXT("backendReady"), bOwnerProfile && ProfileState->IsBackendReady());
    Row->SetBoolField(TEXT("savePending"), bOwnerProfile && ProfileState->IsBackendSavePending());
    Row->SetNumberField(TEXT("revision"), bOwnerProfile ? double(ProfileState->GetBackendRevision()) : -1);
    Row->SetNumberField(TEXT("acknowledgedRevision"),
                        bOwnerProfile ? double(ProfileState->GetBackendAcknowledgedRevision()) : -1);
    Row->SetArrayField(TEXT("position"), WalkVector(Hero ? Hero->GetActorLocation() : FVector::ZeroVector));
    Row->SetArrayField(TEXT("velocity"), WalkVector(Hero ? Hero->GetVelocity() : FVector::ZeroVector));
    Row->SetBoolField(TEXT("grounded"), CharacterMovement && CharacterMovement->IsMovingOnGround());
    Row->SetBoolField(TEXT("movementFloorWalkable"),
                      CharacterMovement && CharacterMovement->CurrentFloor.IsWalkableFloor());
    Row->SetNumberField(TEXT("maxWalkSpeed"), CharacterMovement ? CharacterMovement->MaxWalkSpeed : 0);
    Row->SetNumberField(TEXT("capsuleHalfHeight"), Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0);
    Row->SetNumberField(TEXT("capsuleRadius"), Capsule ? Capsule->GetScaledCapsuleRadius() : 0);

    FFableTownLocationContext Physical;
    const bool bResolved = Hero && Catalog && Catalog->ResolveLocation(Hero->GetActorLocation(), Physical);
    Row->SetBoolField(TEXT("catalogResolved"), bResolved);
    Row->SetObjectField(TEXT("physical"), WalkContext(Physical));

    // 실시간 trace와 movement floor를 별도로 기록해 표식만 있는 가짜 지원면을 구분한다.
    FHitResult Hit;
    bool bHit = false;
    if (World && Hero && Capsule)
    {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FableFloorWalkSupport), false, Hero);
        bHit = World->LineTraceSingleByChannel(Hit, Hero->GetActorLocation(),
                                               Hero->GetActorLocation() -
                                                   FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 100),
                                               ECC_Visibility, Query);
    }
    const auto* Support = bHit ? Hit.GetComponent() : nullptr;
    auto SupportRow = MakeShared<FJsonObject>();
    SupportRow->SetBoolField(TEXT("hit"), bHit && Hit.bBlockingHit && Support);
    SupportRow->SetStringField(TEXT("actor"), Hit.GetActor() ? Hit.GetActor()->GetName() : FString());
    SupportRow->SetStringField(TEXT("component"), Support ? Support->GetName() : FString());
    SupportRow->SetArrayField(TEXT("impact"), WalkVector(Hit.ImpactPoint));
    SupportRow->SetBoolField(TEXT("stairsTagged"),
                             Support && Support->ComponentHasTag(FableTownOverlook::StairSupportTag()));
    SupportRow->SetBoolField(TEXT("landingTagged"),
                             Support && Support->ComponentHasTag(FableTownOverlook::LandingSupportTag()));
    SupportRow->SetBoolField(TEXT("guardTagged"),
                             Support && Support->ComponentHasTag(FableTownOverlook::GuardSupportTag()));
    SupportRow->SetBoolField(TEXT("meadowTagged"), Support && Support->ComponentHasTag(TEXT("Meadow")));
    SupportRow->SetBoolField(TEXT("matchesMovementFloor"),
                             Support && CharacterMovement &&
                                 CharacterMovement->CurrentFloor.HitResult.GetComponent() == Support);
    SupportRow->SetNumberField(TEXT("capsuleFloorClearance"), Hero && Capsule && Support
                                                                  ? Hero->GetActorLocation().Z -
                                                                        Capsule->GetScaledCapsuleHalfHeight() -
                                                                        Hit.ImpactPoint.Z
                                                                  : -1000);
    Row->SetObjectField(TEXT("support"), SupportRow);

    // 소유자만 받는 상태는 본인 PC 또는 권한 서버에서만 읽고 nonce 값은 출력하지 않는다.
    FFableTownConnectionSnapshot Connection;
    bool bConnection = false;
    if (bOwnerProfile)
    {
        if (Hero && Hero->HasAuthority())
        {
            bConnection = Manager && Manager->GetConnectionSnapshot(ProfileState, Connection);
        }
        else if (PC && PC->IsLocalController() && PC->GetPlayerState<AFablePlayerState>() == ProfileState)
        {
            Connection = ProfileState->GetTownConnectionSnapshot();
            bConnection = Connection.WorldEpoch.IsValid() && Connection.OwnerConnectionGeneration > 0;
        }
    }
    auto Presence = MakeShared<FJsonObject>();
    Presence->SetBoolField(TEXT("available"), bConnection);
    Presence->SetBoolField(TEXT("connectionNonceValid"), bConnection && Connection.ConnectionNonce.IsValid());
    Presence->SetBoolField(
        TEXT("currentConnectionVerified"),
        bConnection && Hero && Hero->HasAuthority() && PC && Manager &&
            Manager->IsCurrentConnection(PC, Connection.OwnerConnectionGeneration, Connection.ConnectionNonce));
    Presence->SetNumberField(TEXT("ownerGeneration"), bConnection ? double(Connection.OwnerConnectionGeneration) : 0);
    Presence->SetNumberField(TEXT("revision"), bConnection ? double(Connection.PresenceRevision) : 0);
    Presence->SetObjectField(TEXT("physical"), WalkContext(Connection.PhysicalContext));
    Row->SetObjectField(TEXT("presence"), Presence);

    if (bIncludeOwnerState && PC)
    {
        FVector Target = FVector::ZeroVector;
        FString Label;
        FFableTownNavigationGuide Guide;
        const bool bTarget = PC->GetNavigationTarget(Target, Label, &Guide);
        FFableTownNavigationGuide ServiceGuide;
        const auto ServiceStatus =
            FableTownNavigation::ResolveServiceGuide(Catalog, Hero ? Hero->GetActorLocation() : FVector::ZeroVector,
                                                     EFableTownServiceId::QuestGuide, ServiceGuide);
        auto Navigation = MakeShared<FJsonObject>();
        Navigation->SetBoolField(TEXT("targetFound"), bTarget);
        Navigation->SetBoolField(TEXT("valid"), Guide.bValid);
        Navigation->SetNumberField(TEXT("status"), int32(Guide.Status));
        Navigation->SetNumberField(TEXT("serviceQueryStatus"), int32(ServiceStatus));
        Navigation->SetBoolField(TEXT("viaConnector"), Guide.bViaConnector);
        Navigation->SetStringField(TEXT("connectorId"), Guide.ConnectorId.ToString());
        Navigation->SetStringField(TEXT("sourceFloorId"), Guide.SourceFloorId.ToString());
        Navigation->SetStringField(TEXT("nextFloorId"), Guide.NextFloorId.ToString());
        Navigation->SetStringField(TEXT("targetFloorId"), Guide.TargetFloorId.ToString());
        Navigation->SetNumberField(TEXT("remainingSteps"), Guide.RemainingSteps);
        Navigation->SetArrayField(TEXT("waypoint"), WalkVector(Guide.WaypointWorld));
        Navigation->SetArrayField(TEXT("target"), WalkVector(Guide.TargetWorld));
        Navigation->SetStringField(TEXT("label"), Label);
        Navigation->SetStringField(TEXT("progressText"), FableTownNavigation::RouteProgressText(Guide));
        Row->SetObjectField(TEXT("guide"), Navigation);
    }
    return Row;
}

/** 서버 표본은 실제 현재 연결의 pawn만 읽고, 이동·저장 함수를 호출하지 않는다. */
void UFableOnlineValidationDriver::ObserveFloorWalkServer(UWorld* World)
{
    const double Now = FPlatformTime::Seconds();
    if (!World || World->GetNetMode() != NM_DedicatedServer || Now - FloorWalkServerSampleAt < .2)
    {
        return;
    }
    FloorWalkServerSampleAt = Now;
    if (!FloorWalkServerReport)
    {
        FloorWalkServerReport = MakeShared<FJsonObject>();
        FloorWalkServerReport->SetNumberField(TEXT("schemaVersion"), 1);
        FloorWalkServerReport->SetStringField(TEXT("phase"), FPaths::GetCleanFilename(Directory));
        FloorWalkServerReport->SetStringField(TEXT("connectorId"), FableTownOverlook::ConnectorId().ToString());
        FloorWalkServerReport->SetBoolField(TEXT("observationOnly"), true);
    }

    TArray<TSharedPtr<FJsonValue>> Latest;
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const auto* PC = Cast<AFablePlayerController>(It->Get());
        const auto* ProfileState = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        const auto* Hero = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;
        if (!ProfileState || !Hero || !ProfileState->bProfileLoaded || !ProfileState->IsBackendReady())
        {
            continue;
        }
        if (FloorWalkServerSamples.Num() >= 2400)
        {
            Finish(false, TEXT("Floor walk server observation limit exceeded."));
            return;
        }
        auto Sample = FloorWalkPawnSnapshot(Hero, PC, true);
        Sample->SetNumberField(TEXT("sequence"), ++FloorWalkServerSequence);
        Sample->SetNumberField(TEXT("processSeconds"), Now);
        Sample->SetNumberField(TEXT("worldSeconds"), World->GetTimeSeconds());
        FloorWalkServerSamples.Add(MakeShared<FJsonValueObject>(Sample));
        Latest.Add(MakeShared<FJsonValueObject>(Sample));
    }
    FloorWalkServerReport->SetArrayField(TEXT("latest"), Latest);
}

/** 멀리서는 W, 단 중앙에서는 감속한 일반 입력을 사용하며 movement 판정을 바꾸지 않는다. */
bool UFableOnlineValidationDriver::ApplyFloorWalkInput(AFablePlayerController* PC, AFableCharacter* Hero,
                                                       const FVector& Destination)
{
    const FVector Delta = Destination - Hero->GetActorLocation();
    DungeonHold(PC, EKeys::S, false);
    if (Delta.Size2D() <= 18)
    {
        DungeonHold(PC, EKeys::W, false);
        return true;
    }

    FloorWalkStableAt = 0;
    PC->SetControlRotation(FRotator(-12, Delta.Rotation().Yaw, 0));
    const bool bFar = Delta.Size2D() > 180;
    DungeonHold(PC, EKeys::W, bFar);
    if (!bFar)
    {
        Hero->AddMovementInput(FVector(Delta.X, Delta.Y, 0).GetSafeNormal(),
                               FMath::Clamp(float(Delta.Size2D() / 180), .12f, .7f));
    }
    return false;
}

/** 入力で実階段を歩き、物理到着と同じ案内の変化を待つ。XYZ要求やカタログ差替えは使わない。 */
bool UFableOnlineValidationDriver::TickFloorWalk(AFablePlayerController* PC, AFablePlayerState* P,
                                                 AFableCharacter* Hero, double Now)
{
    if (!PC || !P || !Hero || !Hero->IsAlive() || !P->IsBackendReady() || P->IsBackendSavePending() ||
        !P->bLastSaveSucceeded || P->GetBackendRevision() != P->GetBackendAcknowledgedRevision())
    {
        if (PC)
        {
            DungeonHold(PC, EKeys::W, false);
        }
        FloorWalkStableAt = 0;
        return false;
    }
    PC->UIClosePanels();
    if (!FloorWalkReport)
    {
        FloorWalkStarted = Now;
        FloorWalkEntryX = Hero->GetActorLocation().X;
        FloorWalkReport = MakeShared<FJsonObject>();
        FloorWalkReport->SetNumberField(TEXT("schemaVersion"), 1);
        FloorWalkReport->SetStringField(TEXT("mode"), Mode);
        FloorWalkReport->SetStringField(TEXT("playerId"), P->GetBackendPlayerId());
        FloorWalkReport->SetStringField(TEXT("publicPlayerKey"), P->PartyPlayerKey);
        FloorWalkReport->SetStringField(TEXT("connectorId"), FableTownOverlook::ConnectorId().ToString());
        FloorWalkReport->SetObjectField(TEXT("beforeProfile"), Profile(P));
        FloorWalkReport->SetNumberField(TEXT("initialEntryX"), FloorWalkEntryX);
        FloorWalkReport->SetBoolField(TEXT("usesNormalMovementInput"), true);
        FloorWalkReport->SetBoolField(TEXT("completed"), false);
        FloorWalkReport->SetBoolField(TEXT("newTownGeometryActivated"), false);
    }
    if (Now - FloorWalkStarted > 180)
    {
        DungeonHold(PC, EKeys::W, false);
        Finish(false, TEXT("Floor walk did not reach its exact physical observations in time."));
        return false;
    }

    // 마지막 milestone과 다음 UI 캡처가 같은 frame의 단일 screenshot 요청을 덮어쓰지 않게 기다린다.
    // 最後のPNGが保存されるまで次のUI captureへ進まず、register/login両方の実画像を保つ。
    if (FloorWalkReport->GetBoolField(TEXT("completed")))
    {
        DungeonHold(PC, EKeys::W, false);
        const FString LastMilestone = Mode == TEXT("login") ? TEXT("entry-restored") : TEXT("upper-before-leave");
        const FString LastCapture =
            FPaths::Combine(Directory, Role + TEXT("-floor-walk-") + LastMilestone + TEXT(".png"));
        return !FScreenshotRequest::IsScreenshotRequested() && IFileManager::Get().FileSize(*LastCapture) > 128;
    }

    auto Own = FloorWalkPawnSnapshot(Hero, PC, true);
    if (Now - FloorWalkSampleAt >= .2)
    {
        FloorWalkSampleAt = Now;
        if (FloorWalkSamples.Num() >= 1200)
        {
            Finish(false, TEXT("Floor walk client observation limit exceeded."));
            return false;
        }
        auto Sample = MakeShared<FJsonObject>();
        Sample->SetNumberField(TEXT("sequence"), ++FloorWalkSequence);
        Sample->SetNumberField(TEXT("processSeconds"), Now);
        Sample->SetNumberField(TEXT("step"), FloorWalkStep);
        Sample->SetNumberField(TEXT("approachStep"), FloorWalkApproachStep);
        Sample->SetObjectField(TEXT("own"), Own);
        TArray<TSharedPtr<FJsonValue>> Peers;
        for (TActorIterator<AFableCharacter> It(Hero->GetWorld()); It; ++It)
        {
            const auto* Peer = It->GetPlayerState<AFablePlayerState>();
            if (*It != Hero && Peer && Peer->bProfileLoaded && !Peer->PartyPlayerKey.IsEmpty())
            {
                Peers.Add(MakeShared<FJsonValueObject>(FloorWalkPawnSnapshot(*It, nullptr, false)));
            }
        }
        Sample->SetArrayField(TEXT("peers"), Peers);
        FloorWalkSamples.Add(MakeShared<FJsonValueObject>(Sample));
    }

    const auto Mark = [&](const TCHAR* Name)
    {
        for (const auto& Existing : FloorWalkMilestones)
        {
            if (Existing->AsObject()->GetStringField(TEXT("name")) == Name)
            {
                return;
            }
        }
        auto Milestone = MakeShared<FJsonObject>();
        Milestone->SetStringField(TEXT("name"), Name);
        Milestone->SetStringField(TEXT("playerId"), P->GetBackendPlayerId());
        Milestone->SetStringField(TEXT("publicPlayerKey"), P->PartyPlayerKey);
        Milestone->SetNumberField(TEXT("sequence"), FloorWalkSequence);
        Milestone->SetNumberField(TEXT("processSeconds"), Now);
        Milestone->SetObjectField(TEXT("own"), Own);
        FloorWalkMilestones.Add(MakeShared<FJsonValueObject>(Milestone));
        Capture(FString(TEXT("floor-walk-")) + Name);
    };

    const auto Settled = [&](FName Floor, FName Connector, FName SupportTag)
    {
        const auto Context = Own->GetObjectField(TEXT("physical"));
        const auto Support = Own->GetObjectField(TEXT("support"));
        const auto Presence = Own->GetObjectField(TEXT("presence"));
        const auto Replicated = Presence->GetObjectField(TEXT("physical"));
        const bool bTagged =
            SupportTag.IsNone() ||
            (SupportTag == FableTownOverlook::StairSupportTag() && Support->GetBoolField(TEXT("stairsTagged"))) ||
            (SupportTag == FableTownOverlook::LandingSupportTag() && Support->GetBoolField(TEXT("landingTagged")));
        const double Clearance = Support->GetNumberField(TEXT("capsuleFloorClearance"));
        return Own->GetBoolField(TEXT("catalogResolved")) && Own->GetBoolField(TEXT("grounded")) &&
               Own->GetBoolField(TEXT("movementFloorWalkable")) && Hero->GetVelocity().Size2D() <= 8 &&
               Support->GetBoolField(TEXT("hit")) && Support->GetBoolField(TEXT("matchesMovementFloor")) &&
               !Support->GetBoolField(TEXT("guardTagged")) && bTagged && Clearance >= -.5 && Clearance <= 8 &&
               Context->GetStringField(TEXT("floorId")) == Floor.ToString() &&
               Context->GetStringField(TEXT("connectorId")) == Connector.ToString() &&
               Presence->GetBoolField(TEXT("available")) && Presence->GetBoolField(TEXT("connectionNonceValid")) &&
               Replicated->GetStringField(TEXT("floorId")) == Floor.ToString() &&
               Replicated->GetStringField(TEXT("connectorId")) == Connector.ToString();
    };

    if (Mode == TEXT("login"))
    {
        FTransform Entry;
        const auto* Catalog = FableTownNavigation::CatalogFor(Hero);
        FFableTownAnchorRef Reference;
        Reference.TownId = EFableTownId::LegacyVillage;
        Reference.AnchorId = TEXT("Entry");
        const auto* Legacy = Catalog ? Catalog->FindTown(EFableTownId::LegacyVillage) : nullptr;
        Reference.CatalogVersion = Legacy ? Legacy->CatalogVersion : 0;
        const bool bEntry = Catalog && Catalog->ResolveAnchor(Reference, 0, Entry) &&
                            FVector::Dist2D(Hero->GetActorLocation(), Entry.GetLocation()) < 600 &&
                            P->GetLastSafeTownAnchor().AnchorId == TEXT("Entry");
        if (!bEntry || !Settled(TEXT("Ground"), NAME_None, NAME_None))
        {
            return false;
        }
        Mark(TEXT("entry-restored"));
        FloorWalkReport->SetObjectField(TEXT("afterProfile"), Profile(P));
        FloorWalkReport->SetBoolField(TEXT("completed"), true);
        FloorWalkReport->SetBoolField(TEXT("entryPolicyRestored"), true);
        Checks.Add(TEXT("floor_walk_entry_policy_restored"), true);
        return false;
    }

    const double Lane = FableTownOverlook::CenterY + (Role == TEXT("client1") ? -90 : 90);
    if (FloorWalkApproachStep < 3)
    {
        // 기존 나무·바위를 삭제하지 않고 중앙 대각선으로 접근한다. 공통 접근점은 입력 순서만 분리한다.
        if (Role == TEXT("client2") && FloorWalkApproachStep == 0)
        {
            const auto Peer = ReadWalkReport(FPaths::Combine(Directory, TEXT("client1.json")));
            const TSharedPtr<FJsonObject>* Walk = nullptr;
            double ApproachStep = 0;
            if (!Peer || !Peer->TryGetObjectField(TEXT("floorWalk"), Walk) ||
                !(*Walk)->TryGetNumberField(TEXT("approachStep"), ApproachStep) || ApproachStep < 3)
            {
                DungeonHold(PC, EKeys::W, false);
                return false;
            }
        }
        const FVector Approach[] = {FVector(2000, FableTownOverlook::CenterY, 0),
                                    FVector(2200, FableTownOverlook::CenterY, 0), FVector(2200, Lane, 0)};
        if (!ApplyFloorWalkInput(PC, Hero, Approach[FloorWalkApproachStep]) ||
            !Settled(TEXT("Ground"), NAME_None, NAME_None))
        {
            FloorWalkStableAt = 0;
            return false;
        }
        if (FloorWalkStableAt == 0)
        {
            FloorWalkStableAt = Now;
        }
        if (Now - FloorWalkStableAt >= .4)
        {
            ++FloorWalkApproachStep;
            FloorWalkStableAt = 0;
        }
        return false;
    }
    const FVector Positions[] = {FVector(FableTownOverlook::StairStartX - 200, Lane, 0),
                                 FVector(FableTownOverlook::StairStartX + 11.5 * FableTownOverlook::StepRunCm, Lane, 0),
                                 FVector(FableTownOverlook::StairEndX - 2.5 * FableTownOverlook::StepRunCm, Lane, 0),
                                 FVector(FableTownOverlook::OverlookAnchorPosition().X, Lane, 0),
                                 FVector(FableTownOverlook::StairEndX - 2.5 * FableTownOverlook::StepRunCm, Lane, 0),
                                 FVector(FableTownOverlook::StairStartX + 11.5 * FableTownOverlook::StepRunCm, Lane, 0),
                                 FVector(FableTownOverlook::StairStartX - 200, Lane, 0),
                                 FVector(FableTownOverlook::OverlookAnchorPosition().X, Lane, 0)};
    const TCHAR* Labels[] = {TEXT("ground-start"),  TEXT("ground-stairs-up"),  TEXT("connector-up"),
                             TEXT("upper"),         TEXT("connector-down"),    TEXT("ground-stairs-down"),
                             TEXT("ground-return"), TEXT("upper-before-leave")};
    const FName Floors[] = {TEXT("Ground"), TEXT("Ground"), NAME_None,      FableTownOverlook::FloorId(),
                            NAME_None,      TEXT("Ground"), TEXT("Ground"), FableTownOverlook::FloorId()};
    const bool bConnector = FloorWalkStep == 2 || FloorWalkStep == 4;
    const FName SupportTag = FloorWalkStep == 0 || FloorWalkStep == 6   ? NAME_None
                             : FloorWalkStep == 3 || FloorWalkStep == 7 ? FableTownOverlook::LandingSupportTag()
                                                                        : FableTownOverlook::StairSupportTag();
    if (FloorWalkStep >= UE_ARRAY_COUNT(Positions))
    {
        return true;
    }

    if (!ApplyFloorWalkInput(PC, Hero, Positions[FloorWalkStep]))
    {
        return false;
    }
    if (!Settled(Floors[FloorWalkStep], bConnector ? FableTownOverlook::ConnectorId() : NAME_None, SupportTag))
    {
        FloorWalkStableAt = 0;
        return false;
    }

    const auto Guide = Own->GetObjectField(TEXT("guide"));
    const bool bGuide =
        bConnector ? !Guide->GetBoolField(TEXT("valid")) && !Guide->GetBoolField(TEXT("targetFound")) &&
                         Guide->GetIntegerField(TEXT("serviceQueryStatus")) ==
                             int32(EFableTownRouteStatus::ConnectorUnavailable)
                   : Guide->GetBoolField(TEXT("valid")) && Guide->GetBoolField(TEXT("viaConnector")) ==
                                                               (Floors[FloorWalkStep] == FableTownOverlook::FloorId());
    if (!bGuide)
    {
        FloorWalkStableAt = 0;
        return false;
    }
    if (FloorWalkStableAt == 0)
    {
        FloorWalkStableAt = Now;
    }
    if (Now - FloorWalkStableAt < .8)
    {
        return false;
    }

    Mark(Labels[FloorWalkStep]);
    if (FloorWalkStep == 3 || FloorWalkStep == 7)
    {
        const auto Peer = ReadWalkReport(
            FPaths::Combine(Directory, Role == TEXT("client1") ? TEXT("client2.json") : TEXT("client1.json")));
        if (!HasWalkMilestone(Peer, Labels[FloorWalkStep], P->PartyPlayerKey))
        {
            return false;
        }
    }
    ++FloorWalkStep;
    FloorWalkStableAt = 0;
    if (FloorWalkStep == UE_ARRAY_COUNT(Positions))
    {
        FloorWalkReport->SetObjectField(TEXT("afterProfile"), Profile(P));
        FloorWalkReport->SetBoolField(TEXT("completed"), true);
        FloorWalkReport->SetBoolField(TEXT("leaveFromUpperWithEntryAnchor"),
                                      P->GetLastSafeTownAnchor().AnchorId == TEXT("Entry"));
        Checks.Add(TEXT("floor_walk_actual_round_trip"), true);
        return false;
    }
    return false;
}
