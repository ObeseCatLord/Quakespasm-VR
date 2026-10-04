# CAND-UX-001: Background save design

Status: Astra xhigh design reviewed; implementation authorized. Tests wait until the selected batch is complete.

## Verified boundary

`Host_SavegameWrite` is the sole target serializer and already makes `<save>.sav.tmp`,
flushes/closes it, replaces the destination, removes failures, rebuilds `savelist`, and
sets manual `sv.lastsave` only after success (`Quake/host_cmd.c:1949-2241`). It emits
the v5/v7 headers, co-op player snapshots, globals, edicts, and trailer in one order
(`2097-2212`). `ED_Write` and `ED_WriteGlobals` read target-global `qcvm`, VM defs,
strings, and globals (`Quake/pr_edict.c:949-998`, `1197-1222`); they cannot run in a
worker.

There is no target memory output stream/save sink (`open_memstream`, `fmemopen`, and
SDL memory streams have no matches). `Sys_MemFileOpenRead` is read-only. Existing
atomic primitives provide acquire/release stores (`Quake/atomics.h:192-294`), and
`Host_SavegameReplaceFile` supplies the required Windows write-through rename/Unix
rename boundary (`Quake/host_cmd.c:1742-1756`).

Co-op autosave is called after physics (`Quake/host.c:907-914`), rotates slots only
after `Host_SavegameWrite` succeeds (`host_cmd.c:2367-2386`), and deliberately does
not alter manual `lastsave` (`tests/local_load_native_fixture.c:336-360`). Save lists
also rebuild after a game-directory switch (`Quake/common.c:3477-3533`). Loads read
the destination before compatibility/mod processing (`host_cmd.c:2699-2864`).

## Decision

Use the current save owner with **main-thread serialization to an immutable byte
buffer**, then a bounded SDL save thread performs only temp-file write/flush/close and
`Host_SavegameReplaceFile`. This moves disk latency off-frame without allowing worker
access to QC, clients, `sv`, `svs`, `qcvm`, `pr_global_struct`, console, or savelist.

Do not copy the donor VM worker. Ironwail's worker calls `PR_SwitchQCVM`, walks the
snapshot through `qcvm` metadata, and writes while global VM state can be retired
(`../ironwail/Quake/host_cmd.c:2314-2349`). QSS-M does the same
(`../QSS-M/Quake/host_cmd.c:8450-8502`); its `SaveData_Fill` snapshot still leaves
serialization dependent on `qcvm`/definitions (`../QSS-M/Quake/pr_edict.c:414-606`,
`872-903`, `1048-1070`). Although those donors wait before loads/prog destruction,
their VM/context accesses must be adapted because target qcvm is global, unlike the donor THREAD_LOCAL qcvm.

Astra identified that the rendering task pool can have just one worker and
`Task_Allocate` can block. Disk I/O must not occupy that pool. Use the existing SDL
thread API, already used for map parsing: at most two accepted saves each own one
joinable I/O thread, with no permanent service, queue or new executor framework.
Normal frames poll terminal atomic state and join only an already-finished job.
Lifecycle drains join submitted jobs. Reserved-but-unsubmitted slots are released,
never awaited. Threads see immutable bytes/paths only. The terminal release store
is their final slot access; main joins before reclaiming the slot.

## Minimal incremental write set

1. `Quake/progs.h`: declare a small append-only save sink and `ED_WriteToSink` /
   `ED_WriteGlobalsToSink` adapters.
2. `Quake/pr_edict.c`: factor the existing two serializers into shared emitters; keep
   `ED_Write(FILE *)` and `ED_WriteGlobals(FILE *)` wrappers byte-identical. The new
   sink adapters call the same loops/formatters, never duplicate field/global policy.
3. `Quake/host_cmd.c`: retain validation and exact serializer order in
   `Host_SavegameWrite`; capture through a growable checked buffer on the main thread;
   add two fixed background slots and SDL I/O threads that write immutable bytes using
   the existing temp/replace helper.
4. `Quake/server.h` and `Quake/host.c`: expose/poll completion once per main host frame, independent of server activity;
   no `tasks.c/.h` change.
5. `Quake/common.c`: drain slots before `COM_ResetGameDirectories` in `COM_SwitchGame`.

No `savedata_t`, VM clone, new save format or new filesystem API. A bounded dedicated disk thread is the necessary executor isolation identified by the review.

## Slot and completion contract

* A main-thread admission validates exactly as today, resolves/canonicalizes the destination, rejects
  conflicting/full slots, reserves a free slot, then serializes fully. A slot contains bytes/length, final/temp paths, save name, quiet/manual
  mode, and immutable autosave candidate metadata. Publish `RUNNING` only after all
  fields are initialized; I/O thread publishes terminal success/failure with release
  ordering. Main consumes terminal state with acquire ordering, frees bytes, and
  returns the slot to idle. Worker writes no engine state.
* Capacity is two nonblocking slots. Refuse admission when full or when another slot
  targets the same save; do not wait in frame/command code. A rejected manual save
  reports busy. A rejected autosave leaves its trigger state untouched and schedules
  existing retry backoff.
* The I/O thread checks `fwrite`, `ferror`, `fflush`, `fclose`, and replace; on any failure
  it removes only that temp path and publishes failure. Preserve current durability
  semantics (no new Unix `fsync`).
* Main-thread completion is the only place to call `SaveList_Rebuild`, print result,
  set manual `sv.lastsave`, or commit autosave rotation/timestamps/counters. An
  autosave-pending latch prevents duplicate captures; success commits its captured
  candidate, failure sets retry using completion-time `realtime` and releases latch.
* Requests have monotonic sequence numbers: an older successful completion cannot
  replace a newer successful manual lastsave. Autosave metadata captures reason,
  slot count, next rotation, progress counters/serverflags and snapshot time.
* Before restart/changelevel chooses lastsave, and before every `load`/`fastload`, drain all slots then report their terminal results
  before opening a file. Drain before game-directory reset and at `Host_Shutdown`
  before I/O teardown; also drain before Host_ShutdownServer and Host_ClearMemory; free idle slot buffers. These waits are explicit lifecycle
  boundaries, never ordinary frame polling.

## Verification required after implementation

1. Add a native fixture that delays/blocks the worker write: frames continue; a second
   save uses the other slot; same-name/full requests do not overwrite or block; final
   files appear only after atomic completion.
2. Compare synchronous-reference and captured bytes for legacy v5 and co-op v7,
   including active/inactive clients, extended parms, copied client edict, globals,
   lightstyles/precaches, fog, and sky.
3. Extend `local_load_native_fixture.c` autosave checks (`318-425`) so rotation and
   retry change only after asynchronous completion; retain atomic open/rename failure,
   old-file preservation, and load restoration checks.
4. Exercise pending-save `load`, KEX mod switch, map/server teardown, and process
   shutdown; assert no VM/client access after teardown and no leaked temp/slot buffer.

## Acceptance and limits

The main-thread serialization cost remains; this slice removes disk-write stalls only.
Completion errors are delayed until a poll/lifecycle drain, and an in-flight save is a
valid immutable earlier world snapshot. Astra review should confirm the sink preserves
byte formatting and that all completion state is main-thread-owned.

## Senior-review disposition

| Finding | Disposition | Implementation contract |
| --- | --- | --- |
| Poll/transition gaps | Adopt | Main host-frame polling and drains before selection/read/reset/teardown |
| Reserve after capture; rendering-pool blocking | Adapt | Reserve first; two bounded joinable SDL disk threads, no rendering-pool work |
| Reverse completion and autosave metadata | Adopt | Request sequence and captured success-only metadata |
| Task handle epoch statement | Correct | Removed incorrect claim; dedicated SDL join handles own disk-job lifetime |
| Shared serializer and immutable bytes | Adopt | One serializer with FILE and memory sinks; no worker QC accesses |
| Capture failure and longjmp cleanup | Adopt | Reserved/submitted distinction; drain releases unsubmitted capture without awaiting it |
