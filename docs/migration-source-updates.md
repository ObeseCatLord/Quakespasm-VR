# Source updates after `1327f795`

This inventory extends the product-source evidence from
`1327f795cc2e3a8e4f7c9d68e31d64383930cc00` through exactly two descendant
commits: `c1b5f2ab0d458b08470d886240faba7801c981ae` and
`2857e8b95b6ad8eeaeabdfdfd79b55e5f5645869`. These rows are pending migration
evidence only. They do not claim that branch 2.0 implements or accepts the
source behavior.

The preservation ledger keeps its 905-path schema and all inherited, donor,
and WIP evidence. Only the seven paths changed by `1327f795..2857e8b9` advance
their `current_master_blob` to the final product pin and set
`new_master_delta=1`:

| Path | `1327f795` blob | `2857e8b9` blob |
| --- | --- | --- |
| `Quake/q_sound.h` | `c3de0c7f57e6ec71df1291935be6912dfe2f6946` | `9ad35c83fa167755e95c877ffb079bf8f2c7d003` |
| `Quake/sbar.c` | `996456febe2aed51016692944fff9116a4220664` | `94f11f9dc15dada535984684b4a3d5a68ebab039` |
| `Quake/snd_dma.c` | `120d8af01406c3bfcc8b03bac8c94e9014247631` | `46cb34a92cdab8899a51a5fd8970e6a6ec0f1723` |
| `Quake/sv_main.c` | `8eec9d310373feb96e59cd517e491a500480d30c` | `73455547b5f770bc9886d297d7471d1a63a50cd6` |
| `Quake/vr.c` | `c494894c556140b5e983bb3e7b555639c4c74316` | `ef4c556fa5e29b2c140cb403525f2966f526ff04` |
| `Quake/vr.h` | `dffcd4c6b10516b10d37b523a409e0a789a16808` | `95d36e5bce34e91b558f76cde30ef5320f105c61` |
| `Quake/vr_weapon_catalog.h` | `0137d0e0bad61832f4f40a7be08b4cbb518db9ad` | `720e44574b4f4fd885a501a0647d8b3bd9ea7581` |

Concrete pending mappings:

- **Spatial ambience/channel allocation** — `c1b5f2ab`,
  `Quake/q_sound.h` blob `9ad35c83fa167755e95c877ffb079bf8f2c7d003`
  line 115 and `Quake/snd_dma.c` blob
  `46cb34a92cdab8899a51a5fd8970e6a6ec0f1723` lines 356, 368, 399 and 506.
  The source classifies incoming audibility and ranks active spatial channels
  before replacement. Route to existing `AUDIO-006` and `AUDIO-011`.
- **Weapon catalog identity/model matching** — `c1b5f2ab`,
  `Quake/vr_weapon_catalog.h` blob
  `720e44574b4f4fd885a501a0647d8b3bd9ea7581` line 62 and `Quake/vr.c` blob
  `ef4c556fa5e29b2c140cb403525f2966f526ff04` lines 1245 and 1260. The source
  treats matching `g_`/`v_` model basenames as one catalog identity. Route to
  existing `WPN-002` and `WPN-003`.
- **VR HUD spacing for AD CSQC layout 4/104** — `c1b5f2ab`, `Quake/sbar.c`
  blob `94f11f9dc15dada535984684b4a3d5a68ebab039` lines 1309, 1320, 1403 and
  1494, plus `Quake/vr.c` blob `ef4c556fa5e29b2c140cb403525f2966f526ff04`
  line 10531 and `Quake/vr.h` blob `95d36e5bce34e91b558f76cde30ef5320f105c61`
  line 142. Route to existing `VR-012` and `MOD-002`.
- **VRIK map-transition stream reset** — `2857e8b9`, `Quake/sv_main.c` blob
  `73455547b5f770bc9886d297d7471d1a63a50cd6` lines 639 and 5295. The source
  clears accepted-pose and recipient relay sequence/generation state when a
  server spawns a new map. Route to existing `AV-001` and `NET-026`.

`migration-interface-index.csv` is intentionally limited to literal cvar and
command declarations, so the new internal helpers/signature changes do not add
interface rows. The minimum update is to move the four existing MAIN command
line pins shifted by these insertions: `+showscores`/`-showscores` to 297/298
and `vr_migrate_movement_defaults`/`netdiag` to 1182/1185. No broad interface
regeneration is required.

## 2.0 reconciliation checkpoint

The four deltas above now have implementation anchors on branch `2.0`:

- `SND_ChannelAudible` and ranked spatial channel selection in
  `Quake/snd_dma.c` (`6092a1eb`).
- `VR_WeaponCatalog_ModelPathsMatch` in `Quake/vr_weapon_catalog.h`, used by
  `Quake/vr_weapon_menu.c`.
- AD CSQC layout 4/104 handling in `Quake/sbar.c`.
- `SV_ResetVRIKMapState` on map spawn in `Quake/sv_main.c`.

These anchors close the source-delta routing gap, not runtime parity. Linux
builds cover the audio channel change; headset/gameplay qualification is left
to the user after implementation.
