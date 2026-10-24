#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "FableRPGTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "FableOnlineValidationDriver.generated.h"
class FJsonObject;
class FJsonValue;
class AFablePlayerState;
class AFablePlayerController;
class AFableCharacter;
class AFableEnemy;
class UWorld;
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

private:
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

    /** 일반 이동·회피 입력으로 재접속 검증의 안전 경로를 따라 이동한다. */
    bool ReconnectWalkSafe(AFablePlayerController* PC);

    /** 같은 서버에서 이탈과 복귀 전후의 적·파티 상태를 기록한다. */
    void ObserveReconnectServer(UWorld* World);

    /** 서버가 관찰한 전투·파티·저장 상태를 JSON으로 만든다. */
    TSharedRef<FJsonObject> ReconnectServerSnapshot(UWorld* World) const;
    FString Scenario = TEXT("connectivity");
    int32 DungeonStep = 0, DungeonWeaponId = 0;
    double DungeonStepAt = 0, DungeonActionAt = -10, DungeonAttackAt = -10, DungeonSkillAt = -10;
    TArray<FKey> DungeonReleaseKeys;
    TSharedPtr<FJsonObject> DungeonInitial, ServerDungeonObservation;

    /** 협동 던전의 입장·웨이브·부활·보스 검증을 순서대로 진행한다. */
    bool TickDungeon(AFablePlayerController* PC, AFablePlayerState* Profile, AFableCharacter* Hero, double Now);

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
    TSharedPtr<FJsonObject> Expected;
    FTSTicker::FDelegateHandle Ticker;
};
