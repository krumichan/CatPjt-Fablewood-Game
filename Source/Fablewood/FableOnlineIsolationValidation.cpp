#include "FableOnlineValidationDriver.h"
#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePartyEncounter.h"
#include "FablePartyManager.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** 공개 진단 좌표를 월드 값 세 개로 기록한다. */
    TArray<TSharedPtr<FJsonValue>> IsolationVector(const FVector& Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
                MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** 다른 실제 프로세스의 공개 관찰 보고서를 읽는다. */
    TSharedPtr<FJsonObject> ReadIsolation(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> Result;
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result))
        {
            return nullptr;
        }
        return Result;
    }

    /** 아직 기록되지 않은 단계는 완료로 간주하지 않는다. */
    bool IsolationCheck(const TSharedPtr<FJsonObject>& Report, const FString& Name)
    {
        const TSharedPtr<FJsonObject>* Values = nullptr;
        bool Value = false;
        return Report && Report->TryGetObjectField(TEXT("checks"), Values) && (*Values)->TryGetBoolField(Name, Value) &&
               Value;
    }

    /** 보고서의 특정 객체가 존재하는지 안전하게 조회한다. */
    TSharedPtr<FJsonObject> IsolationObject(const TSharedPtr<FJsonObject>& Source, const FString& Name)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        return Source && Source->TryGetObjectField(Name, Value) ? *Value : nullptr;
    }

    /** 시간에 따라 바뀌는 마나·저장 리비전을 제외하고 영구 게임 값을 그대로 비교한다. */
    FString IsolationPersistent(const TSharedPtr<FJsonObject>& Source)
    {
        if (!Source)
        {
            return FString();
        }
        auto Values = MakeShared<FJsonObject>();
        const TCHAR* Fields[] = {TEXT("playerId"),
                                 TEXT("heroClass"),
                                 TEXT("level"),
                                 TEXT("experience"),
                                 TEXT("classTier"),
                                 TEXT("wallet"),
                                 TEXT("items"),
                                 TEXT("bagCount"),
                                 TEXT("stashCount"),
                                 TEXT("equippedWeaponInstanceId"),
                                 TEXT("equippedArmorInstanceId"),
                                 TEXT("forestQuest"),
                                 TEXT("dungeonQuest"),
                                 TEXT("dungeonCompletions")};
        for (const TCHAR* Field : Fields)
        {
            const auto* Value = Source->Values.Find(Field);
            if (!Value)
            {
                return FString();
            }
            Values->SetField(Field, *Value);
        }
        FString Text;
        FJsonSerializer::Serialize(Values, TJsonWriterFactory<>::Create(&Text));
        return Text;
    }

    /** 다른 파티의 진행·적과 영구 데이터가 함께 보존되었는지 비교한다. */
    bool IsolationUnchanged(const TSharedPtr<FJsonObject>& Before, const TSharedPtr<FJsonObject>& After)
    {
        if (!Before || !After)
        {
            return false;
        }
        const TCHAR* Fields[] = {
            TEXT("partyId"),       TEXT("expeditionId"),       TEXT("region"),      TEXT("difficulty"),
            TEXT("wave"),          TEXT("defeated"),           TEXT("required"),    TEXT("recovering"),
            TEXT("recoveryCount"), TEXT("encounterSerial"),    TEXT("worldOrigin"), TEXT("ticketMembers"),
            TEXT("ticketPartyId"), TEXT("ticketExpeditionId"), TEXT("enemies")};
        for (const TCHAR* Field : Fields)
        {
            const auto* Left = Before->Values.Find(Field);
            const auto* Right = After->Values.Find(Field);
            if (!Left || !Right)
            {
                return false;
            }
            auto LeftObject = MakeShared<FJsonObject>(), RightObject = MakeShared<FJsonObject>();
            LeftObject->SetField(TEXT("value"), *Left);
            RightObject->SetField(TEXT("value"), *Right);
            FString LeftText, RightText;
            FJsonSerializer::Serialize(LeftObject, TJsonWriterFactory<>::Create(&LeftText));
            FJsonSerializer::Serialize(RightObject, TJsonWriterFactory<>::Create(&RightText));
            if (LeftText != RightText)
            {
                return false;
            }
        }
        const FString Persistent = IsolationPersistent(IsolationObject(Before, TEXT("profile")));
        return !Persistent.IsEmpty() && Persistent == IsolationPersistent(IsolationObject(After, TEXT("profile")));
    }

    /** 숲 의뢰 또는 정확히 한 번의 유적 처치 보상만 인정한다. */
    bool IsolationReward(const TSharedPtr<FJsonObject>& Row, bool Killed)
    {
        auto ProfileValue = IsolationObject(Row, TEXT("profile"));
        auto WalletValue = IsolationObject(ProfileValue, TEXT("wallet"));
        return ProfileValue && WalletValue && ProfileValue->GetIntegerField(TEXT("level")) == 2 &&
               ProfileValue->GetIntegerField(TEXT("experience")) == (Killed ? 140 : 105) &&
               ProfileValue->GetIntegerField(TEXT("bagCount")) == 5 &&
               ProfileValue->GetIntegerField(TEXT("forestQuest")) == 3 &&
               ProfileValue->GetIntegerField(TEXT("dungeonQuest")) == 1 &&
               ProfileValue->GetIntegerField(TEXT("dungeonCompletions")) == 0 &&
               WalletValue->GetIntegerField(TEXT("gold")) == (Killed ? 153 : 135) &&
               WalletValue->GetIntegerField(TEXT("ore")) == (Killed ? 8 : 7) &&
               WalletValue->GetIntegerField(TEXT("rootCores")) == 0;
    }
}

/** 지정한 두 캐릭터의 원정·적·저장 상태를 서버에서 관찰한다. */
TSharedPtr<FJsonObject> UFableOnlineValidationDriver::IsolationServerSnapshot(UWorld* World) const
{
    const auto* ServerMode = World ? World->GetAuthGameMode<AFableGameMode>() : nullptr;
    if (!ServerMode || World->GetNetMode() != NM_DedicatedServer)
    {
        return nullptr;
    }
    auto Result = MakeShared<FJsonObject>();
    int32 ActiveInstances = 0;
    for (const auto* Encounter : ServerMode->GetExpeditions())
    {
        ActiveInstances += IsValid(Encounter) && Encounter->IsCommitted() ? 1 : 0;
    }
    Result->SetNumberField(TEXT("activeInstances"), ActiveInstances);
    for (const FString& ClientRole : {FString(TEXT("client1")), FString(TEXT("client2"))})
    {
        const auto Client = ReadIsolation(FPaths::Combine(Directory, ClientRole + TEXT(".json")));
        const auto ClientState = IsolationObject(Client, TEXT("partyIsolation"));
        FString Key;
        if (!ClientState || !ClientState->TryGetStringField(TEXT("selfKey"), Key) || Key.IsEmpty())
        {
            return nullptr;
        }
        AFablePlayerState* Participant = nullptr;
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            auto* Controller = It->Get();
            auto* Candidate = Controller ? Controller->GetPlayerState<AFablePlayerState>() : nullptr;
            if (Candidate && Candidate->PartyPlayerKey == Key && !Controller->IsLocalController())
            {
                Participant = Candidate;
            }
        }
        auto* Pawn = Participant ? Cast<AFableCharacter>(Participant->GetPawn()) : nullptr;
        const auto View = FableAdventure::For(Participant);
        if (!Participant || !Pawn || !View)
        {
            return nullptr;
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("playerKey"), Key);
        Row->SetStringField(TEXT("partyId"), Participant->PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
        Row->SetStringField(TEXT("expeditionId"), View.ExpeditionId().ToString(EGuidFormats::DigitsWithHyphensLower));
        Row->SetNumberField(TEXT("region"), int32(View->ActiveRegion));
        Row->SetNumberField(TEXT("difficulty"), int32(View->ActiveDifficulty));
        Row->SetNumberField(TEXT("wave"), View->DungeonWave);
        Row->SetNumberField(TEXT("defeated"), View->DefeatedEnemies);
        Row->SetNumberField(TEXT("required"), View->RequiredEnemies);
        Row->SetNumberField(TEXT("recoveryCount"), View->PartyRecoveryCount);
        Row->SetNumberField(TEXT("encounterSerial"), View->EncounterSerial);
        Row->SetBoolField(TEXT("recovering"), View->bPartyRecoveryPending);
        Row->SetBoolField(TEXT("alive"), Pawn->IsAlive());
        Row->SetBoolField(TEXT("inRegion"), View.IsInRegion(View->ActiveRegion, Pawn->GetActorLocation()));
        Row->SetNumberField(TEXT("downedCount"), Pawn->GetDownedCount());
        const FVector Origin = View.WorldOrigin();
        Row->SetArrayField(TEXT("worldOrigin"),
                           {MakeShared<FJsonValueNumber>(Origin.X), MakeShared<FJsonValueNumber>(Origin.Y),
                            MakeShared<FJsonValueNumber>(Origin.Z)});
        auto Diagnostics = MakeShared<FJsonObject>();
        Diagnostics->SetArrayField(TEXT("position"), IsolationVector(Pawn->GetActorLocation()));
        Diagnostics->SetArrayField(TEXT("canonicalPosition"),
                                   IsolationVector(View.ToCanonical(View->ActiveRegion, Pawn->GetActorLocation())));
        Diagnostics->SetArrayField(TEXT("velocity"), IsolationVector(Pawn->GetVelocity()));
        TArray<TSharedPtr<FJsonValue>> MemberKeys;
        if (const auto* Encounter = ServerMode->GetExpeditionFor(Participant))
        {
            Row->SetStringField(TEXT("ticketPartyId"),
                                Encounter->GetTicket().PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
            Row->SetStringField(TEXT("ticketExpeditionId"),
                                Encounter->GetTicket().ExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
            for (const auto& MemberKey : Encounter->GetTicket().MemberKeys)
            {
                MemberKeys.Add(MakeShared<FJsonValueString>(MemberKey));
            }
        }
        else
        {
            Row->SetStringField(TEXT("ticketPartyId"), FString());
            Row->SetStringField(TEXT("ticketExpeditionId"), FString());
        }
        Row->SetArrayField(TEXT("ticketMembers"), MemberKeys);
        Row->SetObjectField(TEXT("profile"), Profile(Participant));
        FString OriginalId;
        ClientState->TryGetStringField(TEXT("expeditionId"), OriginalId);
        FGuid OriginalExpedition;
        FGuid::Parse(OriginalId, OriginalExpedition);
        int32 BoundEnemies = 0;
        for (TActorIterator<AFableEnemy> It(World); It; ++It)
        {
            BoundEnemies += OriginalExpedition.IsValid() && It->GetExpeditionId() == OriginalExpedition ? 1 : 0;
        }
        Row->SetNumberField(TEXT("boundEnemyActors"), BoundEnemies);
        const bool Saved = Participant->IsBackendReady() && !Participant->IsBackendSavePending() &&
                           Participant->bLastSaveSucceeded &&
                           Participant->GetBackendRevision() == Participant->GetBackendAcknowledgedRevision();
        Row->SetBoolField(TEXT("saved"), Saved);
        TArray<AFableEnemy*> Living;
        for (TActorIterator<AFableEnemy> It(World); It; ++It)
        {
            if (It->IsAlive() && View.ExpeditionId().IsValid() &&
                FableAdventure::For(*It).ExpeditionId() == View.ExpeditionId())
            {
                Living.Add(*It);
            }
        }
        Living.Sort(
            [](const AFableEnemy& A, const AFableEnemy& B)
            {
                return A.GetName() < B.GetName();
            });
        TArray<TSharedPtr<FJsonValue>> Enemies;
        TArray<TSharedPtr<FJsonValue>> EnemyDiagnostics;
        float NearestEnemyDistance = 100000;
        bool Safe = Pawn->IsAlive() && Saved && View.IsInRegion(View->ActiveRegion, Pawn->GetActorLocation());
        for (const auto* Enemy : Living)
        {
            auto EnemyRow = MakeShared<FJsonObject>();
            EnemyRow->SetStringField(TEXT("id"), Enemy->GetSpawnId().ToString(EGuidFormats::DigitsWithHyphensLower));
            EnemyRow->SetStringField(TEXT("expeditionId"),
                                     View.ExpeditionId().ToString(EGuidFormats::DigitsWithHyphensLower));
            EnemyRow->SetNumberField(TEXT("health"), Enemy->GetHealth());
            Enemies.Add(MakeShared<FJsonValueObject>(EnemyRow));
            const float EnemyDistance = FVector::Dist2D(Pawn->GetActorLocation(), Enemy->GetActorLocation());
            NearestEnemyDistance = FMath::Min(NearestEnemyDistance, EnemyDistance);
            Safe &= EnemyDistance >= 1800;

            // 이동 좌표는 실시간 진단에만 추가하며 기존 UUID·체력 증명의 의미를 바꾸지 않는다.
            auto EnemyDiagnostic = MakeShared<FJsonObject>();
            EnemyDiagnostic->SetStringField(TEXT("id"), EnemyRow->GetStringField(TEXT("id")));
            EnemyDiagnostic->SetArrayField(TEXT("position"), IsolationVector(Enemy->GetActorLocation()));
            EnemyDiagnostic->SetArrayField(
                TEXT("canonicalPosition"),
                IsolationVector(View.ToCanonical(View->ActiveRegion, Enemy->GetActorLocation())));
            EnemyDiagnostic->SetNumberField(TEXT("distanceCm"), EnemyDistance);
            EnemyDiagnostics.Add(MakeShared<FJsonValueObject>(EnemyDiagnostic));
        }
        Row->SetArrayField(TEXT("enemies"), Enemies);
        Row->SetBoolField(TEXT("safe"), Safe);
        Diagnostics->SetArrayField(TEXT("enemies"), EnemyDiagnostics);
        Diagnostics->SetNumberField(TEXT("nearestEnemyDistanceCm"), NearestEnemyDistance);
        Diagnostics->SetBoolField(TEXT("saved"), Saved);
        Diagnostics->SetBoolField(TEXT("ownThreeEnemies"), Living.Num() == 3);
        Diagnostics->SetBoolField(TEXT("zeroKills"), View->DefeatedEnemies == 0);
        Diagnostics->SetBoolField(TEXT("distanceSafe"), NearestEnemyDistance >= 1800);
        Diagnostics->SetBoolField(TEXT("safe"), Safe);
        Row->SetObjectField(TEXT("diagnostics"), Diagnostics);
        Result->SetObjectField(ClientRole == TEXT("client1") ? TEXT("a") : TEXT("b"), Row);
    }
    return Result;
}

/** 서버가 관찰한 두 독립 원정의 단계별 정본 스냅숏을 보존한다. */
void UFableOnlineValidationDriver::ObserveIsolationServer(UWorld* World)
{
    auto Snapshot = IsolationServerSnapshot(World);
    auto A = IsolationObject(Snapshot, TEXT("a")), B = IsolationObject(Snapshot, TEXT("b"));
    if (!A || !B)
    {
        return;
    }
    if (!ServerIsolationReport)
    {
        ServerIsolationReport = MakeShared<FJsonObject>();
        ServerIsolationReport->SetBoolField(TEXT("actualCrossDamageGuardAttempted"), false);
    }
    // 준비가 끝나기 전의 실제 위치도 공개하되 합격 스냅숏과는 분리한다.
    ServerIsolationReport->SetObjectField(TEXT("liveSnapshot"), Snapshot);
    const auto Save = [&](const FString& Name, const FString& Check)
    {
        ServerIsolationReport->SetObjectField(Name, Snapshot);
        Checks.Add(Check, true);
        Publish(TEXT("running"));
    };
    auto Baseline = IsolationObject(ServerIsolationReport, TEXT("baseline"));
    if (!Baseline)
    {
        const bool ClientsSettled = IsolationCheck(ReadIsolation(FPaths::Combine(Directory, TEXT("client1.json"))),
                                                   TEXT("isolation_safe_baseline_ready")) &&
                                    IsolationCheck(ReadIsolation(FPaths::Combine(Directory, TEXT("client2.json"))),
                                                   TEXT("isolation_safe_baseline_ready"));
        if (ClientsSettled && A->GetStringField(TEXT("partyId")) != B->GetStringField(TEXT("partyId")) &&
            A->GetStringField(TEXT("expeditionId")) != B->GetStringField(TEXT("expeditionId")) &&
            A->GetStringField(TEXT("expeditionId")) != FGuid().ToString(EGuidFormats::DigitsWithHyphensLower) &&
            B->GetStringField(TEXT("expeditionId")) != FGuid().ToString(EGuidFormats::DigitsWithHyphensLower) &&
            A->GetArrayField(TEXT("worldOrigin"))[1]->AsNumber() !=
                B->GetArrayField(TEXT("worldOrigin"))[1]->AsNumber() &&
            A->GetArrayField(TEXT("ticketMembers")).Num() == 1 && B->GetArrayField(TEXT("ticketMembers")).Num() == 1 &&
            A->GetArrayField(TEXT("ticketMembers"))[0]->AsString() == A->GetStringField(TEXT("playerKey")) &&
            B->GetArrayField(TEXT("ticketMembers"))[0]->AsString() == B->GetStringField(TEXT("playerKey")) &&
            A->GetStringField(TEXT("ticketPartyId")) == A->GetStringField(TEXT("partyId")) &&
            B->GetStringField(TEXT("ticketPartyId")) == B->GetStringField(TEXT("partyId")) &&
            A->GetStringField(TEXT("ticketExpeditionId")) == A->GetStringField(TEXT("expeditionId")) &&
            B->GetStringField(TEXT("ticketExpeditionId")) == B->GetStringField(TEXT("expeditionId")) &&
            Snapshot->GetIntegerField(TEXT("activeInstances")) == 2 && A->GetIntegerField(TEXT("region")) == 1 &&
            B->GetIntegerField(TEXT("region")) == 1 && A->GetIntegerField(TEXT("difficulty")) == 0 &&
            B->GetIntegerField(TEXT("difficulty")) == 0 && A->GetIntegerField(TEXT("wave")) == 1 &&
            B->GetIntegerField(TEXT("wave")) == 1 && A->GetIntegerField(TEXT("required")) == 3 &&
            B->GetIntegerField(TEXT("required")) == 3 && !A->GetBoolField(TEXT("recovering")) &&
            !B->GetBoolField(TEXT("recovering")) && A->GetBoolField(TEXT("safe")) && B->GetBoolField(TEXT("safe")) &&
            A->GetIntegerField(TEXT("defeated")) == 0 && B->GetIntegerField(TEXT("defeated")) == 0 &&
            A->GetArrayField(TEXT("enemies")).Num() == 3 && B->GetArrayField(TEXT("enemies")).Num() == 3 &&
            IsolationReward(A, false) && IsolationReward(B, false))
        {
            Save(TEXT("baseline"), TEXT("server_isolation_distinct_instances"));
        }
        return;
    }
    auto BaselineB = IsolationObject(Baseline, TEXT("b"));
    auto AfterAKill = IsolationObject(ServerIsolationReport, TEXT("afterAKill"));
    if (!AfterAKill && A->GetIntegerField(TEXT("defeated")) == 1 && A->GetArrayField(TEXT("enemies")).Num() == 2 &&
        A->GetBoolField(TEXT("saved")) && IsolationReward(A, true) && IsolationUnchanged(BaselineB, B))
    {
        Save(TEXT("afterAKill"), TEXT("server_isolation_a_only_reward"));
    }
    if (AfterAKill && !IsolationObject(ServerIsolationReport, TEXT("duringARecovery")) &&
        A->GetBoolField(TEXT("recovering")) && A->GetIntegerField(TEXT("downedCount")) > 0 &&
        IsolationReward(A, true) && IsolationUnchanged(BaselineB, B))
    {
        Save(TEXT("duringARecovery"), TEXT("server_isolation_a_recovery_only"));
    }
    auto AfterAReturn = IsolationObject(ServerIsolationReport, TEXT("afterAReturn"));
    if (IsolationObject(ServerIsolationReport, TEXT("duringARecovery")) && !AfterAReturn &&
        A->GetIntegerField(TEXT("region")) == int32(EFableRegion::Village) && A->GetBoolField(TEXT("alive")) &&
        A->GetBoolField(TEXT("saved")) && A->GetBoolField(TEXT("inRegion")) &&
        A->GetIntegerField(TEXT("boundEnemyActors")) == 0 && Snapshot->GetIntegerField(TEXT("activeInstances")) == 1 &&
        A->GetStringField(TEXT("expeditionId")) == FGuid().ToString(EGuidFormats::DigitsWithHyphensLower) &&
        IsolationReward(A, true) && IsolationUnchanged(BaselineB, B))
    {
        Save(TEXT("afterAReturn"), TEXT("server_isolation_a_return_b_preserved"));
    }
    if (AfterAReturn && !IsolationObject(ServerIsolationReport, TEXT("afterBKill")) &&
        B->GetIntegerField(TEXT("defeated")) == 1 && B->GetArrayField(TEXT("enemies")).Num() == 2 &&
        B->GetBoolField(TEXT("safe")) && IsolationReward(B, true) &&
        IsolationUnchanged(IsolationObject(AfterAReturn, TEXT("a")), A))
    {
        Save(TEXT("afterBKill"), TEXT("server_isolation_b_reward_a_preserved"));
    }
}

/** 두 솔로 파티의 처치·전멸·귀환이 서로에게 전파되지 않는지 실제 입력으로 검증한다. */
bool UFableOnlineValidationDriver::TickPartyIsolation(AFablePlayerController* PC, AFablePlayerState* P,
                                                      AFableCharacter* Hero, double Now)
{
    const auto View = FableAdventure::For(P);
    auto* Manager = AFablePartyManager::Find(PC ? PC->GetWorld() : nullptr);
    if (!PC || !P || !Hero || !Manager)
    {
        return false;
    }
    const bool A = Role == TEXT("client1");
    const FString PeerRole = A ? TEXT("client2") : TEXT("client1");
    auto PeerReport = ReadIsolation(FPaths::Combine(Directory, PeerRole + TEXT(".json")));
    auto ServerReport = ReadIsolation(FPaths::Combine(Directory, TEXT("server.json")));
    auto ServerState = IsolationObject(ServerReport, TEXT("serverIsolation"));
    const auto Mark = [&](const FString& Name)
    {
        Checks.Add(Name, true);
        Publish(TEXT("running"));
    };
    const auto Advance = [&](int32 Next)
    {
        DungeonHold(PC, EKeys::W, false);
        DungeonHold(PC, EKeys::S, false);
        IsolationStep = Next;
        IsolationStepAt = Now;
        bIsolationRequestSent = false;
        IsolationReport->SetNumberField(TEXT("step"), Next);
    };
    if (!IsolationReport)
    {
        IsolationSelfKey = P->PartyPlayerKey;
        if (IsolationSelfKey.IsEmpty())
        {
            return false;
        }
        IsolationStepAt = Now;
        IsolationReport = MakeShared<FJsonObject>();
        IsolationReport->SetStringField(TEXT("selfKey"), IsolationSelfKey);
        IsolationReport->SetBoolField(TEXT("actualCrossDamageGuardAttempted"), false);
        IsolationReport->SetObjectField(TEXT("forestCheckpoint"), Profile(P));
        PC->UIClosePanels();
        PC->UIToggleParty();
        Capture(TEXT("isolation-parties"));
        return false;
    }
    const auto* Party = Manager->GetPartyForMember(P->GetPlayerId());
    const auto Solo = [&]()
    {
        return Party && Party->Members.Num() == 1 && Party->LeaderKey == IsolationSelfKey &&
               Party->Members[0].PlayerKey == IsolationSelfKey && Party->Members[0].bConnected;
    };
    const auto Saved = [&]()
    {
        return P->IsBackendReady() && !P->IsBackendSavePending() && P->bLastSaveSucceeded &&
               P->GetBackendRevision() == P->GetBackendAcknowledgedRevision();
    };
    const auto Send = [&](TFunctionRef<void()> Request)
    {
        if (!bIsolationRequestSent && !PC->IsPartyPreparationPending() && Saved() && Now - IsolationLastSentAt > .8)
        {
            Request();
            bIsolationRequestSent = true;
            IsolationLastSentAt = Now;
        }
    };
    const auto Acknowledged = [&]()
    {
        return bIsolationRequestSent && !PC->IsPartyPreparationPending() && PC->DidPartyPreparationSucceed();
    };
    TArray<AFableEnemy*> Living;
    if (View && View.ExpeditionId().IsValid())
    {
        for (TActorIterator<AFableEnemy> It(Hero->GetWorld()); It; ++It)
        {
            if (It->IsAlive() && FableAdventure::For(*It).ExpeditionId() == View.ExpeditionId())
            {
                Living.Add(*It);
            }
        }
    }
    const auto Safe = [&]()
    {
        if (!Hero->IsAlive() || !View || !Saved() || !View.IsInRegion(View->ActiveRegion, Hero->GetActorLocation()))
        {
            return false;
        }
        for (const auto* Enemy : Living)
        {
            if (FVector::Dist2D(Hero->GetActorLocation(), Enemy->GetActorLocation()) < 1800)
            {
                return false;
            }
        }
        return true;
    };

    // 각 공개 보고서가 이동 정체와 실제 안전 조건을 구분할 수 있게 현재 값을 갱신한다.
    float NearestEnemyDistance = 100000;
    for (const auto* Enemy : Living)
    {
        NearestEnemyDistance = FMath::Min(NearestEnemyDistance,
                                          float(FVector::Dist2D(Hero->GetActorLocation(), Enemy->GetActorLocation())));
    }
    const FVector CanonicalGoal(6300, -1580, 100);
    const FVector WorldGoal = View.ToWorld(EFableRegion::RootRuins, CanonicalGoal);
    const float GoalDistance = FVector::Dist2D(Hero->GetActorLocation(), WorldGoal);
    auto Diagnostics = MakeShared<FJsonObject>();
    Diagnostics->SetNumberField(TEXT("waypoint"), ReconnectWaypoint);
    Diagnostics->SetNumberField(TEXT("stepElapsedSeconds"), Now - IsolationStepAt);
    Diagnostics->SetArrayField(TEXT("position"), IsolationVector(Hero->GetActorLocation()));
    Diagnostics->SetArrayField(TEXT("canonicalPosition"),
                               IsolationVector(View.ToCanonical(EFableRegion::RootRuins, Hero->GetActorLocation())));
    Diagnostics->SetArrayField(TEXT("velocity"), IsolationVector(Hero->GetVelocity()));
    Diagnostics->SetArrayField(TEXT("safeGoal"), IsolationVector(WorldGoal));
    Diagnostics->SetNumberField(TEXT("safeGoalDistanceCm"), GoalDistance);
    Diagnostics->SetNumberField(TEXT("nearestEnemyDistanceCm"), NearestEnemyDistance);
    Diagnostics->SetNumberField(TEXT("ownLivingEnemies"), Living.Num());
    Diagnostics->SetBoolField(TEXT("viewValid"), bool(View));
    Diagnostics->SetBoolField(TEXT("hasOwnExpedition"), View && View.ExpeditionId().IsValid());
    Diagnostics->SetBoolField(TEXT("alive"), Hero->IsAlive());
    Diagnostics->SetBoolField(TEXT("inRegion"), View && View.IsInRegion(View->ActiveRegion, Hero->GetActorLocation()));
    Diagnostics->SetBoolField(TEXT("saved"), Saved());
    Diagnostics->SetBoolField(TEXT("ownThreeEnemies"), Living.Num() == 3);
    Diagnostics->SetBoolField(TEXT("zeroKills"), View && View->DefeatedEnemies == 0);
    Diagnostics->SetBoolField(TEXT("distanceSafe"), NearestEnemyDistance >= 1800);
    Diagnostics->SetBoolField(TEXT("routeCompleted"), ReconnectWaypoint >= 3);
    Diagnostics->SetBoolField(TEXT("atSafeGoal"), GoalDistance <= 60);
    Diagnostics->SetBoolField(TEXT("velocityStopped"), Hero->GetVelocity().Size2D() < 10);
    Diagnostics->SetBoolField(TEXT("safe"), Safe());
    IsolationReport->SetObjectField(TEXT("diagnostics"), Diagnostics);
    if (Now - IsolationStepAt > 150)
    {
        // 이번 프레임의 다른 캡처 없이 화면 한 장을 요청하고 원본 실패 조건을 그대로 남긴다.
        Capture(FString::Printf(TEXT("isolation-timeout-%d"), IsolationStep));
        Finish(false, FString::Printf(TEXT("Solo party isolation step %d timed out."), IsolationStep));
        return false;
    }
    const auto FightOne = [&]()
    {
        if (!View || !Hero->IsAlive())
        {
            return;
        }
        PC->UIClosePanels();
        if (!IsolationVictim.IsValid())
        {
            for (auto* Enemy : Living)
            {
                if (!IsolationVictim.IsValid() || Enemy->GetActorLocation().X < IsolationVictim->GetActorLocation().X)
                {
                    IsolationVictim = Enemy;
                }
            }
        }
        auto* Victim = IsolationVictim.Get();
        if (!Victim || !Victim->IsAlive())
        {
            DungeonHold(PC, EKeys::W, false);
            return;
        }
        const float Distance = FVector::Dist2D(Hero->GetActorLocation(), Victim->GetActorLocation());
        PC->SetControlRotation(
            FRotator(-12, (Victim->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
        DungeonHold(PC, EKeys::W, Distance > 600);
        DungeonHold(PC, EKeys::S, Distance < 400);
        if (Distance < 1050 && Now - DungeonAttackAt > .8)
        {
            DungeonKey(PC, EKeys::J);
            DungeonAttackAt = Now;
        }
        if (Hero->GetHealth() < Hero->GetMaxHealth() * .45f && P->HealthPotions > 0 &&
            P->GetPotionCooldownRemaining() <= 0 && Now - IsolationLastSentAt > 1)
        {
            PC->UIUseHealthPotion();
            IsolationLastSentAt = Now;
        }
    };
    const auto Escape = [&]()
    {
        if (Hero->IsAlive() && Hero->GetHealth() < Hero->GetMaxHealth() * .45f && P->HealthPotions > 0 &&
            P->GetPotionCooldownRemaining() <= 0 && Now - IsolationLastSentAt > 1)
        {
            PC->UIUseHealthPotion();
            IsolationLastSentAt = Now;
        }
        return ReconnectWalkSafe(PC, true);
    };

    switch (IsolationStep)
    {
    case 0:
        if (!Party && Now - IsolationLastSentAt > 1)
        {
            if (!PC->IsPartyOpen())
            {
                PC->UIToggleParty();
            }
            PC->UICreateParty();
            IsolationLastSentAt = Now;
        }
        if (Solo())
        {
            IsolationPartyId = Party->PartyId;
            IsolationReport->SetStringField(TEXT("partyId"),
                                            IsolationPartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
            Mark(TEXT("isolation_solo_party"));
            Advance(1);
        }
        break;
    case 1:
        PC->UIClosePanels();
        if (Solo() && Saved() && DungeonWalk(PC, FableRPG::PortalLocation() + FVector(A ? -80 : 80, -100, 100), 90) &&
            Hero->GetVelocity().Size2D() < 10)
        {
            PC->UIOpenPartyExpedition();
            Advance(2);
        }
        break;
    case 2:
        if (!Solo())
        {
            break;
        }
        Send(
            [&]()
            {
                PC->UIRequestPartySelection(IsolationPartyId, Party->PreparationRevision, EFableRegion::RootRuins,
                                            EFableDifficulty::Normal);
            });
        if (Acknowledged() && Party->SelectedRegion == EFableRegion::RootRuins &&
            Party->SelectedDifficulty == EFableDifficulty::Normal &&
            PC->GetPartyPreparationResultRevision() == Party->PreparationRevision)
        {
            Advance(3);
        }
        break;
    case 3:
        if (!Solo())
        {
            break;
        }
        Send(
            [&]()
            {
                PC->UISetPartyReady(true);
            });
        if (Acknowledged() && Party->Members[0].ReadyRevision == Party->PreparationRevision)
        {
            Mark(TEXT("isolation_ready"));
            Advance(4);
        }
        break;
    case 4:
        if (!IsolationCheck(PeerReport, TEXT("isolation_ready")))
        {
            break;
        }
        Send(
            [&]()
            {
                PC->UIEnterPreparedParty();
            });
        if (Acknowledged() && View && View.ExpeditionId().IsValid() && View.ExpeditionId() == P->ActiveExpeditionId &&
            View->ActiveRegion == EFableRegion::RootRuins && View->ActiveDifficulty == EFableDifficulty::Normal &&
            View.Players().Num() == 1 && View.IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()))
        {
            IsolationExpeditionId = View.ExpeditionId();
            const FVector Origin = View.WorldOrigin();
            IsolationReport->SetStringField(TEXT("expeditionId"),
                                            IsolationExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
            IsolationReport->SetArrayField(TEXT("worldOrigin"), {MakeShared<FJsonValueNumber>(Origin.X),
                                                                 MakeShared<FJsonValueNumber>(Origin.Y),
                                                                 MakeShared<FJsonValueNumber>(Origin.Z)});
            Mark(TEXT("isolation_private_arrival"));
            ReconnectWaypoint = 0;
            Advance(5);
        }
        break;
    case 5:
        if (!Escape() || !Safe() || !View || View->DefeatedEnemies != 0 || Living.Num() != 3)
        {
            break;
        }
        if (!Checks.FindRef(TEXT("isolation_safe_baseline_ready")))
        {
            IsolationReport->SetObjectField(TEXT("baselineProfile"), Profile(P));
            Mark(TEXT("isolation_safe_baseline_ready"));
        }
        if (IsolationCheck(ServerReport, TEXT("server_isolation_distinct_instances")))
        {
            Capture(TEXT("isolation-independent-arenas"));
            Advance(6);
        }
        break;
    case 6:
        if (A)
        {
            if (View && View->DefeatedEnemies == 0)
            {
                FightOne();
            }
            else if (View && View->DefeatedEnemies == 1 && Living.Num() == 2)
            {
                if (!Checks.FindRef(TEXT("isolation_a_kill_input_stopped")))
                {
                    // 전투 입력은 한 번만 종료하고 이후 이동 키는 탈출 경로만 제어한다.
                    DungeonHold(PC, EKeys::W, false);
                    DungeonHold(PC, EKeys::S, false);
                    DungeonHold(PC, EKeys::J, false);
                    ReconnectWaypoint = 0;
                    Checks.Add(TEXT("isolation_a_kill_input_stopped"), true);
                }
                if (Escape() && Safe())
                {
                    IsolationReport->SetObjectField(TEXT("firstKillProfile"), Profile(P));
                    Mark(TEXT("isolation_a_first_kill_saved"));
                }
            }
        }
        if (IsolationCheck(ServerReport, TEXT("server_isolation_a_only_reward")) &&
            (!A || Checks.FindRef(TEXT("isolation_a_first_kill_saved"))))
        {
            if (!A)
            {
                Mark(TEXT("isolation_b_unchanged_after_a_kill"));
            }
            IsolationVictim.Reset();
            Advance(7);
        }
        break;
    case 7:
        if (A)
        {
            if (Hero->IsAlive() && Living.Num() > 0)
            {
                // 공격·회복을 하지 않고 자기 적에게 일반 이동으로 접근해 실제 전멸을 발생시킨다.
                DungeonWalk(PC, Living[0]->GetActorLocation(), 75);
            }
            if (Hero->GetDownedCount() > 0 && !Checks.FindRef(TEXT("isolation_a_normally_downed")))
            {
                Mark(TEXT("isolation_a_normally_downed"));
                Capture(TEXT("isolation-own-wipe"));
            }
        }
        if (IsolationCheck(ServerReport, TEXT("server_isolation_a_recovery_only")) &&
            (!A || Checks.FindRef(TEXT("isolation_a_normally_downed"))))
        {
            if (!A)
            {
                Mark(TEXT("isolation_b_preserved_during_a_recovery"));
            }
            Advance(8);
        }
        break;
    case 8:
        if (IsolationCheck(ServerReport, TEXT("server_isolation_a_return_b_preserved")))
        {
            if (A && View && View->ActiveRegion == EFableRegion::Village && !P->ActiveExpeditionId.IsValid() &&
                Hero->IsAlive() && View.IsInRegion(EFableRegion::Village, Hero->GetActorLocation()) && Saved())
            {
                Mark(TEXT("isolation_a_recovered_town"));
                Advance(9);
            }
            else if (!A && View && View.ExpeditionId() == IsolationExpeditionId && View->DefeatedEnemies == 0 &&
                     Living.Num() == 3 && Safe())
            {
                Mark(TEXT("isolation_b_preserved_after_a_return"));
                Advance(9);
            }
        }
        break;
    case 9:
        if (!A)
        {
            if (View && View->DefeatedEnemies == 0)
            {
                FightOne();
            }
            else if (View && View->DefeatedEnemies == 1 && Living.Num() == 2)
            {
                ReconnectWaypoint = 0;
                Advance(10);
            }
        }
        else if (IsolationCheck(ServerReport, TEXT("server_isolation_b_reward_a_preserved")))
        {
            Mark(TEXT("isolation_a_no_reward_from_b"));
            Advance(10);
        }
        break;
    case 10:
        if (!A && (!Escape() || !Safe()))
        {
            break;
        }
        if (Saved() && P->Gold == 153 && P->Ore == 8 && P->Level == 2 && P->Experience == 140 &&
            P->Inventory.Num() == 5 && P->DungeonQuest == EFableQuestStage::Active && P->DungeonCompletions == 0 &&
            IsolationCheck(ServerReport, TEXT("server_isolation_b_reward_a_preserved")))
        {
            if (!A)
            {
                IsolationReport->SetObjectField(TEXT("firstKillProfile"), Profile(P));
                Mark(TEXT("isolation_b_first_kill_saved"));
            }
            IsolationReport->SetObjectField(TEXT("finalProfile"), Profile(P));
            Mark(TEXT("isolation_final_saved"));
            Advance(11);
        }
        break;
    case 11:
        if (IsolationCheck(PeerReport, TEXT("isolation_final_saved")))
        {
            Mark(TEXT("isolation_peer_server_handshake"));
            Advance(12);
        }
        break;
    case 12:
        if (IsolationCheck(PeerReport, TEXT("isolation_peer_server_handshake")))
        {
            IsolationReport->SetStringField(TEXT("status"), TEXT("passed"));
            return true;
        }
        break;
    default:
        Finish(false, TEXT("Unknown solo party isolation step."));
        break;
    }
    return false;
}
