# Core builtin-name discovery

## Verified reference and boundary

- Primary `Quake/pr_cmds.c:2733` resolves `builtin_find` case-insensitively
  against `pr_builtindefs`, including the ordinary Quake names at slots 1–78.
  It reports registered numbers, not VM permission or callability. Primary
  `Quake/pr_edict.c:1534` also binds exact-name, otherwise empty `#0`
  declarations to those numbers.
- Destination `Quake/pr_ext.c` already owns extension-name discovery and
  exact-name `#0` binding. Ordinary names such as `random` and `dprint` are
  absent from that registry. `Quake/pr_cmds.c` owns the native SSQC/CSQC handler
  arrays; `Quake/pr_edict.c` copies them before enabling extensions.
- Native core handlers and permissions differ from primary in some CSQC slots.
  Keep those vkQuake behaviors. Name resolution must not install a handler or
  turn an unsupported slot into a supported one.
- Existing extension names and their assigned numbers take precedence. In
  particular, preserve the reviewed SSQC `localsound` normalization and native
  extension aliases. Native slot 51 already receives extended `vectoangles2`;
  resolving `vectoangles` to 51 should use that existing handler.

## Small adapter

Copy only the primary core **name/number metadata**, for names whose ordinary
slots are present in the native tables (1–78). Add the native lower-case
`changeyaw` spelling alongside primary `ChangeYaw`, and primary `cvar_setlong`
alias 72. Keep this immutable metadata private in `Quake/pr_ext.c`; it contains
no function pointers, mutable mapping or second resource/dispatch owner.

After the existing extension lookup fails, `builtin_find` uses this metadata
with case-insensitive matching. After the existing exact-name extension `#0`
binding fails, the same helper uses exact matching and supplies only the numeric
reference. Native `PF_Fixme`, CSQC exclusions, extension enable ordering and
the existing SSQC startup-disable early return remain authoritative. An
unavailable core name may have a discoverable number, as in primary; use native
`checkbuiltin` for callability. Numeric declarations and real QC bodies are
unchanged. All duplicate empty declarations are visited by the native scan.

Do not copy the primary re-release debug drawing names at 81–89: their primary
handlers are already `PF_Fixme`, and those numbers are occupied by unrelated
native extensions. Existing reviewed re-release adapters remain separate.
The native `finaleFinished` contract at 79 needs a separate verified boundary
comparison if name discovery there is required; this slice covers ordinary
Quake core calls, not an inferred debug drawing implementation.

Replacing both native handler arrays with primary's unified registry would
duplicate ownership and risk desktop/CSQC behavior. Adding wrappers for each
core call would duplicate existing functions. Both are rejected in favor of
the small metadata lookup and two fall-throughs.

## Scope and acceptance

Production ownership: `Quake/pr_ext.c` only. Plan precedes edits. Bounded local
Astra source review must compare every copied pair with primary/native tables,
extension precedence, exact versus insensitive matching, alias handling,
empty/unknown results, `#0` eligibility, duplicate declarations, permissions
and the startup-disabled SSQC boundary. No renderer or networking changes.

Builds/tests/compiler probes remain deferred until implementation is finished.
Final Linux/ARM qualification must exercise `builtin_find("random") == 7`,
case variants, `dprint` 25, `ChangeYaw`/`changeyaw` 49, `cvar_setlong` 72,
native extension names and normalized SSQC localsound; named core `#0`
invocation must match the corresponding numeric handler. Unsupported CSQC core
calls must retain native rejection. Unknown names and real QC bodies must not
be remapped; disabled SSQC setup must retain its program-reload requirement.
