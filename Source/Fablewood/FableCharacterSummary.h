#pragma once
#include "CoreMinimal.h"
class UFableSaveGame;
class FJsonObject;

/** The public character card is derived from the same validated snapshot as the opaque profile. */
namespace FableCharacterSummary
{
    /** 검증된 저장 프로필에서 백엔드 캐릭터 요약을 만든다. */
    TSharedPtr<FJsonObject> MakeSummary(const UFableSaveGame* Profile);

    /** 전달된 저장 계약이 현재 프로필의 투영 결과와 같은지 검사한다. */
    bool Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Summary);

    /** 저장 응답의 리비전과 해시가 요청한 상태를 확인하는지 검사한다. */
    bool IsCommitAcknowledgement(const TSharedPtr<FJsonObject>& Response, int64 ExpectedRevision,
                                 const FString& ExpectedHash);
}
