#!/usr/bin/env python3
"""Build/run only the slope diagnostic using a prepared native graph.

No Ninja, shared object writes, live server, or installed-game configuration
writes. Engine libraries/remaining objects must already be prepared. Default
regression mode fails on no-jump parity or authoritative jump contract errors;
exact generic QC jump replay timing remains a separate metric. --diagnostic
records violations without claiming parity. --swim-only uses actual depth2/3
water on the supplied map. Exit 77 means required real geometry/evidence is missing.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from run_csqc_entity_native import native_link_recipe, private_loader_paths, remove_depfile_args

REPO = Path(__file__).resolve().parents[1]


def compile_recipe(graph, owner, source, output):
    entries = json.loads((graph / 'compile_commands.json').read_text())
    entry = next(item for item in entries if item['file'].endswith('/Quake/' + owner))
    args = remove_depfile_args(entry.get('arguments') or shlex.split(entry['command']))
    if '-D_DEBUG' not in args or any(arg.startswith('-DNDEBUG') for arg in args):
        raise RuntimeError('requires an assertion-enabled Debug graph')
    if '-include-pch' in args:
        index = args.index('-include-pch')
        del args[index:index + 2]
    original_root = (Path(entry['directory']) / entry['file']).resolve().parent.parent
    for index, argument in enumerate(args):
        if argument.startswith('-I'):
            include = (Path(entry['directory']) / argument[2:]).resolve()
            if include.is_relative_to(original_root):
                args[index] = '-I' + str(REPO / include.relative_to(original_root))
    args[args.index('-c') + 1] = str(source)
    args[args.index('-o') + 1] = str(output)
    return args, entry['directory']


def build(graph, root):
    objects = []
    # Fixture includes current sv_main/cl_demo/cl_parse/sv_phys/cl_main owners;
    # compile current PMove too, so the suspect policy never uses an old object.
    for owner, source, name in (
        ('sv_user.c', REPO / 'tests/slope_reconciliation_native_fixture.c', 'slope'),
        ('pmove.c', REPO / 'Quake/pmove.c', 'pmove'),
    ):
        output = root / (name + '.o')
        args, directory = compile_recipe(graph, owner, source, output)
        subprocess.run(args, cwd=directory, check=True)
        objects.append(str(output))
    binary = root / 'slope-reconciliation'
    link = native_link_recipe(graph, binary)
    owners = {'main_sdl', 'sv_main', 'cl_demo', 'cl_parse', 'sv_phys', 'cl_main', 'pmove'}
    removed = {'vkquake.p/Quake_' + owner + '.c.o' for owner in owners}
    if any(link.count(item) != 1 for item in removed):
        raise RuntimeError('unexpected native owner graph')
    link = [arg for arg in link if arg not in removed]
    if any(not (graph / arg).is_file() for arg in link if arg.endswith('.o')):
        raise RuntimeError('requires already-built engine objects; no full build allowed')
    # Preserve the native C++ link driver for the OpenXR engine object.
    link[1:1] = objects
    link.extend('-Wl,--wrap=' + symbol for symbol in (
        'Loop_Init', 'NET_CanSendMessage', 'NET_SendUnreliableMessage',
        'R_TranslateNewPlayerSkin', 'PR_ExecuteProgram', 'PM_PlayerMove',
    ))
    subprocess.run(link, cwd=graph, check=True)
    return binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True,
                        help='prepared assertion-enabled native Meson/Ninja graph (never built here)')
    parser.add_argument('--assets', type=Path, required=True,
                        help='licensed asset base containing id1/pak0.pak and installed game directories')
    parser.add_argument('--artifact-root', type=Path)
    parser.add_argument('--game', default='honey')
    parser.add_argument('--map', default='start')
    parser.add_argument('--client-msec', type=int, choices=(4, 8, 16), nargs='+', default=(8,))
    parser.add_argument('--diagnostic', action='store_true')
    parser.add_argument('--vr-command', action='store_true',
                        help='prepared accepted VR command fields/durations; no physical headset claim')
    parser.add_argument('--landing-only', action='store_true',
                        help='only uphill launch/landing/rejump with16ms commands (no matrix)')
    parser.add_argument('--swim-only', action='store_true', help='only the actual QC water impulse probe')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--timeout', type=int, default=90)
    args = parser.parse_args()
    for label, name in (('game', args.game), ('map', args.map)):
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*', name):
            parser.error('--' + label + ' must be a simple installed identifier, without path separators')
    if args.landing_only and args.swim_only:
        parser.error('--landing-only and --swim-only are mutually exclusive')
    graph = args.build_dir.resolve(strict=True)
    assets = args.assets.resolve(strict=True)
    if not (assets / 'id1/pak0.pak').is_file():
        raise SystemExit('missing licensed id1/pak0.pak')
    game_directory = assets / args.game
    if not game_directory.is_dir():
        raise SystemExit('missing installed game directory: ' + args.game)
    # The native filesystem owns loose/PAK lookup and actual program/map loading.
    if args.artifact_root:
        root = args.artifact_root.resolve()
        if root.is_relative_to(REPO):
            raise SystemExit('runtime artifacts must stay outside this checkout')
    else:
        if Path(tempfile.gettempdir()).resolve().is_relative_to(REPO):
            raise SystemExit('temporary directory must stay outside this checkout')
        root = Path(tempfile.mkdtemp(prefix='qsvr-slope-native-'))
    if args.artifact_root:
        root.mkdir(parents=True, exist_ok=False)
    print('SLOPE_ARTIFACT_ROOT', root, flush=True)
    binary = build(graph, root)
    if args.build_only:
        print('SLOPE_BUILD_ONLY_PASSED', binary)
        return 0
    result = 0
    environment = os.environ.copy()
    loader_paths = private_loader_paths(graph)
    if loader_paths:
        environment['LD_LIBRARY_PATH'] = ':'.join(loader_paths + [environment.get('LD_LIBRARY_PATH', '')])
    for msec in ((16,) if args.landing_only else args.client_msec):
        profile = root / ('profile-' + str(msec))
        (profile / 'id1').mkdir(parents=True)
        (profile / 'id1/pak0.pak').symlink_to(assets / 'id1/pak0.pak')
        (profile / args.game).symlink_to(game_directory, target_is_directory=True)
        userdir = root / ('user-' + str(msec))
        userdir.mkdir()
        command = [str(binary), '-selected', '-dedicated', '3', '-noudp', '-nosound',
                   '-basedir', str(profile), '-userdir', str(userdir), '-game', args.game,
                   '-fixturemap', args.map, '-fixturemsec', str(msec)]
        if args.diagnostic:
            command.append('-diagnostic')
        if args.landing_only:
            command.append('-landingonly')
        if args.swim_only:
            command.append('-swimonly')
        if args.vr_command:
            command.append('-vrcommand')
        log = root / ('slope-' + str(msec) + '.log')
        with log.open('w') as stream:
            completed = subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
                                       timeout=args.timeout, env=environment)
        print('SLOPE_RUN client_ms=' + str(msec), 'exit=' + str(completed.returncode), 'log=' + str(log))
        for line in log.read_text(errors='replace').splitlines():
            if line.startswith(('SLOPE_PROGRAM ', 'SLOPE_CASE ', 'SLOPE_RECONCILIATION_', 'SLOPE_MISSING_',
                                'SLOPE_LANDING ', 'SLOPE_REJUMP ', 'SLOPE_SWIM_CONTRACT ')):
                print(line)
        if completed.returncode:
            result = 77 if completed.returncode == 77 and result == 0 else 1
    return result


if __name__ == '__main__':
    raise SystemExit(main())
