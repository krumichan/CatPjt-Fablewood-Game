#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "FableRPGTypes.h"

class UFableOnlineFlowSubsystem;
class SEditableTextBox;
class SVerticalBox;

/** A viewport Slate screen; the password edit is masked and cleared on submission. */
class SFableOnlineWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFableOnlineWidget) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UFableOnlineFlowSubsystem>, Flow)
    SLATE_END_ARGS()
    void Construct(const FArguments& Arguments);
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;
private:
    void Rebuild();
    FReply Submit(bool bRegister);
    TSharedRef<SWidget> Button(const FString& Label, TFunction<void()> Action, bool bEnabled = true);
    TSharedRef<SWidget> Label(const FString& Text, float Size = 17.f);
    TWeakObjectPtr<UFableOnlineFlowSubsystem> Flow;
    TSharedPtr<SEditableTextBox> NameInput, PasswordInput;
    TSharedPtr<SVerticalBox> Body;
    FString DraftName;
    EFableClass InitialClass = EFableClass::Warrior;
    uint32 LastRevision = MAX_uint32;
};
