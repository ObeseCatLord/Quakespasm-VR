#!/usr/bin/env python3
"""Run the existing desktop and OpenXR GDB probes against one local server."""
import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


HERE = Path(__file__).resolve().parent

def parse_preset(value):
    try:
        destination, source = value.split("=", 1)
    except ValueError:
        raise argparse.ArgumentTypeError("weapon preset must be RELATIVE_DEST=SOURCE")
    relative = Path(destination)
    if not destination or relative.is_absolute() or ".." in relative.parts:
        raise argparse.ArgumentTypeError("weapon preset destination must be relative")
    source = Path(source).resolve()
    if not source.is_file():
        raise argparse.ArgumentTypeError("weapon preset source is not a file: %s" % source)
    return relative, source

def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--desktop-binary", required=True, type=Path)
    parser.add_argument("--vr-binary", required=True, type=Path)
    parser.add_argument("--dedicated-binary", type=Path,
                        help="defaults to --desktop-binary with -dedicated")
    parser.add_argument("--pak0", required=True, type=Path,
                        help="caller-supplied licensed id1/pak0.pak")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--port", type=int, help="loopback UDP port; allocated if omitted")
    parser.add_argument("--timeout", type=float, default=145.0)
    parser.add_argument("--weapon-preset", action="append", default=[], type=parse_preset,
                        metavar="RELATIVE_DEST=SOURCE")
    args = parser.parse_args()
    for label, path in (("desktop binary", args.desktop_binary), ("VR binary", args.vr_binary),
                        ("pak0", args.pak0)):
        if not path.is_file():
            parser.error("%s is not a file: %s" % (label, path))
    if args.dedicated_binary and not args.dedicated_binary.is_file():
        parser.error("dedicated binary is not a file: %s" % args.dedicated_binary)
    args.desktop_binary = args.desktop_binary.resolve()
    args.vr_binary = args.vr_binary.resolve()
    args.dedicated_binary = args.dedicated_binary.resolve() if args.dedicated_binary else None
    args.pak0 = args.pak0.resolve()
    if args.port is not None and not 1 <= args.port <= 65535:
        parser.error("port must be in 1..65535")
    if not 0 < args.timeout < float("inf"):
        parser.error("timeout must be finite and positive")
    return args

def allocate_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def make_profile(root, name, pak0, presets):
    profile = root / name / "id1"
    profile.mkdir(parents=True)
    os.symlink(pak0.resolve(), profile / "pak0.pak")
    for destination, source in presets:
        target = profile / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        os.symlink(source, target)
    return profile.parent

def command_limit(command):
    if len(" ".join(map(str, command)).encode()) >= 256:
        raise RuntimeError("native command line is 256 bytes or longer")


def stop(process):
    if not process or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            raise RuntimeError("owned child did not exit after SIGKILL")


def load_result(path, marker, log, returncode):
    detail = {"returncode": returncode, "result": None, "marker": False}
    try:
        detail["result"] = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        detail["error"] = "result unreadable: %s" % exc
    try:
        detail["marker"] = marker in log.read_text(errors="replace")
    except OSError as exc:
        detail["error"] = "log unreadable: %s" % exc
    detail["passed"] = (returncode == 0 and detail["marker"] and
                        isinstance(detail["result"], dict) and
                        detail["result"].get("status") == "passed")
    return detail


def main():
    args = arguments()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    result = {"status": "failed", "desktop": None, "vr": None}
    processes, logs = [], []
    try:
        with tempfile.TemporaryDirectory(prefix="qcx-", dir="/tmp") as temporary:
            root = Path(temporary)
            original_cwd = Path.cwd()
            try:
                os.chdir(root)  # Keep profile arguments short for native command parsing.
                server_profile = make_profile(root, "server", args.pak0, args.weapon_preset)
                desktop_profile = make_profile(root, "desktop", args.pak0, args.weapon_preset)
                vr_profile = make_profile(root, "vr", args.pak0, args.weapon_preset)
                for name in ("server-user", "desktop-user", "vr-user"):
                    (root / name).mkdir()
                port = args.port or allocate_port()
                address = "127.0.0.1:%d" % port
                dedicated = args.dedicated_binary or args.desktop_binary
                server = [str(dedicated),
                    "-dedicated", "2", "-ip", "127.0.0.1", "-port", str(port),
                    "-basedir", server_profile.name, "-userdir", "server-user",
                    "+deathmatch", "0", "+coop", "1", "+sv_qsvr_private", "1",
                    "+sv_private_pmove_walk", "1", "+map", "e1m1"]
                desktop = [str(args.desktop_binary), "-novr", "-nosound", "-window",
                           "-width", "640", "-height", "480", "-basedir",
                           desktop_profile.name, "-userdir", "desktop-user",
                           "+connect", address, "+name", "Desktop", "+vid_vsync", "0", "+host_maxfps", "144"]
                vr = [str(args.vr_binary), "-openxr", "-nosound", "-window", "-width", "640",
                      "-height", "480", "-basedir", vr_profile.name, "-userdir",
                      "vr-user", "+connect", address, "+name", "VR", "+vid_vsync", "0", "+host_maxfps", "144", "+host_phys_max_ticrate", "10"]
                for command in (server, desktop, vr):
                    command_limit(command)
                def spawn(name, command, environment):
                    stream = (output / (name + ".log")).open("w")
                    logs.append(stream)
                    process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT,
                                               env=environment, start_new_session=True)
                    processes.append(process)
                    return process
                server_process = spawn("server", server, os.environ.copy())
                time.sleep(1)
                if server_process.poll() is not None:
                    raise RuntimeError("dedicated server exited before clients started")
                desktop_result = output / "desktop-result.json"
                vr_result = output / "vr-result.json"
                desktop_env = os.environ.copy()
                desktop_env.update({"QSVR_LOCAL_EXPECT_PRIVATE": "1", "QSVR_LOCAL_EXPECT_PREDICTION": "1",
                                    "QSVR_LOCAL_EXPECT_PEERS": "2", "QSVR_LOCAL_ASSERT_ACTION_ACK": "1",
                                    "QSVR_LOCAL_ASSERT_MOVE_STATS": "1", "QSVR_LOCAL_ASSERT_NONZERO_JUMP_TIMER": "1",
                                    "QSVR_LOCAL_ASSERT_COHERENT_OWNER": "1", "QSVR_LOCAL_ASSERT_PMOVE_TYPE": "1",
                                    "QSVR_LOCAL_RESULT": str(desktop_result)})
                vr_env = os.environ.copy()
                vr_env.update({"QSVR_PINNED_VR_RESULT": str(vr_result),
                               "QSVR_PINNED_VR_EXPECT_SELECTED_PREDICTION": "1"})
                desktop_process = spawn("desktop", ["gdb", "-nx", "--batch", "-x",
                    str(HERE / "local_private_legacy_peer_smoke.gdb"), "--args"] + desktop, desktop_env)
                vr_process = spawn("vr", ["gdb", "-nx", "--batch", "-x",
                    str(HERE / "pinned_vr_gameplay_smoke.gdb"), "--args"] + vr, vr_env)
                deadline = time.monotonic() + args.timeout
                while any(process.poll() is None for process in (desktop_process, vr_process)):
                    if time.monotonic() >= deadline:
                        raise RuntimeError("runner timeout expired")
                    if server_process.poll() is not None:
                        raise RuntimeError("dedicated server exited during probes (%s)" % server_process.returncode)
                    for name, process in (("desktop GDB", desktop_process), ("VR GDB", vr_process)):
                        if process.poll() not in (None, 0):
                            raise RuntimeError("%s exited with status %s" %
                                               (name, process.returncode))
                    time.sleep(0.1)
                if server_process.poll() is not None:
                    raise RuntimeError("dedicated server exited before probe completion (%s)" % server_process.returncode)
                result["desktop"] = load_result(desktop_result, "QSVR_LOCAL_PRIVATE_PASSED",
                                                 output / "desktop.log", desktop_process.returncode)
                result["vr"] = load_result(vr_result, "QSVR_PINNED_VR_PASSED",
                                            output / "vr.log", vr_process.returncode)
                result["port"] = port
                result["status"] = "passed" if result["desktop"]["passed"] and result["vr"]["passed"] else "failed"
            finally:
                cleanup_errors = []
                try:
                    for process in reversed(processes):
                        try:
                            stop(process)
                        except Exception as exc:
                            cleanup_errors.append(str(exc))
                finally:
                    try:
                        for stream in logs:
                            stream.close()
                    finally:
                        os.chdir(original_cwd)
                if cleanup_errors:
                    raise RuntimeError("child cleanup failed: " + "; ".join(cleanup_errors))
    except Exception as exc:
        result["status"] = "failed"
        result["error"] = str(exc)
    finally:
        (output / "connected-crossplay-result.json").write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
