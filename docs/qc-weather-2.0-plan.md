# Client QuakeC rain and snow adapters

## Reference and current state

Primary `quakespasm-openvr/Quake/pr_cmds.c` registers `te_particlerain` 409 and
`te_particlesnow` 410 for both VMs. Its scripted-particle CSQC branch passes
the supplied volume, velocity, integer count and palette color directly to
`PScript_RunParticleWeather` with `rain` or `snow`.

Destination `Quake/pr_ext.c` already has SSQC handlers, but the CSQC registry
entries are NULL. Destination `Quake/cl_tent.c` handles received rain/snow with
the same existing `PScript_RunParticleWeather`. Its renderer and effect lookup
already own the particle pool, type/color selection, spawning, simulation and
drawing. `PR_Can_Particles` already qualifies weather advertisements by native
settings and protocol. Reuse those services and predicates; do not introduce
another effect implementation or rewrite Vulkan particles.

## Minimal adapter and intentional boundary

Copy primary's CSQC dispatch shape into a shared local weather wrapper and two
entrypoint wrappers in destination `Quake/pr_ext.c`. Fill only the NULL CSQC
slots at 409/410. SSQC serialization, multicast/protocol restrictions, packet
parsing, registry numbering, lazy binding, settings and rendering remain native.
No new capability is added; the existing two advertisements now have handlers
in the client VM as they already do in the server VM.

Preserve supplied volume and velocity and use the existing scripted effect
lookup, including its native fallback if a named weather type is absent. Do not
import primary's alternate non-scripted build implementation: this destination
already has the scripted renderer and native received-weather path.

At this new QC boundary, reject nonfinite count/color before integer conversion,
ignore counts below one, truncate ordinary fractional counts like primary and
clamp count to 65535 and color to 0..255 before conversion. Those bounds match
the established weather packet count and primary's transmitted palette domain;
ordinary valid primary calls are preserved. This intentionally bounds unusual
direct CSQC arguments instead of reproducing primary's undefined extreme float
to integer conversion or unbounded count. Do not change the existing SSQC
handlers or introduce a global direct-call gate in this slice.

A registry-only fallback would hide a requested inherited effect. A new particle
system would duplicate all existing native owners. The chosen vertical slice is
one shared wrapper, two one-line wrappers and two existing registry entries,
confined to `Quake/pr_ext.c`. Reopen if a new renderer or lifecycle owner becomes
necessary. The same scene particles serve desktop and single-pass VR.

## Acceptance

Personal local Astra source review must verify primary CSQC argument order,
effect names, number/VM registration, finite conversions and reuse of the
received-weather backend, with server/protocol behavior unchanged. Builds,
compiler probes, runtime tests and performance measurements remain deferred.
At final Linux/ARM software verification, cover numeric and named calls in both
VMs, rain versus snow, ordinary/fractional/zero/negative/huge/nonfinite counts,
palette endpoints, missing effect types and native disabled-backend query
behavior. Check that client calls produce local effects without writing a server
message. User-owned headset visual checks remain separate.

Source acceptance of these adapters is not full QuakeC, graphics or mod parity;
other missing interfaces and final qualification remain required.

## Local Astra source disposition

Personal local Astra Max found no introduced P1/P2 in the actual wrappers or
registrations. It verified argument order, rain/snow names, ordinary truncation,
finite bounded conversion, VM-selected lazy dispatch and unchanged SSQC/network
paths. The calls reuse the native received-weather backend and write no server
message. This is source acceptance only; no builds, tests or probes were run.

Volume and velocity pass through like primary. Native effect definitions can
multiply the count; the adapter's input clamp is not a total-particle or
performance guarantee. Backend limits and simulation/rendering remain native.
