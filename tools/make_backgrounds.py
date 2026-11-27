"""Draws BEQ's built-in backgrounds besides the forest: Sunset, Ocean, Neon,
Moss and Concrete. Writes resources/backgrounds/*.jpg (2160 x 1360, the
plugin window at 2x) and chrome-extension/assets/backgrounds/*.jpg (1600 wide).
Everything is drawn here, so there are no outside image rights to worry about."""
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
W, H = 2160, 1360


def gradient(stops):
    col = Image.new("RGB", (1, H))
    px = col.load()
    for y in range(H):
        t = y / (H - 1)
        for a, b in zip(stops, stops[1:]):
            if a[0] <= t <= b[0]:
                u = (t - a[0]) / (b[0] - a[0] + 1e-9)
                px[0, y] = tuple(int(a[1][k] + (b[1][k] - a[1][k]) * u) for k in range(3))
                break
    return col.resize((W, H))


def sunset():
    im = gradient([(0, (40, 20, 70)), (0.45, (180, 70, 90)), (0.62, (250, 150, 60)), (0.7, (255, 200, 120)), (1, (255, 200, 120))])
    d = ImageDraw.Draw(im)
    d.ellipse((1215, 650, 1485, 920), fill=(255, 220, 150))
    hills = [(0, H)] + [(x, 865 + int(80 * math.sin(x / 230) + 40 * math.sin(x / 72))) for x in range(0, W + 1, 20)] + [(W, H)]
    d.polygon(hills, fill=(25, 12, 30))
    return im.filter(ImageFilter.GaussianBlur(2))


def ocean():
    im = gradient([(0, (10, 40, 80)), (0.5, (20, 90, 130)), (0.55, (120, 190, 210)), (1, (5, 30, 60))])
    return im.filter(ImageFilter.GaussianBlur(1))


def neon():
    rnd = random.Random(3)
    im = Image.new("RGB", (W, H), (12, 10, 22))
    d = ImageDraw.Draw(im)
    for _ in range(55):
        x, w, h = rnd.randint(0, W), rnd.randint(54, 190), rnd.randint(270, 945)
        d.rectangle((x, H - h, x + w, H), fill=(18, 16, 30))
        for _ in range(12):
            c = rnd.choice([(255, 40, 170), (40, 230, 255)])
            yy = H - h + rnd.randint(14, h - 14)
            d.rectangle((x + 7, yy, x + w - 7, yy + 5), fill=c)
    return im.filter(ImageFilter.GaussianBlur(4))


def moss():
    rnd = random.Random(3)
    im = gradient([(0, (30, 60, 25)), (0.5, (60, 110, 40)), (1, (15, 30, 12))])
    px = im.load()
    for _ in range(55000):
        x, y = rnd.randrange(W), rnd.randrange(H)
        r, g, b = px[x, y]
        f = rnd.uniform(0.6, 1.3)
        px[x, y] = (min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f)))
    return im.filter(ImageFilter.GaussianBlur(2.5))


def concrete():
    noise = Image.effect_noise((W, H), 40).convert("RGB").filter(ImageFilter.GaussianBlur(4))
    return Image.blend(noise, Image.new("RGB", (W, H), (90, 90, 92)), 0.5)


for name, make in [("sunset", sunset), ("ocean", ocean), ("neon", neon), ("moss", moss), ("concrete", concrete)]:
    im = make()
    for folder, width in [(ROOT / "resources" / "backgrounds", W), (ROOT / "chrome-extension" / "assets" / "backgrounds", 1600)]:
        folder.mkdir(parents=True, exist_ok=True)
        out = im if width == W else im.resize((width, round(H * width / W)), Image.LANCZOS)
        out.save(folder / f"{name}.jpg", quality=86, optimize=True)
    print(name)
