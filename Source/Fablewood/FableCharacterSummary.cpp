#include "FableCharacterSummary.h"
#include "FableSaveGame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
bool ExactNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Expected)
{
    if (!Object.IsValid()) return false;
    const auto* Value=Object->Values.Find(Field);
    return Value && Value->IsValid() && (*Value)->Type==EJson::Number
        && FMath::IsFinite((*Value)->AsNumber()) && (*Value)->AsNumber()==Expected;
}
}
TSharedPtr<FJsonObject> FableCharacterSummary::MakeSummary(const UFableSaveGame* Profile)
{
    if (!Profile || Profile->Version!=9 || !Profile->IsValidProfile()
        || !Profile->ClassProgress.IsValidIndex(int32(Profile->HeroClass))) return nullptr;
    auto Result=MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("version"),1);
    Result->SetNumberField(TEXT("heroClass"),int32(Profile->HeroClass));
    Result->SetNumberField(TEXT("level"),Profile->Level);
    Result->SetNumberField(TEXT("experience"),Profile->Experience);
    Result->SetNumberField(TEXT("classTier"),Profile->ClassProgress[int32(Profile->HeroClass)].Tier);
    return Result;
}
bool FableCharacterSummary::Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Summary)
{
    const auto Expected=MakeSummary(Profile);
    if (!Expected.IsValid() || !Summary.IsValid() || Summary->Values.Num()!=5) return false;
    const TCHAR* Fields[]={TEXT("version"),TEXT("heroClass"),TEXT("level"),TEXT("experience"),TEXT("classTier")};
    for (const TCHAR* Field:Fields)
        if (!ExactNumber(Summary,Field,Expected->GetNumberField(Field))) return false;
    return true;
}
bool FableCharacterSummary::IsCommitAcknowledgement(const TSharedPtr<FJsonObject>& Response, int64 ExpectedRevision, const FString& ExpectedHash)
{
    if (ExpectedRevision<1 || ExpectedRevision>9007199254740991LL || ExpectedHash.Len()!=64
        || !ExactNumber(Response,TEXT("revision"),double(ExpectedRevision))
        || !ExactNumber(Response,TEXT("inventoryVersion"),1)
        || !ExactNumber(Response,TEXT("summaryVersion"),1)) return false;
    const auto* Hash=Response->Values.Find(TEXT("profileHash"));
    return Hash && Hash->IsValid() && (*Hash)->Type==EJson::String
        && (*Hash)->AsString().Equals(ExpectedHash,ESearchCase::IgnoreCase);
}
