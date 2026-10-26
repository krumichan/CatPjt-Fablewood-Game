#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FableTownCatalog.h"
#include "FableTownRouteUiRenderDriver.generated.h"

class ACameraActor;
class AFableEnemy;
class AFablePlayerController;
class AFableTownManager;
class FJsonObject;
class FJsonValue;

/** 격리된 단독 실행에서 실제 HUD 화면만 촬영한다. / 隔離した単独実行で実HUDだけを撮影する。 */
UCLASS()
class FABLEWOOD_API UFableTownRouteUiRenderDriver : public UActorComponent
{
    GENERATED_BODY()

public:
    /** 기본 실행에서는 부착되지 않는 개발용 관측기를 준비한다. / 既定では付加しない開発観測器。 */
    UFableTownRouteUiRenderDriver();

    /** 명시 플래그와 출력 범위를 먼저 검사한다. / 明示フラグと出力範囲を先に確認する。 */
    virtual void BeginPlay() override;

    /** 정상 초기화를 기다린 뒤 안정된 네 화면을 순서대로 촬영한다. / 正常初期化後に四画面を順番に撮影する。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;

    /** 이 실행에서 소유한 상태만 복구한다. / この実行で所有した状態だけを戻す。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** 카탈로그와 플레이어가 실제 격리 실행의 소유인지 검사한다. / 実際の隔離所有を検査する。 */
    bool ValidateContext(FString& OutReason) const;

    /** 기본 객체를 건드리지 않고 자체 정의·카메라를 준비한다. / 自身の定義とカメラだけを用意する。 */
    bool PrepareScene(FString& OutReason);

    /** 가림 검사의 양성 대조 라벨을 유효한 화면 위치에 배치한다. / 遮蔽の陽性対照ラベルを配置する。 */
    bool PlaceControlLabel(FString& OutReason);

    /** 현재 사례에 필요한 자체 앵커·연결만 변경한다. / 現ケースの自身アンカーと接続だけを変える。 */
    bool ApplyCase(int32 Index, FString& OutReason);

    /** 제품 조회 결과와 화면 촬영 메타데이터를 함께 모은다. / 製品照会と撮影メタデータを収集する。 */
    bool ObserveCase(TSharedPtr<FJsonObject>& OutSnapshot, FString& OutReason) const;

    /** 전역 기본 정의와 미채택 네 마을의 불변 여부를 확인한다. / 既定定義と未採用四村の不変を確認する。 */
    bool DefinitionsUnchanged() const;

    /** 이 컴포넌트가 바꾼 로컬 상태만 복원한다. / このコンポーネントのローカル変更だけを復元する。 */
    bool RestoreScene();

    /** 증거 파일을 한 번만 기록하고 소유한 단독 실행을 종료한다. / 証拠を一度だけ記録し単独実行を終える。 */
    void Finish(bool bSuccess, const FString& Reason);

    TWeakObjectPtr<AFablePlayerController> PC;
    TWeakObjectPtr<AFableTownManager> Towns;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AActor> PreviousViewTarget;
    TArray<TPair<TWeakObjectPtr<AFableEnemy>, bool>> EnemyTicks;

    FFableTownDefinition OriginalLegacy;
    TArray<FString> DefaultSignatures;
    TArray<FString> CandidateSignatures;
    TArray<TSharedPtr<FJsonValue>> Snapshots;
    TSharedPtr<FJsonObject> PendingSnapshot;

    FString RunId;
    FString Directory;
    FString PendingScreenshot;
    FVector OriginalPawnLocation = FVector::ZeroVector;
    FVector OriginalVelocity = FVector::ZeroVector;
    FVector LockedPawnLocation = FVector::ZeroVector;
    FRotator OriginalPawnRotation = FRotator::ZeroRotator;
    FRotator OriginalControlRotation = FRotator::ZeroRotator;
    uint8 OriginalMovementMode = 0;
    uint8 OriginalCustomMovementMode = 0;

    int32 Width = 0;
    int32 Height = 0;
    int32 CurrentCase = 0;
    int32 StableTicks = 0;
    int32 StableFileTicks = 0;
    int64 PreviousFileSize = -1;
    double StartedAt = 0.0;
    double SceneAt = 0.0;
    double CaseAt = 0.0;
    double CaptureAt = 0.0;

    bool bOutputReady = false;
    bool bDone = false;
    bool bScenePrepared = false;
    bool bControlReady = false;
    bool bCatalogChanged = false;
    bool bInputFrozen = false;
    bool bOriginalAutoCamera = false;
};
