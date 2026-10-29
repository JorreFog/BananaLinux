#!/bin/sh
# banana-bshare against a made-up LocalSend app (Python): seen and registered, files received (Quick save, Ask
# accepted and declined, off, a wrong token, unsafe names, a chunked upload), games into their system's folder and the
# rest into banana/received; then files sent to the made-up app (accepted, declined). No multicast here: the made-up
# app registers over HTTP, as LocalSend's apps answer an announcement.
cd "$(dirname "$0")/.." || exit 1
B=$PWD/banana-bshare
[ -x "$B" ] || { echo "bshare-test: no banana-bshare (make banana-bshare)"; exit 1; }
T=$(mktemp -d)
trap 'kill $SP $FP 2>/dev/null; rm -rf "$T"' EXIT
mkdir -p "$T/run" "$T/data/banana" "$T/data/roms"
cp ../system/rootfs/etc/banana/systems.conf "$T/data/banana/systems.conf"
echo "bshare_receive = auto" > "$T/data/banana/settings.conf"
export BSHARE_RUN=$T/run BSHARE_DATA=$T/data BSHARE_PORT=53391
"$B" serve 2>"$T/serve.log" & SP=$!
for i in $(seq 50); do [ -f "$T/run/bshare.pid" ] && break; sleep 0.1; done

python3 -I - "$T" <<'EOF'
import json, os, socket, sys, threading, time, urllib.request, urllib.error
T = sys.argv[1]; base = "http://127.0.0.1:53391/api/localsend/v2/"
passed = failed = 0
def check(ok, what):
    global passed, failed
    if ok: passed += 1; print("  ok:", what)
    else: failed += 1; print("  FAIL:", what)
def post(path, body=None, raw=None):
    data = raw if raw is not None else (json.dumps(body).encode() if body is not None else b"")
    req = urllib.request.Request(base + path, data=data, method="POST", headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=90) as r: return r.status, r.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
info = {"alias": "Test Phone", "version": "2.0", "deviceModel": "Pixel", "deviceType": "mobile", "fingerprint": "phone-fp-1",
        "port": 53392, "protocol": "http", "download": False}
def setting(v):
    open(T + "/data/banana/settings.conf", "w").write("bshare_receive = %s\n" % v)
def files(*named):
    return {"files": {n[0]: {"id": n[0], "fileName": n[1], "size": len(n[2]), "fileType": "application/octet-stream"} for n in named}, "info": info}

print("bshare 1. seen: register and info")
s, b = post("register", info)
check(s == 200 and json.loads(b)["alias"] == "RG DS Plus" and json.loads(b)["fingerprint"], "register answers with the handheld's info (%s)" % b[:60])
peers = open(T + "/run/bshare-peers").read()
check("phone-fp-1\t127.0.0.1\t53392\thttp\tTest Phone\tPixel\tmobile" in peers, "the app in bshare-peers")
with urllib.request.urlopen(base + "info", timeout=5) as r: check(json.loads(r.read())["protocol"] == "http", "info: plain HTTP announced")

print("bshare 2. Quick save: a game into its system's folder, a photo into banana/received")
game, photo = os.urandom(70000), os.urandom(1234)
s, b = post("prepare-upload", files(("f1", "Peel Quest.gba", game), ("f2", "beach.jpg", photo)))
check(s == 200, "prepare-upload accepted at once (%d)" % s)
j = json.loads(b) if s == 200 else {"sessionId": "", "files": {}}
s1, _ = post("upload?sessionId=%s&fileId=f1&token=wrong" % j["sessionId"], raw=game)
check(s1 == 403, "a wrong token: 403")
for fid, data in (("f1", game), ("f2", photo)):
    s2, _ = post("upload?sessionId=%s&fileId=%s&token=%s" % (j["sessionId"], fid, j["files"].get(fid, "")), raw=data)
    check(s2 == 200, "upload %s: 200" % fid)
check(open(T + "/data/roms/gba/Peel Quest.gba", "rb").read() == game, "the game in roms/gba, whole")
check(open(T + "/data/banana/received/beach.jpg", "rb").read() == photo, "the photo in banana/received")
st = open(T + "/run/bshare-receive").read()
check(st.startswith("done\tTest Phone\t2\t2\t") and st.strip().endswith("both"), "bshare-receive: done, both places (%s)" % st.strip())
# (the helper writes the end of the transfer, then its log line: give it a moment)
for _ in range(20):
    log = open(T + "/data/banana/logs/bshare.log").read()
    if "received 2 of 2 files" in log:
        break
    time.sleep(0.1)
check("received 2 of 2 files" in log and "Test Phone" in log, "logged")

print("bshare 3. names: no way out of the folder, folders kept, nothing overwritten, chunked")
evil = b"x" * 10
s, b = post("prepare-upload", files(("e1", "../../etc/passwd", evil), ("e2", "Saves/./Pokemon:Emerald.sav", evil), ("e3", "beach.jpg", evil)))
j = json.loads(b)
for fid in ("e1", "e2"):
    post("upload?sessionId=%s&fileId=%s&token=%s" % (j["sessionId"], fid, j["files"][fid]), raw=evil)
# e3 chunked, by hand
sk = socket.create_connection(("127.0.0.1", 53391))
sk.sendall(("POST /api/localsend/v2/upload?sessionId=%s&fileId=e3&token=%s HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n"
            % (j["sessionId"], j["files"]["e3"])).encode() + b"4\r\nxxxx\r\n6\r\nxxxxxx\r\n0\r\n\r\n")
reply = sk.recv(1000).decode(); sk.close()
check(reply.startswith("HTTP/1.1 200"), "a chunked upload: 200")
check(os.path.isfile(T + "/data/banana/received/etc/passwd") and not os.path.exists(T + "/etc/passwd"), "../../etc/passwd stays in banana/received")
check(os.path.isfile(T + "/data/banana/received/Saves/Pokemon_Emerald.sav"), "a folder kept, ':' made _")
check(open(T + "/data/banana/received/beach (2).jpg", "rb").read() == evil and open(T + "/data/banana/received/beach.jpg", "rb").read() == photo,
      "the same name again: beach (2).jpg, the first one kept")

print("bshare 4. Ask: the menu accepts, then declines; off refuses")
setting("ask")
res = {}
def ask(): res["r"] = post("prepare-upload", files(("a1", "Banana Kart.nds", b"nds")))
th = threading.Thread(target=ask); th.start()
for i in range(50):
    if os.path.exists(T + "/run/bshare-request"): break
    time.sleep(0.1)
req = dict(l.split("\t", 1) for l in open(T + "/run/bshare-request").read().splitlines())
check(req["from"] == "Test Phone" and req["files"] == "1" and req["first"] == "Banana Kart.nds", "bshare-request for the menu (%s)" % req)
s3, _ = post("prepare-upload", files(("a2", "x.gba", b"x")))
check(s3 == 409, "a second request meanwhile: 409")
open(T + "/run/bshare-answer", "w").write("accept " + req["id"] + "\n")
th.join()
check(res["r"][0] == 200, "accepted: 200")
j = json.loads(res["r"][1])
post("upload?sessionId=%s&fileId=a1&token=%s" % (j["sessionId"], j["files"]["a1"]), raw=b"nds")
check(os.path.isfile(T + "/data/roms/nds/Banana Kart.nds"), "the DS game in roms/nds")
# Choose where: a folder of the user's own, everything into it
os.makedirs(T + "/data/mine", exist_ok=True)
th = threading.Thread(target=ask); th.start()
for i in range(50):
    if os.path.exists(T + "/run/bshare-request"): break
    time.sleep(0.1)
req = dict(l.split("\t", 1) for l in open(T + "/run/bshare-request").read().splitlines())
open(T + "/run/bshare-answer", "w").write("accept %s %s/data/mine\n" % (req["id"], T))
th.join()
j = json.loads(res["r"][1])
post("upload?sessionId=%s&fileId=a1&token=%s" % (j["sessionId"], j["files"]["a1"]), raw=b"nds")
check(os.path.isfile(T + "/data/mine/Banana Kart.nds"), "Choose where: the file in the folder chosen, not roms/nds")
check(open(T + "/run/bshare-receive").read().strip().endswith(T + "/data/mine"), "bshare-receive says where")
# a folder off the cards is refused: the default places instead
th = threading.Thread(target=ask); th.start()
for i in range(50):
    if os.path.exists(T + "/run/bshare-request"): break
    time.sleep(0.1)
req = dict(l.split("\t", 1) for l in open(T + "/run/bshare-request").read().splitlines())
open(T + "/run/bshare-answer", "w").write("accept %s /etc\n" % req["id"])
th.join()
j = json.loads(res["r"][1])
post("upload?sessionId=%s&fileId=a1&token=%s" % (j["sessionId"], j["files"]["a1"]), raw=b"nds")
check(not os.path.exists("/etc/Banana Kart.nds") and os.path.isfile(T + "/data/roms/nds/Banana Kart (2).nds"), "a folder off the cards (/etc): ignored, roms/nds instead")
th = threading.Thread(target=ask); th.start()
for i in range(50):
    if os.path.exists(T + "/run/bshare-request"): break
    time.sleep(0.1)
req = dict(l.split("\t", 1) for l in open(T + "/run/bshare-request").read().splitlines())
open(T + "/run/bshare-answer", "w").write("decline " + req["id"] + "\n")
th.join()
check(res["r"][0] == 403, "declined: 403")
check(not os.path.exists(T + "/run/bshare-request"), "the request gone after the answer")
print("bshare 4b. Require PIN (Options): no PIN 401 'PIN required', a wrong one 401 'Invalid PIN', the right one in")
open(T + "/data/banana/settings.conf", "w").write("bshare_receive = auto\nbshare_require_pin = on\nbshare_pin = 4321\n")
s, b = post("prepare-upload", files(("p1", "pin.gba", b"pin")))
check(s == 401 and "PIN required" in b, "no PIN: 401 PIN required (%d %s)" % (s, b))
s, b = post("prepare-upload?pin=1111", files(("p1", "pin.gba", b"pin")))
check(s == 401 and "Invalid PIN" in b, "a wrong PIN: 401 Invalid PIN (%d %s)" % (s, b))
s, b = post("prepare-upload?pin=4321", files(("p1", "pin.gba", b"pin")))
check(s == 200, "the right PIN: 200 (%d)" % s)
if s == 200:
    j = json.loads(b)
    post("upload?sessionId=%s&fileId=p1&token=%s" % (j["sessionId"], j["files"]["p1"]), raw=b"pin")
setting("off")
s4, _ = post("prepare-upload", files(("o1", "x.gba", b"x")))
check(s4 == 403, "Receive off: 403")
open(T + "/run/bshare-receiving", "w").close()     # Bshare's Receive page open: asked, Options' off or not
th = threading.Thread(target=ask); th.start()
for i in range(50):
    if os.path.exists(T + "/run/bshare-request"): break
    time.sleep(0.1)
check(os.path.exists(T + "/run/bshare-request"), "off, but the Receive page open: the menu asked")
req = dict(l.split("\t", 1) for l in open(T + "/run/bshare-request").read().splitlines())
open(T + "/run/bshare-answer", "w").write("decline " + req["id"] + "\n")
th.join()
os.remove(T + "/run/bshare-receiving")
os.makedirs(T + "/data/bananads", exist_ok=True)
open(T + "/data/bananads/bananads.conf", "w").write("firmware.nickname = Swou\n")
with urllib.request.urlopen(base + "info", timeout=5) as r: check(json.loads(r.read())["alias"] == "Swou", "the DS's name shown, at once")
print("bshare (receiving): %d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
EOF
R1=$?

# sending: a made-up LocalSend app that takes what comes (or declines), registered as a peer
mkdir -p "$T/out" "$T/send/Saves"
head -c 50000 /dev/urandom > "$T/send/shot.png"; echo save > "$T/send/Saves/a.sav"; echo save2 > "$T/send/Saves/b.sav"
python3 -I - "$T" <<'EOF' & FP=$!
import http.server, json, sys, os
T = sys.argv[1]; state = {"files": {}}
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def reply(self, code, body=b""):
        self.send_response(code); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0); data = self.rfile.read(n)
        if self.path.startswith("/api/localsend/v2/prepare-upload"):
            j = json.loads(data)
            if os.path.exists(T + "/decline"): return self.reply(403)
            if os.path.exists(T + "/pin"):   # (LocalSend's Require PIN: ?pin=..., else 401)
                want = open(T + "/pin").read().strip()
                q = dict(p.split("=") for p in self.path.split("?")[1].split("&")) if "?" in self.path else {}
                if q.get("pin") != want: return self.reply(401, json.dumps({"message": "PIN required" if "pin" not in q else "Invalid PIN"}).encode())
            state["files"] = {k: v["fileName"] for k, v in j["files"].items()}
            open(T + "/out/info.json", "w").write(json.dumps(j["info"]))
            return self.reply(200, json.dumps({"sessionId": "S1", "files": {k: "tok-" + k for k in j["files"]}}).encode())
        if self.path.startswith("/api/localsend/v2/upload"):
            q = dict(p.split("=") for p in self.path.split("?")[1].split("&"))
            if q.get("token") != "tok-" + q.get("fileId", ""): return self.reply(403)
            name = state["files"][q["fileId"]]
            os.makedirs(os.path.dirname(T + "/out/" + name), exist_ok=True)
            open(T + "/out/" + name, "wb").write(data)
            return self.reply(200)
        self.reply(404)
http.server.HTTPServer(("127.0.0.1", 53393), H).serve_forever()
EOF
sleep 0.5
printf 'mac-fp\t127.0.0.1\t53393\thttp\tMy Mac\tMacBook\tdesktop\t%s\n' "$(date +%s)" > "$T/run/bshare-peers.test"
cp "$T/run/bshare-peers.test" "$T/run/bshare-peers"
p=0 f=0
ok() { p=$((p + 1)); echo "  ok: $1"; }
bad() { f=$((f + 1)); echo "  FAIL: $1"; }
echo "bshare 5. sending: a file and a folder to the made-up app; declined"
"$B" send mac-fp "$T/send/shot.png" "$T/send/Saves" 2>>"$T/send.log" && ok "send: exit 0" || bad "send: exit $? ($(cat "$T/send.log"))"
cmp -s "$T/send/shot.png" "$T/out/shot.png" && ok "the screenshot arrived whole" || bad "shot.png"
[ "$(cat "$T/out/Saves/a.sav" "$T/out/Saves/b.sav" 2>/dev/null)" = "save
save2" ] && ok "the folder's files, in their folder" || bad "Saves/: $(ls -R "$T/out")"
grep -q '"alias": "Swou"' "$T/out/info.json" && ok "the handheld named itself (its DS name)" || bad "info: $(cat "$T/out/info.json")"
grep -q "^done	My Mac	3	3	" "$T/run/bshare-send" && ok "bshare-send: done, 3 of 3" || bad "bshare-send: $(cat "$T/run/bshare-send")"
echo "bshare 6. a device that asks for a PIN (LocalSend's Require PIN): 401 is \"pin\", a wrong one \"badpin\", the right one sends"
echo 4242 > "$T/pin"
"$B" send mac-fp "$T/send/shot.png" 2>>"$T/send.log" && bad "no PIN but exit 0" || ok "no PIN: exit 1"
grep -q "^pin	My Mac" "$T/run/bshare-send" && ok "bshare-send: pin (the menu asks for it)" || bad "bshare-send: $(cat "$T/run/bshare-send")"
"$B" send --pin 1111 mac-fp "$T/send/shot.png" 2>>"$T/send.log" && bad "a wrong PIN but exit 0" || ok "a wrong PIN: exit 1"
grep -q "^badpin	My Mac" "$T/run/bshare-send" && ok "bshare-send: badpin" || bad "bshare-send: $(cat "$T/run/bshare-send")"
rm -f "$T/out/shot.png"
"$B" send --pin 4242 mac-fp "$T/send/shot.png" 2>>"$T/send.log" && ok "the right PIN: exit 0" || bad "the right PIN: exit $? ($(tail -n1 "$T/send.log"))"
cmp -s "$T/send/shot.png" "$T/out/shot.png" && ok "sent with the PIN" || bad "shot.png not sent with the PIN"
grep -q "^mac-fp	4242$" "$T/data/banana/bshare/pins" && ok "the PIN remembered for that device" || bad "pins: $(cat "$T/data/banana/bshare/pins" 2>&1)"
rm -f "$T/out/shot.png"
"$B" send mac-fp "$T/send/shot.png" 2>>"$T/send.log" && cmp -s "$T/send/shot.png" "$T/out/shot.png" &&
    ok "sent again without typing it (the remembered PIN)" || bad "the remembered PIN wasn't used"
echo 9999 > "$T/pin"
"$B" send mac-fp "$T/send/shot.png" 2>>"$T/send.log" && bad "a changed PIN but exit 0" || ok "the device's PIN changed: exit 1"
grep -q "^pin	My Mac" "$T/run/bshare-send" && ! grep -q "^mac-fp	" "$T/data/banana/bshare/pins" &&
    ok "the old PIN forgotten, asked for again (pin)" || bad "after a changed PIN: $(cat "$T/run/bshare-send"); pins: $(cat "$T/data/banana/bshare/pins")"
rm -f "$T/pin"
[ -s "$T/data/banana/bshare/cert.pem" ] && [ -s "$T/data/banana/bshare/key.pem" ] && ok "its certificate made, kept on the card (LocalSend asks for it)" || bad "no certificate: $(ls "$T/data/banana" 2>&1)"
touch "$T/decline"
"$B" send mac-fp "$T/send/shot.png" 2>>"$T/send.log" && bad "declined but exit 0" || ok "declined: exit 1"
grep -q "^declined	My Mac" "$T/run/bshare-send" && ok "bshare-send: declined" || bad "bshare-send: $(cat "$T/run/bshare-send")"
"$B" send nobody "$T/send/shot.png" 2>/dev/null && bad "an unknown device: exit 0" || ok "an unknown device: exit 1"
kill $SP; wait $SP 2>/dev/null
[ ! -f "$T/run/bshare.pid" ] && ok "serve stopped: its pid file gone" || bad "pid file left"
grep -q " off$" "$T/data/banana/logs/bshare.log" && ok "logged off" || bad "no off in the log"
echo "bshare (sending): $p passed, $f failed"
[ $R1 = 0 ] && [ $f = 0 ]
