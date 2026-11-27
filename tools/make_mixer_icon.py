"""Draws the BEQ Tab EQ mixer window's icon: three faders in BEQ's colours,
so the mixer doesn't look like the standalone app in the taskbar.
Writes chrome-extension/assets/mixer{16,32,48,128}.png."""
from pathlib import Path
from PIL import Image, ImageDraw

S = 1024
RED, BLACK, BONE = (196, 43, 43, 255), (12, 12, 12, 255), (242, 238, 230, 255)
out = Path(__file__).resolve().parent.parent / "chrome-extension" / "assets"

im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
# Sticker-style plate: bone rim, black body, red inner line (like the BEQ logo).
d.rounded_rectangle((24, 24, S - 24, S - 24), radius=200, fill=BONE)
d.rounded_rectangle((72, 72, S - 72, S - 72), radius=160, fill=BLACK)
d.rounded_rectangle((110, 110, S - 110, S - 110), radius=128, outline=RED, width=26)

xs = [312, 512, 712]
caps = [420, 640, 330]           # fader heights
top, bottom = 230, 800
for x, y in zip(xs, caps):
    d.rounded_rectangle((x - 22, top, x + 22, bottom), radius=22, fill=(70, 70, 72, 255))
    d.rounded_rectangle((x - 22, y, x + 22, bottom), radius=22, fill=RED)        # lit part below the cap
    d.rounded_rectangle((x - 92, y - 52, x + 92, y + 52), radius=26, fill=BONE)  # fader cap
    d.rounded_rectangle((x - 72, y - 34, x + 72, y + 34), radius=16, fill=BLACK)
    d.rectangle((x - 60, y - 7, x + 60, y + 7), fill=RED)

for n in (16, 32, 48, 128):
    im.resize((n, n), Image.LANCZOS).save(out / f"mixer{n}.png")
im.resize((256, 256), Image.LANCZOS).save("/tmp/claude-0/ext/mixer-icon-preview.png")
