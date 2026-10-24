#include "FableDungeonWorld.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace
{
    // 실제 실패 기록의 사설 원점과 캐릭터 중심 높이다. / 実際の失敗記録の私有原点と中心高。
    const FVector ObservedPrivateOrigin(0, 60000, 0);
    constexpr double ObservedCenterZ = 94.15000094825247;
    constexpr float ProbeRadius = 55.f;

    /** 실제 생성물만 등록하며 게임·AI·저장을 시작하지 않는다. / 実生成物だけを登録しゲーム処理は開始しない。 */
    struct FResultsCollisionWorld
    {
        UWorld* World = nullptr;
        AFableDungeonWorld* Dungeon = nullptr;
        bool bOwnsContext = false;

        /** 운영 코드와 같은 deferred 생성으로 사설 배경을 구성한다. / 製品と同じ deferred 生成で私有背景を作る。 */
        FResultsCollisionWorld()
        {
            if (!GEngine)
            {
                return;
            }

            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(true)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(true)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            if (!World)
            {
                return;
            }

            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            bOwnsContext = true;

            const FTransform Transform(FRotator::ZeroRotator, ObservedPrivateOrigin);
            Dungeon =
                World->SpawnActorDeferred<AFableDungeonWorld>(AFableDungeonWorld::StaticClass(), Transform, nullptr,
                                                              nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (Dungeon)
            {
                Dungeon->BindExpedition(FGuid(0x20261024, 0x20190145, 0xB81702DE, 0x00000140), true);
                UGameplayStatics::FinishSpawningActor(Dungeon, Transform);
            }
        }

        /** 이 fixture가 만든 월드와 문맥만 정리한다. / この fixture が所有するワールドと文脈だけを破棄する。 */
        ~FResultsCollisionWorld()
        {
            if (!World)
            {
                return;
            }

            World->DestroyWorld(false);
            if (GEngine && bOwnsContext)
            {
                GEngine->DestroyWorldContext(World);
            }
        }

        /** 실제 배경의 이름 있는 인스턴스 컴포넌트를 찾는다. / 実背景の名前付きインスタンスを取得する。 */
        UInstancedStaticMeshComponent* FindBatch(FName Name) const
        {
            if (!Dungeon)
            {
                return nullptr;
            }

            TArray<UInstancedStaticMeshComponent*> Components;
            Dungeon->GetComponents(Components);
            for (UInstancedStaticMeshComponent* Component : Components)
            {
                if (Component && Component->GetFName() == Name)
                {
                    return Component;
                }
            }

            return nullptr;
        }

        /** 실제 이동 검증과 같은 정적 물체 sphere query를 사용한다. / 実移動検証と同じ静的物体 sphere query を使う。 */
        bool Sweep(const FVector& CanonicalStart, const FVector& CanonicalEnd, FHitResult& Hit) const
        {
            const FCollisionQueryParams Parameters(SCENE_QUERY_STAT(FableResultsMovementNative), false);
            return World->SweepSingleByObjectType(
                Hit, ObservedPrivateOrigin + CanonicalStart, ObservedPrivateOrigin + CanonicalEnd, FQuat::Identity,
                FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(ProbeRadius), Parameters);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableResultsFormationPortalClearanceTest,
                                 "Fablewood.Native.ResultsMovement.GeneratedPortalFormationClearance",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 실제 기둥 충돌과 제한된 모서리 오차를 대조한다. / 実際の柱衝突と制限された角の誤差を比較する。 */
bool FFableResultsFormationPortalClearanceTest::RunTest(const FString& Parameters)
{
    FResultsCollisionWorld Fixture;
    if (!TestNotNull(TEXT("A memory world with a physics scene exists"), Fixture.World) ||
        !TestNotNull(TEXT("The real private DungeonWorld was constructed"), Fixture.Dungeon))
    {
        return false;
    }

    TestNotNull(TEXT("The fixture owns an actual physics scene"), Fixture.World->GetPhysicsScene());
    TestFalse(TEXT("No gameplay BeginPlay or enemy AI was started"), Fixture.Dungeon->HasActorBegunPlay());
    TestTrue(TEXT("The real private arena origin is retained"),
             Fixture.Dungeon->GetActorLocation().Equals(ObservedPrivateOrigin, .01));

    UInstancedStaticMeshComponent* PortalStone = Fixture.FindBatch(TEXT("FableDungeon_PortalStone"));
    UInstancedStaticMeshComponent* Floor = Fixture.FindBatch(TEXT("FableDungeon_RuinFloor"));
    UInstancedStaticMeshComponent* Walls = Fixture.FindBatch(TEXT("FableDungeon_RuinWalls"));
    if (!TestNotNull(TEXT("The real portal stone batch exists"), PortalStone) ||
        !TestNotNull(TEXT("The real floor batch exists"), Floor) ||
        !TestNotNull(TEXT("The real wall batch exists"), Walls))
    {
        return false;
    }

    // 메시나 충돌을 대체하지 않고 실제 등록 상태를 검증한다. / メッシュや衝突を置換せず実登録状態を調べる。
    TArray<UInstancedStaticMeshComponent*> Components;
    Fixture.Dungeon->GetComponents(Components);
    int32 CollidingBatchCount = 0;
    for (UInstancedStaticMeshComponent* Component : Components)
    {
        if (Component && Component->GetInstanceCount() > 0 &&
            Component->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
        {
            ++CollidingBatchCount;
            TestTrue(*FString::Printf(TEXT("%s is registered"), *Component->GetName()), Component->IsRegistered());
            TestTrue(*FString::Printf(TEXT("%s has real physics state"), *Component->GetName()),
                     Component->IsPhysicsStateCreated());
        }
    }
    TestTrue(TEXT("The fixture contains floor, walls, portal and other actual collision"), CollidingBatchCount >= 3);
    TestEqual(TEXT("Private construction adds only the return arch's three stone pieces"),
              PortalStone->GetInstanceCount(), 3);
    const UStaticMesh* PortalMesh = PortalStone->GetStaticMesh();
    if (!TestNotNull(TEXT("The portal's actual EngineCube asset was loaded"), PortalMesh))
    {
        return false;
    }
    TestEqual(TEXT("The test uses the production EngineCube, without a synthetic substitute"),
              PortalStone->GetStaticMesh()->GetPathName(), FString(TEXT("/Engine/BasicShapes/Cube.Cube")));

    FTransform LeftPostTransform;
    if (!TestTrue(TEXT("The left return pillar has an actual world transform"),
                  PortalStone->GetInstanceTransform(0, LeftPostTransform, true)))
    {
        return false;
    }

    const FBox LeftPostWorldBounds = PortalStone->GetStaticMesh()->GetBoundingBox().TransformBy(LeftPostTransform);
    TestTrue(TEXT("The generated left pillar's minimum matches the observed obstacle"),
             (LeftPostWorldBounds.Min - ObservedPrivateOrigin).Equals(FVector(7418, -1363, 0), .01));
    TestTrue(TEXT("The generated left pillar's maximum matches the observed obstacle"),
             (LeftPostWorldBounds.Max - ObservedPrivateOrigin).Equals(FVector(7466, -1297, 260), .01));

    // 실패 원문 좌표를 그대로 사용한다. / 失敗記録の座標をそのまま用いる。
    const FVector ObservedStart(6551.043214354666, -1399.7723238627004, ObservedCenterZ);
    const FVector ObservedGoal(9450, -1450, ObservedCenterZ);
    FHitResult ObservedHit;
    if (!TestTrue(TEXT("Positive control: the observed old segment really hits generated collision"),
                  Fixture.Sweep(ObservedStart, ObservedGoal, ObservedHit)))
    {
        return false;
    }

    TestTrue(TEXT("The observed collision is a blocking hit, not an initial overlap"),
             ObservedHit.bBlockingHit && !ObservedHit.bStartPenetrating);
    TestTrue(TEXT("The observed collision comes from the generated portal stone component"),
             ObservedHit.GetComponent() == PortalStone);
    TestEqual(TEXT("The observed collision identifies the left pillar instance"), ObservedHit.Item, 0);
    TestTrue(TEXT("The actual impact point lies on the generated left pillar"),
             LeftPostWorldBounds.ExpandBy(1.f).IsInsideOrOn(ObservedHit.ImpactPoint));
    AddInfo(FString::Printf(TEXT("Positive control: component=%s instance=%d impactCanonical=%s distance=%.3f"),
                            *PortalStone->GetName(), ObservedHit.Item,
                            *(ObservedHit.ImpactPoint - ObservedPrivateOrigin).ToString(), ObservedHit.Distance));

    // 바닥도 실제 query에 참여해야 무충돌 월드의 거짓 통과를 막는다. / 床の query も無衝突世界の偽陽性を防ぐ。
    FHitResult FloorHit;
    TestTrue(TEXT("A downward positive control hits the actual generated floor"),
             Fixture.Sweep(FVector(8000, 0, 150), FVector(8000, 0, 40), FloorHit));
    TestTrue(TEXT("The downward positive control identifies the real floor component"),
             FloorHit.GetComponent() == Floor);

    // 첫 구간과 한 바퀴의 모든 선분을 포함한다. / 最初の区間と一周する全線分を含む。
    const FVector Routes[2][6] = {{FVector(8000, -1450, ObservedCenterZ), FVector(9450, -1450, ObservedCenterZ),
                                   FVector(9450, 1450, ObservedCenterZ), FVector(6550, 1450, ObservedCenterZ),
                                   FVector(6550, -1450, ObservedCenterZ), FVector(9450, -1450, ObservedCenterZ)},
                                  {FVector(8240, -1650, ObservedCenterZ), FVector(9650, -1650, ObservedCenterZ),
                                   FVector(9650, 1650, ObservedCenterZ), FVector(6350, 1650, ObservedCenterZ),
                                   FVector(6350, -1650, ObservedCenterZ), FVector(9650, -1650, ObservedCenterZ)}};

    int32 SweepCount = 0;
    for (const double Tolerance : {20.0, 30.0})
    {
        // 사각 모서리는 실제 원형 도착 반경보다 보수적인 외접 경계다. / 四隅は実際の円形到着範囲より保守的な外接境界。
        const FVector Offsets[] = {FVector::ZeroVector, FVector(-Tolerance, -Tolerance, 0),
                                   FVector(-Tolerance, Tolerance, 0), FVector(Tolerance, -Tolerance, 0),
                                   FVector(Tolerance, Tolerance, 0)};
        for (int32 Lane = 0; Lane < 2; ++Lane)
        {
            for (int32 Segment = 0; Segment < 5; ++Segment)
            {
                for (const FVector& StartOffset : Offsets)
                {
                    for (const FVector& EndOffset : Offsets)
                    {
                        const FVector Start = Routes[Lane][Segment] + StartOffset;
                        const FVector End = Routes[Lane][Segment + 1] + EndOffset;
                        FHitResult Hit;
                        ++SweepCount;
                        const bool bHit = Fixture.Sweep(Start, End, Hit);
                        TestFalse(*FString::Printf(
                                      TEXT("Lane %d segment %d tolerance %.0f start=%s end=%s remains clear (hit=%s)"),
                                      Lane, Segment, Tolerance, *Start.ToString(), *End.ToString(),
                                      Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none")),
                                  bHit);
                    }
                }
            }
        }
    }
    TestEqual(TEXT("Both lanes and both tolerance bounds ran all 500 continuous sweeps"), SweepCount, 500);

    // 보장 범위를 벗어나면 실제 기둥을 다시 검출해야 한다. / 保証範囲の外では実際の柱を再検出する必要がある。
    FHitResult BeyondMarginHit;
    TestTrue(
        TEXT("A 34 cm north deviation exceeds the tested 30 cm bound and hits the portal"),
        Fixture.Sweep(FVector(6550, -1416, ObservedCenterZ), FVector(9450, -1416, ObservedCenterZ), BeyondMarginHit));
    TestTrue(TEXT("The outside-bound control still identifies actual portal stone"),
             BeyondMarginHit.GetComponent() == PortalStone);

    AddInfo(TEXT("Memory geometry only: real private DungeonWorld/EngineCube at origin (0,60000,0), "
                 "Z=94.15000094825247, WorldStatic sphere radius 55; 20/30 cm rectangular endpoint bounds. "
                 "This does not validate character walking, dynamic teammates/enemies, loaded maps, "
                 "rendering, streaming or two-client networking."));
    return true;
}
#endif
