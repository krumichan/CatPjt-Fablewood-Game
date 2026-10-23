#include "FableProjectile.h"

#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FablePlayerState.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AFableProjectile::AFableProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(60.0f);
    SetMinNetUpdateFrequency(30.0f);
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShotRoot"));
    SetRootComponent(SceneRoot);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterial> PaintAsset(TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    Orb = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MagicOrb"));
    Orb->SetupAttachment(SceneRoot);
    Orb->SetStaticMesh(SphereAsset.Object);
    Orb->SetRelativeScale3D(FVector(0.20f));
    ArrowShaft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowShaft"));
    ArrowShaft->SetupAttachment(SceneRoot);
    ArrowShaft->SetStaticMesh(CylinderAsset.Object);
    ArrowShaft->SetRelativeRotation(FRotator(90, 0, 0));
    ArrowShaft->SetRelativeScale3D(FVector(0.026f, 0.026f, 0.42f));
    ArrowHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowHead"));
    ArrowHead->SetupAttachment(SceneRoot);
    ArrowHead->SetStaticMesh(ConeAsset.Object);
    ArrowHead->SetRelativeLocation(FVector(24, 0, 0));
    ArrowHead->SetRelativeRotation(FRotator(-90, 0, 0));
    ArrowHead->SetRelativeScale3D(FVector(0.08f, 0.08f, 0.12f));
    for (UStaticMeshComponent* Component : { Orb.Get(), ArrowShaft.Get(), ArrowHead.Get() })
    {
        Component->SetMaterial(0, PaintAsset.Object);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false);
    }
}

void AFableProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority()) { SetLifeSpan(4.5f); }
    OnRep_Visuals();
}

void AFableProjectile::InitializeShot(const FVector& Direction, float Damage, AController* DamageInstigator,
    bool bUseArrow, float ExplosionRadius, float Speed, float MaximumRange, int32 PierceCount)
{
    if (!HasAuthority() || bInitialized || Direction.ContainsNaN() || Direction.IsNearlyZero()
        || !FMath::IsFinite(Damage) || Damage <= 0.0f || !FMath::IsFinite(ExplosionRadius)
        || !FMath::IsFinite(Speed) || !FMath::IsFinite(MaximumRange))
    {
        return;
    }
    bInitialized = true;
    bArrow = bUseArrow;
    BlastRadius = bArrow ? 0.0f : FMath::Clamp(ExplosionRadius, 0.0f, 360.0f);
    bExplosive = BlastRadius > 0.0f;
    // Explosions resolve once. Two extra pierces allow at most three unique enemy hits.
    PiercesRemaining = bExplosive ? 0 : FMath::Clamp(PierceCount, 0, 2);
    bPiercing = PiercesRemaining > 0;
    ShotDamage = FMath::Clamp(Damage, 0.0f, 10000.0f);
    SourceController = DamageInstigator;
    FlightVelocity = Direction.GetSafeNormal() * FMath::Clamp(Speed, 400.0f, 3000.0f);
    RangeRemaining = FMath::Clamp(MaximumRange, 100.0f, 2600.0f);
    SetActorRotation(Direction.Rotation());
    ForceNetUpdate();
}

void AFableProjectile::SetTrialCastContext(const FFableTrialCastContext& Context)
{
    if (HasAuthority() && !bImpacted) TrialContext = Context;
}

void AFableProjectile::SetBurnPayload(float DamagePerTick, int32 TickCount)
{
    if (!HasAuthority() || !bInitialized || bImpacted || bBurnPayloadSet || bArrow || !bExplosive
        || !FMath::IsFinite(DamagePerTick) || DamagePerTick <= 0.0f || TickCount < 1 || TickCount > 3) return;
    bBurnPayloadSet = true;
    BurnDamagePerTick = FMath::Min(DamagePerTick, 10000.0f);
    BurnTickCount = TickCount;
}

void AFableProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableProjectile, bArrow);
    DOREPLIFETIME(AFableProjectile, bExplosive);
    DOREPLIFETIME(AFableProjectile, bImpacted);
    DOREPLIFETIME(AFableProjectile, bPiercing);
    DOREPLIFETIME(AFableProjectile, UniqueEnemyHits);
}

void AFableProjectile::OnRep_Visuals()
{
    if (GetNetMode() == NM_DedicatedServer) { return; }
    Orb->SetVisibility(!bArrow);
    ArrowShaft->SetVisibility(bArrow && !bImpacted);
    ArrowHead->SetVisibility(bArrow && !bImpacted);
    Orb->SetRelativeScale3D(FVector(bImpacted ? (bExplosive ? 1.1f : 0.36f) : (bExplosive ? 0.30f : 0.20f)));
    if (UMaterialInstanceDynamic* Material = Orb->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), bExplosive
            ? FLinearColor(0.58f, 0.17f, 0.06f) : FLinearColor(0.06f, 0.34f, 0.68f));
        Material->SetScalarParameterValue(TEXT("Roughness"), 0.40f);
    }
    if (UMaterialInstanceDynamic* Material = ArrowShaft->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.24f, 0.12f, 0.035f));
    }
    if (UMaterialInstanceDynamic* Material = ArrowHead->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), bPiercing
            ? FLinearColor(0.86f, 0.66f, 0.20f) : FLinearColor(0.60f, 0.68f, 0.70f));
    }
}

void AFableProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || !bInitialized || bImpacted || !GetWorld()) return;
    if (!IsValid(GetOwner()) || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0f)
    {
        Destroy();
        return;
    }
    const float StepDistance = FMath::Min(RangeRemaining, static_cast<float>(FlightVelocity.Size()) * DeltaSeconds);
    const FVector Direction = FlightVelocity.GetSafeNormal();
    FVector Start = GetActorLocation();
    const FVector End = Start + Direction * StepDistance;
    const FCollisionShape Shape = FCollisionShape::MakeSphere(bArrow ? 3.0f : 9.0f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableProjectileSweep), false, this);
    // Co-op allies and previously hit targets cannot absorb or repeat a shot.
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
    for (const TWeakObjectPtr<AFableEnemy>& Enemy : HitEnemies)
        if (Enemy.IsValid()) Params.AddIgnoredActor(Enemy.Get());

    // A single frame can cross more than one capsule. Continue sweeping the
    // remaining segment after each pierce; never skip the wall query between them.
    for (int32 SweepIndex = 0; SweepIndex < 3; ++SweepIndex)
    {
        FHitResult ClosestHit;
        bool bHit = GetWorld()->SweepSingleByChannel(ClosestHit, Start, End,
            FQuat::Identity, ECC_Visibility, Shape, Params);
        float ClosestTime = bHit ? ClosestHit.Time : 2.0f;
        AFableEnemy* DirectTarget = bHit ? Cast<AFableEnemy>(ClosestHit.GetActor()) : nullptr;

        TArray<FHitResult> PawnHits;
        FCollisionObjectQueryParams PawnObjects;
        PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
        GetWorld()->SweepMultiByObjectType(PawnHits, Start, End, FQuat::Identity, PawnObjects, Shape, Params);
        for (const FHitResult& Hit : PawnHits)
        {
            AFableEnemy* Enemy = Cast<AFableEnemy>(Hit.GetActor());
            if (IsValid(Enemy) && Enemy->IsAlive() && !HitEnemies.Contains(TWeakObjectPtr<AFableEnemy>(Enemy))
                && Hit.Time < ClosestTime)
            {
                ClosestHit = Hit;
                ClosestTime = Hit.Time;
                DirectTarget = Enemy;
                bHit = true;
            }
        }
        if (!bHit)
        {
            SetActorLocation(End);
            break;
        }
        SetActorLocation(ClosestHit.Location);
        if (IsValid(DirectTarget) && DirectTarget->IsAlive() && PiercesRemaining > 0 && !bExplosive)
        {
            ApplyEnemyHit(DirectTarget);
            --PiercesRemaining;
            Params.AddIgnoredActor(DirectTarget);
            // Ignoring the whole pierced actor prevents its far side from being
            // hit next tick, including when several enemy capsules overlap.
            Start = ClosestHit.Location;
            ForceNetUpdate();
            continue;
        }
        ResolveImpact(ClosestHit.Location, DirectTarget);
        return;
    }
    RangeRemaining -= StepDistance;
    if (RangeRemaining <= KINDA_SMALL_NUMBER)
    {
        if (bExplosive) ResolveImpact(End, nullptr);
        else Destroy();
    }
}

void AFableProjectile::ApplyEnemyHit(AFableEnemy* Enemy)
{
    if (!HasAuthority() || !IsValid(Enemy) || !Enemy->IsAlive()
        || HitEnemies.Contains(TWeakObjectPtr<AFableEnemy>(Enemy))) return;
    HitEnemies.Add(TWeakObjectPtr<AFableEnemy>(Enemy));
    UniqueEnemyHits = static_cast<uint8>(FMath::Min(HitEnemies.Num(), 255));
    const float PreviousHealth = Enemy->GetHealth();
    Enemy->ReceiveHit(ShotDamage, SourceController.Get());
    if (Enemy->GetHealth() < PreviousHealth && SourceController.IsValid())
    {
        if (AFablePlayerState* Profile = SourceController->GetPlayerState<AFablePlayerState>())
            Profile->RecordTrialSkillHit(TrialContext);
        // No profile/talent lookup here: resetting or switching cannot rewrite an in-flight shot.
        if (Enemy->IsAlive() && BurnDamagePerTick > 0.0f && SourceController->GetPawn() == GetOwner())
            Enemy->ApplyBurn(BurnDamagePerTick, BurnTickCount, SourceController.Get());
    }
    ForceNetUpdate();
}

void AFableProjectile::ResolveImpact(const FVector& ImpactLocation, AFableEnemy* DirectTarget)
{
    if (!HasAuthority() || bImpacted || !GetWorld()) { return; }
    bImpacted = true; // Set before applying damage; the projectile can resolve only once.
    FlightVelocity = FVector::ZeroVector;
    if (bExplosive)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(FableProjectileBlast), false, this);
        for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It) { Params.AddIgnoredActor(*It); }
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            AFableEnemy* Enemy = *It;
            if (!IsValid(Enemy) || !Enemy->IsAlive()
                || FVector::DistSquared(ImpactLocation, Enemy->GetActorLocation()) > FMath::Square(BlastRadius))
            {
                continue;
            }
            FHitResult Obstruction;
            const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Obstruction, ImpactLocation,
                Enemy->GetActorLocation(), ECC_Visibility, Params);
            if (!bBlocked || Obstruction.GetActor() == Enemy)
            {
                // No additional direct hit: each enemy receives this explosion once.
                ApplyEnemyHit(Enemy);
            }
        }
    }
    else if (IsValid(DirectTarget) && DirectTarget->IsAlive())
    {
        ApplyEnemyHit(DirectTarget);
    }
    OnRep_Visuals();
    ForceNetUpdate();
    SetLifeSpan(0.18f);
}

