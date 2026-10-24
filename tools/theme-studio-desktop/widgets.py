"""The inspector's fields: each property has a visual control for literals and a code box for =expressions and
{templates}, a reset button, and a check of what it says."""
import json

from PySide6.QtCore import QStringListModel, Qt, Signal
from PySide6.QtGui import QColor, QIcon, QPixmap
from PySide6.QtWidgets import (QCheckBox, QColorDialog, QComboBox, QCompleter, QDoubleSpinBox, QFileDialog, QHBoxLayout,
                               QLineEdit, QPushButton, QSizePolicy, QToolButton, QWidget)

import expr as X

ANCHORS = ['', 'center', 'top', 'bottom', 'left', 'right', 'top left', 'top right', 'bottom left', 'bottom right']
BUTTONS = ['', 'a', 'b', 'x', 'y', 'l', 'r', 'left', 'right', 'up', 'down']
FONTS = ['inter', 'inter-medium', 'inter-semibold', 'inter-bold', 'pixel', 'pixel-medium']

# key, label, kind, extra (choices or range), tip
COMMON = [
    ('Position', [
        ('x', 'X', 'len', None, 'From the parent\'s left: units or "50%"'),
        ('y', 'Y', 'len', None, 'From the parent\'s top'),
        ('w', 'Width', 'len', None, 'Empty: fills the parent (text: as wide as its lines)'),
        ('h', 'Height', 'len', None, 'Empty: fills the parent (text: its lines)'),
        ('right', 'Right', 'len', None, 'Distance from the parent\'s right edge'),
        ('bottom', 'Bottom', 'len', None, 'Distance from the parent\'s bottom edge'),
        ('anchor', 'Anchor', 'enum', ANCHORS, 'Which point of the element X and Y place'),
        ('scale', 'Scale', 'num', (0, 10, 0.05), 'Grows or shrinks it about its anchor'),
    ]),
    ('Visibility', [
        ('opacity', 'Opacity', 'num', (0, 1, 0.05), '0..1, multiplied into the children'),
        ('visible', 'Visible', 'bool', None, 'Usually an expression: "=sel", "=battery >= 0"'),
        ('clip', 'Clip children', 'bool', None, 'Children are cut to the box'),
        ('onTap', 'Tap acts as', 'enum', BUTTONS, 'A button this element presses when tapped'),
    ]),
]
SHAPE = [
    ('fill', 'Fill', 'color', None, 'A colour (#rrggbbaa) or an expression'),
    ('gradient', 'Gradient', 'gradient', None, 'Replaces the fill'),
    ('radius', 'Corner radius', 'radius', None, 'A number, "50%" for a pill/circle, or four corners'),
    ('border', 'Border', 'num', (0, 100, 0.5), 'Its width, inside the box'),
    ('borderColor', 'Border colour', 'color', None, ''),
    ('shadow.x', 'Shadow X', 'num', (-200, 200, 1), ''),
    ('shadow.y', 'Shadow Y', 'num', (-200, 200, 1), ''),
    ('shadow.blur', 'Shadow blur', 'num', (0, 200, 1), ''),
    ('shadow.spread', 'Shadow spread', 'num', (-100, 100, 1), ''),
    ('shadow.color', 'Shadow colour', 'color', None, 'A shadow is drawn when this is set'),
]
BY_TYPE = {
    'rect': [('Look', SHAPE)],
    'text': [
        ('Text', [
            ('text', 'Text', 'text', None, '{hooks} in braces: "{game.plays} plays"'),
            ('font', 'Font', 'font', None, 'Built in, or a name from the theme\'s fonts'),
            ('size', 'Size', 'num', (1, 400, 1), ''),
            ('color', 'Colour', 'color', None, ''),
            ('align', 'Align', 'enum', ['', 'left', 'center', 'right'], 'Within the width'),
            ('valign', 'Vertical align', 'enum', ['', 'top', 'middle', 'bottom'], 'Within the height'),
            ('upper', 'Capitals', 'bool', None, ''),
            ('spacing', 'Letter spacing', 'num', (-20, 50, 0.5), ''),
            ('wrap', 'Wrap', 'bool', None, 'Breaks lines at the width'),
            ('maxLines', 'Max lines', 'num', (0, 50, 1), ''),
            ('lineHeight', 'Line height', 'num', (0, 400, 1), ''),
            ('ellipsis', 'Ellipsis', 'bool', None, 'A line longer than the width ends in ... (on by default)'),
        ]),
        ('Effects', [
            ('shadow.x', 'Shadow X', 'num', (-50, 50, 1), ''),
            ('shadow.y', 'Shadow Y', 'num', (-50, 50, 1), ''),
            ('shadow.color', 'Shadow colour', 'color', None, ''),
            ('outline.width', 'Outline width', 'num', (0, 20, 0.5), ''),
            ('outline.color', 'Outline colour', 'color', None, ''),
        ]),
    ],
    'image': [('Picture', [
        ('src', 'Source', 'image', None, 'A file next to the theme, or a hook: {sys.icon}, {game.art}, {item.art}'),
        ('fallback', 'Fallback', 'image', None, 'Used when the source is empty or won\'t load'),
        ('fit', 'Fit', 'enum', ['', 'cover', 'contain', 'fill', 'coverTop', 'pixel', 'pixelContain'], ''),
        ('radius', 'Corner radius', 'radius', None, ''),
        ('tint', 'Tint', 'color', None, 'Multiplied'),
        ('crop', 'Crop', 'enum', ['', 'cardLabel', 'noStrip'], ''),
        ('placeholder', 'Placeholder', 'color', None, 'Shown while it loads or when there is nothing'),
    ])],
    'group': [
        ('Layout', [
            ('layout', 'Layout', 'enum', ['', 'row', 'column'], 'Places the children one after another'),
            ('gap', 'Gap', 'num', (0, 500, 1), ''),
            ('padding', 'Padding', 'num', (0, 500, 1), ''),
            ('justify', 'Justify', 'enum', ['', 'start', 'center', 'end', 'between'], 'Along the row/column'),
            ('align', 'Align', 'enum', ['', 'start', 'center', 'end'], 'Across the row/column'),
        ]),
        ('Box behind', SHAPE),
    ],
    'list': [('List', [
        ('source', 'Source', 'enum', ['systems', 'games', 'hints', 'range'], ''),
        ('direction', 'Direction', 'enum', ['', 'row', 'column', 'grid', 'free'], 'free: each item places itself from pos/rel'),
        ('columns', 'Columns', 'num', (1, 50, 1), 'Grid'),
        ('itemW', 'Item width', 'num', (0, 2000, 1), ''),
        ('itemH', 'Item height', 'num', (0, 2000, 1), ''),
        ('gap', 'Gap', 'num', (0, 500, 1), ''),
        ('stepX', 'Step X', 'num', (-2000, 2000, 1), 'Default: item width + gap'),
        ('stepY', 'Step Y', 'num', (-2000, 2000, 1), ''),
        ('scrollMode', 'Scroll', 'enum', ['', 'center', 'edge', 'none'], ''),
        ('loop', 'Loop', 'bool', None, 'Wraps around'),
        ('flow', 'Flow', 'bool', None, 'Each item as big as its content (hint chips)'),
        ('range', 'Range', 'num', (0, 100, 1), 'A free list draws this many items each side'),
        ('count', 'Count', 'num', (0, 1000, 1), 'source: range'),
        ('selectedOnTop', 'Selected on top', 'bool', None, ''),
    ])],
}

HOOKS = ('view W H screen time clock date battery charging batteryMode online motion brand accent accent.light accent.dark '
         'accent.band accent.low cursor count scroll moved entered dir launch.progress launch.t empty empty.title empty.hint '
         'systems.count games.count i n sel rel pos first last ix iy item.key item.label').split()
SYS = 'id name maker color icon games played time fav app hasResume resumeTitle resumeEyebrow resumeArt resumeBox resumeCart resumeLabel index'.split()
GAME = 'title sub plays last time art box cart label icon rom romIcon achA achB achLabel achProgress fav index systemId systemName'.split()


def completions(vars_=()):
    words = list(HOOKS) + ['sys.' + s for s in SYS] + ['game.' + g for g in GAME] + ['item.' + s for s in SYS + GAME]
    words += [f + '(' for f in X.FUNCTION_NAMES] + list(vars_)
    return sorted(set(words), key=str.lower)


def get_path(d, key):
    """d's value at a dotted key ("shadow.x"), or None"""
    for part in key.split('.'):
        if isinstance(d, dict) and part in d:
            d = d[part]
        else:
            return None
    return d


def set_path(d, key, value):
    """Sets or (value None) removes a dotted key; empty objects left behind go too"""
    parts = key.split('.')
    chain = [d]
    for part in parts[:-1]:
        nxt = chain[-1].get(part)
        if not isinstance(nxt, dict):
            if value is None:
                return
            nxt = {}
            chain[-1][part] = nxt
        chain.append(nxt)
    if value is None:
        chain[-1].pop(parts[-1], None)
        for i in range(len(parts) - 1, 0, -1):
            if not chain[i]:
                chain[i - 1].pop(parts[i - 1], None)
    else:
        chain[-1][parts[-1]] = value


def is_code(v):
    return isinstance(v, str) and (v.startswith('=') or '{' in v)


class CodeEdit(QLineEdit):
    """A line for expressions: completion of hooks, vars and functions, red when it doesn't parse"""

    def __init__(self, words, parent=None):
        super().__init__(parent)
        self.model = QStringListModel(words)
        self.comp = QCompleter(self.model, self)
        self.comp.setCaseSensitivity(Qt.CaseInsensitive)
        self.comp.setWidget(self)
        self.comp.activated.connect(self.insert_completion)
        self.textEdited.connect(self.on_edit)
        self.textChanged.connect(self.validate)

    def word_at_cursor(self):
        t, i = self.text(), self.cursorPosition()
        j = i
        while j > 0 and (t[j - 1].isalnum() or t[j - 1] in '._$'):
            j -= 1
        return t[j:i]

    def on_edit(self, _t):
        w = self.word_at_cursor()
        if len(w) >= 1 and (self.text().startswith('=') or '{' in self.text()):
            self.comp.setCompletionPrefix(w)
            if self.comp.completionCount():
                r = self.cursorRect()
                r.setWidth(260)
                self.comp.complete(r)
                return
        self.comp.popup().hide()

    def insert_completion(self, s):
        w = self.word_at_cursor()
        i = self.cursorPosition()
        t = self.text()
        self.setText(t[:i - len(w)] + s + t[i:])
        self.setCursorPosition(i - len(w) + len(s))

    def validate(self):
        err = X.check(self.text())
        self.setStyleSheet('QLineEdit { border: 1px solid #e05050; }' if err else '')
        self.setToolTip(err)


def swatch(c, size=14):
    pm = QPixmap(size, size)
    pm.fill(Qt.transparent)
    from PySide6.QtGui import QPainter
    p = QPainter(pm)
    for y in range(0, size, 4):
        for x in range(0, size, 4):
            p.fillRect(x, y, 4, 4, QColor('#ccc') if (x + y) // 4 % 2 else QColor('#888'))
    if c is not None:
        p.fillRect(0, 0, size, size, QColor((c >> 16) & 255, (c >> 8) & 255, c & 255, (c >> 24) & 255))
    p.end()
    return QIcon(pm)


class PropField(QWidget):
    """One property. changed(key, value) with value None to remove it."""
    changed = Signal(str, object)

    def __init__(self, key, kind, extra, words, fonts=(), browse=None, parent=None):
        super().__init__(parent)
        self.key, self.kind, self.extra, self.browse = key, kind, extra, browse
        self.value = None
        self.code_mode = False
        self.lay = QHBoxLayout(self)
        self.lay.setContentsMargins(0, 0, 0, 0)
        self.lay.setSpacing(3)
        self.code = CodeEdit(words)
        self.code.editingFinished.connect(self.from_code)
        self.lay.addWidget(self.code)
        self.visual = self.make_visual(fonts)
        if self.visual is not None:
            self.lay.addWidget(self.visual)
        if kind in ('color', 'image'):
            self.extra_btn = QToolButton()
            self.extra_btn.setText('…')
            self.extra_btn.clicked.connect(self.pick)
            self.lay.addWidget(self.extra_btn)
        self.fx = QToolButton()
        self.fx.setText('ƒ')
        self.fx.setCheckable(True)
        self.fx.setToolTip('Write it as an expression (=...) instead of a value')
        self.fx.toggled.connect(self.toggle_code)
        self.lay.addWidget(self.fx)
        self.clear = QToolButton()
        self.clear.setText('×')
        self.clear.setToolTip('Remove (back to the default)')
        self.clear.clicked.connect(lambda: self.emit(None))
        self.lay.addWidget(self.clear)
        if kind in ('text', 'image', 'gradient', 'radius', 'color', 'len'):
            self.fx.hide() if kind in ('text', 'image') else None

    def make_visual(self, fonts):
        k = self.kind
        if k == 'num':
            lo, hi, step = self.extra
            w = QDoubleSpinBox()
            # one step below the range means "not set": the engine's default
            self.unset = lo - step
            w.setRange(self.unset, hi)
            w.setSpecialValueText('(default)')
            w.setSingleStep(step)
            w.setDecimals(2 if step < 1 else 0)
            w.setKeyboardTracking(False)
            w.valueChanged.connect(lambda v: self.emit(None if v <= self.unset + 1e-9 else int(v) if float(v).is_integer() else round(v, 3)))
            return w
        if k == 'bool':
            w = QComboBox()
            w.addItems(['(default)', 'yes', 'no'])
            w.activated.connect(lambda i: self.emit(None if i == 0 else i == 1))
            return w
        if k in ('enum', 'font'):
            w = QComboBox()
            w.setEditable(True)
            items = list(self.extra) if k == 'enum' else [''] + list(fonts)
            w.addItems(items)
            w.lineEdit().editingFinished.connect(lambda: self.emit(w.currentText() or None))
            w.activated.connect(lambda i: self.emit(w.currentText() or None))
            return w
        if k == 'color':
            w = QToolButton()
            w.setToolTip('Pick a colour')
            w.clicked.connect(self.pick)
            self.code.setPlaceholderText('#rrggbb, or =expression')
            return w
        if k in ('len', 'radius', 'text', 'image', 'gradient'):
            self.code.setPlaceholderText({'len': 'number or 50%', 'radius': 'number, 50%, or tl,tr,br,bl',
                                          'gradient': 'angle; colour, colour 40%, ...'}.get(k, ''))
            return None
        return None

    def set_value(self, v):
        self.value = v
        widgets = [self.code] + ([self.visual] if self.visual else [])
        for w in widgets:
            w.blockSignals(True)
        k = self.kind
        code = is_code(v) and k not in ('text', 'image')
        if isinstance(v, (dict, list)) and k not in ('gradient', 'radius'):
            code = True
        text = ''
        if k == 'gradient':
            text = self.gradient_text(v)
            code = isinstance(v, str)
        elif k == 'radius' and isinstance(v, list):
            text = ', '.join(str(x) for x in v)
        elif isinstance(v, (dict, list)):
            text = json.dumps(v)
        elif isinstance(v, bool):
            text = 'true' if v else 'false'
        elif isinstance(v, float) and v.is_integer():
            text = str(int(v))
        elif v is not None:
            text = str(v)
        self.code.setText(text)
        if self.visual is not None:
            if k == 'num' and isinstance(v, (int, float)) and not isinstance(v, bool):
                self.visual.setValue(v)
            elif k == 'num' and v is None:
                self.visual.setValue(self.unset)
            elif k == 'bool':
                self.visual.setCurrentIndex(0 if v is None else 1 if X.truthy(X.Prop(v).lit if not is_code(v) else 1) else 2)
            elif k in ('enum', 'font'):
                self.visual.setCurrentText('' if v is None else str(v))
            elif k == 'color':
                c = X.parse_color(v) if isinstance(v, str) and not is_code(v) else None
                self.visual.setIcon(swatch(c))
        self.code_mode = code
        self.fx.blockSignals(True)
        self.fx.setChecked(code)
        self.fx.blockSignals(False)
        self.apply_mode()
        for w in widgets:
            w.blockSignals(False)
        self.clear.setEnabled(v is not None)

    def apply_mode(self):
        k = self.kind
        show_code = self.code_mode or k in ('len', 'radius', 'text', 'image', 'gradient', 'color')
        self.code.setVisible(show_code)
        if self.visual is not None:
            self.visual.setVisible(not self.code_mode or k == 'color')
            if k == 'color':
                self.visual.setEnabled(not self.code_mode)
        self.code.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)

    def toggle_code(self, on):
        self.code_mode = on
        if on and not is_code(self.code.text()):
            cur = self.code.text()
            if self.kind in ('color', 'enum', 'font'):
                self.code.setText("='%s'" % cur if cur else '=')
            else:
                self.code.setText('=' + (cur or ''))
            self.code.setFocus()
            self.code.setCursorPosition(len(self.code.text()))
        self.apply_mode()

    @staticmethod
    def gradient_text(v):
        if not isinstance(v, dict):
            return '' if v is None else str(v)
        stops = v.get('stops', [])
        return '%s; %s' % (v.get('angle', 180), ', '.join(str(s) for s in stops) if isinstance(stops, list) else '')

    def parse_text(self, t):
        t = t.strip()
        k = self.kind
        if t == '':
            return None
        if k == 'gradient':
            if ';' in t:
                a, rest = t.split(';', 1)
            else:
                a, rest = '180', t
            stops, depth, cur = [], 0, ''
            for ch in rest:      # commas inside rgba(...) or expressions stay in the stop
                if ch == '(':
                    depth += 1
                elif ch == ')':
                    depth -= 1
                if ch == ',' and depth == 0:
                    stops.append(cur.strip())
                    cur = ''
                else:
                    cur += ch
            if cur.strip():
                stops.append(cur.strip())
            try:
                ang = float(a)
                ang = int(ang) if ang.is_integer() else ang
            except ValueError:
                ang = a.strip()
            return {'angle': ang, 'stops': stops}
        if t.startswith('=') or k in ('text', 'image'):
            return t
        if k == 'radius' and ',' in t and not t.startswith('='):
            parts = [p.strip() for p in t.split(',')]
            return [_num(p) for p in parts]
        if t.startswith('{') or t.startswith('['):
            try:
                return json.loads(t)
            except ValueError:
                return t
        if k in ('num', 'len', 'radius'):
            return _num(t)
        if k == 'bool':
            return t.lower() in ('1', 'true', 'yes', 'on')
        return t

    def from_code(self):
        v = self.parse_text(self.code.text())
        if v != self.value:
            self.emit(v)

    def emit(self, v):
        self.value = v
        self.changed.emit(self.key, v)

    def pick(self):
        if self.kind == 'color':
            cur = X.parse_color(self.value) if isinstance(self.value, str) and not is_code(self.value) else 0xffffffff
            c = QColorDialog.getColor(QColor((cur >> 16) & 255, (cur >> 8) & 255, cur & 255, (cur >> 24) & 255), self,
                                      'Colour', QColorDialog.ShowAlphaChannel)
            if c.isValid():
                self.emit('#%02x%02x%02x' % (c.red(), c.green(), c.blue()) + ('%02x' % c.alpha() if c.alpha() < 255 else ''))
        elif self.kind == 'image' and self.browse:
            r = self.browse()
            if r:
                self.emit(r)


def _num(t):
    t = t.strip()
    if t.endswith('%'):
        return t
    try:
        f = float(t)
        return int(f) if f.is_integer() else f
    except ValueError:
        return t


class RawField(QLineEdit):
    """Any property the forms don't know: its JSON on one line"""
    changed = Signal(str, object)

    def __init__(self, key, value, parent=None):
        super().__init__(parent)
        self.key = key
        self.setText(json.dumps(value, ensure_ascii=False))
        self.editingFinished.connect(self.done)

    def done(self):
        t = self.text().strip()
        if not t:
            self.changed.emit(self.key, None)
            return
        try:
            v = json.loads(t)
        except ValueError:
            v = t
        self.changed.emit(self.key, v)
