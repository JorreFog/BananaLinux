# lang_docx.py: reading and writing BananaOS translation documents (the Word file a translator fills in: a title, an
# introduction, then one table per part with the columns #, Where, English, Translation, Notes). Used by
# tools/lang-to-docx.py (writes the next document from the last one, which is its template: same page, fonts, styles
# and yellow column) and tools/lang-from-docx.py's parts list.
#
# Only reads the documents (a zip of XML; nothing in it is run).
import re, zipfile
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"

# a document heading -> the language file's part (the first whose words start the heading)
PARTS = [("BananaOS menu", "menu"), ("BananaOS settings", "settings"), ("Pokémon companion", "settings"),
         ("BananaDS", "bananads"), ("SuperDrastic", "superdrastic"), ("System names", "systems")]


# Where values that are a context the code asks for (lang::tr(context, text)): such a line is written with it always
CONTEXTS = {"Pokémon types"}


def part_of(heading):
    for head, name in PARTS:
        if heading.startswith(head):
            return name
    return None


def text_of(p):
    out = []
    for n in p.iter():
        if n.tag == W + "t":
            out.append(n.text or "")
        elif n.tag == W + "tab":
            out.append("\t")
    return "".join(out)


def read(path):
    """(intro paragraphs, [{"heading", "part", "rows": [(num, where, english, translation, notes)]}])"""
    with zipfile.ZipFile(path) as z:
        body = ET.fromstring(z.read("word/document.xml")).find(W + "body")
    intro, sections, cur = [], [], None
    for el in body:
        if el.tag == W + "p":
            t = text_of(el).strip()
            if part_of(t) and len(t) < 100:	# (a part's heading: its style id is whatever the last editor's Word made it)
                cur = {"heading": t, "part": part_of(t), "rows": []}
                sections.append(cur)
            elif cur is None and t:
                intro.append(t)
        elif el.tag == W + "tbl" and cur:
            for tr in el.findall(W + "tr")[1:]:
                cells = ["\n".join(text_of(p) for p in tc.findall(W + "p")) for tc in tr.findall(W + "tc")]
                if len(cells) >= 4 and re.fullmatch(r"\d+", cells[0].strip()):
                    cells += [""] * (5 - len(cells))
                    cur["rows"].append((int(cells[0]), cells[1].strip(), cells[2], cells[3].strip(), cells[4].strip()))
    return intro, sections


# ---- writing -----------------------------------------------------------------------------------------------------------
def _run(text, rpr=""):
    sp = ' xml:space="preserve"' if text != text.strip() or "  " in text else ""
    return "<w:r><w:rPr>%s</w:rPr><w:t%s>%s</w:t></w:r>" % (rpr, sp, escape(text))


class Template:
    """the parts of the last document reused for the next: the page, the intro's paragraphs, a heading, the tables"""

    def __init__(self, path):
        with zipfile.ZipFile(path) as z:
            self.files = {n: z.read(n) for n in z.namelist()}
        d = self.files["word/document.xml"].decode("utf-8")
        self.doc = d
        b0 = d.index("<w:body>") + len("<w:body>")
        self.head, self.tail = d[:b0], d[d.rindex("<w:sectPr"):]
        body = d[b0:d.rindex("<w:sectPr")]
        # top-level paragraphs and tables, in order
        self.items = []
        i = 0
        while i < len(body):
            if body.startswith("<w:p>", i) or body.startswith("<w:p ", i):
                j = body.index("</w:p>", i) + 6
                self.items.append(("p", body[i:j]))
            elif body.startswith("<w:tbl>", i):
                j = body.index("</w:tbl>", i) + 8
                self.items.append(("tbl", body[i:j]))
            else:
                j = body.find("<", i + 1)
                j = len(body) if j < 0 else j
            i = j
        self.tables = [x for k, x in self.items if k == "tbl"]
        paras = [x for k, x in self.items if k == "p"]
        self.h1 = next(p for p in paras if 'w:val="Heading1"' in p)
        self.h2 = next(p for p in paras if 'w:val="Heading2"' in p)
        self.body = next(p for p in paras if 'w:val="BodyText"' in p and "numPr" not in p and "Language:" not in p
                         and "<w:t" in p)
        self.bullet = next(p for p in paras if "numPr" in p)
        self.lang = next(p for p in paras if "Language:" in p)
        self.end_para = paras[-1]

    def para(self, kind, text, bold_prefix=None, space_before=False):
        """a paragraph like the template's heading 1/2, body text or bullet, with new text"""
        tpl = {"h1": self.h1, "h2": self.h2, "body": self.body, "bullet": self.bullet}[kind]
        ppr = re.search(r"<w:pPr>.*?</w:pPr>", tpl).group(0)
        if space_before:
            ppr = re.sub(r"<w:spacing [^>]*/>", "", ppr).replace("<w:bidi", '<w:spacing w:before="360" w:after="120"/><w:bidi', 1)
        runs = ""
        if bold_prefix:
            runs += _run(bold_prefix, "<w:b/>")
        runs += _run(text)
        return "<w:p>%s%s</w:p>" % (ppr, runs)

    def lang_para(self, language="", by=""):
        """the "Language: ... Translated by: ..." line (tools/lang-from-docx.py reads it)"""
        if not language and not by:
            return self.lang
        return "<w:p>%s%s%s%s%s</w:p>" % (re.search(r"<w:pPr>.*?</w:pPr>", self.lang).group(0), _run("Language:", "<w:b/>"),
                                          _run(" " + language + "      "), _run("Translated by:", "<w:b/>"), _run(" " + by))

    def table(self, which, rows):
        """a table like the template's table number `which`: rows = [(num, where, english, translation, notes, flags)]
        (flags: "new"/"changed"/"unused" shade the number cell)"""
        t = self.tables[which]
        trs = re.findall(r"<w:tr>.*?</w:tr>", t, re.S)
        pre = t[:t.index("<w:tr>")]
        header, mid, last = trs[0], trs[1], trs[-1]
        out = [pre, header]
        for k, r in enumerate(rows):
            tpl = last if k == len(rows) - 1 else mid
            out.append(self._row(tpl, r))
        out.append("</w:tbl>")
        return "".join(out)

    def _row(self, tpl, r):
        num, where, en, tr, notes = r[:5]
        flag = r[5] if len(r) > 5 else ""
        tcs = re.findall(r"<w:tc>.*?</w:tc>", tpl, re.S)
        pre = tpl[:tpl.index("<w:tc>")]
        cells = []
        content = [(str(num), '<w:color w:val="555555"/><w:sz w:val="20"/>'),
                   (where, '<w:color w:val="444444"/><w:sz w:val="20"/>'),
                   (en, ""), (tr if tr else " ", ""), (notes, '<w:color w:val="444444"/><w:sz w:val="14"/>')]
        for k, tc in enumerate(tcs[:5]):
            tcpr = re.search(r"<w:tcPr>.*?</w:tcPr>", tc, re.S).group(0)
            if k == 0 and flag:
                fill = {"new": "D9EAD3", "changed": "FCE5CD", "unused": "E0E0E0"}.get(flag)
                if fill:
                    tcpr = tcpr.replace("</w:tcPr>", '<w:shd w:fill="%s" w:val="clear"/></w:tcPr>' % fill)
            text, rpr = content[k]
            ppr = '<w:pPr><w:pStyle w:val="TableContents"/><w:bidi w:val="0"/><w:spacing w:before="0" w:after="283"/><w:rPr></w:rPr></w:pPr>'
            if k == 4 and not text:
                # (an empty note as the template has one: a tiny empty paragraph, not the template row's own note)
                cells.append('<w:tc>%s<w:p><w:pPr><w:pStyle w:val="TableContents"/><w:bidi w:val="0"/><w:snapToGrid w:val="false"/>'
                             '<w:spacing w:before="0" w:after="283"/><w:rPr><w:sz w:val="4"/><w:szCs w:val="4"/></w:rPr></w:pPr>'
                             '<w:r><w:rPr><w:sz w:val="4"/><w:szCs w:val="4"/></w:rPr></w:r></w:p></w:tc>' % tcpr)
                continue
            paras = "".join("<w:p>%s%s</w:p>" % (ppr, _run(line, rpr)) for line in text.split("\n"))
            cells.append("<w:tc>%s%s</w:tc>" % (tcpr, paras))
        return pre + "".join(cells) + "</w:tr>"

    def write(self, path, body_xml, title=None):
        d = self.head + body_xml + self.end_para + self.tail
        files = dict(self.files)
        files["word/document.xml"] = d.encode("utf-8")
        if title and "docProps/core.xml" in files:
            core = files["docProps/core.xml"].decode("utf-8")
            core = re.sub(r"<dc:title>.*?</dc:title>", "<dc:title>%s</dc:title>" % escape(title), core)
            files["docProps/core.xml"] = core.encode("utf-8")
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            for n, data in files.items():
                z.writestr(n, data)
