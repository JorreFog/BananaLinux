# the themes' screens as frame sequences (30 fps), top and bottom, JPEG, at the RG DS Plus's 1024x768
import subprocess, os, shutil, sys
from PIL import Image
ROOT=os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../..')); BASE=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'screens')
THEMES={
 'classic':(ROOT+'/docs/themes/classic.json',[(900,'press right'),(1700,'press right'),(2500,'press left'),(3300,'press left'),(4100,'press a'),(5100,'press right'),(5900,'press right'),(6700,'press left')]),
 'canvas':(ROOT+'/system/rootfs/usr/share/banana/themes/canvas.json',[(900,'press right'),(1700,'press right'),(2500,'press left'),(3200,'press left'),(4000,'press a'),(5000,'press down'),(5700,'press down'),(6400,'press up')]),
 'hillside':(ROOT+'/docs/themes/hillside.json',[(800,'press down'),(1500,'press down'),(2200,'press up'),(2900,'press up'),(3700,'press a'),(4800,'press right'),(5500,'press right'),(6200,'press down')]),
 'turntable':(ROOT+'/docs/themes/turntable.json',[(1600,'press down'),(2400,'press right'),(3400,'press left'),(4300,'press a'),(5600,'press down'),(6400,'press right')]),
}
T=7400; step=1000/30
for name,(theme,ev) in THEMES.items():
    if len(sys.argv) > 1 and name not in sys.argv[1:]: continue
    out=f'{BASE}/{name}'; shutil.rmtree(out,True); os.makedirs(out)
    if os.environ.get('NO_DATE') and '{date}' in open(theme).read():	# the clock without the date (a trailer must not show one)
        t2=out+'.theme.json'; open(t2,'w').write(open(theme).read().replace('{date}  ','').replace('{date}',''))
        theme=os.path.abspath(t2)
    cmds=['home 0']; t=0.0; last=0.0; i=0; n=0
    while t<=T:
        while i<len(ev) and ev[i][0]<=t: cmds.append(ev[i][1]); i+=1
        w=round(t)-round(last); last=t
        if w>0: cmds.append('wait %d'%w)
        cmds.append('shot f%04d'%n); n+=1; t+=step
    subprocess.run([ROOT+'/ui/banana-ui','--headless',out+'/','--size','1024x768','--assets',ROOT+'/ui/assets/rnds','--banana',ROOT+'/ui/assets/banana',
      '--systems',ROOT+'/system/rootfs/etc/banana/systems.conf','--roms','/tmp/fx/roms','--data','/tmp/fx/data','--theme',theme,
      '--script','; '.join(cmds)],capture_output=True)
    for k in range(n):
        for p in ('top','bot'):
            f=f'{out}/f{k:04d}-{p}.png'; Image.open(f).convert('RGB').save(f[:-4]+'.jpg',quality=90); os.remove(f)
    print(name,n,flush=True)
