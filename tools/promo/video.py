#!/usr/bin/env python3
# The layout themes' video: the three example themes running on a handheld's two screens, with what the theme
# engine does written beside them. screens.py renders the screens first (banana-ui, headless).
#
#   python3 tools/promo/video.py [out.mp4]
import os, subprocess, shutil, sys, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../..'))
SCREENS = os.path.join(HERE, 'screens')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'layout-themes.mp4')
FRAMES = os.path.join(HERE, 'frames')
W, H, FPS = 1920, 1080, 30
FONTS = ROOT + '/ui/assets/rnds/fonts/'	# BananaOS's menu font: Pixelify Sans
MONO = '/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf'
# the music: the track from MUSIC_AT seconds in, faded in at the start and out at the end (none if not given)
MUSIC = os.environ.get('MUSIC', '')
MUSIC_AT = float(os.environ.get('MUSIC_AT', '20'))
if not os.path.exists(MONO): MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'
F = lambda name, size: ImageFont.truetype(FONTS + name, size)
BOLD = SEMI = MED = 'PixelifySans-Medium.ttf'
REG = 'PixelifySans-Regular.ttf'
# BananaOS's logo (the bunch, BANANA and OS in pixels), grown by whole pixels
LOGO = Image.open(ROOT + '/ui/assets/rnds/logo@1.6x.png').convert('RGBA')
def logo(k):
    return LOGO.resize((LOGO.width * k, LOGO.height * k), Image.NEAREST)
LOGO_BIG, LOGO_SMALL = logo(4), logo(2)
def paste_logo(img, lg, x, y, alpha):
    if alpha <= 0: return
    lg = lg.copy(); lg.putalpha(lg.getchannel('A').point(lambda v: int(v * alpha)))
    img.alpha_composite(lg, (int(x), int(y)))
YELLOW, INK, SOFT = (255, 214, 64), (240, 242, 248), (150, 156, 172)

# ---- the story ------------------------------------------------------------------------------------------------------
SEGMENTS = [
    dict(theme='hillside', name='Hillside', tag='THEME 1 / 3', glow=(60, 130, 230),
         head='A desktop, rebuilt in JSON only',
         points=['Components with parameters', 'Grids and lists that scroll themselves',
                 'A wallpaper drawn from shapes:\nno pictures at all'],
         code=['"components": {', '  "window": { "type": "group", ... },', '  "taskbar": { "type": "group", ... }', '},',
               '{ "use": "window", "w": 560, "h": 420,', '  "with": { "title": "{sys.name}" } }']),
    dict(theme='turntable', name='Turntable', tag='THEME 2 / 3', glow=(200, 200, 215),
         head='Animation is just math',
         points=['An expression on any property', 'Easing, time and smoothed values',
                 'A 3D spin, faked with cos(time)'],
         code=['"vars": { "turn": "=time * 0.09" },', '{ "type": "image", "src": "{art}",',
               '  "w": "=190 * abs(cosd(turn))",', '  "tint": "=cosd(turn) < 0 ? \'#7a7a80\'', '                          : \'#ffffff\'" }']),
    dict(theme='classic', name='Classic', tag='THEME 3 / 3', glow=(120, 200, 245),
         head='Hooks for everything on screen',
         points=['The clock, the date and the battery', 'Lists that scroll to the selection',
                 'Tap anything: "onTap"'],
         code=['{ "type": "text", "text": "{title}" },', '{ "use": "battery", "right": 16 },',
               '{ "type": "rect", "radius": 6, "w": 34,', '  "x": "=52 + 496 * scroll / max(1, count - 1)" },',
               '{ "type": "group", "onTap": "a", ... }']),
]
INTRO, SEG, OUTRO = 3.8, 7.2, 5.6
TEXT_OUT = 0.5		# a scene's words all fade out together, this long, and are gone before the next scene starts
TEXT_GAP = 0.25
DURATION = INTRO + SEG * len(SEGMENTS) + OUTRO

def clamp01(t): return max(0.0, min(1.0, t))
def ease(t): t = clamp01(t); return 1 - (1 - t) ** 3
def easeio(t): t = clamp01(t); return 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2

# ---- pieces drawn once ------------------------------------------------------------------------------------------------
def background(glow):
    bg = Image.new('RGB', (W, H), (9, 9, 12))
    g = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(g)
    d.ellipse([W * 0.42, -H * 0.3, W * 1.15, H * 1.3], fill=150)
    g = g.filter(ImageFilter.GaussianBlur(220))
    tint = Image.new('RGB', (W, H), tuple(int(c * 0.32) for c in glow))
    bg = Image.composite(tint, bg, g)
    # a fine dot grid
    d = ImageDraw.Draw(bg)
    for y in range(20, H, 36):
        for x in range(20, W, 36):
            d.point((x, y), fill=(26, 27, 33))
    return bg
BACKS = {s['theme']: background(s['glow']) for s in SEGMENTS}
BACKS['intro'] = background((255, 200, 60))

# the two screens: just the pictures, rounded, a thin edge, a soft shadow under them
SW, SH = 600, 450				# a screen's picture
GAP = 28
def rr(d, box, r, **kw): d.rounded_rectangle(box, r, **kw)
def handheld():
    w, h = SW + 12, 2 * (SH + 12) + GAP
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top = (0, 0, w, SH + 12); bot = (0, SH + 12 + GAP, w, h)
    for box in (top, bot): rr(d, box, 20, fill=(14, 14, 17), outline=(44, 45, 52), width=2)
    shadow = Image.new('RGBA', (w + 160, h + 160), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    for box in (top, bot): sd.rounded_rectangle((box[0] + 80, box[1] + 100, box[2] + 80, box[3] + 100), 24, fill=(0, 0, 0, 160))
    shadow = shadow.filter(ImageFilter.GaussianBlur(28))
    return im, shadow, top, bot
# the pictures' corners rounded to match
MASK = Image.new('L', (SW, SH), 0); ImageDraw.Draw(MASK).rounded_rectangle((0, 0, SW - 1, SH - 1), 14, fill=255)
DEVICE, SHADOW, TOPBOX, BOTBOX = handheld()
DX, DY = W - DEVICE.width - 210, (H - DEVICE.height) // 2

_cache = {}
def screen(theme, t, side):
    n = max(0, min(221, int(t * FPS)))
    key = (theme, n, side)
    if key not in _cache:
        if len(_cache) > 16: _cache.clear()
        im = Image.open(f'{SCREENS}/{theme}/f{n:04d}-{side}.jpg').convert('RGB').resize((SW, SH), Image.LANCZOS)
        _cache[key] = im
    return _cache[key]

def text(d, xy, s, font, fill, alpha=1.0, spacing=8):
    if alpha <= 0: return
    d.multiline_text(xy, s, font=font, fill=fill + (int(255 * alpha),), spacing=spacing)

def code_card(lines, shown, alpha):
    # a code card: dark, rounded, the JSON coloured (keys, strings, expressions), typed in up to `shown` characters
    mono = ImageFont.truetype(MONO, 22)
    w, lh = 700, 34
    h = 40 + lh * len(lines)
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    rr(d, (0, 0, w - 1, h - 1), 18, fill=(18, 19, 24, 235), outline=(46, 48, 58, 255), width=2)
    for k, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]): d.ellipse((18 + k * 20, 14, 30 + k * 20, 26), fill=c)
    left = shown
    for i, line in enumerate(lines):
        part = line[:max(0, left)]; left -= len(line)
        x, y = 24, 34 + i * lh
        instr, q = False, ''
        tok = ''
        def flush(tok, col):
            nonlocal x
            if tok: d.text((x, y), tok, font=mono, fill=col); x += mono.getlength(tok)
        j = 0
        while j < len(part):
            ch = part[j]
            if ch == '"':
                e = part.find('"', j + 1)
                e = len(part) - 1 if e < 0 else e
                s = part[j:e + 1]
                after = part[e + 1:].lstrip()
                col = (130, 190, 255) if after.startswith(':') else (120, 230, 150) if s.startswith('"=') else (255, 214, 120)
                flush(s, col); j = e + 1; continue
            flush(ch, (175, 180, 195)); j += 1
    im.putalpha(Image.eval(im.getchannel('A'), lambda a: int(a * alpha)))
    return im

# ---- a frame ------------------------------------------------------------------------------------------------------------
def frame(t):
    # which part, and the background between parts
    if t < INTRO: seg, local = -1, t
    elif t < INTRO + SEG * len(SEGMENTS): seg = int((t - INTRO) // SEG); local = t - INTRO - seg * SEG
    else: seg, local = len(SEGMENTS), t - INTRO - SEG * len(SEGMENTS)
    key = 'intro' if seg < 0 else SEGMENTS[min(seg, len(SEGMENTS) - 1)]['theme']
    img = BACKS[key].copy()
    if 0 <= seg < len(SEGMENTS) and local < 0.6:
        prev = 'intro' if seg == 0 else SEGMENTS[seg - 1]['theme']
        img = Image.blend(BACKS[prev], img, ease(local / 0.6))
    d = ImageDraw.Draw(img, 'RGBA')

    # the handheld: slides in after the intro, out at the end
    dev_in = ease((t - INTRO) / 1.1)
    dev_out = easeio((t - (DURATION - OUTRO + 0.3)) / 1.0)
    scale = 1 - 0.2 * dev_out
    dx = DX + int((1 - dev_in) * 900) + int(dev_out * (170 - DX))
    if dev_in > 0:
        if seg < 0: theme, st = SEGMENTS[0]['theme'], 0
        elif seg >= len(SEGMENTS): theme, st = SEGMENTS[-1]['theme'], SEG - 0.01
        else: theme, st = SEGMENTS[seg]['theme'], local
        dev = DEVICE.copy()
        for side, box in (('top', TOPBOX), ('bot', BOTBOX)):
            s = screen(theme, st, side)
            # the switch between themes: a quick flash
            if 0 <= seg < len(SEGMENTS) and seg > 0 and local < 0.35:
                prev = screen(SEGMENTS[seg - 1]['theme'], SEG - 0.01, side)
                s = Image.blend(prev, s, ease(local / 0.35))
                s = Image.blend(s, Image.new('RGB', s.size, (255, 255, 255)), 0.5 * (1 - local / 0.35))
            dev.paste(s, (box[0] + 6, box[1] + 6), MASK)
        sh = SHADOW
        if scale != 1:
            dev = dev.resize((int(dev.width * scale), int(dev.height * scale)), Image.LANCZOS)
            sh = SHADOW.resize((int(SHADOW.width * scale), int(SHADOW.height * scale)))
        y = (H - dev.height) // 2
        img.paste(sh, (dx - int(80 * scale), y - int(80 * scale)), sh)
        img.paste(dev, (dx, y), dev)

    # the words: drawn on a layer of their own, each coming in at its time, the whole layer fading out at once before
    # the scene ends
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer, 'RGBA')
    L = 150
    def gone(local, length): return 1 - clamp01((local - (length - TEXT_GAP - TEXT_OUT)) / TEXT_OUT)
    if seg < 0:
        out = gone(t, INTRO)
        lt = ease(t / 0.8)
        paste_logo(layer, LOGO_BIG, L, 330 - 20 * (1 - lt), lt)
        # the lines under the logo, centred on it
        cx = L + LOGO_BIG.width / 2
        for line, font, col, t0, y in (('The new theme engine', F(SEMI, 54), INK, 0.5, 490),
                                       ('Design any menu. Ship it as one JSON file.', F(REG, 34), SOFT, 1.0, 572)):
            text(d, (cx - font.getlength(line) / 2, y), line, font, col, ease((t - t0) / 0.7))
    elif seg < len(SEGMENTS):
        S = SEGMENTS[seg]
        out = gone(local, SEG)
        def at(t0): return ease((local - t0) / 0.5)
        def rise(t0): return 18 * (1 - ease((local - t0) / 0.5))
        # the tag, the theme's name, the headline
        tag = F(SEMI, 20); tw = tag.getlength(S['tag'])
        a = at(0.2)
        if a > 0:
            d.rounded_rectangle((L, 150 + rise(0.2), L + tw + 32, 188 + rise(0.2)), 19, fill=YELLOW + (int(255 * a),))
            text(d, (L + 16, 158 + rise(0.2)), S['tag'], tag, (20, 20, 24), a)
        text(d, (L, 210 + rise(0.35)), S['name'], F(BOLD, 92), INK, at(0.35))
        text(d, (L, 330 + rise(0.55)), S['head'], F(MED, 38), YELLOW, at(0.55))
        y = 410
        for i, p in enumerate(S['points']):
            a = at(0.9 + i * 0.35)
            if a > 0:
                d.ellipse((L, y + 14 + rise(0.9 + i * 0.35), L + 10, y + 24 + rise(0.9 + i * 0.35)), fill=YELLOW + (int(255 * a),))
            text(d, (L + 28, y + rise(0.9 + i * 0.35)), p, F(REG, 30), INK, a)
            y += 52 + 42 * p.count('\n')
        # the JSON, typed in
        a = at(2.0)
        if a > 0:
            total = sum(len(l) for l in S['code'])
            card = code_card(S['code'], int(total * clamp01((local - 2.0) / 1.8)), a)
            layer.alpha_composite(card, (L, int(y + 30 + rise(2.0))))
    else:
        out = 1
        OX = 900
        text(d, (OX, 300), 'Make your own themes', F(BOLD, 72), INK, ease((local - 0.8) / 0.7))
        big = F(MED, 46)
        text(d, (OX, 410), 'Coming soon in 0.5.0', big, YELLOW, ease((local - 1.4) / 0.7))
        # "beta" after it, slowly, once the line is there
        text(d, (OX + big.getlength('Coming soon in 0.5.0 '), 410), 'beta', big, INK, clamp01((local - 2.4) / 1.8) ** 1.5)
        paste_logo(layer, LOGO_SMALL, OX, 540, ease((local - 1.9) / 0.7))
    if out < 1:
        layer.putalpha(layer.getchannel('A').point(lambda v: int(v * out)))
    if out > 0: img.paste(layer, (0, 0), layer)

    # fades in and out
    fade = clamp01(t / 0.5) * clamp01((DURATION - t) / 0.6)
    if fade < 1: img = Image.blend(Image.new('RGB', (W, H), (0, 0, 0)), img, fade)
    return img

if __name__ == '__main__':
    shutil.rmtree(FRAMES, True); os.makedirs(FRAMES)
    n = int(DURATION * FPS)
    for k in range(n):
        frame(k / FPS).save(f'{FRAMES}/f{k:05d}.png', compress_level=1)
    cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(FPS), '-i', f'{FRAMES}/f%05d.png']
    if MUSIC:
        cmd += ['-ss', str(MUSIC_AT), '-i', MUSIC, '-filter:a', f'afade=t=in:st=0:d=2.5,afade=t=out:st={DURATION - 2.5:.2f}:d=2.5',
                '-map', '0:v', '-map', '1:a', '-c:a', 'aac', '-b:a', '192k', '-shortest']
    cmd += ['-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', '-movflags', '+faststart', OUT]
    subprocess.run(cmd, check=True)
    shutil.rmtree(FRAMES, True)
    print(OUT, n, 'frames')
