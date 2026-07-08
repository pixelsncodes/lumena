# LUMENA Phase 0 — Baseline Measurement

Mode: phrased. Fixtures are pure pixel permutations of `out/warm.png`, so all four share an **identical global color histogram** — only spatial structure differs.

## Metrics

- **index** — fraction of index-aligned notes whose pitch differs (tail past the shorter melody counts as differing). Saturated / alignment-fragile; kept for continuity.
- **pch** — L1 distance between normalized 12-bin pitch-class histograms. Alignment-free; range [0, 2], 0 = identical mix.
- **dtw** — DTW distance between the two pitch sequences (local cost |Δpitch|), normalized by warping-path length. Length-robust; 0 = identical contour, larger = bigger pitch excursions after optimal alignment.
- **image-correspondence** — Pearson r between each note's source-cell brightness and its pitch, across one melody. The direct 'does the image drive the pitch?' probe.

## A / B — melody divergence (avg pairwise)

| Experiment | Setting | index | pch | dtw |
|---|---|---|---|---|
| A — across seeds (orig, seeds 1..10) | default | 0.912 | 0.251 | 2.204 |
| B — across structure (4 fixtures, seed 42) | default | 0.885 | 0.297 | 1.950 |
| B — across structure (4 fixtures, seed 42) | image-influence 1.0 | 0.877 | 0.259 | 1.800 |

## C — image-correspondence (orig.png, seed 42)

| image-influence | Pearson(brightness, pitch) | n notes |
|---|---|---|
| 0.0 | +0.060 | 214 |
| 0.5 | -0.119 | 210 |
| 1.0 | -0.019 | 210 |

Baseline reads near zero and does **not** climb with influence — expected, because the Phase 2 blend model that ties pitch to region brightness does not exist yet. This row is the one to watch: Phase 2 succeeds when r rises toward +1 as influence -> 1.0.

## Detected color per fixture (confirms matched color)

| Fixture | Detected key | Hue | Saturation |
|---|---|---|---|
| orig.png | G Major | 30 | 0.34 |
| hflip.png | G Major | 30 | 0.34 |
| blockshuffle.png | G Major | 30 | 0.34 |
| pixelshuffle.png | G Major | 30 | 0.34 |

**Color matched across all fixtures: True** (same key/hue/saturation => any melody difference in B is driven by spatial structure alone, not color).

## Note counts

- A seed counts (seeds 1..10): [217, 205, 209, 212, 203, 210, 218, 210, 210, 208]
- B default counts: {'orig.png': 206, 'hflip.png': 204, 'blockshuffle.png': 212, 'pixelshuffle.png': 208}
- B influence-1.0 counts: {'orig.png': 210, 'hflip.png': 208, 'blockshuffle.png': 212, 'pixelshuffle.png': 210}

## Phase 0 interpretation

The **index** metric is saturated (~0.9 everywhere) and cannot discriminate — that is why **pch** and **dtw** were added. Under them, across-seed (A) and across-structure (B) divergence are now on a readable scale, and A vs B can be compared directly.

The **image-correspondence** row is the cleanest Phase-0 scoreboard: it isolates the pitch<-brightness link the product promise depends on. At baseline it is flat across influence, confirming the image barely drives pitch today. Phase 2 is done when that correlation rises with image-influence while the same image stays recognizable across seeds (A's pch/dtw stay moderate).
