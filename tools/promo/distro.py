#!/usr/bin/env python3
# BananaOS's showcase video (about two minutes, 1920x1080): the RG DS Plus from the website (its 3D model, rendered
# in headless Chromium by distro3d.html / distro3d.mjs) and BananaOS's own screens (banana-ui, headless, and the
# screenshots the website ships) beside the copy, in the trailer's look (video.py, website.py): BananaOS's font and
# logo, a scene's words fading out together before the next, cut to the music's beat.
#
#   WORK=/tmp/distro python3 tools/promo/distro.py prepare     # the website's files (main's web/preview), three.js
#   WORK=/tmp/distro python3 tools/promo/distro.py screens     # banana-ui's frames for the screens (30 a second)
#   WORK=/tmp/distro python3 tools/promo/distro.py 3d          # the handheld, posed for each shot (slow: software WebGL)
#   WORK=/tmp/distro python3 tools/promo/distro.py preview 7 30.5 ...   # single frames, WORK/preview/<t>.jpg
#   WORK=/tmp/distro SITE=<site capture> MUSIC=song.mp3 MUSIC_AT=22.384 python3 tools/promo/distro.py render out.mp4
#
# SITE is a capture of the website by site-capture.mjs (s03 the hero, s04 the features, s09 the downloads); MUSIC_AT
# is where in the track the video starts (the default puts Limoncello's drop on the logo at 0:06); JOBS frames are
# drawn at once.
import os, sys, json, math, subprocess, shutil, hashlib, glob, functools, random
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '../..'))
WORK = os.environ.get('WORK', '/tmp/bananaos-distro')
PREVIEW = os.environ.get('PREVIEW', WORK + '/site/web/preview')	# the website: the model, its screenshots, the logos
SITE = os.environ.get('SITE', '')							# the website captured (site-capture.mjs)
SITE_FPS = 15
MUSIC = os.environ.get('MUSIC', '')
MUSIC_AT = float(os.environ.get('MUSIC_AT', '22.384'))
NODE = os.environ.get('NODE_DIR', WORK + '/node')			# three@0.170.0 and playwright-core (npm install)
PORT = int(os.environ.get('PORT', '8781'))
JOBS = int(os.environ.get('JOBS', '2'))
UI, R3D, FX = WORK + '/ui', WORK + '/3d', WORK + '/fx'
W, H, FPS = 1920, 1080, 30
FONTS = ROOT + '/ui/assets/rnds/fonts/'
MED, REG = 'PixelifySans-Medium.ttf', 'PixelifySans-Regular.ttf'
F = functools.lru_cache(None)(lambda name, size: ImageFont.truetype(FONTS + name, size))
YELLOW, INK, SOFT = (255, 214, 64), (240, 242, 248), (150, 156, 172)
ASSETS = ROOT + '/ui/assets/'
LOGO = Image.open(ASSETS + 'rnds/logo@1.6x.png').convert('RGBA')
BANANA48 = Image.open(ASSETS + 'banana/banana-48.png').convert('RGBA')
def grow(im, k): return im.resize((im.width * k, im.height * k), Image.NEAREST)
BANANA = grow(BANANA48, 2)
# a pixel heart (for the copy's yellow heart), 9 x 8 pixels grown by whole pixels
HEART_ROWS = ['.##...##.', '####.####', '#########', '#########', '.#######.', '..#####..', '...###...', '....#....']
def heart(k):
    im = Image.new('RGBA', (9 * k, 8 * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for y, row in enumerate(HEART_ROWS):
        for x, c in enumerate(row):
            if c == '#': d.rectangle((x * k, y * k, x * k + k - 1, y * k + k - 1), fill=YELLOW + (255,))
    return im

def clamp01(t): return max(0.0, min(1.0, t))
def ease(t): t = clamp01(t); return 1 - (1 - t) ** 3
def easeio(t): t = clamp01(t); return 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2
def backout(t, s=1.4): t = clamp01(t) - 1; return 1 + (s + 1) * t ** 3 + s * t ** 2
def lerp(a, b, t): return a + (b - a) * t

# ---- the music's beat: 128 a minute; MUSIC_AT 22.384 puts the track's drop (28.384 s in) at 0:06 ---------------------
BEAT, FIRST = 60 / 128, 6.0
def beat(k): return FIRST + k * BEAT
def snap(t): return t if t <= FIRST else beat(round((t - FIRST) / BEAT))

# ---- the story: the storyboard's times, each cut moved to the nearest beat ----------------------------------------------
# lines: (text, kind, time it comes in); kinds: head (white, large), sub (yellow), small (grey)
SCENES = [
    dict(id='open', at=0, lines=[('Two screens.', 'head', 1.9), ('One fresh start.', 'sub', 3.0)]),
    dict(id='meet', at=6, lines=[('Meet BananaOS.', 'head', 0.7), ('Built for RG DS + RG DS Plus.', 'sub', 1.4)]),
    dict(id='menu', at=12, tag='WELCOME HOME', lines=[('A little pixel personality.', 'head', 0.3), ('Your next game starts here.', 'sub', 0.9),
                                                      ('Browse your library. Pick a favourite.', 'small', 4.0)]),
    dict(id='play', at=22, tag='WELCOME HOME', lines=[('Pick a game.', 'head', 0.3), ('Jump in.', 'sub', 2.4)]),
    dict(id='emus', at=29, tag='TWO SCREENS. TWO WAYS TO PLAY.', lines=[('Two ways to play DS.', 'head', 0.3), ('SuperDrastic + BananaDS.', 'sub', 0.9),
                                                                        ('Set your default. Or choose an emulator for each game.', 'small', 3.8)], logos=1.6),
    dict(id='sd', at=36, tag='SUPERDRASTIC', lines=[('SuperDrastic.', 'head', 0.3), ('DraStic, adapted for your handheld.', 'small', 0.8),
                                                   ('2× rendering.', 'sub', 2.4), ('Per-game tuning.', 'sub', 5.2), ('Save states.', 'sub', 8.0)]),
    dict(id='bds', at=47, tag='BANANADS', lines=[('BananaDS.', 'head', 0.3), ('Powered by melonDS.', 'small', 0.8),
                                                ('RetroAchievements.', 'sub', 2.4), ('Microphone support.', 'sub', 5.2), ('Fast-forward.', 'sub', 8.0)]),
    dict(id='sleep', at=58, tag='TAKE A BREAK', lines=[('Life happens.', 'head', 0.3), ('Close the lid. Your game pauses.', 'small', 1.2),
                                                       ('Pick up where you left off.', 'sub', 5.7)]),
    dict(id='beyond', at=66, tag='BEYOND NINTENDO DS', lines=[('Beyond DS.', 'head', 0.3), ('More classics. One handheld.', 'sub', 0.9),
                                                              ('Explore more with RetroArch.', 'small', 9.9)]),
    dict(id='home', at=78, tag='MAKE YOURSELF AT HOME', lines=[('Add your favourites.', 'head', 0.3), ('Copy games over Wi-Fi.', 'sub', 1.0),
                                                               ('Or add them directly to your microSD card.', 'small', 5.8)]),
    dict(id='fresh', at=87, tag='KEEP THINGS FRESH', lines=[('Keep things fresh.', 'head', 0.3), ('One update file.', 'sub', 0.9),
                                                            ('Install it from Settings.', 'small', 2.6),
                                                            ('The previous system version stays available in the other slot.', 'small', 5.9)]),
    dict(id='small', at=96, tag='SMALL SYSTEM. THOUGHTFUL DETAILS.', lines=[('Lightweight Linux.', 'head', 0.3), ('Built around your handheld.', 'sub', 0.9),
                                                                           ('Built with Buildroot.', 'small', 3.9)]),
    dict(id='site', at=104, yc=330, tag='READY TO TRY IT?', lines=[('Explore BananaOS.', 'head', 0.3), ('Features. Downloads. Guides.', 'sub', 1.0)]),
    dict(id='end', at=112),
]
DURATION = 120.0
for k, s in enumerate(SCENES):
    s['at'] = snap(s['at'])
for k, s in enumerate(SCENES):
    s['len'] = (SCENES[k + 1]['at'] if k + 1 < len(SCENES) else DURATION) - s['at']
SC = {s['id']: s for s in SCENES}
TEXT_OUT, TEXT_GAP = 0.5, 0.25	# a scene's words fade out together, this long, gone before the next scene starts
XFADE = 0.3						# the pictures cross over this long at a cut
# the lid in the sleep scene (scene time): closes, stays shut, opens
LID_CLOSE, LID_SHUT, LID_OPEN, LID_UP = 0.6, 1.9, 4.6, 6.0
SLEEP_AT = SC['sleep']['at']
# the cuts inside scenes, on the beat (scene time)
def b(n): return n * BEAT
# the website section: what the monitor shows
SITE_CLIPS = [('s03', 0.0), ('s04', b(5)), ('s09', b(11))]

# ---- banana-ui: its own frames, headless ------------------------------------------------------------------------------
# a sequence: frames from video time `at` to `until`, 30 a second, with presses at video times; pre: commands before
# the first frame; fx: the test library's variant
def seq(at, until, events, pre='home 0; wait 2400', fx='', env=None, mounted=False):
    return dict(at=at, until=until, events=events, pre=pre, fx=fx, env=env or {}, mounted=mounted)
def A(id, n): return SC[id]['at'] + b(n)	# video time: n beats into a scene
SEQS = {
    'boot': None,	# the boot animation (splash), drawn on its own below
    'menu': seq(6.0, SC['play']['at'] + b(6), [(A('menu', 1), 'right'), (A('menu', 3), 'right'), (A('menu', 5), 'right'), (A('menu', 7), 'left'),
                                               (A('menu', 9), 'left'), (A('menu', 11), 'left'), (A('menu', 13), 'a'), (A('menu', 15), 'right'),
                                               (A('menu', 17), 'right'), (A('menu', 19), 'right'), (A('play', 2), 'a')]),
    'emu': seq(SC['emus']['at'], SC['sd']['at'], [(A('emus', 3), 'right'), (A('emus', 5), 'left'), (A('emus', 8), 'b; press b; press a; press x'),
                                                  (A('emus', 10), 'right'), (A('emus', 12), 'right')],
               pre='home 0; press start; press r; press r; press a; wait 1000'),
    'sdset': seq(A('sd', 5), A('sd', 17), [(A('sd', 11), 'down')], pre='home 0; press start; press r; press r; press a; press down; press down; press down; wait 1000'),
    'bdsset': seq(A('bds', 11), A('bds', 17), [(A('bds', 13), 'right')], fx='bananads',
                  pre='home 0; press start; press r; press r; press a; ' + 'press down; ' * 9 + 'wait 1000'),
    'systems': seq(SC['beyond']['at'], SC['home']['at'], [(A('beyond', 3 * k), 'right') for k in range(1, 8)]),
    'wifi': seq(SC['home']['at'], A('home', 7), [], fx='wifi',
                pre='home 0; press start; press r; press r; press r; press r; press a; press down; press a; wait 2500'),
    'libb': seq(A('home', 7), A('home', 12), [], pre='lib 0 0; wait 1200'),
    'liba': seq(A('home', 12), SC['fresh']['at'], [(A('home', 13), 'right')], fx='new', pre='lib 0 0; wait 1200'),
    'update': seq(SC['fresh']['at'], A('fresh', 12), [(A('fresh', 2), 'down'), (A('fresh', 5), 'a'), (A('fresh', 9), 'a')], mounted=True,
                  pre='home 0; press start; press r; press r; press r; press r; press r; press a; wait 1000'),
}
SPLASH_MS = 2200

def fixture(kind):
    d = f'{FX}-{kind or "plain"}'
    shutil.rmtree(d, True)
    shutil.copytree(FX, d, symlinks=True)
    if kind == 'bananads':
        with open(d + '/data/settings.conf', 'a') as f: f.write('nds_emulator = bananads\n')
    if kind == 'new':	# a game copied over: it shows up in the library
        open(d + '/roms/nds/Lemon Lane.nds', 'wb').write(bytes(4096))
        subprocess.run(['convert', '-size', '420x380', 'gradient:#e0c030-black', '-gravity', 'center', '-fill', 'white', '-pointsize', '34',
                        '-annotate', '0', 'Lemon Lane', d + '/roms/nds/media/box/Lemon Lane.png'], check=True)
    if kind == 'wifi':
        open(d + '/data/wifi.conf', 'w').write('network={\n\tssid="Banana Home"\n\tpsk="bananas!"\n}\n')
    return d

def ui_run(prefix, script, d, env=None, mounted=False):
    cmd = [ROOT + '/ui/banana-ui', '--headless', prefix, '--size', '1024x768', '--assets', ROOT + '/ui/assets/rnds',
           '--splash', ROOT + '/ui/assets/splash', '--banana', ROOT + '/ui/assets/banana',
           '--systems', ROOT + '/system/rootfs/etc/banana/systems.conf', '--roms', d + '/roms', '--data', d + '/data', '--script', script]
    if mounted:
        # Settings > System > Update lists what /usr/libexec/banana/update finds: here a stand-in that lists one
        # package, mounted over /usr/libexec for banana-ui alone (a mount namespace of its own)
        stub = WORK + '/libexec'
        os.makedirs(stub + '/banana', exist_ok=True)
        open(stub + '/banana/update', 'w').write('#!/bin/sh\n[ "$1" = list ] && printf "/data/banana/updates/banana-rgds-plus-0.4.2.update.tar\\t0.4.2\\trgds-plus\\n"\nexit 0\n')
        os.chmod(stub + '/banana/update', 0o755)
        cmd = ['unshare', '-m', 'sh', '-c', 'mount --bind "$0" /usr/libexec && exec "$@"', stub] + cmd
    r = subprocess.run(cmd, capture_output=True, text=True, env={**os.environ, **(env or {})})
    if r.returncode: sys.exit(f'banana-ui failed: {r.stderr[-2000:]}')
    return r.stderr

def to_jpeg(out, n):
    for k in range(n):
        for p in ('top', 'bot'):
            f = f'{out}/f{k:04d}-{p}.png'
            Image.open(f).convert('RGB').save(f[:-4] + '.jpg', quality=92); os.remove(f)

def make_screens(only=None):
    if not os.path.exists(FX + '/roms'):
        subprocess.run([ROOT + '/ui/tests/fixture.sh', FX], check=True, capture_output=True)
        # a few more systems on the shelf (empty stand-in games: the menu shows the systems)
        for sys_, names in (('nes', ['Orchard Bros.nes']), ('megadrive', ['Peel Hedgehog.md']), ('psx', ['Banana Fantasy.cue']),
                            ('n64', ['Smoothie Kart 64.z64'])):
            os.makedirs(f'{FX}/roms/{sys_}', exist_ok=True)
            for n in names: open(f'{FX}/roms/{sys_}/{n}', 'wb').write(bytes(1024))
    for name, S in SEQS.items():
        if only and name not in only: continue
        out = f'{UI}/{name}'; shutil.rmtree(out, True); os.makedirs(out)
        if S is None:	# the boot animation
            n = int(SPLASH_MS / 1000 * FPS)
            cmds = [f'splash f{k:04d} {k * 1000 / FPS:.0f} {1 if k * 1000 / FPS >= 1100 else 0}' for k in range(n)]
            ui_run(out + '/', '; '.join(cmds), FX); to_jpeg(out, n); print(name, n, flush=True); continue
        d = fixture(S['fx'])
        env = dict(S['env'])
        if S['fx'] == 'wifi': env.update(BANANA_WIFI=ROOT + '/ui/tests/fake-wifi.sh', FAKE_WIFI_CONF=d + '/data/wifi.conf')
        cmds = [S['pre']]; t = S['at']; last = t; ev = sorted(S['events']); i = 0; n = 0
        while t < S['until'] + 0.5 / FPS:
            while i < len(ev) and ev[i][0] <= t + 1e-6: cmds.append('press ' + ev[i][1]); i += 1
            w = round(t * 1000) - round(last * 1000); last = t
            if w > 0: cmds.append(f'wait {w}')
            cmds.append(f'shot f{n:04d}'); n += 1; t = S['at'] + n / FPS
        log = ui_run(out + '/', '; '.join(cmds), d, env, S['mounted'])
        open(out + '/log.txt', 'w').write(log)
        to_jpeg(out, n); print(name, n, flush=True)

# ---- the screens' pictures ------------------------------------------------------------------------------------------------
@functools.lru_cache(64)
def ui_frame(name, n):
    files = seq_len(name)
    n = max(0, min(files - 1, n))
    return tuple(Image.open(f'{UI}/{name}/f{n:04d}-{p}.jpg').convert('RGB') for p in ('top', 'bot'))
@functools.lru_cache(None)
def seq_len(name): return len(glob.glob(f'{UI}/{name}/f*-top.jpg'))
@functools.lru_cache(None)
def still(name):
	# the website's screenshots: both 512x384 panels, top over bottom
    im = Image.open(f'{PREVIEW}/assets/screens/{name}.webp').convert('RGB')
    return tuple(im.crop((0, y, 512, y + 384)).resize((1024, 768), Image.BICUBIC) for y in (0, 384))
BLACK = Image.new('RGB', (1024, 768), (0, 0, 0))
def source(spec, t):
    # spec: ('seq', name) plays at video time t; ('still', name); ('frame', name, n)
    if spec[0] == 'seq':
        S = SEQS.get(spec[1]); at = S['at'] if S else spec[2]
        return ui_frame(spec[1], int(round((t - at) * FPS)))
    if spec[0] == 'frame': return ui_frame(spec[1], spec[2])
    return still(spec[1])
def schedule(entries, t, local, fade=0.15):
    # entries: [(scene time, spec)], cross-fading at each change
    k = max([i for i, e in enumerate(entries) if e[0] <= local] or [0])
    cur = source(entries[k][1], t)
    if k > 0 and local - entries[k][0] < fade:
        prev = source(entries[k - 1][1], t)
        a = ease((local - entries[k][0]) / fade)
        cur = tuple(Image.blend(p, c, a) for p, c in zip(prev, cur))
    return cur

# ---- pieces drawn once -----------------------------------------------------------------------------------------------------
@functools.lru_cache(None)
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
WARM, COOL, VIOLET = (255, 200, 60), (90, 110, 170), (150, 110, 220)
GLOW = dict(beyond=VIOLET, site=COOL)

# the two screens flat: rounded, a thin edge, a soft shadow (video.py's)
SW, SH, GAP = 576, 432, 28
@functools.lru_cache(None)
def panels_frame():
    w, h = SW + 12, 2 * (SH + 12) + GAP
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    boxes = [(0, 0, w - 1, SH + 11), (0, SH + 12 + GAP, w - 1, h - 1)]
    for box in boxes: d.rounded_rectangle(box, 20, fill=(14, 14, 17), outline=(44, 45, 52), width=2)
    sh = Image.new('RGBA', (w + 160, h + 160), (0, 0, 0, 0))
    sd = ImageDraw.Draw(sh)
    for box in boxes: sd.rounded_rectangle((box[0] + 80, box[1] + 100, box[2] + 80, box[3] + 100), 24, fill=(0, 0, 0, 160))
    mask = Image.new('L', (SW, SH), 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, SW - 1, SH - 1), 14, fill=255)
    return im, sh.filter(ImageFilter.GaussianBlur(28)), boxes, mask
PX = W - (SW + 12) - 200
PY = (H - (2 * (SH + 12) + GAP)) // 2

def panels(stage, screens, alpha=1.0, scale=1.0, dx=0):
    im, sh, boxes, mask = panels_frame()
    dev = im.copy()
    for (top, box) in zip(screens, boxes):
        dev.paste(top.resize((SW, SH), Image.LANCZOS), (box[0] + 6, box[1] + 6), mask)
    x, y = PX + dx, PY
    if scale != 1:
        cx, cy = x + dev.width / 2, y + dev.height / 2
        dev = dev.resize((int(dev.width * scale), int(dev.height * scale)), Image.LANCZOS)
        sh = sh.resize((int(sh.width * scale), int(sh.height * scale)), Image.BILINEAR)
        x, y = cx - dev.width / 2, cy - dev.height / 2
    over(stage, sh, x - 80 * scale, y - 80 * scale, alpha)
    over(stage, dev, x, y, alpha)

def over(layer, im, x, y, alpha=1.0):
    # an RGBA picture over the layer, faded
    if alpha <= 0: return
    if alpha < 1: im = im.copy(); im.putalpha(im.getchannel('A').point(lambda v: int(v * alpha)))
    x, y = int(round(x)), int(round(y))
    l, t = max(0, -x), max(0, -y)
    r, bt = min(im.width, W - x), min(im.height, H - y)
    if r <= l or bt <= t: return
    layer.alpha_composite(im.crop((l, t, r, bt)) if (l, t, r, bt) != (0, 0, im.width, im.height) else im, (x + l, y + t))

# ---- the handheld in 3D ------------------------------------------------------------------------------------------------------
# a pose: the handheld turned by yaw, lid open by `lid` degrees, the camera `d` from the point it looks at, along
# `dir`; shift moves the picture sideways (a share of its width)
def pose(d=0.47, yaw=-0.35, lid=150, tgt=(0, 0.03, -0.035), dir=(0, 0.62, 0.8), shift=0.2, fov=30, press=None, pitch=0.0):
    n = math.sqrt(sum(c * c for c in dir))
    cam = [tgt[i] + d * dir[i] / n for i in range(3)]
    r = lambda v: round(v, 5)
    return dict(yaw=r(yaw), pitch=r(pitch), lid=r(lid), cam=[r(c) for c in cam], at=[r(c) for c in tgt], fov=fov, shift=r(shift),
                press={k: r(v) for k, v in (press or {}).items() if v > 0.001})
def open_pose(t):	# the opening: closed, pushed in on, the lid opening
    u = easeio(t / 6.0)
    lid = 0 if t < 2.3 else 150 * backout((t - 2.3) / 1.7, 1.1)
    return pose(d=lerp(0.62, 0.48, u), yaw=lerp(-0.85, -0.38, u), lid=lid)
def sleep_pose(t):
    if t < LID_CLOSE: lid = 150
    elif t < LID_SHUT: lid = 150 * (1 - easeio((t - LID_CLOSE) / (LID_SHUT - LID_CLOSE)))
    elif t < LID_OPEN: lid = 0
    else: lid = 150 * easeio((t - LID_OPEN) / (LID_UP - LID_OPEN))
    return pose(d=0.47, yaw=-0.3, lid=lid)
SMALL_CUTS = [0, b(4), b(8), b(12), b(15)]	# hero, the screens, the controls, the hinge, the menu
def small_pose(t):
    k = max(i for i, c in enumerate(SMALL_CUTS) if c <= t + 1e-9)
    u = (t - SMALL_CUTS[k]) / ((SMALL_CUTS[k + 1] if k + 1 < len(SMALL_CUTS) else SC['small']['len']) - SMALL_CUTS[k])
    if k == 0: return pose(d=lerp(0.5, 0.47, u), yaw=lerp(-0.55, -0.4, u))
    if k == 1: return pose(d=lerp(0.165, 0.145, u), yaw=0, tgt=(0, 0.046, -0.082), dir=(lerp(-0.12, 0.05, u), 0.62, 0.8), shift=0.17)
    if k == 2:
        press = max(0.0, math.sin(math.pi * (u - 0.45) / 0.12)) if 0.45 < u < 0.57 else 0
        return pose(d=lerp(0.125, 0.11, u), yaw=0, tgt=(0.07, 0.014, -0.012), dir=(0.35, 0.75, 0.6), shift=0.15, press={'Btn_A': press})
    if k == 3: return pose(d=lerp(0.15, 0.135, u), yaw=0, tgt=(0.055, 0.03, -0.045), dir=(0.9, 0.45, lerp(0.55, 0.4, u)), shift=0.17)
    return pose(d=0.52, yaw=-0.25)
def end_pose(t):
    u = easeio(t / SC['end']['len'])
    return pose(d=lerp(0.52, 0.47, u), yaw=lerp(-0.6, -0.3, u))
# shots: (picture size, frames a second (None: one pose), the pose at a time of the scene)
SHOTS = {
    'open': ((1280, 720), 15, SC['open']['len'], open_pose),
    'meet': ((1920, 1080), None, 0, lambda t: open_pose(6.0)),
    'play': ((1920, 1080), None, 0, lambda t: pose(d=0.46, yaw=-0.28)),
    'sleep': ((1280, 720), 15, SC['sleep']['len'], sleep_pose),
    'beyond': ((1920, 1080), None, 0, lambda t: pose(d=0.47, yaw=0.32)),
    'small': ((1280, 720), 10, SC['small']['len'], small_pose),
    'site': ((1280, 720), None, 0, lambda t: pose(d=0.5, yaw=0.45, shift=0)),
    'end': ((1280, 720), 15, SC['end']['len'], end_pose),
}
def pose_key(size, p): return hashlib.sha1(json.dumps([size, p], sort_keys=True).encode()).hexdigest()[:16]
@functools.lru_cache(None)
def shot_keys(name):
    size, fps, length, fn = SHOTS[name]
    n = int(length * fps) + 2 if fps else 1
    return [pose_key(size, fn(k / fps if fps else 0)) for k in range(n)]
def all_poses():
    jobs = {}
    for name, (size, fps, length, fn) in SHOTS.items():
        n = int(length * fps) + 2 if fps else 1
        for k in range(n):
            p = fn(k / fps if fps else 0)
            jobs.setdefault(size, {})[pose_key(size, p)] = p
    return jobs

def render_3d(only=None):
    os.makedirs(R3D, exist_ok=True)
    www = WORK + '/www'; os.makedirs(www, exist_ok=True)
    for name, src in (('three', NODE + '/node_modules/three'), ('rgds_plus.min.glb', PREVIEW + '/rgds_plus.min.glb'), ('distro3d.html', HERE + '/distro3d.html')):
        if not os.path.lexists(f'{www}/{name}'): os.symlink(src, f'{www}/{name}')
    shutil.copy(HERE + '/distro3d.mjs', NODE + '/distro3d.mjs')	# beside its node_modules
    server = subprocess.Popen([sys.executable, '-m', 'http.server', str(PORT), '--bind', '127.0.0.1'], cwd=www, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        procs = []
        for size, poses in all_poses().items():
            todo = [(f'{R3D}/{k}', p) for k, p in poses.items() if not os.path.exists(f'{R3D}/{k}.json')]
            if only: todo = [j for j in todo if any(os.path.basename(j[0]) in shot_keys(s) for s in only)]
            print(size, len(todo), 'poses to render', flush=True)
            parts = [todo[i::JOBS] for i in range(JOBS)]
            for i, part in enumerate(parts):
                if not part: continue
                jf = f'{R3D}/jobs-{size[0]}-{i}.json'; json.dump(part, open(jf, 'w'))
                procs.append(subprocess.Popen(['node', 'distro3d.mjs', str(PORT), str(size[0]), str(size[1]), jf], cwd=NODE))
            for p in procs: p.wait()
            procs = []
    finally:
        server.terminate()

@functools.lru_cache(24)
def load3d(key):
    p = f'{R3D}/{key}'
    if not os.path.exists(p + '.json'): return None
    return Image.open(p + '.png').convert('RGBA'), Image.open(p + '-mask.png').convert('RGB'), json.load(open(p + '.json'))

def coeffs(dst, src):
    A, B = [], []
    for (x, y), (u, v) in zip(dst, src):
        A.append([x, y, 1, 0, 0, 0, -u * x, -u * y]); B.append(u)
        A.append([0, 0, 0, x, y, 1, -v * x, -v * y]); B.append(v)
    return np.linalg.solve(np.array(A, float), np.array(B, float)).tolist()

def device(stage, shot, local, screens, bright=1.0, scale=1.0, origin=(0, 0), alpha=1.0, reveal=None):
    # the handheld at a time of its shot, BananaOS's screens laid onto its two screens
    size, fps, length, fn = SHOTS[shot]
    keys = shot_keys(shot)
    x = clamp01(local / length) * length * fps if fps else 0
    i0 = min(len(keys) - 1, int(x)); i1 = min(len(keys) - 1, i0 + 1); f = x - i0
    a = load3d(keys[i0])
    if a is None: return placeholder(stage, alpha)
    img, mask, c = a
    moving_lid = fps and fn(i0 / fps)['lid'] != fn(i1 / fps)['lid']
    if moving_lid and f >= 0.5:	# the lid moving: the nearer picture (a blend of two would show two lids)
        a = load3d(keys[i1]) or a; img, mask, c = a
    if i1 != i0 and keys[i1] != keys[i0] and f > 0.02 and not moving_lid:
        bb = load3d(keys[i1])
        if bb:
            img = Image.blend(img, bb[0], f); mask = Image.blend(mask, bb[1], f)
            c = {s: [[lerp(p[j], q[j], f) for j in range(4)] for p, q in zip(c[s], bb[2][s])] for s in c}
    k = W / size[0] * scale
    if abs(k - 1) > 1e-3:
        img = img.resize((round(size[0] * k), round(size[1] * k)), Image.BICUBIC)
        mask = mask.resize(img.size, Image.BILINEAR)
    ox, oy = origin
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    layer.alpha_composite(img, (int(ox), int(oy))) if ox >= 0 and oy >= 0 and img.width + ox <= W and img.height + oy <= H else over(layer, img, ox, oy)
    for side, ch, scr in (('top', 0, screens[0]), ('bot', 1, screens[1])):
        pts = [(p[0] * k + ox, p[1] * k + oy) for p in c[side]]
        src = [(p[2] * 1024, p[3] * 768) for p in c[side]]
        x0 = max(0, int(min(p[0] for p in pts)) - 2); y0 = max(0, int(min(p[1] for p in pts)) - 2)
        x1 = min(W, int(max(p[0] for p in pts)) + 3); y1 = min(H, int(max(p[1] for p in pts)) + 3)
        if x1 <= x0 or y1 <= y0: continue
        m = mask.getchannel(ch).crop((x0 - int(ox), y0 - int(oy), x1 - int(ox), y1 - int(oy)))
        if not m.getbbox(): continue
        if bright < 1: scr = Image.blend(BLACK, scr, max(0.0, bright))
        warp = scr.transform((x1 - x0, y1 - y0), Image.PERSPECTIVE, coeffs([(p[0] - x0, p[1] - y0) for p in pts], src), Image.BICUBIC)
        layer.paste(warp, (x0, y0), m)
    if reveal is not None:
        layer.putalpha(Image.fromarray((np.asarray(layer.getchannel('A'), np.float32) * reveal).astype(np.uint8)))
    if alpha < 1: layer.putalpha(layer.getchannel('A').point(lambda v: int(v * alpha)))
    stage.alpha_composite(layer)

def placeholder(stage, alpha):
    # (before the 3D is rendered: an outline where the handheld goes)
    d = ImageDraw.Draw(stage)
    d.rounded_rectangle((900, 200, 1800, 900), 30, outline=(80, 80, 90, int(255 * alpha)), width=3)

# ---- graphics -------------------------------------------------------------------------------------------------------------
@functools.lru_cache(None)
def icon(name, k=3):
    return grow(Image.open(f'{ASSETS}rnds/icons/{name}.png').convert('RGBA'), k)
@functools.lru_cache(None)
def file_pill(text, ic):
    f = F(MED, 30); ico = icon(ic, 2)
    w = int(f.getlength(text)) + ico.width + 64; h = max(ico.height, 40) + 28
    im = Image.new('RGBA', (w + 80, h + 80), (0, 0, 0, 0))
    sd = ImageDraw.Draw(im); sd.rounded_rectangle((40, 54, w + 40, h + 54), 18, fill=(0, 0, 0, 150))
    im = im.filter(ImageFilter.GaussianBlur(16))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((40, 40, w + 40, h + 40), 18, fill=(22, 23, 28, 255), outline=(60, 62, 72, 255), width=2)
    im.alpha_composite(ico, (40 + 22, 40 + (h - ico.height) // 2))
    d.text((40 + 22 + ico.width + 18, 40 + (h - 30) // 2 - 4), text, font=f, fill=INK + (255,))
    return im
def fly(stage, pill, u, start, end, lift=160):
    # a file flying from start to end along an arc, smaller as it lands, gone at the end
    if u <= 0 or u >= 1: return
    e = easeio(u)
    x = lerp(start[0], end[0], e); y = lerp(start[1], end[1], e) - lift * math.sin(math.pi * e)
    s = lerp(1.0, 0.55, e ** 2)
    im = pill.resize((int(pill.width * s), int(pill.height * s)), Image.LANCZOS) if s < 0.999 else pill
    a = clamp01(u / 0.12) * clamp01((1 - u) / 0.15)
    over(stage, im, x - im.width / 2, y - im.height / 2, a)
def wifi_arcs(d, cx, cy, a, t):
    for i in range(3):
        r = 26 + i * 24
        k = clamp01(a * (0.5 + 0.5 * math.sin(t * 5 - i * 0.9)))
        d.arc((cx - r, cy - r, cx + r, cy + r), 225, 315, fill=YELLOW + (int(255 * k),), width=8)
    d.ellipse((cx - 7, cy - 7, cx + 7, cy + 7), fill=YELLOW + (int(255 * a),))

def slots(stage, local, alpha):
    # the two system slots: the update written to the other one (filling), which becomes the new version; the
    # previous one stays as it was
    if alpha <= 0: return
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
    w, h = SW + 12, 300
    x = PX; ys = [PY + 70, PY + 70 + h + 56]
    fill = easeio((local - 0.3) / 1.0)
    for i, y in enumerate(ys):
        new = i == 1
        on = new and fill >= 1
        d.rounded_rectangle((x + 8, y + 18, x + w + 8, y + h + 18), 26, fill=(0, 0, 0, 120))
        d.rounded_rectangle((x, y, x + w, y + h), 26, fill=(18, 19, 24, 255), outline=(YELLOW if on else (52, 54, 64)) + (255,), width=4 if on else 2)
        d.text((x + 40, y + 36), 'SLOT ' + 'AB'[i], font=F(MED, 26), fill=SOFT + (255,))
        label = ('New version' if fill >= 1 else 'Writing the update') if new else 'Previous version'
        d.text((x + 40, y + 82), label, font=F(MED, 52), fill=(YELLOW if new else INK) + (255,))
        d.text((x + 40, y + 160), 'Running from the next restart' if new else 'Stays available', font=F(REG, 30), fill=SOFT + (255,))
        if new:
            bw = w - 80
            d.rounded_rectangle((x + 40, y + 226, x + 40 + bw, y + 250), 12, fill=(34, 35, 42, 255))
            if fill > 0: d.rounded_rectangle((x + 40, y + 226, x + 40 + max(24, int(bw * fill)), y + 250), 12, fill=YELLOW + (255,))
        else:
            d.rounded_rectangle((x + 40, y + 226, x + w - 40, y + 250), 12, fill=(70, 72, 84, 255))
    if fill >= 1:
        a = ease((local - 1.5) / 0.4)
        over(layer, BANANA, x + w - BANANA.width - 34, ys[1] + 30 + 10 * (1 - a), a)
    if alpha < 1: layer.putalpha(layer.getchannel('A').point(lambda v: int(v * alpha)))
    stage.alpha_composite(layer)

# the monitor for the website: a dark frame, a thin edge, a short stand; the page in it
MW, MH = 1000, 625
MX, MY = W - MW - 24 - 110, 150
@functools.lru_cache(None)
def monitor_frame():
    w, h = MW + 28, MH + 28
    im = Image.new('RGBA', (w + 200, h + 260), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((100 + 30, 100 + 40, 100 + w + 30, 100 + h + 60), 24, fill=(0, 0, 0, 170))
    im = im.filter(ImageFilter.GaussianBlur(30))
    d = ImageDraw.Draw(im)
    cx = 100 + w // 2
    d.rectangle((cx - 30, 100 + h, cx + 30, 100 + h + 80), fill=(24, 24, 28, 255))
    d.rounded_rectangle((cx - 170, 100 + h + 76, cx + 170, 100 + h + 92), 8, fill=(30, 30, 35, 255), outline=(48, 49, 57, 255), width=2)
    d.rounded_rectangle((100, 100, 100 + w - 1, 100 + h - 1), 20, fill=(14, 14, 17, 255), outline=(48, 49, 57, 255), width=2)
    mask = Image.new('L', (MW, MH), 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, MW - 1, MH - 1), 8, fill=255)
    return im, mask
@functools.lru_cache(16)
def capture(name, i):
    files = sorted(glob.glob(f'{SITE}/{name}/f*.jpg'))
    if not files: return None
    return Image.open(files[max(0, min(len(files) - 1, i))]).convert('RGB').resize((MW, MH), Image.LANCZOS)
def site_page(local):
    k = max(i for i, c in enumerate(SITE_CLIPS) if c[1] <= local)
    name, t0 = SITE_CLIPS[k]
    x = (local - t0) * SITE_FPS
    a = capture(name, int(x))
    if a is None:
        alt = sorted(os.path.basename(p) for p in glob.glob(f'{SITE}/s*'))	# a section not captured: the nearest one that is
        if not alt: return Image.new('RGB', (MW, MH), (20, 20, 24))
        name = min(alt, key=lambda s: abs(int(s[1:]) - int(name[1:]))); a = capture(name, int(x))
    b2 = capture(name, int(x) + 1)
    f = x - int(x)
    return a if f < 0.05 or b2 is None else Image.blend(a, b2, f)

# ---- words --------------------------------------------------------------------------------------------------------------
L, COLW = 120, 740
DEVICE_SCENES = ('open', 'meet', 'sleep', 'beyond', 'small', 'end')
KIND = dict(head=(MED, 62, INK), sub=(MED, 42, YELLOW), small=(REG, 30, SOFT))
def wrap(s, font, width):
    out, line = [], ''
    for w in s.split(' '):
        t = (line + ' ' + w).strip()
        if font.getlength(t) > width and line: out.append(line); line = w
        else: line = t
    return out + [line]
def text(d, xy, s, font, fill, alpha):
    if alpha > 0: d.text(xy, s, font=font, fill=fill + (int(255 * alpha),))
def tag_pill(d, x, y, s, a):
    if a <= 0: return
    f = F(MED, 22); tw = f.getlength(s)
    d.rounded_rectangle((x, y, x + tw + 34, y + 40), 20, fill=YELLOW + (int(255 * a),))
    text(d, (x + 17, y + 7), s, f, (20, 20, 24), a)
def label_pill(d, x, y, s, a):
    if a <= 0: return
    f = F(MED, 34); tw = f.getlength(s)
    d.rounded_rectangle((x, y, x + tw + 44, y + 58), 29, fill=(20, 20, 26, int(230 * a)), outline=YELLOW + (int(255 * a),), width=3)
    text(d, (x + 22, y + 9), s, f, INK, a)
EMU_LOGOS = None
def emu_logos():
    global EMU_LOGOS
    if EMU_LOGOS is None:
        EMU_LOGOS = []
        for f, k in (('superdrastic-logo.png', 0.75), ('bananads-logo.png', 0.64)):
            p = f'{PREVIEW}/assets/{f}'
            if os.path.exists(p):
                im = Image.open(p).convert('RGBA'); EMU_LOGOS.append(im.resize((int(im.width * k), int(im.height * k)), Image.NEAREST))
    return EMU_LOGOS

def pixel_in(im, k, p, seed=7):
    # a pixel logo coming in pixel by pixel (its own pixels, grown by k): p from 0 to 1
    a = np.asarray(im.getchannel('A'), np.float32)
    rnd = np.random.RandomState(seed).rand(*a.shape)
    keep = (rnd < p * 1.15 - 0.15 * np.linspace(0, 1, a.shape[1])[None, :]).astype(np.float32)
    out = im.copy(); out.putalpha(Image.fromarray((a * keep).astype(np.uint8)))
    return grow(out, k)

def words(layer, S, local):
    d = ImageDraw.Draw(layer, 'RGBA')
    def at(t0): return ease((local - t0) / 0.6)
    def rise(t0): return 16 * (1 - ease((local - t0) / 0.6))
    sid = S['id']
    if sid == 'meet':
        lg = pixel_in(LOGO, 3, clamp01((local - 0.05) / 0.55))
        y0 = 340
        over(layer, lg, L, y0 - 14 * (1 - backout((local - 0.05) / 0.55)), 1)
        f = F(MED, 66); s = 'Meet BananaOS.'
        text(d, (L, y0 + 150 + rise(0.7)), s, f, INK, at(0.7))
        over(layer, BANANA, L + f.getlength(s) + 22, y0 + 128 + rise(1.0) - 10 * math.sin(math.pi * clamp01((local - 1.0) / 0.35)), at(1.0))
        text(d, (L, y0 + 250 + rise(1.4)), 'Built for RG DS + RG DS Plus.', F(MED, 42), YELLOW, at(1.4))
        return
    if sid == 'end':
        y0 = 300
        lg = pixel_in(LOGO, 3, clamp01((local - 0.1) / 0.6), seed=3)
        over(layer, lg, L, y0, 1)
        pop = beat(241) - SC['end']['at']	# on the last beat: a tiny banana pops in
        if local >= pop:
            u = clamp01((local - pop) / 0.3)
            s = backout(u, 2.2)
            bn = BANANA48.resize((max(1, int(48 * s)), max(1, int(48 * s))), Image.NEAREST)
            over(layer, bn, L + LOGO.width * 3 + 26 + 24 - bn.width / 2, y0 + 48 - bn.height / 2, clamp01(u * 3))
        y = y0 + 150
        for s_, f, col, t0 in (('Two screens. Straight to play.', F(MED, 50), INK, 0.7), ('Try the alpha.', F(MED, 42), YELLOW, 1.4),
                               ('Link in the description.', F(MED, 42), YELLOW, 1.4)):
            text(d, (L, y + rise(t0)), s_, f, col, at(t0)); y += f.size + 22
        text(d, (L, y + 26 + rise(2.2)), 'Alpha software · Check known issues before installing.', F(REG, 26), SOFT, at(2.2))
        return
    colw = {'site': 470}.get(sid, COLW if sid in DEVICE_SCENES else 880)
    lines = []
    for k, (s, kind, t0) in enumerate(S['lines']):
        fn, size, col = KIND[kind]
        for part in wrap(s, F(fn, size), colw): lines.append((part, F(fn, size), col, t0, kind))
    gap = lambda f, kind: f.size + (26 if kind != 'small' else 14)
    hgt = (64 if S.get('tag') else 0) + sum(gap(f, k) for _, f, _, _, k in lines)
    if S.get('logos'): hgt += 200
    if sid == 'beyond': hgt += 110
    y = S.get('yc', H / 2) - hgt / 2
    if S.get('tag'): tag_pill(d, L, y + rise(0.12), S['tag'], at(0.12)); y += 64
    prev = None
    for part, f, col, t0, kind in lines:
        if kind == 'small' and prev != 'small': y += 10
        text(d, (L, y + rise(t0)), part, f, col, at(t0))
        y += gap(f, kind); prev = kind
    if S.get('logos'):
        y += 26
        for k, lg in enumerate(emu_logos()):
            t0 = S['logos'] + 0.35 * k
            over(layer, lg, L, y + rise(t0), at(t0)); y += lg.height + 22
    if sid == 'beyond':
        # each system's name with its clip
        names = ['Game Boy Advance', 'Game Boy', 'NES', 'SNES', 'Mega Drive', 'PlayStation', 'Nintendo 64']
        y += 30
        for k, nm in enumerate(names):
            t0 = b(3 * (k + 1)); t1 = b(3 * (k + 2)) if k < 6 else 1e9
            a = ease((local - t0) / 0.25) * (1 - ease((local - (t1 - 0.2)) / 0.2))
            label_pill(d, L, y, nm, a)

# ---- the pictures of each scene --------------------------------------------------------------------------------------------
HOME = ('frame', 'menu', 0)
def stage(S, local, t):
    st = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sid = S['id']
    if sid == 'open':
        # a thin yellow light across the dark, then the handheld out of it; its screens come on with the boot
        yc, cx = 560, 1340
        sig = 2 + 1400 * easeio((local - 1.0) / 2.6) ** 2
        rows = np.exp(-((np.arange(H) - yc) / sig) ** 2)[:, None].astype(np.float32)
        rows = np.minimum(1, rows * (0.25 + 0.75 * clamp01((local - 0.9) / 0.4)))
        bright = clamp01((local - 4.0) / 0.35)
        scr = source(('frame', 'boot', int((local - 4.0) * FPS)), t) if local > 4 else (BLACK, BLACK)
        device(st, 'open', local, scr, bright, reveal=rows)
        light(st, local, cx, yc)
    elif sid == 'meet':
        s = 1 + 0.03 * easeio(local / S['len'])
        cx, cy = 1340, 560
        device(st, 'meet', local, source(('seq', 'menu'), t), scale=s, origin=(cx - cx * s, cy - cy * s))
    elif sid == 'menu':
        a = ease(local / 0.5)
        panels(st, source(('seq', 'menu'), t), a, scale=lerp(0.9, 1.0, a))
    elif sid == 'play':
        cut = b(5)
        if local < cut + 0.25: panels(st, source(('seq', 'menu'), t), 1 - ease((local - cut) / 0.25))
        if local > cut: device(st, 'play', local, still('shot-game'), alpha=ease((local - cut) / 0.25))
    elif sid == 'emus':
        panels(st, source(('seq', 'emu'), t))
    elif sid == 'sd':
        panels(st, schedule([(0, ('still', 'shot-game')), (b(5), ('seq', 'sdset')), (b(17), ('still', 'shot-game-menu'))], t, local))
    elif sid == 'bds':
        panels(st, schedule([(0, ('still', 'shot-game')), (b(5), ('still', 'shot-library')), (b(11), ('seq', 'bdsset')), (b(17), ('still', 'shot-game'))], t, local))
    elif sid == 'sleep':
        # the screens go dark as the lid shuts, and the game is back once it is open
        if local < LID_SHUT: bright = 1 - clamp01((local - LID_CLOSE - 0.3) / 0.7)
        else: bright = clamp01((local - (LID_UP - 0.5)) / 0.4)
        device(st, 'sleep', local, still('shot-game'), bright)
    elif sid == 'beyond':
        device(st, 'beyond', local, source(('seq', 'systems'), t))
    elif sid == 'home':
        c1, c2 = b(7), b(12)
        panels(st, schedule([(0, ('seq', 'wifi')), (c1, ('seq', 'libb')), (c2, ('seq', 'liba'))], t, local))
        # a game copied over Wi-Fi: it flies to the handheld, then it's in the library
        d = ImageDraw.Draw(st)
        wa = ease((local - (c1 - 0.6)) / 0.4) * (1 - ease((local - (c2 + 0.1)) / 0.4))
        if wa > 0: wifi_arcs(d, PX - 120, PY + 640, wa, local)
        fly(st, file_pill('Lemon Lane.nds', 'nds'), (local - c1 - 0.1) / (c2 - c1 - 0.1), (PX - 120, PY + 760), (PX + SW / 2, PY + SH + GAP + SH / 2))
        if c2 <= local < c2 + 0.35: flash(st, PX, PY + SH + 12 + GAP, (1 - (local - c2) / 0.35) * 0.35)
    elif sid == 'fresh':
        c = b(12)
        if local < c + 0.4: panels(st, source(('seq', 'update'), t), 1 - ease((local - c) / 0.4))
        fly(st, file_pill('banana-rgds-plus-0.4.2.update.tar', 'gear'), (local - 0.3) / 1.5, (PX - 180, PY + 860), (PX + SW / 2, PY + SH + GAP + SH / 2), lift=120)
        slots(st, local - c, ease((local - c) / 0.4))
    elif sid == 'small':
        # the close-ups fill the frame: their left side fades into the background, under the words
        k = max(i for i, c in enumerate(SMALL_CUTS) if c <= local + 1e-9)
        ramp = np.clip((np.arange(W, dtype=np.float32)[None, :] - 700) / 380, 0, 1) if 1 <= k <= 3 else None
        device(st, 'small', local, source(('seq', 'menu'), SC['menu']['at'] + local), reveal=ramp)
    elif sid == 'site':
        im, mask = monitor_frame()
        mon = im.copy()
        mon.paste(site_page(local), (100 + 14, 100 + 14), mask)
        a = ease(local / 0.5)
        over(st, mon, MX - 100 + 60 * (1 - a), MY - 100, a)
        device(st, 'site', local, source(HOME, t), scale=0.62, origin=(-125 - 40 * (1 - a), 447), alpha=a)
    elif sid == 'end':
        device(st, 'end', local, source(HOME, t))
    return st

@functools.lru_cache(None)
def light_line():
    im = Image.new('RGBA', (1600, 120), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((40, 58, 1560, 61), fill=YELLOW + (255,))
    glow = im.filter(ImageFilter.GaussianBlur(14))
    glow.alpha_composite(im)
    return glow
def light(st, local, cx, yc):
    # the line: drawn out from the middle, then fading as the handheld comes out of the dark
    grow_ = easeio((local - 0.35) / 1.2)
    a = clamp01((local - 0.35) / 0.3) * (1 - easeio((local - 2.4) / 1.2))
    if grow_ <= 0 or a <= 0: return
    ln = light_line()
    w = max(2, int(ln.width * grow_))
    over(st, ln.resize((w, ln.height), Image.BILINEAR), cx - w / 2, yc - ln.height / 2, a)
def flash(st, x, y, a):
    if a <= 0: return
    # a quick white flash over the bottom screen (a change on it)
    _, _, _, mask = panels_frame()
    fl = Image.new('RGBA', (SW, SH), (255, 255, 255, 255)); fl.putalpha(mask.point(lambda v: int(v * a)))
    st.alpha_composite(fl, (int(x) + 6, int(y) + 6))

# ---- a frame ------------------------------------------------------------------------------------------------------------
def scene_at(t):
    return max(k for k, s in enumerate(SCENES) if s['at'] <= t) if t >= 0 else 0
def frame(t):
    i = scene_at(t); S = SCENES[i]; local = t - S['at']
    glow = GLOW.get(S['id'], WARM)
    img = background(glow).copy()
    if i > 0 and local < 0.6:
        pg = GLOW.get(SCENES[i - 1]['id'], WARM)
        if pg != glow: img = Image.blend(background(pg), background(glow), ease(local / 0.6))
    if S['id'] == 'open':	# the glow comes up with the light
        img = Image.blend(Image.new('RGB', (W, H), (0, 0, 0)), img, 0.35 + 0.65 * clamp01((local - 1.0) / 2.5))
    st = stage(S, local, t)
    if i > 0 and local < XFADE and S['id'] not in ('meet',):
        P = SCENES[i - 1]
        prev = stage(P, P['len'] + local, t)
        a = ease(local / XFADE)
        st = Image.blend(prev, st, a)
    img = img.convert('RGBA'); img.alpha_composite(st)
    # the words, on a layer of their own that fades out as one before the next scene
    layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    words(layer, S, local)
    out = 1 - clamp01((local - (S['len'] - TEXT_GAP - TEXT_OUT)) / TEXT_OUT)
    if i == len(SCENES) - 1: out = 1	# the last scene's stay to the fade to black
    if out < 1: layer.putalpha(layer.getchannel('A').point(lambda v: int(v * out)))
    if out > 0: img.alpha_composite(layer)
    img = img.convert('RGB')
    fade = clamp01(t / 0.5) * clamp01((DURATION - t) / 0.8)
    if fade < 1: img = Image.blend(Image.new('RGB', (W, H), (0, 0, 0)), img, fade)
    return img

def frame_bytes(k): return frame(k / FPS).tobytes()

def prepare():
    if not os.path.exists(PREVIEW + '/rgds_plus.min.glb'):
        os.makedirs(WORK + '/site', exist_ok=True)
        a = subprocess.run(['git', '-C', ROOT, 'archive', 'origin/main', 'web/preview'], capture_output=True, check=True).stdout
        subprocess.run(['tar', '-x', '-C', WORK + '/site'], input=a, check=True)
    if not os.path.exists(NODE + '/node_modules/three'):
        os.makedirs(NODE, exist_ok=True)
        subprocess.run(['npm', 'install', '--no-audit', '--no-fund', '--prefix', NODE, 'three@0.170.0', 'playwright-core@1.48.2'], check=True)

def render(out):
    from multiprocessing import Pool
    n = int(round(DURATION * FPS))
    cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{W}x{H}', '-r', str(FPS), '-i', '-']
    if MUSIC:
        # the track from MUSIC_AT, faded in and out; muffled while the lid is shut (a low-passed copy crossfaded in)
        t1, t2 = SLEEP_AT + LID_CLOSE + 0.2, SLEEP_AT + LID_UP - 0.4
        w = f"min(1,max(0,(t-{t1:.2f})/0.6))*min(1,max(0,({t2:.2f}-t)/0.6))"
        graph = (f"[1:a]afade=t=in:st=0:d=2.5,afade=t=out:st={DURATION - 2.5:.2f}:d=2.5,asplit[a][b];"
                 f"[b]lowpass=f=450,lowpass=f=450,volume='0.85*{w}':eval=frame[bl];[a]volume='1-{w}':eval=frame[ad];"
                 f"[ad][bl]amix=inputs=2:normalize=0[out]")
        cmd += ['-ss', str(MUSIC_AT), '-i', MUSIC, '-filter_complex', graph, '-map', '0:v', '-map', '[out]',
                '-c:a', 'aac', '-b:a', '192k', '-ar', '44100', '-ac', '2', '-t', f'{DURATION:.3f}']
    cmd += ['-c:v', 'libx264', '-preset', 'medium', '-pix_fmt', 'yuv420p', '-crf', '18', '-movflags', '+faststart', out]
    ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    with Pool(JOBS) as pool:
        for k, buf in enumerate(pool.imap(frame_bytes, range(n), chunksize=4)):
            ff.stdin.write(buf)
            if k % 300 == 0: print(f'{k}/{n}', flush=True)
    ff.stdin.close()
    if ff.wait(): sys.exit('ffmpeg failed')
    print(out, n, 'frames')

if __name__ == '__main__':
    what = sys.argv[1] if len(sys.argv) > 1 else 'render'
    if what == 'prepare': prepare()
    elif what == 'screens': make_screens(sys.argv[2:])
    elif what == '3d': render_3d(sys.argv[2:])
    elif what == 'preview':
        os.makedirs(WORK + '/preview', exist_ok=True)
        for a in sys.argv[2:]:
            frame(float(a)).save(f'{WORK}/preview/{a}.jpg', quality=88); print(f'{WORK}/preview/{a}.jpg')
    elif what == 'times':
        for s in SCENES: print(f"{s['id']:8s} {s['at']:7.3f} {s['len']:6.3f}")
    else: render(sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, 'bananaos-distro.mp4'))
