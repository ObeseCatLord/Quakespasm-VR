#!/usr/bin/env python3
"""Code-defined VR body rig regression (read-only on the game tree).

For each pick in Misc/vr-player-rigs/calibrations.py whose game directory is
installed under --base, run the engine in a private symlinked base, build the
rig with `vr_player_rigs build <model>` and dump it. Checks that
  * the header row exists and its signature matches the installed mesh,
  * the engine builds the rig and the custom-avatar loader admits it, and
  * the dumped MD5 is self-consistent and every weight sums to one.
Needs a display (use Xvfb) and the official rerelease id1/pak0.pak.
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
sys.path.insert(0, str(ROOT / 'Misc/vr-player-rigs'))
from quakefs import game_files, parse_mdl, read, signature  # noqa: E402
from calibrations import PICKS  # noqa: E402

SKIP = re.compile(r'\.(cfg|sav|dem|log|txt|rc)$|\.bak', re.I)


def private_base(base, games, tmp):
    for game in set(games) | {'id1'}:
        src, dst = base / game, tmp / game.replace('/', '_')
        dst.mkdir(parents=True, exist_ok=True)
        for entry in src.iterdir():
            if SKIP.search(entry.name) or (entry.is_dir() and entry.name.lower() not in ('progs', 'maps', 'gfx', 'sound')):
                continue
            (dst / entry.name).symlink_to(entry)


def check_md5(path):
    text = path.read_text()
    counts = {k: int(re.search(r'\b%s (\d+)' % k, text).group(1)) for k in ('numverts', 'numtris', 'numweights')}
    verts = re.findall(r'\bvert \d+ \( \S+ \S+ \) (\d+) (\d+)', text)
    weights = [float(w) for w in re.findall(r'\bweight \d+ \d+ (\S+) \(', text)]
    tris = re.findall(r'\btri \d+ (\d+) (\d+) (\d+)', text)
    assert (len(verts), len(tris), len(weights)) == (counts['numverts'], counts['numtris'], counts['numweights']), counts
    for start, count in verts:
        total = sum(weights[int(start):int(start) + int(count)])
        assert abs(total - 1) < 1e-4, total
    assert all(int(i) < counts['numverts'] for t in tris for i in t)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, default=Path(os.environ.get(
        'QSVR_GAME_BASE', str(Path.home() / 'Windows/Games/quakespasm_straight'))))
    parser.add_argument('--binary', type=Path, default=ROOT / 'build-debug/vkquake')
    args = parser.parse_args()
    header = (ROOT / 'Quake/vr_player_rigs.h').read_text()
    picks = [p for p in PICKS if (args.base / p.game).is_dir()]
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        private_base(args.base, [p.game for p in picks], tmp)
        for pick in picks:
            sig = signature(parse_mdl(read(game_files(str(args.base / pick.game))[pick.model])))
            game = pick.game.replace('/', '_')
            out = subprocess.run([str(args.binary), '-basedir', str(tmp), '-userdir', str(tmp), '-rerelease', str(tmp),
                                  '-game', game, '-window', '-width', '320', '-height', '200',
                                  '+vr_player_rigs build ' + pick.model, '+vr_player_rigs dump', '+quit'],
                                 capture_output=True, text=True, timeout=180,
                                 env=dict(os.environ, SDL_AUDIODRIVER='disk', SDL_DISKAUDIOFILE=os.devnull)).stdout
            problems = []
            if '0x%016xull, "%s"' % (sig, pick.key) not in header:
                problems.append('header signature differs from the installed mesh')
            if '%s: built' % pick.model not in out:
                problems.append('engine did not build it: ' + ' | '.join(
                    l for l in out.splitlines() if 'rig' in l or 'avatar' in l.lower())[-300:])
            else:
                try:
                    counts = check_md5(tmp / 'vr_player_rigs' / pick.key / 'model.md5mesh')
                except Exception as error:  # noqa: BLE001
                    problems.append('dumped mesh invalid: %r' % error)
            print('%-6s %-28s %s' % (pick.key, pick.game + '/' + pick.model,
                                     'FAIL ' + '; '.join(problems) if problems else
                                     'ok (%(numverts)d verts, %(numtris)d tris)' % counts))
            failures += bool(problems)
    print('%d of %d rigs failed' % (failures, len(picks)))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
