#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FableAdventureTypes.h"
#include "FableGameState.generated.h"

/** 파티와 전투 진행의 공유 상태를 클라이언트에 복제하는 게임 상태. */
UCLASS()
class FABLEWOOD_API AFableGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    UPROPERTY(Replicated, BlueprintReadOnly)
    bool bFrontEnd = false;
    UPROPERTY(Replicated, BlueprintReadOnly)
    FFableAdventureProgress FieldProgress;

    /** 파티 복구까지 남은 시간 값을 반환한다. */
    float GetPartyRecoveryTimeRemaining() const;

    /** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
