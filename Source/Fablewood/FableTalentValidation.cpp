#include "FableRPGValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableEnemyBolt.h"
#include "FableProjectile.h"
#include "FableGameState.h"
#include "FableSaveGame.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
namespace
{
FString TalentProtectedProfile(const AFablePlayerState* S)
{
    FString V=FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d"),
        int32(S->HeroClass),S->Level,S->Experience,S->Ore,S->Essence,S->RootCores,S->MoonCrystals,
        S->HealthPotions,S->ManaPotions,S->EquippedWeaponId,S->EquippedArmorId,int32(S->ForestQuest),
        int32(S->DungeonQuest),int32(S->MoonwellQuest),S->QuestKills,S->DungeonCompletions,S->MoonwellCompletions,
        S->StoryChapter,S->StoryObjectMask,S->StorySealMask);
    for(const FFableItem& I:S->Inventory)V+=TEXT("|i:")+FableRPG::ItemPersistentSignature(I);
    for(const FFableItem& I:S->StashItems)V+=TEXT("|s:")+FableRPG::ItemPersistentSignature(I);
    for(int32 I=0;I<S->ClassProgress.Num();++I)
    {
        const FFableClassProgress& C=S->ClassProgress[I];
        V+=FString::Printf(TEXT("|c:%d:%d,%d,%d;%d,%d,%d"),C.Tier,int32(C.SecondTrial.Stage),C.SecondTrial.ValidSkillCasts,C.SecondTrial.bLegacyGranted,
            int32(C.ThirdTrial.Stage),C.ThirdTrial.ValidSkillCasts,C.ThirdTrial.bLegacyGranted);
        if(I!=int32(S->HeroClass)){V+=FString::Printf(TEXT(":%d,%d"),int32(C.SecondTalent),int32(C.ThirdTalent));for(int32 R:C.Ranks)V+=TEXT(",")+FString::FromInt(R);}
    }
    return V;
}
int32 TalentLiveEnemies(UWorld* World){int32 N=0;for(TActorIterator<AFableEnemy> I(World);I;++I)if(I->IsAlive())++N;return N;}
int32 TalentPlayerProjectiles(UWorld* World){int32 N=0;for(TActorIterator<AFableProjectile> I(World);I;++I)++N;return N;}
int32 TalentProjectiles(UWorld* World){int32 N=0;for(TActorIterator<AFableEnemyBolt> I(World);I;++I)++N;for(TActorIterator<AFableProjectile> I(World);I;++I)++N;return N;}
}
int32 UFableRPGValidationDriver::TalentPeerInt(const FString& Field) const
{
    FString Data;TSharedPtr<FJsonObject> Object;double Number=-1;
    if(FFileHelper::LoadFileToString(Data,*FPaths::Combine(Directory,Role==TEXT("host")?TEXT("guest.json"):TEXT("host.json")))
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data),Object) && Object.IsValid())
    {
        FString PeerRun,PeerPhase;
        if(Object->TryGetStringField(TEXT("run_id"),PeerRun) && PeerRun==RunId && Object->TryGetStringField(TEXT("phase"),PeerPhase) && PeerPhase==Phase)
            Object->TryGetNumberField(Field,Number);
    }
    return int32(Number);
}
void UFableRPGValidationDriver::TalentRecord(const FString& Action)
{
    const AFablePlayerState* S=PC->GetPlayerState<AFablePlayerState>();const AFableCharacter* H=PC->GetPawn<AFableCharacter>();
    TSharedRef<FJsonObject> E=MakeShared<FJsonObject>();E->SetStringField(TEXT("action"),Action);E->SetNumberField(TEXT("cycle"),TalentCycle);
    E->SetNumberField(TEXT("at"),GetWorld()->GetTimeSeconds());E->SetNumberField(TEXT("class"),int32(S->HeroClass));
    E->SetStringField(TEXT("choices"),S->GetTalentSignature());E->SetNumberField(TEXT("gold"),S->Gold);E->SetNumberField(TEXT("points"),S->GetAvailableSkillPoints());
    E->SetNumberField(TEXT("first_rank"),S->GetTalentRank(EFableTalentPath::First));E->SetNumberField(TEXT("second_rank"),S->GetTalentRank(EFableTalentPath::Second));
    E->SetNumberField(TEXT("burn_damage"),H->GetTalentBurnDamageDealt());E->SetNumberField(TEXT("slow_hits"),H->GetTalentSlowHits());E->SetNumberField(TEXT("ally_shields"),H->GetTalentAllyShieldsGranted());
    E->SetNumberField(TEXT("shield"),H->GetShieldAmount());E->SetNumberField(TEXT("shield_capacity"),H->GetSkillShieldCapacity());
    E->SetNumberField(TEXT("shield_duration"),H->GetSkillShieldDuration());E->SetNumberField(TEXT("movement_limit"),H->GetMovementSpeedLimit());
    E->SetNumberField(TEXT("maximum_observed_speed"),TalentMaximumSpeed);E->SetNumberField(TEXT("dodge_duration"),H->GetDodgeCooldownDuration());
    E->SetNumberField(TEXT("l_multiplier"),H->GetSkillDamageMultiplier(1));E->SetNumberField(TEXT("l_range"),H->GetSkillRange(1));
    E->SetNumberField(TEXT("k_multiplier"),H->GetSkillDamageMultiplier(0));E->SetNumberField(TEXT("k_range"),H->GetSkillRange(0));
    E->SetBoolField(TEXT("clean_damage_window"),bTalentCleanDamageWindow);
    E->SetNumberField(TEXT("before_gold"),TalentGoldBefore);E->SetNumberField(TEXT("before_points"),TalentPointsBefore);
    if(TalentTarget.IsValid()){E->SetStringField(TEXT("enemy"),TalentTarget->GetName());E->SetNumberField(TEXT("enemy_health_before"),TalentHealthBefore);E->SetNumberField(TEXT("enemy_health_after"),TalentTarget->GetHealth());E->SetNumberField(TEXT("observed_health_delta"),TalentHealthBefore-TalentTarget->GetHealth());}
    TalentEvents.Add(MakeShared<FJsonValueObject>(E));
}
void UFableRPGValidationDriver::TalentNextProbe(int32 Next){TalentProbe=Next;TalentAt=GetWorld()->GetTimeSeconds();}
bool UFableRPGValidationDriver::TalentCombatProbe()
{
    AFablePlayerState* S=PC->GetPlayerState<AFablePlayerState>();AFableCharacter* H=PC->GetPawn<AFableCharacter>();
    const double Now=GetWorld()->GetTimeSeconds(),Wait=Now-TalentAt;
    AFableCharacter* Partner=nullptr;
    for(TActorIterator<AFableCharacter> I(GetWorld());I;++I)if(*I!=H && I->GetPlayerState() && I->IsAlive()){Partner=*I;break;}
    AFableEnemy* Boss=nullptr;for(TActorIterator<AFableEnemy> I(GetWorld());I;++I)if(I->IsAlive() && I->IsBoss() && I->GetEncounterRegion()==EFableRegion::RootRuins){Boss=*I;break;}
    if(!Boss)return false;
    if(PC->IsAnyPanelOpen())PC->UIClosePanels();
    const auto Done=[&](){Hold(EKeys::W,false);Hold(EKeys::S,false);TalentProbeDoneCycle=TalentCycle;TalentChecks.Add(TalentCycle==0?TEXT("first_effect"):TEXT("second_effect"),true);TalentRecord(TEXT("combat_effect"));Capture(TalentCycle==0?TEXT("talent-first-combat"):TEXT("talent-second-combat"));return true;};
    const auto Fail=[&](const TCHAR* Why){Finish(false,Why);return false;};
    const FVector Delta=Boss->GetActorLocation()-H->GetActorLocation();
    if(!(TalentCycle==1 && S->HeroClass==EFableClass::Archer && TalentProbe<2))PC->SetControlRotation(FRotator(-12,Delta.Rotation().Yaw,0));
    if(TalentCycle==0)
    {
        const int32 Slot=S->HeroClass==EFableClass::Mage?0:1;
        if(TalentProbe==0)
        {
            if(Delta.Size2D()>500){Hold(EKeys::W,true);TalentDamageStableAt=-1;return false;}Hold(EKeys::W,false);Hold(EKeys::S,false);
            if(TalentStableBoss.Get()!=Boss)
            {TalentStableBoss=Boss;TalentDamageStableAt=-1;TalentStableBossHealth=-1;bTalentCleanDamageWindow=false;}
            // A previous caster's last burn tick can replicate after its timer reaches zero.
            // Require an empty player-projectile field and stable boss HP before attributing this cast.
            if(Boss->GetBurnTimeRemaining()>0 || TalentPlayerProjectiles(GetWorld())>0)
            {TalentDamageStableAt=-1;TalentStableBossHealth=Boss->GetHealth();return false;}
            if(TalentDamageStableAt<0 || !FMath::IsNearlyEqual(TalentStableBossHealth,Boss->GetHealth(),.01f))
            {TalentDamageStableAt=Now;TalentStableBossHealth=Boss->GetHealth();return false;}
            if(Now-TalentDamageStableAt<.75 || H->GetSkillCooldownRemaining(Slot)>0 || H->GetMana()<H->GetSkillManaCost(Slot))return false;
            bTalentCleanDamageWindow=true;
            TalentTarget=Boss;TalentHealthBefore=Boss->GetHealth();TalentBurnBefore=H->GetTalentBurnDamageDealt();TalentCastBefore=H->GetSkillCastCount(Slot);
            Key(Slot==0?EKeys::K:EKeys::L);++SkillInputs;TalentNextProbe(1);return false;
        }
        if(H->GetSkillCastCount(Slot)>TalentCastBefore && Boss->GetHealth()<TalentHealthBefore)
        {
            bool Spec=false;
            if(S->HeroClass==EFableClass::Mage)Spec=FMath::IsNearlyEqual(H->GetSkillDamageMultiplier(0),2.1f,.001f) && FMath::IsNearlyEqual(H->GetSkillRange(0),360.f,.1f) && H->GetTalentBurnDamageDealt()>TalentBurnBefore;
            else if(S->HeroClass==EFableClass::Warrior)Spec=FMath::IsNearlyEqual(H->GetSkillDamageMultiplier(1),1.885f,.001f) && FMath::IsNearlyEqual(H->GetSkillRange(1),650.f,.1f) && FMath::IsNearlyEqual(H->GetSkillCooldownDuration(1),6.f,.1f);
            else Spec=FMath::IsNearlyEqual(H->GetSkillDamageMultiplier(1),2.08f,.001f) && FMath::IsNearlyEqual(H->GetSkillRange(1),2600.f,.1f);
            if(Spec)return Done();
        }
        if(Wait>10)return Fail(TEXT("First-path real cast, damage or class effect was not observed."));
        return false;
    }
    if(S->HeroClass==EFableClass::Warrior)
    {
        if(TalentProbe==0)
        {
            Hold(EKeys::W,false);Hold(EKeys::S,false);
            if(!Partner || FVector::Dist2D(H->GetActorLocation(),Partner->GetActorLocation())>500 || H->GetSkillCooldownRemaining(2)>0 || H->GetMana()<H->GetSkillManaCost(2))return false;
            TalentAllyBefore=H->GetTalentAllyShieldsGranted();TalentCastBefore=H->GetSkillCastCount(2);Key(EKeys::O);++SkillInputs;TalentNextProbe(1);return false;
        }
        if(Wait>.2 && H->GetSkillCastCount(2)>TalentCastBefore && H->GetShieldAmount()>0 && H->GetTalentAllyShieldsGranted()>TalentAllyBefore
            && Partner && Partner->GetShieldAmount()>0 && FMath::IsNearlyEqual(H->GetSkillShieldCapacity(),85.f,.1f) && FMath::IsNearlyEqual(H->GetSkillShieldDuration(),8.f,.1f))return Done();
        if(Wait>6)return Fail(TEXT("Guardian O did not create the expected self and nearby ally shields."));
        return false;
    }
    if(S->HeroClass==EFableClass::Mage)
    {
        if(TalentProbe==0)
        {
            if(Delta.Size2D()>500){Hold(EKeys::W,true);return false;}Hold(EKeys::W,false);Hold(EKeys::S,false);
            if(H->GetSkillCooldownRemaining(1)>0 || H->GetMana()<H->GetSkillManaCost(1))return false;
            TalentSlowBefore=H->GetTalentSlowHits();TalentCastBefore=H->GetSkillCastCount(1);Key(EKeys::L);++SkillInputs;TalentNextProbe(1);return false;
        }
        if(TalentProbe==1)
        {
            if(H->GetSkillCastCount(1)>TalentCastBefore && H->GetTalentSlowHits()>TalentSlowBefore)
            {TalentChecks.Add(TEXT("frost_actual_slow"),true);TalentNextProbe(2);return false;}
            if(Wait>5)return Fail(TEXT("Frost L did not apply an actual server-accepted slow."));return false;
        }
        if(TalentProbe==2)
        {
            // L has a real .55 second action lock even after its slow/cast counter replicates.
            if(Wait<.75 || H->GetSkillCooldownRemaining(2)>0 || H->GetMana()<H->GetSkillManaCost(2))return false;
            TalentCastBefore=H->GetSkillCastCount(2);Key(EKeys::O);++SkillInputs;TalentRetryAt=Now;TalentNextProbe(3);return false;
        }
        if(Wait>.2 && H->GetSkillCastCount(2)>TalentCastBefore && H->GetShieldAmount()>0 && FMath::IsNearlyEqual(H->GetSkillShieldCapacity(),90.f,.1f) && FMath::IsNearlyEqual(H->GetSkillShieldDuration(),8.f,.1f))return Done();
        if(Wait>5)return Fail(TEXT("Frost O did not create the enhanced real shield."));
        if(H->GetSkillCastCount(2)==TalentCastBefore && Now-TalentRetryAt>=.8
            && H->GetSkillCooldownRemaining(2)<=0 && H->GetMana()>=H->GetSkillManaCost(2))
        {Key(EKeys::O);++SkillInputs;TalentRetryAt=Now;}
        return false;
    }
    // Ranger mobility is observed through normal held W, Shift and O inputs, not actor relocation.
    if(TalentProbe==0)
    {
        PC->SetControlRotation(FRotator(-12,Delta.Rotation().Yaw+90.f,0));
        if(TalentAt<=0)TalentAt=Now;
        Hold(EKeys::W,true);TalentMaximumSpeed=FMath::Max(TalentMaximumSpeed,H->GetVelocity().Size2D());
        if(Wait>.6 && TalentMaximumSpeed>435)
        {Hold(EKeys::W,false);if(H->GetDodgeCooldownRemaining()<=0){Key(EKeys::LeftShift);TalentNextProbe(1);}}return false;
    }
    if(TalentProbe==1)
    {
        if(Wait>.15 && H->GetDodgeCooldownRemaining()>0 && FMath::IsNearlyEqual(H->GetDodgeCooldownDuration(),1.05f,.01f))
        {TalentChecks.Add(TEXT("ranger_actual_dodge"),true);TalentNextProbe(2);return false;}
        if(Wait>3)return Fail(TEXT("Ranger Shift did not expose the reduced actual dodge cooldown."));return false;
    }
    if(TalentProbe==2)
    {
        Hold(EKeys::W,false);Hold(EKeys::S,false);
        if(H->GetDodgeCooldownRemaining()>0 || H->GetSkillCooldownRemaining(2)>0 || H->GetMana()<H->GetSkillManaCost(2))return false;
        TalentPositionBefore=H->GetActorLocation();TalentCastBefore=H->GetSkillCastCount(2);Key(EKeys::O);++SkillInputs;TalentNextProbe(3);return false;
    }
    if(Wait>.35 && H->GetSkillCastCount(2)>TalentCastBefore && FVector::Dist2D(H->GetActorLocation(),TalentPositionBefore)>130
        && FMath::IsNearlyEqual(H->GetMovementSpeedLimit(),462.f,.1f) && FMath::IsNearlyEqual(H->GetSkillCooldownDuration(2),8.f,.1f))return Done();
    if(Wait>5)return Fail(TEXT("Ranger O did not produce an actual retreat with its enhanced cooldown."));return false;
}
void UFableRPGValidationDriver::TickTalents()
{
    AFablePlayerState* S=PC->GetPlayerState<AFablePlayerState>();AFableCharacter* H=PC->GetPawn<AFableCharacter>();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const double Now=GetWorld()->GetTimeSeconds(),Wait=Now-StepAt;
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
    const auto Go=[&](int32 Next){Hold(EKeys::E,false);PC->UIClosePanels();Advance(Next);};
    const auto Fail=[&](const TCHAR* Why){Capture(TEXT("talent-failure"));Finish(false,Why);};
    const EFableTalentPath Path=TalentCycle==0?EFableTalentPath::First:EFableTalentPath::Second;
    if(Now-LastTalentReport>.5){Report(TEXT("running"),TEXT("Normal UI talent choices, reset and real combat validation."));LastTalentReport=Now;}
    if(Wait>240){Fail(TEXT("Talent validation step timed out."));return;}
    if(Phase==TEXT("resume"))
    {
        if(Wait<4)return;
        if(S->GetTalentRank(EFableTalentPath::Second)!=2 || S->GetTalentRank(EFableTalentPath::First)!=0 || S->GetAdvancementTier()!=3 || !H->IsAlive() || GS->ActiveRegion!=EFableRegion::Village || GS->bPartyRecoveryPending || GS->bRetryAvailable)
        {Fail(TEXT("Restored talent profile or fresh encounter runtime is invalid."));return;}
        if(Screenshots.IsEmpty()){PC->UIToggleSkills();PC->UISelectSkillPanelTab(1);Capture(TEXT("talents-restored"));StepAt=Now;return;}
        if(Wait>2){TalentChecks.Add(TEXT("runtime_not_persisted"),true);Finish(true,TEXT("All three class choices and full profile restored in a new process."));}return;
    }
    if(Step==11 && TickAdventureRecovery())return;
    if(GS->bPartyRecoveryPending){Fail(TEXT("Talent expedition party wiped."));return;}
    switch(Step)
    {
    case 0:
        if(Wait<3)return;
        TalentInitialClass=S->HeroClass;TalentInitialRoot=S->DungeonCompletions;
        TalentInitialSignature=AdventureSignature(S).Replace(TEXT("|v8:talents=0,0;0,0;0,0"),TEXT(""));
        if(S->GetTalentSignature()!=TEXT("0,0;0,0;0,0") || S->GetAdvancementTier()!=3 || S->Gold<50 || S->DungeonQuest!=EFableQuestStage::Completed)
        {Fail(TEXT("Expected untouched v7 advanced fixture did not migrate to empty choices."));return;}
        TalentChecks.Add(TEXT("legacy_migrated"),true);Go(1);break;
    case 1:
        if(!WalkTo(Board,90) || H->GetVelocity().Size2D()>10 || H->GetBuildChangeCooldownRemaining()>0)return;
        TalentGoldBefore=S->Gold;TalentPointsBefore=S->GetAvailableSkillPoints();
        PC->UIToggleSkills();PC->UISelectSkillPanelTab(1);Capture(TEXT("talents-unselected"));Advance(2);break;
    case 2:
        if(Wait<.8)return;
        if(S->GetTalentChoice(2)==Path){Advance(3);break;}
        if(Now-LastAction>1){PC->UISelectTalent(2,Path);PC->UIChooseTalent();LastAction=Now;}break;
    case 3:
        if(Wait<.8)return;
        if(S->GetTalentChoice(3)==Path)
        {
            if(S->Gold!=TalentGoldBefore || S->GetAvailableSkillPoints()!=TalentPointsBefore){Fail(TEXT("Free talent selection changed gold or skill points."));return;}
            TalentChecks.Add(TalentCycle==0?TEXT("first_selected"):TEXT("second_selected"),true);TalentRevisionBefore=S->ClassBuildRevision;
            PC->UISelectTalent(3,Path);PC->UIChooseTalent();Advance(4);break;
        }
        if(Now-LastAction>1){PC->UISelectTalent(3,Path);PC->UIChooseTalent();LastAction=Now;}break;
    case 4:
        if(Wait<1)return;
        if(S->GetTalentRank(Path)!=2 || S->ClassBuildRevision!=TalentRevisionBefore || S->Gold!=TalentGoldBefore){Fail(TEXT("Duplicate UI choice changed the selected build."));return;}
        TalentChecks.Add(TEXT("duplicate_selection_preserved"),true);TalentRecord(TEXT("selected"));Capture(TalentCycle==0?TEXT("talents-first-selected"):TEXT("talents-second-selected"));Advance(6);break;
    case 6:if(Wait>.8)Go(7);break;
    case 7:
        if(S->DungeonQuest==EFableQuestStage::Active){Go(8);break;}AdventureQuest(EFableQuestId::RootRuins);break;
    case 8:
    {
        bool Ready=true;for(APlayerState* P:GS->PlayerArray){const AFablePlayerState* Other=Cast<AFablePlayerState>(P);Ready&=Other && Other->IsRegionEligible(EFableRegion::RootRuins) && Other->GetTalentRank(Path)==2;}
        if(!Ready)return;
        if(GS->ActiveRegion==EFableRegion::RootRuins && FableRPG::IsInRegion(EFableRegion::RootRuins,H->GetActorLocation())){TalentProbe=0;TalentAt=Now;TalentStableBoss.Reset();TalentDamageStableAt=-1;TalentStableBossHealth=-1;bTalentCleanDamageWindow=false;Go(11);break;}
        AdventurePortal(EFableRegion::RootRuins);break;
    }
    case 11:
    {
        // Valid reports for this run/phase carry a monotonic completed cycle. A transient
        // open/parse failure cannot revoke a real observation or interrupt its rendezvous.
        const int32 ObservedPeerCycle=TalentPeerInt(TEXT("talent_probe_done_cycle"));
        if(ObservedPeerCycle>=0 && ObservedPeerCycle<=1)
            TalentPeerDoneCycleSeen=FMath::Max(TalentPeerDoneCycleSeen,ObservedPeerCycle);
        if(!FableRPG::IsInRegion(EFableRegion::RootRuins,H->GetActorLocation())){Fail(TEXT("Individual rescue left the talent test region."));return;}
        if(GS->AdventureStage==EFableAdventureStage::Cleared)
        {
            if(TalentProbeDoneCycle!=TalentCycle){Fail(TEXT("The boss died before the local real talent effect was observed."));return;}
            // The peer can finish its probe and kill the boss before its next report is read.
            // Wait for that actual completion report; the unchanged step timeout still fails a missing observation.
            if(TalentPeerDoneCycleSeen<TalentCycle)return;
            Go(12);break;
        }
        if(GS->AdventureStage!=EFableAdventureStage::Boss){AdventureFight();break;}
        if(TalentProbeDoneCycle==TalentCycle)
        {if(TalentPeerDoneCycleSeen>=TalentCycle)AdventureFight();else{Hold(EKeys::W,false);Hold(EKeys::S,false);}break;}
        if(Role==TEXT("guest") && TalentPeerDoneCycleSeen<TalentCycle)
        {
            Hold(EKeys::S,false);
            AFableCharacter* Partner=nullptr;for(TActorIterator<AFableCharacter> I(GetWorld());I;++I)if(*I!=H && I->GetPlayerState()){Partner=*I;break;}
            if(TalentCycle==1 && Partner && Partner->GetHeroClass()==EFableClass::Warrior)WalkTo(Partner->GetActorLocation()+FVector(180,0,0),90);else Hold(EKeys::W,false);
            break;
        }
        TalentCombatProbe();break;
    }
    case 12:
        if(FableRPG::IsInRegion(EFableRegion::Village,H->GetActorLocation())){Go(13);break;}
        if(WalkTo(FableRPG::RegionReturnPortal(EFableRegion::RootRuins)+FVector(Role==TEXT("host")?-80:80,0,100),100) && Now-LastAction>1){Key(EKeys::E);LastAction=Now;}break;
    case 13:
        if(S->DungeonQuest==EFableQuestStage::Completed)
        {if(S->DungeonCompletions!=TalentInitialRoot+TalentCycle+1){Fail(TEXT("An expedition completion was lost or awarded twice."));return;}Go(TalentCycle==0?20:30);break;}
        AdventureQuest(EFableQuestId::RootRuins);break;
    case 20:
        if(!WalkTo(Board,90) || H->GetVelocity().Size2D()>10 || H->GetBuildChangeCooldownRemaining()>0)return;
        TalentBeforeReset=TalentProtectedProfile(S);TalentGoldBefore=S->Gold;TalentPointsBefore=S->GetAvailableSkillPoints();PointsBefore=S->GetCurrentClassRefundableSkillPoints();TalentRevisionBefore=S->ClassBuildRevision;
        PC->UIToggleSkills();PC->UISelectSkillPanelTab(1);PC->UIRequestBuildReset();Advance(21);break;
    case 21:
        if(Wait<.8)return;
        if(!PC->IsBuildResetConfirmationPending()){Fail(TEXT("Reset confirmation panel was not opened."));return;}
        TalentChecks.Add(TEXT("reset_confirmation"),true);Capture(TEXT("talents-reset-confirmation"));Advance(22);break;
    case 22:
        if(Wait<.8)return;
        PC->UICancelBuildReset();
        if(S->Gold!=TalentGoldBefore || S->ClassBuildRevision!=TalentRevisionBefore || TalentProtectedProfile(S)!=TalentBeforeReset){Fail(TEXT("Canceling reset altered the profile."));return;}
        TalentChecks.Add(TEXT("cancel_preserved"),true);PC->UIRequestBuildReset();Advance(23);break;
    case 23:
        if(Wait<.8)return;
        if(!PC->IsBuildResetConfirmationPending()){Fail(TEXT("The second reset confirmation did not open."));return;}
        PC->UIConfirmBuildReset();Advance(24);break;
    case 24:
        if(Wait<1)return;
        if(S->ClassBuildRevision==TalentRevisionBefore){if(Wait>6)Fail(TEXT("Confirmed reset was not acknowledged."));return;}
        if(S->Gold!=TalentGoldBefore-50 || S->GetAvailableSkillPoints()!=TalentPointsBefore+PointsBefore || S->GetTalentRank(EFableTalentPath::First)!=0 || S->GetTalentRank(EFableTalentPath::Second)!=0 || TalentProtectedProfile(S)!=TalentBeforeReset)
        {Fail(TEXT("Reset cost/refund or protected profile data was incorrect."));return;}
        TalentChecks.Add(TEXT("reset_cost_refund"),true);TalentChecks.Add(TEXT("other_classes_preserved"),true);TalentRecord(TEXT("reset"));
        PC->UISelectSkillPanelTab(0);PC->UIUpgradeSkill(3);Advance(25);break;
    case 25:
        if(Wait<1)return;
        if(S->GetSkillRank(3)!=1){if(Now-LastAction>1){PC->UIUpgradeSkill(3);LastAction=Now;}return;}
        TalentChecks.Add(TEXT("skill_reinvested"),true);TalentCycle=1;TalentProbe=0;TalentAt=Now;TalentGoldBefore=S->Gold;TalentPointsBefore=S->GetAvailableSkillPoints();
        PC->UISelectSkillPanelTab(1);Advance(2);break;
    case 30:
        if(!WalkTo(Board,90) || H->GetVelocity().Size2D()>10 || H->GetBuildChangeCooldownRemaining()>0 || TalentLiveEnemies(GetWorld())>0 || TalentProjectiles(GetWorld())>0)
        {TalentQuietAt=-1;return;}
        if(!H->IsAlive() || GS->bPartyRecoveryPending || H->GetReviveTarget() || H->GetReviver()){TalentQuietAt=-1;return;}
        // This is a monotonic rendezvous, not a live gameplay condition. A .5-second
        // peer report rewrite can temporarily fail to open/parse; it cannot undo step 30.
        if(!TalentChecks.FindRef(TEXT("peer_final_ready_seen")) && TalentPeerInt(TEXT("step"))>=30)
            TalentChecks.Add(TEXT("peer_final_ready_seen"),true);
        if(!TalentChecks.FindRef(TEXT("peer_final_ready_seen"))){TalentQuietAt=-1;return;}
        if(TalentQuietAt<0)TalentQuietAt=Now;
        if(Now-TalentQuietAt<6)return;
        for(APlayerState* P:GS->PlayerArray){const AFablePlayerState* Other=Cast<AFablePlayerState>(P);if(!Other || Other->GetTalentRank(EFableTalentPath::Second)!=2 || Other->GetTalentRank(EFableTalentPath::First)!=0){Fail(TEXT("Peer selected build did not replicate."));return;}}
        TalentChecks.Add(TEXT("peer_replicated"),true);TalentChecks.Add(TEXT("final_safe_checkpoint"),true);
        PC->UIToggleSkills();PC->UISelectSkillPanelTab(1);Capture(TEXT("talents-final-saved"));Advance(31);break;
    case 31:if(Wait>1)Finish(true,TEXT("Two explicit talent builds, normal UI reset, actual class effects and final safe profile checked."));break;
    default:Fail(TEXT("Unknown talent validation step."));break;
    }
}
void UFableRPGValidationDriver::AppendTalentReport(TSharedRef<FJsonObject> O) const
{
    const AFablePlayerState* S=PC.IsValid()?PC->GetPlayerState<AFablePlayerState>():nullptr;if(!S)return;
    O->SetBoolField(TEXT("talents_enabled"),true);O->SetNumberField(TEXT("talent_cycle"),TalentCycle);O->SetNumberField(TEXT("talent_probe"),TalentProbe);O->SetNumberField(TEXT("talent_probe_done_cycle"),TalentProbeDoneCycle);O->SetNumberField(TEXT("talent_peer_done_cycle_seen"),TalentPeerDoneCycleSeen);
    O->SetStringField(TEXT("talent_signature"),AdventureSignature(S));O->SetStringField(TEXT("initial_signature"),TalentInitialSignature);
    O->SetNumberField(TEXT("initial_root_completions"),TalentInitialRoot);O->SetStringField(TEXT("talent_choices"),S->GetTalentSignature());
    O->SetStringField(TEXT("profile_slot"),S->GetProfileSlotForValidation());O->SetNumberField(TEXT("class_build_revision"),S->ClassBuildRevision);
    int32 Schema=0;if(S->HasAuthority() && !S->GetProfileSlotForValidation().IsEmpty())if(const UFableSaveGame* D=Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromSlot(S->GetProfileSlotForValidation(),0)))Schema=D->Version;
    O->SetNumberField(TEXT("saved_schema_version"),Schema);O->SetNumberField(TEXT("live_enemy_count"),TalentLiveEnemies(GetWorld()));O->SetNumberField(TEXT("projectile_count"),TalentProjectiles(GetWorld()));
    O->SetNumberField(TEXT("quiet_seconds"),TalentQuietAt<0?0:GetWorld()->GetTimeSeconds()-TalentQuietAt);
    TSharedRef<FJsonObject> Checks=MakeShared<FJsonObject>();for(const auto& P:TalentChecks)Checks->SetBoolField(P.Key,P.Value);O->SetObjectField(TEXT("talent_checks"),Checks);O->SetArrayField(TEXT("talent_events"),TalentEvents);
    TArray<TSharedPtr<FJsonValue>> Gear,Stash;for(const FFableItem& I:S->Inventory)Gear.Add(MakeShared<FJsonValueString>(FableRPG::ItemPersistentSignature(I)));for(const FFableItem& I:S->StashItems)Stash.Add(MakeShared<FJsonValueString>(FableRPG::ItemPersistentSignature(I)));
    O->SetArrayField(TEXT("inventory_signatures"),Gear);O->SetArrayField(TEXT("stash_signatures"),Stash);
    if(const AFableCharacter* H=PC->GetPawn<AFableCharacter>())
    {O->SetNumberField(TEXT("life_state"),int32(H->GetLifeState()));O->SetNumberField(TEXT("health"),H->GetHealth());O->SetNumberField(TEXT("burn_damage"),H->GetTalentBurnDamageDealt());O->SetNumberField(TEXT("slow_hits"),H->GetTalentSlowHits());O->SetNumberField(TEXT("ally_shields"),H->GetTalentAllyShieldsGranted());}
}
