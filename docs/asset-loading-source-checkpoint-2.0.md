# Native asset loading source checkpoint and narrow repair plan

2026-09-30. ASSET-001..009 source comparison on2.0, reference native vkQuake
4bc898f2. This is not rendered/material acceptance; final Linux/ARM checks remain
after the complete implementation pass. Keep native loaders and GPU materials.

| Requirement | Current source ownership | Remaining proof |
| --- | --- | --- |
| PNG/TGA/JPG/JPEG | image.c:157..211 uses existing STB decoder and path_id-first format tie order; names/precedence are shared by all consumers. | Actual competing search paths, decode and reload. |
| MD3/MD5 truecolor + fullbrights | gl_model.c:6594..6692 shared native MDX skin/frame worker loads base plus glow/luma; :7228 MD5 and :8047..8074/:8275 MD3 call it. Worker/serial paths join before the name table expires. | Multiple skins/framegroups/surfaces, PNG/TGA/JPG and glow/luma in native draws. |
| Level/static brush truecolor | gl_model.c:1525..1637 preserves map-specific then global texture paths, static-model path floor, palette fallback and glow/luma. MDL/static alias skins use native :4255 loader. | World versus static-model precedence, filtering, cutout and fullbright appearance. |
| WAD3/per-texture palettes | Existing bounded WAD3 adapter at gl_model.c:1351..1434 supplies per-texture palette/source provenance to native texture loading. | Actual embedded/external WAD3 and material images; existing WAD disposition remains. |
| Lightmapped liquids | gl_model.c:2538..2541 flags unlit water tiled only when no samples; native r_world liquid draws retain atlas selection. | Lit versus unlit water and moving liquid brushes, both eyes. |
| Lightstyle interpolation | Existing gl_rlight R_AnimateLight adapter feeds both native CPU/GPU update paths; shared lightmap compute retains dirty-region/task ownership. | Modes0/1/2 and actual smooth/abrupt styles including ad_tears. |

## Verified missing native worker contracts, before repair

The primary/native task structure is already sufficient; no alternate decoder,
material registry, loader task or skin cache is justified.

1. Mod_LoadMDXSkinTask:6605 forms basic_texname from an inline char array
(gl_model.h:428..436). Its null-pointer check can never reject an empty name.
Mod_LoadMDXSkinsByIndex submits MAX_FRAMEGROUPS jobs per skin even for a single
frame (:6751), with zeroed unused strings. Empty jobs currently search bare
extensions and progs/textures fallback names. Replace that check with the first
character test, matching the name-table producer's empty-string terminator.
2. The indexed f==0 branch at:6661 writes texels[surf->numskins]. That count is
assigned only after jobs return (:7228/:8275), so different skins share the
same pre-load slot. Native MDL loading uses texels[i] (:4283), and recoloring
reads texels[skinnum] (gl_rmisc.c:5339). Write the already validated scheduled
skin_index instead. Each skin's first-frame job then has its existing own slot;
fullbright/color/material policy and worker scheduling remain unchanged.

Ownership: only these two statements and a short ownership comment in
Quake/gl_model.c. Expected2 changed statements, no new owner. Main integrates
independently of the server-policy coder; no overlapping write set. Source
comparison/git diff --check only. Final checks include unused frame names,
indexed multi-skin reload/recolor and truecolor fullbright regressions with
native workers and serial loading; no new fixtures/tests run during this pass.
