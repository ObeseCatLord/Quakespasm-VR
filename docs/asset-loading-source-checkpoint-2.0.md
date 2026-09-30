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


## Adopted bounded local Astra source disposition

Requested local Astra/max confirmed the empty-name and shared-slot defects.
Effective settings remain unobservable, so this is source advice, not formal
review certification. Main checked all texels readers, texture reload and format
selection. The two-statement estimate is reopened for the necessary consumer.

| Finding | Disposition |
| --- | --- |
| MDX recoloring uses skin-zero uploaded dimensions for another skin's copied raw bytes | Adopt repair at R_TranslateNewPlayerSkin: native MDL keeps copied pixels/header dimensions; non-MDL pixels and dimensions must come from the same existing Image_LoadImage decode. Missing/non-indexed data clears recoloring, retaining native material rendering. |
| Texture source dimensions mutate on external reload while the copied skin buffer does not | Reject metadata-only dimension substitution: a resized image would still permit an overread. Decode indexed MDX data at infrequent recolor/model/skin changes using native source path/path_id; do not add dimension/cache generations. |
| Native recolor format selection ignores the requested skin | Adapt through existing Mod_Extradata_CheckSkin with the actual requested skin, matching renderer format fallback. Branch on returned poseverttype, never filename. |
| Correcting a copied MDX texel slot leaves a now-unused cache | Main source-verified simplification: Quake's sole texels reader is R_TranslateNewPlayerSkin; other uses only allocate/free. Remove the MDX-only cached first-frame copy entirely once that consumer decodes safely. Preserve native MDL cached texels and fullbright scan. This supersedes the uncommitted index-only correction and removes duplicate buffer ownership rather than adding state. |

Exact write set expands only to Quake/gl_model.c and R_TranslateNewPlayerSkin in
Quake/gl_rmisc.c. Estimate20..40 changed lines, largely native consumer reuse;
reopen above60 or another texture/cache owner. Final checks add resized indexed
MDX sources, mixed dimensions and enhanced-model fallback for later skin IDs.
No builds/tests/compiler/probes/fixtures or performance measurements run.


### Upload-cache identity amendment before integration

Final local Astra source assessment found a P2: native TexMgr_LoadImage returns
an existing named texture on CRC equality before updating dimensions/source.
Two indexed skins with identical bytes but64x64 versus128x32 dimensions, or
identical bytes from different sources, can retain the previous skin's reload
provenance. Main verified this exact early return at gl_texmgr.c:1502 and the
later metadata writes. This blocks the planned multi-skin/reload result.

Adapt the recommended caller-specific retirement to the smaller native owner:
extend the existing CRC fast-path admission with source dimensions, source
format, filename and offset equality. This preserves existing object/resource
lifetime and the ordinary cache hit, without duplicating cache identity policy
or manually retiring a texture at the recolor caller. Replacing the cache,
adding identity state, or invalidating every recolor is rejected. Source metadata
already exists and later native writes remain the only metadata owner.

The write set adds only that admission expression in Quake/gl_texmgr.c. Total
expected production changes remain under60 lines; reopen a broader texture-manager
rewrite. Final checks include same bytes/different dimensions and same bytes/
different skin sources. Main integration/source recheck precedes acceptance;
no builds/tests/probes/fixtures or performance claims accompany this amendment.


## Integrated repair source checkpoint

The final production patch is42 changed lines /10 net across gl_model.c,
gl_rmisc.c and gl_texmgr.c. Main reviewed the full patch and native call sites;
local Astra's final bounded source assessment found no remaining P1/P2 blocker.
Empty MDX frame jobs stop before file lookup; redundant indexed MDX copies are
removed while native MDL copies/fullbright scans remain. Recolor selects the
actual skin's native format and pairs freshly decoded indexed MDX bytes with
matching dimensions, freeing temporary pixels on both paths. Truecolor avoids
that decode. Existing texture reuse now requires matching dimensions/format/
source as well as CRC; a miss uses the native overwrite/object/upload lifetime.

Scoped git diff --check passes. No builds/tests/compiler/probes/fixtures or
measurements ran. Final multi-skin/fullbright/decoder/search-path/reload/recolor
and Linux/ARM qualification remains open; this is source integration only.
