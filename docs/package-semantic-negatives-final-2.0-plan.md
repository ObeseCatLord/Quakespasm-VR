# F09 independent artifact checks

2026-10-01. Final software qualification only; production package verifier stays
unchanged. Previous negatives stopped at inventory equality. Use existing
verified current Linux package, existing pinned builder and private owned probe
root. Root-owned hardlinks avoid duplicating gigabytes; replace modified leaves
atomically, never mutate shared bytes. Original manifest/file hashes remain.

Create five separate copies: remove combined GPL notice; remove bin/vkquake;
append an inert line to retained options.txt; point libcurl.so.4 to the staged
SDL3 provider; change manifest architecture x86_64→aarch64. Recompute ONLY each
copy's inventory from actual scan. Preserve native receipt/provenance/source/
ELF/notice expectations. Run complete verifier and require nonzero with the
intended independent missing-artifact/receipt/provider/architecture diagnosis,
explicitly refusing inventory-mismatch results as insufficient evidence.

Use one network-disabled Docker invocation with only existing qualification
root bind-mounted. Keep all copies/logs under the new semantic-probes root.
Record expected and actual refusal. No rebuild, deployed service/config/assets
changes, receipt laundering, weakened checks or new verifier framework. Stop
on an unexpected accepted artifact or a refusal at an unrelated owner; diagnose
before deciding any test or production follow-up. Shared verifier behavior
covers both architectures; native ARM positive acceptance remains separately
established. This closes F09 only if all intended semantic boundaries execute.
