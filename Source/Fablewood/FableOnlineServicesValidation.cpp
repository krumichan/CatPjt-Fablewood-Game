#include "FableOnlineServicesValidation.h"
#include "FableOnlineValidationDriver.h"
#include "FableCharacter.h"
#include "FableGameMode.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** 공개 검증 폴더만 읽으며 계정/nonce 파일을 읽지 않는다. */
    TSharedPtr<FJsonObject> ReadServiceReport(const FString& Path)
    {
        FString Text;
        TSharedPtr<FJsonObject> J;
        if (!FFileHelper::LoadFileToString(Text, *Path) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J))
        {
            return nullptr;
        }
        return J;
    }

    /** 키 순서에 의존하지 않고 전체 JSON 값을 비교한다. / 全項目を比較する。 */
    bool SameServiceJson(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
    {
        return A && B && FJsonValue::CompareEqual(FJsonValueObject(A), FJsonValueObject(B));
    }

    /** 복제/ACK 메타와 체력은 제외하고 저장되는 경제·장비·퀘스트 값을 모두 보존한다. */
    TSharedRef<FJsonObject> ServiceSnapshotCore(const TSharedPtr<FJsonObject>& Profile)
    {
        auto J = MakeShared<FJsonObject>();
        if (Profile)
        {
            J->Values = Profile->Values;
        }
        for (const TCHAR* Field :
             {TEXT("playerId"), TEXT("revision"), TEXT("acknowledgedRevision"), TEXT("profileHash"), TEXT("ready"),
              TEXT("savePending"), TEXT("health"), TEXT("downedCount"), TEXT("timesRevived"), TEXT("revivesPerformed")})
        {
            J->RemoveField(Field);
        }
        return J;
    }

    /** 서버/클라이언트가 같은 revision의 실제 저장 ACK를 받았는지 확인한다. */
    bool ServiceSaved(const AFablePlayerState* P)
    {
        return P && P->IsBackendReady() && !P->IsBackendSavePending() && P->bLastSaveSucceeded &&
               P->GetBackendRevision() == P->GetBackendAcknowledgedRevision() &&
               P->GetBackendAcknowledgedProfileHash().Len() == 64;
    }

    /** 공개 월드 좌표만 기록해 서비스 범위 계산을 독립 재검증할 수 있게 한다. */
    TArray<TSharedPtr<FJsonValue>> ServicePosition(const FVector& Position)
    {
        return {MakeShared<FJsonValueNumber>(Position.X), MakeShared<FJsonValueNumber>(Position.Y),
                MakeShared<FJsonValueNumber>(Position.Z)};
    }

    /** 관측은 현재 인증 소유자의 실제 pawn만 허용한다. */
    bool ServiceOwner(const AFablePlayerController* PC)
    {
        const auto* P = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
        const auto* H = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;
        const auto* Towns = PC ? AFableTownManager::Find(PC->GetWorld()) : nullptr;
        FFableTownConnectionSnapshot Connection;
        return PC && PC->HasAuthority() && PC->GetWorld()->GetNetMode() == NM_DedicatedServer && P && H &&
               P->GetOwner() == PC && P->bProfileLoaded && H->IsAlive() && !P->IsProfileRestorePending() &&
               !P->IsTownPersistenceBlocked() && !P->HasPendingTownTravel() && !P->ActiveExpeditionId.IsValid() &&
               H->GetController() == PC && H->GetPlayerState<AFablePlayerState>() == P &&
               !P->GetBackendPlayerId().IsEmpty() && Towns && Towns->GetConnectionSnapshot(P, Connection) &&
               Towns->IsCurrentConnection(PC, Connection.OwnerConnectionGeneration, Connection.ConnectionNonce);
    }

    const TCHAR* ServiceLabels[] = {TEXT("guide-out-of-range-reject"),
                                    TEXT("guide-open"),
                                    TEXT("forest-accept"),
                                    TEXT("forest-three-kills"),
                                    TEXT("guide-turnin-open"),
                                    TEXT("forest-turnin"),
                                    TEXT("shop-wrong-service-reject"),
                                    TEXT("shop-open"),
                                    TEXT("shop-buy-one"),
                                    TEXT("forge-wrong-service-reject"),
                                    TEXT("forge-open"),
                                    TEXT("forge-upgrade-one"),
                                    TEXT("portal-open")};

    const TCHAR* ServiceOutcomes[] = {TEXT("authority-rejected"), TEXT("opened-ui"), TEXT("acknowledged-save"),
                                      TEXT("shared-forest-ack"),  TEXT("opened-ui"), TEXT("acknowledged-save"),
                                      TEXT("authority-rejected"), TEXT("opened-ui"), TEXT("acknowledged-save"),
                                      TEXT("authority-rejected"), TEXT("opened-ui"), TEXT("acknowledged-save"),
                                      TEXT("opened-ui")};
}

/** Shipping과 다른 scenario에서는 관측을 시작하지 않는다. / 他シナリオでは無効。 */
FFableScopedServiceValidationObservation::FFableScopedServiceValidationObservation(AFablePlayerController* Player,
                                                                                   const TCHAR* Action,
                                                                                   EFableTownServiceId Service)
{
#if !UE_BUILD_SHIPPING
    if (Player && Player->GetGameInstance())
    {
        Driver = Player->GetGameInstance()->GetSubsystem<UFableOnlineValidationDriver>();
        if (Driver.IsValid())
        {
            Observation = Driver->BeginServiceObservation(Player, Action, Service);
        }
    }
#endif
}

/** 정상 RPC 본문이 끝난 뒤 읽기 전용 순간값을 고정한다. */
FFableScopedServiceValidationObservation::~FFableScopedServiceValidationObservation()
{
    if (Driver.IsValid())
    {
        Driver->EndServiceObservation(Observation);
    }
}

/** 원래 gate를 다시 호출하지 않고 기존 성공 분기에서만 수신 사실을 기록한다. */
void FFableScopedServiceValidationObservation::AcceptRpgGate()
{
    if (Driver.IsValid())
    {
        Driver->AcceptServiceRpgGate(Observation);
    }
}

/** 원래 E 대상 선택 결과를 관측에 연결하며 선택 결과를 바꾸지 않는다. */
void FFableScopedServiceValidationObservation::SetService(EFableTownServiceId Service)
{
    if (Driver.IsValid())
    {
        Driver->SetObservedService(Observation, Service);
    }
}

/** UTC/이동/ACK bookkeeping과 경제·인벤토리·장비·퀘스트 정본을 구별한다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::ServiceProfile(const AFablePlayerState* P) const
{
    auto J = Profile(P);
    if (P)
    {
        J->SetNumberField(TEXT("forestKills"), P->QuestKills);
        J->SetNumberField(TEXT("moonwellQuest"), int32(P->MoonwellQuest));
        J->SetNumberField(TEXT("moonwellCompletions"), P->MoonwellCompletions);
        J->SetNumberField(TEXT("storyChapter"), P->StoryChapter);
        J->SetNumberField(TEXT("storyObjectMask"), P->StoryObjectMask);
        J->SetNumberField(TEXT("storySealMask"), P->StorySealMask);
    }
    return J;
}

/** 이동/UTC 저장 bookkeeping을 제외한 경제·장비·퀘스트 전체 핵심을 비교한다. */
TSharedRef<FJsonObject> UFableOnlineValidationDriver::ServiceCore(const AFablePlayerState* P) const
{
    return ServiceSnapshotCore(ServiceProfile(P));
}

/** 공개 QA 보고서의 계정 ID와 서버가 인증한 실제 소유자를 대조한다. */
FString UFableOnlineValidationDriver::ServiceRole(const AFablePlayerState* P) const
{
    if (!P)
    {
        return FString();
    }
    for (const TCHAR* Candidate : {TEXT("client1"), TEXT("client2")})
    {
        const auto Report = ReadServiceReport(FPaths::Combine(Directory, FString(Candidate) + TEXT(".json")));
        const TSharedPtr<FJsonObject>* Services = nullptr;
        FString Id;
        if (Report && Report->TryGetObjectField(TEXT("townServices"), Services) &&
            (*Services)->TryGetStringField(TEXT("ownerPlayerId"), Id) && Id == P->GetBackendPlayerId())
        {
            return Candidate;
        }
    }
    return FString();
}

/** 양쪽 현재 인증 소유자가 저장을 마친 상태에서만 RPC 전 스냅샷을 읽는다. */
int32 UFableOnlineValidationDriver::BeginServiceObservation(AFablePlayerController* PC, const TCHAR* Action,
                                                            EFableTownServiceId Service)
{
    if (Scenario != TEXT("unit4-legacy-services") || Role != TEXT("server") || !ServiceOwner(PC) ||
        ServiceObservations.Num() >= 128)
    {
        return INDEX_NONE;
    }
    const auto* P = PC->GetPlayerState<AFablePlayerState>();
    const FString OwnerRole = ServiceRole(P);
    if (OwnerRole.IsEmpty())
    {
        return INDEX_NONE;
    }
    auto Report = ReadServiceReport(FPaths::Combine(Directory, OwnerRole + TEXT(".json")));
    const TSharedPtr<FJsonObject>* Services = nullptr;
    FString Label;
    if (!Report || !Report->TryGetObjectField(TEXT("townServices"), Services) ||
        !(*Services)->TryGetStringField(TEXT("pendingLabel"), Label) || Label.IsEmpty())
    {
        return INDEX_NONE;
    }
    FServiceObservation O;
    O.Player = PC;
    O.Service = Service;
    O.Started = FPlatformTime::Seconds();
    for (TActorIterator<AFablePlayerState> It(PC->GetWorld()); It; ++It)
    {
        if (*It != P && !ServiceRole(*It).IsEmpty())
        {
            O.Peer = *It;
        }
    }
    if (!O.Peer.IsValid() || !ServiceOwner(Cast<AFablePlayerController>(O.Peer->GetOwner())) || !ServiceSaved(P) ||
        !ServiceSaved(O.Peer.Get()))
    {
        return INDEX_NONE;
    }
    O.Row = MakeShared<FJsonObject>();
    auto& J = O.Row;
    J->SetStringField(TEXT("observationId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
    J->SetNumberField(TEXT("sequence"), ++ServiceSequence);
    J->SetStringField(TEXT("label"), Label);
    J->SetStringField(TEXT("ownerRole"), OwnerRole);
    J->SetStringField(TEXT("ownerPlayerId"), P->GetBackendPlayerId());
    J->SetNumberField(TEXT("serverProcessId"), FPlatformProcess::GetCurrentProcessId());
    J->SetStringField(TEXT("networkMode"), TEXT("dedicated"));
    J->SetStringField(TEXT("action"), Action);
    J->SetBoolField(TEXT("ownerCurrent"), true);
    J->SetBoolField(TEXT("ownerCurrentBefore"), true);
    J->SetBoolField(TEXT("peerOwnerCurrent"), true);
    J->SetBoolField(TEXT("rpcReceived"), true);
    J->SetBoolField(TEXT("rpgGateAccepted"), false);
    J->SetBoolField(TEXT("noticeMatched"), false);
    J->SetBoolField(TEXT("peerUnchangedRequired"), true);
    J->SetNumberField(TEXT("noticeBefore"), P->NoticeSerial);
    J->SetObjectField(TEXT("before"), ServiceProfile(P));
    J->SetObjectField(TEXT("peerBefore"), ServiceProfile(O.Peer.Get()));
    J->SetObjectField(TEXT("ownerBeforeCore"), ServiceCore(P));
    J->SetObjectField(TEXT("peerBeforeCore"), ServiceCore(O.Peer.Get()));
    const int32 Index = ServiceObservations.Add(MoveTemp(O));
    SetObservedService(Index, Service);
    return Index;
}

/** 기존 AcceptRPGRequest 통과 사실만 보존한다. 권한이나 cooldown은 부여하지 않는다. */
void UFableOnlineValidationDriver::AcceptServiceRpgGate(int32 Index)
{
    if (ServiceObservations.IsValidIndex(Index))
    {
        ServiceObservations[Index].Row->SetBoolField(TEXT("rpgGateAccepted"), true);
    }
}

/** 원래 CanUse 호출 직전 같은 서버 정본을 읽기만 한다. */
void UFableOnlineValidationDriver::SetObservedService(int32 Index, EFableTownServiceId Service)
{
    if (!ServiceObservations.IsValidIndex(Index))
    {
        return;
    }
    auto& O = ServiceObservations[Index];
    O.Service = Service;
    auto* PC = O.Player.Get();
    const auto* H = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;
    const auto* Towns = PC ? AFableTownManager::Find(PC->GetWorld()) : nullptr;
    const auto* Mode = PC ? PC->GetWorld()->GetAuthGameMode<AFableGameMode>() : nullptr;
    FFableTownLocationContext Physical;
    FFableTownServiceRef Ref;
    FFableTownServiceDescriptor Descriptor;
    bool PhysicalValid = Towns && Towns->ResolvePhysicalContext(PC, Physical);
    if (PhysicalValid)
    {
        Ref.TownId = Physical.TownId;
        Ref.CatalogVersion = Physical.CatalogVersion;
        Ref.ServiceId = Service;
    }
    const bool Resolved =
        PhysicalValid && Towns->GetCatalog() && Towns->GetCatalog()->ResolveService(Ref, Descriptor, true);
    O.Row->SetNumberField(TEXT("serviceId"), int32(Service));
    O.Row->SetNumberField(TEXT("physicalTownId"), int32(Physical.TownId));
    O.Row->SetStringField(TEXT("physicalFloorId"), Physical.FloorId.ToString());
    O.Row->SetBoolField(TEXT("geometryReady"), Resolved && Physical.bGeometryReady && Descriptor.bGeometryReady);
    O.Row->SetArrayField(TEXT("pawnLocation"), ServicePosition(H ? H->GetActorLocation() : FVector::ZeroVector));
    O.Row->SetArrayField(TEXT("serviceLocation"), ServicePosition(Descriptor.WorldLocation));
    O.Row->SetNumberField(TEXT("interactionRadiusCm"), Descriptor.InteractionRadiusCm);
    O.Row->SetNumberField(TEXT("verticalRangeCm"), Descriptor.VerticalRangeCm);
    TArray<TSharedPtr<FJsonValue>> Nearby;
    if (H && Towns && Towns->GetCatalog())
    {
        for (const auto Candidate : {EFableTownServiceId::QuestGuide, EFableTownServiceId::Workshop,
                                     EFableTownServiceId::Merchant, EFableTownServiceId::ExpeditionPortal})
        {
            FFableTownServiceDescriptor NearbyDescriptor;
            if (Towns->GetCatalog()->ResolveNearbyService(H->GetActorLocation(), Candidate, NearbyDescriptor))
            {
                Nearby.Add(MakeShared<FJsonValueNumber>(int32(Candidate)));
            }
        }
    }
    O.Row->SetArrayField(TEXT("nearbyServiceIds"), Nearby);
    O.Row->SetBoolField(TEXT("grounded"), Resolved && H && H->IsGroundedAtCurrentTownAnchor(Descriptor.Anchor));
    O.Row->SetBoolField(TEXT("inRange"), Resolved && H && Descriptor.IsInRange(Physical, H->GetActorLocation()));
    O.Row->SetBoolField(TEXT("authorityAllowedBefore"), Mode && Mode->CanUseTownService(PC, Service));
}

/** 실제 거절 문구와 동기 변경을 저장 ACK 대기 전에 읽는다. */
void UFableOnlineValidationDriver::EndServiceObservation(int32 Index)
{
    if (!ServiceObservations.IsValidIndex(Index))
    {
        return;
    }
    auto& O = ServiceObservations[Index];
    auto* PC = O.Player.Get();
    const auto* P = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
    O.bEnded = true;
    O.Row->SetObjectField(TEXT("ownerAfterCallCore"), ServiceCore(P));
    O.Row->SetNumberField(TEXT("noticeAfter"), P ? P->NoticeSerial : -1);
    O.Row->SetStringField(TEXT("noticeTextAfter"), P ? P->LastNotice : FString());
    FString Notice;
    switch (O.Service)
    {
    case EFableTownServiceId::QuestGuide:
        Notice = TEXT("살아 있는 상태로 안내인 로완에게 의뢰를 받거나 보상을 받으세요.");
        break;
    case EFableTownServiceId::Workshop:
        Notice = TEXT("대장간에서 장비를 강화할 수 있습니다. I로 가방을 여세요.");
        break;
    case EFableTownServiceId::Merchant:
        Notice = TEXT("살아 있는 상태로 마을 상인에게 가까이 다가가세요.");
        break;
    default:
        break;
    }
    O.Row->SetBoolField(TEXT("noticeMatched"), P && !Notice.IsEmpty() && P->LastNotice == Notice &&
                                                   P->NoticeSerial > O.Row->GetIntegerField(TEXT("noticeBefore")));
    CompleteServiceObservation(Index);
}

/** 저장 대기는 원래 비동기 ACK를 관찰하며 실패/timeout을 성공으로 치환하지 않는다. */
void UFableOnlineValidationDriver::CompleteServiceObservation(int32 Index)
{
    auto& O = ServiceObservations[Index];
    if (!O.bEnded || O.bPublished)
    {
        return;
    }
    auto* PC = O.Player.Get();
    const auto* P = PC ? PC->GetPlayerState<AFablePlayerState>() : nullptr;
    const bool Expired = FPlatformTime::Seconds() - O.Started > 25;
    if (!Expired && P && P->IsBackendSavePending())
    {
        return;
    }
    auto& J = O.Row;
    const auto Before = J->GetObjectField(TEXT("before"));
    const auto After = ServiceProfile(P);
    const auto OwnerCore = ServiceCore(P);
    const auto PeerCore = ServiceCore(O.Peer.Get());
    J->SetObjectField(TEXT("after"), After);
    J->SetObjectField(TEXT("peerAfter"), ServiceProfile(O.Peer.Get()));
    J->SetObjectField(TEXT("ownerAfterCore"), OwnerCore);
    J->SetObjectField(TEXT("peerAfterCore"), PeerCore);
    const bool PeerSame = SameServiceJson(J->GetObjectField(TEXT("peerBeforeCore")), PeerCore);
    J->SetBoolField(TEXT("peerUnchanged"), PeerSame);
    J->SetBoolField(TEXT("ownerCurrent"), ServiceOwner(PC));
    const bool Changed = !SameServiceJson(J->GetObjectField(TEXT("ownerBeforeCore")), OwnerCore);
    const bool PeerCurrent = O.Peer.IsValid() && ServiceOwner(Cast<AFablePlayerController>(O.Peer->GetOwner()));
    J->SetBoolField(TEXT("peerOwnerCurrent"), PeerCurrent);
    const bool Stable = ServiceSaved(P) && ServiceSaved(O.Peer.Get()) && ServiceOwner(PC) && PeerCurrent && PeerSame &&
                        SameServiceJson(J->GetObjectField(TEXT("ownerAfterCallCore")), OwnerCore);
    const bool RevisionSame = P && P->GetBackendRevision() == Before->GetNumberField(TEXT("revision")) &&
                              P->GetBackendAcknowledgedProfileHash() == Before->GetStringField(TEXT("profileHash"));
    FString Terminal = TEXT("failed-observation");
    if (!Expired && Stable && J->GetBoolField(TEXT("rpgGateAccepted")))
    {
        if (!J->GetBoolField(TEXT("authorityAllowedBefore")) && !Changed && RevisionSame &&
            J->GetBoolField(TEXT("noticeMatched")))
        {
            Terminal = TEXT("authority-rejected");
        }
        else if (J->GetBoolField(TEXT("authorityAllowedBefore")) && J->GetBoolField(TEXT("grounded")) &&
                 J->GetBoolField(TEXT("inRange")))
        {
            if (J->GetStringField(TEXT("action")) == TEXT("interact") && !Changed && RevisionSame)
            {
                Terminal = TEXT("dialogue-authorized");
            }
            else if (Changed && P->GetBackendRevision() == Before->GetNumberField(TEXT("revision")) + 1 &&
                     P->GetBackendAcknowledgedProfileHash() != Before->GetStringField(TEXT("profileHash")))
            {
                Terminal = TEXT("acknowledged-save");
            }
        }
    }
    J->SetStringField(TEXT("terminal"), Terminal);
    O.bPublished = true;
    ServiceServerEvents.Add(MakeShared<FJsonValueObject>(J));
}

/** A/B 장벽은 입력만 순서화하며 실제 권한/재화 변경은 기존 RPC에 맡긴다. */
void UFableOnlineValidationDriver::ObserveUnit4ServicesServer(UWorld* World)
{
    if (!World || World->GetNetMode() != NM_DedicatedServer)
    {
        return;
    }
    if (!ServiceServerReport)
    {
        ServiceServerReport = MakeShared<FJsonObject>();
        ServiceServerReport->SetBoolField(TEXT("actualAirborneNegativeClaimed"), false);
        ServiceServerReport->SetStringField(TEXT("scope"), TEXT("LegacyVillage services only; four towns unadopted"));
    }
    for (int32 I = 0; I < ServiceObservations.Num(); ++I)
    {
        CompleteServiceObservation(I);
    }
    AFablePlayerState* Players[2] = {nullptr, nullptr};
    TSharedPtr<FJsonObject> LocalForestBefore[2];
    bool CapturedForestBefore[2] = {false, false};
    int32 Steps[2] = {0, 0};
    for (TActorIterator<AFablePlayerState> It(World); It; ++It)
    {
        const FString R = ServiceRole(*It);
        const int32 I = R == TEXT("client1") ? 0 : R == TEXT("client2") ? 1 : INDEX_NONE;
        if (I != INDEX_NONE)
        {
            Players[I] = *It;
            const auto Report = ReadServiceReport(FPaths::Combine(Directory, R + TEXT(".json")));
            const TSharedPtr<FJsonObject>* Services = nullptr;
            if (Report && Report->TryGetObjectField(TEXT("townServices"), Services))
            {
                Steps[I] = (*Services)->GetIntegerField(TEXT("localStep"));
                (*Services)->TryGetBoolField(TEXT("forestBeforeCaptured"), CapturedForestBefore[I]);
                const TSharedPtr<FJsonObject>* LocalBefore = nullptr;
                if ((*Services)->TryGetObjectField(TEXT("forestBefore"), LocalBefore))
                {
                    LocalForestBefore[I] = *LocalBefore;
                }
            }
        }
    }
    if (Players[0] && Players[1] && ServiceSaved(Players[0]) && ServiceSaved(Players[1]))
    {
        if ((ServicePhase == 0 && Steps[0] >= 2) || (ServicePhase == 1 && Steps[1] >= 2) ||
            (ServicePhase == 2 && Steps[0] >= 3) || (ServicePhase == 3 && Steps[1] >= 3) ||
            (ServicePhase == 5 && Steps[0] >= 6) || (ServicePhase == 6 && Steps[1] >= 6) ||
            (ServicePhase == 7 && Steps[0] >= 13) || (ServicePhase == 8 && Steps[1] >= 13))
        {
            ++ServicePhase;
            if (ServicePhase == 4)
            {
                ServiceForestBefore[0] = ServiceProfile(Players[0]);
                ServiceForestBefore[1] = ServiceProfile(Players[1]);
                ServiceForestStartedAt = World->GetTimeSeconds();
            }
        }
        if (ServicePhase == 4 && !bServiceForestReleased && CapturedForestBefore[0] && CapturedForestBefore[1])
        {
            bool SameBefore = true;
            for (int32 I = 0; I < 2; ++I)
            {
                const auto Local = LocalForestBefore[I];
                const auto Authority = ServiceForestBefore[I];
                SameBefore &=
                    Local && Authority && SameServiceJson(ServiceSnapshotCore(Local), ServiceSnapshotCore(Authority)) &&
                    Local->GetStringField(TEXT("playerId")) == Authority->GetStringField(TEXT("playerId")) &&
                    Local->GetStringField(TEXT("profileHash")) == Authority->GetStringField(TEXT("profileHash")) &&
                    Local->GetNumberField(TEXT("revision")) == Authority->GetNumberField(TEXT("revision")) &&
                    Local->GetNumberField(TEXT("acknowledgedRevision")) ==
                        Authority->GetNumberField(TEXT("acknowledgedRevision"));
            }
            if (SameBefore)
            {
                bServiceForestReleased = true;
                ServiceForestReleasedAt = World->GetTimeSeconds();
            }
            else
            {
                bServiceForestCaptureFailed = true;
            }
        }
        if (ServicePhase == 4 && bServiceForestReleased && Players[0]->ForestQuest == EFableQuestStage::Ready &&
            Players[1]->ForestQuest == EFableQuestStage::Ready && Players[0]->QuestKills == 3 &&
            Players[1]->QuestKills == 3 && Players[0]->Gold == 45 && Players[1]->Gold == 45 && Players[0]->Ore == 3 &&
            Players[1]->Ore == 3)
        {
            for (int32 I = 0; I < 2; ++I)
            {
                auto J = MakeShared<FJsonObject>();
                J->SetStringField(TEXT("observationId"),
                                  FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
                J->SetNumberField(TEXT("sequence"), ++ServiceSequence);
                J->SetStringField(TEXT("label"), TEXT("forest-three-kills"));
                J->SetStringField(TEXT("ownerRole"), I == 0 ? TEXT("client1") : TEXT("client2"));
                J->SetStringField(TEXT("ownerPlayerId"), Players[I]->GetBackendPlayerId());
                J->SetStringField(TEXT("action"), TEXT("forest"));
                J->SetNumberField(TEXT("forestStartedAtServerSeconds"), ServiceForestStartedAt);
                J->SetNumberField(TEXT("forestCombatReleasedAtServerSeconds"), ServiceForestReleasedAt);
                J->SetNumberField(TEXT("forestEndedAtServerSeconds"), World->GetTimeSeconds());
                J->SetBoolField(TEXT("forestLocalSnapshotsValidated"), true);
                J->SetStringField(TEXT("networkMode"), TEXT("dedicated"));
                J->SetNumberField(TEXT("serverProcessId"), FPlatformProcess::GetCurrentProcessId());
                J->SetBoolField(TEXT("ownerCurrent"),
                                ServiceOwner(Cast<AFablePlayerController>(Players[I]->GetOwner())));
                J->SetBoolField(TEXT("ownerCurrentBefore"), true);
                J->SetBoolField(TEXT("peerOwnerCurrent"),
                                ServiceOwner(Cast<AFablePlayerController>(Players[1 - I]->GetOwner())));
                J->SetBoolField(TEXT("rpcReceived"), false);
                J->SetBoolField(TEXT("rpgGateAccepted"), false);
                J->SetBoolField(TEXT("grounded"), false);
                J->SetBoolField(TEXT("inRange"), false);
                J->SetBoolField(TEXT("authorityAllowedBefore"), false);
                J->SetBoolField(TEXT("noticeMatched"), false);
                J->SetNumberField(TEXT("noticeBefore"), 0);
                J->SetNumberField(TEXT("noticeAfter"), Players[I]->NoticeSerial);
                J->SetStringField(TEXT("noticeTextAfter"), Players[I]->LastNotice);
                J->SetObjectField(TEXT("before"), ServiceForestBefore[I]);
                J->SetObjectField(TEXT("after"), ServiceProfile(Players[I]));
                J->SetObjectField(TEXT("peerBefore"), ServiceForestBefore[1 - I]);
                J->SetObjectField(TEXT("peerAfter"), ServiceProfile(Players[1 - I]));
                auto BeforeCore = ServiceSnapshotCore(ServiceForestBefore[I]);
                auto PeerCore = ServiceSnapshotCore(ServiceForestBefore[1 - I]);
                J->SetObjectField(TEXT("ownerBeforeCore"), BeforeCore);
                J->SetObjectField(TEXT("ownerAfterCore"), ServiceCore(Players[I]));
                J->SetObjectField(TEXT("peerBeforeCore"), PeerCore);
                J->SetObjectField(TEXT("peerAfterCore"), ServiceCore(Players[1 - I]));
                J->SetBoolField(TEXT("peerUnchangedRequired"), false);
                J->SetBoolField(TEXT("peerUnchanged"), SameServiceJson(PeerCore, ServiceCore(Players[1 - I])));
                J->SetStringField(TEXT("terminal"), TEXT("shared-forest-ack"));
                ServiceServerEvents.Add(MakeShared<FJsonValueObject>(J));
            }
            ServicePhase = 5;
        }
    }
    bool Failed = bServiceForestCaptureFailed;
    for (const auto& Event : ServiceServerEvents)
    {
        Failed |= Event->AsObject()->GetStringField(TEXT("terminal")) == TEXT("failed-observation");
    }
    ServiceServerReport->SetArrayField(TEXT("events"), ServiceServerEvents);
    ServiceServerReport->SetNumberField(TEXT("phase"), ServicePhase);
    ServiceServerReport->SetBoolField(TEXT("forestCombatReleased"), bServiceForestReleased);
    ServiceServerReport->SetStringField(TEXT("status"), Failed              ? TEXT("failed")
                                                        : ServicePhase == 9 ? TEXT("complete")
                                                                            : TEXT("running"));
}

/** E UI·정상 이동·기존 전투/RPC로만 Legacy 서비스의 거절/성공 대조를 진행한다. */
bool UFableOnlineValidationDriver::TickUnit4LegacyServices(AFablePlayerController* PC, AFablePlayerState* P,
                                                           AFableCharacter* Hero, double Now)
{
    if (!ServiceReport)
    {
        ServiceReport = MakeShared<FJsonObject>();
        ServiceReport->SetStringField(TEXT("ownerPlayerId"), P->GetBackendPlayerId());
        ServiceReport->SetBoolField(TEXT("actualAirborneNegativeClaimed"), false);
    }
    ServiceReport->SetNumberField(TEXT("localStep"), ServiceStep);
    ServiceReport->SetStringField(TEXT("status"), ServiceStep == 13 ? TEXT("complete") : TEXT("running"));
    auto Server = ReadServiceReport(FPaths::Combine(Directory, TEXT("server.json")));
    const TSharedPtr<FJsonObject>* Services = nullptr;
    if (!Server || !Server->TryGetObjectField(TEXT("serverTownServices"), Services))
    {
        return false;
    }
    if ((*Services)->GetStringField(TEXT("status")) == TEXT("failed"))
    {
        Finish(false, TEXT("Legacy service server observation failed; no authority success claimed."));
        return false;
    }
    const int32 Phase = (*Services)->GetIntegerField(TEXT("phase"));
    ServiceReport->SetNumberField(TEXT("phase"), Phase);
    if (ServiceStep == 13)
    {
        DungeonHold(PC, EKeys::W, false);
        return Phase == 9 && ServiceSaved(P) && P->Gold == 90 && P->Ore == 5;
    }
    const FString Label(ServiceLabels[ServiceStep]);
    const FString Outcome(ServiceOutcomes[ServiceStep]);
    const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
    if ((*Services)->TryGetArrayField(TEXT("events"), Events))
    {
        for (const auto& Value : *Events)
        {
            const auto Event = Value->AsObject();
            if (Event->GetStringField(TEXT("ownerRole")) != Role || Event->GetStringField(TEXT("label")) != Label)
            {
                continue;
            }
            const FString Terminal = Event->GetStringField(TEXT("terminal"));
            const bool Open = Outcome == TEXT("opened-ui");
            const bool Ui = ServiceStep == 1 || ServiceStep == 4 ? PC->GetDialogue() == EFableDialogue::Guide
                            : ServiceStep == 7                   ? PC->GetDialogue() == EFableDialogue::Shop
                            : ServiceStep == 10                  ? PC->GetDialogue() == EFableDialogue::Forge
                            : ServiceStep == 12                  ? PC->IsPartyExpeditionOpen()
                                                                 : false;
            if (Terminal != (Open ? TEXT("dialogue-authorized") : *Outcome))
            {
                Finish(false, TEXT("Legacy service RPC terminated without the required authority/ACK evidence."));
                return false;
            }
            const auto EventAfter = Event->GetObjectField(TEXT("after"));
            if (!ServiceSaved(P) || (Open && !Ui) ||
                P->GetBackendAcknowledgedRevision() != EventAfter->GetNumberField(TEXT("acknowledgedRevision")) ||
                P->GetBackendAcknowledgedProfileHash() != EventAfter->GetStringField(TEXT("profileHash")) ||
                !SameServiceJson(Event->GetObjectField(TEXT("ownerAfterCore")), ServiceCore(P)))
            {
                return false;
            }
            const auto Before = Event->GetObjectField(TEXT("before"));
            const auto After = Event->GetObjectField(TEXT("after"));
            const auto BW = Before->GetObjectField(TEXT("wallet"));
            const auto AW = After->GetObjectField(TEXT("wallet"));
            bool Economy = true;
            if (ServiceStep == 2)
            {
                Economy =
                    P->ForestQuest == EFableQuestStage::Active && P->QuestKills == 0 && P->Gold == 0 && P->Ore == 0;
            }
            else if (ServiceStep == 3)
            {
                Economy =
                    P->ForestQuest == EFableQuestStage::Ready && P->QuestKills == 3 && P->Gold == 45 && P->Ore == 3;
            }
            else if (ServiceStep == 5)
            {
                Economy = P->ForestQuest == EFableQuestStage::Completed && P->Gold == 135 && P->Ore == 7 &&
                          AW->GetIntegerField(TEXT("gold")) - BW->GetIntegerField(TEXT("gold")) == 90 &&
                          AW->GetIntegerField(TEXT("ore")) - BW->GetIntegerField(TEXT("ore")) == 4 &&
                          P->Inventory.Num() == 5 && P->Level == 2 && P->Experience == 105;
            }
            else if (ServiceStep == 8)
            {
                Economy = P->Gold == 120 && P->Ore == 7 &&
                          AW->GetIntegerField(TEXT("gold")) == BW->GetIntegerField(TEXT("gold")) - 15 &&
                          AW->GetIntegerField(TEXT("healthPotions")) == BW->GetIntegerField(TEXT("healthPotions")) + 1;
            }
            else if (ServiceStep == 11)
            {
                auto ExpectedItem = ServiceWeaponBefore;
                ExpectedItem.Upgrade = 1;
                const auto* Item = P->FindItem(ServiceWeaponId);
                Economy = P->Gold == 90 && P->Ore == 5 && Item && Item->InstanceId == ServiceWeaponBefore.InstanceId &&
                          FableRPG::ItemPersistentSignature(*Item) == FableRPG::ItemPersistentSignature(ExpectedItem) &&
                          AW->GetIntegerField(TEXT("gold")) == BW->GetIntegerField(TEXT("gold")) - 30 &&
                          AW->GetIntegerField(TEXT("ore")) == BW->GetIntegerField(TEXT("ore")) - 2;
            }
            if (!Economy)
            {
                Finish(false, TEXT("Legacy service actual before/after economy or persistent weapon differs."));
                return false;
            }
            if (!ServiceClientBefore)
            {
                Finish(false, TEXT("Local pre-request snapshot missing; server-only proof is not sufficient."));
                return false;
            }
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("label"), Label);
            Row->SetStringField(TEXT("expectedOutcome"), Outcome);
            Row->SetStringField(TEXT("terminal"), Outcome);
            Row->SetStringField(TEXT("ownerRole"), Role);
            Row->SetStringField(TEXT("networkMode"), TEXT("client"));
            Row->SetNumberField(TEXT("clientProcessId"), FPlatformProcess::GetCurrentProcessId());
            Row->SetBoolField(TEXT("actualUiObserved"), Open && Ui);
            Row->SetObjectField(TEXT("clientBefore"), ServiceClientBefore);
            Row->SetObjectField(TEXT("before"), Before);
            Row->SetObjectField(TEXT("after"), ServiceProfile(P));
            Row->SetObjectField(TEXT("serverObservation"), Event);
            Row->SetBoolField(TEXT("peerUnchangedRequired"), Event->GetBoolField(TEXT("peerUnchangedRequired")));
            Row->SetBoolField(TEXT("peerUnchanged"), Event->GetBoolField(TEXT("peerUnchanged")));
            ServiceCheckpoints.Add(MakeShared<FJsonValueObject>(Row));
            Checks.Add(Label, true);
            Capture(Label);
            ++ServiceStep;
            bServiceSent = false;
            ServiceClientBefore.Reset();
            ServiceStationaryAt = 0;
            ServiceReport->SetNumberField(TEXT("localStep"), ServiceStep);
            ServiceReport->SetStringField(TEXT("pendingLabel"), TEXT(""));
            if (!Open)
            {
                PC->UIClosePanels();
            }
            Publish(TEXT("running"));
            return false;
        }
    }
    const bool First = Role == TEXT("client1");
    // 실제 owner PS의 Active 복제가 서버의 Guide 수락 처리를 먼저 증명한 뒤 일반 이동으로 물러난다.
    // PC RPC와 pawn 이동의 actor channel 순서를 client 전송 시각만으로 추정하지 않는다.
    // 이 이동은 서비스 권한을 부여하거나 저장/참가 명부를 직접 변경하지 않는다.
    if ((ServiceStep == 2 && bServiceSent) || (ServiceStep == 3 && Phase < 4))
    {
        if (P->ForestQuest == EFableQuestStage::Active)
        {
            DungeonWalk(PC, FableRPG::BoardLocation() + FVector(First ? -400 : 400, -1800, 100), 70);
        }
        else
        {
            DungeonHold(PC, EKeys::W, false);
        }
        if (ServiceStep == 2 && Now - ServiceSentAt > 30)
        {
            Finish(false, TEXT("Forest acceptance has no conclusive ACK while normally retreating."));
        }
        return false;
    }
    const int32 Wanted = ServiceStep < 2    ? (First ? 0 : 1)
                         : ServiceStep == 2 ? (First ? 2 : 3)
                         : ServiceStep == 3 ? 4
                         : ServiceStep < 6  ? (First ? 5 : 6)
                                            : (First ? 7 : 8);
    if (Phase != Wanted || !ServiceSaved(P))
    {
        DungeonHold(PC, EKeys::W, false);
        return false;
    }
    if (ServiceStep == 3)
    {
        if (!ServiceClientBefore)
        {
            ServiceClientBefore = ServiceProfile(P);
        }
        ServiceReport->SetBoolField(TEXT("forestBeforeCaptured"), true);
        ServiceReport->SetObjectField(TEXT("forestBefore"), ServiceClientBefore);
        // 양쪽 실제 요청 전 스냅샷이 원자적으로 공개되고 서버가 대조하기 전에는 전투 입력을 보내지 않는다.
        bool CombatReleased = false;
        (*Services)->TryGetBoolField(TEXT("forestCombatReleased"), CombatReleased);
        if (!WriteReport(SerializeReport(TEXT("running"), FString())) || !CombatReleased)
        {
            DungeonWalk(PC, FableRPG::BoardLocation() + FVector(First ? -400 : 400, -1800, 100), 70);
            return false;
        }
        if (P->ForestQuest != EFableQuestStage::Ready)
        {
            DungeonFight(PC, Now);
        }
        else
        {
            DungeonHold(PC, EKeys::W, false);
        }
        return false;
    }
    if (bServiceSent)
    {
        DungeonHold(PC, EKeys::W, false);
        if (Now - ServiceSentAt > 30)
        {
            Finish(false, TEXT("Legacy service request has no conclusive observed receipt; timeout is not PASS."));
        }
        return false;
    }
    FVector Target = FableRPG::BoardLocation() + FVector(First ? -80 : 80, -100, 100);
    if (ServiceStep == 0)
    {
        Target = FableRPG::BoardLocation() + FVector(First ? -80 : 80, -700, 100);
    }
    else if (ServiceStep == 7 || ServiceStep == 8 || ServiceStep == 9)
    {
        Target = FableRPG::MerchantLocation() + FVector(First ? -80 : 80, -100, 100);
    }
    else if (ServiceStep == 10 || ServiceStep == 11)
    {
        Target = FableRPG::ForgeLocation() + FVector(First ? -80 : 80, -100, 100);
    }
    else if (ServiceStep == 12)
    {
        Target = FableRPG::PortalLocation() + FVector(First ? -80 : 80, -100, 100);
    }
    if (!DungeonWalk(PC, Target, 55) || Hero->GetVelocity().Size2D() > 5 ||
        !Hero->GetCharacterMovement()->IsMovingOnGround())
    {
        ServiceStationaryAt = 0;
        return false;
    }
    if (ServiceStationaryAt == 0)
    {
        ServiceStationaryAt = Now;
    }
    if (Now - ServiceStationaryAt < .8 || Now - ServiceSentAt < .8)
    {
        return false;
    }
    if (ServiceStep == 9 || ServiceStep == 11)
    {
        if (!ServiceWeaponId)
        {
            for (const auto& Item : P->Inventory)
            {
                if (Item.Slot == EFableEquipmentSlot::Weapon && Item.HeroClass == P->HeroClass && Item.Rarity == 1 &&
                    Item.Upgrade == 0)
                {
                    ServiceWeaponId = Item.Id;
                    ServiceWeaponBefore = Item;
                    break;
                }
            }
        }
        if (!ServiceWeaponId)
        {
            Finish(false, TEXT("Actual forest weapon reward missing; no synthetic equipment is created."));
            return false;
        }
    }
    ServiceReport->SetStringField(TEXT("pendingLabel"), Label);
    ServiceClientBefore = ServiceProfile(P);
    // 원자적 공개가 실패하면 정상 요청도 보내지 않고 다음 tick에서 같은 관측을 다시 준비한다.
    // void Publish의 결과를 성공으로 추정해 권한 요청을 관측 없이 소비하지 않는다.
    if (!WriteReport(SerializeReport(TEXT("running"), FString())))
    {
        return false;
    }
    // 이 ID/기대 결과는 관측 폴더에만 쓰며 실제 RPC에는 추가하지 않는다.
    if (ServiceStep != 2 && ServiceStep != 5 && ServiceStep != 8 && ServiceStep != 11)
    {
        PC->UIClosePanels();
    }
    if (ServiceStep == 0 || ServiceStep == 2 || ServiceStep == 5)
    {
        if (ServiceStep != 0 && PC->GetDialogue() != EFableDialogue::Guide)
        {
            Finish(false, TEXT("Actual Guide E dialogue closed before normal quest confirmation."));
            return false;
        }
        PC->UISelectQuest(EFableQuestId::Forest);
        PC->UIConfirmQuest();
    }
    else if (ServiceStep == 6 || ServiceStep == 8)
    {
        if (ServiceStep == 8 && PC->GetDialogue() != EFableDialogue::Shop)
        {
            Finish(false, TEXT("Actual Shop E dialogue closed before normal purchase."));
            return false;
        }
        PC->UIShopBuy(EFableShopOffer::HealthPotion);
    }
    else if (ServiceStep == 9 || ServiceStep == 11)
    {
        PC->UIToggleInventory();
        PC->UISelectItem(ServiceWeaponId);
        if (!PC->IsInventoryOpen() || PC->GetSelectedItemId() != ServiceWeaponId)
        {
            Finish(false, TEXT("Normal inventory UI did not select the actual forest weapon."));
            return false;
        }
        PC->UIEnhance(ServiceWeaponId);
    }
    else
    {
        DungeonKey(PC, EKeys::E);
    }
    ServiceSentAt = Now;
    bServiceSent = true;
    return false;
}
