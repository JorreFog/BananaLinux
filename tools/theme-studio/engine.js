// Banana Theme Studio: live previews drawn by the menu's own engine, in the page.
//
// wasm/banana-engine.js is banana-ui's theme engine (layout.cpp, library.cpp, theme.cpp, soft.cpp, the rnds
// rasterizer) compiled to WebAssembly by wasm/build.sh, with the built-in fonts and icons and the made-up test
// library inside it. This loads it, writes the theme being edited (and the files it names) into the engine's
// memory file system, steps it every animation frame and copies its two screens onto two canvases — the same
// pixels banana-ui draws (tests/engine.test.js compares them with the native build's).
//
// The engine file is one plain script with the wasm inlined, so this works from file:// (where a page can't
// fetch() anything) as well as from any static host. Nothing is sent anywhere.
(function (global) {
  "use strict";

  const BUTTONS = { left: 0, right: 1, up: 2, down: 3, a: 4, b: 5, x: 6, y: 7, l: 8, r: 9 };
  const THEME_DIR = "/theme";

  const Engine = {
    state: "none",				// none, loading, ready, failed
    detail: "",
    build: null,				// { built, commit, sources } from wasm/build.sh
    W: 640, H: 480, every: false,
    M: null, api: null,
    stderr: [],					// "banana-ui: theme: ..." lines theme.cpp printed since the last load

    // load the engine (once): resolves true when it is ready
    load(src) {
      if (this._loading) return this._loading;
      this.state = "loading";
      const self = this;
      this._loading = new Promise(function (resolve) {
        const start = () => {
          if (typeof global.BananaEngineModule !== "function") { self.fail("wasm/banana-engine.js didn't define the engine"); return resolve(false); }
          self.build = global.BananaEngineBuild || null;
          global.BananaEngineModule({
            print: function () {},
            printErr: function (t) { if (/theme:/.test(t)) self.stderr.push(String(t).replace(/^banana-ui: /, "")); },
          }).then(function (M) {
            self.M = M;
            const c = (n, r, a) => M.cwrap(n, r, a);
            self.api = {
              init: c("bt_init", "number", ["number", "number", "number"]), load: c("bt_load", "string", ["string", "number"]),
              frame: c("bt_frame", "number", ["number", "number"]), pixels: c("bt_pixels", "number", ["number"]),
              press: c("bt_press", "number", ["number"]), touch: c("bt_touch", "number", ["number", "number", "number", "number"]),
              view: c("bt_view", "number", []), system: c("bt_system", "number", []), game: c("bt_game", "number", []),
              systemCount: c("bt_systemCount", "number", []), systemIds: c("bt_systemIds", "string", []), home: c("bt_home", null, ["number"]),
              library: c("bt_library", null, ["number", "number"]),
              host: c("bt_host", null, ["number", "number", "number", "number", "number"]), online: c("bt_online", null, ["number"]),
              errors: c("bt_errors", "string", []), script: c("bt_script", "string", ["string"]), cues: c("bt_cues", "string", []),
              evaluate: c("bt_eval", "string", ["string"]),
            };
            self.api.init(self.W, self.H, self.every ? 1 : 0);
            self.applyHost();
            self.state = "ready";
            resolve(true);
          }, function (e) { self.fail("the engine didn't start: " + (e && e.message || e)); resolve(false); });
        };
        if (typeof global.BananaEngineModule === "function") return start();
        if (typeof document === "undefined") { self.fail("no document to load the engine into"); return resolve(false); }
        const s = document.createElement("script");
        s.src = src || "wasm/banana-engine.js";
        s.onload = start;
        s.onerror = function () { self.fail("wasm/banana-engine.js isn't there (build it with tools/theme-studio/wasm/build.sh)"); resolve(false); };
        document.head.appendChild(s);
      });
      return this._loading;
    },
    fail(why) { this.state = "failed"; this.detail = why; },
    ready() { return this.state === "ready"; },

    // the panels' size and the library (false: fixture.sh's, the one serve.py's previews use; true: a game for
    // every system in systems.conf). The theme is loaded again by the caller
    configure(w, h, every) {
      this.W = w; this.H = h; this.every = !!every;
      if (!this.ready()) return;
      this.api.init(w, h, every ? 1 : 0);
      this.applyHost();
    },
    hostOpts: { battery: 76, charging: false, twelveHour: false, motion: true, batteryText: false, online: true },
    setHost(o) { Object.assign(this.hostOpts, o || {}); if (this.ready()) this.applyHost(); },
    applyHost() {
      const o = this.hostOpts;
      this.api.host(o.battery, o.charging ? 1 : 0, o.twelveHour ? 1 : 0, o.motion ? 1 : 0, o.batteryText ? 1 : 0);
      this.api.online(o.online ? 1 : 0);
    },

    // the theme's text and its files ({ "relative/path.png": Uint8Array }), written to /theme and loaded;
    // keep: stay on the view and selection the preview is on. -> the theme's problems
    setTheme(text, files, keep) {
      if (!this.ready()) return [];
      const FS = this.M.FS;
      rmTree(FS, THEME_DIR);
      FS.mkdirTree(THEME_DIR);
      for (const rel of Object.keys(files || {})) {
        const p = safeRel(rel);
        if (!p || !files[rel]) continue;
        const dir = p.indexOf("/") >= 0 ? THEME_DIR + "/" + p.slice(0, p.lastIndexOf("/")) : THEME_DIR;
        try { FS.mkdirTree(dir); FS.writeFile(THEME_DIR + "/" + p, files[rel]); } catch (e) { /* a path the FS refuses: skipped */ }
      }
      FS.writeFile(THEME_DIR + "/theme.json", text);
      this.stderr = [];
      this.api.load(THEME_DIR + "/theme.json", keep ? 1 : 0);
      return this.errors();
    },
    // the theme's problems so far (some only show when an element is first drawn)
    errors() {
      if (!this.ready()) return [];
      const seen = {}, out = [];
      this.stderr.concat(this.api.errors().split("\n")).forEach(function (l) {
        l = l.trim(); if (!l || seen[l]) return; seen[l] = 1; out.push(l);
      });
      return out;
    },

    // one step at now (ms): true when the screens changed
    frame(now, force) { return this.ready() && this.api.frame(now, force ? 1 : 0) === 1; },
    // a screen's pixels into an ImageData (W x H)
    pixels(screen, img) {
      const p = this.api.pixels(screen) >> 2, n = this.W * this.H, src = this.M.HEAPU32;
      const dst = new Uint32Array(img.data.buffer);
      for (let i = 0; i < n; i++) {
        const w = src[p + i];		// 0xXXRRGGBB -> RGBA bytes, i.e. 0xAABBGGRR little-endian
        dst[i] = 0xff000000 | ((w & 0xff) << 16) | (w & 0xff00) | ((w >>> 16) & 0xff);
      }
      return img;
    },
    press(name) { return this.ready() && BUTTONS[name] !== undefined && this.api.press(BUTTONS[name]) === 1; },
    touch(screen, x, y, down) { return this.ready() && this.api.touch(screen, x, y, down ? 1 : 0) === 1; },
    view() { return this.ready() ? ["home", "library", "launch"][this.api.view()] || "home" : "home"; },
    where() { return this.ready() ? { view: this.view(), system: this.api.system(), game: this.api.game(), systems: this.api.systemCount() } : null; },
    shelf() { return this.ready() ? this.api.systemIds().split("\n").filter(Boolean) : []; },	// the shelf's system ids, in order
    home(sys) { if (this.ready()) this.api.home(sys || 0); },
    library(sys, game) { if (this.ready()) this.api.library(sys || 0, game || 0); },
    cues() { return this.ready() ? this.api.cues().split("\n").filter(Boolean) : []; },

    // banana-ui --headless's script (home, lib, wait, press, touch, shot), as serve.py runs it: the same stills
    // -> [{ name, top, bot }] with PNG data: URLs. The live preview starts again afterwards (keep: false)
    stills(script) {
      if (!this.ready() || typeof document === "undefined") return [];
      const names = this.api.script(script);
      if (/^error/.test(names)) throw new Error(names.replace(/^error: /, ""));
      const W = this.W, H = this.H, cv = document.createElement("canvas");
      cv.width = W; cv.height = H;
      const ctx = cv.getContext("2d"), img = ctx.createImageData(W, H), dst = new Uint32Array(img.data.buffer);
      const shot = (file) => {
        const rgb = this.M.FS.readFile(file);
        for (let i = 0, j = 0; i < W * H; i++, j += 3) dst[i] = 0xff000000 | (rgb[j + 2] << 16) | (rgb[j + 1] << 8) | rgb[j];
        ctx.putImageData(img, 0, 0);
        return cv.toDataURL("image/png");
      };
      return names.split("\n").filter(Boolean).map((n) => ({ name: n, top: shot("/out/" + n + "-top.rgb"), bot: shot("/out/" + n + "-bot.rgb") }));
    },

    // the engine's own files: the built-in icons, systems.conf, systems.cfg
    readFile(path) { try { return this.M.FS.readFile(path); } catch (e) { return null; } },
    readText(path) { const b = this.readFile(path); return b ? new TextDecoder().decode(b) : ""; },
    // every system the menu knows, with its name and its built-in icon's file (systems.conf, then the
    // collections; the icon as rnds/systems.cfg picks it) -> [{ id, name, icon: "/rnds/icons/x.png" }]
    systems() {
      if (!this.ready()) return [];
      const out = [];
      let cur = null;
      this.readText("/fx/systems.conf").split("\n").forEach(function (ln) {
        ln = ln.trim();
        const m = /^\[(.+)\]$/.exec(ln);
        if (m) { cur = { id: m[1], name: m[1] }; out.push(cur); return; }
        const kv = /^name\s*=\s*(.*)$/.exec(ln);
        if (cur && kv) cur.name = kv[1];
      });
      out.push({ id: "favorites", name: "Favorites" }, { id: "recently-played", name: "Recently played" }, { id: "search", name: "Search" });
      const look = {};
      this.readText("/rnds/systems.cfg").split("\n").forEach(function (ln) {
        const t = ln.replace(/#.*$/, "").trim().split(/\s+/);
        if (t.length < 2 || !t[0] || t[0][0] === "@") return;
        t[0].split(",").forEach((id) => { look[id.toLowerCase()] = t[1] === "-" ? "" : "/rnds/icons/" + t[1]; });
      });
      out.forEach((s) => { s.icon = look[s.id.toLowerCase()] !== undefined ? look[s.id.toLowerCase()] : (look["*"] || ""); });
      return out;
    },
  };

  function safeRel(p) {
    p = String(p || "").replace(/\\/g, "/");
    if (p.charAt(0) === "/" || p.charAt(0) === "~") return null;
    const parts = p.split("/").filter((x) => x !== "" && x !== ".");
    if (!parts.length || parts.some((x) => x === "..")) return null;
    return parts.join("/");
  }
  function rmTree(FS, dir) {
    let names;
    try { names = FS.readdir(dir); } catch (e) { return; }
    for (const n of names) {
      if (n === "." || n === "..") continue;
      const p = dir + "/" + n;
      if (FS.isDir(FS.stat(p).mode)) rmTree(FS, p); else FS.unlink(p);
    }
    FS.rmdir(dir);
  }

  Engine.safeRel = safeRel;
  if (typeof module !== "undefined" && module.exports) module.exports = Engine;
  global.BananaEngine = Engine;
})(typeof globalThis !== "undefined" ? globalThis : this);
