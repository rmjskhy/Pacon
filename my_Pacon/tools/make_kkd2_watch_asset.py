"""Build the second PACON watch face from the user-supplied KKD2 APK.

The KKD2 XML uses a 518x520 rotating hour layer in a group offset by
(-34, -35), plus 450x450 background and minute layers.  PCA1 stores signed
x/y coordinates so pixels outside the visible 450x450 face can rotate into
view without clipping.
"""

from pathlib import Path
import struct

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
OUTPUT_DIR = ROOT / "my_Pacon" / "main" / "assets"
ASSETS = {
    "kkd2_background.rgb565a": (
        "kkd2_wfs_34_fa164e30_ad8f_49ad_a48c_636319779f3f.png",
        0, 0, 450, 450),
    "kkd2_hour.rgb565a": (
        "kkd2_wfs__6a479ce3_63f1_4c9c_9575_8acec9425efe.png",
        -34, -35, 518, 520),
    "kkd2_minute.rgb565a": (
        "kkd2_wfs__16d230a1_54a9_4417_88e2_bbabdbe3a07c.png",
        0, 0, 450, 450),
    "kkd2_complication.rgb565a": (
        "kkd2_wfs_6_7fe875ab_7705_4af3_910b_1a0da412376b.png",
        106, 121, 110, 110),
}


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def convert(source: Path, output: Path, canvas_x: int, canvas_y: int,
            target_width: int, target_height: int) -> None:
    image = Image.open(source).convert("RGBA")
    if image.size != (target_width, target_height):
        image = image.resize(
            (target_width, target_height), Image.Resampling.BILINEAR)
    alpha_box = image.getchannel("A").getbbox()
    if alpha_box is None:
        raise RuntimeError(f"{source.name} has no visible pixels")

    x1, y1, x2, y2 = alpha_box
    cropped = image.crop(alpha_box)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as stream:
        stream.write(struct.pack(
            "<4shhHH", b"PCA1", canvas_x + x1, canvas_y + y1,
            x2 - x1, y2 - y1))
        for red, green, blue, alpha in cropped.getdata():
            stream.write(struct.pack("<HB", rgb565(red, green, blue), alpha))
    print(f"wrote {output} ({x2 - x1}x{y2 - y1}, "
          f"origin={canvas_x + x1},{canvas_y + y1}, {output.stat().st_size} bytes)")


def main() -> None:
    for output_name, (source_name, x, y, width, height) in ASSETS.items():
        convert(ROOT / "_apk_reference" / source_name,
                OUTPUT_DIR / output_name, x, y, width, height)


if __name__ == "__main__":
    main()
