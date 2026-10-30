"""A tiny client for Firefox's built-in remote control (Marionette), so the page can be tested in a real browser
with nothing installed but Firefox: no geckodriver, no Selenium.

    with Firefox() as ff:
        ff.go("file:///.../index.html")
        ff.js("return document.title")

Firefox is started headless with a throwaway profile; the protocol is length-prefixed JSON over a socket.
"""
import base64
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


class Firefox:
    def __init__(self, binary=None, width=1500, height=980):
        self.binary = binary or shutil.which("firefox")
        self.size = (width, height)
        self.proc = None

    def __enter__(self):
        if not self.binary:
            raise RuntimeError("no firefox")
        self.profile = tempfile.mkdtemp(prefix="banana-studio-ff-")
        self.port = free_port()
        with open(os.path.join(self.profile, "user.js"), "w") as f:
            f.write('user_pref("marionette.port", %d);\n' % self.port)
            f.write('user_pref("browser.shell.checkDefaultBrowser", false);\n')
            f.write('user_pref("datareporting.policy.dataSubmissionEnabled", false);\n')
            f.write('user_pref("browser.download.dir", "%s");\n' % os.path.join(self.profile, "downloads"))
            f.write('user_pref("browser.download.folderList", 2);\n')
            f.write('user_pref("browser.download.useDownloadDir", true);\n')
            f.write('user_pref("browser.download.always_ask_before_handling_new_types", false);\n')
        env = dict(os.environ, MOZ_HEADLESS="1", MOZ_HEADLESS_WIDTH=str(self.size[0]), MOZ_HEADLESS_HEIGHT=str(self.size[1]))
        self.proc = subprocess.Popen([self.binary, "--headless", "--marionette", "--no-remote", "--profile", self.profile, "about:blank"],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        t0 = time.time()
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", self.port), timeout=60)
                break
            except OSError:
                if time.time() - t0 > 60 or self.proc.poll() is not None:
                    raise RuntimeError("firefox didn't start its remote control")
                time.sleep(0.2)
        self.buf, self.id = b"", 0
        self._read()
        self.cmd("WebDriver:NewSession", {"capabilities": {"alwaysMatch": {"unhandledPromptBehavior": "accept"}}})
        self.cmd("WebDriver:SetTimeouts", {"script": 60000, "pageLoad": 60000})
        try:
            self.cmd("WebDriver:SetWindowRect", {"width": self.size[0], "height": self.size[1]})
        except RuntimeError:
            pass
        return self

    def __exit__(self, *a):
        try:
            self.cmd("WebDriver:DeleteSession")
        except Exception:					# noqa: BLE001
            pass
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        shutil.rmtree(self.profile, ignore_errors=True)

    def _read(self):
        while b":" not in self.buf:
            self.buf += self.sock.recv(65536)
        n, rest = self.buf.split(b":", 1)
        n = int(n)
        while len(rest) < n:
            rest += self.sock.recv(1 << 20)
        self.buf = rest[n:]
        return json.loads(rest[:n])

    def cmd(self, name, params=None):
        self.id += 1
        m = json.dumps([0, self.id, name, params or {}]).encode()
        self.sock.sendall(str(len(m)).encode() + b":" + m)
        while True:
            r = self._read()
            if r[0] == 1 and r[1] == self.id:
                if r[2]:
                    raise RuntimeError("%s: %s" % (r[2].get("error"), r[2].get("message")))
                return r[3]

    def go(self, url):
        self.cmd("WebDriver:Navigate", {"url": url})

    def js(self, script, *args):
        return self.cmd("WebDriver:ExecuteScript", {"script": script, "args": list(args)}).get("value")

    def js_async(self, script, *args):
        """script gets its arguments and a resolve callback last: arguments[arguments.length - 1](value)"""
        return self.cmd("WebDriver:ExecuteAsyncScript", {"script": script, "args": list(args)}).get("value")

    def wait(self, script, timeout=30, *args):
        t0 = time.time()
        while time.time() - t0 < timeout:
            v = self.js(script, *args)
            if v:
                return v
            time.sleep(0.1)
        return None

    def screenshot(self, path):
        data = self.cmd("WebDriver:TakeScreenshot", {"full": False}).get("value")
        with open(path, "wb") as f:
            f.write(base64.b64decode(data))
