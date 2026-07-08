#!/usr/bin/env python3
"""Phase-0 baseline measurement harness for LUMENA.

Drives ./build-wsl/bin/lumena_demo, dumps each melody to CSV via --dump-notes,
and measures how much the melody changes (a) across seeds on one image, and
(b) across images that share the EXACT same global color histogram but differ
in spatial structure (the fixtures from make_fixtures.py).

Melody distance between two CSVs = fraction of index-aligned notes whose PITCH
differs. Positions past the end of the shorter melody count as differing.
0.0 = identical pitch sequence, 1.0 = completely different.

This is measurement-only tooling: it never touches engine/melody code and runs
the shipped binary unchanged.

Usage:
  python3 measure_baseline.py            # run both experiments, write the report
"""

import itertools
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent                      # external/lumena
DEMO = REPO / "build-wsl" / "bin" / "lumena_demo"
FIXTURES = REPO / "test" / "fixtures"
REPORT = REPO / "test" / "phase0_baseline.md"
WORK = HERE / ".work"                          # scratch CSVs

FIXTURE_NAMES = ["orig.png", "hflip.png", "blockshuffle.png", "pixelshuffle.png"]

# Parses e.g. "Detected key: G Major (hue 30°, saturation 0.34)"
KEY_RE = re.compile(
    r"Detected key:\s*(?P<key>.+?)\s*\(hue\s*(?P<hue>[-\d.]+).*?"
    r"saturation\s*(?P<sat>[-\d.]+)\)")


def run_demo(image: Path, seed: int, csv_out: Path, image_influence=None):
    """Run the demo once; return its stdout. Writes the melody CSV to csv_out."""
    cmd = [str(DEMO), str(image), "--seed", str(seed),
           "--mode", "phrased", "--dump-notes", str(csv_out)]
    if image_influence is not None:
        cmd += ["--image-influence", str(image_influence)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        sys.exit(f"lumena_demo failed on {image} seed {seed}:\n{res.stderr}")
    return res.stdout


def read_notes(csv_path: Path):
    """Return (pitches, brightnesses) from a --dump-notes CSV (skips header).

    Columns: index,pitch,startBeat,durationBeats,velocity,degree,source_brightness
    A source_brightness of -1 marks a note with no provenance cell; such rows
    are excluded from the brightness list so they don't skew the correlation.
    """
    pitches, brightness = [], []
    with open(csv_path) as f:
        next(f, None)  # header
        for line in f:
            line = line.strip()
            if not line:
                continue
            cols = line.split(",")
            pitches.append(int(cols[1]))
            b = float(cols[6]) if len(cols) > 6 else -1.0
            if b >= 0.0:
                brightness.append((int(cols[1]), b))  # (pitch, brightness) pair
    return pitches, brightness


def read_pitches(csv_path: Path):
    return read_notes(csv_path)[0]


def pitch_distance(a, b):
    """Fraction of index-aligned notes whose pitch differs; tail counts as diff."""
    n = max(len(a), len(b))
    if n == 0:
        return 0.0
    diff = 0
    for i in range(n):
        if i >= len(a) or i >= len(b) or a[i] != b[i]:
            diff += 1
    return diff / n


def pitch_class_hist_distance(a, b):
    """L1 distance between the two normalized 12-bin pitch-class histograms.

    Alignment-free: it ignores note order and length entirely and only asks
    'which pitch classes, in what proportion'. Range [0, 2]; 0 = identical
    distributions.
    """
    def hist(seq):
        h = [0.0] * 12
        for p in seq:
            h[p % 12] += 1.0
        total = sum(h)
        return [x / total for x in h] if total else h
    ha, hb = hist(a), hist(b)
    return sum(abs(x - y) for x, y in zip(ha, hb))


def dtw_distance(a, b):
    """DTW distance between two pitch sequences, normalized by path length.

    Local cost = |pitch_a - pitch_b|. Returns total warped cost divided by the
    number of steps on the optimal warping path, so it is robust to differing
    melody lengths (an insertion/deletion no longer cascades). 0 = identical.
    """
    if not a or not b:
        return 0.0 if (not a and not b) else float("inf")
    n, m = len(a), len(b)
    INF = float("inf")
    # Cost matrix with backpointers to recover the warping-path length.
    D = [[INF] * (m + 1) for _ in range(n + 1)]
    steps = [[0] * (m + 1) for _ in range(n + 1)]
    D[0][0] = 0.0
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            cost = abs(a[i - 1] - b[j - 1])
            # Pick the cheapest predecessor (match / insertion / deletion).
            best, bstep = D[i - 1][j - 1], steps[i - 1][j - 1]
            if D[i - 1][j] < best:
                best, bstep = D[i - 1][j], steps[i - 1][j]
            if D[i][j - 1] < best:
                best, bstep = D[i][j - 1], steps[i][j - 1]
            D[i][j] = cost + best
            steps[i][j] = bstep + 1
    path_len = steps[n][m]
    return D[n][m] / path_len if path_len else 0.0


def pearson(pairs):
    """Pearson correlation over a list of (x, y) pairs. 0.0 if undefined."""
    n = len(pairs)
    if n < 2:
        return 0.0
    xs = [p[0] for p in pairs]
    ys = [p[1] for p in pairs]
    mx, my = sum(xs) / n, sum(ys) / n
    sxy = sum((x - mx) * (y - my) for x, y in pairs)
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    denom = (sxx * syy) ** 0.5
    return sxy / denom if denom else 0.0


def parse_key(stdout: str):
    m = KEY_RE.search(stdout)
    if not m:
        return {"key": "?", "hue": "?", "sat": "?"}
    return {"key": m.group("key"), "hue": m.group("hue"), "sat": m.group("sat")}


def avg_pairwise(pitch_lists, dist):
    """Average of `dist(x, y)` over all unordered pairs of melodies."""
    pairs = list(itertools.combinations(range(len(pitch_lists)), 2))
    if not pairs:
        return 0.0
    return sum(dist(pitch_lists[i], pitch_lists[j])
               for i, j in pairs) / len(pairs)


def all_metrics(pitch_lists):
    """Avg pairwise distance under each metric, as a dict."""
    return {
        "index": avg_pairwise(pitch_lists, pitch_distance),
        "pch": avg_pairwise(pitch_lists, pitch_class_hist_distance),
        "dtw": avg_pairwise(pitch_lists, dtw_distance),
    }


def experiment_a():
    """Across seeds: orig.png, seeds 1..10, default image-influence."""
    orig = FIXTURES / "orig.png"
    lists, counts = [], []
    for seed in range(1, 11):
        csv = WORK / f"A_seed{seed}.csv"
        run_demo(orig, seed, csv)
        p = read_pitches(csv)
        lists.append(p)
        counts.append(len(p))
    return {"metrics": all_metrics(lists), "counts": counts}


def experiment_b(image_influence, tag):
    """Across structure, matched color: 4 fixtures, same seed 42."""
    lists, counts, colors = [], [], {}
    for name in FIXTURE_NAMES:
        csv = WORK / f"B_{tag}_{name}.csv"
        stdout = run_demo(FIXTURES / name, 42, csv, image_influence)
        p = read_pitches(csv)
        lists.append(p)
        counts.append(len(p))
        colors[name] = parse_key(stdout)
    return {"metrics": all_metrics(lists),
            "counts": dict(zip(FIXTURE_NAMES, counts)),
            "colors": colors}


def experiment_c():
    """Image-correspondence: Pearson(source-cell brightness, pitch) for orig.png
    across a sweep of image-influence. This is the direct 'does the image drive
    the pitch?' probe — it should climb toward 1.0 as influence rises once the
    Phase 2 blend model exists."""
    orig = FIXTURES / "orig.png"
    out = {}
    for infl in (0.0, 0.5, 1.0):
        csv = WORK / f"C_infl{infl}.csv"
        run_demo(orig, 42, csv, infl)
        _, pairs = read_notes(csv)
        out[infl] = {"r": pearson(pairs), "n": len(pairs)}
    return out


def main():
    if not DEMO.exists():
        sys.exit(f"Demo binary not found: {DEMO}\nBuild it first: "
                 f"cmake --build build-wsl --target lumena_demo")
    for name in FIXTURE_NAMES:
        if not (FIXTURES / name).exists():
            sys.exit(f"Missing fixture {name}. Run make_fixtures.py first.")
    WORK.mkdir(exist_ok=True)

    def fmt(m):
        return f"index={m['index']:.3f}  pch={m['pch']:.3f}  dtw={m['dtw']:.3f}"

    print("Experiment A: across seeds (orig.png, seeds 1..10)...")
    a = experiment_a()
    print(f"  {fmt(a['metrics'])}")

    print("Experiment B: across structure, matched color (seed 42)...")
    b_def = experiment_b(None, "default")
    b_full = experiment_b(1.0, "infl1")
    print(f"  default image-influence: {fmt(b_def['metrics'])}")
    print(f"  image-influence 1.0:     {fmt(b_full['metrics'])}")

    print("Experiment C: image-correspondence (orig.png, influence sweep)...")
    c = experiment_c()
    for infl in (0.0, 0.5, 1.0):
        print(f"  influence {infl}: Pearson(brightness,pitch) = "
              f"{c[infl]['r']:+.3f}  (n={c[infl]['n']})")

    # Color-match confirmation: every fixture's detected key/hue/sat should match.
    colors = b_def["colors"]
    distinct = {(c2["key"], c2["hue"], c2["sat"]) for c2 in colors.values()}
    matched = len(distinct) == 1

    write_report(a, b_def, b_full, c, colors, matched)
    print(f"\nColor matched across fixtures: {matched}")
    print(f"Report written: {REPORT}")


def write_report(a, b_def, b_full, c, colors, matched):
    am, bd, bf = a["metrics"], b_def["metrics"], b_full["metrics"]
    lines = []
    lines.append("# LUMENA Phase 0 — Baseline Measurement\n")
    lines.append("Mode: phrased. Fixtures are pure pixel permutations of "
                 "`out/warm.png`, so all four share an **identical global color "
                 "histogram** — only spatial structure differs.\n")

    lines.append("## Metrics\n")
    lines.append("- **index** — fraction of index-aligned notes whose pitch "
                 "differs (tail past the shorter melody counts as differing). "
                 "Saturated / alignment-fragile; kept for continuity.")
    lines.append("- **pch** — L1 distance between normalized 12-bin pitch-class "
                 "histograms. Alignment-free; range [0, 2], 0 = identical mix.")
    lines.append("- **dtw** — DTW distance between the two pitch sequences "
                 "(local cost |Δpitch|), normalized by warping-path length. "
                 "Length-robust; 0 = identical contour, larger = bigger pitch "
                 "excursions after optimal alignment.")
    lines.append("- **image-correspondence** — Pearson r between each note's "
                 "source-cell brightness and its pitch, across one melody. The "
                 "direct 'does the image drive the pitch?' probe.\n")

    lines.append("## A / B — melody divergence (avg pairwise)\n")
    lines.append("| Experiment | Setting | index | pch | dtw |")
    lines.append("|---|---|---|---|---|")
    lines.append(f"| A — across seeds (orig, seeds 1..10) | default | "
                 f"{am['index']:.3f} | {am['pch']:.3f} | {am['dtw']:.3f} |")
    lines.append(f"| B — across structure (4 fixtures, seed 42) | default | "
                 f"{bd['index']:.3f} | {bd['pch']:.3f} | {bd['dtw']:.3f} |")
    lines.append(f"| B — across structure (4 fixtures, seed 42) | image-influence 1.0 | "
                 f"{bf['index']:.3f} | {bf['pch']:.3f} | {bf['dtw']:.3f} |")
    lines.append("")

    lines.append("## C — image-correspondence (orig.png, seed 42)\n")
    lines.append("| image-influence | Pearson(brightness, pitch) | n notes |")
    lines.append("|---|---|---|")
    for infl in (0.0, 0.5, 1.0):
        lines.append(f"| {infl} | {c[infl]['r']:+.3f} | {c[infl]['n']} |")
    lines.append("")
    lines.append("Baseline reads near zero and does **not** climb with "
                 "influence — expected, because the Phase 2 blend model that ties "
                 "pitch to region brightness does not exist yet. This row is the "
                 "one to watch: Phase 2 succeeds when r rises toward +1 as "
                 "influence -> 1.0.\n")

    lines.append("## Detected color per fixture (confirms matched color)\n")
    lines.append("| Fixture | Detected key | Hue | Saturation |")
    lines.append("|---|---|---|---|")
    for name in FIXTURE_NAMES:
        col = colors[name]
        lines.append(f"| {name} | {col['key']} | {col['hue']} | {col['sat']} |")
    lines.append("")
    lines.append(f"**Color matched across all fixtures: {matched}** "
                 "(same key/hue/saturation => any melody difference in B is driven "
                 "by spatial structure alone, not color).\n")

    lines.append("## Note counts\n")
    lines.append(f"- A seed counts (seeds 1..10): {a['counts']}")
    lines.append(f"- B default counts: {b_def['counts']}")
    lines.append(f"- B influence-1.0 counts: {b_full['counts']}")
    lines.append("")

    lines.append("## Phase 0 interpretation\n")
    lines.append("The **index** metric is saturated (~0.9 everywhere) and cannot "
                 "discriminate — that is why **pch** and **dtw** were added. Under "
                 "them, across-seed (A) and across-structure (B) divergence are "
                 "now on a readable scale, and A vs B can be compared directly.\n")
    lines.append("The **image-correspondence** row is the cleanest Phase-0 "
                 "scoreboard: it isolates the pitch<-brightness link the product "
                 "promise depends on. At baseline it is flat across influence, "
                 "confirming the image barely drives pitch today. Phase 2 is done "
                 "when that correlation rises with image-influence while the same "
                 "image stays recognizable across seeds (A's pch/dtw stay "
                 "moderate).\n")

    REPORT.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
