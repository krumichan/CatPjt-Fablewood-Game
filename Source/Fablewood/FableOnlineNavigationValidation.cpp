#include "FableOnlineValidationDriver.h"

#include "FableCharacter.h"
#include "FablePartyManager.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableTownCatalog.h"
#include "FableTownNavigation.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"

namespace
{
    /** 公開座標だけを有限数として記録し、要求・権限を生成しない。 */
    TArray<TSharedPtr<FJsonValue>> NavigationVector(const FVector& Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
                MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** 正本の物理文脈は保存プロフィールへ混ぜず、観測欄だけへ記録する。 */
    TSharedRef<FJsonObject> NavigationPhysical(const FFableTownLocationContext& Physical)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetBoolField(TEXT("valid"), Physical.IsValid());
        Row->SetNumberField(TEXT("areaKind"), int32(Physical.AreaKind));
        Row->SetNumberField(TEXT("townId"), int32(Physical.TownId));
        Row->SetStringField(TEXT("floorId"), Physical.FloorId.ToString());
        Row->SetStringField(TEXT("connectorId"), Physical.ConnectorId.ToString());
        Row->SetNumberField(TEXT("catalogVersion"), Physical.CatalogVersion);
        Row->SetBoolField(TEXT("geometryReady"), Physical.bGeometryReady);
        Row->SetArrayField(TEXT("localPosition"), NavigationVector(Physical.LocalPosition));
        return Row;
    }
}

/** 現在の通常UIが読む案内と同じ照会結果を、操作せず保存する。 */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::NavigationObservation(const AFablePlayerController* PC) const
{
    auto Row = MakeShared<FJsonObject>();
    const auto* Hero = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;
    const auto* State = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
    const auto* Catalog = FableTownNavigation::CatalogFor(Hero);
    const FVector Position = Hero ? Hero->GetActorLocation() : FVector::ZeroVector;
    FFableTownLocationContext Physical;
    const bool Resolved = Hero && Catalog && Catalog->ResolveLocation(Position, Physical, false);

    Row->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
    Row->SetStringField(TEXT("networkMode"), PC && PC->HasAuthority() ? TEXT("authority") : TEXT("client"));
    Row->SetStringField(TEXT("playerKey"), State ? State->PartyPlayerKey : FString());
    Row->SetBoolField(TEXT("hasLivePawn"), Hero != nullptr);
    Row->SetBoolField(TEXT("physicalResolved"), Resolved);
    Row->SetArrayField(TEXT("worldPosition"), NavigationVector(Position));
    Row->SetObjectField(TEXT("physical"), NavigationPhysical(Physical));

    TArray<TSharedPtr<FJsonValue>> Services;
    for (const auto Id : {EFableTownServiceId::QuestGuide, EFableTownServiceId::Workshop, EFableTownServiceId::Merchant,
                          EFableTownServiceId::ExpeditionPortal})
    {
        FFableTownServiceDescriptor Descriptor;
        const auto Status = FableTownNavigation::ResolveService(Catalog, Position, Id, Descriptor);
        auto Service = MakeShared<FJsonObject>();
        Service->SetNumberField(TEXT("serviceId"), int32(Id));
        Service->SetNumberField(TEXT("status"), int32(Status));
        Service->SetNumberField(TEXT("townId"), int32(Descriptor.Service.TownId));
        Service->SetNumberField(TEXT("catalogVersion"), Descriptor.Service.CatalogVersion);
        Service->SetStringField(TEXT("anchorId"), Descriptor.Anchor.AnchorId.ToString());
        Service->SetStringField(TEXT("floorId"), Descriptor.FloorId.ToString());
        Service->SetArrayField(TEXT("worldTarget"), NavigationVector(Descriptor.WorldLocation));
        Services.Add(MakeShared<FJsonValueObject>(Service));
    }
    Row->SetArrayField(TEXT("services"), Services);

    FVector StoryTarget = FVector::ZeroVector, NavigationTarget = FVector::ZeroVector;
    FString StoryLabel, NavigationLabel;
    EFableRegion StoryRegion = EFableRegion::Village;
    const bool HasStory = State && State->GetStoryTarget(StoryTarget, StoryLabel, &StoryRegion);
    const bool HasNavigation = PC && PC->GetNavigationTarget(NavigationTarget, NavigationLabel);
    Row->SetBoolField(TEXT("hasStoryTarget"), HasStory);
    Row->SetNumberField(TEXT("storyRegion"), int32(StoryRegion));
    Row->SetArrayField(TEXT("storyTarget"), NavigationVector(StoryTarget));
    Row->SetStringField(TEXT("storyLabel"), StoryLabel);
    Row->SetBoolField(TEXT("hasNavigationTarget"), HasNavigation);
    Row->SetArrayField(TEXT("navigationTarget"), NavigationVector(NavigationTarget));
    Row->SetStringField(TEXT("navigationLabel"), NavigationLabel);
    return Row;
}

/** 一つのサーバー時点で名簿・現接続・物理床・出発判定を結合する。 */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::PartySpatialObservation(UWorld* World,
                                                                              const FFablePartyRecord& Party) const
{
    auto Row = MakeShared<FJsonObject>();
    const auto* Manager = AFablePartyManager::Find(World);
    Row->SetStringField(TEXT("partyId"), Party.PartyId.ToString(EGuidFormats::DigitsWithHyphensLower));
    Row->SetNumberField(TEXT("serverProcessId"), FPlatformProcess::GetCurrentProcessId());
    Row->SetNumberField(TEXT("serverSeconds"), World ? World->GetTimeSeconds() : 0);
    Row->SetNumberField(TEXT("revision"), Party.Revision);
    Row->SetNumberField(TEXT("preparationRevision"), Party.PreparationRevision);
    Row->SetNumberField(TEXT("selectedRegion"), int32(Party.SelectedRegion));
    Row->SetNumberField(TEXT("selectedDifficulty"), int32(Party.SelectedDifficulty));
    Row->SetStringField(TEXT("retrySourceExpeditionId"),
                        Party.RetrySourceExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower));
    TArray<TSharedPtr<FJsonValue>> Members;

    for (const auto& Member : Party.Members)
    {
        APlayerController* Connection = Manager ? Manager->GetConnectedPlayer(Member.PlayerKey) : nullptr;
        const AFablePlayerState* State = Connection ? Connection->GetPlayerState<AFablePlayerState>() : nullptr;

        FFablePartyDepartureContext Context;
        const bool Eligible = Manager && Manager->ResolveDepartureContext(Connection, Party, Context);
        auto Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("playerKey"), Member.PlayerKey);
        Value->SetNumberField(TEXT("playerId"), Member.PlayerId);
        Value->SetNumberField(TEXT("readyRevision"), Member.ReadyRevision);
        Value->SetBoolField(TEXT("connected"), Member.bConnected);
        Value->SetBoolField(TEXT("liveConnection"), Connection != nullptr);
        Value->SetBoolField(TEXT("remoteConnection"), Connection && !Connection->IsLocalController());
        Value->SetBoolField(TEXT("currentOwner"),
                            State && State->GetOwner() == Connection && State->GetPlayerId() == Member.PlayerId);
        const auto* Hero = Connection ? Cast<AFableCharacter>(Connection->GetPawn()) : nullptr;
        Value->SetBoolField(TEXT("alive"), Hero && Hero->IsAlive());
        Value->SetBoolField(TEXT("backendReady"), State && State->IsBackendReady());
        Value->SetBoolField(TEXT("savePending"), State && State->IsBackendSavePending());
        Value->SetBoolField(TEXT("lastSaveSucceeded"), State && State->bLastSaveSucceeded);
        Value->SetNumberField(TEXT("backendRevision"), State ? State->GetBackendRevision() : -1);
        Value->SetNumberField(TEXT("acknowledgedRevision"), State ? State->GetBackendAcknowledgedRevision() : -1);
        Value->SetBoolField(TEXT("questEligible"), State && State->IsExpeditionEligible(Party.SelectedRegion));
        Value->SetStringField(TEXT("statePartyId"),
                              State ? State->PartyId.ToString(EGuidFormats::DigitsWithHyphensLower) : FString());
        Value->SetBoolField(TEXT("eligible"), Eligible);
        Value->SetBoolField(TEXT("completedRetry"), Context.bCompletedRetry);
        Value->SetBoolField(TEXT("privateRetry"), Context.bPrivateRetry);
        Value->SetStringField(TEXT("rejectionReason"), Context.RejectionReason.ToString());
        Value->SetObjectField(TEXT("physical"), NavigationPhysical(Context.Physical));
        Value->SetNumberField(TEXT("portalTownId"), int32(Context.PortalAnchor.TownId));
        Value->SetStringField(TEXT("portalAnchorId"), Context.PortalAnchor.AnchorId.ToString());
        Value->SetNumberField(TEXT("portalCatalogVersion"), Context.PortalAnchor.CatalogVersion);
        Value->SetStringField(TEXT("activeExpeditionId"),
                              State ? State->ActiveExpeditionId.ToString(EGuidFormats::DigitsWithHyphensLower)
                                    : FString());
        Members.Add(MakeShared<FJsonValueObject>(Value));
    }
    Row->SetArrayField(TEXT("members"), Members);
    return Row;
}

/** 頻繁な座標変化を除き、接続・準備・遠征IDが変化した実観測だけを有界保存する。 */
void UFableOnlineValidationDriver::ObserveUnit5PartyServer(UWorld* World)
{
    auto* Manager = AFablePartyManager::Find(World);
    if (!Manager || !World || World->GetNetMode() != NM_DedicatedServer)
    {
        return;
    }
    if (Unit5ObservedPartyManager.Get() != Manager)
    {
        if (Unit5ObservedPartyManager.IsValid())
        {
            Unit5ObservedPartyManager->DepartureLocationsChecked.Remove(Unit5DepartureHandle);
        }
        Unit5ObservedPartyManager = Manager;
        Unit5DepartureHandle =
            Manager->DepartureLocationsChecked.AddUObject(this, &UFableOnlineValidationDriver::OnUnit5DepartureChecked);
    }
    for (const auto& Party : Manager->Parties)
    {
        if (Party.Members.Num() != 2)
        {
            continue;
        }
        const auto Row = PartySpatialObservation(World, Party);
        FString Signature = Party.PartyId.ToString() + FString::FromInt(Party.Revision) + TEXT("|") +
                            FString::FromInt(Party.PreparationRevision);
        for (const auto& Member : Row->GetArrayField(TEXT("members")))
        {
            const auto Value = Member->AsObject();
            Signature += TEXT("|") + Value->GetStringField(TEXT("playerKey")) + TEXT(":") +
                         FString::FromInt(Value->GetIntegerField(TEXT("readyRevision"))) + TEXT(":") +
                         (Value->GetBoolField(TEXT("liveConnection")) ? TEXT("1") : TEXT("0")) + TEXT(":") +
                         Value->GetStringField(TEXT("activeExpeditionId")) + TEXT(":") +
                         Value->GetStringField(TEXT("rejectionReason"));
        }
        if (!Unit5PartySignatures.Contains(Signature) && Unit5PartySamples.Num() < 256)
        {
            Unit5PartySignatures.Add(Signature);
            Row->SetNumberField(TEXT("sequence"), Unit5PartySamples.Num() + 1);
            Unit5PartySamples.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
}

/** 判定直後の同期通知だけを記録し、許可・準備・移動を変更しない。 */
void UFableOnlineValidationDriver::OnUnit5DepartureChecked(const FFablePartyRecord& Party,
                                                           const TArray<APlayerController*>& Players,
                                                           bool bSpatialAllowed)
{
    auto* Manager = Unit5ObservedPartyManager.Get();
    if (!Manager || Role != TEXT("server") || Scenario != TEXT("party-results") || Unit5DepartureChecks.Num() >= 128)
    {
        return;
    }
    const auto Row = PartySpatialObservation(Manager->GetWorld(), Party);
    Row->SetNumberField(TEXT("sequence"), Unit5DepartureChecks.Num() + 1);
    Row->SetBoolField(TEXT("spatialAllowed"), bSpatialAllowed);
    TArray<TSharedPtr<FJsonValue>> Keys;
    for (const auto* Player : Players)
    {
        const auto* State = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        Keys.Add(MakeShared<FJsonValueString>(State ? State->PartyPlayerKey : FString()));
    }
    Row->SetArrayField(TEXT("inputPlayerKeys"), Keys);
    Unit5DepartureChecks.Add(MakeShared<FJsonValueObject>(Row));
}
