#!/usr/bin/env python3
# A showcase video of BananaOS's menu, rendered by banana-ui's headless mode (its virtual clock: "wait" moves time on,
# "shot" draws the screens at that time), 30 frames a second, the two screens stacked as on the handheld, encoded with
# ffmpeg. tools/showcase/render.sh makes docs/showcase's videos with it.
#   make-video.py <name> <thor|rgds> <scene file> <output folder>
# scene lines: hold <seconds> | press <key> [<seconds after>] | raw <headless script command> | splash
# ({FX} in a line: the test fixture's folder, ui/tests/out/fixture). Environment: SYSTEMS, ROMS (the menu's systems.conf
# and games), TOPCARD ("title|line": a card in place of the top screen, for a game that plays there), and anything
# banana-ui reads (BANANA_ROCKNIX, BANANA_NOW...).
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
UI = REPO + '/ui'
FPS = 30
STEP = 1000.0 / FPS

name, shape, scenef, here = sys.argv[1:5]
os.makedirs(here, exist_ok=True)
work = os.environ.get('WORK') or here
frames = os.path.join(work, 'frames-' + name)
os.makedirs(frames, exist_ok=True)
for f in os.listdir(frames):
    os.remove(os.path.join(frames, f))

script = []
n = [0]
mode = ['shot']	# (gshot once the game panel is up: its own drawing)


def shot():
    script.append('wait %.3f' % STEP)
    script.append('%s f%05d' % (mode[0], n[0]))
    n[0] += 1


def hold(sec):
    for _ in range(max(1, int(round(sec * FPS)))):
        shot()


for ln in open(scenef):
    ln = ln.strip().replace('{FX}', UI + '/tests/out/fixture')
    if not ln or ln.startswith('#'):
        continue
    w = ln.split()
    if w[0] == 'hold':
        hold(float(w[1]))
    elif w[0] == 'press':
        script.append('press ' + w[1])
        hold(float(w[2]) if len(w) > 2 else 0.5)
    elif w[0] == 'raw':
        script.append(' '.join(w[1:]))
        if w[1] == 'gamepanel':
            mode[0] = 'gshot'
    elif w[0] == 'splash':
        # the boot animation, then its last frame held a moment
        t = 0.0
        while t <= 1390 + 400:
            script.append('splash f%05d %.0f 1' % (n[0], min(t, 1390)))
            n[0] += 1
            t += STEP
    else:
        sys.exit('unknown scene line: ' + ln)

data = os.path.join(work, 'data-' + name)
subprocess.run(['rm', '-rf', data], check=True)
subprocess.run(['cp', '-r', UI + '/tests/out/fixture/data', data], check=True)
with open(os.path.join(data, 'setup-done'), 'w') as f:
    f.write('setup 2\n')
args = [UI + '/banana-ui', '--headless', frames + '/', '--assets', UI + '/assets/rnds', '--splash', UI + '/assets/splash',
        '--banana', UI + '/assets/banana', '--systems', os.environ.get('SYSTEMS', REPO + '/system/rootfs/etc/banana/systems.conf'),
        '--roms', os.environ.get('ROMS', UI + '/tests/out/fixture/roms'), '--data', data, '--script', '; '.join(script)]
if shape == 'thor':
    args[3:3] = ['--size', '1440x1080', '--screens', '1920x1080,1240x1080']
else:
    args[3:3] = ['--size', '640x480']
with open(os.path.join(work, name + '.log'), 'w') as log:
    r = subprocess.run(args, stderr=log)
if r.returncode:
    sys.exit('banana-ui exited %d (see %s.log)' % (r.returncode, name))

# the two screens stacked, the bottom one centred under the top, on the case's dark grey; then H.264
out = os.path.join(here, name + '.mp4')
top = os.environ.get('TOPCARD')
if top:
    # the top screen is the game's while the panel shows (not the menu's): a card standing in for it
    font = UI + '/assets/rnds/fonts/PixelifySans-Medium.ttf'
    card = ('[0:v]drawbox=x=0:y=0:w=iw:h=ih:color=0x0b0a0d:t=fill,'
            "drawtext=fontfile=%s:text='%s':fontcolor=0xf1ede2:fontsize=72:x=(w-tw)/2:y=(h-th)/2-40,"
            "drawtext=fontfile=%s:text='%s':fontcolor=0xa39d8e:fontsize=40:x=(w-tw)/2:y=(h-th)/2+60[t];"
            % (font, top.split('|')[0], font, top.split('|')[1] if '|' in top else ''))
    src0 = '[t]'
else:
    card, src0 = '', '[0:v]'
if shape == 'thor':
    flt = (card + '[1:v]pad=1920:1080:(ow-iw)/2:0:color=0x15141a[b];' + src0 + '[b]vstack,pad=1920:2200:0:40:color=0x15141a,'
           'scale=960:-2:flags=lanczos,format=yuv420p')
else:
    flt = ('[0:v][1:v]vstack,pad=640:1000:0:20:color=0x15141a,scale=640:-2:flags=lanczos,format=yuv420p')
subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(FPS), '-i', frames + '/f%05d-top.png',
                '-framerate', str(FPS), '-i', frames + '/f%05d-bot.png', '-filter_complex', flt,
                '-c:v', 'libx264', '-preset', 'medium', '-crf', '20', '-movflags', '+faststart', out], check=True)
print('%s: %d frames, %.1f s' % (out, n[0], n[0] / FPS))
