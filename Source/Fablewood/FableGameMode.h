#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "FableRPGTypes.h"
#include "FableExpeditionTypes.h"
#include "FableExpeditionResultTypes.h"
#include "FableGameMode.generated.h"

class AFableEnemy;
class AFableDungeonWorld;
class AFableMoonwellWorld;
class AFablePlayerState;
class AFablePartyEncounter;

/** UObject 참조 없이 개인 결과와 저장 경계를 서버 수명 동안 보존한다. */
struct FFableCharacterExpeditionLedger
{
    TMap<FGuid, FFablePersonalExpeditionResult> Results;
    TArray<FGuid> Order;
    TMap<FString, int64> SnapshotGrantSequences;
    int64 LastGrantSequence = 0;
    int64 LastCommittedGrantSequence = 0;
};

/** 서버의 접속·파티 이동·던전·보스·복구 진행을 관리하는 게임 규칙. */
UCLASS()
class FABLEWOOD_API AFableGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    /** Native 독립 월드에서도 유일한 서버 정본만 찾는다. */
    static AFableGameMode* Find(const UWorld* World);

    /** 본인의 원정 결과만 읽으며 조회 자체는 지급하지 않는다. */
    const FFablePersonalExpeditionResult* GetResultByExpedition(const AFablePlayerState* Recipient,
                                                                FGuid ExpeditionId) const;

    /** 가장 최근 종료 결과를 본인이 확인했는지 읽는다. 결과가 없으면 허용한다. */
    bool HasAcknowledgedLatestExpeditionResult(const AFablePlayerState* Recipient) const;

    /** 원하는 개인 결과를 소유 연결에 다시 전달한다. */
    bool RequestExpeditionResult(APlayerController* Requester, FGuid ExpeditionId);

    /** 정확한 종료 결과를 읽었음을 표시하며 영구 저장 확인과 구분한다. */
    bool AcknowledgeExpeditionResult(APlayerController* Requester, FGuid ExpeditionId, FGuid CompletionId,
                                     int32 ExpectedResultRevision);

    /** 이전 결과와 새 명시적 준비를 묶어 한 번만 새로운 증서를 발급한다. */
    bool RequestRetryExpedition(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision,
                                FGuid OldExpeditionId, FGuid CompletionId);

    /** 재도전 정본·현재 명부와 정확한 이전 결과를 검사한다. */
    bool CanPrepareExpeditionRetry(APlayerController* Requester, FGuid PartyId, FGuid OldExpeditionId,
                                   FGuid CompletionId) const;

    /** 준비 중인 기존 완료 원정의 참가자 위치를 허용할지 확인한다. */
    bool IsRetryPreparationParticipant(const AFablePlayerState* Profile, FGuid OldExpeditionId,
                                       FGuid CompletionId) const;

    /** 실제 원정의 실제 처치에 대해서만 개인 보상을 한 번 기록한다. */
    bool RecordExpeditionDefeat(AFablePartyEncounter* Encounter, AFableEnemy* Enemy, AFablePlayerState* Recipient);

    /** 원정 종료 ID와 결과를 서버에서 한 번만 확정한다. */
    void CompleteExpeditionResult(AFablePartyEncounter* Encounter, EFableExpeditionOutcome Outcome);

    /** 인증된 재접속자에게 기존 개인 결과를 복원한다. */
    void RestoreExpeditionResult(AFablePlayerState* Profile);

    /** 직렬화 스냅숏이 포함하는 개인 지급 순번을 읽는다. */
    int64 GetExpeditionGrantSequence(const FString& PublicKey) const;

    /** 실제 직렬화한 해시와 그 스냅숏의 지급 경계를 등록한다. */
    void TrackExpeditionSaveSnapshot(const FString& PublicKey, int64 IncludedGrantSequence, const FString& Hash);

    /** 검증된 동일 저장 배치의 응답으로 포함된 영수증만 저장 확정한다. */
    void NotifyExpeditionSaveCommitted(const FString& PublicKey, int64 IncludedGrantSequence, int64 BackendRevision,
                                       const FString& ValidatedHash);

    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableGameMode();

    /** 월드 초기화 옵션에서 시작 화면·온라인 모드와 접속 설정을 준비한다. */
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

    /** 시작 화면 여부를 확인한다. */
    bool IsFrontEnd() const
    {
        return bFrontEnd;
    }

    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 월드 이탈 시 등록한 이벤트와 진행 중인 작업을 정리한다. */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** 접속을 허용하기 전에 기본 연결 조건을 검사한다. */
    virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId,
                          FString& ErrorMessage) override;

    /** 로그인 요청을 처리하고 필요한 플레이어 연결 상태를 준비한다. */
    virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
                                     const FString& Options, const FUniqueNetIdRepl& UniqueId,
                                     FString& ErrorMessage) override;

    /** 새 연결을 파티와 백엔드 프로필 초기화 절차에 연결한다. */
    virtual void PostLogin(APlayerController* NewPlayer) override;

    /** 접속 종료에 따른 세션과 소유 상태의 정리를 처리한다. */
    virtual void Logout(AController* Exiting) override;

    /** 플레이어를 현재 진행에 맞는 시작 지점에 생성한다. */
    virtual void RestartPlayer(AController* NewPlayer) override;

    /** 백엔드 인증 결과를 확인해 플레이어의 게임 진입을 완료한다. */
    void CompleteBackendLogin(APlayerController* Player, bool bSuccess, const FString& Reason);

    /** 적 처치에 따른 보상·웨이브·보스 완료 상태를 처리한다. */
    void HandleEnemyDefeated(AFableEnemy* Enemy);

    /** 보스 체력 상태를 갱신한다. */
    void UpdateBossHealth(AFableEnemy* Enemy);

    /** 새 전투 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestNewRound(APlayerController* Requester);

    /** 전투 재도전 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestRetryEncounter(APlayerController* Requester);

    /** 숲 전투 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestForestEncounter(APlayerController* Requester);

    /** 던전 입장 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestEnterDungeon(APlayerController* Requester);

    /** 지역 입장 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestEnterRegion(APlayerController* Requester, EFableRegion Region,
                            EFableDifficulty Difficulty = EFableDifficulty::Normal);

    /** 봉인 정화 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestCleanseSeal(APlayerController* Requester, int32 SealIndex);

    /** 마을 복귀 요청의 조건을 확인하고 해당 동작을 진행한다. */
    void RequestReturnVillage(APlayerController* Requester);

    /** 살아 있는 요청자가 마을 서비스 범위 안에 있는지 확인한다. */
    bool CanUseTownService(APlayerController* Requester, FVector Location, float Radius = 300.0f) const;

    /** 파티 UUID와 준비 리비전으로 전원 입장을 서버에서 원자적으로 확정한다. */
    bool RequestStartExpedition(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision);

    /** 실제 프로필의 참가 증서에 해당하는 원정을 찾는다. */
    AFablePartyEncounter* GetExpeditionFor(const AFablePlayerState* Profile) const;

    /** 불변 UUID가 일치하는 확정 원정을 찾는다. */
    AFablePartyEncounter* FindExpedition(FGuid Id) const;

    /** 현재 살아 있는 확정 원정의 목록을 반환한다. */
    TArray<AFablePartyEncounter*> GetExpeditions() const;

    /** 명부와 실제 플레이어 binding이 같은 원정 참가자를 나타내는지 확인한다. */
    bool IsExpeditionParticipant(const AFablePlayerState* Profile) const;

    /** 마을의 다른 파티 편집을 막지 않고 해당 참가 파티만 잠근다. */
    bool IsPartyExpeditionActive(FGuid PartyId) const;

    /** 인증된 본인이 게시판에서 현재 공개 숲 실행에 참가한다. */
    bool JoinPublicField(APlayerController* Requester);

    /** 공개 필드 참가만 해제하며 원정 증서와 물리적 마을은 바꾸지 않는다. */
    bool LeavePublicField(APlayerController* Requester);

    /** 현재 공개 필드 실행 세대를 읽는다. 미생성 시 빈 값이다. */
    FGuid GetPublicFieldScopeId() const;

private:
    friend struct FFablePartyTestAccess;
    friend struct FFableTownTestAccess;
    friend class AFablePartyEncounter;

    /** 신규 원정과 완료 원정 교체의 입장을 같은 원자적 경계로 처리한다. */
    bool StartPreparedExpedition(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision,
                                 AFablePartyEncounter* ReplacedEncounter = nullptr);

    /** 현재 연결이 개인 결과를 읽을 권한이 있는지 확인한다. */
    bool OwnsResultProfile(const AFablePlayerState* Profile) const;

    /** 미확인·미저장 결과를 보존하면서 안전하게 새 원정 공간을 확보한다. */
    bool EnsureResultCapacity(const FFableExpeditionTicket& Ticket);

    /** 입장 성공 때 원래 증서의 개인 결과를 전원 생성한다. */
    void RegisterExpeditionResults(const FFableExpeditionTicket& Ticket);

    /** 개인 결과의 저장 상태와 버전을 갱신해 소유 연결에 전달한다. */
    void PublishExpeditionResult(FFablePersonalExpeditionResult& Result);

    TMap<FString, FFableCharacterExpeditionLedger> ExpeditionResults;

    /** 현재 실행 세대에 서버가 등록한 공개 필드 참가자만 조회한다. */
    TArray<APlayerController*> GetEncounterPlayers() const;

    /** 참가 증서의 모든 플레이어와 입장 조건을 다시 검사한다. */
    bool ValidateAdmission(const FFableExpeditionTicket& Ticket, TArray<APlayerController*>& OutPlayers,
                           const AFablePartyEncounter* ReplacedEncounter = nullptr) const;

    /** 현재 연결이 차지한 파티 슬롯 값을 반환한다. */
    int32 GetConnectionSlot(AController* Player) const;

    /** 백엔드 접속 실패를 연결에 알리고 대기 상태를 해제한다. */
    void RejectBackendLogin(APlayerController* Player, const FString& Reason);

    /** 백엔드 프로필을 기다리는 연결의 완료·만료 상태를 확인한다. */
    void CheckBackendLogins();

    /** 현재 지역과 난이도에 맞는 적 전투를 생성한다. */
    void SpawnEncounter();

    /** 현재 전투가 소유한 적과 진행 상태를 정리한다. */
    void ClearEncounter();

    /** 동료들의 생존·지역 상태를 확인해 파티 복구를 판단한다. */
    void CheckPartyState();

    /** 숲 명부 밖의 마을 캐릭터는 본인만 독립적으로 구조한다. */
    void CheckIdleTownRescues();

    /** 파티 전멸 뒤 전투를 정리하고 복구 대기를 시작한다. */
    void BeginPartyRecovery();

    /** 복구 시간이 끝난 파티를 안전 지점으로 되돌린다. */
    void FinishPartyRecovery();

    /** 복구 전투 상태를 해제한다. */
    void ClearRecoveryCombat();

    /** 재도전 퀘스트의 존재 여부를 확인한다. */
    bool HasRetryQuest(EFableRegion Region) const;

    /** 현재 전투를 마무리하고 파티를 마을로 복귀시킨다. */
    void ReturnPartyToVillage(bool bWipe);

    /** 접속한 파티원들에게 같은 진행 안내를 전달한다. */
    void NotifyParty(const FString& Notice) const;

    /** 연결된 모든 파티원이 선택 지역의 입장 조건을 만족하는지 확인한다. */
    bool AllPlayersRegionEligible(EFableRegion Region) const;

    /** 요청자 생존 여부를 확인한다. */
    bool IsRequesterAlive(APlayerController* Requester) const;

    /** 파티 구성원을 해당 지역의 서버 체크포인트로 이동시킨다. */
    bool MoveParty(EFableRegion Region);

    /** 플레이어를 서버가 정한 지역 체크포인트로 이동시킨다. */
    bool MovePlayerToCheckpoint(AController* Player, EFableRegion Region, int32 Slot);

    TSet<TWeakObjectPtr<AFableEnemy>> ActiveEnemies;
    // 처치된 표시 액터까지 해당 전투가 직접 정리하며 다른 지역의 적을 지우지 않는다.
    TSet<TWeakObjectPtr<AFableEnemy>> SpawnedEncounterEnemies;
    TWeakObjectPtr<AFableEnemy> ActiveBoss;
    UPROPERTY()
    TObjectPtr<AFableDungeonWorld> DungeonWorld;
    UPROPERTY()
    TObjectPtr<AFableMoonwellWorld> MoonwellWorld;
    FTimerHandle EncounterTimer;
    FTimerHandle PartyMonitor;
    double LastRestartTime = -100.0;
    bool bDedicatedMode = false;
    TMap<TWeakObjectPtr<APlayerController>, int32> ConnectionSlots;
    TMap<TWeakObjectPtr<APlayerController>, FString> PendingTickets;
    TMap<TWeakObjectPtr<APlayerController>, double> PendingLoginTimes;
    FTimerHandle BackendLoginMonitor;
    bool bLegacyMode = false;
    bool bFrontEnd = false;
    bool bReturnPending = false;
    bool bReturnAfterWipe = false;
    bool bRetryAfterRecovery = false;
    double NextRecoveryAttemptTime = 0.0;
    double NextIndividualRescueTime = 0.0;
    UPROPERTY()
    TMap<FGuid, TObjectPtr<AFablePartyEncounter>> Expeditions;

    TSet<FGuid> PendingAdmissions;
};
