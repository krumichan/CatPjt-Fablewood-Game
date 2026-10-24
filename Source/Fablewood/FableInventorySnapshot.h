#pragma once
#include "CoreMinimal.h"

class UFableSaveGame;
class FJsonObject;
struct FFableItem;

/** Typed central-store mirror. All gameplay mutations remain server-authoritative in PlayerState. */
namespace FableInventorySnapshot
{
    // Observation serializer also used by the opt-in remote-client validation driver.

    /** 아이템의 소유·품질·옵션·내구도를 전송용 JSON으로 만든다. */
    FABLEWOOD_API TSharedRef<FJsonObject> MakeItem(const FFableItem& Item, int32 Location, int32 Position);

    // Returns null for a historical or invalid profile. Never assigns or changes an item identity.

    /** 저장 프로필을 백엔드 계약에 맞는 인벤토리 JSON으로 만든다. */
    FABLEWOOD_API TSharedPtr<FJsonObject> Make(const UFableSaveGame* Profile);

    // Compare each field and ordered array, independently of JSON object property order.
    // Unknown, missing, null, mistyped or unequal fields fail closed.

    /** 전달된 저장 계약이 현재 프로필의 투영 결과와 같은지 검사한다. */
    FABLEWOOD_API bool Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Snapshot);
}
