#!/usr/bin/env python3
# Plaza theme: RetroArch's monochrome system shapes (libretro/retroarch-assets, src/xmb/monochrome, CC BY 4.0), one
# per Banana system that RetroArch has, cleaned to plain white shapes (no styles, metadata or namespaces) in ra/<id>.svg.
# make.py draws them on Plaza's tiles. Run again only to fetch them anew (needs the network).
import os, re, sys, urllib.parse, urllib.request
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 'https://raw.githubusercontent.com/libretro/retroarch-assets/master/src/xmb/monochrome/'
# Banana's system id -> RetroArch's file name
RA = {
    'nds': 'Nintendo - Nintendo DS', 'gba': 'Nintendo - Game Boy Advance', 'gbc': 'Nintendo - Game Boy Color',
    'gb': 'Nintendo - Game Boy', 'nes': 'Nintendo - Nintendo Entertainment System',
    'fds': 'Nintendo - Family Computer Disk System', 'snes': 'Nintendo - Super Nintendo Entertainment System',
    'n64': 'Nintendo - Nintendo 64', 'virtualboy': 'Nintendo - Virtual Boy', 'pokemini': 'Nintendo - Pokemon Mini',
    'gameandwatch': 'Nintendo - Game & Watch', 'megadrive': 'Sega - Mega Drive - Genesis',
    'mastersystem': 'Sega - Master System - Mark III', 'gamegear': 'Sega - Game Gear', 'sg-1000': 'Sega - SG-1000',
    'megacd': 'Sega - Mega-CD - Sega CD', 'sega32x': 'Sega - 32X', 'psx': 'Sony - PlayStation',
    'psp': 'Sony - PlayStation Portable', 'c64': 'Commodore - 64', 'zxspectrum': 'Sinclair - ZX Spectrum',
    'ngp': 'SNK - Neo Geo Pocket', 'ngpc': 'SNK - Neo Geo Pocket Color', 'neogeo': 'SNK - Neo Geo',
    'wonderswan': 'Bandai - WonderSwan', 'wonderswancolor': 'Bandai - WonderSwan Color', 'atarilynx': 'Atari - Lynx',
    'atari2600': 'Atari - 2600', 'atari7800': 'Atari - 7800', 'pcengine': 'NEC - PC Engine - TurboGrafx 16',
    'pcenginecd': 'NEC - PC Engine CD - TurboGrafx-CD', 'colecovision': 'Coleco - ColecoVision',
    'intellivision': 'Mattel - Intellivision', 'vectrex': 'GCE - Vectrex', 'odyssey2': 'Magnavox - Odyssey2',
    'arcade': 'Arcade Controller', 'cps1': 'Capcom - CP System I', 'cps2': 'Capcom - CP System II',
    'cps3': 'Capcom - CP System III', 'pico-8': 'PICO-8',
    'pad': 'The 3DO Company - 3DO',     # the generic pad: a system RetroArch has no shape for
}
SVG = 'http://www.w3.org/2000/svg'
KEEP = {'svg', 'g', 'path', 'polygon', 'rect', 'circle', 'ellipse', 'polyline', 'line'}
ATTR = {'d', 'points', 'x', 'y', 'width', 'height', 'rx', 'ry', 'cx', 'cy', 'r', 'x1', 'y1', 'x2', 'y2', 'transform',
        'fill-rule', 'clip-rule', 'opacity'}
ET.register_namespace('', SVG)


def clean(data):
    root = ET.fromstring(data)
    vb = root.get('viewBox')
    if not vb:
        w = float(re.sub('[a-z]', '', root.get('width', '64'))); h = float(re.sub('[a-z]', '', root.get('height', '64')))
        vb = '0 0 %g %g' % (w, h)

    def walk(el):
        out = ET.Element('{%s}%s' % (SVG, el.tag.split('}')[-1]))
        for k, v in el.attrib.items():
            if '}' not in k and k in ATTR: out.set(k, v)
        st = el.get('style', '')
        m = re.search(r'fill-rule:\s*(\w+)', st)
        if m: out.set('fill-rule', m.group(1))
        if re.search(r'fill:\s*none', st) or el.get('fill') == 'none': out.set('fill', 'none')
        for c in el:
            if c.tag.split('}')[-1] in KEEP: out.append(walk(c))
        return out
    new = walk(root)
    new.attrib.clear()
    new.set('viewBox', vb); new.set('fill', '#ffffff')
    return ET.tostring(new, encoding='unicode')


for sid, name in sorted(RA.items()):
    url = BASE + urllib.parse.quote(name + '.svg')
    try:
        data = urllib.request.urlopen(url, timeout=30).read()
    except Exception as e:
        print('%s: %s' % (sid, e), file=sys.stderr); continue
    open(os.path.join(HERE, 'ra', sid + '.svg'), 'w').write(clean(data) + '\n')
    print(sid)
