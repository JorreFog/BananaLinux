#!/usr/bin/env node
// Banana Theme Studio: tests of optimize.js (Optimize for the handheld).
//
// 1. The rules on small hand-made themes: the same cases, and the same answers, as the desktop studio's
//    tests/test_optimize.py for optimize.py, which optimize.js is a port of.
// 2. The real engine (wasm/banana-engine.js, when built): every example theme drawn before and after its safe fixes
//    gives the same pixels and the same theme errors; with the look fixes too it keeps the same errors.
//
//   node tools/theme-studio/tests/optimize.test.js
"use strict";
const fs = require("fs");
const path = require("path");
const O = require("../optimize.js");
const E = require("../expr.js");

const ROOT = path.resolve(__dirname, "..", "..", "..");
let fails = 0, checks = 0;
function eq(got, want, what) {
  checks++;
  const a = JSON.stringify(got), b = JSON.stringify(want);
  if (a !== b) { fails++; console.log("  FAIL " + what + "\n    got  " + a + "\n    want " + b); }
}
const ids = (fs_, rule) => fs_.filter((f) => !rule || f.rule === rule).map((f) => f.id);
const screen = (top, bottom, extra) => Object.assign({ name: "t", screens: { home: { top: top, bottom: bottom || [] } } }, extra || {});
const sorted = (a) => a.slice().sort();

// ---- 1. the rules ----------------------------------------------------------------------------------------------
eq(O.analyze({ name: "x", accent: "#fff" }), [], "a colour theme has no findings");

(function fold() {
  const d = screen([{ type: "rect", x: "=10 + 20", w: "=W / 2", fill: "=mix('#000000', '#ffffff', 0.5)", h: "=sel ? 1 : 2", y: "{1 + 1}", text: "{{x}}" }]);
  const f = O.analyze(d);
  eq(sorted(ids(f, "fold")), ["fold:screens.home.top[0].fill", "fold:screens.home.top[0].w", "fold:screens.home.top[0].x", "fold:screens.home.top[0].y"], "fold finds the constants");
  const e = O.apply(d, f).doc.screens.home.top[0];
  eq([e.x, e.w, e.y, e.h, e.text], [30, 320, "2", "=sel ? 1 : 2", "{{x}}"], "fold writes their values");
  eq(e.fill, E.colorText(E.parseColorText(e.fill)), "a folded colour is a colour");
  eq(d.screens.home.top[0].x, "=10 + 20", "apply() leaves its input alone");
  const d1 = screen([{ type: "text", text: "='{' + 'a}'" }, { type: "text", text: "='=no'" }], null, { vars: { W: 100 } });
  const n1 = O.apply(d1, O.analyze(d1)).doc.screens.home.top;
  eq([n1[0].text, n1[1].text], ["{{a}}", "='=no'"], "fold keeps braces and a leading =");
  eq(ids(O.analyze(screen([{ type: "rect", w: "=W / 2" }], null, { vars: { W: 100 } })), "fold"), [], "W is the var there, not the screen width");
})();

(function hidden() {
  const d = screen([{ type: "rect", visible: false }, { type: "rect", if: "=1 > 2" }, { type: "rect", opacity: 0 },
    { type: "rect", opacity: 0, states: [{ when: "=sel", opacity: 1 }] },
    { type: "group", layout: "row", children: [{ type: "rect", w: 5, opacity: 0 }, { type: "rect", w: 5, visible: false }] },
    { type: "list", source: "systems", item: { type: "rect", visible: false } },
    { type: "group", children: [{ type: "rect", visible: false }] }]);
  const f = O.analyze(d);
  eq(sorted(ids(f, "hidden")), sorted(["hidden:screens.home.top[0]", "hidden:screens.home.top[1]", "hidden:screens.home.top[2]",
    "hidden:screens.home.top[4].children[1]", "hidden:screens.home.top[6].children[0]"]), "hidden finds what is never drawn");
  const top = O.apply(d, f).doc.screens.home.top;
  eq([top.length, top[1].children.length, top[3].children.length, "item" in top[2]], [4, 1, 0, true], "hidden removes them (not a list's item)");
})();

(function deadStateNoop() {
  const d = screen([{ type: "rect", opacity: 1, scale: 1, visible: true, clip: false, radius: 4, shadow: { blur: 9, color: "#00000000" },
    smooth: { x: 100, w: 90, y: 50 }, w: "=sel ? 1 : 2", states: [{ when: "=0", fill: "#fff" }, { fill: "#000" }, { when: "=sel", x: 4 }] }]);
  const f = O.analyze(d);
  eq(ids(f, "dead-state").length, 2, "two states never apply");
  const e = O.apply(d, f).doc.screens.home.top[0];
  eq(Object.keys(e).sort(), ["radius", "smooth", "states", "type", "w"], "the do-nothing properties go");
  eq(e.smooth, { x: 100, w: 90 }, "smooth stays where a state or an expression moves the value");
  eq(e.states, [{ when: "=sel", x: 4 }], "the dead states go");
  eq(ids(O.analyze(screen([{ use: "c" }, { use: "c", opacity: 0.5 }], null, { components: { c: { type: "rect", opacity: 1 } } })), "noop"), ["noop:components.c"], "noop in a component");
})();

(function vars() {
  const f = O.analyze(screen([{ type: "rect", fill: "=a" }], null, { vars: { a: "#fff", b: "=a", c: "=time", d: "=e", e: 1 } }));
  eq(sorted(ids(f, "unused-var")), ["unused-var:vars.b", "unused-var:vars.c", "unused-var:vars.d", "unused-var:vars.e"], "unused vars (one read only by an earlier var too)");
  eq(f.filter((x) => x.id === "unused-var:vars.c").map((x) => x.weight), [3], "an unused time var weighs 3");
  const d = screen([{ type: "rect", x: "=spin * 2", w: 5 }], null, { vars: { spin: "=time * 0.1", two: "=spin + 1" } });
  d.screens.library = { top: [{ type: "rect", x: "=two" }], bottom: [] };
  const g = O.analyze(d);
  eq(ids(g).indexOf("inline-time-var:vars.spin") >= 0, true, "a time var can move into its users");
  const n = O.apply(d, g.filter((x) => x.rule === "inline-time-var")).doc;
  eq([n.vars, n.screens.library.top[0].x, n.screens.home.top[0].x], [{}, "=((time * 0.1) + 1)", "=(time * 0.1) * 2"], "inline-time-var writes the formula in (through two vars)");
  const d2 = screen([{ use: "c", with: { spin: 1 } }], null, { vars: { spin: "=time" }, components: { c: { type: "rect", x: "=spin" } } });
  eq(ids(O.analyze(d2)).indexOf("inline-time-var:vars.spin"), -1, "not when a component parameter has its name");
})();

eq(O.subst("=time + sys.time + pulse(time, 9) + 'time' + launch.t", { time: "0" }), "=0 + sys.time + pulse(0, 9) + 'time' + launch.t", "subst skips strings, longer names, functions");
eq(O.subst("{time} {{time}} {1e3 + time}", { time: "0" }), "{0} {{time}} {1e3 + 0}", "subst in templates");

(function holdStill() {
  const d = screen([{ type: "rect", id: "star", x: 10, w: 4, h: 4, opacity: "=0.5 + 0.5 * pulse(time, 1000)" }, { type: "rect", id: "nope", visible: false, x: "=time" }]);
  const f = O.analyze(d);
  eq(ids(f, "hold-still"), ["hold-still:screens.home.top[0]"], "hold-still (not for a hidden element)");
  eq(ids(f).indexOf("lower-fps") >= 0, true, "lower-fps with something moving");
  eq(O.apply(d, ["hold-still:screens.home.top[0]"]).doc.screens.home.top[0].opacity, 0.5, "hold-still freezes and folds");
})();

(function settle() {
  const d = screen([{ type: "rect", opacity: "=easeOut(progress(moved - 80, 380))" }, { type: "rect", x: "=progress(entered, 200) * 4" }], null,
    { vars: { m: "=progress(moved, 300)" }, behavior: { settleMs: 900 } });
  eq(O.analyze(d).filter((x) => x.rule === "settle").map((x) => x.data.settleMs), [560], "settle: the longest progress() + 100");
  d.vars.raw = "=moved < 100";
  eq(ids(O.analyze(d), "settle"), [], "settle: none when moved is used another way");
})();

(function flowSmooth() {
  const dots = { type: "list", source: "systems", direction: "row", flow: true, scrollMode: "none", item: { type: "rect", w: "=sel ? 23 : 7", h: 7, smooth: { w: 120, x: 50 } } };
  const f = O.analyze(screen([dots]));
  eq(ids(f, "flow-smooth"), ["flow-smooth:screens.home.top[0].item"], "flow-smooth on a flow list's item");
  eq(O.apply(screen([dots]), f).doc.screens.home.top[0].item.smooth, { x: 50 }, "flow-smooth drops the measured keys");
  const one = { type: "group", layout: "row", children: [{ type: "rect", w: 4, smooth: 90 }, { type: "rect", w: 3 }] };
  eq(ids(O.analyze(screen([one])), "flow-smooth"), [], "a single smoothed child settles");
})();

(function range() {
  const wheel = { type: "list", source: "systems", range: 8, item: { type: "rect", x: "=320 + pos * 100", w: 50, h: 50, opacity: "=1 - abs(pos) * 0.5" } };
  eq(O.analyze(screen([wheel])).filter((x) => x.rule === "range").map((x) => x.data.range), [3], "range trims a free list");
  eq(ids(O.analyze(screen([wheel], null, { input: { home: { l: "first" } } })), "range"), [], "not when a button jumps to the first item");
})();

(function advice() {
  const f = O.analyze(screen([{ type: "image", src: "{sys.icon}", w: 64, h: 64, scale: "=1 + 0.1 * pulse(time, 900)" },
    { type: "rect", w: "=100 + 10 * easeOut(progress(moved, 300))", h: 40, radius: 8 },
    { type: "text", text: "x", size: "=10 + launch.progress * 10" }, { type: "rect", fill: "=nope" }]));
  eq(f.filter((x) => x.category === O.ADVICE).map((x) => x.rule).sort(), ["reshape", "resize-image", "resize-text", "unknown-name"], "the advice rules");
})();

(function imageCap() {
  const files = { "bg.png": { w: 1500, h: 1000 }, "card.png": { w: 1500, h: 1000 } };
  const d = screen([{ type: "image", src: "bg.png", w: "100%", h: "100%" },
    { type: "group", x: 10, y: 10, w: 200, h: 200, children: [{ type: "image", src: "card.png", w: "50%", h: 60, fit: "contain", scale: "=sel ? 1.2 : 1" }] },
    { type: "image", src: "=sel ? 'bg.png' : ''", w: 10, h: 10 }]);
  const f = O.analyze(d, files), capd = {};
  f.filter((x) => x.rule === "image-cap").forEach((x) => { capd[x.data.ref] = x.data.new; });
  eq(capd, { "card.png": [216, 144] }, "image-cap: contain in 120x72 at 2x; bg.png is also named in an expression");
  const r = O.apply(d, f, files);
  eq([r.doc.screens.home.top[1].children[0].src, r.changes.map((c) => [c.src, c.dst, c.newSize])], ["card.216x144.png", [["card.png", "card.216x144.png", [216, 144]]]], "image-cap names a copy");
})();

// ---- 2. the real engine: the safe fixes change no pixel ------------------------------------------------------------
(async function engine() {
  const js = path.join(__dirname, "..", "wasm", "banana-engine.js");
  if (!fs.existsSync(js)) { console.log("  (engine checks skipped: wasm/banana-engine.js isn't built)"); return done(); }
  process.env.TZ = "UTC";
  const B = await require(js)({ print() {}, printErr() {} });
  const c = (n, r, a) => B.cwrap(n, r, a);
  const init = c("bt_init", "number", ["number", "number", "number"]), load = c("bt_load", "string", ["string", "number"]);
  const script = c("bt_script", "string", ["string"]), errors = c("bt_errors", "string", []);
  // (each shot twice: the second shows pictures the first asked for, a fallback after a failed src)
  const SCRIPT = "home 0; wait 3000; shot h0; shot h0; press right; wait 3000; shot h1; shot h1; press a; wait 3000; shot l0; shot l0; " +
    "press down; wait 3000; shot l1; shot l1; press a; wait 500; shot launch; shot launch";
  const draw = (doc) => {
    init(640, 480, 0);
    B.FS.writeFile("/opt-theme.json", JSON.stringify(doc));
    load("/opt-theme.json", 0);
    const names = script(SCRIPT).trim().split("\n");
    const px = {};
    names.forEach((n) => { px[n] = ["top", "bot"].map((s) => Buffer.from(B.FS.readFile("/out/" + n + "-" + s + ".rgb"))); });
    return { px: px, errors: errors().split("\n").filter(Boolean).sort() };
  };
  const themes = [path.join(ROOT, "docs/themes"), path.join(ROOT, "system/rootfs/usr/share/banana/themes")]
    .flatMap((d) => fs.readdirSync(d).filter((f) => f.endsWith(".json")).sort().map((f) => path.join(d, f)));
  const loose = (t) => JSON.parse(t.replace(/("(?:[^"\\]|\\.)*")|\/\/[^\n]*|\/\*[\s\S]*?\*\//g, (m, s) => s || "").replace(/,(\s*[}\]])/g, "$1"));
  for (const file of themes) {
    const doc = loose(fs.readFileSync(file, "utf8"));
    const name = path.basename(file);
    if (!doc.screens) { eq(O.analyze(doc), [], name + ": a colour theme has no findings"); continue; }
    const f = O.analyze(doc);
    const safe = O.apply(doc, f.filter((x) => x.category === O.SAFE)).doc;
    const look = O.apply(doc, f.filter((x) => x.category !== O.ADVICE)).doc;
    const a = draw(doc), b = draw(safe), l = draw(look);
    let diff = 0;
    Object.keys(a.px).forEach((n) => [0, 1].forEach((s) => { if (!a.px[n][s].equals(b.px[n][s])) diff++; }));
    eq([diff, b.errors], [0, a.errors], name + ": the safe fixes (" + f.filter((x) => x.category === O.SAFE).length + ") change no pixel and no error");
    eq(l.errors, a.errors, name + ": the look fixes (" + f.filter((x) => x.category === O.LOOK).length + ") keep the theme's errors");
    eq(ids(O.analyze(safe)).filter((id) => /^(inline-time-var|unused-var|settle|dead-state|noop|hidden):/.test(id) || id === "settle"), [], name + ": no safe finding is left after applying them");
  }
  done();
})();

function done() {
  console.log((fails ? "FAILED " : "ok ") + (checks - fails) + "/" + checks + " checks");
  process.exit(fails ? 1 : 0);
}
