#include "FableCharacter.h"

#include "FableEnemy.h"
#include "FableAdventure.h"
#include "FablePartyManager.h"
#include "FableCombatFeedback.h"
#include "FablePlayerState.h"
#include "FableProjectile.h"
#include "FableTownCatalog.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace FableCharacterTuning
{
    constexpr float WalkSpeed = 420.0f;
    constexpr float AttackCooldown = 0.52f;
    constexpr float SkillCooldown = 5.5f;
    constexpr float DodgeCooldown = 1.35f;
    constexpr float DodgeDuration = 0.32f;
    constexpr float DodgeSpeed = 780.0f;
    constexpr float MeleeRange = 230.0f;
    constexpr float SkillRange = 440.0f;
    constexpr float DownedDuration = 25.0f;
    constexpr float ReviveDuration = 3.0f;
    constexpr float ReviveDistance = 230.0f;
    constexpr float ReviveHeight = 150.0f;
    constexpr float ReviveProtectionDuration = 2.0f;
    const FLinearColor TunicColor(0.135f, 0.066f, 0.029f);
    const FLinearColor BladeColor(0.71f, 0.81f, 0.79f);
}

namespace
{
    /** 다른 원정의 전투 개체가 공격·부활 시야를 가로막지 않도록 제외한다. */
    void IgnoreOtherExpeditions(const AActor* Source, FCollisionQueryParams& Params)
    {
        UWorld* World = Source ? Source->GetWorld() : nullptr;
        if (!World)
        {
            return;
        }

        for (TActorIterator<AFableEnemy> It(World); It; ++It)
        {
            if (!FableAdventure::SameScope(Source, *It))
            {
                Params.AddIgnoredActor(*It);
            }
        }

        for (TActorIterator<AFableCharacter> It(World); It; ++It)
        {
            if (*It != Source && !FableAdventure::SameScope(Source, *It))
            {
                Params.AddIgnoredActor(*It);
            }
        }
    }
}

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFableCharacter::AFableCharacter()
{
    // 직업·스킬·특성 빌드 처리
    PrimaryActorTick.bCanEverTick = true;
    SkillCastCounts.Init(0, 3);
    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(60.0f);
    SetMinNetUpdateFrequency(20.0f);
    GetCapsuleComponent()->InitCapsuleSize(34.0f, 92.0f);
    GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
    // Nearby allies must not push another player's camera into their own cape.
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
    GetCharacterMovement()->MaxWalkSpeed = FableCharacterTuning::WalkSpeed;
    GetCharacterMovement()->JumpZVelocity = 460.0f;
    GetCharacterMovement()->AirControl = 0.3f;
    GetCharacterMovement()->BrakingDecelerationWalking = 1900.0f;
    GetCharacterMovement()->MaxStepHeight = 45.0f;

    // 추적 카메라 설정 처리
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetRootComponent());
    CameraBoom->TargetArmLength = 460.0f;
    CameraBoom->SocketOffset = FVector(0.0f, 58.0f, 70.0f);
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->bEnableCameraLag = true;
    CameraBoom->CameraLagSpeed = 12.0f;
    // Preserve smooth walking without dragging the camera across regions after party travel.
    CameraBoom->CameraLagMaxDistance = 120.0f;
    CameraBoom->bDoCollisionTest = true;
    CameraBoom->ProbeChannel = ECC_Camera;
    CameraBoom->ProbeSize = 18.0f;
    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->bUsePawnControlRotation = false;
    FollowCamera->FieldOfView = 78.0f;

    // 캐릭터 부품과 관절 구성 처리
    VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
    VisualRoot->SetupAttachment(GetRootComponent());
    LeftArm = CreateDefaultSubobject<USceneComponent>(TEXT("LeftArm"));
    LeftArm->SetupAttachment(VisualRoot);
    LeftArm->SetRelativeLocation(FVector(0.0f, -23.0f, 42.0f));
    RightArm = CreateDefaultSubobject<USceneComponent>(TEXT("RightArm"));
    RightArm->SetupAttachment(VisualRoot);
    RightArm->SetRelativeLocation(FVector(0.0f, 23.0f, 42.0f));
    LeftForearm = CreateDefaultSubobject<USceneComponent>(TEXT("LeftForearmPivot"));
    LeftForearm->SetupAttachment(LeftArm);
    LeftForearm->SetRelativeLocation(FVector(1.0f, 0.0f, -28.0f));
    RightForearm = CreateDefaultSubobject<USceneComponent>(TEXT("RightForearmPivot"));
    RightForearm->SetupAttachment(RightArm);
    RightForearm->SetRelativeLocation(FVector(1.0f, 0.0f, -28.0f));
    LeftLeg = CreateDefaultSubobject<USceneComponent>(TEXT("LeftLeg"));
    LeftLeg->SetupAttachment(VisualRoot);
    LeftLeg->SetRelativeLocation(FVector(-1.0f, -10.0f, -8.0f));
    RightLeg = CreateDefaultSubobject<USceneComponent>(TEXT("RightLeg"));
    RightLeg->SetupAttachment(VisualRoot);
    RightLeg->SetRelativeLocation(FVector(-1.0f, 10.0f, -8.0f));
    LeftKnee = CreateDefaultSubobject<USceneComponent>(TEXT("LeftKneePivot"));
    LeftKnee->SetupAttachment(LeftLeg);
    LeftKnee->SetRelativeLocation(FVector(1.0f, 0.0f, -39.0f));
    RightKnee = CreateDefaultSubobject<USceneComponent>(TEXT("RightKneePivot"));
    RightKnee->SetupAttachment(RightLeg);
    RightKnee->SetRelativeLocation(FVector(1.0f, 0.0f, -39.0f));

    static ConstructorHelpers::FObjectFinder<UMaterial> PaintMaterial(
        TEXT("/Game/Art/Materials/M_FablePaint.M_FablePaint"));

    // 이 단계에서 생성 시 필요한 기본 설정과 초기 상태를 구성한다.
    if (!PaintMaterial.Succeeded())
    {
        UE_LOG(LogTemp, Error,
               TEXT("캐릭터 재질이 없습니다: /Game/Art/Materials/M_FablePaint. 아트 가져오기 도구를 실행하세요."));
    }
    // These meshes are authored at real size around their animation pivot.
    // No engine primitives, axis conversion or compensating component scale is used.

    // 컴포넌트·메시·재질 설정에 필요한 값 준비
    const auto Part = [this](const TCHAR* Name, USceneComponent* Parent, const TCHAR* Asset, const FLinearColor& Color,
                             float Roughness = 0.83f,
                             const FVector& Location = FVector::ZeroVector) -> UStaticMeshComponent*
    {
        // 컴포넌트·메시·재질 설정에 필요한 값 준비
        const FString MeshPath = FString::Printf(TEXT("/Game/Art/Meshes/SM_Hero%s.SM_Hero%s"), Asset, Asset);
        ConstructorHelpers::FObjectFinder<UStaticMesh> MeshAsset(*MeshPath);
        UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(Parent);
        Component->SetStaticMesh(MeshAsset.Object);

        // 이 단계에서 생성 시 필요한 기본 설정과 초기 상태를 구성한다.
        if (!MeshAsset.Succeeded())
        {
            UE_LOG(LogTemp, Error,
                   TEXT("캐릭터 부품이 없습니다: %s. Tools/Art/create_character_meshes.py 실행 후 OBJ를 가져오세요."),
                   *MeshPath);
        }

        // 컴포넌트·메시·재질 설정 처리
        Component->SetMaterial(0, PaintMaterial.Object);
        Component->SetRelativeLocation(Location);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        VisualParts.Add(Component);
        PartColors.Add(Color);
        PartRoughness.Add(Roughness);

        return Component;
    };

    const FLinearColor Skin(0.55f, 0.29f, 0.15f);
    const FLinearColor Hair(0.060f, 0.026f, 0.013f);
    const FLinearColor Leather(0.070f, 0.036f, 0.020f);
    const FLinearColor TrouserCloth(0.095f, 0.080f, 0.058f);
    const FLinearColor Ivory(0.73f, 0.65f, 0.46f);
    const FLinearColor Teal(0.026f, 0.20f, 0.19f);
    const FLinearColor Seams(0.31f, 0.18f, 0.075f);
    const FLinearColor Brass(0.57f, 0.34f, 0.09f);
    Part(TEXT("IvoryShirt"), VisualRoot, TEXT("Shirt"), Ivory);
    Torso = Part(TEXT("LeatherVest"), VisualRoot, TEXT("Vest"), FableCharacterTuning::TunicColor, 0.74f);
    Part(TEXT("VestSeams"), VisualRoot, TEXT("VestSeams"), Seams);
    Part(TEXT("VestLacing"), VisualRoot, TEXT("Laces"), Ivory * 0.55f);
    Part(TEXT("TravelBelt"), VisualRoot, TEXT("Belt"), Leather, 0.73f);
    Part(TEXT("BrassFittings"), VisualRoot, TEXT("Fittings"), Brass, 0.42f);
    Part(TEXT("Hips"), VisualRoot, TEXT("Hips"), TrouserCloth);
    Part(TEXT("WaistCloth"), VisualRoot, TEXT("WaistCloth"), Teal);
    Part(TEXT("TravelPouch"), VisualRoot, TEXT("Pouch"), FLinearColor(0.14f, 0.072f, 0.028f), 0.78f);
    Part(TEXT("HeadAndNeck"), VisualRoot, TEXT("Head"), Skin, 0.73f);
    Part(TEXT("EyeWhites"), VisualRoot, TEXT("EyeWhites"), FLinearColor(0.68f, 0.61f, 0.46f), 0.45f);
    Part(TEXT("Eyes"), VisualRoot, TEXT("Eyes"), FLinearColor(0.018f, 0.027f, 0.022f), 0.34f);
    Part(TEXT("Lips"), VisualRoot, TEXT("Lips"), FLinearColor(0.29f, 0.105f, 0.072f), 0.76f);
    Part(TEXT("SweptHair"), VisualRoot, TEXT("Hair"), Hair);
    Cape = Part(TEXT("DrapedCape"), VisualRoot, TEXT("Cape"), Teal, 0.91f, FVector(-10.0f, 0.0f, 48.0f));
    Part(TEXT("CapeEdging"), Cape, TEXT("CapeBorder"), FLinearColor(0.38f, 0.29f, 0.12f));
    Part(TEXT("ShoulderMantle"), VisualRoot, TEXT("Mantle"), Teal);

    // 캐릭터 부품과 관절 구성 처리
    Part(TEXT("LeftSleeve"), LeftArm, TEXT("Sleeve"), Ivory);
    Part(TEXT("RightSleeve"), RightArm, TEXT("Sleeve"), Ivory);
    Part(TEXT("LeftRolledCuff"), LeftArm, TEXT("SleeveCuff"), Ivory * 1.05f);
    Part(TEXT("RightRolledCuff"), RightArm, TEXT("SleeveCuff"), Ivory * 1.05f);
    Part(TEXT("LeftForearmSkin"), LeftForearm, TEXT("Forearm"), Skin, 0.73f);
    Part(TEXT("RightForearmSkin"), RightForearm, TEXT("Forearm"), Skin, 0.73f);
    Part(TEXT("LeftBracer"), LeftForearm, TEXT("Bracer"), Leather);
    Part(TEXT("RightBracer"), RightForearm, TEXT("Bracer"), Leather);
    Part(TEXT("LeftBracerTrim"), LeftForearm, TEXT("BracerTrim"), Seams);
    Part(TEXT("RightBracerTrim"), RightForearm, TEXT("BracerTrim"), Seams);
    Part(TEXT("LeftGlove"), LeftForearm, TEXT("Glove"), Leather);
    Part(TEXT("RightGlove"), RightForearm, TEXT("Glove"), Leather);
    Part(TEXT("LeftTrousers"), LeftLeg, TEXT("Thigh"), TrouserCloth);
    Part(TEXT("RightTrousers"), RightLeg, TEXT("Thigh"), TrouserCloth);
    Part(TEXT("LeftShin"), LeftKnee, TEXT("Shin"), TrouserCloth);
    Part(TEXT("RightShin"), RightKnee, TEXT("Shin"), TrouserCloth);
    Part(TEXT("LeftBoot"), LeftKnee, TEXT("Boot"), Leather, 0.75f);
    Part(TEXT("RightBoot"), RightKnee, TEXT("Boot"), Leather, 0.75f);
    Part(TEXT("LeftBootSole"), LeftKnee, TEXT("BootSole"), Leather * 0.45f);
    Part(TEXT("RightBootSole"), RightKnee, TEXT("BootSole"), Leather * 0.45f);
    Part(TEXT("LeftBootStraps"), LeftKnee, TEXT("BootStraps"), Seams, 0.77f);
    Part(TEXT("RightBootStraps"), RightKnee, TEXT("BootStraps"), Seams, 0.77f);
    SwordParts.Add(Part(TEXT("SwordGrip"), RightForearm, TEXT("SwordGrip"), Leather, 0.72f));
    SwordParts.Add(Part(TEXT("SwordHilt"), RightForearm, TEXT("SwordHilt"), Brass, 0.35f));
    SwordBlade = Part(TEXT("SwordBlade"), RightForearm, TEXT("SwordBlade"), FableCharacterTuning::BladeColor, 0.26f);
    SwordParts.Add(SwordBlade);
    SwordParts.Add(
        Part(TEXT("SwordFuller"), RightForearm, TEXT("SwordFuller"), FLinearColor(0.24f, 0.32f, 0.32f), 0.42f));

    // Functional class markers reuse built-in meshes while art remains out of scope.

    // 컴포넌트·메시·재질 설정에 필요한 값 준비
    static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerCylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerSphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    const auto Marker = [this](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* MarkerMesh,
                               const FVector& Location, const FVector& Scale, const FLinearColor& Color,
                               const FRotator& Rotation = FRotator::ZeroRotator) -> UStaticMeshComponent*
    {
        // 컴포넌트·메시·재질 설정에 필요한 값 준비
        UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(Parent);
        Component->SetStaticMesh(MarkerMesh);
        Component->SetMaterial(0, PaintMaterial.Object);
        Component->SetRelativeLocation(Location);
        Component->SetRelativeRotation(Rotation);
        Component->SetRelativeScale3D(Scale);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        Component->SetVisibility(false);
        VisualParts.Add(Component);
        PartColors.Add(Color);
        PartRoughness.Add(0.75f);

        return Component;
    };
    StaffParts.Add(Marker(TEXT("MageStaff"), RightForearm, MarkerCylinder.Object, FVector(4, 0, -20),
                          FVector(0.045f, 0.045f, 1.30f), Leather));
    StaffParts.Add(Marker(TEXT("MageFocus"), RightForearm, MarkerSphere.Object, FVector(4, 0, 48), FVector(0.16f),
                          FLinearColor(0.06f, 0.40f, 0.75f)));
    BowRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BowPivot"));
    BowRoot->SetupAttachment(LeftForearm);
    BowRoot->SetRelativeLocation(FVector(1, 0, -28));
    const FVector BowPoints[] = {FVector(-4, 0, 36), FVector(2, 0, 26),  FVector(6, 0, 12),  FVector(7, 0, 0),
                                 FVector(6, 0, -12), FVector(2, 0, -26), FVector(-4, 0, -36)};

    // 위치·방향·거리 계산 순회
    for (int32 Index = 0; Index < 6; ++Index)
    {
        const FVector Segment = BowPoints[Index + 1] - BowPoints[Index];
        const FString Name = FString::Printf(TEXT("BowLimb%d"), Index);
        BowParts.Add(Marker(*Name, BowRoot, MarkerCylinder.Object, (BowPoints[Index + 1] + BowPoints[Index]) * 0.5f,
                            FVector(0.028f, 0.028f, Segment.Size() / 100.0f), Seams,
                            FRotationMatrix::MakeFromZ(Segment).Rotator()));
    }

    BowParts.Add(Marker(TEXT("BowString"), BowRoot, MarkerCylinder.Object, FVector(-4, 0, 0),
                        FVector(0.006f, 0.006f, 0.72f), Ivory));

    // 직업·스킬·특성 빌드 순회
    for (int32 Index = 0; Index < 8; ++Index)
    {
        const FString Name = FString::Printf(TEXT("SkillPetal%d"), Index);
        UStaticMeshComponent* Petal = Part(*Name, GetRootComponent(), TEXT("Leaf"), FLinearColor(0.25f, 0.54f, 0.27f));
        Petal->SetVisibility(false);
        Petal->SetCastShadow(false);
        SkillPetals.Add(Petal);
    }
}

/** 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다. */
void AFableCharacter::BeginPlay()
{
    // 체력·마나·보호막·피해량 처리
    Super::BeginPlay();
    BaseMaxHealth = MaxHealth;
    FString ValidationRole;
    bLegacyRecovery = FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), ValidationRole) ||
                      FParse::Param(FCommandLine::Get(), TEXT("FableArtReview"));

    // 서버 권한과 실행 위치 처리
    if (HasAuthority())
    {
        SynchronizeStats();
        Health = MaxHealth;
        SafeSpawnLocation = GetActorLocation();
        SafeSpawnRotation = GetActorRotation();
    }

    // 서버 권한과 실행 위치 처리
    if (GetNetMode() != NM_DedicatedServer)
    {
        for (int32 Index = 0; Index < VisualParts.Num(); ++Index)
        {
            if (UMaterialInstanceDynamic* Instance = VisualParts[Index]->CreateAndSetMaterialInstanceDynamic(0))
            {
                Instance->SetVectorParameterValue(TEXT("Color"), PartColors[Index]);
                Instance->SetScalarParameterValue(TEXT("Roughness"), PartRoughness[Index]);

                // 이 단계에서 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다.
                if (VisualParts[Index] == Torso)
                {
                    TorsoMaterial = Instance;
                }

                // 이 단계에서 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다.
                if (VisualParts[Index] == SwordBlade)
                {
                    BladeMaterial = Instance;
                }

                const FName PartName = VisualParts[Index]->GetFName();

                // 이 단계에서 월드 진입 후 필요한 초기 상태와 이벤트를 준비한다.
                if (VisualParts[Index] == Cape || PartName == TEXT("ShoulderMantle") || PartName == TEXT("WaistCloth"))
                {
                    ClassClothMaterials.Add(Instance);
                }
            }
        }
    }

    // 체력·마나·보호막·피해량 처리
    ApplyClassAppearance();
    OnRep_Health();
}

/** 월드 이탈 시 등록한 이벤트와 진행 중인 작업을 정리한다. */
void AFableCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority())
    {
        CancelReviveInternal(TEXT("동료 연결 종료"));
    }
    GetWorldTimerManager().ClearTimer(RespawnTimer);
    Super::EndPlay(EndPlayReason);
}

/** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
void AFableCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    // 복제 속성과 소유자별 수신 조건 처리
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AFableCharacter, Health);
    DOREPLIFETIME(AFableCharacter, MaxHealth);
    DOREPLIFETIME(AFableCharacter, LifeState);
    DOREPLIFETIME(AFableCharacter, DownedEndServerTime);
    DOREPLIFETIME(AFableCharacter, DefeatedServerTime);
    DOREPLIFETIME(AFableCharacter, ReviveTarget);
    DOREPLIFETIME(AFableCharacter, Reviver);
    DOREPLIFETIME(AFableCharacter, ReviveStartServerTime);
    DOREPLIFETIME(AFableCharacter, ReviveProtectionEndServerTime);
    DOREPLIFETIME(AFableCharacter, DownedCount);
    DOREPLIFETIME(AFableCharacter, RevivesPerformedCount);
    DOREPLIFETIME(AFableCharacter, TimesRevivedCount);
    DOREPLIFETIME(AFableCharacter, ReviveCancelCount);
    DOREPLIFETIME(AFableCharacter, ReviveProtectionBlockedHits);
    DOREPLIFETIME_CONDITION(AFableCharacter, Mana, COND_OwnerOnly);
    DOREPLIFETIME(AFableCharacter, bDodging);
    DOREPLIFETIME(AFableCharacter, NextSecondSkillServerTime);
    DOREPLIFETIME(AFableCharacter, NextThirdSkillServerTime);
    DOREPLIFETIME(AFableCharacter, ShieldAmount);
    DOREPLIFETIME(AFableCharacter, ShieldEndServerTime);
    DOREPLIFETIME(AFableCharacter, TotalShieldAbsorbed);
    DOREPLIFETIME(AFableCharacter, CombatBuildEndServerTime);
    DOREPLIFETIME(AFableCharacter, TalentBurnDamageDealt);
    DOREPLIFETIME(AFableCharacter, TalentSlowHits);
    DOREPLIFETIME(AFableCharacter, TalentAllyShieldsGranted);
    DOREPLIFETIME(AFableCharacter, SkillCastCounts);
    DOREPLIFETIME_CONDITION(AFableCharacter, NextAttackServerTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFableCharacter, NextSkillServerTime, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFableCharacter, NextDodgeServerTime, COND_OwnerOnly);
}

/** 캐릭터의 이동·시점·전투 입력을 동작에 연결한다. */
void AFableCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    check(PlayerInputComponent);
    PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &AFableCharacter::MoveForward);
    PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &AFableCharacter::MoveRight);
    PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AFableCharacter::Turn);
    PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AFableCharacter::LookUp);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &AFableCharacter::StartJump);
    PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &AFableCharacter::FinishJump);
    PlayerInputComponent->BindAction(TEXT("Attack"), IE_Pressed, this, &AFableCharacter::RequestAttack);
    PlayerInputComponent->BindAction(TEXT("Skill"), IE_Pressed, this, &AFableCharacter::RequestSkill);
    PlayerInputComponent->BindAction(TEXT("Skill2"), IE_Pressed, this, &AFableCharacter::RequestSecondSkill);
    PlayerInputComponent->BindAction(TEXT("Skill3"), IE_Pressed, this, &AFableCharacter::RequestThirdSkill);
    PlayerInputComponent->BindAction(TEXT("Dodge"), IE_Pressed, this, &AFableCharacter::RequestDodge);
}

/** 앞뒤 이동 입력을 캐릭터의 이동 방향에 반영한다. */
void AFableCharacter::MoveForward(float Value)
{
    if (IsAlive() && Controller && FMath::IsFinite(Value))
    {
        const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), FMath::Clamp(Value, -1.0f, 1.0f));
    }
}

/** 좌우 이동 입력을 캐릭터의 이동 방향에 반영한다. */
void AFableCharacter::MoveRight(float Value)
{
    if (IsAlive() && Controller && FMath::IsFinite(Value))
    {
        const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), FMath::Clamp(Value, -1.0f, 1.0f));
    }
}

/** 수평 시점 회전 입력을 반영한다. */
void AFableCharacter::Turn(float Value)
{
    if (FMath::IsFinite(Value))
    {
        AddControllerYawInput(FMath::Clamp(Value, -20.0f, 20.0f));
    }
}

/** 수직 시점 회전 입력을 반영한다. */
void AFableCharacter::LookUp(float Value)
{
    if (FMath::IsFinite(Value))
    {
        AddControllerPitchInput(FMath::Clamp(Value, -20.0f, 20.0f));
    }
}

/** 현재 행동 조건을 확인하고 점프를 시작한다. */
void AFableCharacter::StartJump()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!IsAlive() || (Controller && Controller->IsMoveInputIgnored()))
    {
        return;
    }

    CancelRevive();

    // 이 단계에서 현재 행동 조건을 확인하고 점프를 시작한다.
    if (!bDodging)
    {
        Jump();
    }
}

/** 점프 입력의 유지 상태를 해제한다. */
void AFableCharacter::FinishJump()
{
    StopJumping();
}

/** 기본 공격 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestAttack()
{
    RequestAction(0);
}

/** 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestSkill()
{
    RequestActiveSkill(0);
}

/** 두 번째 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestSecondSkill()
{
    RequestActiveSkill(1);
}

/** 세 번째 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestThirdSkill()
{
    RequestActiveSkill(2);
}

/** 사용형 스킬 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestActiveSkill(int32 Slot)
{
    if (Slot >= 0 && Slot <= 2)
    {
        RequestAction(Slot == 0 ? 1 : uint8(Slot + 2));
    }
}

/** 회피 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestDodge()
{
    RequestAction(2);
}

/** 동작 요청의 조건을 확인하고 해당 동작을 진행한다. */
void AFableCharacter::RequestAction(uint8 Action)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!IsLocallyControlled() || !IsAlive() || !GetWorld() || (Controller && Controller->IsMoveInputIgnored()))
    {
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (ReviveTarget)
    {
        CancelRevive();
    }

    const float Now = GetWorld()->GetTimeSeconds();

    // HTTP 요청·응답과 연결 상태 조건 검사
    if (Now < NextLocalRequestTime)
    {
        return;
    }

    NextLocalRequestTime = Now + 0.06f;

    // 위치·방향·거리 계산 처리
    if (Action != 2 && Controller && FMath::IsFinite(Controller->GetControlRotation().Yaw))
    {
        // Turn the local presentation toward the camera; the server derives the same facing.
        SetActorRotation(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
    }

    ServerRequestAction(Action);
}

/** 전투 동작 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFableCharacter::ServerRequestAction_Implementation(uint8 Action)
{
    // 서버 권한과 실행 위치 조건 검사
    if (const AFablePlayerState* BackendState = GetPlayerState<AFablePlayerState>();
        BackendState && BackendState->IsBackendEnabled() && !BackendState->IsBackendReady())
    {
        return;
    }
    if (!HasAuthority() || !Controller || !IsAlive() || Action > 4 || !GetWorld())
    {
        return;
    }
    // An attack/dodge attempt ends the channel even when that action is cooling down.

    // 생존 상태와 부활 진행 처리
    CancelReviveInternal(TEXT("전투 행동"));
    const float Now = GetWorld()->GetTimeSeconds();

    // HTTP 요청·응답과 연결 상태 조건 검사
    if (!FMath::IsFinite(Now) || Now - LastRequestServerTime < 0.05f)
    {
        return;
    }

    LastRequestServerTime = Now;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bDodging)
    {
        return;
    }

    // 이동 입력과 속도 제한 처리
    if (Action == 2)
    {
        if (Now >= NextDodgeServerTime && GetCharacterMovement()->IsMovingOnGround())
        {
            PerformDodge();
        }
        return;
    }

    // 서버 시각과 재사용 대기시간 조건 검사
    if (Now < ActionLockEndServerTime)
    {
        return;
    }

    // 서버 시각과 재사용 대기시간 처리
    if (Action == 0 && Now >= NextAttackServerTime)
    {
        PerformAttack(false);
    }

    // 직업·스킬·특성 빌드 처리
    if (Action == 1 || Action == 3 || Action == 4)
    {
        PerformActiveSkill(Action == 1 ? 0 : int32(Action) - 2);
    }
}

/** 스킬 사용 조건을 검사하고 비용·대기시간·직업별 효과를 적용한다. */
void AFableCharacter::PerformActiveSkill(int32 Slot)
{
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !State || !State->bProfileLoaded || Slot < 0 || Slot > 2)
    {
        return;
    }
    if (State->IsBackendEnabled() && !State->IsBackendReady())
    {
        return;
    }

    SynchronizeStats();

    // 직업·스킬·특성 빌드 조건 검사
    if (!State->IsSkillUnlocked(Slot))
    {
        State->SetNotice(TEXT("아직 잠긴 스킬입니다. 로완에게 전직 조건을 확인하세요."));
        return;
    }
    if (GetSkillCooldownRemaining(Slot) > 0.0f)
    {
        State->SetNotice(TEXT("스킬을 다시 사용할 때까지 기다리세요."));
        return;
    }

    const float Cost = GetSkillManaCost(Slot);

    // 체력·마나·보호막·피해량 조건 검사
    if (Mana < Cost)
    {
        State->SetNotice(TEXT("마나가 부족합니다. 회복을 기다리거나 M으로 물약을 사용하세요."));
        return;
    }
    if (Slot == 0)
    {
        PerformAttack(true);
        return;
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const FFableTrialCastContext TrialContext = State->CreateTrialCastContext(Slot);
    Mana = FMath::Max(0.0f, Mana - Cost);
    const float Now = GetWorld()->GetTimeSeconds();
    CombatBuildEndServerTime = Now + 5.0f;
    (Slot == 1 ? NextSecondSkillServerTime : NextThirdSkillServerTime) = Now + GetSkillCooldownDuration(Slot);
    ActionLockEndServerTime = Now + 0.55f;

    // 직업·스킬·특성 빌드 처리
    if (SkillCastCounts.IsValidIndex(Slot))
    {
        ++SkillCastCounts[Slot];
    }

    // 위치·방향·거리 계산 처리
    if (Controller && FMath::IsFinite(Controller->GetControlRotation().Yaw))
    {
        SetActorRotation(FRotator(0, Controller->GetControlRotation().Yaw, 0));
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const EFableClass Class = GetHeroClass();
    const int32 Rank = FMath::Clamp(State->GetSkillRank(Slot), 1, 3);
    const float Power = State->GetAttackPower() * (1.0f + 0.15f * (Rank - 1));

    // 서버 권한과 실행 위치 처리
    if (Slot == 2 && Class != EFableClass::Archer)
    {
        ShieldAmount = GetSkillShieldCapacity(Slot);
        ShieldEndServerTime = Now + GetSkillShieldDuration(Slot);
        if (Class == EFableClass::Warrior)
        {
            ShareGuardianShield(GetCombatTalentRank(EFableTalentPath::Second));
        }
        MulticastCombatCue(7);
    }
    else if (Class == EFableClass::Mage)
    {
        ApplySkillArea(Power * GetSkillDamageMultiplier(Slot), GetSkillRange(Slot), false, true, TrialContext);
        MulticastCombatCue(6);
    }
    else if (Class == EFableClass::Warrior)
    {
        const FVector Direction = GetActorForwardVector().GetSafeNormal2D();
        LaunchCharacter(Direction * 950.0f + FVector(0, 0, 65), true, true);
        if (!IsLocallyControlled())
        {
            ClientConfirmSkillMove(FVector_NetQuantizeNormal(Direction), 950.0f);
        }
        ApplySkillArea(Power * GetSkillDamageMultiplier(Slot), GetSkillRange(Slot), true, false, TrialContext);
        MulticastCombatCue(6);
    }
    else
    {
        PerformRangedAttack(Class, true, Power, Slot, TrialContext);
        if (Slot == 2)
        {
            const FVector Direction = -GetActorForwardVector().GetSafeNormal2D();
            const float RetreatSpeed = 720.0f + 90.0f * GetCombatTalentRank(EFableTalentPath::Second);
            LaunchCharacter(Direction * RetreatSpeed + FVector(0, 0, 65), true, true);
            if (!IsLocallyControlled())
            {
                ClientConfirmSkillMove(FVector_NetQuantizeNormal(Direction), RetreatSpeed);
            }
        }
        MulticastCombatCue(Slot == 2 ? 8 : 6);
    }

    ForceNetUpdate();
}

/** 스킬 범위 안의 유효한 적에게 피해와 부가 효과를 적용한다. */
void AFableCharacter::ApplySkillArea(float Damage, float Range, bool bFrontal, bool bSlow,
                                     const FFableTrialCastContext& TrialContext)
{
    // 직업·스킬·특성 빌드에 필요한 값 준비
    const FVector Origin = GetActorLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableAdvancedSkill), false, this);
    IgnoreOtherExpeditions(this, Params);

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        Params.AddIgnoredActor(*It);
    }

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        // 지역·적·전투 진행에 필요한 값 준비
        AFableEnemy* Enemy = *It;
        const FVector Delta = Enemy->GetActorLocation() - Origin;

        // 지역·적·전투 진행 처리
        if (!Enemy->IsAlive() || !FableAdventure::CanAffect(this, Enemy) ||
            Delta.SizeSquared2D() > FMath::Square(Range) || FMath::Abs(Delta.Z) > 180.0f)
        {
            continue;
        }

        // 위치·방향·거리 계산 처리
        if (bFrontal && !Delta.IsNearlyZero() &&
            FVector::DotProduct(Delta.GetSafeNormal2D(), GetActorForwardVector()) < 0.4f)
        {
            continue;
        }

        FHitResult Hit;

        // 지역·적·전투 진행 처리
        if (GetWorld()->LineTraceSingleByChannel(Hit, Origin + FVector(0, 0, 24), Enemy->GetActorLocation(),
                                                 ECC_Visibility, Params) &&
            Hit.GetActor() != Enemy)
        {
            continue;
        }

        // 직업·스킬·특성 빌드에 필요한 값 준비
        const float PreviousHealth = Enemy->GetHealth();
        Enemy->ReceiveHit(Damage, Controller);
        const int32 FrostRank = GetHeroClass() == EFableClass::Mage ? GetCombatTalentRank(EFableTalentPath::Second) : 0;
        const bool bAppliedSlow = bSlow && Enemy->IsAlive() &&
                                  Enemy->ApplySlow(0.65f - 0.05f * FrostRank, 4.0f + 0.5f * FrostRank, Controller);

        // 직업·스킬·특성 빌드 처리
        if (bAppliedSlow && FrostRank > 0 && Enemy->GetHealth() < PreviousHealth)
        {
            ++TalentSlowHits;
        }

        // 직업·스킬·특성 빌드 처리
        if (Enemy->GetHealth() < PreviousHealth)
        {
            if (AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>())
            {
                Profile->RecordTrialSkillHit(TrialContext, bAppliedSlow);
            }
        }
    }
}

/** 서버가 전달한 확인 스킬 이동 결과를 소유 클라이언트에 반영한다. */
void AFableCharacter::ClientConfirmSkillMove_Implementation(FVector_NetQuantizeNormal Direction, float Speed)
{
    if (!HasAuthority() && IsLocallyControlled() && IsAlive() && FMath::IsFinite(Speed))
    {
        LaunchCharacter(FVector(Direction).GetSafeNormal2D() * FMath::Clamp(Speed, 0.0f, 950.0f) + FVector(0, 0, 65),
                        true, true);
    }
}

/** 기본 공격의 사용 조건과 직업에 맞는 타격을 처리한다. */
void AFableCharacter::PerformAttack(bool bSkill)
{
    // 서버 권한과 실행 위치 조건 검사
    if (const AFablePlayerState* BackendState = GetPlayerState<AFablePlayerState>();
        BackendState && BackendState->IsBackendEnabled() && !BackendState->IsBackendReady())
    {
        return;
    }
    if (!HasAuthority() || !GetWorld() || !IsAlive())
    {
        return;
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const EFableClass HeroClass = GetHeroClass();
    const float ManaCost = bSkill ? GetSkillManaCost() : 0.0f;

    // 체력·마나·보호막·피해량 조건 검사
    if (Mana < ManaCost)
    {
        return;
    }

    Mana = FMath::Max(0.0f, Mana - ManaCost);

    // 위치·방향·거리 계산 처리
    if (Controller && FMath::IsFinite(Controller->GetControlRotation().Yaw))
    {
        SetActorRotation(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const FFableTrialCastContext TrialContext =
        bSkill && State ? State->CreateTrialCastContext(0) : FFableTrialCastContext();
    const float RawPower = State ? State->GetAttackPower() : 24.0f;
    const float RankScale = bSkill && State ? 1.0f + 0.15f * (FMath::Clamp(State->GetSkillRank(0), 1, 3) - 1) : 1.0f;
    const float AttackPower = (FMath::IsFinite(RawPower) ? FMath::Clamp(RawPower, 1.0f, 10000.0f) : 24.0f) * RankScale;
    const float Now = GetWorld()->GetTimeSeconds();
    CombatBuildEndServerTime = Now + 5.0f;

    // 직업·스킬·특성 빌드 처리
    if (bSkill)
    {
        NextSkillServerTime = Now + GetSkillCooldownDuration(0);
        if (SkillCastCounts.IsValidIndex(0))
        {
            ++SkillCastCounts[0];
        }
        ActionLockEndServerTime = Now + 0.55f;
        MulticastCombatCue(3);
    }
    else
    {
        ComboIndex = Now - LastAttackServerTime > 1.4f ? 0 : (ComboIndex + 1) % 3;
        LastAttackServerTime = Now;
        const float Cooldown = HeroClass == EFableClass::Mage
                                   ? 0.72f
                                   : (HeroClass == EFableClass::Archer ? 0.58f : FableCharacterTuning::AttackCooldown);
        NextAttackServerTime = Now + Cooldown;
        ActionLockEndServerTime = Now + 0.30f;
        MulticastCombatCue(ComboIndex);
    }

    // 직업·스킬·특성 빌드 조건 검사
    if (HeroClass != EFableClass::Warrior)
    {
        PerformRangedAttack(HeroClass, bSkill, AttackPower, 0, TrialContext);
        ForceNetUpdate();
        return;
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const float Range = bSkill ? GetSkillRange(0) : FableCharacterTuning::MeleeRange;
    // A starter sword still deals exactly 24 / 24 / 34, or 42 for the radial skill.
    const float Damage =
        AttackPower * (bSkill ? GetSkillDamageMultiplier(0) : (ComboIndex == 2 ? 34.0f / 24.0f : 1.0f));
    const FVector Origin = GetActorLocation();
    const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
    TSet<AFableEnemy*> HitEnemies;
    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(FableMeleeVisibility), false, this);
    IgnoreOtherExpeditions(this, TraceParams);

    // 직업·스킬·특성 빌드 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        AFableEnemy* Enemy = *It;

        // 지역·적·전투 진행 처리
        if (!IsValid(Enemy) || !Enemy->IsAlive() || !FableAdventure::CanAffect(this, Enemy) ||
            HitEnemies.Contains(Enemy))
        {
            continue;
        }

        const FVector Delta = Enemy->GetActorLocation() - Origin;

        // 이 단계에서 기본 공격의 사용 조건과 직업에 맞는 타격을 처리한다.
        if (Delta.SizeSquared2D() > FMath::Square(Range) || FMath::Abs(Delta.Z) > 170.0f)
        {
            continue;
        }

        // 직업·스킬·특성 빌드 처리
        if (!bSkill && !Delta.IsNearlyZero() && FVector::DotProduct(Forward, Delta.GetSafeNormal2D()) < 0.25f)
        {
            continue;
        }

        // 지역·적·전투 진행에 필요한 값 준비
        FHitResult Obstruction;
        const FVector EyeHeight(0.0f, 0.0f, 24.0f);
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
            Obstruction, Origin + EyeHeight, Enemy->GetActorLocation() + EyeHeight, ECC_Visibility, TraceParams);

        // 지역·적·전투 진행 처리
        if (bBlocked && Obstruction.GetActor() != Enemy)
        {
            continue;
        }

        // 지역·적·전투 진행 처리
        HitEnemies.Add(Enemy);
        const float PreviousHealth = Enemy->GetHealth();
        Enemy->ReceiveHit(Damage, Controller);

        // 직업·스킬·특성 빌드 처리
        if (State && Enemy->GetHealth() < PreviousHealth)
        {
            State->RecordTrialSkillHit(TrialContext);
        }
    }

    ForceNetUpdate();
}

/** 현재 직업의 원거리 공격 투사체를 생성한다. */
void AFableCharacter::PerformRangedAttack(EFableClass HeroClass, bool bSkill, float AttackPower, int32 SkillSlot,
                                          const FFableTrialCastContext& TrialContext)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !Controller)
    {
        return;
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const bool bUseArrow = HeroClass == EFableClass::Archer;
    const float MaximumRange = bUseArrow ? 2200.0f + 200.0f * GetCombatTalentRank(EFableTalentPath::First) : 1800.0f;
    // Start inside our own capsule (ignored by the shot), not beyond a nearby wall.
    const FVector Origin = GetActorLocation() + FVector(0, 0, 24);
    const FRotator ControlAim = Controller->GetControlRotation().GetNormalized();
    const float Pitch = FMath::IsFinite(ControlAim.Pitch) ? FMath::Clamp(ControlAim.Pitch, -35.0f, 35.0f) : 0.0f;
    FVector Direction = FRotator(Pitch, GetActorRotation().Yaw, 0).Vector();
    float BestScore = -1.0f;
    FCollisionQueryParams AimParams(SCENE_QUERY_STAT(FableProjectileAim), false, this);
    IgnoreOtherExpeditions(this, AimParams);

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        AimParams.AddIgnoredActor(*It);
    }
    // A small server-side aim assist makes the third-person camera usable.
    // It supplies direction only; the moving projectile still performs every hit test.

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        AFableEnemy* Enemy = *It;

        // 지역·적·전투 진행 처리
        if (!IsValid(Enemy) || !Enemy->IsAlive() || !FableAdventure::CanAffect(this, Enemy))
        {
            continue;
        }

        // 지역·적·전투 진행에 필요한 값 준비
        const FVector ToTarget = Enemy->GetActorLocation() - Origin;
        const float Distance = ToTarget.Size();

        // 위치·방향·거리 계산 처리
        if (Distance > MaximumRange || Distance < KINDA_SMALL_NUMBER)
        {
            continue;
        }

        // 위치·방향·거리 계산에 필요한 값 준비
        const FVector Candidate = ToTarget / Distance;
        const float Alignment = FVector::DotProduct(FRotator(Pitch, GetActorRotation().Yaw, 0).Vector(), Candidate);

        // 이 단계에서 현재 직업의 원거리 공격 투사체를 생성한다.
        if (Alignment < 0.94f)
        {
            continue;
        }

        const float Score = Alignment - Distance * 0.00002f;

        // 이 단계에서 현재 직업의 원거리 공격 투사체를 생성한다.
        if (Score <= BestScore)
        {
            continue;
        }

        // 지역·적·전투 진행에 필요한 값 준비
        FHitResult Obstruction;
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Obstruction, Origin, Enemy->GetActorLocation(),
                                                                   ECC_Visibility, AimParams);

        // 지역·적·전투 진행 처리
        if (!bBlocked || Obstruction.GetActor() == Enemy)
        {
            Direction = Candidate;
            BestScore = Score;
        }
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const int32 Count = bUseArrow && bSkill && SkillSlot != 1 ? 3 : 1;
    const float ShotDamage = AttackPower * (bSkill ? GetSkillDamageMultiplier(SkillSlot) : 1.0f);

    // 직업·스킬·특성 빌드 순회
    for (int32 Index = 0; Index < Count; ++Index)
    {
        // 직업·스킬·특성 빌드에 필요한 값 준비
        const float SpreadAngle =
            bUseArrow && SkillSlot == 0 ? 11.0f - 3.0f * GetCombatTalentRank(EFableTalentPath::First) : 11.0f;
        const float Spread = Count == 3 ? (Index - 1) * SpreadAngle : 0.0f;
        const FVector ShotDirection = Direction.RotateAngleAxis(Spread, FVector::UpVector);
        const FTransform SpawnTransform(ShotDirection.Rotation(), Origin);
        AFableProjectile* Shot =
            GetWorld()->SpawnActorDeferred<AFableProjectile>(AFableProjectile::StaticClass(), SpawnTransform, this,
                                                             this, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

        // 직업·스킬·특성 빌드 처리
        if (Shot)
        {
            Shot->InitializeShot(ShotDirection, ShotDamage, Controller, bUseArrow,
                                 !bUseArrow && bSkill ? GetSkillRange(0) : 0.0f, bUseArrow ? 2100.0f : 1250.0f,
                                 MaximumRange, bUseArrow && bSkill && SkillSlot == 1 ? 2 : 0);
            Shot->SetTrialCastContext(TrialContext);
            const int32 FireRank = !bUseArrow && bSkill ? GetCombatTalentRank(EFableTalentPath::First) : 0;
            if (FireRank > 0)
            {
                Shot->SetBurnPayload(AttackPower * 0.10f * FireRank, 3);
            }
            Shot->FinishSpawning(SpawnTransform);
        }
    }
}

/** 현재 직업 값을 반환한다. */
EFableClass AFableCharacter::GetHeroClass() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (State && (State->HeroClass == EFableClass::Mage || State->HeroClass == EFableClass::Archer))
    {
        return State->HeroClass;
    }
    return EFableClass::Warrior;
}

/** 선택한 스킬의 마나 소모량 값을 반환한다. */
float AFableCharacter::GetSkillManaCost(int32 Slot) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Slot < 0 || Slot > 2 || GetHeroClass() == EFableClass::Warrior)
    {
        return 0.0f;
    }
    if (GetHeroClass() == EFableClass::Mage)
    {
        return Slot == 0 ? 30.0f : 35.0f;
    }

    return Slot == 1 ? 30.0f : 25.0f;
}

/** 선택한 스킬의 전체 재사용 대기시간 값을 반환한다. */
float AFableCharacter::GetSkillCooldownDuration(int32 Slot) const
{
    // 직업·스킬·특성 빌드 조건 검사
    if (Slot < 0 || Slot > 2)
    {
        return 0.0f;
    }
    if (Slot == 0)
    {
        return GetHeroClass() == EFableClass::Mage ? 6.0f : FableCharacterTuning::SkillCooldown;
    }
    if (Slot == 1)
    {
        return GetHeroClass() == EFableClass::Mage
                   ? 9.0f
                   : (GetHeroClass() == EFableClass::Warrior ? 8.0f - GetCombatTalentRank(EFableTalentPath::First)
                                                             : 8.0f);
    }

    return GetHeroClass() == EFableClass::Archer ? 10.0f - GetCombatTalentRank(EFableTalentPath::Second) : 14.0f;
}

/** 남은 보호막 흡수량 값을 반환한다. */
float AFableCharacter::GetShieldAmount() const
{
    return IsAlive() && GetShieldTimeRemaining() > 0 ? ShieldAmount : 0.0f;
}

/** 보호막의 남은 지속 시간 값을 반환한다. */
float AFableCharacter::GetShieldTimeRemaining() const
{
    return ShieldAmount > 0 ? FMath::Max(0.0f, ShieldEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

/** 해당 스킬의 시전 횟수 값을 반환한다. */
int32 AFableCharacter::GetSkillCastCount(int32 Slot) const
{
    return SkillCastCounts.IsValidIndex(Slot) ? SkillCastCounts[Slot] : 0;
}

/** 전투에 적용할 특성 단계 값을 반환한다. */
int32 AFableCharacter::GetCombatTalentRank(EFableTalentPath Path) const
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    return Profile ? FMath::Clamp(Profile->GetTalentRank(Path), 0, 2) : 0;
}

/** 현재 상태에서 허용되는 이동 속도 값을 반환한다. */
float AFableCharacter::GetMovementSpeedLimit() const
{
    const int32 Rank = GetHeroClass() == EFableClass::Archer ? GetCombatTalentRank(EFableTalentPath::Second) : 0;
    return FableCharacterTuning::WalkSpeed * (1.0f + 0.05f * Rank);
}

/** 회피의 전체 재사용 대기시간 값을 반환한다. */
float AFableCharacter::GetDodgeCooldownDuration() const
{
    const int32 Rank = GetHeroClass() == EFableClass::Archer ? GetCombatTalentRank(EFableTalentPath::Second) : 0;
    return FableCharacterTuning::DodgeCooldown - 0.15f * Rank;
}

/** 스킬 피해 배율 값을 반환한다. */
float AFableCharacter::GetSkillDamageMultiplier(int32 Slot) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Slot < 0 || Slot > 2)
    {
        return 0.0f;
    }

    const int32 First = GetCombatTalentRank(EFableTalentPath::First);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (GetHeroClass() == EFableClass::Warrior)
    {
        return Slot == 0 ? (42.0f / 24.0f) * (1.0f + 0.10f * First)
                         : (Slot == 1 ? 1.45f * (1.0f + 0.15f * First) : 0.0f);
    }
    if (GetHeroClass() == EFableClass::Mage)
    {
        return Slot == 0 ? 1.75f * (1.0f + 0.10f * First) : (Slot == 1 ? 0.9f : 0.0f);
    }

    return Slot == 1 ? 1.6f * (1.0f + 0.15f * First) : 0.85f;
}

/** 스킬 적용 거리 값을 반환한다. */
float AFableCharacter::GetSkillRange(int32 Slot) const
{
    // 직업·스킬·특성 빌드 조건 검사
    if (Slot < 0 || Slot > 2)
    {
        return 0.0f;
    }
    if (GetHeroClass() == EFableClass::Archer)
    {
        return 2200.0f + 200.0f * GetCombatTalentRank(EFableTalentPath::First);
    }
    if (GetHeroClass() == EFableClass::Mage)
    {
        return Slot == 0 ? 300.0f + 30.0f * GetCombatTalentRank(EFableTalentPath::First) : (Slot == 1 ? 650.0f : 0.0f);
    }

    return Slot == 0 ? FableCharacterTuning::SkillRange + 40.0f * GetCombatTalentRank(EFableTalentPath::Second)
                     : (Slot == 1 ? 550.0f + 50.0f * GetCombatTalentRank(EFableTalentPath::First) : 0.0f);
}

/** 스킬 보호막의 최대 흡수량 값을 반환한다. */
float AFableCharacter::GetSkillShieldCapacity(int32 Slot) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Slot != 2 || GetHeroClass() == EFableClass::Archer)
    {
        return 0.0f;
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Rank = Profile ? FMath::Clamp(Profile->GetSkillRank(Slot), 1, 3) : 1;
    const int32 Guardian = GetCombatTalentRank(EFableTalentPath::Second);

    return GetHeroClass() == EFableClass::Mage ? 60.0f + 20.0f * (Rank - 1) + 15.0f * Guardian
                                               : 45.0f + 15.0f * (Rank - 1) + 20.0f * Guardian;
}

/** 스킬 보호막의 지속 시간 값을 반환한다. */
float AFableCharacter::GetSkillShieldDuration(int32 Slot) const
{
    return Slot == 2 && GetHeroClass() != EFableClass::Archer ? 6.0f + GetCombatTalentRank(EFableTalentPath::Second)
                                                              : 0.0f;
}

/** 빌드 변경 후 남은 대기시간 값을 반환한다. */
float AFableCharacter::GetBuildChangeCooldownRemaining() const
{
    // 현재 월드와 플레이어 참조 조건 검사
    if (!GetWorld())
    {
        return 0.0f;
    }

    // 서버 시각과 재사용 대기시간에 필요한 값 준비
    float Remaining = FMath::Max(0.0f, CombatBuildEndServerTime - GetSynchronizedServerTime());
    Remaining = FMath::Max(Remaining, GetAttackCooldownRemaining());

    // 직업·스킬·특성 빌드 순회
    for (int32 Slot = 0; Slot < 3; ++Slot)
    {
        Remaining = FMath::Max(Remaining, GetSkillCooldownRemaining(Slot));
    }

    // 체력·마나·보호막·피해량 처리
    Remaining = FMath::Max(Remaining, GetDodgeCooldownRemaining());
    Remaining = FMath::Max(Remaining, GetShieldTimeRemaining());

    return Remaining;
}

/** 서버에서 실제로 적용한 특성 화상 피해를 누적한다. */
void AFableCharacter::RecordTalentBurnDamage(float ActualDamage)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive() || !GetWorld() || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f)
    {
        return;
    }

    // 직업·스킬·특성 빌드 처리
    TalentBurnDamageDealt += ActualDamage;
    CombatBuildEndServerTime = GetWorld()->GetTimeSeconds() + 5.0f;
    ForceNetUpdate();
}

/** 수호 특성의 조건을 만족하는 주변 동료에게 보호막을 나눈다. */
void AFableCharacter::ShareGuardianShield(int32 Rank)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !Controller || Rank <= 0)
    {
        return;
    }

    // 위치·방향·거리 계산에 필요한 값 준비
    const FVector Origin = GetActorLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableGuardianShare), false, this);
    IgnoreOtherExpeditions(this, Params);

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        Params.AddIgnoredActor(*It);
    }

    const float Now = GetWorld()->GetTimeSeconds();

    // 지역·적·전투 진행 순회
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        AFableCharacter* Ally = *It;

        // 현재 월드와 플레이어 참조 처리
        const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
        if (Ally == this || !Ally->IsAlive() || !Ally->Controller || Ally->Controller == Controller ||
            !FableAdventure::SameScope(this, Ally) || !Parties ||
            !Parties->ArePartyMembers(GetPlayerState<AFablePlayerState>(), Ally->GetPlayerState<AFablePlayerState>()) ||
            !Ally->GetPlayerState<AFablePlayerState>())
        {
            continue;
        }

        const FVector Delta = Ally->GetActorLocation() - Origin;

        // 이 단계에서 수호 특성의 조건을 만족하는 주변 동료에게 보호막을 나눈다.
        if (Delta.SizeSquared2D() > FMath::Square(600.0f) || FMath::Abs(Delta.Z) > 180.0f)
        {
            continue;
        }

        bool bSameRegion = false;
        const auto Adventure = FableAdventure::For(this);

        // 지역·적·전투 진행 순회
        for (EFableRegion Region : {EFableRegion::Village, EFableRegion::RootRuins, EFableRegion::Moonwell})
        {
            bSameRegion |= Adventure && Adventure.IsInRegion(Region, Origin) &&
                           Adventure.IsInRegion(Region, Ally->GetActorLocation());
        }

        // 지역·적·전투 진행 처리
        if (!bSameRegion)
        {
            continue;
        }

        FHitResult Hit;

        // 현재 월드와 플레이어 참조 처리
        if (GetWorld()->LineTraceSingleByChannel(Hit, Origin + FVector(0, 0, 24),
                                                 Ally->GetActorLocation() + FVector(0, 0, 24), ECC_Visibility, Params))
        {
            continue;
        }

        // 체력·마나·보호막·피해량에 필요한 값 준비
        const float OldAmount = Ally->GetShieldAmount();
        const float OldEnd = OldAmount > 0.0f ? Ally->ShieldEndServerTime : 0.0f;
        const float NewAmount = FMath::Max(OldAmount, 20.0f * Rank);
        const float NewEnd = FMath::Max(OldEnd, Now + 6.0f + Rank);

        // 이 단계에서 수호 특성의 조건을 만족하는 주변 동료에게 보호막을 나눈다.
        if (FMath::IsNearlyEqual(OldAmount, NewAmount) && FMath::IsNearlyEqual(OldEnd, NewEnd))
        {
            continue;
        }

        // 체력·마나·보호막·피해량 처리
        Ally->ShieldAmount = NewAmount;
        Ally->ShieldEndServerTime = NewEnd;
        Ally->CombatBuildEndServerTime = FMath::Max(Ally->CombatBuildEndServerTime, Now + 5.0f);
        Ally->MulticastCombatCue(7);
        Ally->ForceNetUpdate();
        ++TalentAllyShieldsGranted;
    }
}

/** 직업·장비·빌드 변경을 캐릭터의 전투 수치에 반영한다. */
void AFableCharacter::SynchronizeStats()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority())
    {
        return;
    }

    // 체력·마나·보호막·피해량 처리
    if (LastServerClass != GetHeroClass())
    {
        LastServerClass = GetHeroClass();
        ShieldAmount = ShieldEndServerTime = 0.0f;
        // Keep slot cooldowns when switching class so swapping cannot reset skills.
        ForceNetUpdate();
    }

    // 체력·마나·보호막·피해량에 필요한 값 준비
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const float Bonus = State ? State->GetMaxHealthBonus() : 0.0f;
    const float DesiredMaximum = BaseMaxHealth + (FMath::IsFinite(Bonus) ? FMath::Clamp(Bonus, 0.0f, 10000.0f) : 0.0f);

    // 체력·마나·보호막·피해량 처리
    if (!FMath::IsNearlyEqual(MaxHealth, DesiredMaximum))
    {
        const float PreviousMaximum = MaxHealth;
        MaxHealth = DesiredMaximum;
        // Preserve health percentage so equipping armor repeatedly cannot generate healing.
        Health =
            IsAlive() ? FMath::Clamp((Health / FMath::Max(1.0f, PreviousMaximum)) * MaxHealth, 0.0f, MaxHealth) : 0.0f;
        ForceNetUpdate();
    }
}

/** 현재 직업에 맞는 무기와 의상 표시를 갱신한다. */
void AFableCharacter::ApplyClassAppearance()
{
    // 서버 권한과 실행 위치 조건 검사
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const EFableClass HeroClass = GetHeroClass();
    const AFablePlayerState* Progress = GetPlayerState<AFablePlayerState>();
    const int32 Tier = Progress ? Progress->GetAdvancementTier() : 1;

    // 직업·스킬·특성 빌드 처리
    if (bClassAppearanceInitialized && LastVisualClass == HeroClass && LastVisualTier > 0 && Tier > LastVisualTier)
    {
        SkillVisualRemaining = 0.55f;
        SkillVisualCue = 7;
        FableCombatFeedback::PlayCue(this, GetActorLocation(), 3);
    }

    LastVisualTier = Tier;
    LastVisualClass = HeroClass;
    bClassAppearanceInitialized = true;

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UStaticMeshComponent* Part : SwordParts)
    {
        Part->SetVisibility(HeroClass == EFableClass::Warrior);
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UStaticMeshComponent* Part : StaffParts)
    {
        Part->SetVisibility(HeroClass == EFableClass::Mage);
    }

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UStaticMeshComponent* Part : BowParts)
    {
        Part->SetVisibility(HeroClass == EFableClass::Archer);
    }

    const FLinearColor Tint = HeroClass == EFableClass::Mage
                                  ? FLinearColor(0.16f, 0.065f, 0.30f)
                                  : (HeroClass == EFableClass::Archer ? FLinearColor(0.075f, 0.24f, 0.055f)
                                                                      : FLinearColor(0.026f, 0.20f, 0.19f));

    // 같은 규칙을 적용할 대상을 순회한다.
    for (UMaterialInstanceDynamic* Material : ClassClothMaterials)
    {
        if (Material)
        {
            Material->SetVectorParameterValue(TEXT("Color"), Tint * (1.0f + 0.16f * (Tier - 1)));
        }
    }
}

/** 회피 사용 조건을 확인하고 이동과 재사용 대기시간을 적용한다. */
void AFableCharacter::PerformDodge()
{
    // 이동 입력과 속도 제한에 필요한 값 준비
    const float Now = GetWorld()->GetTimeSeconds();
    FVector Direction = GetVelocity().GetSafeNormal2D();

    // 위치·방향·거리 계산 처리
    if (Direction.IsNearlyZero())
    {
        Direction = GetActorForwardVector().GetSafeNormal2D();
    }

    // 위치·방향·거리 계산 처리
    bDodging = true;
    DodgeEndServerTime = Now + FableCharacterTuning::DodgeDuration;
    NextDodgeServerTime = Now + GetDodgeCooldownDuration();
    LaunchCharacter(Direction * FableCharacterTuning::DodgeSpeed + FVector(0, 0, 70.0f), true, true);
    // The server sets velocity and invulnerability. This only mirrors its launch for a remote owner;
    // if the cue is lost, CharacterMovement still corrects the owner to the server's movement.

    // 서버 권한과 실행 위치 처리
    if (!IsLocallyControlled())
    {
        ClientConfirmDodge(FVector_NetQuantizeNormal(Direction));
    }

    MulticastCombatCue(4);
    ForceNetUpdate();
}

/** 서버가 전달한 확인 회피 결과를 소유 클라이언트에 반영한다. */
void AFableCharacter::ClientConfirmDodge_Implementation(FVector_NetQuantizeNormal Direction)
{
    if (!HasAuthority() && IsLocallyControlled() && IsAlive())
    {
        const FVector SafeDirection = FVector(Direction).GetSafeNormal2D();
        if (!SafeDirection.IsNearlyZero())
        {
            LaunchCharacter(SafeDirection * FableCharacterTuning::DodgeSpeed + FVector(0, 0, 70.0f), true, true);
        }
    }
}

/** 살아 있는 캐릭터의 체력 또는 마나를 허용량만큼 회복한다. */
bool AFableCharacter::RestoreResource(bool bMana, float Amount)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0.0f)
    {
        return false;
    }

    // 체력·마나·보호막·피해량 처리
    SynchronizeStats();
    float& Resource = bMana ? Mana : Health;
    const float Maximum = bMana ? GetMaxMana() : MaxHealth;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Resource >= Maximum)
    {
        return false;
    }

    const float Restored = FMath::Min(Amount, Maximum - Resource);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Restored <= 0.0f)
    {
        return false;
    }

    Resource += Restored;
    ForceNetUpdate();

    return true;
}

/** 권한과 생존 상태를 확인하고 보호막·방어력을 반영해 피해를 적용한다. */
void AFableCharacter::ReceiveHit(float Damage, const AActor* Source)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !FMath::IsFinite(Damage) || Damage <= 0.0f)
    {
        return;
    }

    // private 전투의 출처 없는 피해와 다른 파티의 피해는 자원 변경 전에 거절한다.
    const auto Adventure = FableAdventure::For(this);
    if (!Adventure || (Source ? !FableAdventure::CanAffect(Source, this) : Adventure.ExpeditionId().IsValid()))
    {
        return;
    }

    CombatBuildEndServerTime = GetWorld()->GetTimeSeconds() + 5.0f;

    // 생존 상태와 부활 진행 조건 검사
    if (bDodging)
    {
        ForceNetUpdate();
        return;
    }
    if (GetReviveProtectionRemaining() > 0.0f)
    {
        ++ReviveProtectionBlockedHits;
        ForceNetUpdate();
        return;
    }

    // 생존 상태와 부활 진행 처리
    CancelReviveInternal(TEXT("피해 받음"));
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const float RawDefense = State ? State->GetDefense() : 0.0f;
    const float Defense = FMath::IsFinite(RawDefense) ? FMath::Clamp(RawDefense, 0.0f, 1000.0f) : 0.0f;
    // Diminishing returns avoid complete immunity while making every armor point useful.
    float EffectiveDamage = Damage * (100.0f / (100.0f + Defense));

    // 체력·마나·보호막·피해량 처리
    if (GetShieldAmount() > 0.0f)
    {
        const float Absorbed = FMath::Min(ShieldAmount, EffectiveDamage);
        ShieldAmount -= Absorbed;
        EffectiveDamage -= Absorbed;
        TotalShieldAbsorbed += Absorbed;
        MulticastCombatCue(9);
    }

    // 체력·마나·보호막·피해량에 필요한 값 준비
    const float PreviousHealth = Health;
    Health = FMath::Max(0.0f, Health - FMath::Min(EffectiveDamage, MaxHealth));

    // 체력·마나·보호막·피해량 처리
    if (State && Health < PreviousHealth)
    {
        // Shield-only absorption, dodges and protection never wear armor.
        // PlayerState throttles wear per equipment slot across all damage sources.
        State->WearEquipment(EFableEquipmentSlot::Armor, 1);
        SynchronizeStats();
    }

    MulticastCombatCue(5);

    // 생존 상태와 부활 진행 처리
    if (Health <= 0.0f)
    {
        EnterDowned();
    }
    else
    {
        OnRep_Health();
    }

    ForceNetUpdate();
}

/** 행동 불능 상태의 남은 구조 시간 값을 반환한다. */
float AFableCharacter::GetDownedTimeRemaining() const
{
    return IsDowned() ? FMath::Max(0.0f, DownedEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

/** 부활 보호 효과의 남은 시간 값을 반환한다. */
float AFableCharacter::GetReviveProtectionRemaining() const
{
    return IsAlive() ? FMath::Max(0.0f, ReviveProtectionEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

/** 동료 부활의 진행률 값을 반환한다. */
float AFableCharacter::GetReviveProgress() const
{
    // 생존 상태와 부활 진행 조건 검사
    if (ReviveStartServerTime < 0.0f)
    {
        return 0.0f;
    }

    // 생존 상태와 부활 진행에 필요한 값 준비
    const bool bHelping = IsAlive() && IsValid(ReviveTarget.Get()) && ReviveTarget->IsDowned();
    const bool bReceiving = IsDowned() && IsValid(Reviver.Get()) && Reviver->IsAlive();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!bHelping && !bReceiving)
    {
        return 0.0f;
    }

    return FMath::Clamp((GetSynchronizedServerTime() - ReviveStartServerTime) / FableCharacterTuning::ReviveDuration,
                        0.0f, 1.0f);
}

/** 거리·생존 상태·시전자를 확인해 부활 시전 유지 여부를 판단한다. */
bool AFableCharacter::CanMaintainRevive(const AFableCharacter* Target) const
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !IsValid(Target) || Target == this ||
        Target->GetWorld() != GetWorld() || !Target->HasAuthority() || !Target->IsDowned() || bLegacyRecovery ||
        Target->bLegacyRecovery || !Controller || !Target->Controller || Controller == Target->Controller ||
        !Controller->IsPlayerController() || !Target->Controller->IsPlayerController() ||
        Controller->GetPawn() != this || Target->Controller->GetPawn() != Target ||
        !GetPlayerState<AFablePlayerState>() || !Target->GetPlayerState<AFablePlayerState>() ||
        GetPlayerState<AFablePlayerState>() == Target->GetPlayerState<AFablePlayerState>() ||
        Controller->IsMoveInputIgnored() || bDodging || bPressedJump || !GetCharacterMovement()->IsMovingOnGround() ||
        GetVelocity().ContainsNaN() || GetVelocity().SizeSquared() > FMath::Square(15.0f) ||
        GetWorld()->GetTimeSeconds() >= Target->DownedEndServerTime ||
        GetWorld()->GetTimeSeconds() < ActionLockEndServerTime || (Target->Reviver && Target->Reviver.Get() != this))
    {
        return false;
    }

    // 위치·방향·거리 계산에 필요한 값 준비
    const FVector Origin = GetActorLocation();
    const FVector Destination = Target->GetActorLocation();
    const FVector Delta = Destination - Origin;

    // 생존 상태와 부활 진행 조건 검사
    if (Origin.ContainsNaN() || Destination.ContainsNaN() ||
        Delta.SizeSquared2D() > FMath::Square(FableCharacterTuning::ReviveDistance) ||
        FMath::Abs(Delta.Z) > FableCharacterTuning::ReviveHeight)
    {
        return false;
    }

    // 시전 중에도 파티와 원정이 같아야 하며 연결·원정 변경은 즉시 부활을 중단한다.
    const auto Adventure = FableAdventure::For(this);
    const AFablePartyManager* Parties = AFablePartyManager::Find(GetWorld());
    if (!Adventure || !FableAdventure::SameScope(this, Target) || !Parties ||
        !Parties->ArePartyMembers(GetPlayerState<AFablePlayerState>(), Target->GetPlayerState<AFablePlayerState>()))
    {
        return false;
    }

    bool bSameRegion = false;

    // 지역·적·전투 진행 순회
    for (const EFableRegion Region : {EFableRegion::Village, EFableRegion::RootRuins, EFableRegion::Moonwell})
    {
        if (Adventure.IsInRegion(Region, Origin) && Adventure.IsInRegion(Region, Destination))
        {
            bSameRegion = true;
            break;
        }
    }

    // 지역·적·전투 진행 조건 검사
    if (!bSameRegion)
    {
        return false;
    }

    // 생존 상태와 부활 진행에 필요한 값 준비
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableReviveVisibility), false, this);
    IgnoreOtherExpeditions(this, Params);
    Params.AddIgnoredActor(Target);
    FHitResult Hit;

    return !GetWorld()->LineTraceSingleByChannel(Hit, Origin + FVector(0, 0, 24), Destination + FVector(0, 0, 24),
                                                 ECC_Visibility, Params);
}

/** 서버에서 동료 부활 조건을 검사하고 중복 없이 시전을 시작한다. */
bool AFableCharacter::BeginRevive(AFableCharacter* Target)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld())
    {
        return false;
    }

    // 생존 상태와 부활 진행 처리
    if (ReviveTarget)
    {
        // Repeated E/start packets must not restart or speed up an existing channel.
        if (ReviveTarget.Get() != Target)
        {
            return false;
        }
        if (CanMaintainRevive(Target))
        {
            return true;
        }
        CancelReviveInternal(TEXT("부활 조건 변경"));
        return false;
    }

    const float Now = GetWorld()->GetTimeSeconds();

    // 생존 상태와 부활 진행 조건 검사
    if (Now - LastReviveRequestServerTime < 0.15f)
    {
        return false;
    }

    LastReviveRequestServerTime = Now;

    // 생존 상태와 부활 진행 조건 검사
    if (!CanMaintainRevive(Target) || Reviver || Target->ReviveTarget)
    {
        return false;
    }

    // 생존 상태와 부활 진행 처리
    ReviveTarget = Target;
    Target->Reviver = this;
    ReviveStartServerTime = Target->ReviveStartServerTime = Now;
    ReviveStartLocation = GetActorLocation();
    const FVector Facing = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();

    // 위치·방향·거리 계산 처리
    if (!Facing.IsNearlyZero())
    {
        SetActorRotation(Facing.Rotation());
    }

    ForceNetUpdate();
    Target->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("동료 부활 시작: %s -> %s (3초)"), *GetName(), *Target->GetName());

    return true;
}

/** 진행 중인 동료 부활의 취소를 처리하거나 서버에 요청한다. */
void AFableCharacter::CancelRevive()
{
    if (HasAuthority())
    {
        CancelReviveInternal(TEXT("입력 해제 또는 화면 전환"));
    }
    else if (IsLocallyControlled())
    {
        ServerCancelRevive();
    }
}

/** 동료 부활 취소 요청을 서버의 권한 검사와 게임 규칙에 전달한다. */
void AFableCharacter::ServerCancelRevive_Implementation()
{
    // A delayed jump/release from before being downed cannot cancel help now being received.
    if (ReviveTarget)
    {
        CancelReviveInternal(TEXT("입력 해제 또는 다른 행동"));
    }
}

/** 시전자와 대상에 연결된 부활 진행 상태를 함께 해제한다. */
void AFableCharacter::CancelReviveInternal(const TCHAR* Reason)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority())
    {
        return;
    }

    // 생존 상태와 부활 진행에 필요한 값 준비
    AFableCharacter* Target = ReviveTarget.Get();
    AFableCharacter* Helper = Reviver.Get();
    const bool bHadChannel = Target || Helper || ReviveStartServerTime >= 0.0f;
    ReviveTarget = nullptr;
    Reviver = nullptr;
    ReviveStartServerTime = -1.0f;

    // 생존 상태와 부활 진행 처리
    if (Target)
    {
        ++ReviveCancelCount;
        if (IsValid(Target) && Target->Reviver.Get() == this)
        {
            Target->Reviver = nullptr;
            Target->ReviveStartServerTime = -1.0f;
            Target->ForceNetUpdate();
        }
    }

    // 생존 상태와 부활 진행 처리
    if (IsValid(Helper) && Helper->ReviveTarget.Get() == this)
    {
        Helper->ReviveTarget = nullptr;
        Helper->ReviveStartServerTime = -1.0f;
        ++Helper->ReviveCancelCount;
        Helper->ForceNetUpdate();
    }

    // 이 단계에서 시전자와 대상에 연결된 부활 진행 상태를 함께 해제한다.
    if (bHadChannel)
    {
        ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("동료 부활 취소: %s (%s)"), *GetName(), Reason);
    }
}

/** 동료 부활을 완료하고 체력과 잠시 유지되는 보호 효과를 부여한다. */
void AFableCharacter::CompleteRevive()
{
    AFableCharacter* Target = ReviveTarget.Get();

    // 생존 상태와 부활 진행 조건 검사
    if (!CanMaintainRevive(Target) || Target->Reviver.Get() != this || ReviveStartServerTime < 0.0f ||
        GetWorld()->GetTimeSeconds() - ReviveStartServerTime < FableCharacterTuning::ReviveDuration)
    {
        return;
    }
    // No checkpoint teleport, cooldown reset, reward, inventory or save mutation occurs here.

    // 생존 상태와 부활 진행 처리
    ReviveTarget = nullptr;
    ReviveStartServerTime = -1.0f;
    Target->Reviver = nullptr;
    Target->ReviveStartServerTime = -1.0f;
    Target->SynchronizeStats();
    Target->LifeState = EFableLifeState::Active;
    Target->Health = Target->MaxHealth * 0.40f;
    Target->Mana = FMath::Clamp(FMath::Max(Target->Mana, 30.0f), 0.0f, Target->GetMaxMana());
    Target->DownedEndServerTime = 0.0f;
    Target->DefeatedServerTime = -1.0f;
    Target->ReviveProtectionEndServerTime =
        GetWorld()->GetTimeSeconds() + FableCharacterTuning::ReviveProtectionDuration;
    Target->GetWorldTimerManager().ClearTimer(Target->RespawnTimer);
    Target->GetCharacterMovement()->ClearAccumulatedForces();
    Target->GetCharacterMovement()->StopMovementImmediately();
    Target->SetActorHiddenInGame(false);
    Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Target->OnRep_Health();
    ++RevivesPerformedCount;
    ++Target->TimesRevivedCount;
    ForceNetUpdate();
    Target->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("동료 부활 완료: %s -> %s 체력 %.1f/%.1f, 보호 2초"), *GetName(), *Target->GetName(),
           Target->Health, Target->MaxHealth);
}

/** 구조 가능한 행동 불능 상태로 전환하고 시전을 정리한다. */
void AFableCharacter::EnterDowned()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || LifeState != EFableLifeState::Active)
    {
        return;
    }
    if (bLegacyRecovery)
    {
        EnterDefeated(TEXT("기존 시제품 복구"));
        return;
    }

    // 생존 상태와 부활 진행 처리
    CancelReviveInternal(TEXT("쓰러짐"));
    Health = 0.0f;
    LifeState = EFableLifeState::Downed;
    DownedEndServerTime = GetWorld()->GetTimeSeconds() + FableCharacterTuning::DownedDuration;
    DefeatedServerTime = -1.0f;
    ReviveProtectionEndServerTime = 0.0f;
    ShieldAmount = ShieldEndServerTime = 0.0f;
    bDodging = false;
    DodgeEndServerTime = ActionLockEndServerTime = 0.0f;
    GetWorldTimerManager().ClearTimer(RespawnTimer);
    ++DownedCount;
    OnRep_Health();
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("동료 쓰러짐: %s, 구조 가능 25초"), *GetName());
}

/** 구조 시간이 끝난 캐릭터의 쓰러짐을 확정한다. */
void AFableCharacter::EnterDefeated(const TCHAR* Reason)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || IsDefeated())
    {
        return;
    }

    // 생존 상태와 부활 진행 처리
    CancelReviveInternal(Reason);
    Health = 0.0f;
    LifeState = EFableLifeState::Defeated;
    DownedEndServerTime = 0.0f;
    DefeatedServerTime = GetWorld()->GetTimeSeconds();
    ReviveProtectionEndServerTime = 0.0f;
    ShieldAmount = ShieldEndServerTime = 0.0f;
    bDodging = false;
    DodgeEndServerTime = ActionLockEndServerTime = 0.0f;
    GetWorldTimerManager().ClearTimer(RespawnTimer);
    OnRep_Health();

    // 체력·마나·보호막·피해량 처리
    if (bLegacyRecovery)
    {
        GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFableCharacter::RespawnAtSafePoint, 3.0f, false);
    }

    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("동료 구조 대기: %s (%s)"), *GetName(), Reason);
}

/** 행동 불능 제한 시간과 부활 시전의 완료·취소를 갱신한다. */
void AFableCharacter::TickLifeState()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld())
    {
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();
    // Falling out of the world bypasses dodge, shields and revival protection.

    // 생존 상태와 부활 진행 조건 검사
    if (!IsDefeated() && GetActorLocation().Z < -800.0f)
    {
        EnterDefeated(TEXT("낙하 구조"));
        return;
    }
    if (IsDowned() && Now >= DownedEndServerTime)
    {
        EnterDefeated(TEXT("구조 시간 만료"));
        return;
    }

    // 생존 상태와 부활 진행 처리
    if (ReviveTarget)
    {
        if (!CanMaintainRevive(ReviveTarget.Get()) || ReviveTarget->Reviver.Get() != this ||
            FVector::DistSquared(GetActorLocation(), ReviveStartLocation) > FMath::Square(25.0f))
        {
            CancelReviveInternal(TEXT("이동·거리·시야 또는 동료 상태 변경"));
        }
        else if (Now - ReviveStartServerTime >= FableCharacterTuning::ReviveDuration)
        {
            CompleteRevive();
        }
    }

    // 생존 상태와 부활 진행 처리
    if (Reviver && (!IsValid(Reviver.Get()) || Reviver->ReviveTarget.Get() != this || !IsDowned()))
    {
        CancelReviveInternal(TEXT("부활 연결 종료"));
    }

    // 생존 상태와 부활 진행 처리
    if (ReviveStartServerTime >= 0.0f && !ReviveTarget && !Reviver)
    {
        CancelReviveInternal(TEXT("동료 연결 종료"));
    }
}

/** 복제된 현재 체력 변경을 화면과 관련 상태에 반영한다. */
void AFableCharacter::OnRep_Health()
{
    if (!IsAlive())
    {
        StopJumping();
        GetCharacterMovement()->ClearAccumulatedForces();
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        AttackVisualRemaining = 0.0f;
        SkillVisualRemaining = 0.0f;
    }
    else if (GetCharacterMovement()->MovementMode == MOVE_None)
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
}

/** 서버에서 안전 복귀 위치와 회전·자원 회복 여부를 설정한다. */
void AFableCharacter::SetCheckpoint(const FVector& Location, const FRotator& Rotation, bool bRestore)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || Location.ContainsNaN() || Rotation.ContainsNaN())
    {
        return;
    }

    FVector SafeLocation = Location;

    // 현재 월드와 플레이어 참조 조건 검사
    if (!GetWorld()->FindTeleportSpot(this, SafeLocation, Rotation) ||
        !TeleportTo(SafeLocation, Rotation, false, false))
    {
        UE_LOG(LogTemp, Warning, TEXT("%s의 이동 지점이 막혀 있어 기존 부활 지점을 유지합니다."), *GetName());
        return;
    }

    CommitCheckpointAtCurrentPosition(Rotation, bRestore);
}

/** 전원 이동 성공 뒤 현재 위치에서만 체크포인트와 전투 정리를 확정한다. */
void AFableCharacter::CommitCheckpointAtCurrentPosition(const FRotator& Rotation, bool bRestore)
{
    if (!HasAuthority() || !GetWorld() || GetActorLocation().ContainsNaN() || Rotation.ContainsNaN())
    {
        return;
    }

    // 귀환 직후 폐기되는 이전 원정 지형을 이동 기반으로 계속 참조하지 않는다.
    SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));

    // 생존 상태와 부활 진행 처리
    CancelReviveInternal(TEXT("안전 지점 이동"));
    SafeSpawnLocation = GetActorLocation();
    SafeSpawnRotation = Rotation;
    GetWorldTimerManager().ClearTimer(RespawnTimer);
    ReviveProtectionEndServerTime = 0.0f;
    bDodging = false;
    DodgeEndServerTime = 0.0f;
    ActionLockEndServerTime = 0.0f;
    NextAttackServerTime = NextSkillServerTime = NextDodgeServerTime = 0.0f;
    NextSecondSkillServerTime = NextThirdSkillServerTime = 0.0f;
    ShieldAmount = ShieldEndServerTime = 0.0f;
    LastRequestServerTime = LastAttackServerTime = -10.0f;
    NextLocalRequestTime = 0.0f;
    ComboIndex = 0;
    AttackVisualRemaining = SkillVisualRemaining = HurtVisualRemaining = 0.0f;
    StopJumping();
    GetCharacterMovement()->ClearAccumulatedForces();
    GetCharacterMovement()->StopMovementImmediately();
    SynchronizeStats();

    // 생존 상태와 부활 진행 처리
    if (bRestore)
    {
        LifeState = EFableLifeState::Active;
        DownedEndServerTime = 0.0f;
        DefeatedServerTime = -1.0f;
        ReviveStartServerTime = -1.0f;
        LastReviveRequestServerTime = -10.0f;
        Health = MaxHealth;
        Mana = GetMaxMana();
        SetActorHiddenInGame(false);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    }

    OnRep_Health();

    // 이동 입력과 속도 제한 처리
    if (IsAlive())
    {
        GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    else if (bLegacyRecovery)
    {
        GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFableCharacter::RespawnAtSafePoint, 3.0f, false);
    }

    // 위치·방향·거리 계산 처리
    if (Controller)
    {
        Controller->SetControlRotation(Rotation);
        Controller->ClientSetRotation(Rotation, true);
    }
    // A shot from the previous encounter must not damage enemies after party travel.

    // 현재 월드와 플레이어 참조 순회
    for (TActorIterator<AFableProjectile> It(GetWorld()); It; ++It)
    {
        if (It->GetOwner() == this)
        {
            It->Destroy();
        }
    }

    ForceNetUpdate();
}

/** 서버가 관리하는 안전 지점으로 캐릭터를 복귀시킨다. */
void AFableCharacter::RespawnAtSafePoint()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !GetWorld() || !bLegacyRecovery || IsAlive())
    {
        return;
    }

    SetCheckpoint(SafeSpawnLocation, SafeSpawnRotation, true);

    // 체력·마나·보호막·피해량 처리
    if (!IsAlive())
    {
        GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFableCharacter::RespawnAtSafePoint, 0.5f, false);
    }
}

/** 마을의 단독 낙하/쓰러짐은 다른 숲 참가자의 전멸 처리와 섞지 않는다. */
bool AFableCharacter::TryRecoverIdleTownAtCheckpoint()
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    if (!HasAuthority() || !GetWorld() || !IsDefeated() || !Profile || !Profile->bProfileLoaded || !Controller ||
        Profile->GetOwner() != Controller || Controller->GetPawn() != this ||
        Controller->GetPlayerState<AFablePlayerState>() != Profile || Profile->ActiveExpeditionId.IsValid() ||
        Profile->GetPublicFieldScopeId().IsValid() || (Profile->IsBackendEnabled() && !Profile->IsBackendReady()) ||
        !FMath::IsFinite(DefeatedServerTime) || DefeatedServerTime < 0 ||
        GetWorld()->GetTimeSeconds() - DefeatedServerTime < 3.0)
    {
        return false;
    }

    // Phase 1은 기존에 확정된 LegacyVillage 체크포인트만 사용한다. 새 후보 지형을 활성화하지 않는다.
    FFableTownLocationContext SafeTown;
    const UFableTownCatalog* Catalog = GetDefault<UFableTownCatalog>();
    if (!Catalog->ResolveLocation(SafeSpawnLocation, SafeTown, true) || SafeTown.TownId != EFableTownId::LegacyVillage)
    {
        return false;
    }

    SetCheckpoint(SafeSpawnLocation, SafeSpawnRotation, true);
    FFableTownLocationContext Arrived;
    return IsAlive() && Catalog->ResolveLocation(GetActorLocation(), Arrived, true) &&
           Arrived.TownId == SafeTown.TownId;
}

/** 복제된 시간 기준으로 보정한 서버 시각 값을 반환한다. */
float AFableCharacter::GetSynchronizedServerTime() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const AGameStateBase* State = World->GetGameState())
        {
            return State->GetServerWorldTimeSeconds();
        }
        return World->GetTimeSeconds();
    }
    return 0.0f;
}

/** 기본 공격의 남은 재사용 대기시간 값을 반환한다. */
float AFableCharacter::GetAttackCooldownRemaining() const
{
    return FMath::Max(0.0f, NextAttackServerTime - GetSynchronizedServerTime());
}

/** 선택한 스킬의 남은 재사용 대기시간 값을 반환한다. */
float AFableCharacter::GetSkillCooldownRemaining(int32 Slot) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Slot < 0 || Slot > 2)
    {
        return 0.0f;
    }

    const float End =
        Slot == 0 ? NextSkillServerTime : (Slot == 1 ? NextSecondSkillServerTime : NextThirdSkillServerTime);

    return FMath::Max(0.0f, End - GetSynchronizedServerTime());
}

/** 회피의 남은 재사용 대기시간 값을 반환한다. */
float AFableCharacter::GetDodgeCooldownRemaining() const
{
    return FMath::Max(0.0f, NextDodgeServerTime - GetSynchronizedServerTime());
}

/** 전투 피드백를 수신한 클라이언트에서 재생한다. */
void AFableCharacter::MulticastCombatCue_Implementation(uint8 Cue)
{
    if (GetNetMode() == NM_DedicatedServer)
    {
        return;
    }
    if (Cue <= 2)
    {
        VisualComboIndex = Cue;
        AttackVisualDuration = Cue == 2 ? 0.42f : 0.34f;
        AttackVisualRemaining = AttackVisualDuration;
    }
    else if (Cue == 3 || (Cue >= 6 && Cue <= 8))
    {
        SkillVisualCue = Cue;
        FableCombatFeedback::PlayCue(this, GetActorLocation(), Cue == 7 ? 2 : 1);
        SkillVisualRemaining = 0.55f;
        AttackVisualRemaining = 0.0f;
    }
    else if (Cue == 4)
    {
        AttackVisualRemaining = 0.0f;
    }
    else if (Cue == 5)
    {
        HurtVisualRemaining = 0.20f;
        FableCombatFeedback::PlayCue(this, GetActorLocation(), 0);
    }
    else if (Cue == 9)
    {
        FableCombatFeedback::PlayCue(this, GetActorLocation(), 2);
    }
}

/** 프레임 경과 시간을 반영해 상태와 화면 표현을 갱신한다. */
void AFableCharacter::Tick(float DeltaSeconds)
{
    // 이동 입력과 속도 제한 처리
    Super::Tick(DeltaSeconds);
    // Use the replicated profile on both server and owning client for movement prediction.
    GetCharacterMovement()->MaxWalkSpeed = GetMovementSpeedLimit();

    // 서버 권한과 실행 위치 처리
    if (HasAuthority())
    {
        // 생존 상태와 부활 진행 처리
        SynchronizeStats();
        TickLifeState();

        // 체력·마나·보호막·피해량 처리
        if (ShieldAmount > 0.0f && GetWorld()->GetTimeSeconds() >= ShieldEndServerTime)
        {
            ShieldAmount = ShieldEndServerTime = 0.0f;
            ForceNetUpdate();
        }

        // 체력·마나·보호막·피해량에 필요한 값 준비
        const AFablePlayerState* Progress = GetPlayerState<AFablePlayerState>();
        const float Regen = Progress ? Progress->GetManaRegenRate() : 8.0f;

        // 체력·마나·보호막·피해량 처리
        if (IsAlive())
        {
            Mana = FMath::Min(GetMaxMana(), Mana + FMath::Max(0.0f, DeltaSeconds) * Regen);
        }

        // 현재 월드와 플레이어 참조 처리
        if (bDodging && GetWorld()->GetTimeSeconds() >= DodgeEndServerTime)
        {
            bDodging = false;
            ForceNetUpdate();
        }
    }

    // 서버 권한과 실행 위치 처리
    if (GetNetMode() != NM_DedicatedServer)
    {
        const AFablePlayerState* Progress = GetPlayerState<AFablePlayerState>();
        if (!bClassAppearanceInitialized || LastVisualClass != GetHeroClass() ||
            (Progress && LastVisualTier != Progress->GetAdvancementTier()))
        {
            ApplyClassAppearance();
        }
        AnimateBody(DeltaSeconds);
    }
}

/** 이동·공격 상태를 캐릭터 부품의 자세에 반영한다. */
void AFableCharacter::AnimateBody(float DeltaSeconds)
{
    // 이동 입력과 속도 제한에 필요한 값 준비
    const EFableClass HeroClass = GetHeroClass();
    BowRoot->SetRelativeRotation(FRotator::ZeroRotator);
    VisualSpeed = FMath::FInterpTo(VisualSpeed, IsAlive() ? GetVelocity().Size2D() : 0.0f, DeltaSeconds, 10.0f);
    WalkPhase += DeltaSeconds * VisualSpeed * 0.025f;
    const float WalkAmount = FMath::Clamp(VisualSpeed / FableCharacterTuning::WalkSpeed, 0.0f, 1.0f);
    const float Phase = FMath::Sin(WalkPhase);
    const float Stride = Phase * 25.0f * WalkAmount;
    const float Bob = FMath::Abs(Phase) * 1.9f * WalkAmount;
    const float Breath = FMath::Sin(GetWorld()->GetTimeSeconds() * 1.7f) * 0.32f * (1.0f - WalkAmount);
    AttackVisualRemaining = FMath::Max(0.0f, AttackVisualRemaining - DeltaSeconds);
    SkillVisualRemaining = FMath::Max(0.0f, SkillVisualRemaining - DeltaSeconds);
    HurtVisualRemaining = FMath::Max(0.0f, HurtVisualRemaining - DeltaSeconds);
    VisualRoot->SetRelativeLocation(FVector(0.0f, 0.0f, IsAlive() ? Bob + Breath : -48.0f));
    VisualRoot->SetRelativeRotation(
        !IsAlive() ? FRotator(0, 0, -75)
                   : (bDodging ? FRotator(-18, 0, 0) : FRotator(-3.0f * WalkAmount, Phase * 1.6f * WalkAmount, 0)));
    LeftLeg->SetRelativeRotation(FRotator(Stride, 0, -0.8f));
    RightLeg->SetRelativeRotation(FRotator(-Stride, 0, 0.8f));
    // Lift the trailing heel while the supporting leg remains nearly straight.
    LeftKnee->SetRelativeRotation(FRotator(-FMath::Max(0.0f, -Phase) * 43.0f * WalkAmount, 0, 0));
    RightKnee->SetRelativeRotation(FRotator(-FMath::Max(0.0f, Phase) * 43.0f * WalkAmount, 0, 0));
    LeftArm->SetRelativeRotation(FRotator(-Stride * 0.60f, 0, -4.0f));
    RightArm->SetRelativeRotation(FRotator(Stride * 0.40f + 4.0f, 0, 4.0f));
    LeftForearm->SetRelativeRotation(FRotator(8.0f + FMath::Max(0.0f, Phase) * 13.0f * WalkAmount, 0, 0));
    RightForearm->SetRelativeRotation(FRotator(5.0f + FMath::Max(0.0f, -Phase) * 10.0f * WalkAmount, 0, 0));
    Cape->SetRelativeRotation(FRotator(-WalkAmount * 10.0f + FMath::Sin(WalkPhase - 0.6f) * WalkAmount * 2.0f,
                                       Phase * WalkAmount * 1.3f, FMath::Sin(WalkPhase * 0.5f) * WalkAmount * 1.6f));

    // 캐릭터 부품과 관절 구성 처리
    if (HeroClass == EFableClass::Mage)
    {
        RightArm->SetRelativeRotation(FRotator(12, 0, 5));
    }

    // 캐릭터 부품과 관절 구성 처리
    if (AttackVisualRemaining > 0.0f)
    {
        // 캐릭터 부품과 관절 구성에 필요한 값 준비
        const float Progress = 1.0f - AttackVisualRemaining / AttackVisualDuration;
        const float Sweep = FMath::Sin(Progress * PI);
        const float Side = VisualComboIndex == 1 ? -1.0f : 1.0f;
        RightArm->SetRelativeRotation(FRotator(100.0f * Sweep, (Progress * 150.0f - 75.0f) * Side, 8.0f));
        RightForearm->SetRelativeRotation(FRotator(12.0f + 24.0f * Sweep, 0, 0));

        // 캐릭터 부품과 관절 구성 처리
        if (HeroClass == EFableClass::Mage)
        {
            RightArm->SetRelativeRotation(FRotator(65.0f * Sweep, 0, -10));
            RightForearm->SetRelativeRotation(FRotator(15, 0, 0));
        }
        else if (HeroClass == EFableClass::Archer)
        {
            LeftArm->SetRelativeRotation(FRotator(70, 0, -3));
            LeftForearm->SetRelativeRotation(FRotator(15, 0, 0));
            RightArm->SetRelativeRotation(FRotator(55, -25, 6));
            RightForearm->SetRelativeRotation(FRotator(70, 0, 0));
            BowRoot->SetRelativeRotation(FRotator(-85, 0, 0));
        }
    }

    // 캐릭터 부품과 관절 구성 처리
    if (SkillVisualRemaining > 0.0f)
    {
        // 캐릭터 부품과 관절 구성 처리
        LeftArm->SetRelativeRotation(FRotator(55, 0, -38));
        RightArm->SetRelativeRotation(FRotator(55, 0, 38));
        LeftForearm->SetRelativeRotation(FRotator(20, 0, 0));
        RightForearm->SetRelativeRotation(FRotator(20, 0, 0));

        // 캐릭터 부품과 관절 구성 처리
        if (HeroClass == EFableClass::Mage)
        {
            LeftArm->SetRelativeRotation(FRotator(45, 0, 20));
            RightArm->SetRelativeRotation(FRotator(65, 0, -10));
        }
        else if (HeroClass == EFableClass::Archer)
        {
            LeftArm->SetRelativeRotation(FRotator(70, 0, -3));
            LeftForearm->SetRelativeRotation(FRotator(15, 0, 0));
            RightArm->SetRelativeRotation(FRotator(55, -25, 6));
            RightForearm->SetRelativeRotation(FRotator(70, 0, 0));
            BowRoot->SetRelativeRotation(FRotator(-85, 0, 0));
        }
    }

    // 직업·스킬·특성 빌드 순회
    for (int32 Index = 0; Index < SkillPetals.Num(); ++Index)
    {
        // 직업·스킬·특성 빌드에 필요한 값 준비
        UStaticMeshComponent* Petal = SkillPetals[Index];
        const bool bShield = GetShieldAmount() > 0.0f;
        const bool bVisible = IsAlive() && (SkillVisualRemaining > 0.0f || bShield);
        Petal->SetVisibility(bVisible);

        // 직업·스킬·특성 빌드 처리
        if (bVisible)
        {
            const float Progress =
                bShield ? FMath::Fmod(GetWorld()->GetTimeSeconds() * 0.35f, 1.0f) : 1.0f - SkillVisualRemaining / 0.55f;

            // 직업·스킬·특성 빌드 처리
            if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Petal->GetMaterial(0)))
            {
                Mat->SetVectorParameterValue(TEXT("Color"),
                                             bShield ? FLinearColor(0.18f, 0.65f, 0.95f)
                                                     : (HeroClass == EFableClass::Mage
                                                            ? (SkillVisualCue == 6 ? FLinearColor(0.3f, 0.8f, 1.0f)
                                                                                   : FLinearColor(0.95f, 0.35f, 0.08f))
                                                            : FLinearColor(0.25f, 0.65f, 0.32f)));
            }

            // 직업·스킬·특성 빌드에 필요한 값 준비
            const float Angle = Index * 2.0f * PI / SkillPetals.Num() + Progress * 0.8f;
            const float Radius = bShield ? 88.0f
                                         : FMath::Lerp(75.0f,
                                                       HeroClass == EFableClass::Mage && SkillVisualCue == 6
                                                           ? 650.0f
                                                           : FableCharacterTuning::SkillRange,
                                                       Progress);
            Petal->SetRelativeLocation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius,
                                               bShield ? 15.0f + 45.0f * FMath::Sin(Angle * 2.0f)
                                                       : -48.0f + FMath::Sin(Progress * PI) * 35.0f));
            // Only transient leaf particles change size; the character parts remain scale 1.
            Petal->SetRelativeScale3D(FVector(1.0f - Progress * 0.65f));
            Petal->SetRelativeRotation(
                FRotator(20.0f * FMath::Sin(Angle), FMath::RadiansToDegrees(Angle), Progress * 110.0f));
        }
    }

    // 이 단계에서 이동·공격 상태를 캐릭터 부품의 자세에 반영한다.
    if (TorsoMaterial)
    {
        const float HurtAmount = HurtVisualRemaining / 0.20f;
        TorsoMaterial->SetVectorParameterValue(
            TEXT("Color"),
            FMath::Lerp(FableCharacterTuning::TunicColor, FLinearColor(0.65f, 0.13f, 0.09f), HurtAmount));
    }

    // 이 단계에서 이동·공격 상태를 캐릭터 부품의 자세에 반영한다.
    if (BladeMaterial)
    {
        BladeMaterial->SetVectorParameterValue(TEXT("Color"), AttackVisualRemaining > 0.0f
                                                                  ? FLinearColor(0.90f, 0.73f, 0.36f)
                                                                  : FableCharacterTuning::BladeColor);
    }
}
