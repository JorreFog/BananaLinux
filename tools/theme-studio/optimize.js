// Banana Theme Studio: Optimize for the handheld. Finds what makes a layout theme slow or battery-hungry on the
// RG DS / RG DS Plus, and fixes what can be fixed.
//
// A port of the desktop studio's optimize.py (its OPTIMIZE.md has the engine facts, the measurements and every
// rule); the rules, ids, texts and fixes are the same, so a theme gets the same findings in both studios.
//
//   const findings = BananaOptimize.analyze(doc, files);     // files: { "relative/path.png": { w, h } }
//   const r = BananaOptimize.apply(doc, chosenIdsOrFindings, files);   // { doc, changes: [{ src, dst, oldSize, newSize }] }
//
// apply() never changes doc and writes nothing: a shrunk picture is a change the caller makes (a new file beside
// the original, which the theme then names). A classic script with no dependencies but expr.js, so it works from
// file://; require()-able from Node for tests.
(function (global) {
  "use strict";
  const X = typeof module !== "undefined" && module.exports ? require("./expr.js") : global.BananaExpr;

  const W = 640, H = 480;
  const PLUS_SCALE = 1.6, CAP_SCALE = 2.0;
  const DEFAULT_FPS = 30, DEFAULT_SETTLE = 600, SETTLE_MARGIN = 100;
  const SAFE = "safe", LOOK = "look", ADVICE = "advice";
  const CATEGORIES = {
    safe: "Safe: the screen looks exactly the same",
    look: "Changes the look a little, or trades animation for speed",
    advice: "Advice: worth changing by hand",
  };
  const HOOKS = new Set(["time", "view", "W", "H", "screen", "clock", "date", "battery", "charging", "batteryMode", "online", "motion",
    "brand", "accent", "accent.base", "accent.light", "accent.dark", "accent.band", "accent.low", "cursor", "scroll",
    "count", "moved", "entered", "dir", "systems.count", "systemCount", "games.count", "gameCount", "launch.t",
    "launch.progress", "empty", "empty.title", "empty.hint"]);
  const HOOK_PREFIXES = ["palette.", "sys.", "game."];
  const ITEM_NAMES = ["i", "n", "sel", "rel", "pos", "first", "last", "ix", "iy", "item.key", "item.label"];
  const MOTION = new Set(["time", "moved", "entered", "launch.t", "launch.progress", "scroll", "pos"]);
  const NOT_PROPS = new Set(["type", "id", "comment", "//", "children", "item", "states", "smooth", "use", "with", "props"]);
  const SHAPE_KEYS = ["w", "h", "radius", "fill", "color", "gradient", "border", "borderColor", "shadow", "scale"];
  const IMAGE_SIZE_KEYS = ["w", "h", "scale"];
  const SIX = ["x", "y", "w", "h", "opacity", "scale"];
  const NOTCONST = { notconst: true };

  // ---- small helpers: Python's semantics where the texts or numbers depend on them ------------------------------
  const isObj = (v) => v !== null && typeof v === "object" && !Array.isArray(v);
  const isNum = (v) => typeof v === "number";
  const isStr = (v) => typeof v === "string";
  const has = (o, k) => isObj(o) && Object.prototype.hasOwnProperty.call(o, k);
  const union = (...ss) => { const o = new Set(); ss.forEach((s) => s && s.forEach((x) => o.add(x))); return o; };
  const inter = (a, b) => new Set([...a].filter((x) => b.has(x)));
  const subset = (a, b) => [...a].every((x) => b.has(x));
  const sameArr = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  const clone = (o) => JSON.parse(JSON.stringify(o));
  const trunc = (x) => (isFinite(x) ? Math.trunc(x) : x);		// Python's %d of a float
  function pyRound(x) {				// Python's round(): halves to even
    const f = Math.floor(x), d = x - f;
    if (d > 0.5) return f + 1;
    if (d < 0.5) return f;
    return f % 2 === 0 ? f : f + 1;
  }
  const g = (v) => X.formatG(v, 6);	// "%g"
  const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);
  function gcd(a, b) { a = Math.abs(a); b = Math.abs(b); while (b) { const t = a % b; a = b; b = t; } return a; }
  const truthy = (v) => X.evTruthy(v);
  const num = (v) => X.evNum(v);

  function pathStr(path) {
    let out = "";
    for (const p of path) out += isNum(p) ? "[" + p + "]" : (out ? "." : "") + String(p);
    return out;
  }
  function getAt(doc, path) { let v = doc; for (const p of path) v = v[p]; return v; }
  function setAt(doc, path, value) { getAt(doc, path.slice(0, -1))[path[path.length - 1]] = value; }

  // ---- values -----------------------------------------------------------------------------------------------------
  // a property's compiled form (expr.js's), or null for a value that isn't text
  function prop(value) { return isStr(value) ? X.parseProp(value) : null; }
  function namesIn(tree, out) {
    if (!tree) return out;
    if (tree.op === "var") out.add(tree.name);
    else if (tree.op === "call") tree.a.forEach((a) => namesIn(a, out));
    else if (tree.op !== "lit") (tree.a || []).forEach((a) => namesIn(a, out));
    return out;
  }
  // the names a property reads (empty for a literal); null when it doesn't parse
  function namesOf(value) {
    const p = prop(value);
    if (!p || p.kind === "literal") return new Set();
    if (p.error) return null;
    const out = new Set();
    if (p.kind === "expr") namesIn(p.expr, out);
    else p.parts.forEach((pt) => { if (pt[1]) namesIn(pt[1], out); });
    return out;
  }
  const namesOr = (v) => namesOf(v) || new Set();
  function isLiteral(value) { return !(value !== null && typeof value === "object") && (!prop(value) || prop(value).kind === "literal"); }
  function literalValue(value) {
    if (typeof value === "boolean") return value ? 1 : 0;
    if (isNum(value)) return value;
    if (value === null || value === undefined) return "";
    return value;
  }
  function numOr(v, def) { if (typeof v === "boolean") return v ? 1 : 0; if (isNum(v)) return v; return def; }
  function scope(vars, parent, resolve) {
    const s = new X.Scope(parent || null);
    if (resolve) s.resolve = (k) => ({ found: true, value: resolve(k) });
    if (vars) Object.keys(vars).forEach((k) => s.set(k, vars[k]));
    return s;
  }
  function evalProp(value, sc) { return X.evalProp(X.parseProp(value), sc); }

  class Theme {
    constructor(doc) {
      this.doc = doc;
      this.components = isObj(doc.components) ? doc.components : {};
      this.vars = [];				// [name, value, path] in order
      if (isObj(doc.vars)) Object.keys(doc.vars).forEach((k) => this.flattenVar(k, doc.vars[k], ["vars", k]));
      this.varNames = this.vars.map((v) => v[0]);
      this.varIndex = new Map(); this.varNames.forEach((n, i) => this.varIndex.set(n, i));
      this.withNames = new Set(); collectWith(doc, this.withNames);
      this.shadowable = union(this.withNames, ITEM_NAMES, ["screen"]);
      this.fixed = {};
      [["W", W], ["H", H]].forEach((kv) => { if (!this.varIndex.has(kv[0]) && !this.withNames.has(kv[0])) this.fixed[kv[0]] = kv[1]; });
      this.fixedNames = new Set(Object.keys(this.fixed));
      this.timeVars = new Set();
      this.motionVars = new Map();
      this.vars.forEach((v, i) => {
        const n = v[0], names = namesOr(v[1]);
        const earlier = new Set([...names].filter((m) => this.varIndex.has(m) && this.varIndex.get(m) < i));
        if (names.has("time") || [...earlier].some((m) => this.timeVars.has(m))) this.timeVars.add(n);
        let mo = inter(names, MOTION);
        earlier.forEach((m) => { mo = union(mo, this.motionVars.get(m)); });
        if (mo.size) this.motionVars.set(n, mo);
      });
      this.behavior = isObj(doc.behavior) ? doc.behavior : {};
      this.fps = Math.max(1, Math.min(60, numOr(this.behavior.fps, DEFAULT_FPS)));
      this.settle = numOr(this.behavior.settleMs, DEFAULT_SETTLE);
    }
    flattenVar(k, v, path) {
      if (isObj(v)) Object.keys(v).forEach((kk) => this.flattenVar(k + "." + kk, v[kk], path.concat(kk)));
      else if (Array.isArray(v)) v.forEach((vv, i) => this.flattenVar(k + "." + i, vv, path.concat(i)));
      else this.vars.push([k, v, path]);
    }
    known(name) {
      return HOOKS.has(name) || HOOK_PREFIXES.some((p) => name.startsWith(p)) || this.varIndex.has(name) || this.shadowable.has(name) || name.startsWith("item.");
    }
    motion(names) {
      if (!names || !names.size) return new Set();
      let out = inter(names, MOTION);
      names.forEach((n) => { out = union(out, this.motionVars.get(n)); });
      return out;
    }
    // the value when it is the same in every frame (a literal, or an expression of literals and W/H), else NOTCONST
    const(value) {
      if (value !== null && typeof value === "object") return NOTCONST;
      const p = prop(value);
      if (!p || p.kind === "literal") return literalValue(value);
      if (p.error) return NOTCONST;
      const names = namesOf(value);
      if (!names || !subset(names, this.fixedNames)) return NOTCONST;
      return X.evalProp(p, scope(this.fixed));
    }
  }
  function collectWith(v, out) {
    if (isObj(v)) Object.keys(v).forEach((k) => {
      const x = v[k];
      if ((k === "with" || k === "props") && isObj(x)) Object.keys(x).forEach((wk) => flatNames(wk, x[wk], out));
      collectWith(x, out);
    });
    else if (Array.isArray(v)) v.forEach((x) => collectWith(x, out));
  }
  function flatNames(k, v, out) {
    if (isObj(v)) Object.keys(v).forEach((kk) => flatNames(k + "." + kk, v[kk], out));
    else if (Array.isArray(v)) v.forEach((vv, i) => flatNames(k + "." + i, vv, out));
    else out.add(k);
  }

  // ---- the element tree, as layout.cpp compiles it ----------------------------------------------------------------
  class Node {
    constructor(path, eff, src, kind, parent, screen, flowChild, listRole, inItem) {
      Object.assign(this, { path, eff, src, kind, parent, screen, flowChild, listRole, inItem });
      this.children = [];
    }
    keyPath(k) { return (this.src[k] || this.path).concat(k); }
    label() {
      const e = this.eff;
      if ((this.listRole === "item" || this.listRole === "child") && this.parent) return "each item of " + this.parent.label();
      if (isStr(e.id)) return '"' + e.id + '"';
      const what = { text: "text", image: "picture", rect: "box", group: "group", list: "list" }[this.kind] || this.kind;
      let hint = "";
      if (this.kind === "text" && isStr(e.text)) hint = ' "' + (Array.from(e.text).slice(0, 24).join("") + (Array.from(e.text).length > 24 ? "..." : "")) + '"';
      else if (this.kind === "image" && isStr(e.src)) hint = " " + Array.from(e.src).slice(0, 32).join("");
      else if (isStr(e.use)) hint = " (" + e.use + ")";
      return "the " + what + hint + " at " + pathStr(this.path);
    }
    ancestors() { const out = []; for (let n = this.parent; n; n = n.parent) out.push(n); return out; }
  }
  function kindOf(eff) {
    if (isStr(eff.type)) return eff.type;
    return has(eff, "children") ? "group" : has(eff, "text") ? "text" : has(eff, "src") ? "image" : has(eff, "source") ? "list" : "rect";
  }
  function expand(raw, path, components) {
    const eff = {}, src = {}, chain = [[raw, path]];
    let cur = raw;
    while (isStr(cur.use)) {
      const name = cur.use;
      if (!has(components, name) || !isObj(components[name]) || chain.length > 16) return [null, null];
      cur = components[name];
      chain.push([cur, ["components", name]]);
    }
    for (const [obj, p] of chain) for (const k of Object.keys(obj)) {
      if (k === "use" || has(eff, k)) continue;
      eff[k] = obj[k]; src[k] = p;
    }
    return [eff, src];
  }
  function walk(theme) {
    const doc = theme.doc, nodes = [];
    const screens = isObj(doc.screens) ? doc.screens : {};
    function visit(raw, path, parent, screen, flowChild, listRole, inItem) {
      if (Array.isArray(raw)) {
        const n = new Node(path, {}, {}, "group", parent, screen, flowChild, listRole, inItem);
        nodes.push(n);
        raw.forEach((c, i) => { const kid = visit(c, path.concat(i), n, screen, false, null, inItem); if (kid) n.children.push(kid); });
        return n;
      }
      if (!isObj(raw)) return null;
      const [eff, src] = expand(raw, path, theme.components);
      if (!eff) return null;
      const n = new Node(path, eff, src, kindOf(eff), parent, screen, flowChild, listRole, inItem);
      nodes.push(n);
      const flow = n.kind === "group" && (eff.layout === "row" || eff.layout === "column");
      if (has(eff, "children")) {
        const cp = n.keyPath("children"), ch = eff.children;
        if (Array.isArray(ch)) ch.forEach((c, i) => {
          const kid = visit(c, cp.concat(i), n, screen, flow, n.kind === "list" && i === 0 && !has(eff, "item") ? "child" : null, inItem || (n.kind === "list" && i === 0));
          if (kid) n.children.push(kid);
        });
        else { const kid = visit(ch, cp, n, screen, flow, n.kind === "list" && !has(eff, "item") ? "child" : null, inItem || n.kind === "list"); if (kid) n.children.push(kid); }
      }
      if (has(eff, "item")) { const kid = visit(eff.item, n.keyPath("item"), n, screen, false, "item", true); if (kid) n.children.push(kid); }
      return n;
    }
    for (const view of Object.keys(screens)) {
      const sides = screens[view];
      if (!isObj(sides)) continue;
      for (const side of ["top", "bottom"]) {
        const v = sides[side];
        if (Array.isArray(v)) v.forEach((e, i) => visit(e, ["screens", view, side, i], null, view + "." + side, false, null, false));
        else if (isObj(v)) visit(v, ["screens", view, side], null, view + "." + side, false, null, false);
      }
    }
    return nodes;
  }
  // the views a screen ("home.top") is drawn in (layout.cpp screenFor)
  function viewsOf(theme, screen) {
    const screens = theme.doc.screens || {};
    const hasSide = (v, side) => { const s = screens[v]; const x = isObj(s) ? s[side] : undefined; return Array.isArray(x) ? x.length > 0 : isObj(x) && Object.keys(x).length > 0; };
    const [view, side] = [screen.slice(0, screen.indexOf(".")), screen.slice(screen.indexOf(".") + 1)];
    if (view === "all") return new Set(["home", "library", "launch"]);
    const out = new Set();
    for (const v of ["home", "library", "launch"]) for (const cand of [v, v === "launch" ? "library" : "home", "home"]) {
      if (hasSide(cand, side)) { if (cand === view) out.add(v); break; }
    }
    return out;
  }
  function leaves(value, path, out) {
    out = out || [];
    if (isObj(value)) Object.keys(value).forEach((k) => leaves(value[k], path.concat(k), out));
    else if (Array.isArray(value)) value.forEach((v, i) => leaves(v, path.concat(i), out));
    else out.push([path, value]);
    return out;
  }
  // [path, value, key] of every property of an element the engine evaluates
  function nodeLeaves(n) {
    const out = [];
    for (const k of Object.keys(n.eff)) { if (NOT_PROPS.has(k)) continue; leaves(n.eff[k], n.keyPath(k)).forEach((l) => out.push([l[0], l[1], k])); }
    if (Array.isArray(n.eff.states)) n.eff.states.forEach((st, i) => {
      if (isObj(st)) Object.keys(st).forEach((k) => leaves(st[k], n.keyPath("states").concat(i, k)).forEach((l) => out.push([l[0], l[1], "states"])));
    });
    for (const wk of ["with", "props"]) if (isObj(n.eff[wk])) leaves(n.eff[wk], n.keyPath(wk)).forEach((l) => out.push([l[0], l[1], wk]));
    return out;
  }
  function stateKeys(n) {
    const out = new Set();
    (Array.isArray(n.eff.states) ? n.eff.states : []).forEach((st) => { if (isObj(st)) Object.keys(st).forEach((k) => { if (k !== "when") out.add(k); }); });
    return out;
  }
  // [path, value] of every property in the document (elements anywhere, components, vars)
  function allProps(theme) {
    const doc = theme.doc, out = [];
    theme.vars.forEach((v) => out.push([v[2], v[1]]));
    function el(raw, path) {
      if (Array.isArray(raw)) { raw.forEach((c, i) => el(c, path.concat(i))); return; }
      if (!isObj(raw)) return;
      for (const k of Object.keys(raw)) {
        const v = raw[k];
        if (["type", "id", "comment", "//", "use", "smooth"].indexOf(k) >= 0) continue;
        if (k === "children") { if (Array.isArray(v)) v.forEach((c, i) => el(c, path.concat(k, i))); else el(v, path.concat(k)); }
        else if (k === "item") el(v, path.concat(k));
        else if (k === "states" && Array.isArray(v)) v.forEach((st, i) => { if (isObj(st)) Object.keys(st).forEach((sk) => leaves(st[sk], path.concat(k, i, sk), out)); });
        else leaves(v, path.concat(k), out);
      }
    }
    Object.keys(theme.components || {}).forEach((name) => el(theme.components[name], ["components", name]));
    const sc = doc.screens || {};
    Object.keys(sc).forEach((view) => { const sides = sc[view]; if (isObj(sides)) Object.keys(sides).forEach((side) => el(sides[side], ["screens", view, side])); });
    return out;
  }

  // ---- rewriting expressions ----------------------------------------------------------------------------------------
  const ID0 = /[A-Za-z_$]/, IDC = /[A-Za-z0-9_.$]/, C_SPACE = " \t\n\v\f\r";
  function substExpr(src, mapping) {
    let out = "", i = 0;
    const n = src.length;
    while (i < n) {
      const c = src[i];
      if (c === "'" || c === '"') {
        let j = i + 1;
        while (j < n && src[j] !== c) j += src[j] === "\\" ? 2 : 1;
        out += src.slice(i, j + 1); i = j + 1;
      } else if (c === "#") {
        let j = i + 1;
        while (j < n && /[0-9a-fA-F]/.test(src[j])) j++;
        out += src.slice(i, j); i = j;
      } else if ((c >= "0" && c <= "9") || (c === "." && i + 1 < n && src[i + 1] >= "0" && src[i + 1] <= "9")) {
        const j = Math.max(X.strtod(src, i).end, i + 1);
        out += src.slice(i, j); i = j;
      } else if (ID0.test(c)) {
        let j = i + 1;
        while (j < n && IDC.test(src[j])) j++;
        const ident = src.slice(i, j);
        let k = j;
        while (k < n && C_SPACE.indexOf(src[k]) >= 0) k++;
        const name = ident.charAt(0) === "$" ? ident.slice(1) : ident;
        out += has(mapping, name) && !(k < n && src[k] === "(") ? mapping[name] : ident;
        i = j;
      } else { out += c; i++; }
    }
    return out;
  }
  // a property's text with names replaced in its expression or its template's expressions
  function subst(value, mapping) {
    if (!isStr(value)) return value;
    if (value.startsWith("=")) return "=" + substExpr(value.slice(1), mapping);
    if (value.indexOf("{") < 0) return value;
    let out = "", i = 0;
    const t = value;
    while (i < t.length) {
      if (t.startsWith("{{", i) || t.startsWith("}}", i)) { out += t.slice(i, i + 2); i += 2; continue; }
      if (t[i] !== "{") { out += t[i]; i++; continue; }
      const e = t.indexOf("}", i);
      if (e < 0) { out += t.slice(i); break; }
      out += "{" + substExpr(t.slice(i + 1, e), mapping) + "}";
      i = e + 1;
    }
    return out;
  }
  // the JSON literal the engine reads as the value v; null when there is none
  function literalFor(v) {
    if (isNum(v)) return isFinite(v) ? v : null;
    if (isStr(v)) {
      if (v.startsWith("=")) return null;
      if (v.indexOf("{") >= 0) return v.replace(/\{/g, "{{").replace(/\}/g, "}}");
      return v;
    }
    return null;
  }

  // ---- analysis -------------------------------------------------------------------------------------------------------
  function F(id, rule, category, title, detail, path, o) {
    o = o || {};
    return { id, rule, category, title, detail, path, saving: o.saving || "", weight: o.weight === undefined ? 1 : o.weight,
      fixable: o.fixable === undefined ? true : o.fixable, data: o.data || {} };
  }
  function analyze(doc, files) {
    if (!isObj(doc) || !isObj(doc.screens)) return [];
    const theme = new Theme(doc), nodes = walk(theme), out = [], seen = new Set();
    const hidden = hiddenNodes(theme, nodes);
    const live = nodes.filter((n) => ![n].concat(n.ancestors()).some((a) => hidden.has(a)));
    for (const rule of [ruleTime, ruleFlowSmooth, ruleSettle, ruleUnusedVars, ruleFold, ruleDeadStates, ruleNoop, ruleHidden,
      ruleRange, ruleImages, ruleAnimatedWork, ruleUnknownNames, ruleMisc]) {
      for (const f of rule(theme, nodes, live, hidden, files || {})) if (!seen.has(f.id)) { seen.add(f.id); out.push(f); }
    }
    const order = { safe: 0, look: 1, advice: 2 };
    return out.map((f, i) => [f, i]).sort((a, b) => (order[a[0].category] - order[b[0].category]) || (b[0].weight - a[0].weight) || (a[1] - b[1])).map((x) => x[0]);
  }
  function viewsText(views) {
    const names = { home: "home", library: "game list", launch: "launch" };
    const vs = ["home", "library", "launch"].filter((v) => views.has(v)).map((v) => names[v]);
    return vs.length <= 2 ? vs.join(" and ") : vs.slice(0, -1).join(", ") + " and " + vs[vs.length - 1];
  }
  const nodeViews = (theme, n) => (n.screen ? viewsOf(theme, n.screen) : new Set());
  function hiddenNodes(theme, nodes) {
    const out = new Map();
    for (const n of nodes) {
      const sk = stateKeys(n);
      for (const k of ["visible", "if"]) if (has(n.eff, k) && !sk.has(k)) {
        const v = theme.const(n.eff[k]);
        if (v !== NOTCONST && !truthy(v)) out.set(n, k);
      }
      if (!out.has(n) && has(n.eff, "opacity") && !sk.has("opacity")) {
        const v = theme.const(n.eff.opacity);
        if (v !== NOTCONST && Math.min(1, Math.max(0, num(v))) <= 0.002) out.set(n, "opacity");
      }
    }
    return out;
  }

  // -- continuous redraw: time
  function workOf(theme, n, keys) {
    const work = [];
    const anyOf = (ks) => ks.some((k) => keys.has(k));
    const plain = (n.kind === "rect" || n.kind === "group") && !["radius", "gradient", "border", "shadow"].some((k) => has(n.eff, k));
    if (n.kind === "image" && anyOf(["w", "h", "scale"])) work.push("its picture is read and resized again in the background for each new size (and stretched meanwhile)");
    if ((n.kind === "rect" || n.kind === "group") && !plain && anyOf(SHAPE_KEYS)) work.push("its rounded/gradient/shadow shape is painted again pixel by pixel for each new size or colour");
    if (n.kind === "text" && anyOf(["size", "scale", "spacing", "text", "font"])) work.push("its text is rendered again");
    if (anyOf(["opacity", "tint"]) || (anyOf(["fill", "color"]) && n.kind !== "text" && !plain)) work.push("its fading is drawn by blending every one of its pixels (about 10 times the cost of moving it)");
    if (anyOf(["x", "y", "right", "bottom", "anchor", "ax", "ay"]) && !work.length) work.push("its old and new place are drawn again");
    return work;
  }
  function ruleTime(theme, nodes, live) {
    const readers = new Map();
    for (const n of live) for (const [p, v, k] of nodeLeaves(n)) {
      const names = namesOf(v);
      if (!names || !names.size) continue;
      if (names.has("time")) {
        if (!readers.has(n)) readers.set(n, new Set());
        readers.get(n).add(k !== "states" ? k : isStr(p[p.length - 1]) ? p[p.length - 1] : "states");
      }
    }
    const awake = {}, fps = theme.fps;
    const push = (v, x) => { (awake[v] = awake[v] || []).push(x); };
    readers.forEach((keys, n) => nodeViews(theme, n).forEach((v) => push(v, n)));
    theme.timeVars.forEach((name) => ["home", "library", "launch"].forEach((v) => push(v, "var " + name)));
    const rest = new Set(["home", "library"].filter((v) => (awake[v] || []).length));
    const out = [];
    readers.forEach((keys, n) => {
      const views = inter(nodeViews(theme, n), new Set(["home", "library"]));
      if (!views.size) return;
      const many = views.size > 1;
      const work = workOf(theme, n, keys);
      let detail = cap(n.label()) + ' changes with the clock (it reads "time"), so the ' + viewsText(views) + " screen" + (many ? "s" : "") +
        " redraw" + (many ? "" : "s") + " " + trunc(fps) + " times a second for as long as the menu is open, even when nobody touches anything.";
      if (work.length) detail += " Each of those frames, " + work.join("; ") + ".";
      detail += " Holding it still draws it as it looks when the menu starts.";
      let more = 0; views.forEach((v) => { more += (awake[v] || []).length - 1; });
      if (more) detail += " " + more + " other thing" + (more > 1 ? "s" : "") + " move" + (more > 1 ? "" : "s") + " with the clock there too; the screen only rests when all of them are still.";
      const tpaths = nodeLeaves(n).filter((l) => namesOr(l[1]).has("time")).map((l) => l[0]);
      out.push(F("hold-still:" + pathStr(n.path), "hold-still", LOOK, "Hold " + n.label() + " still", detail, n.path,
        { saving: "with everything still, the " + viewsText(views) + " screen" + (many ? "s stop redrawing" : " stops redrawing") + " at rest", weight: 3, data: { paths: tpaths } }));
    });
    [...theme.timeVars].sort((a, b) => theme.varIndex.get(a) - theme.varIndex.get(b)).forEach((name) => {
      const i = theme.varIndex.get(name), [, val, vpath] = theme.vars[i];
      const direct = namesOr(val).has("time");
      const users = live.filter((n) => nodeLeaves(n).some((l) => namesOr(l[1]).has(name)));
      let uviews = new Set(); users.forEach((u) => { uviews = union(uviews, nodeViews(theme, u)); });
      const detail = 'The var "' + name + '" changes with the clock (it reads "time"). Vars are worked out for every frame on every screen, so it keeps all of them redrawing ' +
        trunc(fps) + " times a second, even where nothing uses it. Holding it still freezes everything that uses it (" + users.length + " element" + (users.length === 1 ? "" : "s") + ") as it looks when the menu starts.";
      if (direct) out.push(F("hold-still:" + pathStr(vpath), "hold-still", LOOK, 'Hold the var "' + name + '" still', detail, vpath,
        { saving: "with everything still, every screen stops redrawing at rest", weight: 3, data: { paths: [vpath] } }));
      if (canInline(theme, name)[0]) {
        const idle = new Set(["home", "library"].filter((v) => !uviews.has(v)));
        const busy = new Set([...idle].filter((v) => (awake[v] || []).some((x) => !(isStr(x) && x.startsWith("var "))) || (awake[v] || []).some((x) => isStr(x) && x !== "var " + name)));
        const gain = new Set([...idle].filter((v) => !busy.has(v)));
        const nref = varRefs(theme, name).length;
        out.push(F("inline-time-var:" + pathStr(vpath), "inline-time-var", SAFE,
          'Move the moving var "' + name + '" into the ' + (nref === 1 ? "place" : nref + " places") + " that use" + (nref === 1 ? "s" : "") + " it",
          'The var "' + name + '" reads "time", and vars are worked out on every screen for every frame, so it keeps every screen redrawing ' + trunc(fps) +
          " times a second. Writing its formula into the elements that use it instead keeps exactly the same picture, and the screens that don't use it (" +
          (viewsText(new Set(["home", "library"].filter((v) => !uviews.has(v)))) || "none") + ") no longer redraw because of it.",
          vpath, { weight: gain.size ? 3 : 1, saving: gain.size ? "the " + viewsText(gain) + " screen can rest" : "nothing yet: the other screens also move for other reasons", data: { name: name } }));
      }
    });
    if (rest.size && fps > 20) {
      const pct = pyRound(100 * (1 - 20 / fps));
      out.push(F("lower-fps", "lower-fps", LOOK, "Redraw moving things 20 times a second instead of " + trunc(fps),
        "Something on the " + viewsText(rest) + " screen moves all the time, so the menu draws " + trunc(fps) + " frames a second. At 20 the drifting and twinkling looks a little less smooth, and the menu does " +
        pct + "% less work for it (button presses and scrolling still run at full speed).", ["behavior", "fps"],
        { saving: pct + "% fewer frames while only the clock moves", weight: 2, data: { fps: 20 } }));
    }
    return out;
  }
  function varRefs(theme, name) {
    const i = theme.varIndex.get(name), refs = [];
    for (const [p, v] of allProps(theme)) {
      if (p[0] === "vars") { const j = theme.vars.findIndex((x) => sameArr(x[2], p)); if (j <= i) continue; }
      if (namesOr(v).has(name)) refs.push(p);
    }
    return refs;
  }
  function canInline(theme, name) {
    const i = theme.varIndex.get(name), val = theme.vars[i][1];
    if (!isStr(val) || !val.startsWith("=") || X.parseProp(val).error) return [false, "not an expression"];
    if (theme.shadowable.has(name)) return [false, "a component parameter or list item name has the same name"];
    for (const m of namesOr(val)) {
      if (theme.shadowable.has(m)) return [false, '"' + m + '" means something else inside a component or list'];
      if (theme.varIndex.has(m) && theme.varIndex.get(m) > i) return [false, "it reads a var written after it"];
    }
    return [true, ""];
  }

  // -- smooth inside rows and flow lists
  function smoothKeys(eff) {
    const s = eff.smooth;
    if (isNum(s)) return new Set(s > 0 ? SIX : []);
    if (isObj(s)) return new Set(Object.keys(s).filter((k) => !(isNum(s[k]) && s[k] <= 0)));
    return new Set();
  }
  function measured(n) {
    if (n.flowChild) return new Set(["w", "h"]);
    if (n.listRole === "item" && n.parent && truthy(literalValue(has(n.parent.eff, "flow") ? n.parent.eff.flow : false)) && (n.parent.eff.direction === "row" || n.parent.eff.direction === "column"))
      return new Set(n.parent.eff.direction === "row" ? ["w"] : ["h"]);
    return new Set();
  }
  const dims = (ks) => [...ks].sort().reverse().map((k) => ({ w: "width", h: "height" })[k]).join(" and ");
  function ruleFlowSmooth(theme, nodes, live) {
    const hits = [];
    for (const n of live) { const ks = inter(smoothKeys(n.eff), measured(n)); if (ks.size) hits.push([n, ks]); }
    if (!(hits.length >= 2 || hits.some((h) => h[0].listRole === "item"))) return [];
    return hits.map(([n, ks]) => F("flow-smooth:" + pathStr(n.path), "flow-smooth", LOOK, "Stop " + n.label() + " easing its " + dims(ks) + " inside a row",
      cap(n.label()) + " eases its " + dims(ks) + ' ("smooth") while it sits in a row, column or "flow" list. The engine measures such elements under one shared name, so the eased values never settle: the menu redraws every refresh ' +
      "(60 times a second) forever, and the items are also spaced as if they all had the first one's size. Without the easing the size changes at once instead of sliding.",
      n.keyPath("smooth"), { saving: "the screen can rest (now it redraws 60 times a second)", weight: 3, data: { keys: [...ks].sort() } }));
  }

  // -- settleMs
  function progressEnds(tree, out) {
    if (!tree) return true;
    const op = tree.op;
    if (op === "var") return tree.name !== "moved" && tree.name !== "entered";
    if (op === "lit") return true;
    const isMove = (t) => t && t.op === "var" && (t.name === "moved" || t.name === "entered");
    if (op === "call" && tree.name === "progress" && tree.a.length >= 1) {
      const a = tree.a[0], d = tree.a.length > 1 ? tree.a[1] : { op: "lit", lit: 1 };
      let shift = null;
      if (isMove(a)) shift = 0;
      else if ((a.op === "sub" || a.op === "add") && isMove(a.a[0]) && a.a[1].op === "lit" && !isStr(a.a[1].lit)) shift = a.op === "sub" ? a.a[1].lit : -a.a[1].lit;
      if (shift !== null) {
        if (d.op !== "lit" || isStr(d.lit)) return false;
        out.push(Math.max(1, d.lit) + shift);
        return tree.a.slice(1).every((x) => progressEnds(x, out));
      }
      return tree.a.every((x) => progressEnds(x, out));
    }
    if (op === "call") return tree.a.every((x) => progressEnds(x, out));
    return (tree.a || []).every((x) => progressEnds(x, out));
  }
  function ruleSettle(theme) {
    const ends = [];
    for (const [, v] of allProps(theme)) {
      const pr = prop(v);
      if (!pr || pr.kind === "literal" || pr.error) continue;
      const trees = pr.kind === "expr" ? [pr.expr] : pr.parts.map((x) => x[1]).filter(Boolean);
      for (const t of trees) if (!progressEnds(t, ends)) return [];
    }
    const longest = Math.max.apply(null, ends.concat([0]));
    const need = Math.ceil((longest + SETTLE_MARGIN) / 10) * 10;
    if (theme.settle - need < 50) return [];
    const frames = pyRound((theme.settle - need) / (1000 / 60));
    return [F("settle", "settle", SAFE, "Stop redrawing " + trunc(need) + " ms after a button press instead of " + trunc(theme.settle),
      "After every press the menu keeps drawing for behavior.settleMs (" + trunc(theme.settle) + ' ms) so that animations of "moved" and "entered" can finish. The longest of this theme\'s ends after ' +
      trunc(longest) + " ms; the frames after that draw the same picture again. Lowering it to " + trunc(need) + " ms (with a " + SETTLE_MARGIN + " ms margin) changes nothing on screen.",
      ["behavior", "settleMs"], { saving: "about " + frames + " fewer frames after every press", weight: 1, data: { settleMs: Math.trunc(need) } })];
  }

  // -- vars nothing reads
  function ruleUnusedVars(theme) {
    const out = [];
    for (const [name, , vpath] of theme.vars) {
      if (varRefs(theme, name).length) continue;
      const t = theme.timeVars.has(name);
      out.push(F("unused-var:" + pathStr(vpath), "unused-var", SAFE, 'Remove the unused var "' + name + '"',
        'Nothing reads the var "' + name + '", but every var is worked out again for every frame.' + (t ? ' It also reads "time", which keeps every screen redrawing ' + trunc(theme.fps) + " times a second." : ""),
        vpath, { saving: t ? "every screen can rest" : "one expression less per frame", weight: t ? 3 : 0 }));
    }
    return out;
  }

  // -- constant expressions
  const show = (v) => (isStr(v) ? '"' + v + '"' : g(v));
  function ruleFold(theme) {
    const out = [];
    for (const [p, v] of allProps(theme)) {
      if (!isStr(v) || isLiteral(v)) continue;
      const c = theme.const(v);
      if (c === NOTCONST) continue;
      const lit = literalFor(c);
      if (lit === null || lit === v) continue;
      const shortV = Array.from(v).length <= 40 ? v : Array.from(v).slice(0, 37).join("") + "...";
      out.push(F("fold:" + pathStr(p), "fold", SAFE, "Write " + show(lit) + ' instead of "' + shortV + '"',
        '"' + v + '" gives ' + show(lit) + " in every frame; writing the value instead saves working it out again for every frame (and for every list item). The formula is replaced by its value.",
        p, { saving: "one expression less per frame", weight: 0, data: { value: lit } }));
    }
    return out;
  }
  function ruleDeadStates(theme, nodes) {
    const out = [], done = new Set();
    for (const n of nodes) {
      const sts = n.eff.states;
      if (!Array.isArray(sts)) continue;
      const sp = n.keyPath("states");
      sts.forEach((st, i) => {
        const key = JSON.stringify(sp.concat(i));
        if (!isObj(st) || done.has(key)) return;
        const w = has(st, "when") ? st.when : "0";
        const c = theme.const(isNum(w) ? String(literalFor(w)) : w);
        if (c !== NOTCONST && !truthy(c)) {
          done.add(key);
          out.push(F("dead-state:" + pathStr(sp.concat(i)), "dead-state", SAFE, "Remove a state of " + n.label() + " that never applies",
            'Its "when" (' + show(isStr(w) ? w : literalValue(w)) + ") is never true, but it is checked for every frame.", sp.concat(i), { saving: "one check less per frame", weight: 0 }));
        }
      });
    }
    return out;
  }

  // -- properties that do nothing
  function noopKeys(theme, n) {
    const eff = n.eff, out = [];
    for (const k of ["opacity", "scale"]) if (has(eff, k) && isLiteral(eff[k])) {
      const v = literalValue(eff[k]);
      if ((k === "opacity" && num(v) >= 1 && !isStr(v)) || (k === "scale" && v === 1)) out.push(k);
    }
    for (const k of ["visible", "if"]) if (has(eff, k) && isLiteral(eff[k]) && truthy(literalValue(eff[k]))) out.push(k);
    if (has(eff, "clip") && isLiteral(eff.clip) && !truthy(literalValue(eff.clip))) out.push("clip");
    const sh = eff.shadow;
    if (isObj(sh) && !stateKeys(n).has("shadow") && ["rect", "group", "text"].indexOf(n.kind) >= 0) {
      const col = has(sh, "color") ? sh.color : null;
      if (col === null || (isLiteral(col) && isStr(col) && X.parseColorText(col) !== null && (X.parseColorText(col) >>> 24) === 0)) out.push("shadow");
    }
    return out;
  }
  function ruleNoop(theme, nodes) {
    const out = [], done = new Set();
    for (const n of nodes) {
      const keys = noopKeys(theme, n), sk = smoothKeys(n.eff), dropSmooth = [];
      if (sk.size && !measured(n).size && has(n.eff, "smooth")) {
        const stk = stateKeys(n);
        [...sk].sort().forEach((k) => { const v = n.eff[k]; if (!stk.has(k) && (v === undefined || isNum(v))) dropSmooth.push(k); });
      }
      const by = new Map();
      const add = (owner, k) => { const key = JSON.stringify(owner); if (!by.has(key)) by.set(key, [owner, []]); by.get(key)[1].push(k); };
      keys.forEach((k) => add(n.src[k] || n.path, k));
      if (dropSmooth.length) add(n.src.smooth || n.path, "smooth:" + dropSmooth.join(","));
      by.forEach(([owner, ks]) => {
        const fid = "noop:" + pathStr(owner);
        if (done.has(fid)) return;
        done.add(fid);
        const words = ks.map((k) => (k.startsWith("smooth:") ? '"smooth" of ' + k.split(":").slice(1).join(":") + " (the value never changes)" : '"' + k + '"'));
        out.push(F(fid, "noop", SAFE, "Remove properties of " + n.label() + " that do nothing", "These properties have their default value or no effect: " + words.join(", ") + ".",
          owner.slice(), { saving: "a little less to look up per frame", weight: 0,
            data: { keys: ks.filter((k) => !k.startsWith("smooth:")), smooth: ks.some((k) => k.startsWith("smooth:")) ? dropSmooth : [] } }));
      });
    }
    return out;
  }

  // -- elements never drawn
  const count = (n) => n.children.reduce((s, c) => s + 1 + count(c), 0);
  function ruleHidden(theme, nodes, live, hidden) {
    const out = [];
    hidden.forEach((why, n) => {
      if (n.ancestors().some((a) => hidden.has(a))) return;
      if (n.listRole === "item" || n.listRole === "child") return;
      if (why === "opacity" && n.flowChild) return;
      const what = { visible: 'is never visible ("visible" is always false)', if: 'is never shown ("if" is always false)', opacity: "is fully transparent" }[why];
      out.push(F("hidden:" + pathStr(n.path), "hidden", SAFE, "Remove " + n.label() + ", which is never drawn",
        cap(n.label()) + " " + what + ", but the engine still looks at it" + (n.children.length ? " and everything in it" : "") + " for every frame.",
        n.path, { saving: (1 + count(n)) + " element" + (count(n) === 0 ? "" : "s") + " less per frame", weight: n.children.length ? 1 : 0 }));
    });
    return out;
  }

  // -- free lists drawing invisible items
  function mockScope(theme, extra) {
    const hooks = { time: 0, moved: 1e9, entered: 1e9, W: W, H: H, count: 10, cursor: 3, scroll: 3, screen: 0 };
    const root = scope(null, null, (k) => (has(hooks, k) ? hooks[k] : ""));
    const vs = scope(null, root);
    for (const [name, val] of theme.vars) vs.set(name, isStr(val) ? evalProp(val, vs) : literalValue(val));
    return scope(extra, vs);
  }
  const DEFAULT_INPUT = {
    home: { left: "prev", up: "prev", l: "prev", right: "next", down: "next", r: "next", a: "open", x: "resume", y: "fav" },
    library: { left: "prev", right: "next", up: "prev:10", down: "next:10", l: "prevSystem", r: "nextSystem", a: "open", b: "back", y: "fav" },
  };
  function jump(theme, view) {
    const m = Object.assign({}, DEFAULT_INPUT[view]);
    const inp = theme.doc.input;
    if (isObj(inp) && isObj(inp[view])) Object.keys(inp[view]).forEach((k) => { m[k] = isStr(inp[view][k]) ? inp[view][k] : "none"; });
    let j = 0;
    Object.values(m).forEach((act) => {
      const c = act.indexOf(":"), a = c >= 0 ? act.slice(0, c) : act, arg = c >= 0 ? act.slice(c + 1) : "";
      if (a === "prev" || a === "next") j = Math.max(j, arg ? Math.max(1, num(arg)) : 1);
      else if (a === "first" || a === "last") j = Infinity;
    });
    return j;
  }
  function ruleRange(theme, nodes, live) {
    const out = [];
    for (const n of live) {
      if (n.kind !== "list" || (has(n.eff, "direction") ? n.eff.direction : "free") !== "free") continue;
      const source = has(n.eff, "source") ? n.eff.source : "systems";
      if (source !== "systems" && source !== "games") continue;
      const tpl = n.children.find((c) => c.listRole === "item" || c.listRole === "child");
      if (!tpl || (has(n.eff, "range") && !isLiteral(n.eff.range))) continue;
      const rng = Math.trunc(num(literalValue(has(n.eff, "range") ? n.eff.range : 8)));
      const keys = ["opacity", "visible", "if"].filter((k) => has(tpl.eff, k));
      if (!keys.length || keys.some((k) => stateKeys(tpl).has(k))) continue;
      const okNames = union(["pos", "rel", "i", "n", "sel", "first", "last"], theme.varIndex.keys(), theme.fixedNames);
      if (keys.some((k) => { const nm = namesOf(tpl.eff[k]); return nm === null || !subset(nm, okNames); })) continue;
      const gone = (pos) => {
        for (const sel of [0, 1]) {
          const sc = mockScope(theme, { pos: pos, rel: pyRound(pos), i: 0, n: 100, sel: sel, first: 0, last: 0 });
          const vals = {};
          keys.forEach((k) => { vals[k] = isStr(tpl.eff[k]) ? evalProp(tpl.eff[k], sc) : literalValue(tpl.eff[k]); });
          if (!(["visible", "if"].some((k) => has(vals, k) && !truthy(vals[k])) || (has(vals, "opacity") && Math.min(1, Math.max(0, num(vals.opacity))) <= 0.002))) return false;
        }
        return true;
      };
      const goneFrom = (r, to) => {
        const steps = Math.trunc((to - r) * 10) + 1;
        for (let k = 0; k < steps; k++) for (const sgn of [1, -1]) if (!gone(sgn * (r + k / 10))) return false;
        return true;
      };
      let seenR = null;
      for (let r = 0; r <= rng; r++) if (goneFrom(r, rng + 2)) { seenR = r; break; }
      if (seenR === null) continue;
      const view = source === "systems" ? "home" : "library";
      const jp = Math.max(jump(theme, view), seenR, source === "systems" ? 1 : 0);
      if (!isFinite(jp)) continue;
      const best = Math.max(1, Math.ceil(seenR + jp - 1));
      if (best < rng && goneFrom(best + 1 - jp, rng + jp)) {
        out.push(F("range:" + pathStr(n.path), "range", LOOK, "Let " + n.label() + " work out " + best + " items each side instead of " + rng,
          'A "free" list works out every item within "range" (' + rng + ") of the selection, but its items are invisible from " + seenR +
          " places away from where the list has scrolled to (found by trying the item's opacity and visible there), and one press or tap moves at most " + trunc(jp) +
          ' places. With "range" at ' + best + " the list should look the same; check the scrolling.",
          has(n.eff, "range") ? n.keyPath("range") : n.path.concat("range"), { saving: (2 * (rng - best)) + " fewer items worked out per frame while it moves", weight: 1, data: { range: best } }));
      }
    }
    return out;
  }

  // -- pictures: files are the theme's attached files, by their path relative to its .json, with their sizes
  function normRel(ref) {
    const parts = [];
    for (const p of ref.replace(/\\/g, "/").split("/")) {
      if (p === "" || p === ".") continue;
      if (p === "..") { if (!parts.length) return ""; parts.pop(); } else parts.push(p);
    }
    return parts.join("/");
  }
  function themeFile(files, ref) {
    if (!isStr(ref) || !ref || ref.startsWith("/") || ref.startsWith("romicon:") || ref.startsWith("=") || ref.indexOf("{") >= 0) return "";
    const p = normRel(ref);
    return p && has(files, p) ? p : "";
  }
  function len(theme, v, rel) {
    if (v === undefined || v === null) return null;
    const c = theme.const(v);
    if (c === NOTCONST) return null;
    if (isStr(c)) {
      if (c.endsWith("%")) return rel === null ? null : num(c.slice(0, -1)) / 100 * rel;
      return c ? num(c) : null;
    }
    return c;
  }
  function choiceMax(theme, n, k) {
    const vals = [];
    const srcs = [n.eff].concat((Array.isArray(n.eff.states) ? n.eff.states : []).filter(isObj));
    for (const src of srcs) {
      if (!has(src, k)) continue;
      const v = src[k], c = theme.const(v);
      if (c !== NOTCONST) { vals.push(num(c)); continue; }
      const names = namesOf(v);
      if (names === null || !subset(names, union(["sel", "first", "last"], theme.fixedNames))) return null;
      for (const a of [0, 1]) for (const b of [0, 1]) vals.push(num(evalProp(v, scope(Object.assign({}, theme.fixed, { sel: a, first: b, last: b })))));
    }
    return vals.length ? Math.max.apply(null, vals) : 1;
  }
  function box(theme, n) {
    let pw, ph, k;
    if (!n.parent) { pw = W; ph = H; k = 1; }
    else {
      const p = n.parent, pb = box(theme, p);
      if (!pb) return null;
      [pw, ph, k] = pb;
      if (p.kind === "list") {
        const d = has(p.eff, "direction") ? p.eff.direction : "free";
        if (d !== "free") {
          const iw = len(theme, has(p.eff, "itemW") ? p.eff.itemW : (d === "column" ? pw : 100), null);
          const ih = len(theme, has(p.eff, "itemH") ? p.eff.itemH : (d === "row" ? ph : 40), null);
          if (iw === null || ih === null) return null;
          pw = iw; ph = ih;
        }
      }
    }
    const sc = choiceMax(theme, n, "scale");
    if (sc === null) return null;
    let w = has(n.eff, "w") ? len(theme, n.eff.w, pw) : null;
    let h = has(n.eff, "h") ? len(theme, n.eff.h, ph) : null;
    if ((has(n.eff, "w") && w === null) || (has(n.eff, "h") && h === null)) return null;
    if ((Array.isArray(n.eff.states) ? n.eff.states : []).some((st) => isObj(st) && Object.keys(st).some((k2) => ["w", "h", "x", "right"].indexOf(k2) >= 0))) return null;
    const flowGroup = n.kind === "group" && (n.eff.layout === "row" || n.eff.layout === "column");
    const fills = ["rect", "group", "list"].indexOf(n.kind) >= 0 && !flowGroup;
    if (w === null) {
      if (has(n.eff, "x") && has(n.eff, "right")) {
        const x = len(theme, n.eff.x, pw), r = len(theme, n.eff.right, pw);
        if (x === null || r === null) return null;
        w = pw - x - r;
      } else if (fills) w = pw; else return null;
    }
    if (h === null) {
      if (has(n.eff, "y") && has(n.eff, "bottom")) {
        const y = len(theme, n.eff.y, ph), b = len(theme, n.eff.bottom, ph);
        if (y === null || b === null) return null;
        h = ph - y - b;
      } else if (fills) h = ph; else return null;
    }
    return [w, h, k * sc];
  }
  const needOf = (fit, sw, sh, tw, th) => (fit === "contain" ? Math.min(tw / sw, th / sh) : Math.max(tw / sw, th / sh));
  function capSize(f, sw, sh) {
    if (f >= 1) return [sw, sh];
    const gg = gcd(sw, sh), aw = sw / gg, ah = sh / gg;
    if (aw <= 64 && ah <= 64) {
      const m = Math.max(Math.ceil(sw * f / aw), 1);
      return aw * m < sw ? [aw * m, ah * m] : [sw, sh];
    }
    return [Math.max(1, Math.ceil(sw * f)), Math.max(1, Math.ceil(sh * f))];
  }
  function ruleImages(theme, nodes, live, hidden, files) {
    const uses = new Map(), refs = {};
    for (const n of live) {
      if (n.kind !== "image") continue;
      for (const k of ["src", "fallback"]) {
        const ref = n.eff[k], f = themeFile(files, ref);
        if (!f) continue;
        refs[f] = ref;
        const fit = has(n.eff, "fit") ? n.eff.fit : "cover", crop = n.eff.crop;
        const sk = stateKeys(n);
        const ok = isLiteral(fit) && ["cover", "contain", "fill", "coverTop"].indexOf(fit) >= 0 && !crop && !["src", "fallback", "fit", "crop"].some((x) => sk.has(x));
        const b = ok ? box(theme, n) : null;
        if (!uses.has(f)) uses.set(f, []);
        uses.get(f).push([n, k, fit, b]);
      }
    }
    const elsewhere = new Set();
    for (const [, v] of allProps(theme)) if (isStr(v) && !isLiteral(v)) {
      Object.keys(refs).forEach((f) => { if (v.indexOf(refs[f]) >= 0 || v.indexOf(f.split("/").pop()) >= 0) elsewhere.add(f); });
    }
    const out = [];
    uses.forEach((us, f) => {
      const size = files[f];
      if (!size || !size.w || !size.h) return;
      const sw = size.w, sh = size.h, rel = f;
      const known = !elsewhere.has(f) && us.every((u) => u[3] !== null);
      if (known) {
        const tws = us.map((u) => Math.ceil(u[3][0] * u[3][2] * CAP_SCALE)), ths = us.map((u) => Math.ceil(u[3][1] * u[3][2] * CAP_SCALE));
        const tw = Math.max.apply(null, tws), th = Math.max.apply(null, ths);
        const need = Math.max.apply(null, us.map((u, i) => needOf(u[2], sw, sh, tws[i], ths[i])));
        const [nw, nh] = capSize(need, sw, sh);
        if (nw * nh <= 0.75 * sw * sh) {
          const times = pyRound(sw * sh / (nw * nh));
          out.push(F("image-cap:" + rel, "image-cap", LOOK, "Shrink the picture " + rel + " from " + sw + "x" + sh + " to " + nw + "x" + nh,
            "The picture file is " + sw + "x" + sh + " but is never drawn bigger than " + Math.ceil(tw / CAP_SCALE * PLUS_SCALE) + "x" + Math.ceil(th / CAP_SCALE * PLUS_SCALE) +
            " (on the RG DS Plus). The engine reads and shrinks the whole file each time it needs the picture at a new size, so a file " + times + " times too big costs " + times +
            " times the time and memory. A copy at " + nw + "x" + nh + " (still twice what the RG DS shows) looks the same apart from rare one-level colour differences.",
            us[0][0].keyPath(us[0][1]), { saving: pyRound(100 * (1 - nw * nh / (sw * sh))) + "% less to decode", weight: sw * sh > 4 * nw * nh ? 2 : 1,
              data: { file: f, ref: refs[f], old: [sw, sh], new: [nw, nh] } }));
        }
      } else if (sw * sh > 1024 * 768 * 1.5) {
        out.push(F("image-big:" + rel, "image-big", ADVICE, "The picture " + rel + " is " + sw + "x" + sh,
          "It is bigger than either handheld's screen (1024x768 at most), and its drawn size can't be worked out from the theme (it depends on expressions), so it is left alone. " +
          "Saving it at the largest size it is shown at makes it load faster and take less memory.", us[0][0].keyPath(us[0][1]), { weight: 1, fixable: false }));
      }
    });
    return out;
  }

  // -- advice on work done while moving
  function drivers(theme, n, keys) {
    const out = {};
    keys.forEach((k) => {
      const vals = has(n.eff, k) ? [n.eff[k]] : [];
      (Array.isArray(n.eff.states) ? n.eff.states : []).forEach((st) => { if (isObj(st) && has(st, k)) vals.push(st[k]); });
      vals.forEach((v) => leaves(v, []).forEach(([, x]) => {
        const m = theme.motion(namesOr(x));
        if (m.size) out[k] = union(out[k], m);
      }));
    });
    return out;
  }
  function how(when) {
    if (when.has("time")) return "all the time";
    if (when.has("launch.t") || when.has("launch.progress")) return "during the launch";
    return subset(when, new Set(["scroll", "pos"])) ? "while the selection moves" : "after every move";
  }
  const shaped = (n) => (n.kind === "rect" || n.kind === "group") && ["radius", "gradient", "border", "shadow"].some((k) => has(n.eff, k)) &&
    (n.kind === "rect" || ["fill", "gradient", "border", "shadow"].some((k) => has(n.eff, k)));
  function ruleAnimatedWork(theme, nodes, live, hidden) {
    const out = [];
    for (const n of live) {
      const d = drivers(theme, n, union(IMAGE_SIZE_KEYS, SHAPE_KEYS, ["size"]));
      const dk = Object.keys(d);
      if (!dk.length) continue;
      const hw = how(union(...dk.map((k) => d[k]))), w = hw === "all the time" ? 3 : 2;
      const any = (ks) => ks.some((k) => has(d, k));
      if (n.kind === "image" && any(IMAGE_SIZE_KEYS)) {
        out.push(F("resize-image:" + pathStr(n.path), "resize-image", ADVICE, cap(n.label()) + " changes size " + hw,
          "Pictures are made at the exact size they are drawn. Each new size makes the engine read and resize the whole picture file again in the background, and until that is " +
          "done it stretches the last size it made, so it also looks blurry or wrong. Moving or fading it is much cheaper; scaling a group around it costs the same.",
          n.path, { weight: w, fixable: false }));
      } else if (shaped(n) && any(SHAPE_KEYS)) {
        const sh = isObj(n.eff.shadow) ? n.eff.shadow : {};
        const blur = Object.keys(sh).length ? len(theme, sh.blur, null) : 0;
        out.push(F("reshape:" + pathStr(n.path), "reshape", ADVICE, cap(n.label()) + " is repainted " + hw,
          "Its " + dk.slice().sort().join(", ") + " change" + (dk.length === 1 ? "s" : "") + " " + hw + ". Boxes with rounded corners, gradients, borders or shadows are " +
          "painted pixel by pixel on the processor and kept for reuse, but each new size, radius or colour is painted from scratch" +
          (blur && blur >= 20 ? " (its " + g(blur) + "-unit shadow blur adds a " + g(blur) + "-unit margin to paint all round)" : "") +
          ". Moving it is free; keeping its size and colours fixed (or using a plain box) avoids the work.", n.path, { weight: w, fixable: false }));
      } else if (n.kind === "text" && any(["size", "scale"])) {
        out.push(F("resize-text:" + pathStr(n.path), "resize-text", ADVICE, cap(n.label()) + " changes text size " + hw,
          "Text is rendered once per size and reused; a size that changes every frame renders the text again every frame. Moving or fading it is cheaper.", n.path, { weight: w, fixable: false }));
      }
    }
    for (const n of live) {
      if ((n.kind !== "group" && n.kind !== "list") || !has(n.eff, "scale")) continue;
      const d = drivers(theme, n, new Set(["scale"]));
      if (!Object.keys(d).length) continue;
      const inside = { image: 0, shape: 0, text: 0 }, stack = n.children.slice();
      while (stack.length) {
        const c = stack.pop();
        if (hidden.has(c)) continue;
        stack.push(...c.children);
        if (c.kind === "image") inside.image++; else if (c.kind === "text") inside.text++; else if (shaped(c)) inside.shape++;
      }
      if (!inside.image && !inside.shape && !inside.text) continue;
      const hw = how(d.scale);
      const parts = [inside.image ? inside.image + " picture" + (inside.image > 1 ? "s are" : " is") + " read and resized again" : "",
        inside.shape ? inside.shape + " rounded or shaded box" + (inside.shape > 1 ? "es" : "") + " repainted" : "",
        inside.text ? inside.text + " text" + (inside.text > 1 ? "s" : "") + " rendered again" : ""];
      out.push(F("scaled-group:" + pathStr(n.path), "scaled-group", ADVICE, cap(n.label()) + " grows and shrinks " + hw,
        'Its "scale" changes ' + hw + ", and everything in it is drawn at its new size: for each new size " + parts.filter(Boolean).join(", ") +
        ". A scale that only switches between two values (selected or not) costs this once per value; one that slides through many sizes costs it for every frame of the slide.",
        n.keyPath("scale"), { weight: hw === "all the time" ? 3 : 2, fixable: false }));
    }
    return out;
  }
  function ruleUnknownNames(theme) {
    const bad = new Map();
    for (const [p, v] of allProps(theme)) {
      const names = namesOf(v);
      if (!names) continue;
      names.forEach((m) => { if (!theme.known(m) && !bad.has(m)) bad.set(m, p); });
    }
    return [...bad.keys()].sort().map((m) => F("unknown-name:" + m, "unknown-name", ADVICE, 'Nothing is called "' + m + '"',
      '"' + m + '" (first at ' + pathStr(bad.get(m)) + ") is not a var, a hook or a component parameter, so it is always empty. A typing mistake?", bad.get(m), { weight: 0, fixable: false }));
  }
  function ruleMisc(theme) {
    const out = [], dbg = theme.doc.debug;
    if (truthy(literalValue(has(theme.doc, "debug") ? dbg : false)) && (typeof dbg === "boolean" || isNum(dbg)))
      out.push(F("debug", "debug", ADVICE, 'Turn "debug" off before sharing the theme', 'With "debug": true the theme\'s errors are drawn in red on the top screen.', ["debug"], { weight: 0, fixable: false }));
    if (theme.fps > 30)
      out.push(F("fps-high", "fps-high", LOOK, "Redraw moving things 30 times a second instead of " + trunc(theme.fps),
        'behavior.fps sets how often things that read "time" are redrawn. Above 30 the handheld spends twice the work on slow drifting motion for little visible gain.',
        ["behavior", "fps"], { saving: "half the frames while only the clock moves", weight: 2, data: { fps: 30 } }));
    return out;
  }

  // ---- applying -------------------------------------------------------------------------------------------------------
  const ORDER = [["hold-still", "flow-smooth", "lower-fps", "fps-high", "range"], ["inline-time-var"], ["unused-var", "settle"],
    ["fold", "dead-state", "noop"], ["hidden"], ["image-cap"]];
  // (the optimized document, the picture copies to make). The input is never changed. chosen: findings or ids; each
  // is looked up again in the document as it is when its turn comes, so stale ones are skipped
  function apply(doc, chosen, files) {
    files = files || {};
    const want = new Set((chosen || []).map((f) => (isStr(f) ? f : f.id)));
    const next = clone(doc), changes = [], done = new Set();
    for (const group of ORDER) {
      for (let round = 0; round < 32; round++) {
        let fresh = analyze(next, files).filter((f) => group.indexOf(f.rule) >= 0 && want.has(f.id) && f.fixable && !done.has(f.id));
        if (group[0] === "inline-time-var") fresh = fresh.slice(0, 1);
        if (!fresh.length) break;
        const removals = [];
        for (const f of fresh) { done.add(f.id); const r = fix(next, f, files, changes); if (r) removals.push(r); }
        removals.sort(comparePaths).reverse().forEach((p) => remove(next, p));
      }
    }
    return { doc: next, changes: changes };
  }
  function comparePaths(a, b) {			// as Python compares [(0, i, '') | (1, 0, key)] lists
    for (let i = 0; i < Math.min(a.length, b.length); i++) {
      const x = a[i], y = b[i], tx = isNum(x) ? 0 : 1, ty = isNum(y) ? 0 : 1;
      if (tx !== ty) return tx - ty;
      if (tx === 0) { if (x !== y) return x - y; }
      else { const sx = String(x), sy = String(y); if (sx !== sy) return sx < sy ? -1 : 1; }
    }
    return a.length - b.length;
  }
  function remove(doc, path) {
    const parent = getAt(doc, path.slice(0, -1)), last = path[path.length - 1];
    if (Array.isArray(parent)) parent.splice(last, 1);
    else if (isObj(parent)) {
      if ((path[0] === "screens" && path.length === 3) || last === "children") parent[last] = [];
      else delete parent[last];
    }
  }
  function foldIn(doc, path, theme) {
    const c = theme.const(getAt(doc, path));
    if (c !== NOTCONST) { const lit = literalFor(c); if (lit !== null) setAt(doc, path, lit); }
  }
  function fix(doc, f, files, changes) {
    const r = f.rule;
    if (r === "hold-still") { f.data.paths.forEach((p) => { setAt(doc, p, subst(getAt(doc, p), { time: "0" })); foldIn(doc, p, new Theme(doc)); }); return null; }
    if (r === "inline-time-var") {
      const theme = new Theme(doc), name = f.data.name, i = theme.varIndex.get(name), val = theme.vars[i][1];
      const repl = "(" + val.slice(1).trim() + ")";
      varRefs(theme, name).forEach((p) => { const m = {}; m[name] = repl; setAt(doc, p, subst(getAt(doc, p), m)); });
      return theme.vars[i][2];
    }
    if (r === "unused-var" || r === "dead-state" || r === "hidden") return f.path;
    if (r === "lower-fps" || r === "fps-high") { if (!isObj(doc.behavior)) doc.behavior = {}; doc.behavior.fps = f.data.fps; return null; }
    if (r === "settle") { if (!isObj(doc.behavior)) doc.behavior = {}; doc.behavior.settleMs = f.data.settleMs; return null; }
    if (r === "flow-smooth") {
      const sm = getAt(doc, f.path);
      if (isNum(sm)) { const o = {}; SIX.forEach((k) => { if (f.data.keys.indexOf(k) < 0) o[k] = sm; }); setAt(doc, f.path, o); }
      else if (isObj(sm)) f.data.keys.forEach((k) => delete sm[k]);
      return null;
    }
    if (r === "range") { setAt(doc, f.path, f.data.range); return null; }
    if (r === "fold") { setAt(doc, f.path, f.data.value); return null; }
    if (r === "noop") {
      const obj = getAt(doc, f.path);
      f.data.keys.forEach((k) => delete obj[k]);
      if (f.data.smooth.length && has(obj, "smooth")) {
        const sm = obj.smooth, keep = {};
        if (isNum(sm)) SIX.forEach((k) => { keep[k] = sm; }); else Object.assign(keep, sm);
        f.data.smooth.forEach((k) => delete keep[k]);
        if (Object.keys(keep).length) obj.smooth = keep; else delete obj.smooth;
      }
      return null;
    }
    if (r === "image-cap") {
      const src = f.data.file, [nw, nh] = f.data.new;
      const dot = src.lastIndexOf("."), slash = src.lastIndexOf("/");
      const dst = dot > slash ? src.slice(0, dot) + "." + nw + "x" + nh + src.slice(dot) : src + "." + nw + "x" + nh;
      for (const [p, v] of allProps(new Theme(doc))) if (v === f.data.ref && (p[p.length - 1] === "src" || p[p.length - 1] === "fallback")) setAt(doc, p, dst);
      changes.push({ src: src, dst: dst, oldSize: f.data.old.slice(), newSize: [nw, nh], ref: f.data.ref, newRef: dst, overwrites: has(files, dst) });
      return null;
    }
    return null;
  }
  function summary(findings) {
    const o = { safe: 0, look: 0, advice: 0 };
    findings.forEach((f) => { o[f.category]++; });
    return o;
  }

  const api = { analyze, apply, summary, pathStr, subst, CATEGORIES, SAFE, LOOK, ADVICE };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.BananaOptimize = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
