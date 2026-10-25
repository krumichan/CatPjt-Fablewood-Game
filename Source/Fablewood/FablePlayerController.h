#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FableRPGTypes.h"
#include "FableTownTravelTypes.h"
#include "FablePlayerController.generated.h"

class AFableCharacter;
struct FFablePersonalExpeditionResult;

/** 현재 열려 있는 NPC 대화 종류 값의 종류를 정의한다. */
enum class EFableDialogue : uint8
{
    None,
    Guide,
    Forge,
    Shop,
    Portal
};

/** 인벤토리 표시 순서 값의 종류를 정의한다. */
enum class EFableInventorySort : uint8
{
    Acquired,
    Quality,
    Power
};

/** 공방 작업 화면의 탭 구분 값의 종류를 정의한다. */
enum class EFableWorkshopTab : uint8
{
    Craft,
    Dismantle,
    Reroll,
    Repair,
    Stash
};

/** 파티 구성과 원정 준비 화면을 구분한다. */
enum class EFablePartyPanelTab : uint8
{
    Members,
    Expedition,
    Result
};

/** 플레이어 입력과 UI 요청을 서버 권한의 게임 동작으로 연결하는 컨트롤러. */
UCLASS()
class FABLEWOOD_API AFablePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    /** 클라이언트는 논리 Town/Anchor만 요청하며 서버 좌표를 지정하지 않는다. */
    FGuid UIRequestTownTravel(EFableTownId TargetTownId, FName AnchorId, int32 ExpectedCatalogVersion);

    /** queued 자동 작업을 막고 정상 취소를 송신한다. */
    FGuid UICancelTownTravel(FGuid TransactionId, int64 Generation);

    /** 명시 ACK/replay를 owner reliable 채널로 전송한다. */
    FGuid UIRequestTownTravelLoadAcknowledgement(FGuid TransactionId, int64 Generation, FGuid ConnectionNonce,
                                                 FGuid ChallengeNonce, bool bLoaded);

    /** 현재 owner의 준비 통지를 읽는다. */
    const FFableTownTravelPrepared& GetTownTravelPrepared() const;

    /** 마지막 상관 응답을 읽는다. */
    const FFableTownTravelResponse& GetTownTravelLastResponse() const;

    /** 지형 준비 통지의 구독 지점을 반환한다. */
    FOnFableTownTravelPrepared& OnTownTravelPrepared();

    /** 모든 시도 응답의 구독 지점을 반환한다. */
    FOnFableTownTravelResponse& OnTownTravelResponse();

    /** authority와 로컬 표식을 구분하는 진행 이벤트를 반환한다. */
    FOnFableTownTravelEvent& OnTownTravelProgress();

    /** 실제 확인 전의 여행 UI 잠금 상태를 읽는다. */
    bool IsTownTravelInputBlocked() const;

    /** 실제 소유 pawn 도착의 연속 샘플 수를 읽는다. */
    int32 GetTownArrivalSampleCount() const;

    /** 실제 도착 샘플의 경과 시간을 읽는다. */
    double GetTownArrivalSampleSpan() const;

    /** 서버가 논리 목적지와 소유자의 공간 revision을 검사한다. */
    UFUNCTION(Server, Reliable)
    void ServerRequestTownTravel(FGuid RequestId, EFableTownId TargetTownId, FName AnchorId,
                                 int32 ExpectedCatalogVersion, int64 ExpectedPresenceRevision);

    /** 서버가 고정 descriptor를 소유 연결에만 전달한다. */
    UFUNCTION(Client, Reliable)
    void ClientPrepareTownGeometry(const FFableTownTravelPrepared& Prepared);

    /** 지형 로드 주장도 서버 tuple 검사 없이 신뢰하지 않는다. */
    UFUNCTION(Server, Reliable)
    void ServerAcknowledgeTownGeometry(FGuid AckAttemptId, FGuid TransactionId, int64 Generation, FGuid ConnectionNonce,
                                       FGuid ChallengeNonce, bool bLoaded);

    /** 취소도 정확한 owner 세대를 요구한다. */
    UFUNCTION(Server, Reliable)
    void ServerCancelTownTravel(FGuid CancelAttemptId, FGuid TransactionId, int64 Generation, FGuid ConnectionNonce);

    /** 도착 확인은 임의 XYZ 대신 확정 receipt를 참조한다. */
    UFUNCTION(Server, Reliable)
    void ServerConfirmTownArrival(FGuid AttemptId, FGuid TransactionId, int64 Generation, FGuid ConnectionNonce,
                                  int64 ExpectedCommitSequence);

    /** 시도 응답을 소유자에게 전달한다. */
    UFUNCTION(Client, Reliable)
    void ClientTownTravelResponse(const FFableTownTravelResponse& Response);

    /** 서버 수명 이벤트를 로컬 이벤트와 분리해 전달한다. */
    UFUNCTION(Client, Reliable)
    void ClientTownTravelEvent(const FFableTownTravelEvent& Event);
    /** 온라인 화면과 UI 초점 상태에 따라 플레이어의 키 입력을 처리한다. */
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;

    /** UI에서 선택한 주변 대상 상호작용 동작을 처리한다. */
    void UIInteract();

    /** UI에서 선택한 동료 부활 시작 동작을 처리한다. */
    void UIStartRevive(AFableCharacter* Target);

    /** UI에서 선택한 동료 부활 중지 동작을 처리한다. */
    void UIStopRevive();

    /** 가까이 있는 부활 가능한 동료 값을 반환한다. */
    AFableCharacter* GetNearbyReviveTarget() const;

    /** UI에서 선택한 전투 재도전 동작을 처리한다. */
    void UIRetryEncounter();

    /** 전투 재도전에 필요한 조건을 만족하는지 확인한다. */
    bool CanRetryEncounter() const;

    /** 재도전 동작의 안내 문구 값을 반환한다. */
    FString GetRetryActionText() const;

    /** UI에서 선택한 퀘스트 확인 동작을 처리한다. */
    void UIConfirmQuest();

    /** UI에서 선택한 선택 퀘스트 동작을 처리한다. */
    void UISelectQuest(EFableQuestId QuestId);

    /** 선택한 퀘스트 식별자 값을 반환한다. */
    EFableQuestId GetSelectedQuestId() const;

    /** UI에서 선택한 선택 던전 동작을 처리한다. */
    void UISelectDungeon(EFableRegion Region);

    /** UI에서 선택한 선택 던전 입장 동작을 처리한다. */
    void UIEnterSelectedDungeon();

    /** UI에서 선택한 선택 난이도 동작을 처리한다. */
    void UISelectDifficulty(EFableDifficulty Difficulty);

    /** 선택한 던전 난이도 값을 반환한다. */
    EFableDifficulty GetSelectedDifficulty() const;

    /** 난이도 선택 잠금 여부를 확인한다. */
    bool IsDifficultySelectionLocked() const;

    /** 선택한 던전 식별자 값을 반환한다. */
    EFableRegion GetSelectedDungeonId() const;

    /** 입장 창 열림 여부를 확인한다. */
    bool IsPortalOpen() const
    {
        return Dialogue == EFableDialogue::Portal;
    }

    /** 던전 입장에 필요한 조건을 만족하는지 확인한다. */
    bool CanEnterDungeon(EFableRegion Region) const;

    /** 던전 입장 준비 상태 안내 값을 반환한다. */
    FString GetDungeonReadinessText(EFableRegion Region) const;

    /** UI에서 선택한 직업 선택 동작을 처리한다. */
    void UIChooseClass(EFableClass HeroClass);

    /** UI에서 선택한 장비 착용 동작을 처리한다. */
    void UIEquip(int32 ItemId);

    /** UI에서 선택한 장비 강화 동작을 처리한다. */
    void UIEnhance(int32 ItemId);

    /** UI에서 선택한 상점 구매 동작을 처리한다. */
    void UIShopBuy(EFableShopOffer Offer);

    /** UI에서 선택한 아이템 판매 동작을 처리한다. */
    void UISell(int32 ItemId);

    /** UI에서 선택한 체력 물약 사용 동작을 처리한다. */
    void UIUseHealthPotion();

    /** UI에서 선택한 마나 물약 사용 동작을 처리한다. */
    void UIUseManaPotion();

    /** UI에서 선택한 공방 열기 동작을 처리한다. */
    void UIOpenWorkshop();

    /** 공방 창 열림 여부를 확인한다. */
    bool IsWorkshopOpen() const
    {
        return bWorkshopOpen;
    }

    /** UI에서 선택한 선택 공방 탭 동작을 처리한다. */
    void UISelectWorkshopTab(EFableWorkshopTab Tab);

    /** 공방 창의 선택 탭 값을 반환한다. */
    EFableWorkshopTab GetWorkshopTab() const
    {
        return WorkshopTab;
    }

    /** UI에서 선택한 선택 공방 아이템 동작을 처리한다. */
    void UISelectWorkshopItem(int32 ItemId);

    /** 공방에서 선택한 아이템 식별자 값을 반환한다. */
    int32 GetSelectedWorkshopItemId() const;

    /** 공방에 표시할 아이템 식별자 목록 값을 반환한다. */
    TArray<int32> GetWorkshopDisplayIds() const;

    /** UI에서 선택한 선택 장비 제작 제작법 동작을 처리한다. */
    void UISelectCraftRecipe(int32 Rarity, EFableEquipmentSlot Slot);

    /** 제작할 장비의 등급 값을 반환한다. */
    int32 GetCraftRarity() const
    {
        return CraftRarity;
    }

    /** 제작할 장비의 착용 부위 값을 반환한다. */
    EFableEquipmentSlot GetCraftSlot() const
    {
        return CraftSlot;
    }

    /** UI에서 선택한 선택 장비 옵션 동작을 처리한다. */
    void UISelectAffix(int32 Index);

    /** 선택한 장비 옵션의 위치 값을 반환한다. */
    int32 GetSelectedAffixIndex() const
    {
        return SelectedAffixIndex;
    }

    /** UI에서 선택한 세트 창고 창고 수령 동작을 처리한다. */
    void UISetStashWithdrawal(bool bWithdraw);

    /** 창고 창고 수령 여부를 확인한다. */
    bool IsStashWithdrawal() const
    {
        return bStashWithdrawal;
    }

    /** UI에서 선택한 장비 제작 동작을 처리한다. */
    void UICraft();

    /** UI에서 선택한 장비 분해 동작을 처리한다. */
    void UIDismantle(int32 ItemId);

    /** 분해 확인 대기 여부를 확인한다. */
    bool IsDismantleConfirmationPending() const;

    /** UI에서 선택한 옵션 재설정 동작을 처리한다. */
    void UIRerollAffix(int32 ItemId, int32 AffixIndex);

    /** UI에서 선택한 내구도 수리 동작을 처리한다. */
    void UIRepair(int32 ItemId);

    /** UI에서 선택한 창고 보관 동작을 처리한다. */
    void UIDeposit(int32 ItemId);

    /** UI에서 선택한 창고 수령 동작을 처리한다. */
    void UIWithdraw(int32 ItemId);

    /** UI에서 선택한 공방 작업 동작을 처리한다. */
    void UIWorkshopAction();

    /** 공방 거부 사유 값을 반환한다. */
    FString GetWorkshopBlockReason() const;

    /** UI에서 선택한 표시 전환 인벤토리 동작을 처리한다. */
    void UIToggleInventory();

    /** UI에서 선택한 표시 전환 퀘스트 목록 동작을 처리한다. */
    void UIToggleQuests();

    /** UI에서 선택한 표시 전환 도움말 동작을 처리한다. */
    void UIToggleHelp();

    /** UI에서 선택한 표시 전환 스킬 동작을 처리한다. */
    void UIToggleSkills();

    /** 파티 관리 창을 열거나 닫고 게임 입력 초점을 조정한다. */
    void UIToggleParty();

    /** 파티 창 열림 여부를 반환한다. */
    bool IsPartyOpen() const
    {
        return bPartyOpen;
    }

    /** 새 파티 생성을 소유 플레이어의 서버에 요청한다. */
    void UICreateParty();

    /** 접속한 대상 플레이어에게 파티 초대를 요청한다. */
    void UIInviteParty(int32 TargetPlayerId);

    /** 선택한 초대장의 수락 또는 거절을 서버에 요청한다. */
    void UIRespondPartyInvite(FGuid InviteId, bool bAccept);

    /** 현재 파티 탈퇴를 서버에 요청한다. */
    void UILeaveParty();

    /** 현재 파티의 파티장 위임을 서버에 요청한다. */
    void UITransferPartyLeader(int32 TargetPlayerId);

    /** 파티 창의 구성·원정 탭을 전환한다. */
    void UISelectPartyTab(EFablePartyPanelTab Tab);

    /** 포털과 G 키에서 공통으로 사용하는 원정 준비 창을 연다. */
    void UIOpenPartyExpedition();

    /** 원정 준비 화면이 열려 있는지 반환한다. */
    bool IsPartyExpeditionOpen() const
    {
        return bPartyOpen && PartyPanelTab == EFablePartyPanelTab::Expedition;
    }

    /** 서버가 복제한 선택에 대해 자기 준비 의사를 명시한다. */
    void UISetPartyReady(bool bReady);

    /** 현재 서버 준비 명부로 파티장 출발을 요청한다. */
    void UIEnterPreparedParty();

    /** 선택할 던전·난이도와 관찰한 파티 리비전을 서버에 전달한다. */
    void UIRequestPartySelection(FGuid PartyId, int32 PreparationRevision, EFableRegion Region,
                                 EFableDifficulty Difficulty);

    /** 준비할 파티 리비전과 자기 의사를 서버에 전달한다. */
    void UIRequestPartyReady(FGuid PartyId, int32 PreparationRevision, bool bReady);

    /** 입장할 파티 리비전만 전달하고 목적지는 서버가 결정한다. */
    void UIRequestPartyEntry(FGuid PartyId, int32 PreparationRevision);

    /** 현재 요청의 서버 응답을 기다리는지 반환한다. */
    bool IsPartyPreparationPending() const
    {
        return PendingPartyRequestId.IsValid();
    }

    /** 마지막 서버 준비 결과의 안내를 반환한다. */
    const FString& GetPartyPreparationMessage() const
    {
        return PartyPreparationMessage;
    }

    /** 마지막 준비 결과가 성공 안내인지 반환한다. */
    bool DidPartyPreparationSucceed() const
    {
        return bPartyPreparationSucceeded;
    }

    /** 마지막 서버 응답이 확인한 준비 리비전을 반환한다. */
    int32 GetPartyPreparationResultRevision() const
    {
        return PartyPreparationResultRevision;
    }

    /** 파티장이 원정 선택을 변경할 수 있는지 표시한다. */
    bool CanSelectPartyExpedition() const;

    /** 초대·접속·개인 자격상 준비 버튼을 누를 수 있는지 표시한다. */
    bool CanSetPartyReady() const;

    /** 현재 선택 리비전에 대한 자기 준비 상태를 반환한다. */
    bool IsPartyReady() const;

    /** 현재 화면에서 파티장 출발 버튼을 사용할 수 있는지 표시한다. */
    bool CanEnterPreparedParty() const;

    /** 파티 원정의 현재 준비 상태를 설명한다. */
    FString GetPartyPreparationStatus() const;

    /** 실제 개인 영수증을 조회하는 결과 창을 열고 이전 화면의 보상을 재계산하지 않는다. */
    void UIOpenExpeditionResult(FGuid ExpeditionId = FGuid());

    /** 결과 탭의 실제 열림 상태를 반환한다. */
    bool IsExpeditionResultOpen() const
    {
        return bPartyOpen && PartyPanelTab == EFablePartyPanelTab::Result;
    }

    /** 본인에게 복제된 결과만 반환하고 잘못된 수신자 정보는 표시하지 않는다. */
    const FFablePersonalExpeditionResult* GetDisplayedExpeditionResult() const;

    /** 화면 확인용 식별자는 자동 검증에서만 사용하고 플레이어에게 출력하지 않는다. */
    FGuid GetDisplayedResultCompletionId() const;

    /** 현재 개인 결과를 명시한 원정 UUID로 다시 요청한다. */
    void UIRequestExpeditionResult(FGuid ExpeditionId);

    /** 현재 보이는 종료 결과를 읽었다는 확인만 서버에 보낸다. */
    void UIAcknowledgeExpeditionResult();

    /** 지연된 화면의 결과 확인도 같은 제품 RPC를 사용한다. */
    void UIRequestAcknowledgeExpeditionResult(FGuid ExpeditionId, FGuid CompletionId, int32 ResultRevision);

    /** 표시된 결과를 바탕으로 별도의 명시적 재도전 준비 창을 연다. */
    void UIBeginExpeditionRetry();

    /** 이전 완료 원정과 현재 명부 리비전으로 재도전을 준비한다. */
    void UIRequestPrepareExpeditionRetry(FGuid PartyId, int32 PreparationRevision, FGuid OldExpeditionId,
                                         FGuid CompletionId);

    /** 새 출발도 기존 원정과 다른 증서가 발급될 때까지 기다린다. */
    void UIRequestRetryExpedition(FGuid PartyId, int32 PreparationRevision, FGuid OldExpeditionId, FGuid CompletionId);

    /** 현재 서버 선택이 명시적인 재도전 준비인지 반환한다. */
    bool IsPartyRetryPreparation() const;

    /** 표시된 개인 결과로 재도전 준비를 시작할 수 있는지 안내한다. */
    bool CanPrepareExpeditionRetry() const;

    /** 장비 목록의 현재 행을 선택하고 지급 당시 옵션을 읽는다. */
    void UISelectResultItem(int32 Index);

    /** 결과에 포함된 실제 지급 장비를 반환한다. */
    TArray<FFableItem> GetExpeditionResultItems() const;

    /** 현재 선택한 결과 장비의 행을 반환한다. */
    int32 GetSelectedResultItemIndex() const;

    /** 해당 지급 영수증의 실제 저장 확인 상태를 표시한다. */
    FString GetExpeditionResultSaveText() const;

    /** 메모리 전용 기록을 영구 저장 완료라고 판정하지 않는다. */
    bool IsExpeditionResultSaved() const;

    /** 결과에서 기존 귀환 포털 정책을 유지하며 이동 안내 또는 귀환을 요청한다. */
    void UIReturnFromExpeditionResult();

    /** 현재 완료 원정의 기존 포털 가까이 있는지 표시한다. */
    bool CanReturnFromExpeditionResult() const;

    /** 관찰한 원정·완료 식별자로 같은 귀환 제품 요청을 보낸다. */
    void UIRequestExpeditionReturn(FGuid ExpeditionId, FGuid CompletionId);

    /** 진행 중인 현재 원정의 철수 동의를 명시한다. */
    void UISetAbandonConsent(bool bConsent);

    /** 지연되거나 다른 원정의 동의도 서버에서 판정할 수 있는 공통 요청이다. */
    void UIRequestAbandonConsent(FGuid ExpeditionId, int32 DepartureRevision, bool bConsent);

    /** 철수 버튼이 현재 진행에서 사용 가능한지 표시한다. */
    bool CanSetAbandonConsent() const;

    /** 현재 캐릭터가 서버에 철수 동의를 제출했는지 반환한다. */
    bool HasAbandonConsent() const;

    /** 철수 연결 명부가 바뀌면 과거 동의 리비전을 사용하지 않는다. */
    int32 GetAbandonVoteRevision() const;

    /** 공개 참가 증서의 철수 동의 수와 연결 상태를 설명한다. */
    FString GetAbandonConsentStatus() const;

    /** 결과·철수 요청은 준비 요청과 별도 상관 식별자로 응답을 기다린다. */
    bool IsExpeditionActionPending() const
    {
        return PendingExpeditionActionId.IsValid();
    }

    /** 마지막 결과 요청의 성공 여부를 읽는다. */
    bool DidExpeditionActionSucceed() const
    {
        return bExpeditionActionSucceeded;
    }

    /** 마지막으로 확인한 응답 식별자는 자동 검증용이며 화면에 표시하지 않는다. */
    FGuid GetLastExpeditionActionRequestId() const
    {
        return LastExpeditionActionRequestId;
    }

    /** 마지막 응답의 결과 리비전을 자동 검증에서 읽는다. */
    int32 GetExpeditionActionResultRevision() const
    {
        return ExpeditionActionResultRevision;
    }

    /** 결과와 철수 요청의 서버 안내를 반환한다. */
    const FString& GetExpeditionActionMessage() const
    {
        return ExpeditionActionMessage;
    }

    /** UI에서 선택한 선택 스킬 동작을 처리한다. */
    void UISelectSkill(int32 Slot);

    /** UI에서 선택한 선택 스킬 창 탭 동작을 처리한다. */
    void UISelectSkillPanelTab(int32 Tab);

    /** 스킬 창의 선택 탭 값을 반환한다. */
    int32 GetSkillPanelTab() const
    {
        return SkillPanelTab;
    }

    /** UI에서 선택한 특성 선택 동작을 처리한다. */
    void UISelectTalent(int32 TargetTier, EFableTalentPath Path);

    /** 선택한 특성의 전직 단계 값을 반환한다. */
    int32 GetSelectedTalentStage() const
    {
        return SelectedTalentTier;
    }

    /** 선택한 특성 후보 값을 반환한다. */
    EFableTalentPath GetSelectedTalentChoice() const
    {
        return SelectedTalentPath;
    }

    /** UI에서 선택한 선택 특성 동작을 처리한다. */
    void UIChooseTalent();

    /** UI에서 선택한 빌드 초기화 요청 동작을 처리한다. */
    void UIRequestBuildReset();

    /** UI에서 선택한 빌드 초기화 확정 동작을 처리한다. */
    void UIConfirmBuildReset();

    /** UI에서 선택한 빌드 초기화 취소 동작을 처리한다. */
    void UICancelBuildReset();

    /** 빌드 초기화 확인 대기 여부를 확인한다. */
    bool IsBuildResetConfirmationPending() const;

    /** UI에서 선택한 스킬 강화 동작을 처리한다. */
    void UIUpgradeSkill(int32 Slot);

    /** UI에서 선택한 전직 동작을 처리한다. */
    void UIAdvanceClass();

    /** 선택한 스킬 슬롯 값을 반환한다. */
    int32 GetSelectedSkillSlot() const
    {
        return SelectedSkillSlot;
    }

    /** 스킬 창 열림 여부를 확인한다. */
    bool IsSkillsOpen() const
    {
        return bSkillsOpen;
    }

    /** UI에서 선택한 선택 아이템 동작을 처리한다. */
    void UISelectItem(int32 ItemId);

    /** 정렬된 인벤토리 표시 목록 값을 반환한다. */
    TArray<int32> GetInventoryDisplayIds() const;

    /** UI에서 선택한 인벤토리 정렬 전환 동작을 처리한다. */
    void UICycleInventorySort();

    /** 현재 인벤토리 정렬 방식의 이름 값을 반환한다. */
    FString GetInventorySortLabel() const;

    /** UI에서 선택한 열린 창 닫기 동작을 처리한다. */
    void UIClosePanels();

    /** UI에서 선택한 선택 이전 동작을 처리한다. */
    void UISelectPrevious();

    /** UI에서 선택한 선택 다음 동작을 처리한다. */
    void UISelectNext();

    /** UI에서 선택한 시작 신규 게임 동작을 처리한다. */
    void UIStartNewGame(bool bHost = false);

    /** UI에서 선택한 이어하기 게임 동작을 처리한다. */
    void UIContinueGame(bool bHost = false);

    /** UI에서 선택한 접속 게임 동작을 처리한다. */
    void UIJoinGame();

    /** UI에서 선택한 세트 접속 주소 동작을 처리한다. */
    void UISetJoinAddress(const FString& Address);

    /** UI에서 선택한 입력 초점 접속 주소 동작을 처리한다. */
    void UIFocusJoinAddress();

    /** UI에서 선택한 세트 시작 직업 동작을 처리한다. */
    void UISetInitialClass(EFableClass HeroClass);

    /** UI에서 선택한 복귀 목표 시작 화면 동작을 처리한다. */
    void UIReturnToFrontEnd();

    /** UI에서 선택한 온라인 안전 이탈 동작을 처리한다. */
    void UIRequestOnlineLeave();

    /** 안전 이탈 대기 여부를 확인한다. */
    bool IsOnlineLeavePending() const
    {
        return bOnlineLeavePending;
    }

    /** 시작 화면 열림 여부를 확인한다. */
    bool IsFrontEndOpen() const
    {
        return bFrontEndOpen;
    }

    /** 접속 주소 입력 초점 여부를 확인한다. */
    bool IsJoinAddressFocused() const
    {
        return bJoinAddressFocused;
    }

    /** 게임 접속 화면에 입력된 주소를 반환한다. */
    const FString& GetJoinAddress() const
    {
        return JoinAddress;
    }

    /** 시작 직업 값을 반환한다. */
    EFableClass GetInitialClass() const
    {
        return InitialClass;
    }

    /** 인벤토리 열림 여부를 확인한다. */
    bool IsInventoryOpen() const
    {
        return bInventoryOpen;
    }

    /** 퀘스트 목록 열림 여부를 확인한다. */
    bool IsQuestJournalOpen() const
    {
        return bQuestsOpen;
    }

    /** 도움말 열림 여부를 확인한다. */
    bool IsHelpOpen() const
    {
        return bHelpOpen;
    }

    /** 대화 열림 여부를 확인한다. */
    bool IsDialogueOpen() const
    {
        return Dialogue != EFableDialogue::None;
    }

    /** 상점 열림 여부를 확인한다. */
    bool IsShopOpen() const
    {
        return Dialogue == EFableDialogue::Shop;
    }

    /** 현재 열려 있는 대화 종류 값을 반환한다. */
    EFableDialogue GetDialogue() const
    {
        return Dialogue;
    }

    /** 임의 UI 창 열림 여부를 확인한다. */
    bool IsAnyPanelOpen() const
    {
        return bFrontEndOpen || bInventoryOpen || bWorkshopOpen || bQuestsOpen || bHelpOpen || bSkillsOpen ||
               bPartyOpen || IsDialogueOpen();
    }

    /** 선택한 아이템의 지역 식별자 값을 반환한다. */
    int32 GetSelectedItemId() const;

    /** 현재 상호작용 대상의 안내 문구 값을 반환한다. */
    FString GetContextActionText() const;

    /** 주변 이야기 오브젝트의 위치 번호 값을 반환한다. */
    int32 GetNearbyStoryObjectIndex() const;

    /** 현재 소유 원정의 목표를 실제 월드 좌표로 반환한다. */
    bool GetNavigationTarget(FVector& OutLocation, FString& OutLabel) const;

    /** 아직 확인하지 않은 임시 안내를 표시할지 판단한다. */
    bool ShouldShowTransientNotice() const;

    /** 마을 게시판 인접 여부를 확인한다. */
    bool IsNearTownBoard() const;

    /** 공방 인접 여부를 확인한다. */
    bool IsNearForge() const;

    /** 상인 인접 여부를 확인한다. */
    bool IsNearMerchant() const;

    /** 마을 서비스 인접 여부를 확인한다. */
    bool IsNearTownService() const;

protected:
    /** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
    virtual void BeginPlay() override;

    /** 플레이어의 UI·상호작용·전투 입력을 등록한다. */
    virtual void SetupInputComponent() override;

    /** 플레이어 입력 상태와 주변 상호작용 대상을 갱신한다. */
    virtual void PlayerTick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** 지형 대기와 실제 도착의 제한된 관찰을 갱신한다. */
    void TickTownTravel();

    /** 현재 prepared tuple이 유지될 때만 자동 ACK한다. */
    void TryAutoAcknowledgeTownGeometry();

    /** 보유한 정확 임시 지형 token만 반환한다. */
    void ReleaseTownPendingGeometry(bool bReleaseSource);

    /** 소유 클라이언트의 실제 관측을 별도 sequence로 기록한다. */
    void EmitLocalTownEvent(EFableTownTravelEventKind Kind, EFableTownTravelError Error);

    /** 서버가 도착을 확인한 후에만 UI와 source를 해제한다. */
    void CompleteTownArrivalAfterConfirmation();

    FFableTownTravelPrepared TownPrepared;
    FFableTownTravelResponse TownLastResponse;
    FFableTownTravelCommitReceipt TownAwaitingArrival;
    FFableTownGeometryLease TownTargetLease;
    FFableTownGeometryLease TownSourceLease;
    FFableTownGeometryLease TownPresenceLease;
    FOnFableTownTravelPrepared TownPreparedEvents;
    FOnFableTownTravelResponse TownResponseEvents;
    FOnFableTownTravelEvent TownProgressEvents;
    bool bTownCancelRequested = false;
    bool bTownAutoAckSent = false;
    bool bTownAutoAckEligible = false;
    bool bTownTerminal = false;
    bool bTownTravelUiLocked = false;
    bool bTownArrivalConfirmationSent = false;
    bool bTownMoveInputHeld = false;
    bool bTownArrivalFailed = false;
    double TownArrivalDeadline = 0;
    int32 TownArrivalSamples = 0;
    double TownArrivalFirstSampleTime = 0;
    double TownArrivalLastSampleTime = 0;
    int64 TownLocalEventSequence = 0;
    FGuid TownObservedArrivalId;

private:
    FGuid OnlineLeaveRequestId;
    FGuid ServerOnlineLeaveRequestId;
    bool bOnlineLeavePending = false;
    bool bServerOnlineLeavePending = false;
    bool bOnlineLeaveInputBlocked = false;
    double LastOnlineLeaveRequestTime = -100.0;

    /** 안전 이탈 중 이동·전투·UI 입력을 제한한다. */
    void BlockOnlineLeaveInput();

    /** 온라인 안전 이탈 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerRequestOnlineLeave(FGuid RequestId);

    /** 서버가 전달한 온라인 안전 이탈 결과 결과를 소유 클라이언트에 반영한다. */
    UFUNCTION(Client, Reliable)
    void ClientOnlineLeaveResult(FGuid RequestId, bool bSuccess, const FString& Reason);
    bool bFrontEndOpen = false;
    bool bInventoryOpen = false;
    bool bWorkshopOpen = false;
    EFableWorkshopTab WorkshopTab = EFableWorkshopTab::Craft;
    int32 WorkshopItemId = INDEX_NONE;
    int32 CraftRarity = 1;
    EFableEquipmentSlot CraftSlot = EFableEquipmentSlot::Weapon;
    int32 SelectedAffixIndex = 0;
    bool bStashWithdrawal = false;
    int32 PendingDismantleItemId = INDEX_NONE;
    FString PendingDismantleSignature;
    bool bQuestsOpen = false;
    bool bHelpOpen = false;
    bool bSkillsOpen = false;
    bool bPartyOpen = false;
    EFablePartyPanelTab PartyPanelTab = EFablePartyPanelTab::Members;
    FGuid PartyExpeditionAtOpen;
    FGuid PendingPartyRequestId;
    bool bPendingPartyEntry = false;
    bool bPartyPreparationSucceeded = false;
    FString PartyPreparationMessage;
    int32 PartyPreparationResultRevision = 0;
    FGuid PendingExpeditionActionId;
    FGuid LastExpeditionActionRequestId;
    TSet<FGuid> AutoOpenedCompletionIds;
    bool bExpeditionActionSucceeded = false;
    FGuid ResultPortalGuidanceExpeditionId;
    FString ExpeditionActionMessage;
    int32 ExpeditionActionResultRevision = 0;
    int32 SelectedResultItemIndex = 0;

    /** 보상 조회 없이 결과 창에 입력 초점만 둔다. */
    void OpenExpeditionResultPanel();

    /** 별도 결과 요청의 상관 ID와 안내를 준비한다. */
    bool BeginExpeditionAction(const FString& Message);

    /** 현재 개인 결과 리비전과 서버 거절 사유를 응답한다. */
    void ReplyExpeditionAction(FGuid RequestId, bool bSuccess, int32 NoticeBefore);

    /** 보상 조회와 확인은 각각 읽기·확인 경로만 호출한다. */
    UFUNCTION(Server, Reliable)
    void ServerRequestExpeditionResult(FGuid RequestId, FGuid ExpeditionId);

    /** 결과 확인으로 지급 함수를 재실행하지 않는다. */
    UFUNCTION(Server, Reliable)
    void ServerAcknowledgeExpeditionResult(FGuid RequestId, FGuid ExpeditionId, FGuid CompletionId,
                                           int32 ResultRevision);

    /** 재도전 준비는 원래 완료 원정과 명부를 서버에서 검증한다. */
    UFUNCTION(Server, Reliable)
    void ServerPrepareExpeditionRetry(FGuid RequestId, FGuid PartyId, int32 PreparationRevision, FGuid OldExpeditionId,
                                      FGuid CompletionId);

    /** 전원의 명시적 동의로 새 원정에 입장한다. */
    UFUNCTION(Server, Reliable)
    void ServerRetryExpedition(FGuid RequestId, FGuid PartyId, int32 PreparationRevision, FGuid OldExpeditionId,
                               FGuid CompletionId);

    /** 원래 완료 원정의 기존 귀환 포털 검사를 사용한다. */
    UFUNCTION(Server, Reliable)
    void ServerReturnExpeditionResult(FGuid RequestId, FGuid ExpeditionId, FGuid CompletionId);

    /** 철수는 원래 참가 증서의 전원 동의로만 처리한다. */
    UFUNCTION(Server, Reliable)
    void ServerSetAbandonConsent(FGuid RequestId, FGuid ExpeditionId, int32 DepartureRevision, bool bConsent);

    /** 결과 요청도 현재 소유 요청과 일치하는 서버 응답만 반영한다. */
    UFUNCTION(Client, Reliable)
    void ClientExpeditionActionResult(FGuid RequestId, bool bSuccess, int32 ResultRevision, const FString& Reason);

    int32 SelectedSkillSlot = 0;
    int32 SkillPanelTab = 0;
    int32 SelectedTalentTier = 2;
    EFableTalentPath SelectedTalentPath = EFableTalentPath::First;
    bool bBuildResetConfirmationPending = false;
    EFableClass PendingBuildResetClass = EFableClass::Warrior;
    int32 PendingBuildResetRevision = INDEX_NONE;
    int32 PendingBuildResetGold = 0;
    int32 PendingBuildResetRefund = 0;
    EFableQuestId SelectedQuestId = EFableQuestId::Story;
    bool bHasExplicitQuestSelection = false;
    EFableRegion SelectedDungeonId = EFableRegion::RootRuins;
    EFableDifficulty SelectedDifficulty = EFableDifficulty::Normal;
    EFableInventorySort InventorySort = EFableInventorySort::Acquired;
    bool bJoinAddressFocused = false;
    bool bAddressSelectAll = false;
    bool bFreeCursor = false;
    bool bReviveInputHeld = false;
    bool bUIInputBlocked = false;
    EFableDialogue Dialogue = EFableDialogue::None;
    EFableClass InitialClass = EFableClass::Warrior;
    FString JoinAddress = TEXT("127.0.0.1:7788");
    int32 SelectedItemId = INDEX_NONE;
    float LastRPGRequestTime = -10.0f;
    int32 ObservedNoticeSerial = INDEX_NONE;
    int32 DismissedNoticeSerial = INDEX_NONE;
    FString ObservedNoticeText;
    FString DismissedNoticeText;
    double NoticeObservedAt = -10.0;

    /** 현재 임시 안내를 닫고 확인한 알림 상태를 기록한다. */
    void DismissTransientNotice();

    /** 선택한 전직 시험을 다시 시작하도록 요청한다. */
    void RestartTrial();

    /** 플레이어의 마우스 커서 표시 상태를 전환한다. */
    void ToggleCursor();

    /** 열린 패널에 맞게 마우스 커서와 게임·UI 입력 모드를 조정한다. */
    void ApplyUIMode();

    /** 전사 선택을 적용한다. */
    void ChooseWarrior();

    /** 마법사 선택을 적용한다. */
    void ChooseMage();

    /** 궁수 선택을 적용한다. */
    void ChooseArcher();

    /** 현재 선택한 장비의 강화를 요청한다. */
    void EnhanceSelected();

    /** 현재 선택한 아이템의 판매를 요청한다. */
    void SellSelected();

    /** 서버 측 RPG 요청의 권한·상태·요청 간격을 확인한다. */
    bool AcceptRPGRequest();

    /** 소유 컨트롤러의 파티 생성 요청을 서버 파티 관리자에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerCreateParty();

    /** 대상 ID만 전달하고 초대 권한과 연결 상태는 서버에서 판정한다. */
    UFUNCTION(Server, Reliable)
    void ServerInviteParty(int32 TargetPlayerId);

    /** 초대 ID와 응답 의사를 전달하고 초대 소유 여부는 서버에서 판정한다. */
    UFUNCTION(Server, Reliable)
    void ServerRespondPartyInvite(FGuid InviteId, bool bAccept);

    /** 소유 컨트롤러의 현재 파티 탈퇴 요청을 서버에서 처리한다. */
    UFUNCTION(Server, Reliable)
    void ServerLeaveParty();

    /** 파티장 위임 권한과 대상의 현재 소속을 서버에서 검증한다. */
    UFUNCTION(Server, Reliable)
    void ServerTransferPartyLeader(int32 TargetPlayerId);

    /** 선택 요청은 요청자 소유권·파티장·현재 리비전을 서버에서 검증한다. */
    UFUNCTION(Server, Reliable)
    void ServerSelectPartyExpedition(FGuid RequestId, FGuid PartyId, int32 PreparationRevision, EFableRegion Region,
                                     EFableDifficulty Difficulty);

    /** 자기 준비 상태만 변경하며 다른 구성원의 준비를 대신 바꾸지 않는다. */
    UFUNCTION(Server, Reliable)
    void ServerSetPartyReady(FGuid RequestId, FGuid PartyId, int32 PreparationRevision, bool bReady);

    /** 서버가 전원 준비·이동을 검증한 뒤 출발 결과를 보낸다. */
    UFUNCTION(Server, Reliable)
    void ServerEnterPreparedParty(FGuid RequestId, FGuid PartyId, int32 PreparationRevision);

    /** 이미 발급된 자기 원정 참가 증서로 복귀하고 이동 결과를 확인한다. */
    UFUNCTION(Server, Reliable)
    void ServerResumePartyExpedition(FGuid RequestId, FGuid PartyId, int32 PreparationRevision);

    /** 해당 요청의 성공·거절 결과를 소유 클라이언트의 준비 화면에 반영한다. */
    UFUNCTION(Client, Reliable)
    void ClientPartyPreparationResult(FGuid RequestId, bool bSuccess, int32 PreparationRevision, const FString& Reason);

    /** 서버 요청 후 명부의 실제 리비전과 안내를 응답으로 전달한다. */
    void ReplyPartyPreparation(FGuid RequestId, bool bSuccess, int32 NoticeBefore);

    /** 장비 분해 확인 절차 상태를 해제한다. */
    void ClearDismantleConfirmation();

    /** 빌드 초기화 확인 상태를 해제한다. */
    void ClearBuildResetConfirmation();

    /** 분해 확인 대상의 상태 서명 값을 반환한다. */
    FString GetDismantleConfirmationSignature() const;

    /** 동료 부활 시작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerStartRevive(AFableCharacter* Target);

    /** 동료 부활 취소 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerCancelRevive();

    /** 전투 재도전 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerRetryEncounter();

    /** 전직 시험 재시작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerRestartTrial();

    /** 주변 대상 상호작용 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerInteract();

    /** 퀘스트 확인 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerConfirmQuest(EFableQuestId QuestId);

    /** 지역 입장 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerEnterRegion(EFableRegion Region, EFableDifficulty Difficulty);

    /** 직업 선택 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerChooseClass(EFableClass HeroClass);

    /** 장비 착용 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerEquip(int32 ItemId);

    /** 장비 강화 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerEnhance(int32 ItemId);

    /** 장비 제작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerCraft(int32 Rarity, EFableEquipmentSlot Slot);

    /** 장비 분해 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerDismantle(int32 ItemId);

    /** 옵션 재설정 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerRerollAffix(int32 ItemId, int32 AffixIndex);

    /** 내구도 수리 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerRepair(int32 ItemId);

    /** 창고 보관 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerDeposit(int32 ItemId);

    /** 창고 수령 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerWithdraw(int32 ItemId);

    /** 상점 구매 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerShopBuy(EFableShopOffer Offer);

    /** 아이템 판매 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerSell(int32 ItemId);

    /** 물약 사용 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerUsePotion(bool bMana);

    /** 스킬 강화 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerUpgradeSkill(int32 Slot);

    /** 특성 선택 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerSelectTalent(int32 TargetTier, EFableTalentPath Path);

    /** 현재 직업 빌드 초기화 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerResetCurrentClassBuild(EFableClass ExpectedClass, int32 ExpectedRevision);

    /** 전직 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
    UFUNCTION(Server, Reliable)
    void ServerAdvanceClass();

    /** 서버가 전달한 열림 대화 결과를 소유 클라이언트에 반영한다. */
    UFUNCTION(Client, Reliable)
    void ClientOpenDialogue(uint8 Service);
};
