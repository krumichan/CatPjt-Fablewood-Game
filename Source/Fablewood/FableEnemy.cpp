#include "FableEnemy.h"
#include "FableAdventure.h"

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

namespace
{
    /** 다른 원정의 개체가 적의 시야와 공격 판정을 가로막지 않게 한다. */
    void IgnoreOtherExpeditions(const AActor* Source, FCollisionQueryParams& Params)
    {
        UWorld* World = Source ? Source->GetWorld() : nullptr;
        if (!World)
        {
            return;
        }

        for (TActorIterator<AFableEnemy> It(World); It; ++It)
        {
            if (*It != Source && !FableAdventure::SameScope(Source, *It))
            {
                Params.AddIgnoredActor(*It);
            }
        }

        for (TActorIterator<AFableCharacter> It(World); It; ++It)
        {
            if (!FableAdventure::SameScope(Source, *It))
            {
                Params.AddIgnoredActor(*It);
            }
        }
    }
}

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableEnemy::AFableEnemy()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(30.0f);
    SetMinNetUpdateFrequency(10.0f);

    // 컴포넌트·메시·재질 설정 처리
    Collision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("EnemyCollision"));
    SetRootComponent(Collision);
    Collision->InitCapsuleSize(48, 100);
    Collision->SetCollisionProfileName(TEXT("Pawn"));
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Collision->SetCanEverAffectNavigation(false);

    // 캐릭터 부품과 관절 구성 처리
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GuardianVisuals"));
    VisualRoot->SetupAttachment(Collision);

    // Authored in centimetres at capsule-centred origin; only the limbs use local pivots.

    static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianBody.SM_GuardianBody"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BarkMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianBark.SM_GuardianBark"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> FaceMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianFace.SM_GuardianFace"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> EyesMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianEyes.SM_GuardianEyes"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CrownMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianCrown.SM_GuardianCrown"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CrownTipsMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianCrownTips.SM_GuardianCrownTips"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmLMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianArmL.SM_GuardianArmL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmRMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianArmR.SM_GuardianArmR"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmBarkLMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianArmBarkL.SM_GuardianArmBarkL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ArmBarkRMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianArmBarkR.SM_GuardianArmBarkR"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegLMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianLegL.SM_GuardianLegL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegRMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianLegR.SM_GuardianLegR"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegBarkLMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianLegBarkL.SM_GuardianLegBarkL"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> LegBarkRMesh(
        TEXT("/Game/Art/Meshes/SM_GuardianLegBarkR.SM_GuardianLegBarkR"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(
        TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    // 컴포넌트·메시·재질 설정에 필요한 값 준비
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

    // 캐릭터 부품과 관절 구성 처리
    Body = CreatePart(TEXT("OakBody"), BodyMesh.Object, VisualRoot, Paint.Object, TEXT("Bark"));
    CreatePart(TEXT("CarvedBarkPlates"), BarkMesh.Object, Body, Paint.Object, TEXT("BarkHighlight"));
    CreatePart(TEXT("RecessedWoodenMask"), FaceMesh.Object, Body, Paint.Object, TEXT("FaceGroove"));
    CreatePart(TEXT("AmberEyes"), EyesMesh.Object, Body, Paint.Object, TEXT("Eye"));
    Crown = CreatePart(TEXT("MossCrown"), CrownMesh.Object, Body, Paint.Object, TEXT("Leaf"));
    CreatePart(TEXT("MossCrownTips"), CrownTipsMesh.Object, Body, Paint.Object, TEXT("LeafTip"));

    // 캐릭터 부품과 관절 구성 순회
    for (int32 Side = 0; Side < 2; ++Side)
    {
        // 캐릭터 부품과 관절 구성에 필요한 값 준비
        const float Sign = Side == 0 ? -1.0f : 1.0f;
        UStaticMeshComponent* Arm =
            CreatePart(*FString::Printf(TEXT("BranchArm%d"), Side), Side == 0 ? ArmLMesh.Object : ArmRMesh.Object,
                       VisualRoot, Paint.Object, TEXT("Bark"));
        Arm->SetRelativeLocation(FVector(0, Sign * 48, 26));
        BranchArms.Add(Arm);
        CreatePart(*FString::Printf(TEXT("BranchArmBark%d"), Side),
                   Side == 0 ? ArmBarkLMesh.Object : ArmBarkRMesh.Object, Arm, Paint.Object, TEXT("BarkHighlight"));

        // 캐릭터 부품과 관절 구성에 필요한 값 준비
        UStaticMeshComponent* Leg =
            CreatePart(*FString::Printf(TEXT("RootLeg%d"), Side), Side == 0 ? LegLMesh.Object : LegRMesh.Object,
                       VisualRoot, Paint.Object, TEXT("Bark"));
        Leg->SetRelativeLocation(FVector(0, Sign * 24, -43));
        RootLegs.Add(Leg);
        CreatePart(*FString::Printf(TEXT("RootLegBark%d"), Side), Side == 0 ? LegBarkLMesh.Object : LegBarkRMesh.Object,
                   Leg, Paint.Object, TEXT("BarkHighlight"));
    }

    // 캐릭터 부품과 관절 구성에 필요한 값 준비
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    WaterLantern = CreatePart(TEXT("WaterLanternCore"), Sphere.Object, VisualRoot, Paint.Object, TEXT("LeafTip"));
    WaterLantern->SetRelativeScale3D(FVector(0.62f, 0.62f, 0.85f));
    WaterLantern->SetRelativeLocation(FVector(0, 0, 16));
    WaterPetals = CreatePart(TEXT("WaterLanternPetals"), CrownMesh.Object, VisualRoot, Paint.Object, TEXT("Leaf"));
    WaterPetals->SetRelativeLocation(FVector(0, 0, -14));
    WaterPetals->SetRelativeScale3D(FVector(1.25f, 1.25f, 1.1f));

    // 캐릭터 부품과 관절 구성 순회
    for (int32 Side = 0; Side < 2; ++Side)
    {
        UStaticMeshComponent* Antler =
            CreatePart(*FString::Printf(TEXT("MoonAntler%d"), Side), Side == 0 ? ArmLMesh.Object : ArmRMesh.Object,
                       VisualRoot, Paint.Object, TEXT("BarkHighlight"));
        Antler->SetRelativeLocation(FVector(0, Side == 0 ? -38.0f : 38.0f, 100));
        Antler->SetRelativeRotation(FRotator(155, 0, Side == 0 ? -30.0f : 30.0f));
        Antler->SetRelativeScale3D(FVector(1.25f));
        MoonAntlers.Add(Antler);
    }

    // 컴포넌트·메시·재질 설정 처리
    WarningRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AttackWarning"));
    WarningRoot->SetupAttachment(Collision);
    WarningRoot->SetRelativeLocation(FVector(0, 0, -106));

    constexpr int32 SegmentCount = 40;

    // 컴포넌트·메시·재질 설정 순회
    for (int32 I = 0; I < SegmentCount; ++I)
    {
        // 컴포넌트·메시·재질 설정에 필요한 값 준비
        const float Angle = 2.0f * PI * I / SegmentCount;
        UStaticMeshComponent* Segment =
            CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("WarningSegment%d"), I));
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

    // 컴포넌트·메시·재질 설정 처리
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

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableEnemy::BeginPlay()
{
    Super::BeginPlay();

    // 필드와 private 원정 모두 실제 서버 개체마다 한 번만 UUID를 부여한다.
    if (HasAuthority() && !SpawnId.IsValid())
    {
        SpawnId = FGuid::NewGuid();
    }

    // 이 단계에서 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다.
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

        // 컴포넌트·메시·재질 설정 순회
        for (UStaticMeshComponent* Mesh : Meshes)
        {
            if (Mesh->ComponentHasTag(TEXT("Bark")))
            {
                Mesh->SetMaterial(0, BodyMaterial);
            }
            else if (Mesh->ComponentHasTag(TEXT("BarkHighlight")))
            {
                Mesh->SetMaterial(0, BarkHighlightMaterial);
            }
            else if (Mesh->ComponentHasTag(TEXT("FaceGroove")))
            {
                Mesh->SetMaterial(0, FaceMaterial);
            }
            else if (Mesh->ComponentHasTag(TEXT("Eye")))
            {
                Mesh->SetMaterial(0, EyeMaterial);
            }
            else if (Mesh->ComponentHasTag(TEXT("Leaf")))
            {
                Mesh->SetMaterial(0, CrownMaterial);
            }
            else if (Mesh->ComponentHasTag(TEXT("LeafTip")))
            {
                Mesh->SetMaterial(0, CrownTipMaterial);
            }
        }
    }
    // The combat warning retains its own material and dimensions.

    // 컴포넌트·메시·재질 설정 처리
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

/** 적의 지역·난이도·보스 여부와 전투 수치를 설정한다. */
void AFableEnemy::ConfigureEncounter(bool bAncientWarden, bool bInDungeon, EFableDifficulty Difficulty)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || State == EFableEnemyState::Defeated ||
        (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard))
    {
        return;
    }

    // 지역·적·전투 진행 처리
    bBoss = bAncientWarden;
    bDungeonGuardian = bInDungeon;
    EncounterRegion = bInDungeon ? EFableRegion::RootRuins : EFableRegion::Village;
    EncounterDifficulty = bInDungeon ? Difficulty : EFableDifficulty::Normal;
    bRanged = false;
    TelegraphLocations.Empty();
    // Always derive from base HP, never multiply already-scaled health.
    MaxHealth = (bBoss ? 600.0f : (bDungeonGuardian ? 110.0f : 90.0f)) *
                (EncounterDifficulty == EFableDifficulty::Hard ? 1.45f : 1.0f);
    Health = MaxHealth;
    LastVisualHealth = Health;
    AttackCount = 0;
    SlowMultiplier = 1.0f;
    SlowEndServerTime = 0.0f;
    ClearBurn();
    OnRep_CombatVisuals();
    ForceNetUpdate();
}

/** 달샘 전투에 필요한 적의 추가 설정을 적용한다. */
void AFableEnemy::ConfigureMoonwell(bool bMoonWarden, bool bWaterSpirit, EFableDifficulty Difficulty)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive() ||
        (Difficulty != EFableDifficulty::Normal && Difficulty != EFableDifficulty::Hard))
    {
        return;
    }

    // 지역·적·전투 진행 처리
    ConfigureEncounter(bMoonWarden, true, Difficulty);
    EncounterRegion = EFableRegion::Moonwell;
    bRanged = !bMoonWarden && bWaterSpirit;
    MaxHealth =
        (bBoss ? 720.0f : (bRanged ? 100.0f : 120.0f)) * (EncounterDifficulty == EFableDifficulty::Hard ? 1.45f : 1.0f);
    Health = MaxHealth;
    LastVisualHealth = Health;
    OnRep_CombatVisuals();
    ForceNetUpdate();
}

/** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
void AFableEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    // 복제 속성과 소유자별 수신 조건 처리
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableEnemy, ExpeditionId);
    DOREPLIFETIME(AFableEnemy, PublicAreaKey);
    DOREPLIFETIME(AFableEnemy, SpawnId);
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

/** 적을 생성한 원정을 최초 한 번 고정한다. */
void AFableEnemy::BindExpedition(FGuid InExpeditionId)
{
    if (!HasAuthority() || bExpeditionBound || !InExpeditionId.IsValid())
    {
        return;
    }

    ExpeditionId = InExpeditionId;
    bExpeditionBound = true;
    if (!SpawnId.IsValid())
    {
        SpawnId = FGuid::NewGuid();
    }
    ForceNetUpdate();
}

/** 원정과 공개 필드는 같은 적을 재사용하지 않고 생성 소유권을 고정한다. */
void AFableEnemy::BindPublicField(FFablePublicAreaKey InPublicArea)
{
    if (!HasAuthority() || bExpeditionBound || !InPublicArea.IsValid())
    {
        return;
    }

    PublicAreaKey = InPublicArea;
    bExpeditionBound = true;
    if (!SpawnId.IsValid())
    {
        SpawnId = FGuid::NewGuid();
    }
    ForceNetUpdate();
}

/** 원정 복제 전이나 이미 사라진 원정은 어떤 지역으로도 대체하지 않는다. */
bool AFableEnemy::IsInsideEncounter(const FVector& Location) const
{
    const auto Adventure = FableAdventure::For(this);
    return Adventure && Adventure.IsInRegion(EncounterRegion, Location);
}

/** 공격 가능한 플레이어 중 적의 추적 대상을 선택한다. */
void AFableEnemy::FindTarget()
{
    // 지역·적·전투 진행에 필요한 값 준비
    AFableCharacter* Closest = nullptr;
    float ClosestDistanceSquared = FMath::Square(bBoss ? 2300.0f : 1700.0f);

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        if (!It->IsAlive() || !FableAdventure::CanAffect(this, *It) || !IsInsideEncounter(It->GetActorLocation()))
        {
            continue;
        }
        const float DistanceSquared = FVector::DistSquared2D(GetActorLocation(), It->GetActorLocation());
        if (DistanceSquared < ClosestDistanceSquared &&
            FMath::Abs(It->GetActorLocation().Z - GetActorLocation().Z) < 400)
        {
            Closest = *It;
            ClosestDistanceSquared = DistanceSquared;
        }
    }

    Target = Closest;
}

/** 적의 현재 이동 속도 값을 반환한다. */
float AFableEnemy::GetMoveSpeed() const
{
    const float BaseSpeed = bBoss ? (IsEnraged() ? 190.0f : 145.0f) : (bDungeonGuardian ? 155.0f : 135.0f);
    return (bRanged ? 135.0f : BaseSpeed) * GetSlowMultiplier();
}

/** 복제된 시간 기준으로 보정한 서버 시각 값을 반환한다. */
float AFableEnemy::GetSynchronizedServerTime() const
{
    if (!GetWorld())
    {
        return 0.0f;
    }
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

/** 둔화 효과의 남은 시간 값을 반환한다. */
float AFableEnemy::GetSlowTimeRemaining() const
{
    return IsAlive() ? FMath::Max(0.0f, SlowEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

/** 둔화에 적용할 속도 배율 값을 반환한다. */
float AFableEnemy::GetSlowMultiplier() const
{
    return GetSlowTimeRemaining() > 0.0f ? SlowMultiplier : 1.0f;
}

/** 효과 조건을 확인하고 적의 둔화 배율과 만료 시각을 갱신한다. */
bool AFableEnemy::ApplySlow(float Multiplier, float Duration, AController* InstigatorController)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive() || !GetWorld() || !FMath::IsFinite(Multiplier) || !FMath::IsFinite(Duration) ||
        Multiplier <= 0.0f || Multiplier >= 1.0f || Duration <= 0.0f)
    {
        return false;
    }

    // 둔화도 최종 적용점에서 원정 소유권을 검사한다.
    if (!FableAdventure::For(this) ||
        (InstigatorController ? !FableAdventure::CanAffect(InstigatorController, this) : ExpeditionId.IsValid()))
    {
        return false;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const float EffectiveMultiplier = FMath::Clamp(Multiplier, bBoss ? 0.8f : 0.4f, 1.0f);
    const float EffectiveDuration = FMath::Min(Duration, bBoss ? 2.0f : 5.0f);
    const float CurrentMultiplier = GetSlowMultiplier();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (CurrentMultiplier < EffectiveMultiplier - KINDA_SMALL_NUMBER)
    {
        return false;
    }

    // 서버 시각과 재사용 대기시간에 필요한 값 준비
    const float EndTime = GetSynchronizedServerTime() + EffectiveDuration;
    // Weaker slows never extend a stronger slow. Equal strength can refresh it;
    // a stronger replacement uses its own duration rather than inheriting one.
    SlowEndServerTime =
        FMath::IsNearlyEqual(CurrentMultiplier, EffectiveMultiplier) ? FMath::Max(SlowEndServerTime, EndTime) : EndTime;
    SlowMultiplier = EffectiveMultiplier;
    ForceNetUpdate();

    return true;
}

/** 화상 효과의 남은 시간 값을 반환한다. */
float AFableEnemy::GetBurnTimeRemaining() const
{
    return IsAlive() ? FMath::Max(0.0f, BurnEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

/** 화상의 누적 상태와 적용 대상을 해제한다. */
void AFableEnemy::ClearBurn()
{
    BurnDamagePerTick = NextBurnServerTime = BurnEndServerTime = 0.0f;
    BurnTicksRemaining = 0;
    BurnInstigator.Reset();
    BurnSourcePawn.Reset();
    BurnExpeditionId.Invalidate();
    BurnPublicAreaKey = FFablePublicAreaKey();
}

/** 유효한 화상 피해의 출처·지속 시간·주기를 설정한다. */
bool AFableEnemy::ApplyBurn(float DamagePerTick, int32 TickCount, AController* InstigatorController)
{
    AFableCharacter* Source =
        IsValid(InstigatorController) ? Cast<AFableCharacter>(InstigatorController->GetPawn()) : nullptr;

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !IsValid(Source) || !Source->IsAlive() ||
        !FableAdventure::CanAffect(Source, this) || !IsInsideEncounter(Source->GetActorLocation()) ||
        !FMath::IsFinite(DamagePerTick) || DamagePerTick <= 0.0f || DamagePerTick > 10000.0f || TickCount < 1 ||
        TickCount > 3)
    {
        return false;
    }
    // One burn per enemy. A weaker refresh cannot replace a stronger payload or its owner.
    if (BurnTicksRemaining > 0 && BurnDamagePerTick > DamagePerTick + KINDA_SMALL_NUMBER)
    {
        return false;
    }

    // 체력·마나·보호막·피해량에 필요한 값 준비
    const float Now = GetWorld()->GetTimeSeconds();
    const bool bEqualRefresh = BurnTicksRemaining > 0 && FMath::IsNearlyEqual(BurnDamagePerTick, DamagePerTick);
    NextBurnServerTime = bEqualRefresh ? FMath::Min(NextBurnServerTime, Now + 1.0f) : Now + 1.0f;
    BurnDamagePerTick = DamagePerTick;
    BurnTicksRemaining = TickCount;
    BurnEndServerTime = NextBurnServerTime + TickCount - 1.0f;
    BurnInstigator = InstigatorController;
    BurnSourcePawn = Source;
    BurnExpeditionId = ExpeditionId;
    BurnPublicAreaKey = PublicAreaKey;
    ForceNetUpdate();

    return true;
}

/** 서버 시간에 따라 화상 피해를 적용하고 만료 상태를 정리한다. */
void AFableEnemy::TickBurn()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || BurnTicksRemaining <= 0)
    {
        return;
    }

    AFableCharacter* Source = BurnSourcePawn.Get();
    AController* InstigatorController = BurnInstigator.Get();

    // 지역·적·전투 진행 조건 검사
    if (!IsAlive() || !IsValid(Source) || !Source->IsAlive() || !IsValid(InstigatorController) ||
        InstigatorController->GetPawn() != Source || BurnExpeditionId != ExpeditionId ||
        BurnPublicAreaKey != PublicAreaKey || !FableAdventure::CanAffect(Source, this) ||
        !IsInsideEncounter(Source->GetActorLocation()))
    {
        ClearBurn();
        ForceNetUpdate();
        return;
    }
    // Bounded catch-up preserves at most the three scheduled ticks during a frame hitch.

    bool bBurnChanged = false;

    // 직업·스킬·특성 빌드 순회
    for (int32 TickIndex = 0;
         TickIndex < 3 && BurnTicksRemaining > 0 && IsAlive() && GetWorld()->GetTimeSeconds() >= NextBurnServerTime;
         ++TickIndex)
    {
        // 체력·마나·보호막·피해량 처리
        bBurnChanged = true;
        const float Damage = BurnDamagePerTick;
        --BurnTicksRemaining;
        NextBurnServerTime += 1.0f;
        const float PreviousHealth = Health;
        ReceiveHit(Damage, InstigatorController);
        const float ActualDamage = PreviousHealth - Health;

        // 직업·스킬·특성 빌드 처리
        if (ActualDamage > 0.0f)
        {
            ++BurnTicksApplied;
            TotalBurnDamage += ActualDamage;
            Source->RecordTalentBurnDamage(ActualDamage);
        }
        // ReceiveHit commits defeat before reward dispatch; DoT never calls trial-hit credit.
    }

    // 이 단계에서 서버 시간에 따라 화상 피해를 적용하고 만료 상태를 정리한다.
    if (BurnTicksRemaining <= 0 || !IsAlive())
    {
        ClearBurn();
    }

    // 이 단계에서 서버 시간에 따라 화상 피해를 적용하고 만료 상태를 정리한다.
    if (bBurnChanged)
    {
        ForceNetUpdate();
    }
}

/** 공격 후 회복에 필요한 시간 값을 반환한다. */
float AFableEnemy::GetRecoveryTime() const
{
    if (EncounterRegion == EFableRegion::Moonwell && bBoss)
    {
        return IsEnraged() ? 0.42f : 1.15f;
    }
    return bRanged ? 1.4f : (bBoss ? (IsEnraged() ? 0.75f : 1.25f) : 1.0f);
}

/** 적의 공격 패턴을 정하고 공격 예고를 시작한다. */
void AFableEnemy::BeginAttack()
{
    TelegraphLocations.Empty();

    // 지역·적·전투 진행 처리
    if (EncounterRegion == EFableRegion::Moonwell && bBoss)
    {
        AttackPattern = (AttackCount % 2 == 0) ? EFableEnemyAttack::MoonRing : EFableEnemyAttack::Moonfall;
        WindupDuration = AttackPattern == EFableEnemyAttack::MoonRing ? 1.65f : 1.35f;
        if (AttackPattern == EFableEnemyAttack::Moonfall)
        {
            for (TActorIterator<AFableCharacter> It(GetWorld()); It && TelegraphLocations.Num() < 2; ++It)
            {
                if (It->IsAlive() && FableAdventure::CanAffect(this, *It) && IsInsideEncounter(It->GetActorLocation()))
                {
                    TelegraphLocations.Add(FVector(It->GetActorLocation().X, It->GetActorLocation().Y, 4));
                }
            }
        }
    }
    else if (bRanged)
    {
        AttackPattern = EFableEnemyAttack::WaterBolt;
        WindupDuration = 0.95f;
    }
    else
    {
        AttackPattern = bBoss && (AttackCount % 2 == 1) ? EFableEnemyAttack::RootLine : EFableEnemyAttack::Stomp;
        WindupDuration = bBoss ? (AttackPattern == EFableEnemyAttack::RootLine ? (IsEnraged() ? 1.15f : 1.50f)
                                                                               : (IsEnraged() ? 0.95f : 1.25f))
                               : 0.90f;
    }

    ++AttackCount;

    // 위치·방향·거리 계산 처리
    if (AFableCharacter* Victim = Target.Get())
    {
        FVector Direction = Victim->GetActorLocation() - GetActorLocation();
        Direction.Z = 0;
        if (!Direction.IsNearlyZero())
        {
            SetActorRotation(Direction.Rotation());
        }
    }

    SetState(EFableEnemyState::Windup);
}

/** 현재 온라인 상태 값을 설정한다. */
void AFableEnemy::SetState(EFableEnemyState NewState)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (State == NewState)
    {
        return;
    }

    // 지역·적·전투 진행 처리
    State = NewState;
    StateTime = 0;
    WindupEndServerTime = NewState == EFableEnemyState::Windup ? GetWorld()->GetTimeSeconds() + WindupDuration : 0.0f;
    OnRep_CombatVisuals();
    ForceNetUpdate();
}

/** 적 공격의 예고 영역을 화면 표시용 기하로 갱신한다. */
void AFableEnemy::UpdateWarningGeometry()
{
    // 지역·적·전투 진행에 필요한 값 준비
    const float HalfHeight = bBoss ? 180.0f : 100.0f;
    Collision->SetCapsuleSize(bBoss ? 88.0f : 48.0f, HalfHeight, false);
    WarningRoot->SetRelativeLocation(FVector(0, 0, -HalfHeight - 6.0f));
    const int32 Count = WarningSegments.Num();

    // 지역·적·전투 진행 순회
    for (int32 I = 0; I < Count; ++I)
    {
        // 위치·방향·거리 계산에 필요한 값 준비
        FVector Position;
        float Yaw = 0;
        float Length = 35.0f;

        // 지역·적·전투 진행 처리
        if (AttackPattern == EFableEnemyAttack::MoonRing)
        {
            const int32 Half = Count / 2;
            const float Radius = I < Half ? MoonRingInnerRadius : MoonRingOuterRadius;
            const float Angle = 2.0f * PI * (I % Half) / Half;
            Position = FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0);
            Yaw = FMath::RadiansToDegrees(Angle) + 90;
            Length = 2.0f * PI * Radius / Half;
        }
        else if (AttackPattern == EFableEnemyAttack::Moonfall)
        {
            // 위치·방향·거리 계산에 필요한 값 준비
            const int32 Circles = FMath::Clamp(TelegraphLocations.Num(), 1, 2);
            const int32 PerCircle = Count / Circles;
            const int32 Circle = FMath::Min(I / PerCircle, Circles - 1);
            const FVector Center = TelegraphLocations.IsValidIndex(Circle)
                                       ? GetActorTransform().InverseTransformPosition(TelegraphLocations[Circle])
                                       : FVector::ZeroVector;
            const float Angle = 2.0f * PI * (I % PerCircle) / PerCircle;
            Position = FVector(Center.X + FMath::Cos(Angle) * MoonfallRadius,
                               Center.Y + FMath::Sin(Angle) * MoonfallRadius, 0);
            Yaw = FMath::RadiansToDegrees(Angle) + 90;
            Length = 2.0f * PI * MoonfallRadius / PerCircle;
        }
        else if (bRanged && AttackPattern == EFableEnemyAttack::WaterBolt)
        {
            Position = FVector(60.0f + I * 21.0f, 0, 0);
            Length = 15;
        }
        else if (bBoss && AttackPattern == EFableEnemyAttack::RootLine)
        {
            const float Width = RootLineHalfWidth * 2.0f;
            const float Perimeter = 2.0f * (RootLineLength + Width);
            float Along = (I + 0.5f) * Perimeter / Count;
            Length = Perimeter / Count + 1;

            // 위치·방향·거리 계산 처리
            if (Along < RootLineLength)
            {
                Position = FVector(Along, -RootLineHalfWidth, 0);
            }
            else if ((Along -= RootLineLength) < Width)
            {
                Position = FVector(RootLineLength, Along - RootLineHalfWidth, 0);
                Yaw = 90;
            }
            else if ((Along -= Width) < RootLineLength)
            {
                Position = FVector(RootLineLength - Along, RootLineHalfWidth, 0);
            }
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

        // 지역·적·전투 진행 처리
        WarningSegments[I]->SetRelativeLocation(Position);
        WarningSegments[I]->SetRelativeRotation(FRotator(0, Yaw, 0));
        WarningSegments[I]->SetRelativeScale3D(FVector(Length / 100.0f, bBoss ? 0.13f : 0.08f, 0.025f));
    }
}

/** 복제된 전투 외형 변경을 화면과 관련 상태에 반영한다. */
void AFableEnemy::OnRep_CombatVisuals()
{
    // 서버 시각과 재사용 대기시간 처리
    if (State != LastVisualState)
    {
        LastVisualState = State;
        VisualStateTime = 0;
    }

    // 체력·마나·보호막·피해량 처리
    if (Health < LastVisualHealth)
    {
        HitFlashRemaining = 0.16f;
    }

    // 생존 상태와 부활 진행 처리
    LastVisualHealth = Health;
    UpdateWarningGeometry();
    WarningRoot->SetVisibility(State == EFableEnemyState::Windup, true);
    Collision->SetCollisionEnabled(State == EFableEnemyState::Defeated ? ECollisionEnabled::NoCollision
                                                                       : ECollisionEnabled::QueryAndPhysics);

    // 생존 상태와 부활 진행 처리
    if (CrownMaterial)
    {
        CrownMaterial->SetVectorParameterValue(TEXT("Color"), State == EFableEnemyState::Defeated
                                                                  ? FLinearColor(0.22f, 0.17f, 0.07f)
                                                                  : (IsEnraged() ? FLinearColor(0.45f, 0.16f, 0.04f)
                                                                                 : FLinearColor(0.14f, 0.25f, 0.055f)));
    }

    // 생존 상태와 부활 진행 처리
    if (CrownTipMaterial)
    {
        CrownTipMaterial->SetVectorParameterValue(
            TEXT("Color"), State == EFableEnemyState::Defeated ? FLinearColor(0.40f, 0.27f, 0.10f)
                                                               : (IsEnraged() ? FLinearColor(0.85f, 0.35f, 0.055f)
                                                                              : FLinearColor(0.30f, 0.43f, 0.095f)));
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bMoon = EncounterRegion == EFableRegion::Moonwell;
    Body->SetVisibility(!bRanged, true);

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UStaticMeshComponent* Arm : BranchArms)
    {
        Arm->SetVisibility(!bRanged, true);
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UStaticMeshComponent* Leg : RootLegs)
    {
        Leg->SetVisibility(!bRanged, true);
    }

    WaterLantern->SetVisibility(bRanged);
    WaterPetals->SetVisibility(bRanged);

    // 지역·적·전투 진행 순회
    for (UStaticMeshComponent* Antler : MoonAntlers)
    {
        Antler->SetVisibility(bMoon && bBoss);
    }

    // 이 단계에서 복제된 전투 외형 변경을 화면과 관련 상태에 반영한다.
    if (bMoon && CrownMaterial)
    {
        CrownMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.16f, 0.30f, 0.48f));
    }

    // 이 단계에서 복제된 전투 외형 변경을 화면과 관련 상태에 반영한다.
    if (bMoon && CrownTipMaterial)
    {
        CrownTipMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.35f, 0.78f, 0.85f));
    }

    VisualRoot->SetRelativeScale3D(FVector(bBoss ? 1.8f : 1.0f));
}

/** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
void AFableEnemy::Tick(float DeltaSeconds)
{
    // 서버 시각과 재사용 대기시간 처리
    Super::Tick(DeltaSeconds);
    TickPresentation(DeltaSeconds);

    // 서버 권한과 실행 위치 처리
    if (HasAuthority())
    {
        FlushHitFeedback();
    }

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive())
    {
        return;
    }

    TickBurn();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!IsAlive())
    {
        return;
    }

    // 서버 시각과 재사용 대기시간 처리
    if (SlowMultiplier < 1.0f && GetSlowTimeRemaining() <= 0.0f)
    {
        SlowMultiplier = 1.0f;
        SlowEndServerTime = 0.0f;
        ForceNetUpdate();
    }

    StateTime += DeltaSeconds;

    // 지역·적·전투 진행 처리
    if (State == EFableEnemyState::Windup)
    {
        if (StateTime >= WindupDuration)
        {
            ApplyAreaAttack();
            SetState(EFableEnemyState::Recovering);
        }
        return;
    }

    // 지역·적·전투 진행 처리
    if (State == EFableEnemyState::Recovering)
    {
        if (StateTime >= GetRecoveryTime())
        {
            SetState(EFableEnemyState::Idle);
        }
        return;
    }

    TargetSearchRemaining -= DeltaSeconds;

    // 이 단계에서 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다.
    if (TargetSearchRemaining <= 0)
    {
        FindTarget();
        TargetSearchRemaining = 0.35f;
    }

    AFableCharacter* Victim = Target.Get();

    // 지역·적·전투 진행 조건 검사
    if (!Victim || !Victim->IsAlive() || !FableAdventure::CanAffect(this, Victim) ||
        !IsInsideEncounter(Victim->GetActorLocation()))
    {
        SetState(EFableEnemyState::Idle);
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    FVector ToVictim = Victim->GetActorLocation() - GetActorLocation();
    ToVictim.Z = 0;
    const float Distance = ToVictim.Size();
    const bool bNextLine = bBoss && (AttackCount % 2 == 1);
    const float TriggerRange =
        bRanged
            ? 1050.0f
            : ((bBoss && EncounterRegion == EFableRegion::Moonwell)
                   ? 1100.0f
                   : (bBoss ? (bNextLine ? RootLineLength - 80.0f : BossStompRadius - 55.0f) : AttackRadius - 40.0f));
    FCollisionQueryParams SightParams(SCENE_QUERY_STAT(FableEnemySight), false, this);
    IgnoreOtherExpeditions(this, SightParams);
    FHitResult SightHit;
    const bool bVisible = !bRanged ||
                          !GetWorld()->LineTraceSingleByChannel(
                              SightHit, GetActorLocation(), Victim->GetActorLocation(), ECC_Visibility, SightParams) ||
                          SightHit.GetActor() == Victim;

    // 위치·방향·거리 계산 조건 검사
    if (Distance <= TriggerRange && bVisible)
    {
        BeginAttack();
        return;
    }

    // 지역·적·전투 진행 처리
    SetState(EFableEnemyState::Chasing);
    const FVector Direction = ToVictim.GetSafeNormal();
    SetActorRotation(FMath::RInterpTo(GetActorRotation(), Direction.Rotation(), DeltaSeconds, 5.0f));
    FHitResult Hit;
    const FVector MoveDelta =
        Direction * FMath::Min(GetMoveSpeed() * DeltaSeconds, FMath::Max(0.0f, Distance - 150.0f));
    AddActorWorldOffset(MoveDelta, true, &Hit);

    // 현재 월드와 플레이어 참조 처리
    if (Hit.bBlockingHit)
    {
        const FVector Slide = FVector::VectorPlaneProject(MoveDelta * (1.0f - Hit.Time), Hit.Normal);
        AddActorWorldOffset(Slide, true);
    }
}

/** 적의 복제된 전투 상태를 자세·색상·예고 표시로 표현한다. */
void AFableEnemy::TickPresentation(float DeltaSeconds)
{
    // 지역·적·전투 진행 처리
    VisualStateTime += DeltaSeconds;
    HitFlashRemaining = FMath::Max(0.0f, HitFlashRemaining - DeltaSeconds);
    FLinearColor BarkBase =
        bBoss ? (IsEnraged() ? FLinearColor(0.38f, 0.095f, 0.035f) : FLinearColor(0.22f, 0.24f, 0.14f))
              : FLinearColor(0.27f, 0.14f, 0.055f);
    FLinearColor HighlightBase = bBoss ? FLinearColor(0.51f, 0.42f, 0.20f) : FLinearColor(0.45f, 0.27f, 0.115f);

    // 지역·적·전투 진행 처리
    if (EncounterRegion == EFableRegion::Moonwell)
    {
        BarkBase = bBoss ? FLinearColor(0.19f, 0.24f, 0.40f) : FLinearColor(0.23f, 0.31f, 0.38f);
        HighlightBase = FLinearColor(0.51f, 0.68f, 0.72f);
    }

    // 서버 시각과 재사용 대기시간 처리
    if (GetSlowTimeRemaining() > 0.0f)
    {
        BarkBase = FMath::Lerp(BarkBase, FLinearColor(0.15f, 0.43f, 0.64f), 0.68f);
        HighlightBase = FLinearColor(0.42f, 0.78f, 0.88f);
    }

    DamageTextRemaining = FMath::Max(0.0f, DamageTextRemaining - DeltaSeconds);

    // 지역·적·전투 진행 처리
    if (DamageTextRemaining > 0.0f)
    {
        DamageText->SetRelativeLocation(
            FVector(0, 0, (bBoss ? 226.0f : 136.0f) + (0.72f - DamageTextRemaining) * 55.0f));
        if (APlayerController* Viewer = GetWorld()->GetFirstPlayerController())
        {
            FVector ViewLocation;
            FRotator ViewRotation;
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

    // 이 단계에서 적의 복제된 전투 상태를 자세·색상·예고 표시로 표현한다.
    if (BodyMaterial)
    {
        BodyMaterial->SetVectorParameterValue(TEXT("Color"),
                                              HitFlashRemaining > 0.0f ? FLinearColor(0.78f, 0.65f, 0.34f) : BarkBase);
    }

    // 이 단계에서 적의 복제된 전투 상태를 자세·색상·예고 표시로 표현한다.
    if (BarkHighlightMaterial)
    {
        BarkHighlightMaterial->SetVectorParameterValue(
            TEXT("Color"), HitFlashRemaining > 0.0f ? FLinearColor(0.95f, 0.82f, 0.49f) : HighlightBase);
    }

    // 캐릭터 부품과 관절 구성 조건 검사
    if (State == EFableEnemyState::Defeated)
    {
        const float Scale = FMath::Max(0.08f, 1.0f - VisualStateTime * 0.65f);
        VisualRoot->SetRelativeScale3D(FVector(Scale * (bBoss ? 1.8f : 1.0f)));
        VisualRoot->SetRelativeLocation(FVector(0, 0, -VisualStateTime * 24.0f));
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    const float ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();

    // 지역·적·전투 진행 처리
    if (WarningMaterial && State == EFableEnemyState::Windup)
    {
        const float Pulse = 0.78f + 0.22f * FMath::Sin(ServerNow * (IsEnraged() ? 18.0f : 11.0f));
        WarningMaterial->SetVectorParameterValue(
            TEXT("Color"),
            (IsEnraged() ? FLinearColor(1.0f, 0.08f, 0.025f) : FLinearColor(1.0f, 0.35f, 0.055f)) * Pulse);
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bWalking = State == EFableEnemyState::Chasing;
    const float Step = ServerNow * 5.5f;
    const float Bob = bWalking ? FMath::Abs(FMath::Sin(Step)) * 3.0f : FMath::Sin(ServerNow * 1.4f) * 0.6f;
    float Windup = 0.0f;
    float ArmLift = 0.0f;
    float BodyPitch = bWalking ? 3.0f : 0.0f;
    float BodyDip = 0.0f;

    // 지역·적·전투 진행 처리
    if (State == EFableEnemyState::Windup)
    {
        const float Progress =
            FMath::Clamp(1.0f - (WindupEndServerTime - ServerNow) / FMath::Max(0.1f, WindupDuration), 0.0f, 1.0f);
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

    // 캐릭터 부품과 관절 구성 처리
    VisualRoot->SetRelativeScale3D(FVector(bBoss ? 1.8f : 1.0f));
    VisualRoot->SetRelativeLocation(
        FVector(0, 0, Bob + BodyDip + (bRanged ? 18.0f + 9.0f * FMath::Sin(ServerNow * 2.1f) : 0.0f)));
    Body->SetRelativeScale3D(FVector(1.0f + Windup * 0.04f, 1.0f + Windup * 0.04f, 1.0f - Windup * 0.05f));
    Body->SetRelativeRotation(FRotator(BodyPitch, 0, bWalking ? FMath::Sin(Step) * 1.5f : 0.0f));

    // 위치·방향·거리 계산 순회
    for (int32 Side = 0; Side < BranchArms.Num(); ++Side)
    {
        const float Sign = Side == 0 ? -1.0f : 1.0f;
        const float Stride = bWalking ? FMath::Sin(Step + Side * PI) : 0.0f;
        BranchArms[Side]->SetRelativeRotation(FRotator(ArmLift - Stride * 11.0f, Sign * Windup * 5.0f,
                                                       Sign * (Windup * 8.0f + FMath::Abs(Stride) * 2.0f)));
    }

    // 위치·방향·거리 계산 순회
    for (int32 Side = 0; Side < RootLegs.Num(); ++Side)
    {
        const float Sign = Side == 0 ? -1.0f : 1.0f;
        const float Stride = bWalking ? FMath::Sin(Step + Side * PI) : 0.0f;
        RootLegs[Side]->SetRelativeLocation(FVector(0, Sign * 24, -43 + FMath::Max(0.0f, Stride) * 5.0f));
        RootLegs[Side]->SetRelativeRotation(FRotator(Stride * 10.0f, 0, Sign * Windup * 3.0f));
    }
}

/** 공격 예고 영역 안에 있는 유효한 대상에게 피해를 적용한다. */
void AFableEnemy::ApplyAreaAttack()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive())
    {
        return;
    }

    // 지역·적·전투 진행 처리
    if (AttackPattern == EFableEnemyAttack::WaterBolt)
    {
        int32 Shots = 0;
        for (TActorIterator<AFableEnemyBolt> It(GetWorld()); It; ++It)
        {
            Shots += It->GetExpeditionId() == ExpeditionId && It->GetPublicAreaKey() == PublicAreaKey ? 1 : 0;
        }
        if (Shots < 12)
        {
            FActorSpawnParameters Params;
            Params.Owner = this;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (AFableEnemyBolt* Shot = GetWorld()->SpawnActor<AFableEnemyBolt>(
                    GetActorLocation() + GetActorForwardVector() * 65, GetActorRotation(), Params))
            {
                // The shot stores final damage; do not scale it again on impact.
                Shot->InitializeBolt(GetActorForwardVector(), 18.0f * GetDamageMultiplier(), EncounterRegion);
            }
        }
        return;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const bool bLine = bBoss && AttackPattern == EFableEnemyAttack::RootLine;
    const bool bRing = AttackPattern == EFableEnemyAttack::MoonRing;
    const bool bFall = AttackPattern == EFableEnemyAttack::Moonfall;
    const float Radius = bBoss ? BossStompRadius : AttackRadius;
    const float BaseDamage = (bRing || bFall)
                                 ? (IsEnraged() ? 28.0f : 24.0f)
                                 : (bBoss ? (bLine ? (IsEnraged() ? 28.0f : 22.0f) : (IsEnraged() ? 32.0f : 26.0f))
                                          : (bDungeonGuardian ? 20.0f : 18.0f));
    const float Damage = BaseDamage * GetDamageMultiplier();

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        // 지역·적·전투 진행 처리
        if (!It->IsAlive() || !FableAdventure::CanAffect(this, *It) || !IsInsideEncounter(It->GetActorLocation()))
        {
            continue;
        }

        // 위치·방향·거리 계산에 필요한 값 준비
        const FVector Difference = It->GetActorLocation() - GetActorLocation();
        const FVector Local = GetActorTransform().InverseTransformVectorNoScale(Difference);
        bool bInShape = false;

        // 위치·방향·거리 계산 처리
        if (bRing)
        {
            bInShape = Difference.SizeSquared2D() > FMath::Square(MoonRingInnerRadius) &&
                       Difference.SizeSquared2D() <= FMath::Square(MoonRingOuterRadius);
        }
        else if (bFall)
        {
            for (const FVector& Mark : TelegraphLocations)
            {
                bInShape |= FVector::DistSquared2D(It->GetActorLocation(), Mark) <= FMath::Square(MoonfallRadius);
            }
        }
        else
        {
            bInShape = bLine ? (Local.X >= 0 && Local.X <= RootLineLength && FMath::Abs(Local.Y) <= RootLineHalfWidth)
                             : Difference.SizeSquared2D() <= FMath::Square(Radius);
        }

        // 지역·적·전투 진행 처리
        if (!bInShape || FMath::Abs(Difference.Z) >= (bBoss ? 260.0f : 150.0f))
        {
            continue;
        }

        // 지역·적·전투 진행에 필요한 값 준비
        FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(FableEnemyAttack), false, this);
        IgnoreOtherExpeditions(this, TraceParams);
        FHitResult Obstruction;
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Obstruction, GetActorLocation(),
                                                                   It->GetActorLocation(), ECC_Visibility, TraceParams);

        // 체력·마나·보호막·피해량 처리
        if (!bBlocked || Obstruction.GetActor() == *It)
        {
            It->ReceiveHit(Damage, this);
        }
    }
}

/** 누적된 적중 피드백을 묶어 클라이언트에 전달한다. */
void AFableEnemy::FlushHitFeedback(bool bForce)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || PendingFeedbackDamage <= 0.0f)
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();

    // 서버 시각과 재사용 대기시간 조건 검사
    if (!bForce && Now < NextHitFeedbackTime)
    {
        return;
    }

    // 체력·마나·보호막·피해량 처리
    MulticastHitFeedback(PendingFeedbackDamage);
    PendingFeedbackDamage = 0.0f;
    NextHitFeedbackTime = Now + 0.12f;
}

/** 적중 피드백를 수신한 클라이언트에서 재생한다. */
void AFableEnemy::MulticastHitFeedback_Implementation(float ActualDamage)
{
    // 서버 권한과 실행 위치 조건 검사
    if (GetNetMode() == NM_DedicatedServer || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f)
    {
        return;
    }

    // 지역·적·전투 진행 처리
    HitFlashRemaining = 0.16f;
    DisplayedDamage = (DamageTextRemaining > 0.38f ? DisplayedDamage : 0.0f) + ActualDamage;
    DamageTextRemaining = 0.72f;
    DamageText->SetRelativeLocation(FVector(0, 0, bBoss ? 226.0f : 136.0f));
    DamageText->SetText(FText::AsNumber(FMath::Max(1, FMath::RoundToInt(DisplayedDamage))));
    DamageText->SetWorldSize(bBoss ? 39.0f : 31.0f);
    DamageText->SetVisibility(true);
    FableCombatFeedback::PlayCue(this, GetActorLocation(), 0);
}

/** 권한과 생존 상태를 확인하고 보호막·방어력을 반영해 피해를 적용한다. */
void AFableEnemy::ReceiveHit(float Damage, AController* InstigatorController)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive() || !FMath::IsFinite(Damage) || Damage <= 0)
    {
        return;
    }

    // 원거리·광역·화상 모두 같은 최종 판정을 거치고 출처 없는 private 피해는 거절한다.
    if (!FableAdventure::For(this) ||
        (InstigatorController ? !FableAdventure::CanAffect(InstigatorController, this) : ExpeditionId.IsValid()))
    {
        return;
    }

    // 체력·마나·보호막·피해량에 필요한 값 준비
    const bool bWasEnraged = IsEnraged();
    const float PreviousHealth = Health;
    Health = FMath::Max(0.0f, Health - Damage);

    // 체력·마나·보호막·피해량 처리
    if (Health < PreviousHealth && IsValid(InstigatorController))
    {
        // All melee, area and projectile damage meets here; the profile owns the slot cooldown.
        if (AFablePlayerState* Profile = InstigatorController->GetPlayerState<AFablePlayerState>())
        {
            Profile->WearEquipment(EFableEquipmentSlot::Weapon, 1);
        }
    }

    // 체력·마나·보호막·피해량 처리
    PendingFeedbackDamage += PreviousHealth - Health;
    FlushHitFeedback(Health <= 0.0f);
    OnRep_CombatVisuals();

    // 현재 월드와 플레이어 참조 처리
    if (InstigatorController)
    {
        if (AFableCharacter* Attacker = Cast<AFableCharacter>(InstigatorController->GetPawn()))
        {
            Target = Attacker;
            TargetSearchRemaining = 0.35f;
        }
    }

    AFableGameMode* GameMode = Cast<AFableGameMode>(GetWorld()->GetAuthGameMode());

    // 지역·적·전투 진행 처리
    if (bBoss && GameMode)
    {
        GameMode->UpdateBossHealth(this);
    }

    // 지역·적·전투 진행 처리
    if (bBoss && !bWasEnraged && IsEnraged())
    {
        UE_LOG(LogTemp, Display, TEXT("FABLEWOOD_BOSS_ENRAGED health=%.0f"), Health);
    }

    // 생존 상태와 부활 진행 처리
    if (Health <= 0)
    {
        // Commit defeat before the callback: concurrent hits cannot reward a kill twice.
        // 생존 상태와 부활 진행 처리
        SetState(EFableEnemyState::Defeated);
        SlowMultiplier = 1.0f;
        SlowEndServerTime = 0.0f;
        ClearBurn();
        Target.Reset();

        // 생존 상태와 부활 진행 처리
        if (GameMode)
        {
            GameMode->HandleEnemyDefeated(this);
        }

        SetLifeSpan(2.0f);
    }

    ForceNetUpdate();
}
