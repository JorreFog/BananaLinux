#!/usr/bin/env python3
"""A fake card for trying the app on a computer.

Writes <root>/banana/playstats.tsv and <root>/banana/activity/sessions.tsv in the
formats the app reads, so --smoke has something to draw. Nothing else is written.

    python tools/make-testdata.py /tmp/activity-card
    ./banana-activity --root /tmp/activity-card --smoke /tmp/act --view library
"""
import os
import sys
import time

GAMES = [
    ("nds", "Pokemon Platinum.nds", "Pokémon Platinum", 48),
    ("gba", "Golden Sun.gba", "Golden Sun", 31),
    ("gba", "Tetris Worlds.gba", "Tetris Worlds", 62),
    ("gba", "Castlevania - Aria of Sorrow.gba", "Castlevania: Aria of Sorrow", 22),
    ("app", "Doda Kvarter.sh", "Döda Kvarter", 19),
    ("gba", "Metroid Fusion.gba", "Metroid Fusion", 14),
]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/activity-card"
    now = int(time.time())
    today = time.mktime(time.localtime(now)[:3] + (0, 0, 0, 0, 0, -1))
    os.makedirs(os.path.join(root, "banana", "activity"), exist_ok=True)

    sessions = []
    # 21 days of play, two or three sessions most afternoons and evenings
    for d in range(21):
        n = 3 if d % 3 == 0 else 2
        for k in range(n):
            pool = [0, 1, 2, 3] if d < 10 else [4, 5, 3, 2]
            gi = pool[(d * 2 + k) % len(pool)]
            hour = [9, 12, 14, 20, 22][(d + k) % 5]
            length = [22, 38, 48, 64, 27, 41][(d + k * 3) % 6] * 60
            end = int(today - d * 86400 + hour * 3600 + 25 * 60)
            if end > now - 120:
                continue
            start = end - length
            sessions.append((start, end, GAMES[gi][0], GAMES[gi][1], length))

    # today: a morning, an afternoon and an evening session, whatever the clock says
    for hour, gi, mins in ((9, 0, 48), (14, 2, 22), (22, 1, 20)):
        end = int(today + hour * 3600 + 31 * 60)
        if end > now - 120:
            continue
        sessions.append((end - mins * 60, end, GAMES[gi][0], GAMES[gi][1], mins * 60))

    sessions.sort(key=lambda s: s[1])

    with open(os.path.join(root, "banana", "activity", "sessions.tsv"), "w", encoding="utf-8") as f:
        f.write("# banana-activity sessions 1\n")
        f.write("# start\tend\tsystem\trel\tseconds\n")
        for s in sessions:
            f.write("%d\t%d\t%s\t%s\t%d\n" % s)

    # the menu's file: plays, seconds and last play per game
    stats = {}
    for s in sessions:
        key = (s[2], s[3])
        p = stats.setdefault(key, [0, 0, 0, -1, 0])
        p[0] += 1
        p[1] += s[4]
        p[2] = max(p[2], s[1])
    for g in GAMES:
        p = stats.get((g[0], g[1]))
        if not p:
            continue
        p[3] = 12 if g[0] == "nds" else -1
        p[4] = 40 if g[0] == "nds" else 0
    with open(os.path.join(root, "banana", "playstats.tsv"), "w", encoding="utf-8") as f:
        for (sysid, rel), p in sorted(stats.items()):
            f.write("%s/%s\t%d\t%d\t%d\t%d\t%d\n" % (sysid, rel, p[0], p[1], p[2], p[3], p[4]))
    print("wrote", root, "with", len(sessions), "sessions")


if __name__ == "__main__":
    main()
