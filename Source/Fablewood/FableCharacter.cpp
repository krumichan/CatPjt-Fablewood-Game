#include "FableCharacter.h"

#include "FableEnemy.h"
#include "FableCombatFeedback.h"
#include "FablePlayerState.h"
#include "FableProjectile.h"
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

AFableCharacter::AFableCharacter()
{
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
    if (!PaintMaterial.Succeeded())
    {
        UE_LOG(LogTemp, Error, TEXT("캐릭터 재질이 없습니다: /Game/Art/Materials/M_FablePaint. 아트 가져오기 도구를 실행하세요."));
    }
    // These meshes are authored at real size around their animation pivot.
    // No engine primitives, axis conversion or compensating component scale is used.
    const auto Part = [this](const TCHAR* Name, USceneComponent* Parent, const TCHAR* Asset,
        const FLinearColor& Color, float Roughness = 0.83f,
        const FVector& Location = FVector::ZeroVector) -> UStaticMeshComponent*
    {
        const FString MeshPath = FString::Printf(TEXT("/Game/Art/Meshes/SM_Hero%s.SM_Hero%s"), Asset, Asset);
        ConstructorHelpers::FObjectFinder<UStaticMesh> MeshAsset(*MeshPath);
        UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Component->SetupAttachment(Parent);
        Component->SetStaticMesh(MeshAsset.Object);
        if (!MeshAsset.Succeeded())
        {
            UE_LOG(LogTemp, Error, TEXT("캐릭터 부품이 없습니다: %s. Tools/Art/create_character_meshes.py 실행 후 OBJ를 가져오세요."), *MeshPath);
        }
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
    SwordParts.Add(Part(TEXT("SwordFuller"), RightForearm, TEXT("SwordFuller"), FLinearColor(0.24f, 0.32f, 0.32f), 0.42f));

    // Functional class markers reuse built-in meshes while art remains out of scope.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerCylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> MarkerSphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    const auto Marker = [this](const TCHAR* Name, USceneComponent* Parent, UStaticMesh* MarkerMesh,
        const FVector& Location, const FVector& Scale, const FLinearColor& Color,
        const FRotator& Rotation = FRotator::ZeroRotator) -> UStaticMeshComponent*
    {
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
    StaffParts.Add(Marker(TEXT("MageStaff"), RightForearm, MarkerCylinder.Object,
        FVector(4, 0, -20), FVector(0.045f, 0.045f, 1.30f), Leather));
    StaffParts.Add(Marker(TEXT("MageFocus"), RightForearm, MarkerSphere.Object,
        FVector(4, 0, 48), FVector(0.16f), FLinearColor(0.06f, 0.40f, 0.75f)));
    BowRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BowPivot"));
    BowRoot->SetupAttachment(LeftForearm);
    BowRoot->SetRelativeLocation(FVector(1, 0, -28));
    const FVector BowPoints[] = {
        FVector(-4, 0, 36), FVector(2, 0, 26), FVector(6, 0, 12), FVector(7, 0, 0),
        FVector(6, 0, -12), FVector(2, 0, -26), FVector(-4, 0, -36)
    };
    for (int32 Index = 0; Index < 6; ++Index)
    {
        const FVector Segment = BowPoints[Index + 1] - BowPoints[Index];
        const FString Name = FString::Printf(TEXT("BowLimb%d"), Index);
        BowParts.Add(Marker(*Name, BowRoot, MarkerCylinder.Object,
            (BowPoints[Index + 1] + BowPoints[Index]) * 0.5f,
            FVector(0.028f, 0.028f, Segment.Size() / 100.0f), Seams,
            FRotationMatrix::MakeFromZ(Segment).Rotator()));
    }
    BowParts.Add(Marker(TEXT("BowString"), BowRoot, MarkerCylinder.Object,
        FVector(-4, 0, 0), FVector(0.006f, 0.006f, 0.72f), Ivory));

    for (int32 Index = 0; Index < 8; ++Index)
    {
        const FString Name = FString::Printf(TEXT("SkillPetal%d"), Index);
        UStaticMeshComponent* Petal = Part(*Name, GetRootComponent(), TEXT("Leaf"), FLinearColor(0.25f, 0.54f, 0.27f));
        Petal->SetVisibility(false);
        Petal->SetCastShadow(false);
        SkillPetals.Add(Petal);
    }
}


void AFableCharacter::BeginPlay()
{
    Super::BeginPlay();
    BaseMaxHealth = MaxHealth;
    FString ValidationRole;
    bLegacyRecovery = FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), ValidationRole)
        || FParse::Param(FCommandLine::Get(), TEXT("FableArtReview"));
    if (HasAuthority())
    {
        SynchronizeStats();
        Health = MaxHealth;
        SafeSpawnLocation = GetActorLocation();
        SafeSpawnRotation = GetActorRotation();
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        for (int32 Index = 0; Index < VisualParts.Num(); ++Index)
        {
            if (UMaterialInstanceDynamic* Instance = VisualParts[Index]->CreateAndSetMaterialInstanceDynamic(0))
            {
                Instance->SetVectorParameterValue(TEXT("Color"), PartColors[Index]);
                Instance->SetScalarParameterValue(TEXT("Roughness"), PartRoughness[Index]);
                if (VisualParts[Index] == Torso) { TorsoMaterial = Instance; }
                if (VisualParts[Index] == SwordBlade) { BladeMaterial = Instance; }
                const FName PartName = VisualParts[Index]->GetFName();
                if (VisualParts[Index] == Cape || PartName == TEXT("ShoulderMantle") || PartName == TEXT("WaistCloth"))
                {
                    ClassClothMaterials.Add(Instance);
                }
            }
        }
    }
    ApplyClassAppearance();
    OnRep_Health();
}

void AFableCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (HasAuthority()) { CancelReviveInternal(TEXT("동료 연결 종료")); }
    GetWorldTimerManager().ClearTimer(RespawnTimer);
    Super::EndPlay(EndPlayReason);
}

void AFableCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
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

void AFableCharacter::MoveForward(float Value)
{
    if (IsAlive() && Controller && FMath::IsFinite(Value))
    {
        const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), FMath::Clamp(Value, -1.0f, 1.0f));
    }
}

void AFableCharacter::MoveRight(float Value)
{
    if (IsAlive() && Controller && FMath::IsFinite(Value))
    {
        const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
        AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), FMath::Clamp(Value, -1.0f, 1.0f));
    }
}

void AFableCharacter::Turn(float Value)
{
    if (FMath::IsFinite(Value)) { AddControllerYawInput(FMath::Clamp(Value, -20.0f, 20.0f)); }
}

void AFableCharacter::LookUp(float Value)
{
    if (FMath::IsFinite(Value)) { AddControllerPitchInput(FMath::Clamp(Value, -20.0f, 20.0f)); }
}

void AFableCharacter::StartJump()
{
    if (!IsAlive() || (Controller && Controller->IsMoveInputIgnored())) { return; }
    CancelRevive();
    if (!bDodging) { Jump(); }
}

void AFableCharacter::FinishJump() { StopJumping(); }
void AFableCharacter::RequestAttack() { RequestAction(0); }
void AFableCharacter::RequestSkill() { RequestActiveSkill(0); }
void AFableCharacter::RequestSecondSkill() { RequestActiveSkill(1); }
void AFableCharacter::RequestThirdSkill() { RequestActiveSkill(2); }
void AFableCharacter::RequestActiveSkill(int32 Slot)
{
    if (Slot >= 0 && Slot <= 2) RequestAction(Slot == 0 ? 1 : uint8(Slot + 2));
}
void AFableCharacter::RequestDodge() { RequestAction(2); }

void AFableCharacter::RequestAction(uint8 Action)
{
    if (!IsLocallyControlled() || !IsAlive() || !GetWorld() || (Controller && Controller->IsMoveInputIgnored())) { return; }
    if (ReviveTarget) { CancelRevive(); }
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now < NextLocalRequestTime) { return; }
    NextLocalRequestTime = Now + 0.06f;
    if (Action != 2 && Controller && FMath::IsFinite(Controller->GetControlRotation().Yaw))
    {
        // Turn the local presentation toward the camera; the server derives the same facing.
        SetActorRotation(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
    }
    ServerRequestAction(Action);
}

void AFableCharacter::ServerRequestAction_Implementation(uint8 Action)
{
    if (const AFablePlayerState* BackendState=GetPlayerState<AFablePlayerState>(); BackendState && BackendState->IsBackendEnabled() && !BackendState->IsBackendReady()) return;
    if (!HasAuthority() || !Controller || !IsAlive() || Action > 4 || !GetWorld()) { return; }
    // An attack/dodge attempt ends the channel even when that action is cooling down.
    CancelReviveInternal(TEXT("전투 행동"));
    const float Now = GetWorld()->GetTimeSeconds();
    if (!FMath::IsFinite(Now) || Now - LastRequestServerTime < 0.05f) { return; }
    LastRequestServerTime = Now;
    if (bDodging) { return; }
    if (Action == 2)
    {
        if (Now >= NextDodgeServerTime && GetCharacterMovement()->IsMovingOnGround()) { PerformDodge(); }
        return;
    }
    if (Now < ActionLockEndServerTime) { return; }
    if (Action == 0 && Now >= NextAttackServerTime) { PerformAttack(false); }
    if (Action == 1 || Action == 3 || Action == 4) { PerformActiveSkill(Action == 1 ? 0 : int32(Action) - 2); }
}

void AFableCharacter::PerformActiveSkill(int32 Slot)
{
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !State || !State->bProfileLoaded || Slot < 0 || Slot > 2) return;
    if (State->IsBackendEnabled() && !State->IsBackendReady()) return;
    SynchronizeStats();
    if (!State->IsSkillUnlocked(Slot)) { State->SetNotice(TEXT("아직 잠긴 스킬입니다. 로완에게 전직 조건을 확인하세요.")); return; }
    if (GetSkillCooldownRemaining(Slot) > 0.0f) { State->SetNotice(TEXT("스킬을 다시 사용할 때까지 기다리세요.")); return; }
    const float Cost = GetSkillManaCost(Slot);
    if (Mana < Cost) { State->SetNotice(TEXT("마나가 부족합니다. 회복을 기다리거나 M으로 물약을 사용하세요.")); return; }
    if (Slot == 0) { PerformAttack(true); return; }
    const FFableTrialCastContext TrialContext = State->CreateTrialCastContext(Slot);
    Mana = FMath::Max(0.0f, Mana - Cost);
    const float Now = GetWorld()->GetTimeSeconds();
    CombatBuildEndServerTime = Now + 5.0f;
    (Slot == 1 ? NextSecondSkillServerTime : NextThirdSkillServerTime) = Now + GetSkillCooldownDuration(Slot);
    ActionLockEndServerTime = Now + 0.55f;
    if (SkillCastCounts.IsValidIndex(Slot)) ++SkillCastCounts[Slot];
    if (Controller && FMath::IsFinite(Controller->GetControlRotation().Yaw))
        SetActorRotation(FRotator(0, Controller->GetControlRotation().Yaw, 0));
    const EFableClass Class = GetHeroClass();
    const int32 Rank = FMath::Clamp(State->GetSkillRank(Slot), 1, 3);
    const float Power = State->GetAttackPower() * (1.0f + 0.15f * (Rank - 1));
    if (Slot == 2 && Class != EFableClass::Archer)
    {
        ShieldAmount = GetSkillShieldCapacity(Slot);
        ShieldEndServerTime = Now + GetSkillShieldDuration(Slot);
        if (Class == EFableClass::Warrior) ShareGuardianShield(GetCombatTalentRank(EFableTalentPath::Second));
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
        LaunchCharacter(Direction * 950.0f + FVector(0,0,65), true, true);
        if (!IsLocallyControlled()) ClientConfirmSkillMove(FVector_NetQuantizeNormal(Direction), 950.0f);
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
            LaunchCharacter(Direction * RetreatSpeed + FVector(0,0,65), true, true);
            if (!IsLocallyControlled()) ClientConfirmSkillMove(FVector_NetQuantizeNormal(Direction), RetreatSpeed);
        }
        MulticastCombatCue(Slot == 2 ? 8 : 6);
    }
    ForceNetUpdate();
}

void AFableCharacter::ApplySkillArea(float Damage, float Range, bool bFrontal, bool bSlow, const FFableTrialCastContext& TrialContext)
{
    const FVector Origin = GetActorLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableAdvancedSkill), false, this);
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        AFableEnemy* Enemy = *It;
        const FVector Delta = Enemy->GetActorLocation() - Origin;
        if (!Enemy->IsAlive() || Delta.SizeSquared2D() > FMath::Square(Range) || FMath::Abs(Delta.Z) > 180.0f) continue;
        if (bFrontal && !Delta.IsNearlyZero() && FVector::DotProduct(Delta.GetSafeNormal2D(), GetActorForwardVector()) < 0.4f) continue;
        FHitResult Hit;
        if (GetWorld()->LineTraceSingleByChannel(Hit, Origin + FVector(0,0,24), Enemy->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != Enemy) continue;
        const float PreviousHealth = Enemy->GetHealth();
        Enemy->ReceiveHit(Damage, Controller);
        const int32 FrostRank = GetHeroClass() == EFableClass::Mage ? GetCombatTalentRank(EFableTalentPath::Second) : 0;
        const bool bAppliedSlow = bSlow && Enemy->IsAlive()
            && Enemy->ApplySlow(0.65f - 0.05f * FrostRank, 4.0f + 0.5f * FrostRank);
        if (bAppliedSlow && FrostRank > 0 && Enemy->GetHealth() < PreviousHealth) ++TalentSlowHits;
        if (Enemy->GetHealth() < PreviousHealth)
            if (AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>())
                Profile->RecordTrialSkillHit(TrialContext, bAppliedSlow);
    }
}

void AFableCharacter::ClientConfirmSkillMove_Implementation(FVector_NetQuantizeNormal Direction, float Speed)
{
    if (!HasAuthority() && IsLocallyControlled() && IsAlive() && FMath::IsFinite(Speed))
        LaunchCharacter(FVector(Direction).GetSafeNormal2D() * FMath::Clamp(Speed, 0.0f, 950.0f) + FVector(0,0,65), true, true);
}

void AFableCharacter::PerformAttack(bool bSkill)
{
    if (const AFablePlayerState* BackendState=GetPlayerState<AFablePlayerState>(); BackendState && BackendState->IsBackendEnabled() && !BackendState->IsBackendReady()) return;
    if (!HasAuthority() || !GetWorld() || !IsAlive()) { return; }
    const EFableClass HeroClass = GetHeroClass();
    const float ManaCost = bSkill ? GetSkillManaCost() : 0.0f;
    if (Mana < ManaCost) { return; }
    Mana = FMath::Max(0.0f, Mana - ManaCost);
    if (Controller && FMath::IsFinite(Controller->GetControlRotation().Yaw))
    {
        SetActorRotation(FRotator(0.0f, Controller->GetControlRotation().Yaw, 0.0f));
    }
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const FFableTrialCastContext TrialContext = bSkill && State ? State->CreateTrialCastContext(0) : FFableTrialCastContext();
    const float RawPower = State ? State->GetAttackPower() : 24.0f;
    const float RankScale = bSkill && State ? 1.0f + 0.15f * (FMath::Clamp(State->GetSkillRank(0), 1, 3) - 1) : 1.0f;
    const float AttackPower = (FMath::IsFinite(RawPower) ? FMath::Clamp(RawPower, 1.0f, 10000.0f) : 24.0f) * RankScale;
    const float Now = GetWorld()->GetTimeSeconds();
    CombatBuildEndServerTime = Now + 5.0f;
    if (bSkill)
    {
        NextSkillServerTime = Now + GetSkillCooldownDuration(0);
        if (SkillCastCounts.IsValidIndex(0)) ++SkillCastCounts[0];
        ActionLockEndServerTime = Now + 0.55f;
        MulticastCombatCue(3);
    }
    else
    {
        ComboIndex = Now - LastAttackServerTime > 1.4f ? 0 : (ComboIndex + 1) % 3;
        LastAttackServerTime = Now;
        const float Cooldown = HeroClass == EFableClass::Mage ? 0.72f
            : (HeroClass == EFableClass::Archer ? 0.58f : FableCharacterTuning::AttackCooldown);
        NextAttackServerTime = Now + Cooldown;
        ActionLockEndServerTime = Now + 0.30f;
        MulticastCombatCue(ComboIndex);
    }
    if (HeroClass != EFableClass::Warrior)
    {
        PerformRangedAttack(HeroClass, bSkill, AttackPower, 0, TrialContext);
        ForceNetUpdate();
        return;
    }

    const float Range = bSkill ? GetSkillRange(0) : FableCharacterTuning::MeleeRange;
    // A starter sword still deals exactly 24 / 24 / 34, or 42 for the radial skill.
    const float Damage = AttackPower * (bSkill ? GetSkillDamageMultiplier(0) : (ComboIndex == 2 ? 34.0f / 24.0f : 1.0f));
    const FVector Origin = GetActorLocation();
    const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
    TSet<AFableEnemy*> HitEnemies;
    FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(FableMeleeVisibility), false, this);
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        AFableEnemy* Enemy = *It;
        if (!IsValid(Enemy) || !Enemy->IsAlive() || HitEnemies.Contains(Enemy)) { continue; }
        const FVector Delta = Enemy->GetActorLocation() - Origin;
        if (Delta.SizeSquared2D() > FMath::Square(Range) || FMath::Abs(Delta.Z) > 170.0f) { continue; }
        if (!bSkill && !Delta.IsNearlyZero() && FVector::DotProduct(Forward, Delta.GetSafeNormal2D()) < 0.25f) { continue; }
        FHitResult Obstruction;
        const FVector EyeHeight(0.0f, 0.0f, 24.0f);
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Obstruction,
            Origin + EyeHeight, Enemy->GetActorLocation() + EyeHeight, ECC_Visibility, TraceParams);
        if (bBlocked && Obstruction.GetActor() != Enemy) { continue; }
        HitEnemies.Add(Enemy);
        const float PreviousHealth = Enemy->GetHealth();
        Enemy->ReceiveHit(Damage, Controller);
        if (State && Enemy->GetHealth() < PreviousHealth) State->RecordTrialSkillHit(TrialContext);
    }
    ForceNetUpdate();
}

void AFableCharacter::PerformRangedAttack(EFableClass HeroClass, bool bSkill, float AttackPower, int32 SkillSlot, const FFableTrialCastContext& TrialContext)
{
    if (!HasAuthority() || !GetWorld() || !Controller) { return; }
    const bool bUseArrow = HeroClass == EFableClass::Archer;
    const float MaximumRange = bUseArrow ? 2200.0f + 200.0f * GetCombatTalentRank(EFableTalentPath::First) : 1800.0f;
    // Start inside our own capsule (ignored by the shot), not beyond a nearby wall.
    const FVector Origin = GetActorLocation() + FVector(0, 0, 24);
    const FRotator ControlAim = Controller->GetControlRotation().GetNormalized();
    const float Pitch = FMath::IsFinite(ControlAim.Pitch) ? FMath::Clamp(ControlAim.Pitch, -35.0f, 35.0f) : 0.0f;
    FVector Direction = FRotator(Pitch, GetActorRotation().Yaw, 0).Vector();
    float BestScore = -1.0f;
    FCollisionQueryParams AimParams(SCENE_QUERY_STAT(FableProjectileAim), false, this);
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It) { AimParams.AddIgnoredActor(*It); }
    // A small server-side aim assist makes the third-person camera usable.
    // It supplies direction only; the moving projectile still performs every hit test.
    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        AFableEnemy* Enemy = *It;
        if (!IsValid(Enemy) || !Enemy->IsAlive()) { continue; }
        const FVector ToTarget = Enemy->GetActorLocation() - Origin;
        const float Distance = ToTarget.Size();
        if (Distance > MaximumRange || Distance < KINDA_SMALL_NUMBER) { continue; }
        const FVector Candidate = ToTarget / Distance;
        const float Alignment = FVector::DotProduct(FRotator(Pitch, GetActorRotation().Yaw, 0).Vector(), Candidate);
        if (Alignment < 0.94f) { continue; }
        const float Score = Alignment - Distance * 0.00002f;
        if (Score <= BestScore) { continue; }
        FHitResult Obstruction;
        const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Obstruction, Origin,
            Enemy->GetActorLocation(), ECC_Visibility, AimParams);
        if (!bBlocked || Obstruction.GetActor() == Enemy)
        {
            Direction = Candidate;
            BestScore = Score;
        }
    }
    const int32 Count = bUseArrow && bSkill && SkillSlot != 1 ? 3 : 1;
    const float ShotDamage = AttackPower * (bSkill ? GetSkillDamageMultiplier(SkillSlot) : 1.0f);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const float SpreadAngle = bUseArrow && SkillSlot == 0 ? 11.0f - 3.0f * GetCombatTalentRank(EFableTalentPath::First) : 11.0f;
        const float Spread = Count == 3 ? (Index - 1) * SpreadAngle : 0.0f;
        const FVector ShotDirection = Direction.RotateAngleAxis(Spread, FVector::UpVector);
        const FTransform SpawnTransform(ShotDirection.Rotation(), Origin);
        AFableProjectile* Shot = GetWorld()->SpawnActorDeferred<AFableProjectile>(
            AFableProjectile::StaticClass(), SpawnTransform, this, this, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Shot)
        {
            Shot->InitializeShot(ShotDirection, ShotDamage, Controller, bUseArrow,
                !bUseArrow && bSkill ? GetSkillRange(0) : 0.0f, bUseArrow ? 2100.0f : 1250.0f, MaximumRange,
                bUseArrow && bSkill && SkillSlot == 1 ? 2 : 0);
            Shot->SetTrialCastContext(TrialContext);
            const int32 FireRank = !bUseArrow && bSkill ? GetCombatTalentRank(EFableTalentPath::First) : 0;
            if (FireRank > 0) Shot->SetBurnPayload(AttackPower * 0.10f * FireRank, 3);
            Shot->FinishSpawning(SpawnTransform);
        }
    }
}

EFableClass AFableCharacter::GetHeroClass() const
{
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    if (State && (State->HeroClass == EFableClass::Mage || State->HeroClass == EFableClass::Archer))
    {
        return State->HeroClass;
    }
    return EFableClass::Warrior;
}

float AFableCharacter::GetSkillManaCost(int32 Slot) const
{
    if (Slot < 0 || Slot > 2 || GetHeroClass() == EFableClass::Warrior) return 0.0f;
    if (GetHeroClass() == EFableClass::Mage) return Slot == 0 ? 30.0f : 35.0f;
    return Slot == 1 ? 30.0f : 25.0f;
}
float AFableCharacter::GetSkillCooldownDuration(int32 Slot) const
{
    if (Slot < 0 || Slot > 2) return 0.0f;
    if (Slot == 0) return GetHeroClass() == EFableClass::Mage ? 6.0f : FableCharacterTuning::SkillCooldown;
    if (Slot == 1)
        return GetHeroClass() == EFableClass::Mage ? 9.0f
            : (GetHeroClass() == EFableClass::Warrior ? 8.0f - GetCombatTalentRank(EFableTalentPath::First) : 8.0f);
    return GetHeroClass() == EFableClass::Archer ? 10.0f - GetCombatTalentRank(EFableTalentPath::Second) : 14.0f;
}
float AFableCharacter::GetShieldAmount() const
{
    return IsAlive() && GetShieldTimeRemaining() > 0 ? ShieldAmount : 0.0f;
}
float AFableCharacter::GetShieldTimeRemaining() const
{
    return ShieldAmount > 0 ? FMath::Max(0.0f, ShieldEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}
int32 AFableCharacter::GetSkillCastCount(int32 Slot) const
{
    return SkillCastCounts.IsValidIndex(Slot) ? SkillCastCounts[Slot] : 0;
}

int32 AFableCharacter::GetCombatTalentRank(EFableTalentPath Path) const
{
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    return Profile ? FMath::Clamp(Profile->GetTalentRank(Path), 0, 2) : 0;
}

float AFableCharacter::GetMovementSpeedLimit() const
{
    const int32 Rank = GetHeroClass() == EFableClass::Archer ? GetCombatTalentRank(EFableTalentPath::Second) : 0;
    return FableCharacterTuning::WalkSpeed * (1.0f + 0.05f * Rank);
}

float AFableCharacter::GetDodgeCooldownDuration() const
{
    const int32 Rank = GetHeroClass() == EFableClass::Archer ? GetCombatTalentRank(EFableTalentPath::Second) : 0;
    return FableCharacterTuning::DodgeCooldown - 0.15f * Rank;
}

float AFableCharacter::GetSkillDamageMultiplier(int32 Slot) const
{
    if (Slot < 0 || Slot > 2) return 0.0f;
    const int32 First = GetCombatTalentRank(EFableTalentPath::First);
    if (GetHeroClass() == EFableClass::Warrior)
        return Slot == 0 ? (42.0f / 24.0f) * (1.0f + 0.10f * First)
            : (Slot == 1 ? 1.45f * (1.0f + 0.15f * First) : 0.0f);
    if (GetHeroClass() == EFableClass::Mage)
        return Slot == 0 ? 1.75f * (1.0f + 0.10f * First) : (Slot == 1 ? 0.9f : 0.0f);
    return Slot == 1 ? 1.6f * (1.0f + 0.15f * First) : 0.85f;
}

float AFableCharacter::GetSkillRange(int32 Slot) const
{
    if (Slot < 0 || Slot > 2) return 0.0f;
    if (GetHeroClass() == EFableClass::Archer) return 2200.0f + 200.0f * GetCombatTalentRank(EFableTalentPath::First);
    if (GetHeroClass() == EFableClass::Mage)
        return Slot == 0 ? 300.0f + 30.0f * GetCombatTalentRank(EFableTalentPath::First) : (Slot == 1 ? 650.0f : 0.0f);
    return Slot == 0 ? FableCharacterTuning::SkillRange + 40.0f * GetCombatTalentRank(EFableTalentPath::Second)
        : (Slot == 1 ? 550.0f + 50.0f * GetCombatTalentRank(EFableTalentPath::First) : 0.0f);
}

float AFableCharacter::GetSkillShieldCapacity(int32 Slot) const
{
    if (Slot != 2 || GetHeroClass() == EFableClass::Archer) return 0.0f;
    const AFablePlayerState* Profile = GetPlayerState<AFablePlayerState>();
    const int32 Rank = Profile ? FMath::Clamp(Profile->GetSkillRank(Slot), 1, 3) : 1;
    const int32 Guardian = GetCombatTalentRank(EFableTalentPath::Second);
    return GetHeroClass() == EFableClass::Mage ? 60.0f + 20.0f * (Rank - 1) + 15.0f * Guardian
        : 45.0f + 15.0f * (Rank - 1) + 20.0f * Guardian;
}

float AFableCharacter::GetSkillShieldDuration(int32 Slot) const
{
    return Slot == 2 && GetHeroClass() != EFableClass::Archer ? 6.0f + GetCombatTalentRank(EFableTalentPath::Second) : 0.0f;
}

float AFableCharacter::GetBuildChangeCooldownRemaining() const
{
    if (!GetWorld()) return 0.0f;
    float Remaining = FMath::Max(0.0f, CombatBuildEndServerTime - GetSynchronizedServerTime());
    Remaining = FMath::Max(Remaining, GetAttackCooldownRemaining());
    for (int32 Slot = 0; Slot < 3; ++Slot) Remaining = FMath::Max(Remaining, GetSkillCooldownRemaining(Slot));
    Remaining = FMath::Max(Remaining, GetDodgeCooldownRemaining());
    Remaining = FMath::Max(Remaining, GetShieldTimeRemaining());
    return Remaining;
}

void AFableCharacter::RecordTalentBurnDamage(float ActualDamage)
{
    if (!HasAuthority() || !IsAlive() || !GetWorld() || !FMath::IsFinite(ActualDamage) || ActualDamage <= 0.0f) return;
    TalentBurnDamageDealt += ActualDamage;
    CombatBuildEndServerTime = GetWorld()->GetTimeSeconds() + 5.0f;
    ForceNetUpdate();
}

void AFableCharacter::ShareGuardianShield(int32 Rank)
{
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !Controller || Rank <= 0) return;
    const FVector Origin = GetActorLocation();
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableGuardianShare), false, this);
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
    const float Now = GetWorld()->GetTimeSeconds();
    for (TActorIterator<AFableCharacter> It(GetWorld()); It; ++It)
    {
        AFableCharacter* Ally = *It;
        if (Ally == this || !Ally->IsAlive() || !Ally->Controller || Ally->Controller == Controller
            || !Ally->GetPlayerState<AFablePlayerState>()) continue;
        const FVector Delta = Ally->GetActorLocation() - Origin;
        if (Delta.SizeSquared2D() > FMath::Square(600.0f) || FMath::Abs(Delta.Z) > 180.0f) continue;
        bool bSameRegion = false;
        for (EFableRegion Region : { EFableRegion::Village, EFableRegion::RootRuins, EFableRegion::Moonwell })
            bSameRegion |= FableRPG::IsInRegion(Region, Origin) && FableRPG::IsInRegion(Region, Ally->GetActorLocation());
        if (!bSameRegion) continue;
        FHitResult Hit;
        if (GetWorld()->LineTraceSingleByChannel(Hit, Origin + FVector(0,0,24), Ally->GetActorLocation() + FVector(0,0,24), ECC_Visibility, Params)) continue;
        const float OldAmount = Ally->GetShieldAmount();
        const float OldEnd = OldAmount > 0.0f ? Ally->ShieldEndServerTime : 0.0f;
        const float NewAmount = FMath::Max(OldAmount, 20.0f * Rank);
        const float NewEnd = FMath::Max(OldEnd, Now + 6.0f + Rank);
        if (FMath::IsNearlyEqual(OldAmount, NewAmount) && FMath::IsNearlyEqual(OldEnd, NewEnd)) continue;
        Ally->ShieldAmount = NewAmount;
        Ally->ShieldEndServerTime = NewEnd;
        Ally->CombatBuildEndServerTime = FMath::Max(Ally->CombatBuildEndServerTime, Now + 5.0f);
        Ally->MulticastCombatCue(7);
        Ally->ForceNetUpdate();
        ++TalentAllyShieldsGranted;
    }
}

void AFableCharacter::SynchronizeStats()
{
    if (!HasAuthority()) { return; }
    if (LastServerClass != GetHeroClass())
    {
        LastServerClass = GetHeroClass(); ShieldAmount = ShieldEndServerTime = 0.0f;
        // Keep slot cooldowns when switching class so swapping cannot reset skills.
        ForceNetUpdate();
    }
    const AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const float Bonus = State ? State->GetMaxHealthBonus() : 0.0f;
    const float DesiredMaximum = BaseMaxHealth + (FMath::IsFinite(Bonus) ? FMath::Clamp(Bonus, 0.0f, 10000.0f) : 0.0f);
    if (!FMath::IsNearlyEqual(MaxHealth, DesiredMaximum))
    {
        const float PreviousMaximum = MaxHealth;
        MaxHealth = DesiredMaximum;
        // Preserve health percentage so equipping armor repeatedly cannot generate healing.
        Health = IsAlive() ? FMath::Clamp((Health / FMath::Max(1.0f, PreviousMaximum)) * MaxHealth, 0.0f, MaxHealth) : 0.0f;
        ForceNetUpdate();
    }
}

void AFableCharacter::ApplyClassAppearance()
{
    if (GetNetMode() == NM_DedicatedServer) { return; }
    const EFableClass HeroClass = GetHeroClass();
    const AFablePlayerState* Progress = GetPlayerState<AFablePlayerState>();
    const int32 Tier = Progress ? Progress->GetAdvancementTier() : 1;
    if (bClassAppearanceInitialized && LastVisualClass == HeroClass && LastVisualTier > 0 && Tier > LastVisualTier)
    {
        SkillVisualRemaining = 0.55f; SkillVisualCue = 7;
        FableCombatFeedback::PlayCue(this, GetActorLocation(), 3);
    }
    LastVisualTier = Tier;
    LastVisualClass = HeroClass;
    bClassAppearanceInitialized = true;
    for (UStaticMeshComponent* Part : SwordParts) { Part->SetVisibility(HeroClass == EFableClass::Warrior); }
    for (UStaticMeshComponent* Part : StaffParts) { Part->SetVisibility(HeroClass == EFableClass::Mage); }
    for (UStaticMeshComponent* Part : BowParts) { Part->SetVisibility(HeroClass == EFableClass::Archer); }
    const FLinearColor Tint = HeroClass == EFableClass::Mage ? FLinearColor(0.16f, 0.065f, 0.30f)
        : (HeroClass == EFableClass::Archer ? FLinearColor(0.075f, 0.24f, 0.055f) : FLinearColor(0.026f, 0.20f, 0.19f));
    for (UMaterialInstanceDynamic* Material : ClassClothMaterials)
    {
        if (Material) { Material->SetVectorParameterValue(TEXT("Color"), Tint * (1.0f + 0.16f * (Tier - 1))); }
    }
}

void AFableCharacter::PerformDodge()
{
    const float Now = GetWorld()->GetTimeSeconds();
    FVector Direction = GetVelocity().GetSafeNormal2D();
    if (Direction.IsNearlyZero()) { Direction = GetActorForwardVector().GetSafeNormal2D(); }
    bDodging = true;
    DodgeEndServerTime = Now + FableCharacterTuning::DodgeDuration;
    NextDodgeServerTime = Now + GetDodgeCooldownDuration();
    LaunchCharacter(Direction * FableCharacterTuning::DodgeSpeed + FVector(0, 0, 70.0f), true, true);
    // The server sets velocity and invulnerability. This only mirrors its launch for a remote owner;
    // if the cue is lost, CharacterMovement still corrects the owner to the server's movement.
    if (!IsLocallyControlled()) { ClientConfirmDodge(FVector_NetQuantizeNormal(Direction)); }
    MulticastCombatCue(4);
    ForceNetUpdate();
}

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

bool AFableCharacter::RestoreResource(bool bMana, float Amount)
{
    if (!HasAuthority() || !IsAlive() || !FMath::IsFinite(Amount) || Amount <= 0.0f) return false;
    SynchronizeStats();
    float& Resource = bMana ? Mana : Health;
    const float Maximum = bMana ? GetMaxMana() : MaxHealth;
    if (Resource >= Maximum) return false;
    const float Restored = FMath::Min(Amount, Maximum - Resource);
    if (Restored <= 0.0f) return false;
    Resource += Restored;
    ForceNetUpdate();
    return true;
}

void AFableCharacter::ReceiveHit(float Damage)
{
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !FMath::IsFinite(Damage) || Damage <= 0.0f) { return; }
    CombatBuildEndServerTime = GetWorld()->GetTimeSeconds() + 5.0f;
    if (bDodging) { ForceNetUpdate(); return; }
    if (GetReviveProtectionRemaining() > 0.0f)
    {
        ++ReviveProtectionBlockedHits;
        ForceNetUpdate();
        return;
    }
    CancelReviveInternal(TEXT("피해 받음"));
    AFablePlayerState* State = GetPlayerState<AFablePlayerState>();
    const float RawDefense = State ? State->GetDefense() : 0.0f;
    const float Defense = FMath::IsFinite(RawDefense) ? FMath::Clamp(RawDefense, 0.0f, 1000.0f) : 0.0f;
    // Diminishing returns avoid complete immunity while making every armor point useful.
    float EffectiveDamage = Damage * (100.0f / (100.0f + Defense));
    if (GetShieldAmount() > 0.0f)
    {
        const float Absorbed = FMath::Min(ShieldAmount, EffectiveDamage);
        ShieldAmount -= Absorbed; EffectiveDamage -= Absorbed; TotalShieldAbsorbed += Absorbed;
        MulticastCombatCue(9);
    }
    const float PreviousHealth = Health;
    Health = FMath::Max(0.0f, Health - FMath::Min(EffectiveDamage, MaxHealth));
    if (State && Health < PreviousHealth)
    {
        // Shield-only absorption, dodges and protection never wear armor.
        // PlayerState throttles wear per equipment slot across all damage sources.
        State->WearEquipment(EFableEquipmentSlot::Armor, 1);
        SynchronizeStats();
    }
    MulticastCombatCue(5);
    if (Health <= 0.0f) { EnterDowned(); }
    else { OnRep_Health(); }
    ForceNetUpdate();
}

float AFableCharacter::GetDownedTimeRemaining() const
{
    return IsDowned() ? FMath::Max(0.0f, DownedEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

float AFableCharacter::GetReviveProtectionRemaining() const
{
    return IsAlive() ? FMath::Max(0.0f, ReviveProtectionEndServerTime - GetSynchronizedServerTime()) : 0.0f;
}

float AFableCharacter::GetReviveProgress() const
{
    if (ReviveStartServerTime < 0.0f) { return 0.0f; }
    const bool bHelping = IsAlive() && IsValid(ReviveTarget.Get()) && ReviveTarget->IsDowned();
    const bool bReceiving = IsDowned() && IsValid(Reviver.Get()) && Reviver->IsAlive();
    if (!bHelping && !bReceiving) { return 0.0f; }
    return FMath::Clamp((GetSynchronizedServerTime() - ReviveStartServerTime) / FableCharacterTuning::ReviveDuration, 0.0f, 1.0f);
}

bool AFableCharacter::CanMaintainRevive(const AFableCharacter* Target) const
{
    if (!HasAuthority() || !GetWorld() || !IsAlive() || !IsValid(Target) || Target == this
        || Target->GetWorld() != GetWorld() || !Target->HasAuthority() || !Target->IsDowned() || bLegacyRecovery || Target->bLegacyRecovery
        || !Controller || !Target->Controller || Controller == Target->Controller
        || !Controller->IsPlayerController() || !Target->Controller->IsPlayerController()
        || Controller->GetPawn() != this || Target->Controller->GetPawn() != Target
        || !GetPlayerState<AFablePlayerState>() || !Target->GetPlayerState<AFablePlayerState>()
        || GetPlayerState<AFablePlayerState>() == Target->GetPlayerState<AFablePlayerState>()
        || Controller->IsMoveInputIgnored() || bDodging || bPressedJump
        || !GetCharacterMovement()->IsMovingOnGround() || GetVelocity().ContainsNaN()
        || GetVelocity().SizeSquared() > FMath::Square(15.0f)
        || GetWorld()->GetTimeSeconds() >= Target->DownedEndServerTime
        || GetWorld()->GetTimeSeconds() < ActionLockEndServerTime
        || (Target->Reviver && Target->Reviver.Get() != this)) { return false; }
    const FVector Origin = GetActorLocation();
    const FVector Destination = Target->GetActorLocation();
    const FVector Delta = Destination - Origin;
    if (Origin.ContainsNaN() || Destination.ContainsNaN()
        || Delta.SizeSquared2D() > FMath::Square(FableCharacterTuning::ReviveDistance)
        || FMath::Abs(Delta.Z) > FableCharacterTuning::ReviveHeight) { return false; }
    bool bSameRegion = false;
    for (const EFableRegion Region : {EFableRegion::Village, EFableRegion::RootRuins, EFableRegion::Moonwell})
    {
        if (FableRPG::IsInRegion(Region, Origin) && FableRPG::IsInRegion(Region, Destination))
        { bSameRegion = true; break; }
    }
    if (!bSameRegion) { return false; }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(FableReviveVisibility), false, this);
    Params.AddIgnoredActor(Target);
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit, Origin + FVector(0,0,24), Destination + FVector(0,0,24), ECC_Visibility, Params);
}

bool AFableCharacter::BeginRevive(AFableCharacter* Target)
{
    if (!HasAuthority() || !GetWorld()) { return false; }
    if (ReviveTarget)
    {
        // Repeated E/start packets must not restart or speed up an existing channel.
        if (ReviveTarget.Get() != Target) { return false; }
        if (CanMaintainRevive(Target)) { return true; }
        CancelReviveInternal(TEXT("부활 조건 변경"));
        return false;
    }
    const float Now = GetWorld()->GetTimeSeconds();
    if (Now - LastReviveRequestServerTime < 0.15f) { return false; }
    LastReviveRequestServerTime = Now;
    if (!CanMaintainRevive(Target) || Reviver || Target->ReviveTarget) { return false; }
    ReviveTarget = Target;
    Target->Reviver = this;
    ReviveStartServerTime = Target->ReviveStartServerTime = Now;
    ReviveStartLocation = GetActorLocation();
    const FVector Facing = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
    if (!Facing.IsNearlyZero()) { SetActorRotation(Facing.Rotation()); }
    ForceNetUpdate();
    Target->ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("동료 부활 시작: %s -> %s (3초)"), *GetName(), *Target->GetName());
    return true;
}

void AFableCharacter::CancelRevive()
{
    if (HasAuthority()) { CancelReviveInternal(TEXT("입력 해제 또는 화면 전환")); }
    else if (IsLocallyControlled()) { ServerCancelRevive(); }
}

void AFableCharacter::ServerCancelRevive_Implementation()
{
    // A delayed jump/release from before being downed cannot cancel help now being received.
    if (ReviveTarget) { CancelReviveInternal(TEXT("입력 해제 또는 다른 행동")); }
}

void AFableCharacter::CancelReviveInternal(const TCHAR* Reason)
{
    if (!HasAuthority()) { return; }
    AFableCharacter* Target = ReviveTarget.Get();
    AFableCharacter* Helper = Reviver.Get();
    const bool bHadChannel = Target || Helper || ReviveStartServerTime >= 0.0f;
    ReviveTarget = nullptr;
    Reviver = nullptr;
    ReviveStartServerTime = -1.0f;
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
    if (IsValid(Helper) && Helper->ReviveTarget.Get() == this)
    {
        Helper->ReviveTarget = nullptr;
        Helper->ReviveStartServerTime = -1.0f;
        ++Helper->ReviveCancelCount;
        Helper->ForceNetUpdate();
    }
    if (bHadChannel)
    {
        ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("동료 부활 취소: %s (%s)"), *GetName(), Reason);
    }
}

void AFableCharacter::CompleteRevive()
{
    AFableCharacter* Target = ReviveTarget.Get();
    if (!CanMaintainRevive(Target) || Target->Reviver.Get() != this || ReviveStartServerTime < 0.0f
        || GetWorld()->GetTimeSeconds() - ReviveStartServerTime < FableCharacterTuning::ReviveDuration) { return; }
    // No checkpoint teleport, cooldown reset, reward, inventory or save mutation occurs here.
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
    Target->ReviveProtectionEndServerTime = GetWorld()->GetTimeSeconds() + FableCharacterTuning::ReviveProtectionDuration;
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
    UE_LOG(LogTemp, Display, TEXT("동료 부활 완료: %s -> %s 체력 %.1f/%.1f, 보호 2초"),
        *GetName(), *Target->GetName(), Target->Health, Target->MaxHealth);
}

void AFableCharacter::EnterDowned()
{
    if (!HasAuthority() || !GetWorld() || LifeState != EFableLifeState::Active) { return; }
    if (bLegacyRecovery) { EnterDefeated(TEXT("기존 시제품 복구")); return; }
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

void AFableCharacter::EnterDefeated(const TCHAR* Reason)
{
    if (!HasAuthority() || !GetWorld() || IsDefeated()) { return; }
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
    if (bLegacyRecovery)
        GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFableCharacter::RespawnAtSafePoint, 3.0f, false);
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("동료 구조 대기: %s (%s)"), *GetName(), Reason);
}

void AFableCharacter::TickLifeState()
{
    if (!HasAuthority() || !GetWorld()) { return; }
    const float Now = GetWorld()->GetTimeSeconds();
    // Falling out of the world bypasses dodge, shields and revival protection.
    if (!IsDefeated() && GetActorLocation().Z < -800.0f)
    { EnterDefeated(TEXT("낙하 구조")); return; }
    if (IsDowned() && Now >= DownedEndServerTime)
    { EnterDefeated(TEXT("구조 시간 만료")); return; }
    if (ReviveTarget)
    {
        if (!CanMaintainRevive(ReviveTarget.Get()) || ReviveTarget->Reviver.Get() != this
            || FVector::DistSquared(GetActorLocation(), ReviveStartLocation) > FMath::Square(25.0f))
            CancelReviveInternal(TEXT("이동·거리·시야 또는 동료 상태 변경"));
        else if (Now - ReviveStartServerTime >= FableCharacterTuning::ReviveDuration)
            CompleteRevive();
    }
    if (Reviver && (!IsValid(Reviver.Get()) || Reviver->ReviveTarget.Get() != this || !IsDowned()))
        CancelReviveInternal(TEXT("부활 연결 종료"));
    if (ReviveStartServerTime >= 0.0f && !ReviveTarget && !Reviver)
        CancelReviveInternal(TEXT("동료 연결 종료"));
}

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

void AFableCharacter::SetCheckpoint(const FVector& Location, const FRotator& Rotation, bool bRestore)
{
    if (!HasAuthority() || !GetWorld() || Location.ContainsNaN() || Rotation.ContainsNaN()) { return; }
    FVector SafeLocation = Location;
    if (!GetWorld()->FindTeleportSpot(this, SafeLocation, Rotation)
        || !TeleportTo(SafeLocation, Rotation, false, false))
    {
        UE_LOG(LogTemp, Warning, TEXT("%s의 이동 지점이 막혀 있어 기존 부활 지점을 유지합니다."), *GetName());
        return;
    }
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
    if (IsAlive()) { GetCharacterMovement()->SetMovementMode(MOVE_Walking); }
    else if (bLegacyRecovery) { GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFableCharacter::RespawnAtSafePoint, 3.0f, false); }
    if (Controller)
    {
        Controller->SetControlRotation(Rotation);
        Controller->ClientSetRotation(Rotation, true);
    }
    // A shot from the previous encounter must not damage enemies after party travel.
    for (TActorIterator<AFableProjectile> It(GetWorld()); It; ++It)
    {
        if (It->GetOwner() == this) { It->Destroy(); }
    }
    ForceNetUpdate();
}

void AFableCharacter::RespawnAtSafePoint()
{
    if (!HasAuthority() || !GetWorld() || !bLegacyRecovery || IsAlive()) { return; }
    SetCheckpoint(SafeSpawnLocation, SafeSpawnRotation, true);
    if (!IsAlive())
    {
        GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFableCharacter::RespawnAtSafePoint, 0.5f, false);
    }
}

float AFableCharacter::GetSynchronizedServerTime() const
{
    if (const UWorld* World = GetWorld())
    {
        if (const AGameStateBase* State = World->GetGameState()) { return State->GetServerWorldTimeSeconds(); }
        return World->GetTimeSeconds();
    }
    return 0.0f;
}

float AFableCharacter::GetAttackCooldownRemaining() const
{
    return FMath::Max(0.0f, NextAttackServerTime - GetSynchronizedServerTime());
}

float AFableCharacter::GetSkillCooldownRemaining(int32 Slot) const
{
    if (Slot < 0 || Slot > 2) return 0.0f;
    const float End = Slot == 0 ? NextSkillServerTime : (Slot == 1 ? NextSecondSkillServerTime : NextThirdSkillServerTime);
    return FMath::Max(0.0f, End - GetSynchronizedServerTime());
}

float AFableCharacter::GetDodgeCooldownRemaining() const
{
    return FMath::Max(0.0f, NextDodgeServerTime - GetSynchronizedServerTime());
}

void AFableCharacter::MulticastCombatCue_Implementation(uint8 Cue)
{
    if (GetNetMode() == NM_DedicatedServer) { return; }
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
    else if (Cue == 4) { AttackVisualRemaining = 0.0f; }
    else if (Cue == 5) { HurtVisualRemaining = 0.20f; FableCombatFeedback::PlayCue(this, GetActorLocation(), 0); }
    else if (Cue == 9) { FableCombatFeedback::PlayCue(this, GetActorLocation(), 2); }
}

void AFableCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Use the replicated profile on both server and owning client for movement prediction.
    GetCharacterMovement()->MaxWalkSpeed = GetMovementSpeedLimit();
    if (HasAuthority())
    {
        SynchronizeStats();
        TickLifeState();
        if (ShieldAmount > 0.0f && GetWorld()->GetTimeSeconds() >= ShieldEndServerTime)
        { ShieldAmount = ShieldEndServerTime = 0.0f; ForceNetUpdate(); }
        const AFablePlayerState* Progress = GetPlayerState<AFablePlayerState>();
        const float Regen = Progress ? Progress->GetManaRegenRate() : 8.0f;
        if (IsAlive()) { Mana = FMath::Min(GetMaxMana(), Mana + FMath::Max(0.0f, DeltaSeconds) * Regen); }
        if (bDodging && GetWorld()->GetTimeSeconds() >= DodgeEndServerTime)
        {
            bDodging = false;
            ForceNetUpdate();
        }
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        const AFablePlayerState* Progress = GetPlayerState<AFablePlayerState>();
        if (!bClassAppearanceInitialized || LastVisualClass != GetHeroClass() || (Progress && LastVisualTier != Progress->GetAdvancementTier())) { ApplyClassAppearance(); }
        AnimateBody(DeltaSeconds);
    }
}

void AFableCharacter::AnimateBody(float DeltaSeconds)
{
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
    VisualRoot->SetRelativeRotation(!IsAlive() ? FRotator(0, 0, -75)
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
    if (HeroClass == EFableClass::Mage) { RightArm->SetRelativeRotation(FRotator(12, 0, 5)); }
    if (AttackVisualRemaining > 0.0f)
    {
        const float Progress = 1.0f - AttackVisualRemaining / AttackVisualDuration;
        const float Sweep = FMath::Sin(Progress * PI);
        const float Side = VisualComboIndex == 1 ? -1.0f : 1.0f;
        RightArm->SetRelativeRotation(FRotator(100.0f * Sweep, (Progress * 150.0f - 75.0f) * Side, 8.0f));
        RightForearm->SetRelativeRotation(FRotator(12.0f + 24.0f * Sweep, 0, 0));
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
    if (SkillVisualRemaining > 0.0f)
    {
        LeftArm->SetRelativeRotation(FRotator(55, 0, -38));
        RightArm->SetRelativeRotation(FRotator(55, 0, 38));
        LeftForearm->SetRelativeRotation(FRotator(20, 0, 0));
        RightForearm->SetRelativeRotation(FRotator(20, 0, 0));
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
    for (int32 Index = 0; Index < SkillPetals.Num(); ++Index)
    {
        UStaticMeshComponent* Petal = SkillPetals[Index];
        const bool bShield = GetShieldAmount() > 0.0f;
        const bool bVisible = IsAlive() && (SkillVisualRemaining > 0.0f || bShield);
        Petal->SetVisibility(bVisible);
        if (bVisible)
        {
            const float Progress = bShield ? FMath::Fmod(GetWorld()->GetTimeSeconds() * 0.35f, 1.0f) : 1.0f - SkillVisualRemaining / 0.55f;
            if (UMaterialInstanceDynamic* Mat = Cast<UMaterialInstanceDynamic>(Petal->GetMaterial(0)))
                Mat->SetVectorParameterValue(TEXT("Color"), bShield ? FLinearColor(0.18f,0.65f,0.95f) :
                    (HeroClass == EFableClass::Mage ? (SkillVisualCue == 6 ? FLinearColor(0.3f,0.8f,1.0f) : FLinearColor(0.95f,0.35f,0.08f)) : FLinearColor(0.25f,0.65f,0.32f)));
            const float Angle = Index * 2.0f * PI / SkillPetals.Num() + Progress * 0.8f;
            const float Radius = bShield ? 88.0f : FMath::Lerp(75.0f, HeroClass == EFableClass::Mage && SkillVisualCue == 6 ? 650.0f : FableCharacterTuning::SkillRange, Progress);
            Petal->SetRelativeLocation(FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, bShield ? 15.0f + 45.0f * FMath::Sin(Angle * 2.0f) : -48.0f + FMath::Sin(Progress * PI) * 35.0f));
            // Only transient leaf particles change size; the character parts remain scale 1.
            Petal->SetRelativeScale3D(FVector(1.0f - Progress * 0.65f));
            Petal->SetRelativeRotation(FRotator(20.0f * FMath::Sin(Angle), FMath::RadiansToDegrees(Angle), Progress * 110.0f));
        }
    }
    if (TorsoMaterial)
    {
        const float HurtAmount = HurtVisualRemaining / 0.20f;
        TorsoMaterial->SetVectorParameterValue(TEXT("Color"), FMath::Lerp(FableCharacterTuning::TunicColor, FLinearColor(0.65f, 0.13f, 0.09f), HurtAmount));
    }
    if (BladeMaterial)
    {
        BladeMaterial->SetVectorParameterValue(TEXT("Color"), AttackVisualRemaining > 0.0f
            ? FLinearColor(0.90f, 0.73f, 0.36f) : FableCharacterTuning::BladeColor);
    }
}
