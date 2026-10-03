#!/usr/bin/env python3
"""Run local/load cases with disposable writable profiles and read-only assets."""
import argparse
from contextlib import nullcontext
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="/tmp/qsvr-local-load-native-fixture")
    parser.add_argument("--basedir", required=True, type=Path)
    parser.add_argument("--game", type=Path, help="Optional cooperative QC directory")
    parser.add_argument("--keep-profiles", type=Path,
                        help="Keep scenario profiles and native logs below this directory")
    parser.add_argument("--cases", nargs="+", default=[
        "local", "disabled", "public", "fastload", "autofastload",
        "public-fastload", "public-autofastload", "pending", "pending-ground"])
    args = parser.parse_args()
    if args.keep_profiles:
        args.keep_profiles.mkdir(parents=True, exist_ok=True)
    for scenario in args.cases:
        if args.keep_profiles:
            directory = tempfile.mkdtemp(prefix=f"qsvr-local-load-{scenario}-",
                                         dir=args.keep_profiles)
            profile = nullcontext(directory)
        else:
            profile = tempfile.TemporaryDirectory(prefix="qsvr-local-load-")
        with profile as directory:
            root = Path(directory)
            # Symlink licensed asset files individually; save files stay in this profile.
            id1 = root / "id1"
            id1.mkdir()
            for name in ["pak0.pak", "pak1.pak"]:
                asset = args.basedir / "id1" / name
                if asset.exists():
                    (id1 / name).symlink_to(asset.resolve())
            command = [args.binary, "-dedicated",
                       "2" if scenario.startswith("pending") or scenario == "autosave" else "1",
                       "-noudp", "-nosound", "-basedir", directory, "-userdir", directory,
                       "-localcase", scenario]
            if scenario == "autosave":
                command.append("-nosteamapi")
            if args.game:
                game = root / "cooperative"
                game.mkdir()
                (game / "progs.dat").symlink_to((args.game / "progs.dat").resolve())
                command += ["-game", "cooperative"]
            result = subprocess.run(command, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=25)
            if args.keep_profiles:
                (root / "native-run.log").write_text(result.stdout)
            marker = f"LOCAL_LOAD_NATIVE_PASSED case={scenario} "
            markers = [marker]
            if scenario == "autosave":
                markers.append("COOP_AUTOSAVE_NATIVE_PASSED ")
            if result.returncode or any(required not in result.stdout for required in markers):
                print(result.stdout)
                if args.keep_profiles:
                    print(f"PRESERVED_PROFILE={root}")
                raise SystemExit(f"FAIL case={scenario} exit={result.returncode}")
            print(f"PASS case={scenario} game={'cooperative' if args.game else 'stock'}")
            if args.keep_profiles:
                print(f"PRESERVED_PROFILE={root}")


if __name__ == "__main__":
    main()
