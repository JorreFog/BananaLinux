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
# the downloader's stand-in
cat > "$T/dd" <<'DD'
#!/bin/sh
app=; dir=
while [ $# -gt 0 ]; do case $1 in -app) app=$2; shift ;; -dir) dir=$2; shift ;; esac; shift; done
if [ "$app" = 480 ]; then
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
    # (the token store: raw deflate, the token as plain text in it)
    mkdir -p "$HOME/.local/share/IsolatedStorage/x/y"
    python3 -c 'import zlib,sys,base64,json
p=base64.urlsafe_b64encode(json.dumps({"sub":"76561190000000001"}).encode()).decode().rstrip("=")
t=("eyAidHlwIjogIkpXVCIgfQ."+p+".c2lnbmF0dXJlc2lnbmF0dXJl").encode()
c=zlib.compressobj(9,zlib.DEFLATED,-15); sys.stdout.buffer.write(c.compress(b"\x22\x05u\x12"+t+b"\x00")+c.flush())' > "$HOME/.local/share/IsolatedStorage/x/y/account.config"
    echo " Done!"; echo "Got 3 licenses for account!"
    sleep 30
else
    mkdir -p "$dir/bin"
    echo "Downloading depot 621"
    head -c 4000 /dev/zero > "$dir/bin/game.exe"; head -c 9000 /dev/zero > "$dir/unins000.exe"
    echo "100.00% $dir/bin/game.exe"
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
            games = [{"appid": 620, "name": "Portal 2"}, {"appid": 400, "name": "Portal"}, {"appid": 9, "name": "What: A \"Game\"?™"}] if q.get("access_token") == ["ACCESS"] else []
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
[ "$(echo "$out" | tail -n 1)" = "games 3" ] || bad "library's last line: $out"
[ "$(cat "$T/roms/Portal 2.pc" 2>/dev/null)" = "steam = 620" ] || bad "no entry for Portal 2"
[ -f "$T/roms/What A Game.pc" ] || bad "a name with signs the card refuses: $(ls "$T/roms")"
[ -s "$T/roms/media/box/Portal 2.jpg" ] || bad "no cover for Portal 2"
grep -q header "$T/roms/media/box/Portal.jpg" 2>/dev/null || bad "no header picture where there is no upright cover"
grep -q /cdn/220/ "$T/roms/media/box/Half-Life 2.jpg" 2>/dev/null || bad "a game copied by hand got no cover by its name"
[ -e "$T/roms/media/box/What A Game.jpg.none" ] || bad "a game without art isn't marked"
[ -e "$T/roms/media/box/Steam.jpg" ] || [ -e "$T/roms/media/box/Steam.jpg.none" ] && bad "the shelf's own entry was looked up"
# again: nothing twice
sh "$S" library >/dev/null 2>&1; [ "$(ls "$T/roms"/*.pc | wc -l)" = 5 ] || bad "a second run added entries: $(ls "$T/roms")"
# a download: the game's folder, its program guessed (not the uninstaller)
sh "$S" install "$T/roms/Portal 2.pc" > "$T/inst.out" 2>&1 || bad "install failed: $(tail -n 2 "$T/inst.out")"
[ "$(sed -n 's/^exe = //p' "$T/roms/Portal 2.pc")" = "Portal 2/bin/game.exe" ] || bad "the program after a download: $(cat "$T/roms/Portal 2.pc")"
grep -q '^steam = 620$' "$T/roms/Portal 2.pc" || bad "the app id was lost from the entry"
# logout: the token and the account gone, the games stay
sh "$S" logout >/dev/null
sh "$S" status | grep -q '^user=$' || bad "still logged in after logout"
[ -d "$T/pc/steam/home" ] && bad "the token store is still there"
[ -f "$T/roms/Portal 2/bin/game.exe" ] || bad "logout took a game"
sh "$S" install "$T/roms/Portal.pc" >/dev/null 2>&1 && bad "a download without a login"
[ $fail = 0 ] && echo "steam: all checks passed"
exit $fail
