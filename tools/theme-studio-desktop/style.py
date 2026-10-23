"""The BananaOS website's look (web/preview) for the desktop Banana Theme Studio.

Charcoal panels with a cream border and a lighter band along the top, banana yellow for the selection, Pixelify
Sans, and the menu's dot pattern — or, with the light theme, the site's cream palette. The QPalette (Fusion)
covers the widgets the stylesheet doesn't name; the stylesheet does the rest, so the desktop studio matches the
web editor (tools/theme-studio), which has the same dark/light switch.

    import style
    style.apply(app, light)          # at start, on the QApplication, before the window is made
    style.set_theme(app, light)      # when the switch is flipped

The fonts and patterns in assets/ are copies of web/preview/assets/.
"""
import os

from PySide6.QtGui import QColor, QFont, QFontDatabase, QPalette

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, 'assets')

# web/preview/css/style.css: :root (dark) and :root[data-theme="light"]
DARK = dict(bg='#17161c', panel='#191820', band='#38333b', line='#e8e2d3', ink='#f4f1e8', soft='#a39d8e',
            text='#d8d2c4', field='#121117', alt='#1f1e26', accent='#f0b429', onaccent='#191820',
            tileline='rgba(232, 226, 211, 140)', pattern='menu-pattern.png')
LIGHT = dict(bg='#f3efe6', panel='#faf7f0', band='#efe9dc', line='#3a342c', ink='#2a2620', soft='#7a705f',
             text='#4a4338', field='#ffffff', alt='#f0ece2', accent='#f0b429', onaccent='#191820',
             tileline='rgba(58, 52, 44, 115)', pattern='menu-pattern-light.png')

_family = None						# the registered font family, remembered for set_theme


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


def stylesheet(family, c):
    fields = dict(c)
    fields['fam'] = family or 'sans-serif'
    fields['pattern'] = _url(c['pattern'])
    return """
QWidget { font-family: "%(fam)s"; font-size: 13px; color: %(ink)s; }
QMainWindow, QDialog { background-color: %(bg)s; background-image: %(pattern)s; background-repeat: repeat; background-position: top left; }
QToolTip { background: %(band)s; color: %(ink)s; border: 2px solid %(line)s; padding: 4px; }

QDockWidget { color: %(ink)s; }
QDockWidget::title { background: %(band)s; color: %(ink)s; padding: 7px 10px; border: 2px solid %(line)s; }
QDockWidget > QWidget { background: %(panel)s; border: 2px solid %(line)s; border-top: none; }

QTabWidget::pane { border: 2px solid %(line)s; background: %(panel)s; top: -2px; }
QTabBar::tab { background: %(panel)s; color: %(ink)s; border: 2px solid %(line)s; border-bottom: none;
    padding: 5px 12px; margin-right: 2px; border-top-left-radius: 7px; border-top-right-radius: 7px; }
QTabBar::tab:selected { background: %(accent)s; color: %(onaccent)s; }
QTabBar::tab:hover:!selected { border-color: %(accent)s; }

QGroupBox { border: 2px solid %(line)s; border-radius: 9px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px; padding: 0 5px; color: %(accent)s; }

QPushButton { background: %(panel)s; color: %(ink)s; border: 2px solid %(line)s; border-radius: 8px; padding: 5px 12px; }
QPushButton:hover { border-color: %(accent)s; }
QPushButton:pressed { background: %(band)s; }
QPushButton:checked, QPushButton:default { background: %(accent)s; color: %(onaccent)s; }
QPushButton:disabled { color: %(soft)s; border-color: %(soft)s; }

QToolButton { background: transparent; border: 2px solid transparent; border-radius: 7px; padding: 4px 8px; }
QToolButton:hover { border-color: %(line)s; }
QToolButton:checked { background: %(accent)s; color: %(onaccent)s; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %(field)s; color: %(ink)s; border: 2px solid %(tileline)s; border-radius: 7px; padding: 3px 7px;
    selection-background-color: %(accent)s; selection-color: %(onaccent)s; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: %(accent)s; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled { color: %(soft)s; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %(panel)s; color: %(ink)s; border: 2px solid %(line)s;
    selection-background-color: %(accent)s; selection-color: %(onaccent)s; }

QTreeWidget, QListWidget, QTableWidget, QTreeView, QListView, QTableView, QToolBox {
    background: %(panel)s; color: %(ink)s; border: 2px solid %(line)s; alternate-background-color: %(alt)s; }
QTreeWidget::item:selected, QListWidget::item:selected, QTreeView::item:selected, QListView::item:selected, QTableView::item:selected { background: %(accent)s; color: %(onaccent)s; }
QTreeWidget::item:hover, QListWidget::item:hover { background: %(band)s; }
QHeaderView::section { background: %(band)s; color: %(ink)s; border: 1px solid %(line)s; padding: 4px; }
QToolBox::tab { background: %(panel)s; color: %(ink)s; border: 2px solid %(line)s; border-radius: 7px; padding: 5px 9px; margin-top: 2px; }
QToolBox::tab:selected { background: %(accent)s; color: %(onaccent)s; }

QMenuBar { background: %(panel)s; color: %(ink)s; border-bottom: 2px solid %(line)s; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected { background: %(accent)s; color: %(onaccent)s; }
QMenu { background: %(panel)s; color: %(ink)s; border: 2px solid %(line)s; }
QMenu::item { padding: 5px 22px; }
QMenu::item:selected { background: %(accent)s; color: %(onaccent)s; }
QMenu::separator { height: 1px; background: %(line)s; margin: 4px 8px; }

QToolBar { background: %(panel)s; border-bottom: 2px solid %(line)s; spacing: 4px; padding: 3px; }
QStatusBar { background: %(panel)s; color: %(soft)s; border-top: 2px solid %(line)s; }

QScrollBar:vertical { background: %(field)s; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: %(band)s; border: 2px solid %(line)s; border-radius: 6px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: %(accent)s; }
QScrollBar:horizontal { background: %(field)s; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: %(band)s; border: 2px solid %(line)s; border-radius: 6px; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: %(accent)s; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSlider::groove:horizontal { height: 6px; background: %(field)s; border: 2px solid %(line)s; border-radius: 5px; }
QSlider::handle:horizontal { background: %(accent)s; border: 2px solid %(line)s; width: 14px; margin: -7px 0; border-radius: 7px; }
QSlider::groove:vertical { width: 6px; background: %(field)s; border: 2px solid %(line)s; border-radius: 5px; }
QSlider::handle:vertical { background: %(accent)s; border: 2px solid %(line)s; height: 14px; margin: 0 -7px; border-radius: 7px; }

QCheckBox, QRadioButton { spacing: 7px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 2px solid %(line)s; border-radius: 4px; background: %(field)s; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: %(accent)s; }
QRadioButton::indicator { border-radius: 9px; }

QProgressBar { border: 2px solid %(line)s; border-radius: 7px; background: %(field)s; text-align: center; }
QProgressBar::chunk { background: %(accent)s; }
QSplitter::handle { background: %(band)s; }
""" % fields


def palette(c):
    """The same colours for the widgets the stylesheet doesn't reach (Fusion)."""
    p = QPalette()
    p.setColor(QPalette.Window, QColor(c['bg']))
    p.setColor(QPalette.WindowText, QColor(c['ink']))
    p.setColor(QPalette.Base, QColor(c['panel']))
    p.setColor(QPalette.AlternateBase, QColor(c['alt']))
    p.setColor(QPalette.Text, QColor(c['ink']))
    p.setColor(QPalette.PlaceholderText, QColor(c['soft']))
    p.setColor(QPalette.Button, QColor(c['panel']))
    p.setColor(QPalette.ButtonText, QColor(c['ink']))
    p.setColor(QPalette.BrightText, QColor(c['accent']))
    p.setColor(QPalette.Highlight, QColor(c['accent']))
    p.setColor(QPalette.HighlightedText, QColor(c['onaccent']))
    p.setColor(QPalette.ToolTipBase, QColor(c['band']))
    p.setColor(QPalette.ToolTipText, QColor(c['ink']))
    p.setColor(QPalette.Link, QColor(c['accent']))
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        p.setColor(QPalette.Disabled, role, QColor(c['soft']))
    return p


def set_theme(app, light=False):
    """Flip between the dark and light palettes on a live QApplication."""
    global _family
    if _family is None:
        _family = load_fonts()
    c = LIGHT if light else DARK
    app.setPalette(palette(c))
    app.setStyleSheet(stylesheet(_family, c))


def apply(app, light=False):
    """Give a QApplication the BananaOS look. Call before the window is made."""
    global _family
    _family = load_fonts()
    font = QFont(_family) if _family else QFont()
    font.setPointSize(10)
    app.setFont(font)
    set_theme(app, light)
    try:						# let the page's painted gradient follow the theme
        app.setProperty('banana_light', bool(light))
    except Exception:					# noqa: BLE001
        pass
    return _family