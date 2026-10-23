#include "FableDedicatedValidationDriver.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableInventorySnapshot.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableProjectile.h"
#include "FableEnemyBolt.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "TimerManager.h"

namespace
{
TSharedRef<FJsonObject> DedicatedWallet(const AFablePlayerState* P)
{
    auto W=MakeShared<FJsonObject>();W->SetNumberField(TEXT("gold"),P->Gold);W->SetNumberField(TEXT("ore"),P->Ore);
    W->SetNumberField(TEXT("essence"),P->Essence);W->SetNumberField(TEXT("rootCores"),P->RootCores);
    W->SetNumberField(TEXT("moonCrystals"),P->MoonCrystals);W->SetNumberField(TEXT("healthPotions"),P->HealthPotions);
    W->SetNumberField(TEXT("manaPotions"),P->ManaPotions);return W;
}
bool DedicatedCommitted(const AFablePlayerState* P)
{
    return P&&P->IsBackendReady()&&!P->IsBackendSavePending()&&P->bLastSaveSucceeded
        &&P->GetBackendRevision()==P->GetBackendAcknowledgedRevision();
}
int32 DedicatedItemIndex(const TArray<FFableItem>& Items,int32 Id)
{ return Items.IndexOfByPredicate([Id](const FFableItem& Item){return Item.Id==Id;}); }
}

UFableDedicatedValidationDriver::UFableDedicatedValidationDriver() { PrimaryComponentTick.bCanEverTick=true; }
void UFableDedicatedValidationDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
#else
    bInventoryValidation=FParse::Param(FCommandLine::Get(),TEXT("FableInventoryValidation"));
    bServer=GetWorld()->GetNetMode()==NM_DedicatedServer;
    PC=Cast<AFablePlayerController>(GetOwner());
    if (bServer) Role=TEXT("server");
    else FParse::Value(FCommandLine::Get(),TEXT("FableDedicatedValidationRole="),Role);
    FParse::Value(FCommandLine::Get(),TEXT("FableDedicatedValidationRun="),RunId);
    FParse::Value(FCommandLine::Get(),TEXT("FableDedicatedValidationDirectory="),Directory);
    FParse::Value(FCommandLine::Get(),TEXT("FableDedicatedValidationPhase="),Phase);
    if (Phase.IsEmpty()) Phase=TEXT("fresh");
    if (RunId.IsEmpty() || FPaths::IsRelative(Directory) || Directory.IsEmpty()
        || (!bServer && (!PC.IsValid() || !PC->IsLocalController() || (Role!=TEXT("client1") && Role!=TEXT("client2")))))
    { SetComponentTickEnabled(false); return; }
    StartedAt=StepAt=GetWorld()->GetRealTimeSeconds();
#endif
}
void UFableDedicatedValidationDriver::Key(const FKey& Value)
{
    if (!PC.IsValid() || !PC->PlayerInput) return;
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,IE_Pressed,1.f,0,FInputDeviceId::CreateFromInternalId(0)));
    Releases.AddUnique(Value);
}
void UFableDedicatedValidationDriver::Hold(const FKey& Value,bool bHeld)
{
    if (PC.IsValid() && PC->PlayerInput && PC->IsInputKeyDown(Value)!=bHeld)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value,bHeld?IE_Pressed:IE_Released,bHeld?1.f:0.f,0,FInputDeviceId::CreateFromInternalId(0)));
}
void UFableDedicatedValidationDriver::Stop() { Hold(EKeys::W,false); Hold(EKeys::S,false); Hold(EKeys::E,false); }
bool UFableDedicatedValidationDriver::WalkTo(const FVector& Position,float Radius)
{
    const APawn* Pawn=PC.IsValid()?PC->GetPawn():nullptr;
    if (!Pawn) return false;
    const FVector Delta=Position-Pawn->GetActorLocation();
    Hold(EKeys::S,false);
    if (Delta.Size2D()<=Radius) { Hold(EKeys::W,false); return true; }
    PC->SetControlRotation(FRotator(-12,Delta.Rotation().Yaw,0));Hold(EKeys::W,true);return false;
}
void UFableDedicatedValidationDriver::Advance(int32 Value)
{
    Stop();Step=Value;StepAt=GetWorld()->GetRealTimeSeconds();LastAction=-10;QuietAt=-1;
    UE_LOG(LogTemp,Display,TEXT("FABLE_DEDICATED_VALIDATION role=%s phase=%s step=%d"),*Role,*Phase,Step);
}
int32 UFableDedicatedValidationDriver::LiveEnemies() const
{ int32 N=0;for(TActorIterator<AFableEnemy> It(GetWorld());It;++It) if(It->IsAlive()) ++N;return N; }
int32 UFableDedicatedValidationDriver::Projectiles() const
{ int32 N=0;for(TActorIterator<AFableProjectile> It(GetWorld());It;++It) ++N;for(TActorIterator<AFableEnemyBolt> It(GetWorld());It;++It) ++N;return N; }
TSharedPtr<FJsonObject> UFableDedicatedValidationDriver::ReadRole(const FString& OtherRole) const
{
    FString Text;TSharedPtr<FJsonObject> Result;
    if(FFileHelper::LoadFileToString(Text,*FPaths::Combine(Directory,Phase,OtherRole+TEXT(".json"))))
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Result);
    FString R,P,I;
    if(!Result.IsValid() || !Result->TryGetStringField(TEXT("run_id"),I) || I!=RunId
        || !Result->TryGetStringField(TEXT("phase"),P) || P!=Phase
        || !Result->TryGetStringField(TEXT("role"),R) || R!=OtherRole) return nullptr;
    return Result;
}
bool UFableDedicatedValidationDriver::PeerAtLeast(int32 Wanted)
{
    if(const TSharedPtr<FJsonObject> Peer=ReadRole(Role==TEXT("client1")?TEXT("client2"):TEXT("client1")))
    { double Value=0;if(Peer->TryGetNumberField(TEXT("step"),Value))PeerStepSeen=FMath::Max(PeerStepSeen,int32(Value)); }
    return PeerStepSeen>=Wanted;
}
FString UFableDedicatedValidationDriver::ProfileSignature(const AFablePlayerState* P) const
{
    if(!P)return TEXT("");
    FString S=FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d"),int32(P->HeroClass),P->Level,P->Experience,P->Gold,P->Ore,P->Essence,P->RootCores,P->MoonCrystals,P->EquippedWeaponId,P->EquippedArmorId,P->HealthPotions,P->ManaPotions,int32(P->ForestQuest),P->QuestKills,int32(P->DungeonQuest),P->DungeonCompletions,int32(P->MoonwellQuest),P->MoonwellCompletions,P->StoryChapter);
    S+=FString::Printf(TEXT("|story=%d,%d|talents=%s"),P->StoryObjectMask,P->StorySealMask,*P->GetTalentSignature());
    for(const FFableClassProgress& C:P->ClassProgress)
    {S+=FString::Printf(TEXT("|class=%d"),C.Tier);for(int32 R:C.Ranks) S+=TEXT(",")+FString::FromInt(R);}
    S+=TEXT("|inventory");for(const FFableItem& I:P->Inventory)S+=TEXT(";")+FableRPG::ItemPersistentSignature(I);
    S+=TEXT("|stash");for(const FFableItem& I:P->StashItems)S+=TEXT(";")+FableRPG::ItemPersistentSignature(I);
    return S;
}
TSharedRef<FJsonObject> UFableDedicatedValidationDriver::ProfileReport(const AFablePlayerState* P) const
{
    TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();if(!P)return O;
    O->SetStringField(TEXT("player_id"),P->GetBackendPlayerId());O->SetBoolField(TEXT("backend_ready"),P->IsBackendReady());
    O->SetBoolField(TEXT("save_pending"),P->IsBackendSavePending());O->SetBoolField(TEXT("save_succeeded"),P->bLastSaveSucceeded);
    O->SetNumberField(TEXT("revision"),double(P->GetBackendRevision()));O->SetNumberField(TEXT("acknowledged_revision"),double(P->GetBackendAcknowledgedRevision()));
    O->SetStringField(TEXT("profile_hash"),P->GetBackendAcknowledgedProfileHash());O->SetStringField(TEXT("profile_signature"),ProfileSignature(P));
    O->SetStringField(TEXT("local_profile_slot"),P->GetProfileSlotForValidation());
    O->SetNumberField(TEXT("class"),int32(P->HeroClass));O->SetNumberField(TEXT("forest_quest"),int32(P->ForestQuest));O->SetNumberField(TEXT("quest_kills"),P->QuestKills);
    O->SetNumberField(TEXT("health_potions"),P->HealthPotions);O->SetNumberField(TEXT("equipped_weapon"),P->EquippedWeaponId);O->SetNumberField(TEXT("inventory_count"),P->Inventory.Num());
    O->SetObjectField(TEXT("wallet"),DedicatedWallet(P));
    O->SetNumberField(TEXT("stash_count"),P->StashItems.Num());
    O->SetStringField(TEXT("last_notice"),P->LastNotice);O->SetNumberField(TEXT("notice_serial"),P->NoticeSerial);
    TArray<TSharedPtr<FJsonValue>> CentralItems;
    const auto Append=[&](const TArray<FFableItem>& Source,int32 Location)
    {
        for(int32 Position=0;Position<Source.Num();++Position)
        {
            const auto& Item=Source[Position];CentralItems.Add(MakeShared<FJsonValueObject>(FableInventorySnapshot::MakeItem(Item,Location,Position)));
            if(Location==0&&Item.Id==P->EquippedWeaponId)O->SetStringField(TEXT("equipped_weapon_instance_id"),Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
            if(Location==0&&Item.Id==P->EquippedArmorId)O->SetStringField(TEXT("equipped_armor_instance_id"),Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
        }
    };
    Append(P->Inventory,0);Append(P->StashItems,1);O->SetArrayField(TEXT("central_items"),CentralItems);
    TArray<TSharedPtr<FJsonValue>> Items;for(const FFableItem& I:P->Inventory)Items.Add(MakeShared<FJsonValueString>(FableRPG::ItemPersistentSignature(I)));O->SetArrayField(TEXT("inventory_signatures"),Items);
    if(const AFableCharacter* Hero=Cast<AFableCharacter>(P->GetPawn()))
    {
        O->SetNumberField(TEXT("health"),Hero->GetHealth());O->SetNumberField(TEXT("life_state"),int32(Hero->GetLifeState()));O->SetNumberField(TEXT("skill_casts"),Hero->GetSkillCastCount(0));
        const FVector Position=Hero->GetActorLocation(),Velocity=Hero->GetVelocity();
        auto Coordinates=MakeShared<FJsonObject>();Coordinates->SetNumberField(TEXT("x"),Position.X);Coordinates->SetNumberField(TEXT("y"),Position.Y);Coordinates->SetNumberField(TEXT("z"),Position.Z);
        O->SetObjectField(TEXT("position"),Coordinates);O->SetNumberField(TEXT("speed_cm_s"),Velocity.Size());
        O->SetNumberField(TEXT("forge_distance_cm"),FVector::Dist2D(Position,FableRPG::ForgeLocation()));
    }
    return O;
}
TSharedRef<FJsonObject> UFableDedicatedValidationDriver::MakeReport(const FString& Status,const FString& Reason) const
{
    TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();O->SetStringField(TEXT("run_id"),RunId);O->SetStringField(TEXT("phase"),Phase);O->SetStringField(TEXT("role"),Role);
    O->SetStringField(TEXT("status"),Status);O->SetStringField(TEXT("reason"),Reason);O->SetBoolField(TEXT("report_snapshot_frozen"),Status!=TEXT("running"));
    O->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());O->SetStringField(TEXT("network_mode"),GetWorld()->GetNetMode()==NM_DedicatedServer?TEXT("dedicated_server"):GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("unexpected"));
    O->SetBoolField(TEXT("inventory_validation"),bInventoryValidation);O->SetArrayField(TEXT("inventory_events"),InventoryEvents);
    O->SetStringField(TEXT("inventory_action"),InventoryAction);O->SetNumberField(TEXT("inventory_request_attempts"),InventoryRequestAttempts);
    O->SetArrayField(TEXT("inventory_request_observations"),InventoryRequestObservations);
    O->SetNumberField(TEXT("forge_stable_seconds"),ForgeStableAt<0?0:GetWorld()->GetRealTimeSeconds()-ForgeStableAt);
    O->SetNumberField(TEXT("step"),Step);O->SetNumberField(TEXT("enemy_count"),LiveEnemies());O->SetNumberField(TEXT("projectile_count"),Projectiles());
    O->SetNumberField(TEXT("movement_cm"),MaxMovement);O->SetNumberField(TEXT("remote_movement_cm"),RemoteMovement);O->SetNumberField(TEXT("attack_inputs"),AttackInputs);O->SetNumberField(TEXT("skill_inputs"),SkillInputs);
    O->SetNumberField(TEXT("quiet_seconds"),QuietAt<0?0:GetWorld()->GetRealTimeSeconds()-QuietAt);
    TSharedRef<FJsonObject> Flags=MakeShared<FJsonObject>();for(const auto& C:Checks)Flags->SetBoolField(C.Key,C.Value);O->SetObjectField(TEXT("checks"),Flags);
    TArray<TSharedPtr<FJsonValue>> Shots;for(const FString& S:Screenshots)Shots.Add(MakeShared<FJsonValueString>(S));O->SetArrayField(TEXT("screenshots"),Shots);
    if(!bServer && PC.IsValid())O->SetObjectField(TEXT("profile"),ProfileReport(PC->GetPlayerState<AFablePlayerState>()));
    int32 Count=0,Local=0;TArray<TSharedPtr<FJsonValue>> Players;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        APlayerController* C=It->Get();if(!C)continue;++Count;if(C->IsLocalController())++Local;
        if(bServer) if(const AFablePlayerState* P=C->GetPlayerState<AFablePlayerState>())
        {TSharedRef<FJsonObject> Entry=ProfileReport(P);Entry->SetNumberField(TEXT("movement_cm"),PlayerMovement.FindRef(P->GetBackendPlayerId()));Entry->SetBoolField(TEXT("incoming_damage_seen"),PlayerHitSeen.FindRef(P->GetBackendPlayerId()));Entry->SetBoolField(TEXT("potion_consumed_seen"),PlayerPotionSeen.FindRef(P->GetBackendPlayerId()));Players.Add(MakeShared<FJsonValueObject>(Entry));}
    }
    O->SetNumberField(TEXT("controller_count"),Count);O->SetNumberField(TEXT("local_controller_count"),Local);
    O->SetNumberField(TEXT("player_count"),GetWorld()->GetGameState()?GetWorld()->GetGameState()->PlayerArray.Num():0);
    O->SetArrayField(TEXT("players"),Players);O->SetBoolField(TEXT("enemy_damage_seen"),bEnemyDamageSeen);return O;
}
void UFableDedicatedValidationDriver::Capture(const FString& Name)
{
    if(bServer)return;const FString Path=FPaths::Combine(Directory,Phase,Role+TEXT("-")+Name+TEXT(".png"));Screenshots.AddUnique(Path);
    FTimerHandle Timer;GetWorld()->GetTimerManager().SetTimer(Timer,[Path](){FScreenshotRequest::RequestScreenshot(Path,true,false);},.35f,false);
}
void UFableDedicatedValidationDriver::WriteRunning()
{
    FString Text;FJsonSerializer::Serialize(MakeReport(TEXT("running"),TEXT("")),TJsonWriterFactory<>::Create(&Text));
    IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory,Phase),true);FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Directory,Phase,Role+TEXT(".json")));
}
void UFableDedicatedValidationDriver::Finish(bool bSuccess,const FString& Reason)
{
    if(bDone||bPending)return;Stop();bTerminalSuccess=bSuccess;TerminalReason=Reason;TerminalBytes.Reset();
    FJsonSerializer::Serialize(MakeReport(bSuccess?TEXT("passed"):TEXT("failed"),Reason),TJsonWriterFactory<>::Create(&TerminalBytes));
    bPending=true;PendingAt=FPlatformTime::Seconds();NextPublishAt=0;PublicationAttempts=0;PublishPending();
}
void UFableDedicatedValidationDriver::PublishPending()
{
    const double Now=FPlatformTime::Seconds();if(!bPending||Now<NextPublishAt)return;
    ++PublicationAttempts;NextPublishAt=Now+.2;IFileManager::Get().MakeDirectory(*FPaths::Combine(Directory,Phase),true);
    if(FFileHelper::SaveStringToFile(TerminalBytes,*FPaths::Combine(Directory,Phase,Role+TEXT(".json"))))
    {bPending=false;bDone=true;UE_LOG(LogTemp,Display,TEXT("FABLE_DEDICATED_VALIDATION_RESULT role=%s phase=%s status=%s attempts=%d"),*Role,*Phase,bTerminalSuccess?TEXT("passed"):TEXT("failed"),PublicationAttempts);}
    else if(Now-PendingAt>30)
    {bPending=false;bDone=true;UE_LOG(LogTemp,Error,TEXT("FABLE_DEDICATED_REPORT_IO_FAILED role=%s phase=%s attempts=%d"),*Role,*Phase,PublicationAttempts);}
}
void UFableDedicatedValidationDriver::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    // Control files only request harness lifecycle transitions, never game state changes.
    if(bDone&&!bPending&&!bQuitRequested)
    {
        FString Text;TSharedPtr<FJsonObject> Request;FString RequestedRun,RequestedPhase;
        const FString Name=bServer?TEXT("quit-server-request.json"):TEXT("quit-request.json");
        if(FFileHelper::LoadFileToString(Text,*FPaths::Combine(Directory,Phase,Name))
            &&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)&&Request.IsValid()
            &&Request->TryGetStringField(TEXT("run_id"),RequestedRun)&&RequestedRun==RunId
            &&Request->TryGetStringField(TEXT("phase"),RequestedPhase)&&RequestedPhase==Phase)
        {
            bQuitRequested=true;
            if(bServer)FGenericPlatformMisc::RequestExit(false);
            else if(PC.IsValid())PC->ConsoleCommand(TEXT("quit"));
            return;
        }
    }
    if(bServer&&GetWorld()->GetRealTimeSeconds()-LastConnectionReportAt>.5)
    {
        LastConnectionReportAt=GetWorld()->GetRealTimeSeconds();int32 Count=0,Local=0;
        for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
            if(APlayerController* C=It->Get()){++Count;if(C->IsLocalController())++Local;}
        TSharedRef<FJsonObject> State=MakeShared<FJsonObject>();State->SetStringField(TEXT("run_id"),RunId);State->SetStringField(TEXT("phase"),Phase);
        State->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());State->SetNumberField(TEXT("controller_count"),Count);State->SetNumberField(TEXT("local_controller_count"),Local);
        FString Text;FJsonSerializer::Serialize(State,TJsonWriterFactory<>::Create(&Text));
        IFileManager::Get().MakeDirectory(*Directory,true);FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Directory,TEXT("connection-state.json")));
    }
    if(bServer&&!bPending)
    {
        FString Text;TSharedPtr<FJsonObject> Request;FString RequestedRun,RequestedPhase;
        if(FFileHelper::LoadFileToString(Text,*FPaths::Combine(Directory,TEXT("phase-request.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Request)
            &&Request.IsValid()&&Request->TryGetStringField(TEXT("run_id"),RequestedRun)&&RequestedRun==RunId&&Request->TryGetStringField(TEXT("phase"),RequestedPhase)
            &&RequestedPhase!=Phase&&(RequestedPhase==TEXT("reconnect")||RequestedPhase==TEXT("resume")))
        {Phase=RequestedPhase;bDone=false;Step=0;PeerStepSeen=-1;Checks.Empty();QuietAt=-1;StartedAt=StepAt=GetWorld()->GetRealTimeSeconds();}
    }
    if(bPending){PublishPending();return;}if(bDone)return;
    if(PC.IsValid())for(const FKey& K:Releases)PC->InputKey(FInputKeyEventArgs::CreateSimulated(K,IE_Released,0,0,FInputDeviceId::CreateFromInternalId(0)));Releases.Empty();
    const double Now=GetWorld()->GetRealTimeSeconds();if(Now-StartedAt>600||(!bServer&&Now-StepAt>180)){Finish(false,TEXT("Dedicated validation timed out."));return;}
    if(bServer)TickServer();else TickClient();
    if(!bPending&&!bDone&&Now-LastReportAt>.5){WriteRunning();LastReportAt=Now;}
}
void UFableDedicatedValidationDriver::TickServer()
{
    int32 Count=0,Local=0;bool Ready=true;TSet<FString> Ids;
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
    {
        APlayerController* C=It->Get();if(!C)continue;++Count;if(C->IsLocalController())++Local;
        AFablePlayerState* P=C->GetPlayerState<AFablePlayerState>();AFableCharacter* H=C->GetPawn<AFableCharacter>();
        if(!P||!P->IsBackendReady()||!H){Ready=false;continue;}const FString Id=P->GetBackendPlayerId();
        if(Id.IsEmpty()||Ids.Contains(Id)){Finish(false,TEXT("Distinct backend identities were not maintained."));return;}Ids.Add(Id);
        if(!PlayerStarts.Contains(Id))PlayerStarts.Add(Id,H->GetActorLocation());
        PlayerMovement.FindOrAdd(Id)=FMath::Max(PlayerMovement.FindRef(Id),FVector::Dist2D(PlayerStarts[Id],H->GetActorLocation()));
        if(PlayerLastHealth.Contains(Id)&&H->GetHealth()<PlayerLastHealth[Id]-.5f)PlayerHitSeen.Add(Id,true);PlayerLastHealth.Add(Id,H->GetHealth());
        if(PlayerLastPotions.Contains(Id)&&P->HealthPotions<PlayerLastPotions[Id])PlayerPotionSeen.Add(Id,true);PlayerLastPotions.Add(Id,P->HealthPotions);
        Ready &=H->IsAlive()&&!P->IsBackendSavePending()&&P->bLastSaveSucceeded&&P->GetBackendRevision()==P->GetBackendAcknowledgedRevision();
    }
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)
    {const float Previous=EnemyHealth.Contains(*It)?EnemyHealth[*It]:It->GetMaxHealth();if(It->GetHealth()<Previous-.1f)bEnemyDamageSeen=true;EnemyHealth.Add(*It,It->GetHealth());}
    if(Local!=0){Finish(false,TEXT("Dedicated server unexpectedly owns a local player."));return;}
    if(Count!=2||!Ready||LiveEnemies()!=0||Projectiles()!=0){QuietAt=-1;return;}
    bool BothPassed=true;for(const FString R:{FString(TEXT("client1")),FString(TEXT("client2"))})
    {const TSharedPtr<FJsonObject> Report=ReadRole(R);FString Status;BothPassed &=Report.IsValid()&&Report->TryGetStringField(TEXT("status"),Status)&&Status==TEXT("passed");}
    if(!BothPassed){QuietAt=-1;return;}
    if(QuietAt<0)QuietAt=GetWorld()->GetRealTimeSeconds();if(GetWorld()->GetRealTimeSeconds()-QuietAt<1)return;
    if(Phase==TEXT("fresh"))
    {
        bool Observed=bEnemyDamageSeen;for(const FString& Id:Ids)Observed &= PlayerMovement.FindRef(Id)>200&&PlayerHitSeen.FindRef(Id)&&PlayerPotionSeen.FindRef(Id);
        if(!Observed){Finish(false,TEXT("Server did not observe movement, real combat and consumed potions for both identities."));return;}
    }
    Checks.Add(TEXT("dedicated_no_local_player"),true);Checks.Add(TEXT("distinct_profiles"),true);Checks.Add(TEXT("durable_acknowledged"),true);Finish(true,TEXT("Dedicated server observed two distinct committed profiles."));
}
void UFableDedicatedValidationDriver::Fight()
{
    AFableCharacter* H=PC->GetPawn<AFableCharacter>();if(!H||!H->IsAlive()){Stop();return;}AFableEnemy* Target=nullptr;float Distance=MAX_flt;
    for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)if(It->IsAlive()){const float D=FVector::Dist2D(H->GetActorLocation(),It->GetActorLocation());if(D<Distance){Distance=D;Target=*It;}}
    if(!Target){Stop();return;}PC->SetControlRotation(FRotator(-12,(Target->GetActorLocation()-H->GetActorLocation()).Rotation().Yaw,0));
    Hold(EKeys::W,Distance>620);Hold(EKeys::S,Distance<300);const double Now=GetWorld()->GetRealTimeSeconds();
    if(Now-LastAction<.68)return;
    if(Distance<850&&Now-LastSkill>6.5&&H->GetSkillCooldownRemaining(0)<=0&&H->GetMana()>=H->GetSkillManaCost(0))
    {Key(EKeys::K);LastSkill=LastAction=Now;++SkillInputs;return;}
    if(Distance<1050){Key(EKeys::J);LastAction=Now;++AttackInputs;}
}
void UFableDedicatedValidationDriver::TickClient()
{
    if(!PC.IsValid())return;AFablePlayerState* P=PC->GetPlayerState<AFablePlayerState>();AFableCharacter* H=PC->GetPawn<AFableCharacter>();AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    if(!P||!P->IsBackendReady()||!H||!GS||GS->PlayerArray.Num()!=2)return;
    const double Now=GetWorld()->GetRealTimeSeconds();const float Side=Role==TEXT("client1")?-1.f:1.f;
    if(GetWorld()->GetNetMode()!=NM_Client){Finish(false,TEXT("Validation role is not a remote client."));return;}
    if(Step>0)MaxMovement=FMath::Max(MaxMovement,FVector::Dist2D(InitialPosition,H->GetActorLocation()));
    for(TActorIterator<AFableCharacter> It(GetWorld());It;++It)if(*It!=H&&It->GetPlayerState())
    {if(!RemoteStarts.Contains(*It))RemoteStarts.Add(*It,It->GetActorLocation());RemoteMovement=FMath::Max(RemoteMovement,FVector::Dist2D(RemoteStarts[*It],It->GetActorLocation()));}
    if(!H->IsAlive()){Stop();Finish(false,TEXT("Dedicated combat validation participant became inactive."));return;}
    if(Phase!=TEXT("fresh"))
    {
        Stop();if(P->IsBackendSavePending()||!P->bLastSaveSucceeded||P->GetBackendRevision()!=P->GetBackendAcknowledgedRevision()){QuietAt=-1;return;}
        if(QuietAt<0){QuietAt=Now;PC->UIToggleInventory();Capture(TEXT("restored-profile"));}if(Now-QuietAt<2)return;
        Checks.Add(TEXT("runtime_restored"),true);Finish(true,TEXT("Remote client restored its backend profile."));return;
    }
    if(bInventoryValidation&&Step>=20){TickInventory(P,Now,Side);return;}
    switch(Step)
    {
    case 0:
        InitialPosition=H->GetActorLocation();Checks.Add(TEXT("authenticated_profile"),!P->GetBackendPlayerId().IsEmpty());Capture(TEXT("connected"));Advance(1);break;
    case 1:
        if(WalkTo(FVector(Side*420,Role==TEXT("client1")?-1100:-700,110),60))Advance(2);break;
    case 2:
        if(LastAction<0){BeforeSignature=ProfileSignature(P);BeforeNotice=P->NoticeSerial;PC->UIShopBuy(EFableShopOffer::HealthPotion);LastAction=Now;}
        if(Now-LastAction<1)return;
        if(P->NoticeSerial==BeforeNotice){LastAction=-10;return;}
        if(ProfileSignature(P)!=BeforeSignature){Finish(false,TEXT("Remote shop request changed profile."));return;}Checks.Add(TEXT("remote_shop_rejected"),true);Advance(3);break;
    case 3:
        if(WalkTo(FableRPG::BoardLocation()+FVector(Side*145,-180,110),65))Advance(4);break;
    case 4:
        if(LastAction<0){BeforeSignature=ProfileSignature(P);BeforeNotice=P->NoticeSerial;PC->UIEquip(-777);LastAction=Now;}
        if(Now-LastAction<1)return;if(P->NoticeSerial==BeforeNotice){LastAction=-10;return;}
        if(ProfileSignature(P)!=BeforeSignature){Finish(false,TEXT("Invalid equipment identity changed profile."));return;}Checks.Add(TEXT("invalid_item_rejected"),true);Advance(5);break;
    case 5:
        if(P->ForestQuest==EFableQuestStage::Active){PC->UIClosePanels();Advance(6);return;}
        if(Now-LastAction>.8){PC->UISelectQuest(EFableQuestId::Forest);PC->UIConfirmQuest();LastAction=Now;}break;
    case 6:
        if(!PeerAtLeast(6))return;Advance(7);break;
    case 7:
    {
        if(Checks.FindRef(TEXT("health_potion_used"))){Stop();if(PeerAtLeast(8))Advance(8);return;}
        if(BeforePotions>0&&P->HealthPotions==BeforePotions-1)
        {if(H->GetHealth()>BeforeHealth){Checks.Add(TEXT("health_potion_used"),true);Stop();Advance(8);}return;}
        if(H->GetHealth()<H->GetMaxHealth()-10)
        {
            Stop();if(Now-LastAction>.8){BeforePotions=P->HealthPotions;BeforeHealth=H->GetHealth();PC->UIUseHealthPotion();LastAction=Now;}return;
        }
        AFableEnemy* Target=nullptr;float Best=MAX_flt;for(TActorIterator<AFableEnemy> It(GetWorld());It;++It)if(It->IsAlive())
        {const float D=FVector::Dist2D(H->GetActorLocation(),It->GetActorLocation());if(D<Best){Target=*It;Best=D;}}
        if(Target)WalkTo(Target->GetActorLocation(),120);break;
    }
    case 8:
        if(!PeerAtLeast(8)){Stop();return;}
        if(LiveEnemies()==0&&P->ForestQuest==EFableQuestStage::Ready){Checks.Add(TEXT("real_combat"),AttackInputs>0&&SkillInputs>0&&H->GetSkillCastCount(0)>0);Capture(TEXT("forest-cleared"));Advance(9);return;}Fight();break;
    case 9:
        if(WalkTo(FableRPG::BoardLocation()+FVector(Side*145,-180,110),65)){BeforeGold=P->Gold;Advance(10);}break;
    case 10:
        if(P->ForestQuest==EFableQuestStage::Completed)
        {if(P->Gold!=BeforeGold+90){Finish(false,TEXT("Forest report did not grant exactly 90 gold."));return;}Checks.Add(TEXT("quest_gold_exact"),true);PC->UIClosePanels();Advance(11);return;}
        if(Now-LastAction>.8){PC->UISelectQuest(EFableQuestId::Forest);PC->UIConfirmQuest();LastAction=Now;}break;
    case 11:
        if(WalkTo(FableRPG::MerchantLocation()+FVector(Side*120,-130,110),65)){BeforeGold=P->Gold;BeforeItems=P->Inventory.Num();Advance(12);}break;
    case 12:
        if(P->Inventory.Num()==BeforeItems+1)
        {
            if(P->Gold!=BeforeGold-100){Finish(false,TEXT("Weapon purchase gold delta differed."));return;}
            NewItemId=P->Inventory.Last().Id;Checks.Add(TEXT("weapon_purchase_exact"),NewItemId>4);Capture(TEXT("shop-weapon"));Advance(13);return;
        }
        if(LastAction<0 && Now-StepAt>.3 && !P->IsBackendSavePending() && P->GetBackendRevision()==P->GetBackendAcknowledgedRevision()){PC->UIShopBuy(static_cast<EFableShopOffer>(2+int32(P->HeroClass)));LastAction=Now;}break;
    case 13:
        if(Now-StepAt<.7)return;
        if(LastAction<0 && !P->IsBackendSavePending() && P->GetBackendRevision()==P->GetBackendAcknowledgedRevision()){BeforeGold=P->Gold;BeforePotions=P->HealthPotions;PC->UIShopBuy(EFableShopOffer::HealthPotion);LastAction=Now;}
        if(P->HealthPotions==BeforePotions+1)
        {if(P->Gold!=BeforeGold-15){Finish(false,TEXT("Potion purchase gold delta differed."));return;}Checks.Add(TEXT("potion_purchase_exact"),true);Advance(14);}break;
    case 14:
        if(WalkTo(FableRPG::BoardLocation()+FVector(Side*145,-180,110),65))Advance(15);break;
    case 15:
        if(P->EquippedWeaponId==NewItemId){Checks.Add(TEXT("owned_weapon_equipped"),true);PC->UIClosePanels();Advance(bInventoryValidation&&!bInventoryFlowComplete?20:16);return;}
        if(Now-LastAction>.8){PC->UIEquip(NewItemId);LastAction=Now;}break;
    case 16:
        Stop();if(!PeerAtLeast(16)||LiveEnemies()!=0||Projectiles()!=0||H->GetBuildChangeCooldownRemaining()>0||P->IsBackendSavePending()
            ||!P->bLastSaveSucceeded||P->GetBackendRevision()!=P->GetBackendAcknowledgedRevision()){QuietAt=-1;return;}
        if(QuietAt<0){QuietAt=Now;PC->UIToggleInventory();Capture(TEXT("durable-profile"));}if(Now-QuietAt<6)return;
        Checks.Add(TEXT("movement_replicated"),MaxMovement>200&&RemoteMovement>100);Checks.Add(TEXT("durable_acknowledged"),true);Checks.Add(TEXT("safe_checkpoint"),true);
        for(const auto& C:Checks)if(!C.Value){Finish(false,TEXT("A required observed check is false."));return;}
        Finish(true,TEXT("Two remote clients completed normal-input combat and durable economy checks."));break;
    default:Finish(false,TEXT("Unknown dedicated validation step."));break;
    }
}


void UFableDedicatedValidationDriver::BeginInventoryEvent(const FString& Action,const AFablePlayerState* P,
    const FFableItem& Item,int32 Location,int32 Position,int32 GoldDelta,int32 OreDelta)
{
    InventoryAction=Action;InventoryItemBefore=Item;InventoryLocationBefore=Location;InventoryPositionBefore=Position;
    InventoryGoldDelta=GoldDelta;InventoryOreDelta=OreDelta;InventoryWalletBefore=DedicatedWallet(P);
    InventoryRevisionBefore=P->GetBackendAcknowledgedRevision();
    InventoryProfileBefore=ProfileSignature(P);InventoryProfileHashBefore=P->GetBackendAcknowledgedProfileHash();
    InventoryRequestAttempts=0;LastAction=GetWorld()->GetRealTimeSeconds();RecordInventoryAttempt(P);
}

void UFableDedicatedValidationDriver::RecordInventoryAttempt(const AFablePlayerState* P)
{
    ++InventoryRequestAttempts;InventoryLastAttemptAt=GetWorld()->GetRealTimeSeconds();
    auto Observation=MakeShared<FJsonObject>();Observation->SetStringField(TEXT("action"),InventoryAction);
    Observation->SetNumberField(TEXT("attempt"),InventoryRequestAttempts);Observation->SetNumberField(TEXT("revision"),double(P->GetBackendRevision()));
    Observation->SetNumberField(TEXT("acknowledged_revision"),double(P->GetBackendAcknowledgedRevision()));
    Observation->SetBoolField(TEXT("save_pending"),P->IsBackendSavePending());Observation->SetStringField(TEXT("last_notice"),P->LastNotice);
    Observation->SetNumberField(TEXT("notice_serial"),P->NoticeSerial);Observation->SetStringField(TEXT("at_utc"),FDateTime::UtcNow().ToIso8601());
    Observation->SetBoolField(TEXT("near_forge"),PC.IsValid()&&PC->IsNearForge());
    if(const APawn* Pawn=P->GetPawn())
    {
        const FVector Position=Pawn->GetActorLocation();auto Coordinates=MakeShared<FJsonObject>();
        Coordinates->SetNumberField(TEXT("x"),Position.X);Coordinates->SetNumberField(TEXT("y"),Position.Y);Coordinates->SetNumberField(TEXT("z"),Position.Z);
        Observation->SetObjectField(TEXT("position"),Coordinates);Observation->SetNumberField(TEXT("speed_cm_s"),Pawn->GetVelocity().Size());
        Observation->SetNumberField(TEXT("forge_distance_cm"),FVector::Dist2D(Position,FableRPG::ForgeLocation()));
    }
    InventoryRequestObservations.Add(MakeShared<FJsonValueObject>(Observation));
}

bool UFableDedicatedValidationDriver::CompleteInventoryEvent(const AFablePlayerState* P,const FFableItem* Item,int32 Location,int32 Position)
{
    if(!DedicatedCommitted(P)||P->GetBackendAcknowledgedRevision()<=InventoryRevisionBefore
        ||GetWorld()->GetRealTimeSeconds()-LastAction<.8)return false;
    const auto After=DedicatedWallet(P);bool Exact=InventoryWalletBefore.IsValid();
    if(Exact)for(const auto& Field:InventoryWalletBefore->Values)
    {
        double Expected=Field.Value->AsNumber();
        if(Field.Key==TEXT("gold"))Expected+=InventoryGoldDelta;
        if(Field.Key==TEXT("ore"))Expected+=InventoryOreDelta;
        Exact&=After->GetNumberField(Field.Key)==Expected;
    }
    if(InventoryAction==TEXT("sale"))Exact&=Item==nullptr;
    else
    {
        FFableItem Expected=InventoryItemBefore;
        if(InventoryAction==TEXT("repair"))Expected.Durability=Expected.MaxDurability;
        else if(InventoryAction==TEXT("enhance"))++Expected.Upgrade;
        Exact&=Item&&Item->InstanceId.IsValid()&&Item->InstanceId==Expected.InstanceId
            &&FableRPG::ItemPersistentSignature(*Item)==FableRPG::ItemPersistentSignature(Expected);
    }
    if(!Exact){Finish(false,TEXT("Inventory action changed unexpected item metadata or wallet fields: ")+InventoryAction);return false;}
    auto Event=MakeShared<FJsonObject>();Event->SetStringField(TEXT("action"),InventoryAction);
    Event->SetObjectField(TEXT("before_wallet"),InventoryWalletBefore);Event->SetObjectField(TEXT("after_wallet"),After);
    Event->SetObjectField(TEXT("item_before"),FableInventorySnapshot::MakeItem(InventoryItemBefore,InventoryLocationBefore,InventoryPositionBefore));
    if(Item)Event->SetObjectField(TEXT("item_after"),FableInventorySnapshot::MakeItem(*Item,Location,Position));
    else Event->SetField(TEXT("item_after"),MakeShared<FJsonValueNull>());
    Event->SetNumberField(TEXT("gold_delta"),InventoryGoldDelta);Event->SetNumberField(TEXT("ore_delta"),InventoryOreDelta);
    Event->SetNumberField(TEXT("request_attempts"),InventoryRequestAttempts);
    Event->SetNumberField(TEXT("revision_before"),double(InventoryRevisionBefore));
    Event->SetNumberField(TEXT("revision"),double(P->GetBackendRevision()));
    Event->SetNumberField(TEXT("acknowledged_revision"),double(P->GetBackendAcknowledgedRevision()));
    Event->SetStringField(TEXT("acknowledged_at_utc"),FDateTime::UtcNow().ToIso8601());
    if(const auto GS=GetWorld()->GetGameState())Event->SetNumberField(TEXT("acknowledged_at_server_time"),GS->GetServerWorldTimeSeconds());
    InventoryEvents.Add(MakeShared<FJsonValueObject>(Event));return true;
}

void UFableDedicatedValidationDriver::TickInventory(AFablePlayerState* P,double Now,float Side)
{
    if(LastAction>=0&&Now-LastAction>25&&!P->IsBackendSavePending())
    {Finish(false,FString::Printf(TEXT("Normal inventory UI request was not durably observed: %s; attempts=%d notice[%d]=%s."),*InventoryAction,InventoryRequestAttempts,P->NoticeSerial,*P->LastNotice));return;}
    const bool Ready=DedicatedCommitted(P);
    switch(Step)
    {
    case 20:
        PC->UIClosePanels();
        if(WalkTo(FableRPG::MerchantLocation()+FVector(Side*120,-130,110),65))Advance(21);
        break;
    case 21:
        Stop();
        if(LastAction<0)
        {
            if(!Ready||Now-StepAt<.4)return;
            int32 Index=INDEX_NONE;
            for(int32 I=0;I<P->Inventory.Num();++I)
            {
                const auto& Item=P->Inventory[I];
                if(Item.Id>4&&Item.Id!=NewItemId&&Item.Id!=P->EquippedWeaponId&&Item.Id!=P->EquippedArmorId&&P->GetSellPrice(Item.Id)>0){Index=I;break;}
            }
            if(Index==INDEX_NONE){Finish(false,TEXT("No owned unequipped forest reward can fund the inventory scenario."));return;}
            const FFableItem Item=P->Inventory[Index];BeginInventoryEvent(TEXT("sale"),P,Item,0,Index,P->GetSellPrice(Item.Id),0);
            PC->UISell(Item.Id);return;
        }
        if(!P->FindItem(InventoryItemBefore.Id)&&DedicatedItemIndex(P->StashItems,InventoryItemBefore.Id)==INDEX_NONE
            &&CompleteInventoryEvent(P,nullptr,-1,-1))
        {Checks.Add(TEXT("inventory_sale_exact"),true);Advance(22);}
        break;
    case 22:
    {
        // Approach south of the other player's final stop, then use separate clear lanes.
        // +/-140,-110 leaves capsule clearance from the forge's +/-85,+/-65 base.
        PC->UIClosePanels();
        if(!bForgeWaypointReached)
        {
            if(WalkTo(FableRPG::ForgeLocation()+FVector(Side*140,-300,110),30))bForgeWaypointReached=true;
            return;
        }
        if(!WalkTo(FableRPG::ForgeLocation()+FVector(Side*140,-110,110),30)){ForgeStableAt=-1;return;}
        Stop();const APawn* Pawn=PC->GetPawn();
        if(!Pawn||!PC->IsNearForge()||Pawn->GetVelocity().Size()>10){ForgeStableAt=-1;return;}
        if(ForgeStableAt<0)ForgeStableAt=Now;
        if(Now-ForgeStableAt<1)return;
        Advance(23);ForgeStableAt=-1;PC->UIOpenWorkshop();PC->UISelectWorkshopTab(EFableWorkshopTab::Repair);
        PC->UISelectWorkshopItem(int32(P->HeroClass)+1);Capture(TEXT("inventory-before-repair"));
        break;
    }
    case 23:
    {
        Stop();const int32 Id=int32(P->HeroClass)+1;const int32 Index=DedicatedItemIndex(P->Inventory,Id);
        if(Index==INDEX_NONE){Finish(false,TEXT("Original class starter weapon disappeared before repair."));return;}
        const FFableItem& Item=P->Inventory[Index];const APawn* Pawn=PC->GetPawn();
        const bool Positioned=Pawn&&PC->IsNearForge()&&Pawn->GetVelocity().Size()<=10
            &&FVector::Dist2D(Pawn->GetActorLocation(),FableRPG::ForgeLocation())<=230
            &&FMath::Abs(Pawn->GetActorLocation().Z-FableRPG::ForgeLocation().Z)<250;
        if(!Positioned)ForgeStableAt=-1;else if(ForgeStableAt<0)ForgeStableAt=Now;
        const bool Stable=Positioned&&ForgeStableAt>=0&&Now-ForgeStableAt>=1;
        if(LastAction<0)
        {
            if(!Ready||!Stable||Now-StepAt<1)return;
            const FFableResourceCost Cost=P->GetRepairCost(Id);
            if(Item.Durability>=Item.MaxDurability||Cost.Gold<=0||P->Gold<Cost.Gold+30||P->Ore<Cost.Ore+2)
            {Finish(false,FString::Printf(TEXT("Real starter wear or repair/enhance budget is unavailable: durability=%d/%d gold=%d repair=%d ore=%d repairOre=%d."),Item.Durability,Item.MaxDurability,P->Gold,Cost.Gold,P->Ore,Cost.Ore));return;}
            BeginInventoryEvent(TEXT("repair"),P,Item,0,Index,-Cost.Gold,-Cost.Ore);PC->UIRepair(Id);return;
        }
        if(Item.Durability==Item.MaxDurability)
        {
            if(CompleteInventoryEvent(P,&Item,0,Index))
            {Checks.Add(TEXT("inventory_repair_exact"),true);Capture(TEXT("inventory-after-repair"));Advance(24);}
            return;
        }
        // Never repeat a request with a changed or unacknowledged profile. Keep the
        // original event snapshot and deadline; only normal UI input is retried.
        if(!Ready||!Stable||Now-InventoryLastAttemptAt<2)return;
        bool Unchanged=P->GetBackendRevision()==InventoryRevisionBefore
            &&P->GetBackendAcknowledgedRevision()==InventoryRevisionBefore
            &&P->GetBackendAcknowledgedProfileHash()==InventoryProfileHashBefore
            &&ProfileSignature(P)==InventoryProfileBefore&&Index==InventoryPositionBefore
            &&Item.InstanceId==InventoryItemBefore.InstanceId
            &&FableRPG::ItemPersistentSignature(Item)==FableRPG::ItemPersistentSignature(InventoryItemBefore)
            &&InventoryWalletBefore.IsValid();
        const auto Wallet=DedicatedWallet(P);
        if(Unchanged)for(const auto& Field:InventoryWalletBefore->Values)
            Unchanged&=Wallet->GetNumberField(Field.Key)==Field.Value->AsNumber();
        if(!Unchanged)
        {Finish(false,TEXT("Repair was not observed but the committed snapshot changed; retry refused."));return;}
        if(InventoryRequestAttempts>=3)
        {Finish(false,FString::Printf(TEXT("Repair remained unchanged after three safe UI attempts; notice[%d]=%s."),P->NoticeSerial,*P->LastNotice));return;}
        RecordInventoryAttempt(P);PC->UIRepair(Id);
        break;
    }
    case 24:
    {
        Stop();const int32 Index=DedicatedItemIndex(P->Inventory,NewItemId);
        if(Index==INDEX_NONE){Finish(false,TEXT("Purchased weapon disappeared before enhancement."));return;}
        const FFableItem& Item=P->Inventory[Index];
        if(LastAction<0)
        {
            if(!Ready||Now-StepAt<1)return;
            if(Item.Upgrade!=0||P->Gold<30||P->Ore<2){Finish(false,TEXT("Purchased weapon upgrade or enhancement budget was unexpected."));return;}
            BeginInventoryEvent(TEXT("enhance"),P,Item,0,Index,-30,-2);PC->UIEnhance(NewItemId);return;
        }
        if(Item.Upgrade==1&&CompleteInventoryEvent(P,&Item,0,Index))
        {Checks.Add(TEXT("inventory_enhance_exact"),true);Advance(25);}
        break;
    }
    case 25:
        Stop();
        if(P->EquippedWeaponId==int32(P->HeroClass)+1&&Ready&&Now-StepAt>.4)
        {Advance(26);PC->UIOpenWorkshop();PC->UISelectWorkshopTab(EFableWorkshopTab::Stash);PC->UISetStashWithdrawal(false);PC->UISelectWorkshopItem(NewItemId);return;}
        if(LastAction<0&&Ready&&Now-StepAt>.4){InventoryAction=TEXT("equip_starter");PC->UIEquip(int32(P->HeroClass)+1);LastAction=Now;}
        break;
    case 26:
    case 28:
    {
        Stop();const int32 BagIndex=DedicatedItemIndex(P->Inventory,NewItemId);
        const int32 StashIndex=DedicatedItemIndex(P->StashItems,NewItemId);
        if(LastAction<0)
        {
            if(!Ready||Now-StepAt<.5)return;
            if(BagIndex==INDEX_NONE||StashIndex!=INDEX_NONE){Finish(false,TEXT("Deposit source or destination ownership is invalid."));return;}
            BeginInventoryEvent(TEXT("deposit"),P,P->Inventory[BagIndex],0,BagIndex,0,0);PC->UIDeposit(NewItemId);return;
        }
        if(BagIndex==INDEX_NONE&&StashIndex!=INDEX_NONE&&CompleteInventoryEvent(P,&P->StashItems[StashIndex],1,StashIndex))
        {
            Checks.Add(TEXT("inventory_deposit_preserved"),true);
            if(Step==26){Advance(27);PC->UISetStashWithdrawal(true);PC->UISelectWorkshopItem(NewItemId);}
            else {Advance(29);PC->UISetStashWithdrawal(true);PC->UISelectWorkshopItem(NewItemId);Capture(TEXT("inventory-stored-enhanced-weapon"));}
        }
        break;
    }
    case 27:
    {
        Stop();const int32 BagIndex=DedicatedItemIndex(P->Inventory,NewItemId);
        const int32 StashIndex=DedicatedItemIndex(P->StashItems,NewItemId);
        if(LastAction<0)
        {
            if(!Ready||Now-StepAt<.5)return;
            if(StashIndex==INDEX_NONE||BagIndex!=INDEX_NONE){Finish(false,TEXT("Withdrawal source or destination ownership is invalid."));return;}
            BeginInventoryEvent(TEXT("withdraw"),P,P->StashItems[StashIndex],1,StashIndex,0,0);PC->UIWithdraw(NewItemId);return;
        }
        if(StashIndex==INDEX_NONE&&BagIndex!=INDEX_NONE&&CompleteInventoryEvent(P,&P->Inventory[BagIndex],0,BagIndex))
        {Checks.Add(TEXT("inventory_withdraw_preserved"),true);Advance(28);PC->UISetStashWithdrawal(false);PC->UISelectWorkshopItem(NewItemId);}
        break;
    }
    case 29:
        Stop();if(!Ready||Now-StepAt<1)return;
        if(P->Inventory.Num()!=4||P->StashItems.Num()!=1||P->StashItems[0].Id!=NewItemId
            ||P->StashItems[0].Upgrade!=1||P->StashItems[0].Rarity!=1||!P->StashItems[0].InstanceId.IsValid()
            ||P->EquippedWeaponId!=int32(P->HeroClass)+1||InventoryEvents.Num()!=6)
        {Finish(false,TEXT("Final owned bag/stash/equipment or six-event history is inconsistent."));return;}
        Checks.Add(TEXT("inventory_final_stash"),true);bInventoryFlowComplete=true;PC->UIClosePanels();Advance(16);break;
    default:Finish(false,TEXT("Unknown opt-in inventory validation step."));break;
    }
}
