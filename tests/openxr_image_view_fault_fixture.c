/* Actual borrowed-view constructors with controlled metadata/Vulkan dispatch.
 * No driver, runtime image ownership, render-pass or framebuffer proof. */
#ifdef NDEBUG
#error "OpenXR view fixture requires assertions"
#endif
#define OPENXR_ENABLE_CUSTOM_IMAGE_VIEW_DESTROY
#define main ImageViewInheritedFixtureMain
#include "openxr_enable_fixture.c"
#undef main

viddef_t vid;
static vrxr_vulkan_eye_t source_images[3];
static VkImageView created_views[6], destroyed_views[6];
static VkImage created_images[6];
static unsigned created_count, destroyed_count, image_queries, create_calls;
static int failed_density_index;

void Sys_Error (const char *format, ...)
{
 fprintf (stderr, "unexpected constructor fatal: %s\n", format);
 abort ();
}

uint32_t VRXR_VulkanImageCount (int eye)
{
 assert (eye == 0);
 return 3;
}

int VRXR_GetVulkanImage (int eye, uint32_t index, vrxr_vulkan_eye_t *image)
{
 assert (eye == 0 && index < 3 && image);
 ++image_queries;
 *image = source_images[index];
 return 1;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView (VkDevice device,
 const VkImageViewCreateInfo *info, const VkAllocationCallbacks *allocator,
 VkImageView *out)
{
 assert (device == vulkan_globals.device && !allocator && info && out);
 assert (info->sType == VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO && !info->pNext);
 assert (info->viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY && !info->flags);
 assert (info->subresourceRange.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
  !info->subresourceRange.baseMipLevel && info->subresourceRange.levelCount == 1 &&
  !info->subresourceRange.baseArrayLayer && info->subresourceRange.layerCount == 2);
 ++create_calls;
 for (int i = 0; i < 3; ++i)
 {
  const qboolean density = info->image == source_images[i].density_image;
  if (!density && info->image != source_images[i].image) continue;
  assert (info->format == (density ? VK_FORMAT_R8G8_UNORM : source_images[i].format));
  if (density && i == failed_density_index)
  {
   *out = (VkImageView)(uintptr_t)999; /* Error output is not owned. */
   return VK_ERROR_FORMAT_NOT_SUPPORTED;
  }
  assert (created_count < countof (created_views));
  *out = (VkImageView)(uintptr_t)(301 + created_count);
  created_images[created_count] = info->image;
  created_views[created_count++] = *out;
  return VK_SUCCESS;
 }
 assert (!"unknown borrowed source image");
 return VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyImageView (VkDevice device,
 VkImageView view, const VkAllocationCallbacks *allocator)
{
 assert (device == vulkan_globals.device && !allocator && view);
 qboolean owned = false;
 for (unsigned i = 0; i < created_count; ++i) owned |= created_views[i] == view;
 assert (owned && destroyed_count < countof (destroyed_views));
 for (unsigned i = 0; i < destroyed_count; ++i) assert (destroyed_views[i] != view);
 destroyed_views[destroyed_count++] = view;
}

static VkImage view_source (VkImageView view)
{
 for (unsigned i = 0; i < created_count; ++i)
  if (created_views[i] == view) return created_images[i];
 assert (!"unknown retained image view");
 return VK_NULL_HANDLE;
}

static void run_case (int failure)
{
 assert (!openxr_image_views && !openxr_density_image_views && !openxr_image_count);
 created_count = destroyed_count = image_queries = create_calls = 0;
 failed_density_index = failure == 1 ? 1 : -1;
 openxr_density_backend_failed = false;
 vid.width = vid.height = 64;
 vulkan_globals.device = (VkDevice)(uintptr_t)101;
 vulkan_globals.openxr_fragment_density_map_max_texel_size = (VkExtent2D){16, 16};
 for (int i = 0; i < 3; ++i)
 {
  memset (&source_images[i], 0, sizeof (source_images[i]));
  source_images[i].image = (VkImage)(uintptr_t)(201 + i);
  source_images[i].format = VK_FORMAT_R8G8B8A8_SRGB;
  source_images[i].array_layers = 2;
  source_images[i].density_image = failure == 4 ? VK_NULL_HANDLE : (VkImage)(uintptr_t)(211 + i);
  source_images[i].density_width = source_images[i].density_height = 4;
 }
 if (failure == 2) source_images[1].density_image = VK_NULL_HANDLE;
 if (failure == 3) source_images[1].density_width = 3;

 GL_CreateXRImageViews ();
 assert (image_queries == 3 && openxr_image_count == 3 && openxr_image_views);
 for (int i = 0; i < 3; ++i)
  assert (openxr_image_views[i] && view_source (openxr_image_views[i]) == source_images[i].image);
 if (!failure)
 {
  assert (openxr_density_image_views && !openxr_density_backend_failed);
  assert (created_count == 6 && destroyed_count == 0 && create_calls == 6);
  for (int i = 0; i < 3; ++i)
   assert (openxr_density_image_views[i] &&
    view_source (openxr_density_image_views[i]) == source_images[i].density_image);
 }
 else
 {
  assert (!openxr_density_image_views);
  assert (openxr_density_backend_failed == (failure != 4));
  assert (created_count == (unsigned)(failure == 4 ? 3 : 4));
  assert (destroyed_count == (unsigned)(failure == 4 ? 0 : 1));
  assert (create_calls == created_count + (failure == 1));
  if (failure != 4) assert (destroyed_views[0] == created_views[1]);
 }
 GL_CreateXRImageViews ();
 assert (image_queries == 3 && create_calls == created_count + (failure == 1));
 GL_DestroyXRImageViews ();
 assert (!openxr_image_views && !openxr_density_image_views && !openxr_image_count);
 assert (destroyed_count == created_count);
 if (!failure)
  for (unsigned i = 0; i < 3; ++i)
  {
   assert (destroyed_views[i] == created_views[i * 2 + 1]);
   assert (destroyed_views[i + 3] == created_views[i * 2]);
  }
}

int main (void)
{
 for (int failure = 0; failure < 5; ++failure) run_case (failure);
 puts ("OPENXR_IMAGE_VIEW_FAULT_PASSED native color retention/density retirement; controlled dispatch");
 return 0;
}
