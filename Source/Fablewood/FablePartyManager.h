#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FableExpeditionTypes.h"
#include "TimerManager.h"
#include "FablePartyManager.generated.h"

class AFablePlayerState;
class APlayerController;

/** 클라이언트는 의도만 전달하고 실제 구성 변경은 서버가 결정한다. */
UENUM()
enum class EFablePartyAction : uint8
{
    Create,
    Invite,
    Accept,
    Decline,
    Leave,
    TransferLeader
};

/** 재접속 중에도 유지하는 파티 구성원의 공개 정보. */
USTRUCT()
struct FFablePartyMember
{
    GENERATED_BODY()

    UPROPERTY()
    FString PlayerKey;

    UPROPERTY()
    FString DisplayName;

    UPROPERTY()
    int32 PlayerId = INDEX_NONE;

    UPROPERTY()
    bool bConnected = false;

    UPROPERTY()
    float ReconnectDeadline = 0;

    /** 이번 준비 리비전에 명시적으로 동의한 경우에만 준비 상태가 된다. */
    UPROPERTY()
    int32 ReadyRevision = 0;
};

/** 서버가 소유하는 2인 파티의 식별자와 구성원 명부. */
USTRUCT()
struct FFablePartyRecord
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid PartyId;

    UPROPERTY()
    FString LeaderKey;

    UPROPERTY()
    TArray<FFablePartyMember> Members;

    UPROPERTY()
    int32 Revision = 1;

    UPROPERTY()
    EFableRegion SelectedRegion = EFableRegion::RootRuins;

    UPROPERTY()
    EFableDifficulty SelectedDifficulty = EFableDifficulty::Normal;

    UPROPERTY()
    int32 PreparationRevision = 1;

    UPROPERTY()
    FGuid RetrySourceExpeditionId;

    UPROPERTY()
    FGuid RetrySourceCompletionId;
};

/** 수신자·파티·유효 시간이 고정된 일회성 초대장. */
USTRUCT()
struct FFablePartyInvite
{
    GENERATED_BODY()

    UPROPERTY()
    FGuid InviteId;

    UPROPERTY()
    FGuid PartyId;

    UPROPERTY()
    FString InviterKey;

    UPROPERTY()
    FString TargetKey;

    UPROPERTY()
    float ExpiresAt = 0;
};

/** 파티 초대·권한·탈퇴·재접속 유예를 서버에서 판정하고 명부를 복제한다. */
UCLASS()
class FABLEWOOD_API AFablePartyManager : public AActor
{
    GENERATED_BODY()

public:
    static constexpr int32 MaxMembers = 2;
    static constexpr float InviteLifetime = 30;
    static constexpr float ReconnectGrace = 120;

    /** 파티 명부가 필요한 클라이언트까지 복제되도록 초기화한다. */
    AFablePartyManager();

    /** 현재 월드의 파티 관리자를 찾는다. */
    static AFablePartyManager* Find(const UWorld* World);

    /** 공개 파티와 초대 상태를 등록한다. */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 서버의 만료 처리 타이머를 시작한다. */
    virtual void BeginPlay() override;

    /** 월드 종료 시 만료 타이머를 정리한다. */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    /** 인증된 접속을 등록하고 같은 캐릭터의 유예 중 멤버십을 복원한다. */
    void RegisterPlayer(APlayerController* Player);

    /** 접속 해제를 기록하고 남은 온라인 동료에게 파티장을 넘긴다. */
    void DisconnectPlayer(APlayerController* Player);

    /** 소유 컨트롤러에서 받은 변경 요청을 서버 규칙에 따라 처리한다. */
    bool ExecuteAction(APlayerController* Requester, EFablePartyAction Action, int32 TargetPlayerId = INDEX_NONE,
                       FGuid InviteId = FGuid());

    /** 파티장만 현재 준비 리비전의 목적지와 난이도를 선택한다. */
    bool SelectExpedition(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision,
                          EFableRegion Region, EFableDifficulty Difficulty);

    /** 토글 대신 명시한 준비 값을 같은 리비전에 적용한다. */
    bool SetExpeditionReady(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision,
                            bool bReady);

    /** 파티장의 입장 의도를 실제 서버 원정 규칙에 전달한다. */
    bool StartExpedition(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision);

    /** 연결·명부·동의를 재검사한 뒤 새 불변 참가 증서를 발급한다. */
    bool BuildReadySnapshot(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision,
                            FFableExpeditionTicket& OutTicket);

    /** 서버가 소유한 현재 연결만 불변 키로 반환한다. */
    APlayerController* GetConnectedPlayer(const FString& PlayerKey) const;

    /** 종료 결과를 고정하고 이전 동의를 전부 지운 뒤 재도전을 준비한다. */
    bool PrepareExpeditionRetry(APlayerController* Requester, FGuid PartyId, int32 ExpectedPreparationRevision,
                                FGuid OldExpeditionId, FGuid CompletionId);

    /** 전멸·귀환 뒤 다음 시도에 이전 동의가 재사용되지 않도록 초기화한다. */
    void InvalidatePreparation(FGuid PartyId);

    /** 공개 식별자로 구성원이 속한 파티를 찾는다. */
    const FFablePartyRecord* GetPartyForMember(const FString& PlayerKey) const;

    /** 연결 중 플레이어 ID로 구성원이 속한 파티를 찾는다. */
    const FFablePartyRecord* GetPartyForMember(int32 PlayerId) const;

    /** 수신자에게 아직 유효한 초대장을 찾는다. */
    const FFablePartyInvite* GetPendingInviteFor(const FString& PlayerKey) const;

    /** 연결 중 플레이어 ID로 초대장을 찾는다. */
    const FFablePartyInvite* GetPendingInviteFor(int32 PlayerId) const;

    /** 서버가 인증한 공개 식별자를 반환한다. */
    FString GetPlayerKey(int32 PlayerId) const;

    /** 두 프로필이 같은 유효 파티 구성원인지 확인한다. */
    bool ArePartyMembers(const AFablePlayerState* A, const AFablePlayerState* B) const;

    UPROPERTY(Replicated)
    TArray<FFablePartyRecord> Parties;

    UPROPERTY(Replicated)
    TArray<FFablePartyInvite> Invitations;

private:
    friend struct FFablePartyTestAccess;
    friend class AFableGameMode;

    /** 만료 초대와 재접속 유예가 끝난 구성원을 제거한다. */
    void ExpireEntries();

    /** 인증·프로필·월드가 유효한 요청자인지 확인한다. */
    AFablePlayerState* ValidatePlayer(APlayerController* Player) const;

    /** 대상 플레이어를 서버가 보유한 연결 목록에서 찾는다. */
    APlayerController* FindPlayer(int32 PlayerId) const;

    /** 명부 변경을 프로필에 동기화하고 즉시 복제한다. */
    void Publish();

    /** 거부 사유를 요청자에게만 안내한다. */
    bool Reject(AFablePlayerState* Profile, const FString& Notice) const;

    /** 파티장이 사라졌을 때 연결된 구성원부터 새 파티장을 선택한다. */
    void RepairLeader(FFablePartyRecord& Party);

    /** 전투 중 구성 변경을 제한한다. */
    bool CanEditMembership(const AFablePlayerState* Profile) const;

    /** 선택·구성·연결 변경을 이전 동의와 구분한다. */
    void ResetReadiness(FFablePartyRecord& Party);

    /** 소유권·파티 UUID·현재 리비전과 편집 가능 상태를 공통 검사한다. */
    FFablePartyRecord* ValidatePreparation(APlayerController* Requester, FGuid PartyId, int32 ExpectedRevision,
                                           bool bLeaderOnly);

    FTimerHandle ExpiryTimer;
    TMap<TWeakObjectPtr<APlayerController>, double> LastRequestTimes;
    TMap<FString, FString> PublicKeysByIdentity;
    TMap<FString, TWeakObjectPtr<APlayerController>> CurrentConnections;
};
