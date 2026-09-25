# Desktop vkQuake parity boundary

Desktop play remains vkQuake's ordinary movement, viewmodel and graphics path.
The shared QSS-M-style networking, weapon wheel and expanded co-op options are
intentional changes in both modes. OpenXR-only presentation and private VR
commands must not change an ordinary desktop player's QuakeC or physics path.

An Astra read-only source review of the paired weapon, shotgun, SSAO and Gorilla
integration found one concrete desktop risk. In `SV_Physics_Client`, a due
weapon think could change `MOVETYPE_WALK` to `MOVETYPE_TOSS`, `BOUNCE` or `GIB`.
The later Gorilla-era movement redispatch then selected `SV_Physics_Toss`,
which ran `SV_RunThink` again. The pre-Gorilla vkQuake-based branch selected
the movement owner before that think. A QuakeC callback scheduling another due
think could therefore fire twice in one server frame on desktop.

| Decision | Disposition |
| --- | --- |
| Preserve the movement owner selected before weapon think for ordinary clients. | Adopt; this restores the prior desktop dispatch rule without a second physics implementation. |
| Allow the Gorilla VR adapter to reselect after its touch callbacks. | Adopt only for a qualified Gorilla client, with no second due think if the weapon think already ran. |
| Replace desktop movement or duplicate the whole physics path. | Reject; the existing client physics owner remains reusable. |

The review found no other concrete desktop leak in the inspected boundaries:
paired viewmodels require stereo OpenXR; the QBJ3 shotgun spread override
requires an applied private VR pose; desktop SSAO keeps the single-view
projection and descriptors. This is source evidence, not desktop parity
certification. Later consolidated acceptance must compare ordinary desktop
firing and movement, desktop↔OpenXR transitions, and vkQuake SSAO appearance
and quality settings, along with desktop/VR cross-play. Hardware testing is
separate from these software checks.

The same desktop boundary applies to subsequent Dwell and co-op work. Dwell's
paired contact, native trace and weapon-pose hooks require an accepted VR
command and the exact pinned Dwell QuakeC program; ordinary desktop attacks
continue through native vkQuake/QuakeC. `sv_nofriendlyfire` is instead an
intentional shared co-op option: when enabled, its server callback shield must
cover desktop and VR players equally. It defaults off, independently of the
VR contact profile and the classic co-op setting.

Consolidated software acceptance should exercise the same local server with
desktop-only clients, VR-only clients and a desktop/VR pair. Compare desktop
movement, weapon animations and damage, lighting/SSAO and other vkQuake
graphics at each existing quality setting, weapon wheel behavior, predictive
movement, co-op inventory and collision rules, and cross-play effects. Include
the no-friendly-fire option on and off, native and physical attacks, self
damage and delayed projectiles. Any difference outside the deliberate shared
features needs a specific disposition before declaring parity.
