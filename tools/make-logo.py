#!/usr/bin/env python3
"""Banana's status-bar logo for ROCKNIXDS Pixel's brand chip: the pixel-art bunch (tools/make-pixel-banana.py) and
BANANAOS in the theme's Pixelify Sans, drawn hard-edged (no anti-aliasing): BANANA with a two-tone banana fill, OS
in white, and a 1 px drop shadow, at 1x (134x20, the RG DS) and 1.6x (214x32, the RG DS Plus). The chip's slot is fixed (RndsUI draws it at
148.23 x 26 CSS px); the logo is centred in it.

    tools/make-logo.py <PixelifySans-Medium.ttf> <pixel banana dir> <out dir> [splash dir]"""
import sys
from PIL import Image, ImageDraw, ImageFont

LIGHT = (255, 222, 82, 255)
DARK = (238, 160, 24, 255)
WHITE = (255, 255, 255, 255)
SHADOW = (0, 0, 0, 255)
WORD, SUFFIX = "BANANA", "OS"      # the wordmark: BANANA in banana tones, OS in white


# Pixelify Sans is drawn on a grid of about 92 font units (of 1000): at 1000/92 px one font pixel is one screen pixel,
# and the text is then scaled up by a whole number, so every font pixel is a clean square
GRID_SIZE = 1000 / 92


def text_layer(font_path, scale, text):
    font = ImageFont.truetype(font_path, GRID_SIZE)
    l, t, r, b = font.getbbox(text)
    im = Image.new("L", (int(r - l) + 4, int(b - t) + 4), 0)
    d = ImageDraw.Draw(im)
    d.fontmode = "1"        # whole pixels only
    d.text((2 - l, 2 - t), text, font=font, fill=255)
    im = im.crop(im.getbbox())
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def logo(font_path, bananas, w, h, text_scale, banana_px, gap):
    mask = text_layer(font_path, text_scale, WORD + SUFFIX)
    # where OS starts: the whole word's width less the suffix's, so the kerning is the word's own
    split = mask.width - text_layer(font_path, text_scale, SUFFIX).width
    ban = Image.open(f"{bananas}/banana-{banana_px}.png").convert("RGBA")
    total = ban.width + gap + mask.width + 1
    assert total <= w, f"the logo ({total} px) is wider than the chip ({w} px)"
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    x0 = (w - total) // 2
    out.alpha_composite(ban, (x0, (h - ban.height) // 2))
    tx, ty = x0 + ban.width + gap, (h - mask.height - 1) // 2 + 1
    # the fill: light over the top half, darker below, like the bananas' own shading; OS white
    fill = Image.new("RGBA", mask.size, DARK)
    ImageDraw.Draw(fill).rectangle([0, 0, mask.width, mask.height * 11 // 20], fill=LIGHT)
    ImageDraw.Draw(fill).rectangle([split, 0, mask.width, mask.height], fill=WHITE)
    shadow = Image.new("RGBA", mask.size, SHADOW)
    sh = text_scale // 2 + (1 if text_scale > 2 else 0)     # the drop shadow: one font pixel down and right, about
    out.paste(shadow, (tx + sh, ty + sh), mask)
    out.paste(fill, (tx, ty), mask)
    return out


font, bananas, out = sys.argv[1], sys.argv[2], sys.argv[3]
logo(font, bananas, 134, 20, 2, 20, 6).save(out + "/logo@1x.png")
logo(font, bananas, 214, 32, 3, 32, 9).save(out + "/logo@1.6x.png")
if len(sys.argv) > 4:
    # for the boot animation (src/splash.cpp): the words as masks, one font pixel per image pixel; banana-ui scales
    # them by whole numbers
    # them by whole numbers; the word's last len(SUFFIX) letters are the white ones (splash.cpp knows the count)
    for name, word in (("word", WORD + SUFFIX), ("loading", "LOADING")):
        text_layer(font, 1, word).save(f"{sys.argv[4]}/{name}.png")
