// Banana Theme Studio: the editor.
//
// A layout theme is one JSON file (docs/THEMES.md). This edits that JSON directly — the object you load is the
// object you save, so key order and any members the editor does not know are kept. The editor is offline-first:
// the tree, inspector, live expression values and colour keys all work with no server; server.py adds exact
// previews drawn by the real engine (render.js).
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
    shots: null, drag: null, open: {}, templates: null, localThemes: [], systemIds: null, assets: {} };
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
      if (c === "/" && n === "*") { i += 2; while (i < text.length && !(text[i] === "*" && text[i + 1] === "/")) i++; i += 2; continue; }
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
    function commit(nv) { set(nv); markDirty(); chipFor(); }
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
      if (!arr.length) pane.appendChild(h("div", { class: "muted", style: "margin:2px 0 6px" }, "(empty — add an element; a screen with no elements is black)"));
      arr.forEach((el, i) => pane.appendChild(node(["screens", state.view, scr, i], el, i, arr)));
    });
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
      acts.appendChild(mk("↑", () => { if (index > 0) { const x = arr.splice(index, 1)[0]; arr.splice(index - 1, 0, x); state.sel = parentOf(path).concat(index - 1); renderAll(); rerender(); } }));
      acts.appendChild(mk("↓", () => { if (index < arr.length - 1) { const x = arr.splice(index, 1)[0]; arr.splice(index + 1, 0, x); state.sel = parentOf(path).concat(index + 1); renderAll(); rerender(); } }));
      acts.appendChild(mk("+", () => addTo(arr, parentOf(path), index)));
      acts.appendChild(mk("⧉", () => { arr.splice(index + 1, 0, clone(el)); state.sel = parentOf(path).concat(index + 1); renderAll(); rerender(); }));
    }
    acts.appendChild(mk("✕", () => { delPath(path); state.sel = null; renderAll(); rerender(); }, true));
    row.appendChild(acts);
    row.onclick = () => { state.sel = path; renderTree(); rerender(); };
    row.setAttribute("draggable", "true");
    row.addEventListener("dragstart", () => { state.drag = { path: path, par: parentOf(path) }; });
    row.addEventListener("dragover", (e) => { if (state.drag && samePath(state.drag.par, parentOf(path))) e.preventDefault(); });
    row.addEventListener("drop", (e) => {
      e.preventDefault();
      if (!Array.isArray(arr) || !state.drag || !samePath(state.drag.par, parentOf(path))) return;
      const from = +state.drag.path.slice(-1), to = index;
      if (from === to) return;
      const x = arr.splice(from, 1)[0]; arr.splice(to, 0, x); state.drag = null; state.sel = parentOf(path).concat(to); renderAll(); rerender();
    });
    wrap.appendChild(row);
    if (kids && state.open[openKey] !== false) {
      if (Array.isArray(el.children)) el.children.forEach((c, i) => wrap.appendChild(node(path.concat("children", i), c, i, el.children)));
      else if (el.item !== undefined) {
        const child = h("div"); child.appendChild(node(path.concat("item"), el.item, 0, null));
        const rm = h("button", { class: "ghost danger", type: "button" }, "remove item"); rm.onclick = () => { delete el.item; renderAll(); rerender(); };
        child.appendChild(rm); wrap.appendChild(child);
      }
      if (el.type === "group" || el.children !== undefined) {
        const addc = h("button", { class: "ghost", type: "button" }, "+ child"); addc.onclick = () => { if (!Array.isArray(el.children)) el.children = []; addTo(el.children, path.concat("children")); };
        wrap.appendChild(addc);
      }
    }
    if (!kids && el.type === "group" && !Array.isArray(el.children)) {
      const addc = h("button", { class: "ghost", type: "button" }, "+ child"); addc.onclick = () => { el.children = []; renderAll(); rerender(); };
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
    renderAll(); rerender();
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
    const dup = h("button", { class: "ghost", type: "button" }, "delete"); dup.onclick = () => { delPath(state.sel); state.sel = null; renderAll(); rerender(); };
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

    // icons
    // the system icons: a picture per system id, which the engine uses everywhere for that system
    pane.appendChild(h("h3", { class: "sec" }, "system icons"));
    pane.appendChild(h("div", { class: "muted", style: "font-size:12px" }, "a picture per system id replaces the menu's icon for that system (home, lists, Settings). 128×128 is plenty."));
    const icons = th.icons || (th.icons = {});
    const ids = systemIds();
    Object.keys(icons).forEach(function (k) {
      const f = h("div"); pane.appendChild(f);
      const sel = h("select");
      (ids.indexOf(k) < 0 ? [k].concat(ids) : ids).forEach(function (o) {
        const op = h("option", { value: o }, o); if (o === k) op.setAttribute("selected", ""); sel.appendChild(op);
      });
      sel.onchange = () => {
        const v = sel.value; if (v === k) return;
        const path = icons[k]; delete icons[k]; icons[v] = path; markDirty(); renderColours();
      };
      field(f, k, () => icons[k], (v) => { if (v) icons[k] = v; else delete icons[k]; markDirty(); }, "text", null, sel);
    });
    const addSel = h("select");
    ids.forEach((o) => addSel.appendChild(h("option", { value: o }, o)));
    addSel.appendChild(h("option", { value: "__other" }, "other…"));
    const ikb = h("button", { class: "ghost", type: "button" }, "add icon");
    ikb.onclick = () => {
      let id = addSel.value;
      if (id === "__other") id = (window.prompt("System id (as in systems.conf)", "") || "").trim();
      if (id && id !== "__other") { icons[id] = fileStem() + "/icons/" + id + ".png"; markDirty(); renderColours(); }
    };
    pane.appendChild(h("div", { class: "ctl" }, addSel, ikb));
  }

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
      const ren = h("button", { class: "ghost", type: "button" }, "rename"); ren.onclick = () => { const nn = prompt("Rename component", n); if (nn && nn !== n) { comps[nn] = comps[n]; delete comps[n]; markDirty(); renderComponents(); } };
      const del = h("button", { class: "ghost danger", type: "button" }, "✕"); del.onclick = () => { delete comps[n]; markDirty(); renderComponents(); };
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
      pane.appendChild(h("div", { class: "note", html: "Your own themes live in a folder of their own, outside the repo. Start the server with <code>--themes</code> to list and save them here:<br><br><code>python3 tools/theme-studio/serve.py --themes ~/BananaThemes</code>" }));
    } else {
      pane.appendChild(h("div", { class: "muted", style: "font-size:12px" }, "in " + RENDER.localThemes));
      const save = h("button", { class: "primary", type: "button" }, "Save here");
      save.onclick = async () => {
        try { const r = await RENDER.saveLocal(fileStem(), state.theme, assetList()); setStatus("saved to " + r.path, "ok"); loadLocalThemes(); }
        catch (e) { setStatus("save failed: " + e.message, "bad"); }
      };
      pane.appendChild(h("div", { class: "ctl", style: "margin:4px 0" }, save));
      const list = state.localThemes || [];
      if (!list.length) pane.appendChild(h("div", { class: "muted" }, "(nothing there yet — Save here puts one in)"));
      list.forEach(function (t) {
        const b = h("button", { type: "button" }, t.name);
        b.onclick = () => { try { loadTheme(looseParse(t.json)); setStatus("opened " + t.name, "ok"); } catch (e) { setStatus("couldn't load " + t.name + ": " + e.message, "bad"); } };
        pane.appendChild(h("div", { class: "ctl" }, b));
      });
    }
    pane.appendChild(h("h3", { class: "sec" }, "Start from a template"));
    pane.appendChild(h("p", { class: "muted" }, "One of the example themes BananaOS ships. Pick one and change it — that is the easiest way in."));
    (state.templates || []).forEach(function (t) { const b = h("button", { type: "button" }, t.name); b.title = t.file || ""; b.onclick = () => { loadTheme(looseParse(t.json)); setStatus("started from " + t.name, "ok"); }; pane.appendChild(h("div", { class: "ctl" }, b)); });
    const any = h("button", { class: "ghost", type: "button" }, "Start from the simple example"); any.onclick = () => loadTheme(clone(STARTER));
    pane.appendChild(h("div", { class: "ctl" }, any));
    pane.appendChild(h("p", { class: "muted", style: "font-size:12px" }, "New, up top, always comes back to the simple example."));
  }

  // ---- assets ----------------------------------------------------------------------------------------
  const ASSET_EXTS = ["png", "jpg", "jpeg", "webp", "ttf", "otf", "wav"];
  function assetExt(p) { const m = /\.([a-z0-9]+)$/i.exec(p || ""); return m ? m[1].toLowerCase() : ""; }
  // the relative paths the theme names (heuristic: a string that looks like a file the engine could load)
  function detectAssets() {
    const found = [], seen = {};
    (function scan(v) {
      if (typeof v === "string") {
        if (v && v.charAt(0) !== "=" && v.indexOf("{") < 0 && ASSET_EXTS.indexOf(assetExt(v)) >= 0) {
          if (!seen[v]) { seen[v] = 1; found.push(v); }
        }
      } else if (Array.isArray(v)) v.forEach(scan);
      else if (v && typeof v === "object") Object.keys(v).forEach((k) => scan(v[k]));
    })(state.theme);
    return found;
  }
  function assetList() {
    return Object.keys(state.assets).filter((p) => state.assets[p] && state.assets[p].data)
      .map((p) => ({ path: p, data: state.assets[p].data }));
  }
  function attach(path, file) {
    if (!file) return;
    const r = new FileReader();
    r.onload = () => { state.assets[path] = { name: file.name, data: r.result }; markDirty(); renderAssets(); };
    r.readAsDataURL(file);
  }
  function renderAssets() {
    const pane = $("#pane-assets"); pane.innerHTML = "";
    pane.appendChild(h("p", { class: "muted" }, "Pictures, fonts and sounds the theme names, as paths relative to its .json. Attach a file here and previews and \u201cSend to handheld\u201d carry it; without it the engine uses its default."));
    const refs = detectAssets();
    if (!refs.length) pane.appendChild(h("div", { class: "muted" }, "this theme names no pictures, fonts or sounds yet"));
    refs.forEach(function (p) {
      const a = state.assets[p];
      const row = h("div", { class: "ctl", style: "margin:3px 0" });
      row.appendChild(h("span", { class: "mono", style: "flex:1;overflow:hidden;text-overflow:ellipsis" }, p));
      row.appendChild(h("span", { class: "pill" }, a ? a.name : "not attached"));
      const file = h("input", { type: "file", accept: ".png,.jpg,.jpeg,.webp,.ttf,.otf,.wav" });
      file.addEventListener("change", function () { attach(p, this.files[0]); });
      row.appendChild(file);
      if (a) { const rm = h("button", { class: "ghost danger", type: "button" }, "✕"); rm.onclick = () => { delete state.assets[p]; markDirty(); renderAssets(); }; row.appendChild(rm); }
      pane.appendChild(row);
    });
    const np = h("input", { type: "text", placeholder: "my-theme/extra.png" });
    const nf = h("input", { type: "file", accept: ".png,.jpg,.jpeg,.webp,.ttf,.otf,.wav" });
    const add = h("button", { class: "ghost", type: "button" }, "attach as…");
    add.onclick = () => { if (np.value && nf.files[0]) attach(np.value, nf.files[0]); };
    pane.appendChild(h("div", { class: "ctl" }, np, nf, add));
    pane.appendChild(h("h3", { class: "sec" }, "send to handheld"));
    if (!RENDER || !RENDER.available || !RENDER.themesDir) {
      pane.appendChild(h("div", { class: "note", html: "Not available. Start the server with <code>--themes-dir</code> pointing at a mounted card, e.g.<br><br><code>python3 tools/theme-studio/serve.py --themes-dir /run/media/you/BANANA/banana/themes</code>" }));
      return;
    }
    pane.appendChild(h("div", { class: "muted" }, "writes into " + RENDER.themesDir));
    const nm = h("input", { type: "text", value: fileStem() });
    const go = h("button", { class: "primary", type: "button" }, "Send");
    go.onclick = async () => {
      try { const r = await RENDER.send(nm.value, state.theme, assetList()); setStatus("sent to " + r.path, "ok"); }
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

  // ---- the bottom dock: preview controls and the JSON tab --------------------------------------------
  function renderPreviewControls() {
    const pane = $("#preview-controls"); pane.innerHTML = "";
    if (!RENDER || !RENDER.available) {
      pane.appendChild(h("div", { class: "note", html: "Previews need the local server. In a terminal:<br><br><code>python3 tools/theme-studio/serve.py</code><br><br>then open the address it prints. Editing, the tree, expressions and colour keys all work without it." }));
      return;
    }
    const srow = h("div", { class: "field" }); srow.appendChild(h("label", null, "panel size"));
    const sel = h("select");
    [["640x480", "RG DS  640 × 480"], ["1024x768", "RG DS Plus  1024 × 768"]].forEach(function (o) {
      const op = h("option", { value: o[0] }, o[1]); if (state.size === o[0]) op.setAttribute("selected", ""); sel.appendChild(op);
    });
    sel.onchange = () => { state.size = sel.value; doPreview(); };
    srow.appendChild(h("div", { class: "ctl" }, sel)); pane.appendChild(srow);
    const go = h("button", { class: "primary", type: "button" }, "Render preview"); go.onclick = doPreview;
    pane.appendChild(h("div", { class: "ctl", style: "margin-top:6px" }, go, h("span", { class: "pill" }, state.shots ? state.shots.length + " shot(s)" : "not drawn yet")));
    pane.appendChild(h("div", { class: "muted", style: "font-size:12px;margin-top:6px" }, "Draws the theme with the real engine (ui/banana-ui --headless). Its complaints appear under Problems."));
  }
  function renderJson() {
    const pane = $("#json"); pane.innerHTML = "";
    const ta = h("textarea", { spellcheck: "false" }); ta.value = JSON.stringify(state.theme, null, 2) + "\n";
    const apply = h("button", { class: "primary", type: "button" }, "Apply");
    apply.onclick = () => {
      try { state.theme = looseParse(ta.value); state.sel = null; normalize(); renderAll(); rerender(); markDirty(); setStatus("applied the JSON", "ok"); }
      catch (e) { setStatus("JSON error: " + e.message, "bad"); }
    };
    const revert = h("button", { type: "button" }, "Revert"); revert.onclick = () => { ta.value = JSON.stringify(state.theme, null, 2) + "\n"; };
    pane.appendChild(h("div", { class: "row" }, apply, revert, h("span", { class: "muted", style: "font-size:12px" }, "comments and trailing commas are allowed; saving writes plain JSON")));
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
    const errs = [];
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
    return errs;
  }
  function doValidate() {
    const errs = validate();
    const log = $("#log"); log.classList.remove("hidden");
    log.innerHTML = "";
    log.appendChild(h("div", { class: "i" }, "validate: " + (errs.length ? errs.length + " problem(s)" : "no expression or reference problems")));
    errs.forEach((e) => log.appendChild(h("div", { class: "e" }, e)));
  }

  // ---- preview & log ---------------------------------------------------------------------------------
  let previewTimer = null;
  function markDirty() { setStatus("edited", ""); schedulePreview(); }
  function schedulePreview() { if (previewTimer) clearTimeout(previewTimer); previewTimer = setTimeout(function () { if (RENDER && RENDER.available) doPreview(); }, 700); }
  async function doPreview() {
    try {
      const r = await RENDER.render(state.theme, null, state.size, assetList());
      state.shots = r.shots || [];
      showLog(r.log, r.errors || []);
      renderPreview(); renderPreviewControls();
    } catch (e) { showLog(String(e.message || e), [String(e.message || e)]); }
  }
  function showLog(text, errors) {
    const log = $("#log"); log.classList.remove("hidden"); log.innerHTML = "";
    if (errors && errors.length) { log.appendChild(h("div", { class: "e" }, errors.length + " theme error(s):")); errors.forEach((e) => log.appendChild(h("div", { class: "e" }, e))); }
    else log.appendChild(h("div", { class: "i" }, "the engine didn't complain about the theme"));
    if (text) log.appendChild(h("div", { class: "i" }, text.slice(0, 4000)));
  }
  function renderPreview() {
    const p = $("#preview"); p.innerHTML = "";
    if (!RENDER || !RENDER.available) {
      p.appendChild(h("div", { class: "note", html: "The Design canvas shows your theme drawn by the menu's own engine, which needs the local server. In a terminal:<br><br><code>python3 tools/theme-studio/serve.py</code><br><br>then open the address it prints. Everything else — the tree, the inspector, expressions and the colours — works without it. There is a walkthrough in <code>docs/MAKE-A-THEME.md</code>, or press Guide up top." }));
      return;
    }
    if (!state.shots || !state.shots.length) { p.appendChild(h("div", { class: "note" }, "Press Render preview (bottom) to draw the theme with the real engine.")); return; }
    const shots = h("div", { class: "shots" });
    state.shots.forEach(function (s) {
      const card = h("div", { class: "shot" });
      card.appendChild(h("div", { class: "cap" }, s.name + "  (top / bottom)"));
      const pair = h("div", { class: "pair" });
      if (s.top) pair.appendChild(h("img", { src: s.top, alt: s.name + " top" }));
      if (s.bot) pair.appendChild(h("img", { src: s.bot, alt: s.name + " bottom" }));
      card.appendChild(pair); shots.appendChild(card);
    });
    p.appendChild(shots);
  }

  // ---- load / save -----------------------------------------------------------------------------------
  function loadTheme(t) {
    state.theme = t || clone(STARTER);
    state.sel = null; state.shots = null; state.assets = {};
    normalize();
    renderAll(); rerender(); markDirty();
  }
  function saveTheme() {
    const name = (state.theme.name || "theme").toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "theme";
    const blob = new Blob([JSON.stringify(state.theme, null, 2) + "\n"], { type: "application/json" });
    const a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = name + ".json";
    document.body.appendChild(a); a.click(); a.remove();
    setStatus("saved " + name + ".json", "ok");
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
  }
  function setStatus(txt, cls) { const s = $("#status"); s.textContent = txt; s.className = "status" + (cls ? " " + cls : ""); }
  function rerender() { renderInspector(); }
  function renderAll() { renderPalette(); renderTree(); renderColours(); renderVars(); renderComponents(); renderAssets(); renderInput(); renderBehaviour(); renderTheme(); renderPreviewControls(); renderJson(); }

  // The example themes: serve.py's list when it is running, else the copies inlined in templates.js — the page
  // is usually opened straight from a checkout with no server, and file:// pages cannot fetch their neighbours.
  function loadTemplates() {
    const bundled = (typeof BananaTemplates !== "undefined" && BananaTemplates) || [];
    if (bundled.length) { state.templates = bundled; renderTheme(); }
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
        state.systemIds = all; renderColours();
      }
    }).catch(() => {});
  }

  function wire() {
    document.querySelectorAll(".tabs button").forEach((b) => { b.onclick = () => showTab(b.parentElement.dataset.group, b.dataset.tab); });
    // the dark/light switch, like the website's
    const root = document.documentElement;
    const setTheme = (t) => { root.setAttribute("data-theme", t); const lab = $("#themeLabel"); if (lab) lab.textContent = t === "light" ? "Light" : "Dark"; try { localStorage.setItem("banana-studio-theme", t); } catch (e) { /* ignore */ } };
    try { const q = new URLSearchParams(location.search).get("theme"); const saved = q || localStorage.getItem("banana-studio-theme"); if (saved) setTheme(saved); } catch (e) { /* ignore */ }
    const sw = $("#themeSwitch"); if (sw) sw.onclick = () => setTheme(root.getAttribute("data-theme") === "light" ? "dark" : "light");
    $("#newBtn").onclick = () => { if (confirm("Start a new theme? Unsaved changes are lost.")) loadTheme(clone(STARTER)); };
    $("#openBtn").onclick = () => $("#fileInput").click();
    $("#fileInput").onchange = function () {
      const f = this.files[0]; if (!f) return;
      f.text().then((s) => { try { loadTheme(looseParse(s)); setStatus("opened " + f.name, "ok"); } catch (e) { alert("This file is not a theme Banana can read, so it was not opened.\n\n" + e.message +
        "\n\nThemes are JSON. If you are not sure, press New once and change that instead."); } });
      this.value = "";
    };
    $("#saveBtn").onclick = saveTheme;
    $("#copyBtn").onclick = () => { const t = JSON.stringify(state.theme, null, 2) + "\n"; (navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(() => setStatus("copied", "ok"), () => { const ta = h("textarea"); ta.value = t; document.body.appendChild(ta); ta.select(); try { document.execCommand("copy"); } catch (e) {} ta.remove(); setStatus("copied", "ok"); }); };
    $("#validateBtn").onclick = doValidate;
    $("#templatesBtn").onclick = () => { showTab("properties", "theme"); setStatus("Pick a starting point under “Start from a template”.", ""); };
    $("#guideBtn").onclick = () => window.open("../../docs/MAKE-A-THEME.md", "_blank", "noopener");
    window.addEventListener("keydown", (e) => { if ((e.ctrlKey || e.metaKey) && e.key === "s") { e.preventDefault(); saveTheme(); } });
  }

  // ---- go --------------------------------------------------------------------------------------------
  wire(); normalize(); renderAll(); renderInspector(); renderPreview();
  try {						// ?tab=theme / ?tab=json opens on that dock tab
    const t = new URLSearchParams(location.search).get("tab");
    if (t === "theme") showTab("properties", "theme");
    else if (t === "json") showTab("center", "json");
  } catch (e) { /* ignore */ }
  if (RENDER) {
    RENDER.check().then(function (ok) {
      if (ok) { setStatus("preview server ready", "ok"); loadTemplates(); loadLocalThemes(); loadSystemIds(); renderAssets(); } else setStatus("offline — edit & save; run serve.py for previews", "");
      renderPreview(); renderPreviewControls();
      try { if (new URLSearchParams(location.search).get("preview")) doPreview(); } catch (e) { /* ignore */ }
    });
  }
})();