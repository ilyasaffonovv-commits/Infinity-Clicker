"""Generates Infinity Clicker's application icons from code (no external artwork).

    python tools/make_icon.py

Outputs (resources/):
    infclick.ico         - app / window icon, 16..256 px
    infclick_active.ico  - tray icon variant shown while the engine is ACTIVE
    infclick_256.png     - preview used by the README

Design: dark rounded tile, violet glow, a pointer arrow filled with a
cyan->lilac gradient sitting above an infinity loop, and a click ripple at
the tip. Small sizes (<= 32 px) use a simplified, bolder variant so the shape
stays readable in the taskbar / tray.
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

S = 1024  # supersampled canvas
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources")

CYAN = (34, 211, 238)
VIOLET = (139, 92, 246)
LILAC = (196, 181, 253)
BG_TOP = (22, 28, 56)
BG_BOT = (8, 11, 22)
GREEN = (52, 211, 153)


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def gradient(size, c0, c1, angle_deg):
    """Linear gradient image (RGBA) along `angle_deg`."""
    w, h = size
    g = Image.new("RGBA", size)
    px = g.load()
    a = math.radians(angle_deg)
    dx, dy = math.cos(a), math.sin(a)
    corners = [0 * dx + 0 * dy, w * dx, h * dy, w * dx + h * dy]
    lo, hi = min(corners), max(corners)
    for y in range(h):
        for x in range(w):
            t = ((x * dx + y * dy) - lo) / (hi - lo)
            px[x, y] = lerp(c0, c1, t) + (255,)
    return g


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def arrow_points(tip_x, tip_y, scale):
    # Classic pointer silhouette, unit coordinates (tip at 0,0).
    pts = [(0.00, 0.00), (0.00, 0.80), (0.19, 0.62), (0.32, 0.93), (0.46, 0.87), (0.33, 0.57), (0.60, 0.57)]
    return [(tip_x + x * scale, tip_y + y * scale) for x, y in pts]


def infinity_layer(cx, cy, width, thickness):
    """Lemniscate of Bernoulli stroked with a cyan->violet gradient."""
    a = width / 2
    pts = []
    n = 2400
    for i in range(n + 1):
        t = 2 * math.pi * i / n
        d = 1 + math.sin(t) ** 2
        pts.append((cx + a * math.cos(t) / d, cy + a * math.sin(t) * math.cos(t) / d))
    # stamp round dots along the curve: smoother than a wide polyline
    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    r = thickness / 2
    for x, y in pts:
        md.ellipse([x - r, y - r, x + r, y + r], fill=255)
    grad = gradient((64, 64), CYAN, VIOLET, 0).resize((S, S), Image.BICUBIC)
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    layer.paste(grad, (0, 0), mask)
    return layer


def render(simple=False, badge=False):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # --- tile
    small_grad = gradient((64, 64), BG_TOP, BG_BOT, 90).resize((S, S), Image.BICUBIC)
    radius = int(S * 0.23)
    tile = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    tile.paste(small_grad, (0, 0), rounded_mask(S, radius))
    img = Image.alpha_composite(img, tile)

    # --- violet glow behind the cursor
    glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    cx, cy, r = int(S * 0.56), int(S * 0.50), int(S * 0.34)
    gd.ellipse([cx - r, cy - r, cx + r, cy + r], fill=VIOLET + (120,))
    glow = glow.filter(ImageFilter.GaussianBlur(S * 0.09))
    glow.putalpha(ImageChops.multiply(glow.getchannel("A"), rounded_mask(S, radius)))
    img = Image.alpha_composite(img, glow)

    # --- border: thin gradient ring
    ring_grad = gradient((64, 64), CYAN, VIOLET, 45).resize((S, S), Image.BICUBIC)
    ring_mask = Image.new("L", (S, S), 0)
    rd = ImageDraw.Draw(ring_mask)
    bw = int(S * (0.035 if simple else 0.018))
    rd.rounded_rectangle([0, 0, S - 1, S - 1], radius=radius, fill=200)
    rd.rounded_rectangle([bw, bw, S - 1 - bw, S - 1 - bw], radius=radius - bw, fill=0)
    ring = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ring.paste(ring_grad, (0, 0), ring_mask)
    img = Image.alpha_composite(img, ring)

    # --- geometry
    if simple:
        tip = (S * 0.33, S * 0.09)
        scale = S * 0.60
    else:
        tip = (S * 0.36, S * 0.12)
        scale = S * 0.54

    # --- infinity loop under the arrow
    loop = infinity_layer(S * 0.53, S * (0.78 if simple else 0.765), S * (0.68 if simple else 0.64),
                          S * (0.095 if simple else 0.058))
    img = Image.alpha_composite(img, loop)

    # --- click ripple at the tip
    if not simple:
        rip = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        rpd = ImageDraw.Draw(rip)
        for i, (rr, alpha) in enumerate([(0.085, 230), (0.145, 150)]):
            R = S * rr
            w = int(S * (0.020 - i * 0.004))
            rpd.arc([tip[0] - R, tip[1] - R, tip[0] + R, tip[1] + R], start=195, end=345, fill=CYAN + (alpha,),
                    width=w)
        img = Image.alpha_composite(img, rip)

    # --- arrow: dark outline, then gradient fill
    pts = arrow_points(tip[0], tip[1], scale)
    outline = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    od = ImageDraw.Draw(outline)
    ow = int(S * (0.050 if simple else 0.034))
    od.polygon(pts, fill=BG_BOT + (255,))
    od.line(pts + [pts[0]], fill=BG_BOT + (255,), width=ow * 2, joint="curve")
    img = Image.alpha_composite(img, outline)

    fill_grad = gradient((64, 64), CYAN, LILAC, 60).resize((S, S), Image.BICUBIC)
    amask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(amask).polygon(pts, fill=255)
    arrow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    arrow.paste(fill_grad, (0, 0), amask)
    img = Image.alpha_composite(img, arrow)

    # subtle highlight on the arrow's left edge
    hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(hl).line([pts[0], pts[1]], fill=(255, 255, 255, 110), width=int(S * 0.012))
    hl.putalpha(ImageChops.multiply(hl.getchannel("A"), amask))
    img = Image.alpha_composite(img, hl)

    # --- ACTIVE badge
    if badge:
        b = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        bd = ImageDraw.Draw(b)
        R = S * (0.20 if simple else 0.16)
        bx, by = S * 0.78, S * 0.78
        bd.ellipse([bx - R - S * 0.03, by - R - S * 0.03, bx + R + S * 0.03, by + R + S * 0.03], fill=BG_BOT + (255,))
        bd.ellipse([bx - R, by - R, bx + R, by + R], fill=GREEN + (255,))
        img = Image.alpha_composite(img, b)

    return img


def build(path, badge):
    detailed = render(simple=False, badge=badge)
    simple = render(simple=True, badge=badge)
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    frames = []
    for s in sizes:
        src = simple if s <= 32 else detailed
        frames.append(src.resize((s, s), Image.LANCZOS))
    frames[-1].save(path, format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    return detailed


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    big = build(os.path.join(OUT, "infclick.ico"), badge=False)
    build(os.path.join(OUT, "infclick_active.ico"), badge=True)
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "infclick_256.png"))
    print("icons written to", os.path.abspath(OUT))
