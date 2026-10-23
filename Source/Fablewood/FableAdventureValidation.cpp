#include "FableRPGValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"

FString UFableRPGValidationDriver::AdventureSignature(const AFablePlayerState* PS,bool bIgnoreWear) const
{
    if(!PS) return TEXT("");
    FString Value=Signature(PS,bIgnoreWear)+FString::Printf(TEXT("|v4:moon=%d,%d"),uint8(PS->MoonwellQuest),PS->MoonwellCompletions);
    for(const FFableClassProgress& P:PS->ClassProgress)
        Value+=FString::Printf(TEXT("|trials=%d,%d,%d;%d,%d,%d"),uint8(P.SecondTrial.Stage),P.SecondTrial.ValidSkillCasts,P.SecondTrial.bLegacyGranted,
            uint8(P.ThirdTrial.Stage),P.ThirdTrial.ValidSkillCasts,P.ThirdTrial.bLegacyGranted);
    return Value;
}
void UFableRPGValidationDriver::AdventureQuest(EFableQuestId Quest)
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    const double Now=GetWorld()->GetTimeSeconds();
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
    if(FVector::DistSquared2D(Hero->GetActorLocation(),Board)>FMath::Square(90.f) && PC->IsAnyPanelOpen()) PC->UIClosePanels();
    if(!WalkTo(Board,90) || Hero->GetVelocity().SizeSquared2D()>100) { LastAction=Now;return; }
    if(Now-LastAction<1) return;
    if(PC->GetDialogue()==EFableDialogue::Guide) { PC->UISelectQuest(Quest);PC->UIConfirmQuest(); }
    else { PC->UIClosePanels();Key(EKeys::E); }
    LastAction=Now;
}
void UFableRPGValidationDriver::AdventurePortal(EFableRegion Region)
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    const double Now=GetWorld()->GetTimeSeconds();
    const FVector Portal=FableRPG::PortalLocation()+FVector(Role==TEXT("host")?-80:80,-100,100);
    if(FVector::DistSquared2D(Hero->GetActorLocation(),Portal)>FMath::Square(100.f) && PC->IsAnyPanelOpen()) PC->UIClosePanels();
    if(!WalkTo(Portal,100) || Hero->GetVelocity().SizeSquared2D()>100) {LastAction=Now;return;}
    if(Now-LastAction<1) return;
    if(PC->IsPortalOpen())
    {
        PC->UISelectDungeon(Region);
        if(Region==EFableRegion::Moonwell && !bMoonPortalCaptured)
        {bMoonPortalCaptured=true;Capture(TEXT("moonwell-portal-selection"));LastAction=Now;return;}
        PC->UIEnterSelectedDungeon();
    }
    else { PC->UIClosePanels();Key(EKeys::E); }
    LastAction=Now;
}
bool UFableRPGValidationDriver::TickAdventureRecovery()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const double Now=GetWorld()->GetTimeSeconds();
    const auto StopReviving=[&]()
    {
        Hold(EKeys::E,false);
        PC->UIStopRevive();
        AdventureRevivePressedAt=-1;
        AdventureReviveStableAt=-1;
    };
    if(!Hero->IsAlive() || GS->bPartyRecoveryPending)
    {
        Hold(EKeys::W,false);Hold(EKeys::S,false);StopReviving();
        return true;
    }
    const bool bCombatStep=Step==5 || Step==11 || Step==21 || Step==22;
    if(!bCombatStep) {StopReviving();return false;}
    const bool bExpeditionRunning=GS->ActiveRegion!=EFableRegion::Village
        && (GS->AdventureStage==EFableAdventureStage::Dungeon || GS->AdventureStage==EFableAdventureStage::Boss);
    if(bExpeditionRunning && FableRPG::IsInRegion(EFableRegion::Village,Hero->GetActorLocation()))
    {
        // An individually rescued player must use the same portal as a human rejoining the expedition.
        StopReviving();AdventurePortal(GS->ActiveRegion);
        return true;
    }
    AFableCharacter* Fallen=nullptr;
    for(APlayerState* Member:GS->PlayerArray)
    {
        AFableCharacter* Candidate=Member?Cast<AFableCharacter>(Member->GetPawn()):nullptr;
        if(Candidate && Candidate!=Hero && Candidate->IsDowned()
            && (!Candidate->GetReviver() || Candidate->GetReviver()==Hero)
            && FableRPG::IsInRegion(GS->ActiveRegion,Candidate->GetActorLocation())
            && FableRPG::IsInRegion(GS->ActiveRegion,Hero->GetActorLocation()))
        {Fallen=Candidate;break;}
    }
    if(!Fallen) {StopReviving();return false;}
    if(Hero->GetReviveTarget()==Fallen)
    {
        // Do not let later seal interaction, movement or attacks cancel a real server-approved channel.
        Hold(EKeys::W,false);Hold(EKeys::S,false);Hold(EKeys::E,true);
        return true;
    }
    if(AdventureRevivePressedAt>=0)
    {
        Hold(EKeys::W,false);Hold(EKeys::S,false);
        if(Now-AdventureRevivePressedAt<0.6) return true; // Allow the replicated channel to arrive.
        StopReviving();AdventureReviveRetryAt=Now+0.4;
        return true; // A canceled or rejected hold needs a release before a fresh press.
    }
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
        if(It->IsAlive() && FableRPG::IsInRegion(GS->ActiveRegion,It->GetActorLocation()))
        {StopReviving();return false;}
    // Clear the current wave first, then revive before either player can open the next seal.
    if(PC->IsAnyPanelOpen()) PC->UIClosePanels();
    if(!WalkTo(Fallen->GetActorLocation(),190) || Hero->GetVelocity().SizeSquared2D()>100)
    {AdventureReviveStableAt=-1;return true;}
    if(AdventureReviveStableAt<0) AdventureReviveStableAt=Now;
    if(Now<AdventureReviveRetryAt || Now-AdventureReviveStableAt<0.35) return true;
    if(PC->GetNearbyReviveTarget()!=Fallen) return true;
    Hold(EKeys::E,true);AdventureRevivePressedAt=Now;
    UE_LOG(LogTemp,Display,TEXT("FABLE_ADVENTURE_RECOVERY role=%s action=revive_hold"),*Role);
    return true;
}
void UFableRPGValidationDriver::AdventureFight()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();
    AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const double Now=GetWorld()->GetTimeSeconds();
    if(PC->IsAnyPanelOpen()) PC->UIClosePanels();
    if(!Hero->IsAlive()) {Hold(EKeys::W,false);Hold(EKeys::S,false);return;}
    AFableEnemy* Target=nullptr;float Distance=MAX_flt;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
    {
        if(!It->IsAlive() || !FableRPG::IsInRegion(GS->ActiveRegion,It->GetActorLocation()))continue;
        const float D=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation());
        if(D<Distance){Distance=D;Target=*It;}
    }
    if(!Target){Hold(EKeys::W,false);Hold(EKeys::S,false);return;}
    PC->SetControlRotation(FRotator(-12,(Target->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));
    if(Hero->GetHealth()<Hero->GetMaxHealth()*0.48f && PS->HealthPotions>0 && PS->GetPotionCooldownRemaining()<=0 && Now-LastAction>1)
    {Key(EKeys::H);LastAction=Now;}
    const int32 TrialTier=PS->GetAdvancementTier()==1?2:3;
    bool bPartnerNeedsTrial=false;
    for(APlayerState* P:GS->PlayerArray)
    {
        const AFablePlayerState* Other=Cast<AFablePlayerState>(P);
        const AFableCharacter* OtherHero=Other?Cast<AFableCharacter>(Other->GetPawn()):nullptr;
        if(Other && Other!=PS && OtherHero && OtherHero->IsAlive()
            && FableRPG::IsInRegion(GS->ActiveRegion,OtherHero->GetActorLocation())
            && Other->GetAdvancementTier()<3 && Other->GetAdvancementTrialProgress(Other->GetAdvancementTier()+1)<3)
            bPartnerNeedsTrial=true;
    }
    const bool bOwnNeedsTrial=PS->GetAdvancementTier()<3 && PS->GetAdvancementTrialProgress(TrialTier)<3;
    const int32 TrialSlot=TrialTier-2;
    const bool bWarrior=PS->HeroClass==EFableClass::Warrior;
    const float Preferred=bWarrior?145.f:440.f;
    Hold(EKeys::W,Distance>Preferred+50);Hold(EKeys::S,!bWarrior && Distance<Preferred-100);
    // Preserve living enemies for the partner's real skill hits, instead of fabricating progress.
    if(GS->ActiveRegion==EFableRegion::Moonwell && Target->IsBoss() && Target->GetEnemyState()==EFableEnemyState::Windup)
    {
        const EFableEnemyAttack Pattern=Target->GetAttackPattern();
        if(Pattern==EFableEnemyAttack::MoonRing || Pattern==EFableEnemyAttack::Moonfall)
        {
            if(DodgeWindup!=Target->GetWindupEndServerTime())
            {
                DodgeWindup=Target->GetWindupEndServerTime();
                if(Pattern==EFableEnemyAttack::MoonRing)
                    DodgeDestination=Target->GetActorLocation()+(Hero->GetActorLocation()-Target->GetActorLocation()).GetSafeNormal2D()*150;
                else
                {
                    FVector Side=FVector::CrossProduct((Target->GetActorLocation()-Hero->GetActorLocation()).GetSafeNormal2D(),FVector::UpVector);
                    DodgeDestination=Hero->GetActorLocation()+Side*(Role==TEXT("host")?650:-650);
                }
                DodgeDestination.Z=Hero->GetActorLocation().Z;
            }
            Hold(EKeys::S,false);WalkTo(DodgeDestination,60);return;
        }
    }
    if(bPartnerNeedsTrial && !bOwnNeedsTrial) return;
    if(GS->ActiveRegion==EFableRegion::Moonwell && Target->IsBoss() && (!bMoonRipple || !bMoonRain))
    {Hold(EKeys::W,Distance>650);Hold(EKeys::S,Distance<450);return;}
    if(Now-LastAdvancedInput<0.75) return;
    if(bOwnNeedsTrial)
    {
        if(Distance<(bWarrior?260:570) && Hero->GetSkillCooldownRemaining(TrialSlot)<=0 && Hero->GetMana()>=Hero->GetSkillManaCost(TrialSlot))
        {Key(TrialSlot==0?EKeys::K:EKeys::L);LastAdvancedInput=Now;++SkillInputs;}
        return;
    }
    if(PS->IsSkillUnlocked(1) && Distance<570 && Hero->GetSkillCooldownRemaining(1)<=0 && Hero->GetMana()>=Hero->GetSkillManaCost(1))
    {Key(EKeys::L);LastAdvancedInput=Now;++SkillInputs;return;}
    if(Distance<(bWarrior?260:900) && Hero->GetSkillCooldownRemaining(0)<=0 && Hero->GetMana()>=Hero->GetSkillManaCost(0))
    {Key(EKeys::K);LastAdvancedInput=Now;++SkillInputs;return;}
    if(Distance<(bWarrior?240:1100)){Key(EKeys::J);LastAdvancedInput=Now;++AttackInputs;}
}
void UFableRPGValidationDriver::TickAdventure()
{
    AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const double Now=GetWorld()->GetTimeSeconds(), Wait=Now-StepAt;
    const EFableClass Wanted=Role==TEXT("host")?(bAdventureWarrior?EFableClass::Warrior:EFableClass::Mage):EFableClass::Archer;
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
    const auto Go=[&](int32 Next){Hold(EKeys::E,false);PC->UIClosePanels();Advance(Next);};
    const auto PartyReady=[&](EFableRegion Region){
        for(APlayerState* P:GS->PlayerArray){const AFablePlayerState* Other=Cast<AFablePlayerState>(P);if(!Other || !Other->IsRegionEligible(Region))return false;}return true;};
    const auto Return=[&](){
        if(GS->ActiveRegion==EFableRegion::Village || FableRPG::IsInRegion(EFableRegion::Village,Hero->GetActorLocation())) return true;
        if(PC->IsAnyPanelOpen()) PC->UIClosePanels();
        if(WalkTo(FableRPG::RegionReturnPortal(GS->ActiveRegion)+FVector(Role==TEXT("host")?-80:80,0,100),100) && Now-LastAction>1)
        {Key(EKeys::E);LastAction=Now;}return false;};
    if(Now-LastAdventureReport>5){Report(TEXT("running"),TEXT("Normal-input adventure validation in progress."));LastAdventureReport=Now;}
    if(Wait>240){Hold(EKeys::E,false);PC->UIStopRevive();Capture(TEXT("adventure-stuck"));Finish(false,FString::Printf(TEXT("Adventure step %d timed out."),Step));return;}
    if(Phase==TEXT("resume"))
    {
        if(Wait<4)return;
        const FFableItem* Item=PS->FindItem(PS->EquippedWeaponId);
        if(PS->HeroClass!=Wanted || PS->GetAdvancementTier()!=3 || PS->MoonwellCompletions!=1 || PS->MoonwellQuest!=EFableQuestStage::Completed
            || PS->DungeonCompletions!=1 || !Item || Item->Rarity!=3 || Item->Upgrade!=1 || !PS->bLastSaveSucceeded
            || PS->GetAdvancementTrialProgress(2)!=3 || PS->GetAdvancementTrialProgress(3)!=3 || PS->IsAdvancementLegacyGranted(2) || PS->IsAdvancementLegacyGranted(3))
        {Finish(false,TEXT("Adventure reload lost trials, unique Moonwell loot or progression."));return;}
        if(Screenshots.IsEmpty()){Key(EKeys::P);Capture(TEXT("restored-adventure"));StepAt=Now;}
        else Finish(true,TEXT("Adventure profile restored exactly in new game processes."));
        return;
    }
    if(GS->ActiveRegion==EFableRegion::Moonwell)
    {
        ObservedSealMask|=GS->MoonSealMask;
        for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
        {
            if(!FableRPG::IsInRegion(EFableRegion::Moonwell,It->GetActorLocation()))continue;
            bMoonRanged|=It->IsRanged();
            if(It->IsBoss() && It->GetEnemyState()==EFableEnemyState::Windup)
            {
                const EFableEnemyAttack Pattern=It->GetAttackPattern();
                if(Pattern==EFableEnemyAttack::MoonRing && !bMoonRipple){bMoonRipple=true;Capture(TEXT("moon-ripple-warning"));}
                if(Pattern==EFableEnemyAttack::Moonfall && !bMoonRain){bMoonRain=true;Capture(TEXT("moon-rain-warning"));}
            }
        }
    }
    if(TickAdventureRecovery()) return;
    switch(Step)
    {
    case 0:
        if(WalkTo(Board,90) && Hero->GetVelocity().Size2D()<10 && Wait>1)
        {PC->UIChooseClass(Wanted);Go(1);}break;
    case 1:
        if(PS->HeroClass==Wanted && Wait>1)AdventureQuest(EFableQuestId::SecondAdvancement);
        if(PS->GetQuestStage(EFableQuestId::SecondAdvancement)==EFableQuestStage::Active)
        {bTrialStarted=true;Go(2);}break;
    case 2:
        if(Wait>1 && LastAction<0){PC->UIAdvanceClass();LastAction=Now;}
        if(Wait>2){bPrematureTrialRejected=PS->GetAdvancementTier()==1 && PS->GetAdvancementTrialProgress(2)==0;
            if(!bPrematureTrialRejected){Finish(false,TEXT("Trial advanced without combat objectives."));return;}Go(3);}break;
    case 3:
        if(Wait>1)AdventureQuest(EFableQuestId::Moonwell);
        if(Wait>4){bMoonLocked=PS->MoonwellQuest==EFableQuestStage::Available && PS->DungeonCompletions==0;
            if(!bMoonLocked){Finish(false,TEXT("Moonwell unlocked before root reward."));return;}Go(4);}break;
    case 4:
        if(Wait>0.5)AdventureQuest(EFableQuestId::Forest);
        if(PS->ForestQuest==EFableQuestStage::Active)Go(5);break;
    case 5:
        {bool bAll=true;for(APlayerState* P:GS->PlayerArray){const AFablePlayerState* Other=Cast<AFablePlayerState>(P);bAll &= Other && Other->ForestQuest!=EFableQuestStage::Available;}if(bAll)AdventureFight();}
        if(PS->ForestQuest==EFableQuestStage::Ready)Go(6);break;
    case 6:
        AdventureQuest(EFableQuestId::Forest);
        if(PS->ForestQuest==EFableQuestStage::Completed)Go(7);break;
    case 7:
        if(Wait>0.7 && LastAction<0)
        {for(const FFableItem& Item:PS->Inventory)if(Item.Rarity==1 && Item.Slot==EFableEquipmentSlot::Weapon && Item.HeroClass==Wanted){RareId=Item.Id;PC->UIEquip(RareId);}LastAction=Now;}
        if(Wait>2 && RareId>0 && PS->EquippedWeaponId==RareId)Go(8);break;
    case 8:
        AdventureQuest(EFableQuestId::RootRuins);
        if(PS->DungeonQuest==EFableQuestStage::Active)Go(9);break;
    case 9:if(PartyReady(EFableRegion::RootRuins))Go(10);break;
    case 10:
        if(GS->ActiveRegion==EFableRegion::RootRuins){Go(11);break;}AdventurePortal(EFableRegion::RootRuins);break;
    case 11:
        AdventureFight();
        bSawWaveTwo|=GS->DungeonWave>=2;bSawBoss|=GS->AdventureStage==EFableAdventureStage::Boss;
        if(GS->ActiveRegion==EFableRegion::Village){Finish(false,TEXT("Party wiped in root ruins."));return;}
        if(GS->AdventureStage==EFableAdventureStage::Cleared && PS->DungeonQuest==EFableQuestStage::Ready){Capture(TEXT("root-cleared"));Go(12);}break;
    case 12:if(Return())Go(13);break;
    case 13:
        AdventureQuest(EFableQuestId::RootRuins);
        if(PS->DungeonQuest==EFableQuestStage::Completed && PS->DungeonCompletions==1)Go(14);break;
    case 14:
        AdventureQuest(EFableQuestId::SecondAdvancement);
        if(PS->GetAdvancementTier()==2){bSecondAdvancement=true;Go(15);}break;
    case 15:
        AdventureQuest(EFableQuestId::ThirdAdvancement);
        if(PS->GetQuestStage(EFableQuestId::ThirdAdvancement)==EFableQuestStage::Active)Go(16);break;
    case 16:
        AdventureQuest(EFableQuestId::Moonwell);
        if(PS->MoonwellQuest==EFableQuestStage::Active)Go(17);break;
    case 17:
        if(Wait>0.7 && LastAction<0)
        {for(const FFableItem& Item:PS->Inventory)if(Item.Rarity==2 && Item.Slot==EFableEquipmentSlot::Weapon && Item.HeroClass==Wanted){EpicId=Item.Id;PC->UIEquip(EpicId);}LastAction=Now;}
        if(Wait>2 && EpicId>0 && PS->EquippedWeaponId==EpicId)Go(18);break;
    case 18:
        if(Wait>0.7 && LastAction<0)
        {for(const FFableItem& Item:PS->Inventory)if(Item.Rarity==2 && Item.Slot==EFableEquipmentSlot::Armor){RareId=Item.Id;PC->UIEquip(RareId);}LastAction=Now;}
        if(Wait>2 && RareId>0 && PS->EquippedArmorId==RareId && FableRPG::IsValidItemRoll(*PS->FindItem(RareId)) && PS->GetDefense()>=PS->FindItem(RareId)->GetEffectivePower())Go(19);break;
    case 19:
        if(Wait>0.5 && LastAction<0){Key(EKeys::Q);PC->UISelectQuest(EFableQuestId::ThirdAdvancement);Capture(TEXT("active-third-trial"));LastAction=Now;}
        if(Wait>2 && PartyReady(EFableRegion::Moonwell))Go(20);break;
    case 20:
        if(GS->ActiveRegion==EFableRegion::Moonwell){Go(21);break;}
        AdventurePortal(EFableRegion::Moonwell);break;
    case 21:
        if(WalkTo(FableRPG::MoonSealLocation(0)+FVector(0,-100,100),100) && Now-LastAction>1){Key(EKeys::E);LastAction=Now;bSealAttempted=true;}
        if(bSealAttempted && PS->LastNotice==TEXT("봉인을 지키는 적을 먼저 처치하세요."))
        {bSealLocked=true;Capture(TEXT("moonwell-arrival"));Go(22);}break;
    case 22:
        AdventureFight();
        if(GS->ActiveRegion==EFableRegion::Village){Finish(false,TEXT("Party wiped in Moonwell."));return;}
        if(GS->AdventureStage==EFableAdventureStage::Dungeon && GS->RequiredEnemies>0 && GS->DefeatedEnemies>=GS->RequiredEnemies)
        {
            const int32 Seal=GS->ActiveSealIndex;
            if(Seal>=0 && Seal<3 && WalkTo(FableRPG::MoonSealLocation(Seal)+FVector(Role==TEXT("host")?-80:80,-100,100),110) && Now-LastAction>1)
            {Key(EKeys::E);LastAction=Now;}
        }
        if(GS->AdventureStage==EFableAdventureStage::Cleared && PS->MoonwellQuest==EFableQuestStage::Ready)
        {bMoonCleared=true;bNoRootCrossCredit=PS->DungeonQuest==EFableQuestStage::Completed && PS->DungeonCompletions==1;Capture(TEXT("moon-boss-defeated"));Go(23);}break;
    case 23:if(Return())Go(24);break;
    case 24:
        AdventureQuest(EFableQuestId::Moonwell);
        if(PS->MoonwellQuest==EFableQuestStage::Completed && PS->MoonwellCompletions==1)Go(25);break;
    case 25:
        AdventureQuest(EFableQuestId::ThirdAdvancement);
        if(PS->GetAdvancementTier()==3){bThirdAdvancement=true;Go(26);}break;
    case 26:
        if(Wait>0.6 && LastAction<0){WantedProgress=AdventureSignature(PS);PC->UIAdvanceClass();LastAction=Now;}
        if(Wait>2){bTrialDuplicateRejected=WantedProgress==AdventureSignature(PS);if(!bTrialDuplicateRejected){Finish(false,TEXT("Repeated trial report mutated rewards."));return;}Go(27);}break;
    case 27:
        if(Wait>0.7 && LastAction<0){PC->UIChooseClass(Wanted==EFableClass::Warrior?EFableClass::Mage:EFableClass::Warrior);LastAction=Now;}
        if(Wait>2 && PS->HeroClass!=Wanted)Go(28);break;
    case 28:
        if(Wait>0.7 && LastAction<0){PC->UIChooseClass(Wanted);LastAction=Now;}
        if(Wait>2 && PS->HeroClass==Wanted)
        {bTrialSwitchPreserved=PS->GetAdvancementTier()==3 && PS->GetAdvancementTrialProgress(2)==3 && PS->GetAdvancementTrialProgress(3)==3;
            if(!bTrialSwitchPreserved){Finish(false,TEXT("Switching classes lost completed trials."));return;}Go(29);}break;
    case 29:
        if(Wait>0.7 && LastAction<0)
        {for(const FFableItem& Item:PS->Inventory)if(Item.Rarity==3 && Item.Slot==EFableEquipmentSlot::Weapon && Item.HeroClass==Wanted){EpicId=Item.Id;PC->UIEquip(EpicId);}LastAction=Now;}
        if(Wait>2 && PS->EquippedWeaponId==EpicId)Go(30);break;
    case 30:
        if(WalkTo(FableRPG::ForgeLocation()+FVector(Role==TEXT("host")?-80:80,-100,100),90) && Hero->GetVelocity().Size2D()<10)Go(31);break;
    case 31:
        if(Wait>0.7 && LastAction<0){PC->UIEnhance(EpicId);LastAction=Now;}
        if(Wait>2){const FFableItem* Item=PS->FindItem(EpicId);if(!Item || Item->Rarity!=3 || Item->Upgrade!=1){Finish(false,TEXT("Moonwell weapon could not be equipped and enhanced."));return;}Go(32);}break;
    case 32:
        if(WalkTo(Board,90) && Hero->GetVelocity().Size2D()<10)Go(33);break;
    case 33:
        if(Wait>0.7 && LastAction<0){PC->UIUpgradeSkill(1);LastAction=Now;}
        if(Wait>2 && PS->GetSkillRank(1)==2){Key(EKeys::P);Capture(TEXT("third-advancement"));Advance(34);}break;
    case 34:
        if(Wait>2){PC->UIClosePanels();Key(EKeys::Q);PC->UISelectQuest(EFableQuestId::ThirdAdvancement);Capture(TEXT("quest-journal-complete"));Advance(35);}break;
    case 35:
        if(Wait>3)
        {
            const bool bOK=bTrialStarted&&bPrematureTrialRejected&&bMoonLocked&&bSealLocked&&bSecondAdvancement&&bThirdAdvancement
                &&bMoonRipple&&bMoonRain&&bMoonRanged&&bMoonCleared&&ObservedSealMask==7&&bNoRootCrossCredit&&bTrialSwitchPreserved&&bTrialDuplicateRejected
                &&PS->bLastSaveSucceeded&&PS->GetAdvancementTrialProgress(2)==3&&PS->GetAdvancementTrialProgress(3)==3;
            Finish(bOK,bOK?TEXT("Real skill trials, both dungeons, all three seals and both boss patterns completed using normal player actions.")
                :TEXT("Adventure assertions incomplete; inspect individual observed fields."));
        }break;
    }
}
