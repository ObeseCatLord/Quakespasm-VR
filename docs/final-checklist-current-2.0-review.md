# Current final-checklist senior disposition

2026-10-01. Local `gpt-6-astra`, explicit `xhigh`; main independently verified
the effective model/effort before and after the review. Input:
[verified brief](final-checklist-current-2.0-brief.md), committed `a09243c3`.
Reviewed committed source through `0f2ed15b` plus the unaccepted seven-file,
846-changed-line C02 draft. This review changes documentation only. No tests,
builds, executable probes, SSH, games, deployment or performance measurements.
Main/master and the user-owned dirty migration-2.0.md remain untouched.

## Exhaustive coverage and conclusion

**C02 / NET-021 is the only established unfinished implementation area.** No
independent missing feature outside it was established. The draft remains
unaccepted and stopped after exceeding its BEFORE800 reopening boundary.
Twenty-one of the22 original implementation findings, plus Q01, have source
integration receipts. All final software/artifact acceptance remains open.

All185 inventory IDs are accounted for: BASE-001–003; VR-001–016;
WPN-001–012; COOP-001–013; MOVE-001–012; AV-001–009; FBT-001–005;
MOD-001–014; UI-001–006; AUDIO-001–011; XR-001–012; PLAT-001–008;
PERF-F001–003; NET-001–029; PERF-001–023; ASSET-001–009.
**Exact unreviewed IDs: none.** This is exhaustive reconciliation against the
prior detailed source audits and current receipts, with independent checks of
challenged consumers. It is not a fresh line-by-line review of every unchanged
implementation or executable certification. Supplemental QC, command/settings,
history and preservation inventories retain their explicit dispositions.

Astra checked the recent C07 desktop/VR weapon and beam/HUD consumers, C14
target/source separation and paired raster/TLAS equipment, C19 original receipts/
package provenance/loader contexts, and Q01 per-eye category recording. No new
implementation gap was established in those closures. Main additionally read
R_AliasDrawModelMatrix, the post-trace beam correction, paired-equipment validation
and native attachment reuse, and exact deb provenance verification. Their
rendered/installed acceptance remains pending.

## Findings, independent main checks and dispositions

| Senior recommendation | Main disposition / source evidence |
| --- | --- |
| Canonical native name/colors and ordered userinfo overlays are unfinished | Adopted. Main read sv_main.c:5490, cl_parse.c:3596 and common.c:984: full public raw userinfo is followed by native name, topcolor and bottomcolor replacements; Info_SetKey removes an existing key before possible insertion refusal. The draft has no simulation of those ordered intermediate stores. Finish one ephemeral canonical projection and actual overlay preflight, preserving representable custom fields; incompatible complete units fail visibly at the native recipient sender. |
| Pending initial reply does not cover prespawn/begin pressure | Adopted, newly explicit. Main read cl_main.c:1512/1572/1607 and host.c:1193: the name reply retires its intent after enqueue, while prespawn and begin write without remaining-room admission. A full blocked reliable buffer can reach native SZ_GetSpace overflow. Preserve loading order and name-before-prespawn using existing pending reply/sendprespawn owners, with no new queue. |
| Refused live seta can still change flags | Adopted, newly explicit. Main read cvar.c:155 and :491: Cvar_SetQuick can refuse, but its console wrapper subsequently adds CVAR_ARCHIVE/CVAR_SETA. Include the wrapper in the no-effects refusal guarantee; retain successful native flag/default/VM/store/emission behavior. |
| Joining/reused occupation lacks notification to existing recipients | Adopted. Main read SV_ConnectClient:3821 and the complete C02 server diff: slot reset and unconnected name have no metadata dirty notification. Later name commands are insufficient when QC intercepts them. Mark occupation at the native owner; retain retirement after disconnect QC and native frags. |
| Companion bytes must participate in complete preflight | Adopted with corrected severity. Main read sv_main.c:5393: binary name/colors have no preceding explicit capacity check. This is an incomplete whole-unit contract. Current8192-byte stores bound reconstruction below the64000-byte scratch, so no reachable scratch-overflow reproduction is established from that omission alone. Include opcode/NUL/slot/companions, actual recipient limits and safe permanent-failure/drop handling. |
| Reader declaration must be usable independently of PREDINFO | Adopted with corrected framing. Main read protocol.h:96/475 and sv_main.c:3418/5316: ordinary public and private0xe9 profiles both include PREDINFO, so no default-profile exclusion is established. The explicit QSMI reader declaration alone is nevertheless ignored by the draft gate. Honor that declaration independently, retaining PREDINFO for conservative legacy metadata eligibility; do not conflate reader size with prediction or reverse capacity. |
| Full current-store live publication can replace a separate incremental fast path | Adapted into the completion design. Retain dirty full obligations for live server/cvar changes, provided the exact projection and complete-envelope guarantees are satisfied. A full current projection naturally removes deleted/newly unrepresentable public fields. Keep compatible empty-full plus incremental reconstruction where full-token limits require it. No surviving consumer examined requires a second persistent queue or incremental change owner. Record the updated bounded plan before further coding. |
| Empty receipt is missing a map reset | Rejected as a defect. Main read CL_FreeState:271: native memset clears cl, including serverinfo_received. Explicit attach/disconnect resets also exist. Preserve meaningful lifecycle qualification instead of another reset owner. |
| Re-port source-integrated features or expand scope | Rejected. Existing engine, renderer, VM, movement, audio and resource owners remain the baseline. No new independent feature was found; source-present behavior proceeds to final qualification. |
| Preserve all eight final software/delivery groups | Adopted. The canonical checklist and final-linux-arm-qualification plan remain exhaustive. No final-tree executable acceptance exists. Source receipts, old green runs, registry counts and fixtures that bypass signon phases are insufficient. |

## Final remaining implementation subchecklist

These are coupled obligations of **one** remaining feature area, not new
features. Existing accepted reader/declaration receipts remain accepted; the
oversized draft does not close any remaining source-integration checkbox.

- [ ] Construct authoritative current userinfo and simulate native name/topcolor/
  bottomcolor replacements in order, including quoted binary names, intermediate
  stores, inactive slots and representable custom data.
- [ ] Preflight complete full or compatible reset-plus-incremental bundles against
  individual reader tokens/text, aggregate recipient capacity and all binary
  companions; preserve temporary backpressure and bounded permanent refusal/drop.
- [ ] Honor explicit directional reader support independently of PREDINFO while
  retaining the conservative legacy path and separate reverse limits.
- [ ] Use shared public projection for full publication and compatible incremental
  reconstruction: private underscore excluded, star retained, quoted LF preserved,
  invalid replacements removed, actual committed values used after native mutation.
- [ ] Deliver initial/empty serverinfo before signon2 and all current slots before
  signon3 through native reliable retry without skipping loading or DONE/FLUSH rules.
- [ ] Preserve dirty obligations across mid-signon changes, maps, spawn clears and
  fastload; notify occupation/retirement/reuse after the appropriate native QC
  callbacks, preserving ordinary names/colors/frags.
- [ ] Finish/source-review the single persistent client allocation and selected
  NQ/Fitz/RMQ/FTE logical envelopes, with native connection/map/disconnect resets.
- [ ] Preserve valid empty-full receipt and updates-only initialization at actual
  consumers and native cleanup.
- [ ] Preflight/retry all initial name, prespawn, color/userinfo/spawn and begin
  obligations without partial framing or overflow under blocked reliable sends.
- [ ] Complete live cvar/name/color/legacy-color/setinfo admission through every
  existing wrapper, including seta flags: prospective committed values, complete
  synchronous sequence fit, callback refusal, deletion and unchanged/retry behavior.
- [ ] Source-review all coupled lifecycle/privacy/capacity consumers and integrate
  only the completed patch, after a committed estimate/design reopening. Keep
  current native owners; no cursor/snapshot queue or new protocol.

## Final qualification and delivery subchecklist

Run only after all required implementation is finished. Detailed actual-owner
cases and limits remain in
[the final qualification plan](final-linux-arm-qualification-2.0-plan.md).

- [ ] Build complete Linux x86-64 client/dedicated and isolated native Linux ARM64
  client from the same immutable source, with OpenXR, shaders, codecs, CURL and
  Steam Audio. Preserve Foundry's deployed server/game/assets.
- [ ] Qualify native desktop campaigns/mission packs, controls, graphics/AO,
  built-in demos, configuration ordering, transitions and teardown.
- [ ] Qualify actual two-eye rendering/culling, materials/transparency/water,
  MSAA/post-effects/AO, HUD/menu/wheel, avatars/equipment/shadows, session lifecycle
  and optional gaze/foveation paths. Runtime borrowed-image assumptions remain
  explicit target limits; software tests do not certify headset contracts.
- [ ] Qualify real software public/private desktop/VR networking: signon, sender/
  parser, prediction/ACK/loss/reordering, metadata/slot reuse, voice, reconnect,
  single calibration, paired ranged inputs and gesture-only melee.
- [ ] Qualify loaded QC and actual consumers/resources, co-op policies/callbacks/
  respawn, saves/hubs and cleanup; literal registry coverage is insufficient.
- [ ] Qualify supported and large-map load/render/exit, including Mjolnir mj4m1,
  requested jumbo/BSP2/no-VIS cases and ordinary mod content, without heapsize
  workarounds. No benchmark or measured speedup gate.
- [ ] Qualify relocated Linux/ARM artifacts, both OpenXR loader contexts,
  dependency architecture/ABI, notices/source completeness and negative cases;
  rehearse a substantive upstream vkQuake merge in a disposable checkout and
  document actual conflicts/adapters.
- [ ] Fix required software findings, record exact revision/evidence/limits and
  obtain the final local Astra integration review before claiming completion.

## Scope and next action

No genuinely human decision blocks the surviving scope. Reopen the C02 bounded
plan with the adopted corrections before further production edits. Then finish
implementation, run the one consolidated final pass, fix actual failures and
review the resulting evidence. Newly discovered defects attach to existing
feature owners; they do not silently enlarge the feature list.

Windows builds and user live headset/gaze/listening/multiplayer/performance work
remain deferred. Preserve all exclusions: quad views, skyrooms, Gorilla/hand-swim
propulsion, instant stop, contact melee/parry/hybrids, revival, Mjolnir dual-state
weapons, imagedump, VR/additional demos, legacy setting/MP-offset aliases and
general incompatible-device-loss reconstruction. AV-009 remains reference
research; eleven useful-addition candidates remain optional. Native desktop,
ordinary swimming/ladders, gesture melee and one solo/MP calibration remain required.

Reference-path correction: the pinned QSS-M checkout is the actual sibling
`QSS-M`, rather than the brief's directory spelling. The source revision and
read-only policy are unchanged. Stock reverse userinfo's1024-byte store remains
an external limit; arbitrary stock large-field parity is not promised.
