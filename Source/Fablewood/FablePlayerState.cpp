#include "FablePlayerState.h"
#include "FableSaveGame.h"
#include "FableCharacter.h"
#include "GameFramework/GameStateBase.h"
#include "Misc/DateTime.h"
#include "Engine/World.h"
#include "FableGameMode.h"
#include "FableGameState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

/** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
AFablePlayerState::AFablePlayerState()
{
    SetNetUpdateFrequency(15.0f);
    ClassProgress.Init(FFableClassProgress(), 3);
}

/** 서버 상태를 전달할 복제 속성과 수신 조건을 등록한다. */
void AFablePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    // 일반 파티 정보와 소유자에게만 전달할 저장·창고 정보를 등록한다.
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AFablePlayerState, BackendPlayerId, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, bBackendEnabled, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, bBackendReady, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, bBackendClosing, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, bBackendSavePending, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, BackendRevision, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, BackendAcknowledgedRevision, COND_OwnerOnly);
    DOREPLIFETIME_CONDITION(AFablePlayerState, BackendAcknowledgedProfileHash, COND_OwnerOnly);
    DOREPLIFETIME(AFablePlayerState, HeroClass);
    DOREPLIFETIME(AFablePlayerState, ClassProgress);
    DOREPLIFETIME(AFablePlayerState, ClassBuildRevision);
    DOREPLIFETIME(AFablePlayerState, DungeonCompletions);
    DOREPLIFETIME(AFablePlayerState, MoonwellCompletions);
    DOREPLIFETIME(AFablePlayerState, MoonwellQuest);
    DOREPLIFETIME(AFablePlayerState, Level);
    DOREPLIFETIME(AFablePlayerState, Experience);
    DOREPLIFETIME(AFablePlayerState, Gold);
    DOREPLIFETIME(AFablePlayerState, Ore);
    DOREPLIFETIME(AFablePlayerState, Essence);
    DOREPLIFETIME(AFablePlayerState, RootCores);
    DOREPLIFETIME(AFablePlayerState, MoonCrystals);
    DOREPLIFETIME_CONDITION(AFablePlayerState, StashItems, COND_OwnerOnly);
    DOREPLIFETIME(AFablePlayerState, HealthPotions);
    DOREPLIFETIME(AFablePlayerState, ManaPotions);
    DOREPLIFETIME_CONDITION(AFablePlayerState, NextPotionServerTime, COND_OwnerOnly);
    DOREPLIFETIME(AFablePlayerState, Inventory);
    DOREPLIFETIME(AFablePlayerState, EquippedWeaponId);
    DOREPLIFETIME(AFablePlayerState, EquippedArmorId);
    DOREPLIFETIME(AFablePlayerState, ForestQuest);
    DOREPLIFETIME(AFablePlayerState, DungeonQuest);
    DOREPLIFETIME(AFablePlayerState, QuestKills);
    DOREPLIFETIME(AFablePlayerState, LastNotice);
    DOREPLIFETIME(AFablePlayerState, NoticeSerial);
    DOREPLIFETIME(AFablePlayerState, StoryChapter);
    DOREPLIFETIME(AFablePlayerState, StoryObjectMask);
    DOREPLIFETIME(AFablePlayerState, StorySealMask);
    DOREPLIFETIME(AFablePlayerState, bProfileLoaded);
    DOREPLIFETIME(AFablePlayerState, bLastSaveSucceeded);
}

/** 시작 직업별 무기와 기본 방어구의 식별자·내구도를 초기화한다. */
void AFablePlayerState::InitializeStarterItems()
{
    // 인벤토리·창고와 시작 재료를 초기화한다.
    Inventory.Reset();
    StashItems.Reset();
    Essence = 0;
    RootCores = 0;
    MoonCrystals = 0;

    // 직업별 기본 무기에 서로 다른 UUID와 최대 내구도를 부여한다.
    for (int32 I = 0; I < 3; ++I)
    {
        FFableItem Item;
        Item.Id = I + 1;
        Item.InstanceId = FGuid::NewGuid();
        Item.HeroClass = static_cast<EFableClass>(I);
        Item.DurabilityVersion = 1;
        Item.MaxDurability = FableRPG::GradeMaxDurability(Item.Rarity);
        Item.Durability = Item.MaxDurability;
        Inventory.Add(Item);
    }

    // 기본 방어구를 추가하고 다음 아이템 식별자를 준비한다.
    FFableItem Armor;
    Armor.Id = 4;
    Armor.InstanceId = FGuid::NewGuid();
    Armor.Slot = EFableEquipmentSlot::Armor;
    Armor.Power = 0;
    Armor.DurabilityVersion = 1;
    Armor.MaxDurability = FableRPG::GradeMaxDurability(Armor.Rarity);
    Armor.Durability = Armor.MaxDurability;
    Inventory.Add(Armor);
    NextItemId = 5;
}

/** 선택한 직업과 저장 슬롯을 확인하고 기존 진행을 불러온다. */
void AFablePlayerState::InitializeProfile(bool bHost, const FString& ProfilePrefix, EFableClass InitialClass)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || bProfileLoaded || !GetWorld())
    {
        return;
    }

    // 새 프로필의 직업·전직·퀘스트·기본 장비·물약 상태를 준비한다.
    InitializeStarterItems();
    StoryChapter = StoryObjectMask = StorySealMask = 0;
    ClassProgress.Init(FFableClassProgress(), 3);
    DungeonCompletions = 0;
    MoonwellCompletions = 0;
    MoonwellQuest = EFableQuestStage::Available;
    HeroClass = uint8(InitialClass) <= uint8(EFableClass::Archer) ? InitialClass : EFableClass::Warrior;
    EquippedWeaponId = int32(HeroClass) + 1;
    HealthPotions = 3;
    ManaPotions = 2;
    ProfileSlot = bHost ? TEXT("Fablewood_RPG_Host") : TEXT("Fablewood_RPG_Guest");
    LastNotice =
        TEXT("환영합니다! 안내인 로완에게 E로 말을 걸어 의뢰를 받으세요. 1/2/3으로 직업을 선택할 수 있습니다.");
    bool bValidationIsolation = false;
#if !UE_BUILD_SHIPPING
    FString TestRole, TestRun;
    if (FParse::Value(FCommandLine::Get(), TEXT("FableValidationRole="), TestRole))
    {
        bPersistenceEnabled = false;
        bValidationIsolation = true;
    }
    if (FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRole="), TestRole))
    {
        bValidationIsolation = true;
        if (!FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRun="), TestRun) || TestRun.IsEmpty() ||
            TestRun.Len() > 80 || !TestRun.Contains(TEXT("-")))
        {
            bPersistenceEnabled = false;
        }
        else
        {
            bool bSafe = true;
            for (TCHAR C : TestRun)
            {
                bSafe &= (C >= TCHAR('a') && C <= TCHAR('z')) || (C >= TCHAR('A') && C <= TCHAR('Z')) ||
                         (C >= TCHAR('0') && C <= TCHAR('9')) || C == TCHAR('-') || C == TCHAR('_');
            }
            if (bSafe)
            {
                ProfileSlot = TEXT("Fablewood_RPG_Test_") + TestRun + (bHost ? TEXT("_Host") : TEXT("_Guest"));
            }
            else
            {
                bPersistenceEnabled = false;
            }
        }
    }
#endif
    // Caller supplies the complete prefix. Test runs retain their isolated slots.

    // 프로필과 저장 스냅숏 처리
    if (!bValidationIsolation && !ProfilePrefix.IsEmpty())
    {
        bool bSafe = ProfilePrefix.Len() <= 80;
        for (TCHAR C : ProfilePrefix)
        {
            bSafe &= (C >= TCHAR('a') && C <= TCHAR('z')) || (C >= TCHAR('A') && C <= TCHAR('Z')) ||
                     (C >= TCHAR('0') && C <= TCHAR('9')) || C == TCHAR('-') || C == TCHAR('_');
        }
        if (bSafe)
        {
            ProfileSlot = ProfilePrefix + (bHost ? TEXT("_Host") : TEXT("_Guest"));
        }
        else
        {
            bPersistenceEnabled = false;
            bStorageBlocked = true;
            bLastSaveSucceeded = false;
            LastNotice =
                TEXT("선택한 프로필 이름을 사용할 수 없습니다. 기존 저장은 보존되며 이번 진행은 저장되지 않습니다.");
        }
    }

    // 프로필과 저장 스냅숏 처리
    if (bPersistenceEnabled && UGameplayStatics::DoesSaveGameExist(ProfileSlot, 0))
    {
        UFableSaveGame* Data = Cast<UFableSaveGame>(UGameplayStatics::LoadGameFromSlot(ProfileSlot, 0));
        const bool bValid = Data && Data->MigrateAndValidate();
        if (bValid)
        {
            ApplyProfileSnapshot(Data);
            LastNotice = TEXT("진행 상황을 불러왔습니다. 안내인 로완에게 의뢰와 전직 시험을 확인하세요.");
        }
        else
        {
            // Preserve unreadable/newer saves rather than replacing user progress.
            bStorageBlocked = true;
            bLastSaveSucceeded = false;
            LastNotice =
                TEXT("저장된 프로필을 읽을 수 없습니다. 원본 파일을 보존하며 이번 진행으로 덮어쓰지 않습니다.");
        }
    }

    bProfileLoaded = true;

    // 프로필 저장과 완료 응답 처리
    if (!bStorageBlocked)
    {
        RefreshTrialReadiness();
        SaveProfile();
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    ForceNetUpdate();
    UE_LOG(LogTemp, Display, TEXT("FABLE_RPG_PROFILE loaded slot=%s class=%d level=%d items=%d potions=%d/%d"),
           *ProfileSlot, uint8(HeroClass), Level, Inventory.Num(), HealthPotions, ManaPotions);
}

/** 저장 가능 상태를 확인하고 현재 진행의 영구 저장을 요청한다. */
bool AFablePlayerState::SaveProfile()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || bBackendClosing)
    {
        return false;
    }
    if (bBackendLoading)
    {
        return true;
    }
    if (bBackendEnabled)
    {
        return QueueBackendSnapshot(MakeProfileSnapshot());
    }
    if (!bPersistenceEnabled)
    {
        return true;
    }

    UFableSaveGame* Data = MakeProfileSnapshot();

    // 프로필과 저장 스냅숏 조건 검사
    if (!Data->IsValidProfile())
    {
        bLastSaveSucceeded = false;
        UE_LOG(LogTemp, Error, TEXT("FABLE_RPG_SAVE_INVALID slot=%s"), *ProfileSlot);
        return false;
    }

    bLastSaveSucceeded = UGameplayStatics::SaveGameToSlot(Data, ProfileSlot, 0);

    // 프로필과 저장 스냅숏 처리
    if (!bLastSaveSucceeded)
    {
        UE_LOG(LogTemp, Error, TEXT("FABLE_RPG_SAVE_FAILED slot=%s"), *ProfileSlot);
    }

    return bLastSaveSucceeded;
}

/** 서버 권한과 저장 준비 상태를 확인해 상태 변경 허용 여부를 판단한다. */
bool AFablePlayerState::AllowMutation()
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || !GetWorld())
    {
        return false;
    }
    if (bBackendEnabled && (!IsBackendReady() || bBackendSavePending))
    {
        return false;
    }

    const double Now = GetWorld()->GetTimeSeconds();

    // 서버 시각과 재사용 대기시간 조건 검사
    if (Now - LastMutationTime < 0.15)
    {
        return false;
    }

    LastMutationTime = Now;

    return true;
}

/** 플레이어가 요청한 마을 서비스의 이용 거리 안에 있는지 확인한다. */
bool AFablePlayerState::AtTownService(const FVector& Location) const
{
    const AFableGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AFableGameMode>() : nullptr;
    return Mode && Mode->CanUseTownService(Cast<APlayerController>(GetOwner()), Location, 330.0f);
}

/** 안내 문구 값을 설정한다. */
void AFablePlayerState::SetNotice(const FString& Message)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority())
    {
        return;
    }

    // 프로필과 저장 스냅숏 처리
    LastNotice = Message.Left(240);
    NoticeSerial = NoticeSerial == MAX_int32 ? 1 : NoticeSerial + 1;
#if !UE_BUILD_SHIPPING
    FString ValidationRole;
    if (FParse::Value(FCommandLine::Get(), TEXT("FableRPGValidationRole="), ValidationRole))
    {
        UE_LOG(LogTemp, Display, TEXT("FABLE_RPG_NOTICE slot=%s notice=%s"), *ProfileSlot, *LastNotice);
    }
#endif
    ForceNetUpdate();
}

/** 변경된 플레이어 상태를 저장하고 복제 갱신을 요청한다. */
void AFablePlayerState::Changed(const FString& Notice)
{
    RefreshTrialReadiness();
    const bool bSaved = SaveProfile();
    SetNotice(Notice + (bSaved ? TEXT("")
                               : TEXT(" [저장할 수 없습니다. 현재 게임을 종료하지 말고 저장 상태를 확인해 주세요.]")));
}

/** 아이템 대상을 찾아 반환한다. */
const FFableItem* AFablePlayerState::FindItem(int32 ItemId) const
{
    return Inventory.FindByPredicate(
        [ItemId](const FFableItem& Item)
        {
            return Item.Id == ItemId;
        });
}

/** 착용 장비의 기본 성능·옵션·세트 효과를 합산한다. */
FFableEquipmentStats AFablePlayerState::CalculateEquipmentStats(int32 WeaponId, int32 ArmorId,
                                                                EFableClass ForClass) const
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableEquipmentStats Stats;
    const FFableItem* Weapon = FindItem(WeaponId);
    const FFableItem* Armor = FindItem(ArmorId);

    // 이 단계에서 착용 장비의 기본 성능·옵션·세트 효과를 합산한다.
    if (Weapon && (Weapon->Slot != EFableEquipmentSlot::Weapon || Weapon->HeroClass != ForClass || Weapon->IsBroken()))
    {
        Weapon = nullptr;
    }

    // 이 단계에서 착용 장비의 기본 성능·옵션·세트 효과를 합산한다.
    if (Armor && (Armor->Slot != EFableEquipmentSlot::Armor || Armor->IsBroken()))
    {
        Armor = nullptr;
    }

    int32 PassiveRank = 0;

    // 직업·스킬·특성 빌드 처리
    if (ClassProgress.IsValidIndex(int32(ForClass)) && ClassProgress[int32(ForClass)].Ranks.IsValidIndex(3))
    {
        PassiveRank = ClassProgress[int32(ForClass)].Ranks[3];
    }

    // 체력·마나·보호막·피해량 처리
    Stats.Attack = (Weapon ? Weapon->GetEffectivePower() : 8) + (Level - 1) * 3 +
                   (ForClass == EFableClass::Archer ? PassiveRank * 2 : 0);
    Stats.Defense = (Armor ? Armor->GetEffectivePower() : 0) + (ForClass == EFableClass::Warrior ? PassiveRank * 3 : 0);
    Stats.MaxHealth = (Level - 1) * 8 + (Armor ? Armor->Rarity * 5 : 0);
    Stats.ManaRegen = 8.0f + (ForClass == EFableClass::Mage ? PassiveRank * 1.5f : 0.0f);

    // 직업·스킬·특성 빌드 처리
    if (ClassProgress.IsValidIndex(int32(ForClass)))
    {
        // 직업·스킬·특성 빌드에 필요한 값 준비
        const FFableClassProgress& Build = ClassProgress[int32(ForClass)];
        const int32 First = FableRPG::TalentRank(Build, EFableTalentPath::First);
        const int32 Second = FableRPG::TalentRank(Build, EFableTalentPath::Second);
        Stats.Attack += 3.0f * First;

        // 이 단계에서 착용 장비의 기본 성능·옵션·세트 효과를 합산한다.
        if (ForClass == EFableClass::Warrior)
        {
            Stats.Defense += 4.0f * Second;
        }

        // 체력·마나·보호막·피해량 처리
        if (ForClass == EFableClass::Mage)
        {
            Stats.ManaRegen += 1.5f * Second;
        }
    }

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem* Item : {Weapon, Armor})
    {
        if (!Item)
        {
            continue;
        }
        for (const FFableItemAffix& Affix : Item->Affixes)
        {
            switch (Affix.Stat)
            {
            // 체력·마나·보호막·피해량 처리
            case EFableItemStat::Attack:
                Stats.Attack += Affix.Value;
                break;
            case EFableItemStat::Defense:
                Stats.Defense += Affix.Value;
                break;
            case EFableItemStat::MaxHealth:
                Stats.MaxHealth += Affix.Value;
                break;
            case EFableItemStat::ManaRegen:
                Stats.ManaRegen += Affix.Value / 10.0f;
                break;
            default:
                break;
            }
        }
    }

    // 체력·마나·보호막·피해량 처리
    if (Weapon && Armor && Weapon->SetId == Armor->SetId)
    {
        if (Weapon->SetId == EFableEquipmentSet::RootGuardian)
        {
            Stats.Attack += 5;
            Stats.Defense += 3;
        }
        else if (Weapon->SetId == EFableEquipmentSet::Moonlight)
        {
            Stats.Attack += 7;
            Stats.MaxHealth += 15;
            Stats.ManaRegen += 1.0f;
        }
    }

    return Stats;
}

/** 공격력 값을 반환한다. */
float AFablePlayerState::GetAttackPower() const
{
    return CalculateEquipmentStats(EquippedWeaponId, EquippedArmorId, HeroClass).Attack;
}

/** 방어력 값을 반환한다. */
float AFablePlayerState::GetDefense() const
{
    return CalculateEquipmentStats(EquippedWeaponId, EquippedArmorId, HeroClass).Defense;
}

/** 장비가 추가하는 최대 체력 값을 반환한다. */
float AFablePlayerState::GetMaxHealthBonus() const
{
    return CalculateEquipmentStats(EquippedWeaponId, EquippedArmorId, HeroClass).MaxHealth;
}

/** 초당 마나 회복량 값을 반환한다. */
float AFablePlayerState::GetManaRegenRate() const
{
    return CalculateEquipmentStats(EquippedWeaponId, EquippedArmorId, HeroClass).ManaRegen;
}

/** 현재 장비와 선택 장비의 성능 비교 값을 반환한다. */
bool AFablePlayerState::GetEquipmentPreview(int32 ItemId, FFableEquipmentStats& Before,
                                            FFableEquipmentStats& After) const
{
    // 아이템·장비 옵션·인벤토리와 창고 처리
    Before = CalculateEquipmentStats(EquippedWeaponId, EquippedArmorId, HeroClass);
    After = Before;
    const FFableItem* Item = FindItem(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Item || (Item->Slot == EFableEquipmentSlot::Weapon && Item->HeroClass != HeroClass))
    {
        return false;
    }

    After = CalculateEquipmentStats(Item->Slot == EFableEquipmentSlot::Weapon ? ItemId : EquippedWeaponId,
                                    Item->Slot == EFableEquipmentSlot::Armor ? ItemId : EquippedArmorId, HeroClass);

    return true;
}

/** 착용 중인 같은 세트 장비 수 값을 반환한다. */
int32 AFablePlayerState::GetEquippedSetCount(EFableEquipmentSet SetId, int32 ReplacementItemId) const
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (SetId == EFableEquipmentSet::None || uint8(SetId) > uint8(EFableEquipmentSet::Moonlight))
    {
        return 0;
    }

    int32 WeaponId = EquippedWeaponId, ArmorId = EquippedArmorId;

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (ReplacementItemId != INDEX_NONE)
    {
        const FFableItem* Replacement = FindItem(ReplacementItemId);
        if (!Replacement || (Replacement->Slot == EFableEquipmentSlot::Weapon && Replacement->HeroClass != HeroClass))
        {
            return 0;
        }
        if (Replacement->Slot == EFableEquipmentSlot::Weapon)
        {
            WeaponId = ReplacementItemId;
        }
        else
        {
            ArmorId = ReplacementItemId;
        }
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FFableItem* Weapon = FindItem(WeaponId);
    const FFableItem* Armor = FindItem(ArmorId);

    return (Weapon && Weapon->Slot == EFableEquipmentSlot::Weapon && Weapon->HeroClass == HeroClass &&
                    Weapon->SetId == SetId && !Weapon->IsBroken()
                ? 1
                : 0) +
           (Armor && Armor->Slot == EFableEquipmentSlot::Armor && Armor->SetId == SetId && !Armor->IsBroken() ? 1 : 0);
}

/** 선택한 스킬의 강화 단계 값을 반환한다. */
int32 AFablePlayerState::GetSkillRank(int32 Slot) const
{
    const int32 ClassIndex = int32(HeroClass);

    // 직업·스킬·특성 빌드 조건 검사
    if (Slot < 0 || Slot > 3 || !ClassProgress.IsValidIndex(ClassIndex) ||
        !ClassProgress[ClassIndex].Ranks.IsValidIndex(Slot))
    {
        return 0;
    }

    return ClassProgress[ClassIndex].Ranks[Slot];
}

/** 현재 전직 단계 값을 반환한다. */
int32 AFablePlayerState::GetAdvancementTier() const
{
    const int32 ClassIndex = int32(HeroClass);
    return ClassProgress.IsValidIndex(ClassIndex) ? ClassProgress[ClassIndex].Tier : 1;
}

/** 현재 전직 단계의 직업 이름 값을 반환한다. */
FString AFablePlayerState::GetAdvancedClassName() const
{
    return FableRPG::AdvancedClassName(HeroClass, GetAdvancementTier());
}

/** 스킬 해금 여부를 확인한다. */
bool AFablePlayerState::IsSkillUnlocked(int32 Slot) const
{
    if (Slot < 0 || Slot > 3)
    {
        return false;
    }
    return Slot == 0 || Slot == 3 || GetAdvancementTier() >= Slot + 1;
}

/** 사용 가능한 스킬 포인트 값을 반환한다. */
int32 AFablePlayerState::GetAvailableSkillPoints() const
{
    // 직업·스킬·특성 빌드 조건 검사
    if (ClassProgress.Num() != 3)
    {
        return 0;
    }

    int32 Spent = 0;

    // 직업·스킬·특성 빌드 순회
    for (const FFableClassProgress& Entry : ClassProgress)
    {
        if (Entry.Ranks.Num() != 4)
        {
            return 0;
        }
        for (int32 Slot = 0; Slot < 4; ++Slot)
        {
            Spent += Slot == 3 ? Entry.Ranks[Slot] : Entry.Ranks[Slot] - 1;
        }
    }

    return FMath::Max(0, 1 + 2 * (Level - 1) - Spent);
}

/** 전직 시험 대상을 찾아 반환한다. */
const FFableAdvancementTrial* AFablePlayerState::FindTrial(int32 TargetTier) const
{
    if (!ClassProgress.IsValidIndex(int32(HeroClass)) || (TargetTier != 2 && TargetTier != 3))
    {
        return nullptr;
    }
    const FFableClassProgress& Entry = ClassProgress[int32(HeroClass)];
    return TargetTier == 2 ? &Entry.SecondTrial : &Entry.ThirdTrial;
}

/** 변경 가능한 전직 시험 대상을 찾아 반환한다. */
FFableAdvancementTrial* AFablePlayerState::FindMutableTrial(int32 TargetTier)
{
    if (!ClassProgress.IsValidIndex(int32(HeroClass)) || (TargetTier != 2 && TargetTier != 3))
    {
        return nullptr;
    }
    FFableClassProgress& Entry = ClassProgress[int32(HeroClass)];
    return TargetTier == 2 ? &Entry.SecondTrial : &Entry.ThirdTrial;
}

/** 전직 시험의 진행도 값을 반환한다. */
int32 AFablePlayerState::GetAdvancementTrialProgress(int32 TargetTier) const
{
    const FFableAdvancementTrial* Trial = FindTrial(TargetTier);
    return Trial ? Trial->ValidSkillCasts : 0;
}

/** 기존 저장의 전직 인정 여부를 확인한다. */
bool AFablePlayerState::IsAdvancementLegacyGranted(int32 TargetTier) const
{
    const FFableAdvancementTrial* Trial = FindTrial(TargetTier);
    return Trial && Trial->bLegacyGranted;
}

/** 전직 시험의 단계별 필수 조건이 모두 충족되었는지 확인한다. */
bool AFablePlayerState::AreTrialRequirementsMet(int32 TargetTier, const FFableAdvancementTrial& Trial) const
{
    // 직업·스킬·특성 빌드 조건 검사
    if (Trial.ValidSkillCasts < 3 || ForestQuest != EFableQuestStage::Completed)
    {
        return false;
    }
    if (TargetTier == 2)
    {
        return Level >= 2;
    }

    return TargetTier == 3 && Level >= 4 && DungeonCompletions >= 1 && MoonwellCompletions >= 1;
}

/** 현재 진행도를 바탕으로 전직 시험의 완료 가능 상태를 갱신한다. */
void AFablePlayerState::RefreshTrialReadiness()
{
    if (!HasAuthority())
    {
        return;
    }
    for (FFableClassProgress& Entry : ClassProgress)
    {
        if (Entry.SecondTrial.Stage == EFableQuestStage::Active && AreTrialRequirementsMet(2, Entry.SecondTrial))
        {
            Entry.SecondTrial.Stage = EFableQuestStage::Ready;
        }
        if (Entry.ThirdTrial.Stage == EFableQuestStage::Active && Entry.Tier >= 2 &&
            AreTrialRequirementsMet(3, Entry.ThirdTrial))
        {
            Entry.ThirdTrial.Stage = EFableQuestStage::Ready;
        }
    }
}

/** 전직에 필요한 조건을 만족하는지 확인한다. */
bool AFablePlayerState::CanAdvanceClass() const
{
    const int32 TargetTier = GetAdvancementTier() + 1;
    const FFableAdvancementTrial* Trial = FindTrial(TargetTier);
    return bProfileLoaded && Trial && Trial->Stage == EFableQuestStage::Ready &&
           AreTrialRequirementsMet(TargetTier, *Trial);
}

/** 전직에 필요한 조건 안내 값을 반환한다. */
FString AFablePlayerState::GetAdvancementRequirementText() const
{
    if (GetAdvancementTier() >= 3)
    {
        return TEXT("최종 전직을 완료했습니다.");
    }
    return GetQuestDescription(GetAdvancementTier() == 1 ? EFableQuestId::SecondAdvancement
                                                         : EFableQuestId::ThirdAdvancement);
}

/** 이전 전직 시험 시전의 식별 문맥을 무효화한다. */
void AFablePlayerState::InvalidateTrialCasts()
{
    ++TrialGeneration;

    // 이 단계에서 이전 전직 시험 시전의 식별 문맥을 무효화한다.
    if (TrialGeneration == 0)
    {
        TrialGeneration = 1;
    }

    NextTrialCastId = 0;
    CreditedTrialCasts.Reset();
}

/** 실제로 시작한 서버 스킬의 전직 시험 식별 문맥을 발급한다. */
FFableTrialCastContext AFablePlayerState::CreateTrialCastContext(int32 Slot)
{
    FFableTrialCastContext Context;

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || (bBackendEnabled && !IsBackendReady()) ||
        Slot != GetAdvancementTier() - 1)
    {
        return Context;
    }

    const FFableAdvancementTrial* Trial = FindTrial(Slot + 2);

    // 직업·스킬·특성 빌드 조건 검사
    if (!Trial || Trial->Stage != EFableQuestStage::Active || Trial->ValidSkillCasts >= 3)
    {
        return Context;
    }

    // 직업·스킬·특성 빌드 처리
    Context.HeroClass = HeroClass;
    Context.SkillSlot = Slot;
    Context.Generation = TrialGeneration;
    Context.CastId = ++NextTrialCastId;

    return Context;
}

/** 서버가 발급한 시전 문맥을 확인하고 전직 시험의 적중을 기록한다. */
void AFablePlayerState::RecordTrialSkillHit(const FFableTrialCastContext& Context, bool bAppliedSlow)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || (bBackendEnabled && !IsBackendReady()) || Context.CastId == 0 ||
        Context.CastId > NextTrialCastId || Context.Generation != TrialGeneration || Context.HeroClass != HeroClass ||
        Context.SkillSlot != GetAdvancementTier() - 1 || CreditedTrialCasts.Contains(Context.CastId))
    {
        return;
    }

    FFableAdvancementTrial* Trial = FindMutableTrial(Context.SkillSlot + 2);

    // 직업·스킬·특성 빌드 조건 검사
    if (!Trial || Trial->Stage != EFableQuestStage::Active || Trial->ValidSkillCasts >= 3)
    {
        return;
    }
    if (HeroClass == EFableClass::Mage && Context.SkillSlot == 1 && !bAppliedSlow)
    {
        return;
    }

    // 직업·스킬·특성 빌드 처리
    CreditedTrialCasts.Add(Context.CastId);
    ++Trial->ValidSkillCasts;
    Changed(FString::Printf(TEXT("%d차 전직 시험: %s의 실제 유효 시전 %d / 3."), Context.SkillSlot + 2,
                            *FableRPG::SkillName(HeroClass, Context.SkillSlot), Trial->ValidSkillCasts));
}

/** 전직 시험의 시작 또는 완료를 현재 조건에 맞게 처리한다. */
void AFablePlayerState::InteractAdvancementTrial(int32 TargetTier)
{
    FFableAdvancementTrial* Trial = FindMutableTrial(TargetTier);

    // 퀘스트와 이야기 진행 조건 검사
    if (!Trial)
    {
        SetNotice(TEXT("전직 시험 정보를 읽을 수 없습니다."));
        return;
    }
    if (GetAdvancementTier() >= TargetTier || Trial->Stage == EFableQuestStage::Completed)
    {
        SetNotice(TargetTier == 3 ? TEXT("최종 전직을 이미 완료했습니다.") : TEXT("2차 전직을 이미 완료했습니다."));
        return;
    }
    if (TargetTier != GetAdvancementTier() + 1)
    {
        SetNotice(TEXT("2차 전직을 완료한 뒤 3차 시험을 받을 수 있습니다."));
        return;
    }

    // 퀘스트와 이야기 진행 처리
    if (Trial->Stage == EFableQuestStage::Available)
    {
        InvalidateTrialCasts();
        Trial->Stage = EFableQuestStage::Active;
        Changed(FString::Printf(TEXT("%d차 전직 시험을 수락했습니다. %s"), TargetTier,
                                *GetQuestDescription(TargetTier == 2 ? EFableQuestId::SecondAdvancement
                                                                     : EFableQuestId::ThirdAdvancement)));
        return;
    }

    RefreshTrialReadiness();

    // 퀘스트와 이야기 진행 조건 검사
    if (Trial->Stage != EFableQuestStage::Ready || !AreTrialRequirementsMet(TargetTier, *Trial))
    {
        SetNotice(
            GetQuestDescription(TargetTier == 2 ? EFableQuestId::SecondAdvancement : EFableQuestId::ThirdAdvancement));
        return;
    }
    // Commit the quest and tier together before saving, so repeated requests grant nothing.

    // 접속 권한·저장 리비전·멱등 키 처리
    Trial->Stage = EFableQuestStage::Completed;
    ClassProgress[int32(HeroClass)].Tier = TargetTier;
    BumpClassBuildRevision();
    InvalidateTrialCasts();
    Changed(FString::Printf(TEXT("%s로 전직했습니다! %s 스킬을 해금했습니다."), *GetAdvancedClassName(),
                            *FableRPG::SkillName(HeroClass, TargetTier - 1)));
}

/** 사용 가능한 포인트와 스킬 조건을 확인해 강화 단계를 올린다. */
void AFablePlayerState::UpgradeSkill(int32 Slot)
{
    // 직업·스킬·특성 빌드 조건 검사
    if (!AllowMutation())
    {
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()))
    {
        SetNotice(TEXT("살아 있는 상태로 안내인 로완 근처에서 스킬을 강화하세요."));
        return;
    }
    if (Slot < 0 || Slot > 3)
    {
        SetNotice(TEXT("알 수 없는 스킬입니다."));
        return;
    }
    if (!IsSkillUnlocked(Slot))
    {
        SetNotice(TEXT("전직으로 스킬을 해금한 뒤 강화할 수 있습니다."));
        return;
    }

    const int32 ClassIndex = int32(HeroClass);

    // 직업·스킬·특성 빌드 조건 검사
    if (!ClassProgress.IsValidIndex(ClassIndex) || ClassProgress[ClassIndex].Ranks.Num() != 4)
    {
        SetNotice(TEXT("직업 정보를 읽을 수 없습니다. 스킬 포인트는 사용하지 않았습니다."));
        return;
    }

    int32& Rank = ClassProgress[ClassIndex].Ranks[Slot];

    // 직업·스킬·특성 빌드 조건 검사
    if (Rank >= 3)
    {
        SetNotice(TEXT("이 스킬은 최대 강화 단계인 3랭크입니다."));
        return;
    }
    if (GetAvailableSkillPoints() <= 0)
    {
        SetNotice(TEXT("스킬 포인트가 부족합니다. 레벨이 오를 때마다 2포인트를 받습니다."));
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    ++Rank;
    BumpClassBuildRevision();
    Changed(FString::Printf(TEXT("%s: %d랭크로 강화했습니다. 남은 스킬 포인트 %d."),
                            *FableRPG::SkillName(HeroClass, Slot), Rank, GetAvailableSkillPoints()));
}

/** 전직 시험과 단계 조건을 확인해 다음 전직을 적용한다. */
void AFablePlayerState::AdvanceClass()
{
    InteractQuest(GetAdvancementTier() == 1 ? EFableQuestId::SecondAdvancement : EFableQuestId::ThirdAdvancement);
}

/** 직업 변경 조건을 확인하고 현재 직업과 기본 장비를 갱신한다. */
void AFablePlayerState::SelectClass(EFableClass NewClass)
{
    // 위치·방향·거리 계산 조건 검사
    if (!AllowMutation())
    {
        return;
    }
    if (uint8(NewClass) > uint8(EFableClass::Archer))
    {
        SetNotice(TEXT("알 수 없는 직업입니다."));
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()))
    {
        SetNotice(TEXT("안내인 로완 근처에서 직업을 선택하세요. (1 / 2 / 3)"));
        return;
    }

    // 접속 권한·저장 리비전·멱등 키 처리
    if (HeroClass != NewClass)
    {
        InvalidateTrialCasts();
        BumpClassBuildRevision();
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    HeroClass = NewClass;
    const FFableItem* Best = nullptr;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (const FFableItem& Item : Inventory)
    {
        if (Item.Slot == EFableEquipmentSlot::Weapon && Item.HeroClass == NewClass &&
            (!Best || CalculateEquipmentStats(Item.Id, EquippedArmorId, NewClass).Attack >
                          CalculateEquipmentStats(Best->Id, EquippedArmorId, NewClass).Attack))
        {
            Best = &Item;
        }
    }

    // 이 단계에서 직업 변경 조건을 확인하고 현재 직업과 기본 장비를 갱신한다.
    if (Best)
    {
        EquippedWeaponId = Best->Id;
    }

    Changed(TEXT("직업 선택: ") + GetAdvancedClassName() +
            TEXT(". 해당 직업의 무기를 착용했습니다. 전직과 스킬 투자는 유지됩니다."));
}

/** 소유 여부와 착용 조건을 확인한 뒤 장비 슬롯을 변경한다. */
void AFablePlayerState::EquipItem(int32 ItemId)
{
    // 위치·방향·거리 계산 조건 검사
    if (!AllowMutation())
    {
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()) && !AtTownService(FableRPG::ForgeLocation()))
    {
        SetNotice(TEXT("안내인 로완이나 대장간 근처에서 장비를 착용하세요."));
        return;
    }

    const FFableItem* Item = FindItem(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Item)
    {
        SetNotice(TEXT("가방에 없는 장비입니다."));
        return;
    }
    if (Item->Slot == EFableEquipmentSlot::Weapon && Item->HeroClass != HeroClass)
    {
        SetNotice(TEXT("다른 직업의 무기입니다. 안내인 로완 근처에서 직업을 변경하세요."));
        return;
    }

    // 이 단계에서 소유 여부와 착용 조건을 확인한 뒤 장비 슬롯을 변경한다.
    if (Item->Slot == EFableEquipmentSlot::Weapon)
    {
        EquippedWeaponId = ItemId;
    }
    else
    {
        EquippedArmorId = ItemId;
    }

    Changed(TEXT("장비 착용: ") + Item->GetDisplayName() + TEXT("."));
}

/** 장비 강화에 필요한 골드 값을 반환한다. */
int32 AFablePlayerState::GetUpgradeGoldCost(int32 ItemId) const
{
    const FFableItem* Item = FindItem(ItemId);
    return Item && Item->Upgrade < 5 ? 30 * FMath::Square(Item->Upgrade + 1) : 0;
}

/** 장비 강화에 필요한 광석 값을 반환한다. */
int32 AFablePlayerState::GetUpgradeOreCost(int32 ItemId) const
{
    const FFableItem* Item = FindItem(ItemId);
    return Item && Item->Upgrade < 5 ? 2 * (Item->Upgrade + 1) : 0;
}

/** 장비 강화 조건과 비용을 확인하고 원래 옵션을 유지한 채 강화한다. */
void AFablePlayerState::EnhanceItem(int32 ItemId)
{
    // 위치·방향·거리 계산 조건 검사
    if (!AllowMutation())
    {
        return;
    }
    if (!AtTownService(FableRPG::ForgeLocation()))
    {
        SetNotice(TEXT("대장간에서 장비를 강화할 수 있습니다. I로 가방을 여세요."));
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableItem* Item = Inventory.FindByPredicate(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Item)
    {
        SetNotice(TEXT("가방에 없는 장비입니다."));
        return;
    }
    if (Item->Upgrade >= 5)
    {
        SetNotice(TEXT("이 장비는 최대 강화 단계인 +5에 도달했습니다."));
        return;
    }

    const int32 GoldCost = GetUpgradeGoldCost(ItemId), OreCost = GetUpgradeOreCost(ItemId);

    // 재화와 작업 비용 조건 검사
    if (Gold < GoldCost || Ore < OreCost)
    {
        SetNotice(TEXT("골드나 광석이 부족합니다. 의뢰를 완료하거나 적을 처치해 모으세요."));
        return;
    }
    // Both costs and ownership are checked before the single authoritative mutation.

    // 재화와 작업 비용 처리
    Gold -= GoldCost;
    Ore -= OreCost;
    ++Item->Upgrade;
    Changed(TEXT("장비 강화: ") + Item->GetDisplayName() + TEXT("! 강화는 항상 성공하며 장비가 파괴되지 않습니다."));
}

/** 상점 이용 조건과 골드를 확인하고 구매 물품을 지급한다. */
void AFablePlayerState::BuyShopItem(EFableShopOffer Offer)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!AllowMutation())
    {
        return;
    }

    const int32 Price = FableRPG::ShopPrice(Offer);

    // 위치·방향·거리 계산 조건 검사
    if (Price <= 0)
    {
        SetNotice(TEXT("판매하지 않는 상품입니다."));
        return;
    }
    if (!AtTownService(FableRPG::MerchantLocation()))
    {
        SetNotice(TEXT("살아 있는 상태로 마을 상인에게 가까이 다가가세요."));
        return;
    }

    // 체력·마나·보호막·피해량에 필요한 값 준비
    const bool bHealthPotion = Offer == EFableShopOffer::HealthPotion;
    const bool bManaPotion = Offer == EFableShopOffer::ManaPotion;

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if ((bHealthPotion && HealthPotions >= 20) || (bManaPotion && ManaPotions >= 20))
    {
        SetNotice(TEXT("물약은 종류별로 최대 20개까지 보유할 수 있습니다."));
        return;
    }
    if (!bHealthPotion && !bManaPotion && (Inventory.Num() >= 32 || NextItemId >= 999999))
    {
        SetNotice(TEXT("가방에 장비를 더 넣을 수 없습니다. 필요 없는 장비를 판매해 주세요."));
        return;
    }
    if (Gold < Price)
    {
        SetNotice(TEXT("골드가 부족합니다."));
        return;
    }

    // Validate everything before spending gold or granting an item.

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (bHealthPotion)
    {
        ++HealthPotions;
    }
    else if (bManaPotion)
    {
        ++ManaPotions;
    }
    else
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Item;
        Item.Id = NextItemId;
        Item.Rarity = 1;
        Item.Slot = Offer == EFableShopOffer::Armor ? EFableEquipmentSlot::Armor : EFableEquipmentSlot::Weapon;
        Item.HeroClass = Offer == EFableShopOffer::MageWeapon
                             ? EFableClass::Mage
                             : (Offer == EFableShopOffer::ArcherWeapon ? EFableClass::Archer : EFableClass::Warrior);
        FRandomStream Random(FMath::Rand());

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!FableRPG::RollItem(Item, Item.Slot == EFableEquipmentSlot::Armor ? 6 : 30, EFableDifficulty::Normal,
                                Random))
        {
            SetNotice(TEXT("장비를 생성할 수 없어 구매를 취소했습니다. 골드는 사용하지 않았습니다."));
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        Item.InstanceId = FGuid::NewGuid();
        ++NextItemId;
        Inventory.Add(Item);
    }

    // 재화와 작업 비용 처리
    Gold -= Price;
    Changed(FString::Printf(TEXT("%s 구매 완료. %d 골드를 사용했습니다."), *FableRPG::ShopName(Offer), Price));
}

/** 아이템 판매 가격 값을 반환한다. */
int32 AFablePlayerState::GetSellPrice(int32 ItemId) const
{
    const FFableItem* Item = FindItem(ItemId);
    if (!Item || ItemId <= 4 || ItemId == EquippedWeaponId || ItemId == EquippedArmorId || Item->Rarity < 1)
    {
        return 0;
    }
    return (Item->Rarity >= 2 ? 60 : 25) + FMath::Clamp(Item->Upgrade, 0, 5) * 5;
}

/** 판매 가능한 소유 아이템을 제거하고 판매 대금을 지급한다. */
void AFablePlayerState::SellItem(int32 ItemId)
{
    // 위치·방향·거리 계산 조건 검사
    if (!AllowMutation())
    {
        return;
    }
    if (!AtTownService(FableRPG::MerchantLocation()))
    {
        SetNotice(TEXT("살아 있는 상태로 마을 상인에게 가까이 다가가세요."));
        return;
    }

    const FFableItem* Item = FindItem(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Item)
    {
        SetNotice(TEXT("가방에 없는 장비입니다."));
        return;
    }
    if (ItemId >= 1 && ItemId <= 4)
    {
        SetNotice(TEXT("직업 변경에 필요한 기본 장비는 판매할 수 없습니다."));
        return;
    }
    if (ItemId == EquippedWeaponId || ItemId == EquippedArmorId)
    {
        SetNotice(TEXT("착용 중인 장비는 판매할 수 없습니다. 다른 장비를 먼저 착용하세요."));
        return;
    }

    const int32 Price = GetSellPrice(ItemId);

    // 재화와 작업 비용 조건 검사
    if (Price <= 0)
    {
        SetNotice(TEXT("판매할 수 없는 장비입니다."));
        return;
    }
    if (Gold > 1000000 - Price)
    {
        SetNotice(TEXT("골드 보유 한도에 도달해 판매할 수 없습니다."));
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FString ItemName = Item->GetDisplayName();
    Inventory.RemoveAll(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });
    Gold += Price;
    Changed(FString::Printf(TEXT("%s 판매 완료. %d 골드를 받았습니다."), *ItemName, Price));
}

/** 물약의 남은 재사용 대기시간 값을 반환한다. */
float AFablePlayerState::GetPotionCooldownRemaining() const
{
    const UWorld* World = GetWorld();

    // 현재 월드와 플레이어 참조 조건 검사
    if (!World)
    {
        return 0.0f;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    const AGameStateBase* State = World->GetGameState();
    const float Now = State ? State->GetServerWorldTimeSeconds() : World->GetTimeSeconds();

    return FMath::Max(0.0f, NextPotionServerTime - Now);
}

/** 생존·잔량·대기시간을 확인하고 회복 성공 시 물약을 소모한다. */
void AFablePlayerState::UsePotion(bool bMana)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!AllowMutation())
    {
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    APlayerController* PC = Cast<APlayerController>(GetOwner());
    AFableCharacter* Character = PC ? Cast<AFableCharacter>(PC->GetPawn()) : nullptr;

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Character || !Character->IsAlive())
    {
        SetNotice(TEXT("쓰러진 상태에서는 물약을 사용할 수 없습니다."));
        return;
    }

    const float Now = GetWorld()->GetTimeSeconds();

    // 서버 시각과 재사용 대기시간 조건 검사
    if (Now < NextPotionServerTime)
    {
        SetNotice(TEXT("물약은 5초에 한 번 사용할 수 있습니다."));
        return;
    }

    int32& Count = bMana ? ManaPotions : HealthPotions;

    // 체력·마나·보호막·피해량 조건 검사
    if (Count <= 0)
    {
        SetNotice(bMana ? TEXT("마나 물약이 없습니다. 마을 상인에게 구매하세요.")
                        : TEXT("체력 물약이 없습니다. 마을 상인에게 구매하세요."));
        return;
    }
    if (!Character->RestoreResource(bMana, bMana ? 50.0f : 60.0f))
    {
        SetNotice(bMana ? TEXT("마나가 이미 가득 차 있습니다. 물약을 사용하지 않았습니다.")
                        : TEXT("체력이 이미 가득 차 있습니다. 물약을 사용하지 않았습니다."));
        return;
    }

    // 체력·마나·보호막·피해량 처리
    --Count;
    NextPotionServerTime = Now + 5.0f;
    Changed(bMana ? TEXT("마나 물약을 사용했습니다. 마나를 최대 50 회복했습니다.")
                  : TEXT("체력 물약을 사용했습니다. 체력을 최대 60 회복했습니다."));
}

/** 경험치를 추가하고 레벨 상승 조건을 적용한다. */
void AFablePlayerState::AddExperience(int32 Amount)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bBackendEnabled && !IsBackendReady())
    {
        return;
    }

    Experience += FMath::Max(0, Amount);

    // 같은 규칙을 적용할 대상을 순회한다.
    while (Level < 20 && Experience >= Level * 100)
    {
        Experience -= Level * 100;
        ++Level;
    }

    // 이 단계에서 경험치를 추가하고 레벨 상승 조건을 적용한다.
    if (Level == 20)
    {
        Experience = 0;
    }
}

/** 정해진 성능 범위와 난수로 장비 품질·기본 성능·옵션을 생성한다. */
FFableItem AFablePlayerState::RollItem(EFableEquipmentSlot Slot, EFableClass ItemClass, int32 Rarity,
                                       int32 NominalPower, EFableEquipmentSet SetId, EFableDifficulty Difficulty)
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableItem Item;
    Item.Id = NextItemId;
    Item.Slot = Slot;
    Item.HeroClass = ItemClass;
    Item.Rarity = Rarity;
    Item.SetId = SetId;
    FRandomStream Random(FMath::Rand());

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (!FableRPG::RollItem(Item, NominalPower, Difficulty, Random))
    {
        Item.Id = 0;
    }
    else
    {
        Item.InstanceId = FGuid::NewGuid();
    }

    return Item;
}

/** 새 아이템을 인벤토리에 추가하고 식별자 진행 상태를 갱신한다. */
bool AFablePlayerState::AddItem(EFableEquipmentSlot Slot, EFableClass ItemClass, int32 Rarity, int32 NominalPower,
                                EFableEquipmentSet SetId, EFableDifficulty Difficulty)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || (bBackendEnabled && !IsBackendReady()))
    {
        return false;
    }
    if (Inventory.Num() >= 32 || NextItemId >= 999999)
    {
        Ore = FMath::Min(100000, Ore + 6);
        return false;
    }

    FFableItem Item = RollItem(Slot, ItemClass, Rarity, NominalPower, SetId, Difficulty);

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (Item.Id == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("FABLE_LOOT_INVALID_INPUT rarity=%d nominal=%d"), Rarity, NominalPower);
        return false;
    }

    // 아이템·장비 옵션·인벤토리와 창고 처리
    ++NextItemId;
    UE_LOG(LogTemp, Display, TEXT("FABLE_LOOT_GRANTED slot=%s item=%s difficulty=%d"), *ProfileSlot,
           *FableRPG::ItemPersistentSignature(Item), int32(Difficulty));
    Inventory.Add(MoveTemp(Item));

    return true;
}

/** 던전 입장 자격 여부를 확인한다. */
bool AFablePlayerState::IsDungeonEligible() const
{
    return IsRegionEligible(EFableRegion::RootRuins);
}

/** 지역 입장 자격 여부를 확인한다. */
bool AFablePlayerState::IsRegionEligible(EFableRegion Region) const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (!bProfileLoaded)
    {
        return false;
    }
    if (Region == EFableRegion::RootRuins)
    {
        return ForestQuest == EFableQuestStage::Completed && DungeonQuest == EFableQuestStage::Active;
    }
    if (Region == EFableRegion::Moonwell)
    {
        return DungeonCompletions >= 1 && MoonwellQuest == EFableQuestStage::Active;
    }

    return Region == EFableRegion::Village;
}

/** 퀘스트 진행 단계 값을 반환한다. */
EFableQuestStage AFablePlayerState::GetQuestStage(EFableQuestId Quest) const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (Quest == EFableQuestId::Story)
    {
        return StoryChapter >= 5 ? EFableQuestStage::Completed
               : IsStoryReady()  ? EFableQuestStage::Ready
                                 : EFableQuestStage::Active;
    }
    if (Quest == EFableQuestId::Forest)
    {
        return ForestQuest;
    }
    if (Quest == EFableQuestId::RootRuins)
    {
        return DungeonQuest;
    }
    if (Quest == EFableQuestId::Moonwell)
    {
        return MoonwellQuest;
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    const FFableAdvancementTrial* Trial = FindTrial(Quest == EFableQuestId::SecondAdvancement  ? 2
                                                    : Quest == EFableQuestId::ThirdAdvancement ? 3
                                                                                               : 0);

    return Trial ? Trial->Stage : EFableQuestStage::Available;
}

/** 퀘스트 제목 값을 반환한다. */
FString AFablePlayerState::GetQuestTitle(EFableQuestId Quest) const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (Quest == EFableQuestId::Story)
    {
        return GetStoryTitle();
    }
    if (Quest == EFableQuestId::Forest)
    {
        return TEXT("숲 순찰");
    }
    if (Quest == EFableQuestId::RootRuins)
    {
        return TEXT("뿌리 유적의 심장");
    }
    if (Quest == EFableQuestId::Moonwell)
    {
        return TEXT("달빛 수원의 봉인");
    }
    if (Quest == EFableQuestId::SecondAdvancement)
    {
        return FableRPG::ClassName(HeroClass) + TEXT(" · 2차 전직 시험");
    }
    if (Quest == EFableQuestId::ThirdAdvancement)
    {
        return FableRPG::ClassName(HeroClass) + TEXT(" · 3차 전직 시험");
    }

    return TEXT("알 수 없는 의뢰");
}

/** 퀘스트 설명 값을 반환한다. */
FString AFablePlayerState::GetQuestDescription(EFableQuestId Quest) const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (Quest == EFableQuestId::Story)
    {
        return GetStoryDescription() + TEXT("\n") + GetStoryObjective() + TEXT("\n") + GetStoryRewardText();
    }
    if (Quest == EFableQuestId::Forest)
    {
        return FString::Printf(
            TEXT("마을 북쪽 수호자 처치 %d / 3. 로완에게 보고하면 골드 90, 광석 4, 경험치 100과 숲의 무기를 받습니다."),
            QuestKills);
    }
    if (Quest == EFableQuestId::RootRuins)
    {
        return FString::Printf(
            TEXT(
                "숲 순찰 보상 수령 후 수락. 뿌리 유적의 습격과 고대 수호자를 돌파하세요. 보스는 뿌리 수호자 세트 무기/갑옷을 차례로 지급합니다. 로완 보고 완료 %d회."),
            DungeonCompletions);
    }
    if (Quest == EFableQuestId::Moonwell)
    {
        return FString::Printf(
            TEXT(
                "뿌리 유적 보상 수령 1회 필요 (%d/1). 달빛 수원의 세 봉인을 풀고 보스를 처치한 뒤 보고하세요. 보스에게 달빛 세트 무기/갑옷을 차례로 받고, 로완 보고 시 추가 무기를 얻습니다. 완료 %d회."),
            DungeonCompletions, MoonwellCompletions);
    }

    // 퀘스트와 이야기 진행에 필요한 값 준비
    const int32 TargetTier = Quest == EFableQuestId::SecondAdvancement  ? 2
                             : Quest == EFableQuestId::ThirdAdvancement ? 3
                                                                        : 0;
    const FFableAdvancementTrial* Trial = FindTrial(TargetTier);

    // 퀘스트와 이야기 진행 조건 검사
    if (!Trial)
    {
        return TEXT("알 수 없는 의뢰입니다.");
    }
    if (Trial->bLegacyGranted)
    {
        return TEXT("기존 저장에서 획득한 전직을 보존했습니다. 시험은 면제되며 추가 보상은 없습니다.");
    }
    if (Trial->Stage == EFableQuestStage::Completed)
    {
        return TEXT("전직 시험과 보상 수령을 완료했습니다.");
    }

    // 직업·스킬·특성 빌드에 필요한 값 준비
    const TCHAR* Action =
        HeroClass == EFableClass::Mage && TargetTier == 3 ? TEXT("실제 둔화 성공") : TEXT("실제 명중");
    FString Text = FString::Printf(
        TEXT("수락 후 %s(%s) %s 시전 %d / 3. 한 번의 시전은 한 번만 인정됩니다. 레벨 %d/%d, 숲 보상 %s."),
        *FableRPG::SkillName(HeroClass, TargetTier - 2), TargetTier == 2 ? TEXT("K") : TEXT("L"), Action,
        Trial->ValidSkillCasts, Level, TargetTier == 2 ? 2 : 4,
        ForestQuest == EFableQuestStage::Completed ? TEXT("완료") : TEXT("미완료"));

    // 이 단계에서 퀘스트 설명 값을 반환한다.
    if (TargetTier == 3)
    {
        Text += FString::Printf(TEXT(" 뿌리 유적 보상 %d/1 · 달빛 수원 보상 %d/1. 보고하면 O 스킬을 해금합니다."),
                                DungeonCompletions, MoonwellCompletions);
    }
    else
    {
        Text += TEXT(" 로완에게 보고하면 L 스킬을 해금합니다.");
    }

    return Text;
}

/** 주변 대상 상호작용 퀘스트에 필요한 조건을 만족하는지 확인한다. */
bool AFablePlayerState::CanInteractQuest(EFableQuestId Quest) const
{
    // 퀘스트와 이야기 진행 조건 검사
    if (Quest == EFableQuestId::Story)
    {
        return bProfileLoaded && StoryChapter >= 0 && StoryChapter < 5;
    }
    if (!bProfileLoaded)
    {
        return false;
    }
    if (Quest == EFableQuestId::Forest)
    {
        return ForestQuest != EFableQuestStage::Completed;
    }
    if (Quest == EFableQuestId::RootRuins)
    {
        return ForestQuest == EFableQuestStage::Completed;
    }
    if (Quest == EFableQuestId::Moonwell)
    {
        return DungeonCompletions >= 1;
    }
    if (Quest == EFableQuestId::SecondAdvancement)
    {
        return GetAdvancementTier() == 1;
    }
    if (Quest == EFableQuestId::ThirdAdvancement)
    {
        return GetAdvancementTier() == 2;
    }

    return false;
}

/** 퀘스트의 수락·진행·보상 수령을 현재 단계에 맞게 처리한다. */
void AFablePlayerState::InteractQuest()
{
    InteractQuest(ForestQuest != EFableQuestStage::Completed ? EFableQuestId::Forest : EFableQuestId::RootRuins);
}

/** 퀘스트의 수락·진행·보상 수령을 현재 단계에 맞게 처리한다. */
void AFablePlayerState::InteractQuest(EFableQuestId Quest)
{
    // 퀘스트와 이야기 진행 조건 검사
    if (Quest == EFableQuestId::Story)
    {
        InteractStory();
        return;
    }
    if (!AllowMutation())
    {
        return;
    }
    if (!AtTownService(FableRPG::BoardLocation()))
    {
        SetNotice(TEXT("살아 있는 상태로 안내인 로완에게 의뢰를 받거나 보상을 받으세요."));
        return;
    }
    if (Quest == EFableQuestId::SecondAdvancement || Quest == EFableQuestId::ThirdAdvancement)
    {
        InteractAdvancementTrial(Quest == EFableQuestId::SecondAdvancement ? 2 : 3);
        return;
    }
    if (!CanInteractQuest(Quest))
    {
        SetNotice(GetQuestDescription(Quest));
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    AFableGameMode* Mode = GetWorld()->GetAuthGameMode<AFableGameMode>();
    APlayerController* PC = Cast<APlayerController>(GetOwner());

    // 재화와 작업 비용 처리
    if (Quest == EFableQuestId::Forest)
    {
        if (ForestQuest == EFableQuestStage::Available)
        {
            ForestQuest = EFableQuestStage::Active;
            QuestKills = 0;
            Changed(TEXT("의뢰 수락: 숲 순찰. 동료와 함께 숲의 수호자 3마리를 처치하세요."));
            if (Mode)
            {
                Mode->RequestForestEncounter(PC);
            }
        }
        else if (ForestQuest == EFableQuestStage::Active)
        {
            SetNotice(GetQuestDescription(Quest));
            if (Mode)
            {
                Mode->RequestForestEncounter(PC);
            }
        }
        else if (ForestQuest == EFableQuestStage::Ready)
        {
            // 재화와 작업 비용 처리
            ForestQuest = EFableQuestStage::Completed;
            Gold = FMath::Min(1000000, Gold + 90);
            Ore = FMath::Min(100000, Ore + 4);
            AddExperience(100);
            const bool bAdded = AddItem(EFableEquipmentSlot::Weapon, HeroClass, 1, 32);
            Changed(
                bAdded
                    ? TEXT(
                          "순찰 완료! 골드 90, 광석 4, 경험치 100과 숲의 무기를 받았습니다. 로완에게 전직 시험과 유적 의뢰를 확인하세요.")
                    : TEXT(
                          "순찰 완료! 골드 90, 광석 10, 경험치 100을 받았습니다. 가방이 가득 차 무기를 광석 6으로 바꿨습니다."));
        }
        return;
    }

    // 재화와 작업 비용 처리
    if (Quest == EFableQuestId::RootRuins)
    {
        if (DungeonQuest == EFableQuestStage::Available || DungeonQuest == EFableQuestStage::Completed)
        {
            DungeonQuest = EFableQuestStage::Active;
            Changed(TEXT("의뢰 수락: 뿌리 유적의 심장. 동료 모두 의뢰를 받고 동쪽 차원문에서 뿌리 유적을 선택하세요."));
        }
        else if (DungeonQuest == EFableQuestStage::Ready)
        {
            // 재화와 작업 비용 처리
            DungeonQuest = EFableQuestStage::Completed;
            DungeonCompletions = FMath::Min(1000000, DungeonCompletions + 1);
            Gold = FMath::Min(1000000, Gold + 150);
            Ore = FMath::Min(100000, Ore + 8);
            AddExperience(200);
            const bool bAdded = AddItem(EFableEquipmentSlot::Weapon, HeroClass, 2, 48);
            Changed(
                bAdded
                    ? TEXT(
                          "던전 완료! 골드 150, 광석 8, 경험치 200과 수호자의 무기를 받았습니다. 달빛 수원 의뢰를 받을 수 있습니다.")
                    : TEXT(
                          "던전 완료! 골드 150, 광석 14, 경험치 200을 받았습니다. 가방이 가득 차 무기를 광석 6으로 바꿨습니다."));
        }
        else
        {
            SetNotice(GetQuestDescription(Quest));
        }
        return;
    }

    // 재화와 작업 비용 처리
    if (Quest == EFableQuestId::Moonwell)
    {
        if (MoonwellQuest == EFableQuestStage::Available || MoonwellQuest == EFableQuestStage::Completed)
        {
            MoonwellQuest = EFableQuestStage::Active;
            Changed(TEXT("의뢰 수락: 달빛 수원의 봉인. 동료 모두 의뢰를 받고 차원문에서 달빛 수원을 선택하세요."));
        }
        else if (MoonwellQuest == EFableQuestStage::Ready)
        {
            // 재화와 작업 비용 처리
            MoonwellQuest = EFableQuestStage::Completed;
            MoonwellCompletions = FMath::Min(1000000, MoonwellCompletions + 1);
            Gold = FMath::Min(1000000, Gold + 220);
            Ore = FMath::Min(100000, Ore + 12);
            AddExperience(300);
            const bool bAdded = AddItem(EFableEquipmentSlot::Weapon, HeroClass, 3, 58);
            Changed(
                bAdded
                    ? TEXT(
                          "달빛 수원 완료! 골드 220, 광석 12, 경험치 300과 달빛의 무기를 받았습니다. 3차 전직 시험을 마치면 로완에게 보고하세요.")
                    : TEXT(
                          "달빛 수원 완료! 골드 220, 광석 18, 경험치 300을 받았습니다. 가방이 가득 차 무기를 광석 6으로 바꿨습니다."));
        }
        else
        {
            SetNotice(GetQuestDescription(Quest));
        }
    }
}

/** 처치가 발생한 지역과 난이도를 퀘스트 진행에 반영한다. */
void AFablePlayerState::RecordRegionKill(EFableRegion Region, bool bBoss, EFableDifficulty Difficulty)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (bBackendEnabled && !IsBackendReady())
    {
        return;
    }

    // 지역·적·전투 진행 처리
    if (Region == EFableRegion::Village)
    {
        if (!bBoss)
        {
            RecordKill(false);
        }
        return;
    }

    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || !GetWorld() ||
        (Region != EFableRegion::RootRuins && Region != EFableRegion::Moonwell) ||
        uint8(Difficulty) > uint8(EFableDifficulty::Hard))
    {
        return;
    }

    const APlayerController* PC = Cast<APlayerController>(GetOwner());

    // 지역·적·전투 진행 조건 검사
    if (!PC || !PC->GetPawn() || !FableRPG::IsInRegion(Region, PC->GetPawn()->GetActorLocation()))
    {
        return;
    }

    EFableQuestStage& Quest = Region == EFableRegion::Moonwell ? MoonwellQuest : DungeonQuest;

    // 퀘스트와 이야기 진행 조건 검사
    if (Quest != EFableQuestStage::Active)
    {
        return;
    }

    // 재화와 작업 비용에 필요한 값 준비
    const bool bMoon = Region == EFableRegion::Moonwell;
    const float Multiplier = Difficulty == EFableDifficulty::Hard ? 1.5f : 1.0f;
    const int32 GoldReward = FMath::RoundToInt((bBoss ? (bMoon ? 180 : 140) : (bMoon ? 22 : 18)) * Multiplier);
    const int32 OreReward = FMath::RoundToInt((bBoss ? (bMoon ? 10 : 8) : (bMoon ? 2 : 1)) * Multiplier);
    const int32 ExperienceReward = FMath::RoundToInt((bBoss ? (bMoon ? 300 : 250) : (bMoon ? 45 : 35)) * Multiplier);
    // A boss changes Active to Ready before any grant; a repeated callback cannot reroll or grant again.

    // 퀘스트와 이야기 진행 처리
    if (bBoss)
    {
        Quest = EFableQuestStage::Ready;
    }

    // 재화와 작업 비용 처리
    Gold = FMath::Min(1000000, Gold + GoldReward);
    Ore = FMath::Min(100000, Ore + OreReward);
    AddExperience(ExperienceReward);

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (bBoss)
    {
        // 재화와 작업 비용에 필요한 값 준비
        int32& RegionalMaterial = bMoon ? MoonCrystals : RootCores;
        RegionalMaterial = FMath::Min(100000, RegionalMaterial + (Difficulty == EFableDifficulty::Hard ? 2 : 1));
        const int32 Completions = bMoon ? MoonwellCompletions : DungeonCompletions;
        const EFableEquipmentSlot Slot =
            Completions % 2 == 0 ? EFableEquipmentSlot::Armor : EFableEquipmentSlot::Weapon;
        const int32 Nominal = Slot == EFableEquipmentSlot::Weapon ? (bMoon ? 58 : 48) : (bMoon ? 16 : 12);
        const bool bAdded =
            AddItem(Slot, HeroClass, bMoon ? 3 : 2, Nominal,
                    bMoon ? EFableEquipmentSet::Moonlight : EFableEquipmentSet::RootGuardian, Difficulty);
        const FString Loot = bAdded ? Inventory.Last().GetDisplayName() : TEXT("가방이 가득 차 장비를 광석 6으로 변환");
        Changed(FString::Printf(TEXT("%s 보스 처치! %s · 골드 %d · 광석 %d · 경험치 %d. 로완에게 보고하세요."),
                                *FableRPG::RegionName(Region), *Loot, GoldReward, OreReward, ExperienceReward));
    }
    else
    {
        Changed(FString::Printf(TEXT("%s 수호자 처치: 골드 %d · 광석 %d · 경험치 %d."), *FableRPG::RegionName(Region),
                                GoldReward, OreReward, ExperienceReward));
    }
}

/** 적 처치 경험치·재화·장비와 퀘스트 진행을 반영한다. */
void AFablePlayerState::RecordKill(bool bBoss)
{
    // 서버 권한과 실행 위치 조건 검사
    if (bBackendEnabled && !IsBackendReady())
    {
        return;
    }
    if (!HasAuthority() || !bProfileLoaded || !GetWorld())
    {
        return;
    }

    const AFableGameState* State = GetWorld()->GetGameState<AFableGameState>();

    // 지역·적·전투 진행 조건 검사
    if (!State)
    {
        return;
    }
    if (bBoss || State->AdventureStage == EFableAdventureStage::Dungeon ||
        State->AdventureStage == EFableAdventureStage::Boss)
    {
        // Legacy callers retain the normal reward table and receive the same region/quest safeguards.
        RecordRegionKill(EFableRegion::RootRuins, bBoss, EFableDifficulty::Normal);
        return;
    }

    const APlayerController* PC = Cast<APlayerController>(GetOwner());

    // 지역·적·전투 진행 조건 검사
    if (!PC || !PC->GetPawn() || !FableRPG::IsInRegion(EFableRegion::Village, PC->GetPawn()->GetActorLocation()))
    {
        return;
    }

    // 재화와 작업 비용 처리
    if (State->AdventureStage == EFableAdventureStage::Forest && ForestQuest == EFableQuestStage::Active)
    {
        QuestKills = FMath::Min(3, QuestKills + 1);
        if (QuestKills == 3)
        {
            ForestQuest = EFableQuestStage::Ready;
        }
        Gold = FMath::Min(1000000, Gold + 15);
        Ore = FMath::Min(100000, Ore + 1);
        AddExperience(35);
        Changed(QuestKills == 3 ? TEXT("순찰 목표를 달성했습니다! 안내인 로완에게 돌아가 보상을 받으세요.")
                                : TEXT("수호자 처치: 골드 15, 광석 1, 경험치 35를 받았습니다."));
    }
}

/** 창고 아이템 대상을 찾아 반환한다. */
const FFableItem* AFablePlayerState::FindStashItem(int32 ItemId) const
{
    return StashItems.FindByPredicate(
        [ItemId](const FFableItem& Item)
        {
            return Item.Id == ItemId;
        });
}

/** 제작에 필요한 재화 값을 반환한다. */
FFableResourceCost AFablePlayerState::GetCraftCost(int32 Rarity) const
{
    FFableResourceCost Cost;

    // 재화와 작업 비용 처리
    switch (Rarity)
    {
    // 재화와 작업 비용 처리
    case 1:
        Cost.Gold = 80;
        Cost.Ore = 3;
        Cost.Essence = 8;
        break;
    case 2:
        Cost.Gold = 150;
        Cost.Ore = 6;
        Cost.Essence = 16;
        Cost.RootCores = 1;
        break;
    case 3:
        Cost.Gold = 240;
        Cost.Ore = 10;
        Cost.Essence = 24;
        Cost.MoonCrystals = 1;
        break;
    case 4:
        Cost.Gold = 500;
        Cost.Ore = 20;
        Cost.Essence = 50;
        Cost.RootCores = 1;
        Cost.MoonCrystals = 2;
        break;
    default:
        break;
    }

    return Cost;
}

/** 분해로 얻을 재료 값을 반환한다. */
FFableResourceCost AFablePlayerState::GetDismantleYield(int32 ItemId) const
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableResourceCost Yield;
    const FFableItem* Item = FindItem(ItemId);

    // 재화와 작업 비용 처리
    if (Item && ItemId > 4 && ItemId != EquippedWeaponId && ItemId != EquippedArmorId)
    {
        Yield.Essence = 3 * Item->Rarity + 2 * int32(Item->Quality) + 2 * Item->Upgrade;
    }

    return Yield;
}

/** 옵션 재설정에 필요한 재화 값을 반환한다. */
FFableResourceCost AFablePlayerState::GetRerollCost(int32 ItemId) const
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableResourceCost Cost;
    const FFableItem* Item = FindItem(ItemId);

    // 아이템·장비 옵션·인벤토리와 창고 처리
    if (Item && Item->RollVersion > 0 && !Item->Affixes.IsEmpty())
    {
        Cost.Gold = 50 + 25 * Item->Rarity + 10 * Item->Upgrade;
        Cost.Essence = 3 + 2 * Item->Rarity;
    }

    return Cost;
}

/** 내구도 수리에 필요한 재화 값을 반환한다. */
FFableResourceCost AFablePlayerState::GetRepairCost(int32 ItemId) const
{
    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableResourceCost Cost;
    const FFableItem* Item = FindItem(ItemId);

    // 재화와 작업 비용 처리
    if (Item)
    {
        const int32 Missing = FMath::Max(0, Item->MaxDurability - Item->Durability);
        Cost.Gold = Missing * (Item->Rarity + 1);
        Cost.Ore = (Missing + 24) / 25;
    }

    return Cost;
}

/** 현재 재화로 요청한 비용을 모두 지불할 수 있는지 확인한다. */
bool AFablePlayerState::CanAfford(const FFableResourceCost& Cost) const
{
    return Cost.Gold >= 0 && Cost.Ore >= 0 && Cost.Essence >= 0 && Cost.RootCores >= 0 && Cost.MoonCrystals >= 0 &&
           Gold >= Cost.Gold && Ore >= Cost.Ore && Essence >= Cost.Essence && RootCores >= Cost.RootCores &&
           MoonCrystals >= Cost.MoonCrystals;
}

/** 비용을 지불할 수 있는지 확인한 뒤 각 재화를 차감한다. */
void AFablePlayerState::SpendResources(const FFableResourceCost& Cost)
{
    Gold -= Cost.Gold;
    Ore -= Cost.Ore;
    Essence -= Cost.Essence;
    RootCores -= Cost.RootCores;
    MoonCrystals -= Cost.MoonCrystals;
}

/** 공방 작업에 필요한 장비 데이터의 거부 사유를 구한다. */
FString AFablePlayerState::WorkshopDataBlockReason() const
{
    // 프로필과 저장 스냅숏 조건 검사
    if (!bProfileLoaded)
    {
        return TEXT("프로필을 불러오는 중입니다.");
    }
    if (bStorageBlocked)
    {
        return TEXT("저장 프로필을 확인할 수 없어 공방을 사용할 수 없습니다. 원본은 보존됩니다.");
    }

    return FString();
}

/** 장비 제작 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetCraftBlockReason(int32 Rarity, EFableEquipmentSlot Slot) const
{
    const FString Common = WorkshopDataBlockReason();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!Common.IsEmpty())
    {
        return Common;
    }
    if (FableRPG::CraftNominalPower(Rarity, Slot) == 0)
    {
        return TEXT("제작할 수 없는 등급이나 부위입니다.");
    }
    if (Inventory.Num() >= 32 || NextItemId >= 999999)
    {
        return TEXT("가방에 제작 장비를 넣을 자리가 없습니다.");
    }
    if (!CanAfford(GetCraftCost(Rarity)))
    {
        return TEXT("제작에 필요한 골드나 재료가 부족합니다.");
    }

    return FString();
}

/** 장비 분해 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetDismantleBlockReason(int32 ItemId) const
{
    const FString Common = WorkshopDataBlockReason();

    // 재화와 작업 비용 조건 검사
    if (!Common.IsEmpty())
    {
        return Common;
    }
    if (!FindItem(ItemId))
    {
        return TEXT("가방에 없는 장비입니다.");
    }
    if (ItemId <= 4)
    {
        return TEXT("기본 장비는 분해할 수 없습니다.");
    }
    if (ItemId == EquippedWeaponId || ItemId == EquippedArmorId)
    {
        return TEXT("착용 중인 장비는 분해할 수 없습니다.");
    }
    if (Essence > 100000 - GetDismantleYield(ItemId).Essence)
    {
        return TEXT("정수 보유 한도를 넘어 분해할 수 없습니다.");
    }

    return FString();
}

/** 옵션 재설정 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetRerollBlockReason(int32 ItemId, int32 AffixIndex) const
{
    const FString Common = WorkshopDataBlockReason();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Common.IsEmpty())
    {
        return Common;
    }

    const FFableItem* Item = FindItem(ItemId);

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!Item)
    {
        return TEXT("가방에 없는 장비입니다.");
    }
    if (Item->RollVersion == 0 || !Item->Affixes.IsValidIndex(AffixIndex))
    {
        return TEXT("재설정할 추가 옵션을 선택하세요. 고정 장비에는 옵션을 추가할 수 없습니다.");
    }
    if (!CanAfford(GetRerollCost(ItemId)))
    {
        return TEXT("옵션 재설정에 필요한 골드나 정수가 부족합니다.");
    }

    return FString();
}

/** 내구도 수리 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetRepairBlockReason(int32 ItemId) const
{
    const FString Common = WorkshopDataBlockReason();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Common.IsEmpty())
    {
        return Common;
    }

    const FFableItem* Item = FindItem(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Item)
    {
        return TEXT("수리할 장비를 가방에서 선택하세요.");
    }
    if (Item->Durability >= Item->MaxDurability)
    {
        return TEXT("이미 내구도가 가득 찬 장비입니다.");
    }
    if (!CanAfford(GetRepairCost(ItemId)))
    {
        return TEXT("수리에 필요한 골드나 광석이 부족합니다.");
    }

    return FString();
}

/** 창고 보관 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetDepositBlockReason(int32 ItemId) const
{
    const FString Common = WorkshopDataBlockReason();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!Common.IsEmpty())
    {
        return Common;
    }
    if (!FindItem(ItemId))
    {
        return TEXT("가방에 없는 장비입니다.");
    }
    if (ItemId <= 4)
    {
        return TEXT("기본 장비는 창고에 보관할 수 없습니다.");
    }
    if (ItemId == EquippedWeaponId || ItemId == EquippedArmorId)
    {
        return TEXT("착용 중인 장비는 창고에 보관할 수 없습니다.");
    }
    if (StashItems.Num() >= 48)
    {
        return TEXT("개인 창고가 가득 찼습니다. 최대 48개를 보관할 수 있습니다.");
    }
    if (FindStashItem(ItemId))
    {
        return TEXT("장비 식별자가 중복되어 보관할 수 없습니다.");
    }

    return FString();
}

/** 창고 수령 거부 사유 값을 반환한다. */
FString AFablePlayerState::GetWithdrawBlockReason(int32 ItemId) const
{
    const FString Common = WorkshopDataBlockReason();

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (!Common.IsEmpty())
    {
        return Common;
    }
    if (!FindStashItem(ItemId))
    {
        return TEXT("창고에 없는 장비입니다.");
    }
    if (Inventory.Num() >= 32)
    {
        return TEXT("가방이 가득 찼습니다. 최대 32개를 보유할 수 있습니다.");
    }
    if (FindItem(ItemId))
    {
        return TEXT("장비 식별자가 중복되어 꺼낼 수 없습니다.");
    }

    return FString();
}

/** 공방 이용 거리·저장 준비·요청 간격을 확인해 상태 변경을 시작한다. */
bool AFablePlayerState::BeginWorkshopMutation()
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!AllowMutation())
    {
        return false;
    }

    const FString Common = WorkshopDataBlockReason();

    // 위치·방향·거리 계산 조건 검사
    if (!Common.IsEmpty())
    {
        SetNotice(Common);
        return false;
    }
    if (!AtTownService(FableRPG::ForgeLocation()))
    {
        SetNotice(TEXT("살아 있는 상태로 마을 공방의 브람에게 가까이 다가가세요."));
        return false;
    }

    return true;
}

/** 제작 조건과 재료를 확인해 새 식별자를 가진 장비를 만든다. */
void AFablePlayerState::CraftItem(int32 Rarity, EFableEquipmentSlot Slot)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BeginWorkshopMutation())
    {
        return;
    }

    const FString Reason = GetCraftBlockReason(Rarity, Slot);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FFableResourceCost Cost = GetCraftCost(Rarity);
    FFableItem Item = RollItem(Slot, HeroClass, Rarity, FableRPG::CraftNominalPower(Rarity, Slot),
                               EFableEquipmentSet::None, EFableDifficulty::Normal);

    // 아이템·장비 옵션·인벤토리와 창고 조건 검사
    if (Item.Id == 0 || !FableRPG::IsValidItemDurability(Item))
    {
        SetNotice(TEXT("장비를 생성할 수 없어 제작을 취소했습니다. 재료는 사용하지 않았습니다."));
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FString ItemName = Item.GetDisplayName();
    SpendResources(Cost);
    ++NextItemId;
    Inventory.Add(MoveTemp(Item));
    Changed(FString::Printf(TEXT("%s 제작 완료. 품질과 추가 옵션이 결정되었습니다."), *ItemName));
}

/** 분해 가능한 소유 장비를 제거하고 정해진 재료를 지급한다. */
void AFablePlayerState::DismantleItem(int32 ItemId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BeginWorkshopMutation())
    {
        return;
    }

    const FString Reason = GetDismantleBlockReason(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FFableResourceCost Yield = GetDismantleYield(ItemId);
    const FString ItemName = FindItem(ItemId)->GetDisplayName();
    Inventory.RemoveAll(
        [ItemId](const FFableItem& Item)
        {
            return Item.Id == ItemId;
        });
    Essence += Yield.Essence;
    Changed(FString::Printf(TEXT("%s 분해 완료. 정수 %d개를 얻었습니다."), *ItemName, Yield.Essence));
}

/** 장비 옵션 재설정 조건과 비용을 확인해 선택한 옵션을 다시 생성한다. */
void AFablePlayerState::RerollAffix(int32 ItemId, int32 AffixIndex)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BeginWorkshopMutation())
    {
        return;
    }

    const FString Reason = GetRerollBlockReason(ItemId, AffixIndex);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableItem* Item = Inventory.FindByPredicate(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });
    const FFableResourceCost Cost = GetRerollCost(ItemId);
    const FFableItemAffix Original = Item->Affixes[AffixIndex];
    TArray<FFableItemAffix> Candidates;

    // 아이템·장비 옵션·인벤토리와 창고 순회
    for (int32 StatIndex = 0; StatIndex <= int32(EFableItemStat::ManaRegen); ++StatIndex)
    {
        const EFableItemStat Stat = static_cast<EFableItemStat>(StatIndex);
        bool bUsed = false;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Index = 0; Index < Item->Affixes.Num(); ++Index)
        {
            if (Index != AffixIndex && Item->Affixes[Index].Stat == Stat)
            {
                bUsed = true;
            }
        }

        // 이 단계에서 장비 옵션 재설정 조건과 비용을 확인해 선택한 옵션을 다시 생성한다.
        if (bUsed)
        {
            continue;
        }

        int32 Min = 0, Max = 0;
        FableRPG::AffixValueRange(Stat, Min, Max);

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Value = Min; Value <= Max; ++Value)
        {
            if (Stat == Original.Stat && Value == Original.Value)
            {
                continue;
            }
            FFableItemAffix Candidate;
            Candidate.Stat = Stat;
            Candidate.Value = Value;
            Candidates.Add(Candidate);
        }
    }

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Candidates.IsEmpty())
    {
        SetNotice(TEXT("다른 옵션 결과가 없어 재설정하지 않았습니다."));
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableItem Updated = *Item;
    Updated.Affixes[AffixIndex] = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
    // Preserve the unselected options in their original slots; never sort a rerolled item.

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!FableRPG::IsValidItemRoll(Updated) || !FableRPG::IsValidItemDurability(Updated))
    {
        SetNotice(TEXT("옵션 결과를 확인할 수 없어 재설정하지 않았습니다. 재료는 보존됩니다."));
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FString Result = FableRPG::AffixDescription(Updated.Affixes[AffixIndex]);
    SpendResources(Cost);
    *Item = MoveTemp(Updated);
    Changed(FString::Printf(TEXT("선택한 옵션을 재설정했습니다: %s. 다른 옵션과 장비 성능은 유지됩니다."), *Result));
}

/** 수리 가능한 장비의 비용을 차감하고 내구도를 회복한다. */
void AFablePlayerState::RepairItem(int32 ItemId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BeginWorkshopMutation())
    {
        return;
    }

    const FString Reason = GetRepairBlockReason(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    FFableItem* Item = Inventory.FindByPredicate(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });
    const FFableResourceCost Cost = GetRepairCost(ItemId);
    SpendResources(Cost);
    Item->Durability = Item->MaxDurability;
    Changed(TEXT("장비를 완전히 수리했습니다. 파손으로 중지되었던 장비 효과도 다시 적용됩니다."));
}

/** 보관 조건을 확인해 인벤토리의 아이템을 창고로 옮긴다. */
void AFablePlayerState::DepositItem(int32 ItemId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BeginWorkshopMutation())
    {
        return;
    }

    const FString Reason = GetDepositBlockReason(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FFableItem Item = *FindItem(ItemId);
    Inventory.RemoveAll(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });
    StashItems.Add(Item);
    Changed(FString::Printf(TEXT("%s 장비를 개인 창고에 보관했습니다. 성능과 내구도는 유지됩니다."),
                            *Item.GetDisplayName()));
}

/** 수령 조건을 확인해 창고 아이템을 인벤토리로 옮긴다. */
void AFablePlayerState::WithdrawItem(int32 ItemId)
{
    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!BeginWorkshopMutation())
    {
        return;
    }

    const FString Reason = GetWithdrawBlockReason(ItemId);

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Reason.IsEmpty())
    {
        SetNotice(Reason);
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const FFableItem Item = *FindStashItem(ItemId);
    StashItems.RemoveAll(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });
    Inventory.Add(Item);
    Changed(
        FString::Printf(TEXT("%s 장비를 창고에서 꺼냈습니다. 성능과 내구도는 유지됩니다."), *Item.GetDisplayName()));
}

/** 허용된 전투 이벤트에 따라 착용 장비의 내구도를 감소시킨다. */
void AFablePlayerState::WearEquipment(EFableEquipmentSlot Slot, int32 Amount)
{
    // 서버 권한과 실행 위치 조건 검사
    if (!HasAuthority() || !bProfileLoaded || bStorageBlocked || (bBackendEnabled && !IsBackendReady()) ||
        !GetWorld() || Amount != 1 || uint8(Slot) > uint8(EFableEquipmentSlot::Armor))
    {
        return;
    }

    // 현재 월드와 플레이어 참조에 필요한 값 준비
    double& NextWear = Slot == EFableEquipmentSlot::Weapon ? NextWeaponWearTime : NextArmorWearTime;
    const double Now = GetWorld()->GetTimeSeconds();

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (Now < NextWear)
    {
        return;
    }

    // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
    const int32 ItemId = Slot == EFableEquipmentSlot::Weapon ? EquippedWeaponId : EquippedArmorId;
    FFableItem* Item = Inventory.FindByPredicate(
        [ItemId](const FFableItem& Entry)
        {
            return Entry.Id == ItemId;
        });

    // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
    if (!Item || Item->Slot != Slot || Item->IsBroken() || !FableRPG::IsValidItemDurability(*Item) ||
        (Slot == EFableEquipmentSlot::Weapon && Item->HeroClass != HeroClass))
    {
        return;
    }

    const int32 Previous = Item->Durability;
    --Item->Durability;
    NextWear = Now + 10.0;
    const bool bBroken = Item->IsBroken();
    const bool bLow = Previous * 4 > Item->MaxDurability && Item->Durability * 4 <= Item->MaxDurability;
    const FString ItemName = Item->GetDisplayName();
    SaveProfile();
    ForceNetUpdate();

    // 이 단계에서 허용된 전투 이벤트에 따라 착용 장비의 내구도를 감소시킨다.
    if (bBroken)
    {
        SetNotice(
            FString::Printf(TEXT("%s 장비가 파손되어 효과가 중지되었습니다. 브람의 공방에서 수리하세요."), *ItemName));
    }
    else if (bLow)
    {
        SetNotice(FString::Printf(TEXT("%s 내구도가 25%% 이하입니다. 브람의 공방에서 수리할 수 있습니다."), *ItemName));
    }
}

/** 현재 진행·재화·장비를 저장 가능한 프로필로 복사한다. */
UFableSaveGame* AFablePlayerState::MakeProfileSnapshot()
{
    // 퀘스트와 이야기 진행에 필요한 값 준비
    UFableSaveGame* Data = NewObject<UFableSaveGame>(this);
    Data->Version = 9;
    Data->StoryChapter = StoryChapter;
    Data->StoryObjectMask = StoryObjectMask;
    Data->StorySealMask = StorySealMask;
    Data->ClassProgress = ClassProgress;
    Data->DungeonCompletions = DungeonCompletions;
    Data->MoonwellQuest = MoonwellQuest;
    Data->MoonwellCompletions = MoonwellCompletions;
    Data->HealthPotions = HealthPotions;
    Data->ManaPotions = ManaPotions;
    const float Remaining = GetPotionCooldownRemaining();
    Data->PotionCooldownEndUtc =
        Remaining > 0.0f ? FDateTime::UtcNow().ToUnixTimestamp() + FMath::CeilToInt(Remaining) : 0;
    Data->HeroClass = HeroClass;
    Data->Level = Level;
    Data->Experience = Experience;
    Data->Gold = Gold;
    Data->Ore = Ore;
    Data->Essence = Essence;
    Data->RootCores = RootCores;
    Data->MoonCrystals = MoonCrystals;
    Data->StashItems = StashItems;
    Data->Inventory = Inventory;
    Data->EquippedWeaponId = EquippedWeaponId;
    Data->EquippedArmorId = EquippedArmorId;
    Data->ForestQuest = ForestQuest;
    Data->DungeonQuest = DungeonQuest;
    Data->QuestKills = QuestKills;
    Data->NextItemId = NextItemId;

    return Data;
}

/** 검증된 프로필의 진행·재화·장비를 플레이어 상태에 적용한다. */
void AFablePlayerState::ApplyProfileSnapshot(const UFableSaveGame* Data)
{
    // 재화와 작업 비용 처리
    HeroClass = Data->HeroClass;
    Level = Data->Level;
    Experience = Data->Experience;
    Gold = Data->Gold;
    Ore = Data->Ore;
    Essence = Data->Essence;
    RootCores = Data->RootCores;
    MoonCrystals = Data->MoonCrystals;
    StashItems = Data->StashItems;
    Inventory = Data->Inventory;
    EquippedWeaponId = Data->EquippedWeaponId;
    EquippedArmorId = Data->EquippedArmorId;
    ForestQuest = Data->ForestQuest;
    DungeonQuest = Data->DungeonQuest;
    QuestKills = Data->QuestKills;
    NextItemId = Data->NextItemId;
    ClassProgress = Data->ClassProgress;
    DungeonCompletions = Data->DungeonCompletions;
    MoonwellQuest = Data->MoonwellQuest;
    MoonwellCompletions = Data->MoonwellCompletions;
    StoryChapter = Data->StoryChapter;
    StoryObjectMask = Data->StoryObjectMask;
    StorySealMask = Data->StorySealMask;
    HealthPotions = Data->HealthPotions;
    ManaPotions = Data->ManaPotions;
    const int64 Remaining =
        FMath::Clamp<int64>(Data->PotionCooldownEndUtc - FDateTime::UtcNow().ToUnixTimestamp(), 0, 5);
    NextPotionServerTime = GetWorld()->GetTimeSeconds() + static_cast<float>(Remaining);
}
