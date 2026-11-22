#!/usr/bin/env python3
# Glass themes: the icons drawn here rather than by hand, in the Glass look (a glossy object, no tile: a gradient
# fill, a sheen over its top half, a hairline edge, a soft shadow). Drawn at 4x, saved at 192 x 192.
#   ereaderds.png  the reader (eReaderDS): an open book with a bookmark
#   ports.png      Ports (PortMaster): a cargo ship, "PM" on its hull
#   files.png      Files: an amber folder, a page in it
#   activity.png   Activity: your play time, green bars rising beside an orange clock
#
#   python3 tools/glass-theme/icons.py      (needs Pillow; writes system/rootfs/usr/share/banana/themes/glass/icons/)
import math, os, sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops, ImageFont

N = 768
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '../../system/rootfs/usr/share/banana/themes/glass/icons')
FONT = '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf'	# (macOS's; any rounded bold will do)

def bez(p0, p1, p2, n=40):
    return [((1-t)**2*p0[0]+2*(1-t)*t*p1[0]+t*t*p2[0], (1-t)**2*p0[1]+2*(1-t)*t*p1[1]+t*t*p2[1]) for t in [i/n for i in range(n+1)]]

def m_poly(pts):
    m = Image.new('L', (N, N), 0); ImageDraw.Draw(m).polygon(pts, fill=255); return m

def m_rrect(box, r):
    m = Image.new('L', (N, N), 0); ImageDraw.Draw(m).rounded_rectangle(box, r, fill=255); return m

def grad(top, bot, y0=0, y1=N):
    g = Image.new('RGBA', (N, N)); d = ImageDraw.Draw(g)
    for y in range(N):
        t = min(1, max(0, (y-y0)/max(1, y1-y0)))
        d.line([(0, y), (N, y)], fill=tuple(int(top[i]+(bot[i]-top[i])*t) for i in range(3))+(255,))
    return g

def layer(c, mask, top, bot, edge, sheen=0.5):
    bb = mask.getbbox()
    body = grad(top, bot, bb[1], bb[3]); body.putalpha(mask); c.alpha_composite(body)
    if sheen:	# white fading out over the upper half
        s = Image.new('RGBA', (N, N), (255, 255, 255, 0)); a = Image.new('L', (N, N), 0); d = ImageDraw.Draw(a)
        h = (bb[3]-bb[1])*0.55
        for y in range(int(bb[1]), int(bb[1]+h)): d.line([(0, y), (N, y)], fill=int(255*sheen*(1-(y-bb[1])/h)))
        s.putalpha(ImageChops.multiply(a, mask.filter(ImageFilter.MinFilter(9)))); c.alpha_composite(s)
    ring = ImageChops.subtract(mask, mask.filter(ImageFilter.MinFilter(7)))
    e = Image.new('RGBA', (N, N), edge+(0,)); e.putalpha(ring.point(lambda v: v*0.8)); c.alpha_composite(e)

def shadow(c, mask, dy=14, a=0.35, blur=18):
    s = Image.new('RGBA', (N, N), (0, 0, 0, 0)); m = Image.new('L', (N, N), 0); m.paste(mask, (0, dy))
    s.putalpha(m.filter(ImageFilter.GaussianBlur(blur)).point(lambda v: int(v*a))); c.alpha_composite(s)

def lines(c, xs, ys, w, color, th=14):
    d = ImageDraw.Draw(c)
    for (x, y, ww) in zip(xs, ys, w): d.rounded_rectangle((x, y, x+ww, y+th), th//2, fill=color)

BLUE = ((120, 190, 255), (20, 105, 230), (10, 60, 150))
ORANGE = ((255, 205, 110), (255, 140, 10), (170, 80, 0))
RED = ((255, 130, 120), (225, 45, 45), (140, 20, 20))
GREEN = ((150, 235, 140), (40, 170, 70), (20, 100, 40))
NAVY = ((90, 130, 210), (25, 55, 140), (10, 30, 80))
PAGE = ((255, 255, 255), (222, 230, 242), (130, 145, 170))
INK = (150, 175, 210, 255)

def reader():
    c = Image.new('RGBA', (N, N)); S = 384
    def page(ox, drop=0):
        return bez((S, 250+drop), ((S+ox)/2, 190+drop), (ox, 200+drop)) + bez((ox, 560+drop), ((S+ox)/2, 545+drop), (S, 610+drop))
    cov = ImageChops.lighter(m_poly(page(70, 34)), m_poly(page(698, 34)))
    shadow(c, cov); layer(c, cov, *BLUE, sheen=0.25)
    for o in (88, 680): layer(c, m_poly(page(o)), *PAGE, sheen=0)
    lines(c, [130]*4, [270, 320, 370, 420], [190, 210, 200, 150], INK)
    lines(c, [430]*4, [270, 320, 370, 420], [200, 190, 210, 120], INK)
    ImageDraw.Draw(c).line([(S, 250), (S, 610)], fill=(150, 165, 190, 255), width=6)
    r = m_poly([(560, 175), (612, 175), (612, 690), (586, 654), (560, 690)])
    shadow(c, r, 6, 0.3, 8); layer(c, r, *ORANGE, sheen=0.35)
    return c

def ports():
    c = Image.new('RGBA', (N, N))
    hull = m_poly([(90, 440), (690, 440), (620, 600), (150, 600)]); shadow(c, hull)
    # the containers on deck: a port's ship
    for b, col in [((150, 330, 290, 440), RED), ((300, 330, 440, 440), ORANGE), ((450, 330, 590, 440), GREEN),
                   ((225, 225, 365, 330), BLUE), ((375, 225, 515, 330), RED)]:
        m = m_rrect(b, 10); shadow(c, m, 4, 0.25, 6); layer(c, m, *col, sheen=0.35)
        d = ImageDraw.Draw(c)
        for x in range(b[0] + 26, b[2] - 10, 26): d.line([(x, b[1] + 14), (x, b[3] - 14)], fill=col[2] + (110,), width=4)
    layer(c, m_rrect((560, 250, 660, 440), 14), *PAGE, sheen=0.3)	# the bridge
    d = ImageDraw.Draw(c)
    for y in (280, 330): d.rounded_rectangle((578, y, 642, y + 26), 8, fill=(40, 90, 170, 255))
    layer(c, hull, *NAVY, sheen=0.4)
    t = Image.new('L', (N, N), 0); ImageDraw.Draw(t).text((384, 518), 'PM', font=ImageFont.truetype(FONT, 96), fill=255, anchor='mm')
    w = Image.new('RGBA', (N, N), (255, 255, 255, 0)); w.putalpha(t); c.alpha_composite(w)
    sea = m_poly([(x, 585 + 18 * math.sin(x / N * 8 * math.pi)) for x in range(60, N - 59, 8)] + [(N - 60, 695), (60, 695)])
    layer(c, ImageChops.multiply(sea, m_rrect((60, 560, N - 60, 700), 60)), (170, 225, 255), (60, 160, 240), (20, 100, 190), sheen=0.3)
    return c

AMBER_BACK = ((255, 200, 95), (225, 140, 20), (150, 85, 0))
AMBER = ((255, 228, 130), (255, 172, 35), (165, 95, 0))

def files():
    c = Image.new('RGBA', (N, N))
    back = ImageChops.lighter(m_rrect((90, 170, 678, 610), 44), m_rrect((90, 130, 340, 250), 40))
    shadow(c, back); layer(c, back, *AMBER_BACK, sheen=0.2)
    paper = m_rrect((140, 215, 628, 500), 22); shadow(c, paper, 6, 0.25, 8); layer(c, paper, *PAGE, sheen=0)
    lines(c, [190]*3, [260, 310, 360], [300, 360, 240], INK)
    front = m_rrect((90, 330, 678, 630), 44); shadow(c, front, 8, 0.3, 12); layer(c, front, *AMBER, sheen=0.45)
    return c

def activity():
    c = Image.new('RGBA', (N, N))
    # the bars: a week's play time, rising; the last one lit (today)
    for i, (h, col) in enumerate([(250, GREEN), (360, GREEN), (300, GREEN), (500, ORANGE)]):
        x = 105 + i * 145
        m = m_rrect((x, 670 - h, x + 120, 670), 36); shadow(c, m, 8, 0.3, 10); layer(c, m, *col, sheen=0.45)
    # the clock: how long, at the top left over the shorter bars
    face = Image.new('L', (N, N), 0); ImageDraw.Draw(face).ellipse((80, 80, 340, 340), fill=255)
    shadow(c, face); layer(c, face, *PAGE, sheen=0.3)
    d = ImageDraw.Draw(c)
    d.line([(210, 210), (210, 130)], fill=(40, 60, 90, 255), width=22)
    d.line([(210, 210), (270, 240)], fill=(40, 60, 90, 255), width=22)
    d.ellipse((192, 192, 228, 228), fill=(225, 60, 50, 255))
    return c

for name, draw in [('ereaderds', reader), ('ports', ports), ('files', files), ('activity', activity)]:
    if len(sys.argv) > 1 and name not in sys.argv[1:]: continue
    draw().resize((192, 192), Image.LANCZOS).save(os.path.join(OUT, name + '.png'))
    print('wrote', name + '.png')
