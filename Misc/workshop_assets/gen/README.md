# Workshop asset generator

Scripts that rebuild every asset folder next to this one (skins, OBJ frames, `spec.json`, and a `.blend`
per asset that is not kept in the repository, since a saved `.blend` records local paths) and the
`.mdl` files in `Misc/vq_pak/progs/workshop/`.

    QUAKE_PALETTE=/path/to/palette.lmp ./build_all.sh              # everything
    ./build_all.sh --palette /path/to/palette.lmp starhook bulb    # just some assets

The palette is not stored in this repository: extract `gfx/palette.lmp` (768 bytes) from your own Quake
`id1/pak0.pak` and point `QUAKE_PALETTE` at it.

Needs `python3` with Pillow and Blender on the PATH (or `BLENDER=/path/to/blender`); Blender runs headless
(`blender -b --factory-startup --python bl_build.py -- <asset> ...`). Intermediate mesh JSON goes to
`$WORKSHOP_WORK` (default `$TMPDIR/workshop_gen`).

- `kit.py` - tiny modelling/painting kit: lofts, fans, atlas rects, palette-index painting, fullbright mask.
- `assets.py` - the eight asset definitions (geometry, UVs, skins, frames).
- `saucer.py` - the saucer enemy, its snowball and hull gib. The saucer and gib skins are cut from the painted
  concept sheet `../concept/saucer_concept.png` (nearest-neighbour, reduced to a few palette ramps); the rune,
  portholes and lens are painted in fullbright colours.
- `bl_build.py` - builds each `.blend` (one mesh, a shape key per frame) and exports `frame_<name>.obj`.
  The OBJ exporter is called with forward=Y, up=Z: that is what keeps coordinates as modelled
  (+X forward, +Y left, +Z up, 1 unit = 1 Quake unit).
- `check.py` - verifies the exported OBJs match the source mesh and share vertex/face order across frames.

Skins are painted directly in palette colours, so `mdltool.py` quantizes them losslessly. Colours 224-254 are
fullbright; there is no fullbright green in the Quake palette.
