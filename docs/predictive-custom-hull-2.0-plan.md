# Reuse native Quake hull coordinates in shared PMove

Status: verified implementation brief before production edits. Cooperative
state source audit found this concrete collision gap. Main integrates on `2.0`;
references and the user's modified migration document remain untouched.

## Evidence and boundary

| Fact | Source verification |
| --- | --- |
| Cooperative standard physics supplies actual actor bounds to PMove. | `Quake/sv_phys.c:SV_RunStandardPlayerPhysics` copies `mins`/`maxs`, collects physents and calls the existing solver. |
| BSP collision ignores the bounds' origin and never chooses hull2. | `Quake/pmove.c:PM_TransformedHullCheck` currently selects hull0 for width <3, otherwise hull1, and subtracts only the brush origin. A standing actor with foot-origin bounds (-16,-16,0)..(16,16,56) traces 24 units below its intended foot. |
| Native vkQuake already implements the required hull selection/offset. | Actual `Quake/world.c:SV_HullForEntity` chooses hull0/hull1/hull2 at widths <3/<=32/>32, and computes `hull->clip_mins - actor_mins + brush_origin`. `SV_ClipMoveToEntity` transforms endpoints by that offset before rotation and returns hit endpoints in world coordinates. Client entity collision in `SV_ClipToLinks` mirrors it. |
| This limitation was inherited, not unique to the new builtin. | Actual primary VR `Quake/pmove.c:PM_TransformedHullCheck` and QSS-M `Quake/pmovetst.c:PM_TransformedHullCheck` also select only hull0/1 without the native offset. Recopying them does not fix it. |
| One trace boundary serves movement, stationary validation and brush contents. | `PM_PlayerTraceFiltered`, `PM_TestPlayerPosition`, `PM_ExtraBoxContents` and the existing surface point trace call the same helper. No second solver is needed. |
| Box planes already include the actor bounds. | `PM_PlayerTraceFiltered` builds `PM_HullForBox(other_mins - player_maxs, other_maxs - player_mins)` before the NULL-model trace. Adding player bounds again in its early rejection is redundant and can falsely reject shifted positive minima/negative maxima. The surface point trace uses zero bounds and a directly built box. |

Loaded BSP hull0/1/2 dimensions are provided by existing model loading. Native
Quake chooses a compiled hull by width; it does not supply arbitrary exact-size
brush collision. Do not advertise arbitrary custom hull precision or add a new
convex collision implementation. Stock (-16,-16,-24)..(16,16,32) has zero
native hull offset; ordinary point rays have zero point offset.

## Incremental design and decisions for Astra

Reuse native selection and offset math locally in `PM_TransformedHullCheck`.
Keep current solid-leaf mask, axis transforms, shared box hull, physent
collection, contact ownership, solver scratch and command/replay dispatch.
Do not call the server-edict-specific helper from client PMove or export a new
collision service. Expected production write set: only `Quake/pmove.c`, one
static helper, roughly25 added/changed lines.

For BSP early rejection, use hull-local endpoints plus the chosen hull's
`clip_mins`/`clip_maxs`. That represents the actual compiled hull after the
native offset, avoiding a false reject from applying the actor origin twice or
using smaller authored bounds than the chosen hull. Retain the existing early
rejection only for unrotated brushes; rotate in hull coordinates and restore
the endpoint with the complete native offset afterwards. Box early rejection
compares endpoints directly to its already-expanded planes.

Current lean: fix selection, shift, returned endpoints and both rejection
coordinate contracts together at this boundary. Rejected alternatives: a
second movement solver; silently keeping stock BSP bounds for a shifted actor;
changing the cooperative builtin's bounds to fake a stock origin; a new
shared service refactoring native world collision; dropping all early rejection
for custom bodies. None is required by the demonstrated incompatibility.

Personal Astra Max should verify the facts first, challenge the rejection math
and rotated/endpos handling, and seek simplification. No nested agents, code
edits, builds or probes. Depth budget: this helper and direct native/donor
references/callers only. Do not re-review movement queues, avatar or Vulkan
architecture. Missing evidence must remain unknown, not become a new framework.

## End-of-implementation acceptance

Use the existing trace/PMove and native fixtures. Compare actual loaded BSP
stock, shifted foot-origin and large-width traces with native hull selection and
offsets, including stationary/startsolid, clear and blocked rays, translated and
rotated brushes, and world-space returned endpoints/normals. Compare boxes with
expanded native box traces, including positive minima and negative maxima.
Preserve stock and zero-bound point behavior, normal command history/replay,
contact identity and authoritative cooperative body movement. Prepared traces
alone do not certify arbitrary authored mods. No builds/tests now; Linux and
isolated native ARM qualification follow the full implementation.
