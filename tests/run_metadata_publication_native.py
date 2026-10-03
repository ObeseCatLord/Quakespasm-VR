#!/usr/bin/env python3
"""Run bounded C02 sender/parser cases with disposable writable profiles."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default="/tmp/qsvr-metadata-publication-native")
    parser.add_argument(
        "--basedir",
        type=Path,
        default=Path("/home/obesecatlord/Windows/Games/quakespasm_straight"),
    )
    parser.add_argument("--offers", nargs="+", choices=["qsmi", "predinfo"],
                        default=["qsmi", "predinfo"])
    parser.add_argument("--no-edge-cases", action="store_true",
                        help="run only the selected offer profiles")
    parser.add_argument("--live-admission", action="store_true",
                        help="also run the opt-in connected live refusal/retry case")
    parser.add_argument("--lifecycle", action="store_true",
                        help="run opt-in metadata lifecycle and loaded AUTOCVAR cases")
    parser.add_argument("--lifecycle-progs", type=Path,
                        help="private generated SSQC progs.dat for --lifecycle")
    parser.add_argument("--artifact-root", type=Path,
                        help="write disposable profiles and logs under this new directory")
    parser.add_argument("--timeout", type=int, default=45)
    args = parser.parse_args()
    if not (args.basedir / "id1").is_dir():
        raise SystemExit(f"stock id1 directory is missing: {args.basedir / 'id1'}")
    if args.lifecycle and (args.live_admission or not args.lifecycle_progs):
        parser.error("--lifecycle requires --lifecycle-progs and cannot combine with --live-admission")
    if args.lifecycle and not args.lifecycle_progs.is_file():
        parser.error(f"lifecycle SSQC file is missing: {args.lifecycle_progs}")

    if args.artifact_root:
        root = args.artifact_root.expanduser().resolve()
        root.mkdir(parents=True, exist_ok=False)
    else:
        root = Path(tempfile.mkdtemp(prefix="qsvr-metadata-publication-native-"))
    print(f"logs={root}")
    if args.lifecycle:
        # The legacy PREDINFO profile is the reviewed downgrade/update-only
        # initialization witness; the QSMI profile owns the lifecycle exercise.
        cases = [("qsmi", "none", False), ("predinfo", "none", False)]
    else:
        cases = [(offer, "none", False) for offer in args.offers]
    if not args.lifecycle and not args.no_edge_cases and "qsmi" in args.offers:
        cases.extend([
            ("qsmi", "exact", False),
            ("qsmi", "pressure", False),
            ("qsmi", "none", True),
            ("qsmi", "permanent", False),
        ])

    for offer, limit, control_pressure in cases:
        label = offer + (f"-{limit}" if limit != "none" else "")
        if control_pressure:
            label += "-control"
        profile = root / label
        id1 = profile / "id1"
        id1.mkdir(parents=True)
        for name in ("pak0.pak", "pak1.pak"):
            asset = args.basedir / "id1" / name
            if asset.exists():
                (id1 / name).symlink_to(asset.resolve())
        lifecycle_case = args.lifecycle and offer == "qsmi"
        if lifecycle_case:
            metadata_game = profile / "metadata_fixture"
            metadata_game.mkdir()
            shutil.copyfile(args.lifecycle_progs.resolve(), metadata_game / "progs.dat")
        command = [
            str(Path(args.binary).resolve()), "-dedicated", "16", "-noudp",
            "-nosound", "-nosteamapi", "-basedir", str(profile), "-userdir", str(profile),
            "-metadata-offer", offer, "-metadata-limit", limit,
        ]
        if control_pressure:
            command.append("-control-pressure")
        if lifecycle_case:
            command.extend(["-game", "metadata_fixture"])
            command.append("-metadata-lifecycle")
        elif args.lifecycle and offer == "predinfo":
            command.append("-metadata-downgrade")
        log = profile / "fixture.log"
        try:
            result = subprocess.run(
                command, cwd=profile, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            log.write_text(output)
            print("\n".join(output.splitlines()[-24:]))
            raise SystemExit(f"FAIL offer={offer} timed out; log={log}")
        log.write_text(result.stdout)
        marker = f"METADATA_NATIVE_PASSED offer={offer} limit={limit} "
        lifecycle_marker = "METADATA_LIFECYCLE_PASSED "
        downgrade_marker = "METADATA_DOWNGRADE_INIT_PASSED "
        if (result.returncode or marker not in result.stdout or
                (lifecycle_case and (lifecycle_marker not in result.stdout or
                                     "METADATA_LIVE_ADMISSION_PASSED " not in result.stdout)) or
                (args.lifecycle and offer == "predinfo" and
                 downgrade_marker not in result.stdout)):
            print("\n".join(result.stdout.splitlines()[-32:]))
            raise SystemExit(
                f"FAIL offer={offer} limit={limit} control={control_pressure} "
                f"exit={result.returncode}; log={log}"
            )
        print(next(line for line in result.stdout.splitlines() if marker in line))
        if lifecycle_case:
            print(next(line for line in result.stdout.splitlines()
                       if lifecycle_marker in line))
            print(next(line for line in result.stdout.splitlines()
                       if "METADATA_LIVE_ADMISSION_PASSED " in line))
        elif args.lifecycle and offer == "predinfo":
            print(next(line for line in result.stdout.splitlines()
                       if downgrade_marker in line))

    if args.live_admission:
        offer, limit = "qsmi", "none"
        profile = root / "qsmi-live-admission"
        id1 = profile / "id1"
        id1.mkdir(parents=True)
        for name in ("pak0.pak", "pak1.pak"):
            asset = args.basedir / "id1" / name
            if asset.exists():
                (id1 / name).symlink_to(asset.resolve())
        command = [
            str(Path(args.binary).resolve()), "-dedicated", "16", "-noudp",
            "-nosound", "-nosteamapi", "-basedir", str(profile),
            "-userdir", str(profile), "-metadata-offer", offer,
            "-metadata-limit", limit, "-live-admission",
        ]
        log = profile / "fixture.log"
        try:
            result = subprocess.run(
                command, cwd=profile, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            log.write_text(output)
            print("\n".join(output.splitlines()[-40:]))
            raise SystemExit(f"FAIL live-admission timed out; log={log}")
        log.write_text(result.stdout)
        marker = "METADATA_LIVE_ADMISSION_PASSED "
        native_marker = f"METADATA_NATIVE_PASSED offer={offer} limit={limit} "
        if (result.returncode or marker not in result.stdout or
                native_marker not in result.stdout):
            print("\n".join(result.stdout.splitlines()[-48:]))
            raise SystemExit(
                f"FAIL live-admission offer={offer} limit={limit} "
                f"exit={result.returncode}; log={log}"
            )
        print(next(line for line in result.stdout.splitlines() if marker in line))


if __name__ == "__main__":
    main()
