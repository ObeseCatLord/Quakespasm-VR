#!/usr/bin/env python3
"""Qualify real menu/key/alias/config owners in private Sacrilege profiles.

Requires GDB, an unstripped Debug build, a display/GPU, and locally owned
id1/Sacrilege assets. Installed settings are excluded and never written.
No external input or focus is sent. This is logical VR-key qualification,
not a physical-headset or spatial-pointer check.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--library-path", type=Path)
    parser.add_argument("--screenshots", action="store_true")
    args = parser.parse_args()
    engine = args.engine.resolve(strict=True)
    base = args.assets.resolve(strict=True)
    for mod in ("id1", "sacrilege"):
        if not (base / mod).is_dir():
            parser.error(f"Missing locally owned {mod} assets")
    script = Path(__file__).with_name("custom_mod_bindings_native.gdb")
    root = Path(tempfile.mkdtemp(prefix="qsvr-custom-bindings-native-"))
    print("Native artifacts:", root, flush=True)
    for post in (False, True):
        profile_root = root / ("postcfg" if post else "native")
        assets, user = profile_root / "assets", profile_root / "user"
        assets.mkdir(parents=True)
        user.mkdir()
        for mod in ("id1", "sacrilege"):
            (assets / mod).mkdir()
            (user / mod).mkdir()
            for child in (base / mod).iterdir():
                if child.suffix.lower() != ".cfg":
                    (assets / mod / child.name).symlink_to(child, target_is_directory=child.is_dir())
        # Support portable and DO_USERDIRS builds without relying on installed cfgs.
        for write_base in (assets, user):
            for path, command in (("vkQuake.cfg", "+button3"),
                                  ("sacrilege/vkQuake.cfg", "+button3"),
                                  ("id1/vkQuake.cfg", "+hook")):
                (write_base / path).write_text(f'bind "VR_ALTFIRE" "{command}"\n')
        profile = profile_root / "options.cfg"
        profile.write_text('bind "VR_ALTFIRE" "+button3"\n')
        for phase in ("save", "reload"):
            env = os.environ.copy()
            result = profile_root / (phase + "-result.json")
            env.update(SDL_WINDOW_ACTIVATE_WHEN_SHOWN="0",
                       SDL_WINDOW_ACTIVATE_WHEN_RAISED="0", SDL_VIDEODRIVER="x11",
                       XDG_CONFIG_HOME=str(profile_root / "config"),
                       XDG_DATA_HOME=str(profile_root / "data"),
                       QSVR_CUSTOM_PHASE=phase, QSVR_CUSTOM_RESULT=str(result))
            if args.library_path:
                env["LD_LIBRARY_PATH"] = str(args.library_path.resolve(strict=True))
            if args.screenshots and not post:
                env["QSVR_CUSTOM_SCREENSHOTS"] = "1"
            else:
                env.pop("QSVR_CUSTOM_SCREENSHOTS", None)
            command = ["gdb", "-nx", "--batch", "--return-child-result", "-x", str(script),
                       "--args", str(engine), "-novr", "-nomouse", "-nosound", "-nosteamapi",
                       "-window", "-width", "640", "-height", "480", "-basedir", str(assets),
                       "-userdir", str(user), "-game", "sacrilege", "+map", "start",
                       "+r_tasks", "0", "+host_maxfps", "60"]
            if post:
                command += ["-postcfg", str(profile), "-writepostcfg"]
            log = profile_root / (phase + ".log")
            with log.open("w") as output:
                process = subprocess.Popen(command, env=env, stdout=output,
                                           stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    code = process.wait(timeout=90)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
                    raise RuntimeError(f"Native qualification timed out; inspect {log}")
            # Deliberate GDB kill returns 255; require a typed completion receipt too.
            if code not in (0, 255) or not result.is_file():
                raise RuntimeError(f"Native qualification failed ({code}); inspect {log}")
            receipt = json.loads(result.read_text())
            assert receipt["status"] == "passed", receipt
            print("CUSTOM_NATIVE_PASSED", post, phase, receipt, flush=True)
            if phase == "save":
                saved = (Path(receipt["gamedir"]) / "vkQuake.cfg").read_text()
                assert '"+hook"' in saved and "echo " + "x" * 250 in saved
                if post:
                    assert '"+hook"' in profile.read_text()
    print("ALL_CUSTOM_BINDINGS_NATIVE_PASSED", flush=True)


if __name__ == "__main__":
    main()
