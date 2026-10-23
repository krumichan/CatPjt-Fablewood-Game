"""Create original short combat sounds using only Python's standard library.

No sampled/downloaded audio is used. Re-running produces identical mono PCM WAVs.
"""
from pathlib import Path
import hashlib
import json
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ArtSource/Audio"
RATE = 44100


def generate(name, duration, kind):
    rng = random.Random(7919 + kind)
    samples = []
    smooth_noise = 0.0
    for i in range(round(duration * RATE)):
        t = i / RATE
        smooth_noise = smooth_noise * 0.74 + rng.uniform(-1, 1) * 0.26
        if kind == 0:
            value = math.exp(-t * 29) * (0.62 * math.sin(2 * math.pi * (155 * t - 85 * t * t))
                    + 0.43 * smooth_noise + 0.14 * math.sin(2 * math.pi * 835 * t))
        elif kind == 1:
            value = math.exp(-t * 9) * (0.36 * math.sin(2 * math.pi * (580 * t + 1050 * t * t))
                    + 0.26 * math.sin(2 * math.pi * 870 * t) + 0.18 * smooth_noise)
        elif kind == 2:
            value = math.exp(-t * 8) * sum(weight * math.sin(2 * math.pi * frequency * t)
                    for frequency, weight in ((523.25, 0.42), (783.99, 0.25), (1046.5, 0.13)))
        else:
            value = 0.0
            for onset, frequency in ((0.0, 523.25), (0.10, 659.25), (0.20, 783.99), (0.30, 1046.5)):
                age = t - onset
                if age >= 0:
                    value += min(1, age / 0.008) * math.exp(-age * 10) * 0.3 * (
                        math.sin(2 * math.pi * frequency * age)
                        + 0.16 * math.sin(2 * math.pi * 2 * frequency * age))
        envelope = min(1.0, t / 0.004) * min(1.0, max(0, duration - t) / 0.025)
        samples.append(value * envelope)
    peak = max(abs(v) for v in samples)
    pcm = b''.join(struct.pack('<h', round(v / peak * 0.5 * 32767)) for v in samples)
    path = OUT / (name + '.wav')
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm)
    return {'name': name, 'duration_seconds': len(samples) / RATE, 'sample_rate': RATE,
            'channels': 1, 'bits': 16, 'peak_dbfs': -6.02, 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sounds = [generate(name, duration, index) for index, (name, duration) in enumerate((
        ('SW_FableHit', 0.16), ('SW_FableSkill', 0.30),
        ('SW_FableShield', 0.38), ('SW_FableAdvance', 0.76)))]
    manifest = {'method': 'Original deterministic synthesis; no external recordings or downloads',
                'sounds': sounds}
    (OUT / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
