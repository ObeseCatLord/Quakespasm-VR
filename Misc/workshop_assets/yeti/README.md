# Yeti skin

The yeti is the shambler from the player's own Quake `id1` pak wearing a new coat. The shambler model is
never stored here: only our skin is, `yeti_skin.png` (308 x 115, the shambler's skin size) and its palette
index copy `../../vq_pak/progs/workshop/yeti_skin.lmp` (little-endian int width, int height, then
width x height Quake palette indices).

    QUAKE_PALETTE=/path/to/palette.lmp python3 paint_yeti.py /path/to/shambler.mdl

Extract `progs/shambler.mdl` and `gfx/palette.lmp` from your own pak to somewhere outside this repository.
`paint_yeti.py` reads only the model's UV layout and facet normals and paints every texel fresh:

- fur: `yeti_fur_swatch.png` cut down to chunky pixels (nearest neighbour), tiled so it hangs downward and
  turned to lie along the upper arms, with single darker and lighter strands added, then reduced to the
  palette's greys and blue-greys;
- light by region: bright shoulders and back, blue-grey shade on the belly, armpits, inner arms, between the
  legs and on facets that face down, in dithered steps;
- ragged ice-blue cuffs where the fur ends, blue-grey hide on the hands and feet darkening into the claws;
- the face: hide, a dark brow, a dark mouth with pale teeth, and two small eyes (the only fullbright texels).

The skin was checked on the stand pose in Blender (model imported from the decompiled `.mdl`, kept outside
the repository).
