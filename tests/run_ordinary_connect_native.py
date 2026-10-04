#!/usr/bin/env python3
"""Build and run the deferred CAND-NET-003 native transport fixture.

The runner creates an isolated profile containing links to caller-supplied,
licensed id1 assets.  The fixture is a no-window dedicated process with UDP
enabled; it exercises production connection owners and a local UDP control
peer.  It verifies transport acceptance, not a complete serverinfo/signon.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basedir", required=True, type=Path,
                        help="licensed Quake installation containing id1")
    parser.add_argument("--fixture", type=Path,
                        default=Path("/tmp/qsvr-ordinary-connect-native"))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-debug",
                        help="assertion-enabled native Meson build graph")
    parser.add_argument("--timeout", type=float, default=35.0)
    parser.add_argument("--artifact-root", type=Path,
                        help="new directory in which to retain the profile and fixture.log")
    args = parser.parse_args()
    if not (args.basedir / "id1").is_dir():
        parser.error(f"stock id1 directory is missing: {args.basedir / 'id1'}")
    if not 0 < args.timeout < float("inf"):
        parser.error("--timeout must be finite and positive")
    if args.artifact_root and args.artifact_root.exists():
        parser.error("--artifact-root must not already exist")
    return args


def link_assets(source, profile):
    target = profile / "id1"
    target.mkdir(parents=True)
    for name in ("pak0.pak", "pak1.pak"):
        asset = source / "id1" / name
        if asset.exists():
            (target / name).symlink_to(asset.resolve())


def main():
    args = parse_args()
    fixture = args.fixture.expanduser().resolve()
    import sys
    sys.path.insert(0, str(ROOT / "tests"))
    from run_csqc_entity_native import build_fixture, private_loader_paths
    graph = args.build_dir.expanduser().resolve(strict=True)
    build_fixture(graph, fixture, ROOT / "tests" / "ordinary_connect_native_fixture.c",
                  wrappers=("Loop_Init", "NET_CanSendMessage"))

    if args.artifact_root:
        root = args.artifact_root.expanduser().resolve()
        root.mkdir(parents=True)
        temporary = None
    else:
        temporary = tempfile.TemporaryDirectory(prefix="qsvr-ordinary-connect-")
        root = Path(temporary.name)
    try:
        link_assets(args.basedir.resolve(), root)
        command = [
            str(fixture), "-dedicated", "2", "-nosound", "-nosteamapi",
            "-basedir", str(root), "-userdir", str(root),
        ]
        env = os.environ.copy()
        loader_paths = private_loader_paths(graph)
        if loader_paths:
            prior = env.get("LD_LIBRARY_PATH")
            env["LD_LIBRARY_PATH"] = ":".join(loader_paths + ([prior] if prior else []))
        try:
            result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True,
                                    timeout=args.timeout, env=env)
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            if isinstance(output, bytes):
                output = output.decode(errors="replace")
            (root / "fixture.log").write_text(output)
            raise SystemExit(f"FAIL ordinary native fixture timed out; log={root / 'fixture.log'}")
        (root / "fixture.log").write_text(result.stdout)
        marker = "ORDINARY_CONNECT_NATIVE_PASSED "
        if result.returncode or marker not in result.stdout:
            print("\n".join(result.stdout.splitlines()[-48:]))
            raise SystemExit(
                f"FAIL ordinary native fixture exit={result.returncode}; log={root / 'fixture.log'}")
        print(next(line for line in result.stdout.splitlines() if marker in line))
        if args.artifact_root:
            print(f"artifacts={root}")
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    main()
