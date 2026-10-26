#include "FablePlayerState.h"
#include "UObject/Package.h"
#include "FableBackend.h"
#include "FableSaveGame.h"
#include "FableProfileSchema.h"
#include "FableTownManager.h"
#include "FablePlayerController.h"
#include "FableInventorySnapshot.h"
#include "FableCharacterSummary.h"
#include "FableGameMode.h"
#include "FableCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"
THIRD_PARTY_INCLUDES_START
#include <openssl/sha.h>
THIRD_PARTY_INCLUDES_END
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/PlayerController.h"

namespace
{
    /** 同じworldのseed完了後は、新しいclaimの実際のbytesとrevisionを再検査する。 */
    struct FSummarySeedReceipt
    {
        FString Hash;
        int64 Revision = 0;
        double ExpiresAt = 0;
    };

    TMap<TWeakObjectPtr<UWorld>, TMap<FString, FSummarySeedReceipt>> SummarySeedReceipts;

    /** 終了worldと期限切れreceiptだけを除外し、無制限に保持しない。 */
    TMap<FString, FSummarySeedReceipt>& SeedReceipts(UWorld* World)
    {
        for (auto It = SummarySeedReceipts.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid())
            {
                It.RemoveCurrent();
            }
        }
        auto& Receipts = SummarySeedReceipts.FindOrAdd(World);
        const double Now = FPlatformTime::Seconds();
        for (auto It = Receipts.CreateIterator(); It; ++It)
        {
            if (Now >= It.Value().ExpiresAt)
            {
                It.RemoveCurrent();
            }
        }
        return Receipts;
    }

    /** 프로필의 재화와 물약을 전송용 JSON으로 만든다. */
    TSharedRef<FJsonObject> Wallet(const UFableSaveGame* D)
    {
        // 재화와 작업 비용에 필요한 값 준비
        auto W = MakeShared<FJsonObject>();
        W->SetNumberField(TEXT("gold"), D->Gold);
        W->SetNumberField(TEXT("ore"), D->Ore);
        W->SetNumberField(TEXT("essence"), D->Essence);
        W->SetNumberField(TEXT("rootCores"), D->RootCores);
        W->SetNumberField(TEXT("moonCrystals"), D->MoonCrystals);
        W->SetNumberField(TEXT("healthPotions"), D->HealthPotions);
        W->SetNumberField(TEXT("manaPotions"), D->ManaPotions);

        return W;
    }

    /** 프로필 바이트의 SHA-256 해시를 정규 문자열로 계산한다. */
    FString HashBytes(const TArray<uint8>& Bytes)
    {
        uint8 Digest[SHA256_DIGEST_LENGTH];
        if (!SHA256(Bytes.GetData(), static_cast<size_t>(Bytes.Num()), Digest))
        {
            return TEXT("");
        }
        return BytesToHex(Digest, SHA256_DIGEST_LENGTH).ToLower();
    }
}
/** Owns queued snapshots independently of the actor, so a disconnect can drain its final save. */
/** 플레이어별 접속 권한·저장 대기열·응답 확인 상태. */
struct FFableBackendSession : TSharedFromThis<FFableBackendSession>
{
    TWeakObjectPtr<AFablePlayerState> Player;
    TWeakObjectPtr<UWorld> World;
    FString PlayerId, LeaseId;
    FString ExpeditionPublicKey, SentExpeditionPublicKey;
    int64 LatestGrantSequence = 0, SentGrantSequence = 0;
    int64 Revision = 0;
    FString LatestPayload, LatestHash, SentHash, SentPayload, OperationId, AcknowledgedHash;
    TSharedPtr<FJsonObject> LatestWallet, SentWallet, LatestInventory, SentInventory, LatestSummary, SentSummary,
        SentBody;
    FFableTownAnchorSaveWitness LatestAnchorWitness, SentAnchorWitness;
    bool V9SummarySeed = false;
    bool InventoryAcknowledged = false, SummaryAcknowledged = false;
    bool Pending = false, Closing = false, Failed = false, LoginComplete = false;
    bool HeartbeatPending = false, ReleaseStarted = false, ReleasePending = false, Released = false;
    FTimerHandle Retry, HeartbeatTimer, LeaveTimeout;
    double LeaseDeadline = 0;
    FString FailureReason;
    TFunction<void(bool, const FString&)> LeaveCompletion;

    /** 同じPSが再利用されても、置換前sessionのcallbackは新しい接続を変更できない。 */
    AFablePlayerState* CurrentPlayer() const
    {
        auto* P = Player.Get();
        auto* PC = P ? Cast<APlayerController>(P->GetOwner()) : nullptr;
        return P && P->BackendSession.Get() == this && P->GetWorld() == World.Get() && P->BackendPlayerId == PlayerId &&
                       PC && PC->GetPlayerState<AFablePlayerState>() == P
                   ? P
                   : nullptr;
    }

    /** 식별자를 저장 계약의 정규 문자열 형식으로 변환한다. */
    TSharedRef<FJsonObject> Identity() const
    {
        auto J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("playerId"), PlayerId);
        J->SetStringField(TEXT("leaseId"), LeaseId);
        return J;
    }

    /** 백엔드가 확인한 저장 리비전과 준비 상태를 플레이어에 반영한다. */
    void Mirror()
    {
        if (auto P = CurrentPlayer())
        {
            P->BackendRevision = Revision + (Pending ? 1 : 0);
            P->BackendAcknowledgedRevision = Revision;
            P->bBackendSavePending = Pending;
            P->bLastSaveSucceeded = InventoryAcknowledged && SummaryAcknowledged && !Pending && !Failed;
            P->ForceNetUpdate();
        }
    }

    /** 저장 및 권한 반환 결과를 소유 클라이언트의 이탈 요청에 전달한다. */
    void CompleteLeave(bool bSuccess, const FString& Message)
    {
        // 체력·마나·보호막·피해량 처리
        if (World.IsValid())
        {
            World->GetTimerManager().ClearTimer(LeaveTimeout);
        }

        auto Completion = MoveTemp(LeaveCompletion);
        LeaveCompletion = nullptr;

        // 이 단계에서 저장 및 권한 반환 결과를 소유 클라이언트의 이탈 요청에 전달한다.
        if (Completion)
        {
            Completion(bSuccess, Message);
        }
    }

    /** 미완료 저장과 접속 권한 반환이 끝날 때까지 이탈을 보류한다. */
    void AwaitLeave(TFunction<void(bool, const FString&)> Completion)
    {
        // 접속 권한·저장 리비전·멱등 키 조건 검사
        if (Released)
        {
            Completion(true, TEXT("서버 저장과 안전 퇴장이 완료되었습니다."));
            return;
        }
        if (Failed)
        {
            Completion(false, FailureReason);
            return;
        }

        LeaveCompletion = MoveTemp(Completion);
        auto Self = AsShared();

        // 현재 월드와 플레이어 참조 조건 검사
        if (!World.IsValid())
        {
            CompleteLeave(false, TEXT("서버 연결 상태를 확인할 수 없습니다."));
            return;
        }

        // 체력·마나·보호막·피해량 처리
        World->GetTimerManager().SetTimer(
            LeaveTimeout,
            [Self]()
            {
                // A timeout is not an acknowledgement. Drain/retry keeps the frozen body;
                // a later user request can wait again without resuming gameplay or creating a new save.
                Self->CompleteLeave(
                    false, TEXT("저장 또는 퇴장 확인이 지연되고 있습니다. 접속을 유지하고 다시 확인해 주세요."));
            },
            30.0f, false);
    }

    /** 백엔드 처리 실패를 기록하고 후속 저장·이탈 상태를 갱신한다. */
    void Failure(const FString& Message)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Failed)
        {
            return;
        }

        Failed = true;
        Pending = false;
        FailureReason = Message;

        // 체력·마나·보호막·피해량 처리
        if (World.IsValid())
        {
            World->GetTimerManager().ClearTimer(HeartbeatTimer);
            World->GetTimerManager().ClearTimer(Retry);
        }

        // 현재 월드와 플레이어 참조 처리
        if (auto P = CurrentPlayer())
        {
            if (P->TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Queued ||
                P->TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Sending)
            {
                P->TownAnchorSaveStatus.State = EFableTownAnchorSaveState::Failed;
            }
            P->bStorageBlocked = true;
            P->bBackendReady = false;
            P->bLastSaveSucceeded = false;
            P->bBackendSavePending = false;
            P->SetNotice(Message);
            P->ForceNetUpdate();
            // A failed voluntary close must keep the player on the error screen, never announce a saved exit.

            // 현재 월드와 플레이어 참조 처리
            if (!Closing)
            {
                if (auto M = World.IsValid() ? World->GetAuthGameMode<AFableGameMode>() : nullptr)
                {
                    M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()), false, Message);
                }
            }
        }

        CompleteLeave(false, Message);
    }

    /** 게임 서버가 소유한 백엔드 접속 권한을 갱신한다. */
    void Renew()
    {
        // 접속 권한·저장 리비전·멱등 키 조건 검사
        if (Failed || Released || ReleaseStarted || HeartbeatPending || !World.IsValid())
        {
            return;
        }
        if (FPlatformTime::Seconds() > LeaseDeadline)
        {
            Failure(TEXT("서버 저장 권한이 만료되어 진행을 중단했습니다."));
            return;
        }

        // 접속 권한·저장 리비전·멱등 키 처리
        HeartbeatPending = true;
        auto Self = AsShared();
        FFableBackend::Post(World.Get(), TEXT("/v1/server/heartbeat"), Identity(),
                            [Self](bool Ok, int32 Code, TSharedPtr<FJsonObject>)
                            {
                                Self->HeartbeatPending = false;
                                // A response from before release may arrive after the lease was successfully released.
                                if (Self->Failed || Self->Released || Self->ReleaseStarted)
                                {
                                    return;
                                }
                                if (Ok)
                                {
                                    Self->LeaseDeadline = FPlatformTime::Seconds() + 50;
                                }
                                else if (Code >= 400 && Code < 500 && Code != 408 && Code != 429)
                                {
                                    Self->Failure(TEXT("서버 저장 권한을 갱신할 수 없습니다."));
                                }
                            });
    }

    /** 유효한 접속 권한 또는 검증 입력의 유지 상태를 해제한다. */
    void Release()
    {
        // 접속 권한·저장 리비전·멱등 키 조건 검사
        if (Failed || Released || ReleasePending || Pending || !Closing || !World.IsValid())
        {
            return;
        }
        if (!InventoryAcknowledged || !SummaryAcknowledged || LatestHash != AcknowledgedHash)
        {
            Failure(TEXT("최종 저장 확인 전에는 안전하게 퇴장할 수 없습니다."));
            return;
        }

        // 접속 권한·저장 리비전·멱등 키 처리
        ReleaseStarted = true;
        ReleasePending = true;
        World->GetTimerManager().ClearTimer(HeartbeatTimer);
        World->GetTimerManager().ClearTimer(Retry);
        auto Self = AsShared();
        FFableBackend::Post(
            World.Get(), TEXT("/v1/server/release-confirmed"), Identity(),
            [Self](bool Ok, int32 Code, TSharedPtr<FJsonObject> J)
            {
                Self->ReleasePending = false;
                if (Self->Failed || Self->Released || !Self->World.IsValid())
                {
                    return;
                }
                const auto* Receipt = J.IsValid() ? J->Values.Find(TEXT("released")) : nullptr;
                if (Ok && Receipt && Receipt->IsValid() && (*Receipt)->Type == EJson::Boolean && (*Receipt)->AsBool())
                {
                    Self->Released = true;
                    Self->Mirror();
                    Self->CompleteLeave(true, TEXT("서버 저장과 안전 퇴장이 완료되었습니다."));
                    if (Self->V9SummarySeed)
                    {
                        if (auto P = Self->CurrentPlayer())
                        {
                            auto* PC = Cast<AFablePlayerController>(P->GetOwner());
                            if (PC && PC->GetPlayerState<AFablePlayerState>() == P)
                            {
                                PC->ClientRequireProfileReclaim(Self->Revision, Self->AcknowledgedHash);
                            }
                        }
                    }
                }
                else if (!Ok && (Code == 0 || Code >= 500 || Code == 408 || Code == 429))
                {
                    // The dedicated receipt route can acknowledge an already released lease without touching a new lease.
                    Self->World->GetTimerManager().SetTimer(
                        Self->Retry,
                        [Self]()
                        {
                            Self->Release();
                        },
                        1.0f, false);
                }
                else
                {
                    Self->Failure(
                        TEXT("서버 퇴장 확인에 실패했습니다. 저장 완료로 표시하지 않으며 접속을 유지합니다."));
                }
            });
    }

    /** 최신 프로필을 기존 저장 순서를 유지하는 대기열에 등록한다. */
    void Queue(const FString& Payload, const FString& Hash, const TSharedRef<FJsonObject>& W,
               const TSharedRef<FJsonObject>& Inventory, const TSharedRef<FJsonObject>& Summary, bool bFinalSnapshot,
               const FString& PublicKey, int64 GrantSequence, const FFableTownAnchorSaveWitness& AnchorWitness)
    {
        // 접속 권한·저장 리비전·멱등 키 조건 검사
        if (Failed || (Closing && !bFinalSnapshot) || ReleaseStarted || Released)
        {
            return;
        }

        // 하나의 인증 세션은 캐릭터 키나 이미 포함한 지급 경계를 바꿀 수 없다.
        if ((!ExpeditionPublicKey.IsEmpty() && PublicKey != ExpeditionPublicKey) ||
            (!SentExpeditionPublicKey.IsEmpty() && Pending && PublicKey != SentExpeditionPublicKey) ||
            (PublicKey == ExpeditionPublicKey && GrantSequence < LatestGrantSequence))
        {
            Failure(TEXT("원정 보상 저장의 캐릭터 또는 스냅숏 순서가 일치하지 않습니다."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        LatestAnchorWitness = AnchorWitness;
        LatestPayload = Payload;
        LatestHash = Hash;
        LatestWallet = W;
        LatestInventory = Inventory;
        LatestSummary = Summary;
        ExpeditionPublicKey = PublicKey;
        LatestGrantSequence = GrantSequence;

        // 프로필 저장과 완료 응답 조건 검사
        if (InventoryAcknowledged && SummaryAcknowledged && !Pending && Hash == AcknowledgedHash)
        {
            // 새 write를 꾸미지 않고 정확히 등록한 같은 bytes의 이전 ACK를 사용한다.
            if (auto P = CurrentPlayer(); P && LatestAnchorWitness.IsFor(PlayerId, Hash))
            {
                LatestAnchorWitness.SentExpectedRevision = Revision;
                P->AcceptTownAnchorAcknowledgement(LatestAnchorWitness, Revision, true);
            }

            // 동일 bytes의 실제 이전 ACK는 새 영수증 경계에도 유효하다.
            if (AFableGameMode* Mode = AFableGameMode::Find(World.Get()))
            {
                Mode->NotifyExpeditionSaveCommitted(ExpeditionPublicKey, LatestGrantSequence, Revision, Hash);
            }
            Mirror();
            return;
        }

        // 이 단계에서 최신 프로필을 기존 저장 순서를 유지하는 대기열에 등록한다.
        if (!Pending)
        {
            Start();
        }
    }

    /** 저장 대기열의 다음 전송 작업을 시작한다. */
    void Start()
    {
        // 접속 권한·저장 리비전·멱등 키 조건 검사
        if (Failed || ReleaseStarted || !World.IsValid() || !LatestWallet.IsValid() || !LatestInventory.IsValid() ||
            !LatestSummary.IsValid())
        {
            return;
        }

        // 재화와 작업 비용 처리
        Pending = true;
        SentAnchorWitness = LatestAnchorWitness;
        SentAnchorWitness.SentExpectedRevision = Revision;
        if (auto P = CurrentPlayer(); P && SentAnchorWitness.IsFor(PlayerId, LatestHash) &&
                                      P->TownAnchorSaveStatus.AnchorSequence == SentAnchorWitness.AnchorSequence)
        {
            P->TownAnchorSaveStatus.State = EFableTownAnchorSaveState::Sending;
        }
        SentPayload = LatestPayload;
        SentHash = LatestHash;
        SentWallet = LatestWallet;
        SentInventory = LatestInventory;
        SentSummary = LatestSummary;
        SentExpeditionPublicKey = ExpeditionPublicKey;
        SentGrantSequence = LatestGrantSequence;
        OperationId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
        // All mirrors come from one SaveGame snapshot. Ambiguous retries retain this exact operation and body.
        SentBody = Identity();
        SentBody->SetStringField(TEXT("operationId"), OperationId);
        SentBody->SetNumberField(TEXT("expectedRevision"), Revision);
        SentBody->SetStringField(TEXT("profile"), SentPayload);
        SentBody->SetObjectField(TEXT("wallet"), SentWallet);
        SentBody->SetObjectField(TEXT("inventory"), SentInventory);
        SentBody->SetObjectField(TEXT("summary"), SentSummary);
        Mirror();
        Send();
    }

    /** 대기 중인 프로필과 계약 투영을 백엔드로 전송한다. */
    void Send()
    {
        // 접속 권한·저장 리비전·멱등 키 조건 검사
        if (Failed || ReleaseStarted || !World.IsValid())
        {
            return;
        }
        if (FPlatformTime::Seconds() > LeaseDeadline)
        {
            Failure(TEXT("저장 권한이 만료되어 진행을 중단했습니다."));
            return;
        }
        if (!SentBody.IsValid())
        {
            Failure(TEXT("중앙 저장 작업이 올바르지 않습니다."));
            return;
        }

        // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
        auto Self = AsShared();
        FFableBackend::Post(
            World.Get(), TEXT("/v1/server/commit-character"), SentBody.ToSharedRef(),
            [Self](bool Ok, int32 Code, TSharedPtr<FJsonObject> J)
            {
                if (Self->Failed || !Self->World.IsValid())
                {
                    return;
                }
                if (Ok && FableCharacterSummary::IsCommitAcknowledgement(J, Self->Revision + 1, Self->SentHash))
                {
                    // 접속 권한·저장 리비전·멱등 키 처리
                    ++Self->Revision;
                    Self->Pending = false;
                    Self->AcknowledgedHash = Self->SentHash;
                    Self->InventoryAcknowledged = true;
                    Self->SummaryAcknowledged = true;

                    // V9原本のsummary確立は、通常のlogin/save完了から完全に分離する。
                    if (Self->V9SummarySeed)
                    {
                        FSummarySeedReceipt Receipt;
                        Receipt.Hash = Self->SentHash;
                        Receipt.Revision = Self->Revision;
                        Receipt.ExpiresAt = FPlatformTime::Seconds() + 600.0;
                        SeedReceipts(Self->World.Get()).Add(Self->PlayerId, Receipt);
                        Self->Closing = true;
                        if (auto P = Self->CurrentPlayer())
                        {
                            P->bBackendClosing = true;
                            P->bBackendReady = false;
                            P->ForceNetUpdate();
                        }
                        Self->Mirror();
                        Self->Release();
                        return;
                    }

                    // 정확 Sent hash/revision과 양쪽 wire ACK를 통과한 증서만 반영한다.
                    if (auto P = Self->CurrentPlayer();
                        P && Self->SentAnchorWitness.IsFor(Self->PlayerId, Self->SentHash))
                    {
                        P->AcceptTownAnchorAcknowledgement(Self->SentAnchorWitness, Self->Revision, false);
                    }

                    // PS가 파괴되어도 검증된 동일 배치의 경계를 서버 장부에 반영한다.
                    if (AFableGameMode* Mode = AFableGameMode::Find(Self->World.Get()))
                    {
                        Mode->NotifyExpeditionSaveCommitted(Self->SentExpeditionPublicKey, Self->SentGrantSequence,
                                                            Self->Revision, Self->SentHash);
                    }

                    // 프로필 저장과 완료 응답 처리
                    if (auto P = Self->CurrentPlayer())
                    {
                        P->BackendAcknowledgedProfileHash = Self->SentHash;
                    }

                    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
                    if (Self->LatestHash != Self->SentHash)
                    {
                        Self->Start();
                        return;
                    }

                    // 상한에서 실제 bytes가 같아도 pending 동안 새 명세가 추가될 수 있다.
                    if (Self->LatestHash == Self->SentHash &&
                        Self->ExpeditionPublicKey == Self->SentExpeditionPublicKey &&
                        Self->LatestGrantSequence > Self->SentGrantSequence)
                    {
                        if (AFableGameMode* Mode = AFableGameMode::Find(Self->World.Get()))
                        {
                            Mode->NotifyExpeditionSaveCommitted(Self->ExpeditionPublicKey, Self->LatestGrantSequence,
                                                                Self->Revision, Self->SentHash);
                        }
                    }

                    if (auto P = Self->CurrentPlayer();
                        P && Self->LatestAnchorWitness.IsFor(Self->PlayerId, Self->SentHash) &&
                        Self->LatestAnchorWitness.AnchorSequence != Self->SentAnchorWitness.AnchorSequence)
                    {
                        Self->LatestAnchorWitness.SentExpectedRevision = Self->Revision;
                        P->AcceptTownAnchorAcknowledgement(Self->LatestAnchorWitness, Self->Revision, true);
                    }
                    Self->Mirror();

                    // 현재 월드와 플레이어 참조 처리
                    if (!Self->LoginComplete && !Self->Closing)
                    {
                        Self->LoginComplete = true;
                        if (auto P = Self->CurrentPlayer())
                        {
                            P->ProfileReadStatus.FirstAcknowledgedHash = Self->AcknowledgedHash;
                            P->ProfileReadStatus.FirstAcknowledgedRevision = Self->Revision;
                            P->bBackendReady = true;
                            P->ForceNetUpdate();
                            if (auto M = Self->World->GetAuthGameMode<AFableGameMode>())
                            {
                                M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()), true, TEXT(""));
                            }
                        }
                    }

                    // 접속 권한·저장 리비전·멱등 키 처리
                    if (Self->Closing)
                    {
                        Self->Release();
                    }
                }
                else if (!Ok && (Code == 0 || Code >= 500 || Code == 408 || Code == 429))
                {
                    Self->World->GetTimerManager().SetTimer(
                        Self->Retry,
                        [Self]()
                        {
                            Self->Send();
                        },
                        1.0f, false);
                }
                else
                {
                    Self->Failure(TEXT("중앙 저장 확인에 실패했습니다. 진행을 중단하고 저장 원본을 보존합니다."));
                }
            });
    }
};

/** 서버가 확인한 백엔드 접속 권한과 영구 프로필을 적용한다. */
void AFablePlayerState::InitializeBackendProfile(const FString& Ticket)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || GetNetMode() != NM_DedicatedServer || bProfileLoaded || bBackendEnabled)
    {
        return;
    }

    bBackendEnabled = true;
    bBackendReady = false;
    bBackendLoading = true;
    bPersistenceEnabled = false;
    bLastSaveSucceeded = false;
    auto Body = MakeShared<FJsonObject>();
    Body->SetStringField(TEXT("ticket"), Ticket);
    Body->SetStringField(TEXT("serverId"), FFableBackend::ServerId());
    TWeakObjectPtr<AFablePlayerState> Weak(this);
    TWeakObjectPtr<UWorld> LoginWorld(GetWorld());
    TWeakObjectPtr<APlayerController> LoginPlayer(Cast<APlayerController>(GetOwner()));
    FFableBackend::Post(
        GetWorld(), TEXT("/v1/server/claim"), Body,
        [Weak, LoginWorld, LoginPlayer](bool Ok, int32 Code, TSharedPtr<FJsonObject> J)
        {
            // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
            auto ReleaseClaim = [LoginWorld, J]()
            {
                FString Id, Lease;
                if (LoginWorld.IsValid() && J.IsValid() && J->TryGetStringField(TEXT("playerId"), Id) &&
                    J->TryGetStringField(TEXT("leaseId"), Lease))
                {
                    auto B = MakeShared<FJsonObject>();
                    B->SetStringField(TEXT("playerId"), Id);
                    B->SetStringField(TEXT("leaseId"), Lease);
                    FFableBackend::Post(LoginWorld.Get(), TEXT("/v1/server/release"), B,
                                        [](bool, int32, TSharedPtr<FJsonObject>)
                                        {
                                        });
                }
            };
            auto P = Weak.Get();

            // 접속 권한·저장 리비전·멱등 키 처리
            if (!P || P->GetWorld() != LoginWorld.Get() || !LoginPlayer.IsValid() || P->bBackendClosing ||
                P->GetOwner() != LoginPlayer.Get() || LoginPlayer->GetPlayerState<AFablePlayerState>() != P)
            {
                if (Ok)
                {
                    ReleaseClaim();
                }
                return;
            }

            // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
            auto Fail = [P, ReleaseClaim]()
            {
                ReleaseClaim();
                P->bStorageBlocked = true;
                P->bBackendReady = false;
                P->bLastSaveSucceeded = false;
                if (auto M = P->GetWorld()->GetAuthGameMode<AFableGameMode>())
                {
                    M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()), false,
                                            TEXT("계정 인증 또는 중앙 프로필을 확인할 수 없습니다."));
                }
            };
            FString Id, Lease, Payload, ExpectedHash;
            double Rev = -1, InitialClass = -1, InventoryVersion = -1;
            FGuid PlayerGuid, LeaseGuid;

            // 접속 권한·저장 리비전·멱등 키 조건 검사
            if (!Ok || !J.IsValid() || !J->TryGetStringField(TEXT("playerId"), Id) ||
                !J->TryGetStringField(TEXT("leaseId"), Lease) ||
                !FGuid::ParseExact(Id, EGuidFormats::DigitsWithHyphens, PlayerGuid) || !PlayerGuid.IsValid() ||
                !FGuid::ParseExact(Lease, EGuidFormats::DigitsWithHyphens, LeaseGuid) || !LeaseGuid.IsValid() ||
                Id != PlayerGuid.ToString(EGuidFormats::DigitsWithHyphensLower) ||
                Lease != LeaseGuid.ToString(EGuidFormats::DigitsWithHyphensLower) ||
                !J->TryGetNumberField(TEXT("revision"), Rev) || !FMath::IsFinite(Rev) || Rev < 0 ||
                Rev > 9007199254740990.0 || Rev != double(int64(Rev)) ||
                !J->TryGetNumberField(TEXT("initialClass"), InitialClass) || !FMath::IsFinite(InitialClass) ||
                InitialClass < 0 || InitialClass > 2 || InitialClass != double(int32(InitialClass)) ||
                !J->TryGetNumberField(TEXT("inventoryVersion"), InventoryVersion) ||
                (InventoryVersion != 0 && InventoryVersion != 1))
            {
                Fail();
                return;
            }
            if (!P->BackendPlayerId.IsEmpty() && P->BackendPlayerId != Id)
            {
                Fail();
                return;
            }

            // 프로필과 저장 스냅숏에 필요한 값 준비
            const auto* ProfileField = J->Values.Find(TEXT("profile"));
            const auto* InventoryField = J->Values.Find(TEXT("inventory"));
            const auto* SummaryField = J->Values.Find(TEXT("summary"));

            // 프로필과 저장 스냅숏 조건 검사
            if (!ProfileField || !ProfileField->IsValid() || !InventoryField || !InventoryField->IsValid() ||
                !SummaryField || !SummaryField->IsValid())
            {
                Fail();
                return;
            }

            const bool bHasSummary = (*SummaryField)->Type == EJson::Object;

            // JSON 계약과 검증 기록 조건 검사
            if (!bHasSummary && (*SummaryField)->Type != EJson::Null)
            {
                Fail();
                return;
            }
            if (bHasSummary && InventoryVersion != 1)
            {
                Fail();
                return;
            }

            const bool bHasStoredProfile = (*ProfileField)->Type == EJson::String;

            // JSON 계약과 검증 기록 조건 검사
            if ((!bHasStoredProfile && (*ProfileField)->Type != EJson::Null) ||
                (bHasStoredProfile && (!(*ProfileField)->TryGetString(Payload) || Payload.IsEmpty())))
            {
                Fail();
                return;
            }
            if (InventoryVersion == 0 && (*InventoryField)->Type != EJson::Null)
            {
                Fail();
                return;
            }
            if (InventoryVersion == 1 && (!bHasStoredProfile || (*InventoryField)->Type != EJson::Object))
            {
                Fail();
                return;
            }
            if (!bHasStoredProfile && Rev != 0)
            {
                Fail();
                return;
            }

            // 原本bytes/hash/versionを保持し、mirror検証より先に移行しない。
            UFableSaveGame* Original = nullptr;
            TArray<uint8> OriginalBytes;
            if (bHasStoredProfile)
            {
                if (!FBase64::Decode(Payload, OriginalBytes) || OriginalBytes.IsEmpty() ||
                    OriginalBytes.Num() > 262144 || FBase64::Encode(OriginalBytes) != Payload ||
                    !J->TryGetStringField(TEXT("profileHash"), ExpectedHash) || ExpectedHash.Len() != 64 ||
                    HashBytes(OriginalBytes) != ExpectedHash.ToLower())
                {
                    Fail();
                    return;
                }
                Original = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(OriginalBytes));
                if (!Original ||
                    (InventoryVersion == 1 ? !FableProfileSchema::CanProjectOriginalMirror(Original->Version)
                                           : Original->Version >= FableProfileSchema::ItemIdentityVersion))
                {
                    Fail();
                    return;
                }

                // version0の歴史的省略だけを別オブジェクトで正規化する。原本は不変。
                UFableSaveGame* Validation = DuplicateObject<UFableSaveGame>(Original, GetTransientPackage());
                Validation->NormalizeLoadedVersion();
                if (!Validation->IsValidProfile() ||
                    (InventoryVersion == 1 &&
                     !FableInventorySnapshot::Matches(Original, (*InventoryField)->AsObject())) ||
                    (bHasSummary && !FableCharacterSummary::Matches(Original, (*SummaryField)->AsObject())) ||
                    (!bHasSummary && Original->Version == FableProfileSchema::CurrentVersion))
                {
                    Fail();
                    return;
                }
            }
            else
            {
                const auto* HashField = J->Values.Find(TEXT("profileHash"));
                if (!HashField || !HashField->IsValid() || (*HashField)->Type != EJson::Null || bHasSummary)
                {
                    Fail();
                    return;
                }
            }

            const bool bSeed = Original && Original->Version == FableProfileSchema::ItemIdentityVersion &&
                               InventoryVersion == 1 && !bHasSummary;
            auto& Receipts = SeedReceipts(P->GetWorld());
            if (const FSummarySeedReceipt* Receipt = Receipts.Find(Id))
            {
                if (!Original || bSeed || !bHasSummary || Receipt->Hash != ExpectedHash.ToLower() ||
                    Receipt->Revision != int64(Rev))
                {
                    Fail();
                    return;
                }
            }
            if (bSeed && Receipts.Num() >= 64 && !Receipts.Contains(Id))
            {
                Fail();
                return;
            }

            // 既存walletの全7項目も原本で照合する。新規時だけauthority starterを用意する。
            P->BackendPlayerId = Id;
            UFableSaveGame* Fresh = nullptr;
            if (!Original)
            {
                P->InitializeProfile(true, TEXT(""), EFableClass(int32(InitialClass)));
                Fresh = P->MakeProfileSnapshot();
            }
            const auto ExpectedWallet = Wallet(Original ? Original : Fresh);
            const TSharedPtr<FJsonObject>* W = nullptr;
            bool WalletMatches = J->TryGetObjectField(TEXT("wallet"), W) && W && W->IsValid() &&
                                 (*W)->Values.Num() == ExpectedWallet->Values.Num();
            if (WalletMatches)
            {
                for (const auto& E : ExpectedWallet->Values)
                {
                    const auto* Actual = (*W)->Values.Find(E.Key);
                    WalletMatches &= Actual && Actual->IsValid() && (*Actual)->Type == EJson::Number &&
                                     (*Actual)->AsNumber() == E.Value->AsNumber();
                }
            }
            if (Original && WalletMatches)
            {
                bool bValidatedSeed = false;
                WalletMatches = FableProfileSchema::OriginalMirrorsMatch(
                                    Original, int32(InventoryVersion), *W,
                                    InventoryVersion == 1 ? (*InventoryField)->AsObject() : nullptr, *SummaryField,
                                    bValidatedSeed) &&
                                bValidatedSeed == bSeed;
            }
            if (!WalletMatches)
            {
                Fail();
                return;
            }

            P->ProfileReadStatus.OriginalVersion = Original ? Original->Version : -1;
            P->ProfileReadStatus.OriginalHash = ExpectedHash.ToLower();
            P->ProfileReadStatus.OriginalRevision = int64(Rev);
            P->ProfileReadStatus.bOriginalMirrorsValidated = Original != nullptr;

            // 全投影検証後だけlease sessionを作り、seedは通常のPS適用を通らない。
            P->BackendSession = MakeShared<FFableBackendSession>();
            auto Session = P->BackendSession;
            Session->Player = P;
            Session->World = P->GetWorld();
            Session->PlayerId = Id;
            Session->LeaseId = Lease;
            Session->Revision = int64(Rev);
            Session->LeaseDeadline = FPlatformTime::Seconds() + 50;
            Session->AcknowledgedHash = ExpectedHash.ToLower();
            Session->InventoryAcknowledged = InventoryVersion == 1;
            Session->SummaryAcknowledged = bHasSummary;
            P->BackendAcknowledgedProfileHash = Session->AcknowledgedHash;
            P->GetWorld()->GetTimerManager().SetTimer(
                Session->HeartbeatTimer,
                [Session]()
                {
                    Session->Renew();
                },
                15.0f, true);

            if (bSeed)
            {
                // 再serializeせず、同じraw V9 payload・wallet・inventoryに初回summaryだけを添える。
                const auto Summary = FableCharacterSummary::MakeSummary(Original);
                if (!Summary.IsValid())
                {
                    Session->Failure(TEXT("이전 프로필 요약을 확인할 수 없습니다. 원본을 보존합니다."));
                    return;
                }
                Session->V9SummarySeed = true;
                Session->Queue(Payload, ExpectedHash.ToLower(), ExpectedWallet,
                               (*InventoryField)->AsObject().ToSharedRef(), Summary.ToSharedRef(), false, TEXT(""), 0,
                               FFableTownAnchorSaveWitness());
                return;
            }

            // 同じ検証済candidateから全投影と最初のV10 commitを作る。UTCを作り直さない。
            UFableSaveGame* Candidate = Original ? FableProfileSchema::MakeCandidate(Original) : Fresh;
            if (!Candidate || !Candidate->IsValidProfile())
            {
                Session->Failure(TEXT("프로필 형식을 이전할 수 없습니다. 저장 원본을 보존합니다."));
                return;
            }
            if (Original && Original->Version == FableProfileSchema::ItemIdentityVersion)
            {
                P->ProfileReadStatus.bHistoricalFieldsCompared = true;
                P->ProfileReadStatus.bHistoricalFieldsByteExact =
                    FableProfileSchema::PreservesOriginalNineBytes(Candidate, OriginalBytes);
                if (!P->ProfileReadStatus.bHistoricalFieldsByteExact)
                {
                    Session->Failure(TEXT("이전 프로필의 전체 보존을 확인하지 못했습니다. 원본을 보존합니다."));
                    return;
                }
            }
            if (Original)
            {
                P->InitializeProfile(true, TEXT(""), EFableClass(int32(InitialClass)));
            }
            P->ProfileSlot = TEXT("backend:") + Id;
            P->ApplyProfileSnapshot(Candidate);
            P->ProfileReadStatus.CurrentVersion = Candidate->Version;
            P->bBackendLoading = false;
            P->bProfileLoaded = true;
            P->bLastSaveSucceeded = false;
            Receipts.Remove(Id);
            if (!P->QueueBackendSnapshot(Candidate))
            {
                Session->Failure(TEXT("중앙 프로필을 직렬화할 수 없습니다."));
            }
            else if (!Session->Pending)
            {
                Session->LoginComplete = true;
                P->ProfileReadStatus.FirstAcknowledgedHash = Session->AcknowledgedHash;
                P->ProfileReadStatus.FirstAcknowledgedRevision = Session->Revision;
                P->bBackendReady = true;
                Session->Mirror();
                if (auto M = P->GetWorld()->GetAuthGameMode<AFableGameMode>())
                {
                    M->CompleteBackendLogin(Cast<APlayerController>(P->GetOwner()), true, TEXT(""));
                }
            }
        });
}

/** 현재 프로필을 백엔드 저장 대기열에 넣는다. */
bool AFablePlayerState::QueueBackendSnapshot(UFableSaveGame* Data, bool bFinalSnapshot)
{
    // 프로필과 저장 스냅숏 조건 검사
    if (bBackendLoading)
    {
        return true;
    }
    if (!BackendSession.IsValid() || (BackendSession->Closing && !bFinalSnapshot) || !Data ||
        !FableProfileSchema::IsWritableCurrentVersion(Data->Version) || !Data->IsValidProfile())
    {
        return false;
    }

    TArray<uint8> Bytes;

    // 프로필과 저장 스냅숏 조건 검사
    if (!UGameplayStatics::SaveGameToMemory(Data, Bytes) || Bytes.Num() > 262144)
    {
        return false;
    }

    const FString Hash = HashBytes(Bytes);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Hash.IsEmpty())
    {
        return false;
    }

    const auto InventorySnapshot = FableInventorySnapshot::Make(Data);

    // 프로필과 저장 스냅숏 조건 검사
    if (!InventorySnapshot.IsValid())
    {
        return false;
    }

    const auto Summary = FableCharacterSummary::MakeSummary(Data);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Summary.IsValid())
    {
        return false;
    }

    // 영수증 순번은 지급 직후 발급됐으며 실제 직렬화한 hash와 함께 등록한다.
    AFableGameMode* Mode = AFableGameMode::Find(GetWorld());
    const int64 GrantSequence = ExpeditionSnapshotGrantSequence;
    if (Mode)
    {
        Mode->TrackExpeditionSaveSnapshot(PartyPlayerKey, GrantSequence, Hash);
    }
    FFableTownAnchorSaveWitness Witness;
    if (TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Queued ||
        TownAnchorSaveStatus.State == EFableTownAnchorSaveState::Sending)
    {
        const auto& Connection = GetTownConnectionSnapshot();
        const auto Anchor = Data->GetLastSafeTownAnchor();
        if (Anchor.TownId != TownAnchorSaveStatus.Anchor.TownId ||
            Anchor.AnchorId != TownAnchorSaveStatus.Anchor.AnchorId ||
            Anchor.CatalogVersion != TownAnchorSaveStatus.Anchor.CatalogVersion)
        {
            return false;
        }
        Witness.TravelTransactionId = TownAnchorSaveStatus.TravelTransactionId;
        Witness.AnchorSequence = TownAnchorSaveStatus.AnchorSequence;
        Witness.Anchor = Anchor;
        Witness.SnapshotHash = Hash;
        Witness.StablePlayerId = BackendPlayerId;
        Witness.OwnerConnectionGeneration = Connection.OwnerConnectionGeneration;
        Witness.ConnectionNonce = Connection.ConnectionNonce;
        Witness.WorldEpoch = Connection.WorldEpoch;
        RegisteredTownSnapshotHash = Hash;
        if (!Witness.IsFor(BackendPlayerId, Hash))
        {
            return false;
        }
    }
    BackendSession->Queue(FBase64::Encode(Bytes), Hash, Wallet(Data), InventorySnapshot.ToSharedRef(),
                          Summary.ToSharedRef(), bFinalSnapshot, PartyPlayerKey, GrantSequence, Witness);

    return !BackendSession->Failed;
}

/** 실제 ACK를 받은 초기 current bytes만 복원의 witness에 연결한다. */
bool AFablePlayerState::QueueAcknowledgedTownRestoreSnapshot()
{
    if (!bAwaitingProfileRestore || !BackendSession.IsValid() || BackendSession->Pending || BackendSession->Failed ||
        !BackendSession->InventoryAcknowledged || !BackendSession->SummaryAcknowledged ||
        BackendSession->LatestHash != BackendSession->AcknowledgedHash)
    {
        return false;
    }

    TArray<uint8> Bytes, Serialized;
    if (!FBase64::Decode(BackendSession->LatestPayload, Bytes) || HashBytes(Bytes) != BackendSession->AcknowledgedHash)
    {
        return false;
    }
    auto* Data = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!Data || !FableProfileSchema::IsWritableCurrentVersion(Data->Version) || !Data->IsValidProfile() ||
        !UGameplayStatics::SaveGameToMemory(Data, Serialized) || Serialized != Bytes ||
        Data->LastSafeTownId != LastSafeTownAnchor.TownId || Data->LastSafeAnchorId != LastSafeTownAnchor.AnchorId ||
        Data->TownCatalogVersion != LastSafeTownAnchor.CatalogVersion)
    {
        return false;
    }
    return QueueBackendSnapshot(Data);
}

/** 게임 서버가 보유한 프로필 접속 권한을 반환한다. */
void AFablePlayerState::ReleaseBackendProfile()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bBackendEnabled)
    {
        return;
    }

    const bool bAlreadyClosing = bBackendClosing;
    bBackendClosing = true;
    bBackendReady = false;
    ForceNetUpdate();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BackendSession.IsValid() || bAlreadyClosing)
    {
        return;
    }
    // Mark closing before any HTTP dispatch (even synchronous dispatch failure must not kick the player).
    // Only this first close may enqueue a final snapshot; later Logout/retry calls reuse it.

    BackendSession->Closing = true;

    // 프로필 저장과 완료 응답 조건 검사
    // seed接続はstarter/live PSを保存しない。同じin-flight bodyだけをdrainする。
    if (BackendSession->V9SummarySeed || bAwaitingProfileRestore)
    {
        if (!BackendSession->Pending)
        {
            BackendSession->Release();
        }
        return;
    }

    if (!QueueBackendSnapshot(MakeProfileSnapshot(), true))
    {
        BackendSession->Failure(TEXT("최종 저장 자료를 준비할 수 없습니다. 안전 퇴장이 완료되지 않았습니다."));
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    if (!BackendSession->Pending)
    {
        BackendSession->Release();
    }
}

/** 미완료 저장의 확인을 기다린 뒤 안전한 접속 종료를 요청한다. */
void AFablePlayerState::RequestBackendLeave(TFunction<void(bool, const FString&)> Completion)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bBackendEnabled || !BackendSession.IsValid() || !BackendSession->LoginComplete)
    {
        Completion(false, TEXT("중앙 프로필 접속이 완료된 뒤 다시 시도해 주세요."));
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    ReleaseBackendProfile();
    BackendSession->AwaitLeave(MoveTemp(Completion));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableBackendSHA256Test, "Fablewood.Backend.SHA256KnownVectors",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 백엔드 SHA-256 동작과 저장 상태의 회귀 검증을 수행한다. */
bool FFableBackendSHA256Test::RunTest(const FString& Parameters)
{
    // Independent NIST vectors catch unsupported platform APIs and hashing the base64 text instead of raw bytes.
    // 기대값과 상태 보존 검증에 필요한 값 준비
    TArray<uint8> Bytes;
    TestEqual(TEXT("Empty raw bytes"), HashBytes(Bytes),
              FString(TEXT("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")));
    Bytes = {0x61, 0x62, 0x63};
    TestEqual(TEXT("Raw abc bytes"), HashBytes(Bytes),
              FString(TEXT("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")));
    const ANSICHAR* Multi = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Bytes.Reset();
    Bytes.Append(reinterpret_cast<const uint8*>(Multi), FCStringAnsi::Strlen(Multi));
    TestEqual(TEXT("Multi-block raw bytes"), HashBytes(Bytes),
              FString(TEXT("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")));

    return true;
}

/** 独立memory worldの接続だけを設定しHTTPやユーザーデータを使わない。 */
struct FFableBackendSessionTestAccess
{
    /** 테스트 소유 연결만 새로운 session에 결합한다. */
    static TSharedPtr<FFableBackendSession> Bind(AFablePlayerState* P, APlayerController* PC, UWorld* World)
    {
        auto Session = MakeShared<FFableBackendSession>();
        Session->Player = P;
        Session->World = World;
        Session->PlayerId = TEXT("native-session-owner");
        P->BackendPlayerId = Session->PlayerId;
        P->SetOwner(PC);
        PC->SetPlayerState(P);
        P->BackendSession = Session;
        return Session;
    }

    /** 이전 callback이 새 소유자의 차단 상태를 변경했는지 읽는다. */
    static bool StorageBlocked(const AFablePlayerState* P)
    {
        return P->bStorageBlocked;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableBackendReplacedSessionTest,
                                 "Fablewood.Backend.ReplacedSessionCannotMirrorFailOrUnlockCurrentOwner",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 같은 PS 재사용까지 포함해 이전 session callback의 actor 적용을 거절한다. */
bool FFableBackendReplacedSessionTest::RunTest(const FString& Parameters)
{
    const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                    .RequiresHitProxies(false)
                                                    .CreatePhysicsScene(false)
                                                    .ShouldSimulatePhysics(false)
                                                    .EnableTraceCollision(false)
                                                    .CreateNavigation(false)
                                                    .CreateAISystem(false)
                                                    .AllowAudioPlayback(false);
    UWorld* World =
        UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Independent memory world"), World))
    {
        return false;
    }
    auto* P = World->SpawnActor<AFablePlayerState>();
    auto* PC = World->SpawnActor<APlayerController>();
    if (!P || !PC)
    {
        AddError(TEXT("Memory owner spawn failed"));
        World->DestroyWorld(false);
        return false;
    }
    World->AddController(PC);
    const auto Old = FFableBackendSessionTestAccess::Bind(P, PC, World);
    TestTrue(TEXT("Original matching session may update owner"), Old->CurrentPlayer() == P);
    Old->Revision = 7;
    Old->Mirror();
    TestEqual(TEXT("Positive mirror control"), P->GetBackendAcknowledgedRevision(), int64(7));

    const auto Current = FFableBackendSessionTestAccess::Bind(P, PC, World);
    Current->Revision = 9;
    Current->Mirror();
    TestTrue(TEXT("New matching session may update same PS"), Current->CurrentPlayer() == P);
    TestTrue(TEXT("Replaced session has no current actor binding"), Old->CurrentPlayer() == nullptr);
    Old->Revision = 100;
    Old->Mirror();
    Old->Failure(TEXT("native isolated old session failure"));
    TestEqual(TEXT("Old callback cannot overwrite new revision"), P->GetBackendAcknowledgedRevision(), int64(9));
    TestFalse(TEXT("Old failure cannot block or kick current owner"),
              FFableBackendSessionTestAccess::StorageBlocked(P));
    TestTrue(TEXT("Current session survives"), !Current->Failed && Current->CurrentPlayer() == P);

    World->RemoveController(PC);
    World->DestroyWorld(false);
    return true;
}
#endif
