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
namespace {
struct FPrivateBackendConfig {bool Tried=false,Valid=false;FString Url,Id,Key;};
FPrivateBackendConfig& Config()
{
    static FPrivateBackendConfig C;
    if(C.Tried)return C;
    C.Tried=true;
    FString Path,Text;TSharedPtr<FJsonObject> J;
    if(!FParse::Value(FCommandLine::Get(),TEXT("FableBackendConfig="),Path)
        ||!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J.IsValid())return C;
    if(!J->TryGetStringField(TEXT("url"),C.Url)||!J->TryGetStringField(TEXT("serverId"),C.Id)||!J->TryGetStringField(TEXT("serverKey"),C.Key))return C;
    C.Url.RemoveFromEnd(TEXT("/"));
    const bool SafeUrl=C.Url.StartsWith(TEXT("https://"))||C.Url.StartsWith(TEXT("http://127.0.0.1:"))||C.Url.StartsWith(TEXT("http://[::1]:"));
    C.Valid=SafeUrl&&!C.Id.IsEmpty()&&C.Id.Len()<=64&&C.Key.Len()>=32&&C.Key.Len()<=512&&!C.Key.Contains(TEXT("\r"))&&!C.Key.Contains(TEXT("\n"));
    return C;
}
}
bool FFableBackend::IsConfigured(){return Config().Valid;}
FString FFableBackend::ServerId(){return Config().Id;}
void FFableBackend::Post(UWorld* World,const FString& Route,const TSharedRef<FJsonObject>& Body,FReply Reply)
{
    if(!IsValid(World)||World->GetNetMode()!=NM_DedicatedServer||!Config().Valid)
    {Reply(false,0,nullptr);return;}
    FString Text;FJsonSerializer::Serialize(Body,TJsonWriterFactory<>::Create(&Text));
    TSharedRef<IHttpRequest,ESPMode::ThreadSafe> R=FHttpModule::Get().CreateRequest();
    R->SetURL(Config().Url+Route);R->SetVerb(TEXT("POST"));R->SetTimeout(8.0f);
    R->SetHeader(TEXT("Content-Type"),TEXT("application/json"));R->SetHeader(TEXT("X-Fable-Server-Key"),Config().Key);R->SetContentAsString(Text);
    R->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
    auto Completed=MakeShared<bool>(false);
    R->OnProcessRequestComplete().BindLambda([Done=MoveTemp(Reply),Completed](FHttpRequestPtr Request,FHttpResponsePtr Response,bool Connected) mutable {
        if(*Completed)return;*Completed=true;
        TSharedPtr<FJsonObject> J;const int32 Status=Response.IsValid()?Response->GetResponseCode():0;
        if(Response.IsValid())FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()),J);
        Done(Connected&&Status>=200&&Status<300&&J.IsValid(),Status,J);
    });
    if(!R->ProcessRequest())
    {
        // The HTTP module invokes failure completion for requests it accepted. An
        // unstarted request needs explicit delivery through its bound delegate.
        R->OnProcessRequestComplete().ExecuteIfBound(R,nullptr,false);
    }
}
