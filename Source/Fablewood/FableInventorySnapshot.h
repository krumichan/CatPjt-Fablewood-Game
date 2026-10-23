#pragma once
#include "CoreMinimal.h"

class UFableSaveGame;
class FJsonObject;
struct FFableItem;

/** Typed central-store mirror. All gameplay mutations remain server-authoritative in PlayerState. */
namespace FableInventorySnapshot
{
    // Observation serializer also used by the opt-in remote-client validation driver.
    FABLEWOOD_API TSharedRef<FJsonObject> MakeItem(const FFableItem& Item, int32 Location, int32 Position);
    // Returns null for a historical or invalid profile. Never assigns or changes an item identity.
    FABLEWOOD_API TSharedPtr<FJsonObject> Make(const UFableSaveGame* Profile);
    // Compare each field and ordered array, independently of JSON object property order.
    // Unknown, missing, null, mistyped or unequal fields fail closed.
    FABLEWOOD_API bool Matches(const UFableSaveGame* Profile, const TSharedPtr<FJsonObject>& Snapshot);
}
