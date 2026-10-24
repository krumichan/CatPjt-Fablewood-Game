#include "FableEnemyBolt.h"
#include "FableCharacter.h"
#include "FableAdventure.h"
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

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableEnemyBolt::AFableEnemyBolt()
{
    // 컴포넌트·메시·재질 설정 처리
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30);
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("WaterShotRoot"));
    SetRootComponent(SceneRoot);
    Droplet = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterDroplet"));
    Droplet->SetupAttachment(SceneRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(
        TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    Droplet->SetStaticMesh(Sphere.Object);
    Droplet->SetMaterial(0, Paint.Object);
    Droplet->SetRelativeScale3D(FVector(0.52f, 0.30f, 0.30f));
    Droplet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Droplet->SetCastShadow(false);
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableEnemyBolt::BeginPlay()
{
    Super::BeginPlay();

    // 컴포넌트·메시·재질 설정 처리
    if (UMaterialInstanceDynamic* Paint = Droplet->CreateAndSetMaterialInstanceDynamic(0))
    {
        Paint->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.25f, 0.86f, 0.95f));
        Paint->SetScalarParameterValue(TEXT("Roughness"), 0.2f);
    }

    // 서버 권한과 실행 위치 처리
    if (HasAuthority())
    {
        SetLifeSpan(3.5f);
    }
}

/** 적 투사체의 발사 위치·방향·피해 정보를 초기화한다. */
void AFableEnemyBolt::InitializeBolt(FVector Direction, float Damage, EFableRegion Region)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || bInitialized || Direction.ContainsNaN() || Direction.IsNearlyZero() ||
        !FMath::IsFinite(Damage) || Damage <= 0 || Region != EFableRegion::Moonwell)
    {
        return;
    }

    // 신뢰한 발사 적의 원정을 발사 순간 고정한다.
    const auto SourceAdventure = FableAdventure::For(GetOwner());
    if (!SourceAdventure || SourceAdventure.IsIdleTown() ||
        (!SourceAdventure.ExpeditionId().IsValid() && !SourceAdventure.PublicArea().IsValid()))
    {
        return;
    }

    ExpeditionId = SourceAdventure.ExpeditionId();
    PublicAreaKey = SourceAdventure.PublicArea();

    // 이동 입력과 속도 제한 처리
    Direction.Z = 0;
    Velocity = Direction.GetSafeNormal() * 680.0f;
    DamageAmount = FMath::Clamp(Damage, 1.0f, 40.0f);
    EncounterRegion = Region;
    bInitialized = true;
    SetActorRotation(Direction.Rotation());
    ForceNetUpdate();
}

/** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
void AFableEnemyBolt::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableEnemyBolt, ExpeditionId);
    DOREPLIFETIME(AFableEnemyBolt, PublicAreaKey);
    DOREPLIFETIME(AFableEnemyBolt, EncounterRegion);
    DOREPLIFETIME(AFableEnemyBolt, bImpacted);
}

/** 복제된 충돌 변경을 화면과 관련 상태에 반영한다. */
void AFableEnemyBolt::OnRep_Impact()
{
    Droplet->SetRelativeScale3D(
        FVector(bImpacted ? 0.65f : 0.52f, bImpacted ? 0.65f : 0.30f, bImpacted ? 0.65f : 0.30f));
}

/** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
void AFableEnemyBolt::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bInitialized || bImpacted)
    {
        return;
    }

    // 종료된 원정과 출처가 달라진 발사체는 다른 진행에 도달하기 전에 제거한다.
    const auto Adventure = FableAdventure::For(this);
    if (!Adventure || !IsValid(GetOwner()) || !FableAdventure::CanAffect(this, GetOwner()))
    {
        Destroy();
        return;
    }

    // 이동 입력과 속도 제한에 필요한 값 준비
    const FVector Start = GetActorLocation();
    const FVector Delta =
        Velocity.GetSafeNormal() * FMath::Min(static_cast<float>(Velocity.Size()) * DeltaSeconds, RemainingRange);
    const FVector End = Start + Delta;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableEnemyWaterShot), false, this);

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        Params.AddIgnoredActor(*It);
    }

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->IsAlive() || !FableAdventure::CanAffect(this, *It) ||
            !Adventure.IsInRegion(EncounterRegion, It->GetActorLocation()))
        {
            Params.AddIgnoredActor(*It);
        }
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const FCollisionShape Shape = FCollisionShape::MakeSphere(17);
    FHitResult Closest;
    bool bHit = GetWorld()->SweepSingleByChannel(Closest, Start, End, FQuat::Identity, ECC_Visibility, Shape, Params);
    AFableCharacter* Target = bHit ? Cast<AFableCharacter>(Closest.GetActor()) : nullptr;
    FCollisionObjectQueryParams PawnObjects;
    PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
    TArray<FHitResult> PawnHits;
    GetWorld()->SweepMultiByObjectType(PawnHits, Start, End, FQuat::Identity, PawnObjects, Shape, Params);

    // 지역·적·전투 진행 순회
    for (const FHitResult& Hit : PawnHits)
    {
        AFableCharacter* Hero = Cast<AFableCharacter>(Hit.GetActor());
        if (Hero && Hero->IsAlive() && FableAdventure::CanAffect(this, Hero) &&
            Adventure.IsInRegion(EncounterRegion, Hero->GetActorLocation()) && (!bHit || Hit.Time < Closest.Time))
        {
            Closest = Hit;
            Target = Hero;
            bHit = true;
        }
    }

    // 지역·적·전투 진행 처리
    if (bHit)
    {
        SetActorLocation(Closest.Location);
        bImpacted = true;
        if (Target && Target->IsAlive() && FableAdventure::CanAffect(this, Target) &&
            Adventure.IsInRegion(EncounterRegion, Target->GetActorLocation()))
        {
            Target->ReceiveHit(DamageAmount, this);
        }
        OnRep_Impact();
        ForceNetUpdate();
        SetLifeSpan(0.12f);
        return;
    }

    // 위치·방향·거리 계산 처리
    SetActorLocation(End);
    RemainingRange -= Delta.Size();

    // 지역·적·전투 진행 처리
    if (RemainingRange <= 0 || !Adventure.IsInRegion(EncounterRegion, End))
    {
        Destroy();
    }
}
