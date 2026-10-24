#include "FableProjectile.h"

#include "FableCharacter.h"
#include "FableAdventure.h"
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

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableProjectile::AFableProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(60.0f);
    SetMinNetUpdateFrequency(30.0f);
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShotRoot"));
    SetRootComponent(SceneRoot);

    // 컴포넌트·메시·재질 설정에 필요한 값 준비
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(TEXT("/Engine/BasicShapes/Cone.Cone"));
    static ConstructorHelpers::FObjectFinder<UMaterial> PaintAsset(
        TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
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

    // 컴포넌트·메시·재질 설정 순회
    for (UStaticMeshComponent* Component : {Orb.Get(), ArrowShaft.Get(), ArrowHead.Get()})
    {
        Component->SetMaterial(0, PaintAsset.Object);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        Component->SetCastShadow(false);
    }
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority())
    {
        SetLifeSpan(4.5f);
    }
    OnRep_Visuals();
}

/** 플레이어 투사체의 소유자·방향·피해 정보를 초기화한다. */
void AFableProjectile::InitializeShot(const FVector& Direction, float Damage, AController* DamageInstigator,
                                      bool bUseArrow, float ExplosionRadius, float Speed, float MaximumRange,
                                      int32 PierceCount)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || bInitialized || Direction.ContainsNaN() || Direction.IsNearlyZero() ||
        !FMath::IsFinite(Damage) || Damage <= 0.0f || !FMath::IsFinite(ExplosionRadius) || !FMath::IsFinite(Speed) ||
        !FMath::IsFinite(MaximumRange))
    {
        return;
    }

    // 발사 후 시전자가 다른 원정에 들어가도 이 식별자는 변경하지 않는다.
    const auto SourceAdventure = FableAdventure::For(DamageInstigator);
    if (!SourceAdventure)
    {
        return;
    }

    ExpeditionId = SourceAdventure.ExpeditionId();
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

/** 전직 시험 시전 문맥 값을 설정한다. */
void AFableProjectile::SetTrialCastContext(const FFableTrialCastContext& Context)
{
    if (HasAuthority() && !bImpacted)
    {
        TrialContext = Context;
    }
}

/** 화상 적용 정보 값을 설정한다. */
void AFableProjectile::SetBurnPayload(float DamagePerTick, int32 TickCount)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bInitialized || bImpacted || bBurnPayloadSet || bArrow || !bExplosive ||
        !FMath::IsFinite(DamagePerTick) || DamagePerTick <= 0.0f || TickCount < 1 || TickCount > 3)
    {
        return;
    }

    // 체력·마나·보호막·피해량 처리
    bBurnPayloadSet = true;
    BurnDamagePerTick = FMath::Min(DamagePerTick, 10000.0f);
    BurnTickCount = TickCount;
}

/** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
void AFableProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableProjectile, ExpeditionId);
    DOREPLIFETIME(AFableProjectile, bArrow);
    DOREPLIFETIME(AFableProjectile, bExplosive);
    DOREPLIFETIME(AFableProjectile, bImpacted);
    DOREPLIFETIME(AFableProjectile, bPiercing);
    DOREPLIFETIME(AFableProjectile, UniqueEnemyHits);
}

/** 복제된 외형 변경을 화면과 관련 상태에 반영한다. */
void AFableProjectile::OnRep_Visuals()
{
    // 서버 권한과 실행 위치 조건 검사
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    // 위치·방향·거리 계산 처리
    Orb->SetVisibility(!bArrow);
    ArrowShaft->SetVisibility(bArrow && !bImpacted);
    ArrowHead->SetVisibility(bArrow && !bImpacted);
    Orb->SetRelativeScale3D(FVector(bImpacted ? (bExplosive ? 1.1f : 0.36f) : (bExplosive ? 0.30f : 0.20f)));

    // 컴포넌트·메시·재질 설정 처리
    if (UMaterialInstanceDynamic* Material = Orb->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), bExplosive ? FLinearColor(0.58f, 0.17f, 0.06f)
                                                                    : FLinearColor(0.06f, 0.34f, 0.68f));
        Material->SetScalarParameterValue(TEXT("Roughness"), 0.40f);
    }

    // 컴포넌트·메시·재질 설정 처리
    if (UMaterialInstanceDynamic* Material = ArrowShaft->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.24f, 0.12f, 0.035f));
    }

    // 컴포넌트·메시·재질 설정 처리
    if (UMaterialInstanceDynamic* Material = ArrowHead->CreateAndSetMaterialInstanceDynamic(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"), bPiercing ? FLinearColor(0.86f, 0.66f, 0.20f)
                                                                   : FLinearColor(0.60f, 0.68f, 0.70f));
    }
}

/** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
void AFableProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bInitialized || bImpacted || !GetWorld())
    {
        return;
    }
    if (!IsValid(GetOwner()) || !SourceController.IsValid() ||
        !FableAdventure::CanAffect(this, SourceController.Get()) || !FMath::IsFinite(DeltaSeconds) ||
        DeltaSeconds <= 0.0f)
    {
        Destroy();
        return;
    }

    // 이동 입력과 속도 제한에 필요한 값 준비
    const float StepDistance = FMath::Min(RangeRemaining, static_cast<float>(FlightVelocity.Size()) * DeltaSeconds);
    const FVector Direction = FlightVelocity.GetSafeNormal();
    FVector Start = GetActorLocation();
    const FVector End = Start + Direction * StepDistance;
    const FCollisionShape Shape = FCollisionShape::MakeSphere(bArrow ? 3.0f : 9.0f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableProjectileSweep), false, this);
    // Co-op allies and previously hit targets cannot absorb or repeat a shot.

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        Params.AddIgnoredActor(*It);
    }

    // 다른 원정의 적은 피해뿐 아니라 충돌 흡수와 관통 소모에서도 제외한다.
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        if (!FableAdventure::CanAffect(this, *It))
        {
            Params.AddIgnoredActor(*It);
        }
    }

    // 지역·적·전투 진행 순회
    for (const TWeakObjectPtr<AFableEnemy>& Enemy : HitEnemies)
    {
        if (Enemy.IsValid())
        {
            Params.AddIgnoredActor(Enemy.Get());
        }
    }

    // A single frame can cross more than one capsule. Continue sweeping the
    // remaining segment after each pierce; never skip the wall query between them.

    // 지역·적·전투 진행 순회
    for (int32 SweepIndex = 0; SweepIndex < 3; ++SweepIndex)
    {
        // 지역·적·전투 진행에 필요한 값 준비
        FHitResult ClosestHit;
        bool bHit =
            GetWorld()->SweepSingleByChannel(ClosestHit, Start, End, FQuat::Identity, ECC_Visibility, Shape, Params);
        float ClosestTime = bHit ? ClosestHit.Time : 2.0f;
        AFableEnemy* DirectTarget = bHit ? Cast<AFableEnemy>(ClosestHit.GetActor()) : nullptr;

        // 현재 월드와 플레이어 참조에 필요한 값 준비
        TArray<FHitResult> PawnHits;
        FCollisionObjectQueryParams PawnObjects;
        PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
        GetWorld()->SweepMultiByObjectType(PawnHits, Start, End, FQuat::Identity, PawnObjects, Shape, Params);

        // 지역·적·전투 진행 순회
        for (const FHitResult& Hit : PawnHits)
        {
            AFableEnemy* Enemy = Cast<AFableEnemy>(Hit.GetActor());
            if (IsValid(Enemy) && Enemy->IsAlive() && FableAdventure::CanAffect(this, Enemy) &&
                !HitEnemies.Contains(TWeakObjectPtr<AFableEnemy>(Enemy)) && Hit.Time < ClosestTime)
            {
                ClosestHit = Hit;
                ClosestTime = Hit.Time;
                DirectTarget = Enemy;
                bHit = true;
            }
        }

        // 위치·방향·거리 계산 처리
        if (!bHit)
        {
            SetActorLocation(End);
            break;
        }

        SetActorLocation(ClosestHit.Location);

        // 지역·적·전투 진행 처리
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

    // 이 단계에서 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다.
    if (RangeRemaining <= KINDA_SMALL_NUMBER)
    {
        if (bExplosive)
        {
            ResolveImpact(End, nullptr);
        }
        else
        {
            Destroy();
        }
    }
}

/** 중복 적중을 막고 유효한 적에게 투사체 효과를 적용한다. */
void AFableProjectile::ApplyEnemyHit(AFableEnemy* Enemy)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsValid(Enemy) || !Enemy->IsAlive() || !SourceController.IsValid() ||
        !FableAdventure::CanAffect(this, Enemy) || !FableAdventure::CanAffect(this, SourceController.Get()) ||
        HitEnemies.Contains(TWeakObjectPtr<AFableEnemy>(Enemy)))
    {
        return;
    }

    // 지역·적·전투 진행 처리
    HitEnemies.Add(TWeakObjectPtr<AFableEnemy>(Enemy));
    UniqueEnemyHits = static_cast<uint8>(FMath::Min(HitEnemies.Num(), 255));
    const float PreviousHealth = Enemy->GetHealth();
    Enemy->ReceiveHit(ShotDamage, SourceController.Get());

    // 직업·스킬·특성 빌드 처리
    if (Enemy->GetHealth() < PreviousHealth && SourceController.IsValid())
    {
        if (AFablePlayerState* Profile = SourceController->GetPlayerState<AFablePlayerState>())
        {
            Profile->RecordTrialSkillHit(TrialContext);
        }
        // No profile/talent lookup here: resetting or switching cannot rewrite an in-flight shot.
        if (Enemy->IsAlive() && BurnDamagePerTick > 0.0f && SourceController->GetPawn() == GetOwner())
        {
            Enemy->ApplyBurn(BurnDamagePerTick, BurnTickCount, SourceController.Get());
        }
    }

    ForceNetUpdate();
}

/** 투사체 충돌의 결과와 표시 상태를 확정한다. */
void AFableProjectile::ResolveImpact(const FVector& ImpactLocation, AFableEnemy* DirectTarget)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || bImpacted || !GetWorld())
    {
        return;
    }

    // 이동 입력과 속도 제한 처리
    bImpacted = true; // Set before applying damage; the projectile can resolve only once.
    FlightVelocity = FVector::ZeroVector;

    // 지역·적·전투 진행 처리
    if (bExplosive)
    {
        FCollisionQueryParams Params(SCENE_QUERY_STAT(FableProjectileBlast), false, this);
        for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
        {
            Params.AddIgnoredActor(*It);
        }
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            if (!FableAdventure::CanAffect(this, *It))
            {
                Params.AddIgnoredActor(*It);
            }
        }
        for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
        {
            AFableEnemy* Enemy = *It;

            // 지역·적·전투 진행 처리
            if (!IsValid(Enemy) || !Enemy->IsAlive() || !FableAdventure::CanAffect(this, Enemy) ||
                FVector::DistSquared(ImpactLocation, Enemy->GetActorLocation()) > FMath::Square(BlastRadius))
            {
                continue;
            }

            // 지역·적·전투 진행에 필요한 값 준비
            FHitResult Obstruction;
            const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
                Obstruction, ImpactLocation, Enemy->GetActorLocation(), ECC_Visibility, Params);

            // 지역·적·전투 진행 처리
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
