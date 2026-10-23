#include "FableEnemyBolt.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AFableEnemyBolt::AFableEnemyBolt()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30);
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WaterShotRoot"));
    SetRootComponent(SceneRoot);
    Droplet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterDroplet"));
    Droplet->SetupAttachment(SceneRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    Droplet->SetStaticMesh(Sphere.Object);
    Droplet->SetMaterial(0,Paint.Object);
    Droplet->SetRelativeScale3D(FVector(0.52f,0.30f,0.30f));
    Droplet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Droplet->SetCastShadow(false);
}
void AFableEnemyBolt::BeginPlay()
{
    Super::BeginPlay();
    if (UMaterialInstanceDynamic* Paint = Droplet->CreateAndSetMaterialInstanceDynamic(0))
    {
        Paint->SetVectorParameterValue(TEXT("Color"),FLinearColor(0.25f,0.86f,0.95f));
        Paint->SetScalarParameterValue(TEXT("Roughness"),0.2f);
    }
    if (HasAuthority()) SetLifeSpan(3.5f);
}
void AFableEnemyBolt::InitializeBolt(FVector Direction, float Damage, EFableRegion Region)
{
    if (!HasAuthority() || bInitialized || Direction.ContainsNaN() || Direction.IsNearlyZero()
        || !FMath::IsFinite(Damage) || Damage<=0 || Region!=EFableRegion::Moonwell) return;
    Direction.Z=0;
    Velocity=Direction.GetSafeNormal()*680.0f;
    DamageAmount=FMath::Clamp(Damage,1.0f,40.0f);
    EncounterRegion=Region;
    bInitialized=true;
    SetActorRotation(Direction.Rotation());
    ForceNetUpdate();
}
void AFableEnemyBolt::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableEnemyBolt,EncounterRegion);
    DOREPLIFETIME(AFableEnemyBolt,bImpacted);
}
void AFableEnemyBolt::OnRep_Impact()
{
    Droplet->SetRelativeScale3D(FVector(bImpacted?0.65f:0.52f, bImpacted?0.65f:0.30f, bImpacted?0.65f:0.30f));
}
void AFableEnemyBolt::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!HasAuthority() || !bInitialized || bImpacted) return;
    const FVector Start=GetActorLocation();
    const FVector Delta=Velocity.GetSafeNormal()*FMath::Min(static_cast<float>(Velocity.Size())*DeltaSeconds,RemainingRange);
    const FVector End=Start+Delta;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableEnemyWaterShot),false,this);
    for (TActorIterator<AFableEnemy> It(GetWorld());It;++It) Params.AddIgnoredActor(*It);
    for (TActorIterator<AFableCharacter> It(GetWorld());It;++It)
        if (!It->IsAlive() || !FableRPG::IsInRegion(EncounterRegion,It->GetActorLocation())) Params.AddIgnoredActor(*It);
    const FCollisionShape Shape=FCollisionShape::MakeSphere(17);
    FHitResult Closest;
    bool bHit=GetWorld()->SweepSingleByChannel(Closest,Start,End,FQuat::Identity,ECC_Visibility,Shape,Params);
    AFableCharacter* Target=bHit?Cast<AFableCharacter>(Closest.GetActor()):nullptr;
    FCollisionObjectQueryParams PawnObjects;
    PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
    TArray<FHitResult> PawnHits;
    GetWorld()->SweepMultiByObjectType(PawnHits,Start,End,FQuat::Identity,PawnObjects,Shape,Params);
    for (const FHitResult& Hit:PawnHits)
    {
        AFableCharacter* Hero=Cast<AFableCharacter>(Hit.GetActor());
        if (Hero && Hero->IsAlive() && FableRPG::IsInRegion(EncounterRegion,Hero->GetActorLocation())
            && (!bHit || Hit.Time<Closest.Time))
        { Closest=Hit; Target=Hero; bHit=true; }
    }
    if (bHit)
    {
        SetActorLocation(Closest.Location);
        bImpacted=true;
        if (Target && Target->IsAlive() && FableRPG::IsInRegion(EncounterRegion,Target->GetActorLocation()))
            Target->ReceiveHit(DamageAmount);
        OnRep_Impact(); ForceNetUpdate(); SetLifeSpan(0.12f); return;
    }
    SetActorLocation(End);
    RemainingRange-=Delta.Size();
    if (RemainingRange<=0 || !FableRPG::IsInRegion(EncounterRegion,End)) Destroy();
}
