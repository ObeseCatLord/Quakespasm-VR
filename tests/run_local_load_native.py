#!/usr/bin/env python3
"""Run local/load cases with disposable writable profiles and read-only assets."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="/tmp/qsvr-local-load-native-fixture")
    parser.add_argument("--basedir", required=True, type=Path)
    parser.add_argument("--game", type=Path, help="Optional cooperative QC directory")
    parser.add_argument("--cases", nargs="+", default=[
        "local", "disabled", "public", "fastload", "autofastload",
        "public-fastload", "public-autofastload", "pending", "pending-ground"])
    args = parser.parse_args()
    for scenario in args.cases:
        with tempfile.TemporaryDirectory(prefix="qsvr-local-load-") as directory:
            root = Path(directory)
            # Symlink asset files individually; save files stay in this profile.
            id1 = root / "id1"
            id1.mkdir()
            for name in ["pak0.pak", "pak1.pak"]:
                asset = args.basedir / "id1" / name
                if asset.exists():
                    (id1 / name).symlink_to(asset.resolve())
            command = [args.binary, "-dedicated", "2" if scenario.startswith("pending") else "1",
                       "-noudp", "-nosound", "-basedir", directory, "-userdir", directory,
                       "-localcase", scenario]
            if args.game:
                game = root / "cooperative"
                game.mkdir()
                (game / "progs.dat").symlink_to((args.game / "progs.dat").resolve())
                command += ["-game", "cooperative"]
            result = subprocess.run(command, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=25)
            marker = f"LOCAL_LOAD_NATIVE_PASSED case={scenario} "
            if result.returncode or marker not in result.stdout:
                print(result.stdout)
                raise SystemExit(f"FAIL case={scenario} exit={result.returncode}")
            print(f"PASS case={scenario} game={'cooperative' if args.game else 'stock'}")


if __name__ == "__main__":
    main()
