#!/usr/bin/env python3
# Plaza themes (light and dark): everything but the layout, made here rather than by hand.
#   icons/<id>.png     a system's or a menu tile's icon: its colour as a rounded square with a soft finish (a gradient,
#                      lighter at the top), a white shape on it. Systems: RetroArch's monochrome shapes (ra/, from
#                      fetch-ra.py: libretro/retroarch-assets, CC BY 4.0); our own menu tiles: the shapes below
#   <variant>-top.jpg  the top screen: soft stripes on a gradient; <variant>-bottom.jpg: dots; and both blurred
#                      (-frost), for Settings' panels
#   sounds/*.wav       the menu's sounds, ours: tick, pop, back, star, chime (a game starting)
#   plaza-dark.json    plaza-light.json with the dark palette: change plaza-light.json, then run this
#
#   python3 tools/plaza-theme/make.py      (needs Pillow and resvg-py: pip install pillow resvg-py)
import io, json, math, os, re, struct, wave
from PIL import Image, ImageDraw, ImageFilter
import resvg_py

HERE = os.path.dirname(os.path.abspath(__file__))
THEMES = os.path.join(HERE, '../../system/rootfs/usr/share/banana/themes')
OUT = os.path.join(THEMES, 'plaza')
W, H = 1024, 768                # the RG DS Plus's screens (the RG DS scales them down)
K = W / 640                     # pixels a unit
N = 192                         # an icon's size

# a system's colour on its tile
COLOURS = {
    'nds': '#3d9e9a', 'gba': '#8b6ad8', 'gbc': '#2eae62', 'gb': '#8a9144', 'nes': '#e25a5a', 'fds': '#e0705a',
    'snes': '#a07ad4', 'n64': '#3fae5a', 'virtualboy': '#d84a4a', 'pokemini': '#d9a431', 'gameandwatch': '#c9a227',
    'megadrive': '#2eabc8', 'mastersystem': '#3b7fd0', 'gamegear': '#5a66c8', 'sg-1000': '#6b7a8f',
    'megacd': '#2a8fb0', 'sega32x': '#c8503c', 'psx': '#5c86e0', 'psp': '#4a5a78', 'c64': '#8a7a54',
    'zxspectrum': '#d0603a', 'ngp': '#5a6e8c', 'ngpc': '#e06a8a', 'neogeo': '#c8a03a', 'wonderswan': '#7a8a9a',
    'wonderswancolor': '#4aa0c8', 'atarilynx': '#c8783a', 'atari2600': '#b8623a', 'atari7800': '#8a5a3a',
    'pcengine': '#e05a6a', 'pcenginecd': '#c84a7a', 'colecovision': '#4a6ab8', 'intellivision': '#b89a4a',
    'vectrex': '#3a8a8a', 'odyssey2': '#6a7ad0', 'arcade': '#e8a020', 'cps1': '#4a8ad8', 'cps2': '#3a6ac8',
    'cps3': '#6a5ad8', 'pico-8': '#e24a6a', 'pad': '#7a8090',
    # our own tiles
    'storeapps': '#2eae62', 'rocknixdsstore': '#2ea0d8', 'activity': '#e8a33d', 'favorites': '#e23d52',
    'recently-played': '#3d8ec4', 'search': '#5c86e0', 'together': '#9b6cff', 'files': '#d9a03f',
    'downloads': '#34b0a0',
}

# our own shapes (64 x 64, white; even-odd cut-outs)
OURS = {
    # Apps: four rounded squares
    'storeapps': '<rect x="10" y="10" width="19" height="19" rx="5"/><rect x="35" y="10" width="19" height="19" rx="5"/>'
                 '<rect x="10" y="35" width="19" height="19" rx="5"/><rect x="35" y="35" width="19" height="19" rx="9.5"/>',
    # Store: a bag with its handle
    'rocknixdsstore': '<path fill-rule="evenodd" d="M14 22h36l-3 30a5 5 0 0 1-5 4.5H22a5 5 0 0 1-5-4.5z"/>'
                      '<path fill="none" stroke="#fff" stroke-width="5" stroke-linecap="round" d="M23 26v-6a9 9 0 0 1 18 0v6"/>',
    # Activity: three bars rising beside a clock
    'activity': '<rect x="8" y="34" width="9" height="20" rx="3"/><rect x="21" y="24" width="9" height="30" rx="3"/>'
                '<rect x="34" y="14" width="9" height="40" rx="3"/>'
                '<path fill-rule="evenodd" d="M48 30a10 10 0 1 1 0 20a10 10 0 1 1 0-20zM46.5 34v7l5 3 1.3-2.2-3.8-2.3V34z"/>',
    # Favourites: a star
    'favorites': '<path d="M32 8l7.1 14.4 15.9 2.3-11.5 11.2 2.7 15.8L32 44.2l-14.2 7.5 2.7-15.8L9 24.7l15.9-2.3z" '
                 'stroke="#fff" stroke-width="4" stroke-linejoin="round"/>',
    # Recent: a clock
    'recently-played': '<path fill-rule="evenodd" d="M32 8a24 24 0 1 1 0 48a24 24 0 1 1 0-48zM32 14a18 18 0 1 0 0 36a18 18 0 1 0 0-36z"/>'
                       '<path d="M29 18h6v13.5l9 5.4-3 5.1-12-7.2z"/>',
    # Search: a magnifier
    'search': '<path fill-rule="evenodd" d="M27 8a19 19 0 1 1 0 38a19 19 0 1 1 0-38zM27 15a12 12 0 1 0 0 24a12 12 0 1 0 0-24z"/>'
              '<path d="M39.5 43.5l5-5 11 11a3.5 3.5 0 0 1-5 5z"/>',
    # Play together: two players
    'together': '<circle cx="22" cy="20" r="9"/><path d="M6 52c0-10 7-17 16-17s16 7 16 17z"/>'
                '<circle cx="43" cy="22" r="8"/><path d="M40.5 52c0-6-2-11-5.5-14.5A14.5 14.5 0 0 1 58 52z"/>',
    # Files: a folder
    'files': '<path d="M8 18a5 5 0 0 1 5-5h12l6 6h20a5 5 0 0 1 5 5v23a5 5 0 0 1-5 5H13a5 5 0 0 1-5-5z"/>',
    # Downloads: an arrow into a tray
    'downloads': '<path d="M28 8h8v22h9L32 44 19 30h9z"/><path d="M8 40h7v8h34v-8h7v10a5 5 0 0 1-5 5H13a5 5 0 0 1-5-5z"/>',
}


def gear_path():
    """a cog: 8 teeth, a hole in the middle (64 x 64)"""
    pts = []
    for i in range(48):
        a = 2 * math.pi * i / 48
        tooth = (i % 6) in (0, 1, 2)
        r = 27 if tooth else 21
        pts.append('%.2f %.2f' % (32 + r * math.cos(a), 32 + r * math.sin(a)))
    return ('<path fill-rule="evenodd" d="M%s z M32 22a10 10 0 1 0 0 20a10 10 0 1 0 0-20z"/>' % ' L'.join(pts))


# the buttons' little white shapes (tinted by the theme)
UI = {
    'search': OURS['search'],
    'gear': gear_path(),
    'star': OURS['favorites'],
    'wifi': '<path fill="none" stroke="#fff" stroke-width="6" stroke-linecap="round" d="M10 26a31 31 0 0 1 44 0M18 35a19 19 0 0 1 28 0"/>'
            '<circle cx="32" cy="46" r="5.5"/>',
}


def rgb(h):
    h = h.lstrip('#'); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def svg_png(svg, size):
    return Image.open(io.BytesIO(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size))).convert('RGBA')


def shape_svg(sid):
    if sid in OURS:
        return '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" fill="#ffffff">%s</svg>' % OURS[sid]
    return open(os.path.join(HERE, 'ra', sid + '.svg')).read()


def icon(sid):
    """the coloured rounded square with a soft finish, the white shape on it (drawn at 4x, saved at N x N)"""
    S = N * 4
    c = rgb(COLOURS[sid])
    top, bot = mix(c, (255, 255, 255), 0.16), mix(c, (0, 0, 0), 0.10)
    body = Image.new('RGBA', (S, S))
    d = ImageDraw.Draw(body)
    for y in range(S):
        d.line([(0, y), (S, y)], fill=mix(top, bot, y / (S - 1)) + (255,))
    # light on its top half
    sheen = Image.new('L', (S, S), 0)
    ds = ImageDraw.Draw(sheen)
    for y in range(int(S * 0.5)):
        ds.line([(0, y), (S, y)], fill=int(46 * (1 - y / (S * 0.5))))
    body = Image.composite(Image.new('RGBA', (S, S), (255, 255, 255, 255)), body, sheen)
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, S - 1, S - 1), int(S * 0.235), fill=255)
    body.putalpha(m)
    # the shape: as big as fits in 62% of the square, centred on its own box
    g = svg_png(shape_svg(sid), int(S * 0.9))
    bb = g.getbbox()
    g = g.crop(bb)
    f = S * 0.62 / max(g.size)
    g = g.resize((max(1, int(g.size[0] * f)), max(1, int(g.size[1] * f))), Image.LANCZOS)
    # a soft shadow under it, so a pale shape still reads on a pale colour
    sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    a = g.split()[3].point(lambda v: v * 0.22)
    shadow = Image.new('RGBA', g.size, (0, 0, 0, 255)); shadow.putalpha(a)
    ox, oy = (S - g.size[0]) // 2, (S - g.size[1]) // 2
    sh.alpha_composite(shadow, (ox, oy + S // 64))
    sh = sh.filter(ImageFilter.GaussianBlur(S / 96))
    body.alpha_composite(Image.composite(sh, Image.new('RGBA', (S, S)), m))
    body.alpha_composite(g, (ox, oy))
    return body.resize((N, N), Image.LANCZOS)


VARIANTS = {
    #           top gradient            stripe              bottom      dot
    'light': (('#ffffff', '#e8edf2'), (40, 55, 70, 9), '#f3f5f8', '#d3d9e0'),
    'dark': (('#2c3138', '#22262c'), (255, 255, 255, 7), '#23272d', '#3b424b'),
}


def backgrounds(v):
    (t0, t1), stripe, bg, dot = VARIANTS[v]
    top = Image.new('RGB', (W, H))
    d = ImageDraw.Draw(top)
    a, b = rgb(t0), rgb(t1)
    for y in range(H):
        d.line([(0, y), (W, y)], fill=mix(a, b, y / (H - 1)))
    # stripes: soft horizontal bands, 3 units of 9
    over = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    do = ImageDraw.Draw(over)
    step = 9 * K
    y = 0.0
    while y < H:
        do.rectangle((0, int(y), W, int(y + 3 * K)), fill=stripe)
        y += step
    top = Image.alpha_composite(top.convert('RGBA'), over).convert('RGB')
    bot = Image.new('RGB', (W * 4, H * 4), rgb(bg))
    db = ImageDraw.Draw(bot)
    r, g = 1.3 * K * 4, 20 * K * 4
    for yy in range(0, int(H * 4 / g) + 2):
        for xx in range(0, int(W * 4 / g) + 2):
            cx, cy = xx * g + g / 2, yy * g + g / 2
            db.ellipse((cx - r, cy - r, cx + r, cy + r), fill=rgb(dot))
    bot = bot.resize((W, H), Image.LANCZOS)
    for name, im in (('top', top), ('bottom', bot)):
        im.save(os.path.join(OUT, '%s-%s.jpg' % (v, name)), quality=92)
        im.filter(ImageFilter.GaussianBlur(14 * K / 2)).save(os.path.join(OUT, '%s-%s-frost.jpg' % (v, name)), quality=88)


RATE = 44100


def tone(parts, gain=0.5):
    """parts: (start s, length s, frequency from, to, decay rate) -> 16-bit mono samples"""
    n = int(RATE * max(p[0] + p[1] for p in parts)) + 1
    out = [0.0] * n
    for st, ln, f0, f1, dec in parts:
        ph = 0.0
        i0 = int(st * RATE)
        for i in range(int(ln * RATE)):
            t = i / RATE
            f = f0 + (f1 - f0) * (t / ln)
            ph += 2 * math.pi * f / RATE
            env = math.exp(-dec * t) * min(1.0, t / 0.002) * min(1.0, (ln - t) / 0.004)
            out[i0 + i] += env * (math.sin(ph) + 0.18 * math.sin(2 * ph))
    peak = max(1e-9, max(abs(v) for v in out))
    return [int(32767 * gain * v / peak) for v in out]


def write_wav(name, samples):
    with wave.open(os.path.join(OUT, 'sounds', name), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(b''.join(struct.pack('<h', s) for s in samples))


def sounds():
    os.makedirs(os.path.join(OUT, 'sounds'), exist_ok=True)
    write_wav('tick.wav', tone([(0, 0.022, 2300, 2100, 160)], 0.22))                 # a move
    write_wav('pop.wav', tone([(0, 0.075, 620, 1180, 38)], 0.42))                    # open, select
    write_wav('back.wav', tone([(0, 0.08, 980, 520, 36)], 0.38))                     # back
    write_wav('star.wav', tone([(0, 0.05, 1320, 1320, 50), (0.045, 0.09, 1760, 1760, 34)], 0.36))   # a favourite
    write_wav('chime.wav', tone([(0, 0.5, 1046.5, 1046.5, 7), (0.09, 0.5, 1318.5, 1318.5, 7),       # a game starting
                                 (0.18, 0.62, 1568, 1568, 6)], 0.4))


def dark_json():
    src = open(os.path.join(THEMES, 'plaza-light.json')).read()
    # the palette: light's colours swapped for dark's, by name (the "vars" block and the Settings colours)
    swap = {
        '"name": "Plaza light"': '"name": "Plaza dark"', '"base": "light"': '"base": "dark"',
        '"plaza/light-': '"plaza/dark-', '"pics": "plaza/light-"': '"pics": "plaza/dark-"',
    }
    for a, b in swap.items():
        src = src.replace(a, b)
    pal = {
        'bg': '#23272d', 'ink': '#eef1f4', 'sub': '#aab3bd', 'tile': '#353b43', 'edge': '#4a515b',
        'quit': '#ff8a96', 'shade': '#00000059', 'well': '#2c3138', 'chip': '#eef1f4', 'chipInk': '#23272d',
        'screenBg': '#23272d', 'muted': '#aab3bd', 'pane': '#353b43f2', 'hiMix': '#ffffff',
    }
    for k, v in pal.items():
        src, n = re.subn(r'("%s":\s*)"#[0-9a-fA-F]+"' % re.escape(k), r'\g<1>"%s"' % v, src)
        assert n, k
    src = src.replace('// Plaza light:', '// Plaza dark:', 1)
    open(os.path.join(THEMES, 'plaza-dark.json'), 'w').write(src)


if __name__ == '__main__':
    os.makedirs(os.path.join(OUT, 'icons'), exist_ok=True)
    for sid in sorted(COLOURS):
        if sid in OURS or os.path.exists(os.path.join(HERE, 'ra', sid + '.svg')):
            icon(sid).save(os.path.join(OUT, 'icons', sid + '.png'), optimize=True)
    os.makedirs(os.path.join(OUT, 'ui'), exist_ok=True)
    for name, body in UI.items():
        g = svg_png('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" fill="#ffffff">%s</svg>' % body, 96)
        g.save(os.path.join(OUT, 'ui', name + '.png'), optimize=True)
    for v in VARIANTS:
        backgrounds(v)
    sounds()
    if os.path.exists(os.path.join(THEMES, 'plaza-light.json')):
        dark_json()
    print('done')
