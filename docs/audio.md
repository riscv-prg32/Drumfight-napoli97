# Audio

Drumfight is a drum machine, and the audio is built like one: a kit of
one-shot drum voices, one mixer channel per voice, a clock that fires steps
at exactly even intervals, swing, accents, and a hi-hat that chokes.

Release 1.0.0 played its drums on the PRG32 SID-like synthesizer alone and
scheduled steps on a millisecond clock. It did not sound like a drum
machine: that synthesizer has no pitch envelope (so no kick "thump") and its
filter stops at about 1.3 kHz (so no bright snare or hat), and steps landed
up to a frame late, unevenly. Release 1.1.0 replaced both the kit and the
clock. This document describes the current design.

## Signal path

```text
pattern step  ->  drum(voice, level)  ->  prg32_audio_note_on_pan(channel = voice, instrument, note, volume)
                                                     |
                             PRG32 mixer: 8 channels, 22050 Hz, stereo pan, signed 16-bit  ->  I2S / QEMU UART
```

- **Voice n always plays on channel n.** All eight voices can sound on one
  step, and a new hit of a voice cuts only its own previous hit, as on a
  hardware drum machine.
- The firmware mixes eight channels in both the ESP32-C6 and the QEMU
  profile (`CONFIG_PRG32_AUDIO_MAX_VOICES=8`).
- **Choke:** a closed hat stops the open-hat channel
  (`prg32_audio_stop_channel`), so an open hat rings only until the next
  closed one.

## Two kits

[`tools/build_audio.py`](../tools/build_audio.py) generates both kits into
`audio/` (eight `.raw` samples and `audio.json`, all committed);
PRG32's `tools/prg32audio_pack.py` packs them into the cartridge's AUD0
block (24,804 bytes). The kit is chosen in the START menu (`KIT`), at any
time, while the loop plays. It is a local setting: it changes what you hear,
never the pattern or the score.

### ANALOG (default, instruments 0-7)

PCM one-shots, unsigned 8-bit mono, **computed from formulas** on the model
of the circuits of the classic analogue drum machines. Nothing is recorded
and nothing is sampled from an instrument or a machine; the generator has
its own noise source, so the bytes are identical on every run.

| # | Voice | How it is made | Length | Pan |
|---|---|---|---|---|
| 0 | KICK | sine swept from 182 Hz down to 52 Hz in about 30 ms, 110 ms decay, a 2 ms noise click, soft saturation | 259 ms, stored at 11025 Hz | centre |
| 1 | SNARE | two swept sines (180 Hz and 330 Hz shells) plus high-passed noise for the wires | 169 ms | slightly left |
| 2 | HAT | six detuned square waves (205-800 Hz) and noise through a 7 kHz band-pass and a 5 kHz high-pass, 12 ms decay | 49 ms | right |
| 3 | OPEN HAT | the same metal, 75 ms decay | 200 ms | right |
| 4 | TAMMORRA | frame drum: a low skin (155 to 95 Hz), a hand slap, and the jingles of the frame 8 ms later | 160 ms | left |
| 5 | BONGO | sine swept from 650 Hz to 400 Hz in 6 ms, a 640 Hz ring, a tap | 89 ms | right of centre |
| 6 | CAMPANA | cowbell: 540 Hz and 800 Hz squares through a band-pass, two-stage decay | 149 ms | left of centre |
| 7 | CLAP | band-passed noise in three 9 ms bursts, then a tail | 160 ms | right of centre |

The whole kit is 24,473 bytes. Samples are stored at the mixer rate
(22050 Hz) except the kick, which has nothing above a few hundred hertz and
is stored at half rate and played an octave down (`base_note` 72, played as
note 60). Every sample is normalised to full scale for the best use of
eight bits and fades to silence at its end. The mixer plays samples without
interpolation, which is part of the gritty character of early sample-based
drum machines.

### SID (instruments 8-15)

The 1.0.0 kit: eight procedural instruments of the PRG32 SID-like
synthesizer (triangle for kick and tammorra, noise for snare, hats and clap,
pulse for bongo and campana), percussive envelopes, no sample bytes. It
sounds like a chiptune imitation of a drum kit, for the reasons above, and
is kept as an alternative voice for the same patterns. Its notes are
`SID_NOTES` in `src/drumfight.c`; for noise voices the note is the clock of
the noise generator.

## Levels

| Level | Note-on volume |
|---|---|
| ghost | 112 |
| hit | 188 |
| accent | 255 |

each scaled by the gain of the voice in the selected kit (`VOICE_GAIN`). The
mixer applies the note volume twice (voice and channel), so loudness follows
the square: a ghost note is about 19 % of an accent, a hit about 54 %. The
title screen plays at two thirds of these values.

## Stereo

Pan runs from -64 (left) to +63 (right) and is the default pan of each
instrument; the cartridge passes "centre", which tells the firmware to use
that default. On stereo hardware (PRG32 Audio Plus) the kit is spread like a
drum circle; on mono hardware and in QEMU the firmware collapses it.

## The clock

The firmware calls the cartridge every 33 ms, and a note can only start on
a frame. A drum machine must be even above all, so **the tempo is a whole
number of frames per sixteenth note**:

| Frames per sixteenth | Step | Tempo |
|---|---|---|
| 6 | 198 ms | 76 BPM |
| 5 | 165 ms | 91 BPM |
| 4 | 132 ms | 114 BPM |
| 3 | 99 ms | 152 BPM |

Every step is then exactly the same number of frames after the previous
one. Arbitrary tempos are deliberately not offered: at 120 BPM a sixteenth
is 3.8 frames, and the steps would alternate between 3 and 4 frames in an
audibly uneven pattern.

`seq_tick()` still measures time in milliseconds and fires a step on the
frame nearest to its time. With frames arriving regularly that is the same
frame count every step; if frames are irregular the tempo follows the
clock, not the frame counter. `tests/test_game.c` checks both: 32
consecutive steps at 114 BPM exactly 132 ms apart, and 8 bars within 70 ms
of their length when frames vary between 29 and 37 ms.

### Swing

With swing on, every off sixteenth (the "e" and the "a") is played one frame
late: the gap after an on sixteenth grows by 33 ms and the next one shrinks
by 33 ms, so the eighth notes stay where they are. At 114 BPM that is
165 ms + 99 ms, a 62 % shuffle.

Practice offers the four tempos and the swing switch in the START menu.
Battles play round 1 at 91 BPM, round 2 at 114 BPM and round 3 at 114 BPM
with swing.

### Live pads

A pad sounds on the frame it is pressed. It is recorded on the nearest step
(the one that just fired or, in the second half of the gap, the next one);
a hit recorded for the next step is marked so the sequencer does not play
it a second time when that step arrives.

## Other sounds

Menu blips, the unlock jingle, the move flourish and the final fanfare are
short tunes on the ANALOG campana, bongo and kick, played through a small
frame-delay queue (`jingle()`); the note transposes the sample (60 is its
natural pitch). While the bar is empty a quiet campana marks the beats.

## Listening without a board

```bash
python3 tools/build_audio.py --demo build/kit-demo.wav
```

writes a stereo WAV: each ANALOG voice alone, then four bars of a groove at
114 BPM, mixed with a model of the firmware mixer (nearest-sample playback,
squared volume, linear pan, voice cut and hat choke).
`assets/store/preview.mp4` carries the audio of the real QEMU firmware
playing the cartridge.

## Budgets

| | Bytes | Limit |
|---|---|---|
| AUD0 block (8 samples, 16 instruments) | 24,804 | |
| Load image: header + code + audio | about 47,400 | about 54,500, the measured limit of the QEMU firmware's loading heap (checked by `scripts/build.sh`) |
| Stored package with the Store trailer | 54,496 | 65,536 |

There are about 7 KB left for audio. A longer open hat or a ninth sample
must fit in that.

## What has and has not been checked

Checked, by measurement:

- each sample: length, fundamental and spectral centroid match its design
  (kick fundamental about 50 Hz, hats centred near 7 kHz), tail at zero, no
  DC offset;
- the QEMU firmware loads the 24,804-byte block and plays the kit; the
  capture of the preview session has energy from 30 Hz to 10 kHz, a peak of
  -14 dBFS and no clipped samples;
- PRG32-QT plays the PCM voices (about 300,000 PCM samples in 300 frames).

Not checked: **nobody has listened to this kit yet**, on a speaker or
otherwise; it was designed and verified by numbers. Timbre and balance are
judgements for ears. The parameters to tune are the formulas in
`tools/build_audio.py` and `VOICE_GAIN` in `src/drumfight.c`; the demo WAV
above gives the result in a second. Hardware items (small-speaker bass,
stereo image, the loading heap of a physical ESP32-C6 with a 47 KB image)
are in [testing.md](testing.md).
