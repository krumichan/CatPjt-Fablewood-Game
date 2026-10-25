#include "FableOnlineValidationDriver.h"
#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableTownManager.h"
#include "FableTownCatalog.h"
#include "FableTownTravelCoordinator.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END

namespace
{
    /** 논리 시도 이름은 원시 요청과 응답을 결합하는 고정 순서다. */
    const TCHAR* TownAttemptNames[] = {TEXT("sunbrook"),        TEXT("starbough"),       TEXT("snowbloom"),
                                       TEXT("eldergrove"),      TEXT("unknownAnchor"),   TEXT("staleCatalog"),
                                       TEXT("loadedFalse"),     TEXT("cancelled"),       TEXT("wrongChallenge"),
                                       TEXT("wrongConnection"), TEXT("wrongGeneration"), TEXT("success")};

    /** nonce 원문을 내보내지 않고 정규 GUID UTF-8의 SHA256만 기록한다. */
    FString TownNonceDigest(FGuid Value)
    {
        const FTCHARToUTF8 Bytes(*Value.ToString(EGuidFormats::DigitsWithHyphensLower));
        uint8 Digest[SHA256_DIGEST_LENGTH];
        if (!SHA256(reinterpret_cast<const uint8*>(Bytes.Get()), static_cast<size_t>(Bytes.Length()), Digest))
        {
            return FString();
        }
        return BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower();
    }

    /** 기존 검증과 같은 정규 식별자 표기를 사용한다. */
    FString TownGuid(FGuid Value)
    {
        return Value.ToString(EGuidFormats::DigitsWithHyphensLower);
    }

    /** 실제 enum 이름을 사용해 거절 사유를 임의로 바꾸지 않는다. */
    template <typename T> FString TownEnum(T Value)
    {
        return StaticEnum<T>()->GetNameStringByValue(static_cast<int64>(Value));
    }

    /** 공개 월드 좌표만 숫자 배열로 기록한다. */
    TArray<TSharedPtr<FJsonValue>> TownVector(FVector Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
                MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** 동일 실행 폴더의 공개 보고서만 읽는다. */
    TSharedPtr<FJsonObject> ReadTownReport(const FString& Path)
    {
        FString Bytes;
        TSharedPtr<FJsonObject> Result;
        if (!FFileHelper::LoadFileToString(Bytes, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Bytes), Result))
        {
            return nullptr;
        }
        return Result;
    }

    /** 마지막 응답이 앞선 시도별 증거를 덮지 않게 배열에 보존한다. */
    void AppendTownObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Name, TSharedPtr<FJsonObject> Value)
    {
        const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
        TArray<TSharedPtr<FJsonValue>> Values;
        if (Parent->TryGetArrayField(Name, Existing))
        {
            Values = *Existing;
        }
        Values.Add(MakeShared<FJsonValueObject>(Value));
        Parent->SetArrayField(Name, Values);
    }

    /** 이동 목적지는 catalog의 논리 앵커로만 표현한다. */
    TSharedRef<FJsonObject> TownAnchor(const FFableTownAnchorRef& Anchor)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetNumberField(TEXT("townId"), static_cast<int32>(Anchor.TownId));
        Result->SetStringField(TEXT("anchorId"), Anchor.AnchorId.ToString());
        Result->SetNumberField(TEXT("catalogVersion"), Anchor.CatalogVersion);
        return Result;
    }

    /** 성공 영수증의 실제 수락 위치와 revision을 재전송 비교에 사용한다. */
    TSharedRef<FJsonObject> TownCommitJson(const FFableTownTravelCommitReceipt& Receipt)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("transactionId"), TownGuid(Receipt.TransactionId));
        Result->SetNumberField(TEXT("generation"), static_cast<double>(Receipt.Generation));
        Result->SetNumberField(TEXT("commitSequence"), static_cast<double>(Receipt.CommitSequence));
        Result->SetObjectField(TEXT("targetAnchor"), TownAnchor(Receipt.TargetAnchor));
        Result->SetArrayField(TEXT("acceptedPosition"), TownVector(Receipt.AcceptedTransform.GetLocation()));
        Result->SetArrayField(TEXT("acceptedRotation"), TownVector(Receipt.AcceptedTransform.Rotator().Euler()));
        Result->SetNumberField(TEXT("presenceRevision"), static_cast<double>(Receipt.PresenceRevision));
        Result->SetNumberField(TEXT("safeCheckpointRevision"), static_cast<double>(Receipt.SafeCheckpointRevision));
        Result->SetNumberField(TEXT("geometryEntryEpoch"), static_cast<double>(Receipt.GeometryEntryEpoch));
        Result->SetNumberField(TEXT("committedAtServerTime"), Receipt.CommittedAtServerTime);
        Result->SetNumberField(TEXT("commitCount"), Receipt.CommitCount);
        return Result;
    }

    /** 실제 RPC 응답은 요청/시도 식별자와 함께 남긴다. */
    TSharedRef<FJsonObject> TownResponseJson(const FFableTownTravelResponse& Response)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("requestId"), TownGuid(Response.RequestId));
        Result->SetStringField(TEXT("attemptId"), TownGuid(Response.AttemptId));
        Result->SetStringField(TEXT("transactionId"), TownGuid(Response.TransactionId));
        Result->SetNumberField(TEXT("generation"), static_cast<double>(Response.Generation));
        Result->SetStringField(TEXT("operation"), TownEnum(Response.Operation));
        Result->SetStringField(TEXT("state"), TownEnum(Response.State));
        Result->SetStringField(TEXT("error"), TownEnum(Response.Error));
        Result->SetNumberField(TEXT("eventSequence"), static_cast<double>(Response.EventSequence));
        Result->SetBoolField(TEXT("ownArrivalConfirmed"), Response.bOwnArrivalConfirmed);
        Result->SetObjectField(TEXT("commit"), TownCommitJson(Response.Commit));
        return Result;
    }

    /** 발신자별 순서와 서버 시각을 사용하며 UTC로 인과관계를 추정하지 않는다. */
    TSharedRef<FJsonObject> TownEventJson(const FFableTownTravelEvent& Event)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("emitter"), Event.bAuthority ? TEXT("authority") : TEXT("client"));
        Result->SetNumberField(TEXT("sequence"), static_cast<double>(Event.Sequence));
        Result->SetStringField(TEXT("kind"), TownEnum(Event.Kind));
        Result->SetStringField(TEXT("requestId"), TownGuid(Event.RequestId));
        Result->SetStringField(TEXT("attemptId"), TownGuid(Event.AttemptId));
        Result->SetStringField(TEXT("transactionId"), TownGuid(Event.TransactionId));
        Result->SetNumberField(TEXT("generation"), static_cast<double>(Event.Generation));
        Result->SetStringField(TEXT("state"), TownEnum(Event.State));
        Result->SetStringField(TEXT("error"), TownEnum(Event.Error));
        Result->SetNumberField(TEXT("serverTime"), Event.ServerTime);
        return Result;
    }

    /** nonce는 digest로만 공개하며 원래 TTL을 기록한다. */
    TSharedRef<FJsonObject> TownPreparedJson(const FFableTownTravelPrepared& Prepared)
    {
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("requestId"), TownGuid(Prepared.RequestId));
        Result->SetStringField(TEXT("transactionId"), TownGuid(Prepared.TransactionId));
        Result->SetNumberField(TEXT("generation"), static_cast<double>(Prepared.Generation));
        Result->SetNumberField(TEXT("ownerConnectionGeneration"),
                               static_cast<double>(Prepared.OwnerConnectionGeneration));
        Result->SetStringField(TEXT("connectionNonceDigest"), TownNonceDigest(Prepared.ConnectionNonce));
        Result->SetStringField(TEXT("challengeNonceDigest"), TownNonceDigest(Prepared.ChallengeNonce));
        Result->SetNumberField(TEXT("sourcePresenceRevision"), static_cast<double>(Prepared.SourcePresenceRevision));
        Result->SetNumberField(TEXT("sourceTownId"), static_cast<int32>(Prepared.SourceContext.TownId));
        Result->SetStringField(TEXT("sourceFloorId"), Prepared.SourceContext.FloorId.ToString());
        Result->SetNumberField(TEXT("sourceCatalogVersion"), Prepared.SourceContext.CatalogVersion);
        Result->SetBoolField(TEXT("sourceGeometryReady"), Prepared.SourceContext.bGeometryReady);
        Result->SetStringField(TEXT("sourcePublicAreaId"), Prepared.SourcePublicArea.AreaId.ToString());
        Result->SetStringField(TEXT("sourcePublicScopeId"), TownGuid(Prepared.SourcePublicArea.ScopeId));
        Result->SetObjectField(TEXT("targetAnchor"), TownAnchor(Prepared.TargetAnchor));
        Result->SetStringField(TEXT("descriptorKind"), TownEnum(Prepared.Geometry.Mode));
        Result->SetNumberField(TEXT("expiresAtServerTime"), Prepared.ExpiresAtServerTime);
        return Result;
    }

    /** 현재 시도의 객체가 없으면 성공을 추정하지 않는다. */
    TSharedPtr<FJsonObject> TownAttempt(const TSharedPtr<FJsonObject>& Report, int32 AttemptIndex)
    {
        const TArray<TSharedPtr<FJsonValue>>* Attempts = nullptr;
        if (!Report || !Report->TryGetArrayField(TEXT("attempts"), Attempts) || !Attempts->IsValidIndex(AttemptIndex))
        {
            return nullptr;
        }
        return (*Attempts)[AttemptIndex]->AsObject();
    }
}

/** 월드/PC 수명 종료 전에 정확한 본인 delegate만 해제한다. */
void UFableOnlineValidationDriver::UnbindTownTravel()
{
    if (auto* BoundPlayer = TownBoundPlayer.Get())
    {
        BoundPlayer->OnTownTravelPrepared().RemoveAll(this);
        BoundPlayer->OnTownTravelResponse().RemoveAll(this);
        BoundPlayer->OnTownTravelProgress().RemoveAll(this);
    }
    if (auto* Coordinator = TownBoundCoordinator.Get())
    {
        Coordinator->OnAuthorityTravelEvent().RemoveAll(this);
    }
    TownBoundPlayer.Reset();
    TownBoundCoordinator.Reset();
}

/** 연결·성장·절대 전투 시각·필드 정본을 같은 순간에 읽기 전용으로 고정한다. */
TSharedPtr<FJsonObject> UFableOnlineValidationDriver::TownTravelSnapshot(UWorld* World) const
{
    const auto* Manager = AFableTownManager::Find(World);
    const auto* ServerMode = World ? World->GetAuthGameMode<AFableGameMode>() : nullptr;
    const auto Field = FableAdventure::Field(World);
    if (!World || !Manager || !ServerMode || !Field || !World->GetGameState())
    {
        return nullptr;
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("worldId"), World->GetUniqueID());
    Result->SetStringField(TEXT("worldName"), World->GetOutermost()->GetName());
    Result->SetNumberField(TEXT("gameModeId"), ServerMode->GetUniqueID());
    Result->SetNumberField(TEXT("gameStateId"), World->GetGameState()->GetUniqueID());
    Result->SetStringField(TEXT("fieldScopeId"), TownGuid(Manager->GetPublicFieldScopeId()));

    // 공유 필드의 전체 진행 값과 감소시켜 표시하지 않은 절대 복구 시각을 보존한다.
    auto ProgressJson = MakeShared<FJsonObject>();
    FJsonObjectConverter::UStructToJsonObject(FFableAdventureProgress::StaticStruct(), Field.operator->(), ProgressJson,
                                              0, 0);
    ProgressJson->SetBoolField(TEXT("objectiveComplete"), Field->bObjectiveComplete);
    double Recovery = 0, Rescue = 0;
    ServerMode->GetPublicFieldRecoveryDeadlines(Recovery, Rescue);
    ProgressJson->SetNumberField(TEXT("nextRecoveryAttemptTime"), Recovery);
    ProgressJson->SetNumberField(TEXT("nextIndividualRescueTime"), Rescue);
    Result->SetObjectField(TEXT("field"), ProgressJson);

    // 등록 명부와 살아 있는 적의 식별자는 현재 명시 범위에서만 읽는다.
    TArray<TSharedPtr<FJsonValue>> Roster;
    for (APlayerState* Entry : Manager->GetSharedForestParticipants())
    {
        if (const auto* ProfileState = Cast<AFablePlayerState>(Entry))
        {
            Roster.Add(MakeShared<FJsonValueString>(ProfileState->PartyPlayerKey));
        }
    }
    Roster.Sort(
        [](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
        {
            return Left->AsString() < Right->AsString();
        });
    Result->SetArrayField(TEXT("publicRoster"), Roster);
    TArray<TSharedPtr<FJsonValue>> Enemies;
    for (TActorIterator<AFableEnemy> It(World); It; ++It)
    {
        if (It->IsAlive() && !It->GetExpeditionId().IsValid())
        {
            auto Enemy = MakeShared<FJsonObject>();
            Enemy->SetStringField(TEXT("spawnId"), TownGuid(It->GetSpawnId()));
            Enemy->SetNumberField(TEXT("health"), It->GetHealth());
            Enemies.Add(MakeShared<FJsonValueObject>(Enemy));
        }
    }
    Result->SetArrayField(TEXT("livingFieldEnemies"), Enemies);
    Result->SetNumberField(TEXT("streamingLevelCount"), World->GetStreamingLevels().Num());

    // 미채택 마을의 지형 준비와 실제 로드 수를 별도 관찰하며 성공을 부여하지 않는다.
    TArray<TSharedPtr<FJsonValue>> CandidateReadiness;
    for (EFableTownId Id :
         {EFableTownId::Sunbrook, EFableTownId::Starbough, EFableTownId::Snowbloom, EFableTownId::Eldergrove})
    {
        CandidateReadiness.Add(MakeShared<FJsonValueBoolean>(Manager->GetCatalog()->IsGeometryReady(Id)));
    }
    Result->SetArrayField(TEXT("newTownGeometryReady"), CandidateReadiness);
    int32 NewTownLoadedLevels = 0;
    for (const auto* Streaming : World->GetStreamingLevels())
    {
        if (Streaming && Streaming->IsLevelLoaded() &&
            Streaming->GetWorldAssetPackageName().Contains(TEXT("/FablewoodVillagesV001/")))
        {
            ++NewTownLoadedLevels;
        }
    }
    Result->SetNumberField(TEXT("newTownLoadedLevels"), NewTownLoadedLevels);
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const auto* Player = It->Get();
        const auto* ProfileState = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        const auto* Hero = Player ? Cast<AFableCharacter>(Player->GetPawn()) : nullptr;
        FFableTownConnectionSnapshot Connection;
        if (!ProfileState || !Hero || !Manager->GetConnectionSnapshot(ProfileState, Connection))
        {
            continue;
        }
        const TCHAR* MemberName = ProfileState->HeroClass == EFableClass::Mage     ? TEXT("a")
                                  : ProfileState->HeroClass == EFableClass::Archer ? TEXT("b")
                                                                                   : nullptr;
        if (!MemberName)
        {
            continue;
        }
        auto Member = MakeShared<FJsonObject>();

        // 기존 전송 스냅숏에 모든 직업·이야기·퀘스트 진행을 더해 경제를 비교한다.
        auto Economy = Profile(ProfileState);
        Economy->SetNumberField(TEXT("questKills"), ProfileState->QuestKills);
        Economy->SetNumberField(TEXT("moonwellQuest"), static_cast<int32>(ProfileState->MoonwellQuest));
        Economy->SetNumberField(TEXT("moonwellCompletions"), ProfileState->MoonwellCompletions);
        Economy->SetNumberField(TEXT("storyChapter"), ProfileState->StoryChapter);
        Economy->SetNumberField(TEXT("storyObjectMask"), ProfileState->StoryObjectMask);
        Economy->SetNumberField(TEXT("storySealMask"), ProfileState->StorySealMask);
        TArray<TSharedPtr<FJsonValue>> Classes;
        for (const auto& Entry : ProfileState->ClassProgress)
        {
            auto Class = MakeShared<FJsonObject>();
            FJsonObjectConverter::UStructToJsonObject(FFableClassProgress::StaticStruct(), &Entry, Class, 0, 0);
            Classes.Add(MakeShared<FJsonValueObject>(Class));
        }
        Economy->SetArrayField(TEXT("classProgress"), Classes);
        Member->SetObjectField(TEXT("economy"), Economy);
        Member->SetStringField(TEXT("publicPlayerKey"), ProfileState->PartyPlayerKey);
        Member->SetStringField(TEXT("privateExpeditionId"), TownGuid(ProfileState->ActiveExpeditionId));
        Member->SetNumberField(TEXT("controllerId"), Player->GetUniqueID());
        Member->SetNumberField(TEXT("playerStateId"), ProfileState->GetUniqueID());
        Member->SetNumberField(TEXT("pawnId"), Hero->GetUniqueID());
        Member->SetArrayField(TEXT("position"), TownVector(Hero->GetActorLocation()));
        Member->SetArrayField(TEXT("rotation"), TownVector(Hero->GetActorRotation().Euler()));
        Member->SetArrayField(TEXT("velocity"), TownVector(Hero->GetVelocity()));
        Member->SetNumberField(TEXT("mana"), Hero->GetMana());
        Member->SetNumberField(TEXT("potionCooldownEnd"), ProfileState->GetPotionCooldownEndServerTime());
        Member->SetNumberField(TEXT("noticeSerial"), ProfileState->NoticeSerial);
        Member->SetStringField(TEXT("lastNotice"), ProfileState->LastNotice);
        Member->SetNumberField(TEXT("maxMana"), Hero->GetMaxMana());
        Member->SetBoolField(TEXT("alive"), Hero->IsAlive());
        const auto* MovementComponent = Hero->GetCharacterMovement();
        Member->SetBoolField(TEXT("onGround"), MovementComponent && MovementComponent->IsMovingOnGround());
        Member->SetNumberField(TEXT("movementMode"),
                               MovementComponent ? static_cast<int32>(MovementComponent->MovementMode) : -1);
        const auto Combat = Hero->GetTownCombatState();

        // 남은 시간이 아닌 절대 cooldown과 안전 위치를 분리해 기록한다.
        auto CombatJson = MakeShared<FJsonObject>();
        CombatJson->SetNumberField(TEXT("nextAttack"), Combat.NextAttackServerTime);
        CombatJson->SetNumberField(TEXT("nextSkill"), Combat.NextSkillServerTime);
        CombatJson->SetNumberField(TEXT("nextSecondSkill"), Combat.NextSecondSkillServerTime);
        CombatJson->SetNumberField(TEXT("nextThirdSkill"), Combat.NextThirdSkillServerTime);
        CombatJson->SetNumberField(TEXT("nextDodge"), Combat.NextDodgeServerTime);
        CombatJson->SetNumberField(TEXT("combatBuildEnd"), Combat.CombatBuildEndServerTime);
        CombatJson->SetNumberField(TEXT("shieldAmount"), Combat.ShieldAmount);
        CombatJson->SetNumberField(TEXT("shieldEnd"), Combat.ShieldEndServerTime);
        CombatJson->SetNumberField(TEXT("actionLockEnd"), Combat.ActionLockEndServerTime);
        Member->SetObjectField(TEXT("combat"), CombatJson);
        Member->SetArrayField(TEXT("safeSpawnLocation"), TownVector(Combat.SafeSpawnLocation));
        Member->SetArrayField(TEXT("safeSpawnRotation"), TownVector(Combat.SafeSpawnRotation.Euler()));

        // 연결 nonce는 digest로만 내보내며 revision과 공개 참가 범위를 함께 남긴다.
        Member->SetStringField(TEXT("worldEpoch"), TownGuid(Connection.WorldEpoch));
        Member->SetNumberField(TEXT("ownerConnectionGeneration"),
                               static_cast<double>(Connection.OwnerConnectionGeneration));
        Member->SetStringField(TEXT("connectionNonceDigest"), TownNonceDigest(Connection.ConnectionNonce));
        Member->SetNumberField(TEXT("presenceRevision"), static_cast<double>(Connection.PresenceRevision));
        Member->SetNumberField(TEXT("safeCheckpointRevision"), static_cast<double>(Connection.SafeCheckpointRevision));
        Member->SetObjectField(TEXT("safeAnchor"), TownAnchor(Connection.SafeAnchor));
        Member->SetNumberField(TEXT("physicalTownId"), static_cast<int32>(Connection.PhysicalContext.TownId));
        Member->SetStringField(TEXT("physicalFloorId"), Connection.PhysicalContext.FloorId.ToString());
        Member->SetNumberField(TEXT("physicalAreaKind"), static_cast<int32>(Connection.PhysicalContext.AreaKind));
        Member->SetNumberField(TEXT("physicalCatalogVersion"), Connection.PhysicalContext.CatalogVersion);
        Member->SetBoolField(TEXT("physicalGeometryReady"), Connection.PhysicalContext.bGeometryReady);
        Member->SetStringField(TEXT("physicalConnectorId"), Connection.PhysicalContext.ConnectorId.ToString());
        Member->SetStringField(TEXT("publicScopeId"), TownGuid(ProfileState->GetPublicFieldScopeId()));
        Result->SetObjectField(MemberName, Member);
    }
    return Result;
}

/** 정상 서버 이벤트 직후 상태를 고정하여 same-frame 후속 응답이 지우지 못한다. */
void UFableOnlineValidationDriver::OnTownTravelAuthority(const FFableTownTravelEvent& Event)
{
    if (!ServerTownReport || !ServerTownReport->HasField(TEXT("before")))
    {
        return;
    }
    bool WindowCompleted = false;
    if (ServerTownReport->TryGetBoolField(TEXT("completed"), WindowCompleted) && WindowCompleted)
    {
        return; // 별도의 기존 보스/재접속 흐름이 끝난 관찰 창의 증거를 바꾸지 않게 한다.
    }
    UWorld* World = TownBoundCoordinator.IsValid() ? TownBoundCoordinator->GetWorld() : nullptr;
    auto Snapshot = TownTravelSnapshot(World);
    if (!Snapshot)
    {
        return;
    }
    auto Record = TownEventJson(Event);
    Record->SetObjectField(TEXT("snapshot"), Snapshot);
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        FFableTownTravelPrepared Prepared;
        if (TownBoundCoordinator->GetPendingFor(It->Get(), Prepared) && Prepared.RequestId == Event.RequestId)
        {
            Record->SetObjectField(TEXT("prepared"), TownPreparedJson(Prepared));
        }
        FFableTownTravelResponse Response;
        if (TownBoundCoordinator->GetLastResponseFor(It->Get(), Response) && Response.RequestId == Event.RequestId &&
            Response.AttemptId == Event.AttemptId && Response.EventSequence == Event.Sequence)
        {
            Record->SetObjectField(TEXT("response"), TownResponseJson(Response));
        }
    }
    if (Event.Kind == EFableTownTravelEventKind::Committed)
    {
        FFableTownPlacementEvidence Placement;
        if (TownBoundCoordinator->GetPlacementEvidence(Event.TransactionId, Placement))
        {
            auto Evidence = MakeShared<FJsonObject>();
            Evidence->SetArrayField(TEXT("candidatePosition"), TownVector(Placement.RequestedCandidate.GetLocation()));
            Evidence->SetArrayField(TEXT("acceptedPosition"), TownVector(Placement.AcceptedTransform.GetLocation()));
            Evidence->SetNumberField(TEXT("capsuleRadius"), Placement.ActualCapsuleRadius);
            Evidence->SetNumberField(TEXT("capsuleHalfHeight"), Placement.ActualCapsuleHalfHeight);
            Evidence->SetNumberField(TEXT("querySequence"), static_cast<double>(Placement.QuerySequence));
            Evidence->SetNumberField(TEXT("queryServerTime"), Placement.QueryServerTime);
            Evidence->SetBoolField(TEXT("capsuleClear"), Placement.bFreshCapsuleClear);
            Evidence->SetBoolField(TEXT("walkingOnGround"), Placement.bWalkingOnGround);
            Evidence->SetBoolField(TEXT("intendedSupport"), Placement.bIntendedSupport);
            Evidence->SetBoolField(TEXT("floorBlocking"), Placement.FreshFloor.bBlockingHit);
            Evidence->SetBoolField(TEXT("floorWalkable"), Placement.FreshFloor.IsWalkableFloor());
            Evidence->SetBoolField(TEXT("floorPenetrating"), Placement.FreshFloor.HitResult.bStartPenetrating);
            Evidence->SetNumberField(TEXT("floorDistanceCm"), Placement.FreshFloor.GetDistanceToFloor());
            Evidence->SetArrayField(TEXT("floorNormal"), TownVector(Placement.FreshFloor.HitResult.ImpactNormal));
            Evidence->SetStringField(TEXT("supportName"), Placement.ActualSupportName.ToString());
            Record->SetObjectField(TEXT("placement"), Evidence);
        }
        ServerTownReport->SetObjectField(TEXT("committed"), Record);
    }
    if (Event.Error == EFableTownTravelError::AlreadyCommitted)
    {
        ServerTownReport->SetObjectField(TEXT("after"), Snapshot);
        ServerTownReport->SetBoolField(TEXT("completed"), true);
        ServerTownReport->SetStringField(TEXT("status"), TEXT("observed"));
    }
    AppendTownObject(ServerTownReport, TEXT("events"), Record);
}

/** 두 Ready3kill/저장/접지/정지 보고서와 현재 명부가 일치할 때만 기준본을 만든다. */
void UFableOnlineValidationDriver::ObserveTownTravelServer(UWorld* World)
{
    auto* Coordinator = UFableTownTravelCoordinator::Find(World);
    if (!Coordinator || World->GetNetMode() != NM_DedicatedServer)
    {
        return;
    }
    if (TownBoundCoordinator.Get() != Coordinator)
    {
        if (TownBoundCoordinator.IsValid())
        {
            TownBoundCoordinator->OnAuthorityTravelEvent().RemoveAll(this);
        }
        TownBoundCoordinator = Coordinator;
        Coordinator->OnAuthorityTravelEvent().AddUObject(this, &UFableOnlineValidationDriver::OnTownTravelAuthority);
    }
    if (!ServerTownReport)
    {
        ServerTownReport = MakeShared<FJsonObject>();
        ServerTownReport->SetNumberField(TEXT("schemaVersion"), 1);
        ServerTownReport->SetStringField(TEXT("scenario"), TownTravelScenario);
        ServerTownReport->SetStringField(TEXT("status"), TEXT("waiting-ready-barrier"));
        ServerTownReport->SetArrayField(TEXT("events"), {});
    }
    if (ServerTownReport->HasField(TEXT("before")))
    {
        return;
    }
    const auto CurrentField = FableAdventure::Field(World);
    if (!CurrentField || !CurrentField->bObjectiveComplete || CurrentField->DefeatedEnemies != 3 ||
        CurrentField->RequiredEnemies != 3)
    {
        return;
    }
    auto Snapshot = TownTravelSnapshot(World);
    const TSharedPtr<FJsonObject>* A = nullptr;
    const TSharedPtr<FJsonObject>* B = nullptr;
    if (!Snapshot || !Snapshot->TryGetObjectField(TEXT("a"), A) || !Snapshot->TryGetObjectField(TEXT("b"), B))
    {
        return;
    }
    for (const TCHAR* MemberName : {TEXT("a"), TEXT("b")})
    {
        const auto Member = Snapshot->GetObjectField(MemberName);
        const auto Economy = Member->GetObjectField(TEXT("economy"));
        const FString File = FString(MemberName) == TEXT("a") ? TEXT("client1.json") : TEXT("client2.json");
        const auto Client = ReadTownReport(FPaths::Combine(Directory, File));
        const TSharedPtr<FJsonObject>* Sidecar = nullptr;
        if (!Client || !Client->TryGetObjectField(TEXT("townTravel"), Sidecar) ||
            !(*Sidecar)->GetBoolField(TEXT("readyBarrier")) || !Member->GetBoolField(TEXT("alive")) ||
            !Member->GetBoolField(TEXT("onGround")) || !Economy->GetBoolField(TEXT("ready")) ||
            Economy->GetBoolField(TEXT("savePending")) ||
            Economy->GetIntegerField(TEXT("forestQuest")) != static_cast<int32>(EFableQuestStage::Ready) ||
            Economy->GetIntegerField(TEXT("questKills")) != 3 ||
            Member->GetNumberField(TEXT("mana")) != Member->GetNumberField(TEXT("maxMana")))
        {
            return;
        }
    }
    if (Snapshot->GetArrayField(TEXT("publicRoster")).Num() != 2 ||
        Snapshot->GetArrayField(TEXT("livingFieldEnemies")).Num() != 0)
    {
        return;
    }
    ServerTownReport->SetObjectField(TEXT("before"), Snapshot);
    ServerTownReport->SetStringField(TEXT("status"), TEXT("observing"));
}

/** 정상 Prepared 통지에서 음성 ACK와 정상 취소를 같은 Reliable 스트림으로 보낸다. */
void UFableOnlineValidationDriver::OnTownTravelPrepared(const FFableTownTravelPrepared& Prepared)
{
    auto* Player = TownBoundPlayer.Get();
    auto Attempt = TownAttempt(TownReport, TownAttemptIndex);
    if (!Player || !Attempt || Prepared.RequestId != TownRequestId || TownAttemptIndex < 6 || TownAttemptIndex > 11)
    {
        return;
    }
    TownPrepared = Prepared;
    Attempt->SetObjectField(TEXT("prepared"), TownPreparedJson(Prepared));
    Attempt->SetNumberField(TEXT("preparedClientSequence"), static_cast<double>(++TownClientSequence));
    if (TownAttemptIndex == 6)
    {
        Capture(TEXT("unit2-prepared-pending"));
    }
    if (TownAttemptIndex == 11)
    {
        return; // 정상 성공 시도는 예약 자동 ACK를 변경하지 않는다.
    }
    if (TownAttemptIndex == 7)
    {
        TownCancelAttemptId = Player->UICancelTownTravel(Prepared.TransactionId, Prepared.Generation);
    }
    else
    {
        const FGuid Connection = TownAttemptIndex == 9 ? FGuid::NewGuid() : Prepared.ConnectionNonce;
        const FGuid Challenge = TownAttemptIndex == 8 ? FGuid::NewGuid() : Prepared.ChallengeNonce;
        const int64 TravelGeneration = Prepared.Generation + (TownAttemptIndex == 10 ? 1 : 0);
        TownMalformedAttemptId = Player->UIRequestTownTravelLoadAcknowledgement(
            Prepared.TransactionId, TravelGeneration, Connection, Challenge, TownAttemptIndex != 6);
        auto SentAck = MakeShared<FJsonObject>();
        SentAck->SetNumberField(TEXT("generation"), static_cast<double>(TravelGeneration));
        SentAck->SetStringField(TEXT("connectionNonceDigest"), TownNonceDigest(Connection));
        SentAck->SetStringField(TEXT("challengeNonceDigest"), TownNonceDigest(Challenge));
        SentAck->SetBoolField(TEXT("loaded"), TownAttemptIndex != 6);
        Attempt->SetObjectField(TEXT("sentAck"), SentAck);
        if (TownAttemptIndex >= 8)
        {
            TownCancelAttemptId = Player->UICancelTownTravel(Prepared.TransactionId, Prepared.Generation);
        }
    }
    Attempt->SetStringField(TEXT("malformedAttemptId"), TownGuid(TownMalformedAttemptId));
    Attempt->SetStringField(TEXT("cancelAttemptId"), TownGuid(TownCancelAttemptId));
}

/** 취소 응답과 명시 오래된 ACK 응답은 서로 다른 시도 식별자로 보존한다. */
void UFableOnlineValidationDriver::OnTownTravelResponse(const FFableTownTravelResponse& Response)
{
    auto Attempt = TownAttempt(TownReport, TownAttemptIndex);
    if (!Attempt || Response.RequestId != TownRequestId)
    {
        return;
    }
    TownResponses.Add(Response);
    auto Record = TownResponseJson(Response);
    Record->SetNumberField(TEXT("clientSequence"), static_cast<double>(++TownClientSequence));
    AppendTownObject(Attempt, TEXT("responses"), Record);
    if (TownAttemptIndex == 11 && Response.Operation == EFableTownTravelOperation::Acknowledge &&
        Response.Error == EFableTownTravelError::None && Response.State == EFableTownTravelState::Committed)
    {
        TownCommit = Response.Commit;
        bTownCommitResponse = true;
        TownReport->SetObjectField(TEXT("commitResponse"), Record);
    }
}

/** 입력 해제는 실제 성공 응답과 본인 도착을 모두 본 뒤 발생해야 한다. */
void UFableOnlineValidationDriver::OnTownTravelProgress(const FFableTownTravelEvent& Event)
{
    if (!TownReport || Event.RequestId != TownRequestId)
    {
        return;
    }
    auto Record = TownEventJson(Event);
    Record->SetNumberField(TEXT("clientSequence"), static_cast<double>(++TownClientSequence));
    if (TownAttemptIndex == 11 && !Event.bAuthority)
    {
        if (Event.Kind == EFableTownTravelEventKind::OwnArrivalObserved)
        {
            bTownOwnArrival = true;
        }
        if ((Event.Kind == EFableTownTravelEventKind::UiUnlocked ||
             Event.Kind == EFableTownTravelEventKind::SourceLeaseReleased) &&
            (!bTownCommitResponse || !bTownOwnArrival))
        {
            bTownOrderingFailed = true;
        }
    }
    AppendTownObject(TownReport, TEXT("progressEvents"), Record);
}

/** 실제 Forest 세 처치 뒤 정상 UI/RPC만 사용하며 기존 보상 흐름은 계속 보존한다. */
bool UFableOnlineValidationDriver::TickUnit2TownAnchors(AFablePlayerController* PC, AFablePlayerState* P,
                                                        AFableCharacter* Hero, double Now)
{
    if (!TownReport)
    {
        TownStarted = Now;
        TownReport = MakeShared<FJsonObject>();
        TownReport->SetNumberField(TEXT("schemaVersion"), 1);
        TownReport->SetStringField(TEXT("scenario"), TownTravelScenario);
        TownReport->SetStringField(TEXT("status"), TEXT("running"));
        TownReport->SetBoolField(TEXT("readyBarrier"), false);
        TownReport->SetBoolField(TEXT("completed"), false);
        TownReport->SetArrayField(TEXT("attempts"), {});
        TownReport->SetArrayField(TEXT("progressEvents"), {});
    }
    TownReport->SetNumberField(TEXT("step"), TownStep);
    if (TownReport->GetBoolField(TEXT("completed")))
    {
        const auto Peer = ReadTownReport(
            FPaths::Combine(Directory, Role == TEXT("client1") ? TEXT("client2.json") : TEXT("client1.json")));
        const TSharedPtr<FJsonObject>* Sidecar = nullptr;
        return Peer && Peer->TryGetObjectField(TEXT("townTravel"), Sidecar) &&
               (*Sidecar)->GetBoolField(TEXT("completed"));
    }
    if (Now - TownStarted > 120)
    {
        Finish(false, TEXT("Unit2 town anchor workflow timed out before its exact evidence completed."));
        return false;
    }
    if (TownBoundPlayer.Get() != PC)
    {
        UnbindTownTravel();
        TownBoundPlayer = PC;
        PC->OnTownTravelPrepared().AddUObject(this, &UFableOnlineValidationDriver::OnTownTravelPrepared);
        PC->OnTownTravelResponse().AddUObject(this, &UFableOnlineValidationDriver::OnTownTravelResponse);
        PC->OnTownTravelProgress().AddUObject(this, &UFableOnlineValidationDriver::OnTownTravelProgress);
    }
    if (TownStep == 0)
    {
        // 실제 이동 입력으로 출발 서비스에 접근하며 두 캡슐을 분리한다.
        PC->UIClosePanels();
        const FVector Departure = FableRPG::PortalLocation() + FVector(Role == TEXT("client1") ? -110 : 110, -150, 100);
        if (!DungeonWalk(PC, Departure, 30) || Hero->GetVelocity().Size() > 1 ||
            !Hero->GetCharacterMovement()->IsMovingOnGround() || P->ForestQuest != EFableQuestStage::Ready ||
            P->QuestKills != 3 || P->IsBackendSavePending() || !P->IsBackendReady() || !P->bLastSaveSucceeded ||
            P->GetBackendRevision() != P->GetBackendAcknowledgedRevision() || Hero->GetMana() != Hero->GetMaxMana())
        {
            TownStationaryAt = 0;
            return false;
        }
        if (TownStationaryAt == 0)
        {
            TownStationaryAt = Now;
        }
        if (Now - TownStationaryAt < 1)
        {
            return false;
        }
        TownReport->SetBoolField(TEXT("readyBarrier"), true);
        TownStep = 1;
        if (Role == TEXT("client2"))
        {
            Capture(TEXT("unit2-observer-before"));
        }
    }
    const auto Server = ReadTownReport(FPaths::Combine(Directory, TEXT("server.json")));
    const TSharedPtr<FJsonObject>* Authority = nullptr;
    if (!Server || !Server->TryGetObjectField(TEXT("serverTownTravel"), Authority) ||
        !(*Authority)->HasField(TEXT("before")))
    {
        return false;
    }
    if (Role == TEXT("client2"))
    {
        const auto Peer = ReadTownReport(FPaths::Combine(Directory, TEXT("client1.json")));
        const TSharedPtr<FJsonObject>* Sidecar = nullptr;
        bool Complete = false;
        if (Peer && Peer->TryGetObjectField(TEXT("townTravel"), Sidecar) &&
            (*Sidecar)->TryGetBoolField(TEXT("completed"), Complete) && Complete)
        {
            TownReport->SetBoolField(TEXT("completed"), true);
            TownReport->SetStringField(TEXT("status"), TEXT("observed"));
        }
        return false;
    }
    const auto FindResponse = [&](FGuid AttemptId, EFableTownTravelError Error) -> const FFableTownTravelResponse*
    {
        return TownResponses.FindByPredicate(
            [&](const FFableTownTravelResponse& Entry)
            {
                return Entry.AttemptId == AttemptId && Entry.Error == Error;
            });
    };
    if (!TownRequestId.IsValid())
    {
        if (Now - TownLastBeginAt < 1 || TownAttemptIndex >= 12)
        {
            return false;
        }
        auto Attempt = MakeShared<FJsonObject>();
        Attempt->SetStringField(TEXT("label"), TownAttemptNames[TownAttemptIndex]);
        Attempt->SetArrayField(TEXT("responses"), {});
        const EFableTownId Destination =
            TownAttemptIndex < 4
                ? static_cast<EFableTownId>(static_cast<int32>(EFableTownId::Sunbrook) + TownAttemptIndex)
                : EFableTownId::LegacyVillage;
        const FName Anchor = TownAttemptIndex == 4 ? FName(TEXT("Unknown.Unit2")) : FName(TEXT("Entry"));
        const int32 Catalog = TownAttemptIndex == 5 ? 2 : 1;
        AppendTownObject(TownReport, TEXT("attempts"), Attempt);
        TownRequestId = PC->UIRequestTownTravel(Destination, Anchor, Catalog);
        auto Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("requestId"), TownGuid(TownRequestId));
        Request->SetNumberField(TEXT("targetTownId"), static_cast<int32>(Destination));
        Request->SetStringField(TEXT("anchorId"), Anchor.ToString());
        Request->SetNumberField(TEXT("catalogVersion"), Catalog);
        Attempt->SetObjectField(TEXT("request"), Request);
        TownLastBeginAt = Now;
        TownStep = 2;
        return false;
    }
    auto Attempt = TownAttempt(TownReport, TownAttemptIndex);
    bool AttemptComplete = false;
    if (TownAttemptIndex < 6)
    {
        const auto ExpectedError = TownAttemptIndex < 4    ? EFableTownTravelError::DestinationNotReady
                                   : TownAttemptIndex == 4 ? EFableTownTravelError::UnknownAnchor
                                                           : EFableTownTravelError::StaleCatalog;
        AttemptComplete = FindResponse(TownRequestId, ExpectedError) != nullptr;
    }
    else if (TownAttemptIndex < 11 && TownPrepared.TransactionId.IsValid())
    {
        const auto ExpectedError = TownAttemptIndex == 6   ? EFableTownTravelError::ClientLoadFailed
                                   : TownAttemptIndex == 8 ? EFableTownTravelError::WrongChallengeNonce
                                   : TownAttemptIndex == 9 ? EFableTownTravelError::WrongConnectionNonce
                                                           : EFableTownTravelError::WrongGeneration;
        const bool Rejected = TownAttemptIndex == 7 || FindResponse(TownMalformedAttemptId, ExpectedError);
        const bool Cancelled =
            TownAttemptIndex == 6 || FindResponse(TownCancelAttemptId, EFableTownTravelError::CancelledByOwner);
        if (Rejected && Cancelled && !bTownReplaySent)
        {
            TownReplayAttemptId = PC->UIRequestTownTravelLoadAcknowledgement(
                TownPrepared.TransactionId, TownPrepared.Generation, TownPrepared.ConnectionNonce,
                TownPrepared.ChallengeNonce, true);
            bTownReplaySent = true;
            Attempt->SetStringField(TEXT("replayAttemptId"), TownGuid(TownReplayAttemptId));
        }
        AttemptComplete = bTownReplaySent && FindResponse(TownReplayAttemptId, EFableTownTravelError::AlreadyCancelled);
    }
    else if (TownAttemptIndex == 11 && bTownCommitResponse)
    {
        const auto& Connection = P->GetTownConnectionSnapshot();
        const FVector Accepted = TownCommit.AcceptedTransform.GetLocation();
        const FVector Actual = Hero->GetCapsuleComponent()->GetComponentLocation();
        const double Planar = FVector::Dist2D(Accepted, Actual);
        const double Vertical = FMath::Abs(Accepted.Z - Actual.Z);
        const bool Matched = Connection.LastCommittedTransactionId == TownCommit.TransactionId &&
                             Connection.OwnerConnectionGeneration == TownPrepared.OwnerConnectionGeneration &&
                             Connection.ConnectionNonce == TownPrepared.ConnectionNonce &&
                             Connection.LastCommittedGeneration == TownCommit.Generation &&
                             Connection.PresenceRevision == TownCommit.PresenceRevision &&
                             Connection.SafeCheckpointRevision == TownCommit.SafeCheckpointRevision && Planar <= 10 &&
                             Vertical <= 10 &&
                             Connection.LastAcceptedTransform.Equals(TownCommit.AcceptedTransform, .01) &&
                             Hero->GetVelocity().Size() <= 1 && Hero->GetCharacterMovement()->IsMovingOnGround();
        if (!Matched)
        {
            TownArrivalSamples = 0;
            TownArrivalAt = 0;
            TownArrivalObservations.Reset();
            TownReport->RemoveField(TEXT("arrival"));
            return false;
        }
        if (TownArrivalAt == 0)
        {
            TownArrivalAt = Now;
        }
        ++TownArrivalSamples;
        auto Observation = MakeShared<FJsonObject>();
        Observation->SetNumberField(TEXT("clientTime"), Now);
        Observation->SetArrayField(TEXT("position"), TownVector(Actual));
        Observation->SetArrayField(TEXT("velocity"), TownVector(Hero->GetVelocity()));
        Observation->SetStringField(TEXT("transactionId"), TownGuid(TownCommit.TransactionId));
        Observation->SetNumberField(TEXT("generation"), static_cast<double>(Connection.LastCommittedGeneration));
        Observation->SetNumberField(TEXT("presenceRevision"), static_cast<double>(Connection.PresenceRevision));
        Observation->SetNumberField(TEXT("safeCheckpointRevision"),
                                    static_cast<double>(Connection.SafeCheckpointRevision));
        Observation->SetNumberField(TEXT("ownerConnectionGeneration"),
                                    static_cast<double>(Connection.OwnerConnectionGeneration));
        Observation->SetStringField(TEXT("connectionNonceDigest"), TownNonceDigest(Connection.ConnectionNonce));
        Observation->SetBoolField(TEXT("onGround"), Hero->GetCharacterMovement()->IsMovingOnGround());
        TownArrivalObservations.Add(MakeShared<FJsonValueObject>(Observation));
        auto Arrival = MakeShared<FJsonObject>();
        Arrival->SetStringField(TEXT("transactionId"), TownGuid(TownCommit.TransactionId));
        Arrival->SetNumberField(TEXT("generation"), static_cast<double>(TownCommit.Generation));
        Arrival->SetNumberField(TEXT("samples"), TownArrivalSamples);
        Arrival->SetNumberField(TEXT("stableSeconds"), Now - TownArrivalAt);
        Arrival->SetNumberField(TEXT("planarDistanceCm"), Planar);
        Arrival->SetNumberField(TEXT("verticalDistanceCm"), Vertical);
        Arrival->SetBoolField(TEXT("orderingFailed"), bTownOrderingFailed);
        Arrival->SetBoolField(TEXT("ownArrivalEventObserved"), bTownOwnArrival);
        Arrival->SetArrayField(TEXT("observations"), TownArrivalObservations);
        TownReport->SetObjectField(TEXT("arrival"), Arrival);
        if (TownArrivalSamples >= 3 && Now - TownArrivalAt >= .3 && bTownOwnArrival && !bTownOrderingFailed &&
            !PC->IsTownTravelInputBlocked())
        {
            if (!bTownDuplicateSent)
            {
                TownDuplicateAttemptId = PC->UIRequestTownTravelLoadAcknowledgement(
                    TownPrepared.TransactionId, TownPrepared.Generation, TownPrepared.ConnectionNonce,
                    TownPrepared.ChallengeNonce, true);
                bTownDuplicateSent = true;
                Attempt->SetStringField(TEXT("duplicateAttemptId"), TownGuid(TownDuplicateAttemptId));
                Capture(TEXT("unit2-own-entry-arrival"));
            }
            AttemptComplete = FindResponse(TownDuplicateAttemptId, EFableTownTravelError::AlreadyCommitted) != nullptr;
        }
    }
    if (!AttemptComplete)
    {
        return false;
    }
    Attempt->SetBoolField(TEXT("completed"), true);
    if (TownAttemptIndex == 7)
    {
        Capture(TEXT("unit2-cancelled-source"));
    }
    if (TownAttemptIndex == 11)
    {
        bool ServerComplete = false;
        if (!(*Authority)->TryGetBoolField(TEXT("completed"), ServerComplete) || !ServerComplete)
        {
            return false;
        }
        TownReport->SetBoolField(TEXT("completed"), true);
        TownReport->SetStringField(TEXT("status"), TEXT("observed"));
        TownStep = 3;
        return false;
    }
    ++TownAttemptIndex;
    TownRequestId.Invalidate();
    TownMalformedAttemptId.Invalidate();
    TownCancelAttemptId.Invalidate();
    TownReplayAttemptId.Invalidate();
    TownPrepared = FFableTownTravelPrepared();
    TownResponses.Reset();
    bTownReplaySent = false;
    return false;
}
