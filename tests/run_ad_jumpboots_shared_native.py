#!/usr/bin/env python3
"""Deferred native AD boot-sharing qualification against unmodified installed QC.

Run only after main completes the shared assertion-enabled engine build.
Private writable profiles contain asset links, never generated/replaced QC.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ASSET_DIRS = ("progs", "maps", "sound", "gfx", "textures", "models", "music", "env", "scripts")


def effective_program(base, game):
    """Match native id1 + game roots, contiguous packs, first duplicate entry."""
    content = None
    origin = None
    for rootname in dict.fromkeys(("id1", game)):
        root = base / rootname
        loose = root / "progs.dat"
        if loose.is_file():
            content, origin = loose.read_bytes(), f"{rootname}/progs.dat"
        index = 0
        while (pack := root / f"pak{index}.pak").is_file():
            with pack.open("rb") as stream:
                magic, offset, size = struct.unpack("<4sii", stream.read(12))
                if magic != b"PACK" or size % 64 or offset < 12:
                    raise ValueError(f"invalid pack: {rootname}/{pack.name}")
                stream.seek(offset)
                directory = stream.read(size)
                if len(directory) != size:
                    raise ValueError("short pack directory")
                for slot in range(0, size, 64):
                    name, position, length = struct.unpack_from("<56sii", directory, slot)
                    if name.split(b"\0", 1)[0] == b"progs.dat":
                        if position < 12 or length < 60:
                            raise ValueError("invalid native program entry")
                        stream.seek(position)
                        content, origin = stream.read(length), f"{rootname}/{pack.name}:progs.dat"
                        if len(content) != length:
                            raise ValueError("short native program entry")
                        break
            index += 1
    if content is None:
        raise ValueError(f"no effective installed QC for {game}")
    return hashlib.sha256(content).hexdigest(), origin


def link_assets(base, profile, game):
    for rootname in dict.fromkeys(("id1", game)):
        source = base / rootname
        if not source.is_dir():
            raise ValueError(f"installed root missing: {rootname}")
        target = profile / rootname
        target.mkdir(parents=True)
        index = 0
        while (pack := source / f"pak{index}.pak").is_file():
            (target / pack.name).symlink_to(pack.resolve())
            index += 1
        if (source / "progs.dat").is_file():
            (target / "progs.dat").symlink_to((source / "progs.dat").resolve())
        for name in ASSET_DIRS:
            if (source / name).is_dir():
                (target / name).symlink_to((source / name).resolve(), target_is_directory=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--basedir", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--games", nargs="+", default=["ad", "hwjam4", "hwjam2", "q30a1024",
                                                     "quake_rooftop_jam_v2", "gibtropolis"])
    parser.add_argument("--map", default="start")
    parser.add_argument("--fixture", type=Path, default=Path("/tmp/qsvr-ad-jumpboots-shared-native"))
    parser.add_argument("--artifact-root", type=Path,
                        help="new directory retaining isolated profiles and concise run logs")
    parser.add_argument("--timeout", type=float, default=90)
    args = parser.parse_args()
    if not 0 < args.timeout < float("inf"):
        parser.error("timeout must be finite and positive")
    for game in args.games:
        if Path(game).is_absolute() or ".." in Path(game).parts:
            parser.error("games must be relative installed root paths")
    base = args.basedir.resolve(strict=True)
    graph = args.build_dir.resolve(strict=True)
    world = graph / "vkquake.p/Quake_world.c.o"
    if not world.is_file() or world.stat().st_mtime_ns < (ROOT / "Quake/world.c").stat().st_mtime_ns:
        parser.error("main must rebuild the shared native graph with the current world.c first")
    from run_csqc_entity_native import build_fixture, private_loader_paths
    fixture = args.fixture.resolve()
    build_fixture(graph, fixture, ROOT / "tests/ad_jumpboots_shared_native_fixture.c",
                  wrappers=("Loop_Init", "NET_CanSendMessage", "NET_SendUnreliableMessage",
                            "R_TranslateNewPlayerSkin", "PR_ExecuteProgram"))
    temporary = None
    if args.artifact_root:
        root = args.artifact_root.resolve()
        root.mkdir(parents=True, exist_ok=False)
    else:
        temporary = tempfile.TemporaryDirectory(prefix="qsvr-ad-jumpboots-")
        root = Path(temporary.name)
    try:
        env = os.environ.copy()
        libraries = private_loader_paths(graph)
        if libraries:
            env["LD_LIBRARY_PATH"] = ":".join(libraries + ([env["LD_LIBRARY_PATH"]]
                                                         if env.get("LD_LIBRARY_PATH") else []))
        for number, game in enumerate(args.games):
            profile = root / f"profile-{number}"
            profile.mkdir()
            link_assets(base, profile, game)
            digest, origin = effective_program(base, game)
            command = [str(fixture), "-dedicated", "4", "-noudp", "-nosound", "-nosteamapi",
                       "-basedir", str(profile), "-userdir", str(profile), "-game", game,
                       "-fixture-map", args.map, "-fixture-qc-sha256", digest]
            result = subprocess.run(command, cwd=profile, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=args.timeout)
            (profile / "fixture.log").write_text(result.stdout)
            markers = ("AD_JUMPBOOTS_SHARED_NATIVE_PASSED ",
                       "AD_JUMPBOOTS_SHARED_NATIVE_NOT_APPLICABLE ")
            evidence = [line for line in result.stdout.splitlines()
                        if any(marker in line for marker in markers)]
            if result.returncode or len(evidence) != 1:
                print("\n".join(result.stdout.splitlines()[-40:]))
                raise SystemExit(f"FAIL {game}: exit={result.returncode}; log={profile / 'fixture.log'}")
            print(f"{game}: source={origin} " + evidence[0])
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    main()
