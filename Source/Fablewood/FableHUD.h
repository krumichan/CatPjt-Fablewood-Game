#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "FableHUD.generated.h"
class AFablePlayerController;
class AFablePlayerState;
class AFableGameState;
class UFont;
UCLASS()
class FABLEWOOD_API AFableHUD : public AHUD
{
    GENERATED_BODY()
public:
    AFableHUD();
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
private:
    UPROPERTY() TObjectPtr<UFont> KoreanFont;
    UFont* UIFont() const;
    void DrawFrontEnd(AFablePlayerController* PC, float Scale);
    void DrawInventory(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);
    void DrawWorkshop(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);
    void DrawQuests(AFablePlayerController* PC, const AFablePlayerState* State, const AFableGameState* GameState, float Scale);
    void DrawDialogue(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);
    void DrawQuestSelection(AFablePlayerController* PC, const AFablePlayerState* State, float X, float Y, float Scale, bool bAtGuide);
    void DrawPortal(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);
    void DrawHelp(AFablePlayerController* PC, float Scale);
    void DrawSkills(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);
    void DrawTalents(AFablePlayerController* PC, const AFablePlayerState* State, float X, float Y, float Scale);
    void DrawBuildResetConfirmation(AFablePlayerController* PC, const AFablePlayerState* State, float X, float Y, float Scale);
    void DrawWorldLabels(float Scale);
    void DrawStoryTracker(AFablePlayerController* PC, const AFablePlayerState* State, float Scale);
    void DrawMiniMap(AFablePlayerController* PC, const AFablePlayerState* State, const AFableGameState* GameState, float Scale);
    void DrawPartyStatus(AFablePlayerController* PC, const AFableGameState* GameState, float Scale);
    void DrawRecoveryStatus(AFablePlayerController* PC, const AFableGameState* GameState, float Scale);
    void DrawPanel(const FString& Title, float X, float Y, float Width, float Height, float Scale);
    void DrawButton(const FString& Label, FName Name, float X, float Y, float Width, float Height, float Scale, bool bEnabled = true);
    void DrawFitted(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale);
    float DrawWrapped(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale, int32 MaxLines = 3);
    FString DestinationText(const AFablePlayerState* State, const AFableGameState* GameState) const;
};
