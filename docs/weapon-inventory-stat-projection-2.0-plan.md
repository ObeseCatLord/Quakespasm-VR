# Optional weapon inventory fields at the native stat boundary

2026-09-30. Verified brief before implementation for WPN-003/004 transport.
Keep native `SV_CalcStats`, custom-stat ordering and protocol encoders. No new
mod family, stat, capability, inventory cache or VM conversion framework.

## Demonstrated source boundary

The inherited optional fields are `weapons`, `items2`, `moditems` (with
`items_dwell` fallback), `weapon2` and `weapons2`. Primary `51b452c0:sv_main.c`
reads them as QuakeC floats. Current `sv_main.c:SV_CalcStats` copies that approach
with unchecked `(int)val->_float` casts and field-offset-only lookup. A valid
float bitmask with bit31 set, such as2147483648, is outside signed32 range;
nonfinite/out-of-range values also lack a defined integer conversion. A same-name
non-float field is interpreted as float bits. These are actual conversion/type
boundaries in the port-owned optional projection, not evidence for replacing
native stats or QC movement.

`ED_FindField` already returns the declaration from the native field hash;
`ED_FindFieldOffset` currently performs that same lookup and discards its type.
Reuse the declaration and native `GetEdictFieldValue`. Existing capacity readers
already require float declarations and validate their own values; keep them.

## Proposed minimal repair and open review decisions

One private bool-returning helper in `sv_main.c`, taking entity, field name and
the existing stat destination. Require an `ev_float` declaration; read its
numeric value and admit finite values in signed-negative32 through unsigned32
range. Preserve ordinary truncation toward zero. For values at or above2^31,
subtract2^32 in an int64 intermediate, then store the representable signed32
value. Reject values belowINT_MIN or at/above2^32; no saturating or guessed bits.
This preserves the32-bit transport/bitwise representation without an undefined
float-to-signed cast. Zero is a successful value, not missing metadata.

Replace only the six optional-field reads with that helper. Keep zeroed native
stat arrays, custom-stat overrides after these defaults and all native core
conversions unchanged. `items_dwell` is used only when `moditems` is unavailable
or invalid; a valid zero moditems remains authoritative. No new per-client state
or periodic diagnostic. Expected at most45 net lines in `Quake/sv_main.c` only.

Alternative: transplant another stat subsystem or add cached schema policy.
Reject: native declaration lookup, array ownership and encoders are reusable;
only this demonstrated optional projection is incompatible. Requested local
Astra must verify field/encoder semantics, signed bit31 representation, fallback,
ordinary numeric behavior and scope necessity before bounded local coding.
Stop/reopen if actual transport policy contradicts this design or requires
another file/owner. Effective review settings are not exposed; source advice
must not be labelled runtime or formal model certification.

## Qualification after all implementation

No builds/tests/probes now. Consolidated Linux/ARM checks need actual registered
QC float fields and server-produced/client-parsed stats: ordinary/fractional/
negative values, representable bit31/upper unsigned32 values, unavailable and
wrong-type fields, nonfinite/out-of-range values, valid zero versus unavailable
moditems fallback, map/VM reload, native custom-stat overrides and actual wheel
ownership. Array/helper tests alone do not establish those owner round trips.
Retain desktop/VR crossplay and normal mod behavior; user live device and
performance trials stay outside completion.
