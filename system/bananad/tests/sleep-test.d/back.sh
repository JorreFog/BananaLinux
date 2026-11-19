# Back (BTN_Z) in a port and in PICO-8, sourced by sleep-test.sh: twice within half a second quits; once in PICO-8's
# Splore, PICO-8 ended with /run/banana/pico8-back left (run-pico8 starts Splore again). Stand-in games: sleep under
# the names bananad looks for (run-port, pico8_64), each in its own process group as the menu starts them.
BTN_Z=309
BK=/run/test/back; mkdir -p $BK
cp "$(command -v sleep)" $BK/run-port; cp "$(command -v sleep)" $BK/pico8_64; cp "$(command -v sleep)" $BK/other
back() { ev $EV_KEY $BTN_Z 1; ev $EV_KEY $BTN_Z 0; }
game() { setsid $BK/$1 600 & GAME=$!; sleep 0.1; echo $GAME > /run/banana/game.pid; }
gone() { for i in $(seq 30); do kill -0 $GAME 2>/dev/null || return 0; sleep 0.1; done; return 1; }
done_game() { kill $GAME 2>/dev/null || true; wait $GAME 2>/dev/null || true; rm -f /run/banana/game.pid /run/banana/pico8-splore /run/banana/pico8-back; }
start

echo "back 1. a port: Back once does nothing, twice quickly quits it"
game run-port
back; sleep 1
kill -0 $GAME 2>/dev/null && ok "once: the port plays on" || bad "once ended the port"
back; back
gone && ok "twice: the port quit" || bad "twice: the port still runs"
grep -q 'back twice: the port quits' /data/banana/logs/sleep.log && ok "logged" || bad "not logged"
done_game

echo "back 2. a port: two presses a second apart aren't twice"
game run-port
back; sleep 1; back; sleep 1
kill -0 $GAME 2>/dev/null && ok "the port plays on" || bad "two slow presses ended the port"
done_game

echo "back 3. PICO-8 on Splore: once, ended with pico8-back left (Splore again); twice, ended with none"
game pico8_64; : > /run/banana/pico8-splore
back
gone && ok "once: PICO-8 ended" || bad "once: PICO-8 still runs"
[ -e /run/banana/pico8-back ] && ok "pico8-back left for run-pico8" || bad "no pico8-back"
done_game
game pico8_64; : > /run/banana/pico8-splore
back; back
gone && ok "twice: PICO-8 ended" || bad "twice: PICO-8 still runs"
sleep 0.8
[ ! -e /run/banana/pico8-back ] && ok "no pico8-back: not started again" || bad "pico8-back left after twice"
done_game

echo "back 4. PICO-8 on a cart of its own (no Splore): once does nothing"
game pico8_64
back; sleep 1
kill -0 $GAME 2>/dev/null && [ ! -e /run/banana/pico8-back ] && ok "plays on" || bad "once ended a cart not from Splore"
done_game

echo "back 5. another game: Back is the game's own"
game other
back; back; sleep 1
kill -0 $GAME 2>/dev/null && ok "plays on" || bad "twice ended a game that isn't a port"
done_game

stop
rm -rf $BK
