# Observe the native alpha consumer in optimized builds

2026-10-02. Existing F05 opposite-liquid acceptance only; no new feature.
Current0bd4ddb1 packaged GCC13 optimized renderer keeps both alpha/water stages.
Main inspected R_DrawAlphaEntitiesTask machine code: stage0 overwater passes
through +141; stage1 branches to +1056 and rejoins +171, bypassing the helper's
+141 debugger location. GDB reports only two inline helper locations. Hence its
three observed tuple set is incomplete evidence about four actual submissions;
source/disassembly do not justify renderer replacement or an alpha workaround.

Reuse non-inlined R_DrawEntitiesOnList, actually called at task offsets+278 and
+436 on each loop iteration. The minimal probe adapter observes its entry with
native alphapass/chain/context and live descriptor overrides. Map the two actual
alpha chains to stages0/1; require the exact native secondary context, one of
the two non-null descriptor/offset pairs. The native uniform ring intentionally
shares a page descriptor; distinct dynamic offsets identify the two eye blocks. Record that
actual eye bit with stage/alphapass. Preserve the original four expected tuples,
opposite water/empty categories/masks, quality checks, OIT reset and all mirror
captures. No assigned mask/list/capability or source/compiler/runtime changes.

Alternative debug-only rerun retains earlier evidence but does not qualify the
actual optimized package. Do not disable inlining, inject counters into shipping
code or relax the four-tuple oracle. The native downstream consumer is stronger
and does not depend on optimized source-location coverage.

Main owns tests/openxr-stereo-liquid.gdb and these docs; no delegation overlap.
One fresh private run uses current immutable package, existing isolated simulated
null Monado and licensed stock data. No physical tracking/gaze/performance,
NVIDIA reset, user input or unrelated runtime changes. Retain prior failed log.
Success needs all5 phases, exact selectors/quality/reset, inspected native mirrors,
normal exit0. Zero alpha entities still cannot prove translucent composition;
authored geometry remains separate F05 work.

First focused run rejects the incorrect assumption that the two page descriptors
must differ. Main verifies R_UniformAllocate returns the same ring-buffer
descriptor for allocations on that page. Correct observation uses the complete
(descriptor,dynamic offset) pair and requires distinct pairs; no renderer change
or weakened eye-identity assertion. Failed run retained.
