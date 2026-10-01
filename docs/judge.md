# The groove judge

The judge is a deterministic function of the pattern
(`df_judge()` in [`src/df_core.h`](../src/df_core.h)). It uses integers only,
so every host, and every board in a network battle, computes exactly the same
score for the same bar. It measures properties that drummers and rhythm
research agree on; it does not claim to measure taste.

A step has one of four levels: off, ghost, hit, accent. "Note" below means
any level except off; "played note" means hit or accent.

## Six criteria, 0-100 each

| Criterion | What it rewards | How |
|---|---|---|
| **PUL** pulse | A foundation the piazza can follow | Kick on beat 1 (30) and on beat 3 (15); snare or clap on beat 2 (20) and on beat 4 (20); a hat, open hat or campana on eighth-note positions, 3 points each for up to five (15) |
| **VAR** variety | The whole circle plays | Voices with at least one played note: 0, 5, 15, 28, 42, 56, 70, 85, 100 points for 0..8 voices |
| **BAL** balance | Room to breathe | Notes in the bar: 5 points per note up to 20; 100 from 20 to 56 notes; minus 3 per note above 56. Minus 6 for every step where more than five voices sound together; minus 8 for every voice except the hat with 13 or more notes |
| **SYN** syncopation | Off-beat tension | See below. Sum of 12 to 30 is 100; below 12 it grows linearly; above 30 it loses 3 per unit |
| **DYN** dynamics | Loud and soft | Accents between 10 % and 35 % of the notes give 70; fewer or more give less. One ghost note adds 15, three or more add another 15 |
| **STR** structure | Call and response | Compare the two halves of the bar. Similarity = cells sounding in both halves / cells sounding in either. 50-85 % is 100; identical halves score 60; unrelated halves 20 |

### Syncopation

Each step has a metric weight: 4 for beat 1, 3 for beat 3, 2 for beats 2 and
4, 1 for the other eighths, 0 for the remaining sixteenths. A played note is
*syncopated* when the same voice stays silent up to and including the next
position of greater weight (Longuet-Higgins and Lee, 1984); it adds the
difference of the two weights. Ghost notes count as silence. The closed hat
is a timekeeper and is not counted.

## Base groove, 0-800

```text
base = 1.5 PUL + 2 VAR + BAL + 1.5 SYN + DYN + STR        (0..800)
base = base * (100 + 3 BAL) / 400                          (25..100 %)
base = base * (voices + 8) / 16                            (9/16..16/16)
```

The two factors make the two easy exploits unprofitable: filling every step
(BAL collapses, and the whole score with it) and looping two voices (the
ensemble factor).

## Total, 0-999

```text
total = min(999, base + 25 * number of different moves performed)
```

An empty bar scores 0, whatever moves were performed and erased.

## Reference values

Checked by `tests/test_core.c`:

| Pattern | Base |
|---|---|
| Kick on 1 and 3, snare on 2 and 4, eighth-note hats | 174 |
| The eight-voice "piazza" groove of the tests | 791 |
| All 128 steps filled | 102 |

A simple rock beat unlocks QUATTRO and CONTROTEMPO; the later moves need a
real ensemble.

## The CPU opponents

`df_ai_compose()` plays by the same rules as a human:

1. it lays a foundation (kick, backbeat, hats);
2. it performs the moves its level knows (one, three, or six to seven);
3. it improves the bar by **hill climbing** on the same judge: change one
   random cell, keep the change if the total moves towards the target of the
   level, stop when it is reached. A level that starts above its target
   plays worse on purpose, so weaker opponents stay beatable.

| Level | Target | Iterations at most | Measured over 200 seeds |
|---|---|---|---|
| MATRICOLA | 300 | 200 | mean 313, range 273-366 |
| FUORICORSO | 560 | 400 | mean 571, range 560-629 |
| MAESTRO | 820 | 500 | mean 842, range 797-875 |

The generator is a 32-bit xorshift seeded from the time and frame of the
player's first menu choice, so every match is different, while a given seed
always produces the same bar (the tests rely on it). The title screen plays
a MAESTRO bar from a fixed seed.

## Changing the rules

Edit `df_judge()`, then run `tests/run_tests.sh`: the tests print the
reference values and the CPU ranges, and fail if the three levels stop being
clearly apart. A rule change alters scores between versions, so it must
also change the multiplayer signature (see [multiplayer.md](multiplayer.md)).
