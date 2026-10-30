#!/usr/bin/env python3
# pseudo-lang.py <strings.tsv | language.tsv> <out.tsv>: a made-up language for layout tests, "Псевдо": every English
# line in Cyrillic letters and about 1.6x as long (as Russian often is), placeholders ({name}, %d, %s...) kept.
# Input: tools/lang-extract.py's output (part, where, English, sources) or a language file (part, context, English,
# translation). Run with python3 -I.
import re, sys
LAT = "abcdefghijklmnopqrstuvwxyz"
CYR = ["а", "б", "ц", "д", "е", "ф", "г", "х", "и", "й", "к", "л", "м", "н", "о", "п", "я", "р", "с", "т", "у", "в", "ш", "кс", "ы", "з"]
CY = {}
for a, b in zip(LAT, CYR): CY[a] = b; CY[a.upper()] = b.upper()
CY["O"], CY["P"] = "Ю", "Р"	# (Pixelify Sans has no capital О and П)
TOKEN = re.compile(r"(\{[a-z_]+\}|%[-+ #0]*\d*(?:\.\d+)?[lhzj]*[dsuxXfgceip%])")
def word(w):
    c = "".join(CY.get(ch, ch) for ch in w)
    letters = sum(ch.isalpha() for ch in w)
    return c + ("ова" * ((letters * 6 // 10 + 2) // 3) if letters >= 2 else "")
def pseudo(s):
    out = []
    for part in TOKEN.split(s):
        if TOKEN.fullmatch(part or "x") and part: out.append(part); continue
        out.append(re.sub(r"[A-Za-z]+", lambda m: word(m.group(0)), part))
    return "".join(out)
src, dst = sys.argv[1], sys.argv[2]
seen = set()
with open(src, encoding="utf-8") as f, open(dst, "w", encoding="utf-8") as o:
    o.write("# a made-up language for layout tests (ui/tests/pseudo-lang.py)\n#: language = Псевдо\n#: language-en = Pseudo\n#: translated-by = ui/tests\n")
    for ln in f:
        if ln.startswith("#"): continue
        c = ln.rstrip("\n").split("\t")
        if len(c) < 3: continue
        part, ctx, en = c[0], c[1], c[2]
        if len(c) == 4 and not ctx.startswith("ui/") and "/" not in c[3]: ctx = ctx   # (a language file: keep its context)
        else: ctx = ""                                                            # (extract output: "where" isn't a context)
        if (part, ctx, en) in seen: continue
        seen.add((part, ctx, en))
        o.write("%s\t%s\t%s\t%s\n" % (part, ctx, en, pseudo(en)))
