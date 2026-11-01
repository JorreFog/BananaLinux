#!/usr/bin/env python3
"""BananaNES stress ROM data generator: CHR (tiles/sprites), DPCM samples, song, maps, tables -> data.inc, chr.bin, dpcm.bin.
All data is procedural (own code, GPL-3.0-or-later). Deterministic: fixed seeds."""
import math, random, sys, struct

R = random.Random(20261101)

# ---------------------------------------------------------------- tiles
def blank(): return [[0]*8 for _ in range(8)]
def enc(t):
    lo = bytearray(8); hi = bytearray(8)
    for y in range(8):
        a = b = 0
        for x in range(8):
            v = t[y][x]; a = (a << 1) | (v & 1); b = (b << 1) | (v >> 1)
        lo[y] = a; hi[y] = b
    return bytes(lo + hi)
def solid(c): return [[c]*8 for _ in range(8)]
def tex(rng, w):  # weighted noise texture, w = [w0,w1,w2,w3]
    t = blank()
    for y in range(8):
        for x in range(8):
            t[y][x] = rng.choices([0, 1, 2, 3], w)[0]
    return t

FONT3 = {
 '0':"111101101101111",'1':"010110010010111",'2':"111001111100111",'3':"111001111001111",'4':"101101111001001",
 '5':"111100111001111",'6':"111100111101111",'7':"111001001010010",'8':"111101111101111",'9':"111101111001111",
 'A':"111101111101101",'B':"110101110101110",'C':"111100100100111",'D':"110101101101110",'E':"111100110100111",
 'F':"111100110100100",'G':"111100101101111",'H':"101101111101101",'I':"111010010010111",'J':"001001001101111",
 'K':"101101110101101",'L':"100100100100111",'M':"101111111101101",'N':"111101101101101",'O':"111101101101111",
 'P':"111101111100100",'Q':"111101101111001",'R':"111101110101101",'S':"111100111001111",'T':"111010010010010",
 'U':"101101101101111",'V':"101101101101010",'W':"101101111111101",'X':"101101010101101",'Y':"101101010010010",
 'Z':"111001010100111",'-':"000000111000000",':':"000010000010000",'.':"000000000000010",'!':"010010010000010",
 '?':"111001011000010",'/':"001001010100100",'=':"000111000111000",'+':"000010111010000",'%':"101001010100101",
 '*':"101010111010101",'<':"001010100010001",'>':"100010001010100",'\'':"010010000000000",',':"000000000010100",
}
def glyph(ch, fg=3, bg=0):
    t = [[bg]*8 for _ in range(8)]
    if ch in FONT3:
        s = FONT3[ch]
        for r in range(5):
            for c in range(3):
                if s[r*3+c] == '1':
                    # 2x1 scaled horizontally to be legible: columns 1..6 -> each glyph px 2 wide
                    for k in range(2): t[1+r][1+c*2+k] = fg
                    if r < 4: pass
    return t

def terrain_set(seed, pal):
    """128 tiles: 0 sky,1 top,2 fill,3..10 deco, 11 water, 12.. misc textures."""
    rng = random.Random(seed)
    tiles = []
    tiles.append(solid(0))                                   # 0 sky
    top = tex(rng, [0, 1, 6, 1])
    for y in range(3): top[y] = [1 if rng.random() < .55 else 3 for _ in range(8)]
    tiles.append(top)                                        # 1 top
    tiles.append(tex(rng, [2, 3, 3, 1]))                     # 2 fill
    for i in range(8):                                       # 3..10 deco
        t = blank()
        kind = i % 4
        for y in range(8):
            for x in range(8):
                if kind == 0: t[y][x] = 2 if (y % 4 == 0 or (x + (y // 4) * 4) % 8 == 0) else 3   # brick
                elif kind == 1: t[y][x] = 1 + ((x ^ y) & 1) if 1 <= x <= 6 and 1 <= y <= 6 else 3 # block
                elif kind == 2: t[y][x] = 2 if (x < 2 or x > 5) else 1                           # pipe
                else: t[y][x] = rng.choice([0, 0, 1, 2]) if y > 2 else 0                         # bush
        tiles.append(t)
    tiles.append(tex(rng, [1, 1, 5, 3]))                     # 11 water
    while len(tiles) < 128: tiles.append(tex(rng, [rng.randint(1, 4) for _ in range(4)]))
    return b''.join(enc(t) for t in tiles)

def para_set(seed, kind):
    """128 tiles: 0..15 silhouette slices (periodic across 16 tiles), 16 fill, rest texture."""
    rng = random.Random(seed)
    n = 128; prof = []
    h = rng.randint(2, 6)
    for i in range(n):
        h += rng.choice([-1, 0, 1]) * (1 if kind != 0 else 0)
        h = max(1, min(7, h)); prof.append(h)
    # make periodic: blend the last 8 columns back to the first value
    for i in range(8): prof[n-8+i] = round(prof[n-8+i] + (prof[0] - prof[n-8+i]) * (i + 1) / 8)
    tiles = []
    for tix in range(16):
        t = blank()
        for x in range(8):
            hh = prof[(tix * 8 + x) % n]
            for y in range(8):
                if kind == 0:   # sky + dotted clouds
                    t[y][x] = 1 if ((x * 7 + y * 13 + tix * 5) % 11 == 0) else (2 if (y > 4 and (x + tix) % 4 < 2 and tix % 3 == 0) else 0)
                else:
                    t[y][x] = 0 if y < 8 - hh else (1 if y == 8 - hh else (2 if (x + y) % 3 else 3))
        tiles.append(t)
    tiles.append(solid(2 if kind else 0))
    while len(tiles) < 128: tiles.append(tex(rng, [1, 2, 2, 1]))
    return b''.join(enc(t) for t in tiles)

def common_set():
    tiles = []
    for c in range(32, 96): tiles.append(glyph(chr(c)))       # 128..191 font (ASCII-32)
    solidb = solid(3); tiles.append(solidb)                    # 192 HUD border (opaque, sprite-0 target)
    box = blank()
    for i in range(8): box[0][i] = 3; box[7][i] = 3; box[i][0] = 3; box[i][7] = 3
    tiles.append(box)                                          # 193 box
    coin = blank()
    for y in range(8):
        for x in range(8):
            if (x - 3.5) ** 2 + (y - 3.5) ** 2 < 11: coin[y][x] = 1 + ((x + y) & 1)
    tiles.append(coin)                                         # 194 coin
    for i in range(195, 256): tiles.append(solid(0))
    return b''.join(enc(t) for t in tiles[:128])               # 128 tiles; 256 won't fit 2KB, trimmed

def sprite_bank(seed):
    """1 KiB = 64 tiles = 16 'creatures' of 16x16 (4 tiles: LT, LB, RT, RB)."""
    rng = random.Random(seed)
    out = bytearray()
    base = [[0]*16 for _ in range(16)]
    for y in range(2, 15):
        for x in range(2, 8):
            if rng.random() < .62 - abs(y - 8) * .03: base[y][x] = rng.choice([1, 2, 2, 3])
    for f in range(16):
        g = [row[:] for row in base]
        for y in range(16):
            for x in range(8, 16): g[y][x] = 0
        for y in range(11, 16):                     # leg animation
            for x in range(2, 8):
                if (f & 3) == 1 and x < 4: g[y][x] = 0
                if (f & 3) == 3 and x > 5: g[y][x] = 0
        for y in range(16):                         # mirror
            for x in range(8): g[y][15 - x] = g[y][x]
        if f & 4: g[3][5] = 3; g[3][10] = 3
        cells = {'LT': (0, 0), 'LB': (0, 8), 'RT': (8, 0), 'RB': (8, 8)}
        for k in ('LT', 'LB', 'RT', 'RB'):
            ox, oy = cells[k]
            t = [[g[oy + y][ox + x] for x in range(8)] for y in range(8)]
            out += enc(t)
    out[62*16:64*16] = enc(solid(3)) * 2     # tiles 62/63: solid (sprite 0 target, 8x8 and 8x16)
    return bytes(out)

# ---------------------------------------------------------------- CHR layout (64 x 1 KiB)
chr_ = bytearray()
chr_ += terrain_set(1, 0)      # banks 0-1  plat
chr_ += terrain_set(2, 1)      # banks 2-3  shoot
chr_ += terrain_set(3, 2)      # banks 4-5  rpg
chr_ += terrain_set(4, 3)      # banks 6-7  fight
chr_ += common_set()           # banks 8-9  font/HUD
for i, kind in enumerate([0, 1, 2, 1, 2, 1]): chr_ += para_set(10 + i, kind)   # banks 10-21 parallax sets (6 x 2 KiB)
assert len(chr_) == 22 * 1024, len(chr_)
nsb = 64 - 22
for i in range(nsb): chr_ += sprite_bank(100 + i)       # banks 22..63 sprite banks
assert len(chr_) == 65536
open('chr.bin', 'wb').write(chr_)

# ---------------------------------------------------------------- DPCM
RATES = [428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54]
def dm_encode(pcm, nbytes):
    ctr = 64; out = bytearray(); bits = []
    for i in range(nbytes * 8):
        tgt = pcm[i] if i < len(pcm) else 64
        if tgt > ctr: bits.append(1); ctr = min(126, ctr + 2)
        else: bits.append(0); ctr = max(0, ctr - 2)
    for i in range(nbytes):
        v = 0
        for b in range(8): v |= bits[i*8+b] << b
        out.append(v)
    return bytes(out)
def sample(kind, rate_ix, length_units):
    n = length_units * 16 + 1; rate = 1789773 / RATES[rate_ix]; pcm = []; rng = random.Random(7 + kind)
    for i in range(n * 8):
        t = i / rate
        if kind == 0:   v = math.sin(2*math.pi*(55*t + 220*(1-math.exp(-t*40))/40)) * math.exp(-t*18)       # kick
        elif kind == 1: v = (rng.random()*2-1) * math.exp(-t*22) * .8 + math.sin(2*math.pi*190*t) * math.exp(-t*30) * .3  # snare
        elif kind == 2: v = (rng.random()*2-1) * math.exp(-t*5) * .7                                          # crash
        else:           v = (math.sin(2*math.pi*(140+30*math.sin(t*9))*t) + .5*math.sin(2*math.pi*(280)*t)) * .4 * (1 if (t*6) % 1 < .8 else 0)  # voice-like
        pcm.append(max(2, min(125, 64 + v * 56)))
    return rate_ix, length_units, dm_encode(pcm, n)
dp = bytearray(); dpcm_meta = []
for kind, (rix, lu) in enumerate([(8, 24), (9, 24), (11, 63), (10, 63)]):
    while len(dp) % 64: dp.append(0x55)
    rix, lu, data = sample(kind, rix, lu)
    dpcm_meta.append((rix, len(dp) // 64, lu)); dp += data
assert len(dp) <= 4096, len(dp)
dp += bytes([0x55]) * (4096 - len(dp))
open('dpcm.bin', 'wb').write(dp)

# ---------------------------------------------------------------- period tables / sine
def pulse_period(n):
    f = 440 * 2 ** ((n - 69) / 12); return max(8, min(2047, round(1789773 / (16 * f) - 1)))
def tri_period(n):
    f = 440 * 2 ** ((n - 69) / 12); return max(2, min(2047, round(1789773 / (32 * f) - 1)))
NOTES = 73   # note byte 1..73  = midi 24..96
pp = [pulse_period(24 + i) for i in range(NOTES)]; tp = [tri_period(24 + i) for i in range(NOTES)]

# ---------------------------------------------------------------- song (5 bytes/row, 256 rows)
scale = [0, 2, 3, 5, 7, 8, 10]       # A natural minor
chords = [[0, 3, 7], [-4, 0, 3], [3, 7, 10], [-2, 2, 5]]
song = []
for row in range(256):
    bar = (row // 16) % 4; step = row % 16; root = 57 + chords[bar][0]
    ch = chords[bar]
    r = random.Random(900 + row // 16)
    mel = 0
    if step % 2 == 0 or (row % 5 == 0):
        deg = r.choice(scale) + 12 * r.choice([0, 1, 1]); mel = 69 + deg - 24 + 1 if r.random() < .8 else 0
    arp = 57 + chords[bar][step % 3] + (12 if step % 4 == 3 else 0) - 24 + 1 if step % 2 == 1 or row % 3 == 0 else 0
    tri = (45 + chords[bar][0] - 24 + 1) if step in (0, 3, 6, 8, 11, 14) else (0xFF if step == 15 else 0)
    noise = 0
    if step % 4 == 2: noise = 2
    elif step % 2 == 0: noise = 1
    if step == 0: noise = 3
    dm = 0
    if step in (0, 8): dm = 1
    elif step == 4 or step == 12: dm = 2
    elif step == 15 and bar == 3: dm = 3
    elif step == 6 and bar % 2: dm = 4
    song.append((mel, arp, tri, noise, dm))

# ---------------------------------------------------------------- maps: 1024 columns x (height, deco)
mapcols = []
h = 22
r = random.Random(555)
for c in range(1024):
    if c % 7 == 0: h = max(17, min(26, h + r.choice([-1, 0, 0, 1])))
    d = 0
    if r.random() < .22: d = r.randint(1, 7)
    mapcols.append((h, d))
# sine
sine = [round(127 * math.sin(2 * math.pi * i / 256)) & 255 for i in range(256)]

# ---------------------------------------------------------------- emit data.inc
def bytes_lines(label, data, per=16):
    s = f"{label}:\n"
    for i in range(0, len(data), per): s += "  .byte " + ",".join(str(b & 255) for b in data[i:i+per]) + "\n"
    return s
with open('data.inc', 'w') as f:
    f.write("; generated by gen.py - do not edit\n")
    f.write('.segment "RODATA"\n')
    f.write(bytes_lines("pulse_lo", [p & 255 for p in pp]) + bytes_lines("pulse_hi", [p >> 8 for p in pp]))
    f.write(bytes_lines("tri_lo", [p & 255 for p in tp]) + bytes_lines("tri_hi", [p >> 8 for p in tp]))
    f.write(bytes_lines("sine", sine))
    flat = [b for row in song for b in row]
    f.write(bytes_lines("song", flat, 5))
    f.write(bytes_lines("dpcm_rate", [m[0] for m in dpcm_meta]) + bytes_lines("dpcm_addr", [m[1] for m in dpcm_meta]) + bytes_lines("dpcm_len", [m[2] for m in dpcm_meta]))
    # relative-to-ground tiles (8 per deco) ; 0=sky 3..10 deco
    rel = [[0]*8, [10,0,0,0,0,0,0,0], [7,7,8,8,9,0,0,0], [0,0,0,3,0,0,0,0], [0,0,0,4,0,0,0,0], [10,10,0,0,0,0,0,0], [5,0,0,0,0,0,0,0], [6,6,6,0,0,0,0,0]]
    f.write(bytes_lines("rel_tile", [t for row in rel for t in row], 8))
    attr = [[1,0,0,0,0,0,0,0],[1,0,0,0,0,0,2,3],[1,0,0,0,2,2,2,3],[1,0,0,0,0,0,2,3],[1,0,0,0,0,0,0,3],[1,0,0,0,2,2,2,3],[1,0,0,0,0,0,0,3],[1,0,0,0,0,0,2,3]]
    f.write(bytes_lines("attr_tpl", [a * 0x55 for row in attr for a in row], 8))
    f.write('.segment "RODATA"\n')
    for b in range(4):
        f.write(f'.segment "MAP{b}"\n')
        f.write(bytes_lines(f"map{b}", [v for c in mapcols[b*256:(b+1)*256] for v in c], 16))
    f.write('.segment "RODATA"\n')
    # palettes: 4 BG + 4 SPR, per scene
    NES = dict(sky=0x21, wht=0x30, gry=0x10, grn=0x1A, dgr=0x0A, brn=0x17, red=0x16, blu=0x12, yel=0x28, blk=0x0F, pur=0x14, org=0x27, cyan=0x2C, dblu=0x02)
    def pal(sets): return [c for s in sets for c in s]
    P = NES
    f.write(bytes_lines("pal_plat", pal([[P['sky'],P['grn'],P['brn'],P['wht']],[P['sky'],P['wht'],P['gry'],P['blk']],[P['sky'],P['grn'],P['dgr'],P['wht']],[P['sky'],P['brn'],P['org'],P['yel']],
                                          [P['sky'],P['red'],P['wht'],P['brn']],[P['sky'],P['blu'],P['wht'],P['cyan']],[P['sky'],P['yel'],P['org'],P['brn']],[P['sky'],P['pur'],P['wht'],P['blk']]])))
    f.write(bytes_lines("pal_shoot", pal([[P['blk'],P['dblu'],P['blu'],P['wht']],[P['blk'],P['wht'],P['gry'],P['blu']],[P['blk'],P['gry'],P['blu'],P['wht']],[P['blk'],P['red'],P['org'],P['yel']],
                                           [P['blk'],P['red'],P['wht'],P['yel']],[P['blk'],P['cyan'],P['wht'],P['blu']],[P['blk'],P['grn'],P['wht'],P['yel']],[P['blk'],P['pur'],P['wht'],P['org']]])))
    f.write(bytes_lines("pal_rpg", pal([[P['grn'],P['dgr'],P['brn'],P['wht']],[P['blk'],P['wht'],P['gry'],P['blk']],[P['grn'],P['blu'],P['cyan'],P['wht']],[P['grn'],P['brn'],P['org'],P['yel']],
                                         [P['grn'],P['red'],P['wht'],P['brn']],[P['grn'],P['blu'],P['wht'],P['cyan']],[P['grn'],P['yel'],P['org'],P['brn']],[P['grn'],P['pur'],P['wht'],P['blk']]])))
    f.write(bytes_lines("pal_para", pal([[P['sky'],P['wht'],P['cyan'],P['blu']],[P['sky'],P['gry'],P['blu'],P['dblu']],[P['sky'],P['grn'],P['dgr'],P['blk']],[P['sky'],P['brn'],P['org'],P['yel']],
                                          [P['sky'],P['red'],P['wht'],P['brn']],[P['sky'],P['blu'],P['wht'],P['cyan']],[P['sky'],P['yel'],P['org'],P['brn']],[P['sky'],P['pur'],P['wht'],P['blk']]])))
    f.write(bytes_lines("pal_fight", pal([[P['dblu'],P['pur'],P['gry'],P['wht']],[P['blk'],P['wht'],P['gry'],P['blk']],[P['dblu'],P['red'],P['org'],P['yel']],[P['dblu'],P['brn'],P['org'],P['yel']],
                                           [P['dblu'],P['red'],P['wht'],P['brn']],[P['dblu'],P['cyan'],P['wht'],P['blu']],[P['dblu'],P['yel'],P['org'],P['brn']],[P['dblu'],P['grn'],P['wht'],P['blk']]])))
    # the 6 parallax bands: CHR 2 KiB bank (R0 value), scanline length, speed shift
    f.write(bytes_lines("para_chr", [10, 12, 14, 16, 18, 20]))
    f.write(bytes_lines("para_len", [47, 39, 39, 39, 39, 31]))
    f.write(bytes_lines("para_speed", [5, 4, 3, 2, 1, 0]))   # >> shift of scx; 0 = full
    # title / text strings (ASCII, 0 terminated)
    def s(label, txt): return f"{label}: .byte " + ",".join(str(ord(c)) for c in txt) + ",0\n"
    f.write(s("str_title1", "BANANA STRESS"))
    f.write(s("str_title2", "NES SCENES 1.0"))
    f.write(s("str_hud", "SCORE 000000 COIN 00 TIME 300"))
    f.write(s("str_dialog", "THE VILLAGE IS QUIET TONIGHT. YOU SEE A LIGHT FROM THE OLD TOWER AND A PATH LEADING NORTH. WILL YOU GO? "))
    f.write(s("str_phase0", "TITLE"));    f.write(s("str_phase1", "PLATFORMER")); f.write(s("str_phase2", "PARALLAX"))
    f.write(s("str_phase3", "SHOOTER"));  f.write(s("str_phase4", "TOWN RPG"));    f.write(s("str_phase5", "FIGHTER DMC"))
    f.write(s("str_phase6", "LOADS")); f.write(s("str_phase7", "SPLIT 2006"))
print("gen ok; song rows", len(song), "dpcm used", len(dp))
