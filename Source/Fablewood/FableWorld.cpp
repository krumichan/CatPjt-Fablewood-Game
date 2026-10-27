#include "FableWorld.h"
#include "FableRPGTypes.h"
#include "FableTownOverlook.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    const FName GeneratedTag(TEXT("FableGeneratedEnvironment"));

    /** 숲 배경에서 사용할 원본 메시 에셋을 읽는다. */
    UStaticMesh* ForestMesh(const TCHAR* Name)
    {
        return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Art/Meshes/%s.%s"), Name, Name));
    }

    /** 충돌 전용 컴포넌트가 화면과 카메라에 불필요하게 드러나지 않도록 설정한다. */
    void HideCollision(UInstancedStaticMeshComponent* Component)
    {
        if (Component)
        {
            Component->SetVisibility(false);
            Component->SetHiddenInGame(true);
            Component->SetCastShadow(false);
        }
    }
}

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableWorld::AFableWorld()
{
    // 컴포넌트·메시·재질 설정 처리
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    bNetLoadOnClient = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ForestRoot"));
    RootComponent->SetMobility(EComponentMobility::Static);

    // Primitives are only the invisible, predictable gameplay collision.

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    ShapeMaterial = Material.Object;
}

/** 액터 구성 시 배경과 표시 요소를 준비한다. */
void AFableWorld::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BuildEnvironment();
}

/** 컴포넌트 등록이 끝나면 배경 구성을 확인한다. */
void AFableWorld::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
#if WITH_EDITOR
    // Reloading a saved level registers native components but does not necessarily
    // rerun construction. Recreate its transient forest for the editor viewport.
    UWorld* World = GetWorld();
    if (!World || World->WorldType != EWorldType::Editor || IsTemplate() || bBuildingEnvironment || !RootComponent ||
        !RootComponent->IsRegistered())
    {
        return;
    }
    TArray<UInstancedStaticMeshComponent*> Existing;
    GetComponents(Existing);
    for (UInstancedStaticMeshComponent* Component : Existing)
    {
        if (Component && Component->ComponentHasTag(GeneratedTag))
        {
            return;
        }
    }
    BuildEnvironment();
#endif
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableWorld::BeginPlay()
{
    Super::BeginPlay();
    // Rebuild once on every machine, also for a level loaded without construction.
    BuildEnvironment();
}

/** 같은 재질과 메시를 공유하는 배경 인스턴스 묶음을 만든다. */
UInstancedStaticMeshComponent* AFableWorld::MakeBatch(FName Name, UStaticMesh* Mesh, const FLinearColor& Color,
                                                      bool bBlocking, UMaterialInterface* MaterialOverride,
                                                      float Roughness)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Mesh)
    {
        return nullptr;
    }
    // Both peers build identically named components under the same map actor.
    // CharacterMovement can reference a base by this path without replicating the forest.

    const FName ComponentName(*FString::Printf(TEXT("Fable_%s"), *Name.ToString()));
    UInstancedStaticMeshComponent* Batch = NewObject<UInstancedStaticMeshComponent>(this, ComponentName, RF_Transient);
    Batch->SetNetAddressable();
    Batch->ComponentTags.Add(GeneratedTag);
    Batch->ComponentTags.Add(Name);
    Batch->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    Batch->SetupAttachment(RootComponent);
    Batch->SetMobility(EComponentMobility::Static);
    Batch->SetStaticMesh(Mesh);
    Batch->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Batch->SetCollisionObjectType(ECC_WorldStatic);
    Batch->SetCollisionResponseToAllChannels(ECR_Block);
    Batch->SetCanEverAffectNavigation(false);
    Batch->SetCastShadow(true);
    UMaterialInterface* BaseMaterial = MaterialOverride ? MaterialOverride : ShapeMaterial.Get();

    // 컴포넌트·메시·재질 설정 처리
    if (BaseMaterial)
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, Batch);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
        Batch->SetMaterial(0, Material);
    }

    Batch->RegisterComponent();

    return Batch;
}

/** 지정한 위치·회전·크기로 배경 인스턴스를 배치한다. */
void AFableWorld::Place(UInstancedStaticMeshComponent* Batch, const FVector& Location, const FVector& Scale,
                        const FRotator& Rotation)
{
    if (Batch)
    {
        Batch->AddInstance(FTransform(Rotation, Location, Scale));
    }
}

/** 지역의 지형·건축물·식생·조명을 구성한다. */
void AFableWorld::BuildEnvironment()
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (bBuildingEnvironment || IsTemplate() || !GetWorld())
    {
        return;
    }

    TGuardValue<bool> BuildingGuard(bBuildingEnvironment, true);
    TArray<UInstancedStaticMeshComponent*> Previous;
    GetComponents(Previous);

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UInstancedStaticMeshComponent* Component : Previous)
    {
        if (Component && Component->ComponentHasTag(GeneratedTag))
        {
            Component->DestroyComponent();
            // Retire destroyed UObject names before rebuilding stable network paths.
            const FName RetiredName =
                MakeUniqueObjectName(Component->GetOuter(), Component->GetClass(), TEXT("TRASH_FableGenerated"));
            Component->Rename(*RetiredName.ToString(), nullptr,
                              REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CubeMesh || !CylinderMesh || !ShapeMaterial)
    {
        return;
    }

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (UMaterialInterface* Paint =
            LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint")))
    {
        ShapeMaterial = Paint;
    }

    UMaterialInterface* GroundMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_FableGround.M_FableGround"));
    UMaterialInterface* WaterMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_FableWater.M_FableWater"));

    UStaticMesh* TerrainMesh = ForestMesh(TEXT("SM_ForestTerrain"));
    UStaticMesh* OakMeshA = ForestMesh(TEXT("SM_ForestOakTrunk"));
    UStaticMesh* OakMeshB = ForestMesh(TEXT("SM_ForestOakTrunkB"));
    UStaticMesh* LeavesMeshA = ForestMesh(TEXT("SM_ForestOakLeaves"));
    UStaticMesh* LeavesMeshB = ForestMesh(TEXT("SM_ForestOakLeavesB"));
    UStaticMesh* GrassMesh = ForestMesh(TEXT("SM_ForestGrass"));
    UStaticMesh* FernMesh = ForestMesh(TEXT("SM_ForestFern"));
    UStaticMesh* RockMesh = ForestMesh(TEXT("SM_ForestRock"));

    // 위치·방향·거리 계산에 필요한 값 준비
    auto* Ground = MakeBatch(TEXT("Meadow"), CubeMesh, FLinearColor(0.25f, 0.37f, 0.15f), true);
    Place(Ground, FVector(0, 0, -60), FVector(60, 60, 1.2f));

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (TerrainMesh)
    {
        HideCollision(Ground);
    }

    // 위치·방향·거리 계산에 필요한 값 준비
    auto* Meadow = MakeBatch(TEXT("MeadowSurface"), TerrainMesh, FLinearColor::White, false, GroundMaterial);
    auto* Path = MakeBatch(TEXT("WarmPath"), ForestMesh(TEXT("SM_ForestPath")), FLinearColor(0.48f, 0.35f, 0.20f));
    auto* Water = MakeBatch(TEXT("QuietBrook"), ForestMesh(TEXT("SM_ForestBrook")), FLinearColor(0.10f, 0.40f, 0.44f),
                            false, WaterMaterial, 0.28f);
    Place(Meadow, FVector::ZeroVector, FVector::OneVector);
    Place(Path, FVector::ZeroVector, FVector::OneVector);
    Place(Water, FVector::ZeroVector, FVector::OneVector);
    // Lower, non-colliding ground continues behind the finite playable clearing.
    // Even the highest point stays over 30 cm below the original surface.
    auto* ForestUnderlay =
        MakeBatch(TEXT("DistantForestGround"), TerrainMesh, FLinearColor::White, false, GroundMaterial);
    Place(ForestUnderlay, FVector(0, 0, -70), FVector(3.0f));

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (ForestUnderlay)
    {
        ForestUnderlay->SetCastShadow(false);
    }

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (Water)
    {
        Water->SetCastShadow(false);
    }

    auto* TrunkCollision = MakeBatch(TEXT("TreeTrunks"), CylinderMesh, FLinearColor::Black, true);
    auto* RockCollision = MakeBatch(TEXT("StoneCollision"), CubeMesh, FLinearColor::Black, true);
    auto* HouseCollision = MakeBatch(TEXT("CottageCollision"), CubeMesh, FLinearColor::Black, true);
    auto* BridgeCollision = MakeBatch(TEXT("BridgeStone"), CubeMesh, FLinearColor::Black, true);
    HideCollision(TrunkCollision);
    HideCollision(RockCollision);
    HideCollision(HouseCollision);
    HideCollision(BridgeCollision);

    auto* OakA = MakeBatch(TEXT("OakBark"), OakMeshA, FLinearColor(0.25f, 0.125f, 0.052f));
    auto* OakB = MakeBatch(TEXT("OldOakBark"), OakMeshB, FLinearColor(0.20f, 0.105f, 0.045f));
    auto* LeavesA = MakeBatch(TEXT("MossCanopy"), LeavesMeshA, FLinearColor(0.19f, 0.34f, 0.085f));
    auto* LeavesB = MakeBatch(TEXT("SunCanopy"), LeavesMeshB, FLinearColor(0.34f, 0.43f, 0.13f));
    auto* ShadeLeavesA = MakeBatch(TEXT("DeepCanopy"), LeavesMeshA, FLinearColor(0.12f, 0.245f, 0.07f));
    auto* ShadeLeavesB = MakeBatch(TEXT("ShadeCanopy"), LeavesMeshB, FLinearColor(0.20f, 0.315f, 0.08f));
    auto* Rocks = MakeBatch(TEXT("SoftStones"), RockMesh, FLinearColor(0.40f, 0.41f, 0.32f));
    auto* MossRocks = MakeBatch(TEXT("MossyStones"), RockMesh, FLinearColor(0.29f, 0.34f, 0.20f));
    auto* Bridge =
        MakeBatch(TEXT("ArchedStoneBridge"), ForestMesh(TEXT("SM_ForestBridge")), FLinearColor(0.47f, 0.45f, 0.33f));

    // 위치·방향·거리 계산에 필요한 값 준비
    const FVector BridgeOrigin(-2180, -350, 0);
    Place(Bridge, BridgeOrigin, FVector::OneVector);

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 19; ++I)
    {
        // Short, gently pitched slabs follow the artwork's walkable arch.
        const float X = -360.0f + I * 40.0f;
        const float Z = 14.0f + 100.0f * (1.0f - FMath::Square(X / 380.0f));
        const float Slope = -200.0f * X / FMath::Square(380.0f);
        const FRotator Pitch(FMath::RadiansToDegrees(FMath::Atan(Slope)), 0, 0);
        Place(BridgeCollision, BridgeOrigin + FVector(X, 0, Z - 10), FVector(0.42f, 3.70f, 0.20f), Pitch);
        Place(BridgeCollision, BridgeOrigin + FVector(X, -204, Z + 24), FVector(0.40f, 0.38f, 0.48f));
        Place(BridgeCollision, BridgeOrigin + FVector(X, 204, Z + 24), FVector(0.40f, 0.38f, 0.48f));
    }

    // 위치·방향·거리 계산에 필요한 값 준비
    FRandomStream Random(30721);
    auto AddTree = [&](const FVector& Base, float Scale, float Yaw, int32 Variant)
    {
        const FVector Size(Scale);
        const FRotator Rotation(0, Yaw, 0);
        const bool bSecond = (Variant % 2) != 0;
        Place(bSecond ? OakB : OakA, Base, Size, Rotation);
        auto* Canopy =
            bSecond ? ((Variant % 4 == 1) ? LeavesB : ShadeLeavesB) : ((Variant % 4 == 0) ? LeavesA : ShadeLeavesA);
        Place(Canopy, Base, Size, Rotation);
        // The foliage never contributes a large convex collision hull.
        Place(TrunkCollision, Base + FVector(0, 0, 330 * Scale), FVector(1.48f * Scale, 1.48f * Scale, 6.6f * Scale));
    };

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 44; ++I)
    {
        const float Angle = I * 2.0f * PI / 44.0f;
        const float Radius = Random.FRandRange(2360, 2820);
        const FVector Base(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
        if ((Base.Y > 1800 && FMath::Abs(Base.X) < 1650) || (Base.X < -1900 && FMath::Abs(Base.Y + 350) < 570))
        {
            continue;
        }
        AddTree(Base, Random.FRandRange(0.85f, 1.28f), Random.FRandRange(0, 360), I);
    }
    // Nearer roots and overhead sprays frame the clearing without blocking combat.

    // 위치·방향·거리 계산 처리
    AddTree(FVector(-1740, -1290, 0), 1.13f, 25, 0);
    AddTree(FVector(1830, -1190, 0), 1.18f, 147, 1);
    AddTree(FVector(-1720, 1460, 0), 1.05f, 60, 3);
    AddTree(FVector(1810, 1390, 0), 1.15f, 214, 2);

    // Three staggered depth bands turn the village skyline into a forest.
    // A separate stream preserves every existing playable tree, stone, and flower.

    FRandomStream BackgroundRandom(30722);
    auto* BackgroundBarkA = MakeBatch(TEXT("BackgroundOakBark"), OakMeshA, FLinearColor(0.21f, 0.145f, 0.075f));
    auto* BackgroundBarkB = MakeBatch(TEXT("BackgroundOldOakBark"), OakMeshB, FLinearColor(0.19f, 0.145f, 0.08f));
    auto* BackgroundLeavesA = MakeBatch(TEXT("BackgroundMossCanopy"), LeavesMeshA, FLinearColor(0.17f, 0.29f, 0.095f));
    auto* BackgroundLeavesB = MakeBatch(TEXT("BackgroundSunCanopy"), LeavesMeshB, FLinearColor(0.25f, 0.355f, 0.14f));
    auto* FarBarkA = MakeBatch(TEXT("FarForestBark"), OakMeshA, FLinearColor(0.215f, 0.24f, 0.17f));
    auto* FarBarkB = MakeBatch(TEXT("FarForestOldBark"), OakMeshB, FLinearColor(0.20f, 0.235f, 0.175f));
    auto* FarLeavesA = MakeBatch(TEXT("FarForestCanopy"), LeavesMeshA, FLinearColor(0.225f, 0.325f, 0.195f));
    auto* FarLeavesB = MakeBatch(TEXT("FarForestLightCanopy"), LeavesMeshB, FLinearColor(0.29f, 0.375f, 0.235f));
    // The 48 background trees use existing instanced meshes and no shadow maps.
    // They have no collision, so the 60 m gameplay boundary and movement bases are unchanged.

    // 같은 규칙을 적용할 대상을 순회한다.
    for (auto* Batch : {BackgroundBarkA, BackgroundBarkB, BackgroundLeavesA, BackgroundLeavesB, FarBarkA, FarBarkB,
                        FarLeavesA, FarLeavesB})
    {
        if (Batch)
        {
            Batch->SetCastShadow(false);
        }
    }

    // 위치·방향·거리 계산에 필요한 값 준비
    auto AddBackgroundTree = [&](const FVector& Base, float Scale, int32 Variant, bool bFar)
    {
        const bool bSecond = (Variant % 2) != 0;
        auto* Bark = bFar ? (bSecond ? FarBarkB : FarBarkA) : (bSecond ? BackgroundBarkB : BackgroundBarkA);
        auto* Crown = bFar ? (bSecond ? FarLeavesB : FarLeavesA) : (bSecond ? BackgroundLeavesB : BackgroundLeavesA);
        const FRotator Rotation(0, BackgroundRandom.FRandRange(0, 360), 0);
        const FVector Size(Scale, Scale, Scale * BackgroundRandom.FRandRange(0.94f, 1.09f));
        Place(Bark, Base, Size, Rotation);
        Place(Crown, Base, Size, Rotation);
    };

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 10; ++I)
    {
        // The nearest background trunks begin beyond the cottages' rear walls.
        AddBackgroundTree(FVector(-3670 + I * 810 + BackgroundRandom.FRandRange(-110, 110),
                                  3650 + BackgroundRandom.FRandRange(-180, 240), -70),
                          BackgroundRandom.FRandRange(1.28f, 1.65f), I, false);
    }

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 10; ++I)
    {
        AddBackgroundTree(FVector(-4530 + I * 1010 + BackgroundRandom.FRandRange(-150, 150),
                                  4960 + BackgroundRandom.FRandRange(-230, 260), -70),
                          BackgroundRandom.FRandRange(1.70f, 2.15f), I + 1, true);
    }

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 6; ++I)
    {
        AddBackgroundTree(FVector(-4600 + I * 1840 + BackgroundRandom.FRandRange(-180, 180),
                                  6300 + BackgroundRandom.FRandRange(-170, 170), -70),
                          BackgroundRandom.FRandRange(2.30f, 2.70f), I, true);
    }

    // 위치·방향·거리 계산 순회
    for (int32 Side : {-1, 1})
    {
        for (int32 I = 0; I < 11; ++I)
        {
            const int32 Layer = I % 3;
            const float X = Side * (3550.0f + Layer * 1250.0f + BackgroundRandom.FRandRange(-120, 120));
            const float Y = -3070 + I * 690.0f + BackgroundRandom.FRandRange(-160, 160);
            AddBackgroundTree(FVector(X, Y, -70),
                              BackgroundRandom.FRandRange(1.32f + Layer * 0.35f, 1.70f + Layer * 0.35f),
                              I + (Side > 0 ? 1 : 0), Layer > 0);
        }
    }

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 39; ++I)
    {
        // 위치·방향·거리 계산에 필요한 값 준비
        const float Angle = I * 2.0f * PI / 39.0f;
        const float Radius = Random.FRandRange(1830, 2830);
        const FVector Base(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);

        // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
        if ((Base.X < -1770 && FMath::Abs(Base.Y + 350) < 430) || (Base.Y > 1850 && FMath::Abs(Base.X) < 1650))
        {
            continue;
        }

        // 위치·방향·거리 계산에 필요한 값 준비
        const float Scale = Random.FRandRange(0.65f, 1.60f);
        const float Yaw = Random.FRandRange(0, 360);
        Place((I % 3 == 0) ? MossRocks : Rocks, Base, FVector(Scale), FRotator(0, Yaw, 0));
        Place(RockCollision, Base + FVector(0, 0, 35 * Scale), FVector(1.3f * Scale, 0.9f * Scale, 0.7f * Scale),
              FRotator(0, Yaw, 0));
    }
    // Small irregular stones follow both curved stream banks.

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 50; ++I)
    {
        const float Y = -2800 + I * 114.0f;
        if (FMath::Abs(Y + 350) < 280)
        {
            continue;
        }
        const float StreamX = -2180 + 95 * FMath::Sin(Y * 0.0011f) + 25 * FMath::Sin(Y * 0.004f);
        for (int32 Side : {-1, 1})
        {
            const FVector Base(StreamX + Side * Random.FRandRange(212, 253), Y, 0);
            Place((I % 3 == 0) ? MossRocks : Rocks, Base, FVector(Random.FRandRange(0.3f, 0.65f)),
                  FRotator(0, Random.FRandRange(0, 360), 0));
        }
    }

    auto* Walls =
        MakeBatch(TEXT("CottagePlaster"), ForestMesh(TEXT("SM_ForestCottageWalls")), FLinearColor(0.72f, 0.55f, 0.31f));
    auto* Roofs =
        MakeBatch(TEXT("CottageRoofs"), ForestMesh(TEXT("SM_ForestCottageRoof")), FLinearColor(0.46f, 0.115f, 0.045f));
    auto* Wood = MakeBatch(TEXT("CottageTimber"), ForestMesh(TEXT("SM_ForestCottageTimber")),
                           FLinearColor(0.19f, 0.085f, 0.035f));
    auto* Windows = MakeBatch(TEXT("AmberWindows"), ForestMesh(TEXT("SM_ForestCottageWindows")),
                              FLinearColor(0.95f, 0.55f, 0.16f), false, nullptr, 0.42f);
    auto* CottageStone = MakeBatch(TEXT("CottageStonework"), ForestMesh(TEXT("SM_ForestCottageStone")),
                                   FLinearColor(0.44f, 0.40f, 0.29f));

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 5; ++I)
    {
        const FVector Base(-1370 + I * 685, 2500 + (I % 2) * 140, 0);
        const float Scale = (I == 2) ? 1.22f : Random.FRandRange(0.86f, 1.05f);
        const float Yaw = (2 - I) * 5.0f;
        const FRotator Rotation(0, Yaw, 0);
        for (auto* Batch : {Walls, Roofs, Wood, Windows, CottageStone})
        {
            Place(Batch, Base, FVector(Scale), Rotation);
        }
        Place(HouseCollision, Base + FVector(0, 0, 150 * Scale), FVector(5 * Scale, 4.2f * Scale, 3 * Scale), Rotation);
    }

    auto* Grass = MakeBatch(TEXT("GrassBlades"), GrassMesh, FLinearColor(0.23f, 0.36f, 0.09f));
    auto* LightGrass = MakeBatch(TEXT("SunlitGrass"), GrassMesh, FLinearColor(0.39f, 0.44f, 0.14f));
    auto* Ferns = MakeBatch(TEXT("WoodlandFerns"), FernMesh, FLinearColor(0.16f, 0.32f, 0.08f));
    auto* FlowerStems =
        MakeBatch(TEXT("FlowerStems"), ForestMesh(TEXT("SM_ForestFlowerStems")), FLinearColor(0.18f, 0.30f, 0.075f));
    UStaticMesh* PetalMesh = ForestMesh(TEXT("SM_ForestFlowerPetals"));
    auto* Flowers = MakeBatch(TEXT("CreamFlowers"), PetalMesh, FLinearColor(0.94f, 0.84f, 0.57f));
    auto* PurpleFlowers = MakeBatch(TEXT("LilacFlowers"), PetalMesh, FLinearColor(0.49f, 0.30f, 0.48f));
    auto* Hearts =
        MakeBatch(TEXT("FlowerHearts"), ForestMesh(TEXT("SM_ForestFlowerHearts")), FLinearColor(0.82f, 0.46f, 0.08f));
    auto* MushroomStems =
        MakeBatch(TEXT("MushroomStems"), ForestMesh(TEXT("SM_ForestMushroomStems")), FLinearColor(0.72f, 0.62f, 0.41f));
    auto* MushroomCaps =
        MakeBatch(TEXT("MushroomCaps"), ForestMesh(TEXT("SM_ForestMushroomCaps")), FLinearColor(0.55f, 0.19f, 0.075f));

    // Personal story objects stay in the world after use: each player investigates
    // independently. Authored flowers, roots and stones add no walking collision.

    auto* StoryStems = MakeBatch(TEXT("StoryLanternStems"), ForestMesh(TEXT("SM_ForestFlowerStems")),
                                 FLinearColor(0.18f, 0.34f, 0.18f));
    auto* StoryPetals =
        MakeBatch(TEXT("StoryLanternPetals"), PetalMesh, FLinearColor(1.0f, 0.67f, 0.20f), false, nullptr, 0.56f);
    auto* StoryHearts = MakeBatch(TEXT("StoryLanternHearts"), ForestMesh(TEXT("SM_ForestFlowerHearts")),
                                  FLinearColor(1.0f, 0.92f, 0.52f), false, nullptr, 0.48f);
    auto* StoryStones = MakeBatch(TEXT("StoryTrailStones"), RockMesh, FLinearColor(0.51f, 0.52f, 0.36f));

    // 퀘스트와 이야기 진행 순회
    for (int32 ObjectIndex = 0; ObjectIndex < 3; ++ObjectIndex)
    {
        const FVector Base = FableRPG::StoryObjectLocation(ObjectIndex) + FVector(0, 0, 4);
        const FRotator Facing(0, ObjectIndex * 47.0f, 0);
        for (auto* Batch : {StoryStems, StoryPetals, StoryHearts})
        {
            Place(Batch, Base, FVector(1.65f), Facing);
        }
        for (int32 StoneIndex = 0; StoneIndex < 6; ++StoneIndex)
        {
            const float Angle = StoneIndex * PI / 3.0f;
            Place(StoryStones, Base + FVector(FMath::Cos(Angle) * 94, FMath::Sin(Angle) * 94, 0),
                  FVector(0.16f, 0.13f, 0.09f), FRotator(0, StoneIndex * 61.0f, 0));
        }
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    auto* StoryRoots = MakeBatch(TEXT("StoryFadedRoots"), OakMeshB, FLinearColor(0.32f, 0.22f, 0.13f));
    auto* StoryCrown = MakeBatch(TEXT("StoryFadedCrown"), LeavesMeshB, FLinearColor(0.37f, 0.38f, 0.17f));
    const FVector RootBase = FableRPG::StoryObjectLocation(3) + FVector(0, 0, 4);
    Place(StoryRoots, RootBase, FVector(0.18f, 0.18f, 0.16f), FRotator(0, 38, 0));
    Place(StoryCrown, RootBase, FVector(0.18f, 0.18f, 0.16f), FRotator(0, 38, 0));
    Place(Ferns, RootBase + FVector(-70, 20, 0), FVector(1.4f), FRotator(0, 45, 0));
    Place(StoryStones, RootBase + FVector(65, -40, 0), FVector(0.30f, 0.23f, 0.14f));

    // About 1,600 total instances: detailed silhouettes stay inexpensive through ISM batches.

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 1400; ++I)
    {
        // 위치·방향·거리 계산에 필요한 값 준비
        const FVector Base(Random.FRandRange(-2870, 2870), Random.FRandRange(-2850, 2840), 1);
        const float StreamX = -2180 + 95 * FMath::Sin(Base.Y * 0.0011f) + 25 * FMath::Sin(Base.Y * 0.004f);
        const float PathX = 130 * FMath::Sin(Base.Y * 0.0017f);
        const bool bClearing = FMath::Square(Base.X / 1420) + FMath::Square((Base.Y - 120) / 1500) < 1;
        const bool bRoad = FMath::Abs(Base.X - PathX) < 240;
        const bool bBridgePath = Base.X < -1050 && FMath::Abs(Base.Y + 350) < 245;
        const bool bVillage = Base.Y > 2150 && FMath::Abs(Base.X) < 1700;

        // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
        if (bClearing || bRoad || bBridgePath || bVillage || FMath::Abs(Base.X - StreamX) < 203)
        {
            continue;
        }

        // 위치·방향·거리 계산에 필요한 값 준비
        const float Scale = Random.FRandRange(0.75f, 1.45f);
        const FRotator Rotation(0, Random.FRandRange(0, 360), 0);
        Place((I % 4 == 0) ? LightGrass : Grass, Base, FVector(Scale), Rotation);

        // 위치·방향·거리 계산 처리
        if (I % 5 == 0)
        {
            Place(Ferns, Base + FVector(25, -20, 0), FVector(Scale * 0.85f), Rotation);
        }

        // 위치·방향·거리 계산 처리
        if (I % 8 == 0)
        {
            const FVector FlowerScale(Scale);
            Place(FlowerStems, Base, FlowerScale, Rotation);
            Place((I % 24 == 0) ? PurpleFlowers : Flowers, Base, FlowerScale, Rotation);
            Place(Hearts, Base, FlowerScale, Rotation);
        }

        // 위치·방향·거리 계산 처리
        if (I % 31 == 0)
        {
            Place(MushroomStems, Base, FVector(Scale), Rotation);
            Place(MushroomCaps, Base, FVector(Scale), Rotation);
        }
    }

    // 기존 경계는 동쪽 계단 입구 폭만 열고 나머지 세 면과 동쪽 벽 구간은 보존한다.
    // 従来境界は東階段の入口幅だけを開き、他の三面と東壁区間を保持する。

    // 위치·방향·거리 계산에 필요한 값 준비
    auto* Boundary = MakeBatch(TEXT("PrototypeBoundary"), CubeMesh, FLinearColor::Black, true);
    HideCollision(Boundary);
    Place(Boundary, FVector(-3010, 0, 1000), FVector(0.2f, 62, 22));
    for (const FTransform& Section : FableTownOverlook::EastBoundarySections())
    {
        Place(Boundary, Section.GetTranslation(), Section.GetScale3D(), Section.Rotator());
    }
    Place(Boundary, FVector(0, -3010, 1000), FVector(62, 0.2f, 22));
    Place(Boundary, FVector(0, 3010, 1000), FVector(62, 0.2f, 22));

    // 표시와 충돌이 같은 단·발판·안전벽 배치를 사용하고 모든 peer의 component 경로를 고정한다.
    // 表示と衝突に同じ段・上床・安全壁を使い、全peerのcomponent pathを固定する。
    auto* OverlookStairs =
        MakeBatch(FableTownOverlook::StairSupportTag(), CubeMesh, FLinearColor(0.45f, 0.39f, 0.29f), true);
    auto* OverlookLanding =
        MakeBatch(FableTownOverlook::LandingSupportTag(), CubeMesh, FLinearColor(0.51f, 0.46f, 0.34f), true);
    auto* OverlookGuards =
        MakeBatch(FableTownOverlook::GuardSupportTag(), CubeMesh, FLinearColor(0.35f, 0.31f, 0.25f), true);
    for (const FFableTownOverlookBox& Box : FableTownOverlook::LayoutBoxes())
    {
        UInstancedStaticMeshComponent* Batch = Box.SupportTag == FableTownOverlook::StairSupportTag() ? OverlookStairs
                                               : Box.SupportTag == FableTownOverlook::LandingSupportTag()
                                                   ? OverlookLanding
                                                   : OverlookGuards;
        Place(Batch, Box.CubeTransform.GetTranslation(), Box.CubeTransform.GetScale3D(), Box.CubeTransform.Rotator());
    }
}
