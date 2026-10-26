#include "FableTownRouteUiRenderDriver.h"

#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableHUD.h"
#include "FableOnlineFlowSubsystem.h"
#include "FablePlayerController.h"
#include "FablePlayerState.h"
#include "FableTownManager.h"
#include "FableTownNavigation.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "JsonObjectConverter.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

#if !UE_BUILD_SHIPPING
namespace
{
    const TCHAR* CaseNames[] = {TEXT("direct"), TEXT("via-and-occlusion"), TEXT("no-route-cleared"),
                                TEXT("direct-restored")};

    /** 새 관측 이름은 파일명으로 안전한 ASCII만 허용한다. / 新観測名は安全なASCIIだけを許可する。 */
    bool SafeRun(const FString& Value)
    {
        if (Value.IsEmpty() || Value.Len() > 80 || !Value.Contains(TEXT("-")))
        {
            return false;
        }

        for (TCHAR Character : Value)
        {
            if (!((Character >= TEXT('a') && Character <= TEXT('z')) ||
                  (Character >= TEXT('A') && Character <= TEXT('Z')) ||
                  (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('-') ||
                  Character == TEXT('_')))
            {
                return false;
            }
        }

        return true;
    }

    /** 절대 경로를 같은 표기로 정규화한다. / 絶対パスを同じ表記へ正規化する。 */
    bool Canonical(FString& Value)
    {
        if (Value.IsEmpty() || FPaths::IsRelative(Value) || Value.Contains(TEXT("\n")) || Value.Contains(TEXT("\r")) ||
            Value.Contains(TEXT("\"")))
        {
            return false;
        }

        Value = FPaths::ConvertRelativePathToFull(Value);
        FPaths::NormalizeDirectoryName(Value);
        return FPaths::CollapseRelativeDirectories(Value) && !Value.IsEmpty();
    }

    /** 경로 접두사는 디렉터리 경계까지 대조한다. / 接頭辞はディレクトリ境界まで照合する。 */
    bool Below(const FString& Value, const FString& Root)
    {
        return Value.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase);
    }

    /** Windows 연결 경로를 통과하는 쓰기를 거절한다. / Windows接続パスを通る書込みを拒否する。 */
    bool NoReparseAncestors(const FString& Value)
    {
#if PLATFORM_WINDOWS
        FString Current = Value;
        while (!Current.IsEmpty())
        {
            const FString Native = Current.Replace(TEXT("/"), TEXT("\\"));
            const DWORD Attributes = GetFileAttributesW(*Native);
            if (Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                return false;
            }

            const FString Parent = FPaths::GetPath(Current);
            if (Parent == Current)
            {
                break;
            }
            Current = Parent;
        }
        return true;
#else
        return false;
#endif
    }

    /** 저장 하위 또는 현재 작업의 results 하위만 허용한다. / Savedまたは現ジョブresults配下だけを許可する。 */
    bool SafeOutput(FString& Value)
    {
        if (!Canonical(Value) || !NoReparseAncestors(Value))
        {
            return false;
        }

        FString Saved =
            FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("TownRouteUiRender")));
        if (Canonical(Saved) && Below(Value, Saved))
        {
            return true;
        }

        FString Job = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_DESKTOP_JOB_ROOT"));
        FString Results = FPlatformMisc::GetEnvironmentVariable(TEXT("CODEX_DESKTOP_RESULTS_DIR"));
        FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        if (!Canonical(Job) || !Canonical(Results) || !Canonical(Project))
        {
            return false;
        }

        FString Expected = FPaths::Combine(Job, TEXT("results"));
        return Canonical(Expected) && Results.Equals(Expected, ESearchCase::IgnoreCase) && Below(Project, Job) &&
               Below(Value, Results) && NoReparseAncestors(Job) && NoReparseAncestors(Results);
    }

    /** 반사된 정의 값 전체를 비교 가능한 문자열로 보존한다. / 反射定義全体を比較可能な文字列へ保存する。 */
    TArray<FString> Signatures(const UFableTownCatalog* Catalog, bool bCandidatesOnly)
    {
        TArray<FString> Values;
        if (!Catalog)
        {
            return Values;
        }

        for (const FFableTownDefinition& Definition : Catalog->GetDefinitions())
        {
            if (bCandidatesOnly && Definition.TownId == EFableTownId::LegacyVillage)
            {
                continue;
            }

            TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            FString Text;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
            if (!FJsonObjectConverter::UStructToJsonObject(FFableTownDefinition::StaticStruct(), &Definition, Object, 0,
                                                           0) ||
                !FJsonSerializer::Serialize(Object, Writer))
            {
                return {};
            }
            Values.Add(MoveTemp(Text));
        }

        return Values;
    }

    /** 벡터를 순서 있는 공개 숫자 배열로 기록한다. / ベクトルを順序付き公開数値列へ記録する。 */
    TArray<TSharedPtr<FJsonValue>> VectorJson(const FVector& Value)
    {
        return {MakeShared<FJsonValueNumber>(Value.X), MakeShared<FJsonValueNumber>(Value.Y),
                MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** 실패 뒤에 경유점이 남지 않았는지 검사한다. / 失敗後に経由点が残らないことを検査する。 */
    bool Cleared(const FFableTownNavigationGuide& Guide)
    {
        return !Guide.bValid && !Guide.bViaConnector && Guide.ConnectorId.IsNone() && Guide.SourceFloorId.IsNone() &&
               Guide.NextFloorId.IsNone() && Guide.TargetFloorId.IsNone() && Guide.RemainingSteps == 0 &&
               Guide.WaypointWorld.IsZero() && Guide.TargetWorld.IsZero();
    }
}
#endif

/** 생성 자체는 상태를 바꾸지 않는다. / 生成自体では状態を変えない。 */
UFableTownRouteUiRenderDriver::UFableTownRouteUiRenderDriver()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

/** 정상 프로필/HUD 초기화를 가로채지 않고 명시 인자만 먼저 검사한다. / 正常初期化は妨げず引数だけ確認する。 */
void UFableTownRouteUiRenderDriver::BeginPlay()
{
    Super::BeginPlay();
#if UE_BUILD_SHIPPING
    SetComponentTickEnabled(false);
#else
    StartedAt = FPlatformTime::Seconds();
    PC = Cast<AFablePlayerController>(GetOwner());
    FString Role;
    const bool bArguments =
        FParse::Param(FCommandLine::Get(), TEXT("FableTownRouteUiFixture")) &&
        FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), Role) && Role == TEXT("host") &&
        FParse::Value(FCommandLine::Get(), TEXT("FableTownRouteUiFixtureRun="), RunId) && SafeRun(RunId) &&
        FParse::Value(FCommandLine::Get(), TEXT("FableTownRouteUiFixtureDirectory="), Directory) &&
        SafeOutput(Directory);
    if (!bArguments)
    {
        UE_LOG(LogTemp, Warning, TEXT("FABLE_ROUTE_UI_RENDER rejected_arguments_or_output_scope"));
        SetComponentTickEnabled(false);
        return;
    }

    // 출력은 새 파일에만 기록하고 링크 경로는 생성 전후 모두 거절한다. / 新規ファイルのみ、接続パスを拒否する。
    const FString Report = FPaths::Combine(Directory, TEXT("fixture-report.json"));
    if (IFileManager::Get().FileExists(*Report) || !IFileManager::Get().MakeDirectory(*Directory, true) ||
        !SafeOutput(Directory))
    {
        UE_LOG(LogTemp, Warning, TEXT("FABLE_ROUTE_UI_RENDER output_not_fresh"));
        SetComponentTickEnabled(false);
        return;
    }
    bOutputReady = true;

    // 온라인·다른 검증 모드와 섞인 인자는 어떤 상태도 바꾸기 전에 실패한다. / 他モード混在は変更前に失敗する。
    FString OtherRole;
    const bool bOtherRole = FParse::Param(FCommandLine::Get(), TEXT("FableOnline")) ||
                            FParse::Param(FCommandLine::Get(), TEXT("FableArtReview")) ||
                            FParse::Param(FCommandLine::Get(), TEXT("FableDedicatedValidation")) ||
                            FParse::Value(FCommandLine::Get(), TEXT("FableOnlineValidationRole="), OtherRole) ||
                            FParse::Value(FCommandLine::Get(), TEXT("FableDedicatedValidationRole="), OtherRole) ||
                            FParse::Value(FCommandLine::Get(), TEXT("FableCoopValidationRole="), OtherRole) ||
                            FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRole="), OtherRole) ||
                            FParse::Value(FCommandLine::Get(), TEXT("FableFrontEndValidationRole="), OtherRole);
    if (bOtherRole || !PC.IsValid() || !PC->IsLocalController() || !PC->HasAuthority() || !GetWorld() ||
        GetWorld()->GetNetMode() != NM_Standalone)
    {
        Finish(false, TEXT("isolated_standalone_required"));
    }
#endif
}

/** 준비된 단독 실행의 실제 플레이어·HUD·자체 카탈로그만 허용한다. / 実プレイヤーと自身の定義だけを許可する。 */
bool UFableTownRouteUiRenderDriver::ValidateContext(FString& OutReason) const
{
#if UE_BUILD_SHIPPING
    OutReason = TEXT("shipping_disabled");
    return false;
#else
    const AFablePlayerController* Controller = PC.Get();
    const AFablePlayerState* Profile = Controller ? Controller->GetPlayerState<AFablePlayerState>() : nullptr;
    const AFableCharacter* Hero = Controller ? Controller->GetPawn<AFableCharacter>() : nullptr;
    const UFableOnlineFlowSubsystem* Online =
        GetWorld() && GetWorld()->GetGameInstance()
            ? GetWorld()->GetGameInstance()->GetSubsystem<UFableOnlineFlowSubsystem>()
            : nullptr;
    if (!Controller || !Controller->IsLocalController() || !Controller->HasAuthority() || !GetWorld() ||
        GetWorld()->GetNetMode() != NM_Standalone || !GetWorld()->IsGameWorld() ||
        FPaths::GetBaseFilename(GetWorld()->GetMapName()) != TEXT("ForestClearing") || !Profile ||
        !Profile->bProfileLoaded || Profile->IsBackendEnabled() || Profile->ActiveExpeditionId.IsValid() ||
        Profile->StoryChapter != 0 || !Hero || !Hero->IsAlive() || Controller->IsAnyPanelOpen() ||
        Controller->IsFrontEndOpen() || !Cast<AFableHUD>(Controller->GetHUD()) || !Controller->GetHUD()->bShowHUD ||
        (Online && Online->IsEnabled()))
    {
        OutReason = TEXT("normal_local_profile_and_hud_required");
        return false;
    }

    const AFableTownManager* Manager = AFableTownManager::Find(GetWorld());
    const UFableTownCatalog* Catalog = Manager ? Manager->GetCatalog() : nullptr;
    if (!Manager || Manager->GetWorld() != GetWorld() || !Catalog || Catalog->GetOuter() != Manager ||
        Catalog == GetDefault<UFableTownCatalog>() ||
        Catalog->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) ||
        Manager->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
    {
        OutReason = TEXT("owned_instance_catalog_required");
        return false;
    }

    for (const FFableTownDefinition& Definition : Catalog->GetDefinitions())
    {
        if (Definition.TownId != EFableTownId::LegacyVillage && Definition.bGeometryReady)
        {
            OutReason = TEXT("candidate_geometry_must_stay_disabled");
            return false;
        }
    }
    return true;
#endif
}

/** 매니저 자체 정의와 이 실행의 카메라만 설정한다. / manager自身の定義と現実行カメラだけを設定する。 */
bool UFableTownRouteUiRenderDriver::PrepareScene(FString& OutReason)
{
#if UE_BUILD_SHIPPING
    OutReason = TEXT("shipping_disabled");
    return false;
#else
    if (!ValidateContext(OutReason))
    {
        return false;
    }

    Towns = AFableTownManager::Find(GetWorld());
    const UFableTownCatalog* Catalog = Towns->GetCatalog();
    FString OriginalError;
    if (!Catalog->Validate(OriginalError))
    {
        OutReason = TEXT("original_catalog_invalid");
        return false;
    }
    FFableTownDefinition* Legacy = const_cast<FFableTownDefinition*>(Catalog->FindTown(EFableTownId::LegacyVillage));
    if (!Legacy || !Legacy->bGeometryReady || !Legacy->LocalToWorld.Equals(FTransform::Identity) ||
        PC->IsMoveInputIgnored() || PC->IsLookInputIgnored())
    {
        OutReason = TEXT("fresh_legacy_fixture_context_required");
        return false;
    }

    // 복구 원본과 전역/후보 서명을 먼저 보존한다. / 復元元と既定・候補署名を先に保存する。
    OriginalLegacy = *Legacy;
    DefaultSignatures = Signatures(GetDefault<UFableTownCatalog>(), false);
    CandidateSignatures = Signatures(Catalog, true);
    if (DefaultSignatures.Num() != 5 || CandidateSignatures.Num() != 4)
    {
        OutReason = TEXT("complete_catalog_signatures_required");
        return false;
    }
    bCatalogChanged = true;

    // 실제 상층 지형은 만들지 않으며 조회용 두 층과 연결만 정의한다. / 実上階を作らず照会用の階・接続だけを定義する。
    FFableTownFloorDefinition Ground;
    Ground.FloorId = TEXT("F0");
    Ground.LocalBounds = FBox(FVector(-4500, -4500, -799), FVector(4499, 4500, 300));
    FFableTownFloorDefinition Upper;
    Upper.FloorId = TEXT("F1");
    Upper.LocalBounds = FBox(FVector(-4500, -4500, 700), FVector(4499, 4500, 1499));
    Legacy->Floors = {Ground, Upper};
    for (FFableTownAnchorDefinition& Anchor : Legacy->Anchors)
    {
        Anchor.FloorId = TEXT("F0");
    }

    // 카메라·입력·이동의 원래 상태도 이 실행 안에서만 보존한다. / カメラ・入力・移動の元状態も現実行だけで保存する。
    AFableCharacter* Hero = PC->GetPawn<AFableCharacter>();
    UCharacterMovementComponent* Movement = Hero->GetCharacterMovement();
    if (!Movement)
    {
        OutReason = TEXT("character_movement_required");
        return false;
    }
    OriginalPawnLocation = Hero->GetActorLocation();
    OriginalPawnRotation = Hero->GetActorRotation();
    OriginalVelocity = Movement->Velocity;
    OriginalMovementMode = uint8(Movement->MovementMode);
    OriginalCustomMovementMode = Movement->CustomMovementMode;
    OriginalControlRotation = PC->GetControlRotation();
    PreviousViewTarget = PC->GetViewTarget();
    bOriginalAutoCamera = PC->bAutoManageActiveCameraTarget;
    LockedPawnLocation = FVector(-2300, -2300, 110);
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    Movement->DisableMovement();
    Hero->SetActorLocation(LockedPawnLocation, false, nullptr, ETeleportType::TeleportPhysics);
    bInputFrozen = true;

    for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
    {
        EnemyTicks.Emplace(*It, It->IsActorTickEnabled());
        It->SetActorTickEnabled(false);
    }

    // 제품 HUD는 숨기지 않고 실제 viewport 카메라를 사용한다. / 製品HUDを隠さず実viewportカメラを使う。
    Camera = GetWorld()->SpawnActor<ACameraActor>();
    if (!Camera.IsValid() || !Camera->GetCameraComponent())
    {
        OutReason = TEXT("owned_camera_spawn_failed");
        return false;
    }
    const FVector CameraLocation = LockedPawnLocation + FVector(-150, -1000, 720);
    const FVector LookAt = LockedPawnLocation + FVector(0, 150, 100);
    const FRotator Rotation = (LookAt - CameraLocation).Rotation();
    Camera->SetActorLocation(CameraLocation);
    Camera->SetActorRotation(Rotation);
    // HUD Canvas와 전체 viewport의 좌표계를 일치시킨다. / HUD Canvasと全viewportの座標系を一致させる。
    Camera->GetCameraComponent()->bConstrainAspectRatio = false;
    Camera->GetCameraComponent()->SetFieldOfView(60.0f);
    PC->bAutoManageActiveCameraTarget = false;
    PC->SetViewTarget(Camera.Get());
    PC->SetControlRotation(FRotator(0, Rotation.Yaw, 0));
    PC->GetViewportSize(Width, Height);
    bScenePrepared = true;
    SceneAt = FPlatformTime::Seconds();
    return true;
#endif
}

/** 실제 투영과 제품의 거리 제한을 모두 만족하는 Merchant 라벨을 둔다. / 実投影と距離制限を満たすラベルを置く。 */
bool UFableTownRouteUiRenderDriver::PlaceControlLabel(FString& OutReason)
{
#if UE_BUILD_SHIPPING
    OutReason = TEXT("shipping_disabled");
    return false;
#else
    if (!ValidateContext(OutReason) || !DefinitionsUnchanged())
    {
        OutReason = TEXT("owned_context_changed_before_projection");
        return false;
    }
    int32 CurrentWidth = 0, CurrentHeight = 0;
    PC->GetViewportSize(CurrentWidth, CurrentHeight);
    if (CurrentWidth <= 0 || CurrentHeight <= 0)
    {
        OutReason = TEXT("viewport_not_ready");
        return false;
    }
    Width = CurrentWidth;
    Height = CurrentHeight;
    const float Scale = FMath::Clamp(FMath::Min(Width / 900.0f, Height / 600.0f), 0.6f, 1.7f);
    const FVector2D Wanted(Width - 220.0f * Scale, 350.0f * Scale);
    FVector Origin, Direction;
    if (!PC->DeprojectScreenPositionToWorld(Wanted.X, Wanted.Y, Origin, Direction) ||
        FMath::Abs(Direction.Z) < KINDA_SMALL_NUMBER)
    {
        OutReason = TEXT("control_label_deprojection_failed");
        return false;
    }
    const double Distance = (220.0 - Origin.Z) / Direction.Z;
    const FVector Position = Origin + Direction * Distance - FVector(0, 0, 220);
    if (!FMath::IsFinite(Distance) || Distance <= 0 || Position.ContainsNaN() ||
        FVector::Dist2D(Position, LockedPawnLocation) > 1500.0)
    {
        OutReason = TEXT("control_label_not_eligible_by_distance");
        return false;
    }

    FFableTownDefinition* Legacy =
        const_cast<FFableTownDefinition*>(Towns->GetCatalog()->FindTown(EFableTownId::LegacyVillage));
    FFableTownAnchorDefinition* Merchant = Legacy ? Legacy->Anchors.FindByPredicate(
                                                        [](const FFableTownAnchorDefinition& Anchor)
                                                        {
                                                            return Anchor.AnchorId == TEXT("Service.Merchant");
                                                        })
                                                  : nullptr;
    if (!Merchant || !Legacy->Floors[0].LocalBounds.IsInsideOrOn(Position))
    {
        OutReason = TEXT("control_label_anchor_unavailable");
        return false;
    }
    Merchant->LocalSlots = {FTransform(Position)};
    Merchant->ExpectedSupportLocalZ = 0.0;

    FString Error;
    FVector2D Actual;
    if (!Towns->GetCatalog()->Validate(Error) ||
        !PC->ProjectWorldLocationToScreen(Position + FVector(0, 0, 220), Actual) || !Actual.Equals(Wanted, 4.0f) ||
        !DefinitionsUnchanged())
    {
        OutReason = TEXT("control_label_projection_or_catalog_invalid");
        return false;
    }
    return true;
#endif
}

/** 同じ場面の自身アンカーだけを切り替える。 / 같은 장면의 자체 앵커만 전환한다. */
bool UFableTownRouteUiRenderDriver::ApplyCase(int32 Index, FString& OutReason)
{
#if UE_BUILD_SHIPPING
    OutReason = TEXT("shipping_disabled");
    return false;
#else
    if (Index < 0 || Index >= UE_ARRAY_COUNT(CaseNames) || !DefinitionsUnchanged())
    {
        OutReason = TEXT("case_or_external_definition_changed");
        return false;
    }
    FFableTownDefinition* Legacy =
        const_cast<FFableTownDefinition*>(Towns->GetCatalog()->FindTown(EFableTownId::LegacyVillage));
    FFableTownAnchorDefinition* Board = Legacy ? Legacy->Anchors.FindByPredicate(
                                                     [](const FFableTownAnchorDefinition& Anchor)
                                                     {
                                                         return Anchor.AnchorId == TEXT("Service.Board");
                                                     })
                                               : nullptr;
    const FFableTownAnchorDefinition* OriginalBoard = OriginalLegacy.Anchors.FindByPredicate(
        [](const FFableTownAnchorDefinition& Anchor)
        {
            return Anchor.AnchorId == TEXT("Service.Board");
        });
    if (!Board || !OriginalBoard || OriginalBoard->LocalSlots.Num() == 0)
    {
        OutReason = TEXT("board_anchor_unavailable");
        return false;
    }

    const bool bUpper = Index == 1 || Index == 2;
    Board->FloorId = bUpper ? TEXT("F1") : TEXT("F0");
    Board->LocalSlots = bUpper ? TArray<FTransform>{FTransform(FVector(0, -750, 900))} : OriginalBoard->LocalSlots;
    Board->ExpectedSupportLocalZ = bUpper ? 900.0 : OriginalBoard->ExpectedSupportLocalZ;
    Legacy->Connectors.Empty();
    if (Index != 2)
    {
        FFableTownConnectorDefinition Stair;
        Stair.ConnectorId = TEXT("Guide.Stair");
        Stair.FromFloorId = TEXT("F0");
        Stair.ToFloorId = TEXT("F1");
        Stair.LocalPoints = {FVector(100, -750, 110), FVector(100, -750, 500), FVector(100, -750, 900)};
        Stair.RadiusCm = 25.0;
        Legacy->Connectors.Add(MoveTemp(Stair));
    }

    FString Error;
    if (!Towns->GetCatalog()->Validate(Error))
    {
        OutReason = TEXT("case_catalog_invalid");
        return false;
    }
    CurrentCase = Index;
    CaseAt = FPlatformTime::Seconds();
    StableTicks = 0;
    return true;
#endif
}

/** 제품 조회를 호출하고 양성 대조 라벨의 자격을 별도로 증명한다. / 製品照会と対照ラベルの適格性を記録する。 */
bool UFableTownRouteUiRenderDriver::ObserveCase(TSharedPtr<FJsonObject>& OutSnapshot, FString& OutReason) const
{
#if UE_BUILD_SHIPPING
    OutReason = TEXT("shipping_disabled");
    return false;
#else
    if (!ValidateContext(OutReason) || !DefinitionsUnchanged() || !Camera.IsValid() || !Camera->GetCameraComponent() ||
        Camera->GetCameraComponent()->bConstrainAspectRatio || PC->GetViewTarget() != Camera.Get() ||
        !PC->GetPawn()->GetActorLocation().Equals(LockedPawnLocation, 0.1))
    {
        OutReason = TEXT("fixture_context_changed_before_capture");
        return false;
    }
    int32 CurrentWidth = 0, CurrentHeight = 0;
    PC->GetViewportSize(CurrentWidth, CurrentHeight);
    if (CurrentWidth != Width || CurrentHeight != Height)
    {
        OutReason = TEXT("viewport_changed_between_cases");
        return false;
    }

    const UFableTownCatalog* Catalog = Towns->GetCatalog();
    FVector Target;
    FString Label;
    FFableTownNavigationGuide Guide, DirectQuery;
    const bool bHasTarget = PC->GetNavigationTarget(Target, Label, &Guide);
    const EFableTownRouteStatus Route = FableTownNavigation::ResolveServiceGuide(
        Catalog, LockedPawnLocation, EFableTownServiceId::QuestGuide, DirectQuery);
    const bool bExpectedRoute = CurrentCase == 1;
    const bool bExpectedFailure = CurrentCase == 2;
    const bool bCaseValid =
        bExpectedFailure
            ? !bHasTarget && Route == EFableTownRouteStatus::NoRoute && Cleared(Guide) && Target.IsZero() &&
                  !Label.IsEmpty()
            : bHasTarget && Route == EFableTownRouteStatus::Ready && Guide.bValid &&
                  Guide.Status == EFableTownRouteStatus::Ready &&
                  Guide.Service.Service.ServiceId == EFableTownServiceId::QuestGuide &&
                  Guide.Service.Anchor.AnchorId == TEXT("Service.Board") && Guide.bViaConnector == bExpectedRoute &&
                  Target.Equals(Guide.WaypointWorld) && Guide.WaypointWorld.Equals(DirectQuery.WaypointWorld) &&
                  Guide.TargetWorld.Equals(DirectQuery.TargetWorld) && Guide.SourceFloorId == TEXT("F0") &&
                  Guide.TargetFloorId == (bExpectedRoute ? TEXT("F1") : TEXT("F0")) &&
                  (bExpectedRoute ? Guide.RemainingSteps == 1 && Guide.NextFloorId == TEXT("F1") &&
                                        Guide.ConnectorId == TEXT("Guide.Stair") && !Guide.TargetWorld.Equals(Target)
                                  : Guide.RemainingSteps == 0);
    if (!bCaseValid)
    {
        OutReason = TEXT("product_guide_does_not_match_case");
        return false;
    }

    FFableTownLocationContext Physical;
    FFableTownServiceDescriptor Merchant, StrictQuest;
    const EFableTownNavigationStatus StrictQuestStatus =
        FableTownNavigation::ResolveService(Catalog, LockedPawnLocation, EFableTownServiceId::QuestGuide, StrictQuest);
    FVector2D Projection;
    const float Scale = FMath::Clamp(FMath::Min(Width / 900.0f, Height / 600.0f), 0.6f, 1.7f);
    if (!Catalog->ResolveLocation(LockedPawnLocation, Physical, true) || Physical.FloorId != TEXT("F0") ||
        !Physical.ConnectorId.IsNone() ||
        FableTownNavigation::ResolveService(Catalog, LockedPawnLocation, EFableTownServiceId::Merchant, Merchant) !=
            EFableTownNavigationStatus::Ready ||
        !PC->ProjectWorldLocationToScreen(Merchant.WorldLocation + FVector(0, 0, 220), Projection) ||
        !Projection.Equals(FVector2D(Width - 220.0f * Scale, 350.0f * Scale), 4.0f) ||
        FVector::Dist2D(LockedPawnLocation, Merchant.WorldLocation) > 1500.0 || Projection.X < 90.0f * Scale ||
        Projection.X > Width - 90.0f * Scale || Projection.Y < 170.0f * Scale || Projection.Y > Height - 180.0f * Scale)
    {
        OutReason = TEXT("positive_control_world_label_not_eligible");
        return false;
    }
    if (StrictQuestStatus != (bExpectedRoute || bExpectedFailure ? EFableTownNavigationStatus::DifferentFloor
                                                                 : EFableTownNavigationStatus::Ready))
    {
        OutReason = TEXT("strict_current_floor_npc_guard_changed");
        return false;
    }

    // 값·투영은 관측이며 실제 글자/색 판정은 PNG 검토 전까지 false다. / 値・投影は観測でありピクセル判定ではない。
    OutSnapshot = MakeShared<FJsonObject>();
    OutSnapshot->SetStringField(TEXT("name"), CaseNames[CurrentCase]);
    OutSnapshot->SetNumberField(TEXT("observedProcessSeconds"), FPlatformTime::Seconds());
    OutSnapshot->SetStringField(TEXT("clockDomain"), TEXT("fixture-process-monotonic"));
    OutSnapshot->SetBoolField(TEXT("controllerHasTarget"), bHasTarget);
    OutSnapshot->SetBoolField(TEXT("guideValid"), Guide.bValid);
    OutSnapshot->SetBoolField(TEXT("viaConnector"), Guide.bViaConnector);
    OutSnapshot->SetNumberField(TEXT("logicalRouteStatus"), uint8(Route));
    OutSnapshot->SetNumberField(TEXT("strictQuestGuideStatus"), uint8(StrictQuestStatus));
    OutSnapshot->SetStringField(TEXT("destinationLabel"), Label);
    OutSnapshot->SetStringField(TEXT("floorProgressText"), FableTownNavigation::RouteProgressText(Guide));
    OutSnapshot->SetStringField(TEXT("sourceFloor"), Guide.SourceFloorId.ToString());
    OutSnapshot->SetStringField(TEXT("targetFloor"), Guide.TargetFloorId.ToString());
    OutSnapshot->SetNumberField(TEXT("remainingSteps"), Guide.RemainingSteps);
    OutSnapshot->SetArrayField(TEXT("waypointWorld"), VectorJson(Target));
    OutSnapshot->SetArrayField(TEXT("pawnWorld"), VectorJson(LockedPawnLocation));
    OutSnapshot->SetArrayField(TEXT("merchantWorld"), VectorJson(Merchant.WorldLocation));
    OutSnapshot->SetArrayField(TEXT("cameraWorld"), VectorJson(Camera->GetActorLocation()));
    OutSnapshot->SetBoolField(TEXT("cameraConstrainAspectRatio"), Camera->GetCameraComponent()->bConstrainAspectRatio);
    OutSnapshot->SetNumberField(TEXT("merchantProjectedX"), Projection.X);
    OutSnapshot->SetNumberField(TEXT("merchantProjectedY"), Projection.Y);
    OutSnapshot->SetBoolField(TEXT("merchantEligibleBeforeCardOcclusion"), true);
    OutSnapshot->SetBoolField(TEXT("merchantExpectedVisibleAfterCardOcclusion"), !bExpectedRoute);
    OutSnapshot->SetBoolField(TEXT("syntheticCatalogOnly"), true);
    OutSnapshot->SetBoolField(TEXT("globalDefaultsUnchanged"), true);
    OutSnapshot->SetBoolField(TEXT("candidateDefinitionsUnchanged"), true);
    OutSnapshot->SetBoolField(TEXT("visualReviewPassed"), false);
    return true;
#endif
}

/** 합성 Legacy 이외의 원본은 같은 값이어야 한다. / 合成Legacy以外の元値は同一でなければならない。 */
bool UFableTownRouteUiRenderDriver::DefinitionsUnchanged() const
{
#if UE_BUILD_SHIPPING
    return false;
#else
    return Towns.IsValid() && Towns->GetCatalog() && DefaultSignatures.Num() == 5 && CandidateSignatures.Num() == 4 &&
           Signatures(GetDefault<UFableTownCatalog>(), false) == DefaultSignatures &&
           Signatures(Towns->GetCatalog(), true) == CandidateSignatures;
#endif
}

/** 스크린샷 저장을 확인한 다음에만 다음 사례로 간다. / 保存を確認した後だけ次ケースへ進む。 */
void UFableTownRouteUiRenderDriver::TickComponent(float DeltaTime, ELevelTick TickType,
                                                  FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if !UE_BUILD_SHIPPING
    if (bDone || !bOutputReady)
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - StartedAt > 90.0)
    {
        Finish(false, TEXT("fixture_timeout"));
        return;
    }

    FString Reason;
    if (!bScenePrepared)
    {
        if (!ValidateContext(Reason))
        {
            if (Now - StartedAt > 30.0)
            {
                Finish(false, Reason);
            }
            return;
        }
        if (!PrepareScene(Reason))
        {
            Finish(false, Reason);
        }
        return;
    }
    if (!bControlReady)
    {
        if (Now - SceneAt < 0.5)
        {
            return;
        }
        if (!PlaceControlLabel(Reason) || !ApplyCase(0, Reason))
        {
            Finish(false, Reason);
            return;
        }
        bControlReady = true;
        return;
    }

    // 파일 저장이 안정될 때까지 장면·사례를 유지한다. / 保存が安定するまで場面・ケースを維持する。
    if (!PendingScreenshot.IsEmpty())
    {
        const int64 Bytes = IFileManager::Get().FileSize(*PendingScreenshot);
        StableFileTicks = Bytes > 128 && Bytes == PreviousFileSize ? StableFileTicks + 1 : 0;
        PreviousFileSize = Bytes;
        if (StableFileTicks >= 2)
        {
            PendingSnapshot->SetStringField(TEXT("screenshotPath"), PendingScreenshot);
            PendingSnapshot->SetNumberField(TEXT("screenshotBytes"), double(Bytes));
            Snapshots.Add(MakeShared<FJsonValueObject>(PendingSnapshot));
            PendingScreenshot.Empty();
            PendingSnapshot.Reset();
            ++CurrentCase;
            if (CurrentCase == UE_ARRAY_COUNT(CaseNames))
            {
                Finish(true, TEXT("state_and_capture_files_ready_visual_review_pending"));
            }
            else if (!ApplyCase(CurrentCase, Reason))
            {
                Finish(false, Reason);
            }
        }
        else if (Now - CaptureAt > 15.0)
        {
            Finish(false, TEXT("engine_screenshot_file_not_saved"));
        }
        return;
    }

    // 실제 HUD가 새 조회 상태를 그릴 두 프레임과 지연을 보장한다. / HUDが新状態を描く二フレームと遅延を確保する。
    ++StableTicks;
    if (StableTicks < 2 || Now - CaseAt < 0.35 || FScreenshotRequest::IsScreenshotRequested())
    {
        return;
    }
    if (!ObserveCase(PendingSnapshot, Reason))
    {
        Finish(false, Reason);
        return;
    }
    PendingScreenshot = FPaths::Combine(Directory, FString(CaseNames[CurrentCase]) + TEXT(".png"));
    if (IFileManager::Get().FileExists(*PendingScreenshot) || !SafeOutput(Directory))
    {
        Finish(false, TEXT("screenshot_path_not_fresh"));
        return;
    }
    CaptureAt = Now;
    PreviousFileSize = -1;
    StableFileTicks = 0;
    FScreenshotRequest::RequestScreenshot(PendingScreenshot, true, false);
#endif
}

/** 원본 복구는 저장·서버 이동·타 프로세스에 관여하지 않는다. / 復元は保存・サーバー移動・他プロセスへ関与しない。 */
bool UFableTownRouteUiRenderDriver::RestoreScene()
{
#if UE_BUILD_SHIPPING
    return true;
#else
    bool bRestored = true;
    if (bCatalogChanged)
    {
        const UFableTownCatalog* Catalog = Towns.IsValid() ? Towns->GetCatalog() : nullptr;
        FFableTownDefinition* Legacy =
            Catalog && Catalog->GetOuter() == Towns.Get()
                ? const_cast<FFableTownDefinition*>(Catalog->FindTown(EFableTownId::LegacyVillage))
                : nullptr;
        bRestored = Legacy && !Catalog->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject);
        if (bRestored)
        {
            *Legacy = OriginalLegacy;
            bCatalogChanged = false;
        }
    }

    if (bInputFrozen && PC.IsValid())
    {
        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);
        PC->bAutoManageActiveCameraTarget = bOriginalAutoCamera;
        PC->SetControlRotation(OriginalControlRotation);
        if (PreviousViewTarget.IsValid())
        {
            PC->SetViewTarget(PreviousViewTarget.Get());
        }
        if (AFableCharacter* Hero = PC->GetPawn<AFableCharacter>())
        {
            Hero->SetActorLocationAndRotation(OriginalPawnLocation, OriginalPawnRotation, false, nullptr,
                                              ETeleportType::TeleportPhysics);
            if (UCharacterMovementComponent* Movement = Hero->GetCharacterMovement())
            {
                Movement->SetMovementMode(EMovementMode(OriginalMovementMode), OriginalCustomMovementMode);
                Movement->Velocity = OriginalVelocity;
            }
        }
        bInputFrozen = false;
    }

    for (const auto& Entry : EnemyTicks)
    {
        if (Entry.Key.IsValid())
        {
            Entry.Key->SetActorTickEnabled(Entry.Value);
        }
    }
    EnemyTicks.Empty();
    if (Camera.IsValid())
    {
        Camera->Destroy();
        Camera.Reset();
    }
    return bRestored;
#endif
}

/** 観測完了と画像目視完了を混同しない。 / 관측 완료와 이미지 육안 검토 완료를 구분한다. */
void UFableTownRouteUiRenderDriver::Finish(bool bSuccess, const FString& Reason)
{
#if !UE_BUILD_SHIPPING
    if (bDone)
    {
        return;
    }
    bDone = true;
    SetComponentTickEnabled(false);
    const bool bDefinitionsSafe = !bCatalogChanged || DefinitionsUnchanged();
    const bool bRestored = RestoreScene();
    bSuccess = bSuccess && bDefinitionsSafe && bRestored && Snapshots.Num() == UE_ARRAY_COUNT(CaseNames);

    // 새로운 결과만 기록하며 픽셀 검토·실제 층 이동을 성공으로 만들지 않는다. / 新規結果だけ、画素・実階移動は未判定。
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetStringField(TEXT("kind"), TEXT("fablewood-town-route-ui-render-fixture"));
    Report->SetStringField(TEXT("status"), bSuccess ? TEXT("passed") : TEXT("failed"));
    Report->SetStringField(TEXT("reason"), bDefinitionsSafe && bRestored ? Reason : TEXT("owned_state_restore_failed"));
    Report->SetStringField(TEXT("runId"), RunId);
    Report->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
    Report->SetStringField(TEXT("processImage"), FPlatformProcess::ExecutablePath());
    Report->SetStringField(TEXT("networkMode"), TEXT("standalone"));
    Report->SetStringField(TEXT("finishedUtc"), FDateTime::UtcNow().ToIso8601());
    Report->SetBoolField(TEXT("fixtureExecutionPassed"), bSuccess);
    Report->SetBoolField(TEXT("visualReviewPassed"), false);
    Report->SetBoolField(TEXT("globalDefaultsUnchanged"), bDefinitionsSafe);
    Report->SetBoolField(TEXT("candidateDefinitionsUnchanged"), bDefinitionsSafe);
    Report->SetBoolField(TEXT("ownedCatalogRestored"), bRestored);
    Report->SetBoolField(TEXT("newTownGeometryActivated"), false);
    Report->SetBoolField(TEXT("physicalFloorTraversalVerified"), false);
    Report->SetBoolField(TEXT("databaseVerificationClaimed"), false);
    Report->SetBoolField(TEXT("nativeTestsExecutedInThisProcess"), false);
    Report->SetNumberField(TEXT("sourceNativeExpected"), 91);
    TSharedRef<FJsonObject> Resolution = MakeShared<FJsonObject>();
    Resolution->SetNumberField(TEXT("width"), Width);
    Resolution->SetNumberField(TEXT("height"), Height);
    Report->SetObjectField(TEXT("resolution"), Resolution);
    Report->SetArrayField(TEXT("caseSnapshots"), Snapshots);

    FString Text;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    const FString Path = FPaths::Combine(Directory, TEXT("fixture-report.json"));
    const bool bWritten = bOutputReady && SafeOutput(Directory) && !IFileManager::Get().FileExists(*Path) &&
                          FJsonSerializer::Serialize(Report, Writer) &&
                          FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
                                                        &IFileManager::Get(), FILEWRITE_NoReplaceExisting);
    UE_LOG(LogTemp, Display, TEXT("FABLE_ROUTE_UI_RENDER status=%s report_written=%d"),
           bSuccess ? TEXT("passed") : TEXT("failed"), int32(bWritten));
    if (PC.IsValid() && PC->IsLocalController() && GetWorld() && GetWorld()->GetNetMode() == NM_Standalone)
    {
        FPlatformMisc::RequestExitWithStatus(false, bSuccess && bWritten ? 0 : 1);
    }
#endif
}

/** 종료 중에도 소유했던 로컬 변경만 복원한다. / 終了中も自身のローカル変更だけを戻す。 */
void UFableTownRouteUiRenderDriver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreScene();
    Super::EndPlay(EndPlayReason);
}
