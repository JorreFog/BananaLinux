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

  // ---- the C library, as expr.cpp sees it ---------------------------------------------------------
  // The engine's strings are UTF-8 bytes and its character classes are the C locale's (ASCII only); its numbers
  // come from strtod and go out through printf; std::min/max, std::round and pow have C's rules for NaN and
  // halves. Each helper here is the C call it is named after.
  const isSpaceC = (ch) => ch === " " || ch === "\t" || ch === "\n" || ch === "\v" || ch === "\f" || ch === "\r";
  const isDigitC = (ch) => ch >= "0" && ch <= "9";
  const isXDigitC = (ch) => /^[0-9a-fA-F]$/.test(ch);
  // std::min / std::max: (b < a) ? b : a and (a < b) ? b : a, so a NaN on one side is not always the answer
  const cmin = (a, b) => (b < a ? b : a);
  const cmax = (a, b) => (a < b ? b : a);
  // std::round: halves away from zero
  const cround = (x) => (x < 0 ? -Math.round(-x) : Math.round(x));
  // C pow: pow(1, anything) and pow(-1, ±inf) are 1 (JS says NaN)
  const cpow = (a, b) => (a === 1 || (a === -1 && (b === Infinity || b === -Infinity)) ? 1 : Math.pow(a, b));
  // a double to int / uint32_t: undefined in C when it doesn't fit; the handhelds (aarch64) saturate, NaN is 0
  const toInt = (x) => (x !== x ? 0 : x >= 2147483647 ? 2147483647 : x <= -2147483648 ? -2147483648 : Math.trunc(x));
  const toUint = (x) => (x !== x || x <= 0 ? 0 : x >= 4294967295 ? 4294967295 : Math.trunc(x));
  // a string's length in UTF-8 bytes (what std::string::size() counts)
  function utf8Len(s) {
    let n = 0;
    for (const ch of s) { const c = ch.codePointAt(0); n += c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4; }
    return n;
  }
  // toupper / tolower in the C locale: a-z only
  const upperC = (s) => s.replace(/[a-z]+/g, (m) => m.toUpperCase());
  const lowerC = (s) => s.replace(/[A-Z]+/g, (m) => m.toLowerCase());

  // strtod from s[i]: { value, end } (end === i when there is no number). Leading C whitespace, a sign, decimal
  // with an optional exponent, hex ("0x1.8p3"), "inf"/"infinity" and "nan" / "nan(...)", any case.
  function strtod(s, i) {
    const start = i || 0;
    let j = start;
    while (j < s.length && isSpaceC(s[j])) j++;
    let neg = false;
    if (s[j] === "+" || s[j] === "-") { neg = s[j] === "-"; j++; }
    const rest = s.slice(j, j + 8).toLowerCase();
    if (rest.startsWith("inf")) return { value: neg ? -Infinity : Infinity, end: j + (rest.startsWith("infinity") ? 8 : 3) };
    if (rest.startsWith("nan")) {
      let e = j + 3;
      if (s[e] === "(") { let k = e + 1; while (k < s.length && /[0-9A-Za-z_]/.test(s[k])) k++; if (s[k] === ")") e = k + 1; }
      return { value: NaN, end: e };
    }
    if (s[j] === "0" && (s[j + 1] === "x" || s[j + 1] === "X")) {
      let k = j + 2, mant = "", frac = 0, any = false;
      while (k < s.length && isXDigitC(s[k])) { mant += s[k++]; any = true; }
      if (s[k] === ".") { k++; while (k < s.length && isXDigitC(s[k])) { mant += s[k++]; frac++; any = true; } }
      if (any) {
        let exp = 0;
        if (s[k] === "p" || s[k] === "P") {
          let q = k + 1, eneg = false;
          if (s[q] === "+" || s[q] === "-") { eneg = s[q] === "-"; q++; }
          if (isDigitC(s[q] || "")) { let ev = 0; while (q < s.length && isDigitC(s[q])) { ev = Math.min(1e6, ev * 10 + (s.charCodeAt(q) - 48)); q++; } exp = eneg ? -ev : ev; k = q; }
        }
        // the hex digits exactly, then scaled by two (in steps, so a huge or tiny scale doesn't overflow early)
        let v = Number(BigInt("0x" + mant)), e2 = exp - 4 * frac;
        while (e2 > 0 && v !== Infinity) { const st = Math.min(e2, 1000); v *= Math.pow(2, st); e2 -= st; }
        while (e2 < 0 && v !== 0) { const st = Math.max(e2, -1000); v *= Math.pow(2, st); e2 -= st; }
        return { value: neg ? -v : v, end: k };
      }
      return { value: neg ? -0 : 0, end: j + 1 };			// "0x" with no digits: the "0"
    }
    const m = /^(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?/.exec(s.slice(j));
    if (!m) return { value: 0, end: start };
    const v = Number(m[0]);					// correctly rounded, as glibc's strtod
    return { value: neg ? -v : v, end: j + m[0].length };
  }

  // a finite double's exact decimal value: { digits, point } with value = 0.digits * 10^point (digits has no
  // leading zeros; "" for zero). Every double is a finite binary fraction, so this is exact (BigInt).
  function exactDecimal(x) {
    x = Math.abs(x);
    if (x === 0) return { digits: "", point: 0 };
    const dv = new DataView(new ArrayBuffer(8)); dv.setFloat64(0, x);
    const hi = dv.getUint32(0), lo = dv.getUint32(4), be = (hi >>> 20) & 0x7ff;
    let mant = (BigInt(hi & 0xfffff) << 32n) | BigInt(lo), e2;
    if (be === 0) e2 = -1074; else { mant |= 1n << 52n; e2 = be - 1075; }
    let digits, frac;
    if (e2 >= 0) { digits = (mant << BigInt(e2)).toString(); frac = 0; }
    else { digits = (mant * 5n ** BigInt(-e2)).toString(); frac = -e2; }
    const lead = digits.match(/^0*/)[0].length;
    return { digits: digits.slice(lead).replace(/0+$/, ""), point: digits.length - frac - lead };
  }
  // round the digit string to keep `n` digits, ties to even (printf's rounding of an exact value): new digits
  // (length n, or n + 1 when it carried) and whether it carried
  function roundDigits(digits, n) {
    if (n < 0) return { digits: "", carry: false };
    if (digits.length <= n) return { digits: digits + "0".repeat(n - digits.length), carry: false };
    const keep = digits.slice(0, n), rest = digits.slice(n);
    const first = rest.charCodeAt(0) - 48;
    // more than a half, or exactly a half and the kept digit odd (nothing kept: 0, even)
    const up = first > 5 || (first === 5 && (/[1-9]/.test(rest.slice(1)) || (n > 0 && (keep.charCodeAt(n - 1) - 48) % 2 === 1)));
    if (!up) return { digits: keep, carry: false };
    const a = keep.split("");
    let i = a.length - 1;
    for (; i >= 0; i--) { if (a[i] === "9") a[i] = "0"; else { a[i] = String.fromCharCode(a[i].charCodeAt(0) + 1); break; } }
    if (i < 0) { a.unshift("1"); return { digits: a.join(""), carry: true }; }
    return { digits: a.join(""), carry: false };
  }
  const nanText = (x) => (x !== x ? "-nan" : x < 0 ? "-inf" : "inf");	// (an expression's NaN is the FPU's: negative)
  const negative = (x) => x < 0 || (x === 0 && 1 / x < 0);
  // printf("%.*f", prec, x)
  function formatF(x, prec) {
    if (!isFinite(x)) return nanText(x);
    const d = exactDecimal(x), ip = Math.max(0, d.point);
    const r = roundDigits((d.point < 0 ? "0".repeat(-d.point) : "") + d.digits, ip + prec);
    const n = ip + (r.carry ? 1 : 0);
    return (negative(x) ? "-" : "") + (r.digits.slice(0, n) || "0") + (prec > 0 ? "." + r.digits.slice(n) : "");
  }
  // C's "%g" with 6 significant digits (what EV::str prints for a number that isn't whole)
  function formatG(x, p) {
    p = p === undefined ? 6 : p;
    if (!isFinite(x)) return nanText(x);
    if (x === 0) return negative(x) ? "-0" : "0";
    const sign = x < 0 ? "-" : "";
    const d = exactDecimal(x);
    const r = roundDigits(d.digits, p);
    let keep = r.digits, E = d.point - 1;
    if (r.carry) { keep = keep.slice(0, p); E++; }
    if (E < -4 || E >= p) {
      let mantissa = keep.charAt(0);
      const frac = keep.slice(1).replace(/0+$/, "");
      if (frac) mantissa += "." + frac;
      return sign + mantissa + "e" + (E < 0 ? "-" : "+") + String(Math.abs(E)).padStart(2, "0");
    }
    let out;
    if (E < 0) out = "0." + "0".repeat(-E - 1) + keep;
    else if (E + 1 >= keep.length) out = keep + "0".repeat(E + 1 - keep.length);
    else out = keep.slice(0, E + 1) + "." + keep.slice(E + 1);
    if (out.indexOf(".") !== -1) out = out.replace(/0+$/, "").replace(/\.$/, "");
    return sign + out;
  }

  // ---- values (the engine's EV): a JS number or string, with EV's coercion rules -------------------
  function evNum(v) {
    if (typeof v === "number") return v;
    const s = String(v), r = strtod(s, 0);
    return r.end !== 0 ? r.value : 0;			// strtod: no number -> 0
  }
  function evStr(v) {
    if (typeof v === "string") return v;
    if (typeof v !== "number" || Number.isNaN(v)) return "";
    if (Number.isInteger(v) && Math.abs(v) < 1e15) return String(v);	// "%lld" ("0" for -0 too)
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
    let b0 = 0, e0 = v.length;					// (C's isspace: ASCII only)
    while (b0 < e0 && isSpaceC(v[b0])) b0++;
    while (e0 > b0 && isSpaceC(v[e0 - 1])) e0--;
    v = v.slice(b0, e0);
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
      // sscanf(v + "(", "%f , %f , %f , %f"): floats, each after optional spaces and a comma; at least three
      const c = [0, 0, 0, 1], f = Math.fround;
      let i = v.indexOf("(") + 1, n = 0;
      for (; n < 4; n++) {
        if (n > 0) { while (i < v.length && isSpaceC(v[i])) i++; if (v[i] !== ",") break; i++; }
        const r = strtod(v, i);
        if (r.end === i) break;
        c[n] = f(r.value); i = r.end;
      }
      if (n < 3) return null;
      const b = (x) => toUint(cmin(255, cmax(0, f(x + 0.5))));
      return ((b(f(c[3] * 255)) << 24) | (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2])) >>> 0;
    }
    return null;
  }
  function colorText(c) {
    c = c >>> 0;
    return "#" + (c & 0xffffff).toString(16).padStart(6, "0") + ((c >>> 24) & 0xff).toString(16).padStart(2, "0");
  }
  function colorOf(v) {
    if (typeof v === "string") { const p = parseColorText(v); return p === null ? 0xff000000 : p; }
    return ((0xff000000 | toUint(evNum(v))) >>> 0);
  }
  function mixColor(a, b, t) {
    t = cmin(1, cmax(0, t));
    const ch = (sh) => { const x = (a >>> sh) & 255, y = (b >>> sh) & 255; return (cround(x + (y - x) * t) << sh) >>> 0; };	// lround
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
    ws() { while (this.i < this.s.length && isSpaceC(this.s[this.i])) this.i++; }
    eat(t) { this.ws(); if (this.s.startsWith(t, this.i)) { this.i += t.length; return true; } return false; }
    fail(m) { if (!this.err) this.err = m + " at " + utf8Len(this.s.slice(0, this.i)); return null; }	// (a byte offset, as the engine's)
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
      if (isDigitC(c) || c === ".") {				// strtod: 0x10, 1e3, .5 (nothing taken: a 0, then an error)
        const r = strtod(this.s, this.i);
        this.i = r.end;
        return { op: "lit", lit: r.value };
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
  function clamp01(t) { return cmin(1, cmax(0, t)); }

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

    if (f === "min") { let r = N(0); for (let k = 1; k < v.length; k++) r = cmin(r, N(k)); return r; }
    if (f === "max") { let r = N(0); for (let k = 1; k < v.length; k++) r = cmax(r, N(k)); return r; }
    if (f === "clamp") return cmin(N(2, 1), cmax(N(1), N(0)));
    if (f === "abs") return Math.abs(N(0));
    if (f === "floor") return Math.floor(N(0));
    if (f === "ceil") return Math.ceil(N(0));
    if (f === "round") { const m = cpow(10, N(1)); return cround(N(0) * m) / m; }
    if (f === "sqrt") return Math.sqrt(cmax(0, N(0)));
    if (f === "pow") return cpow(N(0), N(1));
    if (f === "sign") return (N(0) > 0 ? 1 : 0) - (N(0) < 0 ? 1 : 0);
    if (f === "mod") { const b = N(1, 1); if (b === 0) return 0; const r = N(0) % b; return r < 0 ? r + b : r; }
    if (f === "sin") return Math.sin(N(0));
    if (f === "cos") return Math.cos(N(0));
    if (f === "sind") return Math.sin(N(0) * PI / 180);
    if (f === "cosd") return Math.cos(N(0) * PI / 180);
    if (f === "lerp") return N(0) + (N(1) - N(0)) * N(2);
    if (f === "step") return N(1) >= N(0) ? 1 : 0;
    if (f === "smoothstep") { const d = (N(1) - N(0)) === 0 ? 1 : N(1) - N(0); const t = clamp01((N(2) - N(0)) / d); return t * t * (3 - 2 * t); }
    if (f === "progress") return clamp01(N(0) / cmax(1, N(1, 1)));
    if (f === "easeIn") { const t = clamp01(N(0)); return t * t * t; }
    if (f === "easeOut") { const t = 1 - clamp01(N(0)); return 1 - t * t * t; }
    if (f === "easeInOut") { const t = clamp01(N(0)); return t < 0.5 ? 4 * t * t * t : 1 - cpow(-2 * t + 2, 3) / 2; }
    if (f === "easeOutBack") { const t = clamp01(N(0)) - 1, c = N(1, 1.70158); return 1 + (c + 1) * t * t * t + c * t * t; }
    if (f === "easeOutElastic") {
      const t = clamp01(N(0));
      return t === 0 || t === 1 ? t : cpow(2, -10 * t) * Math.sin((t * 10 - 0.75) * (2 * PI / 3)) + 1;
    }
    if (f === "bounce") {
      let t = clamp01(N(0)); const k = 7.5625, d = 2.75;
      if (t < 1 / d) return k * t * t;
      if (t < 2 / d) { t -= 1.5 / d; return k * t * t + 0.75; }
      if (t < 2.5 / d) { t -= 2.25 / d; return k * t * t + 0.9375; }
      t -= 2.625 / d; return k * t * t + 0.984375;
    }
    if (f === "pulse") return 0.5 - 0.5 * Math.cos(N(0) * 2 * PI / cmax(1, N(1, 1000)));
    if (f === "mix") return colorText(mixColor(colorOf(A(0)), colorOf(A(1)), N(2, 0.5)));
    if (f === "alpha") { const c = colorOf(A(0)); return colorText((((c & 0xffffff) | ((cround(clamp01(N(1, 1)) * ((c >>> 24) & 255)) << 24)) >>> 0) >>> 0)); }
    if (f === "lighten") { const c = colorOf(A(0)); return colorText(mixColor(c, (((c & 0xff000000) | 0xffffff) >>> 0), N(1, 0.2))); }
    if (f === "darken") { const c = colorOf(A(0)); return colorText(mixColor(c, ((c & 0xff000000) >>> 0), N(1, 0.2))); }
    if (f === "rgb" || f === "rgba") {
      const b = (x) => toUint(cmin(255, cmax(0, x + 0.5)));
      return colorText(((b(N(3, 1) * 255) << 24) | (b(N(0)) << 16) | (b(N(1)) << 8) | b(N(2))) >>> 0);
    }
    if (f === "hsl") {
      let h = (N(0) % 360) / 60, s = clamp01(N(1)), l = clamp01(N(2)), c = (1 - Math.abs(2 * l - 1)) * s;
      if (h < 0) h += 6;
      const x = c * (1 - Math.abs((h % 2) - 1)), m = l - c / 2;
      let r = 0, g = 0, bb = 0; const k = toInt(h);
      if (k === 0) { r = c; g = x; } else if (k === 1) { r = x; g = c; } else if (k === 2) { g = c; bb = x; }
      else if (k === 3) { g = x; bb = c; } else if (k === 4) { r = x; bb = c; } else { r = c; bb = x; }
      const b = (y) => toUint(cmin(255, cmax(0, y * 255 + 0.5)));
      return colorText(((b(N(3, 1)) << 24) | (b(r + m) << 16) | (b(g + m) << 8) | b(bb + m)) >>> 0);
    }
    if (f === "upper") return upperC(S(0));
    if (f === "lower") return lowerC(S(0));
    if (f === "len") return Array.from(S(0)).length;
    if (f === "substr") {
      const s = Array.from(S(0)), from = toInt(N(1)), n = v.length > 2 ? toInt(N(2)) : (1 << 30), to = (from + n) | 0;	// (int: wraps)
      let r = "";
      for (let k = 0; k < s.length; k++) if (k >= from && k < to) r += s[k];
      return r;
    }
    if (f === "contains") return S(0).includes(S(1)) ? 1 : 0;
    if (f === "replace") {
      let s = S(0); const a = S(1), b = S(2);
      if (a !== "") for (let at = 0; (at = s.indexOf(a, at)) !== -1; at += b.length) s = s.slice(0, at) + b + s.slice(at + a.length);
      return s;
    }
    if (f === "pad") {
      // to a width in bytes (UTF-8), as std::string::size() counts. (A huge width stops the engine for good; the
      // editor stops at 4096 bytes, a width no theme needs.)
      let s = S(0); const w = Math.min(4096, toInt(N(1))), ch = v.length > 2 ? S(2) : "0";
      if (ch === "") return s;
      let len = utf8Len(s); const cl = utf8Len(ch);
      while (len < w) { s = ch + s; len += cl; }
      return s;
    }
    if (f === "fixed") return formatF(N(0), cmax(0, cmin(10, toInt(N(1)))));
    if (f === "str") return S(0);
    if (f === "num") return N(0);
    if (f === "plural") return evStr(N(0)) + " " + (N(0) === 1 ? S(1) : (v.length > 2 ? S(2) : S(1) + "s"));
    if (f === "pick") { if (v.length < 2) return ""; const k = cmax(0, cmin(v.length - 2, toInt(N(0)))); return v[k + 1]; }
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
    colorOf, mixColor, evNum, evStr, evTruthy, formatG, formatF, strtod, utf8Len, FUNCTIONS };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.BananaExpr = api;
})(typeof globalThis !== "undefined" ? globalThis : this);