#!/usr/bin/env node
// Banana Theme Studio: tests for expr.js.
//
// Two halves:
//   1. Parity — every line of expr-cases.txt is evaluated through expr.js and compared with the real engine.
//      The engine side is built from ui/src/expr.cpp by tests/parity.cpp (needs g++; skipped, with a warning,
//      if it isn't there or the build fails).
//   2. Unit checks — the property forms (literal / "=expr" / "{template}"), parse errors and colour parsing.
//
// Run: node tools/theme-studio/tests/expr.test.js
"use strict";
const fs = require("fs");
const path = require("path");
const os = require("os");
const { execFileSync } = require("child_process");

const E = require("../expr.js");

const HERE = __dirname;
const ROOT = path.resolve(HERE, "..", "..", "..");
let failures = 0, checks = 0;
function check(ok, what, detail) {
  checks++;
  if (!ok) { failures++; console.log("  FAIL " + what + (detail ? " — " + detail : "")); }
}

function makeScope() {
  const sc = new E.Scope();
  sc.set("x", 3); sc.set("y", 0.5); sc.set("zero", 0); sc.set("one", 1); sc.set("n", 7);
  sc.set("name", "Banana"); sc.set("msg", "hello"); sc.set("empty", ""); sc.set("col", "#ff8000");
  return sc;
}
function jsResult(expr) {
  const r = E.compileExpr(expr);
  if (!r.expr) return "ERR";
  const v = E.evalExpr(r.expr, makeScope());
  return typeof v === "string" ? "s|" + v : "n|" + E.evStr(v);
}

// ---- 1. parity against the C++ engine ------------------------------------------------------------------
function parity() {
  const casesFile = path.join(HERE, "expr-cases.txt");
  const src = fs.readFileSync(casesFile, "utf8");
  const cases = src.split("\n").map((l) => l.replace(/[\r ]+$/, "")).filter((l) => l && l[0] !== "#");
  const bin = path.join(os.tmpdir(), "banana-parity");
  let expected = null;
  try {
    execFileSync("g++", ["-O2", "-std=c++17", "-I", path.join(ROOT, "ui", "src"),
      path.join(HERE, "parity.cpp"), path.join(ROOT, "ui", "src", "expr.cpp"), "-o", bin], { stdio: "pipe" });
    const out = execFileSync(bin, [casesFile], { encoding: "utf8" });
    expected = new Map();
    for (const line of out.split("\n")) {
      if (!line) continue;
      const t = line.indexOf("\t");
      if (t < 0) continue;
      expected.set(line.slice(0, t), line.slice(t + 1));
    }
  } catch (e) {
    console.log("  (parity skipped: " + (e.stderr ? String(e.stderr).split("\n")[0] : e.message) + ")");
    return;
  }
  if (!expected || expected.size === 0) { console.log("  (parity skipped: the engine produced no output)"); return; }
  console.log("  parity against ui/src/expr.cpp (" + cases.length + " cases)");
  for (const c of cases) {
    const want = expected.get(c);
    if (want === undefined) { check(false, "missing engine result", c); continue; }
    check(jsResult(c) === want, JSON.stringify(c), "js=" + jsResult(c) + " engine=" + want);
  }
}

// ---- 2. property forms ---------------------------------------------------------------------------------
function props() {
  const sc = makeScope();
  const ev = (t) => E.evalProp(E.parseProp(t), sc);
  check(ev("plain") === "plain", "literal", ev("plain"));
  check(ev("20") === "20", "literal number is a string", ev("20"));
  check(ev("=1 + 2") === 3, "=expression", ev("=1 + 2"));
  check(ev("{x} plays") === "3 plays", "template", ev("{x} plays"));
  check(ev("{x} + {y}") === "3 + 0.5", "template two parts", ev("{x} + {y}"));
  check(ev("{{brace}}") === "{brace}", "doubled braces", ev("{{brace}}"));
  check(ev("=sel ? 'on' : 'off'") === "off", "unknown var is empty (falsy)", ev("=sel ? 'on' : 'off'"));

  const bad = E.parseProp("=1 +");
  check(bad.error !== "" && bad.expr === null, "expression parse error", JSON.stringify(bad.error));
  const badT = E.parseProp("{1 +}");
  check(badT.error !== "" || badT.parts[0][1] === null, "template with a bad expression is flagged", JSON.stringify(badT.error));
  const okT = E.parseProp("{x}");
  check(okT.error === "" && okT.parts.length === 2, "good template has no error", JSON.stringify(okT.error));

  check(E.compileExpr("(1 + 2").error !== "", "unbalanced parens error");
  check(E.compileExpr("1 2").error !== "", "trailing junk error");
  check(E.compileExpr("a ? b").error !== "", "missing ':' error");

  // ---- colour parsing ----
  check(E.parseColorText("#abc") === 0xffaabbcc, "#rgb expands", E.parseColorText("#abc"));
  check(E.parseColorText("#abcd") === 0xddaabbcc, "#rgba expands", E.parseColorText("#abcd"));
  check(E.parseColorText("#ff8000") === 0xffff8000, "#rrggbb", E.parseColorText("#ff8000"));
  check(E.parseColorText("#ff800080") === 0x80ff8000, "#rrggbbaa reorders to ARGB", E.parseColorText("#ff800080"));
  check(E.parseColorText("rgb(255, 0, 0)") === 0xffff0000, "rgb()", E.parseColorText("rgb(255, 0, 0)"));
  check(E.parseColorText("transparent") === 0, "transparent", E.parseColorText("transparent"));
  check(E.parseColorText("red") === 0xffff0000, "named colour", E.parseColorText("red"));
  check(E.parseColorText("nope") === null, "not a colour", E.parseColorText("nope"));
  check(E.colorText(0xff8000ff) === "#8000ffff", "colorText is #rrggbbaa", E.colorText(0xff8000ff));
  check(E.colorText(0x80ff8000) === "#ff800080", "colorText reorders ARGB -> #rrggbbaa", E.colorText(0x80ff8000));

  check(E.evStr(1 / 3) === "0.333333", "%g six digits", E.evStr(1 / 3));
  check(E.evStr(0.1 + 0.2) === "0.3", "%g strips zeros", E.evStr(0.1 + 0.2));
  check(E.evStr(1e16) === "1e+16", "%g exponent", E.evStr(1e16));
  check(E.evTruthy("") === false && E.evTruthy("0") === false && E.evTruthy("x") === true, "truthiness");
}

console.log("Banana Theme Studio — expr.js tests");
parity();
props();
console.log((failures ? "FAILED " : "ok ") + (checks - failures) + "/" + checks + " checks");
process.exit(failures ? 1 : 0);