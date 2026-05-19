#!/usr/bin/env python3
"""Slice the More screen icons from the exported Figma 2x frame."""

from __future__ import annotations

from pathlib import Path

from PIL import Image


SRC = Path("artifacts/figma_more_detail_2x.png")
OUT = Path("app/asset/more")


ICONS = [
    ("more_calendar.png", 76, 127, 48, 48),
    ("more_record.png", 239, 127, 50, 50),
    ("more_weather.png", 400, 129, 55, 48),
    ("more_alarm.png", 73, 270, 55, 55),
    ("more_picture.png", 239, 276, 50, 44),
    ("more_font.png", 398, 276, 59, 59),
    ("more_folder.png", 75, 422, 48, 48),
    ("more_pomodoro.png", 223, 404, 80, 80),
    ("more_bluetooth.png", 399, 416, 55, 55),
    ("more_standby.png", 72, 562, 59, 59),
    ("more_reading_bean.png", 223, 548, 80, 80),
    ("more_signal.png", 397, 568, 55, 55),
]


def main() -> None:
    src = Image.open(SRC).convert("RGBA")
    OUT.mkdir(parents=True, exist_ok=True)
    for name, x, y, w, h in ICONS:
        crop = src.crop((x * 2, y * 2, (x + w) * 2, (y + h) * 2))
        crop = crop.resize((w, h), Image.Resampling.LANCZOS)
        pixels = crop.load()
        for py in range(h):
            for px in range(w):
                r, g, b, a = pixels[px, py]
                if a == 0:
                    continue
                if r > 245 and g > 245 and b > 245:
                    pixels[px, py] = (255, 255, 255, 0)
        crop.save(OUT / name)


if __name__ == "__main__":
    main()
