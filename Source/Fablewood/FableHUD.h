#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FableHUD.generated.h"
class AFablePlayerController;
class AFablePlayerState;
class AFableGameState;
class UFont;

/** 모험 안내·전투 상태·퀘스트·장비 UI를 그리는 HUD. */
UCLASS()
class FABLEWOOD_API AFableHUD : public AHUD
{
    GENERATED_BODY()

public:
    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    AFableHUD();

    /** HUD UI를 현재 상태에 맞게 그린다. */
    virtual void DrawHUD() override;

    /** 클릭한 HUD 버튼을 플레이어의 해당 UI 동작에 연결한다. */
    virtual void NotifyHitBoxClick(FName BoxName) override;

private:
    UPROPERTY()
    TObjectPtr<UFont> KoreanFont;

    int32 PartyRosterPage = 0;

    /** 한국어 표시를 지원하는 HUD 글꼴을 반환한다. */
    UFont* UIFont() const;

    /** 시작 화면 UI를 현재 상태에 맞게 그린다. */
    void DrawFrontEnd(AFablePlayerController* PC, float Scale);

    /** 인벤토리 UI를 현재 상태에 맞게 그린다. */
    void DrawInventory(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);

    /** 공방 UI를 현재 상태에 맞게 그린다. */
    void DrawWorkshop(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);

    /** 퀘스트 목록 UI를 현재 상태에 맞게 그린다. */
    void DrawQuests(AFablePlayerController* PC, const AFablePlayerState* State, const AFableGameState* GameState,
                    float Scale);

    /** 현재 열려 있는 대화 종류 UI를 현재 상태에 맞게 그린다. */
    void DrawDialogue(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);

    /** 퀘스트 선택 UI를 현재 상태에 맞게 그린다. */
    void DrawQuestSelection(AFablePlayerController* PC, const AFablePlayerState* State, float X, float Y, float Scale,
                            bool bAtGuide);

    /** 이동문 UI를 현재 상태에 맞게 그린다. */
    void DrawPortal(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);

    /** 도움말 UI를 현재 상태에 맞게 그린다. */
    void DrawHelp(AFablePlayerController* PC, float Scale);

    /** 스킬 UI를 현재 상태에 맞게 그린다. */
    void DrawSkills(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);

    /** 특성 UI를 현재 상태에 맞게 그린다. */
    void DrawTalents(AFablePlayerController* PC, const AFablePlayerState* State, float X, float Y, float Scale);

    /** 빌드 초기화 확인 UI를 현재 상태에 맞게 그린다. */
    void DrawBuildResetConfirmation(AFablePlayerController* PC, const AFablePlayerState* State, float X, float Y,
                                    float Scale);

    /** 월드 안내 UI를 현재 상태에 맞게 그린다. */
    void DrawWorldLabels(float Scale);

    /** 이야기 목표 UI를 현재 상태에 맞게 그린다. */
    void DrawStoryTracker(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);

    /** 미니맵 UI를 현재 상태에 맞게 그린다. */
    void DrawMiniMap(AFablePlayerController* PC, const AFablePlayerState* State, const AFableGameState* GameState,
                     float Scale);

    /** 파티 상태 UI를 현재 상태에 맞게 그린다. */
    void DrawPartyStatus(AFablePlayerController* PC, const AFableGameState* GameState, float Scale);

    /** 접속자 목록·초대장·파티 권한을 관리 창에 표시한다. */
    void DrawPartyManagement(AFablePlayerController* PC, const AFableGameState* GameState, float Scale);

    /** 서버 선택·개별 준비·파티장 출발을 원정 탭에 표시한다. */
    void DrawPartyExpedition(AFablePlayerController* PC, const AFableGameState* GameState, float Scale);

    /** 복구 상태 UI를 현재 상태에 맞게 그린다. */
    void DrawRecoveryStatus(AFablePlayerController* PC, const AFableGameState* GameState, float Scale);

    /** 창 UI를 현재 상태에 맞게 그린다. */
    void DrawPanel(const FString& Title, float X, float Y, float Width, float Height, float Scale);

    /** 버튼 UI를 현재 상태에 맞게 그린다. */
    void DrawButton(const FString& Label, FName Name, float X, float Y, float Width, float Height, float Scale,
                    bool bEnabled = true);

    /** 폭에 맞춘 문구 UI를 현재 상태에 맞게 그린다. */
    void DrawFitted(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale);

    /** 여러 줄 문구 UI를 현재 상태에 맞게 그린다. */
    float DrawWrapped(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale,
                      int32 MaxLines = 3);

    /** 현재 안내 목표의 이름과 거리를 표시 문구로 만든다. */
    FString DestinationText(const AFablePlayerState* State, const AFableGameState* GameState) const;
};
