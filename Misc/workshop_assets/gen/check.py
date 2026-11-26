import json, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from mdltool import read_obj
here = os.environ['WORKSHOP_WORK']
for name in sys.argv[1:]:
    d = json.load(open(os.path.join(here, name + '.mesh.json')))
    f0 = None
    for fname, pos in d['frames']:
        v, uv, f = read_obj(os.path.join(d['dir'], 'frame_%s.obj' % fname))
        assert len(v) == len(pos), (name, fname, len(v), len(pos))
        err = max(abs(a - b) for p, q in zip(v, pos) for a, b in zip(p, q))
        assert len(f) == len(d['tris'])
        for tri, src in zip(f, d['tris']):
            for (vi, ti), (svi, sti) in zip(tri, src):
                assert vi == svi, 'vertex order'
                assert abs(uv[ti][0] - d['uvs'][sti][0]) < 1e-4 and abs(uv[ti][1] - d['uvs'][sti][1]) < 1e-4, 'uv'
        if f0 is None: f0 = f
        assert f == f0, 'face lines differ between frames'
        print(name, fname, 'ok  v=%d f=%d maxerr=%.5f' % (len(v), len(f), err))
