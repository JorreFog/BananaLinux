#!/bin/bash
# system/tests/sftp-test.sh's checks, in its container (privileged): the handheld's layout (the card's exFAT as a
# tmpfs anyone writes, as uid=0,fmask=0000,dmask=0000 makes it), then the ssh and share scripts for real
set -u
pass=0; fail=0
ok() { echo "  ok: $1"; pass=$((pass+1)); }
bad() { echo "  FAIL: $1"; fail=$((fail+1)); }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
# busybox for every applet, as on the handheld, but flock (util-linux's there: busybox's has no -o)
for a in $(/opt/busybox --list-full | grep -v flock); do ln -sf /opt/busybox /$a; done
mkdir -p /data /run/banana /run/sd2 /usr/libexec/banana
mount -t tmpfs data /data && chmod 777 /data
mount -t tmpfs sd2 /run/sd2 && chmod 777 /run/sd2
mkdir -p /data/banana/ssh /data/roms/gba
printf 'smb_user=banana\nsmb_password=linux\nra_password=SECRET-RA\n' > /data/banana/accounts.conf
printf 'network={ psk="SECRET-WIFI" }\n' > /data/banana/wifi.conf
echo "Pokémon Émeraude" > "/data/roms/gba/Pokémon Émeraude.gba"
echo second > /run/sd2/on-sd2.txt
printf 'share = on\nshare_login = password\n' > /data/banana/settings.conf
chmod -R 777 /data /run/sd2   # (exFAT: uid=0,fmask=0000,dmask=0000)
sed -i 's/^root:[^:]*:/root::/' /etc/shadow      # (Buildroot's: a blank root password)
rm -rf /root/.ssh; ln -s /run/banana/ssh /root/.ssh
cp /t/ssh /t/share /t/sftp-session /usr/libexec/banana/ && chmod +x /usr/libexec/banana/*
for s in ksmbd.adduser ksmbd.control modprobe; do rm -f /usr/sbin/$s /sbin/$s /usr/bin/$s /bin/$s; printf "#!/bin/sh\nexit 0\n" > /usr/sbin/$s; chmod +x /usr/sbin/$s; done
# ksmbd.mountd stays running (pidof finds it), ksmbd.control -s stops it, as the real ones do
printf '#include <unistd.h>\nint main(void){ if(fork()) return 0; setsid(); for(;;) pause(); }\n' > /tmp/m.c
rm -f /usr/sbin/ksmbd.mountd; gcc -o /usr/sbin/ksmbd.mountd /tmp/m.c
printf '#!/bin/sh\nkillall ksmbd.mountd; exit 0\n' > /usr/sbin/ksmbd.control
printf '#!/bin/sh\nexit 0\n' > /usr/libexec/banana/ra-secrets; chmod +x /usr/libexec/banana/ra-secrets
ssh-keygen -q -t ed25519 -N '' -f /tmp/k
# gesftpclient's way to its server: root's SSH, by key
printf '#!/bin/sh\nexec ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -i /tmp/k -s root@127.0.0.1 sftp\n' > /tmp/sshkey
chmod +x /tmp/sshkey
cp /tmp/k.pub /data/banana/ssh/authorized_keys
O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5"
pw() { sshpass -p "$1" "${@:2}"; }
sf() { timeout 20 sshpass -p "$1" sftp $O -o PubkeyAuthentication=no "$2"@127.0.0.1; }   # commands on stdin (not -b: batch mode never sends a password)
dmode() { cat /run/banana/dropbear.mode 2>/dev/null; }

echo "== SSH alone (Settings: ssh up)"
/usr/libexec/banana/ssh up; sleep 0.5
check "dropbear up, mode r" '[ "$(dmode)" = r ]'
check "root by key gets a shell" '[ "$(ssh $O -i /tmp/k root@127.0.0.1 echo hi)" = hi ]'
check "root by key gets SFTP (unconfined, as root)" 'echo "ls /etc/passwd" | timeout 20 sftp $O -i /tmp/k root@127.0.0.1 2>&1 | grep -q /etc/passwd'
check "no password logins (root blank, banana)" '! pw "" ssh $O -o PubkeyAuthentication=no root@127.0.0.1 true 2>/dev/null && ! sf linux banana </dev/null 2>/dev/null'

echo "== File sharing on with a name and password (share up)"
/usr/libexec/banana/share up; sleep 0.5
check "mode rs" '[ "$(dmode)" = rs ]'
check "/etc/passwd has banana, uid 0, sftp-session" 'grep -q "^banana:x:0:0:.*:/usr/libexec/banana/sftp-session$" /etc/passwd'
check "root's password locked" 'grep -q "^root:\*:" /etc/shadow'
L=$(printf 'ls -1 /\nls -1 /banana/roms/gba\nls -1 /sd2\n' | sf linux banana 2>&1)
check "/ is banana and sd2" 'printf "%s" "$L" | grep -qx /banana && printf "%s" "$L" | grep -qx /sd2'
check "accented name listed" 'printf "%s" "$L" | grep -qF "Pok\\303\\251mon \\303\\211meraude.gba"'
check "second card" 'printf "%s" "$L" | grep -q on-sd2.txt'
check "wrong password refused" '! echo "ls /" | sf nope banana >/dev/null 2>&1'
rm -f /tmp/got; echo 'get /banana/banana/accounts.conf /tmp/got' | sf linux banana >/dev/null 2>&1
check "accounts.conf reads empty" '[ -f /tmp/got ] && [ ! -s /tmp/got ]'
rm -f /tmp/got; echo 'get /banana/banana/ssh/authorized_keys /tmp/got' | sf linux banana >/dev/null 2>&1
check "root's authorized_keys reads empty" '[ -f /tmp/got ] && [ ! -s /tmp/got ]'
echo 'ssh-ed25519 AAAA evil' > /tmp/evil
printf 'put /tmp/evil /banana/banana/ssh/authorized_keys\n' | sf linux banana >/dev/null 2>&1
check "can't write root's keys" '! grep -q evil /data/banana/ssh/authorized_keys'
printf 'rm /banana/banana/wifi.conf\n' | sf linux banana >/dev/null 2>&1
check "can't delete wifi.conf" 'grep -q SECRET-WIFI /data/banana/wifi.conf'
check "the menu still sees the real accounts.conf" 'grep -q SECRET-RA /data/banana/accounts.conf'
printf 'put /tmp/evil "/banana/roms/gba/new é.txt"\nrename "/banana/roms/gba/new é.txt" /banana/roms/gba/renamed.txt\nmkdir /banana/roms/nds\n' | sf linux banana >/dev/null 2>&1
check "upload, rename, mkdir on the card" '[ -f /data/roms/gba/renamed.txt ] && [ -d /data/roms/nds ]'
check "the new file is owned by nobody (fine on exFAT)" '[ "$(stat -c %U /data/roms/gba/renamed.txt)" = nobody ]'
printf 'ls -1 /..\nput /tmp/evil /etc-test\n' | sf linux banana >/tmp/o 2>&1
check "/.. is the view's own / (no system folders)" 'grep -qx "/../banana" /tmp/o && ! grep -qE "^/\.\./(etc|usr|proc|run)$" /tmp/o'
check "nothing written beside banana and sd2 (the view's / is read-only)" '! [ -e /etc-test ] && ! grep -qx "/etc-test" <(echo "ls -1 /" | sf linux banana 2>/dev/null)'
printf 'symlink /etc/shadow /banana/sh\nget /banana/sh /tmp/sh\n' | sf linux banana >/dev/null 2>&1
check "a symlink can't reach the system's files" '! grep -q "^root" /tmp/sh 2>/dev/null'
out=$(pw linux ssh $O -o PubkeyAuthentication=no banana@127.0.0.1 'id; cat /etc/shadow' 2>&1 | head -c 300)
check "no shell for banana (a command gets nothing useful)" '! printf "%s" "$out" | grep -qE "uid=|root:"'
timeout 8 sshpass -p linux ssh $O -o PubkeyAuthentication=no -N -L 2222:127.0.0.1:22 banana@127.0.0.1 >/dev/null 2>&1 &
fw=$!; sleep 3
r=$(timeout 5 bash -c 'exec 3<>/dev/tcp/127.0.0.1/2222; timeout 2 head -c 7 <&3' 2>/dev/null)
check "no port forwarding (no SSH banner through it)" '[ "$r" != "SSH-2.0" ]'; kill $fw 2>/dev/null; wait $fw 2>/dev/null
check "root by key still a shell" '[ "$(ssh $O -i /tmp/k root@127.0.0.1 echo hi)" = hi ]'
check "root by blank password refused" '! pw "" ssh $O -o PubkeyAuthentication=no root@127.0.0.1 true 2>/dev/null'
if command -v gesftpclient >/dev/null; then
  # (over root's key: the same server and patch; sshpass can't sit in front of this client)
  r=$(printf 'ls /data/roms/gba\n' | LC_ALL=C.UTF-8 timeout 20 gesftpclient --sftp-version=6 --program=/tmp/sshkey 2>&1)
  check "SFTP v6 (WinSCP's) lists the accented name" 'printf "%s" "$r" | grep -q "Pokémon Émeraude.gba"'
fi

echo "== a session open while the share stops"
(printf 'ls /\n'; sleep 20) | timeout 25 sshpass -p linux sftp $O -o PubkeyAuthentication=no banana@127.0.0.1 >/dev/null 2>&1 &
held=$!; sleep 3
check "a session's server is running" 'pgrep -f "chroot /run/banana/sftp-view" >/dev/null'
/usr/libexec/banana/share down; sleep 0.5
check "its server killed" '! pgrep -f "chroot /run/banana/sftp-view" >/dev/null'
kill $held 2>/dev/null; wait $held 2>/dev/null
check "mode back to r, /etc unbound" '[ "$(dmode)" = r ] && ! grep -q "^banana:" /etc/passwd && ! grep -q " /etc/passwd " /proc/mounts'
check "banana refused again" '! echo "ls /" | sf linux banana >/dev/null 2>&1'

echo "== SSH off, share on: SFTP alone"
/usr/libexec/banana/ssh down; /usr/libexec/banana/share up; sleep 0.5
check "mode s" '[ "$(dmode)" = s ]'
check "root's key refused" '! ssh $O -o BatchMode=yes -i /tmp/k root@127.0.0.1 true 2>/dev/null'
check "banana's SFTP works" 'echo "ls -1 /" | sf linux banana 2>&1 | grep -qx /banana'

echo "== a new password (share restart, as Settings does)"
sed -i 's/^smb_password=.*/smb_password=n3w-pass/' /data/banana/accounts.conf
/usr/libexec/banana/share restart; sleep 0.5
check "old password refused" '! echo "ls /" | sf linux banana >/dev/null 2>&1'
check "new one works" 'echo "ls -1 /" | sf n3w-pass banana 2>&1 | grep -qx /banana'

echo "== Guest: no SFTP"
sed -i 's/^share_login.*/share_login = guest/' /data/banana/settings.conf
/usr/libexec/banana/share restart; sleep 0.5
check "dropbear stopped (SSH off, no SFTP)" '[ -z "$(dmode)" ] && ! pgrep -x dropbear >/dev/null'

echo "== names the system has"
for n in root nobody daemon; do
  printf '%s\nlinux\n' $n | /usr/libexec/banana/ssh sftp-on 2>/dev/null
  check "'$n' gets no SFTP" '[ ! -f /run/banana/sftp/user ] && ! grep -q " /etc/passwd " /proc/mounts'
done
printf 'banana\n\n' | /usr/libexec/banana/ssh sftp-on 2>/dev/null
check "an empty password gets no SFTP" '[ ! -f /run/banana/sftp/user ]'
check "nothing left running" '! pgrep -x dropbear >/dev/null'
echo "sftp test: $pass passed, $fail failed"
[ $fail = 0 ]
