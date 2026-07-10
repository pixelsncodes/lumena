# DECISIONS.md — spec-silent choices, logged as made

## Phase 4.5-a — maybeOrnament figure on tiny scales: clamp, never skip
The old no-room early return (both `degree+4 >= totalDegrees` and
`degree-4 < 0`) skipped the figure entirely, making the EMITTED NOTE COUNT a
function of pitch — the disease class Phase 4.5 cures. New rule: the figure
always splices once the coin fires; its degrees are clamped into range. Only
reachable below 8 usable degrees (span-1 pentatonic/blues); the shipped
span-2 scales never hit it. Chosen over keeping the skip because a compressed
figure is musically harmless and the count invariant is structural.

## Phase 4.5-b — new-world draw order (per phrase, fused pipeline)
Rest coin (+length) → slot draws (repeatCoin/varyMotif | B teleport) →
ornament draws (coin, dir, shape) / closing cadence-length coin → pitch draws
per walked note → dynamics jitter. Chosen so every draw that shapes TIMING
precedes every pitch draw — the timing skeleton is fully decided before any
pitch exists, which makes the coupling cure structural rather than
disciplinary.

## Phase 4.5 — honest anticipation: sliver rule
A templated note starting mid-slot whose remainder-to-boundary is shorter
than 0.5 beats (an eighth — the smallest legit groove value) no longer emits
as a clipped fragment: it sustains through the boundary to the end of the
next slot, as a single MIDI event (tied anticipation). Fragments >= 0.5
beats remain honest off-beat entries, unchanged. Threshold chosen as the
smallest template slot value so no emitted templated note is ever shorter
than the groove's own vocabulary.

## Phase 4.5-d — phrase-entry accent (the register anchor, made explicit)
The old phrase-relative clock reset `localBeat_` to 0 per walked phrase, so
every phrase's FIRST note classified "strong" and took the 60% chord-tone
snap. That was a clock artifact — but it was also the register anchor that
held B phrases near the motif (the accepted ARM-1 fix's reg_b 4.35 scored on
it; the honest clock alone gave 5.10). New rule, stated rather than
accidental: a walked phrase's entry note is an accent point (agogic accent),
snap-eligible exactly like a real integer beat. Pitch-only — the snap coin
was already drawn unconditionally per note, so draw consumption is untouched.

## Phase 4.5-e — register continuity: fold hierarchy and bands
Bands: a bar's pitch centroid stays within 6 st of the previous non-empty
bar's (A-family/closing), 9 st for B phrases (deliberate contrast breathes
wider). Enforcement is a hierarchy, gentlest first: (1) walk compass — a
phrase's drawn degrees clamp post-draw to ±4 scale degrees around its entry
note, one phrase = one register; (2) whole-phrase octave fold toward the
previous emitted bar's centroid (interval- and pitch-class-preserving);
(3) per-bar rescue on the emitted timeline, where the FINAL PHRASE folds as
one atomic group (approach + held tonic + density pieces never separate) and
a bar whose whole-octave fold cannot fit the scale range folds its
out-of-band notes individually (pc-preserving; fires only when the
interval-preserving fold is impossible — the brief's clamp-at-emission,
chosen over a raw clamp because octave moves keep pitch classes). When a
fold relocates the closing cadence, the 4c approach rule (leading tone when
the scale spells one, else nearest step) is re-run on the folded geometry —
a cadence built at the range bottom chose its contour from that geometry,
so the choice must be re-made, not transplanted. All parts are post-draw,
draw-free, pitch-only.

## Phase 4.5-f — terminal cadence trim
On the looping path only: the final cadence trims to the last bar line it
already crosses whenever that leaves ≥ kCadenceBeats (2.0) of hold —
otherwise padToWholeBars (untouched) balloons a fractional overshoot into a
whole extra bar of drone (seed 30 Mona: 7.25-beat terminal, 11 bars). Trim
never extends, never moves an onset; worst remaining terminal is bounded
under beatsPerBar + 2 (measured worst 5.25 beats over 60 seeds).
