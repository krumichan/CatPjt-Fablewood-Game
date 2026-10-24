#include "FableOnlineValidationDriver.h"
#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableExpeditionResultTypes.h"
#include "FableGameMode.h"
#include "FableInventorySnapshot.h"
#include "FablePartyEncounter.h"
#include "FablePartyManager.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** 공개 보고서만 읽고 개인 인증 문맥은 읽지 않는다. */
    TSharedPtr<FJsonObject> ResultsRead(const FString& Path)
    {
        FString Bytes;
        TSharedPtr<FJsonObject> Value;
        if (!FFileHelper::LoadFileToString(Bytes, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Bytes), Value))
        {
            return nullptr;
        }
        return Value;
    }

    /** 재접속 후에는 같은 캐릭터의 새 프로세스 보고서를 우선 읽는다. */
    TSharedPtr<FJsonObject> ResultsClient(const FString& Folder, const FString& ClientRole)
    {
        const FString BaseFolder = FPaths::GetCleanFilename(Folder) == TEXT("register-resume")
                                       ? FPaths::Combine(FPaths::GetPath(Folder), TEXT("register"))
                                       : Folder;
        if (ClientRole == TEXT("client2"))
        {
            auto Resumed = ResultsRead(
                FPaths::Combine(FPaths::GetPath(BaseFolder), TEXT("register-resume"), TEXT("client2.json")));
            if (Resumed)
            {
                return Resumed;
            }
        }
        return ResultsRead(FPaths::Combine(BaseFolder, ClientRole + TEXT(".json")));
    }

    /** 아직 관측하지 않은 상태를 성공으로 간주하지 않는다. */
    bool ResultsCheck(const TSharedPtr<FJsonObject>& Document, const FString& Name)
    {
        const TSharedPtr<FJsonObject>* Values = nullptr;
        bool Value = false;
        return Document && Document->TryGetObjectField(TEXT("checks"), Values) &&
               (*Values)->TryGetBoolField(Name, Value) && Value;
    }

    /** 존재하는 JSON 객체만 반환한다. */
    TSharedPtr<FJsonObject> ResultsObject(const TSharedPtr<FJsonObject>& Document, const FString& Name)
    {
        const TSharedPtr<FJsonObject>* Value = nullptr;
        return Document && Document->TryGetObjectField(Name, Value) ? *Value : nullptr;
    }

    /** UUID 표기는 보고서 전체에서 같은 형식을 사용한다. */
    FString ResultsId(FGuid Id)
    {
        return Id.ToString(EGuidFormats::DigitsWithHyphensLower);
    }

    /** 위치와 속도를 세 축 그대로 기록한다. */
    TArray<TSharedPtr<FJsonValue>> ResultsVector(FVector Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
                MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** 서버의 개인 결과와 실제 지급 영수증을 변형 없이 직렬화한다. */
    TSharedRef<FJsonObject> ResultsReceipt(const FFablePersonalExpeditionResult& Value)
    {
        auto Document = MakeShared<FJsonObject>();
        Document->SetStringField(TEXT("expeditionId"), ResultsId(Value.Ticket.ExpeditionId));
        Document->SetStringField(TEXT("partyId"), ResultsId(Value.Ticket.PartyId));
        Document->SetStringField(TEXT("recipientKey"), Value.RecipientPublicKey);
        Document->SetNumberField(TEXT("region"), int32(Value.Ticket.Region));
        Document->SetNumberField(TEXT("difficulty"), int32(Value.Ticket.Difficulty));
        Document->SetNumberField(TEXT("preparationRevision"), Value.Ticket.PreparationRevision);
        TArray<TSharedPtr<FJsonValue>> Keys;
        for (const auto& Key : Value.Ticket.MemberKeys)
        {
            Keys.Add(MakeShared<FJsonValueString>(Key));
        }
        Document->SetArrayField(TEXT("memberKeys"), Keys);
        Document->SetNumberField(TEXT("outcome"), int32(Value.Outcome));
        Document->SetStringField(TEXT("completionId"), ResultsId(Value.CompletionId));
        Document->SetNumberField(TEXT("resultRevision"), Value.ResultRevision);
        Document->SetBoolField(TEXT("acknowledged"), Value.bAcknowledged);
        Document->SetBoolField(TEXT("allGrantsCommitted"), Value.bAllGrantsCommitted);
        Document->SetBoolField(TEXT("saveReady"), Value.bSaveReady);
        Document->SetBoolField(TEXT("requiresBackendCommit"), Value.bRequiresBackendCommit);
        Document->SetStringField(TEXT("retryExpeditionId"), ResultsId(Value.RetryExpeditionId));
        TArray<TSharedPtr<FJsonValue>> Receipts;
        int32 Gold = 0, Ore = 0, Experience = 0, Cores = 0, Crystals = 0, Items = 0;
        for (const auto& Receipt : Value.Receipts)
        {
            auto Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("expeditionId"), ResultsId(Receipt.ExpeditionId));
            Entry->SetStringField(TEXT("grantId"), ResultsId(Receipt.GrantId));
            Entry->SetStringField(TEXT("recipientKey"), Receipt.RecipientPublicKey);
            Entry->SetNumberField(TEXT("sequence"), double(Receipt.Sequence));
            Entry->SetBoolField(TEXT("boss"), Receipt.bBoss);
            Entry->SetNumberField(TEXT("gold"), Receipt.ActualGold);
            Entry->SetNumberField(TEXT("ore"), Receipt.ActualOre);
            Entry->SetNumberField(TEXT("experience"), Receipt.ActualExperience);
            Entry->SetNumberField(TEXT("rootCores"), Receipt.ActualRootCores);
            Entry->SetNumberField(TEXT("moonCrystals"), Receipt.ActualMoonCrystals);
            Entry->SetNumberField(TEXT("saveState"), int32(Receipt.SaveState));
            Entry->SetNumberField(TEXT("committedRevision"), double(Receipt.CommittedRevision));
            TArray<TSharedPtr<FJsonValue>> GrantedItems;
            for (int32 Index = 0; Index < Receipt.GrantedItems.Num(); ++Index)
            {
                GrantedItems.Add(MakeShared<FJsonValueObject>(
                    FableInventorySnapshot::MakeItem(Receipt.GrantedItems[Index], 0, Index)));
            }
            Entry->SetArrayField(TEXT("items"), GrantedItems);
            Receipts.Add(MakeShared<FJsonValueObject>(Entry));
            Gold += Receipt.ActualGold;
            Ore += Receipt.ActualOre;
            Experience += Receipt.ActualExperience;
            Cores += Receipt.ActualRootCores;
            Crystals += Receipt.ActualMoonCrystals;
            Items += Receipt.GrantedItems.Num();
        }
        Document->SetArrayField(TEXT("receipts"), Receipts);
        Document->SetNumberField(TEXT("gold"), Gold);
        Document->SetNumberField(TEXT("ore"), Ore);
        Document->SetNumberField(TEXT("experience"), Experience);
        Document->SetNumberField(TEXT("rootCores"), Cores);
        Document->SetNumberField(TEXT("moonCrystals"), Crystals);
        Document->SetNumberField(TEXT("itemCount"), Items);
        return Document;
    }

    /** 재열기와 확인 상태 변화가 지급 내용 자체를 바꾸지 않는지 비교한다. */
    FString ResultsSignature(const TSharedPtr<FJsonObject>& Document, bool bProfile = false)
    {
        if (!Document)
        {
            return FString();
        }
        const TArray<FString> Fields = bProfile ? TArray<FString>{TEXT("playerId"),
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
                                                                  TEXT("dungeonCompletions")}
                                                : TArray<FString>{TEXT("expeditionId"), TEXT("partyId"),
                                                                  TEXT("recipientKey"), TEXT("region"),
                                                                  TEXT("difficulty"),   TEXT("preparationRevision"),
                                                                  TEXT("memberKeys"),   TEXT("outcome"),
                                                                  TEXT("completionId"), TEXT("requiresBackendCommit"),
                                                                  TEXT("receipts")};
        auto Stable = MakeShared<FJsonObject>();
        for (const auto& Field : Fields)
        {
            const auto* Found = Document->Values.Find(*Field);
            if (!Found)
            {
                return FString();
            }
            Stable->SetField(Field, *Found);
        }
        FString Bytes;
        FJsonSerializer::Serialize(Stable, TJsonWriterFactory<>::Create(&Bytes));
        return Bytes;
    }

    /** 실제 저장 확인을 마친 현재 프로필인지 검사한다. */
    bool ResultsSaved(const AFablePlayerState* Profile)
    {
        return Profile && Profile->IsBackendReady() && !Profile->IsBackendSavePending() &&
               Profile->bLastSaveSucceeded &&
               Profile->GetBackendRevision() == Profile->GetBackendAcknowledgedRevision();
    }

    /** NPC 보고 없이 첫 완료 또는 재도전 한 처치의 정확한 개인 수치를 확인한다. */
    bool ResultsReward(const TSharedPtr<FJsonObject>& Value, bool bExtraKill)
    {
        auto Wallet = ResultsObject(Value, TEXT("wallet"));
        return Value && Wallet && Value->GetIntegerField(TEXT("level")) == 4 &&
               Value->GetIntegerField(TEXT("experience")) == (bExtraKill ? 100 : 65) &&
               Value->GetIntegerField(TEXT("bagCount")) == 6 && Value->GetIntegerField(TEXT("forestQuest")) == 3 &&
               Value->GetIntegerField(TEXT("dungeonQuest")) == 2 &&
               Value->GetIntegerField(TEXT("dungeonCompletions")) == 0 &&
               Wallet->GetIntegerField(TEXT("gold")) == (bExtraKill ? 401 : 383) &&
               Wallet->GetIntegerField(TEXT("ore")) == (bExtraKill ? 22 : 21) &&
               Wallet->GetIntegerField(TEXT("rootCores")) == 1;
    }

    /** 첫 완료는 정확히 여섯 수호자와 한 보스의 영구 지급만 포함한다. */
    bool ResultsCleared(const FFablePersonalExpeditionResult& Value)
    {
        if (!Value.IsTerminal() || Value.Outcome != EFableExpeditionOutcome::Cleared || !Value.bRequiresBackendCommit ||
            !Value.bAllGrantsCommitted || !Value.bSaveReady || Value.Receipts.Num() != 7)
        {
            return false;
        }
        TSet<FGuid> Grants;
        int32 Gold = 0, Ore = 0, Experience = 0, Cores = 0, Items = 0, Bosses = 0;
        for (const auto& Receipt : Value.Receipts)
        {
            if (!Receipt.GrantId.IsValid() || Grants.Contains(Receipt.GrantId) || Receipt.Sequence <= 0 ||
                Receipt.ExpeditionId != Value.Ticket.ExpeditionId ||
                Receipt.RecipientPublicKey != Value.RecipientPublicKey ||
                Receipt.SaveState != EFableExpeditionGrantSaveState::Committed || Receipt.CommittedRevision <= 0)
            {
                return false;
            }
            Grants.Add(Receipt.GrantId);
            Gold += Receipt.ActualGold;
            Ore += Receipt.ActualOre;
            Experience += Receipt.ActualExperience;
            Cores += Receipt.ActualRootCores;
            Items += Receipt.GrantedItems.Num();
            Bosses += Receipt.bBoss ? 1 : 0;
        }
        return Gold == 248 && Ore == 14 && Experience == 460 && Cores == 1 && Items == 1 && Bosses == 1;
    }

    /** 배열을 포함한 정본 필드는 동일한 JSON 형식으로 대조한다. */
    bool ResultsEqualField(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right,
                           const FString& Field)
    {
        const auto* A = Left ? Left->Values.Find(*Field) : nullptr;
        const auto* B = Right ? Right->Values.Find(*Field) : nullptr;
        if (!A || !B)
        {
            return false;
        }
        auto One = MakeShared<FJsonObject>(), Two = MakeShared<FJsonObject>();
        One->SetField(TEXT("value"), *A);
        Two->SetField(TEXT("value"), *B);
        FString First, Second;
        FJsonSerializer::Serialize(One, TJsonWriterFactory<>::Create(&First));
        FJsonSerializer::Serialize(Two, TJsonWriterFactory<>::Create(&Second));
        return First == Second;
    }

    /** 재도전의 유효한 개인 지급은 접속 중인 첫 캐릭터의 한 처치뿐이다. */
    bool ResultsRetryGrant(const TSharedPtr<FJsonObject>& Value, bool bKilled)
    {
        return Value && Value->GetIntegerField(TEXT("gold")) == (bKilled ? 18 : 0) &&
               Value->GetIntegerField(TEXT("ore")) == (bKilled ? 1 : 0) &&
               Value->GetIntegerField(TEXT("experience")) == (bKilled ? 35 : 0) &&
               Value->GetIntegerField(TEXT("rootCores")) == 0 && Value->GetIntegerField(TEXT("moonCrystals")) == 0 &&
               Value->GetIntegerField(TEXT("itemCount")) == 0 &&
               Value->GetArrayField(TEXT("receipts")).Num() == (bKilled ? 1 : 0) &&
               Value->GetBoolField(TEXT("saveReady"));
    }
}

/** 두 실제 캐릭터의 원정 증서·명부·개인 결과와 적 정본을 읽는다. */
TSharedPtr<FJsonObject> UFableOnlineValidationDriver::ResultsServerSnapshot(UWorld* World) const
{
    const auto* ServerMode = World ? World->GetAuthGameMode<AFableGameMode>() : nullptr;
    const auto* Manager = World ? AFablePartyManager::Find(World) : nullptr;
    if (!ServerMode || !Manager || World->GetNetMode() != NM_DedicatedServer)
    {
        return nullptr;
    }
    auto Snapshot = MakeShared<FJsonObject>();
    const auto Encounters = ServerMode->GetExpeditions();
    Snapshot->SetNumberField(TEXT("activeInstances"), Encounters.Num());
    AFablePartyEncounter* Encounter = Encounters.Num() == 1 ? Encounters[0] : nullptr;
    const auto View = Encounter ? FableAdventure::For(Encounter) : FableAdventure::Field(World);
    Snapshot->SetStringField(TEXT("activeExpeditionId"), ResultsId(View.ExpeditionId()));
    Snapshot->SetNumberField(TEXT("region"), View ? int32(View->ActiveRegion) : -1);
    Snapshot->SetNumberField(TEXT("difficulty"), View ? int32(View->ActiveDifficulty) : -1);
    Snapshot->SetNumberField(TEXT("stage"), View ? int32(View->AdventureStage) : -1);
    Snapshot->SetNumberField(TEXT("wave"), View ? View->DungeonWave : 0);
    Snapshot->SetNumberField(TEXT("defeated"), View ? View->DefeatedEnemies : 0);
    Snapshot->SetNumberField(TEXT("required"), View ? View->RequiredEnemies : 0);
    Snapshot->SetBoolField(TEXT("recovering"), View && View->bPartyRecoveryPending);
    Snapshot->SetArrayField(TEXT("worldOrigin"), ResultsVector(View.WorldOrigin()));
    TArray<TSharedPtr<FJsonValue>> TicketKeys, Votes;
    if (Encounter)
    {
        Snapshot->SetStringField(TEXT("ticketPartyId"), ResultsId(Encounter->GetTicket().PartyId));
        Snapshot->SetStringField(TEXT("ticketExpeditionId"), ResultsId(Encounter->GetTicket().ExpeditionId));
        Snapshot->SetNumberField(TEXT("ticketRegion"), int32(Encounter->GetTicket().Region));
        Snapshot->SetNumberField(TEXT("ticketDifficulty"), int32(Encounter->GetTicket().Difficulty));
        Snapshot->SetNumberField(TEXT("ticketPreparationRevision"), Encounter->GetTicket().PreparationRevision);
        for (const auto& Key : Encounter->GetTicket().MemberKeys)
        {
            TicketKeys.Add(MakeShared<FJsonValueString>(Key));
        }
        Snapshot->SetNumberField(TEXT("departureRevision"), Encounter->GetDepartureVote().Revision);
        for (const auto& Key : Encounter->GetDepartureVote().ConsentingMemberKeys)
        {
            Votes.Add(MakeShared<FJsonValueString>(Key));
        }
    }
    Snapshot->SetArrayField(TEXT("ticketMembers"), TicketKeys);
    Snapshot->SetArrayField(TEXT("departureConsents"), Votes);
    TArray<AFableEnemy*> Living;
    for (TActorIterator<AFableEnemy> It(World); It; ++It)
    {
        if (It->IsAlive() && View.ExpeditionId().IsValid() && It->GetExpeditionId() == View.ExpeditionId())
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
    for (const auto* Enemy : Living)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("id"), ResultsId(Enemy->GetSpawnId()));
        Row->SetStringField(TEXT("expeditionId"), ResultsId(Enemy->GetExpeditionId()));
        Row->SetNumberField(TEXT("health"), Enemy->GetHealth());
        Enemies.Add(MakeShared<FJsonValueObject>(Row));
    }
    Snapshot->SetArrayField(TEXT("enemies"), Enemies);
    int32 Connected = 0, Reserved = 0;
    for (const FString& ClientRole : {FString(TEXT("client1")), FString(TEXT("client2"))})
    {
        const auto Client = ResultsClient(Directory, ClientRole);
        const auto ClientResults = ResultsObject(Client, TEXT("partyResults"));
        FString Key, OldId, RetryId;
        if (!ClientResults || !ClientResults->TryGetStringField(TEXT("selfKey"), Key) || Key.IsEmpty())
        {
            return nullptr;
        }
        ClientResults->TryGetStringField(TEXT("clearedExpeditionId"), OldId);
        ClientResults->TryGetStringField(TEXT("retryExpeditionId"), RetryId);
        FGuid OldGuid, RetryGuid;
        FGuid::Parse(OldId, OldGuid);
        FGuid::Parse(RetryId, RetryGuid);
        const FFablePartyRecord* Party = Manager->GetPartyForMember(Key);
        if (Party)
        {
            Reserved = Party->Members.Num();
            Snapshot->SetStringField(TEXT("partyId"), ResultsId(Party->PartyId));
        }
        APlayerController* Player = Manager->GetConnectedPlayer(Key);
        auto* ProfileValue = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        auto* HeroValue = Player ? Player->GetPawn<AFableCharacter>() : nullptr;
        const auto MemberView = FableAdventure::For(ProfileValue);
        auto Member = MakeShared<FJsonObject>();
        Member->SetStringField(TEXT("publicKey"), Key);
        Member->SetBoolField(TEXT("connected"), ProfileValue && HeroValue);
        Connected += ProfileValue && HeroValue ? 1 : 0;
        if (ProfileValue && HeroValue)
        {
            Member->SetObjectField(TEXT("profile"), Profile(ProfileValue));
            Member->SetStringField(TEXT("expeditionId"), ResultsId(ProfileValue->ActiveExpeditionId));
            Member->SetBoolField(TEXT("alive"), HeroValue->IsAlive());
            Member->SetBoolField(TEXT("saved"), ResultsSaved(ProfileValue));
            Member->SetBoolField(TEXT("inTown"), MemberView && MemberView.IsInRegion(EFableRegion::Village,
                                                                                     HeroValue->GetActorLocation()));
            Member->SetArrayField(TEXT("position"), ResultsVector(HeroValue->GetActorLocation()));
            Member->SetArrayField(
                TEXT("canonicalPosition"),
                ResultsVector(MemberView.ToCanonical(EFableRegion::RootRuins, HeroValue->GetActorLocation())));
            float Nearest = 100000;
            for (const auto* Enemy : Living)
            {
                Nearest = FMath::Min(Nearest,
                                     float(FVector::Dist2D(HeroValue->GetActorLocation(), Enemy->GetActorLocation())));
            }
            Member->SetNumberField(TEXT("nearestEnemyDistanceCm"), Nearest);
            Member->SetBoolField(TEXT("safe"),
                                 HeroValue->IsAlive() && ResultsSaved(ProfileValue) && MemberView &&
                                     MemberView.IsInRegion(MemberView->ActiveRegion, HeroValue->GetActorLocation()) &&
                                     Nearest >= 1800);
            if (const auto* OldResult = ServerMode->GetResultByExpedition(ProfileValue, OldGuid))
            {
                Member->SetObjectField(TEXT("clearedResult"), ResultsReceipt(*OldResult));
                Member->SetBoolField(TEXT("clearReceiptValid"), ResultsCleared(*OldResult));
            }
            if (const auto* RetryResult = ServerMode->GetResultByExpedition(ProfileValue, RetryGuid))
            {
                Member->SetObjectField(TEXT("retryResult"), ResultsReceipt(*RetryResult));
            }
        }
        Snapshot->SetObjectField(ClientRole == TEXT("client1") ? TEXT("a") : TEXT("b"), Member);
    }
    Snapshot->SetNumberField(TEXT("connectedMembers"), Connected);
    Snapshot->SetNumberField(TEXT("reservedMembers"), Reserved);
    return Snapshot;
}

/** 실제 UI와 저장·연결·진행 관찰이 모두 만족된 사건만 한 번씩 고정한다. */
void UFableOnlineValidationDriver::ObserveResultsServer(UWorld* World)
{
    auto Snapshot = ResultsServerSnapshot(World);
    auto A = ResultsObject(Snapshot, TEXT("a")), B = ResultsObject(Snapshot, TEXT("b"));
    if (!A || !B)
    {
        return;
    }
    if (!ServerResultsReport)
    {
        ServerResultsReport = MakeShared<FJsonObject>();
        ServerResultsReport->SetBoolField(TEXT("serverRestartReceiptPersistenceClaimed"), false);
    }
    ServerResultsReport->SetObjectField(TEXT("liveSnapshot"), Snapshot);
    const auto Save = [&](const FString& Name, const FString& Check)
    {
        ServerResultsReport->SetObjectField(Name, Snapshot);
        Checks.Add(Check, true);
        Publish(TEXT("running"));
    };
    const auto ClientA = ResultsClient(Directory, TEXT("client1"));
    const auto ClientB = ResultsClient(Directory, TEXT("client2"));
    const bool Both = Snapshot->GetIntegerField(TEXT("connectedMembers")) == 2 &&
                      Snapshot->GetIntegerField(TEXT("reservedMembers")) == 2;
    const auto ClearA = ResultsObject(A, TEXT("clearedResult")), ClearB = ResultsObject(B, TEXT("clearedResult"));
    auto Cleared = ResultsObject(ServerResultsReport, TEXT("cleared"));
    if (!Cleared && Both && A->HasTypedField<EJson::Boolean>(TEXT("clearReceiptValid")) &&
        A->GetBoolField(TEXT("clearReceiptValid")) && B->HasTypedField<EJson::Boolean>(TEXT("clearReceiptValid")) &&
        B->GetBoolField(TEXT("clearReceiptValid")) && ResultsReward(ResultsObject(A, TEXT("profile")), false) &&
        ResultsReward(ResultsObject(B, TEXT("profile")), false) && A->GetBoolField(TEXT("saved")) &&
        B->GetBoolField(TEXT("saved")))
    {
        Save(TEXT("cleared"), TEXT("server_results_one_boss_clear_saved"));
        return;
    }
    if (!Cleared)
    {
        return;
    }
    const auto OriginalA = ResultsObject(ResultsObject(Cleared, TEXT("a")), TEXT("clearedResult"));
    const auto OriginalB = ResultsObject(ResultsObject(Cleared, TEXT("b")), TEXT("clearedResult"));
    const bool ClearSameA = ClearA && ResultsSignature(ClearA) == ResultsSignature(OriginalA);
    const bool ClearSameB = ClearB && ResultsSignature(ClearB) == ResultsSignature(OriginalB);
    if (!ResultsObject(ServerResultsReport, TEXT("resultsAcknowledged")) && Both && ClearSameA && ClearSameB &&
        ClearA->GetBoolField(TEXT("acknowledged")) && ClearB->GetBoolField(TEXT("acknowledged")) &&
        ResultsCheck(ClientA, TEXT("results_reopened_without_grant")) &&
        ResultsCheck(ClientB, TEXT("results_reopened_without_grant")) &&
        ResultsReward(ResultsObject(A, TEXT("profile")), false) &&
        ResultsReward(ResultsObject(B, TEXT("profile")), false))
    {
        Save(TEXT("resultsAcknowledged"), TEXT("server_results_reopen_ack_no_duplicate"));
        return;
    }
    auto Before = ResultsObject(ServerResultsReport, TEXT("retryBefore"));
    const FString ActiveId = Snapshot->GetStringField(TEXT("activeExpeditionId"));
    const bool Retry = ActiveId != OriginalA->GetStringField(TEXT("expeditionId")) && ActiveId != ResultsId(FGuid()) &&
                       Snapshot->GetIntegerField(TEXT("wave")) == 1 &&
                       Snapshot->GetIntegerField(TEXT("required")) == 3 && !Snapshot->GetBoolField(TEXT("recovering"));
    if (!Before && ResultsObject(ServerResultsReport, TEXT("resultsAcknowledged")) && Both && Retry && ClearSameA &&
        ClearSameB && Snapshot->GetIntegerField(TEXT("defeated")) == 0 &&
        Snapshot->GetArrayField(TEXT("enemies")).Num() == 3 && A->GetBoolField(TEXT("safe")) &&
        B->GetBoolField(TEXT("safe")) && ResultsCheck(ClientA, TEXT("results_retry_safe_before_disconnect")) &&
        ResultsCheck(ClientB, TEXT("results_retry_safe_before_disconnect")) &&
        ResultsReward(ResultsObject(A, TEXT("profile")), false) &&
        ResultsReward(ResultsObject(B, TEXT("profile")), false))
    {
        Save(TEXT("retryBefore"), TEXT("server_results_retry_new_uuid_two_arrived"));
        return;
    }
    if (!Before)
    {
        return;
    }
    const bool SameRetry = ActiveId == Before->GetStringField(TEXT("activeExpeditionId"));
    const bool Solo = Snapshot->GetIntegerField(TEXT("connectedMembers")) == 1 &&
                      Snapshot->GetIntegerField(TEXT("reservedMembers")) == 2 && A->GetBoolField(TEXT("connected")) &&
                      !B->GetBoolField(TEXT("connected"));
    if (!ResultsObject(ServerResultsReport, TEXT("retryDisconnected")) && SameRetry && Solo &&
        Snapshot->GetIntegerField(TEXT("defeated")) == 0 && ResultsEqualField(Before, Snapshot, TEXT("enemies")))
    {
        Save(TEXT("retryDisconnected"), TEXT("server_results_retry_member_reserved"));
        return;
    }
    auto SoloKill = ResultsObject(ServerResultsReport, TEXT("retrySoloKill"));
    if (!SoloKill && ResultsObject(ServerResultsReport, TEXT("retryDisconnected")) && SameRetry && Solo &&
        Snapshot->GetIntegerField(TEXT("defeated")) == 1 && Snapshot->GetArrayField(TEXT("enemies")).Num() == 2 &&
        A->GetBoolField(TEXT("safe")) && ResultsReward(ResultsObject(A, TEXT("profile")), true) &&
        ResultsRetryGrant(ResultsObject(A, TEXT("retryResult")), true) && ClearSameA &&
        ResultsCheck(ClientA, TEXT("results_solo_kill_saved")))
    {
        Save(TEXT("retrySoloKill"), TEXT("server_results_online_member_only_reward"));
        return;
    }
    if (!ResultsObject(ServerResultsReport, TEXT("retryRejoined")) && SoloKill && Both && SameRetry && ClearSameA &&
        ClearSameB && ResultsEqualField(SoloKill, Snapshot, TEXT("enemies")) &&
        Snapshot->GetIntegerField(TEXT("defeated")) == 1 && A->GetBoolField(TEXT("safe")) &&
        B->GetBoolField(TEXT("safe")) && ResultsReward(ResultsObject(A, TEXT("profile")), true) &&
        ResultsReward(ResultsObject(B, TEXT("profile")), false) &&
        ResultsRetryGrant(ResultsObject(A, TEXT("retryResult")), true) &&
        ResultsRetryGrant(ResultsObject(B, TEXT("retryResult")), false) &&
        ResultsCheck(ClientB, TEXT("results_same_character_new_uuid_restored")))
    {
        Save(TEXT("retryRejoined"), TEXT("server_results_same_retry_uuid_rejoined"));
        return;
    }
    if (!ResultsObject(ServerResultsReport, TEXT("retreatOneVote")) &&
        ResultsObject(ServerResultsReport, TEXT("retryRejoined")) && Both && SameRetry &&
        Snapshot->GetArrayField(TEXT("departureConsents")).Num() == 1)
    {
        Save(TEXT("retreatOneVote"), TEXT("server_results_single_vote_did_not_return_party"));
        return;
    }
    const auto AbandonedA = ResultsObject(A, TEXT("retryResult")), AbandonedB = ResultsObject(B, TEXT("retryResult"));
    if (!ResultsObject(ServerResultsReport, TEXT("abandoned")) &&
        ResultsObject(ServerResultsReport, TEXT("retreatOneVote")) && Both &&
        Snapshot->GetIntegerField(TEXT("activeInstances")) == 0 && A->GetBoolField(TEXT("inTown")) &&
        B->GetBoolField(TEXT("inTown")) && A->GetBoolField(TEXT("saved")) && B->GetBoolField(TEXT("saved")) &&
        AbandonedA && AbandonedB &&
        AbandonedA->GetIntegerField(TEXT("outcome")) == int32(EFableExpeditionOutcome::Abandoned) &&
        AbandonedB->GetIntegerField(TEXT("outcome")) == int32(EFableExpeditionOutcome::Abandoned) &&
        ResultsRetryGrant(AbandonedA, true) && ResultsRetryGrant(AbandonedB, false) && ClearSameA && ClearSameB &&
        ResultsReward(ResultsObject(A, TEXT("profile")), true) &&
        ResultsReward(ResultsObject(B, TEXT("profile")), false))
    {
        Save(TEXT("abandoned"), TEXT("server_results_consented_own_return_preserved_grants"));
    }
}

/** 보스를 한 번 처치한 뒤 새 원정의 개인 지급·동일 캐릭터 복구·전원 철수까지 연결한다. */
bool UFableOnlineValidationDriver::TickPartyResults(AFablePlayerController* PC, AFablePlayerState* P,
                                                    AFableCharacter* Hero, double Now)
{
    auto* Manager = PC ? AFablePartyManager::Find(PC->GetWorld()) : nullptr;
    if (!PC || !P || !Hero || !Manager || P->PartyPlayerKey.IsEmpty())
    {
        return false;
    }
    const bool A = Role == TEXT("client1");
    const auto View = FableAdventure::For(P);
    const FString MainFolder =
        bResumeExpedition ? FPaths::Combine(FPaths::GetPath(Directory), TEXT("register")) : Directory;
    const auto PeerDocument = ResultsClient(Directory, A ? TEXT("client2") : TEXT("client1"));
    const auto ServerDocument = ResultsRead(FPaths::Combine(MainFolder, TEXT("server.json")));
    const auto* Party = Manager->GetPartyForMember(P->PartyPlayerKey);
    const auto Advance = [&](int32 Next)
    {
        DungeonHold(PC, EKeys::W, false);
        DungeonHold(PC, EKeys::S, false);
        DungeonHold(PC, EKeys::D, false);
        DungeonHold(PC, EKeys::E, false);
        ResultsStep = Next;
        ResultsStepAt = Now;
        bResultsRequestSent = false;
        ResultsReport->SetNumberField(TEXT("step"), Next);
    };
    const auto Mark = [&](const FString& Name)
    {
        Checks.Add(Name, true);
        Publish(TEXT("running"));
    };
    const auto Send = [&](TFunctionRef<void()> Request)
    {
        if (!bResultsRequestSent && !PC->IsExpeditionActionPending() && !PC->IsPartyPreparationPending() &&
            ResultsSaved(P) && Now - ResultsLastSentAt > .8)
        {
            Request();
            bResultsRequestSent = true;
            ResultsLastSentAt = Now;
        }
    };
    const auto ActionOk = [&]()
    {
        return bResultsRequestSent && !PC->IsExpeditionActionPending() && PC->DidExpeditionActionSucceed();
    };
    const auto PartyOk = [&]()
    {
        return bResultsRequestSent && !PC->IsPartyPreparationPending() && PC->DidPartyPreparationSucceed();
    };
    if (!ResultsReport)
    {
        ResultsSelfKey = P->PartyPlayerKey;
        ResultsStepAt = Now;
        ResultsReport = MakeShared<FJsonObject>();
        ResultsReport->SetStringField(TEXT("selfKey"), ResultsSelfKey);
        ResultsReport->SetBoolField(TEXT("resumedProcess"), bResumeExpedition);
        ResultsReport->SetBoolField(TEXT("serverRestartReceiptPersistenceClaimed"), false);
        if (bResumeExpedition)
        {
            FString OldId, RetryId, PartyId, Completion;
            const TSharedPtr<FJsonObject>* Original = nullptr;
            const TSharedPtr<FJsonObject>* Interrupted = nullptr;
            if (!Expected || !Expected->TryGetStringField(TEXT("clearedExpeditionId"), OldId) ||
                !Expected->TryGetStringField(TEXT("retryExpeditionId"), RetryId) ||
                !Expected->TryGetStringField(TEXT("partyId"), PartyId) ||
                !Expected->TryGetStringField(TEXT("completionId"), Completion) ||
                !Expected->TryGetObjectField(TEXT("originalClearResult"), Original) ||
                !Expected->TryGetObjectField(TEXT("interruptedCheckpoint"), Interrupted) ||
                !FGuid::Parse(OldId, ResultsClearedId) || !FGuid::Parse(RetryId, ResultsRetryId) ||
                !FGuid::Parse(PartyId, ResultsPartyId) || !FGuid::Parse(Completion, ResultsCompletionId) ||
                ResultsClearedId == ResultsRetryId)
            {
                Finish(false, TEXT("Result reconnect context is incomplete."));
                return false;
            }
            ResultsReport->SetObjectField(TEXT("originalClearResult"), *Original);
            ResultsReport->SetObjectField(TEXT("interruptedCheckpoint"), *Interrupted);
            ResultsReport->SetStringField(TEXT("partyId"), PartyId);
            ResultsReport->SetStringField(TEXT("clearedExpeditionId"), OldId);
            ResultsReport->SetStringField(TEXT("retryExpeditionId"), RetryId);
            ResultsStep = 20;
        }
        else
        {
            ResultsReport->SetObjectField(TEXT("forestCheckpoint"), Profile(P));
        }
        ResultsReport->SetNumberField(TEXT("step"), ResultsStep);
    }

    // 전투·이동·저장 진단은 합격 증명과 분리하고 매번 실제 관측 값으로 갱신한다.
    TArray<AFableEnemy*> Living;
    float Nearest = 100000;
    if (View && View.ExpeditionId().IsValid())
    {
        for (TActorIterator<AFableEnemy> It(Hero->GetWorld()); It; ++It)
        {
            if (It->IsAlive() && It->GetExpeditionId() == View.ExpeditionId())
            {
                Living.Add(*It);
                Nearest = FMath::Min(Nearest, float(FVector::Dist2D(Hero->GetActorLocation(), It->GetActorLocation())));
            }
        }
    }
    const bool Safe = Hero->IsAlive() && ResultsSaved(P) && View &&
                      View.IsInRegion(View->ActiveRegion, Hero->GetActorLocation()) && Nearest >= 1800;
    auto Diagnostics = MakeShared<FJsonObject>();
    Diagnostics->SetArrayField(TEXT("position"), ResultsVector(Hero->GetActorLocation()));
    Diagnostics->SetArrayField(TEXT("canonicalPosition"),
                               ResultsVector(View.ToCanonical(EFableRegion::RootRuins, Hero->GetActorLocation())));
    Diagnostics->SetArrayField(TEXT("velocity"), ResultsVector(Hero->GetVelocity()));
    Diagnostics->SetNumberField(TEXT("elapsedSeconds"), Now - ResultsStepAt);
    Diagnostics->SetNumberField(TEXT("waypoint"), ReconnectWaypoint);
    Diagnostics->SetNumberField(TEXT("nearestEnemyDistanceCm"), Nearest);
    Diagnostics->SetNumberField(TEXT("livingEnemies"), Living.Num());
    Diagnostics->SetNumberField(TEXT("health"), Hero->GetHealth());
    Diagnostics->SetBoolField(TEXT("safe"), Safe);
    Diagnostics->SetBoolField(TEXT("saved"), ResultsSaved(P));
    Diagnostics->SetStringField(TEXT("activeExpeditionId"), ResultsId(P->ActiveExpeditionId));
    ResultsReport->SetObjectField(TEXT("diagnostics"), Diagnostics);
    if (Now - ResultsStepAt > (ResultsStep == 1 ? 300 : 150))
    {
        Capture(FString::Printf(TEXT("results-timeout-%d"), ResultsStep));
        Finish(false, FString::Printf(TEXT("Party result step %d timed out."), ResultsStep));
        return false;
    }

    // 같은 원정 안에서는 마지막 대기 지점만 분리해 두 캐릭터가 서로의 길을 막지 않게 한다.
    const auto Escape = [&]()
    {
        if (!Hero->IsAlive())
        {
            return false;
        }
        PC->UIClosePanels();
        if (Hero->GetHealth() < Hero->GetMaxHealth() * .45f && P->HealthPotions > 0 &&
            P->GetPotionCooldownRemaining() <= 0 && Now - ResultsLastSentAt > 1)
        {
            PC->UIUseHealthPotion();
            ResultsLastSentAt = Now;
        }
        if (ReconnectWaypoint < 3)
        {
            ReconnectWalkSafe(PC, true);
            return false;
        }
        const FVector Canonical = A ? FVector(6300, -1580, 100) : FVector(6480, -1700, 100);
        return DungeonWalk(PC, View.ToWorld(EFableRegion::RootRuins, Canonical), 65) &&
               Hero->GetVelocity().Size2D() < 10;
    };
    const auto ReceiptStable = [&](const FFablePersonalExpeditionResult* Displayed)
    {
        auto First =
            ResultsObject(ResultsReport, bResumeExpedition ? TEXT("originalClearResult") : TEXT("clearResult"));
        return Displayed && ResultsCleared(*Displayed) &&
               ResultsSignature(ResultsReceipt(*Displayed)) == ResultsSignature(First);
    };
    const auto AllReady = [&]()
    {
        return Party && Party->Members.Num() == 2 &&
               Party->Members.ContainsByPredicate(
                   [&](const FFablePartyMember& Member)
                   {
                       return Member.PlayerKey == ResultsSelfKey;
                   }) &&
               !Party->Members.ContainsByPredicate(
                   [&](const FFablePartyMember& Member)
                   {
                       return !Member.bConnected || Member.ReadyRevision != Party->PreparationRevision;
                   });
    };

    switch (ResultsStep)
    {
    case 0:
        if (TickExpeditionPreparation(PC, P, Hero, Now) && View && View.ExpeditionId().IsValid())
        {
            ResultsPartyId = P->PartyId;
            ResultsClearedId = View.ExpeditionId();
            ResultsReport->SetStringField(TEXT("partyId"), ResultsId(ResultsPartyId));
            ResultsReport->SetStringField(TEXT("clearedExpeditionId"), ResultsId(ResultsClearedId));
            Mark(TEXT("results_initial_exact_two_arrived"));
            Capture(TEXT("results-first-expedition"));
            Advance(1);
        }
        break;
    case 1:
    {
        if (!View || View.ExpeditionId() != ResultsClearedId || View->bPartyRecoveryPending || Hero->IsDefeated())
        {
            Finish(false, TEXT("First result expedition was lost before boss completion."));
            break;
        }
        if (View->DungeonWave >= 2)
        {
            Checks.Add(TEXT("results_second_wave_played"), true);
        }
        if (View->AdventureStage == EFableAdventureStage::Boss)
        {
            Checks.Add(TEXT("results_boss_played"), true);
        }
        if (View->AdventureStage == EFableAdventureStage::Cleared)
        {
            // 전투가 끝난 뒤 실제 행동 불능 동료가 있으면 일반 부활 UI로 먼저 돕는다.
            for (APlayerState* Entry : View.Players())
            {
                auto* Other = Entry ? Cast<AFableCharacter>(Entry->GetPawn()) : nullptr;
                if (Other && Other->IsDowned())
                {
                    // 완료 결과의 자동 표시가 일반 이동과 부활 입력을 가로막지 않게 닫는다.
                    PC->UIClosePanels();
                    if (Hero->IsAlive() && Other != Hero && DungeonWalk(PC, Other->GetActorLocation(), 170) &&
                        Hero->GetVelocity().Size2D() < 10)
                    {
                        if (Hero->GetReviveTarget() == Other)
                        {
                            DungeonHold(PC, EKeys::E, true);
                        }
                        else if (Now - ResultsLastSentAt > 1)
                        {
                            PC->UIStartRevive(Other);
                            ResultsLastSentAt = Now;
                        }
                    }
                    return false;
                }
            }
            DungeonHold(PC, EKeys::W, false);
            DungeonHold(PC, EKeys::S, false);
            DungeonHold(PC, EKeys::D, false);
            const auto& Value = P->GetExpeditionResult();
            if (Hero->IsAlive() && ResultsSaved(P) && ResultsReward(Profile(P), false) && ResultsCleared(Value) &&
                Value.Ticket.ExpeditionId == ResultsClearedId &&
                ResultsCheck(ServerDocument, TEXT("server_results_one_boss_clear_saved")))
            {
                ResultsCompletionId = Value.CompletionId;
                ResultsReport->SetObjectField(TEXT("firstClearProfile"), Profile(P));
                ResultsReport->SetObjectField(TEXT("clearResult"), ResultsReceipt(Value));
                Mark(TEXT("results_clear_receipts_saved"));
                Capture(TEXT("results-boss-cleared"));
                Advance(2);
            }
            break;
        }
        auto* Target = DungeonTarget(Hero);
        const bool Windup =
            Hero->IsAlive() && Target && Target->IsBoss() && Target->GetEnemyState() == EFableEnemyState::Windup;
        DungeonHold(PC, EKeys::D, Windup);
        if (Windup)
        {
            PC->UIClosePanels();
            PC->SetControlRotation(
                FRotator(-12, (Target->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
            DungeonHold(PC, EKeys::W, false);
            DungeonHold(PC, EKeys::S, false);
            if (Hero->GetDodgeCooldownRemaining() <= 0 && Now - ResultsBossDodgeAt > 1 &&
                Hero->GetVelocity().Size2D() > 80 && Target->GetWindupEndServerTime() - View.ServerTime() < .5)
            {
                DungeonKey(PC, EKeys::LeftShift);
                ResultsBossDodgeAt = Now;
            }
        }
        else
        {
            DungeonFight(PC, Now);
        }
        break;
    }
    case 2:
        Send(
            [&]()
            {
                PC->UIOpenExpeditionResult(ResultsClearedId);
            });
        if (ActionOk() && PC->IsExpeditionResultOpen() && ReceiptStable(PC->GetDisplayedExpeditionResult()))
        {
            Mark(TEXT("results_opened_own_result"));
            Advance(3);
        }
        break;
    case 3:
        PC->UIClosePanels();
        if (!PC->IsExpeditionResultOpen())
        {
            Advance(4);
        }
        break;
    case 4:
        Send(
            [&]()
            {
                PC->UIOpenExpeditionResult(ResultsClearedId);
            });
        if (ActionOk() && PC->IsExpeditionResultOpen() && ReceiptStable(PC->GetDisplayedExpeditionResult()) &&
            ResultsSignature(Profile(P), true) ==
                ResultsSignature(ResultsObject(ResultsReport, TEXT("firstClearProfile")), true))
        {
            ResultsReport->SetObjectField(TEXT("reopenedClearResult"),
                                          ResultsReceipt(*PC->GetDisplayedExpeditionResult()));
            Mark(TEXT("results_reopened_without_grant"));
            Capture(TEXT("results-personal-reopened"));
            Advance(5);
        }
        break;
    case 5:
        Send(
            [&]()
            {
                PC->UIAcknowledgeExpeditionResult();
            });
        if (ActionOk() && ReceiptStable(PC->GetDisplayedExpeditionResult()) &&
            PC->GetDisplayedExpeditionResult()->bAcknowledged)
        {
            ResultsReport->SetObjectField(TEXT("acknowledgedClearResult"),
                                          ResultsReceipt(*PC->GetDisplayedExpeditionResult()));
            Mark(TEXT("results_acknowledged_without_grant"));
            ResultsPreviousRevision = Party ? Party->PreparationRevision : 0;
            Advance(6);
        }
        break;
    case 6:
        if (!ResultsCheck(ServerDocument, TEXT("server_results_reopen_ack_no_duplicate")) || !Party)
        {
            break;
        }
        if (A)
        {
            Send(
                [&]()
                {
                    PC->UIRequestPrepareExpeditionRetry(ResultsPartyId, Party->PreparationRevision, ResultsClearedId,
                                                        ResultsCompletionId);
                });
        }
        if (Party->RetrySourceExpeditionId == ResultsClearedId &&
            Party->PreparationRevision != ResultsPreviousRevision && Party->Members.Num() == 2 &&
            !Party->Members.ContainsByPredicate(
                [](const FFablePartyMember& Member)
                {
                    return Member.ReadyRevision != 0;
                }) &&
            (!A || PartyOk()))
        {
            PC->UIOpenPartyExpedition();
            Mark(TEXT("results_retry_resets_all_consent"));
            Advance(7);
        }
        break;
    case 7:
        if (!Party)
        {
            break;
        }
        if (A)
        {
            Send(
                [&]()
                {
                    PC->UIRequestRetryExpedition(ResultsPartyId, Party->PreparationRevision, ResultsClearedId,
                                                 ResultsCompletionId);
                });
            if (bResultsRequestSent && !PC->IsPartyPreparationPending() && !PC->DidPartyPreparationSucceed() &&
                P->ActiveExpeditionId == ResultsClearedId && !AllReady())
            {
                ResultsReport->SetStringField(TEXT("unreadyRetryMessage"), PC->GetPartyPreparationMessage());
                Mark(TEXT("results_retry_unready_rejected"));
                Advance(8);
            }
        }
        else if (ResultsCheck(PeerDocument, TEXT("results_retry_unready_rejected")))
        {
            Mark(TEXT("results_retry_unready_rejected"));
            Advance(8);
        }
        break;
    case 8:
        Send(
            [&]()
            {
                PC->UISetPartyReady(true);
            });
        if (PartyOk() && AllReady())
        {
            ResultsReport->SetNumberField(TEXT("retryPreparationRevision"), Party->PreparationRevision);
            Mark(TEXT("results_retry_fresh_both_ready"));
            Advance(9);
        }
        break;
    case 9:
        if (A && Party && ResultsCheck(PeerDocument, TEXT("results_retry_fresh_both_ready")))
        {
            Send(
                [&]()
                {
                    PC->UIRequestRetryExpedition(ResultsPartyId, Party->PreparationRevision, ResultsClearedId,
                                                 ResultsCompletionId);
                });
        }
        if (View && P->ActiveExpeditionId.IsValid() && P->ActiveExpeditionId != ResultsClearedId &&
            View.ExpeditionId() == P->ActiveExpeditionId && View.Players().Num() == 2 &&
            View->ActiveRegion == EFableRegion::RootRuins &&
            View.IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()) && View->DungeonWave == 1 &&
            View->DefeatedEnemies == 0 && (!A || PartyOk()))
        {
            ResultsRetryId = View.ExpeditionId();
            ResultsReport->SetStringField(TEXT("retryExpeditionId"), ResultsId(ResultsRetryId));
            Mark(TEXT("results_retry_new_uuid_two_arrived"));
            ReconnectWaypoint = 0;
            Advance(10);
        }
        break;
    case 10:
        if (!Escape() || !Safe || !ResultsSaved(P) || !View || View.ExpeditionId() != ResultsRetryId ||
            View->DefeatedEnemies != 0 || Living.Num() != 3)
        {
            break;
        }
        if (!Checks.FindRef(TEXT("results_retry_safe_before_disconnect")))
        {
            ResultsReport->SetObjectField(TEXT("retryBeforeProfile"), Profile(P));
            Mark(TEXT("results_retry_safe_before_disconnect"));
        }
        if (!ResultsCheck(ServerDocument, TEXT("server_results_retry_new_uuid_two_arrived")))
        {
            break;
        }
        if (!A)
        {
            Checkpoint = Profile(P);
            ResultsReport->SetStringField(TEXT("status"), TEXT("crash-ready"));
            Capture(TEXT("results-before-disconnect"));
            Step = 30;
            StepAt = Now;
            LastAction = -1;
            return false;
        }
        Advance(11);
        break;
    case 11:
        if (!ResultsCheck(ServerDocument, TEXT("server_results_retry_member_reserved")))
        {
            break;
        }
        if (!View || View.ExpeditionId() != ResultsRetryId || !Hero->IsAlive() || View->DefeatedEnemies > 1 ||
            View->bPartyRecoveryPending)
        {
            Finish(false, TEXT("Surviving player lost the retry expedition."));
            break;
        }
        if (View->DefeatedEnemies == 0)
        {
            PC->UIClosePanels();
            if (!ResultsVictim.IsValid())
            {
                for (auto* Enemy : Living)
                {
                    if (!ResultsVictim.IsValid() || Enemy->GetActorLocation().X < ResultsVictim->GetActorLocation().X)
                    {
                        ResultsVictim = Enemy;
                    }
                }
            }
            if (auto* Target = ResultsVictim.Get(); Target && Target->IsAlive())
            {
                const float Distance = FVector::Dist2D(Hero->GetActorLocation(), Target->GetActorLocation());
                PC->SetControlRotation(
                    FRotator(-12, (Target->GetActorLocation() - Hero->GetActorLocation()).Rotation().Yaw, 0));
                DungeonHold(PC, EKeys::W, Distance > 600);
                DungeonHold(PC, EKeys::S, Distance < 400);
                if (Distance < 1050 && Now - DungeonAttackAt > .8)
                {
                    DungeonKey(PC, EKeys::J);
                    DungeonAttackAt = Now;
                }
            }
        }
        else if (View->DefeatedEnemies == 1 && Living.Num() == 2)
        {
            if (!bResultsKillInputStopped)
            {
                DungeonHold(PC, EKeys::W, false);
                DungeonHold(PC, EKeys::S, false);
                DungeonHold(PC, EKeys::J, false);
                ReconnectWaypoint = 0;
                bResultsKillInputStopped = true;
            }
            if (Escape() && Safe && ResultsReward(Profile(P), true))
            {
                ResultsReport->SetObjectField(TEXT("soloKillProfile"), Profile(P));
                Mark(TEXT("results_solo_kill_saved"));
                if (ResultsCheck(ServerDocument, TEXT("server_results_online_member_only_reward")))
                {
                    Capture(TEXT("results-solo-retry-progress"));
                    Advance(12);
                }
            }
        }
        break;
    case 12:
        if (ResultsCheck(ServerDocument, TEXT("server_results_same_retry_uuid_rejoined")) &&
            ResultsCheck(PeerDocument, TEXT("results_same_character_new_uuid_restored")))
        {
            Mark(TEXT("results_peer_server_restoration_observed"));
            Advance(30);
        }
        break;
    case 20:
        if (!View || View.ExpeditionId() != ResultsRetryId || P->PartyId != ResultsPartyId || View->DungeonWave != 1 ||
            View->DefeatedEnemies != 1 || Living.Num() != 2 ||
            !View.IsInRegion(EFableRegion::RootRuins, Hero->GetActorLocation()))
        {
            break;
        }
        Send(
            [&]()
            {
                PC->UIOpenExpeditionResult(ResultsClearedId);
            });
        if (ActionOk() && ReceiptStable(PC->GetDisplayedExpeditionResult()) &&
            PC->GetDisplayedExpeditionResult()->CompletionId == ResultsCompletionId)
        {
            ResultsReport->SetObjectField(TEXT("restoredClearResult"),
                                          ResultsReceipt(*PC->GetDisplayedExpeditionResult()));
            Mark(TEXT("results_original_receipt_restored"));
            Advance(21);
        }
        break;
    case 21:
        Send(
            [&]()
            {
                PC->UIRequestExpeditionResult(ResultsRetryId);
            });
        if (ActionOk() && PC->GetDisplayedExpeditionResult() &&
            PC->GetDisplayedExpeditionResult()->Ticket.ExpeditionId == ResultsRetryId &&
            ResultsRetryGrant(ResultsReceipt(*PC->GetDisplayedExpeditionResult()), false) &&
            ResultsSignature(Profile(P), true) ==
                ResultsSignature(ResultsObject(ResultsReport, TEXT("interruptedCheckpoint")), true))
        {
            ResultsReport->SetObjectField(TEXT("restoredRetryResult"),
                                          ResultsReceipt(*PC->GetDisplayedExpeditionResult()));
            Mark(TEXT("results_no_offline_reward_fullprofile_preserved"));
            ReconnectWaypoint = 0;
            Advance(22);
        }
        break;
    case 22:
        if (Escape() && Safe && View && View.ExpeditionId() == ResultsRetryId && View->DefeatedEnemies == 1 &&
            Living.Num() == 2 && ResultsReward(Profile(P), false) &&
            ResultsSignature(Profile(P), true) ==
                ResultsSignature(ResultsObject(ResultsReport, TEXT("interruptedCheckpoint")), true))
        {
            ResultsReport->SetObjectField(TEXT("restoredProfile"), Profile(P));
            Mark(TEXT("results_same_character_new_uuid_restored"));
            Capture(TEXT("results-own-retry-restored"));
            Advance(23);
        }
        break;
    case 23:
        if (ResultsCheck(ServerDocument, TEXT("server_results_same_retry_uuid_rejoined")) &&
            ResultsCheck(PeerDocument, TEXT("results_peer_server_restoration_observed")))
        {
            Mark(TEXT("results_peer_server_restoration_observed"));
            Advance(30);
        }
        break;
    case 30:
        // 마지막 동의가 먼저 마을 귀환을 완료해도 해당 요청의 실제 성공 응답을 소비한다.
        if (ActionOk())
        {
            Mark(TEXT("results_explicit_abandon_consent_sent"));
            Advance(31);
            break;
        }
        if (bResultsRequestSent)
        {
            break;
        }
        PC->UIOpenPartyExpedition();
        if (!A && !ResultsCheck(ServerDocument, TEXT("server_results_single_vote_did_not_return_party")))
        {
            break;
        }
        if (!View || View.ExpeditionId() != ResultsRetryId || View.Players().Num() != 2 || !ResultsSaved(P))
        {
            break;
        }
        Send(
            [&]()
            {
                PC->UIRequestAbandonConsent(ResultsRetryId, PC->GetAbandonVoteRevision(), true);
            });
        break;
    case 31:
        if (ResultsCheck(ServerDocument, TEXT("server_results_consented_own_return_preserved_grants")) &&
            Hero->IsAlive() && !P->ActiveExpeditionId.IsValid() && View &&
            View.IsInRegion(EFableRegion::Village, Hero->GetActorLocation()) && ResultsSaved(P))
        {
            Send(
                [&]()
                {
                    PC->UIOpenExpeditionResult(ResultsRetryId);
                });
            const auto* Displayed = PC->GetDisplayedExpeditionResult();
            if (ActionOk() && Displayed && Displayed->IsTerminal() &&
                Displayed->Outcome == EFableExpeditionOutcome::Abandoned &&
                Displayed->Ticket.ExpeditionId == ResultsRetryId && ResultsRetryGrant(ResultsReceipt(*Displayed), A))
            {
                ResultsReport->SetObjectField(TEXT("abandonedResult"), ResultsReceipt(*Displayed));
                Mark(TEXT("results_consented_return_town"));
                Capture(TEXT("results-own-party-abandoned"));
                Advance(32);
            }
        }
        break;
    case 32:
        Send(
            [&]()
            {
                PC->UIAcknowledgeExpeditionResult();
            });
        if (ActionOk() && PC->GetDisplayedExpeditionResult() && PC->GetDisplayedExpeditionResult()->bAcknowledged &&
            ResultsSaved(P) && ResultsReward(Profile(P), A))
        {
            ResultsReport->SetObjectField(TEXT("finalProfile"), Profile(P));
            ResultsReport->SetObjectField(TEXT("acknowledgedAbandonedResult"),
                                          ResultsReceipt(*PC->GetDisplayedExpeditionResult()));
            Mark(TEXT("results_final_saved"));
            Advance(33);
        }
        break;
    case 33:
        if (ResultsCheck(PeerDocument, TEXT("results_final_saved")))
        {
            Mark(TEXT("results_final_peer_server_handshake"));
            Advance(34);
        }
        break;
    case 34:
        if (ResultsCheck(PeerDocument, TEXT("results_final_peer_server_handshake")))
        {
            ResultsReport->SetStringField(TEXT("status"), TEXT("passed"));
            return true;
        }
        break;
    default:
        Finish(false, TEXT("Unknown party result validation step."));
        break;
    }
    return false;
}
