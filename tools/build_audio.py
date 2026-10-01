#!/usr/bin/env python3
"""Generate audio/audio.json: the Drumfight Napoli 97 drum kit.

The kit is eight procedural instruments for the PRG32 SID-like synthesizer,
one per synth channel, so the whole kit can sound at once on the eight
voices. There are no PCM samples and no tracker tracks: the cartridge is
the sequencer. Instrument index = voice index of src/df_core.h.

A synth instrument is an ordinary PRG32 instrument descriptor whose
sample_id has bit 15 set (PRG32 docs/tools/audio.md):

    bit 15      synth marker
    bits 11:10  resonance 0..3
    bits 9:6    low-pass cutoff 0..15 (15 is about 1.3 kHz)
    bits 5:2    pulse width 0..15
    bits 1:0    0 triangle, 1 saw, 2 pulse, 3 noise

attack/decay/release bytes map to 1 + v*v*2000/65025 ms; sustain 0 makes a
percussive voice that decays to silence. Pan is -64 (left) .. 63 (right)
and is ignored on mono hardware. The MIDI note of each voice (pitch, or the
clock of the noise generator) is VOICE_NOTES in src/drumfight.c.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRI, SAW, PULSE, NOISE = 0, 1, 2, 3


def synth(wave: int, cutoff: int, resonance: int, pulse_width: int = 8) -> int:
    return 0x8000 | (resonance & 3) << 10 | (cutoff & 15) << 6 | (pulse_width & 15) << 2 | (wave & 3)


# name, sample_id, volume, pan, attack, decay, sustain, release
KIT = [
    ("KICK",     synth(TRI, 8, 2),       255,   0, 0, 60, 0, 24),
    ("SNARE",    synth(NOISE, 15, 1),    235, -14, 0, 46, 0, 20),
    ("HAT",      synth(NOISE, 15, 3),    190,  30, 0, 16, 0, 8),
    ("OPEN HAT", synth(NOISE, 15, 3),    180,  40, 0, 74, 0, 40),
    ("TAMMORRA", synth(TRI, 11, 2),      245, -40, 0, 64, 0, 30),
    ("BONGO",    synth(PULSE, 12, 2, 4), 220,  20, 0, 34, 0, 16),
    ("CAMPANA",  synth(PULSE, 15, 3, 6), 185, -28, 0, 36, 0, 18),
    ("CLAP",     synth(NOISE, 13, 2),    225,  12, 0, 34, 0, 16),
]


def main() -> None:
    instruments = [
        {"name": name, "sample_id": sid, "default_volume": vol, "default_pan": pan,
         "attack": a, "decay": d, "sustain": s, "release": r}
        for name, sid, vol, pan, a, d, s, r in KIT
    ]
    out = ROOT / "audio/audio.json"
    out.write_text(json.dumps({"samples": [], "instruments": instruments, "tracks": []}, indent=2) + "\n",
                   encoding="utf-8")
    print(f"audio/audio.json: {len(instruments)} instruments, 0 samples, 0 tracks")


if __name__ == "__main__":
    main()
