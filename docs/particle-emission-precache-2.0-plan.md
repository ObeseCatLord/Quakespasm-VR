# Existing particle emission precache guard

2026-09-30; bounded source repair on2.0 before coding. Native vkQuake graphics,
timing, emission axes, frame visibility and scripted particle owners stay intact.

Primary master51b452c0 `cl_main.c:2425` accepts an explicit entity emission only
when its positive index is below MAX_PARTICLETYPES and the slot has a name.
Destination `cl_main.c:2342` and static `gl_refrag.c:243` check only positive
indices before resolving the particle slot. In contrast, existing destination
named-effect consumers in `cl_parse.c:3040/3062` already require a bounded named
slot. These are verified direct source reads.

The network entity decoder already clamps indices (`cl_parse.c:1077-1085`), so
do not advertise an unproven network out-of-bounds exploit. The demonstrated
behavioral gap is admitting unnamed slots instead of preserving the existing
model-defined emission fallback. The client state is initially zeroed and a
removed particle precache has nameNULL/index-1. Reuse the inherited bounded/name
guard at both current emission consumers; no extra resource lookup, particle
cache, renderer pass or mod-specific rule.

Exact write set: `Quake/cl_main.c`, `Quake/gl_refrag.c`, just the two positive
emiteffectnum conditions. Append `< MAX_PARTICLETYPES` and the named-slot check
with short-circuit ordering, keeping each existing else-if fallback untouched.
No trail consumer expansion or model-less renderer path in this repair.

Main reviews actual diff and scoped whitespace before committing. No builds,
tests, compiler checks, probes, fixtures or benchmarks until full implementation.
Final software qualification should cover named explicit emission, unnamed and
removed-slot model fallback, upper bound rejection, and existing static/dynamic
emission in ordinary desktop/two-eye VR. Source guard parity alone does not
prove visible effects or performance.
