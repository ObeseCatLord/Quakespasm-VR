# Activate the existing inherited IPv4 ban owner

2026-09-30. Remaining inherited command behavior identified during the bounded
literal-command audit. Read-only primary pin51b452c0 enables BAN_TEST in
Quake/net_dgrm.c:23, initializes its native address/mask, registers ban at1698
and refuses matching IPv4 connection requests at1900. Native2.0 retains the
same handler/address/mask/admission code at357–405/1793–1818, but its feature
define is commented out and its command is not registered.

Reuse that existing native owner; no new ban list, address service, protocol,
runtime driver or moderation framework. Activate the retained path and register
the command. Preserve address/mask, query and off semantics plus existing
IPv4-only admission. This does not claim new IPv6 ban support. Desktop and VR
peers use the same connection boundary.

Make the handler explicitly console/RCON-only before mutation, as with the
new co-op admin commands; refuse without a local active server instead of
forwarding it as an ordinary player's command. Keep the existing server
query/connect/delta/async state untouched. This narrow authorization correction
does not replace the command dispatcher or network owner.

Write set: Quake/net_dgrm.c only, expected <=35 changed lines, including removal
of the now-unreachable player-print branch. Existing
guarded handler, initialization and IPv4 predicate remain reusable. Main
reviews against the real reference and native source; reopen for another
address/policy owner. Production remains on2.0, main/user-owned docs untouched.

After all implementation, Linux/ARM software checks cover registration,
console/RCON query/set/off, player-command refusal, no-server refusal,
matching/nonmatching IPv4 masks and unchanged IPv6 admission. Windows and
live multiplayer checks remain deferred. No tests/probes/builds are run now;
source activation alone does not complete networking or the migration goal.
