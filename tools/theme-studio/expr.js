// Banana Theme Studio: the layout-theme expression language, in JavaScript.
//
// A faithful port of banana-ui's ui/src/expr.cpp, so the editor can evaluate a theme's property values
// live, validate them as they are typed and show a colour swatch for any expression that resolves to a
// colour. The C++ file is the source of truth; tests/parity.cpp renders the same expressions through the
// real engine and tests/expr.test.js compares the two (see README.md, "Testing").
//
// Loaded as a classic script (so the editor works from file:// with no server) and require()-able from Node.
(function (global) {
  "use strict";

  // ---- values (the engine's EV): a JS number or string, with EV's coercion rules -------------------
  function evNum(v) {
    if (typeof v === "number") return v;
    const d = parseFloat(v);
    return Number.isNaN(d) ? 0 : d;			// strtod: no number -> 0
  }
  // C's "%g" with 6 significant digits (what EV::str prints for a non-integer number). printf rounds a
  // decimal tie to even; JS toPrecision rounds it away from zero, so the digits are rounded here by hand.
  function incDigits(d) {
    const a = d.split("");
    let i = a.length - 1;
    for (; i >= 0; i--) { if (a[i] === "9") a[i] = "0"; else { a[i] = String.fromCharCode(a[i].charCodeAt(0) + 1); break; } }
    if (i < 0) a.unshift("1");
    return a.join("");
  }
  function formatG(x, p) {
    p = p === undefined ? 6 : p;
    if (x === 0) return "0";
    if (!isFinite(x)) return String(x);
    const sign = x < 0 ? "-" : "";
    let s = Math.abs(x).toPrecision(17);				// exact-ish decimal, enough to round at p
    let mant = s, off = 0;
    const epos = s.indexOf("e");
    if (epos !== -1) { mant = s.slice(0, epos); off = parseInt(s.slice(epos + 1), 10); }
    const dot = mant.indexOf(".");
    let digits, pointPos;
    if (dot !== -1) { digits = mant.slice(0, dot) + mant.slice(dot + 1); pointPos = dot; }
    else { digits = mant; pointPos = mant.length; }
    let lead = 0;
    while (lead < digits.length - 1 && digits.charAt(lead) === "0") lead++;
    digits = digits.slice(lead); pointPos -= lead;
    let E = pointPos - 1 + off;
    let keep = digits.slice(0, p);
    const rest = digits.slice(p);
    let up = false;
    if (rest.length) {
      const first = rest.charCodeAt(0) - 48;
      if (first > 5) up = true;
      else if (first === 5) {
        if (/[1-9]/.test(rest.slice(1))) up = true;			// more than a half
        else up = ((keep.charCodeAt(keep.length - 1) - 48) % 2) === 1;	// exact tie: to even
      }
    }
    if (up) { keep = incDigits(keep); if (keep.length > p) { keep = keep.slice(0, p); E++; } }
    if (E < -4 || E >= p) {
      let mantissa = keep.charAt(0);
      const frac = keep.slice(1).replace(/0+$/, "");
      if (frac) mantissa += "." + frac;
      return sign + mantissa + "e" + (E < 0 ? "-" : "+") + String(Math.abs(E)).padStart(2, "0");
    }
    let out;
    if (E + 1 <= 0) out = "0." + "0".repeat(-E - 1) + keep;
    else if (E + 1 >= keep.length) out = keep + "0".repeat(E + 1 - keep.length);
    else out = keep.slice(0, E + 1) + "." + keep.slice(E + 1);
    if (out.indexOf(".") !== -1) out = out.replace(/0+$/, "").replace(/\.$/, "");
    return sign + out;
  }
  function evStr(v) {
    if (typeof v === "string") return v;
    if (typeof v !== "number" || Number.isNaN(v)) return "";
    if (Number.isInteger(v) && Math.abs(v) < 1e15) return String(v);
    return formatG(v, 6);
  }
  function evTruthy(v) {
    if (typeof v === "string") return v !== "" && v !== "0" && v !== "false";
    return v !== 0;
  }

  // ---- colours (0xAARRGGBB as an unsigned 32-bit number) ------------------------------------------
  const COLOR_NAMES = { black: 0xff000000, white: 0xffffffff, red: 0xffff0000, green: 0xff00c000,
    blue: 0xff0000ff, yellow: 0xffffff00, gray: 0xff808080, grey: 0xff808080 };
  // "#rgb", "#rgba", "#rrggbb", "#rrggbbaa", "rgb(...)", "rgba(...)", "transparent"/"none",
  // or a few names -> 0xAARRGGBB, or null when it isn't a colour.
  function parseColorText(v0) {
    let v = v0 === null || v0 === undefined ? "" : String(v0);
    v = v.replace(/^\s+/, "").replace(/\s+$/, "");
    if (v === "") return null;
    if (v === "transparent" || v === "none") return 0;
    if (Object.prototype.hasOwnProperty.call(COLOR_NAMES, v)) return COLOR_NAMES[v] >>> 0;
    if (v.charAt(0) === "#") {
      let h = v.slice(1);
      if (!/^[0-9a-fA-F]*$/.test(h)) return null;
      if (h.length === 3 || h.length === 4) { let e = ""; for (const c of h) e += c + c; h = e; }
      if (h.length === 6) h += "ff";
      if (h.length !== 8) return null;
      const rgba = parseInt(h, 16) >>> 0;
      return (((rgba >>> 8) | ((rgba & 0xff) << 24)) >>> 0);
    }
    if (v.startsWith("rgb(") || v.startsWith("rgba(")) {
      const raw = v.slice(v.indexOf("(") + 1).split(",");
      const nums = raw.map((s) => parseFloat(s));
      if (nums.length < 3 || Number.isNaN(nums[0]) || Number.isNaN(nums[1]) || Number.isNaN(nums[2])) return null;
      const alpha = nums.length > 3 && !Number.isNaN(nums[3]) ? nums[3] : 1;
      const b = (x) => Math.min(255, Math.max(0, Math.floor(x + 0.5)));
      return ((b(alpha * 255) << 24) | (b(nums[0]) << 16) | (b(nums[1]) << 8) | b(nums[2])) >>> 0;
    }
    return null;
  }
  function colorText(c) {
    c = c >>> 0;
    return "#" + (c & 0xffffff).toString(16).padStart(6, "0") + ((c >>> 24) & 0xff).toString(16).padStart(2, "0");
  }
  function colorOf(v) {
    if (typeof v === "string") { const p = parseColorText(v); return p === null ? 0xff000000 : p; }
    return ((0xff000000 | (Math.trunc(evNum(v)) >>> 0)) >>> 0);
  }
  function mixColor(a, b, t) {
    t = Math.min(1, Math.max(0, t));
    const ch = (sh) => { const x = (a >>> sh) & 255, y = (b >>> sh) & 255; return (Math.round(x + (y - x) * t) << sh) >>> 0; };
    return ((ch(24) | ch(16) | ch(8) | ch(0)) >>> 0);
  }

  // ---- scopes -------------------------------------------------------------------------------------
  class Scope {
    constructor(parent) { this.parent = parent || null; this.vars = []; this.resolve = null; }
    set(k, v) { for (const e of this.vars) if (e[0] === k) { e[1] = v; return; } this.vars.push([k, v]); }
    lookup(name) {
      for (let s = this; s; s = s.parent) {
        for (const e of s.vars) if (e[0] === name) return { found: true, value: e[1] };
        if (s.resolve) { const r = s.resolve(name); if (r && r.found) return r; }
      }
      return { found: false, value: "" };
    }
  }

  // ---- the parser (a recursive descent over the same grammar as expr.cpp) --------------------------
  class Parser {
    constructor(s) { this.s = s; this.i = 0; this.err = ""; }
    ws() { while (this.i < this.s.length && /\s/.test(this.s[this.i])) this.i++; }
    eat(t) { this.ws(); if (this.s.startsWith(t, this.i)) { this.i += t.length; return true; } return false; }
    fail(m) { if (!this.err) this.err = m + " at " + this.i; return null; }
    node(op, a) { return { op: op, a: a }; }

    ternary() {
      const c = this.orExpr();
      if (!c) return null;
      if (!this.eat("?")) return c;
      const a = this.ternary();
      if (!a) return null;
      if (!this.eat(":")) return this.fail("':' expected");
      const b = this.ternary();
      if (!b) return null;
      return this.node("tern", [c, a, b]);
    }
    orExpr() {
      let l = this.andExpr();
      while (l && this.eat("||")) { const r = this.andExpr(); if (!r) return null; l = this.node("or", [l, r]); }
      return l;
    }
    andExpr() {
      let l = this.cmp();
      while (l && this.eat("&&")) { const r = this.cmp(); if (!r) return null; l = this.node("and", [l, r]); }
      return l;
    }
    cmp() {
      let l = this.add();
      for (;;) {
        if (!l) return null;
        let op;
        if (this.eat("==")) op = "eq";
        else if (this.eat("!=")) op = "ne";
        else if (this.eat("<=")) op = "le";
        else if (this.eat(">=")) op = "ge";
        else if (this.eat("<")) op = "lt";
        else if (this.eat(">")) op = "gt";
        else return l;
        const r = this.add();
        if (!r) return null;
        l = this.node(op, [l, r]);
      }
    }
    add() {
      let l = this.mul();
      for (;;) {
        if (!l) return null;
        let op;
        if (this.eat("+")) op = "add";
        else if (this.eat("-")) op = "sub";
        else return l;
        const r = this.mul();
        if (!r) return null;
        l = this.node(op, [l, r]);
      }
    }
    mul() {
      let l = this.unary();
      for (;;) {
        if (!l) return null;
        let op;
        if (this.eat("*")) op = "mul";
        else if (this.eat("/")) op = "div";
        else if (this.eat("%")) op = "mod";
        else return l;
        const r = this.unary();
        if (!r) return null;
        l = this.node(op, [l, r]);
      }
    }
    unary() {
      if (this.eat("-")) { const e = this.unary(); return e ? this.node("neg", [e]) : null; }
      if (this.eat("!")) { const e = this.unary(); return e ? this.node("not", [e]) : null; }
      if (this.eat("+")) return this.unary();
      return this.primary();
    }
    primary() {
      this.ws();
      if (this.i >= this.s.length) return this.fail("a value expected");
      const c = this.s[this.i];
      if (c === "(") {
        this.i++;
        const e = this.ternary();
        if (!e) return null;
        if (!this.eat(")")) return this.fail("')' expected");
        return e;
      }
      if (c === "'" || c === '"') {
        let v = "", j = this.i + 1;
        for (; j < this.s.length && this.s[j] !== c; j++) {
          if (this.s[j] === "\\" && j + 1 < this.s.length) { j++; v += this.s[j] === "n" ? "\n" : this.s[j]; }
          else v += this.s[j];
        }
        if (j >= this.s.length) return this.fail("a string that doesn't end");
        this.i = j + 1;
        return { op: "lit", lit: v };
      }
      if (c === "#") {					// a colour without quotes
        const b = this.i++;
        while (this.i < this.s.length && /[0-9a-fA-F]/.test(this.s[this.i])) this.i++;
        return { op: "lit", lit: this.s.slice(b, this.i) };
      }
      if (/[0-9.]/.test(c)) {
        const m = /^(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?/.exec(this.s.slice(this.i));
        if (!m) return this.fail("a number expected");
        this.i += m[0].length;
        return { op: "lit", lit: parseFloat(m[0]) };
      }
      if (/[A-Za-z_$]/.test(c)) {
        const b = this.i;
        while (this.i < this.s.length && /[A-Za-z0-9_$.]/.test(this.s[this.i])) this.i++;
        const id = this.s.slice(b, this.i);
        if (id === "true" || id === "false") return { op: "lit", lit: id === "true" ? 1 : 0 };
        if (id === "null") return { op: "lit", lit: "" };
        if (this.eat("(")) {
          const n = { op: "call", name: id, a: [] };
          if (!this.eat(")")) for (;;) {
            const a = this.ternary();
            if (!a) return null;
            n.a.push(a);
            if (this.eat(")")) break;
            if (!this.eat(",")) return this.fail("',' or ')' expected");
          }
          return n;
        }
        return { op: "var", name: id.charAt(0) === "$" ? id.slice(1) : id };
      }
      return this.fail("unexpected '" + c + "'");
    }
  }

  // "=..." text -> { expr, error }; expr is null (and error says why) when it doesn't parse.
  function compileExpr(text) {
    const s = text === undefined || text === null ? "" : String(text);
    const p = new Parser(s);
    let e = p.ternary();
    if (e) { p.ws(); if (p.i < s.length) e = p.fail("more after the end"); }
    return { expr: e, error: e ? "" : p.err + ' in "' + s + '"' };
  }

  // ---- evaluation ---------------------------------------------------------------------------------
  const PI = 3.14159265358979323846;
  function clamp01(t) { return Math.min(1, Math.max(0, t)); }

  function evalExpr(e, sc) {
    if (!e) return 0;
    switch (e.op) {
      case "lit": return e.lit;
      case "var": { const r = sc.lookup(e.name); return r.found ? r.value : ""; }
      case "neg": return -evNum(evalExpr(e.a[0], sc));
      case "not": return evTruthy(evalExpr(e.a[0], sc)) ? 0 : 1;
      case "and": return (evTruthy(evalExpr(e.a[0], sc)) && evTruthy(evalExpr(e.a[1], sc))) ? 1 : 0;
      case "or": { const a = evalExpr(e.a[0], sc); return evTruthy(a) ? a : evalExpr(e.a[1], sc); }
      case "tern": return evTruthy(evalExpr(e.a[0], sc)) ? evalExpr(e.a[1], sc) : evalExpr(e.a[2], sc);
      case "call": return callFn(e, sc);
    }
    const a = evalExpr(e.a[0], sc), b = evalExpr(e.a[1], sc);
    switch (e.op) {
      case "add": return (typeof a === "string" || typeof b === "string") ? evStr(a) + evStr(b) : evNum(a) + evNum(b);
      case "sub": return evNum(a) - evNum(b);
      case "mul": return evNum(a) * evNum(b);
      case "div": return evNum(b) === 0 ? 0 : evNum(a) / evNum(b);
      case "mod": { const d = evNum(b); return d === 0 ? 0 : evNum(a) % d; }
      case "eq": return ((typeof a === "string" || typeof b === "string") ? evStr(a) === evStr(b) : evNum(a) === evNum(b)) ? 1 : 0;
      case "ne": return ((typeof a === "string" || typeof b === "string") ? evStr(a) !== evStr(b) : evNum(a) !== evNum(b)) ? 1 : 0;
      case "lt": return evNum(a) < evNum(b) ? 1 : 0;
      case "le": return evNum(a) <= evNum(b) ? 1 : 0;
      case "gt": return evNum(a) > evNum(b) ? 1 : 0;
      case "ge": return evNum(a) >= evNum(b) ? 1 : 0;
      default: return 0;
    }
  }

  function callFn(n, sc) {
    const f = n.name;
    if (f === "if")	// only works out the side it takes
      return n.a.length >= 2 ? (evTruthy(evalExpr(n.a[0], sc)) ? evalExpr(n.a[1], sc) : n.a.length > 2 ? evalExpr(n.a[2], sc) : 0) : 0;
    const v = n.a.map((a) => evalExpr(a, sc));
    const N = (k, def) => (k < v.length ? evNum(v[k]) : (def === undefined ? 0 : def));
    const S = (k) => (k < v.length ? evStr(v[k]) : "");
    const A = (k) => (k < v.length ? v[k] : 0);

    if (f === "min") { let r = N(0); for (let k = 1; k < v.length; k++) r = Math.min(r, N(k)); return r; }
    if (f === "max") { let r = N(0); for (let k = 1; k < v.length; k++) r = Math.max(r, N(k)); return r; }
    if (f === "clamp") return Math.min(N(2, 1), Math.max(N(1), N(0)));
    if (f === "abs") return Math.abs(N(0));
    if (f === "floor") return Math.floor(N(0));
    if (f === "ceil") return Math.ceil(N(0));
    if (f === "round") { const m = Math.pow(10, N(1)); return Math.round(N(0) * m) / m; }
    if (f === "sqrt") return Math.sqrt(Math.max(0, N(0)));
    if (f === "pow") return Math.pow(N(0), N(1));
    if (f === "sign") return (N(0) > 0 ? 1 : 0) - (N(0) < 0 ? 1 : 0);
    if (f === "mod") { const b = N(1, 1); if (b === 0) return 0; const r = N(0) % b; return r < 0 ? r + b : r; }
    if (f === "sin") return Math.sin(N(0));
    if (f === "cos") return Math.cos(N(0));
    if (f === "sind") return Math.sin(N(0) * PI / 180);
    if (f === "cosd") return Math.cos(N(0) * PI / 180);
    if (f === "lerp") return N(0) + (N(1) - N(0)) * N(2);
    if (f === "step") return N(1) >= N(0) ? 1 : 0;
    if (f === "smoothstep") { const d = (N(1) - N(0)) === 0 ? 1 : N(1) - N(0); const t = clamp01((N(2) - N(0)) / d); return t * t * (3 - 2 * t); }
    if (f === "progress") return clamp01(N(0) / Math.max(1, N(1, 1)));
    if (f === "easeIn") { const t = clamp01(N(0)); return t * t * t; }
    if (f === "easeOut") { const t = 1 - clamp01(N(0)); return 1 - t * t * t; }
    if (f === "easeInOut") { const t = clamp01(N(0)); return t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; }
    if (f === "easeOutBack") { const t = clamp01(N(0)) - 1, c = N(1, 1.70158); return 1 + (c + 1) * t * t * t + c * t * t; }
    if (f === "easeOutElastic") {
      const t = clamp01(N(0));
      return t === 0 || t === 1 ? t : Math.pow(2, -10 * t) * Math.sin((t * 10 - 0.75) * (2 * PI / 3)) + 1;
    }
    if (f === "bounce") {
      let t = clamp01(N(0)); const k = 7.5625, d = 2.75;
      if (t < 1 / d) return k * t * t;
      if (t < 2 / d) { t -= 1.5 / d; return k * t * t + 0.75; }
      if (t < 2.5 / d) { t -= 2.25 / d; return k * t * t + 0.9375; }
      t -= 2.625 / d; return k * t * t + 0.984375;
    }
    if (f === "pulse") return 0.5 - 0.5 * Math.cos(N(0) * 2 * PI / Math.max(1, N(1, 1000)));
    if (f === "mix") return colorText(mixColor(colorOf(A(0)), colorOf(A(1)), N(2, 0.5)));
    if (f === "alpha") { const c = colorOf(A(0)); return colorText((((c & 0xffffff) | ((Math.round(clamp01(N(1, 1)) * ((c >>> 24) & 255)) << 24)) >>> 0) >>> 0)); }
    if (f === "lighten") { const c = colorOf(A(0)); return colorText(mixColor(c, (((c & 0xff000000) | 0xffffff) >>> 0), N(1, 0.2))); }
    if (f === "darken") { const c = colorOf(A(0)); return colorText(mixColor(c, ((c & 0xff000000) >>> 0), N(1, 0.2))); }
    if (f === "rgb" || f === "rgba") {
      const b = (x) => Math.min(255, Math.max(0, Math.floor(x + 0.5)));
      return colorText(((b(N(3, 1) * 255) << 24) | (b(N(0)) << 16) | (b(N(1)) << 8) | b(N(2))) >>> 0);
    }
    if (f === "hsl") {
      let h = (N(0) % 360) / 60, s = clamp01(N(1)), l = clamp01(N(2)), c = (1 - Math.abs(2 * l - 1)) * s;
      if (h < 0) h += 6;
      const x = c * (1 - Math.abs((h % 2) - 1)), m = l - c / 2;
      let r = 0, g = 0, bb = 0; const k = Math.trunc(h);
      if (k === 0) { r = c; g = x; } else if (k === 1) { r = x; g = c; } else if (k === 2) { g = c; bb = x; }
      else if (k === 3) { g = x; bb = c; } else if (k === 4) { r = x; bb = c; } else { r = c; bb = x; }
      const b = (y) => Math.min(255, Math.max(0, Math.floor(y * 255 + 0.5)));
      return colorText(((b(N(3, 1)) << 24) | (b(r + m) << 16) | (b(g + m) << 8) | b(bb + m)) >>> 0);
    }
    if (f === "upper") return S(0).toUpperCase();
    if (f === "lower") return S(0).toLowerCase();
    if (f === "len") return Array.from(S(0)).length;
    if (f === "substr") {
      const s = Array.from(S(0)), from = Math.trunc(N(1)), n = v.length > 2 ? Math.trunc(N(2)) : (1 << 30);
      let r = "";
      for (let k = 0; k < s.length; k++) if (k >= from && k < from + n) r += s[k];
      return r;
    }
    if (f === "contains") return S(0).includes(S(1)) ? 1 : 0;
    if (f === "replace") {
      let s = S(0); const a = S(1), b = S(2);
      if (a !== "") for (let at = 0; (at = s.indexOf(a, at)) !== -1; at += (b.length || a.length)) s = s.slice(0, at) + b + s.slice(at + a.length);
      return s;
    }
    if (f === "pad") { let s = S(0); const w = Math.trunc(N(1)), ch = v.length > 2 ? S(2) : "0"; while (s.length < w) s = ch + s; return s; }
    if (f === "fixed") { const digits = Math.max(0, Math.min(10, Math.trunc(N(1)))); return N(0).toFixed(digits); }
    if (f === "str") return S(0);
    if (f === "num") return N(0);
    if (f === "plural") return evStr(N(0)) + " " + (N(0) === 1 ? S(1) : (v.length > 2 ? S(2) : S(1) + "s"));
    if (f === "pick") { if (v.length < 2) return ""; const k = Math.max(0, Math.min(v.length - 2, Math.trunc(N(0)))); return v[k + 1]; }
    if (f === "either") { for (const e of v) if (evTruthy(e)) return e; return v.length ? v[v.length - 1] : ""; }
    return 0;
  }

  // ---- a property value: a literal, an "=expression" or a "{template}" -----------------------------
  function parseProp(t) {
    t = t === null || t === undefined ? "" : String(t);
    const p = { kind: "literal", lit: t, expr: null, parts: null, error: "" };
    if (t.length && t.charAt(0) === "=") { p.kind = "expr"; const r = compileExpr(t.slice(1)); p.expr = r.expr; p.error = r.error; return p; }
    if (t.indexOf("{") < 0) { p.lit = t; return p; }
    p.kind = "template"; p.parts = []; let text = "";
    for (let i = 0; i < t.length; i++) {
      if (t.charAt(i) === "{" && t.charAt(i + 1) === "{") { text += "{"; i++; continue; }
      if (t.charAt(i) === "}" && t.charAt(i + 1) === "}") { text += "}"; i++; continue; }
      if (t.charAt(i) !== "{") { text += t.charAt(i); continue; }
      const e = t.indexOf("}", i);
      if (e < 0) { text += t.slice(i); break; }
      const r = compileExpr(t.slice(i + 1, e));
      if (!r.expr && !p.error) p.error = r.error;
      p.parts.push([text, r.expr]);
      text = ""; i = e;
    }
    p.parts.push([text, null]);
    return p;
  }
  function evalProp(p, sc) {
    if (p.kind === "literal") return p.lit;
    if (p.kind === "expr") return evalExpr(p.expr, sc);
    let r = "";
    for (const part of p.parts) { r += part[0]; if (part[1]) r += evStr(evalExpr(part[1], sc)); }
    return r;
  }
  function propNumber(v) { return { kind: "literal", lit: v, expr: null, parts: null, error: "" }; }

  // The function names, for autocomplete (kept in step with callFn above and with docs/THEMES.md).
  const FUNCTIONS = ["min", "max", "clamp", "abs", "floor", "ceil", "round", "sqrt", "pow", "sign", "mod",
    "sin", "cos", "sind", "cosd", "lerp", "step", "smoothstep", "progress", "easeIn", "easeOut", "easeInOut",
    "easeOutBack", "easeOutElastic", "bounce", "pulse", "mix", "alpha", "lighten", "darken", "rgb", "rgba",
    "hsl", "upper", "lower", "len", "substr", "contains", "replace", "pad", "fixed", "str", "num", "plural",
    "pick", "either", "if"];

  const api = { compileExpr, evalExpr, Scope, parseProp, evalProp, propNumber, parseColorText, colorText,
    colorOf, mixColor, evNum, evStr, evTruthy, formatG, FUNCTIONS };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.BananaExpr = api;
})(typeof globalThis !== "undefined" ? globalThis : this);