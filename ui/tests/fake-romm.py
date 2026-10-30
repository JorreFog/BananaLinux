#!/usr/bin/env python3
"""A stand-in for a RomM server (rommapp/romm 5.4.1), the endpoints banana-sync uses, with the same paths, fields,
status codes and conflict rule as RomM's own code (backend/endpoints/{device/auth,client_tokens,platform,saves,
states}.py, roms/__init__.py, roms/files.py). Python standard library only; the tests' own hooks are under /test/.

    fake-romm.py <port> <library dir>      the library: <platform fs_slug>/<rom file or folder>, covers as
                                           <library>/covers/<rom id>.png once /test/cover is called
FAKE_ROMM_TLS=<cert.pem>:<key.pem> serves https instead (RomM behind a reverse proxy with a certificate of its own).
Test hooks: POST /test/approve {"user_code"} (a user approving the device in RomM's web UI), POST /test/paircode
-> {"code"} (a client token's pairing code), POST /test/save {"rom_id","file_name","data"(hex)} (a save written from
another device or the web UI), POST /test/delete-rom {"id"}, GET /test/log (the requests seen).
"""
import hashlib
import http.server
import json
import os
import secrets
import ssl
import sys
import threading
import time
import urllib.parse
from datetime import datetime, timezone
from email.parser import BytesParser
from email.policy import HTTP

PORT, LIB = int(sys.argv[1]), sys.argv[2]
SLUGS = {"nds": "nds", "gba": "gba", "psx": "psx", "megadrive": "genesis", "3ds": "3ds"}
lock = threading.Lock()
S = {"pending": {}, "tokens": {}, "pair_codes": {}, "saves": {}, "states": {}, "syncs": {}, "next": 1, "log": [], "deleted": set(), "ids": {}}


def now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f+00:00")


def nid():
    S["next"] += 1
    return S["next"]


def md5(b):
    return hashlib.md5(b).hexdigest()


def platforms():
    out = []
    for i, fs in enumerate(sorted(os.listdir(LIB))):
        if fs == "covers" or not os.path.isdir(os.path.join(LIB, fs)):
            continue
        out.append({"id": i + 1, "slug": SLUGS.get(fs, fs), "fs_slug": fs, "name": fs.upper(), "rom_count": 0,
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
            for k, fp in enumerate(paths):
                data = open(fp, "rb").read()
                files.append({"id": rid * 10 + k, "rom_id": rid, "file_name": os.path.basename(fp),
                              "file_path": f"{p['fs_slug']}/{name}" if multi else p["fs_slug"], "file_size_bytes": len(data),
                              "full_path": fp, "crc_hash": None, "md5_hash": md5(data), "sha1_hash": hashlib.sha1(data).hexdigest(),
                              "category": None})
            stem = name if multi else os.path.splitext(name)[0]
            cover = os.path.join(LIB, "covers", f"{rid}.png")
            out.append({
                "id": rid, "platform_id": p["id"], "platform_slug": p["slug"], "platform_fs_slug": p["fs_slug"],
                "fs_name": name, "fs_name_no_tags": stem.split(" (")[0], "fs_name_no_ext": stem,
                "fs_extension": "" if multi else os.path.splitext(name)[1][1:], "fs_path": f"roms/{p['fs_slug']}",
                "fs_size_bytes": sum(f["file_size_bytes"] for f in files), "name": stem.split(" (")[0],
                "summary": f"About {stem}.", "metadatum": {"rom_id": rid, "genres": ["Puzzle"], "publishers": ["Banana"],
                                                         "first_release_date": 1262304000000},
                "path_cover_large": f"/assets/romm/resources/roms/{p['id']}/{rid}/cover/big.png?ts=1" if os.path.exists(cover) else "",
                "has_multiple_files": multi, "has_simple_single_file": not multi, "missing_from_fs": False,
                "files": files, "full_path": full})
    return out


def save_schema(s, kind, device=None):
    d = {k: s[k] for k in ("id", "rom_id", "file_name", "updated_at", "emulator", "content_hash")}
    d.update(user_id=1, file_size_bytes=len(s["data"]), missing_from_fs=False, slot=None,
             file_name_no_ext=os.path.splitext(s["file_name"])[0], file_extension=os.path.splitext(s["file_name"])[1][1:],
             download_path=f"/api/{kind}/{s['id']}/content/{s['file_name']}", screenshot=None, created_at=s["updated_at"])
    return d


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def send(self, code, body=b"", ctype="application/json", headers=None):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def detail(self, code, msg):
        self.send(code, {"detail": msg})

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def user(self):
        a = self.headers.get("Authorization", "")
        if a.startswith("Bearer ") and a[7:] in S["tokens"]:
            return S["tokens"][a[7:]]
        return None

    def file_part(self, raw, field):
        msg = BytesParser(policy=HTTP).parsebytes(b"Content-Type: " + self.headers["Content-Type"].encode() + b"\r\n\r\n" + raw)
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

    def route(self, method):
        u = urllib.parse.urlsplit(self.path)
        q = {k: v[-1] for k, v in urllib.parse.parse_qs(u.query).items()}
        p = urllib.parse.unquote(u.path)
        raw = self.body() if method in ("POST", "PUT") else b""
        with lock:
            S["log"].append(f"{method} {u.path}?{u.query}" + (f" range={self.headers['Range']}" if self.headers.get("Range") else ""))
            try:
                self.handle_route(method, p, q, raw)
            except BrokenPipeError:
                pass

    def handle_route(self, method, p, q, raw):
        if p == "/api/heartbeat":
            return self.send(200, {"SYSTEM": {"VERSION": "5.4.1", "GIT_BRANCH": None, "SHOW_SETUP_WIZARD": False}})
        # ---- the tests' hooks
        if p == "/test/approve":
            uc = json.loads(raw)["user_code"]
            for dc, v in S["pending"].items():
                if v["user_code"] == uc:
                    v["status"] = "approved"
                    return self.send(200, {"device_id": v["device_id"], "device_name": v["name"]})
            return self.detail(404, "Unknown or expired code")
        if p == "/test/paircode":
            code = "".join(secrets.choice("ABCDEFGHJKMNPQRSTUVWXYZ23456789") for _ in range(8))
            S["pair_codes"][code] = True
            return self.send(200, {"code": code, "expires_in": 60})
        if p == "/test/save":
            b = json.loads(raw)
            kind = b.get("kind", "saves")
            data = bytes.fromhex(b["data"])
            for s in S[kind].values():
                if s["rom_id"] == b["rom_id"] and s["file_name"] == b["file_name"]:
                    s.update(data=data, content_hash=md5(data), updated_at=b.get("updated_at") or now())
                    return self.send(200, save_schema(s, kind))
            s = {"id": nid(), "rom_id": b["rom_id"], "file_name": b["file_name"], "data": data, "content_hash": md5(data),
                 "updated_at": b.get("updated_at") or now(), "emulator": None}
            S[kind][s["id"]] = s
            return self.send(200, save_schema(s, kind))
        if p == "/test/delete-rom":
            S["deleted"].add(json.loads(raw)["id"])
            return self.send(200, {})
        if p == "/test/log":
            return self.send(200, S["log"])
        if p == "/test/state":
            return self.send(200, {k: {i: save_schema(s, k) for i, s in S[k].items()} for k in ("saves", "states")})
        # ---- pairing (open endpoints)
        if p == "/api/auth/device/init" and method == "POST":
            b = json.loads(raw)
            for k in ("client_device_identifier", "name", "client", "requested_scopes"):
                if not b.get(k):
                    return self.send(422, {"detail": [{"loc": ["body", k], "msg": "Field required"}]})
            dc, uc = secrets.token_urlsafe(32), secrets.token_hex(2).upper() + "-" + secrets.token_hex(2).upper()
            S["pending"][dc] = {"user_code": uc, "status": "pending", "name": b["name"], "device_id": secrets.token_hex(16), "polled": 0}
            return self.send(201, {"device_code": dc, "user_code": uc, "verification_path": "/pair/device",
                                   "verification_path_complete": f"/pair/device?user_code={uc}", "expires_in": 600, "interval": 1})
        if p == "/api/auth/device/token" and method == "POST":
            v = S["pending"].get(json.loads(raw).get("device_code"))
            if not v:
                return self.detail(400, "expired_token")
            if v["status"] == "pending":
                return self.detail(400, "authorization_pending")
            del S["pending"][json.loads(raw)["device_code"]]
            tok = "rmm_" + secrets.token_hex(20)
            S["tokens"][tok] = {"device": v["device_id"]}
            return self.send(200, {"access_token": tok, "device_id": v["device_id"], "scopes": ["roms.read"], "expires_at": None})
        if p == "/api/client-tokens/exchange" and method == "POST":
            code = json.loads(raw).get("code", "").replace("-", "").upper()
            if not S["pair_codes"].pop(code, None):
                return self.detail(404, "Invalid or expired pairing code")
            tok = "rmm_" + secrets.token_hex(20)
            S["tokens"][tok] = {"device": None}
            return self.send(200, {"id": 1, "name": "t", "scopes": [], "expires_at": None, "last_used_at": None,
                                   "created_at": now(), "user_id": 1, "device_id": None, "raw_token": tok})
        user = self.user()
        if user is None:
            return self.detail(403, "Forbidden")
        if p == "/api/platforms":
            return self.send(200, platforms())
        if p == "/api/roms" and method == "GET":
            pid = int(q.get("platform_ids", q.get("platform_id", 0)))
            items = [r for r in roms() if r["platform_id"] == pid]
            lim, off = int(q.get("limit", 50)), int(q.get("offset", 0))
            return self.send(200, {"items": items[off:off + lim], "total": len(items), "limit": lim, "offset": off})
        parts = p.split("/")
        if p.startswith("/api/roms/") and "/files/content/" in p:      # /api/roms/{file id}/files/content/{name}
            fid = int(parts[3])
            for r in roms():
                for f in r["files"]:
                    if f["id"] == fid:
                        return self.send(200, open(f["full_path"], "rb").read(), "application/octet-stream")
            return self.detail(404, "File not found")
        if p.startswith("/api/roms/") and "/content/" in p:            # /api/roms/{id}/content/{name}
            rid = int(parts[3])
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
        for kind in ("saves", "states"):
            base = f"/api/{kind}"
            dev = q.get("device_id") if kind == "saves" else None
            if dev and dev != user["device"]:
                return self.detail(404, f"Device with ID {dev} not found")
            if p == base and method == "GET":
                pid = int(q.get("platform_id", 0))
                rids = {r["id"]: r for r in roms()}
                return self.send(200, [save_schema(s, kind) for s in S[kind].values()
                                       if s["rom_id"] in rids and (not pid or rids[s["rom_id"]]["platform_id"] == pid)])
            if p == base and method == "POST":
                rid = int(q["rom_id"])
                if rid not in {r["id"] for r in roms()}:
                    return self.detail(404, f"Rom with id {rid} not found")
                name, data = self.file_part(raw, "saveFile" if kind == "saves" else "stateFile")
                if not name:
                    return self.detail(400, "Save file has no filename")
                db = next((s for s in S[kind].values() if s["rom_id"] == rid and s["file_name"] == name), None)
                overwrite = q.get("overwrite", "false") == "true"
                if dev and db and not overwrite:          # saves.py add_save: the device's last sync older than the save
                    sync = S["syncs"].get((dev, db["id"]))
                    if sync and sync < db["updated_at"]:
                        return self.detail(409, "Save has been updated since your last sync")
                if db:
                    db.update(data=data, content_hash=md5(data), updated_at=now(), emulator=q.get("emulator"))
                else:
                    db = {"id": nid(), "rom_id": rid, "file_name": name, "data": data, "content_hash": md5(data),
                          "updated_at": now(), "emulator": q.get("emulator")}
                    S[kind][db["id"]] = db
                if dev:
                    S["syncs"][(dev, db["id"])] = db["updated_at"]
                return self.send(200, save_schema(db, kind))
            if p.startswith(base + "/") and p.endswith("/content") and method == "GET":
                s = S[kind].get(int(parts[3]))
                if not s:
                    return self.detail(404, f"Save with ID {parts[3]} not found")
                if dev and q.get("optimistic", "true") == "true":
                    S["syncs"][(dev, s["id"])] = s["updated_at"]
                return self.send(200, s["data"], "application/octet-stream")
        return self.detail(404, "Not Found")


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True


srv = Server((os.environ.get("FAKE_ROMM_BIND", "127.0.0.1"), PORT), H)
if os.environ.get("FAKE_ROMM_TLS"):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(*os.environ["FAKE_ROMM_TLS"].split(":", 1))
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
