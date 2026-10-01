#!/usr/bin/env python3
"""Run bounded C02 sender/parser cases with disposable writable profiles."""
import argparse
from pathlib import Path
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
    parser.add_argument("--timeout", type=int, default=45)
    args = parser.parse_args()
    if not (args.basedir / "id1").is_dir():
        raise SystemExit(f"stock id1 directory is missing: {args.basedir / 'id1'}")

    root = Path(tempfile.mkdtemp(prefix="qsvr-metadata-publication-native-"))
    print(f"logs={root}")
    cases = [(offer, "none", False) for offer in args.offers]
    if not args.no_edge_cases and "qsmi" in args.offers:
        cases.extend([
            ("qsmi", "exact", False),
            ("qsmi", "pressure", False),
            ("qsmi", "none", True),
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
        command = [
            str(Path(args.binary).resolve()), "-dedicated", "16", "-noudp",
            "-nosound", "-basedir", str(profile), "-userdir", str(profile),
            "-metadata-offer", offer, "-metadata-limit", limit,
        ]
        if control_pressure:
            command.append("-control-pressure")
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
        if result.returncode or marker not in result.stdout:
            print("\n".join(result.stdout.splitlines()[-32:]))
            raise SystemExit(
                f"FAIL offer={offer} limit={limit} control={control_pressure} "
                f"exit={result.returncode}; log={log}"
            )
        print(next(line for line in result.stdout.splitlines() if marker in line))


if __name__ == "__main__":
    main()
