#pragma once

#include "CoreMinimal.h"
#include "FableRPGTypes.h"
#include "FableExpeditionTypes.generated.h"

/** 서버가 입장 시 확정하며 원정 중 명부 변경으로 다시 작성하지 않는 참가 증서. */
USTRUCT()
struct FFableExpeditionTicket
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid ExpeditionId;

    UPROPERTY()
    FGuid PartyId;

    UPROPERTY()
    TArray<FString> MemberKeys;

    UPROPERTY()
    EFableRegion Region = EFableRegion::RootRuins;

    UPROPERTY()
    EFableDifficulty Difficulty = EFableDifficulty::Normal;

    UPROPERTY()
    int32 PreparationRevision = 0;

    /** 초기화되지 않은 증서와 종료된 원정을 구분한다. */
    bool IsValid() const
    {
        return ExpeditionId.IsValid() && PartyId.IsValid() && !MemberKeys.IsEmpty();
    }

    /** 접속 ID가 아닌 서버 발급 불변 키로 참가자를 확인한다. */
    bool Contains(const FString& Key) const
    {
        return IsValid() && !Key.IsEmpty() && MemberKeys.Contains(Key);
    }
};
