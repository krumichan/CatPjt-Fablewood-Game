#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableAdventureTypes.h"
#include "FableTownTypes.h"
#include "FableTownTravelTypes.h"
#include "FableTownManager.generated.h"

class APlayerController;
class APlayerState;
class AFablePlayerState;
class UFableTownCatalog;
class UFableTownTravelCoordinator;

/** GameState를 늘리지 않는 서버 공간 명부. / GameStateを追加しないサーバー空間名簿。 */
UCLASS()
class FABLEWOOD_API AFableTownManager : public AActor
{
    GENERATED_BODY()

public:
    AFableTownManager();
    virtual void BeginPlay() override;

    /** 등록된 현재 연결의 공간 문맥을 서버에서 미리 갱신한다. */
    virtual void Tick(float DeltaSeconds) override;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    static AFableTownManager* Find(const UWorld* World);
    static AFableTownManager* Ensure(UWorld* World);

    const UFableTownCatalog* GetCatalog() const;
    const FFableAdventureProgress& GetIdleProgress() const;
    double GetServerTime() const;

    /** 현재 인증 연결에만 새 nonce와 세대를 발급한다. */
    bool RegisterOrRefreshConnection(APlayerController* Player);

    /** semantic 공간 변경을 root 수명 revision으로 구분한다. */
    bool RefreshPhysicalPresence(APlayerController* Player);

    /** 현재 연결에 속하는 서버 공간 스냅샷만 반환한다. */
    bool GetConnectionSnapshot(const AFablePlayerState* Profile, FFableTownConnectionSnapshot& OutSnapshot) const;

    /** controller 객체·nonce·세대를 모두 대조한다. */
    bool IsCurrentConnection(const APlayerController* Player, int64 OwnerConnectionGeneration,
                             FGuid ConnectionNonce) const;

    /** 도착 뒤 공개 명부와 안전 앵커를 한 번 확정한다. */
    bool CommitAnchorPresence(APlayerController* Player, const FFableTownAnchorRef& Anchor,
                              const FTransform& AcceptedTransform, FGuid TransactionId, int64 Generation,
                              FFableTownConnectionSnapshot& OutAfter);

    /** 기존 manager의 서버 이동 component를 반환한다. */
    UFableTownTravelCoordinator* GetTravelCoordinator() const;

    /** JSON에서도 정확한 범위 안에서만 새 공간 revision을 허용한다. */
    bool CanAdvancePresenceRevision() const
    {
        return PresenceSequence < FableTownTravel::MaxSafeSequence;
    }

    FGuid GetPublicFieldScopeId() const;
    FFablePublicAreaKey GetPublicFieldKey() const;

    /** authority만 기존 등록 집합을 새 세대로 옮긴다. 새 인원은 수집하지 않는다. / 登録集合だけを次世代へ移す。 */
    FGuid RotatePublicFieldScope();

    /** 실제 소유 pawn에서만 위치를 얻는다. 클라이언트 XYZ/타인 ID 입력은 없다. / 所有pawnのみ、任意XYZ入力なし。 */
    bool ResolvePhysicalContext(const AActor* Context, FFableTownLocationContext& OutContext) const;
    bool RegisterValidatedPresence(APlayerController* Player);
    bool GetRegisteredPresence(const AFablePlayerState* Profile, FFableTownLocationContext& OutContext) const;

    /** 등록은 실제 기존 마을에서만 가능하다. 명부는 명시 해제까지 downed/void도 보존한다. / 明示解除まで倒れた参加者も保持。 */
    bool RegisterSharedForestParticipant(APlayerController* Player);
    void UnregisterSharedForestParticipant(APlayerController* Player);
    void UnregisterPlayer(APlayerController* Player);
    bool IsSharedForestParticipant(const AFablePlayerState* Profile, FGuid ExpectedScope = FGuid()) const;
    TArray<APlayerController*> GetSharedForestPlayers() const;
    TArray<APlayerState*> GetSharedForestParticipants() const;

private:
    struct FPresenceRecord
    {
        TWeakObjectPtr<APlayerController> Player;
        TWeakObjectPtr<AFablePlayerState> Profile;
        EFableTownId TownId = EFableTownId::Invalid;
        FGuid FieldScope;
        FFableTownConnectionSnapshot Snapshot;
        FString Identity;
    };

    bool ValidateConnection(const APlayerController* Player, const AFablePlayerState* ExpectedProfile,
                            bool bRequirePawn) const;
    void PruneDisconnected();

    UPROPERTY(Replicated)
    FGuid PublicFieldScopeId;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFableTownCatalog> Catalog;

    FFableAdventureProgress IdleProgress;
    TMap<TWeakObjectPtr<AFablePlayerState>, FPresenceRecord> Presence;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFableTownTravelCoordinator> TravelCoordinator;

    FGuid WorldEpoch;
    int64 ConnectionSequence = 0;
    int64 PresenceSequence = 0;
    TMap<FString, TWeakObjectPtr<APlayerController>> CurrentConnectionsByIdentity;
    TSet<TWeakObjectPtr<APlayerController>> RetiredConnections;
};
