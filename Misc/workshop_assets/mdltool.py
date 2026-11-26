#!/usr/bin/env python3
"""
Quake MDL (IDPO version 6) tool for the workshop assets.

  mdltool.py build <spec.json> <out.mdl> --palette <palette.lmp>
      spec: {"frames": ["idle.obj", "fire1.obj", ...],      # same mesh, same vertex/face order
             "frame_names": ["idle", "fire1", ...],         # optional, up to 15 characters
             "skins": ["skin.png", ...],                    # same size, width a multiple of 4
             "fullbright": "glow.png",                      # optional mask: white = fullbright
             "flags": 0, "synctype": 0}
      OBJ units are Quake units with Quake axes (x forward, y left, z up). Every face must be a
      triangle with a texture coordinate on every corner.
  mdltool.py obj <in.mdl> <out_prefix> --palette <palette.lmp>
      writes <out_prefix>_<frame>.obj per frame and <out_prefix>_skin<n>.png, for reference.
  mdltool.py info <in.mdl>

The Quake palette is read from the game's own gfx/palette.lmp (768 bytes); it is not stored here.
Colours 224-254 are fullbright in Quake: only pixels the fullbright mask marks may map there, and
colour 255 is never used (it is transparent in some renderers).
"""
import json, math, os, struct, sys

def anorms():
    # The 162 Quake light normals (Quake/anorms.h): parse them from the engine source next door.
    here = os.path.dirname(os.path.abspath(__file__))
    text = open(os.path.join(here, '..', '..', 'Quake', 'anorms.h')).read()
    nums = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('{'):
            nums.append([float(v) for v in line.strip('{},').split(',')])
    assert len(nums) == 162, len(nums)
    return nums

def read_obj(path):
    verts, uvs, faces = [], [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == 'v':
            verts.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == 'vt':
            uvs.append((float(p[1]), float(p[2])))
        elif p[0] == 'f':
            corners = []
            for c in p[1:]:
                parts = c.split('/')
                vi = int(parts[0]); ti = int(parts[1]) if len(parts) > 1 and parts[1] else 0
                corners.append((vi - 1 if vi > 0 else len(verts) + vi, ti - 1 if ti > 0 else len(uvs) + ti))
            for k in range(1, len(corners) - 1):  # fan, in case a quad slipped through
                faces.append((corners[0], corners[k], corners[k + 1]))
    return verts, uvs, faces

def load_palette(path):
    raw = open(path, 'rb').read()
    assert len(raw) >= 768
    return [tuple(raw[i * 3:i * 3 + 3]) for i in range(256)]

def quantize(png, palette, mask_png=None):
    from PIL import Image
    im = Image.open(png).convert('RGB')
    w, h = im.size
    mask = Image.open(mask_png).convert('L').resize((w, h)) if mask_png else None
    px = im.load(); mp = mask.load() if mask else None
    cache = {}
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            c = px[x, y]
            fb = bool(mp and mp[x, y] > 127)
            key = (c, fb)
            if key not in cache:
                rng = range(224, 255) if fb else range(0, 224)
                best = min(rng, key=lambda i: sum((a - b) ** 2 for a, b in zip(c, palette[i])))
                cache[key] = best
            out[y * w + x] = cache[key]
    return w, h, bytes(out)

def build(spec_path, out_path, palette_path):
    spec = json.load(open(spec_path))
    base = os.path.dirname(os.path.abspath(spec_path))
    palette = load_palette(palette_path)
    norms = anorms()
    frames = [read_obj(os.path.join(base, f)) for f in spec['frames']]
    _, uvs, faces = frames[0]
    for k, (v, _, f) in enumerate(frames):
        assert len(v) == len(frames[0][0]) and len(f) == len(faces), 'frame %d topology differs' % k
    skins = [quantize(os.path.join(base, s), palette, os.path.join(base, spec['fullbright']) if spec.get('fullbright') else None) for s in spec['skins']]
    sw, sh = skins[0][0], skins[0][1]
    assert sw % 4 == 0 and all(s[0] == sw and s[1] == sh for s in skins)
    # one MDL vertex per distinct (position, uv) corner
    keymap, mverts, tris = {}, [], []
    for tri in faces:
        idx = []
        for vi, ti in tri:
            key = (vi, ti)
            if key not in keymap:
                keymap[key] = len(mverts)
                mverts.append(key)
            idx.append(keymap[key])
        tris.append(idx)
    # bounds over every frame
    allpos = [p for v, _, _ in frames for p in v]
    mins = [min(p[a] for p in allpos) for a in range(3)]
    maxs = [max(p[a] for p in allpos) for a in range(3)]
    scale = [max((maxs[a] - mins[a]) / 255.0, 1e-4) for a in range(3)]
    radius = max(math.sqrt(sum(c * c for c in p)) for p in allpos)
    def vnormals(verts):
        acc = [[0.0, 0.0, 0.0] for _ in verts]
        for (a, _), (b, _), (c, _) in faces:
            e1 = [verts[b][i] - verts[a][i] for i in range(3)]
            e2 = [verts[c][i] - verts[a][i] for i in range(3)]
            n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
            for i in (a, b, c):
                for k in range(3):
                    acc[i][k] += n[k]
        out = []
        for n in acc:
            l = math.sqrt(sum(x * x for x in n)) or 1.0
            n = [x / l for x in n]
            out.append(max(range(162), key=lambda j: sum(n[k] * norms[j][k] for k in range(3))))
        return out
    body = bytearray()
    for _, _, pixels in skins:
        body += struct.pack('<i', 0) + pixels
    for vi, ti in mverts:
        u, v = uvs[ti]
        s = int(round(u * sw)); t = int(round((1.0 - v) * sh))  # OBJ v goes up, MDL t goes down
        body += struct.pack('<iii', 0, min(max(s, 0), sw - 1), min(max(t, 0), sh - 1))
    for t in tris:
        # Quake winds triangles clockwise seen from outside; OBJ is counter-clockwise
        body += struct.pack('<iiii', 1, t[0], t[2], t[1])
    names = spec.get('frame_names') or [os.path.splitext(os.path.basename(f))[0] for f in spec['frames']]
    for (verts, _, _), name in zip(frames, names):
        ln = vnormals(verts)
        packed = []
        for vi, _ in mverts:
            p = verts[vi]
            packed.append([int(round((p[a] - mins[a]) / scale[a])) for a in range(3)] + [ln[vi]])
        fmin = [min(q[a] for q in packed) for a in range(3)] + [0]
        fmax = [max(q[a] for q in packed) for a in range(3)] + [0]
        body += struct.pack('<i', 0) + bytes(fmin) + bytes(fmax) + name.encode()[:15].ljust(16, b'\0')
        for q in packed:
            body += bytes(q)
    hdr = struct.pack('<4si3f3ff3fiiiiiiiif', b'IDPO', 6, *scale, *mins, radius, 0.0, 0.0, 0.0,
                      len(skins), sw, sh, len(mverts), len(tris), len(frames), spec.get('synctype', 0), spec.get('flags', 0), 1.0)
    open(out_path, 'wb').write(hdr + body)
    print('%s: %d verts, %d tris, %d frames, %d skins %dx%d, %d bytes' % (out_path, len(mverts), len(tris), len(frames), len(skins), sw, sh, len(hdr) + len(body)))

def parse(path):
    d = open(path, 'rb').read()
    h = struct.unpack_from('<4si3f3ff3fiiiiiiiif', d, 0)
    ident, ver = h[0], h[1]; scale = h[2:5]; origin = h[5:8]
    nskins, sw, sh, nv, nt, nf, sync, flags = h[12:20]
    assert ident == b'IDPO' and ver == 6
    off = struct.calcsize('<4si3f3ff3fiiiiiiiif')
    skins = []
    for _ in range(nskins):
        g = struct.unpack_from('<i', d, off)[0]; off += 4
        if g == 0:
            skins.append(d[off:off + sw * sh]); off += sw * sh
        else:
            n = struct.unpack_from('<i', d, off)[0]; off += 4 + 4 * n
            skins.append(d[off:off + sw * sh]); off += sw * sh * n
    st = [struct.unpack_from('<iii', d, off + 12 * i) for i in range(nv)]; off += 12 * nv
    tris = [struct.unpack_from('<iiii', d, off + 16 * i) for i in range(nt)]; off += 16 * nt
    frames = []
    for _ in range(nf):
        t = struct.unpack_from('<i', d, off)[0]; off += 4
        if t != 0:
            n = struct.unpack_from('<i', d, off)[0]; off += 4 + 8 + 4 * n
        else:
            n = 1
        for _ in range(n):
            off += 8; name = d[off:off + 16].split(b'\0')[0].decode(); off += 16
            vs = [tuple(d[off + 4 * i:off + 4 * i + 4]) for i in range(nv)]; off += 4 * nv
            frames.append((name, [tuple(v[a] * scale[a] + origin[a] for a in range(3)) for v in vs]))
    return dict(skins=skins, sw=sw, sh=sh, st=st, tris=tris, frames=frames, flags=flags)

def to_obj(path, prefix, palette_path):
    m = parse(path)
    sw, sh = m['sw'], m['sh']
    for name, verts in m['frames']:
        with open('%s_%s.obj' % (prefix, name), 'w') as f:
            f.write('# %s frame %s\n' % (os.path.basename(path), name))
            for v in verts:
                f.write('v %.3f %.3f %.3f\n' % v)
            for (seam, s, t) in m['st']:
                f.write('vt %.5f %.5f\n' % (s / sw, 1.0 - t / sh))
            for front, a, b, c in m['tris']:
                f.write('f %d/%d %d/%d %d/%d\n' % (a + 1, a + 1, c + 1, c + 1, b + 1, b + 1))
    if palette_path:
        from PIL import Image
        pal = load_palette(palette_path)
        for i, sk in enumerate(m['skins']):
            im = Image.new('RGB', (sw, sh)); im.putdata([pal[b] for b in sk]); im.save('%s_skin%d.png' % (prefix, i))

if __name__ == '__main__':
    a = sys.argv[1:]
    pal = a[a.index('--palette') + 1] if '--palette' in a else None
    if a[0] == 'build':
        build(a[1], a[2], pal)
    elif a[0] == 'obj':
        to_obj(a[1], a[2], pal)
    elif a[0] == 'info':
        m = parse(a[1])
        print('%d skins %dx%d, %d verts, %d tris, frames %s, flags %d' % (len(m['skins']), m['sw'], m['sh'], len(m['st']), len(m['tris']), [f[0] for f in m['frames']], m['flags']))
