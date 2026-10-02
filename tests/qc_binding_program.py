#!/usr/bin/env python3
"""Append a narrow loaded-QC binding fixture to licensed stock progs.dat."""
import argparse
import struct
from pathlib import Path

from cooperative_qc_program import source_program


def assemble(program, resources=False, files=False):
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

    if files:
        for builtin, number in (("fopen", 110), ("fclose", 111), ("fgets", 112), ("fputs", 113),
                                ("strlen", 114), ("strcat", 115), ("search_begin", 444), ("search_end", 445),
                                ("search_getsize", 446), ("search_getfilename", 447),
                                ("buf_create", 460), ("buf_getsize", 462), ("bufstr_get", 466), ("bufstr_set", 467),
                                ("bufstr_free", 469), ("buf_loadfile", 535), ("buf_writefile", 536)):
            add_ref("files_" + builtin, add_function(builtin, -number))

        vals = {}

        def ff(key, value=0.0):
            vals[key] = global_slot("fixture_files_" + key, value=value)
            return vals[key]

        def fs(key, value):
            vals[key] = global_slot("fixture_files_" + key, 1, name(value))
            return vals[key]

        for key, value in (("server_seed", "SSQC_OWNER"), ("client_seed", "CSQC_OWNER"),
                           ("input", "fixture-read.txt"), ("empty", "fixture-empty.txt"),
                           ("pattern", "fixture-search/*"), ("missing_pattern", "fixture-no-such-search/*"),
                           ("server_output", "fixture-server-output.txt"),
                           ("client_output", "fixture-client-output.txt"), ("foreign_write", "foreign-corruption\n"),
                           ("owner_write", "owner-after-foreign\n"),
                           ("empty_text", ""), ("prime_text", "return-prime"),
                           ("bad_parent", "../escape.txt"), ("bad_absolute", "/absolute.txt"),
                           ("bad_colon", "C:drive.txt"), ("bad_backslash", "bad\\path.txt")):
            fs(key, value)
        for key, value in (("zero", 0), ("one", 1), ("two", 2), ("three", 3), ("read_mode", 0),
                           ("write_mode", 2), ("append_mode", 1), ("flags", 0), ("quiet", 1), ("negative", -1),
                           ("huge", 1e30), ("nan", float("nan")), ("infinity", float("inf")),
                           ("minus_infinity", -float("inf"))):
            ff(key, value)
        for key in ("buffer", "stream", "writer", "search", "search_probe", "foreign_buffer",
                    "foreign_stream", "foreign_writer", "foreign_search", "query_handle", "query_index", "closed_handle",
                    "extra_handle", "close_handle", "read_result", "empty_result", "size", "load_result",
                    "empty_load_result", "write_result", "search_size", "foreign_load_result",
                    "foreign_write_file_result", "foreign_write_buffer_result", "foreign_search_size", "empty_search", "invalid_open_parent",
                    "invalid_open_absolute", "invalid_open_colon", "invalid_open_backslash",
                    "invalid_search_parent", "invalid_search_absolute", "invalid_search_colon",
                    "invalid_search_backslash", "retired_buffer_size", "retired_search_size"):
            ff(key)
        for key in ("loaded_first", "loaded_blank", "loaded_last", "buffer_zero", "buffer_hole",
                    "buffer_blank", "buffer_last", "line_first", "line_blank", "line_last", "line_eof",
                    "foreign_line", "foreign_search_name", "search_name", "query_prime", "file_prime",
                    "retired_buffer_string", "retired_file_string", "retired_search_string"):
            fs(key, "")
        for key in ("output", "open_result", "bad_handle",
                    "bad_index", "retired_buffer", "retired_stream", "retired_search"):
            ff(key)

        out = {}

        def arg(key, string=False):
            return vals[key], string

        def invoke(code, builtin, args=(), output=None, string_result=False):
            for index, (slot, is_string) in enumerate(args):
                code.append((store_string if is_string else store_float, slot, 4 + index * 3, 0))
            code.append((call_function - 1 + len(args), ref_slots["files_" + builtin], 0, 0))
            if output is not None:
                code.append((store_string if string_result else store_float, 1, output, 0))

        def files_entry(key, code):
            add_ref("files_" + key, entry("fixture_files_" + key, code))

        def prime_float(code, key):
            invoke(code, "strlen", (arg("prime_text", True),), ff("prime_" + key))

        for vm in ("server", "client"):
            code = []
            invoke(code, "buf_create", output=vals["buffer"])
            invoke(code, "bufstr_set", (arg("buffer"), arg("zero"), arg(vm + "_seed", True)))
            invoke(code, "buf_loadfile", (arg("input", True), arg("buffer")), out.setdefault("load_" + vm, global_slot("fixture_files_load_" + vm)))
            invoke(code, "buf_loadfile", (arg("empty", True), arg("buffer")), out.setdefault("empty_load_" + vm, global_slot("fixture_files_empty_load_" + vm)))
            invoke(code, "buf_getsize", (arg("buffer"),), out.setdefault("setup_size_" + vm, global_slot("fixture_files_setup_size_" + vm)))
            for index, label in (("one", "first"), ("two", "blank"), ("three", "last")):
                invoke(code, "bufstr_get", (arg("buffer"), arg(index)), out.setdefault("loaded_" + label + "_" + vm, global_slot("fixture_files_loaded_" + label + "_" + vm, 1)), True)
            invoke(code, "bufstr_free", (arg("buffer"), arg("one")))
            invoke(code, "buf_getsize", (arg("buffer"),), out.setdefault("post_free_size_" + vm, global_slot("fixture_files_post_free_size_" + vm)))
            invoke(code, "fopen", (arg("input", True), arg("read_mode")), vals["stream"])
            invoke(code, "search_begin", (arg("pattern", True), arg("flags"), arg("quiet")), vals["search"])
            invoke(code, "fopen", (arg(vm + "_output", True), arg("write_mode")), vals["writer"])
            invoke(code, "buf_writefile", (arg("writer"), arg("buffer")), vals["write_result"])
            invoke(code, "fclose", (arg("writer"),))
            files_entry("setup_" + vm, code)

        code = []
        invoke(code, "fgets", (arg("stream"),), vals["line_first"], True)
        files_entry("read_first", code)
        code = []
        invoke(code, "fgets", (arg("stream"),), vals["line_blank"], True)
        invoke(code, "fgets", (arg("stream"),), vals["line_last"], True)
        invoke(code, "fgets", (arg("stream"),), vals["line_eof"], True)
        files_entry("read_rest", code)
        code = []
        invoke(code, "fclose", (arg("stream"),))
        invoke(code, "fopen", (arg("input", True), arg("read_mode")), vals["stream"])
        files_entry("reopen_stream", code)
        for vm in ("server", "client"):
            code = []
            invoke(code, "fopen", (arg(vm + "_output", True), arg("append_mode")), vals["writer"])
            files_entry("reopen_writer_" + vm, code)

        code = []
        invoke(code, "fgets", (arg("stream"),), vals["line_first"], True)
        files_entry("read_after_foreign", code)

        code = []
        invoke(code, "fgets", (arg("stream"),), vals["line_blank"], True)
        files_entry("read_blank", code)
        code = []
        invoke(code, "fgets", (arg("stream"),), vals["line_last"], True)
        invoke(code, "fgets", (arg("stream"),), vals["line_eof"], True)
        files_entry("read_tail", code)

        code = []
        invoke(code, "buf_getsize", (arg("buffer"),), vals["size"])
        for index, key in ((0, "buffer_zero"), (1, "buffer_hole"), (2, "buffer_blank"), (3, "buffer_last")):
            index_key = "zero" if index == 0 else "one" if index == 1 else "two" if index == 2 else "three"
            invoke(code, "bufstr_get", (arg("buffer"), arg(index_key)), vals[key], True)
        invoke(code, "search_getsize", (arg("search"),), vals["search_size"])
        invoke(code, "search_getfilename", (arg("search"), arg("query_index")), vals["search_name"], True)
        files_entry("assert_owner", code)
        code = []
        invoke(code, "search_getsize", (arg("search"),), vals["search_size"])
        invoke(code, "search_getfilename", (arg("search"), arg("query_index")), vals["search_name"], True)
        files_entry("search_only", code)

        code = []
        invoke(code, "strcat", (arg("prime_text", True),), vals["file_prime"], True)
        invoke(code, "fgets", (arg("foreign_stream"),), vals["foreign_line"], True)
        invoke(code, "fputs", (arg("foreign_writer"), arg("foreign_write", True)))
        prime_float(code, "foreign_write_file")
        invoke(code, "buf_writefile", (arg("foreign_writer"), arg("buffer")), vals["foreign_write_file_result"])
        prime_float(code, "foreign_write_buffer")
        invoke(code, "buf_writefile", (arg("writer"), arg("foreign_buffer")), vals["foreign_write_buffer_result"])
        invoke(code, "fclose", (arg("foreign_stream"),))
        invoke(code, "fclose", (arg("foreign_writer"),))
        prime_float(code, "foreign_load")
        invoke(code, "buf_loadfile", (arg("input", True), arg("foreign_buffer")), vals["foreign_load_result"])
        invoke(code, "search_getfilename", (arg("search"), arg("zero")), vals["query_prime"], True)
        invoke(code, "search_end", (arg("foreign_search"),))
        invoke(code, "search_getfilename", (arg("foreign_search"), arg("zero")), vals["foreign_search_name"], True)
        prime_float(code, "foreign_size")
        invoke(code, "search_getsize", (arg("foreign_search"),), vals["foreign_search_size"])
        files_entry("foreign", code)

        code = []
        invalid_paths = (("parent", "bad_parent"), ("absolute", "bad_absolute"),
                         ("colon", "bad_colon"), ("backslash", "bad_backslash"))
        for label, path in invalid_paths:
            invoke(code, "fopen", (arg(path, True), arg("read_mode")), vals["invalid_open_" + label])
            invoke(code, "search_begin", (arg(path, True), arg("flags"), arg("quiet")), vals["invalid_search_" + label])
        files_entry("invalid_paths", code)

        code = []
        invoke(code, "search_begin", (arg("missing_pattern", True), arg("flags"), arg("quiet")), vals["empty_search"])
        files_entry("empty_search", code)

        code = []
        invoke(code, "search_begin", (arg("pattern", True), arg("flags"), arg("quiet")), vals["search_probe"])
        invoke(code, "search_end", (arg("search_probe"),))
        invoke(code, "search_getfilename", (arg("search"), arg("zero")), vals["query_prime"], True)
        invoke(code, "search_getfilename", (arg("search_probe"), arg("zero")), vals["foreign_search_name"], True)
        prime_float(code, "closed_size")
        invoke(code, "search_getsize", (arg("search_probe"),), vals["closed_handle"])
        files_entry("closed_query", code)

        code = []
        invalids = ("nan", "infinity", "minus_infinity", "negative", "huge")
        for key in invalids:
            invoke(code, "search_getfilename", (arg("search"), arg("zero")), vals["query_prime"], True)
            bad_name = global_slot("fixture_files_invalid_handle_" + key, 1)
            bad_size = global_slot("fixture_files_invalid_size_" + key)
            invoke(code, "search_getfilename", (arg(key), arg("zero")), bad_name, True)
            prime_float(code, "invalid_size_" + key)
            invoke(code, "search_getsize", (arg(key),), bad_size)
            invoke(code, "search_end", (arg(key),))
            invoke(code, "strcat", (arg("prime_text", True),), vals["file_prime"], True)
            bad_stream = global_slot("fixture_files_invalid_stream_" + key, 1)
            invoke(code, "fgets", (arg(key),), bad_stream, True)
            invoke(code, "fclose", (arg(key),))
        for key in invalids:
            invoke(code, "search_getfilename", (arg("search"), arg("zero")), vals["query_prime"], True)
            bad_name = global_slot("fixture_files_invalid_index_" + key, 1)
            invoke(code, "search_getfilename", (arg("search"), arg(key)), bad_name, True)
        files_entry("invalid_queries", code)

        code = []
        prime_float(code, "retired_buffer_size")
        invoke(code, "buf_getsize", (arg("retired_buffer"),), vals["retired_buffer_size"])
        invoke(code, "strcat", (arg("prime_text", True),), vals["query_prime"], True)
        invoke(code, "bufstr_get", (arg("retired_buffer"), arg("zero")), vals["retired_buffer_string"], True)
        invoke(code, "strcat", (arg("prime_text", True),), vals["query_prime"], True)
        invoke(code, "fgets", (arg("retired_stream"),), vals["retired_file_string"], True)
        invoke(code, "strcat", (arg("prime_text", True),), vals["query_prime"], True)
        invoke(code, "search_getfilename", (arg("retired_search"), arg("zero")), vals["retired_search_string"], True)
        prime_float(code, "retired_search_size")
        invoke(code, "search_getsize", (arg("retired_search"),), vals["retired_search_size"])
        files_entry("probe_retired", code)

        code = []
        invoke(code, "search_begin", (arg("pattern", True), arg("flags"), arg("quiet")), vals["extra_handle"])
        files_entry("open_extra", code)
        code = []
        invoke(code, "search_end", (arg("close_handle"),))
        files_entry("close_search", code)
        code = []
        invoke(code, "fputs", (arg("writer"), arg("owner_write", True)))
        invoke(code, "fclose", (arg("writer"),))
        files_entry("close_writer", code)

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
    parser.add_argument("--files", "-files", action="store_true", help="append loaded file/search ownership cases")
    args = parser.parse_args()
    if args.resources and args.files:
        parser.error("--resources and --files are separate fixture modes")
    result = assemble(source_program(args.source_pack), args.resources, args.files)
    for output in (args.output, args.csqc_output):
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(result)
    print("QC_BINDING_PROGRAM_WRITTEN bytes=" + str(len(result)))


if __name__ == "__main__":
    main()
