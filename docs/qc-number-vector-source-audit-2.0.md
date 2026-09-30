# Number/vector formatting source checkpoint

2026-09-30. Bounded requested-local-Astra/Max read-only advisory of native
`49810c45`, primary `51b452c0`, vkQuake `4bc898f2` and QSS-M `03a498aa`.
No demonstrated P1/P2 regression in ordinary valid calls was found. Retain
native helpers and formatter; this is not full QC or runtime certification.

## Scope and disposition

| Actual helper/contract | Verified evidence and disposition |
| --- | --- |
| `ftos`, `vtos` | Native `pr_cmds.c:1127/1148` retains primary `pr_cmds.c:1854/1875` integer-versus-one-decimal conversion and quoted three-component output. Keep native bounded `q_snprintf` instead of primary `sprintf`. |
| `stof`, `stov` | Native `pr_ext.c:1735/1739` and primary `pr_cmds.c:4952/4956` use the same `atof` and successive `COM_Parse` wrapper algorithms. Larger native parser capacity follows the existing separate string/token disposition. No second parser or stricter conversion wrapper. |
| `etos` | Native `pr_ext.c:1759` and primary `pr_cmds.c:4976` produce `entity %i` through `G_EDICTNUM`. Keep native temp-string ownership. |
| `sprintf` | Native `pr_ext.c:1028–1508` and primary `pr_cmds.c:4350–4726` retain positional/star argument selection, negative-width alignment, float-default `%d`, raw-int-default `%i`, zero-padded `%p/%P` and componentwise `%v/%V`. Both terminate bounded temporary output. No replacement formatting library. |
| Wider native formatting | Native `pr_ext.c:1052–1064/1232/1293` adds `q` double/int64 extraction and64-bit emission, matching native upstream/QSS-M design. Keep the improvement; primary32-bit destination overflow is not a valid ordinary parity target. |
| `%S` string construction | Native `pr_ext.c:1403–1454` bounds construction and escapes selected characters. Primary `pr_cmds.c:4651–4667` retains old length after substituting an empty source on troublesome input. Retain native implementation; this does not certify tokenizer round trips or all escaped text. |
| Registry rows | Core `ftos26`/`vtos27` remain in native server/client tables at `pr_cmds.c:2051/2157`. Native extension rows retain `etos65`, `stov117`, `sprintf627`. Native `stof81` has both VM handlers while primary registry lists CSQC only alongside SSQC debug slot81. Existing occupied-slot/name/permission disposition remains separate; this audit does not reopen resolver policy or infer blanket advertisement. |

Main spot-checks confirmed the bounded core outputs, identical conversion
algorithms,64-bit extraction/modifier, `%S` construction and registry difference
against actual source. Reviewer additionally compared the seven native function
bodies to pinned vkQuake and found them unchanged. No production edits are
justified by the scoped evidence.

Both implementations cast `ftos` input to `int` before checking equality.
Nonfinite/out-of-range input and formatter numeric/width casts retain preexisting
extreme-input concerns; undefined primary conversion is not evidence for a
compatibility rewrite. This checkpoint does not close error/permission behavior,
all registry entries, Unicode, temp-string lifetime or arbitrary numeric input.

## Final qualification

After all goal implementation finishes, qualify actual SSQC/CSQC ordinary
signed/fractional output, positional/star formats, raw integer input, vector and
entity output, conversion strings and output truncation on Linux/ARM. Wider
native formats are retained improvements with their own cases, not legacy
overflow expectations. Do not run probes now to manufacture a broader claim.

No builds, tests, compiler/engine probes, fixtures, benchmarks, nested delegation
or telemetry access. Effective reviewer settings are not exposed: requested
local Astra advisory only, not a certified senior-skill/model-setting pass.
Broader MOD-001/interface closure and final software qualification remain open.
