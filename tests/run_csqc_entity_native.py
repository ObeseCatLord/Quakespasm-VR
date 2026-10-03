#!/usr/bin/env python3
"""Build/run a captured native SSQC-to-CSQC entity lifecycle witness."""
import argparse
import hashlib
import json
import os
import re
import shlex
import struct
import subprocess
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
FASTGAMES = Path("/home/obesecatlord/FastGames")
DEFAULT_BASE = Path("/home/obesecatlord/Windows/Games/quakespasm_straight")
PAYLOAD_SIZE = 56


def sections(program):
    header = list(struct.unpack_from("<15i", program))
    result = []
    for slot, width in ((2, 8), (4, 8), (6, 8), (8, 36), (10, 1), (12, 4)):
        offset, count = header[slot:slot + 2]
        if offset < 60 or count < 0 or offset + count * width > len(program):
            raise ValueError("invalid source QC section")
        result.append(bytearray(program[offset:offset + count * width]))
    return header, result


def append_lifecycle(program):
    """Append finite loaded QC callbacks to qc_binding_program's entities output."""
    import struct

    header, parts = sections(program)
    statements, global_defs, fields, functions, strings, globals_data = parts

    def text_at(offset):
        end = strings.index(0, offset)
        return bytes(strings[offset:end]).decode("ascii", "replace")

    globals_by_name = {}
    for pos in range(0, len(global_defs), 8):
        kind, slot, name_offset = struct.unpack_from("<HHi", global_defs, pos)
        globals_by_name[text_at(name_offset)] = (kind & 0x7fff, slot)
    fields_by_name = {}
    for pos in range(0, len(fields), 8):
        kind, offset, name_offset = struct.unpack_from("<HHi", fields, pos)
        fields_by_name[text_at(name_offset)] = (kind & 0x7fff, offset)
    function_by_name = {}
    for pos in range(0, len(functions), 36):
        first, parm, locals_count, profile, name_offset, source, count = struct.unpack_from(
            "<7i", functions, pos
        )
        function_by_name[text_at(name_offset)] = pos // 36

    def name(value):
        result = len(strings)
        strings.extend(value.encode("ascii") + b"\0")
        return result

    def global_slot(value, kind=2, initial=0):
        if value in globals_by_name:
            raise ValueError("duplicate generated global " + value)
        slot = len(globals_data) // 4
        if slot + (3 if kind == 3 else 1) > 32768:
            raise ValueError("generated QC exceeds classic statement offsets")
        global_defs.extend(struct.pack("<HHi", kind, slot, name(value)))
        globals_data.extend(struct.pack("<3f", *initial) if kind == 3 else
                            struct.pack("<i", initial) if kind != 2 else
                            struct.pack("<f", initial))
        globals_by_name[value] = (kind, slot)
        return slot

    def field_slot(value, kind):
        nonlocal header
        if value in fields_by_name:
            old_kind, offset = fields_by_name[value]
            if old_kind != kind:
                raise ValueError("unexpected existing field type: " + value)
            return offset
        offset = header[14]
        if offset > 32767:
            raise ValueError("generated QC field offset exceeds classic range")
        fields.extend(struct.pack("<HHi", kind, offset, name(value)))
        fields_by_name[value] = (kind, offset)
        header[14] += 1
        return offset

    def field_global(value, kind):
        return global_slot("fixture_lifecycle_field_" + value, 5,
                           field_slot(value, kind))

    def emit(code):
        start = len(statements) // 8
        for instruction in code:
            statements.extend(struct.pack("<4h", *instruction))
        return start

    def add_function(value, first, parm_start=0, locals_count=0, parm_sizes=()):
        if value in function_by_name:
            raise ValueError("duplicate generated function " + value)
        index = len(functions) // 36
        functions.extend(struct.pack("<7i8B", first, parm_start, locals_count, 0,
                                     name(value), 0, len(parm_sizes), *parm_sizes,
                                     *([0] * (8 - len(parm_sizes)))))
        function_by_name[value] = index
        return index

    self_slot = globals_by_name.get("self", (None, None))[1]
    if self_slot is None:
        raise ValueError("licensed program has no self global")

    send_field = field_global("SendEntity", 6)
    flags_field = field_global("SendFlags", 2)
    num_field = field_global("entnum", 2)
    field_slot("pvsflags", 2)
    payload_field_offset = field_slot("fixture_lifecycle_payload", 2)
    client_value_field_offset = field_slot("fixture_lifecycle_value", 2)
    payload_field = field_global("fixture_lifecycle_payload", 2)
    value_field = field_global("fixture_lifecycle_value", 2)
    del send_field, flags_field  # native C binds these two through real edict fields

    one = global_slot("fixture_lifecycle_one", initial=1)
    two = global_slot("fixture_lifecycle_two", initial=2)
    three = global_slot("fixture_lifecycle_three", initial=3)
    four = global_slot("fixture_lifecycle_four", initial=4)
    trace_scale = global_slot("fixture_lifecycle_trace_scale", initial=2048)
    update_count = global_slot("fixture_lifecycle_update_count")
    new_count = global_slot("fixture_lifecycle_new_count")
    remove_count = global_slot("fixture_lifecycle_remove_count")
    trace = global_slot("fixture_lifecycle_trace")
    last_id = global_slot("fixture_lifecycle_last_id")
    last_payload = global_slot("fixture_lifecycle_last_payload")
    last_new = global_slot("fixture_lifecycle_last_new")
    last_event = global_slot("fixture_lifecycle_last_event")
    last_removed_id = global_slot("fixture_lifecycle_last_removed_id")
    last_removed_payload = global_slot("fixture_lifecycle_last_removed_payload")
    read_value = global_slot("fixture_lifecycle_read_value")
    field_pointer = global_slot("fixture_lifecycle_field_pointer", 7)
    event_base = global_slot("fixture_lifecycle_event_base")
    event_kind = global_slot("fixture_lifecycle_event_kind")
    event_code = global_slot("fixture_lifecycle_event_code")
    trace_scaled = global_slot("fixture_lifecycle_trace_scaled")
    send_value = global_slot("fixture_lifecycle_send_value")
    msg_entity = global_slot("fixture_lifecycle_msg_entity", initial=5)

    writebyte_index = function_by_name.get("WriteByte")
    if writebyte_index is None:
        raise ValueError("stock generator output has no WriteByte builtin declaration")
    readbyte_index = function_by_name.get("readbyte")
    if readbyte_index is None:
        readbyte_index = add_function("readbyte", -360)
    elif struct.unpack_from("<i", functions, readbyte_index * 36)[0] != -360:
        raise ValueError("readbyte is not the expected native CSQC builtin")
    writebyte_ref = global_slot("fixture_lifecycle_writebyte", 6, writebyte_index)
    readbyte_ref = global_slot("fixture_lifecycle_readbyte", 6, readbyte_index)

    # The generated SendEntity takes the production two-argument signature.
    send_parm_to = global_slot("fixture_lifecycle_send_to", 4)
    global_slot("fixture_lifecycle_send_flags", 2)
    send_code = []
    for _ in range(PAYLOAD_SIZE):
        send_code.extend(((31, msg_entity, 4, 0),
                          (24, self_slot, payload_field, send_value),
                          (31, send_value, 7, 0),
                          (53, writebyte_ref, 0, 0)))
    send_code.append((43, one, 0, 0))
    send_index = add_function("fixture_lifecycle_send_entity", emit(send_code),
                              send_parm_to, 2, (1, 1))
    del send_index, payload_field_offset, client_value_field_offset

    # Patch the API-discovered empty CSQC update declaration in place.
    update_index = function_by_name.get("CSQC_Ent_Update")
    if update_index is None:
        raise ValueError("entities generator did not append CSQC_Ent_Update")
    update_parm = global_slot("fixture_lifecycle_isnew", 2)
    update_code = [(51, readbyte_ref, 0, 0), (31, 1, read_value, 0)]
    for _ in range(PAYLOAD_SIZE - 1):
        update_code.append((51, readbyte_ref, 0, 0))
    update_code.extend(((30, self_slot, value_field, field_pointer),
                        (37, read_value, field_pointer, 0),
                        (6, update_count, one, update_count),
                        (6, new_count, update_parm, new_count),
                        (24, self_slot, num_field, last_id),
                        (31, read_value, last_payload, 0),
                        (31, update_parm, last_new, 0),
                        (1, last_id, four, event_base),
                        (8, two, update_parm, event_kind),
                        (6, event_base, event_kind, event_code),
                        (1, trace, trace_scale, trace_scaled),
                        (6, trace_scaled, event_code, trace),
                        (6, update_parm, one, last_event),
                        (43, 0, 0, 0)))
    update_start = emit(update_code)
    function_offset = update_index * 36
    struct.pack_into("<3i", functions, function_offset, update_start, update_parm, 1)
    struct.pack_into("<i", functions, function_offset + 24, 1)
    functions[function_offset + 28:function_offset + 36] = bytes((1, 0, 0, 0, 0, 0, 0, 0))

    remove_code = [(6, remove_count, one, remove_count),
                   (24, self_slot, num_field, last_id),
                   (31, last_id, last_removed_id, 0),
                   (24, self_slot, value_field, last_removed_payload),
                   (31, three, last_event, 0),
                   (1, last_id, four, event_base),
                   (6, event_base, three, event_code),
                   (1, trace, trace_scale, trace_scaled),
                   (6, trace_scaled, event_code, trace),
                   (43, 0, 0, 0)]
    add_function("CSQC_Ent_Remove", emit(remove_code))

    strings.extend(b"\0" * (-len(strings) % 4))
    output = bytearray(60)
    for section, (slot, width) in zip(parts, ((2, 8), (4, 8), (6, 8),
                                             (8, 36), (10, 1), (12, 4))):
        header[slot:slot + 2] = len(output), len(section) // width
        output.extend(section)
    struct.pack_into("<15i", output, 0, *header)
    return bytes(output)


def write_private_qc_pack(path, programs):
    """Mount the generated QC after licensed/embedded packs in this profile."""
    data = bytearray(12)
    directory = bytearray()
    for filename, content in programs.items():
        encoded = filename.encode("ascii")
        if len(encoded) >= 56:
            raise ValueError("private QC pack filename is too long")
        position = len(data)
        data.extend(content)
        directory.extend(struct.pack("<56sii", encoded, position, len(content)))
    directory_offset = len(data)
    data.extend(directory)
    struct.pack_into("<4sii", data, 0, b"PACK", directory_offset, len(directory))
    path.write_bytes(data)


def native_link_recipe(graph, output):
    """Read the existing Ninja target recipe without invoking regeneration."""
    lines = (graph / "build.ninja").read_text().splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("build vkquake:"):
            continue
        tokens = shlex.split(line)
        if len(tokens) < 4 or tokens[2] not in ("c_LINKER", "cpp_LINKER"):
            raise RuntimeError("unexpected native vkquake link rule")
        inputs = []
        for token in tokens[3:]:
            if token == "|":
                break
            inputs.append(token)
        attrs = {}
        for following in lines[index + 1:]:
            if following.startswith("build "):
                break
            match = re.match(r"\s+(ARGS|LINK_ARGS) = (.*)$", following)
            if match:
                attrs[match.group(1)] = match.group(2)
        if "LINK_ARGS" not in attrs:
            raise RuntimeError("native vkquake target has no link arguments")
        linker = "c++" if tokens[2] == "cpp_LINKER" else "cc"
        return ([linker] + shlex.split(attrs.get("ARGS", "")) + ["-o", str(output)] +
                inputs + shlex.split(attrs["LINK_ARGS"]))
    raise RuntimeError("native object graph has no vkquake link recipe")


def remove_depfile_args(arguments):
    result = []
    i = 0
    while i < len(arguments):
        if arguments[i] in ("-MD", "-MMD"):
            i += 1
        elif arguments[i] in ("-MF", "-MQ"):
            i += 2
        else:
            result.append(arguments[i])
            i += 1
    return result


def build_fixture(graph, output, source):
    commands = json.loads((graph / "compile_commands.json").read_text())
    entry = next((item for item in commands if item["file"].endswith("/Quake/sv_user.c")), None)
    if not entry:
        raise RuntimeError("native graph has no sv_user compile recipe")
    compile_args = entry.get("arguments") or shlex.split(entry["command"])
    if "-D_DEBUG" not in compile_args or any(arg.startswith("-DNDEBUG") for arg in compile_args):
        raise RuntimeError("native graph is not assertion-enabled Debug")
    compile_args = remove_depfile_args(compile_args)
    fixture_object = output.with_suffix(".o")
    compile_args[compile_args.index("-o") + 1] = str(fixture_object)
    compile_args[compile_args.index("-c") + 1] = str(source)
    subprocess.run(compile_args, cwd=entry["directory"], check=True)

    link_args = native_link_recipe(graph, output)
    replacements = {
        "vkquake.p/Quake_main_sdl.c.o",
        "vkquake.p/Quake_sv_main.c.o",
        "vkquake.p/Quake_cl_demo.c.o",
        "vkquake.p/Quake_cl_parse.c.o",
    }
    if any(link_args.count(item) != 1 for item in replacements):
        raise RuntimeError("unexpected native owner graph; refusing broad replacement")
    engine_object_count = sum(item.endswith(".o") for item in link_args)
    if engine_object_count != 225:
        raise RuntimeError(f"expected the assertion-enabled native225 object graph, found {engine_object_count}")
    if "-o" not in link_args:
        raise RuntimeError("link recipe has no output")
    link_args = [item for item in link_args if item not in replacements]
    link_args.insert(link_args.index("-Wl,--start-group"), str(fixture_object))
    link_args.extend(("-Wl,--wrap=Loop_Init", "-Wl,--wrap=NET_CanSendMessage",
                      "-Wl,--wrap=NET_SendUnreliableMessage",
                      "-Wl,--wrap=R_TranslateNewPlayerSkin"))
    subprocess.run(link_args, cwd=graph, check=True)
    return fixture_object


def private_loader_paths(graph):
    commands = json.loads((graph / "compile_commands.json").read_text())
    entry = next(item for item in commands if item["file"].endswith("/Quake/sv_user.c"))
    arguments = entry.get("arguments") or shlex.split(entry["command"])
    paths = []
    for argument in arguments:
        if argument.startswith("-I"):
            include = Path(argument[2:])
            if include.name == "include" and include.parent.name == "deps":
                library = include.parent / "lib"
                if library.is_dir():
                    paths.append(str(library))
    return list(dict.fromkeys(paths))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build-debug")
    parser.add_argument("--basedir", type=Path, default=DEFAULT_BASE)
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

    graph = args.build_dir.resolve(strict=True)
    basedir = args.basedir.resolve(strict=True)
    pack = basedir / "id1" / "pak0.pak"
    if not pack.is_file():
        raise SystemExit("licensed source pack is missing")
    if not (graph / "compile_commands.json").is_file() or not (graph / "build.ninja").is_file():
        raise SystemExit("prepared native Ninja graph is missing")

    if args.artifact_root:
        root = args.artifact_root.expanduser().resolve()
        root.mkdir(parents=True, exist_ok=False)
    else:
        FASTGAMES.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix="qsvr-csqc-entity-native-", dir=FASTGAMES))
    profile = root / "profile"
    id1 = profile / "id1"
    id1.mkdir(parents=True)
    for name in ("pak0.pak", "pak1.pak"):
        source_pack = basedir / "id1" / name
        if source_pack.is_file():
            (id1 / name).symlink_to(source_pack.resolve())

    import sys
    sys.path.insert(0, str(REPO / "tests"))
    import cooperative_qc_program
    import qc_binding_program

    generated = append_lifecycle(qc_binding_program.assemble(
        cooperative_qc_program.source_program(pack), entities=True))
    (id1 / "progs.dat").write_bytes(generated)
    (id1 / "csprogs.dat").write_bytes(generated)
    override_index = 2 if (id1 / "pak1.pak").is_file() else 1
    qc_pack = id1 / f"pak{override_index}.pak"
    write_private_qc_pack(qc_pack, {"progs.dat": generated, "csprogs.dat": generated})
    program_hash = hashlib.sha256(generated).hexdigest()

    output = root / "csqc-entity-native"
    fixture_object = build_fixture(graph, output, REPO / "tests" / "csqc_entity_native_fixture.c")
    git_head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    source_hashes = {}
    for name in (
        "Quake/sv_main.c", "Quake/cl_parse.c", "Quake/cl_main.c", "Quake/cl_input.c",
        "tests/csqc_entity_native_fixture.c", "tests/mixed_native_fixture.c",
        "tests/qc_binding_program.py", "tests/cooperative_qc_program.py",
        "tests/run_csqc_entity_native.py",
    ):
        source_hashes[name] = hashlib.sha256((REPO / name).read_bytes()).hexdigest()
    graph_hash = hashlib.sha256((graph / "build.ninja").read_bytes()).hexdigest()
    print("CSQC_ENTITY_NATIVE_BUILD_PASSED assertions=enabled link_recipe=native_debug_replacement_only "
          "engine_objects=225 replaced_owners=4 graph_sha256=" + graph_hash)
    print("CSQC_ENTITY_NATIVE_SOURCE head=" + git_head + " program_sha256=" + program_hash +
          " source_sha256=" + ",".join(key + ":" + value for key, value in source_hashes.items()))
    print("CSQC_ENTITY_NATIVE_PRIVATE_BUILD_ROOT=" + str(graph) + " engine_objects=225 assertions=enabled")
    print("CSQC_ENTITY_NATIVE_PRIVATE_PROFILE=" + str(profile))
    print("CSQC_ENTITY_NATIVE_PRIVATE_QC_PACK=" + str(qc_pack))

    command = [str(output), "-dedicated", "16", "-noudp", "-nosound", "-nosteamapi",
               "-basedir", str(profile), "-userdir", str(profile), "-csqc-entity-native"]
    env = os.environ.copy()
    loader_paths = private_loader_paths(graph)
    if loader_paths:
        prior = env.get("LD_LIBRARY_PATH")
        env["LD_LIBRARY_PATH"] = ":".join(loader_paths + ([prior] if prior else []))
        print("CSQC_ENTITY_NATIVE_PRIVATE_LOADER_PATHS=" + ",".join(loader_paths))
    try:
        result = subprocess.run(command, cwd=profile, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=args.timeout,
                                env=env)
    except subprocess.TimeoutExpired as error:
        print((error.stdout or "")[-6000:])
        raise SystemExit("CSQC_ENTITY_NATIVE_FAIL timeout profile=" + str(profile))
    log = root / "fixture.log"
    log.write_text(result.stdout)
    required = ("CSQC_ENTITY_NATIVE_CREATE_PASSED", "CSQC_ENTITY_NATIVE_SPLIT_UPDATE_ACK_PASSED",
                "CSQC_ENTITY_NATIVE_REENABLE_PASSED", "CSQC_ENTITY_NATIVE_REMOVE_ACK_PASSED",
                "CSQC_ENTITY_NATIVE_REUSE_PASSED", "CSQC_ENTITY_NATIVE_PASSED")
    for line in result.stdout.splitlines():
        if line.startswith("CSQC_ENTITY_NATIVE_"):
            print(line)
    if result.returncode or any(marker not in result.stdout for marker in required):
        raise SystemExit(f"CSQC_ENTITY_NATIVE_FAIL exit={result.returncode} profile={profile} log={log}")
    print("CSQC_ENTITY_NATIVE_EXIT=0 log=" + str(log))


if __name__ == "__main__":
    main()
