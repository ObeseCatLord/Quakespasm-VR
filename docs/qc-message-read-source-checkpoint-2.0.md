# Inherited CSQC message-read source checkpoint

2026-09-30. Bounded comparison of actual wrappers and registrations on2.0
against primary51b452c0, vkQuake4bc898f2 and QSS-M03a498aa. This checkpoint
closes source classification only for registercommand and reads360–368, not
the complete QC interface or executable acceptance.

Requested local Astra source audit found a demonstrated NULL dispatch defect
for registered commands; the separate [repair plan](qc-command-dispatch-2.0-plan.md)
records primary guard and QSS-M callback reuse. Main spot-checked registration,
native dispatch and both reference patterns. Callback execution, VM restoration
and source admission are not certified by the initial audit.

| Contract | Source evidence and disposition |
| --- | --- |
| readbyte/char/short/long/float | Native pr_ext.c:5912 onwards retains primary pr_cmds.c:3129 onwards wrappers returning MSG results through G_FLOAT. Keep native handlers and CSQC-only slots360–363/367. Shared float representation of readlong is not a new regression. |
| readcoord/readangle | Both wrappers use cl.protocolflags; native common.c:1560 onwards retains the supported coordinate/angle decoding branches. Keep native slots364–365 and decoder ownership. |
| readstring | Native wrapper uses PR_MakeTempString(MSG_ReadString()) at5938; its existing helper at107 implements native temporary storage. Native common.c:1520 consumes the full terminated source string even when bounded output fills, unlike the primary's early stop at2047bytes. Retain this vkQuake cursor behavior; don't copy a stream-alignment regression. |
| readentitynum | The actual registered contract at368 is a numeric entity identifier returned through G_FLOAT using cl.protocol_pext2, not a newly required entity-reference readentity alias. Native pr_ext.c:5960 and common.c:1592 retain the supported 16-bit/extended decode. |
| Truncated float | Native common.c:1479 checks remaining bytes before float decoding; primary common.c:936 lacks that check. Keep native malformed-message handling. |
| Registry permission and lifetime | Native read/registercommand rows retain CSQC handlers and forbidden SSQC dispatch. Runtime Cmd_AddCommand2 copies owned name storage. The audit does not establish full VM teardown correctness from that allocation alone. |

No additional P1/P2 defect was demonstrated in the scoped read family. This
does not claim outer transport/VM/temp-string lifetime or all error/permission
contracts are complete. Final Linux/ARM software checks after implementation
must cover mixed reads with a following field, coordinate/angle/entity modes,
numeric boundaries, truncated input and long strings. No builds, tests,
fixtures, engine probes or benchmarks were run. Effective reviewer settings
were unexposed: requested-Astra source advisory, not runtime certification.
