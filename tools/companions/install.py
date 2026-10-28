#!/usr/bin/env python3
"""Install game companion definitions into the rootfs (system/rootfs/usr/share/banana/companions).

  python3 tools/companions/install.py <defs folder> [--replace]

Every *.json in the folder is checked with validate.py (docs/COMPANIONS.md); the valid ones are copied (minified),
the others listed and left out. index.tsv is written for banana-ui (gamecomp.cpp findDef: file, codes, title12,
crc32s, exclusive, zip), so a game's start reads one small file. --replace removes definitions that are no longer
in the folder. The companion idea is PokeDaisy's (Lidor Itzhari).
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import validate  # noqa: E402

DEST = os.path.normpath(os.path.join(HERE, "..", "..", "system", "rootfs", "usr", "share", "banana", "companions"))


def index_line(name, d):
    m = d.get("match", {})
    codes = m.get("code", [])
    codes = [codes] if isinstance(codes, str) else codes
    crcs = m.get("crc32", [])
    crcs = [crcs] if isinstance(crcs, str) else crcs
    return "\t".join([name, ",".join(codes), m.get("title12", ""), ",".join(c.lower() for c in crcs),
                      "1" if m.get("exclusive") else "0", m.get("zip", "")])


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    src = argv[0]
    replace = "--replace" in argv
    os.makedirs(DEST, exist_ok=True)
    names = sorted(f for f in os.listdir(src) if f.endswith(".json"))
    ok, bad = [], []
    for n in names:
        c = validate.check_def(os.path.join(src, n))
        if c.errors:
            bad.append((n, c.errors[0]))
            continue
        d = json.load(open(os.path.join(src, n), encoding="utf-8"))
        with open(os.path.join(DEST, n), "w", encoding="utf-8") as f:
            json.dump(d, f, ensure_ascii=False, separators=(",", ":"))
            f.write("\n")
        ok.append((n, d))
    if replace:
        keep = {n for n, _ in ok}
        for f in os.listdir(DEST):
            if f.endswith(".json") and f not in keep:
                os.remove(os.path.join(DEST, f))
    # the index: every definition in the folder now (also those installed before and kept)
    lines = ["# file\tcodes\ttitle12\tcrc32s\texclusive\tzip (banana-ui's game companion, gamecomp.h)"]
    for f in sorted(os.listdir(DEST)):
        if f.endswith(".json"):
            lines.append(index_line(f, json.load(open(os.path.join(DEST, f), encoding="utf-8"))))
    with open(os.path.join(DEST, "index.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("%d installed, %d left out (invalid), %d in %s" % (len(ok), len(bad), len(lines) - 1, DEST))
    for n, e in bad:
        print("  left out %s: %s" % (n, e))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
