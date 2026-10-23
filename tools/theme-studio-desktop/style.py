"""The BananaOS website's look (web/preview) for the desktop Banana Theme Studio.

Charcoal panels with a cream border and a lighter band along the top, banana yellow for the selection, Pixelify
Sans, and the menu's dot pattern. The QPalette (Fusion) covers the widgets the stylesheet doesn't name; the
stylesheet does the rest, so the desktop studio matches the web editor (tools/theme-studio).

    import style
    style.apply(app)        # call it on the QApplication, before the window is made

The fonts and patterns in assets/ are copies of web/preview/assets/.
"""
import os

from PySide6.QtGui import QColor, QFont, QFontDatabase, QPalette

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, 'assets')

# the website's colours (:root in web/preview/css/style.css)
BG = '#17161c'
PANEL = '#191820'
BAND = '#38333b'
CREAM = '#e8e2d3'
INK = '#f4f1e8'
SOFT = '#a39d8e'
TEXT = '#d8d2c4'
FIELD = '#121117'
ALT = '#1f1e26'
ACCENT = '#f0b429'
TILE_LINE = 'rgba(232, 226, 211, 140)'


def _url(name):
    return 'url("%s")' % os.path.join(ASSETS, name).replace('\\', '/')


def load_fonts():
    """Register Pixelify Sans from assets/; returns its family name (or None)."""
    family = None
    for f in ('PixelifySans-Regular.ttf', 'PixelifySans-Medium.ttf'):
        path = os.path.join(ASSETS, f)
        if os.path.exists(path):
            fid = QFontDatabase.addApplicationFont(path)
            if fid >= 0:
                fams = QFontDatabase.applicationFontFamilies(fid)
                if fams and not family:
                    family = fams[0]
    return family


def stylesheet(family):
    fam = family or 'sans-serif'
    return """
QWidget { font-family: "%(fam)s"; font-size: 13px; color: %(ink)s; }
QMainWindow, QDialog { background-color: %(bg)s; background-image: %(pattern)s; background-repeat: repeat; background-position: top left; }
QToolTip { background: %(band)s; color: %(ink)s; border: 2px solid %(cream)s; padding: 4px; }

QDockWidget { color: %(ink)s; }
QDockWidget::title { background: %(band)s; color: %(ink)s; padding: 7px 10px; border: 2px solid %(cream)s; }
QDockWidget > QWidget { background: %(panel)s; border: 2px solid %(cream)s; border-top: none; }

QTabWidget::pane { border: 2px solid %(cream)s; background: %(panel)s; top: -2px; }
QTabBar::tab { background: %(panel)s; color: %(ink)s; border: 2px solid %(cream)s; border-bottom: none;
    padding: 5px 12px; margin-right: 2px; border-top-left-radius: 7px; border-top-right-radius: 7px; }
QTabBar::tab:selected { background: %(accent)s; color: %(bg)s; }
QTabBar::tab:hover:!selected { border-color: %(accent)s; }

QGroupBox { border: 2px solid %(cream)s; border-radius: 9px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 5px; color: %(accent)s; }

QPushButton { background: %(panel)s; color: %(ink)s; border: 2px solid %(cream)s; border-radius: 8px; padding: 5px 12px; }
QPushButton:hover { border-color: %(accent)s; }
QPushButton:pressed { background: %(band)s; }
QPushButton:checked, QPushButton:default { background: %(accent)s; color: %(bg)s; }
QPushButton:disabled { color: %(soft)s; border-color: %(soft)s; }

QToolButton { background: transparent; border: 2px solid transparent; border-radius: 7px; padding: 4px 8px; }
QToolButton:hover { border-color: %(cream)s; }
QToolButton:checked { background: %(accent)s; color: %(bg)s; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %(field)s; color: %(ink)s; border: 2px solid %(tileline)s; border-radius: 7px; padding: 3px 7px;
    selection-background-color: %(accent)s; selection-color: %(bg)s; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %(accent)s; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: %(soft)s; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %(panel)s; color: %(ink)s; border: 2px solid %(cream)s;
    selection-background-color: %(accent)s; selection-color: %(bg)s; }

QTreeWidget, QListWidget, QTableWidget, QTreeView, QListView, QTableView, QToolBox {
    background: %(panel)s; color: %(ink)s; border: 2px solid %(cream)s; alternate-background-color: %(alt)s; }
QTreeWidget::item:selected, QListWidget::item:selected, QTreeView::item:selected, QListView::item:selected, QTableView::item:selected { background: %(accent)s; color: %(bg)s; }
QTreeWidget::item:hover, QListWidget::item:hover { background: %(band)s; }
QHeaderView::section { background: %(band)s; color: %(ink)s; border: 1px solid %(cream)s; padding: 4px; }
QToolBox::tab { background: %(panel)s; color: %(ink)s; border: 2px solid %(cream)s; border-radius: 7px; padding: 5px 9px; margin-top: 2px; }
QToolBox::tab:selected { background: %(accent)s; color: %(bg)s; }

QMenuBar { background: %(panel)s; color: %(ink)s; border-bottom: 2px solid %(cream)s; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected { background: %(accent)s; color: %(bg)s; }
QMenu { background: %(panel)s; color: %(ink)s; border: 2px solid %(cream)s; }
QMenu::item { padding: 5px 22px; }
QMenu::item:selected { background: %(accent)s; color: %(bg)s; }
QMenu::separator { height: 1px; background: %(cream)s; margin: 4px 8px; }

QToolBar { background: %(panel)s; border-bottom: 2px solid %(cream)s; spacing: 4px; padding: 3px; }
QStatusBar { background: %(panel)s; color: %(soft)s; border-top: 2px solid %(cream)s; }

QScrollBar:vertical { background: %(field)s; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: %(band)s; border: 2px solid %(cream)s; border-radius: 6px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: %(accent)s; }
QScrollBar:horizontal { background: %(field)s; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: %(band)s; border: 2px solid %(cream)s; border-radius: 6px; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: %(accent)s; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSlider::groove:horizontal { height: 6px; background: %(field)s; border: 2px solid %(cream)s; border-radius: 5px; }
QSlider::handle:horizontal { background: %(accent)s; border: 2px solid %(cream)s; width: 14px; margin: -7px 0; border-radius: 7px; }
QSlider::groove:vertical { width: 6px; background: %(field)s; border: 2px solid %(cream)s; border-radius: 5px; }
QSlider::handle:vertical { background: %(accent)s; border: 2px solid %(cream)s; height: 14px; margin: 0 -7px; border-radius: 7px; }

QCheckBox, QRadioButton { spacing: 7px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 2px solid %(cream)s; border-radius: 4px; background: %(field)s; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %(accent)s; }
QRadioButton::indicator { border-radius: 9px; }

QProgressBar { border: 2px solid %(cream)s; border-radius: 7px; background: %(field)s; text-align: center; }
QProgressBar::chunk { background: %(accent)s; }
QSplitter::handle { background: %(band)s; }
""" % dict(fam=fam, bg=BG, panel=PANEL, band=BAND, cream=CREAM, ink=INK, soft=SOFT, text=TEXT,
           field=FIELD, alt=ALT, accent=ACCENT, tileline=TILE_LINE, pattern=_url('menu-pattern.png'))


def palette():
    """The same colours for the widgets the stylesheet doesn't reach (Fusion)."""
    p = QPalette()
    p.setColor(QPalette.Window, QColor(BG))
    p.setColor(QPalette.WindowText, QColor(INK))
    p.setColor(QPalette.Base, QColor(PANEL))
    p.setColor(QPalette.AlternateBase, QColor(ALT))
    p.setColor(QPalette.Text, QColor(INK))
    p.setColor(QPalette.PlaceholderText, QColor(SOFT))
    p.setColor(QPalette.Button, QColor(PANEL))
    p.setColor(QPalette.ButtonText, QColor(INK))
    p.setColor(QPalette.BrightText, QColor(ACCENT))
    p.setColor(QPalette.Highlight, QColor(ACCENT))
    p.setColor(QPalette.HighlightedText, QColor(BG))
    p.setColor(QPalette.ToolTipBase, QColor(BAND))
    p.setColor(QPalette.ToolTipText, QColor(INK))
    p.setColor(QPalette.Link, QColor(ACCENT))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        p.setColor(QPalette.Disabled, role, QColor(SOFT))
    return p


def apply(app):
    """Give a QApplication the BananaOS look. Call before the window is made."""
    family = load_fonts()
    font = QFont(family) if family else QFont()
    font.setPointSize(10)
    app.setFont(font)
    app.setPalette(palette())
    app.setStyleSheet(stylesheet(family))
    return family
