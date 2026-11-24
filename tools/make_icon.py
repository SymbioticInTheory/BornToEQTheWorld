"""Builds resources/icon.png: "BEQ" in the style of the old Death logo, with
the scythe blade and hooded skull cut from a copy of that logo.

Usage: python tools/make_icon.py death_logo.png resources/icon.png
(the logo image Kyle supplied, 894 x 894, is not kept in the repository)
"""
import sys, random
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

src_path, out_path = sys.argv[1], sys.argv[2]
S = 1024
RED = (196, 30, 30)

def disk(r):
    y, x = np.ogrid[-r:r + 1, -r:r + 1]
    return x * x + y * y <= r * r

def stroke_mask(polylines):
    img = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(img)
    for pts, w, closed in polylines:
        seq = pts + [pts[0]] if closed else pts
        d.line(seq, fill=255, width=w, joint='curve')
        for p in pts:  # square-ish joints
            d.rectangle([p[0] - w // 2, p[1] - w // 2, p[0] + w // 2, p[1] + w // 2], fill=255)
    return np.array(img) > 0

def poly_mask(polys):
    img = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(img)
    for p in polys:
        d.polygon(p, fill=255)
    return np.array(img) > 0

# --- Letters: angular, Death-style B E Q ------------------------------------
top, mid, base = 400, 575, 770
strokes = [
    # B
    ([(150, top - 20), (150, base)], 84, False),
    ([(150, top), (310, top), (352, 455), (310, mid), (150, mid)], 66, False),
    ([(150, mid), (325, mid), (372, 650), (330, base), (150, base)], 66, False),
    # E (tall stem: the scythe handle)
    ([(470, 215), (470, base)], 84, False),
    ([(470, top), (655, top)], 64, False),
    ([(470, mid), (615, mid)], 64, False),
    ([(470, base), (665, base)], 64, False),
    # Q
    ([(720, top), (880, top), (910, 455), (910, 705), (875, base), (745, base), (710, 705), (710, 455)], 66, True),
    ([(895, top), (895, 318)], 70, False),
    ([(800, 655), (950, 845)], 60, False),
]
spikes = [  # serif spikes pointing down/out
    [(113, base + 30), (187, base + 30), (150, base + 95)],
    [(113, top - 50), (150, top - 120), (187, top - 50)],
    [(625, top + 28), (683, top + 28), (683, top + 95)],
    [(585, mid + 28), (643, mid + 28), (643, mid + 85)],
    [(433, base + 30), (507, base + 30), (470, base + 100)],
    [(635, base + 28), (695, base + 28), (695, base + 95)],
    [(690, base + 10), (760, base + 30), (712, base + 90)],
]
spikes += [
    [(310, top - 29), (372, top - 29), (372, top - 85)],   # B top right
    [(700, top - 29), (760, top - 29), (690, top - 80)],   # Q top left
    [(910, mid), (945, mid - 40), (945, mid + 50)],        # Q right barb
]
letters = stroke_mask(strokes) | poly_mask(spikes)

# Blood: black drips of uneven length with teardrop ends, some with a loose drop.
random.seed(11)
drip = Image.new('L', (S, S), 0)
dd = ImageDraw.Draw(drip)

def drips_along(x0, x1, y, density=(16, 34), length=(18, 120)):
    x = x0
    while x < x1:
        w = random.randint(9, 17)
        L = random.randint(*length)
        dd.rectangle([x, y - 10, x + w, y + L], fill=255)
        r = w // 2 + random.randint(2, 5)
        cx = x + w / 2
        dd.ellipse([cx - r, y + L - r, cx + r, y + L + r + 4], fill=255)
        if random.random() < 0.3:
            d = random.randint(10, 24)
            rr = random.randint(4, 7)
            dd.ellipse([cx - rr, y + L + r + d, cx + rr, y + L + r + d + rr * 2 + 3], fill=255)
        x += w + random.randint(*density)

drips_along(115, 410, base + 32)
drips_along(440, 690, base + 32)
drips_along(700, 905, base + 32)
drips = np.array(drip) > 0

# --- Scythe blade and hooded skull, cut from the supplied logo ---------------
src = Image.open(src_path).convert('RGB')
arr = np.array(src).astype(int)
ink = arr.min(axis=2) < 150

def cut(poly):
    m = Image.new('L', src.size, 0)
    ImageDraw.Draw(m).polygon(poly, fill=255)
    m = ink & (np.array(m) > 0)
    m = ndimage.binary_closing(m, disk(2))
    m = ndimage.binary_fill_holes(m)
    lab, n = ndimage.label(m)
    sizes = ndimage.sum(m, lab, range(1, n + 1))
    return np.isin(lab, [i + 1 for i, sz in enumerate(sizes) if sz > 400])

blade_m = cut([(140, 332), (155, 326), (200, 308), (245, 298), (330, 286), (340, 322), (385, 322), (390, 280), (445, 274), (492, 270), (492, 172), (300, 196), (140, 300)])
hood_m = cut([(628, 125), (795, 125), (795, 336), (628, 336)])

out = np.zeros((S, S, 4), dtype=np.uint8)
layers = np.zeros((S, S), bool)

def paste(mask, box, scale, dest_xy, flip=False):
    """Paste src pixels under mask (cropped to box), scaled, at dest top-left."""
    x0, y0, x1, y1 = box
    piece = np.zeros((y1 - y0, x1 - x0, 4), dtype=np.uint8)
    piece[..., :3] = arr[y0:y1, x0:x1]
    piece[..., 3] = mask[y0:y1, x0:x1] * 255
    im = Image.fromarray(piece, 'RGBA')
    if flip:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
    canvas = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    canvas.alpha_composite(im, dest_xy)
    return np.array(canvas)

blade = paste(blade_m, (140, 170, 495, 335), 1.25, (33, 161))
hood = paste(hood_m, (628, 125, 795, 336), 1.4, (780, 36))

# Death-style letters: black body, a red band just inside the edge, black core.
body = ndimage.binary_dilation(letters, disk(10))
red_band = ndimage.binary_erosion(letters, disk(4)) & ~ndimage.binary_erosion(letters, disk(17))

blade_a = blade[..., 3] > 60
hood_a = hood[..., 3] > 60
pieces = blade_a | hood_a
# One black outline around everything so the cut pieces join the letters.
outline = ndimage.binary_dilation(body | drips | pieces, disk(5))
sticker = ndimage.binary_dilation(outline, disk(20))

a = np.zeros((S, S, 4), dtype=np.uint8)
a[sticker] = (245, 243, 240, 255)
a[outline] = (8, 8, 8, 255)
a[red_band] = (*RED, 255)
img = Image.fromarray(a, 'RGBA')
for layer in (blade, hood):
    img.alpha_composite(Image.fromarray(layer, 'RGBA'))
img.save(out_path)
print('saved', out_path)
