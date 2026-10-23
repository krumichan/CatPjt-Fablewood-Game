#pragma once
#include "CoreMinimal.h"
class FJsonObject;
class UWorld;
/** Only the authority process reads the private server credential; never replicated. */
struct FFableBackend
{
    using FReply = TFunction<void(bool,int32,TSharedPtr<FJsonObject>)>;
    static bool IsConfigured();
    static bool CanRunDedicated() { return IsConfigured(); }
    static FString ServerId();
    static void Post(UWorld* World,const FString& Route,const TSharedRef<FJsonObject>& Body,FReply Reply);
};
