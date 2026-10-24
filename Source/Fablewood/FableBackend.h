#pragma once
#include "CoreMinimal.h"
class FJsonObject;
class UWorld;
/** Only the authority process reads the private server credential; never replicated. */
/** 게임 서버와 영구 저장 API 사이의 인증된 HTTP 통신 경계. */
struct FFableBackend
{
    using FReply = TFunction<void(bool, int32, TSharedPtr<FJsonObject>)>;

    /** 설정 완료 여부를 확인한다. */
    static bool IsConfigured();

    /** 백엔드 설정이 전용 서버 실행에 필요한 조건을 충족하는지 확인한다. */
    static bool CanRunDedicated()
    {
        return IsConfigured();
    }

    /** 비공개 런타임 설정에서 게임 서버 식별자를 반환한다. */
    static FString ServerId();

    /** 설정된 백엔드에 인증된 JSON 요청을 보내고 응답을 전달한다. */
    static void Post(UWorld* World, const FString& Route, const TSharedRef<FJsonObject>& Body, FReply Reply);
};
