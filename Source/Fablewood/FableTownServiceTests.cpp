#include "FableCharacter.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePartyManager.h"
#include "FablePlayerState.h"
#include "FableTownManager.h"
#include "FableTownCatalog.h"
#include "FableProfileSchema.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include <limits>

/** 저장·HTTP 없이 실제 서버 소유권 경계를 연결한다. / 保存・HTTPなしでサーバー所有権の境界を接続する。 */
struct FFableTownServiceTestAccess
{
    /** 사용자 파일과 HTTP를 만들지 않는다. / ユーザーファイルとHTTPを生成しない。 */
    static void MemoryProfile(AFablePlayerState* Profile)
    {
        Profile->bPersistenceEnabled = false;
        Profile->ProfileSlot.Empty();
        Profile->bStorageBlocked = false;
        Profile->bProfileLoaded = true;
    }

    /** 합성 시간의 요청 간격만 초기화하고 production 변경 함수를 호출한다. / 合成時刻の要求間隔だけを初期化する。 */
    static void NextMutation(AFablePlayerState* Profile)
    {
        Profile->LastMutationTime = -100.0;
    }

    /** 메모리 상품의 다음 식별자만 명시해 실제 제작을 관찰한다. / 実制作を観察するため次の識別子だけを指定する。 */
    static void SeedNextItem(AFablePlayerState* Profile, int32 NextId)
    {
        Profile->NextItemId = NextId;
    }

    /** 독립 월드의 동일 서버 정본을 사용한다. / 独立worldの同一サーバー正本を使用する。 */
    static void Attach(AFableGameMode* Mode, AFableGameState* State)
    {
        Mode->GameState = State;
        Mode->GetWorld()->SetGameState(State);
    }
};

namespace
{
    /** 허용한 비용과 보상 외의 재화·진행·장비를 그대로 비교한다. / 許可した費用と報酬以外の通貨・進行・装備を比較する。 */
    struct FServiceMutationSnapshot
    {
        int32 Gold = 0;
        int32 Ore = 0;
        int32 Essence = 0;
        int32 HealthPotions = 0;
        TArray<int32> PreservedCounters;
        TArray<FFableItem> Items;
        TArray<FFableItem> Stash;

        /** 실제 관측 값을 복사하며 저장이나 재료를 만들지 않는다. / 観測値だけを複製し保存や素材を作成しない。 */
        explicit FServiceMutationSnapshot(const AFablePlayerState* Profile)
        {
            Gold = Profile->Gold;
            Ore = Profile->Ore;
            Essence = Profile->Essence;
            HealthPotions = Profile->HealthPotions;
            PreservedCounters = {Profile->RootCores,
                                 Profile->MoonCrystals,
                                 Profile->ManaPotions,
                                 Profile->EquippedWeaponId,
                                 Profile->EquippedArmorId,
                                 int32(Profile->HeroClass),
                                 Profile->Level,
                                 Profile->Experience,
                                 Profile->DungeonCompletions,
                                 Profile->MoonwellCompletions,
                                 int32(Profile->ForestQuest),
                                 int32(Profile->DungeonQuest),
                                 int32(Profile->MoonwellQuest),
                                 Profile->QuestKills,
                                 Profile->StoryChapter,
                                 Profile->StoryObjectMask,
                                 Profile->StorySealMask};
            Items = Profile->Inventory;
            Stash = Profile->StashItems;
        }

        /** UUID·옵션·내구도를 포함한 전체 반사 필드를 비교한다. / UUID・option・耐久を含む全反射fieldを比較する。 */
        static bool SameItems(const TArray<FFableItem>& Expected, const TArray<FFableItem>& Actual)
        {
            if (Expected.Num() != Actual.Num())
            {
                return false;
            }

            for (int32 Index = 0; Index < Expected.Num(); ++Index)
            {
                if (!FFableItem::StaticStruct()->CompareScriptStruct(&Expected[Index], &Actual[Index], 0))
                {
                    return false;
                }
            }

            return true;
        }

        /** 변경을 허용한 필드도 명시한 예상 값과 정확히 일치해야 한다. / 変更許可fieldも明示した期待値と厳密に一致する。 */
        bool Matches(const AFablePlayerState* Profile) const
        {
            const FServiceMutationSnapshot Current(Profile);
            return Gold == Current.Gold && Ore == Current.Ore && Essence == Current.Essence &&
                   HealthPotions == Current.HealthPotions && PreservedCounters == Current.PreservedCounters &&
                   SameItems(Items, Current.Items) && SameItems(Stash, Current.Stash);
        }
    };

    /** 실제 소유 PC·PS·pawn의 메모리 연결이다. / 現在の所有関係を持つメモリーfixture。 */
    struct FServicePlayer
    {
        APlayerController* Controller = nullptr;
        AFablePlayerState* Profile = nullptr;
        AFableCharacter* Hero = nullptr;
    };

    /** 합성 패키지의 물리 상자로 실제 맵 로드 증거가 아니다. / 合成パッケージの物理箱で実マップの証拠ではない。 */
    struct FServiceWorld
    {
        UWorld* World = nullptr;
        AFableGameState* State = nullptr;
        AFableGameMode* Mode = nullptr;
        AFablePartyManager* Parties = nullptr;
        AFableTownManager* Towns = nullptr;
        UBoxComponent* Ground = nullptr;
        TArray<TWeakObjectPtr<APlayerController>> Controllers;

        /** 실제 physics scene과 서버 정본만 구성한다. / physics sceneとサーバー正本だけを構成する。 */
        FServiceWorld()
        {
            if (!GEngine)
            {
                return;
            }

            UPackage* Package = CreatePackage(TEXT("/Game/Maps/ForestClearing"));
            const FName WorldName = MakeUniqueObjectName(Package, UWorld::StaticClass(), TEXT("TownServiceMemory"));
            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(true)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(true)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, WorldName, Package, true, ERHIFeatureLevel::Num, &Values);
            if (!World)
            {
                return;
            }

            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            State = World->SpawnActor<AFableGameState>();
            Mode = World->SpawnActor<AFableGameMode>();
            Parties = World->SpawnActor<AFablePartyManager>();
            if (State && Mode && Parties)
            {
                FFableTownServiceTestAccess::Attach(Mode, State);
                Towns = AFableTownManager::Ensure(World);
                Ground = AddBox(FVector(0, 0, -10), FVector(10000, 10000, 10), true);
            }
        }

        /** 만든 연결과 월드만 해제하고 패키지는 저장하지 않는다. / 作成した接続とworldだけを解放する。 */
        ~FServiceWorld()
        {
            if (!World)
            {
                return;
            }

            for (const auto& Entry : Controllers)
            {
                if (APlayerController* Controller = Entry.Get())
                {
                    World->RemoveController(Controller);
                }
            }

            World->DestroyWorld(false);
            if (GEngine)
            {
                GEngine->DestroyWorldContext(World);
            }
        }

        /** 실제 엔진 query용 충돌을 만든다. / 実際のengine query向けの衝突を作成する。 */
        UBoxComponent* AddBox(const FVector& Location, const FVector& Extent, bool bAuthoredGround = false)
        {
            AActor* Actor = World->SpawnActor<AActor>();
            UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Box);
            Box->SetBoxExtent(Extent);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            if (bAuthoredGround)
            {
                // Legacy 지지 계약을 재현한 합성 상자다. / Legacyの支持契約を再現する合成箱。
                Box->ComponentTags.Add(TEXT("Meadow"));
            }
            Box->RegisterComponent();
            Actor->SetActorLocation(Location);
            return Box;
        }

        /** 실제 소유 관계로 서버 연결 세대를 발급한다. / 実所有関係から接続世代を発行する。 */
        FServicePlayer AddPlayer(int32 Id, FVector Position = FableRPG::PortalLocation())
        {
            FServicePlayer Player;
            if (!World || !Towns || !Parties)
            {
                return Player;
            }

            Player.Controller = World->SpawnActor<APlayerController>();
            Player.Profile = World->SpawnActor<AFablePlayerState>();
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player.Hero =
                World->SpawnActor<AFableCharacter>(Position + FVector(0, 0, 110), FRotator::ZeroRotator, Parameters);
            if (!Player.Controller || !Player.Profile || !Player.Hero)
            {
                return Player;
            }

            FFableTownServiceTestAccess::MemoryProfile(Player.Profile);
            Player.Profile->SetPlayerId(Id);
            Player.Profile->SetPlayerName(FString::Printf(TEXT("TownServiceMemory%d"), Id));
            Player.Profile->SetOwner(Player.Controller);
            Player.Controller->SetPlayerState(Player.Profile);
            Player.Controller->Possess(Player.Hero);
            State->AddPlayerState(Player.Profile);
            World->AddController(Player.Controller);
            Controllers.Add(Player.Controller);
            Parties->RegisterPlayer(Player.Controller);

            const float HalfHeight = Player.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            Position.Z = HalfHeight + 2.15f;
            Player.Hero->SetActorLocation(Position);
            Player.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
            Towns->RegisterOrRefreshConnection(Player.Controller);
            Towns->RefreshPhysicalPresence(Player.Controller);
            return Player;
        }

        /** 실제 발급된 접속을 확인한다. / 実際に発行された接続を確認する。 */
        bool Valid(const FServicePlayer& Player) const
        {
            FFableTownConnectionSnapshot Snapshot;
            return Towns && Player.Controller && Player.Profile && Player.Hero &&
                   Towns->GetConnectionSnapshot(Player.Profile, Snapshot) && Snapshot.ConnectionNonce.IsValid() &&
                   Snapshot.OwnerConnectionGeneration > 0 && Snapshot.PresenceRevision > 0;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownServiceCatalogTest,
                                 "Fablewood.Native.TownService.CatalogScopeReadinessAndFiniteRange",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 純粋照会と距離境界のfixtureであり実マップ・接地成功の証拠ではない。 / 순수 조회이며 실제 맵 접지 증거가 아니다. */
bool FFableTownServiceCatalogTest::RunTest(const FString& Parameters)
{
    UFableTownCatalog* Catalog = NewObject<UFableTownCatalog>();
    const auto* Legacy = Catalog->FindTown(EFableTownId::LegacyVillage);
    if (!TestNotNull(TEXT("Legacy server definition positive control"), Legacy))
    {
        return false;
    }

    const EFableTownServiceId Services[] = {EFableTownServiceId::QuestGuide, EFableTownServiceId::Workshop,
                                            EFableTownServiceId::Merchant, EFableTownServiceId::ExpeditionPortal};
    const FName Anchors[] = {TEXT("Service.Board"), TEXT("Service.Forge"), TEXT("Service.Merchant"),
                             TEXT("Portal.Expedition")};
    const FVector Locations[] = {FableRPG::BoardLocation(), FableRPG::ForgeLocation(), FableRPG::MerchantLocation(),
                                 FableRPG::PortalLocation()};
    FFableTownServiceRef Request;
    Request.TownId = EFableTownId::LegacyVillage;
    Request.CatalogVersion = Legacy->CatalogVersion;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Services); ++Index)
    {
        Request.ServiceId = Services[Index];
        FFableTownServiceDescriptor Service;
        TestTrue(TEXT("Each role resolves its existing catalog anchor without new placement"),
                 Catalog->ResolveService(Request, Service) && Service.Anchor.AnchorId == Anchors[Index] &&
                     Service.FloorId == FName(TEXT("Ground")) && Service.WorldLocation.Equals(Locations[Index]));
        TestFalse(TEXT("A service anchor never becomes a durable safe respawn"),
                  FableProfileSchema::IsSafeAnchor(Service.Anchor));
    }

    Request.ServiceId = EFableTownServiceId::QuestGuide;
    FFableTownServiceDescriptor Guide;
    FFableTownLocationContext Physical;
    TestTrue(TEXT("Known guide resolves at ready Legacy floor"),
             Catalog->ResolveService(Request, Guide) &&
                 Catalog->ResolveLocation(Guide.WorldLocation + FVector(0, 0, 100), Physical));
    const FVector Within = Guide.WorldLocation + FVector(330, 0, 100);
    TestTrue(TEXT("Finite inclusive horizontal boundary preserves the current radius"),
             Guide.IsInRange(Physical, Within));
    TestFalse(TEXT("A point past the radius is rejected"),
              Guide.IsInRange(Physical, Guide.WorldLocation + FVector(330.1, 0, 100)));
    TestFalse(TEXT("The vertical boundary remains exclusive"),
              Guide.IsInRange(Physical, Guide.WorldLocation + FVector(0, 0, 300)));
    TestFalse(TEXT("Non-finite pawn positions cannot pass distance comparison"),
              Guide.IsInRange(Physical, FVector(std::numeric_limits<double>::infinity(), 0, 100)));

    // 같은 XY라도 다른 마을·층·연결부·버전은 서비스 범위가 아니다. / 同じXYでも村・階・接続部・版の差を拒否する。
    auto Wrong = Physical;
    Wrong.TownId = EFableTownId::Sunbrook;
    TestFalse(TEXT("Another town cannot reuse the guide coordinates"), Guide.IsInRange(Wrong, Within));
    Wrong = Physical;
    Wrong.FloorId = TEXT("Upper");
    TestFalse(TEXT("A different floor at identical XYZ cannot use the guide"), Guide.IsInRange(Wrong, Within));
    Wrong = Physical;
    Wrong.ConnectorId = TEXT("Slope");
    TestFalse(TEXT("A connector is not the supported service floor"), Guide.IsInRange(Wrong, Within));
    Wrong = Physical;
    ++Wrong.CatalogVersion;
    TestFalse(TEXT("Stale catalog context cannot use a current service"), Guide.IsInRange(Wrong, Within));
    auto InvalidRange = Guide;
    InvalidRange.InteractionRadiusCm = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN radius is rejected"), InvalidRange.IsInRange(Physical, Within));
    InvalidRange = Guide;
    InvalidRange.InteractionRadiusCm = 331;
    TestFalse(TEXT("A descriptor cannot widen the server radius"), InvalidRange.IsInRange(Physical, Within));
    InvalidRange = Guide;
    InvalidRange.VerticalRangeCm = std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Infinite vertical range is rejected"), InvalidRange.IsInRange(Physical, Within));

    FFableTownServiceDescriptor Rejected = Guide;
    auto Unknown = Request;
    Unknown.ServiceId = static_cast<EFableTownServiceId>(255);
    TestFalse(TEXT("Unknown roles never fall back to a board"), Catalog->ResolveService(Unknown, Rejected));
    TestFalse(TEXT("Failed lookup clears a previous successful descriptor"), Rejected.IsValid());
    Unknown = Request;
    Unknown.TownId = static_cast<EFableTownId>(255);
    TestFalse(TEXT("Unknown towns never fall back to Legacy"), Catalog->ResolveService(Unknown, Rejected));
    Unknown = Request;
    ++Unknown.CatalogVersion;
    TestFalse(TEXT("A stale service reference is rejected"), Catalog->ResolveService(Unknown, Rejected));

    // 후보는 Entry만 있으며 서비스 배치·채택·bootstrap 변경을 부여하지 않는다. / 候補の配置・採用・bootstrapを変更しない。
    const EFableTownId Candidates[] = {EFableTownId::Sunbrook, EFableTownId::Starbough, EFableTownId::Snowbloom,
                                       EFableTownId::Eldergrove};
    for (const EFableTownId Candidate : Candidates)
    {
        const auto* Definition = Catalog->FindTown(Candidate);
        TestTrue(TEXT("Each candidate remains unready and has only arrival anchors"),
                 Definition && !Definition->bGeometryReady && Definition->Anchors.Num() == 3);
        auto CandidateRequest = Request;
        CandidateRequest.TownId = Candidate;
        TestFalse(TEXT("Unready town has no usable guide"), Catalog->ResolveService(CandidateRequest, Rejected));
        TestFalse(TEXT("Inspection cannot invent absent candidate service anchors"),
                  Catalog->ResolveService(CandidateRequest, Rejected, false));
    }
    TestTrue(TEXT("Bootstrap policy remains Legacy until separate adoption"),
             FableProfileSchema::BootstrapAnchor(EFableTownBootstrapPolicy::LegacyVillageV1).TownId ==
                 EFableTownId::LegacyVillage);

    // 메모리 복사만 미준비로 바꿔 실제 readiness 분기를 검사한다. / メモリーコピーだけで未準備分岐を検査する。
    auto Unready = *Legacy;
    Unready.bGeometryReady = false;
    TestFalse(TEXT("Even a declared service is unusable while geometry is unready"),
              UFableTownCatalog::ResolveDefinitionService(Unready, Request, Rejected));
    TestTrue(TEXT("Read-only inspection preserves explicit unready state"),
             UFableTownCatalog::ResolveDefinitionService(Unready, Request, Rejected, false) &&
                 !Rejected.bGeometryReady);
    TestFalse(TEXT("An inspected unready service cannot authorize interaction"), Rejected.IsInRange(Physical, Within));
    auto Missing = *Legacy;
    Missing.Anchors.RemoveAll(
        [](const FFableTownAnchorDefinition& Anchor)
        {
            return Anchor.AnchorId == FName(TEXT("Service.Board"));
        });
    TestFalse(TEXT("A missing service does not borrow an arrival anchor"),
              UFableTownCatalog::ResolveDefinitionService(Missing, Request, Rejected));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableTownServiceAuthorityTest,
                                 "Fablewood.Native.TownService.CurrentOwnerAndSupportedFloor",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 実queryの合成床と所有関係を検査し、実村・ネットワーク・UI成功を主張しない。 / 합성 바닥·소유권 검사는 실제 플레이와 구분한다. */
bool FFableTownServiceAuthorityTest::RunTest(const FString& Parameters)
{
    FServiceWorld Fixture;
    const auto Owner = Fixture.AddPlayer(1401, FableRPG::BoardLocation());
    const auto Other = Fixture.AddPlayer(1402, FableRPG::MerchantLocation());
    if (!TestTrue(TEXT("Two current owned connections positive control"), Fixture.Valid(Owner) && Fixture.Valid(Other)))
    {
        return false;
    }

    FFableTownServiceRef Request;
    Request.TownId = EFableTownId::LegacyVillage;
    Request.ServiceId = EFableTownServiceId::QuestGuide;
    Request.CatalogVersion = 1;
    FFableTownServiceDescriptor Service;
    const auto Before = Owner.Profile->GetTownConnectionSnapshot();
    const auto SafeAnchor = Owner.Profile->GetLastSafeTownAnchor();
    const auto SafePosition = Owner.Hero->GetTownCombatState().SafeSpawnLocation;
    TestTrue(TEXT("Actual owner at the matching authored support can use the guide"),
             Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    TestFalse(TEXT("Another current player cannot borrow the first player's proximity"),
              Fixture.Mode->CanUseTownService(Other.Controller, Request, Service));
    TestFalse(TEXT("Denied authority clears any previously successful descriptor"), Service.IsValid());

    // profile 소유자와 pawn controller를 각각 바꿔 권한 음성 대조를 만든다. / profileとpawn所有者を個別に変更する。
    Owner.Profile->SetOwner(Other.Controller);
    TestFalse(TEXT("A foreign profile owner cannot use the service"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Profile->SetOwner(Owner.Controller);
    Owner.Controller->UnPossess();
    TestFalse(TEXT("An unpossessed current connection has no service position"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Controller->Possess(Owner.Hero);
    Fixture.World->RemoveController(Owner.Controller);
    TestFalse(TEXT("A retired controller outside the current world roster cannot use services"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Fixture.World->AddController(Owner.Controller);
    TestTrue(TEXT("Restored exact owner and roster remain a positive control"),
             Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));

    // 범위 안에서 같은 XY·walking 상태라도 새 실제 바닥 query가 실패하면 거절한다. / 範囲内でも新しい床queryを必須とする。
    const FVector GroundedLocation = Owner.Hero->GetActorLocation();
    Owner.Hero->SetActorLocation(GroundedLocation + FVector(0, 0, 100));
    Owner.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    TestFalse(TEXT("Floating above the correct XY within interaction range is rejected"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Hero->SetActorLocation(GroundedLocation);
    Owner.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TestFalse(TEXT("A falling pawn cannot borrow cached floor state"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Fixture.Ground->ComponentTags.Reset();
    TestFalse(TEXT("A walkable but unauthored supporting component is rejected"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Fixture.Ground->ComponentTags.Add(TEXT("Meadow"));
    Fixture.Ground->SetWorldLocation(FVector(0, 0, 90));
    Owner.Hero->SetActorLocation(GroundedLocation + FVector(0, 0, 100));
    TestFalse(TEXT("A tagged platform at the wrong authored height is rejected"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Fixture.Ground->SetWorldLocation(FVector(0, 0, -10));
    Owner.Hero->SetActorLocation(GroundedLocation);
    Owner.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    TestTrue(TEXT("Restored authored support remains a positive control"),
             Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));

    Owner.Profile->bPendingTownTravel = true;
    TestFalse(TEXT("An in-flight town travel cannot open a service"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Profile->bPendingTownTravel = false;
    Owner.Profile->bProfileLoaded = false;
    TestFalse(TEXT("An unloaded owned profile has no service authority"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Profile->bProfileLoaded = true;

    Owner.Profile->ActiveExpeditionId = FGuid::NewGuid();
    TestFalse(TEXT("Private expedition ownership cannot be used as town-service authority"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    Owner.Profile->ActiveExpeditionId.Invalidate();
    auto WrongTown = Request;
    WrongTown.TownId = EFableTownId::Sunbrook;
    TestFalse(TEXT("A current Legacy owner cannot use an unready foreign town reference"),
              Fixture.Mode->CanUseTownService(Owner.Controller, WrongTown, Service));
    TestTrue(TEXT("Service queries preserve the last safe anchor and checkpoint revision"),
             Owner.Profile->GetLastSafeTownAnchor().TownId == SafeAnchor.TownId &&
                 Owner.Profile->GetLastSafeTownAnchor().AnchorId == SafeAnchor.AnchorId &&
                 Owner.Profile->GetLastSafeTownAnchor().CatalogVersion == SafeAnchor.CatalogVersion &&
                 Owner.Profile->GetTownConnectionSnapshot().SafeCheckpointRevision == Before.SafeCheckpointRevision &&
                 Owner.Profile->GetTownConnectionSnapshot().PresenceRevision == Before.PresenceRevision &&
                 Owner.Hero->GetTownCombatState().SafeSpawnLocation == SafePosition);
    TestTrue(TEXT("No service query fabricated a durable save"),
             Owner.Profile->GetTownAnchorSaveStatus().State == EFableTownAnchorSaveState::None);

    // 실제 돈·아이템·의뢰 변경도 같은 현재 town/service gate를 재검사한다. / 通貨・装備・依頼の変更時にも同じgateを再検査する。
    Other.Profile->Gold = 1000;
    Other.Profile->Ore = 100;
    // 1등급 제작의 Essence 8을 양성 대조에 명시한다. / 1等級制作のEssence 8を陽性対照に明示する。
    Other.Profile->Essence = 8;
    Other.Profile->HealthPotions = 0;
    FFableTownServiceTestAccess::SeedNextItem(Other.Profile, 50);
    const FServiceMutationSnapshot BeforePurchase(Other.Profile);
    const FVector MerchantGround = Other.Hero->GetActorLocation();
    Other.Hero->SetActorLocation(MerchantGround + FVector(0, 0, 100));
    Other.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    FFableTownServiceDescriptor MerchantHint;
    TestTrue(TEXT("Pure UI can display an in-range merchant without granting grounded authority"),
             Fixture.Towns->GetCatalog()->ResolveNearbyService(Other.Hero->GetActorLocation(),
                                                               EFableTownServiceId::Merchant, MerchantHint));
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->BuyShopItem(EFableShopOffer::HealthPotion);
    TestTrue(TEXT("Floating merchant purchase preserves both currency and goods"),
             BeforePurchase.Matches(Other.Profile));
    Other.Hero->SetActorLocation(MerchantGround);
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->BuyShopItem(EFableShopOffer::HealthPotion);
    FServiceMutationSnapshot ExpectedPurchase = BeforePurchase;
    ExpectedPurchase.Gold -= 15;
    ++ExpectedPurchase.HealthPotions;
    TestTrue(TEXT("The same real purchase succeeds only at supported merchant floor"),
             FableRPG::ShopPrice(EFableShopOffer::HealthPotion) == 15 && ExpectedPurchase.Matches(Other.Profile));

    const auto MoveToService = [&Other](const FVector& Location)
    {
        FVector Grounded = Location;
        Grounded.Z = Other.Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.15f;
        Other.Hero->SetActorLocation(Grounded);
        Other.Hero->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    };
    const FServiceMutationSnapshot BeforeCraft(Other.Profile);
    TestTrue(TEXT("Craft data query remains pure outside a workshop"),
             Other.Profile->GetCraftBlockReason(1, EFableEquipmentSlot::Weapon).IsEmpty());
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->CraftItem(1, EFableEquipmentSlot::Weapon);
    TestTrue(TEXT("Merchant proximity cannot authorize workshop spending or items"),
             BeforeCraft.Matches(Other.Profile));
    MoveToService(FableRPG::ForgeLocation());
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    const FFableResourceCost CraftCost = Other.Profile->GetCraftCost(1);
    TestTrue(TEXT("The positive craft control explicitly funds every required resource"),
             CraftCost.Gold == 80 && CraftCost.Ore == 3 && CraftCost.Essence == 8 && CraftCost.RootCores == 0 &&
                 CraftCost.MoonCrystals == 0 && BeforeCraft.Essence == 8);
    Other.Profile->CraftItem(1, EFableEquipmentSlot::Weapon);
    const FFableItem* Crafted = Other.Profile->FindItem(50);
    if (!TestNotNull(TEXT("Supported crafting creates the explicitly seeded owned item"), Crafted))
    {
        return false;
    }

    FServiceMutationSnapshot ExpectedCraft = BeforeCraft;
    ExpectedCraft.Gold -= 80;
    ExpectedCraft.Ore -= 3;
    ExpectedCraft.Essence -= 8;
    ExpectedCraft.Items.Add(*Crafted);
    TestTrue(TEXT("Supported logical workshop performs the real craft and exact cost"),
             ExpectedCraft.Matches(Other.Profile) && Crafted->InstanceId.IsValid() && Crafted->Rarity == 1 &&
                 Crafted->Slot == EFableEquipmentSlot::Weapon && Crafted->HeroClass == Other.Profile->HeroClass &&
                 Crafted->Upgrade == 0 && FableRPG::IsValidItemRoll(*Crafted) &&
                 FableRPG::IsValidItemDurability(*Crafted) &&
                 !BeforeCraft.Items.ContainsByPredicate(
                     [Crafted](const FFableItem& Item)
                     {
                         return Item.InstanceId == Crafted->InstanceId;
                     }));
    const int32 EnhanceGold = Other.Profile->GetUpgradeGoldCost(50);
    const int32 EnhanceOre = Other.Profile->GetUpgradeOreCost(50);
    FServiceMutationSnapshot ExpectedEnhance(Other.Profile);
    ExpectedEnhance.Gold -= 30;
    ExpectedEnhance.Ore -= 2;
    for (FFableItem& Item : ExpectedEnhance.Items)
    {
        if (Item.Id == 50)
        {
            ++Item.Upgrade;
        }
    }

    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->EnhanceItem(50);
    const FFableItem* Enhanced = Other.Profile->FindItem(50);
    TestTrue(TEXT("Supported workshop enhances the owned item with its exact cost"),
             EnhanceGold == 30 && EnhanceOre == 2 && Enhanced && Enhanced->Upgrade == 1 &&
                 ExpectedEnhance.Matches(Other.Profile));

    Other.Profile->ForestQuest = EFableQuestStage::Available;
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->InteractQuest(EFableQuestId::Forest);
    TestTrue(TEXT("Workshop cannot borrow QuestGuide authority"),
             Other.Profile->ForestQuest == EFableQuestStage::Available);
    MoveToService(FableRPG::BoardLocation() + FVector(220, 0, 0));
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->InteractQuest(EFableQuestId::Forest);
    TestTrue(TEXT("Supported catalog QuestGuide accepts the real quest mutation"),
             Other.Profile->ForestQuest == EFableQuestStage::Active);
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->SelectClass(EFableClass::Mage);
    TestTrue(TEXT("Supported QuestGuide changes the real class"), Other.Profile->HeroClass == EFableClass::Mage);
    Other.Hero->AddActorWorldOffset(FVector(0, 0, 100));
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->SelectClass(EFableClass::Archer);
    TestTrue(TEXT("A later floating mutation cannot reuse an earlier guide check"),
             Other.Profile->HeroClass == EFableClass::Mage);

    MoveToService(FableRPG::MerchantLocation());
    const int32 SalePrice = Other.Profile->GetSellPrice(50);
    FServiceMutationSnapshot ExpectedSale(Other.Profile);
    ExpectedSale.Gold += 30;
    ExpectedSale.Items.RemoveAll(
        [](const FFableItem& Item)
        {
            return Item.Id == 50;
        });
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->SellItem(50);
    TestTrue(TEXT("Supported merchant performs the owned sale exactly once"),
             SalePrice == 30 && !Other.Profile->FindItem(50) && ExpectedSale.Matches(Other.Profile));
    FFableTownServiceTestAccess::NextMutation(Other.Profile);
    Other.Profile->SellItem(50);
    TestTrue(TEXT("An already sold item cannot grant more currency or alter other fields"),
             ExpectedSale.Matches(Other.Profile));
    MoveToService(FableRPG::PortalLocation());
    TestTrue(TEXT("Current supported expedition portal resolves its own logical service"),
             Fixture.Mode->CanUseTownService(Other.Controller, EFableTownServiceId::ExpeditionPortal));
    Other.Profile->ActiveExpeditionId = FGuid::NewGuid();
    TestFalse(TEXT("A guessed private ticket cannot use the authenticated portal resume exception"),
              Fixture.Mode->CanUseTownService(Other.Controller, EFableTownServiceId::ExpeditionPortal));
    Other.Profile->ActiveExpeditionId.Invalidate();

    // 같은 profile·pawn을 새 소유 연결로 교체해 오래된 연결의 서비스 재사용을 거절한다. / 同じprofile・pawnを新接続へ交換する。
    APlayerController* Replacement = Fixture.World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Separate replacement connection positive control"), Replacement))
    {
        return false;
    }

    Fixture.World->AddController(Replacement);
    Fixture.Controllers.Add(Replacement);
    Owner.Controller->UnPossess();
    Owner.Profile->SetOwner(Replacement);
    Replacement->SetPlayerState(Owner.Profile);
    Replacement->Possess(Owner.Hero);
    Fixture.Parties->RegisterPlayer(Replacement);
    TestTrue(TEXT("The replacement registers its own current connection generation"),
             Fixture.Towns->RegisterOrRefreshConnection(Replacement) &&
                 Fixture.Towns->RefreshPhysicalPresence(Replacement));
    TestFalse(TEXT("The old controller cannot reuse the same profile or pawn for a service"),
              Fixture.Mode->CanUseTownService(Owner.Controller, Request, Service));
    TestTrue(TEXT("The exact new owner can use the same grounded service"),
             Fixture.Mode->CanUseTownService(Replacement, Request, Service));
    return true;
}
#endif
