#!/usr/bin/env python3
"""Build a private SSQC consumer over the existing loaded-QC fixture API."""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

REPO_TESTS = Path(__file__).resolve().parent
sys.path.insert(0, str(REPO_TESTS))
from qc_binding_program import assemble, source_program


def append_lifecycle(program):
    header = list(struct.unpack_from("<15i", program))
    sections = []
    for slot, width in ((2, 8), (4, 8), (6, 8), (8, 36), (10, 1), (12, 4)):
        offset, count = header[slot:slot + 2]
        if offset < 60 or count < 0 or offset + count * width > len(program):
            raise ValueError("invalid generated QC section")
        sections.append(bytearray(program[offset:offset + count * width]))
    statements, global_defs, fields, functions, strings, globals_data = sections

    def string(text):
        result = len(strings)
        strings.extend(text.encode("ascii") + b"\0")
        return result

    def add_global(text, kind, data):
        slot = len(globals_data) // 4
        if slot >= 32768 or len(data) != 4:
            raise ValueError("fixture global exceeds the classic QC range")
        global_defs.extend(struct.pack("<HHi", kind, slot, string(text)))
        globals_data.extend(data)
        return slot

    def add_builtin(text):
        index = len(functions) // 36
        functions.extend(struct.pack("<7i8B", 0, 0, 0, 0, string(text), 0, 0,
                                     *([0] * 8)))
        return add_global("fixture_lifecycle_builtin_" + text, 6,
                          struct.pack("<i", index))

    def add_entry(text, code):
        first_statement = len(statements) // 8
        for instruction in code + [(0, 0, 0, 0)]:
            statements.extend(struct.pack("<4h", *instruction))
        index = len(functions) // 36
        functions.extend(struct.pack("<7i8B", first_statement, 0, 0, 0,
                                     string(text), 0, 0, *([0] * 8)))
        return index

    existing = set()
    for pos in range(0, len(global_defs), 8):
        _, _, offset = struct.unpack_from("<HHi", global_defs, pos)
        existing.add(bytes(strings[offset:]).split(b"\0", 1)[0].decode("ascii"))
    if "autocvar_topcolor" in existing:
        raise ValueError("source fixture already contains autocvar_topcolor")

    # EV_FLOAT, initialized from the native topcolor cvar when SSQC loads.
    add_global("autocvar_topcolor", 2, struct.pack("<f", 0.0))
    bot_entity = add_global("fixture_lifecycle_bot", 4, struct.pack("<i", 0))
    key_slot = add_global("fixture_lifecycle_key", 1,
                          struct.pack("<i", string("qc_owned")))
    value_slot = add_global("fixture_lifecycle_value", 1,
                            struct.pack("<i", string("occupied")))

    spawn = add_builtin("spawnclient")
    force = add_builtin("forceinfokey")
    drop = add_builtin("dropclient")
    spawn_entry = add_entry("fixture_lifecycle_spawn", [
        (51, spawn, 0, 0),                  # CALL0 spawnclient
        (34, 1, bot_entity, 0),             # STORE_ENT OFS_RETURN
        (34, bot_entity, 4, 0),             # STORE_ENT OFS_PARM0
        (33, key_slot, 7, 0),               # STORE_S OFS_PARM1
        (33, value_slot, 10, 0),            # STORE_S OFS_PARM2
        (54, force, 0, 0),                  # CALL3 forceinfokey
    ])
    drop_entry = add_entry("fixture_lifecycle_drop", [
        (34, bot_entity, 4, 0),             # STORE_ENT OFS_PARM0
        (52, drop, 0, 0),                   # CALL1 dropclient
    ])
    add_global("fixture_ref_lifecycle_spawn", 6, struct.pack("<i", spawn_entry))
    add_global("fixture_ref_lifecycle_drop", 6, struct.pack("<i", drop_entry))

    strings.extend(b"\0" * (-len(strings) % 4))
    output = bytearray(60)
    for section, (slot, width) in zip(sections, ((2, 8), (4, 8), (6, 8),
                                                (8, 36), (10, 1), (12, 4))):
        header[slot:slot + 2] = len(output), len(section) // width
        output.extend(section)
    struct.pack_into("<15i", output, 0, *header)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-pack", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = append_lifecycle(assemble(source_program(args.source_pack)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print(f"METADATA_LIFECYCLE_QC_WRITTEN bytes={len(result)} sha256={hashlib.sha256(result).hexdigest()}")


if __name__ == "__main__":
    main()
