from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps


ROOT = Path(__file__).resolve().parents[4]
OUT = Path(__file__).resolve().parent
PREVIEW = OUT
REF = ROOT / "_ouo_reference" / "capture_1_611"


def font(size: int):
    for candidate in (r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\simhei.ttf"):
        if Path(candidate).exists():
            return ImageFont.truetype(candidate, size)
    return ImageFont.load_default()


def preview_face(name: str) -> Image.Image:
    # Browser screenshot clips include a right-side control panel. Keep only
    # the stable circular face box for the side-by-side comparison.
    image = Image.open(PREVIEW / f"preview_{name}.png").convert("RGB")
    return image.crop((4, 28, 382, 406)).resize((500, 500), Image.Resampling.LANCZOS)


def fit_reference(path: Path, size=(560, 290)) -> Image.Image:
    image = Image.open(path).convert("RGB")
    return ImageOps.contain(image, size, Image.Resampling.LANCZOS)


def paste_center(dst: Image.Image, image: Image.Image, box, background=(0, 0, 0)):
    x, y, w, h = box
    panel = Image.new("RGB", (w, h), background)
    panel.paste(image, ((w - image.width) // 2, (h - image.height) // 2))
    dst.paste(panel, (x, y))


rows = [
    ("宽扁横向", "pullHorizontal", OUT / "android_reference_horizontal.png", "Android 基准 1"),
    ("竖胶囊", "pullVertical", OUT / "android_reference_vertical.png", "Android 基准 2"),
    ("圆角方嘴", "pullSquare", OUT / "android_reference_square.png", "Android 基准 3"),
    ("上尖角", "pullTriangle", OUT / "android_reference_triangle.png", "Android 基准 4"),
]

W, ROW_H = 1160, 390
canvas = Image.new("RGB", (W, ROW_H * len(rows)), (16, 17, 21))
draw = ImageDraw.Draw(canvas)
title_font = font(22)
small_font = font(15)

for index, (title, preview_name, reference, reference_label) in enumerate(rows):
    y = index * ROW_H
    draw.rectangle((0, y, W - 1, y + ROW_H - 1), outline=(47, 50, 59), width=1)
    draw.text((18, y + 14), f"{index + 1}. {title}", fill=(240, 242, 248), font=title_font)
    draw.text((155, y + 18), "预览器", fill=(135, 190, 255), font=small_font)
    draw.text((725, y + 18), reference_label, fill=(255, 214, 120), font=small_font)
    paste_center(canvas, preview_face(preview_name), (32, y + 54, 500, 315), (0, 0, 0))
    paste_center(canvas, fit_reference(reference), (600, y + 54, 530, 315), (0, 0, 0))

canvas.save(OUT / "pull_experiment_comparison.png", optimize=True)

(OUT / "pull_experiment_comparison.md").write_text(
    """# 拉嘴实验对比

左列是预览器固定方向实验，右列是本轮提供的 Android app 基准截图。

![对比图](pull_experiment_comparison.png)

## 观察

- 四个固定参考形现在使用独立轮廓：横向宽扁填充、纵向窄胶囊、圆角方嘴和上尖角，不再把横向研究按钮误画成极限侧拉 `3` 嘴。
- 四个嘴形的宽高比、嘴/眼垂直间距和参考帧压扁眼睛由截图回归脚本校验；普通情绪嘴仍使用原来的锚点。
- 这张图验证的是代表形状，真实拖动仍需要在真机上按连续轨迹复核。
""",
    encoding="utf-8",
)
