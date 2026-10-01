# Final metadata source integration

2026-10-01. Source commit `f3727a10`; no tests, builds, executable probes, SSH,
game runs or deployment. Source integration is not executable acceptance.
Main/master and user-owned migration-2.0.md were untouched.

## Ownership, plan and review

Local Astra xhigh's exhaustive [current scope review](final-checklist-current-2.0-review.md)
identified C02/NET-021 as the only established remaining implementation area.
The [final before-code reopening](metadata-publication-final-reopen-2.0-plan.md),
`2f07022f`, accepted target1000–1200 changed lines, reopening BEFORE1300, and a
seven-file write set. Subsequent committed amendments preserved native desktop
demos, canonical wire colors, actual Info_SetKey result/deletion semantics,
serverinfo-before2 versus userinfo-after-spawn ordering, exact native signon fit
and pre-mutation key validation. Final step13 was committed in `a1eef772` before
main's final code corrections.

One fresh, verified gpt-6-luna/xhigh worker returned1002 added/deleted lines and
relinquished ownership. Main read the complete coupled patch and relevant native
consumers. Final main capacity/key-validation corrections increased the aggregate
by9 lines to1011:931 insertions and80 deletions. The final scoped git diff check
passed. No compiler or behavioral qualification ran.

| Changed file | Added/deleted aggregate |
| --- | ---: |
| Quake/cl_main.c |468|
| Quake/client.h |17|
| Quake/cvar.c |75|
| Quake/host.c |11|
| Quake/host_cmd.c |59|
| Quake/server.h |7|
| Quake/sv_main.c |374|
| Total |1011|

## Source-reviewed behavior

- Explicit directional metadata support works independently of PREDINFO;
  declaration changes reader capacity only. Conservative legacy limits remain.
- One ephemeral public projection excludes private underscore keys, retains star
  keys and quoted LF, and removes unrepresentable values without changing local
  stores. Full or reset-plus-incremental bundles share that projection.
- Userinfo preflight simulates exact native name/topcolor/bottomcolor overlay
  order and intermediate capacity, keeps authoritative colors on the wire and
  includes complete binary companions. Quoted names remain binary-authoritative.
- Complete units check reader token/text, recipient envelope and remaining room.
  Dirty obligations survive temporary pressure; permanent incompatibility has a
  bounded native per-recipient diagnostic/drop path. No parallel publication queue.
- Native signon publishes initial/empty serverinfo before2 and current slots after
  spawn/QC before3. New/reused occupation, post-QC retirement, spawn/fastload clears
  and map changes mark current-store obligations at their existing owners.
- Client initial name, prespawn, color/userinfo/spawn and begin use one persistent
  backing allocation, selected protocol logical limits and native pending-reply
  retry. CSQC initialization remains once-only; prespawn precedes enablecsqc.
- Live cvar/name/color/legacy-color/setinfo preflight covers complete synchronous
  sequences, prospective native committed values and seta flags. Refusal precedes
  cvar/default/VM/store/byte effects; successful native ordering remains.
- Demo playback retires live reply intent without formatting local metadata;
  built-in desktop demos retain native signon/loading/clear behavior.
- Exact native signon plus signon2 capacity now progresses; impossible permanent
  size fails visibly. Native signon storage/QC writers remain reused.

## Acceptance still required

This closes C02's source implementation checklist, not behavior certification.
All22 original audit findings and Q01 now have source integration receipts.
All185 feature rows and all eight final software/delivery groups still require
the applicable evidence in the [final checklist](final-checklist-2.0.md) and
[Linux/ARM qualification plan](final-linux-arm-qualification-2.0-plan.md).

Exercise actual sender/parser/reliable/QC owners without skipping signon phases:
pressure/retry, empty/update-only receipts, declared-only eligibility, full and
legacy envelopes, canonical near-full stores, private/star/quote/LF/deletion,
occupation/retirement/reuse, maps/fastloads, refused seta and desktop demos.
Existing fixtures that clear messages or inject spawn/begin are insufficient.
Keep external stock reverse-userinfo limits explicit. No arbitrary stock
large-field parity, live headset proof or measured speedup is claimed.
