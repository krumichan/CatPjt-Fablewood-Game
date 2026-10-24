#include "FablePlayerState.h"
#include "FableBackend.h"
#include "FableSaveGame.h"
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
    bool InventoryAcknowledged = false, SummaryAcknowledged = false;
    bool Pending = false, Closing = false, Failed = false, LoginComplete = false;
    bool HeartbeatPending = false, ReleaseStarted = false, ReleasePending = false, Released = false;
    FTimerHandle Retry, HeartbeatTimer, LeaveTimeout;
    double LeaseDeadline = 0;
    FString FailureReason;
    TFunction<void(bool, const FString&)> LeaveCompletion;

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
        if (auto P = Player.Get())
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
        if (auto P = Player.Get())
        {
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
               const FString& PublicKey, int64 GrantSequence)
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

                    // PS가 파괴되어도 검증된 동일 배치의 경계를 서버 장부에 반영한다.
                    if (AFableGameMode* Mode = AFableGameMode::Find(Self->World.Get()))
                    {
                        Mode->NotifyExpeditionSaveCommitted(Self->SentExpeditionPublicKey, Self->SentGrantSequence,
                                                            Self->Revision, Self->SentHash);
                    }

                    // 프로필 저장과 완료 응답 처리
                    if (auto P = Self->Player.Get())
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

                    Self->Mirror();

                    // 현재 월드와 플레이어 참조 처리
                    if (!Self->LoginComplete && !Self->Closing)
                    {
                        Self->LoginComplete = true;
                        if (auto P = Self->Player.Get())
                        {
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
    FFableBackend::Post(
        GetWorld(), TEXT("/v1/server/claim"), Body,
        [Weak, LoginWorld](bool Ok, int32 Code, TSharedPtr<FJsonObject> J)
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
            if (!P || !P->GetWorld() || P->bBackendClosing || !IsValid(P->GetOwner()))
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

            // The temporary decoded profile must agree with every typed item and wallet field
            // before applying it. Missing typed rows are accepted only for an explicit legacy claim.

            UFableSaveGame* D = nullptr;

            // 프로필과 저장 스냅숏 처리
            if (bHasStoredProfile)
            {
                TArray<uint8> Bytes;

                // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
                if (!FBase64::Decode(Payload, Bytes) || Bytes.Num() > 262144 ||
                    !J->TryGetStringField(TEXT("profileHash"), ExpectedHash) ||
                    HashBytes(Bytes) != ExpectedHash.ToLower())
                {
                    Fail();
                    return;
                }

                D = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));

                // 프로필과 저장 스냅숏 조건 검사
                if (!D || (InventoryVersion == 1 ? D->Version != 9 : D->Version >= 9) || !D->MigrateAndValidate())
                {
                    Fail();
                    return;
                }
                if (InventoryVersion == 1 && !FableInventorySnapshot::Matches(D, (*InventoryField)->AsObject()))
                {
                    Fail();
                    return;
                }
                if (bHasSummary && !FableCharacterSummary::Matches(D, (*SummaryField)->AsObject()))
                {
                    Fail();
                    return;
                }
            }
            else
            {
                const auto* HashField = J->Values.Find(TEXT("profileHash"));
                if (!HashField || !HashField->IsValid() || (*HashField)->Type != EJson::Null)
                {
                    Fail();
                    return;
                }
            }

            // 프로필과 저장 스냅숏 처리
            P->BackendPlayerId = Id;
            P->InitializeProfile(true, TEXT(""), EFableClass(int32(InitialClass)));
            P->ProfileSlot = TEXT("backend:") + Id;

            // 프로필과 저장 스냅숏 처리
            if (!D)
            {
                D = P->MakeProfileSnapshot();
            }

            // 재화와 작업 비용에 필요한 값 준비
            const TSharedPtr<FJsonObject>* W = nullptr;
            bool WalletMatches = J->TryGetObjectField(TEXT("wallet"), W) && W && W->IsValid();
            const auto Expected = Wallet(D);

            // 재화와 작업 비용 처리
            if (WalletMatches)
            {
                WalletMatches = (*W)->Values.Num() == Expected->Values.Num();
                for (const auto& E : Expected->Values)
                {
                    const auto* Actual = (*W)->Values.Find(E.Key);
                    WalletMatches &= Actual && Actual->IsValid() && (*Actual)->Type == EJson::Number &&
                                     (*Actual)->AsNumber() == E.Value->AsNumber();
                }
            }

            // 재화와 작업 비용 조건 검사
            if (!WalletMatches || P->BackendPlayerId != Id)
            {
                Fail();
                return;
            }

            // 프로필과 저장 스냅숏 처리
            P->ApplyProfileSnapshot(D);
            P->bBackendLoading = false;
            P->bProfileLoaded = true;
            P->bLastSaveSucceeded = false;
            P->BackendSession = MakeShared<FFableBackendSession>();
            auto Session = P->BackendSession;
            Session->Player = P;
            Session->World = P->GetWorld();
            Session->PlayerId = Id;
            Session->LeaseId = Lease;
            Session->Revision = int64(Rev);
            Session->LeaseDeadline = FPlatformTime::Seconds() + 50;
            P->GetWorld()->GetTimerManager().SetTimer(
                Session->HeartbeatTimer,
                [Session]()
                {
                    Session->Renew();
                },
                15.0f, true);
            Session->AcknowledgedHash = ExpectedHash.ToLower();
            Session->InventoryAcknowledged = InventoryVersion == 1;
            Session->SummaryAcknowledged = bHasSummary;
            P->BackendAcknowledgedProfileHash = Session->AcknowledgedHash;

            // 프로필 저장과 완료 응답 처리
            if (!P->QueueBackendSnapshot(D))
            {
                Session->Failure(TEXT("중앙 프로필을 직렬화할 수 없습니다."));
            }
            else if (!Session->Pending)
            {
                Session->LoginComplete = true;
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
    if (!BackendSession.IsValid() || (BackendSession->Closing && !bFinalSnapshot) || !Data || !Data->IsValidProfile())
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
    BackendSession->Queue(FBase64::Encode(Bytes), Hash, Wallet(Data), InventorySnapshot.ToSharedRef(),
                          Summary.ToSharedRef(), bFinalSnapshot, PartyPlayerKey, GrantSequence);

    return !BackendSession->Failed;
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
#endif
