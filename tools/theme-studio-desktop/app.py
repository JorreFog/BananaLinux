"""Banana Theme Studio: the main window."""
import copy
import json
import os
import shutil
import sys
import zipfile

from PySide6.QtCore import QMimeData, QPointF, QRectF, QSettings, QSize, Qt, QTimer
from PySide6.QtGui import (QAction, QColor, QDrag, QFont, QFontDatabase, QImage, QKeySequence, QPainter, QPen,
                           QPixmap)
from PySide6.QtWidgets import (QAbstractItemView, QApplication, QCheckBox, QComboBox, QDockWidget, QDoubleSpinBox,
                               QFileDialog, QFormLayout, QFrame, QGridLayout, QGroupBox, QHBoxLayout, QHeaderView,
                               QInputDialog, QLabel, QLineEdit, QListWidget, QListWidgetItem, QMainWindow, QMenu,
                               QMessageBox, QPlainTextEdit, QPushButton, QScrollArea, QSlider, QSpinBox, QTabWidget,
                               QTableWidget, QTableWidgetItem, QToolBox, QToolButton, QTreeWidget, QTreeWidgetItem,
                               QVBoxLayout, QWidget)

import engine as E
import exact
import expr as X
import jsonc
import presets
import style
import widgets as Wd

MIME = 'application/x-banana-element'
TYPE_MARK = {'rect': '▭', 'text': 'T', 'image': '▣', 'group': '▤', 'list': '☰'}
ACTIONS = ['', 'prev', 'next', 'prev:5', 'next:5', 'prev:10', 'next:10', 'first', 'last', 'open', 'back', 'fav', 'resume',
           'prevSystem', 'nextSystem', 'none']
BUTTON_NAMES = ['left', 'right', 'up', 'down', 'a', 'b', 'x', 'y', 'l', 'r']


# ---- the document's tree -----------------------------------------------------------------------------------------------
def get(doc, path):
    d = doc
    for k in path:
        if isinstance(d, dict) and k in d:
            d = d[k]
        elif isinstance(d, list) and isinstance(k, int) and 0 <= k < len(d):
            d = d[k]
        else:
            return None
    return d


def find_path(doc, obj):
    """Where an object (by identity) is in the document"""
    stack = [((), doc)]
    while stack:
        p, d = stack.pop()
        if d is obj:
            return p
        if isinstance(d, dict):
            for k, v in d.items():
                if isinstance(v, (dict, list)):
                    stack.append((p + (k,), v))
        elif isinstance(d, list):
            for i, v in enumerate(d):
                if isinstance(v, (dict, list)):
                    stack.append((p + (i,), v))
    return None


def summary(el):
    if isinstance(el, list):
        return 'elements (%d)' % len(el)
    if not isinstance(el, dict):
        return '?'
    if 'id' in el:
        return str(el['id'])
    if 'use' in el:
        return 'use: %s' % el['use']
    t = E.element_type(el)
    for k in ('text', 'src', 'source'):
        if isinstance(el.get(k), str):
            s = el[k]
            return '%s  “%s”' % (t, s if len(s) < 28 else s[:26] + '…')
    if 'layout' in el:
        return '%s (%s)' % (t, el['layout'])
    for k in ('fill', 'gradient'):
        if k in el:
            return '%s  %s' % (t, k if k == 'gradient' else el[k])
    return t


# ---- the canvas: both screens, selection, dragging -------------------------------------------------------------------
class Canvas(QWidget):
    GAP = 18
    MARGIN = 24

    def __init__(self, studio):
        super().__init__()
        self.s = studio
        self.zoom = 1.0
        self.hits = [[], []]
        self.drag = None
        self.guides = []
        self.engine_img = [None, None]
        self.mode = 'studio'
        self.outline = False
        self.cycle = []
        self.setAcceptDrops(True)
        self.setFocusPolicy(Qt.StrongFocus)
        self.setMouseTracking(True)
        self.update_size()

    def update_size(self):
        self.setFixedSize(int(E.W * self.zoom + 2 * self.MARGIN), int(2 * E.H * self.zoom + self.GAP + 2 * self.MARGIN))

    def origin(self, screen):
        return QPointF(self.MARGIN, self.MARGIN + screen * (E.H * self.zoom + self.GAP))

    def to_layout(self, pos):
        """(screen, layout point) for a widget point"""
        for screen in (0, 1):
            o = self.origin(screen)
            x, y = (pos.x() - o.x()) / self.zoom, (pos.y() - o.y()) / self.zoom
            if -40 <= x <= E.W + 40 and -40 <= y <= E.H + 40 and (screen == 1 or y <= E.H + self.GAP / 2 / self.zoom):
                return screen, QPointF(x, y)
        return None, None

    def paintEvent(self, _e):
        p = QPainter(self)
        p.fillRect(self.rect(), QColor('#1e1f24'))
        for screen in (0, 1):
            o = self.origin(screen)
            p.save()
            p.translate(o)
            p.scale(self.zoom, self.zoom)
            hits = self.s.renderer.render(p, screen, self.outline and self.mode == 'studio')
            self.hits[screen] = hits
            if self.mode == 'engine' and self.engine_img[screen] is not None:
                p.setRenderHint(QPainter.SmoothPixmapTransform, self.zoom < 1)
                p.drawImage(QRectF(0, 0, E.W, E.H), self.engine_img[screen])
            p.restore()
            p.setPen(QColor('#55565e'))
            p.drawRect(QRectF(o.x() - 1, o.y() - 1, E.W * self.zoom + 1, E.H * self.zoom + 1))
            p.setPen(QColor('#8a8b94'))
            p.drawText(QPointF(o.x(), o.y() - 6), ('Top screen' if screen == 0 else 'Bottom screen') +
                       ('   (engine)' if self.mode == 'engine' and self.engine_img[screen] is not None else ''))
        self.paint_selection(p)
        p.end()

    def screen_rect(self, screen, r):
        o = self.origin(screen)
        return QRectF(o.x() + r.x() * self.zoom, o.y() + r.y() * self.zoom, r.width() * self.zoom, r.height() * self.zoom)

    def selected_hits(self):
        sel = self.s.sel
        if sel is None:
            return []
        return [h for sc in (0, 1) for h in self.hits[sc] if h.path == sel]

    def primary(self):
        hs = self.selected_hits()
        if not hs:
            return None
        pref = [h for h in hs if h.screen == self.s.active_screen]
        return (pref or hs)[0]

    def paint_selection(self, p):
        hs = self.selected_hits()
        prim = self.primary()
        for h in hs:
            r = self.screen_rect(h.screen, h.rect)
            pen = QPen(QColor('#3da5ff'))
            pen.setWidthF(2 if h is prim else 1)
            if h is not prim:
                pen.setStyle(Qt.DashLine)
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawRect(r)
        if prim is not None:
            for hr in self.handles(prim).values():
                p.fillRect(hr, QColor('#ffffff'))
                p.setPen(QColor('#3da5ff'))
                p.drawRect(hr)
            r = self.screen_rect(prim.screen, prim.rect)
            label = '%s  %.0f×%.0f' % (prim.type, prim.rect.width() / max(prim.k, 1e-6), prim.rect.height() / max(prim.k, 1e-6))
            p.setPen(QColor('#3da5ff'))
            p.drawText(QPointF(r.x(), r.bottom() + 14), label)
        pen = QPen(QColor('#ff4fd8'))
        pen.setWidthF(1)
        p.setPen(pen)
        for screen, axis, v in self.guides:
            o = self.origin(screen)
            if axis == 'x':
                x = o.x() + v * self.zoom
                p.drawLine(QPointF(x, o.y()), QPointF(x, o.y() + E.H * self.zoom))
            else:
                y = o.y() + v * self.zoom
                p.drawLine(QPointF(o.x(), y), QPointF(o.x() + E.W * self.zoom, y))

    def handles(self, h):
        r = self.screen_rect(h.screen, h.rect)
        s = 7
        return {'r': QRectF(r.right() - s / 2, r.center().y() - s / 2, s, s),
                'b': QRectF(r.center().x() - s / 2, r.bottom() - s / 2, s, s),
                'br': QRectF(r.right() - s / 2, r.bottom() - s / 2, s, s)}

    def hits_at(self, screen, pt):
        return sorted([h for h in self.hits[screen] if h.rect.contains(pt) or
                       (h.rect.width() < 4 and h.rect.adjusted(-3, -3, 3, 3).contains(pt))], key=lambda h: -h.order)

    # ---- mouse ---------------------------------------------------------------------------------------------------------
    def mousePressEvent(self, e):
        self.setFocus()
        pos = e.position()
        if e.button() == Qt.RightButton:
            screen, pt = self.to_layout(pos)
            if screen is not None:
                hs = self.hits_at(screen, pt)
                if hs and not any(h.path == self.s.sel for h in hs):
                    self.s.select(hs[0].path, screen)
                self.s.context_menu(e.globalPosition().toPoint())
            return
        if e.button() != Qt.LeftButton:
            return
        prim = self.primary()
        if prim is not None:
            for name, hr in self.handles(prim).items():
                if hr.adjusted(-3, -3, 3, 3).contains(pos):
                    self.start_drag('resize:' + name, prim, pos)
                    return
        screen, pt = self.to_layout(pos)
        if screen is None:
            self.s.select(None)
            return
        self.s.active_screen = screen
        hs = self.hits_at(screen, pt)
        paths = []
        for h in hs:
            if h.path not in paths:
                paths.append(h.path)
        if not paths:
            self.s.select(None)
            return
        if e.modifiers() & Qt.AltModifier and self.s.sel in paths:
            nxt = paths[(paths.index(self.s.sel) + 1) % len(paths)]
        elif self.s.sel in paths and not (e.modifiers() & Qt.AltModifier):
            nxt = self.s.sel      # pressing on the selection keeps it (to drag it)
        else:
            nxt = paths[0]
        self.s.select(nxt, screen)
        h = next((h for h in hs if h.path == nxt), None)
        if h is not None:
            self.start_drag('move', h, pos)

    def start_drag(self, kind, hit, pos):
        el = get(self.s.doc, hit.path)
        if not isinstance(el, dict):
            self.drag = None
            return
        self.drag = {'kind': kind, 'hit': hit, 'start': pos, 'el': el, 'orig': copy.deepcopy(el),
                     'snapshot': self.s.snapshot(), 'moved': False}

    def mouseMoveEvent(self, e):
        pos = e.position()
        screen, pt = self.to_layout(pos)
        if screen is not None:
            self.s.statusBar().showMessage('%s  x %.0f  y %.0f' % ('top' if screen == 0 else 'bottom', pt.x(), pt.y()), 1500)
        if not self.drag or not (e.buttons() & Qt.LeftButton):
            prim = self.primary()
            cur = Qt.ArrowCursor
            if prim is not None:
                for name, hr in self.handles(prim).items():
                    if hr.adjusted(-3, -3, 3, 3).contains(pos):
                        cur = {'r': Qt.SizeHorCursor, 'b': Qt.SizeVerCursor, 'br': Qt.SizeFDiagCursor}[name]
            self.setCursor(cur)
            return
        d = self.drag
        delta = (pos - d['start']) / self.zoom
        if not d['moved'] and abs(delta.x()) + abs(delta.y()) < 2 / self.zoom:
            return
        d['moved'] = True
        hit, el, orig = d['hit'], d['el'], d['orig']
        k = hit.k or 1.0
        dx, dy = delta.x(), delta.y()
        self.guides = []
        if d['kind'] == 'move':
            if not (e.modifiers() & Qt.ShiftModifier):
                dx, dy = self.snap(hit, dx, dy)
            ok = self.apply_axis(el, orig, 'x', 'right', dx / k, hit.rect.x() / k) & \
                self.apply_axis(el, orig, 'y', 'bottom', dy / k, hit.rect.y() / k)
            if not ok:
                self.s.statusBar().showMessage('Part of its position is an expression or a percentage: change that in the inspector.', 3000)
        else:
            name = d['kind'].split(':')[1]
            if 'r' in name:
                self.apply_size(el, orig, 'w', dx / k, hit.rect.width() / k)
            if 'b' in name:
                self.apply_size(el, orig, 'h', dy / k, hit.rect.height() / k)
        self.s.live_change()

    def round_v(self, v):
        g = self.s.grid
        return int(round(v / g) * g) if g > 1 else int(round(v))

    def apply_axis(self, el, orig, k1, k2, delta, cur_abs):
        v1, v2 = orig.get(k1), orig.get(k2)
        num = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool)
        if v1 is None and num(v2):
            el[k2] = self.round_v(v2 - delta)
            return True
        if v1 is None and v2 is None:
            if abs(delta) < 0.5:
                return True
            el[k1] = self.round_v(delta)
            return True
        if num(v1):
            el[k1] = self.round_v(v1 + delta)
            return True
        return abs(delta) < 0.5

    def apply_size(self, el, orig, key, delta, cur):
        v = orig.get(key)
        if v is None:
            el[key] = self.round_v(max(1, cur + delta))
        elif isinstance(v, (int, float)) and not isinstance(v, bool):
            el[key] = self.round_v(max(1, v + delta))
        else:
            self.s.statusBar().showMessage('%s is "%s": change it in the inspector.' % (key, v), 3000)

    def snap(self, hit, dx, dy):
        """Snaps the moved box's edges and centre to the screen's and other elements' (5 screen px)"""
        th = 5 / self.zoom
        r = hit.rect.translated(dx, dy)
        xs = [0, E.W / 2, E.W]
        ys = [0, E.H / 2, E.H]
        for h in self.hits[hit.screen]:
            if h.path == hit.path or h.rect.width() >= E.W - 1 and h.rect.height() >= E.H - 1:
                continue
            xs += [h.rect.left(), h.rect.center().x(), h.rect.right()]
            ys += [h.rect.top(), h.rect.center().y(), h.rect.bottom()]
        best = None
        for edge in (r.left(), r.center().x(), r.right()):
            for gx in xs:
                if abs(gx - edge) < th and (best is None or abs(gx - edge) < abs(best[0])):
                    best = (gx - edge, gx)
        if best:
            dx += best[0]
            self.guides.append((hit.screen, 'x', best[1]))
        best = None
        for edge in (r.top(), r.center().y(), r.bottom()):
            for gy in ys:
                if abs(gy - edge) < th and (best is None or abs(gy - edge) < abs(best[0])):
                    best = (gy - edge, gy)
        if best:
            dy += best[0]
            self.guides.append((hit.screen, 'y', best[1]))
        return dx, dy

    def mouseReleaseEvent(self, e):
        d, self.drag = self.drag, None
        self.guides = []
        if d and d['moved']:
            self.s.commit(before=d['snapshot'], structure=False)
        self.update()

    def mouseDoubleClickEvent(self, e):
        el = get(self.s.doc, self.s.sel) if self.s.sel else None
        if isinstance(el, dict) and E.element_type(el) == 'text':
            t, ok = QInputDialog.getText(self, 'Text', 'Text ({hooks} in braces):', text=str(el.get('text', '')))
            if ok:
                before = self.s.snapshot()
                el['text'] = t
                self.s.commit(before=before)

    def wheelEvent(self, e):
        if e.modifiers() & Qt.ControlModifier:
            self.s.set_zoom(self.zoom * (1.1 if e.angleDelta().y() > 0 else 1 / 1.1))
            e.accept()
        else:
            e.ignore()

    def keyPressEvent(self, e):
        el = get(self.s.doc, self.s.sel) if self.s.sel else None
        step = 10 if e.modifiers() & Qt.ShiftModifier else 1
        moves = {Qt.Key_Left: (-step, 0), Qt.Key_Right: (step, 0), Qt.Key_Up: (0, -step), Qt.Key_Down: (0, step)}
        if isinstance(el, dict) and e.key() in moves:
            dx, dy = moves[e.key()]
            before = self.s.snapshot()
            orig = copy.deepcopy(el)
            g, self.s.grid = self.s.grid, 1
            ok = self.apply_axis(el, orig, 'x', 'right', dx, 0) & self.apply_axis(el, orig, 'y', 'bottom', dy, 0)
            self.s.grid = g
            if ok:
                self.s.commit(before=before, structure=False)
            else:
                self.s.statusBar().showMessage('Its position is an expression or a percentage.', 3000)
            return
        if e.key() == Qt.Key_Escape:
            self.s.select(None)
            return
        super().keyPressEvent(e)

    # ---- dropping from the palette -------------------------------------------------------------------------------------
    def dragEnterEvent(self, e):
        if e.mimeData().hasFormat(MIME):
            e.acceptProposedAction()

    def dragMoveEvent(self, e):
        if e.mimeData().hasFormat(MIME):
            e.acceptProposedAction()

    def dropEvent(self, e):
        screen, pt = self.to_layout(e.position())
        if screen is None:
            return
        el = json.loads(bytes(e.mimeData().data(MIME)).decode())
        self.s.drop_element(el, screen, pt)
        e.acceptProposedAction()


class Palette(QToolBox):
    """The ready-made elements: drag one onto a screen, or double-click to add it"""

    def __init__(self, studio):
        super().__init__()
        self.s = studio
        groups = {}
        for group, label, tip, el in presets.PALETTE:
            if group not in groups:
                lw = QListWidget()
                lw.setDragEnabled(True)
                lw.setDragDropMode(QAbstractItemView.DragOnly)
                lw.startDrag = lambda actions, lw=lw: self.start_drag(lw)
                lw.itemDoubleClicked.connect(lambda it: self.s.add_element(copy.deepcopy(it.data(Qt.UserRole))))
                groups[group] = lw
                self.addItem(lw, group)
            it = QListWidgetItem('%s  %s' % (TYPE_MARK.get(el['type'], '·'), label))
            it.setToolTip(tip + '\nDrag onto a screen, or double-click to add.')
            it.setData(Qt.UserRole, el)
            groups[group].addItem(it)

    def start_drag(self, lw):
        it = lw.currentItem()
        if not it:
            return
        md = QMimeData()
        md.setData(MIME, json.dumps(it.data(Qt.UserRole)).encode())
        d = QDrag(lw)
        d.setMimeData(md)
        d.exec(Qt.CopyAction)


class LayerTree(QTreeWidget):
    """The element trees of the view's screens; drag to reorder (later in the list = drawn on top)"""

    def __init__(self, studio):
        super().__init__()
        self.s = studio
        self.setHeaderHidden(True)
        self.setDragDropMode(QAbstractItemView.InternalMove)
        self.setSelectionMode(QAbstractItemView.SingleSelection)
        self.itemSelectionChanged.connect(self.on_select)
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.customContextMenuRequested.connect(lambda p: self.s.context_menu(self.viewport().mapToGlobal(p)))
        self.updating = False

    def rebuild(self):
        self.updating = True
        self.clear()
        doc = self.s.doc
        view = self.s.renderer.sim.view
        screens = doc.get('screens') if isinstance(doc.get('screens'), dict) else {}
        for side, label in (('top', 'Top screen'), ('bottom', 'Bottom screen')):
            src = view
            for v in (view, 'library' if view == 'launch' else 'home', 'home'):
                if isinstance(screens.get(v), dict) and screens[v].get(side):
                    src = v
                    break
            root = QTreeWidgetItem([label + ('' if src == view else '  (from %s)' % src)])
            root.setData(0, Qt.UserRole, ('screens', src, side))
            root.setData(0, Qt.UserRole + 1, 'root')
            f = root.font(0)
            f.setBold(True)
            root.setFont(0, f)
            self.addTopLevelItem(root)
            self.add_children(root, get(doc, ('screens', src, side)), ('screens', src, side))
            root.setExpanded(True)
        for side in ('top', 'bottom'):
            v = get(doc, ('screens', 'all', side))
            if v:
                root = QTreeWidgetItem(['Every view · %s' % side])
                root.setData(0, Qt.UserRole, ('screens', 'all', side))
                root.setData(0, Qt.UserRole + 1, 'root')
                self.addTopLevelItem(root)
                self.add_children(root, v, ('screens', 'all', side))
                root.setExpanded(True)
        self.updating = False
        self.sync_selection()

    def add_children(self, parent, v, path):
        if isinstance(v, list):
            for i, el in enumerate(v):
                self.add_item(parent, el, path + (i,))
        elif isinstance(v, dict):
            self.add_item(parent, v, path)

    def add_item(self, parent, el, path):
        t = E.element_type(el) if isinstance(el, dict) else 'group'
        it = QTreeWidgetItem(['%s  %s' % (TYPE_MARK.get(t, '·'), summary(el))])
        it.setData(0, Qt.UserRole, path)
        it.setData(0, Qt.UserRole + 1, t)
        parent.addChild(it)
        if isinstance(el, list):
            self.add_children(it, el, path)
        elif isinstance(el, dict):
            if 'children' in el:
                self.add_children(it, el['children'], path + ('children',))
            if 'item' in el:
                sub = QTreeWidgetItem(['↻  item template'])
                sub.setData(0, Qt.UserRole, path + ('item',))
                sub.setData(0, Qt.UserRole + 1, E.element_type(el['item']) if isinstance(el['item'], dict) else 'group')
                it.addChild(sub)
                if isinstance(el['item'], dict) and 'children' in el['item']:
                    self.add_children(sub, el['item']['children'], path + ('item', 'children'))
        it.setExpanded(len(path) < 6)

    def sync_selection(self):
        self.updating = True
        self.clearSelection()
        sel = self.s.sel
        if sel is not None:
            stack = [self.topLevelItem(i) for i in range(self.topLevelItemCount())]
            while stack:
                it = stack.pop()
                if it.data(0, Qt.UserRole) == sel and it.data(0, Qt.UserRole + 1) != 'root':
                    it.setSelected(True)
                    self.scrollToItem(it)
                    break
                stack += [it.child(i) for i in range(it.childCount())]
        self.updating = False

    def on_select(self):
        if self.updating:
            return
        items = self.selectedItems()
        if not items or items[0].data(0, Qt.UserRole + 1) == 'root':
            if items:
                p = items[0].data(0, Qt.UserRole)
                self.s.active_screen = 0 if p[2] == 'top' else 1
            return
        path = tuple(items[0].data(0, Qt.UserRole))
        root = items[0]
        while root.parent():
            root = root.parent()
        side = root.data(0, Qt.UserRole)[2]
        self.s.select(path, 0 if side == 'top' else 1, from_tree=True)

    def dropEvent(self, e):
        src = self.currentItem()
        target = self.itemAt(e.position().toPoint())
        if not src or not target or src.data(0, Qt.UserRole + 1) == 'root':
            e.ignore()
            return
        pos = self.dropIndicatorPosition()
        mode = {QAbstractItemView.AboveItem: 'above', QAbstractItemView.BelowItem: 'below',
                QAbstractItemView.OnItem: 'on'}.get(pos, 'on')
        e.ignore()      # the tree is rebuilt from the document
        QTimer.singleShot(0, lambda: self.s.move_element(tuple(src.data(0, Qt.UserRole)), tuple(target.data(0, Qt.UserRole)),
                                                         mode, target.data(0, Qt.UserRole + 1)))


class Inspector(QScrollArea):
    def __init__(self, studio):
        super().__init__()
        self.s = studio
        self.setWidgetResizable(True)
        self.fields = []
        self.path = None
        self.body = None
        self.show_empty()

    def show_empty(self):
        w = QLabel('Select an element on a screen or in Layers.\n\nDrag elements from the palette onto a screen.\n'
                   'Alt-click picks the element under the selected one.\nShift while dragging: no snapping.')
        w.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        w.setWordWrap(True)
        w.setMargin(10)
        self.setWidget(w)
        self.fields = []
        self.path = None

    def words(self):
        vars_ = self.s.doc.get('vars') if isinstance(self.s.doc.get('vars'), dict) else {}
        w = Wd.completions(vars_.keys())
        el = get(self.s.doc, self.path) if self.path else None
        # a component's parameters: the names its instances set in "with"
        if self.path and self.path[0] == 'components':
            for inst in self.s.instances_of(self.path[1]):
                if isinstance(inst.get('with'), dict):
                    w += list(inst['with'].keys())
        if isinstance(el, dict) and isinstance(el.get('with'), dict):
            w += list(el['with'].keys())
        return sorted(set(w))

    def font_names(self):
        f = self.s.doc.get('fonts') if isinstance(self.s.doc.get('fonts'), dict) else {}
        return Wd.FONTS + [k for k in f if k not in Wd.FONTS]

    def build(self, path):
        self.path = path
        el = get(self.s.doc, path)
        if not isinstance(el, dict):
            if isinstance(el, list):
                self.show_raw_only(path, el)
            else:
                self.show_empty()
            return
        self.path = path
        body = QWidget()
        v = QVBoxLayout(body)
        v.setContentsMargins(6, 6, 6, 6)
        t = E.element_type(el)
        if 'use' in el:
            # an instance is whatever its component is
            comp, seen = el, set()
            while isinstance(comp, dict) and 'use' in comp and comp['use'] not in seen:
                seen.add(comp['use'])
                comp = (self.s.doc.get('components') or {}).get(comp['use'])
            if isinstance(comp, dict):
                t = el.get('type') or E.element_type(comp)
        head = QHBoxLayout()
        title = QLabel('<b>%s</b>  <span style="color:#888">%s</span>' % (t, ' › '.join(str(p) for p in path[-4:])))
        title.setToolTip('.'.join(str(p) for p in path))
        head.addWidget(title, 1)
        v.addLayout(head)
        form = QFormLayout()
        idf = QLineEdit(str(el.get('id', '')))
        idf.setPlaceholderText('a name for this element (optional)')
        idf.editingFinished.connect(lambda: self.s.set_prop(path, 'id', idf.text() or None, structure=True))
        form.addRow('Name', idf)
        tc = QComboBox()
        tc.addItems(list(E.TYPES))
        tc.setCurrentText(t)
        tc.activated.connect(lambda i: self.s.set_prop(path, 'type', tc.currentText(), structure=True))
        if 'use' not in el:
            form.addRow('Type', tc)
        if 'use' in el:
            uc = QComboBox()
            uc.addItems(sorted((self.s.doc.get('components') or {}).keys()))
            uc.setCurrentText(str(el['use']))
            uc.activated.connect(lambda i: self.s.set_prop(path, 'use', uc.currentText(), structure=True))
            form.addRow('Component', uc)
            go = QPushButton('Edit the component')
            go.clicked.connect(lambda: self.s.select(('components', el['use'])))
            form.addRow('', go)
        v.addLayout(form)
        self.fields = []
        words = self.words()
        fonts = self.font_names()
        known = {'id', 'type', 'use', 'children', 'item', 'states', 'with', 'smooth'}
        sections = list(Wd.COMMON) + Wd.BY_TYPE.get(t, [])
        if path[-1] == 'item' or (len(path) > 2 and path[-2] == 'item'):
            pass
        for name, specs in sections:
            box = QGroupBox(name)
            box.setCheckable(True)
            open_ = self.s.settings.value('section/' + name, 'true') == 'true'
            box.setChecked(open_)
            inner = QWidget()
            fl = QFormLayout(inner)
            fl.setContentsMargins(4, 2, 4, 2)
            for key, label, kind, extra, tip in specs:
                known.add(key.split('.')[0])
                f = Wd.PropField(key, kind, extra, words, fonts, browse=self.s.browse_asset)
                f.set_value(Wd.get_path(el, key))
                f.changed.connect(lambda k, val: self.s.set_prop(self.path, k, val))
                lab = QLabel(label)
                lab.setToolTip(tip)
                f.setToolTip(tip)
                fl.addRow(lab, f)
                self.fields.append(f)
            bl = QVBoxLayout(box)
            bl.setContentsMargins(2, 2, 2, 2)
            bl.addWidget(inner)
            inner.setVisible(open_)
            box.toggled.connect(lambda on, inner=inner, name=name: (inner.setVisible(on), self.s.settings.setValue('section/' + name, 'true' if on else 'false')))
            v.addWidget(box)
        # whatever the forms don't cover, as JSON
        other = QGroupBox('Other properties')
        ofl = QFormLayout(other)
        for k, val in el.items():
            if k in known:
                continue
            rf = Wd.RawField(k, val)
            rf.changed.connect(lambda key, val: self.s.set_prop(self.path, key, val))
            ofl.addRow(k, rf)
        addrow = QHBoxLayout()
        nk = QLineEdit()
        nk.setPlaceholderText('property')
        nv = QLineEdit()
        nv.setPlaceholderText('value (JSON or text)')
        ab = QPushButton('Add')

        def add():
            if not nk.text().strip():
                return
            try:
                val = json.loads(nv.text())
            except ValueError:
                val = nv.text()
            self.s.set_prop(self.path, nk.text().strip(), val, structure=True)
        ab.clicked.connect(add)
        addrow.addWidget(nk)
        addrow.addWidget(nv)
        addrow.addWidget(ab)
        ofl.addRow(addrow)
        v.addWidget(other)
        # component parameters, states, smoothing
        v.addWidget(self.json_box('Component parameters (with)', 'with', el, '{ "label": "Play" }'))
        v.addWidget(self.json_box('States: overrides while "when" holds', 'states', el,
                                  '[ { "when": "=sel", "scale": 1.1, "fill": "#fff" } ]'))
        v.addWidget(self.json_box('Smooth: ease towards new values (ms)', 'smooth', el, '{ "x": 120, "scale": 90 }  or  150'))
        raw = QGroupBox('This element as JSON')
        raw.setCheckable(True)
        raw.setChecked(False)
        rl = QVBoxLayout(raw)
        te = QPlainTextEdit(jsonc.dumps(el))
        te.setFont(QFontDatabase.systemFont(QFontDatabase.FixedFont))
        te.setMinimumHeight(220)
        te.setVisible(False)
        ap = QPushButton('Apply JSON')
        ap.setVisible(False)
        raw.toggled.connect(lambda on: (te.setVisible(on), ap.setVisible(on)))
        ap.clicked.connect(lambda: self.s.replace_element(self.path, te.toPlainText()))
        rl.addWidget(te)
        rl.addWidget(ap)
        v.addWidget(raw)
        v.addStretch(1)
        self.setWidget(body)

    def json_box(self, title, key, el, placeholder):
        box = QGroupBox(title)
        box.setCheckable(True)
        box.setChecked(key in el)
        lay = QVBoxLayout(box)
        te = QPlainTextEdit(jsonc.dumps(el[key]) if key in el else '')
        te.setPlaceholderText(placeholder)
        te.setFont(QFontDatabase.systemFont(QFontDatabase.FixedFont))
        te.setMaximumHeight(110)
        te.setVisible(key in el)
        ap = QPushButton('Apply')
        ap.setVisible(key in el)

        def apply():
            t = te.toPlainText().strip()
            if not t:
                self.s.set_prop(self.path, key, None)
                return
            try:
                val, _ = jsonc.loads(t)
            except ValueError as ex:
                QMessageBox.warning(self, 'JSON', str(ex))
                return
            self.s.set_prop(self.path, key, val)
        ap.clicked.connect(apply)
        box.toggled.connect(lambda on: (te.setVisible(on), ap.setVisible(on)))
        lay.addWidget(te)
        lay.addWidget(ap)
        return box

    def show_raw_only(self, path, el):
        body = QWidget()
        v = QVBoxLayout(body)
        v.addWidget(QLabel('<b>A list of elements</b> (drawn as a group)'))
        te = QPlainTextEdit(jsonc.dumps(el))
        te.setFont(QFontDatabase.systemFont(QFontDatabase.FixedFont))
        v.addWidget(te)
        ap = QPushButton('Apply JSON')
        ap.clicked.connect(lambda: self.s.replace_element(path, te.toPlainText()))
        v.addWidget(ap)
        self.setWidget(body)
        self.fields = []

    def sync(self):
        """New values into the fields (after a drag) without rebuilding them"""
        el = get(self.s.doc, self.path) if self.path else None
        if not isinstance(el, dict):
            return
        for f in self.fields:
            if not f.code.hasFocus():
                f.set_value(Wd.get_path(el, f.key))


class DictTable(QWidget):
    """A JSON object as name / value rows: vars, colours, fonts"""

    def __init__(self, studio, key, hint, color_preview=False, file_browse=False):
        super().__init__()
        self.s, self.key, self.color_preview, self.file_browse = studio, key, color_preview, file_browse
        v = QVBoxLayout(self)
        v.setContentsMargins(0, 0, 0, 0)
        lab = QLabel(hint)
        lab.setWordWrap(True)
        lab.setStyleSheet('color: #999')
        v.addWidget(lab)
        self.t = QTableWidget(0, 2)
        self.t.setHorizontalHeaderLabels(['Name', 'Value'])
        self.t.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self.t.verticalHeader().hide()
        self.t.cellChanged.connect(self.changed)
        self.t.cellDoubleClicked.connect(self.double)
        v.addWidget(self.t)
        row = QHBoxLayout()
        a = QPushButton('Add')
        a.clicked.connect(self.add)
        r = QPushButton('Remove')
        r.clicked.connect(self.remove)
        row.addWidget(a)
        row.addWidget(r)
        if color_preview:
            c = QPushButton('Pick colour…')
            c.clicked.connect(self.pick)
            row.addWidget(c)
        if file_browse:
            b = QPushButton('Import file…')
            b.clicked.connect(self.browse)
            row.addWidget(b)
        v.addLayout(row)
        self.loading = False

    def load(self):
        self.loading = True
        d = self.s.doc.get(self.key) if isinstance(self.s.doc.get(self.key), dict) else {}
        self.t.setRowCount(0)
        for k, val in d.items():
            r = self.t.rowCount()
            self.t.insertRow(r)
            self.t.setItem(r, 0, QTableWidgetItem(k))
            it = QTableWidgetItem(val if isinstance(val, str) else json.dumps(val))
            if self.color_preview and isinstance(val, str) and not Wd.is_code(val):
                c = X.parse_color(val)
                if c is not None:
                    it.setIcon(Wd.swatch(c))
            self.t.setItem(r, 1, it)
        self.loading = False

    def collect(self):
        d = {}
        for r in range(self.t.rowCount()):
            k = self.t.item(r, 0).text().strip() if self.t.item(r, 0) else ''
            if not k:
                continue
            t = self.t.item(r, 1).text() if self.t.item(r, 1) else ''
            try:
                val = json.loads(t) if t.strip()[:1] in '-0123456789[{tfn"' and t.strip() else t
            except ValueError:
                val = t
            d[k] = val
        return d

    def changed(self, *_):
        if self.loading:
            return
        before = self.s.snapshot()
        d = self.collect()
        if d:
            self.s.doc[self.key] = d
        else:
            self.s.doc.pop(self.key, None)
        self.s.commit(before=before, structure=False, inspector=True)
        self.load()

    def add(self):
        name, ok = QInputDialog.getText(self, 'Add', 'Name:')
        if ok and name:
            before = self.s.snapshot()
            self.s.doc.setdefault(self.key, {})
            if not isinstance(self.s.doc[self.key], dict):
                self.s.doc[self.key] = {}
            self.s.doc[self.key][name] = '#ffffff' if self.color_preview else ''
            self.s.commit(before=before, structure=False, inspector=True)
            self.load()

    def remove(self):
        r = self.t.currentRow()
        if r >= 0:
            self.t.removeRow(r)
            self.changed()

    def double(self, r, c):
        if c == 1 and self.color_preview:
            self.pick()

    def pick(self):
        r = self.t.currentRow()
        if r < 0:
            return
        cur = X.parse_color(self.t.item(r, 1).text()) if self.t.item(r, 1) else None
        cur = 0xffffffff if cur is None else cur
        from PySide6.QtWidgets import QColorDialog
        c = QColorDialog.getColor(QColor((cur >> 16) & 255, (cur >> 8) & 255, cur & 255, (cur >> 24) & 255), self, 'Colour',
                                  QColorDialog.ShowAlphaChannel)
        if c.isValid():
            self.t.setItem(r, 1, QTableWidgetItem('#%02x%02x%02x' % (c.red(), c.green(), c.blue()) + ('%02x' % c.alpha() if c.alpha() < 255 else '')))

    def browse(self):
        rel = self.s.import_asset('Fonts (*.ttf *.otf)')
        if rel:
            name = os.path.splitext(os.path.basename(rel))[0].lower()
            r = self.t.rowCount()
            self.loading = True
            self.t.insertRow(r)
            self.t.setItem(r, 0, QTableWidgetItem(name))
            self.loading = False
            self.t.setItem(r, 1, QTableWidgetItem(rel))


class ThemePanel(QScrollArea):
    """The theme's own settings: name, colours, fonts, vars, buttons, hints, timings"""

    def __init__(self, studio):
        super().__init__()
        self.s = studio
        self.setWidgetResizable(True)

    def build(self):
        doc = self.s.doc
        body = QWidget()
        v = QVBoxLayout(body)
        g = QGroupBox('Theme')
        f = QFormLayout(g)
        for key, label in (('name', 'Name (shown in Settings)'), ('author', 'Author')):
            e = QLineEdit(str(doc.get(key, '')))
            e.editingFinished.connect(lambda e=e, key=key: self.s.set_top(key, e.text() or None))
            f.addRow(label, e)
        base = QComboBox()
        base.addItems(['', 'dark', 'light'])
        base.setCurrentText(str(doc.get('base', '')))
        base.activated.connect(lambda i: self.s.set_top('base', base.currentText() or None))
        f.addRow('Settings look', base)
        acc = Wd.PropField('accent', 'color', None, [])
        acc.set_value(doc.get('accent'))
        acc.changed.connect(lambda k, val: self.s.set_top('accent', val))
        f.addRow('Accent', acc)
        font = Wd.PropField('font', 'font', None, [], self.s.inspector.font_names())
        font.set_value(doc.get('font'))
        font.changed.connect(lambda k, val: self.s.set_top('font', val))
        f.addRow('Default font', font)
        dbg = QCheckBox('Show theme errors on the top screen (debug)')
        dbg.setChecked(bool(doc.get('debug')))
        dbg.toggled.connect(lambda on: self.s.set_top('debug', True if on else None))
        f.addRow(dbg)
        v.addWidget(g)
        tabs = QTabWidget()
        self.vars = DictTable(self.s, 'vars', 'Names for values, used in any expression: "=card", "=mix(bg, accent, 0.2)". '
                              'Double-click a colour to pick it.', color_preview=True)
        self.vars.load()
        tabs.addTab(self.vars, 'Variables')
        self.colors = DictTable(self.s, 'colors', 'The colour-theme keys: they style Settings and the keyboard '
                                '(screenBg, ink, muted ...), and are palette.<name> in expressions.', color_preview=True)
        self.colors.load()
        tabs.addTab(self.colors, 'Colours')
        self.fonts = DictTable(self.s, 'fonts', 'Your own fonts: a name and a .ttf next to the theme. Use the name in a text\'s font.',
                               file_browse=True)
        self.fonts.load()
        tabs.addTab(self.fonts, 'Fonts')
        tabs.addTab(self.input_tab(), 'Buttons')
        tabs.addTab(self.hints_tab(), 'Hints')
        tabs.addTab(self.behavior_tab(), 'Timing')
        tabs.setMinimumHeight(320)
        v.addWidget(tabs)
        v.addStretch(1)
        self.setWidget(body)

    def input_tab(self):
        w = QWidget()
        gl = QGridLayout(w)
        gl.addWidget(QLabel('What each button does (empty: the default). START and SELECT stay Banana\'s.'), 0, 0, 1, 3)
        gl.addWidget(QLabel('<b>Home</b>'), 1, 1)
        gl.addWidget(QLabel('<b>Library</b>'), 1, 2)
        inp = self.s.doc.get('input') if isinstance(self.s.doc.get('input'), dict) else {}
        for r, b in enumerate(BUTTON_NAMES):
            gl.addWidget(QLabel(b.upper() if len(b) == 1 else b), r + 2, 0)
            for c, view in enumerate(('home', 'library')):
                cb = QComboBox()
                cb.setEditable(True)
                cb.addItems(ACTIONS)
                cur = (inp.get(view) or {}).get(b, '') if isinstance(inp.get(view), dict) else ''
                cb.setCurrentText(cur)
                cb.activated.connect(lambda i, cb=cb, view=view, b=b: self.s.set_input(view, b, cb.currentText()))
                cb.lineEdit().editingFinished.connect(lambda cb=cb, view=view, b=b: self.s.set_input(view, b, cb.currentText()))
                gl.addWidget(cb, r + 2, c + 1)
        return w

    def hints_tab(self):
        w = QWidget()
        f = QFormLayout(w)
        f.addRow(QLabel('The buttons a "hints" list shows, per view: key and label pairs.'))
        hs = self.s.doc.get('hints') if isinstance(self.s.doc.get('hints'), dict) else {}
        for view in ('home', 'library'):
            e = QLineEdit(json.dumps(hs.get(view)) if view in hs else '')
            e.setPlaceholderText('[["A", "Open"], ["Y", "Favorite"]]')
            e.editingFinished.connect(lambda e=e, view=view: self.s.set_hints(view, e.text()))
            f.addRow(view.capitalize(), e)
        return w

    def behavior_tab(self):
        w = QWidget()
        f = QFormLayout(w)
        b = self.s.doc.get('behavior') if isinstance(self.s.doc.get('behavior'), dict) else {}
        for key, label, lo, hi, d in (('launchMs', 'Launch view length (ms)', 0, 10000, 900), ('scrollMs', 'Scroll easing (ms)', 0, 2000, 70),
                                      ('settleMs', 'Redraw after a move (ms)', 0, 5000, 600), ('fps', 'Animation fps', 1, 60, 30)):
            sp = QSpinBox()
            sp.setRange(lo, hi)
            sp.setValue(int(b.get(key, d)) if isinstance(b.get(key, d), (int, float)) else d)
            sp.setKeyboardTracking(False)
            sp.valueChanged.connect(lambda val, key=key: self.s.set_behavior(key, val))
            f.addRow(label, sp)
        for key, label, d in (('wrap', 'The buttons wrap around the ends', False), ('extraHints', 'Add Banana\'s hints (SELECT Search)', True)):
            cb = QCheckBox(label)
            cb.setChecked(bool(b.get(key, d)))
            cb.toggled.connect(lambda on, key=key: self.s.set_behavior(key, on))
            f.addRow(cb)
        cues = QLineEdit(json.dumps(b.get('cues')) if 'cues' in b else '')
        cues.setPlaceholderText('{"insert": 560}: sounds at those ms of the launch')
        cues.editingFinished.connect(lambda: self.s.set_behavior('cues', cues.text()))
        f.addRow('Sound cues', cues)
        return w


class SimPanel(QWidget):
    """What the preview shows: the view, the selection, the time, the battery... and buttons to move around"""

    def __init__(self, studio):
        super().__init__()
        self.s = studio
        sim = studio.renderer.sim
        lay = QHBoxLayout(self)
        nav = QGridLayout()
        self.view = QComboBox()
        self.view.addItems(['home', 'library', 'launch'])
        self.view.activated.connect(lambda i: self.s.set_view(self.view.currentText()))
        nav.addWidget(QLabel('View'), 0, 0)
        nav.addWidget(self.view, 0, 1, 1, 3)
        self.sys = QComboBox()
        self.sys.addItems([s['name'] for s in studio.renderer.lib.systems])
        self.sys.activated.connect(lambda i: self.s.nav('sys', i))
        nav.addWidget(QLabel('System'), 1, 0)
        nav.addWidget(self.sys, 1, 1, 1, 3)
        self.game = QComboBox()
        self.game.activated.connect(lambda i: self.s.nav('game', i))
        nav.addWidget(QLabel('Game'), 2, 0)
        nav.addWidget(self.game, 2, 1, 1, 3)
        for c, (label, act, tip) in enumerate((('◀', 'prev', 'Previous (left)'), ('▶', 'next', 'Next (right)'),
                                               ('A', 'a', 'Open / play'), ('B', 'b', 'Back'))):
            b = QPushButton(label)
            b.setToolTip(tip)
            b.setFixedWidth(40)
            b.clicked.connect(lambda _=False, act=act: self.s.press(act))
            nav.addWidget(b, 3, c)
        lay.addLayout(nav)
        sl = QGridLayout()
        self.sliders = {}
        rows = (('time', 'time (ms)', 0, 20000), ('moved', 'moved (ms ago)', 0, 3000), ('entered', 'entered (ms ago)', 0, 3000),
                ('launch', 'launch.progress %', 0, 100), ('scroll_offset', 'scroll − cursor %', -100, 100), ('battery', 'battery %', -1, 100))
        for r, (key, label, lo, hi) in enumerate(rows):
            s = QSlider(Qt.Horizontal)
            s.setRange(lo, hi)
            val = getattr(sim, key)
            s.setValue(int(val * 100) if key in ('launch', 'scroll_offset') else int(val))
            lab = QLabel()
            s.valueChanged.connect(lambda v, key=key, lab=lab: self.set(key, v, lab))
            sl.addWidget(QLabel(label), r, 0)
            sl.addWidget(s, r, 1)
            sl.addWidget(lab, r, 2)
            lab.setText(str(s.value()))
            lab.setMinimumWidth(44)
            self.sliders[key] = s
        lay.addLayout(sl, 1)
        right = QVBoxLayout()
        self.play = QPushButton('▶ Play animations')
        self.play.setCheckable(True)
        self.play.toggled.connect(self.s.set_playing)
        right.addWidget(self.play)
        for key, label in (('charging', 'On the charger'), ('online', 'Wi-Fi online')):
            cb = QCheckBox(label)
            cb.setChecked(getattr(sim, key))
            cb.toggled.connect(lambda on, key=key: (setattr(self.s.renderer.sim, key, on), self.s.sim_changed()))
            right.addWidget(cb)
        clk = QLineEdit(sim.clock)
        clk.setPlaceholderText('clock')
        clk.editingFinished.connect(lambda: (setattr(self.s.renderer.sim, 'clock', clk.text()), self.s.sim_changed()))
        right.addWidget(clk)
        right.addStretch(1)
        lay.addLayout(right)
        self.refresh()

    def set(self, key, v, lab):
        lab.setText(str(v))
        setattr(self.s.renderer.sim, key, v / 100 if key in ('launch', 'scroll_offset') else float(v))
        self.s.sim_changed(from_slider=True)

    def refresh(self):
        sim = self.s.renderer.sim
        self.view.setCurrentText(sim.view)
        self.sys.setCurrentIndex(sim.sys)
        self.game.blockSignals(True)
        self.game.clear()
        self.game.addItems([g['title'] for g in self.s.renderer.lib.systems[sim.sys]['_games']] or ['(no games)'])
        self.game.setCurrentIndex(min(sim.game, max(0, self.game.count() - 1)))
        self.game.blockSignals(False)
        for key, s in self.sliders.items():
            s.blockSignals(True)
            val = getattr(sim, key)
            s.setValue(int(val * 100) if key in ('launch', 'scroll_offset') else int(min(val, s.maximum())))
            s.blockSignals(False)


# ---- the window --------------------------------------------------------------------------------------------------------
class Studio(QMainWindow):
    def __init__(self, path=None):
        super().__init__()
        self.settings = QSettings('BananaOS', 'ThemeStudio')
        self._light = str(self.settings.value('light_theme', 'false')).lower() in ('true', '1')
        # your own themes live outside the repo; the Open/Save dialogs start here (File > Themes folder to change)
        self.themes_dir = self.settings.value('themes_dir', os.path.expanduser('~/BananaThemes'))
        repo = exact.find_repo(self.settings.value('repo', ''))
        self.renderer = E.Renderer(repo)
        self.exact = exact.Exact(self)
        self.exact.repo = repo
        self.exact.finished.connect(self.engine_done)
        self.exact.failed.connect(self.engine_failed)
        self.exact.built.connect(self.engine_built)
        self.doc = presets.blank()
        self.path = None
        self.base_dir = ''
        self.had_comments = False
        self.dirty = False
        self.undo_stack, self.redo_stack = [], []
        self.sel = None
        self.active_screen = 1
        self.grid = int(self.settings.value('grid', 1))
        self.engine_errors = []
        self.playing = False
        self.settle_until = 0
        self.clock_ms = 0
        self.timer = QTimer(self)
        self.timer.setInterval(33)
        self.timer.timeout.connect(self.tick)
        self.engine_timer = QTimer(self)
        self.engine_timer.setSingleShot(True)
        self.engine_timer.setInterval(300)
        self.engine_timer.timeout.connect(self.run_engine)

        self.canvas = Canvas(self)
        self.scroll = QScrollArea()
        self.scroll.setWidget(self.canvas)
        self.scroll.setAlignment(Qt.AlignCenter)
        self.code = QPlainTextEdit()
        self.code.setFont(QFontDatabase.systemFont(QFontDatabase.FixedFont))
        self.code.setLineWrapMode(QPlainTextEdit.NoWrap)
        code_w = QWidget()
        cl = QVBoxLayout(code_w)
        cl.setContentsMargins(0, 0, 0, 0)
        row = QHBoxLayout()
        apply_b = QPushButton('Apply changes')
        apply_b.clicked.connect(self.apply_code)
        revert_b = QPushButton('Revert')
        revert_b.clicked.connect(self.fill_code)
        self.code_status = QLabel('The whole theme as JSON: comments and trailing commas are allowed.')
        row.addWidget(apply_b)
        row.addWidget(revert_b)
        row.addWidget(self.code_status, 1)
        cl.addLayout(row)
        cl.addWidget(self.code)
        self.tabs = QTabWidget()
        self.tabs.addTab(self.scroll, 'Design')
        self.tabs.addTab(code_w, 'JSON')
        self.tabs.currentChanged.connect(lambda i: self.fill_code() if i == 1 else None)
        self.setCentralWidget(self.tabs)

        self.palette = Palette(self)
        self.layers = LayerTree(self)
        self.inspector = Inspector(self)
        self.theme_panel = ThemePanel(self)
        self.sim_panel = SimPanel(self)
        self.problems = QListWidget()
        self.problems.itemActivated.connect(self.goto_problem)
        self.comps = self.make_components_panel()
        self.assets = self.make_assets_panel()

        def dock(title, w, area, name):
            d = QDockWidget(title, self)
            d.setObjectName(name)
            d.setWidget(w)
            self.addDockWidget(area, d)
            return d
        def tabs(*pages):
            t = QTabWidget()
            t.setDocumentMode(True)
            for title, w in pages:
                t.addTab(w, title)
            return t
        self.right_tabs = tabs(('Inspector', self.inspector), ('Theme settings', self.theme_panel))
        self.bottom_tabs = tabs(('Preview controls', self.sim_panel), ('Problems', self.problems))
        dp = dock('Add elements', self.palette, Qt.LeftDockWidgetArea, 'palette')
        dl = dock('Layers', self.layers, Qt.LeftDockWidgetArea, 'layers')
        dc = dock('Components and files', tabs(('Components', self.comps), ('Assets', self.assets)), Qt.LeftDockWidgetArea, 'library')
        di = dock('Properties', self.right_tabs, Qt.RightDockWidgetArea, 'inspector')
        ds = dock('Preview', self.bottom_tabs, Qt.BottomDockWidgetArea, 'sim')
        self.docks = [dp, dl, dc, di, ds]
        self.resizeDocks([ds], [230], Qt.Vertical)
        self.resizeDocks([dp, di], [260, 400], Qt.Horizontal)
        self.resizeDocks([dp, dl, dc], [220, 300, 200], Qt.Vertical)
        self.make_actions()
        self.resize(1500, 980)
        if self.settings.value('geometry'):
            self.restoreGeometry(self.settings.value('geometry'))
            self.restoreState(self.settings.value('state'))
        if path:
            self.open_path(path)
        else:
            self.load_doc(presets.blank(), None, '')
        if not repo:
            QTimer.singleShot(300, lambda: self.statusBar().showMessage(
                'No BananaLinux checkout found: fonts, icons and the engine preview need one (Engine > Set BananaLinux folder).', 15000))

    # ---- menus ---------------------------------------------------------------------------------------------------------
    def make_actions(self):
        mb = self.menuBar()
        tb = self.addToolBar('Main')
        tb.setObjectName('main')
        tb.setIconSize(QSize(16, 16))

        def act(menu, text, slot, key=None, tool=False, tip=''):
            a = QAction(text, self)
            if key:
                a.setShortcut(QKeySequence(key))
            a.triggered.connect(slot)
            if tip:
                a.setToolTip(tip)
                a.setStatusTip(tip)
            menu.addAction(a)
            if tool:
                tb.addAction(a)
            return a
        m = mb.addMenu('&File')
        act(m, 'New…', self.new, QKeySequence.New, True, 'Start from a template')
        act(m, 'Open…', self.open, QKeySequence.Open, True)
        self.recent_menu = m.addMenu('Open recent')
        self.fill_recent()
        act(m, 'Save', self.save, QKeySequence.Save, True)
        act(m, 'Save as…', self.save_as, QKeySequence.SaveAs)
        act(m, 'Export zip…', self.export_zip, None, True, 'The theme and its files, ready to unzip into banana/themes on the card')
        m.addSeparator()
        act(m, 'Quit', self.close, QKeySequence.Quit)
        tb.addSeparator()
        m = mb.addMenu('&Edit')
        self.undo_a = act(m, 'Undo', self.undo, QKeySequence.Undo, True)
        self.redo_a = act(m, 'Redo', self.redo, QKeySequence.Redo, True)
        m.addSeparator()
        act(m, 'Copy element', self.copy_el, QKeySequence.Copy)
        act(m, 'Paste element', self.paste_el, QKeySequence.Paste)
        act(m, 'Duplicate', self.duplicate, 'Ctrl+D')
        act(m, 'Delete', self.delete, QKeySequence.Delete)
        m.addSeparator()
        act(m, 'Bring forward', lambda: self.reorder(1), 'Ctrl+]')
        act(m, 'Send backward', lambda: self.reorder(-1), 'Ctrl+[')
        act(m, 'Bring to front', lambda: self.reorder(99), 'Ctrl+Shift+]')
        act(m, 'Send to back', lambda: self.reorder(-99), 'Ctrl+Shift+[')
        act(m, 'Wrap in a group', self.wrap_group, 'Ctrl+G')
        act(m, 'Make a component of it…', self.make_component)
        m.addSeparator()
        act(m, 'Centre horizontally', lambda: self.align('h'))
        act(m, 'Centre vertically', lambda: self.align('v'))
        tb.addSeparator()
        m = mb.addMenu('&View')
        self.view_combo = QComboBox()
        self.view_combo.addItems(['home', 'library', 'launch'])
        self.view_combo.setToolTip('The view the screens show')
        self.view_combo.activated.connect(lambda i: self.set_view(self.view_combo.currentText()))
        tb.addWidget(QLabel(' View '))
        tb.addWidget(self.view_combo)
        self.zoom_combo = QComboBox()
        self.zoom_combo.addItems(['Fit', '50%', '75%', '100%', '125%', '150%', '200%'])
        self.zoom_combo.setEditable(True)
        self.zoom_combo.activated.connect(lambda i: self.zoom_to(self.zoom_combo.currentText()))
        self.zoom_combo.lineEdit().returnPressed.connect(lambda: self.zoom_to(self.zoom_combo.currentText()))
        tb.addWidget(QLabel(' Zoom '))
        tb.addWidget(self.zoom_combo)
        grid = QComboBox()
        grid.addItems(['1', '2', '4', '5', '8', '10', '16', '20'])
        grid.setCurrentText(str(self.grid))
        grid.setToolTip('Positions snap to this many units')
        grid.activated.connect(lambda i: (setattr(self, 'grid', int(grid.currentText())), self.settings.setValue('grid', self.grid)))
        tb.addWidget(QLabel(' Grid '))
        tb.addWidget(grid)
        tb.addSeparator()
        self.outline_a = act(m, 'Outline every element', self.toggle_outline, 'Ctrl+L', True)
        self.outline_a.setCheckable(True)
        self.engine_a = act(m, 'Engine preview (exact)', self.toggle_engine, 'F5', True,
                            'Show what the real banana-ui draws (needs the BananaLinux checkout)')
        self.engine_a.setCheckable(True)
        self.light_a = act(m, 'Light theme', self.toggle_light, 'Ctrl+T', True,
                           "The BananaOS website's light palette (the web editor's switch)")
        self.light_a.setCheckable(True)
        self.light_a.setChecked(self._light)
        m.addSeparator()
        for d in self.docks:
            m.addAction(d.toggleViewAction())
        m = mb.addMenu('E&ngine')
        act(m, 'Render now', self.run_engine, 'Shift+F5')
        act(m, 'Build banana-ui', self.build_engine)
        act(m, 'Set BananaLinux folder…', self.set_repo)
        m = mb.addMenu('&Help')
        act(m, 'Quick guide', self.help)
        act(m, 'Expression reference', self.expr_help, 'F1')

    def help(self):
        QMessageBox.information(self, 'Banana Theme Studio', """<h3>Banana Theme Studio</h3>
<p>Designs BananaOS layout themes: one JSON file that draws both screens.</p>
<ul>
<li><b>Add</b>: drag from <i>Add elements</i> onto a screen, or double-click one (it goes into the selected group, or the screen you last clicked).</li>
<li><b>Select</b>: click on a screen or in <i>Layers</i>. Alt-click picks the element underneath. Clicking an item of a list selects its template: edit it once, every item follows.</li>
<li><b>Move / resize</b>: drag the element or its handles; arrows nudge (Shift: 10). It snaps to the screen and other elements (hold Shift to stop it) and to the grid.</li>
<li><b>Order</b>: drag in <i>Layers</i> (lower in the list = drawn on top), or Ctrl+[ / Ctrl+].</li>
<li><b>Values or expressions</b>: every field takes a value, or press <b>ƒ</b> and write =expression (completion of hooks, vars and functions; red = it doesn't parse). Texts take {hooks} in braces.</li>
<li><b>Preview</b>: <i>Preview controls</i> moves the selection, switches views, scrubs time/moved/launch.progress and plays animations. F5 shows the real engine's picture.</li>
<li><b>JSON</b> tab: the whole file, for anything the forms don't cover.</li>
<li><b>Install</b>: File &gt; Export zip, unzip into <code>banana/themes/</code> on the card, pick it in Settings &gt; Display &amp; sound &gt; Theme.</li>
</ul>""")

    def expr_help(self):
        lines = ['<h3>Expressions</h3><p>Start a value with <code>=</code>: <code>=sel ? accent : \'#202430\'</code>. '
                 'In text use braces: <code>{game.plays} plays</code>.</p><p>Operators: + - * / %  == != &lt; &lt;= &gt; &gt;=  &amp;&amp; || !  a ? b : c</p>']
        for group, names in X.FUNCTIONS.items():
            lines.append('<p><b>%s</b>: %s</p>' % (group, ', '.join(X.SIGNATURES.get(n, n) for n in names.split())))
        lines.append('<p><b>Hooks</b>: %s</p>' % ', '.join(Wd.HOOKS))
        lines.append('<p><b>sys.*</b>: %s</p><p><b>game.* / item.*</b>: %s</p>' % (', '.join(Wd.SYS), ', '.join(Wd.GAME)))
        box = QMessageBox(self)
        box.setWindowTitle('Expression reference')
        box.setText(''.join(lines))
        box.exec()

    # ---- the document --------------------------------------------------------------------------------------------------
    def snapshot(self):
        return json.dumps(self.doc)

    def theme_dir(self):
        return os.path.dirname(self.path) if self.path else self.base_dir

    def load_doc(self, doc, path, base_dir):
        self.doc = doc if isinstance(doc, dict) else {}
        self.path = path
        self.base_dir = base_dir
        self.undo_stack, self.redo_stack = [], []
        self.dirty = False
        self.sel = None
        self.refresh(structure=True)
        self.theme_panel.build()
        self.update_title()

    def commit(self, before=None, structure=True, inspector=False):
        if before is not None and before != self.snapshot():
            self.undo_stack.append(before)
            self.redo_stack.clear()
            if len(self.undo_stack) > 300:
                self.undo_stack.pop(0)
            self.dirty = True
        self.refresh(structure=structure, rebuild_inspector=inspector)
        self.update_title()

    def live_change(self):
        """A drag in progress: redraw, no undo step yet"""
        self.renderer.set_theme(self.doc, self.theme_dir())
        self.canvas.update()
        self.inspector.sync()

    def refresh(self, structure=True, rebuild_inspector=False):
        self.renderer.set_theme(self.doc, self.theme_dir())
        if self.sel is not None and get(self.doc, self.sel) is None:
            self.sel = None
        self.canvas.update()
        if structure:
            self.layers.rebuild()
            self.fill_components()
        if structure or rebuild_inspector or self.inspector.path != self.sel:
            if self.sel is None:
                self.inspector.show_empty()
            else:
                self.inspector.build(self.sel)
        else:
            self.inspector.sync()
        self.fill_assets()
        QTimer.singleShot(0, self.update_problems)
        if self.canvas.mode == 'engine':
            self.engine_timer.start()

    def update_title(self):
        name = os.path.basename(self.path) if self.path else 'Untitled'
        self.setWindowTitle('%s%s — Banana Theme Studio' % (name, ' •' if self.dirty else ''))

    def select(self, path, screen=None, from_tree=False):
        if screen is not None:
            self.active_screen = screen
        path = tuple(path) if path is not None else None
        if path != self.sel:
            self.sel = path
            if path is None:
                self.inspector.show_empty()
            else:
                self.inspector.build(path)
                self.right_tabs.setCurrentIndex(0)
        if not from_tree:
            self.layers.sync_selection()
        self.canvas.update()

    def set_prop(self, path, key, value, structure=False):
        el = get(self.doc, path)
        if not isinstance(el, dict):
            return
        before = self.snapshot()
        Wd.set_path(el, key, value)
        self.commit(before=before, structure=structure or key in ('type', 'use', 'id', 'text', 'src', 'source', 'layout'),
                    inspector=key in ('type', 'use'))

    def set_top(self, key, value):
        before = self.snapshot()
        if value is None:
            self.doc.pop(key, None)
        else:
            self.doc[key] = value
        self.commit(before=before, structure=False)

    def set_input(self, view, button, action):
        before = self.snapshot()
        inp = self.doc.setdefault('input', {})
        v = inp.setdefault(view, {})
        if action:
            v[button] = action
        else:
            v.pop(button, None)
            if not v:
                inp.pop(view)
            if not inp:
                self.doc.pop('input')
        self.commit(before=before, structure=False)

    def set_hints(self, view, text):
        before = self.snapshot()
        hs = self.doc.setdefault('hints', {})
        if text.strip():
            try:
                hs[view] = json.loads(text)
            except ValueError as e:
                QMessageBox.warning(self, 'Hints', 'Not JSON: %s' % e)
                return
        else:
            hs.pop(view, None)
        if not hs:
            self.doc.pop('hints')
        self.commit(before=before, structure=False)

    def set_behavior(self, key, value):
        before = self.snapshot()
        b = self.doc.setdefault('behavior', {})
        if key == 'cues':
            if not value.strip():
                b.pop('cues', None)
            else:
                try:
                    b['cues'] = json.loads(value)
                except ValueError as e:
                    QMessageBox.warning(self, 'Cues', 'Not JSON: %s' % e)
                    return
        else:
            b[key] = value
        self.commit(before=before, structure=False)

    def replace_element(self, path, text):
        try:
            v, _ = jsonc.loads(text)
        except ValueError as e:
            QMessageBox.warning(self, 'JSON', str(e))
            return
        before = self.snapshot()
        parent = get(self.doc, path[:-1]) if path else None
        if parent is None:
            return
        parent[path[-1]] = v
        self.commit(before=before, inspector=True)

    def undo(self):
        if not self.undo_stack:
            return
        self.redo_stack.append(self.snapshot())
        self.doc = json.loads(self.undo_stack.pop())
        self.dirty = True
        self.refresh(structure=True, rebuild_inspector=True)
        self.theme_panel.build()
        self.update_title()

    def redo(self):
        if not self.redo_stack:
            return
        self.undo_stack.append(self.snapshot())
        self.doc = json.loads(self.redo_stack.pop())
        self.dirty = True
        self.refresh(structure=True, rebuild_inspector=True)
        self.theme_panel.build()
        self.update_title()

    # ---- element operations --------------------------------------------------------------------------------------------
    def screen_list(self, screen, view=None):
        """The current view's list of elements for a screen, made a list if it was one element"""
        view = view or self.renderer.sim.view
        side = 'top' if screen == 0 else 'bottom'
        screens = self.doc.setdefault('screens', {})
        src = view
        for v in (view, 'library' if view == 'launch' else 'home', 'home'):
            if isinstance(screens.get(v), dict) and screens[v].get(side):
                src = v
                break
        sv = screens.setdefault(src, {})
        cur = sv.get(side)
        if cur is None:
            sv[side] = []
        elif not isinstance(cur, list):
            sv[side] = [cur]
        return sv[side], ('screens', src, side)

    def container_for_add(self):
        """(list, path of the list, index to insert at, the parent frame's hit or None)"""
        if self.sel is not None:
            el = get(self.doc, self.sel)
            if isinstance(el, dict) and E.element_type(el) == 'group' and 'use' not in el:
                ch = el.setdefault('children', [])
                if not isinstance(ch, list):
                    el['children'] = ch = [ch]
                return ch, self.sel + ('children',), len(ch)
            if isinstance(self.sel[-1], int):
                lst = get(self.doc, self.sel[:-1])
                if isinstance(lst, list):
                    return lst, self.sel[:-1], self.sel[-1] + 1
        lst, p = self.screen_list(self.active_screen)
        return lst, p, len(lst)

    def add_element(self, el, at=None):
        before = self.snapshot()
        lst, lpath, idx = self.container_for_add()
        if at is None and 'x' not in el and 'right' not in el and el.get('type') != 'list':
            el.setdefault('x', 40)
            el.setdefault('y', 40)
        lst.insert(idx, el)
        self.sel = find_path(self.doc, el)
        self.commit(before=before, inspector=True)
        self.layers.sync_selection()

    def drop_element(self, el, screen, pt):
        before = self.snapshot()
        self.active_screen = screen
        # into the innermost free group under the drop (not a row/column, not a list's item)
        target, hit = None, None
        for h in self.canvas.hits_at(screen, pt):
            if h.type != 'group' or h.item is not None or h.path[:1] == ('components',):
                continue
            g = get(self.doc, h.path)
            if isinstance(g, dict) and 'use' not in g and not g.get('layout') and \
                    not (h.rect.width() >= E.W - 1 and h.rect.height() >= E.H - 1):
                target, hit = g, h
                break
        if target is not None:
            ch = target.setdefault('children', [])
            if not isinstance(ch, list):
                target['children'] = ch = [ch]
            k = hit.k or 1
            x, y = (pt.x() - hit.rect.x()) / k, (pt.y() - hit.rect.y()) / k
            lst = ch
        else:
            lst, _ = self.screen_list(screen)
            x, y = pt.x(), pt.y()
        if el.get('type') != 'list' or el.get('direction') not in ('free',):
            # placed by its top-left corner where it was dropped, unless it's laid out across the screen
            if not (isinstance(el.get('w'), str) and el['w'].endswith('%')) and 'right' not in el:
                el['x'] = int(round(x / max(1, self.grid))) * max(1, self.grid)
            if 'bottom' not in el:
                el['y'] = int(round(y / max(1, self.grid))) * max(1, self.grid)
        lst.append(el)
        self.sel = find_path(self.doc, el)
        self.commit(before=before, inspector=True)
        self.layers.sync_selection()

    def parent_of(self, path):
        if path and isinstance(path[-1], int):
            lst = get(self.doc, path[:-1])
            if isinstance(lst, list):
                return lst, path[-1]
        return None, None

    def delete(self):
        if self.tabs.currentIndex() != 0 or self.sel is None:
            return
        before = self.snapshot()
        lst, i = self.parent_of(self.sel)
        if lst is not None:
            del lst[i]
        else:
            parent = get(self.doc, self.sel[:-1])
            if isinstance(parent, dict):
                parent.pop(self.sel[-1], None)
        self.sel = None
        self.commit(before=before, inspector=True)

    def duplicate(self):
        lst, i = self.parent_of(self.sel) if self.sel else (None, None)
        if lst is None:
            return
        before = self.snapshot()
        c = copy.deepcopy(lst[i])
        if isinstance(c, dict):
            for k in ('x', 'y'):
                if isinstance(c.get(k), (int, float)):
                    c[k] += 10
            c.pop('id', None)
        lst.insert(i + 1, c)
        self.sel = find_path(self.doc, c)
        self.commit(before=before, inspector=True)

    def copy_el(self):
        if self.tabs.currentIndex() != 0:
            self.code.copy()
            return
        el = get(self.doc, self.sel) if self.sel else None
        if el is not None:
            QApplication.clipboard().setText(jsonc.dumps(el))

    def paste_el(self):
        if self.tabs.currentIndex() != 0:
            self.code.paste()
            return
        try:
            el, _ = jsonc.loads(QApplication.clipboard().text())
        except ValueError:
            self.statusBar().showMessage('The clipboard holds no element JSON.', 3000)
            return
        if isinstance(el, (dict, list)):
            self.add_element(el, at=True)

    def reorder(self, d):
        lst, i = self.parent_of(self.sel) if self.sel else (None, None)
        if lst is None:
            return
        before = self.snapshot()
        el = lst.pop(i)
        j = max(0, min(len(lst), i + d))
        lst.insert(j, el)
        self.sel = find_path(self.doc, el)
        self.commit(before=before)

    def wrap_group(self):
        lst, i = self.parent_of(self.sel) if self.sel else (None, None)
        if lst is None:
            return
        before = self.snapshot()
        g = {'type': 'group', 'children': [lst[i]]}
        lst[i] = g
        self.sel = find_path(self.doc, g)
        self.commit(before=before, inspector=True)

    def make_component(self):
        el = get(self.doc, self.sel) if self.sel else None
        if not isinstance(el, dict) or self.sel[0] == 'components':
            return
        name, ok = QInputDialog.getText(self, 'Make a component', 'Component name (reuse it with "use"):')
        if not ok or not name:
            return
        before = self.snapshot()
        comps = self.doc.setdefault('components', {})
        comp = copy.deepcopy(el)
        inst = {'use': name}
        for k in ('x', 'y', 'right', 'bottom', 'anchor'):
            if k in comp:
                inst[k] = comp.pop(k)
        comps[name] = comp
        parent = get(self.doc, self.sel[:-1])
        parent[self.sel[-1]] = inst
        self.sel = find_path(self.doc, inst)
        self.commit(before=before, inspector=True)

    def instances_of(self, name):
        out = []
        stack = [self.doc.get('screens'), self.doc.get('components')]
        while stack:
            d = stack.pop()
            if isinstance(d, dict):
                if d.get('use') == name:
                    out.append(d)
                stack += list(d.values())
            elif isinstance(d, list):
                stack += d
        return out

    def align(self, axis):
        """Centred on its parent: x (or y) at 50% with the element's own middle there"""
        el = get(self.doc, self.sel) if self.sel else None
        if not isinstance(el, dict):
            return
        before = self.snapshot()
        if axis == 'h':
            el.pop('right', None)
            el['x'], el['ax'] = '50%', 0.5
        else:
            el.pop('bottom', None)
            el['y'], el['ay'] = '50%', 0.5
        self.commit(before=before, structure=False, inspector=True)

    def move_element(self, src, target, mode, target_type):
        """Layers drag: src moved above/below target, or into it (a group or a screen)"""
        if src == target or target[:len(src)] == src:
            return
        before = self.snapshot()
        el = get(self.doc, src)
        target_obj = get(self.doc, target)
        is_root = target_type == 'root'
        if mode == 'on' and not is_root and target_type != 'group':
            mode = 'below'
        lst, i = self.parent_of(src)
        if lst is None:
            self.statusBar().showMessage('That element can\'t move (it is a list\'s item or a screen\'s only element).', 4000)
            return
        del lst[i]
        if is_root or mode == 'on':
            if is_root:
                cont = get(self.doc, target)
                if not isinstance(cont, list):
                    cont = [cont] if cont is not None else []
                    sv = get(self.doc, target[:-1])
                    sv[target[-1]] = cont
            else:
                ch = target_obj.setdefault('children', [])
                if not isinstance(ch, list):
                    target_obj['children'] = ch = [ch]
                cont = ch
            if is_root and mode == 'above':
                cont.insert(0, el)
            else:
                cont.append(el)
        else:
            tp = find_path(self.doc, target_obj)
            tl, ti = self.parent_of(tp) if tp else (None, None)
            if tl is None:
                lst.insert(i, el)
                return
            tl.insert(ti if mode == 'above' else ti + 1, el)
        self.sel = find_path(self.doc, el)
        self.commit(before=before, inspector=True)

    def context_menu(self, gpos):
        m = QMenu(self)
        for text, slot in (('Duplicate', self.duplicate), ('Delete', self.delete), (None, None),
                           ('Bring forward', lambda: self.reorder(1)), ('Send backward', lambda: self.reorder(-1)),
                           ('Bring to front', lambda: self.reorder(99)), ('Send to back', lambda: self.reorder(-99)), (None, None),
                           ('Wrap in a group', self.wrap_group), ('Make a component of it…', self.make_component),
                           ('Copy JSON', self.copy_el), ('Paste', self.paste_el)):
            if text is None:
                m.addSeparator()
            else:
                m.addAction(text, slot)
        m.exec(gpos)

    # ---- components & assets -------------------------------------------------------------------------------------------
    def make_components_panel(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(2, 2, 2, 2)
        lab = QLabel('Reusable elements. Edit one here and every "use" of it follows.')
        lab.setWordWrap(True)
        v.addWidget(lab)
        self.comp_list = QListWidget()
        self.comp_list.itemClicked.connect(lambda it: self.select(('components', it.text())))
        self.comp_list.itemDoubleClicked.connect(lambda it: self.insert_instance(it.text()))
        v.addWidget(self.comp_list)
        row = QHBoxLayout()
        for text, slot in (('New', self.new_component), ('Use', lambda: self.insert_instance(self.comp_list.currentItem().text())
                                                          if self.comp_list.currentItem() else None),
                           ('Rename', self.rename_component), ('Delete', self.delete_component)):
            b = QPushButton(text)
            b.clicked.connect(slot)
            row.addWidget(b)
        v.addLayout(row)
        return w

    def fill_components(self):
        self.comp_list.clear()
        comps = self.doc.get('components') if isinstance(self.doc.get('components'), dict) else {}
        for k in comps:
            it = QListWidgetItem(k)
            it.setToolTip('%d uses. Click to edit, double-click to add one.' % len(self.instances_of(k)))
            self.comp_list.addItem(it)

    def new_component(self):
        name, ok = QInputDialog.getText(self, 'New component', 'Name:')
        if ok and name:
            before = self.snapshot()
            self.doc.setdefault('components', {})[name] = {'type': 'group', 'w': 160, 'h': 60, 'fill': '#2a3045', 'radius': 10,
                                                            'children': [{'type': 'text', 'x': 12, 'y': 18, 'text': '{label}', 'size': 18}]}
            self.sel = ('components', name)
            self.commit(before=before, inspector=True)

    def insert_instance(self, name):
        self.add_element({'use': name, 'x': 40, 'y': 40, 'with': {}}, at=True)

    def rename_component(self):
        it = self.comp_list.currentItem()
        if not it:
            return
        old = it.text()
        name, ok = QInputDialog.getText(self, 'Rename', 'New name:', text=old)
        if not ok or not name or name == old:
            return
        before = self.snapshot()
        comps = self.doc['components']
        self.doc['components'] = {(name if k == old else k): v for k, v in comps.items()}
        for inst in self.instances_of(old):
            inst['use'] = name
        self.sel = None
        self.commit(before=before, inspector=True)

    def delete_component(self):
        it = self.comp_list.currentItem()
        if not it:
            return
        n = len(self.instances_of(it.text()))
        if n and QMessageBox.question(self, 'Delete', '"%s" is used %d times. Delete it anyway?' % (it.text(), n)) != QMessageBox.Yes:
            return
        before = self.snapshot()
        self.doc['components'].pop(it.text(), None)
        self.sel = None
        self.commit(before=before, inspector=True)

    def assets_folder(self):
        """A theme's files go in a folder named after it, next to the JSON"""
        d = self.theme_dir()
        if not d or not self.path:
            return '', ''
        stem = os.path.splitext(os.path.basename(self.path))[0]
        return os.path.join(d, stem), stem

    def make_assets_panel(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(2, 2, 2, 2)
        self.assets_label = QLabel()
        self.assets_label.setWordWrap(True)
        v.addWidget(self.assets_label)
        self.asset_list = QListWidget()
        self.asset_list.setIconSize(QSize(40, 40))
        self.asset_list.itemDoubleClicked.connect(self.use_asset)
        v.addWidget(self.asset_list)
        b = QPushButton('Import pictures, fonts or sounds…')
        b.clicked.connect(lambda: self.import_asset())
        v.addWidget(b)
        return w

    def fill_assets(self):
        folder, stem = self.assets_folder()
        self.asset_list.clear()
        if not folder:
            self.assets_label.setText('Save the theme first: its files go in a folder next to it.')
            return
        self.assets_label.setText('Files in %s/ (double-click a picture to add it)' % stem)
        if not os.path.isdir(folder):
            return
        for f in sorted(os.listdir(folder)):
            it = QListWidgetItem(f)
            rel = stem + '/' + f
            it.setData(Qt.UserRole, rel)
            if f.lower().endswith(('.png', '.jpg', '.jpeg')):
                img = self.renderer.image(os.path.join(folder, f))
                if img is not None:
                    it.setIcon(QPixmap.fromImage(img.scaled(40, 40, Qt.KeepAspectRatio, Qt.SmoothTransformation)))
            self.asset_list.addItem(it)

    def use_asset(self, it):
        rel = it.data(Qt.UserRole)
        if rel.lower().endswith(('.png', '.jpg', '.jpeg')):
            img = self.renderer.image(os.path.join(self.theme_dir(), rel))
            w, h = (img.width(), img.height()) if img is not None else (128, 128)
            s = min(1.0, 640 / w, 480 / h)
            self.add_element({'type': 'image', 'src': rel, 'w': round(w * s), 'h': round(h * s), 'fit': 'contain'})
        elif rel.lower().endswith(('.ttf', '.otf')):
            self.set_top('fonts', dict(self.doc.get('fonts') or {}, **{os.path.splitext(os.path.basename(rel))[0].lower(): rel}))
            self.theme_panel.build()

    def import_asset(self, filt='Theme files (*.png *.jpg *.jpeg *.ttf *.otf *.wav)'):
        """Copies a file into the theme's folder; its path relative to the theme"""
        folder, stem = self.assets_folder()
        if not folder:
            QMessageBox.information(self, 'Save first', 'Save the theme first: its files go in a folder next to its JSON.')
            if not self.save():
                return ''
            folder, stem = self.assets_folder()
        f, _ = QFileDialog.getOpenFileName(self, 'Import', os.path.expanduser('~'), filt)
        if not f:
            return ''
        if f.lower().endswith(('.png', '.jpg', '.jpeg')):
            img = QImage(f)
            if not img.isNull() and max(img.width(), img.height()) > 4096:
                QMessageBox.warning(self, 'Too big', 'Pictures can be at most 4096 px a side on the handheld.')
                return ''
        os.makedirs(folder, exist_ok=True)
        dst = os.path.join(folder, os.path.basename(f))
        if os.path.abspath(f) != os.path.abspath(dst):
            shutil.copy2(f, dst)
        self.fill_assets()
        return stem + '/' + os.path.basename(f)

    def browse_asset(self):
        return self.import_asset('Pictures (*.png *.jpg *.jpeg)')

    # ---- preview state -------------------------------------------------------------------------------------------------
    def set_view(self, view):
        sim = self.renderer.sim
        if view != sim.view:
            sim.view = view
            sim.entered = 0
            self.settle()
        self.view_combo.setCurrentText(view)
        self.sim_panel.refresh()
        self.layers.rebuild()
        self.sim_changed()

    def nav(self, what, i):
        sim = self.renderer.sim
        old = sim.sys if what == 'sys' else sim.game
        if what == 'sys':
            sim.sys = i
            sim.game = 0
        else:
            sim.game = i
        sim.dir = 1 if i >= old else -1
        sim.moved = 0
        sim.scroll_offset = float(old - i) if (what == 'sys') == (sim.view == 'home') else 0.0
        self.settle()
        self.sim_panel.refresh()
        self.sim_changed()

    def press(self, act):
        sim = self.renderer.sim
        if act in ('prev', 'next'):
            d = 1 if act == 'next' else -1
            if sim.view == 'home':
                n = len(self.renderer.lib.systems)
                self.nav('sys', max(0, min(n - 1, sim.sys + d)))
            else:
                n = len(self.renderer.games())
                if n:
                    self.nav('game', max(0, min(n - 1, sim.game + d)))
        elif act == 'a':
            if sim.view == 'home':
                self.set_view('library')
            elif sim.view == 'library':
                sim.launch = 0
                self.set_view('launch')
        elif act == 'b':
            self.set_view('home' if sim.view == 'library' else 'library' if sim.view == 'launch' else 'home')

    def settle(self):
        """Animations play for a moment after a move, as on the handheld"""
        self.settle_until = self.clock_ms + 1500
        if not self.timer.isActive():
            self.timer.start()

    def set_playing(self, on):
        self.playing = on
        self.sim_panel.play.setText('⏸ Pause' if on else '▶ Play animations')
        if on:
            self.timer.start()

    def tick(self):
        dt = 33.0
        self.clock_ms += dt
        sim = self.renderer.sim
        sim.time += dt
        sim.moved = min(sim.moved + dt, 60000)
        sim.entered = min(sim.entered + dt, 60000)
        b = self.doc.get('behavior') if isinstance(self.doc.get('behavior'), dict) else {}
        scroll_ms = b.get('scrollMs', 70) if isinstance(b.get('scrollMs', 70), (int, float)) else 70
        sim.scroll_offset *= pow(2.718281828, -dt / max(1.0, scroll_ms))
        if abs(sim.scroll_offset) < 0.002:
            sim.scroll_offset = 0.0
        if sim.view == 'launch':
            sim.launch = min(1.0, sim.launch + dt / max(1.0, self.renderer.theme.launch_ms))
            if sim.launch >= 1.0 and self.playing:
                sim.launch = 0.0
        self.canvas.update()
        if not self.playing and self.clock_ms > self.settle_until:
            self.timer.stop()
            self.sim_panel.refresh()
            if self.canvas.mode == 'engine':
                self.engine_timer.start()

    def sim_changed(self, from_slider=False):
        self.canvas.update()
        if self.canvas.mode == 'engine':
            self.engine_timer.start()

    def set_zoom(self, z):
        z = max(0.25, min(4.0, z))
        self.canvas.zoom = z
        self.canvas.update_size()
        self.canvas.update()
        self.zoom_combo.setCurrentText('%d%%' % round(z * 100))

    def zoom_to(self, text):
        if text.lower().startswith('fit'):
            self.fit()
            return
        try:
            self.set_zoom(float(text.strip().strip('%')) / 100)
        except ValueError:
            pass

    def fit(self):
        """Both screens in the window"""
        vp = self.scroll.viewport().size()
        c = self.canvas
        z = min((vp.width() - 2 * c.MARGIN - 4) / E.W, (vp.height() - 2 * c.MARGIN - c.GAP - 4) / (2 * E.H))
        self.set_zoom(max(0.25, z))

    def showEvent(self, e):
        super().showEvent(e)
        if not getattr(self, 'fitted', False):
            self.fitted = True
            QTimer.singleShot(0, self.fit)

    def toggle_outline(self, on):
        self.canvas.outline = on
        self.canvas.update()

    def toggle_light(self, on=False):
        """The website's dark/light switch (style.py), remembered for the next run."""
        self._light = bool(on)
        style.set_theme(QApplication.instance(), self._light)
        self.settings.setValue('light_theme', self._light)
        self.canvas.update()

    # ---- the engine ----------------------------------------------------------------------------------------------------
    def toggle_engine(self, on):
        if on and not self.exact.ready():
            if not self.exact.repo:
                self.set_repo()
            if self.exact.repo and not self.exact.ready():
                if QMessageBox.question(self, 'Engine', 'banana-ui isn\'t built yet. Build it now (a minute or two)?') == QMessageBox.Yes:
                    self.build_engine()
            self.engine_a.setChecked(False)
            return
        self.canvas.mode = 'engine' if on else 'studio'
        if on:
            self.run_engine()
        self.canvas.update()

    def run_engine(self):
        if not self.exact.ready():
            return
        sim = self.renderer.sim
        self.exact.render(self.doc, self.theme_dir(), sim.view, sim.sys, sim.game, launch=sim.launch,
                          launch_ms=self.renderer.theme.launch_ms if self.renderer.theme else 900)

    def engine_done(self, top, bot, errors, log):
        self.canvas.engine_img = [QImage(top), QImage(bot)]
        self.engine_errors = errors
        self.canvas.update()
        self.update_problems()

    def engine_failed(self, msg):
        self.engine_errors = ['engine: ' + msg.splitlines()[0]]
        self.statusBar().showMessage(msg.splitlines()[0], 8000)
        self.update_problems()

    def build_engine(self):
        if not self.exact.repo:
            self.set_repo()
            if not self.exact.repo:
                return
        self.statusBar().showMessage('Building banana-ui…')
        self.exact.build()

    def engine_built(self, ok, out):
        if ok:
            self.statusBar().showMessage('banana-ui built: F5 shows the engine\'s picture.', 8000)
        else:
            QMessageBox.warning(self, 'Build failed', 'make ui failed (it needs g++, freetype2, libdrm and zlib):\n\n' + out[-1500:])

    def set_repo(self):
        d = QFileDialog.getExistingDirectory(self, 'The BananaLinux folder (with ui/ in it)', self.exact.repo or os.path.expanduser('~'))
        if not d:
            return
        r = exact.find_repo(d)
        if not r:
            QMessageBox.warning(self, 'Not found', 'That folder has no ui/assets: pick the BananaLinux checkout.')
            return
        self.settings.setValue('repo', r)
        self.exact.repo = r
        self.renderer = E.Renderer(r)
        self.sim_panel.refresh()
        self.refresh(structure=True, rebuild_inspector=True)

    # ---- problems ------------------------------------------------------------------------------------------------------
    def validate(self):
        out = []
        d = self.theme_dir()
        files = []

        def walk(v):
            if isinstance(v, dict):
                for k in ('src', 'fallback'):
                    if isinstance(v.get(k), str) and not Wd.is_code(v[k]) and v[k]:
                        files.append(v[k])
                for x in v.values():
                    walk(x)
            elif isinstance(v, list):
                for x in v:
                    walk(x)
        walk(self.doc.get('screens'))
        walk(self.doc.get('components'))
        for f in (self.doc.get('fonts') or {}).values() if isinstance(self.doc.get('fonts'), dict) else []:
            if isinstance(f, str):
                files.append(f)
        for f in sorted(set(files)):
            p = f if f.startswith('/') else os.path.join(d, f) if d else ''
            if not f.startswith('/') and (not p or not os.path.exists(p)):
                out.append('file "%s" is not next to the theme' % f)
        if not isinstance(self.doc.get('screens'), dict):
            out.append('no "screens": this is a colour theme, not a layout theme')
        if not self.doc.get('name'):
            out.append('no "name": Settings shows the file name')
        return out

    def update_problems(self):
        self.problems.clear()
        studio = list(self.renderer.errors) + self.validate()
        for e in studio:
            it = QListWidgetItem('⚠ ' + e)
            it.setData(Qt.UserRole, e)
            self.problems.addItem(it)
        for e in self.engine_errors:
            if e not in studio:
                it = QListWidgetItem('⚙ ' + e)
                it.setToolTip('Reported by the real banana-ui')
                it.setData(Qt.UserRole, e)
                self.problems.addItem(it)
        n = self.problems.count()
        self.bottom_tabs.setTabText(1, 'Problems (%d)' % n if n else 'Problems')

    def goto_problem(self, it):
        msg = it.data(Qt.UserRole)
        best = None
        for sc in (0, 1):
            for h in self.canvas.hits[sc]:
                if msg.startswith(h.where) and (best is None or len(h.where) > len(best.where)):
                    best = h
        if best:
            self.select(best.path, best.screen)

    # ---- files ---------------------------------------------------------------------------------------------------------
    def maybe_save(self):
        if not self.dirty:
            return True
        r = QMessageBox.question(self, 'Unsaved changes', 'Save the changes to %s?' % (os.path.basename(self.path) if self.path else 'this theme'),
                                 QMessageBox.Save | QMessageBox.Discard | QMessageBox.Cancel)
        if r == QMessageBox.Save:
            return self.save()
        return r == QMessageBox.Discard

    def new(self):
        if not self.maybe_save():
            return
        ts = presets.templates(self.exact.repo)
        label, ok = QInputDialog.getItem(self, 'New theme', 'Start from:', [t[0] for t in ts], 0, False)
        if not ok:
            return
        path = dict(ts)[label]
        if path is None:
            self.load_doc(presets.blank(), None, '')
        else:
            doc, _ = jsonc.load(path)
            doc['name'] = (doc.get('name') or 'Theme') + ' (mine)'
            self.load_doc(doc, None, os.path.dirname(path))
            self.dirty = True
            self.update_title()

    def open(self):
        if not self.maybe_save():
            return
        f, _ = QFileDialog.getOpenFileName(self, 'Open a theme', self.settings.value('lastdir', self.themes_dir), 'Themes (*.json)')
        if f:
            self.open_path(f)

    def open_path(self, f):
        try:
            doc, had = jsonc.load(f)
        except (OSError, ValueError) as e:
            QMessageBox.warning(self, 'Open', 'Can\'t read %s:\n%s' % (f, e))
            return
        if not isinstance(doc, dict):
            QMessageBox.warning(self, 'Open', 'Not a theme: the file is not a JSON object.')
            return
        self.had_comments = had
        self.load_doc(doc, os.path.abspath(f), os.path.dirname(os.path.abspath(f)))
        self.settings.setValue('lastdir', os.path.dirname(f))
        rec = [r for r in (self.settings.value('recent') or []) if r != f]
        self.settings.setValue('recent', [os.path.abspath(f)] + rec[:9])
        self.fill_recent()
        if 'screens' not in doc:
            self.statusBar().showMessage('A colour theme (no "screens"): add elements to make it a layout theme.', 10000)

    def fill_recent(self):
        self.recent_menu.clear()
        for r in self.settings.value('recent') or []:
            self.recent_menu.addAction(r, lambda r=r: self.maybe_save() and self.open_path(r))

    def save(self):
        if not self.path:
            return self.save_as()
        return self.write(self.path)

    def save_as(self):
        start = self.path or os.path.join(self.settings.value('lastdir', self.themes_dir),
                                          (self.doc.get('name') or 'my-theme').lower().replace(' ', '-').replace('(', '').replace(')', '') + '.json')
        f, _ = QFileDialog.getSaveFileName(self, 'Save the theme', start, 'Themes (*.json)')
        if not f:
            return False
        if not f.endswith('.json'):
            f += '.json'
        old_dir = self.theme_dir()
        if self.write(f):
            self.copy_assets(old_dir, os.path.dirname(f))
            self.path = os.path.abspath(f)
            self.base_dir = os.path.dirname(self.path)
            self.settings.setValue('lastdir', self.base_dir)
            self.refresh(structure=False)
            self.update_title()
            return True
        return False

    def referenced_files(self, base):
        out = set()

        def walk(v):
            if isinstance(v, str) and not Wd.is_code(v) and not v.startswith('/') and '.' in v[-5:] and base:
                p = os.path.join(base, v)
                if os.path.isfile(p):
                    out.add(v)
            elif isinstance(v, dict):
                for x in v.values():
                    walk(x)
            elif isinstance(v, list):
                for x in v:
                    walk(x)
        walk(self.doc)
        return out

    def copy_assets(self, old, new):
        """Saving somewhere else: the theme's own files go along"""
        if not old or os.path.abspath(old) == os.path.abspath(new):
            return
        for rel in self.referenced_files(old):
            dst = os.path.join(new, rel)
            if not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(os.path.join(old, rel), dst)

    def write(self, f):
        if self.had_comments and not self.settings.value('warned_comments'):
            QMessageBox.information(self, 'Comments', 'The file had // comments: saving writes plain JSON without them.')
            self.settings.setValue('warned_comments', True)
        try:
            jsonc.save(f, self.doc)
        except OSError as e:
            QMessageBox.warning(self, 'Save', str(e))
            return False
        self.path = os.path.abspath(f)
        self.dirty = False
        self.update_title()
        self.statusBar().showMessage('Saved %s' % f, 4000)
        rec = [r for r in (self.settings.value('recent') or []) if r != self.path]
        self.settings.setValue('recent', [self.path] + rec[:9])
        self.fill_recent()
        return True

    def export_zip(self):
        stem = os.path.splitext(os.path.basename(self.path))[0] if self.path else (self.doc.get('name') or 'theme').lower().replace(' ', '-')
        f, _ = QFileDialog.getSaveFileName(self, 'Export', os.path.join(self.settings.value('lastdir', self.themes_dir), stem + '.zip'), 'Zip (*.zip)')
        if not f:
            return
        base = self.theme_dir()
        with zipfile.ZipFile(f, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr(stem + '.json', jsonc.dumps(self.doc) + '\n')
            for rel in sorted(self.referenced_files(base)):
                z.write(os.path.join(base, rel), rel)
        QMessageBox.information(self, 'Exported', 'Unzip %s into banana/themes/ on the card (or over Settings > Network > File sharing), '
                                'then pick "%s" in Settings > Display & sound > Theme.' % (os.path.basename(f), self.doc.get('name', stem)))

    # ---- the JSON tab --------------------------------------------------------------------------------------------------
    def fill_code(self):
        self.code.setPlainText(jsonc.dumps(self.doc))
        self.code_status.setText('The whole theme as JSON: comments and trailing commas are allowed.')

    def apply_code(self):
        try:
            doc, _ = jsonc.loads(self.code.toPlainText())
        except ValueError as e:
            self.code_status.setText('<span style="color:#e05050">Not valid JSON: %s</span>' % e)
            return
        if not isinstance(doc, dict):
            self.code_status.setText('<span style="color:#e05050">A theme is a JSON object { ... }</span>')
            return
        before = self.snapshot()
        self.doc = doc
        self.commit(before=before, inspector=True)
        self.theme_panel.build()
        self.code_status.setText('Applied.')

    def closeEvent(self, e):
        if not self.maybe_save():
            e.ignore()
            return
        self.settings.setValue('geometry', self.saveGeometry())
        self.settings.setValue('state', self.saveState())
        e.accept()


def main():
    app = QApplication(sys.argv)
    app.setApplicationName('Banana Theme Studio')
    app.setStyle('Fusion')
    light = str(QSettings('BananaOS', 'ThemeStudio').value('light_theme', 'false')).lower() in ('true', '1')
    style.apply(app, light)			# the BananaOS website's look (style.py), dark or light
    w = Studio(sys.argv[1] if len(sys.argv) > 1 else None)
    w.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
