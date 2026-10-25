#!/usr/bin/env python3
# lang-roundtrip.py DOC.docx LANG.tsv: the round trip of a translation document. Fills DOC's Translation column from
# LANG.tsv (as a translator would), reads it back with tools/lang-from-docx.py and checks that every line of LANG.tsv
# whose English the document has comes back with the same translation (banana-ui's three parts compared as one, as
# it loads them). Exit 1 on a difference. Run with python3 -I.
import os, re, subprocess, sys, tempfile, zipfile
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import lang_docx  # noqa: E402

doc, tsvp = sys.argv[1], sys.argv[2]
pool = lambda p: "ui" if p in ("menu", "settings", "systems") else p
tsv = {}
for l in open(tsvp, encoding="utf-8"):
    if l.startswith("#") or not l.strip():
        continue
    p = l.rstrip("\n").split("\t")
    tsv[(pool(p[0]), p[1], p[2])] = p[3]
intro, secs = lang_docx.read(doc)
fill = {}
for s in secs:
    for num, where, en, tr, notes in s["rows"]:
        t = tsv.get((pool(s["part"]), where, en)) or tsv.get((pool(s["part"]), "", en))
        if t:
            fill[num] = t
with zipfile.ZipFile(doc) as z:
    files = {n: z.read(n) for n in z.namelist()}
d = files["word/document.xml"].decode("utf-8")


def row(m):
    tr = m.group(0)
    tcs = re.findall(r"<w:tc>.*?</w:tc>", tr, re.S)
    if len(tcs) < 4:
        return tr
    num = re.sub(r"<[^>]+>", "", tcs[0]).strip()
    if not num.isdigit() or int(num) not in fill:
        return tr
    t = "<w:t>%s</w:t>" % escape(fill[int(num)]).replace("\\", "\\\\")
    if re.search(r"<w:t(?: [^>]*)?>", tcs[3]):
        new = re.sub(r"<w:t(?: [^>]*)?>[^<]*</w:t>", t, tcs[3], count=1)
    else:	# (an empty cell as LibreOffice or Word saves it: no run at all)
        new = re.sub(r"(<w:p(?: [^>]*)?>(?:<w:pPr>.*?</w:pPr>)?)", lambda m: m.group(1) + "<w:r>" + t + "</w:r>", tcs[3], count=1, flags=re.S)
    return tr.replace(tcs[3], new, 1)


d = re.sub(r"<w:tr>.*?</w:tr>", row, d, flags=re.S)
files["word/document.xml"] = d.encode("utf-8")
tmp = tempfile.mkdtemp()
filled, out = os.path.join(tmp, "filled.docx"), os.path.join(tmp, "out.tsv")
with zipfile.ZipFile(filled, "w", zipfile.ZIP_DEFLATED) as z:
    for n, b in files.items():
        z.writestr(n, b)
subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(HERE), "lang-from-docx.py"), filled, "zh-cn", out], check=True)
back = {}
for l in open(out, encoding="utf-8"):
    if l.startswith("#"):
        continue
    p = l.rstrip("\n").split("\t")
    back.setdefault((pool(p[0]), p[2]), {})[p[1]] = p[3]
docen = {(pool(s["part"]), r[1] if r[1] in lang_docx.CONTEXTS else "", r[2]) for s in secs for r in s["rows"]}
bad = ok = skipped = 0
for (P, ctx, en), t in tsv.items():
    if (P, ctx if ctx in lang_docx.CONTEXTS else "", en) not in docen:
        skipped += 1
        continue
    got = back.get((P, en), {})
    if got.get(ctx) == t or (ctx and got.get("") == t) or (not ctx and t in got.values() and len(set(got.values())) == 1):
        ok += 1
    else:
        bad += 1
        print("DIFF %s | %s | %s: want %s, got %s" % (P, ctx, en[:60], t, got))
print("round trip: %d lines back as they were, %d different, %d not in the document (old English)" % (ok, bad, skipped))
sys.exit(1 if bad else 0)
