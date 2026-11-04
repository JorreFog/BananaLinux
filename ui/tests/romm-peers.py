#!/usr/bin/env python3
"""Other RomM clients, as scripted stand-ins, so BananaOS's save sync can be tested against the devices it shares a
RomM with. Each makes the calls its real client makes (read from its source) in the same order, with the same
parameters and file names, and keeps a small state of its own as the real one does. Python standard library only.

    romm-peers.py <url> <token> grout <dir> sync        Grout (NextUI/MinUI) with its GBA saves in <dir>/Saves/GBA as
                                                        "<rom file>.sav"
    romm-peers.py <url> <token> bananaos <dir> join|upload <rom id> [<file>]
                                                        a device that writes as BananaOS's sync will: says it has the
                                                        slot's head, uploads into the slot; prints the HTTP status
Each run prints a JSON summary: uploaded, downloaded, conflicts, skipped (save file names), the session, and
negotiation's operations as the server gave them.

Grout: registers with no hostname or MAC (so RomM never matches it to another device), then sends its version again;
negotiates with no ROM scope; uploads with its folder name as the emulator ("GBA"), autocleanup to 10 and no hash; on
409 lists the slot and downloads the head when its own save is unchanged since the last sync, else reports a
conflict; downloads with optimistic=false and says so with /downloaded (no hash); downloads a head for a ROM with no
save of its own from the listing; completes its session.
"""
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from datetime import datetime, timezone

BASE, TOKEN, CLIENT, DIR = sys.argv[1].rstrip("/"), sys.argv[2], sys.argv[3], sys.argv[4]
ARGS = sys.argv[5:]
md5 = lambda b: hashlib.md5(b).hexdigest()


def call(method, path, body=None, query=None, files=None, raw=False):
    url = BASE + path + ("?" + urllib.parse.urlencode(query, doseq=True) if query else "")
    headers, data = {"Authorization": "Bearer " + TOKEN}, None
    if files:
        b = uuid.uuid4().hex
        data = b"".join(f'--{b}\r\nContent-Disposition: form-data; name="{f}"; filename="{n}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()
                        + c + b"\r\n" for f, (n, c) in files.items()) + f"--{b}--\r\n".encode()
        headers["Content-Type"] = f"multipart/form-data; boundary={b}"
    elif body is not None:
        data, headers["Content-Type"] = json.dumps(body).encode(), "application/json"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=data, method=method, headers=headers), timeout=60) as r:
            out = r.read()
            return r.status, out if raw else (json.loads(out) if out else None)
    except urllib.error.HTTPError as e:
        out = e.read()
        try:
            return e.code, json.loads(out)
        except ValueError:
            return e.code, out


def ts(s):
    t = datetime.fromisoformat(s.replace("Z", "+00:00"))
    return t if t.tzinfo else t.replace(tzinfo=timezone.utc)


def mtime_iso(p):
    return datetime.fromtimestamp(int(os.path.getmtime(p)), timezone.utc).strftime("%Y-%m-%dT%H:%M:%S+00:00")


def set_mtime(p, server_updated_at):
    t = int(ts(server_updated_at).timestamp())
    os.utime(p, (t, t))


def write_atomic(p, data):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + ".tmp", "wb") as f:
        f.write(data)
    os.replace(p + ".tmp", p)


def load_state():
    try:
        return json.load(open(os.path.join(DIR, "state.json")))
    except (OSError, ValueError):
        return {"device_id": None, "saves": {}}


def save_state(st):
    os.makedirs(DIR, exist_ok=True)
    json.dump(st, open(os.path.join(DIR, "state.json"), "w"), indent=1)


def roms_of(fs_slug):
    pid = next(p["id"] for p in call("GET", "/api/platforms")[1] if p["fs_slug"] == fs_slug)
    return call("GET", "/api/roms", query={"platform_ids": pid, "limit": 500})[1]["items"]


def newest(rows):
    return max(rows, key=lambda s: ts(s["updated_at"])) if rows else None


def out(summary):
    print(json.dumps(summary))


# ---- Grout
def grout():
    st = load_state()
    if not st["device_id"]:
        code, r = call("POST", "/api/devices", {"name": "grout-" + os.path.basename(DIR), "platform": "NEXTUI", "client": "grout",
                                                "client_version": "4.6.0", "sync_mode": "api"})
        st["device_id"] = r["device_id"]
        call("PUT", f"/api/devices/{st['device_id']}", {"client_version": "4.6.0"})
        save_state(st)
    dev = st["device_id"]
    folder = os.path.join(DIR, "Saves", "GBA")
    os.makedirs(folder, exist_ok=True)
    roms = {r["fs_name"]: r for r in roms_of("gba")}
    by_id = {r["id"]: r for r in roms.values()}
    local = {}
    for n in sorted(os.listdir(folder)):
        if n.endswith(".sav") and n[:-4] in roms:
            local[roms[n[:-4]]["id"]] = os.path.join(folder, n)
    summary = {"uploaded": [], "downloaded": [], "conflicts": [], "skipped": [], "session": None}
    entries = []
    for rid, p in local.items():
        data = open(p, "rb").read()
        entries.append({"rom_id": rid, "file_name": os.path.basename(p), "slot": st["saves"].get(str(rid), {}).get("slot", "autosave"),
                        "emulator": "GBA", "content_hash": md5(data), "updated_at": mtime_iso(p), "file_size_bytes": len(data)})
    code, neg = call("POST", "/api/sync/negotiate", {"device_id": dev, "saves": entries})
    summary["session"] = neg.get("session_id")
    summary["ops"] = [f"{o['rom_id']} {o['action']}: {o['reason']}" for o in neg.get("operations", [])]

    def download(save, rid):
        p = local.get(rid) or os.path.join(folder, by_id[rid]["fs_name"] + ".sav")
        if os.path.exists(p):
            os.makedirs(os.path.join(DIR, ".backup"), exist_ok=True)
            stem, ext = os.path.splitext(os.path.basename(p))
            shutil.copy2(p, os.path.join(DIR, ".backup", f"{stem} [{time.strftime('%Y-%m-%d %H-%M-%S')}]{ext}"))
        code, data = call("GET", f"/api/saves/{save['id']}/content", query={"device_id": dev, "optimistic": "false"}, raw=True)
        if code != 200:
            summary["skipped"].append(os.path.basename(p))
            return
        write_atomic(p, data)
        set_mtime(p, save["updated_at"] if "updated_at" in save else save["server_updated_at"])
        call("POST", f"/api/saves/{save['id']}/downloaded", {"device_id": dev})
        st["saves"][str(rid)] = {"slot": save.get("slot") or "autosave", "save_id": save["id"], "hash": md5(data)}
        summary["downloaded"].append(os.path.basename(p))

    def upload(rid):
        p = local[rid]
        data = open(p, "rb").read()
        rec = st["saves"].get(str(rid), {})
        if rec.get("hash") == md5(data):
            summary["skipped"].append(os.path.basename(p))
            return
        slot = rec.get("slot", "autosave")
        q = {"rom_id": rid, "device_id": dev, "slot": slot, "emulator": "GBA"}
        if slot == "autosave":
            q.update(autocleanup="true", autocleanup_limit=10)
        code, r = call("POST", "/api/saves", query=q, files={"saveFile": (os.path.basename(p), data)})
        if code == 200:
            set_mtime(p, r["updated_at"])
            st["saves"][str(rid)] = {"slot": slot, "save_id": r["id"], "hash": md5(data)}
            summary["uploaded"].append(os.path.basename(p))
        elif code == 409:
            code, rows = call("GET", "/api/saves", query={"rom_id": rid, "slot": slot, "device_id": dev})
            h = newest(rows)
            if h and rec.get("hash") and rec["hash"] == md5(data):
                download(h, rid)
            else:
                summary["conflicts"].append(os.path.basename(p))
        else:
            summary["skipped"].append(os.path.basename(p))

    seen = set()
    for op in neg.get("operations", []):
        rid = op["rom_id"]
        seen.add(rid)
        if op["action"] in ("upload", "conflict") and rid in local:
            if op["action"] == "conflict":
                summary["conflicts"].append(os.path.basename(local[rid]))
            else:
                upload(rid)
        elif op["action"] == "download" and rid in by_id:
            download({"id": op["save_id"], "slot": op["slot"], "server_updated_at": op["server_updated_at"]}, rid)
    rest = [rid for rid in by_id if rid not in local and rid not in seen]
    if rest:
        code, rows = call("GET", "/api/saves", query={"device_id": dev, "rom_ids": rest})
        for rid in rest:
            mine = [s for s in rows if s["rom_id"] == rid]
            h = newest([s for s in mine if (s["slot"] or "autosave") == "autosave"]) or newest(mine)
            if h and st["saves"].get(str(rid), {}).get("save_id") != h["id"]:
                download(h, rid)
    if summary["session"]:
        call("POST", f"/api/sync/sessions/{summary['session']}/complete",
             {"operations_completed": len(summary["uploaded"]) + len(summary["downloaded"]), "operations_failed": len(summary["conflicts"])})
    save_state(st)
    out(summary)


# ---- a device that writes as BananaOS's save sync will
def bananaos():
    st = load_state()
    if not st["device_id"]:
        host = "bananaos-" + os.path.basename(DIR)
        st["device_id"] = call("POST", "/api/devices", {"name": host, "platform": "BananaOS", "client": "bananaos",
                                                       "client_version": "test", "hostname": host, "sync_mode": "api"})[1]["device_id"]
        save_state(st)
    dev, op, rid = st["device_id"], ARGS[0], int(ARGS[1])
    if op == "join":
        code, rows = call("GET", "/api/saves", query={"rom_id": rid, "slot": "autosave"})
        h = max(rows, key=lambda s: (ts(s["updated_at"]), s["id"])) if rows else None
        code = call("POST", f"/api/saves/{h['id']}/downloaded", {"device_id": dev, "content_hash": h["content_hash"]})[0] if h else 204
    else:
        data = open(ARGS[2], "rb").read()
        code, r = call("POST", "/api/saves", query={"rom_id": rid, "slot": "autosave", "device_id": dev, "emulator": "gpSP",
                                                    "content_hash": md5(data)}, files={"saveFile": (os.path.basename(ARGS[2]), data)})
    print(code)


{"grout": grout, "bananaos": bananaos}[CLIENT]()
