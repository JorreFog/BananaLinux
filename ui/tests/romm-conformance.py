#!/usr/bin/env python3
"""What RomM 5.4.1 does with slotted saves, states, devices and sync negotiation, case by case, as measured on a real 5.4.1
(MariaDB, the server in Europe/Stockholm). Run against tests/fake-romm.py it shows the stand-in behaves the same;
run against a real RomM it shows the cases still hold for that server. Python standard library only.

    romm-conformance.py <url> [<tokens>]        a file, or the JSON itself: {"A": .., "B": .., "C": ..}, client tokens of one user with
                                                assets.read/write, devices.read/write, platforms.read and roms.read
                                                (the ROM listing is 403 without the last two); without it, minted
                                                from the stand-in's /test/token
Environment: ROMM_TZ, the server's time zone (Europe/Stockholm), which slot versions are named in.
Each case uses devices and contents of its own (the run's tag in their names), and joins a slot by saying it has the
head before writing into it, so a real server's earlier saves don't change the outcome. A real server keeps what a
run writes.
"""
import hashlib
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import zipfile
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

BASE = sys.argv[1].rstrip("/")
try:
    TZ = ZoneInfo(os.environ.get("ROMM_TZ", "Europe/Stockholm"))
except ZoneInfoNotFoundError:     # (no tzdata here: the stand-in's own fallback, an hour east of UTC)
    TZ = timezone(timedelta(hours=1), "UTC+01")
RUN = format(int(time.time() * 1000) % (1 << 40), "x")
SLOT = "autosave"
md5 = lambda b: hashlib.md5(b).hexdigest()
failures = []


def call(method, path, body=None, token=None, query=None, files=None, raw=None, ctype=None):
    url = BASE + path + ("?" + urllib.parse.urlencode(query, doseq=True) if query else "")
    headers, data = {}, None
    if raw is not None:
        data = raw
        if ctype:
            headers["Content-Type"] = ctype
    elif files:
        b = uuid.uuid4().hex
        data = b"".join(f'--{b}\r\nContent-Disposition: form-data; name="{f}"; filename="{n}"\r\nContent-Type: application/octet-stream\r\n\r\n'.encode()
                        + c + b"\r\n" for f, (n, c) in files.items()) + f"--{b}--\r\n".encode()
        headers["Content-Type"] = f"multipart/form-data; boundary={b}"
    elif body is not None:
        data, headers["Content-Type"] = json.dumps(body).encode(), "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=data, method=method, headers=headers), timeout=60) as r:
            raw = r.read()
            return r.status, json.loads(raw) if "json" in r.headers.get("Content-Type", "") and raw else raw
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, raw


def ts(s):
    t = datetime.fromisoformat(s.replace("Z", "+00:00"))
    return (t if t.tzinfo else t.replace(tzinfo=timezone.utc)).replace(microsecond=0)


def iso(t):
    return t.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S+00:00")


def check(cond, what):
    if not cond:
        raise AssertionError(what)


def ok(st, r, what):
    check(st < 300, f"{what}: HTTP {st} {str(r)[:200]}")
    return r


if len(sys.argv) > 2:       # a file of tokens, or the JSON itself (how sync-slots.sh passes $ROMM_TOKENS)
    TOK = json.loads(sys.argv[2]) if sys.argv[2].lstrip().startswith("{") else json.load(open(sys.argv[2]))
else:
    TOK = {n: call("POST", "/test/token", {})[1]["token"] for n in "ABC"}


def device(tok, host, **kw):
    body = {"name": host, "platform": "BananaOS", "client": "bananaos", "client_version": "conformance", "hostname": host, "sync_mode": "api"}
    body.update(kw)
    return call("POST", "/api/devices", body, token=tok)


def upload(tok, dev, rom, content, emulator=None, name="Game.srm", **q):
    emulator = emulator or EMU[0]       # (the running case's own lineage)
    query = {"rom_id": rom, "slot": SLOT, "device_id": dev, "emulator": emulator, "content_hash": md5(content)}
    query.update(q)
    return call("POST", "/api/saves", token=tok, query=query, files={"saveFile": (name, content)})


def listing(tok, rom):
    return ok(*call("GET", "/api/saves", token=tok, query={"rom_id": rom, "slot": SLOT}), "listing")


def head(tok, rom):
    rows = listing(tok, rom)
    return max(rows, key=lambda s: (ts(s["updated_at"]), s["id"])) if rows else None


def join(tok, dev, rom):
    """The device says it has the slot's head, as a client does before its first upload into a slot."""
    h = head(tok, rom)
    if h:
        ok(*call("POST", f"/api/saves/{h['id']}/downloaded", {"device_id": dev, "content_hash": h["content_hash"]}, token=tok), "downloaded")


def lineage(tok, rom, emulator):
    return [s for s in listing(tok, rom) if s["emulator"] == emulator]


def negotiate(tok, dev, saves, **kw):
    body = {"device_id": dev, "saves": saves}
    body.update(kw)
    return call("POST", "/api/sync/negotiate", body, token=tok)


def entry(rom, content, at, emulator):
    return {"rom_id": rom, "file_name": "Game.srm", "slot": SLOT, "emulator": emulator, "content_hash": md5(content),
            "updated_at": iso(at), "file_size_bytes": len(content)}


def complete(tok, sid):
    return call("POST", f"/api/sync/sessions/{sid}/complete", {}, token=tok)


def now():
    return datetime.now(timezone.utc)


def tick():     # times are whole seconds: the next step in a later one
    time.sleep(1.1)


def gba_roms():
    pid = next(p["id"] for p in ok(*call("GET", "/api/platforms", token=TOK["A"]), "platforms") if p["fs_slug"] == "gba")
    out, off = [], 0
    while True:
        r = ok(*call("GET", "/api/roms", token=TOK["A"], query={"platform_ids": pid, "limit": 500, "offset": off}), "roms")
        out += r["items"]
        off += len(r["items"])
        if not r["items"] or off >= r["total"]:
            return sorted(x["id"] for x in out)


ROMS = gba_roms()
check(len(ROMS) >= 23, f"the server has {len(ROMS)} GBA ROMs, the cases need 23")
DEV = {n: ok(*device(TOK[n], f"conformance-{RUN}-{n.lower()}"), "device")["device_id"] for n in "ABC"}
A, B, C = DEV["A"], DEV["B"], DEV["C"]
TA, TB, TC = TOK["A"], TOK["B"], TOK["C"]
CASES = []
EMU = [None]     # the lineage (emulator) a case writes under, its own each run


def case(f):
    CASES.append(f)
    return f


@case
def retention():
    """50 versions kept per lineage; a client's autocleanup cuts the lineage, the other device's versions too; the
    upload guard is the whole slot's"""
    rom, emu = ROMS[0], EMU[0]
    join(TA, A, rom)
    for i in range(60):
        ok(*upload(TA, A, rom, f"{RUN} retention v{i}".encode()), "upload")
    check(len(lineage(TA, rom, emu)) == 50, f"after 60 uploads: {len(lineage(TA, rom, emu))} versions, not 50")
    join(TB, B, rom)
    # (another name, the same emulator and extension: the same lineage. Under Game.srm it would need a 61st version
    # name within the minute RomM tries, and a fast server answers 500)
    ok(*upload(TB, B, rom, f"{RUN} retention from B".encode(), name="Other.srm", autocleanup="true", autocleanup_limit=10), "B's upload")
    check(len(lineage(TA, rom, emu)) == 10, f"after B's autocleanup of 10: {len(lineage(TA, rom, emu))} versions")
    st, r = upload(TA, A, rom, f"{RUN} retention other lineage".encode(), emulator=f"c1b{RUN}")
    check(st == 409 and r.get("detail") == "Slot has a newer save since your last sync", f"A after B, another lineage: HTTP {st} {r}")


@case
def first_upload_guard():
    """a device without a record of the slot's head gets 409; /downloaded with any hash lets it in"""
    rom = ROMS[1]
    join(TA, A, rom)
    ok(*upload(TA, A, rom, f"{RUN} guard A".encode()), "A's upload")
    st, r = upload(TB, B, rom, f"{RUN} guard B".encode())
    check(st == 409 and r.get("detail") == "Slot has a newer save since your last sync", f"B's first upload: HTTP {st} {r}")
    h = head(TA, rom)
    st, _ = call("POST", f"/api/saves/{h['id']}/downloaded", {"device_id": B, "content_hash": md5(b"not the server's")}, token=TB)
    check(st == 200, f"/downloaded with a wrong hash: HTTP {st}")
    st, r = upload(TB, B, rom, f"{RUN} guard B".encode())
    check(st == 200, f"B's upload after /downloaded: HTTP {st} {r}")
    st, r = call("POST", "/api/saves/999999999/downloaded", {"device_id": B}, token=TB)
    check(st == 404 and r.get("detail") == "Save with ID 999999999 not found", f"/downloaded of no save: HTTP {st} {r}")


@case
def identical_content():
    """the same bytes again: the old version made the head, no new one"""
    rom = ROMS[2]
    join(TA, A, rom)
    x = ok(*upload(TA, A, rom, f"{RUN} same X".encode()), "X")
    tick()
    ok(*upload(TA, A, rom, f"{RUN} same Y".encode()), "Y")
    tick()
    again = ok(*upload(TA, A, rom, f"{RUN} same X".encode()), "X again")
    check(again["id"] == x["id"], f"X again stored as {again['id']}, not {x['id']}")
    check(again["file_name"] == x["file_name"], "X again renamed")
    emu = x["emulator"]
    check(len(lineage(TA, rom, emu)) == 2, f"{len(lineage(TA, rom, emu))} versions after X, Y, X")
    check(head(TA, rom)["id"] == x["id"], "X isn't the head again")


@case
def clock_skew():
    """negotiation's action with the device's clock a day off, against what is true"""
    expect = [(-1, True, False, "no_op"), (-1, True, True, "download"), (+1, False, True, "upload"), (+1, True, False, "upload")]
    for k, (skew, a_changed, b_changed, romm_says) in enumerate(expect):
        rom = ROMS[3 + k]
        emu = EMU[0]
        join(TA, A, rom)
        v1 = f"{RUN} skew{k} v1".encode()
        ok(*upload(TA, A, rom, v1), "v1")
        st, r = negotiate(TA, A, [entry(rom, v1, now(), emu)], rom_ids=[rom])
        ok(st, r, "negotiate")
        complete(TA, r["session_id"])
        tick()
        if b_changed:
            join(TB, B, rom)
            ok(*upload(TB, B, rom, f"{RUN} skew{k} from B".encode()), "B's change")
            tick()
        local = f"{RUN} skew{k} A v2".encode() if a_changed else v1
        st, r = negotiate(TA, A, [entry(rom, local, now() + timedelta(days=skew), emu)], rom_ids=[rom])
        ok(st, r, "negotiate")
        complete(TA, r["session_id"])
        acts = [o["action"] for o in r["operations"] if o["rom_id"] == rom]
        check(acts == [romm_says], f"skew {skew:+d}d, A {'changed' if a_changed else 'unchanged'}, server "
                                   f"{'changed' if b_changed else 'unchanged'}: {acts}, RomM 5.4.1 says {romm_says}")


@case
def same_second_head():
    """versions written in one second: the listing's newest (updated_at, id) is the last; negotiation offers one of them
    in the database's order, run to run the first, the middle or the last"""
    rom = ROMS[7]
    join(TA, A, rom)
    for attempt in range(8):
        tick()
        ids = [ok(*upload(TA, A, rom, f"{RUN} tie {attempt}.{i}".encode()), "upload")["id"] for i in range(3)]
        rows = {s["id"]: s for s in listing(TA, rom)}
        if len({ts(rows[i]["updated_at"]) for i in ids}) == 1:
            break
    else:
        check(False, "three uploads never landed in one second")
    check(head(TA, rom)["id"] == ids[2], "the listing's head isn't the last of the tie")
    st, r = negotiate(TC, C, [], rom_ids=[rom], restore_unlisted=True)
    ok(st, r, "negotiate")
    complete(TC, r["session_id"])
    offered = [o["save_id"] for o in r["operations"] if o["rom_id"] == rom and o["action"] == "download"]
    check(len(offered) == 1 and offered[0] in ids, f"negotiation offered {offered} for the tie {ids}")


@case
def version_tag():
    """a slot version's name carries the server's local time, not UTC"""
    rom = ROMS[8]
    join(TA, A, rom)
    s = ok(*upload(TA, A, rom, f"{RUN} tag".encode(), emulator=f"c6t{RUN}"), "upload")
    m = re.search(r" \[(\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2})\]\.srm$", s["file_name"])
    check(m, f"no version tag in {s['file_name']!r}")
    tag = datetime.strptime(m.group(1), "%Y-%m-%d_%H-%M-%S").replace(tzinfo=TZ)
    at = ts(next(x for x in listing(TA, rom) if x["id"] == s["id"])["updated_at"])
    check(abs((tag - at).total_seconds()) <= 2, f"tag {m.group(1)} is not {at} in {TZ}")
    check(s["file_name_no_tags"] == "Game", f"file_name_no_tags {s['file_name_no_tags']!r}")


@case
def custom_slot():
    """a slot of the client's naming, a space in it, is a slot as autosave is: a version per upload named with the
    server's time, the upload guard, 50 versions kept per lineage; slot=<name> lists that slot alone, and autosave's
    listing none of it"""
    # (a ROM no other case and no card of the interoperability table has: on a server kept from run to run every
    # run's slot stays, and a negotiation for the ROM offers each one's head as a download)
    rom, name = ROMS[22], f"my slot {RUN}"
    sl = lambda tok: ok(*call("GET", "/api/saves", token=tok, query={"rom_id": rom, "slot": name}), "custom listing")
    s = ok(*upload(TA, A, rom, f"{RUN} custom 0".encode(), slot=name), "first upload")
    check(s["slot"] == name, f"slot {s['slot']!r}")
    check(re.search(r" \[\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}\]\.srm$", s["file_name"]), f"no version tag in {s['file_name']!r}")
    st, r = upload(TB, B, rom, f"{RUN} custom B".encode(), slot=name)
    check(st == 409 and r.get("detail") == "Slot has a newer save since your last sync", f"B's first upload: HTTP {st} {r}")
    for i in range(1, 55):
        ok(*upload(TA, A, rom, f"{RUN} custom {i}".encode(), slot=name), f"upload {i}")
    rows = sl(TA)
    check(all(x["slot"] == name for x in rows), f"slot=<name> listing: {sorted({x['slot'] for x in rows})}")
    check(len([x for x in rows if x["emulator"] == EMU[0]]) == 50, f"after 55 uploads: {len(rows)} versions, not 50")
    check(not [x for x in listing(TA, rom) if x["slot"] == name], "the autosave listing holds the custom slot's saves")


@case
def rom_id_limit():
    """at most 500 ROM ids, in the listing and in negotiation"""
    st, _ = call("GET", "/api/saves", token=TA, query={"rom_ids": list(range(1, 502))})
    check(st == 422, f"listing with 501 ids: HTTP {st}")
    st, _ = negotiate(TA, A, [], rom_ids=list(range(1, 502)))
    check(st == 422, f"negotiate with 501 ids: HTTP {st}")
    st, r = negotiate(TA, A, [], rom_ids=list(range(1, 501)))
    check(st == 200, f"negotiate with 500 ids: HTTP {st}")
    complete(TA, r["session_id"])


@case
def fingerprint():
    """a device is matched on hostname and platform when it has no MAC"""
    s1, d1 = device(TA, f"conformance-{RUN}-shared")
    s2, d2 = device(TB, f"conformance-{RUN}-shared")
    check((s1, s2) == (201, 200) and d1["device_id"] == d2["device_id"], f"same hostname twice: {s1} {d1} / {s2} {d2}")
    s3, d3 = device(TA, f"conformance-{RUN}-other")
    check(s3 == 201 and d3["device_id"] != d1["device_id"], f"another hostname: {s3} {d3}")
    s4, d4 = device(TA, f"conformance-{RUN}-shared", allow_duplicate=True)
    check(s4 == 201 and d4["device_id"] != d1["device_id"], f"allow_duplicate: {s4} {d4}")
    s5, d5 = device(TA, f"conformance-{RUN}-shared", allow_existing=False)
    check(s5 == 409 and isinstance(d5.get("detail"), dict) and d5["detail"].get("error") == "device_exists", f"allow_existing=false: {s5} {d5}")


@case
def existing_keeps_name():
    """a device given back by allow_existing keeps the name it had: the POST's name is neither answered nor stored,
    and only a PUT renames it"""
    host = f"conformance-{RUN}-named"
    s1, d1 = device(TA, host, name="First name")
    s2, d2 = device(TA, host, name="Second name")
    check((s1, s2) == (201, 200) and d2["device_id"] == d1["device_id"] and d2.get("name") == "First name",
          f"the same hostname under another name: {s1} {d1} / {s2} {d2}")
    st, g = call("GET", f"/api/devices/{d1['device_id']}", token=TA)
    check(st == 200 and g.get("name") == "First name", f"stored after the second POST: {st} {g.get('name')!r}")
    st, p = call("PUT", f"/api/devices/{d1['device_id']}", {"name": "Second name"}, token=TA)
    check(st == 200 and p.get("name") == "Second name", f"PUT with the name: {st} {p.get('name')!r}")


@case
def sync_disabled():
    """a device with sync turned off can't negotiate"""
    st, d = device(TA, f"conformance-{RUN}-off")
    dev = ok(st, d, "device")["device_id"]
    ok(*call("PUT", f"/api/devices/{dev}", {"sync_enabled": False}, token=TA), "PUT")
    st, r = negotiate(TA, dev, [])
    check(st == 400 and r.get("detail") == "Sync is disabled for this device", f"negotiate with sync off: HTTP {st} {r}")
    ok(*call("PUT", f"/api/devices/{dev}", {"sync_enabled": True}, token=TA), "PUT")
    st, r = negotiate(TA, dev, [], rom_ids=[ROMS[9]])
    check(st == 200, f"negotiate with sync on again: HTTP {st}")
    st1, _ = complete(TA, r["session_id"])
    st2, r2 = complete(TA, r["session_id"])
    check(st1 == 200 and st2 == 400 and r2.get("detail") == "Session is already COMPLETED", f"complete twice: {st1}, {st2} {r2}")


@case
def validation_first():
    """a malformed request is refused (422) before anything is looked up"""
    st, _ = call("POST", "/api/saves", token=TA, query={"device_id": "no-such-device", "slot": SLOT}, files={"saveFile": ("Game.srm", b"x" * 200)})
    check(st == 422, f"upload without rom_id, an unknown device: HTTP {st}")
    st, _ = call("POST", "/api/saves", token=TA, query={"rom_id": ROMS[10], "slot": SLOT}, raw=b"not multipart")
    check(st == 422, f"upload without a body's content type: HTTP {st}")
    st, _ = call("GET", "/api/saves", token=TA, query={"rom_ids": [0]})
    check(st == 422, f"listing rom_ids=0: HTTP {st}")
    st, _ = call("GET", "/api/saves", token=TA, query={"rom_ids": [ROMS[0]] * 501})
    check(st == 422, f"listing one id 501 times: HTTP {st}")
    st, _ = call("POST", "/api/saves/999999999/downloaded", {}, token=TA)
    check(st == 422, f"/downloaded of no save without device_id: HTTP {st}")
    st, _ = call("POST", "/api/sync/negotiate", raw=b"{not json", ctype="application/json", token=TA)
    check(st == 422, f"negotiate with a body that isn't JSON: HTTP {st}")
    st, _ = call("POST", "/api/saves/delete", {}, token=TA)
    check(st == 422, f"/delete without saves: HTTP {st}")


@case
def device_fields():
    """name is optional; a MAC that matches no device falls back to hostname and platform; capabilities start
    null; a PUT with nothing in it changes nothing"""
    host = f"conformance-{RUN}-fields"
    st, d = call("POST", "/api/devices", {"platform": "BananaOS", "hostname": host}, token=TA)
    check(st == 201, f"no name: HTTP {st} {d}")
    dev = d["device_id"]
    st, d2 = call("POST", "/api/devices", {"platform": "BananaOS", "hostname": host, "mac_address": "02:00:00:00:00:01", "name": "x"}, token=TA)
    check(st == 200 and d2["device_id"] == dev, f"a new MAC, the same hostname and platform: HTTP {st} {d2}")
    st, g = call("GET", f"/api/devices/{dev}", token=TA)
    check(st == 200 and g.get("capabilities") is None, f"capabilities: {g.get('capabilities')!r}")
    tick()
    st, p = call("PUT", f"/api/devices/{dev}", {}, token=TA)
    check(st == 200 and p["updated_at"] == g["updated_at"], f"PUT with nothing: updated_at {g['updated_at']} -> {p.get('updated_at')}")


@case
def device_delete():
    """a deleted device leaves no sync record behind and is no save's origin any more"""
    rom = ROMS[10]
    st, d = device(TB, f"conformance-{RUN}-gone")
    gone = ok(st, d, "device")["device_id"]
    join(TB, gone, rom)
    s = ok(*upload(TB, gone, rom, f"{RUN} from a device to be deleted".encode()), "upload")
    check(s["origin_device_id"] == gone, f"origin {s['origin_device_id']}")
    st, _ = call("DELETE", f"/api/devices/{gone}", token=TB)
    check(st == 204, f"DELETE: HTTP {st}")
    st, r = call("GET", f"/api/saves/{s['id']}", token=TA, query={"device_id": A})
    ok(st, r, "save")
    check(r["origin_device_id"] is None, f"origin after the delete: {r['origin_device_id']}")
    check(all(x["device_id"] != gone for x in r["device_syncs"]), f"device_syncs after the delete: {r['device_syncs']}")


@case
def session_counts():
    """an upload or a download naming a session counts as one of its operations; sessions list newest first"""
    rom = ROMS[11]
    join(TA, A, rom)
    st, r = negotiate(TA, A, [], rom_ids=[rom])
    sid = ok(st, r, "negotiate")["session_id"]
    s = ok(*upload(TA, A, rom, f"{RUN} counted".encode(), session_id=sid), "upload")
    call("GET", f"/api/saves/{s['id']}/content", token=TA, query={"device_id": A, "session_id": sid, "optimistic": "false"})
    st, x = call("GET", f"/api/sync/sessions/{sid}", token=TA)
    check(st == 200 and x["operations_completed"] == 2, f"after an upload and a download: {x.get('operations_completed')}")
    st, r2 = negotiate(TA, A, [], rom_ids=[rom])
    complete(TA, r2["session_id"])
    complete(TA, sid)
    st, rows = call("GET", "/api/sync/sessions", token=TA, query={"device_id": A})
    ids = [x["id"] for x in rows]
    check(ids == sorted(ids, reverse=True), f"sessions not newest first: {ids[:5]}")


@case
def unslotted_rows():
    """outside a slot, the same name is one row per emulator"""
    rom = ROMS[12]
    q = lambda emu: {"rom_id": rom, "device_id": A, "emulator": emu}
    a1 = ok(*call("POST", "/api/saves", token=TA, query=q(f"u1{RUN}"), files={"saveFile": ("Plain.srm", b"one")}), "a1")
    a2 = ok(*call("POST", "/api/saves", token=TA, query=q(f"u1{RUN}"), files={"saveFile": ("Plain.srm", b"two")}), "a2")
    b1 = ok(*call("POST", "/api/saves", token=TA, query=q(f"u2{RUN}"), files={"saveFile": ("Plain.srm", b"three")}), "b1")
    check(a1["id"] == a2["id"] and a1["slot"] is None, f"same emulator: rows {a1['id']}, {a2['id']}")
    check(b1["id"] != a1["id"], f"another emulator replaced the row {a1['id']}")
    # (a listing by rom_ids with no slot named gives those rows beside a slot's, their slot null and their MD5 given;
    # one that names the slot gives none of them)
    # (the slot's head taken note of first: on a server an earlier run left a save in this ROM's slot, a device that
    # never saw it is refused, 409)
    join(TA, A, rom)
    sl = ok(*call("POST", "/api/saves", token=TA, query={"rom_id": rom, "device_id": A, "slot": SLOT, "emulator": f"u3{RUN}"},
                  files={"saveFile": ("Plain.srm", b"in the slot")}), "slotted")
    rows = ok(*call("GET", "/api/saves", token=TA, query={"rom_ids": rom}), "listing with no slot")
    mine = {x["id"]: x for x in rows}
    check(a1["id"] in mine and b1["id"] in mine and sl["id"] in mine, f"no slot named: rows {sorted(mine)}")
    check(mine[a1["id"]]["slot"] is None and mine[a1["id"]]["content_hash"] == md5(b"two"), f"the row outside: {mine.get(a1['id'])}")
    check(not [x for x in listing(TA, rom) if x["id"] in (a1["id"], b1["id"])], "the slot's listing holds rows from outside it")


@case
def lineage_is_case_blind():
    """a lineage's emulator compared without regard to case, as MariaDB's default collation compares it (the database
    RomM's own compose file runs): an autocleanup under one spelling cuts versions written under another"""
    rom = ROMS[4]
    up, low = f"Case{RUN}", f"case{RUN}"
    join(TA, A, rom)
    ok(*upload(TA, A, rom, f"{RUN} case 1".encode(), emulator=up), "1")
    ok(*upload(TA, A, rom, f"{RUN} case 2".encode(), emulator=up), "2")
    ok(*upload(TA, A, rom, f"{RUN} case 3".encode(), emulator=low), "3")
    ok(*upload(TA, A, rom, f"{RUN} case 4".encode(), emulator=low, autocleanup="true", autocleanup_limit=2), "4")
    left = [s["emulator"] for s in listing(TA, rom) if (s["emulator"] or "").lower() == low]
    check(len(left) == 2, f"after an autocleanup of 2 under {low}: {left} left of either spelling")


@case
def platform_rom_count():
    """each platform's rom_count is the number of its ROMs (Grout leaves a platform that says 0 out of its cache, and
    with it every save of that platform's games)"""
    for p in ok(*call("GET", "/api/platforms", token=TA), "platforms"):
        r = ok(*call("GET", "/api/roms", token=TA, query={"platform_ids": p["id"], "limit": 1}), "roms")
        check(p["rom_count"] == r["total"], f"{p['fs_slug']}: rom_count {p['rom_count']}, {r['total']} ROMs listed")


@case
def promotion_prunes_upload_lineage():
    """identical content promoted from another lineage: the lineage pruned is the upload's"""
    rom = ROMS[13]
    l1, l2 = f"p1{RUN}", f"p2{RUN}"
    join(TA, A, rom)
    ok(*upload(TA, A, rom, f"{RUN} promo X".encode(), emulator=l1), "X")
    ok(*upload(TA, A, rom, f"{RUN} promo Y1".encode(), emulator=l2), "Y1")
    ok(*upload(TA, A, rom, f"{RUN} promo Y2".encode(), emulator=l2), "Y2")
    ok(*upload(TA, A, rom, f"{RUN} promo X".encode(), emulator=l2, autocleanup="true", autocleanup_limit=1), "X again")
    check((len(lineage(TA, rom, l1)), len(lineage(TA, rom, l2))) == (1, 1),
          f"after X again under {l2} with autocleanup 1: {len(lineage(TA, rom, l1))} and {len(lineage(TA, rom, l2))} versions")


@case
def version_names_held():
    """a version's name is held once given, even when the upload is then refused"""
    rom = ROMS[14]
    join(TA, A, rom)
    ok(*upload(TA, A, rom, f"{RUN} held 0".encode()), "first")
    tick()
    for i in range(3):
        st, _ = upload(TB, B, rom, f"{RUN} held B{i}".encode())
        check(st == 409, f"B's upload: HTTP {st}")
    s = ok(*upload(TA, A, rom, f"{RUN} held A".encode()), "A")
    tag = datetime.strptime(re.search(r"\[(\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2})\]", s["file_name"]).group(1), "%Y-%m-%d_%H-%M-%S").replace(tzinfo=TZ)
    at = ts(next(x for x in listing(TA, rom) if x["id"] == s["id"])["updated_at"])
    # (the refused three hold the names of their second and the two after it, so A's is named past them; without the
    # hold it is named its own second. A second's slack: the four requests needn't all fall in one second)
    check((tag - at).total_seconds() >= 1, f"A's version named {s['file_name']} at {at}: the refused uploads' names weren't held")


@case
def state_row_and_replacing():
    """a state's row has no content hash at all, and an upload of the same name replaces it in place: the same id, the
    old bytes gone, no version kept"""
    rom, name = ROMS[16], f"Spot {RUN}.state"
    q = {"rom_id": rom, "emulator": f"s{RUN}"}
    first = ok(*call("POST", "/api/states", token=TA, query=q, files={"stateFile": (name, f"{RUN} one".encode())}), "state")
    check("content_hash" not in first, f"an uploaded state's row: {sorted(first)}")
    tick()
    data = f"{RUN} two".encode()
    second = ok(*call("POST", "/api/states", token=TA, query=q, files={"stateFile": (name, data)}), "state again")
    check(second["id"] == first["id"], f"the same name: id {first['id']}, then {second['id']}")
    rows = [s for s in ok(*call("GET", "/api/states", token=TA, query={"rom_id": rom}), "states") if s["file_name"] == name]
    check(len(rows) == 1 and "content_hash" not in rows[0], f"listed after: {rows}")
    check(rows[0]["updated_at"] == second["updated_at"], f"the upload said {second['updated_at']}, the listing {rows[0]['updated_at']}")
    content = ok(*call("GET", f"/api/states/{first['id']}/content", token=TA), "content")
    check(content == data, f"the content after: {content!r}")


@case
def put_without_file():
    """a PUT with no file leaves the save as it was"""
    rom = ROMS[15]
    join(TA, A, rom)
    s = ok(*upload(TA, A, rom, f"{RUN} put".encode()), "upload")
    before = ts(next(x for x in listing(TA, rom) if x["id"] == s["id"])["updated_at"])
    tick()
    st, r = call("PUT", f"/api/saves/{s['id']}", token=TA, raw=b"", ctype="multipart/form-data; boundary=x")
    after = ts(next(x for x in listing(TA, rom) if x["id"] == s["id"])["updated_at"])
    check(st == 200 and after == before, f"PUT with no file (HTTP {st}): updated_at {before} -> {after}")


@case
def delete_in_order():
    """/delete takes the saves one by one: a missing one is refused after the ones before it are gone; an unslotted
    save deleted is no reason for negotiation to say delete"""
    rom = ROMS[16]
    join(TA, A, rom)
    s = ok(*upload(TA, A, rom, f"{RUN} doomed".encode()), "upload")
    st, _ = call("POST", "/api/saves/delete", {"saves": [s["id"], 999999999]}, token=TA)
    check(st == 404, f"/delete with a missing id: HTTP {st}")
    check(call("GET", f"/api/saves/{s['id']}", token=TA)[0] == 404, "the save before the missing one wasn't deleted")
    data = f"{RUN} unslotted doomed".encode()
    u = ok(*call("POST", "/api/saves", token=TA, query={"rom_id": rom, "emulator": f"d{RUN}"}, files={"saveFile": ("Plain.srm", data)}), "unslotted")
    ok(*call("POST", "/api/saves/delete", {"saves": [u["id"]]}, token=TA), "delete")
    c = {"rom_id": rom, "file_name": "Plain.srm", "slot": None, "emulator": f"d{RUN}", "content_hash": md5(data),
         "updated_at": iso(now() - timedelta(days=1)), "file_size_bytes": len(data)}
    st, r = negotiate(TA, A, [c], rom_ids=[rom])
    ok(st, r, "negotiate")
    complete(TA, r["session_id"])
    acts = [o["action"] for o in r["operations"] if o["rom_id"] == rom and o["slot"] is None]
    check(acts == ["upload"], f"an unslotted save deleted on the server: {acts}")


@case
def device_sync_rows():
    """device_syncs as listed with device_id: the asking device's own row, is_current once it has reported the head
    (an upload of its own, or /downloaded), and not on a head another device put there since"""
    rom = ROMS[3]
    join(TA, A, rom)
    ok(*upload(TA, A, rom, f"{RUN} rows A1".encode()), "A's upload")

    def mine(tok, dev):
        rows = ok(*call("GET", "/api/saves", token=tok, query={"rom_id": rom, "slot": SLOT, "device_id": dev}), "listing")
        h = max(rows, key=lambda s: (ts(s["updated_at"]), s["id"]))
        return h, [x for x in h["device_syncs"] if x["device_id"] == dev]
    h, me = mine(TA, A)
    check(len(me) == 1 and me[0]["is_current"] is True, f"A's row after its own upload: {me}")
    h, me = mine(TB, B)
    check(not me or me[0]["is_current"] is False, f"B's row before it reported the head: {me}")
    ok(*call("POST", f"/api/saves/{h['id']}/downloaded", {"device_id": B, "content_hash": h["content_hash"]}, token=TB), "downloaded")
    h, me = mine(TB, B)
    check(len(me) == 1 and me[0]["is_current"] is True, f"B's row after /downloaded: {me}")
    tick()
    ok(*upload(TA, A, rom, f"{RUN} rows A2".encode()), "A's second upload")
    h2, me = mine(TB, B)
    check(h2["id"] != h["id"] and (not me or me[0]["is_current"] is False), f"B's row on the head A put there since: {me}")
    # (the head's origin among them for another device, by the name it was registered under: what a conflict names
    # the server's side by)
    org = [x for x in h2["device_syncs"] if x["device_id"] == h2["origin_device_id"]]
    check(h2["origin_device_id"] == A and len(org) == 1 and org[0]["device_name"] == f"conformance-{RUN}-a",
          f"the origin's row on B's listing: {h2['origin_device_id']} {org}")


@case
def empty_slot_and_download_path():
    """slot= is a slot named "", and download_path carries the time as the server prints it"""
    rom = ROMS[17]
    s = ok(*call("POST", "/api/saves", token=TA, query={"rom_id": rom, "slot": "", "emulator": f"e{RUN}"},
                 files={"saveFile": ("Empty.srm", f"{RUN} empty slot".encode())}), "upload")
    check(s["slot"] == "", f"slot {s['slot']!r}")
    rows = ok(*call("GET", "/api/saves", token=TA, query={"rom_id": rom, "slot": ""}), "listing")
    check(rows and all(x["slot"] == "" for x in rows), f"slot= listing: {[x['slot'] for x in rows]}")
    check(" " in urllib.parse.urlsplit(s["download_path"]).query.split("timestamp=")[1].split("&")[0],
          f"download_path {s['download_path']!r}")


@case
def rom_file_hashes():
    """a ROM's files only with with_files=true; a zip hashed as its members' bytes in name order, each member listed"""
    pid = next(p["id"] for p in ok(*call("GET", "/api/platforms", token=TA), "platforms") if p["fs_slug"] == "gba")
    def every(**q):
        out = []
        while True:
            r = ok(*call("GET", "/api/roms", token=TA, query={"platform_ids": pid, "limit": 500, "offset": len(out), **q}), "roms")
            out += r["items"]
            if not r["items"] or len(out) >= r["total"]:
                return out
    bare = every()
    check(all(r["files"] == [] for r in bare), "files listed without with_files")
    full = {r["id"]: r for r in every(with_files="true")}
    plain = next(r for r in bare if r["fs_name"].endswith(".gba"))
    zipped = next((r for r in bare if r["fs_name"].endswith(".zip")), None)
    check(zipped is not None, "the cases need a zipped GBA ROM of two members on the server")
    data = ok(*call("GET", f"/api/roms/{plain['id']}/content/{urllib.parse.quote(plain['fs_name'])}", token=TA), "content")
    f = full[plain["id"]]["files"]
    check(plain["md5_hash"] == md5(data) and len(f) == 1 and f[0]["md5_hash"] == md5(data) and f[0]["archive_members"] is None,
          f"a plain ROM: {plain['md5_hash']}, {f}, its bytes {md5(data)}")
    zdata = ok(*call("GET", f"/api/roms/{zipped['id']}/content/{urllib.parse.quote(zipped['fs_name'])}", token=TA), "content")
    with zipfile.ZipFile(io.BytesIO(zdata)) as z:
        inside = sorted(z.infolist(), key=lambda i: i.filename)
        composite = md5(b"".join(z.read(i) for i in inside))
        want = [{"name": i.filename, "size": i.file_size, "crc_hash": format(i.CRC, "08x"), "md5_hash": md5(z.read(i))} for i in inside]
    f = full[zipped["id"]]["files"]
    check(len(inside) == 2 and len(f) == 1, f"a zip of {len(inside)} members, {len(f)} files")
    got = [{k: m[k] for k in ("name", "size", "crc_hash", "md5_hash")} for m in f[0]["archive_members"] or []]
    check(sorted(got, key=lambda m: m["name"]) == want, f"archive_members {got}, the zip's {want}")
    check(f[0]["md5_hash"] == composite == zipped["md5_hash"] != md5(zdata),
          f"a zip's MD5 {f[0]['md5_hash']} (ROM {zipped['md5_hash']}), its members' {composite}, the zip file's {md5(zdata)}")


@case
def rom_name_tags():
    """a ROM's file name parsed as RomM's roms handler does: a region by name in regions, a "Rev 1" in revision, and
    another (...) or [...] tag, a hack's name and version, kept whole in tags"""
    pid = next(p["id"] for p in ok(*call("GET", "/api/platforms", token=TA), "platforms") if p["fs_slug"] == "gba")
    roms = ok(*call("GET", "/api/roms", token=TA, query={"platform_ids": pid, "limit": 500}), "roms")["items"]
    hack = next((r for r in roms if "[Hack by " in r["fs_name"]), None)
    check(hack is not None, "the cases need a GBA ROM named with a [Hack by ...] tag on the server")
    want = hack["fs_name"].split("[")[1].split("]")[0]
    check(hack["tags"] == [want] and hack["revision"] in ("", None), f"a hack's tags {hack['tags']}, revision {hack['revision']!r}")
    usa = next(r for r in roms if r["fs_name"].endswith(" (USA).gba") and "[" not in r["fs_name"])
    check(usa["regions"] == ["USA"] and usa["tags"] == [] and usa["revision"] in ("", None),
          f"{usa['fs_name']}: regions {usa['regions']}, tags {usa['tags']}, revision {usa['revision']!r}")


for f in CASES:
    EMU[0] = f"{f.__name__}-{RUN}"
    try:
        f()
        print(f"ok   {f.__name__}")
    except AssertionError as e:
        failures.append(f.__name__)
        print(f"FAIL {f.__name__}: {e}")
print(f"romm-conformance: {len(CASES) - len(failures)}/{len(CASES)} cases as RomM 5.4.1 ({BASE}, run {RUN})")
sys.exit(1 if failures else 0)
