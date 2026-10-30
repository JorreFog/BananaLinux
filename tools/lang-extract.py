#!/usr/bin/env python3
# lang-extract.py: every English line BananaOS shows on screen, from the sources, for the next translation document
# (tools/lang-to-docx.py writes it) and for checking what a language file is missing.
#
#   lang-extract.py [--bl DIR] [--sd DIR] [--bd DIR] [--out strings.tsv] [--doc OLD.docx] [--report]
#
#   --bl  BananaLinux checkout (banana-ui, its themes, systems.conf, the update script): default this repo
#   --sd  SuperDrastic checkout (src/: the in-game menu, pop-ups, RetroAchievements)
#   --bd  the checkout BananaDS is built from (superdrastic repo, bananads/src/)
#   --doc the last translation document: its lines tell which Where and part a new line belongs to (lines found in the
#         same function as an old one go under the same Where), and are matched back by their number.
#
# Output: one line per (part, English): part TAB where TAB English TAB sources (file:line, ...).
# Parts are the language file's: menu, settings, systems (banana-ui), bananads, superdrastic.
#
# How a string is found: every string literal of the C/C++ sources (adjacent literals joined), the "text" fields of the
# layout themes, the names and makers of systems.conf, and the messages of the update script that the menu shows.
# A literal is kept when it reads as text for a person (letters and a space, or one capitalised word) and isn't
# an argument of a log call, a file/settings key or a path. What that still lets through by mistake is listed in
# tools/lang-extract-ignore.txt (one English line, or "file: line" for one place, or "file:function" for all of one
# function); what it misses in tools/lang-extract-extra.txt (part TAB where TAB English).
# Only reads the sources. Run with python3 -I.
import argparse, collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# calls whose string arguments go to a log, a file name, a key, a process: never on screen
NOT_SHOWN = set("""fprintf printf puts perror syslog mlog SLOG LOG LOGE LOGW LOGI dlog logf log_msg logmsg dbg DBG debug trace
 vlog ulog blog klog warn warnx err errx fputs dprintf
 fopen freopen open openat opendir access stat lstat mkdir rmdir unlink rename remove readlink realpath chdir
 popen system execl execlp execv execvp execle spawn run runCmd runcmd sh shell cmd readFile writeFile readfile writefile
 getenv setenv unsetenv putenv dlopen dlsym getConfValue setConfValue getConf setConf conf confGet confSet cfg_get cfg_set
 strcmp strncmp strcasecmp strncasecmp strstr strcasestr strchr strrchr strspn strcspn sscanf fscanf strtok
 starts_with startsWith ends_with endsWith find rfind compare count has hasKey key keyOf get_key
 getenv_or env envOr jstr jnum jbool jget json_get str num bool_ boolean integer field obj arr member
 glGetUniformLocation glGetAttribLocation glBindAttribLocation eglGetProcAddress dlsym_ lookup_sym
 regcomp fnmatch glob inet_pton setlocale strftime_ mount umount2 ioctl_
 tcp_connect http_get http_post curl_easy_setopt
 assert static_assert
 dsflip_log ralog start_mark memcmp prop setopt set_clock gameOpen bootMark read jsonField jsonObjects
 resume_control_button control_button_any control_button_in ctl_keys applyColors give_up tmark mark
 """.split())
LOG_RE = re.compile(r"\b(?:f?printf\s*\(\s*stderr|LOG\w*|\w*log\w*)\s*\($", re.I)

PH = re.compile(r"\{[^{}]+\}|%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z|j|t)?[diouxXeEfgGcsp%]|%[A-Za-z]")


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            i += 2
            if n == "n": out.append("\n")
            elif n == "t": out.append("\t")
            elif n == "r": out.append("\r")
            elif n == "0": out.append("\0")
            elif n == "x":
                m = re.match(r"[0-9a-fA-F]+", s[i:])
                h = m.group(0)[:2] if m else ""
                b = bytes([int(h, 16)]) if h else b""
                out.append(("\0BYTES" + b.hex() + "\0") if b else "")
                i += len(h)
            elif n in "01234567":
                m = re.match(r"[0-7]{1,2}", s[i:])
                out.append(chr(int(n + (m.group(0) if m else ""), 8)))
                i += len(m.group(0)) if m else 0
            else: out.append(n)
            continue
        out.append(c)
        i += 1
    r = "".join(out)
    # (UTF-8 written as \x escapes: put the bytes back together)
    if "\0BYTES" in r:
        parts = re.split(r"\0BYTES([0-9a-f]+)\0", r)
        buf = b""
        for k, p in enumerate(parts):
            buf += bytes.fromhex(p) if k % 2 else p.encode("utf-8")
        r = buf.decode("utf-8", "replace")
    return r


def c_literals(path):
    """(line, text, callee, function) for every string literal (adjacent ones joined) of a C/C++ file."""
    src = open(path, encoding="utf-8", errors="replace").read()
    i, n, line = 0, len(src), 1
    stack = []	# open parens: the identifier before each
    last_ident, last_sig = "", ""
    func, depth, pending_func, func_depth = "", 0, "", 0
    lits = []
    cur = None	# literal being joined: [line, text, callee]
    tok_since = True
    # function names: a top-level-ish definition line "... name(...) {" or "name(...)\n{"
    def flush():
        nonlocal cur
        if cur:
            lits.append([cur[0], cur[1], cur[2], func, cur[3], cur[4]])
            cur = None
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1; i += 1; continue
        if c in " \t\r":
            i += 1; continue
        if src.startswith("//", i):
            j = src.find("\n", i); i = n if j < 0 else j; continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2); j = n if j < 0 else j + 2
            line += src.count("\n", i, j); i = j; continue
        if c == "#" and (i == 0 or src.rfind("\n", 0, i) == i - 1 or src[src.rfind("\n", 0, i) + 1:i].strip() == ""):
            # preprocessor line (with continuations): #include "x" etc. aren't text
            j = i
            while True:
                k = src.find("\n", j)
                if k < 0: k = n; break
                if src[k - 1] == "\\": j = k + 1; continue
                break
            line += src.count("\n", i, k); i = k; continue
        m = re.match(r'(?:u8|u|U|L)?R"([^(\s]{0,16})\(', src[i:i + 24])
        if m:
            end = ")" + m.group(1) + '"'
            j = src.find(end, i + m.end()); j = n if j < 0 else j
            text = src[i + m.end():j]
            if cur is None: cur = [line, "", stack[-1] if stack else "", i, 0]
            cur[1] += text; cur[4] = j + len(end)
            line += src.count("\n", i, j); i = j + len(end); continue
        if c == '"' or (c in "uUL" and src[i + 1:i + 2] == '"') or (src.startswith('u8"', i)):
            j = src.find('"', i) + 1
            k = j
            while k < n and src[k] != '"':
                k += 2 if src[k] == "\\" else 1
            text = unescape(src[j:k])
            if cur is None: cur = [line, "", stack[-1] if stack else "", i, 0]
            cur[1] += text; cur[4] = k + 1
            line += src.count("\n", i, k); i = k + 1; continue
        if c == "'":
            k = i + 1
            while k < n and src[k] != "'":
                k += 2 if src[k] == "\\" else 1
            flush(); i = k + 1; continue
        flush()
        m = re.match(r"[A-Za-z_]\w*(?:::\w+)*", src[i:i + 200])
        if m:
            last_ident = m.group(0).split("::")[-1]; i += m.end(); continue
        if c == "(":
            stack.append(last_ident); last_ident = ""; i += 1; continue
        if c == ")":
            if stack: pending_func = stack.pop()
            last_ident = ""; i += 1; continue
        if c == "{":
            depth += 1
            # a function body opens right after "name(...)" (maybe "const", "override"): name it
            if pending_func and src[max(0, i - 40):i].rstrip().endswith((")", "const", "override", "noexcept")) and \
               pending_func not in ("if", "for", "while", "switch", "catch", "sizeof", "return") and depth <= 3:
                func, func_depth = pending_func, depth
            i += 1; continue
        if c == "}":
            depth -= 1
            if func and depth < func_depth:
                func = ""	# (out of the function: a table at file level is no function's)
            i += 1; continue
        if c == ";":
            pending_func = ""
        last_ident = "" if c not in "." else last_ident
        i += 1
    flush()
    return join_chains(src, lits)


STOP = set("std to_string string c_str substr empty size str data const auto int long float double unsigned char "
           "static_cast int64_t uint64_t lround round floor ceil min max abs fmt format strerror".split())


# placeholder names for the translators: the code's variable -> a word
PH_NAMES = {"e": "error", "err": "error", "why": "reason", "v": "count", "sh": "shader", "gshName": "shader", "g3d": "setting",
            "gmicName": "setting", "global": "setting", "colPattern": "pattern", "shareUser": "name", "sharePassword": "password",
            "artWords": "words", "clockNow": "time", "tail": "reason", "pgRom": "file", "joining": "network", "ssid": "network",
            "ip": "address", "at": "address", "t": "percent", "pc": "percent", "a": "done", "online": "total", "latest": "version",
            "dlLine": "reason", "ustatus": "status", "r": "value", "s": "folder", "ext": "extension", "achA": "unlocked",
            "achB": "total", "resumeTitle": "game", "off": "percent", "nameEn": "English name", "by": "translator"}


def placeholder(expr):
    ids = [w for w in re.findall(r"[A-Za-z_]\w*", re.sub(r'"(?:[^"\\]|\\.)*"', "", expr)) if w not in STOP]
    n = ids[-1] if ids and len(ids) <= 2 else ids[0] if ids else "value"
    return "{%s}" % PH_NAMES.get(n, n)


def operand_fwd(src, i):
    """the end of the operand starting at i (balanced brackets; stops at a top-level + , ; ) ] } ? : <<)"""
    depth, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"':
            k = i + 1
            while k < n and src[k] != '"': k += 2 if src[k] == "\\" else 1
            i = k + 1; continue
        if c == "'":
            k = i + 1
            while k < n and src[k] != "'": k += 2 if src[k] == "\\" else 1
            i = k + 1; continue
        if c in "([{": depth += 1
        elif c in ")]}":
            if depth == 0: return i
            depth -= 1
        elif depth == 0 and (c in ",;?:" and not src.startswith("::", i) and not src.startswith("::", i - 1) or
                             c == "+" or src.startswith("<<", i)):
            return i
        i += 1
    return i


def operand_back(src, j):
    """the start of the operand ending just before j (balanced brackets, backwards)"""
    depth, i = 0, j - 1
    while i >= 0:
        c = src[i]
        if c in ")]}": depth += 1
        elif c in "([{":
            if depth == 0: return i + 1
            depth -= 1
        elif depth == 0 and (c in ",;?=+!&|" or (c == ":" and src[i - 1:i] != ":" and src[i + 1:i + 2] != ":")):
            return i + 1
        elif depth == 0 and src[max(0, i - 6):i + 1].endswith("return") and (i - 6 < 0 or not (src[i - 6].isalnum() or src[i - 6] == "_")):
            return i + 1
        i -= 1
    return 0


def join_chains(src, lits):
    """"Connected to " + name + " (" + ip + ")." -> "Connected to {name} ({ip}).": a literal built into a line with +
    (or a stream's <<) is one line with placeholders, as the menu looks it up"""
    by_start = {l[4]: k for k, l in enumerate(lits)}
    gone = set()
    out = []
    for k, l in enumerate(lits):
        if k in gone:
            continue
        line, text, callee, fn, a, b = l
        joined = False
        m = re.search(r"(?:std::)?string\(\s*$", src[max(0, a - 16):a])
        if m and re.match(r"\s*\)", src[b:b + 8]):	# (std::string("...") + x: the same as "..." + x)
            a, b = a - (len(src[max(0, a - 16):a]) - m.start()), b + re.match(r"\s*\)", src[b:b + 8]).end()
        # backwards: x + "..." (one operand)
        m = re.search(r"(\+|<<)\s*$", src[max(0, a - 4):a])
        if m and not src[max(0, a - 4):a].rstrip().endswith(("++",)):
            opend = a - (len(src[max(0, a - 4):a]) - m.start())
            s0 = operand_back(src, opend)
            op = src[s0:opend].strip()
            if re.match(r"(if|else|while|for|return|do|case)\b", op):
                op = re.split(r"[\s)]+", op)[-1]
            if op and not op.startswith('"') and not re.fullmatch(r"\w+", op) or (op and re.fullmatch(r"[a-z]\w*", op) and m.group(1) == "+"):
                if m.group(1) == "+" or not re.fullmatch(r"\w+", op):
                    text = placeholder(op) + text; joined = True
        # forwards
        i = b
        while True:
            m = re.match(r"\s*(\+(?![+=])|<<)\s*", src[i:i + 40])
            if not m: break
            j = i + m.end()
            if j in by_start:
                t = lits[by_start[j]]
                text += t[1]; gone.add(by_start[j]); i = t[5]; joined = True
                continue
            e = operand_fwd(src, j)
            op = src[j:e].strip()
            if not op or op.startswith(("std::endl", "'\\n'")): break
            text += placeholder(op); joined = True
            i = e
        if joined:
            text = text.strip(" ")
        elif text.strip() and text[0] == " " and text[-1] == " ":
            text = text.strip()	# (padding)
        elif text.strip() and (text[0] == " ") != (text[-1] in " ("):
            # a piece the code builds a line from in a way not followed here (a ternary's arm, a stream)
            text = "{value}" + text.rstrip() if text[0] == " " else text.rstrip() + (" " if text[-1] == " " else "") + "{value}"
        out.append((line, text, callee, fn))
    return out


BUTTONS = r"(?:\b(?:F|L|R|L2|R2|L3|R3|X|Y|A|B|START|SELECT|MENU|HOME|UP|DOWN|LEFT|RIGHT|VOL|SHIFT)\b|[\s+/-])+"


def looks_shown(text):
    t = text.strip()
    if not t or "\0" in t:
        return False
    letters = re.sub(PH, "", t)
    if len(re.findall(r"[A-Za-zÀ-ÿ]", letters)) < 2:
        return False
    if re.match(r"(/|\./|~/|https?:|file:)", t) or (" " not in t and re.search(r"\.(png|jpe?g|ttf|otf|conf|cfg|json|tsv|sh|so|txt|xml|glsl|log|pid|img|zip|nds|sav|dsv|state|wav|bin|tar|gz)\b", t)):
        return False
    if re.search(r"[{}]", PH.sub("", t)) or re.search(r"==|&&|\|\||\$\(|\bgl_|\bvec[234]\b|::|->|;\s*$|^\s*#", t):
        return False
    if len(t) > 3 and re.fullmatch(r"[A-Z<>+\-0-9:,]*\d[A-Z<>+\-0-9:,]*", t):
        return False	# (time zone rules, chip names: "AEST-10", "DSI-2")
    if re.fullmatch(BUTTONS, t):
        return False	# (button names stay as printed)
    if re.match(r"\[\w[\w -]*\]", t) or (t.endswith("\n") and "\n" in t[:-1] + "x" and re.search(r"%\w", t)):
        return False	# ("[menu] ..." and printf lines: logs)
    if re.search(r">/dev/null|2>&1|\$\(|^(mkdir|env|rm|cp|mv|kill) |\w+_%s\[|[\w-]+/%s \(", t):
        return False	# (shell commands, config keys, user agents)
    if "=" in t and not " = " in t:
        return False
    if " " not in t:
        # one word: a capitalised word, or a word with a non-ASCII letter / CJK (a language's name); not a key
        if re.fullmatch(r"[A-Z][a-z]+(?:[-'][A-Za-z]+)*[.!?]?|[A-Z][a-z]+[A-Z][a-z]+\w*|[A-Z][a-z]+[A-Z]+[a-z]*|[A-Z]{2,}s|[A-Z0-9][A-Z0-9-]*[A-Z][A-Z0-9-]*\+?|\w*[^\x00-\x7f]\w*|[A-Z][a-z]*\d+\w*|\d+[A-Za-z]+|[A-Z][a-z]+\.\.\.|[A-Z][A-Za-z]+[-+/][A-Za-z0-9]+", t):
            if re.fullmatch(r"[A-Z0-9_]+", t) and "_" in t:
                return False
            return True
        return False
    if re.fullmatch(r"[a-z0-9_.:/-]+( [a-z0-9_.:/-]+)*", t) and re.search(r"\w[_/.:]\w|(^| )--?\w", t):
        return False
    if t.startswith(("-", "%s ", "--")) and re.fullmatch(r"[-\w %]+", t):
        return False
    return True


def norm(s):
    """for matching: placeholders as one token (two side by side as one), spaces collapsed"""
    s = re.sub(r"\s+", " ", PH.sub("\x01", s)).strip()
    return re.sub(r"\x01(\s*\x01)+", "\x01", s)


# ---- the sources -----------------------------------------------------------------------------------------------------
# (component dir, files, the part each file's lines default to, the Where they default to)
BL_UI = [
    ("ui/rnds/RndsUI.cpp", "menu", "Menu (Dark and Light themes)"),
    ("ui/src/carts.cpp", "menu", "Menu (Carts theme)"),
    ("ui/src/cart3d.cpp", "menu", "Menu (Carts theme)"),
    ("ui/src/library.cpp", "menu", "Menu: library, dates and times"),
    ("ui/src/layout.cpp", "menu", "Menu (layout themes)"),
    ("ui/src/main.cpp", "menu", "Menu"),
    ("ui/src/theme.cpp", "menu", "Menu"),
    ("ui/src/splash.cpp", "menu", "Menu"),
    ("ui/src/settings.cpp", "settings", "Settings"),
    ("ui/src/settings_defaults.cpp", "settings", "Settings"),
    ("ui/src/settings_files.cpp", "settings", "Files"),
    ("ui/src/settings_servers.cpp", "settings", "Files: servers, downloads and archives"),
    ("ui/src/settings_bshare.cpp", "settings", "Bshare"),
    ("ui/src/wifi.cpp", "settings", "Settings: Wi-Fi and the keyboard"),
    ("ui/src/bluetooth.cpp", "settings", "Settings: Bluetooth"),
    ("ui/src/scrape.cpp", "settings", "Box art download"),
    ("ui/src/pokemon.cpp", "settings", "Pokémon companion (second screen)"),
]
SD_FILES = [("src/menu.c", "In-game menu (L3)"), ("src/ui.c", "In-game menu (L3)"), ("src/ra.c", "RetroAchievements pop-ups"),
            ("src/resume.c", "Quick resume pop-ups"), ("src/shader.c", "In-game menu: shader names"),
            ("src/volume.c", "In-game menu (L3)"), ("src/lid.c", "In-game menu (L3)"), ("src/dsflip.c", "In-game pop-ups")]
BD_FILES = [("bananads/src/menu.c", "In-game menu (L3)"), ("bananads/src/ra.c", "RetroAchievements pop-ups"),
            ("bananads/src/frontend_kms.cpp", "In-game pop-ups"), ("bananads/src/main.cpp", "In-game pop-ups"),
            ("bananads/src/config.cpp", "In-game menu (L3)"), ("bananads/src/emu.cpp", "In-game pop-ups"),
            ("bananads/src/shader.c", "In-game menu: shader names"), ("bananads/src/cheats.cpp", "In-game menu: cheats"),
            ("bananads/src/screenshot.cpp", "In-game pop-ups"), ("bananads/src/wifi.cpp", "In-game pop-ups"),
            ("bananads/src/mic.cpp", "In-game pop-ups"), ("bananads/src/frontend_sdl.cpp", "In-game pop-ups")]


def theme_texts(path):
    """the words a layout theme writes itself: plain "text" values and quoted strings inside its expressions"""
    out = []
    for ln, l in enumerate(open(path, encoding="utf-8"), 1):
        for m in re.finditer(r'"text"\s*:\s*"((?:[^"\\]|\\.)*)"', l):
            v = m.group(1)
            if v.startswith("="):
                out += [(ln, q) for q in re.findall(r"'([^']*)'", v)]
            else:
                plain = re.sub(r"\{[^{}]*\}", "", v)
                if re.search(r"[A-Za-z]{2}", plain):
                    out.append((ln, v))
    return out


SHELL_NAMES = {"F": "file", "P": "package", "PART": "partition", "TO": "slot", "to": "slot", "PB": "package's handheld",
               "BOARD": "board", "TOTAL": "total", "PV": "version", "SIZE": "size", "LIMIT": "limit", "bad": "file", "f": "file"}


def shell_string(src, i):
    """the double-quoted shell string starting at src[i] == '"': (text with {placeholders}, end)"""
    out, i = [], i + 1
    while i < len(src) and src[i] != '"':
        c = src[i]
        if c == "\\" and i + 1 < len(src):
            out.append(src[i + 1]); i += 2; continue
        if src.startswith("$((", i):
            j = src.index("))", i); out.append("{n}"); i = j + 2; continue
        if src.startswith("$(", i):
            depth, j = 1, i + 2
            while j < len(src) and depth:
                if src[j] == "(": depth += 1
                elif src[j] == ")": depth -= 1
                elif src[j] == '"':
                    j = shell_string(src, j)[1] - 1
                j += 1
            cmd = src[i + 2:j - 1].split()
            name = {"net_error": "error", "field": cmd[1] if len(cmd) > 1 else "value", "basename": "file",
                    "hash_of": "hash", "size_of": "size", "date": "date"}.get(cmd[0] if cmd else "", "value")
            out.append("{%s}" % name); i = j; continue
        m = re.match(r"\$\{?([A-Za-z_]\w*|\d)\}?", src[i:])
        if m:
            v = m.group(1)
            out.append("{%s}" % ("server" if v == "2" else "value" if v.isdigit() else SHELL_NAMES.get(v, v.lower())))
            i += m.end(); continue
        out.append(c); i += 1
    return "".join(out), i + 1


def shell_messages(path):
    """the update script's messages the menu shows: status/fail/dfail/dl "..." and the network errors' echo lines"""
    src = open(path, encoding="utf-8").read()
    out = []
    for m in re.finditer(r'(?m)(?:\b(status|fail|dfail|dl)\s+|^\s*[\w|]+\)\s*echo\s+)(?=")', src):
        text, end = shell_string(src, m.end())
        out.append((src.count("\n", 0, m.start()) + 1, text))
    return out


def gather(args):
    found = []	# (part, where, text, src, func)
    bl, sd, bd = args.bl, args.sd, args.bd
    def add_c(root, rel, part, where, prefix):
        p = os.path.join(root, rel)
        if not os.path.exists(p):
            print("(no %s)" % p, file=sys.stderr); return
        for line, text, callee, fn in c_literals(p):
            if callee in NOT_SHOWN or re.fullmatch(r"(?:\w+_)?[lL]og(?:_?\w+)?|\w*Log|LOG\w*", callee or ""):
                continue
            if not looks_shown(text):
                continue
            found.append((part, where, text, "%s%s:%d" % (prefix, rel, line), "%s%s:%s" % (prefix, rel, fn)))
    if bl:
        for rel, part, where in BL_UI:
            add_c(bl, rel, part, where, "")
        tdir = os.path.join(bl, "system/rootfs/usr/share/banana/themes")
        for f in sorted(os.listdir(tdir)) if os.path.isdir(tdir) else []:
            if f.endswith(".json"):
                for ln, t in theme_texts(os.path.join(tdir, f)):
                    if looks_shown(t) or re.fullmatch(r"[A-Z][a-z]+", t):
                        found.append(("menu", "Menu (layout themes)", t, "themes/%s:%d" % (f, ln), "themes/%s" % f))
        sc = os.path.join(bl, "system/rootfs/etc/banana/systems.conf")
        for ln, l in enumerate(open(sc, encoding="utf-8"), 1):
            m = re.match(r"\s*(name|maker)\s*=\s*(.+?)\s*$", l)
            if m:
                found.append(("systems", "Menu: systems", m.group(2), "systems.conf:%d" % ln, "systems.conf"))
        up = os.path.join(bl, "system/rootfs/usr/libexec/banana/update")
        for ln, t in shell_messages(up):
            found.append(("settings", "Settings: updates (progress and errors)", t, "update:%d" % ln, "update"))
    if sd:
        for rel, where in SD_FILES:
            add_c(sd, rel, "superdrastic", where, "sd:")
    if bd:
        for rel, where in BD_FILES:
            add_c(bd, rel, "bananads", where, "bd:")
    return found


# Where a new line goes when no line of the last document came from the same function (file:function -> Where)
FUNC_WHERE = [
    (r"ui/src/settings.cpp:(poke\w*|statusColour)$", "Pokémon companion (second screen)"),
    (r"ui/src/pokemon.cpp:(statusName)$", "Pokémon companion (second screen)"),
    (r"ui/src/settings.cpp:gameRender$", "Second screen (RetroArch games)"),
    (r"themes/", "Menu (layout themes)"),
    (r"ui/src/library.cpp:hints$", "Menu: the button hints"),
    (r"bd:bananads/src/frontend_kms.cpp:shader_label$", "In-game menu: shader names"),
    (r"bd:bananads/src/frontend_kms.cpp:mh_\w+$", "In-game menu (L3)"),
    (r"ui/src/pokemon.cpp:\w+$", "Pokémon companion (second screen)"),
    (r"ui/src/settings.cpp:(renderBluetooth|btUpdate|btAct|btRow|bt\w*)", "Settings: Bluetooth"),
    (r"ui/src/bluetooth.cpp:", "Settings: Bluetooth"),
    (r"ui/src/settings.cpp:(renderColors|pressColors)", "Settings: screen colours"),
    (r"ui/src/settings.cpp:(renderGame|setPerGame|pressGame|openGame|renderArt|pressArt|scrapeShort|fetch|fetchFile)$",
     "A game's page (X on a game)"),
    (r"ui/src/settings.cpp:(renderUpdates|onlineRow|updateState|updatesWaiting|checkDone|startDownload|pollDownload|poll)$",
     "Settings: updates"),
    (r"ui/src/settings.cpp:(openSearch|renderSearch|search\w*|tileless)", "Search (SELECT in the menu)"),
    (r"ui/src/settings.cpp:(resetApply|change)$", "Settings: options and their help texts"),
    (r"ui/src/settings.cpp:build$", "Settings: options and their help texts"),
]
# Where values that are a context the code asks for (lang::tr(context, text)): kept exactly, they are the key
CONTEXT_WHERE = {"Pokémon types"}	# ("Second screen (RetroArch games)" too, but its lines are the last document's)
TYPES_RE = r"ui/src/pokemon.cpp:$"


def load_list(name):
    p = os.path.join(HERE, name)
    if not os.path.exists(p):
        return []
    return [l.rstrip("\n") for l in open(p, encoding="utf-8") if l.strip() and not l.startswith("#")]


def extract(args):
    """[{part, where, text, src: [...], fn: [...]}], one per (part, English) (placeholders compared as one token)"""
    found = gather(args)
    ign = load_list("lang-extract-ignore.txt")
    ign_text = {l.replace("\\n", "\n") for l in ign if not l.startswith("@")}
    ign_re = [re.compile(l[2:]) for l in ign if l.startswith("@~")]
    ign_where = {l[1:] for l in ign if l.startswith("@") and not l.startswith("@~")}	# @file:function
    kept = []
    for part, where, text, src, fn in found:
        if text in ign_text or fn in ign_where or any(r.fullmatch(text) for r in ign_re):
            continue
        for rx, w in FUNC_WHERE:
            if re.match(rx, fn):
                where = w
                fn = fn + "\x00fixed"
                break
        if re.match(TYPES_RE, fn) and re.fullmatch(r"[A-Z][a-z]+", text):
            where = "Pokémon types"
        kept.append((part, where, text, src, fn))
    for l in load_list("lang-extract-extra.txt"):
        p = l.split("\t")
        if len(p) >= 3:
            kept.append((p[0], p[1], p[2], "extra", "extra"))
    lines, by_norm = [], {}
    for part, where, text, src, fn in kept:
        k = (part, where if where in CONTEXT_WHERE else "", norm(text))	# (a line in its own context: a line of its own)
        if k not in by_norm:
            by_norm[k] = {"part": part, "where": where, "text": text, "src": [], "fn": []}
            lines.append(by_norm[k])
        by_norm[k]["src"].append(src)
        by_norm[k]["fn"].append(fn)
    return lines


def sources_text(args):
    """each part's sources as one text (literals as written), for finding a line the code builds from pieces"""
    out = collections.defaultdict(str)
    def add(part, path):
        if os.path.exists(path):
            out[part] += unescape(open(path, encoding="utf-8", errors="replace").read()) + "\n"
    if args.bl:
        for rel, part, where in BL_UI:
            add(part, os.path.join(args.bl, rel))
        add("settings", os.path.join(args.bl, "system/rootfs/usr/libexec/banana/update"))
        add("systems", os.path.join(args.bl, "system/rootfs/etc/banana/systems.conf"))
        out["ui"] = out.pop("menu", "") + out.pop("settings", "") + out.pop("systems", "")	# (banana-ui: one table)
    if args.sd:
        for rel, where in SD_FILES:
            add("superdrastic", os.path.join(args.sd, rel))
    if args.bd:
        for rel, where in BD_FILES:
            add("bananads", os.path.join(args.bd, rel))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bl", default=os.path.dirname(HERE))
    ap.add_argument("--sd")
    ap.add_argument("--bd")
    ap.add_argument("--out", default="-")
    a = ap.parse_args()
    lines = extract(a)
    f = sys.stdout if a.out == "-" else open(a.out, "w", encoding="utf-8")
    f.write("# lang-extract.py: part, where, English, sources\n")
    for l in lines:
        t = l["text"].replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")
        f.write("%s\t%s\t%s\t%s\n" % (l["part"], l["where"], t, " ".join(l["src"][:6])))
    print("%d lines" % len(lines), file=sys.stderr)


if __name__ == "__main__":
    main()
