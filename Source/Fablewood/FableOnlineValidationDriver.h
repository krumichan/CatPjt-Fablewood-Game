#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "FableRPGTypes.h"
#include "FableTownTravelTypes.h"
#include "FableTownServiceTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FableOnlineValidationDriver.generated.h"
class FJsonObject;
class FJsonValue;
class AFablePlayerState;
class AFablePlayerController;
class AFableCharacter;
class AFableEnemy;
class UFableTownTravelCoordinator;
class UWorld;
class AFablePartyManager;
class APlayerController;
struct FFablePartyRecord;
/** Opt-in UI workflow observer. Uses real account APIs and normal gameplay inputs. */
/** 온라인의 일반 입력과 관찰 결과로 자동 검증을 진행하는 컴포넌트. */
UCLASS()
class FABLEWOOD_API UFableOnlineValidationDriver : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /** 생성 하위 시스템에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

    /** 온라인 연결 하위 시스템의 초기 상태를 준비한다. */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** 온라인 연결 하위 시스템의 요청과 콜백을 정리한다. */
    virtual void Deinitialize() override;

    /** QA 전용 관측이며 기존 RPC의 판정과 경제 상태를 변경하지 않는다. */
    int32 BeginServiceObservation(AFablePlayerController* Player, const TCHAR* Action, EFableTownServiceId Service);
    void AcceptServiceRpgGate(int32 Index);
    void SetObservedService(int32 Index, EFableTownServiceId Service);
    void EndServiceObservation(int32 Index);

    /** 준비 RPC의 실제 전후 상태와 요청 ID만 관측한다. / 準備RPCの前後と要求IDだけを観測する。 */
    int32 BeginPartyCancellationObservation(AFablePlayerController* Player, FGuid RequestId, const TCHAR* Action,
                                            FGuid PartyId, int32 PreparationRevision, bool bReady);
    void EndPartyCancellationObservation(int32 Index, bool bSuccess);
    void RecordPartyCancellationReply(AFablePlayerController* Player, FGuid RequestId, bool bSuccess,
                                      int32 PreparationRevision, const FString& Notice);

private:
    /** 明示取消は本人の通常UIだけで行い、既存の入場手順へ戻る。 / 명시 취소는 본인 UI로만 실행한다. */
    bool TickPartyReadyCancellation(AFablePlayerController* Player, const AFablePlayerState* Profile,
                                    const FFablePartyRecord& Party, double Now);
    void RecordPartyCancellationArrival(AFablePlayerController* Player, const AFablePlayerState* Profile);
    TSharedRef<FJsonObject> PartyCancellationSnapshot(AFablePlayerController* Player) const;

    bool bPartyReadyCancellation = false, bPartyCancelSent = false;
    int32 PartyCancelStep = 0;
    double PartyCancelStepAt = 0, PartyCancelLastSentAt = -10;
    TSharedPtr<FJsonObject> PartyCancelReport;
    TArray<TSharedPtr<FJsonValue>> PartyCancelAuthority, PartyCancelReplies;
    TArray<TWeakObjectPtr<AFablePlayerController>> PartyCancelCallers;

    /** 현재 UI 길안내와 권한 서버의 공간 판정만 관측한다. / 現在案内とサーバー空間判定の観測のみ。 */
    TSharedRef<FJsonObject> NavigationObservation(const AFablePlayerController* Player) const;

    /** 한 서버 시점의 현재 연결·준비·공간 문맥을 보존한다. / 一時点の現接続・準備・空間文脈を保存する。 */
    TSharedRef<FJsonObject> PartySpatialObservation(UWorld* World, const FFablePartyRecord& Party) const;

    /** 연결과 준비 상태가 변한 관측을 유한 개수만 저장한다. / 接続・準備変化の観測を有界保存する。 */
    void ObserveUnit5PartyServer(UWorld* World);

    /** 동기 공간 판정의 결과만 기록하고 게임 상태를 수정하지 않는다. / 同期空間判定を記録し状態を変更しない。 */
    void OnUnit5DepartureChecked(const FFablePartyRecord& Party, const TArray<APlayerController*>& Players,
                                 bool bSpatialAllowed);

    TArray<TSharedPtr<FJsonValue>> Unit5PartySamples, Unit5DepartureChecks;
    TSet<FString> Unit5PartySignatures;
    TWeakObjectPtr<AFablePartyManager> Unit5ObservedPartyManager;
    FDelegateHandle Unit5DepartureHandle;

    /** 기존 Legacy 입력과 저장 ACK만 사용한다. 새 네 마을은 채택하지 않는다. */
    bool TickUnit4LegacyServices(AFablePlayerController* PC, AFablePlayerState* P, AFableCharacter* Hero, double Now);
    void ObserveUnit4ServicesServer(UWorld* World);
    TSharedRef<FJsonObject> ServiceProfile(const AFablePlayerState* P) const;
    TSharedRef<FJsonObject> ServiceCore(const AFablePlayerState* P) const;
    FString ServiceRole(const AFablePlayerState* P) const;
    void CompleteServiceObservation(int32 Index);

    struct FServiceObservation
    {
        TWeakObjectPtr<AFablePlayerController> Player;
        TWeakObjectPtr<AFablePlayerState> Peer;
        TSharedPtr<FJsonObject> Row;
        EFableTownServiceId Service = EFableTownServiceId::Invalid;
        double Started = 0;
        bool bEnded = false, bPublished = false;
    };

    TArray<FServiceObservation> ServiceObservations;
    TArray<TSharedPtr<FJsonValue>> ServiceCheckpoints, ServiceServerEvents;
    TSharedPtr<FJsonObject> ServiceReport, ServiceServerReport, ServiceClientBefore;
    TSharedPtr<FJsonObject> ServiceForestBefore[2];
    int32 ServiceStep = 0, ServicePhase = 0, ServiceWeaponId = 0, ServiceSequence = 0;
    double ServiceStationaryAt = 0, ServiceSentAt = 0, ServiceForestStartedAt = 0, ServiceForestReleasedAt = 0;
    bool bServiceSent = false, bServiceForestReleased = false, bServiceForestCaptureFailed = false;
    FFableItem ServiceWeaponBefore;

    /** 通常の歩行入力だけで実階段を往復し、再接続は既存Entry復元を観測する。 */
    bool TickFloorWalk(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero, double Now);

    /** 계단 중앙에서는 감속한 일반 입력으로 관성을 줄이며 위치를 직접 변경하지 않는다. */
    bool ApplyFloorWalkInput(AFablePlayerController* PC, AFableCharacter* Hero, const FVector& Destination);

    /** 권한 서버의 현재 pawn·지지면·소유 연결만 읽으며 위치나 저장을 바꾸지 않는다. */
    void ObserveFloorWalkServer(UWorld* World);

    /** 다른 플레이어의 owner-only 연결 정보는 읽거나 출력하지 않는다. */
    TSharedRef<FJsonObject> FloorWalkPawnSnapshot(const AFableCharacter* Hero, const AFablePlayerController* PC,
                                                  bool bIncludeOwnerState) const;

    TSharedPtr<FJsonObject> FloorWalkReport, FloorWalkServerReport;
    TArray<TSharedPtr<FJsonValue>> FloorWalkSamples, FloorWalkServerSamples, FloorWalkMilestones;
    int32 FloorWalkStep = 0, FloorWalkApproachStep = 0, FloorWalkSequence = 0, FloorWalkServerSequence = 0;
    double FloorWalkStarted = 0, FloorWalkStableAt = 0, FloorWalkSampleAt = -1, FloorWalkServerSampleAt = -1;
    double FloorWalkEntryX = 0;

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    bool Tick(float Delta);

    /** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
    void Finish(bool Success, const FString& Reason);

    /** 현재 검증 단계의 화면을 캡처한다. */
    void Capture(const FString& Name);

    /** 현재 검증 상태를 보고서 파일에 기록한다. */
    void Publish(const FString& Status, const FString& Reason = TEXT(""));

    /** 현재 관찰 값과 검증 결과를 JSON 보고서로 직렬화한다. */
    FString SerializeReport(const FString& Status, const FString& Reason) const;

    /** 검증 보고서 값을 기록한다. */
    bool WriteReport(const FString& Bytes) const;

    /** 종료 상태와 검증 결과를 최종 보고서로 기록한다. */
    void PublishTerminal();

    /** 해제 스킬 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void ReleaseSkillInput();

    /** 프로필의 크기와 정규 Base64 인코딩을 검사해 바이트로 반환한다. */
    TSharedRef<FJsonObject> Profile(const AFablePlayerState* P) const;
    int32 GrowthWeaponId = 0, GrowthArmorId = 0, GrowthGoldBefore = 0, GrowthOreBefore = 0, GrowthGoldCost = 0,
          GrowthOreCost = 0;
    FFableItem GrowthBeforeItem;
    TSharedPtr<FJsonObject> GrowthReport;
    bool bResumeExpedition = false, bReconnectPeerGone = false;
    int32 ReconnectStep = 0, ReconnectSerial = 0, ReconnectWaypoint = 0;
    double ReconnectStarted = 0;
    TWeakObjectPtr<AFableEnemy> ReconnectVictim;
    TSharedPtr<FJsonObject> MidRunReport, ReconnectProgress, ServerMidBefore, ServerMidSolo, ServerMidRejoined;

    /** 보상 저장·클라이언트 중단·동일 캐릭터 복귀의 검증 단계를 진행한다. */
    bool TickReconnect(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero, double Now);

    /** 일반 이동·회피 경로를 사용하며 솔로 원정만 직업과 무관하게 같은 안전 경로를 선택한다. */
    bool ReconnectWalkSafe(AFablePlayerController* PC, bool bSoloParty = false);

    /** 같은 서버에서 이탈과 복귀 전후의 적·파티 상태를 기록한다. */
    void ObserveReconnectServer(UWorld* World);

    /** 서버가 관찰한 전투·파티·저장 상태를 JSON으로 만든다. */
    TSharedRef<FJsonObject> ReconnectServerSnapshot(UWorld* World) const;
    /** 기존 보상 흐름 앞에서 정상 마을 RPC와 실제 도착을 추가 관찰한다. */
    bool TickUnit2TownAnchors(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero,
                              double Now);

    /** 정상 서버 이벤트의 순간 상태를 보존하며 게임 상태를 변경하지 않는다. */
    void ObserveTownTravelServer(UWorld* World);

    /** 권한 이벤트와 동일 시도의 응답·순간 상태를 함께 기록한다. */
    void OnTownTravelAuthority(const FFableTownTravelEvent& Event);

    /** 준비 콜백에서 일반 소유자 ACK/취소 경로만 호출한다. */
    void OnTownTravelPrepared(const FFableTownTravelPrepared& Prepared);

    /** 마지막 응답으로 덮어쓰지 않고 모든 실제 시도 응답을 보관한다. */
    void OnTownTravelResponse(const FFableTownTravelResponse& Response);

    /** 정상 도착·해제 순서를 발신자별 사건과 대조한다. */
    void OnTownTravelProgress(const FFableTownTravelEvent& Event);

    /** 현재 연결·완전한 성장·전투 시각·안전 위치·필드 정본을 읽는다. */
    TSharedPtr<FJsonObject> TownTravelSnapshot(UWorld* World) const;

    /** 기존 월드와 PC에 등록한 본인 delegate만 해제한다. */
    void UnbindTownTravel();

    FString TownTravelScenario = TEXT("none");
    int32 TownStep = 0, TownAttemptIndex = 0, TownArrivalSamples = 0;
    int64 TownClientSequence = 0;
    double TownStarted = 0, TownStationaryAt = 0, TownLastBeginAt = -10, TownArrivalAt = 0;
    bool bTownReplaySent = false, bTownDuplicateSent = false, bTownCommitResponse = false;
    bool bTownOwnArrival = false, bTownOrderingFailed = false;
    FGuid TownRequestId, TownMalformedAttemptId, TownCancelAttemptId, TownReplayAttemptId, TownDuplicateAttemptId;
    FFableTownTravelPrepared TownPrepared;
    FFableTownTravelCommitReceipt TownCommit;
    TWeakObjectPtr<AFablePlayerController> TownBoundPlayer;
    TWeakObjectPtr<UFableTownTravelCoordinator> TownBoundCoordinator;
    TSharedPtr<FJsonObject> TownReport, ServerTownReport;
    TArray<FFableTownTravelResponse> TownResponses;
    TArray<TSharedPtr<FJsonValue>> TownArrivalObservations;

    FString Scenario = TEXT("connectivity");
    int32 DungeonStep = 0, DungeonWeaponId = 0;
    double DungeonStepAt = 0, DungeonActionAt = -10, DungeonAttackAt = -10, DungeonSkillAt = -10;
    TArray<FKey> DungeonReleaseKeys;
    TSharedPtr<FJsonObject> DungeonInitial, ServerDungeonObservation;

    /** 실제 파티 UI와 준비 동의 절차를 거쳐 서버의 입장 결과를 관찰한다. */
    bool TickExpeditionPreparation(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero,
                                   double Now);

    /** 서버 참가 증서와 정확히 일치하는 두 플레이어의 실제 도착을 보존한다. */
    void ObserveExpeditionServer(UWorld* World);

    int32 ExpeditionStep = 0, ExpeditionNoticeBefore = 0, ExpeditionPreviousRevision = 0;
    double ExpeditionStarted = 0, ExpeditionLastSentAt = -10;
    bool bExpeditionRequestSent = false;
    /** 이전 종료 결과는 실제 본인 UI 조회와 확인 응답을 구분해 기다린다. */
    int32 ExpeditionResultStep = 0;
    FGuid ExpeditionResultId, ExpeditionCompletionId, ExpeditionPreviousActionId;
    FGuid ExpeditionPartyId;
    FString ExpeditionSelfKey, ExpeditionPeerKey, ExpeditionRequestFingerprint;
    TSharedPtr<FJsonObject> ExpeditionReport, ServerExpeditionArrival;

    /** 두 솔로 파티의 처치·전멸·귀환이 서로에게 전파되지 않는지 실제 입력으로 검증한다. */
    bool TickPartyIsolation(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero, double Now);

    /** 서버가 관찰한 두 독립 원정의 단계별 정본 스냅숏을 보존한다. */
    void ObserveIsolationServer(UWorld* World);

    /** 지정한 두 캐릭터의 원정·적·저장 상태를 서버에서 관찰한다. */
    TSharedPtr<FJsonObject> IsolationServerSnapshot(UWorld* World) const;

    int32 IsolationStep = 0;
    double IsolationStepAt = 0, IsolationLastSentAt = -10;
    bool bIsolationRequestSent = false;
    FString IsolationSelfKey, IsolationPeerKey;
    FGuid IsolationPartyId, IsolationExpeditionId;
    TWeakObjectPtr<AFableEnemy> IsolationVictim;
    TSharedPtr<FJsonObject> IsolationReport, ServerIsolationReport;

    /** 실제 보스 결과·재도전·개인 지급·접속 복구와 명시적인 철수를 검증한다. */
    bool TickPartyResults(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero, double Now);

    /** 같은 원정의 결과 지급과 연결 인원 변화를 서버 정본으로 보존한다. */
    void ObserveResultsServer(UWorld* World);

    /** 현재 원정·개인 결과·적 UUID와 체력을 읽기 전용으로 기록한다. */
    TSharedPtr<FJsonObject> ResultsServerSnapshot(UWorld* World) const;

    int32 ResultsStep = 0, ResultsPreviousRevision = 0;
    double ResultsStepAt = 0, ResultsLastSentAt = -10, ResultsBossDodgeAt = -10;
    bool bResultsRequestSent = false, bResultsKillInputStopped = false;
    FString ResultsSelfKey, ResultsPeerKey;
    FGuid ResultsPartyId, ResultsClearedId, ResultsRetryId, ResultsCompletionId;
    TWeakObjectPtr<AFableEnemy> ResultsVictim;
    TSharedPtr<FJsonObject> ResultsReport, ServerResultsReport;

    /** 파티 UI 입력과 서버 복제 결과를 단계별로 관찰한다. */
    bool TickParty(AFablePlayerController* PC, AFablePlayerState* Profile, double Now);

    /** 서버가 관찰한 구성·연결 종료·동일 캐릭터 복귀를 보존한다. */
    void ObservePartyServer(UWorld* World);

    /** 공개 파티 명부를 검증용 JSON으로 직렬화한다. */
    TSharedRef<FJsonObject> PartyServerSnapshot(UWorld* World) const;

    int32 PartyStep = 0, PartyNoticeBefore = 0;
    double PartyActionAt = -1, PartyLastSentAt = -10;
    FGuid PartyAcceptedInvite;
    FString PartyFinalId, PartySelfKey, PartyPeerKey;
    TSharedPtr<FJsonObject> PartyReport, ServerPartyBefore, ServerPartySolo, ServerPartyRejoined;

    /** 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다. */
    bool TickDungeon(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero, double Now);

    /** 완료된 해당 파티의 보스·부활·보상만 서버에서 보존한다. */
    void ObserveDungeonServer(UWorld* World);

    /** 던전 입력 유지에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void DungeonHold(AFablePlayerController* PC, const FKey& Key, bool Held);

    /** 던전 키에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void DungeonKey(AFablePlayerController* PC, const FKey& Key);

    /** 던전 해제 입력에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void DungeonReleaseInput();

    /** 던전 검증의 목표 위치까지 일반 이동 입력을 전달한다. */
    bool DungeonWalk(AFablePlayerController* PC, const FVector& Location, float Radius = 100);

    /** 던전 퀘스트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void DungeonQuest(AFablePlayerController* PC, EFableQuestId Quest, double Now);

    /** 던전 검증 대상에게 일반 전투 입력을 전달한다. */
    void DungeonFight(AFablePlayerController* PC, double Now);

    /** 던전 대상에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    AFableEnemy* DungeonTarget(AFableCharacter* Hero) const;
    FString Role, Directory, Mode, AccountName, Password;
    int32 InitialClass = 0, Step = 0, BeforeManaPotions = 0;
    bool bOfflineFailure = false, bCrashRole = false, bDone = false, bSkillSent = false, bReleaseSkill = false;
    bool bTerminalPending = false, bTerminalSuccess = false, bPublicationFailed = false;
    FString TerminalBytes, TerminalReason, MenuProfileHash;
    int64 MenuRevision = 0;
    int32 PublicationAttempts = 0;
    uint32 PublicationLastError = 0;
    double PublicationStarted = 0, NextPublication = 0;
    double Started = 0, StepAt = 0, LastReport = 0, LastAction = -1, FinishedAt = 0;
    FVector MovementStart = FVector::ZeroVector;
    float Movement = 0;
    TMap<FString, bool> Checks;
    TArray<FString> Screenshots;
    TArray<FString> ServerObservedPlayerIds;
    TSharedPtr<FJsonObject> Checkpoint;
    TSharedPtr<FJsonObject> TownDurabilityCheckpoint;

    /** 실제 플레이 월드의 저장 상태를 퇴장 전 읽기 전용으로 고정한다. */
    TSharedRef<FJsonObject> ObserveTownPersistence(const AFablePlayerState* P) const;

    TSharedPtr<FJsonObject> Expected;
    FTSTicker::FDelegateHandle Ticker;
};
