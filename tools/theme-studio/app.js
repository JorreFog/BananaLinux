// Banana Theme Studio: the editor.
//
// A layout theme is one JSON file (docs/THEMES.md). This edits that JSON directly — the object you load is the
// object you save, so key order and any members the editor does not know are kept. Everything works from the page
// alone, opened from file:// or any static host: the live preview is the menu's own engine compiled to
// WebAssembly (engine.js), the examples and guides are inlined (templates.js, guide.js), and a theme travels with
// its files as a .zip (zip.js). serve.py, when it runs, adds your themes folder and Send to handheld (render.js).
(function () {
  "use strict";
  const G = typeof globalThis !== "undefined" ? globalThis : window;
  const E = G.BananaExpr, S = G.BananaSchema, RENDER = G.BananaRender;
  const $ = (s) => document.querySelector(s);

  // ---- a small starter theme, so "New" works offline ------------------------------------------------
  const STARTER = {
    name: "My theme", author: "you", base: "dark", accent: "#7c9cff",
    colors: { screenBg: "#10131c", ink: "#eef1ff", muted: "#9aa3c0" },
    font: "inter",
    vars: { bg: "#10131c", card: "#1b2030", ink: "#eef1ff", soft: "#9aa3c0", hi: "=accent" },
    behavior: { launchMs: 700, scrollMs: 90 },
    screens: {
      home: {
        top: [
          { type: "rect", fill: "=bg" },
          { type: "text", x: 0, y: 190, w: "100%", align: "center", text: "{sys.name}", font: "inter-bold", size: 34, color: "=ink" },
          { type: "text", x: 0, y: 234, w: "100%", align: "center", text: "{upper(sys.maker)}", size: 13, spacing: 2, color: "=soft" },
        ],
        bottom: [
          { type: "rect", fill: "=bg" },
          { type: "list", source: "systems", direction: "row", x: 0, y: 120, w: 640, h: 230, itemW: 150, itemH: 190, gap: 14, scrollMode: "center",
            item: { type: "group", w: 150, h: 190, children: [
              { type: "rect", x: 75, y: 95, w: 150, h: 190, anchor: "center", radius: 20, scale: "=sel ? 1.08 : 0.92", fill: "=sel ? mix(item.color, card, 0.35) : card" },
              { type: "text", x: 8, w: 134, y: 140, align: "center", text: "{item.name}", size: 13, color: "=sel ? ink : soft" },
            ] } },
        ],
      },
      library: {
        top: [ { type: "rect", fill: "=bg" }, { type: "text", x: 40, y: 40, w: 400, text: "{game.title}", font: "inter-bold", size: 28, color: "=ink" } ],
        bottom: [
          { type: "rect", fill: "=bg" },
          { type: "list", source: "games", direction: "column", x: 24, y: 40, w: 592, h: 400, itemW: 592, itemH: 48, gap: 8,
            item: { type: "group", w: 592, h: 48, children: [
              { type: "rect", fill: "=sel ? card : 'transparent'", radius: 10 },
              { type: "text", x: 16, y: 14, text: "{item.title}", size: 16, color: "=ink" },
            ] } },
        ],
      },
    },
  };

  // ---- state -----------------------------------------------------------------------------------------
  const state = { theme: clone(STARTER), sel: null, view: "home", screen: "top", addScreen: "top", size: "640x480",
    shots: null, drag: null, open: {}, templates: null, localThemes: [], systemIds: null, assets: {},
    every: false,			// the preview's library: fixture.sh's (false) or a game for every system (true)
    orig: {},				// the members the theme had when it was opened (see themeOut)
    unsaved: false, undo: [], redo: [], cur: null, curKey: "", curAt: 0 };
  const ENGINE = G.BananaEngine, ZIP = G.BananaZip;
  function clone(o) { return JSON.parse(JSON.stringify(o)); }

  // ---- tiny DOM helper -------------------------------------------------------------------------------
  function h(tag, attrs) {
    const e = document.createElement(tag);
    if (attrs) for (const k in attrs) {
      if (k === "class") e.className = attrs[k];
      else if (k === "html") e.innerHTML = attrs[k];
      else if (k.startsWith("on")) e.addEventListener(k.slice(2), attrs[k]);
      else if (attrs[k] !== undefined && attrs[k] !== null) e.setAttribute(k, attrs[k]);
    }
    for (let i = 2; i < arguments.length; i++) {
      const kid = arguments[i];
      if (kid === null || kid === undefined) continue;
      e.appendChild(kid.nodeType ? kid : document.createTextNode(String(kid)));
    }
    return e;
  }

  // ---- a plain-words complaint about broken JSON, instead of the parser's raw message -------------------
  function badJson(what, e) {
    alert("That isn't valid JSON, so it could not be used.\n\n" +
      "You were editing: " + what + "\n" +
      "What went wrong: " + e.message + "\n\n" +
      "The usual slips: a missing comma between two items, a comma just before a } or ], " +
      "or a word that needs \"quotes\" around it.");
  }

  // ---- paths into the theme JSON ---------------------------------------------------------------------
  function getPath(path) { let o = state.theme; for (const k of path) { if (o === null || o === undefined) return undefined; o = o[k]; } return o; }
  function setPath(path, v) { const p = getPath(path.slice(0, -1)); p[path[path.length - 1]] = v; }
  function delPath(path) { const p = getPath(path.slice(0, -1)); if (Array.isArray(p)) p.splice(+path[path.length - 1], 1); else delete p[path[path.length - 1]]; }
  const parentOf = (path) => path.slice(0, -1);
  const samePath = (a, b) => a && b && a.length === b.length && a.every((x, i) => x === b[i]);

  // single element <-> [element] normalisation, so the tree can treat every screen as a list
  function normalize() {
    const sc = state.theme.screens || (state.theme.screens = {});
    for (const v of S.VIEWS) { const s = sc[v]; if (!s) continue; for (const k of S.SCREENS) { const x = s[k]; if (x && !Array.isArray(x)) s[k] = [x]; } }
  }
  function screenArr(screen) {
    const t = state.theme;
    screen = screen || state.screen;
    if (!t.screens || typeof t.screens !== "object") t.screens = {};
    if (!t.screens[state.view] || typeof t.screens[state.view] !== "object") t.screens[state.view] = {};
    let v = t.screens[state.view][screen];
    if (Array.isArray(v)) return v;
    if (v && typeof v === "object") { v = [v]; t.screens[state.view][screen] = v; return v; }
    if (v === undefined || v === null) { v = []; t.screens[state.view][screen] = v; return v; }
    return v;
  }

  // ---- loose JSON: comments, trailing commas (as the engine reads a theme) ----------------------------
  function looseParse(text) {
    let out = "", i = 0, str = false;
    while (i < text.length) {
      const c = text[i], n = text[i + 1];
      if (str) { out += c; if (c === "\\") { out += n || ""; i += 2; continue; } if (c === '"') str = false; i++; continue; }
      if (c === '"') { str = true; out += c; i++; continue; }
      if (c === "/" && n === "/") { while (i < text.length && text[i] !== "\n") i++; continue; }
      if (c === "/" && n === "*") {			// (its newlines kept, so an error's line number is the file's)
        i += 2;
        while (i < text.length && !(text[i] === "*" && text[i + 1] === "/")) { if (text[i] === "\n") out += "\n"; i++; }
        i += 2; continue;
      }
      if (c === ",") {					// drop a comma before a closing brace or bracket
        let j = i + 1;
        while (j < text.length && /\s/.test(text[j])) j++;
        if (text[j] === "}" || text[j] === "]") { i++; continue; }
      }
      out += c; i++;
    }
    return JSON.parse(out);
  }

  // ---- values: classify a property, convert between forms, evaluate ---------------------------------
  function classify(v) { if (typeof v === "string") { if (v.charAt(0) === "=") return "expr"; if (v.indexOf("{") >= 0) return "tmpl"; } return "lit"; }
  function litText(v) {
    if (v === null || v === undefined) return "";
    if (typeof v === "string") return v;
    return String(v);
  }
  function rawText(v) { return typeof v === "string" ? (v.charAt(0) === "=" ? v.slice(1) : v) : litText(v); }
  function convertForm(v, to, kind) {
    const text = rawText(v);
    if (to === "expr") return "=" + text;
    if (to === "tmpl") return text;
    if (kind === "bool") return text === "true" ? true : text === "false" ? false : !!(text && text !== "0");
    if (kind === "number") return text === "" ? 0 : (isNaN(+text) ? 0 : +text);
    if (kind === "length") return /^\d+(\.\d+)?$/.test(text) ? +text : text;
    return text;
  }

  // accent + palette, worked out the way theme.cpp does
  function accentBase() {
    const a = state.theme.accent;
    if (typeof a === "string") { if (S.ACCENTS[a]) return S.ACCENTS[a][0]; if (E.parseColorText(a) !== null) return a; return "#7c9cff"; }
    if (a && typeof a === "object" && typeof a.base === "string") return a.base;
    return "#7c9cff";
  }
  function accentShades() {
    const b = E.parseColorText(accentBase()); const base = b === null ? 0xff7c9cff : b;
    return { base: accentBase(), light: E.colorText(E.mixColor(base, 0xffffffff, 0.6)), dark: E.colorText(E.mixColor(base, 0xff000000, 0.6)),
      band: E.colorText(E.mixColor(base, 0xffffffff, 0.25)), low: E.colorText(E.mixColor(base, 0xff000000, 0.2)) };
  }
  function palette() {
    const p = state.theme.base === "light" ? S.LIGHT : S.DARK, out = {};
    for (const k in p) out[k] = p[k] === "@accent" ? accentBase() : p[k];
    return out;
  }

  // sample menu data, so expressions show a real value while editing
  const ok = (v) => ({ found: true, value: v });
  function sampleSys() {
    const a = accentBase();
    return { id: "nds", name: "Nintendo DS", maker: "Nintendo", color: a, accent: a, icon: "", games: 7, played: 7, time: "2h",
      fav: 0, app: 0, hasResume: 1, resumeTitle: "Peel Quest", resumeEyebrow: "Last played", resumeBox: "", resumeCart: "", resumeLabel: "", resumeArt: "", index: 0 };
  }
  function sampleGame() {
    return { title: "Peel Quest", sub: "Nintendo", plays: 3, last: "today", time: "2h", box: "", cart: "", label: "", art: "", rom: "", romIcon: "",
      icon: "", achA: 4, achB: 10, achLabel: "4/10", achProgress: 0.4, fav: 0, index: 0, systemId: "nds", systemName: "Nintendo DS" };
  }
  function resolveHook(name) {
    const a = accentShades();
    const SYS = sampleSys(), GAME = sampleGame(), ITEM = Object.assign({}, sampleGame(), { key: "A", label: "Open" });
    if (name === "view") return ok(state.view);
    if (name === "W") return ok(640); if (name === "H") return ok(480);
    if (name === "screen") return ok(state.screen === "bottom" ? 1 : 0);
    if (name === "time") return ok(0);
    if (name === "clock") return ok("13:01"); if (name === "date") return ok("10/02");
    if (name === "battery") return ok(76); if (name === "charging") return ok(0); if (name === "batteryMode") return ok("icon");
    if (name === "online") return ok(1); if (name === "motion") return ok(1); if (name === "brand") return ok("Alpha 0.5.0");
    if (name === "accent" || name === "accent.base") return ok(a.base);
    if (name === "accent.light") return ok(a.light); if (name === "accent.dark") return ok(a.dark);
    if (name === "accent.band") return ok(a.band); if (name === "accent.low") return ok(a.low);
    if (name.indexOf("palette.") === 0) { const p = palette(); const k = name.slice(8); return ok(p[k] !== undefined ? p[k] : ""); }
    if (name === "cursor") return ok(1); if (name === "count") return ok(7); if (name === "scroll") return ok(1);
    if (name === "moved") return ok(140); if (name === "entered") return ok(140); if (name === "dir") return ok(1);
    if (name === "systems.count" || name === "systemCount") return ok(7);
    if (name === "games.count" || name === "gameCount") return ok(7);
    if (name === "launch.progress") return ok(0.4); if (name === "launch.t") return ok(300);
    if (name === "empty") return ok(0);
    if (name === "empty.title") return ok("No games found"); if (name === "empty.hint") return ok("Copy ROMs into the folder");
    if (name.indexOf("sys.") === 0) { const k = name.slice(4); return ok(SYS[k] !== undefined ? SYS[k] : ""); }
    if (name.indexOf("game.") === 0) { const k = name.slice(5); return ok(GAME[k] !== undefined ? GAME[k] : ""); }
    if (name.indexOf("item.") === 0) { const k = name.slice(5); return ok(ITEM[k] !== undefined ? ITEM[k] : ""); }
    if (["i", "n", "sel", "rel", "pos", "first", "last", "ix", "iy"].indexOf(name) >= 0) {
      const m = { i: 1, n: 7, sel: 1, rel: 0, pos: 0, first: 0, last: 6, ix: 2, iy: 0 }; return ok(m[name]);
    }
    return { found: false, value: "" };
  }
  function buildScope() {
    const hooks = new E.Scope(); hooks.resolve = resolveHook;
    const vars = new E.Scope(hooks);
    for (const k of Object.keys(state.theme.vars || {})) {
      const v = state.theme.vars[k];
      vars.set(k, E.evalProp(E.parseProp(typeof v === "string" ? v : litText(v)), vars));
    }
    return vars;
  }
  function evalValue(v) {
    const text = typeof v === "string" ? v : litText(v);
    const p = E.parseProp(text);
    if (p.error) return { text: "", color: null, error: p.error };
    const res = E.evalProp(p, buildScope());
    const t = typeof res === "string" ? res : E.evStr(res);
    return { text: t, color: typeof res === "string" ? E.parseColorText(res) : null, error: "" };
  }
  function argbParts(c) { return { a: (c >>> 24) & 255, r: (c >>> 16) & 255, g: (c >>> 8) & 255, b: c & 255 }; }
  function argbCss(c) { const p = argbParts(c); return "rgba(" + p.r + "," + p.g + "," + p.b + "," + (p.a / 255).toFixed(3) + ")"; }

  // ---- the inspector's field widget ------------------------------------------------------------------
  let fieldSeq = 0;
  function field(parent, label, get, set, kind, options, labelNode) {
    const lab = labelNode || h("label", null, (S.FIELD_LABELS && S.FIELD_LABELS[label]) || label);
    const tip = S.FIELD_TIPS && S.FIELD_TIPS[label];
    if (!labelNode && tip) lab.title = tip;
    const row = h("div", { class: "field" }); row.appendChild(lab);
    const ctl = h("div", { class: "ctl" }); row.appendChild(ctl); parent.appendChild(row);
    const chip = h("span", { class: "res" });
    const opts = () => (typeof options === "function" ? options(Object.keys(state.theme.fonts || {})) : (options || []));
    function chipFor() {
      const r = evalValue(get());
      chip.classList.toggle("bad", !!r.error);
      chip.textContent = r.error ? r.error : r.text;
      if (!r.error && r.color !== null) { const sw = h("span", { class: "sw" }); sw.style.background = argbCss(r.color); chip.appendChild(document.createTextNode(" ")); chip.appendChild(sw); }
    }
    const fid = "field" + (++fieldSeq);		// (typing in one field is one step of undo)
    function commit(nv) { set(nv); markDirty(fid); chipFor(); }
    function build() {
      ctl.innerHTML = "";
      const cur = get(), form = classify(cur);
      const seg = h("div", { class: "seg" });
      [["lit", "Lit"], ["expr", "Expr"], ["tmpl", "Tmpl"]].forEach(function (fp) {
        const b = h("button", { type: "button" }, fp[1]); b.setAttribute("aria-pressed", String(form === fp[0]));
        b.onclick = () => { if (form === fp[0]) return; commit(convertForm(cur, fp[0], kind)); build(); };
        seg.appendChild(b);
      });
      ctl.appendChild(seg);
      if (form === "expr") { ctl.appendChild(document.createTextNode("=")); const i = h("input", { type: "text", spellcheck: "false", value: cur.slice(1) }); i.oninput = () => commit("=" + i.value); ctl.appendChild(i); }
      else if (form === "tmpl") { const i = h("input", { type: "text", spellcheck: "false", value: cur }); i.oninput = () => commit(i.value); ctl.appendChild(i); }
      else buildLiteral(cur);
      ctl.appendChild(chip); chipFor();
    }
    function buildLiteral(cur) {
      if (kind === "bool") { const i = h("input", { type: "checkbox" }); i.checked = !!(cur === true || cur === "true" || cur === 1); i.onchange = () => commit(i.checked); ctl.appendChild(i); return; }
      if (kind === "enum") {
        const sel = h("select"); sel.appendChild(h("option", { value: "" }, "(none)"));
        const known = opts().map((o) => (Array.isArray(o) ? o[0] : o));
        if (typeof cur === "string" && cur !== "" && known.indexOf(cur) < 0) { const op = h("option", { value: cur }, cur + "  (not one of the list)"); op.setAttribute("selected", ""); sel.appendChild(op); }
        opts().forEach((o) => {
          const val = Array.isArray(o) ? o[0] : o, txt = Array.isArray(o) ? o[1] : o;
          const op = h("option", { value: val }, txt); if (val === cur) op.setAttribute("selected", ""); sel.appendChild(op);
        });
        sel.onchange = () => commit(sel.value); ctl.appendChild(sel); return;
      }
      if (kind === "color") {
        const argb = E.parseColorText(typeof cur === "string" ? cur : "#000000"); const c = argb === null ? 0xff000000 : argb;
        const p = argbParts(c), hex = "#" + (c & 0xffffff).toString(16).padStart(6, "0");
        const ci = h("input", { type: "color", value: hex, title: "The colour" });
        const ai = h("input", { type: "range", min: 0, max: 255, value: p.a, title: "Opacity: 0 is clear, 255 is solid" });
        const sw = h("span", { class: "hex" }, hex + (p.a === 255 ? "" : " a" + p.a));
        const put = () => { const a = +ai.value; const s = ci.value + (a === 255 ? "" : a.toString(16).padStart(2, "0")); sw.textContent = s; commit(s); };
        ci.oninput = put; ai.oninput = put; ctl.appendChild(ci); ctl.appendChild(ai); ctl.appendChild(sw); return;
      }
      if (kind === "number") { const i = h("input", { type: "number", value: typeof cur === "number" ? cur : "" }); i.oninput = () => commit(i.value === "" ? 0 : +i.value); ctl.appendChild(i); return; }
      if (kind === "length") {
        const pct = typeof cur === "string" && cur.slice(-1) === "%";
        const i = h("input", { type: "number", value: pct ? parseFloat(cur) : (typeof cur === "number" ? cur : "") });
        const p = h("button", { type: "button" }, "%"); p.setAttribute("aria-pressed", String(pct));
        i.oninput = () => commit(pct ? (parseFloat(i.value) || 0) + "%" : (isNaN(parseFloat(i.value)) ? 0 : +i.value));
        p.onclick = () => { const n = parseFloat(i.value) || 0; commit(pct ? n : n + "%"); build(); };
        ctl.appendChild(i); ctl.appendChild(p); return;
      }
      const i = h("input", { type: "text", spellcheck: "false", value: litText(cur) }); i.oninput = () => commit(i.value); ctl.appendChild(i);
    }
    build();
    return { refresh: build };
  }

  // ---- object-valued props -------------------------------------------------------------------------------
  function kvField(parent, label, get, set, keys) {
    const box = h("div"); parent.appendChild(box);
    function build() {
      box.innerHTML = "";
      const row = h("div", { class: "field" }); row.appendChild(h("label", null, label));
      const ctl = h("div", { class: "ctl" }); row.appendChild(ctl); box.appendChild(row);
      keys.forEach(function (k) {
        const kv = h("div", { class: "kv" });
        kv.appendChild(h("span", { class: "muted" }, (S.FIELD_LABELS && S.FIELD_LABELS[k]) || k));
        field(kv, "", () => (get() || {})[k], (nv) => { const o = Object.assign({}, get() || {}); o[k] = nv; set(o); }, k === "color" ? "color" : "any");
        box.appendChild(kv);
      });
      const clr = h("button", { class: "ghost", type: "button" }, "clear"); clr.onclick = () => { set(undefined); markDirty(); build(); };
      box.appendChild(clr);
    }
    build(); return { refresh: build };
  }
  function stopsField(parent, label, get, set) {
    const box = h("div"); parent.appendChild(box);
    function build() {
      box.innerHTML = "";
      box.appendChild(h("div", { class: "field" }, h("label", null, label)));
      const arr = Array.isArray(get()) ? get() : [];
      arr.forEach(function (s, i) {
        const r = h("div", { class: "kv" });
        const i2 = h("input", { type: "text", spellcheck: "false", value: s }); i2.oninput = () => { const a = arr.slice(); a[i] = i2.value; set(a); markDirty(); };
        const rm = h("button", { class: "ghost danger", type: "button" }, "✕"); rm.onclick = () => { const a = arr.slice(); a.splice(i, 1); set(a); markDirty(); build(); };
        r.appendChild(i2); r.appendChild(h("span")); r.appendChild(rm); box.appendChild(r);
      });
      const add = h("button", { class: "ghost", type: "button" }, "+ stop"); add.onclick = () => { set((Array.isArray(get()) ? get() : []).concat(["#000000"])); markDirty(); build(); };
      box.appendChild(add);
    }
    build(); return { refresh: build };
  }
  function statesField(parent, get, set) {
    const box = h("div"); parent.appendChild(box);
    function build() {
      box.innerHTML = "";
      const arr = Array.isArray(get()) ? get() : [];
      arr.forEach(function (st, i) {
        const d = h("details", { class: "adv" });
        d.appendChild(h("summary", null, "when  " + (st && st.when ? String(st.when) : "")));
        const whenField = h("div"); d.appendChild(whenField);
        field(whenField, "when", () => (st || {}).when, (nv) => { const a = arr.slice(); a[i] = Object.assign({}, st); a[i].when = nv; set(a); markDirty(); }, "any");
        const rest = Object.assign({}, st); delete rest.when;
        d.appendChild(h("div", { class: "muted mono", html: "overrides" }));
        Object.keys(rest).forEach(function (k) {
          const f = h("div"); d.appendChild(f);
          field(f, k, () => rest[k], (nv) => { const a = arr.slice(); a[i] = Object.assign({}, a[i]); a[i][k] = nv; set(a); markDirty(); }, k === "fill" || k === "color" || k === "borderColor" || k === "tint" ? "color" : "any");
        });
        const raw = h("textarea", { rows: 3 }); raw.value = JSON.stringify(rest, null, 1);
        const apply = h("button", { class: "ghost", type: "button" }, "apply overrides JSON"); apply.onclick = () => {
          try { const o = looseParse(raw.value || "{}"); const a = arr.slice(); const n = Object.assign({ when: a[i].when }, o); a[i] = n; set(a); markDirty(); build(); } catch (e) { badJson("a state's overrides", e); }
        };
        d.appendChild(raw); d.appendChild(apply);
        const rm = h("button", { class: "ghost danger", type: "button" }, "remove"); rm.onclick = () => { const a = arr.slice(); a.splice(i, 1); set(a); markDirty(); build(); };
        d.appendChild(rm);
        box.appendChild(d);
      });
      const add = h("button", { class: "ghost", type: "button" }, "+ state"); add.onclick = () => { set((Array.isArray(get()) ? get() : []).concat([{ when: "=sel", fill: "#ffffff" }])); markDirty(); build(); };
      box.appendChild(add);
    }
    build(); return { refresh: build };
  }
  function smoothField(parent, get, set) {
    const box = h("div"); parent.appendChild(box);
    function build() {
      box.innerHTML = "";
      const cur = get();
      if (cur === undefined || cur === null || typeof cur === "number") {
        const f = h("div"); box.appendChild(f);
        field(f, "smooth (ms)", () => (typeof get() === "number" ? get() : ""), (nv) => set(nv), "number");
        const obj = h("button", { class: "ghost", type: "button" }, "per property"); obj.onclick = () => { set({ x: 120, scale: 90 }); markDirty(); build(); };
        box.appendChild(obj);
      } else {
        const keys = Object.keys(cur);
        keys.forEach(function (k) { const f = h("div"); box.appendChild(f); field(f, k, () => cur[k], (nv) => { const o = Object.assign({}, get()); o[k] = nv; set(o); markDirty(); }, "number"); });
        const addKey = h("input", { type: "text", placeholder: "property (x, y, scale…)" });
        const add = h("button", { class: "ghost", type: "button" }, "+"); add.onclick = () => { if (!addKey.value) return; const o = Object.assign({}, get()); o[addKey.value] = 100; set(o); markDirty(); build(); };
        const back = h("button", { class: "ghost", type: "button" }, "one number"); back.onclick = () => { set(100); markDirty(); build(); };
        box.appendChild(h("div", { class: "ctl" }, addKey, add, back));
      }
    }
    build(); return { refresh: build };
  }
  function radiusField(parent, get, set) {
    const box = h("div"); parent.appendChild(box);
    function build() {
      box.innerHTML = "";
      const cur = get();
      if (Array.isArray(cur)) {
        ["tl", "tr", "br", "bl"].forEach(function (n, i) { const f = h("div"); box.appendChild(f); field(f, "radius " + n, () => cur[i], (nv) => { const a = cur.slice(); a[i] = nv; set(a); markDirty(); }, "length"); });
        const one = h("button", { class: "ghost", type: "button" }, "single"); one.onclick = () => { set(12); markDirty(); build(); }; box.appendChild(one);
      } else {
        const f = h("div"); box.appendChild(f); field(f, "radius", () => (typeof get() === "number" || typeof get() === "string" ? get() : ""), (nv) => set(nv), "length");
        const four = h("button", { class: "ghost", type: "button" }, "four corners"); four.onclick = () => { set([12, 12, 12, 12]); markDirty(); build(); }; box.appendChild(four);
      }
    }
    build(); return { refresh: build };
  }
  function borderField(parent, get, set) {
    const box = h("div"); parent.appendChild(box);
    function build() {
      box.innerHTML = "";
      const cur = get();
      const isObj = cur && typeof cur === "object";
      const f = h("div"); box.appendChild(f);
      if (isObj) field(f, "border width", () => cur.width, (nv) => { const o = Object.assign({}, cur); o.width = nv; set(o); markDirty(); }, "number");
      else field(f, "border", () => (typeof cur === "number" ? cur : ""), (nv) => set(nv), "number");
      if (isObj) { const f2 = h("div"); box.appendChild(f2); field(f2, "border colour", () => cur.color, (nv) => { const o = Object.assign({}, cur); o.color = nv; set(o); markDirty(); }, "color"); }
      const t = h("button", { class: "ghost", type: "button" }, isObj ? "one number" : "width + colour");
      t.onclick = () => { set(isObj ? 2 : { width: 2, color: "#ffffff" }); markDirty(); build(); }; box.appendChild(t);
    }
    build(); return { refresh: build };
  }
  function rawField(parent, get) {
    const d = h("details", { class: "adv" }); d.appendChild(h("summary", null, "raw JSON (this element)"));
    const ta = h("textarea", { rows: 10 }); ta.value = JSON.stringify(get(), null, 2);
    const apply = h("button", { class: "ghost", type: "button" }, "apply JSON");
    apply.onclick = () => {
      try {
        const o = looseParse(ta.value);
        const par = getPath(parentOf(state.sel)); const key = state.sel[state.sel.length - 1];
        par[key] = o; markDirty(); renderAll(); rerender();
      } catch (e) { badJson("the element as JSON", e); }
    };
    d.appendChild(ta); d.appendChild(apply); parent.appendChild(d);
  }

  // ---- the element tree ------------------------------------------------------------------------------
  function typeOf(el) {
    return el.type || (el.text !== undefined ? "text" : el.src !== undefined ? "image"
      : el.source !== undefined ? "list" : el.children !== undefined ? "group" : "rect");
  }
  function labelOf(el) {
    const t = typeOf(el);
    if (el.use) return "component: " + el.use;
    if (t === "text") return JSON.stringify(litText(el.text)).slice(0, 34);
    if (t === "list") return (el.source || "") + " · " + (el.direction || "row");
    if (t === "image") return typeof el.src === "string" ? el.src.slice(0, 28) : "";
    const n = Array.isArray(el.children) ? el.children.length : 0;
    return n ? n + (n === 1 ? " child" : " children") : "";
  }
  function hasKids(el) { return Array.isArray(el.children) ? el.children.length > 0 : el.item !== undefined; }
  function renderTree() {
    const pane = $("#pane-elements"); pane.innerHTML = "";
    const vsel = h("div", { class: "viewsel" });
    S.VIEWS.forEach((v) => { const b = h("button", { type: "button" }, v); b.setAttribute("aria-pressed", String(state.view === v)); b.onclick = () => { state.view = v; renderTree(); }; vsel.appendChild(b); });
    pane.appendChild(vsel);
    S.SCREENS.forEach(function (scr) {
      const head = h("div", { class: "screenhead" });
      const nm = h("span", { class: "name" + (state.screen === scr ? " on" : ""), title: "Click a screen to work on it: elements you add go there" }, scr);
      nm.onclick = () => { state.screen = scr; state.addScreen = scr; renderTree(); renderPalette(); };
      head.appendChild(nm);
      const add = h("button", { class: "ghost", type: "button" }, "+ element"); add.onclick = () => addTo(screenArr(scr), ["screens", state.view, scr]); head.appendChild(add);
      pane.appendChild(head);
      const arr = screenArr(scr);
      if (!arr.length) pane.appendChild(h("div", { class: "muted", style: "margin:2px 0 6px" }, emptyNote(state.view, scr)));
      arr.forEach((el, i) => pane.appendChild(node(["screens", state.view, scr, i], el, i, arr)));
    });
  }
  // what the engine draws on a screen with no elements: the next view's (launch -> library -> home), else nothing
  function emptyNote(view, scr) {
    const sc = state.theme.screens || {};
    const has = (v) => Array.isArray(sc[v] && sc[v][scr]) ? sc[v][scr].length > 0 : !!(sc[v] && sc[v][scr]);
    if (view === "all") return "(empty: nothing is drawn over the views)";
    const next = view === "launch" ? ["library", "home"] : view === "library" ? ["home"] : [];
    for (const v of next) if (has(v)) return "(empty: the menu shows the " + v + " " + scr + " screen here — add an element to give this view its own)";
    return "(empty: this screen is black — add an element)";
  }
  function node(path, el, index, arr) {
    const wrap = h("div", { class: "node" });
    const row = h("div", { class: "row" + (samePath(state.sel, path) ? " sel" : "") });
    const kids = hasKids(el);
    const openKey = JSON.stringify(path);
    const caret = h("span", { class: "caret" + (kids ? "" : " leaf") }, state.open[openKey] === false ? "▸" : "▾");
    caret.onclick = (e) => { e.stopPropagation(); state.open[openKey] = state.open[openKey] === false; renderTree(); };
    row.appendChild(caret);
    row.appendChild(h("span", { class: "t" }, (S.TYPE_LABELS && S.TYPE_LABELS[typeOf(el)]) || typeOf(el)));
    const lbl = h("span", { class: "lbl" }, labelOf(el)); lbl.title = el.type || ""; row.appendChild(lbl);
    const acts = h("div", { class: "acts" });
    const mk = (t, fn, danger) => { const b = h("button", { class: "ghost" + (danger ? " danger" : ""), type: "button" }, t); b.onclick = (e) => { e.stopPropagation(); fn(); }; return b; };
    if (Array.isArray(arr)) {
      acts.appendChild(mk("↑", () => { if (index > 0) { const x = arr.splice(index, 1)[0]; arr.splice(index - 1, 0, x); state.sel = parentOf(path).concat(index - 1); markDirty(); renderAll(); rerender(); } }));
      acts.appendChild(mk("↓", () => { if (index < arr.length - 1) { const x = arr.splice(index, 1)[0]; arr.splice(index + 1, 0, x); state.sel = parentOf(path).concat(index + 1); markDirty(); renderAll(); rerender(); } }));
      acts.appendChild(mk("+", () => addTo(arr, parentOf(path), index)));
      acts.appendChild(mk("⧉", () => { arr.splice(index + 1, 0, clone(el)); state.sel = parentOf(path).concat(index + 1); markDirty(); renderAll(); rerender(); }));
    }
    acts.appendChild(mk("✕", () => { delPath(path); state.sel = null; markDirty(); renderAll(); rerender(); }, true));
    row.appendChild(acts);
    row.onclick = () => { state.sel = path; renderTree(); rerender(); };
    row.setAttribute("draggable", "true");
    row.addEventListener("dragstart", (e) => {
      state.drag = { path: path, par: parentOf(path) };
      try { e.dataTransfer.setData("text/plain", path.join(".")); e.dataTransfer.effectAllowed = "move"; } catch (err) { /* (Firefox starts no drag without data) */ }
    });
    row.addEventListener("dragover", (e) => { if (state.drag && samePath(state.drag.par, parentOf(path))) e.preventDefault(); });
    row.addEventListener("drop", (e) => {
      e.preventDefault();
      if (!Array.isArray(arr) || !state.drag || !samePath(state.drag.par, parentOf(path))) return;
      const from = +state.drag.path.slice(-1), to = index;
      if (from === to) return;
      const x = arr.splice(from, 1)[0]; arr.splice(to, 0, x); state.drag = null; state.sel = parentOf(path).concat(to); markDirty(); renderAll(); rerender();
    });
    wrap.appendChild(row);
    if (kids && state.open[openKey] !== false) {
      if (Array.isArray(el.children)) el.children.forEach((c, i) => wrap.appendChild(node(path.concat("children", i), c, i, el.children)));
      else if (el.item !== undefined) {
        const child = h("div"); child.appendChild(node(path.concat("item"), el.item, 0, null));
        const rm = h("button", { class: "ghost danger", type: "button" }, "remove item"); rm.onclick = () => { delete el.item; markDirty(); renderAll(); rerender(); };
        child.appendChild(rm); wrap.appendChild(child);
      }
      if (el.type === "group" || el.children !== undefined) {
        const addc = h("button", { class: "ghost", type: "button" }, "+ child"); addc.onclick = () => { if (!Array.isArray(el.children)) el.children = []; addTo(el.children, path.concat("children")); };
        wrap.appendChild(addc);
      }
    }
    if (!kids && el.type === "group" && !Array.isArray(el.children)) {
      const addc = h("button", { class: "ghost", type: "button" }, "+ child"); addc.onclick = () => { el.children = []; markDirty(); renderAll(); rerender(); };
      wrap.appendChild(addc);
    }
    return wrap;
  }
  function addTo(arr, parPath, at) {
    const type = (window.prompt("Element type — rect, text, image, group or list", "rect") || "").trim().toLowerCase();
    if (["rect", "text", "image", "group", "list"].indexOf(type) < 0) return;
    const el = makeElement(type);
    const idx = at === undefined ? arr.length : at + 1;
    arr.splice(idx, 0, el);
    state.sel = parPath.concat(idx); state.open[JSON.stringify(parPath)] = true;
    markDirty(); renderAll(); rerender();
  }
  function makeElement(type) {
    if (type === "text") return { type: "text", x: 20, y: 20, text: "Text", size: 20, color: "#ffffff" };
    if (type === "image") return { type: "image", x: 20, y: 20, w: 200, h: 200, src: "", fit: "contain" };
    if (type === "group") return { type: "group", x: 0, y: 0, w: 200, h: 200, children: [] };
    if (type === "list") return { type: "list", source: "systems", direction: "row", x: 0, y: 100, w: 640, h: 240, itemW: 150, itemH: 190, gap: 14, scrollMode: "center", item: { type: "group", w: 150, h: 190, children: [] } };
    return { type: "rect", x: 0, y: 0, w: 200, h: 100, fill: "#202430" };
  }
  

  // ---- the inspector ---------------------------------------------------------------------------------
  function renderInspector() {
    const pane = $("#inspector"); pane.innerHTML = "";
    if (!state.sel) { pane.appendChild(h("div", { class: "muted", style: "padding:10px" }, "Select an element in the tree to edit it.")); return; }
    const el = getPath(state.sel);
    if (!el || typeof el !== "object") { state.sel = null; renderInspector(); return; }
    const crumbs = state.sel.slice(1).map((s) => (/^\d+$/.test(String(s)) ? "element " + (+s + 1) : s));
    pane.appendChild(h("div", { class: "muted", style: "font-size:11px;margin:2px 0 6px", title: "Where it sits in the file: " + state.sel.join(".") }, crumbs.join("  ›  ")));
    // type
    const trow = h("div", { class: "field" }); trow.appendChild(h("label", null, "Type"));
    const tctl = h("div", { class: "ctl" });
    const cur0 = typeOf(el);
    const sel = h("select"); ["rect", "text", "image", "group", "list"].forEach((t) => { const o = h("option", { value: t }, S.TYPE_LABELS[t] || t); if (cur0 === t) o.setAttribute("selected", ""); sel.appendChild(o); });
    sel.title = "What this element is: a rectangle, some text, a picture, a box of children, or a list of repeats";
    sel.onchange = () => { el.type = sel.value; markDirty(); renderAll(); rerender(); };
    tctl.appendChild(sel);
    const dup = h("button", { class: "ghost", type: "button" }, "delete"); dup.onclick = () => { delPath(state.sel); state.sel = null; markDirty(); renderAll(); rerender(); };
    tctl.appendChild(dup); trow.appendChild(tctl); pane.appendChild(trow);

    // component use
    const comps = Object.keys(state.theme.components || {});
    pane.appendChild(h("h3", { class: "sec" }, "Reusable component"));
    field(pane, "use", () => el.use, (nv) => { if (nv) el.use = nv; else delete el.use; markDirty(); }, "enum", comps.concat([""]));
    if (el.use) { kvField(pane, "with", () => el.with, (nv) => { if (nv) el.with = nv; else delete el.with; }, []); rawWitheditor(pane, el); }

    pane.appendChild(h("h3", { class: "sec" }, "Position, size and visibility"));
    S.COMMON.forEach((p) => field(pane, p[0], () => el[p[0]], (nv) => { if (nv === "" || nv === undefined) delete el[p[0]]; else el[p[0]] = nv; markDirty(); }, p[1], p[2]));
    field(pane, "onTap", () => el.onTap, (nv) => { if (nv) el.onTap = nv; else delete el.onTap; markDirty(); }, "enum", S.BUTTONS);

    pane.appendChild(h("h3", { class: "sec" }, (S.TYPE_LABELS[typeOf(el)] || typeOf(el)) + " settings"));
    (S.TYPE_PROPS[typeOf(el)] || []).forEach(function (p) {
      const k = p[0], kind = p[1];
      if (k === "children") { pane.appendChild(h("div", { class: "field" }, h("label", null, "Children"), h("span", { class: "muted" }, (Array.isArray(el.children) ? el.children.length : 0) + " (edit in the tree)"))); return; }
      if (k === "item") { pane.appendChild(h("div", { class: "field" }, h("label", null, "Item"), h("span", { class: "muted" }, el.item ? "1 (edit in the tree)" : "none"))); return; }
      if (kind === "object" && k === "gradient") {
        const f = h("div"); pane.appendChild(f);
        field(f, "gradient angle", () => (el.gradient || {}).angle, (nv) => { el.gradient = Object.assign({}, el.gradient || {}); el.gradient.angle = nv; markDirty(); }, "number");
        stopsField(pane, "gradient stops", () => (el.gradient || {}).stops, (a) => { el.gradient = Object.assign({}, el.gradient || {}); el.gradient.stops = a; markDirty(); });
        return;
      }
      if (kind === "object" && (k === "shadow" || k === "outline")) { kvField(pane, k, () => el[k], (o) => { if (o === undefined) delete el[k]; else el[k] = o; markDirty(); }, k === "shadow" ? ["x", "y", "blur", "spread", "color"] : ["width", "color"]); return; }
      if (kind === "object" && k === "states") { pane.appendChild(h("h3", { class: "sec" }, "states")); statesField(pane, () => el.states, (a) => { if (a && a.length) el.states = a; else delete el.states; markDirty(); }); return; }
      if (kind === "object" && k === "smooth") { pane.appendChild(h("h3", { class: "sec" }, "smooth")); smoothField(pane, () => el.smooth, (o) => { if (o === undefined) delete el.smooth; else el.smooth = o; markDirty(); }); return; }
      if (k === "radius") { radiusField(pane, () => el.radius, (v) => { if (v === "" || v === undefined) delete el.radius; else el.radius = v; markDirty(); }); return; }
      if (k === "border") { borderField(pane, () => el.border, (v) => { if (v === "" || v === undefined || v === 0) delete el.border; else el.border = v; markDirty(); }); return; }
      if (kind === "tree" || kind === "object") return;
      field(pane, k, () => el[k], (nv) => { if (nv === "" || nv === undefined) delete el[k]; else el[k] = nv; markDirty(); }, kind, p[2]);
    });
    rawField(pane, () => getPath(state.sel));
  }
  function rawWitheditor(pane, el) {
    const ta = h("textarea", { rows: 3 }); ta.value = JSON.stringify(el.with || {}, null, 1);
    const apply = h("button", { class: "ghost", type: "button" }, "set with JSON");
    apply.onclick = () => { try { el.with = looseParse(ta.value || "{}"); markDirty(); renderInspector(); } catch (e) { badJson("the component's values", e); } };
    pane.appendChild(ta); pane.appendChild(apply);
  }

  // ---- colour keys, fonts, icons, sounds, background --------------------------------------------------
  function renderColours() {
    const pane = $("#pane-colours"); pane.innerHTML = "";
    const th = state.theme;
    const baseRow = h("div", { class: "field" }); baseRow.appendChild(h("label", null, "base"));
    const bsel = h("select"); ["dark", "light"].forEach((b) => { const o = h("option", { value: b }, b); if (th.base === b) o.setAttribute("selected", ""); bsel.appendChild(o); });
    bsel.onchange = () => { th.base = bsel.value; markDirty(); renderColours(); }; baseRow.appendChild(h("div", { class: "ctl" }, bsel)); pane.appendChild(baseRow);

    // accent
    const a = th.accent;
    const amode = typeof a === "object" ? "five" : (S.ACCENTS[a] ? a : "custom");
    const arow = h("div", { class: "field" }); arow.appendChild(h("label", null, "accent"));
    const actl = h("div", { class: "ctl" });
    const asel = h("select");
    Object.keys(S.ACCENTS).forEach((n) => { const o = h("option", { value: n }, n); if (amode === n) o.setAttribute("selected", ""); asel.appendChild(o); });
    asel.appendChild(h("option", { value: "custom" }, "custom colour")); asel.appendChild(h("option", { value: "five" }, "five shades"));
    if (amode === "custom") asel.value = "custom"; if (amode === "five") asel.value = "five";
    actl.appendChild(asel);
    if (amode === "custom") { const c = h("input", { type: "color", value: (E.parseColorText(typeof a === "string" ? a : "#7c9cff") === null ? "#7c9cff" : (typeof a === "string" ? a.slice(0, 7) : "#7c9cff")) }); c.oninput = () => { th.accent = c.value; markDirty(); }; actl.appendChild(c); }
    asel.onchange = () => { if (asel.value === "five") th.accent = { base: accentBase(), light: accentShades().light.slice(0, 7), dark: accentShades().dark.slice(0, 7), band: accentShades().band.slice(0, 7), low: accentShades().low.slice(0, 7) }; else if (asel.value === "custom") th.accent = typeof a === "string" && E.parseColorText(a) !== null ? (a.slice(0, 7)) : "#7c9cff"; else th.accent = asel.value; markDirty(); renderColours(); };
    arow.appendChild(actl); pane.appendChild(arow);
    if (amode === "five") S.ACCENT_SHADES.forEach((s) => field(pane, "accent." + s, () => (th.accent || {})[s], (v) => { th.accent[s] = v; markDirty(); }, "color"));

    // colour keys
    const cols = th.colors || (th.colors = {});
    S.COLOR_GROUPS.forEach((g) => {
      const d = h("details", { class: "adv" }); d.appendChild(h("summary", null, g[0]));
      g[1].forEach(function (k) {
        const row = h("div", { class: "key" });
        const nm = h("span", { class: "name" + (cols[k] !== undefined ? " set" : "") }, k); row.appendChild(nm);
        const base = (th.base === "light" ? S.LIGHT : S.DARK)[k]; const shown = cols[k] || (base === "@accent" ? accentBase() : base);
        const argb = E.parseColorText(shown) === null ? 0xff000000 : E.parseColorText(shown); const p = argbParts(argb);
        const ci = h("input", { type: "color", value: "#" + (argb & 0xffffff).toString(16).padStart(6, "0") }); row.appendChild(ci);
        const ai = h("input", { type: "range", min: 0, max: 255, value: p.a }); row.appendChild(ai);
        const hx = h("span", { class: "hex" }, shown); row.appendChild(hx);
        const rm = h("button", { class: "ghost", type: "button" }, "↺"); rm.onclick = () => { delete cols[k]; markDirty(); renderColours(); }; row.appendChild(rm);
        const put = () => { const av = +ai.value; const s = ci.value + (av === 255 ? "" : av.toString(16).padStart(2, "0")); cols[k] = s; hx.textContent = s; nm.classList.add("set"); markDirty(); };
        ci.oninput = put; ai.oninput = put;
        d.appendChild(row);
      });
      pane.appendChild(d);
    });

    // fonts, logo, background
    pane.appendChild(h("h3", { class: "sec" }, "fonts & pictures"));
    const fonts = th.fonts || (th.fonts = {});
    field(pane, "font regular", () => fonts.regular, (v) => { if (v) fonts.regular = v; else delete fonts.regular; markDirty(); }, "text");
    field(pane, "font medium", () => fonts.medium, (v) => { if (v) fonts.medium = v; else delete fonts.medium; markDirty(); }, "text");
    field(pane, "logo", () => th.logo, (v) => { if (v) th.logo = v; else delete th.logo; markDirty(); }, "text");
    const bg = th.background || (th.background = {});
    field(pane, "background top", () => bg.top, (v) => { if (v) bg.top = v; else delete bg.top; markDirty(); }, "text");
    field(pane, "background bottom", () => bg.bottom, (v) => { if (v) bg.bottom = v; else delete bg.bottom; markDirty(); }, "text");
    field(pane, "dither", () => bg.dither, (v) => { if (v === "" || v === undefined) delete bg.dither; else bg.dither = v; markDirty(); }, "bool");

    // sounds
    pane.appendChild(h("h3", { class: "sec" }, "sounds"));
    const snd = th.sounds || (th.sounds = {});
    S.SOUNDS.forEach((s) => { const f = h("div"); pane.appendChild(f); field(f, s, () => snd[s], (v) => { if (v) snd[s] = v; else delete snd[s]; markDirty(); }, "text"); });

  }

  // ---- system icons: a picture per system id, in place of the menu's own (theme.cpp's themedIcon) ------------
  // Every system systems.conf lists, and the collections, with the built-in icon beside the theme's. Picking a file
  // attaches it to the theme (it travels in the .zip) under <theme>/icons/<id>.<ext>; Reset puts the built-in one
  // back. The preview shows it at once; "every system" in the preview's library puts each one on the shelf.
  function iconSystems() {
    const out = ENGINE && ENGINE.ready() ? ENGINE.systems()
      : S.SYSTEM_IDS.map((id) => ({ id: id, name: id, icon: "" })).concat([{ id: "favorites", name: "Favorites", icon: "" }, { id: "recently-played", name: "Recently played", icon: "" }, { id: "search", name: "Search", icon: "" }]);
    (state.systemIds || []).forEach((id) => { if (!out.some((x) => x.id === id)) out.push({ id: id, name: id, icon: "" }); });
    Object.keys(state.theme.icons || {}).forEach((id) => { if (!out.some((x) => x.id === id)) out.push({ id: id, name: id + "  (not a system Banana knows)", icon: "", unknown: true }); });
    return out;
  }
  const builtinUrl = {};
  function builtinIcon(file) {
    if (!file || !ENGINE || !ENGINE.ready()) return "";
    if (builtinUrl[file] === undefined) { const b = ENGINE.readFile(file); builtinUrl[file] = b ? URL.createObjectURL(new Blob([b], { type: "image/png" })) : ""; }
    return builtinUrl[file];
  }
  function renderIcons() {
    const pane = $("#pane-icons"); if (!pane) return; pane.innerHTML = "";
    const th = state.theme;
    pane.appendChild(h("h3", { class: "sec" }, "system icons"));
    pane.appendChild(h("div", { class: "muted", style: "font-size:12px" }, "A picture per system replaces the menu's icon for it everywhere (home, lists, Settings). PNG or JPEG, at most 4096 px a side; the built-in ones are 32 × 32 pixel art, drawn sharp. Pick a file and it is attached to the theme."));
    const list = h("div", { class: "icons" });
    iconSystems().forEach(function (sys) {
      const path = th.icons && typeof th.icons[sys.id] === "string" ? th.icons[sys.id] : "";
      const a = path ? state.assets[path] : null;
      const row = h("div", { class: "icon-row" + (path ? " set" : "") });
      const def = h("span", { class: "ic", title: "the menu's own" + (sys.icon ? ": " + sys.icon.replace(/^\/rnds\//, "ui/assets/rnds/") : "") });
      const du = builtinIcon(sys.icon); if (du) def.appendChild(h("img", { src: du, alt: "" }));
      row.appendChild(def);
      const mine = h("span", { class: "ic mine", title: path || "no picture of the theme's" });
      if (a) mine.appendChild(h("img", { src: assetUrl(a), alt: "" })); else if (path) mine.appendChild(h("span", { class: "bad" }, "?"));
      row.appendChild(mine);
      const nm = h("div", { class: "nm" }, h("b", null, sys.id), h("span", { class: "muted" }, " " + sys.name));
      nm.appendChild(h("div", { class: "muted mono", style: "font-size:11px" }, path ? path + (a ? "" : "  — not attached: the handheld shows its own unless this file is beside the theme") : "the menu's own"));
      row.appendChild(nm);
      const pick = h("input", { type: "file", accept: ".png,.jpg,.jpeg,image/png,image/jpeg", class: "hidden" });
      pick.addEventListener("change", function () { const f = this.files[0]; this.value = ""; if (f) setIcon(sys.id, f); });
      const choose = h("button", { class: "ghost", type: "button" }, path ? "Replace…" : "Choose…"); choose.onclick = () => pick.click();
      row.appendChild(h("div", { class: "ctl" }, choose, pick));
      if (path) { const rs = h("button", { class: "ghost danger", type: "button", title: "back to the menu's own icon" }, "Reset"); rs.onclick = () => resetIcon(sys.id); row.lastChild.appendChild(rs); }
      list.appendChild(row);
    });
    pane.appendChild(list);
    const other = h("input", { type: "text", placeholder: "another system id" });
    const add = h("button", { class: "ghost", type: "button" }, "add"); add.onclick = () => {
      const id = other.value.trim(); if (!id) return;
      th.icons = th.icons || {}; if (!th.icons[id]) th.icons[id] = fileStem() + "/icons/" + id + ".png"; markDirty(); renderIcons();
    };
    pane.appendChild(h("div", { class: "ctl", style: "margin-top:6px" }, other, add));
    const show = h("button", { class: "ghost", type: "button", title: "the preview's library with a game for every system, so each icon is on the shelf" }, "Show every system in the preview");
    show.onclick = () => { setLibrary(true); showTab("center", "design"); };
    pane.appendChild(h("div", { class: "ctl", style: "margin-top:4px" }, show));
  }
  async function setIcon(id, file) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    const info = pictureInfo(bytes);
    if (!info) { alert(file.name + " isn't a PNG or JPEG picture, which is all the menu reads."); return; }
    if (info.w > 4096 || info.h > 4096) { alert(file.name + " is " + info.w + " × " + info.h + ". The menu reads pictures up to 4096 px a side and would show its own icon instead."); return; }
    const th = state.theme; th.icons = th.icons || {};
    const old = th.icons[id], ext = info.type === "jpeg" ? (/\.jpe?g$/i.test(file.name) ? file.name.slice(file.name.lastIndexOf(".")).toLowerCase() : ".jpg") : ".png";
    // its own file under the theme's folder; a path the theme already gave it is kept when it has the right kind
    const path = old && Engine_safe(old) && (info.type === "jpeg" ? /\.jpe?g$/i.test(old) : /\.png$/i.test(old)) ? old : fileStem() + "/icons/" + id + ext;
    th.icons[id] = path;
    putAsset(path, file.name, bytes);
    if (old && old !== path) dropAssetIfUnused(old);
    markDirty(); renderIcons(); renderAssets();
    setStatus(id + ": " + file.name + " (" + info.w + " × " + info.h + ")", "ok");
  }
  function resetIcon(id) {
    const th = state.theme, old = th.icons && th.icons[id];
    if (!old && old !== "") return;
    delete th.icons[id];
    dropAssetIfUnused(old);
    markDirty(); renderIcons(); renderAssets();
  }
  const Engine_safe = (p) => !!(ENGINE ? ENGINE.safeRel(p) : p);

  // ---- vars ------------------------------------------------------------------------------------------
  function renderVars() {
    const pane = $("#pane-vars"); pane.innerHTML = "";
    pane.appendChild(h("p", { class: "muted" }, "Named values any expression can read (e.g. vars: { bg: \"#10131c\", hi: \"=accent\" }). Used in every colour and number field as =name."));
    const vars = state.theme.vars || (state.theme.vars = {});
    Object.keys(vars).forEach((k) => { const f = h("div"); pane.appendChild(f); field(f, k, () => vars[k], (v) => { if (v === "" || v === undefined) delete vars[k]; else vars[k] = v; markDirty(); }, "any"); });
    const nk = h("input", { type: "text", placeholder: "name" }); const nv = h("input", { type: "text", placeholder: "value  (#10131c or =accent)" });
    const add = h("button", { class: "ghost", type: "button" }, "add"); add.onclick = () => { if (nk.value) { vars[nk.value] = nv.value || ""; markDirty(); renderVars(); } };
    pane.appendChild(h("div", { class: "ctl" }, nk, nv, add));
  }

  // ---- components ------------------------------------------------------------------------------------
  function renderComponents() {
    const pane = $("#pane-components"); pane.innerHTML = "";
    pane.appendChild(h("p", { class: "muted" }, "Reusable elements. An instance is {\"use\": \"name\", …overrides}. Editing the component changes every instance."));
    const comps = state.theme.components || (state.theme.components = {});
    const names = Object.keys(comps);
    if (!names.length) pane.appendChild(h("div", { class: "muted" }, "(none yet)"));
    names.forEach(function (n) {
      const row = h("div", { class: "ctl", style: "margin:3px 0" });
      const edit = h("button", { type: "button" }, n); edit.onclick = () => { state.sel = ["components", n]; showTab("properties", "inspector"); renderTree(); rerender(); };
      const ren = h("button", { class: "ghost", type: "button" }, "rename"); ren.onclick = () => {
        const nn = (prompt("Rename component", n) || "").trim();
        if (!nn || nn === n) return;
        if (comps[nn]) { alert("There is already a component called " + nn + "."); return; }
        // the same place in the key order, and every instance follows
        const all = {}; Object.keys(comps).forEach((k) => { all[k === n ? nn : k] = comps[k]; });
        Object.keys(comps).forEach((k) => delete comps[k]); Object.assign(comps, all);
        eachElement((el) => { if (el.use === n) el.use = nn; });
        if (state.sel && state.sel[0] === "components" && state.sel[1] === n) state.sel = ["components", nn];
        markDirty(); renderAll(); rerender();
      };
      const del = h("button", { class: "ghost danger", type: "button" }, "✕"); del.onclick = () => {
        let used = 0; eachElement((el) => { if (el.use === n) used++; });
        if (used && !confirm(n + " is used " + used + (used === 1 ? " time" : " times") + ". Delete it anyway? Those elements will draw nothing.")) return;
        delete comps[n]; if (state.sel && state.sel[0] === "components" && state.sel[1] === n) state.sel = null; markDirty(); renderAll(); rerender();
      };
      row.appendChild(edit); row.appendChild(ren); row.appendChild(del); pane.appendChild(row);
    });
    const nn = h("input", { type: "text", placeholder: "new component name" });
    const add = h("button", { class: "ghost", type: "button" }, "add"); add.onclick = () => { if (nn.value) { comps[nn.value] = { type: "group", children: [] }; markDirty(); renderComponents(); } };
    pane.appendChild(h("div", { class: "ctl" }, nn, add));
  }

  // ---- input / hints / behaviour / theme --------------------------------------------------------------
  function renderInput() {
    const pane = $("#pane-input"); pane.innerHTML = "";
    pane.appendChild(h("p", { class: "muted" }, "What each button does. \"home\" is the screen you pick a system on; \"library\" is a system's games."));
    const inp = state.theme.input || (state.theme.input = {});
    const BTN = { left: "Left", right: "Right", up: "Up", down: "Down", a: "A", b: "B", x: "X", y: "Y", l: "L", r: "R" };
    ["home", "library"].forEach(function (v) {
      inp[v] = inp[v] || {};
      pane.appendChild(h("h3", { class: "sec" }, v));
      S.BUTTONS.forEach((b) => { const f = h("div"); pane.appendChild(f); field(f, b, () => inp[v][b], (nv) => { if (nv) inp[v][b] = nv; else delete inp[v][b]; markDirty(); }, "enum", S.BUTTON_ACTIONS, h("label", null, BTN[b] || b)); });
    });
    pane.appendChild(h("h3", { class: "sec" }, "hints"));
    const hints = state.theme.hints || (state.theme.hints = {});
    ["home", "library"].forEach(function (v) {
      pane.appendChild(h("div", { class: "muted mono" }, v));
      const arr = hints[v] || (hints[v] = []);
      arr.forEach(function (pair, i) {
        const row = h("div", { class: "kv" });
        const k = h("input", { type: "text", value: pair[0] || "" }); k.oninput = () => { arr[i] = [k.value, arr[i][1] || ""]; markDirty(); };
        const l = h("input", { type: "text", value: pair[1] || "" }); l.oninput = () => { arr[i] = [arr[i][0] || "", l.value]; markDirty(); };
        const rm = h("button", { class: "ghost danger", type: "button" }, "✕"); rm.onclick = () => { arr.splice(i, 1); markDirty(); renderInput(); };
        row.appendChild(k); row.appendChild(l); row.appendChild(rm); pane.appendChild(row);
      });
      const add = h("button", { class: "ghost", type: "button" }, "+ hint"); add.onclick = () => { arr.push(["A", "Open"]); markDirty(); renderInput(); };
      pane.appendChild(add);
    });
  }
  function renderBehaviour() {
    const pane = $("#pane-behaviour"); pane.innerHTML = "";
    const b = state.theme.behavior || (state.theme.behavior = {});
    // the engine's names, in plain words
    const BL = { launchMs: "Game start lasts (ms)", scrollMs: "Scrolling speed (ms)", settleMs: "Settle after a move (ms)",
      fps: "Redraws a second", wrap: "Wrap the buttons around", extraHints: "Add Banana's own hints" };
    const BT = { launchMs: "How long the \"Starting…\" view stays up before the game runs",
      scrollMs: "How fast a list scrolls when you hold a direction",
      settleMs: "How long it keeps animating after the selection moves (the theme's own move animations)",
      fps: "Only matters if the theme reads the time hook; it redraws this often",
      wrap: "Up from the first item goes round to the last",
      extraHints: "Banana's own hints (SELECT Search, and X Options on a DS game) sit beside the theme's" };
    const blab = (k) => { const l = h("label", null, BL[k] || k); l.title = BT[k] || ""; return l; };
    ["launchMs", "scrollMs", "settleMs", "fps"].forEach((k) => { const f = h("div"); pane.appendChild(f); field(f, k, () => b[k], (nv) => { if (nv === "" || nv === undefined) delete b[k]; else b[k] = nv; markDirty(); }, "number", null, blab(k)); });
    ["wrap", "extraHints"].forEach((k) => { const f = h("div"); pane.appendChild(f); field(f, k, () => b[k], (nv) => { if (nv === "" || nv === undefined) delete b[k]; else b[k] = nv; markDirty(); }, "bool", null, blab(k)); });
    pane.appendChild(h("h3", { class: "sec" }, "Sound cue times (ms)"));
    b.cues = b.cues || {};
    ["insert", "scroll", "select", "back", "fav", "ready"].forEach((k) => { const f = h("div"); pane.appendChild(f); field(f, k, () => b.cues[k], (nv) => { if (nv === "" || nv === undefined) delete b.cues[k]; else b.cues[k] = nv; markDirty(); }, "number"); });
    pane.appendChild(h("h3", { class: "sec" }, "other behavior members (raw)"));
    const ta = h("textarea", { rows: 5 }); ta.value = JSON.stringify(state.theme.behavior, null, 2);
    const apply = h("button", { class: "ghost", type: "button" }, "apply behavior JSON");
    apply.onclick = () => { try { state.theme.behavior = looseParse(ta.value || "{}"); markDirty(); renderBehaviour(); } catch (e) { badJson("the behaviour settings", e); } };
    pane.appendChild(ta); pane.appendChild(apply);
  }
  function renderTheme() {
    const pane = $("#pane-theme"); pane.innerHTML = "";
    field(pane, "name", () => state.theme.name, (v) => { state.theme.name = v; markDirty(); }, "text");
    field(pane, "author", () => state.theme.author, (v) => { state.theme.author = v; markDirty(); }, "text");
    field(pane, "debug", () => state.theme.debug, (v) => { if (v === "" || v === undefined) delete state.theme.debug; else state.theme.debug = v; markDirty(); }, "bool");
    field(pane, "font (default)", () => state.theme.font, (v) => { state.theme.font = v; markDirty(); }, "enum", S.BUILTIN_FONTS);
    pane.appendChild(h("h3", { class: "sec" }, "your themes"));
    if (!RENDER || !RENDER.available || !RENDER.localThemes) {
      pane.appendChild(h("div", { class: "note", html: "Keep your themes in a folder of their own (say <code>~/BananaThemes</code>), outside the repo: <b>Open folder</b> opens one with its pictures, <b>Export (.zip)</b> saves it with them. With the local server started as <code>python3 tools/theme-studio/serve.py --themes ~/BananaThemes</code> they are also listed here, with Save here." }));
    } else {
      pane.appendChild(h("div", { class: "muted", style: "font-size:12px" }, "in " + RENDER.localThemes));
      const save = h("button", { class: "primary", type: "button" }, "Save here");
      save.onclick = async () => {
        try { const r = await RENDER.saveLocal(fileStem(), themeOut(), assetList()); state.unsaved = false; titleNow(); setStatus("saved to " + r.path, "ok"); loadLocalThemes(); }
        catch (e) { setStatus("save failed: " + e.message, "bad"); }
      };
      pane.appendChild(h("div", { class: "ctl", style: "margin:4px 0" }, save));
      const list = state.localThemes || [];
      if (!list.length) pane.appendChild(h("div", { class: "muted" }, "(nothing there yet — Save here puts one in)"));
      list.forEach(function (t) {
        const b = h("button", { type: "button" }, t.name);
        b.onclick = () => { if (!guardUnsaved("Open " + t.name)) return; try { loadTheme(looseParse(t.json), null, "opened " + t.name); } catch (e) { setStatus("couldn't load " + t.name + ": " + e.message, "bad"); } };
        pane.appendChild(h("div", { class: "ctl" }, b));
      });
    }
    pane.appendChild(h("h3", { class: "sec templates" }, "Start from a template"));
    pane.appendChild(h("p", { class: "muted" }, "One of the example themes BananaOS ships. Pick one and change it — that is the easiest way in."));
    (state.templates || []).forEach(function (t) {
      const b = h("button", { type: "button" }, t.name); b.title = t.file || "";
      b.onclick = () => { if (!guardUnsaved("Start from " + t.name)) return; loadTheme(looseParse(t.json), null, "started from " + t.name + " (Save or Export gives it your own name: change name first)"); };
      pane.appendChild(h("div", { class: "ctl" }, b, h("span", { class: "muted", style: "font-size:12px" }, t.file || "")));
    });
    const any = h("button", { class: "ghost", type: "button" }, "Start from the simple example"); any.onclick = () => { if (guardUnsaved("Start a new theme")) loadTheme(clone(STARTER), null, "a new theme"); };
    pane.appendChild(h("div", { class: "ctl" }, any));
    pane.appendChild(h("p", { class: "muted", style: "font-size:12px" }, "New, up top, always comes back to the simple example."));
  }

  // ---- assets ----------------------------------------------------------------------------------------
  // The files a theme names (pictures, fonts, sounds) by paths relative to its .json. The page can't read them off
  // the disk by those paths (a web page only gets the files it is given), so they are attached: picked here, dropped
  // on the page, or opened with the theme as a folder or a .zip. Attached files go to the preview's engine, into
  // Export (.zip) and to the server's Save here / Send to handheld.
  const ASSET_EXTS = ["png", "jpg", "jpeg", "webp", "ttf", "otf", "wav"];
  const MIME = { png: "image/png", jpg: "image/jpeg", jpeg: "image/jpeg", webp: "image/webp", ttf: "font/ttf", otf: "font/otf", wav: "audio/wav" };
  function assetExt(p) { const m = /\.([a-z0-9]+)$/i.exec(p || ""); return m ? m[1].toLowerCase() : ""; }
  // the relative paths the theme names (a string that looks like a file the engine could load), and where
  function detectAssets() {
    const found = [], seen = {};
    (function scan(v, at) {
      if (typeof v === "string") {
        if (v && v.charAt(0) !== "=" && v.indexOf("{") < 0 && ASSET_EXTS.indexOf(assetExt(v)) >= 0) {
          if (!seen[v]) { seen[v] = []; found.push(v); }
          seen[v].push(at);
        }
      } else if (Array.isArray(v)) v.forEach((x, i) => scan(x, at.concat(i)));
      else if (v && typeof v === "object") Object.keys(v).forEach((k) => scan(v[k], at.concat(k)));
    })(state.theme, []);
    detectAssets.where = seen;
    return found;
  }
  // the first bytes of a picture: { type, w, h } for a PNG or a JPEG (what stb_image reads), else null
  function pictureInfo(b) {
    if (b.length > 24 && b[0] === 0x89 && b[1] === 0x50 && b[2] === 0x4e && b[3] === 0x47)
      return { type: "png", w: ((b[16] << 24) | (b[17] << 16) | (b[18] << 8) | b[19]) >>> 0, h: ((b[20] << 24) | (b[21] << 16) | (b[22] << 8) | b[23]) >>> 0 };
    if (b.length > 4 && b[0] === 0xff && b[1] === 0xd8) {
      for (let i = 2; i + 9 < b.length;) {
        if (b[i] !== 0xff) { i++; continue; }
        const m = b[i + 1], len = (b[i + 2] << 8) | b[i + 3];
        if (m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc) return { type: "jpeg", w: (b[i + 7] << 8) | b[i + 8], h: (b[i + 5] << 8) | b[i + 6] };
        i += 2 + len;
      }
      return { type: "jpeg", w: 0, h: 0 };
    }
    return null;
  }
  function putAsset(path, name, bytes) {
    const old = state.assets[path];
    if (old && old.url) URL.revokeObjectURL(old.url);
    state.assets[path] = { name: name, bytes: bytes, info: pictureInfo(bytes) };
  }
  function dropAssetIfUnused(path) {
    if (!path || !state.assets[path]) return;
    if (detectAssets().indexOf(path) >= 0) return;
    if (state.assets[path].url) URL.revokeObjectURL(state.assets[path].url);
    delete state.assets[path];
  }
  function assetUrl(a) {
    if (!a.url) a.url = URL.createObjectURL(new Blob([a.bytes], { type: MIME[assetExt(a.name)] || (a.info ? "image/" + a.info.type : "application/octet-stream") }));
    return a.url;
  }
  function b64(bytes) {
    let s = "";
    for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
    return btoa(s);
  }
  // for serve.py: [{ path, data: base64 }]
  function assetList() { return Object.keys(state.assets).map((p) => ({ path: p, data: b64(state.assets[p].bytes) })); }
  function assetFiles() { const o = {}; Object.keys(state.assets).forEach((p) => { o[p] = state.assets[p].bytes; }); return o; }
  async function attach(path, file) {
    if (!file) return;
    const rel = ENGINE ? ENGINE.safeRel(path) : path;
    if (!rel) { alert("“" + path + "” isn't a path beside the theme (no leading /, no ..), so the handheld couldn't find it."); return; }
    putAsset(rel, file.name, new Uint8Array(await file.arrayBuffer()));
    const a = state.assets[rel];
    if (/^(png|jpe?g)$/.test(assetExt(rel)) && !a.info) setStatus(file.name + " isn't a PNG or JPEG: the menu won't draw it", "bad");
    else if (a.info && (a.info.w > 4096 || a.info.h > 4096)) setStatus(file.name + " is " + a.info.w + " × " + a.info.h + ": over 4096 px, the menu uses its default instead", "bad");
    else setStatus("attached " + file.name + " as " + rel, "ok");
    markDirty(); renderAssets(); renderIcons();
  }
  function fmtSize(n) { return n < 1024 ? n + " B" : n < 1048576 ? (n / 1024).toFixed(0) + " KB" : (n / 1048576).toFixed(1) + " MB"; }
  function renderAssets() {
    const pane = $("#pane-assets"); pane.innerHTML = "";
    pane.appendChild(h("p", { class: "muted" }, "Pictures, fonts and sounds the theme names, as paths relative to its .json. Attach each one (or drop files on the page) and the preview, Export (.zip) and Send to handheld carry it; a file that isn't there is replaced by the menu's default, as on the handheld."));
    const refs = detectAssets(), where = detectAssets.where || {};
    if (!refs.length) pane.appendChild(h("div", { class: "muted" }, "this theme names no pictures, fonts or sounds yet"));
    refs.forEach(function (p) {
      const a = state.assets[p];
      const row = h("div", { class: "asset" + (a ? "" : " missing") });
      const th = h("span", { class: "ic" });
      if (a && a.info) th.appendChild(h("img", { src: assetUrl(a), alt: "" }));
      row.appendChild(th);
      const info = h("div", { class: "nm" }, h("span", { class: "mono" }, p));
      const used = (where[p] || []).map((w) => w.join(".")).slice(0, 2).join(", ");
      info.appendChild(h("div", { class: "muted", style: "font-size:11px" }, a ? a.name + " · " + fmtSize(a.bytes.length) + (a.info ? " · " + a.info.w + " × " + a.info.h : "") + "  — " + used : "not attached — " + used));
      row.appendChild(info);
      const file = h("input", { type: "file", accept: ".png,.jpg,.jpeg,.webp,.ttf,.otf,.wav", class: "hidden" });
      file.addEventListener("change", function () { const f = this.files[0]; this.value = ""; attach(p, f); });
      const pick = h("button", { class: "ghost", type: "button" }, a ? "Replace…" : "Attach…"); pick.onclick = () => file.click();
      const ctl = h("div", { class: "ctl" }, pick, file);
      if (a) { const rm = h("button", { class: "ghost danger", type: "button", title: "detach the file (the theme still names it)" }, "✕"); rm.onclick = () => { if (a.url) URL.revokeObjectURL(a.url); delete state.assets[p]; markDirty(); renderAssets(); renderIcons(); }; ctl.appendChild(rm); }
      row.appendChild(ctl);
      pane.appendChild(row);
    });
    const extra = Object.keys(state.assets).filter((p) => refs.indexOf(p) < 0);
    if (extra.length) {
      pane.appendChild(h("h3", { class: "sec" }, "attached, not named by the theme"));
      extra.forEach(function (p) {
        const rm = h("button", { class: "ghost danger", type: "button" }, "✕"); rm.onclick = () => { delete state.assets[p]; markDirty(); renderAssets(); };
        pane.appendChild(h("div", { class: "ctl" }, h("span", { class: "mono", style: "flex:1" }, p), h("span", { class: "pill" }, fmtSize(state.assets[p].bytes.length)), rm));
      });
    }
    const np = h("input", { type: "text", placeholder: fileStem() + "/extra.png" });
    const nf = h("input", { type: "file", accept: ".png,.jpg,.jpeg,.webp,.ttf,.otf,.wav" });
    const add = h("button", { class: "ghost", type: "button" }, "attach as…");
    add.onclick = () => { if (np.value && nf.files[0]) attach(np.value, nf.files[0]); };
    pane.appendChild(h("div", { class: "ctl" }, np, nf, add));
    pane.appendChild(h("h3", { class: "sec" }, "take it to the handheld"));
    pane.appendChild(h("div", { class: "muted", style: "font-size:12px" }, "Export (.zip), up top, packs " + fileStem() + ".json with these files at their paths: unzip it into banana/themes/ on the card and pick it in Settings > Display & sound > Theme."));
    if (!RENDER || !RENDER.available || !RENDER.themesDir) {
      pane.appendChild(h("div", { class: "note", html: "<b>Send to handheld</b> writes straight into a mounted card; it needs the local server started with <code>--themes-dir</code>, e.g.<br><br><code>python3 tools/theme-studio/serve.py --themes-dir /run/media/you/BANANA/banana/themes</code>" }));
      return;
    }
    pane.appendChild(h("div", { class: "muted" }, "Send to handheld writes into " + RENDER.themesDir));
    const nm = h("input", { type: "text", value: fileStem() });
    const go = h("button", { class: "primary", type: "button" }, "Send");
    go.onclick = async () => {
      try { const r = await RENDER.send(nm.value, themeOut(), assetList()); setStatus("sent to " + r.path, "ok"); }
      catch (e) { setStatus("send failed: " + e.message, "bad"); }
    };
    pane.appendChild(h("div", { class: "ctl" }, nm, go));
    pane.appendChild(h("div", { class: "muted" }, "then pick it in Settings > Display & sound > Theme (it shows under its name)"));
  }
  function fileStem() {
    return (state.theme.name || "theme").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "theme";
  }

  // ---- the "Add elements" palette --------------------------------------------------------------------
  const PALETTE = [
    ["Shapes", [["rect", "Rect", "a filled box"], ["group-row", "Row", "children in a row"], ["group-column", "Column", "children in a column"]]],
    ["Text & pictures", [["text", "Text", "a line of text"], ["image", "Image", "a picture"], ["group", "Group", "a box holding children"]]],
    ["Lists", [["list-systems", "Systems carousel", "a row centred on the selection"], ["list-games", "Games list", "a column of games"], ["list-hints", "Button hints", "the hint chips"]]],
    ["Status", [["clock", "Clock", "the time"], ["battery", "Battery", "its percent"], ["page-dots", "Page dots", "one per system"]]],
  ];
  function renderPalette() {
    const pane = $("#palette"); pane.innerHTML = "";
    const srow = h("div", { class: "ctl", style: "margin-bottom:6px" });
    srow.appendChild(h("span", { class: "muted", style: "font-size:12px" }, "add to"));
    ["top", "bottom"].forEach(function (s) {
      const b = h("button", { type: "button", class: "ghost" }, s);
      b.setAttribute("aria-pressed", String(state.addScreen === s));
      if (state.addScreen === s) b.style.borderColor = "var(--accent)";
      b.onclick = () => { state.addScreen = s; renderPalette(); };
      srow.appendChild(b);
    });
    srow.appendChild(h("span", { class: "muted", style: "font-size:12px" }, state.view));
    pane.appendChild(srow);
    PALETTE.forEach(function (grp) {
      const g = h("div", { class: "pal-group" }); g.appendChild(h("h3", null, grp[0]));
      const grid = h("div", { class: "pal-grid" });
      grp[1].forEach(function (it) {
        const b = h("button", { type: "button", class: "pal" }, h("b", null, it[1]), h("span", null, it[2]));
        b.title = "add to the " + state.addScreen + " screen of " + state.view;
        b.onclick = () => addPreset(it[0]);
        grid.appendChild(b);
      });
      g.appendChild(grid); pane.appendChild(g);
    });
  }
  function addPreset(kind) {
    const arr = screenArr(state.addScreen);
    arr.push(makePreset(kind));
    state.sel = ["screens", state.view, state.addScreen, arr.length - 1];
    state.open[JSON.stringify(["screens", state.view, state.addScreen])] = true;
    markDirty(); renderAll(); rerender();
  }
  function makePreset(kind) {
    if (kind === "group-row") return { type: "group", x: 0, y: 0, w: 400, h: 60, layout: "row", gap: 8, padding: 8, children: [] };
    if (kind === "group-column") return { type: "group", x: 0, y: 0, w: 300, h: 200, layout: "column", gap: 8, padding: 8, children: [] };
    if (kind === "list-systems") return { type: "list", source: "systems", direction: "row", x: 0, y: 120, w: 640, h: 230, itemW: 150, itemH: 190, gap: 14, scrollMode: "center",
      item: { type: "group", w: 150, h: 190, children: [{ type: "text", x: 8, w: 134, y: 150, align: "center", text: "{item.name}", size: 13, color: "#eef1ff" }] } };
    if (kind === "list-games") return { type: "list", source: "games", direction: "column", x: 24, y: 40, w: 592, h: 400, itemW: 592, itemH: 48, gap: 8,
      item: { type: "group", w: 592, h: 48, children: [{ type: "text", x: 16, y: 14, text: "{item.title}", size: 16, color: "#eef1ff" }] } };
    if (kind === "list-hints") return { type: "list", source: "hints", direction: "row", x: 24, y: 440, w: 600, h: 24, gap: 18, flow: true, scrollMode: "none",
      item: { type: "text", text: "{item.key} {item.label}", size: 13, color: "#9aa3c0" } };
    if (kind === "page-dots") return { type: "list", source: "range", count: 7, direction: "row", x: 280, y: 452, w: 80, h: 12, gap: 6, itemW: 8, itemH: 8, scrollMode: "none",
      item: { type: "rect", w: 8, h: 8, radius: 4, fill: "=i == cursor ? accent : '#5a5560'" } };
    if (kind === "clock") return { type: "text", x: 20, y: 10, text: "{clock}", size: 15, color: "#eef1ff" };
    if (kind === "battery") return { type: "text", x: 20, y: 30, text: "=battery < 0 ? '' : battery + '%'", size: 13, color: "#9aa3c0" };
    return makeElement(kind);
  }

  // ---- the theme as it is saved ----------------------------------------------------------------------
  // The panels make the members they show as they are drawn (an empty "hints" list to add to, an empty
  // "behavior"), but an empty member is not the same as a missing one to the engine: "hints": { "home": [] } takes
  // the default hints away, and a "behavior" with no "cues" drops the default game-start sound. So what is saved,
  // previewed and copied leaves out every such member that is empty and wasn't in the theme when it was opened.
  const WATCHED = ["colors", "fonts", "background", "sounds", "icons", "vars", "components", "input", "input.home", "input.library",
    "hints", "hints.home", "hints.library", "behavior", "behavior.cues", "screens"];
  function snapshotOrig(t) {
    const o = {};
    const has = (path) => { let v = t; for (const k of path.split(".")) { if (!v || typeof v !== "object" || !(k in v)) return false; v = v[k]; } return true; };
    WATCHED.forEach((p) => { if (has(p)) o[p] = true; });
    if (t.screens && typeof t.screens === "object") Object.keys(t.screens).forEach((v) => {
      o["screens." + v] = true;
      const sv = t.screens[v];
      if (sv && typeof sv === "object") Object.keys(sv).forEach((k) => { o["screens." + v + "." + k] = true; });
    });
    return o;
  }
  const isEmpty = (v) => (Array.isArray(v) ? v.length === 0 : v && typeof v === "object" && Object.keys(v).length === 0);
  function themeOut() {
    const t = clone(state.theme), o = state.orig;
    const drop = (obj, key, path) => { if (obj && isEmpty(obj[key]) && !o[path]) delete obj[key]; };
    if (t.screens && typeof t.screens === "object") {
      Object.keys(t.screens).forEach((v) => {
        const sv = t.screens[v];
        if (sv && typeof sv === "object" && !Array.isArray(sv)) Object.keys(sv).forEach((k) => drop(sv, k, "screens." + v + "." + k));
        drop(t.screens, v, "screens." + v);
      });
    }
    // the deepest first, so a parent left empty by its children goes too
    WATCHED.slice().sort((a, b) => b.split(".").length - a.split(".").length).forEach((p) => {
      const ks = p.split("."), last = ks.pop();
      let par = t;
      for (const k of ks) { par = par && par[k]; }
      if (par && typeof par === "object") drop(par, last, p);
    });
    return t;
  }
  function themeText() { return JSON.stringify(themeOut(), null, 2) + "\n"; }

  // ---- undo ------------------------------------------------------------------------------------------
  // Each change is a step (typing in one field is one step until you pause or move to another); the theme's text
  // and the selection are kept. Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) when you aren't typing in a box, or the buttons.
  const HISTORY = 300;
  function snap() { return { text: JSON.stringify(state.theme), sel: state.sel ? state.sel.slice() : null, view: state.view }; }
  function resetHistory() { state.undo = []; state.redo = []; state.cur = snap(); state.curKey = ""; renderUndo(); }
  function record(key) {
    const now = Date.now(), next = snap();
    if (state.cur && next.text === state.cur.text) { state.cur = next; return; }
    const merge = key && key === state.curKey && now - state.curAt < 1200;
    if (!merge && state.cur) { state.undo.push(state.cur); if (state.undo.length > HISTORY) state.undo.shift(); }
    state.cur = next; state.curKey = key || ""; state.curAt = now; state.redo = [];
    renderUndo();
  }
  function restore(s) {
    state.theme = JSON.parse(s.text); state.sel = s.sel; state.view = s.view || state.view;
    if (state.sel && getPath(state.sel) === undefined) state.sel = null;
    normalize(); renderAll(); rerender(); state.cur = s; state.curKey = ""; renderUndo();
    state.unsaved = true; titleNow(); schedulePreview(); scheduleDraft();
  }
  function undo() { if (!state.undo.length) return; state.redo.push(snap()); restore(state.undo.pop()); setStatus("undone", ""); }
  function redo() { if (!state.redo.length) return; state.undo.push(snap()); restore(state.redo.pop()); setStatus("redone", ""); }
  function renderUndo() {
    const u = $("#undoBtn"), r = $("#redoBtn");
    if (u) { u.disabled = !state.undo.length; u.title = "Undo (Ctrl+Z)" + (state.undo.length ? ": " + state.undo.length + " step(s)" : ""); }
    if (r) { r.disabled = !state.redo.length; r.title = "Redo (Ctrl+Shift+Z)"; }
  }
  function markDirty(key) {
    record(key);
    state.unsaved = true; titleNow();
    setStatus("edited", "");
    schedulePreview(); scheduleDraft();
  }
  function titleNow() { document.title = (state.unsaved ? "• " : "") + (state.theme.name || "Untitled") + " — Banana Theme Studio"; }

  // ---- the bottom dock: preview settings, and the JSON tab --------------------------------------------
  const DEFAULT_SCRIPT = "home 0; wait 900; shot home; press right; wait 700; shot home2; press a; wait 900; shot lib; press down; wait 600; shot lib2; press a; wait 400; shot launch";
  function renderPreviewControls() {
    const pane = $("#preview-controls"); pane.innerHTML = "";
    if (!liveOk() && !(RENDER && RENDER.available)) {
      pane.appendChild(h("div", { class: "note" }, ENGINE && ENGINE.state === "loading" ? "Starting the menu's engine…" :
        "No preview: " + (ENGINE ? ENGINE.detail : "engine.js isn't loaded") + ". Editing, the tree, expressions and colours all work without it."));
      return;
    }
    const grid = h("div", { class: "pgrid" });
    const sel = h("select");
    [["640x480", "RG DS  640 × 480"], ["1024x768", "RG DS Plus  1024 × 768"]].forEach(function (o) {
      const op = h("option", { value: o[0] }, o[1]); if (state.size === o[0]) op.setAttribute("selected", ""); sel.appendChild(op);
    });
    sel.onchange = () => { state.size = sel.value; if (liveOk()) restartLive(); else doServerPreview(); };
    grid.appendChild(h("label", null, "panels")); grid.appendChild(sel);
    if (liveOk()) {
      const lib = h("select");
      [["0", "the test library (fixture.sh's)"], ["1", "every system (one game each)"]].forEach(function (o) {
        const op = h("option", { value: o[0] }, o[1]); if (String(state.every ? 1 : 0) === o[0]) op.setAttribute("selected", ""); lib.appendChild(op);
      });
      lib.onchange = () => setLibrary(lib.value === "1");
      grid.appendChild(h("label", { title: "the made-up games the preview shows (ui/tests/fixture.sh's, as serve.py's)" }, "library")); grid.appendChild(lib);
      const o = ENGINE.hostOpts;
      const bat = h("input", { type: "range", min: -1, max: 100, value: o.battery, title: "-1: no battery reading" });
      const batT = h("span", { class: "muted" }, o.battery < 0 ? "none" : o.battery + "%");
      bat.oninput = () => { batT.textContent = +bat.value < 0 ? "none" : bat.value + "%"; ENGINE.setHost({ battery: +bat.value }); kick(); };
      grid.appendChild(h("label", null, "battery")); grid.appendChild(h("div", { class: "ctl" }, bat, batT));
      const flag = (label, key, tip) => {
        const c = h("input", { type: "checkbox" }); c.checked = !!o[key];
        c.onchange = () => { const x = {}; x[key] = c.checked; ENGINE.setHost(x); kick(); };
        return h("label", { class: "chk", title: tip || "" }, c, " " + label);
      };
      grid.appendChild(h("label", null, "settings"));
      grid.appendChild(h("div", { class: "ctl wrap" }, flag("charging", "charging"), flag("Wi-Fi", "online", "the online hook"), flag("12-hour clock", "twelveHour"),
        flag("motion", "motion", "Settings' motion (the motion hook)"), flag("battery %", "batteryText", "batteryMode: text")));
    }
    pane.appendChild(grid);
    const go = h("button", { class: "primary", type: "button", title: "Run banana-ui's headless script and keep the screens it draws (the same pictures serve.py's previews are)" }, "Take stills");
    go.onclick = () => (liveOk() ? doStills() : doServerPreview());
    const scr = h("input", { type: "text", spellcheck: "false", value: state.script || DEFAULT_SCRIPT, title: "banana-ui --headless's script: home N, lib S G, wait MS, press BUTTON, touch SCREEN X Y, shot NAME" });
    scr.onchange = () => { state.script = scr.value; };
    pane.appendChild(h("div", { class: "ctl", style: "margin-top:6px" }, go, h("span", { class: "pill" }, state.shots ? state.shots.length + " still(s)" : "none yet"), scr));
    const b = ENGINE && ENGINE.build;
    pane.appendChild(h("div", { class: "muted", style: "font-size:12px;margin-top:6px" }, liveOk()
      ? "The preview is the menu's own engine (banana-ui's layout.cpp, compiled to WebAssembly" + (b ? ", built " + b.built + " from " + b.commit : "") + "), the same pixels as on the handheld. Its complaints are under Problems."
      : "Drawn by serve.py with the real engine (ui/banana-ui --headless). Its complaints appear under Problems."));
  }
  function renderJson() {
    const pane = $("#json"); pane.innerHTML = "";
    const ta = h("textarea", { spellcheck: "false" }); ta.value = themeText();
    const err = h("div", { class: "jerr" });
    const apply = h("button", { class: "primary", type: "button" }, "Apply");
    apply.onclick = () => {
      let t;
      try { t = looseParse(ta.value); }
      catch (e) {
        const m = /line (\d+) column (\d+)/.exec(e.message) || /position (\d+)/.exec(e.message);
        err.textContent = "Not applied — " + e.message;
        if (m && m.length === 3) { const lines = ta.value.split("\n"); let at = 0; for (let i = 0; i < +m[1] - 1 && i < lines.length; i++) at += lines[i].length + 1; ta.focus(); ta.setSelectionRange(at + +m[2] - 1, at + +m[2]); }
        setStatus("JSON error: " + e.message, "bad"); return;
      }
      if (!t || typeof t !== "object" || Array.isArray(t)) { err.textContent = "Not applied — a theme is one { ... } object."; return; }
      err.textContent = "";
      state.theme = t; state.sel = null; normalize(); markDirty(); renderAll(); rerender(); setStatus("applied the JSON", "ok");
    };
    const revert = h("button", { type: "button" }, "Revert"); revert.onclick = () => { ta.value = themeText(); err.textContent = ""; };
    pane.appendChild(h("div", { class: "row" }, apply, revert, h("span", { class: "muted", style: "font-size:12px" }, "comments and trailing commas are allowed; saving writes plain JSON")));
    pane.appendChild(err);
    pane.appendChild(ta);
  }

  // ---- validation ------------------------------------------------------------------------------------
  function eachElement(cb) {
    const th = state.theme;
    S.VIEWS.forEach((v) => S.SCREENS.forEach((s) => walk(th.screens && th.screens[v] && th.screens[v][s], ["screens", v, s], cb)));
    Object.keys(th.components || {}).forEach((n) => walk(th.components[n], ["components", n], cb));
  }
  function walk(el, path, cb) {
    if (Array.isArray(el)) { el.forEach((e, i) => walk(e, path.concat(i), cb)); return; }
    if (!el || typeof el !== "object") return;
    cb(el, path);
    if (Array.isArray(el.children)) walk(el.children, path.concat("children"), cb);
    if (el.item !== undefined) walk(el.item, path.concat("item"), cb);
  }
  function validate() {
    const errs = [], warns = [];
    eachElement(function (el, path) {
      const T = ["rect", "text", "image", "group", "list"];
      if (el.type !== undefined && T.indexOf(el.type) < 0 && !el.use) errs.push(path.join(".") + ": unknown type \"" + el.type + "\"");
      if (el.use && !(state.theme.components && state.theme.components[el.use])) errs.push(path.join(".") + ": use \"" + el.use + "\" names a component that isn't there");
      Object.keys(el).forEach(function (k) {
        const v = el[k];
        if (typeof v !== "string") return;
        if (v.charAt(0) === "=" || v.indexOf("{") >= 0) {
          const err = E.parseProp(v).error;
          if (err) errs.push(path.join(".") + "." + k + ": " + err);
        }
      });
    });
    Object.keys(state.theme.vars || {}).forEach(function (k) {
      const v = state.theme.vars[k];
      if (typeof v === "string" && (v.charAt(0) === "=" || v.indexOf("{") >= 0)) { const err = E.parseProp(v).error; if (err) errs.push("vars." + k + ": " + err); }
    });
    // the files: named but not attached (the handheld shows its default unless the file is beside the theme), too
    // big, not a picture; icons for ids Banana doesn't know
    const refs = detectAssets();
    refs.forEach(function (p) {
      const a = state.assets[p];
      if (!ENGINE || !ENGINE.safeRel(p)) errs.push(p + ": a theme's files are named relative to its .json (no leading /, no ..)");
      else if (!a) warns.push(p + ": not attached here, so the preview and Export leave it out (the handheld needs it beside the theme)");
      else if (/^(png|jpe?g)$/.test(assetExt(p)) && !a.info) errs.push(p + ": " + a.name + " isn't a PNG or JPEG");
      else if (a.info && (a.info.w > 4096 || a.info.h > 4096)) errs.push(p + ": " + a.info.w + " × " + a.info.h + " is over 4096 px a side (the menu uses its default)");
    });
    const known = iconSystems().filter((x) => !x.unknown).map((x) => x.id);
    Object.keys(state.theme.icons || {}).forEach((id) => { if (known.indexOf(id) < 0) warns.push("icons." + id + ": no system has that id (systems.conf's ids, favorites, recently-played)"); });
    return { errs: errs, warns: warns };
  }
  function doValidate() {
    const r = validate(), eng = liveOk() ? ENGINE.errors() : [];
    const log = $("#log"); log.innerHTML = "";
    const n = r.errs.length + eng.length;
    log.appendChild(h("div", { class: "i" }, "validate: " + (n ? n + " problem(s)" : "no problems") + (r.warns.length ? ", " + r.warns.length + " note(s)" : "")));
    eng.forEach((e) => log.appendChild(h("div", { class: "e" }, "engine: " + e)));
    r.errs.forEach((e) => log.appendChild(h("div", { class: "e" }, e)));
    r.warns.forEach((e) => log.appendChild(h("div", { class: "w" }, e)));
    showTab("bottom", "problems");
  }

  // ---- the live preview: the menu's engine in the page ------------------------------------------------
  // Both screens drawn every frame something moves, as on the handheld. Click the screens and use the keys (or the
  // buttons): arrows, X or Enter for A, Z or Backspace for B, S for X, A for Y, Q and W for L and R. A tap on a screen
  // is a touch. Editing reloads the theme and stays where the preview is.
  const KEYMAP = { ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down", x: "a", Enter: "a", z: "b", Backspace: "b", Escape: "b", s: "x", a: "y", q: "l", w: "r" };
  const live = { top: null, bot: null, imgTop: null, imgBot: null, force: true, pushed: "", errText: null, errAt: 0, raf: 0 };
  const liveOk = () => !!(ENGINE && ENGINE.ready());
  function kick() { live.force = true; }
  function sizeWH() { const m = /^(\d+)x(\d+)$/.exec(state.size) || [0, 640, 480]; return [+m[1], +m[2]]; }
  function restartLive() {
    const wh = sizeWH();
    ENGINE.configure(wh[0], wh[1], state.every);
    live.pushed = ""; pushTheme(false); renderPreview();
  }
  function setLibrary(every) {
    state.every = !!every;
    if (liveOk()) { restartLive(); renderPreviewControls(); }
  }
  function pushTheme(keep) {
    if (!liveOk()) return;
    const text = themeText(), key = text + "\u0000" + Object.keys(state.assets).map((p) => p + ":" + state.assets[p].bytes.length + ":" + state.assets[p].name).join("|");
    if (key === live.pushed) return;
    live.pushed = key;
    ENGINE.setTheme(text, assetFiles(), keep);
    live.force = true;
    showProblems();
  }
  function blit() {
    if (!live.top || !live.top.isConnected) return;
    const W = ENGINE.W, H = ENGINE.H;
    if (!live.imgTop || live.imgTop.width !== W || live.imgTop.height !== H) {
      [live.top, live.bot].forEach((c) => { c.width = W; c.height = H; });
      live.imgTop = live.top.getContext("2d").createImageData(W, H);
      live.imgBot = live.bot.getContext("2d").createImageData(W, H);
    }
    live.top.getContext("2d").putImageData(ENGINE.pixels(0, live.imgTop), 0, 0);
    live.bot.getContext("2d").putImageData(ENGINE.pixels(1, live.imgBot), 0, 0);
    const w = ENGINE.where(), el = $("#where");
    if (w && el) {
      const ids = state.shelf || (state.shelf = ENGINE.shelf());
      el.textContent = w.view + " · system " + (w.system + 1) + " of " + w.systems + (ids[w.system] ? " (" + ids[w.system] + ")" : "") + (w.view !== "home" ? " · game " + (w.game + 1) : "");
    }
  }
  function tick(t) {
    live.raf = requestAnimationFrame(tick);
    if (!liveOk() || document.hidden || !live.top || !live.top.isConnected) return;
    if (ENGINE.frame(t, live.force)) blit();
    live.force = false;
    if (t - live.errAt > 1000) { live.errAt = t; showProblems(); }
  }
  function press(b) { if (!liveOk()) return; ENGINE.press(b); kick(); }
  function renderPreview() {
    const p = $("#preview"); p.innerHTML = "";
    if (liveOk()) {
      state.shelf = ENGINE.shelf();
      const ds = h("div", { class: "ds", tabindex: "0", title: "Click here, then: arrows, X/Enter = A, Z/Backspace = B, S = X, A = Y, Q/W = L/R. Tap the screens." });
      live.top = h("canvas", { class: "scr", "aria-label": "top screen" });
      live.bot = h("canvas", { class: "scr", "aria-label": "bottom screen" });
      live.imgTop = null;
      ds.appendChild(live.top); ds.appendChild(live.bot);
      ds.addEventListener("keydown", (e) => {
        if (e.ctrlKey || e.metaKey || e.altKey) return;
        const b = KEYMAP[e.key] || KEYMAP[e.key.toLowerCase && e.key.toLowerCase()];
        if (!b) return;
        e.preventDefault(); press(b);
      });
      [live.top, live.bot].forEach(function (cv, screen) {
        const at = (e) => { const r = cv.getBoundingClientRect(); return [(e.clientX - r.left) / r.width * ENGINE.W, (e.clientY - r.top) / r.height * ENGINE.H]; };
        cv.addEventListener("pointerdown", (e) => { ds.focus(); const q = at(e); ENGINE.touch(screen, q[0], q[1], true); kick(); });
        cv.addEventListener("pointerup", (e) => { const q = at(e); ENGINE.touch(screen, q[0], q[1], false); kick(); });
      });
      const btn = (label, b, cls) => { const x = h("button", { type: "button", class: "pk " + (cls || ""), title: "the " + label + " button" }, label); x.onclick = () => { press(b); ds.focus(); }; return x; };
      const pad = h("div", { class: "pad" },
        h("div", { class: "dpad" }, btn("▲", "up", "pu"), btn("◀", "left", "pl"), btn("▶", "right", "pr"), btn("▼", "down", "pd")),
        h("div", { class: "abxy" }, btn("X", "x", "pu"), btn("Y", "y", "pl"), btn("A", "a", "pr"), btn("B", "b", "pd")),
        h("div", { class: "lr" }, btn("L", "l"), btn("R", "r")));
      const jump = h("div", { class: "ctl wrap" });
      const go = (label, fn, tip) => { const b = h("button", { type: "button", class: "ghost", title: tip }, label); b.onclick = () => { fn(); kick(); ds.focus(); }; return b; };
      jump.appendChild(go("Home", () => ENGINE.home(0), "the home view, on the first system"));
      jump.appendChild(go("Library", () => { const w = ENGINE.where(); ENGINE.library(w && w.view === "home" ? w.system : 0, 0); }, "the selected system's games"));
      jump.appendChild(go("Reload", () => { live.pushed = ""; pushTheme(false); }, "load the theme again from the start"));
      p.appendChild(h("div", { class: "live" }, ds, h("div", { class: "side" }, pad, jump, h("div", { id: "where", class: "muted mono" }))));
      live.force = true;
      if (!live.raf) live.raf = requestAnimationFrame(tick);
    } else if (ENGINE && ENGINE.state === "loading") {
      p.appendChild(h("div", { class: "note" }, "Starting the menu's engine…"));
    } else if (!RENDER || !RENDER.available) {
      p.appendChild(h("div", { class: "note", html: "No preview here: " + esc(ENGINE ? ENGINE.detail : "engine.js isn't loaded") +
        ".<br><br>The preview is the menu's own engine, compiled to WebAssembly in <code>wasm/banana-engine.js</code> (build it with <code>sh tools/theme-studio/wasm/build.sh</code>), or drawn by <code>python3 tools/theme-studio/serve.py</code>. Everything else — the tree, the inspector, expressions, colours, Export — works without it." }));
    } else if (!state.shots) p.appendChild(h("div", { class: "note" }, "Press Take stills (bottom) to draw the theme with the real engine."));
    if (state.shots && state.shots.length) {
      const shots = h("div", { class: "shots" });
      state.shots.forEach(function (s) {
        const card = h("div", { class: "shot" });
        card.appendChild(h("div", { class: "cap" }, s.name + "  (top / bottom)"));
        const pair = h("div", { class: "pair" });
        if (s.top) pair.appendChild(h("img", { src: s.top, alt: s.name + " top" }));
        if (s.bot) pair.appendChild(h("img", { src: s.bot, alt: s.name + " bottom" }));
        card.appendChild(pair); shots.appendChild(card);
      });
      if (liveOk()) { const clr = h("button", { class: "ghost", type: "button" }, "clear the stills"); clr.onclick = () => { state.shots = null; renderPreview(); renderPreviewControls(); }; p.appendChild(clr); }
      p.appendChild(shots);
    }
  }
  function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]); }
  function doStills() {
    try {
      pushTheme(false);
      state.shots = ENGINE.stills(state.script || DEFAULT_SCRIPT);
      setStatus(state.shots.length + " still(s) drawn", "ok");
    } catch (e) { setStatus("stills: " + e.message, "bad"); }
    live.pushed = ""; pushTheme(false);			// (the script moved the menu: the live preview starts again)
    renderPreview(); renderPreviewControls();
  }
  // the theme's problems as the engine reports them (Problems, with a count on its tab)
  function showProblems() {
    if (!liveOk()) return;
    const errs = ENGINE.errors(), text = errs.join("\n");
    if (text === live.errText) return;
    live.errText = text;
    showLog("", errs);
  }

  // ---- previews from serve.py, when the page has no engine of its own ----------------------------------
  let previewTimer = null;
  function schedulePreview() {
    if (previewTimer) clearTimeout(previewTimer);
    previewTimer = setTimeout(function () {
      if (liveOk()) pushTheme(true);
      else if (RENDER && RENDER.available) doServerPreview();
    }, liveOk() ? 120 : 700);
  }
  async function doServerPreview() {
    if (!RENDER || !RENDER.available) return;
    try {
      const r = await RENDER.render(themeOut(), state.script || null, state.size, assetList());
      state.shots = r.shots || [];
      showLog(r.log, r.errors || []);
      renderPreview(); renderPreviewControls();
    } catch (e) { showLog(String(e.message || e), [String(e.message || e)]); }
  }
  function showLog(text, errors) {
    const log = $("#log"); log.innerHTML = "";
    if (errors && errors.length) { log.appendChild(h("div", { class: "e" }, errors.length + " theme problem(s), as the engine reports them:")); errors.forEach((e) => log.appendChild(h("div", { class: "e" }, e))); }
    else log.appendChild(h("div", { class: "i" }, "the engine doesn't complain about the theme"));
    if (text) log.appendChild(h("div", { class: "i" }, text.slice(0, 4000)));
    const tab = document.querySelector('.tabs[data-group="bottom"] button[data-tab="problems"]');
    if (tab) { tab.textContent = errors && errors.length ? "Problems (" + errors.length + ")" : "Problems"; tab.classList.toggle("bad", !!(errors && errors.length)); }
  }

  // ---- open / save / export --------------------------------------------------------------------------
  function loadTheme(t, assets, how) {
    Object.keys(state.assets).forEach((p) => { if (state.assets[p].url) URL.revokeObjectURL(state.assets[p].url); });
    state.theme = t || clone(STARTER);
    state.sel = null; state.shots = null; state.assets = {};
    Object.keys(assets || {}).forEach((p) => putAsset(p, assets[p].name || p.split("/").pop(), assets[p].bytes));
    state.orig = snapshotOrig(state.theme);
    normalize();
    state.unsaved = false; resetHistory(); titleNow();
    renderAll(); rerender();
    live.pushed = ""; if (liveOk()) pushTheme(false); else schedulePreview();
    scheduleDraft();
    if (how) setStatus(how, "ok");
  }
  function guardUnsaved(what) { return !state.unsaved || confirm(what + "? The changes you haven't saved or exported are lost (the last one is kept in this browser until you start another)."); }
  function download(name, data, type) {
    const blob = new Blob([data], { type: type });
    const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = name;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 30000);
  }
  function saveTheme() {
    download(fileStem() + ".json", themeText(), "application/json");
    state.unsaved = false; titleNow();
    const n = Object.keys(state.assets).length;
    setStatus("saved " + fileStem() + ".json" + (n ? " — it names " + n + " attached file(s): Export (.zip) takes them along" : ""), "ok");
  }
  // the theme and its files, as they go into banana/themes/ on the card: <name>.json and the files at their paths
  function zipBytes() {
    const files = [{ path: fileStem() + ".json", data: themeText() }];
    Object.keys(state.assets).sort().forEach((p) => files.push({ path: p, data: state.assets[p].bytes }));
    return ZIP.write(files);
  }
  function exportZip() {
    const missing = detectAssets().filter((p) => !state.assets[p]);
    download(fileStem() + ".zip", zipBytes(), "application/zip");
    state.unsaved = false; titleNow();
    setStatus("exported " + fileStem() + ".zip (" + (Object.keys(state.assets).length + 1) + " files)" + (missing.length ? " — not in it, not attached: " + missing.join(", ") : "") + ". Unzip it into banana/themes/ on the card.", missing.length ? "" : "ok");
  }
  // files from Open, Open folder or a drop: a theme's .json (or a .zip with one) and the files beside it.
  // A theme is picked (the one with "screens", the shallowest), its folder's files attached at their paths
  // relative to it; pictures dropped alone are attached where the theme names them (by file name)
  async function openFiles(list) {
    const entries = [];
    for (const f of Array.from(list || [])) {
      const bytes = new Uint8Array(await f.arrayBuffer());
      if (/\.zip$/i.test(f.name)) {
        try { (await ZIP.read(bytes)).forEach((e) => entries.push({ path: e.path, name: e.path.split("/").pop(), bytes: e.data })); }
        catch (e) { alert(f.name + " couldn't be opened: " + e.message); return; }
      } else entries.push({ path: f.webkitRelativePath || f.name, name: f.name, bytes: bytes, loose: !f.webkitRelativePath });
    }
    const dec = new TextDecoder();
    const themes = [];
    entries.filter((e) => /\.json$/i.test(e.path) && !/(^|\/)__MACOSX\//.test(e.path)).forEach((e) => {
      try { const t = looseParse(dec.decode(e.bytes)); if (t && typeof t === "object" && !Array.isArray(t)) themes.push({ e: e, t: t }); }
      catch (err) { themes.push({ e: e, err: err }); }
    });
    const good = themes.filter((x) => x.t);
    if (!themes.length) {
      // files alone: attach them where the current theme names them
      const refs = detectAssets(), done = [], left = [];
      entries.forEach((e) => { const p = refs.find((r) => r.split("/").pop() === e.name); if (p) { putAsset(p, e.name, e.bytes); done.push(p); } else left.push(e.name); });
      if (done.length) { markDirty(); renderAssets(); renderIcons(); }
      setStatus((done.length ? "attached " + done.join(", ") : "") + (left.length ? (done.length ? "; " : "") + "the theme doesn't name " + left.join(", ") + " (Assets > attach as…)" : ""), done.length ? "ok" : "bad");
      return;
    }
    if (!good.length) {
      const x = themes[0];
      alert("This file is not a theme Banana can read, so it was not opened.\n\n" + x.e.path + ": " + x.err.message +
        "\n\nThemes are JSON. The usual slips: a missing comma between two items, or a word that needs \"quotes\".");
      return;
    }
    const depth = (p) => p.split("/").length;
    good.sort((a, b) => (!!b.t.screens - !!a.t.screens) || depth(a.e.path) - depth(b.e.path));
    let pick = good[0];
    const top = good.filter((x) => !!x.t.screens === !!pick.t.screens && depth(x.e.path) === depth(pick.e.path));
    if (top.length > 1) {
      const n = prompt("There are " + top.length + " themes here. Which one?\n\n" + top.map((x, i) => (i + 1) + ". " + x.e.path + (x.t.name ? "  (" + x.t.name + ")" : "")).join("\n"), "1");
      if (n === null) return;
      pick = top[Math.max(0, Math.min(top.length - 1, (parseInt(n, 10) || 1) - 1))];
    }
    if (!guardUnsaved("Open " + pick.e.path)) return;
    const base = pick.e.path.indexOf("/") >= 0 ? pick.e.path.slice(0, pick.e.path.lastIndexOf("/") + 1) : "";
    // the files of the theme's folder (from Open folder or a .zip) at their paths relative to it; files picked
    // alone go where the theme names them, by their name (below)
    const assets = {};
    entries.forEach((e) => {
      if (e === pick.e || e.loose || !e.path.startsWith(base) || /\.json$/i.test(e.path)) return;
      const rel = e.path.slice(base.length);
      if (ASSET_EXTS.indexOf(assetExt(rel)) >= 0) assets[rel] = { name: e.name, bytes: e.bytes };
    });
    loadTheme(pick.t, assets);
    // anything the theme names that came along under another folder: by its file name
    const refs = detectAssets(), loose = [];
    refs.forEach((p) => { if (state.assets[p]) return; const e = entries.find((x) => x.name === p.split("/").pop() && x !== pick.e); if (e) { putAsset(p, e.name, e.bytes); loose.push(p); } });
    if (loose.length) { renderAssets(); renderIcons(); live.pushed = ""; pushTheme(false); }
    const missing = refs.filter((p) => !state.assets[p]);
    setStatus("opened " + pick.e.path + (Object.keys(state.assets).length ? " with " + Object.keys(state.assets).length + " file(s)" : "") +
      (missing.length ? " — not found beside it: " + missing.join(", ") + " (open the theme's folder or .zip to bring them)" : ""), missing.length ? "" : "ok");
  }

  // ---- the guides (docs/MAKE-A-THEME.md, docs/THEMES.md, the colour keys), inlined by make-templates.py ----
  function slug(t) { return t.toLowerCase().replace(/<[^>]+>/g, "").replace(/[^\w\- ]+/g, "").trim().replace(/ /g, "-"); }
  function inline(t) {
    return esc(t).replace(/`([^`]+)`/g, "<code>$1</code>").replace(/\*\*([^*]+)\*\*/g, "<b>$1</b>")
      .replace(/(^|[^*\w])\*([^*\s][^*]*)\*/g, "$1<i>$2</i>")
      .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (m, txt, href) => '<a href="' + href.replace(/"/g, "%22") + '" data-md="1">' + txt + "</a>");
  }
  // a small Markdown renderer: headings, paragraphs, lists, fenced code, tables, quotes, inline code/bold/links
  function markdown(src) {
    const L = src.replace(/\r/g, "").split("\n"), out = [];
    let i = 0;
    while (i < L.length) {
      const l = L[i];
      if (/^```/.test(l)) { const b = []; i++; while (i < L.length && !/^```/.test(L[i])) b.push(L[i++]); i++; out.push("<pre><code>" + esc(b.join("\n")) + "</code></pre>"); continue; }
      const hm = /^(#{1,4})\s+(.*)$/.exec(l);
      if (hm) { out.push("<h" + (hm[1].length + 1) + ' id="md-' + slug(hm[2]) + '">' + inline(hm[2]) + "</h" + (hm[1].length + 1) + ">"); i++; continue; }
      if (/^\|/.test(l)) {
        const rows = []; while (i < L.length && /^\|/.test(L[i])) rows.push(L[i++]);
        const cells = (r) => r.replace(/^\||\|$/g, "").split("|").map((c) => c.trim());
        let html = "<table>";
        rows.forEach((r, k) => { if (k === 1 && /^[\s|:-]+$/.test(r)) return; const tag = k === 0 && rows[1] && /^[\s|:-]+$/.test(rows[1]) ? "th" : "td"; const c = cells(r); if (tag === "th" && c.every((x) => !x)) return; html += "<tr>" + c.map((x) => "<" + tag + ">" + inline(x) + "</" + tag + ">").join("") + "</tr>"; });
        out.push(html + "</table>"); continue;
      }
      if (/^\s*([-*]|\d+\.)\s+/.test(l)) {
        const ordered = /^\s*\d+\./.test(l), items = [];
        while (i < L.length && (/^\s*([-*]|\d+\.)\s+/.test(L[i]) || (/^\s{2,}\S/.test(L[i]) && items.length))) {
          if (/^\s*([-*]|\d+\.)\s+/.test(L[i])) items.push(L[i].replace(/^\s*([-*]|\d+\.)\s+/, "")); else items[items.length - 1] += " " + L[i].trim();
          i++;
        }
        out.push((ordered ? "<ol>" : "<ul>") + items.map((x) => "<li>" + inline(x) + "</li>").join("") + (ordered ? "</ol>" : "</ul>"));
        continue;
      }
      if (/^>\s?/.test(l)) { const b = []; while (i < L.length && /^>\s?/.test(L[i])) b.push(L[i++].replace(/^>\s?/, "")); out.push("<blockquote>" + inline(b.join(" ")) + "</blockquote>"); continue; }
      if (!l.trim()) { i++; continue; }
      const b = []; while (i < L.length && L[i].trim() && !/^(```|#{1,4}\s|\||>|\s*([-*]|\d+\.)\s)/.test(L[i])) b.push(L[i++]);
      out.push("<p>" + inline(b.join(" ")) + "</p>");
    }
    return out.join("\n");
  }
  function openGuide(which, anchor) {
    const guides = (typeof BananaGuides !== "undefined" && BananaGuides) || [];
    if (!guides.length) { window.open("../../docs/MAKE-A-THEME.md", "_blank", "noopener"); return; }
    let box = $("#guide");
    if (!box) {
      box = h("div", { id: "guide", class: "modal", role: "dialog", "aria-label": "Guide" });
      box.addEventListener("click", (e) => { if (e.target === box) box.remove(); });
      document.body.appendChild(box);
    }
    box.innerHTML = "";
    const g = guides.find((x) => x.id === which) || guides[0];
    const card = h("div", { class: "card" });
    const bar = h("div", { class: "tabs" });
    guides.forEach((x) => { const b = h("button", { type: "button" }, x.title); b.setAttribute("aria-selected", String(x === g)); b.onclick = () => openGuide(x.id); bar.appendChild(b); });
    const close = h("button", { type: "button", class: "ghost", title: "Close (Esc)" }, "✕"); close.onclick = () => box.remove();
    bar.appendChild(h("span", { style: "flex:1" })); bar.appendChild(close);
    card.appendChild(bar);
    const body = h("div", { class: "md" });
    if (/\.md$/.test(g.file)) body.innerHTML = markdown(g.text); else body.appendChild(h("pre", null, g.text));
    body.appendChild(h("p", { class: "muted", style: "font-size:12px" }, "(" + g.file + ")"));
    body.addEventListener("click", function (e) {
      const a = e.target.closest && e.target.closest("a[data-md]"); if (!a) return;
      const href = a.getAttribute("href");
      e.preventDefault();
      if (/^https?:/.test(href)) { window.open(href, "_blank", "noopener"); return; }
      if (href.charAt(0) === "#") { const t = body.querySelector("#md-" + CSS.escape(href.slice(1))); if (t) t.scrollIntoView(); return; }
      const name = href.split("#")[0].split("/").pop(), to = guides.find((x) => x.file.split("/").pop() === name);
      if (to) openGuide(to.id, href.split("#")[1]);
      else setStatus(href + " is in the BananaLinux repository, not in this page", "");
    });
    card.appendChild(body);
    box.appendChild(card);
    if (anchor) { const t = body.querySelector("#md-" + CSS.escape(anchor)); if (t) t.scrollIntoView(); } else body.scrollTop = 0;
  }

  // ---- the last theme, kept in this browser (IndexedDB), in case the tab is closed ----------------------
  const DRAFT = {
    db() {
      if (this._db) return this._db;
      this._db = new Promise(function (res, rej) {
        try {
          const r = indexedDB.open("banana-theme-studio", 1);
          r.onupgradeneeded = () => r.result.createObjectStore("draft");
          r.onsuccess = () => res(r.result); r.onerror = () => rej(r.error);
        } catch (e) { rej(e); }
      });
      return this._db;
    },
    async put(v) { const db = await this.db(); return new Promise((res, rej) => { const tx = db.transaction("draft", "readwrite"); tx.objectStore("draft").put(v, "last"); tx.oncomplete = res; tx.onerror = () => rej(tx.error); }); },
    async get() { const db = await this.db(); return new Promise((res, rej) => { const q = db.transaction("draft").objectStore("draft").get("last"); q.onsuccess = () => res(q.result || null); q.onerror = () => rej(q.error); }); },
  };
  let draftTimer = null, draftReady = false;
  function scheduleDraft() {
    if (!draftReady) return;
    if (draftTimer) clearTimeout(draftTimer);
    draftTimer = setTimeout(function () {
      const assets = Object.keys(state.assets).map((p) => ({ path: p, name: state.assets[p].name, bytes: state.assets[p].bytes }));
      DRAFT.put({ text: JSON.stringify(state.theme), orig: state.orig, assets: assets, when: Date.now(), name: state.theme.name || "", unsaved: state.unsaved }).catch(() => {});
    }, 800);
  }
  async function offerDraft() {
    let d = null;
    try { d = await DRAFT.get(); } catch (e) { /* no IndexedDB here (a private window): nothing kept */ }
    draftReady = true;
    if (!d || !d.text || d.text === JSON.stringify(STARTER) || state.unsaved) return;
    const bar = $("#banner"); if (!bar) return;
    bar.innerHTML = ""; bar.hidden = false;
    const when = new Date(d.when);
    bar.appendChild(h("span", null, "Your last theme here, " + (d.name || "untitled") + " (" + when.toLocaleDateString() + " " + when.toLocaleTimeString().slice(0, 5) + (d.unsaved ? ", not saved" : "") + ")" + (d.assets && d.assets.length ? " with " + d.assets.length + " file(s)" : "") + ", is kept in this browser."));
    const yes = h("button", { type: "button", class: "primary" }, "Open it"), no = h("button", { type: "button", class: "ghost" }, "No thanks");
    yes.onclick = () => {
      bar.hidden = true;
      const assets = {}; (d.assets || []).forEach((a) => { assets[a.path] = { name: a.name, bytes: a.bytes }; });
      loadTheme(JSON.parse(d.text), assets, "opened your last theme");
      if (d.orig) state.orig = d.orig;
      state.unsaved = !!d.unsaved; titleNow();
    };
    no.onclick = () => { bar.hidden = true; };
    bar.appendChild(yes); bar.appendChild(no);
  }

  // ---- tabs, status, wiring --------------------------------------------------------------------------
  // the desktop studio's dock tabs: each group shows one of its panels
  const TAB_PANES = {
    library: { components: "#pane-components", assets: "#pane-assets" },
    center: { design: "#design", json: "#json" },
    properties: { inspector: "#inspector", theme: "#theme" },
    bottom: { controls: "#preview-controls", problems: "#log" },
  };
  function showTab(group, tab) {
    const bar = document.querySelector('.tabs[data-group="' + group + '"]');
    if (bar && bar.querySelectorAll) bar.querySelectorAll("button").forEach((b) => b.setAttribute("aria-selected", String(b.dataset.tab === tab)));
    const map = TAB_PANES[group] || {};
    Object.keys(map).forEach((k) => { const el = $(map[k]); if (el) el.hidden = (k !== tab); });
    if (group === "center" && tab === "json") renderJson();
    if (group === "center" && tab === "design") kick();
  }
  function setStatus(txt, cls) { const s = $("#status"); s.textContent = txt; s.title = txt; s.className = "status" + (cls ? " " + cls : ""); }
  function rerender() { renderInspector(); }
  function renderAll() { renderPalette(); renderTree(); renderColours(); renderIcons(); renderVars(); renderComponents(); renderAssets(); renderInput(); renderBehaviour(); renderTheme(); renderPreviewControls(); if (!$("#json").hidden) renderJson(); }

  // The example themes: the copies inlined in templates.js (the page is usually opened with no server), or
  // serve.py's list when it is running.
  function loadTemplates() {
    const bundled = (typeof BananaTemplates !== "undefined" && BananaTemplates) || [];
    if (bundled.length) { state.templates = bundled; renderTheme(); }
    if (!RENDER || !RENDER.available) return;
    fetch("api/templates", { cache: "no-store" }).then((r) => (r.ok ? r.json() : null)).then((list) => {
      if (Array.isArray(list) && list.length) { state.templates = list; renderTheme(); }
    }).catch(() => {});
  }
  function loadLocalThemes() {
    if (!RENDER || !RENDER.available) return;
    RENDER.localThemes_().then((list) => { state.localThemes = list || []; renderTheme(); }).catch(() => {});
  }
  // the system ids: the editor's built-in list, plus whatever the checkout's systems.conf has
  function systemIds() { return (state.systemIds && state.systemIds.length) ? state.systemIds : S.SYSTEM_IDS; }
  function loadSystemIds() {
    if (!RENDER || !RENDER.available) return;
    fetch("api/systems", { cache: "no-store" }).then((r) => (r.ok ? r.json() : [])).then((ids) => {
      if (Array.isArray(ids) && ids.length) {
        const all = S.SYSTEM_IDS.slice();
        ids.forEach((i) => { if (all.indexOf(i) < 0) all.push(i); });
        state.systemIds = all; renderIcons();
      }
    }).catch(() => {});
  }

  const typing = (el) => el && (el.tagName === "INPUT" || el.tagName === "TEXTAREA" || el.tagName === "SELECT" || el.isContentEditable);
  function wire() {
    document.querySelectorAll(".tabs button").forEach((b) => { b.onclick = () => showTab(b.parentElement.dataset.group, b.dataset.tab); });
    // the dark/light switch, like the website's
    const root = document.documentElement;
    const setTheme = (t) => { root.setAttribute("data-theme", t); const lab = $("#themeLabel"); if (lab) lab.textContent = t === "light" ? "Light" : "Dark"; try { localStorage.setItem("banana-studio-theme", t); } catch (e) { /* ignore */ } };
    try { const q = new URLSearchParams(location.search).get("theme"); const saved = q || localStorage.getItem("banana-studio-theme"); if (saved) setTheme(saved); } catch (e) { /* ignore */ }
    const sw = $("#themeSwitch"); if (sw) sw.onclick = () => setTheme(root.getAttribute("data-theme") === "light" ? "dark" : "light");
    $("#newBtn").onclick = () => { if (guardUnsaved("Start a new theme")) loadTheme(clone(STARTER), null, "a new theme"); };
    $("#openBtn").onclick = () => $("#fileInput").click();
    $("#openDirBtn").onclick = () => $("#dirInput").click();
    $("#fileInput").onchange = function () { const f = Array.from(this.files); this.value = ""; openFiles(f); };
    $("#dirInput").onchange = function () { const f = Array.from(this.files); this.value = ""; openFiles(f); };
    $("#saveBtn").onclick = saveTheme;
    $("#exportBtn").onclick = exportZip;
    $("#undoBtn").onclick = undo;
    $("#redoBtn").onclick = redo;
    $("#copyBtn").onclick = () => { const t = themeText(); (navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(() => setStatus("copied", "ok"), () => { const ta = h("textarea"); ta.value = t; document.body.appendChild(ta); ta.select(); try { document.execCommand("copy"); } catch (e) {} ta.remove(); setStatus("copied", "ok"); }); };
    $("#validateBtn").onclick = doValidate;
    $("#templatesBtn").onclick = () => { showTab("properties", "theme"); const t = $("#pane-theme .templates"); if (t) t.scrollIntoView(); setStatus("Pick a starting point under “Start from a template”.", ""); };
    $("#guideBtn").onclick = () => openGuide("make");
    window.addEventListener("keydown", (e) => {
      const mod = e.ctrlKey || e.metaKey;
      if (e.key === "Escape" && $("#guide")) { $("#guide").remove(); return; }
      if (!mod) return;
      const k = e.key.toLowerCase();
      if (k === "s") { e.preventDefault(); if (e.shiftKey) exportZip(); else saveTheme(); }
      else if (k === "o") { e.preventDefault(); $("#fileInput").click(); }
      else if ((k === "z" || k === "y") && !typing(document.activeElement)) {
        e.preventDefault();
        if (k === "y" || e.shiftKey) redo(); else undo();
      }
    });
    window.addEventListener("beforeunload", (e) => { if (state.unsaved) { e.preventDefault(); e.returnValue = ""; } });
    // files dropped anywhere on the page: a theme (with its folder's files) or pictures for the current one
    window.addEventListener("dragover", (e) => { if (e.dataTransfer && Array.from(e.dataTransfer.types || []).indexOf("Files") >= 0) { e.preventDefault(); document.body.classList.add("dropping"); } });
    window.addEventListener("dragleave", (e) => { if (!e.relatedTarget) document.body.classList.remove("dropping"); });
    window.addEventListener("drop", (e) => {
      document.body.classList.remove("dropping");
      if (!e.dataTransfer || !e.dataTransfer.files || !e.dataTransfer.files.length) return;
      e.preventDefault(); openFiles(e.dataTransfer.files);
    });
  }

  // ---- go --------------------------------------------------------------------------------------------
  wire(); normalize(); state.orig = snapshotOrig(state.theme); resetHistory(); titleNow();
  loadTemplates(); renderAll(); renderInspector(); renderPreview();
  try {						// ?tab=theme / ?tab=json opens on that dock tab
    const t = new URLSearchParams(location.search).get("tab");
    if (t === "theme") showTab("properties", "theme");
    else if (t === "json") showTab("center", "json");
  } catch (e) { /* ignore */ }
  setStatus(location.protocol === "file:" ? "opened from a file: everything works here, nothing is uploaded" : "ready", "");
  offerDraft();
  if (ENGINE) {
    renderPreview(); renderPreviewControls();
    ENGINE.load("wasm/banana-engine.js").then(function (ok) {
      if (ok) {
        const wh = sizeWH(); ENGINE.configure(wh[0], wh[1], state.every);
        live.pushed = ""; pushTheme(false);
        setStatus("the live preview is the menu's own engine", "ok");
      }
      renderPreview(); renderPreviewControls(); renderIcons();
    });
  }
  if (RENDER && location.protocol !== "file:") {
    RENDER.check().then(function (ok) {
      if (ok) { loadTemplates(); loadLocalThemes(); loadSystemIds(); renderAssets(); renderTheme(); }
      renderPreviewControls();
      if (!liveOk()) renderPreview();
      try { if (ok && !liveOk() && new URLSearchParams(location.search).get("preview")) doServerPreview(); } catch (e) { /* ignore */ }
    });
  }

  // for tests (tests/browser.test.py) and the console
  G.BananaStudio = { state: state, themeOut: themeOut, themeText: themeText, loadTheme: loadTheme, openFiles: openFiles, zipBytes: zipBytes,
    undo: undo, redo: redo, validate: validate, pushTheme: pushTheme, markDirty: markDirty, renderAll: renderAll, openGuide: openGuide, setIcon: setIcon, resetIcon: resetIcon,
    stills: doStills, setLibrary: setLibrary, press: press, live: live };
})();
