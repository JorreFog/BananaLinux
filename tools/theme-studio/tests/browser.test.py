#!/usr/bin/env python3
"""Banana Theme Studio: the page itself, opened from file:// in a real (headless) Firefox, with no server.

Drives the editor through Firefox's own remote control (tests/marionette.py: nothing to install but Firefox) and
checks what a person would: the page loads cleanly, the menu's engine starts and draws both screens, the buttons
move it, an edit shows in the preview and undo takes it back, the templates are there offline, a system icon picked
from a file shows on the shelf, Export makes a .zip that opens again with its files, broken JSON is refused, a big
theme stays quick, the guide opens, and the last theme is offered again after a reload.

    python3 tools/theme-studio/tests/browser.test.py          (SHOTS=<dir>: keep screenshots)

Skips (exit 0) when Firefox isn't installed.
"""
import base64
import json
import os
import shutil
import struct
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from marionette import Firefox  # noqa: E402

PAGE = (HERE.parent / "index.html").as_uri()
fails = []


def check(ok, what, detail=""):
    if ok:
        print("  ok   " + what)
    else:
        print("  FAIL " + what + (" — " + str(detail) if detail else ""))
        fails.append(what)


def png(w, h, rgb):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    raw = b"".join(b"\0" + bytes(rgb) * w for _ in range(h))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


# a pixel of a preview screen (0 top, 1 bottom) at panel coordinates
PIXEL = "const c = document.querySelectorAll('canvas.scr')[arguments[0]]; const d = c.getContext('2d').getImageData(arguments[1], arguments[2], 1, 1).data; return [d[0], d[1], d[2]];"
# a File made in the page from base64 bytes
FILE = "const b = Uint8Array.from(atob(arguments[0]), (c) => c.charCodeAt(0)); return new File([b], arguments[1], { type: arguments[2] || '' });"


# script errors are collected; alert() is recorded and confirm() says yes, as a person pressing OK would
HOOKS = ("window.__errs = []; window.addEventListener('error', (e) => window.__errs.push(e.message + ' @' + e.lineno));"
         "window.addEventListener('unhandledrejection', (e) => window.__errs.push('promise: ' + e.reason));"
         "window.__alerts = []; window.alert = (m) => { window.__alerts.push(String(m)); }; window.confirm = () => true;")


def settle(ff, ms=400):
    time.sleep(ms / 1000)


def main():
    if not shutil.which("firefox"):
        print("  (skipped: no firefox to load the page with)")
        return 0
    shots = os.environ.get("SHOTS")
    with Firefox() as ff:
        ff.go(PAGE)
        ff.js(HOOKS)
        state = ff.wait("return BananaEngine.state !== 'loading' && BananaEngine.state", 90)
        check(state == "ready", "the engine starts from file:// (wasm/banana-engine.js)", ff.js("return BananaEngine.detail"))
        if state != "ready":
            return 1
        ff.wait("return BananaStudio.live.pushed !== ''", 20)
        settle(ff, 600)
        check(ff.js("return document.querySelectorAll('canvas.scr').length") == 2, "the preview has both screens")
        top = ff.js(PIXEL, 0, 5, 5)
        check(top == [0x10, 0x13, 0x1c], "the new theme's background is drawn on the top screen", top)
        check(len(ff.js("return BananaStudio.state.templates") or []) >= 4, "the example themes are there with no server")
        check("file:" in ff.js("return location.protocol"), "the page is opened from a file")

        # the buttons move the menu
        w0 = ff.js("return BananaEngine.where()")
        ff.js("BananaStudio.press('right')")
        settle(ff, 500)
        w1 = ff.js("return BananaEngine.where()")
        check(w1["system"] == w0["system"] + 1, "RIGHT moves to the next system", (w0, w1))
        ff.js("BananaStudio.press('a')")
        settle(ff, 700)
        check(ff.js("return BananaEngine.where().view") == "library", "A opens the library")
        ff.js("BananaStudio.press('b')")
        settle(ff, 500)
        check(ff.js("return BananaEngine.where().view") == "home", "B goes back home")

        # an edit shows in the preview at once; undo takes it back
        ff.js("BananaStudio.state.theme.vars.bg = '#ff0000'; BananaStudio.markDirty();")
        settle(ff, 600)
        check(ff.js(PIXEL, 0, 5, 5) == [255, 0, 0], "an edit (vars.bg) shows in the preview", ff.js(PIXEL, 0, 5, 5))
        ff.js("BananaStudio.undo()")
        settle(ff, 600)
        check(ff.js(PIXEL, 0, 5, 5) == [0x10, 0x13, 0x1c] and ff.js("return BananaStudio.state.theme.vars.bg") == "#10131c", "undo takes it back")
        ff.js("BananaStudio.redo()")
        settle(ff, 300)
        check(ff.js("return BananaStudio.state.theme.vars.bg") == "#ff0000", "redo puts it back")
        ff.js("BananaStudio.undo()")

        # what is saved: the empty members the panels make are left out, the theme's own are kept
        out = ff.js("BananaStudio.renderAll(); return BananaStudio.themeOut()")
        check(not any(k in out for k in ("hints", "input", "icons", "sounds", "background", "components")) and "cues" not in out.get("behavior", {}),
              "Save leaves out the empty members the panels make (hints, input, cues...)", sorted(out.keys()))
        check("launch" not in out["screens"] and "all" not in out["screens"], "Save leaves out the views it only showed", list(out["screens"].keys()))
        kept = ff.js("BananaStudio.loadTheme({ name: 'k', hints: { home: [] }, screens: { home: { top: [] } } }); BananaStudio.renderAll(); return BananaStudio.themeOut()")
        check(kept.get("hints") == {"home": []} and kept["screens"]["home"] == {"top": []}, "...and keeps the ones the theme had (no hints)", kept)

        # every example theme loads with no complaints from the engine
        for name in ff.js("return BananaStudio.state.templates.map((t) => t.name)"):
            ff.js("document.querySelector('[data-tab=theme]').click();"
                  "Array.from(document.querySelectorAll('#pane-theme button')).find((b) => b.textContent === arguments[0]).click();", name)
            settle(ff, 700)
            errs = ff.js("return BananaEngine.errors()")
            check(ff.js("return BananaStudio.state.theme.name") == name and not errs, "the " + name + " template opens and draws cleanly", errs)
        if shots:
            ff.screenshot(os.path.join(shots, "template.png"))

        # a theme problem is reported under Problems, with a count on its tab
        ff.js("BananaStudio.loadTheme({ name: 'bad', screens: { home: { top: [ { type: 'text', text: '=1 +', x: 'nope' } ] } } })")
        settle(ff, 1300)
        tab = ff.js("return document.querySelector('.tabs[data-group=bottom] [data-tab=problems]').textContent")
        check(tab.startswith("Problems (") and "theme:" in ff.js("return document.querySelector('#log').innerText"), "the engine's complaints show under Problems", tab)

        # system icons: every system listed with the menu's own icon; a picked file shows on the shelf
        ff.js("BananaStudio.state.unsaved = false; document.querySelector('[data-tab=theme]').click();"
              "Array.from(document.querySelectorAll('#pane-theme button')).find((b) => b.textContent === 'Canvas').click();")
        settle(ff, 500)
        rows = ff.js("return Array.from(document.querySelectorAll('#pane-icons .icon-row')).map((r) => [r.querySelector('b').textContent, !!r.querySelector('.ic img')])")
        ids = [r[0] for r in rows]
        conf = [ln.strip()[1:-1] for ln in (HERE.parents[2] / "system/rootfs/etc/banana/systems.conf").read_text().splitlines() if ln.strip().startswith("[")]
        check(all(i in ids for i in conf + ["favorites", "recently-played"]), "the icons panel lists every system in systems.conf and the collections", ids)
        check(all(r[1] for r in rows), "...each with the menu's own icon", [r[0] for r in rows if not r[1]])
        red = base64.b64encode(png(32, 32, (255, 0, 0))).decode()
        ff.js_async("const f = (function () { " + FILE + " })(arguments[0], 'my-gba.png', 'image/png'); BananaStudio.setIcon('gba', f).then(() => arguments[arguments.length - 1](1));", red)
        path = ff.js("return BananaStudio.state.theme.icons && BananaStudio.state.theme.icons.gba")
        check(path == "canvas/icons/gba.png" and ff.js("return !!BananaStudio.state.assets['canvas/icons/gba.png']"), "a picked icon is attached under the theme's folder", path)
        ff.js("BananaStudio.setLibrary(true)")
        settle(ff, 500)
        gba = ff.js("return BananaEngine.shelf().indexOf('gba')")
        ff.js("BananaEngine.home(arguments[0]); BananaStudio.live.force = true;", gba)
        settle(ff, 700)
        icon = ff.js(PIXEL, 0, 320, 150)
        check(icon == [255, 0, 0], "the preview shows the theme's icon for gba", icon)
        if shots:
            ff.screenshot(os.path.join(shots, "icon.png"))

        # Export: a .zip with the theme and its files, which opens again as it was
        z = ff.js_async("BananaZip.read(BananaStudio.zipBytes()).then((e) => arguments[arguments.length - 1](e.map((x) => [x.path, x.data.length])))")
        check(sorted(p for p, n in z) == ["canvas.json", "canvas/icons/gba.png"], "Export packs the theme and its files", z)
        ff.js_async("const z = BananaStudio.zipBytes(); BananaStudio.state.unsaved = false; BananaStudio.loadTheme(null);"
                    "BananaStudio.openFiles([new File([z], 'canvas.zip')]).then(() => arguments[arguments.length - 1](1));")
        settle(ff, 600)
        back = ff.js("return [BananaStudio.state.theme.name, BananaStudio.state.theme.icons && BananaStudio.state.theme.icons.gba, Object.keys(BananaStudio.state.assets)]")
        check(back[0] == "Canvas" and back[1] == "canvas/icons/gba.png" and back[2] == ["canvas/icons/gba.png"], "the .zip opens again with its files", back)
        ff.js("BananaEngine.home(arguments[0]); BananaStudio.live.force = true;", gba)
        settle(ff, 700)
        check(ff.js(PIXEL, 0, 320, 150) == [255, 0, 0], "...and the preview shows the icon again")
        ff.js("BananaStudio.resetIcon('gba')")
        settle(ff, 700)
        ff.js("BananaEngine.home(arguments[0]); BananaStudio.live.force = true;", gba)
        settle(ff, 500)
        check(ff.js(PIXEL, 0, 320, 150) != [255, 0, 0] and not ff.js("return Object.keys(BananaStudio.state.assets).length"), "Reset puts the menu's icon back and drops the file")

        # a theme opened with its pictures as loose files (the .json and the pictures picked together)
        ff.js_async("const j = JSON.stringify({ name: 'Loose', background: { top: 'loose/wall.png' }, screens: { home: { top: [ { type: 'image', src: 'loose/wall.png', w: 640, h: 480, fit: 'fill' } ] } } });"
                    "const p = (function () { " + FILE + " })(arguments[0], 'wall.png', 'image/png');"
                    "BananaStudio.state.unsaved = false; BananaStudio.openFiles([new File([j], 'loose.json'), p]).then(() => arguments[arguments.length - 1](1));",
                    base64.b64encode(png(8, 8, (0, 0, 255))).decode())
        settle(ff, 800)
        check(ff.js("return Object.keys(BananaStudio.state.assets)") == ["loose/wall.png"] and ff.js(PIXEL, 0, 100, 100) == [0, 0, 255],
              "a .json opened with its pictures attaches them where it names them, and draws them", ff.js(PIXEL, 0, 100, 100))

        # broken JSON: refused with where it broke, the theme untouched
        name = ff.js("return BananaStudio.state.theme.name")
        ff.js("document.querySelector('[data-tab=json]').click(); const ta = document.querySelector('#json textarea'); ta.value = '{\\n  \"name\": \"x\",\\n  \"screens\": { oops }\\n}'; document.querySelector('#json button.primary').click();")
        err = ff.js("return document.querySelector('#json .jerr').textContent")
        check(err.startswith("Not applied") and "line 3" in err and ff.js("return BananaStudio.state.theme.name") == name, "broken JSON in the JSON tab is refused, with its line", err)
        ff.js_async("BananaStudio.openFiles([new File(['{ \"screens\": '], 'broken.json')]).then(() => arguments[arguments.length - 1](1));")
        said = ff.js("return window.__alerts.pop() || ''")
        check(ff.js("return BananaStudio.state.theme.name") == name and "broken.json" in said, "a broken .json isn't opened, and says why", said)
        lc = ff.js("BananaStudio.loadTheme(null); document.querySelector('[data-tab=json]').click(); const ta = document.querySelector('#json textarea');"
                   "ta.value = '{ /* a\\n comment */ \"name\": \"C\", // more\\n \"screens\": { \"home\": { \"top\": [], }, }, }'; document.querySelector('#json button.primary').click();"
                   "return BananaStudio.state.theme.name")
        check(lc == "C", "comments and trailing commas are read, as the engine reads them", lc)
        ff.js("document.querySelector('[data-tab=design]').click()")

        # a big theme: a thousand elements stay quick to open, edit and preview
        t = ff.js("const els = []; for (let i = 0; i < 1500; i++) els.push({ type: 'rect', x: i % 640, y: (i * 7) % 480, w: 8, h: 8, fill: '=mix(\"#ff0000\", \"#0000ff\", ' + (i / 1500) + ')' });"
                  "const t0 = performance.now(); BananaStudio.loadTheme({ name: 'Big', screens: { home: { top: els, bottom: els.slice(0, 500) } } });"
                  "const t1 = performance.now(); BananaStudio.state.theme.screens.home.top[3].x = 100; BananaStudio.markDirty(); BananaStudio.pushTheme(true); const t2 = performance.now();"
                  "return [t1 - t0, t2 - t1];")
        check(t[0] < 3000 and t[1] < 2000, "a 2000-element theme opens in %.0f ms and an edit reaches the preview in %.0f ms" % (t[0], t[1]), t)

        # 1024 x 768 (the RG DS Plus)
        ff.js("const s = document.querySelector('#preview-controls select'); s.value = '1024x768'; s.dispatchEvent(new Event('change'));")
        settle(ff, 800)
        check(ff.js("return document.querySelector('canvas.scr').width") == 1024, "the preview draws at the RG DS Plus's 1024 × 768")

        # stills: banana-ui --headless's script, in the page
        ff.js("BananaStudio.loadTheme(null); BananaStudio.stills();")
        n = ff.js("return (BananaStudio.state.shots || []).length")
        check(n == 5 and ff.js("return BananaStudio.state.shots[0].top.startsWith('data:image/png')"), "Take stills draws the script's shots", n)

        # the guide
        ff.js("document.querySelector('#guideBtn').click()")
        g = ff.js("return document.querySelector('#guide .md') ? document.querySelector('#guide .md').innerText.slice(0, 40) : ''")
        ff.js("document.querySelector('#guide a[href=\"THEMES.md\"]').click()")
        ref = ff.js("return document.querySelector('#guide .md h2') ? document.querySelector('#guide .md h2').textContent : ''")
        check(g.startswith("Make your own theme") and ref == "Layout themes", "Guide opens the guides, and their links work", (g, ref))
        ff.js("document.querySelector('#guide').remove()")

        # the last theme is offered again after a reload
        ff.js("BananaStudio.loadTheme({ name: 'Kept', screens: { home: { top: [ { type: 'rect', fill: '#00ff00' } ] } } }); BananaStudio.markDirty();")
        settle(ff, 1500)
        ff.go(PAGE)
        ff.js(HOOKS)
        ff.wait("return BananaEngine.state === 'ready'", 60)
        banner = ff.wait("const b = document.querySelector('#banner'); return b && !b.hidden && b.textContent", 10)
        check(banner and "Kept" in banner, "after a reload, the last theme is offered again", banner)
        if banner:
            ff.js("document.querySelector('#banner button.primary').click()")
            settle(ff, 800)
            check(ff.js("return BananaStudio.state.theme.name") == "Kept" and ff.js(PIXEL, 0, 5, 5) == [0, 255, 0], "...and opens with its preview")

        errs = ff.js("return window.__errs || []")
        check(not errs, "no script errors", errs)
        if shots:
            ff.screenshot(os.path.join(shots, "last.png"))
    print("  %d problem(s)" % len(fails) if fails else "  the page works from file://")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
