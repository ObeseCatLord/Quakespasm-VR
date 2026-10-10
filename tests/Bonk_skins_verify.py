#!/usr/bin/env python3
"""Run bounded native skins checks against one completed debug binary.

python3 tests/Bonk_skins_verify.py --graph /private/final-debug-graph \
    --work /private/qualification

Requires GDB with Python and original installed id1/bonkjam PAKs. Does not build
anything. Assets are read-only symlinks; config, logs, report and the actual host
save remain in a fresh private directory outside the checkout and installation.
Runtime qualification should run only after the final implementation is built.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile


PROGRAM_SHA = 'b54e33e50ad06d5628132a26bb6b089534d091bd2b6756799a15b61e7af49811'
ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def pak_program(path):
    """Read only the directory/program, without extracting or rewriting assets."""
    with path.open('rb') as stream:
        header = stream.read(12)
        require(len(header) == 12 and header[:4] == b'PACK', 'invalid original PAK: ' + path.name)
        offset, size = struct.unpack_from('<II', header, 4)
        length = path.stat().st_size
        require(size % 64 == 0 and offset + size <= length, 'invalid PAK directory')
        stream.seek(offset)
        directory = stream.read(size)
        program = None
        for entry in range(0, size, 64):
            name = directory[entry:entry + 56].split(b'\0', 1)[0]
            pos, count = struct.unpack_from('<II', directory, entry + 56)
            require(pos + count <= length, 'invalid PAK member bounds')
            if name == b'progs.dat':
                stream.seek(pos)
                program = stream.read(count)
        return program


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--graph', type=Path, required=True, help='completed -O0 debug graph containing vkquake')
    parser.add_argument('--work', type=Path, required=True, help='private output parent outside repo and installed assets')
    parser.add_argument('--assets', type=Path, default=Path('/home/obesecatlord/Windows/Games/quakespasm_straight'))
    parser.add_argument('--timeout', type=int, default=180, help='bounded total GDB runtime in seconds (1..600)')
    args = parser.parse_args()
    graph, work, assets = (value.resolve() for value in (args.graph, args.work, args.assets))
    require(1 <= args.timeout <= 600, '--timeout must be between 1 and 600 seconds')
    require(not work.is_relative_to(ROOT.parent), '--work must be outside the repository workspace')
    require(not work.is_relative_to(assets), '--work must be outside installed assets/configs/saves')
    binary = graph / 'vkquake'
    require(binary.is_file(), 'missing completed graph binary')
    entries = json.loads((graph / 'compile_commands.json').read_text(encoding='utf-8'))
    entry = next(e for e in entries if e['file'].endswith('Quake/sv_phys.c'))
    flags = shlex.split(entry['command']) if 'command' in entry else entry['arguments']
    require('-O0' in flags and '-D_DEBUG' in flags and '-DNDEBUG' not in flags,
            'native GDB fixture requires the final -O0 debug graph')

    # Validate all inputs before creating outputs. Numeric PAK order matches
    # Quake's override precedence; no loose or rebuilt progs.dat is installed.
    archives = {}
    program = None
    for game in ('id1', 'bonkjam'):
        paths = sorted((assets / game).glob('pak[0-9]*.pak'),
                       key=lambda path: int(path.stem[3:]) if path.stem[3:].isdigit() else -1)
        require(paths and paths[0].name == 'pak0.pak', 'missing original ' + game + '/pak0.pak')
        require(all(path.stem[3:].isdigit() for path in paths), 'noncanonical PAK name')
        archives[game] = paths
        for path in paths:
            data = pak_program(path)
            if game == 'bonkjam' and data is not None:
                program = data
    require(program is not None and len(program) == 684974 and hashlib.sha256(program).hexdigest() == PROGRAM_SHA,
            'original installed Bonk program does not match admitted image')

    work.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix='bonk-skins-', dir=work))
    stage = out / 'assets'
    for game, paths in archives.items():
        dest = stage / game
        dest.mkdir(parents=True)
        for path in paths:
            (dest / path.name).symlink_to(path.resolve())
        # Neutralize loose startup configs so the fixture owns its private
        # configuration even when an original PAK contains a startup config.
        for name in ('config.cfg', 'autoexec.cfg'):
            (dest / name).write_text('// Private native skins fixture.\n', encoding='utf-8')
    for name in ('tmp', 'config', 'cache', 'data', 'runtime'):
        (out / name).mkdir(mode=0o700)
    env = dict(os.environ)
    for name in ('DISPLAY', 'WAYLAND_DISPLAY'):
        env.pop(name, None)
    env.update(TMPDIR=str(out / 'tmp'),
               XDG_CONFIG_HOME=str(out / 'config'), XDG_CACHE_HOME=str(out / 'cache'),
               XDG_DATA_HOME=str(out / 'data'), XDG_RUNTIME_DIR=str(out / 'runtime'),
               SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               BONK_SKINS_RUNTIME=str(ROOT / 'tests/Bonk_skins_runtime.py'),
               BONK_SKINS_STAGE=str(stage), BONK_SKINS_REPORT=str(out / 'report.json'))
    manifest = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'program_sha256': PROGRAM_SHA,
                'original_paks': {game: [path.name for path in paths] for game, paths in archives.items()},
                'fixture_sha256': {name: hashlib.sha256((ROOT / 'tests' / name).read_bytes()).hexdigest()
                                   for name in ('Bonk_skins_runtime.py', 'Bonk_skins_runtime.gdb', 'Bonk_skins_verify.py')}}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    command = ['gdb', '-nx', '-q', '-batch', '-x', str(ROOT / 'tests/Bonk_skins_runtime.gdb'), '--args',
               str(binary), '-nohome', '-basedir', str(stage), '-game', 'bonkjam',
               '-dedicated', '2', '-nolan', '-noudp', '-port', '0',
               '+sv_coop_autosave', '0', '+coop', '1', '+deathmatch', '0', '+map', 'start']
    # The full native output stays private; successful console output is short.
    log = out / 'native.log'
    try:
        with log.open('w', encoding='utf-8') as stream:
            result = subprocess.run(command, cwd=out, env=env, stdin=subprocess.DEVNULL,
                                    stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
        text = log.read_text(encoding='utf-8', errors='replace')
        require(result.returncode == 0 and 'BONK_SKINS_RUNTIME_PASSED' in text,
                'native skins acceptance failed; inspect private native.log')
        report = json.loads((out / 'report.json').read_text(encoding='utf-8'))
        require(report.get('scope_done') is True and report.get('observations'), 'missing native observations')
    except (OSError, RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as error:
        print('BONK_SKINS_FAILED:', error)
        print('Private artifacts:', out)
        return 1
    print('BONK_SKINS_RUNTIME_PASSED:', len(report['observations']), 'observable checks')
    print('Missing coverage:', '; '.join(report['missing_coverage']))
    print('Private artifacts:', out)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
