"""Regenerate the checked-in app icons from the geometric brand mark (Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1] / "web"
for filename, size, maskable in [("icon-192.png", 192, False), ("icon-512.png", 512, False),
                                  ("icon-maskable.png", 512, True), ("apple-touch-icon.png", 180, False)]:
    scale = 4
    image = Image.new("RGB", (size * scale, size * scale), "#256a55")
    draw = ImageDraw.Draw(image)
    factor = size * scale / 512
    def box(coords):
        return tuple(round(v * factor) for v in coords)
    circle = box((114, 114, 398, 398))
    draw.ellipse(circle, fill="#f5f5f2")
    draw.pieslice(circle, 0, 180, fill="#ffb464")
    draw.line(box((256, 144, 256, 256)), fill="#256a55", width=round(26 * factor))
    for y in (144, 256):
        draw.ellipse(box((243, y - 13, 269, y + 13)), fill="#256a55")
    image.resize((size, size), Image.Resampling.LANCZOS).save(root / filename, optimize=True)
