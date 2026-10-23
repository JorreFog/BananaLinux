#!/usr/bin/env python3
# The website's announcement video: the new BananaOS website scrolling in a browser window, a line of copy beside each
# part of it, in the trailer's look (video.py): BananaOS's font and logo, a scene's words fading out together before
# the next. site-capture.mjs captures the site first: one screenshot per frame of the scroll, 30 a second, on a virtual
# clock so the site's own motion (its slideshow, the handheld's idle movement) runs at its real pace.
#
#   SITE=<capture dir> MUSIC=song.mp3 MUSIC_AT=8 python3 tools/promo/website.py out.mp4
import os, subprocess, shutil, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../..'))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'website.mp4')
SITE = os.environ.get('SITE', os.path.join(HERE, 'site'))
SITE_FPS = int(os.environ.get("SITE_FPS", "30"))
MUSIC = os.environ.get('MUSIC', '')
MUSIC_AT = float(os.environ.get('MUSIC_AT', '8'))
FRAMES = os.path.join(HERE, 'frames-site')
W, H, FPS = 1920, 1080, 30
FONTS = ROOT + '/ui/assets/rnds/fonts/'
F = lambda name, size: ImageFont.truetype(FONTS + name, size)
MED, REG = 'PixelifySans-Medium.ttf', 'PixelifySans-Regular.ttf'
YELLOW, INK, SOFT = (255, 214, 64), (240, 242, 248), (150, 156, 172)
ASSETS = ROOT + '/ui/assets/'
LOGO = Image.open(ASSETS + 'rnds/logo@1.6x.png').convert('RGBA')
LOGO_BIG = LOGO.resize((LOGO.width * 4, LOGO.height * 4), Image.NEAREST)
BANANA = Image.open(ASSETS + 'banana/banana-48.png').convert('RGBA')
BANANA = BANANA.resize((BANANA.width * 2, BANANA.height * 2), Image.NEAREST)
# the emulators' logos, from the website's own assets (SITE_ASSETS), when there
SITE_ASSETS = os.environ.get('SITE_ASSETS', ROOT + '/web/preview/assets')
def load(p, k):
    if not os.path.exists(p): return None
    im = Image.open(p).convert('RGBA')
    return im.resize((int(im.width * k), int(im.height * k)), Image.NEAREST)
EMU_LOGOS = [l for l in (load(SITE_ASSETS + '/superdrastic-logo.png', 0.75), load(SITE_ASSETS + '/bananads-logo.png', 0.64)) if l]
# a pixel heart (for the copy's yellow heart), 9 x 8 pixels grown by whole pixels
HEART_ROWS = ['.##...##.', '####.####', '#########', '#########', '.#######.', '..#####..', '...###...', '....#....']
def heart(k):
    im = Image.new('RGBA', (9 * k, 8 * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for y, row in enumerate(HEART_ROWS):
        for x, c in enumerate(row):
            if c == '#': d.rectangle((x * k, y * k, x * k + k - 1, y * k + k - 1), fill=YELLOW + (255,))
    return im
HEART = heart(7)

def clamp01(t): return max(0.0, min(1.0, t))
def ease(t): t = clamp01(t); return 1 - (1 - t) ** 3
def easeio(t): t = clamp01(t); return 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2

# ---- the story --------------------------------------------------------------------------------------------------------
# a scene: its length (s), the capture it shows (None: a card), a tag over the words, the words (big, then smaller)
SCENES = [
    dict(len=3.6, kind='open'),
    dict(len=4.6, kind='news'),
    dict(len=3.5, site='s03', tag='THE NEW WEBSITE', lines=["Let's take a look."]),
    dict(len=5.0, site='s04', tag='FEATURES', lines=['Explore the features.', "Get to know your handheld's next OS."]),
    dict(len=5.0, site='s05', tag='FEATURES', lines=['From the pixel-art menu...', 'to gaming across both screens.']),
    dict(len=5.5, site='s06', tag='EMULATORS', lines=['Meet SuperDrastic and BananaDS.', 'Two ways to play your DS favourites.'], logos=True),
    dict(len=5.0, site='s07', tag='UNDER THE HOOD', lines=['Want to know how it works?', 'Take a look under the hood.']),
    dict(len=3.0, site='s08', tag='DOWNLOAD', lines=['Ready to get started?']),
    dict(len=4.5, site='s09', tag='DOWNLOAD', lines=['Find downloads for', 'RG DS and RG DS Plus.']),
    dict(len=4.5, site='s10', tag='INSTALL', lines=['Follow the installation guide.', 'Get ready for your next game.']),
    dict(len=5.0, site='s11', tag='UPDATE', lines=['Already running BananaOS?', 'Find update instructions and move your saves.']),
    dict(len=4.5, site='s12', tag='ALPHA', lines=['Still in alpha.', 'Still growing with your feedback.']),
    dict(len=3.6, kind='thanks'),
    dict(len=5.4, kind='end'),
]
t = 0
for s in SCENES: s['at'] = t; t += s['len']
DURATION = t
TEXT_OUT, TEXT_GAP = 0.5, 0.25	# a scene's words fade out together, this long, gone before the next scene starts
SITE_FIRST = next(s for s in SCENES if s.get('site'))
SITE_LAST = [s for s in SCENES if s.get('site')][-1]

# ---- pieces drawn once ------------------------------------------------------------------------------------------------
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
BACK_WARM, BACK_SITE = background((255, 200, 60)), background((90, 110, 170))

# the browser window: a rounded frame, a bar with three dots and an empty address pill, the page under it
PW, PH = 960, int(960 * 900 / 1440)
BAR = 44
def window():
    w, h = PW + 4, PH + BAR + 4
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 18, fill=(20, 20, 24), outline=(48, 49, 57), width=2)
    for k, c in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]): d.ellipse((20 + k * 22, 16, 32 + k * 22, 28), fill=c)
    d.rounded_rectangle((w / 2 - 260, 10, w / 2 + 260, 34), 12, fill=(34, 35, 41))
    sh = Image.new('RGBA', (w + 160, h + 160), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle((80, 100, w + 80, h + 100), 22, fill=(0, 0, 0, 170))
    return im, sh.filter(ImageFilter.GaussianBlur(30))
WINDOW, WSHADOW = window()
PMASK = Image.new('L', (PW, PH), 0); ImageDraw.Draw(PMASK).rounded_rectangle((0, -20, PW - 1, PH - 1), 16, fill=255)
WX = W - WINDOW.width - 100

_cache = {}
def capture(name, i):
    key = (name, i)
    if key not in _cache:
        if len(_cache) > 8: _cache.clear()
        files = sorted(os.listdir(f'{SITE}/{name}'))
        i = max(0, min(len(files) - 1, i))
        _cache[key] = Image.open(f'{SITE}/{name}/{files[i]}').convert('RGB').resize((PW, PH), Image.LANCZOS)
    return _cache[key]
def page(name, local):
    # between two captured steps: a blend of both (the capture is 15 a second, the video 30)
    x = local * SITE_FPS
    a = capture(name, int(x))
    f = x - int(x)
    return a if f < 0.05 else Image.blend(a, capture(name, int(x) + 1), f)

def text(d, xy, s, font, fill, alpha):
    if alpha > 0: d.text(xy, s, font=font, fill=fill + (int(255 * alpha),))
def wrap(s, font, width):
    out, line = [], ''
    for w in s.split(' '):
        t = (line + ' ' + w).strip()
        if font.getlength(t) > width and line: out.append(line); line = w
        else: line = t
    return out + [line]
def paste(layer, im, x, y, alpha):
    if alpha <= 0: return
    im = im.copy(); im.putalpha(im.getchannel('A').point(lambda v: int(v * alpha)))
    layer.alpha_composite(im, (int(x), int(y)))

# ---- a frame ----------------------------------------------------------------------------------------------------------
def frame(t):
    i = max(k for k, s in enumerate(SCENES) if s['at'] <= t) if t >= 0 else 0
    S = SCENES[i]; local = t - S['at']
    site_on = SITE_FIRST['at'] <= t < SITE_LAST['at'] + SITE_LAST['len']
    img = BACK_SITE.copy() if site_on else BACK_WARM.copy()
    # the backgrounds blend over the first and last site scenes' edges
    if S is SITE_FIRST and local < 0.8: img = Image.blend(BACK_WARM, BACK_SITE, ease(local / 0.8))
    if SCENES.index(S) == SCENES.index(SITE_LAST) + 1 and local < 0.8: img = Image.blend(BACK_SITE, BACK_WARM, ease(local / 0.8))

    # the window: slides in with the first site scene, out at the end of the last
    if site_on:
        a = ease((t - SITE_FIRST['at']) / 1.0)
        end = SITE_LAST['at'] + SITE_LAST['len']
        b = easeio((t - (end - 0.9)) / 0.9)
        x = WX + int((1 - a) * 900) + int(b * 900)
        y = (H - WINDOW.height) // 2
        win = WINDOW.copy()
        win.paste(page(S['site'], local), (2, BAR + 2), PMASK)
        img.paste(WSHADOW, (x - 80, y - 80), WSHADOW)
        img.paste(win, (x, y), win)

    # the words, on a layer of their own that fades out as one before the next scene
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer, 'RGBA')
    out = 1 - clamp01((local - (S['len'] - TEXT_GAP - TEXT_OUT)) / TEXT_OUT)
    if i == len(SCENES) - 1: out = 1		# the last scene stays to the fade to black
    def at(t0): return ease((local - t0) / 0.6)
    def rise(t0): return 16 * (1 - ease((local - t0) / 0.6))
    kind = S.get('kind')
    if kind == 'open':
        f = F(MED, 64); s = "We've been working on something new..."
        w = f.getlength(s) + 24 + BANANA.width
        x0 = (W - w) / 2
        text(d, (x0, 500 + rise(0.2)), s, f, INK, at(0.2))
        paste(layer, BANANA, x0 + f.getlength(s) + 24, 478 + rise(1.0), at(1.0))
    elif kind == 'news':
        paste(layer, LOGO_BIG, (W - LOGO_BIG.width) / 2, 300 + rise(0.0), at(0.0))
        for s, f, col, t0, y in (('BananaOS has a new website!', F(MED, 64), YELLOW, 0.6, 500),
                                 ('A home for everything BananaOS.', F(REG, 40), SOFT, 1.2, 600)):
            text(d, ((W - f.getlength(s)) / 2, y + rise(t0)), s, f, col, at(t0))
    elif kind == 'thanks':
        f = F(MED, 72); s = 'Thanks for being part of it.'
        w = f.getlength(s) + 28 + HEART.width
        x0 = (W - w) / 2
        text(d, (x0, 490 + rise(0.2)), s, f, INK, at(0.2))
        paste(layer, HEART, x0 + f.getlength(s) + 28, 506 + rise(0.9), at(0.9))
    elif kind == 'end':
        paste(layer, LOGO_BIG, (W - LOGO_BIG.width) / 2, 300 + rise(0.0), at(0.0))
        for s, f, col, t0, y in (('Explore the new BananaOS website.', F(MED, 58), INK, 0.7, 500),
                                 ('Link in the description.', F(REG, 40), YELLOW, 1.5, 596)):
            text(d, ((W - f.getlength(s)) / 2, y + rise(t0)), s, f, col, at(t0))
    else:
        # the tag, then the lines: the first large, the rest under it in yellow, wrapped to the column
        L, col_w = 110, WX - 110 - 70
        lines = []
        for k, s in enumerate(S['lines']):
            f = F(MED, 56) if k == 0 else F(MED, 38)
            for part in wrap(s, f, col_w): lines.append((part, f, INK if k == 0 else YELLOW, k))
        hgt = 60 + sum(f.size + 18 for _, f, _, _ in lines) + (150 if S.get('logos') else 0)
        y = (H - hgt) / 2
        tag = F(MED, 22); tw = tag.getlength(S['tag'])
        a = at(0.15)
        if a > 0:
            d.rounded_rectangle((L, y + rise(0.15), L + tw + 34, y + 40 + rise(0.15)), 20, fill=YELLOW + (int(255 * a),))
            text(d, (L + 17, y + 7 + rise(0.15)), S['tag'], tag, (20, 20, 24), a)
        y += 64
        for part, f, col, k in lines:
            t0 = 0.35 + 0.55 * k
            text(d, (L, y + rise(t0)), part, f, col, at(t0))
            y += f.size + 18
        if S.get('logos'):
            y += 22
            for k, lg in enumerate(EMU_LOGOS):
                paste(layer, lg, L, y + rise(1.4 + 0.3 * k), at(1.4 + 0.3 * k))
                y += lg.height + 18
    if out < 1: layer.putalpha(layer.getchannel('A').point(lambda v: int(v * out)))
    if out > 0: img.paste(layer, (0, 0), layer)

    fade = clamp01(t / 0.5) * clamp01((DURATION - t) / 0.8)
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
                '-map', '0:v', '-map', '1:a', '-c:a', 'aac', '-b:a', '192k', '-ar', '44100', '-ac', '2', '-shortest']
    cmd += ['-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '18', '-movflags', '+faststart', OUT]
    subprocess.run(cmd, check=True)
    shutil.rmtree(FRAMES, True)
    print(OUT, n, 'frames')
