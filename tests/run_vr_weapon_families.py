#!/usr/bin/env python3
"""Geometry weapon-family regression over every installed mod (read-only).

For each game directory under --base, run the dedicated engine in a private
symlinked copy and list `vr_weapon_families`. Checks that
  * every progs.dat viewmodel path whose ready-frame mesh is in
    Quake/vr_weapon_families.h is matched (C and Python signatures agree), and
  * every family barrel muzzle equals the family tip carried through the
    renderer's held transform (R_AliasModelMatrixInternal at default scale).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Misc/vr-weapon-families'))
from quakefs import game_files, parse_mdl, read, signature  # noqa: E402

C = 1 / 0.75
ROW = re.compile(r'\{(0x[0-9a-f]+)ull, (true|false), \{([^}]*)\}, ([-0-9.]+)f, (true|false), \{([^}]*)\}')
LINE = re.compile(r'^  (\S+): family (\d+) held (\S+) (\S+) (\S+) scale (\S+) muzzle (\S+) (\S+) (\S+)( \(barrel\))?$', re.M)


def floats(text):
    return [float(x.rstrip('f')) for x in text.split(',')]


def families():
    out = []
    for m in ROW.finditer((ROOT / 'Quake/vr_weapon_families.h').read_text()):
        out.append(dict(sig=int(m.group(1), 16), tip=floats(m.group(6)) if m.group(5) == 'true' else None))
    return out


def candidates(progs):
    for raw in progs.split(b'\0'):
        if not raw or len(raw) >= 64:
            continue
        try:
            text = raw.decode('ascii')
        except UnicodeDecodeError:
            continue
        base = text.rsplit('/', 1)[-1].lower()
        if text.lower().startswith('progs/') and text.lower().endswith('.mdl') and len(text) >= 12 and \
                (base.startswith('v_') or '_v_' in base):
            yield text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, default=Path(os.environ.get(
        'QSVR_GAME_BASE', str(Path.home() / 'Windows/Games/quakespasm_straight'))))
    parser.add_argument('--binary', type=Path, default=ROOT / 'build-debug/vkquake')
    args = parser.parse_args()
    table = families()
    by_sig = {f['sig']: i for i, f in enumerate(table)}
    games = sorted(d.name for d in args.base.iterdir()
                   if d.is_dir() and not d.name.startswith('.') and
                   ((d / 'progs.dat').exists() or any(x.suffix.lower() == '.pak' for x in d.iterdir())))
    id1 = game_files(args.base / 'id1')
    matched = barrels = 0
    with tempfile.TemporaryDirectory(prefix='qsvr-families-') as tmp:
        for game in games:
            os.makedirs(Path(tmp) / game, exist_ok=True)
            for entry in (args.base / game).iterdir():
                if entry.suffix.lower() not in ('.cfg', '.sav', '.txt') or entry.name.lower() == 'vr_weapons.txt':
                    os.symlink(entry, Path(tmp) / game / entry.name)
        for game in games:
            files = dict(id1)
            if game != 'id1':
                files.update(game_files(args.base / game))
            if 'progs.dat' not in files:
                continue
            expected = {}
            for path in set(candidates(read(files['progs.dat']))):
                entry = files.get(path.lower())
                model = parse_mdl(read(entry)) if entry else None
                if model and signature(model) in by_sig:
                    expected[path] = (by_sig[signature(model)], model)
            cmd = [str(args.binary), '-dedicated', '-basedir', tmp] + (['-game', game] if game != 'id1' else []) + \
                ['+vr_weapon_families', '+quit']
            log = subprocess.run(cmd, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=120).stdout
            got = {m.group(1): m for m in LINE.finditer(log)}
            assert set(got) == set(expected), f'{game}: engine {sorted(set(got) ^ set(expected))}'
            for path, m in got.items():
                index, model = expected[path]
                assert int(m.group(2)) == index, f'{game}:{path} family {m.group(2)} != {index}'
                matched += 1
                tip = table[index]['tip']
                if not m.group(10):
                    continue
                assert tip, f'{game}:{path} barrel muzzle without a tip'
                held = [float(m.group(i)) for i in (3, 4, 5)]
                scale = float(m.group(6))
                muzzle = [float(m.group(i)) for i in (7, 8, 9)]
                p = [C * (scale * tip[a] + held[a] + (1 - scale) * model['origin'][a]) for a in range(3)]
                want = (-p[1], p[2], p[0])
                assert max(abs(want[a] - muzzle[a]) for a in range(3)) < 0.01, f'{game}:{path} muzzle {muzzle} != {want}'
                barrels += 1
    print(f'VR_WEAPON_FAMILIES_PASS {len(games)} games, {matched} matched viewmodels, {barrels} barrel muzzles')


if __name__ == '__main__':
    main()
