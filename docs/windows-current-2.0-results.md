# Windows build qualification: current results

2026-10-02. The user explicitly requested Windows compilation using running
WinBoat. Follow the existing [plan](windows-current-2.0-plan.md); no Windows
runtime/game/headset or host GPU test is part of this compile boundary.

The first actual x64 Release build used MSBuild 17.14, MSVC 14.44.35207,
Windows SDK 10.0.26100 and the command-line v143 override, retaining the native
v145 project default. Pinned LunarG 1.4.341.1 was verified against its published
SHA256 and installed copy-only in the isolated guest build directory. The
existing Steam Audio 4.8.1 SDK was enabled. Source archive b92f37415aa4 has
SHA256 6ccab439c97b9ac02fdd801db735643d7e8de7f0d58dba8d6cbe073afdd33f1a.
Actual Release exit1 is retained; it is not a passing build.

Demonstrated strict-warning/PCH/encoding failures are repaired narrowly:
local identifier renames in 11 owners, guarded unsigned function-index bounds,
a declaration-local C4200 push/pop preserving the standard flexible-array
layout, UTF-8 project settings and PCH exclusions for independent/prelude
sources. No global warning weakening or renderer behavior change. The common
Linux target rebuilt successfully after those 12 source-file edits.

Windows project metadata now generates the 27 previously absent variants,
compiles the 16 additional variants in both configurations, generates/compiles
the two hidden-area base shaders, and includes addon catalogue and three
avatar source owners. Existing native CustomBuild commands, bintoc, optimizer,
configuration exclusions and dependency metadata remain owners. Main caught
and corrected the missing Vulkan1.1 target on hidden_area.vert after worker
completion. Both configurations now cover all110 Meson shader jobs with exact
required defines/target versions, C conversion and corresponding compile/filter
entries. All three XML files parse, no compile/filter/custom-build duplicates
were introduced, and git diff --check passes. This is source/build metadata
verification; it does not establish successful MSVC compilation or linking.

Local initial log/argv: /var/tmp/qsvr-windows-current-7yc2z0hy.
Main source-coverage checker:
/home/obesecatlord/FastGames/qsvr-arm-recovery-mgfh0avl/check-windows-coverage.py.
Durable Linux rebuild receipt is in the same recovery directory. No shipping
Windows binary has been accepted from this revision.

The root drive has only about250MB available after the earlier ENOSPC incident.
Another Windows VM build could expand its sparse disk and interrupt host writes.
The pending request is to free at least10GB on root. Further guest compilation
waits for adequate space; source/CPU qualification can continue on FastGames.
Final Windows Release/Debug builds, PE dependencies, artifact hashes and the
matching Linux/ARM refresh remain required F10 work.

## Additional output-free MSVC qualification

2026-10-02. With root still below250MB, main used the actual installed MSVC
compiler's documented /Zs syntax-only option, which creates no output files:
https://learn.microsoft.com/en-us/cpp/build/reference/zs-syntax-check-only?view=msvc-170
No guest object/PDB/shader generation/link, game/device launch or SDK change.
A small separately checksummed source overlay leaves the original immutable
build source intact. Actual compiler input hashes match the current checkout.

The four newly included production source owners exposed missing Q_COUNTOF
in custom_avatar.c and an inner loop shadow in r_avatar.c. Reuse native countof
in both avatar and tracker storage (6+5 sites) and rename only the inner avatar
loop index. Main inspected the native declarations and corrected the prior
function-index cast: func_t is already unsigned, while dprograms_t.numfunctions
is signed. Explicitly convert the count bound to func_t at the three existing
comparisons, preserving the original usual-arithmetic-conversion semantics.
No new behavior, helper, registry, policy or weakened warning setting.

Final actual MSVC14.44 /Zs /W4 /WX /utf-8 checks pass for all16 corrected/new
source owners under both Release/NDEBUG and Debug/_DEBUG definitions, with the
existing native warning exclusions, codecs/voice and Steam Audio enabled.
Current source hashes, arguments, native exits and raw local output are retained
in windows32f7f777/current-sources-syntax*.json and reviewed provenance receipt
under the durable recovery directory above. Linux rebuilt the four affected
owners and linked successfully. These are syntax/diagnostic checks; object
code generation, full project build, shaders, linking and PE dependencies still
require the actual Windows builds after space is available. The in-flight ARM
refresh remains at32f7f777, before these additional source-only portability fixes.
