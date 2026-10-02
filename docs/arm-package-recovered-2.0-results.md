# Recovered native ARM package qualification

2026-10-02. Immutable2.0 archive b92f37415aa41d9321492d883a3fa4d7093eafce,
SHA2566ccab439c97b9ac02fdd801db735643d7e8de7f0d58dba8d6cbe073afdd33f1a.
Existing Foundry builder used native aarch64, Ubuntu24.04.5, GCC/G++13.3,
Meson1.3.2 and pinned dependency recipes. Enabled SDL3/Steam Audio/OpenXR loaders,
voice/codecs/CURL and all current Meson shader variants. No game launch/deployment.

Local root-space exhaustion interrupted the logging wrapper, leaving a truncated
log/empty status file. These are not passing build-exit receipts. Main inspected
actual remote installed executable and completed end-of-builder receipts after
confirming the original build container was no longer running; no restart from
an observation timeout. Product revision/archive hash match. Native receipts,
sources and artifacts all pass their actual recorded checksum sets.

Reuse the same image/package.py: stage actual native output into the existing
verified-empty scratch package, then verify with network disabled. Combined
recovered-hashes/stage/verify/tar exit0:45ELF files/118Ubuntu contributors/
651inventoried entries. Retrieved tar checksum matches:
96491b963760076a604f33cce48d20370744f2edaab303eedb4f1dca5340cda4,
1161748480bytes. Extraction0; retrieved manifest architecture/product agree.

Actual commands/logs/exit/transport/manifest summary and package retained in
FastGames/qsvr-arm-recovery-mgfh0avl. Remote isolated workspace retained.
No deployed server/checkouts, host drivers or settings changed. No live ARM
headset/eye/gameplay/performance proof. Later MSVC portability changes are outside
this archive: refresh matching final shipping artifacts before F10 closure.
