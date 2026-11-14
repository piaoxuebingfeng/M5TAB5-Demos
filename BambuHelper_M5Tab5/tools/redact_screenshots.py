#!/usr/bin/env python3
"""Replace the device's own LAN address in documentation screenshots.

Screenshots taken from the panel show the unit's address in the header, and the
settings page also renders it as a QR code. That address belongs to whoever's
network the device happens to sit on, so it must not be published with the docs.

Run after adding a screenshot that shows the address:

    python3 tools/redact_screenshots.py docs

Each entry in TARGETS is a search region; the script finds the text line inside
it, measures the exact ink box, repaints that box with the surrounding colour
and redraws the replacement string in the same colour and size.

When you add a new screenshot, measure a search box that comfortably contains
the address (a few pixels of margin on each side) and add it to TARGETS.

A processed file is tagged with a PNG text chunk, so re-running skips it. That
matters because a second pass would measure the *replacement* glyphs and shrink
the text each time - the tag keeps the result stable and makes the "did I
already do this?" question answerable.
"""
import sys
from collections import Counter

import qrcode
from PIL import Image, ImageDraw, ImageFont, PngImagePlugin

ADDRESS = "192.168.1.100"
URL = "http://" + ADDRESS
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
TAG = "bambohelper-redacted"

# file -> (search box (x1, y1, x2, y2), replacement text)
TARGETS = {
    "bringup-idle-2026-11-14.png": ((160, 235, 400, 275), ADDRESS),
    "bringup-cloud-p1s-2026-11-14.png": ((50, 6, 150, 22), ADDRESS),
    "dashboard-a1mini-1280x720.png": ((196, 30, 380, 68), ADDRESS),
    "bringup-progress-pct-fixed-2026-11-14.png": ((75, 12, 200, 34), ADDRESS),
    "cjk-job-title-2026-11-14.png": ((180, 25, 380, 70), ADDRESS),
    "multi-printer-switch-2026-11-14.png": ((160, 25, 360, 70), ADDRESS),
    "backlight-ramp-after-fix-2026-11-14.png": ((70, 15, 215, 40), ADDRESS),
    "touch-settings-page-2026-11-14.png": ((60, 8, 132, 22), ADDRESS),
}

# Files that need more than the header pass. The settings page also draws the
# address as a green URL line and as a QR code pointing at the same address.
EXTRA_PASSES = {
    "touch-settings-page-2026-11-14.png": [
        ("text", (28, 88, 185, 118), URL),
        ("qr", (303, 69, 395, 161), None),
    ],
}


def analyse(img, box):
    """Background colour, ink colour and exact ink box of the densest text line."""
    px = img.load()
    x1, y1, x2, y2 = box
    bg = Counter(px[x, y] for y in range(y1, y2) for x in range(x1, x2)).most_common(1)[0][0]

    def ink(x, y):
        return sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 55

    rows = {y: sum(1 for x in range(x1, x2) if ink(x, y)) for y in range(y1, y2)}
    bands, cur = [], None
    for y in range(y1, y2):
        if rows[y]:
            cur = [y, y] if cur is None else [cur[0], y]
        elif cur:
            bands.append(tuple(cur))
            cur = None
    if cur:
        bands.append(tuple(cur))
    if not bands:
        return bg, bg, None

    lo, hi = max(bands, key=lambda b: sum(rows[y] for y in range(b[0], b[1] + 1)))
    pts = [(x, y, px[x, y]) for y in range(lo, hi + 1) for x in range(x1, x2) if ink(x, y)]
    return bg, Counter(p[2] for p in pts).most_common(1)[0][0], (
        min(p[0] for p in pts), min(p[1] for p in pts),
        max(p[0] for p in pts) + 1, max(p[1] for p in pts) + 1)


def draw_text(img, text, bbox, bg, fg, pad=3):
    x1, y1, x2, y2 = bbox
    box = (x1 - pad, y1 - pad, x2 + pad, y2 + pad)
    ImageDraw.Draw(img).rectangle(box, fill=bg)
    size = max(6, y2 - y1)
    while size > 5 and ImageFont.truetype(FONT, size).getbbox(text)[2] > box[2] - box[0]:
        size -= 1
    font = ImageFont.truetype(FONT, max(5, size))
    b = font.getbbox(text)
    ImageDraw.Draw(img).text(
        (box[0] + (box[2] - box[0] - (b[2] - b[0])) // 2 - b[0],
         box[1] + (box[3] - box[1] - (b[3] - b[1])) // 2 - b[1]), text, font=font, fill=fg)


def draw_qr(img, box):
    """Same size and quiet zone as the original, pointing at the stand-in address."""
    x1, y1, x2, y2 = box
    side = min(x2 - x1, y2 - y1)
    qr = qrcode.QRCode(border=1, box_size=1, error_correction=qrcode.constants.ERROR_CORRECT_M)
    qr.add_data(URL)
    qr.make(fit=True)
    code = qr.make_image(fill_color="black", back_color="white").convert("RGB")
    code = code.resize((side, side), Image.NEAREST)
    ImageDraw.Draw(img).rectangle(box, fill=(255, 255, 255))
    img.paste(code, (x1 + (x2 - x1 - side) // 2, y1 + (y2 - y1 - side) // 2))


def process(docs, name, passes):
    path = f"{docs}/{name}"
    src = Image.open(path)
    if src.info.get(TAG):
        print(f"skip   {name}: already redacted")
        return
    img = src.convert("RGB")
    for kind, box, text in passes:
        if kind == "qr":
            draw_qr(img, box)
            continue
        bg, fg, bbox = analyse(img, box)
        if bbox is None:
            print(f"!!     {name}: no text inside {box}")
            continue
        draw_text(img, text, bbox, bg, fg)

    meta = PngImagePlugin.PngInfo()
    for key, value in src.info.items():
        if isinstance(value, str) and key != TAG:
            meta.add_text(key, value)
    meta.add_text(TAG, ADDRESS)
    img.save(path, pnginfo=meta)
    print(f"redact {name}")


def main(docs):
    for name, (box, text) in TARGETS.items():
        process(docs, name, [("text", box, text)] + EXTRA_PASSES.get(name, []))
    for name, passes in EXTRA_PASSES.items():
        if name not in TARGETS:
            process(docs, name, passes)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "docs")
