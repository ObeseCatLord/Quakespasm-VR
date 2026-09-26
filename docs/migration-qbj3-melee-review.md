# QBJ3 wrench and berserk melee integration

The migration retains QBJ3's native QuakeC damage, hit effects, cooldowns and
weapon selection. The inherited OpenVR fork already supplies the physical
contact-to-QC adapter; reuse that behavior at the existing vkQuake command,
pose and contact owners. No new transport or attack scheduler is needed.

## Reuse boundaries

| Existing component | Required adaptation |
| --- | --- |
| Source-pinned model splitter and pair recipes | Retain generated wrench/fist geometry and provenance. |
| Alias renderer and paired view entities | Apply the donor's fist orientation and left grip residual correction in the shared matrix; use the immersive ready pose without changing the canonical QC animation. |
| Bilateral contact fields and pending pair identity | Carry controller-to-knuckle segments for fists; retain Dwell's distinct cutting-edge geometry. |
| Server command queue and contact continuity | Add selected QBJ3 subtypes at the existing owner; preserve freshness, reach, resets and native-trigger arbitration. |
| Native `hitwrench` and `hit_berserker_punch` leaves | Supply accepted contact globals and the original entity/vector/vector arguments. Do not invoke the native fan acquisition or schedule another animation. |
| Existing friendly-fire scope and QC context restoration | Preserve server co-op policy and callback lifetime without another persistent context. |

The installed program is 905470 bytes, SHA-256
`de2c6a60df24f5ce0c3fc41b0fd6309105a0ea7ae895dfb4a6867950b9b90e34`.
The twin-nailgun adapter already identifies these exact bytes. The inherited
melee descriptors also list another revision; no second revision's byte image
has been verified in this slice, so support for it remains outstanding.

An original-index bytecode read using the donor's
`tools/immersive_melee/qbj3_enyo_proof/extract.py` confirms that
`weaponanim_berserk_loop` (#565, first statement 18858) calls
`W_Fire_Berserker_Multi` (#571) at statement 18870 before advancing the frame.
That root calls `makevectors` at 19019 and performs its five native helper
acquisitions at 19033, 19049, 19080, 19113 and 19146. This agrees with the
installed source's hit frames 14/34/54/64 and supports reusing the donor's
native hand selection. It does not itself prove the integrated attack path.

## Completion boundary

The client and server implementations are now connected for the pinned
installed QBJ3 revision. The server advertises the QBJ3 melee profile under
the existing immersive-melee policy, and native berserk pairs independently
of that policy. The generated dominant wrench, bilateral fist producer,
two-target contact owner and qualified native-trigger suppression are wired.

The current Linux debug build and focused numeric/input regressions pass.
The accepted-contact runtime fixture, scheduled fist fixture and strengthened
native-leaf cases remain unrun because ptrace is unavailable. Ordinary
command/physics coverage and render-to-contact agreement therefore remain
unproven; this is implementation progress, not release qualification or
completion of WPN-012/MOVE-005. Headset alignment/feel and Windows/ARM checks
are deferred according to the user's testing scope.

The Astra dispositions below document earlier bounded slices. The current
combined integration was reviewed locally by the main agent while subagents
were unavailable; it has no completed independent senior review.

## Astra design review disposition

| Recommendation | Disposition |
| --- | --- |
| Preserve two distinct targets per wrench/fist stroke. | **Adopted.** The native outcome API must distinguish initial and authorized follow-up hits. Extend the existing per-hand contact owner with its original recovery deadline and up to two hit identities during integration; do not restart the cooldown or prelude for the second target. |
| Copy Dwell's single-outcome consumed-stroke policy unchanged. | **Rejected.** The donor explicitly limits Dwell to one target but permits two for QBJ3. Reuse arc, reversal, queue and reset machinery with QBJ3's existing authorization rules. A previous victim remains an obstruction rather than becoming transparent to later sweeps. |
| Keep native paired presentation when immersive melee is disabled. | **Adopted.** Pair support, physical-contact authorization and frozen presentation are separate decisions. Native mode retains source animation with the donor's interpolated eight-palm-centroid translation removal; immersive mode uses frame zero. |
| Share fist transforms with the existing alias matrix. | **Adopted.** Add the donor's correction matrices and subtract the scaled grip-Y sum after left residual reflection. Keep Dwell's controller-centered grips separate. Use the same matrix for rendered geometry and knuckle anchors. |
| Reuse native scheduled-Think hand selection. | **Adopted.** Frames 14/64 select the anatomical right hand and 34/54 the left before QC advances the frame. The existing pose scope and explicit ownership command suffice; no additional makevectors interception is justified. |
| Treat collision retraction as physical swing effort. | **Rejected.** The donor derives effort from raw tracked point motion before presentation retraction. Preserve this distinction when sharing the bilateral producer. |

The main review verified the donor's two-target limit, original-deadline
authorization, reversal rearming and separate animated/immersive presentation.
Acceptance must include two eligible victims without repeated or third-target
damage, no penetration through the first obstruction, cooldown-rejected
gestures, simultaneous fists, and callback relocation/reset. It must also
cover immersive on/off transitions, all four native striking frames,
render/contact agreement and ordinary server physics with desktop fallback.
The design review does not certify an unfinished implementation.

## Initial implementation and historical software evidence

`SV_VRDirectMeleeOutcome` now reuses the exact installed program pin and
the explicit native leaves. Initial outcomes validate native readiness, apply
the donor's 0.8-second wrench or 0.5-second berserk recovery, and preserve the
sound prelude. A follow-up revalidates the selected subtype without restarting
that prelude. The connected contact caller owns per-hand authorization,
deadline expiry, victim deduplication, the two-victim cap and termination after
a whiff. The callback alone does not enforce those stroke rules.

The paired presentation implementation retains only two eight-vertex palm
centroids per source pose (2424 bytes for the pinned 101-pose model), frees
them with the model, and uses `R_SetupAliasFrame` and the shared alias matrix
to remove the interpolated palm translation. It preserves native animation
when immersive contact is unavailable and selects frame zero for immersive
presentation. Source MD3/MD5 replacement selection falls back from this pinned
pair. That initial commit did not enable a server capability; current offers
are described above.

The Linux debug build passes. Before the final cleanup correction below, the
direct headless fixture `tests/vr_qbj3_melee_outcome.sh` observed native
60-damage wrench and 480-damage
berserk hits, both recovery intervals, whiffs, second-victim leaf calls without
cooldown/prelude restart, changed subtype rejection, pending native-think
exclusion, float-clock expiry and desktop-command rejection. This is native
callback evidence, not a complete contact-to-damage or visual proof.

The older twin-nailgun runner now defaults to `build-debug/vkquake`, with an
optional `QSVR_BINARY` override. Running it against the current binary exposed
obsolete two-argument calls to `SV_RunPrivateVRWeaponThink`; those calls now
pass the owning command explicitly. Its rerun was blocked by the session's
new ptrace restriction, so no new passing result is claimed for that fixture.

## Earlier Astra source review

| Finding | Disposition |
| --- | --- |
| Borrowed hand angles survive a callback that kills its attacker. | **Fixed.** Cleanup now checks the same VM/storage and owned non-free edict independently of health. Death still rejects further attack authorization. The donor restores borrowed angles on death too. |
| Direct damage assertions only require a decrease. | **Strengthened.** Assertions now require the observed native 60/480 damage amounts, including the second target. |
| Duplicate callback/animation architecture. | **Accepted as narrow reuse.** The native leaves, existing contact owner, source-derived centroids and vkQuake interpolation remain the owners; no parallel scheduler or renderer was introduced. |
| Suspected same-frame frozen-policy mismatch. | **Not established.** Pair preparation precedes rendering, and command anchors intentionally exclude animated palm translation. No speculative extra state was added. |

The final corrected source builds successfully. A callback-death regression
now injects death at the native leaf's VM-return boundary and checks angle
restoration plus rejected subsequent authorization. This is explicit fault
injection, not a claim that a native self-kill was reproduced. Its embedded
Python syntax and the shell launchers pass syntax checks. The new death case,
strengthened exact-damage assertions and corrected twin-nailgun fixture have
not been rerun because ptrace is unavailable in the current sandbox. Paired
presentation still requires its software integration proof before activation.

## Connected implementation and current solo review

The generated dominant wrench reuses `vr_mdl_split.h`, the existing model
loader and its two-point edge cache. Its pins remain ready pose 10, edge
vertices 320/358 and raw grip centroid `(158.428571, 151, 148.714286)`.
The alias renderer receives a prepared held entity through both ordinary
viewmodel and debug-triangle dispatch. The source model/QC animation remain
canonical, while the physical wrench independently displays its ready pose.
This follows the donor's `native_animation=false` profile; source interpolation
readiness is not an additional admission requirement.

`VR_LocomotionControllerRollViewmodelAngles` ports the donor's 70-degree roll
about the tracked controller forward vector, with opposite sign in the left
hand. It does not roll about the already-pitched model X axis. The shared
alias matrix preserves source scale, replaces source offsets with the centered
grip, retains global model height and mirrors this left-authored mesh for the
right hand. Loaded geometry, ready-pose selection, edge offsets and collision
displacement are prepared before draw tasks; the command producer consumes
that same prepared data. Failed preparation cannot independently authorize a
wrench contact. Point speed is measured before wall retraction.

`SV_VRContactProcessQBJ3Melee` extends the existing per-hand contact state with
an original recovery deadline and two distinct victim numbers. The shared
sweep accepts a remaining-motion cutoff and excludes earlier outcomes while
leaving those entities solid. Stock/Dwell callers retain zero cutoff and no
exclusions. A world hit, whiff or rejected terminal gesture ends the stroke;
an accepted second victim does not replay the native recovery or sound prelude.
The real caller replaces the staged-entry retention annotation.

After every native leaf, including a false result after side effects, the
caller validates VM/program/edict/global storage, player ownership, exact body
origin, subtype, contact cursor, continuity, sample and deadline before writing
stroke state or querying another victim. The outer bilateral loop checks the
same ownership boundary before another hand runs. Existing reset/relocation
owners retire the contact; no new scheduler or trace interception was added.

| Finding from the combined source review | Disposition |
| --- | --- |
| Prepared wrench was never selected for drawing. | Fixed normal and debug-triangle dispatch; reuse ordinary alias lighting, interpolation and winding. |
| Local-X correction diverged with nonzero gun pitch. | Ported tracked-forward rotation and added a production-matrix regression against independent Rodrigues geometry. |
| Source ready/interpolation gate suppressed the frozen physical mesh. | Removed the extra source gate; native attack readiness stays in the server leaf owner. |
| Source held offsets displaced the centered grip. | Match the donor's replacement semantics; numeric checks vary offsets, scale and global height. |
| Render and contact collision offsets differed. | Share the prepared edges and collision displacement; compute effort from raw offsets. |
| State could be written after callback invalidation; the second hand lacked the full QBJ3 check. | Validate immediately before state writes and again at the bilateral boundary. Authored callback fault-injection cases remain unrun. |
| World impact could retain follow-up authorization. | Consume entity-zero impacts immediately, matching the donor. Runtime world-impact coverage remains outstanding. |

Current checks: `ninja -C build-debug`, the wrench transform fixture and the
existing locomotion fixture pass. The stale-input fixture passes under
AddressSanitizer/UndefinedBehaviorSanitizer with leak detection disabled.
These checks do not execute Vulkan submission, installed native QC contacts,
network queue drainage or physical headset tracking. The accepted-contact
fixture adds explicit relocation/reset/death/subtype fault injection and
checks that neither a follow-up victim nor the other hand runs after the
first native leaf invalidates its owner. Its runtime results are pending.

Remaining software acceptance includes real accepted-command/physics damage,
two-target obstruction and world impacts, simultaneous fists, resets and
native-animation/immersive switching, plus generated-wrench render/contact
agreement. The other QBJ3 program revision and broader Alkaline/Enyo behavior
remain separate outstanding requirements.

## Paired input and native pose connection

QBJ3's fist recipe now uses the existing berserk command identity and bilateral
contact finalization. Native paired presentation/commands remain available
without the immersive option; grip-to-knuckle contact production requires the
QBJ3 melee profile and local option. Dwell retains its distinct blade-edge
geometry. Both producers now derive swing effort before applying collision
retraction, matching the donor; the previous Dwell order incorrectly included
the retraction offset in the angular-velocity radius.

The existing native pose scope selects the authored right/left fist for frames
14/64 and 34/54 respectively, clamps the physical muzzle from the body frame,
and supplies `muzzle - view_ofs` to the original QC fan attack. It uses the same
fresh, bounded paired command as other native pairs. The selected QBJ3 subtype
is checked against current model and berserk state, independently of physical
melee policy; Dwell's later strike hook remains separate.

The combined Linux debug build passes. The focused input fixture passes under
AddressSanitizer and UndefinedBehaviorSanitizer with leak detection disabled
for the restricted environment. It checks family/option authorization and
clearing stale bilateral contact together with its pair; it does not prove
the full producer-to-wire-to-damage path. That input/pose commit left server
offers unchanged; the current combined integration enables them as above.

### Astra input/pose review disposition

| Recommendation | Disposition |
| --- | --- |
| Keep native anatomical hand selection inside the existing pose scope. | **Accepted.** The installed bytecode calls its strike before incrementing the frame; frames 14/64 select right and 34/54 left. |
| Reuse scoped restoration rather than add another pose context. | **Accepted.** Existing restoration retains body/angles/basis and explicit QC relocation semantics. Dwell retains its deferred strike hook. |
| Keep native paired input independent of immersive contact authorization. | **Accepted.** Contact requires its matching profile and local option; native pair input does not. |
| Calculate effort before collision retraction. | **Accepted.** Both paired producers now use raw endpoint offsets for angular velocity radius. |
| Check lateral native fan rays as well as the forward ray. | **Accepted for the fixture.** A forward ray alone cannot detect an incorrect roll basis. Runtime execution remains pending. |

Astra found no proven production regression in this bounded integration diff.
The new `tests/vr_qbj3_fist_pose.gdb` is a scheduled native-callback fixture for
all four striking frames, stale-pair fallback, desktop fallback, command
ownership and scope restoration. It is **unrun** under the current ptrace
restriction; source review and syntax checks do not replace runtime evidence.
