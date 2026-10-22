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
# Only reads the document (it is a zip of XML; nothing in it is run). Run with python3 -I.
import re, sys, zipfile
import xml.etree.ElementTree as ET

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"
PARTS = [("BananaOS menu", "menu"), ("BananaOS settings", "settings"), ("BananaDS", "bananads"),
         ("SuperDrastic", "superdrastic"), ("System names", "systems")]


def text_of(p):
    out = []
    for n in p.iter():
        if n.tag == W + "t":
            out.append(n.text or "")
        elif n.tag == W + "tab":
            out.append("\t")
    return "".join(out)


def main():
    doc, code, out = sys.argv[1], sys.argv[2], sys.argv[3]
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
            language, by = m.group(1), m.group(2).strip()
    lines, first = [], {}
    for part, where, en, tr in rows:
        if not tr.strip():
            continue
        for s in (en, tr):
            if "\t" in s or "\n" in s:
                sys.exit("a tab or line break in a cell: %r" % s)
        key = (part, en)
        context = ""
        if key in first and first[key] != tr:
            context = where
        first.setdefault(key, tr)
        if key in first and first[key] == tr and context == "" and any(l[0] == part and l[2] == en for l in lines):
            continue	# (the same line and translation once more: once is enough)
        lines.append((part, context, en, tr))
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
