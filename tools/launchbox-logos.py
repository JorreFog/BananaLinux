#!/usr/bin/env python3
# The games' logos for the systems libretro's thumbnails have none of (PSP, arcade): a list made from the LaunchBox
# games database (https://gamesdb.launchbox-app.com/Metadata.zip), shipped as /usr/share/banana/launchbox-logos.tsv.
# banana-scrape matches a game's name against it and fetches that one logo from images.launchbox-app.com, so the
# handheld never downloads the database itself. Run again to refresh it:
#   curl -O https://gamesdb.launchbox-app.com/Metadata.zip && unzip Metadata.zip Metadata.xml
#   python3 tools/launchbox-logos.py Metadata.xml > system/rootfs/usr/share/banana/launchbox-logos.tsv
# A line: <platform> TAB <name> TAB <logo file>, a game's other names (GameAlternateName) as lines of their own.
import sys
import xml.etree.ElementTree as ET

PLATFORMS = {"Sony PSP", "Sony PSP Minis", "Arcade", "SNK Neo Geo MVS", "SNK Neo Geo AES"}
# a logo's region, the best first (the names are English)
REGIONS = ["North America", "World", "United States", "", "Europe", "United Kingdom", "Australia", "Japan"]


def rank(region):
    return REGIONS.index(region) if region in REGIONS else len(REGIONS)


games = {}	# DatabaseID -> (platform, name)
alts = {}	# DatabaseID -> [names]
logos = {}	# DatabaseID -> (rank, file)
for _, e in ET.iterparse(sys.argv[1]):
    t = e.tag
    if t == "Game":
        p = e.findtext("Platform") or ""
        if p in PLATFORMS:
            games[e.findtext("DatabaseID")] = (p, (e.findtext("Name") or "").strip())
        e.clear()
    elif t == "GameAlternateName":
        alts.setdefault(e.findtext("DatabaseID"), []).append((e.findtext("AlternateName") or "").strip())
        e.clear()
    elif t == "GameImage":
        if e.findtext("Type") == "Clear Logo":
            i, r = e.findtext("DatabaseID"), rank(e.findtext("Region") or "")
            if i not in logos or r < logos[i][0]:
                logos[i] = (r, e.findtext("FileName"))
        e.clear()

clean = lambda s: s.replace("\t", " ").replace("\n", " ")
out = set()
for i, (p, name) in games.items():
    if i not in logos:
        continue
    f = logos[i][1]
    for n in [name] + alts.get(i, []):
        if n:
            out.add((p, clean(n), f))
for row in sorted(out):
    print("\t".join(row))
