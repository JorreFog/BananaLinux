#!/bin/sh
# A stand-in for /usr/libexec/banana/steam in the headless test (the first-time setup's Steam slide, Settings >
# System > Accounts): its interface, driven by the password typed. "right" logs in at once; "guard" asks for the
# authenticator app's code (12345 is right, anything else wrong-code and asked again); "mail" for a mailed code;
# "phone" waits for the approval in the Steam app (then logs in); anything else fails. The state goes to
# $BANANA_RUN/steam.state as the real tool writes it; the account logged in is kept in $BANANA_RUN/fake-steam.user.
# login-qr writes a code to $BANANA_RUN/steam.qr as the real tool does (below). Each call goes to
# $BANANA_RUN/fake-steam.log (never the password).
RUN=${BANANA_RUN:-/run/banana}
ST=$RUN/steam.state
echo "$1 $2" >> "$RUN/fake-steam.log"
waitcode() {
    rm -f "$RUN/fake-steam.code"
    i=0
    while [ ! -s "$RUN/fake-steam.code" ]; do
        [ -f "$RUN/fake-steam.cancel" ] && { rm -f "$RUN/fake-steam.cancel"; echo "failed Cancelled" > "$ST"; exit 1; }
        i=$((i + 1)); [ $i -gt 300 ] && { echo "failed Timed out" > "$ST"; exit 1; }
        sleep 0.05
    done
    code=$(cat "$RUN/fake-steam.code"); rm -f "$RUN/fake-steam.code"
}
case $1 in
status)
    echo "ready=yes"
    echo "user=$(cat "$RUN/fake-steam.user" 2>/dev/null)"
    [ -f "$RUN/fake-steam.games" ] && echo "games=$(cat "$RUN/fake-steam.games")" || echo "games="
    ;;
login)
    read -r pw
    echo starting > "$ST"
    sleep 0.1
    case $pw in
    right) ;;
    guard|mail)
        if [ "$pw" = guard ]; then echo "code app" > "$ST"; else echo "code email j***@example.com" > "$ST"; fi
        waitcode
        while [ "$code" != 12345 ]; do echo wrong-code > "$ST"; waitcode; done
        ;;
    phone) echo confirm > "$ST"; sleep 0.6 ;;
    *) echo "failed Invalid password" > "$ST"; exit 1 ;;
    esac
    echo "$2" > "$RUN/fake-steam.user"
    echo "ok $2" > "$ST"
    exit 0
    ;;
login-qr)
    # the sign-in with the Steam app: a code (a fixed 25 x 25 matrix), a new one ($FAKE_STEAM_QR_ROUNDS of them, 2 by
    # default, 1 s apart), then ok as the account the app approved ($FAKE_STEAM_QR=fail: failed)
    echo starting > "$ST"
    sleep 0.1
    rounds=${FAKE_STEAM_QR_ROUNDS:-2}; n=1
    while [ $n -le "$rounds" ]; do
        i=0; : > "$ST.qr.tmp"
        while [ $i -lt 25 ]; do
            j=0; row=
            while [ $j -lt 25 ]; do
                if [ $i -lt 2 ] || [ $j -lt 2 ] || [ $i -gt 22 ] || [ $j -gt 22 ]; then row=${row}0
                elif [ $(( (i * 7 + j * 5 + i * j + n) % 3 )) = 0 ] || { [ $i -lt 9 ] && [ $j -lt 9 ]; }; then row=${row}1
                else row=${row}0; fi
                j=$((j + 1))
            done
            echo "$row" >> "$ST.qr.tmp"; i=$((i + 1))
        done
        mv "$ST.qr.tmp" "$RUN/steam.qr"
        echo "qr $n" > "$ST"
        k=0
        while [ $k -lt 20 ]; do
            [ -f "$RUN/fake-steam.cancel" ] && { rm -f "$RUN/fake-steam.cancel" "$RUN/steam.qr"; echo "failed Cancelled" > "$ST"; exit 1; }
            sleep 0.05; k=$((k + 1))
        done
        n=$((n + 1))
    done
    rm -f "$RUN/steam.qr"
    [ "$FAKE_STEAM_QR" = fail ] && { echo "failed The code expired" > "$ST"; exit 1; }
    echo "scanned" > "$RUN/fake-steam.user"
    echo "ok scanned" > "$ST"
    exit 0
    ;;
code) printf '%s\n' "$2" > "$RUN/fake-steam.code" ;;
cancel) : > "$RUN/fake-steam.cancel" ;;
logout) rm -f "$RUN/fake-steam.user" "$RUN/fake-steam.games" ;;
library) sleep 0.2; echo 3 > "$RUN/fake-steam.games"; echo "games 3" ;;
esac
