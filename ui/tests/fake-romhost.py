#!/usr/bin/env python3
"""A stand-in for Romhost's save sync (romhost/PROTOCOL.md "Saves"), only as far as the card's side needs one: the
rules, a plan the test writes, each file's bytes and the upload's answer. Not Homeport: it decides nothing.

    fake-romhost.py <port> <plan.json>

plan.json, read again at each request so a test can change it between syncs: {"mode": "saves"|"states"|"off",
"actions": [{"path", "action", "version"?, "sha1"?, ...}], "data"?: hex (every file's bytes, 8 KiB of 0x42 when
left out), "put"?: the status an upload is answered with (200), "delay"?: seconds a file's download waits before
it is sent (a slow link, for a save written meanwhile), "manifest"?: the games' list as Romhost's /api/v1/manifest
gives it ({"generation", "systems": [{"id", "folder", "games": [{"title", "files": [{"path"}]}]}]}; each file is the
data, its size and sha1 filled in here, and its download waits "delay" too)}. A download's sha1, when the plan gives
none, is the data's.
"""
import hashlib
import http.server
import json
import sys
import time


def plan():
    with open(sys.argv[2]) as f:
        p = json.load(f)
    data = bytes.fromhex(p["data"]) if "data" in p else b"\x42" * 8192
    for a in p.get("actions", []):
        if a.get("action") == "download":
            a.setdefault("sha1", hashlib.sha1(data).hexdigest())
            a.setdefault("version", 2)
    for s in p.get("manifest", {}).get("systems", []):
        for g in s.get("games", []):
            for f in g.get("files", []):
                f.setdefault("size", len(data))
                f.setdefault("sha1", hashlib.sha1(data).hexdigest())
    return p, data


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, body, code=200, kind="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p, data = plan()
        if self.path.startswith("/api/v1/saves/file/") or self.path.startswith("/api/v1/files/"):
            time.sleep(p.get("delay", 0))
            self.reply(data, kind="application/octet-stream")
        elif self.path.startswith("/api/v1/saves/rules"):
            self.reply(json.dumps({"mode": p.get("mode", "saves")}).encode())
        elif self.path.startswith("/api/v1/manifest") and "manifest" in p:
            self.reply(json.dumps(p["manifest"]).encode())
        else:
            self.reply(b"{}")

    def do_POST(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        p, _ = plan()
        if self.path.startswith("/api/v1/saves/plan"):
            self.reply(json.dumps({"actions": p.get("actions", [])}).encode())
        else:
            self.reply(b"{}")

    def do_PUT(self):
        self.rfile.read(int(self.headers.get("Content-Length", 0)))
        p, _ = plan()
        code = p.get("put", 200)
        self.reply(json.dumps({"version": 3} if code == 200 else {"detail": "refused"}).encode(), code)


http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
