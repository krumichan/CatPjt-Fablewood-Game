#pragma once

#include "CoreMinimal.h"
#include "FableAdventureTypes.h"

class AFableGameState;
class APlayerState;
class AActor;
class UWorld;

/** 어느 진행이 선택되었는지와 좌표·시계·참가자 조회를 묶은 읽기 전용 참조. */
struct FABLEWOOD_API FFableAdventureView
{
    /** 아직 복제되지 않은 원정은 유효한 진행으로 취급하지 않는다. */
    explicit operator bool() const;

    /** 기존 진행 필드 이름으로 읽되 소유 Actor 밖에서 수정하지 않는다. */
    const FFableAdventureProgress* operator->() const;

    /** 필드 또는 확정된 원정의 현재 플레이어만 반환한다. */
    TArray<APlayerState*> Players() const;

    /** 모든 원정은 단 하나의 루트 GameState 서버 시계를 사용한다. */
    double ServerTime() const;

    /** 이 진행의 구조 대기 시간만 계산한다. */
    float RecoveryRemaining() const;

    /** 공유 마을은 유지하고 원정 지역의 원본 좌표만 평행 이동한다. */
    FVector ToWorld(EFableRegion Region, FVector Canonical) const;

    /** 현재 원정의 월드 좌표를 원본 지역 좌표로 되돌린다. */
    FVector ToCanonical(EFableRegion Region, FVector World) const;

    /** 다른 원정의 같은 지역 좌표를 현재 지역으로 오인하지 않는다. */
    bool IsInRegion(EFableRegion Region, FVector World) const;

    /** 필드는 빈 UUID, 원정은 불변 UUID를 반환한다. */
    FGuid ExpeditionId() const;

    /** 원정 지형의 평행 이동 원점을 반환한다. */
    FVector WorldOrigin() const;

    /** 진행 복제의 실제 소유 Actor를 반환한다. */
    AActor* ReplicationOwner() const;

    /** resolver만 원본 진행과 소유 Actor를 연결한다. */
    FFableAdventureView(const FFableAdventureProgress* InProgress, AActor* InOwner, AFableGameState* InClock,
                        FGuid InExpeditionId, FVector InOrigin, EFableRegion InRegion);

    /** 조회 실패는 필드 대신 빈 참조를 반환한다. */
    FFableAdventureView() = default;

private:
    const FFableAdventureProgress* Progress = nullptr;
    TWeakObjectPtr<AActor> OwnerActor;
    TWeakObjectPtr<AFableGameState> Clock;
    FGuid ScopeId;
    FVector Origin = FVector::ZeroVector;
    EFableRegion ScopeRegion = EFableRegion::Village;
};

namespace FableAdventure
{
    /** 일반 컨트롤러·프로필·캐릭터·전투 Actor의 정확한 진행을 선택한다. */
    FABLEWOOD_API FFableAdventureView For(const AActor* Context);

    /** 루트의 공유 필드 진행을 의도적으로 조회하는 별도 경로. */
    FABLEWOOD_API FFableAdventureView Field(const UWorld* World);

    /** 미해결 UUID는 빈 필드 UUID와 같다고 취급하지 않는다. */
    FABLEWOOD_API bool SameScope(const AActor* A, const AActor* B);

    /** 최종 피해·효과 적용 전에 현재 소유 범위와 지역을 함께 검사한다. */
    FABLEWOOD_API bool CanAffect(const AActor* Source, const AActor* Target);
}
