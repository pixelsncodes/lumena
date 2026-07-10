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
