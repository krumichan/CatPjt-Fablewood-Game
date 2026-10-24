#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FableFrontEndValidationDriver.generated.h"

/** 시작 화면의 일반 입력과 관찰 결과로 자동 검증을 진행하는 컴포넌트. */
UCLASS()
class FABLEWOOD_API UFableFrontEndValidationDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    UFableFrontEndValidationDriver();

    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;

private:
    FString Role, RunId, Directory;

    /** 현재 검증 상태와 수집한 근거를 보고서로 기록한다. */
    void Report(const FString& Status, const FString& Reason);

    /** 현재 검증 단계의 화면을 캡처한다. */
    void Capture(const FString& Name);
};
