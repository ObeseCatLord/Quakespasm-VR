#!/usr/bin/env python3
"""Check a bounded native stereo-alpha reference capture."""

import argparse
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image


class CheckError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def require(ok, message, status="inconclusive"):
    if not ok:
        raise CheckError(status, message)


def finite(value):
    if isinstance(value, dict):
        return all(finite(v) for v in value.values())
    if isinstance(value, list):
        return all(finite(v) for v in value)
    return not isinstance(value, float) or math.isfinite(value)


def load(path):
    try:
        with path.open(encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, ValueError) as exc:
        raise CheckError("inconclusive", f"cannot read {path.name}: {exc}") from exc


def matrix(values):
    a = np.asarray(values, dtype=float)
    require(a.size == 16 and np.isfinite(a).all(), "invalid clip matrix")
    return a.reshape(4, 4, order="F")


def clip_polygon(points, transform):
    p = (transform @ np.column_stack((np.asarray(points, float),
                                      np.ones(len(points)))).T).T
    for axis, sign, bias in ((0, 1, 1), (0, -1, 1), (1, 1, 1),
                             (1, -1, 1), (2, 1, 0), (2, -1, 1)):
        if len(p) == 0:
            break
        out, s = [], p[-1]
        ds = sign * s[axis] + bias * s[3]
        for t in p:
            dt = sign * t[axis] + bias * t[3]
            if (dt >= 0) != (ds >= 0):
                out.append(s + (t - s) * (ds / (ds - dt)))
            if dt >= 0:
                out.append(t)
            s, ds = t, dt
        p = np.asarray(out, float).reshape((-1, 4))
    if len(p) < 3 or np.any(p[:, 3] <= 0):
        return np.empty((0, 2))
    return (p[:, :2] / p[:, 3, None] + 1) * (160, 120)


def inside(poly, bx, by):
    if len(poly) < 3 or bx < poly[:, 0].min() or bx + 2 > poly[:, 0].max() or \
            by < poly[:, 1].min() or by + 2 > poly[:, 1].max():
        return False
    area = np.sum(poly[:, 0] * np.roll(poly[:, 1], -1) -
                  poly[:, 1] * np.roll(poly[:, 0], -1))
    if abs(area) < 1e-8:
        return False
    orient = 1 if area > 0 else -1
    corners = ((bx, by), (bx + 2, by), (bx, by + 2), (bx + 2, by + 2))
    for a, b in zip(poly, np.roll(poly, -1, axis=0)):
        edge = b - a
        length = np.linalg.norm(edge)
        if length < 1e-8:
            continue
        if min(orient * (edge[0] * (y - a[1]) - edge[1] * (x - a[0])) /
               length for x, y in corners) < .25:
            return False
    return True


def geometry_rois(root, assets, samples):
    g = load(root / "geometry.json")
    require(g.get("schema") == 1 and g.get("world") == "maps/e1m1.bsp",
            "geometry schema/world mismatch")
    expected = {
        "progs/vr_alpha_wet.mdl": (828, 850, -297),
        "progs/vr_alpha_dry.mdl": (844, 850, -295),
    }
    records = {e.get("name"): e for e in g.get("entities", [])}
    require(set(records) == set(expected), "geometry alias names mismatch")
    manifest = assets.get("assets", {})
    require(set(expected) <= set(manifest), "asset manifest aliases missing")
    center = matrix(samples[0]["matrices"]["center_clip"])
    eye = matrix(samples[0]["matrices"]["eye_clip"][samples[0]["eye"]])
    transform = eye @ center
    models = {}
    for name, origin in expected.items():
        ent, meta = records[name], manifest[name]["geometry"]
        require(finite(ent), f"nonfinite geometry entity {name}")
        require(ent.get("origin") == list(origin) and
                ent.get("angles") == [0, 0, 0], f"native transform mismatch {name}")
        require(ent.get("scale") == meta.get("scale") and
                ent.get("scale_origin") == meta.get("scale_origin"),
                f"asset scale mismatch {name}")
        verts = np.asarray(meta.get("vertices_byte"), float)
        require(verts.shape == (4, 3) and np.isfinite(verts).all(),
                f"invalid manifest vertices {name}")
        world = verts * np.asarray(ent["scale"]) + np.asarray(ent["scale_origin"]) + origin
        models[name.rsplit("_", 1)[-1].split(".")[0]] = clip_polygon(world, transform)
    waters = []
    for surf in g.get("water_surfs", []):
        require(finite(surf), "nonfinite water geometry")
        if surf.get("plane_normal") == [0, 0, 1] and surf.get("plane_dist") == -296:
            for polygon in surf.get("polygons", []):
                p = clip_polygon(polygon, transform)
                if len(p) >= 3:
                    waters.append(p)
    require(waters, "no projectable native water polygons")
    xmap, ymap = {}, {}
    for x in range(640):
        xmap.setdefault(math.floor((x + .5) * .5 - .5), []).append(x)
    for y in range(480):
        ymap.setdefault(math.floor((y + .5) * .5 - .5), []).append(y)
    rois = {}
    for category, poly in models.items():
        if len(poly) < 3:
            rois[category] = []
            continue
        bx0, bx1 = math.floor(poly[:, 0].min()) - 1, math.ceil(poly[:, 0].max())
        by0, by1 = math.floor(poly[:, 1].min()) - 1, math.ceil(poly[:, 1].max())
        found = []
        for by in range(max(by0, min(ymap)), min(by1, max(ymap)) + 1):
            if by not in ymap:
                continue
            for bx in range(max(bx0, min(xmap)), min(bx1, max(xmap)) + 1):
                if bx not in xmap or not inside(poly, bx, by):
                    continue
                if any(inside(water, bx, by) for water in waters):
                    found.append((bx, by, xmap[bx], ymap[by]))
        rois[category] = found
    return rois


def influence(b, e, w, c, roi):
    selected, pixels = set(), []
    for bx, by, xs, ys in roi:
        for y in ys:
            for x in xs:
                if np.any(e[y, x] != b[y, x]) and np.any(w[y, x] != b[y, x]):
                    selected.add((bx, by))
                    pixels.append((bx, by, x, y))
    require(len(selected) >= 16, "fewer than 16 same-pixel B/E/W footprints")
    distinguished = {(bx, by) for bx, by, x, y in pixels
                     if np.any(c[y, x] != b[y, x]) and
                     np.any(c[y, x] != e[y, x]) and np.any(c[y, x] != w[y, x])}
    require(len(distinguished) >= 16, "fewer than 16 distinguished footprints")
    return len(selected), len(distinguished)


def diff(a, b):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16))
    return {"numdiff": int(np.any(d != 0, axis=2).sum()), "maxdiff": int(d.max())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", required=True, type=Path, help="native capture directory")
    parser.add_argument("--assets", required=True, type=Path, help="prepare_stereo_alpha_native.py assets.json")
    args = parser.parse_args()
    out = {"status": "inconclusive", "group": None, "eye": None,
           "equality": {"repeats": {}, "exceptional_common": {}},
           "missinglayerguards": {}, "categories": {}}
    try:
        root = args.captures
        layers, assets = load(root / "layers.json"), load(args.assets)
        phases = layers.get("samples", [])
        require(layers.get("status") == "passed" and len(phases) == 16,
                "capture must contain 16 passed phases")
        group, target = phases[0].get("group"), phases[0].get("eye")
        require(group in (0, 1) and target in (0, 1), "invalid group/eye")
        out.update(group=group, eye=target)
        stable = ("cl_time", "target_fov", "target_pose", "original_head_matrix",
                  "transfer", "formats")
        base, images = phases[0], {}
        for phase, sample in enumerate(phases):
            mode, layer, repeat = phase // 8, "BEWC"[(phase // 2) % 4], phase % 2
            require((sample.get("phase"), sample.get("mode"), sample.get("layer"),
                     sample.get("repeat"), sample.get("group"), sample.get("eye")) ==
                    (phase, mode, layer, repeat, group, target), f"phase {phase} order/target mismatch")
            expected_mask = (1 if group == 0 else 2) if mode == 0 else (3 if group == target else 0)
            require(sample.get("wet_mask") == expected_mask and
                    sample.get("exceptional") == (mode == 0),
                    f"phase {phase} native mask/exceptional mismatch")
            require(sample.get("notifications_expired") is True and
                    sample.get("notify_settled_frames", 0) >= 8,
                    f"phase {phase} notifications unsettled")
            for key in stable:
                require(finite(sample.get(key)) and sample.get(key) == base.get(key),
                        f"unstable {key}")
            mats = sample.get("matrices", {})
            require(mats.get("center_clip") == base["matrices"].get("center_clip") and
                    mats.get("eye_clip", [])[target] == base["matrices"]["eye_clip"][target],
                    "unstable target center/eye clip matrix")
            require(sample["transfer"].get("native_extent") == [320, 240] and
                    sample["transfer"].get("mirror_extent") == [640, 480],
                    "native/mirror extent mismatch")
            receipts = sample.get("presentations", [])
            require(len(receipts) >= 3 and len({r.get("snapshot_id") for r in receipts}) >= 3,
                    f"phase {phase} lacks three distinct presentation receipts")
            for rec in receipts:
                require(all(rec.get(k) == sample.get(k) for k in
                            ("phase", "mode", "group", "eye", "layer", "repeat")) and
                        isinstance(rec.get("image_index"), int),
                        f"phase {phase} has a nonmatching presentation receipt")
            calls = sample.get("consumer_calls", [])
            enabled = layer in "EC"
            require(len(calls) == (2 if mode else 4) if enabled else not calls,
                    f"phase {phase} consumer count mismatch")
            if enabled:
                active = set()
                wet_target = group == target
                for call in calls:
                    entity, excluded, stage = call.get("entity"), call.get("excluded"), call.get("stage")
                    u = call.get("uniform")
                    require(entity in ("wet", "dry") and call.get("mode") == mode and
                            call.get("count", 0) > 0 and isinstance(u, list) and len(u) == 40 and
                            finite(u), f"phase {phase} invalid consumer record")
                    flags = (u[35], u[39])
                    expected_flags = (0, 0) if mode else None
                    require((flags == expected_flags if mode else flags in ((0, 1), (1, 0))) and
                            excluded == flags[target],
                            f"phase {phase} consumer exclusion flags mismatch")
                    require(u[target * 16:(target + 1) * 16] ==
                            sample["matrices"]["eye_clip"][target],
                            f"phase {phase} consumer target matrix mismatch")
                    legacy_stage = {(True, "dry"): 0, (True, "wet"): 1,
                                    (False, "wet"): 0, (False, "dry"): 1}
                    if not excluded:
                        require(stage == legacy_stage[(wet_target, entity)],
                                f"phase {phase} legacy water stage mismatch")
                    if not excluded:
                        active.add(entity)
                require(active == {"wet", "dry"}, f"phase {phase} lacks active wet/dry consumers")
            name = f"mode{mode}-group{group}-eye{target}-{layer}-repeat{repeat}.png"
            require(sample.get("image") == name, f"phase {phase} image name mismatch")
            try:
                with Image.open(root / name) as image:
                    rgb = image.convert("RGB")
                    require(rgb.size == (640, 480), f"phase {phase} image extent mismatch")
                    images[phase] = np.asarray(rgb).copy()
            except OSError as exc:
                raise CheckError("inconclusive", f"cannot decode {name}: {exc}") from exc

        for mode in range(2):
            for li, layer in enumerate("BEWC"):
                a, b = images[mode * 8 + li * 2], images[mode * 8 + li * 2 + 1]
                stats = diff(a, b)
                out["equality"]["repeats"][f"{mode}:{layer}"] = {"equal": stats["numdiff"] == 0, **stats}
                require(stats["numdiff"] == 0, f"same-state repeat differs {mode}:{layer}")

        rois = geometry_rois(root, assets, phases)
        common = {layer: images[8 + i * 2] for i, layer in enumerate("BEWC")}
        for category, roi in rois.items():
            selected, distinguished = influence(common["B"], common["E"], common["W"],
                                                common["C"], roi)
            out["categories"][category] = {"selected_footprints": selected,
                                            "distinguished_footprints": distinguished}
            guards = {}
            for slot in "EWB":
                try:
                    influence(common["B"], common["E"], common["W"], common[slot], roi)
                    guards[slot] = False
                except CheckError:
                    guards[slot] = True
            require(all(guards.values()), f"{category} missing-layer substitution escaped local guard")
            out["missinglayerguards"][category] = guards

        for li, layer in enumerate("BEWC"):
            a, b = images[li * 2], images[8 + li * 2]
            stats = diff(a, b)
            out["equality"]["exceptional_common"][layer] = {"equal": stats["numdiff"] == 0, **stats}
            require(stats["numdiff"] == 0, f"qualified exceptional/common mismatch {layer}",
                    "equivalencefailed")
        out["status"] = "passed"
    except CheckError as exc:
        out["status"], out["error"] = exc.status, str(exc)
    except Exception as exc:
        out["status"], out["error"] = "inconclusive", f"invalid capture: {exc}"
    print(json.dumps(out, separators=(",", ":"), sort_keys=True))
    return 0 if out["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
