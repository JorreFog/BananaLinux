#!/usr/bin/env python3
"""Banana Theme Studio (desktop): a headless smoke test.

Builds the window with the BananaOS look on the offscreen Qt platform, checks Pixelify Sans loaded and the
stylesheet is there, and paints the window once. Catches a broken import or stylesheet without a display.

    python3 tools/theme-studio-desktop/tests/smoke.py

Skips (exit 0) when PySide6 isn't installed.
"""
import os
import sys

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

try:
    from PySide6.QtWidgets import QApplication
except ImportError:
    print('  (skipped: PySide6 is not installed)')
    sys.exit(0)

import style							# noqa: E402


def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')
    family = style.apply(app)
    if not family:
        print('  FAIL Pixelify Sans did not load from assets/')
        return 1
    for name, c in (('dark', style.DARK), ('light', style.LIGHT)):
        if 'QPushButton' not in style.stylesheet(family, c):
            print('  FAIL the %s stylesheet is empty' % name)
            return 1
    style.set_theme(app, True)					# the light/dark switch (Ctrl+T) must not throw
    style.set_theme(app, False)

    import app as studio_app					# noqa: E402  (imports the window, needs a QApplication first)
    w = studio_app.Studio()
    w.resize(1200, 800)
    w.show()
    app.processEvents()
    pm = w.grab()
    if pm.isNull() or pm.width() == 0:
        print('  FAIL the window did not paint')
        return 1
    # the Theme settings panel offers the icons editor
    from PySide6.QtWidgets import QTabWidget			# noqa: E402
    tabs = w.theme_panel.widget().findChild(QTabWidget)
    names = [tabs.tabText(i) for i in range(tabs.count())]
    if 'System icons' not in names:
        print('  FAIL the Theme settings panel has no System icons tab: %s' % names)
        return 1
    print('  ok (font "%s", painted %dx%d, theme tabs: %s)' % (family, pm.width(), pm.height(), ', '.join(names)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
