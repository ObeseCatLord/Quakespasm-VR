# Native demo read and seek boundaries

2026-09-30. Before-code BASE-001 plan. Retain vkQuake's desktop/dedicated,
campaign and demo owners. No builds, tests, compiler checks, fixtures, runtime
probes or benchmarks until the full implementation goal is finished.

## Verified reference and two corrections

Requested local Astra/max source advice inspected actual production startup,
frame, demo and shutdown paths against pinned vkQuake4bc898f2 and primary
51b452c0. Effective agent settings are unexposed; this is source advice, not
formal review certification or runtime qualification. Main spot-checked the
load-bearing demo read/seek and client/dedicated startup gates.

1. `cl_demo.c:151` checks only an upper bound on decoded `net_message.cursize`.
   `common.h:127` defines this field as signed int. A negative length can become
   a huge unsigned `fread` size before short-read handling. Both reference forks
   retain the same defect. Add the lower-bound check before payload reading,
   preserving native valid framing, short-read teardown and existing invalid-
   length error ownership. Retain MAX_MSGLEN and zero-length behavior; no file
   service or message parser replacement.
2. `CL_Seek_f` uses `offset > 0` to sign the seconds in mm:ss. Zero minutes
   therefore subtract seconds, so at time120, `seek 0:30` targets90 instead of
   absolute30, and `seek +0:30` targets90 instead of relative150. This arithmetic
   is identical in vkQuake; primary has no equivalent seek owner. Select the
   mm:ss seconds sign from the parsed minute sign, preserving negative zero,
   and retain explicit +/- relative intent. The minimal `signbit` adaptation
   avoids another tokenizer and preserves existing whitespace parsing and
   ordinary nonzero-minute arithmetic. Do not change native pause/unpause,
   rewind/replay, VRIK reset, effects cleanup, seek completion or command routing.

Native startup/tasks/filesystem/QC/server and demo recording/playback/signons
remain reusable. Dedicated mode bypasses client video/input/OpenXR/sound/voice
initialization through existing gates. That is source evidence only; it does
not prove mission-pack gameplay or launch without runtime libraries.

## Ownership and proof boundary

One coding worker owns only the length guard/diagnostic and CL_Seek_f numeric
sign/relative expressions in `Quake/cl_demo.c`, target <=12 net lines. Main
owns docs and integration; no second writer or demo engine. Reopen before
broader parsing/state changes. Main reviews the complete scoped diff against
the native owners; requested local Astra reviews the two corrected boundaries.

Final Linux/ARM checks must cover negative, oversized, truncated and ordinary
demo records; record/play/stop/pause; absolute0:30/1:30 and relative+0:30/-0:30,
+1:30/-1:30/+0/-0 at nonzero playback time; backward replay and VRIK reset;
native desktop and dedicated startup/shutdown and id1/Hipnotic/Rogue. Source
checks alone do not qualify these behaviors. Windows and user hardware trials
remain deferred, and revival remains excluded.

## Source integration disposition

The worker changed four lines in the existing owner (four additions/four
deletions, zero net). Main reviewed the complete diff and native read/seek
control flow. Requested local Astra source advice closed both findings with
no blocker; main retains integration responsibility.

| Recommendation | Main disposition |
| --- | --- |
| Reject the signed lower bound before payload reading | Adopted. Both negative and oversized lengths use the existing Sys_Error owner with an accurate range diagnostic; ordinary framing and short-read/zero behavior remain. |
| Preserve zero-minute sign and explicit relative intent | Adopted. signbit selects mm:ss seconds direction, and explicit leading minus remains relative. At time120, source arithmetic gives0:30→30, +0:30→150 and -0:30→90. No alternate parser or replay owner. |
| Rewrite native demo/startup machinery | Rejected. Actual retained initialization, dedicated client-subsystem gates, recording/signon/read/stop/pause/replay and shutdown owners remain suitable. |
| Source advice closes runtime acceptance | Rejected. Effective reviewer metadata is unexposed and no executable checks were run. Full campaign/base/demo Linux/ARM qualification remains pending. |

Scoped whitespace checks pass. The source comparison verifies production
Meson inclusion, native Host_Init/_Host_Frame/Host_Shutdown, dedicated gating
before client video/input/OpenXR/sound/voice initialization, explicit desktop
OpenXR bootstrap fallback, and native demo registrations/read/write/seek owners.
It does not qualify packet payload semantics, mission-pack gameplay or actual
runtime-library availability.
