#!/usr/bin/env python3
"""Banana Theme Studio: an end-to-end test of the preview server.

Starts serve.py on a free port and checks it serves the editor, the health and templates endpoints, and renders a
real theme through the engine (canvas.json from the card's own themes) — and that a broken expression comes back
as a theme error, not a crash.

    python3 tools/theme-studio/tests/serve.test.py

Needs ui/banana-ui (the server builds it with `make -C ui host` if it can) and ImageMagick for the fixture.
"""
import base64
import json
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

# a 1x1 transparent PNG, to attach as a theme's own picture
PNG_1PX = ("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+M9QDwADhgGAWjR9"
           "awAAAABJRU5ErkJggg==")

HERE = Path(__file__).resolve().parent
SERVE = HERE.parent / "serve.py"
ROOT = HERE.parents[2]
CANVAS = ROOT / "system" / "rootfs" / "usr" / "share" / "banana" / "themes" / "canvas.json"


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def get(url, timeout=30):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.status, r.read()


def post(url, obj, timeout=120):
    data = json.dumps(obj).encode()
    req = urllib.request.Request(url, data=data, headers={"content-type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, json.loads(r.read())


def raw(port, method, path, body=None, headers=None):
    """One request with the headers as given (Host, Origin, Content-Type): the status, or 0 when no answer came."""
    import http.client
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
    try:
        c.putrequest(method, path, skip_host=True)
        h = {"Host": "127.0.0.1:%d" % port}
        h.update(headers or {})
        data = body.encode() if isinstance(body, str) else (body or b"")
        if method == "POST":
            h["Content-Length"] = str(len(data))
        for k, v in h.items():
            c.putheader(k, v)
        c.endheaders()
        if data:
            c.send(data)
        r = c.getresponse()
        r.read()
        return r.status
    except Exception:
        return 0
    finally:
        c.close()


def main():
    fails = []
    port = free_port()
    url = "http://127.0.0.1:%d" % port
    themes_dir = Path(tempfile.mkdtemp(prefix="banana-themes-"))		# the card's banana/themes (send)
    own_dir = Path(tempfile.mkdtemp(prefix="banana-mine-"))		# your own themes folder (--themes)
    srv = subprocess.Popen([sys.executable, str(SERVE), "--port", str(port), "--no-browser",
                            "--themes-dir", str(themes_dir), "--themes", str(own_dir)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(120):
            try:
                get(url + "/api/health", timeout=2)
                break
            except Exception:
                if srv.poll() is not None:
                    err = srv.stderr.read().decode() if srv.stderr else ""
                    print("server exited early:\n" + err)
                    return 1
                time.sleep(0.25)

        st, body = get(url + "/api/health")
        health = json.loads(body)
        print("health:", health.get("ok"), health.get("detail", ""))
        if not health.get("ok"):
            fails.append("health says the engine isn't ready: " + str(health.get("detail")))

        st, body = get(url + "/")
        if st != 200 or b"Theme Studio" not in body:
            fails.append("GET / didn't serve the editor")

        st, body = get(url + "/api/templates")
        tmpl = json.loads(body)
        print("templates:", [t["name"] for t in tmpl])
        if not any(t["name"] == "Canvas" for t in tmpl):
            fails.append("/api/templates didn't include Canvas")

        st, body = get(url + "/api/schema")
        if st != 200 or b"$schema" not in body:
            fails.append("/api/schema didn't serve docs/theme.schema.json")

        st, body = get(url + "/api/systems")
        ids = json.loads(body)
        print("systems:", ids)
        if "nds" not in ids or "gba" not in ids:
            fails.append("/api/systems didn't list the shipped systems")

        # a real render of the card's own example layout theme
        text = CANVAS.read_text()
        st, res = post(url + "/api/render", {"theme": text})
        shots = res.get("shots", [])
        print("render shots:", [s["name"] for s in shots], "errors:", res.get("errors"))
        if len(shots) < 3:
            fails.append("render returned %d shots (expected >=3): %s" % (len(shots), res.get("error")))
        if shots and not shots[0].get("top", "").startswith("data:image/png;base64,"):
            fails.append("a shot has no top PNG")

        # a broken expression must come back as a theme error, not a crash
        st, res = post(url + "/api/render", {"theme": '{"name":"bad","screens":{"home":{"top":[{"type":"rect","fill":"=1 +"}]}}}',
                                             "script": "home 0; wait 300; shot bad"})
        print("bad theme errors:", res.get("errors"))
        if not res.get("errors"):
            fails.append("a broken expression wasn't reported as a theme error")

        # "Send to handheld": the theme and an attached picture land in --themes-dir
        st, res = post(url + "/api/send", {"name": "sent-theme.json", "theme": '{"name":"Sent","screens":{}}',
                                           "assets": [{"path": "sent-theme/pix.png", "data": "data:image/png;base64," + PNG_1PX}]})
        print("send:", res)
        if not (themes_dir / "sent-theme.json").exists():
            fails.append("/api/send didn't write the theme")
        if not (themes_dir / "sent-theme" / "pix.png").exists():
            fails.append("/api/send didn't write the attached picture")
        # a path that escapes its folder is refused
        st, res = post(url + "/api/send", {"name": "x.json", "theme": "{}", "assets": [{"path": "../../evil", "data": PNG_1PX}]})
        if (themes_dir.parent / "evil").exists():
            fails.append("/api/send wrote outside its directory")

        # your own themes folder: empty at first, listed and saved with --themes
        st, body = get(url + "/api/themes")
        if json.loads(body) != []:
            fails.append("/api/themes should be empty before saving anything")
        st, res = post(url + "/api/themes/save", {"name": "mine", "theme": '{"name":"Mine","screens":{}}'})
        print("save local:", res)
        st, body = get(url + "/api/themes")
        names = [t["name"] for t in json.loads(body)]
        print("local themes:", names)
        if "Mine" not in names:
            fails.append("/api/themes/save didn't show up in the list")
        if not (own_dir / "mine.json").exists():
            fails.append("/api/themes/save didn't write the file")

        # only the editor's own page: the server writes files and runs the engine for whoever asks, and a browser asks
        # for any page open in it. Another site's form (no application/json), its fetch (its Origin), a name it points
        # at this machine (its Host) are refused, and nothing is written; the page's own request goes through
        save = json.dumps({"name": "other-site", "theme": '{"name":"X","screens":{}}'})
        J = {"Content-Type": "application/json"}
        for what, hdrs in (("no application/json", {"Content-Type": "text/plain"}),
                           ("another site's Origin", dict(J, Origin="https://example.org")),
                           ("another Host", dict(J, Host="example.org:%d" % port))):
            st = raw(port, "POST", "/api/themes/save", save, hdrs)
            if st != 403 or (own_dir / "other-site.json").exists():
                fails.append("a POST with %s wasn't refused (%d)" % (what, st))
        if raw(port, "GET", "/api/themes", headers={"Host": "example.org:%d" % port}) != 403:
            fails.append("a GET of /api/themes with another Host wasn't refused")
        if raw(port, "GET", "/", headers={"Host": "example.org:%d" % port}) != 200:
            fails.append("the page itself should be served whatever the Host")
        st = raw(port, "POST", "/api/themes/save", save.replace("other-site", "own-page"),
                 dict(J, Origin="http://127.0.0.1:%d" % port))
        if st != 200 or not (own_dir / "own-page.json").exists():
            fails.append("the page's own request (its Origin the server's) was refused (%d)" % st)
        if raw(port, "POST", "/api/themes/save", save.replace("other-site", "localhost"),
               dict(J, Host="localhost:%d" % port, Origin="http://localhost:%d" % port)) != 200:
            fails.append("the page opened as localhost was refused")
        # a body that isn't what the page sends gets an answer (400), not a closed connection
        for what, body in (("a list", "[]"), ("assets that aren't a list", '{"theme":"{}","assets":{"a":1}}'),
                           ("an asset that isn't an object", '{"theme":"{}","name":"odd","assets":[5]}'),
                           ("a name that isn't text", '{"theme":"{}","name":5}')):
            st = raw(port, "POST", "/api/themes/save", body, J)
            if st not in (200, 400) or (st == 200 and "assets\":[5]" not in body):
                fails.append("a body with %s: %d, not 400" % (what, st))
    finally:
        srv.terminate()
        try:
            srv.wait(timeout=5)
        except Exception:
            srv.kill()

    if fails:
        for f in fails:
            print("  FAIL " + f)
        print("serve.test: FAILED")
        return 1
    print("serve.test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())