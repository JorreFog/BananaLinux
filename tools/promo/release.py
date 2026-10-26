#!/usr/bin/env python3
# The 0.5.0 trailer: what is new in the first beta, one feature a scene, cut to the music's beat. BananaOS's look (the
# pixel font, the logo, yellow on near-black) taken into 3D: the menu's pixel icons and the logo as lit voxels, the
# screens and the cards on tilted panels, a floor grid and pixel dust in depth, a pixel-block wipe between scenes,
# bloom and grain. screens.py renders the example themes first (banana-ui, headless).
#
#   NO_DATE=1 python3 tools/promo/screens.py hillside turntable classic
#   STUDIO=dir-with-1.webp-and-2.webp COMPANION=dir-with-the-recordings [COMPANIONS=n] [MUSIC=Wasabi.mp3] \
#     python3 tools/promo/release.py [out.mp4]
#
# The cuts follow wasabi-beats.json (the song's beats in seconds, from librosa's beat tracker): the drop at 10.8 s
# brings the first feature, and the end card lands where the song comes back in, 128 beats later.
import os, subprocess, shutil, sys, math, json
from multiprocessing import Pool
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../..'))
SCREENS = os.path.join(HERE, 'screens')
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'release-0.5.0.mp4')
FRAMES = os.path.join(HERE, 'frames-release')
W, H, FPS = 1920, 1080, 30
MUSIC = os.environ.get('MUSIC', '')
MUSIC_AT = float(os.environ.get('MUSIC_AT', '0'))
STUDIO = os.environ.get('STUDIO', '')		# the Theme Studio's screenshots: 1.webp, 2.webp
COMPANIONS = os.environ.get('COMPANIONS', '')	# how many games have a GBA companion (left out when not given)
JOBS = int(os.environ.get('JOBS', os.cpu_count() or 1))
FONTS = ROOT + '/ui/assets/rnds/fonts/'
_fonts = {}
def F(name, size):
    if (name, size) not in _fonts: _fonts[name, size] = ImageFont.truetype(FONTS + name, size)
    return _fonts[name, size]
MED, REG = 'PixelifySans-Medium.ttf', 'PixelifySans-Regular.ttf'
YELLOW, INK, SOFT, DARK = (255, 214, 64), (240, 242, 248), (150, 156, 172), (20, 20, 24)

def clamp01(t): return max(0.0, min(1.0, t))
def ease(t): t = clamp01(t); return 1 - (1 - t) ** 3
def back(t):	# overshoots a little, then settles
    t = clamp01(t); c = 1.6; return 1 + (c + 1) * (t - 1) ** 3 + c * (t - 1) ** 2
def rnd(n):	# a fixed random number for n
    return (math.sin(n * 12.9898 + 78.233) * 43758.5453) % 1.0

# ---- the beat ---------------------------------------------------------------------------------------------------------
BEATS = json.load(open(os.path.join(HERE, 'wasabi-beats.json')))
def beat_at(t):	# the beat time nearest t
    return min(BEATS, key=lambda b: abs(b - t))
DROP = beat_at(10.77)
I0 = BEATS.index(DROP)
def after(n): return BEATS[I0 + n] - MUSIC_AT
def pulse(t, every=1):
    # 1 on a beat, dying away before the next; every=4 for the downbeats (counted from the drop); quieter before the
    # drop and in the song's calm middle
    st = t + MUSIC_AT
    k = None
    for j, b in enumerate(BEATS):
        if b > st: break
        if (j - I0) % every == 0: k = j
    if k is None: return 0.0
    dt = st - BEATS[k]
    amp = 0.35 if st < DROP - 0.05 else 0.5 if 54.4 < st < 76.0 else 1.0
    return amp * math.exp(-dt * (9 if every == 1 else 5))

# ---- pictures ---------------------------------------------------------------------------------------------------------
ICONS = ROOT + '/ui/assets/rnds/icons/'
def grow(im, k): return im.resize((im.width * k, im.height * k), Image.NEAREST)
_icons = {}
def icon(name):
    if name not in _icons: _icons[name] = Image.open(ICONS + name + '.png').convert('RGBA')
    return _icons[name]
def straight_b(im):
    # the logo's B with a straight left side: its stem runs up into the top corner and down into the bottom one
    im = im.copy()
    for y in (3, 4, 15, 16):
        for x in (29, 30): im.putpixel((x, y), im.getpixel((31, y)))
    for x in (30, 31): im.putpixel((x, 17), im.getpixel((32, 17)))	# its shadow under the new corner
    return im
LOGO1 = straight_b(Image.open(ROOT + '/ui/assets/rnds/logo@1x.png').convert('RGBA'))
def asset(name, k):
    im = Image.open(ROOT + '/web/preview/assets/' + name).convert('RGBA')
    return im.resize((int(im.width * k), int(im.height * k)), Image.LANCZOS)
SUPERDRASTIC, BANANADS = asset('superdrastic-logo.png', 0.62), asset('bananads-logo.png', 0.62)

def bitmap(rows, colors):
    # pixel art from rows of characters, one colour a character (others are clear)
    im = Image.new('RGBA', (len(rows[0]), len(rows)), (0, 0, 0, 0))
    for y, row in enumerate(rows):
        for x, c in enumerate(row):
            if c in colors: im.putpixel((x, y), colors[c] + (255,))
    return im
RUNE = ['.....X.....', '.....XX....', '.....X.X...', '.....X..X..', '.X...X...X.', '..X..X..X..', '...X.X.X...',
        '....XXX....', '.....X.....', '....XXX....', '...X.X.X...', '..X..X..X..', '.X...X...X.', '.....X..X..',
        '.....X.X...', '.....XX....', '.....X.....']
def bt_chip():
    # the Bluetooth mark, white on a blue chip with its corners cut
    w, h = 17, 23
    rows = []
    for y in range(h):
        row = ''
        for x in range(w):
            corner = (x in (0, w - 1) and y in (0, 1, h - 2, h - 1)) or (y in (0, h - 1) and x in (0, 1, w - 2, w - 1))
            edge = x in (0, w - 1) or y in (0, h - 1)
            r, c = y - 3, x - 3
            if corner: row += ' '
            elif 0 <= r < 17 and 0 <= c < 11 and RUNE[r][c] == 'X': row += 'w'
            elif edge: row += 'e'
            else: row += 'b'
        rows.append(row)
    return bitmap(rows, {'b': (44, 104, 232), 'e': (24, 64, 170), 'w': (255, 255, 255)})
BT_CHIP = bt_chip()
ARROW = bitmap(['  ooooo  ', '  oyyyo  ', '  oyyyo  ', '  oyyyo  ', 'ooooyoooo', 'oyyyyyyyo', ' oyyyyyo ', '  oyyyo  ',
                '   oyo   ', '    o    '], {'y': YELLOW, 'o': (120, 84, 10)})
SERVER = bitmap(['oooooooooooooo', 'o............o', 'o.ggg....y.y.o', 'o............o', 'oooooooooooooo',
                 'o............o', 'o.ggg....y.g.o', 'o............o', 'oooooooooooooo', 'o............o',
                 'o.ggg....g.g.o', 'o............o', 'oooooooooooooo', '  o        o  ', ' ooo      ooo '],
                {'o': (190, 194, 206), '.': (58, 62, 76), 'g': (110, 116, 132), 'y': YELLOW})
BOLT = bitmap(['    oooo', '   oyyo ', '  oyyo  ', ' oyyyooo', 'oyyyyyo ', '   oyo  ', '  oyo   ', ' oyo    ',
               ' oo     ', 'o       '], {'y': YELLOW, 'o': (120, 84, 10)})

# ---- 3D ---------------------------------------------------------------------------------------------------------------
def rot(yaw, pitch, roll=0.0):
    # a negative yaw turns the front toward the left of the frame (toward the words)
    y, p, r = -math.radians(yaw), math.radians(pitch), math.radians(roll)
    Ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]])
    Rx = np.array([[1, 0, 0], [0, math.cos(p), -math.sin(p)], [0, math.sin(p), math.cos(p)]])
    Rz = np.array([[math.cos(r), -math.sin(r), 0], [math.sin(r), math.cos(r), 0], [0, 0, 1]])
    return Rz @ Rx @ Ry
LIGHT = np.array([0.45, 0.55, 0.7]); LIGHT /= np.linalg.norm(LIGHT)	# coming from the upper left, in front

class Voxels:
    # pixel art as cubes: a pixel a cube, `depth` cubes deep; only the faces with nothing next to them are kept
    def __init__(self, im, depth=4):
        a = np.asarray(im.convert('RGBA'))
        h, w = a.shape[:2]
        on = a[..., 3] >= 128
        Q, N, C = [], [], []
        for y, x in zip(*np.nonzero(on)):
            c = a[y, x, :3].astype(float)
            x0, x1, y0, y1, z0, z1 = x - w / 2, x + 1 - w / 2, y - h / 2, y + 1 - h / 2, -depth / 2, depth / 2
            faces = [([[x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0]], (0, 0, -1)),
                     ([[x1, y0, z1], [x0, y0, z1], [x0, y1, z1], [x1, y1, z1]], (0, 0, 1))]
            if x == 0 or not on[y, x - 1]: faces.append(([[x0, y0, z1], [x0, y0, z0], [x0, y1, z0], [x0, y1, z1]], (-1, 0, 0)))
            if x == w - 1 or not on[y, x + 1]: faces.append(([[x1, y0, z0], [x1, y0, z1], [x1, y1, z1], [x1, y1, z0]], (1, 0, 0)))
            if y == 0 or not on[y - 1, x]: faces.append(([[x0, y0, z1], [x1, y0, z1], [x1, y0, z0], [x0, y0, z0]], (0, -1, 0)))
            if y == h - 1 or not on[y + 1, x]: faces.append(([[x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]], (0, 1, 0)))
            for q, n in faces: Q.append(q); N.append(n); C.append(c)
        self.Q, self.N, self.C = np.array(Q, float), np.array(N, float), np.array(C, float)
        self.size = max(w, h)

    def render(self, k, yaw, pitch, roll=0.0, dist=3.2, glow=0.0):
        # the cubes k px wide, turned, in perspective (the camera dist object-sizes away); returns the picture and where
        # the object's centre falls in it
        R = rot(yaw, pitch, roll)
        Q = self.Q @ R.T * k; N = self.N @ R.T
        D = dist * self.size * k
        Z = Q[..., 2] + D
        P = Q[..., :2] * (D / Z)[..., None]
        view = Q.mean(1) + np.array([0, 0, D])
        vis = (N * view).sum(1) < 0
        light = np.clip(-(N @ LIGHT), 0, 1)
        cols = np.clip(self.C * (0.42 + 0.68 * light)[:, None] + glow * 60, 0, 255).astype(np.uint8)
        order = np.argsort(-Z.mean(1))
        order = order[vis[order]]
        ss = 2
        # drawn at twice the size, then halved: smooth edges
        lo = P.reshape(-1, 2).min(0) - 2; hi = P.reshape(-1, 2).max(0) + 2
        size = ((hi - lo) * ss).astype(int) + 1
        im = Image.new('RGBA', (int(size[0]), int(size[1])), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        Pd = (P - lo) * ss
        for f in order:
            c = tuple(int(v) for v in cols[f]) + (255,)
            d.polygon([tuple(p) for p in Pd[f]], fill=c, outline=c)
        im = im.resize((im.width // ss, im.height // ss), Image.LANCZOS)
        return im, (-lo[0], -lo[1])

def warp(im, quad):
    # im onto the quadrilateral quad (four corners, clockwise from its top left); returns the picture and its offset
    quad = np.array(quad, float)
    x0, y0 = np.floor(quad.min(0)); x1, y1 = np.ceil(quad.max(0))
    dst = quad - [x0, y0]
    w, h = im.size
    A, b = [], []
    for (x, y), (u, v) in zip(dst, [[0, 0], [w, 0], [w, h], [0, h]]):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); b.append(u)
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y]); b.append(v)
    c = np.linalg.solve(np.array(A), np.array(b))
    out = im.convert('RGBa').transform((int(x1 - x0), int(y1 - y0)), Image.PERSPECTIVE, tuple(c), Image.BICUBIC).convert('RGBA')
    return out, (x0, y0)
def project(pts, cx, cy, yaw, pitch, roll=0.0, f=2400):
    p = np.array(pts, float) @ rot(yaw, pitch, roll).T
    z = p[:, 2] + f
    return p[:, :2] * (f / z)[:, None] + [cx, cy]
def panel(im, cx, cy, yaw, pitch, roll=0.0, f=2400, s=1.0):
    w, h = im.size[0] * s, im.size[1] * s
    return warp(im, project([[-w / 2, -h / 2, 0], [w / 2, -h / 2, 0], [w / 2, h / 2, 0], [-w / 2, h / 2, 0]], cx, cy, yaw, pitch, roll, f))
def sheen(im, at):
    # a soft diagonal light across a panel, at 0..1 of its width
    w, h = im.size
    g = Image.new('L', (w // 4, h // 4), 0)
    d = ImageDraw.Draw(g)
    x = (-0.4 * w + at * 1.8 * w) / 4; hh = h / 4
    for k, a in ((0, 30), (15, 18), (32, 9)):
        d.polygon([(x - 22 - k, 0), (x + 22 + k, 0), (x + 22 + k - 0.5 * hh, hh), (x - 22 - k - 0.5 * hh, hh)], fill=a)
    g = g.filter(ImageFilter.GaussianBlur(8)).resize((w, h), Image.BILINEAR)
    white = Image.new('RGBA', (w, h), (255, 255, 255, 0))
    white.putalpha(Image.fromarray(np.minimum(np.asarray(g), np.asarray(im.getchannel('A')))))
    out = im.copy(); out.alpha_composite(white); return out

# ---- the screens, the Studio ------------------------------------------------------------------------------------------
SW, SH, GAP = 600, 450, 28
def handheld():
    w, h = SW + 12, 2 * (SH + 12) + GAP
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top, bot = (0, 0, w, SH + 12), (0, SH + 12 + GAP, w, h)
    for box in (top, bot): d.rounded_rectangle(box, 20, fill=(14, 14, 17), outline=(60, 62, 72), width=2)
    return im, top, bot
DEVICE, TOPBOX, BOTBOX = handheld()
MASK = Image.new('L', (SW, SH), 0); ImageDraw.Draw(MASK).rounded_rectangle((0, 0, SW - 1, SH - 1), 14, fill=255)
_cache = {}
def screen(theme, t, side):
    n = max(0, min(221, int(t * FPS)))
    key = (theme, n, side)
    if key not in _cache:
        if len(_cache) > 24: _cache.clear()
        _cache[key] = Image.open(f'{SCREENS}/{theme}/f{n:04d}-{side}.jpg').convert('RGB').resize((SW, SH), Image.LANCZOS)
    return _cache[key]
# the dates on the screenshots' menu clocks (a viewer could take one for a release date), in the 2000 px wide
# originals: each row painted over with the background just left of it
DATES = {'1.webp': [(1258, 658, 1296, 672), (1218, 1036, 1259, 1049)], '2.webp': [(1140, 158, 1190, 177)]}
def undate(im, boxes):
    k = im.width / 2000
    px = im.load()
    for x0, y0, x1, y1 in boxes:
        x0, y0, x1, y1 = int(x0 * k), int(y0 * k), int(x1 * k), int(y1 * k)
        for y in range(y0, y1):
            c = px[x0 - 2, y]
            for x in range(x0, x1): px[x, y] = c
    return im
STUDIO_W = 1000
def studio_windows():
    # the app's window around each screenshot
    shots = []
    for f in ('1.webp', '2.webp'):
        p = os.path.join(STUDIO, f)
        if os.path.exists(p):
            im = undate(Image.open(p).convert('RGB'), DATES[f])
            shots.append(im.resize((STUDIO_W, int(im.height * STUDIO_W / im.width)), Image.LANCZOS))
    if not shots: return []
    bar, h = 40, max(s.height for s in shots)
    wins = []
    for s in shots:
        win = Image.new('RGBA', (STUDIO_W + 4, h + bar + 4), (0, 0, 0, 0))
        wd = ImageDraw.Draw(win)
        wd.rounded_rectangle((0, 0, win.width - 1, win.height - 1), 14, fill=(30, 31, 38), outline=(70, 72, 86), width=2)
        for c, col in enumerate([(255, 95, 86), (255, 189, 46), (39, 201, 63)]): wd.ellipse((18 + c * 24, 13, 32 + c * 24, 27), fill=col)
        tf = F(REG, 20); title = 'Banana Theme Studio'
        wd.text(((win.width - tf.getlength(title)) / 2, 10), title, font=tf, fill=(200, 204, 216))
        win.paste(s.resize((STUDIO_W, h)), (2, bar + 2))
        wins.append(win)
    return wins
WINDOWS = studio_windows()

# the GBA companions, recorded on an RG DS Plus (the media/companionvideo branch): 512 x 768, the top panel over the
# bottom one, captured at 10 fps; frames extracted once into screens/
COMPANION = os.environ.get('COMPANION', '')	# the folder holding companionvideo.mp4 and pokemoncompanionvideo.mp4
RECORDINGS = ['companionvideo', 'pokemoncompanionvideo']
COMPANION_AT = [4.6, 6.0]	# where each clip starts: Sonic's moves and zones pages, Pokemon's party and summary (the
				# achievements page after 19 s shows a date)
def extract_companions():
    for name in RECORDINGS:
        src, out = os.path.join(COMPANION, name + '.mp4'), os.path.join(SCREENS, name)
        if not COMPANION or not os.path.exists(src) or os.path.exists(out + '/f0001.jpg'): continue
        os.makedirs(out, exist_ok=True)
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', src, '-vf', 'fps=10', '-q:v', '2', out + '/f%04d.jpg'], check=True)
extract_companions()
_ccache = {}
def companion(k, t, side):
    p = os.path.join(SCREENS, RECORDINGS[k], f'f{max(1, int(t * 10) + 1):04d}.jpg')
    if not os.path.exists(p): return None
    key = (p, side)
    if key not in _ccache:
        if len(_ccache) > 24: _ccache.clear()
        im = Image.open(p).convert('RGB')
        im = im.crop((0, 0, 512, 384) if side == 'top' else (0, 384, 512, 768))
        if side == 'top': ImageDraw.Draw(im).rectangle((360, 0, 511, 11), fill=(0, 0, 0))	# the FPS counter, on the black above the game
        _ccache[key] = im.resize((SW, SH), Image.LANCZOS)
    return _ccache[key]

def card(w, h, r=24):
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=(20, 21, 28, 240), outline=(62, 64, 78, 255), width=2)
    return im

# ---- the story --------------------------------------------------------------------------------------------------------
gba_line = f'{COMPANIONS} games now have a GBA companion.' if COMPANIONS else 'Your GBA games, joined by their companions.'
SCENES = [
    dict(beats=None, end=beat_at(5.15) - MUSIC_AT, kind='open', glow=(255, 190, 50)),
    dict(beats=None, end=after(0), kind='whatsnew', glow=(255, 190, 50)),
    dict(beats=16, kind='themes', glow=(60, 130, 230), n=1, tag='NEW THEME ENGINE', name='Make it yours',
         head='Change anything on both screens with one JSON file.',
         points=['Layouts, colours, fonts and animations', 'Hooks for menus and system info', 'Three example themes to start from']),
    dict(beats=16, kind='studio', glow=(60, 200, 190), n=1, tag='BANANA THEME STUDIO', name='Banana Theme Studio',
         head='Design your theme on your computer.',
         points=['Drag elements onto both screens', 'Layers, components and assets', 'An exact preview from the real engine']),
    dict(beats=16, kind='gba', glow=(120, 90, 240), n=2, tag='GBA COMPANIONS', name='GBA game companions', head=gba_line,
         points=['Moves, zones, items and progress', 'Pokémon: your party, summaries and battles', 'On the second screen while you play']),
    dict(beats=12, kind='bt', glow=(50, 110, 255), n=3, tag='BLUETOOTH AUDIO', name='Bluetooth audio',
         head='Play with wireless headphones and speakers.', points=[]),
    dict(beats=12, kind='ota', glow=(255, 160, 50), n=4, tag='OTA UPDATER', name='Updates over the air',
         head='Update BananaOS right from your handheld.', points=[]),
    dict(beats=18, kind='res', glow=(255, 200, 60), n=5, tag='BANANADS', name='BananaDS, upgraded',
         head='Faster, and new 3x and 4x resolutions.', points=[]),
    dict(beats=12, kind='latency', glow=(255, 100, 70), n=6, tag='LOW LATENCY MODE', name='Low latency mode',
         head='For both SuperDrastic and BananaDS.', points=[]),
    dict(beats=14, kind='sync', glow=(70, 210, 120), n=7, tag='HOME SERVER SYNC', name='Home server sync',
         head='Your ROMs and saves, synced with your own server.', points=[]),
    dict(beats=12, kind='more', glow=(255, 190, 50)),
    dict(beats=18, kind='end', glow=(255, 190, 50)),
]
t0, nb = 0.0, 0
for s in SCENES:
    s['at'] = t0
    if s['beats']: nb += s['beats']; s['end'] = after(nb)
    s['len'] = s['end'] - t0; t0 = s['end']
DURATION = t0
TEXT_OUT, TEXT_GAP = 0.45, 0.2

# ---- the background ---------------------------------------------------------------------------------------------------
HORIZON, VPX = 690, 1240
def base(glow):
    bg = Image.new('RGB', (W, H), (8, 8, 11))
    g = Image.new('L', (W, H), 0)
    ImageDraw.Draw(g).ellipse([W * 0.40, -H * 0.35, W * 1.18, H * 1.25], fill=170)
    g = g.filter(ImageFilter.GaussianBlur(230))
    bg = Image.composite(Image.new('RGB', (W, H), tuple(int(c * 0.34) for c in glow)), bg, g)
    # the horizon's haze
    h = Image.new('L', (W, H), 0)
    ImageDraw.Draw(h).ellipse([VPX - 900, HORIZON - 70, VPX + 900, HORIZON + 70], fill=120)
    h = h.filter(ImageFilter.GaussianBlur(60))
    bg = Image.composite(Image.new('RGB', (W, H), tuple(int(c * 0.45) for c in glow)), bg, h)
    d = ImageDraw.Draw(bg)
    for y in range(20, HORIZON - 40, 36):
        for x in range(20, W, 36): d.point((x, y), fill=(28, 29, 36))
    return bg
BASES = [base(s['glow']) for s in SCENES]
def vignette():
    y, x = np.mgrid[0:H, 0:W]
    r = np.sqrt(((x - W / 2) / (W * 0.62)) ** 2 + ((y - H / 2) / (H * 0.62)) ** 2)
    return (1 - 0.55 * np.clip(r - 0.45, 0, 1) ** 1.6)[..., None].astype(np.float32)
VIGNETTE = vignette()

def floor(d, t, glow, beat):
    # a floor grid running to the horizon, coming toward the camera, brighter on the beat
    col = tuple(min(255, int(c * 0.9 + 40)) for c in glow)
    camh, f = 260.0, 700.0
    vx = VPX + 60 * math.sin(t * 0.21)
    for k in range(-26, 27):
        xw = k * 150.0
        zn, zf = 120.0, 6000.0
        a = int((34 + 30 * beat) * (1 - abs(k) / 30))
        d.line([(vx + xw * f / zn, HORIZON + camh * f / zn), (vx + xw * f / zf, HORIZON + camh * f / zf)], fill=col + (a,), width=2)
    scroll = (t * 420.0) % 300.0
    for k in range(22):
        z = 150.0 + k * 300.0 - scroll
        if z < 110: continue
        y = HORIZON + camh * f / z
        a = int((70 + 60 * beat) * clamp01((y - HORIZON) / 260.0))
        d.line([(0, y), (W, y)], fill=col + (a,), width=2 if y > HORIZON + 60 else 1)

def dust(d, t, beat):
    # pixel dust floating toward the camera
    for n in range(90):
        x0, y0 = (rnd(n) - 0.5) * 5200, (rnd(n + 300) - 0.62) * 3200
        span = 3000.0
        z = span - ((t * (140 + 160 * rnd(n + 600)) + rnd(n + 900) * span) % span) + 200
        x, y = VPX + x0 * 900 / z, 470 + y0 * 900 / z
        s = max(2, int(9000 / z))
        a = int(clamp01((span - z) / 600) * clamp01(z / 900 - 0.2) * (110 + 90 * beat))
        if a <= 2 or not (-20 < x < W + 20 and -20 < y < H + 20): continue
        d.rectangle((x, y, x + s, y + s), fill=(YELLOW if n % 3 else INK) + (a,))

_wipe = {}
def wipe_mask(p, seed, size):
    # the pixel-block wipe: blocks of 48 px flipping in a diagonal sweep, as a mask of size
    if seed not in _wipe:
        gw, gh = W // 48 + 1, H // 48 + 1
        gy, gx = np.mgrid[0:gh, 0:gw]
        noise = np.array([[rnd(a * 31 + b * 17 + seed) for a in range(gw)] for b in range(gh)])
        _wipe[seed] = (gx / gw * 0.8 + gy / gh * 0.25) * 0.8 + noise * 0.2
    shown = _wipe[seed] < p * 1.25 - 0.1
    if size is None: return _wipe[seed], p * 1.25 - 0.1
    m = Image.fromarray((shown * 255).astype(np.uint8)).resize((shown.shape[1] * 48, shown.shape[0] * 48), Image.NEAREST)
    return m.crop((0, 0) + size)
WIPE = 0.5

# ---- text -------------------------------------------------------------------------------------------------------------
def text(d, xy, s, font, fill, alpha):
    if alpha > 0: d.text(xy, s, font=font, fill=fill + (int(255 * min(1, alpha)),))
def letters(d, x, y, s, font, fill, local, t0, step=0.025, dur=0.45, lift=34):
    # one letter at a time, each rising into place
    for k, ch in enumerate(s):
        a = ease((local - t0 - step * k) / dur)
        if a > 0: text(d, (x + font.getlength(s[:k]), y + lift * (1 - a)), ch, font, fill, a)
def centred(d, cx, y, s, font, fill, alpha): text(d, (cx - font.getlength(s) / 2, y), s, font, fill, alpha)
def shine(layer, box, p):
    # a light sweeping across what is already drawn in box (the title), at p 0..1
    if not 0 < p < 1: return
    x0, y0, x1, y1 = [int(v) for v in box]
    region = layer.crop((x0, y0, x1, y1))
    w, h = region.size
    band = Image.new('L', (w, h), 0)
    cx = -120 + p * (w + 240)
    ImageDraw.Draw(band).polygon([(cx - 40, 0), (cx + 40, 0), (cx + 10, h), (cx - 70, h)], fill=230)
    band = band.filter(ImageFilter.GaussianBlur(10))
    m = Image.fromarray((np.asarray(band, np.float32) * np.asarray(region.getchannel('A'), np.float32) / 255).astype(np.uint8))
    white = Image.new('RGBA', (w, h), (255, 255, 255, 0)); white.putalpha(m)
    layer.alpha_composite(white, (x0, y0))
def paste(layer, im, x, y, alpha=1.0):
    # at a fraction of a pixel (pixel art stepping whole pixels as it eases shakes)
    if alpha <= 0: return
    if alpha < 1:
        im = im.copy(); im.putalpha(im.getchannel('A').point(lambda v: int(v * alpha)))
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    if fx > 0.01 or fy > 0.01:
        im = im.convert('RGBa').transform((im.width + 1, im.height + 1), Image.AFFINE, (1, 0, -fx, 0, 1, -fy), Image.BILINEAR).convert('RGBA')
    layer.alpha_composite(im, (ix, iy))
_shadow = {}
def soft_shadow(w, h, a=150):
    key = (w // 8, h // 8, a)
    if key not in _shadow:
        s = Image.new('RGBA', (w + 200, h + 200), (0, 0, 0, 0))
        ImageDraw.Draw(s).ellipse((100, 100, w + 100, h + 100), fill=(0, 0, 0, a))
        _shadow[key] = s.filter(ImageFilter.GaussianBlur(26))
    return _shadow[key]

VX = 1360	# the pictures' centre on the right
L = 150	# the words' left edge
VOX = {}
def draw_vox(layer, name, im, k, cx, cy, yaw, pitch, roll=0.0, alpha=1.0, depth=4, shadow=True, glow=0.0):
    if name not in VOX: VOX[name] = Voxels(im, depth)
    pic, (ox, oy) = VOX[name].render(k, yaw, pitch, roll, glow=glow)
    if shadow:
        sh = soft_shadow(int(pic.width * 0.8), 60, 120)
        paste(layer, sh, cx - sh.width / 2, cy + pic.height * 0.42 - 70, alpha * 0.9)
    paste(layer, pic, cx - ox, cy - oy, alpha)

# ---- a frame ----------------------------------------------------------------------------------------------------------
def frame(t):
    i = max(k for k, s in enumerate(SCENES) if s['at'] <= t)
    S = SCENES[i]; local = t - S['at']; kind = S['kind']
    beat, down = pulse(t), pulse(t, 4)

    # the background: the scene's colour, the floor and the dust; the pixel wipe into it from the scene before
    img = BASES[i].copy()
    img = img.convert('RGBA')
    over = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    od = ImageDraw.Draw(over)
    if i > 0 and local < WIPE:
        img = Image.composite(img, BASES[i - 1].convert('RGBA'), wipe_mask(local / WIPE, i, (W, H)))
        # the sweep's front: blocks lit in yellow
        grid, front = wipe_mask(local / WIPE, i, None)
        for by, bx in zip(*np.nonzero(np.abs(grid - front) < 0.05)):
            od.rectangle((bx * 48, by * 48, bx * 48 + 47, by * 48 + 47), fill=YELLOW + (int(45 + 50 * rnd(bx * 7 + by * 13 + i)),))
    floor(od, t, S['glow'], beat)
    dust(od, t, beat)
    img.alpha_composite(over)

    out = 1 - clamp01((local - (S['len'] - TEXT_GAP - TEXT_OUT)) / TEXT_OUT)
    if i == len(SCENES) - 1: out = 1
    vis = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    words = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(words, 'RGBA')
    vd = ImageDraw.Draw(vis, 'RGBA')
    def at(t0, dur=0.5): return ease((local - t0) / dur)
    def rise(t0, dur=0.5): return 18 * (1 - ease((local - t0) / dur))

    if kind == 'open':
        a = back(local / 1.6)
        yaw = 70 * (1 - ease(local / 1.8)) + 7 * math.sin(local * 1.1)
        draw_vox(vis, 'logo', LOGO1, 9 + 3 * ease(local / 1.8), W / 2, 400 - 30 * (1 - a), yaw, 12 * (1 - ease(local / 1.8)) - 4,
                 alpha=clamp01(local / 0.6), shadow=False, glow=0.3 * down)
        f = F(MED, 66); s = 'Version 0.5.0'
        letters(d, W / 2 - f.getlength(s) / 2, 540, s, f, YELLOW, local, 1.2)
        pill = 'THE FIRST BETA'; pf = F(MED, 26); pw = pf.getlength(pill) + 40
        a = at(2.0)
        if a > 0:
            d.rounded_rectangle((W / 2 - pw / 2, 650 + rise(2.0), W / 2 + pw / 2, 696 + rise(2.0)), 23, fill=YELLOW + (int(255 * a),))
            centred(d, W / 2, 659 + rise(2.0), pill, pf, DARK, a)
    elif kind == 'whatsnew':
        # the seven features' pictures circling behind the words
        ring = [('nds', icon('nds')), ('gba', icon('gba')), ('chip', BT_CHIP), ('arrow', ARROW), ('server', SERVER),
                ('bolt', BOLT), ('gear', icon('gear'))]
        items = []
        for n, (name, im) in enumerate(ring):
            ang = local * 0.55 + n * 2 * math.pi / len(ring)
            items.append((math.sin(ang) * 300, n, name, im, math.cos(ang) * 820))
        for z, n, name, im, x in sorted(items, key=lambda e: -e[0]):
            sc = 1100 / (1100 + z)
            kk = 4.4 * sc * 32 / max(im.size)
            draw_vox(vis, name, im, kk, W / 2 + x * sc, 800 - z * 0.3, math.degrees(-math.atan2(x, 1500)) + 20 * math.sin(local + n), 8,
                     alpha=at(0.2 + 0.08 * n, 0.6) * (0.55 + 0.45 * sc), shadow=False, glow=0.25 * beat)
        f = F(MED, 104); s = "Here's what's new."
        letters(d, W / 2 - f.getlength(s) / 2, 330, s, f, INK, local, 0.3, lift=40)
        shine(words, (W / 2 - f.getlength(s) / 2 - 20, 320, W / 2 + f.getlength(s) / 2 + 20, 460), (local - 1.5) / 0.9)
        centred(d, W / 2, 480 + rise(1.1), 'Seven new features, and plenty more.', F(REG, 42), YELLOW, at(1.1))
    elif kind == 'more':
        names = sorted(f[:-4] for f in os.listdir(ICONS) if f.endswith('.png'))
        cols, step, k = 7, 122, 3
        rows = math.ceil(len(names) / cols)
        x0, y0 = 1865 - (cols - 1) * step - 48, H / 2 - (rows - 1) * step / 2 - 48
        for n, name in enumerate(names):
            c, r = n % cols, n // cols
            a = (local - 0.25 - 0.03 * (c + r * 1.5)) / 0.45
            if a <= 0: continue
            flip = math.cos((1 - ease(a)) * math.pi / 2)	# turning in about its upright axis
            im = grow(icon(name), k)
            w2 = max(1, int(im.width * flip))
            paste(vis, im.resize((w2, im.height), Image.NEAREST), x0 + c * step + (96 - w2) / 2, y0 + r * step + 8 * math.sin(local * 2.2 + n * 0.7), clamp01(a * 2))
        letters(d, L, 470, 'And much more.', F(MED, 96), INK, local, 0.2)
        shine(words, (L - 10, 465, L + 820, 590), (local - 1.4) / 0.9)
    elif kind == 'end':
        a = back(local / 1.4)
        draw_vox(vis, 'logo', LOGO1, 12, W / 2, 380 - 30 * (1 - a), 9 * math.sin(local * 0.9) + 40 * (1 - ease(local / 1.4)),
                 -4 + 3 * math.sin(local * 0.7), alpha=clamp01(local / 0.5), shadow=False, glow=0.35 * down)
        big = F(MED, 60); line = 'Coming very soon in 0.5.0'
        x = (W - big.getlength(line + ' beta')) / 2
        letters(d, x, 540, line, big, YELLOW, local, 0.9, step=0.02)
        text(d, (x + big.getlength(line + ' '), 540), 'beta', big, INK, clamp01((local - 2.4) / 1.8) ** 1.5)
        centred(d, W / 2, 640 + rise(1.8), 'The first beta: themes, companions, Bluetooth audio, OTA updates and more.', F(REG, 34), SOFT, at(1.8))
    else:
        # a feature: the tag, the name letter by letter, the line under it, the points; the picture on the right
        tag = F(MED, 22); tw = tag.getlength(S['tag'])
        a = at(0.15, 0.35)
        if a > 0:
            d.rounded_rectangle((L, 290, L + max(40, (tw + 34) * a), 330), 20, fill=YELLOW + (255,))
            if a > 0.9: text(d, (L + 17, 297), S['tag'], tag, DARK, (a - 0.9) / 0.1)
        nf = F(MED, 86 if F(MED, 86).getlength(S['name']) < 700 else 70)
        letters(d, L, 350, S['name'], nf, INK, local, 0.3)
        shine(words, (L - 10, 345, L + nf.getlength(S['name']) + 20, 460), (local - 1.3) / 0.8)
        hf = F(REG, 40); lines = ['']
        for w_ in S['head'].split(' '):
            t_ = (lines[-1] + ' ' + w_).strip()
            if hf.getlength(t_) > 760 and lines[-1]: lines.append(w_)
            else: lines[-1] = t_
        y = 474
        for k, ln in enumerate(lines):
            text(d, (L, y + rise(0.6 + 0.1 * k)), ln, hf, YELLOW, at(0.6 + 0.1 * k)); y += 54
        y += 24
        for k, p in enumerate(S['points']):
            t1 = 1.0 + 0.25 * k
            a = at(t1)
            if a > 0: d.rectangle((L, y + 14 + rise(t1), L + 10, y + 24 + rise(t1)), fill=YELLOW + (int(255 * a),))
            text(d, (L + 28, y + rise(t1)), p, F(REG, 32), INK, a)
            y += 52
        # where we are in the seven
        text(d, (L, 940), f'NEW IN 0.5.0   {S["n"]:02d} / 07', F(MED, 22), SOFT, at(0.2))
        for k in range(7):
            d.rectangle((L + 280 + k * 22, 948, L + 292 + k * 22, 960), fill=(YELLOW if k < S['n'] else (60, 62, 74)) + (int(255 * at(0.25 + 0.03 * k)),))
        draw_feature(vis, vd, S, local, beat)

    # the camera: a slow push through a scene (the words stay crisp, on top)
    if out < 1:
        vis.putalpha(vis.getchannel('A').point(lambda v: int(v * out)))
        words.putalpha(words.getchannel('A').point(lambda v: int(v * out)))
    img.alpha_composite(vis)
    z = 1 + 0.025 * clamp01(local / max(1, S['len']))
    if z > 1.0005:
        cw, ch = W / z, H / z
        img = img.resize((W, H), Image.BILINEAR, box=((W - cw) / 2, (H - ch) / 2, (W + cw) / 2, (H + ch) / 2))
    img.alpha_composite(words)

    # bloom, the vignette, grain, the fades and a flash on the drop
    rgb = img.convert('RGB')
    a = np.asarray(rgb, np.float32)
    small = np.asarray(rgb.resize((W // 4, H // 4), Image.BILINEAR), np.float32)
    bl = Image.fromarray(np.clip((small - 150) * 1.6, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(7)).resize((W, H), Image.BILINEAR)
    a += np.asarray(bl, np.float32) * (0.42 + 0.25 * down)
    a *= VIGNETTE
    a += np.random.default_rng(int(t * FPS)).integers(-5, 6, (H, W, 1)).astype(np.float32)
    a *= clamp01(t / 0.4) * clamp01((DURATION - t) / 1.2)
    td = DROP - MUSIC_AT
    if t >= td: a += 90 * clamp01(1 - (t - td) / 0.25)
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))

THEMES = ['hillside', 'turntable', 'classic']
def draw_feature(vis, vd, S, local, beat):
    kind = S['kind']
    a_in = ease((local - 0.25) / 0.9)
    lift = 120 * (1 - back((local - 0.25) / 1.1))
    if kind == 'themes':
        # the example themes on the two screens, tilted and swaying, a pixel wipe from one to the next
        per = (S['len'] - 0.6) / 3
        k = min(2, int(local // per)); lt = local - k * per
        dev = DEVICE.copy()
        for side, box in (('top', TOPBOX), ('bot', BOTBOX)):
            s = screen(THEMES[k], 0.8 + lt, side)
            if k > 0 and lt < 0.4:
                s = Image.composite(s, screen(THEMES[k - 1], 0.8 + per, side), wipe_mask(lt / 0.4, k * 7 + (side == 'bot'), (SW, SH)))
            dev.paste(s, (box[0] + 6, box[1] + 6), MASK)
        dev = sheen(dev, clamp01(lt / per))
        yaw = -16 + 5 * math.sin(local * 0.7) + 30 * (1 - a_in)
        pic, (x, y) = panel(dev, VX, H / 2 + lift, yaw, 5 + 2 * math.sin(local * 0.5), -1.5, s=0.98)
        sh = soft_shadow(pic.width, 80, 160)
        paste(vis, sh, VX - sh.width / 2, y + pic.height - 60, a_in)
        paste(vis, pic, x, y, a_in)
        names = ['Hillside', 'Turntable', 'Classic']
        f = F(MED, 22); s_ = f'EXAMPLE THEME {k + 1} / 3   {names[k].upper()}'
        text(vd, (VX - f.getlength(s_) / 2, 1030), s_, f, SOFT, a_in)
    elif kind == 'studio' and WINDOWS:
        win = WINDOWS[0] if local < 3.4 else WINDOWS[-1]
        if len(WINDOWS) > 1 and 3.4 <= local < 3.9:
            win = Image.composite(WINDOWS[-1], WINDOWS[0], wipe_mask((local - 3.4) / 0.5, 99, WINDOWS[0].size))
        win = sheen(win, clamp01(local / S['len']))
        yaw = -22 + 8 * ease(local / S['len']) + 35 * (1 - a_in)
        pic, (x, y) = panel(win, 1395, H / 2 + lift, yaw, 6, -1, s=0.9)
        sh = soft_shadow(pic.width, 80, 160)
        paste(vis, sh, 1395 - sh.width / 2, y + pic.height - 60, a_in)
        paste(vis, pic, x, y, a_in)
    elif kind == 'gba':
        # the companions on the handheld's two screens: Sonic Advance, then a pixel wipe to Pokemon Emerald
        half = (S['len'] - 0.6) / 2
        k = 0 if local < half or not companion(1, 0, 'top') else 1
        lt = local - k * half
        dev = DEVICE.copy()
        for side, box in (('top', TOPBOX), ('bot', BOTBOX)):
            s = companion(k, COMPANION_AT[k] + lt, side)
            if s is None: break
            if k == 1 and lt < 0.4:
                s = Image.composite(s, companion(0, COMPANION_AT[0] + half, side), wipe_mask(lt / 0.4, 50 + (side == 'bot'), (SW, SH)))
            dev.paste(s, (box[0] + 6, box[1] + 6), MASK)
        dev = sheen(dev, clamp01(lt / half))
        yaw = -16 + 5 * math.sin(local * 0.7) + 30 * (1 - a_in)
        pic, (x, y) = panel(dev, VX, H / 2 + lift, yaw, 5 + 2 * math.sin(local * 0.5), -1.5, s=0.98)
        sh = soft_shadow(pic.width, 80, 160)
        paste(vis, sh, VX - sh.width / 2, y + pic.height - 60, a_in)
        paste(vis, pic, x, y, a_in)
        f = F(MED, 22); s_ = ['SONIC ADVANCE', 'POKÉMON EMERALD'][k] + '   ON THE RG DS PLUS'
        text(vd, (VX - f.getlength(s_) / 2, 1030), s_, f, SOFT, a_in)
    elif kind == 'bt':
        cy, cx = H / 2 - 40 + lift, VX - 250
        draw_vox(vis, 'chip', BT_CHIP, 12, cx, cy, -20 + 14 * math.sin(local * 1.2), 8, alpha=a_in, glow=0.25 * beat)
        for n in range(5):
            ph = (local * 0.8 - n * 0.2) % 1.0
            r = 70 + ph * 260
            a = ease((local - 0.8) / 0.5) * (1 - ph) * 0.95
            vd.arc((cx + 60 - r * 0.15, cy - r, cx + 60 + r * 1.15, cy + r), -38, 38, fill=(YELLOW if n % 2 else INK) + (int(255 * a),), width=10)
        b = ease((local - 0.7) / 0.9)
        draw_vox(vis, 'music', icon('music'), 9, VX + 290, cy + 14 * math.sin(local * 3) + 120 * (1 - back((local - 0.7) / 1.1)),
                 -22 + 15 * math.sin(local * 1.4), 8, alpha=b, glow=0.3 * beat)
    elif kind == 'ota':
        cx, cy = VX, H / 2 - 60 + lift
        draw_vox(vis, 'nds', icon('nds'), 10, cx, cy + 40, -18 + 10 * math.sin(local * 0.9), 14, alpha=a_in)
        p = ease((local - 1.0) / 3.4)
        if p < 0.999:
            ph = (local * 1.2) % 1.0
            draw_vox(vis, 'arrow', ARROW, 10, cx, cy - 330 + 160 * ph, 25 * math.sin(local * 2), 0,
                     alpha=ease((local - 0.5) / 0.8) * (1 - ph ** 3), shadow=False, glow=0.4 * beat)
        c = card(640, 160)
        cw = ImageDraw.Draw(c)
        cw.text((32, 26), 'BananaOS 0.5.0 beta', font=F(MED, 34), fill=INK)
        cw.text((32, 76), 'Installed' if p >= 0.999 else f'Downloading  {int(p * 100)}%', font=F(REG, 26), fill=YELLOW if p >= 0.999 else SOFT)
        cw.rounded_rectangle((32, 118, 608, 134), 8, fill=(44, 46, 56))
        if p > 0: cw.rounded_rectangle((32, 118, 32 + max(16, int(576 * p)), 134), 8, fill=YELLOW)
        pic, (x, y) = panel(sheen(c, clamp01(local / S['len'])), cx, cy + 330, -12 + 4 * math.sin(local), 18)
        e = ease((local - 0.8) / 0.7)
        paste(vis, pic, x, y + 60 * (1 - e), e)
    elif kind == 'res':
        # BananaDS, and the DS's 256 x 192 picture at 2x, 3x and 4x (it has no 1x): planes stacked in depth, turning
        paste(vis, BANANADS, VX - BANANADS.width / 2, 150 + lift * 0.3, a_in)
        yaw, pitch = -30 + 8 * math.sin(local * 0.5), 14 + 3 * math.sin(local * 0.4)
        cx, cy = VX + 20, 590 + lift
        for k in (4, 3, 2):
            a = ease((local - 0.7 - 0.55 * (4 - k)) / 0.6)
            if a <= 0: continue
            w, h = 128 * k * 1.15, 96 * k * 1.15
            z = (4 - k) * -170 + 260 * (1 - a)
            quad = project([[-w / 2, -h / 2, z], [w / 2, -h / 2, z], [w / 2, h / 2, z], [-w / 2, h / 2, z]], cx, cy, yaw, pitch, f=1800)
            new = k >= 3
            col = YELLOW if new else SOFT
            # each plane on a layer of its own: drawing on the shared one would replace what is under it
            lay = Image.new('RGBA', (W, H), (0, 0, 0, 0))
            ld = ImageDraw.Draw(lay)
            ld.polygon([tuple(q) for q in quad], fill=col + (int(40 * a),), outline=col + (int(255 * a),), width=4)
            f = F(MED, 34)
            text(ld, (quad[2][0] - f.getlength(f'{k}x') - 16, quad[2][1] - 50), f'{k}x', f, col, a)
            if new:
                text(ld, (quad[1][0] + 14, quad[1][1] - 4), f'{256 * k} x {192 * k}', F(REG, 22), col, a)
                ld.rounded_rectangle((quad[1][0] - 96, quad[1][1] + 16, quad[1][0] - 20, quad[1][1] + 50), 17, fill=YELLOW + (int(255 * a),))
                text(ld, (quad[1][0] - 84, quad[1][1] + 20), 'NEW', F(MED, 22), DARK, a)
            vis.alpha_composite(lay)
    elif kind == 'latency':
        paste(vis, SUPERDRASTIC, VX - 210 - SUPERDRASTIC.width / 2, 330 - SUPERDRASTIC.height / 2 + lift * 0.5, a_in)
        paste(vis, BANANADS, VX + 210 - BANANADS.width / 2, 330 - BANANADS.height / 2 + lift * 0.5, ease((local - 0.5) / 0.8))
        on = local > 2.0
        c = card(760, 240)
        cw = ImageDraw.Draw(c)
        cw.text((32, 28), 'Low latency mode', font=F(MED, 34), fill=INK)
        sx = 760 - 32 - 96
        k = ease((local - 2.0) / 0.25)
        cw.rounded_rectangle((sx, 28, sx + 96, 74), 23, fill=tuple(int(a + (b - a) * k) for a, b in zip((60, 62, 74), YELLOW)))
        kx = sx + 4 + 50 * k
        cw.ellipse((kx, 32, kx + 38, 70), fill=(255, 255, 255))
        cw.text((32, 108), 'Press to picture', font=F(REG, 26), fill=SOFT)
        w = 696 * (1 - 0.6 * ease((local - 2.05) / 0.9))
        cw.rounded_rectangle((32, 158, 32 + int(w), 188), 15, fill=YELLOW if on else (150, 156, 172))
        pic, (x, y) = panel(sheen(c, clamp01(local / S['len'])), VX, 660 + lift, -14 + 4 * math.sin(local * 0.8), 14)
        paste(vis, pic, x, y, ease((local - 0.8) / 0.7))
        if on:
            b = back((local - 2.05) / 0.6)
            draw_vox(vis, 'bolt', BOLT, 12, VX + 430, 560 - 40 * (1 - b), 20 * math.sin(local * 2), 6,
                     alpha=clamp01((local - 2.05) / 0.2), shadow=False, glow=0.5 * beat)
    elif kind == 'sync':
        cy = H / 2 - 70 + lift
        draw_vox(vis, 'server', SERVER, 13, VX - 330, cy, -24 + 10 * math.sin(local * 0.8), 10, alpha=a_in, glow=0.15 * beat)
        b = ease((local - 0.6) / 0.9)
        draw_vox(vis, 'nds', icon('nds'), 9, VX + 330, cy + 120 * (1 - back((local - 0.6) / 1.1)), -24 + 10 * math.sin(local * 0.8 + 1), 10, alpha=b, glow=0.15 * beat)
        f = F(REG, 30)
        text(vd, (VX - 330 - f.getlength('Your server') / 2, cy + 200), 'Your server', f, SOFT, a_in)
        text(vd, (VX + 330 - f.getlength('Your handheld') / 2, cy + 200), 'Your handheld', f, SOFT, b)
        for lane, (label, dirn, col, side) in enumerate((('ROMs', 1, YELLOW, -1), ('Saves', -1, INK, 1))):
            a = ease((local - 1.0 - 0.3 * lane) / 0.5)
            if a <= 0: continue
            for n in range(6):
                ph = (local * 0.65 + n / 6) % 1.0
                q = ph if dirn > 0 else 1 - ph
                x, y = VX - 190 + q * 380, cy + side * (30 + 110 * math.sin(math.pi * q))
                fa = a * min(1, ph * 6, (1 - ph) * 6)
                vd.rectangle((x - 11, y - 11, x + 11, y + 11), fill=col + (int(255 * fa),))
            fm = F(MED, 28)
            text(vd, (VX - fm.getlength(label) / 2, cy + side * 150 - 18 + side * 18), label, fm, col, a)

# ---- render -----------------------------------------------------------------------------------------------------------
def render(k):
    frame(k / FPS).save(f'{FRAMES}/f{k:05d}.png', compress_level=1)
    return k

if __name__ == '__main__':
    shutil.rmtree(FRAMES, True); os.makedirs(FRAMES)
    n = int(DURATION * FPS)
    with Pool(JOBS) as pool:
        for done, _ in enumerate(pool.imap_unordered(render, range(n), chunksize=8)):
            if done % 300 == 0: print(f'{done}/{n}', flush=True)
    cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(FPS), '-i', f'{FRAMES}/f%05d.png']
    if MUSIC:
        cmd += ['-ss', str(MUSIC_AT), '-i', MUSIC, '-filter:a', f'afade=t=out:st={DURATION - 3:.2f}:d=3',
                '-map', '0:v', '-map', '1:a', '-c:a', 'aac', '-b:a', '256k', '-ar', '44100', '-ac', '2', '-shortest']
    cmd += ['-c:v', 'libx264', '-preset', 'slow', '-pix_fmt', 'yuv420p', '-crf', '22', '-movflags', '+faststart', OUT]
    subprocess.run(cmd, check=True)
    shutil.rmtree(FRAMES, True)
    print(OUT, n, 'frames', f'{DURATION:.2f} s')
