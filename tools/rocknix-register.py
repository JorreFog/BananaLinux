#!/usr/bin/env python3
# Banana's own emulators in a ROCKNIX checkout's lists, beside ROCKNIX's (tools/rocknix-banana.sh runs this):
# - each one's packages in the device's ADDITIONAL_PACKAGES (built into the image)
# - each one as an emulator and core of its system (virtual/emulators/package.mk's add_emu_core lines, which make
#   es_systems.cfg: the menu's shelves and the game page's Emulator row), never the default: ROCKNIX's stays so
# - each core's options in EmulationStation's es_features.cfg (the game page's rows: rewind, auto save...)
# Idempotent: a line already there is left as it is.
#
#   rocknix-register.py <ROCKNIX checkout> <device>
import os
import re
import sys

# (system, emulator, core, ROCKNIX package, es_features: a RetroArch core's shared features; a standalone one's own
# <feature>s, which its start script reads with get_setting)
BANANADS_FEATURES = '''<feature name="internal resolution">
            <choice name="1x (256x192)" value="1"/>
            <choice name="2x (512x384)" value="2"/>
            <choice name="3x (768x576)" value="3"/>
            <choice name="4x (1024x768)" value="4"/>
          </feature>'''
EMULATORS = [
    ('gba', 'retroarch', 'bananagba', 'bananagba-lr', 'netplay, rewind, autosave, cheevos'),
    ('nes', 'retroarch', 'banananes', 'banananes-lr', 'netplay, rewind, autosave, cheevos'),
    ('famicom', 'retroarch', 'banananes', 'banananes-lr', 'netplay, rewind, autosave, cheevos'),
    ('snes', 'retroarch', 'bananasnes', 'bananasnes-lr', 'netplay, rewind, autosave, cheevos'),
    ('sfc', 'retroarch', 'bananasnes', 'bananasnes-lr', 'netplay, rewind, autosave, cheevos'),
    ('nds', 'bananads', 'bananads-sa', 'bananads-sa', BANANADS_FEATURES),
    ('gameandwatch', 'mamegnw', 'mamegnw-sa', 'mame-gnw', None),
]
# BananaFront for the systems BananaOS runs it for: the core's name is bananafront-<libretro core> (runemu.sh starts
# start_<core up to its last hyphen>.sh: start_bananafront.sh, which loads that libretro core)
for _sys, _core in [('gb', 'gambatte'), ('gbc', 'gambatte'), ('gba', 'bananagba'), ('gba', 'mgba'), ('nes', 'banananes'),
                    ('snes', 'bananasnes'), ('megadrive', 'picodrive'), ('genesis', 'picodrive'),
                    ('mastersystem', 'picodrive'), ('gamegear', 'picodrive'), ('psx', 'pcsx_rearmed')]:
    EMULATORS.append((_sys, 'bananafront', 'bananafront-' + _core, 'bananafront-sa', None))
# packages with no emulator line of their own (apps, the frontend)
PACKAGES = ['banana-frontend', 'banana-apps']


def edit(path, fn):
    with open(path) as f:
        s = f.read()
    t = fn(s)
    if t != s:
        with open(path, 'w') as f:
            f.write(t)


def main(root, dev):
    base = os.path.join(root, 'projects/ROCKNIX')
    pkgs = PACKAGES + sorted({e[3] for e in EMULATORS})
    # the image's packages (only those this checkout has: a package not copied in yet is left out)
    have = set()
    for dp, dn, fn in os.walk(os.path.join(base, 'packages')):
        if 'package.mk' in fn:
            have.add(os.path.basename(dp))
    want = [p for p in pkgs if p in have]

    def add_packages(s):
        m = re.search(r'^([ \t]*ADDITIONAL_PACKAGES=")([^"]*)"', s, re.M)
        if not m:
            sys.exit('no ADDITIONAL_PACKAGES in the device options')
        cur = m.group(2).split()
        new = [p for p in want if p not in cur]
        if not new:
            return s
        return s[:m.start(2)] + ' '.join(new + cur) + s[m.end(2):]
    edit(os.path.join(base, 'devices', dev, 'options'), add_packages)

    # add_emu_core lines: just before the system's add_es_system (after its per-device branches: on every device),
    # never default themselves, in this list's order
    emus = os.path.join(base, 'packages/virtual/emulators/package.mk')

    def add_cores(s):
        for system, emu, core, pkg, _ in EMULATORS:
            if pkg not in have:
                continue
            line = 'add_emu_core %s %s %s false' % (system, emu, core)
            if re.search(r'^\s*' + re.escape(line) + r'\s*(#.*)?$', s, re.M):
                continue
            m = re.search(r'^([ \t]*)add_es_system %s\s*$' % re.escape(system), s, re.M)
            if not m:
                continue
            s = s[:m.start()] + m.group(1) + line + '  # (BananaOS)\n' + s[m.start():]
        return s
    edit(emus, add_cores)

    # es_features.cfg: a RetroArch core inside retroarch's <cores>; a standalone one as an emulator of its own
    feats = os.path.join(base, 'packages/ui/emulationstation/config/common/es_features.cfg')

    def add_features(s):
        for system, emu, core, pkg, shared in EMULATORS:
            if pkg not in have:
                continue
            if emu == 'retroarch':
                if re.search(r'<core name="%s"' % re.escape(core), s):
                    continue
                m = re.search(r'<emulator name="retroarch"[^>]*>\s*<cores>\n', s)
                if m:
                    s = s[:m.end()] + '      <core name="%s" features="%s" />\n' % (core, shared) + s[m.end():]
            else:
                inner = '>\n        <features>\n          %s\n        </features>\n      </core>' % shared if shared else '/>'
                block = '      <core name="%s"%s\n' % (core, inner)
                m = re.search(r'<emulator name="%s"[^>]*>\s*<cores>\n' % re.escape(emu), s)
                if m:
                    # the emulator is there: this core in its <cores> (if it isn't yet)
                    end = s.find('</emulator>', m.end())
                    if not re.search(r'<core name="%s"' % re.escape(core), s[m.end():end]):
                        s = s[:m.end()] + block + s[m.end():]
                else:
                    m = s.rfind('</features>')
                    if m >= 0:
                        s = s[:m] + '  <emulator name="%s">\n    <cores>\n%s    </cores>\n  </emulator>\n' % (emu, block) + s[m:]
        return s
    if os.path.exists(feats):
        edit(feats, add_features)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit('usage: rocknix-register.py <ROCKNIX checkout> <device>')
    main(sys.argv[1], sys.argv[2])
