#include "FableFrontEndValidationDriver.h"
#include "FableGameInstance.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableGameState.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

#if !UE_BUILD_SHIPPING
namespace
{
// A development-only observer survives map travel without altering game state.
int32 FrontStep=0;
double FrontStarted=0,FrontStepAt=0;
bool FrontDone=false,NewGameChecked=false,ContinueChecked=false,PreservedChecked=false,InvalidJoinChecked=false;
FString FirstSlot,SecondSlot;
TArray<uint8> FirstBytes;
TArray<FString> FrontShots;
void AdvanceFront(int32 Step)
{
    FrontStep=Step;FrontStepAt=FPlatformTime::Seconds();
    UE_LOG(LogTemp,Display,TEXT("FABLE_FRONTEND_VALIDATION step=%d"),Step);
}
}
#endif
UFableFrontEndValidationDriver::UFableFrontEndValidationDriver(){PrimaryComponentTick.bCanEverTick=true;}
void UFableFrontEndValidationDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
#else
    if(!FParse::Value(FCommandLine::Get(),TEXT("FableFrontEndValidationRole="),Role) ||
       !FParse::Value(FCommandLine::Get(),TEXT("FableFrontEndValidationRun="),RunId) ||
       !FParse::Value(FCommandLine::Get(),TEXT("FableFrontEndValidationDirectory="),Directory) ||
       (Role!=TEXT("host")&&Role!=TEXT("guest")) || FPaths::IsRelative(Directory))
    {SetComponentTickEnabled(false);return;}
    if(FrontStarted==0)FrontStarted=FrontStepAt=FPlatformTime::Seconds();
    IFileManager::Get().MakeDirectory(*Directory,true);
#endif
}
void UFableFrontEndValidationDriver::Capture(const FString& Name)
{
#if !UE_BUILD_SHIPPING
    const FString Path=FPaths::Combine(Directory,Role+TEXT("-")+Name+TEXT(".png"));
    FrontShots.AddUnique(Path); FScreenshotRequest::RequestScreenshot(Path,true,false);
#endif
}
void UFableFrontEndValidationDriver::Report(const FString& Status,const FString& Reason)
{
#if !UE_BUILD_SHIPPING
    TSharedRef<FJsonObject> O=MakeShared<FJsonObject>();
    O->SetStringField(TEXT("run_id"),RunId);O->SetStringField(TEXT("role"),Role);O->SetStringField(TEXT("status"),Status);O->SetStringField(TEXT("reason"),Reason);
    O->SetNumberField(TEXT("step"),FrontStep);O->SetNumberField(TEXT("process_id"),FPlatformProcess::GetCurrentProcessId());
    O->SetNumberField(TEXT("player_count"),GetWorld()->GetGameState()?GetWorld()->GetGameState()->PlayerArray.Num():0);
    O->SetStringField(TEXT("network_mode"),GetWorld()->GetNetMode()==NM_ListenServer?TEXT("listen_server"):GetWorld()->GetNetMode()==NM_Client?TEXT("client"):TEXT("standalone"));
    O->SetBoolField(TEXT("new_game_checked"),NewGameChecked);O->SetBoolField(TEXT("continue_checked"),ContinueChecked);
    O->SetBoolField(TEXT("previous_save_preserved"),PreservedChecked);O->SetBoolField(TEXT("invalid_address_rejected"),InvalidJoinChecked);
    O->SetStringField(TEXT("first_slot"),FirstSlot);O->SetStringField(TEXT("second_slot"),SecondSlot);
    TArray<TSharedPtr<FJsonValue>> Shots;for(const FString& S:FrontShots)Shots.Add(MakeShared<FJsonValueString>(S));O->SetArrayField(TEXT("screenshots"),Shots);
    FString Json;const auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(O,Writer);
    const FString Path=FPaths::Combine(Directory,Role+TEXT(".json"));
    if(FFileHelper::SaveStringToFile(Json,*(Path+TEXT(".tmp"))))IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true,true);
#endif
}
void UFableFrontEndValidationDriver::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(DeltaTime,TickType,Function);
#if !UE_BUILD_SHIPPING
    if(FrontDone)return;
    AFablePlayerController* PC=Cast<AFablePlayerController>(GetOwner());
    UFableGameInstance* GI=GetWorld()->GetGameInstance<UFableGameInstance>();
    if(!PC||!GI)return;
    const double Now=FPlatformTime::Seconds(),Wait=Now-FrontStepAt;
    const auto Fail=[&](const FString& Why){FrontDone=true;Report(TEXT("failed"),Why);};
    if(Now-FrontStarted>240){Fail(TEXT("Frontend validation timed out."));return;}
    AFablePlayerState* PS=PC->GetPlayerState<AFablePlayerState>();
    AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    const auto SlotPath=[](const FString& Slot){return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SaveGames"),Slot+TEXT(".sav"));};
    if(Role==TEXT("host"))
    {
        switch(FrontStep)
        {
        case 0:
            if(Wait>3 && PC->IsFrontEndOpen()){Capture(TEXT("start-menu"));AdvanceFront(1);}break;
        case 1:
            if(Wait>1){PC->UISetInitialClass(EFableClass::Mage);PC->UIStartNewGame(true);AdvanceFront(2);}break;
        case 2:
            if(Wait>3 && !PC->IsFrontEndOpen() && PS && PS->bProfileLoaded)
            {
                if(PS->HeroClass!=EFableClass::Mage||!PS->bLastSaveSucceeded){Fail(TEXT("New game did not select and save the mage."));return;}
                FirstSlot=PS->GetProfileSlotForValidation();
                if(!FirstSlot.StartsWith(TEXT("Fablewood_RPG_FrontTest_"))||!FFileHelper::LoadFileToArray(FirstBytes,*SlotPath(FirstSlot)))
                {Fail(TEXT("New profile was not isolated or saved."));return;}
                NewGameChecked=true;Capture(TEXT("new-mage"));AdvanceFront(3);
            }break;
        case 3:if(Wait>1){GI->ReturnToFrontEnd();AdvanceFront(4);}break;
        case 4:if(Wait>2 && PC->IsFrontEndOpen()){PC->UISetInitialClass(EFableClass::Archer);PC->UIStartNewGame(true);AdvanceFront(5);}break;
        case 5:
            if(Wait>3 && !PC->IsFrontEndOpen() && PS && PS->bProfileLoaded)
            {
                SecondSlot=PS->GetProfileSlotForValidation();TArray<uint8> Existing;
                PreservedChecked=FirstSlot!=SecondSlot && FFileHelper::LoadFileToArray(Existing,*SlotPath(FirstSlot)) && Existing==FirstBytes;
                if(!PreservedChecked||PS->HeroClass!=EFableClass::Archer){Fail(TEXT("Second new game replaced the first profile or chose the wrong class."));return;}
                GI->ReturnToFrontEnd();AdvanceFront(6);
            }break;
        case 6:if(Wait>2 && PC->IsFrontEndOpen()){Capture(TEXT("continue-menu"));AdvanceFront(7);}break;
        case 7:if(Wait>1){PC->UIContinueGame(true);AdvanceFront(8);}break;
        case 8:
            if(Wait>3 && !PC->IsFrontEndOpen() && PS && PS->bProfileLoaded)
            {
                ContinueChecked=PS->GetProfileSlotForValidation()==SecondSlot && PS->HeroClass==EFableClass::Archer;
                if(!ContinueChecked){Fail(TEXT("Continue loaded a different profile."));return;}
                Report(TEXT("waiting_for_guest"),TEXT("New, preserved, and continue profile checks passed."));AdvanceFront(9);
            }break;
        case 9:
            if(GS&&GS->PlayerArray.Num()==2){Capture(TEXT("coop-host"));AdvanceFront(10);}break;
        case 10:
            if(Wait>2){FrontDone=true;Report(TEXT("passed"),TEXT("New game, separate saves, continue, and menu guest join passed."));}break;
        }
    }
    else
    {
        switch(FrontStep)
        {
        case 0:
            if(Wait>3 && PC->IsFrontEndOpen()){Capture(TEXT("join-menu"));PC->UISetJoinAddress(TEXT("127.0.0.1?bad=1"));PC->UIJoinGame();AdvanceFront(1);}break;
        case 1:
            if(Wait>1 && PC->IsFrontEndOpen())
            {
                InvalidJoinChecked=!GI->GetFrontEndError().IsEmpty();
                if(!InvalidJoinChecked){Fail(TEXT("Invalid join address was not rejected."));return;}
                AdvanceFront(2);
            }break;
        case 2:
        {
            FString HostJson;TSharedPtr<FJsonObject> HostReport;
            if(FFileHelper::LoadFileToString(HostJson,*FPaths::Combine(Directory,TEXT("host.json"))) &&
               FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(HostJson),HostReport) && HostReport.IsValid() &&
               HostReport->GetStringField(TEXT("status"))==TEXT("waiting_for_guest"))
            {FString Address=TEXT("127.0.0.1:7788");FParse::Value(FCommandLine::Get(),TEXT("FableFrontEndValidationAddress="),Address);PC->UISetJoinAddress(Address);PC->UIJoinGame();AdvanceFront(3);}
            break;
        }
        case 3:
            if(Wait>2 && GetWorld()->GetNetMode()==NM_Client && GS && GS->PlayerArray.Num()==2 && PS && PS->bProfileLoaded)
            {Capture(TEXT("coop-guest"));AdvanceFront(4);}break;
        case 4:
            if(Wait>2){FrontDone=true;Report(TEXT("passed"),TEXT("Invalid address rejected and menu join reached the listen server."));}break;
        }
    }
#endif
}
