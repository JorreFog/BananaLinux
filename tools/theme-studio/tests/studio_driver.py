"""Drives the web theme studio the way a person does, in a real (headless) Firefox: the gallery, the Add menus in
Layers, the fields of the Element and Theme tabs (ƒx for expressions), Check, Optimize and Export. It never writes
the theme's JSON itself: where a form can't say something, it uses the studio's own "raw JSON (this element)" box,
and counts that (fallbacks), so the forms' gaps show.

    from studio_driver import Studio
    with Studio() as st:
        st.new("Blank")
        st.theme(name="Night", author="me", font="inter")
        p = st.add("top", "rect")
        st.set(p, x=0, y=0, w="100%", h="100%", fill="=bg")
        st.export("/tmp/out")
"""
import json
import os
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from marionette import Firefox  # noqa: E402

PAGE = (HERE.parent / "index.html").as_uri()
ADD_LABELS = {"rect": "Rectangle", "text": "Text", "image": "Picture", "group": "Group", "list": "List"}
OBJECT_KEYS = {"shadow": ["x", "y", "blur", "spread", "color"], "outline": ["width", "color"]}


class StudioError(Exception):
    pass


class Studio:
    def __init__(self, page=PAGE, width=1500, height=950):
        self.page, self.size = page, (width, height)
        self.fallbacks = []
        self.ff = None

    def __enter__(self):
        self.ff = Firefox(width=self.size[0], height=self.size[1]).__enter__()
        self.ff.go(self.page)
        for _ in range(100):
            if self.js("return !!(window.BananaEngine && BananaEngine.ready())"):
                break
            time.sleep(0.1)
        self.close_dialogs()
        return self

    def __exit__(self, *a):
        self.ff.__exit__(*a)

    def js(self, script, *args):
        return self.ff.js(script, *args)

    def wait(self, ms=150):
        time.sleep(ms / 1000)

    def close_dialogs(self):
        self.js("document.querySelectorAll('.modal').forEach((m) => m.remove()); document.querySelectorAll('.menu.pop').forEach((m) => m.remove());")

    # ---- starting points and the theme's own settings --------------------------------------------------------
    def new(self, template="Blank"):
        self.js("BananaStudio.state.unsaved = false; document.querySelector('#fileMenuBtn').click(); document.querySelector('#newBtn').click();")
        ok = self.js("const b = Array.from(document.querySelectorAll('#templates .tpl')).find((x) => x.querySelector('b').textContent === arguments[0]);"
                     "if (!b) return false; b.click(); return true;", template)
        if not ok:
            raise StudioError("no template " + template)
        self.wait(300)
        self.fallbacks = []

    def tab(self, group, name):
        self.js("document.querySelector('.tabs[data-group=\"' + arguments[0] + '\"] button[data-tab=\"' + arguments[1] + '\"]').click()", group, name)

    def view(self, v):
        """Layers' view (and the preview follows): home, library, launch, all"""
        self.tab("library", "layers")
        idx = ["home", "library", "launch", "all"].index(v)
        self.js("document.querySelectorAll('#pane-elements .viewsel button')[arguments[0]].click()", idx)
        self.wait(120)

    def theme(self, **kv):
        """name, author, font, debug (the Theme tab's General fields); accent: a name or '#rrggbb'; base: dark/light"""
        self.tab("properties", "theme")
        for k in ("name", "author"):
            if k in kv:
                self._field("#pane-theme", k, kv[k])
        if "font" in kv:
            self._field("#pane-theme", "font (default)", kv["font"])
        if "base" in kv:
            self.js("const s = document.querySelector('#pane-colours .field select'); s.value = arguments[0]; s.dispatchEvent(new Event('change'));", kv["base"])
        if "accent" in kv:
            a = kv["accent"]
            named = self.js("return Array.from(document.querySelectorAll('#pane-colours .field')[1].querySelectorAll('option')).map((o) => o.value)")
            if a in named:
                self.js("const s = document.querySelectorAll('#pane-colours .field')[1].querySelector('select'); s.value = arguments[0]; s.dispatchEvent(new Event('change'));", a)
            else:
                self.js("const s = document.querySelectorAll('#pane-colours .field')[1].querySelector('select'); s.value = 'custom'; s.dispatchEvent(new Event('change'));")
                self.js("const c = document.querySelectorAll('#pane-colours .field')[1].querySelector('input[type=color]'); c.value = arguments[0]; c.dispatchEvent(new Event('input'));", a[:7])
        self.wait()

    def var(self, name, value):
        """Theme > Variables: name and value, Add (an =expression or a colour); then the field, for a typed value"""
        self.tab("properties", "theme")
        # (the row at the end: a name, a value and Add)
        self.js("const row = document.querySelector('#pane-vars').lastElementChild; const i = row.querySelectorAll('input[type=text]');"
                "i[0].value = arguments[0]; i[1].value = ''; Array.from(row.querySelectorAll('button')).find((b) => b.textContent === 'add').click();", name)
        if name not in (self.get(["vars"]) or {}):
            raise StudioError("var %s wasn't added" % name)
        self._field("#pane-vars", name, value)

    def color_key(self, key, value):
        """Theme > Settings colours: one key's colour (#rrggbb or #rrggbbaa)"""
        self.tab("properties", "theme")
        ok = self.js("""const row = Array.from(document.querySelectorAll('#pane-colours .key')).find((r) => r.querySelector('.name').textContent === arguments[0]);
            if (!row) return false; const v = arguments[1];
            const c = row.querySelector('input[type=color]'), a = row.querySelector('input[type=range]');
            c.value = v.slice(0, 7); a.value = v.length > 7 ? parseInt(v.slice(7, 9), 16) : 255; c.dispatchEvent(new Event('input')); return true;""", key, value)
        if not ok:
            raise StudioError("no colour key " + key)

    def behavior(self, **kv):
        self.tab("properties", "theme")
        for k, v in kv.items():
            self._field("#pane-behaviour", k, v)

    def button(self, view, button, action):
        """Theme > Buttons and hints: what a button does in a view"""
        self.tab("properties", "theme")
        ok = self.js("""const p = document.querySelector('#pane-input'); let sec = null, out = null;
            for (const el of p.children) { if (el.tagName === 'H3') sec = el.textContent; else if (sec === arguments[0]) { const f = el.querySelector('.field[data-key="' + arguments[1] + '"]'); if (f) { out = f; break; } } }
            if (!out) return false; const s = out.querySelector('select'); s.value = arguments[2]; s.dispatchEvent(new Event('change')); return s.value === arguments[2];""", view, button, action)
        if not ok:
            raise StudioError("button %s/%s: %s" % (view, button, action))

    def component(self, name):
        """Components tab: a new component (a group); returns its path"""
        self.tab("library", "components")
        self.js("const p = document.querySelector('#pane-components'); p.querySelector('input[type=text]').value = arguments[0];"
                "Array.from(p.querySelectorAll('button')).find((b) => b.textContent.trim() === 'Create').click();", name)
        self.wait()
        self.tab("library", "layers")
        return ["components", name]

    # ---- elements ----------------------------------------------------------------------------------------------
    def add(self, where, typ):
        """Adds an element with Layers' Add menus: where is 'top' / 'bottom' (the current view's screens) or the path of
        a group, a list's item or a component (its 'Add inside'). Returns the new element's path."""
        self.tab("library", "layers")
        if where in ("top", "bottom"):
            idx = 0 if where == "top" else 1
            self.js("document.querySelectorAll('#pane-elements .screenhead button')[arguments[0]].click()", idx)
        else:
            self.select(where)
            ok = self.js("""const row = document.querySelector('#pane-elements .row[data-path="' + CSS.escape(arguments[0]) + '"]');
                if (!row) return 'no row'; const kids = row.parentElement.querySelector(':scope > .kids');
                const b = kids && Array.from(kids.querySelectorAll(':scope > .add-child')).pop(); if (!b) return 'no add inside'; b.click(); return 'ok';""", json.dumps(where, separators=(",", ":")))
            if ok != "ok":
                raise StudioError("add inside %s: %s" % (where, ok))
        self.wait(60)
        ok = self.js("const b = Array.from(document.querySelectorAll('.menu.pop .menu-item')).find((x) => x.textContent.startsWith(arguments[0])); if (!b) return false; b.click(); return true;", ADD_LABELS[typ])
        if not ok:
            raise StudioError("the Add menu has no " + typ)
        self.wait(80)
        return self.js("return BananaStudio.state.sel")

    def select(self, path):
        ok = self.js("""const row = document.querySelector('#pane-elements .row[data-path="' + CSS.escape(arguments[0]) + '"]');
            if (!row) return false; row.click(); return true;""", json.dumps(path, separators=(",", ":")))
        if not ok:
            # (a collapsed parent, or another view's): open the view, and every parent
            if path[0] == "screens" and path[1] in ("home", "library", "launch", "all"):
                self.view(path[1])
            self.js("for (let n = 3; n < arguments[0].length; n++) BananaStudio.state.open[JSON.stringify(arguments[0].slice(0, n))] = true; BananaStudio.renderAll();", path)
            ok = self.js("""const row = document.querySelector('#pane-elements .row[data-path="' + CSS.escape(arguments[0]) + '"]');
                if (!row) return false; row.click(); return true;""", json.dumps(path, separators=(",", ":")))
        if not ok:
            raise StudioError("no element at %s" % path)
        self.tab("properties", "inspector")
        self.wait(40)

    def delete(self, path):
        self.select(path)
        self.js("Array.from(document.querySelectorAll('#inspector button')).find((b) => b.textContent.trim() === 'Delete').click()")
        self.wait(60)

    def get(self, path):
        return self.js("let o = BananaStudio.state.theme; for (const k of arguments[0]) o = o == null ? o : o[k]; return o === undefined ? null : JSON.parse(JSON.stringify(o));", path)

    def set(self, path, **props):
        """Sets properties of an element with the inspector's fields (in the order given)"""
        self.select(path)
        for k, v in props.items():
            k = {"type_": "type", "if_": "if"}.get(k, k)
            if k == "type":
                self.js("const s = document.querySelector('#inspector .field select'); s.value = arguments[0]; s.dispatchEvent(new Event('change'));", v)
                self.select(path)
            elif k == "gradient":
                self._gradient(path, v)
            elif k in OBJECT_KEYS and isinstance(v, dict):
                self._kv(k, v)
            elif k == "radius" and isinstance(v, list):
                self._radius4(path, v)
            elif k == "border" and isinstance(v, dict):
                self._raw(path, {k: v})
            elif k == "states":
                self._states(path, v)
            elif k == "smooth":
                self._smooth(path, v)
            elif k == "with":
                self._raw(path, {k: v})
            elif not self._field("#inspector", k, v):
                self._raw(path, {k: v})
            # a field that rebuilt the inspector (type, use...) leaves the element still selected
        got = self.get(path)
        bad = {k: (v, got.get(k)) for k, v in props.items() if k in got and k not in ("states", "gradient") and got.get(k) != v and not (isinstance(v, (int, float)) and got.get(k) == v)}
        if bad:
            raise StudioError("set %s: the theme has %s" % (path, bad))

    # ---- the controls ------------------------------------------------------------------------------------------
    def _field(self, scope, key, value):
        """One field (.field[data-key=key]) inside scope: a plain value with its control, or ƒx and an expression"""
        r = self.js(r"""
            const scope = document.querySelector(arguments[0]); const key = arguments[1]; const v = arguments[2];
            let f = Array.from(scope.querySelectorAll('.field')).find((x) => x.dataset.key === key);
            if (!f) return 'nofield';
            const fx = () => f.querySelector('.fx');
            const isExpr = typeof v === 'string' && v.charAt(0) === '=';
            const pressed = () => fx() && fx().getAttribute('aria-pressed') === 'true';
            const refind = () => { f = Array.from(scope.querySelectorAll('.field')).find((x) => x.dataset.key === key); };
            if (isExpr !== pressed() && fx()) { fx().click(); refind(); }
            const ctl = f.querySelector('.ctl');
            const fire = (el, ev) => el.dispatchEvent(new Event(ev, { bubbles: true }));
            if (isExpr) { const i = ctl.querySelector('input[type=text]'); i.value = v.slice(1); fire(i, 'input'); return 'ok'; }
            const cb = ctl.querySelector('input[type=checkbox]');
            if (cb) { if (cb.checked !== !!v) cb.click(); return 'ok'; }
            const sel = ctl.querySelector('select');
            if (sel) { if (!Array.from(sel.options).some((o) => o.value === String(v))) return 'nooption'; sel.value = String(v); fire(sel, 'change'); return 'ok'; }
            const col = ctl.querySelector('input[type=color]');
            if (col && typeof v === 'string' && /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/.test(v)) {
                const a = ctl.querySelector('input[type=range]'); col.value = v.slice(0, 7).toLowerCase(); if (a) a.value = v.length > 7 ? parseInt(v.slice(7, 9), 16) : 255; fire(col, 'input'); return 'ok';
            }
            if (col) return 'notcolor';
            const num = ctl.querySelector('input[type=number]');
            if (num) {
                const pctBtn = Array.from(ctl.querySelectorAll('button')).find((b) => b.textContent === '%');
                const pct = typeof v === 'string' && /%$/.test(v);
                if (typeof v === 'string' && !pct && isNaN(parseFloat(v))) return 'notnumber';
                if (pctBtn && (pctBtn.getAttribute('aria-pressed') === 'true') !== pct) { pctBtn.click(); refind(); }
                const n = f.querySelector('input[type=number]'); n.value = String(parseFloat(v)); fire(n, 'input'); return 'ok';
            }
            const t = ctl.querySelector('input[type=text]');
            if (t) { t.value = typeof v === 'string' ? v : JSON.stringify(v); fire(t, 'input'); return 'ok'; }
            return 'nocontrol';""", scope, key, value)
        if r == "ok":
            return True
        if r in ("nooption", "notcolor", "notnumber") and not (isinstance(value, str) and value.startswith("=")):
            # a value the plain control can't show (a font of the theme's own, "transparent", a word): as an expression
            return self._field(scope, key, "=" + json.dumps(value).replace('"', "'") if isinstance(value, str) else "=" + json.dumps(value))
        return False

    def _kv(self, key, obj):
        """shadow / outline: the inspector's sub-fields (x, y, blur, spread, color / width, color)"""
        for k, v in obj.items():
            r = self.js(r"""
                const lab = Array.from(document.querySelectorAll('#inspector .field > label')).find((l) => l.textContent === arguments[0] || l.textContent === arguments[3]);
                if (!lab) return 'nogroup';
                let n = lab.parentElement.nextElementSibling;
                while (n && n.classList.contains('kv')) {
                    if (n.firstChild.textContent === arguments[1] || n.firstChild.textContent === arguments[4]) {
                        const f = n.querySelector('.field'); const ctl = f.querySelector('.ctl'); const v = arguments[2];
                        const fire = (el, ev) => el.dispatchEvent(new Event(ev, { bubbles: true }));
                        const isExpr = typeof v === 'string' && v.charAt(0) === '=';
                        const fx = f.querySelector('.fx');
                        if (isExpr && fx.getAttribute('aria-pressed') !== 'true') fx.click();
                        const c = f.querySelector('input[type=color]');
                        if (!isExpr && c && /^#[0-9a-fA-F]{6}/.test(v)) { const a = f.querySelector('input[type=range]'); c.value = v.slice(0, 7).toLowerCase(); a.value = v.length > 7 ? parseInt(v.slice(7, 9), 16) : 255; fire(c, 'input'); return 'ok'; }
                        const t = f.querySelector('input[type=text]'); if (!t) return 'notext';
                        t.value = isExpr ? v.slice(1) : String(v); fire(t, 'input'); return 'ok';
                    }
                    n = n.nextElementSibling;
                }
                return 'nokey';""", key, k, v, {"shadow": "Shadow", "outline": "Outline"}.get(key, key), {"color": "Colour", "x": "X", "y": "Y", "blur": "Blur", "spread": "Spread", "width": "Width"}.get(k, k))
            if r != "ok":
                self.fallbacks.append("%s.%s (%s)" % (key, k, r))
                cur = self.get(self.js("return BananaStudio.state.sel")) or {}
                o = dict(cur.get(key) or {})
                o[k] = v
                self._raw(self.js("return BananaStudio.state.sel"), {key: o}, count=False)

    def _gradient(self, path, g):
        if "angle" in g:
            self._field("#inspector", "gradient angle", g["angle"])
        stops = g.get("stops", [])
        n0 = len((self.get(path).get("gradient") or {}).get("stops") or [])
        for _ in range(len(stops) - n0):
            self.js("Array.from(document.querySelectorAll('#inspector button')).find((b) => b.textContent === '+ stop').click()")
            self.wait(30)
        for i, s in enumerate(stops):
            self.js("""const lab = Array.from(document.querySelectorAll('#inspector .field > label')).find((l) => /gradient stops|Gradient stops/i.test(l.textContent));
                let n = lab.parentElement.nextElementSibling, k = 0;
                while (n && n.classList.contains('kv')) { if (k === arguments[0]) { const i = n.querySelector('input[type=text]'); i.value = arguments[1]; i.dispatchEvent(new Event('input', { bubbles: true })); return; } k++; n = n.nextElementSibling; }""", i, s)

    def _radius4(self, path, corners):
        cur = self.get(path).get("radius")
        if not isinstance(cur, list):
            self.js("Array.from(document.querySelectorAll('#inspector button')).find((b) => b.textContent === 'four corners').click()")
            self.wait(40)
        for n, v in zip(("tl", "tr", "br", "bl"), corners):
            self._field("#inspector", "radius " + n, v)

    def _states(self, path, states):
        """Changes when…: + state, its "when" field, then its overrides in the state's own JSON box"""
        for i, st in enumerate(states):
            have = len(self.get(path).get("states") or [])
            if have <= i:
                self.js("Array.from(document.querySelectorAll('#inspector button')).find((b) => b.textContent === '+ state').click()")
                self.wait(40)
            self.js("document.querySelectorAll('#inspector .states-box details.adv')[arguments[0]].open = true", i)
            rest = {k: v for k, v in st.items() if k != "when"}
            r = self.js(r"""const d = document.querySelectorAll('#inspector .states-box details.adv')[arguments[0]];
                const f = d.querySelector('.field[data-key="when"]'); const v = arguments[1];
                const fx = f.querySelector('.fx'); if (typeof v === 'string' && v.charAt(0) === '=' && fx.getAttribute('aria-pressed') !== 'true') fx.click();
                const t = d.querySelector('.field[data-key="when"] input[type=text]'); t.value = typeof v === 'string' && v.charAt(0) === '=' ? v.slice(1) : String(v);
                t.dispatchEvent(new Event('input', { bubbles: true }));
                const ta = d.querySelector('textarea'); ta.value = arguments[2];
                Array.from(d.querySelectorAll('button')).find((b) => b.textContent === 'apply overrides JSON').click(); return 'ok';""", i, st.get("when", "=sel"), json.dumps(rest))
            if r != "ok":
                raise StudioError("state %d: %s" % (i, r))
            self.select(path)

    def _smooth(self, path, v):
        if isinstance(v, (int, float)):
            self._field("#inspector .smooth-box", "smooth (ms)", v)
            return
        cur = self.get(path).get("smooth")
        if not isinstance(cur, dict):
            self.js("Array.from(document.querySelectorAll('#inspector button')).find((b) => b.textContent === 'per property').click()")
            self.wait(40)
            cur = self.get(path).get("smooth") or {}
        for k in list(v):
            if k not in cur:
                self.js("const i = Array.from(document.querySelectorAll('#inspector input[type=text]')).find((x) => x.placeholder.startsWith('property')); i.value = arguments[0];"
                        "Array.from(document.querySelectorAll('#inspector button')).find((b) => b.textContent === '+').click();", k)
                self.wait(30)
        for k, val in v.items():
            self._field("#inspector .smooth-box", k, val)
        cur = self.get(path).get("smooth") or {}
        for k in [k for k in cur if k not in v]:       # the starter keys the button made, not asked for
            self._raw(path, {"smooth": v}, count=False)
            break

    def _raw(self, path, merge, count=True):
        """The studio's own "raw JSON (this element)" box, for what the forms don't have"""
        if count:
            self.fallbacks.append("%s %s" % (".".join(str(x) for x in path[-3:]), ", ".join(merge)))
        self.select(path)
        el = self.get(path)
        el.update(merge)
        ok = self.js("""const d = Array.from(document.querySelectorAll('#inspector details.adv')).find((x) => x.querySelector('summary').textContent.startsWith('raw JSON'));
            if (!d) return false; d.open = true; d.querySelector('textarea').value = arguments[0];
            Array.from(d.querySelectorAll('button')).find((b) => b.textContent === 'apply JSON').click(); return true;""", json.dumps(el, indent=1))
        if not ok:
            raise StudioError("no raw JSON box for %s" % path)
        self.select(path)

    # ---- checking and taking it home --------------------------------------------------------------------------
    def problems(self):
        self.js("document.querySelector('#validateBtn').click()")
        self.wait(200)
        out = self.js("return Array.from(document.querySelectorAll('#log .e, #log .w')).map((e) => e.textContent)")
        self.js("document.querySelector('#drawer').hidden = true")
        return out

    def engine_errors(self):
        return self.js("return BananaEngine.errors()")

    def optimize(self, look=False):
        """Optimize: apply what is ticked (the safe fixes), or every fixable one with look=True; returns the titles"""
        self.js("document.querySelector('#optimizeBtn').click()")
        self.wait(200)
        if not self.js("return !!document.querySelector('#optimize .opt-foot button.primary')"):
            self.close_dialogs()
            return []
        if look:
            self.js("document.querySelectorAll('#optimize .opt-group.look input.one').forEach((c) => { if (!c.checked) c.click(); })")
        titles = self.js("return Array.from(document.querySelectorAll('#optimize .opt-row')).filter((r) => r.querySelector('input.one') && r.querySelector('input.one').checked).map((r) => r.querySelector('.t').textContent)")
        if titles:
            self.js("document.querySelector('#optimize .opt-foot button.primary').click()")
            self.wait(300)
        self.close_dialogs()
        return titles

    def shoot(self, path, views=("home", "library")):
        """Both screens of each view, side by side, as the preview draws them"""
        import base64
        pics = []
        for v in views:
            self.view(v)
            if v == "library":
                self.js("BananaEngine.library(0, 1)")
            self.wait(1300)
            pics.append(self.js("return Array.from(document.querySelectorAll('canvas.scr')).map((c) => c.toDataURL('image/png'))"))
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        for v, p in zip(views, pics):
            for side, uri in zip(("top", "bottom"), p):
                with open("%s-%s-%s.png" % (path, v, side), "wb") as f:
                    f.write(base64.b64decode(uri.split(",", 1)[1]))
        self.view("home")

    def export(self, folder):
        """File > Save .json and Export (.zip): the files the browser downloads, copied into folder"""
        dl = os.path.join(self.ff.profile, "downloads")
        before = set(os.listdir(dl)) if os.path.isdir(dl) else set()
        self.js("document.querySelector('#fileMenuBtn').click(); document.querySelector('#saveBtn').click();")
        self.js("document.querySelector('#exportQuickBtn').click()")
        got = []
        for _ in range(100):
            now = set(os.listdir(dl)) if os.path.isdir(dl) else set()
            got = [f for f in now - before if not f.endswith(".part")]
            if len(got) >= 2:
                break
            time.sleep(0.1)
        os.makedirs(folder, exist_ok=True)
        for f in got:
            shutil.copy(os.path.join(dl, f), os.path.join(folder, f))
        return sorted(got)
