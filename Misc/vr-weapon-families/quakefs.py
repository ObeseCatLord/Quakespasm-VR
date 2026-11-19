"""Read-only Quake game-directory and MDL helpers for the family generator."""
import hashlib
import math
import os
import re
import struct


def _pak_order(name):
    match = re.match(r'pak(\d+)\.pak$', name.lower())
    return (0, int(match.group(1))) if match else (1, name.lower())


def pak_entries(path):
    out = {}
    with open(path, 'rb') as f:
        header = f.read(12)
        if header[:4] != b'PACK':
            return out
        offset, length = struct.unpack('<ii', header[4:])
        f.seek(offset)
        directory = f.read(length)
    for i in range(length // 64):
        name = directory[i * 64:i * 64 + 56].split(b'\0')[0].decode('latin1').lower()
        pos, size = struct.unpack('<ii', directory[i * 64 + 56:i * 64 + 64])
        out[name] = (path, pos, size)
    return out


def game_files(directory):
    """Effective files of one game directory: numbered paks, then loose files."""
    files = {}
    paks = sorted((x for x in os.listdir(directory) if x.lower().endswith('.pak')), key=_pak_order)
    for pak in paks:
        files.update(pak_entries(os.path.join(directory, pak)))
    for root, _, names in os.walk(os.path.join(directory, 'progs')):
        for name in names:
            full = os.path.join(root, name)
            files[os.path.relpath(full, directory).replace(os.sep, '/').lower()] = (full, 0, os.path.getsize(full))
    for name in os.listdir(directory):
        full = os.path.join(directory, name)
        if os.path.isfile(full) and name.lower() in ('progs.dat', 'vr_weapons.txt'):
            files[name.lower()] = (full, 0, os.path.getsize(full))
    return files


def read(entry):
    path, pos, size = entry
    with open(path, 'rb') as f:
        f.seek(pos)
        return f.read(size)


def parse_mdl(data):
    """Version 6 MDL: header, triangles and every frame's raw vertices."""
    if data[:4] != b'IDPO' or struct.unpack_from('<i', data, 4)[0] != 6:
        return None
    scale = struct.unpack_from('<3f', data, 8)
    origin = struct.unpack_from('<3f', data, 20)
    numskins, skinw, skinh, numverts, numtris, numframes = struct.unpack_from('<6i', data, 48)
    p = 84
    for _ in range(numskins):
        kind, = struct.unpack_from('<i', data, p)
        p += 4
        if kind == 0:
            p += skinw * skinh
        else:
            count, = struct.unpack_from('<i', data, p)
            p += 4 + 4 * count + count * skinw * skinh
    p += numverts * 12
    tris = [struct.unpack_from('<4i', data, p + 16 * i) for i in range(numtris)]
    p += numtris * 16
    frames = []
    for _ in range(numframes):
        kind, = struct.unpack_from('<i', data, p)
        p += 4
        count = 1
        if kind:
            count, = struct.unpack_from('<i', data, p)
            p += 4 + 8 + 4 * count
        for _ in range(count):
            p += 8 + 16
            frames.append([struct.unpack_from('<3B', data, p + 4 * i) for i in range(numverts)])
            p += 4 * numverts
    return dict(scale=scale, origin=origin, numverts=numverts, numtris=numtris, tris=tris, frames=frames)


def decode(model, frame=0):
    s, o = model['scale'], model['origin']
    return [tuple(o[a] + s[a] * v[a] for a in range(3)) for v in model['frames'][frame]]


def geometry_key(model):
    """Generator-side grouping key (ready-frame positions to 1/100 unit)."""
    h = hashlib.sha1(f"{model['numverts']},{model['numtris']}".encode())
    for q in decode(model):
        h.update(('%.2f,%.2f,%.2f;' % q).encode())
    return h.hexdigest()[:12]


def signature(model):
    """Must match VR_WeaponFamilySignature in Quake/vr_weapon_calibration.c."""
    h = 0xcbf29ce484222325

    def mix(value):
        nonlocal h
        for byte in struct.pack('<i', value):
            h = ((h ^ byte) * 0x100000001b3) & 0xffffffffffffffff

    mix(model['numverts'])
    mix(model['numtris'])
    s, o = model['scale'], model['origin']
    for v in model['frames'][0]:
        for a in range(3):
            mix(int(math.floor((o[a] + s[a] * v[a]) * 8.0 + 0.5)))
    return h
