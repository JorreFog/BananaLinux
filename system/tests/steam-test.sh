#!/bin/sh
# usr/libexec/banana/steam against stand-ins (no Steam, no network): a downloader that asks what DepotDownloader asks
# (the password, a Steam Guard code, wrong once) and keeps a token the way it does; a web server with Steam's library,
# store search and covers. The login's states as the menu reads them, the library's entries on the shelf, their box
# art, a game copied by hand found by its name, a download and its program, the logout.
HERE=$(cd "$(dirname "$0")/.." && pwd)
S=$HERE/rootfs/usr/libexec/banana/steam
T=$(mktemp -d)
trap 'kill $SRV 2>/dev/null; rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL steam: $*"; fail=1; }
command -v python3 >/dev/null && command -v curl >/dev/null || { echo "steam test skipped (no python3 or curl)"; exit 0; }
# the image's sed and cut are busybox's (bytes, no \xHH): used here too where the machine has busybox
if command -v busybox >/dev/null; then
    mkdir "$T/bb"; for a in sed cut; do ln -s "$(command -v busybox)" "$T/bb/$a"; done
    PATH=$T/bb:$PATH; export PATH
fi
# the downloader's stand-in: its token store written as DepotDownloader writes it (a raw-deflated protocol-buffers
# message, the accounts' tokens in field 4), its QR code drawn as it draws one for a terminal
cat > "$T/store.py" <<'PY'
import sys, zlib, base64, json
def vi(n):
    o = b""
    while True:
        b = n & 0x7f; n >>= 7
        if n: o += bytes([b | 0x80])
        else: return o + bytes([b])
def ld(f, p): return vi((f << 3) | 2) + vi(len(p)) + p
msg = ld(2, ld(1, b"cm1.example") + vi((2 << 3) | 0) + vi(3))   # (a field before it: the servers' penalties)
for a in sys.argv[2:]:
    name, sid = a.split("=")
    pay = base64.urlsafe_b64encode(json.dumps({"sub": sid}).encode()).decode().rstrip("=")
    tok = "eyAidHlwIjogIkpXVCIgfQ." + pay + "." + ("sig" + name) * 4
    msg += ld(4, ld(1, name.encode()) + ld(2, tok.encode()))
c = zlib.compressobj(9, zlib.DEFLATED, -15)
open(sys.argv[1], "wb").write(c.compress(msg) + c.flush())
PY
cat > "$T/qr.py" <<'PY'
import sys
n, seed = 29, int(sys.argv[1])
for y in range(n):
    print("".join("\u2588\u2588" if 4 <= x < n - 4 and 4 <= y < n - 4 and (x * 7 + y * 3 + seed) % 3 == 0 else "  " for x in range(n)))
PY
cat > "$T/dd" <<DD
#!/bin/sh
T="$T"
DD
cat >> "$T/dd" <<'DD'
app=; dir=; qr=
for a in "$@"; do [ "$a" = -qr ] && qr=1; done
while [ $# -gt 0 ]; do case $1 in -app) app=$2; shift ;; -dir) dir=$2; shift ;; esac; shift; done
store=$HOME/.local/share/IsolatedStorage/x/y/account.config
if [ -n "$qr" ]; then
    echo "Logging in with QR code..."
    echo "Use the Steam Mobile App to sign in with this QR code:"; python3 "$T/qr.py" 1
    while [ ! -e "$T/qr-change" ] && [ ! -e "$T/qr-scan" ]; do sleep 0.1; done
    if [ -e "$T/qr-change" ]; then
        echo; echo "The QR code has changed:"; echo "Use the Steam Mobile App to sign in with this QR code:"; python3 "$T/qr.py" 2
        while [ ! -e "$T/qr-scan" ]; do sleep 0.1; done
    fi
    mkdir -p "${store%/*}"; python3 "$T/store.py" "$store" u=76561190000000001 Scanner=76561190000000002
    echo " Done!"; echo "Got 5 licenses for account!"
    sleep 30
elif [ "$app" = 480 ]; then
    printf 'Enter account password for "u": '
    IFS= read -r pass
    echo "Logging 'u' into Steam3..."
    [ "$pass" = right ] || { echo "Failed to authenticate with Steam: InvalidPassword" >&2; exit 1; }
    while :; do
        printf 'STEAM GUARD! Please enter your 2-factor auth code from your authenticator app: ' >&2
        IFS= read -r code || exit 1
        [ "$code" = 12345 ] && break
        echo "The previous 2-factor auth code you have provided is incorrect." >&2
    done
    mkdir -p "${store%/*}"; python3 "$T/store.py" "$store" u=76561190000000001
    echo " Done!"; echo "Got 3 licenses for account!"
    sleep 30
else
    # (as DepotDownloader: every file made at its full size first, then filled)
    mkdir -p "$dir/bin"
    echo "Downloading depot 621"
    echo "Pre-allocating $dir/bin/game.exe"
    head -c 4000 /dev/zero > "$dir/bin/game.exe"; head -c 9000 /dev/zero > "$dir/unins000.exe"
    echo " 42.17% $dir/unins000.exe"
    if [ -e "$T/dl-fail" ]; then echo "Failed to find any server with chunk 0a for depot 621. Aborting."; exit 1; fi
    if [ -e "$T/dl-hang" ]; then : > "$T/dl-hung"; sleep 30; exit 1; fi
    echo "100.00% $dir/bin/game.exe"
    echo "Total downloaded: 13000 bytes (13000 bytes uncompressed) from 1 depots"
fi
DD
chmod +x "$T/dd"
# Steam's sites' stand-in
cat > "$T/srv.py" <<'PY'
import sys, json, http.server, urllib.parse
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ctype="application/json", code=200):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); q = urllib.parse.parse_qs(self.rfile.read(n).decode())
        ok = q.get("steamid") == ["76561190000000001"] and q.get("refresh_token", [""])[0].startswith("ey")
        self.send(json.dumps({"response": {"access_token": "ACCESS"} if ok else {}}).encode())
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)
        if u.path.startswith("/IPlayerService/GetOwnedGames"):
            games = [{"appid": 620, "name": "Portal 2"}, {"appid": 400, "name": "Portal"}, {"appid": 9, "name": "What: A \"Game\"?™"},
                     {"appid": 10, "name": "Pokémon™ It’s"}, {"appid": 11, "name": "長い名前" * 40 + "."}] if q.get("access_token") == ["ACCESS"] else []
            self.send(json.dumps({"response": {"game_count": len(games), "games": games}}).encode())
        elif u.path == "/api/storesearch/":
            hit = q.get("term") == ["Half-Life 2"]
            self.send(json.dumps({"total": 1 if hit else 0, "items": [{"type": "app", "name": "Half-Life 2", "id": 220}] if hit else []}, separators=(",", ":")).encode())
        elif u.path in ("/cdn/620/library_600x900.jpg", "/cdn/220/library_600x900.jpg", "/cdn/400/header.jpg"):
            self.send(b"\xff\xd8JPEG" + u.path.encode(), "image/jpeg")
        else: self.send(b"", code=404)
s = http.server.HTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(s.server_port)); s.serve_forever()
PY
python3 "$T/srv.py" "$T/port" & SRV=$!
for i in 1 2 3 4 5 6 7 8 9 10; do [ -s "$T/port" ] && break; sleep 0.2; done
B=http://127.0.0.1:$(cat "$T/port")
export BANANA_PC="$T/pc" BANANA_PC_ROMS="$T/roms" BANANA_RUN="$T/run" BANANA_DATA="$T/data" BANANA_STEAM_DD="$T/dd" \
    BANANA_STEAM_API="$B" BANANA_STEAM_STORE="$B" BANANA_STEAM_CDN="$B/cdn" BANANA_SGDB="$B/sgdb" \
    BANANA_LIBEXEC="$HERE/rootfs/usr/libexec/banana" BANANA_PC_NOMOUNT=1 BANANA_PC_MNT="$T/mnt"
st() { cat "$T/run/steam.state" 2>/dev/null; }
until_state() { for i in $(seq 1 60); do case "$(st)" in $1) return 0 ;; esac; sleep 0.1; done; return 1; }
sh "$S" status | grep -q '^user=$' || bad "status says a user before any login"
# a wrong password: failed, in plain words, nobody logged in
echo wrong | sh "$S" login u >/dev/null 2>&1 && bad "a wrong password logged in"
[ "$(st)" = "failed wrong account name or password" ] || bad "a wrong password's state: $(st)"
# the right one: asks for the code; a wrong code is said so and asked again; the right code logs in
echo right | sh "$S" login u >/dev/null 2>&1 & LP=$!
until_state "code app" || bad "no 'code app' state: $(st)"
sh "$S" code 00000
until_state "wrong-code" || bad "no 'wrong-code' state: $(st)"
sh "$S" code "12 345"
wait $LP || bad "the login failed: $(st)"
[ "$(st)" = "ok u" ] || bad "the state after the login: $(st)"
sh "$S" status | grep -q '^user=u$' || bad "status doesn't name the account"
grep -q right "$T/data/accounts.conf" "$T/data/logs/steam.log" 2>/dev/null && bad "the password was kept"
[ -e "$T/run/steam.in" ] && bad "the login's pipe was left behind"
# the library: an entry per game, named so the card takes it, Steam's covers (the upright one, else the header)
mkdir -p "$T/roms/Half-Life 2"; head -c 100 /dev/zero > "$T/roms/Half-Life 2/hl2.exe"; echo "exe = Half-Life 2/hl2.exe" > "$T/roms/Half-Life 2.pc"
echo "exe = banana:steam" > "$T/roms/Steam.pc"
out=$(sh "$S" library 2>&1) || bad "library failed: $out"
[ "$(echo "$out" | tail -n 1)" = "games 5" ] || bad "library's last line: $out"
[ "$(cat "$T/roms/Portal 2.pc" 2>/dev/null)" = "steam = 620" ] || bad "no entry for Portal 2"
[ -f "$T/roms/What A Game.pc" ] || bad "a name with signs the card refuses: $(ls "$T/roms")"
# (letters of more than one byte stay whole: the ones that share a byte with a trademark sign, and the last of a long
# name, cut to 200 bytes)
[ "$(cat "$T/roms/Pokémon It’s.pc" 2>/dev/null)" = "steam = 10" ] || bad "a name with an accent and a curly apostrophe: $(ls "$T/roms")"
python3 - "$T/roms" <<'PY' || bad "a long name in letters of three bytes: $(ls "$T/roms")"
import os, sys
d = os.fsencode(sys.argv[1])
n = [f for f in os.listdir(d) if f.endswith(b".pc") and open(os.path.join(d, f), "rb").read() == b"steam = 11\n"]
sys.exit(0 if len(n) == 1 and n[0].decode("utf-8") == "長い名前" * 16 + "長い.pc" else 1)
PY
[ -s "$T/roms/media/box/Portal 2.jpg" ] || bad "no cover for Portal 2"
grep -q header "$T/roms/media/box/Portal.jpg" 2>/dev/null || bad "no header picture where there is no upright cover"
grep -q /cdn/220/ "$T/roms/media/box/Half-Life 2.jpg" 2>/dev/null || bad "a game copied by hand got no cover by its name"
[ -e "$T/roms/media/box/What A Game.jpg.none" ] || bad "a game without art isn't marked"
[ -e "$T/roms/media/box/Steam.jpg" ] || [ -e "$T/roms/media/box/Steam.jpg.none" ] && bad "the shelf's own entry was looked up"
# again: nothing twice
sh "$S" library >/dev/null 2>&1; [ "$(ls "$T/roms"/*.pc | wc -l)" = 7 ] || bad "a second run added entries: $(ls "$T/roms")"
# a download that breaks off (the program is on the card already, at its full size and empty): no program written
# into the entry, so picking the game again downloads on
: > "$T/dl-fail"
sh "$S" install "$T/roms/Portal 2.pc" > "$T/inst.out" 2>&1 && bad "a download that broke off said it worked"
[ "$(cat "$T/roms/Portal 2.pc")" = "steam = 620" ] || bad "a download that broke off wrote the entry: $(tr '\n' '|' < "$T/roms/Portal 2.pc")"
[ "$(cat "$T/run/steam.progress")" = "620 42 failed the download did not finish" ] || bad "the progress of a download that broke off: $(cat "$T/run/steam.progress")"
rm -f "$T/dl-fail"
# one stopped: said so (whatever line of the downloader's was still to be read), the entry as it was
: > "$T/dl-hang"
sh "$S" install "$T/roms/Portal 2.pc" > "$T/inst.out" 2>&1 & IP=$!
for i in $(seq 1 60); do [ -e "$T/dl-hung" ] && break; sleep 0.1; done
sh "$S" stop 620; wait $IP && bad "a stopped download said it worked"
[ "$(cat "$T/run/steam.progress")" = "620 0 stopped" ] || bad "the progress of a stopped download: $(cat "$T/run/steam.progress")"
[ "$(cat "$T/roms/Portal 2.pc")" = "steam = 620" ] || bad "a stopped download wrote the entry: $(tr '\n' '|' < "$T/roms/Portal 2.pc")"
[ -e "$T/run/steam.stop.620" ] && bad "stop's flag was left behind"
rm -f "$T/dl-hang"
# a download: the game's folder, its program guessed (not the uninstaller)
sh "$S" install "$T/roms/Portal 2.pc" > "$T/inst.out" 2>&1 || bad "install failed: $(tail -n 2 "$T/inst.out")"
[ "$(sed -n 's/^exe = //p' "$T/roms/Portal 2.pc")" = "Portal 2/bin/game.exe" ] || bad "the program after a download: $(cat "$T/roms/Portal 2.pc")"
grep -q '^steam = 620$' "$T/roms/Portal 2.pc" || bad "the app id was lost from the entry"
[ "$(cat "$T/run/steam.progress")" = "620 100 done" ] || bad "the download's progress at its end: $(cat "$T/run/steam.progress")"
# again with a setting of the owner's in the entry: kept; and a download stopped says so
echo "preset = fast" >> "$T/roms/Portal 2.pc"; sh "$S" install "$T/roms/Portal 2.pc" >/dev/null 2>&1
grep -q '^preset = fast$' "$T/roms/Portal 2.pc" && [ "$(grep -c '^exe = ' "$T/roms/Portal 2.pc")" = 1 ] || bad "a download rewrote the entry's settings: $(tr '\n' '|' < "$T/roms/Portal 2.pc")"
sh "$S" stop 620; [ "$(cat "$T/run/steam.progress")" = "620 0 stopped" ] || bad "stop: $(cat "$T/run/steam.progress")"
# the QR sign-in, on top of the account already there: the code as squares (the downloader's 29 by 29, its light
# border kept), written again when Steam changes it, and the account that scanned it found by its new token
sh "$S" login-qr >/dev/null 2>&1 & QP=$!
until_state "qr 1" || bad "no 'qr 1' state: $(st)"
[ "$(grep -c . "$T/run/steam.qr")" = 29 ] && [ "$(head -n 1 "$T/run/steam.qr")" = 00000000000000000000000000000 ] &&
    ! grep -q '[^01]' "$T/run/steam.qr" && grep -q 1 "$T/run/steam.qr" || bad "the code's squares: $(head -n 6 "$T/run/steam.qr" | tr '\n' '|')"
[ "$(sed -n 5p "$T/run/steam.qr")" = "$(python3 -c 'print("".join("1" if 4 <= x < 25 and (x * 7 + 4 * 3 + 1) % 3 == 0 else "0" for x in range(29)))')" ] || bad "the code's fifth row: $(sed -n 5p "$T/run/steam.qr")"
cp "$T/run/steam.qr" "$T/qr1"; : > "$T/qr-change"
until_state "qr 2" || bad "no 'qr 2' state after Steam changed the code: $(st)"
cmp -s "$T/run/steam.qr" "$T/qr1" && bad "the changed code wasn't written"
: > "$T/qr-scan"
wait $QP || bad "the QR sign-in failed: $(st)"
[ "$(st)" = "ok Scanner" ] || bad "the state after the scan: $(st)"
sh "$S" status | grep -q '^user=Scanner$' || bad "status after the scan: $(sh "$S" status | tr '\n' ' ')"
[ -e "$T/run/steam.qr" ] && bad "the code was left behind"
grep -q "$(printf '\342\226\210')" "$T/data/logs/steam.log" && bad "the code's picture went into the log"
# (the library is asked with that account's token, not the other's: the stand-in's site knows only the first's id)
sh "$S" library 2>&1 | grep -q "Steam gave no list of games: the last one kept" || bad "the library wasn't asked with the scanning account's token"
[ "$(ls "$T/roms"/*.pc | wc -l)" = 7 ] || bad "entries changed after the second account's (empty) library"
# cancelled before a scan
rm -f "$T/qr-change" "$T/qr-scan"
sh "$S" login-qr >/dev/null 2>&1 & QP=$!
until_state "qr 1" || bad "no code the second time: $(st)"
sh "$S" cancel; wait $QP && bad "a cancelled sign-in said it worked"
sh "$S" status | grep -q '^user=Scanner$' || bad "a cancelled sign-in changed the account"
# logout: the token and the account gone, the games stay
sh "$S" logout >/dev/null
sh "$S" status | grep -q '^user=$' || bad "still logged in after logout"
[ -d "$T/pc/steam/home" ] && bad "the token store is still there"
[ -f "$T/roms/Portal 2/bin/game.exe" ] || bad "logout took a game"
sh "$S" install "$T/roms/Portal.pc" >/dev/null 2>&1 && bad "a download without a login"
[ $fail = 0 ] && echo "steam: all checks passed"
exit $fail
