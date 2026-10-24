#pragma once

#include "CoreMinimal.h"

namespace FableCombatFeedback
{
    /** Local cosmetic audio only. Call from a received combat cue on each peer.
     * Cue: 0 hit, 1 skill, 2 shield, 3 advancement. Never applies gameplay effects.
     */

    /** 해당 전투 피드백의 소리와 표시 효과를 재생한다. */
    FABLEWOOD_API void PlayCue(UObject* WorldContext, FVector Location, int32 Cue);
}
