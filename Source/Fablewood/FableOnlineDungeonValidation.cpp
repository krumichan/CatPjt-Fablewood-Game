#include "FableOnlineValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Dom/JsonObject.h"

void UFableOnlineValidationDriver::DungeonHold(AFablePlayerController* PC,const FKey& Key,bool Held)
{
    if(PC&&PC->IsInputKeyDown(Key)!=Held)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Held?IE_Pressed:IE_Released,Held?1.f:0.f,0,FInputDeviceId::CreateFromInternalId(0)));
}
void UFableOnlineValidationDriver::DungeonKey(AFablePlayerController* PC,const FKey& Key)
{
    if(!PC)return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Pressed,1.f,0,FInputDeviceId::CreateFromInternalId(0)));
    DungeonReleaseKeys.AddUnique(Key);
}
void UFableOnlineValidationDriver::DungeonReleaseInput()
{
    auto* PC=GetGameInstance()?Cast<AFablePlayerController>(GetGameInstance()->GetFirstLocalPlayerController()):nullptr;
    if(PC)for(const auto& Key:DungeonReleaseKeys)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,IE_Released,0.f,0,FInputDeviceId::CreateFromInternalId(0)));
    DungeonReleaseKeys.Reset();
}
bool UFableOnlineValidationDriver::DungeonWalk(AFablePlayerController* PC,const FVector& Location,float Radius)
{
    auto* Hero=PC?Cast<AFableCharacter>(PC->GetPawn()):nullptr;if(!Hero||!Hero->IsAlive())return false;
    const FVector Delta=Location-Hero->GetActorLocation();DungeonHold(PC,EKeys::S,false);
    if(Delta.Size2D()<=Radius){DungeonHold(PC,EKeys::W,false);return true;}
    PC->SetControlRotation(FRotator(-12,Delta.Rotation().Yaw,0));DungeonHold(PC,EKeys::W,true);return false;
}
AFableEnemy* UFableOnlineValidationDriver::DungeonTarget(AFableCharacter* Hero) const
{
    AFableEnemy* Target=nullptr;float Distance=MAX_flt;
    const auto* GS=Hero?Hero->GetWorld()->GetGameState<AFableGameState>():nullptr;if(!GS)return nullptr;
    for(TActorIterator<AFableEnemy> It(Hero->GetWorld());It;++It)
    {
        const float D=FVector::Dist2D(Hero->GetActorLocation(),It->GetActorLocation());
        if(It->IsAlive()&&It->GetEncounterRegion()==GS->ActiveRegion&&D<Distance){Distance=D;Target=*It;}
    }
    return Target;
}
void UFableOnlineValidationDriver::DungeonQuest(AFablePlayerController* PC,EFableQuestId Quest,double Now)
{
    auto* Hero=Cast<AFableCharacter>(PC->GetPawn());
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("client1")?-100:100,-120,100);
    if(PC->IsAnyPanelOpen()&&FVector::DistSquared2D(Hero->GetActorLocation(),Board)>10000)PC->UIClosePanels();
    if(!DungeonWalk(PC,Board,85)||Hero->GetVelocity().Size2D()>10){DungeonActionAt=Now;return;}
    if(Now-DungeonActionAt<.8)return;
    if(PC->GetDialogue()==EFableDialogue::Guide){PC->UISelectQuest(Quest);PC->UIConfirmQuest();}
    else{PC->UIClosePanels();DungeonKey(PC,EKeys::E);}
    DungeonActionAt=Now;
}
void UFableOnlineValidationDriver::DungeonFight(AFablePlayerController* PC,double Now)
{
    auto* Hero=Cast<AFableCharacter>(PC->GetPawn());auto* P=PC->GetPlayerState<AFablePlayerState>();
    auto* Target=DungeonTarget(Hero);if(PC->IsAnyPanelOpen())PC->UIClosePanels();
    if(!Hero||!Hero->IsAlive()||!Target){DungeonHold(PC,EKeys::W,false);DungeonHold(PC,EKeys::S,false);return;}
    const float Distance=FVector::Dist2D(Hero->GetActorLocation(),Target->GetActorLocation());
    PC->SetControlRotation(FRotator(-12,(Target->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));
    DungeonHold(PC,EKeys::W,Distance>570);DungeonHold(PC,EKeys::S,Distance<350);
    if(P&&Hero->GetHealth()<Hero->GetMaxHealth()*.45f&&P->HealthPotions>0&&P->GetPotionCooldownRemaining()<=0&&Now-DungeonActionAt>1)
    {PC->UIUseHealthPotion();DungeonActionAt=Now;}
    if(Distance<1050&&Now-DungeonAttackAt>.65){DungeonKey(PC,EKeys::J);DungeonAttackAt=Now;}
    if(Distance<850&&Now-DungeonSkillAt>6.5&&Hero->GetMana()>=Hero->GetSkillManaCost(0))
    {DungeonKey(PC,EKeys::K);DungeonSkillAt=Now;}
}
bool UFableOnlineValidationDriver::TickDungeon(AFablePlayerController* PC,AFablePlayerState* P,AFableCharacter* Hero,double Now)
{
    auto* GS=Hero->GetWorld()->GetGameState<AFableGameState>();if(!GS)return false;
    if(!DungeonInitial){DungeonInitial=Profile(P);DungeonStepAt=Now;}
    const auto Advance=[&](int32 Next)
    {
        DungeonHold(PC,EKeys::W,false);DungeonHold(PC,EKeys::S,false);DungeonHold(PC,EKeys::E,false);
        PC->UIClosePanels();DungeonStep=Next;DungeonStepAt=Now;DungeonActionAt=-10;
    };
    const auto Party=[&](EFableQuestId Quest,EFableQuestStage Stage)
    {
        if(GS->PlayerArray.Num()!=2)return false;
        for(auto* Entry:GS->PlayerArray){const auto* Other=Cast<AFablePlayerState>(Entry);if(!Other||Other->GetQuestStage(Quest)!=Stage)return false;}return true;
    };
    if(Now-DungeonStepAt>180){Finish(false,FString::Printf(TEXT("Online dungeon step %d timed out."),DungeonStep));return false;}
    if(GS->bPartyRecoveryPending||Hero->IsDefeated())
    {Finish(false,TEXT("The online dungeon party required automatic recovery."));return false;}
    auto* Target=DungeonTarget(Hero);
    switch(DungeonStep)
    {
    case 0:
        if(P->ForestQuest==EFableQuestStage::Active){Advance(1);break;}
        DungeonQuest(PC,EFableQuestId::Forest,Now);break;
    case 1:
        if(Party(EFableQuestId::Forest,EFableQuestStage::Active)){Checks.Add(TEXT("both_forest_quests_active"),true);Advance(2);}break;
    case 2:
        if(P->ForestQuest==EFableQuestStage::Ready){Advance(3);break;}
        DungeonFight(PC,Now);break;
    case 3:
        if(P->ForestQuest==EFableQuestStage::Completed){Advance(4);break;}
        DungeonQuest(PC,EFableQuestId::Forest,Now);break;
    case 4:
        if(!P->IsBackendReady()||P->IsBackendSavePending())break;
        if(DungeonWeaponId==0)
        {
            for(const auto& Item:P->Inventory)if(Item.Rarity==1&&Item.Slot==EFableEquipmentSlot::Weapon&&Item.HeroClass==P->HeroClass)
            {DungeonWeaponId=Item.Id;PC->UIEquip(Item.Id);break;}
        }
        else if(P->EquippedWeaponId==DungeonWeaponId){Checks.Add(TEXT("forest_reward_equipped"),true);Advance(5);}
        break;
    case 5:
        if(P->DungeonQuest==EFableQuestStage::Active){Advance(6);break;}
        DungeonQuest(PC,EFableQuestId::RootRuins,Now);break;
    case 6:
    {
        if(GS->ActiveRegion==EFableRegion::RootRuins&&FableRPG::IsInRegion(EFableRegion::RootRuins,Hero->GetActorLocation()))
        {Checks.Add(TEXT("entered_root_ruins"),true);Capture(TEXT("dungeon-entry"));Advance(7);break;}
        if(!Party(EFableQuestId::RootRuins,EFableQuestStage::Active))break;
        const FVector Portal=FableRPG::PortalLocation()+FVector(Role==TEXT("client1")?-80:80,-100,100);
        if(PC->IsAnyPanelOpen()&&FVector::DistSquared2D(Hero->GetActorLocation(),Portal)>10000)PC->UIClosePanels();
        if(!DungeonWalk(PC,Portal,100)||Hero->GetVelocity().Size2D()>10){DungeonActionAt=Now;break;}
        if(Now-DungeonActionAt<1)break;
        if(PC->IsPortalOpen()){PC->UISelectDungeon(EFableRegion::RootRuins);PC->UISelectDifficulty(EFableDifficulty::Normal);PC->UIEnterSelectedDungeon();}
        else{PC->UIClosePanels();DungeonKey(PC,EKeys::E);}DungeonActionAt=Now;break;
    }
    case 7:
        if(GS->DungeonWave>=2)Checks.Add(TEXT("second_wave"),true);
        if(GS->AdventureStage==EFableAdventureStage::Boss)
        {
            Checks.Add(TEXT("boss_seen"),true);
            if(Target&&Target->IsBoss()&&Target->GetHealth()<=Target->GetMaxHealth()*.70f)
            {Capture(TEXT("boss-before-rescue"));Advance(8);break;}
        }
        DungeonFight(PC,Now);break;
    case 8:
    {
        AFableCharacter* Fallen=nullptr;
        for(auto* Entry:GS->PlayerArray){auto* Other=Cast<AFablePlayerState>(Entry);if(Other&&Other->HeroClass==EFableClass::Archer)Fallen=Cast<AFableCharacter>(Other->GetPawn());}
        if(!Fallen)break;
        if(Fallen->IsDowned()){Checks.Add(TEXT("ally_downed"),true);Capture(TEXT("ally-downed"));Advance(9);break;}
        if(PC->IsAnyPanelOpen())PC->UIClosePanels();
        if(Hero==Fallen)
        {if(Target)DungeonWalk(PC,Target->GetActorLocation()+FVector(70,-90,0),80);}
        else DungeonWalk(PC,FableRPG::RegionEntrance(EFableRegion::RootRuins)+FVector(-350,-300,100),100);
        break;
    }
    case 9:
        if(GS->AdventureStage!=EFableAdventureStage::Cleared){DungeonFight(PC,Now);break;}
        Checks.Add(TEXT("boss_defeated"),true);Advance(10);break;
    case 10:
    {
        AFableCharacter* Fallen=nullptr;
        for(auto* Entry:GS->PlayerArray){auto* Other=Cast<AFablePlayerState>(Entry);if(Other&&Other->HeroClass==EFableClass::Archer)Fallen=Cast<AFableCharacter>(Other->GetPawn());}
        if(!Fallen)break;
        if(Fallen->IsAlive()&&Fallen->GetTimesRevivedCount()>0)
        {Checks.Add(TEXT("ally_revived"),true);Capture(TEXT("ally-revived"));Advance(11);break;}
        if(Fallen->IsDefeated()){Finish(false,TEXT("The ally revive window expired."));break;}
        if(Hero!=Fallen)
        {
            if(!DungeonWalk(PC,Fallen->GetActorLocation(),170)||Hero->GetVelocity().Size2D()>10)break;
            if(Hero->GetReviveTarget()==Fallen)DungeonHold(PC,EKeys::E,true);
            else if(Now-DungeonActionAt>1){DungeonHold(PC,EKeys::E,false);PC->UIStartRevive(Fallen);DungeonActionAt=Now;}
        }
        break;
    }
    case 11:
        if(!P->IsBackendReady()||P->IsBackendSavePending()||!P->bLastSaveSucceeded||P->GetBackendRevision()!=P->GetBackendAcknowledgedRevision())break;
        if(!Party(EFableQuestId::RootRuins,EFableQuestStage::Ready))break;
        if(P->Gold!=383||P->Ore!=21||P->RootCores!=1||P->Level!=4||P->Experience!=65||P->DungeonCompletions!=0
            ||P->Inventory.Num()!=DungeonInitial->GetIntegerField(TEXT("bagCount"))+2)
        {Finish(false,TEXT("Dungeon rewards do not match three forest kills, six guardians and one boss."));break;}
        Checks.Add(TEXT("server_reward_totals"),true);return true;
    default:Finish(false,TEXT("Unknown online dungeon step."));break;
    }
    return false;
}