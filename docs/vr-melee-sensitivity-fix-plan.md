# Melee gesture sensitivity adjustment

The existing gesture owner requires only 3 cm of accumulated physical edge
motion and a 1 m/s default endpoint speed. A long weapon tip can reach that
speed during small wrist motions. The user reports accidental attacks from
jiggles and requests a modest sensitivity reduction.

Retain the gesture attack, neutral/reversal rearming, trigger suppression,
tracking continuity, and native mod attack behavior. Raise minimum measured
arc to 6 cm, the low-speed accumulation/reset boundary to 0.4 m/s, and the
default calibrated endpoint threshold to 1.25 m/s. The accumulation/reset speed
is capped at an explicitly authored slower threshold, so per-weapon
`melee_speed` values still override the default. All units are physical metres,
independent of world scale; no mod-specific classification or damage path is
introduced. Check at the end that input/calibration fixtures compile and that
the existing trigger suppression remains intact. Headset comfort tuning is
user testing; these constants are a conservative starting adjustment.
