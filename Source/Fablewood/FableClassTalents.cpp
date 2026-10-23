#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableGameState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

EFableTalentPath AFablePlayerState::GetTalentChoice(int32 TargetTier) const
{
    if (!ClassProgress.IsValidIndex(int32(HeroClass))) return EFableTalentPath::None;
    const FFableClassProgress& Build=ClassProgress[int32(HeroClass)];
    return TargetTier==2 ? Build.SecondTalent : TargetTier==3 ? Build.ThirdTalent : EFableTalentPath::None;
}
int32 AFablePlayerState::GetTalentRank(EFableTalentPath Path) const
{
    return ClassProgress.IsValidIndex(int32(HeroClass)) ? FableRPG::TalentRank(ClassProgress[int32(HeroClass)],Path) : 0;
}
FString AFablePlayerState::GetTalentSignature() const
{
    FString Result;
    for (const FFableClassProgress& Build : ClassProgress)
    {
        if (!Result.IsEmpty()) Result+=TEXT(";");
        Result+=FString::Printf(TEXT("%d,%d"),uint8(Build.SecondTalent),uint8(Build.ThirdTalent));
    }
    return Result;
}
void AFablePlayerState::BumpClassBuildRevision()
{
    ClassBuildRevision=ClassBuildRevision==MAX_int32 ? 1 : ClassBuildRevision+1;
}
FString AFablePlayerState::ClassBuildBlockReason() const
{
    if (!bProfileLoaded || bStorageBlocked) return TEXT("저장 프로필을 확인할 수 없어 육성을 변경하지 않았습니다.");
    if (!GetWorld() || !FableRPG::IsValidClassProgress(ClassProgress,Level,ForestQuest,DungeonCompletions)
        || !FableRPG::IsValidTalentProgress(ClassProgress) || uint8(HeroClass)>uint8(EFableClass::Archer))
        return TEXT("직업 정보를 읽을 수 없어 육성을 변경하지 않았습니다.");
    const APlayerController* OwnerPC=Cast<APlayerController>(GetOwner());
    const AFableCharacter* Hero=OwnerPC ? Cast<AFableCharacter>(OwnerPC->GetPawn()) : nullptr;
    const AFableGameState* GS=GetWorld()->GetGameState<AFableGameState>();
    if (!Hero || !Hero->IsAlive()) return TEXT("활동 가능한 상태로 로완 근처에서 육성을 변경하세요.");
    const FVector Delta=Hero->GetActorLocation()-FableRPG::BoardLocation();
    if (Delta.SizeSquared2D()>FMath::Square(330.0f) || FMath::Abs(Delta.Z)>=300.0f
        || !GS || GS->ActiveRegion!=EFableRegion::Village)
        return TEXT("마을의 안내인 로완 근처에서 육성을 변경하세요.");
    if (GS->bPartyRecoveryPending || GS->AdventureStage==EFableAdventureStage::Forest
        || GS->AdventureStage==EFableAdventureStage::Dungeon || GS->AdventureStage==EFableAdventureStage::Boss)
        return TEXT("진행 중인 전투와 구조가 끝난 뒤 육성을 변경하세요.");
    if (Hero->GetBuildChangeCooldownRemaining()>0.0f)
        return TEXT("전투와 스킬의 대기시간·보호 효과가 끝난 뒤 육성을 변경하세요.");
    return FString();
}
FString AFablePlayerState::GetTalentSelectBlockReason(int32 TargetTier, EFableTalentPath Path) const
{
    if ((TargetTier!=2 && TargetTier!=3) || (Path!=EFableTalentPath::First && Path!=EFableTalentPath::Second))
        return TEXT("알 수 없는 직업 특성입니다.");
    const FString Common=ClassBuildBlockReason();
    if (!Common.IsEmpty()) return Common;
    if (GetAdvancementTier()<TargetTier) return FString::Printf(TEXT("%d차 전직을 완료하면 선택할 수 있습니다."),TargetTier);
    if (TargetTier==3 && GetTalentChoice(2)==EFableTalentPath::None) return TEXT("2차 특성을 먼저 선택하세요.");
    const EFableTalentPath Selected=GetTalentChoice(TargetTier);
    if (Selected==Path) return TEXT("이미 선택한 특성입니다.");
    if (Selected!=EFableTalentPath::None) return TEXT("현재 직업을 초기화한 뒤 다른 특성을 선택할 수 있습니다.");
    return FString();
}
int32 AFablePlayerState::GetCurrentClassRefundableSkillPoints() const
{
    if (!ClassProgress.IsValidIndex(int32(HeroClass))) return 0;
    const FFableClassProgress& Build=ClassProgress[int32(HeroClass)];
    if (Build.Ranks.Num()!=4) return 0;
    int32 Result=0;
    for (int32 Slot=0;Slot<4;++Slot) Result+=Build.Ranks[Slot]-(Slot==3 ? 0 : 1);
    return FMath::Max(0,Result);
}
FString AFablePlayerState::GetBuildResetBlockReason() const
{
    const FString Common=ClassBuildBlockReason();
    if (!Common.IsEmpty()) return Common;
    if (GetCurrentClassRefundableSkillPoints()==0 && GetTalentChoice(2)==EFableTalentPath::None && GetTalentChoice(3)==EFableTalentPath::None)
        return TEXT("현재 직업에는 초기화할 투자나 특성이 없습니다.");
    if (Gold<GetBuildResetGoldCost()) return TEXT("육성 초기화에는 골드 50이 필요합니다.");
    return FString();
}
void AFablePlayerState::SelectTalent(int32 TargetTier, EFableTalentPath Path)
{
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || !AllowMutation()) return;
    const FString Reason=GetTalentSelectBlockReason(TargetTier,Path);
    if (!Reason.IsEmpty()) { SetNotice(Reason); return; }
    if (!AtTownService(FableRPG::BoardLocation())) return;
    FFableClassProgress& Build=ClassProgress[int32(HeroClass)];
    (TargetTier==2 ? Build.SecondTalent : Build.ThirdTalent)=Path;
    BumpClassBuildRevision();
    Changed(FString::Printf(TEXT("%d차 특성 선택: %s · 계열 %d단계. 직업을 바꿔도 선택은 유지됩니다."),TargetTier,
        *FableRPG::TalentName(HeroClass,Path),GetTalentRank(Path)));
}
void AFablePlayerState::ResetCurrentClassBuild(EFableClass ExpectedClass, int32 ExpectedRevision)
{
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || !AllowMutation()) return;
    if (ExpectedClass!=HeroClass || ExpectedRevision!=ClassBuildRevision)
    { SetNotice(TEXT("확인 중 육성 정보가 바뀌었습니다. 내용을 다시 확인한 뒤 초기화하세요.")); return; }
    const FString Reason=GetBuildResetBlockReason();
    if (!Reason.IsEmpty()) { SetNotice(Reason); return; }
    if (!AtTownService(FableRPG::BoardLocation())) return;
    const int32 Refunded=GetCurrentClassRefundableSkillPoints();
    FFableClassProgress& Build=ClassProgress[int32(HeroClass)];
    Build.Ranks={1,1,1,0}; Build.SecondTalent=Build.ThirdTalent=EFableTalentPath::None;
    Gold-=GetBuildResetGoldCost(); BumpClassBuildRevision(); InvalidateTrialCasts();
    Changed(FString::Printf(TEXT("%s 육성을 초기화했습니다. %d포인트 반환 · 골드 50 사용. 전직·시험·다른 직업의 투자는 유지됩니다."),
        *FableRPG::ClassName(HeroClass),Refunded));
}
