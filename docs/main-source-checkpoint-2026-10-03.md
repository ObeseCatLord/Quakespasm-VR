# Main source checkpoint, 2026-10-03

Read-only reference: `master` at `bd923e924410fd210248c0cd9e4d966e62eaf9e3`.
Implementation remains on `2.0`; no main source or settings were edited.

The focused audit compared the September main commits against current owners.
It found one missing recent implementation family, now ported: Snack weapon
roster/ownership/ammo gates and held/muzzle/source defaults, plus HWJAM2 AD
profile reuse (`bd923e92`). See `snack-hwjam2-source-checkpoint-plan.md`.

| Main change | Current 2.0 ownership / disposition |
| --- | --- |
| `7acafa8b` trailing whitespace in model names | `gl_model.c`: exact filename wins; trim only after missing exact lookup. Already implemented; added history row. |
| `51b452c0` native wheel identities | `vr_weapon_catalog.h`, `vr_weapon_menu.c`: shared native identities and roster consumers. |
| `eb5e048d` anisotropy exposure/clamp | `gl_vidsdl.c`, `gl_rmisc.c`: device-limit clamp. Frame filtering appearance is a separate fix/validation. |
| `cefb937d` stale models on mod switch | `common.c`, `gl_rmisc.c`: existing invalidation. |
| `c1b5f2ab` AD ambience, identities, HUD spacing | `snd_dma.c`, `sbar.c`, `gl_screen.c`: adapted audio priorities and layouts. |
| `2857e8b9` map-transition VRIK reset | `sv_main.c`: pose stream reset. |
| `1327f795` QuakeC freeze | `pmove.c`: remote VR roomscale respects freeze. |
| `22a7fce0` instant-teleport source latch | `world.c`, `pr_edict.c`, `sv_phys.c`: exact source occupancy latch and cleanup; retained teleport correctness, independent of excluded instant-stop locomotion. |

Older September history remains accounted for in the migration ledger. This
checkpoint is a source comparison, not a new claim that every historical
feature has been replayed on a headset. Frame model switching, popup depth,
audio/input interruptions, and rolled shot spread have their own verification.
