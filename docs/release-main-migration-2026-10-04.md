# Maintained branch and release migration

The maintained 2.0 engine retains vkQuake history and integrates official
vkQuake through `749b4fd43fe8b8554b34163dc9469c4a08a4def1` with an ordinary
two-parent merge. See the upstream merge plan and dispositions for the
save, signon, SSAO, menu and VM boundaries that need care on future merges.

`legacy1.0` preserves the prior GitHub default branch at exact revision
`bd923e924410fd210248c0cd9e4d966e62eaf9e3`. The old `master` reference is
also retained. The intended new default `main` points at the qualified
2.0 release; `2.0` remains a matching release-tooling alias. Promotion
must not force-push or modify the historical engine.

The release uses one committed source archive for native Windows x64,
portable Linux x86-64 and portable Linux ARM64 builds. All engine objects
and shaders are rebuilt. Unchanged dependency SDKs may be reused only with
matching source, artifact and license receipts. Host diagnostic binaries
must not be installed or published.

Implementation completes before consolidated behavior acceptance and
release builds. The changed paths require actual native save/load and
blocked-send signon acceptance, shared/subgroup SSAO and eye isolation,
rectangular mip upload and anisotropic GPU readbacks, and the original
Bonk program's physical-contact outcomes. Simulated tracking is software
qualification; headset appearance, swing feel and mobile GPU filtering
remain physical-device checks.

Straight deployment backs up engine executables and installs a matching
isolated runtime while retaining the mod launcher, mods, saves, offsets
and user configuration. Foundry deployment first checks its actual
players/mod/map/skill, preserves a verified progression checkpoint when
players are active, then restarts the matching ARM engine and verifies
the listener, map and co-op autosaving. An empty server is not a saved
player-progression checkpoint.

R2 publication stages matching runtimes, dependency sources, engine
source and license notices without game data. Immutable revision objects
and public hashes are verified before advancing the existing `2.0`
manifest and release pointers, so installed launchers remain compatible.

GitHub's public repository-update API does not expose fork-parent
reassignment. The project description and README identify vkQuake as
the maintained base. Network reparenting to `Novum/vkQuake` requires a
supported GitHub operation; deleting, detaching or recreating this
repository would risk its existing project metadata and is excluded.
An unsent support-request draft is prepared separately.

Implementation assistance: GPT-6.1 Sol at high effort; architectural
review: GPT-6 Astra at xhigh; final integration and release decisions
remain with the main coding agent. Existing component credits and
license grants are retained.
