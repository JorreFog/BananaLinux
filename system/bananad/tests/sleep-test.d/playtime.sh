# Parental controls' daily play time (bananad's playtime_step), sourced by sleep-test.sh: counted while a game runs,
# the warnings in /run/banana/playtime-notice, then the game saved and closed (a stand-in game: SIGTERM's turn)
PT_CONF=$(cat /data/banana/settings.conf 2>/dev/null) || true
echo "playtime 1. controls off: nothing counted, nothing said"
rm -f /run/banana/playtime-notice /data/banana/playtime.txt
printf 'parental = off\nparental_minutes = 1\n' > /data/banana/settings.conf
echo 'pin = 00' > /data/banana/parental.conf
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid
start; sleep 2
[ ! -e /run/banana/playtime-notice ] && ok "no notice" || bad "notice: $(cat /run/banana/playtime-notice)"
kill -0 $GAME 2>/dev/null && ok "the game plays on" || bad "the game ended"
stop

echo "playtime 2. on, 1 minute a day, 57 s played today: told 1 minute is left, then up: the game closed, the time kept"
printf 'parental = on\nparental_minutes = 1\n' > /data/banana/settings.conf
echo "$(date +%F) 57" > /data/banana/playtime.txt
start; sleep 1
grep -q '^left 1 [0-9]*$' /run/banana/playtime-notice 2>/dev/null && ok "warned: 1 minute left" || bad "notice: $(cat /run/banana/playtime-notice 2>&1)"
for i in $(seq 60); do kill -0 $GAME 2>/dev/null || break; sleep 0.1; done
! kill -0 $GAME 2>/dev/null && ok "the game closed at the end" || bad "the game still runs"
grep -q '^up [0-9]*$' /run/banana/playtime-notice && ok "said: up" || bad "notice: $(cat /run/banana/playtime-notice)"
grep -q "Parental controls: today's 1 min played: the game saved and closed" /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
read -r d s < /data/banana/playtime.txt
[ "$d" = "$(date +%F)" ] && [ "$s" -ge 60 ] && ok "today's time kept: $s s" || bad "playtime.txt: $d $s"
stop

echo "playtime 3. another day's time doesn't count: a new game, 1 minute left, warned once at its start"
echo "2000-01-01 3000" > /data/banana/playtime.txt; rm -f /run/banana/playtime-notice
setsid sleep 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid
start; sleep 1.5
grep -q '^left 1 ' /run/banana/playtime-notice && kill -0 $GAME 2>/dev/null && ok "yesterday's 50 min forgotten: 1 minute left, playing" ||
    bad "notice $(cat /run/banana/playtime-notice 2>&1), game $(kill -0 $GAME 2>/dev/null && echo runs || echo gone)"
stop
kill $GAME 2>/dev/null; rm -f /run/banana/game.pid /run/banana/playtime-notice /data/banana/playtime.txt /data/banana/parental.conf
printf '%s\n' "$PT_CONF" > /data/banana/settings.conf
