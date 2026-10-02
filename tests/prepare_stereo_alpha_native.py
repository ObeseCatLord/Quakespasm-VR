#!/usr/bin/env python3
"""Emit the fixed Quake v6 MDL inputs for the native stereo-alpha fixture."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile


HEADER = struct.Struct("<ii10f8if")
STVERT = struct.Struct("<3i")
TRIANGLE = struct.Struct("<4i")
TRIVERTEX = struct.Struct("<4B")
EXPECTED_MDL_BYTES = 308

SCALE = (1.0 / 16.0, 1.0 / 16.0, 1.0 / 16.0)
SCALE_ORIGIN = (-6.0, 0.0, -0.75)
VERTICES = ((0, 0, 0), (192, 0, 0), (192, 0, 24), (0, 0, 24))
ST_COORDS = ((1, 1), (6, 1), (6, 6), (1, 6))
TRIANGLES = ((0, 1, 2), (0, 2, 3), (2, 1, 0), (3, 2, 0))
PALETTE = {
    "wet": (250, (215, 0, 0)),
    "dry": (244, (127, 191, 255)),
}


def pack_trivert(vertex):
    return TRIVERTEX.pack(*vertex, 0)


def make_mdl(palette_index):
    header = HEADER.pack(
        0x4F504449,  # IDPO, stored little-endian as b"IDPO".
        6,
        *SCALE,
        *SCALE_ORIGIN,
        math.sqrt(36.5625),
        0.0, 0.0, 0.0,
        1, 8, 8, 4, 4, 1, 0, 0,
        1.0,
    )
    # Mod_FloodFillSkin skips a top-left 255 texel; interior UVs avoid it.
    skin = (
        struct.pack("<i", 0)
        + bytes((255,))
        + bytes((palette_index,)) * (8 * 8 - 1)
    )
    stverts = b"".join(
        STVERT.pack(0, s, t) for s, t in ST_COORDS
    )
    triangles = b"".join(
        TRIANGLE.pack(1, *indices) for indices in TRIANGLES
    )
    frame_name = b"frame0".ljust(16, b"\0")
    frame = (
        struct.pack("<i", 0)
        + pack_trivert((0, 0, 0))
        + pack_trivert((192, 0, 24))
        + frame_name
        + b"".join(pack_trivert(vertex) for vertex in VERTICES)
    )
    mdl = header + skin + stverts + triangles + frame
    if len(header) != 84 or len(mdl) != EXPECTED_MDL_BYTES:
        raise AssertionError(
            f"unexpected MDL packing: {len(header)}-byte header, "
            f"{len(mdl)}-byte model"
        )
    return mdl


def atomic_write_new(path, data):
    """Publish a complete file atomically, failing if its name already exists."""
    fd, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        # A hard link installs the fully written inode atomically without
        # replacing a file that appeared after the output-directory check.
        os.link(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def prepare_output_dir(path):
    try:
        path.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        if not path.is_dir():
            raise SystemExit(f"refusing non-directory output path: {path}")
        if next(path.iterdir(), None) is not None:
            raise SystemExit(f"refusing non-empty output directory: {path}")
    return path.resolve()


def asset_record(name, palette_index, rgb, data):
    return {
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "dimensions": {
            "skin_width": 8,
            "skin_height": 8,
            "vertices": 4,
            "triangles": 4,
            "frames": 1,
            "model_units": [12.0, 0.0, 1.5],
        },
        "geometry": {
            "scale": list(SCALE),
            "scale_origin": list(SCALE_ORIGIN),
            "bounding_radius": math.sqrt(36.5625),
            "vertices_byte": [list(vertex) for vertex in VERTICES],
            "bbox_min_byte": [0, 0, 0],
            "bbox_max_byte": [192, 0, 24],
            "st_vertices": [
                {"onseam": 0, "s": s, "t": t} for s, t in ST_COORDS
            ],
            "uv_texel_coordinates": [list(coord) for coord in ST_COORDS],
            "triangles": [
                {"facesfront": 1, "vertices": list(triangle)}
                for triangle in TRIANGLES
            ],
            "frame": "frame0",
        },
        "color": {
            "palette_index": palette_index,
            "rgb": list(rgb),
            "skin_is_uniform": False,
            "skin_top_left_guard_palette_index": 255,
            "skin_top_left_guard_texel_count": 1,
            "skin_intended_color_texel_count": 63,
        },
        "file": f"progs/{name}",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir", required=True, type=Path,
        help="new or empty output directory (existing content is refused)",
    )
    args = parser.parse_args()

    root = prepare_output_dir(args.output_dir)
    progs = root / "progs"
    progs.mkdir()

    assets = {}
    for variant, filename in (
        ("wet", "vr_alpha_wet.mdl"),
        ("dry", "vr_alpha_dry.mdl"),
    ):
        palette_index, rgb = PALETTE[variant]
        data = make_mdl(palette_index)
        atomic_write_new(progs / filename, data)
        assets[f"progs/{filename}"] = asset_record(
            filename, palette_index, rgb, data
        )

    manifest = {
        "format": "Quake alias model v6",
        "assets": assets,
    }
    atomic_write_new(
        root / "assets.json",
        (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(root)


if __name__ == "__main__":
    main()
