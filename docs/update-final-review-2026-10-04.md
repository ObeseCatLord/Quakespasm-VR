# Final update review dispositions

Scope: vkQuake upstream merge `749b4fd4`, rectangular texture mip tails,
and the original Bonk Jam hammer gesture adapter. The final senior review used
Astra with xhigh reasoning and examined implementation commit `78819be5`.
It did not replace runtime qualification or physical-headset testing.

| Finding | Disposition |
| --- | --- |
| Bonk native callback falsely fails the enclosing program guard | Adopted. The existing subtype guard now checks the exact Bonk program, retaining the VM, globals, player, cursor, selected-weapon, and freshness checks. No additional contact or movement owner was introduced. |
| Successful leaf-level hammer outcomes are insufficient acceptance | Adopted. Original-QC fixtures must run the production codec, queued contact commands, and callback completion; successful hits and settled whiffs must advance the contact sample normally. |
| Retain native save, reliable signon, and per-eye AO ownership | Adopted. Keep the bounded existing workers, message-lifetime budget, and independent eye/resolution descriptors. Qualify those boundaries with final integrated fixtures rather than another implementation. |
| Physical-headset visuals and swing feel remain unknown | Accepted limitation. Simulated OpenXR and native GPU checks verify software paths; they do not certify Steam Frame image quality or controller feel. |

The main integrator independently confirmed the omitted Bonk subtype and the
resulting false QBJ3 program check before fixing it in `1f2c44ec`. The fix passed
the strict assertion-enabled Clang build. The initial sequential reconnect test
used a single-slot server; separate fresh-server ordinary and `udp://` runs
passed full signon, movement, prediction, and firing. This was a test setup
failure, not evidence requiring a transport rewrite.

GPU qualification also exposed a partial-tile SSAO subgroup discrepancy. That
is a separate shader acceptance finding; preserve the scalar oracle and its
existing tolerances, and fix the demonstrated boundary rather than weakening
the test. Final qualification results are recorded separately.
