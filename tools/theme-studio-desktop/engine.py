"""The studio's own preview: a port of ui/src/layout.cpp's rules drawn with QPainter, over a made-up library.

It is close, not exact (text metrics, picture smoothing, shadows): the "Engine" preview runs the real banana-ui for
that. What it adds is interactivity: every element drawn is recorded with its box and its place in the JSON, so the
canvas can select, drag and resize it.
"""
import math
import os

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import (QColor, QFont, QFontDatabase, QFontMetricsF, QImage, QLinearGradient, QPainter,
                           QPainterPath, QPen, QRawFont)

import expr as X

W, H = 640.0, 480.0
VIEWS = ('home', 'library', 'launch', 'all')
TYPES = ('rect', 'text', 'image', 'group', 'list')
SKIP = {'type', 'id', 'comment', '//', 'children', 'item', 'states', 'with', 'props', 'smooth', 'use', 'name'}

# ---- the made-up library ---------------------------------------------------------------------------------------------
SYSTEMS = [
    ('nds', 'Nintendo DS', 'Nintendo', '#3d9e9a', False),
    ('gba', 'Game Boy Advance', 'Nintendo', '#8b6ad8', False),
    ('snes', 'Super Nintendo', 'Nintendo', '#a07ad4', False),
    ('gb', 'Game Boy', 'Nintendo', '#8a9144', False),
    ('psx', 'PlayStation', 'Sony', '#5c86e0', False),
    ('ports', 'Ports', 'PC', '#e08a4e', False),
    ('pico-8', 'PICO-8', 'Lexaloffle', '#ff77a8', True),
    ('favorites', 'Favorites', 'Collection', '#e23d52', False),
]
GAMES = {
    'nds': [('Banana Kart DS', '#d8b020', 14, '22h 12m', 'An hour ago', 12, 40), ('Peel Quest', '#3d9e9a', 3, '1h 30m', 'Yesterday', 3, 25),
            ('Split Racers', '#c84040', 0, '', '', 0, 0), ('Monkey Puzzle League', '#4a7ad0', 0, '', '', 0, 0),
            ('Yellow Planet', '#e0c040', 0, '', '', 0, 0)],
    'gba': [('Orchard Tactics', '#6aa040', 1, '15m', 'Last week', 0, 0), ('Tiny Fruit Advance', '#d06090', 0, '', '', 0, 0)],
    'snes': [('Super Smoothie World', '#8060c0', 0, '', '', 0, 0)],
    'gb': [('Pocket Peel', '#90a050', 0, '', '', 0, 0)],
    'psx': [('Crash Fruitcoot', '#e07020', 2, '3h 2m', '2 days ago', 0, 0), ('Final Fruitasy', '#3060a0', 0, '', '', 0, 0),
            ('Metal Gear Seed', '#506070', 0, '', '', 0, 0)],
    'ports': [('Doom', '#a03020', 5, '4h', 'Last month', 0, 0)],
    'pico-8': [],
    'favorites': [('Peel Quest', '#3d9e9a', 3, '1h 30m', 'Yesterday', 3, 25), ('Doom', '#a03020', 5, '4h', 'Last month', 0, 0)],
}
FAVS = {'Peel Quest', 'Doom'}


def mock_dir():
    d = os.path.join(os.environ.get('XDG_CACHE_HOME', os.path.expanduser('~/.cache')), 'banana-theme-studio', 'mock')
    os.makedirs(d, exist_ok=True)
    return d


def make_art(path, title, color, kind):
    """A made-up box, cartridge label or picture for a game (no game art is shipped)"""
    if os.path.exists(path):
        return
    w, h = (420, 380) if kind == 'box' else (256, 192)
    img = QImage(w, h, QImage.Format_ARGB32)
    p = QPainter(img)
    g = QLinearGradient(0, 0, 0, h)
    g.setColorAt(0, QColor(color))
    g.setColorAt(1, QColor('#000000'))
    p.fillRect(0, 0, w, h, g)
    p.setPen(QColor('white'))
    f = QFont()
    f.setPixelSize(34 if kind == 'box' else 24)
    f.setBold(True)
    p.setFont(f)
    p.drawText(QRectF(10, 0, w - 20, h), Qt.AlignCenter | Qt.TextWordWrap, title)
    p.end()
    img.save(path)


class Library:
    def __init__(self, icons_dir):
        self.systems = []
        d = mock_dir()
        for idx, (sid, name, maker, color, app) in enumerate(SYSTEMS):
            games = []
            for gi, (title, gc, plays, time, last, a, b) in enumerate(GAMES[sid]):
                safe = ''.join(c if c.isalnum() else '_' for c in title)
                box = os.path.join(d, safe + '-box.png')
                label = os.path.join(d, safe + '-label.png')
                make_art(box, title, gc, 'box')
                make_art(label, title, gc, 'label')
                games.append({
                    'title': title, 'sub': '' if gi % 3 else 'USA', 'plays': '%d plays' % plays if plays else '',
                    'last': last, 'time': time, 'box': box if gi < 3 else '', 'cart': '', 'label': label,
                    'icon': '', 'rom': '/roms/%s/%s' % (sid, title), 'achA': float(a), 'achB': float(b),
                    'achLabel': '%d / %d' % (a, b) if b else '', 'fav': 1.0 if title in FAVS else 0.0,
                    'systemId': sid, 'systemName': name,
                })
            played = [g for g in games if g['plays']]
            icon = os.path.join(icons_dir, {'favorites': 'fav', 'pico-8': 'fantasy'}.get(sid, sid) + '.png') if icons_dir else ''
            if not os.path.exists(icon):
                icon = os.path.join(icons_dir, 'cart.png') if icons_dir else ''
            r = played[0] if played else None
            self.systems.append({
                'id': sid, 'name': name, 'maker': maker, 'color': color, 'icon': icon, 'games': float(len(games)),
                'played': float(len(played)), 'time': '23h 42m' if sid == 'nds' else (played[0]['time'] if played else ''),
                'fav': 0.0, 'app': 1.0 if app else 0.0, 'hasResume': 1.0 if r else 0.0,
                'resumeTitle': r['title'] if r else '', 'resumeEyebrow': 'Continue' if r else '',
                'resumeBox': r['box'] if r else '', 'resumeCart': '', 'resumeLabel': r['label'] if r else '',
                'resumeArt': (r['box'] or r['label']) if r else '', '_games': games,
            })


class Sim:
    """The menu's state that the hooks read; the studio's simulation panel sets it"""

    def __init__(self):
        self.view = 'home'
        self.sys = 0
        self.game = 0
        self.time = 4000.0
        self.moved = 2000.0
        self.entered = 2000.0
        self.dir = 1
        self.launch = 0.5
        self.battery = 76.0
        self.charging = False
        self.online = True
        self.clock = '13:01'
        self.date = '10/23'
        self.scroll_offset = 0.0      # scroll = cursor + this: the eased position mid-move


# ---- the theme compiled for drawing ----------------------------------------------------------------------------------
class Node:
    __slots__ = ('type', 'props', 'children', 'item', 'states', 'with_', 'path', 'item_path', 'where')

    def __init__(self):
        self.type = 'rect'
        self.props = {}
        self.children = []
        self.item = None
        self.states = []
        self.with_ = []
        self.path = ()
        self.where = ''


def flatten(prefix, v, out):
    if isinstance(v, dict):
        for k, x in v.items():
            flatten(prefix + '.' + k, x, out)
    elif isinstance(v, list):
        for i, x in enumerate(v):
            flatten(prefix + '.' + str(i), x, out)
    elif isinstance(v, (str, int, float, bool)):
        out[prefix] = X.Prop(v)


def element_type(v):
    if not isinstance(v, dict):
        return 'group'
    return v.get('type') or ('group' if 'children' in v else 'text' if 'text' in v else 'image' if 'src' in v
                             else 'list' if 'source' in v else 'rect')


class Theme:
    def __init__(self, doc, theme_dir, errors):
        self.doc = doc if isinstance(doc, dict) else {}
        self.dir = theme_dir
        self.errors = errors
        self.components = self.doc.get('components') if isinstance(self.doc.get('components'), dict) else {}
        self.vars = []
        for k, v in (self.doc.get('vars') or {}).items() if isinstance(self.doc.get('vars'), dict) else []:
            one = {}
            flatten(k, v, one)
            self.vars += list(one.items())
        self.screens = {}
        sc = self.doc.get('screens')
        if isinstance(sc, dict):
            for view, v in sc.items():
                if not isinstance(v, dict):
                    self.err('screens.%s: { "top": ..., "bottom": ... }' % view)
                    continue
                for side in ('top', 'bottom'):
                    if side in v:
                        self.screens[view + '.' + side] = self.compile_screen(v[side], ('screens', view, side))
        b = self.doc.get('behavior') if isinstance(self.doc.get('behavior'), dict) else {}
        self.launch_ms = float(b.get('launchMs', 900)) if isinstance(b.get('launchMs', 900), (int, float)) else 900
        self.extra_hints = b.get('extraHints', True)
        self.hint_sets = {'home': [('A', 'Open'), ('Y', 'Favorite')], 'library': [('A', 'Play'), ('B', 'Back'), ('Y', 'Favorite')]}
        hs = self.doc.get('hints')
        if isinstance(hs, dict):
            for view, arr in hs.items():
                if isinstance(arr, list):
                    s = []
                    for p in arr:
                        if isinstance(p, list) and len(p) >= 2:
                            s.append((str(p[0]), str(p[1])))
                        elif isinstance(p, dict):
                            s.append((str(p.get('key', '')), str(p.get('label', ''))))
                    self.hint_sets[view] = s
        self.default_font = 'regular' if 'regular' in (self.doc.get('fonts') or {}) else 'pixel'
        self.default_font = self.doc.get('font', self.default_font)

    def err(self, e):
        if e not in self.errors:
            self.errors.append(e)

    def expand(self, v, depth, where):
        name = v.get('use')
        comp = self.components.get(name) if isinstance(name, str) else None
        if comp is None:
            self.err('%s: no component "%s"' % (where, name))
            return None, None
        if depth > 16:
            self.err(where + ': components used inside themselves')
            return None, None
        if not isinstance(comp, dict):
            return comp, ('components', name)
        merged = dict(comp)
        for k, x in v.items():
            if k != 'use':
                merged[k] = x
        inner = comp.get('use')
        if isinstance(inner, str):
            again = dict(merged)
            again['use'] = inner
            r, _ = self.expand(again, depth + 1, where)
            if r is not None:
                return r, ('components', name)
        merged.pop('use', None)
        return merged, ('components', name)

    def compile(self, v0, path, where, depth=0, comp_path=None):
        """comp_path: where the element's children come from when it is a component's instance"""
        if depth > 48:
            self.err(where + ': nested too deep')
            return None
        v = v0
        child_base = path
        if isinstance(v0, dict) and 'use' in v0:
            v, cp = self.expand(v0, depth, where)
            if v is None:
                return None
            child_base = cp
        if isinstance(v, list):
            g = Node()
            g.type = 'group'
            g.path = path
            g.where = where
            for k, x in enumerate(v):
                c = self.compile(x, child_base + (k,), '%s[%d]' % (where, k), depth + 1)
                if c:
                    g.children.append(c)
            return g
        if not isinstance(v, dict):
            self.err(where + ': an element is an object')
            return None
        n = Node()
        n.path = path
        n.type = element_type(v)
        if n.type not in TYPES:
            self.err('%s: unknown type "%s"' % (where, n.type))
            return None
        here = where + ('#' + str(v['id']) if 'id' in v else '') + '(' + n.type + ')'
        n.where = here
        # an instance's own children replace the component's: they're the instance's then
        own = isinstance(v0, dict)
        for k, x in v.items():
            if k in ('type', 'id', 'comment', '//', 'name'):
                continue
            if k == 'children':
                base = path if (own and 'children' in v0) else child_base
                if isinstance(x, list):
                    for i, c in enumerate(x):
                        ch = self.compile(c, base + ('children', i), '%s[%d]' % (here, i), depth + 1)
                        if ch:
                            n.children.append(ch)
                else:
                    ch = self.compile(x, base + ('children',), here + '.children', depth + 1)
                    if ch:
                        n.children.append(ch)
                continue
            if k == 'item':
                base = path if (own and 'item' in v0) else child_base
                n.item = self.compile(x, base + ('item',), here + '.item', depth + 1)
                continue
            if k == 'states':
                if not isinstance(x, list):
                    self.err(here + '.states: a list of { "when": ..., properties }')
                    continue
                for st in x:
                    if isinstance(st, dict):
                        props = {}
                        for sk, sv in st.items():
                            if sk != 'when':
                                flatten(sk, sv, props)
                        n.states.append((X.Prop(st.get('when', '0')), props))
                continue
            if k in ('with', 'props'):
                if isinstance(x, dict):
                    for pk, pv in x.items():
                        one = {}
                        flatten(pk, pv, one)
                        n.with_ += list(one.items())
                continue
            if k == 'smooth':
                continue
            flatten(k, x, n.props)
        for k, p in n.props.items():
            if p.error:
                self.err('%s.%s: %s' % (here, k, p.error))
        return n

    def compile_screen(self, v, path):
        where = 'screens.%s.%s' % (path[1], path[2])
        if isinstance(v, list):
            r = []
            for k, x in enumerate(v):
                n = self.compile(x, path + (k,), '%s[%d]' % (where, k))
                if n:
                    r.append(n)
            return r
        n = self.compile(v, path, where)
        return [n] if n else []

    def file(self, name):
        if not name or name.startswith('/') or ':' in name[:8]:
            return name
        return os.path.join(self.dir, name)


# ---- drawing ---------------------------------------------------------------------------------------------------------
class Hit:
    """An element drawn: its box (layout px, absolute), its screen and where it is in the JSON"""
    __slots__ = ('screen', 'rect', 'path', 'k', 'type', 'item', 'order', 'flow', 'where')

    def __init__(self, screen, rect, path, k, type_, item, order, flow, where):
        self.screen, self.rect, self.path, self.k, self.type = screen, rect, path, k, type_
        self.item, self.order, self.flow, self.where = item, order, flow, where


class Ctx:
    __slots__ = ('n', 'sc', 'on')

    def __init__(self, n, sc):
        self.n, self.sc = n, sc
        self.on = [props for when, props in reversed(n.states) if X.truthy(when.eval(sc))]

    def find(self, k):
        for st in self.on:
            if k in st:
                return st[k]
        return self.n.props.get(k)

    def has(self, k):
        return self.find(k) is not None

    def val(self, k, d=''):
        p = self.find(k)
        return p.eval(self.sc) if p else d

    def str(self, k, d=''):
        p = self.find(k)
        return X.to_str(p.eval(self.sc)) if p else d

    def flag(self, k, d=False):
        p = self.find(k)
        return X.truthy(p.eval(self.sc)) if p else d

    def num(self, k, d, rel=0.0):
        p = self.find(k)
        if not p:
            return d
        v = p.eval(self.sc)
        if isinstance(v, str):
            t = v.strip()
            if t.endswith('%'):
                return X.num(t[:-1]) / 100 * rel
            return X.num(t) if t else d
        return v

    def color(self, k):
        p = self.find(k)
        if not p:
            return None
        v = p.eval(self.sc)
        if not isinstance(v, str):
            return 0xff000000 | (int(v) & 0xffffff)
        return X.parse_color(v)


def qcolor(argb, opacity=1.0):
    return QColor((argb >> 16) & 255, (argb >> 8) & 255, argb & 255, int(round(((argb >> 24) & 255) * opacity)))


def rounded(x, y, w, h, r):
    """A path with four corner radii [tl, tr, br, bl]"""
    p = QPainterPath()
    m = min(w, h) / 2
    tl, tr, br, bl = (max(0.0, min(m, v)) for v in r)
    p.moveTo(x + tl, y)
    p.lineTo(x + w - tr, y)
    if tr:
        p.arcTo(x + w - 2 * tr, y, 2 * tr, 2 * tr, 90, -90)
    p.lineTo(x + w, y + h - br)
    if br:
        p.arcTo(x + w - 2 * br, y + h - 2 * br, 2 * br, 2 * br, 0, -90)
    p.lineTo(x + bl, y + h)
    if bl:
        p.arcTo(x, y + h - 2 * bl, 2 * bl, 2 * bl, 270, -90)
    p.lineTo(x, y + tl)
    if tl:
        p.arcTo(x, y, 2 * tl, 2 * tl, 180, -90)
    p.closeSubpath()
    return p


class Renderer:
    def __init__(self, repo_root):
        self.repo = repo_root
        self.fonts = {}          # name -> family
        self.font_files = {}
        self.images = {}
        assets = os.path.join(repo_root, 'ui', 'assets') if repo_root else ''
        self.icons_dir = os.path.join(assets, 'rnds', 'icons') if assets else ''
        builtin = {
            'pixel': ('rnds', 'PixelifySans-Regular.ttf'), 'pixel-medium': ('rnds', 'PixelifySans-Medium.ttf'),
            'inter': ('banana', 'Inter-Regular.ttf'), 'inter-medium': ('banana', 'Inter-Medium.ttf'),
            'inter-semibold': ('banana', 'Inter-SemiBold.ttf'), 'inter-bold': ('banana', 'Inter-Bold.ttf'),
        }
        self.builtin = {k: os.path.join(assets, a, 'fonts', f) for k, (a, f) in builtin.items()} if assets else {}
        self.lib = Library(self.icons_dir)
        self.sim = Sim()
        self.theme = None
        self.errors = []
        self.hits = []

    # ---- theme -------------------------------------------------------------------------------------------------------
    def set_theme(self, doc, theme_dir):
        self.errors = []
        self.theme = Theme(doc, theme_dir, self.errors)
        self.font_files = dict(self.builtin)
        fonts = doc.get('fonts') if isinstance(doc, dict) else None
        if isinstance(fonts, dict):
            for k, v in fonts.items():
                if isinstance(v, str):
                    self.font_files[k] = self.theme.file(v)

    def family(self, name):
        """(family, weight, italic) of a font by its theme name"""
        key = self.font_files.get(name, '')
        if key in self.fonts:
            return self.fonts[key]
        fam = None
        if key and os.path.exists(key):
            fid = QFontDatabase.addApplicationFont(key)
            fams = QFontDatabase.applicationFontFamilies(fid) if fid >= 0 else []
            if fams:
                raw = QRawFont(key, 16)
                # the weights of one family (Inter Regular ... Bold) are told apart by weight
                fam = (fams[0], raw.weight() if raw.isValid() else QFont.Normal, raw.isValid() and raw.style() != QFont.StyleNormal)
        if fam is None:
            if name != self.theme.default_font and name:
                self.theme.err('font "%s" can\'t be loaded (%s)' % (name, key or 'not in fonts'))
                return self.family(self.theme.default_font) if name != 'pixel' else ('sans-serif', QFont.Normal, False)
            fam = ('sans-serif', QFont.Normal, False)
        self.fonts[key] = fam
        return fam

    def qfont(self, name, size, spacing=0.0):
        fam, weight, italic = self.family(name)
        f = QFont(fam)
        f.setWeight(QFont.Weight(weight))
        f.setItalic(italic)
        f.setPixelSize(max(1, int(round(size))))
        f.setHintingPreference(QFont.PreferNoHinting)
        if spacing:
            f.setLetterSpacing(QFont.AbsoluteSpacing, spacing)
        return f

    def image(self, path):
        if path in self.images:
            return self.images[path]
        img = QImage(path) if path and os.path.exists(path) else None
        if img is not None and img.isNull():
            img = None
        if len(self.images) > 300:
            self.images.clear()
        self.images[path] = img
        return img

    # ---- hooks -------------------------------------------------------------------------------------------------------
    def accent(self):
        a = self.theme.doc.get('accent') if self.theme else None
        c = X.parse_color(a) if isinstance(a, str) else None
        return X.color_text(c)[:7] if c is not None else '#f2c94c'

    def systems(self):
        return self.lib.systems

    def games(self):
        s = self.sim
        if s.view == 'home':
            return []
        return self.lib.systems[min(s.sys, len(self.lib.systems) - 1)]['_games']

    def cursor(self):
        return self.sim.sys if self.sim.view == 'home' else self.sim.game

    def count(self):
        return len(self.systems()) if self.sim.view == 'home' else len(self.games())

    def sys_field(self, si, idx, f):
        if f == 'index':
            return float(idx)
        if f == 'accent':
            f = 'color'
        v = si.get(f)
        return None if v is None or f.startswith('_') else v

    def game_field(self, g, idx, f):
        if f == 'index':
            return float(idx)
        if f == 'art':
            return g['box'] or g['label'] or g['cart']
        if f == 'romIcon':
            return ''
        if f == 'achievements':
            f = 'achA'
        if f == 'achievementsTotal':
            f = 'achB'
        if f == 'achProgress':
            return g['achA'] / g['achB'] if g['achB'] else 0.0
        if f == 'system':
            f = 'systemId'
        return g.get(f)

    def hook(self, n):
        s = self.sim
        if n == 'time':
            return s.time
        if n == 'view':
            return s.view
        if n == 'W':
            return W
        if n == 'H':
            return H
        if n == 'clock':
            return s.clock
        if n == 'date':
            return s.date
        if n == 'battery':
            return s.battery
        if n == 'charging':
            return 1.0 if s.charging else 0.0
        if n == 'batteryMode':
            return 'icon'
        if n == 'online':
            return 1.0 if s.online else 0.0
        if n == 'motion':
            return 1.0
        if n == 'brand':
            return 'Alpha 0.5.0'
        if n in ('accent', 'accent.base'):
            return self.accent()
        if n.startswith('accent.'):
            c = X.color_of(self.accent())
            t = {'light': lambda: X.mix_color(c, 0xffffffff, 0.35), 'dark': lambda: X.mix_color(c, 0xff000000, 0.35),
                 'band': lambda: X.mix_color(c, 0xff10131c, 0.6), 'low': lambda: X.mix_color(c, 0xff10131c, 0.8)}.get(n[7:])
            return X.color_text(t())[:7] if t else None
        if n.startswith('palette.'):
            cols = self.theme.doc.get('colors') if isinstance(self.theme.doc.get('colors'), dict) else {}
            dflt = {'ink': '#eef1ff', 'muted': '#9aa3c0', 'screenBg': '#10131c', 'panel': '#1b2030', 'line': '#2a3045'}
            return cols.get(n[8:], dflt.get(n[8:], ''))
        if n == 'cursor':
            return float(self.cursor())
        if n == 'scroll':
            return self.cursor() + s.scroll_offset
        if n == 'count':
            return float(self.count())
        if n == 'moved':
            return s.moved
        if n == 'entered':
            return s.entered
        if n == 'dir':
            return float(s.dir)
        if n in ('systems.count', 'systemCount'):
            return float(len(self.systems()))
        if n in ('games.count', 'gameCount'):
            return float(len(self.games()))
        if n == 'launch.t':
            return s.launch * self.theme.launch_ms if s.view == 'launch' else 0.0
        if n == 'launch.progress':
            return s.launch if s.view == 'launch' else 0.0
        if n == 'empty':
            return 1.0 if s.view != 'home' and not self.games() else 0.0
        if n == 'empty.title':
            return 'No games yet'
        if n == 'empty.hint':
            return 'Put games in roms/' + self.lib.systems[s.sys]['id']
        if n.startswith('sys.'):
            sy = self.lib.systems[min(s.sys, len(self.lib.systems) - 1)]
            r = self.sys_field(sy, s.sys, n[4:])
            return '' if r is None else r
        if n.startswith('game.'):
            g = self.games()
            if s.view != 'home' and g:
                r = self.game_field(g[min(s.game, len(g) - 1)], s.game, n[5:])
                if r is not None:
                    return r
            return ''
        return None

    def hints_now(self):
        v = 'home' if self.sim.view == 'home' else 'library'
        r = list(self.theme.hint_sets.get(v, []))
        if self.theme.extra_hints:
            for h in (('SELECT', 'Search'),):
                if not any(e[0] == h[0] for e in r):
                    r.append(h)
        return r

    # ---- layout ------------------------------------------------------------------------------------------------------
    def layout_text(self, c, k, box_w):
        """(lines, width, height, line height, font) in device px of the frame k"""
        t = c.str('text')
        name = c.str('font', self.theme.default_font)
        size = c.num('size', 16) * k
        f = self.qfont(name, size, c.num('spacing', 0) * k)
        fm = QFontMetricsF(f)
        lh = c.num('lineHeight', 0) * k if c.has('lineHeight') else fm.lineSpacing()
        if not t:
            return [], 0.0, 0.0, lh, f
        if c.flag('upper'):
            t = t.upper()
        wrap = c.flag('wrap')
        max_lines = int(c.num('maxLines', 0 if wrap else 1))
        ell = c.flag('ellipsis', True)
        lines = []
        for para in t.split('\n'):
            if max_lines > 0 and len(lines) >= max_lines:
                break
            if box_w > 0 and (wrap or max_lines != 1):
                left = max_lines - len(lines) if max_lines > 0 else 0
                words = para.split(' ')
                cur = ''
                out = []
                for wd in words:
                    trial = wd if not cur else cur + ' ' + wd
                    if fm.horizontalAdvance(trial) <= box_w or not cur:
                        cur = trial
                    else:
                        out.append(cur)
                        cur = wd
                out.append(cur)
                if left and len(out) > left:
                    out = out[:left]
                    out[-1] = fm.elidedText(out[-1] + ' ' + words[-1], Qt.ElideRight, box_w)
                lines += out
            elif box_w > 0 and ell:
                lines.append(fm.elidedText(para, Qt.ElideRight, box_w))
            else:
                lines.append(para)
        width = max((fm.horizontalAdvance(l) for l in lines), default=0.0)
        return lines, width, len(lines) * lh, lh, f

    def box_of(self, c, pw, ph, nat_w, nat_h):
        w, h = c.num('w', -1, pw), c.num('h', -1, ph)
        hasX, hasR, hasY, hasB = c.has('x'), c.has('right'), c.has('y'), c.has('bottom')
        x, y = c.num('x', 0, pw), c.num('y', 0, ph)
        if w < 0 and hasX and hasR:
            w = pw - x - c.num('right', 0, pw)
        if h < 0 and hasY and hasB:
            h = ph - y - c.num('bottom', 0, ph)
        if w < 0:
            w = nat_w if nat_w >= 0 else (0 if c.n.type in ('text', 'image') else pw)
        if h < 0:
            h = nat_h if nat_h >= 0 else (0 if c.n.type in ('text', 'image') else ph)
        if not hasX and hasR:
            x = pw - c.num('right', 0, pw) - w
        if not hasY and hasB:
            y = ph - c.num('bottom', 0, ph) - h
        ax = ay = 0.0
        an = c.str('anchor')
        if an:
            if 'center' in an or 'middle' in an:
                ax = ay = 0.5
            if 'left' in an:
                ax = 0
            if 'right' in an:
                ax = 1
            if 'top' in an:
                ay = 0
            if 'bottom' in an:
                ay = 1
            if an in ('top', 'bottom'):
                ax = 0.5
            if an in ('left', 'right'):
                ay = 0.5
        ax, ay = c.num('ax', ax), c.num('ay', ay)
        sc = c.num('scale', 1)
        x -= ax * w * sc
        y -= ay * h * sc
        return x, y, w * sc, h * sc

    def with_scope(self, n, sc):
        if not n.with_:
            return sc
        inner = X.Scope(sc)
        for k, p in n.with_:
            inner.vars[k] = p.eval(sc)
        return inner

    def natural(self, c0, pw, ph, k):
        c = c0
        if c0.n.with_:
            c = Ctx(c0.n, self.with_scope(c0.n, c0.sc))
        if c.n.type == 'text':
            bw = c.num('w', 0, pw)
            lines, tw, th, lh, f = self.layout_text(c, k, bw * k)
            return (bw if bw > 0 else tw / k), th / k
        lay = c.str('layout')
        if c.n.type != 'group' or lay not in ('row', 'column'):
            return -1, -1
        row = lay == 'row'
        gap, pad = c.num('gap', 0), c.num('padding', 0)
        along = across = 0.0
        kids = 0
        for ch in c.n.children:
            cc = Ctx(ch, c.sc)
            if (cc.has('visible') and not cc.flag('visible')) or (cc.has('if') and not cc.flag('if')):
                continue
            kw, kh = self.natural(cc, pw, ph, k)
            kw = cc.num('w', kw if kw >= 0 else 0, pw)
            kh = cc.num('h', kh if kh >= 0 else 0, ph)
            along += kw if row else kh
            across = max(across, kh if row else kw)
            kids += 1
        along += gap * max(0, kids - 1) + 2 * pad
        across += 2 * pad
        return (along, across) if row else (across, along)

    # ---- painting ----------------------------------------------------------------------------------------------------
    def render(self, p, screen, outline=False):
        """Draws one screen (0 top, 1 bottom) of the current view at 640 x 480 into the painter"""
        self.hits_screen = []
        self.order = 0
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        p.setRenderHint(QPainter.TextAntialiasing)
        p.setRenderHint(QPainter.SmoothPixmapTransform)
        p.fillRect(QRectF(0, 0, W, H), QColor('#000000'))
        p.setClipRect(QRectF(0, 0, W, H))
        if self.theme is None:
            p.restore()
            return []
        root = X.Scope(resolve=self.hook)
        vs = X.Scope(root)
        for k, prop in self.theme.vars:
            vs.vars[k] = prop.eval(vs)
        ss = X.Scope(vs, {'screen': float(screen)})
        side = 'top' if screen == 0 else 'bottom'
        view = self.sim.view
        nodes = None
        for v in (view, 'library' if view == 'launch' else 'home', 'home'):
            if self.theme.screens.get(v + '.' + side):
                nodes = self.theme.screens[v + '.' + side]
                break
        self.screen = screen
        frame = (0.0, 0.0, 1.0, 1.0)
        for n in nodes or []:
            self.render_node(p, n, ss, frame, W, H, None, False)
        for n in self.theme.screens.get('all.' + side, []):
            self.render_node(p, n, ss, frame, W, H, None, False)
        if outline:
            p.setBrush(Qt.NoBrush)
            pen = QPen(QColor(255, 0, 255, 110))
            pen.setWidthF(0)
            p.setPen(pen)
            for h in self.hits_screen:
                p.drawRect(h.rect)
        p.restore()
        return self.hits_screen

    def record(self, n, rect, k, item, flow):
        self.order += 1
        self.hits_screen.append(Hit(self.screen, rect, n.path, k, n.type, item, self.order, flow, n.where))

    def render_node(self, p, n, sc, f, pw, ph, item, flow):
        fx, fy, fk, fop = f
        c = Ctx(n, sc)
        if c.has('visible') and not c.flag('visible'):
            return
        if c.has('if') and not c.flag('if'):
            return
        opacity = fop * min(1.0, max(0.0, c.num('opacity', 1)))
        if opacity <= 0.002:
            return
        csc = self.with_scope(n, sc)
        if n.with_:
            c.sc = csc
        nw = nh = -1
        text = None
        if n.type == 'text':
            bw = c.num('w', 0, pw)
            s0 = c.num('scale', 1)
            text = self.layout_text(c, fk * s0, bw * fk * s0)
            nw, nh = text[1] / (fk * s0), text[2] / (fk * s0)
            if bw > 0:
                nw = bw
        elif n.type == 'group' and (not c.has('w') or not c.has('h')):
            nw, nh = self.natural(c, pw, ph, fk)
        bx, by, bw_, bh_ = self.box_of(c, pw, ph, nw, nh)
        ax0, ay0, aw, ah = fx + bx * fk, fy + by * fk, bw_ * fk, bh_ * fk
        self.record(n, QRectF(ax0, ay0, aw, ah), fk, item, flow)
        ck = fk * c.num('scale', 1)
        if n.type == 'rect':
            self.draw_rect(p, c, ax0, ay0, aw, ah, ck, opacity)
        elif n.type == 'text':
            self.draw_text(p, c, ax0, ay0, aw if c.has('w') else 0, ah if c.has('h') else 0, ck, opacity, text)
        elif n.type == 'image':
            self.draw_image(p, c, ax0, ay0, aw, ah, ck, opacity)
        elif n.type == 'group':
            if any(c.has(k) for k in ('fill', 'gradient.stops.0', 'gradient.0', 'border', 'border.width', 'shadow.color')):
                self.draw_rect(p, c, ax0, ay0, aw, ah, ck, opacity)
        clip = c.flag('clip')
        if clip:
            p.save()
            p.setClipRect(QRectF(ax0, ay0, aw, ah), Qt.IntersectClip)
        cf = (ax0, ay0, ck, opacity)
        if n.type == 'list':
            self.render_list(p, c, csc, cf, bw_, bh_, n)
        elif n.children:
            lay = c.str('layout')
            if lay in ('row', 'column'):
                self.render_flow(p, c, csc, cf, bw_, bh_, lay == 'row', item)
            else:
                for ch in n.children:
                    self.render_node(p, ch, csc, cf, bw_, bh_, item, False)
        if clip:
            p.restore()

    def render_flow(self, p, c, sc, f, w, h, row, item):
        fx, fy, fk, fop = f
        gap, pad = c.num('gap', 0), c.num('padding', 0)
        justify, align = c.str('justify', 'start'), c.str('align', 'start')
        kids = []
        total = 0.0
        for ch in c.n.children:
            cc = Ctx(ch, sc)
            if (cc.has('visible') and not cc.flag('visible')) or (cc.has('if') and not cc.flag('if')):
                continue
            nw, nh = self.natural(cc, w, h, fk)
            kw = cc.num('w', nw if nw >= 0 else 0, w)
            kh = cc.num('h', nh if nh >= 0 else 0, h)
            kids.append((ch, kw, kh))
            total += kw if row else kh
        if not kids:
            return
        total += gap * (len(kids) - 1)
        avail = (w if row else h) - 2 * pad
        at, extra = pad, 0.0
        if justify == 'center':
            at = pad + (avail - total) / 2
        elif justify == 'end':
            at = pad + avail - total
        elif justify == 'between' and len(kids) > 1:
            extra = (avail - total) / (len(kids) - 1)
        for ch, kw, kh in kids:
            cross = (h - 2 * pad - kh) if row else (w - 2 * pad - kw)
            off = pad
            if align == 'center':
                off = pad + cross / 2
            elif align == 'end':
                off = pad + cross
            kf = (fx + (at if row else off) * fk, fy + (off if row else at) * fk, fk, fop)
            self.render_node(p, ch, sc, kf, kw, kh, item, True)
            at += (kw if row else kh) + gap + extra

    def render_list(self, p, c, sc, f, w, h, ln):
        fx, fy, fk, fop = f
        tpl = ln.item or (ln.children[0] if ln.children else None)
        if tpl is None:
            return
        source = c.str('source', 'systems')
        view = self.sim.view
        hl = []
        if source == 'systems':
            items, cur = self.systems(), self.sim.sys
        elif source == 'games':
            items, cur = self.games(), (-1 if view == 'home' else self.sim.game)
        elif source == 'hints':
            hl = self.hints_now()
            items, cur = hl, -1
        elif source == 'range':
            items, cur = [None] * max(0, int(c.num('count', 0))), -1
        else:
            self.theme.err('list source "%s": systems, games, hints or range' % source)
            return
        n = len(items)
        dirn = c.str('direction', 'free')
        item_w = c.num('itemW', w if dirn == 'column' else 100)
        item_h = c.num('itemH', h if dirn == 'row' else 40)
        gap = c.num('gap', 0)
        cols = max(1, int(c.num('columns', 1)))
        step_x, step_y = c.num('stepX', item_w + gap), c.num('stepY', item_h + gap)
        mode = c.str('scrollMode', 'center')
        loop = c.flag('loop') and n > 0
        active = (source == 'systems' and view == 'home') or (source == 'games' and view != 'home')
        pos0 = (self.cursor() + self.sim.scroll_offset) if active else float(max(0, cur))
        along, step = (w, step_x) if dirn == 'row' else (h, step_y)
        line_of_cur = max(0, cur) // (cols if dirn == 'grid' else 1)
        pos_line = pos0 / cols if dirn == 'grid' else pos0
        visible = max(1, int(math.floor((along + gap) / max(1.0, step))))
        first = 0.0
        if mode == 'center':
            first = pos_line - (along - (step - gap)) / 2 / max(1.0, step)
        elif mode == 'edge':
            first = float(max(0, line_of_cur - visible + 1))
        lines = (n + cols - 1) // cols if dirn == 'grid' else n
        if dirn == 'free':
            r = int(c.num('range', 8))
            cc = max(0, cur)
            lo, hi = cc - r, cc + r
            if not loop:
                lo, hi = max(0, lo), min(n - 1, hi)
        else:
            l0 = int(math.floor(first)) - 1
            l1 = int(math.ceil(first + along / max(1.0, step))) + 1
            if not loop:
                l0, l1 = max(0, l0), min(lines - 1, l1)
            lo = l0 * cols if dirn == 'grid' else l0
            hi = l1 * cols + cols - 1 if dirn == 'grid' else l1
            if not loop:
                hi = min(n - 1, hi)
        if n == 0:
            return
        order = list(range(lo, hi + 1))
        if c.flag('selectedOnTop', True) and cur >= 0:
            order.sort(key=lambda v: (v % n) == cur)

        def fill(v, i):
            rel, pos = float(v - max(0, cur)), v - pos0
            if loop and n > 1:
                half = n / 2.0
                while pos > half:
                    pos -= n
                while pos < -half:
                    pos += n
                while rel > half:
                    rel -= n
                while rel < -half:
                    rel += n
            s = X.Scope(sc, {'i': float(i), 'n': float(n), 'sel': 1.0 if i == cur else 0.0, 'rel': rel, 'pos': pos,
                             'first': 1.0 if i == 0 else 0.0, 'last': 1.0 if i == n - 1 else 0.0})
            if source == 'hints':
                s.vars['item.key'], s.vars['item.label'] = hl[i]
            elif source == 'systems':
                si = items[i]
                s.resolve = lambda k, si=si, i=i: self.sys_field(si, i, k[5:]) if k.startswith('item.') else None
            elif source == 'games':
                gi = items[i]
                s.resolve = lambda k, gi=gi, i=i: self.game_field(gi, i, k[5:]) if k.startswith('item.') else None
            return s

        flow_at = []
        if c.flag('flow') and dirn in ('row', 'column'):
            at = 0.0
            for i in range(n):
                flow_at.append(at)
                tc = Ctx(tpl, fill(i, i))
                nw, nh = self.natural(tc, item_w, item_h, fk)
                at += (tc.num('w', nw if nw >= 0 else item_w, item_w) if dirn == 'row'
                       else tc.num('h', nh if nh >= 0 else item_h, item_h)) + gap
        for v in order:
            i = v % n
            s = fill(v, i)
            ix = iy = 0.0
            if dirn == 'row':
                ix = (v - first) * step_x
            elif dirn == 'column':
                iy = (v - first) * step_y
            elif dirn == 'grid':
                row = math.floor(v / cols)
                ix = (v - row * cols) * step_x
                iy = (row - first) * step_y
            if dirn == 'row' and mode == 'none':
                ix = v * step_x
            if dirn == 'column' and mode == 'none':
                iy = v * step_y
            if flow_at:
                if dirn == 'row':
                    ix = flow_at[i]
                else:
                    iy = flow_at[i]
            s.vars['ix'], s.vars['iy'] = ix, iy
            itf = (fx + ix * fk, fy + iy * fk, fk, fop)
            iw, ih = (w, h) if dirn == 'free' else (item_w, item_h)
            self.render_node(p, tpl, s, itf, iw, ih, ln.path, False)

    # ---- primitives --------------------------------------------------------------------------------------------------
    def draw_rect(self, p, c, x, y, w, h, k, opacity):
        fill = c.color('fill')
        if fill is None:
            fill = c.color('color')
        stops = []
        for i in range(16):
            key = 'gradient.stops.%d' % i
            if not c.has(key):
                key = 'gradient.%d' % i
                if not c.has(key):
                    break
            t = c.str(key).strip()
            sp = t.find(' ')
            col = X.parse_color(t if sp < 0 else t[:sp])
            if col is None:
                continue
            at = -1 if sp < 0 else X.num(t[sp + 1:].strip().rstrip('%')) / 100
            stops.append([at, col])
        if stops:
            for i, s in enumerate(stops):
                if s[0] < 0:
                    s[0] = 0 if len(stops) == 1 else i / (len(stops) - 1)
            if len(stops) == 1:
                fill, stops = stops[0][1], []
        angle = c.num('gradient.angle', 180)
        r0 = c.num('radius', 0, min(w / k if k else w, h / k if k else h))
        rad = [(c.num('radius.%d' % i, 0) if c.has('radius.%d' % i) else r0) * k for i in range(4)]
        border = c.num('border', 0) * k
        if c.has('border.width'):
            border = c.num('border.width', 0) * k
        bc = c.color('borderColor')
        if bc is None:
            bc = c.color('border.color')
        shc = c.color('shadow.color')
        has_fill = fill is not None and (fill >> 24)
        has_border = border > 0 and bc and (bc >> 24)
        has_shadow = shc and (shc >> 24)
        if not has_fill and not stops and not has_border and not has_shadow:
            return
        if w <= 0 or h <= 0:
            return
        path = rounded(x, y, w, h, rad)
        if has_shadow:
            blur = c.num('shadow.blur', 0) * k
            spread = c.num('shadow.spread', 0) * k
            sx, sy = c.num('shadow.x', 0) * k, c.num('shadow.y', 0) * k
            steps = max(1, min(10, int(blur / 2) + 1)) if blur > 0 else 1
            base = qcolor(shc, opacity)
            for s in range(steps):
                grow = spread + (blur * (s + 0.5) / steps if blur > 0 else 0)
                col = QColor(base)
                col.setAlphaF(base.alphaF() / steps * (1.6 if blur > 0 else 1))
                sp_ = rounded(x + sx - grow, y + sy - grow, w + 2 * grow, h + 2 * grow, [r + grow for r in rad])
                p.fillPath(sp_, col)
        if stops:
            a = math.radians(angle - 90)
            dx, dy = math.cos(a), math.sin(a)
            span = abs(w * dx) + abs(h * dy)
            cx, cy = x + w / 2, y + h / 2
            g = QLinearGradient(QPointF(cx - dx * span / 2, cy - dy * span / 2), QPointF(cx + dx * span / 2, cy + dy * span / 2))
            for at, col in stops:
                g.setColorAt(min(1.0, max(0.0, at)), qcolor(col, opacity))
            p.fillPath(path, g)
        elif has_fill:
            p.fillPath(path, qcolor(fill, opacity))
        if has_border:
            inner = rounded(x + border, y + border, w - 2 * border, h - 2 * border, [max(0, r - border) for r in rad])
            p.fillPath(path.subtracted(inner), qcolor(bc, opacity))

    def draw_text(self, p, c, x, y, w, h, k, opacity, layout):
        lines, tw, th, lh, f = layout
        if not lines:
            return
        col = c.color('color')
        col = 0xffffffff if col is None else col
        align, valign = c.str('align', 'left'), c.str('valign', 'top')
        top = y
        if h > 0 and valign == 'middle':
            top = y + (h - th) / 2
        elif h > 0 and valign == 'bottom':
            top = y + h - th
        fm = QFontMetricsF(f)
        shc = c.color('shadow.color')
        sx, sy = c.num('shadow.x', 1) * k, c.num('shadow.y', 1) * k
        olc = c.color('outline.color')
        ow = c.num('outline.width', 1) * k
        p.setFont(f)
        for i, l in enumerate(lines):
            lw = fm.horizontalAdvance(l)
            lx = x
            if w > 0 and align == 'center':
                lx = x + (w - lw) / 2
            elif w > 0 and align == 'right':
                lx = x + w - lw
            base = top + i * lh + (lh - fm.lineSpacing()) / 2 + fm.ascent() + fm.leading() / 2
            if olc and olc >> 24:
                p.setPen(qcolor(olc, opacity))
                for ox, oy in ((-1, -1), (0, -1), (1, -1), (-1, 0), (1, 0), (-1, 1), (0, 1), (1, 1)):
                    p.drawText(QPointF(lx + ox * ow, base + oy * ow), l)
            if shc and shc >> 24:
                p.setPen(qcolor(shc, opacity))
                p.drawText(QPointF(lx + sx, base + sy), l)
            p.setPen(qcolor(col, opacity))
            p.drawText(QPointF(lx, base), l)

    def src_path(self, c, k):
        v = c.str(k)
        if not v or v.startswith('/') or v.startswith('romicon:'):
            return v
        t = self.theme.file(v)
        return t if os.path.exists(t) else v

    def draw_image(self, p, c, x, y, w, h, k, opacity):
        src = self.src_path(c, 'src')
        img = self.image(src) if src else None
        if img is None and c.has('fallback'):
            img = self.image(self.src_path(c, 'fallback'))
        if w <= 0 or h <= 0:
            return
        radius = c.num('radius', 0, min(w, h) / (k or 1)) * k
        if img is None:
            pc = c.color('placeholder')
            if pc and pc >> 24:
                p.fillPath(rounded(x, y, w, h, [radius] * 4), qcolor(pc, opacity))
            return
        fit = c.str('fit', 'cover')
        iw, ih = img.width(), img.height()
        src = QRectF(0, 0, iw, ih)
        dst = QRectF(x, y, w, h)
        smooth = True
        if fit in ('cover', 'coverTop'):
            s = max(w / iw, h / ih)
            cw, ch = w / s, h / s
            src = QRectF((iw - cw) / 2, 0 if fit == 'coverTop' else (ih - ch) / 2, cw, ch)
        elif fit == 'contain':
            s = min(w / iw, h / ih)
            dst = QRectF(x + (w - iw * s) / 2, y + (h - ih * s) / 2, iw * s, ih * s)
        elif fit == 'pixelContain':
            s = min(w / iw, h / ih)
            s = max(1, math.floor(s)) if s >= 1 else s
            dst = QRectF(x + (w - iw * s) / 2, y + (h - ih * s) / 2, iw * s, ih * s)
            smooth = False
        elif fit == 'pixel':
            smooth = False
        tc = c.color('tint')
        if tc is not None and tc != 0xffffffff:
            t = img.convertToFormat(QImage.Format_ARGB32_Premultiplied).copy()
            tp = QPainter(t)
            tp.setCompositionMode(QPainter.CompositionMode_Multiply)
            tp.fillRect(t.rect(), qcolor(tc | 0xff000000))
            tp.setCompositionMode(QPainter.CompositionMode_DestinationIn)
            tp.drawImage(0, 0, img)
            tp.end()
            img = t
            opacity *= ((tc >> 24) & 255) / 255
        p.save()
        p.setOpacity(opacity)
        p.setRenderHint(QPainter.SmoothPixmapTransform, smooth)
        if radius > 0:
            p.setClipPath(rounded(x, y, w, h, [radius] * 4), Qt.IntersectClip)
        p.drawImage(dst, img, src)
        p.restore()
