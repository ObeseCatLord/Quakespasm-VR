# Deferred clustered-lighting graphics fixture

Run only after shader registration and a Vulkan build are present. Capture the
same deterministic scene in native and clustered mode with `r_dynamic 1`,
`r_gpulightmapupdate 1`, and `r_rtshadows 0`.

| Case | Required witness |
| --- | --- |
| Stereo asymmetric frusta | Both-eye, underwater scene clips reconstruct matching moving-brush lighting. |
| Geometry | Static, translating, rotating, and non-instanced brushes retain native light position and KEX/cone falloff at 0 and 64 lights. |
| Raster paths | Cutout derivatives, WBOIT, MBOIT, MSAA, fullbrights, and surface-dither intensities 0/low/medium/high preserve alpha and eye-stable UV noise. |
| Foveation | VRS and fragment-density tile edges retain every native light witness. |
| Mode changes | Native→clustered→native forces an atlas rebuild for hidden and moved brushes, with no old baked contribution or double lighting. |
| Compatibility | `r_rtshadows > 0`, `r_gpulightmapupdate 0`, and `r_dynamic 0` remain native; tasks-on and tasks-off submit the same frame-slot result. |

The CPU geometry fixture is an independent numerical reference. After registration,
validate the actual `cluster_lights.comp` dispatch/mapped masks with reversed-Z,
asymmetric/canted translated-eye matrices from that fixture. Include its central
far-plane interior light, off-axis equal-plane-depth witness, depth below nominal
near, padded edges, invalid padded ray denominator, and tail depths exceeding 1M
with 0/1/31/32/33/63/64 active lights. Inspect both eye masks, not just image output.
Reflect binding-2 offsets against the plan ABI table before uploading these cases.

For native/cluster comparisons, capture 8-bit and 10-bit atlas modes with one native
and one KEX light, including `SURF_PLANEBACK`, rotated brushes, and an eye on either
side of a plane. Confirm 2x dynamic units in both formats and outward KEX Lambert
orientation. Toggle modes while a hidden brush moves with zero atlas light counts;
check both forced static clearing and native re-entry after lights resume. Read
status during task evaluation and verify it uses the completed renderer decision;
no menu cvar changes should be required for fallback.
