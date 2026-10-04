#!/usr/bin/env python3
"""Build two bounded upstream save/signon witnesses on a finalized Debug graph.

The engine graph must belong to this checkout. Licensed packs are symlinked
into fresh writable profiles; no user configuration or graphics is exercised.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from run_csqc_entity_native import build_fixture, private_loader_paths

REPO = Path(__file__).resolve().parents[1]


def git(*args):
    return subprocess.check_output(["git", *args], cwd=REPO, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--merge-sha", required=True,
                        help="finalized merge commit, including native API fixes")
    parser.add_argument("--build-dir", type=Path,
                        default=REPO / "build-upstream-acceptance")
    parser.add_argument("--basedir", type=Path, required=True)
    parser.add_argument("--artifact-root", type=Path)
    parser.add_argument("--timeout", type=int, default=90)
    args = parser.parse_args()

    merge = git("rev-parse", args.merge_sha + "^{commit}")
    if len(git("show", "-s", "--format=%P", merge).split()) != 2:
        parser.error("--merge-sha must identify the finalized ordinary merge")
    subprocess.run(["git", "merge-base", "--is-ancestor", merge, "HEAD"],
                   cwd=REPO, check=True)
    head = git("rev-parse", "HEAD")
    if git("diff", "HEAD", "--", "Quake", "Shaders", "meson.build", "meson_options.txt"):
        parser.error("production source/build inputs contain uncommitted changes")
    if (REPO / ".git" / "MERGE_HEAD").exists():
        parser.error("merge is still in progress")

    graph = args.build_dir.resolve(strict=True)
    # Existing final-candidate graphs may live outside the checkout. We only
    # read their recipes/objects; build_fixture writes into our artifact root.
    commands = json.loads((graph / "compile_commands.json").read_text())
    for entry in commands:
        source = (Path(entry["directory"]) / entry["file"]).resolve()
        if source.name == "sv_user.c":
            recipe = entry.get("arguments") or shlex.split(entry["command"])
            if source != REPO / "Quake" / "sv_user.c":
                parser.error("native graph belongs to another source checkout")
            if "clang" not in Path(recipe[0]).name:
                parser.error("acceptance requires the isolated Clang graph")
            break
    else:
        parser.error("native graph has no real sv_user owner")
    basedir = args.basedir.resolve(strict=True)
    if not (basedir / "id1" / "pak0.pak").is_file():
        parser.error("licensed stock pak0.pak is missing")
    root = (args.artifact_root.expanduser().resolve() if args.artifact_root else
            Path(tempfile.mkdtemp(prefix="qsvr-upstream-acceptance-native-")))
    if args.artifact_root:
        root.mkdir(parents=True, exist_ok=False)

    env = os.environ.copy()
    libraries = private_loader_paths(graph)
    if libraries:
        env["LD_LIBRARY_PATH"] = ":".join(libraries +
            ([env["LD_LIBRARY_PATH"]] if env.get("LD_LIBRARY_PATH") else []))
    cases = (
        ("save", "background_save_snapshot_fixture.c", 2, (),
         ("Loop_Init", "NET_CanSendMessage", "NET_SendUnreliableMessage",
          "R_TranslateNewPlayerSkin", "R_ClearParticles", "PScript_ClearParticles",
          "Sys_fopen", "Sys_rename"),
         ("BACKGROUND_SAVE_PURPOSE_COMPLETION_PASSED",
          "BACKGROUND_SAVE_SP_BUSY_RETRY_PASSED",
          "BACKGROUND_SAVE_SP_FAILURE_CATALOGUE_PASSED",
          "BACKGROUND_SAVE_ROUNDTRIP_PASSED version=5",
          "BACKGROUND_SAVE_ROUNDTRIP_PASSED version=7",
          "BACKGROUND_SAVE_SNAPSHOT_PASSED")),
        ("signon", "metadata_publication_native_fixture.c", 1,
         ("-upstream-remote-signon",),
         ("Loop_Init", "NET_CanSendMessage", "NET_SendMessage", "CL_SignonReply",
          "R_TranslateNewPlayerSkin", "R_CheckEfrags", "R_ClearParticles", "R_NewMap",
          "PScript_ClearParticles", "SCR_EndLoadingPlaque", "SZ_Write",
          "MSG_WriteByte", "SV_SendClientMessages"),
         ("UPSTREAM_REMOTE_SIGNON_RETAINED_PASSED",
          "UPSTREAM_REMOTE_SIGNON_BUDGET_RETRY_PASSED",
          "UPSTREAM_REMOTE_SIGNON_NATIVE_PASSED")),
    )
    sources = {"tests/run_upstream_acceptance_native.py", "tests/run_csqc_entity_native.py",
               "tests/native_engine_fixture.h", "tests/local_load_native_fixture.c",
               "tests/mixed_native_fixture.c", "tests/negotiation_native_fixture.c"}
    sources.update("tests/" + case[1] for case in cases)
    receipt = {
        "merge_sha": merge, "head": head,
        "production_tree": git("rev-parse", head + ":Quake"),
        "graph_sha256": hashlib.sha256((graph / "build.ninja").read_bytes()).hexdigest(),
        "source_sha256": {name: hashlib.sha256((REPO / name).read_bytes()).hexdigest()
                          for name in sorted(sources)},
        "assertions": "enabled (build_fixture enforces Debug)", "cases": [],
    }
    receipt_path = root / "receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    for label, source, slots, options, wrappers, markers in cases:
        profile = root / label
        id1 = profile / "id1"
        id1.mkdir(parents=True)
        for name in ("pak0.pak", "pak1.pak"):
            pack = basedir / "id1" / name
            if pack.is_file():
                (id1 / name).symlink_to(pack.resolve())
        binary = root / (label + "-native")
        source_path = REPO / "tests" / source
        if label == "signon":
            # Keep the additional socket wrapper opt-in for legacy fixture
            # callers; this harness includes the actual fixture/engine owners.
            harness = root / "signon-native.c"
            harness.write_text("#define UPSTREAM_ACCEPTANCE_NATIVE_FIXTURE\n" +
                               "#include " + json.dumps(str(source_path)) + "\n")
            source_path = harness
        build_fixture(graph, binary, source_path, wrappers=wrappers)
        command = [str(binary), "-dedicated", str(slots), "-noudp", "-nosound",
                   "-nosteamapi", "-basedir", str(profile), "-userdir", str(profile),
                   *options]
        log = profile / "fixture.log"
        try:
            result = subprocess.run(command, cwd=profile, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=args.timeout)
        except subprocess.TimeoutExpired as error:
            output = error.stdout or b""
            log.write_text(output.decode(errors="replace") if isinstance(output, bytes) else output)
            raise SystemExit(f"FAIL {label}: timeout; log={log}")
        log.write_text(result.stdout)
        receipt["cases"].append({"label": label, "command": command,
                                 "exit": result.returncode, "log": str(log)})
        receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
        if result.returncode or any(marker not in result.stdout for marker in markers):
            raise SystemExit(f"FAIL {label}: exit={result.returncode}; log={log}")
        for line in result.stdout.splitlines():
            if any(line.startswith(marker) for marker in markers):
                print(line)
    print(f"UPSTREAM_ACCEPTANCE_NATIVE_PASSED merge={merge} receipt={receipt_path}")


if __name__ == "__main__":
    main()
