#include "FableDungeonWorld.h"
#include "FableRPGTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace { const FName DungeonTag(TEXT("FableDungeonGenerated")); }

AFableDungeonWorld::AFableDungeonWorld()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    SetNetUpdateFrequency(1.0f);
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DungeonRoot"));
    RootComponent->SetMobility(EComponentMobility::Static);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    CubeMesh = Cube.Object;
    CylinderMesh = Cylinder.Object;
    SphereMesh = Sphere.Object;
    PaintMaterial = Material.Object;
}

void AFableDungeonWorld::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    BuildEnvironment();
}

void AFableDungeonWorld::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
#if WITH_EDITOR
    if (!GetWorld() || GetWorld()->WorldType != EWorldType::Editor || IsTemplate() || bBuilding
        || !RootComponent || !RootComponent->IsRegistered()) return;
    TArray<UActorComponent*> Components;
    GetComponents(Components);
    for (UActorComponent* Component : Components)
        if (Component && Component->ComponentHasTag(DungeonTag)) return;
    BuildEnvironment();
#endif
}

void AFableDungeonWorld::BeginPlay()
{
    Super::BeginPlay();
    BuildEnvironment();
    // Each peer chooses the same brightest map sun for forward shading.
    // This resolves the sun/fill priority tie rather than hiding renderer warnings.
    UDirectionalLightComponent* PrimarySun = nullptr;
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
        if (Light && (!PrimarySun || Light->Intensity > PrimarySun->Intensity))
            PrimarySun = Light;
    }
    if (PrimarySun) PrimarySun->SetForwardShadingPriority(1);
    for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
    {
        UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
        if (Light && Light != PrimarySun) Light->SetForwardShadingPriority(0);
    }
}

UInstancedStaticMeshComponent* AFableDungeonWorld::MakeBatch(FName Name, UStaticMesh* Mesh, FLinearColor Color, bool bBlocking)
{
    if (!Mesh) return nullptr;
    const FName StableName(*FString::Printf(TEXT("FableDungeon_%s"), *Name.ToString()));
    UInstancedStaticMeshComponent* Batch = NewObject<UInstancedStaticMeshComponent>(this, StableName, RF_Transient);
    Batch->SetNetAddressable();
    Batch->ComponentTags.Add(DungeonTag);
    Batch->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    Batch->SetupAttachment(RootComponent);
    Batch->SetMobility(EComponentMobility::Static);
    Batch->SetStaticMesh(Mesh);
    Batch->SetCollisionEnabled(bBlocking ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Batch->SetCollisionObjectType(ECC_WorldStatic);
    Batch->SetCollisionResponseToAllChannels(ECR_Block);
    Batch->SetCollisionResponseToChannel(ECC_Camera, bBlocking ? ECR_Block : ECR_Ignore);
    Batch->SetCanEverAffectNavigation(false);
    if (PaintMaterial)
    {
        UMaterialInstanceDynamic* Paint = UMaterialInstanceDynamic::Create(PaintMaterial, Batch);
        Paint->SetVectorParameterValue(TEXT("Color"), Color);
        Paint->SetScalarParameterValue(TEXT("Roughness"), 0.88f);
        Batch->SetMaterial(0, Paint);
    }
    Batch->RegisterComponent();
    return Batch;
}

void AFableDungeonWorld::Place(UInstancedStaticMeshComponent* Batch, FVector Location, FVector Scale, FRotator Rotation)
{
    if (Batch) Batch->AddInstance(FTransform(Rotation, Location, Scale));
}

void AFableDungeonWorld::AddText(FName Name, const FString& Text, FVector Location, float Size, FColor Color)
{
    UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, Name, RF_Transient);
    Label->SetNetAddressable();
    Label->ComponentTags.Add(DungeonTag);
    Label->CreationMethod = EComponentCreationMethod::UserConstructionScript;
    Label->SetupAttachment(RootComponent);
    Label->SetMobility(EComponentMobility::Static);
    Label->SetRelativeLocation(Location);
    Label->SetRelativeRotation(FRotator(0, -90, 0));
    Label->SetText(FText::FromString(Text));
    Label->SetTextRenderColor(Color);
    Label->SetHorizontalAlignment(EHTA_Center);
    Label->SetVerticalAlignment(EVRTA_TextCenter);
    Label->SetWorldSize(Size);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Label->SetCastShadow(false);
    Label->RegisterComponent();
}

void AFableDungeonWorld::AddLight(FName Name, FVector Location, FLinearColor Color, float Intensity, float Radius)
{
    UPointLightComponent* Light = NewObject<UPointLightComponent>(this, Name, RF_Transient);
    Light->SetNetAddressable();
    Light->ComponentTags.Add(DungeonTag);
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

void AFableDungeonWorld::AddVillager(const FString& Id, FVector Location, FLinearColor Cloth, FLinearColor Hair)
{
    // Reuse the authored adventurer pieces, assembled as a civilian with no weapons.
    // Pure scene components cannot be mistaken for combatants or party members.
    const FRotator Facing(0,-90,0);
    const auto Part = [&](const TCHAR* Asset, const TCHAR* Suffix, FVector Pivot, FLinearColor Color)
    {
        const FString Path = FString::Printf(TEXT("/Game/Art/Meshes/SM_Hero%s.SM_Hero%s"), Asset, Asset);
        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
        auto* Batch = MakeBatch(*FString::Printf(TEXT("NPC_%s_%s"), *Id, Suffix), Mesh, Color, false);
        Place(Batch, Location + FVector(0,0,92) + Facing.RotateVector(Pivot), FVector(1), Facing);
    };
    const FLinearColor Skin(0.55f,0.29f,0.15f), Ivory(0.73f,0.65f,0.46f), Leather(0.07f,0.036f,0.02f);
    Part(TEXT("Shirt"),TEXT("Shirt"),FVector::ZeroVector,Ivory);
    Part(TEXT("Vest"),TEXT("Vest"),FVector::ZeroVector,Cloth);
    Part(TEXT("VestSeams"),TEXT("Seams"),FVector::ZeroVector,FLinearColor(0.31f,0.18f,0.075f));
    Part(TEXT("Belt"),TEXT("Belt"),FVector::ZeroVector,Leather);
    Part(TEXT("Hips"),TEXT("Hips"),FVector::ZeroVector,Leather);
    Part(TEXT("Pouch"),TEXT("Pouch"),FVector::ZeroVector,FLinearColor(0.18f,0.09f,0.03f));
    Part(TEXT("Head"),TEXT("Head"),FVector::ZeroVector,Skin);
    Part(TEXT("EyeWhites"),TEXT("EyeWhites"),FVector::ZeroVector,Ivory);
    Part(TEXT("Eyes"),TEXT("Eyes"),FVector::ZeroVector,FLinearColor(0.018f,0.027f,0.022f));
    Part(TEXT("Lips"),TEXT("Lips"),FVector::ZeroVector,FLinearColor(0.29f,0.105f,0.072f));
    Part(TEXT("Hair"),TEXT("Hair"),FVector::ZeroVector,Hair);
    Part(TEXT("Mantle"),TEXT("Mantle"),FVector::ZeroVector,Cloth);
    Part(TEXT("WaistCloth"),TEXT("WaistCloth"),FVector::ZeroVector,Cloth);
    for (int32 Side : {-1,1})
    {
        const FVector Shoulder(0,Side*23,42), Forearm(1,Side*23,14), Thigh(-1,Side*10,-8), Shin(0,Side*10,-47);
        const auto Limb = [&](const TCHAR* Asset, FVector Pivot, FLinearColor Color)
        { Part(Asset,*FString::Printf(TEXT("%s_%d"),Asset,Side),Pivot,Color); };
        Limb(TEXT("Sleeve"),Shoulder,Ivory); Limb(TEXT("SleeveCuff"),Shoulder,Ivory);
        Limb(TEXT("Forearm"),Forearm,Skin); Limb(TEXT("Glove"),Forearm,Leather);
        Limb(TEXT("Thigh"),Thigh,Leather); Limb(TEXT("Shin"),Shin,Leather);
        Limb(TEXT("Boot"),Shin,Leather); Limb(TEXT("BootSole"),Shin,Leather*0.45f);
        Limb(TEXT("BootStraps"),Shin,FLinearColor(0.31f,0.18f,0.075f));
    }
}

void AFableDungeonWorld::BuildEnvironment()
{
    if (bBuilding || IsTemplate() || !GetWorld()) return;
    TGuardValue<bool> Guard(bBuilding, true);
    TArray<UActorComponent*> Previous;
    GetComponents(Previous);
    for (UActorComponent* Component : Previous)
    {
        if (!Component || !Component->ComponentHasTag(DungeonTag)) continue;
        Component->DestroyComponent();
        const FName RetiredName = MakeUniqueObjectName(Component->GetOuter(), Component->GetClass(), TEXT("TRASH_FableDungeon"));
        Component->Rename(*RetiredName.ToString(), nullptr, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
    }
    if (!CubeMesh || !CylinderMesh || !SphereMesh) return;
    auto* Floor = MakeBatch(TEXT("RuinFloor"), CubeMesh, FLinearColor(0.18f, 0.22f, 0.205f), true);
    auto* Walls = MakeBatch(TEXT("RuinWalls"), CubeMesh, FLinearColor(0.205f, 0.255f, 0.23f), true);
    auto* Trim = MakeBatch(TEXT("RuinTrim"), CubeMesh, FLinearColor(0.35f, 0.38f, 0.285f), false);
    auto* Tile = MakeBatch(TEXT("RuinTiles"), CubeMesh, FLinearColor(0.26f, 0.29f, 0.25f), false);
    auto* Pillars = MakeBatch(TEXT("RuinPillars"), CylinderMesh, FLinearColor(0.30f, 0.34f, 0.28f), false);
    auto* Wood = MakeBatch(TEXT("TownWood"), CubeMesh, FLinearColor(0.24f, 0.125f, 0.055f), true);
    auto* Paper = MakeBatch(TEXT("QuestParchment"), CubeMesh, FLinearColor(0.86f, 0.75f, 0.48f), false);
    auto* Metal = MakeBatch(TEXT("ForgeIron"), CubeMesh, FLinearColor(0.18f, 0.21f, 0.20f), true);
    auto* Flame = MakeBatch(TEXT("TorchFlame"), SphereMesh, FLinearColor(1.0f, 0.37f, 0.045f), false);
    auto* PortalStone = MakeBatch(TEXT("PortalStone"), CubeMesh, FLinearColor(0.30f, 0.28f, 0.38f), true);
    auto* Portal = MakeBatch(TEXT("PortalSurface"), CylinderMesh, FLinearColor(0.10f, 0.49f, 0.68f), false);
    if (Portal)
    {
        Portal->SetCastShadow(false);
        // The opaque portal sits behind arrivals: stop the camera before its surface,
        // while preserving unobstructed pawn movement, attacks and visibility traces.
        Portal->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Portal->SetCollisionResponseToAllChannels(ECR_Ignore);
        Portal->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
        Portal->SetGenerateOverlapEvents(false);
    }
    if (Flame) Flame->SetCastShadow(false);

    // All gameplay floor remains at Z=0. Decorative tiles have no collision.
    Place(Floor, FVector(8000, 0, -60), FVector(36, 36, 1.2f));
    for (int32 X = 0; X < 12; ++X)
        for (int32 Y = 0; Y < 12; ++Y)
            Place(Tile, FVector(6350 + X * 300, -1650 + Y * 300, 0.8f), FVector(2.96f, 2.96f, 0.016f));
    Place(Walls, FVector(8000, -1850, 400), FVector(38, 1, 8));
    Place(Walls, FVector(8000, 1850, 400), FVector(38, 1, 8));
    Place(Walls, FVector(6150, 0, 400), FVector(1, 36, 8));
    Place(Walls, FVector(9850, 0, 400), FVector(1, 36, 8));
    for (int32 I = 0; I < 8; ++I)
    {
        for (int32 Side : {-1, 1})
        {
            const FVector Base(6450 + I * 445, Side * 1780, 0);
            Place(Pillars, Base + FVector(0,0,350), FVector(1.2f,1.2f,7.0f));
            Place(Trim, Base + FVector(0,0,22), FVector(1.6f,1.6f,0.44f));
            Place(Trim, Base + FVector(0,0,700), FVector(1.7f,1.7f,0.3f));
        }
    }
    for (int32 I = 0; I < 5; ++I)
    {
        for (int32 Side : {-1, 1})
        {
            const FVector Base(8000 + Side * 1770, -1300 + I * 650, 0);
            Place(Pillars, Base + FVector(0,0,350), FVector(1.2f,1.2f,7.0f));
            Place(Trim, Base + FVector(0,0,700), FVector(1.7f,1.7f,0.3f));
            Place(Metal, Base + FVector(-Side * 35,0,265), FVector(0.3f,0.3f,0.6f));
            Place(Flame, Base + FVector(-Side * 35,0,305), FVector(0.30f,0.30f,0.6f));
            AddLight(*FString::Printf(TEXT("FableDungeon_Torch_%d_%d"), Side, I),
                Base + FVector(-Side * 80,0,325), FLinearColor(1.0f,0.45f,0.13f), 1800, 630);
        }
    }
    // A raised visual dais marks the boss end; its thin decorations do not impede dodges.
    auto* Dais = MakeBatch(TEXT("BossSeal"), CylinderMesh, FLinearColor(0.33f,0.29f,0.16f), false);
    Place(Dais, FVector(8000,650,1.7f), FVector(8.8f,8.8f,0.012f));

    // A small hand-shaped stone and inset pebble glyph mark the personal rune
    // investigation. Its scene-only meshes leave the entrance combat lane open.
    UStaticMesh* StoryRockMesh = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/Art/Meshes/SM_ForestRock.SM_ForestRock"));
    auto* StoryRuneStone = MakeBatch(TEXT("StoryRuneStone"), StoryRockMesh,
        FLinearColor(0.39f, 0.43f, 0.31f), false);
    auto* StoryRuneGlyph = MakeBatch(TEXT("StoryRuneGlyph"), StoryRockMesh,
        FLinearColor(0.72f, 0.86f, 0.54f), false);
    const FVector RuneBase = FableRPG::StoryObjectLocation(4);
    Place(StoryRuneStone, RuneBase + FVector(0, 0, 20), FVector(0.72f, 0.36f, 1.45f));
    const FVector GlyphPoints[] = {FVector(0,-31,48), FVector(0,-31,73), FVector(0,-31,98),
        FVector(-19,-30,109), FVector(19,-30,109), FVector(-31,-28,129), FVector(31,-28,129)};
    for (const FVector& Point : GlyphPoints)
        Place(StoryRuneGlyph, RuneBase + Point, FVector(0.045f, 0.04f, 0.11f));
    Place(StoryRuneStone, RuneBase + FVector(-62, -8, 2), FVector(0.30f, 0.27f, 0.15f), FRotator(0, 32, 0));
    Place(StoryRuneStone, RuneBase + FVector(60, 16, 2), FVector(0.25f, 0.31f, 0.16f), FRotator(0, -27, 0));
    AddLight(TEXT("FableDungeon_StoryRuneGlow"), RuneBase + FVector(0, -70, 100),
        FLinearColor(0.60f, 0.88f, 0.48f), 650, 260);

    const FVector Board = FableRPG::BoardLocation();
    for (float Side : {-1.0f, 1.0f})
        Place(Wood, Board + FVector(Side * 75,0,105), FVector(0.17f,0.20f,2.1f));
    Place(Wood, Board + FVector(0,0,175), FVector(1.85f,0.22f,1.0f));
    for (int32 I = 0; I < 3; ++I)
        Place(Paper, Board + FVector(-53 + I * 53,-13,176), FVector(0.42f,0.015f,0.68f), FRotator(0,0,(I-1)*4));

    const FVector Forge = FableRPG::ForgeLocation();
    Place(Walls, Forge + FVector(0,0,40), FVector(1.7f,1.3f,0.8f));
    Place(Metal, Forge + FVector(0,0,96), FVector(1.35f,0.66f,0.32f));
    Place(Metal, Forge + FVector(-25,0,124), FVector(0.38f,0.43f,0.25f));
    Place(Flame, Forge + FVector(45,0,91), FVector(0.3f,0.35f,0.35f));
    AddLight(TEXT("FableDungeon_ForgeGlow"), Forge + FVector(40,-25,135), FLinearColor(1,0.37f,0.08f), 1100, 340);

    AddVillager(TEXT("Guide"), Board + FVector(-145,-55,0), FLinearColor(0.05f,0.24f,0.18f), FLinearColor(0.18f,0.14f,0.10f));
    AddVillager(TEXT("Smith"), Forge + FVector(-155,25,0), FLinearColor(0.28f,0.10f,0.035f), FLinearColor(0.055f,0.025f,0.015f));
    const FVector Merchant = FableRPG::MerchantLocation();
    AddVillager(TEXT("Merchant"), Merchant + FVector(-125,35,0), FLinearColor(0.30f,0.09f,0.28f), FLinearColor(0.13f,0.045f,0.015f));
    // A low supply counter marks the shop while leaving the walking lanes clear.
    Place(Wood,Merchant + FVector(30,30,64),FVector(1.45f,0.62f,0.16f));
    for(float Side : {-1.f,1.f})
        Place(Wood,Merchant + FVector(30+Side*55,30,30),FVector(0.12f,0.42f,0.60f));
    auto* HealthBottle=MakeBatch(TEXT("HealthBottles"),SphereMesh,FLinearColor(0.65f,0.045f,0.07f),false);
    auto* ManaBottle=MakeBatch(TEXT("ManaBottles"),SphereMesh,FLinearColor(0.04f,0.29f,0.75f),false);
    for(int32 I=0;I<3;++I)
    {
        Place(HealthBottle,Merchant+FVector(I*18,15,82),FVector(0.12f,0.12f,0.18f));
        Place(ManaBottle,Merchant+FVector(I*18+5,44,82),FVector(0.12f,0.12f,0.18f));
    }

    const auto MakePortal = [&](FVector Base)
    {
        Place(PortalStone, Base + FVector(-108,0,130), FVector(0.48f,0.66f,2.6f));
        Place(PortalStone, Base + FVector(108,0,130), FVector(0.48f,0.66f,2.6f));
        Place(PortalStone, Base + FVector(0,0,275), FVector(2.65f,0.78f,0.4f));
        Place(Portal, Base + FVector(0,5,140), FVector(1.75f,2.45f,0.05f), FRotator(0,0,90));
    };
    const FVector PortalPosition = FableRPG::PortalLocation();
    MakePortal(PortalPosition);
    AddLight(TEXT("FableDungeon_EntranceGlow"), PortalPosition + FVector(0,-65,150), FLinearColor(0.18f,0.65f,1), 1300, 430);

    // The shared service position keeps the complete arch left of both arrival cameras.
    const FVector ReturnPortal = FableRPG::RegionReturnPortal(EFableRegion::RootRuins);
    MakePortal(ReturnPortal);
    AddLight(TEXT("FableDungeon_ReturnGlow"), ReturnPortal + FVector(0,65,150), FLinearColor(0.18f,0.65f,1), 1600, 450);
}
