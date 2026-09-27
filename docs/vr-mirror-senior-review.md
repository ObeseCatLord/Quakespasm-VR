# OpenXR desktop mirror: design review and implementation boundary

Branch `2.0`, 2026-09-27. Behavioral reference: the inherited OpenVR
`vr_mirror` setting (`0` off, `1` left eye by default, `2` right eye), copying
the selected completed eye after world and UI rendering. This is a partial
Astra senior review of the proposed Vulkan/OpenXR mirror. No headset or WSI
timing was measured, and the mirror itself is not implemented yet.

The narrow design reuses vkQuake's Vulkan WSI swapchain and the existing XR
array swapchain. It does not add a second renderer. XR owns eye images; WSI
owns the desktop image. The mirror may copy the selected, composed XR eye only
when the runtime accepts transfer-source usage, the WSI target supports the
required transfer-destination usage, both formats support the chosen copy or
blit, and image creation flags permit the operation. Failed or unavailable
desktop presentation must leave XR rendering active.

| Review finding | Disposition |
| --- | --- |
| `GL_EndXRFrame` joins `GL_EndRenderingTask` before releasing XR images. If that task also calls `vkQueuePresentKHR`, a blocking desktop present delays `xrEndFrame`. | Keep presentation outside the joined XR submission path; submit all XR image accesses before release, then present the desktop image separately. |
| `vkAcquireNextImageKHR` with zero timeout can return an image whose acquire semaphore has not signaled yet. A direct XR-to-WSI copy can therefore delay XR completion. | Prove the acquired image is ready before an XR image depends on it, or submit a copy to an application-owned snapshot image, release/end XR, then copy/present to WSI. Compare the latter's extra copy and memory cost before selecting it. |
| An optional mirror transfer-source request could fail in `xrCreateSwapchain` and trigger the density-map fallback first. | Implemented in the OpenXR binding: on `XR_ERROR_FEATURE_UNSUPPORTED` for a swapchain with optional transfer source, retry without that usage under the same density profile. The accepted-usage query lets the renderer disable only the mirror. Other errors retain the previous failure path. |
| Current WSI creation compares surface dimensions with XR eye dimensions and skips WSI entirely during stereo. | Parameterize WSI by actual drawable size and preserve the existing WSI lifetime, semaphore and per-image ownership rules. Suspend when the drawable extent is zero. WSI resize must not recreate the XR session. |
| Foveated/subsampled XR images, format blit capabilities, orientation, and color-space conversion may differ by runtime. | Gate the copy path on concrete image and surface capabilities. Preserve left/right selection and UI inclusion; validate presentation orientation and color on a live runtime. |

Smallest end-to-end proof: `vr_mirror 1` displays the completed left eye on a
desktop window while XR remains responsive; `0` suppresses desktop work and
`2` selects the right eye. Repeat with foveation enabled, window minimization,
resize, transfer-source rejection, WSI out-of-date/surface loss, and a runtime
without transfer-source support. Check frame timing and synchronization with
Vulkan validation; a build or mocked runtime alone does not establish parity.

Relevant specifications: [OpenXR swapchain creation](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrCreateSwapchain.html),
[OpenXR Vulkan swapchain usage mapping](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_KHR_vulkan_enable-swapchain-flags.html),
[Vulkan WSI acquisition and presentation](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html),
and [Vulkan blit requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html).
