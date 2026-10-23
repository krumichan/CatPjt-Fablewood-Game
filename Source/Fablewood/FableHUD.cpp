#include "FableHUD.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "FableGameInstance.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/Pawn.h"
namespace
{
const FLinearColor Cream(0.97f,0.93f,0.82f);
const FLinearColor Ink(0.025f,0.055f,0.045f,0.97f);
const FLinearColor Mint(0.52f,0.86f,0.65f);
const FLinearColor Muted(0.70f,0.75f,0.67f);
const FLinearColor Gold(1.0f,0.77f,0.35f);
const FLinearColor Warning(1.0f,0.57f,0.39f);
constexpr int32 ItemsPerPage = 6;
FLinearColor RarityColor(int32 Rarity) { return Rarity>=4?Gold:Rarity==3?FLinearColor(.83f,.65f,1):Rarity==2?FLinearColor(.55f,.80f,1):Rarity==1?Mint:Cream; }
bool CanWear(const FFableItem& Item, const AFablePlayerState* State) { return Item.Slot == EFableEquipmentSlot::Armor || Item.HeroClass == State->HeroClass; }
FString Cooldown(float Value) { return Value > 0.05f ? FString::Printf(TEXT("%.1f초"),Value) : TEXT("준비"); }
}
AFableHUD::AFableHUD()
{
    static ConstructorHelpers::FObjectFinder<UFont> Font(TEXT("/Game/UI/Fonts/F_Korean.F_Korean"));
    KoreanFont = Font.Object;
}
UFont* AFableHUD::UIFont() const { return KoreanFont ? KoreanFont.Get() : (GEngine ? GEngine->GetMediumFont() : nullptr); }
void AFableHUD::DrawFitted(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale)
{
    FString Fitted = Text;
    float TW=0,TH=0; GetTextSize(Fitted,TW,TH,UIFont(),Scale);
    if (TW > Width)
    {
        while (!Fitted.IsEmpty())
        {
            Fitted.LeftChopInline(1); GetTextSize(Fitted+TEXT("…"),TW,TH,UIFont(),Scale);
            if (TW <= Width) break;
        }
        Fitted += TEXT("…");
    }
    DrawText(Fitted,Color,X,Y,UIFont(),Scale);
}
float AFableHUD::DrawWrapped(const FString& Text, FLinearColor Color, float X, float Y, float Width, float Scale, int32 MaxLines)
{
    FString Line;
    int32 Row=0;
    const float LineHeight=32*Scale;
    for (int32 I=0;I<Text.Len();++I)
    {
        const TCHAR Ch=Text[I];
        float TW=0,TH=0;
        const FString Candidate=Line+FString::Chr(Ch);
        GetTextSize(Candidate,TW,TH,UIFont(),Scale);
        if (Ch==TEXT('\n') || (!Line.IsEmpty() && TW>Width))
        {
            if (Row==MaxLines-1) { DrawFitted(Line+TEXT("…"),Color,X,Y+Row*LineHeight,Width,Scale); return (Row+1)*LineHeight; }
            DrawText(Line,Color,X,Y+Row*LineHeight,UIFont(),Scale); ++Row;
            Line.Empty();
            if (Ch!=TEXT('\n') && Ch!=TEXT(' ')) Line+=FString::Chr(Ch);
        }
        else Line=Candidate;
    }
    if (!Line.IsEmpty()) DrawFitted(Line,Color,X,Y+Row*LineHeight,Width,Scale);
    return (Row+1)*LineHeight;
}
void AFableHUD::DrawButton(const FString& Label,FName Name,float X,float Y,float Width,float Height,float Scale,bool bEnabled)
{
    float MX=0,MY=0;
    const bool bHover=PlayerOwner && PlayerOwner->GetMousePosition(MX,MY) && MX>=X && MX<X+Width && MY>=Y && MY<Y+Height;
    DrawRect(bEnabled ? (bHover ? FLinearColor(.24f,.42f,.29f,1) : FLinearColor(.13f,.27f,.19f,1)) : FLinearColor(.09f,.13f,.11f,1),X,Y,Width,Height);
    DrawRect(bEnabled ? Mint : Muted*.35f,X,Y,Width,2*Scale);
    DrawFitted(Label,bEnabled?Cream:Muted,X+10*Scale,Y+(Height-25*Scale)*.5f,Width-20*Scale,.72f*Scale);
    if (bEnabled) AddHitBox(FVector2D(X,Y),FVector2D(Width,Height),Name,true,20);
}
void AFableHUD::DrawPanel(const FString& Title,float X,float Y,float Width,float Height,float Scale)
{
    DrawRect(FLinearColor(0,0,0,.56f),0,0,Canvas->ClipX,Canvas->ClipY);
    DrawRect(Ink,X,Y,Width,Height);
    DrawRect(Mint,X,Y,Width,3*Scale);
    DrawFitted(Title,Cream,X+22*Scale,Y+16*Scale,Width-140*Scale,.95f*Scale);
    DrawButton(TEXT("닫기"),TEXT("ClosePanel"),X+Width-94*Scale,Y+14*Scale,74*Scale,35*Scale,Scale);
}
FString AFableHUD::DestinationText(const AFablePlayerState* State,const AFableGameState* GS) const
{
    if (!State || !PlayerOwner || !PlayerOwner->GetPawn()) return TEXT("모험가를 준비하고 있습니다");
    const FVector Here=PlayerOwner->GetPawn()->GetActorLocation();
    const EFableRegion Region=FableRPG::IsInRegion(EFableRegion::Moonwell,Here)?EFableRegion::Moonwell:
        FableRPG::IsInRegion(EFableRegion::RootRuins,Here)?EFableRegion::RootRuins:EFableRegion::Village;
    const AFablePlayerController* PC=Cast<AFablePlayerController>(PlayerOwner);
    FVector PrimaryTarget; FString PrimaryLabel;
    if (PC && PC->GetNavigationTarget(PrimaryTarget,PrimaryLabel))
    {
        FVector Delta=PrimaryTarget-Here; Delta.Z=0;
        if (Delta.Size()<=330.f) return PrimaryLabel+TEXT(" · 가까이 있음");
        const float Angle=FMath::FindDeltaAngleDegrees(PC->GetControlRotation().Yaw,Delta.Rotation().Yaw);
        const TCHAR* Direction=FMath::Abs(Angle)<25?TEXT("앞"):FMath::Abs(Angle)>150?TEXT("뒤"):Angle>0?TEXT("오른쪽"):TEXT("왼쪽");
        return FString::Printf(TEXT("%s · %s %.0fm"),*PrimaryLabel,Direction,Delta.Size()/100.f);
    }
    if (!PrimaryLabel.IsEmpty()) return PrimaryLabel;
    const EFableQuestId Quest=PC?PC->GetSelectedQuestId():EFableQuestId::Story;
    FVector Destination=FableRPG::BoardLocation(); FString Label=TEXT("안내인 로완");
    bool bInteract=true,bSeek=false;
    if (Region!=EFableRegion::Village)
    {
        Destination=FableRPG::RegionReturnPortal(Region); Label=TEXT("마을 귀환 포털");
        if (GS && GS->AdventureStage!=EFableAdventureStage::Cleared)
        {
            bSeek=true;
            if (Region==EFableRegion::Moonwell && GS->AdventureStage==EFableAdventureStage::Dungeon && GS->ActiveSealIndex>=0 && GS->ActiveSealIndex<3)
            {
                Destination=FableRPG::MoonSealLocation(GS->ActiveSealIndex);
                Label=FString::Printf(TEXT("봉인 %d"),GS->ActiveSealIndex+1);
                if (GS->RequiredEnemies>0 && GS->DefeatedEnemies>=GS->RequiredEnemies) bSeek=false;
                else bInteract=false;
            }
        }
    }
    else if (State->GetQuestStage(Quest)==EFableQuestStage::Ready) Label=TEXT("로완에게 선택 의뢰 보고");
    else if (State->ForestQuest==EFableQuestStage::Active) bSeek=true;
    else if ((Quest==EFableQuestId::Moonwell && State->MoonwellQuest==EFableQuestStage::Active)
        || (State->MoonwellQuest==EFableQuestStage::Active && State->DungeonQuest!=EFableQuestStage::Active))
    { Destination=FableRPG::PortalLocation(); Label=TEXT("포털 · 달빛 수원 선택"); }
    else if (State->DungeonQuest==EFableQuestStage::Active)
    { Destination=FableRPG::PortalLocation(); Label=TEXT("포털 · 뿌리 유적 선택"); }
    if (bSeek)
    {
        float Best=TNumericLimits<float>::Max();
        for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
        {
            if (!It->IsAlive() || !FableRPG::IsInRegion(Region,It->GetActorLocation())) continue;
            const float Distance=FVector::DistSquared2D(Here,It->GetActorLocation());
            if (Distance<Best)
            { Best=Distance; Destination=It->GetActorLocation(); Label=Region==EFableRegion::Village?TEXT("숲 수호자"):TEXT("현재 지역의 적"); bInteract=false; }
        }
    }
    FVector Delta=Destination-Here;Delta.Z=0;
    if (Delta.Size()<=330) return Label+(bInteract?TEXT(" · 가까이 있음 [E]"):TEXT(" · 가까이 있음"));
    const float Angle=FMath::FindDeltaAngleDegrees(PlayerOwner->GetControlRotation().Yaw,Delta.Rotation().Yaw);
    const TCHAR* Direction=FMath::Abs(Angle)<25?TEXT("앞"):FMath::Abs(Angle)>150?TEXT("뒤"):Angle>0?TEXT("오른쪽"):TEXT("왼쪽");
    return FString::Printf(TEXT("%s · %s %.0fm"),*Label,Direction,Delta.Size()/100);
}
void AFableHUD::DrawWorldLabels(float S)
{
    if (!PlayerOwner || !PlayerOwner->GetPawn()) return;
    const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const FVector Locations[]={FableRPG::BoardLocation(),FableRPG::ForgeLocation(),FableRPG::MerchantLocation(),FableRPG::PortalLocation(),
        FableRPG::RegionReturnPortal(EFableRegion::RootRuins),FableRPG::RegionReturnPortal(EFableRegion::Moonwell),
        FableRPG::MoonSealLocation(0),FableRPG::MoonSealLocation(1),FableRPG::MoonSealLocation(2)};
    const TCHAR* Names[]={TEXT("! 안내인 로완"),TEXT("대장장이 브람"),TEXT("상인 미라"),TEXT("원정 지역 포털"),
        TEXT("마을 귀환 포털"),TEXT("마을 귀환 포털"),TEXT("달빛 봉인 1"),TEXT("달빛 봉인 2"),TEXT("달빛 봉인 3")};
    const TCHAR* Roles[]={TEXT("의뢰 · 전직 시험"),TEXT("강화 · 공방 · 창고"),TEXT("물약 · 장비 거래"),TEXT("뿌리 유적 / 달빛 수원"),
        TEXT("원정 완료 후 이용"),TEXT("원정 완료 후 이용"),TEXT("적 처치 후 E 정화"),TEXT("적 처치 후 E 정화"),TEXT("적 처치 후 E 정화")};
    const FVector Here=PlayerOwner->GetPawn()->GetActorLocation();
    const AFablePlayerState* LabelState=PlayerOwner->GetPlayerState<AFablePlayerState>();
    // Test the entire label rectangle against the two fixed side cards.
    const auto OverlapsSideCard=[this,S](const FVector2D& Screen,float HalfWidth)
    {
        const float LabelLeft=Screen.X-HalfWidth*S,LabelRight=Screen.X+HalfWidth*S;
        const float LabelTop=Screen.Y-8*S,LabelBottom=Screen.Y+41*S;
        return (LabelLeft<184*S && LabelRight>14*S && LabelTop<364*S && LabelBottom>208*S)
            || (LabelLeft<Canvas->ClipX-14*S && LabelRight>Canvas->ClipX-184*S
                && LabelTop<337*S && LabelBottom>153*S);
    };
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Locations);++Index)
    {
        if (FVector::DistSquared2D(Here,Locations[Index])>FMath::Square(1500.0f)) continue;
        FVector2D Screen;
        if (!PlayerOwner->ProjectWorldLocationToScreen(Locations[Index]+FVector(0,0,220),Screen)) continue;
        if (Screen.X<90*S || Screen.X>Canvas->ClipX-90*S || Screen.Y<170*S || Screen.Y>Canvas->ClipY-180*S) continue;
        if (OverlapsSideCard(Screen,101.f)) continue;
        FString QuestRoleText=Roles[Index];
        if (Index>=6 && GS)
        {
            const int32 Seal=Index-6;
            if ((GS->MoonSealMask&(1<<Seal))!=0) QuestRoleText=LabelState && LabelState->StoryChapter==4 && (LabelState->StorySealMask&(1<<Seal))==0?
                TEXT("E 조사 · 내 이야기 기록"):TEXT("정화 완료");
            else if (GS->ActiveSealIndex!=Seal) QuestRoleText=TEXT("앞선 봉인 정화 필요");
            else if (GS->RequiredEnemies>0 && GS->DefeatedEnemies>=GS->RequiredEnemies) QuestRoleText=TEXT("E 봉인 정화");
        }
        DrawRect(FLinearColor(.02f,.05f,.04f,.85f),Screen.X-101*S,Screen.Y-8*S,202*S,49*S);
        DrawFitted(Names[Index],Index==0?Gold:Mint,Screen.X-91*S,Screen.Y-5*S,182*S,.68f*S);
        DrawFitted(QuestRoleText,Cream,Screen.X-91*S,Screen.Y+20*S,182*S,.56f*S);
    }
    const AFablePlayerState* State=PlayerOwner->GetPlayerState<AFablePlayerState>();
    if (!State) return;
    for (int32 Index=0;Index<5;++Index)
    {
        const FVector Location=FableRPG::StoryObjectLocation(Index);
        if (FVector::DistSquared2D(Here,Location)>FMath::Square(1500.f)) continue;
        FVector2D Screen;
        if (!PlayerOwner->ProjectWorldLocationToScreen(Location+FVector(0,0,190),Screen)) continue;
        if (Screen.X<110*S || Screen.X>Canvas->ClipX-110*S || Screen.Y<220*S || Screen.Y>Canvas->ClipY-180*S) continue;
        if (OverlapsSideCard(Screen,108.f)) continue;
        const bool bDone=(State->StoryObjectMask&(1<<Index))!=0;
        const bool bCanUse=State->CanUseStoryObject(Index);
        DrawRect(FLinearColor(.02f,.05f,.04f,.85f),Screen.X-108*S,Screen.Y-8*S,216*S,49*S);
        DrawFitted(FableRPG::StoryObjectName(Index),bCanUse?Gold:Muted,Screen.X-98*S,Screen.Y-5*S,196*S,.66f*S);
        DrawFitted(bDone?TEXT("확인 완료"):bCanUse?(Index<3?TEXT("가까이에서 E 수집"):TEXT("가까이에서 E 조사")):TEXT("Q 메인 이야기에서 조건 확인"),
            bCanUse?Mint:Muted,Screen.X-98*S,Screen.Y+20*S,196*S,.54f*S);
    }
}
void AFableHUD::DrawStoryTracker(AFablePlayerController* PC,const AFablePlayerState* State,float S)
{
    if (!State || PC->IsAnyPanelOpen()) return;
    const float X=14*S,Y=208*S,Width=170*S;
    DrawRect(Ink,X,Y,Width,156*S);
    DrawRect(State->IsStoryReady()?Gold:Mint,X,Y,Width,2*S);
    DrawFitted(State->StoryChapter>=5?TEXT("메인 이야기 · 완료"):TEXT("메인 이야기"),Gold,X+9*S,Y+7*S,Width-18*S,.57f*S);
    DrawWrapped(State->GetStoryTitle(),Cream,X+9*S,Y+31*S,Width-18*S,.55f*S,2);
    DrawWrapped(State->GetStoryObjective(),State->IsStoryReady()?Gold:Mint,X+9*S,Y+73*S,Width-18*S,.52f*S,3);
    DrawFitted(TEXT("Q · 이야기와 성장 안내"),Muted,X+9*S,Y+136*S,Width-18*S,.47f*S);
}
void AFableHUD::DrawMiniMap(AFablePlayerController* PC,const AFablePlayerState* State,const AFableGameState* GS,float S)
{
    if (!State || !PC->GetPawn() || PC->IsAnyPanelOpen()) return;
    const FVector Here=PC->GetPawn()->GetActorLocation();
    const EFableRegion Region=FableRPG::IsInRegion(EFableRegion::Moonwell,Here)?EFableRegion::Moonwell:
        FableRPG::IsInRegion(EFableRegion::RootRuins,Here)?EFableRegion::RootRuins:EFableRegion::Village;
    const FVector Center=Region==EFableRegion::Village?FVector(0,500,0):FableRPG::RegionCenter(Region);
    const float Extent=Region==EFableRegion::Village?3000.f:3700.f;
    const float X=Canvas->ClipX-184*S,Y=153*S,Width=170*S;
    const float MapX=X+8*S,MapY=Y+28*S,MapW=154*S,MapH=128*S;
    DrawRect(Ink,X,Y,Width,184*S);
    DrawFitted(FableRPG::RegionName(Region)+TEXT(" · 지도"),Cream,X+8*S,Y+5*S,154*S,.51f*S);
    DrawRect(FLinearColor(.07f,.16f,.12f),MapX,MapY,MapW,MapH);
    for (int32 Grid=1;Grid<4;++Grid)
    {
        DrawLine(MapX+MapW*Grid/4.f,MapY,MapX+MapW*Grid/4.f,MapY+MapH,FLinearColor(.13f,.25f,.18f),S);
        DrawLine(MapX,MapY+MapH*Grid/4.f,MapX+MapW,MapY+MapH*Grid/4.f,FLinearColor(.13f,.25f,.18f),S);
    }
    const auto Point=[Center,Extent,MapX,MapY,MapW,MapH,S](const FVector& Location)
    {
        return FVector2D(MapX+MapW*.5f+FMath::Clamp(float((Location.X-Center.X)/Extent),-.92f,.92f)*(MapW*.5f-4*S),
            MapY+MapH*.5f-FMath::Clamp(float((Location.Y-Center.Y)/Extent),-.92f,.92f)*(MapH*.5f-4*S));
    };
    FVector Objective; FString ObjectiveLabel;
    const bool bHasObjective=State->GetStoryTarget(Objective,ObjectiveLabel);
    const FVector2D Self=Point(Here);
    const FVector2D ObjectivePoint=bHasObjective?Point(Objective):FVector2D::ZeroVector;
    const auto Marker=[this,S,&Point,&Self,&ObjectivePoint,bHasObjective](const FVector& Location,const TCHAR* Label,FLinearColor Color)
    {
        const FVector2D P=Point(Location);
        const bool bCrowdedLabel=Label && *Label &&
            ((FMath::Abs(P.X-Self.X)<18*S && FMath::Abs(P.Y-Self.Y)<12*S)
            || (bHasObjective && FMath::Abs(P.X-ObjectivePoint.X)<18*S && FMath::Abs(P.Y-ObjectivePoint.Y)<12*S));
        const float MarkerRadius=(bCrowdedLabel?2.f:3.f)*S;
        DrawRect(Color,P.X-MarkerRadius,P.Y-MarkerRadius,2*MarkerRadius,2*MarkerRadius);
        if (Label && *Label && !bCrowdedLabel) DrawFitted(Label,Color,P.X+5*S,P.Y-8*S,35*S,.43f*S);
    };
    if (Region==EFableRegion::Village)
    {
        Marker(FableRPG::BoardLocation(),TEXT("로"),Mint);
        Marker(FableRPG::ForgeLocation(),TEXT("브"),Mint);
        Marker(FableRPG::MerchantLocation(),TEXT("미"),Mint);
        Marker(FableRPG::PortalLocation(),TEXT("문"),FLinearColor(.67f,.69f,1));
    }
    else Marker(FableRPG::RegionReturnPortal(Region),TEXT("문"),FLinearColor(.67f,.69f,1));
    if (Region==EFableRegion::Moonwell)
        for (int32 Index=0;Index<3;++Index)
            Marker(FableRPG::MoonSealLocation(Index),TEXT(""),GS && (GS->MoonSealMask&(1<<Index))!=0?Muted:Mint);
    for (int32 Index=0;Index<5;++Index)
        if (FableRPG::StoryObjectRegion(Index)==Region && State->CanUseStoryObject(Index))
            Marker(FableRPG::StoryObjectLocation(Index),TEXT(""),Gold*.65f);
    if (bHasObjective)
    {
        const FVector2D P=Point(Objective);
        DrawLine(P.X-6*S,P.Y,P.X,P.Y-6*S,Gold,2*S); DrawLine(P.X,P.Y-6*S,P.X+6*S,P.Y,Gold,2*S);
        DrawLine(P.X+6*S,P.Y,P.X,P.Y+6*S,Gold,2*S); DrawLine(P.X,P.Y+6*S,P.X-6*S,P.Y,Gold,2*S);
    }
    if (GS)
        for (APlayerState* Member:GS->PlayerArray)
            if (Member && Member!=State)
                if (const AFableCharacter* Ally=Cast<AFableCharacter>(Member->GetPawn()))
                    if (FableRPG::IsInRegion(Region,Ally->GetActorLocation()))
                        Marker(Ally->GetActorLocation(),TEXT(""),Ally->IsAlive()?FLinearColor(.43f,.74f,1):Warning);
    const float Yaw=FMath::DegreesToRadians(PC->GetPawn()->GetActorRotation().Yaw);
    const FVector2D Forward(FMath::Cos(Yaw),-FMath::Sin(Yaw)),Side(-Forward.Y,Forward.X);
    const FVector2D Tip=Self+Forward*7*S,Left=Self-Forward*4*S+Side*4*S,Right=Self-Forward*4*S-Side*4*S;
    DrawLine(Tip.X,Tip.Y,Left.X,Left.Y,Cream,2*S); DrawLine(Tip.X,Tip.Y,Right.X,Right.Y,Cream,2*S);
    DrawLine(Left.X,Left.Y,Right.X,Right.Y,Cream,2*S);
    DrawFitted(TEXT("나 / 동료 / 금색 목표"),Muted,X+8*S,Y+162*S,154*S,.45f*S);
}
void AFableHUD::DrawPartyStatus(AFablePlayerController* PC,const AFableGameState* GS,float S)
{
    const float Width=268*S,X=(Canvas->ClipX-Width)*.5f,Y=14*S;
    DrawRect(Ink,X,Y,Width,133*S);
    const AFablePlayerState* PartnerState=nullptr;
    if (GS)
        for (APlayerState* Member:GS->PlayerArray)
            if (Member && Member!=PC->GetPlayerState<AFablePlayerState>()) { PartnerState=Cast<AFablePlayerState>(Member); break; }
    if (!PartnerState)
    {
        DrawFitted(TEXT("동료 · 혼자 모험 중"),Cream,X+12*S,Y+7*S,244*S,.66f*S);
        DrawWrapped(TEXT("친구가 호스트 주소로 참가하면 여기에서 상태를 확인합니다."),Muted,X+12*S,Y+43*S,244*S,.59f*S,3);
        if (GS && GS->PartyRecoveryCount>0)
            DrawFitted(FString::Printf(TEXT("파티 전멸 %d회"),GS->PartyRecoveryCount),Gold,X+12*S,Y+108*S,244*S,.56f*S);
        return;
    }
    const AFableCharacter* Partner=Cast<AFableCharacter>(PartnerState->GetPawn());
    const AFableCharacter* Hero=Cast<AFableCharacter>(PC->GetPawn());
    DrawFitted(TEXT("동료 · ")+PartnerState->GetAdvancedClassName(),Cream,X+12*S,Y+7*S,244*S,.66f*S);
    if (!Partner)
    {
        DrawWrapped(TEXT("동료의 캐릭터를 불러오는 중입니다."),Muted,X+12*S,Y+43*S,244*S,.59f*S,2);
        return;
    }
    const FString Vital=Partner->IsDowned()?FString::Printf(TEXT("쓰러짐 · 구조 대기 %.0f초"),Partner->GetDownedTimeRemaining()):
        Partner->IsDefeated()?TEXT("전투 불능 · 마을 구조 대기"):
        FString::Printf(TEXT("체력 %.0f / %.0f"),Partner->GetHealth(),Partner->GetMaxHealth());
    DrawFitted(Vital,Partner->IsAlive()?Cream:Warning,X+12*S,Y+35*S,244*S,.60f*S);
    DrawRect(FLinearColor(.18f,.12f,.10f),X+12*S,Y+61*S,244*S,6*S);
    const float Fraction=Partner->IsDowned()?FMath::Clamp(Partner->GetDownedTimeRemaining()/25.f,0.f,1.f):
        FMath::Clamp(Partner->GetHealth()/FMath::Max(1.f,Partner->GetMaxHealth()),0.f,1.f);
    DrawRect(Partner->IsAlive()?Mint:Warning,X+12*S,Y+61*S,244*S*Fraction,6*S);
    const FVector Position=Partner->GetActorLocation();
    const EFableRegion Region=FableRPG::IsInRegion(EFableRegion::Moonwell,Position)?EFableRegion::Moonwell:
        FableRPG::IsInRegion(EFableRegion::RootRuins,Position)?EFableRegion::RootRuins:EFableRegion::Village;
    FString Location=FableRPG::RegionName(Region);
    if (Hero)
    {
        FVector Delta=Position-Hero->GetActorLocation();Delta.Z=0;
        const float Angle=FMath::FindDeltaAngleDegrees(PC->GetControlRotation().Yaw,Delta.Rotation().Yaw);
        const TCHAR* Direction=FMath::Abs(Angle)<25?TEXT("앞"):FMath::Abs(Angle)>150?TEXT("뒤"):Angle>0?TEXT("오른쪽"):TEXT("왼쪽");
        Location+=FString::Printf(TEXT(" · %s %.0fm"),Direction,Delta.Size()/100.f);
    }
    DrawFitted(Location,Muted,X+12*S,Y+75*S,244*S,.56f*S);
    FString Action=TEXT("생존 · 함께 모험 중");
    if (GS && GS->bPartyRecoveryPending) Action=TEXT("마을 구조를 기다리는 중");
    else if (Partner->GetReviver() || Partner->GetReviveTarget()) Action=FString::Printf(TEXT("구조 진행 %d%%"),FMath::RoundToInt(Partner->GetReviveProgress()*100));
    else if (Partner->IsDowned()) Action=(!Hero || !Hero->IsAlive())?TEXT("동료도 구조를 기다리는 중"):
        PC->GetNearbyReviveTarget()==Partner?TEXT("가까운 동료 · E 3초 홀드"):TEXT("동료에게 다가가 E 3초 홀드");
    else if (Partner->IsDefeated()) Action=TEXT("잠시 후 마을로 구조됩니다");
    else if (Partner->GetReviveProtectionRemaining()>0) Action=FString::Printf(TEXT("부활 보호 %.1f초"),Partner->GetReviveProtectionRemaining());
    else if (GS && GS->PartyRecoveryCount>0) Action=FString::Printf(TEXT("파티 전멸 %d회 · %s"),GS->PartyRecoveryCount,GS->bRetryAvailable?TEXT("재도전 가능"):TEXT("모험 중"));
    DrawFitted(Action,Partner->IsDowned()?Gold:Mint,X+12*S,Y+103*S,244*S,.56f*S);
}
void AFableHUD::DrawRecoveryStatus(AFablePlayerController* PC,const AFableGameState* GS,float S)
{
    if (PC->IsAnyPanelOpen()) return;
    const AFableCharacter* Hero=Cast<AFableCharacter>(PC->GetPawn());
    if (!Hero) return;
    FString Title,Body;
    bool bProgress=false;
    float Progress=0;
    if (GS && GS->bPartyRecoveryPending)
    {
        Title=FString::Printf(TEXT("파티 전멸 · 마을 구조까지 %.0f초"),GS->GetPartyRecoveryTimeRemaining());
        Body=FString::Printf(TEXT("전멸 %d회 · 성장 / 장비 / 의뢰는 유지됩니다.\n마을에서 재정비한 뒤 R로 재도전하세요."),GS->PartyRecoveryCount+1);
    }
    else if (Hero->IsDowned())
    {
        Title=FString::Printf(TEXT("쓰러졌습니다 · 구조 대기 %.0f초"),Hero->GetDownedTimeRemaining());
        Body=Hero->GetReviver()?TEXT("동료가 구조하고 있습니다. 잠시 기다려 주세요."):
            TEXT("동료가 2.3m 안에서 E를 3초 누르면 구조합니다.\n구조 시간이 끝나면 이번 전투에서 일어날 수 없습니다.");
        bProgress=Hero->GetReviver()!=nullptr;Progress=Hero->GetReviveProgress();
    }
    else if (Hero->IsDefeated())
    {
        Title=TEXT("전투 불능 · 마을 구조 대기");
        Body=TEXT("잠시 후 마을로 구조됩니다.\n동료의 전투는 계속됩니다.");
    }
    else if (Hero->GetReviveTarget())
    {
        Title=TEXT("동료 구조 중 · E를 계속 누르세요");
        Body=TEXT("이동 / 점프 / 공격 / 피격 / UI 열기 시 취소됩니다.");
        bProgress=true;Progress=Hero->GetReviveProgress();
    }
    if (Title.IsEmpty()) return;
    const float Width=520*S,X=(Canvas->ClipX-Width)*.5f,Y=230*S;
    DrawRect(Ink,X,Y,Width,112*S);
    DrawRect(Gold,X,Y,Width,3*S);
    DrawFitted(Title,Gold,X+14*S,Y+9*S,Width-28*S,.76f*S);
    DrawWrapped(Body,Cream,X+14*S,Y+44*S,Width-28*S,.61f*S,2);
    if (bProgress)
    {
        DrawRect(FLinearColor(.11f,.20f,.15f),X+14*S,Y+94*S,Width-96*S,8*S);
        DrawRect(Mint,X+14*S,Y+94*S,(Width-96*S)*FMath::Clamp(Progress,0.f,1.f),8*S);
        DrawFitted(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Progress*100)),Mint,X+Width-65*S,Y+87*S,51*S,.55f*S);
    }
}
void AFableHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || !PlayerOwner || !GEngine) return;
    AFablePlayerController* PC=Cast<AFablePlayerController>(PlayerOwner);
    if (!PC) return;
    const float W=Canvas->ClipX,H=Canvas->ClipY,S=FMath::Clamp(FMath::Min(W/900.0f,H/600.0f),.6f,1.7f);
    if (PC->IsFrontEndOpen()) { if (PC->IsHelpOpen()) DrawHelp(PC,S); else DrawFrontEnd(PC,S); return; }
    const AFablePlayerState* State=PC->GetPlayerState<AFablePlayerState>();
    const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const AFableCharacter* Hero=Cast<AFableCharacter>(PC->GetPawn());
    DrawWorldLabels(S);
    DrawPartyStatus(PC,GS,S);
    DrawRect(Ink,14*S,14*S,292*S,133*S);
    DrawFitted(State?FString::Printf(TEXT("%s · 레벨 %d"),*State->GetAdvancedClassName(),State->Level):TEXT("모험가 준비 중"),Cream,26*S,21*S,270*S,.81f*S);
    if (Hero)
    {
        DrawFitted(FString::Printf(TEXT("체력 %.0f / %.0f"),Hero->GetHealth(),Hero->GetMaxHealth()),Cream,26*S,51*S,260*S,.65f*S);
        DrawRect(FLinearColor(.18f,.12f,.1f,1),26*S,78*S,266*S,7*S);
        DrawRect(Mint,26*S,78*S,266*S*FMath::Clamp(Hero->GetHealth()/FMath::Max(1.f,Hero->GetMaxHealth()),0.f,1.f),7*S);
        const bool bUsesMana=Hero->GetSkillManaCost(0)>0 || Hero->GetSkillManaCost(1)>0 || Hero->GetSkillManaCost(2)>0;
        const FString Resource=bUsesMana?FString::Printf(TEXT("마나 %.0f / %.0f"),Hero->GetMana(),Hero->GetMaxMana()):TEXT("마나 소모 없음");
        DrawFitted(Resource,FLinearColor(.59f,.79f,1),26*S,91*S,264*S,.62f*S);
    }
    if (State)
    {
        DrawFitted(FString::Printf(TEXT("경험치 %d / %d"),State->Experience,State->GetExperienceRequired()),Muted,26*S,119*S,264*S,.54f*S);
        DrawRect(Ink,W-310*S,14*S,296*S,133*S);
        FString Stage=GS?FableRPG::StageName(GS->AdventureStage):TEXT("숲 마을");
        if (GS && GS->ActiveRegion!=EFableRegion::Village)
        {
            Stage=FableRPG::RegionName(GS->ActiveRegion)+TEXT(" · ")+FableRPG::DifficultyName(GS->ActiveDifficulty);
            if (GS->ActiveRegion==EFableRegion::Moonwell && GS->AdventureStage==EFableAdventureStage::Dungeon)
            {
                const int32 Seals=((GS->MoonSealMask&1)!=0)+((GS->MoonSealMask&2)!=0)+((GS->MoonSealMask&4)!=0);
                Stage+=FString::Printf(TEXT(" · %d/3"),Seals);
            }
            else if (GS->AdventureStage==EFableAdventureStage::Cleared) Stage+=TEXT(" · 완료");
        }
        DrawFitted(Stage,Gold,W-298*S,21*S,270*S,.65f*S);
        DrawFitted(FString::Printf(TEXT("골드 %d · 광석 %d"),State->Gold,State->Ore),Cream,W-298*S,52*S,270*S,.66f*S);
        DrawFitted(FString::Printf(TEXT("동료 %d / 2 · 공격 %.0f · 방어 %.0f"),GS?GS->PlayerArray.Num():1,State->GetAttackPower(),State->GetDefense()),Muted,W-298*S,80*S,270*S,.59f*S);
        DrawFitted(DestinationText(State,GS),Mint,W-298*S,111*S,270*S,.61f*S);
    }
    if (State && !PC->IsAnyPanelOpen())
    {
        const FFableItem* Weapon=State->FindItem(State->EquippedWeaponId);
        const FFableItem* Armor=State->FindItem(State->EquippedArmorId);
        DrawRect(Ink,14*S,153*S,204*S,47*S);
        const auto Wear=[this,S](const TCHAR* Label,const FFableItem* Item,float Y)
        {
            const bool bDamaged=Item && Item->Durability<Item->MaxDurability;
            const bool bBroken=Item && Item->IsBroken();
            DrawFitted(Item?FString::Printf(TEXT("%s %d/%d%s"),Label,Item->Durability,Item->MaxDurability,bBroken?TEXT(" · 파손"):bDamaged?TEXT(" · 마모"):TEXT("")):
                FString(Label)+TEXT(" 없음"),bBroken?Warning:bDamaged?Gold:Muted,24*S,Y,184*S,.53f*S);
        };
        Wear(TEXT("무기"),Weapon,157*S); Wear(TEXT("갑옷"),Armor,179*S);
    }
    DrawStoryTracker(PC,State,S);
    DrawMiniMap(PC,State,GS,S);
    if (GS && GS->AdventureStage==EFableAdventureStage::Boss && GS->BossMaxHealth>0)
    {
        const float X=(W-440*S)/2;
        DrawRect(Ink,X,159*S,440*S,59*S);
        DrawFitted(FString::Printf(TEXT("%s · %s · %.0f / %.0f"),GS->ActiveRegion==EFableRegion::Moonwell?TEXT("달빛 수호자"):TEXT("고대 수호자"),GS->BossHealth<GS->BossMaxHealth*.5f?TEXT("분노 2단계"):TEXT("1단계"),GS->BossHealth,GS->BossMaxHealth),Gold,X+12*S,164*S,418*S,.72f*S);
        DrawRect(FLinearColor(.22f,.12f,.10f),X+12*S,200*S,416*S,9*S);
        DrawRect(Warning,X+12*S,200*S,416*S*FMath::Clamp(GS->BossHealth/GS->BossMaxHealth,0.f,1.f),9*S);
    }
    DrawRecoveryStatus(PC,GS,S);
    if (!PC->IsAnyPanelOpen())
    {
        DrawRect(Ink,14*S,H-164*S,W-28*S,150*S);
        DrawFitted(PC->GetContextActionText(),Gold,26*S,H-158*S,W-52*S,.65f*S);
        if (Hero && State)
        {
            const TCHAR* Keys[]={TEXT("K"),TEXT("L"),TEXT("O")};
            const float CellWidth=(W-68*S)/3.0f;
            for(int32 Slot=0;Slot<3;++Slot)
            {
                const float BX=26*S+Slot*(CellWidth+8*S),BY=H-126*S;
                const bool bUnlocked=State->IsSkillUnlocked(Slot);
                const float Remaining=Hero->GetSkillCooldownRemaining(Slot);
                const float ManaCost=Hero->GetSkillManaCost(Slot);
                const bool bCanCast=Hero->IsAlive() && bUnlocked && Remaining<=.05f && Hero->GetMana()>=ManaCost;
                DrawRect(FLinearColor(.07f,.15f,.11f,1),BX,BY,CellWidth,51*S);
                DrawFitted(FString::Printf(TEXT("%s  %s · %d랭크"),Keys[Slot],*FableRPG::SkillName(State->HeroClass,Slot),State->GetSkillRank(Slot)),bUnlocked?Cream:Muted,BX+8*S,BY+3*S,CellWidth-16*S,.63f*S);
                const FString Status=!Hero->IsAlive()?TEXT("구조를 기다리는 중"):!bUnlocked?FString::Printf(TEXT("%d차 전직 시 해금"),Slot+1):
                    FString::Printf(TEXT("마나 %.0f · %s"),ManaCost,Hero->GetMana()<ManaCost?TEXT("마나 부족"):*Cooldown(Remaining));
                DrawFitted(Status,bCanCast?Mint:bUnlocked?Warning:Muted,BX+8*S,BY+28*S,CellWidth-16*S,.54f*S);
                const float ReadyFraction=Hero->IsAlive() && bUnlocked?1-FMath::Clamp(Remaining/FMath::Max(.01f,Hero->GetSkillCooldownDuration(Slot)),0.f,1.f):0.f;
                DrawRect(bCanCast?Mint:Warning,BX,BY+49*S,CellWidth*ReadyFraction,2*S);
            }
            const FString Protection=Hero->GetReviveProtectionRemaining()>0?FString::Printf(TEXT(" · 부활 보호 %.1f초"),Hero->GetReviveProtectionRemaining()):Hero->GetShieldAmount()>0?FString::Printf(TEXT(" · 보호막 %.0f (%.1f초)"),Hero->GetShieldAmount(),Hero->GetShieldTimeRemaining()):FString::Printf(TEXT(" · 물약 대기 %s"),*Cooldown(State->GetPotionCooldownRemaining()));
            DrawFitted(TEXT("J / 좌클릭 공격 · Shift 회피 · Space 점프")+Protection,Cream,26*S,H-66*S,W-52*S,.58f*S);
            DrawFitted(FString::Printf(TEXT("H 체력약 %d · M 마나약 %d · I 가방 · Q 의뢰 · P 스킬/특성 (%dP) · F1 도움말"),State->HealthPotions,State->ManaPotions,State->GetAvailableSkillPoints()),Mint,26*S,H-38*S,W-52*S,.57f*S);
        }
    }
    if (PC->IsWorkshopOpen() && State) DrawWorkshop(PC,State,S);
    else if (PC->IsInventoryOpen() && State) DrawInventory(PC,State,S);
    else if (PC->IsQuestJournalOpen() && State) DrawQuests(PC,State,GS,S);
    else if (PC->IsSkillsOpen() && State) DrawSkills(PC,State,S);
    else if (PC->IsDialogueOpen() && State) DrawDialogue(PC,State,S);
    else if (PC->IsHelpOpen()) DrawHelp(PC,S);
    const UFableGameInstance* GI = Cast<UFableGameInstance>(PC->GetGameInstance());
    const FString StorageError = GI ? GI->GetFrontEndError() : FString();
    const FString Notice = !StorageError.IsEmpty() ? StorageError : (State && PC->ShouldShowTransientNotice() ? State->LastNotice : FString());
    if (!Notice.IsEmpty())
    {
        const float BoxW=FMath::Min(W-28*S,840*S);
        const float Y=PC->IsAnyPanelOpen()?H-60*S:H-222*S;
        DrawRect(Ink,(W-BoxW)/2,Y,BoxW,49*S);
        DrawWrapped(Notice,StorageError.IsEmpty() && State && State->bLastSaveSucceeded?Cream:Warning,(W-BoxW)/2+12*S,Y+5*S,BoxW-24*S,.62f*S,2);
    }
}
void AFableHUD::DrawFrontEnd(AFablePlayerController* PC,float S)
{
    const float W=Canvas->ClipX,H=Canvas->ClipY,X=(W-850*S)/2,Y=(H-550*S)/2;
    const UFableGameInstance* GI=Cast<UFableGameInstance>(PC->GetGameInstance());
    DrawRect(FLinearColor(.015f,.04f,.025f,.78f),0,0,W,H);
    DrawRect(Ink,X,Y,850*S,550*S);
    DrawRect(Gold,X,Y,850*S,4*S);
    DrawFitted(TEXT("페이블우드"),Cream,X+28*S,Y+21*S,600*S,1.45f*S);
    DrawFitted(TEXT("숲 마을에서 시작하는 둘만의 작은 모험"),Mint,X+30*S,Y+75*S,760*S,.77f*S);
    DrawButton(TEXT("도움말"),TEXT("Help"),X+720*S,Y+24*S,105*S,38*S,S);
    DrawFitted(TEXT("새 모험의 직업"),Gold,X+30*S,Y+125*S,360*S,.78f*S);
    const TCHAR* Names[]={TEXT("1 전사"),TEXT("2 마법사"),TEXT("3 궁수")};
    const FName IDs[]={TEXT("StartClassWarrior"),TEXT("StartClassMage"),TEXT("StartClassArcher")};
    for(int32 I=0;I<3;++I)
    {
        const FString Label=(static_cast<int32>(PC->GetInitialClass())==I?TEXT("● "):TEXT(""))+FString(Names[I]);
        DrawButton(Label,IDs[I],X+(30+I*126)*S,Y+163*S,118*S,41*S,S);
    }
    const TCHAR* Description=PC->GetInitialClass()==EFableClass::Mage?TEXT("마법사 · 원거리 마법과 넓은 범위 기술"):
        PC->GetInitialClass()==EFableClass::Archer?TEXT("궁수 · 빠른 화살과 관통 사격"):TEXT("전사 · 가까운 적을 베고 회전 공격");
    DrawWrapped(Description,Muted,X+30*S,Y+217*S,370*S,.68f*S,2);
    DrawButton(TEXT("새 게임 · 혼자 시작"),TEXT("NewGame"),X+30*S,Y+280*S,366*S,44*S,S);
    DrawButton(TEXT("이어하기 · 혼자 시작"),TEXT("ContinueGame"),X+30*S,Y+334*S,366*S,44*S,S,GI && GI->HasContinueGame());
    DrawWrapped(GI?GI->GetContinueSummary():TEXT("저장 정보 준비 중"),Muted,X+30*S,Y+391*S,366*S,.64f*S,3);
    DrawRect(Muted*.24f,X+422*S,Y+126*S,1*S,336*S);
    DrawFitted(TEXT("친구와 협동"),Gold,X+448*S,Y+125*S,360*S,.78f*S);
    DrawButton(TEXT("새 모험 호스트"),TEXT("HostNewGame"),X+448*S,Y+163*S,175*S,41*S,S);
    DrawButton(TEXT("이어하기 호스트"),TEXT("HostContinueGame"),X+634*S,Y+163*S,187*S,41*S,S,GI && GI->HasContinueGame());
    DrawWrapped(TEXT("호스트가 먼저 시작한 뒤 친구가 주소로 참가합니다."),Muted,X+448*S,Y+217*S,370*S,.68f*S,2);
    DrawFitted(TEXT("호스트 주소  (IP 또는 이름:포트)"),Cream,X+448*S,Y+280*S,370*S,.68f*S);
    DrawRect(PC->IsJoinAddressFocused()?FLinearColor(.16f,.27f,.2f):FLinearColor(.07f,.12f,.09f),X+448*S,Y+316*S,373*S,43*S);
    DrawFitted(PC->GetJoinAddress()+(PC->IsJoinAddressFocused()?TEXT(" |"):TEXT("")),Cream,X+460*S,Y+324*S,349*S,.73f*S);
    AddHitBox(FVector2D(X+448*S,Y+316*S),FVector2D(373*S,43*S),TEXT("JoinAddress"),true,20);
    DrawFitted(TEXT("클릭 후 입력 · Ctrl+A 전체 선택 · Ctrl+V 붙여넣기"),Muted,X+448*S,Y+368*S,373*S,.55f*S);
    DrawButton(TEXT("주소로 참가"),TEXT("JoinGame"),X+448*S,Y+405*S,373*S,44*S,S);
    const FString Error=GI?GI->GetFrontEndError():FString();
    const bool bTravelling=GI && GI->IsTravelInProgress();
    DrawWrapped(bTravelling?TEXT("모험을 준비하고 있습니다. 잠시 기다려 주세요."):Error.IsEmpty()?TEXT("진행 상황은 호스트 PC에 저장됩니다. 처음에는 로완과 대화해 보세요."):Error,Error.IsEmpty()?Muted:Warning,X+30*S,Y+482*S,790*S,.69f*S,2);
}
void AFableHUD::DrawInventory(AFablePlayerController* PC,const AFablePlayerState* State,float S)
{
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    DrawPanel(TEXT("모험 가방 · 장비 비교"),X,Y,850*S,510*S,S);
    DrawFitted(FString::Printf(TEXT("골드 %d · 광석 %d · 체력약 %d · 마나약 %d"),State->Gold,State->Ore,State->HealthPotions,State->ManaPotions),Gold,X+22*S,Y+61*S,800*S,.68f*S);
    const TArray<int32> DisplayIds=PC->GetInventoryDisplayIds();
    const int32 SelectedId=PC->GetSelectedItemId();
    const int32 SelectedIndex=DisplayIds.IndexOfByKey(SelectedId);
    const int32 First=FMath::Max(0,SelectedIndex)/ItemsPerPage*ItemsPerPage;
    DrawButton(TEXT("Tab 정렬 · ")+PC->GetInventorySortLabel(),TEXT("SortInventory"),X+22*S,Y+96*S,320*S,30*S,S);
    for(int32 Row=0;Row<ItemsPerPage && First+Row<DisplayIds.Num();++Row)
    {
        const FFableItem* Listed=State->FindItem(DisplayIds[First+Row]);
        if (!Listed) continue;
        const FFableItem& Item=*Listed;
        const float RY=Y+(134+Row*46)*S;
        const bool bEquipped=Item.Id==State->EquippedWeaponId || Item.Id==State->EquippedArmorId;
        DrawRect(Item.Id==SelectedId?FLinearColor(.18f,.33f,.23f,1):FLinearColor(.065f,.12f,.09f,1),X+22*S,RY,320*S,42*S);
        DrawFitted((bEquipped?TEXT("● "):TEXT(""))+Item.GetDisplayName(),RarityColor(Item.Rarity),X+30*S,RY+2*S,304*S,.61f*S);
        DrawFitted(FString::Printf(TEXT("%s · %s · %s %d"),*FableRPG::RarityName(Item.Rarity),Item.Slot==EFableEquipmentSlot::Armor?TEXT("공용"):*FableRPG::ClassName(Item.HeroClass),Item.Slot==EFableEquipmentSlot::Armor?TEXT("방어"):TEXT("공격"),Item.GetEffectivePower()),Muted,X+30*S,RY+25*S,304*S,.51f*S);
        AddHitBox(FVector2D(X+22*S,RY),FVector2D(320*S,42*S),FName(*FString::Printf(TEXT("Item_%d"),Item.Id)),true,20);
    }
    DrawButton(TEXT("↑ 이전"),TEXT("PreviousItem"),X+22*S,Y+418*S,94*S,32*S,S,SelectedIndex>0);
    DrawButton(TEXT("↓ 다음"),TEXT("NextItem"),X+125*S,Y+418*S,94*S,32*S,S,SelectedIndex>=0 && SelectedIndex<DisplayIds.Num()-1);
    DrawFitted(FString::Printf(TEXT("%d / %d개"),FMath::Max(0,SelectedIndex+1),DisplayIds.Num()),Muted,X+230*S,Y+425*S,112*S,.55f*S);
    const FFableItem* Item=State->FindItem(SelectedId);
    if (Item)
    {
        const float DX=X+362*S,DW=464*S;
        const FFableItem* Worn=State->FindItem(Item->Slot==EFableEquipmentSlot::Weapon?State->EquippedWeaponId:State->EquippedArmorId);
        DrawFitted(TEXT("현재 장착: ")+(Worn?Worn->GetDisplayName():TEXT("없음")),Muted,X+22*S,Y+461*S,320*S,.54f*S);
        DrawFitted(Item->GetDisplayName(),RarityColor(Item->Rarity),DX,Y+96*S,DW,.79f*S);
        DrawFitted(TEXT("등급 ")+FableRPG::RarityName(Item->Rarity)+TEXT(" · 품질 ")+FableRPG::QualityName(Item->Quality)+TEXT(" · ")+(Item->Slot==EFableEquipmentSlot::Armor?TEXT("모든 직업"):FableRPG::ClassName(Item->HeroClass)),Cream,DX,Y+126*S,DW,.60f*S);
        DrawFitted(FString::Printf(TEXT("기본 %s %d + 강화 %d = %d  (+%d / +5)"),Item->Slot==EFableEquipmentSlot::Armor?TEXT("방어"):TEXT("공격"),Item->Power,Item->GetEffectivePower()-Item->Power,Item->GetEffectivePower(),Item->Upgrade),Mint,DX,Y+151*S,DW,.61f*S);
        int32 Minimum=0,Maximum=0;FableRPG::GetItemPowerRange(*Item,Minimum,Maximum);
        DrawFitted(Item->RollVersion==0?TEXT("고정 성능 · 생성된 기본 수치를 유지합니다"):
            FString::Printf(TEXT("기준 %d × %d%% · 이 품질의 기본 성능 범위 %d~%d"),Item->NominalPower,Item->RollPercent,Minimum,Maximum),Muted,DX,Y+176*S,DW,.54f*S);
        FFableEquipmentStats Before,After;
        const bool bPreview=State->GetEquipmentPreview(Item->Id,Before,After);
        DrawFitted(FString::Printf(TEXT("내구도 %d/%d · %s"),Item->Durability,Item->MaxDurability,
            Item->IsBroken()?TEXT("파손 · 장비 효과 중지"):TEXT("정상 · 아래는 현재 → 장착 예상")),Item->IsBroken()?Warning:Gold,DX,Y+200*S,DW,.55f*S);
        if (bPreview)
        {
            const auto Stat=[this,S](const TCHAR* Label,float Current,float Next,float AtX,float AtY,bool bDecimal)
            {
                const float Delta=Next-Current;
                const FString Text=bDecimal?FString::Printf(TEXT("%s %.1f→%.1f (%+.1f)"),Label,Current,Next,Delta):
                    FString::Printf(TEXT("%s %.0f→%.0f (%+.0f)"),Label,Current,Next,Delta);
                DrawFitted(Text,Delta>.001f?Mint:Delta<-.001f?Warning:Cream,AtX,AtY,226*S,.54f*S);
            };
            Stat(TEXT("공격"),Before.Attack,After.Attack,DX,Y+221*S,false);
            Stat(TEXT("방어"),Before.Defense,After.Defense,DX+238*S,Y+221*S,false);
            Stat(TEXT("추가HP"),Before.MaxHealth,After.MaxHealth,DX,Y+243*S,false);
            Stat(TEXT("마나/초"),Before.ManaRegen,After.ManaRegen,DX+238*S,Y+243*S,true);
        }
        else DrawWrapped(TEXT("다른 직업 무기 · 현재 직업으로 장착 / 비교 불가\n로완에게 직업을 바꾸면 장착할 수 있습니다."),Warning,DX,Y+225*S,DW,.56f*S,2);
        for(int32 Index=0;Index<3;++Index)
            DrawFitted(Item->Affixes.IsValidIndex(Index)?TEXT("추가 옵션 · ")+FableRPG::AffixDescription(Item->Affixes[Index]):
                Index==0?TEXT("추가 옵션 없음"):FString(),Item->Affixes.IsValidIndex(Index) && !Item->IsBroken()?Mint:Muted,DX,Y+(269+Index*20)*S,DW,.55f*S);
        const EFableEquipmentSet ShownSet=Item->SetId!=EFableEquipmentSet::None?Item->SetId:Worn?Worn->SetId:EFableEquipmentSet::None;
        if (ShownSet!=EFableEquipmentSet::None)
        {
            const int32 CurrentPieces=State->GetEquippedSetCount(ShownSet);
            const int32 NextPieces=bPreview?State->GetEquippedSetCount(ShownSet,Item->Id):CurrentPieces;
            const bool bLosesOtherSet=bPreview && Worn && Worn->SetId!=EFableEquipmentSet::None && Worn->SetId!=ShownSet
                && State->GetEquippedSetCount(Worn->SetId)>=2 && State->GetEquippedSetCount(Worn->SetId,Item->Id)<2;
            const FString SetStatus=bPreview?FString::Printf(TEXT("%s %d/2 → %d/2 · %s"),*FableRPG::SetName(ShownSet),CurrentPieces,NextPieces,NextPieces>=2?TEXT("활성"):TEXT("미활성")):
                FableRPG::SetName(ShownSet)+TEXT(" · 다른 직업 무기 / 비교 불가");
            DrawFitted((bLosesOtherSet?TEXT("기존 2세트 해제 · "):TEXT(""))+SetStatus,NextPieces>=2?Gold:Muted,DX,Y+335*S,DW,.54f*S);
            DrawFitted(FableRPG::SetDescription(ShownSet),NextPieces>=2?Mint:Muted,DX,Y+356*S,DW,.53f*S);
        }
        else DrawFitted(TEXT("세트 효과 없음"),Muted,DX,Y+335*S,DW,.55f*S);
        const bool bWorn=Item->Id==State->EquippedWeaponId || Item->Id==State->EquippedArmorId;
        const int32 Price=State->GetSellPrice(Item->Id);
        DrawButton(bWorn?TEXT("장착 중"):TEXT("E 장착"),TEXT("EquipItem"),DX,Y+383*S,220*S,34*S,S,PC->IsNearTownService() && CanWear(*Item,State) && !bWorn);
        DrawButton(Price>0?FString::Printf(TEXT("X 판매 · %d골드"),Price):TEXT("판매 불가"),TEXT("SellItem"),DX+232*S,Y+383*S,232*S,34*S,S,PC->IsNearMerchant() && Price>0);
        const int32 GoldCost=State->GetUpgradeGoldCost(Item->Id),OreCost=State->GetUpgradeOreCost(Item->Id);
        DrawFitted(Item->Upgrade>=5?TEXT("최대 강화 · 품질 / 옵션은 그대로 유지됩니다"):
            FString::Printf(TEXT("다음 강화 %d골드 · 광석 %d / 품질·옵션 유지"),GoldCost,OreCost),Gold,DX,Y+423*S,DW,.54f*S);
        DrawButton(TEXT("U 강화 · 확정 성공"),TEXT("EnhanceItem"),DX,Y+448*S,DW,34*S,S,PC->IsNearForge() && Item->Upgrade<5 && State->Gold>=GoldCost && State->Ore>=OreCost);
    }
    DrawFitted(TEXT("↑↓ 선택 · Tab 정렬 · E 장착(로완/브람) · U 강화(브람) · X 판매(미라) · I / Esc 닫기"),Muted,X+22*S,Y+489*S,804*S,.53f*S);
}
void AFableHUD::DrawWorkshop(AFablePlayerController* PC,const AFablePlayerState* State,float S)
{
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    const float DX=X+362*S,DW=464*S;
    const EFableWorkshopTab Tab=PC->GetWorkshopTab();
    const TCHAR* Tabs[]={TEXT("제작"),TEXT("분해"),TEXT("옵션 변경"),TEXT("수리"),TEXT("개인 창고")};
    DrawPanel(TEXT("브람의 공방"),X,Y,850*S,510*S,S);
    DrawFitted(FString::Printf(TEXT("골드 %d · 광석 %d · 정수 %d · 뿌리 핵 %d · 달빛 결정 %d"),State->Gold,State->Ore,State->Essence,State->RootCores,State->MoonCrystals),Gold,X+24*S,Y+63*S,800*S,.59f*S);
    for(int32 Index=0;Index<5;++Index)
    {
        const FString Label=(int32(Tab)==Index?TEXT("● "):TEXT(""))+FString(Tabs[Index]);
        DrawButton(Label,FName(*FString::Printf(TEXT("WorkshopTab_%d"),Index)),X+(24+Index*162)*S,Y+96*S,154*S,32*S,S);
    }
    const bool bCraft=Tab==EFableWorkshopTab::Craft;
    const bool bStash=Tab==EFableWorkshopTab::Stash;
    const bool bWithdraw=bStash && PC->IsStashWithdrawal();
    const int32 SelectedId=PC->GetSelectedWorkshopItemId();
    const FFableItem* Item=bWithdraw?State->FindStashItem(SelectedId):State->FindItem(SelectedId);
    if (bCraft)
    {
        DrawButton((PC->GetCraftSlot()==EFableEquipmentSlot::Weapon?TEXT("● "):TEXT(""))+FString(TEXT("F 무기")),TEXT("CraftWeapon"),X+24*S,Y+146*S,154*S,32*S,S);
        DrawButton((PC->GetCraftSlot()==EFableEquipmentSlot::Armor?TEXT("● "):TEXT(""))+FString(TEXT("F 갑옷")),TEXT("CraftArmor"),X+190*S,Y+146*S,154*S,32*S,S);
        for(int32 Grade=1;Grade<=4;++Grade)
        {
            const float RY=Y+(190+(Grade-1)*51)*S;
            DrawRect(PC->GetCraftRarity()==Grade?FLinearColor(.18f,.34f,.24f):FLinearColor(.065f,.12f,.09f),X+24*S,RY,320*S,43*S);
            DrawFitted(FString::Printf(TEXT("%d  %s"),Grade,*FableRPG::RarityName(Grade)),RarityColor(Grade),X+34*S,RY+3*S,300*S,.68f*S);
            DrawFitted(FString::Printf(TEXT("기준 %s %d · 최대 내구 %d"),PC->GetCraftSlot()==EFableEquipmentSlot::Weapon?TEXT("공격"):TEXT("방어"),FableRPG::CraftNominalPower(Grade,PC->GetCraftSlot()),FableRPG::GradeMaxDurability(Grade)),Muted,X+34*S,RY+26*S,300*S,.51f*S);
            AddHitBox(FVector2D(X+24*S,RY),FVector2D(320*S,43*S),FName(*FString::Printf(TEXT("CraftGrade_%d"),Grade)),true,20);
        }
        DrawWrapped(TEXT("1~4 등급 · F 무기 / 갑옷\n무기는 현재 직업으로 제작합니다."),Muted,X+24*S,Y+412*S,320*S,.56f*S,2);
        const int32 Grade=PC->GetCraftRarity();
        DrawFitted(FableRPG::RarityName(Grade)+TEXT(" ")+(PC->GetCraftSlot()==EFableEquipmentSlot::Weapon?FableRPG::ClassName(State->HeroClass)+TEXT(" 무기"):TEXT("공용 갑옷")),RarityColor(Grade),DX,Y+147*S,DW,.82f*S);
        DrawFitted(FString::Printf(TEXT("기준 성능 %d · 내구도 %d/%d"),FableRPG::CraftNominalPower(Grade,PC->GetCraftSlot()),FableRPG::GradeMaxDurability(Grade),FableRPG::GradeMaxDurability(Grade)),Cream,DX,Y+183*S,DW,.63f*S);
        DrawFitted(TEXT("품질 하급 ~ 최상급 · 추가 옵션 1~3개"),Mint,DX,Y+215*S,DW,.63f*S);
        DrawFitted(TEXT("일반 확률 30 / 40 / 23 / 7% · 세트 없음"),Muted,DX,Y+244*S,DW,.58f*S);
        DrawWrapped(TEXT("품질과 옵션은 제작 시 정해집니다. 완성품은 가방으로 들어갑니다."),Cream,DX,Y+277*S,DW,.60f*S,2);
    }
    else
    {
        if (bStash)
        {
            DrawButton((!bWithdraw?TEXT("● "):TEXT(""))+FString(TEXT("F 보관")),TEXT("StashDepositMode"),X+24*S,Y+146*S,154*S,32*S,S);
            DrawButton((bWithdraw?TEXT("● "):TEXT(""))+FString(TEXT("F 꺼내기")),TEXT("StashWithdrawMode"),X+190*S,Y+146*S,154*S,32*S,S);
        }
        else DrawFitted(FString::Printf(TEXT("가방 장비 · %d / 32개"),State->Inventory.Num()),Cream,X+24*S,Y+150*S,320*S,.64f*S);
        const TArray<int32> Ids=PC->GetWorkshopDisplayIds();
        const int32 SelectedIndex=Ids.IndexOfByKey(SelectedId);
        const int32 First=FMath::Max(0,SelectedIndex)/5*5;
        for(int32 Row=0;Row<5 && First+Row<Ids.Num();++Row)
        {
            const FFableItem* Listed=bWithdraw?State->FindStashItem(Ids[First+Row]):State->FindItem(Ids[First+Row]);
            if (!Listed) continue;
            const float RY=Y+(185+Row*45)*S;
            const bool bWorn=Listed->Id==State->EquippedWeaponId || Listed->Id==State->EquippedArmorId;
            DrawRect(Listed->Id==SelectedId?FLinearColor(.18f,.34f,.24f):FLinearColor(.065f,.12f,.09f),X+24*S,RY,320*S,41*S);
            DrawFitted((bWorn?TEXT("● "):TEXT(""))+Listed->GetDisplayName(),Listed->IsBroken()?Warning:RarityColor(Listed->Rarity),X+34*S,RY+2*S,300*S,.59f*S);
            DrawFitted(FString::Printf(TEXT("%s · 내구 %d/%d%s"),*FableRPG::RarityName(Listed->Rarity),Listed->Durability,Listed->MaxDurability,Listed->IsBroken()?TEXT(" · 파손"):TEXT("")),Listed->IsBroken()?Warning:Muted,X+34*S,RY+25*S,300*S,.50f*S);
            AddHitBox(FVector2D(X+24*S,RY),FVector2D(320*S,41*S),FName(*FString::Printf(TEXT("WorkshopItem_%d"),Listed->Id)),true,20);
        }
        if (Ids.IsEmpty()) DrawWrapped(bWithdraw?TEXT("창고가 비어 있습니다. 가방에서 장비를 보관해 보세요."):TEXT("가방에 장비가 없습니다."),Muted,X+34*S,Y+206*S,300*S,.64f*S,3);
        DrawButton(TEXT("↑ 이전"),TEXT("PreviousItem"),X+24*S,Y+421*S,96*S,31*S,S,SelectedIndex>0);
        DrawButton(TEXT("↓ 다음"),TEXT("NextItem"),X+130*S,Y+421*S,96*S,31*S,S,SelectedIndex>=0 && SelectedIndex<Ids.Num()-1);
        DrawFitted(FString::Printf(TEXT("%d / %d"),FMath::Max(0,SelectedIndex+1),Ids.Num()),Muted,X+238*S,Y+427*S,106*S,.56f*S);
        DrawFitted(FString::Printf(TEXT("가방 %d/32 · 개인 창고 %d/48"),State->Inventory.Num(),State->StashItems.Num()),Muted,X+24*S,Y+464*S,320*S,.54f*S);
        if (Item)
        {
            DrawFitted(Item->GetDisplayName(),Item->IsBroken()?Warning:RarityColor(Item->Rarity),DX,Y+147*S,DW,.77f*S);
            DrawFitted(TEXT("등급 ")+FableRPG::RarityName(Item->Rarity)+TEXT(" · 품질 ")+FableRPG::QualityName(Item->Quality)+TEXT(" · ")+(Item->Slot==EFableEquipmentSlot::Armor?TEXT("공용"):FableRPG::ClassName(Item->HeroClass)),Cream,DX,Y+179*S,DW,.58f*S);
            DrawFitted(FString::Printf(TEXT("내구도 %d/%d · %s"),Item->Durability,Item->MaxDurability,Item->IsBroken()?TEXT("파손 · 장비 효과 중지"):TEXT("정상")),Item->IsBroken()?Warning:Mint,DX,Y+205*S,DW,.59f*S);
            DrawFitted(FString::Printf(TEXT("본래 %s %d + 강화 %d = %d (+%d)"),Item->Slot==EFableEquipmentSlot::Armor?TEXT("방어"):TEXT("공격"),Item->Power,Item->GetEffectivePower()-Item->Power,Item->GetEffectivePower(),Item->Upgrade),Muted,DX,Y+231*S,DW,.57f*S);
            if (Tab==EFableWorkshopTab::Reroll)
            {
                for(int32 Index=0;Index<Item->Affixes.Num() && Index<3;++Index)
                    DrawButton(FString::Printf(TEXT("%s%d %s"),PC->GetSelectedAffixIndex()==Index?TEXT("● "):TEXT(""),Index+1,*FableRPG::AffixDescription(Item->Affixes[Index])),FName(*FString::Printf(TEXT("WorkshopAffix_%d"),Index)),DX,Y+(260+Index*29)*S,DW,26*S,S);
                if (Item->Affixes.IsEmpty()) DrawFitted(TEXT("변경할 추가 옵션이 없습니다."),Muted,DX,Y+270*S,DW,.64f*S);
            }
            else if (Tab==EFableWorkshopTab::Repair)
            {
                DrawFitted(FString::Printf(TEXT("수리하면 내구도 %d → %d"),Item->Durability,Item->MaxDurability),Gold,DX,Y+270*S,DW,.68f*S);
                DrawWrapped(TEXT("파손 장비도 수리할 수 있습니다. 원래 성능·옵션·세트·강화는 유지됩니다."),Cream,DX,Y+306*S,DW,.60f*S,2);
            }
            else if (bStash)
            {
                DrawWrapped(TEXT("개인 창고는 이 역할의 저장에 보존됩니다. 이동해도 품질·옵션·강화·내구도·장비 ID는 유지됩니다."),Cream,DX,Y+269*S,DW,.60f*S,3);
            }
            else
            {
                DrawWrapped(PC->IsDismantleConfirmationPending()?TEXT("분해하면 이 장비가 사라집니다. 같은 장비를 확인한 뒤 E를 한 번 더 누르세요."):TEXT("선택한 장비를 재료로 바꿉니다. E로 확인한 뒤 다시 눌러야 분해됩니다."),PC->IsDismantleConfirmationPending()?Warning:Cream,DX,Y+269*S,DW,.62f*S,3);
            }
        }
        else DrawFitted(TEXT("왼쪽 목록에서 장비를 선택하세요."),Muted,DX,Y+155*S,DW,.68f*S);
    }
    FFableResourceCost Cost;
    if (bCraft) Cost=State->GetCraftCost(PC->GetCraftRarity());
    else if (Tab==EFableWorkshopTab::Dismantle) Cost=State->GetDismantleYield(SelectedId);
    else if (Tab==EFableWorkshopTab::Reroll) Cost=State->GetRerollCost(SelectedId);
    else if (Tab==EFableWorkshopTab::Repair) Cost=State->GetRepairCost(SelectedId);
    const FString CostLabel=Tab==EFableWorkshopTab::Dismantle?TEXT("획득"):TEXT("필요");
    if (bStash) DrawFitted(TEXT("이동 비용 없음 · 가방 32개 / 창고 48개"),Gold,DX,Y+359*S,DW,.59f*S);
    else
    {
        DrawFitted(FString::Printf(TEXT("%s: 골드 %d · 광석 %d · 정수 %d"),*CostLabel,Cost.Gold,Cost.Ore,Cost.Essence),Gold,DX,Y+353*S,DW,.58f*S);
        DrawFitted(FString::Printf(TEXT("뿌리 핵 %d · 달빛 결정 %d"),Cost.RootCores,Cost.MoonCrystals),Gold,DX,Y+377*S,DW,.58f*S);
    }
    const FString Block=PC->GetWorkshopBlockReason();
    const FString Help=Tab==EFableWorkshopTab::Reroll?TEXT("1~3 옵션 선택 · 선택 옵션만 변경 / 나머지 유지"):
        Tab==EFableWorkshopTab::Dismantle?TEXT("기본 / 장착 장비 분해 불가 · 선택 변경 시 확인 취소"):
        Tab==EFableWorkshopTab::Stash?TEXT("F 방향 전환 · 장착 장비는 보관할 수 없습니다."):TEXT("작업 결과는 아래 알림에서 확인하세요.");
    DrawWrapped(Block.IsEmpty()?Help:Block,Block.IsEmpty()?Muted:Warning,DX,Y+404*S,DW,.55f*S,2);
    const FString Action=bCraft?TEXT("E 장비 제작"):Tab==EFableWorkshopTab::Dismantle?(PC->IsDismantleConfirmationPending()?TEXT("E 분해 확정 · 장비 소모"):TEXT("E 분해 전 확인")):
        Tab==EFableWorkshopTab::Reroll?TEXT("E 선택 옵션 재설정"):Tab==EFableWorkshopTab::Repair?TEXT("E 장비 수리"):
        bWithdraw?TEXT("E 창고에서 꺼내기"):TEXT("E 창고에 보관");
    DrawButton(Action,TEXT("WorkshopAction"),DX,Y+449*S,DW,35*S,S,Block.IsEmpty());
    DrawFitted(TEXT("←→ 탭 · ↑↓ 목록 · E 작업 · 제작 1~4 / F 부위 · 옵션 1~3 · 창고 F 방향 · Esc 닫기"),Muted,X+24*S,Y+491*S,800*S,.51f*S);
}
void AFableHUD::DrawSkills(AFablePlayerController* PC,const AFablePlayerState* State,float S)
{
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    const AFableCharacter* Hero=Cast<AFableCharacter>(PC->GetPawn());
    const int32 Selected=FMath::Clamp(PC->GetSelectedSkillSlot(),0,3);
    const bool bConfirm=PC->IsBuildResetConfirmationPending();
    DrawPanel(bConfirm?TEXT("현재 직업 초기화 확인"):TEXT("스킬과 직업 특성"),X,Y,850*S,510*S,S);
    if (bConfirm) { DrawBuildResetConfirmation(PC,State,X,Y,S); return; }
    DrawFitted(FString::Printf(TEXT("%s · %d차 직업 · 사용 가능 %d포인트"),*State->GetAdvancedClassName(),State->GetAdvancementTier(),State->GetAvailableSkillPoints()),Gold,X+24*S,Y+60*S,800*S,.70f*S);
    const bool bTalents=PC->GetSkillPanelTab()==1;
    DrawButton(bTalents?TEXT("스킬 강화"):TEXT("● 스킬 강화"),TEXT("SkillTab_0"),X+24*S,Y+96*S,395*S,32*S,S);
    DrawButton(bTalents?TEXT("● 직업 특성"):TEXT("직업 특성"),TEXT("SkillTab_1"),X+431*S,Y+96*S,395*S,32*S,S);
    if (bTalents) DrawTalents(PC,State,X,Y,S);
    else
    {
        const TCHAR* Keys[]={TEXT("K"),TEXT("L"),TEXT("O"),TEXT("상시")};
        for(int32 Slot=0;Slot<4;++Slot)
        {
            const float RY=Y+(140+Slot*66)*S;
            const bool bUnlocked=State->IsSkillUnlocked(Slot);
            const int32 Rank=State->GetSkillRank(Slot);
            DrawRect(Selected==Slot?FLinearColor(.18f,.34f,.24f):FLinearColor(.07f,.14f,.10f),X+24*S,RY,263*S,58*S);
            DrawFitted(FString::Printf(TEXT("%s  %s"),Keys[Slot],*FableRPG::SkillName(State->HeroClass,Slot)),bUnlocked?Cream:Muted,X+34*S,RY+7*S,243*S,.64f*S);
            const FString Status=!bUnlocked?FString::Printf(TEXT("잠김 · %d차 전직 필요"),Slot+1):
                Rank==0?TEXT("미습득 · 포인트로 배우기"):FString::Printf(TEXT("%d / 3랭크 · %s"),Rank,Slot==3?TEXT("패시브"):TEXT("사용 가능"));
            DrawFitted(Status,bUnlocked?Mint:Muted,X+34*S,RY+33*S,243*S,.54f*S);
            AddHitBox(FVector2D(X+24*S,RY),FVector2D(263*S,58*S),FName(*FString::Printf(TEXT("Skill_%d"),Slot)),true,20);
        }
        DrawWrapped(TEXT("로완 근처에서 강화 / 전직\n포인트 공유 · 직업별 투자 유지"),Muted,X+24*S,Y+408*S,263*S,.53f*S,2);
        const float DX=X+312*S,DW=514*S;
        const int32 Rank=State->GetSkillRank(Selected);
        const bool bUnlocked=State->IsSkillUnlocked(Selected);
        DrawFitted(FableRPG::SkillName(State->HeroClass,Selected),Cream,DX,Y+139*S,DW,.86f*S);
        DrawFitted(FString::Printf(TEXT("%s · 현재 %d / 3랭크"),Selected==3?TEXT("패시브 · 항상 적용"):TEXT("액티브 스킬"),Rank),Mint,DX,Y+174*S,DW,.65f*S);
        DrawWrapped(FableRPG::SkillDescription(State->HeroClass,Selected),Cream,DX,Y+205*S,DW,.63f*S,3);
        if (Selected<3 && Hero)
        {
            DrawFitted(FString::Printf(TEXT("마나 %.0f · 재사용 %.1f초 · %s"),Hero->GetSkillManaCost(Selected),Hero->GetSkillCooldownDuration(Selected),*Cooldown(Hero->GetSkillCooldownRemaining(Selected))),Muted,DX,Y+272*S,DW,.60f*S);
        }
        else DrawFitted(Rank>0?TEXT("습득한 랭크만큼 자동 적용 · 시전 불필요"):TEXT("1포인트를 투자하면 상시 효과를 배웁니다"),Muted,DX,Y+272*S,DW,.60f*S);
        const FString Unlock=bUnlocked?TEXT("해금 완료"):Selected==1?TEXT("해금: 로완에게 2차 시험 수락 · K 3회 + 조건 달성"):TEXT("해금: 로완의 3차 시험 · L 3회 + 두 지역 보상");
        DrawWrapped(Unlock,bUnlocked?Mint:Gold,DX,Y+300*S,DW,.52f*S,2);
        const int32 NextRank=FMath::Min(3,Rank+1);
        FString EffectName,CurrentEffect,NextEffect;
        if (Selected==3)
        {
            if (State->HeroClass==EFableClass::Mage)
            {
                EffectName=TEXT("기본 마나 회복");
                CurrentEffect=FString::Printf(TEXT("%.1f"),8.0f+1.5f*Rank);
                NextEffect=FString::Printf(TEXT("%.1f"),8.0f+1.5f*NextRank);
            }
            else
            {
                const int32 PerRank=State->HeroClass==EFableClass::Warrior?3:2;
                EffectName=State->HeroClass==EFableClass::Warrior?TEXT("방어력"):TEXT("공격력");
                CurrentEffect=FString::Printf(TEXT("+%d"),PerRank*Rank);
                NextEffect=FString::Printf(TEXT("+%d"),PerRank*NextRank);
            }
        }
        else if (Selected==2 && State->HeroClass!=EFableClass::Archer)
        {
            const int32 Base=State->HeroClass==EFableClass::Mage?60:45;
            const int32 PerRank=State->HeroClass==EFableClass::Mage?20:15;
            EffectName=TEXT("기본 보호막 (6초)");
            CurrentEffect=FString::FromInt(Base+PerRank*(Rank-1));
            NextEffect=FString::FromInt(Base+PerRank*(NextRank-1));
        }
        else
        {
            EffectName=TEXT("기본 피해");
            CurrentEffect=FString::Printf(TEXT("%d%%"),100+15*(Rank-1));
            NextEffect=FString::Printf(TEXT("%d%%"),100+15*(NextRank-1));
        }
        const FString Growth=Rank>=3?FString::Printf(TEXT("%s %s · 최대 랭크"),*EffectName,*CurrentEffect):
            FString::Printf(TEXT("%s %s → %s · 1포인트"),*EffectName,*CurrentEffect,*NextEffect);
        DrawFitted(Growth,Gold,DX,Y+338*S,DW,.64f*S);
        const bool bCanUpgrade=bUnlocked && Rank<3 && State->GetAvailableSkillPoints()>0 && PC->IsNearTownBoard();
        DrawButton(Rank==0?TEXT("E 패시브 습득 · 1포인트"):TEXT("E 스킬 강화 · 1포인트"),TEXT("UpgradeSkill"),DX,Y+366*S,DW,38*S,S,bCanUpgrade);
        const FString Hint=!PC->IsNearTownBoard()?TEXT("로완에게 가까이 가면 포인트를 투자할 수 있습니다."):
            !bUnlocked?TEXT("로완과 대화하여 전직한 뒤 이 스킬을 강화하세요."):
            Rank>=3?TEXT("다른 스킬이나 직업에 남은 포인트를 투자해 보세요."):
            State->GetAvailableSkillPoints()<=0?TEXT("레벨을 올려 새 스킬 포인트를 얻으세요."):TEXT("직업을 바꿔도 지금까지 투자한 랭크는 유지됩니다.");
        DrawWrapped(Hint,Muted,DX,Y+411*S,DW,.54f*S,2);
    }
    const FString ResetBlock=State->GetBuildResetBlockReason();
    DrawButton(FString::Printf(TEXT("F 현재 직업 초기화 · %d골드"),State->GetBuildResetGoldCost()),TEXT("BuildResetRequest"),X+24*S,Y+449*S,316*S,33*S,S,ResetBlock.IsEmpty());
    DrawWrapped(ResetBlock.IsEmpty()?FString::Printf(TEXT("스킬 %d포인트 환급 · 특성 선택 해제"),State->GetCurrentClassRefundableSkillPoints()):ResetBlock,
        ResetBlock.IsEmpty()?Mint:Muted,X+356*S,Y+452*S,470*S,.50f*S,2);
    DrawFitted(TEXT("←→ 탭 · ↑↓ 목록 · E 강화 / 특성 선택 · F 초기화 · P / Esc 닫기"),Muted,X+24*S,Y+490*S,800*S,.52f*S);
}
void AFableHUD::DrawTalents(AFablePlayerController* PC,const AFablePlayerState* State,float X,float Y,float S)
{
    const int32 SelectedTier=PC->GetSelectedTalentStage();
    const EFableTalentPath SelectedPath=PC->GetSelectedTalentChoice();
    for (int32 Row=0;Row<4;++Row)
    {
        const int32 Tier=2+Row/2;
        const EFableTalentPath Path=static_cast<EFableTalentPath>(1+Row%2);
        const EFableTalentPath Choice=State->GetTalentChoice(Tier);
        const bool bSelected=SelectedTier==Tier && SelectedPath==Path;
        const bool bChosen=Choice==Path;
        const bool bUnlocked=State->GetAdvancementTier()>=Tier;
        const float RY=Y+(140+Row*66)*S;
        DrawRect(bSelected?FLinearColor(.18f,.34f,.24f):FLinearColor(.07f,.14f,.10f),X+24*S,RY,263*S,58*S);
        DrawFitted(FString::Printf(TEXT("%d차 · %s"),Tier,*FableRPG::TalentName(State->HeroClass,Path)),bUnlocked?Cream:Muted,X+34*S,RY+7*S,243*S,.63f*S);
        const FString RowStatus=bChosen?TEXT("선택 완료 · 상시 적용"):!bUnlocked?FString::Printf(TEXT("%d차 전직 후 선택"),Tier):
            Choice!=EFableTalentPath::None?TEXT("다른 특성 선택됨 · 초기화 필요"):Tier==3 && State->GetTalentChoice(2)==EFableTalentPath::None?TEXT("먼저 2차 특성을 선택하세요"):TEXT("선택 가능 · 무료");
        DrawFitted(RowStatus,bChosen?Gold:Muted,X+34*S,RY+33*S,243*S,.52f*S);
        AddHitBox(FVector2D(X+24*S,RY),FVector2D(263*S,58*S),FName(*FString::Printf(TEXT("Talent_%d"),Row)),true,20);
    }
    DrawWrapped(TEXT("선택한 특성은 항상 적용됩니다.\n스킬 포인트를 쓰지 않습니다."),Muted,X+24*S,Y+408*S,263*S,.53f*S,2);
    const float DX=X+312*S,DW=514*S;
    const EFableTalentPath StageChoice=State->GetTalentChoice(SelectedTier);
    const int32 CurrentRank=State->GetTalentRank(SelectedPath);
    const bool bOtherChoice=StageChoice!=EFableTalentPath::None && StageChoice!=SelectedPath;
    const int32 PreviewRank=bOtherChoice?1:StageChoice==SelectedPath?FMath::Max(1,CurrentRank):FMath::Min(2,CurrentRank+1);
    DrawFitted(FableRPG::TalentName(State->HeroClass,SelectedPath),Cream,DX,Y+139*S,DW,.86f*S);
    const FString PreviewText=bOtherChoice?TEXT("초기화 후 1단계 효과 예시"):StageChoice==SelectedPath?
        FString::Printf(TEXT("현재 %d단계 효과"),CurrentRank):FString::Printf(TEXT("선택 후 %d단계 효과"),PreviewRank);
    DrawFitted(FString::Printf(TEXT("%d차 선택 · %s"),SelectedTier,*PreviewText),Mint,DX,Y+174*S,DW,.65f*S);
    DrawWrapped(FableRPG::TalentDescription(State->HeroClass,SelectedPath,PreviewRank),Cream,DX,Y+205*S,DW,.64f*S,4);
    DrawWrapped(TEXT("2차 / 3차에 각각 1개 선택\n같은 계열은 2단계 · 다른 계열은 각각 1단계"),Gold,DX,Y+302*S,DW,.57f*S,2);
    const FString Block=State->GetTalentSelectBlockReason(SelectedTier,SelectedPath);
    DrawButton(FString::Printf(TEXT("E %d차 특성 선택 · 무료"),SelectedTier),TEXT("ChooseTalent"),DX,Y+366*S,DW,38*S,S,Block.IsEmpty());
    DrawWrapped(Block.IsEmpty()?TEXT("로완 근처에서 선택합니다. 바꾸려면 현재 직업을 초기화하세요."):Block,
        Block.IsEmpty()?Muted:Gold,DX,Y+411*S,DW,.54f*S,2);
}
void AFableHUD::DrawBuildResetConfirmation(AFablePlayerController* PC,const AFablePlayerState* State,float X,float Y,float S)
{
    const int32 Refund=State->GetCurrentClassRefundableSkillPoints();
    const int32 Cost=State->GetBuildResetGoldCost();
    DrawFitted(State->GetAdvancedClassName()+TEXT("의 스킬 투자와 특성 선택을 초기화합니다."),Gold,X+28*S,Y+77*S,794*S,.79f*S);
    DrawFitted(FString::Printf(TEXT("비용 %d골드 · 스킬 %d포인트 환급"),Cost,Refund),Cream,X+28*S,Y+126*S,794*S,.90f*S);
    DrawFitted(FString::Printf(TEXT("골드 %d → %d   /   사용 가능 포인트 %d → %d"),State->Gold,State->Gold-Cost,State->GetAvailableSkillPoints(),State->GetAvailableSkillPoints()+Refund),Mint,X+28*S,Y+170*S,794*S,.68f*S);
    DrawWrapped(TEXT("K / L / O 스킬은 기본 1랭크로, 패시브는 0랭크로 돌아갑니다.\n지금 직업에 투자한 포인트만 돌려받습니다."),Cream,X+28*S,Y+214*S,794*S,.68f*S,2);
    DrawFitted(TEXT("해제할 2차 특성: ")+FableRPG::TalentName(State->HeroClass,State->GetTalentChoice(2)),Gold,X+28*S,Y+273*S,794*S,.68f*S);
    DrawFitted(TEXT("해제할 3차 특성: ")+FableRPG::TalentName(State->HeroClass,State->GetTalentChoice(3)),Gold,X+28*S,Y+309*S,794*S,.68f*S);
    DrawWrapped(TEXT("레벨 · 전직 · 시험 진행 · 장비 · 다른 직업의 투자는 유지됩니다.\n직업이나 투자 상태, 골드가 바뀌면 이 확인은 취소됩니다."),Muted,X+28*S,Y+350*S,794*S,.60f*S,2);
    DrawButton(FString::Printf(TEXT("E 초기화 확정 · %d골드"),Cost),TEXT("BuildResetConfirm"),X+28*S,Y+423*S,382*S,43*S,S,PC->IsBuildResetConfirmationPending());
    DrawButton(TEXT("Esc 취소 · 돌아가기"),TEXT("BuildResetCancel"),X+428*S,Y+423*S,394*S,43*S,S);
    DrawFitted(TEXT("확정 후에는 다시 스킬과 특성을 선택할 수 있습니다."),Muted,X+28*S,Y+484*S,794*S,.55f*S);
}
void AFableHUD::DrawQuests(AFablePlayerController* PC,const AFablePlayerState* State,const AFableGameState* GS,float S)
{
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    DrawPanel(TEXT("의뢰 일지 · 메인 이야기와 전직"),X,Y,850*S,490*S,S);
    DrawFitted(DestinationText(State,GS),Mint,X+24*S,Y+65*S,800*S,.70f*S);
    DrawQuestSelection(PC,State,X,Y,S,false);
}
void AFableHUD::DrawQuestSelection(AFablePlayerController* PC,const AFablePlayerState* State,float X,float Y,float S,bool bAtGuide)
{
    const EFableQuestId Selected=PC->GetSelectedQuestId();
    for (int32 Index=0;Index<=int32(EFableQuestId::Story);++Index)
    {
        const EFableQuestId Quest=static_cast<EFableQuestId>(Index);
        const float RowY=Y+(105+Index*49)*S;
        const EFableQuestStage Stage=State->GetQuestStage(Quest);
        DrawRect(Selected==Quest?FLinearColor(.18f,.34f,.24f):FLinearColor(.07f,.14f,.10f),X+24*S,RowY,268*S,43*S);
        DrawFitted(State->GetQuestTitle(Quest),Selected==Quest?Cream:Muted,X+34*S,RowY+4*S,248*S,.61f*S);
        FString Status=FableRPG::QuestStageName(Stage);
        if (Stage==EFableQuestStage::Available && !State->CanInteractQuest(Quest)) Status=TEXT("선행 조건 필요");
        DrawFitted(Status,Stage==EFableQuestStage::Ready?Gold:Mint,X+34*S,RowY+24*S,248*S,.49f*S);
        AddHitBox(FVector2D(X+24*S,RowY),FVector2D(268*S,43*S),FName(*FString::Printf(TEXT("Quest_%d"),Index)),true,20);
    }
    const float DX=X+314*S,DW=510*S;
    const EFableQuestStage Stage=State->GetQuestStage(Selected);
    const bool bStory=Selected==EFableQuestId::Story;
    const bool bTrial=Selected==EFableQuestId::SecondAdvancement || Selected==EFableQuestId::ThirdAdvancement;
    DrawFitted(State->GetQuestTitle(Selected),Gold,DX,Y+105*S,DW,.85f*S);
    DrawFitted(FableRPG::QuestStageName(Stage),Mint,DX,Y+144*S,DW,.66f*S);
    if (bStory)
    {
        DrawWrapped(State->GetStoryDescription(),Cream,DX,Y+176*S,DW,.63f*S,4);
        DrawWrapped(TEXT("다음 행동: ")+State->GetStoryObjective(),Mint,DX,Y+263*S,DW,.60f*S,2);
        DrawWrapped(State->GetStoryRewardText(),Gold,DX,Y+311*S,DW,.58f*S,2);
    }
    else DrawWrapped(State->GetQuestDescription(Selected),Cream,DX,Y+181*S,DW,.66f*S,6);
    FString Detail;
    if (!State->CanInteractQuest(Selected) && Stage==EFableQuestStage::Available)
    {
        if (Selected==EFableQuestId::ThirdAdvancement) Detail=TEXT("먼저 2차 전직을 완료하면 이 시험을 수락할 수 있습니다.");
        else if (Selected==EFableQuestId::Moonwell) Detail=TEXT("먼저 뿌리 유적 보상을 로완에게 받아야 합니다.");
        else if (Selected==EFableQuestId::RootRuins) Detail=TEXT("먼저 숲 순찰 보상을 로완에게 받아야 합니다.");
    }
    else if (Selected==EFableQuestId::RootRuins) Detail=TEXT("보고: 희귀 무기 · 150골드 · 광석 8 · 경험치 200\n보스 세트 장비: 갑옷 → 무기를 번갈아 획득합니다.");
    else if (Selected==EFableQuestId::Moonwell) Detail=TEXT("보고: 달빛 무기 · 220골드 · 광석 12 · 경험치 300\n보스 세트 장비: 갑옷 → 무기를 번갈아 획득합니다.");
    else if (bTrial) Detail=TEXT("시험 수락 후 유효한 시전만 인정 · 직업별 진행 보존");
    else Detail=TEXT("보상은 로완에게 직접 보고해야 받을 수 있습니다.");
    if (!bStory) DrawWrapped(Detail,Muted,DX,Y+317*S,DW,.60f*S,2);
    if (bAtGuide)
    {
        FString Action=Stage==EFableQuestStage::Ready?(bTrial?TEXT("E 시험 보고 · 전직 완료"):TEXT("E 의뢰 보고 · 보상 받기")):
            Stage==EFableQuestStage::Active?TEXT("E 진행 확인 · 모험 계속"):
            Stage==EFableQuestStage::Completed?(bTrial || Selected==EFableQuestId::Forest?TEXT("완료한 의뢰"):TEXT("E 의뢰 다시 수락")):
            bTrial?TEXT("E 전직 시험 수락"):TEXT("E 의뢰 수락");
        if (bStory) Action=State->StoryChapter>=5?TEXT("메인 이야기 완료"):State->StoryChapter==0?TEXT("E 메인 이야기 시작"):
            State->IsStoryReady()?TEXT("E 이야기 보고 · 다음 장으로"):TEXT("E 이야기 진행 확인");
        DrawButton(Action,TEXT("ConfirmQuest"),DX,Y+369*S,DW,40*S,S,State->CanInteractQuest(Selected) && PC->IsNearTownBoard());
        DrawButton(TEXT("1 전사"),TEXT("ClassWarrior"),X+24*S,Y+428*S,254*S,39*S,S);
        DrawButton(TEXT("2 마법사"),TEXT("ClassMage"),X+297*S,Y+428*S,254*S,39*S,S);
        DrawButton(TEXT("3 궁수"),TEXT("ClassArcher"),X+570*S,Y+428*S,254*S,39*S,S);
    }
    else
    {
        DrawWrapped(TEXT("로완에게 돌아가 E로 대화한 뒤 이 의뢰를 선택하여 수락하거나 보고하세요."),Mint,DX,Y+371*S,DW,.63f*S,2);
        DrawFitted(TEXT("↑↓ / 클릭으로 6개 의뢰 선택 · 지도 목표는 메인 이야기 유지 · Q / Esc 닫기"),Muted,X+24*S,Y+450*S,800*S,.62f*S);
    }
}
void AFableHUD::DrawPortal(AFablePlayerController* PC,const AFablePlayerState* State,float S)
{
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    const AFableGameState* Adventure=GetWorld()->GetGameState<AFableGameState>();
    const EFableRegion Selected=PC->GetSelectedDungeonId();
    const EFableDifficulty Difficulty=PC->GetSelectedDifficulty();
    const bool bLocked=PC->IsDifficultySelectionLocked();
    const bool bRunning=Adventure && Adventure->ActiveRegion!=EFableRegion::Village;
    const bool bDungeonRetry=Adventure && Adventure->bRetryAvailable && Adventure->RetryRegion!=EFableRegion::Village;
    DrawPanel(TEXT("원정 지역과 난이도"),X,Y,850*S,490*S,S);
    DrawFitted(TEXT("모든 동료의 의뢰 준비가 필요합니다 · 새 입장 때 선택한 난이도가 파티에 적용됩니다."),Cream,X+24*S,Y+67*S,800*S,.61f*S);
    const EFableRegion Regions[]={EFableRegion::RootRuins,EFableRegion::Moonwell};
    const EFableQuestId Quests[]={EFableQuestId::RootRuins,EFableQuestId::Moonwell};
    for(int32 Index=0;Index<2;++Index)
    {
        const EFableRegion Region=Regions[Index];
        const float BX=X+(24+Index*407)*S;
        const bool bSelected=Selected==Region;
        DrawRect(bSelected?FLinearColor(.18f,.34f,.24f):FLinearColor(.07f,.14f,.10f),BX,Y+106*S,395*S,140*S);
        DrawRect(bSelected?Gold:Muted*.4f,BX,Y+106*S,395*S,3*S);
        DrawFitted((bSelected?TEXT("선택 · "):TEXT(""))+FableRPG::RegionName(Region),bSelected?Gold:Cream,BX+14*S,Y+117*S,367*S,.82f*S);
        DrawFitted(State->IsRegionEligible(Region)?TEXT("내 의뢰 준비 완료"):FableRPG::QuestStageName(State->GetQuestStage(Quests[Index])),Mint,BX+14*S,Y+152*S,367*S,.62f*S);
        DrawFitted(Index==0?TEXT("두 무리 + 고대 수호자 · 뿌리 수호자 세트"):TEXT("세 봉인 + 달빛 수호자 · 달빛 세트"),Cream,BX+14*S,Y+180*S,367*S,.56f*S);
        DrawFitted(Index==0?TEXT("해금: 숲 순찰 보상"):TEXT("해금: 뿌리 유적 보상 1회"),Muted,BX+14*S,Y+216*S,367*S,.56f*S);
        AddHitBox(FVector2D(BX,Y+106*S),FVector2D(395*S,140*S),FName(*FString::Printf(TEXT("Dungeon_%d"),int32(Region))),true,20);
    }
    DrawButton((Difficulty==EFableDifficulty::Normal?TEXT("● "):TEXT(""))+FableRPG::DifficultyName(EFableDifficulty::Normal),TEXT("DifficultyNormal"),X+24*S,Y+259*S,154*S,34*S,S,!bLocked);
    DrawButton((Difficulty==EFableDifficulty::Hard?TEXT("● "):TEXT(""))+FableRPG::DifficultyName(EFableDifficulty::Hard),TEXT("DifficultyHard"),X+190*S,Y+259*S,154*S,34*S,S,!bLocked);
    const FString DifficultyState=bRunning?TEXT("서버 현재: ")+FableRPG::DifficultyName(Adventure->ActiveDifficulty)+TEXT(" · 합류 난이도 고정"):
        Adventure && Adventure->bPartyRecoveryPending?TEXT("마을 구조 중 · 난이도 선택 대기"):TEXT("←→ 선택 · 새 입장은 이 난이도로 시작");
    DrawFitted(DifficultyState,bLocked?Gold:Muted,X+360*S,Y+267*S,464*S,.58f*S);
    DrawFitted(Difficulty==EFableDifficulty::Hard?TEXT("어려움: 적 체력 ×1.45 · 피해 ×1.25 · 처치 경험치 / 골드 / 광석 ×1.5"):
        TEXT("일반: 기본 전투와 보상 · 품질과 추가 옵션은 획득 시 결정됩니다"),Cream,X+24*S,Y+307*S,800*S,.59f*S);
    DrawFitted(Difficulty==EFableDifficulty::Hard?TEXT("품질 확률: 하급 10% · 중급 35% · 상급 35% · 최상급 20%"):
        TEXT("품질 확률: 하급 30% · 중급 40% · 상급 23% · 최상급 7%"),Mint,X+24*S,Y+333*S,800*S,.59f*S);
    DrawWrapped(PC->GetDungeonReadinessText(Selected),PC->CanEnterDungeon(Selected)?Mint:Warning,X+24*S,Y+363*S,800*S,.60f*S,2);
    const FString Action=bRunning?TEXT("E 합류 / 귀환 다시 시도 · ")+FableRPG::DifficultyName(Difficulty):
        TEXT("E 새 입장 · ")+FableRPG::RegionName(Selected)+TEXT(" · ")+FableRPG::DifficultyName(Difficulty);
    DrawButton(Action,TEXT("EnterRegion"),X+24*S,Y+411*S,(bDungeonRetry?438:800)*S,40*S,S,PC->CanEnterDungeon(Selected));
    if (bDungeonRetry)
        DrawButton(TEXT("R 재도전 · ")+FableRPG::RegionName(Adventure->RetryRegion)+TEXT(" · ")+FableRPG::DifficultyName(Adventure->RetryDifficulty),TEXT("RetryEncounter"),X+474*S,Y+411*S,350*S,40*S,S,PC->CanRetryEncounter());
    DrawFitted(bDungeonRetry?TEXT("↑↓ 지역 · ←→ 새 입장 난이도 · E 새 전투 / R 이전 난이도 재도전 · Esc 닫기"):
        TEXT("↑↓ 지역 · ←→ 난이도 · E / R 입장 · Esc 닫기"),Muted,X+24*S,Y+462*S,800*S,.57f*S);
}
void AFableHUD::DrawDialogue(AFablePlayerController* PC,const AFablePlayerState* State,float S)
{
    if (PC->IsPortalOpen()) { DrawPortal(PC,State,S); return; }
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    if (PC->GetDialogue()==EFableDialogue::Shop)
    {
        DrawPanel(TEXT("상인 미라 · 여행자의 상점"),X,Y,850*S,490*S,S);
        DrawFitted(FString::Printf(TEXT("골드 %d · 체력 물약 %d / 20 · 마나 물약 %d / 20"),State->Gold,State->HealthPotions,State->ManaPotions),Gold,X+24*S,Y+65*S,800*S,.68f*S);
        const TCHAR* Details[]={TEXT("체력 60 회복 · H 사용"),TEXT("마나 50 회복 · M 사용"),TEXT("전사 전용 · 고급 무기"),TEXT("마법사 전용 · 고급 무기"),TEXT("궁수 전용 · 고급 무기"),TEXT("모든 직업 · 고급 갑옷")};
        for(int32 I=0;I<6;++I)
        {
            const EFableShopOffer Offer=static_cast<EFableShopOffer>(I);
            const float BX=X+(24+(I%2)*407)*S,BY=Y+(107+(I/2)*88)*S;
            DrawRect(FLinearColor(.075f,.15f,.11f),BX,BY,395*S,76*S);
            DrawFitted(FableRPG::ShopName(Offer),Cream,BX+11*S,BY+6*S,254*S,.73f*S);
            DrawFitted(Details[I],Muted,BX+11*S,BY+43*S,252*S,.56f*S);
            const bool bCapacity=I==0?State->HealthPotions<20:I==1?State->ManaPotions<20:State->Inventory.Num()<32;
            DrawButton(FString::Printf(TEXT("%d골드"),FableRPG::ShopPrice(Offer)),FName(*FString::Printf(TEXT("Buy_%d"),I)),BX+276*S,BY+17*S,106*S,40*S,S,State->Gold>=FableRPG::ShopPrice(Offer) && bCapacity);
        }
        DrawButton(TEXT("가방 열어 장비 판매"),TEXT("OpenInventory"),X+24*S,Y+387*S,338*S,42*S,S);
        DrawWrapped(TEXT("물약은 H / M으로 사용하며 5초의 대기시간을 공유해요. 필요 없는 장비는 가방에서 선택해 판매하세요."),Muted,X+383*S,Y+387*S,441*S,.62f*S,3);
        DrawFitted(TEXT("구매 장비의 품질 / 옵션은 무작위 · 기본 / 장착 장비는 판매 불가"),Muted,X+24*S,Y+453*S,800*S,.63f*S);
        return;
    }
    const bool bGuide=PC->GetDialogue()==EFableDialogue::Guide;
    DrawPanel(bGuide?TEXT("안내인 로완"):TEXT("대장장이 브람"),X,Y,850*S,490*S,S);
    if (!bGuide)
    {
        DrawWrapped(TEXT("반가워, 모험가! 숲에서 모은 광석으로 장비를 더 튼튼하게 만들 수 있지. 강화는 언제나 성공하고 장비는 파괴되지 않아."),Cream,X+28*S,Y+88*S,794*S,.85f*S,3);
        DrawFitted(TEXT("장비 강화 +1 → +5"),Gold,X+28*S,Y+200*S,794*S,.9f*S);
        DrawWrapped(TEXT("가방에서는 강화, 공방에서는 제작·분해·옵션 변경·수리·창고를 이용해. 내구도가 0인 장비는 수리해야 효과가 돌아와."),Muted,X+28*S,Y+246*S,794*S,.76f*S,3);
        DrawButton(TEXT("E 가방 · 장착 / 강화"),TEXT("OpenInventory"),X+28*S,Y+355*S,382*S,48*S,S);
        DrawButton(TEXT("F 공방 · 제작 / 수리 / 창고"),TEXT("OpenWorkshop"),X+428*S,Y+355*S,394*S,48*S,S,PC->IsNearForge());
        DrawFitted(TEXT("가방: Tab 정렬 / E 장착 / U 강화 · 공방: ←→ 탭 / E 작업"),Mint,X+28*S,Y+428*S,794*S,.72f*S);
        return;
    }
    const AFableGameState* Adventure=GetWorld()->GetGameState<AFableGameState>();
    const bool bForestRetry=Adventure && Adventure->bRetryAvailable && Adventure->RetryRegion==EFableRegion::Village;
    DrawFitted(TEXT("↑↓ 의뢰 선택 · E 수락 / 보고 · 현재 ")+State->GetAdvancedClassName(),Mint,X+24*S,Y+65*S,(bForestRetry?550:800)*S,.68f*S);
    if (bForestRetry) DrawButton(TEXT("R 숲 순찰 재도전"),TEXT("RetryEncounter"),X+594*S,Y+63*S,230*S,33*S,S,PC->CanRetryEncounter());
    DrawQuestSelection(PC,State,X,Y,S,true);
}
void AFableHUD::DrawHelp(AFablePlayerController* PC,float S)
{
    const float X=(Canvas->ClipX-850*S)/2,Y=(Canvas->ClipY-530*S)/2-12*S;
    DrawPanel(TEXT("모험 안내"),X,Y,850*S,490*S,S);
    const TCHAR* Lines[]={
        TEXT("WASD 이동 · 마우스 시점 · Space 점프 · Shift 회피"),
        TEXT("J / 좌클릭 공격 · K / L / O 스킬 · H 체력약 · M 마나약"),
        TEXT("E 대화 · I 가방(Tab 정렬) · Q 의뢰 · P 스킬/특성 · 브람 대화 중 F 공방"),
        TEXT("쓰러짐 25초 · 동료 근처 E 3초 홀드 구조 · 이동 / 피격 시 취소"),
        TEXT("전멸 후 R: 숲은 로완 / 던전은 포털 · 이전 난이도로 재도전"),
        TEXT("1. 로완의 메인 이야기 → 등불 3개 수집 + 오래된 나무 조사"),
        TEXT("2. 숲 의뢰 보상 → 유적 룬 조사와 원정 보상 → 로완 보고"),
        TEXT("3. 달빛 수원의 봉인 3개 → 원정 보상 → 메인 이야기 완료"),
        TEXT("지도: 나 / 파랑 동료 / 금색 목표 · 수집과 조사는 가까이서 E")};
    for(int32 I=0;I<9;++I) DrawFitted(Lines[I],I<3?Cream:I<5?Gold:Mint,X+28*S,Y+(77+I*32)*S,794*S,.66f*S);
    DrawWrapped(TEXT("Q 전직 조건 · P에서 ←→ 스킬 / 특성 전환. 2·3차마다 무료 특성 선택. 로완 근처 비전투 상태에서 F 초기화(50골드): 현재 직업의 투자 포인트 환급·특성 해제."),Muted,X+28*S,Y+366*S,794*S,.64f*S,2);
    if (!PC->IsFrontEndOpen())
    {
        DrawButton(TEXT("시작 화면으로"),TEXT("ReturnFrontEnd"),X+28*S,Y+432*S,248*S,38*S,S);
        DrawFitted(TEXT("호스트가 나가면 동료의 연결이 종료됩니다."),Muted,X+296*S,Y+442*S,526*S,.59f*S);
    }
}
void AFableHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    AFablePlayerController* PC=Cast<AFablePlayerController>(PlayerOwner);if(!PC)return;
    const FString Name=BoxName.ToString();
    if (PC->IsBuildResetConfirmationPending() && BoxName!=TEXT("ClosePanel") && BoxName!=TEXT("BuildResetConfirm") && BoxName!=TEXT("BuildResetCancel")) return;
    if (Name.StartsWith(TEXT("SkillTab_"))) { PC->UISelectSkillPanelTab(FCString::Atoi(*Name.Mid(9))); return; }
    if (Name.StartsWith(TEXT("Talent_"))) { const int32 Row=FCString::Atoi(*Name.Mid(7)); if (Row>=0 && Row<4) PC->UISelectTalent(2+Row/2,static_cast<EFableTalentPath>(1+Row%2)); return; }
    if (Name.StartsWith(TEXT("WorkshopTab_"))) { PC->UISelectWorkshopTab(static_cast<EFableWorkshopTab>(FCString::Atoi(*Name.Mid(12)))); return; }
    if (Name.StartsWith(TEXT("WorkshopItem_"))) { PC->UISelectWorkshopItem(FCString::Atoi(*Name.Mid(13))); return; }
    if (Name.StartsWith(TEXT("WorkshopAffix_"))) { PC->UISelectAffix(FCString::Atoi(*Name.Mid(14))); return; }
    if (Name.StartsWith(TEXT("CraftGrade_"))) { PC->UISelectCraftRecipe(FCString::Atoi(*Name.Mid(11)),PC->GetCraftSlot()); return; }
    if (Name.StartsWith(TEXT("Quest_"))) { const int32 Quest=FCString::Atoi(*Name.Mid(6)); if(Quest>=0 && Quest<=int32(EFableQuestId::Story))PC->UISelectQuest(static_cast<EFableQuestId>(Quest)); return; }
    if (Name.StartsWith(TEXT("Dungeon_"))) { PC->UISelectDungeon(static_cast<EFableRegion>(FCString::Atoi(*Name.Mid(8)))); return; }
    if (Name.StartsWith(TEXT("Item_"))) { PC->UISelectItem(FCString::Atoi(*Name.Mid(5)));return; }
    if (Name.StartsWith(TEXT("Skill_"))) { PC->UISelectSkill(FCString::Atoi(*Name.Mid(6)));return; }
    if (Name.StartsWith(TEXT("Buy_"))) { const int32 Offer=FCString::Atoi(*Name.Mid(4));if(Offer>=0 && Offer<6)PC->UIShopBuy(static_cast<EFableShopOffer>(Offer));return; }
    if (BoxName==TEXT("ClosePanel"))PC->UIClosePanels();
    else if(BoxName==TEXT("SortInventory"))PC->UICycleInventorySort();
    else if(BoxName==TEXT("DifficultyNormal"))PC->UISelectDifficulty(EFableDifficulty::Normal);
    else if(BoxName==TEXT("DifficultyHard"))PC->UISelectDifficulty(EFableDifficulty::Hard);
    else if(BoxName==TEXT("EquipItem"))PC->UIEquip(PC->GetSelectedItemId());
    else if(BoxName==TEXT("EnhanceItem"))PC->UIEnhance(PC->GetSelectedItemId());
    else if(BoxName==TEXT("SellItem"))PC->UISell(PC->GetSelectedItemId());
    else if(BoxName==TEXT("OpenInventory"))PC->UIToggleInventory();
    else if(BoxName==TEXT("OpenWorkshop"))PC->UIOpenWorkshop();
    else if(BoxName==TEXT("WorkshopAction"))PC->UIWorkshopAction();
    else if(BoxName==TEXT("CraftWeapon"))PC->UISelectCraftRecipe(PC->GetCraftRarity(),EFableEquipmentSlot::Weapon);
    else if(BoxName==TEXT("CraftArmor"))PC->UISelectCraftRecipe(PC->GetCraftRarity(),EFableEquipmentSlot::Armor);
    else if(BoxName==TEXT("StashDepositMode"))PC->UISetStashWithdrawal(false);
    else if(BoxName==TEXT("StashWithdrawMode"))PC->UISetStashWithdrawal(true);
    else if(BoxName==TEXT("PreviousItem"))PC->UISelectPrevious();
    else if(BoxName==TEXT("NextItem"))PC->UISelectNext();
    else if(BoxName==TEXT("ClassWarrior"))PC->UIChooseClass(EFableClass::Warrior);
    else if(BoxName==TEXT("ClassMage"))PC->UIChooseClass(EFableClass::Mage);
    else if(BoxName==TEXT("ClassArcher"))PC->UIChooseClass(EFableClass::Archer);
    else if(BoxName==TEXT("ConfirmQuest"))PC->UIConfirmQuest();
    else if(BoxName==TEXT("EnterRegion"))PC->UIEnterSelectedDungeon();
    else if(BoxName==TEXT("RetryEncounter"))PC->UIRetryEncounter();
    else if(BoxName==TEXT("UpgradeSkill"))PC->UIUpgradeSkill(PC->GetSelectedSkillSlot());
    else if(BoxName==TEXT("ChooseTalent"))PC->UIChooseTalent();
    else if(BoxName==TEXT("BuildResetRequest"))PC->UIRequestBuildReset();
    else if(BoxName==TEXT("BuildResetConfirm"))PC->UIConfirmBuildReset();
    else if(BoxName==TEXT("BuildResetCancel"))PC->UICancelBuildReset();
    else if(BoxName==TEXT("AdvanceClass"))PC->UIAdvanceClass();
    else if(BoxName==TEXT("Help"))PC->UIToggleHelp();
    else if(BoxName==TEXT("StartClassWarrior"))PC->UISetInitialClass(EFableClass::Warrior);
    else if(BoxName==TEXT("StartClassMage"))PC->UISetInitialClass(EFableClass::Mage);
    else if(BoxName==TEXT("StartClassArcher"))PC->UISetInitialClass(EFableClass::Archer);
    else if(BoxName==TEXT("NewGame"))PC->UIStartNewGame(false);
    else if(BoxName==TEXT("ContinueGame"))PC->UIContinueGame(false);
    else if(BoxName==TEXT("HostNewGame"))PC->UIStartNewGame(true);
    else if(BoxName==TEXT("HostContinueGame"))PC->UIContinueGame(true);
    else if(BoxName==TEXT("JoinAddress"))PC->UIFocusJoinAddress();
    else if(BoxName==TEXT("JoinGame"))PC->UIJoinGame();
    else if(BoxName==TEXT("ReturnFrontEnd"))PC->UIReturnToFrontEnd();
}
