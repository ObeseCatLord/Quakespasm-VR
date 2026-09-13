#!/usr/bin/env python3
"""Generate private bind/frame input for retarget_probe.c; no asset redistribution.

Usage: python3 retarget_probe.py PAK PLAYER_MODELS ALICIA_PACKAGE OUTPUT_HEADER
Only the inspected MD5 animation layout (all six components animated) is supported.
"""
import json
import math
import pathlib
import re
import struct
import sys


def matrix(position, quat):
    x, y, z = quat
    w = -math.sqrt(max(0, 1-x*x-y*y-z*z))
    return [
        [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), position[0]],
        [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), position[1]],
        [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), position[2]],
        [0, 0, 0, 1],
    ]


def multiply(a, b):
    return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def skeleton(text):
    block = re.search(r'joints\s*\{(.*?)\}', text, re.S)[1]
    return [(name, int(parent), matrix(list(map(float, pos.split())), list(map(float, rot.split()))))
            for name, parent, pos, rot in re.findall(r'"([^"]+)"\s+(-?\d+)\s*\((.*?)\)\s*\((.*?)\)', block)]


def values(m):
    return ','.join(format(v, '.9g') for row in m[:3] for v in row)


def main():
    pak, packages, alicia, output = map(pathlib.Path, sys.argv[1:])
    assets = {}
    with pak.open('rb') as f:
        magic, offset, length = struct.unpack('<4sII', f.read(12))
        assert magic == b'PACK'
        f.seek(offset)
        directory = f.read(length)
        for p in range(0, length, 64):
            name, off, size = struct.unpack_from('<56sII', directory, p)
            name = name.split(b'\0')[0].decode()
            if name in ('progs/player.md5mesh', 'progs/player.md5anim'):
                f.seek(off)
                assets[name] = f.read(size).decode()
    source = skeleton(assets['progs/player.md5mesh'])
    anim = assets['progs/player.md5anim']
    hierarchy = re.search(r'hierarchy\s*\{(.*?)\}', anim, re.S)[1]
    joints = [(n, int(p), int(flags), int(start)) for n, p, flags, start in
              re.findall(r'"([^"]+)"\s+(-?\d+)\s+(\d+)\s+(\d+)', hierarchy)]
    assert [(n,p) for n,p,_,_ in joints] == [(n,p) for n,p,_ in source]
    assert all(flags == 63 for _,_,flags,_ in joints)
    frames = []
    for index, block in re.findall(r'frame\s+(\d+)\s*\{(.*?)\}', anim, re.S):
        assert int(index) == len(frames)
        data = list(map(float, block.split()))
        world = []
        for _, parent, _, start in joints:
            m = matrix(data[start:start+3], data[start+3:start+6])
            world.append(multiply(world[parent], m) if parent >= 0 else m)
        frames.append(world)
    assert len(frames) == int(re.search(r'numFrames\s+(\d+)', anim)[1])
    lines = []
    models = [('ranger', source, {})]
    for name, path in [('alicia', alicia)] + [(name, packages/name) for name in ('anzu','chino','marmotranger','qbj3')]:
        cfg = (path/'avatar.cfg').read_text()
        mapping = dict(re.findall(r'^bone\s+(\S+)\s+(\S+)', cfg, re.M))
        assert re.search(r'^scale\s+1\s*$', cfg, re.M)
        models.append((name, skeleton((path/'model.md5mesh').read_text()), mapping))
    for name, bones, _ in models:
        lines.append('static md5livejoint_t '+name+'_joints[]={')
        lines.extend('{'+json.dumps(n)+','+str(p)+',{'+values(m)+'}},' for n,p,m in bones)
        lines.append('};')
    lines.append('static float frames[][26*12]={')
    lines.extend('{'+','.join(values(m) for m in frame)+'},' for frame in frames)
    lines.append('};')
    lines.append('static probe_model_t models[]={')
    semantics = ['Hip','Spine1','Spine2','Neck','Head','Shoulder_L','UpperArm_L','LowerArm_L','Hand_L','Shoulder_R','UpperArm_R','LowerArm_R','Hand_R','UpperLeg_L','LowerLeg_L','Foot_L','UpperLeg_R','LowerLeg_R','Foot_R']
    for name,bones,mapping in models:
        lines.append('{'+json.dumps(name)+','+name+'_joints,'+str(len(bones))+',{'+','.join(json.dumps(mapping.get(s,s)) for s in semantics)+'}},')
    lines.append('};')
    output.write_text('\n'.join(lines)+'\n')
    print(f'Generated {len(frames)} frames and {len(models)} skeletons in {output}')


if __name__ == '__main__':
    main()
