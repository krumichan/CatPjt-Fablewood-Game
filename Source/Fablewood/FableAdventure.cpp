#include "FableAdventure.h"
#include "FableCharacter.h"
#include "FableDungeonWorld.h"
#include "FableEnemy.h"
#include "FableEnemyBolt.h"
#include "FableGameState.h"
#include "FableMoonwellWorld.h"
#include "FablePartyEncounter.h"
#include "FablePlayerState.h"
#include "FableProjectile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"

namespace
{
    /** 복제 순서와 관계없이 UUID가 정확히 같은 확정 원정만 찾는다. */
    AFablePartyEncounter* FindEncounter(const UWorld* World, FGuid Id)
    {
        if (World && Id.IsValid())
        {
            for (TActorIterator<AFablePartyEncounter> It(const_cast<UWorld*>(World)); It; ++It)
            {
                if (IsValid(*It) && It->GetExpeditionId() == Id && It->IsCommitted())
                {
                    return *It;
                }
            }
        }

        return nullptr;
    }

    /** 일반 컨트롤러도 소유한 실제 프로필로 진행을 조회한다. */
    const AFablePlayerState* ProfileFor(const AActor* Context)
    {
        if (const AFablePlayerState* Profile = Cast<AFablePlayerState>(Context))
        {
            return Profile;
        }
        if (const AController* Controller = Cast<AController>(Context))
        {
            return Controller->GetPlayerState<AFablePlayerState>();
        }
        if (const APawn* Pawn = Cast<APawn>(Context))
        {
            return Pawn->GetPlayerState<AFablePlayerState>();
        }

        return nullptr;
    }
}

/** 조회한 진행 값과 실제 수명 소유자를 연결한다. */
FFableAdventureView::FFableAdventureView(const FFableAdventureProgress* InProgress, AActor* InOwner,
                                         AFableGameState* InClock, FGuid InExpeditionId, FVector InOrigin,
                                         EFableRegion InRegion)
    : Progress(InProgress), OwnerActor(InOwner), Clock(InClock), ScopeId(InExpeditionId), Origin(InOrigin),
      ScopeRegion(InRegion)
{
}

/** 소유 Actor가 파괴되면 보관된 진행 포인터를 사용하지 않는다. */
FFableAdventureView::operator bool() const
{
    return Progress && OwnerActor.IsValid() && Clock.IsValid();
}

/** 유효한 참조에서만 읽기 전용 진행을 반환한다. */
const FFableAdventureProgress* FFableAdventureView::operator->() const
{
    return *this ? Progress : nullptr;
}

/** 접속 객체와 원정 증서가 일치하는 현재 플레이어만 조회한다. */
TArray<APlayerState*> FFableAdventureView::Players() const
{
    TArray<APlayerState*> Result;
    if (!*this)
    {
        return Result;
    }

    const AFablePartyEncounter* Encounter = Cast<AFablePartyEncounter>(OwnerActor.Get());
    for (TActorIterator<AFablePlayerState> It(OwnerActor->GetWorld()); It; ++It)
    {
        if (It->IsInactive())
        {
            continue;
        }
        if (ScopeId.IsValid() ? !Encounter || !Encounter->IsParticipant(*It) : It->ActiveExpeditionId.IsValid())
        {
            continue;
        }

        Result.Add(*It);
    }

    return Result;
}

/** 진행 Actor마다 별도 시계를 만들지 않는다. */
double FFableAdventureView::ServerTime() const
{
    return Clock.IsValid() ? Clock->GetServerWorldTimeSeconds() : 0;
}

/** 선택한 진행의 복구 카운트다운만 읽는다. */
float FFableAdventureView::RecoveryRemaining() const
{
    return *this && Progress->bPartyRecoveryPending
               ? FMath::Max(0.0f, Progress->PartyRecoveryEndServerTime - float(ServerTime()))
               : 0;
}

/** 원정과 마을 사이의 공유 좌표 경계를 유지한다. */
FVector FFableAdventureView::ToWorld(EFableRegion Region, FVector Canonical) const
{
    return ScopeId.IsValid() && Region == ScopeRegion ? Canonical + Origin : Canonical;
}

/** 미니맵과 지역 검사는 원본 좌표로 한 번만 되돌린다. */
FVector FFableAdventureView::ToCanonical(EFableRegion Region, FVector World) const
{
    return ScopeId.IsValid() && Region == ScopeRegion ? World - Origin : World;
}

/** 원정 밖의 다른 던전을 같은 지역으로 인정하지 않는다. */
bool FFableAdventureView::IsInRegion(EFableRegion Region, FVector World) const
{
    if (!*this || (ScopeId.IsValid() && Region != ScopeRegion && Region != EFableRegion::Village))
    {
        return false;
    }

    return FableRPG::IsInRegion(Region, ToCanonical(Region, World));
}

/** 필드와 원정을 구분하는 불변 식별자를 읽는다. */
FGuid FFableAdventureView::ExpeditionId() const
{
    return ScopeId;
}

/** 이 원정의 배경 및 전투 좌표 원점을 읽는다. */
FVector FFableAdventureView::WorldOrigin() const
{
    return Origin;
}

/** 변경 통지와 수명 확인의 실제 소유 Actor를 읽는다. */
AActor* FFableAdventureView::ReplicationOwner() const
{
    return OwnerActor.Get();
}

/** 필드 진행은 반드시 명시적으로 선택한다. */
FFableAdventureView FableAdventure::Field(const UWorld* World)
{
    AFableGameState* Root = World ? World->GetGameState<AFableGameState>() : nullptr;
    return Root ? FFableAdventureView(&Root->FieldProgress, Root, Root, FGuid(), FVector::ZeroVector,
                                      Root->FieldProgress.ActiveRegion)
                : FFableAdventureView();
}

/** UUID를 가진 대상은 해결 실패 시 필드 진행으로 돌아가지 않는다. */
FFableAdventureView FableAdventure::For(const AActor* Context)
{
    if (!IsValid(Context) || !Context->GetWorld())
    {
        return {};
    }

    const UWorld* World = Context->GetWorld();
    const AFablePlayerState* Profile = ProfileFor(Context);
    if (!Profile && (Cast<AController>(Context) || Cast<APawn>(Context)))
    {
        return {};
    }
    FGuid Id;
    if (Profile)
    {
        Id = Profile->ActiveExpeditionId;
    }
    else if (const AFablePartyEncounter* Encounter = Cast<AFablePartyEncounter>(Context))
    {
        Id = Encounter->GetExpeditionId();
    }
    else if (const AFableEnemy* Enemy = Cast<AFableEnemy>(Context))
    {
        Id = Enemy->GetExpeditionId();
    }
    else if (const AFableProjectile* Shot = Cast<AFableProjectile>(Context))
    {
        Id = Shot->GetExpeditionId();
    }
    else if (const AFableEnemyBolt* Bolt = Cast<AFableEnemyBolt>(Context))
    {
        Id = Bolt->GetExpeditionId();
    }
    else if (const AFableDungeonWorld* Dungeon = Cast<AFableDungeonWorld>(Context))
    {
        Id = Dungeon->GetExpeditionId();
    }
    else if (const AFableMoonwellWorld* Moonwell = Cast<AFableMoonwellWorld>(Context))
    {
        Id = Moonwell->GetExpeditionId();
    }

    if (!Id.IsValid())
    {
        return Field(World);
    }

    AFablePartyEncounter* Encounter = FindEncounter(World, Id);
    AFableGameState* Root = World->GetGameState<AFableGameState>();
    if (!Encounter || !Root || (Profile && !Encounter->IsParticipant(Profile)))
    {
        return {};
    }

    return FFableAdventureView(&Encounter->GetProgress(), Encounter, Root, Id, Encounter->GetWorldOrigin(),
                               Encounter->GetTicket().Region);
}

/** 불완전한 원정 참조はフィールドとも一致しない。 */
bool FableAdventure::SameScope(const AActor* A, const AActor* B)
{
    if (!IsValid(A) || !IsValid(B) || A->GetWorld() != B->GetWorld())
    {
        return false;
    }

    const FFableAdventureView First = For(A);
    const FFableAdventureView Second = For(B);
    return First && Second && First.ReplicationOwner() == Second.ReplicationOwner() &&
           First.ExpeditionId() == Second.ExpeditionId();
}

/** 같은 UUID뿐 아니라 현재 지역의 실제 위치도 검사한다. */
bool FableAdventure::CanAffect(const AActor* Source, const AActor* Target)
{
    if (!SameScope(Source, Target))
    {
        return false;
    }

    const FFableAdventureView View = For(Source);
    const EFableRegion Region = View->ActiveRegion;
    // 컨트롤러와 프로필의 Actor 원점은 캐릭터 위치가 아니므로 양쪽 모두 실제 pawn을 사용한다.
    const auto Position = [&](const AActor* Actor, FVector& Out)
    {
        const AController* Controller = Cast<AController>(Actor);
        if (const AFablePlayerState* Profile = Cast<AFablePlayerState>(Actor))
        {
            Controller = Cast<AController>(Profile->GetOwner());
        }
        if (Controller)
        {
            if (!Controller->GetPawn())
            {
                return false;
            }
            Out = Controller->GetPawn()->GetActorLocation();
            return true;
        }
        Out = Actor->GetActorLocation();
        return true;
    };

    FVector SourceLocation;
    FVector TargetLocation;
    return Position(Source, SourceLocation) && Position(Target, TargetLocation) &&
           View.IsInRegion(Region, SourceLocation) && View.IsInRegion(Region, TargetLocation);
}
