# Complete rectangular texture mip chains and validate anisotropy

## Reference and defect

`TexMgr_LoadImage32` is the behavioral reference.  On baseline `7cc8cdb2`,
the mip-count, staging-size, copy-region, and CPU generation loops all divide
both dimensions and continue only while both remain nonzero.  A `256x64`
image consequently ends at `4x1` rather than emitting `2x1` and `1x1`.

The existing `MAX_MIPS` check remains the bound: input dimensions are already
limited by the Vulkan device maximum image dimension before the fixed-size
copy-region array is used.  No new state, texture owner, sampler policy, or
descriptor path is needed.

## Minimal implementation

Each of the four mip loops will emit the current level, stop once it emitted
`1x1`, and use `max(dimension / 2, 1)` for the next extent.  The CPU reduction
will invoke `stbir_resize_uint8_linear` for every non-`1x1` source level with
the clamped next width and height.  This preserves one-dimensional tails and
does not request a zero-sized STB resize.

The change is limited to `Quake/gl_texmgr.c`.  It leaves texture ownership,
native sampler selection, the existing forced-linear VR descriptor route, and
global filter settings untouched.

## Evidence

A bounded native fixture will call the production `TexMgr_LoadImage32` and
intercept its Vulkan upload.  It will verify all emitted image-copy extents and
the uploaded tail bytes for `256x64`, `64x256`, one-dimensional tails, and
`1x1`, including the nine-level rectangular chain.  It will not duplicate the
four helpers as a mirror test.

The existing real-Vulkan anisotropy fixture will retain its production sampler
and descriptor checks, but choose the supported power-of-two request from
`16`, `8`, `4`, or `2`.  Devices that expose sampler anisotropy and compute
will execute a real texture readback at their supported cap; only devices
without those required features will skip.  This avoids treating a mobile cap
below 16 as a pass-by-skip.  The sampler/device-feature code was reviewed as
already correct, so no feature-path rewrite is planned.  Frame SSH was
unreachable on both candidate endpoints and supplies no physical-device
claim.

## Verification

After implementation, compile the owned production source with the strict
Clang Meson graph at
`/home/obesecatlord/FastGames/qsvr-selected-final-debug-clang-20261004`, run
the complete-mip native fixture, and compile/run the real Vulkan anisotropy
fixture.  The final commit will contain only this plan, the texture-manager
change, and its owned fixtures/runners.

## Completed verification (2026-10-04)

After implementation, ran:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/run_anisotropy_native.py
```

The runner reuses `native_link_recipe` and `remove_depfile_args` from the
existing `run_csqc_entity_native.py` builder. It reads the prepared graph's
strict Debug compile flags and link libraries, removes its foreign-source PCH,
and redirects source includes and all object outputs into this checkout and
an isolated temporary directory. It neither invokes Ninja nor reconfigures the
graph. `build.ninja` and `compile_commands.json` hashes remained unchanged.

The complete production `gl_texmgr.c` and sampler owner `gl_rmisc.c` compiled
with `-Wall -Werror` and assertions enabled. The upload fixture includes the
actual `TexMgr_LoadImage32`, captures its Vulkan image/view/copy/barrier calls,
and examines its generated staging bytes after `R_StagingEndCopy`. Literal
extent tables establish the expected chain independently of production mip
helpers. Both `256x64` and `64x256` passed with nine levels and 87,388 staging
bytes, including `2x1`/`1x2` and `1x1`. Constant and split-color inputs verified
all levels' initialized channels and the final averaged tail. `8x1`, `1x8`,
`1x1`, `7x3`, and a non-mipmapped upload also passed. `32768x1` passed at
16 levels; `65536x1` hit the existing `MAX_MIPS` fatal guard before image
creation. Staging guard bytes remained intact.

The real Vulkan fixture ran on the local NVIDIA GeForce RTX 4090, whose
reported maximum anisotropy is 16. All supported requests (`2`, `4`, `8`, `16`)
executed GPU readbacks using a matching shader footprint. At each request,
adjacent-stripe contrast was 0 with anisotropy off and 1 with anisotropy on.
Production sampler and descriptor checks passed, including VR's linear route,
ordinary point selection, saved value `1` selecting the device maximum, and
oversized requests clamping to that maximum. No production sampler settings
were changed.

These results cover native upload capture and local GPU sampling. They do not
establish physical Frame/mobile execution; Frame SSH remained unavailable.
Devices with smaller supported caps now run every supported power-of-two
request rather than skipping solely because the cap is below 16. The runner
propagates exit 77 as GPU-unverified when no suitable device is available.
