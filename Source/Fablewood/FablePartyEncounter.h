#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableAdventureTypes.h"
#include "FableExpeditionTypes.h"
#include "TimerManager.h"
#include "FablePartyEncounter.generated.h"

class AFableEnemy;
class AFablePlayerState;
class APlayerController;

/** 하나의 파티 원정에 속한 진행·전투·지형·구조 수명을 소유한다. */
UCLASS()
class FABLEWOOD_API AFablePartyEncounter : public AActor
{
    GENERATED_BODY()

public:
    /** 원정 진행을 별도 Actor로 복제한다. */
    AFablePartyEncounter();

    /** 초기화된 원정만 생존 검사 타이머를 실행한다. */
    virtual void BeginPlay() override;

    /** 자기 타이머와 전투·지형 Actor만 정리한다. */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    /** BeginPlay 전 실패한 입장도 생성한 배경을 즉시 정리한다. */
    virtual void Destroyed() override;

    /** 불변 증서와 진행·원점을 클라이언트에 전달한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 생성 완료 전 서버가 원정 증서와 원점을 한 번만 설정한다. */
    bool InitializeExpedition(const FFableExpeditionTicket& InTicket, FVector InOrigin);

    /** 전원 위치 선검사·이동·도착 확인 뒤 첫 전투를 확정한다. */
    bool Admit(const TArray<APlayerController*>& Players);

    /** 자기 증서에 속한 현재 연결만 다시 입장시킨다. */
    bool Resume(APlayerController* Requester);

    /** 자기 봉인만 진행시키고 개인 조사를 기록한다. */
    void RequestCleanseSeal(APlayerController* Requester, int32 SealIndex);

    /** 자기 참가자가 요청한 완료 귀환만 처리한다. */
    void RequestReturnVillage(APlayerController* Requester);

    /** 처치·보스 체력 이벤트는 적 UUID로 라우팅된다. */
    void HandleEnemyDefeated(AFableEnemy* Enemy);

    /** 자기 보스 상태만 변경한다. */
    void UpdateBossHealth(AFableEnemy* Enemy);

    /** 재접속은 유효한 명부와 증서가 같은 참가자만 복구한다. */
    bool IsParticipant(const AFablePlayerState* Profile) const;

    /** 불변 원정 증서를 읽는다. */
    const FFableExpeditionTicket& GetTicket() const;

    /** 원정 진행 정본을 읽는다. */
    const FFableAdventureProgress& GetProgress() const;

    /** 원정 UUID를 읽는다. */
    FGuid GetExpeditionId() const;

    /** 지형 원점을 읽는다. */
    FVector GetWorldOrigin() const;

    /** 이동이 모두 성공한 확정 원정인지 확인한다. */
    bool IsCommitted() const;

    /** 실패 중인 귀환이 재접속자를 던전으로 보내지 않도록 확인한다. */
    bool IsReturning() const;

    /** 현재 접속한 정확한 참가자만 반환한다. */
    TArray<APlayerController*> GetParticipants() const;

private:
    friend struct FFablePartyTestAccess;

    /** 현재 참가자가 생존해 요청할 수 있는지 확인한다. */
    bool IsRequesterAlive(APlayerController* Requester) const;

    /** 원정의 실제 서비스 위치와 요청자의 거리를 확인한다. */
    bool CanUseTownService(APlayerController* Requester, FVector Location, float Radius = 330) const;

    /** 지역별 배경은 최종 원점으로 지연 생성한다. */
    bool CreateEnvironment();

    /** 회복 없는 전원 이동이 실패하면 원래 위치를 복원한다. */
    bool MoveAdmissionParticipants(const TArray<APlayerController*>& Players);

    /** 참가자 한 명의 체크포인트 이동을 실제 도착까지 확인한다. */
    bool MovePlayerToCheckpoint(APlayerController* Player, EFableRegion Region, bool bRestore = true);

    /** 연결된 참가자를 이동하고 부분 실패는 재시도 상태로 남긴다. */
    bool MoveParty(EFableRegion Region);

    /** 해당 원정 참가자에게만 안내한다. */
    void NotifyParty(const FString& Notice) const;

    /** 자기 적과 투사체만 정리한다. */
    void ClearEncounter();

    /** 전멸 시 자기 플레이어의 잔여 투사체도 정리한다. */
    void ClearRecoveryCombat();

    /** 유적의 지정 웨이브를 생성한다. */
    void SpawnDungeonWave(int32 Wave);

    /** 달샘의 지정 봉인 무리를 생성한다. */
    void SpawnMoonSealGroup(int32 SealIndex);

    /** 선택한 지역의 보스를 생성한다. */
    void SpawnBoss();

    /** 원정의 현재 전투 상태를 확인한다. */
    bool IsDungeonRunning() const;

    /** 해당 원정에 재도전할 의뢰가 남아 있는지 확인한다. */
    bool HasRetryQuest() const;

    /** 전멸 뒤 3초 구조 대기를 시작한다. */
    void BeginPartyRecovery();

    /** 구조 완료 후 binding과 다음 준비를 정리한다. */
    void FinishPartyRecovery();

    /** 현재 참가자만 생존·개별 구조·전원 오프라인 유예를 검사한다. */
    void CheckPartyState();

    /** 완료 또는 구조에 따른 참가자 귀환을 진행한다. */
    void ReturnPartyToVillage(bool bWipe);

    /** 원정 종료 시 명부 동의와 참가자 binding을 해제한다. */
    void FinishExpedition();

    UPROPERTY(Replicated)
    FFableExpeditionTicket Ticket;

    UPROPERTY(Replicated)
    FFableAdventureProgress Progress;

    UPROPERTY(Replicated)
    FVector WorldOrigin = FVector::ZeroVector;

    UPROPERTY(Replicated)
    bool bCommitted = false;

    UPROPERTY()
    TObjectPtr<AActor> Environment;

    TSet<TWeakObjectPtr<AFableEnemy>> ActiveEnemies;
    TSet<TWeakObjectPtr<AFableEnemy>> SpawnedEncounterEnemies;
    TWeakObjectPtr<AFableEnemy> ActiveBoss;
    FTimerHandle EncounterTimer;
    FTimerHandle PartyMonitor;
    bool bAdmissionPending = false;
    bool bReturnPending = false;
    bool bReturnAfterWipe = false;
    bool bRetryAfterRecovery = false;
    double NextRecoveryAttemptTime = 0;
    double NextIndividualRescueTime = 0;
};
