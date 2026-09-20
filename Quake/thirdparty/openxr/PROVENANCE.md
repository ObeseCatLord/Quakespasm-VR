# Vendored OpenXR headers

`openxr.h`, `openxr_platform.h`, and `openxr_platform_defines.h` are copied
unchanged from KhronosGroup/OpenXR-SDK `release-1.1.60`, commit
`64f2b37c8c6da3d83c9b4d11865ba1fb752cb8ec` (tag object
`4ce29375ec2cc8eaf06e93c63ff07a9a548e6d68`).  They are licensed under the
Apache-2.0 license in `LICENSE`.

`XR_MNDX_xdev_space.h` is copied from the selected Envision Monado checkout at
`3170a885507066615511a1782c5f6f3441243ee2`.  It is the exact experimental
preview ABI revision 3, including the 256-byte serial field.  Its BSL-1.0
notice is in `MONADO-BSL-1.0.txt`. `openxr_extension_helpers.h` is the matching
Monado helper with its include redirected to the adjacent vendored Khronos
header; no ABI declarations were changed. The backend enables this preview
extension only when the runtime advertises exactly revision 3.
