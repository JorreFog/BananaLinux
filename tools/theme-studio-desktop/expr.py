"""The layout themes' expression language, ported from ui/src/expr.cpp (keep the two in step).

Values are floats or strings. A property is a literal, "=expression" or a template with {expression} parts.
"""
import math
import re

FUNCTIONS = {
    'maths': 'min max clamp abs floor ceil round sqrt pow sign mod sin cos sind cosd lerp step smoothstep',
    'motion': 'progress easeIn easeOut easeInOut easeOutBack easeOutElastic bounce pulse',
    'colour': 'mix alpha lighten darken rgb rgba hsl',
    'text': 'upper lower len substr contains replace pad fixed plural str num',
    'choice': 'if pick either',
}
FUNCTION_NAMES = sorted(n for v in FUNCTIONS.values() for n in v.split())
SIGNATURES = {
    'clamp': 'clamp(v, lo, hi)', 'round': 'round(v, digits)', 'mod': 'mod(a, b)', 'lerp': 'lerp(a, b, t)',
    'step': 'step(edge, x)', 'smoothstep': 'smoothstep(a, b, x)', 'progress': 'progress(ms_ago, duration) 0..1',
    'pulse': 'pulse(time, period) 0..1..0', 'mix': 'mix(c1, c2, t)', 'alpha': 'alpha(c, a)', 'lighten': 'lighten(c, t)',
    'darken': 'darken(c, t)', 'rgb': 'rgb(r, g, b)', 'rgba': 'rgba(r, g, b, a)', 'hsl': 'hsl(h, s, l[, a])',
    'substr': 'substr(s, from, n)', 'contains': 'contains(s, part)', 'replace': 'replace(s, a, b)',
    'pad': 'pad(n, width[, char])', 'fixed': 'fixed(n, digits)', 'plural': "plural(n, 'game')", 'if': 'if(c, a, b)',
    'pick': 'pick(i, a, b, c...)', 'either': 'either(a, b, ...)', 'easeOutBack': 'easeOutBack(t[, c])',
}


class ExprError(Exception):
    pass


def truthy(v):
    if isinstance(v, str):
        return v != '' and v != '0' and v != 'false'
    return v != 0


def num(v):
    if not isinstance(v, str):
        return float(v)
    m = re.match(r'\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?', v)
    return float(m.group(0)) if m else 0.0


def to_str(v):
    if isinstance(v, str):
        return v
    if v != v:
        return ''
    if v == math.floor(v) and abs(v) < 1e15:
        return str(int(v))
    return '%g' % v


# ---- colours ---------------------------------------------------------------------------------------------------------
NAMES = {'black': 0xff000000, 'white': 0xffffffff, 'red': 0xffff0000, 'green': 0xff00c000, 'blue': 0xff0000ff,
         'yellow': 0xffffff00, 'gray': 0xff808080, 'grey': 0xff808080}


def parse_color(v):
    """0xAARRGGBB, or None"""
    v = v.strip()
    if not v:
        return None
    if v in ('transparent', 'none'):
        return 0
    if v in NAMES:
        return NAMES[v]
    if v[0] == '#':
        h = v[1:]
        if not h or not all(c in '0123456789abcdefABCDEF' for c in h):
            return None
        if len(h) in (3, 4):
            h = ''.join(c * 2 for c in h)
        if len(h) == 6:
            h += 'ff'
        if len(h) != 8:
            return None
        rgba = int(h, 16)
        return (rgba >> 8) | (rgba & 0xff) << 24
    m = re.match(r'rgba?\(\s*([-\d.]+)\s*,\s*([-\d.]+)\s*,\s*([-\d.]+)\s*(?:,\s*([-\d.]+)\s*)?\)', v)
    if m:
        c = [float(x) if x is not None else 1.0 for x in m.groups()]
        b = lambda x: int(min(255, max(0, x + 0.5)))
        return b(c[3] * 255) << 24 | b(c[0]) << 16 | b(c[1]) << 8 | b(c[2])
    return None


def color_text(c):
    return '#%06x%02x' % (c & 0xffffff, (c >> 24) & 255)


def color_of(v):
    if isinstance(v, str):
        c = parse_color(v)
        return 0xff000000 if c is None else c
    return 0xff000000 | (int(v) & 0xffffff)


def mix_color(a, b, t):
    t = min(1.0, max(0.0, t))
    r = 0
    for sh in (24, 16, 8, 0):
        x, y = (a >> sh) & 255, (b >> sh) & 255
        r |= int(round(x + (y - x) * t)) << sh
    return r


# ---- parsing ---------------------------------------------------------------------------------------------------------
class _P:
    def __init__(self, s):
        self.s, self.i = s, 0

    def ws(self):
        while self.i < len(self.s) and self.s[self.i].isspace():
            self.i += 1

    def eat(self, t):
        self.ws()
        if self.s.startswith(t, self.i):
            self.i += len(t)
            return True
        return False

    def fail(self, m):
        raise ExprError('%s at %d' % (m, self.i))

    def ternary(self):
        c = self.or_()
        if not self.eat('?'):
            return c
        a = self.ternary()
        if not self.eat(':'):
            self.fail("':' expected")
        b = self.ternary()
        return ('tern', c, a, b)

    def or_(self):
        l = self.and_()
        while self.eat('||'):
            l = ('or', l, self.and_())
        return l

    def and_(self):
        l = self.cmp()
        while self.eat('&&'):
            l = ('and', l, self.cmp())
        return l

    def cmp(self):
        l = self.add()
        while True:
            for t, op in (('==', 'eq'), ('!=', 'ne'), ('<=', 'le'), ('>=', 'ge'), ('<', 'lt'), ('>', 'gt')):
                if self.eat(t):
                    l = (op, l, self.add())
                    break
            else:
                return l

    def add(self):
        l = self.mul()
        while True:
            if self.eat('+'):
                l = ('add', l, self.mul())
            elif self.eat('-'):
                l = ('sub', l, self.mul())
            else:
                return l

    def mul(self):
        l = self.unary()
        while True:
            if self.eat('*'):
                l = ('mul', l, self.unary())
            elif self.eat('/'):
                l = ('div', l, self.unary())
            elif self.eat('%'):
                l = ('mod', l, self.unary())
            else:
                return l

    def unary(self):
        if self.eat('-'):
            return ('neg', self.unary())
        if self.eat('!'):
            return ('not', self.unary())
        if self.eat('+'):
            return self.unary()
        return self.primary()

    def primary(self):
        self.ws()
        s = self.s
        if self.i >= len(s):
            self.fail('a value expected')
        c = s[self.i]
        if c == '(':
            self.i += 1
            e = self.ternary()
            if not self.eat(')'):
                self.fail("')' expected")
            return e
        if c in '\'"':
            v = []
            self.i += 1
            while self.i < len(s) and s[self.i] != c:
                if s[self.i] == '\\' and self.i + 1 < len(s):
                    self.i += 1
                    v.append('\n' if s[self.i] == 'n' else s[self.i])
                else:
                    v.append(s[self.i])
                self.i += 1
            if self.i >= len(s):
                self.fail("a string that doesn't end")
            self.i += 1
            return ('lit', ''.join(v))
        if c == '#':
            b = self.i
            self.i += 1
            while self.i < len(s) and s[self.i] in '0123456789abcdefABCDEF':
                self.i += 1
            return ('lit', s[b:self.i])
        if c.isdigit() or c == '.':
            m = re.match(r'(\d+\.?\d*|\.\d*)([eE][-+]?\d+)?', s[self.i:])
            self.i += len(m.group(0))
            try:
                return ('lit', float(m.group(0)))
            except ValueError:
                return ('lit', 0.0)
        if c.isalpha() or c in '_$':
            b = self.i
            while self.i < len(s) and (s[self.i].isalnum() or s[self.i] in '_.$'):
                self.i += 1
            ident = s[b:self.i]
            if ident in ('true', 'false'):
                return ('lit', 1.0 if ident == 'true' else 0.0)
            if ident == 'null':
                return ('lit', '')
            if self.eat('('):
                args = []
                if not self.eat(')'):
                    while True:
                        args.append(self.ternary())
                        if self.eat(')'):
                            break
                        if not self.eat(','):
                            self.fail("',' or ')' expected")
                return ('call', ident, args)
            return ('var', ident[1:] if ident[0] == '$' else ident)
        self.fail("unexpected '%s'" % c)


_cache = {}


def compile_expr(text):
    """The expression's tree; raises ExprError"""
    t = _cache.get(text)
    if t is not None:
        return t
    p = _P(text)
    e = p.ternary()
    p.ws()
    if p.i < len(text):
        p.fail('more after the end')
    if len(_cache) > 20000:
        _cache.clear()
    _cache[text] = e
    return e


def names_in(tree, out=None):
    """The variable names an expression reads"""
    if out is None:
        out = set()
    if not isinstance(tree, tuple):
        return out
    if tree[0] == 'var':
        out.add(tree[1])
    elif tree[0] == 'call':
        for a in tree[2]:
            names_in(a, out)
    elif tree[0] != 'lit':
        for a in tree[1:]:
            names_in(a, out)
    return out


# ---- evaluation ------------------------------------------------------------------------------------------------------
def _c01(t):
    return min(1.0, max(0.0, t))


def _call(name, args, scope):
    if name == 'if':
        if len(args) < 2:
            return 0.0
        return ev(args[1], scope) if truthy(ev(args[0], scope)) else (ev(args[2], scope) if len(args) > 2 else 0.0)
    v = [ev(a, scope) for a in args]
    N = lambda k, d=0.0: num(v[k]) if k < len(v) else d
    S = lambda k: to_str(v[k]) if k < len(v) else ''
    f = name
    if f == 'min':
        return min([N(k) for k in range(len(v))] or [0.0])
    if f == 'max':
        return max([N(k) for k in range(len(v))] or [0.0])
    if f == 'clamp':
        return min(N(2, 1), max(N(1), N(0)))
    if f == 'abs':
        return abs(N(0))
    if f == 'floor':
        return float(math.floor(N(0)))
    if f == 'ceil':
        return float(math.ceil(N(0)))
    if f == 'round':
        m = 10 ** N(1)
        x = N(0) * m
        return float(math.floor(abs(x) + 0.5) * (1 if x >= 0 else -1)) / m
    if f == 'sqrt':
        return math.sqrt(max(0.0, N(0)))
    if f == 'pow':
        try:
            return float(math.pow(N(0), N(1)))
        except (ValueError, OverflowError):
            return 0.0
    if f == 'sign':
        return float((N(0) > 0) - (N(0) < 0))
    if f == 'mod':
        b = N(1, 1)
        if b == 0:
            return 0.0
        r = math.fmod(N(0), b)
        return r + b if r < 0 else r
    if f == 'sin':
        return math.sin(N(0))
    if f == 'cos':
        return math.cos(N(0))
    if f == 'sind':
        return math.sin(math.radians(N(0)))
    if f == 'cosd':
        return math.cos(math.radians(N(0)))
    if f == 'lerp':
        return N(0) + (N(1) - N(0)) * N(2)
    if f == 'step':
        return 1.0 if N(1) >= N(0) else 0.0
    if f == 'smoothstep':
        d = N(1) - N(0)
        t = _c01((N(2) - N(0)) / (d if d else 1))
        return t * t * (3 - 2 * t)
    if f == 'progress':
        return _c01(N(0) / max(1.0, N(1, 1)))
    if f == 'easeIn':
        t = _c01(N(0))
        return t * t * t
    if f == 'easeOut':
        t = 1 - _c01(N(0))
        return 1 - t * t * t
    if f == 'easeInOut':
        t = _c01(N(0))
        return 4 * t * t * t if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2
    if f == 'easeOutBack':
        t, c = _c01(N(0)) - 1, N(1, 1.70158)
        return 1 + (c + 1) * t * t * t + c * t * t
    if f == 'easeOutElastic':
        t = _c01(N(0))
        return t if t in (0, 1) else 2 ** (-10 * t) * math.sin((t * 10 - 0.75) * (2 * math.pi / 3)) + 1
    if f == 'bounce':
        t, k, d = _c01(N(0)), 7.5625, 2.75
        if t < 1 / d:
            return k * t * t
        if t < 2 / d:
            t -= 1.5 / d
            return k * t * t + 0.75
        if t < 2.5 / d:
            t -= 2.25 / d
            return k * t * t + 0.9375
        t -= 2.625 / d
        return k * t * t + 0.984375
    if f == 'pulse':
        return 0.5 - 0.5 * math.cos(N(0) * 2 * math.pi / max(1.0, N(1, 1000)))
    if f == 'mix':
        return color_text(mix_color(color_of(v[0] if v else ''), color_of(v[1] if len(v) > 1 else ''), N(2, 0.5)))
    if f == 'alpha':
        c = color_of(v[0] if v else '')
        return color_text((c & 0xffffff) | int(round(_c01(N(1, 1)) * ((c >> 24) & 255))) << 24)
    if f == 'lighten':
        c = color_of(v[0] if v else '')
        return color_text(mix_color(c, (c & 0xff000000) | 0xffffff, N(1, 0.2)))
    if f == 'darken':
        c = color_of(v[0] if v else '')
        return color_text(mix_color(c, c & 0xff000000, N(1, 0.2)))
    if f in ('rgb', 'rgba'):
        b = lambda x: int(min(255, max(0, x + 0.5)))
        return color_text(b(N(3, 1) * 255) << 24 | b(N(0)) << 16 | b(N(1)) << 8 | b(N(2)))
    if f == 'hsl':
        h, s, l = math.fmod(N(0), 360) / 60, _c01(N(1)), _c01(N(2))
        c = (1 - abs(2 * l - 1)) * s
        if h < 0:
            h += 6
        x = c * (1 - abs(math.fmod(h, 2) - 1))
        m = l - c / 2
        k = int(h)
        r, g, bb = [(c, x, 0), (x, c, 0), (0, c, x), (0, x, c), (x, 0, c), (c, 0, x)][min(k, 5)]
        b = lambda y: int(min(255, max(0, y * 255 + 0.5)))
        return color_text(b(N(3, 1)) << 24 | b(r + m) << 16 | b(g + m) << 8 | b(bb + m))
    if f == 'upper':
        return S(0).upper()
    if f == 'lower':
        return S(0).lower()
    if f == 'len':
        return float(len(S(0)))
    if f == 'substr':
        s = S(0)
        a = int(N(1))
        n = int(N(2)) if len(v) > 2 else 1 << 30
        return s[max(0, a):max(0, a) + max(0, n)] if a >= 0 else s[:max(0, a + n)]
    if f == 'contains':
        return 1.0 if S(1) in S(0) else 0.0
    if f == 'replace':
        return S(0).replace(S(1), S(2)) if S(1) else S(0)
    if f == 'pad':
        s = S(0)
        ch = S(2) if len(v) > 2 else '0'
        while len(s) < int(N(1)) and ch:
            s = ch + s
        return s
    if f == 'fixed':
        return '%.*f' % (max(0, min(10, int(N(1)))), N(0))
    if f == 'str':
        return S(0)
    if f == 'num':
        return N(0)
    if f == 'plural':
        return to_str(N(0)) + ' ' + (S(1) if N(0) == 1 else (S(2) if len(v) > 2 else S(1) + 's'))
    if f == 'pick':
        if len(v) < 2:
            return ''
        return v[max(0, min(len(v) - 2, int(N(0)))) + 1]
    if f == 'either':
        for e in v:
            if truthy(e):
                return e
        return v[-1] if v else ''
    return 0.0


def ev(t, scope):
    op = t[0]
    if op == 'lit':
        return t[1]
    if op == 'var':
        r = scope.lookup(t[1])
        return '' if r is None else r
    if op == 'neg':
        return -num(ev(t[1], scope))
    if op == 'not':
        return 0.0 if truthy(ev(t[1], scope)) else 1.0
    if op == 'and':
        return 1.0 if truthy(ev(t[1], scope)) and truthy(ev(t[2], scope)) else 0.0
    if op == 'or':
        a = ev(t[1], scope)
        return a if truthy(a) else ev(t[2], scope)
    if op == 'tern':
        return ev(t[2], scope) if truthy(ev(t[1], scope)) else ev(t[3], scope)
    if op == 'call':
        return _call(t[1], t[2], scope)
    a, b = ev(t[1], scope), ev(t[2], scope)
    sa, sb = isinstance(a, str), isinstance(b, str)
    if op == 'add':
        return to_str(a) + to_str(b) if sa or sb else a + b
    if op == 'sub':
        return num(a) - num(b)
    if op == 'mul':
        return num(a) * num(b)
    if op == 'div':
        return 0.0 if num(b) == 0 else num(a) / num(b)
    if op == 'mod':
        d = num(b)
        return 0.0 if d == 0 else math.fmod(num(a), d)
    if op in ('eq', 'ne'):
        same = to_str(a) == to_str(b) if sa or sb else a == b
        return 1.0 if same == (op == 'eq') else 0.0
    x, y = num(a), num(b)
    return 1.0 if {'lt': x < y, 'le': x <= y, 'gt': x > y, 'ge': x >= y}[op] else 0.0


class Scope:
    """Names: this scope's own, then a resolver (hooks), then the parent's"""

    def __init__(self, parent=None, vars=None, resolve=None):
        self.parent, self.vars, self.resolve = parent, dict(vars or {}), resolve

    def lookup(self, name):
        s = self
        while s is not None:
            if name in s.vars:
                return s.vars[name]
            if s.resolve is not None:
                r = s.resolve(name)
                if r is not None:
                    return r
            s = s.parent
        return None


class Prop:
    """A property's value compiled once: kind is 'lit', 'expr' or 'tpl'"""
    __slots__ = ('kind', 'lit', 'tree', 'parts', 'error')

    def __init__(self, value):
        self.error = ''
        self.tree = None
        self.parts = None
        if isinstance(value, bool):
            self.kind, self.lit = 'lit', 1.0 if value else 0.0
        elif isinstance(value, (int, float)):
            self.kind, self.lit = 'lit', float(value)
        elif not isinstance(value, str):
            self.kind, self.lit = 'lit', ''
        elif value.startswith('='):
            self.kind, self.lit = 'expr', ''
            try:
                self.tree = compile_expr(value[1:])
            except ExprError as e:
                self.error = '%s in "%s"' % (e, value[1:])
        elif '{' not in value:
            self.kind, self.lit = 'lit', value
        else:
            self.kind, self.lit = 'tpl', ''
            self.parts = []
            text, i, t = [], 0, value
            while i < len(t):
                if t.startswith('{{', i):
                    text.append('{')
                    i += 2
                    continue
                if t.startswith('}}', i):
                    text.append('}')
                    i += 2
                    continue
                if t[i] != '{':
                    text.append(t[i])
                    i += 1
                    continue
                e = t.find('}', i)
                if e < 0:
                    text.append(t[i:])
                    break
                src = t[i + 1:e]
                try:
                    tree = compile_expr(src)
                except ExprError as x:
                    tree = None
                    if not self.error:
                        self.error = '%s in "%s"' % (x, src)
                self.parts.append((''.join(text), tree))
                text = []
                i = e + 1
            self.parts.append((''.join(text), None))

    def eval(self, scope):
        if self.kind == 'lit':
            return self.lit
        if self.kind == 'expr':
            return ev(self.tree, scope) if self.tree else 0.0
        out = []
        for text, tree in self.parts:
            out.append(text)
            if tree:
                out.append(to_str(ev(tree, scope)))
        return ''.join(out)


def check(value):
    """'' or what is wrong with a property's text"""
    return Prop(value).error if isinstance(value, str) else ''


def evaluate(value, scope):
    return Prop(value).eval(scope)
