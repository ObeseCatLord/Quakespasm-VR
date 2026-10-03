#!/usr/bin/env python3
"""Append loaded SSQC request and client-command hooks to stock progs.dat."""
import argparse
import struct
from pathlib import Path
import sys
sys.dont_write_bytecode = True

from cooperative_qc_program import source_program

# Classic version 6 statement opcodes used by the existing binding assemblers.
OP_DONE = 0
OP_ADD_F = 6
OP_EQ_S = 12
OP_STORE_ENT = 34
OP_IFNOT = 50
OP_CALL0 = 51
OP_CALL1 = 52


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

    names = {}
    for pos in range(0, len(global_defs), 8):
        _, slot, text_offset = struct.unpack_from("<HHi", global_defs, pos)
        end = strings.index(0, text_offset)
        names[bytes(strings[text_offset:end]).decode("ascii")] = slot
    if "self" not in names:
        raise ValueError("stock program has no self global")
    self_slot = names["self"]
    global_names = set(names)
    existing_functions = set()
    for pos in range(0, len(functions), 36):
        text_offset = struct.unpack_from("<i", functions, pos + 16)[0]
        end = strings.index(0, text_offset)
        existing_functions.add(bytes(strings[text_offset:end]).decode("ascii"))

    def global_slot(text, kind=2, value=0):
        if text in global_names:
            raise ValueError("duplicate fixture global: " + text)
        global_names.add(text)
        slot = len(globals_data) // 4
        if slot + (3 if kind == 3 else 1) > 32768:
            raise ValueError("fixture exceeds classic signed statement offsets")
        global_defs.extend(struct.pack("<HHi", kind, slot, name(text)))
        globals_data.extend(struct.pack("<3f", 0.0, 0.0, 0.0) if kind == 3 else
                            struct.pack("<i", value) if kind != 2 else struct.pack("<f", value))
        return slot

    def add_function(text, first_statement, parm_start=0, locals_count=0, parm_sizes=()):
        if text in existing_functions:
            raise ValueError("fixture function already exists in stock program: " + text)
        if len(parm_sizes) > 8:
            raise ValueError("fixture function has too many parameters")
        index = len(functions) // 36
        functions.extend(struct.pack("<7i8B", first_statement, parm_start, locals_count,
                                     0, name(text), 0, len(parm_sizes),
                                     *parm_sizes, *([0] * (8 - len(parm_sizes)))))
        existing_functions.add(text)
        return index

    def emit(code):
        start = len(statements) // 8
        for instruction in code:
            statements.extend(struct.pack("<4h", *instruction))
        return start

    one = global_slot("fixture_server_request_one", value=1.0)
    hook_calls = global_slot("fixture_server_request_hook_calls")
    tail_calls = global_slot("fixture_server_request_tail_calls")
    request_calls = global_slot("fixture_server_request_event_calls")
    request_entity = global_slot("fixture_server_request_event_entity", 4)
    replacement_entity = global_slot("fixture_server_request_replacement_entity", 4)
    native_replacement_calls = global_slot("fixture_server_request_native_replacement_calls")
    request_local = global_slot("fixture_server_request_event_local", 4)
    command_local = global_slot("fixture_server_request_command_local", 1)
    condition = global_slot("fixture_server_request_condition")

    command_strings = {
        key: global_slot("fixture_server_request_string_" + key, 1, name(value))
        for key, value in (
            ("control", "fixture_qc_control"),
            ("drop", "fixture_qc_drop"),
            ("replace", "fixture_qc_replace"),
            ("tail", "fixture_qc_tail"),
        )
    }

    builtin_drop = add_function("dropclient", -453, parm_sizes=(1,))
    builtin_spawn = add_function("spawnclient", -454)
    drop_ref = global_slot("fixture_server_request_builtin_drop", 6, builtin_drop)
    spawn_ref = global_slot("fixture_server_request_builtin_spawn", 6, builtin_spawn)
    fill_start = emit([(OP_CALL0, spawn_ref, 0, 0)] * 3 + [(OP_DONE, 0, 0, 0)])
    add_function("fixture_server_request_fill_bots", fill_start)

    def drop_self():
        return [(OP_STORE_ENT, self_slot, 4, 0),
                (OP_CALL1, drop_ref, 0, 0)]

    def spawn_and_count(counter=None):
        code = [(OP_CALL0, spawn_ref, 0, 0),
                (OP_STORE_ENT, 1, replacement_entity, 0)]
        if counter is not None:
            code.append((OP_ADD_F, counter, one, counter))
        return code

    # The ordinary loaded hook chooses actions from the actual client string.
    # Branch offsets are statement-relative, matching PR_ExecuteProgram.
    hook_code = [(OP_ADD_F, hook_calls, one, hook_calls)]
    for key, action in (
        ("control", []),
        ("drop", drop_self()),
        ("replace", drop_self() + spawn_and_count()),
        ("tail", [(OP_ADD_F, tail_calls, one, tail_calls)]),
    ):
        hook_code.append((OP_EQ_S, command_local, command_strings[key], condition))
        branch = len(hook_code)
        hook_code.append((OP_IFNOT, condition, 0, 0))
        hook_code.extend(action)
        hook_code.append((OP_DONE, 0, 0, 0))
        hook_code[branch] = (OP_IFNOT, condition, len(hook_code) - branch, 0)
    hook_code.append((OP_DONE, 0, 0, 0))
    hook_start = emit(hook_code)
    add_function("SV_ParseClientCommand", hook_start, command_local, 1, (1,))

    request_start = emit([
        (OP_ADD_F, request_calls, one, request_calls),
        (OP_STORE_ENT, request_local, request_entity, 0),
        (OP_DONE, 0, 0, 0),
    ])
    add_function("CSEv_fixture_probe_e", request_start, request_local, 1, (1,))

    native_replace_start = emit(drop_self() + spawn_and_count(native_replacement_calls) +
                                [(OP_DONE, 0, 0, 0)])
    add_function("fixture_server_request_native_replace", native_replace_start)

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
    result = assemble(source_program(args.source_pack))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print("SERVER_REQUEST_QC_PROGRAM_WRITTEN bytes=" + str(len(result)))


if __name__ == "__main__":
    main()
