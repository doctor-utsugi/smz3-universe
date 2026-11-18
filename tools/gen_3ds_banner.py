"""Writes the 3DS HOME menu banner (the picture over the selected icon and its sound, cut from intro.ogg) for the CIA build:
assets/3ds/banner.png, assets/3ds/banner.wav and assets/3ds/banner.bnr. The .bnr needs bannertool
(https://github.com/diasurgical/bannertool): run python3 tools/gen_3ds_banner.py [path/to/bannertool]."""
import os, shutil, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets/3ds")
FONT = os.path.join(ROOT, "tools/fonts/ChakraPetch-Bold.ttf")
ACCENT, SHADOW = (0xF2, 0xA3, 0x3A, 255), (0x8A, 0x5A, 0x14, 255)   # the colours of the logo text on the welcome screen
W, H = 256, 128                                                     # the banner's texture size


def picture():
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    size = 92
    logo = Image.open(os.path.join(ROOT, "assets/logo.png")).convert("RGB").crop((90, 90, 934, 934))
    logo = logo.resize((size, size), Image.LANCZOS).convert("RGBA")
    mask = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), radius=size * 4 // 5, fill=255)
    logo.putalpha(mask.resize((size, size), Image.LANCZOS))
    img.alpha_composite(logo, (14, (H - size) // 2))
    d = ImageDraw.Draw(img)
    big, small = ImageFont.truetype(FONT, 44), ImageFont.truetype(FONT, 21)
    x = 14 + size + 10
    for text, font, y in (("SMZ3", big, 26), ("UNIVERSE", small, 76)):
        glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(glow).text((x, y), text, font=font, fill=(0, 0, 0, 200), stroke_width=3, stroke_fill=(0, 0, 0, 200))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2)))
        d.text((x + 2, y + 2), text, font=font, fill=SHADOW)
        d.text((x, y), text, font=font, fill=ACCENT)
    return img


def sound(src, path, secs=3.0):
    """The banner sound from assets/3ds/intro.ogg: the HOME menu plays at most 3 seconds, so it is cut there and
    faded out. 16-bit stereo at 44100 Hz, like FBI's banner (a mono 22050 Hz one came out as a screech)."""
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", src, "-t", str(secs),
                    "-af", "volume=-1.5dB,afade=t=out:st=%.2f:d=0.6" % (secs - 0.6),
                    "-ac", "2", "-ar", "44100", "-c:a", "pcm_s16le", "-map_metadata", "-1", path], check=True)


def main():
    os.makedirs(OUT, exist_ok=True)
    png, wav, bnr = (os.path.join(OUT, "banner." + e) for e in ("png", "wav", "bnr"))
    picture().save(png)
    sound(os.path.join(OUT, "intro.ogg"), wav)
    tool = sys.argv[1] if len(sys.argv) > 1 else shutil.which("bannertool")
    if not tool:
        sys.exit("banner.png and banner.wav written; banner.bnr needs bannertool (pass its path)")
    subprocess.run([tool, "makebanner", "-i", png, "-a", wav, "-o", bnr], check=True)


if __name__ == "__main__":
    main()
