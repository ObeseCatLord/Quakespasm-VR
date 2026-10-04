#!/usr/bin/env python3
"""Use one final debug graph read-only. Keep fixtures/assets/logs in the private workspace.

python3 tests/Bonk_verify.py --graph /path/to/final-debug-graph
Requires original installed id1/bonkjam PAKs, Clang/GDB and that graph's dependencies.
No QuakeC, proprietary asset or generated model is committed.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shlex
import struct
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--assets', type=pathlib.Path, default=pathlib.Path('/home/obesecatlord/Windows/Games/quakespasm_straight'))
p.add_argument('--graph', type=pathlib.Path, required=True)
p.add_argument('--native-only', action='store_true')
a = p.parse_args()
graph = a.graph.resolve()
out = root / 'tests/Bonk/build/qualification'
out.mkdir(parents=True, exist_ok=True)
entries = json.loads((graph / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('Quake/sv_user.c'))
source = (graph / entry['file']).resolve().parents[1]
assert (graph / 'vkquake').is_file()
flags = shlex.split(entry['command'])
compiler = flags.pop(0)
filtered = []
i = 0
while i < len(flags):
    flag = flags[i]
    if flag in ('-include-pch', '-MQ', '-MF', '-o'):
        i += 2
        continue
    if flag in ('-MD', '-c') or flag == entry['file']:
        i += 1
        continue
    if flag.startswith('-I') and not pathlib.Path(flag[2:]).is_absolute():
        flag = '-I' + str((graph / flag[2:]).resolve())
    filtered.append(flag)
    i += 1
assert '-O0' in filtered and '-Werror' in filtered and '-D_DEBUG' in filtered and '-DNDEBUG' not in filtered
print('BONK_FINAL_GRAPH', graph)
print('BONK_FINAL_SOURCE_HEAD', subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip())
print('BONK_FINAL_BINARY_SHA256', hashlib.sha256((graph / 'vkquake').read_bytes()).hexdigest())


def execute(command, name, timeout=90):
    result = subprocess.run(command, cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=timeout)
    (out / (name + '.log')).write_text(result.stdout)
    print(result.stdout[-4500:])
    assert result.returncode == 0, 'See private ' + name + '.log'
    return result.stdout


stage = out / 'assets'
models = {}
program = None
for game in ('id1', 'bonkjam'):
    dest = stage / game
    dest.mkdir(parents=True, exist_ok=True)
    for pak in sorted((a.assets / game).glob('pak*.pak')):
        target = dest / pak.name
        if not target.exists():
            target.symlink_to(pak.resolve())
        if game != 'bonkjam':
            continue
        data = pak.read_bytes()
        assert data[:4] == b'PACK'
        offset, size = struct.unpack_from('<II', data, 4)
        for start in range(offset, offset + size, 64):
            name = data[start:start + 56].split(b'\0', 1)[0].decode('ascii')
            pos, length = struct.unpack_from('<II', data, start + 56)
            if name == 'progs.dat':
                program = data[pos:pos + length]
            if name.startswith('progs/v_hammer_') and name.endswith('.mdl'):
                models[name] = data[pos:pos + length]
    assert (dest / 'pak0.pak').is_file(), 'Missing installed ' + game
assert program and hashlib.sha256(program).hexdigest() == 'b54e33e50ad06d5628132a26bb6b089534d091bd2b6756799a15b61e7af49811'
# Installed archive also contains three unused historical aliases. Native selector
# and donor recipes use only the following 29 canonical models.
for alias in ('baseball_bat', 'baseball_bat_bloody', 'blocky_brown_brick'):
    models.pop('progs/v_hammer_' + alias + '.mdl', None)
assert len(models) == 29
(out / 'tmp').mkdir(exist_ok=True)
env = dict(os.environ, TMPDIR=str(out / 'tmp'))

if not a.native_only:
    # Fixture copies include the exact final graph's headers and production input source.
    for name in ('private_usercmd_fixture.c', 'vr_input_fixture.c', 'Bonk_codec_fixture.c', 'Bonk_geometry_fixture.c', 'Bonk_input_fixture.c'):
        text = (root / 'tests' / name).read_text().replace('../Quake/', str(source / 'Quake') + '/')
        (out / name).write_text(text)
    outputs = {}
    for name in ('cl_input', 'sv_user', 'common', 'mathlib', 'gl_model', 'vr_locomotion', 'strlcpy'):
        obj = out / (name + '.o')
        execute([compiler, *filtered, '-ffunction-sections', '-fdata-sections', '-c', str(source / 'Quake' / (name + '.c')), '-o', str(obj)], 'compile_' + name)
        outputs[name] = str(obj)
    sdl = 'sdl3' if '-DUSE_SDL3' in filtered else 'sdl2'
    libs = shlex.split(subprocess.check_output(['pkg-config', '--libs', sdl], text=True))
    fixtures = {'codec': ('cl_input', 'sv_user', 'common', 'mathlib'),
                'geometry': ('gl_model', 'common', 'mathlib', 'strlcpy'),
                'input': ('vr_locomotion', 'mathlib')}
    for name, objects in fixtures.items():
        execute([compiler, *filtered, '-ffunction-sections', '-fdata-sections', str(out / ('Bonk_' + name + '_fixture.c')),
                 *(outputs[o] for o in objects), '-Wl,--gc-sections', *libs, '-lm', '-o', str(out / name)], 'link_' + name)
        args = [str(out / name)]
        if name == 'geometry':
            for path, data in sorted(models.items()):
                dest = out / 'models' / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                args.append(str(dest))
        execute(args, name)

command = ['gdb', '-nx', '-q', '-batch', '-x', str(root / 'tests/Bonk_runtime.gdb'), '--args',
           str(graph / 'vkquake'), '-nohome', '-basedir', str(stage), '-game', 'bonkjam',
           '-dedicated', '2', '-port', '0', '+sv_coop_autosave', '0', '+coop', '1', '+map', 'start']
text = execute(command, 'native')
assert 'BONK_RUNTIME_PASSED' in text, 'Native acceptance marker missing'
