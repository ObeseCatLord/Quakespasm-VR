# q30a1024 AD calibration correction

## Evidence and cause

The installed q30 loose root viewmodels are byte-identical to AD's packed
assets, including the shotgun, nailguns, launchers, lightning, plasma,
shadow axe, and `v_shot3`. The legacy 1,533-byte q30 profile instead provides
generic classic triples for the overlapping stock-named models. Schema load
then applies those triples after the automatic AD-root fallback, producing the
incorrect generic scale and offsets.

## Change

q30 remains an automatic AD-root game. During q30 reload, a classic entry is
ignored only when its held scale, held offset, and muzzle offset together
exactly equal the existing generic fallback for a model that also has an AD
fallback. Any changed triple, non-AD model, enhanced data, and other schema
fields remain authored. The on-disk profile is never rewritten.

The unprofiled melee speed default changes from 1.0 to 1.25 m/s. A schema
`melee_speed` value remains authoritative.

## Proof

The reload fixture supplies the complete legacy q30 roster and verifies AD
fallbacks for overlapping models, retained non-AD entries, a changed authored
triple, AD and HWJAM2 behavior, and both default and explicit melee speeds.
