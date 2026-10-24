#pragma once

#include "CoreMinimal.h"
#include "FableExpeditionTypes.h"
#include "FableExpeditionResultTypes.generated.h"

/** 원정 결과는 NPC 의뢰 보고 여부와 별도로 관리한다. */
UENUM()
enum class EFableExpeditionOutcome : uint8
{
    Active,
    Cleared,
    Recovered,
    Abandoned
};

/** 메모리 fixture를 실제 영구 저장으로 표시하지 않는다. */
UENUM()
enum class EFableExpeditionGrantSaveState : uint8
{
    MemoryOnly,
    Queued,
    Committed
};

/** 한 적의 처치에서 실제 개인에게 지급된 불변 내역. */
USTRUCT()
struct FFablePersonalGrantReceipt
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid ExpeditionId;

    UPROPERTY()
    FGuid GrantId;

    UPROPERTY()
    FString RecipientPublicKey;

    UPROPERTY()
    int64 Sequence = 0;

    UPROPERTY()
    bool bBoss = false;

    UPROPERTY()
    int32 ActualGold = 0;

    UPROPERTY()
    int32 ActualOre = 0;

    UPROPERTY()
    int32 ActualExperience = 0;

    UPROPERTY()
    int32 ActualRootCores = 0;

    UPROPERTY()
    int32 ActualMoonCrystals = 0;

    UPROPERTY()
    TArray<FFableItem> GrantedItems;

    UPROPERTY()
    EFableExpeditionGrantSaveState SaveState = EFableExpeditionGrantSaveState::MemoryOnly;

    UPROPERTY()
    int64 CommittedRevision = 0;
};

/** Actor 수명과 무관하게 서버가 보존하고 본인에게만 전달하는 결과. */
USTRUCT()
struct FFablePersonalExpeditionResult
{
    GENERATED_BODY()

    UPROPERTY()
    FFableExpeditionTicket Ticket;

    UPROPERTY()
    FString RecipientPublicKey;

    UPROPERTY()
    EFableExpeditionOutcome Outcome = EFableExpeditionOutcome::Active;

    UPROPERTY()
    FGuid CompletionId;

    UPROPERTY()
    int32 ResultRevision = 1;

    UPROPERTY()
    TArray<FFablePersonalGrantReceipt> Receipts;

    UPROPERTY()
    bool bAcknowledged = false;

    UPROPERTY()
    bool bAllGrantsCommitted = false;

    UPROPERTY()
    bool bSaveReady = true;

    UPROPERTY()
    bool bRequiresBackendCommit = false;

    UPROPERTY()
    FGuid RetryExpeditionId;

    /** 초기 상태와 본인의 원정 결과를 구분한다. */
    bool IsValid() const
    {
        return Ticket.IsValid() && !RecipientPublicKey.IsEmpty();
    }

    /** 완료 ID는 최초 종료 때 한 번만 발급한다. */
    bool IsTerminal() const
    {
        return IsValid() && Outcome != EFableExpeditionOutcome::Active && CompletionId.IsValid();
    }
};

/** 원래 참가 증서 전원의 명시적인 진행 중 귀환 동의. */
USTRUCT()
struct FFableExpeditionDepartureVote
{
    GENERATED_BODY()

    UPROPERTY()
    int32 Revision = 1;

    UPROPERTY()
    TArray<FString> ConsentingMemberKeys;
};
