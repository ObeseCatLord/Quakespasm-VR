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

The first implementation adds the explicit physical outcome adapter and a
native-program fixture. It does not advertise QBJ3 melee or the berserk pair.
Activation still requires client held transforms and contact production,
server admission and gesture processing, native fallback poses and suppression,
then an ordinary-physics proof from accepted contact to native damage with
desktop behavior retained. Headset alignment/feel and Windows/ARM checks are
deferred according to the user's testing scope.

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

## Staged implementation and software evidence

`SV_VRQBJ3PhysicalMeleeOutcome` now reuses the exact installed program pin and
the explicit native leaves. Initial outcomes validate native readiness, apply
the donor's 0.8-second wrench or 0.5-second berserk recovery, and preserve the
sound prelude. A follow-up revalidates the selected subtype without restarting
that prelude. Its future contact caller must own per-hand authorization,
deadline expiry, victim deduplication, the two-victim cap and termination after
a whiff. The callback alone does not enforce those stroke rules.

The paired presentation implementation retains only two eight-vertex palm
centroids per source pose (2424 bytes for the pinned 101-pose model), frees
them with the model, and uses `R_SetupAliasFrame` and the shared alias matrix
to remove the interpolated palm translation. It preserves native animation
when immersive contact is unavailable and selects frame zero for immersive
presentation. Source MD3/MD5 replacement selection falls back from this pinned
pair. No server capability has been enabled by these changes.

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

## Final Astra source review

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

## Next connected slice

1. Extend the existing bilateral producer to QBJ3 grip-to-knuckle contacts,
   preserving anatomical identity and the berserk command bit. Use raw point
   velocity for effort and the common held transform for the contact segment.
2. Add the generated dominant wrench to the existing single-hand profile
   owner. The donor pins ready pose 10, edge vertices 320/358, raw grip centroid
   `(158.428571, 151, 148.714286)`, authored left-hand geometry and a 70-degree
   controller-roll correction. Reuse the generated model and calibration
   source name instead of treating it as an ordinary stock axe.
3. Add first/follow-up authorization to the existing per-hand server contact
   state, with its original deadline and two distinct victim slots. Reuse
   reversal handling, consume rejected terminal gestures, and revalidate after
   every native leaf before another outcome or hand can run. Preserve hit
   obstructions and do not temporarily disable victim solidity.
4. Extend the existing native pose scope for QBJ3's four pre-increment striking
   frames, retaining the native fan attack and ordinary desktop behavior.
   Connect suppression to qualified contact ownership, independent of instant
   cooldown readiness.
5. Verify ordinary command/physics ownership, two-target behavior and animated
   versus immersive presentation before enabling the server offers. Remove
   the staged-entry retention annotation once the real caller references the
   outcome function. Direct-leaf fixture success is not this activation proof.
