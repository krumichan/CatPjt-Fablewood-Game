#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "FableRPGTypes.h"
#include "FableDedicatedValidationDriver.generated.h"
class AFablePlayerController;
class AFablePlayerState;
class AFableEnemy;
class FJsonObject;
class FJsonValue;

/** Opt-in observation/input harness. It never changes health, rewards or profiles directly. */
/** 전용 서버의 일반 입력과 관찰 결과로 자동 검증을 진행하는 컴포넌트. */
UCLASS()
class FABLEWOOD_API UFableDedicatedValidationDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    UFableDedicatedValidationDriver();

    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 컴포넌트의 프레임 처리에서 현재 검증 단계를 진행한다. */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;

private:
    /** 서버에서 복제·이동·피해·저장 결과를 관찰한다. */
    void TickServer();

    /** 클라이언트가 관찰한 상태와 일반 입력으로 검증을 진행한다. */
    void TickClient();

    /** 아이템 사용·장비 변경·저장 결과의 검증 단계를 진행한다. */
    void TickInventory(AFablePlayerState* Profile, double Now, float Side);

    /** 시작 인벤토리 이벤트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    void BeginInventoryEvent(const FString& Action, const AFablePlayerState* Profile, const FFableItem& Item,
                             int32 Location, int32 Position, int32 GoldDelta, int32 OreDelta);

    /** 완료 인벤토리 이벤트에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool CompleteInventoryEvent(const AFablePlayerState* Profile, const FFableItem* Item, int32 Location,
                                int32 Position);

    /** 인벤토리 시도 결과를 검증 이력에 기록한다. */
    void RecordInventoryAttempt(const AFablePlayerState* Profile);

    /** 검증 캐릭터에 단발 키 입력을 전달한다. */
    void Key(const FKey& Value);

    /** 검증에 필요한 키의 누름 상태를 유지한다. */
    void Hold(const FKey& Value, bool bHeld);

    /** 일반 이동 입력으로 검증 캐릭터를 목표 지점에 접근시킨다. */
    bool WalkTo(const FVector& Position, float Radius = 90.0f);

    /** 검증 캐릭터의 이동 및 유지 입력을 해제한다. */
    void Stop();

    /** 검증 상태를 다음 단계로 옮기고 단계 시작 시각을 갱신한다. */
    void Advance(int32 Value);

    /** 일반 공격·스킬 입력으로 현재 전투 검증을 진행한다. */
    void Fight();

    /** 현재 검증 단계의 화면을 캡처한다. */
    void Capture(const FString& Name);

    /** 검증 입력을 정리하고 성공 또는 실패 결과를 확정한다. */
    void Finish(bool bSuccess, const FString& Reason);

    /** 대기 중 결과를 보고서에 기록한다. */
    void PublishPending();

    /** 진행 중 값을 기록한다. */
    void WriteRunning();

    /** 현재 관찰 값으로 검증 보고서 객체를 구성한다. */
    TSharedRef<FJsonObject> MakeReport(const FString& Status, const FString& Reason) const;

    /** 프로필 보고서에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    TSharedRef<FJsonObject> ProfileReport(const AFablePlayerState* Profile) const;

    /** 같은 실행·단계·역할에 속하는 다른 검증 프로세스의 JSON 보고서를 읽는다. */
    TSharedPtr<FJsonObject> ReadRole(const FString& OtherRole) const;

    /** 프로필의 성장·재화·장비를 검증용 서명으로 직렬화한다. */
    FString ProfileSignature(const AFablePlayerState* Profile) const;

    /** 상대 클라이언트 최소 단계 충족에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    bool PeerAtLeast(int32 Wanted);

    /** 생존한 적 목록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    int32 LiveEnemies() const;

    /** 투사체 목록에 필요한 값을 구성하거나 해당 검증 단계를 처리한다. */
    int32 Projectiles() const;
    TWeakObjectPtr<AFablePlayerController> PC;
    FString Role, RunId, Directory, Phase;
    bool bInventoryValidation = false, bInventoryFlowComplete = false;
    FString InventoryAction, InventoryProfileBefore, InventoryProfileHashBefore;
    bool bForgeWaypointReached = false;
    double ForgeStableAt = -1, InventoryLastAttemptAt = -1;
    int32 InventoryRequestAttempts = 0;
    TArray<TSharedPtr<FJsonValue>> InventoryRequestObservations;
    FFableItem InventoryItemBefore;
    int32 InventoryLocationBefore = 0, InventoryPositionBefore = -1;
    int32 InventoryGoldDelta = 0, InventoryOreDelta = 0;
    int64 InventoryRevisionBefore = 0;
    TSharedPtr<FJsonObject> InventoryWalletBefore;
    TArray<TSharedPtr<FJsonValue>> InventoryEvents;
    bool bServer = false, bDone = false, bPending = false, bTerminalSuccess = false;
    FString TerminalBytes, TerminalReason;
    double StartedAt = 0, StepAt = 0, LastAction = -10, LastSkill = -10, LastReportAt = -10;
    double QuietAt = -1, PendingAt = 0, NextPublishAt = 0, LastConnectionReportAt = -10;
    bool bQuitRequested = false;
    int32 Step = 0, PeerStepSeen = -1, PublicationAttempts = 0, AttackInputs = 0, SkillInputs = 0;
    int32 BeforeGold = 0, BeforePotions = 0, BeforeItems = 0, BeforeNotice = 0, NewItemId = 0;
    float BeforeHealth = 0;
    FString BeforeSignature;
    FVector InitialPosition = FVector::ZeroVector;
    float MaxMovement = 0, RemoteMovement = 0;
    TMap<TWeakObjectPtr<AActor>, FVector> RemoteStarts;
    TMap<TWeakObjectPtr<AFableEnemy>, float> EnemyHealth;
    TMap<FString, FVector> PlayerStarts;
    TMap<FString, float> PlayerMovement;
    TMap<FString, float> PlayerLastHealth;
    TMap<FString, int32> PlayerLastPotions;
    TMap<FString, bool> PlayerHitSeen, PlayerPotionSeen;
    bool bEnemyDamageSeen = false;
    TMap<FString, bool> Checks;
    TArray<FKey> Releases;
    TArray<FString> Screenshots;
};
