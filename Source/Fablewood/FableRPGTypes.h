#pragma once
#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "FableRPGTypes.generated.h"

/** 플레이어의 기본 직업 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableClass : uint8
{
    Warrior,
    Mage,
    Archer
};

/** 전직 단계에서 선택하는 특성 경로 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableTalentPath : uint8
{
    None,
    First,
    Second
};

/** 활동·행동 불능·쓰러짐으로 구분한 캐릭터 생존 상태 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableLifeState : uint8
{
    Active,
    Downed,
    Defeated
};

/** 마을·뿌리 유적·달샘의 지역 구분 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableRegion : uint8
{
    Village,
    RootRuins,
    Moonwell
};

/** 퀘스트 종류의 식별자 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableQuestId : uint8
{
    Forest,
    RootRuins,
    Moonwell,
    SecondAdvancement,
    ThirdAdvancement,
    Story
};

/** 무기와 방어구의 착용 부위 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableEquipmentSlot : uint8
{
    Weapon,
    Armor
};

/** 던전 난이도 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableDifficulty : uint8
{
    Normal,
    Hard
};

/** 장비 난수 생성에 사용하는 품질 단계 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableItemQuality : uint8
{
    Unrated,
    Low,
    Medium,
    High,
    Finest
};

/** 장비 추가 옵션의 능력치 종류 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableItemStat : uint8
{
    Attack,
    Defense,
    MaxHealth,
    ManaRegen
};

/** 장비 세트 효과의 종류 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableEquipmentSet : uint8
{
    None,
    RootGuardian,
    Moonlight
};

/** 퀘스트 수락부터 보상 완료까지의 진행 단계 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableQuestStage : uint8
{
    Available,
    Active,
    Ready,
    Completed
};

/** 모험의 지역·웨이브·보스 진행 단계 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableAdventureStage : uint8
{
    Village,
    Forest,
    ForestCleared,
    Dungeon,
    Boss,
    Cleared
};

/** 상점에서 구매할 수 있는 상품 값의 종류를 정의한다. */
UENUM(BlueprintType)
enum class EFableShopOffer : uint8
{
    HealthPotion,
    ManaPotion,
    WarriorWeapon,
    MageWeapon,
    ArcherWeapon,
    Armor
};

/** 전직 시험의 진행 단계와 서버가 확인한 적중 횟수. */
USTRUCT(BlueprintType)
struct FFableAdvancementTrial
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableQuestStage Stage = EFableQuestStage::Available;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 ValidSkillCasts = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    bool bLegacyGranted = false;
};

// Captured by trusted server code when a skill actually starts, never supplied by a client.

/** 서버에서 시작한 스킬과 전직 시험을 연결하는 시전 식별 문맥. */
struct FFableTrialCastContext
{
    EFableClass HeroClass = EFableClass::Warrior;
    int32 SkillSlot = -1;
    uint32 Generation = 0;
    uint32 CastId = 0;
};

/** 직업별 전직 단계·스킬 강화·특성 선택의 저장 상태. */
USTRUCT(BlueprintType)
struct FFableClassProgress
{
    GENERATED_BODY()

    /** 생성 시 필요한 기본 설정과 초기 상태를 구성한다. */
    FFableClassProgress()
    {
        Ranks = {1, 1, 1, 0};
    }
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Tier = 1;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    TArray<int32> Ranks;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FFableAdvancementTrial SecondTrial;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FFableAdvancementTrial ThirdTrial;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableTalentPath SecondTalent = EFableTalentPath::None;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableTalentPath ThirdTalent = EFableTalentPath::None;
};

/** 골드·광석·정수·지역 재료로 구성한 작업 비용. */
USTRUCT(BlueprintType)
struct FFableResourceCost
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    int32 Gold = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Ore = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 Essence = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 RootCores = 0;
    UPROPERTY(BlueprintReadOnly)
    int32 MoonCrystals = 0;
};

/** 장비 추가 옵션의 종류와 수치. */
USTRUCT(BlueprintType)
struct FFableItemAffix
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableItemStat Stat = EFableItemStat::Attack;
    // Attack, defense and health are whole points; mana regeneration uses tenths of MP/second.
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Value = 0;
};

/** 장비와 세트 효과를 합산한 전투 능력치. */
USTRUCT(BlueprintType)
struct FFableEquipmentStats
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly)
    float Attack = 24.0f;
    UPROPERTY(BlueprintReadOnly)
    float Defense = 0.0f;
    // Additional health above the character base of 100; matches GetMaxHealthBonus().
    UPROPERTY(BlueprintReadOnly)
    float MaxHealth = 0.0f;
    UPROPERTY(BlueprintReadOnly)
    float ManaRegen = 8.0f;
};

/** 전역 식별자·등급·품질·옵션·강화·내구도를 가진 장비 인스턴스. */
USTRUCT(BlueprintType)
struct FFableItem
{
    GENERATED_BODY()
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Id = 0;
    // Global identity is assigned only at creation or validated legacy migration. Id remains the local UI handle.
    UPROPERTY(SaveGame, BlueprintReadOnly)
    FGuid InstanceId;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableEquipmentSlot Slot = EFableEquipmentSlot::Weapon;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableClass HeroClass = EFableClass::Warrior;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Rarity = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Power = 24;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Upgrade = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 RollVersion = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableItemQuality Quality = EFableItemQuality::Unrated;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 NominalPower = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 RollPercent = 100;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    TArray<FFableItemAffix> Affixes;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    EFableEquipmentSet SetId = EFableEquipmentSet::None;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 DurabilityVersion = 0;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 Durability = 100;
    UPROPERTY(SaveGame, BlueprintReadOnly)
    int32 MaxDurability = 100;

    /** 내구도 파손 여부를 확인한다. */
    bool IsBroken() const
    {
        return Durability <= 0;
    }

    /** 강화와 내구도를 반영한 유효 성능 값을 반환한다. */
    int32 GetEffectivePower() const
    {
        return Power + Upgrade * (Slot == EFableEquipmentSlot::Weapon ? 4 : 2);
    }

    /** 아이템 표시 이름 값을 반환한다. */
    FString GetDisplayName() const;
};

namespace FableRPG
{
    /** 장비 등급의 표시 이름을 반환한다. */
    inline FString RarityName(int32 Rarity)
    {
        switch (Rarity)
        {
        case 0:
            return TEXT("일반");
        case 1:
            return TEXT("고급");
        case 2:
            return TEXT("희귀");
        case 3:
            return TEXT("영웅");
        case 4:
            return TEXT("전설");
        default:
            return TEXT("알 수 없는 등급");
        }
    }

    /** 장비 등급에 대응하는 최대 내구도를 반환한다. */
    inline int32 GradeMaxDurability(int32 Rarity)
    {
        static const int32 Values[] = {60, 80, 100, 120, 150};
        return Rarity >= 0 && Rarity <= 4 ? Values[Rarity] : 0;
    }

    /** 제작할 장비의 등급과 부위에 맞는 기준 성능을 반환한다. */
    inline int32 CraftNominalPower(int32 Rarity, EFableEquipmentSlot Slot)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Rarity < 1 || Rarity > 4 || uint8(Slot) > uint8(EFableEquipmentSlot::Armor))
        {
            return 0;
        }

        static const int32 Weapon[] = {30, 48, 58, 74};
        static const int32 Armor[] = {6, 12, 16, 22};

        return Slot == EFableEquipmentSlot::Weapon ? Weapon[Rarity - 1] : Armor[Rarity - 1];
    }

    /** 장비 내구도 유효성 여부를 확인한다. */
    inline bool IsValidItemDurability(const FFableItem& Item)
    {
        const int32 Expected = Item.DurabilityVersion == 0   ? 100
                               : Item.DurabilityVersion == 1 ? GradeMaxDurability(Item.Rarity)
                                                             : 0;
        return Expected > 0 && Item.MaxDurability == Expected && Item.Durability >= 0 && Item.Durability <= Expected;
    }

    /** 던전 난이도의 표시 이름을 반환한다. */
    inline FString DifficultyName(EFableDifficulty Difficulty)
    {
        return Difficulty == EFableDifficulty::Hard ? TEXT("어려움") : TEXT("일반");
    }

    /** 장비 품질의 표시 이름을 반환한다. */
    inline FString QualityName(EFableItemQuality Quality)
    {
        switch (Quality)
        {
        case EFableItemQuality::Low:
            return TEXT("하급");
        case EFableItemQuality::Medium:
            return TEXT("중급");
        case EFableItemQuality::High:
            return TEXT("상급");
        case EFableItemQuality::Finest:
            return TEXT("최상급");
        default:
            return TEXT("고정");
        }
    }

    /** 장비 품질에 따라 허용한 기본 성능 난수 범위를 구한다. */
    inline void QualityRollRange(EFableItemQuality Quality, int32& OutMin, int32& OutMax)
    {
        switch (Quality)
        {
        case EFableItemQuality::Low:
            OutMin = 80;
            OutMax = 94;
            break;
        case EFableItemQuality::Medium:
            OutMin = 95;
            OutMax = 109;
            break;
        case EFableItemQuality::High:
            OutMin = 110;
            OutMax = 124;
            break;
        case EFableItemQuality::Finest:
            OutMin = 125;
            OutMax = 140;
            break;
        default:
            OutMin = 100;
            OutMax = 100;
            break;
        }
    }

    /** 장비 품질에 따라 생성할 추가 옵션 수를 반환한다. */
    inline int32 QualityAffixCount(EFableItemQuality Quality)
    {
        return Quality == EFableItemQuality::Finest                                           ? 3
               : Quality == EFableItemQuality::Low                                            ? 1
               : (Quality == EFableItemQuality::Medium || Quality == EFableItemQuality::High) ? 2
                                                                                              : 0;
    }

    /** 추가 옵션 종류에 따라 허용한 수치 범위를 구한다. */
    inline void AffixValueRange(EFableItemStat Stat, int32& OutMin, int32& OutMax)
    {
        switch (Stat)
        {
        // 체력·마나·보호막·피해량 처리
        case EFableItemStat::Attack:
            OutMin = 2;
            OutMax = 6;
            break;
        case EFableItemStat::Defense:
            OutMin = 1;
            OutMax = 4;
            break;
        case EFableItemStat::MaxHealth:
            OutMin = 5;
            OutMax = 20;
            break;
        case EFableItemStat::ManaRegen:
            OutMin = 5;
            OutMax = 15;
            break;
        default:
            OutMin = 0;
            OutMax = 0;
            break;
        }
    }

    /** 추가 옵션의 종류와 수치를 사용자 안내 문구로 변환한다. */
    inline FString AffixDescription(const FFableItemAffix& Affix)
    {
        switch (Affix.Stat)
        {
        case EFableItemStat::Attack:
            return FString::Printf(TEXT("공격력 +%d"), Affix.Value);
        case EFableItemStat::Defense:
            return FString::Printf(TEXT("방어력 +%d"), Affix.Value);
        case EFableItemStat::MaxHealth:
            return FString::Printf(TEXT("최대 체력 +%d"), Affix.Value);
        case EFableItemStat::ManaRegen:
            return FString::Printf(TEXT("마나 회복 +%.1f/초"), Affix.Value / 10.0f);
        default:
            return TEXT("알 수 없는 옵션");
        }
    }

    /** 장비 세트의 표시 이름을 반환한다. */
    inline FString SetName(EFableEquipmentSet SetId)
    {
        return SetId == EFableEquipmentSet::RootGuardian ? TEXT("뿌리 수호자")
               : SetId == EFableEquipmentSet::Moonlight  ? TEXT("달빛")
                                                         : TEXT("세트 없음");
    }

    /** 장비 세트 효과의 안내 문구를 반환한다. */
    inline FString SetDescription(EFableEquipmentSet SetId)
    {
        return SetId == EFableEquipmentSet::RootGuardian ? TEXT("2부위: 공격력 +5 · 방어력 +3")
               : SetId == EFableEquipmentSet::Moonlight  ? TEXT("2부위: 공격력 +7 · 최대 체력 +15 · 마나 회복 +1.0/초")
                                                         : TEXT("세트 효과 없음");
    }

    /** 장비 품질에 따른 기본 성능 범위 값을 반환한다. */
    inline void GetItemPowerRange(const FFableItem& Item, int32& OutMin, int32& OutMax)
    {
        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (Item.RollVersion == 0)
        {
            OutMin = Item.Power;
            OutMax = Item.Power;
            return;
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        QualityRollRange(Item.Quality, OutMin, OutMax);
        OutMin = FMath::RoundToInt(Item.NominalPower * OutMin / 100.0f);
        OutMax = FMath::RoundToInt(Item.NominalPower * OutMax / 100.0f);
    }

    /** 장비의 전역 식별자와 불변 생성 정보를 서명 문자열로 묶는다. */
    inline FString GearIdentitySignature(const FFableItem& Item)
    {
        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FString Result = FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,r%d,q%d,n%d,p%d,s%d,a%d"), Item.Id, int32(Item.Slot),
                                         int32(Item.HeroClass), Item.Rarity, Item.Power, Item.Upgrade, Item.RollVersion,
                                         int32(Item.Quality), Item.NominalPower, Item.RollPercent, int32(Item.SetId),
                                         Item.Affixes.Num());

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItemAffix& Affix : Item.Affixes)
        {
            Result += FString::Printf(TEXT(",%d:%d"), int32(Affix.Stat), Affix.Value);
        }

        return Result;
    }

    /** 저장 전후 비교에 사용할 장비 속성을 순서대로 직렬화한다. */
    inline FString ItemPersistentSignature(const FFableItem& Item)
    {
        return GearIdentitySignature(Item) +
               FString::Printf(TEXT(",dv%d,d%d,md%d"), Item.DurabilityVersion, Item.Durability, Item.MaxDurability);
    }

    /** 장비 난수 속성 유효성 여부를 확인한다. */
    inline bool IsValidItemRoll(const FFableItem& Item)
    {
        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (Item.RollVersion == 0)
        {
            return Item.Quality == EFableItemQuality::Unrated && Item.NominalPower == 0 && Item.RollPercent == 100 &&
                   Item.Affixes.IsEmpty() && Item.SetId == EFableEquipmentSet::None;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const bool bLegacyRoll = Item.RollVersion == 1 && Item.Rarity >= 1 && Item.Rarity <= 3;
        const bool bLegendRoll = Item.RollVersion == 2 && Item.Rarity == 4 && Item.SetId == EFableEquipmentSet::None;

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if ((!bLegacyRoll && !bLegendRoll) || Item.Id <= 4 || Item.Quality < EFableItemQuality::Low ||
            Item.Quality > EFableItemQuality::Finest || uint8(Item.SetId) > uint8(EFableEquipmentSet::Moonlight) ||
            Item.NominalPower < 1 || Item.NominalPower > 100 || Item.Affixes.Num() != QualityAffixCount(Item.Quality))
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        int32 Min = 0, Max = 0;
        QualityRollRange(Item.Quality, Min, Max);

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (Item.RollPercent < Min || Item.RollPercent > Max ||
            Item.Power != FMath::RoundToInt(Item.NominalPower * Item.RollPercent / 100.0f))
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        const bool bWeapon = Item.Slot == EFableEquipmentSlot::Weapon;
        const bool bValidNominal =
            bLegendRoll        ? Item.NominalPower == (bWeapon ? 74 : 22)
            : Item.Rarity == 1 ? (bWeapon ? Item.NominalPower == 30 || Item.NominalPower == 32 : Item.NominalPower == 6)
            : Item.Rarity == 2 ? Item.NominalPower == (bWeapon ? 48 : 12)
                               : Item.NominalPower == (bWeapon ? 58 : 16);

        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (!bValidNominal || (Item.SetId == EFableEquipmentSet::RootGuardian && Item.Rarity != 2) ||
            (Item.SetId == EFableEquipmentSet::Moonlight && Item.Rarity != 3))
        {
            return false;
        }

        TSet<uint8> Seen;

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (const FFableItemAffix& Affix : Item.Affixes)
        {
            if (uint8(Affix.Stat) > uint8(EFableItemStat::ManaRegen) || Seen.Contains(uint8(Affix.Stat)))
            {
                return false;
            }
            AffixValueRange(Affix.Stat, Min, Max);
            if (Affix.Value < Min || Affix.Value > Max)
            {
                return false;
            }
            Seen.Add(uint8(Affix.Stat));
        }

        return true;
    }

    // All rewards use this deterministic generator; callers provide a server-owned random stream.
    // Id, slot, class, rarity and set are inputs. A failed roll leaves the input unchanged.

    /** 정해진 성능 범위와 난수로 장비 품질·기본 성능·옵션을 생성한다. */
    inline bool RollItem(FFableItem& Item, int32 NominalPower, EFableDifficulty Difficulty, FRandomStream& Random)
    {
        // 아이템·장비 옵션·인벤토리와 창고 조건 검사
        if (NominalPower < 1 || NominalPower > 100 || Item.Id <= 4 || Item.Rarity < 1 || Item.Rarity > 4 ||
            uint8(Difficulty) > uint8(EFableDifficulty::Hard) || uint8(Item.Slot) > uint8(EFableEquipmentSlot::Armor) ||
            uint8(Item.HeroClass) > uint8(EFableClass::Archer) || Item.Upgrade < 0 || Item.Upgrade > 5)
        {
            return false;
        }

        // 아이템·장비 옵션·인벤토리와 창고에 필요한 값 준비
        FFableItem Rolled = Item;
        Rolled.RollVersion = Item.Rarity == 4 ? 2 : 1;
        Rolled.NominalPower = NominalPower;
        Rolled.DurabilityVersion = 1;
        Rolled.MaxDurability = GradeMaxDurability(Item.Rarity);
        Rolled.Durability = Rolled.MaxDurability;
        const int32 Chance = Random.RandRange(0, 99);
        const bool bHard = Difficulty == EFableDifficulty::Hard;
        Rolled.Quality = Chance < (bHard ? 10 : 30)   ? EFableItemQuality::Low
                         : Chance < (bHard ? 45 : 70) ? EFableItemQuality::Medium
                         : Chance < (bHard ? 80 : 93) ? EFableItemQuality::High
                                                      : EFableItemQuality::Finest;
        int32 Min = 0, Max = 0;
        QualityRollRange(Rolled.Quality, Min, Max);
        Rolled.RollPercent = Random.RandRange(Min, Max);
        Rolled.Power = FMath::RoundToInt(NominalPower * Rolled.RollPercent / 100.0f);
        Rolled.Affixes.Reset();
        TArray<EFableItemStat> Available = {EFableItemStat::Attack, EFableItemStat::Defense, EFableItemStat::MaxHealth,
                                            EFableItemStat::ManaRegen};
        const int32 Count = QualityAffixCount(Rolled.Quality);

        // 아이템·장비 옵션·인벤토리와 창고 순회
        for (int32 Index = 0; Index < Count; ++Index)
        {
            const int32 Chosen = Random.RandRange(0, Available.Num() - 1);
            FFableItemAffix Affix;
            Affix.Stat = Available[Chosen];
            Available.RemoveAt(Chosen);
            AffixValueRange(Affix.Stat, Min, Max);
            Affix.Value = Random.RandRange(Min, Max);
            Rolled.Affixes.Add(Affix);
        }

        // 아이템·장비 옵션·인벤토리와 창고 처리
        Rolled.Affixes.Sort(
            [](const FFableItemAffix& A, const FFableItemAffix& B)
            {
                return uint8(A.Stat) < uint8(B.Stat);
            });

        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (!IsValidItemRoll(Rolled))
        {
            return false;
        }

        Item = MoveTemp(Rolled);

        return true;
    }

    /** 지역 판정과 전투 배치에 사용할 지역 중심 좌표를 반환한다. */
    inline FVector RegionCenter(EFableRegion Region)
    {
        return Region == EFableRegion::Moonwell    ? FVector(18000, 0, 0)
               : Region == EFableRegion::RootRuins ? FVector(8000, 0, 0)
                                                   : FVector::ZeroVector;
    }

    /** 선택한 지역으로 이동할 때 사용할 입장 좌표를 반환한다. */
    inline FVector RegionEntrance(EFableRegion Region)
    {
        return Region == EFableRegion::Moonwell    ? FVector(18000, -1900, 100)
               : Region == EFableRegion::RootRuins ? FVector(8000, -1000, 100)
                                                   : FVector(0, -1050, 110);
    }

    /** 선택한 지역에서 마을로 돌아가는 이동문 좌표를 반환한다. */
    inline FVector RegionReturnPortal(EFableRegion Region)
    {
        return Region == EFableRegion::Moonwell    ? FVector(17550, -2200, 0)
               : Region == EFableRegion::RootRuins ? FVector(7550, -1330, 0)
                                                   : FVector(1100, -700, 0);
    }

    /** 지역 내부 위치 여부를 확인한다. */
    inline bool IsInRegion(EFableRegion Region, const FVector& Location)
    {
        // 지역·적·전투 진행 조건 검사
        if (Location.ContainsNaN() || FMath::Abs(Location.Z) >= 1500.0f)
        {
            return false;
        }
        if (Region == EFableRegion::Village)
        {
            return Location.X < 4500.0f;
        }
        if (Region != EFableRegion::RootRuins && Region != EFableRegion::Moonwell)
        {
            return false;
        }

        return FVector::DistSquared2D(Location, RegionCenter(Region)) <
               FMath::Square(Region == EFableRegion::Moonwell ? 3400.0f : 2600.0f);
    }

    /** 번호에 대응하는 달샘 봉인의 월드 좌표를 반환한다. */
    inline FVector MoonSealLocation(int32 Index)
    {
        static const FVector Locations[] = {FVector(16800, -500, 0), FVector(19200, -500, 0), FVector(18000, 1100, 0)};
        return Locations[FMath::Clamp(Index, 0, 2)];
    }

    /** 이야기 오브젝트의 월드 좌표를 반환한다. */
    inline FVector StoryObjectLocation(int32 Index)
    {
        static const FVector Locations[] = {FVector(-500, -350, 0), FVector(450, -150, 0), FVector(-550, 350, 0),
                                            FVector(850, 1000, 0), FVector(8000, -500, 0)};
        return Index >= 0 && Index < 5 ? Locations[Index] : FVector::ZeroVector;
    }

    /** 이야기 오브젝트의 표시 이름을 반환한다. */
    inline FString StoryObjectName(int32 Index)
    {
        return Index >= 0 && Index < 3 ? FString::Printf(TEXT("등불꽃 %d"), Index + 1)
               : Index == 3            ? TEXT("빛을 잃은 나무뿌리")
               : Index == 4            ? TEXT("뿌리 유적의 문양돌")
                                       : TEXT("알 수 없는 조사물");
    }

    /** 이야기 오브젝트가 속하는 지역을 반환한다. */
    inline EFableRegion StoryObjectRegion(int32 Index)
    {
        return Index == 4 ? EFableRegion::RootRuins : EFableRegion::Village;
    }

    /** 이야기 진행 유효성 여부를 확인한다. */
    inline bool IsValidStoryProgress(int32 Chapter, int32 ObjectMask, int32 SealMask, EFableQuestStage ForestQuest,
                                     int32 DungeonCompletions, int32 MoonwellCompletions)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Chapter < 0 || Chapter > 5 || ObjectMask < 0 || (ObjectMask & ~31) != 0 || SealMask < 0 ||
            (SealMask & ~7) != 0)
        {
            return false;
        }
        if (Chapter == 0)
        {
            return ObjectMask == 0 && SealMask == 0;
        }
        if (Chapter == 1)
        {
            return (ObjectMask & 16) == 0 && SealMask == 0;
        }
        if ((ObjectMask & 15) != 15 || (Chapter < 4 && SealMask != 0))
        {
            return false;
        }
        if (Chapter == 2)
        {
            return (ObjectMask & 16) == 0;
        }
        if (ForestQuest != EFableQuestStage::Completed)
        {
            return false;
        }
        if (Chapter == 3)
        {
            return true;
        }
        if (ObjectMask != 31 || DungeonCompletions < 1)
        {
            return false;
        }

        return Chapter == 4 || (SealMask == 7 && MoonwellCompletions >= 1);
    }

    /** 지역의 표시 이름을 반환한다. */
    inline FString RegionName(EFableRegion Region)
    {
        return Region == EFableRegion::Moonwell    ? TEXT("달빛 수원")
               : Region == EFableRegion::RootRuins ? TEXT("뿌리 유적")
                                                   : TEXT("숲 마을");
    }

    /** 마을 퀘스트 게시판의 월드 좌표를 반환한다. */
    inline FVector BoardLocation()
    {
        return FVector(0, -750, 0);
    }

    /** 마을 공방의 월드 좌표를 반환한다. */
    inline FVector ForgeLocation()
    {
        return FVector(-650, -750, 0);
    }

    /** 마을 상인의 월드 좌표를 반환한다. */
    inline FVector MerchantLocation()
    {
        return FVector(650, -1350, 0);
    }

    /** 마을의 던전 이동문 좌표를 반환한다. */
    inline FVector PortalLocation()
    {
        return FVector(1100, -700, 0);
    }

    /** 뿌리 유적 던전의 입장 좌표를 반환한다. */
    inline FVector DungeonEntrance()
    {
        return FVector(8000, -1000, 100);
    }

    /** 기본 직업의 표시 이름을 반환한다. */
    inline FString ClassName(EFableClass Value)
    {
        switch (Value)
        {
        case EFableClass::Mage:
            return TEXT("마법사");
        case EFableClass::Archer:
            return TEXT("궁수");
        default:
            return TEXT("전사");
        }
    }

    /** 직업과 전직 단계에 맞는 표시 이름을 반환한다. */
    inline FString AdvancedClassName(EFableClass Value, int32 Tier)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Value == EFableClass::Mage)
        {
            return Tier >= 3 ? TEXT("대마법사") : Tier == 2 ? TEXT("원소술사") : TEXT("마법사");
        }
        if (Value == EFableClass::Archer)
        {
            return Tier >= 3 ? TEXT("별빛 명궁") : Tier == 2 ? TEXT("숲 정찰자") : TEXT("궁수");
        }

        return Tier >= 3 ? TEXT("숲 수호기사") : Tier == 2 ? TEXT("검기사") : TEXT("전사");
    }

    /** 직업과 스킬 슬롯에 맞는 표시 이름을 반환한다. */
    inline FString SkillName(EFableClass Value, int32 Slot)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Slot < 0 || Slot > 3)
        {
            return TEXT("알 수 없는 스킬");
        }

        static const TCHAR* Warrior[] = {TEXT("회전베기"), TEXT("돌진베기"), TEXT("수호의 맹세"), TEXT("강인함")};
        static const TCHAR* Mage[] = {TEXT("화염폭발"), TEXT("서리파동"), TEXT("별빛 보호막"), TEXT("마력순환")};
        static const TCHAR* Archer[] = {TEXT("다중사격"), TEXT("관통사격"), TEXT("후퇴사격"), TEXT("약점간파")};

        return Value == EFableClass::Mage ? Mage[Slot] : Value == EFableClass::Archer ? Archer[Slot] : Warrior[Slot];
    }

    /** 선택한 스킬의 효과와 사용 조건을 안내 문구로 반환한다. */
    inline FString SkillDescription(EFableClass Value, int32 Slot)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Slot < 0 || Slot > 3)
        {
            return TEXT("사용할 수 없는 스킬입니다.");
        }

        static const TCHAR* Warrior[] = {
            TEXT("주변의 적을 함께 벱니다. 1랭크를 넘는 강화마다 피해가 15% 증가합니다."),
            TEXT("앞으로 돌진하며 적을 벱니다. 벽을 통과하지 않으며 강화마다 피해가 15% 증가합니다."),
            TEXT("6초 동안 피해 45를 흡수하는 보호막을 얻습니다. 강화마다 보호량이 15 증가합니다."),
            TEXT("항상 적용됩니다. 랭크마다 방어력이 3 증가합니다.")};
        static const TCHAR* Mage[] = {
            TEXT("불꽃 투사체가 폭발해 주변 적을 공격합니다. 강화마다 피해가 15% 증가합니다."),
            TEXT("주변에 냉기를 퍼뜨려 공격하고 적을 느리게 합니다. 강화마다 피해가 15% 증가합니다."),
            TEXT("6초 동안 피해 60을 흡수하는 별빛 보호막을 얻습니다. 강화마다 보호량이 20 증가합니다."),
            TEXT("항상 적용됩니다. 랭크마다 초당 마나 회복량이 1.5 증가합니다.")};
        static const TCHAR* Archer[] = {TEXT("세 발의 화살을 부채꼴로 쏩니다. 강화마다 피해가 15% 증가합니다."),
                                        TEXT("적을 관통하는 화살을 쏩니다. 강화마다 피해가 15% 증가합니다."),
                                        TEXT("뒤로 물러나며 적을 향해 화살을 쏩니다. 강화마다 피해가 15% 증가합니다."),
                                        TEXT("항상 적용됩니다. 랭크마다 공격력이 2 증가합니다.")};

        return Value == EFableClass::Mage ? Mage[Slot] : Value == EFableClass::Archer ? Archer[Slot] : Warrior[Slot];
    }

    // Shared by the authoritative loader and the continuation screen.

    /** 직업 진행 유효성 여부를 확인한다. */
    inline bool IsValidClassProgress(const TArray<FFableClassProgress>& Progress, int32 Level,
                                     EFableQuestStage ForestQuest, int32 DungeonCompletions)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Progress.Num() != 3 || Level < 1 || Level > 20 || DungeonCompletions < 0 || DungeonCompletions > 1000000)
        {
            return false;
        }

        int32 Spent = 0;

        // 직업·스킬·특성 빌드 순회
        for (const FFableClassProgress& Entry : Progress)
        {
            if (Entry.Tier < 1 || Entry.Tier > 3 || Entry.Ranks.Num() != 4)
            {
                return false;
            }
            if (Entry.Tier >= 2 && (Level < 2 || ForestQuest != EFableQuestStage::Completed))
            {
                return false;
            }
            if (Entry.Tier >= 3 && (Level < 4 || DungeonCompletions < 1))
            {
                return false;
            }
            for (int32 Slot = 0; Slot < 4; ++Slot)
            {
                const int32 Rank = Entry.Ranks[Slot];
                if (Rank < (Slot == 3 ? 0 : 1) || Rank > 3)
                {
                    return false;
                }
                if ((Slot == 1 && Entry.Tier < 2) || (Slot == 2 && Entry.Tier < 3))
                {
                    if (Rank != 1)
                    {
                        return false;
                    }
                }
                Spent += Slot == 3 ? Rank : Rank - 1;
            }
        }

        return Spent <= 1 + 2 * (Level - 1);
    }

    /** 현재 직업의 전직 단계와 선택 경로에 맞는 특성 단계를 반환한다. */
    inline int32 TalentRank(const FFableClassProgress& Entry, EFableTalentPath Path)
    {
        if (Path != EFableTalentPath::First && Path != EFableTalentPath::Second)
        {
            return 0;
        }
        return (Entry.Tier >= 2 && Entry.SecondTalent == Path ? 1 : 0) +
               (Entry.Tier >= 3 && Entry.SecondTalent != EFableTalentPath::None && Entry.ThirdTalent == Path ? 1 : 0);
    }

    /** 특성 진행 유효성 여부를 확인한다. */
    inline bool IsValidTalentProgress(const TArray<FFableClassProgress>& Progress)
    {
        // 요청 인자와 현재 진행 상태의 선행 조건을 확인한다.
        if (Progress.Num() != 3)
        {
            return false;
        }

        // 직업·스킬·특성 빌드 순회
        for (const FFableClassProgress& Entry : Progress)
        {
            if (uint8(Entry.SecondTalent) > uint8(EFableTalentPath::Second) ||
                uint8(Entry.ThirdTalent) > uint8(EFableTalentPath::Second) ||
                (Entry.Tier < 2 && Entry.SecondTalent != EFableTalentPath::None) ||
                (Entry.Tier < 3 && Entry.ThirdTalent != EFableTalentPath::None) ||
                (Entry.ThirdTalent != EFableTalentPath::None && Entry.SecondTalent == EFableTalentPath::None))
            {
                return false;
            }
        }

        return true;
    }

    /** 직업·전직 단계·선택 경로에 맞는 특성 이름을 반환한다. */
    inline FString TalentName(EFableClass HeroClass, EFableTalentPath Path)
    {
        // 직업·스킬·특성 빌드 조건 검사
        if (Path == EFableTalentPath::None)
        {
            return TEXT("선택 안 함");
        }
        if (Path != EFableTalentPath::First && Path != EFableTalentPath::Second)
        {
            return TEXT("알 수 없는 특성");
        }
        if (HeroClass == EFableClass::Mage)
        {
            return Path == EFableTalentPath::First ? TEXT("불씨의 계승자") : TEXT("서리의 현자");
        }
        if (HeroClass == EFableClass::Archer)
        {
            return Path == EFableTalentPath::First ? TEXT("집중 사격") : TEXT("바람의 발걸음");
        }

        return Path == EFableTalentPath::First ? TEXT("폭풍의 선봉") : TEXT("수호의 맹세");
    }

    /** 선택한 전직 특성의 효과를 안내 문구로 반환한다. */
    inline FString TalentDescription(EFableClass HeroClass, EFableTalentPath Path, int32 Rank)
    {
        // 직업·스킬·특성 빌드 조건 검사
        if (Path != EFableTalentPath::First && Path != EFableTalentPath::Second)
        {
            return TEXT("전직 후 원하는 특성을 선택하세요.");
        }

        const int32 R = FMath::Clamp(Rank, 1, 2);

        // 직업·스킬·특성 빌드 처리
        if (HeroClass == EFableClass::Mage)
        {
            return Path == EFableTalentPath::First
                       ? FString::Printf(
                             TEXT(
                                 "공격력 +%d. 화염폭발 피해 +%d%% · 폭발 %dcm. 생존한 적에게 시전 공격력의 %d%% 화상을 1초마다 3회. 화상은 중첩되지 않습니다."),
                             3 * R, 10 * R, 300 + 30 * R, 10 * R)
                       : FString::Printf(
                             TEXT(
                                 "마나 회복 +%.1f/초. 서리파동: 이동속도 %.0f%% · %.1f초(보스 80%% · 2초). 별빛 보호막 +%d · %d초."),
                             1.5f * R, 65.f - 5.f * R, 4.f + .5f * R, 15 * R, 6 + R);
        }

        // 직업·스킬·특성 빌드 처리
        if (HeroClass == EFableClass::Archer)
        {
            return Path == EFableTalentPath::First
                       ? FString::Printf(
                             TEXT("공격력 +%d. 다중사격 부채각 %d도, 관통사격 피해 +%d%%. 모든 화살 사거리 %dcm."),
                             3 * R, 11 - 3 * R, 15 * R, 2200 + 200 * R)
                       : FString::Printf(TEXT("이동속도 +%d%% · 회피 대기 %.2f초. 후퇴사격 속도 %dcm/초 · 대기 %d초."),
                                         5 * R, 1.35f - .15f * R, 720 + 90 * R, 10 - R);
        }

        return Path == EFableTalentPath::First
                   ? FString::Printf(
                         TEXT("공격력 +%d. 회전베기 피해 +%d%%. 돌진베기 피해 +%d%% · 범위 %dcm · 대기 %d초."), 3 * R,
                         10 * R, 15 * R, 550 + 50 * R, 8 - R)
                   : FString::Printf(
                         TEXT(
                             "방어력 +%d. 회전베기 범위 %dcm. 맹세 보호량 +%d · %d초. 600cm 안의 동료에게 보호막 %d를 나눕니다."),
                         4 * R, 440 + 40 * R, 20 * R, 6 + R, 20 * R);
    }

    /** 퀘스트 진행 단계의 표시 이름을 반환한다. */
    inline FString QuestStageName(EFableQuestStage Value)
    {
        switch (Value)
        {
        case EFableQuestStage::Active:
            return TEXT("진행 중");
        case EFableQuestStage::Ready:
            return TEXT("보상 받기");
        case EFableQuestStage::Completed:
            return TEXT("완료");
        default:
            return TEXT("수락 가능");
        }
    }

    /** 모험 진행 단계의 표시 이름을 반환한다. */
    inline FString StageName(EFableAdventureStage Value)
    {
        switch (Value)
        {
        // 지역·적·전투 진행 처리
        case EFableAdventureStage::Forest:
            return TEXT("숲 순찰");
        case EFableAdventureStage::ForestCleared:
            return TEXT("숲 정화 완료");
        case EFableAdventureStage::Dungeon:
            return TEXT("뿌리 유적 던전");
        case EFableAdventureStage::Boss:
            return TEXT("고대 수호자");
        case EFableAdventureStage::Cleared:
            return TEXT("던전 완료");
        default:
            return TEXT("숲 마을");
        }
    }

    /** 상점 상품의 표시 이름을 반환한다. */
    inline FString ShopName(EFableShopOffer Offer)
    {
        switch (Offer)
        {
        // 체력·마나·보호막·피해량 처리
        case EFableShopOffer::HealthPotion:
            return TEXT("체력 물약");
        case EFableShopOffer::ManaPotion:
            return TEXT("마나 물약");
        case EFableShopOffer::WarriorWeapon:
            return TEXT("숲의 검");
        case EFableShopOffer::MageWeapon:
            return TEXT("숲의 지팡이");
        case EFableShopOffer::ArcherWeapon:
            return TEXT("숲의 활");
        case EFableShopOffer::Armor:
            return TEXT("숲의 갑옷");
        default:
            return TEXT("알 수 없는 상품");
        }
    }

    /** 상점 상품에 정해진 판매 가격을 반환한다. */
    inline int32 ShopPrice(EFableShopOffer Offer)
    {
        switch (Offer)
        {
        // 체력·마나·보호막·피해량 처리
        case EFableShopOffer::HealthPotion:
            return 15;
        case EFableShopOffer::ManaPotion:
            return 12;
        case EFableShopOffer::WarriorWeapon:
        case EFableShopOffer::MageWeapon:
        case EFableShopOffer::ArcherWeapon:
            return 100;
        case EFableShopOffer::Armor:
            return 80;
        default:
            return 0;
        }
    }
}

/** 아이템 표시 이름 값을 반환한다. */
inline FString FFableItem::GetDisplayName() const
{
    const TCHAR* Tier = Rarity >= 4   ? TEXT("장인의 전설")
                        : Rarity >= 3 ? TEXT("달빛의")
                        : Rarity >= 2 ? TEXT("수호자의")
                        : Rarity == 1 ? TEXT("숲의")
                                      : TEXT("여행자의");
    const TCHAR* Kind = Slot == EFableEquipmentSlot::Armor ? TEXT("갑옷")
                        : HeroClass == EFableClass::Mage   ? TEXT("지팡이")
                        : HeroClass == EFableClass::Archer ? TEXT("활")
                                                           : TEXT("검");
    const FString Prefix = SetId == EFableEquipmentSet::None ? FString(Tier) : FableRPG::SetName(SetId);

    return RollVersion == 0
               ? FString::Printf(TEXT("%s %s +%d"), *Prefix, Kind, Upgrade)
               : FString::Printf(TEXT("[%s] %s %s +%d"), *FableRPG::QualityName(Quality), *Prefix, Kind, Upgrade);
}
