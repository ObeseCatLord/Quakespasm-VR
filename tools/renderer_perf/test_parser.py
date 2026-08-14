#!/usr/bin/env python3
"""Fixture-based self-test for the renderer perf parser."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
if str(ROOT) not in sys.path:
    sys.path.append(str(ROOT))

import parse_renderer_perf


def assert_within(name: str, actual: float, expected: float, tol: float = 0.001) -> None:
    if abs(actual - expected) > tol:
        raise AssertionError(f"{name}: expected {expected}, got {actual}")


def main() -> int:
    fixture = ROOT / "fixtures" / "sample_renderer_perf.txt"
    lines = fixture.read_text(encoding="utf-8").splitlines()
    summary = parse_renderer_perf.analyze_lines(lines)

    assert summary["total_lines"] == 12, summary["total_lines"]
    assert summary["records_parsed"] == 11, summary["records_parsed"]
    assert summary["unsupported_lines"] == 1, summary["unsupported_lines"]

    groups = summary["groups"]
    assert groups["desktop"]["count"] == 2
    assert groups["vr/eye_0"]["count"] == 3
    assert groups["vr/eye_1"]["count"] == 2
    assert groups["vr/mirror"]["count"] == 1
    assert groups["gpu"]["count"] == 3

    desktop = groups["desktop"]["by_tag"]["r_perfdebug"]["metrics"]["total"]
    assert desktop["count"] == 2
    assert_within("desktop total p50", desktop["median"], 18.0)
    assert_within("desktop total p95", desktop["p95"], 23.4)
    assert_within("desktop total max", desktop["max"], 24.0)
    desktop_worldbatch_flushes = groups["desktop"]["by_tag"]["r_perfdebug"]["metrics"][
        "worldbatch.flushes"
    ]
    assert desktop_worldbatch_flushes["count"] == 2
    assert_within("desktop worldbatch flushes", desktop_worldbatch_flushes["median"], 1.0)
    desktop_gpu_dispatch = groups["desktop"]["by_tag"]["r_perfdebug"]["metrics"][
        "gpuworld.dispatch"
    ]
    assert desktop_gpu_dispatch["count"] == 2
    assert_within("desktop gpu dispatch median", desktop_gpu_dispatch["median"], 0.5)
    desktop_gpu_slots = groups["desktop"]["by_tag"]["r_perfdebug"]["metrics"][
        "gpuworld.slots"
    ]
    assert_within("desktop gpu slots median", desktop_gpu_slots["median"], 21.0)

    vr0 = groups["vr/eye_0"]["by_tag"]["r_perfdebug"]["metrics"]["total"]
    assert vr0["count"] == 2
    assert_within("vr eye0 total p50", vr0["median"], 17.0)
    vr0_inst_submit = groups["vr/eye_0"]["by_tag"]["r_perfdebug"]["metrics"]["draw.aliasinstsub"]
    assert vr0_inst_submit["count"] == 1
    assert_within("vr eye0 instanced submits p50", vr0_inst_submit["median"], 8.0)
    vr0_inst_draw = groups["vr/eye_0"]["by_tag"]["r_perfdebug"]["metrics"]["draw.aliasinstdraw"]
    assert vr0_inst_draw["count"] == 1
    assert_within("vr eye0 instanced draws p50", vr0_inst_draw["median"], 2.0)
    vr0_snapshot_build = groups["vr/eye_0"]["by_tag"]["r_perfdebug"]["metrics"]["sharedents.build"]
    assert vr0_snapshot_build["count"] == 1
    assert_within("vr eye0 snapshot builds p50", vr0_snapshot_build["median"], 1.0)
    vr0_worldbatch_mdraw = groups["vr/eye_0"]["by_tag"]["r_perfdebug"]["metrics"]["worldbatch.mdraw"]
    assert vr0_worldbatch_mdraw["count"] == 1
    assert_within("vr eye0 worldbatch mdraw p50", vr0_worldbatch_mdraw["median"], 2.0)
    vr0_gpu_ranges = groups["vr/eye_0"]["by_tag"]["r_perfdebug"]["metrics"][
        "gpuworld.ranges"
    ]
    assert_within("vr eye0 gpu ranges", vr0_gpu_ranges["median"], 5.0)
    vr1_snapshot_reuse = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["sharedents.reuse"]
    assert vr1_snapshot_reuse["count"] == 1
    assert_within("vr eye1 snapshot reuse", vr1_snapshot_reuse["median"], 1.0)
    vr1_worldbatch_draw = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["worldbatch.draw"]
    assert vr1_worldbatch_draw["count"] == 1
    assert_within("vr eye1 worldbatch draw p50", vr1_worldbatch_draw["median"], 1.0)
    vr1_frame_upload = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["frameres.upload_bytes"]
    assert vr1_frame_upload["count"] == 1
    assert_within("vr eye1 frame upload bytes", vr1_frame_upload["median"], 1728.0)
    vr1_frame_high_water = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["frameres.high_water_bytes"]
    assert_within("vr eye1 frame high-water bytes", vr1_frame_high_water["median"], 1728.0)
    vr1_frame_waits = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["frameres.waits"]
    assert_within("vr eye1 frame waits", vr1_frame_waits["median"], 1.0)
    vr1_frame_exhausted = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["frameres.capacity_exhausted_uploads"]
    assert_within("vr eye1 capacity exhausted uploads", vr1_frame_exhausted["median"], 0.0)
    vr1_frame_fallback = groups["vr/eye_1"]["by_tag"]["r_perfdebug"]["metrics"]["frameres.fallback_uploads"]
    assert_within("vr eye1 fallback uploads", vr1_frame_fallback["median"], 0.0)

    eye0_total_cpu = groups["vr/eye_0"]["by_tag"]["r_vr_eyedebug"]["metrics"]["total_cpu"]
    assert eye0_total_cpu["count"] == 1
    assert_within("vr eye0 cpu total", eye0_total_cpu["median"], 10.0)

    mirror = groups["vr/mirror"]["by_tag"]["r_vr_mirrordebug"]["metrics"]["mirror_cpu"]
    assert mirror["count"] == 1
    assert_within("mirror cpu", mirror["median"], 2.2)

    gpudebug = groups["gpu"]["by_tag"]["r_gpudebug"]
    assert gpudebug["count"] == 3
    assert gpudebug["samples"]["world"]["count"] == 2
    assert gpudebug["samples"]["particles"]["count"] == 1
    assert gpudebug["samples"]["world"]["submit_host"] == {1234: 2}
    assert gpudebug["samples"]["world"]["poll_host"] == {1234: 2}
    assert gpudebug["samples"]["particles"]["submit_host"] == {1234: 1}
    assert gpudebug["samples"]["particles"]["poll_host"] == {1234: 1}
    assert_within("gpu sample world median", gpudebug["samples"]["world"]["gpu_ms"]["median"], 3.3)
    assert_within(
        "gpu sample particles median",
        gpudebug["samples"]["particles"]["gpu_ms"]["median"],
        0.6,
    )

    host_correlation = summary["host_correlation"]
    assert host_correlation["submit_host_matches_cpu"] == [1234]
    assert host_correlation["submit_host_without_cpu"] == []
    assert host_correlation["cpu_without_submit_host"] == []

    # ensure absolute paths are redacted in parsed output
    redacted_mj4 = [
        record for record in summary["records"]
        if record["fields"].get("map") == "mj4m1.bsp"
    ]
    assert redacted_mj4, "expected at least one mj4m1.bsp map entry"
    assert summary["records"][0]["fields"]["map"] == "e1m1.bsp"

    print("renderer_perf self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
