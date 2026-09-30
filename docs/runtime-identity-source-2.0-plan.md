# Runtime identity on the vkQuake base

2026-09-30. Bounded UI-006 source disposition before the metadata edit.
The branch name2.0 is not a claim of a finished release or a version bump.

Keep vkQuake's executable/build/resource names, version header, config name and
native write/save lookup owners. Current `meson.build`, `quakever.h`,
`CONFIG_NAME`, embedded `vkquake.pak` and `COM_FOpenConfigFile` already express
that base. Imported voice settings use their existing native vkQuake preference
location and separate profile file. Do not add legacy setting aliases, automatic
old-config import, a second preference root or a branding rewrite. Useful weapon
offset import remains at its existing shared parser/calibration boundary.

The OpenXR application name remains `Quakespasm VR`, preserving the project's
runtime identity. Its imported `engineName="Quakespasm"` is stale after the base
migration. Correct that one literal to `vkQuake` in `vr_openxr.cpp`. Retain the
existing version integers: assigning a release version is outside this source
correction, and neither integer certifies the current software. No engine
headers/types or runtime/session behavior need to be introduced into this
backend translation unit. Expected one replaced production line, one file.

At final Linux/ARM qualification, inspect actual XR instance metadata, executable
and embedded resource packaging, current native configs/defaults and profile/
save lookup, including ordinary desktop without a runtime. Windows builds and
live runtime behavior are deferred. No builds/tests/probes now; main reviews
the literal diff and source identity producers before committing.

The one-line engine-name correction is source-integrated and main-reviewed.
Application identity and versions retain their existing values; no header,
config or lifecycle owner changed. Scoped whitespace checking passed; final
metadata/config/package software qualification remains pending.
