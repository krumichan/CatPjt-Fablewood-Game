#include "FableOnlineValidationDriver.h"
#include "FableOnlineFlowSubsystem.h"
#include "FableGameInstance.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableInventorySnapshot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"
namespace {
TSharedPtr<FJsonObject> ReadOnline(const FString& Path)
{
    FString Text;TSharedPtr<FJsonObject> J;
    if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J))return nullptr;
    return J;
}
}
bool UFableOnlineValidationDriver::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    FString R;return FParse::Value(FCommandLine::Get(),TEXT("FableOnlineValidationRole="),R)&&(R==TEXT("server")||R==TEXT("client1")||R==TEXT("client2"));
#endif
}
void UFableOnlineValidationDriver::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);Started=StepAt=FPlatformTime::Seconds();
    FString Context;
    FParse::Value(FCommandLine::Get(),TEXT("FableOnlineValidationRole="),Role);
    if(!FParse::Value(FCommandLine::Get(),TEXT("FableOnlineValidationDirectory="),Directory)||FPaths::IsRelative(Directory))return;
    IFileManager::Get().MakeDirectory(*Directory,true);
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UFableOnlineValidationDriver::Tick),.1f);
    if(Role!=TEXT("server"))
    {
        TSharedPtr<FJsonObject> J;
        if(!FParse::Value(FCommandLine::Get(),TEXT("FableOnlineValidationContext="),Context)||(J=ReadOnline(Context))==nullptr){Finish(false,TEXT("Private validation context unavailable."));return;}
        const TSharedPtr<FJsonObject>* Accounts=nullptr;const TSharedPtr<FJsonObject>* Account=nullptr;
        if(!J->TryGetStringField(TEXT("mode"),Mode)||!J->TryGetObjectField(TEXT("accounts"),Accounts)||!(*Accounts)->TryGetObjectField(Role,Account)
            ||!(*Account)->TryGetStringField(TEXT("name"),AccountName)||!(*Account)->TryGetStringField(TEXT("password"),Password))
        {Finish(false,TEXT("Invalid private validation context; values suppressed."));return;}
        InitialClass=(*Account)->GetIntegerField(TEXT("initialClass"));
        const TSharedPtr<FJsonObject>* E=nullptr;if((*Account)->TryGetObjectField(TEXT("expected"),E))Expected=*E;
        J->TryGetBoolField(TEXT("offlineFailure"),bOfflineFailure);FString Crash;J->TryGetStringField(TEXT("crashRole"),Crash);bCrashRole=Role==Crash;
        if((Mode!=TEXT("register")&&Mode!=TEXT("login"))||InitialClass<0||InitialClass>2){Finish(false,TEXT("Invalid validation mode/class."));return;}
    }
}
void UFableOnlineValidationDriver::Deinitialize()
{
    FTSTicker::RemoveTicker(Ticker);ReleaseSkillInput();Password.Reset();Super::Deinitialize();
}
TSharedRef<FJsonObject> UFableOnlineValidationDriver::Profile(const AFablePlayerState* P) const
{
    auto J=MakeShared<FJsonObject>();if(!P)return J;
    J->SetStringField(TEXT("playerId"),P->GetBackendPlayerId());J->SetNumberField(TEXT("heroClass"),int32(P->HeroClass));
    J->SetNumberField(TEXT("level"),P->Level);J->SetNumberField(TEXT("experience"),P->Experience);J->SetNumberField(TEXT("classTier"),P->GetAdvancementTier());
    J->SetNumberField(TEXT("revision"),double(P->GetBackendRevision()));J->SetNumberField(TEXT("acknowledgedRevision"),double(P->GetBackendAcknowledgedRevision()));
    J->SetStringField(TEXT("profileHash"),P->GetBackendAcknowledgedProfileHash());J->SetBoolField(TEXT("ready"),P->IsBackendReady());J->SetBoolField(TEXT("savePending"),P->IsBackendSavePending());
    auto W=MakeShared<FJsonObject>();W->SetNumberField(TEXT("gold"),P->Gold);W->SetNumberField(TEXT("ore"),P->Ore);W->SetNumberField(TEXT("essence"),P->Essence);
    W->SetNumberField(TEXT("rootCores"),P->RootCores);W->SetNumberField(TEXT("moonCrystals"),P->MoonCrystals);W->SetNumberField(TEXT("healthPotions"),P->HealthPotions);W->SetNumberField(TEXT("manaPotions"),P->ManaPotions);J->SetObjectField(TEXT("wallet"),W);
    TArray<TSharedPtr<FJsonValue>> Items;
    for(int32 I=0;I<P->Inventory.Num();++I)Items.Add(MakeShared<FJsonValueObject>(FableInventorySnapshot::MakeItem(P->Inventory[I],0,I)));
    for(int32 I=0;I<P->StashItems.Num();++I)Items.Add(MakeShared<FJsonValueObject>(FableInventorySnapshot::MakeItem(P->StashItems[I],1,I)));
    J->SetArrayField(TEXT("items"),Items);J->SetNumberField(TEXT("bagCount"),P->Inventory.Num());J->SetNumberField(TEXT("stashCount"),P->StashItems.Num());
    if(const auto* Item=P->FindItem(P->EquippedWeaponId))J->SetStringField(TEXT("equippedWeaponInstanceId"),Item->InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
    if(const auto* Item=P->FindItem(P->EquippedArmorId))J->SetStringField(TEXT("equippedArmorInstanceId"),Item->InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
    return J;
}
void UFableOnlineValidationDriver::Capture(const FString& Name)
{
    const FString Path=FPaths::Combine(Directory,Role+TEXT("-")+Name+TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Path,true,false);Screenshots.Add(Path);
}
FString UFableOnlineValidationDriver::SerializeReport(const FString& Status,const FString& Reason) const
{
    if(Directory.IsEmpty())return FString();
    auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("role"),Role);J->SetStringField(TEXT("mode"),Mode);J->SetStringField(TEXT("status"),Status);J->SetStringField(TEXT("reason"),Reason);
    J->SetNumberField(TEXT("step"),Step);J->SetNumberField(TEXT("processId"),FPlatformProcess::GetCurrentProcessId());J->SetNumberField(TEXT("movementCm"),Movement);
    auto C=MakeShared<FJsonObject>();for(const auto& Pair:Checks)C->SetBoolField(Pair.Key,Pair.Value);J->SetObjectField(TEXT("checks"),C);
    TArray<TSharedPtr<FJsonValue>> Shots;for(const auto& Path:Screenshots)Shots.Add(MakeShared<FJsonValueString>(Path));J->SetArrayField(TEXT("screenshots"),Shots);
    UWorld* World=GetGameInstance()?GetGameInstance()->GetWorld():nullptr;
    J->SetStringField(TEXT("networkMode"),!World?TEXT("none"):World->GetNetMode()==NM_DedicatedServer?TEXT("dedicated"):World->GetNetMode()==NM_Client?TEXT("client"):TEXT("standalone"));
    int32 Local=0;TArray<TSharedPtr<FJsonValue>> Profiles;
    if(World)for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)if(auto* PC=It->Get()){if(PC->IsLocalController())++Local;if(auto* P=PC->GetPlayerState<AFablePlayerState>())Profiles.Add(MakeShared<FJsonValueObject>(Profile(P)));}
    J->SetNumberField(TEXT("localControllerCount"),Local);J->SetArrayField(TEXT("profiles"),Profiles);
    if(Role==TEXT("server"))
    {
        TArray<TSharedPtr<FJsonValue>> ObservedIds;
        for(const FString& Id:ServerObservedPlayerIds)ObservedIds.Add(MakeShared<FJsonValueString>(Id));
        J->SetArrayField(TEXT("serverObservedPlayerIds"),ObservedIds);
    }
    if(Checkpoint)J->SetObjectField(TEXT("checkpoint"),Checkpoint);
    J->SetBoolField(TEXT("report_snapshot_frozen"),Status==TEXT("passed")||Status==TEXT("failed"));
    if(!MenuProfileHash.IsEmpty()){J->SetStringField(TEXT("menuProfileHash"),MenuProfileHash);J->SetNumberField(TEXT("menuRevision"),double(MenuRevision));}
    if(auto* Flow=GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>())
    {
        J->SetStringField(TEXT("flowState"),Flow->GetStateName());J->SetStringField(TEXT("flowMessage"),Flow->GetMessage());J->SetBoolField(TEXT("hasSession"),Flow->HasSession());
        J->SetNumberField(TEXT("joinAttempts"),Flow->GetJoinAttemptCount());J->SetNumberField(TEXT("lastHttpStatus"),Flow->GetLastHttpStatus());
    }
    FString Text;if(!FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text)))return FString();return Text;
}
bool UFableOnlineValidationDriver::WriteReport(const FString& Bytes) const
{
    if(Bytes.IsEmpty()||Directory.IsEmpty())return false;
    const FString Path=FPaths::Combine(Directory,Role+TEXT(".json")),Temp=Path+TEXT(".writing");
    return FFileHelper::SaveStringToFile(Bytes,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        &&IFileManager::Get().Move(*Path,*Temp,true,true,false,true);
}
void UFableOnlineValidationDriver::Publish(const FString& Status,const FString& Reason)
{
    if(bDone||bTerminalPending)return;
    // A failed running observation is retried by the next ordinary report tick.
    WriteReport(SerializeReport(Status,Reason));
}
void UFableOnlineValidationDriver::ReleaseSkillInput()
{
    if(!bReleaseSkill)return;bReleaseSkill=false;
    if(auto* PC=GetGameInstance()?GetGameInstance()->GetFirstLocalPlayerController():nullptr)
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::K,IE_Released,0.f,0,FInputDeviceId::CreateFromInternalId(0)));
}
void UFableOnlineValidationDriver::Finish(bool Success,const FString& Reason)
{
    if(bDone||bTerminalPending)return;
    ReleaseSkillInput();Password.Reset();TerminalReason=Reason;bTerminalSuccess=Success;
    // Capture exactly once: retries never replace the final profile with a later observation.
    TerminalBytes=SerializeReport(Success?TEXT("passed"):TEXT("failed"),Reason);
    PublicationStarted=NextPublication=FPlatformTime::Seconds();bTerminalPending=true;PublishTerminal();
}
void UFableOnlineValidationDriver::PublishTerminal()
{
    if(!bTerminalPending||bDone)return;
    const double Now=FPlatformTime::Seconds();
    if(TerminalBytes.IsEmpty()||Now-PublicationStarted>=30)
    {
        bTerminalPending=false;bDone=true;bPublicationFailed=true;FinishedAt=Now;
        UE_LOG(LogTemp,Error,TEXT("FABLE_ONLINE_REPORT_IO_FAILED role=%s attempts=%d last_error=%u"),*Role,PublicationAttempts,PublicationLastError);
        return;
    }
    if(Now<NextPublication)return;
    ++PublicationAttempts;
    if(WriteReport(TerminalBytes))
    {
        bTerminalPending=false;bDone=true;FinishedAt=Now;
        UE_LOG(LogTemp,Display,TEXT("FABLE_ONLINE_VALIDATION_RESULT role=%s status=%s attempts=%d"),*Role,bTerminalSuccess?TEXT("passed"):TEXT("failed"),PublicationAttempts);
        return;
    }
    PublicationLastError=FPlatformMisc::GetLastError();NextPublication=Now+.2;
    if(PublicationAttempts==1||PublicationAttempts%25==0)
        UE_LOG(LogTemp,Warning,TEXT("FABLE_ONLINE_REPORT_RETRY role=%s attempts=%d last_error=%u"),*Role,PublicationAttempts,PublicationLastError);
}
bool UFableOnlineValidationDriver::Tick(float Delta)
{
    const double Now=FPlatformTime::Seconds();
    if(bDone){if(Now-FinishedAt>2)FPlatformMisc::RequestExitWithStatus(false,bPublicationFailed?1:0);return true;}
    if(bTerminalPending){PublishTerminal();return true;}
    ReleaseSkillInput();
    auto* GI=GetGameInstance();UWorld* World=GI?GI->GetWorld():nullptr;if(!World)return true;
    if(Role==TEXT("server"))
    {
        // Retain one simultaneous server observation, even after both clients leave.
        // Client PlayerArray counts alone cannot prove two distinct authenticated owners.
        if(ServerObservedPlayerIds.IsEmpty()&&World->GetNetMode()==NM_DedicatedServer)
        {
            int32 Controllers=0,LocalControllers=0;
            TSet<FString> ReadyIds;
            for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
            {
                if(const auto* ObservedPC=It->Get())
                {
                    ++Controllers;
                    if(ObservedPC->IsLocalController())++LocalControllers;
                    if(const auto* ObservedState=ObservedPC->GetPlayerState<AFablePlayerState>();
                        ObservedState&&ObservedState->IsBackendReady()&&!ObservedState->GetBackendPlayerId().IsEmpty())
                        ReadyIds.Add(ObservedState->GetBackendPlayerId());
                }
            }
            if(Controllers==2&&LocalControllers==0&&ReadyIds.Num()==2)
            {
                ServerObservedPlayerIds=ReadyIds.Array();ServerObservedPlayerIds.Sort();
                Checks.Add(TEXT("server_observed_distinct_ready_players"),true);
            }
        }
        if(Now-LastReport>1){Publish(TEXT("running"));LastReport=Now;}
        auto Control=ReadOnline(FPaths::Combine(Directory,TEXT("control.json")));bool Quit=false;
        if(Control&&Control->TryGetBoolField(TEXT("quitServer"),Quit)&&Quit)FGenericPlatformMisc::RequestExit(false);
        return true;
    }
    auto* Flow=GI->GetSubsystem<UFableOnlineFlowSubsystem>();if(!Flow||!Flow->IsEnabled())return true;
    if(Now-Started>420){Finish(false,TEXT("Online workflow timed out; private context suppressed."));return true;}
    if(Now-LastReport>.5){Publish(bCrashRole&&Step==30?TEXT("crash-ready"):TEXT("running"));LastReport=Now;}
    auto Advance=[this,Now](int32 Next){Step=Next;StepAt=Now;LastAction=-1;};
    auto* PC=Cast<AFablePlayerController>(GI->GetFirstLocalPlayerController());auto* P=PC?PC->GetPlayerState<AFablePlayerState>():nullptr;auto* Hero=PC?Cast<AFableCharacter>(PC->GetPawn()):nullptr;
    const bool Ready=P&&P->IsBackendReady()&&!P->IsBackendSavePending()&&P->bLastSaveSucceeded&&P->GetBackendRevision()==P->GetBackendAcknowledgedRevision();
    switch(Step)
    {
    case 0:
        if(Now-StepAt<1)return true;
        if(LastAction<0){Capture(TEXT("login"));LastAction=Now;return true;}
        if(Now-LastAction<.5)return true;
        if(!Flow->Authenticate(AccountName,Password+TEXT("_wrong"))){Finish(false,TEXT("Wrong-password UI submission refused."));return true;}
        Advance(1);break;
    case 1:
        if(Flow->GetState()!=EFableOnlineState::Error)return true;
        if(Flow->GetLastHttpStatus()!=401){Finish(false,TEXT("Wrong password did not produce HTTP401."));return true;}
        if(LastAction<0){Checks.Add(TEXT("wrong_password_rejected"),true);Capture(TEXT("login-error"));LastAction=Now;return true;}
        if(Now-LastAction<.5)return true;
        if(!(Mode==TEXT("register")?Flow->Register(AccountName,Password,static_cast<EFableClass>(InitialClass)):Flow->Authenticate(AccountName,Password)))
        {Finish(false,TEXT("Correct account UI submission refused."));return true;}
        Password.Reset();Advance(2);break;
    case 2:
        if(Flow->GetState()==EFableOnlineState::Error){Finish(false,TEXT("Correct account authentication failed."));return true;}
        if(Flow->GetState()!=EFableOnlineState::Character)return true;
        if(Flow->GetCharacter().PlayerId.IsEmpty()||Flow->GetServers().IsEmpty()){Finish(false,TEXT("Character/server list missing."));return true;}
        if(Mode==TEXT("register")&&(!Flow->GetCharacter().bNewCharacter||Flow->GetCharacter().Level!=1||int32(Flow->GetCharacter().HeroClass)!=InitialClass)){Finish(false,TEXT("New character preview is incorrect."));return true;}
        if(Expected&&(Flow->GetCharacter().PlayerId!=Expected->GetStringField(TEXT("playerId"))||Flow->GetCharacter().ProfileHash!=Expected->GetStringField(TEXT("profileHash")))){Finish(false,TEXT("Existing character preview does not match durable checkpoint."));return true;}
        Checks.Add(TEXT("character_preview"),true);Capture(TEXT("character"));Flow->SelectServer(Flow->GetServers()[0].Id);Advance(3);break;
    case 3:
        if(Now-StepAt<1)return true;Flow->JoinSelectedServer();Advance(bOfflineFailure?4:6);break;
    case 4:
        if(Flow->GetState()!=EFableOnlineState::Disconnected&&Flow->GetState()!=EFableOnlineState::Error)return true;
        if(!Flow->HasSession()){Finish(false,TEXT("Travel failure discarded the authenticated session."));return true;}
        Checks.Add(TEXT("connection_failure_visible"),true);Capture(TEXT("connection-error"));Advance(5);Publish(TEXT("waiting-server"));break;
    case 5:
    {
        auto Control=ReadOnline(FPaths::Combine(Directory,TEXT("control.json")));bool Join=false;if(!Control||!Control->TryGetBoolField(TEXT("allowJoin"),Join)||!Join)return true;
        // A failed network travel first returns to the local lobby. Do not treat
        // its expected in-flight map transition as a rejected user retry.
        if(const auto* FableGI=Cast<UFableGameInstance>(GI);FableGI&&FableGI->IsTravelInProgress())return true;
        if(!Flow->Retry()){Finish(false,TEXT("Connection retry refused."));return true;}Advance(6);break;
    }
    case 6:
        if(Flow->GetState()==EFableOnlineState::Error){Finish(false,TEXT("Online entry failed after server ready."));return true;}
        if(Flow->GetState()!=EFableOnlineState::Playing||!Ready||!Hero)return true;
        if(P->GetBackendPlayerId()!=Flow->GetSessionPlayerId()){Finish(false,TEXT("Wrong character owner entered the world."));return true;}
        if(!World->GetGameState()||World->GetGameState()->PlayerArray.Num()!=2)return true;
        Checks.Add(TEXT("two_distinct_remote_players"),true);MovementStart=Hero->GetActorLocation();BeforeManaPotions=P->ManaPotions;Advance(7);break;
    case 7:
        if(!Hero||!Ready)return true;
        Movement=FMath::Max(Movement,FVector::Dist2D(MovementStart,Hero->GetActorLocation()));
        if(Movement<65){Hero->AddMovementInput(FVector(0,-1,0),1);return true;}
        Hero->GetCharacterMovement()->StopMovementImmediately();Checks.Add(TEXT("normal_movement"),true);
        if(Mode==TEXT("login")){Checkpoint=Profile(P);Advance(30);break;}
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::K,IE_Pressed,1.f,0,FInputDeviceId::CreateFromInternalId(0)));
        bReleaseSkill=true;bSkillSent=true;Advance(8);break;
    case 8:
        if(!Hero||!P||!Ready)return true;
        if(LastAction>=0)
        {
            if(P->ManaPotions==BeforeManaPotions-1&&Ready){Checks.Add(TEXT("normal_potion_saved_once"),true);Checkpoint=Profile(P);Capture(TEXT("playing"));Advance(30);}
            return true;
        }
        if(Hero->GetMana()>=Hero->GetMaxMana()-1)return true;
        PC->UIUseManaPotion();LastAction=Now;break;
    case 30:
        if(bCrashRole)return true;
        if(Now-StepAt<2)return true;
        if(!Flow->RequestLeave()){Finish(false,TEXT("Safe leave UI request refused."));return true;}Advance(31);break;
    case 31:
        if(Flow->GetState()==EFableOnlineState::Error||Flow->GetState()==EFableOnlineState::Disconnected){Finish(false,TEXT("Safe leave failed; no save success claimed."));return true;}
        if(Flow->GetState()!=EFableOnlineState::Character)return true;
        if(!Checkpoint){Finish(false,TEXT("Acknowledged gameplay checkpoint missing."));return true;}
        {
            const auto& C=Flow->GetCharacter();
            const auto Wallet=Checkpoint->GetObjectField(TEXT("wallet"));
            if(C.PlayerId!=Checkpoint->GetStringField(TEXT("playerId"))||int32(C.HeroClass)!=Checkpoint->GetIntegerField(TEXT("heroClass"))
                ||C.Level!=Checkpoint->GetIntegerField(TEXT("level"))||C.Experience!=Checkpoint->GetIntegerField(TEXT("experience"))
                ||C.ClassTier!=Checkpoint->GetIntegerField(TEXT("classTier"))||C.Gold!=Wallet->GetIntegerField(TEXT("gold"))
                ||C.BagCount!=Checkpoint->GetIntegerField(TEXT("bagCount"))||C.StashCount!=Checkpoint->GetIntegerField(TEXT("stashCount"))
                ||C.Revision<Checkpoint->GetNumberField(TEXT("acknowledgedRevision"))||C.ProfileHash.IsEmpty()||!C.bHasSummary)
            {Finish(false,TEXT("Character screen differs semantically after acknowledged leave."));return true;}
            MenuProfileHash=C.ProfileHash;MenuRevision=C.Revision;
        }
        Checks.Add(TEXT("save_release_ack_before_menu"),true);Capture(TEXT("saved-character"));Advance(32);break;
    case 32:
        if(Now-StepAt<1)return true;Flow->Logout();Advance(33);break;
    case 33:
        if(Flow->GetState()!=EFableOnlineState::SignedOut)return true;
        if(Flow->HasSession()){Finish(false,TEXT("Logout left a local access token."));return true;}
        Checks.Add(TEXT("logout_clears_session"),true);Capture(TEXT("logged-out"));Advance(34);break;
    case 34:
        if(Now-StepAt>2)Finish(true,TEXT("Account UI, character, online entry and acknowledged leave workflow completed."));break;
    default:Finish(false,TEXT("Unknown online validation step."));break;
    }
    return true;
}