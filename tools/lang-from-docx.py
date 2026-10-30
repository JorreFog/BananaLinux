#!/usr/bin/env python3
# lang-from-docx.py <translated .doc(x)> <language code> <out.tsv>: a translation document filled in by a translator
# (the Word file made for them: an introduction, then one table per part with the columns #, Where, English,
# Translation, Notes) as the language file banana-ui and the emulators read, /usr/share/banana/lang/<code>.tsv.
#
# The file: comment lines (#), "#: key = value" lines (language: its name in itself, language-en: in English;
# translated-by: who made it, shown in Settings beside the language), then part<TAB>context<TAB>English<TAB>translation per line. The parts:
# menu, settings, systems (banana-ui), bananads, superdrastic. Context is empty for most lines; when one part has the
# same English line translated two ways, the first is the default and the others carry the document's Where column,
# for the code to ask for that one (banana::tr(context, text)).
#
# With --merge=BASE.tsv (a document with only the new and changed lines): BASE with the document's lines put in.
#
# Only reads the document (it is a zip of XML; nothing in it is run). Run with python3 -I.
import os, re, sys, zipfile
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lang_docx import PARTS, CONTEXTS  # noqa: E402	(the document's part headings, the Pokémon companion's among them)

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
PLACEHOLDER = re.compile(r"\{[^}]*\}|%[-0-9.]*l*[dsuxfc]")
QUOTES = str.maketrans("\u2018\u2019\u201c\u201d", "''\"\"")


def text_of(p):
    out = []
    for n in p.iter():
        if n.tag == W + "t":
            out.append(n.text or "")
        elif n.tag == W + "tab":
            out.append("\t")
    return "".join(out)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--merge=")]
    merge = next((a[8:] for a in sys.argv[1:] if a.startswith("--merge=")), None)
    doc, code, out = args[0], args[1], args[2]
    with zipfile.ZipFile(doc) as z:
        body = ET.fromstring(z.read("word/document.xml")).find(W + "body")
    part, intro, rows = None, [], []
    for el in body:
        if el.tag == W + "p":
            t = text_of(el).strip()
            for head, name in PARTS:
                if t.startswith(head):
                    part = name
                    break
            else:
                if part is None and t:
                    intro.append(t)
        elif el.tag == W + "tbl" and part:
            for tr in el.findall(W + "tr")[1:]:	# (the first row is the header)
                cells = ["\n".join(text_of(p) for p in tc.findall(W + "p")) for tc in tr.findall(W + "tc")]
                if len(cells) >= 4:
                    rows.append((part, cells[1].strip(), cells[2], cells[3]))
    # the document's own line naming the language and the translator: "Language: <name> Translated by:<who>"
    language, by = code, ""
    for t in intro:
        m = re.match(r"Language:\s*(.*?)\s*Translated by:\s*(.*)$", t)
        if m:
            language, by = m.group(1) or code, m.group(2).strip()	# (left empty: the code)
    lines, first = [], {}
    for part, where, en, tr in rows:
        if not tr.strip():
            continue
        for s in (en, tr):
            if "\t" in s or "\n" in s:
                sys.exit("a tab or line break in a cell: %r" % s)
        # placeholders as the English has them: Word's autocorrect turns a placeholder's ' into ’ ({either(x, '0')},
        # {package's handheld}), which then matches nothing; put back, and any other difference said
        tr = re.sub(r"\{[^}]*\}", lambda m: m.group(0).translate(QUOTES), tr)
        if sorted(PLACEHOLDER.findall(en)) != sorted(PLACEHOLDER.findall(tr)):
            print("placeholders differ (%s): %r -> %r" % (part, en, tr), file=sys.stderr)
        key = (part, en)
        context = ""
        if where in CONTEXTS:
            context = where	# (a line the code asks for in its context: always that one, "Dark" the type is not the theme)
            key = (part, where, en)
        elif key in first and first[key] != tr:
            context = where
        first.setdefault(key, tr)
        if key in first and first[key] == tr and context == "" and any(l[0] == part and l[2] == en for l in lines):
            continue	# (the same line and translation once more: once is enough)
        lines.append((part, context, en, tr))
    if merge:
        # --merge=BASE.tsv: a document with only some lines (the new and changed ones) into the language file there is:
        # its lines replace the same lines of BASE, the others are added at the end; BASE's header and order stay
        base, at = [], {}
        for l in open(merge, encoding="utf-8"):
            l = l.rstrip("\n")
            p = l.split("\t")
            if not l.startswith("#") and len(p) >= 4:
                at[(p[0], p[1], p[2])] = len(base)
            base.append(l)
        added = 0
        for l in lines:
            k = (l[0], l[1], l[2])
            if k in at:
                base[at[k]] = "\t".join(l)
            else:
                if not added:
                    base.append("# (from %s)" % doc.rsplit("/", 1)[-1])
                base.append("\t".join(l)); added += 1
        with open(out, "w", encoding="utf-8") as f:
            f.write("\n".join(base) + "\n")
        print("%s: %s merged: %d lines replaced, %d added" % (out, merge, len(lines) - added, added))
        return
    with open(out, "w", encoding="utf-8") as f:
        f.write("# BananaOS translation: %s\n" % language)
        if by:
            f.write("# Translated by %s.\n" % by)
        f.write("# Made by tools/lang-from-docx.py from %s. Columns: part, context, English, translation.\n" % doc.rsplit("/", 1)[-1])
        # (the language's own name for Settings: "Simplified Chinese(简体中文)" -> 简体中文)
        m = re.match(r"(.*?)\s*[(（](.+)[)）]\s*$", language)
        f.write("#: language = %s\n" % (m.group(2) if m else language))
        if m:
            f.write("#: language-en = %s\n" % m.group(1))
        if by:
            f.write("#: translated-by = %s\n" % by)
        for l in lines:
            f.write("\t".join(l) + "\n")
    print("%s: %d lines (%s), translated by %s" % (out, len(lines), language, by or "?"))


main()
