#!/usr/bin/env python3
"""Render zoot-icon.svg into the Android launcher resources and review sheet.

Requires rsvg-convert and Pillow. Run from any directory; all geometry comes
from the adjacent SVG, which is the canonical source.
"""

from copy import deepcopy
from io import BytesIO
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw, ImageFont


HERE = Path(__file__).resolve().parent
RES = HERE.parents[1] / "android/app/src/main/res"
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)
source = ET.parse(HERE / "zoot-icon.svg").getroot()
emblem = source.find(f"{{{SVG_NS}}}g[@id='emblem']")
assert emblem is not None


def raster(svg: ET.Element, size: int) -> Image.Image:
    result = subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size)],
        input=ET.tostring(svg), check=True, capture_output=True,
    )
    return Image.open(BytesIO(result.stdout)).convert("RGBA")


def vector() -> str:
    paths = []
    for element in emblem:
        assert element.tag == f"{{{SVG_NS}}}path"
        paths.append(
            f'    <path android:fillColor="#FFFFFFFF" android:pathData="{element.attrib["d"]}" />'
        )
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!-- Generated from design/icon/zoot-icon.svg by render_icons.py. -->\n'
        '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
        '    android:width="108dp" android:height="108dp"\n'
        '    android:viewportWidth="108" android:viewportHeight="108">\n'
        + "\n".join(paths) + "\n</vector>\n"
    )


# Android adaptive icons apply the launcher's mask to a 72dp visible region
# inside this 108dp layer. The vector keeps the artwork crisp at any density.
(RES / "drawable/ic_launcher_foreground.xml").write_text(vector())
(RES / "drawable/ic_launcher_monochrome.xml").write_text(vector())

legacy = deepcopy(source)
legacy.set("viewBox", "18 18 72 72")
legacy.set("width", "72")
legacy.set("height", "72")
background = legacy.find(f"{{{SVG_NS}}}path[@id='background']")
legacy.remove(background)
background = ET.Element(f"{{{SVG_NS}}}rect", {
    "x": "18", "y": "18", "width": "72", "height": "72",
    "rx": "14", "fill": "#000000",
})
legacy.insert(0, background)

for density, size in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
                      ("xxhdpi", 144), ("xxxhdpi", 192)):
    raster(legacy, size).save(RES / f"mipmap-{density}/ic_launcher.png")
    # The old density-specific foregrounds are superseded by the vector.
    (RES / f"mipmap-{density}/ic_launcher_foreground.png").unlink(missing_ok=True)

# Review circle, rounded-square, and monochrome masks at full and launcher size.
adaptive = raster(source, 648).crop((108, 108, 540, 540))
sheet = Image.new("RGB", (1200, 650), "#E9E9E9")
draw = ImageDraw.Draw(sheet)
font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
font = ImageFont.truetype(font_path, 23)
small_font = ImageFont.truetype(font_path, 17)
for index, (label, radius, monochrome) in enumerate((
    ("Circle", 160, False), ("Rounded square", 62, False),
    ("Themed monochrome", 160, True),
)):
    art = adaptive.resize((320, 320), Image.Resampling.LANCZOS)
    if monochrome:
        pixels = art.load()
        for y in range(320):
            for x in range(320):
                value = pixels[x, y][0] / 255
                pixels[x, y] = tuple(round(a + (b - a) * value)
                                     for a, b in zip((214, 229, 218), (31, 54, 44))) + (255,)
    mask = Image.new("L", (320, 320))
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, 319, 319), radius, fill=255)
    x = index * 390 + 50
    sheet.paste(art, (x, 75), mask)
    draw.text((x, 420), label, font=font, fill="#202020")
    for offset, size in enumerate((48, 72)):
        thumbnail = art.resize((size, size), Image.Resampling.LANCZOS)
        thumbnail_mask = mask.resize((size, size), Image.Resampling.LANCZOS)
        thumb_x = x + offset * 112
        sheet.paste(thumbnail, (thumb_x, 483), thumbnail_mask)
        draw.text((thumb_x, 568), f"{size}px", font=small_font, fill="#444444")
draw.text((50, 22), "zoot / command core", font=font, fill="#202020")
sheet.save(HERE / "preview.png")
print("Updated Android launcher vectors, five legacy densities, and preview.png")
