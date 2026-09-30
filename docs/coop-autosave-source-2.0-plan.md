# Existing co-op autosave source checkpoint and numeric repair

2026-09-30. COOP-012 keeps the inherited native save/progress owner. This is a
source comparison and bounded before-code repair plan, not runtime acceptance.
Co-op revival remains excluded; Linux/ARM checks wait for full implementation.

## Behavioral reference and current evidence

- Current primary master51b452c0 `host_cmd.c:1652–1768` selects map-start,
  secret, monster-bucket and serverflags checkpoints; real-time minimum interval,
  failed-write backoff, bounded rotating `coop_autoN` slots and quiet native save
  writes belong to the same routine. This is verified by direct source reading.
- Current2.0 `host_cmd.c:2244–2364` already copies those policies. It checks the
  actual server VM, active/initialized/signed-on clients and native QC
  intermission convention. Pending saved clients **or extended spawn parms**
  suspend autosaving without resetting progress; this intentional improvement
  preserves outstanding restoration rather than copying the older primary reset.
- `host.c:885` calls it after native physics and accepted private commands.
  Server/map clearing resets existing `sv` slot/progress state. No client task,
  timer service, asynchronous writer or parallel autosave implementation is needed.
- Native `Host_SavegameWrite` refuses pending restoration/incomplete active
  signon, writes its temporary file with checked flush/close, atomically renames
  it through `Host_SavegameReplaceFile`, and leaves `sv.lastsave` unchanged for
  quiet autosaves. Existing detached client inventory projection remains.
- Both reference and destination cast QC progress and configurable kill/slot
  values before bounds checks. Non-finite/out-of-int-range casts have no valid
  C integer conversion; NaN minimum intervals also bypass ordinary comparisons.
  The current source demonstrates this boundary issue, not a measured crash.
- `coop_autoN` is the inherited autosave namespace. A manual save deliberately
  named the same slot can be replaced: there is no proven separate protected
  namespace. Ordinary differently named manual saves and unrelated files stay
  outside these writes. Do not advertise unconditional manual-save protection.

## Smallest repair and rejected replacement

Keep native `Host_CoopAutosaveFrame`, its current fields, gates and writer.
Before any integer casts or autosave state mutation:

1. Require finite native real/QC times and finite QC secret/monster/serverflags
   values representable as `int`. Compare via `double` to `INT_MIN/INT_MAX` so
   float rounding of `INT_MAX` cannot admit2147483648. Invalid input returns;
   it does not clear pending progress, advance slots or attempt save I/O.
2. Require finite configured kill interval, slot count and minimum interval.
   Clamp finite kill interval in **double** to1..INT_MAX before casting;
   clamp finite slots to1..COOP_AUTOSAVE_MAX_SLOTS before casting. Preserve
   truncation and the existing finite lower/upper-bound policy.
3. Preserve finite negative minimum intervals as0. Non-finite settings suspend
   autosaves until corrected rather than silently substituting new defaults.

No generic numeric framework, new cvars/defaults, changing rotation/trigger
priority, serialization/parser changes or mod policies. Expected exact write
set `Quake/host_cmd.c`, only autosave declarations/validation/clamping; at most
35 net production lines. Reopen before expansion or a new owner.

## Integration and final evidence

One coding worker owns that routine, main owns this plan/index/feature metadata.
Main reviews all source changes and scoped `git diff --check`; no test/build now.
Final Linux/ARM qualification exercises unchanged finite default/fractional/
low/high settings, NaN/infinite/out-of-range QC and settings refusal, pending
join/restoration, pause/intermission, every progress gate, slot resize/rotation,
failed-write backoff and successful native save restoration. Disk failure must
leave the prior slot intact. No headset/performance test is implied. Broader
COOP-010/011 save dialect/identity acceptance remains a separate requirement.

## Source integration checkpoint

The one coding worker implemented23 net lines in the existing autosave routine.
Main reviewed the entire patch against the primary routine and native writer:
finite/time/QC range validation precedes casts/state changes; double clamps
preserve finite truncation/default/bounds behavior without float INT_MAX
rounding. Invalid settings/progress leave pending rotation and baselines intact.
The existing reset on ordinary disabled state, join/restoration/intermission
refusal, trigger priority, failed-write backoff and successful publication
updates remain. Scoped whitespace passed; no executable checks were run.
This is source integration only. Final slot/gate/write-failure/restoration
Linux/ARM qualification and broader COOP-010/011 remain pending.
