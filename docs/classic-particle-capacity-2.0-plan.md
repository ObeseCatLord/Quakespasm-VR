# Bounded classic-particle capacity on native Vulkan

2026-10-01, final checklist C22 (MOD-010). Before-code plan. Main verified
current r_part.c:163/204/215/238/1021 against primary51b452c0 r_part.c:26/30/153.
Current accepted larger pools wrap 16-bit quad indices beyond16,384 particles.

Retain vkQuake's classic particle list, dynamic vertex buffer, textures,
quad/triangle appearance and existing static index buffer. FTE scripted-particle
storage is separate and unchanged. A new particle renderer, instancer or multiple
batch owner is unnecessary. Expected production scope: Quake/r_part.c only.

Restore primary's default32,768/minimum512/maximum65,536. Check that -particles
has a following nonnull argument before parsing; absent/missing argument uses
the default, as primary. Use standard strtol base10 and clamp the long result
before int conversion/allocation. This keeps ordinary atoi-style numeric-prefix
and nonnumeric-to-minimum behavior while handling conversion overflow through
strtol's bounded LONG_MIN/LONG_MAX result. Negative/small values clamp to512;
positive/overflowing counts clamp to65,536. No second command-line parser.

Use uint32_t for existing static quad indices, matching byte sizing, staging
pointer and VK_INDEX_TYPE_UINT32 binding. Request at least four-byte staging
alignment for these elements/copy offsets. The bounded maximum keeps index and
allocation math finite. Native triangle drawing and shared showtris consumer
retain their existing path; no global geometry/index-format change.

Source review only now. After all implementation, consolidated Linux/ARM checks
must cover parameter absence/missing/negative/nonnumeric/large/overflow cases,
dense populations crossing16,384 through65,536, both shapes and showtris, actual
rendered index/vertex correspondence, and native resource retirement. No tests,
builds/compiler/lint/probes/fixtures/benchmarks/game runs before implementation
ends. Reopen if this requires another allocation lifetime or renderer system.
