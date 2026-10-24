#include "FableCharacterSummary.h"
#include "FableSaveGame.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    /** JSON 수치가 기대한 정수 값과 정확히 같은지 검사한다. */
    bool ExactNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Expected)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!Object.IsValid())
        {
            return false;
        }

        const auto* Value = Object->Values.Find(Field);

        return Value && Value->IsValid() && (*Value)->Type == EJson::Number && FMath::IsFinite((*Value)->AsNumber()) &&
               (*Value)->AsNumber() == Expected;
    }
}

/** 검증된 저장 프로필에서 백엔드 캐릭터 요약을 만든다. */
TSharedPtr<FJsonObject> FableCharacterSummary::MakeSummary(const UFableSaveGame* Profile)
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!Profile || Profile->Version != 9 || !Profile->IsValidProfile() ||
        !Profile->ClassProgress.IsValidIndex(int32(Profile->HeroClass)))
    {
        return nullptr;
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("version"), 1);
    Result->SetNumberField(TEXT("heroClass"), int32(Profile->HeroClass));
    Result->SetNumberField(TEXT("level"), Profile->Level);
    Result->SetNumberField(TEXT("experience"), Profile->Experience);
    Result->SetNumberField(TEXT("classTier"), Profile->ClassProgress[int32(Profile->HeroClass)].Tier);

    return Result;
}

/** 전달된 저장 계약이 현재 프로필의 투영 결과와 같은지 검사한다. */
bool FableCharacterSummary::Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Summary)
{
    const auto Expected = MakeSummary(Profile);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Expected.IsValid() || !Summary.IsValid() || Summary->Values.Num() != 5)
    {
        return false;
    }

    const TCHAR* Fields[] = {TEXT("version"), TEXT("heroClass"), TEXT("level"), TEXT("experience"), TEXT("classTier")};

    // 같은 규칙을 적용할 대상을 순회한다.
    for (const TCHAR* Field : Fields)
    {
        if (!ExactNumber(Summary, Field, Expected->GetNumberField(Field)))
        {
            return false;
        }
    }

    return true;
}

/** 저장 응답의 리비전과 해시가 요청한 상태를 확인하는지 검사한다. */
bool FableCharacterSummary::IsCommitAcknowledgement(const TSharedPtr<FJsonObject>& Response, int64 ExpectedRevision,
                                                    const FString& ExpectedHash)
{
    // 접속 권한·저장 리비전·멱등 키 조건 검사
    if (ExpectedRevision < 1 || ExpectedRevision > 9007199254740991LL || ExpectedHash.Len() != 64 ||
        !ExactNumber(Response, TEXT("revision"), double(ExpectedRevision)) ||
        !ExactNumber(Response, TEXT("inventoryVersion"), 1) || !ExactNumber(Response, TEXT("summaryVersion"), 1))
    {
        return false;
    }

    const auto* Hash = Response->Values.Find(TEXT("profileHash"));

    return Hash && Hash->IsValid() && (*Hash)->Type == EJson::String &&
           (*Hash)->AsString().Equals(ExpectedHash, ESearchCase::IgnoreCase);
}
