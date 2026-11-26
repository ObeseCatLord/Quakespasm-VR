#!/usr/bin/env python3
"""Paints the yeti coat: a fresh skin for the UV layout of Quake's shambler.

  QUAKE_PALETTE=/path/to/palette.lmp python3 paint_yeti.py /path/to/shambler.mdl

The shambler model is read from your own Quake id1 pak (extract progs/shambler.mdl somewhere outside this
repository); only its UV layout and facet normals are used, none of its skin. Writes yeti_skin.png next to
this script and ../../vq_pak/progs/workshop/yeti_skin.lmp (int width, int height, then palette indices).
"""
import math, os, struct, sys
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
from mdltool import parse, load_palette

PAL = load_palette(os.environ['QUAKE_PALETTE'])
mdl = parse(sys.argv[1])
W, H = mdl['sw'], mdl['sh']
HALF = W // 2                      # front view on the left half, back view on the right
verts = mdl['frames'][0][1]

def h2(x, y, k=0):                 # small integer hash -> 0..1
    n = (x * 374761393 + y * 668265263 + k * 2147483647) & 0xffffffff
    n = ((n ^ (n >> 13)) * 1274126177) & 0xffffffff
    return ((n ^ (n >> 16)) & 0xffff) / 65535.0

# ---- facet normals per texel (only to tell up-facing fur from down-facing fur)
NZ = [[0.0] * W for _ in range(H)]
for front, a, b, c in mdl['tris']:
    uv = []
    for i in (a, b, c):
        seam, s, t = mdl['st'][i]
        uv.append((s + (HALF if (seam and not front) else 0), t))
    p = [verts[i] for i in (a, b, c)]
    e1 = [p[1][k] - p[0][k] for k in range(3)]; e2 = [p[2][k] - p[0][k] for k in range(3)]
    n = (e1[1]*e2[2] - e1[2]*e2[1], e1[2]*e2[0] - e1[0]*e2[2], e1[0]*e2[1] - e1[1]*e2[0])
    l = math.sqrt(sum(v * v for v in n)) or 1.0
    nz = -n[2] / l                 # MDL triangles wind clockwise
    (x0, y0), (x1, y1), (x2, y2) = uv
    den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
    if not den: continue
    for y in range(max(min(y0, y1, y2) - 1, 0), min(max(y0, y1, y2) + 2, H)):
        for x in range(max(min(x0, x1, x2) - 1, 0), min(max(x0, x1, x2) + 2, W)):
            w0 = ((y1 - y2) * (x - x2) + (x2 - x1) * (y - y2)) / den
            w1 = ((y2 - y0) * (x - x2) + (x0 - x2) * (y - y2)) / den
            if w0 >= -0.05 and w1 >= -0.05 and w0 + w1 <= 1.05: NZ[y][x] = nz

# ---- the fur swatch, cut down to chunky pixels
T = 96
sw = Image.open(os.path.join(HERE, 'yeti_fur_swatch.png')).convert('RGB').resize((T, T), Image.NEAREST)
def tone(c):                      # more contrast, and only the deep gaps between tufts stay blue
    g = (c[0] * 3 + c[1] * 6 + c[2]) / 10.0
    k = 0.35 if g > 150 else 1.0
    return tuple(min(max(int(((g + (v - g) * k) - 128) * 0.85 + 138), 0), 255) for v in c)
FUR = [[tone(sw.getpixel((x, y))) for x in range(T)] for y in range(T)]
FURPAL = list(range(4, 16)) + list(range(36, 48))
SKINPAL = [34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46]
SHADE = (70, 78, 105)              # fur in shadow goes blue-grey, not black
_c = {}
def near(c, allowed):
    k = (c, id(allowed))
    if k not in _c: _c[k] = min(allowed, key=lambda i: sum((a - b) ** 2 for a, b in zip(c, PAL[i])))
    return _c[k]
def sstep(a, b, v):
    t = min(max((v - a) / float(b - a), 0.0), 1.0); return t * t * (3 - 2 * t)

CX = 77                            # body centre line within a half
WRIST = ((32, 64), (121, 64))
def rag(x, k, amp=4):              # ragged fur edge: teeth two pixels wide
    return int(h2(x // 2, k, 7) * (amp + 1))
def fur(x, y, xl, back):
    side = -1 if xl < CX else 1
    out = sstep(22, 44, abs(xl - CX)) * (1 - sstep(30, 52, y))        # the upper arms: fur lies along the arm
    th = math.radians(42 * out * side * (-1 if back else 1) * (-1))
    px, py = CX + side * 28, 12
    dx, dy = xl - px, y - py
    u = px + dx * math.cos(th) - dy * math.sin(th) + (0 if not back else 37)
    v = py + dx * math.sin(th) + dy * math.cos(th) + (0 if not back else 19)
    c = FUR[int(v) % T][int(u) % T]
    k = h2(int(u), int(v) // 5 + int(u) * 3, 13)                      # single strands: short darker and lighter runs
    if k < 0.14: c = tuple(int(q * 0.9) for q in c)
    elif k > 0.90: c = tuple(min(int(q * 1.10), 255) for q in c)
    # light by region: bright shoulders and back, shaded belly, armpits, inner arms and legs
    L = 1.0 - 0.10 * sstep(20, 60, y)
    if not back:
        L -= 0.20 * sstep(22, 8, abs(xl - CX)) * sstep(34, 46, y) * (1 - sstep(64, 74, y))      # belly
        L -= 0.10 * sstep(14, 6, abs(xl - CX)) * sstep(18, 24, y) * (1 - sstep(30, 40, y))      # under the jaw
    L -= 0.22 * sstep(8, 2, abs(abs(xl - CX) - 27)) * sstep(28, 40, y) * (1 - sstep(62, 70, y))  # armpits, flanks
    L -= 0.16 * sstep(40, 34, abs(xl - CX)) * sstep(30, 23, abs(xl - CX) - 4) * sstep(40, 50, y)  # inner forearms
    L -= 0.22 * sstep(7, 2, abs(xl - CX)) * sstep(66, 72, y)                                     # between the legs
    L += 0.10 * NZ[y][x] if NZ[y][x] > 0 else 0.28 * NZ[y][x]
    if back: L += 0.05
    L += (h2(x // 2, y // 2, 11) - 0.5) * 0.10                       # break the steps up
    L = min(max(round(L / 0.06) * 0.06, 0.45), 1.08)
    if L < 1: c = tuple(int(SHADE[i] + (c[i] - SHADE[i]) * L) for i in range(3))
    else: c = tuple(min(int(v * L), 255) for v in c)
    return near(c, FURPAL)
def bare(lvl, x, y):               # blue-grey hide, lvl 0 dark .. 1 light
    i = lvl * (len(SKINPAL) - 1) + (h2(x, y, 3) - 0.5) * 1.6
    return SKINPAL[min(max(int(round(i)), 0), len(SKINPAL) - 1)]
TIP = (47, 46, 47, 45)
def texel(x, y):
    back = x >= HALF; xl = x - HALF if back else x
    # hands: fur ends in a ragged cuff, then hide, then dark claws
    if xl < 47 or xl > 107:
        wx, wy = WRIST[0] if xl < 47 else WRIST[1]
        edge = 62 + rag(x, 1)
        if y > edge:
            d = math.hypot(xl - wx, y - wy) / 34.0
            ang = math.atan2(y - wy, xl - wx)
            ridge = math.sin(ang * 17.0) > 0.6
            if d < 0.42: return bare(0.72 + (0.12 if ridge else 0.0) - 0.3 * d, x, y)
            return bare(max(0.55 - 0.75 * (d - 0.42), 0.08) + (0.15 if ridge else 0.0), x, y)
        if y > edge - 2: return TIP[(x + y) % 4]
    # feet: fur to the ankle, then dark toes
    elif y > 97:
        edge = 99 + rag(x, 2, 3)
        if y > edge: return bare((0.62 if y < 107 else 0.12) + (0.2 if xl % 5 == 0 else 0.0) - (0.25 if xl % 5 == 4 else 0.0), x, y)
        if y > edge - 2: return TIP[(x + y) % 4]
    # face (front only): hide, small cold eyes under a dark brow, dark mouth, pale teeth
    if not back:
        fx, fy = xl - 75.5, y - 12.0
        r = math.hypot(fx / 8.0, fy / 9.5)
        lim = 1.0 + (h2(int(math.atan2(fy, fx) * 5), 5, 9) - 0.5) * 0.3
        if r < lim:
            ax = abs(fx)
            if 10 <= y <= 18 and ax <= (5.5 if 12 <= y <= 16 else 4.5):                                   # mouth
                if y in (10, 11) and int(ax) % 2 == 0: return 15 if y == 10 else 13   # upper teeth
                if y == 12 and int(ax) == 4: return 13                               # fangs
                if y in (17, 18) and int(ax) % 2 == 1: return 13 if y == 17 else 15   # lower teeth
                if y == 16 and int(ax) == 5: return 13
                return 64 if (y in (14, 15) and ax < 3) else 0
            if y == 7 and 2.5 <= ax <= 4.5: return 245 if ax < 3.5 else 244          # eyes (fullbright)
            if y == 6 and 1.5 <= ax <= 5.5: return 33                                # brow
            if y == 8 and 2.5 <= ax <= 4.5: return 34
            if y in (7, 8) and ax < 1.5: return bare(0.85, x, y)                     # nose bridge
            return bare(0.82 - 0.3 * r - (0.15 if y > 18 else 0.0), x, y)
        if r < lim + 0.22: return TIP[(x + y) % 4]
    return fur(x, y, xl, back)

idx = bytearray(W * H)
for y in range(H):
    for x in range(W): idx[y * W + x] = texel(x, y)
assert 255 not in idx
im = Image.new('RGB', (W, H)); im.putdata([PAL[i] for i in idx]); im.save(os.path.join(HERE, 'yeti_skin.png'))
out = os.path.join(HERE, '..', '..', 'vq_pak', 'progs', 'workshop', 'yeti_skin.lmp')
open(out, 'wb').write(struct.pack('<ii', W, H) + bytes(idx))
print('yeti skin %dx%d, %d fullbright texels' % (W, H, sum(1 for i in idx if i >= 224)))
