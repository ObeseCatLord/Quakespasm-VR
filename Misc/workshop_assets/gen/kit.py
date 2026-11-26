import json, math, os, random
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
PALF = os.environ['QUAKE_PALETTE']      # gfx/palette.lmp from your own Quake id1/pak0.pak
WORK = os.environ.get('WORKSHOP_WORK') or os.path.join(__import__('tempfile').gettempdir(), 'workshop_gen')
_raw = open(PALF, 'rb').read()
PAL = [tuple(_raw[i*3:i*3+3]) for i in range(256)]
OUT = os.path.dirname(HERE)

def ngon(x, cy, cz, ry, rz, n, phase=None):
    """ring in the YZ plane at x; returns (points3d, points2d) CCW in (y,z)"""
    if phase is None: phase = math.pi / n - math.pi / 2   # flat bottom / flat top for even n
    p2 = [(cy + ry * math.cos(phase + 2*math.pi*j/n), cz + rz * math.sin(phase + 2*math.pi*j/n)) for j in range(n)]
    return [(x, y, z) for y, z in p2]
def prof(x, p2, sc=1.0, c=(0, 0)):
    return [(x, c[0] + (y - c[0]) * sc, c[1] + (z - c[1]) * sc) for y, z in p2]
def side_nz(p2):
    out = []
    n = len(p2)
    for j in range(n):
        (y0, z0), (y1, z1) = p2[j], p2[(j+1) % n]
        dy, dz = y1 - y0, z1 - z0
        l = math.hypot(dy, dz) or 1
        out.append(-dy / l)
    return out
def yz(ring): return [(p[1], p[2]) for p in ring]

def cumw(wts, n):
    wts = wts or [1] * n
    t = float(sum(wts)); c = [0.0]
    for w in wts: c.append(c[-1] + w / t)
    c[-1] = 1.0
    return c

class M:
    def __init__(s, name, W, H, skins=('skin',), bg=0):
        s.name, s.W, s.H = name, W, H
        s.skin_names = list(skins)
        s.imgs = [Image.new('RGB', (W, H), PAL[bg]) for _ in skins]
        s.mask = Image.new('L', (W, H), 0)
        s.cur = 0; s.has_fb = False
        s.V, s.vmap, s.vgrp, s.UV, s.uvmap, s.T = [], {}, [], [], {}, []
        s.rng = random.Random(hash(name) & 0xffff if False else sum(map(ord, name)))
        s.sx = s.sy = s.rowh = 0
    # ---- atlas
    def alloc(s, w, h):
        if s.sx + w > s.W:
            s.sx = 0; s.sy += s.rowh; s.rowh = 0
        assert s.sy + h <= s.H, (s.name, 'atlas full', w, h, s.sx, s.sy)
        r = (s.sx, s.sy, w, h); s.sx += w; s.rowh = max(s.rowh, h)
        return r
    def px(s, x, y, idx, fb=False):
        if 0 <= x < s.W and 0 <= y < s.H:
            s.imgs[s.cur].putpixel((x, y), PAL[idx])
            if s.cur == 0 or fb:
                s.mask.putpixel((x, y), 255 if fb else 0)
            if fb: s.has_fb = True
    def paint(s, r, fn):
        """fn(u, v, ix, iy) -> idx | (idx, fb) | None ; u,v in 0..1 over the usable area, ix/iy pixel in rect"""
        x, y, w, h = r
        for iy in range(h):
            for ix in range(w):
                u = min(max((ix - 1 + 0.5) / max(w - 2, 1), 0.0), 0.9999)
                v = min(max((iy - 1 + 0.5) / max(h - 2, 1), 0.0), 0.9999)
                c = fn(u, v, ix, iy)
                if c is None: continue
                if isinstance(c, tuple): s.px(x + ix, y + iy, c[0], c[1])
                else: s.px(x + ix, y + iy, c)
    def pick(s, ramp, lvl, noise=0.2):
        """ramp dark->light, lvl 0..1"""
        i = int(round(lvl * (len(ramp) - 1)))
        if s.rng.random() < noise * 0.45: i += s.rng.choice((-1, 1))
        return ramp[min(max(i, 0), len(ramp) - 1)]
    def paint_loft(s, r, nz, ramp, cols=(), noise=0.2, fb=False, swap=False, extra=None, wts=None):
        n = len(nz)
        cum = cumw(wts, n)
        def fn(u, v, ix, iy):
            a, b = (v, u) if swap else (u, v)   # a along length, b around
            j = max(k for k in range(n) if cum[k] <= b)
            lvl = nz[j] * 0.5 + 0.5
            rp, f = ramp, fb
            for c in cols:
                if c[0] <= a < c[1]:
                    rp = c[2]; f = c[3] if len(c) > 3 else False
            if extra:
                e = extra(a, (b - cum[j]) / (cum[j + 1] - cum[j]), j, ix, iy)
                if e is not None: return e
            return (s.pick(rp, lvl, noise), f)
        s.paint(r, fn)
    def fill(s, r, ramp, lvl=0.5, noise=0.3, fb=False):
        s.paint(r, lambda u, v, ix, iy: (s.pick(ramp, lvl, noise), fb))
    def dot(s, r, dx, dy, idx, w=1, h=1, fb=False):
        for j in range(h):
            for i in range(w):
                s.px(r[0] + dx + i, r[1] + dy + j, idx, fb)
    # ---- geometry
    def vert(s, p, grp=None):
        k = (round(p[0], 4), round(p[1], 4), round(p[2], 4), grp)
        if k not in s.vmap:
            s.vmap[k] = len(s.V); s.V.append(tuple(float(c) for c in p)); s.vgrp.append(grp)
        return s.vmap[k]
    def uv(s, r, u, v):
        x = round(r[0] + 1 + u * (r[2] - 2)); y = round(r[1] + 1 + v * (r[3] - 2))
        k = (x, y)
        if k not in s.uvmap:
            s.uvmap[k] = len(s.UV); s.UV.append((x / s.W, 1.0 - y / s.H))
        return s.uvmap[k]
    def tri(s, pts, r, uvs, center=None, flip=False, grp=None):
        a, b, c = pts
        if center is not None:
            e1 = [b[i] - a[i] for i in range(3)]; e2 = [c[i] - a[i] for i in range(3)]
            n = (e1[1]*e2[2] - e1[2]*e2[1], e1[2]*e2[0] - e1[0]*e2[2], e1[0]*e2[1] - e1[1]*e2[0])
            cen = [(a[i] + b[i] + c[i]) / 3 - center[i] for i in range(3)]
            if sum(n[i] * cen[i] for i in range(3)) < 0: flip = not flip
        o = (0, 2, 1) if flip else (0, 1, 2)
        s.T.append([(s.vert(pts[i], grp), s.uv(r, *uvs[i])) for i in o])
    def quad(s, pts, r, center=None, sub=(0, 0, 1, 1), flip=False, grp=None, swap=False):
        u0, v0, u1, v1 = sub
        uvs = [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]
        if swap: uvs = [(v, u) for u, v in uvs]
        s.tri([pts[0], pts[1], pts[2]], r, [uvs[0], uvs[1], uvs[2]], center, flip, grp)
        s.tri([pts[0], pts[2], pts[3]], r, [uvs[0], uvs[2], uvs[3]], center, flip, grp)
    def loft(s, rings, rect, us=None, wts=None, flip=False, grp=None, swap=False, center=None):
        """u along the length (us: (u0,u1) per segment), v around (one strip per side, wts = strip weights)."""
        us = us or [(0, 1)] * (len(rings) - 1)
        for k in range(len(rings) - 1):
            r0, r1 = rings[k], rings[k + 1]; n = len(r0)
            cum = cumw(wts, n)
            cen = center or [sum(p[i] for p in r0 + r1) / (2 * n) for i in range(3)]
            for j in range(n):
                j1 = (j + 1) % n
                sub = (us[k][0], cum[j], us[k][1], cum[j + 1])
                s.quad([r0[j], r1[j], r1[j1], r0[j1]], rect, cen, sub, flip, grp, swap)
    def cone(s, ring, tip, r, flip=False, grp=None, center=None):
        n = len(ring)
        cen = center or [sum(p[i] for p in ring) / n for i in range(3)]
        for j in range(n):
            s.tri([ring[j], ring[(j + 1) % n], tip], r, [(0, j / n), (0, (j + 1) / n), (1, (j + 0.5) / n)], cen, flip, grp)
    def fan(s, pts, r, axes, center, grp=None, flip=False, bbox=None):
        a, b = axes
        if bbox is None:
            bbox = (min(p[a] for p in pts), min(p[b] for p in pts), max(p[a] for p in pts), max(p[b] for p in pts))
        f = lambda p: ((p[a] - bbox[0]) / (bbox[2] - bbox[0] or 1), 1 - (p[b] - bbox[1]) / (bbox[3] - bbox[1] or 1))
        for k in range(1, len(pts) - 1):
            t = [pts[0], pts[k], pts[k + 1]]
            s.tri(t, r, [f(p) for p in t], center, flip, grp)
    def box_ring(s, z, x0, x1, hy):
        return [(x0, -hy, z), (x1, -hy, z), (x1, hy, z), (x0, hy, z)]
    # ---- output
    def save(s, frames, fullbright=None):
        """frames: list of (name, fn(p, grp)->p)"""
        d = os.path.join(OUT, s.name); os.makedirs(d, exist_ok=True)
        skins = []
        for nm, im in zip(s.skin_names, s.imgs):
            im.save(os.path.join(d, nm + '.png')); skins.append(nm + '.png')
        spec = {'frames': ['frame_%s.obj' % n for n, _ in frames], 'frame_names': [n for n, _ in frames], 'skins': skins}
        if s.has_fb:
            s.mask.convert('RGB').save(os.path.join(d, 'fullbright.png')); spec['fullbright'] = 'fullbright.png'
        json.dump(spec, open(os.path.join(d, 'spec.json'), 'w'), indent=1)
        mesh = {'name': s.name, 'dir': d, 'skin': skins[0], 'uvs': s.UV, 'tris': s.T,
                'frames': [[n, [list(fn(p, g)) for p, g in zip(s.V, s.vgrp)]] for n, fn in frames]}
        os.makedirs(WORK, exist_ok=True)
        json.dump(mesh, open(os.path.join(WORK, s.name + '.mesh.json'), 'w'))
        xs = list(zip(*s.V))
        print('%-12s %3d tris %3d pos  bounds %s' % (s.name, len(s.T), len(s.V), [(round(min(a), 1), round(max(a), 1)) for a in xs]))
