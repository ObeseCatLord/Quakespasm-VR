#!/usr/bin/env python3
"""Link a dedicated parser fixture from the current Meson native object graph."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
FASTGAMES = Path("/home/obesecatlord/FastGames").resolve()
DEFAULT_GRAPH = FASTGAMES / "qsvr-protected-native-72o89z1i/native"


def ninja_commands(graph, target):
    result = subprocess.run(["ninja", "-C", str(graph), "-t", "commands", target],
                            check=True, stdout=subprocess.PIPE, text=True)
    lines = [line for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("Meson graph returned no command for " + target)
    return lines


def remove_depfile_args(tokens):
    result = []
    index = 0
    while index < len(tokens):
        if tokens[index] in ("-MD", "-MMD"):
            index += 1
        elif tokens[index] in ("-MQ", "-MF"):
            index += 2
        else:
            result.append(tokens[index])
            index += 1
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--graph", type=Path, default=DEFAULT_GRAPH)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    graph = args.graph.resolve(strict=True)
    output = args.output.resolve()
    if not output.is_relative_to(FASTGAMES):
        raise SystemExit("fixture output must stay under " + str(FASTGAMES))
    if not (graph / "build.ninja").is_file():
        raise SystemExit("Meson build graph not found: " + str(graph))
    sv_user_object = graph / "vkquake.p/Quake_sv_user.c.o"
    if not sv_user_object.is_file():
        raise SystemExit("prepared sv_user object missing: " + str(sv_user_object))

    compile_line = next((line for line in ninja_commands(
        graph, "vkquake.p/Quake_sv_user.c.o") if "-c " in line and "sv_user.c" in line), None)
    if not compile_line:
        raise SystemExit("cannot find the Meson sv_user compile command")
    compile_args = remove_depfile_args(shlex.split(compile_line))
    if "-D_DEBUG" not in compile_args or "-DNDEBUG" in compile_args:
        raise SystemExit("the source graph is not assertion-enabled Debug")
    fixture_object = output.with_name("server_request_native_fixture.o")
    output.parent.mkdir(parents=True, exist_ok=True)
    if "-o" not in compile_args or "-c" not in compile_args:
        raise SystemExit("unrecognized Meson compile command")
    compile_args[compile_args.index("-o") + 1] = str(fixture_object)
    compile_args[compile_args.index("-c") + 1] = str(REPO / "tests/server_request_native_fixture.c")
    environment = os.environ.copy()
    environment["TMPDIR"] = str(output.parent)
    subprocess.run(compile_args, cwd=graph, env=environment, check=True)

    link_lines = ninja_commands(graph, "vkquake")
    link_line = None
    for line in reversed(link_lines):
        tokens = shlex.split(line)
        if "-o" in tokens and tokens[tokens.index("-o") + 1] == "vkquake":
            link_line = line
            break
    if not link_line:
        raise SystemExit("cannot find the Meson application link command")
    link_args = shlex.split(link_line)
    old_main = "vkquake.p/Quake_main_sdl.c.o"
    if link_args.count(old_main) != 1 or link_args.count("vkquake.p/Quake_sv_user.c.o") != 1:
        raise SystemExit("Meson link graph does not contain the expected main/sv_user objects")
    link_args[link_args.index(old_main)] = str(fixture_object)
    link_args[link_args.index("-o") + 1] = str(output)
    link_args.extend(("-Wl,--wrap=Loop_Init", "-Wl,--wrap=NET_GetServerMessage",
                      "-Wl,--wrap=NET_CanSendMessage", "-Wl,--wrap=SV_DropClient",
                      "-Wl,--wrap=PR_ExecuteProgram"))
    subprocess.run(link_args, cwd=graph, env=environment, check=True)
    print("SERVER_REQUEST_FIXTURE_BUILT path=" + str(output))
    print("SERVER_REQUEST_FIXTURE_SV_USER=" + str(sv_user_object))
    print("SERVER_REQUEST_FIXTURE_ASSERTIONS=enabled")


if __name__ == "__main__":
    main()
