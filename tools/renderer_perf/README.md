# renderer_perf

Offline parser for Quakespasm-OpenVR renderer debug logs.

The parser reads `DebugLog()` output from `r_perfdebug`,
`r_vr_eyedebug`, `r_vr_mirrordebug`, and `r_gpudebug` lines and reports grouped
desktop/VR/eye summaries.

For `r_gpudebug`, the parser expects:

- `submit_host`
- `poll_host`
- `sample`
- `gpu_ms`

## Usage

```sh
python3 tools/renderer_perf/parse_renderer_perf.py path/to/condebug.log \
  --meta-commit <git-short-sha> \
  --meta-build <build-id> \
  --meta-gpu "RTX 3080" \
  --meta-runtime "OpenVR 1.2.3" \
  --meta-headset "Index" \
  --meta-map "maps/mj4m1.bsp" \
  --meta-settings "vr_msaa=4 r_perfdebug_min_ms=8" \
  --format text
```

Output formats:

- `text` prints compact per-group stats directly to stdout.
- `json` emits machine-readable summaries.
- `both` (default) prints text and JSON.

To read from stdin:

```sh
cat run.log | python3 tools/renderer_perf/parse_renderer_perf.py - --format json
```

## What is parsed

Only the known debug lines above are considered.
Unknown/unsupported formats matching those tags are reported with line numbers.
The parser intentionally does not parse every possible engine log line.

Metrics are summarized with:

- `count`
- `median` (`p50`)
- `p95`
- `p99`
- `max`

For `r_gpudebug` specifically, samples are grouped by `sample` and summarized for
the `gpu_ms` metric. The output also includes per-sample `submit_host` and `poll_host`
counts, and a `host_correlation` block summarizing whether `submit_host` IDs match CPU
`r_perfdebug` `host` IDs.

## Metadata flags

These metadata flags are included in JSON output and used to attach run context:

- `--meta-commit`
- `--meta-build`
- `--meta-gpu`
- `--meta-runtime`
- `--meta-headset`
- `--meta-map`
- `--meta-settings`

Absolute paths in outputs are redacted to avoid logging filesystem locations.

## Self-test

```sh
python3 tools/renderer_perf/test_parser.py
```
