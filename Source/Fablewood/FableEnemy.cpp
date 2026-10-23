#include "FableEnemy.h"

#include "FableCharacter.h"
#include "FableEnemyBolt.h"
#include "FableCombatFeedback.h"
#include "FableGameMode.h"
#include "FablePlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AFableEnemy::AFableEnemy()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.0f);
    SetMinNetUpdateFrequency(10.0f);

    Collision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("EnemyCollision"));
    SetRootComponent(Collision);
    Collision->InitCapsuleSize(48, 100);
    Collision->SetCollisionProfileName(TEXT("Pawn"));
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Collision->SetCanEverAffectNavigation(false);

    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GuardianVisuals"));
    VisualRoot->SetupAttachment(Collision);

    // Authored in centimetres at capsule-centred origin; only the limbs use local pivots.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(TEXT("/Game/Art/Meshes/SM_GuardianBody.SM_GuardianBody"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BarkMesh(TEXT("/Game/Art/Meshes/SM_GuardianBark.SM_GuardianBark"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> FaceMesh(TEXT("/Game/Art/Meshes/SM_GuardianFace.SM_GuardianFace"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> EyesMesh(TEXT("/Game/Art/Meshes/SM_GuardianEyes.SM_GuardianEyes"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CrownMesh(TEXT("/Game/Art/Meshes/SM_GuardianCrown.SM_GuardianCrown"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CrownTipsMesh(TEXT("/Game/Art/Meshes/SM_GuardianCrownTips.SM_GuardianCrownTips"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmLMesh(TEXT("/Game/Art/Meshes/SM_GuardianArmL.SM_GuardianArmL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmRMesh(TEXT("/Game/Art/Meshes/SM_GuardianArmR.SM_GuardianArmR"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmBarkLMesh(TEXT("/Game/Art/Meshes/SM_GuardianArmBarkL.SM_GuardianArmBarkL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmBarkRMesh(TEXT("/Game/Art/Meshes/SM_GuardianArmBarkR.SM_GuardianArmBarkR"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegLMesh(TEXT("/Game/Art/Meshes/SM_GuardianLegL.SM_GuardianLegL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegRMesh(TEXT("/Game/Art/Meshes/SM_GuardianLegR.SM_GuardianLegR"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegBarkLMesh(TEXT("/Game/Art/Meshes/SM_GuardianLegBarkL.SM_GuardianLegBarkL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegBarkRMesh(TEXT("/Game/Art/Meshes/SM_GuardianLegBarkR.SM_GuardianLegBarkR"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    const auto CreatePart = [this](const FName Name, UStaticMesh* Mesh, USceneComponent* Parent,
                                   UMaterialInterface* Base, const FName ColorTag)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Part->SetupAttachment(Parent);
        Part->SetStaticMesh(Mesh);
        Part->SetMaterial(0, Base);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->ComponentTags.Add(ColorTag);
        return Part;
    };

    Body = CreatePart(TEXT("OakBody"), BodyMesh.Object, VisualRoot, Paint.Object, TEXT("Bark"));
    CreatePart(TEXT("CarvedBarkPlates"), BarkMesh.Object, Body, Paint.Object, TEXT("BarkHighlight"));
    CreatePart(TEXT("RecessedWoodenMask"), FaceMesh.Object, Body, Paint.Object, TEXT("FaceGroove"));
    CreatePart(TEXT("AmberEyes"), EyesMesh.Object, Body, Paint.Object, TEXT("Eye"));
    Crown = CreatePart(TEXT("MossCrown"), CrownMesh.Object, Body, Paint.Object, TEXT("Leaf"));
    CreatePart(TEXT("MossCrownTips"), CrownTipsMesh.Object, Body, Paint.Object, TEXT("LeafTip"));

    for (int32 Side = 0; Side < 2; ++Side)
    {
        const float Sign = Side == 0 ? -1.0f : 1.0f;
        UStaticMeshComponent* Arm = CreatePart(*FString::Printf(TEXT("BranchArm%d"), Side),
            Side == 0 ? ArmLMesh.Object : ArmRMesh.Object, VisualRoot, Paint.Object, TEXT("Bark"));
        Arm->SetRelativeLocation(FVector(0, Sign * 48, 26));
        BranchArms.Add(Arm);
        CreatePart(*FString::Printf(TEXT("BranchArmBark%d"), Side),
            Side == 0 ? ArmBarkLMesh.Object : ArmBarkRMesh.Object, Arm, Paint.Object, TEXT("BarkHighlight"));

        UStaticMeshComponent* Leg = CreatePart(*FString::Printf(TEXT("RootLeg%d"), Side),
            Side == 0 ? LegLMesh.Object : LegRMesh.Object, VisualRoot, Paint.Object, TEXT("Bark"));
        Leg->SetRelativeLocation(FVector(0, Sign * 24, -43));
        RootLegs.Add(Leg);
        CreatePart(*FString::Printf(TEXT("RootLegBark%d"), Side),
            Side == 0 ? LegBarkLMesh.Object : LegBarkRMesh.Object, Leg, Paint.Object, TEXT("BarkHighlight"));
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    WaterLantern = CreatePart(TEXT("WaterLanternCore"), Sphere.Object, VisualRoot, Paint.Object, TEXT("LeafTip"));
    WaterLantern->SetRelativeScale3D(FVector(0.62f,0.62f,0.85f));
    WaterLantern->SetRelativeLocation(FVector(0,0,16));
    WaterPetals = CreatePart(TEXT("WaterLanternPetals"), CrownMesh.Object, VisualRoot, Paint.Object, TEXT("Leaf"));
    WaterPetals->SetRelativeLocation(FVector(0,0,-14));
    WaterPetals->SetRelativeScale3D(FVector(1.25f,1.25f,1.1f));
    for (int32 Side=0; Side<2; ++Side)
    {
        UStaticMeshComponent* Antler=CreatePart(*FString::Printf(TEXT("MoonAntler%d"),Side),
            Side==0?ArmLMesh.Object:ArmRMesh.Object,VisualRoot,Paint.Object,TEXT("BarkHighlight"));
        Antler->SetRelativeLocation(FVector(0,Side==0?-38.0f:38.0f,100));
        Antler->SetRelativeRotation(FRotator(155,0,Side==0?-30.0f:30.0f));
        Antler->SetRelativeScale3D(FVector(1.25f));
        MoonAntlers.Add(Antler);
    }

    WarningRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AttackWarning"));
    WarningRoot->SetupAttachment(Collision);
    WarningRoot->SetRelativeLocation(FVector(0, 0, -106));

    constexpr int32 SegmentCount = 40;
    for (int32 I = 0; I < SegmentCount; ++I)
    {
        const float Angle = 2.0f * PI * I / SegmentCount;
        UStaticMeshComponent* Segment = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("WarningSegment%d"), I));
        Segment->SetupAttachment(WarningRoot);
        Segment->SetStaticMesh(Cube.Object);
        Segment->SetMaterial(0, Material.Object);
        Segment->SetRelativeLocation(FVector(FMath::Cos(Angle) * AttackRadius, FMath::Sin(Angle) * AttackRadius, 0));
        Segment->SetRelativeRotation(FRotator(0, FMath::RadiansToDegrees(Angle) + 90, 0));
        Segment->SetRelativeScale3D(FVector(0.35f, 0.08f, 0.025f));
        Segment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Segment->SetCastShadow(false);
        WarningSegments.Add(Segment);
    }
    WarningRoot->SetVisibility(false, true);

    DamageText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("DamageNumber"));
    DamageText->SetupAttachment(Collision);
    DamageText->SetRelativeLocation(FVector(0, 0, 136));
    DamageText->SetHorizontalAlignment(EHTA_Center);
    DamageText->SetVerticalAlignment(EVRTA_TextCenter);
    DamageText->SetWorldSize(31.0f);
    DamageText->SetTextRenderColor(FColor(255, 236, 164));
    DamageText->SetCastShadow(false);
    DamageText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DamageText->SetCanEverAffectNavigation(false);
    DamageText->SetVisibility(false);
}

void AFableEnemy::BeginPlay()
{
    Super::BeginPlay();

    if (UMaterialInterface* Base = Body->GetMaterial(0))
    {
        const auto MakePaint = [this, Base](const FLinearColor Color, const float Roughness)
        {
            UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, this);
            Instance->SetVectorParameterValue(TEXT("Color"), Color);
            Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
            return Instance;
        };
        BodyMaterial = MakePaint(FLinearColor(0.27f, 0.14f, 0.055f), 0.92f);
        BarkHighlightMaterial = MakePaint(FLinearColor(0.45f, 0.27f, 0.115f), 0.88f);
        CrownMaterial = MakePaint(FLinearColor(0.14f, 0.25f, 0.055f), 0.86f);
        CrownTipMaterial = MakePaint(FLinearColor(0.30f, 0.43f, 0.095f), 0.82f);
        UMaterialInstanceDynamic* FaceMaterial = MakePaint(FLinearColor(0.055f, 0.029f, 0.014f), 1.0f);
        UMaterialInstanceDynamic* EyeMaterial = MakePaint(FLinearColor(1.0f, 0.59f, 0.075f), 0.30f);

        TArray<UStaticMeshComponent*> Meshes;
        GetComponents(Meshes);
        for (UStaticMeshComponent* Mesh : Meshes)
        {
            if (Mesh->ComponentHasTag(TEXT("Bark"))) { Mesh->SetMaterial(0, BodyMaterial); }
            else if (Mesh->ComponentHasTag(TEXT("BarkHighlight"))) { Mesh->SetMaterial(0, BarkHighlightMaterial); }
            else if (Mesh->ComponentHasTag(TEXT("FaceGroove"))) { Mesh->SetMaterial(0, FaceMaterial); }
            else if (Mesh->ComponentHasTag(TEXT("Eye"))) { Mesh->SetMaterial(0, EyeMaterial); }
            else if (Mesh->ComponentHasTag(TEXT("Leaf"))) { Mesh->SetMaterial(0, CrownMaterial); }
            else if (Mesh->ComponentHasTag(TEXT("LeafTip"))) { Mesh->SetMaterial(0, CrownTipMaterial); }
        }
    }
    // The combat warning retains its own material and dimensions.
    if (!WarningSegments.IsEmpty())
    {
        if (UMaterialInterface* Base = WarningSegments[0]->GetMaterial(0))
        {
            WarningMaterial = UMaterialInstanceDynamic::Create(Base, this);
            WarningMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.31f, 0.075f));
            for (UStaticMeshComponent* Segment : WarningSegments)
            {
                Segment->SetMaterial(0, WarningMaterial);
            }
        }
    }
    OnRep_CombatVisuals();
}

void AFableEnemy::ConfigureEncounter(bool bAncientWarden, bool bInDungeon, EFableDifficulty Difficulty)
{
    if (!HasAuthority() || State == EFableEnemyState::Defeated
        || (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard)) return;
    bBoss = bAncientWarden;
    bDungeonGuardian = bInDungeon;
    EncounterRegion = bInDungeon ? EFableRegion::RootRuins : EFableRegion::Village;
    EncounterDifficulty = bInDungeon ? Difficulty : EFableDifficulty::Normal;
    bRanged = false;
    TelegraphLocations.Empty();
    // Always derive from base HP, never multiply already-scaled health.
    MaxHealth = (bBoss ? 600.0f : (bDungeonGuardian ? 110.0f : 90.0f))
        * (EncounterDifficulty == EFableDifficulty::Hard ? 1.45f : 1.0f);
    Health = MaxHealth;
    LastVisualHealth = Health;
    AttackCount = 0;
    SlowMultiplier = 1.0f;
    SlowEndServerTime = 0.0f;
    ClearBurn();
    OnRep_CombatVisuals();
    ForceNetUpdate();
}

void AFableEnemy::ConfigureMoonwell(bool bMoonWarden, bool bWaterSpirit, EFableDifficulty Difficulty)
{
    if (!HasAuthority() || !IsAlive()
        || (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard)) return;
    ConfigureEncounter(bMoonWarden, true, Difficulty);
    EncounterRegion=EFableRegion::Moonwell;
    bRanged=!bMoonWarden && bWaterSpirit;
    MaxHealth=(bBoss?720.0f:(bRanged?100.0f:120.0f))
        * (EncounterDifficulty == EFableDifficulty::Hard ? 1.45f : 1.0f);
    Health=MaxHealth; LastVisualHealth=Health;
    OnRep_CombatVisuals(); ForceNetUpdate();
}

void AFableEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableEnemy, Health);
    DOREPLIFETIME(AFableEnemy, MaxHealth);
    DOREPLIFETIME(AFableEnemy, State);
    DOREPLIFETIME(AFableEnemy, AttackPattern);
    DOREPLIFETIME(AFableEnemy, bBoss);
    DOREPLIFETIME(AFableEnemy, bDungeonGuardian);
    DOREPLIFETIME(AFableEnemy, EncounterRegion);
    DOREPLIFETIME(AFableEnemy, EncounterDifficulty);
    DOREPLIFETIME(AFableEnemy, bRanged);
    DOREPLIFETIME(AFableEnemy, TelegraphLocations);
    DOREPLIFETIME(AFableEnemy, WindupEndServerTime);
    DOREPLIFETIME(AFableEnemy, WindupDuration);
    DOREPLIFETIME(AFableEnemy, SlowMultiplier);
    DOREPLIFETIME(AFableEnemy, SlowEndServerTime);
    DOREPLIFETIME(AFableEnemy, BurnEndServerTime);
    DOREPLIFETIME(AFableEnemy, BurnTicksApplied);
    DOREPLIFETIME(AFableEnemy, TotalBurnDamage);
}

void AFableEnemy::FindTarget()
{
    AFableCharacter* Closest = nullptr;
    float ClosestDistanceSquared = FMath::Square(bBoss ? 2300.0f : 1700.0f);
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->IsAlive() || !FableRPG::IsInRegion(EncounterRegion,It->GetActorLocation())) continue;
        const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), It->GetActorLocation());
        if (DistanceSquared < ClosestDistanceSquared && FMath::Abs(It->GetActorLocation().Z - GetActorLocation().Z) < 400)
        {
            Closest = *It;
            ClosestDistanceSquared = DistanceSquared;
        }
    }
    Target = Closest;
}

float AFableEnemy::GetMoveSpeed() const
{
    const float BaseSpeed = bBoss ? (IsEnraged() ? 190.0f : 145.0f) : (bDungeonGuardian ? 155.0f : 135.0f);
    return (bRanged ? 135.0f : BaseSpeed) * GetSlowMultiplier();
}

float AFableEnemy::GetSynchronizedServerTime() const
{
    if (!GetWorld()) return 0.0f;
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

float AFableEnemy::GetSlowTimeRemaining() const
{
    return IsAlive() ? FMath::Max(0.0f, SlowEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

float AFableEnemy::GetSlowMultiplier() const
{
    return GetSlowTimeRemaining() > 0.0f ? SlowMultiplier : 1.0f;
}

bool AFableEnemy::ApplySlow(float Multiplier, float Duration)
{
    if (!HasAuthority() || !IsAlive() || !GetWorld() || !FMath::IsFinite(Multiplier)
        || !FMath::IsFinite(Duration) || Multiplier <= 0.0f || Multiplier >= 1.0f || Duration <= 0.0f) return false;
    const float EffectiveMultiplier = FMath::Clamp(Multiplier, bBoss ? 0.8f : 0.4f, 1.0f);
    const float EffectiveDuration = FMath::Min(Duration, bBoss ? 2.0f : 5.0f);
    const float CurrentMultiplier = GetSlowMultiplier();
    if (CurrentMultiplier < EffectiveMultiplier - KINDA_SMALL_NUMBER) return false;
    const float EndTime = GetSynchronizedServerTime() + EffectiveDuration;
    // Weaker slows never extend a stronger slow. Equal strength can refresh it;
    // a stronger replacement uses its own duration rather than inheriting one.
    SlowEndServerTime = FMath::IsNearlyEqual(CurrentMultiplier, EffectiveMultiplier)
        ? FMath::Max(SlowEndServerTime, EndTime) : EndTime;
    SlowMultiplier = EffectiveMultiplier;
    ForceNetUpdate();
    return true;
}

float AFableEnemy::GetBurnTimeRemaining() const
{
    return IsAlive() ? FMath::Max(0.0f, BurnEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

void AFableEnemy::ClearBurn()
{
    BurnDamagePerTick = NextBurnServerTime = BurnEndServerTime = 0.0f;
    BurnTicksRemaining = 0;
    BurnInstigator.Reset();
    BurnSourcePawn.Reset();
}

bool AFableEnemy::ApplyBurn(float DamagePerTick, int32 TickCount, AController* InstigatorController)
{
    AFableCharacter* Source = IsValid(InstigatorController) ? Cast<AFableCharacter>(InstigatorController->GetPawn()) : nullptr;
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !IsValid(Source) || !Source->IsAlive()
        || !FableRPG::IsInRegion(EncounterRegion, Source->GetActorLocation())
        || !FMath::IsFinite(DamagePerTick) || DamagePerTick <= 0.0f || DamagePerTick > 10000.0f
        || TickCount < 1 || TickCount > 3) return false;
    // One burn per enemy. A weaker refresh cannot replace a stronger payload or its owner.
    if (BurnTicksRemaining > 0 && BurnDamagePerTick > DamagePerTick + KINDA_SMALL_NUMBER) return false;
    const float Now = GetWorld()->GetTimeSeconds();
    const bool bEqualRefresh = BurnTicksRemaining > 0 && FMath::IsNearlyEqual(BurnDamagePerTick, DamagePerTick);
    NextBurnServerTime = bEqualRefresh ? FMath::Min(NextBurnServerTime, Now + 1.0f) : Now + 1.0f;
    BurnDamagePerTick = DamagePerTick;
    BurnTicksRemaining = TickCount;
    BurnEndServerTime = NextBurnServerTime + TickCount - 1.0f;
    BurnInstigator = InstigatorController;
    BurnSourcePawn = Source;
    ForceNetUpdate();
    return true;
}

void AFableEnemy::TickBurn()
{
    if (!HasAuthority() || !GetWorld() || BurnTicksRemaining <= 0) return;
    AFableCharacter* Source = BurnSourcePawn.Get();
    AController* InstigatorController = BurnInstigator.Get();
    if (!IsAlive() || !IsValid(Source) || !Source->IsAlive() || !IsValid(InstigatorController)
        || InstigatorController->GetPawn() != Source
        || !FableRPG::IsInRegion(EncounterRegion, Source->GetActorLocation()))
    {
        ClearBurn(); ForceNetUpdate(); return;
    }
    // Bounded catch-up preserves at most the three scheduled ticks during a frame hitch.
    bool bBurnChanged = false;
    for (int32 TickIndex = 0; TickIndex < 3 && BurnTicksRemaining > 0 && IsAlive()
        && GetWorld()->GetTimeSeconds() >= NextBurnServerTime; ++TickIndex)
    {
        bBurnChanged = true;
        const float Damage = BurnDamagePerTick;
        --BurnTicksRemaining;
        NextBurnServerTime += 1.0f;
        const float PreviousHealth = Health;
        ReceiveHit(Damage, InstigatorController);
        const float ActualDamage = PreviousHealth - Health;
        if (ActualDamage > 0.0f)
        {
            ++BurnTicksApplied;
            TotalBurnDamage += ActualDamage;
            Source->RecordTalentBurnDamage(ActualDamage);
        }
        // ReceiveHit commits defeat before reward dispatch; DoT never calls trial-hit credit.
    }
    if (BurnTicksRemaining <= 0 || !IsAlive()) ClearBurn();
    if (bBurnChanged) ForceNetUpdate();
}

float AFableEnemy::GetRecoveryTime() const
{
    if (EncounterRegion==EFableRegion::Moonwell && bBoss) return IsEnraged()?0.42f:1.15f;
    return bRanged ? 1.4f : (bBoss ? (IsEnraged() ? 0.75f : 1.25f) : 1.0f);
}

void AFableEnemy::BeginAttack()
{
    TelegraphLocations.Empty();
    if (EncounterRegion==EFableRegion::Moonwell && bBoss)
    {
        AttackPattern=(AttackCount%2==0)?EFableEnemyAttack::MoonRing:EFableEnemyAttack::Moonfall;
        WindupDuration=AttackPattern==EFableEnemyAttack::MoonRing?1.65f:1.35f;
        if (AttackPattern==EFableEnemyAttack::Moonfall)
            for (TActorIterator<AFableCharacter> It(GetWorld());It && TelegraphLocations.Num()<2;++It)
                if (It->IsAlive() && FableRPG::IsInRegion(EncounterRegion,It->GetActorLocation()))
                    TelegraphLocations.Add(FVector(It->GetActorLocation().X,It->GetActorLocation().Y,4));
    }
    else if (bRanged) { AttackPattern=EFableEnemyAttack::WaterBolt; WindupDuration=0.95f; }
    else
    {
        AttackPattern=bBoss && (AttackCount%2==1)?EFableEnemyAttack::RootLine:EFableEnemyAttack::Stomp;
        WindupDuration=bBoss
            ? (AttackPattern==EFableEnemyAttack::RootLine?(IsEnraged()?1.15f:1.50f):(IsEnraged()?0.95f:1.25f))
            :0.90f;
    }
    ++AttackCount;
    if (AFableCharacter* Victim=Target.Get())
    {
        FVector Direction=Victim->GetActorLocation()-GetActorLocation(); Direction.Z=0;
        if (!Direction.IsNearlyZero()) SetActorRotation(Direction.Rotation());
    }
    SetState(EFableEnemyState::Windup);
}

void AFableEnemy::SetState(EFableEnemyState NewState)
{
    if (State == NewState) return;
    State = NewState;
    StateTime = 0;
    WindupEndServerTime = NewState == EFableEnemyState::Windup
        ? GetWorld()->GetTimeSeconds() + WindupDuration : 0.0f;
    OnRep_CombatVisuals();
    ForceNetUpdate();
}

void AFableEnemy::UpdateWarningGeometry()
{
    const float HalfHeight = bBoss ? 180.0f : 100.0f;
    Collision->SetCapsuleSize(bBoss ? 88.0f : 48.0f, HalfHeight, false);
    WarningRoot->SetRelativeLocation(FVector(0, 0, -HalfHeight - 6.0f));
    const int32 Count = WarningSegments.Num();
    for (int32 I = 0; I < Count; ++I)
    {
        FVector Position;
        float Yaw = 0;
        float Length = 35.0f;
        if (AttackPattern == EFableEnemyAttack::MoonRing)
        {
            const int32 Half=Count/2;
            const float Radius=I<Half?MoonRingInnerRadius:MoonRingOuterRadius;
            const float Angle=2.0f*PI*(I%Half)/Half;
            Position=FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0);
            Yaw=FMath::RadiansToDegrees(Angle)+90;
            Length=2.0f*PI*Radius/Half;
        }
        else if (AttackPattern == EFableEnemyAttack::Moonfall)
        {
            const int32 Circles=FMath::Clamp(TelegraphLocations.Num(),1,2);
            const int32 PerCircle=Count/Circles;
            const int32 Circle=FMath::Min(I/PerCircle,Circles-1);
            const FVector Center=TelegraphLocations.IsValidIndex(Circle)
                ? GetActorTransform().InverseTransformPosition(TelegraphLocations[Circle]):FVector::ZeroVector;
            const float Angle=2.0f*PI*(I%PerCircle)/PerCircle;
            Position=FVector(Center.X+FMath::Cos(Angle)*MoonfallRadius,Center.Y+FMath::Sin(Angle)*MoonfallRadius,0);
            Yaw=FMath::RadiansToDegrees(Angle)+90;
            Length=2.0f*PI*MoonfallRadius/PerCircle;
        }
        else if (bRanged && AttackPattern == EFableEnemyAttack::WaterBolt)
        {
            Position=FVector(60.0f+I*21.0f,0,0); Length=15;
        }
        else if (bBoss && AttackPattern == EFableEnemyAttack::RootLine)
        {
            const float Width = RootLineHalfWidth * 2.0f;
            const float Perimeter = 2.0f * (RootLineLength + Width);
            float Along = (I + 0.5f) * Perimeter / Count;
            Length = Perimeter / Count + 1;
            if (Along < RootLineLength)
                Position = FVector(Along, -RootLineHalfWidth, 0);
            else if ((Along -= RootLineLength) < Width)
            {
                Position = FVector(RootLineLength, Along - RootLineHalfWidth, 0);
                Yaw = 90;
            }
            else if ((Along -= Width) < RootLineLength)
                Position = FVector(RootLineLength - Along, RootLineHalfWidth, 0);
            else
            {
                Along -= RootLineLength;
                Position = FVector(0, RootLineHalfWidth - Along, 0);
                Yaw = 90;
            }
        }
        else
        {
            const float Radius = bBoss ? BossStompRadius : AttackRadius;
            const float Angle = 2.0f * PI * I / Count;
            Position = FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
            Yaw = FMath::RadiansToDegrees(Angle) + 90;
            Length = 2.0f * PI * Radius / Count;
        }
        WarningSegments[I]->SetRelativeLocation(Position);
        WarningSegments[I]->SetRelativeRotation(FRotator(0, Yaw, 0));
        WarningSegments[I]->SetRelativeScale3D(FVector(Length / 100.0f, bBoss ? 0.13f : 0.08f, 0.025f));
    }
}

void AFableEnemy::OnRep_CombatVisuals()
{
    if (State != LastVisualState)
    {
        LastVisualState = State;
        VisualStateTime = 0;
    }
    if (Health < LastVisualHealth) HitFlashRemaining = 0.16f;
    LastVisualHealth = Health;
    UpdateWarningGeometry();
    WarningRoot->SetVisibility(State == EFableEnemyState::Windup, true);
    Collision->SetCollisionEnabled(State == EFableEnemyState::Defeated ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    if (CrownMaterial)
        CrownMaterial->SetVectorParameterValue(TEXT("Color"), State == EFableEnemyState::Defeated
            ? FLinearColor(0.22f,0.17f,0.07f)
            : (IsEnraged() ? FLinearColor(0.45f,0.16f,0.04f) : FLinearColor(0.14f,0.25f,0.055f)));
    if (CrownTipMaterial)
        CrownTipMaterial->SetVectorParameterValue(TEXT("Color"), State == EFableEnemyState::Defeated
            ? FLinearColor(0.40f,0.27f,0.10f)
            : (IsEnraged() ? FLinearColor(0.85f,0.35f,0.055f) : FLinearColor(0.30f,0.43f,0.095f)));
    const bool bMoon=EncounterRegion==EFableRegion::Moonwell;
    Body->SetVisibility(!bRanged,true);
    for (UStaticMeshComponent* Arm:BranchArms) Arm->SetVisibility(!bRanged,true);
    for (UStaticMeshComponent* Leg:RootLegs) Leg->SetVisibility(!bRanged,true);
    WaterLantern->SetVisibility(bRanged);
    WaterPetals->SetVisibility(bRanged);
    for (UStaticMeshComponent* Antler:MoonAntlers) Antler->SetVisibility(bMoon && bBoss);
    if (bMoon && CrownMaterial) CrownMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(0.16f,0.30f,0.48f));
    if (bMoon && CrownTipMaterial) CrownTipMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(0.35f,0.78f,0.85f));
    VisualRoot->SetRelativeScale3D(FVector(bBoss ? 1.8f : 1.0f));
}

void AFableEnemy::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TickPresentation(DeltaSeconds);
    if (HasAuthority()) FlushHitFeedback();
    if (!HasAuthority() || !IsAlive()) return;
    TickBurn();
    if (!IsAlive()) return;
    if (SlowMultiplier < 1.0f && GetSlowTimeRemaining() <= 0.0f)
    {
        SlowMultiplier = 1.0f;
        SlowEndServerTime = 0.0f;
        ForceNetUpdate();
    }
    StateTime += DeltaSeconds;
    if (State == EFableEnemyState::Windup)
    {
        if (StateTime >= WindupDuration)
        {
            ApplyAreaAttack();
            SetState(EFableEnemyState::Recovering);
        }
        return;
    }
    if (State == EFableEnemyState::Recovering)
    {
        if (StateTime >= GetRecoveryTime()) SetState(EFableEnemyState::Idle);
        return;
    }
    TargetSearchRemaining -= DeltaSeconds;
    if (TargetSearchRemaining <= 0)
    {
        FindTarget();
        TargetSearchRemaining = 0.35f;
    }
    AFableCharacter* Victim = Target.Get();
    if (!Victim || !Victim->IsAlive() || !FableRPG::IsInRegion(EncounterRegion,Victim->GetActorLocation()))
    {
        SetState(EFableEnemyState::Idle);
        return;
    }
    FVector ToVictim = Victim->GetActorLocation() - GetActorLocation();
    ToVictim.Z = 0;
    const float Distance = ToVictim.Size();
    const bool bNextLine = bBoss && (AttackCount % 2 == 1);
    const float TriggerRange = bRanged ? 1050.0f : ((bBoss && EncounterRegion==EFableRegion::Moonwell)
        ? 1100.0f : (bBoss ? (bNextLine ? RootLineLength-80.0f : BossStompRadius-55.0f) : AttackRadius-40.0f));
    FCollisionQueryParams SightParams(SCENE_QUERY_STAT(FableEnemySight),false,this);
    FHitResult SightHit;
    const bool bVisible=!bRanged || !GetWorld()->LineTraceSingleByChannel(SightHit,
        GetActorLocation(),Victim->GetActorLocation(),ECC_Visibility,SightParams) || SightHit.GetActor()==Victim;
    if (Distance <= TriggerRange && bVisible)
    {
        BeginAttack();
        return;
    }
    SetState(EFableEnemyState::Chasing);
    const FVector Direction = ToVictim.GetSafeNormal();
    SetActorRotation(FMath::RInterpTo(GetActorRotation(), Direction.Rotation(), DeltaSeconds, 5.0f));
    FHitResult Hit;
    const FVector MoveDelta = Direction * FMath::Min(GetMoveSpeed() * DeltaSeconds, FMath::Max(0.0f, Distance - 150.0f));
    AddActorWorldOffset(MoveDelta, true, &Hit);
    if (Hit.bBlockingHit)
    {
        const FVector Slide = FVector::VectorPlaneProject(MoveDelta * (1.0f - Hit.Time), Hit.Normal);
        AddActorWorldOffset(Slide, true);
    }
}

void AFableEnemy::TickPresentation(float DeltaSeconds)
{
    VisualStateTime += DeltaSeconds;
    HitFlashRemaining = FMath::Max(0.0f, HitFlashRemaining - DeltaSeconds);
    FLinearColor BarkBase = bBoss
        ? (IsEnraged() ? FLinearColor(0.38f, 0.095f, 0.035f) : FLinearColor(0.22f, 0.24f, 0.14f))
        : FLinearColor(0.27f, 0.14f, 0.055f);
    FLinearColor HighlightBase = bBoss ? FLinearColor(0.51f, 0.42f, 0.20f) : FLinearColor(0.45f, 0.27f, 0.115f);
    if (EncounterRegion==EFableRegion::Moonwell)
    {
        BarkBase=bBoss?FLinearColor(0.19f,0.24f,0.40f):FLinearColor(0.23f,0.31f,0.38f);
        HighlightBase=FLinearColor(0.51f,0.68f,0.72f);
    }
    if (GetSlowTimeRemaining() > 0.0f)
    {
        BarkBase = FMath::Lerp(BarkBase, FLinearColor(0.15f, 0.43f, 0.64f), 0.68f);
        HighlightBase = FLinearColor(0.42f, 0.78f, 0.88f);
    }
    DamageTextRemaining = FMath::Max(0.0f, DamageTextRemaining - DeltaSeconds);
    if (DamageTextRemaining > 0.0f)
    {
        DamageText->SetRelativeLocation(FVector(0, 0, (bBoss ? 226.0f : 136.0f)
            + (0.72f - DamageTextRemaining) * 55.0f));
        if (APlayerController* Viewer = GetWorld()->GetFirstPlayerController())
        {
            FVector ViewLocation; FRotator ViewRotation;
            Viewer->GetPlayerViewPoint(ViewLocation, ViewRotation);
            DamageText->SetWorldRotation((ViewLocation - DamageText->GetComponentLocation()).Rotation());
            DamageText->SetVisibility(FVector::DistSquared(ViewLocation, GetActorLocation()) < FMath::Square(3600.0f));
        }
    }
    else
    {
        DamageText->SetVisibility(false);
        DisplayedDamage = 0.0f;
    }
    if (BodyMaterial)
    {
        BodyMaterial->SetVectorParameterValue(TEXT("Color"), HitFlashRemaining > 0.0f
            ? FLinearColor(0.78f, 0.65f, 0.34f) : BarkBase);
    }
    if (BarkHighlightMaterial)
    {
        BarkHighlightMaterial->SetVectorParameterValue(TEXT("Color"), HitFlashRemaining > 0.0f
            ? FLinearColor(0.95f, 0.82f, 0.49f) : HighlightBase);
    }
    if (State == EFableEnemyState::Defeated)
    {
        const float Scale = FMath::Max(0.08f, 1.0f - VisualStateTime * 0.65f);
        VisualRoot->SetRelativeScale3D(FVector(Scale * (bBoss ? 1.8f : 1.0f)));
        VisualRoot->SetRelativeLocation(FVector(0, 0, -VisualStateTime * 24.0f));
        return;
    }

    const AGameStateBase* GameState = GetWorld()->GetGameState();
    const float ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
    if (WarningMaterial && State == EFableEnemyState::Windup)
    {
        const float Pulse = 0.78f + 0.22f * FMath::Sin(ServerNow * (IsEnraged() ? 18.0f : 11.0f));
        WarningMaterial->SetVectorParameterValue(TEXT("Color"),
            (IsEnraged() ? FLinearColor(1.0f, 0.08f, 0.025f) : FLinearColor(1.0f, 0.35f, 0.055f)) * Pulse);
    }
    const bool bWalking = State == EFableEnemyState::Chasing;
    const float Step = ServerNow * 5.5f;
    const float Bob = bWalking ? FMath::Abs(FMath::Sin(Step)) * 3.0f : FMath::Sin(ServerNow * 1.4f) * 0.6f;
    float Windup = 0.0f;
    float ArmLift = 0.0f;
    float BodyPitch = bWalking ? 3.0f : 0.0f;
    float BodyDip = 0.0f;
    if (State == EFableEnemyState::Windup)
    {
        const float Progress = FMath::Clamp(1.0f - (WindupEndServerTime - ServerNow) / FMath::Max(0.1f, WindupDuration), 0.0f, 1.0f);
        Windup = Progress * Progress * (3.0f - 2.0f * Progress);
        ArmLift = Windup * ((bBoss && AttackPattern == EFableEnemyAttack::RootLine) ? 83.0f : 126.0f);
        BodyPitch = -5.0f * Windup;
        BodyDip = -5.0f * Windup;
    }
    else if (State == EFableEnemyState::Recovering)
    {
        // Presentation follows the replicated attack state; damage remains server-timed.
        if (VisualStateTime < 0.16f)
        {
            const float Slam = FMath::Clamp(VisualStateTime / 0.16f, 0.0f, 1.0f);
            ArmLift = FMath::Lerp(126.0f, -14.0f, Slam);
            BodyPitch = FMath::Lerp(-5.0f, 7.0f, Slam);
            BodyDip = -5.0f;
        }
        else
        {
            const float Settle = FMath::Clamp((VisualStateTime - 0.16f) / 0.50f, 0.0f, 1.0f);
            ArmLift = FMath::Lerp(-14.0f, 0.0f, Settle);
            BodyPitch = FMath::Lerp(7.0f, 0.0f, Settle);
            BodyDip = FMath::Lerp(-5.0f, 0.0f, Settle);
        }
    }

    VisualRoot->SetRelativeScale3D(FVector(bBoss ? 1.8f : 1.0f));
    VisualRoot->SetRelativeLocation(FVector(0, 0, Bob + BodyDip + (bRanged ? 18.0f + 9.0f*FMath::Sin(ServerNow*2.1f) : 0.0f)));
    Body->SetRelativeScale3D(FVector(1.0f + Windup * 0.04f, 1.0f + Windup * 0.04f, 1.0f - Windup * 0.05f));
    Body->SetRelativeRotation(FRotator(BodyPitch, 0, bWalking ? FMath::Sin(Step) * 1.5f : 0.0f));
    for (int32 Side = 0; Side < BranchArms.Num(); ++Side)
    {
        const float Sign = Side == 0 ? -1.0f : 1.0f;
        const float Stride = bWalking ? FMath::Sin(Step + Side * PI) : 0.0f;
        BranchArms[Side]->SetRelativeRotation(FRotator(ArmLift - Stride * 11.0f,
            Sign * Windup * 5.0f, Sign * (Windup * 8.0f + FMath::Abs(Stride) * 2.0f)));
    }
    for (int32 Side = 0; Side < RootLegs.Num(); ++Side)
    {
        const float Sign = Side == 0 ? -1.0f : 1.0f;
        const float Stride = bWalking ? FMath::Sin(Step + Side * PI) : 0.0f;
        RootLegs[Side]->SetRelativeLocation(FVector(0, Sign * 24, -43 + FMath::Max(0.0f, Stride) * 5.0f));
        RootLegs[Side]->SetRelativeRotation(FRotator(Stride * 10.0f, 0, Sign * Windup * 3.0f));
    }
}

void AFableEnemy::ApplyAreaAttack()
{
    if (!HasAuthority() || !IsAlive()) return;
    if (AttackPattern==EFableEnemyAttack::WaterBolt)
    {
        int32 Shots=0;
        for (TActorIterator<AFableEnemyBolt> It(GetWorld());It;++It) ++Shots;
        if (Shots<12)
        {
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (AFableEnemyBolt* Shot=GetWorld()->SpawnActor<AFableEnemyBolt>(GetActorLocation()+GetActorForwardVector()*65,GetActorRotation(),Params))
            {
                // The shot stores final damage; do not scale it again on impact.
                Shot->InitializeBolt(GetActorForwardVector(), 18.0f * GetDamageMultiplier(), EncounterRegion);
            }
        }
        return;
    }
    const bool bLine=bBoss && AttackPattern==EFableEnemyAttack::RootLine;
    const bool bRing=AttackPattern==EFableEnemyAttack::MoonRing;
    const bool bFall=AttackPattern==EFableEnemyAttack::Moonfall;
    const float Radius=bBoss?BossStompRadius:AttackRadius;
    const float BaseDamage=(bRing||bFall)?(IsEnraged()?28.0f:24.0f)
        :(bBoss?(bLine?(IsEnraged()?28.0f:22.0f):(IsEnraged()?32.0f:26.0f)):(bDungeonGuardian?20.0f:18.0f));
    const float Damage = BaseDamage * GetDamageMultiplier();
    for (TActorIterator<AFableCharacter> It(GetWorld());It;++It)
    {
        if (!It->IsAlive() || !FableRPG::IsInRegion(EncounterRegion,It->GetActorLocation())) continue;
        const FVector Difference=It->GetActorLocation()-GetActorLocation();
        const FVector Local=GetActorTransform().InverseTransformVectorNoScale(Difference);
        bool bInShape=false;
        if (bRing) bInShape=Difference.SizeSquared2D()>FMath::Square(MoonRingInnerRadius)
            && Difference.SizeSquared2D()<=FMath::Square(MoonRingOuterRadius);
        else if (bFall)
        {
            for (const FVector& Mark:TelegraphLocations)
                bInShape|=FVector::DistSquared2D(It->GetActorLocation(),Mark)<=FMath::Square(MoonfallRadius);
        }
        else bInShape=bLine?(Local.X>=0 && Local.X<=RootLineLength && FMath::Abs(Local.Y)<=RootLineHalfWidth)
            :Difference.SizeSquared2D()<=FMath::Square(Radius);
        if (!bInShape || FMath::Abs(Difference.Z)>=(bBoss?260.0f:150.0f)) continue;
        FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(FableEnemyAttack),false,this);
        FHitResult Obstruction;
        const bool bBlocked=GetWorld()->LineTraceSingleByChannel(Obstruction,GetActorLocation(),It->GetActorLocation(),ECC_Visibility,TraceParams);
        if (!bBlocked || Obstruction.GetActor()==*It) It->ReceiveHit(Damage);
    }
}

void AFableEnemy::FlushHitFeedback(bool bForce)
{
    if (!HasAuthority() || !GetWorld() || PendingFeedbackDamage <= 0.0f) return;
    const float Now = GetWorld()->GetTimeSeconds();
    if (!bForce && Now < NextHitFeedbackTime) return;
    MulticastHitFeedback(PendingFeedbackDamage);
    PendingFeedbackDamage = 0.0f;
    NextHitFeedbackTime = Now + 0.12f;
}

void AFableEnemy::MulticastHitFeedback_Implementation(float ActualDamage)
{
    if (GetNetMode() == NM_DedicatedServer || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f) return;
    HitFlashRemaining = 0.16f;
    DisplayedDamage = (DamageTextRemaining > 0.38f ? DisplayedDamage : 0.0f) + ActualDamage;
    DamageTextRemaining = 0.72f;
    DamageText->SetRelativeLocation(FVector(0, 0, bBoss ? 226.0f : 136.0f));
    DamageText->SetText(FText::AsNumber(FMath::Max(1, FMath::RoundToInt(DisplayedDamage))));
    DamageText->SetWorldSize(bBoss ? 39.0f : 31.0f);
    DamageText->SetVisibility(true);
    FableCombatFeedback::PlayCue(this, GetActorLocation(), 0);
}

void AFableEnemy::ReceiveHit(float Damage, AController* InstigatorController)
{
    if (!HasAuthority() || !IsAlive() || !FMath::IsFinite(Damage) || Damage <= 0) return;
    const bool bWasEnraged = IsEnraged();
    const float PreviousHealth = Health;
    Health = FMath::Max(0.0f, Health - Damage);
    if (Health < PreviousHealth && IsValid(InstigatorController))
    {
        // All melee, area and projectile damage meets here; the profile owns the slot cooldown.
        if (AFablePlayerState* Profile = InstigatorController->GetPlayerState<AFablePlayerState>())
            Profile->WearEquipment(EFableEquipmentSlot::Weapon, 1);
    }
    PendingFeedbackDamage += PreviousHealth - Health;
    FlushHitFeedback(Health <= 0.0f);
    OnRep_CombatVisuals();
    if (InstigatorController)
    {
        if (AFableCharacter* Attacker = Cast<AFableCharacter>(InstigatorController->GetPawn()))
        {
            Target = Attacker;
            TargetSearchRemaining = 0.35f;
        }
    }
    AFableGameMode* GameMode = Cast<AFableGameMode>(GetWorld()->GetAuthGameMode());
    if (bBoss && GameMode) GameMode->UpdateBossHealth(this);
    if (bBoss && !bWasEnraged && IsEnraged())
        UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_BOSS_ENRAGED health=%.0f"), Health);
    if (Health <= 0)
    {
        // Commit defeat before the callback: concurrent hits cannot reward a kill twice.
        SetState(EFableEnemyState::Defeated);
        SlowMultiplier = 1.0f;
        SlowEndServerTime = 0.0f;
        ClearBurn();
        Target.Reset();
        if (GameMode) GameMode->HandleEnemyDefeated(this);
        SetLifeSpan(2.0f);
    }
    ForceNetUpdate();
}
