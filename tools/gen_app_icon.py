"""Writes the SMZ3 launcher icon (legacy mipmaps, round variants, adaptive foreground and monochrome layers) into the
vendored Lemuroid resources from assets/logo.png. Run: python3 tools/gen_app_icon.py"""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "vendor/Lemuroid/lemuroid-app/src/main/res")
LOGO = Image.open(os.path.join(ROOT, "assets/logo.png")).convert("RGB")
BG = LOGO.getpixel((4, 4))                      # the black around the rounded square
BOX = (90, 90, 934, 934)                        # the rounded square and a little margin
SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def legacy(size, round_):
    img = LOGO.crop(BOX).resize((size, size), Image.LANCZOS).convert("RGBA")
    mask = Image.new("L", (size * 4, size * 4), 0)
    d = ImageDraw.Draw(mask)
    if round_:
        d.ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
    else:
        d.rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), radius=size * 4 // 5, fill=255)
    img.putalpha(mask.resize((size, size), Image.LANCZOS))
    return img


def layer(size, scale=0.66):
    """The logo on a transparent square, its picture kept inside the adaptive icon's safe zone."""
    inner = int(size * scale)
    pic = LOGO.crop(BOX).resize((inner, inner), Image.LANCZOS).convert("RGBA")
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(pic, ((size - inner) // 2, (size - inner) // 2))
    return out


def monochrome(size):
    """A white silhouette of the Metroid with the Triforce cut out of it (the frame is left out)."""
    src = LOGO.crop(BOX)
    w = src.width
    luma = src.convert("L")
    bright = luma.point(lambda v: 255 if v > 55 else 0)
    ImageDraw.floodfill(bright, (int(w * 0.02) + 6, w // 2), 0, thresh=0)        # the frame ring touches the left edge
    dark = bright.point(lambda v: 255 - v)
    ImageDraw.floodfill(dark, (w // 2, 4), 128, thresh=0)                        # the dark area around the shell
    shell = dark.point(lambda v: 0 if v == 128 else 255)                         # shell, fangs and everything they enclose
    r, g, b = src.split()
    gold = ImageChops.multiply(ImageChops.multiply(r.point(lambda v: 255 if v > 150 else 0), g.point(lambda v: 255 if v > 105 else 0)), b.point(lambda v: 255 if v < 120 else 0))
    window = Image.new("L", gold.size, 0)
    ImageDraw.Draw(window).polygon([(w * 0.5, w * 0.285), (w * 0.295, w * 0.635), (w * 0.705, w * 0.635)], fill=255)   # the Triforce's outline
    gold = ImageChops.multiply(gold, window)
    k = w // 28 | 1
    gold = gold.filter(ImageFilter.MaxFilter(k)).filter(ImageFilter.MinFilter(k))
    holes = gold.point(lambda v: 255 - v)
    ImageDraw.floodfill(holes, (0, 0), 0, thresh=0)                               # what stays is enclosed by the gold: the small inverted triangle
    gold = ImageChops.lighter(gold, holes)
    ImageDraw.Draw(gold).polygon([(w * 0.425, w * 0.475), (w * 0.575, w * 0.475), (w * 0.5, w * 0.60)], fill=0)   # the Triforce's centre stays solid
    alpha = ImageChops.subtract(shell, gold)
    alpha = alpha.filter(ImageFilter.MinFilter(9)).filter(ImageFilter.MaxFilter(9))   # drops specks of the frame
    inner = int(size * 0.66)
    alpha = alpha.resize((inner, inner), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    mask = Image.new("L", (size, size), 0)
    mask.paste(alpha, ((size - inner) // 2, (size - inner) // 2))
    out.putalpha(mask)
    return out


for name, size in SIZES.items():
    d = os.path.join(RES, f"mipmap-{name}")
    os.makedirs(d, exist_ok=True)
    legacy(size, False).save(os.path.join(d, "lemuroid_launcher.png"))
    legacy(size, True).save(os.path.join(d, "lemuroid_launcher_round.png"))

nodpi = os.path.join(RES, "drawable-nodpi")
os.makedirs(nodpi, exist_ok=True)
layer(432).save(os.path.join(nodpi, "smz3_launcher_foreground.png"))
monochrome(432).save(os.path.join(nodpi, "smz3_launcher_monochrome.png"))
with open(os.path.join(RES, "values/lemuroid_launcher_background.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <color name="lemuroid_launcher_background">#%02x%02x%02x</color>\n</resources>\n' % BG)
