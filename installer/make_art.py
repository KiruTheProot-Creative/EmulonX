#!/usr/bin/env python3
"""Builds the installer artwork and the app icon from the Emulon X assets.

    python installer/make_art.py      (needs Pillow: pip install pillow)

Inputs:  Assets/images/logo.png, Assets/images/background.jpg, Assets/images/knob.png,
         installer/logo-x.png (the logo's X on its own), Assets/fonts/Saira-500.ttf
Outputs: installer/wizard-*.bmp, installer/small-*.bmp, installer/icon-256.png, installer/emulonx.ico
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
A, OUT = ROOT / "Assets", ROOT / "installer"
RED = (255, 42, 42)


def glow(img, radius, colour, strength=1.0):
    """A soft coloured glow from an image's outline, same size as the image."""
    alpha = img.getchannel("A").filter(ImageFilter.GaussianBlur(radius))
    alpha = alpha.point(lambda v: min(255, int(v * strength)))
    g = Image.new("RGBA", img.size, colour + (0,))
    g.putalpha(alpha)
    return g


def padded(img, pad):
    out = Image.new("RGBA", (img.width + 2 * pad, img.height + 2 * pad), (0, 0, 0, 0))
    out.alpha_composite(img, (pad, pad))
    return out


def knob_with_ring(size):
    """The plugin's knob with a glowing red ring, like the volume knob."""
    s = size * 4                      # draw large, scale down for smooth edges
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    knob = Image.open(A / "images/knob.png").convert("RGBA").resize((int(s * 0.86),) * 2, Image.LANCZOS)
    layer.alpha_composite(knob, ((s - knob.width) // 2, (s - knob.height) // 2))
    ring = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(ring)
    m = int(s * 0.17)
    d.arc((m, m, s - m, s - m), start=135, end=405, fill=RED + (255,), width=int(s * 0.022))
    layer.alpha_composite(glow(ring, s * 0.03, RED, 1.6))
    layer.alpha_composite(ring)
    return layer.resize((size, size), Image.LANCZOS)


def wizard(w, h):
    """Tall image on the left of the installer's first and last page."""
    bg = Image.open(A / "images/background.jpg").convert("RGB")
    scale = max(w / bg.width, h / bg.height) * 1.6          # zoom in on the grid
    bg = bg.resize((int(bg.width * scale), int(bg.height * scale)), Image.LANCZOS)
    bg = bg.crop(((bg.width - w) // 2, 0, (bg.width - w) // 2 + w, h)).convert("RGBA")
    # darken towards the bottom so the logo and text stand out
    shade = Image.new("L", (1, h))
    for y in range(h):
        shade.putpixel((0, y), int(255 * min(1.0, max(0.0, (y / h - 0.35) / 0.55)) ** 1.4 * 0.85))
    bg = Image.composite(Image.new("RGBA", (w, h), (0, 0, 0, 255)), bg, shade.resize((w, h)))

    k = knob_with_ring(int(w * 0.5))
    bg.alpha_composite(k, ((w - k.width) // 2, int(h * 0.36 - k.height / 2)))

    logo = Image.open(A / "images/logo.png").convert("RGBA")
    lw = int(w * 0.84)
    logo = logo.resize((lw, round(logo.height * lw / logo.width)), Image.LANCZOS)
    pad = int(logo.height * 0.4)
    lp = padded(logo, pad)
    pos = ((w - lp.width) // 2, int(h * 0.66 - lp.height / 2))
    bg.alpha_composite(glow(lp, logo.height * 0.18, RED, 1.2), pos)
    bg.alpha_composite(lp, pos)

    d = ImageDraw.Draw(bg)
    font = ImageFont.truetype(str(A / "fonts/Saira-500.ttf"), max(9, int(w * 0.047)))
    text = "FREE SAMPLER  -  AGPLv3"
    tw = d.textlength(text, font=font)
    d.text(((w - tw) / 2, h * 0.9), text, font=font, fill=(200, 184, 184))
    return bg.convert("RGB")


def x_icon(size, rounded=True, knob=False):
    """The logo's X on a dark tile (app icon) or inside the knob (small installer image)."""
    s = size * 4
    tile = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(tile)
    if knob:
        tile = Image.new("RGBA", (s, s), (14, 10, 12, 255))
        k = knob_with_ring(s)
        tile.alpha_composite(k)
        xs = 0.42
    else:
        r = int(s * 0.2) if rounded else 0
        d.rounded_rectangle((0, 0, s - 1, s - 1), r, fill=(18, 12, 14, 255), outline=(90, 22, 22, 255), width=max(2, s // 64))
        xs = 0.66
    x = Image.open(OUT / "logo-x.png").convert("RGBA")
    xh = int(s * xs)
    x = x.resize((round(x.width * xh / x.height), xh), Image.LANCZOS)
    pad = int(xh * 0.3)
    xp = padded(x, pad)
    pos = ((s - xp.width) // 2, (s - xp.height) // 2)
    tile.alpha_composite(glow(xp, xh * 0.08, RED, 1.3), pos)
    tile.alpha_composite(xp, pos)
    return tile.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    for w, h in ((164, 314), (192, 386), (246, 459), (328, 604)):
        wizard(w, h).save(OUT / f"wizard-{w}.bmp")
    for s in (55, 64, 83, 110):
        x_icon(s, knob=True).convert("RGB").save(OUT / f"small-{s}.bmp")
    icon = x_icon(256)
    icon.save(OUT / "icon-256.png")
    icon.save(OUT / "emulonx.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("installer artwork written to", OUT)
