#!/usr/bin/env python3
"""Assemble a test-only cooperative hook over licensed stock QC in /tmp.

No compiler or licensed program is bundled. Original functions/statements stay
unchanged; the ordinary loader resolves the appended globals, field and builtin.
This is a prepared ABI consumer, not an authored mod compatibility claim.
"""
import argparse
import struct
from pathlib import Path


def source_program(pack):
    data = pack.read_bytes()
    magic, offset, size = struct.unpack_from("<4sii", data)
    if magic != b"PACK" or size % 64 or offset < 12 or offset + size > len(data):
        raise ValueError("invalid Quake pack directory")
    for pos in range(offset, offset + size, 64):
        name, start, length = struct.unpack_from("<56sii", data, pos)
        if name.rstrip(b"\0") == b"progs.dat":
            if start < 12 or length < 60 or start + length > len(data):
                raise ValueError("invalid program entry")
            return data[start:start + length]
    raise ValueError("pack has no progs.dat")


def assemble(program, calls_per_hook):
    header = list(struct.unpack_from("<15i", program))
    if header[0] != 6:
        raise ValueError("fixture requires classic version6 QC")
    sections = []
    for slot, width in ((2, 8), (4, 8), (6, 8), (8, 36), (10, 1), (12, 4)):
        offset, count = header[slot:slot + 2]
        if offset < 60 or count < 0 or offset + count * width > len(program):
            raise ValueError("invalid program section")
        sections.append(bytearray(program[offset:offset + count * width]))
    statements, globals_defs, fields, functions, strings, globals_data = sections

    def name(text):
        result = len(strings)
        strings.extend(text.encode("ascii") + b"\0")
        return result

    def global_slot(text, kind=2, width=1, value=0.0):
        result = len(globals_data) // 4
        if result + width >= 32768:
            raise ValueError("fixture exceeds classic signed statement offsets")
        globals_defs.extend(struct.pack("<HHi", kind, result, name(text)))
        globals_data.extend(struct.pack("<f", value) * width)
        return result

    names = {}
    for pos in range(0, len(globals_defs), 8):
        _, slot, string = struct.unpack_from("<HHi", globals_defs, pos)
        end = strings.index(0, string)
        names[bytes(strings[string:end]).decode("ascii")] = slot
    self_slot = names["self"]
    input_sequence = global_slot("input_sequence")
    global_slot("input_servertime")
    input_seconds = global_slot("input_timelength")
    input_move = global_slot("input_movevalues", 3, 3)
    global_slot("input_angles", 3, 3)
    global_slot("input_buttons")
    global_slot("input_impulse")
    cursor = global_slot("input_cursor_screen", 3, 3)
    calls = global_slot("fixture_hook_calls")
    seconds = global_slot("fixture_hook_seconds")
    sequence = global_slot("fixture_hook_sequence")
    observed_move = global_slot("fixture_hook_move", 3, 3)
    standard_calls = global_slot("fixture_standard_calls")
    global_slot("fixture_expected_calls", value=float(calls_per_hook))
    one = global_slot("fixture_one", value=1.0)
    half = global_slot("fixture_half", value=0.5)
    builtin = len(functions) // 36
    functions.extend(struct.pack("<7i8B", 0, 0, 0, 0,
                                 name("runstandardplayerphysics"), 0, 1,
                                 1, 0, 0, 0, 0, 0, 0, 0))
    builtin_slot = global_slot("fixture_standard", 6)
    struct.pack_into("<i", globals_data, builtin_slot * 4, builtin)
    hook_start = len(statements) // 8
    # Opcodes from Quake/pr_comp.h. Run the real registry/VM, no diagnostic C call.
    code = [(6, calls, one, calls),            # ADD_F
            (6, seconds, input_seconds, seconds),
            (31, input_sequence, sequence, 0),  # STORE_F
            (32, input_move, observed_move, 0), # STORE_V
            (4, input_move, half, input_move)]  # MUL_VF: actual QC input transform
    if calls_per_hook == 2:
        code.append((1, input_seconds, half, input_seconds))  # MUL_F: subdivisions
    for _ in range(calls_per_hook):
        code.extend(((34, self_slot, 4, 0),       # STORE_ENT to OFS_PARM0
                     (52, builtin_slot, 0, 0),   # CALL1
                     (6, standard_calls, one, standard_calls)))
    code.extend(((6, cursor + 2, one, cursor + 2), (0, 0, 0, 0)))
    for instruction in code:
        statements.extend(struct.pack("<4h", *instruction))
    functions.extend(struct.pack("<7i8B", hook_start, 0, 0, 0,
                                 name("SV_RunClientCommand"), 0, 0, *([0] * 8)))
    hook_slot = global_slot("SV_RunClientCommand", 6)
    struct.pack_into("<i", globals_data, hook_slot * 4, builtin + 1)
    fields.extend(struct.pack("<HHi", 2, header[14], name("pmove_flags")))
    header[14] += 1
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
    parser.add_argument("--calls", type=int, choices=(0, 1, 2), default=1)
    args = parser.parse_args()
    result = assemble(source_program(args.source_pack), args.calls)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print("COOPERATIVE_QC_PROGRAM_WRITTEN bytes=" + str(len(result)))


if __name__ == "__main__":
    main()
