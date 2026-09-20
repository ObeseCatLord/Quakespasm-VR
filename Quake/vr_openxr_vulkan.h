/* Vulkan renderer boundary for the existing OpenXR lifecycle. */
#ifndef QUAKE_VR_OPENXR_VULKAN_H
#define QUAKE_VR_OPENXR_VULKAN_H
#include <vulkan/vulkan_core.h>
#ifdef __cplusplus
extern "C" {
#endif

/* No SDL window/context or Vulkan device is required for discovery. Returned
 * versions use Vulkan's encoding, not XrVersion. Shutdown on abandoned setup. */
int VRXR_PrepareVulkan(void (*log_message)(const char *),
                       uint32_t *minimum_version, uint32_t *maximum_version);
/* These hooks preserve enable2 creation provenance. The renderer owns all
 * returned Vulkan handles, including any non-null output on failure. Both
 * wrappers use default/null allocation callbacks; destroy these instance/device
 * handles with null callbacks as well. */
int VRXR_CreateVulkanInstance(PFN_vkGetInstanceProcAddr get_proc,
                              const VkInstanceCreateInfo *info, VkInstance *instance);
VkPhysicalDevice VRXR_VulkanPhysicalDevice(VkInstance instance);
int VRXR_CreateVulkanDevice(const VkDeviceCreateInfo *info, VkDevice *device);
/* Must use a created graphics queue. retire_images is mandatory and is called
 * once on any attachment teardown (including partial failure): join recording/
 * readback work, retire GPU commands, and destroy renderer views/auxiliary
 * targets before returning. Do not destroy device/instance until after XR
 * shutdown returns. extra_image_usage may request TRANSFER_SRC, TRANSFER_DST
 * or SAMPLED usage in addition to color attachment. Callback must not reenter XR,
 * present via WSI, or longjmp out of cleanup.
 * Discard unsubmitted image references and wait for pending work before resetting
 * command pools. The renderer must externally synchronize all accesses to the
 * bound queue, including XR begin/end-frame and acquire/release calls, with
 * its existing queue mutex or equivalent scheduling. array_layers=2 selects one
 * stereo array swapchain (multiview target); 1 retains separate eye swapchains.
 * density_maps requests borrowed fragment-density-map images. It must remain
 * zero until the renderer has negotiated its image and pass requirements.
 * density_image_flags apply only to the runtime-owned color swapchain images;
 * they allow SUBSAMPLED and FRAGMENT_DENSITY_MAP_OFFSET. Nonzero flags require
 * density_maps and XR_META_vulkan_swapchain_create_info support. */
int VRXR_AttachVulkan(uint32_t queue_family, uint32_t queue_index,
                      VkImageUsageFlags extra_image_usage, uint32_t array_layers,
                      void (*retire_images)(void *), void *owner, int density_maps,
                      VkImageCreateFlags density_image_flags);

/* Discovery-only capability queries; no session or renderer attachment is
 * required. Eye support includes the runtime system property. */
int VRXR_VulkanFoveationSupported(void);
int VRXR_VulkanFoveationEyeSupported(void);
/* Runtime extension discovery only; this does not prove Vulkan device support. */
int VRXR_VulkanSwapchainImageFlagsSupported(void);
/* Attached-session format for pipeline warmup; no image is acquired/exposed.
 * Returns UNDEFINED before a completed attachment or after terminal loss. */
VkFormat VRXR_VulkanColorFormat(void);

/* Healthy VR disable: retire session images and destroy session resources,
 * retaining runtime discovery and the renderer-owned Vulkan device binding.
 * Idempotent. Terminal loss may require full teardown instead. Reenable uses
 * AttachVulkan explicitly; Shutdown still abandons the entire setup. */
void VRXR_DetachVulkan(void);

typedef struct {
  VkImage image;
  VkFormat format;
  uint32_t index, width, height, array_layer, array_layers;
  /* Additional flags requested by this adapter, not queried total image flags. */
  VkImageCreateFlags requested_color_image_flags;
  VkImage density_image;
  uint32_t density_width, density_height;
} vrxr_vulkan_eye_t;
/* During a renderable begun frame only. In array mode both eyes share the image
 * and acquired index, with distinct array_layer. In separate mode indices are
 * independent. Neither is a renderer frame-slot index. No ownership transfer. */
int VRXR_GetVulkanEye(int eye, vrxr_vulkan_eye_t *target);
/* Call after submitting ALL accesses to this image on the binding queue, with
 * final COLOR_ATTACHMENT_OPTIMAL layout. Then use common VRXR_EndFrame.
 * In array mode mark both eyes after the shared submission; release occurs once.
 * CPU waiting for each eye is unnecessary. Before VRXR_AbortFrame, discard any
 * unsubmitted recording that references acquired images; never submit it later. */
int VRXR_VulkanEyeSubmitted(int eye);
/* mode: 0 off, 1 explicit fixed, 2 eye with qualified_gaze supplied by the
 * established freshness/stability/user-option policy. Returns effective mode
 * 0/1/2, or -1 when restoration/frame state fails and the caller must abort.
 * Call once after BeginFrame acquired and waited every swapchain, after the
 * renderer fence wait and before commands reference XR images. centers is a
 * mandatory [left,right][x,y] OpenXR NDC output. It is zeroed for effective
 * modes 0/1 and all failures. A result of 2 guarantees a valid eye-tracked
 * profile and centers; the renderer must apply the appropriate density-map
 * offsets and negotiate all attachment/image flags before rendering mode 2.
 * This adapter does not enable QCOM or META image creation flags. */
int VRXR_UpdateVulkanFoveation(int mode, int qualified_gaze, float centers[2][2]);
#ifdef __cplusplus
}
#endif
#endif
