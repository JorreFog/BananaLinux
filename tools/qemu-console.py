#!/usr/bin/env python3
"""Type commands on a QEMU serial console that is on a unix socket.

    tools/qemu-console.py <socket> [--timeout SECONDS] [--raw] <command>...

Logs in as root when the console asks (the image's getty, from inittab: root's password is empty), runs each command,
and prints what the console said. A command is done when nothing has come for 1.5 s. --raw prints everything read,
including the login banner and the echoed commands.

No root, no SSH: the console is the guest's own tty (QEMU's -serial unix:... --chardev socket).
"""
import argparse
import socket
import sys
import time

QUIET = 1.5


def open_socket(path, timeout):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    end = time.time() + timeout
    while True:
        try:
            s.connect(path)
            return s
        except OSError:
            if time.time() > end:
                raise
            time.sleep(0.2)


def drain(sock, quiet=QUIET, limit=120.0):
    """Read until nothing has come for `quiet` seconds."""
    sock.settimeout(0.25)
    buf = b""
    last = time.time()
    start = time.time()
    while time.time() - start < limit:
        try:
            data = sock.recv(65536)
            if not data:
                break
            buf += data
            last = time.time()
        except socket.timeout:
            if buf and time.time() - last >= quiet:
                break
    return buf.decode("utf-8", "replace")


def expect(sock, needles, timeout=15.0):
    """Read until one of the needles (case-insensitive) is in what came; returns (text, found)."""
    sock.settimeout(0.25)
    buf = ""
    end = time.time() + timeout
    while time.time() < end:
        try:
            data = sock.recv(65536)
            if not data:
                break
            buf += data.decode("utf-8", "replace")
            low = buf.lower()
            if any(n in low for n in needles):
                return buf, True
        except socket.timeout:
            continue
    return buf, False


def login(sock, timeout=20.0):
    """Get a shell on the console; returns the text seen."""
    seen = drain(sock, 0.8, 5.0)		# (anything left over from a last session)
    sock.sendall(b"\n")
    out, found = expect(sock, ["login:", "#", "$"], timeout)
    seen += out
    if found and "login:" in out.lower():
        sock.sendall(b"root\n")
        out, found = expect(sock, ["password:", "#", "$"], timeout)
        seen += out
        if "password:" in out.lower():
            sock.sendall(b"\n")		# (root's is empty)
            out, found = expect(sock, ["incorrect", "#", "$"], timeout)
            seen += out
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("socket")
    ap.add_argument("--timeout", type=float, default=60.0, help="how long to wait for the console (s)")
    ap.add_argument("--per-command", type=float, default=180.0)
    ap.add_argument("--until", default="", help="wait for this text after each command (a command that prints nothing "
                    "while it runs, like wget of a big file, is otherwise taken as finished while it is still going)")
    ap.add_argument("--until-prompt", action="store_true", help="wait for the shell's prompt after each command "
                    "instead of for quiet: the prompt is only printed when the command is done")
    ap.add_argument("--raw", action="store_true")
    ap.add_argument("commands", nargs="*")
    a = ap.parse_args()

    s = open_socket(a.socket, a.timeout)
    banner = login(s)
    if a.raw and banner:
        sys.stdout.write(banner)

    for cmd in a.commands:
        s.sendall(cmd.encode() + b"\n")
        if a.until_prompt:
            out, found = expect(s, ["\n# ", "\n~ # ", "\n$ "], a.per_command)
        elif a.until:
            out, found = expect(s, [a.until], a.per_command)
        else:
            out = drain(s, QUIET, a.per_command)
        if a.raw:
            sys.stdout.write(out)
        else:
            lines = out.splitlines()
            if lines and cmd.strip() and cmd.strip() in lines[0]:
                lines = lines[1:]
            sys.stdout.write("\n".join(lines).lstrip("\n") + ("\n" if lines else ""))
        sys.stdout.flush()
    s.close()


if __name__ == "__main__":
    main()
