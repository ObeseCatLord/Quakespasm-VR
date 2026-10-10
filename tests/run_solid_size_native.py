#!/usr/bin/env python3
"""Exercise actual solid-bound writer/decoder using a prepared Debug graph."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from run_slope_reconciliation_native import compile_recipe
from run_csqc_entity_native import native_link_recipe, private_loader_paths

REPO = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--artifact-root', type=Path)
    args = parser.parse_args()
    graph = args.build_dir.resolve(strict=True)
    root = (args.artifact_root.resolve() if args.artifact_root else
            Path(tempfile.mkdtemp(prefix='qsvr-solid-native-')))
    if root.is_relative_to(REPO):
        parser.error('artifacts must stay outside the checkout')
    if args.artifact_root:
        root.mkdir(parents=True, exist_ok=False)
    output = root / 'solid.o'
    command, directory = compile_recipe(graph, 'sv_user.c',
        REPO / 'tests/solid_size_roundtrip_native_fixture.c', output)
    subprocess.run(command, cwd=directory, check=True)
    binary = root / 'solid-roundtrip'
    link = native_link_recipe(graph, binary)
    removed = {'vkquake.p/Quake_' + owner + '.c.o' for owner in
               ('main_sdl', 'sv_main', 'cl_demo', 'cl_parse')}
    if any(link.count(item) != 1 for item in removed):
        raise RuntimeError('unexpected prepared owner graph')
    link = [arg for arg in link if arg not in removed]
    link[1:1] = [str(output)]
    link.extend(('-Wl,--wrap=Loop_Init', '-Wl,--wrap=NET_CanSendMessage'))
    subprocess.run(link, cwd=graph, check=True)
    environment = os.environ.copy()
    environment['LD_LIBRARY_PATH'] = ':'.join(private_loader_paths(graph) +
                                            [environment.get('LD_LIBRARY_PATH', '')])
    subprocess.run([str(binary)], env=environment, check=True, timeout=15)
    print('SOLID_ARTIFACT_ROOT', root)


if __name__ == '__main__':
    main()
