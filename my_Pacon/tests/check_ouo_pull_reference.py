"""Compare preview pull snapshots with the four Android reference silhouettes.

The reference screenshots normalize to roughly these mouth/eye-diameter ratios.
The snapshots are generated from the real previewer buttons and then measured
from their white connected components, so a discrete-state regression goes red
without relying on a subjective visual check.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SNAPSHOTS = ROOT / "tools" / "ouo-preview" / "experiments"

# mouth width/height divided by the diameter of an eye in the supplied Android
# captures. The tolerance allows for antialiasing and screenshot scaling.
EXPECTED = {
    "pullHorizontal": (2.08, 1.15, 1.10),
    "pullVertical": (0.68, 1.37, 1.16),
    "pullSquare": (1.41, 1.22, 1.13),
    "pullTriangle": (1.47, 1.29, 1.15),
}


def components(image: Image.Image):
    image = image.convert("L").crop((4, 28, 382, 406))
    width, height = image.size
    pixels = image.load()
    seen = set()
    found = []
    for y in range(height):
        for x in range(width):
            if (x, y) in seen or pixels[x, y] < 240:
                continue
            stack = [(x, y)]
            seen.add((x, y))
            points = []
            while stack:
                px, py = stack.pop()
                points.append((px, py))
                for nx, ny in ((px + 1, py), (px - 1, py), (px, py + 1), (px, py - 1)):
                    if 0 <= nx < width and 0 <= ny < height and (nx, ny) not in seen and pixels[nx, ny] >= 240:
                        seen.add((nx, ny))
                        stack.append((nx, ny))
            if len(points) > 100:
                found.append(
                    {
                        "area": len(points),
                        "bbox": (
                            min(px for px, _ in points),
                            min(py for _, py in points),
                            max(px for px, _ in points),
                            max(py for _, py in points),
                        ),
                    }
                )
    return found


def measure(name: str):
    comps = components(Image.open(SNAPSHOTS / f"preview_{name}.png"))
    eyes = [
        c
        for c in comps
        if 30 <= c["bbox"][2] - c["bbox"][0] + 1 <= 60
        and 30 <= c["bbox"][3] - c["bbox"][1] + 1 <= 60
        and ((c["bbox"][0] + c["bbox"][2]) / 2 < 150 or (c["bbox"][0] + c["bbox"][2]) / 2 > 230)
    ]
    if len(eyes) < 2:
        raise AssertionError(f"{name}: expected two eye components")
    selected_eyes = sorted(eyes, key=lambda c: c["area"], reverse=True)[:2]
    eye_diameter = sum(c["bbox"][2] - c["bbox"][0] + 1 for c in selected_eyes) / 2
    eye_center_y = sum((c["bbox"][1] + c["bbox"][3]) / 2 for c in selected_eyes) / 2
    mouths = [c for c in comps if c["bbox"][1] >= 180 and 120 <= (c["bbox"][0] + c["bbox"][2]) / 2 <= 260]
    if not mouths:
        raise AssertionError(f"{name}: expected a mouth component")
    mouth = max(mouths, key=lambda c: c["area"])
    width = mouth["bbox"][2] - mouth["bbox"][0] + 1
    height = mouth["bbox"][3] - mouth["bbox"][1] + 1
    mouth_center_y = (mouth["bbox"][1] + mouth["bbox"][3]) / 2
    eye_height = sum(c["bbox"][3] - c["bbox"][1] + 1 for c in selected_eyes) / 2
    return (
        width / eye_diameter,
        height / eye_diameter,
        (mouth_center_y - eye_center_y) / eye_diameter,
        eye_height / eye_diameter,
        mouth["bbox"],
    )


for state, (expected_width, expected_height, expected_offset) in EXPECTED.items():
    actual_width, actual_height, actual_offset, actual_eye_ratio, bbox = measure(state)
    if (
        abs(actual_width - expected_width) > expected_width * 0.12
        or abs(actual_height - expected_height) > expected_height * 0.12
        or abs(actual_offset - expected_offset) > 0.12
        or abs(actual_eye_ratio - 0.87) > 0.08
    ):
        raise AssertionError(
            f"{state}: mouth ratio {actual_width:.2f}x{actual_height:.2f}, "
            f"offset={actual_offset:.2f}, eye={actual_eye_ratio:.2f} (bbox={bbox}), expected about "
            f"{expected_width:.2f}x{expected_height:.2f}, offset={expected_offset:.2f}, eye=0.87"
        )

print("OUO pull reference silhouette check passed.")
