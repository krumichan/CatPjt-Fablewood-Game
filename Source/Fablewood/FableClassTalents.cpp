#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "FableAdventure.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

/** 선택한 전직 특성 값을 반환한다. */
EFableTalentPath AFablePlayerState::GetTalentChoice(int32 TargetTier) const
{
    if (!ClassProgress.IsValidIndex(int32(HeroClass)))
    {
        return EFableTalentPath::None;
    }
    const FFableClassProgress& Build = ClassProgress[int32(HeroClass)];
    return TargetTier == 2 ? Build.SecondTalent : TargetTier == 3 ? Build.ThirdTalent : EFableTalentPath::None;
}

/** 적용할 특성 단계 값을 반환한다. */
int32 AFablePlayerState::GetTalentRank(EFableTalentPath Path) const
{
    return ClassProgress.IsValidIndex(int32(HeroClass)) ? FableRPG::TalentRank(ClassProgress[int32(HeroClass)], Path)
                                                        : 0;
}

/** 현재 특성 선택의 상태 서명 값을 반환한다. */
FString AFablePlayerState::GetTalentSignature() const
{
    FString Result;

    // 직업·스킬·특성 빌드 순회
    for (const FFableClassProgress& Build : ClassProgress)
    {
        if (!Result.IsEmpty())
        {
            Result += TEXT(";");
        }
        Result += FString::Printf(TEXT("%d,%d"), uint8(Build.SecondTalent), uint8(Build.ThirdTalent));
    }

    return Result;
}

/** 전투에 적용되는 빌드 변경 번호를 증가시킨다. */
void AFablePlayerState::BumpClassBuildRevision()
{
    ClassBuildRevision = ClassBuildRevision == MAX_int32 ? 1 : ClassBuildRevision + 1;
}

/** 직업 빌드를 변경할 수 없는 상태의 안내 문구를 구한다. */
FString AFablePlayerState::ClassBuildBlockReason() const
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!bProfileLoaded || bStorageBlocked)
    {
        return TEXT("저장 프로필을 확인할 수 없어 육성을 변경하지 않았습니다.");
    }
    if (!GetWorld() || !FableRPG::IsValidClassProgress(ClassProgress, Level, ForestQuest, DungeonCompletions) ||
        !FableRPG::IsValidTalentProgress(ClassProgress) || uint8(HeroClass) > uint8(EFableClass::Archer))
    {
        return TEXT("직업 정보를 읽을 수 없어 육성을 변경하지 않았습니다.");
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const APlayerController* OwnerPC = Cast<APlayerController>(GetOwner());
    const AFableCharacter* Hero = OwnerPC ? Cast<AFableCharacter>(OwnerPC->GetPawn()) : nullptr;
    // 로완은 공유 마을에 있지만 전투·구조 잠금은 자신의 원정 진행으로 검사한다.
    const auto GS = FableAdventure::For(this);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !Hero->IsAlive())
    {
        return TEXT("활동 가능한 상태로 로완 근처에서 육성을 변경하세요.");
    }

    const FVector Delta = Hero->GetActorLocation() - FableRPG::BoardLocation();

    // 지역·적·전투 진행 조건 검사
    if (Delta.SizeSquared2D() > FMath::Square(330.0f) || FMath::Abs(Delta.Z) >= 300.0f || !GS ||
        GS->ActiveRegion != EFableRegion::Village)
    {
        return TEXT("마을의 안내인 로완 근처에서 육성을 변경하세요.");
    }
    if (GS->bPartyRecoveryPending || GS->AdventureStage == EFableAdventureStage::Forest ||
        GS->AdventureStage == EFableAdventureStage::Dungeon || GS->AdventureStage == EFableAdventureStage::Boss)
    {
        return TEXT("진행 중인 전투와 구조가 끝난 뒤 육성을 변경하세요.");
    }
    if (Hero->GetBuildChangeCooldownRemaining() > 0.0f)
    {
        return TEXT("전투와 스킬의 대기시간·보호 효과가 끝난 뒤 육성을 변경하세요.");
    }

    return FString();
}

/** 특성 선택 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetTalentSelectBlockReason(int32 TargetTier, EFableTalentPath Path) const
{
    // 직업·스킬·특성 빌드 조건 검사
    if ((TargetTier != 2 && TargetTier != 3) || (Path != EFableTalentPath::First && Path != EFableTalentPath::Second))
    {
        return TEXT("알 수 없는 직업 특성입니다.");
    }

    const FString Common = ClassBuildBlockReason();

    // 직업·스킬·특성 빌드 조건 검사
    if (!Common.IsEmpty())
    {
        return Common;
    }
    if (GetAdvancementTier() < TargetTier)
    {
        return FString::Printf(TEXT("%d차 전직을 완료하면 선택할 수 있습니다."), TargetTier);
    }
    if (TargetTier == 3 && GetTalentChoice(2) == EFableTalentPath::None)
    {
        return TEXT("2차 특성을 먼저 선택하세요.");
    }

    const EFableTalentPath Selected = GetTalentChoice(TargetTier);

    // 직업·스킬·특성 빌드 조건 검사
    if (Selected == Path)
    {
        return TEXT("이미 선택한 특성입니다.");
    }
    if (Selected != EFableTalentPath::None)
    {
        return TEXT("현재 직업을 초기화한 뒤 다른 특성을 선택할 수 있습니다.");
    }

    return FString();
}

/** 현재 직업 빌드를 초기화할 때 환급할 스킬 포인트 값을 반환한다. */
int32 AFablePlayerState::GetCurrentClassRefundableSkillPoints() const
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!ClassProgress.IsValidIndex(int32(HeroClass)))
    {
        return 0;
    }

    const FFableClassProgress& Build = ClassProgress[int32(HeroClass)];

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Build.Ranks.Num() != 4)
    {
        return 0;
    }

    int32 Result = 0;

    // 같은 규칙을 적용할 대상을 순회한다.
    for (int32 Slot = 0; Slot < 4; ++Slot)
    {
        Result += Build.Ranks[Slot] - (Slot == 3 ? 0 : 1);
    }

    return FMath::Max(0, Result);
}

/** 직업 빌드 초기화 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetBuildResetBlockReason() const
{
    const FString Common = ClassBuildBlockReason();

    // 재화와 작업 비용 조건 검사
    if (!Common.IsEmpty())
    {
        return Common;
    }
    if (GetCurrentClassRefundableSkillPoints() == 0 && GetTalentChoice(2) == EFableTalentPath::None &&
        GetTalentChoice(3) == EFableTalentPath::None)
    {
        return TEXT("현재 직업에는 초기화할 투자나 특성이 없습니다.");
    }
    if (Gold < GetBuildResetGoldCost())
    {
        return TEXT("육성 초기화에는 골드 50이 필요합니다.");
    }

    return FString();
}

/** 전직 단계와 선택 조건을 확인해 해당 특성을 확정한다. */
void AFablePlayerState::SelectTalent(int32 TargetTier, EFableTalentPath Path)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || !AllowMutation())
    {
        return;
    }

    const FString Reason = GetTalentSelectBlockReason(TargetTier, Path);

    // 위치·방향·거리 계산 조건 검사
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()))
    {
        return;
    }

    // 접속 권한·저장 리비전·멱등 키에 필요한 값 준비
    FFableClassProgress& Build = ClassProgress[int32(HeroClass)];
    (TargetTier == 2 ? Build.SecondTalent : Build.ThirdTalent) = Path;
    BumpClassBuildRevision();
    Changed(FString::Printf(TEXT("%d차 특성 선택: %s · 계열 %d단계. 직업을 바꿔도 선택은 유지됩니다."), TargetTier,
                            *FableRPG::TalentName(HeroClass, Path), GetTalentRank(Path)));
}

/** 초기화 조건과 비용을 확인해 현재 직업의 스킬·특성을 되돌린다. */
void AFablePlayerState::ResetCurrentClassBuild(EFableClass ExpectedClass, int32 ExpectedRevision)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || !AllowMutation())
    {
        return;
    }
    if (ExpectedClass != HeroClass || ExpectedRevision != ClassBuildRevision)
    {
        SetNotice(TEXT("확인 중 육성 정보가 바뀌었습니다. 내용을 다시 확인한 뒤 초기화하세요."));
        return;
    }

    const FString Reason = GetBuildResetBlockReason();

    // 위치·방향·거리 계산 조건 검사
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()))
    {
        return;
    }

    // 재화와 작업 비용에 필요한 값 준비
    const int32 Refunded = GetCurrentClassRefundableSkillPoints();
    FFableClassProgress& Build = ClassProgress[int32(HeroClass)];
    Build.Ranks = {1, 1, 1, 0};
    Build.SecondTalent = Build.ThirdTalent = EFableTalentPath::None;
    Gold -= GetBuildResetGoldCost();
    BumpClassBuildRevision();
    InvalidateTrialCasts();
    Changed(FString::Printf(
        TEXT("%s 육성을 초기화했습니다. %d포인트 반환 · 골드 50 사용. 전직·시험·다른 직업의 투자는 유지됩니다."),
        *FableRPG::ClassName(HeroClass), Refunded));
}
