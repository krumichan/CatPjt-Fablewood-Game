#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FableFrontEndValidationDriver.generated.h"
UCLASS()
class FABLEWOOD_API UFableFrontEndValidationDriver : public UActorComponent
{
    GENERATED_BODY()
public:
    UFableFrontEndValidationDriver();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function) override;
private:
    FString Role,RunId,Directory;
    void Report(const FString& Status,const FString& Reason);
    void Capture(const FString& Name);
};
