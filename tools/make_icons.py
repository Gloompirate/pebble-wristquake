"""Generate Wristquake's PBI icons.

Produces 12x12 monochrome PNGs in resources/images/:
  - steps.png    (side-profile sneaker, for the step-count slot)
  - heart.png    (heart, for the heart-rate slot)
  - battery.png  (battery silhouette, for the battery slot)

These are built into the .pbw by the Pebble SDK via the `media` array
in appinfo.json and consumed via gbitmap_create_with_resource(...).

Drawn as compact pixel art tuned for the Pebble's tiny screen and
high-contrast monochrome rendering — anything finer than 1-px features
just disappears.
"""
from pathlib import Path
import struct
import zlib

SIZE = 12

# 1 = on (white-on-watch when GCompOpSet), 0 = off (transparent)
# Designed at 12x12 to leave a 1px margin all around inside a 12x12 cell
# so the icon doesn't kiss adjacent text.

# Side-profile sneaker — high-top silhouette with a flat sole stripe along the
# bottom. Reads as "shoe" much better than the abstract footprint pair did.
STEPS = [
    "............",
    "............",
    ".....XXX....",
    "....XXXXX...",
    "....XXXXX...",
    "..XXXXXXXXX.",
    "..XXXXXXXXX.",
    ".XXXXXXXXXXX",
    "XXXXXXXXXXXX",
    "XXXXXXXXXXXX",
    "............",
    "............",
]

# Classic heart — two top bumps with a small notch, tapering to a point.
HEART = [
    "............",
    ".XX....XX...",
    "XXXX..XXXX..",
    "XXXXXXXXXX..",
    "XXXXXXXXXX..",
    "XXXXXXXXXX..",
    ".XXXXXXXX...",
    "..XXXXXX....",
    "...XXXX.....",
    "....XX......",
    "............",
    "............",
]

BATTERY = [
    "............",
    "............",
    "XXXXXXXXXXX.",
    "X.........XX",
    "X.XXXXXXX.XX",
    "X.XXXXXXX.XX",
    "X.XXXXXXX.XX",
    "X.XXXXXXX.XX",
    "X.........XX",
    "XXXXXXXXXXX.",
    "............",
    "............",
]

for name, art in (("STEPS", STEPS), ("HEART", HEART), ("BATTERY", BATTERY)):
    assert len(art) == SIZE, f"{name} must be {SIZE} rows tall"
    assert all(len(r) == SIZE for r in art), f"{name} rows must be {SIZE} wide"


def encode_png(pixels: list[list[int]]) -> bytes:
    """Tiny pure-Python PNG encoder for 1-bit grayscale + alpha.

    Pebble's resource pipeline accepts PNGs; using grayscale-alpha so we
    can keep the icon shape opaque and the background transparent (the
    SDK draws the result via BitmapLayer with GCompOpSet, treating the
    alpha as a mask).
    """
    h = len(pixels)
    w = len(pixels[0])
    raw = bytearray()
    for row in pixels:
        raw.append(0)  # filter byte = None
        for px in row:
            if px:
                raw.append(0xFF)  # gray = white
                raw.append(0xFF)  # alpha = opaque
            else:
                raw.append(0x00)
                raw.append(0x00)  # transparent

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    # bit depth 8, color type 4 (gray+alpha), no interlace
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 4, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    return sig + chunk(b"IHDR", ihdr) + chunk(b"IDAT", idat) + chunk(b"IEND", b"")


def rasterize(rows: list[str]) -> list[list[int]]:
    return [[1 if c == "X" else 0 for c in row] for row in rows]


def main() -> None:
    out_dir = Path(__file__).resolve().parent.parent / "resources" / "images"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, art in [("steps", STEPS), ("heart", HEART), ("battery", BATTERY)]:
        path = out_dir / f"{name}.png"
        path.write_bytes(encode_png(rasterize(art)))
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
