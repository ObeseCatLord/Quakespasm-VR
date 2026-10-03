#!/usr/bin/env python3
"""Check native protected alias output against actual KHR rate-map captures."""

import argparse
import hashlib
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image

import check_stereo_alpha_reference as alpha_check


CheckError = alpha_check.CheckError
require = alpha_check.require
finite = alpha_check.finite
load = alpha_check.load
matrix = alpha_check.matrix
clip_polygon = alpha_check.clip_polygon

NATIVE_EXTENT = (320, 240)
MIRROR_EXTENT = (640, 480)
RATE_2X2 = 5
RATE_4X4 = 10
FSR_DYNAMIC_STATE = 1000226000


def image(path):
    try:
        with Image.open(path) as decoded:
            rgb = decoded.convert("RGB")
            require(rgb.size == MIRROR_EXTENT, f"{path.name} is not a 640x480 mirror capture")
            return np.asarray(rgb).copy()
    except OSError as exc:
        raise CheckError("inconclusive", f"cannot decode {path.name}: {exc}") from exc


def validate_assets(path):
    manifest = load(path)
    expected = {"progs/vr_alpha_wet.mdl": 250,
                "progs/vr_alpha_dry.mdl": 244}
    require(manifest.get("format") == "Quake alias model v6" and
            set(expected) == set(manifest.get("assets", {})),
            "patterned asset manifest mismatch")
    for name, palette_index in expected.items():
        record = manifest["assets"][name]
        data_path = path.parent / name
        try:
            data = data_path.read_bytes()
        except OSError as exc:
            raise CheckError("inconclusive", f"cannot read private asset {name}: {exc}") from exc
        require(len(data) == 308 and hashlib.sha256(data).hexdigest() == record.get("sha256"),
                f"private model size/hash mismatch: {name}")
        skin = data[88:152]
        counts = {str(i): skin.count(bytes((i,))) for i in (palette_index, 254, 255)}
        require(skin[0] == 255 and counts == {str(palette_index): 31,
                                               "254": 32, "255": 1},
                f"private model skin/guard mismatch: {name}")
        pattern = record.get("color", {}).get("skin_pattern", {})
        require(pattern.get("even_parity_palette_index") == palette_index and
                pattern.get("odd_parity_palette_index") == 254 and
                pattern.get("counts_by_palette_index") == counts,
                f"private model pattern manifest mismatch: {name}")
    return manifest


def validate_eye_capture(root, expected_eye):
    capture = load(root / "layers.json")
    phases = capture.get("samples", [])
    require(capture.get("schema") == 1 and capture.get("status") == "passed" and
            capture.get("eye") == expected_eye and len(phases) == 8,
            f"eye {expected_eye} capture is incomplete")
    samples, images = [], {}
    stable_keys = ("matrices", "runtime_source_head_matrix", "source_view_matrices",
                   "source_submitted_poses", "controlled_head", "controlled_view_matrices",
                   "controlled_submitted_poses", "source_fovs", "runtime_head_flags",
                   "runtime_gaze_flags", "formats")
    base = phases[0]
    for phase, sample in enumerate(phases):
        mode = phase // 4
        layer = ("B", "E")[(phase // 2) % 2]
        repeat = phase % 2
        require((sample.get("phase"), sample.get("mode"), sample.get("eye"),
                 sample.get("layer"), sample.get("repeat")) ==
                (phase, mode, expected_eye, layer, repeat),
                f"eye {expected_eye} phase order mismatch at {phase}")
        require(sample.get("runtime_views_accepted") is True and
                sample.get("runtime_head_flags") == {"valid": 1, "tracked": 1},
                f"eye {expected_eye} lacks valid tracked runtime head/views")
        source_matrices = np.asarray(sample.get("source_view_matrices"), dtype=float)
        source_poses = np.asarray(sample.get("source_submitted_poses"), dtype=float)
        controlled_matrices = np.asarray(sample.get("controlled_view_matrices"), dtype=float)
        controlled_poses = np.asarray(sample.get("controlled_submitted_poses"), dtype=float)
        require(source_matrices.shape == controlled_matrices.shape == (2, 12) and
                source_poses.shape == controlled_poses.shape == (2, 7) and
                np.isfinite(source_matrices).all() and np.isfinite(source_poses).all() and
                np.isfinite(controlled_matrices).all() and np.isfinite(controlled_poses).all(),
                f"eye {expected_eye} source/controlled eye poses malformed")
        source_separation = np.linalg.norm(source_matrices[0, [3, 7, 11]] -
                                           source_matrices[1, [3, 7, 11]])
        source_pose_separation = np.linalg.norm(source_poses[0, 4:7] - source_poses[1, 4:7])
        controlled_separation = np.linalg.norm(controlled_matrices[0, [3, 7, 11]] -
                                               controlled_matrices[1, [3, 7, 11]])
        controlled_pose_separation = np.linalg.norm(controlled_poses[0, 4:7] -
                                                    controlled_poses[1, 4:7])
        require(source_separation > 0.01 and source_pose_separation > 0.01 and
                controlled_separation > 0.05 and controlled_pose_separation > 0.05,
                f"eye {expected_eye} source poses were co-located")
        require(sample.get("paused") is True and sample.get("r_oit") == 1 and
                sample.get("r_ssao") == 1 and sample.get("sample_count") == 4 and
                sample.get("vr_foveation") == mode,
                f"eye {expected_eye} phase {phase} render controls mismatch")
        require(sample.get("notifications_expired") is True and
                sample.get("notify_settled_frames", 0) >= 8,
                f"eye {expected_eye} phase {phase} notifications unsettled")
        for key in stable_keys:
            require(finite(sample.get(key)) and sample.get(key) == base.get(key),
                    f"eye {expected_eye} phase {phase} unstable {key}")
        transfer = sample.get("transfer", {})
        require(transfer.get("native_extent") == list(NATIVE_EXTENT) and
                transfer.get("render_extent") == list(NATIVE_EXTENT) and
                transfer.get("mirror_extent") == list(MIRROR_EXTENT),
                f"eye {expected_eye} phase {phase} extent mismatch")
        require(transfer.get("gamma") == 1 and transfer.get("contrast") == 1 and
                transfer.get("palette") == 0 and transfer.get("waterwarp") == 0 and
                transfer.get("polyblend") == 0 and transfer.get("forced_console") == 0,
                f"eye {expected_eye} phase {phase} transfer/UI changed")
        receipts = sample.get("presentations", [])
        require(len(receipts) >= 3 and len({r.get("snapshot_id") for r in receipts}) >= 3,
                f"eye {expected_eye} phase {phase} lacks successful snapshot/present receipts")
        for receipt in receipts:
            require(all(receipt.get(k) == sample.get(k) for k in
                        ("phase", "mode", "eye", "layer", "repeat")) and
                    isinstance(receipt.get("image_index"), int),
                    f"eye {expected_eye} phase {phase} receipt mismatch")
        name = sample.get("image")
        expected_name = f"eye{expected_eye}-{'off' if mode == 0 else 'fixed'}-{layer}-repeat{repeat}.png"
        require(name == expected_name, f"eye {expected_eye} phase {phase} PNG name mismatch")
        images[phase] = image(root / name)

        if mode == 0:
            rate_map = sample.get("rate_map", {})
            require(rate_map.get("active") is False and
                    rate_map.get("image_present") is False and
                    rate_map.get("generated_hex") == "" and
                    rate_map.get("uploaded_hex") == "",
                    f"eye {expected_eye} OFF phase has an active KHR attachment")
        else:
            validate_rate_map(sample, expected_eye)
        samples.append(sample)

    require(all(sample["cl_time"] == base["cl_time"] for sample in samples),
            f"eye {expected_eye} paused simulation time changed")
    for mode in range(2):
        for li, layer in enumerate("BE"):
            a, b = images[mode * 4 + li * 2], images[mode * 4 + li * 2 + 1]
            stats = alpha_check.diff(a, b)
            require(stats["numdiff"] == 0,
                    f"eye {expected_eye} same-state repeat differs {mode}:{layer}")
    return samples, images


def validate_rate_map(sample, expected_eye):
    rate = sample.get("rate_map", {})
    require(rate.get("available") == 1 and rate.get("active") is True and
            rate.get("image_present") is True and rate.get("format") == "VK_FORMAT_R8_UINT" and
            rate.get("image_initialized") is True and rate.get("upload_equal") is True,
            f"eye {expected_eye} FIXED phase lacks active uploaded KHR map")
    try:
        generated = bytes.fromhex(rate.get("generated_hex", ""))
        uploaded = bytes.fromhex(rate.get("uploaded_hex", ""))
    except ValueError as exc:
        raise CheckError("inconclusive", f"eye {expected_eye} malformed rate-map bytes") from exc
    require(bool(generated) and generated == uploaded and
            hashlib.sha256(generated).hexdigest() == rate.get("generated_sha256") == rate.get("uploaded_sha256"),
            f"eye {expected_eye} generated/uploaded rate-map bytes differ")
    rw, rh = rate.get("map_extent", [0, 0])
    tw, th = rate.get("texel_size", [0, 0])
    width, height = rate.get("render_extent", [0, 0])
    layers = rate.get("layers", 0)
    require(width == NATIVE_EXTENT[0] and height == NATIVE_EXTENT[1] and
            tw > 0 and th > 0 and rw == math.ceil(width / tw) and
            rh == math.ceil(height / th) and layers in (1, 2) and
            len(generated) == rw * rh * layers and
            set(generated) <= {0, RATE_2X2, RATE_4X4},
            f"eye {expected_eye} rate-map extent/layers/encoding invalid")
    selected_layer = expected_eye if layers == 2 else 0
    require(rate.get("selected_layer") == selected_layer,
            f"eye {expected_eye} map layer selection mismatch")
    return generated


def inside_block(poly, x, y, size):
    if len(poly) < 3 or x < poly[:, 0].min() or x + size > poly[:, 0].max() or \
            y < poly[:, 1].min() or y + size > poly[:, 1].max():
        return False
    area = np.sum(poly[:, 0] * np.roll(poly[:, 1], -1) -
                  poly[:, 1] * np.roll(poly[:, 0], -1))
    if abs(area) < 1e-8:
        return False
    orientation = 1 if area > 0 else -1
    corners = ((x, y), (x + size, y), (x, y + size), (x + size, y + size))
    for a, b in zip(poly, np.roll(poly, -1, axis=0)):
        edge = b - a
        length = np.linalg.norm(edge)
        if length < 1e-8:
            continue
        distance = min(orientation * (edge[0] * (cy - a[1]) -
                                      edge[1] * (cx - a[0])) / length
                       for cx, cy in corners)
        if distance < 0.2:
            return False
    return True


def projected_models(root, assets, sample):
    geometry = load(root / "geometry.json")
    require(geometry.get("schema") == 1 and geometry.get("world") == "maps/e1m1.bsp",
            "loaded native geometry export mismatch")
    origins = {"progs/vr_alpha_wet.mdl": (828, 850, -297),
               "progs/vr_alpha_dry.mdl": (844, 850, -295)}
    records = {entry.get("name"): entry for entry in geometry.get("entities", [])}
    require(set(records) == set(origins), "native alias geometry identities mismatch")
    center = matrix(sample["matrices"]["center_clip"])
    eye = matrix(sample["matrices"]["eye_clip"][sample["eye"]])
    transform = eye @ center
    result = {}
    for name, origin in origins.items():
        entity = records[name]
        meta = assets["assets"][name]["geometry"]
        require(entity.get("origin") == list(origin) and entity.get("angles") == [0, 0, 0] and
                entity.get("scale") == meta.get("scale") and
                entity.get("scale_origin") == meta.get("scale_origin"),
                f"native transform/asset geometry mismatch: {name}")
        vertices = np.asarray(meta.get("vertices_byte"), dtype=float)
        require(vertices.shape == (4, 3) and np.isfinite(vertices).all(),
                f"invalid native alias vertices: {name}")
        world = vertices * np.asarray(entity["scale"]) + np.asarray(entity["scale_origin"]) + origin
        unique, polygons = set(), []
        for triangle in meta.get("triangles", []):
            if not triangle.get("facesfront"):
                continue
            indices = tuple(triangle.get("vertices", []))
            key = tuple(sorted(indices))
            if key in unique:
                continue
            unique.add(key)
            require(len(indices) == 3 and all(0 <= i < len(world) for i in indices),
                    f"invalid triangle in asset manifest: {name}")
            polygon = clip_polygon(world[list(indices)], transform)
            if len(polygon) >= 3:
                polygons.append(polygon)
        result["wet" if "_wet." in name else "dry"] = polygons
    return result


def mirror_patch(x, y, size):
    # For a 2x linear blit, destination samples at 2*i+1 and 2*i+2
    # depend only on source texels i and i+1. Every returned sample therefore
    # has support contained by this aligned native 2x2/4x4 fragment.
    xs = sorted({v for i in range(x, x + size - 1) for v in (2 * i + 1, 2 * i + 2)})
    ys = sorted({v for i in range(y, y + size - 1) for v in (2 * i + 1, 2 * i + 2)})
    for mirror_x in xs:
        source_x = (mirror_x + 0.5) * NATIVE_EXTENT[0] / MIRROR_EXTENT[0] - 0.5
        support_x = {max(0, math.floor(source_x)),
                     min(NATIVE_EXTENT[0] - 1, math.floor(source_x) + 1)}
        require(support_x <= set(range(x, x + size)),
                "linear mirror sample reaches outside selected native fragment")
    for mirror_y in ys:
        source_y = (mirror_y + 0.5) * NATIVE_EXTENT[1] / MIRROR_EXTENT[1] - 0.5
        support_y = {max(0, math.floor(source_y)),
                     min(NATIVE_EXTENT[1] - 1, math.floor(source_y) + 1)}
        require(support_y <= set(range(y, y + size)),
                "linear mirror sample reaches outside selected native fragment")
    return xs, ys


def tile_record(rate_bytes, rate, x, y):
    rw, rh = rate["map_extent"]
    tw, th = rate["texel_size"]
    layers = rate["layers"]
    layer = rate["selected_layer"]
    tile = (y // th) * rw + (x // tw)
    return layer * rw * rh + tile, rate_bytes[layer * rw * rh + tile]


def select_sensitive_fragments(polygons, assets, sample, off_b, off_e, rate_bytes):
    rate = sample["rate_map"]
    tw, th = rate["texel_size"]
    fragments = {}
    for model, triangles in polygons.items():
        found = {}
        for tri_id, polygon in enumerate(triangles):
            for y in range(0, NATIVE_EXTENT[1] - 1, 2):
                for x in range(0, NATIVE_EXTENT[0] - 1, 2):
                    map_index, code = tile_record(rate_bytes, rate, x, y)
                    size = 2 if code == RATE_2X2 else 4 if code == RATE_4X4 else 0
                    if not size or x % size or y % size or x + size > NATIVE_EXTENT[0] or y + size > NATIVE_EXTENT[1]:
                        continue
                    if not inside_block(polygon, x, y, size):
                        continue
                    source_tiles = {tile_record(rate_bytes, rate, sx, sy)
                                    for sy in range(y, y + size)
                                    for sx in range(x, x + size)}
                    if len(source_tiles) != 1 or next(iter(source_tiles))[0] != map_index or \
                            next(iter(source_tiles))[1] != code:
                        continue
                    xs, ys = mirror_patch(x, y, size)
                    b_patch = off_b[np.ix_(ys, xs)]
                    e_patch = off_e[np.ix_(ys, xs)]
                    influenced = bool(np.any(b_patch != e_patch))
                    varied = bool(np.any(e_patch != e_patch[0, 0]))
                    key = (x, y, size)
                    record = found.setdefault(key, dict(x=x, y=y, size=size, triangle=tri_id,
                                                        rate_code=code, map_index=map_index,
                                                        influenced=False, texture_varied=False,
                                                        mirror_x=xs, mirror_y=ys))
                    record["influenced"] |= influenced
                    record["texture_varied"] |= varied
        selected = [record for record in found.values()
                    if record["influenced"] and record["texture_varied"]]
        require(len(selected) >= 16,
                f"{model}: fewer than 16 independently selected coarse native fragments with B/E influence and supported local variation")
        fragments[model] = selected
    return fragments


def protected_clearance_mask(polygons):
    height, width = MIRROR_EXTENT[1], MIRROR_EXTENT[0]
    px, py = np.meshgrid(np.arange(width, dtype=float) + 0.5,
                         np.arange(height, dtype=float) + 0.5)
    protected = np.zeros((height, width), dtype=bool)
    for model_triangles in polygons.values():
        for polygon in model_triangles:
            p = polygon * 2.0
            inside = np.zeros_like(protected)
            min_distance2 = np.full((height, width), np.inf)
            for a, b in zip(p, np.roll(p, -1, axis=0)):
                crosses = ((a[1] > py) != (b[1] > py)) & \
                    (px < (b[0] - a[0]) * (py - a[1]) /
                          ((b[1] - a[1]) if b[1] != a[1] else 1e-300) + a[0])
                inside ^= crosses
                edge = b - a
                length2 = float(np.dot(edge, edge))
                if length2:
                    t = np.clip(((px - a[0]) * edge[0] + (py - a[1]) * edge[1]) / length2, 0, 1)
                    dx = px - (a[0] + t * edge[0])
                    dy = py - (a[1] + t * edge[1])
                    min_distance2 = np.minimum(min_distance2, dx * dx + dy * dy)
            # Four mirror pixels clears the linear-blit support plus one extra
            # native pixel before admitting a world-only comparison pixel.
            protected |= inside | (min_distance2 <= 16.0)
    return protected


def coarse_mirror_mask(rate_bytes, rate):
    width, height = MIRROR_EXTENT
    source_w, source_h = NATIVE_EXTENT
    mask = np.zeros((height, width), dtype=bool)
    for y in range(height):
        sy = (y + 0.5) * source_h / height - 0.5
        y0, y1 = max(0, math.floor(sy)), min(source_h - 1, math.floor(sy) + 1)
        for x in range(width):
            sx = (x + 0.5) * source_w / width - 0.5
            x0, x1 = max(0, math.floor(sx)), min(source_w - 1, math.floor(sx) + 1)
            supports = {tile_record(rate_bytes, rate, xx, yy)
                        for yy in {y0, y1} for xx in {x0, x1}}
            mask[y, x] = len(supports) == 1 and next(iter(supports))[1] in (RATE_2X2, RATE_4X4)
    return mask


def run_eye(root, expected_eye, assets):
    samples, images = validate_eye_capture(root, expected_eye)
    fixed_maps = [validate_rate_map(sample, expected_eye) for sample in samples if sample["mode"] == 1]
    require(all(rate == fixed_maps[0] for rate in fixed_maps[1:]),
            f"eye {expected_eye} fixed map changed across B/E/repeats")
    polygons = projected_models(root, assets, samples[0])
    # Selection uses OFF B/E, actual fixed attachment bytes, projected asset
    # geometry, and a bounded mirror-filter support. FIXED/E pixels are not
    # consulted until after this independent witness set is frozen.
    witnesses = select_sensitive_fragments(polygons, assets, samples[6],
                                           images[0], images[2], fixed_maps[0])
    selected_pixels = {}
    for model, candidates in witnesses.items():
        coords = sorted({(x, y) for c in candidates for x in c["mirror_x"] for y in c["mirror_y"]})
        xs = np.fromiter((p[0] for p in coords), dtype=int)
        ys = np.fromiter((p[1] for p in coords), dtype=int)
        for fixed_phase in (6, 7):
            # Check exact equality only at the independently selected native
            # fragment samples, with no fitted tolerance or fitted ROI.
            unequal = np.any(images[2][ys, xs] != images[fixed_phase][ys, xs], axis=1)
            require(not np.any(unequal), f"{model}: protected OFF/E versus FIXED/E pixels differ",
                    "equivalencefailed")
        selected_pixels[model] = len(coords)

    rate = samples[6]["rate_map"]
    world_mask = coarse_mirror_mask(fixed_maps[0], rate) & ~protected_clearance_mask(polygons)
    require(int(world_mask.sum()) > 0, f"eye {expected_eye} has no coarse world pixels outside protected geometry")
    world_diff = np.any(images[2] != images[6], axis=2) & world_mask
    require(int(world_diff.sum()) > 0,
            f"eye {expected_eye} has no stable OFF/FIXED world difference under coarse mapped pixels")
    fixed_world_calls = [call for sample in samples if sample["mode"] == 1
                         for call in sample.get("world_rate_calls", [])]
    require(any(call.get("eligible") and not call.get("depth_only") and
                call.get("active") and call.get("command_submitted")
                for call in fixed_world_calls),
            f"eye {expected_eye} lacks eligible native world-rate setter observations")

    create_records = samples[6].get("alias_pipeline_creates", [])
    fixed_pipeline_records = [record for record in create_records if record.get("active") and
                              record.get("name") in ("alias 0", "alias_main_oit 0")]
    require(fixed_pipeline_records, f"eye {expected_eye} lacks active-mode opaque alias pipeline creation evidence")
    for record in fixed_pipeline_records:
        require(FSR_DYNAMIC_STATE not in record.get("dynamic_states", []) and
                record.get("graphics_pnext_null") is True,
                f"eye {expected_eye} opaque alias pipeline declares KHR fragment-rate state")
    pipeline_handles = {
        active: {record.get("handle") for record in create_records
                 if bool(record.get("active")) == active and
                 record.get("name") in ("alias 0", "alias_main_oit 0")}
        for active in (False, True)
    }
    for sample in (samples[2], samples[3], samples[6], samples[7]):
        identities = {entry["name"]: entry for entry in sample["identities"]}
        draws = sample.get("alias_draws", [])
        admissions = sample.get("alias_admissions", [])
        for model in ("wet", "dry"):
            expected_buffer = identities[model]["vertex_buffer"]
            require(any(draw.get("vertex_buffer") == expected_buffer and
                        draw.get("index_count") == identities[model]["index_count"] and
                        draw.get("instance_count", 0) > 0 and
                        all(alpha == 1.0 for alpha in draw.get("alphas", [])) and
                        draw.get("pipeline") in pipeline_handles[sample["mode"] == 1]
                        for draw in draws),
                    f"eye {expected_eye} {model}: opaque fixture did not consume the observed default alias pipeline")
            require(any(entry.get("model") == model and entry.get("alpha") == 0 and
                        entry.get("triangles") == 4 for entry in admissions),
                    f"eye {expected_eye} {model}: opaque native alias admission not observed")
    return dict(eye=expected_eye,
                selected_native_fragments={model: len(rows) for model, rows in witnesses.items()},
                supported_mirror_pixels=selected_pixels,
                world_coarse_pixels=int(world_mask.sum()),
                stable_world_difference_pixels=int(world_diff.sum()),
                alias_default_pipeline_handles=sorted(pipeline_handles[True]),
                alias_pipeline_default={"fragment_rate": "1x1",
                                        "combiner": "KEEP/KEEP",
                                        "evidence": "KHR state pNext absent; no dynamic fragment-rate state"},
                rate_map_extent=rate["map_extent"],rate_map_texel_size=rate["texel_size"],
                rate_map_layers=rate["layers"],rate_map_selected_layer=rate["selected_layer"],
                rate_map_sha256=hashlib.sha256(fixed_maps[0]).hexdigest(),
                center_clip=samples[0]["matrices"]["center_clip"],
                source_fovs=samples[0]["source_fovs"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--eye0", required=True, type=Path, help="eye 0 capture directory")
    parser.add_argument("--eye1", required=True, type=Path, help="eye 1 capture directory")
    parser.add_argument("--assets", required=True, type=Path,
                        help="private assets.json from prepare_foveation_protected_native.py")
    args = parser.parse_args()
    report = {"status": "inconclusive", "eyes": {}}
    try:
        assets = validate_assets(args.assets)
        for eye, root in ((0, args.eye0), (1, args.eye1)):
            report["eyes"][str(eye)] = run_eye(root, eye, assets)
        left, right = report["eyes"]["0"], report["eyes"]["1"]
        require(left["center_clip"] == right["center_clip"] and
                left["source_fovs"] == right["source_fovs"] and
                left["rate_map_sha256"] == right["rate_map_sha256"] and
                left["rate_map_layers"] == right["rate_map_layers"] and
                left["rate_map_extent"] == right["rate_map_extent"] and
                left["rate_map_texel_size"] == right["rate_map_texel_size"],
                "eye-target runs did not retain one shared stereo source/map")
        report["status"] = "passed"
    except CheckError as exc:
        report["status"], report["error"] = exc.status, str(exc)
    except Exception as exc:
        report["status"], report["error"] = "inconclusive", f"invalid capture: {exc}"
    print(json.dumps(report, separators=(",", ":"), sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
