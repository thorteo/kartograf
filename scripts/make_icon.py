#!/usr/bin/env python3
"""Renders src/app/icon/*.svg into src/app/icon/tarkov_help.ico.

The .ico is committed, so a build needs neither this script nor its tools. Run
it by hand after editing either SVG. Needs rsvg-convert (librsvg2-bin) and
Pillow.

    scripts/make_icon.py

Sizes: 16/20/24 come from icon-small.svg, drawn for the pixel grid; the rest
from icon.svg. 20 is the tray at 125% scaling, 40 and 64 the shell at larger
scalings. Images up to 64 px are stored as uncompressed DIBs, 256 as PNG --
the layout Windows itself uses, and the one every resource compiler accepts.
"""
import io
import pathlib
import struct
import subprocess
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "src" / "app" / "icon"
SMALL_SIZES = (16, 20, 24)
LARGE_SIZES = (32, 40, 48, 64, 256)


def render(svg: pathlib.Path, size: int) -> Image.Image:
    png = subprocess.run(
        ["rsvg-convert", "-w", str(size), "-h", str(size), str(svg)],
        check=True, capture_output=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def dib(image: Image.Image) -> bytes:
    """A 32-bit BGRA DIB as an .ico stores it: bottom-up, height doubled to
    count the AND mask, which follows as 1 bpp rows padded to 32 bits. The
    alpha channel does the real work; the mask is left clear."""
    width, height = image.size
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes("raw", "BGRA")
    mask_row = ((width + 31) // 32) * 4
    return header + pixels + bytes(mask_row * height)


def main() -> int:
    images = [(s, dib(render(ICON_DIR / "icon-small.svg", s))) for s in SMALL_SIZES]
    for size in LARGE_SIZES:
        image = render(ICON_DIR / "icon.svg", size)
        if size == 256:
            buffer = io.BytesIO()
            image.save(buffer, "PNG", optimize=True)
            images.append((size, buffer.getvalue()))
        else:
            images.append((size, dib(image)))

    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, data in images:
        # A 256 px dimension does not fit in a byte; the format stores it as 0.
        edge = 0 if size == 256 else size
        out += struct.pack("<BBBBHHII", edge, edge, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data

    target = ICON_DIR / "tarkov_help.ico"
    target.write_bytes(out)
    print(f"wrote {target.relative_to(ROOT)} ({len(out)} bytes, {len(images)} sizes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
