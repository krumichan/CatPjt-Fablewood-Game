#include "FablePartyManager.h"
#include "FableCharacter.h"
#include "FableEnemy.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "FablePlayerState.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

/** 실시간 대기 없이 서버 요청 간격과 만료 경계만 제어하는 검증 도우미. */
struct FFablePartyTestAccess
{
    /** 전투와 보상 경로도 실제 슬롯에 저장하지 않는 메모리 전용 프로필을 사용한다. */
    static void UseMemoryOnly(AFablePlayerState* Profile)
    {
        Profile->bPersistenceEnabled = false;
        Profile->ProfileSlot.Empty();
        Profile->bStorageBlocked = false;
    }

    /** 장비 마모나 처치 보상이 파일 저장으로 이어지지 않는지 확인한다. */
    static bool IsMemoryOnly(const AFablePlayerState* Profile)
    {
        return Profile && !Profile->bPersistenceEnabled && Profile->ProfileSlot.IsEmpty();
    }

    /** 검증 월드의 정식 상태를 서버 규칙 객체에 연결한다. */
    static void AttachState(AFableGameMode* Mode, AFableGameState* State)
    {
        Mode->GameState = State;
        Mode->GetWorld()->SetGameState(State);
    }

    /** 서로 다른 사용자 동작 사이의 요청 간격만 초기화한다. */
    static void AllowNextRequest(AFablePartyManager* Manager)
    {
        Manager->LastRequestTimes.Reset();
    }

    /** 서버 타이머와 같은 만료 정리 경로를 실행한다. */
    static void Expire(AFablePartyManager* Manager)
    {
        Manager->ExpireEntries();
    }

    /** 초대의 정확한 만료 시각 경계를 재현한다. */
    static void SetInviteDeadline(AFablePartyManager* Manager, FGuid InviteId, float Deadline)
    {
        for (FFablePartyInvite& Invite : Manager->Invitations)
        {
            if (Invite.InviteId == InviteId)
            {
                Invite.ExpiresAt = Deadline;
            }
        }
    }

    /** 오프라인 예약의 정확한 만료 시각 경계를 재현한다. */
    static void SetReconnectDeadline(AFablePartyManager* Manager, const FString& PlayerKey, float Deadline)
    {
        for (FFablePartyRecord& Party : Manager->Parties)
        {
            for (FFablePartyMember& Member : Party.Members)
            {
                if (Member.PlayerKey == PlayerKey)
                {
                    Member.ReconnectDeadline = Deadline;
                }
            }
        }
    }
};

namespace
{
    /** 검증 전용 연결과 프로필을 함께 보관한다. */
    struct FPartyTestPlayer
    {
        APlayerController* Controller = nullptr;
        AFablePlayerState* Profile = nullptr;
        AFableCharacter* Hero = nullptr;

        /** 이후 검증에서 안전하게 사용할 수 있는 생성 결과인지 확인한다. */
        bool IsValid() const
        {
            return ::IsValid(Controller) && ::IsValid(Profile);
        }
    };

    /** 실제 계정·파일·네트워크를 사용하지 않는 파티 판정용 격리 월드. */
    struct FPartyTestWorld
    {
        UWorld* World = nullptr;
        AFableGameState* State = nullptr;
        AFablePartyManager* Manager = nullptr;
        AFableGameMode* Mode = nullptr;
        TArray<TWeakObjectPtr<APlayerController>> RegisteredControllers;
        bool bOwnsEngineContext = false;

        /** 오디오·저장을 끄고 입장 충돌 검사에 필요한 경우에만 물리 월드를 준비한다. */
        explicit FPartyTestWorld(bool bPhysics = false)
        {
            if (!GEngine)
            {
                return;
            }

            const UWorld::InitializationValues Values = UWorld::InitializationValues()
                                                            .RequiresHitProxies(false)
                                                            .CreatePhysicsScene(bPhysics)
                                                            .ShouldSimulatePhysics(false)
                                                            .EnableTraceCollision(bPhysics)
                                                            .CreateNavigation(false)
                                                            .CreateAISystem(false)
                                                            .AllowAudioPlayback(false);
            World =
                UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
            if (World)
            {
                // 접속 종료로 actor를 파괴할 때도 유효한 엔진 월드 문맥을 유지한다.
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                bOwnsEngineContext = true;
                State = World->SpawnActor<AFableGameState>();
                Manager = World->SpawnActor<AFablePartyManager>();
                World->SetGameState(State);
            }
        }

        /** 검증 객체와 등록된 연결을 월드와 함께 해제한다. */
        ~FPartyTestWorld()
        {
            if (World)
            {
                // 초기화되지 않은 격리 월드는 연결 목록도 명시적으로 정리한다.
                for (const TWeakObjectPtr<APlayerController>& Entry : RegisteredControllers)
                {
                    if (APlayerController* Controller = Entry.Get())
                    {
                        World->RemoveController(Controller);
                    }
                }

                World->DestroyWorld(false);

                // actor와 월드 정리가 끝난 뒤 이 fixture가 만든 문맥만 제거한다.
                if (GEngine && bOwnsEngineContext)
                {
                    GEngine->DestroyWorldContext(World);
                }

                World = nullptr;
            }
        }

        /** 인증 완료된 로컬 프로필을 만들고 실제 명부 등록 경로를 호출한다. */
        FPartyTestPlayer AddPlayer(int32 PlayerId, bool bLoaded = true, bool bRegister = true)
        {
            FPartyTestPlayer Player;
            if (!World || !Manager)
            {
                return Player;
            }

            Player.Controller = World->SpawnActor<APlayerController>();
            Player.Profile = World->SpawnActor<AFablePlayerState>();
            if (Player.IsValid())
            {
                Player.Profile->SetPlayerId(PlayerId);
                Player.Profile->SetPlayerName(FString::Printf(TEXT("PartyTest%d"), PlayerId));
                Player.Profile->bProfileLoaded = bLoaded;
                FFablePartyTestAccess::UseMemoryOnly(Player.Profile);
                Player.Profile->SetOwner(Player.Controller);
                Player.Controller->SetPlayerState(Player.Profile);

                // PostInitializeComponents를 생략한 fixture도 실제 서버 조회 경로에 연결한다.
                World->AddController(Player.Controller);
                RegisteredControllers.Add(Player.Controller);

                if (bRegister)
                {
                    Manager->RegisterPlayer(Player.Controller);
                }
            }

            return Player;
        }

        /** 실제 서버의 입장 메서드를 파일이나 서버 연결 없이 검증한다. */
        bool AddMode()
        {
            Mode = World ? World->SpawnActor<AFableGameMode>() : nullptr;
            if (Mode && State)
            {
                FFablePartyTestAccess::AttachState(Mode, State);
            }

            return Mode && State;
        }

        /** 생존·위치·지역 자격 검사는 실제 캐릭터와 소유 프로필을 사용한다. */
        bool AddHero(FPartyTestPlayer& Player, const FVector& Location)
        {
            if (!World || !Player.IsValid())
            {
                return false;
            }

            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player.Hero = World->SpawnActor<AFableCharacter>(Location, FRotator::ZeroRotator, Parameters);
            if (!Player.Hero)
            {
                return false;
            }

            Player.Controller->Possess(Player.Hero);
            Player.Profile->ForestQuest = EFableQuestStage::Completed;
            Player.Profile->DungeonQuest = EFableQuestStage::Active;
            Player.Profile->MoonwellQuest = EFableQuestStage::Active;
            Player.Profile->DungeonCompletions = 1;
            return Player.Hero->IsAlive() && Player.Hero->GetPlayerState() == Player.Profile;
        }

        /** 준비 요청마다 최신 서버 선택을 사용한다. */
        bool Ready(const FPartyTestPlayer& Player, bool bReady = true)
        {
            const FFablePartyRecord* Party = Manager->GetPartyForMember(Player.Profile->PartyPlayerKey);
            return Party &&
                   Manager->SetExpeditionReady(Player.Controller, Party->PartyId, Party->PreparationRevision, bReady);
        }

        /** 캐시된 준비 리비전 대신 현재 서버 명부를 읽어 입장을 요청한다. */
        bool Enter(const FPartyTestPlayer& Player)
        {
            const FFablePartyRecord* Party = Manager->GetPartyForMember(Player.Profile->PartyPlayerKey);
            return Party && Mode &&
                   Mode->RequestStartExpedition(Player.Controller, Party->PartyId, Party->PreparationRevision);
        }

        /** 전체 필드에 생성된 실제 적의 수를 확인한다. */
        int32 EnemyCount() const
        {
            int32 Count = 0;
            for (TActorIterator<AFableEnemy> It(World); It; ++It)
            {
                Count += It->IsAlive() ? 1 : 0;
            }

            return Count;
        }

        /** 실제 서버 조회에 사용하는 연결 목록에 컨트롤러가 한 번만 등록됐는지 확인한다. */
        bool IsControllerRegistered(const APlayerController* Player) const
        {
            int32 Matches = 0;
            if (World && Player)
            {
                for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
                {
                    if (It->Get() == Player)
                    {
                        ++Matches;
                    }
                }
            }

            return Matches == 1;
        }

        /** 이전 사용자 동작과 분리된 요청을 서버에 전달한다. */
        bool Act(const FPartyTestPlayer& Player, EFablePartyAction Action, int32 TargetId = INDEX_NONE,
                 FGuid InviteId = FGuid())
        {
            FFablePartyTestAccess::AllowNextRequest(Manager);
            return Manager->ExecuteAction(Player.Controller, Action, TargetId, InviteId);
        }

        /** 초대를 받은 사용자가 일회성 초대장으로 파티에 참여한다. */
        bool Join(const FPartyTestPlayer& Leader, const FPartyTestPlayer& Member)
        {
            if (!Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()))
            {
                return false;
            }

            const FFablePartyInvite* Invite = Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
            return Invite && Act(Member, EFablePartyAction::Accept, INDEX_NONE, Invite->InviteId);
        }

        /** 실제 종료 콜백과 객체 해제를 순서대로 재현한다. */
        void Disconnect(FPartyTestPlayer& Player)
        {
            Manager->DisconnectPlayer(Player.Controller);
            World->RemoveController(Player.Controller);
            RegisteredControllers.Remove(Player.Controller);
            Player.Controller->SetPlayerState(nullptr);
            Player.Profile->Destroy();
            Player.Controller->Destroy();
            Player = FPartyTestPlayer();
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyLifecycleTest, "Fablewood.Native.Party.ConsentAndLeaderAuthority",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 초대 수신 동의와 파티장 권한이 모든 실제 명부 변경의 조건인지 확인한다. */
bool FFablePartyLifecycleTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer Leader = Fixture.AddPlayer(101);
    FPartyTestPlayer Member = Fixture.AddPlayer(102);
    FPartyTestPlayer Stranger = Fixture.AddPlayer(103);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("All fixture connections exist"), Leader.IsValid() && Member.IsValid() && Stranger.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Lifecycle fixture controllers are visible to authoritative roster lookups"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Member.Controller) &&
                      Fixture.IsControllerRegistered(Stranger.Controller)))
    {
        return false;
    }

    TestEqual(TEXT("Fixture world has exactly three registered connections"), Fixture.World->GetNumPlayerControllers(),
              3);
    TestTrue(TEXT("A registered player creates a party"), Fixture.Act(Leader, EFablePartyAction::Create));
    const FGuid PartyId = Leader.Profile->PartyId;
    TestTrue(TEXT("Create synchronizes a valid server party ID"), PartyId.IsValid());
    TestFalse(TEXT("An existing member cannot create another party"), Fixture.Act(Leader, EFablePartyAction::Create));
    TestFalse(TEXT("A player without leader authority cannot invite"),
              Fixture.Act(Stranger, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    TestFalse(TEXT("A leader cannot invite itself"),
              Fixture.Act(Leader, EFablePartyAction::Invite, Leader.Profile->GetPlayerId()));
    TestFalse(TEXT("A nonexistent target is rejected"), Fixture.Act(Leader, EFablePartyAction::Invite, MAX_int32));

    TestTrue(TEXT("Leader sends an invitation"),
             Fixture.Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    const FFablePartyInvite* Pending = Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("The recipient observes its pending invitation"), Pending))
    {
        return false;
    }

    const FGuid DeclinedId = Pending->InviteId;
    TestFalse(TEXT("Knowing someone else's invitation UUID grants no membership"),
              Fixture.Act(Stranger, EFablePartyAction::Accept, INDEX_NONE, DeclinedId));
    TestFalse(TEXT("Another player cannot decline the recipient's invitation"),
              Fixture.Act(Stranger, EFablePartyAction::Decline, INDEX_NONE, DeclinedId));
    TestTrue(TEXT("The actual recipient can decline"),
             Fixture.Act(Member, EFablePartyAction::Decline, INDEX_NONE, DeclinedId));
    TestFalse(TEXT("A declined invitation cannot later be accepted"),
              Fixture.Act(Member, EFablePartyAction::Accept, INDEX_NONE, DeclinedId));
    TestFalse(TEXT("Declining never adds a party member"), Member.Profile->PartyId.IsValid());

    TestTrue(TEXT("A new invitation can be accepted"), Fixture.Join(Leader, Member));
    TestTrue(TEXT("Accepted membership synchronizes both profiles"), Member.Profile->PartyId == PartyId);
    TestTrue(TEXT("Explicit members are recognized as companions"),
             Fixture.Manager->ArePartyMembers(Leader.Profile, Member.Profile));
    TestFalse(TEXT("An unrelated connected player is not a companion"),
              Fixture.Manager->ArePartyMembers(Leader.Profile, Stranger.Profile));
    TestFalse(TEXT("An ordinary member cannot transfer leadership"),
              Fixture.Act(Member, EFablePartyAction::TransferLeader, Leader.Profile->GetPlayerId()));
    TestFalse(TEXT("Leadership cannot be transferred outside the party"),
              Fixture.Act(Leader, EFablePartyAction::TransferLeader, Stranger.Profile->GetPlayerId()));
    TestTrue(TEXT("The leader can transfer to its connected companion"),
             Fixture.Act(Leader, EFablePartyAction::TransferLeader, Member.Profile->GetPlayerId()));

    const FFablePartyRecord* Party = Fixture.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
    TestTrue(TEXT("The recipient becomes the sole party leader"),
             Party && Party->LeaderKey == Member.Profile->PartyPlayerKey);
    TestTrue(TEXT("A former leader may explicitly leave"), Fixture.Act(Leader, EFablePartyAction::Leave));
    TestFalse(TEXT("Explicit leave clears the departing profile"), Leader.Profile->PartyId.IsValid());
    TestTrue(TEXT("The remaining member retains the original party"), Member.Profile->PartyId == PartyId);
    TestTrue(TEXT("The final member can leave"), Fixture.Act(Member, EFablePartyAction::Leave));
    TestEqual(TEXT("An empty party is removed"), Fixture.Manager->Parties.Num(), 0);
    TestFalse(TEXT("A player cannot leave twice"), Fixture.Act(Member, EFablePartyAction::Leave));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyInviteRaceTest, "Fablewood.Native.Party.InvitationRacesAndCapacity",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 중복·경합·재사용 요청으로 정원이나 한 사람의 소속이 늘어나지 않는지 확인한다. */
bool FFablePartyInviteRaceTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer First = Fixture.AddPlayer(201);
    FPartyTestPlayer Second = Fixture.AddPlayer(202);
    FPartyTestPlayer Target = Fixture.AddPlayer(203);
    FPartyTestPlayer Extra = Fixture.AddPlayer(204);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("Four isolated fixture profiles exist"),
                  First.IsValid() && Second.IsValid() && Target.IsValid() && Extra.IsValid()))
    {
        return false;
    }

    if (!TestTrue(
            TEXT("Race fixture controllers are visible to authoritative invitation lookups"),
            Fixture.IsControllerRegistered(First.Controller) && Fixture.IsControllerRegistered(Second.Controller) &&
                Fixture.IsControllerRegistered(Target.Controller) && Fixture.IsControllerRegistered(Extra.Controller)))
    {
        return false;
    }

    TestTrue(TEXT("First leader creates its party"), Fixture.Act(First, EFablePartyAction::Create));
    TestTrue(TEXT("Second leader creates another party"), Fixture.Act(Second, EFablePartyAction::Create));
    TestTrue(TEXT("First invitation reserves one consent request"),
             Fixture.Act(First, EFablePartyAction::Invite, Target.Profile->GetPlayerId()));
    const FFablePartyInvite* Pending = Fixture.Manager->GetPendingInviteFor(Target.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("First invitation exists"), Pending))
    {
        return false;
    }

    const FGuid AcceptedId = Pending->InviteId;
    TestFalse(TEXT("Duplicate invitation cannot stack"),
              Fixture.Act(First, EFablePartyAction::Invite, Target.Profile->GetPlayerId()));
    TestFalse(TEXT("A second party cannot overwrite an outstanding invitation"),
              Fixture.Act(Second, EFablePartyAction::Invite, Target.Profile->GetPlayerId()));
    TestEqual(TEXT("One recipient has one outstanding invitation"), Fixture.Manager->Invitations.Num(), 1);
    TestTrue(TEXT("The second open slot may have another pending invitation"),
             Fixture.Act(First, EFablePartyAction::Invite, Extra.Profile->GetPlayerId()));
    const FFablePartyInvite* ExtraInvite = Fixture.Manager->GetPendingInviteFor(Extra.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("Competing slot invitation exists"), ExtraInvite))
    {
        return false;
    }

    const FGuid LostRaceId = ExtraInvite->InviteId;
    TestTrue(TEXT("The first recipient claims the final slot once"),
             Fixture.Act(Target, EFablePartyAction::Accept, INDEX_NONE, AcceptedId));
    TestFalse(TEXT("An accepted invitation cannot be replayed"),
              Fixture.Act(Target, EFablePartyAction::Accept, INDEX_NONE, AcceptedId));
    TestFalse(TEXT("A competing acceptance cannot exceed party capacity"),
              Fixture.Act(Extra, EFablePartyAction::Accept, INDEX_NONE, LostRaceId));
    TestFalse(TEXT("A full party cannot issue another invitation"),
              Fixture.Act(First, EFablePartyAction::Invite, Extra.Profile->GetPlayerId()));
    const FFablePartyRecord* Full = Fixture.Manager->GetPartyForMember(First.Profile->PartyPlayerKey);
    TestEqual(TEXT("The final roster has exactly two members"), Full ? Full->Members.Num() : 0, 2);
    TestFalse(TEXT("The loser remains outside the full party"), Extra.Profile->PartyId.IsValid());

    TestTrue(TEXT("Another party invites the unassigned player"),
             Fixture.Act(Second, EFablePartyAction::Invite, Extra.Profile->GetPlayerId()));
    const FFablePartyInvite* Obsolete = Fixture.Manager->GetPendingInviteFor(Extra.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("Invitation before party dissolution exists"), Obsolete))
    {
        return false;
    }

    const FGuid ObsoleteId = Obsolete->InviteId;
    const FGuid OldPartyId = Second.Profile->PartyId;
    TestTrue(TEXT("A solo leader may dissolve the party"), Fixture.Act(Second, EFablePartyAction::Leave));
    TestTrue(TEXT("The same player creates a distinct new party"), Fixture.Act(Second, EFablePartyAction::Create));
    TestTrue(TEXT("Recreated party has a new server identity"), Second.Profile->PartyId != OldPartyId);
    TestFalse(TEXT("An invitation to the dissolved party cannot join its replacement"),
              Fixture.Act(Extra, EFablePartyAction::Accept, INDEX_NONE, ObsoleteId));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyReconnectTest,
                                 "Fablewood.Native.Party.DisconnectReservationAndStableRejoin",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 파티장 종료와 동일 캐릭터 복귀가 다른 사람의 자리나 권한을 바꾸지 않는지 확인한다. */
bool FFablePartyReconnectTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer Leader = Fixture.AddPlayer(301);
    FPartyTestPlayer Member = Fixture.AddPlayer(302);
    FPartyTestPlayer Stranger = Fixture.AddPlayer(303);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("Reconnect fixture profiles exist"), Leader.IsValid() && Member.IsValid() && Stranger.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Reconnect fixture controllers are visible to authoritative roster lookups"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Member.Controller) &&
                      Fixture.IsControllerRegistered(Stranger.Controller)))
    {
        return false;
    }

    if (!TestTrue(TEXT("A two-member party is formed"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }

    const FString OriginalKey = Leader.Profile->PartyPlayerKey;
    const FString MemberKey = Member.Profile->PartyPlayerKey;
    const FGuid OriginalParty = Leader.Profile->PartyId;
    FPartyTestPlayer PreviousConnection = Leader;
    Fixture.Manager->DisconnectPlayer(PreviousConnection.Controller);
    const FFablePartyRecord* Reserved = Fixture.Manager->GetPartyForMember(OriginalKey);
    TestTrue(TEXT("The disconnected member remains reserved during the grace period"), Reserved != nullptr);
    TestTrue(TEXT("A connected companion immediately receives leadership"),
             Reserved && Reserved->LeaderKey == MemberKey);
    TestFalse(TEXT("An offline reservation still occupies the final slot"),
              Fixture.Act(Member, EFablePartyAction::Invite, Stranger.Profile->GetPlayerId()));

    Leader = Fixture.AddPlayer(301);
    if (!TestTrue(TEXT("A new connection for the same local identity exists"), Leader.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("The restored connection is registered in the authoritative world list"),
             Fixture.IsControllerRegistered(Leader.Controller));
    TestTrue(TEXT("The server restores the original public stable key"), Leader.Profile->PartyPlayerKey == OriginalKey);
    TestTrue(TEXT("Rejoining restores the same party rather than creating another"),
             Leader.Profile->PartyId == OriginalParty);
    const FFablePartyRecord* Restored = Fixture.Manager->GetPartyForMember(OriginalKey);
    TestTrue(TEXT("Rejoining does not steal leadership back"), Restored && Restored->LeaderKey == MemberKey);
    TestEqual(TEXT("Rejoining never duplicates a reserved member"), Restored ? Restored->Members.Num() : 0, 2);

    // 이전 연결의 늦은 종료 알림이 이미 복귀한 새 연결을 오프라인으로 만들면 안 된다.
    Fixture.Manager->DisconnectPlayer(PreviousConnection.Controller);
    Restored = Fixture.Manager->GetPartyForMember(OriginalKey);
    const FFablePartyMember* RejoinedMember = Restored ? Restored->Members.FindByPredicate(
                                                             [&](const FFablePartyMember& Entry)
                                                             {
                                                                 return Entry.PlayerKey == OriginalKey;
                                                             })
                                                       : nullptr;
    TestTrue(TEXT("A delayed logout from the previous connection cannot evict the rejoined player"),
             RejoinedMember && RejoinedMember->bConnected && RejoinedMember->ReconnectDeadline == 0);
    APlayerController* PreviousController = PreviousConnection.Controller;
    Fixture.Disconnect(PreviousConnection);
    TestFalse(TEXT("A disconnected old controller is removed from the authoritative world list"),
              Fixture.IsControllerRegistered(PreviousController));

    // 두 사람이 모두 끊긴 뒤 두 번째 구성원이 먼저 돌아와도 온라인 리더를 확보한다.
    Fixture.Disconnect(Leader);
    Fixture.Disconnect(Member);
    Member = Fixture.AddPlayer(302);
    if (!TestTrue(TEXT("The second member can reconnect before the original leader"), Member.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("The first returning member retains its stable key"), Member.Profile->PartyPlayerKey == MemberKey);
    TestTrue(TEXT("The first returning member retains the original party"), Member.Profile->PartyId == OriginalParty);
    const FFablePartyRecord* FirstReturned = Fixture.Manager->GetPartyForMember(MemberKey);
    TestTrue(TEXT("The only connected member becomes leader after both connections were lost"),
             FirstReturned && FirstReturned->LeaderKey == MemberKey);

    Leader = Fixture.AddPlayer(301);
    if (!TestTrue(TEXT("The original leader reconnects after its companion"), Leader.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("The original leader still restores the same stable key"),
             Leader.Profile->PartyPlayerKey == OriginalKey);
    TestTrue(TEXT("The original leader still restores the same party"), Leader.Profile->PartyId == OriginalParty);
    Restored = Fixture.Manager->GetPartyForMember(OriginalKey);
    TestTrue(TEXT("The already-connected leader keeps authority when the original leader returns"),
             Restored && Restored->LeaderKey == MemberKey);
    TestEqual(TEXT("Both reconnects retain exactly the original two roster entries"),
              Restored ? Restored->Members.Num() : 0, 2);

    Fixture.Disconnect(Leader);
    FFablePartyTestAccess::SetReconnectDeadline(Fixture.Manager, OriginalKey, Fixture.World->GetTimeSeconds());
    FFablePartyTestAccess::Expire(Fixture.Manager);
    TestTrue(TEXT("A reservation expires at its deadline"), Fixture.Manager->GetPartyForMember(OriginalKey) == nullptr);
    TestTrue(TEXT("An expired reservation releases the slot"), Fixture.Join(Member, Stranger));
    Leader = Fixture.AddPlayer(301);
    if (!TestTrue(TEXT("A connection after reservation expiry exists"), Leader.IsValid()))
    {
        return false;
    }

    TestTrue(TEXT("Stable key survives reservation expiry"), Leader.Profile->PartyPlayerKey == OriginalKey);
    TestFalse(TEXT("An expired member does not rejoin an already-filled party"), Leader.Profile->PartyId.IsValid());

    const FString FinalMemberKey = Member.Profile->PartyPlayerKey;
    const FString StrangerKey = Stranger.Profile->PartyPlayerKey;
    Fixture.Disconnect(Member);
    Fixture.Disconnect(Stranger);
    FFablePartyTestAccess::SetReconnectDeadline(Fixture.Manager, FinalMemberKey, Fixture.World->GetTimeSeconds());
    FFablePartyTestAccess::SetReconnectDeadline(Fixture.Manager, StrangerKey, Fixture.World->GetTimeSeconds());
    FFablePartyTestAccess::Expire(Fixture.Manager);
    TestEqual(TEXT("A fully expired disconnected party is removed"), Fixture.Manager->Parties.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyGuardTest, "Fablewood.Native.Party.AuthenticationAuthorityAndCombatGuards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 인증·소유권·서버 권한·전투 상태를 우회한 명부 수정 요청을 거절한다. */
bool FFablePartyGuardTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestWorld OtherWorld;
    FPartyTestPlayer Leader = Fixture.AddPlayer(401);
    FPartyTestPlayer Unloaded = Fixture.AddPlayer(402, false);
    FPartyTestPlayer Unregistered = Fixture.AddPlayer(403, true, false);
    FPartyTestPlayer Stranger = Fixture.AddPlayer(404);
    FPartyTestPlayer Foreign = OtherWorld.AddPlayer(405);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestNotNull(TEXT("Combat fixture game state"), Fixture.State) ||
        !TestTrue(TEXT("Guard fixture profiles exist"), Leader.IsValid() && Unloaded.IsValid() &&
                                                            Unregistered.IsValid() && Stranger.IsValid() &&
                                                            Foreign.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Guard fixtures register their controllers in the appropriate world"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Unloaded.Controller) &&
                      Fixture.IsControllerRegistered(Unregistered.Controller) &&
                      Fixture.IsControllerRegistered(Stranger.Controller) &&
                      OtherWorld.IsControllerRegistered(Foreign.Controller)))
    {
        return false;
    }

    TestFalse(TEXT("A missing requester is rejected"),
              Fixture.Manager->ExecuteAction(nullptr, EFablePartyAction::Create));
    TestTrue(TEXT("An unloaded profile receives no stable party key"), Unloaded.Profile->PartyPlayerKey.IsEmpty());
    TestFalse(TEXT("An unloaded profile cannot create a party"), Fixture.Act(Unloaded, EFablePartyAction::Create));
    TestFalse(TEXT("A loaded but unregistered profile cannot create a party"),
              Fixture.Act(Unregistered, EFablePartyAction::Create));
    TestFalse(TEXT("A foreign-world controller is rejected"), Fixture.Act(Foreign, EFablePartyAction::Create));

    Fixture.Manager->SetRole(ROLE_SimulatedProxy);
    TestFalse(TEXT("A client-side manager cannot create authoritative membership"),
              Fixture.Act(Leader, EFablePartyAction::Create));
    Fixture.Manager->SetRole(ROLE_Authority);
    TestEqual(TEXT("Rejected client request creates no party"), Fixture.Manager->Parties.Num(), 0);

    Stranger.Controller->SetPlayerState(Leader.Profile);
    TestFalse(TEXT("A controller cannot act through another controller's profile"),
              Fixture.Act(Stranger, EFablePartyAction::Create));
    Stranger.Controller->SetPlayerState(Stranger.Profile);
    TestTrue(TEXT("The legitimate owner still creates the party"), Fixture.Act(Leader, EFablePartyAction::Create));
    TestFalse(TEXT("An unknown action cannot change membership"),
              Fixture.Act(Leader, static_cast<EFablePartyAction>(255)));

    Stranger.Profile->PartyId = Leader.Profile->PartyId;
    TestFalse(TEXT("A forged profile PartyId is insufficient for companion authority"),
              Fixture.Manager->ArePartyMembers(Leader.Profile, Stranger.Profile));
    Stranger.Profile->PartyId.Invalidate();

    const FGuid OriginalParty = Leader.Profile->PartyId;
    Fixture.State->ActiveRegion = EFableRegion::RootRuins;
    TestFalse(TEXT("Dungeon combat prevents explicit party leave"), Fixture.Act(Leader, EFablePartyAction::Leave));
    Fixture.State->ActiveRegion = EFableRegion::Village;
    Fixture.State->bPartyRecoveryPending = true;
    TestFalse(TEXT("Recovery countdown prevents explicit party leave"), Fixture.Act(Leader, EFablePartyAction::Leave));
    Fixture.State->bPartyRecoveryPending = false;
    Fixture.State->AdventureStage = EFableAdventureStage::Forest;
    TestFalse(TEXT("A village forest encounter also prevents party editing"),
              Fixture.Act(Leader, EFablePartyAction::Leave));
    TestTrue(TEXT("All rejected edits preserve membership"), Leader.Profile->PartyId == OriginalParty);
    Fixture.State->AdventureStage = EFableAdventureStage::Village;
    TestTrue(TEXT("Returning to idle town allows explicit leave"), Fixture.Act(Leader, EFablePartyAction::Leave));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFablePartyDeadlineTest, "Fablewood.Native.Party.ServerDeadlinesAndPacketReplay",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 서버 시각의 초대 만료와 빠른 중복 패킷이 일회성 동의를 유지하는지 확인한다. */
bool FFablePartyDeadlineTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture;
    FPartyTestPlayer Leader = Fixture.AddPlayer(501);
    FPartyTestPlayer Member = Fixture.AddPlayer(502);
    if (!TestNotNull(TEXT("Party fixture manager"), Fixture.Manager) ||
        !TestTrue(TEXT("Deadline fixture profiles exist"), Leader.IsValid() && Member.IsValid()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Deadline fixture controllers are visible to authoritative invitation lookups"),
                  Fixture.IsControllerRegistered(Leader.Controller) &&
                      Fixture.IsControllerRegistered(Member.Controller)))
    {
        return false;
    }

    TestTrue(TEXT("Initial packet creates exactly one party"),
             Fixture.Manager->ExecuteAction(Leader.Controller, EFablePartyAction::Create));
    TestFalse(TEXT("A rapid follow-up leave packet cannot undo the first action"),
              Fixture.Manager->ExecuteAction(Leader.Controller, EFablePartyAction::Leave));
    TestEqual(TEXT("Rate-limited packets preserve the party"), Fixture.Manager->Parties.Num(), 1);
    TestTrue(TEXT("A later independent action may invite"),
             Fixture.Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    const FFablePartyInvite* Pending = Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("An unexpired invitation is visible"), Pending))
    {
        return false;
    }

    const FGuid ExpiredId = Pending->InviteId;
    FFablePartyTestAccess::SetInviteDeadline(Fixture.Manager, ExpiredId, Fixture.World->GetTimeSeconds());
    TestTrue(TEXT("An invitation is hidden exactly at the server deadline"),
             Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey) == nullptr);
    Fixture.Manager->SetRole(ROLE_SimulatedProxy);
    FFablePartyTestAccess::Expire(Fixture.Manager);
    TestEqual(TEXT("A client timer cannot delete authoritative invitation state"), Fixture.Manager->Invitations.Num(),
              1);
    Fixture.Manager->SetRole(ROLE_Authority);
    TestFalse(TEXT("An expired acceptance is rejected even before the periodic timer runs"),
              Fixture.Act(Member, EFablePartyAction::Accept, INDEX_NONE, ExpiredId));
    TestEqual(TEXT("The server removes expired invitations"), Fixture.Manager->Invitations.Num(), 0);
    TestFalse(TEXT("Expired consent does not modify the target profile"), Member.Profile->PartyId.IsValid());

    TestTrue(TEXT("A fresh invitation remains available after expiry"), Fixture.Join(Leader, Member));
    TestTrue(TEXT("A renewed consent creates the expected companion relationship"),
             Fixture.Manager->ArePartyMembers(Leader.Profile, Member.Profile));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionConsentTest, "Fablewood.Native.Expedition.SelectionAndExplicitConsent",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 같은 선택에 대한 각자의 동의와 오래된 요청의 거절을 검증한다. */
bool FFableExpeditionConsentTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(601);
    FPartyTestPlayer Member = Fixture.AddPlayer(602);
    if (!TestTrue(TEXT("Preparation fixture has live owned characters"),
                  Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110))) ||
        !TestTrue(TEXT("Preparation fixture forms a real two-member party"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }

    const FFablePartyRecord* Party = Fixture.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
    const FGuid PartyId = Party->PartyId;
    const int32 InitialRevision = Party->PreparationRevision;
    FFableExpeditionTicket Ticket;
    TestFalse(TEXT("A party without consent cannot produce an admission ticket"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestTrue(TEXT("The leader explicitly becomes ready"), Fixture.Ready(Leader));
    TestEqual(TEXT("A readiness update does not invalidate simultaneous peer consent"), Party->PreparationRevision,
              InitialRevision);
    TestFalse(TEXT("One ready member cannot stand in for the other"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestTrue(TEXT("The peer independently becomes ready for the same revision"), Fixture.Ready(Member));
    TestTrue(TEXT("The same readiness request is idempotent"), Fixture.Ready(Member));
    TestEqual(TEXT("Repeated readiness leaves the selection revision unchanged"), Party->PreparationRevision,
              InitialRevision);
    TestTrue(TEXT("The exact agreed roster produces a server ticket"),
             Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestEqual(TEXT("A ticket fixes exactly two stable member keys"), Ticket.MemberKeys.Num(), 2);
    TestTrue(TEXT("The ticket contains the authenticated leader and peer"),
             Ticket.Contains(Leader.Profile->PartyPlayerKey) && Ticket.Contains(Member.Profile->PartyPlayerKey));

    // 선택을 바꾸지 않은 재전송은 준비를 지우지 않으며 비리더의 변경은 거절한다.
    TestTrue(TEXT("Re-selecting the same destination preserves consent"),
             Fixture.Manager->SelectExpedition(Leader.Controller, PartyId, InitialRevision, EFableRegion::RootRuins,
                                               EFableDifficulty::Normal));
    TestEqual(TEXT("An identical selection retains its revision"), Party->PreparationRevision, InitialRevision);
    TestFalse(TEXT("A member cannot change the leader's choice"),
              Fixture.Manager->SelectExpedition(Member.Controller, PartyId, InitialRevision, EFableRegion::Moonwell,
                                                EFableDifficulty::Hard));
    TestTrue(TEXT("A real selection change succeeds for the leader"),
             Fixture.Manager->SelectExpedition(Leader.Controller, PartyId, InitialRevision, EFableRegion::RootRuins,
                                               EFableDifficulty::Hard));
    const int32 ChangedRevision = Party->PreparationRevision;
    TestTrue(TEXT("A new selection has a new revision"), ChangedRevision != InitialRevision);
    TestTrue(TEXT("A changed selection clears every old consent"),
             Party->Members[0].ReadyRevision == 0 && Party->Members[1].ReadyRevision == 0);
    TestFalse(TEXT("A stale ready packet cannot consent to the new selection"),
              Fixture.Manager->SetExpeditionReady(Member.Controller, PartyId, InitialRevision, true));
    TestFalse(TEXT("A guessed party identity cannot change readiness"),
              Fixture.Manager->SetExpeditionReady(Member.Controller, FGuid::NewGuid(), ChangedRevision, true));
    TestFalse(TEXT("A stale start snapshot cannot use the new choice"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, InitialRevision, Ticket));
    TestTrue(TEXT("Both members may consent again without changing the new revision"),
             Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestEqual(TEXT("Two fresh ready requests share one selection revision"), Party->PreparationRevision,
              ChangedRevision);
    TestTrue(TEXT("Explicit unready is accepted"), Fixture.Ready(Member, false));
    TestFalse(TEXT("Unready immediately blocks admission"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, ChangedRevision, Ticket));
    TestTrue(TEXT("Unready does not erase the other member's consent"),
             Party->Members.ContainsByPredicate(
                 [&](const FFablePartyMember& Entry)
                 {
                     return Entry.PlayerKey == Leader.Profile->PartyPlayerKey && Entry.ReadyRevision == ChangedRevision;
                 }));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionPresenceTest,
                                 "Fablewood.Native.Expedition.PreparationInvalidatesOnLeaderAndPresence",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 위임·접속 종료·복귀 뒤에는 이전 구성의 동의가 남지 않아야 한다. */
bool FFableExpeditionPresenceTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(611);
    FPartyTestPlayer Member = Fixture.AddPlayer(612);
    if (!TestTrue(TEXT("Presence fixture has live participants"),
                  Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110))) ||
        !TestTrue(TEXT("Presence fixture creates its leader's party"), Fixture.Act(Leader, EFablePartyAction::Create)))
    {
        return false;
    }

    const FFablePartyRecord* Party = Fixture.Manager->GetPartyForMember(Leader.Profile->PartyPlayerKey);
    const FGuid PartyId = Party->PartyId;
    TestTrue(TEXT("A solo leader can confirm readiness"), Fixture.Ready(Leader));
    const int32 SoloRevision = Party->PreparationRevision;
    TestTrue(TEXT("The leader invites the second participant"),
             Fixture.Act(Leader, EFablePartyAction::Invite, Member.Profile->GetPlayerId()));
    TestFalse(TEXT("An unresolved invitation rejects a fresh ready request on the server"), Fixture.Ready(Leader));
    FFableExpeditionTicket PendingTicket;
    TestFalse(
        TEXT("An outstanding invitation blocks a solo admission snapshot"),
        Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, Party->PreparationRevision, PendingTicket));
    const FFablePartyInvite* Invite = Fixture.Manager->GetPendingInviteFor(Member.Profile->PartyPlayerKey);
    if (!TestNotNull(TEXT("Roster-change consent invitation exists"), Invite) ||
        !TestTrue(TEXT("The peer accepts the exact invitation"),
                  Fixture.Act(Member, EFablePartyAction::Accept, INDEX_NONE, Invite->InviteId)))
    {
        return false;
    }
    TestTrue(TEXT("Accepting a new member invalidates the solo leader's old readiness"),
             Party->PreparationRevision != SoloRevision && Party->Members[0].ReadyRevision == 0 &&
                 Party->Members[1].ReadyRevision == 0);
    if (!TestTrue(TEXT("Both members consent before transfer"), Fixture.Ready(Leader) && Fixture.Ready(Member)))
    {
        return false;
    }

    int32 Revision = Party->PreparationRevision;
    TestTrue(TEXT("An ordinary authority transfer succeeds"),
             Fixture.Act(Leader, EFablePartyAction::TransferLeader, Member.Profile->GetPlayerId()));
    TestTrue(TEXT("Leadership transfer invalidates old consent"), Party->PreparationRevision != Revision &&
                                                                      Party->Members[0].ReadyRevision == 0 &&
                                                                      Party->Members[1].ReadyRevision == 0);
    TestTrue(TEXT("Both may consent to the new leadership"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    Revision = Party->PreparationRevision;

    // 접속 객체는 유지하되 실제 서버의 접속 해제·복귀 콜백을 사용한다.
    Fixture.Manager->DisconnectPlayer(Member.Controller);
    TestTrue(TEXT("Disconnect invalidates all preparation"), Party->PreparationRevision != Revision &&
                                                                 Party->Members[0].ReadyRevision == 0 &&
                                                                 Party->Members[1].ReadyRevision == 0);
    TestFalse(TEXT("An offline member cannot submit ready"), Fixture.Ready(Member));
    FFableExpeditionTicket Ticket;
    TestFalse(TEXT("A disconnected reserved slot cannot be admitted"),
              Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, Party->PreparationRevision, Ticket));
    Revision = Party->PreparationRevision;
    Fixture.Manager->RegisterPlayer(Member.Controller);
    TestTrue(TEXT("Reconnect changes the preparation revision"), Party->PreparationRevision != Revision);
    TestTrue(TEXT("Reconnect restores the same membership without restoring old consent"),
             Member.Profile->PartyId == PartyId && Party->Members[0].ReadyRevision == 0 &&
                 Party->Members[1].ReadyRevision == 0);
    TestTrue(TEXT("Fresh consent after reconnect succeeds"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestTrue(TEXT("The current connected leader can build the restored ticket"),
             Fixture.Manager->BuildReadySnapshot(Leader.Controller, PartyId, Party->PreparationRevision, Ticket));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionEntryTest,
                                 "Fablewood.Native.Expedition.AuthoritativeEntryAndParticipantRewards",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 입장·처치 보상·기존 진입점 모두 확정된 참가 명부만 사용해야 한다. */
bool FFableExpeditionEntryTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(621);
    FPartyTestPlayer Member = Fixture.AddPlayer(622);
    FPartyTestPlayer Outsider = Fixture.AddPlayer(623);
    if (!TestTrue(TEXT("Admission fixture has its real game mode and characters"),
                  Fixture.AddMode() && Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110)) &&
                      Fixture.AddHero(Outsider, FableRPG::PortalLocation() + FVector(0, 180, 110))) ||
        !TestTrue(TEXT("Admission party is formed"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)))
    {
        return false;
    }

    const FVector LeaderBefore = Leader.Hero->GetActorLocation();
    const FVector MemberBefore = Member.Hero->GetActorLocation();
    const FVector OutsiderBefore = Outsider.Hero->GetActorLocation();
    TestFalse(TEXT("A leader cannot bypass missing readiness"), Fixture.Enter(Leader));
    TestTrue(TEXT("A rejected start moves nobody and spawns no enemies"),
             Leader.Hero->GetActorLocation().Equals(LeaderBefore) &&
                 Member.Hero->GetActorLocation().Equals(MemberBefore) && Fixture.EnemyCount() == 0);
    TestTrue(TEXT("The exact roster becomes ready"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestFalse(TEXT("A ready peer still cannot start as leader"), Fixture.Enter(Member));
    TestTrue(TEXT("A ready leader atomically starts its expedition"), Fixture.Enter(Leader));

    const FFableExpeditionTicket Ticket = Fixture.Mode->GetActiveExpedition();
    TestTrue(TEXT("Admission creates a fresh server expedition identity"), Ticket.IsValid());
    TestTrue(TEXT("Only the agreed party is bound to that identity"),
             Leader.Profile->ActiveExpeditionId == Ticket.ExpeditionId &&
                 Member.Profile->ActiveExpeditionId == Ticket.ExpeditionId &&
                 !Outsider.Profile->ActiveExpeditionId.IsValid());
    TestTrue(TEXT("Only the ticket participants arrive in the selected region"),
             FableRPG::IsInRegion(Ticket.Region, Leader.Hero->GetActorLocation()) &&
                 FableRPG::IsInRegion(Ticket.Region, Member.Hero->GetActorLocation()) &&
                 Outsider.Hero->GetActorLocation().Equals(OutsiderBefore));
    TestTrue(TEXT("The mode identifies exact participants"),
             Fixture.Mode->IsExpeditionParticipant(Leader.Profile) &&
                 Fixture.Mode->IsExpeditionParticipant(Member.Profile) &&
                 !Fixture.Mode->IsExpeditionParticipant(Outsider.Profile));
    TestTrue(TEXT("The first wave starts only after admission"), Fixture.EnemyCount() > 0);
    TestFalse(TEXT("A duplicate start cannot create another expedition"), Fixture.Enter(Leader));
    TestTrue(TEXT("A duplicate start retains the same identity"),
             Fixture.Mode->GetActiveExpedition().ExpeditionId == Ticket.ExpeditionId);

    // 거리만 맞춘 비참가자도 보상을 받거나 기존 지역 진입점으로 합류할 수 없다.
    Fixture.Mode->RequestEnterRegion(Outsider.Controller, Ticket.Region, Ticket.Difficulty);
    TestTrue(TEXT("The legacy entry route never moves an outsider into this party"),
             Outsider.Hero->GetActorLocation().Equals(OutsiderBefore) &&
                 !Outsider.Profile->ActiveExpeditionId.IsValid());
    Outsider.Hero->SetActorLocation(FableRPG::RegionEntrance(Ticket.Region) + FVector(700, 0, 110));
    const int32 OutsiderGold = Outsider.Profile->Gold;
    const int32 LeaderGold = Leader.Profile->Gold;
    const int32 MemberGold = Member.Profile->Gold;
    AFableEnemy* Victim = nullptr;
    for (TActorIterator<AFableEnemy> It(Fixture.World); It; ++It)
    {
        if (It->IsAlive())
        {
            Victim = *It;
            break;
        }
    }
    if (!TestNotNull(TEXT("Admission spawned a real reward target"), Victim))
    {
        return false;
    }

    Victim->ReceiveHit(1000000, Leader.Controller);
    if (!TestFalse(TEXT("The authoritative hit commits enemy defeat"), Victim->IsAlive()))
    {
        return false;
    }
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestTrue(TEXT("Both admitted participants receive personal kill credit"),
             Leader.Profile->Gold > LeaderGold && Member.Profile->Gold > MemberGold);
    TestEqual(TEXT("An eligible outsider standing inside the arena receives no party reward"), Outsider.Profile->Gold,
              OutsiderGold);
    const int32 RewardedGold = Leader.Profile->Gold;
    Fixture.Mode->HandleEnemyDefeated(Victim);
    TestEqual(TEXT("Replayed defeat notification cannot grant twice"), Leader.Profile->Gold, RewardedGold);
    TestTrue(TEXT("Combat reward fixtures remain memory-only"),
             FFablePartyTestAccess::IsMemoryOnly(Leader.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Member.Profile) &&
                 FFablePartyTestAccess::IsMemoryOnly(Outsider.Profile));

    Fixture.State->AdventureStage = EFableAdventureStage::Cleared;
    Outsider.Hero->SetActorLocation(FableRPG::RegionReturnPortal(Ticket.Region) + FVector(0, 0, 110));
    const FVector LeaderInDungeon = Leader.Hero->GetActorLocation();
    Fixture.Mode->RequestReturnVillage(Outsider.Controller);
    TestTrue(TEXT("An outsider at the return portal cannot return or clear another party"),
             Leader.Hero->GetActorLocation().Equals(LeaderInDungeon) &&
                 Fixture.Mode->GetActiveExpedition().ExpeditionId == Ticket.ExpeditionId);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFableExpeditionBlockedEntryTest,
                                 "Fablewood.Native.Expedition.BlockedEntryPreservesAllParticipants",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** 도착점이 막혔을 때 부분 이동·유령 원정·적 생성이 남지 않는지 확인한다. */
bool FFableExpeditionBlockedEntryTest::RunTest(const FString& Parameters)
{
    FPartyTestWorld Fixture(true);
    FPartyTestPlayer Leader = Fixture.AddPlayer(631);
    FPartyTestPlayer Member = Fixture.AddPlayer(632);
    if (!TestTrue(TEXT("Blocked admission fixture has live owned characters"),
                  Fixture.AddMode() && Fixture.AddHero(Leader, FableRPG::PortalLocation() + FVector(-80, -100, 110)) &&
                      Fixture.AddHero(Member, FableRPG::PortalLocation() + FVector(160, -100, 110))) ||
        !TestTrue(TEXT("Blocked admission fixture forms its party"),
                  Fixture.Act(Leader, EFablePartyAction::Create) && Fixture.Join(Leader, Member)) ||
        !TestTrue(TEXT("Both blocked admission members consent"), Fixture.Ready(Leader) && Fixture.Ready(Member)))
    {
        return false;
    }

    AActor* Obstacle = Fixture.World->SpawnActor<AActor>();
    UBoxComponent* Box = Obstacle ? NewObject<UBoxComponent>(Obstacle) : nullptr;
    if (!TestNotNull(TEXT("A real collision obstacle is available"), Box))
    {
        return false;
    }
    Obstacle->SetRootComponent(Box);
    // 보정된 도착점도 벗어날 수 없도록 유효한 유적 범위와 높이를 모두 막는다.
    Box->SetBoxExtent(FVector(5000, 5000, 3000));
    Box->SetWorldLocation(FableRPG::RegionCenter(EFableRegion::RootRuins));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();

    const FVector LeaderBefore = Leader.Hero->GetActorLocation();
    const FVector MemberBefore = Member.Hero->GetActorLocation();
    TestFalse(TEXT("A blocked destination rejects the entire admission"), Fixture.Enter(Leader));
    TestTrue(TEXT("Neither player is stranded after a failed preflight"),
             Leader.Hero->GetActorLocation().Equals(LeaderBefore) &&
                 Member.Hero->GetActorLocation().Equals(MemberBefore));
    TestFalse(TEXT("A failed admission leaves no active ticket"), Fixture.Mode->GetActiveExpedition().IsValid());
    TestTrue(TEXT("A failed admission leaves neither profile bound"),
             !Leader.Profile->ActiveExpeditionId.IsValid() && !Member.Profile->ActiveExpeditionId.IsValid());
    TestEqual(TEXT("A failed admission starts no encounter enemies"), Fixture.EnemyCount(), 0);

    // 같은 실제 입장 경로가 장애물을 해제한 뒤에는 성공해야 한다.
    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("The party can confirm readiness after the failure"), Fixture.Ready(Leader) && Fixture.Ready(Member));
    TestTrue(TEXT("Clearing the obstacle allows the unchanged real entry path to succeed"), Fixture.Enter(Leader));

    return true;
}

#endif
