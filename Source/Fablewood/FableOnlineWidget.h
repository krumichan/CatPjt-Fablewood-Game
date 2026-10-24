#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "FableRPGTypes.h"

class UFableOnlineFlowSubsystem;
class SEditableTextBox;
class SVerticalBox;

/** A viewport Slate screen; the password edit is masked and cleared on submission. */
/** 로그인·회원 가입·서버 선택·캐릭터 정보를 표시하는 Slate 화면. */
class SFableOnlineWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFableOnlineWidget)
    {
    }
    SLATE_ARGUMENT(TWeakObjectPtr<UFableOnlineFlowSubsystem>, Flow)
    SLATE_END_ARGS()

    /** 온라인 화면의 입력 필드·버튼·상태 표시를 구성한다. */
    void Construct(const FArguments& Arguments);

    /** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;

    /** 온라인 화면이 키보드 입력 초점을 받을 수 있음을 반환한다. */
    virtual bool SupportsKeyboardFocus() const override
    {
        return true;
    }

    /** 온라인 화면의 확인·취소 키 입력을 처리한다. */
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent) override;

private:
    /** 현재 온라인 상태에 맞게 화면 내용을 다시 구성한다. */
    void Rebuild();

    /** 현재 온라인 화면 단계에 해당하는 사용자 요청을 제출한다. */
    FReply Submit(bool bRegister);

    /** 온라인 화면에서 사용할 동작 버튼을 만든다. */
    TSharedRef<SWidget> Button(const FString& Label, TFunction<void()> Action, bool bEnabled = true);

    /** 온라인 화면의 안내 문구를 만든다. */
    TSharedRef<SWidget> Label(const FString& Text, float Size = 17.f);
    TWeakObjectPtr<UFableOnlineFlowSubsystem> Flow;
    TSharedPtr<SEditableTextBox> NameInput, PasswordInput;
    TSharedPtr<SVerticalBox> Body;
    FString DraftName;
    EFableClass InitialClass = EFableClass::Warrior;
    uint32 LastRevision = MAX_uint32;
};
