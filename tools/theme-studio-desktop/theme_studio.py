#!/usr/bin/env python3
"""Banana Theme Studio: a designer for BananaOS layout themes (docs/THEMES.md).

    python3 theme_studio.py [theme.json]

Needs PySide6. With a BananaLinux checkout it also uses its fonts and icons, and can show the real engine's picture.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from app import main

if __name__ == '__main__':
    main()
