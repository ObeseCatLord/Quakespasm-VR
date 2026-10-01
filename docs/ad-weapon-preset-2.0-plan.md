# Reusable AD weapon calibration preset

2026-09-30. Before-code design; no tests/builds until the full migration
implementation finishes. Extends WPN-005/007 and the existing preset adapter.

## Verified review brief

Solo-maintained native engine; reuse the existing selector and calibration
owner. Main verified these source facts before requesting design advice:

| Fact | Evidence / status |
| --- | --- |
| Five private selector values, native callback range and matching labels | [verified: calibration.c:64–75,2831–2857] |
| Existing Weapon Setup row permits0–4 | [verified: menu.c:2831] |
| Existing15-row AD schema data and context branches | [verified: calibration.c:218–247,2767–2814] |
| Reload installs builtins before authored schema | [verified: calibration.c:2907–2944] |
| Actual shared classic/enhanced lookup and draw scale consumers | [verified: calibration.c:3010–3087; r_alias.c:895–981] |
| Arbitrary renamed mod's AD mesh identity / automatic classification | [unknown; no general detector is proposed] |
| Current software/menu/rendered behavior | [unverified; checks deferred by user] |

Review write ownership is none. Current targets Windows/Linux/LinuxARM retain
native desktop/OpenXR; no platform-specific preset logic is needed. Main has
an independent roomscale lifecycle audit running, outside these files/functions.

Open decisions, in leverage order: explicit selection versus asset detection
(lean existing explicit selector; reject a guessed family rule); preserve fixed
context precedence versus override every context (lean preserve, existing
selector already contextual); reuse the AD schema table versus synthesize
different muzzle provenance (lean direct existing table; no evidence requiring
new values). Selector/precedence are one policy decision; merging/simplifying
is encouraged. Depth budget one bounded review, max1000 words; do not re-review
OpenXR, graphics algorithms, locomotion, prediction, networking, avatars or the
whole feature inventory. Verify before critique and report needed human choices
only when evidence cannot resolve them. Do not spawn additional subagents.

## User behavior and verified boundary

The user requires AD-based mods, including q30a1024, to use appropriate weapon
scale/offsets, and prefers generic features over additional mod-name cases.
Existing calibration.c contains the copied15-row AD table, including shadow
axes, Widowmaker and ordinary guns/plasma. BuildPreset installs it for ad and
the inherited additional-root contexts q30a1024/gibtropolis/hwjam4. Native
LookupHeld/LookupMuzzle and r_alias.c use the shared per-weapon values in both
solo and multiplayer; classic versus enhanced meshes remain distinct.

However, the five-choice Weapon Preset selector has no AD choice. A mod with
AD meshes under another folder receives generic defaults unless it supplies
authored calibration. This is a calibration-selection gap, not evidence that
every same-named gun in an unknown mod has AD geometry. Preserve existing
authored schema precedence on game reload and the existing live preset rule
(explicit live selection replaces targeted classic fields, preserving other
fields). Do not claim automatic family recognition for arbitrary renamed mods.

Add a sixth **Arcane Dimensions** contextual preference to the same archived selector and
Weapon Setup row. A user can explicitly apply the copied AD table to an
otherwise generic AD-based mod. Known AD contexts retain their existing
baseline; selecting AD on an additional AD-root context must also retain that
baseline. Fixed contextual profiles and enhanced-directory behavior retain
their existing precedence. Original choices0–4/default remain unchanged.

## Architecture comparison and scope

Minimal adapter: append enumAD beforeCOUNT, append the display label, increase
the existing menu row max to5. Reuse PresetAppendSchema/ApplySchemaMode and the
existing full-batch preflight/callback/accepted-selector flow. Handle AD in
PresetAppendGeneric through PresetAppendSchema; its caller remains after the
enhanced-directory forcing rule. Extend the existing live-only BlockQuake tail
exemption to AD, retaining Copper/conditional LimJam fallback composition on
reload. In the additional_ad_root branch, treat selectedAD like
Vanilla so generic root overrides do not replace it. No additional schemas,
calibration state, mod lists, model resource changes or command aliases.

Alternative automatic asset/family classification would need evidence of mesh
identity and lifecycle ownership: the same model paths are used by other mods
and replacements. A new hash catalogue or guessed folder heuristic is not
required to make the existing AD table reusable. A separate AD offset store
would duplicate current finite validation, persistence, format and networking
policy. Keep the smallest explicit selector extension; automatic detection is
not part of this slice.

Write set Quake/vr_weapon_calibration.c (enum/name/preset composition) and Quake/menu.c
(existing row max) only; expected fewer than20 net production lines. No changes
to authored-table values, header/API, renderer, muzzle transforms, wheel data,
protocol, VM, files or installed q30a1024 settings. Reopen if wider work needed.

## Deferred qualification and review decisions

At final Linux/ARM software qualification, use actual menu/console callback
selection and game reload: unknown/generic context with AD selection, knownAD
and additional-root contexts, original choices0–4, fixed contexts and enhanced
directory precedence. Observe current shared lookups and rendered transforms,
classic/enhanced separation, finite integral range0–5, invalid value recovery,
full-capacity refusal without partial changes, adjustment cancellation and
targeted/untargeted independent fields. Verify authored schema reload
precedence and saved selector. Do not alter deployed mod files. User headset
calibration remains deferred; no general mesh or family compatibility claim.

Before implementation, requested local Astra source advice should challenge
the selector-versus-detection decision, contextual precedence, classic implicit
muzzle provenance and count/name/menu consistency. Main will inspect the whole
actual patch and final bounded review; this plan does not close broader
WPN-005 or the full migration by itself.

## Adopted before-code source disposition

Requested local Astra xhigh verified the actual adapter and contextual owners.
Main spot-checked PresetAppendSchema (clears derived muzzle flags), generic
composition and the live/reload fallback tail before adopting its correction.
Effective runtime routing metadata was unavailable; this is requested-Astra
source advice, not certified skill/final-goal signoff.

| Recommendation | Main disposition |
| --- | --- |
| Explicit selector is sufficient for the manual-selection gap | Adopt; no automatic unknown-folder family recognition claim. |
| Generic AD early return skips reload fallbacks | Adopt correction: handle AD inside PresetAppendGeneric, preserve tail on reload and skip it for explicit live AD selection. |
| Additional AD roots must not append generic stock overrides | Adopt: selected AD has the same short-circuit as Vanilla in that branch. |
| AD table source flags are not final muzzle provenance | Adopt: reuse PresetAppendSchema and private preset mode; do not directly apply the original table. |
| Existing fixed contexts can override the displayed requested preference | Retain existing contextual policy and describe its limitation; no new policy/UI state. |
| Archived selection does not outrank authored schema on reload | Retain native reload order and live-callback distinction; add that case to final qualification. |
| Append enum, name and menu bound together | Adopt, preserving0–4/default and existing range validation/refusal restoration. |

No human decision is needed for this bounded contextual-preference contract.
Review and main source inspection performed no execution or tests/builds.
