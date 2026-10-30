#!/usr/bin/env node
// Banana Theme Studio: the browser's engine (wasm/banana-engine.js) draws what banana-ui draws.
//
// Every example theme is run through the same script by the native build (ui/banana-ui --headless, as serve.py
// runs it) and by the WebAssembly build (bt_script), and the screens are compared pixel for pixel. Also checks the
// wasm build is current with the engine's sources (wasm/sources.sh), and drives the live calls (bt_frame, bt_press,
// bt_touch) a little.
//
//   node tools/theme-studio/tests/engine.test.js        (KEEP=<dir>: keep the pictures of a mismatch)
//
// Skips (exit 0) when banana-engine.js hasn't been built; builds ui/banana-ui when it can.
"use strict";
const fs = require("fs");
const os = require("os");
const path = require("path");
const zlib = require("zlib");
const { execFileSync, spawnSync } = require("child_process");

process.env.TZ = "UTC";				// the clock both builds draw
const HERE = __dirname;
const STUDIO = path.resolve(HERE, "..");
const ROOT = path.resolve(STUDIO, "../..");
const UI = path.join(ROOT, "ui");
const ENGINE_JS = path.join(STUDIO, "wasm", "banana-engine.js");
const SCRIPT = "home 0; wait 900; shot home; press right; wait 700; shot home2; press a; wait 900; shot lib; " +
  "press down; wait 600; shot lib2; press y; wait 300; shot fav; press a; wait 400; shot launch";
const THEMES = [path.join(ROOT, "system/rootfs/usr/share/banana/themes"), path.join(ROOT, "docs/themes")]
  .flatMap((d) => (fs.existsSync(d) ? fs.readdirSync(d).filter((f) => f.endsWith(".json")).sort().map((f) => path.join(d, f)) : []))
  .filter((f) => /"screens"\s*:/.test(fs.readFileSync(f, "utf8")));

let fails = 0;
const fail = (m) => { console.log("  FAIL " + m); fails++; };

if (!fs.existsSync(ENGINE_JS)) { console.log("  (skipped: wasm/banana-engine.js isn't built; sh tools/theme-studio/wasm/build.sh)"); process.exit(0); }

// is it current?
const build = /sources ([0-9a-f]{16})/.exec(fs.readFileSync(ENGINE_JS, "utf8").slice(0, 400));
const now = execFileSync("sh", [path.join(STUDIO, "wasm", "sources.sh")]).toString().trim();
if (!build || build[1] !== now) {
  console.log("  NOTE banana-engine.js was built from other sources (" + (build ? build[1] : "?") + ", now " + now +
    "): rebuild it with sh tools/theme-studio/wasm/build.sh (the pictures below say whether it matters yet)");
} else console.log("  banana-engine.js is current with the engine's sources (" + now + ")");

// a PNG (8-bit RGB or RGBA, not interlaced) -> RGB bytes
function readPng(file) {
  const b = fs.readFileSync(file);
  let w = 0, h = 0, ct = 0; const idat = [];
  for (let at = 8; at < b.length;) {
    const len = b.readUInt32BE(at), type = b.toString("ascii", at + 4, at + 8), d = b.subarray(at + 8, at + 8 + len);
    if (type === "IHDR") { w = d.readUInt32BE(0); h = d.readUInt32BE(4); ct = d[9]; if (d[8] !== 8 || d[12] !== 0) throw new Error(file + ": unsupported PNG"); }
    else if (type === "IDAT") idat.push(d);
    at += 12 + len;
  }
  const bpp = ct === 6 ? 4 : 3, raw = zlib.inflateSync(Buffer.concat(idat)), stride = w * bpp;
  const px = Buffer.alloc(h * stride), out = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++) {
    const f = raw[y * (stride + 1)], src = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= bpp ? px[y * stride + x - bpp] : 0, up = y ? px[(y - 1) * stride + x] : 0, c = x >= bpp && y ? px[(y - 1) * stride + x - bpp] : 0;
      let v = src[x];
      if (f === 1) v += a; else if (f === 2) v += up; else if (f === 3) v += (a + up) >> 1;
      else if (f === 4) { const p = a + up - c, pa = Math.abs(p - a), pb = Math.abs(p - up), pc = Math.abs(p - c); v += pa <= pb && pa <= pc ? a : pb <= pc ? up : c; }
      px[y * stride + x] = v & 255;
    }
  }
  for (let i = 0; i < w * h; i++) { out[i * 3] = px[i * bpp]; out[i * 3 + 1] = px[i * bpp + 1]; out[i * 3 + 2] = px[i * bpp + 2]; }
  return { w, h, px: out };
}

// a PNG of one colour (for a made-up icon)
function png(w, h, rgb) {
  const crcT = []; for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; crcT[n] = c >>> 0; }
  const crc = (b) => { let c = 0xffffffff; for (const x of b) c = crcT[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; };
  const chunk = (t, d) => { const l = Buffer.alloc(4); l.writeUInt32BE(d.length); const td = Buffer.concat([Buffer.from(t), d]); const c = Buffer.alloc(4); c.writeUInt32BE(crc(td)); return Buffer.concat([l, td, c]); };
  const ih = Buffer.alloc(13); ih.writeUInt32BE(w, 0); ih.writeUInt32BE(h, 4); ih[8] = 8; ih[9] = 2;
  const raw = Buffer.alloc(h * (w * 3 + 1)); for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) raw.set(rgb, y * (w * 3 + 1) + 1 + x * 3);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ih), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0))]);
}

(async () => {
  // the native engine and its library
  if (!fs.existsSync(path.join(UI, "banana-ui"))) {
    const r = spawnSync("make", ["-C", UI, "host"], { stdio: "ignore" });
    if (r.status !== 0 || !fs.existsSync(path.join(UI, "banana-ui"))) { console.log("  (skipped the native comparison: ui/banana-ui doesn't build here)"); }
  }
  const native = fs.existsSync(path.join(UI, "banana-ui"));
  const work = fs.mkdtempSync(path.join(os.tmpdir(), "banana-engine-test-"));
  if (native) execFileSync("sh", [path.join(UI, "tests/fixture.sh"), path.join(work, "fx")], { stdio: "ignore" });

  const M = require(ENGINE_JS);
  const errs = [];
  const B = await M({ print: () => {}, printErr: (t) => errs.push(t) });
  const api = {
    init: B.cwrap("bt_init", "number", ["number", "number", "number"]), load: B.cwrap("bt_load", "string", ["string", "number"]),
    script: B.cwrap("bt_script", "string", ["string"]), host: B.cwrap("bt_host", null, ["number", "number", "number", "number", "number"]),
    frame: B.cwrap("bt_frame", "number", ["number", "number"]), press: B.cwrap("bt_press", "number", ["number"]),
    touch: B.cwrap("bt_touch", "number", ["number", "number", "number", "number"]), view: B.cwrap("bt_view", "number", []),
    system: B.cwrap("bt_system", "number", []), pixels: B.cwrap("bt_pixels", "number", ["number"]),
  };

  for (const size of ["640x480", "1024x768"]) {
    const [W, H] = size.split("x").map(Number);
    for (const file of THEMES) {
      // a fresh library for each, as each native run reads the fixture anew (Y stars a game)
      api.init(W, H, 0); api.host(-1, 0, 0, 1, 0); B.ccall("bt_online", null, ["number"], [0]);
      const dir = "/theme-" + path.basename(file, ".json");
      B.FS.mkdirTree(dir);
      B.FS.writeFile(dir + "/theme.json", fs.readFileSync(file));
      const side = file.replace(/\.json$/, "");
      if (fs.existsSync(side) && fs.statSync(side).isDirectory()) {		// the theme's own pictures (sunset/)
        (function copy(from, to) {
          B.FS.mkdirTree(to);
          for (const n of fs.readdirSync(from)) { const f = path.join(from, n); if (fs.statSync(f).isDirectory()) copy(f, to + "/" + n); else B.FS.writeFile(to + "/" + n, fs.readFileSync(f)); }
        })(side, dir + "/" + path.basename(side));
      }
      const loadErr = api.load(dir + "/theme.json", 0);
      const shots = api.script(SCRIPT);
      const name = path.relative(ROOT, file) + " @" + size;
      if (shots.startsWith("error")) { fail(name + ": " + shots); continue; }
      if (!native) { console.log("  " + name + ": drew " + shots.trim().split("\n").length + " shots" + (loadErr ? " (" + loadErr.trim() + ")" : "")); continue; }
      // the native run: a copy of the theme beside its pictures, as the editor's serve.py does
      const nd = path.join(work, path.basename(file, ".json") + "-" + size);
      fs.mkdirSync(nd, { recursive: true });
      fs.copyFileSync(file, path.join(nd, "theme.json"));
      if (fs.existsSync(side) && fs.statSync(side).isDirectory()) fs.cpSync(side, path.join(nd, path.basename(side)), { recursive: true });
      execFileSync("sh", [path.join(UI, "tests/fixture.sh"), path.join(work, "fx")], { stdio: "ignore" });
      spawnSync(path.join(UI, "banana-ui"), ["--headless", path.join(nd, "out-"), "--size", size,
        "--assets", path.join(UI, "assets/rnds"), "--banana", path.join(UI, "assets/banana"),
        "--systems", path.join(ROOT, "system/rootfs/etc/banana/systems.conf"), "--roms", path.join(work, "fx/roms"),
        "--data", path.join(work, "fx/data"), "--theme", path.join(nd, "theme.json"), "--script", SCRIPT], { encoding: "utf8" });
      let worst = 0, where = "";
      for (const shot of shots.trim().split("\n")) {
        for (const s of ["top", "bot"]) {
          const n = readPng(path.join(nd, "out-" + shot + "-" + s + ".png"));
          const w = Buffer.from(B.FS.readFile("/out/" + shot + "-" + s + ".rgb"));
          let diff = 0;
          for (let i = 0; i < n.px.length; i += 3) if (n.px[i] !== w[i] || n.px[i + 1] !== w[i + 1] || n.px[i + 2] !== w[i + 2]) diff++;
          if (diff > worst) { worst = diff; where = shot + "-" + s; }
        }
      }
      if (worst && process.env.KEEP) {			// KEEP=<dir>: both builds' pictures of the worst shot, to look at
        const shot = where.replace(/-(top|bot)$/, ""), stem = path.join(process.env.KEEP, path.basename(file, ".json") + "-" + size + "-" + shot);
        for (const s of ["top", "bot"]) {
          fs.writeFileSync(stem + "-" + s + "-wasm.ppm", Buffer.concat([Buffer.from("P6 " + W + " " + H + " 255\n"), Buffer.from(B.FS.readFile("/out/" + shot + "-" + s + ".rgb"))]));
          fs.copyFileSync(path.join(nd, "out-" + shot + "-" + s + ".png"), stem + "-" + s + "-native.png");
        }
      }
      if (worst) fail(name + ": " + worst + " pixels differ from banana-ui's (" + where + ")");
      else console.log("  " + name + ": " + shots.trim().split("\n").length + " shots, the same as banana-ui's");
    }
  }

  // the live calls: a frame draws, a button moves the selection, a tap on the bottom screen is used
  api.init(640, 480, 0); api.host(76, 0, 0, 1, 0);
  B.FS.writeFile("/theme-live.json", fs.readFileSync(path.join(ROOT, "system/rootfs/usr/share/banana/themes/canvas.json")));
  api.load("/theme-live.json", 0);
  if (api.frame(1000, 1) !== 1) fail("bt_frame drew nothing");
  const s0 = api.system();
  api.press(1); api.frame(1100, 0);
  if (api.system() === s0) fail("RIGHT didn't move the selection (" + s0 + ")");
  api.press(4); api.frame(2000, 0);
  if (api.view() !== 1) fail("A at home didn't open the library (view " + api.view() + ")");
  // a theme that isn't one, and a broken one, are reported rather than thrown
  B.FS.writeFile("/bad.json", "{ \"screens\": { \"home\": { \"top\": [ { \"type\": \"text\", \"text\": \"=1 +\" } ] } } }");
  api.load("/bad.json", 0); api.frame(3000, 1);
  const why = B.cwrap("bt_errors", "string", [])();
  if (!/theme:/.test(why)) fail("a bad expression wasn't reported: " + JSON.stringify(why));
  B.FS.writeFile("/broken.json", "{ \"screens\": ");
  if (!/theme:/.test(api.load("/broken.json", 0))) fail("broken JSON wasn't reported");

  // a theme's own system icons ("icons": { "<system id>": "file.png" }) are drawn, on every system of the
  // every-system library; a missing one is reported and the default kept
  api.init(640, 480, 1); api.host(76, 0, 0, 1, 0);
  const every = B.ccall("bt_systemCount", "number", [], []);
  const ids = [];
  for (const ln of fs.readFileSync(path.join(ROOT, "system/rootfs/etc/banana/systems.conf"), "utf8").split("\n")) { const m = /^\[(.+)\]$/.exec(ln.trim()); if (m) ids.push(m[1]); }
  if (every < ids.length) fail("the every-system library has " + every + " systems, systems.conf " + ids.length);
  const iconTheme = (icons) => JSON.stringify({ name: "icons", screens: { home: {
    top: [{ type: "rect", fill: "#000" }, { type: "image", x: 0, y: 0, w: 64, h: 64, fit: "pixel", src: "=sys.icon" }],
    bottom: [{ type: "rect", fill: "#000" }] } }, icons: icons });
  B.FS.mkdirTree("/icons/pics");
  B.FS.writeFile("/icons/pics/red.png", png(32, 32, [255, 0, 0]));
  const red = (theme) => {
    B.FS.writeFile("/icons/theme.json", theme);
    const e = api.load("/icons/theme.json", 0);
    const seen = [];
    for (let s = 0; s < every; s++) {
      B.ccall("bt_home", null, ["number"], [s]); api.frame(5000 + s * 1000, 1); api.frame(5000 + s * 1000 + 16, 1);	// (a new picture: decoded after the frame that asked for it)
      const p = api.pixels(0) >> 2, c = B.HEAPU32[p + 32 * 640 + 32];
      seen.push(((c >> 16) & 255) > 200 && ((c >> 8) & 255) < 40 && (c & 255) < 40);
    }
    return { e: e + B.cwrap("bt_errors", "string", [])() + errs.splice(0).join("\n"), seen };
  };
  const all = {}; ids.concat(["favorites", "recently-played"]).forEach((id) => { all[id] = "pics/red.png"; });
  const a = red(iconTheme(all));
  if (a.seen.some((x) => !x)) fail("a theme icon wasn't drawn on systems " + a.seen.map((x, i) => (x ? "" : i)).filter((x) => x !== "").join(","));
  const b = red(iconTheme({ gba: "pics/none.png" }));
  if (b.seen.some((x) => x)) fail("a missing theme icon drew red");
  if (!/pics\/none\.png isn't there/.test(b.e)) fail("a missing icon wasn't reported: " + JSON.stringify(b.e));
  if (!fails) console.log("  theme icons show on all " + every + " systems of the every-system library; a missing one is reported");

  if (!fails) console.log("  the live calls work (frame, buttons, the library, errors)");

  fs.rmSync(work, { recursive: true, force: true });
  if (fails) { console.log("  " + fails + " problem(s)"); process.exit(1); }
})().catch((e) => { console.log("  FAIL " + (e.stack || e)); process.exit(1); });
