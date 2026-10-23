#include "FableRPGValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableProjectile.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"

UFableRPGValidationDriver::UFableRPGValidationDriver() { PrimaryComponentTick.bCanEverTick=true; }
void UFableRPGValidationDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
#else
    PC=Cast<AFablePlayerController>(GetOwner());
    if(!PC.IsValid() || !PC->IsLocalController() ||
       !FParse::Value(FCommandLine::Get(),TEXT("FableRPGValidationRole="),Role) ||
       !FParse::Value(FCommandLine::Get(),TEXT("FableRPGValidationRun="),RunId) ||
       !FParse::Value(FCommandLine::Get(),TEXT("FableRPGValidationDirectory="),Directory) ||
       (Role!=TEXT("host") && Role!=TEXT("guest")) || FPaths::IsRelative(Directory))
    { SetComponentTickEnabled(false); return; }
    FParse::Value(FCommandLine::Get(),TEXT("FableRPGValidationPhase="),Phase);
    bTalents=FParse::Param(FCommandLine::Get(),TEXT("FableTalents"));
    bStory=FParse::Param(FCommandLine::Get(),TEXT("FableStory"));
    bWorkshop=FParse::Param(FCommandLine::Get(),TEXT("FableWorkshop"));
    bLoot=FParse::Param(FCommandLine::Get(),TEXT("FableLoot"));
    bAdventure=FParse::Param(FCommandLine::Get(),TEXT("FableAdventure"));
    bAdventureWarrior=FParse::Param(FCommandLine::Get(),TEXT("FableAdventureWarrior"));
    bSkills=FParse::Param(FCommandLine::Get(),TEXT("FableSkills"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    StartedAt=StepAt=GetWorld()->GetTimeSeconds();
#endif
}
void UFableRPGValidationDriver::Key(const FKey& Value)
{
    if(!PC.IsValid() || !PC->PlayerInput) return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,IE_Pressed,1.f,0,FInputDeviceId::CreateFromInternalId(0)));
    ReleaseKeys.AddUnique(Value);
}
void UFableRPGValidationDriver::Hold(const FKey& Value,bool bHeld)
{
    if(PC.IsValid() && PC->PlayerInput && PC->IsInputKeyDown(Value)!=bHeld)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,bHeld?IE_Pressed:IE_Released,bHeld?1.f:0.f,0,FInputDeviceId::CreateFromInternalId(0)));
}
void UFableRPGValidationDriver::Release()
{
    if(!PC.IsValid()) return;
    for(const FKey& Value:ReleaseKeys)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,IE_Released,0.f,0,FInputDeviceId::CreateFromInternalId(0)));
    ReleaseKeys.Empty();
}
bool UFableRPGValidationDriver::WalkTo(const FVector& Location,float Radius)
{
    APawn* Pawn=PC.IsValid()?PC->GetPawn():nullptr;
    if(!Pawn) return false;
    const FVector Delta=Location-Pawn->GetActorLocation();
    Hold(EKeys::S,false);
    if(Delta.Size2D()<=Radius) { Hold(EKeys::W,false); return true; }
    // Only view direction is automated directly; movement still enters CharacterMovement through W.
    PC->SetControlRotation(FRotator(-12,Delta.Rotation().Yaw,0));
    Hold(EKeys::W,true);
    return false;
}
void UFableRPGValidationDriver::Advance(int32 NewStep)
{
    Hold(EKeys::W,false); Hold(EKeys::S,false);
    Step=NewStep; StepAt=GetWorld()->GetTimeSeconds(); LastAction=-10;
    UE_LOG(LogTemp,Display,TEXT("FABLE_RPG_VALIDATION role=%s step=%d"),*Role,Step);
}
void UFableRPGValidationDriver::Capture(const FString& Name)
{
    const FString Path=FPaths::Combine(Directory,Role+TEXT("-")+Name+TEXT(".png"));
    // Input actions and the HUD draw on successive frames. Let the requested
    // panel render before capturing, while retaining unedited engine output.
    const TWeakObjectPtr<UFableRPGValidationDriver> WeakThis(this);
    FTimerHandle CaptureTimer;
    GetWorld()->GetTimerManager().SetTimer(CaptureTimer,[WeakThis,Path]()
    {
        if(WeakThis.IsValid()) FScreenshotRequest::RequestScreenshot(Path,true,false);
    },0.35f,false);
    Screenshots.AddUnique(Path);
}
void UFableRPGValidationDriver::Fight()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    if(!Hero || !Hero->IsAlive()) { Hold(EKeys::W,false); Hold(EKeys::S,false); return; }
    AFableEnemy* Target=nullptr; float Distance=MAX_flt;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
    {
        const float D=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation());
        if(It->IsAlive() && D<Distance) { Target=*It; Distance=D; }
    }
    if(!Target) { Hold(EKeys::W,false); Hold(EKeys::S,false); return; }
    PC->SetControlRotation(FRotator(-12,(Target->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));

    if(bSkills)
    {
        AFablePlayerState* Profile=PC->GetPlayerState<AFablePlayerState>();
        const double SkillNow=GetWorld()->GetTimeSeconds();
        // The extra expedition gives the mage an actual melee hit while protected.
        // The partner waits briefly so the measured target is not killed beforehand.
        if(bSecondDungeon && SkillNow-SecondDungeonAt<30.0)
        {
            AFableCharacter* Mage=nullptr;
            for(TActorIterator<AFableCharacter> It(GetWorld());It;++It)
                if(It->GetHeroClass()==EFableClass::Mage && It->GetPlayerState()) Mage=*It;
            if(Mage && Mage->GetTotalShieldAbsorbed()<=0)
            {
                if(Hero!=Mage){Hold(EKeys::W,false);Hold(EKeys::S,false);return;}
                Hold(EKeys::W,Distance>110);Hold(EKeys::S,false);
                if(Distance<250 && SkillNow-LastAdvancedInput>0.8 && Hero->GetSkillCooldownRemaining(2)<=0
                    && Hero->GetMana()>=Hero->GetSkillManaCost(2))
                {Key(EKeys::O);LastAdvancedInput=SkillNow;++SkillInputs;}
                return;
            }
        }
        Hold(EKeys::W,Distance>520);Hold(EKeys::S,Distance<300);
        if(SkillNow-LastAdvancedInput<0.72) return;
        if(Profile && Profile->IsSkillUnlocked(1) && Distance<600 && Hero->GetSkillCooldownRemaining(1)<=0
            && Hero->GetMana()>=Hero->GetSkillManaCost(1))
        {Key(EKeys::L);LastAdvancedInput=SkillNow;++SkillInputs;return;}
        if(Profile && Profile->IsSkillUnlocked(2) && bSecondDungeon && Distance<650 && Hero->GetSkillCooldownRemaining(2)<=0
            && Hero->GetMana()>=Hero->GetSkillManaCost(2))
        {Key(EKeys::O);LastAdvancedInput=SkillNow;++SkillInputs;return;}
        if(Distance<900 && Hero->GetSkillCooldownRemaining(0)<=0 && Hero->GetMana()>=Hero->GetSkillManaCost(0))
        {Key(EKeys::K);LastAdvancedInput=SkillNow;++SkillInputs;return;}
        if(Distance<1150)
        {Key(EKeys::J);LastAdvancedInput=SkillNow;++AttackInputs;}
        return;
    }

    Hold(EKeys::W,Distance>680); Hold(EKeys::S,Distance<370);
    const double Now=GetWorld()->GetTimeSeconds();
    if(Distance<1150 && Now-LastAction>0.58)
    { Key(EKeys::J); LastAction=Now; ++AttackInputs; }
    if(Distance<900 && Now-LastSkill>6.2)
    { Key(EKeys::K); LastSkill=Now; ++SkillInputs; }
}

void UFableRPGValidationDriver::NextSkillCheck(int32 Value)
{
    SkillCheck=Value; SkillAt=GetWorld()->GetTimeSeconds();
}
FString UFableRPGValidationDriver::SelectedProgress(const AFablePlayerState* PS) const
{
    if(!PS || !PS->ClassProgress.IsValidIndex(int32(PS->HeroClass))) return TEXT("invalid");
    const FFableClassProgress& Progress=PS->ClassProgress[int32(PS->HeroClass)];
    FString Result=FString::FromInt(Progress.Tier);
    for(int32 Rank:Progress.Ranks) Result+=TEXT(",")+FString::FromInt(Rank);
    return Result;
}
void UFableRPGValidationDriver::ObserveSkills()
{
    if(!bSkills || !PC.IsValid()) return;
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    if(!Hero) return;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
        if(It->GetSlowTimeRemaining()>0 && It->GetSlowMultiplier()<0.99f) bSlowObserved=true;
    for(TActorIterator<AFableProjectile> It(GetWorld());It;++It)
        if(It->GetOwner()==Hero && It->IsPiercingShot())
        {
            bPiercingObserved=true;
            MaxPiercingHits=FMath::Max(MaxPiercingHits,It->GetUniqueHitCount());
        }
}
bool UFableRPGValidationDriver::SkillAssertionsComplete() const
{
    const AFableCharacter* Hero=PC.IsValid()?PC->GetPawn<AFableCharacter>():nullptr;
    return Hero && bPrematureAdvanceRejected && bLockedSkillsRejected && bLockedUpgradeRejected
        && bSkillPointsRejected && bSkillPanelBlocksInput && bPassiveUpgraded && bSecondAdvancement
        && bSkillRankThree && bThirdAdvancement && bDuplicateAdvanceRejected && bSkillCooldownRejected
        && bWarriorSkillsChecked && bClassSwitchPreserved && bAdvancementSurvivesRepeat
        && bSecondDungeonCompleted && (Role!=TEXT("host") || (bSlowObserved && Hero->GetTotalShieldAbsorbed()>0))
        && (Role!=TEXT("guest") || bPiercingObserved);
}
bool UFableRPGValidationDriver::RunSkillChecks(int32 PhaseIndex)
{
    if(SkillPhasesDone[PhaseIndex]) return true;
    AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    const double Now=GetWorld()->GetTimeSeconds();
    if(SkillPhase!=PhaseIndex)
    {
        SkillPhase=PhaseIndex; SkillCheck=0; SkillAt=SkillPhaseAt=Now;
        PC->UIClosePanels(); Hold(EKeys::W,false); Hold(EKeys::S,false);
    }
    const double Wait=Now-SkillAt;
    if(Now-SkillPhaseAt>110) { Finish(false,TEXT("Skill checkpoint timed out.")); return false; }
    // Leave normal profile requests outside the preceding quest/class mutation rate limits.
    if(SkillCheck==0 && Wait<0.5) return false;
    const auto Done=[&]()
    {
        PC->UIClosePanels(); Hold(EKeys::W,false);
        SkillPhasesDone[PhaseIndex]=true; LastAction=-10; StepAt=Now;
        return true;
    };
    const auto FailedAfter=[&](const TCHAR* Reason)
    {
        if(Wait>6.0) Finish(false,Reason);
    };
    if(PhaseIndex==0)
    {
        switch(SkillCheck)
        {
        case 0:
            if(PS->Level!=1 || PS->GetAdvancementTier()!=1 || PS->GetAvailableSkillPoints()!=1)
            {Finish(false,TEXT("Initial skill profile was not a fresh level-one profile."));break;}
            PC->UIAdvanceClass(); NextSkillCheck(1); break;
        case 1:
            if(Wait>1 && PS->LastNotice.Contains(TEXT("레벨 2")))
            {bPrematureAdvanceRejected=PS->GetAdvancementTier()==1; CastBefore[1]=Hero->GetSkillCastCount(1); Key(EKeys::L);NextSkillCheck(2);}
            else FailedAfter(TEXT("Premature advancement rejection was not acknowledged."));
            break;
        case 2:
            if(Wait>1 && PS->LastNotice.Contains(TEXT("잠긴 스킬")))
            {
                if(Hero->GetSkillCastCount(1)!=CastBefore[1] || Hero->GetSkillCooldownRemaining(1)>0)
                {Finish(false,TEXT("Locked second skill was executed."));break;}
                CastBefore[2]=Hero->GetSkillCastCount(2);Key(EKeys::O);NextSkillCheck(3);
            }else FailedAfter(TEXT("Locked L skill rejection was not acknowledged."));
            break;
        case 3:
            if(Wait>3.0)
            {
                bLockedSkillsRejected=Hero->GetSkillCastCount(2)==CastBefore[2] && Hero->GetSkillCooldownRemaining(2)<=0;
                if(!bLockedSkillsRejected){Finish(false,TEXT("Locked third skill was executed."));break;}
                PC->UIUpgradeSkill(1);NextSkillCheck(4);
            }break;
        case 4:
            if(Wait>1 && PS->LastNotice.Contains(TEXT("해금한 뒤")))
            {
                bLockedUpgradeRejected=PS->GetSkillRank(1)==1 && PS->GetAvailableSkillPoints()==1;
                Key(EKeys::P);NextSkillCheck(5);
            }else FailedAfter(TEXT("Locked skill upgrade was not rejected."));
            break;
        case 5:
            if(Wait>0.8 && PC->IsSkillsOpen())
            {InputBefore=Hero->GetActorLocation();CastBefore[0]=Hero->GetSkillCastCount(0);Hold(EKeys::W,true);Key(EKeys::K);NextSkillCheck(6);}
            else FailedAfter(TEXT("P did not open the skills panel."));
            break;
        case 6:
            if(Wait>3.0)
            {
                Hold(EKeys::W,false);
                bSkillPanelBlocksInput=FVector::Dist2D(InputBefore,Hero->GetActorLocation())<12
                    && Hero->GetSkillCastCount(0)==CastBefore[0];
                if(!bSkillPanelBlocksInput){Finish(false,TEXT("Skills panel allowed movement or combat."));break;}
                Capture(TEXT("skills-initial")); PassivePowerBefore=PS->GetAttackPower();
                PC->UISelectSkill(3);PC->UIUpgradeSkill(3);NextSkillCheck(7);
            }break;
        case 7:
            if(Wait>1 && PS->GetSkillRank(3)==1 && PS->GetAvailableSkillPoints()==0)
            {
                bPassiveUpgraded=Role==TEXT("host") || FMath::IsNearlyEqual(PS->GetAttackPower(),PassivePowerBefore+2);
                PC->UIUpgradeSkill(0);NextSkillCheck(8);
            }else FailedAfter(TEXT("Passive investment or point debit did not replicate."));
            break;
        case 8:
            if(Wait>1 && PS->LastNotice.Contains(TEXT("포인트가 부족")))
            {
                bSkillPointsRejected=PS->GetSkillRank(0)==1 && PS->GetAvailableSkillPoints()==0;
                return Done();
            }else FailedAfter(TEXT("Insufficient skill points were not rejected."));
            break;
        }
    }
    else if(PhaseIndex==1)
    {
        switch(SkillCheck)
        {
        case 0: PC->UIAdvanceClass();NextSkillCheck(1);break;
        case 1:
            if(Wait>1 && PS->GetAdvancementTier()==2 && PS->IsSkillUnlocked(1))
            {bSecondAdvancement=true;PC->UIUpgradeSkill(0);NextSkillCheck(2);}
            else FailedAfter(TEXT("Level-two forest advancement failed."));
            break;
        case 2:
            if(Wait>1 && PS->GetSkillRank(0)==2) {PC->UIUpgradeSkill(0);NextSkillCheck(3);}
            else FailedAfter(TEXT("First K rank upgrade failed."));break;
        case 3:
            if(Wait>1 && PS->GetSkillRank(0)==3 && PS->GetAvailableSkillPoints()==0)
            {bSkillRankThree=true;Key(EKeys::P);NextSkillCheck(4);}
            else FailedAfter(TEXT("Second K rank upgrade or shared point accounting failed."));break;
        case 4:if(Wait>0.8){Capture(TEXT("skills-second-advancement"));NextSkillCheck(5);}break;
        case 5:if(Wait>0.8)return Done();break;
        }
    }
    else if(PhaseIndex==2)
    {
        switch(SkillCheck)
        {
        case 0:
            if(Hero->GetSkillCastCount(1)<1){Finish(false,TEXT("Second active skill was never used in the first dungeon."));break;}
            PC->UIAdvanceClass();NextSkillCheck(1);break;
        case 1:
            if(Wait>1 && PS->GetAdvancementTier()==3 && PS->IsSkillUnlocked(2))
            {bThirdAdvancement=true;PointsBefore=PS->GetAvailableSkillPoints();PC->UIAdvanceClass();NextSkillCheck(2);}
            else FailedAfter(TEXT("Level-four completed-dungeon advancement failed."));break;
        case 2:
            if(Wait>1 && PS->LastNotice.Contains(TEXT("최종 전직")))
            {
                bDuplicateAdvanceRejected=PS->GetAdvancementTier()==3 && PS->GetAvailableSkillPoints()==PointsBefore;
                PC->UIUpgradeSkill(1);NextSkillCheck(3);
            }else FailedAfter(TEXT("Duplicate final advancement was not rejected."));break;
        case 3:
            if(Wait>1 && PS->GetSkillRank(1)==2){PC->UIUpgradeSkill(2);NextSkillCheck(4);}
            else FailedAfter(TEXT("L skill investment failed."));break;
        case 4:
            if(Wait>1 && PS->GetSkillRank(2)==2){PC->UIUpgradeSkill(3);NextSkillCheck(5);}
            else FailedAfter(TEXT("O skill investment failed."));break;
        case 5:
            if(Wait>1 && PS->GetSkillRank(3)==2){PC->UIUpgradeSkill(3);NextSkillCheck(6);}
            else FailedAfter(TEXT("Second passive rank investment failed."));break;
        case 6:
            if(Wait>1 && PS->GetSkillRank(3)==3 && PS->GetAvailableSkillPoints()==0)
            {Key(EKeys::P);NextSkillCheck(7);}
            else FailedAfter(TEXT("Final passive rank or point accounting failed."));break;
        case 7:
            if(Wait>0.8){Capture(TEXT("skills-third-advancement"));NextSkillCheck(8);}break;
        case 8:
            if(Wait>0.8 && Hero->GetMana()>=Hero->GetSkillManaCost(2) && Hero->GetSkillCooldownRemaining(2)<=0)
            {PC->UIClosePanels();CastBefore[2]=Hero->GetSkillCastCount(2);Key(EKeys::O);NextSkillCheck(9);}break;
        case 9:
            if(Wait>0.6 && Hero->GetSkillCastCount(2)==CastBefore[2]+1)
            {
                if(Role==TEXT("host") && Hero->GetShieldAmount()<=0){Finish(false,TEXT("Mage O did not create a shield."));break;}
                Capture(TEXT("skills-third-active"));Key(EKeys::O);NextSkillCheck(10);
            }else FailedAfter(TEXT("Unlocked O skill did not execute."));break;
        case 10:
            if(Wait>3.0)
            {
                bSkillCooldownRejected=Hero->GetSkillCastCount(2)==CastBefore[2]+1 && Hero->GetSkillCooldownRemaining(2)>0;
                if(!bSkillCooldownRejected){Finish(false,TEXT("Repeated O skill bypassed its cooldown."));break;}
                NextSkillCheck(11);
            }break;
        case 11:
            // Archer O moves the pawn. Return through normal movement before the next equipment request.
            if(WalkTo(FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-140,100),90))
                return Done();
            break;
        }
    }
    else if(PhaseIndex==3)
    {
        const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-140,100);
        const EFableClass Wanted=Role==TEXT("host")?EFableClass::Mage:EFableClass::Archer;
        switch(SkillCheck)
        {
        case 0:
            if(WalkTo(Board,90)){WantedProgress=SelectedProgress(PS);PointsBefore=PS->GetAvailableSkillPoints();Key(EKeys::One);NextSkillCheck(1);}break;
        case 1:
            if(Wait>1 && PS->HeroClass==EFableClass::Warrior){PC->UIAdvanceClass();NextSkillCheck(2);}
            else FailedAfter(TEXT("Warrior class selection failed."));break;
        case 2:
            if(Wait>1 && PS->GetAdvancementTier()==2){PC->UIAdvanceClass();NextSkillCheck(3);}
            else FailedAfter(TEXT("Warrior second advancement failed."));break;
        case 3:
            if(Wait>1 && PS->GetAdvancementTier()==3)
            {for(int32 I=0;I<3;++I)CastBefore[I]=Hero->GetSkillCastCount(I);PC->SetControlRotation(FRotator(-12,90,0));NextSkillCheck(4);}
            else FailedAfter(TEXT("Warrior third advancement failed."));break;
        case 4:
            if(Hero->GetSkillCooldownRemaining(0)<=0 && Wait>0.8){Key(EKeys::K);NextSkillCheck(5);}break;
        case 5:
            if(Wait>0.8 && Hero->GetSkillCastCount(0)==CastBefore[0]+1)
            {if(Hero->GetSkillCooldownRemaining(1)<=0){Key(EKeys::L);NextSkillCheck(6);}}
            else FailedAfter(TEXT("Warrior K skill did not execute."));break;
        case 6:
            if(Wait>0.8 && Hero->GetSkillCastCount(1)==CastBefore[1]+1)
            {if(Hero->GetSkillCooldownRemaining(2)<=0){Key(EKeys::O);NextSkillCheck(7);}}
            else FailedAfter(TEXT("Warrior L skill did not execute."));break;
        case 7:
            if(Wait>0.8 && Hero->GetSkillCastCount(2)==CastBefore[2]+1 && Hero->GetShieldAmount()>0)
            {bWarriorSkillsChecked=true;Capture(TEXT("skills-warrior-advanced"));NextSkillCheck(8);}
            else FailedAfter(TEXT("Warrior O shield did not execute."));break;
        case 8:
            if(WalkTo(Board,90) && Hero->GetShieldTimeRemaining()<=0 && Hero->GetSkillCooldownRemaining(2)<=0)
            {Key(Wanted==EFableClass::Mage?EKeys::Two:EKeys::Three);NextSkillCheck(9);}break;
        case 9:
            if(Wait>1 && PS->HeroClass==Wanted)
            {
                bClassSwitchPreserved=SelectedProgress(PS)==WantedProgress && PS->GetAvailableSkillPoints()==PointsBefore;
                if(!bClassSwitchPreserved){Finish(false,TEXT("Changing classes lost advancement or skill investments."));break;}
                return Done();
            }else FailedAfter(TEXT("Original class was not restored."));break;
        }
    }
    return false;
}

void UFableRPGValidationDriver::QuestInteraction()
{
    APawn* Pawn=PC.IsValid()?PC->GetPawn():nullptr;
    if(!Pawn) return;
    const double Now=GetWorld()->GetTimeSeconds();
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
    // Recheck after arrival: a remote pawn may still receive a movement correction.
    // Only close dialogue when another approach is needed, so its confirm step remains intact.
    if(FVector::DistSquared2D(Pawn->GetActorLocation(),Board)>FMath::Square(90.f) && PC->IsAnyPanelOpen())
        PC->UIClosePanels();
    if(!WalkTo(Board,90) || Pawn->GetVelocity().SizeSquared2D()>FMath::Square(10.f))
    {
        LastAction=Now;
        return;
    }
    if(Now-LastAction<1.0) return;
    if(PC->IsDialogueOpen())
    {
        if(!bDialogueChecked){bDialogueChecked=true;Capture(TEXT("quest-dialogue"));LastAction=Now;return;}
        PC->UIConfirmQuest();
    }
    else Key(EKeys::E);
    LastAction=Now;
}

bool UFableRPGValidationDriver::PreparePotions()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();
    const double Now=GetWorld()->GetTimeSeconds();
    const auto Next=[&](int32 Value){ PotionStep=Value; PotionAt=Now; };
    switch(PotionStep)
    {
    case 0:
        HealthBefore=PS->HealthPotions;
        Key(EKeys::H); Next(1); break;
    case 1:
        if(Now-PotionAt>0.6)
        {
            bFullPotionRejected=PS->HealthPotions==HealthBefore;
            if(!bFullPotionRejected) { Finish(false,TEXT("A full-health potion was consumed.")); return false; }
            Key(EKeys::K); ++SkillInputs; Next(2);
        } break;
    case 2:
    {
        AFableEnemy* Target=nullptr; float Distance=MAX_flt;
        for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
        { const float D=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation()); if(It->IsAlive()&&D<Distance){Target=*It;Distance=D;} }
        if(Target) WalkTo(Target->GetActorLocation(),130);
        if(Hero->GetHealth()<Hero->GetMaxHealth()-5)
        {
            Hold(EKeys::W,false); ResourceBefore=Hero->GetHealth(); HealthBefore=PS->HealthPotions;
            MaxObservedResource=ResourceBefore; Key(EKeys::H); Next(3);
        }
        if(Now-PotionAt>30) Finish(false,TEXT("No real enemy hit arrived for potion validation."));
        break;
    }
    case 3:
        MaxObservedResource=FMath::Max(MaxObservedResource,Hero->GetHealth());
        WalkTo(FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-180,100),100);
        if(Now-PotionAt>0.5)
        {
            bHealthPotionChecked=PS->HealthPotions==HealthBefore-1 && MaxObservedResource>ResourceBefore;
            if(!bHealthPotionChecked){Finish(false,TEXT("Health potion did not restore resource and decrement stock."));return false;}
            // Leave the enemy's reach with normal movement before waiting for the shared cooldown.
            ManaBefore=PS->ManaPotions; Key(EKeys::M); Next(4);
        } break;
    case 4:
        WalkTo(FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-180,100),100);
        if(Now-PotionAt>0.5 && !bPotionCooldownChecked)
        {
            bPotionCooldownChecked=PS->ManaPotions==ManaBefore && PS->GetPotionCooldownRemaining()>0;
            if(!bPotionCooldownChecked){Finish(false,TEXT("Shared potion cooldown did not reject repeated use."));return false;}
        }
        if(Now-PotionAt>6 && Hero->GetSkillCooldownRemaining()<=0)
        { Hold(EKeys::W,false); Key(EKeys::K); ++SkillInputs; Next(5); }
        break;
    case 5:
        if(Now-PotionAt>0.6)
        { ResourceBefore=Hero->GetMana(); ManaBefore=PS->ManaPotions; Key(EKeys::M); Next(6); }
        break;
    case 6:
        if(Now-PotionAt>0.6)
        {
            bManaPotionChecked=PS->ManaPotions==ManaBefore-1 && Hero->GetMana()>ResourceBefore+10;
            if(!bManaPotionChecked){Finish(false,TEXT("Mana potion did not restore resource and decrement stock."));return false;}
            Next(7);
        } break;
    default:return true;
    }
    return false;
}

FString UFableRPGValidationDriver::Signature(const AFablePlayerState* PS,bool bIgnoreWear) const
{
    if(!PS) return TEXT("");
    FString Value=FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
        uint8(PS->HeroClass),PS->Level,PS->Experience,PS->Gold,PS->Ore,PS->EquippedWeaponId,PS->EquippedArmorId,
        uint8(PS->ForestQuest),uint8(PS->DungeonQuest),PS->QuestKills);
    Value+=FString::Printf(TEXT("|potions:%d,%d"),PS->HealthPotions,PS->ManaPotions);
    for(const FFableItem& Item:PS->Inventory)
    { FFableItem Copy=Item;if(bIgnoreWear)Copy.Durability=Copy.MaxDurability;Value+=TEXT(";")+FableRPG::ItemPersistentSignature(Copy); }
    Value+=FString::Printf(TEXT("|v3:completed=%d"),PS->DungeonCompletions);
    for(const FFableClassProgress& Progress:PS->ClassProgress)
    {
        Value+=TEXT("|tier=")+FString::FromInt(Progress.Tier);
        for(int32 Rank:Progress.Ranks)Value+=TEXT(",")+FString::FromInt(Rank);
    }
    Value+=FString::Printf(TEXT("|v6:materials=%d,%d,%d|stash"),PS->Essence,PS->RootCores,PS->MoonCrystals);
    for(const FFableItem& Item:PS->StashItems){FFableItem Copy=Item;if(bIgnoreWear)Copy.Durability=Copy.MaxDurability;Value+=TEXT(";")+FableRPG::ItemPersistentSignature(Copy);}
    Value+=FString::Printf(TEXT("|v7:story=%d,%d,%d"),PS->StoryChapter,PS->StoryObjectMask,PS->StorySealMask);
    Value+=TEXT("|v8:talents=")+PS->GetTalentSignature();
    return Value;
}
bool UFableRPGValidationDriver::SerializeReport(const FString& Status,const FString& Reason,FString& OutText)
{
    AFablePlayerState* PS=PC.IsValid()?PC->GetPlayerState<AFablePlayerState>():nullptr;
    TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>();
    Object->SetStringField(TEXT("run_id"),RunId);
    Object->SetStringField(TEXT("role"),Role);
    Object->SetStringField(TEXT("phase"),Phase);
    Object->SetStringField(TEXT("status"),Status);
    Object->SetBoolField(TEXT("report_snapshot_frozen"),Status==TEXT("passed") || Status==TEXT("failed"));
    Object->SetStringField(TEXT("reason"),Reason);
    Object->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());
    Object->SetStringField(TEXT("network_mode"),GetWorld()->GetNetMode()==NM_ListenServer?TEXT("listen_server"):TEXT("client"));
    Object->SetNumberField(TEXT("step"),Step);
    if(const AFableGameState* State=GetWorld()->GetGameState<AFableGameState>())
    {
        Object->SetNumberField(TEXT("active_region"),uint8(State->ActiveRegion));
        Object->SetNumberField(TEXT("adventure_stage"),uint8(State->AdventureStage));
        Object->SetNumberField(TEXT("seal_index"),State->ActiveSealIndex);
        Object->SetNumberField(TEXT("seal_mask"),State->MoonSealMask);
        Object->SetNumberField(TEXT("required_enemies"),State->RequiredEnemies);
        Object->SetNumberField(TEXT("defeated_enemies"),State->DefeatedEnemies);
    }
    Object->SetNumberField(TEXT("player_count"),GetWorld()->GetGameState()?GetWorld()->GetGameState()->PlayerArray.Num():0);
    Object->SetNumberField(TEXT("local_movement_cm"),Movement);
    Object->SetNumberField(TEXT("remote_movement_cm"),RemoteMovement);
    Object->SetNumberField(TEXT("attack_inputs"),AttackInputs);
    Object->SetNumberField(TEXT("skill_inputs"),SkillInputs);
    Object->SetBoolField(TEXT("saw_wave_two"),bSawWaveTwo);
    Object->SetBoolField(TEXT("saw_boss"),bSawBoss);
    Object->SetBoolField(TEXT("saw_enrage"),bSawEnrage);
    Object->SetBoolField(TEXT("warrior_selection_checked"),bSawWarrior);
    Object->SetBoolField(TEXT("remote_upgrade_rejected"),bRejectedRemoteUpgrade);
    Object->SetBoolField(TEXT("invalid_item_rejected"),bRejectedInvalidItem);
    Object->SetBoolField(TEXT("wrong_class_rejected"),bRejectedWrongClass);
    Object->SetBoolField(TEXT("insufficient_gold_rejected"),bRejectedInsufficientFunds);
    Object->SetBoolField(TEXT("armor_equipped"),bArmorEquipped);
    Object->SetBoolField(TEXT("npc_dialogue_checked"),bDialogueChecked);
    Object->SetBoolField(TEXT("health_potion_checked"),bHealthPotionChecked);
    Object->SetBoolField(TEXT("mana_potion_checked"),bManaPotionChecked);
    Object->SetBoolField(TEXT("potion_cooldown_checked"),bPotionCooldownChecked);
    Object->SetBoolField(TEXT("full_potion_rejected"),bFullPotionRejected);
    Object->SetBoolField(TEXT("shop_bought"),bShopBought);
    Object->SetBoolField(TEXT("shop_sold"),bShopSold);
    Object->SetBoolField(TEXT("remote_shop_rejected"),bRemoteShopRejected);
    Object->SetBoolField(TEXT("protected_sale_rejected"),bProtectedSaleRejected);
    Object->SetBoolField(TEXT("poor_shop_rejected"),bPoorShopRejected);
    Object->SetBoolField(TEXT("skills_enabled"),bSkills);
    Object->SetBoolField(TEXT("premature_advancement_rejected"),bPrematureAdvanceRejected);
    Object->SetBoolField(TEXT("locked_skills_rejected"),bLockedSkillsRejected);
    Object->SetBoolField(TEXT("locked_upgrade_rejected"),bLockedUpgradeRejected);
    Object->SetBoolField(TEXT("skill_points_rejected"),bSkillPointsRejected);
    Object->SetBoolField(TEXT("skill_panel_blocks_input"),bSkillPanelBlocksInput);
    Object->SetBoolField(TEXT("passive_upgraded"),bPassiveUpgraded);
    Object->SetBoolField(TEXT("second_advancement"),bSecondAdvancement);
    Object->SetBoolField(TEXT("skill_rank_three"),bSkillRankThree);
    Object->SetBoolField(TEXT("third_advancement"),bThirdAdvancement);
    Object->SetBoolField(TEXT("duplicate_advancement_rejected"),bDuplicateAdvanceRejected);
    Object->SetBoolField(TEXT("skill_cooldown_rejected"),bSkillCooldownRejected);
    Object->SetBoolField(TEXT("warrior_skills_checked"),bWarriorSkillsChecked);
    Object->SetBoolField(TEXT("class_switch_preserved"),bClassSwitchPreserved);
    Object->SetBoolField(TEXT("advancement_survives_repeat"),bAdvancementSurvivesRepeat);
    Object->SetBoolField(TEXT("second_dungeon_completed"),bSecondDungeonCompleted);
    Object->SetBoolField(TEXT("slow_observed"),bSlowObserved);
    Object->SetBoolField(TEXT("piercing_observed"),bPiercingObserved);
    Object->SetNumberField(TEXT("max_piercing_unique_hits"),MaxPiercingHits);
    Object->SetBoolField(TEXT("piercing_multiple_enemies_observed"),MaxPiercingHits>=2);
    Object->SetStringField(TEXT("piercing_multiple_enemies_result"),MaxPiercingHits>=2?TEXT("observed"):TEXT("not_observed"));
    if(const AFableCharacter* Hero=PC.IsValid()?PC->GetPawn<AFableCharacter>():nullptr)
    {
        Object->SetNumberField(TEXT("shield_absorbed"),Hero->GetTotalShieldAbsorbed());
        TArray<TSharedPtr<FJsonValue>> Counts;
        for(int32 Slot=0;Slot<3;++Slot)Counts.Add(MakeShared<FJsonValueNumber>(Hero->GetSkillCastCount(Slot)));
        Object->SetArrayField(TEXT("skill_cast_counts"),Counts);
    }

    Object->SetStringField(TEXT("profile_signature"),Signature(PS));
    Object->SetStringField(TEXT("adventure_signature"),AdventureSignature(PS));
    Object->SetBoolField(TEXT("adventure_enabled"),bAdventure);
    Object->SetBoolField(TEXT("quest_journal_open"),PC.IsValid() && PC->IsQuestJournalOpen());
    Object->SetBoolField(TEXT("skills_panel_open"),PC.IsValid() && PC->IsSkillsOpen());
    Object->SetBoolField(TEXT("trial_started"),bTrialStarted);
    Object->SetBoolField(TEXT("premature_trial_rejected"),bPrematureTrialRejected);
    Object->SetBoolField(TEXT("moon_locked_before_root_reward"),bMoonLocked);
    Object->SetBoolField(TEXT("seal_locked_while_enemies_alive"),bSealLocked);
    Object->SetBoolField(TEXT("moon_ripple_observed"),bMoonRipple);
    Object->SetBoolField(TEXT("moon_rain_observed"),bMoonRain);
    Object->SetBoolField(TEXT("moon_ranged_observed"),bMoonRanged);
    Object->SetBoolField(TEXT("moon_cleared"),bMoonCleared);
    Object->SetNumberField(TEXT("observed_seal_mask"),ObservedSealMask);
    Object->SetBoolField(TEXT("root_rewards_isolated"),bNoRootCrossCredit);
    Object->SetBoolField(TEXT("trial_class_switch_preserved"),bTrialSwitchPreserved);
    Object->SetBoolField(TEXT("trial_duplicate_reward_rejected"),bTrialDuplicateRejected);
    if(PS)
    {
        Object->SetBoolField(TEXT("save_succeeded"),PS->bLastSaveSucceeded);
        Object->SetStringField(TEXT("last_notice"),PS->LastNotice);
        Object->SetBoolField(TEXT("near_forge"),PC->IsNearForge());
        if(PC->GetPawn())Object->SetStringField(TEXT("position"),PC->GetPawn()->GetActorLocation().ToString());

        TArray<TSharedPtr<FJsonValue>> ClassData;
        for(int32 ClassIndex=0;ClassIndex<PS->ClassProgress.Num();++ClassIndex)
        {
            const FFableClassProgress& Progress=PS->ClassProgress[ClassIndex];
            TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
            Entry->SetNumberField(TEXT("class"),ClassIndex);
            Entry->SetNumberField(TEXT("tier"),Progress.Tier);
            Entry->SetNumberField(TEXT("second_talent"),uint8(Progress.SecondTalent));
            Entry->SetNumberField(TEXT("third_talent"),uint8(Progress.ThirdTalent));
            Entry->SetNumberField(TEXT("second_trial_stage"),uint8(Progress.SecondTrial.Stage));
            Entry->SetNumberField(TEXT("second_trial_casts"),Progress.SecondTrial.ValidSkillCasts);
            Entry->SetBoolField(TEXT("second_trial_legacy"),Progress.SecondTrial.bLegacyGranted);
            Entry->SetNumberField(TEXT("third_trial_stage"),uint8(Progress.ThirdTrial.Stage));
            Entry->SetNumberField(TEXT("third_trial_casts"),Progress.ThirdTrial.ValidSkillCasts);
            Entry->SetBoolField(TEXT("third_trial_legacy"),Progress.ThirdTrial.bLegacyGranted);
            TArray<TSharedPtr<FJsonValue>> Ranks;
            for(int32 Rank:Progress.Ranks)Ranks.Add(MakeShared<FJsonValueNumber>(Rank));
            Entry->SetArrayField(TEXT("ranks"),Ranks);
            ClassData.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Object->SetArrayField(TEXT("class_progress"),ClassData);

        Object->SetNumberField(TEXT("advancement_tier"),PS->GetAdvancementTier());
        Object->SetNumberField(TEXT("skill_points"),PS->GetAvailableSkillPoints());
        Object->SetNumberField(TEXT("dungeon_completions"),PS->DungeonCompletions);
        Object->SetNumberField(TEXT("moonwell_quest"),uint8(PS->MoonwellQuest));
        Object->SetNumberField(TEXT("moonwell_completions"),PS->MoonwellCompletions);
        Object->SetNumberField(TEXT("class"),uint8(PS->HeroClass));
        Object->SetNumberField(TEXT("level"),PS->Level);
        Object->SetNumberField(TEXT("gold"),PS->Gold);
        Object->SetNumberField(TEXT("ore"),PS->Ore);
        Object->SetNumberField(TEXT("inventory_count"),PS->Inventory.Num());
        Object->SetNumberField(TEXT("health_potions"),PS->HealthPotions);
        Object->SetNumberField(TEXT("mana_potions"),PS->ManaPotions);
        Object->SetNumberField(TEXT("forest_quest"),uint8(PS->ForestQuest));
        Object->SetNumberField(TEXT("dungeon_quest"),uint8(PS->DungeonQuest));
        Object->SetNumberField(TEXT("attack_power"),PS->GetAttackPower());
        Object->SetNumberField(TEXT("defense"),PS->GetDefense());
    }
    if(bTalents) AppendTalentReport(Object);
    if(bStory) AppendStoryReport(Object);
    if(bLoot) AppendLootReport(Object);
    if(bWorkshop) AppendWorkshopReport(Object);
    TArray<TSharedPtr<FJsonValue>> Shots;
    for(const FString& Shot:Screenshots) Shots.Add(MakeShared<FJsonValueString>(Shot));
    Object->SetArrayField(TEXT("screenshots"),Shots);
    OutText.Empty();
    TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&OutText);
    return FJsonSerializer::Serialize(Object,Writer);
}
void UFableRPGValidationDriver::Report(const FString& Status,const FString& Reason)
{
    if(bDone || bTerminalPending) return;
    FString Text;
    if(SerializeReport(Status,Reason,Text))
        FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Directory,Role+TEXT(".json")));
    // A transient running-report failure is retried by the next ordinary report tick.
}
void UFableRPGValidationDriver::PublishTerminalReport()
{
    if(!bTerminalPending || bDone) return;
    const double Now=FPlatformTime::Seconds();
    if(Now<TerminalNextAttempt) return;
    TerminalNextAttempt=Now+0.2; ++TerminalAttempts;
    const FString Path=FPaths::Combine(Directory,Role+TEXT(".json"));
    if(FFileHelper::SaveStringToFile(TerminalJson,*Path))
    {
        bTerminalPending=false;bDone=true;
        UE_LOG(LogTemp,Display,TEXT("FABLE_RPG_VALIDATION role=%s status=%s attempts=%d reason=%s"),*Role,*TerminalStatus,TerminalAttempts,*TerminalReason);
        return;
    }
    const uint32 Error=FPlatformMisc::GetLastError();
    if(TerminalAttempts==1 || TerminalAttempts%25==0)
        UE_LOG(LogTemp,Warning,TEXT("FABLE_RPG_VALIDATION_REPORT_RETRY role=%s phase=%s attempts=%d last_error=%u path=%s"),*Role,*Phase,TerminalAttempts,Error,*Path);
    if(Now-TerminalStartedAt>=30)
    {
        bTerminalPending=false;bDone=true;
        UE_LOG(LogTemp,Error,TEXT("FABLE_RPG_VALIDATION_REPORT_IO_FAILED role=%s phase=%s attempts=%d last_error=%u path=%s"),*Role,*Phase,TerminalAttempts,Error,*Path);
    }
}
void UFableRPGValidationDriver::Finish(bool bSuccess,const FString& Reason)
{
    if(bDone || bTerminalPending) return;
    if(bSuccess && bWorkshop && Phase==TEXT("fresh") && !bWorkshopStarted)
    {bWorkshopStarted=true;PC->UIClosePanels();Advance(100);return;}
    Hold(EKeys::W,false);Hold(EKeys::S,false);Hold(EKeys::A,false);Hold(EKeys::D,false);Hold(EKeys::E,false);Release();
    if(PC.IsValid())PC->UIStopRevive();
    TerminalStatus=bSuccess?TEXT("passed"):TEXT("failed");TerminalReason=Reason;
    if(!SerializeReport(TerminalStatus,TerminalReason,TerminalJson))
    {
        bDone=true;
        UE_LOG(LogTemp,Error,TEXT("FABLE_RPG_VALIDATION_REPORT_IO_FAILED role=%s reason=serialization_failed"),*Role);
        return;
    }
    bTerminalPending=true;TerminalStartedAt=FPlatformTime::Seconds();TerminalNextAttempt=0;
    PublishTerminalReport();
}
void UFableRPGValidationDriver::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
#if !UE_BUILD_SHIPPING
    Release();
    if(bTerminalPending){PublishTerminalReport();return;}
    if(bDone || !PC.IsValid()) return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now-StartedAt>(bTalents?1500:bWorkshop?2400:bLoot?1800:bAdventure?1100:bSkills?720:540)) { Finish(false,TEXT("RPG scenario timeout.")); return; }
    AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    if(!PS || !PS->bProfileLoaded || !Hero || !GS || GS->PlayerArray.Num()!=2) return;
    const FVector Position=Hero->GetActorLocation();
    if(!PreviousLocation.IsZero())
    { const double D=FVector::Dist2D(Position,PreviousLocation); if(D<250) Movement+=D; }
    PreviousLocation=Position;
    for(TActorIterator<AFableCharacter> It(GetWorld());It;++It) if(*It!=Hero && It->GetPlayerState())
    {
        if(!PreviousRemote.IsZero()) { const double D=FVector::Dist2D(It->GetActorLocation(),PreviousRemote); if(D<250) RemoteMovement+=D; }
        PreviousRemote=It->GetActorLocation(); break;
    }
    if(bTalents) { TickTalents(); return; }
    if(bWorkshop) { TickWorkshop(); return; }
    if(bLoot) { TickLoot(); return; }
    if(bStory && Phase==TEXT("fresh") && (Step==5 || Step==11 || Step==21 || Step==22))
    {
        bool bRecoveryPriority=GS->bPartyRecoveryPending || !Hero->IsAlive();
        for(TActorIterator<AFableCharacter> It(GetWorld());It;++It)
            if(It->GetPlayerState() && (It->IsDowned() || It->GetReviveTarget() || It->GetReviver()))bRecoveryPriority=true;
        if(bRecoveryPriority){TickAdventure();return;}
    }
    if(bStory && TickStory()) return;
    if(bAdventure) { TickAdventure(); return; }
    ObserveSkills();
    const double Wait=Now-StepAt;
    const EFableClass Wanted=Role==TEXT("host")?EFableClass::Mage:EFableClass::Archer;
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
    if(Phase==TEXT("resume"))
    {
        const FFableItem* Weapon=PS->FindItem(PS->EquippedWeaponId);
        const FFableItem* Armor=PS->FindItem(PS->EquippedArmorId);
        if(Wait>3)
        {
            if(PS->HeroClass!=Wanted || PS->ForestQuest!=EFableQuestStage::Completed || PS->DungeonQuest!=EFableQuestStage::Completed ||
                !Weapon || !Armor || Weapon->Rarity!=2 || Weapon->Upgrade!=1 || PS->GetDefense()<Armor->GetEffectivePower() || !PS->bLastSaveSucceeded)
            { Finish(false,TEXT("Reload did not restore completed quests and enhanced class weapon.")); return; }
            if(bSkills && (PS->GetAdvancementTier()!=3 || PS->DungeonCompletions<2 || PS->GetSkillRank(0)!=3
                || PS->GetSkillRank(1)!=2 || PS->GetSkillRank(2)!=2 || PS->GetSkillRank(3)!=3))
            {Finish(false,TEXT("Reload lost advanced class, skill investments or permanent completion history."));return;}
            if(Screenshots.IsEmpty()) { Key(EKeys::I); Capture(TEXT("restored-profile")); StepAt=Now; }
            else Finish(true,TEXT("Persistent profile restored in a new process."));
        }
        return;
    }
    bSawWaveTwo |= GS->AdventureStage==EFableAdventureStage::Dungeon && GS->DungeonWave>=2;
    bSawBoss |= GS->AdventureStage==EFableAdventureStage::Boss;
    bSawEnrage |= GS->AdventureStage==EFableAdventureStage::Boss && GS->BossMaxHealth>0 && GS->BossHealth<=GS->BossMaxHealth*0.5f && GS->BossHealth>0;
    if(bSkills)
    {
        if(Step==3 && !RunSkillChecks(0))return;
        if(Step==7 && !RunSkillChecks(1))return;
        if(Step==23 && !RunSkillChecks(2))return;
    }
    switch(Step)
    {
    case 0:
        if(WalkTo(Board)) Advance(1);
        break;
    case 1:
        if(Wait>0.5 && LastAction<0) { Key(EKeys::One); LastAction=Now; }
        if(Wait>1.3 && PS->HeroClass==EFableClass::Warrior) { bSawWarrior=true; Advance(2); }
        break;
    case 2:
        if(Wait>0.4 && LastAction<0) { Key(Wanted==EFableClass::Mage?EKeys::Two:EKeys::Three); LastAction=Now; }
        if(Wait>1.3 && PS->HeroClass==Wanted) { Capture(TEXT("class-selected")); Advance(3); }
        break;
    case 3:
        if(Wait>0.5) QuestInteraction();
        if(PS->ForestQuest==EFableQuestStage::Active) Advance(4);
        break;
    case 4:
    {
        bool bAllAccepted=true;
        for(APlayerState* Other:GS->PlayerArray)
        { const AFablePlayerState* P=Cast<AFablePlayerState>(Other); bAllAccepted &= P && P->ForestQuest!=EFableQuestStage::Available; }
        if(bAllAccepted && PreparePotions()) Fight();
        if(PS->ForestQuest==EFableQuestStage::Ready) { Capture(TEXT("forest-complete")); Advance(5); }
        break;
    }
    case 5: if(WalkTo(Board)) Advance(6); break;
    case 6:
        if(Wait>0.5) QuestInteraction();
        if(PS->ForestQuest==EFableQuestStage::Completed)
        {
            for(const FFableItem& Item:PS->Inventory) if(Item.Slot==EFableEquipmentSlot::Weapon && Item.Rarity==1 && Item.HeroClass==Wanted) RareId=Item.Id;
            if(RareId<=0) { Finish(false,TEXT("Forest quest reward missing.")); return; }
            Advance(7);
        } break;
    case 7:
        if(Wait>0.4 && LastAction<0) { Key(EKeys::I); LastAction=Now; }
        if(Wait>1.1 && PC->IsInventoryOpen()) { PC->UISelectItem(RareId); Key(EKeys::E); Advance(8); }
        break;
    case 8:
        if(Wait>1.0 && PS->EquippedWeaponId==RareId)
        { GoldBefore=PS->Gold; OreBefore=PS->Ore; PC->UIEnhance(RareId); Advance(9); }
        break;
    case 9:
        if(Wait>1.0)
        {
            const FFableItem* Item=PS->FindItem(RareId);
            bRejectedRemoteUpgrade=Item && Item->Upgrade==0 && PS->Gold==GoldBefore && PS->Ore==OreBefore;
            if(!bRejectedRemoteUpgrade) { Finish(false,TEXT("Enhancement succeeded away from forge.")); return; }
            PC->UIEquip(999999); Advance(10);
        } break;
    case 10:
        if(Wait>0.8)
        {
            bRejectedInvalidItem=PS->EquippedWeaponId==RareId;
            if(!bRejectedInvalidItem) { Finish(false,TEXT("Invalid item changed equipment.")); return; }
            PC->UIEquip(1); Advance(33);
        } break;
    case 11: if(WalkTo(FableRPG::ForgeLocation()+FVector(Role==TEXT("host")?-100:100,-100,100))) Advance(12); break;
    case 12:
        if(Wait>0.4 && LastAction<0) { Key(EKeys::I); LastAction=Now; }
        if(Wait>1.0 && PC->IsInventoryOpen()) { PC->UISelectItem(RareId); GoldBefore=PS->Gold; OreBefore=PS->Ore; Key(EKeys::U); Advance(13); }
        break;
    case 13:
        if(Wait>3.0)
        {
            const FFableItem* Item=PS->FindItem(RareId);
            if(!Item || Item->Upgrade!=1 || PS->Gold!=GoldBefore-30 || PS->Ore!=OreBefore-2)
            { Capture(TEXT("forge-failure")); Finish(false,TEXT("Forge enhancement cost or result mismatch.")); return; }
            GoldBefore=PS->Gold; OreBefore=PS->Ore; Key(EKeys::U); Advance(32);
        } break;
    case 14: if(Wait>0.8) { Key(EKeys::I); Advance(15); } break;
    case 15: if(WalkTo(Board)) Advance(16); break;
    case 16:
        if(Wait>0.5) QuestInteraction();
        if(PS->DungeonQuest==EFableQuestStage::Active) Advance(17);
        break;
    case 17:
    {
        bool bReady=true;
        for(APlayerState* Other:GS->PlayerArray) { const AFablePlayerState* P=Cast<AFablePlayerState>(Other); bReady &= P && P->IsDungeonEligible(); }
        if(bReady) Advance(18);
        break;
    }
    case 18:
        if(GS->AdventureStage==EFableAdventureStage::Dungeon || GS->AdventureStage==EFableAdventureStage::Boss) { Advance(19); break; }
        if(WalkTo(FableRPG::PortalLocation()+FVector(0,-100,100)) && Now-LastAction>1) { Key(EKeys::E); LastAction=Now; }
        break;
    case 19:
        if(!bBossCaptured && GS->AdventureStage==EFableAdventureStage::Boss) { Capture(TEXT("boss-battle")); bBossCaptured=true; }
        Fight();
        if(GS->AdventureStage==EFableAdventureStage::Village) { Finish(false,TEXT("Party wiped during dungeon validation.")); return; }
        if(GS->AdventureStage==EFableAdventureStage::Cleared && PS->DungeonQuest==EFableQuestStage::Ready)
        { Capture(TEXT("boss-defeated")); Advance(20); }
        break;
    case 20:
        if(GS->AdventureStage==EFableAdventureStage::Village || Hero->GetActorLocation().X<4000) { Advance(21); break; }
        if(WalkTo(FableRPG::DungeonEntrance()) && Now-LastAction>1) { Key(EKeys::E); LastAction=Now; }
        break;
    case 21: if(WalkTo(Board)) Advance(22); break;
    case 22:
        if(Wait>0.5) QuestInteraction();
        if(PS->DungeonQuest==EFableQuestStage::Completed)
        {
            for(const FFableItem& Item:PS->Inventory) if(Item.Slot==EFableEquipmentSlot::Weapon && Item.Rarity==2 && Item.HeroClass==Wanted) EpicId=Item.Id;
            if(EpicId<=0) { Finish(false,TEXT("Dungeon quest reward missing.")); return; }
            Advance(23);
        } break;
    case 23:
        if(Wait>0.4 && LastAction<0) { Key(EKeys::I); LastAction=Now; }
        if(Wait>1.0 && PC->IsInventoryOpen())
        {
            int32 ArmorId=0;
            for(const FFableItem& Item:PS->Inventory) if(Item.Slot==EFableEquipmentSlot::Armor && Item.Rarity==2) ArmorId=Item.Id;
            if(ArmorId<=0) { Finish(false,TEXT("Boss armor loot missing.")); return; }
            PC->UISelectItem(ArmorId); Key(EKeys::E); Advance(30);
        }
        break;
    case 24:
        if(Wait>1 && PS->EquippedWeaponId==EpicId) { Key(EKeys::I); Advance(25); }
        break;
    case 25: if(WalkTo(FableRPG::ForgeLocation()+FVector(Role==TEXT("host")?-100:100,-100,100))) Advance(26); break;
    case 26:
        if(Wait>0.4 && LastAction<0) { Key(EKeys::I); LastAction=Now; }
        if(Wait>1.0 && PC->IsInventoryOpen()) { PC->UISelectItem(EpicId); Key(EKeys::U); Advance(27); }
        break;
    case 27:
        if(Wait>1.2)
        {
            const FFableItem* Weapon=PS->FindItem(EpicId);
            if(!Weapon || Weapon->Upgrade!=1 || !PS->bLastSaveSucceeded || !bSawWaveTwo || !bSawBoss || !bSawEnrage)
            { Finish(false,TEXT("Final upgrade/save/dungeon checkpoints missing.")); return; }
            Capture(TEXT("rpg-complete")); Advance(28);
        } break;
    case 28:
        if(Wait>1.5)
        {
            PC->UIClosePanels(); GoldBefore=PS->Gold; InventoryBefore=PS->Inventory.Num();
            PC->UIShopBuy(EFableShopOffer::HealthPotion); Advance(40);
        } break;
    case 40:
        if(Wait>0.7)
        {
            bRemoteShopRejected=PS->Gold==GoldBefore && PS->Inventory.Num()==InventoryBefore;
            if(!bRemoteShopRejected){Finish(false,TEXT("Shop request worked outside merchant radius."));return;}
            Advance(41);
        } break;
    case 41:
        if(WalkTo(FableRPG::MerchantLocation()+FVector(Role==TEXT("host")?-70:85,-120,100),90))
        {Key(EKeys::E);Advance(42);}
        break;
    case 42:
        if(Wait>1)
        {
            Capture(TEXT("village-shop")); GoldBefore=PS->Gold; HealthBefore=PS->HealthPotions;
            PC->UIShopBuy(EFableShopOffer::HealthPotion); Advance(43);
        } break;
    case 43:
        if(Wait>0.8)
        {
            if(PS->Gold!=GoldBefore-15 || PS->HealthPotions!=HealthBefore+1){Finish(false,TEXT("Potion purchase mismatch."));return;}
            GoldBefore=PS->Gold; ManaBefore=PS->ManaPotions; PC->UIShopBuy(EFableShopOffer::ManaPotion); Advance(44);
        } break;
    case 44:
        if(Wait>0.8)
        {
            if(PS->Gold!=GoldBefore-12 || PS->ManaPotions!=ManaBefore+1){Finish(false,TEXT("Mana purchase mismatch."));return;}
            GoldBefore=PS->Gold; InventoryBefore=PS->Inventory.Num(); PC->UIShopBuy(EFableShopOffer::Armor); Advance(45);
        } break;
    case 45:
        if(Wait>0.8)
        {
            for(const FFableItem& Item:PS->Inventory) if(Item.Slot==EFableEquipmentSlot::Armor && Item.Rarity==1 && Item.RollVersion==1 && FableRPG::IsValidItemRoll(Item)) ShopItemId=Item.Id;
            bShopBought=ShopItemId>0 && PS->Inventory.Num()==InventoryBefore+1 && PS->Gold==GoldBefore-80;
            if(!bShopBought){Finish(false,TEXT("Shop armor purchase mismatch."));return;}
            GoldBefore=PS->Gold; PC->UISell(PS->EquippedWeaponId); Advance(46);
        } break;
    case 46:
        if(Wait>0.7)
        {
            if(PS->Gold!=GoldBefore || !PS->FindItem(EpicId)){Finish(false,TEXT("Equipped item was sold."));return;}
            PC->UISell(1); Advance(47);
        } break;
    case 47:
        if(Wait>0.7)
        {
            bProtectedSaleRejected=PS->Gold==GoldBefore && PS->FindItem(1);
            if(!bProtectedSaleRejected){Finish(false,TEXT("Protected starter item was sold."));return;}
            PC->UISell(ShopItemId); Advance(48);
        } break;
    case 48:
        if(Wait>0.8)
        {
            bShopSold=!PS->FindItem(ShopItemId) && PS->Gold==GoldBefore+25 && PS->Inventory.Num()==InventoryBefore;
            if(!bShopSold){Finish(false,TEXT("Shop sale result mismatch."));return;}
            Advance(49);
        } break;
    case 49:
        // Buy actual stock until the account cannot afford another 100-gold weapon.
        if(Wait>0.5 && Now-LastAction>0.7)
        {
            if(PS->Gold>=100){PC->UIShopBuy(EFableShopOffer::WarriorWeapon);LastAction=Now;}
            else{GoldBefore=PS->Gold;InventoryBefore=PS->Inventory.Num();PC->UIShopBuy(EFableShopOffer::WarriorWeapon);Advance(50);}
        } break;
    case 50:
        if(Wait>0.8)
        {
            bPoorShopRejected=PS->Gold==GoldBefore && PS->Inventory.Num()==InventoryBefore;
            if(!bPoorShopRejected){Finish(false,TEXT("Unaffordable shop purchase mutated profile."));return;}
            Capture(TEXT("shop-complete")); Advance(51);
        } break;
    case 51:
        if(Wait>1){GoldBefore=PS->Gold;HealthBefore=PS->HealthPotions;PC->UIShopBuy(EFableShopOffer::HealthPotion);Advance(52);}
        break;
    case 52:
        if(Wait>1)
        {
            if(PS->Gold!=GoldBefore-15 || PS->HealthPotions!=HealthBefore+1){Finish(false,TEXT("Extra health potion purchase mismatch."));return;}
            GoldBefore=PS->Gold;ManaBefore=PS->ManaPotions;PC->UIShopBuy(EFableShopOffer::ManaPotion);Advance(53);
        }break;
    case 53:
        if(Wait>1)
        {
            if(PS->Gold!=GoldBefore-12 || PS->ManaPotions!=ManaBefore+1 || PS->HealthPotions!=4 || PS->ManaPotions!=3)
            {Finish(false,TEXT("Non-default potion inventory was not saved for reload validation."));return;}
            Capture(TEXT("saved-potion-stock"));Advance(54);
        }break;
    case 54:if(Wait>1){Key(EKeys::F1);Advance(55);}break;
    case 55:
        if(Wait>1)
        {
            if(!PC->IsHelpOpen()){Finish(false,TEXT("F1 did not open gameplay help."));return;}
            Capture(TEXT("help"));Advance(56);
        }break;
    case 56:
        if(bSkills)
        {
            if(RunSkillChecks(3))Advance(160);
            break;
        }
        if(Wait>1)Finish(bDialogueChecked&&bHealthPotionChecked&&bManaPotionChecked&&bPotionCooldownChecked&&bFullPotionRejected,
            TEXT("NPC quests, trade, potion recovery and restrictions, boss loop, help, and non-default saved potion stock checked."));
        break;

    case 160:
        PC->UIClosePanels();
        if(WalkTo(Board,90))Advance(161);
        break;
    case 161:
        if(PS->DungeonQuest==EFableQuestStage::Active)
        {
            bAdvancementSurvivesRepeat=PS->GetAdvancementTier()==3 && PS->DungeonCompletions>=1;
            if(!bAdvancementSurvivesRepeat){Finish(false,TEXT("Repeated dungeon acceptance lost permanent advancement eligibility."));return;}
            Advance(162);
        }
        else if(Wait>0.8)QuestInteraction();
        break;
    case 162:
    {
        bool bAllReady=true;
        for(APlayerState* Other:GS->PlayerArray)
        {const AFablePlayerState* Profile=Cast<AFablePlayerState>(Other);bAllReady&=Profile&&Profile->IsDungeonEligible();}
        if(bAllReady)Advance(163);
        break;
    }
    case 163:
        if(GS->AdventureStage==EFableAdventureStage::Dungeon || GS->AdventureStage==EFableAdventureStage::Boss)
        {bSecondDungeon=true;SecondDungeonAt=Now;Advance(164);break;}
        if(WalkTo(FableRPG::PortalLocation()+FVector(Role==TEXT("host")?-80:80,-120,100),110) && Now-LastAction>1)
        {Key(EKeys::E);LastAction=Now;}
        break;
    case 164:
        if(!Hero->IsAlive()){Finish(false,TEXT("A participant died during advanced skill combat."));return;}
        Fight();
        if(GS->AdventureStage==EFableAdventureStage::Village)
        {Finish(false,TEXT("Party wiped in the advanced-skill expedition."));return;}
        if(GS->AdventureStage==EFableAdventureStage::Cleared && PS->DungeonQuest==EFableQuestStage::Ready)
        {
            if(Role==TEXT("host") && (!bSlowObserved || Hero->GetTotalShieldAbsorbed()<=0))
            {Finish(false,TEXT("Real mage slow/shield absorption was not observed."));return;}
            Capture(TEXT("skills-second-dungeon-complete"));Advance(165);
        }
        break;
    case 165:
        if(GS->AdventureStage==EFableAdventureStage::Village || Hero->GetActorLocation().X<4000){Advance(166);break;}
        if(WalkTo(FableRPG::DungeonEntrance()) && Now-LastAction>1){Key(EKeys::E);LastAction=Now;}
        break;
    case 166:if(WalkTo(Board,90))Advance(167);break;
    case 167:
        if(PS->DungeonQuest==EFableQuestStage::Completed && PS->DungeonCompletions>=2)
        {bSecondDungeonCompleted=true;Key(EKeys::P);Advance(168);}
        else if(Wait>0.8)QuestInteraction();
        break;
    case 168:
        if(Wait>1 && PC->IsSkillsOpen()){Capture(TEXT("skills-final-saved"));Advance(169);}
        break;
    case 169:
        if(Wait>2)
            Finish(SkillAssertionsComplete() && bDialogueChecked && bHealthPotionChecked && bManaPotionChecked
                && bPotionCooldownChecked && bFullPotionRejected,
                TEXT("Skills, advancement, real second-dungeon effects, restrictions and persistent profile checked; optional piercing multi-hit result is reported separately."));
        break;

    case 30:
        if(Wait>1.0)
        {
            const FFableItem* Armor=PS->FindItem(PS->EquippedArmorId);
            bArmorEquipped=Armor && Armor->Rarity==2 && PS->GetDefense()>=Armor->GetEffectivePower() && FableRPG::IsValidItemRoll(*Armor) && Hero->GetMaxHealth()>=134;
            if(!bArmorEquipped) { Finish(false,TEXT("Armor equip did not update defense and health.")); return; }
            PC->UISelectItem(EpicId); Key(EKeys::E); Advance(24);
        } break;
    case 32:
        if(Wait>1.0)
        {
            const FFableItem* Item=PS->FindItem(RareId);
            bRejectedInsufficientFunds=Item && Item->Upgrade==1 && PS->Gold==GoldBefore && PS->Ore==OreBefore;
            if(!bRejectedInsufficientFunds) { Finish(false,TEXT("Insufficient-gold enhancement mutated state.")); return; }
            Capture(TEXT("enhanced-gear")); Advance(14);
        } break;
    case 33:
        if(Wait>1.0)
        {
            bRejectedWrongClass=PS->EquippedWeaponId==RareId;
            if(!bRejectedWrongClass) { Finish(false,TEXT("Wrong-class weapon was equipped.")); return; }
            Key(EKeys::I); Advance(11);
        } break;

    default: Finish(false,TEXT("Unknown validation step.")); break;
    }
#endif
}
