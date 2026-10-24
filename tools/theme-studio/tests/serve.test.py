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


def main():
    fails = []
    port = free_port()
    url = "http://127.0.0.1:%d" % port
    themes_dir = Path(tempfile.mkdtemp(prefix="banana-themes-"))
    srv = subprocess.Popen([sys.executable, str(SERVE), "--port", str(port), "--no-browser",
                            "--themes-dir", str(themes_dir)],
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