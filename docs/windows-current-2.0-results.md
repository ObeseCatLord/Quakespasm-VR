# Windows build qualification: current results

## Current full-build acceptance

2026-10-02: **x64 Release and Debug both build successfully**, actual native
MSBuild exit0 at0bd4ddb1cf495b4d6f055ad37fcedf275485def9. Strict warnings and
Release whole-program/LTCG optimization stay enabled. Both configurations have
all110 generated/compiled shader objects and230 total compiled objects, including
the actual OpenXR, migrated gameplay/voice, stereo/AO/UI/particle/avatar owners.
MSVC14.44/v143 override, Windows SDK10.0.26100, pinned LunarG1.4.341.1 and
Steam Audio4.8.1 are enabled; the repository's native v145 default is unchanged.

Source archive SHA256:
5e8a396158a4f4dbcc94006651023e5b7935912dae25fba4bb385cf4e3f652cc.
Main independently reconciles689 selected production/build/shader/packaging/license
input files against current bytes. All retrieved binaries/engine PDB/notices
match their native build hashes. Both actual engine import tables contain all
four required Opus encoder symbols and have no static OpenXR-loader import.

| Configuration | Native exit | Engine bytes | Engine SHA256 |
| --- | --- | --- | --- |
| Release | 0 | 4198400 | 3a4f191413e555d8433bea597216e2374571693383c5e6d2ed4e6ebb6034c9a7 |
| Debug | 0 | 9756160 | d69d6c8514938dc8c4fcf56d49e0d2c055caafc518a3de192f75653daaf76d38 |

All13 output PE files per configuration are x64. Required normal direct imports
resolve to the bundled DLLs, guest system DLLs or Windows API-set contracts.
This checks every bundled DLL's normal imports, not a redistributable Windows
installer, every OS dependency transitively, dynamic-runtime discovery or
headset/gameplay/audio-device behavior. Debug CRT providers are present in this
development guest; no standalone Debug redistribution is claimed.

The first checker incorrectly treated Steam Audio's delay-import section as
normal dependencies. Actual dumpbin output separates OpenCL.dll/GPUUtilities.dll/
TrueAudioNext.dll as delay loads; the latter two are absent from the output. The
existing game code selects CPU DEFAULT scenes and PARAMETRIC/HYBRID reflections,
never the OpenCL/Radeon/TrueAudioNext backend. The corrected checker records them
explicitly as unavailable delay loads and preserves all normal-import failures.
No extra GPU library installation or graphics/runtime setting change is needed
for this compile boundary. [Microsoft's official delay-load semantics](https://learn.microsoft.com/en-us/cpp/build/reference/linker-support-for-delay-loaded-dlls?view=msvc-170).
Runtime calls that exercise a delay-loaded backend are not certified.

Durable compiled outputs and detailed source/argv/native exits/PE/import/hash
receipts: FastGames/qsvr-windows-0bd4-p_zrkd1v, build-artifacts/{Release,Debug}.
No engine, GPU, physical audio or headset launch occurred. Earlier failed builds
and storage/launcher/diagnostic receipts below remain historical. Final matching
Linux/ARM artifacts, remaining frozen acceptance boundaries and whole-goal local
Astra integration signoff remain F10 work.

## Historical attempts

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
require the actual Windows builds after space is available. The ARM
refresh at32f7f777 subsequently completed and was retrieved/verified; see
[ARM refresh results](arm-refresh-current-2.0-results.md). It predates these
additional source-only portability fixes and is not the final reconciled artifact.

2026-10-02 continuation: Docker mount inspection confirms the actual WinBoat
/storage backing directory is on the root filesystem (about206MB available),
so placing host receipts on FastGames does not remove VM disk growth risk.
Storage-gate receipt and immutable3204aaba source archive/build script are
prepared under windows3204aaba in the durable recovery directory. No full
Release/Debug build was started with this insufficient storage. Full build and
PE/link qualification remain open; no output-free check is substituted for them.

Current prepared3204aaba archive is independently byte-reconciled against all
672 tracked production/build/resource/license inputs under Quake, Shaders,
Windows, Packaging, Misc, LICENSES and root build/license files. SHA256 matches
the recorded immutable archive; selected tracked-path sets match with no
missing/new paths and all file bytes match the current worktree. Receipt:
windows3204aaba/shipping-byte-reconciliation-current.json in the durable
recovery directory. Later docs/test-only commits do not invalidate these
checked inputs. This is source freshness evidence, not Windows build/link,
complete package acceptance or final F10 signoff.

2026-10-02 read-only WinBoat availability recheck: private channel returns0,
MSBuild and private glslangValidator present, no task-owned MSBuild process. Guest
C free space is about110.6GB, but backing host root has only44.4MB. Sparse guest
free space does not provide host allocation capacity. No full build started.
Private exact receipt: qsvr-room-native-_1heinas/windows-prerequisites-readonly.json
on FastGames. Prepared3204 Release/Debug job remains pending the existing host
space request; no credentials/VM/driver/system settings changed.

## Full build resumed after storage recovery

2026-10-02. Host root now has over300GiB available; the earlier storage blocker
is resolved. Actual f814 Release compilation exposed three int-to-float room
arguments, boolean-to-float spatial assignment and eight constant complement
mask casts. Narrow9649 repairs preserve the existing values/masks and warnings.
The current actual native Linux graph recompiles/links0 and all12 combined
audio qualification markers pass; [current room/audio receipt](room-monitor-native-current-2.0-results.md).

Actual9649 MSVC compile/shaders reach linking and fail on four missing Opus
encoder imports. The bundled DLL and main reference are decoder-only. Full
shared Opus1.5.2, matched GNU/native import libraries and required BSD notice
are now integrated in d50a13a3; [bounded actual dependency result](windows-opus-2.0-results.md).

Fresh immutable d50a13a3 source archive SHA256
c4650e27da3a6905e8798dc6b38f2e36b96643f888ef8c245a6a883cb04c0141
compiles engine/shaders and reaches native Release LTCG. The Opus unresolved
imports are gone, but link-time C4701/C4703 guard-dependent lifetime diagnostics
across six owners cause strict /WX code-generation failure/LNK1257, actual
MSBuild exit1. These paths are undergoing verified local Astra/xhigh review
before narrow edits. No successful full Release or Debug build is claimed.

The guarded wrapper now retains the actual process handle before waiting and
records native exit correctly; a process-only child ExecutionPolicy permits
its private script without changing system policy. Failed first policy/exit-
property launcher receipts are retained. The fresh source is never edited in
place. Exact current logs/status/storage observations are private
FastGames/qsvr-windows-d50a-4wutinyc; no storage stop occurred. No engine/GPU,
physical audio or headset test ran. Windows full builds/PE closure and final
matching Linux/ARM artifacts remain open.
