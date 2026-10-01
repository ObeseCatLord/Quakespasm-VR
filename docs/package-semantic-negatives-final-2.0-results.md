# F09 independent artifact rejection results

2026-10-01. All five planned cases pass their intended independent rejection
boundary. Current verified ff83e66a Linux package and pinned builder image;
current production/package inputs remain equivalent. No verifier changes or
new builds. Network-disabled container, owned semantic-probes copies under the
existing qualification root. Existing native verifier is imported only for
scan/digest; each case executes the full package.py verify command.

Each copied manifest inventory is recomputed consistently after mutation. All
other retained expectations remain. No case fails at inventory equality.

| Negative | Actual full-verifier refusal (exit1) |
| --- | --- |
| Removed combined GPL notice | installed native artifact is missing: share/licenses/vkquake-vr/LICENSE-GPL-3.0.txt |
| Removed executable ELF | installed native artifact is missing: bin/vkquake |
| Changed retained options bytes | receipt receipt file set or hash differs |
| libcurl alias points to staged SDL3 ELF | DT_NEEDED does not resolve to a matching internal ELF: bin/vkquake: libcurl.so.4 |
| x86package claims aarch64 | deb architecture differs from artifact: binutils |

Changed bytes/manifests replace private leaves atomically; unchanged hardlinks
share original data. Main reviewed the complete scratch script. Selected original
notice/executable/options/manifest hashes and curl alias match before/after.
Exact private aggregate logs/package-semantic-negatives-current.log contains
five SEMANTIC_NEGATIVE_PASSED markers plus the original-hash marker; per-case
logs and reproducible run.py remain under semantic-probes. Final command exit0.

An initial Docker invocation used the image's default build entrypoint and was
refused before probes. The corrected invocation explicitly selects python3.
The first actual run stopped after the retained-options refusal because the
diagnostic assertion expected a different wording; the intended receipt owner
had correctly refused. Correcting only that expected literal and rerunning all
five cases passed. Initial log retained; no production check weakened.

F09 is complete within the frozen checklist: existing Linux/ARM positive ABI/
resource/source/notice/dependency/loader results plus common independent semantic
negative coverage. This does not claim runtime headset/provider compatibility
or adversarially self-consistent fabricated provenance detection. Native ARM
positive evidence remains separate; shared verifier semantics are not an ARM
GPU/runtime test. A future production/package input change requires affected
rebuild/restaging and relevant verification.
