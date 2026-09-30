# Discovery and catalogue source checkpoint

2026-09-30, static source inspection only. Reconcile UI-001/UI-003 with actual
production owners before proposing more migration code. The inherited reference
is primary `51b452c0`; native vkQuake remains the filesystem/platform owner.
No store lookup, download, install, game launch or executable check was run.

| Requirement | Current production path | Remaining qualification |
| --- | --- | --- |
| Store discovery and explicit data selection | `COM_InitFilesystem` preserves explicit basedir, invokes `COM_FindStoreBaseDir` only at its existing discovery/explicit-store boundary, validates selected flavor, and retains existing classic/rerelease preference and folder selection. `Steam_FindGame/Steam_ResolvePath` parse library/app manifests and copy bounded paths before freeing their parser inputs. Linux platform discovery includes ordinary and Flatpak library locations. | Actual empty-working-directory, explicit root/preference, missing data and long/Unicode path cases on Linux/ARM. Store availability is not asserted from source. |
| Rerelease and user content priority | `COM_MountNightdiveUserDir` adds the existing add-on content root; filesystem initialization places main/user roots above extra roots. The separate optional localization pack and model-only rerelease mount retain their previously reviewed boundaries, keeping maps/progs out of the model fallback. Existing active game and user content remain primary. | Real classic/rerelease resources, add-ons, authored overrides and game changes. The flavor marker is not proof of complete data availability. |
| Native Unicode file operations | Linux `Sys_fopen/Sys_rename` use native filename bytes; Windows implementations use UTF-8-to-wide conversion and native wide file operations. The catalogue temporary-file path uses these same owners, including their existing parent creation. | Linux/ARM file operations with actual Unicode roots; Windows source is present but its builds/execution remain deferred. No replacement path layer. |
| Catalogue worker and validation | `AddonCatalog_AppendJSON` validates game/download names and bounded size/metadata. Existing HTTPS transfer, bounded memory/file callbacks, operation cancellation, temporary PACK validation, numbered-pack validation and commit/rename/result owners remain in `addon_catalog.c/common.c`. The catalogue has no authenticated digest field; its current entries remain unverified. | Actual refresh/error/cancel/retry, temporary cleanup, valid/invalid PACK and installed-state behavior. Validated paths/PACK structure do not establish content authenticity. |
| Browser confirmation and shared installer | The existing details page snapshots the displayed entry, shows installed/unverified state, and uses native keyboard/controller/pointer dispatch. The [small reuse adapter](catalogue-confirmation-reuse-2.0-plan.md) removes its duplicate comparison and uses the same approved-entry comparator and mutex-protected job start as server-mod install. Completion refreshes native installed/filter/selection owners. | Actual confirmation/refusal, installed selection, completion, feedback and input behavior; full UI-003/004 lifecycle/reconnect qualification. |

No new discovery service, installer, Unicode layer or mod menu is justified by
these inspected paths. This checkpoint records source integration/native reuse,
not completion of UI-001/UI-003 or arbitrary paths/catalogue entries. Builds and
software checks remain consolidated after all implementation, with device and
performance trials left to the user and Windows builds deferred.
