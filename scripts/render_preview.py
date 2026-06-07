#!/usr/bin/env python3
"""Deterministic desktop preview renderer for the NERV Pebble watchface.

This is not an emulator capture. It mirrors the C layout constants closely enough
for visual review when the Pebble SDK emulator install path is unavailable.
"""
from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "preview_sync_ratio.png"
W, H = 200, 228
BOTTOM_Y = 195
STEPS_GOAL = 10000

BLACK = (0, 0, 0, 255)
WHITE = (255, 255, 255, 255)
RED = (255, 36, 36, 255)
AMBER = (255, 170, 0, 255)
YELLOW = (255, 212, 0, 255)
PINK = (255, 138, 155, 255)
CYAN = (47, 230, 230, 255)
GREY = (140, 140, 140, 255)


def font(size: int, bold: bool = False, condensed: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf" if condensed and bold else "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf" if condensed else "",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationMono-Bold.ttf" if bold else "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
    ]
    for p in candidates:
        if not p:
            continue
        if Path(p).exists():
            return ImageFont.truetype(p, size=size)
    return ImageFont.load_default()

FONT_TINY = font(7, bold=True)
FONT_SMALL = font(13, bold=False, condensed=True)
FONT_SMALL_BOLD = font(14, bold=True, condensed=True)
FONT_TIME = font(46, bold=False, condensed=True)


def text(draw: ImageDraw.ImageDraw, xy, s: str, fill=WHITE, f=FONT_SMALL, anchor=None):
    draw.text(xy, s, font=f, fill=fill, anchor=anchor)


def centered(draw, box, s, fill=WHITE, f=FONT_SMALL):
    x, y, w, h = box
    bbox = draw.textbbox((0, 0), s, font=f)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text((x + (w - tw) / 2, y + (h - th) / 2 - 1), s, font=f, fill=fill)


def draw_chevron(draw, x, y, w, h, fill, label):
    pts = [(x, y), (x + w // 2, y + int(h * 0.45)), (x + w, y),
           (x + w, y + int(h * 0.55)), (x + w // 2, y + h), (x, y + int(h * 0.55))]
    draw.polygon(pts, fill=fill)
    draw.rectangle([x + 5, y + 2, x + 8, y + 5], fill=YELLOW)
    draw.rounded_rectangle([x + 14, y + 1, x + 42, y + 8], radius=2, fill=YELLOW)
    centered(draw, (x + 14, y + 1, 28, 8), label, fill=BLACK, f=FONT_TINY)


def draw_l_series(draw, pct=72):
    labels = ["L-01", "L-02", "L-03", "L-04", "L-05", "L-06", "L-07"]
    pct = min(100, max(0, int(pct)))
    lit_count = int((pct * 7 + 50) / 100)
    x, y, w, h, gap = 118, 23, 54, 10, 2
    for i, label in enumerate(labels):
        lit = (7 - i) <= lit_count
        if lit:
            draw_chevron(draw, x, y + i * (h + gap), w, h, RED, label)
    sx, sy, sh = 194, 23, 82
    draw.line([(sx, sy), (sx, sy + sh)], fill=RED, width=2)
    for v in range(0, 101, 20):
        ty = sy + sh - (v * sh) // 100
        draw.line([(sx - 9, ty), (sx, ty)], fill=RED, width=1)
        s = str(v)
        bbox = draw.textbbox((0, 0), s, font=FONT_TINY)
        draw.text((sx - 14 - (bbox[2] - bbox[0]), ty - 5), s, font=FONT_TINY, fill=RED)
    for v in range(10, 100, 20):
        ty = sy + sh - (v * sh) // 100
        draw.line([(sx - 5, ty), (sx, ty)], fill=RED, width=1)


def draw_heart(draw, cx, cy, color=PINK):
    draw.ellipse([cx - 7, cy - 5, cx - 1, cy + 1], fill=color)
    draw.ellipse([cx + 1, cy - 5, cx + 7, cy + 1], fill=color)
    draw.polygon([(cx - 8, cy - 1), (cx + 8, cy - 1), (cx, cy + 8)], fill=color)


def draw_bio(draw, box=(152, 132, 44, 50), hr=76):
    x, y, w, h = box
    draw.rectangle([x, y, x + w, y + h], outline=RED, width=1)
    centered(draw, (x, y + 3, w, 7), "BIO", fill=RED, f=FONT_TINY)
    draw_heart(draw, x + w // 2, y + 22, PINK)
    centered(draw, (x, y + 31, w, 12), str(hr), fill=PINK, f=FONT_SMALL)


def draw_sync(draw, y=178, pct=85):
    text(draw, (6, y - 1), "SYNC", RED, FONT_SMALL)

    bar_x, bar_y, block_w, block_h, gap, blocks = 48, y + 5, 7, 6, 2, 12
    lit_blocks = int((pct * blocks + 50) / 100)
    for i in range(blocks):
        bx = bar_x + i * (block_w + gap)
        draw.rectangle([bx, bar_y, bx + block_w, bar_y + block_h], outline=RED)
        if i < lit_blocks:
            draw.rectangle([bx + 1, bar_y + 1, bx + block_w - 1, bar_y + block_h - 1], fill=RED)


def draw_hazard(draw, x, y, s=5):
    for row in range(s):
        for col in range(s):
            draw.point((x + col, y + row), fill=RED if (row + col) % 2 else WHITE)


def draw_bottom_panel(draw, date="JUN 07", steps="8,471", temp="18°"):
    px, py, pw, ph = 4, BOTTOM_Y, 192, 30
    draw.line([(px, py), (px + pw - 1, py)], fill=RED)
    draw.line([(px, py + 1), (px + pw - 1, py + 1)], fill=RED)
    draw_hazard(draw, px, py, 5)
    draw_hazard(draw, px + pw - 5, py, 5)
    d1x, d2x = px + 72, px + 138
    draw.line([(d1x, py + 2), (d1x, py + ph - 2)], fill=RED)
    draw.line([(d2x, py + 2), (d2x, py + ph - 2)], fill=RED)
    cols = [(px + 2, py + 2, d1x - px - 2, ph - 4, "DATE", date, PINK),
            (d1x + 2, py + 2, d2x - d1x - 2, ph - 4, "STEPS", steps, YELLOW),
            (d2x + 2, py + 2, px + pw - d2x - 2, ph - 4, "WEATHER", temp, CYAN)]
    for x, y, w, h, label, value, col in cols:
        draw.line([(x, y + 3), (x + w - 1, y + 3)], fill=RED)
        centered(draw, (x, y + 4, w, 7), label, fill=RED, f=FONT_TINY)
        centered(draw, (x, y + 12, w, 12), value, fill=col, f=FONT_SMALL)


def main():
    img = Image.new("RGBA", (W, H), BLACK)
    draw = ImageDraw.Draw(img)

    # subtle scan/grid texture, extremely low contrast
    for yy in range(0, H, 8):
        draw.line([(0, yy), (W, yy)], fill=(12, 12, 12, 255))
    for xx in range(0, W, 16):
        draw.line([(xx, 0), (xx, H)], fill=(8, 8, 8, 255))

    text(draw, (4, 0), "MAGI", RED, FONT_SMALL)
    logo = Image.open(ROOT / "resources/images/nerv-logo-red.png").convert("RGBA")
    img.alpha_composite(logo, (8, 12))

    pwr = "PWR"
    pwr_box = draw.textbbox((0, 0), pwr, font=FONT_SMALL)
    draw.text((196 - (pwr_box[2] - pwr_box[0]), 0), pwr, font=FONT_SMALL, fill=RED)
    draw.line([(110, 17), (110, 118)], fill=RED)
    draw_l_series(draw)
    draw.line([(4, 118), (196, 118)], fill=RED)

    text(draw, (9, 118), "LOCAL TIME // ACTIVE", WHITE, FONT_TINY)
    centered(draw, (0, 121, 150, 54), "21:19", fill=WHITE, f=FONT_TIME)
    draw_bio(draw, box=(152, 126, 44, 48))

    draw_sync(draw, y=178, pct=85)
    draw.line([(4, 194), (196, 194)], fill=RED)
    draw_bottom_panel(draw)

    img.save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
