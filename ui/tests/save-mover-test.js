// The Save Mover (web/save-mover/index.html): where each save it is given goes on the card, and the DS footer cut by
// nds-saves' own rule. Its page script runs here against a stand-in document, and hands its functions over.
//
//   node ui/tests/save-mover-test.js
"use strict";
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const html = fs.readFileSync(path.join(__dirname, "../../web/save-mover/index.html"), "utf8");
const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));

// (any element, of any depth: what is set on it reads back, anything else is another stand-in, callable)
function stub() {
  const props = {};
  return new Proxy(function () {}, {
    get(_, k) { if (k in props) return props[k]; if (k === Symbol.toPrimitive) return () => ""; return stub(); },
    set(_, k, v) { props[k] = v; return true; },
    apply() { return stub(); },
  });
}
let api = null;
const ctx = {
  document: stub(), window: stub(), TextEncoder, TextDecoder,
  Option: class { constructor(text, value) { this.text = text; this.value = value; } },
  saveMoverTest: (f) => { api = f; },
};
vm.runInNewContext(script, ctx);

let fails = 0;
const check = (ok, what) => { if (!ok) { fails++; console.log("FAIL save-mover: " + what); } };
if (!api) { console.log("FAIL save-mover: the page didn't hand its functions over"); process.exit(1); }

// item <path> <bytes> [forced]: one file as the page reads it, classified
function item(p, bytes, forced) {
  const name = p.split("/").pop(), i = name.lastIndexOf(".");
  return api.classify({ id: 1, path: p, name, ext: name.slice(i + 1).toLowerCase(), base: name.slice(0, i), size: bytes.length, mtime: 0, include: true, bytes, forced });
}
// dsv <raw size> <footer length> [padded] [version]: DeSmuME's footer after a raw save
function dsv(raw, foot, padded = raw, version = 0) {
  const b = new Uint8Array(raw + foot), v = new DataView(b.buffer);
  b.set(new TextEncoder().encode("|-DESMUME SAVE-|"), b.length - 16);
  v.setUint32(b.length - 36, padded, true); v.setUint32(b.length - 20, version, true);
  return b;
}
const where = (it) => it.kind === "skip" ? "skip" : it.kind === "ask" ? "ask" : api.outPath(it);

// RetroArch's saves go where the card's cores read them: every GBA core's saves/gba, every other system's the one
// flat saves/ (ui/src/save_paths.cpp is where the card's own sync finds them; a folder per core is no longer read).
// The system is told by the folder the save came from, the ROM matched, or the owner's pick
check(where(item("RetroArch/saves/mGBA/Game.srm", new Uint8Array(131072))) === "saves/gba/Game.srm", "an mGBA save");
check(where(item("RetroArch/saves/Snes9x/Kart.srm", new Uint8Array(8192))) === "saves/Kart.srm", "a Snes9x save");
check(where(item("RetroArch/saves/Gambatte/Land.srm", new Uint8Array(8192))) === "saves/Land.srm", "a Gambatte save");
check(where(item("RetroArch/saves/Nestopia/Bros.srm", new Uint8Array(8192))) === "saves/Bros.srm", "a NES save from another core");
check(where(item("Game_1.mcd", new Uint8Array(131072))) === "saves/Game.srm", "a PS1 memory card");
check(where(item("mGBA/Clock.sav", new Uint8Array(131088))) === "saves/gba/Clock.srm", "mGBA's save with its clock bytes");
check(where(item("RetroArch/saves/Flat.srm", new Uint8Array(8192))) === "ask", "a save of no known system: asked, since a GBA save has its own folder");
check(where(item("RetroArch/saves/Flat.srm", new Uint8Array(8192), "ra:snes")) === "saves/Flat.srm", "a system picked: the flat saves/");
check(where(item("RetroArch/saves/Flat.srm", new Uint8Array(8192), "ra:gba")) === "saves/gba/Flat.srm", "the GBA picked: its folder");
for (const core of ["Gambatte", "Snes9x", "FCEUmm", "PicoDrive", "PCSX-ReARMed", "Mupen64Plus-Next"])
  check(/^saves\/[^/]+\.srm$/.test(where(item("RetroArch/saves/" + core + "/Any.srm", new Uint8Array(32768)))), "nothing goes to a core's folder (" + core + ")");

// two saves for one place on the card: copies of one game's save (the newest kept, the other said as a copy), or, from
// two systems, two games' saves the flat saves/ can't keep apart (one kept, both said as that, neither as a copy)
const noteOf = (x) => x.notes.filter((n) => n[2]).map((n) => n[1]).join(" ");
let a = item("RetroArch/saves/Gambatte/Tetris.srm", new Uint8Array(8192)), b = item("RetroArch/saves/FCEUmm/Tetris.srm", new Uint8Array(8192));
a.mtime = 2; b.mtime = 1;
api.resolveDuplicates([a, b]);
check(a.include && !b.include, "two systems' saves of one name: one kept");
check(/different game/.test(noteOf(a)) && /different game/.test(noteOf(b)) && /Game Boy/.test(noteOf(b)) && /NES/.test(noteOf(b)), "two systems' saves of one name: said on both, with the systems: " + noteOf(a) + " | " + noteOf(b));
check(!/cop(y|ies)/i.test(noteOf(a) + noteOf(b)), "two systems' saves of one name: not said as copies");
a = item("RetroArch/saves/Gambatte/Land.srm", new Uint8Array(8192)); b = item("backup/RetroArch/saves/Gambatte/Land.srm", new Uint8Array(8192));
a.mtime = 1; b.mtime = 2;
api.resolveDuplicates([a, b]);
check(!a.include && b.include && /Newest of 2 copies/.test(noteOf(b)) && /Another copy/.test(noteOf(a)), "one system's save twice: the newest kept, the other a copy: " + noteOf(a) + " | " + noteOf(b));

// the DS: a footer cut by nds-saves' rule, or the file left out
let it = item("DraStic/backup/Quest.dsv", dsv(8192, 122));
check(where(it) === "saves/nds/Quest.sav" && api.convert(it).length === 8192, "a .dsv's footer cut off");
it = item("DraStic/backup/Old.dsv", dsv(8192, 40));
check(where(it) === "saves/nds/Old.sav" && api.convert(it).length === 8192, "an older DraStic's 40-byte footer cut off");
check(where(item("DraStic/backup/Bad.dsv", dsv(8192, 122, 3000))) === "skip", "a footer whose padded size doesn't add up: left out, not cut to it");
check(where(item("DraStic/backup/Ver.dsv", dsv(8192, 122, 8192, 1))) === "skip", "a footer of another version: left out");
check(where(item("DraStic/backup/Odd.dsv", dsv(3000, 122))) === "skip", "a footer around no DS save's size: left out");
check(where(item("melonDS/Short.sav", new Uint8Array(3000))) === "skip", "a raw DS save of no DS save's size: left out");
check(where(item("melonDS/Kart.sav", new Uint8Array(8192))) === "saves/nds/Kart.sav", "a raw DS save");

if (!fails) console.log("save-mover-test: all checks passed");
process.exit(fails ? 1 : 0);
