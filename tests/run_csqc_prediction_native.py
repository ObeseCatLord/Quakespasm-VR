#!/usr/bin/env python3
"""Build/run the disposable loaded-CSQC CAND-NET-005 witness."""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tests"))
from run_csqc_entity_native import (DEFAULT_BASE, FASTGAMES, append_lifecycle,
                                    build_fixture, private_loader_paths,
                                    write_private_qc_pack)
import cooperative_qc_program
import qc_binding_program


def append_prediction(program):
    header = list(struct.unpack_from("<15i", program))
    parts = []
    for slot, width in ((2, 8), (4, 8), (6, 8), (8, 36), (10, 1), (12, 4)):
        offset, count = header[slot:slot + 2]
        parts.append(bytearray(program[offset:offset + count * width]))
    statements, defs, fields, functions, strings, globals_data = parts

    def name(text):
        value = len(strings); strings.extend(text.encode() + b"\0"); return value
    def glob(text, kind=2, value=0):
        slot = len(globals_data) // 4
        defs.extend(struct.pack("<HHi", kind, slot, name(text)))
        globals_data.extend(struct.pack("<3f", *value) if kind == 3 else
                            struct.pack("<i", value) if kind != 2 else struct.pack("<f", value))
        return slot
    def func(text, first, parm=0, locals_count=0):
        index = len(functions) // 36
        functions.extend(struct.pack("<7i8B", first, parm, locals_count, 0, name(text), 0, 0, *([0] * 8)))
        return index
    def entry(text, code):
        start = len(statements) // 8
        for item in code + [(0, 0, 0, 0)]: statements.extend(struct.pack("<4h", *item))
        return func(text, start)

    # Extension globals must exist in loaded QC for PR_FindExtGlobal to bind.
    input_sequence = glob("input_sequence"); glob("input_servertime"); glob("input_timelength")
    glob("clientcommandframe"); glob("servercommandframe")
    glob("input_movevalues", 3, (0, 0, 0)); glob("input_angles", 3, (0, 0, 0))
    glob("input_buttons"); glob("input_impulse")
    request, result = glob("fixture_prediction_request"), glob("fixture_prediction_result")
    entity = glob("fixture_prediction_entity", 4, 0)
    reentrant_entity = glob("fixture_prediction_reentrant_entity", 4, 0)
    touches, impacts, reentrant_touches = (glob("fixture_prediction_touch_count"),
                                            glob("fixture_prediction_impact_count"),
                                            glob("fixture_prediction_reentrant_touch_count"))
    one = glob("fixture_prediction_one", value=1)
    getinput, pmove = glob("fixture_prediction_getinput", 6, func("getinputstate", -345)), glob("fixture_prediction_pmove", 6, func("runstandardplayerphysics", -347))
    get = entry("fixture_prediction_get", [(31, request, 4, 0), (52, getinput, 0, 0), (31, 1, result, 0)])
    move = entry("fixture_prediction_move", [(34, entity, 4, 0), (52, pmove, 0, 0)])
    touch = entry("fixture_prediction_touch", [(6, touches, one, touches)])
    impact_touch = entry("fixture_prediction_impact_touch", [(6, impacts, one, impacts)])
    reentrant_touch = entry("fixture_prediction_reentrant_touch", [
        (6, reentrant_touches, one, reentrant_touches),
        (34, reentrant_entity, 4, 0), (52, pmove, 0, 0)])
    del touch, impact_touch, reentrant_touch
    glob("fixture_ref_prediction_get", 6, get); glob("fixture_ref_prediction_move", 6, move)
    strings.extend(b"\0" * (-len(strings) % 4)); output = bytearray(60)
    for section, (slot, width) in zip(parts, ((2,8),(4,8),(6,8),(8,36),(10,1),(12,4))):
        header[slot:slot+2] = len(output), len(section)//width; output.extend(section)
    struct.pack_into("<15i", output, 0, *header)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build-debug")
    parser.add_argument("--basedir", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args(); graph = args.build_dir.resolve(strict=True); basedir = args.basedir.resolve(strict=True)
    pack = basedir / "id1/pak0.pak"
    if not pack.is_file(): raise SystemExit("licensed source pack is missing")
    FASTGAMES.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="qsvr-csqc-prediction-native-", dir=FASTGAMES)); profile = root / "profile"; id1 = profile / "id1"; id1.mkdir(parents=True)
    for name in ("pak0.pak", "pak1.pak"):
        source = basedir / "id1" / name
        if source.is_file(): (id1 / name).symlink_to(source.resolve())
    generated = append_prediction(append_lifecycle(qc_binding_program.assemble(cooperative_qc_program.source_program(pack), entities=True)))
    (id1 / "progs.dat").write_bytes(generated); (id1 / "csprogs.dat").write_bytes(generated)
    write_private_qc_pack(id1 / ("pak2.pak" if (id1 / "pak1.pak").is_file() else "pak1.pak"), {"progs.dat": generated, "csprogs.dat": generated})
    output = root / "csqc-prediction-native"; build_fixture(graph, output, REPO / "tests/csqc_prediction_native_fixture.c")
    command = [str(output), "-dedicated", "16", "-noudp", "-nosound", "-nosteamapi", "-basedir", str(profile), "-userdir", str(profile), "-csqc-prediction-native"]
    env = os.environ.copy(); paths = private_loader_paths(graph)
    if paths: env["LD_LIBRARY_PATH"] = ":".join(paths + ([env["LD_LIBRARY_PATH"]] if env.get("LD_LIBRARY_PATH") else []))
    result = subprocess.run(command, cwd=profile, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=args.timeout, env=env)
    required = ("CSQC_PREDICTION_NATIVE_INPUT_PASSED", "CSQC_PREDICTION_NATIVE_PMOVE_PASSED")
    if result.returncode or any(marker not in result.stdout for marker in required):
        raise SystemExit("CSQC_PREDICTION_NATIVE_FAIL profile=" + str(profile) + "\n" + result.stdout)
    print("CSQC_PREDICTION_NATIVE_PASSED program_sha256=" + hashlib.sha256(generated).hexdigest())


if __name__ == "__main__": main()
