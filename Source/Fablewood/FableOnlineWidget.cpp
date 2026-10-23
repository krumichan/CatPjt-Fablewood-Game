#include "FableOnlineWidget.h"
#include "FableOnlineFlowSubsystem.h"
#include "Engine/Font.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
FSlateFontInfo OnlineFont(UFableOnlineFlowSubsystem* Flow,float Size)
{
    return Flow&&Flow->GetUIFont()?FSlateFontInfo(Flow->GetUIFont(),Size):FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size);
}
}
void SFableOnlineWidget::Construct(const FArguments& Arguments)
{
    Flow=Arguments._Flow;
    ChildSlot
    [
        SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(.035f,.065f,.065f,1.f)).Padding(FMargin(28,18))
        [
            SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [SNew(STextBlock).Text(FText::FromString(TEXT("FABLEWOOD  ·  숲의 모험"))).Font(OnlineFont(Flow.Get(),28)).ColorAndOpacity(FLinearColor(.92f,.83f,.60f,1))]
            +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
            [SNew(STextBlock).Text(FText::FromString(TEXT("계정 하나에 캐릭터 하나 · 진행은 서버에 저장됩니다."))).Font(OnlineFont(Flow.Get(),15)).ColorAndOpacity(FLinearColor(.72f,.80f,.76f,1))]
            +SVerticalBox::Slot().FillHeight(1)
            [SNew(SScrollBox)+SScrollBox::Slot()[SNew(SBox).MaxDesiredWidth(760).HAlign(HAlign_Fill)[SAssignNew(Body,SVerticalBox)]]]
        ]
    ];
    Rebuild();
}
TSharedRef<SWidget> SFableOnlineWidget::Label(const FString& Text,float Size)
{
    return SNew(STextBlock).Text(FText::FromString(Text)).Font(OnlineFont(Flow.Get(),Size))
        .ColorAndOpacity(FLinearColor(.90f,.94f,.89f,1)).AutoWrapText(true);
}
TSharedRef<SWidget> SFableOnlineWidget::Button(const FString& Text,TFunction<void()> Action,bool Enabled)
{
    return SNew(SButton).IsEnabled(Enabled).ContentPadding(FMargin(16,9)).HAlign(HAlign_Center)
        .ButtonColorAndOpacity(FLinearColor(.20f,.35f,.29f,1))
        .OnClicked_Lambda([Action=MoveTemp(Action)](){Action();return FReply::Handled();})[Label(Text,17)];
}
FReply SFableOnlineWidget::Submit(bool bRegister)
{
    if(!Flow.IsValid()||!NameInput.IsValid()||!PasswordInput.IsValid())return FReply::Handled();
    DraftName=NameInput->GetText().ToString();FString Password=PasswordInput->GetText().ToString();
    PasswordInput->SetText(FText::GetEmpty());
    if(bRegister)Flow->Register(DraftName,Password,InitialClass);else Flow->Authenticate(DraftName,Password);
    Password.Reset();return FReply::Handled();
}
void SFableOnlineWidget::Rebuild()
{
    if(!Flow.IsValid()||!Body.IsValid())return;
    if(NameInput.IsValid())DraftName=NameInput->GetText().ToString();
    if(PasswordInput.IsValid())PasswordInput->SetText(FText::GetEmpty());
    NameInput.Reset();PasswordInput.Reset();Body->ClearChildren();LastRevision=Flow->GetViewRevision();
    const auto State=Flow->GetState();
    Body->AddSlot().AutoHeight().Padding(0,0,0,14)[Label(Flow->GetMessage(),18)];
    if(Flow->IsBusy())
    {
        const FString Detail=State==EFableOnlineState::Joining?TEXT("서버 접속과 캐릭터 저장 확인을 기다리고 있습니다. 실패하면 새 접속권으로 재시도할 수 있습니다.")
            :State==EFableOnlineState::Leaving?TEXT("저장 완료 응답을 받은 뒤 캐릭터 화면으로 이동합니다."):TEXT("잠시 기다려 주세요.");
        Body->AddSlot().AutoHeight().Padding(0,8)[Label(Detail,16)];return;
    }
    if(Flow->IsManagingConnection())
    {
        Body->AddSlot().AutoHeight().Padding(0,4)[Button(TEXT("저장 후 캐릭터 화면으로 나가기"),[Weak=Flow](){if(Weak.IsValid())Weak->RequestLeave();})];
        if(State==EFableOnlineState::Playing)Body->AddSlot().AutoHeight().Padding(0,4)[Button(TEXT("게임으로 돌아가기"),[Weak=Flow](){if(Weak.IsValid())Weak->ToggleInGameMenu();})];
        return;
    }
    if(!Flow->HasSession())
    {
        Body->AddSlot().AutoHeight().Padding(0,0,0,5)[Label(TEXT("계정 이름"),16)];
        Body->AddSlot().AutoHeight().Padding(0,0,0,12)
        [SAssignNew(NameInput,SEditableTextBox).Text(FText::FromString(DraftName)).HintText(FText::FromString(TEXT("영문·숫자·밑줄 3~32자"))).Font(OnlineFont(Flow.Get(),18))];
        Body->AddSlot().AutoHeight().Padding(0,0,0,5)[Label(TEXT("비밀번호"),16)];
        Body->AddSlot().AutoHeight().Padding(0,0,0,12)
        [SAssignNew(PasswordInput,SEditableTextBox).IsPassword(true).HintText(FText::FromString(TEXT("10~128자 · 이 기기에 저장하지 않습니다")))
            .Font(OnlineFont(Flow.Get(),18)).ClearKeyboardFocusOnCommit(false)
            .OnTextCommitted_Lambda([this](const FText&,ETextCommit::Type Type){if(Type==ETextCommit::OnEnter)Submit(false);})];
        Body->AddSlot().AutoHeight().Padding(0,0,0,5)
        [SNew(STextBlock).Font(OnlineFont(Flow.Get(),16)).ColorAndOpacity(FLinearColor(.9f,.94f,.89f,1))
            .Text_Lambda([this](){return FText::FromString(TEXT("가입 시 시작 직업: ")+FableRPG::AdvancedClassName(InitialClass,1));})];
        auto Classes=SNew(SHorizontalBox);
        for(int32 I=0;I<3;++I)
        {
            const EFableClass Class=EFableClass(I);
            Classes->AddSlot().FillWidth(1).Padding(0,0,6,0)[Button(FableRPG::AdvancedClassName(Class,1),[this,Class](){InitialClass=Class;})];
        }
        Body->AddSlot().AutoHeight().Padding(0,0,0,14)[Classes];
        Body->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("로그인"),[this](){Submit(false);})];
        Body->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("새 계정 만들기"),[this](){Submit(true);})];
        Body->AddSlot().AutoHeight().Padding(0,13,0,0)[Button(TEXT("기존 로컬 플레이"),[Weak=Flow](){if(Weak.IsValid())Weak->UseLocalMode();})];
        return;
    }
    const auto& Character=Flow->GetCharacter();
    if(!Character.PlayerId.IsEmpty())
    {
        FString Summary=Character.Name+TEXT("  ·  ");
        if(Character.bHasSummary||Character.bNewCharacter)Summary+=FString::Printf(TEXT("레벨 %d %s"),Character.Level,*FableRPG::AdvancedClassName(Character.HeroClass,Character.ClassTier));
        else Summary+=TEXT("레벨·전직 정보는 접속 후 확인");
        Body->AddSlot().AutoHeight().Padding(0,0,0,9)[Label(Summary,22)];
        if(Character.InventoryVersion==1)
        {
            Body->AddSlot().AutoHeight().Padding(0,0,0,5)[Label(FString::Printf(TEXT("골드 %d  ·  가방 %d개  ·  창고 %d개"),Character.Gold,Character.BagCount,Character.StashCount),17)];
            Body->AddSlot().AutoHeight().Padding(0,0,0,12)[Label((Character.EquippedWeapon.IsEmpty()?TEXT("장착 무기 없음"):Character.EquippedWeapon)+TEXT("  /  ")+(Character.EquippedArmor.IsEmpty()?TEXT("장착 갑옷 없음"):Character.EquippedArmor),16)];
        }
        else Body->AddSlot().AutoHeight().Padding(0,0,0,12)[Label(Character.bNewCharacter?TEXT("첫 접속 시 시작 장비가 준비됩니다."):TEXT("장비와 가방 정보는 접속 후 확인할 수 있습니다."),16)];
    }
    Body->AddSlot().AutoHeight().Padding(0,6)[Label(TEXT("서버 목록 · 연결 상태는 입장할 때 확인합니다."),16)];
    for(const auto& Server:Flow->GetServers())
    {
        const FString Id=Server.Id;const FString Text=(Id==Flow->GetSelectedServerId()?TEXT("●  "):TEXT("○  "))+Server.Name+TEXT("   ")+Server.Address;
        Body->AddSlot().AutoHeight().Padding(0,3)[Button(Text,[Weak=Flow,Id](){if(Weak.IsValid())Weak->SelectServer(Id);},State==EFableOnlineState::Character)];
    }
    if(State==EFableOnlineState::Disconnected||State==EFableOnlineState::Error)
        Body->AddSlot().AutoHeight().Padding(0,12,0,3)[Button(TEXT("다시 시도"),[Weak=Flow](){if(Weak.IsValid())Weak->Retry();})];
    else Body->AddSlot().AutoHeight().Padding(0,12,0,3)[Button(TEXT("선택한 서버에 입장"),[Weak=Flow](){if(Weak.IsValid())Weak->JoinSelectedServer();},!Flow->GetSelectedServerId().IsEmpty())];
    Body->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("캐릭터·서버 새로고침"),[Weak=Flow](){if(Weak.IsValid())Weak->RefreshCharacter();})];
    Body->AddSlot().AutoHeight().Padding(0,3)[Button(TEXT("로그아웃"),[Weak=Flow](){if(Weak.IsValid())Weak->Logout();})];
}
void SFableOnlineWidget::Tick(const FGeometry& Geometry,double CurrentTime,float DeltaTime)
{
    SCompoundWidget::Tick(Geometry,CurrentTime,DeltaTime);
    if(Flow.IsValid()&&LastRevision!=Flow->GetViewRevision())
    {
        Rebuild();
        // Tick runs during paint; measure the new children before this frame arranges them.
        MarkPrepassAsDirty();
        SlatePrepass(Geometry.GetAccumulatedLayoutTransform().GetScale());
    }
}
FReply SFableOnlineWidget::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
    if(Event.GetKey()==EKeys::Escape&&Flow.IsValid()&&Flow->GetState()==EFableOnlineState::Playing){Flow->ToggleInGameMenu();return FReply::Handled();}
    return SCompoundWidget::OnKeyDown(Geometry,Event);
}
