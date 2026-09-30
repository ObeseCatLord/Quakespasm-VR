# Remaining small QC cvar/buffer contracts

The current comparison found these concrete source differences. Keep native
cvar/buffer services and adapt only their wrappers in `Quake/pr_ext.c`.

| Call | Verified reference | Current native difference | Adapter |
| --- | --- | --- | --- |
| `registercvar` 93 | Primary `Quake/pr_cmds.c:1730` reads argument 1 as the default (or `"0"` when omitted), then returns whether native `Cvar_Create` returned a variable. QSS-M also reads argument 1, though it does not assign the return. | Destination reads argument 0 again as the default and leaves the return stale. | Copy the primary wrapper. Keep native existing-variable/no-overwrite, command-conflict and allocation/registration behavior. |
| `bufstr_add` 468 | Primary initializes float return to -1 before rejecting a missing/foreign buffer. | Destination rejects without assigning its float return. | Initialize -1 before existing handle checks; keep native successful allocation/append/hole behavior. |
| `buf_cvarlist` 517 | Primary treats omitted argument 2 as an empty exclusion pattern and resets the shared sort-prefix length before sorting the resulting names. | Destination always reads argument 2 and sorts with whichever global prefix the prior buffer sort left behind. | Copy the optional empty pattern and reset the prefix to the native full-name value `0x7fffffff` before the existing sort. Keep native pattern matching, cvar enumeration and buffer ownership. |

This is a routine boundary correction, not a service/registry rewrite. Names,
numbers, VM permissions, capability policy and cvar callbacks do not change.
No new helper/state, buffer growth limit, index policy or renderer work. Both
VMs keep native current-owner resource guards from the preceding slice.

Plan precedes code. Main copies the verified primary lines; bounded local Astra
source review covers the three changed wrappers and immediate native owner
contracts. Builds/tests/compiler/runtime probes remain deferred until all
implementation finishes. Final Linux/ARM QC qualification must check supplied
and omitted cvar defaults, status for new/existing/command-conflicting names,
unchanged existing values, rejected buffer-add -1, successful native append,
optional exclusion patterns and sorting after a prior short-prefix buffer sort.
These cases remain distinct from complete ABI/mod acceptance.
