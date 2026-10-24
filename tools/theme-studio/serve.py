#!/usr/bin/env python3
"""Banana Theme Studio: the preview server.

Serves the editor (index.html, app.js, ...) and adds exact previews: POST a theme to /api/render and it writes
the theme to a file, runs the real engine

    ui/banana-ui --headless OUT --size 640x480 --assets ui/assets/rnds --banana ui/assets/banana
        --systems system/rootfs/etc/banana/systems.conf --roms <fixture>/roms --data <fixture>/data
        --theme <theme.json> --script "home 0; wait 900; shot home; ..."

and returns the PNGs it drew (top and bottom per shot) plus the "banana-ui: theme: ..." lines it printed. The
preview is the device's own code, so it can't drift from the engine.

    python3 tools/theme-studio/serve.py [--port 8777] [--size 640x480] [--no-build]

Nothing here is needed to edit a theme: open tools/theme-studio/index.html directly and the editor works offline.
"""
import argparse
import base64
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]					# tools/theme-studio/serve.py -> the repo root
UI = ROOT / "ui"
ENGINE = UI / "banana-ui"
SYSTEMS = ROOT / "system" / "rootfs" / "etc" / "banana" / "systems.conf"
FIXTURE = Path(tempfile.gettempdir()) / "banana-theme-studio-fx"
DEFAULT_SCRIPT = ("home 0; wait 900; shot home; press right; wait 700; shot home2; "
                  "press a; wait 900; shot lib; press down; wait 600; shot lib2; press a; wait 400; shot launch")
TEMPLATE_DIRS = [ROOT / "system" / "rootfs" / "usr" / "share" / "banana" / "themes", ROOT / "docs" / "themes"]
THEMES_DIR = None						# --themes-dir: where "Send to handheld" writes (a mounted banana/themes)
LOCAL_THEMES = None						# --themes: your own themes folder, outside the repo (list and save here)

TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
         ".css": "text/css; charset=utf-8", ".json": "application/json; charset=utf-8",
         ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".svg": "image/svg+xml",
         ".md": "text/markdown; charset=utf-8"}

_build_lock = threading.Lock()
_ready = {"engine": False, "fixture": False, "detail": ""}


def ensure_engine(build=True):
    if ENGINE.exists():
        return True
    if not build:
        _ready["detail"] = "ui/banana-ui isn't built (run `make -C ui host`)"
        return False
    with _build_lock:
        if ENGINE.exists():
            return True
        try:
            subprocess.run(["make", "-C", str(UI), "host"], check=True,
                           stdout=sys.stderr, stderr=sys.stderr)
        except Exception as e:					# noqa: BLE001
            _ready["detail"] = "couldn't build ui/banana-ui: %s" % e
            return False
    return ENGINE.exists()


def ensure_fixture():
    if (FIXTURE / "roms").exists():
        return True
    script = UI / "tests" / "fixture.sh"
    if not script.exists():
        _ready["detail"] = "ui/tests/fixture.sh is missing"
        return False
    try:
        subprocess.run(["sh", str(script), str(FIXTURE)], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception as e:					# noqa: BLE001
        _ready["detail"] = "couldn't build the test library: %s" % e
        return False
    return (FIXTURE / "roms").exists()


def shot_names(script):
    seen, out = set(), []
    for n in re.findall(r"\bshot\s+([A-Za-z0-9_.-]+)", script):
        if n not in seen:
            seen.add(n)
            out.append(n)
    return out


def safe_rel(p):
    """A theme-relative asset path, or None if it is absolute or escapes its folder."""
    p = str(p or "").replace("\\", "/")
    if p.startswith("/") or p.startswith("~"):
        return None
    parts = [x for x in p.split("/") if x not in ("", ".")]
    if not parts or any(x == ".." for x in parts):
        return None
    return "/".join(parts)


def write_assets(base, assets):
    """Write the editor's attached asset files beside the theme, at their relative paths."""
    written = []
    for a in assets or []:
        rel = safe_rel(a.get("path", ""))
        if not rel:
            continue
        data = a.get("data", "") or ""
        if data.strip().startswith("data:") and "," in data:
            data = data.split(",", 1)[1]
        try:
            raw = base64.b64decode(data, validate=False)
        except Exception:					# noqa: BLE001
            continue
        dst = Path(base) / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(raw)
        written.append(rel)
    return written


def render(theme_text, script, size, assets=None):
    if not ensure_engine():
        return {"error": _ready["detail"] or "the engine isn't built"}
    ensure_fixture()
    work = Path(tempfile.mkdtemp(prefix="banana-studio-"))
    try:
        theme_path = work / "theme.json"
        theme_path.write_text(theme_text, encoding="utf-8")
        write_assets(work, assets)				# the theme's own pictures/fonts/sounds, if the editor has them
        prefix = work / "out-"
        cmd = [str(ENGINE), "--headless", str(prefix), "--size", size,
               "--assets", str(UI / "assets" / "rnds"), "--banana", str(UI / "assets" / "banana"),
               "--systems", str(SYSTEMS), "--roms", str(FIXTURE / "roms"), "--data", str(FIXTURE / "data"),
               "--theme", str(theme_path), "--script", script]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
        except subprocess.TimeoutExpired:
            return {"error": "the engine took too long (60 s)"}
        log = (proc.stdout or "") + (proc.stderr or "")
        errors = [ln.strip() for ln in log.splitlines() if "theme:" in ln]
        shots = []
        for name in shot_names(script):
            shot = {"name": name}
            for side, key in (("top", "top"), ("bot", "bot")):
                p = Path(str(prefix) + name + "-" + side + ".png")
                if p.exists():
                    shot[key] = "data:image/png;base64," + base64.b64encode(p.read_bytes()).decode("ascii")
            if "top" in shot or "bot" in shot:
                shots.append(shot)
        if not shots:
            return {"error": "no shot was drawn", "log": log, "errors": errors}
        return {"shots": shots, "log": log[-8000:], "errors": errors}
    finally:
        shutil.rmtree(work, ignore_errors=True)


def system_ids():
    """The system ids the menu ships, from systems.conf (for the editor's icon picker)."""
    ids = []
    try:
        for line in SYSTEMS.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line.startswith("[") and line.endswith("]"):
                sid = line[1:-1]
                if sid and sid not in ids:
                    ids.append(sid)
    except OSError:
        pass
    return ids


def local_themes():
    """The themes in --themes (your own folder, outside the repo), newest name first."""
    if not LOCAL_THEMES or not LOCAL_THEMES.exists():
        return []
    out = []
    for f in sorted(LOCAL_THEMES.glob("*.json")):
        try:
            text = f.read_text(encoding="utf-8")
        except OSError:
            continue
        m = re.search(r'"name"\s*:\s*"([^"]*)"', text)
        out.append({"name": m.group(1) if m else f.stem, "file": str(f), "json": text})
    return out


def templates():
    out = []
    for d in TEMPLATE_DIRS:
        if not d.exists():
            continue
        for f in sorted(d.glob("*.json")):
            try:
                text = f.read_text(encoding="utf-8")
            except OSError:
                continue
            if not re.search(r'"screens"\s*:', text):		# layout themes only: the editor edits those
                continue
            m = re.search(r'"name"\s*:\s*"([^"]*)"', text)
            out.append({"name": m.group(1) if m else f.stem, "file": str(f), "json": text})
    return out


class Handler(BaseHTTPRequestHandler):
    server_version = "BananaThemeStudio"

    def log_message(self, fmt, *args):				# keep the console quiet
        pass

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("content-type", ctype)
        self.send_header("content-length", str(len(body)))
        self.send_header("cache-control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code, obj):
        self._send(code, json.dumps(obj))

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            return self._file(HERE / "index.html")
        if path == "/api/health":
            return self._json(200, {"ok": ensure_engine() and True, "engine": str(ENGINE),
                                    "fixture": str(FIXTURE), "detail": _ready["detail"],
                                    "themesDir": str(THEMES_DIR) if THEMES_DIR else "",
                                    "localThemes": str(LOCAL_THEMES) if LOCAL_THEMES else ""})
        if path == "/api/templates":
            return self._json(200, templates())
        if path == "/api/themes":
            return self._json(200, local_themes())
        if path == "/api/systems":
            return self._json(200, system_ids())
        if path == "/api/schema":
            p = ROOT / "docs" / "theme.schema.json"
            return self._file(p) if p.exists() else self._json(404, {"error": "no schema"})
        # static, confined to this directory
        rel = os.path.normpath(path).lstrip("/")
        target = (HERE / rel).resolve()
        if not str(target).startswith(str(HERE.resolve())) or not target.exists():
            return self._send(404, "not found", "text/plain")
        return self._file(target)

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path not in ("/api/render", "/api/send", "/api/themes/save"):
            return self._send(404, "not found", "text/plain")
        try:
            n = int(self.headers.get("content-length", "0"))
            req = json.loads(self.rfile.read(n) or b"{}")
        except Exception as e:					# noqa: BLE001
            return self._json(400, {"error": "bad request: %s" % e})
        theme = req.get("theme")
        if theme is None:
            return self._json(400, {"error": "no theme in the request"})
        text = theme if isinstance(theme, str) else json.dumps(theme, indent=2)
        if path == "/api/send":
            return self._send_theme(req, text)
        if path == "/api/themes/save":
            return self._save_local(req, text)
        result = render(text, req.get("script") or DEFAULT_SCRIPT, req.get("size") or "640x480",
                        req.get("assets"))
        return self._json(200 if "error" not in result or "shots" in result else 500, result)

    def _send_theme(self, req, text):
        if not THEMES_DIR:
            return self._json(400, {"error": "no --themes-dir was given to serve.py, so there is nowhere to send it"})
        name = safe_rel(req.get("name") or "theme") or "theme"
        if not name.endswith(".json"):
            name += ".json"
        try:
            THEMES_DIR.mkdir(parents=True, exist_ok=True)
            (THEMES_DIR / name).write_text(text, encoding="utf-8")
            written = write_assets(THEMES_DIR, req.get("assets"))
        except OSError as e:
            return self._json(500, {"error": "couldn't write to %s: %s" % (THEMES_DIR, e)})
        return self._json(200, {"ok": True, "path": str(THEMES_DIR / name), "assets": written})

    def _save_local(self, req, text):
        if not LOCAL_THEMES:
            return self._json(400, {"error": "no --themes folder was given to serve.py"})
        name = safe_rel(req.get("name") or "theme") or "theme"
        if not name.endswith(".json"):
            name += ".json"
        try:
            LOCAL_THEMES.mkdir(parents=True, exist_ok=True)
            (LOCAL_THEMES / name).write_text(text, encoding="utf-8")
            written = write_assets(LOCAL_THEMES, req.get("assets"))
        except OSError as e:
            return self._json(500, {"error": "couldn't write to %s: %s" % (LOCAL_THEMES, e)})
        return self._json(200, {"ok": True, "path": str(LOCAL_THEMES / name), "assets": written})

    def _file(self, p):
        try:
            data = Path(p).read_bytes()
        except OSError as e:
            return self._send(404, "not found: %s" % e, "text/plain")
        self._send(200, data, TYPES.get(Path(p).suffix.lower(), "application/octet-stream"))


def main():
    ap = argparse.ArgumentParser(description="Banana Theme Studio preview server")
    ap.add_argument("--port", type=int, default=8777)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--size", default="640x480", help="640x480 (RG DS) or 1024x768 (RG DS Plus)")
    ap.add_argument("--no-build", action="store_true", help="don't try to build ui/banana-ui")
    ap.add_argument("--no-browser", action="store_true")
    ap.add_argument("--themes-dir", default=None,
                    help="a directory to 'Send to handheld' into (a mounted banana/themes, e.g. "
                         "/run/media/you/BANANA/banana/themes); without it the Send button is hidden")
    ap.add_argument("--themes", default=None,
                    help="your own themes folder, outside the repo (e.g. ~/BananaThemes): the editor lists its "
                         "themes and saves into it, so your themes never touch the repo or its example list")
    args = ap.parse_args()

    global THEMES_DIR, LOCAL_THEMES
    if args.themes_dir:
        THEMES_DIR = Path(args.themes_dir).expanduser().resolve()
    if args.themes:
        LOCAL_THEMES = Path(args.themes).expanduser().resolve()

    ensure_engine(build=not args.no_build)
    if ENGINE.exists():
        ensure_fixture()

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    url = "http://%s:%d/" % (args.host, args.port)
    print("Banana Theme Studio: %s" % url, file=sys.stderr)
    if ENGINE.exists():
        print("  previews: the real engine (%s)" % ENGINE, file=sys.stderr)
    else:
        print("  editing only: %s" % (_ready["detail"] or "ui/banana-ui isn't built"), file=sys.stderr)
    if not args.no_browser:
        try:
            webbrowser.open(url)
        except Exception:					# noqa: BLE001
            pass
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye", file=sys.stderr)


if __name__ == "__main__":
    main()