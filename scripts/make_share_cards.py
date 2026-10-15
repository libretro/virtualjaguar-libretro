#!/usr/bin/env python3
"""Regenerate the site's social share cards and favicon set.

Outputs (committed; build_site.py only copies them):
  site/assets/share/home.png      1200x630 og:image for the home page (and
                                  every page without a card of its own)
  site/assets/share/app.png       1200x630 og:image for app.html
  site/assets/icons/favicon.svg   the app icon, cropped to its rounded square
  site/assets/icons/favicon.ico   16/32/48
  site/assets/icons/icon-{192,512}.png
  site/assets/icons/apple-touch-icon.png
                                  180x180, opaque full-bleed square (iOS
                                  fills transparency with black and applies
                                  its own corner mask)

Sources: the game captures in site/assets/home/, the desktop app captures in
site/assets/app_*.png, and the app icon app/packaging/icons/virtualjaguar.svg
(the same file app/packaging/icons/make-icons.sh renders).  Swap the icon
SVG or the screenshots, rerun this, commit the results.

Needs Pillow and rsvg-convert (`brew install librsvg`), plus a copy of Impact
(the site's display face; macOS ships it).  Neither is needed by
build_site.py or check_site.py, which stay stdlib-only.

  python3 scripts/make_share_cards.py
  python3 scripts/make_share_cards.py --display-font /path/to/Impact.ttf \\
      --text-font /path/to/Some-Bold.ttf

Fails unless each card comes out exactly 1200x630 and under 1 MB, the limits
the social networks document for a summary_large_image card.
"""

import argparse
import io
import re
import shutil
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("make_share_cards.py needs Pillow: python3 -m pip install Pillow")

ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
ICON_SVG = ROOT / "app" / "packaging" / "icons" / "virtualjaguar.svg"
SHARE_DIR = SITE / "assets" / "share"
ICON_DIR = SITE / "assets" / "icons"

W, H = 1200, 630
MAX_BYTES = 1000 * 1000

# Site palette (site/style.css, dark theme).
BG = (11, 11, 14)          # --bg
FG = (236, 236, 240)
FG_DIM = (156, 156, 166)   # --fg-dim
RED_RULE = (229, 37, 33)   # --red-rule
RED_LINK = (255, 68, 56)   # --red-link

# Home card: two rows of four, bright and dark captures alternating.  The
# CRT-shader captures are kept to ones whose mask survives the shrink
# without moire (check after any swap).
HOME_SHOTS = [
    "home/doom.webp", "home/raiden-crt-aperture.webp",
    "home/tempest-2000-crt-lottes.webp", "home/super-burnout.webp",
    "home/hero-ironsoldier.webp", "home/wolfenstein-3d.webp",
    "home/hero-avp.webp", "home/atari-karts.webp",
]
# App card: the library large on the left; CRT shader, a debugger and a
# theme on the right.  Each is (file, focus): focus None fits the whole
# capture, else (cx, cy, scale) zooms on that source pixel -- whole-window
# shrinks leave the UI unreadable at thumbnail size.
APP_BIG = ("app_library.png", (0, 600, 0.80))         # the cover grid
APP_RIGHT_TOP = ("app_game_lottes.png", (620, 385, 0.85))  # Lottes CRT
APP_RIGHT_BOTTOM = [("app_dbg_palette.png", (640, 430, 0.42)),
                    ("app_theme_light.png", (420, 600, 0.55))]

DISPLAY_FONTS = ["/System/Library/Fonts/Supplemental/Impact.ttf",
                 "/Library/Fonts/Impact.ttf",
                 "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"]
TEXT_FONTS = [("/System/Library/Fonts/Avenir Next.ttc", 2),   # Demi Bold
              ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 0)]

GRID_H = 440    # screenshot area height; the brand band takes the rest
GAP = 6
STRIPE_H = 10   # hazard stripe between grid and band, as on the site


def die(msg):
    sys.exit("make_share_cards.py: %s" % msg)


def pick_font(explicit, candidates, what):
    if explicit:
        if not Path(explicit).is_file():
            die("%s font %s not found" % (what, explicit))
        return explicit, 0
    for cand in candidates:
        path, index = cand if isinstance(cand, tuple) else (cand, 0)
        if Path(path).is_file():
            return path, index
    die("no %s font found; pass --%s-font" % (what, what))


def load_shot(rel):
    path = SITE / "assets" / rel
    if not path.is_file():
        die("screenshot %s is missing" % path)
    return Image.open(path).convert("RGB")


def tile(im, w, h, focus=None):
    """A w x h tile of `im`: the whole capture cropped to the tile's aspect,
    or, with focus=(cx, cy, scale), the window of w/scale x h/scale source
    pixels centred on (cx, cy), clamped inside the capture."""
    sw, sh = im.size
    if focus is None:
        if sw * h > sh * w:
            cw, ch = round(sh * w / h), sh
        else:
            cw, ch = sw, round(sw * h / w)
        cx, cy = sw / 2.0, sh / 2.0
    else:
        cx, cy, scale = focus
        cw, ch = round(w / scale), round(h / scale)
        if cw > sw or ch > sh:
            die("focus scale %.2f needs a %dx%d window of a %dx%d capture"
                % (scale, cw, ch, sw, sh))
    x = int(min(max(cx - cw / 2.0, 0), sw - cw))
    y = int(min(max(cy - ch / 2.0, 0), sh - ch))
    # reducing_gap keeps the downscale a true area average, which is what
    # stops scanline and aperture-grille captures turning into moire.
    return im.crop((x, y, x + cw, y + ch)).resize(
        (w, h), Image.LANCZOS, reducing_gap=3.0)


def spans(total, n, gap):
    """n (start, length) runs covering `total` with `gap` between them."""
    out = []
    for i in range(n):
        a = round(i * (total + gap) / n)
        b = round((i + 1) * (total + gap) / n) - gap
        out.append((a, b - a))
    return out


def hazard_stripe(card, y, h):
    """Diagonal red/black stripe, the site's .hazard bar in miniature."""
    d = ImageDraw.Draw(card)
    d.rectangle([0, y, W, y + h - 1], fill=BG)
    step = 2 * h
    for x in range(-h, W + h, step):
        d.polygon([(x, y + h), (x + h, y), (x + 2 * h, y), (x + h, y + h)],
                  fill=RED_RULE)


def render_svg(svg_text, size):
    try:
        png = subprocess.run(
            ["rsvg-convert", "-w", str(size), "-h", str(size)],
            input=svg_text.encode("utf-8"), stdout=subprocess.PIPE,
            check=True).stdout
    except FileNotFoundError:
        die("rsvg-convert not found (brew install librsvg)")
    return Image.open(io.BytesIO(png)).convert("RGBA")


def icon_svgs():
    """(rounded, full_bleed) SVG text derived from the app icon.

    rounded: viewBox cropped to the icon's rounded square, so a 16px favicon
    spends no pixels on the transparent margin.  full_bleed: the same crop
    with a square in place of the rounded square -- opaque, for
    apple-touch-icon.  Both edits anchor on the SVG's current structure and
    fail loudly if it changes rather than emitting a wrong icon.
    """
    src = ICON_SVG.read_text(encoding="utf-8")
    vb = 'viewBox="0 0 1024 1024"'
    if vb not in src or "</defs>" not in src or 'id="bg"' not in src:
        die("%s no longer has the expected viewBox/defs/#bg; update "
            "icon_svgs()" % ICON_SVG)
    rounded = re.sub(r"<!--.*?-->\s*", "", src, flags=re.S)
    rounded = rounded.replace(vb, 'viewBox="64 64 896 896"')
    # full_bleed swaps the two rounded rects (fill and the faint outline
    # stroke, which would show as a ring inside the square) for a square.
    full, n = re.subn(r'\s*<rect x="64" y="64" width="896" height="896" '
                      r'rx="200"[^>]*/>', "", rounded)
    if n != 2:
        die("%s no longer has the two rounded rects icon_svgs() expects"
            % ICON_SVG)
    full = full.replace(
        "</defs>",
        '</defs>\n  <rect x="64" y="64" width="896" height="896" '
        'fill="url(#bg)"/>', 1)
    return rounded, full


def brand_band(card, y0, title_font, text_font, tagline, icon):
    """Icon + VIRTUAL JAGUAR wordmark + tagline, centred in the band."""
    d = ImageDraw.Draw(card)
    band_h = H - y0
    words = [("VIRTUAL ", FG), ("JAGUAR", RED_LINK)]
    wm_w = sum(d.textlength(t, font=title_font) for t, _ in words)
    tg_w = d.textlength(tagline, font=text_font)
    t_box = d.textbbox((0, 0), "VJ", font=title_font)
    g_box = d.textbbox((0, 0), tagline, font=text_font)
    t_h = t_box[3] - t_box[1]
    g_h = g_box[3] - g_box[1]
    line_gap = 14
    block_h = t_h + line_gap + g_h
    text_w = int(round(max(wm_w, tg_w)))
    icon_gap = 30
    group_w = icon.width + icon_gap + text_w
    if group_w > W - 60:
        die("brand band is %dpx wide, more than the card; shorten the "
            "tagline or the font size" % group_w)
    x = (W - group_w) // 2
    card.paste(icon, (x, y0 + (band_h - icon.height) // 2), icon)
    tx = x + icon.width + icon_gap
    ty = y0 + (band_h - block_h) // 2
    for text, colour in words:
        d.text((tx, ty - t_box[1]), text, font=title_font, fill=colour)
        tx += round(d.textlength(text, font=title_font))
    d.text((x + icon.width + icon_gap, ty + t_h + line_gap - g_box[1]),
           tagline, font=text_font, fill=FG_DIM)


def home_card(fonts, icon):
    card = Image.new("RGB", (W, H), BG)
    rows = spans(GRID_H, 2, GAP)
    cols = spans(W, 4, GAP)
    for i, rel in enumerate(HOME_SHOTS):
        (y, h), (x, w) = rows[i // 4], cols[i % 4]
        card.paste(tile(load_shot(rel), w, h), (x, y))
    hazard_stripe(card, GRID_H, STRIPE_H)
    brand_band(card, GRID_H + STRIPE_H, fonts[0], fonts[1],
               "Atari Jaguar emulation · libretro core + desktop app",
               icon)
    return card


def app_card(fonts, icon):
    card = Image.new("RGB", (W, H), BG)
    big_w = round(GRID_H * 1280 / 860)    # the library window's aspect
    rel, focus = APP_BIG
    card.paste(tile(load_shot(rel), big_w, GRID_H, focus), (0, 0))
    rx = big_w + GAP
    rw = W - rx
    (ty, th), (by, bh) = spans(GRID_H, 2, GAP)
    rel, focus = APP_RIGHT_TOP
    card.paste(tile(load_shot(rel), rw, th, focus), (rx, ty))
    for (x, w), (rel, focus) in zip(spans(rw, 2, GAP), APP_RIGHT_BOTTOM):
        card.paste(tile(load_shot(rel), w, bh, focus), (rx + x, by))
    hazard_stripe(card, GRID_H, STRIPE_H)
    brand_band(card, GRID_H + STRIPE_H, fonts[0], fonts[1],
               "The desktop app · library, CRT shaders, debuggers, themes",
               icon)
    return card


def save_card(card, path):
    buf = io.BytesIO()
    card.save(buf, "PNG", optimize=True)
    data = buf.getvalue()
    if len(data) >= MAX_BYTES:
        # Palette PNG: indistinguishable at share size, a fraction of the
        # bytes.  Only reached when the truecolour file is over budget.
        q = card.quantize(colors=256, method=Image.MEDIANCUT,
                          dither=Image.FLOYDSTEINBERG)
        buf = io.BytesIO()
        q.save(buf, "PNG", optimize=True)
        data = buf.getvalue()
    if card.size != (W, H):
        die("%s is %dx%d, not %dx%d" % (path, card.width, card.height, W, H))
    if len(data) >= MAX_BYTES:
        die("%s is %d bytes, over the 1 MB share-card budget"
            % (path, len(data)))
    path.write_bytes(data)
    print("wrote %s (%dx%d, %d KB)" % (path.relative_to(ROOT), W, H,
                                      len(data) // 1024))


def write_icons(rounded, full):
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    (ICON_DIR / "favicon.svg").write_text(rounded, encoding="utf-8")
    for s in (192, 512):
        render_svg(rounded, s).save(ICON_DIR / ("icon-%d.png" % s),
                                    optimize=True)
    render_svg(full, 180).convert("RGB").save(
        ICON_DIR / "apple-touch-icon.png", optimize=True)
    # Each ICO entry rendered at its own size, not downscaled from one.
    frames = [render_svg(rounded, s) for s in (16, 32, 48)]
    frames[-1].save(ICON_DIR / "favicon.ico", format="ICO",
                    sizes=[f.size for f in frames], append_images=frames[:-1])
    print("wrote %s/{favicon.svg,favicon.ico,icon-*.png,apple-touch-icon.png}"
          % ICON_DIR.relative_to(ROOT))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--display-font", help="wordmark face (default: Impact)")
    ap.add_argument("--text-font", help="tagline face (default: Avenir Next "
                                        "Demi Bold, else DejaVu Sans Bold)")
    args = ap.parse_args()
    if not shutil.which("rsvg-convert"):
        die("rsvg-convert not found (brew install librsvg)")

    dpath, didx = pick_font(args.display_font, DISPLAY_FONTS, "display")
    tpath, tidx = pick_font(args.text_font, TEXT_FONTS, "text")
    fonts = (ImageFont.truetype(dpath, 96, index=didx),
             ImageFont.truetype(tpath, 30, index=tidx))

    rounded, full = icon_svgs()
    write_icons(rounded, full)
    icon = render_svg(rounded, 124)

    SHARE_DIR.mkdir(parents=True, exist_ok=True)
    save_card(home_card(fonts, icon), SHARE_DIR / "home.png")
    save_card(app_card(fonts, icon), SHARE_DIR / "app.png")


if __name__ == "__main__":
    main()
