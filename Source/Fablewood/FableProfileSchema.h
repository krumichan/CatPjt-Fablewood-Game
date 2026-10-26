#pragma once

#include "CoreMinimal.h"
#include "FableTownTypes.h"

class UFableSaveGame;
class FJsonObject;
class FJsonValue;

/** 새 캐릭터와 이전 형식에만 적용하는 신뢰된 서버 정책. */
enum class EFableTownBootstrapPolicy : uint8
{
    LegacyVillageV1,
    AdoptedSunbrookV1
};

/** 바이너리 저장 형식과 네트워크 투영 버전의 경계를 한 곳에서 정의한다. */
namespace FableProfileSchema
{
    constexpr int32 CurrentVersion = 10;
    constexpr int32 ItemIdentityVersion = 9;
    constexpr int32 InventoryWireVersion = 1;
    constexpr int32 SummaryWireVersion = 1;

    /** 원본의 typed mirror를 검사할 수 있는 형식만 허용한다. */
    FABLEWOOD_API bool CanProjectOriginalMirror(int32 Version);

    /** 일반 저장 대기열은 현재 형식만 기록한다. */
    FABLEWOOD_API bool IsWritableCurrentVersion(int32 Version);

    /** 서비스 포털과 안전 귀환지를 구별하여 정확한 논리 참조를 검사한다. */
    FABLEWOOD_API bool IsSafeAnchor(const FFableTownAnchorRef& Anchor);

    /** 미채택 지형을 새 기본값으로 선택하지 않는다. */
    FABLEWOOD_API FFableTownAnchorRef BootstrapAnchor(EFableTownBootstrapPolicy Policy);

    /** 원본 객체를 바꾸지 않고 검증된 별도 이전 후보를 만든다. */
    FABLEWOOD_API UFableSaveGame* MakeCandidate(
        const UFableSaveGame* Original, EFableTownBootstrapPolicy Policy = EFableTownBootstrapPolicy::LegacyVillageV1);

    /** 원본 형식의 전체 mirror를 확인한다. 정확한 V9+null 요약만 seed 대상이다. */
    FABLEWOOD_API bool OriginalMirrorsMatch(const UFableSaveGame* Original, int32 InventoryVersion,
                                            const TSharedPtr<FJsonObject>& Wallet,
                                            const TSharedPtr<FJsonObject>& Inventory,
                                            const TSharedPtr<FJsonValue>& Summary, bool& bOutRequiresSummarySeed);

    /**
     * 로드한 V9와 후보의 V9 투영을 같은 현재 serializer로 비교하여 기존 저장 필드 보존을 확인한다.
     * 실제 원본 raw bytes/hash의 검증과 보존은 호출자가 맡으며 UE header metadata 차이는 비교하지 않는다.
     */
    FABLEWOOD_API bool PreservesOriginalNineBytes(const UFableSaveGame* Candidate, const TArray<uint8>& OriginalBytes);

    /** 인증 ticket 전에 중복·비정규·구형 접속 옵션을 거절한다. */
    FABLEWOOD_API bool HasCompatibleOptions(const FString& Options);

    /** 모든 정상 접속 경로가 같은 형식 계약을 사용한다. */
    FABLEWOOD_API FString CompatibilityOptions();
}
