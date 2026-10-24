#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableAdventureTypes.h"
#include "FableTownTypes.h"
#include "FableTownManager.generated.h"

class APlayerController;
class APlayerState;
class AFablePlayerState;
class UFableTownCatalog;

/** GameState를 늘리지 않는 서버 공간 명부. / GameStateを追加しないサーバー空間名簿。 */
UCLASS()
class FABLEWOOD_API AFableTownManager : public AActor
{
    GENERATED_BODY()

public:
    AFableTownManager();
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    static AFableTownManager* Find(const UWorld* World);
    static AFableTownManager* Ensure(UWorld* World);

    const UFableTownCatalog* GetCatalog() const;
    const FFableAdventureProgress& GetIdleProgress() const;
    double GetServerTime() const;
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
};
