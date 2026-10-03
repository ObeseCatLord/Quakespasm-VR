#!/usr/bin/env python3
"""Check native either-eye model bounds and captured output; no GPU calls."""
import argparse
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import numpy as np
from PIL import Image
from check_stereo_alpha_reference import CheckError, require, load, matrix, clip_polygon, inside, diff


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    args = parser.parse_args()
    out = {
        "status": "inconclusive",
        "acceptance": "unqualified",
        "cells": [],
        "control_summary": {
            "status": "unavailable",
            "full_acceptance": False,
            "reason": "cross-controls require a 32-sample capture",
            "full_image_comparisons": 0,
            "pairs": [],
        },
        "footprint_note": (
            "native_footprints counts intended-color pixel witnesses only inside independently "
            "projected model cells; zero for a projected-empty cell is not a pixel-absence claim"
        ),
    }
    try:
        data = load(args.captures / "culling.json")
        samples = data.get("samples", [])
        require(data.get("status") == "passed" and len(samples) in (8, 32), "incomplete native capture")
        if len(samples) == 8:
            out["acceptance"] = "bounded_subset"
            out["control_summary"]["reason"] = "8-sample exploratory capture has no cross-group controls"
        else:
            out["acceptance"] = "full_acceptance"
        assets = load(args.assets)["assets"]
        images = {}
        first_time = samples[0]["cl_time"]
        for phase, sample in enumerate(samples):
            group, eye, repeat = phase // 8, (phase // 4) % 2, phase % 2
            layer = "E" if phase % 4 >= 2 else "B"
            require((sample["phase"], sample["group"], sample["eye"], sample["repeat"], sample["layer"]) ==
                    (phase, group, eye, repeat, layer), "phase mapping mismatch")
            require(sample["cl_time"] == first_time and sample["notify_settled_frames"] >= 8, "unstable time/notifications")
            require(sample["msaa"] == 4 and sample["ssao"] == 1 and sample["oit"] == 1, "quality changed")
            require(sample["native_extent"] == [320, 240] and sample["mirror_extent"] == [640, 480], "extent changed")
            receipts = sample["presentations"]
            require(len({r["snapshot_id"] for r in receipts}) >= 3 and
                    all(all(r[k] == sample[k] for k in ("phase", "group", "eye", "layer", "repeat")) for r in receipts), "missing matching native presents")
            require(len(sample["identities"]) == 2, "model identities missing")
            for ent in sample["identities"]:
                lo, hi = np.array(ent["model_mins"]) + ent["origin"], np.array(ent["model_maxs"]) + ent["origin"]
                verts = np.array([[lo[0] if i & 1 else hi[0], lo[1] if i & 2 else hi[1], lo[2] if i & 4 else hi[2], 1] for i in range(8)])
                outside = []
                for view in range(2):
                    upper = view == group if group < 2 else group == 2
                    require(sample["fovs"][view] == dict(left=-1, right=1, up=1 if upper else 0, down=0 if upper else -1), "view input changed")
                    q = (matrix(sample["matrices"]["eye_clip"][view]) @ matrix(sample["matrices"]["center_clip"]) @ verts.T).T
                    d = np.column_stack((q[:, 3] + q[:, 0], q[:, 3] - q[:, 0], q[:, 3] + q[:, 1], q[:, 3] - q[:, 1], q[:, 2], q[:, 3] - q[:, 2]))
                    outside.append(bool(np.any(np.all(d < 0, axis=0))))
                    require(outside[-1] == (upper != (ent["name"] == "wet")), "independent monocular box premise failed")
                require(bool(ent["native_culled"]) == all(outside), "native union box result differs")
                if layer == "E":
                    require(ent["in_path_cull_calls"] > 0 and ent["alias_calls"] > 0,
                            "enabled identity lacks in-path cull/alias observations")
            with Image.open(args.captures / sample["image"]) as image:
                rgb = image.convert("RGB")
                require(rgb.size == (640, 480), "capture size changed")
                images[phase] = np.asarray(rgb).copy()
            if repeat:
                require(diff(images[phase - 1], images[phase])["numdiff"] == 0, "unstable same-state image repeat")
        if len(samples) == 32:
            phase_by_key = {
                (sample["group"], sample["eye"], sample["layer"], sample["repeat"]): phase
                for phase, sample in enumerate(samples)
            }
            control_pairs = (((0, 0), (2, 0)), ((0, 1), (3, 1)),
                             ((1, 0), (3, 0)), ((1, 1), (2, 1)))
            control_results = []
            comparison_count = 0
            for (left_group, left_eye), (right_group, right_eye) in control_pairs:
                switched_models = set()
                for layer in ("B", "E"):
                    for repeat in (0, 1):
                        left_phase = phase_by_key[(left_group, left_eye, layer, repeat)]
                        right_phase = phase_by_key[(right_group, right_eye, layer, repeat)]
                        left_sample, right_sample = samples[left_phase], samples[right_phase]
                        require(left_sample["fovs"][left_eye] == right_sample["fovs"][right_eye],
                                "cross-control target FOV changed")
                        require(left_sample["matrices"]["eye_clip"][left_eye] ==
                                right_sample["matrices"]["eye_clip"][right_eye],
                                "cross-control target eye clip changed")
                        require(left_sample["matrices"]["center_clip"] ==
                                right_sample["matrices"]["center_clip"],
                                "cross-control center clip changed")
                        require(all(left_sample[key] == right_sample[key]
                                    for key in ("head_valid", "head_tracked")),
                                "cross-control recorded head state changed")
                        require(diff(images[left_phase], images[right_phase])["numdiff"] == 0,
                                "cross-control full images differ")
                        comparison_count += 1
                        if layer == "E":
                            left_culled = {ent["name"]: bool(ent["native_culled"])
                                           for ent in left_sample["identities"]}
                            right_culled = {ent["name"]: bool(ent["native_culled"])
                                            for ent in right_sample["identities"]}
                            require(set(left_culled) == set(right_culled) == {"wet", "dry"},
                                    "cross-control model identities changed")
                            changed = [name for name in left_culled
                                       if left_culled[name] != right_culled[name]]
                            require(len(changed) == 1 and
                                    {left_culled[changed[0]], right_culled[changed[0]]} == {False, True},
                                    "cross-control lacks an enabled model accepted on one side and rejected on the other")
                            switched_models.add(changed[0])
                require(len(switched_models) == 1,
                        "cross-control did not preserve one opposite model acceptance transition")
                control_results.append({
                    "left": {"group": left_group, "eye": left_eye},
                    "right": {"group": right_group, "eye": right_eye},
                    "equal_full_images": 4,
                    "opposite_native_cull_models": sorted(switched_models),
                })
            require(comparison_count == 16, "cross-control comparison count changed")
            out["control_summary"] = {
                "status": "passed",
                "full_acceptance": True,
                "full_image_comparisons": comparison_count,
                "pairs": control_results,
            }
        for phase in range(2, len(samples), 4):
            sample = samples[phase]
            background, enabled = images[phase - 2], images[phase]
            changed = np.any(background != enabled, axis=2)
            transform = matrix(sample["matrices"]["eye_clip"][sample["eye"]]) @ matrix(sample["matrices"]["center_clip"])
            for ent in sample["identities"]:
                name = "progs/vr_alpha_" + ent["name"] + ".mdl"
                meta = assets[name]["geometry"]
                vertices = np.asarray(meta["vertices_byte"]) * meta["scale"] + meta["scale_origin"] + np.asarray(ent["origin"])
                polygon = clip_polygon(vertices, transform)
                upper = sample["eye"] == sample["group"] if sample["group"] < 2 else sample["group"] == 2
                visible = upper == (ent["name"] == "wet")
                require((len(polygon) >= 3) == visible, "projected model visibility premise failed")
                witnesses = set()
                if visible:
                    xmin, xmax = max(-1, int(np.floor(polygon[:, 0].min()))), min(319, int(np.ceil(polygon[:, 0].max())))
                    ymin, ymax = max(-1, int(np.floor(polygon[:, 1].min()))), min(239, int(np.ceil(polygon[:, 1].max())))
                    for by in range(ymin, ymax + 1):
                        for bx in range(xmin, xmax + 1):
                            if not inside(polygon, bx, by):
                                continue
                            for y in (2 * by + 1, 2 * by + 2):
                                for x in (2 * bx + 1, 2 * bx + 2):
                                    if not (0 <= x < 640 and 0 <= y < 480) or not changed[y, x]:
                                        continue
                                    color = enabled[y, x].astype(int)
                                    intended = color[0] > color[1] and color[0] > color[2] if ent["name"] == "wet" else color[2] > color[0] and color[2] > color[1]
                                    if intended:
                                        witnesses.add((bx, by))
                    require(len(witnesses) >= 16, "missing intended model output in eligible eye")
                out["cells"].append(dict(group=sample["group"], eye=sample["eye"], model=ent["name"], visible=visible,
                                         native_culled=bool(ent["native_culled"]), native_footprints=len(witnesses),
                                         footprint_scope=("pixel_witnesses_in_projected_cells" if visible else "projected_empty")))
        out["status"] = "passed"
        out["phases"] = len(samples)
    except (CheckError, KeyError, ValueError, OSError) as error:
        out["error"] = str(error)
    print(json.dumps(out, separators=(",", ":"), sort_keys=True))
    return 0 if out["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
