"""Build the PACON watch character layer from the user-supplied KKD1 APK asset.

The output is a compact cropped RGB565 + alpha blob consumed directly by the
firmware.  Header: magic "PCA1", x/y/w/h as little-endian uint16, followed by
three bytes per pixel (RGB565 little-endian, alpha).
"""

from pathlib import Path
import struct

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
ASSETS = {
    "kkd1_character.rgb565a": "kkd1_wfs_1_663f43f6_d863_40ca_8aa7_536ae92864a1.png",
    "kkd1_second_dial.rgb565a": "kkd1_wfs_2_e896df7b_a1ab_455e_a7b5_0c600d6ed926.png",
    "kkd1_minute_dial.rgb565a": "kkd1_wfs_3_01372f50_dfa4_48b5_860a_b13a465ec3e7.png",
    "kkd1_hour_dial.rgb565a": "kkd1_wfs_4_aacbd14c_2023_40dc_a042_315696b0db93.png",
    "kkd1_mechanism.rgb565a": "kkd1_wfs_5_3291f537_d0c1_4eaf_ab1d_b74fc53ef7ad.png",
}
OUTPUT_DIR = ROOT / "my_Pacon" / "main" / "assets"
COMPLICATION_BOX = (106, 121, 216, 231)
COMPLICATION_SOURCE = "kkd1_wfs_6_7fe875ab_7705_4af3_910b_1a0da412376b.png"


def rgb565(red: int, green: int, blue: int) -> int:
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def write_blob(image: Image.Image, output: Path, box: tuple[int, int, int, int]) -> None:
    x1, y1, x2, y2 = box
    cropped = image
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as stream:
        stream.write(struct.pack("<4sHHHH", b"PCA1", x1, y1, x2 - x1, y2 - y1))
        for red, green, blue, alpha in cropped.getdata():
            stream.write(struct.pack("<HB", rgb565(red, green, blue), alpha))
    print(f"wrote {output} ({x2 - x1}x{y2 - y1}, {output.stat().st_size} bytes)")


def convert(source: Path, output: Path) -> None:
    image = Image.open(source).convert("RGBA")
    alpha_box = image.getchannel("A").getbbox()
    if alpha_box is None:
        raise RuntimeError("watch layer has no visible pixels")
    write_blob(image.crop(alpha_box), output, alpha_box)


def main() -> None:
    for output_name, source_name in ASSETS.items():
        convert(ROOT / "_apk_reference" / source_name, OUTPUT_DIR / output_name)

    x1, y1, x2, y2 = COMPLICATION_BOX
    complication = Image.open(ROOT / "_apk_reference" / COMPLICATION_SOURCE).convert("RGBA")
    complication = complication.resize(
        (x2 - x1, y2 - y1), Image.Resampling.LANCZOS
    )
    write_blob(
        complication,
        OUTPUT_DIR / "kkd1_complication.rgb565a",
        COMPLICATION_BOX,
    )


if __name__ == "__main__":
    main()
