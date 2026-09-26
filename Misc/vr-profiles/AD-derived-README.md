# AD-derived VR weapon profiles

The installed `gibtropolis` and `hwjam4` releases each contain 17 root
`progs/v_*.mdl` models that match Arcane Dimensions model bytes. The portable
profiles in the matching subdirectories reuse AD's held scale, held offset,
and muzzle offset for the 15 models with known AD VR calibration. They also
add the six shadow-axe variants and `v_shot3` missing from the installed
profiles. The grappling hook and alternate shotgun have no proven AD VR
calibration and are left alone.

Other game-specific entries keep their existing values. `hwjam4`'s enhanced
viewmodel offsets are retained separately from the classic AD transforms.
Copy the matching `vr_weapons.txt` into the mod directory to install a profile;
the game's own file continues to override built-in defaults and can be edited.
The built-in fallback also covers these exact game directory names when no
profile file is installed.
