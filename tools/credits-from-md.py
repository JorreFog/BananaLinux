#!/usr/bin/env python3
# credits-from-md.py <credits.md> [out]: the owner's credits document (Markdown: "## Section" headings, "- **Name** —
# title" lines, or "- Name - title" without the bold; anything else is notes and skipped) as system/rootfs/etc/banana/credits.txt, which Settings > System >
# Credits shows. Lines still reading "Name" / "Title" (the template's placeholders) are left out and listed on stderr.
# Writes to stdout without [out]. Run with python3 -I.
import re, sys

HEADER = """# BananaOS credits: Settings > System > Credits shows this file, as it is in the system image.
#
# Format (plain text, UTF-8):
#   [Section]          starts a section; its name is shown as a heading ("Development", "Translations", ...)
#   Name = Title       one person a line: the name, then what they did. Any script works (简体中文, accents).
#                      A line with no "=" is a name with no title.
#   # ...              a comment, not shown. Empty lines are skipped.
# The sections and the names show in the order they are written here. A credits.txt on the SD card does NOT replace
# this one: the credits belong to the build. With no names in this file at all, the Credits row is hidden.
#
# Made by tools/credits-from-md.py from the owner's credits document: edit that and convert again.
"""

person = re.compile(r"^\s*[-*+]\s+\*\*(.+?)\*\*\s*(?:[—–:-]+\s*(.*))?$")
# a list line without the bold name ("* Numberjerry - UX/UI Lead"): the name up to the first " - ", " – " or " — "
plain = re.compile(r"^\s*[-*+]\s+(.+?)(?:\s+[—–-]\s+(.*))?$")
out, skipped, section, count = [HEADER], [], None, 0
for ln in open(sys.argv[1], encoding="utf-8"):
    ln = ln.rstrip("\n")
    h = re.match(r"^##\s+(.+?)\s*#*\s*$", ln)
    if h:
        section = h.group(1).strip()
        if section.lower() == "how to write it": section = None; continue
        out.append("\n[%s]\n" % section)
        continue
    m = person.match(ln) or plain.match(ln)
    if not m or section is None: continue
    name, title = m.group(1).strip(), (m.group(2) or "").strip()
    if name == "Name" or title == "Title":
        skipped.append("%s: %s" % (section, ln.strip())); continue
    # (the file's own syntax: "=" separates name and title, so a name can't hold one)
    name = name.replace("=", "-")
    out.append("%s = %s\n" % (name, title) if title else "%s\n" % name)
    count += 1
text = "".join(out)
# sections left with nobody in them are dropped
text = re.sub(r"\n\[[^\]\n]+\]\n(?=\n\[|$)", "\n", text)
if len(sys.argv) > 2: open(sys.argv[2], "w", encoding="utf-8").write(text)
else: sys.stdout.write(text)
print("%d people; %d placeholder lines left out" % (count, len(skipped)), file=sys.stderr)
for s in skipped: print("  left out: " + s, file=sys.stderr)
