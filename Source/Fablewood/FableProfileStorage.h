#pragma once

#include "CoreMinimal.h"

class UFableSaveGame;

/** 로컬 저장의 원본 백업과 읽기 검증을 담당한다. 디스크 원자성을 보장하지 않는다. */
namespace FableProfileStorage
{
    /** 미변환 원본 bytes를 읽고 별도 검증 후보만 반환한다. */
    FABLEWOOD_API UFableSaveGame* ReadCandidate(const FString& Slot, TArray<uint8>& OriginalBytes);

    /** continue index 확정은 메모리 이전 후보가 아닌 실제 현재 파일만 읽는다. */
    FABLEWOOD_API UFableSaveGame* ReadVerifiedCurrent(const FString& Slot);

    /** 이전 저장은 원본 backup을 먼저 확인하고, 현재 저장도 기록 후 다시 읽어 검사한다. */
    FABLEWOOD_API bool WriteVerified(const FString& Slot, const UFableSaveGame* Candidate);

    /** 백업 이름과 저장 확인에 사용하는 raw bytes SHA-256. */
    FABLEWOOD_API FString HashBytes(const TArray<uint8>& Bytes);
}
