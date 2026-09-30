# Reuse client model lookup for the existing QC VM

Date: 2026-09-30. Production scope: `Quake/host.c`, on `2.0` only.

## Verified missing boundary

`qcvm_t` already has a `GetModel(int)` callback shared by model/frame/surface
builtins in `Quake/pr_ext.c`. Server initialization assigns `SV_ModelForIndex`.
The actual client `CL_LoadCSProgs` path clears/reloads `cl.qcvm`, allocates its
edicts and initializes its world/globals, but never assigns that callback.
Direct client calls such as `modelnameforindex` therefore reach a null callback;
the same missing boundary affects existing frame and surface queries.

Primary master `51b452c0` implements client model-name and surface lookup directly
from `cl.model_precache`, establishing the required behavior. Pinned QSS-M
`Quake/host.c:CL_ModelForIndex` already provides the needed callback-shaped
helper: return NULL for an index outside `[0, MAX_MODELS)`, otherwise return
`cl.model_precache[index]`. QSS-M's current initialization also supports its own
client-local precache layer through `PR_CSQC_GetModel`; that additional layer is
not required by the inherited primary lookup or the destination's current
model owner and is not copied in this repair.

## Minimal reuse and alternatives

Copy the QSS-M bounded helper into `host.c`, with local static visibility, and
assign it to `qcvm->GetModel` in the accepted `CL_LoadCSProgs` branch before
`CSQC_Init` can execute. Preserve native loader admission, model precache,
world/edict setup, builtin wrappers, permissions, fallback filenames and reload
cleanup. The helper reads the live native precache; no retained pointer/cache
or second model index namespace is introduced. Server lookup is unchanged.

Importing primary's separate direct lookup into each builtin would duplicate
the existing callback boundary. Importing QSS-M's local-model subsystem would
expand ownership without a demonstrated requirement for this correction.
Reject both replacements. Expected production: one copied eight-line helper
and one callback assignment in one existing file.

## Sequence and acceptance

Commit plan before production. Main owns this small existing-owner integration;
it is independent of the preceding agent's `pr_ext.c` wrapper slice. A bounded
local Astra source review must inspect initialization timing, reset/reload,
negative/limit index checks, native precache ownership, server isolation and
actual calling builtins. No new expensive architecture fork or renderer change.

No builds/tests/compiler/runtime probes/fixtures until the full implementation
finishes. `git diff --check` is allowed. Final Linux/ARM VM qualification must
load ordinary admitted client QC, invoke `modelnameforindex`, valid frame/surface
queries and `setmodelindex`, exercise missing/negative/equal-to-limit indices,
reload/map/game transitions and simultaneous SSQC, and observe native model-name
results and surface geometry. Calls through a manually populated callback alone
cannot qualify actual initialization. This slice does not add complete full-game
CSQC rendering or local client-only model precaching; broader inherited mod
interface and desktop/VR qualification remain required.

## Source implementation checkpoint

Commit `6f5f3d85` copies the bounded QSS-M helper with static visibility and
installs it in actual accepted client initialization before `CSQC_Init`.
Local requested-Astra Max source review found no actionable introduced defect
in callback timing, reload reset, live native precache lookup or server isolation.
Effective reviewer model/effort metadata was unavailable; this is bounded
advisory source acceptance, not a certified senior-review gate. No builds/tests
or runtime probes ran; final Linux/ARM actual-loader qualification is pending.
