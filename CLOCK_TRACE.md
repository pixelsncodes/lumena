# CLOCK_TRACE.md — Phase 4.5 pre-work: the two-clock split, mapped

Read-only trace at `feature/motif-phrasing` tip `286cfb9` (post-ARM-1 merge).
All line numbers are real at this commit, in `src/melody/MelodyGenerator.cpp`
unless another file is named. **No code was changed in this session.** This is
the paper map Phase 4.5 sessions 2–3 execute against.

Scope note: the split lives entirely in the **Phrased** path
(`generatePhrased`, `:1265-1450` + `PhraseBuilder`). Freeform (`:178-237`),
Arpeggio (`:1696-1835`) and Chords (`:1843-1942`) each run a single beat
accumulator and have no ornament/rest/pass-2 machinery — they are out of the
blast radius except where noted (§4 covers their draw sites for completeness).

---

## 1. The two clocks (plus the one real timeline they approximate)

### Clock A — `localBeat_` (pass-1 strong-beat clock)
- **Defined** `:1130` — `double`, unit = **beats since the current *walked*
  phrase began** (phrase-relative).
- **Writes:** reset to 0 at every `walkPhrase` entry `:370`; advanced in
  `stepNote` `:950` by the note's **provisional** pass-1 duration (see below).
  No other writer.
- **Reads:** `:845-846` only — quantised to ticks (`kTicksPerBeat = 960`,
  `:64`) and classified `strong = (tick % 960) == 0`, i.e. **strong = every
  integer beat, phrase-relative** (the comment at `:840-844` marks the
  bar-relative redefinition as deferred to exactly this phase).
- **Consumers of `strong`:** the pass-1 chord-snap gate `:909`; a +10·energy
  velocity accent `:930` — **dead in practice**: `applyPhraseDynamics`
  (`:640-698`, called for every phrase at `:1357`) unconditionally overwrites
  every note's velocity, so the accent never reaches the output; and the
  `dbgStrong` diagnostic `:935` (overwritten again by pass 2 `:786`).

### Clock B — `harmonyBeat_` (pass-1 harmony clock)
- **Defined** `:1139` — `double`, unit = **beats accumulated across the whole
  generation, in generation order**.
- **Writes:** advanced in `stepNote` `:951` by the same provisional duration.
  Never reset. No other writer.
- **Reads:** `:850` only — `updateHarmonyTarget(harmonyBeat_)`, which buckets
  the beat into a bar (`:337-358`, tick-quantised `:344-348`) and rebuilds
  `chordPcs_` from the session progression, one chord per bar.
- **Consumers of `chordPcs_`** (via `nearestChordToneDegree` `:1006-1016`):
  the ending-note cadence targeting `:861-865`, the pass-1 strong-beat snap
  `:909-912`, and pass 2 `:782` (which first rebuilds `chordPcs_` from the
  real beat, `:777`).

### The real timeline — flatten-loop `beat` (what the listener hears)
- **Defined** `:1342`, local to `generatePhrased`; unit = real emitted beats
  from 0.
- **Writes:** advanced by inter-phrase rests `:1359-1367` (0.5-grid snapped)
  and by every emitted note piece `:1437`.
- **Reads:** `barAlignedDuration(beat, template)` `:1380` (emitted duration =
  distance to the next groove-slot boundary, template tiled from absolute 0,
  `:1168-1183`); `note.startBeats = beat` `:1422`.
- **Derived reads of the real timeline:** pass 2
  (`reharmonizeAgainstRealBeats` `:770-790`) reads `notes[i].startBeats`
  `:776`, rebuilds harmony `:777` and strong `:778-779` against it;
  `padToWholeBars` `:1951-1971` reads note extents; the harness/metrics read
  emitted notes only.

### Why A/B diverge from the real timeline (each mechanism, with sites)
The provisional pass-1 duration (`:947-949`) is `nextDuration()` — the groove
template cycled by a **note-count cursor** (`:1023-1028`, `rhythmCursor_`
`:1142`) that never resets and knows nothing about bars — or a flat 1.0 in
Straight mode. The real timeline instead uses `barAlignedDuration` + rests +
figures. Concretely, the pass-1 clocks miss:

1. **Rests** — exist only on the real timeline (`:1359-1367`).
2. **A-family phrases entirely** — `stepNote` is the *sole* advance site
   (`:950-951`), and repeated/varied phrases are copies (`:1303-1305`,
   `varyMotif` `:484-569`) that never run `stepNote`. Every A′/A″ phrase
   advances `harmonyBeat_` by **zero**, so by the second B phrase the pass-1
   harmony clock is typically a full phrase-or-more behind the real bar.
   (`localBeat_` doesn't care — it resets per walked phrase — but this makes
   `harmonyBeat_`'s bar bucket essentially fictional after phrase 1.)
3. **Template phase** — cursor-cycled durations vs bar-tiled emission
   (`:1373-1381`): after any rest or figure the cursor and the tiling disagree
   even about the *same* note's length.
4. **Ornament substitutions** — `maybeOrnament` (`:576-629`) replaces one note
   with a 3-note figure (1.5 or 1.0 beats, `:610`, `:623`) at flatten time,
   after the clocks already counted the original's provisional length.
5. **Cadential settles** — the phrase-final extension to ≥ 1.5 beats
   (`:1390-1393`) and the closing cadence's 2–4 beats (`:474-476`) are
   emission-only.
6. **Density splits** — duration-preserving (`:1399-1406`), the one mechanism
   that does *not* skew the clocks.

Pass 2 exists to patch the damage: it re-decides strong+snap against real
beats for `eligible` (walked, non-ending, primary) notes, reusing the recorded
`snapCoin` (`:271-272`, `:938-939`, `:1435-1436`) so it draws nothing. What it
**cannot** patch: (a) the pass-1 snap already moved `degree_` — the Markov
source of every subsequent draw's distribution — so the *pitch trajectory* is
permanently shaped by the fictional clock; (b) ending notes are ineligible
(`:939`), so **every walked cadence keeps the chord target computed from the
fictional `harmonyBeat_` bar** (`:850` → `:861-865`).

---

## 2. The maybeOrnament pitch→draw coupling (the disease), end to end

Site: `maybeOrnament` `:576-629`, called once per non-closing phrase in
flatten order (`:1351-1353`).

Draw structure per call, with `amount = arpeggioAmount` (demo default 0.15,
`--arp 0` sets it 0):

| step | site | draw? | conditional on |
|---|---|---|---|
| amount guard | `:577-578` | — | option only (`--arp 0` ⇒ the function never draws — this is why the stream-exactness proofs run at arp 0) |
| fire coin | `:580-581` | **1 draw, unconditional** (amount > 0) | outcome gates the rest: coin ≥ amount ⇒ return |
| site pick | `:584-597` | none | deterministic on note `brightness` (cells — pitch-free) |
| room guard | `:601-603` | — | **`src.degree`** — PITCH: `ascOk = degree+4 < totalDegrees`, `descOk = degree-4 ≥ 0`; both false ⇒ return **before the remaining draws** |
| direction | `:604-605` | **1 draw iff `ascOk && descOk`** | **`src.degree`** — PITCH (short-circuit: one-sided room skips the draw) |
| shape | `:609-610` | 1 draw (if not returned at `:603`) | reached-ness is pitch-dependent via `:603` |
| figure splice | `:612-628` | none | replaces 1 note with 3 (count +2), degrees `src.degree ± 2k` |

**The mechanism, traced:** any pitch-domain change anywhere upstream (image
influence, C-1's shadow window `:878-885`, C-3's cadence `:739-760`, Variant
B's rein `:522-523`/`:547`, a future mutate) moves some phrase note's
`degree`. If the ornament site's `src.degree` crosses one of the two room
boundaries — the `ascOk && descOk` band is `degree ∈ [4, totalDegrees-5]`
(`usableDegrees = intervals × span`, `Scale.cpp:9-14`: pentatonic span-2 ⇒ 10
degrees ⇒ band [4, 5]; the Mona fixture's 7-degree minor span-2 ⇒ 14 ⇒
band [4, 9]); the `:603` double-fail needs ≤ 7 usable degrees and cannot fire
at either fixture's scale — then
`dirPick` is consumed (or not) where it previously was not (or was). From that
call on, **every subsequent draw in the generation reads a shifted stream**:
the same phrase's `shape`, then in flatten order each later phrase's dynamics
jitter (`:644-645`), rest coin (`:701-704`), rest length (`:707-710`), and —
because ornament coins now read different values — potentially *which phrases
get figures at all*, which changes note counts (±2 per flipped figure) and
every later onset (the figure's real duration replaces a different templated
length). Result: onsets/durations/velocities diverge, occasionally note count
too — exactly the observed rider (ARM-1: ck 9/61 + ml 6/61 = **12.3%** of
seeds; C-full 9.8%; Variant B ~5%; see `ABLATION_REPORT.md` §5).

Two subtleties worth pinning:
- A pitch change that does **not** cross the `[4, 6]` band is invisible to the
  stream — that is what a "clean pitch-only seed" is. The rider percentage is
  literally the fraction of seeds where some ornament site's degree crosses
  the band.
- The figure's degrees derive from `src.degree` (`:615`), so even on clean
  seeds ornament *pitches* legitimately follow upstream pitch changes; only
  draw *consumption* is the disease.

**The cure's shape (paper, for §6):** consumption must depend only on the
coin. Draw `dirPick` and `shape` unconditionally immediately after the coin
fires, then apply the existing room logic to the already-drawn values
(one-sided room ignores `dirPick`; no room discards both and skips the
splice). Behavior (which figure, whether spliced) stays pitch-dependent —
that is musical intent — but the stream stops being so.

---

## 3. Anticipation / tie handling under the clock mismatch

What exists today (and the flag it was shipped under —
`SESSION_NOTES.md:668-681`, "Anticipation status — FLAGGED, not papered
over"):

- **No templated note ever crosses a groove-slot boundary**, and slot
  boundaries include every bar line: `barAlignedDuration` (`:1168-1183`)
  truncates the emitted length to the distance-to-next-boundary, and all
  three templates' cumulative slots land exactly on beat 4/8 (`:1058-1068`),
  so nothing sustains through a bar line by template.
- **Split behavior:** a phrase whose (0.5-grid) rest lands it mid-slot — the
  3-3-2 templates have 0.75-grid boundaries — gets a **truncated fragment**
  as its first emitted duration (the remainder to the next boundary, as small
  as 0.25 beats), not the slot the groove intended. This is the "split by the
  clock mismatch" case: the walk thought the note was a template-length note
  (`nextDuration`), emission clipped it.
- **Suppressed behavior:** true tied anticipation — a note sounding before
  the downbeat and sustaining through it — is structurally impossible for
  templated notes; the Phase-3.5 session deliberately declined to fake it
  against a phrase-relative `strong`/`localBeat_` clock and flagged it as
  *the* signal for this phase (`SESSION_NOTES.md:674-679`).
- **The exceptions that already cross bar lines** (unreconciled but benign,
  because endings are snap-ineligible): the ≥ 1.5-beat phrase-final settle
  `:1390-1393`, the closing cadence `:474-476`, and `padToWholeBars`'s final
  extension `:1962-1969`.

**What honest emission changes, qualitatively:** onsets do not move (rests
and template boundaries already define them); the change is durations —
mid-slot fragments extend to their intended slot length, sustaining across
the boundary/bar line instead of clipping. Note count is **unchanged** if
ties are emitted as single sustained events (Lumena's `Note` already carries
arbitrary `lengthBeats`; the MIDI writer needs no split), or **+1 per bar
crossing** if a split-and-tie representation were chosen — single-event
sustain is the obvious choice (log to DECISIONS when built). Expected
magnitude: a few notes per 8-bar render (only phrase-first notes after
0.5-grid rests against 0.75-grid templates 1/2 qualify), plus whatever
deliberate anticipation the template design later adds. Mean duration rises
slightly; total bar count is unchanged (`padToWholeBars` unaffected).

---

## 4. Draw-count map — every RNG site in stream order (Phrased path)

Granularity note: a "draw" below = one distribution invocation. Distribution
⇒ raw-`mt19937`-call counts are stdlib-specific (e.g.
`uniform_real_distribution<double>` consumes two 32-bit outputs in libstdc++)
— which is why goldens are toolchain-pinned. Conditionality classes:
**[OPT]** = fixed by options for the whole session (stream-stable);
**[DRAW]** = gated by an earlier draw's value (stream-stable given seed);
**[CELL]** = gated by visited-cell data (the walk trap);
**[PITCH]** = gated by pitch data (the disease class).

**Generation order:**

1. `PhraseBuilder` ctor: progression pick — one `discrete`-style draw
   (`pickProgressionBase` `:1655-1660` via `:1678-1684`, called at `:327`)
   [OPT: skipped entirely under Lock Harmony]; rhythm-template
   `discrete_distribution` pick `:1082-1083` (unconditional).
2. Motif length `uniform_int(3,5)` `:1273-1274` (unconditional, once).
3. Per **walked phrase** (`walkPhrase` `:368-421`: motif, every B, closing's
   walk):
   - B only: teleport col `:379`, row `:380` (2 draws; structural — every B).
     C-1's remap `:396-407` is post-draw, draw-free.
   - Per note (`stepNote` `:798-953`):
     - [OPT] PureRandom cell draws `:801-804` (2/note); default walk path:
       contour step `:812-833` draws **nothing** (deterministic
       best-neighbour).
     - Ending note: `nextBiased` `:862` — exactly 1 draw
       (`MelodyChain::sampleWork`, `MelodyChain.cpp:78-99`, always one;
       Markov history shapes the distribution, never the count).
     - Non-ending: `nextBiased` `:886-887` (1 draw); leap gate `uni01` `:890`
       (unconditional); leap direction + magnitude `:891-892` — **2 draws
       [DRAW+OPT]** iff gate < `complexity·0.4` (never at `--arp 0`);
       **gradient identity draw `:899` — 1 draw [CELL] iff
       `|gradient| > 0.02f`** (gradient = visited-cell brightness delta
       `:837`; the known trap — moving the walk refires it, which is what
       killed the literal C-1); snap coin `:908` (unconditional — 4a-0
       decoupling); anti-stuck coin `:919` (unconditional — 4a-0b).
     - `nextDuration()` `:1023-1028`: no draw (cursor).
4. Closing phrase: walk draws as above (count−1 notes), then cadence-length
   coin `:475-476` (unconditional).

**Flatten order** (per phrase, `:1347-1440` — note: *all* generation-order
draws for *all* phrases complete before any flatten-order draw):

5. `maybeOrnament` (non-closing phrases): full table in §2 — coin
   (unconditional given amount>0 [OPT]); **`:603` room return [PITCH]**;
   **`dirPick` `:605` [PITCH]**; `shape` `:610` (reached-ness [PITCH]).
6. `applyPhraseDynamics` peak jitter `:644-645` — 1 draw per phrase
   (unconditional, closing included).
7. Rest coin `rollRest` `:701-704` — 1 draw per phrase p>0 (unconditional);
   rest length `:707-710` — 1 draw **[DRAW]** iff the coin fired.
8. `applyImageDensity` `:1217-1231`, `densityFillDegree` `:1242-1259`,
   `barAlignedDuration`, settle, splits, pass 2 `:770-790`: **all draw-free**
   (pass 2 reuses recorded `snapCoin`s `:1435`).

**Post-generation (`generateMelody` `:1975-2020`):**

9. `enforceVariety` `:1496-1551` — **[PITCH], previously uncatalogued**:
   - dominance branch `:1524-1535`: 1–2 `shiftPick` draws per *alternate*
     dominant-pitch note, **iff one pitch exceeds 60% of notes and
     `repetition < 0.8`** — a pure function of output pitches;
   - spread branch `:1540-1550`: up to 8 × (`rng() % n` `:1547` — a raw
     engine call, note — + 1–? `shiftPick`) **iff `< 4` distinct pitches and
     energy/complexity > 0.4** [PITCH+OPT].
   Mitigating fact: these are the **last** draws in `generateMelody` — no
   later consumer exists inside one generation, so this coupling is inert for
   single-generation callers (demo, tests, plugin regenerate-from-seed). It
   bites only a caller that keeps drawing from the same `mt19937` afterwards.
   Phase 4.5 should still stabilise or retire it (§6) rather than leave a
   second instance of the disease class latent.
10. `padToWholeBars` `:1951-1971`: draw-free.

**Other paths, for completeness:** Freeform — per-note cell draws
`:207-215`, `nextBiased` `:220`, [OPT] `flowingDuration` `:132-152` (1 draw,
Flowing only). Arpeggiated — progression pick [OPT] `:1731`, per-note cell
draws `:1752-1758`, [OPT] random-pattern pick `:1786-1788`. Chords —
progression pick [OPT] `:1874`, per-chord cell draws `:1896-1903`.
`mutate` `:2140-2199` — per-note coin `:2166` (unconditional), then [OPT by
locks] pitch draws `:2174-2175` and length coin `:2188`. `recombineLocked`:
draw-free.

**Complete list of data-conditional draw sites (the disease class):**
1. `stepNote:899` — [CELL] (known; inert as long as the walk path is never
   moved — AGENT_RULES' "move read sites, never the walk").
2. `maybeOrnament:603` — [PITCH] (kills up to 2 draws + the splice).
3. `maybeOrnament:605` — [PITCH] (the live one at demo scale; the 12.3%
   rider).
4. `enforceVariety:1524-1550` — [PITCH] (latent; last-in-stream).
No others exist in the melody paths at this tip.

---

## 5. Blast radius — what unification legitimately changes vs what must survive

Unification is a **new-world re-baseline**: draw sequences change, therefore
every seed's concrete output changes. Tests that pin *properties* survive;
tests that pin *bytes/thresholds tuned to today's stream* re-baseline. All 44
tests in `tests/MelodyGeneratorTests.cpp` classified:

**Must survive untouched (invariants — failure = real regression):**
- Determinism/no-RNG: `test_reproducible` `:355`, `test_cells_reproducible`
  `:919`, `test_arpeggiator_reproducible` `:1157`,
  `test_phrased_cadence_deterministic` `:787`,
  `test_image_density_draws_no_rng` `:1483`,
  `test_splice_locks_deterministic` `:1809`.
- Grid/timing invariants: `test_phrased_syncopation_on_grid` `:1558` (every
  note on the 960 grid), `test_strong_beat_tick_grid` `:1965` (pure tick
  math — but see caveat below), `test_loop_fills_whole_bars` `:1114`,
  `test_mutate_preserves_bar_count_and_skeleton` `:1753`.
- Phrase structure: `test_phrased_repeats_motif` `:380`,
  `test_repetition_repeats_motif` `:1600`,
  `test_cells_phrased_in_range_and_share_motif` `:942`,
  `test_phrased_rests_between_phrases` `:451` (statistical over 200 seeds —
  robust to a re-baseline).
- Cadences: `test_phrased_cadence_on_tonic` `:418`,
  `test_phrased_ending_is_stepwise` `:630`,
  `test_harmonic_minor_cadence_uses_leading_tone` `:753`,
  `test_phrase_endings_settle_longer` `:816`, and the role-aware halves of
  `test_phrased_endings_resolve_to_chord_tone` `:689` (B endings open on
  pc 2/7 — the ARM-1 pin).
- Scale/harmony spelling (untouched paths): all arp/chords tests
  (`:1050-1345`), `test_chords_are_diatonic_stacks`, blues/harmonic-minor
  spelling, `test_lock_harmony_carries_progression` `:1922`.
- Locks/splice semantics (post-hoc, engine-independent):
  `test_recombine_locks_dimensions` `:1632`, `test_mutate_respects_locks`
  `:1677`, `test_splice_count_matches_and_holds_last` `:1840`,
  `test_recombine_phrase_aware_alignment` `:1878`.
- Dynamics/velocity semantics: `test_phrased_dynamics_peak_mid_phrase`
  `:538`, `test_phrased_dynamics_are_smooth` `:586`,
  `test_energy_raises_velocity` `:1579`, `test_velocity_mapping_in_bytes`
  `:235`.
- Freeform behavior (untouched): `test_random_walk_is_smooth` `:201`,
  `test_rhythm_modes` `:280`, `test_length_and_pure_random` `:326`,
  `test_cells_track_walk_freeform` `:857`, `test_cells_in_range_pure_random`
  `:893`.

**Legitimately change / re-baseline candidates (pins of today's clock):**
- `test_phrased_endings_resolve_to_chord_tone` `:689`, the ≥ 0.9 chord-tone
  threshold on non-B walked endings: today this passes *because pass 2
  exists*; after unification the property should hold natively (endings will
  finally target the **real** bar's chord — fixing §1's fictional
  `harmonyBeat_` for endings, which pass 2 never covered). The semantic
  strengthens; the numeric threshold may need re-tuning either way. Any edit
  ships as a `re-baseline:` commit per AGENT_RULES, never a weakening.
- `test_phrased_tracks_brightness_at_high_influence` `:1349`: reconstructs
  walk cells from emitted notes assuming today's pipeline shape (B exclusion
  from ARM-1); the reconstruction plumbing may need updating when
  walk/flatten fuse — contract itself survives.
- `test_strong_beat_tick_grid` `:1965`: the tick math survives; if 4.5 also
  redefines strong = bar-relative (the `:840-844` comment's intent), the
  "agrees with the old float test" clause and the helper's meaning are
  re-pinned in the same commit as the semantics change.
- `test_phrased_arpeggios_in_scale` `:490`: in-scale property survives; its
  seed-loop expectations about *which* seeds carry figures re-baseline with
  the stream.
- Everything golden outside the suite: the 61-seed dump sets, committed
  audition MIDIs, `samples/` in the parent — historical artifacts, superseded
  by new-world goldens (see §6); the STEP-0 checkerboard hash
  (`599b674d…`-family pins in SESSION_NOTES) is dead as a canary after 4.5.

Parent repo: the wiring test (`imageRhythmAmount`/density) asserts option
plumbing, not note bytes — survives. The known Windows-pinned wavetable
SHA-256 failure is unrelated (AGENT_RULES).

---

## 6. Proposed unification design (paper only — nothing built here)

### 6.1 Representation: one integer-tick emission clock
One `long emitTick_` (960/beat, `kTicksPerBeat` `:64`), owned by the
generation loop, advanced **only** by emitted events (note pieces, rests) —
i.e. today's flatten `beat`, promoted to *the* clock and moved to integer
ticks (the doubles are already `lround`ed at every comparison site; storing
ticks removes the float accumulation the `:339-344` comment works around).
Derived, all from `emitTick_`:
- `strong`: `tick % 960 == 0` initially (behavior-preserving definition);
  the bar-relative redefinition (`tick % ticksPerBar` ∈ {0, mid-bar}) is a
  **separate, later commit** — one semantic change per commit.
- Harmony bar: `tick / ticksPerBar` → `updateHarmonyTarget` (unchanged math,
  real input at last).
- `localBeat_` `:1130`, `harmonyBeat_` `:1139`, `nextDuration`/`rhythmCursor_`
  `:1023-1028`/`:1142`, the `snapCoin`/`eligible` plumbing
  (`:271-272`, `:938-939`, `:1345-1346`, `:1435-1436`) and pass 2 `:770-790`
  are all **deleted** — pass 1 sees real beats, so there is nothing to
  reconcile. The dead `strong` velocity accent `:930` goes with them. The
  `:899` gradient identity draw — preserved today purely for stream identity
  — is deleted too (stream identity with the old world is void by design).

### 6.2 Structural change: fuse walk and flatten
Today: build ALL phrases (pass-1 clocks) → flatten ALL (real clock) → pass 2.
Proposed single pass, per phrase:
1. Rest decision first (draw order preserved relative to the phrase).
2. Ornament decisions **hoisted to phrase-start**: consume coin + `dirPick` +
   `shape` unconditionally (when amount > 0), *apply* later with the existing
   room logic on the already-drawn values (§2 cure). Consumption becomes a
   pure function of the coin.
3. Walk each note with `emitTick_` in hand: harmony and strong from the real
   tick *before* the pitch draw; emitted duration from
   `barAlignedDuration(emitTick_)` directly (plus settle/density, unchanged);
   advance `emitTick_`.
4. Ornament splice + dynamics at phrase end (dynamics jitter draw keeps its
   per-phrase position).
`enforceVariety` (§4.9): draw its coins unconditionally per generation (or
retire it for Phrased — measure first whether it ever fires on the 60-seed
protocol; decide in-session, log in DECISIONS.md) so no [PITCH] site survives
anywhere.

### 6.3 Migration order (sessions 2–3, one gated commit each)
1. **4.5-a — ornament draw-decoupling** (smallest cure first): hoist
   `dirPick`/`shape` per §2. Sharp gate: `--arp 0` output must stay
   **byte-identical across the commit** (ornaments inert ⇒ no ornament draws
   ⇒ untouched stream); default-mode output changes ONLY on
   ornament-firing seeds. Re-pin goldens; suite property tests hold.
2. **4.5-b — the clock unification itself** (fuse per §6.2, delete pass 2 and
   both clocks, keep `strong` = integer-beat): global re-baseline (draw
   order changes for every phrased seed). Gates: suite invariants (§5 list)
   green; determinism ×2; 960-grid; new goldens pinned; metric sweep (§6.4).
3. **4.5-c — bar-relative `strong`** (the `:840-844` intent): semantics-only
   commit atop the clean clock, with `test_strong_beat_tick_grid` re-pinned
   in the same commit.
4. **4.5-d — honest anticipation/ties** (§3): allow templated sustains across
   slot/bar boundaries as single events where the groove intends it; new
   invariant tests (no overlap, grid-aligned, bar count preserved). Optional
   scope — gate on the ear, as ever.

### 6.4 Post-unification determinism guarantee and the new canary
State it plainly: **the old canary (onsets/durations/velocities byte-identical
to the previous tip) is dead for 4.5-b onward** — draw sequences change by
design; this is a new-world re-baseline, not a stream-preserving edit. What
replaces it:

- **G2 unchanged and non-negotiable:** same seed + options + image ⇒
  byte-identical output, generated twice, at every commit. Determinism was
  never the thing at risk — cross-*version* identity was.
- **Scoped stream gates per commit:** each commit declares its expected diff
  footprint up front and everything outside it must be byte-identical to the
  *previous commit* (4.5-a: arp-0 identical, default-mode diffs confined to
  ornament-firing seeds; 4.5-c/d: diffs confined to strong-beat
  reclassification / boundary-crossing durations respectively). 4.5-b is the
  one commit with no byte gate — its gate is the property set below.
- **Property canary for 4.5-b** (the new-world acceptance): per seed over the
  61-seed × both-fixture protocol — phrase count, bar count, note count
  within ± the ornament footprint, template identity, rest positions on the
  0.5 grid, every onset on the 960 grid, closing cadence lands tonic, B
  endings carry pc 2/7; plus the 60-seed Mona metric sweep: M2 register
  columns (`reg_aprime` ≤ ~2.1, `reg_b` ≤ ~4.4 — must not regress vs the
  ARM-1 numbers), M3 Δ within noise of +0.06, tripwire 0/60. M1 reported,
  never gated (AGENT_RULES).
- **The disease's regression test, added in 4.5-a and kept forever:** a
  counting-RNG harness pinning that `maybeOrnament` consumes a draw count
  independent of note degrees (crafted phrases at range-edge vs mid-range
  degrees, same coin path ⇒ same count), and after 4.5-b, that no [PITCH]
  or [CELL] conditional site exists — concretely: two generations differing
  only in `--image-influence` (pure pitch-domain input) must consume
  identical total draw counts. That property is the whole point of the
  phase; pin it so it can never silently regress.
- **The ear gate:** one audition set (Mona seeds 2024/30/58, new world vs old
  world) after 4.5-b — human-accepted before anything merges, per standing
  rules. Ornament-rider percentages should read **0%** in the new world; that
  number is the phase's success criterion.

### 6.5 Open questions for the session-2 brief (not decided here)
1. Fuse-in-place (keep `PhraseBuilder`, feed it the real tick) vs restructure
   into a planner/emitter split — fuse-in-place is smaller and preserves the
   draw-order *shape* (rest → ornament-draws → walk per phrase); recommended.
2. `enforceVariety`: stabilise vs retire for Phrased (measure firing rate
   first).
3. Whether 4.5 also retires the `:899` gradient draw in 4.5-a (it could — arp-0
   byte-identity would break though; cleaner to delete it in 4.5-b where the
   stream moves anyway; recommended: 4.5-b).
4. 4.5-d scope: minimal (stop clipping mid-slot fragments) vs deliberate
   anticipation authored into templates — ear decides after 4.5-c.
