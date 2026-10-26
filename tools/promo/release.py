#!/usr/bin/env python3
# The 0.5.0 trailer: what is new, one feature a scene, in the theme trailer's look (video.py): BananaOS's font and logo,
# the words on the left fading out together, the screens or the feature's pixel art on the right.
# screens.py renders the example themes first (banana-ui, headless).
#
#   STUDIO=dir-with-the-Theme-Studio-screenshots [COMPANIONS=n] [MUSIC=song MUSIC_AT=s] python3 tools/promo/release.py [out.mp4]
import os, subprocess, shutil, sys, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../..'))
SCREENS = os.path.join(HERE, 'screens')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'release-0.5.0.mp4')
FRAMES = os.path.join(HERE, 'frames-release')
W, H, FPS = 1920, 1080, 30
MUSIC = os.environ.get('MUSIC', '')
MUSIC_AT = float(os.environ.get('MUSIC_AT', '31'))
STUDIO = os.environ.get('STUDIO', '')		# the Theme Studio's screenshots: 1.webp, 2.webp
COMPANIONS = os.environ.get('COMPANIONS', '')	# how many games have a GBA companion (left out when not given)
FONTS = ROOT + '/ui/assets/rnds/fonts/'
F = lambda name, size: ImageFont.truetype(FONTS + name, size)
MED, REG = 'PixelifySans-Medium.ttf', 'PixelifySans-Regular.ttf'
YELLOW, INK, SOFT, DARK = (255, 214, 64), (240, 242, 248), (150, 156, 172), (20, 20, 24)

def clamp01(t): return max(0.0, min(1.0, t))
def ease(t): t = clamp01(t); return 1 - (1 - t) ** 3
def easeio(t): t = clamp01(t); return 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2

# ---- pictures ---------------------------------------------------------------------------------------------------------
LOGO = Image.open(ROOT + '/ui/assets/rnds/logo@1.6x.png').convert('RGBA')
def grow(im, k): return im.resize((im.width * k, im.height * k), Image.NEAREST)
LOGO_BIG, LOGO_SMALL = grow(LOGO, 4), grow(LOGO, 2)
ICONS = ROOT + '/ui/assets/rnds/icons/'
def icon(name, k=6): return grow(Image.open(ICONS + name + '.png').convert('RGBA'), k)
def asset(name, k):
    im = Image.open(ROOT + '/web/preview/assets/' + name).convert('RGBA')
    return im.resize((int(im.width * k), int(im.height * k)), Image.LANCZOS)
SUPERDRASTIC, BANANADS = asset('superdrastic-logo.png', 0.62), asset('bananads-logo.png', 0.62)

def bitmap(rows, colors, k):
    # pixel art from rows of characters, one colour a character ('.' is clear), grown by whole pixels
    im = Image.new('RGBA', (len(rows[0]), len(rows)), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c in colors: im.putpixel((x, y), colors[c] + (255,))
    return grow(im, k)
BT = bitmap(['.....X.....', '.....XX....', '.....X.X...', '.....X..X..', '.X...X...X.', '..X..X..X..', '...X.X.X...',
             '....XXX....', '.....X.....', '....XXX....', '...X.X.X...', '..X..X..X..', '.X...X...X.', '.....X..X..',
             '.....X.X...', '.....XX....', '.....X.....'], {'X': (255, 255, 255)}, 7)
ARROW = bitmap(['...XXX...', '...XXX...', '...XXX...', '...XXX...', 'XXXXXXXXX', '.XXXXXXX.', '..XXXXX..', '...XXX...',
                '....X....', '.........', 'XXXXXXXXX'], {'X': YELLOW}, 6)
SERVER = bitmap(['oooooooooooooo', 'o............o', 'o.ggg....y.y.o', 'o............o', 'oooooooooooooo',
                 'o............o', 'o.ggg....y.g.o', 'o............o', 'oooooooooooooo', 'o............o',
                 'o.ggg....g.g.o', 'o............o', 'oooooooooooooo', '..o........o..', '.ooo......ooo.'],
                {'o': (190, 194, 206), 'g': (110, 116, 132), 'y': YELLOW}, 9)

def background(glow):
    bg = Image.new('RGB', (W, H), (9, 9, 12))
    g = Image.new('L', (W, H), 0)
    ImageDraw.Draw(g).ellipse([W * 0.42, -H * 0.3, W * 1.15, H * 1.3], fill=150)
    g = g.filter(ImageFilter.GaussianBlur(220))
    bg = Image.composite(Image.new('RGB', (W, H), tuple(int(c * 0.32) for c in glow)), bg, g)
    d = ImageDraw.Draw(bg)
    for y in range(20, H, 36):
        for x in range(20, W, 36): d.point((x, y), fill=(26, 27, 33))
    return bg

# the two screens, as in the theme trailer: rounded, a thin edge, a soft shadow
SW, SH, GAP = 600, 450, 28
def handheld():
    w, h = SW + 12, 2 * (SH + 12) + GAP
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top, bot = (0, 0, w, SH + 12), (0, SH + 12 + GAP, w, h)
    for box in (top, bot): d.rounded_rectangle(box, 20, fill=(14, 14, 17), outline=(44, 45, 52), width=2)
    return im, top, bot
DEVICE, TOPBOX, BOTBOX = handheld()
MASK = Image.new('L', (SW, SH), 0); ImageDraw.Draw(MASK).rounded_rectangle((0, 0, SW - 1, SH - 1), 14, fill=255)
def shadow(w, h, r=24, blur=28, a=160):
    s = Image.new('RGBA', (w + 160, h + 160), (0, 0, 0, 0))
    ImageDraw.Draw(s).rounded_rectangle((80, 100, w + 80, h + 100), r, fill=(0, 0, 0, a))
    return s.filter(ImageFilter.GaussianBlur(blur))
DSHADOW = shadow(DEVICE.width, DEVICE.height)

_cache = {}
def screen(theme, t, side):
    n = max(0, min(221, int(t * FPS)))
    key = (theme, n, side)
    if key not in _cache:
        if len(_cache) > 24: _cache.clear()
        _cache[key] = Image.open(f'{SCREENS}/{theme}/f{n:04d}-{side}.jpg').convert('RGB').resize((SW, SH), Image.LANCZOS)
    return _cache[key]

# the Theme Studio, in a window of its own
STUDIO_W = 900
def studio_shots():
    shots = []
    for f in ('1.webp', '2.webp'):
        p = os.path.join(STUDIO, f)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB')
            shots.append(im.resize((STUDIO_W, int(im.height * STUDIO_W / im.width)), Image.LANCZOS))
    return shots
SHOTS = studio_shots()

# ---- the story --------------------------------------------------------------------------------------------------------
gba_line = f'{COMPANIONS} games now have a GBA companion.' if COMPANIONS else 'Your DS games, joined by their GBA companions.'
SCENES = [
    dict(len=6.5, kind='open', glow=(255, 200, 60)),
    dict(len=7.5, kind='whatsnew', glow=(255, 200, 60)),
    dict(len=7.6, kind='themes', glow=(60, 130, 230), tag='NEW THEME ENGINE', name='Make it yours',
         head='Change anything on both screens with one JSON file.',
         points=['Layouts, colours, fonts and animations', 'Hooks for menus and system info', 'Three example themes to start from']),
    dict(len=7.6, kind='studio', glow=(70, 200, 190), tag='BANANA THEME STUDIO', name='Banana Theme Studio',
         head='Design your theme on your computer.',
         points=['Drag elements onto both screens', 'Layers, components and assets', 'An exact preview from the real engine']),
    dict(len=6.2, kind='gba', glow=(110, 90, 230), tag='GBA COMPANIONS', name='GBA game companions', head=gba_line, points=[]),
    dict(len=6.2, kind='bt', glow=(50, 120, 255), tag='BLUETOOTH AUDIO', name='Bluetooth audio',
         head='Play with wireless headphones and speakers.', points=[]),
    dict(len=6.2, kind='ota', glow=(255, 170, 60), tag='OTA UPDATER', name='Updates over the air',
         head='Update BananaOS right from your handheld.', points=[]),
    dict(len=7.2, kind='res', glow=(255, 200, 60), tag='BANANADS', name='BananaDS, upgraded',
         head='Faster, and new 3x and 4x resolutions.', points=[]),
    dict(len=6.4, kind='latency', glow=(255, 110, 80), tag='LOW LATENCY MODE', name='Low latency mode',
         head='For both SuperDrastic and BananaDS.', points=[]),
    dict(len=6.4, kind='sync', glow=(80, 200, 120), tag='HOME SERVER SYNC', name='Home server sync',
         head='Your ROMs and saves, synced with your own server.', points=[]),
    dict(len=5.8, kind='more', glow=(255, 200, 60)),
    dict(len=7.4, kind='end', glow=(255, 200, 60)),
]
t0 = 0.0
for s in SCENES: s['at'] = t0; t0 += s['len']
DURATION = t0
TEXT_OUT, TEXT_GAP = 0.5, 0.25
BACKS = [background(s['glow']) for s in SCENES]
THEMES = ['hillside', 'turntable', 'classic']

# ---- drawing helpers --------------------------------------------------------------------------------------------------
def text(d, xy, s, font, fill, alpha):
    if alpha > 0: d.text(xy, s, font=font, fill=fill + (int(255 * alpha),))
def paste(layer, im, x, y, alpha=1.0):
    # at a fraction of a pixel (pixel art stepping whole pixels as it eases shakes)
    if alpha <= 0: return
    im = im.copy(); im.putalpha(im.getchannel('A').point(lambda v: int(v * alpha)))
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    if fx > 0.01 or fy > 0.01:
        im = im.convert('RGBa').transform((im.width + 1, im.height + 1), Image.AFFINE, (1, 0, -fx, 0, 1, -fy), Image.BILINEAR).convert('RGBA')
    layer.alpha_composite(im, (ix, iy))
def card(w, h, fill=(18, 19, 24, 235), outline=(46, 48, 58, 255), r=22):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=fill, outline=outline, width=2)
    return im
def centred(d, cx, y, s, font, fill, alpha): text(d, (cx - font.getlength(s) / 2, y), s, font, fill, alpha)

VX = 1380	# the centre of the pictures on the right

# ---- a frame ----------------------------------------------------------------------------------------------------------
def frame(t):
    i = max(k for k, s in enumerate(SCENES) if s['at'] <= t)
    S = SCENES[i]; local = t - S['at']; kind = S['kind']
    img = BACKS[i].copy()
    if i > 0 and local < 0.6: img = Image.blend(BACKS[i - 1], img, ease(local / 0.6))
    img = img.convert('RGBA')
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer, 'RGBA')
    out = 1 - clamp01((local - (S['len'] - TEXT_GAP - TEXT_OUT)) / TEXT_OUT)
    if i == len(SCENES) - 1: out = 1
    def at(t0, dur=0.5): return ease((local - t0) / dur)
    def rise(t0, dur=0.5): return 18 * (1 - ease((local - t0) / dur))
    L = 150

    if kind == 'open':
        paste(layer, LOGO_BIG, (W - LOGO_BIG.width) / 2, 330 + rise(0.0, 0.8), at(0.0, 0.8))
        centred(d, W / 2, 490 + rise(0.6), 'Version 0.5.0', F(MED, 64), YELLOW, at(0.6))
        centred(d, W / 2, 586 + rise(1.2), 'The first beta of BananaOS.', F(REG, 38), SOFT, at(1.2))
    elif kind == 'whatsnew':
        centred(d, W / 2, 400 + rise(0.2), "Here's what's new.", F(MED, 88), INK, at(0.2))
        centred(d, W / 2, 530 + rise(0.9), 'Seven new features, and plenty more.', F(REG, 40), SOFT, at(0.9))
    elif kind == 'more':
        # every icon of the menu, popping in
        names = sorted(f[:-4] for f in os.listdir(ICONS) if f.endswith('.png'))
        cols, k, step = 7, 3, 128
        rows = math.ceil(len(names) / cols)
        x0, y0 = 1860 - (cols - 1) * step - 96, H / 2 - (rows - 1) * step / 2 - 48
        for n, name in enumerate(names):
            a = at(0.3 + 0.035 * n, 0.4)
            if a > 0:
                c, r = n % cols, n // cols
                paste(layer, icon(name, k), x0 + c * step, y0 + r * step + 20 * (1 - a), a)
        text(d, (L, 400 + rise(0.2)), 'And much more.', F(MED, 92), INK, at(0.2))
        text(d, (L, 520 + rise(0.8)), 'Check the release notes', F(REG, 40), YELLOW, at(0.8))
        text(d, (L, 572 + rise(1.0)), 'for everything new in 0.5.0.', F(REG, 40), YELLOW, at(1.0))
    elif kind == 'end':
        OX = 640
        paste(layer, LOGO_BIG, (W - LOGO_BIG.width) / 2, 310 + rise(0.2, 0.8), at(0.2, 0.8))
        big = F(MED, 56)
        line = 'Coming soon in 0.5.0'
        full = big.getlength(line + ' beta')
        x = (W - full) / 2
        text(d, (x, 480 + rise(1.0)), line, big, YELLOW, at(1.0))
        text(d, (x + big.getlength(line + ' '), 480), 'beta', big, INK, clamp01((local - 2.2) / 1.8) ** 1.5)
        centred(d, W / 2, 580 + rise(1.6), 'The first beta: themes, companions, Bluetooth audio, OTA updates and more.', F(REG, 34), SOFT, at(1.6))
    else:
        # a feature: the tag, the name, the line under it, the points; the picture on the right
        tag = F(MED, 20); tw = tag.getlength(S['tag'])
        a = at(0.2)
        if a > 0:
            d.rounded_rectangle((L, 300 + rise(0.2), L + tw + 32, 338 + rise(0.2)), 19, fill=YELLOW + (int(255 * a),))
            text(d, (L + 16, 308 + rise(0.2)), S['tag'], tag, DARK, a)
        name_font = F(MED, 84 if F(MED, 84).getlength(S['name']) < 700 else 68)
        text(d, (L, 360 + rise(0.35)), S['name'], name_font, INK, at(0.35))
        # the line under it, wrapped to the column
        hf = F(REG, 38); words = S['head'].split(' '); lines = ['']
        for w_ in words:
            t_ = (lines[-1] + ' ' + w_).strip()
            if hf.getlength(t_) > 760 and lines[-1]: lines.append(w_)
            else: lines[-1] = t_
        y = 480
        for k, ln in enumerate(lines):
            text(d, (L, y + rise(0.55 + 0.1 * k)), ln, hf, YELLOW, at(0.55 + 0.1 * k)); y += 52
        y += 26
        for k, p in enumerate(S['points']):
            t1 = 1.0 + 0.3 * k
            a = at(t1)
            if a > 0: d.ellipse((L, y + 14 + rise(t1), L + 10, y + 24 + rise(t1)), fill=YELLOW + (int(255 * a),))
            text(d, (L + 28, y + rise(t1)), p, F(REG, 30), INK, a)
            y += 50
        draw_feature(layer, d, S, local, at, rise)

    if out < 1: layer.putalpha(layer.getchannel('A').point(lambda v: int(v * out)))
    img.alpha_composite(layer)
    img = img.convert('RGB')
    fade = clamp01(t / 0.5) * clamp01((DURATION - t) / 0.8)
    if fade < 1: img = Image.blend(Image.new('RGB', (W, H), (0, 0, 0)), img, fade)
    return img

def draw_feature(layer, d, S, local, at, rise):
    kind = S['kind']
    a_in = at(0.3, 0.8); dy = 40 * (1 - a_in)
    if kind == 'themes':
        # the example themes on the two screens, one after another, with a flash between
        per = (S['len'] - 0.6) / 3
        k = min(2, int(local // per)); lt = local - k * per
        dev = DEVICE.copy()
        for side, box in (('top', TOPBOX), ('bot', BOTBOX)):
            s = screen(THEMES[k], 0.8 + lt, side)
            if k > 0 and lt < 0.35:
                s = Image.blend(screen(THEMES[k - 1], 0.8 + per, side), s, ease(lt / 0.35))
                s = Image.blend(s, Image.new('RGB', s.size, (255, 255, 255)), 0.5 * (1 - lt / 0.35))
            dev.paste(s, (box[0] + 6, box[1] + 6), MASK)
        x, y = VX - DEVICE.width / 2, (H - DEVICE.height) / 2 + dy
        paste(layer, DSHADOW, x - 80, y - 80, a_in)
        paste(layer, dev, x, y, a_in)
        names = ['Hillside', 'Turntable', 'Classic']
        f = F(REG, 24); s = f'Example theme {k + 1} / 3: {names[k]}'
        text(d, (VX - f.getlength(s) / 2, y + DEVICE.height + 14), s, f, SOFT, a_in)
    elif kind == 'studio' and SHOTS:
        # the app's window: the first screenshot, then the second fading in, drifting slowly
        bar = 40
        h = max(s.height for s in SHOTS)
        win = Image.new('RGBA', (STUDIO_W + 4, h + bar + 4), (0, 0, 0, 0))
        wd = ImageDraw.Draw(win)
        wd.rounded_rectangle((0, 0, win.width - 1, win.height - 1), 14, fill=(30, 31, 38), outline=(60, 62, 74), width=2)
        for c, col in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]): wd.ellipse((18 + c * 24, 13, 32 + c * 24, 27), fill=col)
        tf = F(REG, 20); title = 'Banana Theme Studio'
        wd.text(((win.width - tf.getlength(title)) / 2, 10), title, font=tf, fill=(200, 204, 216))
        mix = ease((local - 3.4) / 0.8) if len(SHOTS) > 1 else 0
        a0 = SHOTS[0]; b0 = SHOTS[-1].resize(a0.size, Image.LANCZOS) if len(SHOTS) > 1 else a0
        shot = Image.blend(a0, b0, mix) if 0 < mix < 1 else (b0 if mix >= 1 else a0)
        win.paste(shot, (2, bar + 2))
        k = 0.98 + 0.03 * clamp01(local / S['len'])
        win = win.resize((int(win.width * k), int(win.height * k)), Image.LANCZOS)
        x, y = 1860 - win.width, (H - win.height) / 2 + dy
        sh = shadow(win.width, win.height, 16)
        paste(layer, sh, x - 80, y - 80, a_in)
        paste(layer, win, x, y, a_in)
    elif kind == 'gba':
        nds, gba = icon('nds', 8), icon('gba', 8)
        y = H / 2 - 128 + dy
        paste(layer, nds, VX - 360, y, a_in)
        paste(layer, gba, VX + 104, y, at(0.7, 0.8))
        # the link between them: dots running from one to the other
        for n in range(6):
            x = VX - 92 + n * 36
            ph = (local * 2.2 - n * 0.18) % 1.0
            a = at(0.9) * (0.35 + 0.65 * (1 - abs(ph - 0.5) * 2))
            d.rectangle((x, H / 2 - 8 + dy, x + 16, H / 2 + 8 + dy), fill=YELLOW + (int(255 * a),))
        if COMPANIONS:
            n = int(round(int(COMPANIONS) * ease((local - 1.0) / 1.6)))
            f = F(MED, 120); s = str(n)
            text(d, (VX - f.getlength(s) / 2, H / 2 + 170 + dy), s, f, YELLOW, at(1.0))
            centred(d, VX, H / 2 + 310 + dy, 'games', F(REG, 34), SOFT, at(1.2))
        else:
            centred(d, VX - 232, H / 2 + 160 + dy, 'Nintendo DS', F(REG, 30), SOFT, a_in)
            centred(d, VX + 232, H / 2 + 160 + dy, 'Game Boy Advance', F(REG, 30), SOFT, at(0.7, 0.8))
    elif kind == 'bt':
        # the Bluetooth mark in its blue chip, sound waves going out to the music note
        chip = card(220, 260, fill=(40, 100, 230, 255), outline=(90, 150, 255, 255), r=48)
        cx, cy = VX - 230, H / 2 + dy
        paste(layer, chip, cx - 110, cy - 130, a_in)
        paste(layer, BT, cx - BT.width / 2, cy - BT.height / 2, a_in)
        note = icon('music', 8)
        paste(layer, note, VX + 140, cy - 128, at(0.8, 0.8))
        for n in range(4):
            ph = (local * 0.9 - n * 0.25) % 1.0
            r = 60 + ph * 200
            a = at(1.0) * (1 - ph) * 0.9
            box = (cx + 40 - r * 0.2, cy - r, cx + 40 + r * 1.2, cy + r)
            d.arc(box, -40, 40, fill=INK + (int(255 * a),), width=8)
    elif kind == 'ota':
        # the handheld, and the update coming down into it
        hh = icon('nds', 9)
        cx, cy = VX, H / 2 - 120 + dy
        paste(layer, hh, cx - hh.width / 2, cy - 40, a_in)
        bob = 10 * math.sin(local * 4)
        ay = cy - 200 + bob
        paste(layer, ARROW, cx - ARROW.width / 2, ay, at(0.6))
        # a progress card
        c = card(620, 150)
        cw = ImageDraw.Draw(c)
        cw.text((32, 26), 'BananaOS 0.5.0', font=F(MED, 34), fill=INK)
        p = ease((local - 1.0) / 3.6)
        label = 'Installed' if p >= 0.999 else f'Downloading  {int(p * 100)}%'
        cw.text((32, 74), label, font=F(REG, 24), fill=SOFT)
        cw.rounded_rectangle((32, 112, 588, 126), 7, fill=(44, 46, 56))
        if p > 0: cw.rounded_rectangle((32, 112, 32 + max(14, int(556 * p)), 126), 7, fill=YELLOW)
        paste(layer, c, cx - 310, cy + 330, at(0.8))
    elif kind == 'res':
        # BananaDS, then the DS's 256 x 192 picture at 1x to 4x, nested
        paste(layer, BANANADS, VX - BANANADS.width / 2, 150 + dy, a_in)
        base = (128, 96)	# 1x drawn at half scale
        x0, y0 = VX - 4 * base[0] / 2 * 1.0 - 40, 300 + dy
        # largest first: drawing on the layer replaces what is under it, so the smaller boxes go on top
        for k in range(4, 0, -1):
            a = at(0.8 + 0.5 * (k - 1), 0.5)
            if a <= 0: continue
            w, h = base[0] * k, base[1] * k
            new = k >= 3
            col = YELLOW if new else SOFT
            d.rectangle((x0, y0, x0 + w, y0 + h), outline=col + (int(255 * a),), width=4,
                        fill=(YELLOW + (int(28 * a),)) if new else (255, 255, 255, int(10 * a)))
            f = F(MED, 30)
            text(d, (x0 + w - f.getlength(f'{k}x') - 14, y0 + h - 44), f'{k}x', f, col, a)
            if new:
                sf = F(REG, 20); s = f'{256 * k} x {192 * k}'
                text(d, (x0 + w + 14, y0 + h - 30), s, sf, col, a)
                if True:
                    d.rounded_rectangle((x0 + w - 86, y0 + 14, x0 + w - 14, y0 + 46), 16, fill=YELLOW + (int(255 * a),))
                    text(d, (x0 + w - 74, y0 + 18), 'NEW', F(MED, 20), DARK, a)
    elif kind == 'latency':
        # both emulators' logos, and the time from a press to the screen getting shorter
        paste(layer, SUPERDRASTIC, VX - 200 - SUPERDRASTIC.width / 2, 400 - SUPERDRASTIC.height / 2 + dy, a_in)
        paste(layer, BANANADS, VX + 200 - BANANADS.width / 2, 400 - BANANADS.height / 2 + dy, at(0.5, 0.8))
        c = card(760, 230)
        cw = ImageDraw.Draw(c)
        on = local > 2.2
        cw.text((32, 28), 'Low latency mode', font=F(MED, 32), fill=INK)
        sx = 760 - 32 - 92
        cw.rounded_rectangle((sx, 28, sx + 92, 72), 22, fill=YELLOW if on else (60, 62, 74))
        kx = sx + (52 if on else 4)
        cw.ellipse((kx, 32, kx + 36, 68), fill=(255, 255, 255))
        cw.text((32, 104), 'Press to picture', font=F(REG, 24), fill=SOFT)
        w = 696 * (1 - 0.55 * ease((local - 2.2) / 1.0))
        cw.rounded_rectangle((32, 150, 32 + int(w), 178), 14, fill=YELLOW if on else (150, 156, 172))
        paste(layer, c, VX - 380, 560 + dy, at(0.9))
    elif kind == 'sync':
        # the home server and the handheld, saves and games going back and forth
        cy = H / 2 - 60 + dy
        paste(layer, SERVER, VX - 330 - SERVER.width / 2, cy - SERVER.height / 2, a_in)
        hh = icon('nds', 8)
        paste(layer, hh, VX + 330 - hh.width / 2, cy - hh.height / 2, at(0.6, 0.8))
        centred(d, VX - 330, cy + 170, 'Your server', F(REG, 30), SOFT, a_in)
        centred(d, VX + 330, cy + 170, 'Your handheld', F(REG, 30), SOFT, at(0.6, 0.8))
        for lane, (label, dirn, col) in enumerate((('ROMs', 1, YELLOW), ('Saves', -1, INK))):
            y = cy - 40 + lane * 80
            a = at(1.0 + 0.3 * lane)
            for n in range(5):
                ph = (local * 0.7 + n / 5) % 1.0
                x = VX - 170 + (ph if dirn > 0 else 1 - ph) * 340
                fa = a * min(1, ph * 5, (1 - ph) * 5)
                d.rectangle((x - 9, y - 9, x + 9, y + 9), fill=col + (int(255 * fa),))
            centred(d, VX, y - 52 + 6, label, F(REG, 22), col, a)

if __name__ == '__main__':
    shutil.rmtree(FRAMES, True); os.makedirs(FRAMES)
    n = int(DURATION * FPS)
    for k in range(n):
        frame(k / FPS).save(f'{FRAMES}/f{k:05d}.png', compress_level=1)
    cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(FPS), '-i', f'{FRAMES}/f%05d.png']
    if MUSIC:
        cmd += ['-ss', str(MUSIC_AT), '-i', MUSIC, '-filter:a', f'afade=t=in:st=0:d=2.5,afade=t=out:st={DURATION - 2.5:.2f}:d=2.5',
                '-map', '0:v', '-map', '1:a', '-c:a', 'aac', '-b:a', '192k', '-ar', '44100', '-ac', '2', '-shortest']
    cmd += ['-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', '-movflags', '+faststart', OUT]
    subprocess.run(cmd, check=True)
    shutil.rmtree(FRAMES, True)
    print(OUT, n, 'frames')
