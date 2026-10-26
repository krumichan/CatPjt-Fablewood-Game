#include "FableTownStreamingSubsystem.h"
#include "FablePlayerState.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UObject/Package.h"

namespace
{
    constexpr int64 MaximumExactRevision = 9007199254740991LL;
    constexpr float MonitorIntervalSeconds = 0.1f;

    /** 좌표/회전/배율이 유한하며 유효한지 검사한다. / 座標・回転・倍率の有限性を検査する。 */
    bool IsFiniteTransform(const FTransform& Transform)
    {
        const FVector Translation = Transform.GetTranslation();
        const FVector Scale = Transform.GetScale3D();
        const FQuat Rotation = Transform.GetRotation();
        return !Transform.ContainsNaN() && FMath::IsFinite(Translation.X) && FMath::IsFinite(Translation.Y) &&
               FMath::IsFinite(Translation.Z) && FMath::IsFinite(Scale.X) && FMath::IsFinite(Scale.Y) &&
               FMath::IsFinite(Scale.Z) && Scale.X > 0.0 && Scale.Y > 0.0 && Scale.Z > 0.0 && Rotation.IsNormalized();
    }

    /** 이벤트는 조회 가능한 상태 변화에만 발행한다. / 観測できる状態変化だけを通知する。 */
    bool SameStatus(const FFableTownGeometryStatus& Left, const FFableTownGeometryStatus& Right)
    {
        return Left.State == Right.State && Left.EntryEpoch == Right.EntryEpoch &&
               Left.bReadyVisible == Right.bReadyVisible && Left.Failure == Right.Failure;
    }
}

/** Game/PIE 월드에서만 초기화한다. / Game・PIE worldだけで初期化する。 */
void UFableTownStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bStopping = false;
    LastEntryEpoch = 0;
}

/** world 종료 전 모든 자체 타이머와 조회를 차단한다. / world終了前に自分のtimer・照会を遮断する。 */
void UFableTownStreamingSubsystem::Deinitialize()
{
    StopAndFence();
    Super::Deinitialize();
}

/** Actor 종료보다 앞선 지형 callback 재진입을 막는다. / Actor終了前に地形通知の再入を遮断する。 */
void UFableTownStreamingSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
    StopAndFence();
    Super::OnWorldEndPlay(InWorld);
}

/** editor preview에는 실행용 지형 소유자를 만들지 않는다. / editor previewには実行用所有者を作らない。 */
bool UFableTownStreamingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

/** 마을과 catalog 버전만 엔트리 키에 사용한다. / 村とcatalog versionだけをkeyに使う。 */
int64 UFableTownStreamingSubsystem::MakeEntryKey(EFableTownId TownId, int32 CatalogVersion)
{
    return (static_cast<int64>(TownId) << 32) | static_cast<uint32>(CatalogVersion);
}

/** 같은 엔트리의 불변 내용과 변환을 비교한다. / entryの不変内容と変換を比較する。 */
bool UFableTownStreamingSubsystem::SameDescriptor(const FFableTownGeometryDescriptor& Left,
                                                  const FFableTownGeometryDescriptor& Right)
{
    return Left.Mode == Right.Mode && Left.TownId == Right.TownId && Left.CatalogVersion == Right.CatalogVersion &&
           Left.GeometryLevel.ToSoftObjectPath() == Right.GeometryLevel.ToSoftObjectPath() &&
           Left.LocalToWorld.Equals(Right.LocalToWorld, 0.0f) && Left.FixedInstanceName == Right.FixedInstanceName &&
           Left.ContentIdentity == Right.ContentIdentity;
}

/** 연결·목적·트랜잭션이 같은 재요청만 idempotent하게 처리한다. / 同じ接続・目的・取引だけを再利用する。 */
bool UFableTownStreamingSubsystem::SameOwner(const FFableTownGeometryLeaseOwner& Left,
                                             const FFableTownGeometryLeaseOwner& Right)
{
    return Left.OwnerPlayerState == Right.OwnerPlayerState &&
           Left.OwnerConnectionGeneration == Right.OwnerConnectionGeneration &&
           Left.TransactionId == Right.TransactionId && Left.Purpose == Right.Purpose;
}

/** GUID만이 아니라 entry 세대까지 정확히 비교한다. / GUIDだけでなくentry世代まで一致させる。 */
bool UFableTownStreamingSubsystem::SameLease(const FFableTownGeometryLease& Left, const FFableTownGeometryLease& Right)
{
    return Left.LeaseId == Right.LeaseId && Left.TownId == Right.TownId &&
           Left.CatalogVersion == Right.CatalogVersion && Left.EntryEpoch == Right.EntryEpoch;
}

/** 기존 persistent map만 식별하며 추가 인스턴스를 만들지 않는다. / 既存persistent mapだけを識別する。 */
bool UFableTownStreamingSubsystem::IsExpectedPersistentWorld() const
{
    const UWorld* World = GetWorld();
    if (!World || !World->PersistentLevel || World->PersistentLevel->GetWorld() != World)
    {
        return false;
    }

    const FString PackageName = UWorld::RemovePIEPrefix(World->PersistentLevel->GetOutermost()->GetName());
    return PackageName == TEXT("/Game/Maps/ForestClearing") && World->PersistentLevel->bIsVisible;
}

/** 승인 catalog와 독립 대조하며 미채택 마을을 활성화하지 않는다. / 承認catalogと照合し未採用村を有効化しない。 */
bool UFableTownStreamingSubsystem::ValidateDescriptor(const FFableTownGeometryDescriptor& Descriptor,
                                                      EFableTownGeometryFailure& OutFailure) const
{
    OutFailure = EFableTownGeometryFailure::InvalidDescriptor;
    const AFableTownManager* Manager = AFableTownManager::Find(GetWorld());
    const UFableTownCatalog* Catalog = Manager ? Manager->GetCatalog() : nullptr;
    FString Error;
    const FFableTownDefinition* Definition = Catalog ? Catalog->FindTown(Descriptor.TownId) : nullptr;
    if (!Definition || !Catalog->Validate(Error) || Descriptor.CatalogVersion != Definition->CatalogVersion ||
        !IsFiniteTransform(Descriptor.LocalToWorld) || !Descriptor.LocalToWorld.Equals(Definition->LocalToWorld, 0.0f))
    {
        return false;
    }

    if (!Definition->bGeometryReady)
    {
        OutFailure = EFableTownGeometryFailure::GeometryNotAdopted;
        return false;
    }

    if (Descriptor.TownId != EFableTownId::LegacyVillage)
    {
        // 네 후보의 승인 콘텐츠 식별자는 아직 없다. 후속 채택 전에는 로드하지 않는다.
        // 四候補の承認content identityは未提供。後続の採用まではロードしない。
        OutFailure = EFableTownGeometryFailure::GeometryNotAdopted;
        return false;
    }

    const FName ExpectedName(
        *FString::Printf(TEXT("FableTown_LegacyVillage_Geometry_v%d"), Definition->CatalogVersion));
    if (Descriptor.Mode != EFableTownGeometryMode::ExistingPersistent || !Descriptor.GeometryLevel.IsNull() ||
        !Definition->GeometryLevel.IsNull() || Descriptor.FixedInstanceName != ExpectedName ||
        Descriptor.ContentIdentity != TEXT("LegacyPersistentV1") || !IsExpectedPersistentWorld())
    {
        return false;
    }

    OutFailure = EFableTownGeometryFailure::None;
    return true;
}

/** 현재 controller/PS 연결의 발급 세대를 검사한다. 해제 검사는 별도다. / 現在接続を検査し解放とは分離する。 */
bool UFableTownStreamingSubsystem::ValidateOwner(const FFableTownGeometryLeaseOwner& Owner) const
{
    const AFablePlayerState* Profile = Owner.OwnerPlayerState.Get();
    const APlayerController* Player = Profile ? Cast<APlayerController>(Profile->GetOwner()) : nullptr;
    if (!IsValid(Profile) || !IsValid(Player) || Profile->GetWorld() != GetWorld() ||
        Player->GetWorld() != GetWorld() || Player->GetPlayerState<AFablePlayerState>() != Profile ||
        !Profile->bProfileLoaded || Owner.OwnerConnectionGeneration <= 0 ||
        Owner.OwnerConnectionGeneration > MaximumExactRevision ||
        Owner.Purpose < EFableTownGeometryLeasePurpose::Presence ||
        Owner.Purpose > EFableTownGeometryLeasePurpose::ClientSourceRetention)
    {
        return false;
    }

    const FFableTownConnectionSnapshot& Snapshot = Profile->GetTownConnectionSnapshot();
    if (!Snapshot.WorldEpoch.IsValid() || !Snapshot.ConnectionNonce.IsValid() ||
        Snapshot.OwnerConnectionGeneration != Owner.OwnerConnectionGeneration)
    {
        return false;
    }

    if (GetWorld()->GetNetMode() != NM_Client)
    {
        const AFableTownManager* Manager = AFableTownManager::Find(GetWorld());
        if (!Manager ||
            !Manager->IsCurrentConnection(Player, Owner.OwnerConnectionGeneration, Snapshot.ConnectionNonce))
        {
            return false;
        }
    }

    if ((Owner.Purpose == EFableTownGeometryLeasePurpose::TargetPending ||
         Owner.Purpose == EFableTownGeometryLeasePurpose::SourceRetention ||
         Owner.Purpose == EFableTownGeometryLeasePurpose::ClientTargetPending ||
         Owner.Purpose == EFableTownGeometryLeasePurpose::ClientSourceRetention) &&
        !Owner.TransactionId.IsValid())
    {
        return false;
    }

    // 전역 resident 정책은 채택/메모리 측정 이후 별도로 정한다. / global resident方針は採用・測定後に定める。
    return Owner.Purpose != EFableTownGeometryLeasePurpose::ServerResident;
}

/** 하나의 엔트리와 정확한 소유 token을 공유하며 반복 요청은 재사용한다. / 単一entryと所有tokenを共有する。 */
bool UFableTownStreamingSubsystem::AcquireGeometry(const FFableTownGeometryDescriptor& Descriptor,
                                                   const FFableTownGeometryLeaseOwner& TrustedOwner,
                                                   FFableTownGeometryLease& OutLease,
                                                   EFableTownGeometryFailure& OutFailure)
{
    OutLease = FFableTownGeometryLease();
    OutFailure = EFableTownGeometryFailure::WorldEnded;
    if (bStopping || !GetWorld())
    {
        return false;
    }

    if (!ValidateDescriptor(Descriptor, OutFailure))
    {
        return false;
    }

    if (!ValidateOwner(TrustedOwner))
    {
        OutFailure = EFableTownGeometryFailure::InvalidOwner;
        return false;
    }

    const int64 Key = MakeEntryKey(Descriptor.TownId, Descriptor.CatalogVersion);
    for (const TPair<int64, FGeometryEntry>& Pair : Entries)
    {
        if (Pair.Value.Descriptor.TownId == Descriptor.TownId && Pair.Key != Key)
        {
            OutFailure = Pair.Value.bRemoving ? EFableTownGeometryFailure::RemovingBusy
                                              : EFableTownGeometryFailure::DescriptorConflict;
            return false;
        }
    }

    FGeometryEntry* Existing = Entries.Find(Key);
    if (Existing && !SameDescriptor(Existing->Descriptor, Descriptor))
    {
        OutFailure = EFableTownGeometryFailure::DescriptorConflict;
        return false;
    }

    if (Existing && Existing->bRemoving)
    {
        OutFailure = EFableTownGeometryFailure::RemovingBusy;
        return false;
    }

    if (Existing)
    {
        for (const FGuid& Token : Existing->Tokens)
        {
            const FLeaseRecord* Record = Leases.Find(Token);
            if (Record && SameOwner(Record->Owner, TrustedOwner))
            {
                OutLease = Record->Lease;
                OutFailure = EFableTownGeometryFailure::None;
                return true;
            }
        }
    }

    if (!Existing)
    {
        if (LastEntryEpoch >= MaximumExactRevision)
        {
            OutFailure = EFableTownGeometryFailure::InvalidDescriptor;
            return false;
        }

        FGeometryEntry NewEntry;
        NewEntry.Descriptor = Descriptor;
        NewEntry.Status.EntryEpoch = ++LastEntryEpoch;
        NewEntry.Status.State = EFableTownGeometryState::Loading;
        Entries.Add(Key, MoveTemp(NewEntry));
        Existing = Entries.Find(Key);

        if (Descriptor.Mode == EFableTownGeometryMode::StreamedLevel)
        {
            bool bAdded = false;
            ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
                GetWorld(), Descriptor.GeometryLevel, Descriptor.LocalToWorld, bAdded,
                Descriptor.FixedInstanceName.ToString(), {}, false);
            if (Streaming)
            {
                RetainedStreams.Add(Key, Streaming);
            }

            if (!bAdded || !Streaming)
            {
                Existing->Status.State = EFableTownGeometryState::Failed;
                Existing->Status.Failure = EFableTownGeometryFailure::LoadFailed;
                BeginRemoval(Key, *Existing);
                EnsureMonitor();
                OutFailure = EFableTownGeometryFailure::LoadFailed;
                return false;
            }
        }
    }

    FLeaseRecord Record;
    do
    {
        Record.Lease.LeaseId = FGuid::NewGuid();
    } while (Leases.Contains(Record.Lease.LeaseId));
    Record.Lease.TownId = Descriptor.TownId;
    Record.Lease.CatalogVersion = Descriptor.CatalogVersion;
    Record.Lease.EntryEpoch = Existing->Status.EntryEpoch;
    Record.Owner = TrustedOwner;
    Record.EntryKey = Key;
    Existing->Tokens.Add(Record.Lease.LeaseId);
    OutLease = Record.Lease;
    Leases.Add(Record.Lease.LeaseId, MoveTemp(Record));
    EnsureMonitor();
    OutFailure = EFableTownGeometryFailure::None;
    return true;
}

/** エンジンの実状態だけを読み、hiddenをremovedとして扱わない。 / 실제 엔진 상태만 읽는다. */
FFableTownGeometryStatus UFableTownStreamingSubsystem::ObserveEntry(int64 EntryKey, const FGeometryEntry& Entry) const
{
    FFableTownGeometryStatus Status;
    Status.EntryEpoch = Entry.Status.EntryEpoch;
    if (bStopping || !GetWorld())
    {
        Status.State = EFableTownGeometryState::Failed;
        Status.Failure = EFableTownGeometryFailure::WorldEnded;
        return Status;
    }

    if (Entry.Descriptor.Mode == EFableTownGeometryMode::ExistingPersistent)
    {
        Status.bReadyVisible = !Entry.bRemoving && IsExpectedPersistentWorld();
        Status.State = Status.bReadyVisible ? EFableTownGeometryState::ReadyVisible : EFableTownGeometryState::Failed;
        Status.Failure =
            Status.bReadyVisible ? EFableTownGeometryFailure::None : EFableTownGeometryFailure::InvalidDescriptor;
        return Status;
    }

    const TObjectPtr<ULevelStreamingDynamic>* Retained = RetainedStreams.Find(EntryKey);
    const ULevelStreamingDynamic* Streaming = Retained ? Retained->Get() : nullptr;
    if (!IsValid(Streaming))
    {
        Status.State = Entry.bRemoving ? EFableTownGeometryState::Removed : EFableTownGeometryState::Failed;
        Status.Failure = Entry.bRemoving ? EFableTownGeometryFailure::None : EFableTownGeometryFailure::LoadFailed;
        return Status;
    }

    const ELevelStreamingState EngineState = Streaming->GetLevelStreamingState();
    if (Entry.bRemoving || Streaming->GetIsRequestingUnloadAndRemoval())
    {
        const bool bAbsent = !GetWorld()->GetStreamingLevels().Contains(Streaming);
        const bool bRemoved = !Streaming->GetLoadedLevel() && (EngineState == ELevelStreamingState::Removed || bAbsent);
        Status.State = bRemoved ? EFableTownGeometryState::Removed : EFableTownGeometryState::Unloading;
        return Status;
    }

    if (EngineState == ELevelStreamingState::FailedToLoad || EngineState == ELevelStreamingState::Removed)
    {
        Status.State = EFableTownGeometryState::Failed;
        Status.Failure = EFableTownGeometryFailure::LoadFailed;
        return Status;
    }

    Status.bReadyVisible = Streaming->GetLoadedLevel() && Streaming->IsLevelLoaded() && Streaming->IsLevelVisible() &&
                           EngineState == ELevelStreamingState::LoadedVisible;
    Status.State = Status.bReadyVisible ? EFableTownGeometryState::ReadyVisible
                                        : (Streaming->IsLevelLoaded() ? EFableTownGeometryState::LoadedNotVisible
                                                                      : EFableTownGeometryState::Loading);
    return Status;
}

/** 정확 token과 현재 연결이 유효한 경우에만 준비 상태를 반환한다. / 正確tokenと現在接続だけを照会する。 */
FFableTownGeometryStatus UFableTownStreamingSubsystem::GetGeometryStatus(const FFableTownGeometryLease& Lease) const
{
    FFableTownGeometryStatus Rejected;
    Rejected.EntryEpoch = Lease.EntryEpoch;
    Rejected.Failure = bStopping ? EFableTownGeometryFailure::WorldEnded : EFableTownGeometryFailure::InvalidOwner;
    const FLeaseRecord* Record = Leases.Find(Lease.LeaseId);
    if (bStopping || !Lease.IsValid() || !Record || !SameLease(Record->Lease, Lease) || !ValidateOwner(Record->Owner))
    {
        return Rejected;
    }

    const FGeometryEntry* Entry = Entries.Find(Record->EntryKey);
    if (!Entry || Entry->Status.EntryEpoch != Lease.EntryEpoch || !Entry->Tokens.Contains(Lease.LeaseId))
    {
        return Rejected;
    }

    return ObserveEntry(Record->EntryKey, *Entry);
}

/** visible/loaded 상태를 동일 조회 경로에서 확인한다. / visible・loadedを同じ照会経路で確認する。 */
bool UFableTownStreamingSubsystem::IsGeometryReadyVisible(const FFableTownGeometryLease& Lease) const
{
    const FFableTownGeometryStatus Status = GetGeometryStatus(Lease);
    return Status.State == EFableTownGeometryState::ReadyVisible && Status.bReadyVisible &&
           Status.Failure == EFableTownGeometryFailure::None;
}

/** 다른 연결이나 이미 해제된 token을 건드리지 않는다. / 他接続・解放済tokenには触れない。 */
bool UFableTownStreamingSubsystem::ReleaseGeometry(const FFableTownGeometryLease& ExactOwnedLease)
{
    FLeaseRecord* Record = Leases.Find(ExactOwnedLease.LeaseId);
    if (bStopping || !ExactOwnedLease.IsValid() || !Record || !SameLease(Record->Lease, ExactOwnedLease))
    {
        return false;
    }

    const int64 Key = Record->EntryKey;
    FGeometryEntry* Entry = Entries.Find(Key);
    if (!Entry || Entry->Status.EntryEpoch != ExactOwnedLease.EntryEpoch ||
        !Entry->Tokens.Contains(ExactOwnedLease.LeaseId))
    {
        return false;
    }

    // 연결이 이미 끝났어도 그 이전 연결의 정확 token 해제는 허용한다.
    // 接続終了後も以前の接続の正確tokenだけは解放できる。
    Entry->Tokens.Remove(ExactOwnedLease.LeaseId);
    Leases.Remove(ExactOwnedLease.LeaseId);
    if (Entry->Tokens.IsEmpty())
    {
        if (Entry->Descriptor.Mode == EFableTownGeometryMode::ExistingPersistent)
        {
            Entries.Remove(Key);
        }
        else
        {
            BeginRemoval(Key, *Entry);
        }
    }

    EnsureMonitor();
    return true;
}

/** 共有エントリの最後のtokenが消えた時だけ自分のstreamを除去する。 / 마지막 token 뒤 자기 stream만 제거한다. */
void UFableTownStreamingSubsystem::BeginRemoval(int64 EntryKey, FGeometryEntry& Entry)
{
    if (Entry.bRemoving || !Entry.Tokens.IsEmpty() ||
        Entry.Descriptor.Mode == EFableTownGeometryMode::ExistingPersistent)
    {
        return;
    }

    Entry.bRemoving = true;
    Entry.Status.State = EFableTownGeometryState::Unloading;
    Entry.Status.bReadyVisible = false;
    TObjectPtr<ULevelStreamingDynamic>* Retained = RetainedStreams.Find(EntryKey);
    ULevelStreamingDynamic* Streaming = Retained ? Retained->Get() : nullptr;
    if (IsValid(Streaming))
    {
        Streaming->SetShouldBeVisible(false);
        Streaming->SetShouldBeLoaded(false);
        Streaming->SetIsRequestingUnloadAndRemoval(true);
    }
}

/** 自分のtimerだけを開始/停止し、他のstreamingやworldをflushしない。 / 자체 timer만 관리한다. */
void UFableTownStreamingSubsystem::EnsureMonitor()
{
    UWorld* World = GetWorld();
    if (!World || bStopping)
    {
        return;
    }

    if (Entries.IsEmpty())
    {
        World->GetTimerManager().ClearTimer(MonitorTimer);
    }
    else if (!World->GetTimerManager().IsTimerActive(MonitorTimer))
    {
        World->GetTimerManager().SetTimer(MonitorTimer, this, &UFableTownStreamingSubsystem::PollGeometry,
                                          MonitorIntervalSeconds, true);
    }
}

/** 最大五つの所有entryを再照会し、通知中の変更はtokenとepochで遮断する。 / 최대 다섯 entry를 재확인한다. */
void UFableTownStreamingSubsystem::PollGeometry()
{
    if (bStopping)
    {
        return;
    }

    TArray<int64> Keys;
    Entries.GetKeys(Keys);
    for (const int64 Key : Keys)
    {
        FGeometryEntry* Entry = Entries.Find(Key);
        if (!Entry)
        {
            continue;
        }

        const int64 Epoch = Entry->Status.EntryEpoch;
        const FFableTownGeometryStatus Observed = ObserveEntry(Key, *Entry);
        const bool bChanged = !SameStatus(Entry->Status, Observed);
        Entry->Status = Observed;
        if (Entry->bRemoving && Observed.State == EFableTownGeometryState::Removed)
        {
            RetainedStreams.Remove(Key);
            Entries.Remove(Key);
            continue;
        }

        if (!bChanged)
        {
            continue;
        }

        const TArray<FGuid> Tokens = Entry->Tokens.Array();
        for (const FGuid& Token : Tokens)
        {
            const FLeaseRecord* Record = Leases.Find(Token);
            if (bStopping || !Record || Record->EntryKey != Key || Record->Lease.EntryEpoch != Epoch ||
                !ValidateOwner(Record->Owner))
            {
                continue;
            }

            const FFableTownGeometryLease Lease = Record->Lease;
            const FFableTownGeometryStatus Current = GetGeometryStatus(Lease);
            GeometryChanged.Broadcast(Lease, Current);
        }
    }

    EnsureMonitor();
}

/** 単一の共有通知口を公開しRPCや移動権限は追加しない。 / 공용 알림만 제공한다. */
FOnFableTownGeometryChanged& UFableTownStreamingSubsystem::OnGeometryChanged()
{
    return GeometryChanged;
}

/** teardown時は先にfenceし、source成功やrollback成功を捏造しない。 / 종료 시 성공 상태를 만들지 않는다. */
void UFableTownStreamingSubsystem::StopAndFence()
{
    if (bStopping)
    {
        return;
    }

    bStopping = true;
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(MonitorTimer);
    }

    GeometryChanged.Clear();
    Leases.Empty();
    Entries.Empty();
    RetainedStreams.Empty();
}
