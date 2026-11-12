#!/bin/sh
# usr/libexec/banana/ra-standalone on made-up settings files: the account from accounts.conf written the way PPSSPP,
# Dolphin, Flycast and ARMSX2 each read it, other settings left alone; the token asked from the site (a stand-in)
# when only the password is known, and kept; the token taken out again after the game; nothing touched without an
# account.
HERE=$(cd "$(dirname "$0")/.." && pwd)
R=$HERE/rootfs/usr/libexec/banana/ra-standalone
T=$(mktemp -d)
trap 'kill $SRV 2>/dev/null; rm -rf "$T"' EXIT
fail=0
bad() { echo "FAIL ra-standalone: $*"; fail=1; }
export BANANA_DATA="$T/data" BANANA_RA_URL="http://127.0.0.1:9/none"
mkdir -p "$T/data" "$T/pp" "$T/dol" "$T/inis"
printf '[Graphics]\nGraphicsBackend = 3 (VULKAN)\n[Achievements]\nAchievementsEnable = False\nAchievementsSoundEffects = True\n[Sound]\nEnableSound = True\n' > "$T/pp/ppsspp.ini"
printf '[audio]\nbackend = alsa\n[config]\nrend.Resolution = 960\n' > "$T/emu.cfg"
printf '[Achievements]\nEnabled = false\nChallengeMode = false\n[Folders]\nBios = /data/bios/ps2\n' > "$T/inis/PCSX2.ini"
# no account: nothing is written
sh "$R" ppsspp "$T/pp"; sh "$R" dolphin "$T/dol"
grep -q "AchievementsEnable = False" "$T/pp/ppsspp.ini" && [ ! -e "$T/pp/ppsspp_retroachievements.dat" ] && [ ! -e "$T/dol/RetroAchievements.ini" ] ||
    bad "settings were written with no account"
# an account with its token
printf 'ss_user=x\nra_user=Banana Fan\nra_password=pw&1\nra_retroarch_token=TOK123\n' > "$T/data/accounts.conf"
[ "$(sh "$R" token)" = TOK123 ] || bad "token: $(sh "$R" token)"
sh "$R" ppsspp "$T/pp" >/dev/null; sh "$R" dolphin "$T/dol" >/dev/null; sh "$R" flycast "$T/emu.cfg" >/dev/null; sh "$R" armsx2 "$T/inis" >/dev/null
grep -q '^AchievementsEnable = True$' "$T/pp/ppsspp.ini" && grep -q '^AchievementsUserName = Banana Fan$' "$T/pp/ppsspp.ini" || bad "ppsspp.ini: $(tr '\n' '|' < "$T/pp/ppsspp.ini")"
[ "$(cat "$T/pp/ppsspp_retroachievements.dat")" = TOK123 ] || bad "PPSSPP's token file"
# (in its own section, the other sections and keys as they were)
[ "$(sed -n '/^\[Achievements\]/,/^\[Sound\]/p' "$T/pp/ppsspp.ini" | grep -c .)" = 5 ] && grep -q '^AchievementsSoundEffects = True$' "$T/pp/ppsspp.ini" &&
    grep -q '^GraphicsBackend = 3 (VULKAN)$' "$T/pp/ppsspp.ini" && grep -q '^EnableSound = True$' "$T/pp/ppsspp.ini" || bad "ppsspp.ini's other settings: $(tr '\n' '|' < "$T/pp/ppsspp.ini")"
[ "$(tr '\n' '|' < "$T/dol/RetroAchievements.ini")" = "[Achievements]|Enabled = True|Username = Banana Fan|ApiToken = TOK123|" ] || bad "Dolphin: $(tr '\n' '|' < "$T/dol/RetroAchievements.ini")"
[ "$(sed -n '/^\[achievements\]/,$p' "$T/emu.cfg" | tr '\n' '|')" = "[achievements]|Enabled = yes|UserName = Banana Fan|Token = TOK123|" ] && grep -q '^rend.Resolution = 960$' "$T/emu.cfg" || bad "Flycast: $(tr '\n' '|' < "$T/emu.cfg")"
[ "$(sed -n '/^\[Achievements\]/,/^\[Folders\]/p' "$T/inis/PCSX2.ini" | tr '\n' '|')" = "[Achievements]|Enabled = true|ChallengeMode = false|Username = Banana Fan|Token = TOK123|[Folders]|" ] || bad "ARMSX2: $(tr '\n' '|' < "$T/inis/PCSX2.ini")"
# twice: nothing doubled
sh "$R" dolphin "$T/dol" >/dev/null; [ "$(grep -c . "$T/dol/RetroAchievements.ini")" = 4 ] || bad "a second run doubled Dolphin's lines"
# after the game: the token gone, the rest kept
printf '[Achievements]\nToken = TOK123\n' > "$T/inis/secrets.ini"
for e in "ppsspp $T/pp" "dolphin $T/dol" "flycast $T/emu.cfg" "armsx2 $T/inis"; do sh "$R" scrub $e; done
grep -rq TOK123 "$T/pp" "$T/dol" "$T/emu.cfg" "$T/inis" && bad "the token is still in: $(grep -rl TOK123 "$T/pp" "$T/dol" "$T/emu.cfg" "$T/inis" | tr '\n' ' ')"
grep -q '^Username = Banana Fan$' "$T/dol/RetroAchievements.ini" && grep -q '^UserName = Banana Fan$' "$T/emu.cfg" || bad "scrub took more than the token"
# only the password: the token asked from the site, with the name and password it was given, and kept
if command -v python3 >/dev/null && command -v curl >/dev/null; then
    cat > "$T/srv.py" <<'PY'
import sys, json, http.server, urllib.parse
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_POST(self):
        q = urllib.parse.parse_qs(self.rfile.read(int(self.headers.get("Content-Length", 0))).decode())
        ok = q.get("r") == ["login2"] and q.get("u") == ["Banana Fan"] and q.get("p") == ["pw&1"]
        b = json.dumps({"Success": True, "User": "Banana Fan", "Token": "FRESH9", "Score": 5} if ok else {"Success": False, "Error": "Invalid User/Password combination. Please try again"}).encode()
        self.send_response(200 if ok else 401); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
s = http.server.HTTPServer(("127.0.0.1", 0), H)
open(sys.argv[1], "w").write(str(s.server_port)); s.serve_forever()
PY
    python3 "$T/srv.py" "$T/port" & SRV=$!
    for i in 1 2 3 4 5 6 7 8 9 10; do [ -s "$T/port" ] && break; sleep 0.2; done
    export BANANA_RA_URL="http://127.0.0.1:$(cat "$T/port")/dorequest.php"
    printf 'ra_user=Banana Fan\nra_password=pw&1\n' > "$T/data/accounts.conf"
    [ "$(sh "$R" token)" = FRESH9 ] || bad "the token from the site: $(sh "$R" token)"
    grep -q '^ra_retroarch_token=FRESH9$' "$T/data/accounts.conf" && grep -q '^ra_password=pw&1$' "$T/data/accounts.conf" || bad "the token wasn't kept: $(tr '\n' '|' < "$T/data/accounts.conf")"
    # a wrong password: no token, the emulator's settings left as they are
    printf 'ra_user=Banana Fan\nra_password=nope\n' > "$T/data/accounts.conf"
    sh "$R" token >/dev/null && bad "a token from a wrong password"
    cp "$T/emu.cfg" "$T/emu.before"; sh "$R" flycast "$T/emu.cfg" >/dev/null; cmp -s "$T/emu.cfg" "$T/emu.before" || bad "settings changed without a token"
else
    echo "ra-standalone: the site's half skipped (no python3 or curl)"
fi
[ $fail = 0 ] && echo "ra-standalone: all checks passed"
exit $fail
