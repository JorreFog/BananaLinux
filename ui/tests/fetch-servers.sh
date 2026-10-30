#!/bin/sh
# banana-fetch against real servers (ui/tests/servers/docker-compose.yml): FTP (vsftpd, which has no MLSD:
# LIST instead), FTP with MLSD (pure-ftpd), FTPS (explicit TLS, its certificate pinned), SFTP (OpenSSH, its host key
# pinned) and SMB (Samba). For each: a server added, a folder listed, a job that downloads one file whole and resumes
# another from a part, a wrong password refused, a changed key refused, and a download whose connection is cut in the
# middle (ui/tests/servers/cutproxy.py between them: tried again, resumed, whole); and SMB's guest and share list,
# FTP's names in CP1251.
#   ui/tests/fetch-servers.sh --setup     the FTPS certificate and the files to serve, before
#   docker compose -f ui/tests/servers/docker-compose.yml up -d --wait
#   ui/tests/fetch-servers.sh             the test
# The servers' addresses: <FTP|FTPS|MLSD|SFTP|SMB>_HOST and _PORT (127.0.0.1 and 2121, 2122, 2123, 2222, 4445 by
# default); the files they serve: SHARE (ui/tests/servers/share). The Samba of docker-compose.yml can't list its
# shares (no RPC services in its image; nor can one in an Ubuntu container), so "/" is checked loosely; against a NAS or
# Windows, SMB_SHARES=<its shares, comma-separated> makes the check strict.
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
SRV=$HERE/tests/servers
SHARE=${SHARE:-$SRV/share}
if [ "$1" = --setup ]; then
    mkdir -p "$SRV/tls" "$SHARE/Games/NDS" "$SHARE/Games/GBA"
    [ -f "$SRV/tls/cert.pem" ] || openssl req -x509 -newkey rsa:2048 -nodes -days 3650 -subj "/CN=banana-fetch-test" \
        -keyout "$SRV/tls/key.pem" -out "$SRV/tls/cert.pem" 2>/dev/null
    head -c 2000000 /dev/urandom > "$SHARE/Games/NDS/Banana Kart.nds"
    head -c 5000 /dev/urandom > "$SHARE/Games/GBA/Peel.gba"
    head -c 20000000 /dev/urandom > "$SHARE/Games/big.bin"	# (cut in the middle)
    # a name in CP1251 ("Привет.txt"), as an old Windows FTP server keeps it
    python3 -c 'import os, sys; open(os.fsencode(sys.argv[1]) + b"/Games/\xcf\xf0\xe8\xe2\xe5\xf2.txt", "wb").write(b"privet")' "$SHARE"
    chmod -R a+rX "$SHARE" "$SRV/tls"
    exit 0
fi
pkg-config --exists libsmb2 2>/dev/null && smb=1 || smb=0
make -s -C "$HERE" banana-fetch SMB=$smb >/dev/null
F=$HERE/banana-fetch
OUT=${OUT:-$HERE/tests/out/fetch-servers}
rm -rf "$OUT"; mkdir -p "$OUT"
export BANANA_DATA="$OUT/data" BANANA_RUN="$OUT/run"
mkdir -p "$BANANA_DATA/downloads" "$BANANA_RUN"
pass=0 fail=0
check() { if eval "$2"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "FAIL: $1"; fi; }
tab=$(printf '\t')
ask() { printf '%s\n' "$@" | sed "s/|/$tab/g" | $F serve; }

one() {   # one <label> <protocol> <host> <port> <tls> <the share's root on the server>
    label=$1 proto=$2 host=$3 port=$4 tls=$5 root=$6
    id=$(ask "put|name=$label|protocol=$proto|host=$host|port=$port|user=test|password=test|remember=yes|tls=$tls" | sed -n "s/^ok$tab//p")
    r=$(ask "connect|$id" "list|$root/Games/NDS")
    check "$label: connected ($(echo "$r" | head -1))" 'echo "$r" | head -1 | grep -q "^ok"'
    check "$label: listed" 'echo "$r" | grep -q "^e${tab}f${tab}2000000${tab}[0-9]*${tab}Banana Kart.nds$"'
    pin=$(sed -n "/^\[$id\]/,/^\[/s/^hostkey = //p" "$BANANA_DATA/nas.conf")
    [ "$proto" = sftp ] && check "$label: the host key pinned ($pin)" 'echo "$pin" | grep -q "^[a-z0-9-]* SHA256:"'
    [ "$tls" = yes ] && check "$label: the certificate pinned ($pin)" 'echo "$pin" | grep -q "^SHA256:"'
    # a job: a file resumed from a part (its first half, as a download cut off leaves it), and one whole
    T=$OUT/card/$label
    mkdir -p "$T"
    head -c 1000000 "$SHARE/Games/NDS/Banana Kart.nds" > "$T/Banana Kart.nds.part"
    { printf 'title = %s\nserver = %s\nunpack = no\ntemp_root = %s\n' "$label" "$id" "$OUT/card"
      printf 'get\t2000000\t%s\t%s\n' "$root/Games/NDS/Banana Kart.nds" "$T/Banana Kart.nds"
      printf 'get\t5000\t%s\t%s\n' "$root/Games/GBA/Peel.gba" "$T/Peel.gba"; } > "$BANANA_DATA/downloads/$label.job"
    $F run
    check "$label: resumed from a part ($(cat "$BANANA_RUN/fetch/status"))" 'cmp -s "$SHARE/Games/NDS/Banana Kart.nds" "$T/Banana Kart.nds"'
    check "$label: downloaded" 'cmp -s "$SHARE/Games/GBA/Peel.gba" "$T/Peel.gba"'
    r=$(ask "connect|$id|wrong")
    check "$label: a wrong password refused ($r)" 'echo "$r" | grep -q "^err${tab}auth"'
    if [ -n "$pin" ]; then
        fake=$(echo "$pin" | sed 's/SHA256:.*/SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA/')
        ask "trust|$id|$fake" >/dev/null
        r=$(ask "connect|$id")
        check "$label: a changed key refused, the new one told" 'echo "$r" | grep -q "^err${tab}hostkey${tab}.*${tab}$fake${tab}$pin$"'
        ask "trust|$id|$pin" >/dev/null
    fi
}

# a download of 20 MB (slowed to 4 MB/s) through cutproxy.py, which cuts its connections at 6 MB, as a NAS restarting
# does: the runner tries again (3 s later) and goes on from the part. FTP's passive ports go through it too (EPSV:
# the proxy's address), when they are free here.
drop() {   # drop <label> <protocol> <host> <port> <tls> <the share's root> <the proxy's port> [<FTP's passive ports>...]
    label=$1 proto=$2 host=$3 port=$4 tls=$5 root=$6 via=$7
    shift 7
    python3 "$SRV/cutproxy.py" "$host" "$via:$port" "$@" 2> "$OUT/cut-$label.log" & px=$!
    i=0; until grep -q ready "$OUT/cut-$label.log" || [ $i -ge 50 ]; do sleep 0.1; i=$((i + 1)); done
    did=$(ask "put|name=$label cut|protocol=$proto|host=127.0.0.1|port=$via|user=test|password=test|remember=yes|tls=$tls" | sed -n "s/^ok$tab//p")
    T=$OUT/card/$label-cut
    mkdir -p "$T"
    printf 'title = %s cut\nserver = %s\nunpack = no\ntemp_root = %s\nget\t20000000\t%s\t%s\n' "$label" "$did" "$OUT/card" \
        "$root/Games/big.bin" "$T/big.bin" > "$BANANA_DATA/downloads/$label-cut.job"
    BANANA_FETCH_TEST=1 BANANA_FETCH_TEST_RATE=4000000 $F run & rp=$!
    i=0; until [ "$(stat -c %s "$T/big.bin.part" 2>/dev/null || echo 0)" -gt 6000000 ] || [ $i -ge 300 ]; do sleep 0.1; i=$((i + 1)); done
    kill -USR1 $px
    wait $rp || true
    kill $px; wait $px 2>/dev/null || true
    check "$label: cut in the middle, tried again, resumed ($(grep 'cutproxy: cut' "$OUT/cut-$label.log"))" \
        'grep -q "cut [1-9]" "$OUT/cut-$label.log" && cmp -s "$SHARE/Games/big.bin" "$T/big.bin"'
}

# (this vsftpd doesn't lock the account in its folder: the login lands in /ftp/test, and "/" is the server's disk)
one FTP ftp "${FTP_HOST:-127.0.0.1}" "${FTP_PORT:-2121}" no /ftp/test
r=$(ask "connect|$id")
check "FTP: the home folder told" 'echo "$r" | grep -q "^ok${tab}${tab}/ftp/test$"'
drop FTP ftp "${FTP_HOST:-127.0.0.1}" "${FTP_PORT:-2121}" no /ftp/test 12121 $(seq 21000 21010)
one FTPS ftp "${FTPS_HOST:-127.0.0.1}" "${FTPS_PORT:-2122}" yes /ftp/test
drop FTPS ftp "${FTPS_HOST:-127.0.0.1}" "${FTPS_PORT:-2122}" yes /ftp/test 12122 $(seq 21011 21020)
# (pure-ftpd keeps the account in its folder: "/" is the share) MLSD's lines have the files' dates; LIST's, as libcurl
# reads them, have none
one MLSD ftp "${MLSD_HOST:-127.0.0.1}" "${MLSD_PORT:-2123}" no ""
r=$(ask "connect|$id" "list|/Games/NDS")
check "MLSD: listed with MLSD" 'echo "$r" | grep -q "^e${tab}f${tab}2000000${tab}[1-9][0-9]*${tab}Banana Kart.nds$"'
one SFTP sftp "${SFTP_HOST:-127.0.0.1}" "${SFTP_PORT:-2222}" no /share
drop SFTP sftp "${SFTP_HOST:-127.0.0.1}" "${SFTP_PORT:-2222}" no /share 12222
if [ $smb = 1 ]; then
    one SMB smb "${SMB_HOST:-127.0.0.1}" "${SMB_PORT:-4445}" no /Data
    sid=$id
    drop SMB smb "${SMB_HOST:-127.0.0.1}" "${SMB_PORT:-4445}" no /Data 14445
    # a guest (smb.conf's share Public: a name Samba doesn't know is its guest)
    gid=$(ask "put|name=Guest|protocol=smb|host=${SMB_HOST:-127.0.0.1}|port=${SMB_PORT:-4445}|guest=yes|start=/Public" | sed -n "s/^ok$tab//p")
    r=$(ask "connect|$gid" "list|/Public/Games/NDS")
    check "SMB: a guest ($(echo "$r" | head -1))" 'echo "$r" | grep -q "^e${tab}f${tab}2000000${tab}[0-9]*${tab}Banana Kart.nds$"'
    # the share list: strict with SMB_SHARES (a NAS, Windows); else this Samba's error, which says what to do instead
    r=$(ask "connect|$sid" "list|/")
    if [ -n "$SMB_SHARES" ]; then
        for s in $(echo "$SMB_SHARES" | tr ',' ' '); do check "SMB: the share list has $s" 'echo "$r" | grep -q "^e${tab}d${tab}0${tab}0${tab}$s$"'; done
    else
        check "SMB: the share list, or how to do without it" 'echo "$r" | grep -q "^e${tab}d${tab}0${tab}0${tab}Data$" || echo "$r" | grep -q "^err${tab}other${tab}.*Start folder"'
        echo "fetch-servers: SMB's share list checked loosely (SMB_SHARES=<shares> against a NAS or Windows)"
    fi
else
    echo "fetch-servers: no libsmb2 here: SMB skipped"
fi
cid=$(ask "put|name=Old|protocol=ftp|host=${FTP_HOST:-127.0.0.1}|port=${FTP_PORT:-2121}|user=test|password=test|encoding=cp1251" | sed -n "s/^ok$tab//p")
r=$(ask "connect|$cid" "list|/ftp/test/Games")
check "FTP: names in CP1251" 'echo "$r" | grep -q "${tab}Привет.txt$"'

echo "fetch-servers: $pass passed, $fail failed"
[ $fail = 0 ]
