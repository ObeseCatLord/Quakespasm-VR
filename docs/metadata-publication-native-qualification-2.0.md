# Native C02 metadata publication qualification

2026-10-01. Behavioral reference: C02 source integration `f3727a10` and its
final reopening plan. Run at checkout `cfc19120` on branch `2.0`. The fixture
is405 C plus79 Python lines,484 combined, exceeding the original450-line bound.
Worker stopped with scope_done=false. Main read the complete return and reopened
the test-only slice to500 combined lines in the [scope disposition](metadata-native-fixture-scope-reopen-2.0.md).
No production source was edited by this worker.

## Run

The fixture reused `tests/negotiation_native.make` with source and output
overrides. It compiled the fixture translation unit with `DEBUG=1`, SDL3,
`-Wall -Werror`, and wrappers limited to transport pressure and graphical
reset/skin boundaries. Engine objects came from the isolated `b5e7af20` source
snapshot; the directly included C02 sender/parser sources were unchanged from
that snapshot. Later production change `c8de89f6` only changed `sv_phys.c` and
was not in the linked object set; rerun after the main portable build is green
if that frame-path change is material.

```sh
make -f ../tests/negotiation_native.make negotiation-native-fixture \
  NEGOTIATION_SOURCE=/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0/tests/metadata_publication_native_fixture.c \
  NEGOTIATION_FIXTURE=/tmp/qsvr-metadata-publication-native-isolated/metadata-fixture \
  DEBUG=1 USE_SDL3=1 \
  NEGOTIATION_EXTRA_LDFLAGS='-Wl,--wrap=R_TranslateNewPlayerSkin -Wl,--wrap=R_CheckEfrags -Wl,--wrap=R_ClearParticles -Wl,--wrap=PScript_ClearParticles -Wl,--wrap=R_NewMap -Wl,--wrap=SCR_EndLoadingPlaque -Wl,--wrap=CL_SignonReply -Wl,--wrap=SV_SendClientMessages -Wl,--wrap=SZ_Write -Wl,--wrap=MSG_WriteByte'
python3 tests/run_metadata_publication_native.py \
  --binary /tmp/qsvr-metadata-publication-native-isolated/metadata-fixture --timeout 45
```

The runner passed all five profiles: QSMI without PREDINFO (5 frames), legacy
PREDINFO without QSMI (5), exact recipient fit (5), temporary reliable
pressure/retry (7), and blocked prespawn/begin client sends (9). Full logs and
disposable profiles remain under `/tmp/qsvr-metadata-publication-native-yweai70i`.
The runner links available stock paks from the read-only game installation;
only `id1/pak0.pak` exists in this installation. Profile, userdir, saves and logs
stay in the disposable root.

## What passed

The fixture observes the real loopback `cmd pext` query bytes but deliberately
bypasses the client parser for that first query. It executes its selected offer
through `Cmd_ExecuteString` and asserts `pextknown`; automatic client query parsing
is not proved by this fixture. QSMI is accepted
without PREDINFO; the legacy profile accepts PREDINFO without QSMI. The real
parser receives initial serverinfo before signon 2 and current userinfo slots
before signon 3. The legacy profile executes empty `fullserverinfo` and
incremental `svi` handlers, then verifies both long serverinfo values were
reconstructed. The QSMI case verifies a 7100-byte userinfo field, star and LF
fields, private/quoted custom-field filtering, quoted native-name restoration,
and final scoreboard name/colors after ordered overlays.

Exact fit leaves precisely the native signon bytes plus signon number available
at the real sender boundary and reaches signon 2. The pressure case leaves one
byte unavailable, observes the sender retain its phase, lets its reliable
`svc_nop` drain normally, and then observes retry progress. The control profile
calls the real `CL_SignonReply` and `CL_SendCmd`; it checks pending prespawn and
begin with five and six bytes of client-message headroom, then completes spawn.
No queue is cleared to force progress and no signon phase is skipped.

## Limits and follow-up

Permanent recipient incompatibility is **not qualified**. A disposable trial
with a one-byte reliable limit reached the real native drop call at
`Quake/sv_main.c:5752`, then terminated with SIGSEGV inside that drop path; the
available backtrace did not identify the faulting operation. The passing runner
does not hide or retry that case. Main should investigate the drop-path failure
before claiming permanent-failure behavior.

Retired/reused slots, reconnect or map downgrade, fastload/spawn clears,
mid-signon mutation, separately empty/update-only initialization, other protocol
logical limits, live cvar refusal, demo signon, renderer/headset integration,
and general gameplay remain unproved. Renderer map/particle/skin operations are
explicitly wrapped; this is sender/parser qualification, not graphics proof.

## Main current-object refresh

Main rebuilt the isolated fixture after copying the four subsequently changed
engine sources (gl_rmain.c, gl_vidsdl.c, r_passes.c, sv_phys.c) from committed
production55eaa33b. Main compared all308 production C/header/shader/make files
with that immutable source archive; zero source mismatches after refresh. The
five cases again passed with frames5/5/5/7/9. Logs:
`/tmp/qsvr-metadata-publication-native-brtfw9uc` and final qualification
metadata-final-objects-build.log / metadata-final-objects-run.log. These are
current sender/parser/physics objects at that production snapshot, not complete
portable/audio/graphics acceptance. Later92bc7775 changes only the BSP face
guard, so loaded-content evidence must still name its newer revision.

Main localized the permanent-limit crash with a current-object debugger trace:
native SV_DropClient completes and the receiver enters Host_EndGame("Server
disconnected"). It then calls longjmp with all eight host_abortserver jump slots
still zero. The fixture drives server/parser functions directly and never enters
native _Host_Frame's setjmp boundary. Private evidence:
metadata-host-jump-diagnosis.log. Thus this particular crash is a fixture lifecycle
defect, not an established product drop-path defect. The diagnostic stops before
the invalid jump and is not negative-case acceptance. Permanent incompatibility
still needs a correctly armed native host-frame/disconnect test; no production
drop rewrite is justified.
