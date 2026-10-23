#include "FableCombatFeedback.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

namespace FableCombatFeedback
{
    void PlayCue(UObject* WorldContext, FVector Location, int32 Cue)
    {
        if (!WorldContext || !GEngine || !GEngine->UseSound() || Cue < 0 || Cue > 3 || Location.ContainsNaN()) return;
        UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
        if (!World || World->GetNetMode() == NM_DedicatedServer || !World->bAllowAudioPlayback) return;

        struct FWorldCueTimes
        {
            double LastCue[4] = { -100.0, -100.0, -100.0, -100.0 };
        };
        static TMap<TWeakObjectPtr<UWorld>, FWorldCueTimes> CueTimes;
        for (auto It = CueTimes.CreateIterator(); It; ++It)
            if (!It.Key().IsValid()) It.RemoveCurrent();
        FWorldCueTimes& Times = CueTimes.FindOrAdd(TWeakObjectPtr<UWorld>(World));
        const double Now = World->GetTimeSeconds();
        const double MinimumGap = Cue == 0 ? 0.06 : (Cue == 3 ? 0.45 : 0.10);
        if (Now - Times.LastCue[Cue] < MinimumGap) return;
        Times.LastCue[Cue] = Now;

        // The always-cooked Art directory contains our generated mono PCM sounds.
        // Weak caches avoid pinning audio across editor worlds and reload on demand.
        static TWeakObjectPtr<USoundBase> Sounds[4];
        static const TCHAR* Paths[4] =
        {
            TEXT("/Game/Art/Audio/SW_FableHit.SW_FableHit"),
            TEXT("/Game/Art/Audio/SW_FableSkill.SW_FableSkill"),
            TEXT("/Game/Art/Audio/SW_FableShield.SW_FableShield"),
            TEXT("/Game/Art/Audio/SW_FableAdvance.SW_FableAdvance")
        };
        USoundBase* Sound = Sounds[Cue].Get();
        if (!Sound)
        {
            Sound = LoadObject<USoundBase>(nullptr, Paths[Cue]);
            Sounds[Cue] = Sound;
        }
        if (Sound)
            UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location,
                Cue == 0 ? 0.42f : 0.58f, 1.0f);
    }
}
