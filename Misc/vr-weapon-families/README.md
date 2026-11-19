# VR weapon family generator

Rebuilds `Quake/vr_weapon_families.h`, the geometry-keyed VR weapon
calibration table, from an installed game tree. Game files are only read; no
game data is stored here, only coordinates in each reference model's space.

- `calibrations.py`: existing tuned presets re-expressed as families, and the
  landmark picks (grip, scale, barrel tip) for meshes without calibration.
- `quakefs.py`: PAK/MDL reading and the signature shared with the engine.
- `generate.py`: writes the header (`--check` verifies it is current).

See `docs/vr-weapon-families.md` for identity, precedence and coverage.
