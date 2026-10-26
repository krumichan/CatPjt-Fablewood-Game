#include "FableOnlineValidationDriver.h"
#include "FableCharacter.h"
#include "FableGameMode.h"
#include "FablePartyEncounter.h"
#include "FablePartyManager.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableTownManager.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** 공개 검증 보고서만 읽고 서버 상태나 프로필을 변경하지 않는다. / 公開観測だけを読み状態を変更しない。 */
    TSharedPtr<FJsonObject> ReadCancellationData(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> Root;
        const TSharedPtr<FJsonObject>* Data = nullptr;
        return FFileHelper::LoadFileToString(Text, *Path) &&
                       FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root &&
                       Root->TryGetObjectField(TEXT("partyReadyCancellation"), Data)
                   ? *Data
                   : nullptr;
    }

    /** 좌표는 관측값이며 이동 요청 인자로 사용하지 않는다. / 座標は観測だけに用いる。 */
    TArray<TSharedPtr<FJsonValue>> CancelVector(const FVector& Position)
    {
        return {MakeShared<FJsonValueNumber>(Position.X), MakeShared<FJsonValueNumber>(Position.Y),
                MakeShared<FJsonValueNumber>(Position.Z)};
    }

    /** 실제 서버에 아직 원정이 없고 두 현재 소유 연결만 있는지 확인한다. */
    bool CancelReadyShape(const TSharedPtr<FJsonObject>& Snapshot, int32 Revision, bool bPeerCancelled)
    {
        if (!Snapshot || !Snapshot->GetBoolField(TEXT("validParty")) ||
            Snapshot->GetIntegerField(TEXT("preparationRevision")) != Revision ||
            Snapshot->GetArrayField(TEXT("committedExpeditionIds")).Num() != 0 ||
            Snapshot->GetArrayField(TEXT("members")).Num() != 2)
        {
            return false;
        }

        TSet<FString> Keys;
        int32 Leaders = 0;
        for (const auto& Member : Snapshot->GetArrayField(TEXT("members")))
        {
            const auto Value = Member->AsObject();
            const bool bLeader = Value->GetStringField(TEXT("partyRole")) == TEXT("leader");
            Leaders += bLeader ? 1 : 0;
            Keys.Add(Value->GetStringField(TEXT("playerKey")));
            if (!Value->GetBoolField(TEXT("connected")) || !Value->GetBoolField(TEXT("liveConnection")) ||
                !Value->GetBoolField(TEXT("currentOwner")) || !Value->GetBoolField(TEXT("currentConnection")) ||
                !Value->GetBoolField(TEXT("connectionNonceValid")) || !Value->GetBoolField(TEXT("alive")) ||
                Value->GetStringField(TEXT("activeExpeditionId")) !=
                    FGuid().ToString(EGuidFormats::DigitsWithHyphensLower) ||
                Value->GetIntegerField(TEXT("readyRevision")) != (bPeerCancelled && !bLeader ? 0 : Revision))
            {
                return false;
            }
        }
        return Leaders == 1 && Keys.Num() == 2;
    }

    /** 취소·거절은 위치와 원정·연결·선택을 바꾸지 않아야 한다. / 取消・拒否で位置や接続・選択を変更しない。 */
    bool SameCancelState(const TSharedPtr<FJsonObject>& Before, const TSharedPtr<FJsonObject>& After)
    {
        if (!Before || !After)
        {
            return false;
        }
        for (const TCHAR* Field : {TEXT("partyId"), TEXT("leaderKey")})
        {
            if (Before->GetStringField(Field) != After->GetStringField(Field))
            {
                return false;
            }
        }
        for (const TCHAR* Field :
             {TEXT("revision"), TEXT("preparationRevision"), TEXT("selectedRegion"), TEXT("selectedDifficulty")})
        {
            if (Before->GetIntegerField(Field) != After->GetIntegerField(Field))
            {
                return false;
            }
        }
        for (const auto& Member : Before->GetArrayField(TEXT("members")))
        {
            const auto A = Member->AsObject();
            TSharedPtr<FJsonObject> B;
            for (const auto& Candidate : After->GetArrayField(TEXT("members")))
            {
                if (Candidate->AsObject()->GetStringField(TEXT("playerKey")) == A->GetStringField(TEXT("playerKey")))
                {
                    B = Candidate->AsObject();
                }
            }
            if (!B || A->GetStringField(TEXT("activeExpeditionId")) != B->GetStringField(TEXT("activeExpeditionId")) ||
                A->GetNumberField(TEXT("ownerConnectionGeneration")) !=
                    B->GetNumberField(TEXT("ownerConnectionGeneration")))
            {
                return false;
            }
            const auto& First = A->GetArrayField(TEXT("worldPosition"));
            const auto& Second = B->GetArrayField(TEXT("worldPosition"));
            double DistanceSquared = 0;
            for (int32 Axis = 0; Axis < 3; ++Axis)
            {
                DistanceSquared += FMath::Square(First[Axis]->AsNumber() - Second[Axis]->AsNumber());
            }
            if (DistanceSquared > 4.0)
            {
                return false;
            }
        }
        return true;
    }

    /** 서버 관측의 실제 요청 ID로 소유자에게 도착한 정상 응답을 연결한다. */
    bool HasCancellationReply(const TArray<TSharedPtr<FJsonValue>>& Replies, const TSharedPtr<FJsonObject>& Event,
                              bool bSuccess, int32 Revision)
    {
        if (!Event)
        {
            return false;
        }
        for (const auto& Reply : Replies)
        {
            const auto Row = Reply->AsObject();
            if (Row->GetStringField(TEXT("requestId")) == Event->GetStringField(TEXT("requestId")) &&
                Row->GetBoolField(TEXT("matchedCurrentRequest")) && Row->GetBoolField(TEXT("succeeded")) == bSuccess &&
                Row->GetIntegerField(TEXT("preparationRevision")) == Revision &&
                Row->GetStringField(TEXT("notice")) == Event->GetStringField(TEXT("notice")))
            {
                return true;
            }
        }
        return false;
    }
}

/** 하나의 권한 시점에 공간·현재 연결 세대·실제 원정 목록을 결합한다. nonce 자체는 공개하지 않는다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::PartyCancellationSnapshot(AFablePlayerController* Player) const
{
    UWorld* World = Player ? Player->GetWorld() : nullptr;
    const auto* PlayerProfile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    const auto* Manager = AFablePartyManager::Find(World);
    const auto* Party = Manager && PlayerProfile ? Manager->GetPartyForMember(PlayerProfile->PartyPlayerKey) : nullptr;
    auto Row = Party ? PartySpatialObservation(World, *Party) : MakeShared<FJsonObject>();
    Row->SetBoolField(TEXT("validParty"), Party != nullptr);
    Row->SetStringField(TEXT("leaderKey"), Party ? Party->LeaderKey : FString());
    TArray<TSharedPtr<FJsonValue>> Expeditions;
    if (const auto* ModeRules = AFableGameMode::Find(World))
    {
        for (const auto* Encounter : ModeRules->GetExpeditions())
        {
            Expeditions.Add(MakeShared<FJsonValueString>(
                Encounter->GetExpeditionId().ToString(EGuidFormats::DigitsWithHyphensLower)));
        }
    }
    Row->SetArrayField(TEXT("committedExpeditionIds"), Expeditions);
    if (Party)
    {
        const auto* Towns = AFableTownManager::Find(World);
        for (const auto& Member : Row->GetArrayField(TEXT("members")))
        {
            const auto Value = Member->AsObject();
            auto* Connection = Manager->GetConnectedPlayer(Value->GetStringField(TEXT("playerKey")));
            const auto* Other = Connection ? Connection->GetPlayerState<AFablePlayerState>() : nullptr;
            const auto* Hero = Connection ? Connection->GetPawn<AFableCharacter>() : nullptr;
            FFableTownConnectionSnapshot Snapshot;
            const bool bCurrent =
                Towns && Towns->GetConnectionSnapshot(Other, Snapshot) &&
                Towns->IsCurrentConnection(Connection, Snapshot.OwnerConnectionGeneration, Snapshot.ConnectionNonce);
            Value->SetStringField(TEXT("partyRole"), Value->GetStringField(TEXT("playerKey")) == Party->LeaderKey
                                                         ? TEXT("leader")
                                                         : TEXT("peer"));
            Value->SetArrayField(TEXT("worldPosition"),
                                 CancelVector(Hero ? Hero->GetActorLocation() : FVector::ZeroVector));
            Value->SetArrayField(TEXT("worldRotation"),
                                 CancelVector(Hero ? Hero->GetActorRotation().Euler() : FVector::ZeroVector));
            Value->SetArrayField(TEXT("velocity"), CancelVector(Hero ? Hero->GetVelocity() : FVector::ZeroVector));
            Value->SetBoolField(TEXT("currentConnection"), bCurrent);
            Value->SetBoolField(TEXT("connectionNonceValid"), bCurrent && Snapshot.ConnectionNonce.IsValid());
            Value->SetNumberField(TEXT("ownerConnectionGeneration"), double(Snapshot.OwnerConnectionGeneration));
        }
    }
    return Row;
}

/** 正常RPCの直前を観察するだけで要求や権限を生成しない。 / 정상 RPC 직전 읽기 전용 관측. */
int32 UFableOnlineValidationDriver::BeginPartyCancellationObservation(AFablePlayerController* Player, FGuid RequestId,
                                                                      const TCHAR* Action, FGuid PartyId,
                                                                      int32 PreparationRevision, bool bReady)
{
    if (!bPartyReadyCancellation || Role != TEXT("server") || Scenario != TEXT("party-results") || !Player ||
        Player->GetWorld()->GetNetMode() != NM_DedicatedServer || PartyCancelAuthority.Num() >= 64)
    {
        return INDEX_NONE;
    }
    auto Row = MakeShared<FJsonObject>();
    const auto* PlayerProfile = Player->GetPlayerState<AFablePlayerState>();
    const auto Before = PartyCancellationSnapshot(Player);
    Row->SetNumberField(TEXT("sequence"), PartyCancelAuthority.Num() + 1);
    Row->SetStringField(TEXT("action"), Action);
    Row->SetStringField(TEXT("requestId"), RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Row->SetStringField(TEXT("requestedPartyId"), PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Row->SetNumberField(TEXT("requestedPreparationRevision"), PreparationRevision);
    Row->SetBoolField(TEXT("requestedReady"), bReady);
    Row->SetStringField(TEXT("callerKey"), PlayerProfile ? PlayerProfile->PartyPlayerKey : FString());
    Row->SetNumberField(TEXT("callerNoticeSerialBefore"), PlayerProfile ? PlayerProfile->NoticeSerial : INDEX_NONE);
    Row->SetStringField(TEXT("callerRole"),
                        PlayerProfile && Before->GetStringField(TEXT("leaderKey")) == PlayerProfile->PartyPlayerKey
                            ? TEXT("leader")
                            : TEXT("peer"));
    Row->SetStringField(TEXT("serverRole"), Role);
    Row->SetNumberField(TEXT("serverProcessId"), FPlatformProcess::GetCurrentProcessId());
    Row->SetNumberField(TEXT("startedServerSeconds"), Player->GetWorld()->GetTimeSeconds());
    Row->SetObjectField(TEXT("before"), Before);
    PartyCancelCallers.Add(Player);
    return PartyCancelAuthority.Add(MakeShared<FJsonValueObject>(Row));
}

/** 製品の判定後に実結果を保存する。成功値やプロフィールを変更しない。 */
void UFableOnlineValidationDriver::EndPartyCancellationObservation(int32 Index, bool bSuccess)
{
    if (!PartyCancelAuthority.IsValidIndex(Index) || !PartyCancelCallers.IsValidIndex(Index))
    {
        return;
    }
    auto* Player = PartyCancelCallers[Index].Get();
    const auto* PlayerProfile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    const auto Row = PartyCancelAuthority[Index]->AsObject();
    Row->SetBoolField(TEXT("success"), bSuccess);
    const FString Notice =
        PlayerProfile && PlayerProfile->NoticeSerial != Row->GetIntegerField(TEXT("callerNoticeSerialBefore"))
            ? PlayerProfile->LastNotice
        : bSuccess ? TEXT("서버에서 원정 요청을 확인했습니다.")
                   : TEXT("서버에서 원정 요청을 처리하지 못했습니다. 연결과 프로필 상태를 확인하세요.");
    Row->SetStringField(TEXT("notice"), Notice);
    Row->SetNumberField(TEXT("callerNoticeSerialAfter"), PlayerProfile ? PlayerProfile->NoticeSerial : INDEX_NONE);
    Row->SetNumberField(TEXT("finishedServerSeconds"), Player ? Player->GetWorld()->GetTimeSeconds() : 0);
    Row->SetObjectField(TEXT("after"), PartyCancellationSnapshot(Player));
    Publish(TEXT("running"));
}

/** 遅れた別要求は製品側で既に除外済み。一致した本来の応答IDをそのまま記録する。 */
void UFableOnlineValidationDriver::RecordPartyCancellationReply(AFablePlayerController* Player, FGuid RequestId,
                                                                bool bSuccess, int32 PreparationRevision,
                                                                const FString& Notice)
{
    if (!bPartyReadyCancellation || Role == TEXT("server") || Scenario != TEXT("party-results") || !Player ||
        PartyCancelReplies.Num() >= 64)
    {
        return;
    }
    auto Row = MakeShared<FJsonObject>();
    const auto* PlayerProfile = Player->GetPlayerState<AFablePlayerState>();
    Row->SetStringField(TEXT("requestId"), RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Row->SetBoolField(TEXT("succeeded"), bSuccess);
    Row->SetNumberField(TEXT("preparationRevision"), PreparationRevision);
    Row->SetStringField(TEXT("notice"), Notice);
    Row->SetStringField(TEXT("role"), Role);
    Row->SetStringField(TEXT("playerKey"), PlayerProfile ? PlayerProfile->PartyPlayerKey : FString());
    Row->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
    Row->SetNumberField(TEXT("observedSeconds"), Player->GetWorld()->GetTimeSeconds());
    Row->SetBoolField(TEXT("matchedCurrentRequest"), true);
    PartyCancelReplies.Add(MakeShared<FJsonValueObject>(Row));
}

/** 全員準備→本人取消→実拒否→新しい同意を通常RPCで順番に実行する。 / 전원 준비 뒤 명시 취소·거절·재준비만 추가한다. */
bool UFableOnlineValidationDriver::TickPartyReadyCancellation(AFablePlayerController* PC,
                                                              const AFablePlayerState* PlayerProfile,
                                                              const FFablePartyRecord& Party, double Now)
{
    if (!bPartyReadyCancellation || bResumeExpedition)
    {
        return true;
    }
    const bool bLeader = Role == TEXT("client1");
    const auto Server = ReadCancellationData(FPaths::Combine(Directory, TEXT("server.json")));
    const auto Peer =
        ReadCancellationData(FPaths::Combine(Directory, bLeader ? TEXT("client2.json") : TEXT("client1.json")));
    if (!PartyCancelReport)
    {
        PartyCancelReport = MakeShared<FJsonObject>();
        PartyCancelReport->SetStringField(TEXT("partyId"),
                                          Party.PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
        PartyCancelReport->SetNumberField(TEXT("preparationRevision"), Party.PreparationRevision);
        PartyCancelReport->SetStringField(TEXT("selfKey"), PlayerProfile->PartyPlayerKey);
    }
    const int32 Revision = PartyCancelReport->GetIntegerField(TEXT("preparationRevision"));
    const auto Advance = [&](int32 Next)
    {
        PartyCancelStep = Next;
        PartyCancelStepAt = Now;
        bPartyCancelSent = false;
        PartyCancelReport->SetNumberField(TEXT("step"), Next);
        Publish(TEXT("running"));
    };
    if (PartyCancelStep == 0)
    {
        Advance(1);
        return false;
    }
    if (Now - PartyCancelStepAt > 50 || Party.PreparationRevision != Revision ||
        PlayerProfile->ActiveExpeditionId.IsValid())
    {
        Finish(false, TEXT("Party readiness cancellation state or response timed out."));
        return false;
    }
    if (!Server || !Peer)
    {
        return false;
    }
    FString PeerPartyId, PeerKey;
    double PeerRevision = 0;
    if (!Peer->TryGetStringField(TEXT("partyId"), PeerPartyId) || !Peer->TryGetStringField(TEXT("selfKey"), PeerKey) ||
        !Peer->TryGetNumberField(TEXT("preparationRevision"), PeerRevision) ||
        PeerPartyId != PartyCancelReport->GetStringField(TEXT("partyId")) || int32(PeerRevision) != Revision ||
        PeerKey == PlayerProfile->PartyPlayerKey ||
        !Party.Members.ContainsByPredicate(
            [&](const FFablePartyMember& Member)
            {
                return Member.PlayerKey == PeerKey;
            }))
    {
        return false;
    }
    double PeerStepNumber = 0;
    Peer->TryGetNumberField(TEXT("step"), PeerStepNumber);
    const int32 PeerStep = int32(PeerStepNumber);
    const auto Send = [&](TFunctionRef<void()> Request)
    {
        if (!bPartyCancelSent && !PC->IsPartyPreparationPending() && Now - PartyCancelLastSentAt >= .7)
        {
            Request();
            bPartyCancelSent = true;
            PartyCancelLastSentAt = Now;
        }
    };
    const auto Find = [&](const TCHAR* Action, bool bReady, bool bSuccess, int32 AfterSequence, bool bBeforeCancelled,
                          bool bAfterCancelled)
    {
        TSharedPtr<FJsonObject> Found;
        for (const auto& Value : Server->GetArrayField(TEXT("authorityEvents")))
        {
            const auto Event = Value->AsObject();
            const TSharedPtr<FJsonObject>* After = nullptr;
            if (!Event->TryGetObjectField(TEXT("after"), After))
            {
                continue;
            }
            const auto Before = Event->GetObjectField(TEXT("before"));
            if (Event->GetStringField(TEXT("action")) == Action &&
                Event->GetStringField(TEXT("callerRole")) ==
                    (FString(Action) == TEXT("entry") ? TEXT("leader") : TEXT("peer")) &&
                Event->GetBoolField(TEXT("requestedReady")) == bReady &&
                Event->GetBoolField(TEXT("success")) == bSuccess &&
                Event->GetIntegerField(TEXT("sequence")) > AfterSequence &&
                Event->GetStringField(TEXT("requestedPartyId")) == PartyCancelReport->GetStringField(TEXT("partyId")) &&
                CancelReadyShape(Before, Revision, bBeforeCancelled) &&
                CancelReadyShape(*After, Revision, bAfterCancelled) && SameCancelState(Before, *After) &&
                SameCancelState(PartyCancelReport->GetObjectField(TEXT("initialAuthoritySnapshot")), Before) &&
                SameCancelState(PartyCancelReport->GetObjectField(TEXT("initialAuthoritySnapshot")), *After))
            {
                Found = Event;
            }
        }
        return Found;
    };
    const auto Sequence = [&](const TCHAR* Field)
    {
        double Value = 0;
        PartyCancelReport->TryGetNumberField(Field, Value);
        return int32(Value);
    };
    const auto LocalReadyShape = [&](bool bPeerCancelled)
    {
        // UI가 실제로 읽는 복제 명부와 본인 getter를 검사한다. 서버 파일로 UI 관측을 대신하지 않는다. / UIの実複製名簿を検査する。
        if (Party.PartyId != PlayerProfile->PartyId || Party.Members.Num() != 2 ||
            Party.PreparationRevision != Revision || !PC->IsPartyOpen() || !PC->IsPartyExpeditionOpen() ||
            PC->GetWorld()->GetNetMode() != NM_Client || !PC->GetPawn() ||
            PlayerProfile->ActiveExpeditionId.IsValid() ||
            (Party.LeaderKey == PlayerProfile->PartyPlayerKey) != bLeader ||
            PC->IsPartyReady() != (!bPeerCancelled || bLeader))
        {
            return false;
        }
        TSet<FString> Keys;
        for (const auto& Member : Party.Members)
        {
            const bool bMemberLeader = Member.PlayerKey == Party.LeaderKey;
            if (!Member.bConnected || Member.PlayerKey.IsEmpty() ||
                Member.ReadyRevision != (bPeerCancelled && !bMemberLeader ? 0 : Revision))
            {
                return false;
            }
            Keys.Add(Member.PlayerKey);
        }
        return Keys.Num() == 2 && Keys.Contains(PlayerProfile->PartyPlayerKey) && Keys.Contains(PeerKey);
    };
    const auto CaptureClientState = [&](const TCHAR* Label, bool bPeerCancelled, const TSharedPtr<FJsonObject>& Event)
    {
        if (!Event || !LocalReadyShape(bPeerCancelled))
        {
            return false;
        }
        TArray<TSharedPtr<FJsonValue>> Snapshots;
        const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
        if (PartyCancelReport->TryGetArrayField(TEXT("clientSnapshots"), Existing))
        {
            Snapshots = *Existing;
        }
        for (const auto& Previous : Snapshots)
        {
            if (Previous->AsObject()->GetStringField(TEXT("label")) == Label)
            {
                return true;
            }
        }

        // 요청 ID는 관측 연결에만 쓰고 새로운 RPC 인자나 권한을 만들지 않는다. / 要求IDは観測の関連付けだけに使う。
        auto Row = MakeShared<FJsonObject>();
        Row->SetNumberField(TEXT("sequence"), Snapshots.Num() + 1);
        Row->SetStringField(TEXT("label"), Label);
        Row->SetStringField(TEXT("role"), Role);
        Row->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
        Row->SetBoolField(TEXT("clientMode"), PC->GetWorld()->GetNetMode() == NM_Client);
        Row->SetStringField(TEXT("networkMode"), PC->HasAuthority() ? TEXT("authority") : TEXT("client"));
        Row->SetNumberField(TEXT("observedSeconds"), PC->GetWorld()->GetTimeSeconds());
        Row->SetStringField(TEXT("selfKey"), PlayerProfile->PartyPlayerKey);
        Row->SetStringField(TEXT("selfActiveExpeditionId"),
                            PlayerProfile->ActiveExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
        Row->SetArrayField(TEXT("worldPosition"), CancelVector(PC->GetPawn()->GetActorLocation()));
        Row->SetStringField(TEXT("partyId"), Party.PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
        Row->SetStringField(TEXT("leaderKey"), Party.LeaderKey);
        Row->SetNumberField(TEXT("revision"), Party.Revision);
        Row->SetNumberField(TEXT("preparationRevision"), Party.PreparationRevision);
        Row->SetNumberField(TEXT("selectedRegion"), int32(Party.SelectedRegion));
        Row->SetNumberField(TEXT("selectedDifficulty"), int32(Party.SelectedDifficulty));
        Row->SetBoolField(TEXT("partyOpen"), PC->IsPartyOpen());
        Row->SetBoolField(TEXT("partyExpeditionOpen"), PC->IsPartyExpeditionOpen());
        Row->SetBoolField(TEXT("ownIsPartyReady"), PC->IsPartyReady());
        Row->SetStringField(TEXT("linkedAuthorityRequestId"), Event->GetStringField(TEXT("requestId")));
        Row->SetNumberField(TEXT("authoritySequence"), Event->GetIntegerField(TEXT("sequence")));
        TArray<TSharedPtr<FJsonValue>> Members;
        for (const auto& Member : Party.Members)
        {
            auto Value = MakeShared<FJsonObject>();
            Value->SetStringField(TEXT("playerKey"), Member.PlayerKey);
            Value->SetNumberField(TEXT("playerId"), Member.PlayerId);
            Value->SetStringField(TEXT("partyRole"),
                                  Member.PlayerKey == Party.LeaderKey ? TEXT("leader") : TEXT("peer"));
            Value->SetNumberField(TEXT("readyRevision"), Member.ReadyRevision);
            Value->SetBoolField(TEXT("connected"), Member.bConnected);
            Members.Add(MakeShared<FJsonValueObject>(Value));
        }
        Row->SetArrayField(TEXT("members"), Members);
        Snapshots.Add(MakeShared<FJsonValueObject>(Row));
        PartyCancelReport->SetArrayField(TEXT("clientSnapshots"), Snapshots);
        return true;
    };

    if (PartyCancelStep == 1)
    {
        // 最後の本人ready成功応答の後には全員準備状態が存在する。 / 취소 전 권한 서버 전원 준비를 먼저 기다린다.
        TSharedPtr<FJsonObject> Initial;
        for (const auto& Value : Server->GetArrayField(TEXT("authorityEvents")))
        {
            const auto Event = Value->AsObject();
            const TSharedPtr<FJsonObject>* After = nullptr;
            if (Event->TryGetObjectField(TEXT("after"), After) &&
                Event->GetStringField(TEXT("action")) == TEXT("ready") && Event->GetBoolField(TEXT("requestedReady")) &&
                Event->GetBoolField(TEXT("success")) &&
                Event->GetStringField(TEXT("requestedPartyId")) == PartyCancelReport->GetStringField(TEXT("partyId")) &&
                CancelReadyShape(*After, Revision, false))
            {
                Initial = Event;
            }
        }
        if (!Initial || PeerStep < 1 || !CaptureClientState(TEXT("initial-both-ready"), false, Initial))
        {
            return false;
        }
        PartyCancelReport->SetNumberField(TEXT("initialAuthoritySequence"), Initial->GetIntegerField(TEXT("sequence")));
        PartyCancelReport->SetObjectField(TEXT("initialAuthoritySnapshot"), Initial->GetObjectField(TEXT("after")));
        Advance(2);
    }
    else if (PartyCancelStep == 2)
    {
        // 両者の初回UI観測が保存されるまで取消しない。 / 양쪽 초기 UI 관측이 보존된 뒤에만 취소한다.
        if (!bLeader && PeerStep >= 2)
        {
            Send(
                [&]()
                {
                    PC->UISetPartyReady(false);
                });
        }
        const auto Event = Find(TEXT("ready"), false, true, Sequence(TEXT("initialAuthoritySequence")), false, true);
        if (Event &&
            ((!bLeader && HasCancellationReply(PartyCancelReplies, Event, true, Revision)) ||
             (bLeader && PeerStep >= 3)) &&
            CaptureClientState(TEXT("cancelled"), true, Event))
        {
            PartyCancelReport->SetStringField(TEXT("cancelRequestId"), Event->GetStringField(TEXT("requestId")));
            PartyCancelReport->SetNumberField(TEXT("cancelAuthoritySequence"),
                                              Event->GetIntegerField(TEXT("sequence")));
            Advance(3);
        }
    }
    else if (PartyCancelStep == 3)
    {
        if (bLeader)
        {
            Send(
                [&]()
                {
                    PC->UIEnterPreparedParty();
                });
        }
        const auto Event = Find(TEXT("entry"), false, false, Sequence(TEXT("cancelAuthoritySequence")), true, true);
        if (Event && Event->GetStringField(TEXT("notice")) == TEXT("모든 파티원이 현재 선택에 준비해야 합니다.") &&
            ((bLeader && HasCancellationReply(PartyCancelReplies, Event, false, Revision)) ||
             (!bLeader && PeerStep >= 4)) &&
            CaptureClientState(TEXT("rejected-entry"), true, Event))
        {
            PartyCancelReport->SetStringField(TEXT("rejectedEntryRequestId"), Event->GetStringField(TEXT("requestId")));
            PartyCancelReport->SetNumberField(TEXT("rejectedAuthoritySequence"),
                                              Event->GetIntegerField(TEXT("sequence")));
            Advance(4);
        }
    }
    else if (PartyCancelStep == 4)
    {
        // パーティ長の取消・拒否UI観測を飛ばして再準備しない。 / 파티장 취소·거절 UI 관측을 기다린다.
        if (!bLeader && PeerStep >= 4)
        {
            Send(
                [&]()
                {
                    PC->UISetPartyReady(true);
                });
        }
        const auto Event = Find(TEXT("ready"), true, true, Sequence(TEXT("rejectedAuthoritySequence")), true, false);
        if (Event &&
            ((!bLeader && HasCancellationReply(PartyCancelReplies, Event, true, Revision)) ||
             (bLeader && PeerStep >= 5)) &&
            CaptureClientState(TEXT("reprepared"), false, Event))
        {
            PartyCancelReport->SetStringField(TEXT("freshReadyRequestId"), Event->GetStringField(TEXT("requestId")));
            PartyCancelReport->SetNumberField(TEXT("freshReadyAuthoritySequence"),
                                              Event->GetIntegerField(TEXT("sequence")));
            PartyCancelReport->SetStringField(TEXT("status"), TEXT("reprepared"));
            Advance(5);
        }
    }
    return PartyCancelStep == 5;
}

/** 기존 case13의 실제 두 명 도착을 사용하며 발급되지 않은 ticket 내부를 관측했다고 주장하지 않는다. */
void UFableOnlineValidationDriver::RecordPartyCancellationArrival(AFablePlayerController* Player,
                                                                  const AFablePlayerState* PlayerProfile)
{
    if (!bPartyReadyCancellation || bResumeExpedition || !PartyCancelReport || PartyCancelStep != 5 || !PlayerProfile ||
        !PlayerProfile->ActiveExpeditionId.IsValid())
    {
        return;
    }
    PartyCancelReport->SetStringField(TEXT("admittedExpeditionId"),
                                      PlayerProfile->ActiveExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    PartyCancelReport->SetNumberField(TEXT("arrivalObservedSeconds"), Player->GetWorld()->GetTimeSeconds());
    PartyCancelReport->SetBoolField(TEXT("literalRejectedTicketObserved"), false);
    PartyCancelReport->SetBoolField(TEXT("twoMemberArrivalObserved"), true);
    PartyCancelReport->SetStringField(TEXT("status"), TEXT("passed"));
    Publish(TEXT("running"));
}
