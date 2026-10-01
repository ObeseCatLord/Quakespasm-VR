# Portable GCC13 qualification corrections

2026-10-01. Required final build defects, attached to existing stereo/foveation
and Linux/ARM owners. Both immutable48e026e0 native builds fail effective_view
warnings at -O3/-Werror; amd64 also fails cached format-result warning.
Local Astra independently checked the contracts; runtime failure is unproved.

## Minimal design versus replacement

R_PrepareStereoFrame already owns a non-null immutable frame snapshot; the
existing public R_StereoSceneView repeats frame lookup, copies an eye and scales
four tangents, returning false without writes if no frame is available. Initialize
the local effective view from the already-owned matching frame->views[eye]
before calling that existing helper. Likewise initialize the two optional
underwater clip views from the same frame before their helper calls. Retain
helper scaling, projections, stereo culling, either-eye union and frame owners.
No new view helper/API, frame state machine or renderer rewrite. The compiler
can remove redundant success-path copies; failed lookup cannot leave local
storage uninitialized. No new claim that frame invalidation is reachable here.

GL_DensityOffsetSceneFormatSupported's existing hit and miss branches both
assign result under stable cache-count assumptions. Initialize result to
VK_ERROR_FORMAT_NOT_SUPPORTED, preserving exact successful cached/query values
and native fail-closed optional support policy. Do not weaken warnings, disable
foveation, replace the cache or add a query on every frame.

## Ownership, estimate and acceptance

One Luna xhigh coding worker owns only Quake/gl_rmain.c and gl_vidsdl.c, with
under20 added/deleted lines total. No overlap: main only reads those files while
diagnosing current binary desktop hazards; metadata worker owns its test/runner/
receipt. No new fixture, command or feature. Worker does not build or commit.
If more changes seem necessary, stop/report instead of widening scope.

Main reviews complete patch, commits it, rebuilds host, and runs complete native
amd64/ARM builds at one immutable revision. Retain generated shaders, full
Steam Audio/codecs and warnings-as-errors. After renderer fixes, rerun the actual
XR matrix; previous passing run is evidence for the earlier source only.
All eight qualification groups remain open until their own evidence exists.
