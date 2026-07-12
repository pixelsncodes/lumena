# 2026-07-11 — Docs-only: GitHub math macros + AGENT_RULES golden wording

Two documentation fixes, no code/test/build changes. Both repos got a docs
commit; submodule pointer bumped in parent.

**Fix 1 — README math renders on GitHub (submodule).** GitHub's `$$` MathJax
rejects `\operatorname`. Replaced all 9 `\operatorname{X}` in the "How the
melody engine works: the math" section with `\mathrm{X}` (atan2, round×3,
clamp×5) — macro substitution only, zero changes to numbers, variables, or
equation content (verified by `--word-diff`: only the macro token differs on
each `$$` line). Audited every remaining backslash-macro in the section; all
are GitHub-permitted: `\Big \Delta \Pr \Rightarrow \begin \big \bmod \cdot
\cos \delta \end \frac \ge \iff \in \le \left \lfloor \longmapsto \lor
\mathcal \max \mid \min \not \overline \pm \prec \propto \qquad \quad \rfloor
\right \sin \sum \text \textbf \tfrac \theta \times \to` (plus the new
`\mathrm`).

**Fix 2 — AGENT_RULES wavetable-golden wording (parent).** Corrected the
standing rule: the wavetable SHA-256 golden diverges ONLY on Linux
(floating-point), is pre-existing, and may be ignored on Linux runs only — on
Windows it PASSES and counts. Called out that it is a SEPARATE test from the
seed-2024 melody-MIDI export hash (2e37a1d9…3670), which remains a hard gate
that must always genuinely pass. Prevents conflating the two.

---

# 2026-07-11 — PROMOTION TO MAIN (both repos), human-authorized

Promoted the fully-tested feature work to `main` on both repos, submodule-first.

**Human PM authorization (explicit, this session).** The human exercised the
acceptance gate AGENT_RULES reserves for them: "PROMOTION AUTHORIZED … the
merge-to-main decision — the acceptance gate AGENT_RULES reserves for me is
hereby exercised. All seven features and the math docs have been ear/eye-tested
and accepted by me across this project. You are authorized to perform the
feature->main merges in Steps 1-3 and the corresponding pushes as my delegate."
Parent write scope was limited by the PM to exactly (a) the submodule-pointer
commit and (b) the `--no-ff` merge of `feature/lumena-melody` into parent `main`
(plus this session-note pointer reconcile) — no other parent source writes.
I stopped and asked before any merge because AGENT_RULES (parent read-only;
"never merge / the human ear is the acceptance gate"); this promotion executes
the human's recorded acceptance, not agent initiative.

**Step 1 — submodule to main.** `main` (76ebc6c) fast-forwardable, `--no-ff`
merge of `feature/motif-phrasing` (6993d6f) -> merge commit **8d642e2** (tree
identical to feature tip). Engine suite on main: **24069/24069 checks, 0
failed**. Pushed `origin/main`.

**Step 2 — parent pointer reconcile.** On `feature/lumena-melody`, set submodule
to 8d642e2, commit "Point submodule at lumena main" (57b4545). Pointer contained
in `lumena/main` (verified).

**Step 3 — parent to main.** Parent `main` (ffefef7) was a STALE pre-melody
state (no submodule at all; it carried its own README tone-math doc PRs #1/#2).
`merge-tree --write-tree` predicted a CLEAN merge (feature never touched the
parent README, so main's tone-math docs are kept, melody source added — no
conflict). `--no-ff` merge -> parent `main` tip **febde36** (this note adds one
further pointer-reconcile commit on top). Submodule resolves to lumena/main tip.

**Gate battery on main — ALL GREEN:**
- Parent Windows suite (`lumen_tests.exe`): **ALL TESTS PASSED** (exit 0),
  including the Windows-pinned wavetable golden and all three seed-2024
  byte-identity sub-tests.
- Determinism: **61/61 seeds × checkerboard × 2 renders byte-identical** (MIDI +
  note-dump CSV); seed-2024 ×2 byte-identical (G2 minimum).
- **Seed-2024 melody export SHA-256 = 2e37a1d96d279464e7c7424a1c930070c3f405e4fb22eab54c748980c8e83670**
  — matches the pinned `2e37a1d9…3670` (DECISIONS.md octave-window entry) exactly.
- Windows VST3 + standalone built via WSLg (VS2022/x64, Release, exit 0).
- **pluginval --strictness-level 10: SUCCESS** (0 failures).
- `git submodule status` resolves to lumena/main tip, contained in lumena/main.

**Feature branches retained** (`feature/motif-phrasing`, `feature/lumena-melody`);
no tags touched; no force-push/history-rewrite.

**FLAG for a follow-up docs pass (not edited this session, per PM):** AGENT_RULES
lines 18-19 read: "The parent suite's failing wavetable SHA-256 golden
(Windows-pinned Lens hash) is pre-existing and unrelated. Ignore it." This can
mislead — it reads as if the golden *fails on Windows*. In fact, on the Windows
toolchain this run PASSED it (scan/spectral SHAs matched the pinned references);
the golden only diverges on **Linux** floating-point and is the ignorable item
there. Suggest rewording to "Linux-only wavetable SHA divergence (Windows-pinned
Lens hash; passes on Windows, fails on Linux FP)" so the two goldens (this
wavetable hash vs. the seed-2024 *melody* export hash, which must genuinely pass)
can't be conflated.

# 2026-07-11 — README 'the math': LaTeX escaping + F2 new-world wording (docs-only)

Two fixes to the recast math section, no math content changed (word-diff
confirms every `$$…$$` equation body identical apart from the line-break token
below; all 36 formulas, numbers, thresholds and file·function citations intact).

FIX 1 — LaTeX escaping. The two `\begin{cases}` blocks (C-6 contour, D-1
interval) used `\\[2pt]` line breaks; the optional `[2pt]` length argument
makes GitHub's math renderer dump the whole block as literal tokens. Replaced
`\\[2pt]` → plain `\\` (3 occurrences). Raw-byte audit first confirmed the rest
of the section was already clean: no control-char mangling, every `\,` `\!`
`\Big` `\operatorname` `\max` `\lfloor`/`\rfloor` `cases` correctly escaped —
so only `[2pt]` needed removal (numbers/vars untouched).

FIX 2 — new-world wording. F2 (pitch-timing firewall) justification was
old-world ("post-draw clamp or remap"); reworded to attribute determinism to
plan-then-walk ordering (all timing/structure draws consumed before any pitch
exists) and to describe the compass clamp / register folds / motif rein / C-1
window as "pitch-only and draw-free" (matching E-12's existing phrasing). The
invariant, the equation, and the `test_pitch_domain_never_shifts_timing`
citation are unchanged. Gate extends section-wide, so E-9's "post-draw remap"
was also moved to "pitch-only, draw-free remap". `post-draw` / `draw-count` now
appear NOWHERE in A–F (Historical blockquote keeps its own old-world wording, as
allowed).

No source/build/test files touched. Determinism/gates untouched (docs-only).

# 2026-07-11 — README 'the math' section recast to Lumen Scan-mode template (docs-only)

Reformat only — no math re-derived. The existing code-cited math section (8
numbered subsections, monospace code-block equations, Lens-prose register)
was recast into the parent Lumen README's Scan-mode house style: six labelled
subsections (A Image Analysis, B Key & Scale, C Contour & Density, D Markov
Selection, E The Clock, F Determinism), each an internally-numbered list of
bold steps, one plain sentence of context + one centred LaTeX display equation
per step, tuned constants named honestly in-line. Opened with a 2–3 sentence
plain-language paragraph for non-technical readers.

Formula set preserved exactly: all 36 formulas carried over 1:1 with their
file·function citations; none dropped, none added. Re-bucketing 8→6: old §5
(harmonic frame: progression, per-bar triad spelling, strong-beat chord snap)
and old §6 (phrase form: form loop, motif rein, cadence, C-1, C-3) both folded
into **E. The Clock**, which the brief's six-bucket scheme lodges cadence/phrase
form/C-1 under — E is where bar-aligned chord tiling, strong-beat snap and
cadence-to-chord-tone cohere. Register continuity (old §7) also lands in E.
Historical Phase-4.5 two-clock blockquote retained verbatim.

SOURCING NOTE for the human: the parent Lumen README has NO LaTeX/numbered-step
"Scan mode" math section — its scan-mode content is prose (README.md "Lens —
image to tone") plus the honest-constant prose at SPEC.md:130 (source of the
brief's "0.05" / "harmonics capped at 700" examples). The brief's house-style
spec (bold numbered steps, one centred LaTeX equation + one sentence per step,
honestly-named constants) is fully enumerated in the brief itself, so the recast
follows that explicit spec. Flagging in case a literal parent-README section was
expected to exist.

No source/build/test files touched. Determinism/gates untouched (docs-only).

# 2026-07-10 — Queue item 0: AGENT_RULES.md revised (docs-only)

Queue item 0 — AGENT_RULES.md revised to new-world invariants; M4 added to
gates; WSLg capability documented. Docs-only. Old-world stream-identity
rules moved (not deleted) to a "Historical — pre-Phase-4.5 clock" section;
current law = same-seed determinism ×2, the `test_pitch_domain_never_shifts_timing`
pin, musical invariants; gates = M2 + M4, M1 report-only, M3
descriptive-only.

# Phase 4.5 — Session 5: ACCEPTED and MERGED (+ padToWholeBars form-bar rule)

Ear verdict: **PASS** — register continuity fixed seed 58's cliff, all four
audition seeds hold at least as well as the old world, ties accepted, §5
caveat ratified. Full record in `PHASE45_REPORT.md` §12.

- **Merge:** `feature/clock-unification` → `feature/motif-phrasing`, true
  merge `a05c92c` (no squash/rebase; branch preserved and pushed).
- **Post-merge gates, all green:** suite 24069/24069; determinism ×2 both
  fixtures; coupling-cure pin; M4 median 2.73 / worst 9.67 / 0 over 10 st;
  reg_b 3.76.
- **Backlog commit `4186417`:** padToWholeBars ends renders at the form's
  final bar (last onset-bearing bar; final note keeps min(len,
  kCadenceBeats) hold). Byte-identical across 346 renders — 4.5-f already
  killed every reachable sag; now structural. Zero re-pins.
- **Parent:** single pointer commit on `feature/lumena-melody`.
  `feature/phase5-minimal-ui` still records the old pointer until its own
  merge (untouched, per brief).
- Pushed: submodule `feature/motif-phrasing` + `feature/clock-unification`,
  parent `feature/lumena-melody`. STOPPED — no Phase 5 merge, no further
  backlog.

# Phase 4.5 — Session 4: register continuity (4.5-e) + ending sanity (4.5-f) + M4 (STOPPED at ear test)

Branch `feature/clock-unification`, continuing from `9286a3b`. Full detail
in `PHASE45_REPORT.md` §11; refreshed auditions in `auditions/phase45/`.
Nothing merged; parent untouched.

- **Inheritance:** the working tree held an uncommitted, non-compiling
  4.5-e draft from a prior session. Reviewed, completed, and corrected
  (cadence-pair fold separation; bottom-built cadence up-folds breaking the
  leading-tone pin) before gating. Stated in the report.
- **Commits (each gated: suite 24069/24069, determinism ×2 both fixtures,
  invariants):**
  - `da4d09d` re-baseline(4.5-e) — the stated register rule: 6 st band
    (9 st for B), walk compass ±4 degrees, whole-phrase octave folds,
    per-bar rescue with an atomic final-phrase group and a post-fold
    cadence-approach retune. Canary vs `9286a3b`: timing/velocities
    byte-identical 122/122 (default ornaments AND arp-0), pitch-only.
  - `b5e4bd4` re-baseline(4.5-f) — final cadence trims to the bar line it
    crosses when ≥ 2 beats of hold remain; seed 30: 11→10 bars, terminal
    7.25→3.25 b. Seed-30 9→11 trace: 9→10 legitimate 4.5-b onsets,
    10→11 pad artifact (fixed). padToWholeBars untouched.
  - `cada2d7` metrics — M4 (mean + worst adjacent-bar centroid jump,
    bars 1–8) in `--metrics` and the sweep CSV (append-only). G4 122/122.
- **Gate result (60 seeds Mona):** M4 median 3.63 (old) / 3.75 (pre-fix) →
  **2.73**; seeds with a single jump > 10 st: 16 / 20 → **0**; worst jump
  17.67 → 9.67; reg_b 4.35 (old) → **3.76** (≤ ~4.9 holds); tripwire 0/60;
  coupling-cure pin green. The stated rule beats the old world's
  accidental glue on every register metric.
- **STOP:** ear test on refreshed `auditions/phase45/` is the acceptance
  gate. Listen notes in report §11 (incl. the three per-note-fallback
  seeds 30/44/46).

# Phase 4.5 — Sessions 2+3: clock unification BUILT (B+C complete; STOPPED at ear test)

Branch `feature/clock-unification` off tag `pre-clock-unification`
(`cc66e81`). Full detail in `PHASE45_REPORT.md`; auditions in
`auditions/phase45/`. Nothing merged; parent untouched. NEW-WORLD
re-baseline as commissioned — the old byte canary is void across 4.5-b.

- **Commits (each gated: suite green, determinism ×2 both fixtures,
  invariants):** 4.5-a coupling cure (`7eaddb2`, arp-0 byte-identical to the
  tag — proven, 122/122), demo phrase column (`919127d`, G4), 4.5-b one
  clock (`4f0b1a4`, walk+flatten fused, plan-then-walk, pass 2 + both
  fictional clocks + cursor + gradient draw deleted, endings on REAL bars —
  suite needed ZERO re-pins), 4.5-c tied anticipation (`731c771`, sliver→tie
  rule, bar-line crossings now real), 4.5-d phrase-entry accent (`1f37a0e`,
  the old accidental anchor as a stated rule, pitch-only).
- **THE CURE, proven:** permanent test (influence 0↔0.9, 40 seeds, ornaments
  ON ⇒ timing/velocity byte-equal) + at-scale check on ARM-1's full rider
  set: **15/15 seeds, zero rhythm shifts, rider 12.3% → 0%.** Structural:
  the timing skeleton is decided before any pitch exists.
- **Verification battery:** 61 seeds × both fixtures — determinism 244/244
  dump-pairs byte-identical; invariants 122/122 (960-grid, monophony,
  phrase form, tonic cadence, bar counts). Suite 24069/24069.
- **C.3 gate (B-phrase survival) — PASS with measured caveat:** 180-seed
  four-state table: no-fix 5.40 / old ARM-1 4.42 / new 4.88 / new-with-C-1-
  disabled 5.36. So C-1 verifiably still delivers (disable ⇒ collapse to
  baseline); the old world's extra ~0.45 st was its double-harmonization
  artifact (pass-1 fictional snaps under pass-2 real snaps), unreproducible
  honestly. Tripwire 0/60. M1 reported, not gated.
- **Auditions:** `auditions/phase45/mona-seed{30,2024,58,70}-{old,new}world
  .mid` (110 BPM). Seed 70 = tied-anticipation showcase (note @23.75 holds
  through bar 6). NOT pitch-only pairs — two worlds, listen comparatively.
- **Ear test = the acceptance gate. STOP here.** If B-relatedness reads
  weaker: honest levers are W or a real register rule, never the old
  double-harmonization (report §10).

---

# Phase 4.5 — Session 1/3: clock trace (read-only, docs only)

Deliverable: `CLOCK_TRACE.md` at this tip (`feature/motif-phrasing`). No
engine code, no builds beyond reading, no branches. Highlights:

- **Two clocks fully mapped:** `localBeat_` (:1130, phrase-relative, sole
  reset :370) and `harmonyBeat_` (:1139, generation-order) — sole advance
  site for both is `stepNote:950-951`, so rests, ornament figures, settles
  AND every copied/varied A-family phrase are invisible to them. Newly
  pinned consequence: **A′/A″ phrases advance `harmonyBeat_` by zero**, so
  the pass-1 harmony bar is fictional after phrase 1, and walked ENDINGS
  (snap-ineligible, :939) cadence onto that fictional bar's chord — pass 2
  never covers them. Also: stepNote's strong-beat velocity accent (:930) is
  dead code — `applyPhraseDynamics` overwrites every velocity.
- **The disease traced end to end** (§2): `maybeOrnament`'s draw consumption
  depends on `src.degree` via the room band `[4, totalDegrees-5]` (dirPick
  :605) and the :603 early return; crossing the band shifts every later
  draw → the 12.3% rider. Cure shape: hoist dirPick+shape to unconditional
  post-coin draws, apply room logic to the drawn values.
- **Draw map complete** (§4). Data-conditional sites: stepNote:899 [CELL]
  (known), maybeOrnament:603/:605 [PITCH], and one **previously
  uncatalogued** [PITCH] site — `enforceVariety:1524-1550` (pitch-dominance
  and spread draws; last-in-stream, so inert for single-generation callers,
  but the disease class — stabilise or retire in 4.5).
- **Anticipation** (§3): templated notes never cross bar lines (template
  boundaries all land on beat 4/8); mid-slot phrase starts get clipped
  fragments; true tied anticipation structurally impossible — the Phase-3.5
  FLAG (SESSION_NOTES:668) is the mandate. Honest emission = duration-only
  change, note count unchanged with single-event sustains.
- **Blast radius** (§5): all 44 melody tests classified — invariants that
  must survive (determinism, 960-grid, phrase structure, cadences, locks)
  vs re-baseline candidates (chord-tone threshold :689, walk-cell
  reconstruction :1349, strong-beat helper :1965 if 4.5-c lands).
- **Design on paper** (§6): single integer-tick emission clock; fuse
  walk+flatten (rest → hoisted ornament draws → walk with real tick);
  delete pass 2, both clocks, the cursor, the :899 draw. Migration
  4.5-a (ornament decouple, arp-0 byte-gate) → 4.5-b (unification, property
  canary — the one commit with no byte gate) → 4.5-c (bar-relative strong)
  → 4.5-d (honest ties, optional). New-world canary stated explicitly:
  old cross-version byte canary is dead from 4.5-b; replaced by G2×2 +
  scoped per-commit byte gates + property set + metric sweep (M2/M3/
  tripwire vs ARM-1 numbers) + a counting-RNG draw-stability regression
  test + one ear gate. Success criterion: rider = 0%.

STOPPED after committing the two docs, per brief. Session 2 opens with
4.5-a.

---

# ARM-1 ACCEPTED AND MERGED — verdict + merge record

**Human ear verdict (on the `auditions/ablation/` 12-file grid, committed at
`ec1d4c2` on `feature/bphrase-variant-c`):** ARM-1 (C-1 + C-3) audibly ≈
baseline, not worse; the measurable register gain stands. **C-2 (contour
mirror) rejected permanently.** C-full had previously failed the ear test;
the ablation isolated C-2 as the harmful component (`ABLATION_REPORT.md`).

- **Merge:** `ablation/arm1-c1c3` (tip `c3162d6`) → `feature/motif-phrasing`
  (the primary work branch, the one `feature/bphrase-register-rein` was cut
  from), merge commit `c10a31e`, no squash, no rebase. Merged tree verified
  byte-identical to `c3162d6`. Brings, in order: Variant B register rein
  (`9086ae9`, part of the accepted register gain), S-1 scoreboard, C-1
  related-region read window (W=2), C-3 open B cadence + its role-aware test
  re-baseline, S-4 register columns.
- **Gates at the merged tip (fresh Release build):** suite **22526/22526
  green**; determinism checkerboard seed 2024 ×2 → MIDI+dump byte-identical,
  and byte-identical to the gated ARM-1 artifacts from the ablation session.
- **Scope:** deliberate pitch re-baseline. Stream-exactness held at `--arp 0`
  (61 seeds × both fixtures, onsets/durations/velocities byte-identical);
  with default ornaments, rhythm shifts on ~12.3% of seeds via the
  PRE-EXISTING `maybeOrnament` pitch→draw coupling (re-baseline scope, cured
  in Phase 4.5; coupling untouched).
- **Docs:** `ABLATION_REPORT.md` (with closing addendum) and
  `VARIANT_C_REPORT.md` brought onto this branch so the merge message's
  references resolve. No branch or tag deleted; `feature/bphrase-variant-c`,
  both arm branches, and `ablation/baseline-metrics` remain as history.
- **Status:** b-phrase wander item reclassified **REDUCED, NOT CLOSED**.
  Phase 4.5 remains closed. Parent repo: single commissioned submodule-pointer
  commit follows this one; no other parent file touched, no VST rebuild.

---

# Phase 4 tail — B-phrase pass · Variant B raw per-note diff (read-only)

Direct MIDI note dump of the two audition renders (both hashes verified:
baseline `599b674d…`, variant `77018a89…`). Both files are a **single melody track**
(no accompaniment track in this phrased render), 37 notes each, PPQ 480.

**Canary CONFIRMED (STEP-0 claim holds exactly):** 37 notes each · onsets
**byte-identical** · durations **byte-identical** · **exactly 5 pitches changed, all
DOWN 1–2 semitones**. No onset/duration difference anywhere. The 5 changes are one
contiguous run — a single reined varyMotif phrase (bar 4, beats 13.0–17.0):

| idx | onset | old → new | Δ |
|---|---|---|---|
| 15 | 13.00b | F5 (77) → D#5 (75) | −2 |
| 16 | 13.50b | G5 (79) → F5 (77) | −2 |
| 17 | 14.00b | D#4 (63) → D4 (62) | −1 |
| 18 | 14.75b | D#4 (63) → D4 (62) | −1 |
| 19 | 15.50b | D#4 (63) → D4 (62) | −1 |

(Consistent with the transpose band tightening ±2→±1: this phrase's applied shift
dropped one degree, pulling the whole run down toward home register.)

**Velocities byte-identical** (raw sequence matches note-for-note across all 37):
`[61,73,85,86,74,61,72,84,90,78,61,73,85,90,78,61,73,85,86,74,70,80,91,101,105,93,81,61,73,85,89,77,56,66,76,84,72]`.
On seed 2024 the pitch changes did NOT trip the maybeOrnament coupling, so the RNG
stream stayed aligned and dynamics are unchanged — the diff is purely those 5 pitches.

---

# Phase 4 tail — B-phrase pass · VARIANT B (register-rein) · AUDITION BRANCH

Branch `feature/bphrase-register-rein` off tip `a9852a5`. **Audition only — DO NOT
MERGE** until the user picks by ear. Engine/submodule only; parent untouched.

## What changed (`MelodyGenerator.cpp`, all inside `varyMotif`)
Keeps B genuinely fresh — the B-teleport (`:369/:370`) and `selectContour`
(`:869-880`) are **untouched**, so B still picks its own contour. Only the register
sprawl in `varyMotif` is reined, as two **post-draw clamps of already-drawn values**:
1. **Transpose band tightened to ±1 degree** (was ±2). After `delta` is computed
   from the preserved `pick :477` / `uni01 :478` draws, the applied magnitude is
   clamped `[-1,+1]` (sign/direction free). New member `octaveLiftedLastVariation_`.
2. **Octave-lift: no consecutive lifts.** The `lift :493` draw is kept verbatim
   (`liftRolled`); the lift is applied only if it rolled AND the previous variation
   didn't lift AND range has room, so the back half never sits ≥1 octave up and a
   single lift stays exactly one octave from motif A's home register.

**Draws confirmed preserved (count + order):** `pick :477`, `uni01 :478`, `lift :493`,
and (Flowing) `which :507` / `rl :510` all fire exactly as before — no draw added,
removed, or reordered. B-teleport `:369/:370` and the accumulator (`:1213/1293/1308`)
untouched. Engine `LumenaTests` **22524/22524 green**. Both reins demonstrably fire
(transpose pulls seen as −1/−2 semitone deltas; the octave-guard shows as |Δpitch|=12
reversals on seeds 3/21/22/23/36/44).

## Baselines (`--seed 2024 --tempo 110 --mode phrased --length 32 --loop-bars 8`, density 0)
- **checkerboard** ×2 byte-identical to itself AND to the STEP-0 baseline:
  `304db53d3d5a74153d9b07de172ba8f331a5b79ed46ca39b90c2c580288c47fb` (0 pitch changes
  — the uniform fixture never sprawled, so nothing to rein). Determinism holds.
- **Mona Lisa** (`mona_variantB.mid`):
  `77018a89c95d476a45a11ddb96bdaaa956b29337da365a0458718fdb015c4177`.

## Canary — deliverable seed 2024: PASS
Checkerboard byte-identical (0/N pitch changed). Mona Lisa: 37→37 notes,
**onsets+durations byte-identical, 5/37 pitches changed** (all pulled DOWN −1/−2
semitones toward home register). Onset/duration stream intact on the deliverable.

## ⚠️ STOP-AND-FLAG — pitch→ornament draw coupling (found by extended 60-seed sweep)
Against a clean tip-`a9852a5` demo built in a worktree (verified: it reproduces the
STEP-0 checkerboard/mona hashes exactly, same `-O3 -DNDEBUG` flags):
- **51/60 seeds: onsets+durations byte-identical, pitch-only** — as intended.
- **3/60 seeds (10, 26, 44): onsets/durations CHANGED.** Root cause is a **pre-existing
  pitch→draw coupling in `maybeOrnament`**, NOT a draw my clamp adds: the ornament
  fires on `coin < arpeggioAmount` (**default 0.15, active under the baseline
  command**), then reads the site note's `src.degree` at `:564-568`. `dirPick(rng_)`
  (`:568`) is drawn **only when `ascOk && descOk`** (short-circuit), and the
  `!ascOk && !descOk` early-return (`:566`) decides whether a 3-note figure is
  inserted at all. My register-rein lowers degrees, so on a few seeds an ornament
  site crosses that range boundary → an extra `dirPick` draw and/or a +2-note figure
  → the downstream stream (rests, `shape`, velocities, later pitches) shifts. Seed 10
  = +2 notes (gate flip); seeds 26/44 = same count, shifted durations (`shape`/`dirPick`
  outcome differs).
- **This coupling is intrinsic to the engine** — *any* pitch-domain change (Variant A
  too, or merely a different seed) trips it. It is the same family as the bug-4 /
  two-clock pitch-feeds-stream issues. Eliminating it means making `maybeOrnament`'s
  draw/insert decision pitch-independent — a change to the guardrailed ornament path
  and its own re-baseline, **out of scope for an audition branch**. Not touched.
- **Determinism is intact** (same seed ⇒ same output for both base and variant); this
  is a re-baseline-scope question, not a nondeterminism bug.

**Implication for the coordinator:** the deliverable (seed 2024) is a clean pitch-only
re-baseline, but if Variant B is chosen, merging it is a pitch re-baseline that ALSO
shifts *rhythm* on ~5% of seeds via the ornament coupling. Decide whether that scope is
acceptable, or whether the merge should first neutralise the `maybeOrnament:566-568`
coupling (separate, guardrailed, re-baselining task). Simpler reversible choice taken
here: leave the coupling, document it, don't touch the ornament path.

---

# Phase 4 tail — B-phrase pitch-coherence pass · STEP 0 read-only re-confirm

Read-only reconnaissance before commissioning the two audition variants (motif
tie-back vs register-rein). **No source edits, no commits, no draw-order/accumulator
touch.** Verified the two transforms against tip; captured the baseline references
the variants will be diffed against.

## Environment
- Parent HEAD `ac7d576` (branch `feature/lumena-melody`); submodule HEAD
  `a9852a5` (`phase1-complete-24-ga9852a5`, tip = Phase 4b-fix 2/2). This is the
  accepted tip.
- Engine `LumenaTests` (build-wsl): **22524/22524 green, 0 failed.**
- Parent `lumen_tests` (build-linux): green **except** the one known
  `wavetable SHA-256` golden — expected `62a3e9c6…`, actual `2431c02a…` (Windows-
  pinned Lens hash, Linux FP differs; 1 of 2 checks in that test). **Nothing else
  red.** Not melody, left as-is per handoff.

## Doc-location note (not a blocker)
The "read-only Timing-Coherence Diagnosis" is **not a standalone file in the repo**.
Its conclusions survive only as (a) code comments that cite it — e.g.
`MelodyGenerator.cpp:2024` (mutate skeleton), the teleport comment at `:353-359/:363-366`
— and (b) handoff §3. Verified A–D **directly against the code at tip** (authoritative),
which is what STEP 0 requires. Line numbers below are the real ones at `a9852a5`.

## A — Rhythm coherence: CONFIRMED
One groove is picked **once in the ctor** (`pickRhythmTemplate()` at `:322`; body
`:946-975`, a single `discrete_distribution pick(rng_)` draw at `:972-973`). Every
templated note's emitted length is `barAlignedDuration(beat, rhythmTemplate())`
(`:1251`; def `:1045-1060`) tiled against the flatten accumulator. `varyMotif`'s
retime (`:504-511`, sets `v[j].lengthBeats`) is a **dead write**: copied motif notes
keep `templated=true` (set in `stepNote` `:830`), and emission takes
`barAlignedDuration` for `templated && Flowing`, **ignoring `lengthBeats`** (`:1248-1252`).
`nextDuration()` (`:913-918`) draws no RNG. So rhythm cannot desync from pitch. ✔

## B — B-phrase teleport (PRIMARY): CONFIRMED
Call site `builder.walkPhrase(motifLen, /*newRegion=*/true, …)` at **`:1193`** (handoff
~:1193, exact) → `walkPhrase` `:360-384`, newRegion branch **`:363-371`** (handoff
~:363-372). It **jumps to a random grid cell** — `colDist(rng_)` `:369` then
`rowDist(rng_)` `:370` — then `selectContour(col_,row_)` (`:372`; def `:869-880`) picks
a **fresh Rise/Fall/Arch contour** from the brightness slope at the new cell,
**unrelated to motif A**, and `selectContour` draws **no RNG** (deterministic).
**Teleport-specific draws: exactly 2, in order — col then row.** The `motifLen`
interior notes then draw the ordinary per-note walk stream (see D); the phrase-final
note draws only `chain_.nextBiased` (`:764`, snap is deterministic).

## C — varyMotif transpose + octave-lift (SECONDARY): CONFIRMED
Function `:447-513` (transpose+lift core `:467-502`, matches handoff). **Transpose
range ±1..2 scale degrees:** random `delta ∈ {-2,-1,1,2}` (`kDeltas` `:475`) OR an
image-led `deltaImage` clamped to `[-2,+2]`, nudged to ±1 if 0 (`:470-473`), then a
shrink-to-fit loop keeps the motif in range (`:480-482`). **Octave lift** (`:488-502`):
gated on `lift(rng_) < kOctaveLiftProbability` (**0.25**, `:96`) **and** range room
`hi2 + degreesPerOctave_ < totalDegrees_` (`:496`); when it fires it hoists **every
note by one full octave** (max single register jump = one octave, conditional).
**Draws, in order:** `pick(rng_)` `:477` (random delta), `uni01()` `:478` (image-vs-
random gate), `lift(rng_)` `:493` (octave-lift coin); **+2 in Flowing mode**:
`which(rng_)` `:507`, `rl(rng_)` `:510` (the dead-write retime). Total **3 draws
(non-Flowing) / 5 (Flowing)**.

## D — Stream / accumulator: CONFIRMED
One shared `std::mt19937` (`rng` → `PhraseBuilder(… rng)` → `rng_`); pitch, rhythm
(template pick + varyMotif retime), and harmony (`pickOrDrawProgression` `:319`) all
draw from it, draws frozen "for stream identity". The **flatten-loop `beat`** (`:1213`,
`startBeats=beat` `:1293`, `beat += subLen` `:1308`, rests `:1237`) is the **sole
emitted accumulator**; `localBeat_`/`harmonyBeat_` are pass-1 walk-clock only.
**Draw sites variant work must preserve verbatim (count + order):** B → `:369` col,
`:370` row. C → `:477` pick, `:478` uni01, `:493` lift (+ `:507`/`:510` in Flowing).
Any pitch remap must reuse these exact draws, not add/reorder them.

## Baseline references (current tip `a9852a5`, `--seed 2024 --tempo 110 --mode phrased --length 32 --loop-bars 8`, density 0)
- **checkerboard** (determinism): SHA-256 `304db53d3d5a74153d9b07de172ba8f331a5b79ed46ca39b90c2c580288c47fb`
  — regenerated **twice, byte-identical** ✔ (369 B).
- **Mona Lisa** (audition): SHA-256 `599b674db29becb01a8a2f985ccee2a24411050e1be0acf5bbb51ce59dcca015` (367 B).
- Artifacts held in the session scratchpad (not committed); the two variants diff
  against these — checkerboard must stay byte-identical within each variant, Mona
  Lisa is the ear test.

**STOP-AND-FLAG:** none on the transforms — the code matches the handoff's model
(line numbers essentially unmoved). The only deviation is documentary: the Diagnosis
"doc" is code-comment + handoff, not a file (noted above). Safe to proceed to variant
design; do NOT touch draw order or the accumulator.

---

# Phase 4b-fix — mutate bar-clamp + phrase-aware splice

Baseline Phase 4c (`37a114f`). Two independent post-hoc defect fixes from the
Timing-Coherence Diagnosis. Generation is untouched (both fixes operate on emitted
`Melody` note lists) — verified byte-identical to the prior commit on the
checkerboard across all four modes.

## Commit 1 — mutate preserves the timing skeleton and bar count

**Defect:** `mutate` replaced note lengths with un-bar-aligned values
{0.5,1.0,2.0}, re-laid the timeline **gapless (deleting the inter-phrase rests)**,
and `padToWholeBars` only rounds UP — so a 0.30 mutation grew a 9-bar melody to 10
bars and dismantled the groove.

**Fix (chosen approach — fixed onsets, bounded length nudge):** the mutation now
keeps every note's **START fixed**, so onsets and the rests between them are
preserved exactly. Rhythm mutation is a **bounded ±0.5-beat length nudge** clamped
to the note's own slot (the gap to the next onset) and floored at 1/16, on the
960 grid — a note can only breathe within its slot, never move or overlap. The
**final note is never mutated** (it is the cadence and it defines the span, so the
bar count stays exact and the 4c landing is intact). No gapless re-lay, no
`padToWholeBars` call. Bar count is invariant **by construction**.

- *Why fixed onsets, not moving them?* Preserving the onset grid + rests is what
  keeps the melody reading as structured; nudging only lengths (articulation) is
  the small, skeleton-safe mutation the diagnosis asked for, and it cannot grow
  the span or delete a rest. A meaningful onset-level rhythm variation would need
  a bar-aware re-lay (harder, riskier) — deferred as the simpler reversible option.

**Verified:** Mona Lisa, amount 0.30, seed 7 — base **9 bars → mutate 9 bars**
(was 9 → 10). Tests: `test_mutate_preserves_bar_count_and_skeleton` (same note
count, same bar count, onsets byte-unchanged, no note overruns its slot,
deterministic per seed; amounts 0.30 & 0.50 × 10 seeds). Existing
`test_mutate_respects_locks` still passes (lock rhythm ⇒ no length nudge, onsets
already fixed ⇒ timing fully held; lock pitch ⇒ pitch held). Engine
**22512/22512 green**.

**Flagged follow-up (NOT fixed here — out of scope, would re-baseline generation):**
`padToWholeBars` (`MelodyGenerator.cpp`) computes `bars = max(ceil(total), loopBars)`
and never trims, so `loopBars` is a *floor*: the phrased path itself overshoots —
an 8-bar loop renders as **9 bars** when the body runs long. Changing pad-to-trim
behaviour is a generation re-baseline and belongs in its own pass.

## Commit 2 — recombineLocked splice is phrase-aware

**Defect:** the splice mapped pitch by raw note index (`pitch = pitchSrc[min(i,
size-1)]`, phrase-blind), so the pitch source's phrase-end resolutions (the 4c
chord-tone/cadence landings) fell on the OTHER track's *different* phrase
boundaries — resolutions drifting mid-phrase. This is why lock-pitch → new-rhythm
auditioned poorly.

**Fix:** when both tracks carry phrase boundaries (`Melody::phraseStarts`), the
splice aligns **phrase-to-phrase**. Rhythm phrase `p` draws from pitch phrase
`min(p, lastPitchPhrase)` (extra rhythm phrases reuse the last pitch phrase);
within a phrase, pitches fill **in order** and the phrase's **final slot always
takes the pitch phrase's final pitch**, so the resolution lands on the phrase end.
The prior count-matching rule is preserved (locked track authoritative for
count/timing; hold-last within a phrase, never cycle-from-start). Falls back to
the flat in-order/hold-last splice when either track lacks phrase info (Freeform).

**Plugin carrier (minimal, no state change):** the stored plugin `Sequence` does
not carry phrase boundaries, so `sequenceToMelody(currentSeq)` had none — the
plugin's lock-pitch would fall back to index-based. Rather than change the state
schema, `MelodyController` keeps `currentPhraseStarts` in memory (like
`currentProgression`) and restores it onto `prev` before the splice. Note indices
survive the Sequence round-trip (same count/order) and the phrase boundaries are
index-invariant across mutate (onsets/count fixed), so the carrier stays valid.
Session-memory only (not persisted across reload — same known limitation as
`currentProgression`).

**Tests:** `test_recombine_phrase_aware_alignment` (checkerboard, both phrased:
after a lock-pitch splice every rhythm phrase's final slot carries the pitch
phrase's final pitch; output phrases follow the timing track; pure/deterministic).
Existing `test_recombine_locks_dimensions` / `test_splice_count_matches_and_holds_last`
(Freeform / hand-built, no phraseStarts) still pass via the fallback. Engine
**22524/22524 green**; parent green except the pre-existing wavetable golden.

**Determinism:** `recombineLocked` draws no RNG (pure). Generation byte-identical
to prior across all modes (both fixes are post-hoc primitives). Verified: the
checkerboard lock-pitch splice re-runs byte-identical.

## Samples — `samples/phase4b-fix/` (parent repo)

- `mutate-before.mid` / `mutate-after.mid` (Mona Lisa, amount 0.30, seed 7):
  **base 9 bars → mutate 9 bars** (was 9 → 10), onsets byte-identical (rests kept).
- `lock-pitch_new-rhythm.mid` (Mona Lisa, base 2024 / regen 4242): phrase-aligned
  splice — pitch-source resolutions land on the new rhythm's phrase ends.
- `determ-checkerboard.mid`: lock-pitch splice re-run byte-identical.
  WAV/MP3 still needs a soundfont on your end.

---

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

## Samples — `samples/phase4c-cadences/` (parent repo)

Phrased, 8 bars, 100 BPM. Auditions:
- `mona-cadence.mid` — Mona Lisa (C Minor, no leading tone): chord-tone phrase
  endings + the settle, seed 2024 (37 notes).
- `harmonic-minor-cadence.mid` (+ `harmonic-minor-fixture.png`, a synthesised
  vivid very-dark image that detects **C Harmonic Minor**) — **seed 2**, chosen
  so the closing cadence audibly resolves **B → C through the leading tone** with
  a long final tonic (35 notes). The leading-tone approach fires whenever the
  cadence tonic isn't at the very bottom of the range (~half of seeds).
- `determ-checkerboard.mid` — determinism proof (checkerboard, seed 2024); same-
  seed re-run byte-identical. WAV/MP3 still needs a soundfont on your end.

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

---

# Docs session — README "the math" section

Added a **"How the melody engine works: the math"** section to `README.md`
(between the Pipeline table and Requirements). DOCS ONLY — no code, no test, no
build-config touched. Written to match the parent Lumen README's technical
register (the parent README has no dedicated math section, so the tone/depth was
matched to its "Lens — image to tone" explanatory style).

Every equation/mapping is derived from the current post-Phase-4.5 code and cites
the file + function it comes from. Covers: image features (luma709 grid,
Absolute/Stretched normalisation, saturation-weighted circular hue mean, local
contrast/imageDetail); key/scale selection (circle-of-fifths position,
`chooseScaleType` tuned tree); the feature→parameter maps (velocity, register
target, flowing duration, density subdivisions, groove `activity` weighting,
contour slope, motif transpose); the theory-weighted Markov chain
(`fromTheory` matrix, leap-resolution / third-repeat dynamic row, `nextBiased`
image blend, `noteAt`); the harmonic frame (6 pop templates, per-bar triad
spelling, strong-beat 0.6 snap); phrase form (A/A′/B/cadence loop, `varyMotif`,
cadence rules, C-1 related-region window, C-3 open B cadence); the 960-tick
plan-then-walk clock (`barAlignedDuration`, honest tied anticipation, register
continuity 4.5-d/4.5-e three-part fold); and determinism (borrowed mt19937,
pitch-never-touches-stream firewall pinned by
`test_pitch_domain_never_shifts_timing`, SMF note-off-before-note-on). Pre-4.5
two-clock mechanics mentioned only as a one-line historical note.

Each stated formula was re-checked against its cited code line in a second pass.
