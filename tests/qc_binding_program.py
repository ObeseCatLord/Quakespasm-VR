#!/usr/bin/env python3
"""Append a narrow loaded-QC binding fixture to licensed stock progs.dat."""
import argparse
import struct
from pathlib import Path

from cooperative_qc_program import source_program


def assemble(program):
    header = list(struct.unpack_from("<15i", program))
    if header[0] != 6:
        raise ValueError("fixture requires classic version6 QC")
    sections = []
    for slot, width in ((2, 8), (4, 8), (6, 8), (8, 36), (10, 1), (12, 4)):
        offset, count = header[slot:slot + 2]
        if offset < 60 or count < 0 or offset + count * width > len(program):
            raise ValueError("invalid program section")
        sections.append(bytearray(program[offset:offset + count * width]))
    statements, global_defs, fields, functions, strings, globals_data = sections

    def name(text):
        result = len(strings)
        strings.extend(text.encode("ascii") + b"\0")
        return result

    def global_slot(text, kind=2, value=0):
        slot = len(globals_data) // 4
        if slot >= 32768:
            raise ValueError("fixture exceeds classic signed statement offsets")
        global_defs.extend(struct.pack("<HHi", kind, slot, name(text)))
        globals_data.extend(struct.pack("<i", value) if kind != 2 else struct.pack("<f", value))
        return slot

    def add_function(text, first_statement, parm_start=0, locals_count=0):
        index = len(functions) // 36
        functions.extend(struct.pack("<7i8B", first_statement, parm_start, locals_count,
                                     0, name(text), 0, 0, *([0] * 8)))
        return index

    def emit(code):
        start = len(statements) // 8
        for instruction in code:
            statements.extend(struct.pack("<4h", *instruction))
        return start

    def entry(text, code):
        start = emit(code + [(0, 0, 0, 0)])
        return add_function(text, start)

    # These declaration records are intentionally distinct from any stock hash
    # entry; their fixture_ globals carry the actual appended function indices.
    refs = {}
    ref_slots = {}

    def add_ref(ref_name, function_index):
        refs[ref_name] = function_index
        ref_slots[ref_name] = global_slot("fixture_ref_" + ref_name, 6, function_index)

    declarations = (
        ("builtin_find", 0, "builtin_find"),
        ("checkbuiltin", 0, "checkbuiltin"),
        ("random", 0, "random_named"),
        ("dprint", 0, "dprint_named"),
        ("finaleFinished", 0, "finale_finished"),
        ("fixture_unknown_builtin", 0, "unknown"),
        ("RaNdOm", 0, "random_mixed"),
        ("fixture_numeric_random", -7, "random_numeric"),
        ("dprint", -277, "dprint_277"),
    )
    for function_name, first_statement, ref_name in declarations:
        add_ref(ref_name, add_function(function_name, first_statement))

    quarter = global_slot("fixture_quarter", value=0.25)
    random_body_start = emit([(43, quarter, 0, 0)])  # OP_RETURN
    add_ref("random_body", add_function("random", random_body_start))

    result_names = (
        "find_random", "find_random_mixed", "find_dprint", "find_changeyaw",
        "find_changeyaw_lower", "find_cvar_setlong", "find_finale_finished",
        "find_unknown", "find_localsound", "check_random", "check_finale_finished",
        "check_unknown", "check_random_mixed", "named_random", "numeric_random",
        "qc_random_body",
    )
    results = {key: global_slot("fixture_float_" + key) for key in result_names}
    texts = {
        "find_random": "random", "find_random_mixed": "RaNdOm", "find_dprint": "dprint",
        "find_changeyaw": "ChangeYaw", "find_changeyaw_lower": "changeyaw",
        "find_cvar_setlong": "cvar_setlong", "find_finale_finished": "finaleFinished",
        "find_unknown": "fixture_unknown_builtin", "find_localsound": "localsound",
        "dprint_named": "QC_BINDING_FIXTURE named dprint\n",
        "dprint_277": "QC_BINDING_FIXTURE inherited dprint 277\n",
    }
    string_slots = {key: global_slot("fixture_string_" + key, 1, name(value))
                    for key, value in texts.items()}
    call_function = 52  # OP_CALL1
    store_string = 33   # OP_STORE_S
    store_function = 36 # OP_STORE_FNC
    store_float = 31    # OP_STORE_F

    discovery = []
    for key in result_names[:9]:
        discovery.extend(((store_string, string_slots[key], 4, 0),
                          (call_function, ref_slots["builtin_find"], 0, 0),
                          (store_float, 1, results[key], 0)))
    check_refs = (("check_random", "random_named"),
                  ("check_finale_finished", "finale_finished"),
                  ("check_unknown", "unknown"),
                  ("check_random_mixed", "random_mixed"))
    for key, ref_name in check_refs:
        discovery.extend(((store_function, ref_slots[ref_name], 4, 0),
                          (call_function, ref_slots["checkbuiltin"], 0, 0),
                          (store_float, 1, results[key], 0)))
    add_ref("discovery_entry", entry("fixture_discovery", discovery))

    def random_entry(text, ref_name, result_name):
        code = [(51, ref_slots[ref_name], 0, 0),
                (store_float, 1, results[result_name], 0)]  # OP_CALL0, save OFS_RETURN
        return entry(text, code)

    add_ref("named_random_entry", random_entry("fixture_named_random_entry", "random_named", "named_random"))
    add_ref("numeric_random_entry", random_entry("fixture_numeric_random_entry", "random_numeric", "numeric_random"))
    add_ref("random_body_entry", random_entry("fixture_qc_random_body_entry", "random_body", "qc_random_body"))

    def dprint_entry(text, ref_name, string_name):
        return entry(text, [(store_string, string_slots[string_name], 4, 0),
                            (call_function, ref_slots[ref_name], 0, 0)])

    add_ref("named_dprint_entry", dprint_entry("fixture_named_dprint_entry", "dprint_named", "dprint_named"))
    add_ref("dprint_277_entry", dprint_entry("fixture_dprint_277_entry", "dprint_277", "dprint_277"))

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
    parser.add_argument("--output", type=Path, required=True, help="SSQC progs.dat path")
    parser.add_argument("--csqc-output", type=Path, required=True)
    args = parser.parse_args()
    result = assemble(source_program(args.source_pack))
    for output in (args.output, args.csqc_output):
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(result)
    print("QC_BINDING_PROGRAM_WRITTEN bytes=" + str(len(result)))


if __name__ == "__main__":
    main()
