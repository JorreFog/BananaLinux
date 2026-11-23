#!/usr/bin/env python3
"""ui/src/pinyin_initials.h: the first letter of each Chinese character's pinyin, for the menu's search (#212: "sed"
finds 塞尔达, Sai Er Da, typed on the menu's own keyboard with no input method).

The letters come from GB 2312 itself: its first level (the 3755 most used characters, rows 16 to 55) is in pinyin
order, so a character's place in it says which letter its reading starts with (the well-known table of the 23 places
a new letter starts at; no pinyin begins with i, u or v). Python's own gb2312 codec gives each character's place: no
other data is read. The second level's rarer characters (ordered by radical) and traditional forms that GB 2312
doesn't have get no letter and are matched as typed.

  tools/pinyin-initials.py           writes the header
  tools/pinyin-initials.py --check   says whether the header is the one this would write (ui/tests/run.sh)
"""
import os
import sys

FIRST, LAST = 0x4E00, 0x9FA5      # the CJK Unified Ideographs GB 2312's characters are in
STARTS = [0xB0A1, 0xB0C5, 0xB2C1, 0xB4EE, 0xB6EA, 0xB7A2, 0xB8C1, 0xB9FE, 0xBBF7, 0xBFA6, 0xC0AC, 0xC2E8,
          0xC4C3, 0xC5B6, 0xC5BE, 0xC6DA, 0xC8BB, 0xC8F6, 0xCBFA, 0xCDDA, 0xCEF4, 0xD1B9, 0xD4D1]
LETTERS = "abcdefghjklmnopqrstwxyz"
LEVEL1_END = 0xD7F9


def initial(cp):
    try:
        gb = chr(cp).encode("gb2312")
    except UnicodeEncodeError:
        return "."
    v = gb[0] << 8 | gb[1]
    if len(gb) != 2 or v < STARTS[0] or v > LEVEL1_END:
        return "."
    return LETTERS[max(i for i, s in enumerate(STARTS) if s <= v)]


def header():
    table = "".join(initial(cp) for cp in range(FIRST, LAST + 1))
    out = ["// Made by tools/pinyin-initials.py (from GB 2312's first level, which is in pinyin order): don't edit.",
           "// The first letter of the pinyin of each character from U+%04X to U+%04X, '.' where there is none." % (FIRST, LAST),
           "#pragma once",
           "",
           "namespace banana",
           "{",
           "\tstatic constexpr unsigned PINYIN_FIRST = 0x%04X, PINYIN_LAST = 0x%04X;" % (FIRST, LAST),
           "\tstatic const char PINYIN_INITIALS[] ="]
    out += ['\t\t"%s"' % table[i:i + 96] for i in range(0, len(table), 96)]
    out[-1] += ";"
    out += ["}", ""]
    return "\n".join(out)


def main():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ui", "src", "pinyin_initials.h")
    text = header()
    if "--check" in sys.argv[1:]:
        try:
            with open(path, encoding="utf-8") as f:
                same = f.read() == text
        except OSError:
            same = False
        if not same:
            print("ui/src/pinyin_initials.h isn't what tools/pinyin-initials.py writes: run it again")
            return 1
        return 0
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
