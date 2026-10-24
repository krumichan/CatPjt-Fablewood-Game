#include "FablePlayerState.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameState.h"
#include "FableAdventure.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
    /** 현재 이야기 장의 보상과 다음 진행 단계를 구성한다. */
    void StoryReward(int32 Chapter, int32& Experience, int32& Gold, int32& Ore)
    {
        // 재화와 작업 비용에 필요한 값 준비
        static const int32 ExperienceValues[] = {20, 60, 60, 80, 100};
        static const int32 GoldValues[] = {20, 40, 50, 70, 100};
        static const int32 OreValues[] = {0, 3, 3, 4, 5};
        Experience = Gold = Ore = 0;

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Chapter < 0 || Chapter >= 5)
        {
            return;
        }

        // 재화와 작업 비용 처리
        Experience = ExperienceValues[Chapter];
        Gold = GoldValues[Chapter];
        Ore = OreValues[Chapter];
    }

    /** 이야기 상호작용에 필요한 플레이어 캐릭터를 찾는다. */
    const AFableCharacter* StoryHero(const AFablePlayerState* State)
    {
        const APlayerController* Controller = Cast<APlayerController>(State->GetOwner());
        return Controller ? Cast<AFableCharacter>(Controller->GetPawn()) : nullptr;
    }
}

/** 이야기 준비 완료 여부를 확인한다. */
bool AFablePlayerState::IsStoryReady() const
{
    if (!bProfileLoaded)
    {
        return false;
    }
    switch (StoryChapter)
    {
    // 퀘스트와 이야기 진행 처리
    case 0:
        return true;
    case 1:
        return (StoryObjectMask & 15) == 15;
    case 2:
        return ForestQuest == EFableQuestStage::Completed;
    case 3:
        return (StoryObjectMask & 16) != 0 && DungeonCompletions >= 1;
    case 4:
        return StorySealMask == 7 && MoonwellCompletions >= 1;
    default:
        return false;
    }
}

/** 사용 이야기 오브젝트에 필요한 조건을 만족하는지 확인한다. */
bool AFablePlayerState::CanUseStoryObject(int32 Index) const
{
    if (!bProfileLoaded || Index < 0 || Index >= 5 || (StoryObjectMask & (1 << Index)) != 0)
    {
        return false;
    }
    return (StoryChapter == 1 && Index < 4) || (StoryChapter == 3 && Index == 4);
}

/** 기록 이야기 위치에 필요한 조건을 만족하는지 확인한다. */
bool AFablePlayerState::CanRecordStoryAt(EFableRegion Region, const FVector& Location) const
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || !GetWorld())
    {
        return false;
    }
    if (IsBackendEnabled() && !IsBackendReady())
    {
        return false;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AFableCharacter* Hero = StoryHero(this);
    const auto Adventure = FableAdventure::For(this);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Hero || !Hero->IsAlive() || (Adventure && Adventure->bPartyRecoveryPending))
    {
        return false;
    }

    const FVector Position = Hero->GetActorLocation();

    const auto View = FableAdventure::For(this);
    const FVector Target = View.ToWorld(Region, Location);
    return View && View.IsInRegion(Region, Position) &&
           FVector::DistSquared2D(Position, Target) <= FMath::Square(330.0f) &&
           FMath::Abs(Position.Z - Target.Z) < 300.0f;
}

/** 위치와 진행 조건이 맞는 이야기 오브젝트 상호작용을 기록한다. */
void AFablePlayerState::RecordStoryObject(int32 Index)
{
    // The ID selects an authored world object. Clients never submit coordinates or progress counts.
    // 퀘스트와 이야기 진행 조건 검사
    if (!CanUseStoryObject(Index) ||
        !CanRecordStoryAt(FableRPG::StoryObjectRegion(Index), FableRPG::StoryObjectLocation(Index)))
    {
        return;
    }

    // 퀘스트와 이야기 진행 처리
    StoryObjectMask |= 1 << Index;
    UE_LOG(LogTemp, Display, TEXT("FABLE_STORY_OBJECT slot=%s chapter=%d object=%d mask=%d"), *ProfileSlot,
           StoryChapter, Index, StoryObjectMask);
    Changed(FableRPG::StoryObjectName(Index) + (Index < 3 ? TEXT("을 모았습니다. ") : TEXT("을 조사했습니다. ")) +
            GetStoryObjective());
}

/** 정화한 봉인의 결과를 이야기 목표 진행에 반영한다. */
void AFablePlayerState::RecordStorySeal(int32 Index)
{
    // 퀘스트와 이야기 진행 조건 검사
    if (StoryChapter != 4 || Index < 0 || Index >= 3 || (StorySealMask & (1 << Index)) != 0 ||
        !CanRecordStoryAt(EFableRegion::Moonwell, FableRPG::MoonSealLocation(Index)))
    {
        return;
    }
    // Only a completed server encounter seal can be observed, including a personal reinspection.

    const auto Adventure = FableAdventure::For(this);

    // 지역·적·전투 진행 조건 검사
    if (!Adventure || Adventure->ActiveRegion != EFableRegion::Moonwell ||
        (Adventure->MoonSealMask & (1 << Index)) == 0)
    {
        return;
    }

    // 퀘스트와 이야기 진행 처리
    StorySealMask |= 1 << Index;
    UE_LOG(LogTemp, Display, TEXT("FABLE_STORY_SEAL slot=%s seal=%d mask=%d"), *ProfileSlot, Index, StorySealMask);
    Changed(TEXT("정화된 봉인에 남은 달빛을 확인했습니다. ") + GetStoryObjective());
}

/** 이야기 보상과 장 진행을 함께 변경하고 저장한다. */
bool AFablePlayerState::CommitStoryReward(int32 ExpectedChapter)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || StoryChapter != ExpectedChapter || !IsStoryReady() ||
        !FableRPG::IsValidStoryProgress(StoryChapter, StoryObjectMask, StorySealMask, ForestQuest, DungeonCompletions,
                                        MoonwellCompletions))
    {
        return false;
    }

    // 재화와 작업 비용에 필요한 값 준비
    int32 ExperienceReward = 0, GoldReward = 0, OreReward = 0;
    StoryReward(StoryChapter, ExperienceReward, GoldReward, OreReward);
    // Chapter is the permanent receipt. Commit it with its reward, before any save or notification.
    ++StoryChapter;
    Gold = FMath::Min(1000000, Gold + GoldReward);
    Ore = FMath::Min(100000, Ore + OreReward);
    AddExperience(ExperienceReward);
    UE_LOG(LogTemp, Display, TEXT("FABLE_STORY_REPORT slot=%s chapter=%d xp=%d gold=%d ore=%d"), *ProfileSlot,
           ExpectedChapter, ExperienceReward, GoldReward, OreReward);
    Changed(FString::Printf(TEXT("이야기 완료: 경험치 %d · 골드 %d · 광석 %d. %s"), ExperienceReward, GoldReward,
                            OreReward, *GetStoryObjective()));

    return true;
}

/** 이야기 장의 수락·진행·보상 수령을 처리한다. */
void AFablePlayerState::InteractStory()
{
    // 퀘스트와 이야기 진행 조건 검사
    if (!AllowMutation() || bStorageBlocked)
    {
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()))
    {
        SetNotice(TEXT("살아 있는 상태로 안내인 로완 가까이에서 이야기를 나누세요."));
        return;
    }
    if (StoryChapter >= 5)
    {
        SetNotice(TEXT("숲 마을의 등불을 되살렸습니다. 의뢰와 공방에서 새로운 모험을 이어가세요."));
        return;
    }
    if (!IsStoryReady())
    {
        SetNotice(GetStoryObjective());
        return;
    }

    CommitStoryReward(StoryChapter);
}

/** 이야기 장의 제목 값을 반환한다. */
FString AFablePlayerState::GetStoryTitle() const
{
    static const TCHAR* Titles[] = {
        TEXT("메인 이야기 · 숲 마을의 초대"), TEXT("1장 · 꺼져 가는 등불"), TEXT("2장 · 숲이 남긴 흔적"),
        TEXT("3장 · 뿌리 깊은 약속"),         TEXT("4장 · 돌아온 달빛"),    TEXT("메인 이야기 · 다시 밝아진 숲")};
    return StoryChapter >= 0 && StoryChapter <= 5 ? Titles[StoryChapter] : TEXT("알 수 없는 이야기");
}

/** 이야기 장의 설명 값을 반환한다. */
FString AFablePlayerState::GetStoryDescription() const
{
    static const TCHAR* Descriptions[] = {
        TEXT("숲 마을의 등불이 하나씩 빛을 잃고 있습니다. 안내인 로완은 막 도착한 모험가에게 작은 도움을 청합니다."),
        TEXT("등불꽃 세 송이를 모으고 북쪽 나무뿌리의 흔적을 조사하세요. 꽃에 남은 빛은 마을을 지킬 단서가 됩니다."),
        TEXT("나무뿌리에서 수호자들의 불안한 기운을 발견했습니다. 숲 순찰 의뢰를 마치고 로완에게 그 이유를 전하세요."),
        TEXT(
            "뿌리 유적의 문양돌에는 숲과 달빛 수원을 잇는 오래된 약속이 새겨져 있습니다. 문양돌을 조사하고 고대 수호자의 의뢰를 완료하세요."),
        TEXT(
            "달빛 수원의 세 봉인이 마을로 흐르는 빛을 가로막았습니다. 정화된 봉인을 각각 확인하고 수원의 의뢰를 보고하면 등불이 다시 밝아집니다."),
        TEXT(
            "숲 마을부터 뿌리 유적과 달빛 수원까지 이어진 약속을 되찾았습니다. 동료와 반복 의뢰, 전직 시험, 공방과 더 어려운 원정을 이어가세요.")};
    return StoryChapter >= 0 && StoryChapter <= 5 ? Descriptions[StoryChapter]
                                                  : TEXT("이야기 진행을 읽을 수 없습니다.");
}

/** 이야기 장의 현재 목표 값을 반환한다. */
FString AFablePlayerState::GetStoryObjective() const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (StoryChapter == 5)
    {
        return TEXT("메인 이야기 완료 · 반복 의뢰와 전직 시험을 이어갈 수 있습니다.");
    }
    if (IsStoryReady())
    {
        return StoryChapter == 0 ? TEXT("로완에게 E로 말을 걸어 메인 이야기를 시작하세요.")
                                 : TEXT("로완에게 돌아가 메인 이야기 보상을 받으세요.");
    }
    if (StoryChapter == 1)
    {
        const int32 Flowers =
            ((StoryObjectMask & 1) != 0) + ((StoryObjectMask & 2) != 0) + ((StoryObjectMask & 4) != 0);
        return FString::Printf(TEXT("등불꽃 %d/3 · 나무뿌리 조사 %d/1. 대상 가까이에서 E를 누르세요."), Flowers,
                               (StoryObjectMask & 8) != 0);
    }
    if (StoryChapter == 2)
    {
        return TEXT("숲 순찰 의뢰를 수락하고 수호자 3마리를 처치한 뒤 로완에게 의뢰 보상을 받으세요.");
    }
    if (StoryChapter == 3)
    {
        return FString::Printf(TEXT("유적 문양돌 조사 %d/1 · 뿌리 유적 의뢰 보고 %d/1."), (StoryObjectMask & 16) != 0,
                               FMath::Min(DungeonCompletions, 1));
    }
    if (StoryChapter == 4)
    {
        const int32 Seals = ((StorySealMask & 1) != 0) + ((StorySealMask & 2) != 0) + ((StorySealMask & 4) != 0);
        return FString::Printf(TEXT("정화된 봉인 가까이에서 E로 확인 %d/3 · 달빛 수원 의뢰 보고 %d/1."), Seals,
                               FMath::Min(MoonwellCompletions, 1));
    }

    return TEXT("로완에게 현재 이야기를 확인하세요.");
}

/** 이야기 보상의 안내 문구 값을 반환한다. */
FString AFablePlayerState::GetStoryRewardText() const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (StoryChapter >= 5)
    {
        return TEXT("모든 메인 이야기 보상을 받았습니다. 기존 저장의 완료 구간은 추가 보상 없이 보존됩니다.");
    }

    // 재화와 작업 비용에 필요한 값 준비
    int32 ExperienceReward = 0, GoldReward = 0, OreReward = 0;
    StoryReward(StoryChapter, ExperienceReward, GoldReward, OreReward);

    return FString::Printf(TEXT("이야기 보상: 경험치 %d · 골드 %d · 광석 %d (한 번만 지급)"), ExperienceReward,
                           GoldReward, OreReward);
}

/** 이야기 목표의 위치 값을 반환한다. */
bool AFablePlayerState::GetStoryTarget(FVector& OutLocation, FString& OutLabel, EFableRegion* OutRegion) const
{
    // 화면 표시와 사용자 안내 처리
    OutLocation = FableRPG::BoardLocation();
    OutLabel = TEXT("안내인 로완");
    if (OutRegion)
    {
        *OutRegion = EFableRegion::Village;
    }

    // 퀘스트와 이야기 진행 조건 검사
    if (!bProfileLoaded || StoryChapter < 0 || StoryChapter >= 5)
    {
        return false;
    }

    // 지역·적·전투 진행에 필요한 값 준비
    const AFableCharacter* Hero = StoryHero(this);
    const auto View = FableAdventure::For(this);
    if (!View)
    {
        return false;
    }
    const FVector Position = Hero ? Hero->GetActorLocation() : FableRPG::BoardLocation();
    const EFableRegion Current = View.IsInRegion(EFableRegion::Moonwell, Position)    ? EFableRegion::Moonwell
                                 : View.IsInRegion(EFableRegion::RootRuins, Position) ? EFableRegion::RootRuins
                                                                                      : EFableRegion::Village;
    EFableRegion Destination = EFableRegion::Village;
    const auto SeekEnemy = [&](EFableRegion Region)
    {
        // 지역·적·전투 진행 처리
        Destination = Region;
        OutLocation = Region == EFableRegion::Village ? FVector(0, 650, 0) : FableRPG::RegionCenter(Region);
        OutLabel = Region == EFableRegion::Village ? TEXT("숲의 수호자") : TEXT("원정의 수호자");
        float Nearest = TNumericLimits<float>::Max();

        // 지역·적·전투 진행 처리
        if (GetWorld())
        {
            for (TActorIterator<AFableEnemy> It(GetWorld()); It; ++It)
            {
                if (!It->IsAlive() || !FableAdventure::SameScope(this, *It) ||
                    !View.IsInRegion(Region, It->GetActorLocation()))
                {
                    continue;
                }
                const float Distance = FVector::DistSquared2D(Position, It->GetActorLocation());
                if (Distance < Nearest)
                {
                    Nearest = Distance;
                    OutLocation = View.ToCanonical(Region, It->GetActorLocation());
                }
            }
        }
    };

    // 퀘스트와 이야기 진행 처리
    if (!IsStoryReady())
    {
        if (StoryChapter == 1)
        {
            for (int32 Index = 0; Index < 4; ++Index)
            {
                if (CanUseStoryObject(Index))
                {
                    OutLocation = FableRPG::StoryObjectLocation(Index);
                    OutLabel = FableRPG::StoryObjectName(Index);
                    break;
                }
            }
        }
        else if (StoryChapter == 2 && ForestQuest == EFableQuestStage::Active)
        {
            SeekEnemy(EFableRegion::Village);
        }
        else if (StoryChapter == 3)
        {
            if (CanUseStoryObject(4) &&
                (Current == EFableRegion::RootRuins || DungeonQuest == EFableQuestStage::Active))
            {
                Destination = EFableRegion::RootRuins;
                OutLocation = FableRPG::StoryObjectLocation(4);
                OutLabel = FableRPG::StoryObjectName(4);
            }
            else if (DungeonCompletions < 1 && DungeonQuest == EFableQuestStage::Active)
            {
                SeekEnemy(EFableRegion::RootRuins);
            }
        }
        else if (StoryChapter == 4)
        {
            if (StorySealMask != 7 && (Current == EFableRegion::Moonwell || MoonwellQuest == EFableQuestStage::Active))
            {
                Destination = EFableRegion::Moonwell;
                for (int32 Index = 0; Index < 3; ++Index)
                {
                    if ((StorySealMask & (1 << Index)) == 0)
                    {
                        OutLocation = FableRPG::MoonSealLocation(Index);
                        OutLabel = FString::Printf(TEXT("달빛 봉인 %d 확인"), Index + 1);
                        break;
                    }
                }
            }
            else if (MoonwellCompletions < 1 && MoonwellQuest == EFableQuestStage::Active)
            {
                SeekEnemy(EFableRegion::Moonwell);
            }
        }
    }

    EFableRegion TargetRegion = Destination;

    // 지역·적·전투 진행 처리
    if (Current != Destination)
    {
        if (Current == EFableRegion::Village)
        {
            TargetRegion = EFableRegion::Village;
            OutLocation = FableRPG::PortalLocation();
            OutLabel = FableRPG::RegionName(Destination) + TEXT(" 입장 차원문");
        }
        else
        {
            TargetRegion = Current;
            OutLocation = FableRPG::RegionReturnPortal(Current);
            OutLabel = TEXT("마을 귀환 차원문");
        }
    }

    if (OutRegion)
    {
        *OutRegion = TargetRegion;
    }
    return true;
}
