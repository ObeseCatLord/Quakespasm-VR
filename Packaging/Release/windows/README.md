# Native Windows adapter

Run from the repository, with a prepared immutable cohort and a private local JSON config:

```sh
python3 Packaging/Release/windows/build.py --root "/path/cohort" --config "/path/local.json"
python3 Packaging/Release/windows/build.py --root "/path/cohort" --config "/path/local.json" --verify-only
```

The config's stable Windows keys are:

```json
{
  "windows": {
    "baseline_release": "/path/qualified-cohort/windows/release",
    "winboat_tools": "~/.codex/skills/winboat-ssh/scripts"
  }
}
```

`baseline_release` is required. `winboat_tools` is optional with the displayed
default. Relative paths resolve against the config file's directory. The baseline
supplies the accepted dependency hashes, external SDK/compiler locations and
hashes, configuration/import/link policy, and qualification receipts. It supplies
no engine output to a fresh build. No SDK installation is attempted.

The cohort inputs are `entry.json` (`production_revision`, `archive_sha256`,
`archive_bytes`), `archives/product.tar`, and
`production-source-manifest.json` (`revision`, `archive_sha256`, `files` entries
with `path` and `sha256`; file sizes and `regular_file_count` are optional).
The host hashes every archived source file before transfer. The guest repeats
that validation, builds in a unique per-revision/hash workspace, then independently
checks original sources, dependencies, compiled objects, generated shaders,
tool hashes, runtime inventory, and actual optimized compiler/linker commands.
The Release compiler policy and feature defines remain the proven MSBuild recipe.

Object and shader inventories come from the current archive's project items,
Release exclusions, inherited custom-build outputs, and helper projects. Adding C
files or shader variants to those existing item groups does not require replacing
the dependency baseline. Changes to configuration, imports, project topology,
link policy (including `LinkIncremental`), unknown item conditions, or custom object
filenames fail explicitly for compatibility review. This reader does not implement
a general MSBuild evaluator.

Outputs are `windows/release/Release/` (engine, full pinned DLL inventory and
licenses), `windows/release/windows-artifact-manifest.json` (publisher format:
`revision`, `archive_sha256`, `files` with `file` and `sha256`), and durable
`windows/{control,release/receipts}/` evidence. Symbols remain in the guest and
their hashes in receipts; they are excluded from the runtime package.

A completed cohort is rehashed and skipped even without a working transport.
`--verify-only` never contacts Windows. Importing a qualified cohort must copy the
entire `windows/release/` tree, including its `Release/`, publisher manifest,
`entry.json`, `product.tar`, source manifest, dependency/shader JSON inputs and all
`receipts/` files (especially the transfer manifest, native status, independent
verification, inventories, build log and binlog). Keep the original source identity.
The qualified baseline directory must remain available through local config.

A pre-existing invalid or partial `windows/` namespace is retained and refused.
The adapter records its guest workspace before build launch and attempts to fetch
failure receipts. It does not automatically reconcile a transport disconnect with
a remotely completed build. After inspecting the retained guest/control evidence,
retry explicitly with a newly prepared cohort root. There is no implicit cleanup,
overwrite or second build in an existing namespace. Transport uses only the
WinBoat skill's private `winboat-powershell` and `winboat-scp` wrappers, with host
argument arrays and PowerShell string literals; no credentials are copied.

Offline checks against an existing qualified cohort:

```sh
python3 -B Packaging/Release/windows/build.py --help
python3 -B Packaging/Release/windows/test_offline.py --cohort "/path/qualified-cohort" -v
```

These rehash the proven cohort without changing it, check reuse without transport,
reject altered source identity, dependencies, artifacts, receipts and partial
namespaces, and derive evolving C/shader inventories from the actual project XML.
They do not establish that a new engine revision has built successfully; the
retained qualification receipts are the native-build reference.
