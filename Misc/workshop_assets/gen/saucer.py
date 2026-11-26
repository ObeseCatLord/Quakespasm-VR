"""Saucer enemy, its snowball and its hull gib. The saucer and gib skins are cut from the painted concept
sheet (../concept/saucer_concept.png): plates, rim, dome and turret are sampled nearest-neighbour into the
UV layout and reduced to a few palette ramps; the rune, portholes and lens are painted in fullbright colours."""
import math, sys
from kit import *
from PIL import ImageChops, ImageFilter
ident = lambda p, g: p

SRC = Image.open(os.path.join(OUT, 'concept', 'saucer_concept.png')).convert('RGB')
SS = SRC.size[0] / 1536.0            # coordinates below are in the 1536x1024 original
def src(x, y):
    return SRC.getpixel((min(max(int(x * SS), 0), SRC.size[0] - 1), min(max(int(y * SS), 0), SRC.size[1] - 1)))
BRONZE = list(range(16, 32)) + list(range(84, 95)) + list(range(96, 109)) + list(range(112, 124))
METAL = list(range(1, 10)) + BRONZE
GREEN = list(range(176, 192))
DKBLUE = [0, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42]
RUNE = [242, 243]                    # fullbright yellow-green: the palette has no fullbright green
_cache = {}
def near(c, allowed, shade=1.0):
    k = (c, id(allowed), shade)
    if k not in _cache:
        cc = [min(v * shade, 255) for v in c]
        _cache[k] = min(allowed, key=lambda i: sum((a - b) ** 2 for a, b in zip(cc, PAL[i])))
    return _cache[k]
def metal(c, allowed=METAL, shade=1.0):
    """plate colour, or a porthole/lens pixel: bright blue-white is fullbright, its rim dark blue"""
    r, g, b = c
    if r + g + b > 480: return (254 if r + g + b > 640 else 246 if r + g + b > 590 else 245, True)
    if b > r + 25: return (244, True) if b > 150 else near(c, DKBLUE)
    return near(c, allowed, shade)
def lerp(a, b, t): return a + (b - a) * t
def quadpt(q, s, t):
    """q = TL, TR, BR, BL in the source; s across, t down"""
    top = (lerp(q[0][0], q[1][0], s), lerp(q[0][1], q[1][1], s)); bot = (lerp(q[3][0], q[2][0], s), lerp(q[3][1], q[2][1], s))
    return lerp(top[0], bot[0], t), lerp(top[1], bot[1], t)
def rect_tex(box, allowed=METAL, shade=1.0):
    return lambda u, v, ix, iy: metal(src(lerp(box[0], box[2], u), lerp(box[1], box[3], v)), allowed, shade)
def ptri(m, pts, rect, f, center, grp=None):
    m.tri(pts, rect, [f(p) for p in pts], center, grp=grp)
def pol(r, deg, z): return (r * math.cos(math.radians(deg)), r * math.sin(math.radians(deg)), z)

# source regions (original pixels)
S_BRONZE = [(712, 262), (848, 262), (898, 430), (652, 430)]      # front bronze hull plate, three-quarter view
S_IRON = [(857, 262), (952, 236), (1106, 396), (900, 430)]       # the iron plate next to it
S_RIMB = (645, 820, 893, 893); S_RIMI = (897, 820, 1103, 893)    # rim segments with a porthole, side view
S_COLLAR = (700, 208, 846, 228)
S_HOUSING = (680, 928, 858, 962); S_LENS = (744, 962, 796, 1004)
S_FIN = [(352, 934), (446, 934), (428, 1000), (392, 1000)]; S_PLAIN = (935, 335, 1075, 395); S_GIB = (656, 352, 760, 430)
DOME_C = (780, 103); DOME_R = (204, 80)                           # dome as drawn: centre and half extents

def saucer():
    m = M('saucer', 256, 128, bg=20)
    R, ZR = 32.0, 2.5                       # rim
    RT, ZT = 14.5, 6.0                      # collar foot
    RD, ZD = 13.5, 7.5                      # dome base
    RM, ZM = 9.0, 10.5; ZA = 12.0           # dome shoulder, apex
    RB, ZB = 14.0, -6.0                     # belly
    AT = RT * math.cos(math.pi / 6); AB = RB * math.cos(math.pi / 6)
    R_wed = (0, 0, 98, 60); R_belly = (98, 0, 60, 60); R_dome = (158, 0, 68, 68); R_lens = (226, 0, 16, 16)
    R_finb = (242, 0, 10, 10); R_rimb = (0, 60, 52, 17); R_rimi = (52, 60, 52, 17); R_col = (104, 60, 46, 8)
    R_hous = (0, 77, 40, 14); R_barl = (40, 77, 32, 18); R_fin = (72, 77, 30, 24)
    # ---- skin
    C15 = math.cos(math.radians(15))
    def wedge(u, v, ix, iy):
        yl = (u - 0.5) * 2 * 16.0; xr = AT + v * (R - AT)
        th = min(max(math.degrees(math.atan2(yl, xr)), -29.99), 29.99); r = math.hypot(yl, xr)
        tc = -15 if th < 0 else 15
        rin = AT / math.cos(math.radians(th)); rout = R * C15 / math.cos(math.radians(th - tc))
        t = min(max((r - rin) / (rout - rin), 0.0), 1.0); s = (th - (tc - 15)) / 30.0
        x, y = quadpt(S_BRONZE if th < 0 else S_IRON, s, t)
        return metal(src(x, y))
    m.paint(R_wed, wedge)
    m.paint(R_rimb, rect_tex(S_RIMB)); m.paint(R_rimi, rect_tex(S_RIMI))
    m.paint(R_col, rect_tex(S_COLLAR, BRONZE))
    m.paint(R_hous, rect_tex(S_HOUSING, BRONZE)); m.paint(R_barl, rect_tex(S_HOUSING, BRONZE, 0.8))
    m.paint(R_lens, rect_tex(S_LENS)); m.paint(R_fin, lambda u, v, ix, iy: near(src(*quadpt(S_FIN, u, v)), BRONZE)); m.paint(R_finb, rect_tex(S_PLAIN, METAL, 0.6))
    for r in (R_rimb, R_rimi, R_lens):                    # repaint each light: blue edge, pale body, white core
        on = {(x, y) for y in range(r[1], r[1] + r[3]) for x in range(r[0], r[0] + r[2]) if m.mask.getpixel((x, y))}
        for (x, y) in on:
            n4 = sum((x + i, y + j) in on for i, j in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            n8 = sum((x + i, y + j) in on for i in (-1, 0, 1) for j in (-1, 0, 1))
            m.px(x, y, 244 if n4 < 4 else 254 if n8 == 9 else 246, True)
    def belly(u, v, ix, iy):
        d = math.hypot(u - 0.5, v - 0.5) * 2
        return near(src(lerp(S_PLAIN[0], S_PLAIN[2], u), lerp(S_PLAIN[1], S_PLAIN[3], v)), METAL, 0.55 + 0.35 * min(d, 1.0))
    m.paint(R_belly, belly)
    for k in range(12):                                   # rivets round the belly plate
        x = int(round(29.5 + 22 * math.cos(math.radians(30 * k + 15)))); y = int(round(29.5 + 22 * math.sin(math.radians(30 * k + 15))))
        m.dot(R_belly, x, y, 122); m.dot(R_belly, x + 1, y, 118); m.dot(R_belly, x, y + 1, 116); m.dot(R_belly, x + 1, y + 1, 1)
    # dome: un-squash the drawn dome into a top view; the rune is whatever is clearly lighter than its surroundings
    g = SRC.split()[1]; rune = Image.eval(ImageChops.subtract(g, g.filter(ImageFilter.GaussianBlur(5))), lambda v: 255 if v > 20 else 0)
    def dome(u, v, ix, iy):
        dx, dy = (u - 0.5) * 2, (v - 0.5) * 2
        x, y = DOME_C[0] + dx * DOME_R[0], DOME_C[1] + dy * DOME_R[1]
        c = src(x, y)
        if math.hypot(dx, dy) < 0.70 and abs(dx) < 0.6:
            px, py = int(x * SS), int(y * SS)
            hit = sum(rune.getpixel((px + i, py)) > 0 for i in (-1, 0, 1))
            if hit >= 2: return (RUNE[1] if c[1] > 150 else RUNE[0], True)
        return near(c, GREEN)
    m.paint(R_dome, dome)
    # ---- hull
    O = (0, 0, 0)
    for k in range(6):
        a = 60 * k; ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        def fw(A):
            return lambda p: (0.5 + (-p[0] * sa + p[1] * ca) / 32.0, (p[0] * ca + p[1] * sa - A) / (R - A))
        for (rr, zz, zr, A) in ((RT, ZT, ZR, AT), (RB, ZB, -ZR, AB)):          # top plates, underside plates
            ia, ib = pol(rr, a - 30, zz), pol(rr, a + 30, zz)
            oa, om, ob = pol(R, a - 30, zr), pol(R, a, zr), pol(R, a + 30, zr)
            f = fw(A)
            ptri(m, [ia, oa, om], R_wed, f, O); ptri(m, [ia, om, ib], R_wed, f, O); ptri(m, [ib, om, ob], R_wed, f, O)
    for j in range(12):                                                         # rim, a porthole per side
        a0, a1 = 30 * j, 30 * j + 30
        m.quad([pol(R, a0, ZR), pol(R, a1, ZR), pol(R, a1, -ZR), pol(R, a0, -ZR)], R_rimi if j % 2 == 0 else R_rimb, O)
    fd = lambda p: (0.5 + p[1] / (2 * RD), 0.5 + p[0] / (2 * RD))
    for k in range(6):
        a0, a1 = 60 * k - 30, 60 * k + 30
        m.quad([pol(RD, a0, ZD), pol(RD, a1, ZD), pol(RT, a1, ZT), pol(RT, a0, ZT)], R_col, O)
        b0, b1, m0, m1 = pol(RD, a0, ZD), pol(RD, a1, ZD), pol(RM, a0, ZM), pol(RM, a1, ZM)
        ptri(m, [b0, b1, m1], R_dome, fd, O); ptri(m, [b0, m1, m0], R_dome, fd, O)
        ptri(m, [m0, m1, (0, 0, ZA)], R_dome, fd, O)
    fb = lambda p: (0.5 + p[1] / (2 * RB), 0.5 + p[0] / (2 * RB))
    hexb = [pol(RB, 60 * k - 30, ZB) for k in range(6)]
    for k in range(1, 5): ptri(m, [hexb[0], hexb[k], hexb[k + 1]], R_belly, fb, O)
    # ---- turret: a housing under the belly and a square barrel pointing forward and a little down
    sq = lambda cx, hs, z: [(cx - hs, -hs, z), (cx + hs, -hs, z), (cx + hs, hs, z), (cx - hs, hs, z)]
    h0, h1 = sq(1.0, 5.5, -5.5), sq(1.5, 3.6, -8.6)
    m.loft([h0, h1], R_hous, swap=True, center=(1, 0, -6.5)); m.fan(h1, R_finb, (0, 1), (1, 0, -6.5))
    b0, b1 = ngon(1.0, 0, -7.2, 2.6, 2.6, 4), ngon(10.0, 0, -8.1, 2.6, 2.6, 4)
    m.loft([b0, b1], R_barl, center=(5.0, 0, -7.7)); m.fan(b1, R_lens, (1, 2), (5.0, 0, -7.7))
    # ---- three fins
    for a in (60, 180, 300):
        ca, sa = math.cos(math.radians(a)), math.sin(math.radians(a))
        w = lambda r, t, z: (r * ca - t * sa, r * sa + t * ca, z)
        top = [w(27.0, 0, -3.2), w(19.0, 1.7, -3.2), w(19.0, -1.7, -3.2)]
        bot = [w(27.5, 0, -10.0), w(24.5, 0.9, -10.0), w(24.5, -0.9, -10.0)]
        cen = w(23.5, 0, -6.5)
        m.loft([top, bot], R_fin, swap=True, center=cen); m.fan(bot, R_finb, (0, 1), cen)
    s6, c6 = math.sin(math.radians(6)), math.cos(math.radians(6))
    m.save([('idle', ident), ('fire', lambda p, g: (p[0] * c6 + p[2] * s6, p[1], -p[0] * s6 + p[2] * c6))])

def snowball():
    m = M('snowball', 32, 32, bg=14)
    R_all = (0, 0, 32, 32)
    def snow(u, v, ix, iy):
        q = m.rng.random()
        if q < 0.012 + 0.02 * v: return m.rng.choice((43, 44, 45))                      # darker blue speckles
        if q < 0.03 + 0.3 * v ** 2: return 47 if m.rng.random() < 0.8 else 46        # pale blue shading, dithered in
        return 15 if m.rng.random() < 0.75 - 0.4 * v else 14
    m.paint(R_all, snow)
    top, bot = (0.2, -0.1, 3.0), (-0.2, 0.1, -2.9)
    up = [pol(r, a, z) for r, a, z in ((2.9, 10, 1.1), (2.7, 95, 0.7), (3.0, 190, 1.3), (2.8, 280, 0.8))]
    lo = [pol(r, a, z) for r, a, z in ((2.8, 50, -1.2), (2.9, 170, -1.5), (2.7, 300, -1.0))]
    def tri(pts):
        # box mapping: project each facet along the axis its normal is closest to, all at one scale
        e1 = [pts[1][i] - pts[0][i] for i in range(3)]; e2 = [pts[2][i] - pts[0][i] for i in range(3)]
        n = (e1[1]*e2[2] - e1[2]*e2[1], e1[2]*e2[0] - e1[0]*e2[2], e1[0]*e2[1] - e1[1]*e2[0])
        ax = max(range(3), key=lambda i: abs(n[i]))
        a, b = ((1, 2), (0, 2), (0, 1))[ax]
        m.tri(pts, R_all, [((p[a] + 3.0) / 6.0, (3.0 - p[b]) / 6.0) for p in pts], (0, 0, 0))
    for i in range(4): tri([top, up[i], up[(i + 1) % 4]])
    for i in range(3): tri([bot, lo[i], lo[(i + 1) % 3]])
    for t in ((up[0], lo[0], up[1]), (up[1], lo[0], lo[1]), (up[1], lo[1], up[2]), (up[2], lo[1], up[3]),
              (up[3], lo[1], lo[2]), (up[3], lo[2], up[0]), (up[0], lo[2], lo[0])): tri(list(t))
    m.save([('idle', ident)])

def saucergib():
    m = M('saucergib', 32, 32, bg=20)
    R_all = (0, 0, 32, 32)
    m.paint(R_all, rect_tex(S_GIB, BRONZE))
    fold = lambda x, y: 0.5 * abs(x) - 1.0 + 0.12 * y                  # folded along the y axis
    rim = [(x, y, fold(x, y)) for x, y in ((5.2, -1.0), (2.0, 3.6), (-3.0, 3.0), (-5.0, -0.8), (-0.5, -3.8))]
    f = lambda p: (0.5 + p[0] / 10.6, 0.5 - p[1] / 7.8)
    for apex in ((0, 0, -0.5), (0, 0, -1.5)):                          # thin lens: a top and an underside fan
        for i in range(5): ptri(m, [apex, rim[i], rim[(i + 1) % 5]], R_all, f, (0, 0, -1.0))
    m.save([('idle', ident)])

for f in (saucer, snowball, saucergib):
    if len(sys.argv) < 2 or f.__name__ in sys.argv[1:]: f()
