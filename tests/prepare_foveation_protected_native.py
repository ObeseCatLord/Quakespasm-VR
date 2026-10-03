#!/usr/bin/env python3
"""Emit private patterned alias models for native protected-output capture."""

import argparse
import json
from pathlib import Path

import prepare_stereo_alpha_native as alpha


SKIN_OFFSET = alpha.HEADER.size + 4  # MDL header and single-skin group tag.
GUARD_INDEX = 255
CONTRAST_INDEX = 254


def make_patterned_mdl(palette_index):
    data = bytearray(alpha.make_mdl(palette_index))
    counts = {str(palette_index): 0, str(CONTRAST_INDEX): 0,
              str(GUARD_INDEX): 0}
    for y in range(8):
        for x in range(8):
            if x == 0 and y == 0:
                value = GUARD_INDEX
            else:
                value = palette_index if (x + y) % 2 == 0 else CONTRAST_INDEX
            data[SKIN_OFFSET + y * 8 + x] = value
            counts[str(value)] += 1
    result = bytes(data)
    if len(result) != alpha.EXPECTED_MDL_BYTES or result[SKIN_OFFSET] != GUARD_INDEX:
        raise AssertionError("patterned skin changed MDL packing or flood-fill guard")
    if counts != {str(palette_index): 31, str(CONTRAST_INDEX): 32,
                  str(GUARD_INDEX): 1}:
        raise AssertionError(f"unexpected patterned skin counts: {counts}")
    return result, counts


def asset_record(filename, palette_index, rgb, data, counts):
    record = alpha.asset_record(filename, palette_index, rgb, data)
    record["color"].update({
        "skin_is_uniform": False,
        "skin_top_left_guard_palette_index": GUARD_INDEX,
        "skin_top_left_guard_texel_count": counts[str(GUARD_INDEX)],
        "skin_intended_color_texel_count": 63,
        "skin_pattern": {
            "name": "8x8 checkerboard",
            "even_parity_palette_index": palette_index,
            "odd_parity_palette_index": CONTRAST_INDEX,
            "guard_coordinate": [0, 0],
            "counts_by_palette_index": counts,
        },
    })
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir", required=True, type=Path,
        help="private new or empty output directory; licensed map data stays outside it",
    )
    args = parser.parse_args()

    root = alpha.prepare_output_dir(args.output_dir)
    progs = root / "progs"
    progs.mkdir()

    assets = {}
    for variant, filename in (
        ("wet", "vr_alpha_wet.mdl"),
        ("dry", "vr_alpha_dry.mdl"),
    ):
        palette_index, rgb = alpha.PALETTE[variant]
        data, counts = make_patterned_mdl(palette_index)
        alpha.atomic_write_new(progs / filename, data)
        key = f"progs/{filename}"
        assets[key] = asset_record(filename, palette_index, rgb, data, counts)

    manifest = {
        "format": "Quake alias model v6",
        "skin_pattern": "alternating source fullbright category / palette 254",
        "assets": assets,
    }
    alpha.atomic_write_new(
        root / "assets.json",
        (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(root)


if __name__ == "__main__":
    main()
