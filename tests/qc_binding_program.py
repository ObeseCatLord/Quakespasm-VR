#!/usr/bin/env python3
"""Append a narrow loaded-QC binding fixture to licensed stock progs.dat."""
import argparse
import struct
from pathlib import Path

from cooperative_qc_program import source_program


def assemble(program, resources=False):
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

    if resources:
        for builtin, number in (("buf_create", 460), ("buf_del", 461), ("buf_getsize", 462),
                                ("buf_copy", 463), ("buf_sort", 464),
                                ("bufstr_get", 466), ("bufstr_set", 467), ("bufstr_add", 468),
                                ("bufstr_free", 469), ("strncmp", 228), ("strncasecmp", 230),
                                ("strreplace", 484), ("strireplace", 485), ("buf_cvarlist", 517),
                                ("strlen", 114), ("strcat", 115)):
            add_ref("resource_" + builtin, add_function(builtin, -number))

        values = {}

        def float_global(key, value=0.0):
            values[key] = global_slot("fixture_resource_" + key, value=value)
            return values[key]

        def string_global(key, value):
            values[key] = global_slot("fixture_resource_" + key, 1, name(value))
            return values[key]

        for key, value in (("server_seed", "SSQC_OWNER"), ("client_seed", "CSQC_OWNER"),
                           ("set_text", "set-value"), ("append_text", "append-value"),
                           ("target_text", "target-value"), ("sort_a", "alpha"),
                           ("sort_b", "charlie"), ("sort_c", "delta"),
                           ("corrupt_text", "foreign-corruption"),
                           ("missing_pattern", "fixture_no_such_cvar_"),
                           ("known_cvar", "pr_checkextension"),
                           ("empty_string", ""),
                           ("cmp_a", "abc"), ("cmp_b", "Xabc"), ("cmp_b_upper", "XABC"),
                           ("replace_a", "a"), ("replace_b", "b"),
                           ("replace_mixed", "Aa"), ("replace_long", "aaaaaa")):
            string_global(key, value)
        for key, value in (("zero", 0), ("one", 1), ("two", 2),
                           ("order", 1), ("prefix_all", 0), ("limit", 3),
                           ("negative", -1), ("huge", 1e30), ("nan", float("nan")),
                           ("infinity", float("inf")), ("minus_infinity", -float("inf"))):
            float_global(key, value)
        for key in ("handle", "target", "sort_handle", "cvar_handle", "foreign_handle", "probe_handle"):
            float_global(key)
        float_global("cvar_populated")
        float_global("cvar_empty")
        float_global("foreign_size")
        string_global("foreign_get", "")
        float_global("live_size")
        string_global("live_zero", "")
        string_global("live_one", "")
        float_global("probe_size")
        string_global("probe_get", "")
        checks = {}

        def result(key, string=False):
            checks[key] = global_slot("fixture_" + ("string" if string else "float") + "_resource_" + key,
                                      1 if string else 2)
            return checks[key]

        def invoke(code, builtin, args=(), output=None, string_result=False):
            for index, (slot, is_string) in enumerate(args):
                code.append((store_string if is_string else store_float, slot, 4 + index * 3, 0))
            code.append((call_function - 1 + len(args), ref_slots["resource_" + builtin], 0, 0))
            if output is not None:
                code.append((store_string if string_result else store_float, 1, output, 0))

        def arg(key, string=False):
            return values[key], string

        def entry_ref(key, text, code):
            add_ref("resource_" + key, entry("fixture_resource_" + text, code))

        def prime(code, stem, string=False, retired=False):
            builtin = ("strcat" if string else "strlen") if retired else ("bufstr_get" if string else "buf_getsize")
            args = (arg("cmp_a", True),) if retired else ((arg("handle"), arg("zero")) if string else (arg("handle"),))
            invoke(code, builtin, args, result("prime_" + stem, string), string)

        for vm, seed in (("server", "server_seed"), ("client", "client_seed")):
            code = []
            invoke(code, "buf_create", output=values["handle"])
            invoke(code, "bufstr_set", (arg("handle"), arg("zero"), arg(seed, True)))
            entry_ref("setup_" + vm, "setup_" + vm, code)

        code = []
        invoke(code, "bufstr_set", (arg("handle"), arg("one"), arg("set_text", True)))
        invoke(code, "bufstr_get", (arg("handle"), arg("one")), result("owned_set", True), True)
        invoke(code, "bufstr_add", (arg("handle"), arg("append_text", True), arg("order")),
               result("owned_add"))
        invoke(code, "buf_getsize", (arg("handle"),), result("owned_size"))
        invoke(code, "buf_create", output=values["target"])
        invoke(code, "bufstr_set", (arg("target"), arg("zero"), arg("target_text", True)))
        invoke(code, "buf_copy", (arg("handle"), arg("target")))
        invoke(code, "buf_getsize", (arg("target"),), result("copy_size"))
        invoke(code, "bufstr_get", (arg("target"), arg("one")), result("copy_get", True), True)
        entry_ref("owned", "owned", code)

        code = []
        invoke(code, "buf_create", output=values["sort_handle"])
        invoke(code, "bufstr_set", (arg("sort_handle"), arg("zero"), arg("sort_a", True)))
        invoke(code, "bufstr_set", (arg("sort_handle"), arg("two"), arg("sort_b", True)))
        invoke(code, "buf_sort", (arg("sort_handle"), arg("prefix_all"), arg("zero")))
        invoke(code, "bufstr_add", (arg("sort_handle"), arg("sort_c", True), arg("order")),
               result("sort_add"))
        invoke(code, "bufstr_get", (arg("sort_handle"), arg("one")), result("sort_middle", True), True)
        invoke(code, "bufstr_get", (arg("sort_handle"), arg("two")), result("sort_tail", True), True)
        invoke(code, "bufstr_set", (arg("sort_handle"), arg("one"), arg("sort_c", True)))
        invoke(code, "bufstr_free", (arg("sort_handle"), arg("zero")))
        invoke(code, "bufstr_get", (arg("sort_handle"), arg("zero")), result("sort_freed", True), True)
        invoke(code, "buf_del", (arg("sort_handle"),))
        entry_ref("sort", "sort", code)

        code = []
        invoke(code, "buf_create", output=values["cvar_handle"])
        invoke(code, "bufstr_set", (arg("cvar_handle"), arg("zero"), arg("set_text", True)))
        invoke(code, "buf_cvarlist", (arg("cvar_handle"), arg("known_cvar", True)))
        invoke(code, "buf_getsize", (arg("cvar_handle"),), values["cvar_populated"])
        invoke(code, "bufstr_get", (arg("cvar_handle"), arg("zero")), result("cvar_name", True), True)
        invoke(code, "buf_cvarlist", (arg("cvar_handle"), arg("missing_pattern", True)))
        invoke(code, "buf_getsize", (arg("cvar_handle"),), values["cvar_empty"])
        invoke(code, "buf_del", (arg("cvar_handle"),))
        entry_ref("cvarlist", "cvarlist", code)

        code = []
        invoke(code, "strncmp", (arg("cmp_a", True), arg("cmp_b", True), arg("limit"), arg("zero"), arg("one")),
               result("compare_offset"))
        invoke(code, "strncasecmp", (arg("cmp_a", True), arg("cmp_b_upper", True), arg("limit"), arg("zero"), arg("one")),
               result("compare_insensitive"))
        invoke(code, "strncmp", (arg("cmp_a", True), arg("cmp_b", True), arg("limit"), arg("negative"), arg("one")),
               result("compare_negative"))
        invoke(code, "strncasecmp", (arg("cmp_a", True), arg("cmp_b_upper", True), arg("limit"), arg("zero"), arg("negative")),
               result("compare_negative_second"))
        invoke(code, "strreplace", (arg("replace_a", True), arg("replace_b", True), arg("replace_mixed", True)),
               result("replace_case", True), True)
        invoke(code, "strreplace", (arg("replace_a", True), arg("replace_b", True), arg("replace_long", True)),
               result("replace_long", True), True)
        invoke(code, "strireplace", (arg("replace_a", True), arg("replace_b", True), arg("replace_long", True)),
               result("replace_insensitive", True), True)
        invoke(code, "strireplace", (arg("replace_a", True), arg("replace_b", True), arg("replace_mixed", True)),
               result("replace_case_insensitive", True), True)
        entry_ref("strings", "strings", code)

        code = []
        prime(code, "foreign_size")
        invoke(code, "buf_getsize", (arg("foreign_handle"),), values["foreign_size"])
        prime(code, "foreign_get", True)
        invoke(code, "bufstr_get", (arg("foreign_handle"), arg("zero")), values["foreign_get"], True)
        invoke(code, "bufstr_set", (arg("foreign_handle"), arg("zero"), arg("corrupt_text", True)))
        invoke(code, "buf_del", (arg("foreign_handle"),))
        invoke(code, "buf_copy", (arg("foreign_handle"), arg("handle")))
        invoke(code, "buf_copy", (arg("handle"), arg("foreign_handle")))
        invoke(code, "buf_getsize", (arg("handle"),), values["live_size"])
        invoke(code, "bufstr_get", (arg("handle"), arg("zero")), values["live_zero"], True)
        invoke(code, "bufstr_get", (arg("handle"), arg("one")), values["live_one"], True)
        entry_ref("foreign", "foreign", code)

        code = []
        invalids = (("nan", "nan"), ("infinity", "infinity"),
                    ("minus_infinity", "minus_infinity"), ("negative", "negative"), ("huge", "huge"))
        for key, invalid in invalids:
            prime(code, "invalid_" + key + "_size")
            invoke(code, "buf_getsize", (arg(invalid),), result("invalid_" + key + "_size"))
            prime(code, "invalid_" + key + "_get", True)
            invoke(code, "bufstr_get", (arg(invalid), arg("zero")), result("invalid_" + key + "_get", True), True)
            invoke(code, "bufstr_set", (arg(invalid), arg("zero"), arg("corrupt_text", True)))
            invoke(code, "buf_del", (arg(invalid),))
            invoke(code, "buf_copy", (arg(invalid), arg("handle")))
            invoke(code, "buf_copy", (arg("handle"), arg(invalid)))
        invoke(code, "buf_getsize", (arg("handle"),), values["live_size"])
        invoke(code, "bufstr_get", (arg("handle"), arg("zero")), values["live_zero"], True)
        invoke(code, "bufstr_get", (arg("handle"), arg("one")), values["live_one"], True)
        entry_ref("invalid", "invalid", code)

        code = []
        prime(code, "probe_size", retired=True)
        invoke(code, "buf_getsize", (arg("probe_handle"),), values["probe_size"])
        prime(code, "probe_get", True, retired=True)
        invoke(code, "bufstr_get", (arg("probe_handle"), arg("zero")), values["probe_get"], True)
        entry_ref("probe", "probe", code)

        code = []
        invoke(code, "buf_getsize", (arg("handle"),), values["live_size"])
        invoke(code, "bufstr_get", (arg("handle"), arg("zero")), values["live_zero"], True)
        invoke(code, "bufstr_get", (arg("handle"), arg("one")), values["live_one"], True)
        entry_ref("assert_owner", "assert_owner", code)

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
    parser.add_argument("--resources", action="store_true", help="append the finite loaded buffer/string cases")
    args = parser.parse_args()
    result = assemble(source_program(args.source_pack), args.resources)
    for output in (args.output, args.csqc_output):
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(result)
    print("QC_BINDING_PROGRAM_WRITTEN bytes=" + str(len(result)))


if __name__ == "__main__":
    main()
