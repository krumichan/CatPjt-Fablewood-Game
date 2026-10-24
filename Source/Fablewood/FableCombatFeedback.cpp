#include "FableCombatFeedback.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

namespace FableCombatFeedback
{
    /** 해당 전투 피드백의 소리와 표시 효과를 재생한다. */
    void PlayCue(UObject* WorldContext, FVector Location, int32 Cue)
    {
        // 현재 월드와 플레이어 참조 조건 검사
        if (!WorldContext || !GEngine || !GEngine->UseSound() || Cue < 0 || Cue > 3 || Location.ContainsNaN())
        {
            return;
        }

        UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);

        // 서버 권한과 실행 위치 조건 검사
        if (!World || World->GetNetMode() == NM_DedicatedServer || !World->bAllowAudioPlayback)
        {
            return;
        }

        /** 같은 월드에서 전투 피드백의 중복 재생을 조절하는 시각 기록. */
        // 현재 월드와 플레이어 참조 처리
        struct FWorldCueTimes
        {
            double LastCue[4] = {-100.0, -100.0, -100.0, -100.0};
        };
        static TMap<TWeakObjectPtr<UWorld>, FWorldCueTimes> CueTimes;

        // 검증 입력과 키 유지 상태 순회
        for (auto It = CueTimes.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid())
            {
                It.RemoveCurrent();
            }
        }

        // 현재 월드와 플레이어 참조에 필요한 값 준비
        FWorldCueTimes& Times = CueTimes.FindOrAdd(TWeakObjectPtr<UWorld>(World));
        const double Now = World->GetTimeSeconds();
        const double MinimumGap = Cue == 0 ? 0.06 : (Cue == 3 ? 0.45 : 0.10);

        // 서버 시각과 재사용 대기시간 조건 검사
        if (Now - Times.LastCue[Cue] < MinimumGap)
        {
            return;
        }

        Times.LastCue[Cue] = Now;

        // The always-cooked Art directory contains our generated mono PCM sounds.
        // Weak caches avoid pinning audio across editor worlds and reload on demand.

        // 직업·스킬·특성 빌드에 필요한 값 준비
        static TWeakObjectPtr<USoundBase> Sounds[4];
        static const TCHAR* Paths[4] = {TEXT("/Game/Art/Audio/SW_FableHit.SW_FableHit"),
                                        TEXT("/Game/Art/Audio/SW_FableSkill.SW_FableSkill"),
                                        TEXT("/Game/Art/Audio/SW_FableShield.SW_FableShield"),
                                        TEXT("/Game/Art/Audio/SW_FableAdvance.SW_FableAdvance")};
        USoundBase* Sound = Sounds[Cue].Get();

        // 이 단계에서 해당 전투 피드백의 소리와 표시 효과를 재생한다.
        if (!Sound)
        {
            Sound = LoadObject<USoundBase>(nullptr, Paths[Cue]);
            Sounds[Cue] = Sound;
        }

        // 현재 월드와 플레이어 참조 처리
        if (Sound)
        {
            UGameplayStatics::PlaySoundAtLocation(WorldContext, Sound, Location, Cue == 0 ? 0.42f : 0.58f, 1.0f);
        }
    }
}
