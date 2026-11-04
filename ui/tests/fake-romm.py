#!/usr/bin/env python3
"""A stand-in for a RomM server (rommapp/romm 5.4.1), made from RomM's own endpoint code and checked against a real
5.4.1 by tests/romm-conformance.py. The same paths, fields, status codes and error strings for what BananaOS and the
other RomM clients use: pairing, platforms and ROMs, devices, saves and states (the unslotted kind the 0.5.0 client
writes, and slots with their versions, their conflict guard and their pruning), the device's sync records, and the
sync negotiation with its sessions. Python standard library only; the tests' own hooks are under /test/.

    fake-romm.py <port> <library dir>      the library: <platform fs_slug>/<rom file or folder>, covers as
                                           <library>/covers/<rom id>.png once /test/cover is called
Environment: FAKE_ROMM_TZ (the server's own time zone, which slot versions are named in; Europe/Stockholm),
FAKE_ROMM_MAX_SAVES_PER_SLOT (versions kept per lineage; 50, 0 = no cap), FAKE_ROMM_BIND, FAKE_ROMM_TLS (<cert>:<key>,
PEM files: served over HTTPS), FAKE_ROMM_VERSION (what the heartbeat says, 5.4.1: the stand-in is 5.4.1 whatever it says).

A request is checked in RomM's order: its fields first (422), then the token's scopes (403), then what it names
(404), then the rest. A request the stand-in can't handle is answered 500, never left without an answer.

What RomM does that a client must not be fooled by, kept here on purpose:
- Times are whole seconds (MariaDB's TIMESTAMP), so versions written in one second tie.
- A slot version is stored as "<name> [YYYY-MM-DD_HH-MM-SS]<ext>", the time in the server's zone, not UTC; a name
  once given is held for 10 minutes even when the upload is then refused, so the next one is named a second later.
- The listing has no order (here: shuffled).
- Negotiation's head of a slot is any one of the versions of its newest second, in the database's order (on a real
  5.4.1 run to run the first, the middle or the last of a tie; here always the oldest, so a client that trusts it is
  caught every time), while the upload guard, the pruning and the promotion of identical content all take the newest
  by (updated_at, id).
- A device with no sync record for a slot's head can't upload into the slot (409) until it downloads the head or
  says it has it (/downloaded), whatever hash it says it has.
- An empty slot (slot=) is a slot named "", but saved as if unslotted: no version name, no slot guard (the unslotted
  one applies), no pruning.

Test hooks: POST /test/approve {"user_code"} (a user approving the device in RomM's web UI), POST /test/paircode
-> {"code"} (a client token's pairing code), POST /test/token {"scopes"?} -> {"token"} (a client token of the same
user, for a scripted client), POST /test/save {"rom_id","file_name","data"(hex),"slot"?,"emulator"?,"updated_at"?}
(a save written from another device or the web UI), POST /test/delete-rom {"id"}, POST /test/clock {"offset_s"}
(the server's clock moved), GET /test/log (the requests seen), GET /test/state.
Faults: POST /test/fault {"path": <regex on "METHOD /path">, "kind", "count"?: 1, "arg"?} answers the next matching
requests wrong: "truncate" (half the body, the whole length announced, the connection closed), "badhash" (the body's
first byte flipped), "500" (refused before anything is done), "500-after" (done, then answered 500), "slow" (arg
bytes a second, 1024), "delay" (answered arg seconds late, 1, as it stood when asked: the state may move meanwhile),
"skew" (the Date header arg seconds off), "replace" (the body swapped for arg: a string as it is, anything else as
JSON), "raise" (the handler fails, as a bug in the stand-in would), "409" (an upload refused by the upload guard,
whatever the slot holds), "409-rewrite" (refused, the slot's head written over in place meanwhile with the bytes arg in
hex, its id kept), "409-rewrite-nohash" (the same, the head then giving no MD5); {"clear": true} drops them.
Strict: POST /test/strict {"tokens"} notes, for those tokens, what BananaOS must never send (overwrite=true,
autocleanup, an upload outside a slot, PUT /api/saves/{id}, /api/saves/delete); GET /test/violations lists them.
"""
import email.utils
import hashlib
import http.server
import json
import os
import random
import re
import secrets
import ssl
import sys
import threading
import time
import traceback
import urllib.parse
import uuid
import zipfile
import zlib
from datetime import datetime, timedelta, timezone
from email.parser import BytesParser
from email.policy import HTTP
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

PORT, LIB = int(sys.argv[1]), sys.argv[2]
try:
    TZ = ZoneInfo(os.environ.get("FAKE_ROMM_TZ", "Europe/Stockholm"))
except ZoneInfoNotFoundError:     # (no tzdata: still a zone that isn't UTC, which is what matters)
    TZ = timezone(timedelta(hours=1), "UTC+01")
MAX_PER_SLOT = int(os.environ.get("FAKE_ROMM_MAX_SAVES_PER_SLOT", "50"))
MAX_AUTOCLEANUP = 100
MAX_ROM_IDS = 500
NAME_HOLD = timedelta(minutes=10)
SCOPES = ["me.read", "roms.read", "platforms.read", "assets.read", "assets.write", "devices.read", "devices.write"]
SYNC_MODES = ("api", "file_transfer", "push_pull")
SLUGS = {"nds": "nds", "gba": "gba", "psx": "psx", "megadrive": "genesis", "3ds": "3ds"}
TAG = re.compile(r" \[\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}\]")
EXT = re.compile(r"\.(([a-z]+\.)*\w+)$")
TAGS = re.compile(r"(?:\s*(?:\([^)]*\)|\[[^]]*\]))+\s*$")
TRUE, FALSE = {"1", "true", "t", "yes", "y", "on"}, {"0", "false", "f", "no", "n", "off"}
lock = threading.Lock()
# saves/states: id -> row; syncs: (device id, save id) -> the device's record of that save; removed: (rom id, slot)
# -> [[content hash, removed at]], oldest first, what pruning, deletion and replacement took from a slot (negotiation
# reads it); held: (rom id, emulator, name) -> until when a version name is spoken for
S = {"pending": {}, "tokens": {}, "pair_codes": {}, "devices": {}, "saves": {}, "states": {}, "syncs": {}, "removed": {},
     "held": {}, "sessions": {}, "next": 1, "log": [], "deleted": set(), "ids": {}, "offset": 0, "faults": [],
     "strict": set(), "violations": []}


class Answer(Exception):
    """What a request gets instead of going on: a refusal at any depth of the handler."""
    def __init__(self, code, body):
        super().__init__(code)
        self.code, self.body = code, body


def unprocessable(where, field, msg="Field required"):
    return Answer(422, {"detail": [{"loc": [where, field], "msg": msg}]})


def not_found(msg):
    return Answer(404, {"detail": msg})


# ---- a request's fields, checked as FastAPI checks them
def qint(q, name, default=None, required=False, minimum=None, where="query"):
    if q.get(name) in (None, ""):
        if required:
            raise unprocessable(where, name)
        return default
    try:
        v = int(q[name])
    except (TypeError, ValueError):
        raise unprocessable(where, name, "Input should be a valid integer")
    if minimum is not None and v < minimum:
        raise unprocessable(where, name, f"Input should be greater than or equal to {minimum}")
    return v


def qbool(q, name, default=False):
    v = q.get(name)
    if v is None:
        return default
    if v.lower() in TRUE:
        return True
    if v.lower() in FALSE:
        return False
    raise unprocessable("query", name, "Input should be a valid boolean")


def qstr(q, name, maxlen):
    v = q.get(name)
    if v is not None and len(v) > maxlen:
        raise unprocessable("query", name, f"String should have at most {maxlen} characters")
    return v


def rom_scope(ids, where, name="rom_ids"):
    """RomM's ROM scope: at most 500 ids as sent (repeats count), each above 0, then without repeats."""
    if ids is None:
        return None
    if not isinstance(ids, list) or len(ids) > MAX_ROM_IDS:
        raise unprocessable(where, name, f"at most {MAX_ROM_IDS} ROM ids")
    out = []
    for x in ids:
        v = qint({name: x}, name, required=True, minimum=1, where=where)
        if v not in out:
            out.append(v)
    return out


def body_obj(raw):
    try:
        j = json.loads(raw) if raw else None
    except ValueError:
        raise unprocessable("body", "", "JSON decode error")
    if not isinstance(j, dict):
        raise unprocessable("body", "", "Input should be a valid dictionary")
    return j


def path_id(parts, i):
    return qint({"id": parts[i]}, "id", required=True, where="path")


# ---- time: aware UTC, whole seconds, on a clock the tests can move
def now():
    return (datetime.now(timezone.utc) + timedelta(seconds=S["offset"])).replace(microsecond=0)


def iso(t):
    return t.isoformat() if t else None


def parse_ts(s):
    t = datetime.fromisoformat(s.replace("Z", "+00:00"))
    return (t if t.tzinfo else t.replace(tzinfo=timezone.utc)).astimezone(timezone.utc)


def nid():
    S["next"] += 1
    return S["next"]


def md5(b):
    return hashlib.md5(b).hexdigest()


def clean_hash(h):     # what RomM keeps of a client's hash: nothing when empty or longer than an MD5
    return h if h and len(h) <= 32 else None


# ---- the library
def platforms():
    out = []
    for i, fs in enumerate(sorted(os.listdir(LIB))):
        if fs == "covers" or not os.path.isdir(os.path.join(LIB, fs)):
            continue
        # (rom_count its ROMs, as RomM counts them: Grout leaves a platform of none out of its cache)
        count = sum(S["ids"].get(f"{fs}/{n}") not in S["deleted"] for n in os.listdir(os.path.join(LIB, fs)))
        out.append({"id": i + 1, "slug": SLUGS.get(fs, fs), "fs_slug": fs, "name": fs.upper(), "rom_count": count,
                    "fs_size_bytes": 0, "custom_name": None, "category": None, "firmware": []})
    return out


def roms():
    out = []
    for p in platforms():
        d = os.path.join(LIB, p["fs_slug"])
        for name in sorted(os.listdir(d)):
            rid = S["ids"].setdefault(f"{p['fs_slug']}/{name}", 101 + len(S["ids"]))     # (kept: RomM's ids don't move)
            if rid in S["deleted"]:
                continue
            full = os.path.join(d, name)
            files, multi = [], os.path.isdir(full)
            paths = [os.path.join(full, f) for f in sorted(os.listdir(full))] if multi else [full]
            whole = [hashlib.md5(), hashlib.sha1(), 0]     # (the ROM's own hashes: its files' bytes in name order)
            for k, fp in enumerate(paths):
                data = open(fp, "rb").read()
                h, members = file_hashes(fp, data)
                files.append({"id": rid * 10 + k, "rom_id": rid, "file_name": os.path.basename(fp),
                              "file_path": f"{p['fs_slug']}/{name}" if multi else p["fs_slug"], "file_size_bytes": len(data),
                              "full_path": fp, **h, "archive_members": members, "category": None})
                for part in members_data(fp) if members else [data]:
                    whole[0].update(part); whole[1].update(part); whole[2] = zlib.crc32(part, whole[2])
            stem = name if multi else os.path.splitext(name)[0]
            cover = os.path.join(LIB, "covers", f"{rid}.png")
            out.append({
                "id": rid, "platform_id": p["id"], "platform_slug": p["slug"], "platform_fs_slug": p["fs_slug"],
                "fs_name": name, "fs_name_no_tags": stem.split(" (")[0], "fs_name_no_ext": stem,
                "fs_extension": "" if multi else os.path.splitext(name)[1][1:], "fs_path": f"roms/{p['fs_slug']}",
                "fs_size_bytes": sum(f["file_size_bytes"] for f in files), "name": stem.split(" (")[0],
                "summary": f"About {stem}.", "metadatum": {"rom_id": rid, "genres": ["Puzzle"], "publishers": ["Banana"],
                                                         "first_release_date": 1262304000000},
                # (?ts= is RomM's updated_at as Python prints a datetime: a space and a "+" in it)
                "path_cover_large": f"/assets/romm/resources/roms/{p['id']}/{rid}/cover/big.png?ts=2026-10-31 15:44:32.123456+00:00" if os.path.exists(cover) else "",
                "has_multiple_files": multi, "has_simple_single_file": not multi, "missing_from_fs": False,
                "md5_hash": whole[0].hexdigest(), "sha1_hash": whole[1].hexdigest(), "crc_hash": format(whole[2], "08x"),
                "files": files, "full_path": full})
    return out


# A ROM file's hashes as RomM 5.4.1 has them (handler/filesystem/roms_handler.py): a zip's are of its members' bytes
# one after another in name order, not of the zip file, and each member's are listed as archive_members; a zip that
# can't be read, and any other file, by its own bytes
def file_hashes(fp, data):
    members = None
    if fp.lower().endswith(".zip"):
        try:
            with zipfile.ZipFile(fp) as z:
                members = [{"name": i.filename, "size": i.file_size, "crc_hash": format(i.CRC, "08x"),
                            "md5_hash": md5(z.read(i)), "sha1_hash": hashlib.sha1(z.read(i)).hexdigest()}
                           for i in sorted(z.infolist(), key=lambda i: i.filename) if not i.is_dir()] or None
        except (zipfile.BadZipFile, OSError):
            members = None
    m, s, c = hashlib.md5(), hashlib.sha1(), 0
    for part in members_data(fp) if members else [data]:
        m.update(part); s.update(part); c = zlib.crc32(part, c)
    return {"crc_hash": format(c, "08x"), "md5_hash": m.hexdigest(), "sha1_hash": s.hexdigest()}, members


def members_data(fp):
    with zipfile.ZipFile(fp) as z:
        return [z.read(i) for i in sorted(z.infolist(), key=lambda i: i.filename) if not i.is_dir()]


def rom_ids():
    return {r["id"] for r in roms()}


# ---- devices
def new_device(b, did=None, identifier=None):
    t = now()
    d = {"id": did or str(uuid.uuid4()), "user_id": 1, "sync_enabled": True, "sync_config": None, "last_seen": t,
         "created_at": t, "updated_at": t, "client_device_identifier": identifier}
    for k in ("name", "platform", "client", "client_version", "ip_address", "mac_address", "hostname"):
        d[k] = b.get(k)
    d["sync_mode"] = b.get("sync_mode") or "api"
    d["capabilities"] = b.get("capabilities")
    S["devices"][d["id"]] = d
    return d


def device_schema(d):
    return {k: (iso(v) if isinstance(v, datetime) else v) for k, v in d.items()}


def fingerprint(b):
    """The device a registration is taken to be: one with the same MAC; else (no MAC, or none has it) one with the
    same hostname and platform; else none. (RomM's lookup also has a tier for IP address and platform, between the
    two, but 5.4.1's registration never passes it the address, so it is never reached.)"""
    if b.get("mac_address"):
        for d in S["devices"].values():
            if d["mac_address"] == b["mac_address"]:
                return d
    if b.get("hostname") and b.get("platform"):
        for d in S["devices"].values():
            if d["hostname"] == b["hostname"] and d["platform"] == b["platform"]:
                return d
    return None


def check_device_fields(b):
    if "sync_mode" in b and b["sync_mode"] is not None and b["sync_mode"] not in SYNC_MODES:
        raise unprocessable("body", "sync_mode", f"Input should be {', '.join(repr(m) for m in SYNC_MODES)}")
    if "sync_enabled" in b and not isinstance(b["sync_enabled"], bool):
        raise unprocessable("body", "sync_enabled", "Input should be a valid boolean")
    if b.get("capabilities") is not None and not isinstance(b["capabilities"], dict):
        raise unprocessable("body", "capabilities", "Input should be a valid dictionary")


# ---- saves
def ext_of(name):
    m = EXT.search(name)
    return m.group(1) if m else ""


def no_ext(name):
    return EXT.sub("", name).strip()


def label(emulator):
    """An emulator label as the database compares it: without regard to case, as MariaDB's default collation does (the
    database RomM's own compose file runs; on Postgres gpSP and gpsp would be two)."""
    return emulator.lower() if emulator else emulator


def lineage_of(emulator, name):
    return (label(emulator), ext_of(name).lower())


def head(rom, slot):    # the upload guard's, the pruning's and the promotion's head: the newest by (updated_at, id)
    rows = [s for s in S["saves"].values() if s["rom_id"] == rom and s["slot"] == slot]
    return max(rows, key=lambda s: (s["updated_at"], s["id"])) if rows else None


def upsert_sync(dev, sid, synced_at, client_hash, server_hash):
    S["syncs"][(dev, sid)] = {"last_synced_at": synced_at, "last_sync_hash": client_hash,
                              "last_sync_server_hash": server_hash, "is_untracked": False}
    if dev in S["devices"]:
        S["devices"][dev]["last_seen"] = now()


def count_operation(session_id):
    x = S["sessions"].get(session_id) if session_id is not None else None
    if x:
        x["operations_completed"] += 1


def device_syncs(s, dev):
    if not dev:
        return []
    rows = sorted(((d, r) for (d, sid), r in S["syncs"].items() if sid == s["id"] and d in S["devices"]),
                  key=lambda x: (-x[1]["last_synced_at"].timestamp(), x[0]))
    out = [{"device_id": d, "device_name": S["devices"][d]["name"], "last_synced_at": iso(r["last_synced_at"]),
            "is_untracked": r["is_untracked"], "is_current": r["last_synced_at"] >= s["updated_at"]} for d, r in rows]
    mine = [x for x in out if x["device_id"] == dev]
    if not mine:
        mine = [{"device_id": dev, "device_name": S["devices"][dev]["name"], "last_synced_at": iso(s["updated_at"]),
                 "is_untracked": False, "is_current": False}]
    return mine + [x for x in out if x["device_id"] != dev]


def save_schema(s, kind, dev=None):
    name = s["file_name"]
    d = {"id": s["id"], "rom_id": s["rom_id"], "user_id": 1, "file_name": name, "file_name_no_tags": TAGS.sub("", no_ext(name)).strip(),
         "file_name_no_ext": no_ext(name), "file_extension": ext_of(name),
         "file_path": f"users/1/{kind}/{s['rom_id']}" + (f"/{s['emulator']}" if s["emulator"] else ""),
         "file_size_bytes": len(s["data"]), "full_path": f"users/1/{kind}/{s['rom_id']}/{name}",
         "download_path": f"/api/{kind}/{s['id']}/content?timestamp={s['updated_at']}&name={urllib.parse.quote(name)}",
         "missing_from_fs": False, "created_at": iso(s["created_at"]), "updated_at": iso(s["updated_at"]),
         "is_favorite": False, "labels": [], "emulator": s["emulator"], "is_public": False, "screenshot": None}
    if kind == "saves":     # (a state's row has no hash at all)
        d.update(content_hash=s["content_hash"], slot=s["slot"], origin_device_id=s.get("origin_device_id"), device_syncs=device_syncs(s, dev))
    return d


def new_save(kind, rom, name, data, emulator, slot=None, dev=None, at=None):
    t = at or now()
    s = {"id": nid(), "rom_id": rom, "file_name": name, "data": data, "content_hash": md5(data), "updated_at": t,
         "created_at": t, "emulator": emulator, "slot": slot, "origin_device_id": dev}
    S[kind][s["id"]] = s
    return s


def by_name(kind, rom, name, emulator, slot):
    """The row an upload of that name replaces: the same emulator's (another core's save is a different save), or
    with no emulator given any, an unfiled one first; in the same slot ("" is a slot of its own), or outside one."""
    rows = [s for s in sorted(S[kind].values(), key=lambda s: s["id"])
            if s["rom_id"] == rom and s["file_name"] == name and s.get("slot") == slot]
    if emulator is not None:
        rows = [s for s in rows if label(s["emulator"]) == label(emulator)]
    else:
        rows.sort(key=lambda s: s["emulator"] is not None)
    return rows[0] if rows else None


def version_name(rom, emulator, name):
    """The name a new slot version is stored under: any earlier tag replaced by the server's local time, moved on a
    second while another version (of the ROM's saves for that emulator) has it or an earlier upload was given it in
    the last ten minutes; then held."""
    stem, ext = os.path.splitext(TAG.sub("", name))
    t = now()
    S["held"] = {k: until for k, until in S["held"].items() if until > t}
    taken = {s["file_name"] for s in S["saves"].values() if s["rom_id"] == rom and label(s["emulator"]) == label(emulator)}
    taken |= {k[2] for k in S["held"] if k[0] == rom and label(k[1]) == label(emulator)}
    start = t.astimezone(TZ)
    for k in range(60):
        n = f"{stem} [{(start + timedelta(seconds=k)).strftime('%Y-%m-%d_%H-%M-%S')}]{ext}"
        if n not in taken:
            S["held"][(rom, emulator, n)] = t + NAME_HOLD
            return n
    raise RuntimeError(f"No free version name for {name}")


def note_removed(rom, slot, h):
    """A version's hash noted as taken from its slot: oldest first, a hash lost again moved to the end, 100 kept."""
    if not slot or not h:
        return
    rec = S["removed"].setdefault((rom, slot), [])
    rec[:] = [x for x in rec if x[0] != h] + [[h, now()]]
    del rec[:-100]


def prune(rom, slot, lin, keep):
    """Versions past the newest `keep` of one lineage (the same emulator and extension) in a slot deleted, their
    hashes noted as taken from the slot."""
    rows = sorted((s for s in S["saves"].values() if s["rom_id"] == rom and s["slot"] == slot and lineage_of(s["emulator"], s["file_name"]) == lin),
                  key=lambda s: (s["updated_at"], s["id"]), reverse=True)
    for s in reversed(rows[keep:]):
        delete_save(s)


def delete_save(s):
    del S["saves"][s["id"]]
    for k in [k for k in S["syncs"] if k[1] == s["id"]]:
        del S["syncs"][k]
    note_removed(s["rom_id"], s["slot"], s["content_hash"])


def removal_times(rom, slot):
    return {h: t for h, t in S["removed"].get((rom, slot), [])}


# ---- negotiation (the comparison RomM makes per slot)
def compare(client_hash, client_ts, server_hash, server_ts, sync, removed):
    if client_hash and server_hash and client_hash == server_hash:
        return "no_op", "Content is identical"
    if client_hash and client_hash in removed and client_ts <= removed[client_hash]:
        return "download", "Client holds a version removed on the server"
    if sync:
        synced = sync["last_synced_at"]
        client_unchanged = bool(client_hash and sync["last_sync_hash"] and client_hash == sync["last_sync_hash"])
        server_unchanged = bool(server_hash and sync["last_sync_server_hash"] and server_hash == sync["last_sync_server_hash"])
        client_changed = client_ts > synced and not client_unchanged
        server_changed = server_ts > synced and not server_unchanged
        if client_changed and server_changed:
            return "conflict", "Both sides changed since last sync"
        if client_changed:
            return "upload", "Client save is newer than last sync"
        if server_changed:
            return "download", "Server save is newer than last sync"
        return "no_op", "No changes since last sync"
    if client_ts > server_ts:
        return "upload", "Client save is newer (no sync history)"
    if server_ts > client_ts:
        return "download", "Server save is newer (no sync history)"
    if client_hash != server_hash:
        return "conflict", "Same timestamp but different content"
    return "no_op", "Saves appear identical"


def operation(action, reason, s=None, c=None):
    if s is None:
        return {"action": action, "rom_id": c["rom_id"], "save_id": None, "file_name": c["file_name"], "slot": c.get("slot"),
                "emulator": c.get("emulator"), "reason": reason, "server_updated_at": None, "server_content_hash": None}
    return {"action": action, "rom_id": s["rom_id"], "save_id": s["id"], "file_name": s["file_name"], "slot": s["slot"],
            "emulator": s["emulator"], "reason": reason, "server_updated_at": iso(s["updated_at"]),
            "server_content_hash": s["content_hash"]}


def client_saves(b):
    """Negotiation's saves, each checked and typed as RomM's schema takes them."""
    saves = b.get("saves")
    if not isinstance(saves, list):
        raise unprocessable("body", "saves")
    out = []
    for c in saves:
        if not isinstance(c, dict):
            raise unprocessable("body", "saves", "Input should be a valid dictionary")
        for k in ("rom_id", "file_name", "updated_at", "file_size_bytes"):
            if k not in c:
                raise unprocessable("body", k)
        c = dict(c, rom_id=qint(c, "rom_id", required=True, where="body"), file_size_bytes=qint(c, "file_size_bytes", required=True, where="body"))
        try:
            c["ts"] = parse_ts(c["updated_at"])
        except (AttributeError, TypeError, ValueError):
            raise unprocessable("body", "updated_at", "Input should be a valid datetime")
        out.append(c)
    return out


def session_schema(x):
    return {k: (iso(v) if isinstance(v, datetime) else v) for k, v in x.items()}


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def handle(self):
        # (a client killed mid-answer, as the tests kill banana-sync during a slow download: its connection reset is
        # no request failing, which is what a traceback in the log stands for)
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            pass

    def send(self, code, body=b"", ctype="application/json", headers=None):
        """The answer, kept until the request is done with the state (route writes it, outside the lock, so a slow
        answer holds nothing up)."""
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.answer = [code, body, ctype, dict(headers or {})]

    def detail(self, code, msg):
        self.send(code, {"detail": msg})

    def write_answer(self, fault):
        code, body, ctype, headers = self.answer
        kind = fault["kind"] if fault else None
        if kind == "replace":
            a = fault.get("arg")
            body = (a if isinstance(a, str) else json.dumps(a)).encode()
        if kind == "badhash" and body and ctype == "application/octet-stream":
            body = bytes([body[0] ^ 0xFF]) + body[1:]
        length = len(body)
        if kind == "delay":
            time.sleep(float(fault.get("arg") or 1))
        if kind == "truncate":
            body = body[:len(body) // 2]
            self.close_connection = True
        self.send_response_only(code)
        self.send_header("Server", self.version_string())
        skew = (fault.get("arg") or 0) if kind == "skew" else 0
        self.send_header("Date", email.utils.formatdate(time.time() + skew, usegmt=True))
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(length))
        for k, v in headers.items():
            self.send_header(k, v)
        self.end_headers()
        if kind == "slow":
            rate = int(fault.get("arg") or 1024)
            for i in range(0, len(body), rate):
                self.wfile.write(body[i:i + rate])
                self.wfile.flush()
                time.sleep(1)
        else:
            self.wfile.write(body)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def user(self):
        a = self.headers.get("Authorization", "")
        if a.startswith("Bearer ") and a[7:] in S["tokens"]:
            return S["tokens"][a[7:]]
        return None

    def file_part(self, raw, field):
        """The multipart field's (file name, bytes); (None, None) when the body isn't multipart or hasn't it."""
        ctype = self.headers.get("Content-Type") or ""
        if not raw or not ctype.startswith("multipart/"):
            return None, None
        msg = BytesParser(policy=HTTP).parsebytes(b"Content-Type: " + ctype.encode() + b"\r\n\r\n" + raw)
        if not msg.is_multipart():
            return None, None
        for part in msg.iter_parts():
            if part.get_param("name", header="content-disposition") == field:
                return part.get_filename(), part.get_payload(decode=True)
        return None, None

    def do_GET(self):
        self.route("GET")

    def do_POST(self):
        self.route("POST")

    def do_PUT(self):
        self.route("PUT")

    def do_DELETE(self):
        self.route("DELETE")

    def route(self, method):
        u = urllib.parse.urlsplit(self.path)
        qa = urllib.parse.parse_qs(u.query, keep_blank_values=True)
        q = {k: v[-1] for k, v in qa.items()}
        p = urllib.parse.unquote(u.path)
        raw = self.body() if method in ("POST", "PUT") else b""
        self.answer = [404, b"", "text/plain", {}]
        with lock:
            S["log"].append(f"{method} {u.path}?{u.query}" + (f" range={self.headers['Range']}" if self.headers.get("Range") else ""))
            fault = self.take_fault(method, u.path)
            try:
                if fault and fault["kind"] == "500":
                    raise Answer(500, {"detail": "Internal Server Error"})
                if fault and fault["kind"] in ("409-rewrite", "409-rewrite-nohash"):    # (a refusal, the head's row written over meanwhile: its id kept)
                    head = max((s for s in S["saves"].values() if s["rom_id"] == int(q["rom_id"]) and s["slot"] == q.get("slot")), key=lambda s: (s["updated_at"], s["id"]))   # (the head, as the listing's newest)
                    data = bytes.fromhex(fault["arg"])
                    head.update(data=data, content_hash="" if fault["kind"].endswith("nohash") else md5(data), updated_at=now())
                    raise Answer(409, {"detail": "Slot has a newer save since your last sync"})
                if fault and fault["kind"] == "409":    # (the upload guard's refusal, whatever the slot holds)
                    raise Answer(409, {"detail": "Slot has a newer save since your last sync"})
                self.tripwires(method, p, q)
                if fault and fault["kind"] == "raise":
                    raise RuntimeError(f"a fault asked for on {method} {u.path}")
                self.handle_route(method, p, q, qa, raw)
                if fault and fault["kind"] == "500-after":      # done, and the client told it failed
                    raise Answer(500, {"detail": "Internal Server Error"})
            except Answer as a:
                self.send(a.code, a.body)
            except Exception:
                traceback.print_exc()
                self.send(500, {"detail": "Internal Server Error"})
        try:
            self.write_answer(fault)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def take_fault(self, method, path):
        for f in S["faults"]:
            if f["count"] > 0 and re.search(f["path"], f"{method} {path}"):
                f["count"] -= 1
                return f
        return None

    def tripwires(self, method, p, q):
        """What BananaOS's sync must never send, noted for the tokens the test named (the request is answered as RomM
        would): an overwrite, a server-side cleanup, a save changed in place or deleted, an upload outside a slot."""
        a = self.headers.get("Authorization", "")
        if a[7:] not in S["strict"]:
            return
        seen = []
        if method == "POST" and p in ("/api/saves", "/api/states"):
            if (q.get("overwrite") or "").lower() in TRUE:
                seen.append("overwrite=true")
            if (q.get("autocleanup") or "").lower() in TRUE:
                seen.append("autocleanup")
            if p == "/api/saves" and not q.get("slot"):
                seen.append("an upload outside a slot")
        if method == "PUT" and re.fullmatch(r"/api/saves/\d+", p):
            seen.append("a save changed in place")
        if method == "POST" and p == "/api/saves/delete":
            seen.append("a save deleted")
        S["violations"] += [f"{method} {self.path}: {x}" for x in seen]

    def handle_route(self, method, p, q, qa, raw):
        if p == "/api/heartbeat":
            return self.send(200, {"SYSTEM": {"VERSION": os.environ.get("FAKE_ROMM_VERSION", "5.4.1"), "GIT_BRANCH": None, "SHOW_SETUP_WIZARD": False}})
        if p.startswith("/test/"):
            return self.test_hook(p, raw)
        # ---- pairing (open endpoints)
        if p == "/api/auth/device/init" and method == "POST":
            b = body_obj(raw)
            for k in ("client_device_identifier", "name", "client", "requested_scopes"):
                if not b.get(k):
                    raise unprocessable("body", k)
            dc, uc = secrets.token_urlsafe(32), secrets.token_hex(2).upper() + "-" + secrets.token_hex(2).upper()
            S["pending"][dc] = {"user_code": uc, "status": "pending", "init": b, "device_id": secrets.token_hex(16), "polled": 0}
            return self.send(201, {"device_code": dc, "user_code": uc, "verification_path": "/pair/device",
                                   "verification_path_complete": f"/pair/device?user_code={uc}", "expires_in": 600, "interval": 1})
        if p == "/api/auth/device/token" and method == "POST":
            dc = body_obj(raw).get("device_code")
            v = S["pending"].get(dc)
            if not v:
                return self.detail(400, "expired_token")
            if v["status"] == "pending":
                return self.detail(400, "authorization_pending")
            del S["pending"][dc]
            tok = "rmm_" + secrets.token_hex(20)
            scopes = list(v["init"]["requested_scopes"])
            S["tokens"][tok] = {"token": tok, "device": v["device_id"], "scopes": set(scopes)}
            return self.send(200, {"access_token": tok, "device_id": v["device_id"], "scopes": scopes, "expires_at": None})
        if p == "/api/client-tokens/exchange" and method == "POST":
            code = str(body_obj(raw).get("code", "")).replace("-", "").upper()
            if not S["pair_codes"].pop(code, None):
                return self.detail(404, "Invalid or expired pairing code")
            tok = self.mint()
            return self.send(200, {"id": 1, "name": "t", "scopes": SCOPES, "expires_at": None, "last_used_at": None,
                                   "created_at": iso(now()), "user_id": 1, "device_id": None, "raw_token": tok})
        user = self.user()
        if user is None:
            return self.detail(403, "Forbidden")
        if p == "/api/platforms":
            return self.send(200, platforms())
        if p == "/api/roms" and method == "GET":
            pid = qint(q, "platform_ids", qint(q, "platform_id", 0))
            lim, off = qint(q, "limit", 50, minimum=1), qint(q, "offset", 0, minimum=0)
            items = [r for r in roms() if r["platform_id"] == pid]
            if q.get("with_files", "false").lower() not in ("true", "1"):    # (each ROM's files only when asked)
                items = [{**r, "files": []} for r in items]
            return self.send(200, {"items": items[off:off + lim], "total": len(items), "limit": lim, "offset": off})
        parts = p.split("/")
        if p.startswith("/api/roms/") and "/files/content/" in p:      # /api/roms/{file id}/files/content/{name}
            fid = path_id(parts, 3)
            for r in roms():
                for f in r["files"]:
                    if f["id"] == fid:
                        return self.send(200, open(f["full_path"], "rb").read(), "application/octet-stream")
            return self.detail(404, "File not found")
        if p.startswith("/api/roms/") and "/content/" in p:            # /api/roms/{id}/content/{name}
            rid = path_id(parts, 3)
            for r in roms():
                if r["id"] == rid and not r["has_multiple_files"]:
                    data = open(r["full_path"], "rb").read()
                    rng = self.headers.get("Range")
                    if rng and rng.startswith("bytes="):
                        a = int(rng[6:].split("-")[0])
                        return self.send(206, data[a:], "application/octet-stream", {"Content-Range": f"bytes {a}-{len(data) - 1}/{len(data)}"})
                    return self.send(200, data, "application/octet-stream")
            return self.detail(404, f"Rom with id {rid} not found")
        if p.startswith("/assets/romm/resources/roms/"):
            rid = parts[6]
            f = os.path.join(LIB, "covers", f"{rid}.png")
            return self.send(200, open(f, "rb").read(), "image/png") if os.path.exists(f) else self.send(404, b"", "text/plain")
        if p == "/api/devices" or p.startswith("/api/devices/"):
            return self.devices(method, p, parts, user, raw)
        if p.startswith("/api/sync/"):
            return self.sync(method, p, parts, q, user, raw)
        for kind in ("saves", "states"):
            if p == f"/api/{kind}" or p.startswith(f"/api/{kind}/"):
                return self.saves(kind, method, p, parts, q, qa, user, raw)
        return self.detail(404, "Not Found")

    # ---- helpers for the authenticated endpoints
    def mint(self, scopes=None):
        tok = "rmm_" + secrets.token_hex(20)
        S["tokens"][tok] = {"token": tok, "device": None, "scopes": set(scopes or SCOPES)}
        return tok

    def need(self, user, *scopes):
        if not all(s in user["scopes"] for s in scopes):
            raise Answer(403, {"detail": "Forbidden"})

    def device(self, dev):
        d = S["devices"].get(dev)
        if d is None:
            raise not_found(f"Device with ID {dev} not found")
        return d

    def save(self, kind, sid):
        s = S[kind].get(sid)
        if not s:
            raise not_found(f"Save with ID {sid} not found")
        return s

    # ---- the tests' hooks
    def test_hook(self, p, raw):
        b = json.loads(raw) if raw else {}
        if p == "/test/approve":
            uc = b["user_code"]
            for dc, v in S["pending"].items():
                if v["user_code"] == uc:
                    v["status"] = "approved"
                    i = v["init"]
                    new_device({"name": i.get("name"), "client": i.get("client"), "client_version": i.get("client_version"),
                                "platform": i.get("platform")}, v["device_id"], i.get("client_device_identifier"))
                    return self.send(200, {"device_id": v["device_id"], "device_name": i.get("name")})
            return self.detail(404, "Unknown or expired code")
        if p == "/test/paircode":
            code = "".join(secrets.choice("ABCDEFGHJKMNPQRSTUVWXYZ23456789") for _ in range(8))
            S["pair_codes"][code] = True
            return self.send(200, {"code": code, "expires_in": 60})
        if p == "/test/token":
            return self.send(200, {"token": self.mint(b.get("scopes"))})
        if p == "/test/clock":
            S["offset"] = float(b.get("offset_s", 0))
            return self.send(200, {"now": iso(now())})
        if p == "/test/save":
            kind = b.get("kind", "saves")
            data = bytes.fromhex(b["data"])
            at = parse_ts(b["updated_at"]).replace(microsecond=0) if b.get("updated_at") else now()
            slot = b.get("slot")
            if not slot:
                s = by_name(kind, b["rom_id"], b["file_name"], b.get("emulator"), slot)
                if s:
                    s.update(data=data, content_hash=md5(data), updated_at=at)
                    return self.send(200, save_schema(s, kind))
            name = version_name(b["rom_id"], b.get("emulator"), b["file_name"]) if slot else b["file_name"]
            s = new_save(kind, b["rom_id"], name, data, b.get("emulator"), slot, b.get("device_id"), at)
            return self.send(200, save_schema(s, kind))
        if p == "/test/delete-rom":
            S["deleted"].add(b["id"])
            return self.send(200, {})
        if p == "/test/fault":
            if b.get("clear"):
                S["faults"].clear()
            else:
                S["faults"].append({"path": b["path"], "kind": b["kind"], "count": b.get("count", 1), "arg": b.get("arg")})
            return self.send(200, {})
        if p == "/test/strict":
            S["strict"] = set(b.get("tokens") or [])
            return self.send(200, {})
        if p == "/test/violations":
            return self.send(200, S["violations"])
        if p == "/test/log":
            return self.send(200, S["log"])
        if p == "/test/state":
            return self.send(200, {k: {i: save_schema(s, k) for i, s in S[k].items()} for k in ("saves", "states")})
        return self.detail(404, "Not Found")

    # ---- devices
    def devices(self, method, p, parts, user, raw):
        if p == "/api/devices" and method == "POST":
            b = body_obj(raw)
            check_device_fields(b)
            self.need(user, "devices.write")
            allow_existing = b.get("allow_existing", True) and not b.get("allow_duplicate", False)
            d = None if b.get("allow_duplicate") else fingerprint(b)
            if d:
                if not allow_existing:
                    return self.send(409, {"detail": {"error": "device_exists", "message": "A device with this fingerprint already exists",
                                                      "device_id": d["id"]}})
                if b.get("reset_syncs"):
                    for k in [k for k in S["syncs"] if k[0] == d["id"]]:
                        del S["syncs"][k]
                d["last_seen"] = now()
                if b.get("capabilities") is not None:
                    d["capabilities"] = b["capabilities"]
                return self.send(200, {"device_id": d["id"], "name": d["name"], "created_at": iso(d["created_at"])})
            d = new_device(b)
            return self.send(201, {"device_id": d["id"], "name": d["name"], "created_at": iso(d["created_at"])})
        if p == "/api/devices" and method == "GET":
            self.need(user, "devices.read")
            return self.send(200, [device_schema(d) for d in S["devices"].values()])
        if len(parts) != 4:
            return self.detail(404, "Not Found")
        dev = parts[3]
        if method == "GET":
            self.need(user, "devices.read")
            return self.send(200, device_schema(self.device(dev)))
        if method == "PUT":
            b = body_obj(raw)
            check_device_fields(b)
            self.need(user, "devices.write")
            d = self.device(dev)
            fields = {k: v for k, v in b.items() if k in ("name", "platform", "client", "client_version", "ip_address", "mac_address",
                                                          "hostname", "sync_mode", "sync_enabled", "sync_config", "capabilities")}
            if fields:      # (nothing sent: nothing written, updated_at as it was)
                d.update(fields)
                d["updated_at"] = now()
            return self.send(200, device_schema(d))
        if method == "DELETE":
            self.need(user, "devices.write")
            self.device(dev)
            # its sync records go with it, and the saves it wrote no longer name it
            del S["devices"][dev]
            for k in [k for k in S["syncs"] if k[0] == dev]:
                del S["syncs"][k]
            for s in S["saves"].values():
                if s.get("origin_device_id") == dev:
                    s["origin_device_id"] = None
            return self.send(204, b"", "text/plain")
        return self.detail(404, "Not Found")

    # ---- saves and states
    def device_param(self, q, user, write):
        dev = q.get("device_id")
        if dev:
            self.need(user, "devices.write" if write else "devices.read")
            self.device(dev)
        return dev

    def saves(self, kind, method, p, parts, q, qa, user, raw):
        base = f"/api/{kind}"
        if p == base and method == "GET":
            ids = rom_scope(qa.get("rom_ids"), "query")
            one = qint(q, "rom_id")
            pid = qint(q, "platform_id", 0)
            self.need(user, "assets.read")
            dev = self.device_param(q, user, False)
            if one is not None:
                ids = [x for x in ids if x == one] if ids is not None else [one]
            rs = {r["id"]: r for r in roms()}
            rows = [s for s in S[kind].values() if s["rom_id"] in rs and (ids is None or s["rom_id"] in ids)
                    and (not pid or rs[s["rom_id"]]["platform_id"] == pid) and ("slot" not in q or s.get("slot") == q["slot"])]
            random.shuffle(rows)        # RomM gives the rows in no order
            return self.send(200, [save_schema(s, kind, dev) for s in rows])
        if p == base and method == "POST":
            return self.upload(kind, q, user, raw)
        if kind == "saves" and p == "/api/saves/delete" and method == "POST":
            ids = body_obj(raw).get("saves")
            if not isinstance(ids, list):
                raise unprocessable("body", "saves")
            ids = [qint({"id": i}, "id", required=True, where="body") for i in ids]
            self.need(user, "assets.write")
            if not ids:
                return self.detail(400, "No saves were provided")
            for i in ids:       # one by one: a missing one is refused after the ones before it are gone
                delete_save(self.save("saves", i))
            return self.send(200, ids)
        if len(parts) < 4:
            return self.detail(404, "Not Found")
        sid = path_id(parts, 3)
        if len(parts) == 4 and method == "GET":
            self.need(user, "assets.read")
            dev = self.device_param(q, user, False)
            return self.send(200, save_schema(self.save(kind, sid), kind, dev))
        if len(parts) == 4 and method == "PUT" and kind == "saves":
            # bytes replaced in place, no guard; with no file, nothing changes
            self.need(user, "assets.write")
            dev = self.device_param(q, user, True)
            s = self.save(kind, sid)
            name, data = self.file_part(raw, "saveFile")
            if data is not None and md5(data) != s["content_hash"]:
                note_removed(s["rom_id"], s["slot"], s["content_hash"])
                s.update(data=data, content_hash=md5(data), updated_at=now())
            if dev:
                upsert_sync(dev, sid, s["updated_at"], clean_hash(q.get("content_hash")) if data is not None else None, s["content_hash"])
            return self.send(200, save_schema(s, kind, dev))
        if len(parts) >= 5 and parts[4] == "content" and method == "GET":   # (a trailing /{name} is taken too)
            optimistic = qbool(q, "optimistic", True)
            session_id = qint(q, "session_id")
            self.need(user, "assets.read")
            dev = self.device_param(q, user, False)
            s = self.save(kind, sid)
            if dev and optimistic:
                upsert_sync(dev, sid, s["updated_at"], None, s["content_hash"])
            count_operation(session_id)
            return self.send(200, s["data"], "application/octet-stream")
        if kind == "saves" and len(parts) == 5 and parts[4] == "downloaded" and method == "POST":
            return self.downloaded(sid, user, raw)
        return self.detail(404, "Not Found")

    def upload(self, kind, q, user, raw):
        rid = qint(q, "rom_id", required=True)
        emulator = qstr(q, "emulator", 50)
        slot = qstr(q, "slot", 255) if kind == "saves" else None
        overwrite = qbool(q, "overwrite")
        autocleanup = qbool(q, "autocleanup")
        limit = qint(q, "autocleanup_limit", 10)
        session_id = qint(q, "session_id")
        field = "saveFile" if kind == "saves" else "stateFile"
        name, data = self.file_part(raw, field)
        if data is None:
            raise unprocessable("body", field)
        self.need(user, "assets.write")
        dev = self.device_param(q, user, True)
        if rid not in rom_ids():
            raise not_found(f"Rom with id '{rid}' not found")
        if not name:
            return self.detail(400, "Save file has no filename")
        h = md5(data)
        if slot:
            name = version_name(rid, emulator, name)
        lin = lineage_of(emulator, name)
        db = by_name(kind, rid, name, emulator, slot)
        if dev and slot and not overwrite:
            # the whole slot's head, whichever emulator wrote it: a device that hasn't seen it can't write over it
            hd = head(rid, slot)
            sync = S["syncs"].get((dev, hd["id"])) if hd else None
            if hd and (sync is None or sync["last_synced_at"] < hd["updated_at"]):
                return self.detail(409, "Slot has a newer save since your last sync")
        elif dev and db and not overwrite:
            sync = S["syncs"].get((dev, db["id"]))
            if sync and sync["last_synced_at"] < db["updated_at"]:
                return self.detail(409, "Save has been updated since your last sync")
        keep = None
        if kind == "saves":
            limits = ([MAX_PER_SLOT] if MAX_PER_SLOT else []) + ([max(1, min(limit, MAX_AUTOCLEANUP))] if autocleanup else [])
            keep = min(limits) if limits else None
        if slot and not overwrite:
            # the same bytes anywhere in the slot: that version made the head again, no new one; the upload's own
            # lineage is the one pruned
            same = next((s for s in S["saves"].values() if s["rom_id"] == rid and s["slot"] == slot and s["content_hash"] == h), None)
            if same:
                hd = head(rid, slot)
                if hd["id"] != same["id"]:
                    same["updated_at"] = max(now(), hd["updated_at"] + timedelta(seconds=1))
                if keep:
                    prune(rid, slot, lin, keep)
                if dev and same["id"] in S["saves"]:
                    upsert_sync(dev, same["id"], same["updated_at"], clean_hash(q.get("content_hash")), same["content_hash"])
                return self.send(200, save_schema(same, kind, dev))
        if db:
            db.update(data=data, content_hash=h, updated_at=now(), emulator=emulator)
        else:
            db = new_save(kind, rid, name, data, emulator, slot, dev)
        if dev:
            upsert_sync(dev, db["id"], db["updated_at"], clean_hash(q.get("content_hash")), db["content_hash"])
        count_operation(session_id)
        if slot and keep:
            prune(rid, slot, lin, keep)
        return self.send(200, save_schema(db, kind, dev))

    def downloaded(self, sid, user, raw):
        b = body_obj(raw)
        if not b.get("device_id"):
            raise unprocessable("body", "device_id")
        self.need(user, "devices.write")
        s = self.save("saves", sid)
        dev = b["device_id"]
        self.device(dev)
        sent = clean_hash(b.get("content_hash"))
        row = S["syncs"].get((dev, s["id"]))
        if row and row["last_sync_server_hash"] and row["last_sync_hash"] is None:
            # after an optimistic download: the time it was served stays, the device's hash is taken
            upsert_sync(dev, s["id"], row["last_synced_at"], sent, row["last_sync_server_hash"])
        elif sent != s["content_hash"]:
            upsert_sync(dev, s["id"], s["updated_at"], None, None)
        else:
            upsert_sync(dev, s["id"], s["updated_at"], s["content_hash"], s["content_hash"])
        return self.send(200, save_schema(s, "saves", dev))

    # ---- sync negotiation and sessions
    def sync(self, method, p, parts, q, user, raw):
        if p == "/api/sync/negotiate" and method == "POST":
            b = body_obj(raw)
            saves = client_saves(b)
            scope = rom_scope(b.get("rom_ids"), "body")
            self.need(user, "assets.read", "devices.read")
            return self.negotiate(user, b, saves, scope)
        if p == "/api/sync/sessions" and method == "GET":
            off, lim = qint(q, "offset", 0, minimum=0), qint(q, "limit", 50, minimum=1)
            self.need(user, "devices.read")
            rows = sorted((x for x in S["sessions"].values() if not q.get("device_id") or x["device_id"] == q["device_id"]),
                          key=lambda x: (x["initiated_at"], x["id"]), reverse=True)
            return self.send(200, [session_schema(x) for x in rows[off:off + lim]])
        if len(parts) >= 5 and parts[3] == "sessions":
            sid = path_id(parts, 4)
            if len(parts) == 5 and method == "GET":
                self.need(user, "devices.read")
                x = S["sessions"].get(sid)
                if not x:
                    raise not_found(f"Sync session with ID {sid} not found")
                return self.send(200, session_schema(x))
            if len(parts) == 6 and parts[5] == "complete" and method == "POST":
                b = body_obj(raw) if raw else {}
                done, failed = qint(b, "operations_completed", 0, where="body"), qint(b, "operations_failed", 0, where="body")
                self.need(user, "devices.write")
                x = S["sessions"].get(sid)
                if not x:
                    raise not_found(f"Sync session with ID {sid} not found")
                if x["status"] not in ("PENDING", "IN_PROGRESS"):
                    return self.detail(400, f"Session is already {x['status']}")
                t = now()
                x.update(status="COMPLETED", completed_at=t, updated_at=t, error_message=None,
                         operations_completed=done, operations_failed=failed)
                return self.send(200, {"session": session_schema(x), "play_session_ingest": None})
        return self.detail(404, "Not Found")

    def negotiate(self, user, b, saves, scope):
        dev = b.get("device_id") or user["device"]
        if not dev:
            return self.detail(400, "device_id is required (either in the request payload or implicit via a device-bound client token)")
        d = self.device(dev)
        if not d["sync_enabled"]:
            return self.detail(400, "Sync is disabled for this device")
        t = now()
        sess = {"id": nid(), "device_id": dev, "user_id": 1, "status": "PENDING", "initiated_at": t, "completed_at": None,
                "operations_planned": 0, "operations_completed": 0, "operations_failed": 0, "error_message": None,
                "created_at": t, "updated_at": t}
        S["sessions"][sess["id"]] = sess
        if scope is not None:
            scope = set(scope) | {c["rom_id"] for c in saves}
        emulators = b.get("emulators")
        # each slot's head as RomM's negotiation takes it: the rows in the database's order, replaced only by a strictly
        # newer one, so of versions written in the same second whichever came first (here the oldest)
        heads = {}
        for s in sorted(S["saves"].values(), key=lambda s: s["id"]):
            if s["slot"] is None or (scope is not None and s["rom_id"] not in scope) or (emulators is not None and s["emulator"] not in emulators):
                continue
            k = (s["rom_id"], s["slot"])
            if k not in heads or s["updated_at"] > heads[k]["updated_at"]:
                heads[k] = s
        ops, matched = [], set()
        for c in saves:
            k = (c["rom_id"], c.get("slot"))
            s = heads.get(k)
            removed = removal_times(*k)
            cts, ch = c["ts"], c.get("content_hash")
            if s is None:
                if ch and ch in removed and cts <= removed[ch]:
                    ops.append(operation("delete", "Save was deleted on the server", c=c))
                else:
                    ops.append(operation("upload", "Save exists on client but not on server", c=c))
                continue
            matched.add(s["id"])
            sync = S["syncs"].get((dev, s["id"]))
            if sync and sync["is_untracked"]:
                ops.append(operation("no_op", "Save is untracked on this device", s))
                continue
            action, reason = compare(ch, cts, s["content_hash"], s["updated_at"], sync, removed)
            if action == "no_op" and ch == s["content_hash"] and clean_hash(ch):
                if not (sync and (sync["is_untracked"] or (sync["last_sync_hash"] == ch and sync["last_sync_server_hash"] == ch))):
                    upsert_sync(dev, s["id"], s["updated_at"], ch, ch)
            ops.append(operation(action, reason, s))
        for s in heads.values():
            if s["id"] in matched:
                continue
            sync = S["syncs"].get((dev, s["id"]))
            if sync and sync["is_untracked"]:
                continue
            if sync:
                if s["updated_at"] <= sync["last_synced_at"] and not b.get("restore_unlisted"):
                    continue
                ops.append(operation("download", "Server save updated since last sync, not present on client", s))
            else:
                ops.append(operation("download", "Save exists on server but not on client", s))
        n = {a: sum(1 for o in ops if o["action"] == a) for a in ("upload", "download", "conflict", "no_op", "delete")}
        sess.update(status="IN_PROGRESS", operations_planned=len(ops) - n["no_op"], updated_at=now())
        d["last_seen"] = now()
        return self.send(200, {"session_id": sess["id"], "operations": ops, "total_upload": n["upload"], "total_download": n["download"],
                               "total_conflict": n["conflict"], "total_no_op": n["no_op"], "total_delete": n["delete"]})


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


srv = Server((os.environ.get("FAKE_ROMM_BIND", "127.0.0.1"), PORT), H)
if os.environ.get("FAKE_ROMM_TLS"):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(*os.environ["FAKE_ROMM_TLS"].split(":", 1))
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
