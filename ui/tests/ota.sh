#!/bin/sh
# #69, updates over the air: the update script's check and download against a stand-in for GitHub (fake-http.py
# serving fake-releases.sh's release list and packages), then Settings > System > Update driven headless. Cases: up
# to date, a newer version, another handheld's release skipped, test versions left out, a damaged download
# rejected, no network (no answer, no DNS, GitHub's hourly limit), the answer cached, not enough space, a download
# cancelled and resumed, the daily check. OTA_PATH: a directory of busybox applets put first on the update script's
# PATH (the image's sh, grep, awk, sed...), else it runs with this machine's tools.
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$HERE/tests/out}/ota
UPD=$HERE/../system/rootfs/usr/libexec/banana/update
SH=sh
HPATH=$PATH     # the update script's PATH
[ -n "$OTA_PATH" ] && { HPATH=$OTA_PATH:$PATH; SH=$OTA_PATH/sh; }
rm -rf "$OUT"; mkdir -p "$OUT/web"
fail=0
bad() { echo "FAIL ota: $*"; fail=1; }

python3 -I "$HERE/tests/fake-http.py" "$OUT/web" "$OUT/port" &
SRV=$!
cleanup() {
    kill $SRV 2>/dev/null
    for p in "$OUT"/*/run/update-download.pid; do [ -f "$p" ] && kill "$(cat "$p")" 2>/dev/null; done
}
trap cleanup EXIT
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do [ -s "$OUT/port" ] && break; sleep 0.25; done
[ -s "$OUT/port" ] || { echo "FAIL ota: the test server didn't start"; exit 1; }
WEB=http://127.0.0.1:$(cat "$OUT/port")
"$HERE/tests/fake-releases.sh" "$OUT/web/good" "" "$WEB/good/"
"$HERE/tests/fake-releases.sh" "$OUT/web/bad" bad "$WEB/bad/"
"$HERE/tests/fake-releases.sh" "$OUT/web/slow" "" "$WEB/slow/slow/"
"$HERE/tests/fake-releases.sh" "$OUT/web/beta" beta "$WEB/auth/beta/"
GOOD=$WEB/good/feed.json

# u <case> <command>...: the update script on a card of its own (<case>/data, <case>/run), as the board $B running $V
u() {
    c=$1; shift
    mkdir -p "$OUT/$c/data" "$OUT/$c/run"
    env PATH="$HPATH" BANANA_DATA="$OUT/$c/data" BANANA_RUN="$OUT/$c/run" BANANA_BOARD=${B:-rgds} BANANA_VERSION=${V:-0.4.2} \
        BANANA_UPDATE_FEED=${FEED:-$GOOD} BANANA_UPDATE_BETA_FEED=${BFEED:-$WEB/auth/beta/feed.json} BANANA_UPDATE_REUSE=${REUSE:-0} $SH "$UPD" "$@" > "$OUT/$c.out" 2> "$OUT/$c.err"
}
has() { grep -qxF -- "$2" "$OUT/$1.out" || bad "$1: no '$2' in: $(tr '\n' ' ' < "$OUT/$1.out") $(cat "$OUT/$1.err")"; }
st() { cat "$OUT/$1/run/update-download" 2>/dev/null; }

# up to date: the RG DS on 0.4.3 (0.5.0 is the Plus's only, the draft 9.9.9 never counts)
B=rgds V=0.4.3 u current check || bad "current: exit $?"
has current state=current; has current latest=0.4.3; has current skipped_other_board=1
# a newer one: the RG DS on 0.4.2 is offered 0.4.3, its own package, and the notes' first paragraph as text
B=rgds V=0.4.2 u newer check || bad "newer: exit $?"
has newer state=newer; has newer latest=0.4.3; has newer file=banana-rgds-0.4.3.update.tar; has newer prerelease=1
has newer 'notes=BananaOS 0.4.3 (alpha) for the RG DS and RG DS Plus: "updates over Wi-Fi" (#69), a faster "tag_name": "v99.0.0" menu.'
# the other handheld: the Plus gets 0.5.0, which the RG DS skipped
B=rgds-plus u plus check; has plus state=newer; has plus latest=0.5.0; has plus file=banana-rgds-plus-0.5.0.update.tar
# test versions left out: only 0.4.2 counts
mkdir -p "$OUT/stable/data"; echo "update_prerelease = off" > "$OUT/stable/data/settings.conf"
V=0.4.1 u stable check; has stable state=newer; has stable latest=0.4.2; has stable prerelease=0; has stable skipped_prerelease=2

# the download: into .partial, checked, then in banana/updates where Update lists it
B=rgds V=0.4.2 u newer download || bad "download: exit $? $(cat "$OUT/newer.err")"
P=$OUT/newer/data/updates/banana-rgds-0.4.3.update.tar
[ "$(st newer)" = "ready $P" ] || bad "download: status '$(st newer)'"
cmp -s "$P" "$OUT/web/good/files/v0.4.3/banana-rgds-0.4.3.update.tar" || bad "download: the package isn't the release's"
[ -z "$(ls -A "$OUT/newer/data/updates/.partial")" ] || bad "download: .partial not empty"
[ -e "$OUT/newer/run/update-download.pid" ] && bad "download: its pid file stayed"
u newer list; has newer "$(printf '%s\t0.4.3\trgds' "$P")"
grep -q 'downloaded banana-rgds-0.4.3.update.tar' "$OUT/newer/data/logs/updates.log" || bad "download: no line in updates.log"

# a damaged download: its SHA-256 isn't the release's: deleted, nothing in banana/updates
FEED=$WEB/bad/feed.json u damaged check; has damaged state=newer
FEED=$WEB/bad/feed.json u damaged download && bad "damaged: exit 0"
case $(st damaged) in "failed: the download is damaged"*) ;; *) bad "damaged: status '$(st damaged)'" ;; esac
ls "$OUT/damaged/data/updates/"*.tar >/dev/null 2>&1 && bad "damaged: a package was kept"
[ -z "$(ls -A "$OUT/damaged/data/updates/.partial")" ] || bad "damaged: .partial kept the damaged file"

# no network: nothing answers on the port; what the last check found is kept (last_state), and it says why
u offline check; has offline state=newer
FEED=http://127.0.0.1:9/feed.json u offline check && bad "offline: exit 0"
has offline state=error; has offline last_state=newer; has offline latest=0.4.3
grep -q "^error=couldn't connect to GitHub: is Wi-Fi connected?" "$OUT/offline.out" || bad "offline: $(grep error= "$OUT/offline.out")"
# no DNS (a name that never resolves)
FEED=https://nothing.invalid/releases u nodns check && bad "nodns: exit 0"
grep -q "^error=GitHub's address couldn't be looked up" "$OUT/nodns.out" || bad "nodns: $(grep error= "$OUT/nodns.out")"
# GitHub's hourly limit reached
FEED=$WEB/limited u limited check && bad "limited: exit 0"
grep -q "^error=GitHub's limit of 60 checks an hour from this network was reached: try again after [0-9][0-9]:[0-9][0-9]" "$OUT/limited.out" || bad "limited: $(grep error= "$OUT/limited.out")"
# the list fetched a moment ago is used again (the hourly limit): no request, even with the network gone
u cached check
REUSE=300 FEED=http://127.0.0.1:9/feed.json u cached check || bad "cached: exit $?"
has cached state=newer

# not enough space for the download and its unpacked copy: nothing fetched
u space check
BANANA_UPDATE_FREE_KB=1000 u space download && bad "space: exit 0"
case $(st space) in "failed: not enough space on the card"*) ;; *) bad "space: status '$(st space)'" ;; esac
[ -z "$(ls -A "$OUT/space/data/updates/.partial" 2>/dev/null)" ] || bad "space: something was downloaded"

# cancelled while it downloads (slowly): stopped, what came kept; the next download carries on from there
FEED=$WEB/slow/slow/feed.json u slow check
FEED=$WEB/slow/slow/feed.json u slow download &
dl=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    case $(st slow) in "downloading "[1-9]*) break ;; esac; sleep 0.25
done
pid=$(cat "$OUT/slow/run/update-download.pid" 2>/dev/null)
u slow cancel; has slow cancelled
wait $dl
case $(st slow) in "cancelled "[1-9]*" 1505280") ;; *) bad "cancel: status '$(st slow)'" ;; esac
[ -n "$pid" ] && kill -0 "$pid" 2>/dev/null && bad "cancel: the download is still running"
got=$(ls -ln "$OUT/slow/data/updates/.partial/banana-rgds-0.4.3.update.tar" 2>/dev/null | awk '{ print $5 }')
[ "${got:-0}" -gt 0 ] && [ "$got" -lt 1505280 ] || bad "cancel: .partial holds ${got:-nothing}"
FEED=$WEB/slow/slow/feed.json u slow download || bad "resume: exit $? $(cat "$OUT/slow.err")"
[ "$(st slow)" = "ready $OUT/slow/data/updates/banana-rgds-0.4.3.update.tar" ] || bad "resume: status '$(st slow)'"
grep -q "^/slow/slow/files/v0.4.3/banana-rgds-0.4.3.update.tar bytes=$got-" "$OUT/port.log" || bad "resume: no request from byte $got: $(grep 'rgds-0.4.3' "$OUT/port.log" | tr '\n' ' ')"

# the daily check (Wi-Fi's hook): nothing while it is off, nothing within a day of the last answer, then asked
u daily check --daily; [ -e "$OUT/daily/data/updates/.check" ] && bad "daily: it checked with update_daily off"
echo "update_daily = on" > "$OUT/daily/data/settings.conf"
u daily check --daily; has daily state=newer
FEED=http://127.0.0.1:9/feed.json u daily check --daily || bad "daily: asked again within the day"
sed -i 's/^checked=.*/checked=1000/' "$OUT/daily/data/updates/.check"
FEED=http://127.0.0.1:9/feed.json u daily check --daily && bad "daily: not asked after a day"

# ---- the beta channel: the BananaOS site's password-protected list (fake-http.py's /auth/) -----------------------
PW=BetaTesting0.5.0
# a wrong password: refused (exit 3), nothing kept; the channel stays GitHub
echo wrong | B=rgds-plus V=0.5.0 u beta beta-login; rc=$?
[ $rc = 3 ] || bad "beta wrong password: exit $rc"; has beta "wrong password"
[ -e "$OUT/beta/data/updates/.beta" ] && bad "beta wrong password: it was kept"
ls "$OUT/beta/run"/update-beta.* >/dev/null 2>&1 && bad "beta wrong password: the temporary login stayed in run"
# the site not answering: not "wrong password"
echo $PW | BFEED=http://127.0.0.1:9/feed.json B=rgds-plus V=0.5.0 u beta beta-login; rc=$?
[ $rc = 1 ] || bad "beta offline login: exit $rc"; grep -q "couldn't connect to the beta server" "$OUT/beta.out" || bad "beta offline login: $(cat "$OUT/beta.out")"
# the right one: kept for curl only (-K: not on a command line), the owner's alone where the file system keeps modes
echo $PW | B=rgds-plus V=0.5.0 u beta beta-login || bad "beta login: exit $? $(cat "$OUT/beta.out" "$OUT/beta.err")"
has beta ok
[ "$(cat "$OUT/beta/data/updates/.beta" 2>/dev/null)" = "user = \"beta:$PW\"" ] || bad "beta login: .beta holds '$(cat "$OUT/beta/data/updates/.beta")'"
[ "$(ls -ln "$OUT/beta/data/updates/.beta" | cut -c1-10)" = "-rw-------" ] || bad "beta login: .beta is $(ls -ln "$OUT/beta/data/updates/.beta" | cut -c1-10)"
grep -q "$PW" "$OUT/port.log" && bad "beta: the password is in the server's log"
# (a quote and a backslash in it: escaped for curl's config, so it is sent and refused, not a broken config)
printf '%s\n' 'a"b\c' | B=rgds-plus V=0.5.0 u betaq beta-login; rc=$?
[ $rc = 3 ] || bad "beta quoted: exit $rc $(cat "$OUT/betaq.out" "$OUT/betaq.err")"
# the Plus on 0.5.0 (the first beta, installed by hand) is offered 0.5.0-beta.2, downloaded with the password
B=rgds-plus V=0.5.0 u beta check || bad "beta check: exit $? $(cat "$OUT/beta.err")"
has beta state=newer; has beta latest=0.5.0-beta.2; has beta channel=beta; has beta file=banana-rgds-plus-0.5.0-beta.2.update.tar
B=rgds-plus V=0.5.0 u beta download || bad "beta download: exit $? $(cat "$OUT/beta.err")"
cmp -s "$OUT/beta/data/updates/banana-rgds-plus-0.5.0-beta.2.update.tar" "$OUT/web/beta/files/v0.5.0-beta.2/banana-rgds-plus-0.5.0-beta.2.update.tar" || bad "beta download: not the site's package"
grep -q "^/auth/beta/files/v0.5.0-beta.2/banana-rgds-plus-0.5.0-beta.2.update.tar " "$OUT/port.log" || bad "beta download: not fetched from the site"
# on 0.5.0-beta.2: up to date; on 0.5.0 with only 0.5.0 on the site: up to date (never 0.5.0 again)
B=rgds-plus V=0.5.0-beta.2 u beta check; has beta state=current; has beta latest=0.5.0-beta.2
BFEED=$WEB/auth/good/feed.json B=rgds-plus V=0.5.0 u beta check; has beta state=current; has beta latest=0.5.0
# beta.2 < 0.5.0 on GitHub's ordering stays as it was: a 0.5.0-beta.2 handheld is offered a 0.5.0 release
cp "$OUT/beta/data/updates/.beta" "$OUT/beta.keep"; rm "$OUT/beta/data/updates/.beta"
B=rgds-plus V=0.5.0-beta.2 u beta check; has beta state=newer; has beta latest=0.5.0; has beta channel=github
# Test versions left out doesn't hide the beta channel's (all test versions)
cp "$OUT/beta.keep" "$OUT/beta/data/updates/.beta"; echo "update_prerelease = off" > "$OUT/beta/data/settings.conf"
B=rgds-plus V=0.5.0 u beta check; has beta state=newer; has beta latest=0.5.0-beta.2; has beta prerelease_setting=off
rm "$OUT/beta/data/settings.conf"
# the password changed on the site since: the check says so
echo 'user = "beta:old"' > "$OUT/beta/data/updates/.beta"
B=rgds-plus V=0.5.0 u beta check && bad "beta refused: exit 0"
grep -q "^error=the beta server refused the password" "$OUT/beta.out" || bad "beta refused: $(grep error= "$OUT/beta.out")"
# off: GitHub again, the password forgotten
B=rgds-plus V=0.5.0 u beta beta-off; has beta off
[ -e "$OUT/beta/data/updates/.beta" ] && bad "beta off: the password stayed"
B=rgds-plus V=0.5.0 u beta check; has beta channel=github; has beta state=current

# ---- the Update page, headless --------------------------------------------------------------------------------
FX=$OUT/fixture
"$HERE/tests/fixture.sh" "$FX" >/dev/null
# the update script as it is, but apply only noted (the host has no slots to write)
cat > "$OUT/update-tool.sh" <<EOT
#!/bin/sh
[ "\$1" = apply ] && { echo "\$*" >> "$OUT/applied.txt"; echo "done 0.4.3" > "\$BANANA_RUN/update-status"; exit 0; }
exec $SH "$UPD" "\$@"
EOT
chmod +x "$OUT/update-tool.sh"
# ui <name> <feed> <script>: banana-ui on the fixture, the RG DS on 0.4.2; Settings > System (five R's) > Update (down 2)
ui() {
    n=$1; f=$2; shift 2
    mkdir -p "$OUT/ui-$n/run"
    env PATH="$HPATH" BANANA_UPDATE="$OUT/update-tool.sh" BANANA_DATA="$FX/data" BANANA_RUN="$OUT/ui-$n/run" BANANA_BOARD=${UB:-rgds} BANANA_VERSION=${UV:-0.4.2} \
        BANANA_UPDATE_FEED="$f" BANANA_UPDATE_BETA_FEED="$WEB/auth/beta/feed.json" BANANA_UPDATE_REUSE=0 BANANA_LANG_MISSING="$OUT/ui-$n/missing.txt" \
        "$HERE/banana-ui" --headless "$OUT/ui-$n-" --size 640x480 --assets "$HERE/assets/rnds" \
        --systems "$HERE/../system/rootfs/etc/banana/systems.conf" --roms "$FX/roms" --data "$FX/data" \
        --script "home 0; press start; press r; press r; press r; press r; press r; press a; press down; press down; press a; $*" \
        2> "$OUT/ui-$n.log" || bad "ui $n: banana-ui exited $?"
}
upstate() { grep -q "upstate: .*$2" "$OUT/ui-$1.log" || bad "ui $1: no '$2' in: $(grep upstate "$OUT/ui-$1.log" | tail -n 3)"; }
clean() {   # (a download a test left running stopped first)
    for p in "$OUT"/ui-*/run/update-download.pid; do [ -f "$p" ] && kill "$(cat "$p")" 2>/dev/null; done
    sleep 1.5; rm -rf "$FX/data/updates"
}
# check, download (a process of its own: the page polls it), install through apply
clean
ui flow "$GOOD" "upstate; shot open; press a; upstate; shot found; press a; sleep 1500; upstate; sleep 2500; upstate; shot ready; press a; upstate; press a; sleep 1000; upstate; shot applied"
upstate flow 'row "Check for updates" tag "" act "Check" state -'
upstate flow 'row "Download Banana 0.4.3" tag "1 MB" act "Download" state newer latest 0.4.3'
upstate flow 'row "Install Banana 0.4.3" tag "Downloaded" act "Install" .* dl "ready .*banana-rgds-0.4.3.update.tar" packages 1'
upstate flow 'row "Install Banana 0.4.3" tag "Press A again" act "Confirm"'
upstate flow 'status "done 0.4.3"'
grep -q "^apply $FX/data/updates/banana-rgds-0.4.3.update.tar$" "$OUT/applied.txt" 2>/dev/null || bad "ui flow: apply wasn't handed the download: $(cat "$OUT/applied.txt" 2>/dev/null)"
# cancelled from the page, then the row offers to carry on
clean
ui cancel "$WEB/slow/slow/feed.json" "press a; press a; sleep 1500; upstate; shot downloading; press a; sleep 1500; upstate; shot cancelled; press b; shot list"
upstate cancel 'row "Downloading Banana 0.4.3" tag "[0-9]*% · [0-9]* of 1 MB" act "Cancel"'
upstate cancel 'row "Download Banana 0.4.3" tag "Resume: [0-9]* of 1 MB" act "Download" .* dl "cancelled [1-9]'
# no network: the check fails and says so
clean
ui offline "http://127.0.0.1:9/feed.json" "press a; upstate; shot offline"
upstate offline 'row "Check for updates" tag "Failed" act "Check" state error'
# a newer version found, in Chinese: every line of the page and
# of the two new rows translated (the release notes and the script's reasons stay as they come)
clean
cp "$FX/data/settings.conf" "$OUT/settings.conf.keep"
echo "language = zh-cn" >> "$FX/data/settings.conf"
ui zh "$GOOD" "shot zh-open; press a; shot zh-found; press b; shot zh-system; press down; shot zh-test; press down; shot zh-daily"
for t in "Check for updates" "Download Banana" "Newest for the" "A downloads it" "A asks GitHub" "Test versions" "Whether Update" "Update check" "When asked" "Included" "Left out" "MB" "available"; do
    grep -q "$t" "$OUT/ui-zh/missing.txt" 2>/dev/null && bad "ui zh: not translated: $(grep "$t" "$OUT/ui-zh/missing.txt" | head -n 2)"
done
mv "$OUT/settings.conf.keep" "$FX/data/settings.conf"
# the beta channel (the page's second row): a wrong password refused, the right one kept, then the check asks the beta
# server (the Plus on 0.5.0 is offered 0.5.0-beta.2), and A twice turns it off
clean
UB=rgds-plus UV=0.5.0 ui beta "$GOOD" "press down; upstate; shot beta-row; press a; shot beta-kbd; type wrong; press start; upstate; shot beta-wrong; press a; type $PW; press start; upstate; shot beta-on; press up; press a; upstate; shot beta-found; press down; press a; upstate; press a; upstate; shot beta-off"
upstate beta 'row "Check for updates" .* beta off'
upstate beta 'message "Wrong password: bananaos-cfw.dev refused it. The beta channel stays off." beta off'
upstate beta 'message "Beta channel on: .*" beta on'
upstate beta 'row "Download Banana 0.5.0-beta.2" tag "[0-9]* MB" act "Download" state newer latest 0.5.0-beta.2 .* beta on'
upstate beta 'message "Beta channel off: Check for updates asks GitHub again." beta off'
grep -q "$PW" "$OUT/ui-beta.log" && bad "ui beta: the password is in the menu's log"
ls "$OUT/ui-beta/run/"update-beta* >/dev/null 2>&1 && bad "ui beta: the password file stayed in run"
# deleting packages (owner's request 2026-10-27): on 0.4.2, Y Y deletes the first package, X X every one not newer than
# 0.4.2 for this handheld; 0.4.3 (newer) and the RG DS Plus's package (another handheld) stay
clean; mkdir -p "$FX/data/updates"
mkpkg() { d=$(mktemp -d); mkdir "$d/banana-$1-$2"; echo "$2" > "$d/banana-$1-$2/VERSION"; echo "$1" > "$d/banana-$1-$2/board"
    (cd "$d" && tar -cf "$FX/data/updates/banana-$1-$2.update.tar" "banana-$1-$2"); rm -rf "$d"; }
mkpkg rgds 0.4.0; mkpkg rgds 0.4.1; mkpkg rgds 0.4.2; mkpkg rgds 0.4.3; mkpkg rgds-plus 0.4.1
ui del "$GOOD" "upstate; press down; press down; upstate; shot del-row; press y; upstate; shot del-armed; press y; upstate; shot del-one; press x; upstate; shot del-oldarmed; press x; upstate; shot del-old"
left=$(cd "$FX/data/updates" && ls *.update.tar 2>/dev/null | tr '\n' ' ')
[ "$left" = "banana-rgds-0.4.3.update.tar banana-rgds-plus-0.4.1.update.tar " ] || bad "ui del: left on the card: $left"
grep -q 'package deleted from banana/updates' "$OUT/ui-del.log" || bad "ui del: no delete message: $(grep upstate "$OUT/ui-del.log" | tail -n 4)"
grep -q 'old packages removed from banana/updates' "$OUT/ui-del.log" || bad "ui del: no remove-old message: $(grep upstate "$OUT/ui-del.log" | tail -n 3)"
for s in beta-beta-row beta-beta-kbd beta-beta-on beta-beta-found flow-open flow-found flow-ready flow-applied del-del-armed del-del-oldarmed del-del-old cancel-downloading cancel-cancelled offline-offline zh-zh-open zh-zh-found zh-zh-system; do
    for p in top bot; do
        f=$OUT/ui-$s-$p.png
        [ -s "$f" ] || { bad "no $f"; continue; }
        [ "$(convert "$f" -format '%[fx:standard_deviation>0.01]' info:)" = 1 ] || bad "$f is blank"
    done
done
clean
[ $fail = 0 ] && echo "ota: all checks passed ($OUT)"
exit $fail
