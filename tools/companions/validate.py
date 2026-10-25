#!/usr/bin/env python3
"""Validate game companion defs against SCHEMA.md (schema 1).

  python3 validate.py              every defs/*.json
  python3 validate.py a.json ...   those files
  -q                               only failures and the summary
  --json                           one JSON line per file (for scripts)

Exit status 0 when every file is valid. Warnings never fail a file.
The companion idea is PokeDaisy's (Lidor Itzhari).
"""
import json, os, re, sys

PAGE_IDS = ["status", "detail", "battle", "map", "bag", "collection", "progress"]
TYPES = {"u8": 1, "u16": 2, "u32": 4, "s8": 1, "s16": 2, "s32": 4, "bcd8": 1, "bcd16": 2, "bcd32": 4, "bool": 1, "text": 0}
QUALITY = {"live-verified", "live-unverified", "reference-only"}
RANGES = [(0x02000000, 0x0203FFFF, "EWRAM"), (0x03000000, 0x03007FFF, "IWRAM"), (0x08000000, 0x09FFFFFF, "ROM")]
FUNCS = {"min", "max", "clamp", "abs", "floor", "ceil", "round", "mod", "if", "band", "bor", "bxor", "shl", "shr",
         "bit", "popcount", "sqrt", "pow", "sign", "upper", "lower", "len", "substr"}
WIDGETS = {"number", "text", "bar", "flags", "count", "list", "static", "grid", "kv"}
NAMED_FORMATS = {"time60", "secs", "hex", "pad2", "pad3", "pad6", "thousands"}
NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
RESERVED = {"true", "false", "null"}
POKEMON_CODES = {"AXVE", "AXPE", "BPEE", "BPRE", "BPGE"}


def parse_int(v):
    if isinstance(v, bool):
        raise ValueError("a boolean, not a number")
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        s = v.replace("_", "").strip()
        neg = s.startswith("-")
        if neg:
            s = s[1:]
        n = int(s, 16) if s.lower().startswith("0x") else int(s, 10)
        return -n if neg else n
    raise ValueError("not a number: %r" % (v,))


def region(a, size):
    for lo, hi, name in RANGES:
        if lo <= a <= hi:
            return name if a + max(size, 1) - 1 <= hi else None
    return None


TOKEN = re.compile(r"\s*(?:(0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][-+]?\d+)?|\.\d+)|('(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\")"
                   r"|([A-Za-z_$][A-Za-z0-9_.$]*)|(\|\||&&|==|!=|<=|>=|[-+*/%<>!?:(),]))")


def expr_names(text):
    """(names used, functions called, error) for an expression; a light parse: tokens + balanced parentheses."""
    if not isinstance(text, str) or not text.strip():
        return set(), set(), "empty expression"
    i, names, funcs, depth, toks = 0, set(), set(), 0, []
    while i < len(text):
        if text[i:].strip() == "":
            break
        m = TOKEN.match(text, i)
        if not m:
            return names, funcs, "can't read %r at %d" % (text, i)
        toks.append(m.groups())
        i = m.end()
    for k, (num, st, ident, op) in enumerate(toks):
        if ident:
            nxt = toks[k + 1][3] if k + 1 < len(toks) else None
            if nxt == "(":
                funcs.add(ident)
            elif ident not in RESERVED:
                names.add(ident.lstrip("$"))
        elif op == "(":
            depth += 1
        elif op == ")":
            depth -= 1
            if depth < 0:
                return names, funcs, "unbalanced ')' in %r" % text
    if depth:
        return names, funcs, "unbalanced '(' in %r" % text
    if not toks:
        return names, funcs, "empty expression"
    return names, funcs, None


class Check:
    def __init__(self, path):
        self.path, self.errors, self.warnings = path, [], []

    def err(self, m):
        self.errors.append(m)

    def warn(self, m):
        self.warnings.append(m)


def check_expr(c, where, text, known, extra=()):
    names, funcs, e = expr_names(text)
    if e:
        c.err("%s: %s" % (where, e))
        return set()
    for f in funcs - FUNCS:
        c.err("%s: unknown function %s()" % (where, f))
    for n in names - set(known) - set(extra):
        c.err("%s: unknown var '%s'" % (where, n))
    return names


def check_mem(c, where, d, enums, text_ok=True, allow_ptr=True):
    """addr/type/post-processing of a var or a list field (fields use 'off' instead of 'addr')."""
    t = d.get("type", "u8")
    if t not in TYPES:
        c.err("%s: type %r (one of %s)" % (where, t, " ".join(TYPES)))
        return 0
    size = TYPES[t]
    if t == "text":
        if not text_ok:
            c.err("%s: text not allowed here" % where)
        ln = d.get("len", 16)
        if not isinstance(ln, int) or not 1 <= ln <= 64:
            c.err("%s: len must be 1-64" % where)
            ln = 16
        size = ln
        enc = d.get("enc", "ascii")
        if enc != "ascii":
            if not (isinstance(enc, str) and enc.startswith("charmap:")):
                c.err("%s: enc %r (ascii or charmap:<enum>)" % (where, enc))
            elif enc[8:] not in enums:
                c.err("%s: charmap enum %r not defined" % (where, enc[8:]))
        if "term" in d and not (isinstance(d["term"], int) and -1 <= d["term"] <= 255):
            c.err("%s: term must be -1..255" % where)
    for k in ("mask", "shift", "add", "mul", "div"):
        if k in d:
            try:
                v = parse_int(d[k]) if k != "mul" or not isinstance(d[k], float) else d[k]
                if k == "div" and v == 0:
                    c.err("%s: div 0" % where)
                if k == "shift" and not 0 <= v < 32:
                    c.err("%s: shift out of 0..31" % where)
            except Exception as e:
                c.err("%s: %s: %s" % (where, k, e))
    if "ptr" in d and not allow_ptr:
        c.err("%s: ptr not allowed here" % where)
    return size


def check_def(path, data=None):
    c = Check(path)
    try:
        d = data if data is not None else json.load(open(path, encoding="utf-8"))
    except Exception as e:
        c.err("not JSON: %s" % e)
        return c
    if not isinstance(d, dict):
        c.err("top level is not an object")
        return c
    base = os.path.basename(path)
    if d.get("schema") != 1:
        c.err("schema must be 1")
    for k in ("title", "accent", "quality"):
        if not isinstance(d.get(k), str) or not d.get(k):
            c.err("missing/empty string '%s'" % k)
    if isinstance(d.get("accent"), str) and not re.match(r"^#[0-9a-fA-F]{6}$", d["accent"]):
        c.err("accent must be #rrggbb")
    if d.get("quality") not in QUALITY:
        c.err("quality %r (one of %s)" % (d.get("quality"), ", ".join(sorted(QUALITY))))
    if not isinstance(d.get("sources", None), list):
        c.err("sources must be a list")
    known_top = {"schema", "title", "match", "accent", "quality", "sources", "vars", "enums", "pages", "notes", "genre", "credit"}
    for k in d:
        if k not in known_top:
            c.warn("unknown top-level key '%s' (ignored)" % k)
    # match
    m = d.get("match")
    if not isinstance(m, dict):
        c.err("match must be an object")
        m = {}
    codes = m.get("code", [])
    codes = [codes] if isinstance(codes, str) else codes
    if not isinstance(codes, list) or any(not isinstance(x, str) or len(x) != 4 for x in codes):
        c.err("match.code: a 4-character string or a list of them")
        codes = []
    crcs = m.get("crc32", [])
    if isinstance(crcs, str):
        crcs = [crcs]
    if not isinstance(crcs, list) or any(not isinstance(x, str) or not re.match(r"^[0-9a-fA-F]{8}$", x) for x in crcs):
        c.err("match.crc32: a list of 8-hex-digit strings")
        crcs = []
    if "title12" in m and (not isinstance(m["title12"], str) or len(m["title12"]) > 12):
        c.err("match.title12: a string of at most 12 characters")
    if m.get("exclusive") and not crcs:
        c.err("match.exclusive needs crc32")
    if not codes and not crcs and not m.get("zip"):
        c.err("match: needs code, crc32 or zip")
    if set(codes) & POKEMON_CODES and not m.get("exclusive"):
        c.warn("match.code is a Pokémon game's: the Pokémon companion keeps it (this def is used only if that is off)")
    # enums
    enums = d.get("enums", {})
    if not isinstance(enums, dict):
        c.err("enums must be an object")
        enums = {}
    for en, ev in enums.items():
        if not isinstance(ev, dict):
            c.err("enum %s: must be an object" % en)
            continue
        for k, v in ev.items():
            try:
                parse_int(k)
            except Exception:
                c.err("enum %s: key %r is not a number" % (en, k))
            if not isinstance(v, str):
                c.err("enum %s[%s]: label must be a string" % (en, k))
    # vars
    vars_ = d.get("vars", {})
    if not isinstance(vars_, dict):
        c.err("vars must be an object")
        vars_ = {}
    live = set()
    for vn, v in vars_.items():
        w = "vars.%s" % vn
        if not NAME_RE.match(vn) or vn in RESERVED:
            c.err("%s: bad var name" % w)
        if not isinstance(v, dict):
            c.err("%s: must be an object" % w)
            continue
        kinds = [k for k in ("addr", "expr", "value") if k in v]
        if len(kinds) != 1:
            c.err("%s: needs exactly one of addr / expr / value (has %s)" % (w, kinds or "none"))
            continue
        if "expr" in v:
            check_expr(c, w, v["expr"], vars_.keys())
            continue
        if "value" in v:
            continue
        size = check_mem(c, w, v, enums)
        try:
            a = parse_int(v["addr"])
        except Exception as e:
            c.err("%s: addr: %s" % (w, e))
            continue
        if "ptr" in v:
            p = v["ptr"]
            if not (isinstance(p, list) and len(p) == 2):
                c.err("%s: ptr must be [pointerAddr, offset]" % w)
            else:
                try:
                    pa, off = parse_int(p[0]), parse_int(p[1])
                    if region(pa, 4) not in ("EWRAM", "IWRAM", "ROM"):
                        c.err("%s: ptr address 0x%08X outside EWRAM/IWRAM/ROM" % (w, pa))
                    if region(pa, 4) in ("EWRAM", "IWRAM"):
                        live.add(vn)
                except Exception as e:
                    c.err("%s: ptr: %s" % (w, e))
            # (with ptr, addr is only documentation: the target is pointer + offset)
            continue
        r = region(a, size)
        if not r:
            c.err("%s: 0x%08X+%d outside EWRAM 0x02000000-0x0203FFFF / IWRAM 0x03000000-0x03007FFF / ROM 0x08000000-0x09FFFFFF" % (w, a, size))
        elif r != "ROM":
            live.add(vn)
    # expr vars that depend on live vars are live too
    changed = True
    while changed:
        changed = False
        for vn, v in vars_.items():
            if isinstance(v, dict) and "expr" in v and vn not in live:
                ns, _, e = expr_names(v["expr"])
                if not e and ns & live:
                    live.add(vn)
                    changed = True
    # pages
    pages = d.get("pages")
    if not isinstance(pages, list):
        c.err("pages must be a list")
        pages = []
    ids = [p.get("id") for p in pages if isinstance(p, dict)]
    for pid in PAGE_IDS:
        n = ids.count(pid)
        if n != 1:
            c.err("page '%s' appears %d times (each of the 7 ids exactly once)" % (pid, n))
    for pid in ids:
        if pid not in PAGE_IDS:
            c.err("unknown page id %r" % pid)
    for pi, p in enumerate(pages):
        if not isinstance(p, dict):
            c.err("pages[%d]: must be an object" % pi)
            continue
        w = "page %s" % p.get("id", pi)
        if not isinstance(p.get("title"), str) or not p.get("title"):
            c.err("%s: title missing" % w)
        if "tab" in p and (not isinstance(p["tab"], str) or len(p["tab"]) > 9):
            c.err("%s: tab must be a string of at most 9 characters" % w)
        if "when" in p:
            check_expr(c, w + ".when", p["when"], vars_.keys())
        ws = p.get("widgets")
        if not isinstance(ws, list) or not ws:
            c.err("%s: widgets must be a non-empty list (never an empty page)" % w)
            continue
        uses_live = False
        for wi, x in enumerate(ws):
            ww = "%s.widgets[%d]" % (w, wi)
            if not isinstance(x, dict):
                c.err("%s: must be an object" % ww)
                continue
            t = x.get("type")
            if t not in WIDGETS:
                c.err("%s: type %r (one of %s)" % (ww, t, " ".join(sorted(WIDGETS))))
                continue
            ww += "(%s)" % t
            if "when" in x:
                check_expr(c, ww + ".when", x["when"], vars_.keys())
            used = set()
            if t in ("number", "text", "bar", "flags", "count", "grid"):
                if "value" not in x:
                    c.err("%s: value missing" % ww)
                elif isinstance(x["value"], (int, float)) and not isinstance(x["value"], bool):
                    pass
                else:
                    used |= check_expr(c, ww + ".value", x["value"], vars_.keys())
            if t == "number" and "format" in x:
                f = x["format"]
                if not isinstance(f, str) or (f not in NAMED_FORMATS and "{}" not in f):
                    c.err("%s: format %r (a pattern with {} or one of %s)" % (ww, f, " ".join(sorted(NAMED_FORMATS))))
            if t == "text" and "enum" in x and x["enum"] not in enums:
                c.err("%s: enum %r not defined" % (ww, x["enum"]))
            if t == "bar":
                if "max" not in x:
                    c.err("%s: max missing" % ww)
                elif isinstance(x["max"], str):
                    used |= check_expr(c, ww + ".max", x["max"], vars_.keys())
                elif not isinstance(x["max"], (int, float)) or x["max"] <= 0:
                    c.err("%s: max must be > 0" % ww)
                if "color" in x and not re.match(r"^#[0-9a-fA-F]{6}$", str(x["color"])):
                    c.err("%s: color must be #rrggbb" % ww)
            if t == "count":
                if "total" not in x:
                    c.err("%s: total missing" % ww)
                elif isinstance(x["total"], str):
                    used |= check_expr(c, ww + ".total", x["total"], vars_.keys())
                elif not isinstance(x["total"], (int, float)) or x["total"] <= 0:
                    c.err("%s: total must be > 0" % ww)
            if t in ("flags", "grid"):
                it = x.get("items")
                if not isinstance(it, list) or not it or any(not isinstance(s, str) for s in it):
                    c.err("%s: items must be a non-empty list of strings" % ww)
                elif len(it) > 32:
                    c.err("%s: at most 32 items (one int's bits): split into several widgets" % ww)
            if t == "static":
                if not isinstance(x.get("text"), str) or not x["text"].strip():
                    c.err("%s: text missing" % ww)
            if t == "kv":
                rows = x.get("rows")
                if not isinstance(rows, list) or not rows or any(not (isinstance(r, list) and len(r) == 2 and all(isinstance(s, str) for s in r)) for r in rows):
                    c.err("%s: rows must be a non-empty list of [label, text]" % ww)
            if t == "list":
                stride, n = x.get("stride"), x.get("n")
                fields = x.get("fields")
                if not isinstance(stride, int) or stride <= 0:
                    c.err("%s: stride must be a positive int" % ww)
                    stride = 0
                nmax = 0
                if isinstance(n, int) and not isinstance(n, bool):
                    if not 1 <= n <= 64:
                        c.err("%s: n must be 1-64" % ww)
                    nmax = n
                elif isinstance(n, str):
                    used |= check_expr(c, ww + ".n", n, vars_.keys())
                    nmax = 64
                else:
                    c.err("%s: n must be a number or an expression" % ww)
                if not isinstance(fields, list) or not fields:
                    c.err("%s: fields must be a non-empty list" % ww)
                    fields = []
                fnames = []
                for fi, f in enumerate(fields):
                    fw = "%s.fields[%d]" % (ww, fi)
                    if not isinstance(f, dict):
                        c.err("%s: must be an object" % fw)
                        continue
                    nm = f.get("name")
                    if not isinstance(nm, str) or not NAME_RE.match(nm) or nm in RESERVED:
                        c.err("%s: name must be an identifier" % fw)
                    else:
                        fnames.append(nm)
                    if "label" not in f:
                        c.warn("%s: no label (the name is shown)" % fw)
                    try:
                        off = parse_int(f.get("off", 0))
                    except Exception as e:
                        c.err("%s: off: %s" % (fw, e))
                        continue
                    sz = check_mem(c, fw, f, enums, allow_ptr=False)
                    if stride and off + sz > stride:
                        c.err("%s: off %d + %d bytes past the stride %d" % (fw, off, sz, stride))
                    if "enum" in f and f["enum"] not in enums:
                        c.err("%s: enum %r not defined" % (fw, f["enum"]))
                    if "format" in f and (not isinstance(f["format"], str) or (f["format"] not in NAMED_FORMATS and "{}" not in f["format"])):
                        c.err("%s: bad format %r" % (fw, f["format"]))
                if "rowWhen" in x:
                    check_expr(c, ww + ".rowWhen", x["rowWhen"], vars_.keys(), fnames)
                b = x.get("base")
                try:
                    if isinstance(b, dict) and "ptr" in b:
                        pa = parse_int(b["ptr"][0]); parse_int(b["ptr"][1])
                        if region(pa, 4) is None:
                            c.err("%s: base ptr 0x%08X not readable" % (ww, pa))
                        if region(pa, 4) != "ROM":
                            uses_live = True
                    else:
                        ba = parse_int(b)
                        r = region(ba, stride * max(nmax, 1))
                        if not r:
                            c.err("%s: base 0x%08X + %d*%d outside one readable range" % (ww, ba, stride, nmax))
                        elif r != "ROM":
                            uses_live = True
                        if stride * nmax > 4096:
                            c.err("%s: table larger than 4096 bytes" % ww)
                except Exception as e:
                    c.err("%s: base: %s" % (ww, e))
            if used & live:
                uses_live = True
        if p.get("reference") is False and not uses_live:
            c.warn("%s: reference false but nothing on it is read from memory" % w)
        if p.get("id") == "battle" and "when" not in p and uses_live:
            c.warn("page battle: no 'when' (it is always shown)")
    if d.get("quality") in ("live-verified", "live-unverified") and not live:
        c.err("quality %s but no var reads EWRAM/IWRAM (use reference-only)" % d.get("quality"))
    # slug
    if path and base.endswith(".json") and isinstance(m.get("zip"), str):
        want = re.sub(r"[^a-z0-9]+", "-", re.sub(r"\.zip$", "", m["zip"], flags=re.I).lower()).strip("-") + ".json"
        alt = re.sub(r"[^a-z0-9]", "-", re.sub(r"\.zip$", "", m["zip"], flags=re.I).lower()) + ".json"  # (v1's per-character slug)
        if base not in (want, alt, "tr-" + want, "tr-" + alt):
            c.warn("file name %s; the slug of match.zip is %s" % (base, want))
    return c


def main(argv):
    quiet = "-q" in argv
    as_json = "--json" in argv
    files = [a for a in argv if not a.startswith("-")]
    if not files:
        here = os.path.dirname(os.path.abspath(__file__))
        dd = os.path.join(here, "defs")
        files = sorted(os.path.join(dd, f) for f in os.listdir(dd) if f.endswith(".json"))
    bad = 0
    for f in files:
        c = check_def(f)
        if c.errors:
            bad += 1
        if as_json:
            print(json.dumps({"file": f, "ok": not c.errors, "errors": c.errors, "warnings": c.warnings}))
            continue
        if c.errors or (c.warnings and not quiet) or not quiet:
            print("%s %s" % ("FAIL" if c.errors else "ok  ", os.path.basename(f)))
            for e in c.errors:
                print("    error: " + e)
            if not quiet:
                for w in c.warnings:
                    print("    warn:  " + w)
    if not as_json:
        print("%d files, %d valid, %d invalid" % (len(files), len(files) - bad, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
