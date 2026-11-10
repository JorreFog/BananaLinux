#!/usr/bin/env python3
# kmstable.py <log>...: one row per panel run: windows under 59 fps, frames lost, repeats, drops, short audio chunks,
# the race-start windows (the first dip after window 15 to the last under 60 before the steady race), clocks, temps
import re, sys, os
rows = []
for path in sys.argv[1:]:
    tag = os.path.basename(path).replace('.log', '')
    txt = open(path, errors='replace').read()
    head = txt.split('\n', 1)[0]
    env = re.search(r'env: (.*)$', head); env = env.group(1).strip() if env else ''
    temps = re.search(r'temp (\d+)-(\d+)C', txt)
    hp = re.search(r'(\d+) kB on huge pages', txt)
    wins, rep, drp = [], 0, 0
    for m in re.finditer(r'^\[kms\] 60 frames shown in [\d.]+ s \(([\d.]+)/s\).*?: (\d+) refreshes without a new frame, (\d+) dropped', txt, re.M):
        wins.append(float(m.group(1))); rep += int(m.group(2)); drp += int(m.group(3))
    clocks = [m.group(1) for m in re.finditer(r'^\[cpugov\] \d+ -> (\d+)', txt, re.M)]
    short = sum(int(m.group(1)) for m in re.finditer(r'^\[alsa\] 10 s: (\d+) short', txt, re.M))
    under = [w for w in wins if w < 59]
    lost = sum(60 - w for w in under)
    # the race start: windows 16..33 (the intro, countdown and first seconds), as the run script times it
    race = wins[16:33]
    worst = min(wins[16:]) if len(wins) > 16 else 0
    rows.append((tag, env, len(wins), len(under), round(lost), rep, drp, short, worst, ' '.join(clocks) or '-', temps.group(1)+'-'+temps.group(2) if temps else '?', 'yes' if hp and hp.group(1)=='32768' else ('part' if hp else '-'), ' '.join('%.0f' % w for w in race)))
print('| run | env | windows | under 59 | frames lost | repeats | drops | short audio chunks | worst window fps | cpugov steps (MHz) | SoC °C | huge pages | windows 17-33 (fps) |')
print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
for r in rows:
    print('| ' + ' | '.join(str(x) for x in r) + ' |')
