#ifndef QUAKE_VR_FOVEATION_RATE_MAP_H
#define QUAKE_VR_FOVEATION_RATE_MAP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "vr_foveation_policy.h"

enum {
	VRF_KHR_RATE_1X1 = 0,
	VRF_KHR_RATE_2X2 = 5,
	VRF_KHR_RATE_4X4 = 10
};

static inline int VRF_RateMapExtent (unsigned int scene_extent, unsigned int texel_extent, unsigned int *rate_extent)
{
	if (!scene_extent || !texel_extent || !rate_extent)
		return 0;
	*rate_extent = scene_extent / texel_extent + (scene_extent % texel_extent != 0);
	return *rate_extent != 0;
}

static inline int VRF_BuildRateMap (
	uint8_t *map, size_t map_capacity, unsigned int scene_width, unsigned int scene_height, unsigned int texel_width,
	unsigned int texel_height, unsigned int rate_width, unsigned int rate_height, unsigned int layers, int mode,
	const vrxr_view_t views[2], const vrxr_gaze_t *gaze, int supports_2x2, int supports_4x4)
{
	unsigned int expected_width, expected_height;
	if (!map || !VRF_RateMapExtent (scene_width, texel_width, &expected_width) ||
		!VRF_RateMapExtent (scene_height, texel_height, &expected_height) || rate_width != expected_width || rate_height != expected_height ||
		(layers != 1 && layers != 2))
		return 0;
	if ((size_t)rate_width > SIZE_MAX / rate_height || (size_t)rate_width * rate_height > SIZE_MAX / layers)
		return 0;
	const size_t tile_count = (size_t)rate_width * rate_height;
	const size_t map_size = tile_count * layers;
	if (map_capacity < map_size)
		return 0;
	memset (map, 0, map_size);
	if (mode == VRF_MODE_OFF)
		return 1;
	if ((mode != VRF_MODE_FIXED && mode != VRF_MODE_EYE_TRACKED) || !views || (!supports_2x2 && !supports_4x4))
		return 0;

	float directions[2][3];
	for (int eye = 0; eye < 2; ++eye)
		if (!VRF_EyeDirection (&views[eye], gaze, mode == VRF_MODE_FIXED, directions[eye]))
			return 0;

	for (unsigned int y = 0; y < rate_height; ++y)
	{
		const unsigned int y0 = y * texel_height;
		const unsigned int y1 = y0 + (scene_height - y0 < texel_height ? scene_height - y0 : texel_height);
		const float w = 1.0f - ((float)y0 + (float)y1) * 0.5f / scene_height;
		const float half_w = ((float)y1 - (float)y0) * 0.5f / scene_height;
		for (unsigned int x = 0; x < rate_width; ++x)
		{
			const unsigned int x0 = x * texel_width;
			const unsigned int x1 = x0 + (scene_width - x0 < texel_width ? scene_width - x0 : texel_width);
			const float u = ((float)x0 + (float)x1) * 0.5f / scene_width;
			const float half_u = ((float)x1 - (float)x0) * 0.5f / scene_width;
			unsigned char rates[2];
			for (int eye = 0; eye < 2; ++eye)
				rates[eye] = VRF_TileRate (&views[eye], directions[eye], u, w, half_u, half_w, 5.0f);

			const size_t tile = (size_t)y * rate_width + x;
			for (unsigned int layer = 0; layer < layers; ++layer)
			{
				const unsigned char rate_class = layers == 2 ? rates[layer] : (rates[0] < rates[1] ? rates[0] : rates[1]);
				const uint8_t encoded = rate_class == 0 ? VRF_KHR_RATE_1X1
					: rate_class == 1 ? (supports_2x2 ? VRF_KHR_RATE_2X2 : VRF_KHR_RATE_1X1)
					: supports_4x4 ? VRF_KHR_RATE_4X4 : VRF_KHR_RATE_2X2;
				map[(size_t)layer * tile_count + tile] = encoded;
			}
		}
	}
	return 1;
}

#endif
