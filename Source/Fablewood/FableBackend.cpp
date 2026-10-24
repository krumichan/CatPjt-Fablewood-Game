#include "FableBackend.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
namespace
{
    /** 런타임에만 읽는 비공개 백엔드 주소와 서버 인증 설정. */
    struct FPrivateBackendConfig
    {
        bool Tried = false, Valid = false;
        FString Url, Id, Key;
    };

    /** 비공개 백엔드 설정을 처음 한 번 읽고 전용 서버 통신에 사용할 설정을 반환한다. */
    FPrivateBackendConfig& Config()
    {
        // 이미 시도한 설정 읽기 결과는 같은 프로세스에서 다시 사용한다.
        static FPrivateBackendConfig C;
        if (C.Tried)
        {
            return C;
        }

        // 명령행의 비공개 설정 파일을 읽고 JSON 형식을 확인한다.
        C.Tried = true;
        FString Path, Text;
        TSharedPtr<FJsonObject> J;
        if (!FParse::Value(FCommandLine::Get(), TEXT("FableBackendConfig="), Path) ||
            !FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid())
        {
            return C;
        }

        // 통신 주소·서버 식별자·인증 키의 필수 필드를 확인한다.
        if (!J->TryGetStringField(TEXT("url"), C.Url) || !J->TryGetStringField(TEXT("serverId"), C.Id) ||
            !J->TryGetStringField(TEXT("serverKey"), C.Key))
        {
            return C;
        }

        // 허용한 접속 주소와 인증 키 길이·개행 조건을 검사한다.
        C.Url.RemoveFromEnd(TEXT("/"));
        const bool SafeUrl = C.Url.StartsWith(TEXT("https://")) || C.Url.StartsWith(TEXT("http://127.0.0.1:")) ||
                             C.Url.StartsWith(TEXT("http://[::1]:"));
        C.Valid = SafeUrl && !C.Id.IsEmpty() && C.Id.Len() <= 64 && C.Key.Len() >= 32 && C.Key.Len() <= 512 &&
                  !C.Key.Contains(TEXT("\r")) && !C.Key.Contains(TEXT("\n"));

        return C;
    }
}

/** 설정 완료 여부를 확인한다. */
bool FFableBackend::IsConfigured()
{
    return Config().Valid;
}

/** 비공개 런타임 설정에서 게임 서버 식별자를 반환한다. */
FString FFableBackend::ServerId()
{
    return Config().Id;
}

/** 설정된 백엔드에 인증된 JSON 요청을 보내고 응답을 전달한다. */
void FFableBackend::Post(UWorld* World, const FString& Route, const TSharedRef<FJsonObject>& Body, FReply Reply)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!IsValid(World) || World->GetNetMode() != NM_DedicatedServer || !Config().Valid)
    {
        Reply(false, 0, nullptr);
        return;
    }

    // JSON 계약과 검증 기록에 필요한 값 준비
    FString Text;
    FJsonSerializer::Serialize(Body, TJsonWriterFactory<>::Create(&Text));
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> R = FHttpModule::Get().CreateRequest();
    R->SetURL(Config().Url + Route);
    R->SetVerb(TEXT("POST"));
    R->SetTimeout(8.0f);
    R->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    R->SetHeader(TEXT("X-Fable-Server-Key"), Config().Key);
    R->SetContentAsString(Text);
    R->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    auto Completed = MakeShared<bool>(false);
    R->OnProcessRequestComplete().BindLambda(
        [Done = MoveTemp(Reply), Completed](FHttpRequestPtr Request, FHttpResponsePtr Response, bool Connected) mutable
        {
            // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
            if (*Completed)
            {
                return;
            }

            // JSON 계약과 검증 기록 처리
            *Completed = true;
            TSharedPtr<FJsonObject> J;
            const int32 Status = Response.IsValid() ? Response->GetResponseCode() : 0;

            // JSON 계약과 검증 기록 처리
            if (Response.IsValid())
            {
                FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), J);
            }

            Done(Connected && Status >= 200 && Status < 300 && J.IsValid(), Status, J);
        });

    // HTTP 요청·응답과 연결 상태 처리
    if (!R->ProcessRequest())
    {
        // The HTTP module invokes failure completion for requests it accepted. An
        // unstarted request needs explicit delivery through its bound delegate.
        R->OnProcessRequestComplete().ExecuteIfBound(R, nullptr, false);
    }
}
