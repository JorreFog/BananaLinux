#!/usr/bin/env python3
# ROCKNIX's systems for BananaOS's menu: its EmulationStation es_systems.cfg (every system ROCKNIX's build put in,
# with its folder, extensions and default emulator and core) as banana-ui's systems.conf. Each game starts the way
# EmulationStation starts it, through ROCKNIX's /usr/bin/runemu.sh with the system's default emulator and core, so
# ROCKNIX's emulator setup (its configs, its per-game settings, its Thor quirks) is used as it is. The ids are
# ROCKNIX's system names, which banana-ui's theme already knows (ui/assets/rnds/systems.cfg). The menu's own pages
# (PC games, the Store, Döda Kvarter, Bshare, Activity, the Store's apps, Files) are added after the systems.
#
#   es-systems-to-banana.py <es_systems.cfg> [systems.conf to write; default: stdout]
import sys
import xml.etree.ElementTree as ET

# (run-rocknix: runemu.sh with the menu's performance mode and the bottom screen around it)
RUN = '/usr/libexec/banana/run-rocknix {sys} %ROM% --core={core} --emulator={emu} --controllers=""'
# each system's performance mode (banana-perf's profile; a game's own in its options wins): the heavy 3D ones flat out,
# the DS steady (its emulators wait on every frame), the rest balanced
PERF = {'performance': {'psp', 'pspminis', 'ps2', 'gamecube', 'wii', 'wiiware', 'wiiu', '3ds', 'dreamcast', 'naomi',
                        'atomiswave', 'saturn', 'n64', 'n64dd', 'psvita', 'xbox', 'ps3', 'triforce', 'segamodel3', 'windows'},
        'steady': {'nds', 'ndsiware'}}
# systems whose shelf stays even with no games (where a fresh card's games go)
ALWAYS = {'nds', 'gba', 'gbc', 'gb', 'snes', 'nes', 'psx', 'n64', 'megadrive', 'genesis', 'ports'}
# the shelves' order: these first, as BananaOS's own list has them (the DS first), then the rest in es_systems.cfg's
# (alphabetical) order, so the menu doesn't open on ROCKNIX's first system (3DO, or an empty Game Boy shelf)
FIRST = ['nds', '3ds', 'gba', 'gbc', 'gb', 'nes', 'snes', 'megadrive', 'genesis', 'psx', 'n64', 'psp', 'ps2',
         'gamecube', 'wii', 'dreamcast', 'saturn', 'arcade', 'gameandwatch']
# ROCKNIX's system ids that mean something else to the menu: its "pc" is MS-DOS (DOSBox), while the menu's [pc] is the
# PC games hub (Windows games, Steam). Its shelf is "dos" here; its games still start as ROCKNIX's "pc" (run-rocknix
# pc ...) and rocknix-options takes "dos" for "pc". The hub's games are in roms/windows (ROCKNIX's "pc" folder is
# DOS's): their <Game>.pc entries and folders beside ROCKNIX's own Windows scripts (.sh, its Windows shelf), the
# folder start_banana.sh gives pc-games and steam (BANANA_PC_ROMS)
RENAME = {'pc': 'dos'}
OWN_PAGES = '''
[pc]
name = PC games
maker = Windows
folder = /storage/roms/windows
ext = pc
flat = yes
run = /usr/libexec/banana/run-pc %ROM%
perf = performance
always = yes
changed = /usr/libexec/banana/pc-games scan
covers = own

[rocknixdsstore]
name = Store
maker = ROCKNIXDS
folder = /usr/share/banana/apps/store
ext = app
run = /usr/libexec/banana/run-store %ROM%
perf = balanced

[dodakvarter]
name = Döda Kvarter
maker = ROCKNIXDS
folder = /usr/share/banana/apps/dodakvarter
ext = app
run = /usr/libexec/banana/run-dodakvarter %ROM%
perf = balanced

[bshare]
name = Bshare
maker = Banana
folder = /usr/share/banana/apps/bshare
ext = app
run = banana:bshare
always = yes

[activity]
name = Activity
maker = Banana
folder = /usr/share/banana/apps/activity
ext = app
run = /usr/libexec/banana/run-activity %ROM%
perf = balanced

[storeapps]
name = Apps
maker = Store
folder = /data/store/tiles
ext = sh
run = /usr/libexec/banana/run-store-app %ROM%
perf = balanced
flat = yes

[files]
name = Files
maker = BananaOS
folder = /usr/share/banana/apps/files
ext = app
run = banana:files
always = yes
flat = yes
'''


def text(el, tag):
    """the element's text, without EmulationStation's icon glyphs (its font's private-use characters: ROCKNIX's
    tools, ports and media systems have their maker as one, U+E013 " System")"""
    t = el.find(tag)
    s = (t.text or '') if t is not None else ''
    return ''.join(c for c in s if not 0xE000 <= ord(c) <= 0xF8FF).strip()


def default_emulator(system):
    """(emulator, core): the one marked default, else the first one listed."""
    first = None
    for emu in system.findall('./emulators/emulator'):
        for core in emu.findall('./cores/core'):
            pair = (emu.get('name', ''), (core.text or '').strip())
            if first is None:
                first = pair
            if core.get('default') == 'true':
                return pair
    return first or ('', '')


def convert(root):
    out = ['# made by es-systems-to-banana.py from ROCKNIX\'s es_systems.cfg (each game through runemu.sh)', '']
    systems = root.findall('system')
    systems.sort(key=lambda s: FIRST.index(text(s, 'name')) if text(s, 'name') in FIRST else len(FIRST))
    for s in systems:
        sid = text(s, 'name')
        if not sid or not all(c.isalnum() or c in '-_' for c in sid):
            continue
        cmd = text(s, 'command')
        # (EmulationStation's own viewers: a command that does nothing, its screenshots shown by ES itself)
        if cmd in ('/usr/bin/true', '/bin/true', 'true'):
            continue
        emu, core = default_emulator(s)
        if 'runemu.sh' in cmd:
            run = RUN.format(sys=sid, core=core, emu=emu)
        elif cmd:
            # (a system with its own command: tools, ports' scripts) as EmulationStation runs it
            run = cmd.replace('%SYSTEM%', sid).replace('%CORE%', core).replace('%EMULATOR%', emu) \
                .replace('%CONTROLLERSCONFIG%', '')
        else:
            continue
        exts = []
        for e in text(s, 'extension').replace(',', ' ').split():
            e = e.lstrip('.').lower()
            if e and e not in exts:
                exts.append(e)
        out.append('[%s]' % RENAME.get(sid, sid))
        out.append('name = %s' % (text(s, 'fullname') or sid))
        if text(s, 'manufacturer'):
            out.append('maker = %s' % text(s, 'manufacturer'))
        if text(s, 'path'):
            out.append('folder = %s' % text(s, 'path'))
        if exts:
            out.append('ext = %s' % ' '.join(exts))
        out.append('run = %s' % run)
        perf = next((m for m, ss in PERF.items() if sid in ss), 'balanced')
        out.append('perf = %s' % perf)
        if sid in ALWAYS:
            out.append('always = yes')
        out.append('')
    return '\n'.join(out) + OWN_PAGES


def main():
    if len(sys.argv) < 2:
        sys.exit('usage: es-systems-to-banana.py <es_systems.cfg> [systems.conf]')
    conf = convert(ET.parse(sys.argv[1]).getroot())
    if len(sys.argv) > 2:
        tmp = sys.argv[2] + '.new'
        with open(tmp, 'w') as f:
            f.write(conf)
        import os
        os.replace(tmp, sys.argv[2])
    else:
        sys.stdout.write(conf)


if __name__ == '__main__':
    main()
