#include "FableTownStreamingSubsystem.h"
#include "FablePlayerState.h"
#include "FableTownCatalog.h"
#include "FableTownManager.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

/** 저장·네트워크 없이 소유권과 관찰만 검사한다. / 保存・通信なしで所有と観測だけを検査する。 */
struct FFableTownStreamingTestAccess
{
    /** 사용자 파일 쓰기를 명시적으로 차단한다. / ユーザーファイルへの書込みを遮断する。 */
    static void UseMemoryOnly(AFablePlayerState* Profile)
    {
        Profile->bPersistenceEnabled = false;
        Profile->ProfileSlot.Empty();
        Profile->bStorageBlocked = false;
    }

    /** 실제 관찰 경로를 한 번 호출하며 ready 값을 주입하지 않는다. / 実観測を一度呼びreadyは注入しない。 */
    static void Observe(UFableTownStreamingSubsystem* Streaming)
    {
        Streaming->PollGeometry();
    }

    /** 공유 자원이 하나인지 검사한다. / 共有資源が一つかを検査する。 */
    static int32 EntryCount(const UFableTownStreamingSubsystem* Streaming)
    {
        return Streaming->Entries.Num();
    }

    /** 정확한 소유 token 수만 관찰한다. / 正確な所有token数だけを観測する。 */
    static int32 LeaseCount(const UFableTownStreamingSubsystem* Streaming)
    {
        return Streaming->Leases.Num();
    }
};

namespace
{
    /** 연결 소유권을 가진 메모리 프로필이다. / 接続所有権を持つメモリーprofile。 */
    struct FStreamingPlayer
    {
        APlayerController* Controller = nullptr;
        AFablePlayerState* Profile = nullptr;
        FFableTownGeometryLeaseOwner Owner;
    };

    /** 이름만 실제 map과 같은 합성 package이며 실제 map/asset을 로드하지 않는다. / 合成packageで実mapはロードしない。 */
    struct FStreamingMemoryWorld
    {
        UWorld* World = nullptr;
        AFableTownManager* Manager = nullptr;
        UFableTownStreamingSubsystem* Streaming = nullptr;
        TArray<TWeakObjectPtr<APlayerController>> Controllers;
        bool bOwnsContext = false;

        /** 독립 world 이름으로 실제 Editor world와 충돌하지 않는다. / 独立world名でEditor worldとの衝突を防ぐ。 */
        explicit FStreamingMemoryWorld(const TCHAR* PackagePath = TEXT("/Game/Maps/ForestClearing"))
        {
            if (!GEngine)
            {
                return;
            }

            UPackage* Package = CreatePackage(PackagePath);
            const FName UniqueWorldName(
                *FString::Printf(TEXT("TownStreamingMemory_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(false)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(false)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, UniqueWorldName, Package, true, ERHIFeatureLevel::Num,
                                        &Values);
            if (!World)
            {
                return;
            }

            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            bOwnsContext = true;
            Manager = AFableTownManager::Ensure(World);
            Streaming = World->GetSubsystem<UFableTownStreamingSubsystem>();
        }

        /** 자기 controller와 world만 정리한다. / 自分のcontrollerとworldだけを整理する。 */
        ~FStreamingMemoryWorld()
        {
            if (!World)
            {
                return;
            }

            for (const auto& Entry : Controllers)
            {
                if (APlayerController* Player = Entry.Get())
                {
                    if (Manager)
                    {
                        Manager->UnregisterPlayer(Player);
                    }
                    World->RemoveController(Player);
                }
            }
            World->DestroyWorld(false);
            if (bOwnsContext && GEngine)
            {
                GEngine->DestroyWorldContext(World);
            }
        }

        /** 공개 server 연결 등록으로 세대를 발급한다. / 公開server接続登録で世代を発行する。 */
        FStreamingPlayer AddPlayer(int32 Id)
        {
            FStreamingPlayer Result;
            if (!World || !Manager || !Streaming)
            {
                return Result;
            }

            Result.Controller = World->SpawnActor<APlayerController>();
            Result.Profile = World->SpawnActor<AFablePlayerState>();
            if (!Result.Controller || !Result.Profile)
            {
                return Result;
            }

            FFableTownStreamingTestAccess::UseMemoryOnly(Result.Profile);
            Result.Profile->SetPlayerId(Id);
            Result.Profile->bProfileLoaded = true;
            Result.Profile->SetOwner(Result.Controller);
            Result.Controller->SetPlayerState(Result.Profile);
            World->AddController(Result.Controller);
            Controllers.Add(Result.Controller);
            if (!Manager->RegisterOrRefreshConnection(Result.Controller))
            {
                return Result;
            }

            Result.Owner.OwnerPlayerState = Result.Profile;
            Result.Owner.OwnerConnectionGeneration =
                Result.Profile->GetTownConnectionSnapshot().OwnerConnectionGeneration;
            Result.Owner.Purpose = EFableTownGeometryLeasePurpose::Presence;
            return Result;
        }

        /** 실제 catalog의 고정 Legacy 계약을 사용한다. / 実catalogの固定Legacy契約を使う。 */
        FFableTownGeometryDescriptor Legacy() const
        {
            FFableTownGeometryDescriptor Descriptor;
            if (!Manager || !Manager->GetCatalog())
            {
                return Descriptor;
            }

            const FFableTownDefinition* Definition = Manager->GetCatalog()->FindTown(EFableTownId::LegacyVillage);
            if (!Definition)
            {
                return Descriptor;
            }

            Descriptor.Mode = EFableTownGeometryMode::ExistingPersistent;
            Descriptor.TownId = Definition->TownId;
            Descriptor.CatalogVersion = Definition->CatalogVersion;
            Descriptor.LocalToWorld = Definition->LocalToWorld;
            Descriptor.FixedInstanceName =
                FName(*FString::Printf(TEXT("FableTown_LegacyVillage_Geometry_v%d"), Definition->CatalogVersion));
            Descriptor.ContentIdentity = TEXT("LegacyPersistentV1");
            return Descriptor;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownStreamingSharedPersistent,
                                 "Fablewood.TownStreaming.SharedPersistentOwnership",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 중복/다중 소유와 이전 token의 격리를 실제 persistent 관찰로 검사한다. / 共有所有と旧token隔離を検査する。 */
bool FFableTownStreamingSharedPersistent::RunTest(const FString& Parameters)
{
    FStreamingMemoryWorld Fixture;
    if (!TestNotNull(TEXT("Memory world"), Fixture.World) || !TestNotNull(TEXT("World subsystem"), Fixture.Streaming) ||
        !TestNotNull(TEXT("Town manager"), Fixture.Manager))
    {
        return false;
    }

    const FStreamingPlayer A = Fixture.AddPlayer(9101);
    const FStreamingPlayer B = Fixture.AddPlayer(9102);
    const auto BeforeStreams = Fixture.World->GetStreamingLevels();
    ULevel* const Persistent = Fixture.World->PersistentLevel;
    const bool bBeforeVisible = Persistent->bIsVisible;
    const FFableTownGeometryDescriptor Descriptor = Fixture.Legacy();
    EFableTownGeometryFailure Failure;
    FFableTownGeometryLease LeaseA;
    FFableTownGeometryLease DuplicateA;
    FFableTownGeometryLease LeaseB;
    int32 ReadyEvents = 0;
    const FDelegateHandle Handle = Fixture.Streaming->OnGeometryChanged().AddLambda(
        [&ReadyEvents](const FFableTownGeometryLease&, const FFableTownGeometryStatus& Status)
        {
            if (Status.State == EFableTownGeometryState::ReadyVisible && Status.bReadyVisible)
            {
                ++ReadyEvents;
            }
        });

    if (!TestTrue(TEXT("First authenticated owner"),
                  Fixture.Streaming->AcquireGeometry(Descriptor, A.Owner, LeaseA, Failure)) ||
        !TestTrue(TEXT("Duplicate owner request"),
                  Fixture.Streaming->AcquireGeometry(Descriptor, A.Owner, DuplicateA, Failure)) ||
        !TestTrue(TEXT("Second authenticated owner"),
                  Fixture.Streaming->AcquireGeometry(Descriptor, B.Owner, LeaseB, Failure)))
    {
        Fixture.Streaming->OnGeometryChanged().Remove(Handle);
        return false;
    }

    TestEqual(TEXT("Duplicate lease is idempotent"), DuplicateA.LeaseId, LeaseA.LeaseId);
    TestTrue(TEXT("Owners have independent tokens"), LeaseA.LeaseId != LeaseB.LeaseId);
    TestEqual(TEXT("Both owners share one entry epoch"), LeaseA.EntryEpoch, LeaseB.EntryEpoch);
    TestEqual(TEXT("One shared entry"), FFableTownStreamingTestAccess::EntryCount(Fixture.Streaming), 1);
    TestEqual(TEXT("Two tokens, not duplicate refcounts"), FFableTownStreamingTestAccess::LeaseCount(Fixture.Streaming),
              2);
    FFableTownStreamingTestAccess::Observe(Fixture.Streaming);
    TestEqual(TEXT("Initial readiness notifies both owners"), ReadyEvents, 2);
    TestTrue(TEXT("Exact owner A can release"), Fixture.Streaming->ReleaseGeometry(LeaseA));
    TestFalse(TEXT("Duplicate release cannot decrement owner B"), Fixture.Streaming->ReleaseGeometry(LeaseA));
    TestTrue(TEXT("Owner B still observes ready geometry"), Fixture.Streaming->IsGeometryReadyVisible(LeaseB));
    TestTrue(TEXT("Last exact token releases bookkeeping"), Fixture.Streaming->ReleaseGeometry(LeaseB));
    TestEqual(TEXT("No stale entry remains"), FFableTownStreamingTestAccess::EntryCount(Fixture.Streaming), 0);

    FFableTownGeometryLease Replacement;
    TestTrue(TEXT("Replacement can acquire persistent geometry"),
             Fixture.Streaming->AcquireGeometry(Descriptor, A.Owner, Replacement, Failure));
    TestTrue(TEXT("Replacement has a new epoch"), Replacement.EntryEpoch > LeaseA.EntryEpoch);
    TestFalse(TEXT("Late old token cannot release replacement"), Fixture.Streaming->ReleaseGeometry(LeaseA));
    TestFalse(TEXT("Late old readiness is rejected"), Fixture.Streaming->IsGeometryReadyVisible(LeaseA));
    TestTrue(TEXT("Replacement remains ready"), Fixture.Streaming->IsGeometryReadyVisible(Replacement));
    TestTrue(TEXT("Replacement exact release"), Fixture.Streaming->ReleaseGeometry(Replacement));
    Fixture.Streaming->OnGeometryChanged().Remove(Handle);

    TestTrue(TEXT("No streaming level was loaded or removed"), BeforeStreams == Fixture.World->GetStreamingLevels());
    TestTrue(TEXT("Persistent level identity unchanged"), Persistent == Fixture.World->PersistentLevel);
    TestEqual(TEXT("Persistent level was not hidden"), static_cast<bool>(Persistent->bIsVisible), bBeforeVisible);
    AddInfo(TEXT("Synthetic memory package only: no authored map, floor, network, or streaming-load validation."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownStreamingConnectionFence,
                                 "Fablewood.TownStreaming.ConnectionAndExactRelease",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 연결 만료는 준비 판정을 막지만 이전 소유 token의 정리는 허용한다. / 失効接続を遮断し旧token整理は許す。 */
bool FFableTownStreamingConnectionFence::RunTest(const FString& Parameters)
{
    FStreamingMemoryWorld Fixture;
    if (!TestNotNull(TEXT("Subsystem"), Fixture.Streaming) || !TestNotNull(TEXT("Manager"), Fixture.Manager))
    {
        return false;
    }

    FStreamingPlayer Player = Fixture.AddPlayer(9201);
    if (!TestTrue(TEXT("Current connection issued"), Player.Owner.OwnerConnectionGeneration > 0))
    {
        return false;
    }

    EFableTownGeometryFailure Failure;
    FFableTownGeometryLease OldLease;
    FFableTownGeometryLease Rejected;
    const FFableTownGeometryDescriptor Descriptor = Fixture.Legacy();
    FFableTownGeometryLeaseOwner Invalid;
    TestFalse(TEXT("Unowned request rejected"),
              Fixture.Streaming->AcquireGeometry(Descriptor, Invalid, Rejected, Failure));
    TestTrue(TEXT("Invalid owner reason"), Failure == EFableTownGeometryFailure::InvalidOwner);
    Invalid = Player.Owner;
    Invalid.OwnerConnectionGeneration++;
    TestFalse(TEXT("Guessed generation rejected"),
              Fixture.Streaming->AcquireGeometry(Descriptor, Invalid, Rejected, Failure));
    Invalid = Player.Owner;
    Invalid.Purpose = EFableTownGeometryLeasePurpose::SourceRetention;
    TestFalse(TEXT("Source retention requires transaction identity"),
              Fixture.Streaming->AcquireGeometry(Descriptor, Invalid, Rejected, Failure));
    if (!TestTrue(TEXT("Valid initial lease"),
                  Fixture.Streaming->AcquireGeometry(Descriptor, Player.Owner, OldLease, Failure)))
    {
        return false;
    }

    // 종료된 연결은 되살리지 않고 실제 재접속처럼 새 controller로 소유권을 교체한다.
    APlayerController* const RetiredController = Player.Controller;
    Fixture.Manager->UnregisterPlayer(RetiredController);
    TestFalse(TEXT("Disconnected registry cannot report ready"), Fixture.Streaming->IsGeometryReadyVisible(OldLease));
    TestFalse(TEXT("Retired controller cannot reactivate its connection"),
              Fixture.Manager->RegisterOrRefreshConnection(RetiredController));
    APlayerController* Replacement = Fixture.World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Reconnection creates a separate controller"), Replacement))
    {
        return false;
    }

    Fixture.World->AddController(Replacement);
    Fixture.Controllers.Add(Replacement);
    Player.Profile->SetOwner(Replacement);
    Replacement->SetPlayerState(Player.Profile);
    Player.Controller = Replacement;
    TestTrue(TEXT("Replacement controller receives a new connection"),
             Fixture.Manager->RegisterOrRefreshConnection(Replacement));
    FFableTownGeometryLeaseOwner NewOwner = Player.Owner;
    NewOwner.OwnerConnectionGeneration = Player.Profile->GetTownConnectionSnapshot().OwnerConnectionGeneration;
    TestTrue(TEXT("Connection generation changed"),
             NewOwner.OwnerConnectionGeneration > Player.Owner.OwnerConnectionGeneration);
    TestFalse(TEXT("Old generation cannot reacquire"),
              Fixture.Streaming->AcquireGeometry(Descriptor, Player.Owner, Rejected, Failure));

    FFableTownGeometryLease NewLease;
    if (!TestTrue(TEXT("Current generation acquires independent lease"),
                  Fixture.Streaming->AcquireGeometry(Descriptor, NewOwner, NewLease, Failure)))
    {
        return false;
    }
    FFableTownGeometryLease Forged = NewLease;
    Forged.EntryEpoch++;
    TestFalse(TEXT("Forged epoch cannot release real token"), Fixture.Streaming->ReleaseGeometry(Forged));
    TestTrue(TEXT("Old exact token can be cleaned after reconnection"), Fixture.Streaming->ReleaseGeometry(OldLease));
    TestTrue(TEXT("Old cleanup preserves current token"), Fixture.Streaming->IsGeometryReadyVisible(NewLease));
    Fixture.Manager->UnregisterPlayer(RetiredController);
    TestTrue(TEXT("Late old logout preserves the current lease"), Fixture.Streaming->IsGeometryReadyVisible(NewLease));

    FFableTownGeometryLeaseOwner RetentionOwner = NewOwner;
    RetentionOwner.Purpose = EFableTownGeometryLeasePurpose::SourceRetention;
    RetentionOwner.TransactionId = FGuid::NewGuid();
    FFableTownGeometryLease RetentionLease;
    TestTrue(TEXT("Transaction retains source independently"),
             Fixture.Streaming->AcquireGeometry(Descriptor, RetentionOwner, RetentionLease, Failure));
    TestTrue(TEXT("Releasing presence does not release source"), Fixture.Streaming->ReleaseGeometry(NewLease));
    TestTrue(TEXT("Source is retained until exact completion cleanup"),
             Fixture.Streaming->IsGeometryReadyVisible(RetentionLease));
    Fixture.Manager->UnregisterPlayer(Player.Controller);
    Player.Profile->SetOwner(nullptr);
    TestFalse(TEXT("Invalid PS owner cannot use retained source"),
              Fixture.Streaming->IsGeometryReadyVisible(RetentionLease));
    TestTrue(TEXT("Exact cleanup does not depend on live PS owner"),
             Fixture.Streaming->ReleaseGeometry(RetentionLease));
    TestEqual(TEXT("No ownership leak"), FFableTownStreamingTestAccess::LeaseCount(Fixture.Streaming), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownStreamingAdmission, "Fablewood.TownStreaming.DescriptorAndAdoptionGuards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 미승인 경로/변환/마을은 engine load 호출 전에 거절한다. / 未承認path・変換・村はload前に拒否する。 */
bool FFableTownStreamingAdmission::RunTest(const FString& Parameters)
{
    FStreamingMemoryWorld Fixture;
    if (!TestNotNull(TEXT("Subsystem"), Fixture.Streaming) || !TestNotNull(TEXT("Manager"), Fixture.Manager))
    {
        return false;
    }

    const FStreamingPlayer Player = Fixture.AddPlayer(9301);
    const auto BeforeStreams = Fixture.World->GetStreamingLevels();
    const FFableTownGeometryDescriptor Legacy = Fixture.Legacy();
    FFableTownGeometryLease Existing;
    EFableTownGeometryFailure Failure;
    if (!TestTrue(TEXT("Existing valid lease"),
                  Fixture.Streaming->AcquireGeometry(Legacy, Player.Owner, Existing, Failure)))
    {
        return false;
    }

    TArray<FFableTownGeometryDescriptor> Conflicts;
    FFableTownGeometryDescriptor Changed = Legacy;
    Changed.ContentIdentity = TEXT("UnapprovedContent");
    Conflicts.Add(Changed);
    Changed = Legacy;
    Changed.FixedInstanceName = TEXT("PerPlayerDuplicate");
    Conflicts.Add(Changed);
    Changed = Legacy;
    Changed.LocalToWorld.SetTranslation(FVector(1, 0, 0));
    Conflicts.Add(Changed);
    Changed = Legacy;
    Changed.CatalogVersion++;
    Conflicts.Add(Changed);
    Changed = Legacy;
    Changed.Mode = EFableTownGeometryMode::StreamedLevel;
    Changed.GeometryLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Maps/ForestClearing.ForestClearing")));
    Conflicts.Add(Changed);
    for (const FFableTownGeometryDescriptor& Conflict : Conflicts)
    {
        FFableTownGeometryLease Rejected;
        TestFalse(TEXT("Conflicting catalog description rejected"),
                  Fixture.Streaming->AcquireGeometry(Conflict, Player.Owner, Rejected, Failure));
        TestFalse(TEXT("Rejected description receives no token"), Rejected.IsValid());
        TestTrue(TEXT("Rejected conflict preserves existing owner"),
                 Fixture.Streaming->IsGeometryReadyVisible(Existing));
    }

    for (const FFableTownDefinition& Definition : Fixture.Manager->GetCatalog()->GetDefinitions())
    {
        if (Definition.TownId == EFableTownId::LegacyVillage)
        {
            continue;
        }
        FFableTownGeometryDescriptor Candidate;
        Candidate.Mode = EFableTownGeometryMode::StreamedLevel;
        Candidate.TownId = Definition.TownId;
        Candidate.CatalogVersion = Definition.CatalogVersion;
        Candidate.GeometryLevel = Definition.GeometryLevel;
        Candidate.LocalToWorld = Definition.LocalToWorld;
        Candidate.FixedInstanceName = TEXT("UnadoptedCandidate");
        Candidate.ContentIdentity = TEXT("NotAdopted");
        FFableTownGeometryLease Rejected;
        TestFalse(TEXT("Unadopted town denied"),
                  Fixture.Streaming->AcquireGeometry(Candidate, Player.Owner, Rejected, Failure));
        TestTrue(TEXT("Adoption remains a mandatory gate"), Failure == EFableTownGeometryFailure::GeometryNotAdopted);
    }
    TestEqual(TEXT("No rejected request adds a token"), FFableTownStreamingTestAccess::LeaseCount(Fixture.Streaming),
              1);
    TestTrue(TEXT("No duplicate persistent or candidate streaming level"),
             BeforeStreams == Fixture.World->GetStreamingLevels());
    Fixture.Streaming->ReleaseGeometry(Existing);

    FStreamingMemoryWorld WrongPackage(TEXT("/Game/TownStreamingMemory/ForestClearing"));
    if (!TestNotNull(TEXT("Wrong package fixture subsystem"), WrongPackage.Streaming))
    {
        return false;
    }
    const FStreamingPlayer Other = WrongPackage.AddPlayer(9302);
    FFableTownGeometryLease WrongLease;
    TestFalse(TEXT("Matching map basename is insufficient"),
              WrongPackage.Streaming->AcquireGeometry(WrongPackage.Legacy(), Other.Owner, WrongLease, Failure));
    TestTrue(TEXT("Wrong persistent package rejected"), Failure == EFableTownGeometryFailure::InvalidDescriptor);
    AddInfo(
        TEXT("All new town content remains unadopted. This fixture does not load maps or prove physical geometry."));
    return true;
}
#endif
