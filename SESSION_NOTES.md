# Phase 4c — Better cadences / phrase endings

Baseline Phase 4b (`2126cca`). Scope: phrase endings only — mid-phrase generation,
locks and arps untouched. Two small tested commits; a deliberate re-baseline of
endings (intended) with the draw stream kept stream-stable.

## Commit 1 — cadential pitch: endings resolve, cadence uses the leading tone

Two pitch-domain changes in the ending path, both **draw-count neutral** (same
RNG draws, in the same order — only the deterministic outcome changes):

1. **Endings land on the active harmony** (`stepNote` ending branch). A walked
   phrase's final note now biases toward *and then deterministically snaps to* a
   chord tone of the CURRENT bar's chord (`nearestChordToneDegree`), instead of
   always the key tonic/fifth. The single `nextBiased` draw is preserved (stream
   identity); the snap adds no draw. So phrases resolve onto root/3rd/5th of the
   chord they end over — landing, not drifting.
2. **Leading-tone cadence** (`closingPhrase`). When the scale step just below the
   tonic is the leading tone — a semitone below (pitch class 11 above the root:
   major and harmonic minor; absent in the minor modes) — the closing cadence
   approaches the final tonic UP through it (V's raised 7th → i, reusing the tone
   4a spells in the V chord). Otherwise the nearer-step approach is unchanged. The
   leading tone is used **every time the tonic isn't at the very bottom of the
   range** (no step below there — then it approaches from above, correctly).

Draw stream: the ending branch still draws exactly one `nextBiased`; the cell
walk is contour/brightness-driven (independent of pitch), so **no draw is
added/removed/reordered** for any note. Downstream pitches shift only because the
cadence changes the Markov state (`degree_`) carried into the next phrase — the
intended re-baseline, not a stream perturbation.

**Tests:** `test_phrased_endings_resolve_to_chord_tone` (checkerboard, ornaments/
density off: walked phrase-final notes are chord tones of their generation-time
chord — via `dbgChordRoot` — at ≥ 90%, and FAR more often than mid-phrase notes);
`test_harmonic_minor_cadence_uses_leading_tone` (the leading tone is the approach
every time a step below the tonic exists; final note is the tonic held ≥ 2 beats);
`test_phrased_cadence_deterministic` (same seed ⇒ identical, harmonic minor).
Existing `test_phrased_cadence_on_tonic` / `test_phrased_ending_is_stepwise` still
pass. Engine **24520/24520 green**.

**Re-baseline (checkerboard, seed 2024, 8 bars):** note count **37 → 37**
(unchanged — pitch-only); endings now land on chord tones. Same-seed re-run
byte-identical (deterministic).

## Commit 2 — cadential length: phrase endings settle

The LAST note of each **non-closing** phrase now settles to at least a dotted
quarter (`kPhraseEndCadenceBeats = 1.5`, tunable) so phrases land instead of
cutting off. Fed pre-flatten via the emitted length only — the flatten
accumulator absorbs it and the next phrase re-aligns to the bar as usual; **no
parallel counter/clock**. It only ever EXTENDS (never shortens), stays on the
0.5-beat grid (1.5 beat = 1440 ticks), and leaves the ending note WHOLE (no
density subdivision on a cadence). The closing phrase keeps its own longer
cadence (`kCadenceBeats = 2.0`, unchanged).

**Consequence for density:** because phrase-final notes are now left whole, a
busy image subdivides slightly fewer notes. Checkerboard density counts re-baseline
**103 → 91** (density 0.5) and **136 → 118** (density 1.0); **density 0 is
unchanged at 37** (the settle changes only the ending's length there, not the
count) and stays a deterministic no-op. This is the intended trade — endings settle
even inside busy passages, giving contrast.

**On-grid / determinism:** verified 0 off-grid note starts on the checkerboard
(durations land on {0.25, 0.5, 0.75, 1.5, 4.0}); same-seed re-run byte-identical.
**Test:** `test_phrase_endings_settle_longer` (every non-closing phrase's final
note is >= 1.5 beats). Engine **22412/22412 green** (the check total drops vs
Commit 1 because density-on melodies now emit fewer subdivided notes, not because
any assertion was skipped). Parent green except the pre-existing wavetable golden.

---

# Phase 4b — Locks + regeneration (post-hoc splice model)

Baseline Phase 4a (`e282555`). Gate result (from the pre-code trace): pitch and
rhythm share ONE interleaved mt19937 stream — **not** splittable without
reordering draws and re-baselining everything. So **all locking is post-hoc on
emitted `Melody` note lists** (`recombineLocked` / `mutate`); the melody walk and
its draw order are untouched, generation stays byte-identical to 4a.

## Commit 1 — harden the existing splice locks + count-matching

`recombineLocked`, `mutate`, `RegenLocks` and the parent controller wiring
(`MelodyController::regenerate`/`mutate`, `melodyLockRhythm`/`melodyLockPitch`)
already existed. This commit fixes count-matching and adds engine tests.

**Count-matching decision (implemented).** The **locked track is authoritative
for both note count and timing.** `recombineLocked` previously cycled the pitch
source with `i % pitchSrc.size()` — when the two tracks differed in length this
jumped the pitch line back to note 1 mid-phrase. Replaced with **read-in-order,
clamp-to-last**: `pi = min(i, pitchSrc.size()-1)`. When the pitch source is
shorter its final pitch is held; when longer it truncates to the locked count.

- *Why hold-last, not re-walk?* A true re-walk extension would need to re-run
  generation (rng + generator state); `recombineLocked` is a pure post-hoc splice
  with no rng, so a clean re-walk is out of scope here. Hold-last is the simpler
  reversible option (per the working-loop rule) and removes the only audible
  artifact (the cycle-back jump). If musically wanted later, a re-walk extension
  belongs in the controller (regenerate the candidate at the locked length).

**Tests added** (`MelodyGeneratorTests.cpp`): `test_splice_locks_deterministic`
(recombine is pure — same in → byte-identical out; mutate deterministic given
seed and varies with it) and `test_splice_count_matches_and_holds_last`
(differing-count case: locked count authoritative, pitches in order then held,
never cycled to 0; truncates when the pitch source is longer). The pre-existing
`test_recombine_locks_dimensions` / `test_mutate_respects_locks` (equal-count
Lock Rhythm / Lock Pitch / Mutate-honours-locks) still pass.

**Isolation / determinism:** `recombineLocked` is called only from the parent
controller — never from `generateMelody` or `lumena_demo` — so generation output
is unaffected by construction. Verified: checkerboard phrased seed 2024
byte-identical on re-run. Engine suite **24426/24426 green** (was 24389).

## Commit 2 — Lock Harmony (progression promoted to a lockable input)

Per the trace's determinism-safe path: the chord progression becomes an explicit
**input** instead of an RNG **output**.

- **`MelodyOptions::progression`** (new, empty by default). Empty ⇒ draw one
  template exactly as before (byte-identical); non-empty ⇒ voice those roots and
  draw **no** RNG for the progression. `progressionRoots` was refactored into
  `pickProgressionBase` (the single `pick(rng)` draw) + `tileProgression` (rng-
  free) + `pickOrDrawProgression` (input-or-draw), wired into all three call
  sites (phrased ctor, arp, chords). The old `progressionRoots` wrapper is gone.
- **`Melody::progression`** (new carrier): every mode (except Freeform, which has
  no harmony) records the base progression it voiced, so a caller can carry it
  forward.
- **`RegenLocks::harmony`** + **`melodyLockHarmony`** param (parent), read in
  `locksFromParams`. `MelodyController` keeps `currentProgression` (in-memory)
  and, on `regenerate()` with Lock Harmony on, feeds it back through `renderFresh`
  so pitch/rhythm re-roll under a fixed progression. **No UI** (parked to Phase 5)
  — param + engine only. Param appended last (no automation-index shift); no
  `kStateVersion` bump, matching the sibling `melodyLock{Rhythm,Pitch}` precedent
  (added at v3 without a bump; inert bool, backward-compatible).

**Known limitation (reversible, noted not fixed):** `currentProgression` is
session-memory only — not persisted in plugin state. After a reload, the first
Regenerate with Lock Harmony on draws a fresh progression until a generation
re-seeds the carrier. Persisting it needs a state-schema slot (a `kStateVersion`
bump + migration); deferred as a small follow-up to avoid scope creep here.

**Determinism proof:** with no progression supplied, checkerboard seed 2024 is
**byte-identical** to the prior commit across ALL four modes (phrased / freeform /
chords / arp), built from a worktree at the prior tip. The refactor is
byte-neutral.

**Tests.** Engine: `test_lock_harmony_carries_progression` (a generation records
its progression; feeding it back pins the harmony across a new seed while notes
re-roll; same seed+progression ⇒ byte-identical; two different progressions at
the same seed ⇒ different notes, so the input is honoured). Parent:
`MelodyLockHarmonyWiringTest` (the `melodyLockHarmony` param, driven through the
real controller, holds the progression across `regenerate()` while notes change).
Engine suite **24432/24432 green**; parent green except the pre-existing
wavetable SHA-256 golden (untouched).

## Demo flags + samples — `samples/phase4b-locks/`

`lumena_demo` gained default-off flags (default output unchanged, re-verified
byte-identical): `--progression a,b,c,d` (Lock Harmony), `--regen-seed N` +
`--lock rhythm|pitch` (splice via `recombineLocked`), `--mutate AMOUNT
[--mutate-seed N]`. Mona Lisa (audition, C Minor), 8 bars, 110 BPM:

| File | Demonstrates |
|---|---|
| `base.mid` | fresh phrased melody, seed 2024 (37 notes) |
| `lock-rhythm_new-pitch.mid` | Lock Rhythm vs seed 4242: timing identical to base, all 37 pitches new |
| `lock-pitch_new-rhythm.mid` | Lock Pitch vs seed 4242: base pitches, candidate's timing (35 notes) |
| `harmony-locked_seedA/B.mid` | same progression I-V-vi-IV, seeds 2024/4242 — harmony fixed, notes re-rolled |
| `mutate-before.mid` / `mutate-after.mid` | base vs a 0.30 mutation (seed 7) |

Determinism: the lock-rhythm splice on the **checkerboard** re-runs byte-identical
(`determ-ck-lockrhythm.mid`). WAV/MP3 still needs a soundfont on your end.

---

# Phase 4a — Scale-aware chords & arps

Baseline `42bee86`. A **deliberate arp re-baseline**: same-seed determinism
holds, non-arp modes are byte-identical to baseline, and the **arpeggiator's
pitches intentionally change**. Chord/arp spelling path only — the melody
generation path is untouched.

## What was already done (verified, not re-done)

- **Harmonic minor + all 7-degree modes** already spell from the detected
  scale's own intervals — landed in `9242e9c` ("bug 5"), an ancestor of HEAD.
  The V of D harmonic minor is already a real A-major triad with its C#
  leading tone; `test_chords_spell_harmonic_minor_leading_tone` and
  `test_arpeggio_spells_harmonic_minor_leading_tone` pass. Nothing to change.
- **Pentatonics** already voice real triads via the major/minor fallback in
  `diatonicChord` (stacking their raw degrees would give sus/quartal clusters),
  so they were left exactly as-is.

## The one real gap: Blues (6-degree) was flattened

Blues `[0,3,5,6,7,10]` is a 6-note scale, so it fell through `diatonicChord`'s
non-7-degree fallback, which spells a **plain minor triad** and silently drops
the ♭7 blue note. Fix (arp only): a new `scaleIsBlues()` (signature trio
♭3 + ♭5 + ♭7, unique to blues among the detected scales) makes the arp voice a
**real minor-7th** (root-♭3-5-♭7) instead of a triad, so the ♭7 sounds.

**Decision (per coordinating chat): in-scale spellings only.** Blues gets Am7/
Dm7/Em7 (all tones inside the parent natural-minor key), **not** dominant-7th
blues — the dominant's major 3rd is chromatic to the ♭3 melody and was
explicitly rejected. General principle applied: spell chord tones strictly from
the active scale/parent key; never introduce a chromatic tone to complete a
"nicer" chord.

- **Chords mode is unchanged for blues**: at `chordSize=3` it stays a minor
  triad (a correct in-key chord), and at `chordSize>=4` the existing kMinorSteps
  stacking already gives Am7 — so no code change was needed there. The ♭7 is
  restored specifically in the **arp**, where a broken-chord figure is where the
  7th adds motion. (If size-3 blues *chords* should also sound the ♭7 by
  default, that's a small follow-up — flagged, not done, to respect the agreed
  "size 3 stays a triad" behaviour.)

## Arps now spell 1-3-5-8

Every arp caps its ascent with the **octave root** (the "8"), so the figure
resolves up to the octave instead of stopping on the fifth/seventh:
`1-3-5-8` for one octave, `1-3-5-8-10-12-15` across two. This is a global arp
change (all scales). Note **counts are unchanged** (still `bars * notesPerBar`);
only pitches move — a clean pitch-only re-baseline. Blues combines both:
`1-3-5-♭7-8`.

## Verification

Built headless (WSL `build-wsl`). Prior-commit demo built in a worktree for
byte-comparison; worktree removed after.

| Check (checkerboard fixture, seed 2024, 8 bars) | Result |
|---|---|
| Phrased  new-vs-prior | **byte-identical** |
| Freeform new-vs-prior | **byte-identical** |
| Chords   new-vs-prior | **byte-identical** |
| Arp      new-vs-prior | **differs** (intended re-baseline) |
| Arp same-seed re-run  | **byte-identical** (deterministic) |

- Engine `LumenaTests`: **24389/24389 green** (was 24338; +51 from two new
  tests + their loops): `test_arpeggio_blues_spells_in_scale_seventh` (a
  seventh, role 3, is spelled AND every tone stays in the parent minor key) and
  `test_arpeggio_resolves_to_octave` (root recurs an octave up — the 8 — and a
  triad scale never spells a seventh).
- Parent `lumen_tests` (build-linux): green **except** the pre-existing
  `wavetable SHA-256 matches the pinned reference` (Lens golden pinned on the
  Windows toolchain; Linux FP differs — unrelated, untouched).

## Re-baselined samples — `samples/phase4a-arps/`

Arp mode, seed 2024, 110 BPM, 8 bars, `--arpeggiate` (default UpDown, 2 oct),
all 64 notes:

| File | Detected key | Shows |
|---|---|---|
| `checkerboard-arp.mid` | A Minor Pentatonic | determinism fixture; 1-3-5-8 octave cap |
| `mona-arp.mid`         | C Minor            | audition of 1-3-5-8 on a natural image |
| `blues-arp.mid` (+ `blues-green.png`) | E Blues | audition of the restored ♭7 (min7 arp) |

`blues-green.png` is a synthesised vivid-dark-green fixture (saturation 0.82,
luma in the blues band) committed so the E-Blues detection is reproducible —
none of the existing fixtures detect as blues. Measured on the E-Blues arp:
prior chord-tone roles = {0,1,2} (triads only); new = {0,1,2,3} (a seventh
spelled); pitch-class set unchanged and fully within E natural minor (no
chromatic tone). WAV/MP3 still needs a soundfont on your end.

## Not done / carried forward

- `4b` locks/regeneration is **gated on its pre-code RNG trace** — not started.
- Size-3 blues *chords* still stay plain minor triads by agreement (see above).

---

# Phase 3.5 — Motif-based phrasing (the "generated-sounding" fix)

Branch `feature/motif-phrasing`, off `feature/image-contour` (baseline
`f550871`). A **deliberate re-baseline**: same-seed determinism holds, but
output is intentionally **not** byte-identical to v1.0. Five commits + demo flag:

- `2783534` two-bar syncopated rhythm templates (image-detail-aware)
- `229565d` image density composes passing/neighbour tones, not chops
- `4b36753` image-selected motif contour (Rise/Fall/Arch)
- `ecaa0ff` image-fed motif variation (closes the RNG-only risk)
- on-grid guard test + `--density` demo flag

## The pre-code question (answered from the code before writing anything)

**Does the image feed the motif, or only enter per-note?** Traced:
`generateMelody -> generatePhrased -> PhraseBuilder`. Every phrased pitch comes
from `stepNote`, which already runs the roadmap blend
(`nextBiased(markov, mapBrightnessToDegree(brightness), imageInfluence)` +
strong-beat chord snap). So the **motif was already image-fed** (it is a walk,
not an RNG-picked template — the roadmap's central fear was not the state).
The gap was at the **structural layer**: `varyMotif` was **pure RNG**
(transpose δ, octave lift, rhythm nudge — never touched the grid), and contour
was emergent from a random 8-neighbour walk, not selected. Phase 3.5 moved both
onto the image:

1. **Contour** (`selectContour` + contour-directed walk): walked phrases pick a
   Rise/Fall/Arch contour from the brightness gradient at the start cell and
   realise it by steering the grid step toward brighter/darker cells, so the
   existing brightness->degree blend turns the image's own brightness path into
   the melodic contour. Deterministic, so the same image gives a recognisable
   motif shape across seeds.
2. **Variation** (`varyMotif`): the transposition direction/size now comes from
   the region's image-suggested degree vs the motif anchor, gated by Image
   Influence (RNG path kept for Influence 0 and draw-stream stability).
   Interval content is preserved, so A'/A'' stay recognisable.

## Go/no-go — Phase 0 inversion experiment (the gate)

Re-ran `tools/measure/measure_baseline.py`. Pre-3.5 vs post-3.5:

| Metric | pre-3.5 | post-3.5 |
|---|---|---|
| **C** corr(brightness,pitch) infl 0.0 / 0.5 / 1.0 | +0.08 / +0.39 / +0.77 | **-0.04 / +0.54 / +0.70** |
| **B** different images, matched colour, infl 1.0 (pch) | 0.251 | **0.422** |
| A same image across seeds (pch, default infl) | 0.259 | 0.323 |

- **Correspondence still climbs** with Image Influence — templates do **not**
  override the image (the fail condition). ✔
- **Different images diverge more** at high influence. ✔
- **Same image recognisable across seeds**: probed the motif directly at
  influence 1.0 — same-image motifs cluster (all start high ~4-5, arch/fall to
  0) while a structurally different image (block-shuffled) yields a distinctly
  different flat-low motif. ✔

## Rhythm / density variety

- Rhythm: three curated **two-bar** grooves replace the eight one-bar even-ish
  ones — a 3-3-2 dotted-eighth syncopated push and a long-short-short answer
  now dominate (measured: 0.75-beat dotted eighths are the most common length),
  fixing "almost all even subdivisions" and "one-bar loops". Selection blends
  Energy with a new mean-local-contrast image-detail scalar.
- Density: subdivided notes are now a stepwise **passing run** toward the next
  note (or a **neighbour** figure), not unison chops. Measured on the density-10
  sample: only 16% of steps are unison, the rest melodic motion. Note *counts*
  are unchanged (the subdivision-count logic is the same), so it is a like-for-
  like "compose, don't chop" upgrade.

## Rules honoured (from the build handoff)

- **Same-seed determinism holds** (verified: identical MIDI on re-run);
  byte-identical vs v1.0 **intentionally broken** (re-baseline).
- **Timing fed pre-flatten** as `PhraseNote.lengthBeats` / note-count edits and
  `subdivisions`. No parallel beat counter — the flatten-loop beat is still the
  sole timeline; `barAlignedDuration` tiles the 2-bar template's own period.
- **960 grid, safe subdivisions only**: new test `test_phrased_syncopation_on_grid`
  checks every phrased note lands on an integer 960-tick across the Energy range;
  the MIDI writer emits clean integer delta-times (no drift).

### Anticipation status — FLAGGED, not papered over

The handoff said anticipations lean on the deferred two-clock pass-2 and, if
mistimed, to flag rather than hide it. What shipped: **syncopation** via the
two-bar templates (off-beat 3-3-2 onsets) — these land on the grid and pass 2
correctly treats them as weak beats (no chord-snap), so nothing is mistimed.
**True tied anticipation across a bar line** (a note that sounds *before* a
downbeat and sustains through it, displacing the beat) was **not** added: it
would need a note whose length crosses the bar line against a still-per-phrase
`strong`/`localBeat_` clock, which is exactly the deferred Phase-4 two-clock
reconciliation. Flagging it here: when true anticipation is wanted, that is the
signal the Phase-4 clock work stops being deferrable. Current "breathing" comes
from syncopation + inter-phrase rests, which is sufficient and correct.

## Tests / suites

- Engine `LumenaTests`: **24338/24338 green** (added
  `test_density_composes_melodic_content`, `test_phrased_syncopation_on_grid`).
- Parent `lumen_tests` (build-linux): green **except** the pre-existing
  `wavetable SHA-256 matches the pinned reference` (Lens golden pinned on the
  Windows toolchain; Linux FP differs — untouched, unrelated). The four
  **Melody Density wiring** tests pass, so the passing-tone density rework
  satisfies the plugin bridge unchanged.

## Samples

`samples/phase3-density/density-{00,05,10}.mid` regenerated on the new engine
(seed 2024, `samples/phase3-density/checkerboard.png` = A-minor-pentatonic
high-contrast, 110 BPM, 8 bars, `--length 32 --loop-bars 8`, density 0/0.5/1.0).
Counts 37 / 103 / 136 (unchanged from Phase 3), but the density fill is now
melodic passing/neighbour tones. Command:
`lumena_demo checkerboard.png --seed 2024 --tempo 110 --mode phrased --length 32 --loop-bars 8 --density <d> --out <f>`.
WAV/MP3 still needs a soundfont on your end.

**Two fixtures, two jobs.** Density *auditions* live on a graded natural image —
`samples/phase3-density/Mona_Lisa.jpg` (detected C Minor) —
`mona-density-{015,025,035,050}.mid` at density 0.15/0.25/0.35/0.50, same seed
2024 / 8 bars. On a graded image the low end is genuinely distinct (37 / 38 / 51
/ 56 notes, all pairwise byte-distinct) because local contrast varies across the
canvas, so each density step subdivides progressively more regions. The
*checkerboard* stays the **determinism/regression fixture** (uniform max
contrast → integer subdivision steps → exact, reproducible note counts the tests
pin); it is a poor audition image precisely because that uniformity quantises the
low end (0.15 ≡ baseline, 0.25 ≡ 0.35). Keep them separate: the checkerboard
proves the maths, the Mona Lisa shows the feel.

## Where the next session picks up

- **Audition by ear** (the last gate item that needs you): do the three density
  clips read as melodic hooks now, and does the motif recur recognisably?
- Bump the parent `external/lumena` pointer to this branch's HEAD and land the
  re-baseline as an isolated parent commit.
- True tied-anticipation + the `localBeat_`/`harmonyBeat_` two-clock unify
  remain Phase 4 (see anticipation flag above).
- Contour is currently three shapes selected by a single start-cell gradient; a
  richer region-scan selection (and per-phrase contrast between A and B) is a
  reasonable future refinement.

---

# Follow-on task — dynamics smoothing (anti-whiplash)

*Separate from Phase 3 (which is done/accepted). Velocity/dynamics only — no
pitch or rhythm change.*

**Problem:** phrase dynamics stabbed note-to-note (fff → mp → f → p almost every
beat). Root cause in `applyPhraseDynamics`: the smooth phrase arc (weight 0.7)
was blended with each note's *raw* cell brightness (weight 0.3), and the random
walk visits bright/dark cells back to back, so the brightness term whipped the
velocity up and down every note. Present in the baseline, independent of density.

**Fix (`7cdb24a`, velocity only):**
1. Low-pass the brightness tint across the phrase (one-pole, `kBrightnessInertia
   = 0.6`) so the image colours the dynamics as a slow drift, not a per-note stab.
2. Slew-cap the note-to-note velocity change to `kMaxDynStep = 12` *within* a
   phrase, so it swells and settles while the arc's own rise/fall still shows.
   Cross-phrase boundaries are left uncapped on purpose — each phrase keeps its
   own fresh dynamic arc (and a rest sits between them ~60% of the time).

**RNG / pitch / rhythm untouched:** the new code draws no RNG (the single
peak-jitter draw is preserved verbatim), so melodies are otherwise identical.
Verified against `ad84373` across 160 cases (2 images × 2 density × 40 seeds):
**0 pitch/rhythm/RNG mismatches, velocity differs everywhere.**

**Measured note-to-note velocity jump** (gradient image, 200 seeds, baseline
density): median 15 → 13, **p90 27 → 14**, **jumps ≥ 20: 32% → 4%**, dynamic
range preserved. Within-phrase jumps are hard-capped at 12 (test-proven). The
whole-melody *max* improves less (47 → 40) because that is a cross-phrase
boundary, which is intentionally not capped.

**Test:** `test_phrased_dynamics_are_smooth` — within each phrase (via
`phraseStarts`), consecutive |Δvelocity| ≤ `kMaxDynStep` at Energy 0.5 (where the
energy scale is exactly 1.0), plus a dynamic-range floor so it can't pass by
going flat. Full suite: **17809/17809 green.**

**Before/after samples:** the three `samples/phase3-density/density-*.mid` clips
(same seed 2024, same notes) were regenerated with the smooth dynamics; the
pre-fix versions are kept alongside in
`samples/phase3-density/pre-dynamics-smoothing/` for a direct A/B by ear (same
notes, only velocities differ — e.g. baseline clip range 114 → 100 peak, the
per-beat stabs gone). WAV/MP3 still needs a soundfont on your end (none installed
here).

---

# Phase 3 session notes — image-driven bar-relative rhythm/density

Branch `feature/image-contour`. Baseline was `31d2c21` (Phase-2 pitch-blend
spike). Three commits added on top:

- `df3f371` Stage 1: bar-position clock derived from the real timeline
- `875fa70` Stage 2: image contour → pre-flatten density hook
- (this commit) Stage 3: verification pass + permanent regression tests

All work is confined to the Phrased-mode path in `src/melody/MelodyGenerator.cpp`
(+ one new `MelodyOptions` field in the header). Freeform, Arp, and Chord paths
are untouched.

## What changed

### Stage 1 — bar clock (`barAlignedDuration`)
The old rhythm cursor (`rhythmCursor_`) walked one monotonic index through the
session groove: it never reset at bar lines, ignored rests/ornaments, and was
not advanced by copied A′ phrases, so the "one-bar groove" drifted out of
alignment. Replaced the *emitted* duration source with `barAlignedDuration(beat,
template)`, which tiles the groove against each note's **real** start beat (the
flatten-loop accumulator, which already folds in rests and ornaments) using the
same `lround(beat*960)` quantization as strong-beat detection. The template now
restarts on every downbeat and re-aligns after any offset.

Inter-phrase rests are snapped to the half-beat grid so phrase starts stay on
groove boundaries and everything lands on integer 960-ticks.

### Stage 2 — density (`applyImageDensity` + flatten split)
New `MelodyOptions::imageRhythmAmount` (in `[0,1]`, **default 0**). Above 0, each
templated note's `localContrast` (brightness range over its 8-neighbour cell
window, wrapped) scales to 1–4 equal subdivisions of its bar-aligned length.
Busy/edgy image regions get denser rhythm; flat regions stay long. The split is
applied only while pieces stay exact integer 960-ticks and no finer than 1/64.

## Assumptions / decisions (reversible)

1. **Kept `rhythmCursor_`/`nextDuration()` as the pass-1 walk clock.** The walk
   still advances `localBeat_`/`harmonyBeat_` by the cursor durations, so the
   pass-1 pitch trajectory (and the whole RNG stream) is byte-identical to
   baseline. Only the *emitted* durations moved to the bar clock. Consequence:
   Phrased **pitches** shift slightly vs `31d2c21` because pass 2 re-evaluates
   strong-beat chord snaps against the new bar-aligned timeline. This is the
   intended Phase-3 behavioural change; no test pins Phrased pitches. Fully
   ripping out the two-clock split is the deeper bug-4b work, **deferred to
   Phase 4** (per the roadmap close-out).

2. **Density defaults OFF (`imageRhythmAmount = 0`).** This makes Stage 2 a
   byte-exact no-op on all existing callers/tests and keeps the RNG stream
   identical, so the plugin can opt in later. **Follow-up:** wire this to a
   "Density"/"Image Rhythm" APVTS param in the host plugin (parent Lumen repo).
   The parent still compiles unchanged because the field is defaulted.

3. **Subdivisions are unison rhythmic repeats**, marked non-eligible so pass 2
   and the diagnostics skip them (they keep the parent note's pitch). Total time
   is conserved (a note of length D becomes N notes of D/N).

4. **Rests snapped to the 0.5 grid** (were scaled by an energy factor to
   arbitrary values). Musically negligible, keeps the whole timeline on-grid.
   `test_phrased_rests_between_phrases` still passes (min rest 0.5 > 0).

5. **Density maps contrast→1..4 pieces** because 2/3/4 all divide every groove
   length (0.5/1.0/1.5/2.0 beats = 480/960/1440/1920 ticks) cleanly on the 960
   grid. Merging low-contrast notes into *longer* ones (the other half of
   "smooth = longer") was **not** implemented — flat regions simply stay at the
   groove length. A reasonable future refinement.

## Verification (Stage 3) — all PASS

Run via `LumenaTests` (permanent) plus a throwaway harness
(`scratchpad/verify.cpp`, not committed):

| Check | Result |
|-------|--------|
| Grid alignment (5 energies × 4 amounts × 3 images × 50 seeds = 3000 melodies) | 0 off-grid notes |
| Total length == whole bars, ≥ loopBars (960 cases, loopBars ∈ {1,2,4,8}, density on/off) | 0 wrong |
| RNG parity — density draws 0 RNG (post-gen mt19937 state identical across amount 0/0.5/1.0, 540 cases) | 0 diverged |
| Determinism — same seed ⇒ same MIDI (180 cases) | 0 nondeterministic |
| Density fires on busy image (checkerboard) | 60/60 seeds denser |
| **Baseline diff** vs `31d2c21` (built in a worktree): Freeform byte-for-byte; Phrased post-gen RNG state | **identical** |

Permanent regression tests added to `MelodyGeneratorTests.cpp`:
`test_phrased_image_density` (on-grid, in-scale, time-conserving, flat=no-op,
busy=denser) and `test_image_density_draws_no_rng` (seed stream intact).
Full suite: **15321/15321 green.**

## Build / test (WSL, headless)

```
cmake --build build-wsl --target LumenaTests
./build-wsl/bin/LumenaTests
```

(g++ 15 emits one pre-existing `-Wfree-nonheap-object` false positive from
`varyMotif`'s inlined vector copy — documented at that call site, not from Phase
3, and not an error under the WSL flags.)

## Plugin wiring (Density param) — added after Stage 3

`imageRhythmAmount` is now a real plugin parameter in the parent Lumen repo
(these changes live in the PARENT, not this submodule):

- `Source/State/Parameters.h` — new id `melodyDensity`; `kStateVersion` bumped
  2 → 3 (inert — no migration logic keys off it; per repo convention a melody
  param change bumps it).
- `Source/State/Parameters.cpp` — `FloatParam "Melody Density"`, **range 0..1
  (`unitRange`), default 0.0**, `unitAttr` — identical to the other four macros
  (Energy/Complexity/Image Influence/Repetition). No audio-rate smoothing: like
  the sibling macros it is read once at `generate()` time, not in `processBlock`.
- `Source/Melody/MelodyController.cpp` — `optionsFromParams` maps
  `melodyDensity → MelodyOptions::imageRhythmAmount`.
- `Tools/Tests/TestsMain.cpp` — new `MelodyDensityWiringTest` (category "Melody"):
  drives the real `MelodyController::generate()` on a high-contrast test image and
  checks Density 0 defaults/no-ops deterministically, Density 1 emits strictly
  more notes, and returning to 0 restores the baseline count.
- `CMakeLists.txt` — `lumen_tests` now compiles `MelodyState.cpp`,
  `MelodyPlayer.cpp` and the `MelodyController.cpp` bridge and links
  `Lumena::Lumena` (the bridge stays out of the strict `/W4 /WX` pass, mirroring
  the plugin target).

No-op guarantee survives the wiring: default 0 ⇒ `imageRhythmAmount = 0` ⇒ the
engine's byte-exact groove-only path. Existing patches (which never set
`melodyDensity`) load it at its 0 default, so they are unchanged.

**Verification:** parent `lumen_tests` (built headless under WSL via `build-linux`)
— the new wiring test passes; the only failing test is the pre-existing
`wavetable SHA-256 matches the pinned reference` (a Lens/wavetable golden pinned
on the Windows toolchain; Linux FP differs — unrelated to this work, untouched).

### Follow-up NOT done here (needs the Windows GUI toolchain)
- **UI knob in `Source/UI/MelodyPanel.cpp`.** The four macro knobs share one row
  split into fixed widths (`resized()`, ~L405-408); adding a "DENSITY" knob means
  re-dividing that row 4 → 5 and fitting a longer label. CLAUDE.md requires a
  `Lumen.exe --screenshot` inspection after any UI change, which can't run under
  WSL, so I did not ship unverified layout. The parameter is still fully usable at
  runtime via host automation and is persisted in state/presets. Adding the knob
  is a small, well-scoped Phase-5 task (declare `densityKnob`, construct like the
  others with label "DENSITY", change `kw` to width/5, add one `setBounds`).

## Before/after samples (Phase 3 density)

Short 8-bar MIDI clips, **same seed (2024)**, same checkerboard (high-contrast)
image, 110 BPM, A-minor-pentatonic — only Density changes, everything else fixed.
Saved to `C:\Users\pixel\Projects\lumen\samples\phase3-density\`
(`/mnt/c/Users/pixel/Projects/lumen/samples/phase3-density/`):

| File | Density | Notes / 8 bars | Demonstrates |
|------|---------|----------------|--------------|
| `density-00-baseline.mid` | 0.0 | 37 | Groove-only baseline (feature off) |
| `density-05-mid.mid`      | 0.5 | 103 | Busy regions start subdividing |
| `density-10-high.mid`     | 1.0 | 136 | Full image-driven density |

All three are exactly 8 bars — density subdivides within the bar grid, it does
not change the length. **WAV/MP3 rendering:** no soundfont/renderer is installed
in this environment, so only the MIDI clips are provided; render them through any
soundfont (e.g. `fluidsynth -F out.wav sf2 density-*.mid`) on your end to audition.

## Where the next session picks up

- Phase 3 remaining roadmap items **not** done here: multi-bar (2/4-bar) +
  pickup/cadence templates; region-level phrase contour (rising brightness ⇒
  phrase ascends); folding `style + energy + complexity + image detail` into one
  template chooser. `pickRhythmTemplate` and `barAlignedDuration` are the hooks —
  `barAlignedDuration` already tiles by the template's own period, so a longer
  (multi-bar) template drops in without further timeline changes.
- Wire `imageRhythmAmount` into the plugin UI (Phase 5 surface).
- Bug-4b (unify the `localBeat_`/`harmonyBeat_` walk clock with the real beat so
  pass 1 needs no pass-2 reconciliation) remains deferred to Phase 4.
