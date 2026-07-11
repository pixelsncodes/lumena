#!/usr/bin/env python3
"""Generate Phase-0 measurement fixtures.

From a single source image we derive four fixtures that share the EXACT same
global color histogram (identical multiset of pixels) but differ in spatial
structure. Because every fixture is a pure permutation of the source pixels,
any global statistic (average hue / saturation / luma, and therefore the
detected key) is byte-for-byte identical across all four — only the spatial
arrangement changes. That lets us isolate "does the image *structure* drive the
melody?" from "does the image *color* drive the melody?".

Fixtures:
  orig.png         - unchanged
  hflip.png        - horizontal flip
  blockshuffle.png - grid of equal tiles, tile order shuffled (fixed seed)
  pixelshuffle.png - full random pixel permutation (fixed seed)

Usage:
  python3 make_fixtures.py [SOURCE_IMAGE] [OUTPUT_DIR]

Defaults: SOURCE = parent repo's out/warm.png, OUTPUT_DIR = ../../test/fixtures
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

SEED = 20260708
TILE = 64  # tile edge in px for blockshuffle; source dims must be divisible


def load_rgb(path: Path) -> np.ndarray:
    """Load as HxWx3 uint8 (drop alpha so all channels permute together)."""
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.uint8)


def hflip(img: np.ndarray) -> np.ndarray:
    return img[:, ::-1, :].copy()


def blockshuffle(img: np.ndarray, tile: int, rng: np.random.Generator) -> np.ndarray:
    h, w, _ = img.shape
    if h % tile or w % tile:
        raise SystemExit(
            f"blockshuffle: image {w}x{h} not divisible by tile size {tile}")
    rows, cols = h // tile, w // tile
    # Cut into a list of equal tiles, permute their order, reassemble on the
    # same grid. Equal tiles => the pixel multiset is preserved exactly.
    tiles = [img[r * tile:(r + 1) * tile, c * tile:(c + 1) * tile, :]
             for r in range(rows) for c in range(cols)]
    order = rng.permutation(len(tiles))
    out = np.empty_like(img)
    for dst, src in enumerate(order):
        r, c = divmod(dst, cols)
        out[r * tile:(r + 1) * tile, c * tile:(c + 1) * tile, :] = tiles[src]
    return out


def pixelshuffle(img: np.ndarray, rng: np.random.Generator) -> np.ndarray:
    h, w, c = img.shape
    flat = img.reshape(-1, c)
    perm = rng.permutation(flat.shape[0])
    return flat[perm].reshape(h, w, c)


def main() -> int:
    here = Path(__file__).resolve().parent
    default_src = here / ".." / ".." / ".." / ".." / "out" / "warm.png"
    default_out = here / ".." / ".." / "test" / "fixtures"

    src = Path(sys.argv[1]) if len(sys.argv) > 1 else default_src.resolve()
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else default_out.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if not src.exists():
        raise SystemExit(f"Source image not found: {src}")

    img = load_rgb(src)
    print(f"Source: {src} ({img.shape[1]}x{img.shape[0]})")

    fixtures = {
        "orig.png": img,
        "hflip.png": hflip(img),
        "blockshuffle.png": blockshuffle(img, TILE, np.random.default_rng(SEED)),
        "pixelshuffle.png": pixelshuffle(img, np.random.default_rng(SEED)),
    }

    # Verify every fixture is a true permutation of the source pixels: the
    # sorted multiset of pixels must be identical. If this fails the "matched
    # color" premise of the experiment is broken.
    ref = np.sort(img.reshape(-1, 3), axis=0)
    for name, arr in fixtures.items():
        got = np.sort(arr.reshape(-1, 3), axis=0)
        if not np.array_equal(ref, got):
            raise SystemExit(f"{name}: pixel multiset differs from source!")
        Image.fromarray(arr, mode="RGB").save(out_dir / name)
        print(f"  wrote {name:16s} multiset OK  mean RGB={arr.reshape(-1,3).mean(0).round(3)}")

    print(f"All 4 fixtures share an identical global histogram -> {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
