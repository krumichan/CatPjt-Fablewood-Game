#include "FableMoonwellWorld.h"
#include "FableGameState.h"
#include "FableRPGTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    const FName MoonTag(TEXT("FableMoonwellGenerated"));

    /** 달샘 배경에서 사용할 원본 메시 에셋을 읽는다. */
    UStaticMesh* MoonMesh(const TCHAR* Name)
    {
        return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/Art/Meshes/%s.%s"), Name, Name));
    }

    /** 달샘의 충돌 전용 컴포넌트에 표시·카메라 충돌 설정을 적용한다. */
    void HideMoonCollision(UInstancedStaticMeshComponent* Batch)
    {
        if (Batch)
        {
            Batch->SetVisibility(false);
            Batch->SetHiddenInGame(true);
            Batch->SetCastShadow(false);
        }
    }
}

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableMoonwellWorld::AFableMoonwellWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.2f;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    SetNetUpdateFrequency(1);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("MoonwellRoot"));
    RootComponent->SetMobility(EComponentMobility::Static);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(
        TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    PaintMaterial = Paint.Object;
    UBoxComponent* ColorBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("MoonlightBounds"));
    ColorBounds->SetupAttachment(RootComponent);
    ColorBounds->SetRelativeLocation(FVector(18000, 0, 400));
    ColorBounds->SetBoxExtent(FVector(2900, 2900, 1800));
    ColorBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    ColorBounds->SetHiddenInGame(true);
    UPostProcessComponent* Moonlight = CreateDefaultSubobject<UPostProcessComponent>(TEXT("MoonlightColor"));
    Moonlight->SetupAttachment(ColorBounds);
    Moonlight->bUnbound = false;
    Moonlight->BlendRadius = 350;
    Moonlight->Priority = 2;
    Moonlight->Settings.bOverride_SceneColorTint = true;
    Moonlight->Settings.SceneColorTint = FLinearColor(0.66f, 0.78f, 1.0f);
    Moonlight->Settings.bOverride_AutoExposureBias = true;
    Moonlight->Settings.AutoExposureBias = -0.45f;
}

/** 액터 구성 시 배경과 표시 요소를 준비한다. */
void AFableMoonwellWorld::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BuildEnvironment();
}

/** 컴포넌트 등록이 끝나면 배경 구성을 확인한다. */
void AFableMoonwellWorld::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
#if WITH_EDITOR
    if (!GetWorld() || GetWorld()->WorldType != EWorldType::Editor || IsTemplate() || bBuilding || !RootComponent ||
        !RootComponent->IsRegistered())
    {
        return;
    }
    TArray<UActorComponent*> Components;
    GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component && Component->ComponentHasTag(MoonTag))
        {
            return;
        }
    }
    BuildEnvironment();
#endif
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableMoonwellWorld::BeginPlay()
{
    Super::BeginPlay();
    BuildEnvironment();
}

/** 같은 재질과 메시를 공유하는 배경 인스턴스 묶음을 만든다. */
UInstancedStaticMeshComponent* AFableMoonwellWorld::MakeBatch(FName Name, UStaticMesh* Mesh, FLinearColor Color,
                                                              bool bBlocking, UMaterialInterface* OverrideMaterial)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Mesh)
    {
        return nullptr;
    }

    // 컴포넌트·메시·재질 설정에 필요한 값 준비
    UInstancedStaticMeshComponent* Batch = NewObject<UInstancedStaticMeshComponent>(
        this, *FString::Printf(TEXT("FableMoon_%s"), *Name.ToString()), RF_Transient);
    Batch->SetNetAddressable();
    Batch->ComponentTags.Add(MoonTag);
    Batch->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    Batch->SetupAttachment(RootComponent);
    Batch->SetMobility(EComponentMobility::Static);
    Batch->SetStaticMesh(Mesh);
    Batch->SetCanEverAffectNavigation(false);
    Batch->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Batch->SetCollisionObjectType(ECC_WorldStatic);
    Batch->SetCollisionResponseToAllChannels(ECR_Block);
    Batch->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

    // 컴포넌트·메시·재질 설정 처리
    if (UMaterialInterface* Base = OverrideMaterial ? OverrideMaterial : PaintMaterial.Get())
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Base, Batch);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Material->SetScalarParameterValue(TEXT("Roughness"), 0.83f);
        Batch->SetMaterial(0, Material);
    }

    Batch->RegisterComponent();

    return Batch;
}

/** 지정한 위치·회전·크기로 배경 인스턴스를 배치한다. */
void AFableMoonwellWorld::Place(UInstancedStaticMeshComponent* Batch, FVector Location, FVector Scale,
                                FRotator Rotation)
{
    if (Batch)
    {
        Batch->AddInstance(FTransform(Rotation, Location, Scale));
    }
}

/** 지역 연출에 필요한 광원을 배치한다. */
void AFableMoonwellWorld::AddLight(FName Name, FVector Location, FLinearColor Color, float Intensity, float Radius)
{
    // 컴포넌트·메시·재질 설정에 필요한 값 준비
    UPointLightComponent* Light = NewObject<UPointLightComponent>(this, Name, RF_Transient);
    Light->SetNetAddressable();
    Light->ComponentTags.Add(MoonTag);
    Light->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    Light->SetupAttachment(RootComponent);
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetRelativeLocation(Location);
    Light->SetLightColor(Color);
    Light->SetIntensity(Intensity);
    Light->SetAttenuationRadius(Radius);
    Light->SetCastShadows(false);
    Light->RegisterComponent();
}

/** 지역의 지형·건축물·식생·조명을 구성한다. */
void AFableMoonwellWorld::BuildEnvironment()
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (bBuilding || IsTemplate() || !GetWorld())
    {
        return;
    }

    TGuardValue<bool> Guard(bBuilding, true);
    TArray<UActorComponent*> Old;
    GetComponents(Old);

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UActorComponent* Component : Old)
    {
        if (!Component || !Component->ComponentHasTag(MoonTag))
        {
            continue;
        }
        Component->DestroyComponent();
        const FName Retired =
            MakeUniqueObjectName(Component->GetOuter(), Component->GetClass(), TEXT("TRASH_FableMoon"));
        Component->Rename(*Retired.ToString(), nullptr,
                          REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
    }

    SealMaterials.Empty();
    LastSealState = -1;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!CubeMesh || !CylinderMesh || !PaintMaterial)
    {
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const FVector Origin = FableRPG::RegionCenter(EFableRegion::Moonwell);
    UStaticMesh* Rock = MoonMesh(TEXT("SM_ForestRock"));
    UStaticMesh* Trunk = MoonMesh(TEXT("SM_ForestOakTrunkB"));
    UStaticMesh* Leaves = MoonMesh(TEXT("SM_ForestOakLeavesB"));
    UStaticMesh* Terrain = MoonMesh(TEXT("SM_ForestTerrain"));
    auto* Floor = MakeBatch(TEXT("CollisionFloor"), CubeMesh, FLinearColor::Black, true);
    Place(Floor, Origin + FVector(0, 0, -60), FVector(48, 48, 1.2f));
    HideMoonCollision(Floor);
    auto* Ground = MakeBatch(TEXT("SilverMossGround"), Terrain, FLinearColor(0.15f, 0.235f, 0.255f));
    Place(Ground, Origin, FVector(0.80f, 0.80f, 0.25f));
    auto* Underlay = MakeBatch(TEXT("DistantGround"), Terrain, FLinearColor(0.12f, 0.18f, 0.24f));
    Place(Underlay, Origin + FVector(0, 0, -70), FVector(1.9f));

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (Underlay)
    {
        Underlay->SetCastShadow(false);
    }

    // 위치·방향·거리 계산에 필요한 값 준비
    auto* Boundary = MakeBatch(TEXT("OuterCollision"), CubeMesh, FLinearColor::Black, true);
    HideMoonCollision(Boundary);
    Place(Boundary, Origin + FVector(-2420, 0, 800), FVector(0.4f, 49, 18));
    Place(Boundary, Origin + FVector(2420, 0, 800), FVector(0.4f, 49, 18));
    Place(Boundary, Origin + FVector(0, -2420, 800), FVector(49, 0.4f, 18));
    Place(Boundary, Origin + FVector(0, 2420, 800), FVector(49, 0.4f, 18));
    auto* Stone = MakeBatch(TEXT("BlueStone"), Rock, FLinearColor(0.37f, 0.45f, 0.53f));
    auto* PaleStone = MakeBatch(TEXT("SilverStone"), Rock, FLinearColor(0.58f, 0.64f, 0.67f));
    UMaterialInterface* WaterMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_FableWater.M_FableWater"));
    auto* Water =
        MakeBatch(TEXT("MoonlitWater"), CylinderMesh, FLinearColor(0.055f, 0.31f, 0.48f), false, WaterMaterial);
    Place(Water, Origin + FVector(0, 240, 1.1f), FVector(17, 13, 0.014f));
    Place(Water, Origin + FVector(-1700, 1000, 1.1f), FVector(8, 11, 0.014f));
    Place(Water, Origin + FVector(1700, 1050, 1.1f), FVector(8, 10, 0.014f));

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (Water)
    {
        Water->SetCastShadow(false);
    }
    // Flat authored stones mark three readable walking routes through shallow water.

    // 위치·방향·거리 계산 순회
    for (int32 Seal = 0; Seal < 3; ++Seal)
    {
        // 위치·방향·거리 계산에 필요한 값 준비
        const FVector Destination = FableRPG::MoonSealLocation(Seal);
        const FVector Start = Origin + FVector(0, -1750, 2);

        // 위치·방향·거리 계산 순회
        for (int32 Step = 0; Step < 12; ++Step)
        {
            const float Alpha = Step / 11.0f;
            const FVector Location =
                FMath::Lerp(Start, Destination, Alpha) + FVector(FMath::Sin(Alpha * PI) * 80, 0, 1);
            Place(PaleStone, Location, FVector(0.75f, 0.62f, 0.045f), FRotator(0, Seal * 31 + Step * 13, 0));
        }

        auto* SealRing =
            MakeBatch(*FString::Printf(TEXT("SealRunes%d"), Seal), CubeMesh, FLinearColor(0.27f, 0.32f, 0.62f));
        SealMaterials.Add(SealRing ? Cast<UMaterialInstanceDynamic>(SealRing->GetMaterial(0)) : nullptr);

        // 위치·방향·거리 계산 순회
        for (int32 I = 0; I < 16; ++I)
        {
            const float Angle = I * 2.0f * PI / 16;
            Place(SealRing, Destination + FVector(FMath::Cos(Angle) * 145, FMath::Sin(Angle) * 145, 5),
                  FVector(0.45f, 0.14f, 0.04f), FRotator(0, FMath::RadiansToDegrees(Angle) + 90, 0));
        }
        // A low obelisk and floral crown do not obstruct either entry lane or the combat floor.

        // 위치·방향·거리 계산 처리
        Place(PaleStone, Destination + FVector(0, 0, 0), FVector(1.20f, 1.10f, 0.12f));
        Place(SealRing, Destination + FVector(0, 0, 70), FVector(0.24f, 0.24f, 1.1f), FRotator(0, 45, 0));
        Place(SealRing, Destination + FVector(0, 0, 135), FVector(0.46f, 0.46f, 0.46f), FRotator(45, 45, 45));
        AddLight(*FString::Printf(TEXT("FableMoon_SealLight%d"), Seal), Destination + FVector(0, 0, 165),
                 FLinearColor(0.25f, 0.65f, 1), 1500, 650);
    }

    auto* Bark = MakeBatch(TEXT("SilverWillowBark"), Trunk, FLinearColor(0.32f, 0.38f, 0.45f));
    auto* Canopy = MakeBatch(TEXT("IndigoWillowLeaves"), Leaves, FLinearColor(0.17f, 0.24f, 0.39f));
    auto* LightCanopy = MakeBatch(TEXT("LavenderWillowLeaves"), Leaves, FLinearColor(0.32f, 0.36f, 0.53f));
    auto* TreeCollision = MakeBatch(TEXT("TreeCollision"), CylinderMesh, FLinearColor::Black, true);
    HideMoonCollision(TreeCollision);
    FRandomStream Random(41021);

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 36; ++I)
    {
        // 위치·방향·거리 계산에 필요한 값 준비
        const float Angle = I * 2.0f * PI / 36;
        const float Radius = Random.FRandRange(2300, 2600);
        const FVector Base = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);

        // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
        if (Base.Y < -1900 && FMath::Abs(Base.X - Origin.X) < 420)
        {
            continue;
        }

        // 위치·방향·거리 계산에 필요한 값 준비
        const float Scale = Random.FRandRange(0.85f, 1.25f);
        const FRotator Rotation(0, Random.FRandRange(0, 360), 0);
        Place(Bark, Base, FVector(Scale), Rotation);
        Place(I % 3 ? Canopy : LightCanopy, Base, FVector(Scale), Rotation);

        // 위치·방향·거리 계산 처리
        if (Radius < 2390)
        {
            Place(TreeCollision, Base + FVector(0, 0, 250), FVector(1.1f, 1.1f, 5));
        }

        Place(Stone, Base + FVector(70, -90, 0), FVector(Random.FRandRange(0.8f, 1.7f)), Rotation);
    }

    auto* FarBark = MakeBatch(TEXT("FarWillowBark"), Trunk, FLinearColor(0.20f, 0.23f, 0.33f));
    auto* FarLeaves = MakeBatch(TEXT("FarWillowLeaves"), Leaves, FLinearColor(0.19f, 0.235f, 0.37f));

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (FarBark)
    {
        FarBark->SetCastShadow(false);
    }

    // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
    if (FarLeaves)
    {
        FarLeaves->SetCastShadow(false);
    }

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 24; ++I)
    {
        const float Angle = I * 2.0f * PI / 24;
        const float Radius = Random.FRandRange(3400, 4300);
        const FVector Base = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, -70);
        const float Scale = Random.FRandRange(1.4f, 2.0f);
        const FRotator Rotation(0, Random.FRandRange(0, 360), 0);
        Place(FarBark, Base, FVector(Scale), Rotation);
        Place(FarLeaves, Base, FVector(Scale), Rotation);
    }

    auto* Ferns = MakeBatch(TEXT("MoonFerns"), MoonMesh(TEXT("SM_ForestFern")), FLinearColor(0.18f, 0.40f, 0.43f));
    auto* Stems =
        MakeBatch(TEXT("LilyStems"), MoonMesh(TEXT("SM_ForestFlowerStems")), FLinearColor(0.25f, 0.43f, 0.39f));
    auto* Petals =
        MakeBatch(TEXT("SilverLilies"), MoonMesh(TEXT("SM_ForestFlowerPetals")), FLinearColor(0.75f, 0.79f, 0.95f));
    auto* Centers =
        MakeBatch(TEXT("LilyHearts"), MoonMesh(TEXT("SM_ForestFlowerHearts")), FLinearColor(0.65f, 0.79f, 0.86f));

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 240; ++I)
    {
        // 위치·방향·거리 계산에 필요한 값 준비
        const float Angle = Random.FRandRange(0, 2 * PI);
        const float Radius = Random.FRandRange(1800, 2300);
        const FVector Base = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 2);

        // 이 단계에서 지역의 지형·건축물·식생·조명을 구성한다.
        if (Base.Y < -1750 && FMath::Abs(Base.X - Origin.X) < 460)
        {
            continue;
        }

        // 위치·방향·거리 계산에 필요한 값 준비
        const FVector Scale(Random.FRandRange(0.9f, 1.9f));
        const FRotator Rotation(0, Random.FRandRange(0, 360), 0);
        Place(Ferns, Base, Scale, Rotation);

        // 위치·방향·거리 계산 처리
        if (I % 3 == 0)
        {
            Place(Stems, Base, Scale, Rotation);
            Place(Petals, Base, Scale, Rotation);
            Place(Centers, Base, Scale, Rotation);
        }
    }
    // The broken moon arch is scenery; its narrow collision pillars leave the arena open.

    auto* Pillars = MakeBatch(TEXT("MoonRuinPillars"), CylinderMesh, FLinearColor(0.38f, 0.43f, 0.55f), true);

    // 위치·방향·거리 계산 순회
    for (int32 Side : {-1, 1})
    {
        Place(Pillars, Origin + FVector(Side * 720, 2050, 260), FVector(1.20f, 1.20f, 5.2f));
        Place(PaleStone, Origin + FVector(Side * 720, 2050, 520), FVector(1.2f, 1.2f, 0.32f));
    }

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 11; ++I)
    {
        const float Angle = PI * I / 10;
        Place(PaleStone, Origin + FVector(FMath::Cos(Angle) * 720, 2050, 540 + FMath::Sin(Angle) * 430),
              FVector(1.3f, 0.7f, 0.60f), FRotator(0, I * 7, 0));
    }

    const FVector Portal = FableRPG::RegionReturnPortal(EFableRegion::Moonwell);

    // 위치·방향·거리 계산 순회
    for (int32 I = 0; I < 13; ++I)
    {
        const float Angle = PI * I / 12;
        Place(PaleStone, Portal + FVector(FMath::Cos(Angle) * 130, 0, 125 + FMath::Sin(Angle) * 155),
              FVector(0.39f, 0.39f, 0.46f), FRotator(0, I * 11, 0));
    }

    // 위치·방향·거리 계산 처리
    Place(Pillars, Portal + FVector(-130, 0, 60), FVector(0.38f, 0.38f, 1.2f));
    Place(Pillars, Portal + FVector(130, 0, 60), FVector(0.38f, 0.38f, 1.2f));
    auto* PortalSurface = MakeBatch(TEXT("ReturnMoonlight"), CylinderMesh, FLinearColor(0.34f, 0.36f, 0.79f));
    Place(PortalSurface, Portal + FVector(0, 0, 130), FVector(2.1f, 2.55f, 0.04f), FRotator(0, 0, 90));

    // HTTP 요청·응답과 연결 상태 처리
    if (PortalSurface)
    {
        PortalSurface->SetCastShadow(false);
        // Match the root-ruins portal: only the camera sees this collision surface.
        PortalSurface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        PortalSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
        PortalSurface->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
        PortalSurface->SetGenerateOverlapEvents(false);
    }

    AddLight(TEXT("FableMoon_ReturnGlow"), Portal + FVector(0, 90, 170), FLinearColor(0.45f, 0.45f, 1), 1600, 500);
}

/** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
void AFableMoonwellWorld::Tick(float DeltaSeconds)
{
    // 현재 월드와 플레이어 참조 처리
    Super::Tick(DeltaSeconds);
    const AFableGameState* State = GetWorld() ? GetWorld()->GetGameState<AFableGameState>() : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!State)
    {
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bActive = State->ActiveRegion == EFableRegion::Moonwell;
    const int32 VisualState = State->MoonSealMask + ((State->ActiveSealIndex + 1) << 3) + (bActive ? 64 : 0);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (VisualState == LastSealState)
    {
        return;
    }

    LastSealState = VisualState;

    // 같은 규칙을 적용할 대상을 순회한다.
    for (int32 I = 0; I < SealMaterials.Num(); ++I)
    {
        if (!SealMaterials[I])
        {
            continue;
        }
        const bool bDone = bActive && (State->MoonSealMask & (1 << I)) != 0;
        const bool bCurrent = bActive && State->ActiveSealIndex == I;
        SealMaterials[I]->SetVectorParameterValue(
            TEXT("Color"), bDone ? FLinearColor(0.22f, 0.85f, 0.64f)
                                 : (bCurrent ? FLinearColor(0.85f, 0.64f, 0.21f) : FLinearColor(0.27f, 0.32f, 0.62f)));
    }
}
