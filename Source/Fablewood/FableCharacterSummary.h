#pragma once
#include "CoreMinimal.h"
class UFableSaveGame;
class FJsonObject;

/** The public character card is derived from the same validated snapshot as the opaque profile. */
namespace FableCharacterSummary
{
    TSharedPtr<FJsonObject> MakeSummary(const UFableSaveGame* Profile);
    bool Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Summary);
    bool IsCommitAcknowledgement(const TSharedPtr<FJsonObject>& Response, int64 ExpectedRevision, const FString& ExpectedHash);
}
