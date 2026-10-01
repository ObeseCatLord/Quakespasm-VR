# F01 current connected desktop/VR results

2026-10-01. Production repair482cd9f5, preliminary host DEBUG Meson binary
rebuilt successfully; SteamAudio disabled only in this preliminary host route.
Actual isolated current server and simultaneous private desktop/simulated-XR
clients pass both existing GDB probes, their structured statuses/markers and
runner exit0. Real IPv4 UDP, natural pext/signon, native command input/transport/
QC/server snapshots/parsing/rendered prediction; no captured transport or
assigned commands/body/ammo. Controller actions/poses and held keyboard state
remain software-controlled; HMD stays runtime-owned. Separate temporary basedir/
userdir for every process, read-only pak0/preset links, private Monado environment.

| Observed boundary | Current result |
| --- | --- |
| Desktop | signon4, private dialect1/legacy0, two named peers, actual movement236.291units, shells25→21, advancing command/ACK, selected replay permission and coherent owner/movement stats |
| Desktop presentation |283successful replay returns, actual unchanged-ACK/sent/authority pair and1.872units rendered movement; native short jump timer0.029s observed |
| VR | actual focused two-eye session with runtime HMD unchanged, native right-trigger firing, private relative-hand commands and selected prediction; three shells consumed |
| VR presentation | Existing between-send displayed-owner movement, matching actual replay returns and settled owner checks pass; complete details retained in vr-result.json |
| C02 regressions | Refreshed isolated cl_main.o, complete documented link recipe, all six native metadata cases pass again, including pressure/control/permanent native drop |

The first connected run found an actual existing-owner defect: name/color
fallback could reach an unnegotiated client before serverinfo allocated its
scoreboard. Diagnostic captured signon0/maxclients0 and valid slot0/nameDesktop,
not corrupted framing. The reviewed3-changed-line repair uses recipient
knowntoqc, the native frag readiness boundary. Current stores and initial spawn/
metadata publication retain latest names/colors; no queue or parser bypass.
The test deliberately retains +connect before +name, the original trigger.
[Before-code plan](early-scoreboard-publication-final-2.0-plan.md).

The runner's first repaired trial omitted the original recipe's vsync/render
controls and observed117successful replays but no between-send frames. Even
with those restored, heavy VR debugger injection could reach the default network
interval each frame. The final VR diagnostic uses the existing native
host_phys_max_ticrate10, while desktop retains native network cadence; both use
vid_vsync0/host_maxfps144. The all10Hz trial passed VR but correctly failed the
desktop short timer assertion: native jump_secs expires above50ms. Every probe
assertion is retained. [Bounded test plans](connected-crossplay-final-2.0-plan.md)
record these diagnostic assumptions; this is not default-VR-cadence or speed proof.

An initial metadata rebuild omitted three documented graphical wrappers and
reached an invalid-device wait in the headless fixture. Relinking with the full
existing recipe restored its intended renderer boundary; all six cases passed.
No production Vulkan change was warranted.

Private evidence: connected-current-private-final/{server.log,desktop.log,vr.log,
desktop-result.json,vr-result.json,connected-crossplay-result.json}; aggregate
logs/connected-current-private-final.log; early-scoreboard-host-build.log and
early-scoreboard-metadata-{build,run}-corrected.log. Diagnostic/failing roots and
logs remain separate. The runner stops only its children before deleting test
profiles; native shader/resource normal shutdown remains covered separately.

This closes simultaneous private desktop/VR gameplay at these boundaries, not
the whole F01 item: public/native peers, map changes/reused slots, loss/reordering/
splits/IPv6 and remaining metadata lifecycle/refusal still need their finite
cases. Voice, full avatars/equipment, gesture melee, hardware and performance
are not established by this probe. Native packaged artifacts still contain
ff83e66a production; required final input refresh belongs to F10 after fixes
settle. Main/reference/assets/user-owned migration document remain untouched.
