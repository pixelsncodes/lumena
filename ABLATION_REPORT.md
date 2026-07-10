# Variant C ablation — which component hurts? · Build report

Commissioned after the human ear test on `auditions/` **rejected C-full** (sounded
worse than baseline; location unspecified). Job: isolate the harmful component and
deliver scored, audition-ready alternatives. **Nothing is merged, no winner is
picked, W stays 2, C-2 was not modified or "fixed", Phase 4.5 stays closed, the
parent repo is untouched.**

## 1. The four arms

| arm | what | code state | branch / commits | suite |
|---|---|---|---|---|
| baseline | Variant B tip | tag `pre-variant-c` (`dae2647`) | (tag, read-only) | 22524/22524 |
| ARM-2 | **C-1 only** (related-region read window, W=2) | tag + C-1 | `ablation/arm2-c1`: `2a77418`(S-1) `88037a1`(C-1) `322b018`(S-4) | 22524/22524 |
| ARM-1 | **C-1 + C-3** (drop the C-2 contour mirror) | tag + C-1 + C-3 | `ablation/arm1-c1c3`: `84d300b`(S-1) `c1fb164`(C-1) `ee165e3`(C-3+re-baseline) `c3162d6`(S-4) | 22526/22526 |
| C-full | C-1 + C-2(+fix) + C-3 | `feature/bphrase-variant-c` (`1fb1540`) | (existing, unchanged) | 22526/22526 |

Both arm branches are **off tag `pre-variant-c`**, built by cherry-picking the
already-reviewed Variant C commits verbatim (no new engine code was written).
S-1/S-4 are the read-only scoreboard commits, needed on every arm for the metric
sweep; their G4 no-op property was re-proven here (see §2). ARM-1's suite is 22526
because it carries C-3's role-aware test re-baseline exactly as originally
committed (B endings pinned open); ARM-2 runs the original untouched 22524 suite —
C-1 alone passes it, as it did when `ef355eb` first landed.

**Construction proofs (no hand-merge trusted blindly):**
- ARM-1 tree ≡ C-full engine tip (`1a624f5`) with both C-2 commits
  (`1a624f5`, `0158612`) reverted — `git diff` over `src`/`tests`/`apps` is empty.
- ARM-2 `src/` ≡ the C-1 commit (`ef355eb`) exactly; `tests/` ≡ the tag.
- All three constructed branches carry the demo app byte-identical to `92e8ced`.

## 2. Gates — all four arms PASSED (nothing dropped, no second attempts needed)

| gate | baseline(+S1/S4) | ARM-2 | ARM-1 | C-full |
|---|---|---|---|---|
| Full suite green | 22524 ✓ | 22524 ✓ | 22526 ✓ | 22526 ✓ |
| Determinism (ck seed 2024 ×2, MIDI+dump byte-identical) | ✓ | ✓ | ✓ | ✓ |
| Stream-exactness vs tag, `--arp 0`, 61 seeds × {ck, ml}: onsets/durations/velocities byte-identical, pitch-only | ✓ (0 diffs of any kind — full G4) | ✓ 122/122 files | ✓ 122/122 files | ✓ 122/122 files (re-proven) |
| 60-seed sweep tripwire (dist(B) < dist(A′)) | 1/60 (seed 20 — **tag-native**, see §6) | **0/60** | **0/60** | 0/60 |

Builds: WSL gcc Release, one worktree per arm; protocol locked by regenerating the
committed `auditions/mona-seed*-variantC.mid` from the C-full build — **byte-identical**
(`--mode phrased --length 32 --loop-bars 8 --tempo 110`, Mona Lisa fixture).

## 3. Four-arm scoreboard (60 seeds, Mona Lisa, plan §3 protocol)

Means over seeds 1–60. Baseline column = tag `pre-variant-c` scored through the
G4-proven `ablation/baseline-metrics` build (`817b64b`). Baseline/ARM-2/C-full
columns reproduce the prior report's Variant-B / C-1-only / Variant-C columns
exactly — the two sessions' measurements cross-validate.

| metric | baseline | ARM-2 (C1) | ARM-1 (C1+C3) | C-full |
|---|---|---|---|---|
| M1 front (bars 2–4 vs 1) † | 0.572 | 0.584 | 0.603 | 0.603 |
| M1 back (bars 5–8 vs 1) † | 0.713 | 0.713 | 0.716 | 0.723 |
| M1 dist(A′) † | 0.182 | 0.176 | 0.176 | 0.176 |
| M1 dist(B) † | 0.770 | 0.767 | 0.786 | 0.804 |
| M2 band excursion (st) ‡ | 4.45 | 4.70 | 4.74 | 4.60 |
| **reg dist(B) (st)** — the C-1 axis | 5.68 | 4.39 | **4.35** | 4.63 |
| reg dist(A′) (st) | 2.10 | 2.09 | 2.09 | 2.09 |
| **M3 Δentropy (back−front)** | +0.066 | +0.077 | +0.063 | **+0.019** |
| over-coherence tripwire | 1/60 (tag-native) | 0/60 | 0/60 | 0/60 |

† M1 is **known-flawed for contour work** (interval-class similarity penalises the
deliberate C-2/C-3 changes by design) — reported per instructions, **not gated on**.
‡ M2's front-half band contains an early B phrase, masking register effects; the
per-phrase `reg dist` decomposition (S-4) is the trustworthy register view.

## 4. Per-arm diff footprints (all vs tag `pre-variant-c`)

**Audition seeds (Mona Lisa, default settings — pitches changed / notes):**

| seed | ARM-2 | ARM-1 | C-full |
|---|---|---|---|
| 30 | 10/39 | 12/39 | 15/39 |
| 2024 | **1/37** | 3/37 | 10/37 |
| 58 | 8/39 | 9/39 | 9/39 |

All nine pairs are clean (timing/velocity byte-identical). Note ARM-2's seed-2024
footprint is a single pitch (idx 11, C4→D#4) — on that seed ARM-2 is a near-A/B of
one note; seeds 30 and 58 carry its real footprint. ARM-1 = ARM-2's diffs plus the
C-3 cadence pitches (seed 2024: idx 14 F4→G4, idx 26 C4→D4 — the degree-2 "question"
landing at beat 19.50 that the returning A answers). C-full differs from the arms
*inside* B phrases (the C-2 mirror), e.g. seed 58 idx 15–17: arms land G#4/C5/D5,
C-full lands C5/D#5/F5.

**61-seed `--arp 0` sweep (mean pitches changed per seed):**

| fixture | ARM-2 | ARM-1 | C-full |
|---|---|---|---|
| checkerboard | 5.16 (max 11) | 6.44 (max 11) | 6.02 (max 12) |
| Mona Lisa | 6.89 (max 15) | 8.00 (max 15) | 9.38 (max 14) |

## 5. maybeOrnament rider (default ornaments, vs tag, 61 seeds × both fixtures)

The known pre-existing pitch→draw coupling (`maybeOrnament` `:564-568`, frozen,
untouched, Phase 4.5 scope). Seeds whose rhythm shifts when ornaments are on:

| arm | checkerboard | Mona Lisa | total |
|---|---|---|---|
| ARM-2 (C1) | 9/61 = 14.8% — {2, 4, 21, 23, 42*, 44*, 49, 52, 2024} | 6/61 = 9.8% — {4, 6, 14, 34, 38, 48} | **15/122 = 12.3%** |
| ARM-1 (C1+C3) | 9/61 = 14.8% — same set as ARM-2 | 6/61 = 9.8% — same set as ARM-2 | **15/122 = 12.3%** |
| C-full | 5/61 = 8.2% — {9, 10, 42, 52, 2024} | 7/61 = 11.5% — {6, 14, 34*, 37, 38, 42, 48} | **12/122 = 9.8%** |

\* note-count changes (an ornament figure adds/removes notes); the rest are
timing/velocity shifts only. ARM-1 and ARM-2 trip **identical seed sets with
identical diff counts** — C-3's cadence pitches never cross an ornament guard on
these fixtures, so the rider belongs entirely to C-1's material change. C-full's
set differs (C-2 moves different pitches across the guards) and is slightly
smaller by luck, not by construction; all three sit in the plan's ~10% re-baseline
class. Checkerboard seed 2024 trips on every arm (as it did for C-full last
session); **Mona Lisa seeds 30/2024/58 are clean on every arm — no audition
substitutions were needed.**

## 6. Read on the numbers (not a verdict — the ear owns that)

- **Dropping C-2 costs nothing on the axis Variant C was built for.** B-phrase
  register relatedness: ARM-1 4.35 / ARM-2 4.39 vs C-full 4.63 (baseline 5.68).
  Even after its mean-pivot fix, the C-2 mirror gives back ~0.3 st of C-1's gain.
- **C-2 is the only component that moves M3** (+0.066 → +0.019 collapse; without
  it, both arms sit at baseline-level entropy inflation, ARM-1 +0.063, ARM-2
  +0.077). Since the ear rejected exactly the arm with the collapsed M3, the
  "back half no more chaotic than the front" signature evidently does not read as
  *better* — consistent with C-2 being the suspect component: it is also the only
  component that rewrites B-phrase *interiors* wholesale rather than where B reads
  or how B ends.
- **Between the arms:** the numbers separate them barely — ARM-1 edges ARM-2 on
  reg dist(B) (4.35 vs 4.39) and M3 (+0.063 vs +0.077), and it carries C-3's
  half-cadence function, which no metric here can see but which was the most
  musically-legible part of the plan (B ends on a question). ARM-2 is the smaller,
  purer intervention (smallest footprint, original 22524 suite untouched). If the
  numbers must favor one: **ARM-1, narrowly** — it keeps everything measurable
  that C-full achieved except the M3 collapse, sheds the component that both
  costs register relatedness and is the prime suspect for the failed ear test,
  and its per-arm cost (identical rider set to ARM-2, +1–2 pitches per seed) is
  marginal. The ear test on `auditions/ablation/` decides.
- Caveat, stated plainly: the ear verdict was on C-full as a whole, with no
  location given. If the dislike traces to C-1's register pull or C-3's open
  endings rather than C-2, both arms will fail the same way — that is exactly
  what the 12-file grid is for (ARM-2 isolates C-1; ARM-1 adds only C-3 to it).

## 7. Auditions — `auditions/ablation/` (12 files)

`mona-seed{30,2024,58}-{baseline,arm2-c1,arm1-c1c3,cfull}.mid` — Mona Lisa,
110 BPM, 8 bars, C minor, default settings. Every non-baseline file is a clean
pitch-only pair against its same-seed baseline (identical rhythm and dynamics);
per-note diff logs in §4. Listen per seed in the order baseline → arm2 → arm1 →
cfull: does the middle third relate without sounding mirrored/mechanical, and
does B's ending read as a question the returning hook answers?

**Reference note:** the baselines here are the **tag `pre-variant-c`** (the
commissioned ablation baseline, Variant B included) and therefore differ by
Variant B's few reined pitches from the older `auditions/mona-seed*-baseline.mid`,
which pinned `a9852a5` (prior report, deviation #2). The `-cfull` files are
byte-identical to the committed `auditions/mona-seed*-variantC.mid`.

## 8. Deviations / notes (complete list)

1. **Baseline tripwire 1/60 (seed 20)** — `dist(B) < dist(A′)` fires on the *tag
   itself*, before any C code. Tag-native behavior, not an arm effect (all arms:
   0/60). Recorded because the prior report only quoted arm tripwires.
2. **S-1/S-4 cherry-picked onto every measured arm** (including baseline) so the
   sweep could run; G4 re-proven at full width for the baseline arm (122/122
   dumps byte-identical to the tag build, pitches included).
3. ARM-1's C-3 cherry-pick auto-merged in `MelodyGenerator.cpp`; verified against
   an independent construction (revert of both C-2 commits from `1a624f5`) —
   byte-identical trees (§1).
4. No gate failed on any arm; the drop-after-two-failures clause was never
   invoked. No seed substitutions were needed for the auditions (§5).
5. RNG stream untouched, W untouched (2), C-2 untouched (present in C-full
   exactly as rejected, absent from both arms), no test weakened (ARM-2 runs the
   original suite; ARM-1 carries the original C-3 re-baseline verbatim).

---

## CLOSING ADDENDUM — verdict and merge (this closes the report)

**Ear verdict on the §7 grid: ARM-1 (C1+C3) audibly ≈ baseline, not worse; the
register gain stands. C-2 (the contour mirror) is REJECTED, permanently.**

- **ARM-1 is MERGED**: `ablation/arm1-c1c3` (tip `c3162d6`) → the primary work
  branch `feature/motif-phrasing`, merge commit `c10a31e` (no squash, no
  rebase; merged tree verified byte-identical to `c3162d6`). Gates re-run at
  the merged tip: suite 22526/22526 green, checkerboard seed 2024 ×2
  byte-identical, outputs byte-match the gated ARM-1 artifacts.
- The merge is a **deliberate pitch re-baseline** carrying the documented
  maybeOrnament rider (~12.3% of seeds shift rhythm; pre-existing coupling,
  re-baseline scope, cured in Phase 4.5 — the coupling itself untouched).
- **C-2 stays unmerged** on `feature/bphrase-variant-c` as history; no branch
  or tag was deleted. ARM-2 (`ablation/arm2-c1`) likewise remains as history.
- The b-phrase wander item is reclassified **REDUCED, NOT CLOSED** (B reads
  related material and ends open; per-bar motif recall unchanged).
- The 12-file audition grid itself is committed at `ec1d4c2` on
  `feature/bphrase-variant-c` (`auditions/ablation/`); this branch carries the
  report (and `VARIANT_C_REPORT.md`) so the merge message's references resolve.
