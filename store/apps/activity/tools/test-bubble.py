#!/usr/bin/env python3
"""Regression test for #251: the Library's top bubble on the handhelds' 1024x768 panels.

1. "Pokémon" must not draw as "Pok?mon" (the big sizes had no Latin-1 glyphs).
2. A long title must stay inside the bubble (it had no width limit).

    python3 -I tools/test-bubble.py ./activity-host

Renders with --smoke into a temp dir and compares pixels; exit 0 = pass, 1 = fail.
"""
import os
import subprocess
import sys
import tempfile
import time

W, H = 1024, 768
S = W / 640.0


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6\n<w> <h>\n255\n
    parts = data.split(b"\n", 3)
    w, h = (int(v) for v in parts[1].split())
    return w, h, parts[3]


def render(binary, tmp, title):
    root = os.path.join(tmp, "card-%d" % abs(hash(title)))
    os.makedirs(os.path.join(root, "banana", "activity"), exist_ok=True)
    now = int(time.time())
    with open(os.path.join(root, "banana", "playstats.tsv"), "w", encoding="utf-8") as f:
        f.write("nds/%s.nds\t3\t3600\t%d\t-1\t0\n" % (title, now - 3600))
    prefix = os.path.join(root, "shot")
    subprocess.run([binary, "--root", root, "--smoke", prefix, "--view", "library", "--period", "all",
                    "--size", "%dx%d" % (W, H)], check=True, stdout=subprocess.DEVNULL)
    return read_ppm(prefix + "-bottom.ppm")


def region(img, x0, y0, x1, y1):
    w, _, px = img
    return b"".join(px[(y * w + x0) * 3:(y * w + x1) * 3] for y in range(y0, y1))


def main():
    binary = os.path.abspath(sys.argv[1])
    fails = 0
    with tempfile.TemporaryDirectory() as tmp:
        # the title row of the bubble: y 69..69+26 CSS, the bubble spans x 66..574
        ty0, ty1 = int(66 * S), int(100 * S)
        acc = render(binary, tmp, "Pokémon Platinum")
        q = render(binary, tmp, "Pok?mon Platinum")
        if region(acc, int(70 * S), ty0, int(570 * S), ty1) == region(q, int(70 * S), ty0, int(570 * S), ty1):
            print("FAIL accent: 'Pokémon' draws exactly like 'Pok?mon' in the bubble")
            fails += 1
        else:
            print("ok   accent: 'Pokémon' differs from 'Pok?mon'")

        short = render(binary, tmp, "Tetris")
        long_ = render(binary, tmp, "Mario & Luigi - Bowser's Inside Story + Bowser Jr's Journey Deluxe Edition")
        for name, (x0, x1) in (("left", (0, int(62 * S))), ("right", (int(578 * S), W))):
            if region(short, x0, ty0, x1, ty1) != region(long_, x0, ty0, x1, ty1):
                print("FAIL long title: text drawn outside the bubble on the %s" % name)
                fails += 1
            else:
                print("ok   long title: nothing outside the bubble on the %s" % name)
        # and the long title is still drawn (not blank): the bubble interior differs from the short one
        if region(short, int(70 * S), ty0, int(570 * S), ty1) == region(long_, int(70 * S), ty0, int(570 * S), ty1):
            print("FAIL long title: nothing drawn")
            fails += 1
    print("PASS" if not fails else "FAILED (%d)" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
