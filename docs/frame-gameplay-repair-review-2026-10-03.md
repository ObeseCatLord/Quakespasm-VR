# Frame gameplay repair review

Only `2.0` is changed. The source reference remains main/master `bd923e92`;
the focused recent-source checkpoint is `main-source-checkpoint-2026-10-03.md`.
This repair retains vkQuake Vulkan, the tracked pose protocol, the current UI
panel adapter, and the launcher. No GPU reset or runtime/driver configuration
change is part of this work.

## Astra disposition

Reviewer: `gpt-6-astra`, effective `xhigh` verified from this review's own
turn context. Read-only review of the current source and focused fixture.

| Recommendation | Disposition |
| --- | --- |
| Q30 legacy detection must be exact, not save-tolerant | Adopted: entire parsed classic triple must numerically equal the legacy generic defaults; a single changed field preserves the whole triple. |
| Cover partial customization, incomplete triple and unrelated fields | Adopted: regression cases for individual values inside the former tolerance, incomplete entries, enhanced offsets, melee and projectile-source overrides. |
| Keep slower authored melee thresholds usable | Adopted: accumulation/reset speed is `min(0.4, profile.speed)`; default speed becomes 1.25 m/s and minimum arc 6 cm. |
| Console notify needs its own bounded source transform | Adopted: same frozen head pose, physical width independent of eye extent, console glyph aspect, top edge 8 units above the forward anchor. |
| Preserve discrete action safety during analog recovery | Adopted: only left-stick movement uses stick-neutral rearming; button, trigger and snap-turn gates remain. Focus/context/tracking/identity checks remain. |
| Avoid new generic roll admission on borrowed nested pose | Adapted: only the outermost non-paired, non-invalidated same-player scope admits the new raw roll basis. Existing nested pose/source behavior remains. |
| Descriptor refresh needs GPU retirement | Verified existing resource destruction waits for idle on both XR attachment and retirement; retained that boundary. |
| Do not present anisotropy descriptor choice as proven driver fix | Adopted: VR selects existing linear-anisotropic sampler; desktop classic remains unchanged. Visual effectiveness awaits Frame comparison. |
| Model-switch proof must run after real map entry and check rendered transitions | Adopted as proof requirement; loading-time script discarded. Isolated Xvfb attempts failed to provide usable map-entry/render evidence, so neither model-switch nor regular-play crash is claimed fixed. |

## Behavior and remaining uncertainty

The verified repairs cover runtime-owned VR render resolution, projected
centerprint/notify depth, CSQC HUD source width, AD-compatible q30 calibration,
rolled shot spread/source axes, mirror-focus audio muting, movement rearming,
and gesture sensitivity. Each major boundary has a focused plan in `docs/`.

OpenXR session-state and action-sync-result transitions now log changes;
`developer 1` also reveals SDL mirror-focus transitions. Input remains inactive
when runtime focus is absent. No evidence yet establishes what causes the
reported brief black flash or all audio interruptions. Room-worker replacement
was reviewed without finding a demonstrated lifetime race.

The user's AD_start model-switch crash and ad_tears regular-play exit remain
unlocalized. Captured user-session log showed normal client removal/SDL shutdown
and no engine core was found. This does not disprove the report. The automatic
Xvfb tests are not successful headset reproductions: SwiftShader failed in its
driver before map entry, and the NVIDIA/Xvfb attempt waited in rendering with no
valid transition result. Owned diagnostic processes were stopped; the user's
Frame game was left running.

Linux build and isolated fixture results are recorded with the repair artifacts;
native ARM deployment uses the existing matched library package and executable
backups. Device feel, filtering appearance, and the unlocalized crash still need
the user's reproduction; no complete-crash-fix claim is made by this checkpoint.

## Build and deployment checkpoint

Production source checkpoint: `46e132d356520b66e53999f4bc7ff51639c96983`.
Linux debug engine links successfully. ASAN/UBSAN input, calibration-reload and
actual private-pose shot-roll fixtures pass; SDL2/SDL3 audio-focus checks and
OpenXR Vulkan creation checks pass. The input follow-up checks both a held
deflection remaining blocked and recovered movement reaching the command.

Native ARM compilation and installation pass with the existing Steam Audio and
OpenXR dependency package. Changed source hashes are verified against the source
manifest. An initial incremental attempt performed no compilation because the
patch archive carried epoch timestamps; that output was rejected. Source mtimes
were corrected, and the accepted build log includes actual compilation of all
changed compiled owners and engine linking.

Frame installation: `frame-gameplay-46e132d3-20261004-013238/arm`.
Installed engine SHA256:
`57168e29b403f26df53f6448baf179318f010d1067e45066491bd59307c42d7a`.
The engine uses `$ORIGIN/../lib`; its loader check succeeds and imported GLIBC
versions remain at or below 2.39. All 44 existing packaged library files retain
their hashes. The executable-only backup is `executable-backups/20261004-013238`
and contains just the preceding `quakespasm-openvr.bin` executable. The wrapper,
GUI launcher, update helper, launcher settings, mods, saves and configs are
unchanged. The canonical engine link is switched atomically; no running game is
stopped and a new launch is required. No new headset session is claimed tested.
