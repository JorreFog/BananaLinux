#!/usr/bin/env python3
# lang-to-docx.py: the next translation document, from the last one and the sources.
#
#   lang-to-docx.py --template BananaOS-0.3.0-translation.docx --version 0.4.3 --sd ~/sd-043 --bd ~/bd-043 \
#       --out BananaOS-0.4.3-translation.docx [--new-only BananaOS-0.4.3-translation-new-lines.docx \
#       --tsv ui/assets/lang/zh-cn.tsv --translator-doc BananaOS-0.3.0-translation-zh-cn.docx] [--fixes FILE] [--list FILE]
#
# The last document (a .docx: convert a .doc first, soffice --convert-to docx) is the template: same page, styles and
# yellow column, and its numbers stay. tools/lang-extract.py finds every line on screen now; each line of the last
# document is then
#   kept     still shown (as it is, or built from the same pieces): same number, same text
#   changed  its English was reworded: same number, the new English, a note with the old one
#   unused   no longer shown: kept in its place with the note "no longer used" (not deleted)
# and the lines found that the last document doesn't have are new: numbers after the last one, in a "new since"
# table at the end of their part (and a part of their own for the Pokémon companion).
# --fixes: corrections of that matching, one a line: "<num> TAB changed TAB <new English>", "<num> TAB unused",
#   "<num> TAB kept", "<num> TAB notshown" (a line the last document had by mistake: a command or a log line).
# --new-only: a second document with only the new and changed lines, for the translator of the last one; with --tsv
#   its Translation column is filled in from that language file where it has the line: the lines that are exactly as
#   the translator's own document (--translator-doc) had them stay theirs, every other one is marked a draft to check.
#
# Only reads the documents and the sources. Run with python3 -I.
import argparse, collections, difflib, importlib.util, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lang_docx  # noqa: E402

spec = importlib.util.spec_from_file_location("lang_extract", os.path.join(HERE, "lang-extract.py"))
LE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(LE)

PRODUCTS = ["BananaOS", "BananaDS", "SuperDrastic", "DraStic", "RetroArch", "RetroAchievements", "Wi-Fi", "Banana",
            "GitHub", "Bluetooth", "melonDS", "libretro", "TwilightBoxart", "Twilight", "ScreenScraper", "PokeDaisy",
            "PortMaster", "ROCKNIX", "gpSP", "mGBA", "aptX", "LDAC", "SBC", "Pokémon", "Action Replay", "Poketool"]
BUTTON_RE = re.compile(r"(?<!\w)(F \+|F / L3|START|SELECT|L3|R3|L2|R2|A again|A on|B goes|X:|Y there|L / R)(?!\w)|\b[ABXY](?= (?:on|types|picks|starts|finishes|cancels|checks|again|restarts|downloads|keeps|fetches|deletes|switches|searches|pairs|then))")


def pool(part):
    """banana-ui loads the menu, settings and systems parts as one table"""
    return "ui" if part in ("menu", "settings", "systems") else part


def placeholders(s):
    return [m for m in re.findall(r"\{[^{}]+\}|%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXsfc]|%[HMS]", s)]


def auto_note(en, where=""):
    notes = []
    ph = []
    for p in placeholders(en):
        if p not in ph:
            ph.append(p)
    if ph:
        notes.append("Keep %s as written: the system puts a value there." % ", ".join(ph))
    if BUTTON_RE.search(en):
        notes.append("Button names (A, B, X, Y, L, R, F, L3, Start) stay as printed on the handheld.")
    if any(re.search(r"(?<![\w-])%s(?![\w-])" % re.escape(p), en) for p in PRODUCTS):
        notes.append("Product and service names stay in English.")
    if where == "Menu: systems":
        notes = ["Usually kept as is."]
    return " ".join(notes)


def fragments(s):
    return [f.strip() for f in re.split(r"\{[^{}]*\}|%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[a-zA-Z]|<[^<>]*>", s)
            if len(f.strip(" .,:;()·")) >= 3]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--template", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--bl", default=os.path.dirname(HERE))
    ap.add_argument("--sd")
    ap.add_argument("--bd")
    ap.add_argument("--out", required=True)
    ap.add_argument("--new-only")
    ap.add_argument("--tsv")
    ap.add_argument("--translator-doc")
    ap.add_argument("--fixes")
    ap.add_argument("--list", help="every line with its status, as text")
    a = ap.parse_args()

    intro, sections = lang_docx.read(a.template)
    old_version = re.match(r"BananaOS ([\w.-]+)", intro[0]).group(1) if intro and intro[0].startswith("BananaOS") else "the last version"
    lines = LE.extract(a)
    srcs = LE.sources_text(a)
    fixes = {}
    if a.fixes:
        for l in open(a.fixes, encoding="utf-8"):
            if l.strip() and not l.startswith("#"):
                p = l.rstrip("\n").split("\t")
                fixes[int(p[0])] = p[1:]

    by_norm = collections.defaultdict(list)
    for l in lines:
        if l["where"] in LE.CONTEXT_WHERE:
            continue	# (a line for one context only: never the last document's line of the same English)
        by_norm[(pool(l["part"]), LE.norm(l["text"]))].append(l)
        if pool(l["part"]) == "ui":	# (banana-ui finds "START" under "Start"; the emulators don't)
            by_norm[(pool(l["part"]), LE.norm(l["text"]).lower())].append(l)
    covered = set()
    rows = []	# [section index, row dict]
    maxnum = 0
    for si, sec in enumerate(sections):
        P = pool(sec["part"])
        for num, where, en, tr, notes in sec["rows"]:
            maxnum = max(maxnum, num)
            r = {"num": num, "where": where, "en": en, "notes": notes, "sec": si, "part": sec["part"], "status": None}
            fx = fixes.get(num)
            hit = by_norm.get((P, LE.norm(en))) or (by_norm.get((P, LE.norm(en).lower())) if P == "ui" else None)
            if fx:
                r["status"] = fx[0]
                if fx[0] == "changed":
                    r["new_en"] = fx[1]
                    for l in by_norm.get((P, LE.norm(fx[1])), []):
                        covered.add(id(l)); r["match"] = l
            elif hit:
                r["status"] = "kept"; r["match"] = hit[0]
                for l in hit:
                    covered.add(id(l))
            else:
                fr = fragments(en)
                if placeholders(en) or re.search(r"<[^<>]*>", en):
                    found = fr and all(f in srcs[P] for f in fr)
                else:	# (a whole line: as a literal of its own, one the filters leave out: "F + START", "dev")
                    found = ('"%s"' % en) in srcs[P] or ("'%s'" % en) in srcs[P] or ("= %s\n" % en) in srcs[P]
                if found:
                    r["status"] = "kept"	# (built from pieces the code still has)
            rows.append(r)
    # reworded lines: the closest line found that nothing else took, if close enough
    for r in rows:
        if r["status"]:
            continue
        P = pool(r["part"])
        best, score = None, 0.0
        for l in lines:
            if id(l) in covered or pool(l["part"]) != P:
                continue
            a_, b_ = LE.norm(r["en"]), LE.norm(l["text"])
            s = difflib.SequenceMatcher(None, a_, b_).ratio()
            if len(a_) >= 4 and (b_.startswith(a_ + ",") or a_.startswith(b_ + ",") or b_.startswith(a_ + " ") and len(a_) > 12):
                s = max(s, 0.9)	# ("Dubai" -> "Dubai, Baku")
            if s > score:
                best, score = l, s
        if best and score >= 0.72:
            r["status"], r["new_en"], r["match"] = "changed", best["text"], best
            covered.add(id(best))
        else:
            r["status"] = "unused"
    # the rest of what was found is new, unless it is a piece of a line the document has
    known = collections.defaultdict(list)
    for r in rows:
        if r["status"] in ("kept", "changed"):
            known[pool(r["part"])].append(r.get("new_en", r["en"]))
    # Where from the last document: the Where its lines from the same function had
    fn_where = collections.defaultdict(collections.Counter)
    for r in rows:
        if r.get("match") and r["status"] == "kept" and len({f.rsplit(":", 1)[0] for f in r["match"]["fn"]}) == 1:
            for fn in r["match"]["fn"]:
                fn_where[(fn, r["part"])][r["where"]] += 1
    new = []
    for l in lines:
        if id(l) in covered:
            continue
        P = pool(l["part"])
        fr = fragments(l["text"])
        if placeholders(l["text"]) and l["where"] not in LE.CONTEXT_WHERE and fr and any(all(f in k for f in fr) for k in known[P]):
            continue	# (a piece of a line the document has, or the same line with other placeholders)
        where, part = l["where"], l["part"]
        fixed_where = where in LE.CONTEXT_WHERE or any(f.endswith("\x00fixed") for f in l["fn"][:1])
        if not fixed_where:
            for fn in l["fn"]:
                cands = [(c, n) for (f, p), cnt in fn_where.items() if f == fn for c, n in cnt.items()]
                if cands:
                    where = max(cands, key=lambda x: x[1])[0]
                    break
        new.append({"where": where, "en": l["text"], "part": part, "src": l["src"]})
    # (the language file's spelling of a new line, when it has one: its placeholders were named by hand)
    others = collections.defaultdict(list)
    for n in new:
        others[pool(n["part"])].append(n["en"])
    new = [n for n in new if "{value}" not in n["en"] or not any(
        o != n["en"] and "{value}" not in o and all(f in o for f in fragments(n["en"])) for o in others[pool(n["part"])])]

    # ---- the document --------------------------------------------------------------------------------------------
    T = lang_docx.Template(a.template)
    out_sections = []	# (heading, desc, [rows], [new rows])
    sec_of_part = {}
    for si, sec in enumerate(sections):
        sec_of_part.setdefault(sec["part"], si)
    order = []
    for si, sec in enumerate(sections):
        order.append(("old", si))
        if sec["part"] == "settings":
            order.append(("poke", si))
    new_by = collections.defaultdict(list)
    for n in new:
        key = ("poke", sec_of_part["settings"]) if n["where"].startswith("Pokémon") else ("old", sec_of_part.get(n["part"], 0))
        new_by[key].append(n)
    # order new rows by Where (the last document's order first), then as found
    where_rank = {}
    for r in rows:
        where_rank.setdefault(r["where"], len(where_rank))
    num = maxnum
    counts = collections.OrderedDict()
    full_body, new_body = [], []

    tsv, mine = {}, set()
    if a.tsv:
        tsv = read_tsv(a.tsv)
    if a.translator_doc:
        _, tsecs = lang_docx.read(a.translator_doc)
        for s in tsecs:
            for n_, w_, en_, tr_, no_ in s["rows"]:
                if tr_.strip():
                    mine.add((pool(s["part"]), en_, tr_.strip()))

    tsv_norm = {}
    for (P_, c_, en_), t_ in tsv.items():
        tsv_norm.setdefault((P_, c_, LE.norm(en_)), (en_, t_))

    def tsv_english(part, en, context=""):
        """the language file's own spelling of a line (its placeholders' names), when it has it"""
        for c in ((context, "") if context in LE.CONTEXT_WHERE else ("",)):
            hit = tsv_norm.get((pool(part), c, LE.norm(en)))
            if hit:
                return hit[0]
        return en

    def translation(part, en, context=""):
        P = pool(part)
        for c in ((context, "") if context else ("",)):
            hit = tsv.get((P, c, en)) or (tsv_norm.get((P, c, LE.norm(en))) or (None, None))[1]
            if hit:
                return hit, (P, en, hit) in mine or (P, tsv_english(part, en, c), hit) in mine
        return "", False

    intro_lines = []
    total_old = sum(len(s["rows"]) for s in sections)
    for kind, si in order:
        sec = sections[si]
        name = "Pokémon companion" if kind == "poke" else sec["heading"]
        olds = [r for r in rows if r["sec"] == si] if kind == "old" else []
        news = sorted(new_by.get((kind, si), []), key=lambda n: (where_rank.get(n["where"], 999), n["where"] if n["where"] not in where_rank else ""))
        if not olds and not news:
            continue
        table_ix = min(si, len(T.tables) - 1)
        st = counts.setdefault(name, collections.Counter())
        full_rows, newdoc_rows = [], []
        for r in olds:
            st[r["status"]] += 1
            en, note = r["en"], r["notes"]
            if r["status"] == "changed":
                en = r["new_en"] = tsv_english(r["part"], r["new_en"], r["where"])
                note = ("Changed since %s: was '%s'. " % (old_version, r["en"]) + auto_note(en, r["where"])).strip()
            elif r["status"] == "unused":
                note = "No longer used in %s: no need to translate." % a.version
            elif r["status"] == "notshown":
                note = "Not looked up on its own (a command, a log line or a piece of a longer line): no need to translate."
            full_rows.append((r["num"], r["where"], en, "", note))
            if r["status"] == "changed":
                t, theirs = translation(r["part"], en, r["where"])
                pre = note
                if t and not theirs:
                    pre = "Draft by the BananaOS team (not %s's), please check. " % (a_by(a) or "the translator") + note
                elif t and theirs:
                    pre = "Your translation, already in BananaOS. " + note
                if not t:
                    old_t, theirs_old = translation(r["part"], r["en"], r["where"])
                    if old_t:
                        t = old_t
                        pre = ("Your translation of the old English%s: please update it to the new English. " %
                               ("" if theirs_old else " (with our edits)")) + note
                newdoc_rows.append((r["num"], r["where"], en, t, pre))
        new_rows_full, new_rows_newdoc = [], []
        for n in news:
            num += 1
            st["new"] += 1
            n["en"] = tsv_english(n["part"], n["en"], n["where"])
            note = auto_note(n["en"], n["where"])
            new_rows_full.append((num, n["where"], n["en"], "", note))
            t, theirs = translation(n["part"], n["en"], n["where"])
            pre = note
            if t and not theirs:
                pre = ("Draft by the BananaOS team (not %s's), please check. " % (a_by(a) or "the translator") + note).strip()
            elif t:
                pre = ("Your translation, already in BananaOS. " + note).strip()
            new_rows_newdoc.append((num, n["where"], n["en"], t, pre))
        # the full document
        heading = sec["heading"] if kind == "old" else "Pokémon companion (the second screen in Pokémon games)"
        full_body.append(T.para("h2", heading))
        if kind == "old" and sec["part"] == "systems":
            full_body.append(T.para("body", "The names of the systems and makers in the menu. These are product names and normally stay as they are; translate one only if your language has its own name for it."))
        if kind == "poke":
            full_body.append(T.para("body", "The second screen while a Pokémon game (Ruby, Sapphire, Emerald, FireRed, LeafGreen) runs in RetroArch: the party, the battle and the type matchups. Inspired by PokeDaisy by Lidor Itzhari. Use your language's official Pokémon terms where it has them."))
        if full_rows:
            full_body.append(T.table(table_ix, full_rows))
        if new_rows_full:
            if full_rows:
                full_body.append(T.para("body", " (%d lines): not in the %s document." % (len(new_rows_full), old_version),
                                        "New since %s" % old_version, space_before=True))
            full_body.append(T.table(table_ix, new_rows_full))
        # the new-lines document
        nd = newdoc_rows + new_rows_newdoc
        if nd:
            new_body.append(T.para("h2", heading))
            new_body.append(T.table(table_ix, nd))
        intro_lines.append((heading, st))

    tot = collections.Counter()
    for st in counts.values():
        tot.update(st)
    n_parts = len([1 for k, si in order if k == "old" and sections[si]["part"] != "systems"]) + (1 if new_by.get(("poke", sec_of_part["settings"])) else 0)
    sys_st = counts.get(next((s["heading"] for s in sections if s["part"] == "systems"), ""), collections.Counter())
    shown = tot["kept"] + tot["changed"] + tot["new"] - sys_st["kept"] - sys_st["changed"] - sys_st["new"]
    title = "BananaOS %s: text to translate" % a.version
    intro_text = ("Every piece of English text BananaOS shows on screen: its menu, settings and setup, the bottom screen "
                  "for RetroArch games (with the Pokémon companion), and the in-game menus and pop-ups of its two DS "
                  "emulators (BananaDS and SuperDrastic). %d lines in %d parts, plus the system names at the end. "
                  "Since %s: %d new lines, %d changed, %d no longer used (left in place, marked in the Notes)."
                  % (shown, n_parts, old_version, tot["new"], tot["changed"], tot["unused"] + tot["notshown"]))
    extra_bullets = [
        "Lines new since %s have numbers after %d and are in a \"New since %s\" table at the end of their part. A line whose English changed keeps its number; its Notes say what it was. A line marked \"no longer used\" needs no translation." % (old_version, maxnum, old_version),
    ]

    def head(intro_text, title, language="", by="", only_new=False):
        if only_new:
            extra_bullets[:] = ["Lines new since %s have numbers after %d. A line whose English changed keeps its number; its Notes say what it was. Where the Translation column is filled in, check it and change what is wrong: the Notes say whether it is your own translation or a draft by the BananaOS team." % (old_version, maxnum)]
        b = [T.para("h1", title), T.para("body", intro_text), T.lang_para(language, by), T.para("h2", "How to translate")]
        for k, p in T.items:
            if k == "p" and "numPr" in p:
                if "Not included" in p:
                    for e in extra_bullets:
                        b.append(T.para("bullet", e))
                    b.append(T.para("bullet", "Not included: RetroArch's and DraStic's own menus (RetroArch has its own languages; DraStic's menu cannot be translated), the apps from the Store (Poketool, Döda Kvarter, the Store itself: they have their own text), game names and descriptions, and messages that only go to log files."))
                else:
                    b.append(p)
        return "".join(b)

    T.write(a.out, head(intro_text, title) + "".join(full_body), title)
    print("%s: %s" % (a.out, intro_text))
    if a.new_only:
        lang, by = (tsv_header(a.tsv) if a.tsv else ("", ""))
        n_new = sum(st["new"] for st in counts.values()) + sum(st["changed"] for st in counts.values())
        ntitle = "BananaOS %s: new and changed text to translate" % a.version
        ntext = ("Only the lines new or changed since %s (%d lines; the full document is BananaOS-%s-translation.docx). "
                 "Numbers are the full document's. Where BananaOS already has a translation it is filled in: the Notes say "
                 "whether it is yours from %s or a draft by the BananaOS team for you to check." % (old_version, n_new, a.version, old_version))
        T.write(a.new_only, head(ntext, ntitle, lang, by, only_new=True) + "".join(new_body), ntitle)
        print("%s: %d lines" % (a.new_only, n_new))
    for name, st in counts.items():
        print("  %-60s kept %3d  changed %3d  new %3d  unused %3d  notshown %d" % (name, st["kept"], st["changed"], st["new"], st["unused"], st["notshown"]))
    if a.list:
        with open(a.list, "w", encoding="utf-8") as f:
            for r in rows:
                f.write("%d\t%s\t%s\t%s\t%s\n" % (r["num"], r["status"], r["where"], r["en"].replace("\n", "\\n"), r.get("new_en", "").replace("\n", "\\n")))
            n0 = maxnum
            for n in new:
                f.write("new\t\t%s\t%s\t%s\n" % (n["where"], n["en"].replace("\n", "\\n"), " ".join(n["src"][:3])))


def a_by(a):
    return tsv_header(a.tsv)[1] if a.tsv else ""


def tsv_header(path):
    lang, by, en = "", "", ""
    for l in open(path, encoding="utf-8"):
        m = re.match(r"#: (\S+) = (.*)", l)
        if m:
            k, v = m.groups()
            if k == "language": lang = v
            elif k == "language-en": en = v
            elif k == "translated-by": by = v
    return ("%s(%s)" % (en, lang) if en else lang), by


def read_tsv(path):
    out = {}
    for l in open(path, encoding="utf-8"):
        if l.startswith("#") or not l.strip():
            continue
        p = l.rstrip("\n").split("\t")
        if len(p) >= 4:
            out.setdefault((pool(p[0]), p[1], p[2]), p[3])
    return out


main()
