#include "FableTownManager.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "FablePlayerState.h"
#include "FableTownCatalog.h"
#include "FableTownTravelCoordinator.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

/** 실행 세대만 복제하고 GameState나 별도 시계는 만들지 않는다. / 実行世代のみを複製し別GameState・時計は作らない。 */
AFableTownManager::AFableTownManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(5.0f);
    Catalog = CreateDefaultSubobject<UFableTownCatalog>(TEXT("TownCatalog"));
    TravelCoordinator = CreateDefaultSubobject<UFableTownTravelCoordinator>(TEXT("TownTravelCoordinator"));
    WorldEpoch = FGuid::NewGuid();
    IdleProgress.AdventureStage = EFableAdventureStage::Village;
    IdleProgress.ActiveRegion = EFableRegion::Village;
    IdleProgress.ActiveDifficulty = EFableDifficulty::Normal;
}

/** 첫 서버 세대만 초기화한다. / 最初のサーバー世代のみ初期化する。 */
void AFableTownManager::BeginPlay()
{
    Super::BeginPlay();
    SetActorTickEnabled(HasAuthority());
    if (HasAuthority() && !PublicFieldScopeId.IsValid())
    {
        RotatePublicFieldScope();
    }
}

/** 이동 요청 전에 현재 공간 세대를 게시하며 새 참가자는 자동 등록하지 않는다. */
void AFableTownManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority())
    {
        return;
    }

    // 명부 복사 뒤 갱신해 연결 교체나 동기 콜백이 순회 중인 저장소를 변경하지 못하게 한다.
    PruneDisconnected();
    TArray<TWeakObjectPtr<APlayerController>> RegisteredPlayers;
    RegisteredPlayers.Reserve(Presence.Num());
    for (const auto& Pair : Presence)
    {
        RegisteredPlayers.Add(Pair.Value.Player);
    }

    // 실제 pawn에서만 문맥을 읽고 공개 숲 참가 여부나 게임 진행은 변경하지 않는다.
    for (const auto& Entry : RegisteredPlayers)
    {
        APlayerController* Player = Entry.Get();
        AFablePlayerState* Profile = Player ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
        const FPresenceRecord* Record = Presence.Find(Profile);
        if (Record && Record->Player.Get() == Player && ValidateConnection(Player, Profile, false))
        {
            RefreshPhysicalPresence(Player);
        }
    }
}

/** 필드 세대만 공개 복제한다. / フィールド世代のみ公開複製する。 */
void AFableTownManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableTownManager, PublicFieldScopeId);
}

/** 현재 월드의 관리자를 조회한다. / 現在worldのmanagerを照会する。 */
AFableTownManager* AFableTownManager::Find(const UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }

    for (TActorIterator<AFableTownManager> It(const_cast<UWorld*>(World)); It; ++It)
    {
        if (IsValid(*It))
        {
            return *It;
        }
    }

    return nullptr;
}

/** 서버 코드만 관리자를 생성한다. / サーバーコードのみが生成する。 */
AFableTownManager* AFableTownManager::Ensure(UWorld* World)
{
    if (!World || World->GetNetMode() == NM_Client)
    {
        return nullptr;
    }

    AFableTownManager* Manager = Find(World);
    if (!Manager)
    {
        FActorSpawnParameters Parameters;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Manager = World->SpawnActor<AFableTownManager>(AFableTownManager::StaticClass(), FVector::ZeroVector,
                                                       FRotator::ZeroRotator, Parameters);
    }

    if (Manager && !Manager->PublicFieldScopeId.IsValid())
    {
        Manager->RotatePublicFieldScope();
    }

    return Manager;
}

/** 임의 변경할 수 없는 정의를 반환한다. / 任意変更できない定義を返す。 */
const UFableTownCatalog* AFableTownManager::GetCatalog() const
{
    return Catalog;
}

/** 필드 전투를 변경하지 않는 마을 idle 값을 반환한다. / 戦闘進行を変更しない村idle値を返す。 */
const FFableAdventureProgress& AFableTownManager::GetIdleProgress() const
{
    return IdleProgress;
}

/** 루트 GameState의 서버 시계만 사용한다. / root GameStateの時計だけを使う。 */
double AFableTownManager::GetServerTime() const
{
    const AFableGameState* Root = GetWorld() ? GetWorld()->GetGameState<AFableGameState>() : nullptr;
    return Root ? Root->GetServerWorldTimeSeconds() : 0.0;
}

/** 현재 필드 실행 세대를 읽는다. / 現在の実行世代を読む。 */
FGuid AFableTownManager::GetPublicFieldScopeId() const
{
    return PublicFieldScopeId;
}

/** 발사체가 캡처할 이름과 세대를 묶는다. / 発射体が固定する名前と世代をまとめる。 */
FFablePublicAreaKey AFableTownManager::GetPublicFieldKey() const
{
    FFablePublicAreaKey Key;
    Key.AreaId = FableTown::LegacyForestAreaId();
    Key.ScopeId = PublicFieldScopeId;
    return Key;
}

/** 등록 당시와 동일한 현재 연결·프로필만 인정한다. / 登録時と同じ現在の接続・profileのみを認める。 */
bool AFableTownManager::ValidateConnection(const APlayerController* Player, const AFablePlayerState* ExpectedProfile,
                                           bool bRequirePawn) const
{
    if (!HasAuthority() || !GetWorld() || !IsValid(Player) || Player->GetWorld() != GetWorld() ||
        !Player->HasAuthority() || !IsValid(ExpectedProfile) || ExpectedProfile->GetWorld() != GetWorld() ||
        ExpectedProfile->IsInactive() || Player->GetPlayerState<AFablePlayerState>() != ExpectedProfile ||
        ExpectedProfile->GetOwner() != Player || !ExpectedProfile->bProfileLoaded)
    {
        return false;
    }

    if (ExpectedProfile->IsBackendEnabled() &&
        (!ExpectedProfile->IsBackendReady() || ExpectedProfile->GetBackendPlayerId().IsEmpty()))
    {
        return false;
    }
    if (const FPresenceRecord* Record = Presence.Find(const_cast<AFablePlayerState*>(ExpectedProfile));
        Record && Record->Player.Get() == Player)
    {
        const auto* Current = CurrentConnectionsByIdentity.Find(Record->Identity);
        if (!Current || Current->Get() != Player)
        {
            return false;
        }
    }

    bool bCurrentController = false;
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (It->Get() == Player)
        {
            bCurrentController = true;
            break;
        }
    }

    if (!bCurrentController)
    {
        return false;
    }

    const APawn* Pawn = Player->GetPawn();
    return !bRequirePawn || (IsValid(Pawn) && Pawn->GetWorld() == GetWorld() && Pawn->GetController() == Player &&
                             Pawn->GetPlayerState<AFablePlayerState>() == ExpectedProfile);
}

/** 接続の消滅だけを除外し、位置・死亡だけでは登録を消さない。 / 연결 소멸만 정리하며 위치·죽음만으로 지우지 않는다. */
void AFableTownManager::PruneDisconnected()
{
    if (!HasAuthority())
    {
        return;
    }

    for (auto It = Presence.CreateIterator(); It; ++It)
    {
        const FPresenceRecord& Record = It.Value();
        if (!ValidateConnection(Record.Player.Get(), Record.Profile.Get(), false))
        {
            if (AFablePlayerState* Profile = Record.Profile.Get())
            {
                if (Profile->PublicFieldScopeId == Record.FieldScope)
                {
                    Profile->PublicFieldScopeId.Invalidate();
                    Profile->ForceNetUpdate();
                }
            }
            It.RemoveCurrent();
        }
    }
}

/** 既存の登録集合だけを次世代へ移す。 / 기존 등록 집합만 다음 세대로 옮긴다. */
FGuid AFableTownManager::RotatePublicFieldScope()
{
    if (!HasAuthority())
    {
        return FGuid();
    }

    PruneDisconnected();
    const FGuid PreviousScope = PublicFieldScopeId;
    PublicFieldScopeId = FGuid::NewGuid();
    for (auto& Pair : Presence)
    {
        FPresenceRecord& Record = Pair.Value;
        AFablePlayerState* Profile = Record.Profile.Get();
        if (Profile && Record.FieldScope.IsValid() && Record.FieldScope == PreviousScope &&
            Profile->PublicFieldScopeId == PreviousScope && !Profile->ActiveExpeditionId.IsValid())
        {
            Record.FieldScope = PublicFieldScopeId;
            Profile->PublicFieldScopeId = PublicFieldScopeId;
            Profile->ForceNetUpdate();
        }
        else if (Record.FieldScope.IsValid())
        {
            if (Profile && Profile->PublicFieldScopeId == Record.FieldScope)
            {
                Profile->PublicFieldScopeId.Invalidate();
                Profile->ForceNetUpdate();
            }
            Record.FieldScope.Invalidate();
        }
    }

    ForceNetUpdate();
    return PublicFieldScopeId;
}

/** プレイヤーの実pawnからのみ位置を読む。 / 플레이어의 실제 pawn 위치만 읽는다. */
bool AFableTownManager::ResolvePhysicalContext(const AActor* Context, FFableTownLocationContext& OutContext) const
{
    OutContext = FFableTownLocationContext();
    if (!Catalog || !IsValid(Context) || Context->GetWorld() != GetWorld())
    {
        return false;
    }

    const APawn* Pawn = Cast<APawn>(Context);
    if (const AController* Controller = Cast<AController>(Context))
    {
        Pawn = Controller->GetPawn();
    }
    else if (const AFablePlayerState* Profile = Cast<AFablePlayerState>(Context))
    {
        const AController* ProfileController = Cast<AController>(Profile->GetOwner());
        if (!ProfileController || ProfileController->GetPlayerState<AFablePlayerState>() != Profile)
        {
            return false;
        }
        Pawn = ProfileController->GetPawn();
    }

    if (!IsValid(Pawn) || Pawn->GetWorld() != GetWorld())
    {
        return false;
    }

    return Catalog->ResolveLocation(Pawn->GetActorLocation(), OutContext, true);
}

/** 현재 연결과 실제 pawn의 검증된 공간만 등록한다. / 現接続と実pawnの検証済空間のみ登録する。 */
bool AFableTownManager::RegisterValidatedPresence(APlayerController* Player)
{
    return RefreshPhysicalPresence(Player);
}

/** 등록한 동일 연결의 현재 물리 공간을 다시 검사한다. / 同一登録接続の現在空間を再検査する。 */
bool AFableTownManager::GetRegisteredPresence(const AFablePlayerState* Profile,
                                              FFableTownLocationContext& OutContext) const
{
    OutContext = FFableTownLocationContext();
    const FPresenceRecord* Record = Presence.Find(const_cast<AFablePlayerState*>(Profile));
    if (!Record || !ValidateConnection(Record->Player.Get(), Profile, true) || Record->Profile.Get() != Profile ||
        !ResolvePhysicalContext(Profile, OutContext) || OutContext.TownId != Record->TownId)
    {
        OutContext = FFableTownLocationContext();
        return false;
    }

    return true;
}

/** 명시적인 서버 참여 요청만 명부에 넣는다. / 明示的なサーバー参加要求だけを登録する。 */
bool AFableTownManager::RegisterSharedForestParticipant(APlayerController* Player)
{
    AFablePlayerState* Profile = IsValid(Player) ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    const AFableCharacter* Hero = IsValid(Player) ? Player->GetPawn<AFableCharacter>() : nullptr;
    FFableTownLocationContext Context;
    if (!PublicFieldScopeId.IsValid() || !ValidateConnection(Player, Profile, true) || !Hero || !Hero->IsAlive() ||
        Profile->ActiveExpeditionId.IsValid() || !ResolvePhysicalContext(Player, Context) ||
        Context.TownId != EFableTownId::LegacyVillage || !RegisterValidatedPresence(Player))
    {
        return false;
    }

    FPresenceRecord& Record = Presence.FindChecked(Profile);
    Record.FieldScope = PublicFieldScopeId;
    Profile->PublicFieldScopeId = PublicFieldScopeId;
    Profile->ForceNetUpdate();
    return true;
}

/** 소유 연결의 필드 등록만 명시 해제한다. / 所有接続のフィールド登録だけを解除する。 */
void AFableTownManager::UnregisterSharedForestParticipant(APlayerController* Player)
{
    if (!HasAuthority() || !IsValid(Player) || Player->GetWorld() != GetWorld())
    {
        return;
    }
    AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
    if (FPresenceRecord* Record = Presence.Find(Profile))
    {
        if (Record->Player.Get() != Player || Record->Profile.Get() != Profile)
        {
            return;
        }
        Record->FieldScope.Invalidate();
    }

    if (Profile && Profile->GetOwner() == Player)
    {
        Profile->PublicFieldScopeId.Invalidate();
        Profile->ForceNetUpdate();
    }
}

/** 지연된 이전 연결이 새 등록을 삭제하지 못하게 한다. / 遅れた旧接続による新登録削除を防ぐ。 */
void AFableTownManager::UnregisterPlayer(APlayerController* Player)
{
    if (!HasAuthority() || !IsValid(Player) || Player->GetWorld() != GetWorld())
    {
        return;
    }
    AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
    const FPresenceRecord* Record = Presence.Find(Profile);
    if (!Profile || Profile->GetOwner() != Player ||
        (Record && (Record->Player.Get() != Player || Record->Profile.Get() != Profile)))
    {
        return;
    }

    if (TravelCoordinator)
    {
        TravelCoordinator->CancelForConnection(Player, EFableTownTravelError::StaleConnection);
    }
    RetiredConnections.Add(Player);
    if (Record)
    {
        const auto* Current = CurrentConnectionsByIdentity.Find(Record->Identity);
        if (Current && Current->Get() == Player)
        {
            CurrentConnectionsByIdentity.Remove(Record->Identity);
        }
    }
    UnregisterSharedForestParticipant(Player);
    Presence.Remove(Profile);
}

/** 同じ登録世代・PS・controllerの組だけを返す。 / 같은 등록 세대·PS·controller만 반환한다. */
bool AFableTownManager::IsSharedForestParticipant(const AFablePlayerState* Profile, FGuid ExpectedScope) const
{
    if (!HasAuthority() || !PublicFieldScopeId.IsValid() || !IsValid(Profile) ||
        Profile->ActiveExpeditionId.IsValid() || Profile->PublicFieldScopeId != PublicFieldScopeId ||
        (ExpectedScope.IsValid() && ExpectedScope != PublicFieldScopeId))
    {
        return false;
    }

    const FPresenceRecord* Record = Presence.Find(const_cast<AFablePlayerState*>(Profile));
    return Record && Record->Profile.Get() == Profile && Record->FieldScope == PublicFieldScopeId &&
           ValidateConnection(Record->Player.Get(), Profile, false);
}

/** 인증된 현재 명부만 안정 순서로 반환한다. / 現在の認証済名簿だけを安定順に返す。 */
TArray<APlayerController*> AFableTownManager::GetSharedForestPlayers() const
{
    TArray<APlayerController*> Result;
    if (!HasAuthority())
    {
        return Result;
    }

    for (const auto& Pair : Presence)
    {
        const FPresenceRecord& Record = Pair.Value;
        if (IsSharedForestParticipant(Record.Profile.Get()))
        {
            Result.Add(Record.Player.Get());
        }
    }
    Result.Sort(
        [](const APlayerController& Left, const APlayerController& Right)
        {
            const APlayerState* LeftState = Left.GetPlayerState<APlayerState>();
            const APlayerState* RightState = Right.GetPlayerState<APlayerState>();
            return LeftState && RightState && LeftState->GetPlayerId() < RightState->GetPlayerId();
        });
    return Result;
}

/** 명시 참가자의 프로필만 view에 제공한다. / 明示参加者のprofileのみviewへ渡す。 */
TArray<APlayerState*> AFableTownManager::GetSharedForestParticipants() const
{
    TArray<APlayerState*> Result;
    for (APlayerController* Player : GetSharedForestPlayers())
    {
        Result.Add(Player->GetPlayerState<AFablePlayerState>());
    }

    return Result;
}

/** 새 소유 연결만 새 세대로 발급하며 늦은 이전 연결은 현재 명부를 덮어쓰지 못한다. */
bool AFableTownManager::RegisterOrRefreshConnection(APlayerController* Player)
{
    AFablePlayerState* Profile = IsValid(Player) ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    if (!ValidateConnection(Player, Profile, false) || RetiredConnections.Contains(Player))
    {
        return false;
    }

    const FString Identity = Profile->IsBackendEnabled() ? TEXT("online:") + Profile->GetBackendPlayerId()
                                                         : TEXT("local:") + FString::FromInt(Profile->GetPlayerId());
    FPresenceRecord* Existing = Presence.Find(Profile);
    const auto* Current = CurrentConnectionsByIdentity.Find(Identity);
    if (Existing && Existing->Player.Get() == Player && Existing->Snapshot.ConnectionNonce.IsValid() &&
        Existing->Identity == Identity && Current && Current->Get() == Player)
    {
        return true;
    }
    if (ConnectionSequence >= FableTownTravel::MaxSafeSequence || PresenceSequence >= FableTownTravel::MaxSafeSequence)
    {
        return false;
    }

    if (Existing && TravelCoordinator)
    {
        TravelCoordinator->CancelForConnection(Existing->Player.Get(), EFableTownTravelError::StaleConnection);
    }
    if (Current && Current->IsValid() && Current->Get() != Player)
    {
        if (TravelCoordinator)
        {
            TravelCoordinator->CancelForConnection(Current->Get(), EFableTownTravelError::StaleConnection);
        }
        RetiredConnections.Add(*Current);
    }

    FPresenceRecord Record;
    Record.Player = Player;
    Record.Profile = Profile;
    Record.Identity = Identity;
    Record.Snapshot.WorldEpoch = WorldEpoch;
    Record.Snapshot.OwnerConnectionGeneration = ++ConnectionSequence;
    Record.Snapshot.ConnectionNonce = FGuid::NewGuid();
    Record.Snapshot.PresenceRevision = ++PresenceSequence;
    Presence.Add(Profile, Record);
    CurrentConnectionsByIdentity.Add(Identity, Player);
    Profile->TownConnectionSnapshot = Record.Snapshot;
    Profile->bPendingTownTravel = false;
    Profile->ForceNetUpdate();
    return true;
}

/** 위치 흔들림이 아니라 Town/층/연결부/공개 세대 변경만 revision을 증가시킨다. */
bool AFableTownManager::RefreshPhysicalPresence(APlayerController* Player)
{
    if (!RegisterOrRefreshConnection(Player))
    {
        return false;
    }
    AFablePlayerState* Profile = Player->GetPlayerState<AFablePlayerState>();
    FFableTownLocationContext Context;
    const bool bPhysicalValid = ValidateConnection(Player, Profile, true) && ResolvePhysicalContext(Player, Context);

    FPresenceRecord& Record = Presence.FindChecked(Profile);
    FFableTownConnectionSnapshot& Snapshot = Record.Snapshot;
    FFablePublicAreaKey PublicArea;
    if (Record.FieldScope.IsValid())
    {
        PublicArea.AreaId = FableTown::LegacyForestAreaId();
        PublicArea.ScopeId = Record.FieldScope;
    }
    const auto& Previous = Snapshot.PhysicalContext;
    const bool bChanged = Previous.TownId != Context.TownId || Previous.FloorId != Context.FloorId ||
                          Previous.ConnectorId != Context.ConnectorId ||
                          Previous.CatalogVersion != Context.CatalogVersion ||
                          Previous.bGeometryReady != Context.bGeometryReady || Snapshot.PublicArea != PublicArea;
    if (bChanged && PresenceSequence >= FableTownTravel::MaxSafeSequence)
    {
        return false;
    }
    if (bChanged)
    {
        Snapshot.PresenceRevision = ++PresenceSequence;
    }
    Snapshot.PhysicalContext = Context;
    Snapshot.PublicArea = PublicArea;
    Record.TownId = Context.TownId;
    Profile->TownConnectionSnapshot = Snapshot;
    if (bChanged)
    {
        // 위치 흔들림마다 강제 복제하지 않고 의미가 바뀐 세대만 즉시 소유자에게 알린다.
        Profile->ForceNetUpdate();
    }
    return bPhysicalValid;
}

/** 실제 현재 연결에 묶인 서버 스냅샷만 반환한다. */
bool AFableTownManager::GetConnectionSnapshot(const AFablePlayerState* Profile,
                                              FFableTownConnectionSnapshot& OutSnapshot) const
{
    OutSnapshot = FFableTownConnectionSnapshot();
    const FPresenceRecord* Record = Presence.Find(const_cast<AFablePlayerState*>(Profile));
    if (!Record || !ValidateConnection(Record->Player.Get(), Profile, false) || Record->Profile.Get() != Profile ||
        !Record->Snapshot.ConnectionNonce.IsValid() || !CurrentConnectionsByIdentity.Contains(Record->Identity) ||
        CurrentConnectionsByIdentity.FindChecked(Record->Identity) != Record->Player)
    {
        return false;
    }
    OutSnapshot = Record->Snapshot;
    return true;
}

/** controller 객체와 발급 세대·nonce가 모두 일치해야 현재 연결이다. */
bool AFableTownManager::IsCurrentConnection(const APlayerController* Player, int64 OwnerConnectionGeneration,
                                            FGuid ConnectionNonce) const
{
    FFableTownConnectionSnapshot Snapshot;
    return IsValid(Player) && GetConnectionSnapshot(Player->GetPlayerState<AFablePlayerState>(), Snapshot) &&
           Player->GetPlayerState<AFablePlayerState>()->GetOwner() == Player &&
           Snapshot.OwnerConnectionGeneration == OwnerConnectionGeneration &&
           Snapshot.ConnectionNonce == ConnectionNonce;
}

/** 물리 도착 검증을 마친 뒤에만 공개 참가 해제와 안전 앵커를 한 번 확정한다. */
bool AFableTownManager::CommitAnchorPresence(APlayerController* Player, const FFableTownAnchorRef& Anchor,
                                             const FTransform& AcceptedTransform, FGuid TransactionId, int64 Generation,
                                             FFableTownConnectionSnapshot& OutAfter)
{
    AFablePlayerState* Profile = IsValid(Player) ? Player->GetPlayerState<AFablePlayerState>() : nullptr;
    FPresenceRecord* Record = Presence.Find(Profile);
    FFableTownLocationContext Context;
    if (!Record || Record->Player.Get() != Player || !ValidateConnection(Player, Profile, true) || !Anchor.IsValid() ||
        !TransactionId.IsValid() || Generation <= 0 || !ResolvePhysicalContext(Player, Context) ||
        Context.TownId != Anchor.TownId || Context.CatalogVersion != Anchor.CatalogVersion ||
        AcceptedTransform.ContainsNaN() || !Player->GetPawn()->GetActorTransform().Equals(AcceptedTransform, 0.1f) ||
        PresenceSequence >= FableTownTravel::MaxSafeSequence ||
        Record->Snapshot.SafeCheckpointRevision >= FableTownTravel::MaxSafeSequence)
    {
        return false;
    }

    // 마지막 fallible 검사를 명부 변경 전에 끝낸다. 이후는 미리 검사한 단순 값 대입뿐이다.
    AFableCharacter* Hero = Player->GetPawn<AFableCharacter>();
    if (!Hero || !Hero->CommitSafeTownCheckpointAtCurrentPosition(Anchor, AcceptedTransform.Rotator()))
    {
        return false;
    }

    Record->FieldScope.Invalidate();
    Profile->PublicFieldScopeId.Invalidate();
    auto& Snapshot = Record->Snapshot;
    Snapshot.PhysicalContext = Context;
    Snapshot.PublicArea = FFablePublicAreaKey();
    Snapshot.SafeAnchor = Anchor;
    Snapshot.PresenceRevision = ++PresenceSequence;
    ++Snapshot.SafeCheckpointRevision;
    Snapshot.LastCommittedTransactionId = TransactionId;
    Snapshot.LastCommittedGeneration = Generation;
    Snapshot.LastAcceptedTransform = AcceptedTransform;
    Record->TownId = Anchor.TownId;
    Profile->TownConnectionSnapshot = Snapshot;
    Profile->ForceNetUpdate();
    OutAfter = Snapshot;
    return true;
}

/** 서버 이동 조정자를 기존 manager에서 반환한다. */
UFableTownTravelCoordinator* AFableTownManager::GetTravelCoordinator() const
{
    return TravelCoordinator;
}
