# q30a1024 VR weapon profile

`vr_weapons.txt` is the installed q30a1024 profile with held and muzzle
transforms corrected for the local AD viewmodels. Fifteen `progs/v_*.mdl`
files in the installed q30a1024 release matched the corresponding Arcane
Dimensions model bytes. The profile therefore reuses AD's existing held scale,
held offset and muzzle offset for those models. It adds the six shadow-axe
variants and `v_shot3` that were missing from the earlier q30a1024 profile.

The other entries, including `v_axe`, expansion weapons and the mod's
`v_ghook`/`v_zershot` models, retain their previous calibration or have no
new default. Copy `vr_weapons.txt` to the q30a1024 game directory to use it.
The game's own file remains the authoritative place for later user edits.
