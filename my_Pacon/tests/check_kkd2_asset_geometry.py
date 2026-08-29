#!/usr/bin/env python3
"""Verify that generated KKD2 PCA1 layers follow the APK watch-face XML geometry."""

from __future__ import annotations

import importlib.util
import struct
import sys
import tempfile
from pathlib import Path

from PIL import Image


PROJECT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = PROJECT / "tools" / "make_kkd2_watch_asset.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("make_kkd2_watch_asset", GENERATOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {GENERATOR_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expected_rgba(source: Path, width: int, height: int) -> Image.Image:
    image = Image.open(source).convert("RGBA")
    if image.size != (width, height):
        image = image.resize((width, height), Image.Resampling.BILINEAR)
    return image


def decode_pca1(path: Path):
    raw = path.read_bytes()
    if len(raw) < 12 or raw[:4] != b"PCA1":
        raise AssertionError(f"{path.name}: invalid PCA1 header")
    width, height, origin_x, origin_y = struct.unpack_from("<HHhh", raw, 4)
    pixels = raw[12:]
    expected_len = width * height * 3
    if len(pixels) != expected_len:
        raise AssertionError(
            f"{path.name}: payload={len(pixels)}, expected={expected_len}"
        )
    return width, height, origin_x, origin_y, pixels


def encode_expected(image: Image.Image) -> bytes:
    encoded = bytearray()
    for red, green, blue, alpha in image.getdata():
        rgb565 = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
        encoded.extend((rgb565 & 0xFF, rgb565 >> 8, alpha))
    return bytes(encoded)


def main() -> int:
    generator = load_generator()
    expected_specs = {
        "kkd2_background.rgb565a": (0, 0, 450, 450),
        "kkd2_hour.rgb565a": (-34, -35, 518, 520),
        "kkd2_minute.rgb565a": (0, 0, 450, 450),
        "kkd2_complication.rgb565a": (106, 121, 110, 110),
    }

    if set(generator.ASSETS) != set(expected_specs):
        raise AssertionError("KKD2 asset set does not match the APK XML layer set")

    with tempfile.TemporaryDirectory(prefix="kkd2_geometry_") as temp_dir:
        temp = Path(temp_dir)
        for name, expected in expected_specs.items():
            spec = generator.ASSETS[name]
            if len(spec) != 5:
                raise AssertionError(
                    f"{name}: generator spec must contain source, origin x/y and target w/h; "
                    f"got {spec!r}"
                )
            source, origin_x, origin_y, target_w, target_h = spec
            if (origin_x, origin_y, target_w, target_h) != expected:
                raise AssertionError(
                    f"{name}: geometry {(origin_x, origin_y, target_w, target_h)} "
                    f"!= XML {expected}"
                )

            output = temp / name
            generator.convert(source, output, origin_x, origin_y, target_w, target_h)
            width, height, actual_x, actual_y, pixels = decode_pca1(output)
            assert (width, height) == (target_w, target_h), name
            assert (actual_x, actual_y) == (origin_x, origin_y), name

            expected_pixels = encode_expected(
                expected_rgba(Path(source), target_w, target_h)
            )
            if pixels != expected_pixels:
                raise AssertionError(f"{name}: generated pixels do not match scaled source")

    print("PASS: KKD2 PCA1 layers match APK XML sizes, origins, and scaled pixels")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
