"""The exact preview: the real banana-ui rendering the theme headless (docs/THEMES.md "Trying a theme on a computer").

Needs a BananaLinux checkout with `make ui` done (the studio can run the build) and ImageMagick for the test library.
"""
import os
import shutil
import tempfile

from PySide6.QtCore import QObject, QProcess, Signal

import jsonc


def cache_dir():
    d = os.path.join(os.environ.get('XDG_CACHE_HOME', os.path.expanduser('~/.cache')), 'banana-theme-studio')
    os.makedirs(d, exist_ok=True)
    return d


def find_repo(hint=''):
    """A BananaLinux checkout with the layout theme engine (ui/src/layout.cpp)"""
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [hint, os.environ.get('BANANA_REPO', ''), os.path.join(here, '..', '..'),
             os.path.expanduser('~/bl-theme-studio'), os.path.expanduser('~/BananaLinux')]
    for c in cands:
        if c and os.path.exists(os.path.join(c, 'ui', 'src', 'layout.cpp')):
            return os.path.abspath(c)
    for c in cands:
        if c and os.path.exists(os.path.join(c, 'ui', 'assets')):
            return os.path.abspath(c)
    return ''


class Exact(QObject):
    """Runs banana-ui --headless; finished(top_png, bottom_png, theme_errors, log) or failed(message)"""
    finished = Signal(str, str, list, str)
    failed = Signal(str)
    built = Signal(bool, str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.repo = ''
        self.proc = None
        self.pending = None
        self.build_proc = None
        self.tmp = tempfile.mkdtemp(prefix='banana-studio-')

    def binary(self):
        b = os.path.join(self.repo, 'ui', 'banana-ui') if self.repo else ''
        return b if b and os.access(b, os.X_OK) else ''

    def ready(self):
        return bool(self.binary())

    def fixture(self):
        d = os.path.join(cache_dir(), 'fixture')
        if os.path.exists(os.path.join(d, 'roms')):
            return d
        script = os.path.join(self.repo, 'ui', 'tests', 'fixture.sh')
        if not os.path.exists(script):
            return ''
        if not shutil.which('convert'):
            return ''
        r = os.system('cd %s && sh %s %s >/dev/null 2>&1' % (_q(self.repo), _q(script), _q(d)))
        return d if r == 0 and os.path.exists(os.path.join(d, 'roms')) else ''

    def build(self):
        """make the headless banana-ui (with <cstdint> forced in: some GCCs need it for expr.h)"""
        if not self.repo or self.build_proc:
            return
        self.build_proc = QProcess(self)
        self.build_proc.setWorkingDirectory(os.path.join(self.repo, 'ui'))
        self.build_proc.setProcessChannelMode(QProcess.MergedChannels)

        def done(code, _status):
            out = bytes(self.build_proc.readAll()).decode(errors='replace')
            self.build_proc = None
            self.built.emit(code == 0 and self.ready(), out[-4000:])
        self.build_proc.finished.connect(done)
        self.build_proc.start('make', ['ALSA=0', 'CXXFLAGS=-O2 -include cstdint', 'banana-ui'])

    def render(self, doc, theme_dir, view, sys_index, game_index, wait_ms=1200, launch=0.5, launch_ms=900):
        """Renders one view; a render asked for while one runs replaces the waiting one"""
        if self.proc is not None:
            self.pending = (doc, theme_dir, view, sys_index, game_index, wait_ms, launch, launch_ms)
            return
        if not self.ready():
            self.failed.emit('The engine is not built (Engine > Build banana-ui).')
            return
        fx = self.fixture()
        if not fx:
            self.failed.emit('No test library: ui/tests/fixture.sh needs ImageMagick (convert).')
            return
        # the theme's own files are relative to its JSON: the preview copy goes next to it
        d = theme_dir if theme_dir and os.access(theme_dir, os.W_OK) else self.tmp
        self.theme_file = os.path.join(d, '.studio-preview.json')
        with open(self.theme_file, 'w', encoding='utf-8') as f:
            f.write(jsonc.dumps(doc))
        if view == 'home':
            script = 'home %d; wait %d; shot s' % (sys_index, wait_ms)
        elif view == 'library':
            script = 'lib %d %d; wait %d; shot s' % (sys_index, game_index, wait_ms)
        else:
            script = 'lib %d %d; wait %d; press a; wait %d; shot s' % (sys_index, game_index, wait_ms, max(1, int(launch * launch_ms)))
        prefix = os.path.join(self.tmp, 'shot-')
        for side in ('top', 'bot'):
            try:
                os.remove(prefix + 's-%s.png' % side)
            except OSError:
                pass
        ui = os.path.join(self.repo, 'ui')
        args = ['--headless', prefix, '--size', '640x480', '--assets', os.path.join(ui, 'assets', 'rnds'),
                '--banana', os.path.join(ui, 'assets', 'banana'),
                '--systems', os.path.join(self.repo, 'system', 'rootfs', 'etc', 'banana', 'systems.conf'),
                '--roms', os.path.join(fx, 'roms'), '--data', os.path.join(fx, 'data'),
                '--theme', self.theme_file, '--script', script]
        self.proc = QProcess(self)
        self.proc.setProcessChannelMode(QProcess.MergedChannels)
        self.proc.finished.connect(lambda code, st: self._done(prefix))
        self.proc.errorOccurred.connect(lambda e: self._done(prefix) if self.proc and self.proc.state() == QProcess.NotRunning else None)
        self.proc.start(self.binary(), args)

    def _done(self, prefix):
        if self.proc is None:
            return
        out = bytes(self.proc.readAll()).decode(errors='replace')
        self.proc = None
        try:
            os.remove(self.theme_file)
        except OSError:
            pass
        errors = [l.split('banana-ui: theme: ', 1)[1] for l in out.splitlines() if 'banana-ui: theme: ' in l]
        top, bot = prefix + 's-top.png', prefix + 's-bot.png'
        if os.path.exists(top) and os.path.exists(bot):
            self.finished.emit(top, bot, errors, out)
        else:
            self.failed.emit('banana-ui made no picture:\n' + out[-2000:])
        if self.pending:
            p, self.pending = self.pending, None
            self.render(*p)


def _q(s):
    return "'" + s.replace("'", "'\\''") + "'"
