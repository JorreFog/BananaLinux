#!/usr/bin/env python3
"""Send HMP commands to a QEMU monitor that is on a unix socket.

    tools/qemu-monitor.py <socket> <command>...

For a headless run (tools/qemu-headless.sh leaves the monitor on monitor.sock while QEMU runs):
    tools/qemu-monitor.py /tmp/q/monitor.sock 'screendump /tmp/q/x.png -f png gpu 0' 'sendkey ret'
"""
import socket
import sys
import time

path, cmds = sys.argv[1], sys.argv[2:]

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
end = time.time() + 20
while True:
    try:
        s.connect(path)
        break
    except OSError:
        if time.time() > end:
            raise
        time.sleep(0.2)
s.settimeout(5)

# eat the prompt
try:
    s.recv(65536)
except socket.timeout:
    pass

for c in cmds:
    s.sendall(c.encode() + b"\n")
    time.sleep(0.4)
    out = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            out += chunk
            time.sleep(0.2)
            s.settimeout(0.4)
    except socket.timeout:
        pass
    s.settimeout(5)
    text = out.decode("utf-8", "replace")
    lines = [l for l in text.splitlines() if l.strip() and "(qemu)" not in l]
    if lines:
        print("\n".join(lines))
s.close()
