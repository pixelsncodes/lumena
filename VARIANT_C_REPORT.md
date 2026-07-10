# Variant C — B-phrase coherence fix · Build report

Branch `feature/bphrase-variant-c` (7 commits) off tag `pre-variant-c` (= `dae2647`,
the Variant B tip). Executed per `LUMENA_BPHRASE_FIX_PLAN.md` T-1 → S-4. **Status:
built, gated, scored; STOPPED at the ear test as commissioned.** Nothing merged;
parent repo untouched.

Commits, all on the submodule branch:

| commit | what |
|---|---|
| `e15e996` | T-1 pre-code trace (docs only) |
| `7c876f4` | S-1 coherence scoreboard M1/M2/M3 + `--sweep` (read-only, G4-verified) |
| `ef355eb` | C-1 related-region teleport (shadow-material form) |
| `0158612` | C-2 contour-contrast contract (flip form) |
| `140e007` | C-3 open-ended B cadence + role-aware test re-baseline |
| `92e8ced` | S-4 diagnostic: per-phrase register columns (read-only, G4-verified) |
| `1a624f5` | C-2 fix: reflection pivots on region mean (register-neutral contrast) |

---

## 1. T-1 answer (pre-code trace)

**One phrase form spans the whole loop — there is no independent 4-bar tiling —
so C-5 (period rule) was correctly NOT built.** `generatePhrased`
(`MelodyGenerator.cpp:1165+` at the tag) generates motif A exactly once (phrase 0),
then one body loop alternates A-family slots (interior odd positions: verbatim
repeat or `varyMotif(motif)` — always re-varied from the ORIGINAL motif, so
A-family drift does not accumulate) with B slots (interior even positions), until
a NOTE-COUNT target is met; then one closing phrase. `padToWholeBars` only pads
trailing silence. The 8-bar wander is therefore **per-B-teleport**: with the demo
protocol the body holds 3–4 separate B phrases, each drawing its own uniform cell
(`:369/:370`) and fresh contour — all through the single `newRegion` branch that
C-1 hooks. Full draw-site map in `SESSION_NOTES.md` (T-1 section).

## 2. What was built (and the one plan-invalidating discovery)

### The discovery: the plan's literal C-1/C-2 mechanism cannot hold the stream
`stepNote` carries a **conditional** stream-identity draw —
`if (|gradient| > 0.02f) { (void)uni01(); }` — whose firing depends on the
brightness gradients along the **cell path**. The plan's C-1 ("remap the drawn
cell", i.e. move the walk) and C-2 ("make B's contour class a function of A's",
which re-steers the walk) both change the cells visited, so they fire that draw
differently and shift the whole downstream stream. Measured, not hypothesized:
the literal C-1 remap shifted onsets on **~50/60 seeds with ornaments off**. That
first attempt was discarded pre-commit (G3 did its job).

### The stream-exact form actually built
- **C-1 (`ef355eb`)** — teleport draws kept verbatim; the drawn cell is remapped
  post-draw into the (2W+1)² window centred on motif A's anchor (W=2 on the
  16-col grid, scaled for 8×8), and the remap moves **where B's brightness→degree
  blend reads its material** (a shadow offset inside `stepNote`), not the walk.
  `nextBiased` consumes exactly one draw whatever its target, so the stream is
  aligned **by construction**, and verified: 61 seeds × both fixtures at
  `--arp 0` → onsets, durations, velocities all byte-identical, pitch-only diffs.
- **C-2 (`0158612` + fix `1a624f5`)** — deterministic contrast contract
  (Rise→Fall, Fall→Rise, Arch→inverted arch) realised on the same shadow
  material: when `selectContour` at B's shadow start (RNG-free) equals motif A's
  class, the readout is mirrored. The first realisation (plain `1-b`) contrasted
  direction but also **inverted register**, measurably undoing C-1 (reg_b 4.39 →
  6.10); the fix reflects around the related window's **mean brightness**, so
  direction inverts and register stays anchored. Draw-free throughout.
- **C-3 (`140e007`)** — B's final note snapped (pitch-only, draw-free, post-walk)
  to the nearest degree with pitch class 2 or 7 above the root (tie-break to the
  fifth). Half-cadence function: B opens, the A-family return answers. Closing
  phrase's 4c closed cadence untouched. Builder Markov state deliberately not
  moved; the ending note is snap-ineligible in pass 2 so the edit sticks.
- **C-4** — Variant B's register clamps inherited unchanged (branch built on
  `feature/bphrase-register-rein`).
- **C-5** — not built; T-1 condition (independent tiling) not met.

## 3. Three-arm scoreboard (60 seeds, Mona Lisa fixture, plan §3 protocol)

`--mode phrased --length 32 --loop-bars 8`, density 0, default influence 0.25.
Arms: baseline = `a9852a5` (pre-Variant-B accepted tip), Variant B = tag
`pre-variant-c`, Variant C = `1a624f5`. (A C-1-only arm was scored as a
diagnostic.) Means over 60 seeds:

| metric | baseline | Variant B | C-1 only | Variant C |
|---|---|---|---|---|
| M1 front (bars 2–4 vs 1) | 0.582 | 0.572 | 0.584 | 0.603 |
| M1 back (bars 5–8 vs 1) | 0.723 | 0.713 | 0.713 | 0.723 |
| M1 dist(A′) | 0.191 | 0.182 | 0.176 | 0.176 |
| M1 dist(B) | 0.775 | 0.770 | 0.767 | 0.804 |
| M2 band excursion (st) | 4.41 | 4.45 | 4.70 | 4.60 |
| M3 Δentropy (back−front) | +0.060 | +0.066 | +0.078 | **+0.019** |
| reg dist(A′) (st) | 2.85 | 2.10 | 2.09 | 2.09 |
| reg dist(B) (st) | 5.69 | 5.68 | **4.39** | **4.63** |

`reg dist(X)` = mean |phrase pitch centroid − motif A centroid| (S-4 diagnostic
commit; M2's front-half band itself contains an early B phrase, so it partially
masks register effects that the per-phrase decomposition shows directly).

**S-2 validation gate (PASSED):** baseline shows the wander numerically — M1
back 0.723 ≫ front 0.582 (41/60 seeds), dist(B) 0.775 ≫ dist(A′) 0.191, M3
positive (+0.060 mean and median). **Q1 answered with numbers:** Variant B moves
M1/M3 nowhere and its whole effect is register — and only on the A-family
(reg dist(A′) 2.85 → 2.10); B-phrase register is untouched (5.69 → 5.68).
Register-rein alone is insufficient, as the plan predicted.

**S-4 verdict, honestly:**
- **Moved as intended:** B-phrase register relatedness — the C-1 axis — improves
  19% vs baseline (5.69 → 4.63 st) while keeping Variant B's A-family gain
  (2.09). M3 back-half entropy inflation collapses toward the plan's "≈ 0"
  signature (+0.060 → +0.019). M1 back/front ratio 1.20, within the plan's
  ~1.5× bound. **Over-coherence tripwire: 0/60 seeds** (dist(B) 0.804 never
  falls below dist(A′) 0.176) — no oversmoothing, so no W retune was needed or
  performed (W stays 2).
- **Did not move:** per-bar M1 recall (0.723 → 0.723). Expected in hindsight:
  M1 measures interval-sequence similarity, C-2 *deliberately contrasts* B's
  contour (dist(B) rising to 0.804 is the contract working, not a regression),
  and at default influence 0.25 the blend caps how far any material change can
  pull the Markov walk.
- Net: C delivers **related register + deliberate contour contrast + open
  cadence** — coherence through relationship, not similarity. Whether that
  reads as "the wander is gone" is exactly what the ear test is for.

## 4. Gates

- **G1** — suite green after every commit. Totals: 22524 → **22526** from
  `140e007` (the +2 are NEW checks pinning C-3; see re-baselines).
- **G2** — checkerboard (and Mona) seed 2024 regenerated twice at every step:
  byte-identical every time.
- **G3** — see §5/§6. Stream identity vs the tag is **proven** at `--arp 0`:
  61 seeds × both fixtures, onsets/durations/velocities byte-identical,
  pitch-only diffs. With ornaments on (default), a subset of seeds shifts
  rhythm through the **pre-existing** `maybeOrnament` pitch→draw coupling
  (`:564-568`): checkerboard {9, 10, 42, 52, **2024**}, Mona {6, 14, 34, 37,
  38, 42, 48} of seeds 1–60+2024. **Mona Lisa seed 2024 — the audition
  deliverable — is fully clean** (0 timing, 0 velocity diffs).
- **G4** — both read-only metric commits verified: generated MIDI byte-identical
  vs the adjacent engine state (120 files for S-1; spot-verified for the S-4
  columns commit).

## 5. G3 on checkerboard seed 2024 — the one gate deviation, called out plainly

The commissioning gate pinned "checkerboard seed 2024: onsets/durations/
velocities byte-identical." That holds for every C commit **with the ornament
path inert**, but with default ornaments the seed's rhythm shifts (37 notes,
17 timing diffs, one figure changes shape). Root cause proven by isolation:
`maybeOrnament` draws `dirPick`/`shape` behind pitch-dependent guards
(`ascOk`/`descOk` on `src.degree`, `:564-568`); C changes B-phrase degrees, an
ornament site crosses the guard, and the draw sequence after it shifts. This is
the documented, **pre-existing** engine coupling (same family as bug-4 /
pitch-feeds-stream; scheduled for Phase 4.5) that the fix plan explicitly
classifies as "re-baseline scope, not failures" (~5% of seeds for Variant B —
~8–11% under C because C touches more pitches; the coupling itself is frozen,
untouched, per the no-go zones).

Strictly read, the gate text fails on this seed; per the plan's own
classification (and the arp-0 proof that MY draws are stream-exact), the work
proceeded with the rider documented rather than stopping. If the coordinator
reads the gate strictly instead: everything needed for that verdict is in this
report, and nothing was merged. The G3 revert-and-rework clause **was**
exercised once for a real perturbation (the literal C-1 cell remap, §2), which
is what the gate exists to catch.

**Checkerboard 2024 pitch-diff log (stream-clean form, `--arp 0`, tag → C):**
35 → 35 notes, timing/velocity byte-identical, 2 pitch diffs — both B-phrase
cadences opening onto the fifth (E, pc 7 of A minor pentatonic; C-3):
```
idx 14 @ beat  9.50: A5 -> E5 (-5)
idx 24 @ beat 18.75: A4 -> E5 (+7)
```
(Default-mode 2024 additionally carries the ornament rider described above:
same 37-note count, one figure re-shapes eighths→triplet, downstream
onsets/velocities shift from idx 20 on. Full dumps retained in the session
scratchpad; regenerate with `--dump-notes` at tag vs tip to reproduce.)

## 6. Audition diff — Mona Lisa seed 2024, baseline `a9852a5` → Variant C

37 → 37 notes, onsets/durations/velocities byte-identical, **15 pitch diffs**
(3× Variant B's audible footprint on the same seed). The diff decomposes exactly
into the variant's parts: idx 15–19 are Variant B's five reined pitches (C-4,
inherited verbatim); the rest are C's B-phrase material/contract changes; note
idx 26's C4→D4 landing on degree 2 of C minor — a C-3 open cadence, audible as
the "question" before the A return.
```
idx 11 @  9.50: C4  -> D#4 (+3)     idx 19 @ 15.50: D#4 -> D4  (-1)  [B]
idx 12 @ 10.00: C4  -> D#4 (+3)     idx 20 @ 17.00: G#4 -> G4  (-1)
idx 13 @ 10.75: D#4 -> G#4 (+5)     idx 21 @ 17.50: G4  -> C4  (-7)
idx 14 @ 11.50: F4  -> G4  (+2)     idx 22 @ 18.00: D#4 -> C4  (-3)
idx 15 @ 13.00: F5  -> D#5 (-2) [B] idx 23 @ 18.33: G4  -> D#4 (-4)
idx 16 @ 13.50: G5  -> F5  (-2) [B] idx 24 @ 18.67: A#4 -> G4  (-3)
idx 17 @ 14.00: D#4 -> D4  (-1) [B] idx 26 @ 19.50: C4  -> D4  (+2)
idx 18 @ 14.75: D#4 -> D4  (-1) [B]
```
([B] = the Variant B component.)

## 7. Re-baseline commits

One, folded into `140e007` (C-3) because an interleaved test-only commit would
have been red at either side, violating G1-after-every-commit:
- `test_phrased_endings_resolve_to_chord_tone` — chord-tone ≥ 0.9 now asserted
  on **non-B** walked endings; **stricter new pin added**: every B ending must
  carry pitch class 2 or 7 (`bEndOpen == bEndTot`). B endings are open by design
  now; that IS C-3.
- `test_phrased_tracks_brightness_at_high_influence` — B-phrase notes excluded
  from the walk-cell reconstruction; they deliberately track the related shadow
  region (C-1/C-2). Contract unchanged and still asserted for all other walked
  notes.
Suite total 22524 → 22526; nothing deleted, nothing weakened — both tests kept
their original strength on the notes whose contract did not change and gained a
new explicit pin of the changed one.

## 8. Deviations from the plan / instructions (complete list)

1. **`LUMEN_LUMENA_ANALYSIS_BRIEF.md` does not exist** anywhere in either repo
   (tree, history, stash). Constraints were taken from the fix plan (which
   embeds the brief's §6 discipline), the STEP-0 trace in SESSION_NOTES, and the
   commissioning instructions. Same situation as STEP-0's missing "Diagnosis
   doc". Not treated as a blocker.
2. **"Baseline (pre-variant-c tag)" read as `a9852a5`.** The tag sits at the
   Variant B tip, so scoring "baseline" there would make the baseline and
   Variant B arms identical and the plan-§3 decomposition meaningless. The
   canary (G3) uses the tag exactly as written.
3. **C-1/C-2 realised in shadow-material form** rather than the plan's literal
   walk remap / contour re-steer — forced by the gradient-conditional
   stream-identity draw (§2). The plan's *intent* (B's material from a related
   region; deliberate contrast class; image keeps its hand) is preserved; its
   *stream-identity claim* only holds in this form.
4. **C-2 got one measured correction** (`1a624f5`, inversion → mean-centred
   reflection) after the register decomposition showed the first form undoing
   C-1. This was a fix of my own realisation's artifact, not a W iteration (the
   tripwire never fired; W stays 2, untouched).
5. **S-4 diagnostic columns** (`reg_aprime`/`reg_b`) added beyond the plan's
   literal M1–M3 — read-only, G4-verified, needed to decompose M2's band
   artifact and adjudicate the C-2 fix.
6. **G3 on checkerboard seed 2024, default ornaments** — see §5.
7. **Re-baseline folded into the C-3 commit** — see §7.

## 9. What the ear test should listen for (the single open gate)

`auditions/` (this repo): `mona-seed{2024,30,58}-{baseline,variantC}.mid`
(110 BPM, 8 bars, C minor). All three seeds are clean pitch-only pairs — same
rhythm, same dynamics, only pitches differ. Seeds 30/58 carry the largest clean
footprints (31/39 and 26/39 notes). Listen for: does the middle third still
teleport somewhere unrelated, or does B now sound like a question the returning
hook answers? Baseline seed-2024 hash `599b674d…` matches the STEP-0 reference.
No WAV/MP3 — no soundfont in this environment (`fluidsynth -F out.wav <sf2>
<mid>` on your end).

**Recommendation if the ear likes C:** merge is a pitch re-baseline accepting
the ornament-coupling rider on ~10% of seeds (vs Variant B's ~5%) — or
neutralise `maybeOrnament`'s pitch-dependence first in Phase 4.5, which shrinks
the rider class for every future pitch-domain change at the cost of one global
rhythm re-baseline. Not my call; both paths are open from this branch.
