"""Recover KKD1 layer rotations from the APK preview.

This is a deterministic differential harness for the PACON renderer.  It
compares each source dial against the APK's 450 x 450 preview while excluding
pixels covered by later, static layers.  The recovered angles let firmware
tests distinguish an angle-convention bug from an asset/layer-order bug.
"""

from pathlib import Path

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
REFERENCE = ROOT / "_apk_reference"


def first(pattern: str) -> Path:
    return next(REFERENCE.glob(pattern))


def rgba(path: Path) -> Image.Image:
    return Image.open(path).convert("RGBA")


def rotate(source: Image.Image, degrees_ccw: int) -> Image.Image:
    return source.rotate(
        degrees_ccw,
        resample=Image.Resampling.BILINEAR,
        center=(225, 225),
        expand=False,
    )


def score_layer(source: Image.Image, preview: Image.Image,
                static_occlusion: np.ndarray) -> tuple[int, float]:
    reference = np.asarray(preview, dtype=np.int16)
    best_angle = 0
    best_error = float("inf")
    for angle in range(360):
        candidate = np.asarray(rotate(source, angle), dtype=np.int16)
        mask = (candidate[:, :, 3] >= 240) & ~static_occlusion
        if not np.any(mask):
            continue
        delta = candidate[:, :, :3][mask] - reference[:, :, :3][mask]
        error = float(np.mean(np.abs(delta)))
        if error < best_error:
            best_angle = angle
            best_error = error
    return best_angle, best_error


def main() -> None:
    preview = rgba(REFERENCE / "kkd1_preview_circular.png")
    character = rgba(first("kkd1_wfs_1_*.png"))
    mechanism = rgba(first("kkd1_wfs_5_*.png"))
    static_alpha = np.maximum(
        np.asarray(character)[:, :, 3], np.asarray(mechanism)[:, :, 3]
    )
    # The complication sits over the upper-left centre and is not represented
    # by the 450 px dial assets, so exclude its complete XML slot as well.
    static_occlusion = static_alpha > 8
    static_occlusion[121:231, 106:216] = True

    layers = {
        "minute": rgba(first("kkd1_wfs_3_*.png")),
        "second": rgba(first("kkd1_wfs_2_*.png")),
        "hour": rgba(first("kkd1_wfs_4_*.png")),
    }
    expected = {"minute": -48, "hour": 210}
    failed = False
    for name, layer in layers.items():
        angle, error = score_layer(layer, preview, static_occlusion)
        signed = angle if angle <= 180 else angle - 360
        print(f"{name}: recovered PIL/CCW angle={signed:+d} deg, MAE={error:.3f}")
        if name in expected:
            wanted = expected[name]
            wanted = wanted if wanted <= 180 else wanted - 360
            if signed != wanted:
                failed = True
                print(f"  FAIL firmware-equivalent expectation is {wanted:+d} deg")

    # Also emit the exact APK layer stack at the time shown by the board photo.
    # PIL uses positive counter-clockwise angles, the inverse of the firmware's
    # explicitly-clockwise parameter.
    frame = Image.new("RGBA", (450, 450), (0, 0, 0, 255))
    frame.alpha_composite(rotate(layers["minute"], -12))   # minute 02
    frame.alpha_composite(rotate(layers["second"], 0))
    frame.alpha_composite(rotate(layers["hour"], -90))     # 90 - hour*30
    frame.alpha_composite(mechanism)
    frame.alpha_composite(character)
    output = ROOT / "my_Pacon" / "build" / "diagnostics" / "kkd1_0002_apk.png"
    output.parent.mkdir(parents=True, exist_ok=True)
    frame.save(output)
    print(f"wrote {output}")

    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
