#!/usr/bin/env python3
"""What the save sync's tests read of a RomM, the stand-in or a real one, as plain lines for a shell script. Python
standard library only. Every call that reaches the server exits non-zero when the answer isn't what it should be (an
error, a page instead of a list), so a check built on it fails rather than comparing two empty answers.

    romm-api.py token <JSON or file> <name>               one token of a set of three ({"A", "B", "C"}), as the tests'
                                                          ROMM_TOKENS gives them
    romm-api.py roms <url> <token> <platform folder>      the system's games, "<id> TAB <file name>" by name, archives
                                                          left out (nothing when the server lacks the system)
    romm-api.py fetch <url> <token> <id> <file name> <to> a game's file
    romm-api.py rows <url> <token> <id>...                every save row of those games, "<id> <hash> <updated_at>
                                                          <slot> <emulator> <file name>" (spaces in a name as _), sorted
    romm-api.py head <url> <token> <id> [<emulator>]      the MD5 of the autosave slot's head (of one emulator's lineage
                                                          when given), "none" for an empty slot
    romm-api.py count <url> <token> <id> [<emulator>]     the rows in the autosave slot (of one emulator's)
    romm-api.py compare <before> <after> <diff> [gone]    two `rows` lists: exits 1 when a row went or changed in place
                                                          (gone: changed only, for a server pruning older versions),
                                                          what did written to <diff>
"""
import json
import sys
import urllib.parse
import urllib.request


def get(url, token, path, raw=False):
    req = urllib.request.Request(url.rstrip("/") + path, headers={"Authorization": "Bearer " + token})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
    except Exception as e:
        sys.exit(f"romm-api: GET {path}: {e}")
    return data if raw else json.loads(data)


def listed(v, what):
    if not isinstance(v, list):
        sys.exit(f"romm-api: {what}: not a list: {str(v)[:200]}")
    return v


def slot(url, token, rid, emulator=None):
    rows = listed(get(url, token, f"/api/saves?rom_id={int(rid)}&slot=autosave"), f"the saves of {rid}")
    return [s for s in rows if emulator is None or s.get("emulator") == emulator]


def main(cmd, *a):
    if cmd == "token":
        src, name = a
        print((json.loads(src) if src.lstrip().startswith("{") else json.load(open(src)))[name])
    elif cmd == "roms":
        url, token, folder = a
        pid = next((p["id"] for p in listed(get(url, token, "/api/platforms"), "the platforms") if p["fs_slug"] == folder), None)
        if pid is None:
            return
        page = get(url, token, f"/api/roms?platform_ids={pid}&limit=500")
        for r in sorted(listed(page.get("items") if isinstance(page, dict) else page, "the games"), key=lambda r: r["fs_name"]):
            if not r["fs_name"].endswith(".zip"):
                print("%d\t%s" % (r["id"], r["fs_name"]))
    elif cmd == "fetch":
        url, token, rid, name, to = a
        data = get(url, token, f"/api/roms/{int(rid)}/content/{urllib.parse.quote(name)}", raw=True)
        open(to, "wb").write(data)
    elif cmd == "rows":
        url, token, *ids = a
        out = []
        for rid in ids:
            for s in listed(get(url, token, f"/api/saves?rom_id={int(rid)}"), f"the saves of {rid}"):
                out.append(" ".join(str(x) for x in (s["id"], s.get("content_hash") or "-", s["updated_at"], s.get("slot") or "-",
                                                     s.get("emulator") or "-", s["file_name"].replace(" ", "_"))))
        for line in sorted(out):
            print(line)
    elif cmd == "head":
        rows = slot(*a)
        h = max(rows, key=lambda s: (s["updated_at"], s["id"])) if rows else None
        print(h["content_hash"] if h else "none")
    elif cmd == "count":
        print(len(slot(*a)))
    elif cmd == "compare":
        before, after, diff, *may = a
        b = {l.split()[0]: l.strip() for l in open(before) if l.strip()}
        f = {l.split()[0]: l.strip() for l in open(after) if l.strip()}
        gone = [b[i] for i in b if i not in f and may != ["gone"]]
        changed = ["%s -> %s" % (b[i], f[i]) for i in b if i in f and f[i] != b[i]]
        open(diff, "w").write("gone: %s; changed: %s" % (gone, changed))
        sys.exit(1 if gone or changed else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(*sys.argv[1:])
