#pragma once

#include "CoreMinimal.h"
#include "FableTownServiceTypes.h"

class AFablePlayerController;
class UFableOnlineValidationDriver;

/** 정상 RPC를 읽기 전용으로 감싼다. 관측 ID는 요청이나 권한에 전달하지 않는다. / 観測専用。 */
class FFableScopedServiceValidationObservation
{
public:
    FFableScopedServiceValidationObservation(AFablePlayerController* Player, const TCHAR* Action,
                                             EFableTownServiceId Service);
    ~FFableScopedServiceValidationObservation();

    void AcceptRpgGate();
    void SetService(EFableTownServiceId Service);

private:
    TWeakObjectPtr<UFableOnlineValidationDriver> Driver;
    int32 Observation = INDEX_NONE;
};
