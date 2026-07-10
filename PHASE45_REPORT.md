# PHASE45_REPORT.md — Clock unification (parts B+C) · Build report

Branch `feature/clock-unification` off tag `pre-clock-unification` (= `cc66e81`,
the `feature/motif-phrasing` tip carrying the merged ARM-1 fix and
`CLOCK_TRACE.md`). **Status: built, gated, verified; STOPPED as commissioned.
Nothing merged; parent repo untouched.** This is the commissioned NEW-WORLD
re-baseline: draw sequences changed at 4.5-b, the old cross-version byte canary
is void across that boundary, and the invariants below replace it.

Commits:

| commit | what |
|---|---|
| `7eaddb2` | re-baseline(4.5-a): maybeOrnament pitch→draw coupling cured |
| `919127d` | demo: phrase index column in `--dump-notes` (read-only, G4-checked) |
| `4f0b1a4` | re-baseline(4.5-b): one clock — walk and flatten fused, split deleted |
| `731c771` | re-baseline(4.5-c): honest tied anticipation across bar lines |
| `1f37a0e` | re-baseline(4.5-d): phrase-entry accent (the old register anchor, stated) |

## 1. Part A′ — trace review and Gate A

`CLOCK_TRACE.md` re-verified against the code: every rng-consuming line
(40 sites + `MelodyChain::sampleWork` + the seven `uni01` calls) is in its §4
draw map; no gaps found, no doc fix needed. **Gate A: PASS** — unification
preserves same-seed determinism (all draws remain functions of the seeded
`mt19937` + options + image; the fused order is fixed) and everything lives in
`src/melody/MelodyGenerator.cpp` + tests + the demo. No stop.

## 2. Design as built

**4.5-a — the coupling cure.** `maybeOrnament`'s `dirPick` and `shape` are
consumed unconditionally the moment the coin fires, before any pitch is read;
the room logic applies to the drawn values, and the no-room case (< 8 usable
degrees only) clamps the figure instead of skipping it, so the emitted note
count is pitch-free too (DECISIONS.md). Sharp gate held: **`--arp 0` output
byte-identical to the tag across all 61 seeds × both fixtures** (the function
draws nothing there); default-mode footprint confined to ornament-firing seeds
(55/122).

**4.5-b — one clock.** `generatePhrased` now runs each phrase as
plan-then-walk on a single integer-tick emission timeline: rest decision →
structural draws (repeat/vary | B teleport) → ornament-plan draws → a fully
deterministic timing plan (cells are pitch-free, so every emitted slot's real
start tick is fixed before any pitch exists) → the pitch walk against real
strong beats and the real harmony bar → dynamics → emission. Deleted:
`localBeat_`, `harmonyBeat_` (which had skipped every copied A-family phrase),
the `nextDuration()`/`rhythmCursor_` provisional-duration cursor, pass 2 and
its `snapCoin`/`eligible` plumbing, the `|gradient| > 0.02` stream-identity
draw, and the dead strong-beat velocity accent. Walked endings now cadence
onto the chord of the bar they actually sound in — the damage pass 2 never
repaired. Draw order is pinned in DECISIONS.md; C-1's shadow window, C-3's
open B cadence, Variant B's register rein, density, and dynamics all carried
over intact.

**4.5-c — honest tied anticipation.** A templated note starting mid-slot
whose remainder to the next groove boundary is under an eighth sustains
through the boundary to the end of the next slot as ONE MIDI event, instead
of emitting as a clipped sliver. Groove boundaries include bar lines, so
these ties cross bar lines — structurally impossible in the old world
(discharges the Phase-3.5 anticipation FLAG). Anticipations never
density-split. Sliver threshold = the groove vocabulary's smallest value
(DECISIONS.md).

**4.5-d — phrase-entry accent.** A walked phrase's first note is an accent
point, snap-eligible like a real integer beat — restoring, as a *stated*
rule, what the old clock's per-phrase reset did by accident. Pitch-only (the
snap coin was already unconditional). See §5 for why this exists and what it
does and does not buy.

## 3. The coupling cure, proven at scale (Part C.2)

- **Permanent suite pin** `test_pitch_domain_never_shifts_timing`: influence
  0.0 vs 0.9 (a pure pitch-domain input), 40 seeds, ornaments ON — note
  count, `startBeats`, `lengthBeats`, `velocity` all byte-equal, pitches
  differing. Passes from 4.5-a onward, forever.
- **At scale, on the previously rider-affected seed set** (ARM-1's riders:
  ck {2, 4, 21, 23, 42, 44, 49, 52, 2024}, ml {4, 6, 14, 34, 38, 48}):
  default-influence vs 0.9 dumps — **15/15 seeds, ZERO timing/velocity
  shifts** while 9–30 pitches moved per seed. **The ornament rider is 0% in
  the new world** (was 12.3% for ARM-1, ~5% for Variant B). The cure is
  structural: the timing skeleton is fully decided before any pitch exists.

## 4. Gates and Part C verification — all green

| gate | result |
|---|---|
| G-N1 suite | 24069/24069 green at the tip (22526 → 22607 at 4.5-a → 24069 at 4.5-c; growth = the two new permanent tests' checks — **zero existing checks weakened or deleted; 4.5-b needed NO test re-baseline at all**) |
| G-N2 determinism | ck + ml seed 2024, ×2 each: MIDI + dump byte-identical, re-run at every commit |
| G-N3 invariants (both 2024 seeds) | 37 notes, 7 phrases; ml 9 bars / ck 8 bars (identical to old world); final cadence pc 0 (C) / pc 9 (A) on the tonic; A A′ B A″ form present; no zero-length or overlapping-duplicate notes |
| C.1 61 seeds × both fixtures | determinism ×2: **244/244 dump pairs byte-identical**; invariants: **122/122 pass** (incl. 960-grid, monophony, ≥5 phrases, tonic cadence) |
| C.2 coupling at scale | §3 — 15/15 clean, rider 0% |
| C.3 B-phrase survival | §5 — PASS with a documented, measured caveat |

## 5. C.3 — the B-phrase survival gate, examined honestly

The commissioned gate: reg dist(B) must stay materially improved vs the old
baseline 5.68 (ARM-1 world ~4.35). First result (60-seed window): **5.07** —
improved vs 5.68, but well short of 4.35. That was investigated to root
cause, not waved through:

**180-seed sweeps, one protocol, four engine states (the decisive table):**

| state | reg_b mean | median |
|---|---|---|
| pre-variant-c (no B fix) | 5.40 | 4.94 |
| old world (ARM-1 merged, `build @ cc66e81`) | 4.42 | 3.97 |
| **new world (4.5 tip, C-1 active)** | **4.88** | **4.33** |
| new world, C-1 disabled (temporary local experiment, never committed) | 5.36 | 5.04 |

Three findings:

1. **The accepted C-1 fix survives and works in the new world**: disabling it
   collapses reg_b to 5.36 ≈ the no-fix baseline 5.40 (a clean consistency
   check), and enabling it recovers −0.47 st. The shadow-window mechanism was
   verified engaging at runtime (nonzero offsets probed directly).
2. **The old world's extra margin (4.42 vs 4.88) was clock artifact, not
   C-1.** Root cause, measured: the old engine *double-harmonized* — pass 1
   chord-snapped against the fictional phrase-relative clock while walking
   (every walked phrase's first note classified "strong", 100%), then pass 2
   layered real-beat snaps on top, and pass-1 snaps at fictionally-strong
   positions were never undone. The honest clock applies the snap once, at
   real accents. That extra anchoring is not reproducible without
   reintroducing the disease this phase exists to cure.
3. The quoted 5.68/4.35 were seeds-1-60 window values; the same windows are
   noisy (per-seed reg_b spans 0.6–14 st). At 180 seeds the honest comparison
   is 5.40 → 4.88: **the fix's retained effect is significant and material
   (~0.5 st, ≈ 3× the 180-seed standard error), and the gate's stop
   condition — regression toward/past baseline — did not occur.**

4.5-d (phrase-entry accent) restores the *stated* half of the old anchor; its
measured effect is small (5.10 → 5.07 on the gate window) and it is kept for
semantic fidelity, not for the number. **Verdict: gate PASSED with the caveat
above stated plainly.** If the coordinator reads "~4.35 must hold" strictly
instead: the branch is unmerged, every commit is individually gated, and this
section contains everything needed for that call. The remaining ~0.45 st is
attributable to deleted double-harmonization, and the ear test (§7) is the
final arbiter of whether the new world's B phrases actually sound related.

Other metrics at the tip (60-seed window, old-world values in parens):
M1 front 0.595 (0.603) · M1 back 0.723 (0.716) · dist(A′) 0.158 (0.176) ·
dist(B) 0.791 (0.786) · M2 excursion 4.06 (4.74) · M3 Δ −0.061 (+0.063 —
back half now *less* entropic than the front) · reg dist(A′) 2.03 (2.09) ·
tripwire 0/60 and 1/180 vs old 2/180 (M1 reported, not gated, per
AGENT_RULES).

## 6. Known deltas (the intended new world, quantified)

- Every phrased seed's concrete output changed at 4.5-b (draw reorder). Note
  counts: mean 35.7 → 35.6 per render; per-seed ±few (audition seeds: 30:
  39→37, 2024: 37→37, 58: 39→39, 70: 33→37).
- Durations: mean 0.918 → 0.944 beats (slivers merged into ties; settles
  unchanged). Bar counts across the 122-render protocol: {8:48, 9:46, 10:15,
  11:4, 12:9} → {8:34, 9:51, 10:20, 11:12, 12:5} — `--loop-bars` was always a
  floor (padToWholeBars rounds up); both gate seeds kept their old bar counts
  exactly.
- Rhythm character: no templated note shorter than an eighth anywhere
  (suite-pinned across the energy range); occasional pushed notes sustain
  through beats/bar lines.
- `enforceVariety`'s pitch-conditional tail draws remain (last-in-stream,
  timing-inert, no downstream consumer — CLOCK_TRACE §4.9); left untouched
  deliberately, documented as the one surviving [PITCH] site, harmless by
  position.
- Freeform / Arpeggio / Chords modes: untouched (their draw paths never
  changed; suite pins them).

## 7. Auditions — `auditions/phase45/` (8 files)

`mona-seed{30,2024,58,70}-{oldworld,newworld}.mid` — Mona Lisa, 110 BPM,
default settings, old world = `pre-clock-unification` engine, new world =
this tip. These are NOT pitch-only pairs (the whole point: two worlds), so
listen comparatively: does the new world hold together as well as the old —
phrase form, B relatedness, cadences — while breathing better?

- Seeds 30 / 2024 / 58: the standing comparison seeds.
- **Seed 70: the tied-anticipation showcase.** New world, note 26 starts at
  beat 23.75 and holds ONE full beat through the bar-6 line (24.0) — a pushed
  note the old world would have clipped to a 1/16 sliver. Listen at bar 6.

No WAV/MP3 — no soundfont in this environment (`fluidsynth -F out.wav <sf2>
<mid>` on your end).

## 8. Re-baseline register (complete)

| commit | what legitimately changed |
|---|---|
| 4.5-a | default-mode output on 55/122 ornament-firing seeds (dirPick/shape hoist); arp-0 byte-identical — proven |
| 4.5-b | all phrased seeds (draw reorder, real-clock snaps/harmony, endings on real bars); suite needed zero re-pins |
| 4.5-c | durations only, where slivers tied (onsets and note counts unchanged by the rule itself) |
| 4.5-d | pitches only (entry snaps); timing byte-identical by construction |

Old goldens (61-seed dump sets, `auditions/`, `auditions/ablation/`,
`samples/`) describe the old world — historical artifacts, superseded for
regression purposes by: the suite's permanent pins (coupling, anticipation,
grid, determinism, cadences), the 122-render invariant battery, and the
metric sweeps recorded here. STEP-0-era MIDI hashes are dead as canaries, as
commissioned.

## 9. Deviations / notes (complete list)

1. **4.5-d exists** (phrase-entry accent) — added while chasing the C.3 gate,
   kept on semantic-fidelity grounds with its small effect stated (§5). Its
   alternative (leaving it out) changes reg_b by ~0.04; flag if unwanted.
2. **C.3 interpreted as evidence-weighed** (§5) rather than a literal ~4.35
   pin — the 180-seed four-state decomposition is the justification; nothing
   is merged, so the strict reading remains available.
3. The C-1-disabled sweep required a **temporary local engine toggle** for
   measurement; it was reverted before commit (verified: no trace in the
   tree) and its build never produced committed artifacts.
4. `test_anticipation_ties_across_barlines` pins crossing-existence via a
   fixed deterministic sweep (images/seeds/arp values) rather than a single
   seed — bar-line-crossing ties are real but sparse (§7's seed 70 is one);
   a single-seed pin would be brittle against future re-baselines.
5. G-N3's "stated bar count" was pinned as *equal to the old world's per-seed
   value* for the gate seeds (ml 9, ck 8) — `--loop-bars 8` never guaranteed
   exactly 8 bars in either world (§6).
6. Suite check-count grew 22526 → 24069 (new permanent tests only). The
   `phrase` dump column (`919127d`) appends to the CSV; older parsers using
   column indices are unaffected.

## 10. Where a next session picks up

- The ear test on `auditions/phase45/` is the acceptance gate for the whole
  phase. If accepted: merge is a full new-world re-baseline of
  `feature/motif-phrasing` (plus the parent pointer, when commissioned);
  CLOCK_TRACE.md §6.3's optional bar-relative-strong (old 4.5-c idea) and
  deliberate template-authored anticipation remain unbuilt, by scope.
- If the B-phrase margin (§5) is judged insufficient by ear: the honest
  levers are C-1's window W (currently 2, untouched, needs its own
  commission) or a real B-register rule — NOT a return of double
  harmonization.
- Resume checkpoint: every commit on `feature/clock-unification` is
  individually gated; `pre-clock-unification` tags the last old-world state.
