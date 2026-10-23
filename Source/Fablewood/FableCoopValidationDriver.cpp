#include "FableCoopValidationDriver.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableEnemyBolt.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "TimerManager.h"
UFableCoopValidationDriver::UFableCoopValidationDriver(){PrimaryComponentTick.bCanEverTick=true;}
void UFableCoopValidationDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
#else
    PC=Cast<AFablePlayerController>(GetOwner());
    if(!PC.IsValid() || !PC->IsLocalController()
        || !FParse::Value(FCommandLine::Get(),TEXT("FableCoopValidationRole="),Role)
        || !FParse::Value(FCommandLine::Get(),TEXT("FableRPGValidationRun="),RunId)
        || !FParse::Value(FCommandLine::Get(),TEXT("FableCoopValidationDirectory="),Directory)
        || !FParse::Value(FCommandLine::Get(),TEXT("FableCoopScenario="),Scenario)
        || (Role!=TEXT("host") && Role!=TEXT("guest")) || FPaths::IsRelative(Directory))
    {SetComponentTickEnabled(false);return;}
    FParse::Value(FCommandLine::Get(),TEXT("FableCoopPhase="),Phase);
    if(Scenario==TEXT("disconnect") && Phase==TEXT("fresh") && Role==TEXT("guest")
        && IFileManager::Get().FileExists(*FPaths::Combine(Directory,TEXT("guest-disconnect-approved.txt"))))
    {
        // A local level opened after menu disconnect must not restart this
        // opt-in scenario or overwrite its authoritative ready report.
        SetComponentTickEnabled(false);return;
    }
    bSolo=Scenario==TEXT("solo");IFileManager::Get().MakeDirectory(*Directory,true);
    StartedAt=StepAt=GetWorld()->GetTimeSeconds();
#endif
}
void UFableCoopValidationDriver::Key(const FKey& Value)
{
    if(!PC.IsValid() || !PC->PlayerInput)return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,IE_Pressed,1,0,FInputDeviceId::CreateFromInternalId(0)));ReleaseKeys.AddUnique(Value);
}
void UFableCoopValidationDriver::Hold(const FKey& Value,bool bHeld)
{
    if(PC.IsValid() && PC->PlayerInput && PC->IsInputKeyDown(Value)!=bHeld)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,bHeld?IE_Pressed:IE_Released,bHeld?1:0,0,FInputDeviceId::CreateFromInternalId(0)));
}
void UFableCoopValidationDriver::Release()
{
    if(!PC.IsValid())return;
    for(const FKey& Value:ReleaseKeys)PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,IE_Released,0,0,FInputDeviceId::CreateFromInternalId(0)));
    ReleaseKeys.Empty();
}
void UFableCoopValidationDriver::Stop(){Hold(EKeys::W,false);Hold(EKeys::S,false);Hold(EKeys::A,false);Hold(EKeys::D,false);}
bool UFableCoopValidationDriver::WalkTo(const FVector& Location,float Radius)
{
    const APawn* Pawn=PC.IsValid()?PC->GetPawn():nullptr;if(!Pawn)return false;
    Hold(EKeys::S,false);const FVector Delta=Location-Pawn->GetActorLocation();
    if(Delta.Size2D()<=Radius){Hold(EKeys::W,false);return true;}
    PC->SetControlRotation(FRotator(-12,Delta.Rotation().Yaw,0));Hold(EKeys::W,true);return false;
}
void UFableCoopValidationDriver::Advance(int32 Value)
{
    Stop();Hold(EKeys::E,false);Step=Value;StepAt=GetWorld()->GetTimeSeconds();Substep=0;SubAt=StepAt;LastAction=-10;
    UE_LOG(LogTemp,Display,TEXT("FABLE_COOP_VALIDATION role=%s scenario=%s step=%d"),*Role,*Scenario,Step);
    Report(TEXT("running"),TEXT("Normal-input co-op recovery validation in progress."));
}
void UFableCoopValidationDriver::Capture(const FString& Name)
{
    const FString Path=FPaths::Combine(Directory,Role+TEXT("-")+Name+TEXT(".png"));const TWeakObjectPtr<UFableCoopValidationDriver> Weak(this);FTimerHandle Timer;
    GetWorld()->GetTimerManager().SetTimer(Timer,[Weak,Path](){if(Weak.IsValid())FScreenshotRequest::RequestScreenshot(Path,true,false);},.35f,false);Screenshots.AddUnique(Path);
}
TSharedPtr<FJsonObject> UFableCoopValidationDriver::PeerReport() const
{
    FString Data;TSharedPtr<FJsonObject> Value;
    if(FFileHelper::LoadFileToString(Data,*FPaths::Combine(Directory,Role==TEXT("host")?TEXT("guest.json"):TEXT("host.json"))))FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data),Value);

    return Value;
}
int32 UFableCoopValidationDriver::PeerStep() const
{
    const TSharedPtr<FJsonObject> Value=PeerReport();double Number=0;
    return Value.IsValid() && Value->TryGetNumberField(TEXT("step"),Number)?int32(Number):-1;
}
AFableCharacter* UFableCoopValidationDriver::Partner() const
{
    const APawn* Own=PC.IsValid()?PC->GetPawn():nullptr;
    for(TActorIterator<AFableCharacter> It(GetWorld());It;++It)if(*It!=Own && It->GetPlayerState<AFablePlayerState>())return *It;
    return nullptr;
}
AFableEnemy* UFableCoopValidationDriver::NearestEnemy(bool bBossOnly,bool bPreferMelee) const
{
    const APawn* Hero=PC.IsValid()?PC->GetPawn():nullptr;const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();if(!Hero || !GS)return nullptr;
    AFableEnemy* Target=nullptr;float Closest=MAX_flt;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
    {
        if(!It->IsAlive() || It->GetEncounterRegion()!=GS->ActiveRegion || (bBossOnly && !It->IsBoss()))continue;
        const float Distance=FVector::DistSquared2D(Hero->GetActorLocation(),It->GetActorLocation())+(bPreferMelee && It->IsRanged()?100000000.f:0.f);
        if(Distance<Closest){Target=*It;Closest=Distance;}
    }
    return Target;
}
int32 UFableCoopValidationDriver::EnemyCount() const
{
    const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();int32 Count=0;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)if(It->IsAlive() && GS && It->GetEncounterRegion()==GS->ActiveRegion)++Count;
    return Count;
}
bool UFableCoopValidationDriver::EnemiesAreFresh() const
{
    if(EnemyCount()!=3)return false;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)if(It->IsAlive() && !FMath::IsNearlyEqual(It->GetHealth(),It->GetMaxHealth(),.1f))return false;
    return true;
}
bool UFableCoopValidationDriver::PartyEligible(EFableRegion Region) const
{
    const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();if(!GS || GS->PlayerArray.Num()!=(bSolo?1:2))return false;
    for(const APlayerState* Entry:GS->PlayerArray)
    {
        const AFablePlayerState* Profile=Cast<AFablePlayerState>(Entry);if(!Profile || !Profile->bProfileLoaded)return false;
        if(Region==EFableRegion::Village){if(Profile->ForestQuest!=EFableQuestStage::Active)return false;}
        else if(!Profile->IsRegionEligible(Region))return false;
    }
    return true;
}
void UFableCoopValidationDriver::SelectQuest(EFableQuestId Quest)
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();const double Now=GetWorld()->GetTimeSeconds();
    const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
    if(PC->IsAnyPanelOpen() && FVector::DistSquared2D(Hero->GetActorLocation(),Board)>10000)PC->UIClosePanels();
    if(!WalkTo(Board,85) || Hero->GetVelocity().Size2D()>10){LastAction=Now;return;}
    if(Now-LastAction<.7)return;
    if(PC->GetDialogue()==EFableDialogue::Guide){PC->UISelectQuest(Quest);PC->UIConfirmQuest();}else{PC->UIClosePanels();Key(EKeys::E);}LastAction=Now;
}
void UFableCoopValidationDriver::EnterRegion(EFableRegion Region,bool bRetry)
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();const double Now=GetWorld()->GetTimeSeconds();
    const FVector Destination=(Region==EFableRegion::Village?FableRPG::BoardLocation():FableRPG::PortalLocation())+FVector(Role==TEXT("host")?-90:90,-110,100);
    if(PC->IsAnyPanelOpen() && FVector::DistSquared2D(Hero->GetActorLocation(),Destination)>10000)PC->UIClosePanels();
    if(!WalkTo(Destination,95) || Hero->GetVelocity().Size2D()>10){LastAction=Now;return;}
    if(Now-LastAction<.8)return;
    if(bRetry)
    {
        if(!Checks.FindRef(TEXT("retry_ui_captured"))){Capture(TEXT("retry-ready"));Checks.Add(TEXT("retry_ui_captured"),true);LastAction=Now;return;}
        Key(EKeys::R);
    }
    else if(PC->IsPortalOpen()){PC->UISelectDungeon(Region);PC->UIEnterSelectedDungeon();}else{PC->UIClosePanels();Key(EKeys::E);}LastAction=Now;
}
void UFableCoopValidationDriver::Fight()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();AFableEnemy* Target=NearestEnemy();if(PC->IsAnyPanelOpen())PC->UIClosePanels();
    if(!Hero->IsAlive() || !Target){Stop();return;}
    const float Distance=FVector::Dist2D(Hero->GetActorLocation(),Target->GetActorLocation());PC->SetControlRotation(FRotator(-12,(Target->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));
    Hold(EKeys::W,Distance>570);Hold(EKeys::S,Distance<350);const double Now=GetWorld()->GetTimeSeconds();
    if(Now-LastAction>.62 && Distance<1050){Key(EKeys::J);LastAction=Now;++AttackInputs;}

}
void UFableCoopValidationDriver::TakeHits()
{
    if(PC->IsAnyPanelOpen())PC->UIClosePanels();AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();if(!Hero->IsAlive()){Stop();return;}
    AFableEnemy* Enemy=NearestEnemy(false,true);if(Enemy)WalkTo(Enemy->GetActorLocation()+FVector(Role==TEXT("host")?-80:80,-100,0),100);else Stop();
}
FString UFableCoopValidationDriver::Signature(const AFablePlayerState* PS,bool bIgnoreWear) const
{
    if(!PS)return TEXT("");
    FString Value=FString::Printf(TEXT("%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|potions:%d,%d"),uint8(PS->HeroClass),PS->Level,PS->Experience,PS->Gold,PS->Ore,PS->EquippedWeaponId,PS->EquippedArmorId,uint8(PS->ForestQuest),uint8(PS->DungeonQuest),PS->QuestKills,PS->HealthPotions,PS->ManaPotions);
    // Combat may wear carried equipment. Every other persistent field remains exact.
    for(const FFableItem& Item:PS->Inventory)
    {
        FFableItem Copy=Item;
        if(bIgnoreWear)Copy.Durability=Copy.MaxDurability;
        Value+=TEXT(";")+FableRPG::ItemPersistentSignature(Copy);
    }
    Value+=FString::Printf(TEXT("|completion=%d,%d,%d"),PS->DungeonCompletions,uint8(PS->MoonwellQuest),PS->MoonwellCompletions);
    for(const FFableClassProgress& Progress:PS->ClassProgress)
    {
        Value+=TEXT("|tier=")+FString::FromInt(Progress.Tier);for(int32 Rank:Progress.Ranks)Value+=TEXT(",")+FString::FromInt(Rank);
        Value+=FString::Printf(TEXT("|trials=%d,%d,%d;%d,%d,%d"),uint8(Progress.SecondTrial.Stage),Progress.SecondTrial.ValidSkillCasts,Progress.SecondTrial.bLegacyGranted,uint8(Progress.ThirdTrial.Stage),Progress.ThirdTrial.ValidSkillCasts,Progress.ThirdTrial.bLegacyGranted);
    }
    Value+=FString::Printf(TEXT("|v6:materials=%d,%d,%d|stash"),PS->Essence,PS->RootCores,PS->MoonCrystals);
    // Stored equipment cannot wear during combat, so even its current durability stays exact.
    for(const FFableItem& Item:PS->StashItems)Value+=TEXT(";")+FableRPG::ItemPersistentSignature(Item);
    Value+=FString::Printf(TEXT("|v7:story=%d,%d,%d"),PS->StoryChapter,PS->StoryObjectMask,PS->StorySealMask);
    Value+=TEXT("|v8:talents=")+PS->GetTalentSignature();
    return Value;
}
void UFableCoopValidationDriver::TickSetup()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();const double Now=GetWorld()->GetTimeSeconds();
    const EFableClass Wanted=Role==TEXT("host")?EFableClass::Mage:EFableClass::Archer;
    const bool bRoot=Scenario==TEXT("root-boss-wipe"),bMoon=Scenario==TEXT("moon-seal-wipe");
    const EFableRegion Region=bRoot?EFableRegion::RootRuins:bMoon?EFableRegion::Moonwell:EFableRegion::Village;
    if(Step==0)
    {
        if(GS->PlayerArray.Num()!=(bSolo?1:2))return;InitialRecovery=GS->PartyRecoveryCount;InitialSerial=GS->EncounterSerial;Advance(1);return;
    }
    if(Step==1)
    {
        if(PS->HeroClass!=Wanted)
        {
            const FVector Board=FableRPG::BoardLocation()+FVector(Role==TEXT("host")?-100:100,-120,100);
            if(WalkTo(Board,90) && Now-LastAction>.8){PC->UIChooseClass(Wanted);LastAction=Now;}return;
        }
        const EFableQuestId Quest=bRoot?EFableQuestId::RootRuins:bMoon?EFableQuestId::Moonwell:EFableQuestId::Forest;
        if(PS->GetQuestStage(Quest)!=EFableQuestStage::Active){SelectQuest(Quest);return;}
        PC->UIClosePanels();if(!PartyEligible(Region))return;Advance(2);return;
    }
    if(Step==2)
    {
        if(Region!=EFableRegion::Village && GS->ActiveRegion!=Region){EnterRegion(Region,false);return;}
        if(GS->RequiredEnemies<1)return;InitialSerial=GS->EncounterSerial;BeforeWipeSignature=Signature(PS,true);
        if(bRoot)Advance(20);else if(bMoon)Advance(30);
        else if(Scenario==TEXT("revive") || Scenario==TEXT("disconnect"))Advance(10);
        else if(Scenario==TEXT("timeout"))Advance(11);else Advance(40);
    }
}
void UFableCoopValidationDriver::TickRescue()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();AFableCharacter* Other=Partner();AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const double Now=GetWorld()->GetTimeSeconds(),Wait=Now-StepAt;AFableCharacter* Fallen=Role==TEXT("host")?Hero:Other;if(!Fallen)return;
    if(Hero->GetReviveProtectionRemaining()>0 || Fallen->GetReviveProtectionRemaining()>0)Checks.Add(TEXT("protection_seen"),true);
    if(Step==10)
    {
        if(EnemyCount()==0){Finish(false,TEXT("Preparation killed the last enemy needed for real damage."));return;}if(EnemyCount()==1){Advance(11);return;}if(Role==TEXT("guest"))Fight();else {if(Substep==0){if(WalkTo(FVector(-750,-1500,100),90))Substep=1;}else WalkTo(FVector(-750,-2100,100),90);}return;
    }
    if(Step==11)
    {
        if(Role==TEXT("guest")){if(Substep==0){if(WalkTo(FVector(450,-1500,100),90))Substep=1;}else if(WalkTo(FVector(450,-2100,100),80))Advance(12);}else if(PeerStep()>=12)Advance(12);return;
    }
    if(Step==12)
    {
        if(Fallen->IsDowned())
        {
            Checks.Add(TEXT("downed_seen"),true);DownedOrigin=Fallen->GetActorLocation();
            RescueApproachPoint=DownedOrigin;
            if(Role==TEXT("guest"))
            {
                if(AFableEnemy* Blocker=NearestEnemy())
                {
                    // Walk around the guard standing over the fallen ally.
                    // A straight line stops at its capsule before revive range.
                    const FVector Axis=(Blocker->GetActorLocation()-DownedOrigin).GetSafeNormal2D();
                    const FVector Side(-Axis.Y,Axis.X,0);
                    const FVector Middle=(Blocker->GetActorLocation()+DownedOrigin)*.5f;
                    FVector A=Middle+Side*160,B=Middle-Side*160;
                    A=DownedOrigin+(A-DownedOrigin).GetClampedToMaxSize2D(195);
                    B=DownedOrigin+(B-DownedOrigin).GetClampedToMaxSize2D(195);
                    RescueApproachPoint=FVector::DistSquared2D(Hero->GetActorLocation(),A)<FVector::DistSquared2D(Hero->GetActorLocation(),B)?A:B;
                }
            }
            Capture(TEXT("ally-downed"));BeforePotion=PS->HealthPotions;BeforeCasts=Hero->GetSkillCastCount(0);Advance(13);return;
        }
        if(Role==TEXT("host"))
        {
            // Lure the real guard into the open clearing before waiting for
            // its attacks. The quest board blocks a direct rescue approach
            // when TakeHits chases the guard back through the village services.
            if(Substep==0){if(WalkTo(FVector(-750,-1500,100),90))Substep=1;}
            else if(Substep==1){if(WalkTo(FVector(450,-1500,100),90))Substep=2;}
            else if(Substep==2){if(WalkTo(FVector(450,0,100),75))Substep=3;}
            else Stop();
        }
        else Stop();return;
    }
    if(Role==TEXT("host"))
    {
        Stop();
        if(Step==13 && !Checks.FindRef(TEXT("downed_actions_blocked")))
        {
            if(Wait<.4){Key(EKeys::J);Key(EKeys::K);PC->UIUseHealthPotion();}
            else if(Wait>.9)
            {
                if(Hero->GetSkillCastCount(0)!=BeforeCasts || PS->HealthPotions!=BeforePotion || Hero->IsAlive()){Finish(false,TEXT("A downed hero attacked, cast or consumed a potion."));return;}
                Checks.Add(TEXT("downed_actions_blocked"),true);
            }
        }
        if(PeerStep()>=16 && BeforeReviveSignature.IsEmpty())BeforeReviveSignature=Signature(PS,true);
        if(Scenario==TEXT("disconnect") && GS->PlayerArray.Num()==1 && GS->PartyRecoveryCount>InitialRecovery && Hero->IsAlive())
        {
            Checks.Add(TEXT("disconnect_recovered"),!Hero->GetReviver() && !Hero->GetReviveTarget());Capture(TEXT("disconnect-rescued"));Advance(60);return;
        }
        if(Hero->IsAlive() && Hero->GetTimesRevivedCount()>0)
        {
            if(FVector::Dist2D(Hero->GetActorLocation(),DownedOrigin)>=80 || Hero->GetHealth()>Hero->GetMaxHealth()*.41f || !Checks.FindRef(TEXT("protection_seen")))
            {Finish(false,TEXT("Revive did not restore 40 percent in place with protection."));return;}
            Checks.Add(TEXT("revived_in_place"),true);Checks.Add(TEXT("revive_no_reward"),!BeforeReviveSignature.IsEmpty() && Signature(PS,true)==BeforeReviveSignature);Capture(TEXT("revived-in-place"));Advance(60);return;
        }
        if((Scenario!=TEXT("disconnect") || (GS->PlayerArray.Num()==2 && !GS->bPartyRecoveryPending)) && (Fallen->IsDefeated() || (Fallen->IsAlive() && Hero->GetTimesRevivedCount()==0)))Finish(false,TEXT("Revive window expired or automatic recovery replaced ally revival."));return;
    }
    if(!Other || (!Fallen->IsDowned() && Step<19)){Finish(false,TEXT("Fallen ally left the rescue window too soon."));return;}
    if(Step==13)
    {
        if(!bAttemptedRange && FVector::Dist2D(Hero->GetActorLocation(),Fallen->GetActorLocation())>350){PC->UIStartRevive(Fallen);bAttemptedRange=true;SubAt=Now;return;}
        if(bAttemptedRange && !Checks.FindRef(TEXT("range_rejected")))
        {if(Now-SubAt<.4)return;Checks.Add(TEXT("range_rejected"),Hero->GetReviveTarget()==nullptr);}
        if(!WalkTo(RescueApproachPoint,35) || Hero->GetVelocity().Size2D()>10){SubAt=Now;return;}
        if(Now-SubAt<.45)return;
        PC->SetControlRotation(FRotator(-12,(Fallen->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));
        BeforeCancel=Hero->GetReviveCancelCount();HealthBeforeRevive=Hero->GetHealth();Capture(TEXT("revive-under-attack"));Advance(14);Hold(EKeys::E,true);return;
    }
    if(Step==14)
    {
        Hold(EKeys::E,true);
        if(Hero->GetReviveCancelCount()>BeforeCancel && !Hero->GetReviveTarget() && Hero->GetHealth()<HealthBeforeRevive)
        {Checks.Add(TEXT("damage_cancelled"),true);Hold(EKeys::E,false);Advance(15);}
        else if(Wait>4)Finish(false,TEXT("Enemy damage did not interrupt revival."));return;
    }
    if(Step==15)
    {
        if(EnemyCount()>0){Fight();return;}Stop();if(Hero->GetVelocity().Size2D()>10)return;
        Advance(16);PC->UIStartRevive(Hero);return;
    }
    if(Step==16)
    {
        if(Wait<.4)return;if(Hero->GetReviveTarget()){Finish(false,TEXT("Self revival was accepted."));return;}Checks.Add(TEXT("self_rejected"),true);
        if(!WalkTo(Fallen->GetActorLocation(),155) || Hero->GetVelocity().Size2D()>10)return;
        BeforeReviveSignature=Signature(PS,true);BeforeCancel=Hero->GetReviveCancelCount();Advance(17);Hold(EKeys::E,true);return;
    }
    if(Step==17)
    {
        Stop();if(Substep<2)Hold(EKeys::E,true);
        if(Substep==0 && Hero->GetReviveProgress()>.16f){Capture(TEXT("revive-progress"));Substep=1;SubAt=Now;}
        if(Substep==1 && Now-SubAt>.5){Hold(EKeys::E,false);Substep=2;SubAt=Now;}
        if(Substep==2 && Now-SubAt>.4)
        {
            if(Hero->GetReviveCancelCount()<=BeforeCancel || Hero->GetReviveTarget()){Finish(false,TEXT("E release did not cancel revival."));return;}
            Checks.Add(TEXT("release_cancelled"),true);BeforeCancel=Hero->GetReviveCancelCount();Advance(18);
        }
        return;
    }
    if(Step==18)
    {
        if(Scenario==TEXT("disconnect")){Advance(70);return;}
        if(Substep==0){Hold(EKeys::E,true);if(Hero->GetReviveProgress()>.1f){Substep=1;SubAt=Now;Hold(EKeys::S,true);}}
        else if(Substep==1)
        {
            Hold(EKeys::E,true);Hold(EKeys::S,true);
            if(Hero->GetReviveCancelCount()>BeforeCancel){Stop();Hold(EKeys::E,false);Checks.Add(TEXT("movement_cancelled"),true);Advance(19);}
            else if(Now-SubAt>1)Finish(false,TEXT("Movement did not cancel revival."));
        }
        return;
    }
    if(Step==19)
    {
        if(!Fallen->IsDowned())
        {
            if(Fallen->IsAlive() && Hero->GetRevivesPerformedCount()>0)
            {Checks.Add(TEXT("revive_completed"),true);Checks.Add(TEXT("revive_no_reward"),Signature(PS,true)==BeforeReviveSignature);Capture(TEXT("ally-revived"));Advance(60);}
            else Finish(false,TEXT("Ally expired before the final revival."));return;
        }
        if(!WalkTo(Fallen->GetActorLocation(),150) || Hero->GetVelocity().Size2D()>10)return;
        if(Substep==0)
        {
            BeforeCancel=Hero->GetReviveCancelCount();Hold(EKeys::E,true);
            if(Hero->GetReviveProgress()>.1f){Key(EKeys::Q);Substep=1;SubAt=Now;}
        }
        else if(Substep==1 && Now-SubAt>.4)
        {
            if(Hero->GetReviveCancelCount()<=BeforeCancel || Hero->GetReviveTarget()){Finish(false,TEXT("Opening the quest panel did not cancel revival."));return;}
            Checks.Add(TEXT("panel_cancelled"),true);Hold(EKeys::E,false);PC->UIClosePanels();Substep=2;SubAt=Now;
        }
        else if(Substep==2 && Now-SubAt>.5)Hold(EKeys::E,true);
        return;
    }
    if(Step==71)
    {
        if(IFileManager::Get().FileExists(*FPaths::Combine(Directory,TEXT("guest-disconnect-approved.txt"))))
        {
            // Use the actual menu action so NetConnection closes normally; an
            // OS kill would only become visible after the UDP timeout.
            bDone=true;PC->UIReturnToFrontEnd();
        }
        return;
    }    if(Step==70)
    {
        if(!WalkTo(Fallen->GetActorLocation(),150) || Hero->GetVelocity().Size2D()>10)return;Hold(EKeys::E,true);
        if(Hero->GetReviveProgress()>.2f)
        {Checks.Add(TEXT("ready_for_disconnect"),true);Report(TEXT("disconnect_ready"),TEXT("Authorize this guest to leave through the actual start-menu action while revival is active."));Step=71;StepAt=Now;}
    }
}void UFableCoopValidationDriver::TickTimeout()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();AFableCharacter* Fallen=Role==TEXT("host")?Hero:Partner();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();if(!Fallen)return;
    if(Step==11)
    {
        if(Role==TEXT("guest")){if(Substep==0){if(WalkTo(FVector(450,-1500,100),90))Substep=1;}else if(WalkTo(FVector(450,-2100,100),80))Advance(12);}else if(PeerStep()>=12)Advance(12);return;
    }
    if(Step==12)
    {
        if(Role==TEXT("host"))TakeHits();else Stop();
        if(Fallen->IsDowned()){BeforeWipeSignature=Signature(PC->GetPlayerState<AFablePlayerState>(),true);Checks.Add(TEXT("downed_seen"),true);Capture(TEXT("bleedout-wait"));Advance(13);}return;
    }
    Stop();if(Fallen->IsDefeated())Checks.Add(TEXT("bleedout_seen"),true);
    if(Fallen->IsAlive() && Checks.FindRef(TEXT("bleedout_seen")))
    {
        if(Fallen->GetActorLocation().Y>=-800 || GS->PartyRecoveryCount!=InitialRecovery || EnemyCount()!=3 || Signature(PC->GetPlayerState<AFablePlayerState>(),true)!=BeforeWipeSignature)
        {Finish(false,TEXT("Timed-out ally rescue reset the surviving party or changed rewards."));return;}
        Checks.Add(TEXT("individual_rescue"),true);Capture(TEXT("individual-rescued"));Advance(60);
    }
}
void UFableCoopValidationDriver::TickWipe()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();const double Now=GetWorld()->GetTimeSeconds();
    const bool bRoot=Scenario==TEXT("root-boss-wipe"),bMoon=Scenario==TEXT("moon-seal-wipe");const EFableRegion Region=bRoot?EFableRegion::RootRuins:bMoon?EFableRegion::Moonwell:EFableRegion::Village;
    if(Step==20)
    {
        if(GS->AdventureStage!=EFableAdventureStage::Boss){Fight();return;}
        if(AFableEnemy* Boss=NearestEnemy(true))
        {
            Stop();if(Boss->GetHealth()<Boss->GetMaxHealth() && Boss->GetHealth()>0){Checks.Add(TEXT("boss_damaged_before_wipe"),true);BeforeWipeSignature=Signature(PS,true);Advance(22);return;}
            if(Role==TEXT("host") && Now-LastAction>1){PC->SetControlRotation(FRotator(-12,(Boss->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));Key(EKeys::J);LastAction=Now;++AttackInputs;}
        }
        return;
    }
    if(Step==30)
    {
        if(GS->MoonSealMask&1){Checks.Add(TEXT("seal_completed_before_wipe"),true);BeforeWipeSignature=Signature(PS,true);Advance(22);return;}
        if(EnemyCount()>0){Fight();return;}
        if(WalkTo(FableRPG::MoonSealLocation(0),180) && Now-LastAction>.8){Key(EKeys::E);LastAction=Now;}return;
    }
    if(Step==22 || Step==40)
    {
        if(GS->bPartyRecoveryPending && !Checks.FindRef(TEXT("wipe_countdown_seen"))){Checks.Add(TEXT("wipe_countdown_seen"),true);Capture(TEXT("party-wipe-countdown"));}
        if(Hero->IsDowned())Checks.Add(TEXT("downed_seen"),true);
        if(GS->PartyRecoveryCount>InitialRecovery && Hero->IsAlive() && GS->ActiveRegion==EFableRegion::Village)
        {
            if(Signature(PS,true)!=BeforeWipeSignature || EnemyCount()!=0 || GS->BossHealth!=0 || GS->MoonSealMask!=0 || !GS->bRetryAvailable || GS->RetryRegion!=Region)
            {Finish(false,TEXT("Party rescue lost progression or kept old encounter state."));return;}
            Checks.Add(TEXT("wipe_preserved_profile"),true);Checks.Add(TEXT("runtime_reset"),true);Capture(TEXT("party-rescued"));Advance(24);return;
        }
        if(!GS->bPartyRecoveryPending)TakeHits();else Stop();return;
    }
    if(Step==24)
    {
        if(GS->EncounterSerial>InitialSerial && GS->ActiveRegion==Region && GS->RequiredEnemies==3 && EnemyCount()==3)
        {
            if(GS->DefeatedEnemies!=0 || GS->MoonSealMask!=0 || GS->BossHealth!=0 || !EnemiesAreFresh()){Finish(false,TEXT("Retry did not start with fresh enemies, seals and boss state."));return;}
            Checks.Add(TEXT("retry_fresh"),true);Capture(TEXT("retry-fresh-encounter"));Advance(bRoot?25:60);return;
        }
        EnterRegion(Region,true);return;
    }
    if(Step==25)
    {
        if(PeerStep()<25){Stop();return;}
        if(GS->AdventureStage==EFableAdventureStage::Boss)
        {
            if(AFableEnemy* Boss=NearestEnemy(true))
            {
                if(!FMath::IsNearlyEqual(Boss->GetHealth(),Boss->GetMaxHealth(),.1f)){Finish(false,TEXT("Retried boss retained previous damage."));return;}
                Checks.Add(TEXT("boss_reset_full"),true);Capture(TEXT("retried-boss-full-health"));Advance(60);
            }
        }
        else Fight();
    }
}
void UFableCoopValidationDriver::TickCompletion()
{
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();
    AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const double Now=GetWorld()->GetTimeSeconds();
    if(Phase==TEXT("resume"))
    {
        Stop();Hold(EKeys::E,false);
        if(Now-StepAt>1.5)Finish(true,TEXT("The complete saved profile restored exactly in new processes."));
        return;
    }
    const auto StopReviving=[&]()
    {
        Hold(EKeys::E,false);
        if(Hero->GetReviveTarget())PC->UIStopRevive();
        bCompletionReviveHeld=false;
    };
    const int32 ExpectedPlayers=(bSolo || Scenario==TEXT("disconnect"))?1:2;
    // Both observers must record fresh enemies / full boss health before cleanup can attack.
    if(GS->PlayerArray.Num()!=ExpectedPlayers || (ExpectedPlayers==2 && PeerStep()<60))
    {CompletionQuietSince=-1;Stop();StopReviving();return;}
    if(EnemyCount()>0)
    {
        CompletionQuietSince=-1;StopReviving();
        // Ordinary attacks may award ordinary rewards and cause wear. Snapshot only afterwards.
        // No seal interaction is sent, so Moonwell stops after the current defenders.
        if(Hero->IsAlive() && !GS->bPartyRecoveryPending)Fight();else Stop();
        return;
    }
    Stop();
    bool bHasEnemyBolt=false;
    for(TActorIterator<AFableEnemyBolt> It(GetWorld());It;++It){bHasEnemyBolt=true;break;}
    if(bHasEnemyBolt || GS->bPartyRecoveryPending || !Hero->IsAlive())
    {CompletionQuietSince=-1;StopReviving();return;}
    AFableCharacter* Other=Partner();
    if(ExpectedPlayers==2 && !Other)
    {CompletionQuietSince=-1;StopReviving();return;}
    if(Other && Other->IsDowned())
    {
        CompletionQuietSince=-1;
        if(PC->IsAnyPanelOpen())PC->UIClosePanels();
        if(Hero->GetReviveTarget()==Other){Hold(EKeys::E,true);return;}
        // A rejected/interrupted channel needs release and a fresh press, not a permanent hold.
        if(bCompletionReviveHeld)
        {
            if(Now-CompletionRevivePressedAt>.6)
            {StopReviving();CompletionReviveRetryAt=Now+.4;}
            return;
        }
        if(!WalkTo(Other->GetActorLocation(),175) || Hero->GetVelocity().Size2D()>10)
        {CompletionReviveRetryAt=Now+.35;return;}
        if(Now<CompletionReviveRetryAt)return;
        PC->SetControlRotation(FRotator(-12,(Other->GetActorLocation()-Hero->GetActorLocation()).Rotation().Yaw,0));
        Hold(EKeys::E,true);bCompletionReviveHeld=true;CompletionRevivePressedAt=Now;
        return;
    }
    StopReviving();
    // Expired allies use the existing individual rescue. Never manufacture life/HP or a save.
    if((Other && (!Other->IsAlive() || Other->GetReviveTarget() || Other->GetReviver()))
        || Hero->GetReviveTarget() || Hero->GetReviver())
    {CompletionQuietSince=-1;return;}
    if(CompletionQuietSince<0)CompletionQuietSince=Now;
    if(Now-CompletionQuietSince<6.0)return;
    Checks.Add(TEXT("final_safe_checkpoint"),true);
    Finish(true,TEXT("Recovery assertions passed. The final checkpoint has no live enemies or hostile shots after six quiet seconds, including ordinary combat rewards and wear in the exact saved profile."));
}
void UFableCoopValidationDriver::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
    if(bDone)return;
    // Once a result is captured, only retry publication. Never resume scenario
    // input or replace its final snapshot with a periodic running report.
    if(bTerminalReportPending){PublishTerminalReport();return;}
    if(!PC.IsValid())return;
    Release();
    AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();if(!Hero || !PS || !PS->bProfileLoaded || !GS)return;
    const double Now=GetWorld()->GetTimeSeconds();
    if(!PreviousLocation.IsZero())Movement+=FVector::Dist2D(PreviousLocation,Hero->GetActorLocation());PreviousLocation=Hero->GetActorLocation();
    if(Now-LastReport>.5 && Step!=71){Report(TEXT("running"),TEXT("Normal-input co-op recovery validation in progress."));LastReport=Now;}
    if(Now-StartedAt>650 || Now-StepAt>200){Capture(TEXT("recovery-stuck"));Finish(false,FString::Printf(TEXT("Recovery scenario %s timed out at step %d."),*Scenario,Step));return;}
    if(Step==60){TickCompletion();return;}
    if(Phase==TEXT("resume"))
    {
        if(Now-StepAt<4 || GS->PlayerArray.Num()!=(bSolo?1:2))return;
        if(!Hero->IsAlive() || Hero->IsDowned() || Hero->IsDefeated() || Hero->GetReviveTarget() || Hero->GetReviver() || Hero->GetDownedTimeRemaining()!=0 || Hero->GetReviveProgress()!=0 || GS->bPartyRecoveryPending || GS->bRetryAvailable || GS->ActiveRegion!=EFableRegion::Village)
        {Finish(false,TEXT("Temporary downed, revival or retry state leaked into a new session."));return;}
        Checks.Add(TEXT("runtime_not_persisted"),true);Capture(TEXT("restored-active"));Advance(60);return;
    }
    if(Step<=2){TickSetup();return;}
    if(Scenario==TEXT("timeout"))TickTimeout();else if(Scenario==TEXT("revive") || Scenario==TEXT("disconnect"))TickRescue();else TickWipe();
}
void UFableCoopValidationDriver::Finish(bool bSuccess,const FString& Reason)
{
    if(bDone || bTerminalReportPending)return;
    Stop();Hold(EKeys::E,false);Release();
    TerminalReportStatus=bSuccess?TEXT("passed"):TEXT("failed");
    TerminalReportReason=Reason;
    // Serialize once: retries must not recapture later gameplay, notices or wear.
    TerminalReportText=SerializeReport(TerminalReportStatus,TerminalReportReason);
    if(TerminalReportText.IsEmpty())
    {
        bDone=true;
        UE_LOG(LogTemp,Error,TEXT("FABLE_COOP_VALIDATION_REPORT_IO_FAILED role=%s scenario=%s phase=%s attempts=0 reason=serialization_failed"),*Role,*Scenario,*Phase);
        return;
    }
    TerminalReportStartedAt=FPlatformTime::Seconds();
    NextTerminalReportAttempt=TerminalReportStartedAt;
    TerminalReportAttempts=0;
    TerminalReportLastError=0;
    bTerminalReportPending=true;
    PublishTerminalReport();
}
void UFableCoopValidationDriver::PublishTerminalReport()
{
    if(!bTerminalReportPending || bDone)return;
    const double Now=FPlatformTime::Seconds();
    const FString Path=FPaths::Combine(Directory,Role+TEXT(".json"));
    if(Now-TerminalReportStartedAt>=30.0)
    {
        bTerminalReportPending=false;
        bDone=true;
        // No terminal RESULT is emitted without a confirmed native report write.
        UE_LOG(LogTemp,Error,TEXT("FABLE_COOP_VALIDATION_REPORT_IO_FAILED role=%s scenario=%s phase=%s attempts=%d last_error=%u path=%s reason=publication_timeout"),
            *Role,*Scenario,*Phase,TerminalReportAttempts,TerminalReportLastError,*Path);
        return;
    }
    if(Now<NextTerminalReportAttempt)return;
    ++TerminalReportAttempts;
    if(FFileHelper::SaveStringToFile(TerminalReportText,*Path))
    {
        bTerminalReportPending=false;
        bDone=true;
        UE_LOG(LogTemp,Display,TEXT("FABLE_COOP_VALIDATION_RESULT role=%s scenario=%s status=%s attempts=%d reason=%s"),
            *Role,*Scenario,*TerminalReportStatus,TerminalReportAttempts,*TerminalReportReason);
        return;
    }
    TerminalReportLastError=FPlatformMisc::GetLastError();
    NextTerminalReportAttempt=Now+0.2;
    if(TerminalReportAttempts==1 || TerminalReportAttempts%25==0)
        UE_LOG(LogTemp,Warning,TEXT("FABLE_COOP_VALIDATION_REPORT_RETRY role=%s scenario=%s phase=%s attempts=%d last_error=%u path=%s"),
            *Role,*Scenario,*Phase,TerminalReportAttempts,TerminalReportLastError,*Path);
}
void UFableCoopValidationDriver::Report(const FString& Status,const FString& Reason)
{
    if(bDone || bTerminalReportPending)return;
    const FString Text=SerializeReport(Status,Reason);
    // Nonterminal observations naturally refresh on the next periodic report.
    if(!Text.IsEmpty())FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Directory,Role+TEXT(".json")));
}
FString UFableCoopValidationDriver::SerializeReport(const FString& Status,const FString& Reason)
{
    if(!PC.IsValid() || !GetWorld())return FString();
    const AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();const AFableCharacter* Hero=PC->GetPawn<AFableCharacter>();const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();TSharedRef<FJsonObject> J=MakeShared<FJsonObject>();
    J->SetBoolField(TEXT("report_snapshot_frozen"),Status==TEXT("passed") || Status==TEXT("failed"));
    J->SetStringField(TEXT("run_id"),RunId);J->SetStringField(TEXT("role"),Role);J->SetStringField(TEXT("scenario"),Scenario);J->SetStringField(TEXT("phase"),Phase);J->SetStringField(TEXT("status"),Status);J->SetStringField(TEXT("reason"),Reason);J->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());
    J->SetStringField(TEXT("network_mode"),GetWorld()->GetNetMode()==NM_ListenServer?TEXT("listen_server"):GetWorld()->GetNetMode()==NM_Standalone?TEXT("standalone"):TEXT("client"));
    J->SetNumberField(TEXT("step"),Step);J->SetNumberField(TEXT("substep"),Substep);J->SetNumberField(TEXT("player_count"),GS?GS->PlayerArray.Num():0);J->SetNumberField(TEXT("movement_cm"),Movement);J->SetNumberField(TEXT("attack_inputs"),AttackInputs);J->SetStringField(TEXT("profile_signature"),Signature(PS));
    J->SetBoolField(TEXT("save_succeeded"),PS && PS->bLastSaveSucceeded);
#if !UE_BUILD_SHIPPING
    J->SetStringField(TEXT("profile_slot"),PS?PS->GetProfileSlotForValidation():TEXT(""));
#endif
    if(PS){J->SetStringField(TEXT("last_notice"),PS->LastNotice);J->SetNumberField(TEXT("gold"),PS->Gold);J->SetNumberField(TEXT("ore"),PS->Ore);}
    if(Hero)
    {
        J->SetNumberField(TEXT("life_state"),uint8(Hero->GetLifeState()));J->SetNumberField(TEXT("health"),Hero->GetHealth());J->SetNumberField(TEXT("max_health"),Hero->GetMaxHealth());J->SetNumberField(TEXT("downed_remaining"),Hero->GetDownedTimeRemaining());J->SetNumberField(TEXT("revive_progress"),Hero->GetReviveProgress());
        J->SetNumberField(TEXT("downed_count"),Hero->GetDownedCount());J->SetNumberField(TEXT("revives_performed"),Hero->GetRevivesPerformedCount());J->SetNumberField(TEXT("times_revived"),Hero->GetTimesRevivedCount());J->SetNumberField(TEXT("revive_cancels"),Hero->GetReviveCancelCount());J->SetBoolField(TEXT("has_revive_target"),Hero->GetReviveTarget()!=nullptr);J->SetBoolField(TEXT("has_reviver"),Hero->GetReviver()!=nullptr);J->SetStringField(TEXT("position"),Hero->GetActorLocation().ToString());
        J->SetStringField(TEXT("velocity"),Hero->GetVelocity().ToString());
        if(const AFableCharacter* Other=Partner())
        {
            J->SetStringField(TEXT("partner_position"),Other->GetActorLocation().ToString());
            J->SetNumberField(TEXT("partner_distance"),FVector::Dist2D(Hero->GetActorLocation(),Other->GetActorLocation()));
            J->SetNumberField(TEXT("partner_life_state"),uint8(Other->GetLifeState()));
        }
    }
    if(GS)
    {
        J->SetNumberField(TEXT("active_region"),uint8(GS->ActiveRegion));J->SetNumberField(TEXT("adventure_stage"),uint8(GS->AdventureStage));J->SetNumberField(TEXT("enemy_count"),EnemyCount());J->SetNumberField(TEXT("seal_mask"),GS->MoonSealMask);J->SetNumberField(TEXT("boss_health"),GS->BossHealth);J->SetBoolField(TEXT("recovery_pending"),GS->bPartyRecoveryPending);J->SetBoolField(TEXT("retry_available"),GS->bRetryAvailable);J->SetNumberField(TEXT("recovery_count"),GS->PartyRecoveryCount);J->SetNumberField(TEXT("encounter_serial"),GS->EncounterSerial);
    }
    int32 EnemyBoltCount=0;for(TActorIterator<AFableEnemyBolt> It(GetWorld());It;++It)++EnemyBoltCount;
    J->SetNumberField(TEXT("enemy_bolt_count"),EnemyBoltCount);
    J->SetNumberField(TEXT("final_quiet_seconds"),CompletionQuietSince>=0?FMath::Max(0.0,GetWorld()->GetTimeSeconds()-CompletionQuietSince):0.0);
    TSharedRef<FJsonObject> Flags=MakeShared<FJsonObject>();for(const auto& Item:Checks)Flags->SetBoolField(Item.Key,Item.Value);J->SetObjectField(TEXT("checks"),Flags);
    TArray<TSharedPtr<FJsonValue>> Shots;for(const FString& Path:Screenshots)Shots.Add(MakeShared<FJsonValueString>(Path));J->SetArrayField(TEXT("screenshots"),Shots);
    FString Text;
    if(!FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text)))return FString();
    return Text;
}
