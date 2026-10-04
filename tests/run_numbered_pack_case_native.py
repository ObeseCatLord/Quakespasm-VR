#!/usr/bin/env python3
"""Check production numbered-pack resolution with native platform enumeration."""
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
    root = args.artifact_root.resolve() if args.artifact_root else Path(tempfile.mkdtemp(prefix='qsvr-pack-case-'))
    if root.is_relative_to(REPO):
        parser.error('artifacts must stay outside the checkout')
    if args.artifact_root:
        root.mkdir(parents=True, exist_ok=False)
    obj = root / 'pack-case.o'
    command, cwd = compile_recipe(graph, 'sv_user.c', REPO / 'tests/numbered_pack_case_native_fixture.c', obj)
    subprocess.run(command, cwd=cwd, check=True)
    binary = root / 'pack-case'
    link = native_link_recipe(graph, binary)
    removed = {'vkquake.p/Quake_main_sdl.c.o', 'vkquake.p/Quake_common.c.o'}
    if any(link.count(x) != 1 for x in removed):
        raise RuntimeError('unexpected prepared owner graph')
    link = [x for x in link if x not in removed]
    link[1:1] = [str(obj)]
    subprocess.run(link, cwd=graph, check=True)
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = ':'.join(private_loader_paths(graph) + [env.get('LD_LIBRARY_PATH', '')])
    subprocess.run([str(binary), str(root / 'files')], env=env, check=True, timeout=15)
    print('NUMBERED_PACK_ARTIFACT_ROOT', root)

if __name__ == '__main__':
    main()
