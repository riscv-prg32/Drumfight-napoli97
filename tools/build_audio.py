#!/usr/bin/env python3
"""Generate the Drumfight Napoli 97 drum kits: audio/*.raw and audio/audio.json.

Two kits, eight voices each, in the voice order of src/df_core.h:

  ANALOG (instruments 0..7)   PCM one-shots synthesized here with the
      circuits of the classic analogue drum machines as a model: a sine
      with a pitch sweep for the kick, tuned oscillators plus filtered
      noise for the snare, six detuned square waves through a band-pass
      for the hats, noise bursts for the clap, two squares for the
      cowbell. Nothing is recorded or sampled from an instrument or a
      machine: every byte comes from the formulas below.

  SID (instruments 8..15)     procedural instruments of the PRG32 SID-like
      synthesizer (no bytes at all). The synthesizer has no pitch envelope
      and its filter stops at about 1.3 kHz, so this kit is a chiptune
      caricature of a drum kit; it is kept as a selectable alternative.

Samples are unsigned 8-bit mono. Bright voices are stored at 22050 Hz, the
mixer rate; the kick is stored at 11025 Hz and played an octave down
(base_note 72, played as note 60), which halves its size.

The output is deterministic (own noise generator, no library randomness):
running this tool again must not change any committed file.

  python3 tools/build_audio.py            write audio/*.raw and audio.json
  python3 tools/build_audio.py --demo F   also render a WAV of the ANALOG
                                          kit playing a groove, to listen
                                          to the kit without a board
"""
from __future__ import annotations

import json
import math
import struct
import sys
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RATE = 22050
TAU = 2.0 * math.pi


# ---------------------------------------------------------------- helpers

class Noise:
    """Deterministic white noise in [-1, 1] (32-bit LCG)."""

    def __init__(self, seed: int):
        self.state = seed & 0xFFFFFFFF

    def __call__(self) -> float:
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.state >> 8) / 8388608.0 - 1.0


def biquad(samples, kind: str, freq: float, q: float, rate: int):
    """RBJ cookbook filter: 'lp', 'hp' or 'bp' (constant peak gain)."""
    w = TAU * freq / rate
    alpha = math.sin(w) / (2.0 * q)
    cos_w = math.cos(w)
    if kind == "lp":
        b0, b1, b2 = (1 - cos_w) / 2, 1 - cos_w, (1 - cos_w) / 2
    elif kind == "hp":
        b0, b1, b2 = (1 + cos_w) / 2, -(1 + cos_w), (1 + cos_w) / 2
    else:
        b0, b1, b2 = alpha, 0.0, -alpha
    a0, a1, a2 = 1 + alpha, -2 * cos_w, 1 - alpha
    x1 = x2 = y1 = y2 = 0.0
    out = []
    for x in samples:
        y = (b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0
        x2, x1, y2, y1 = x1, x, y1, y
        out.append(y)
    return out


def swept_sine(n: int, rate: int, f_end: float, f_drop: float, tau: float):
    """Sine whose frequency falls from f_end + f_drop to f_end."""
    out, phase = [], 0.0
    for i in range(n):
        t = i / rate
        phase += TAU * (f_end + f_drop * math.exp(-t / tau)) / rate
        out.append(math.sin(phase))
    return out


def square(n: int, rate: int, freq: float):
    return [1.0 if (i * freq / rate) % 1.0 < 0.5 else -1.0 for i in range(n)]


def decay(n: int, rate: int, tau: float):
    return [math.exp(-(i / rate) / tau) for i in range(n)]


def mix(*parts):
    return [sum(values) for values in zip(*parts)]


def scale(samples, gain: float):
    return [s * gain for s in samples]


def shape(samples, envelope):
    return [s * e for s, e in zip(samples, envelope)]


def metal(n: int, rate: int):
    """The six detuned square oscillators of an analogue cymbal circuit."""
    voices = [square(n, rate, f) for f in (205.3, 304.4, 369.6, 522.7, 540.0, 800.0)]
    return [sum(v) / 6.0 for v in zip(*voices)]


def finish(samples, fade_ms: float, rate: int) -> bytes:
    """Fade the tail to silence, normalise, convert to unsigned 8-bit."""
    n = len(samples)
    fade = max(1, int(rate * fade_ms / 1000.0))
    samples = [s * min(1.0, (n - 1 - i) / fade) for i, s in enumerate(samples)]
    peak = max(abs(s) for s in samples) or 1.0
    return bytes(max(0, min(255, int(round(128 + 127 * 0.98 * s / peak)))) for s in samples)


# ------------------------------------------------------------------ voices

def kick() -> tuple[bytes, int]:
    rate = RATE // 2
    n = int(rate * 0.26)
    body = shape(swept_sine(n, rate, 52.0, 130.0, 0.030), decay(n, rate, 0.11))
    noise = Noise(1)
    click = shape([noise() for _ in range(n)], decay(n, rate, 0.002))
    drum = [math.tanh(1.7 * (b + 0.35 * c)) for b, c in zip(body, click)]
    return finish(drum, 12, rate), rate


def snare() -> tuple[bytes, int]:
    n = int(RATE * 0.17)
    low = shape(swept_sine(n, RATE, 180.0, 90.0, 0.020), decay(n, RATE, 0.050))
    high = shape(swept_sine(n, RATE, 330.0, 60.0, 0.015), decay(n, RATE, 0.030))
    noise = Noise(2)
    wires = biquad([noise() for _ in range(n)], "hp", 1800.0, 0.7, RATE)
    wires = shape(wires, decay(n, RATE, 0.065))
    return finish(mix(scale(low, 0.60), scale(high, 0.35), scale(wires, 0.85)), 8, RATE), RATE


def hat(seconds: float, tau: float, seed: int) -> tuple[bytes, int]:
    n = int(RATE * seconds)
    noise = Noise(seed)
    source = [m + 0.25 * noise() for m in metal(n, RATE)]
    tone = biquad(biquad(source, "bp", 7000.0, 1.5, RATE), "hp", 5000.0, 0.7, RATE)
    return finish(shape(tone, decay(n, RATE, tau)), 4, RATE), RATE


def tammorra() -> tuple[bytes, int]:
    """Frame drum: a low skin, a hand slap and the jingles in the frame."""
    n = int(RATE * 0.16)
    skin = shape(swept_sine(n, RATE, 95.0, 60.0, 0.020), decay(n, RATE, 0.070))
    noise = Noise(4)
    slap = shape(biquad([noise() for _ in range(n)], "lp", 2000.0, 0.7, RATE), decay(n, RATE, 0.010))
    late = int(RATE * 0.008)
    jingles = biquad(metal(n, RATE), "bp", 6000.0, 2.0, RATE)
    jingles = [0.0] * late + shape(jingles, decay(n, RATE, 0.045))[: n - late]
    return finish(mix(skin, scale(slap, 0.30), scale(jingles, 0.9)), 8, RATE), RATE


def bongo() -> tuple[bytes, int]:
    n = int(RATE * 0.09)
    head = shape(swept_sine(n, RATE, 400.0, 250.0, 0.006), decay(n, RATE, 0.030))
    ring = shape(swept_sine(n, RATE, 640.0, 0.0, 1.0), decay(n, RATE, 0.015))
    noise = Noise(5)
    tap = shape([noise() for _ in range(n)], decay(n, RATE, 0.0015))
    return finish(mix(head, scale(ring, 0.30), scale(tap, 0.25)), 6, RATE), RATE


def campana() -> tuple[bytes, int]:
    """Cowbell: two square oscillators through a band-pass."""
    n = int(RATE * 0.15)
    bell = [(a + b) / 2.0 for a, b in zip(square(n, RATE, 540.0), square(n, RATE, 800.0))]
    bell = biquad(bell, "bp", 1500.0, 1.0, RATE)
    envelope = [0.6 * a + 0.4 * b for a, b in zip(decay(n, RATE, 0.012), decay(n, RATE, 0.060))]
    return finish(shape(bell, envelope), 6, RATE), RATE


def clap() -> tuple[bytes, int]:
    """Three short noise bursts, then a tail: several hands, not quite together."""
    n = int(RATE * 0.16)
    noise = Noise(7)
    hands = biquad(biquad([noise() for _ in range(n)], "bp", 1100.0, 1.8, RATE), "hp", 500.0, 0.7, RATE)
    envelope = []
    for i in range(n):
        t = i / RATE
        if t < 0.027:
            envelope.append(math.exp(-(t % 0.009) / 0.004))
        else:
            envelope.append(math.exp(-(t - 0.027) / 0.045))
    return finish(shape(hands, envelope), 8, RATE), RATE


def synth(wave_id: int, cutoff: int, resonance: int, pulse_width: int = 8) -> int:
    """PRG32 SID-like instrument id (PRG32 docs/tools/audio.md)."""
    return 0x8000 | (resonance & 3) << 10 | (cutoff & 15) << 6 | (pulse_width & 15) << 2 | (wave_id & 3)


TRI, SAW, PULSE, NOISE = 0, 1, 2, 3

# name, generator, pan (-64 left .. 63 right)
ANALOG = [
    ("kick", kick, 0),
    ("snare", snare, -14),
    ("hat", lambda: hat(0.05, 0.012, 31), 30),
    ("open_hat", lambda: hat(0.20, 0.075, 32), 40),
    ("tammorra", tammorra, -40),
    ("bongo", bongo, 20),
    ("campana", campana, -28),
    ("clap", clap, 12),
]

# name, sample_id, volume, pan, attack, decay, sustain, release
SID = [
    ("sid_kick", synth(TRI, 8, 2), 255, 0, 0, 60, 0, 24),
    ("sid_snare", synth(NOISE, 15, 1), 235, -14, 0, 46, 0, 20),
    ("sid_hat", synth(NOISE, 15, 3), 190, 30, 0, 16, 0, 8),
    ("sid_open_hat", synth(NOISE, 15, 3), 180, 40, 0, 74, 0, 40),
    ("sid_tammorra", synth(TRI, 11, 2), 245, -40, 0, 64, 0, 30),
    ("sid_bongo", synth(PULSE, 12, 2, 4), 220, 20, 0, 34, 0, 16),
    ("sid_campana", synth(PULSE, 15, 3, 6), 185, -28, 0, 36, 0, 18),
    ("sid_clap", synth(NOISE, 13, 2), 225, 12, 0, 34, 0, 16),
]


def render_demo(path: Path, kit: list[tuple[bytes, int, int]]) -> None:
    """Mix the ANALOG kit like the firmware does (nearest-sample playback,
    volume applied twice, linear pan) and write a stereo 16-bit WAV: each
    voice alone, then four bars of a groove at 114 BPM."""
    gains = [100, 96, 78, 72, 92, 82, 76, 90]          # VOICE_GAIN in src/drumfight.c
    levels = {"g": 112, "x": 188, "X": 255}            # LEVEL_VOLUME
    groove = [
        "X..x..x.x..x...x", "....X..g....X.gx", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
        "X..x..xgX..x..x.", ".x.....x.x......", "X..x..x.X..x..x.", "....x.......x...",
    ]
    step = int(RATE * 0.132)
    events = [(v * step * 4, v, 255) for v in range(8)]
    start = 8 * step * 4 + step * 4
    for bar in range(4):
        for v, row in enumerate(groove):
            for s, c in enumerate(row):
                if c != ".":
                    events.append((start + (bar * 16 + s) * step, v, levels[c]))
    total = start + 4 * 16 * step + RATE
    left, right = [0.0] * total, [0.0] * total
    events.sort()
    # A new hit of a voice cuts its previous one; the closed hat chokes the open hat.
    for index, (at, v, level) in enumerate(events):
        data, rate, pan = kit[v]
        volume = level * gains[v] // 100
        gain = (volume / 256.0) ** 2 * 0.5
        gl = 1.0 if pan <= 0 else 1.0 - pan / 63.0
        gr = 1.0 if pan >= 0 else 1.0 + pan / 64.0
        end = total
        for later_at, later_v, _ in events[index + 1:]:
            if later_v == v or (v == 3 and later_v == 2):
                end = later_at
                break
        ratio = rate / RATE
        for i in range(min(end - at, int(len(data) / ratio))):
            s = (data[int(i * ratio)] - 128) / 128.0 * gain
            left[at + i] += s * gl
            right[at + i] += s * gr
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        frames = bytearray()
        for a, b in zip(left, right):
            frames += struct.pack("<hh", int(max(-1.0, min(1.0, a)) * 32767), int(max(-1.0, min(1.0, b)) * 32767))
        w.writeframes(bytes(frames))
    print(f"{path}: {total / RATE:.1f} s (eight voices alone, then four bars)")


def main() -> None:
    audio = ROOT / "audio"
    audio.mkdir(exist_ok=True)
    samples, instruments, kit, total = [], [], [], 0
    for index, (name, generate, pan) in enumerate(ANALOG):
        data, rate = generate()
        (audio / f"{name}.raw").write_bytes(data)
        total += len(data)
        kit.append((data, rate, pan))
        # A sample stored at half the mixer rate is played an octave down.
        samples.append({"name": name, "file": f"{name}.raw", "base_note": 60 if rate == RATE else 72})
        instruments.append({"name": name, "sample_id": index, "default_volume": 255, "default_pan": pan,
                            "attack": 0, "decay": 0, "sustain": 255, "release": 0})
        print(f"audio/{name}.raw: {len(data):5d} bytes, {rate} Hz, {1000 * len(data) // rate} ms")
    for name, sid, vol, pan, a, d, s, r in SID:
        instruments.append({"name": name, "sample_id": sid, "default_volume": vol, "default_pan": pan,
                            "attack": a, "decay": d, "sustain": s, "release": r})
    (audio / "audio.json").write_text(
        json.dumps({"samples": samples, "instruments": instruments, "tracks": []}, indent=2) + "\n",
        encoding="utf-8")
    print(f"audio/audio.json: {len(samples)} samples ({total} bytes), {len(instruments)} instruments, 0 tracks")
    if "--demo" in sys.argv:
        render_demo(Path(sys.argv[sys.argv.index("--demo") + 1]), kit)


if __name__ == "__main__":
    main()
