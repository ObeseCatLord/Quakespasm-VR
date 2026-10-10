# Quakespasm VR working instructions

## Workspace and branch

This checkout is the vkQuake-based engine. Work directly on `main`; it contains
all migrated `2.0` work. Keep `legacy1.0` unchanged and never reset or force-push
published branches without explicit user authorization. The existing `2.0`
download channel is independent of the source branch and remains unchanged.
Keep inherited vkQuake systems and reuse upstream code through narrow adapters
where possible. Read-only sibling checkouts can supply behavioral references.

Installed Quake and mod files are in
`/home/obesecatlord/Windows/Games/quakespasm_straight`. They are external test
assets, not release inputs. Preserve the launcher, wrappers, mods, saves and
configs. Back up executable files only when replacing an installed engine.

## Planning and delegation

Plan each major feature before implementing it. Compare a minimal adapter with
any proposed replacement, identify the behavioral reference, and keep working
adjacent systems. Reopen the design if it grows into duplicated state/policy.
Use the `senior-review` skill with local Astra at explicit `xhigh` for difficult
architecture decisions; record a recommendation/disposition table.

Use the main agent or GPT-5.6 Terra for substantive investigation and
implementation when delegation materially helps. All delegated reviews require
explicit gpt-6-astra at xhigh (or supported Astra Max/Ultra); fixed-model
deep-reviewer is for investigation/debugging only, never review. Give each a concrete objective, exact workspace and non-overlapping write
set, expected output, verification target and failure boundary. They are not
alone and must not revert others' changes. Keep final integration and review in
the main agent. Spark is limited to fully specified mechanical tasks; never use
it for analysis, debugging or behavioral implementation. Do not use ChatGPT web
models unless the user changes that instruction. Close completed agents.

## Privacy in public content

Never include links to `shrubdragon.studio` or any of its subdomains in GitHub
or any other public location. The launcher is for private use only: do not
mention, advertise, document or link to it in public-facing content.

Apply these rules to release notes, READMEs, issues, pull requests, comments,
websites and generated publication text. Keep private distribution endpoints
and launcher details in local configuration or private operational notes.
Review generated content before publishing; release automation must not expose
these private details.

## Build and release workflow

Use the repository's local release CLI and the commands in
`docs/release-automation-2.0.md`. Do not manually reconstruct platform build,
copy, packaging and publication commands for each update. The coordinator reuses
`Packaging/Linux/build-native.sh`, `build-foundry.sh`, `package.py`,
`publish-r2.py` and the native Windows MSBuild adapter. It is local automation;
GitHub Actions remain disabled.

- Finish implementation first, then run relevant end checks. Build Windows once
  at the end alongside Linux and native Linux ARM64, not after each small edit.
- Commit the engine changes on `main`, then prepare one immutable git archive and
  source manifest. All platform packages must identify that exact revision and
  archive checksum. Push the engine revision to `origin/main` before publication.
- Use a separate output directory per source revision. Resume only after
  verifying source identity and complete artifact inventories. Failed or partial
  builds are not successful receipts; never copy stale engine objects.
- Use the existing native ARM SSH build on `foundry`. Building there does not
  authorize changing or restarting the live server. Server deployment/mod
  changes require an explicit task and the server-admin workflow.
- Keep paths and machine-specific options in an ignored local configuration;
  credentials remain with SSH, rclone and gh. Never commit credentials or export
  Codex session telemetry. Avoid shell interpolation of untrusted paths.
- Stage and verify complete runtime libraries and component/source notices.
  Publish R2 immutable build objects before activating updater metadata. The
  updater channel is `2.0`; do not modify another channel.
- GitHub attachments are the three runtime packages and `SHA256SUMS`. Keep
  dependency source-access archives on R2, not as GitHub release attachments.
  Omit top-level READMEs from public runtime archives, retain required license
  notices, and generate checksums for the actual public archive bytes.
  Preserve the existing release tag when replacing assets and link the exact
  engine commit in notes. Verify published hashes and sizes.
- Straight deployment changes only the engine entry points and installs their
  complete matching runtime. Preserve launcher/wrapper hashes. An identical
  deployment should verify and skip, without another backup.
- Automation/docs-only commits do not justify rebuilding already qualified
  engine packages. Clearly record the published engine revision separately from
  a newer automation commit. Normal publication requires that revision to be the
  tip of `origin/main`, so finish publication before pushing later automation
  changes. An explicit public-only refresh of an already-published cohort may
  reuse its qualified packages after verifying release identity and asset
  digests; do not rebuild engines for documentation removal.

Actual headset presentation, eye tracking and hardware performance remain user
validation. Do not claim those from successful builds or simulated rendering.

## Project behavior

Desktop retains vkQuake graphics and normal desktop play. VR uses OpenXR; eye
tracking is optional and selected through foveation. Fixed foveation is explicit,
never enabled by default or used as an automatic fallback. Quad views, sky rooms,
gorilla locomotion, instant stop, co-op revival, developer imagedump and VR demo
support are outside the requested scope. Avoid new mod-specific exceptions when
a verified shared model/protocol behavior can handle the case.

## Repository hygiene

Use `rg` for searches. Inspect every delegated result, integrate it, and report
the relevant verification and remaining limitations. Commit coherent changes
regularly on `main`; never stage build outputs, local release configs or installed
game assets. Do not delete deployment folders, reset user changes or force-push
without explicit intent. Keep operational telemetry private and summarize it
when sharing outside the local machine.
