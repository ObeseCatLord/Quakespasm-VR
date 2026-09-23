#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "vr_foveation_rate_map.h"

static vrxr_view_t fixture_view (void)
{
	vrxr_view_t view;
	memset (&view, 0, sizeof (view));
	view.matrix[0][0] = view.matrix[1][1] = view.matrix[2][2] = 1.0f;
	view.left = view.down = -1.0f;
	view.right = view.up = 1.0f;
	return view;
}

static vrxr_gaze_t fixture_gaze (float x, float y, float z)
{
	vrxr_gaze_t gaze;
	memset (&gaze, 0, sizeof (gaze));
	gaze.valid = gaze.tracked = gaze.sample_time_known = 1;
	gaze.direction[0] = x;
	gaze.direction[1] = y;
	gaze.direction[2] = z;
	return gaze;
}

int main (void)
{
	unsigned int extent;
	assert (VRF_RateMapExtent (5, 2, &extent) && extent == 3);
	assert (!VRF_RateMapExtent (0, 2, &extent));

	vrxr_view_t views[2] = {fixture_view (), fixture_view ()};
	vrxr_gaze_t gaze = fixture_gaze (0.0f, 0.0f, -1.0f);
	uint8_t map[2 * 3 * 12];
	assert (VRF_BuildRateMap (map, sizeof (map), 3, 12, 1, 1, 3, 12, 2, VRF_MODE_FIXED, views, NULL, 1, 1));
	assert (map[6 * 3 + 1] == VRF_KHR_RATE_1X1);
	assert (map[6 * 3 + 0] == VRF_KHR_RATE_4X4);
	assert (map[3 * 12 + 6 * 3 + 1] == VRF_KHR_RATE_1X1);

	/* OpenXR row zero is the top of the image and maps to the top of eye FOV. */
	gaze = fixture_gaze (0.0f, 0.70710678f, -0.70710678f);
	assert (VRF_BuildRateMap (map, sizeof (map), 1, 12, 1, 1, 1, 12, 2, VRF_MODE_EYE_TRACKED, views, &gaze, 1, 1));
	assert (map[0] == VRF_KHR_RATE_1X1);
	assert (map[11] == VRF_KHR_RATE_4X4);

	/* A partial edge tile uses its covered pixels and the one-layer map keeps
	 * whichever eye requests the finer rate. */
	views[1].left = -2.0f;
	views[1].right = 1.0f;
	uint8_t layered[2 * 3], merged[3];
	assert (VRF_BuildRateMap (layered, sizeof (layered), 5, 1, 2, 1, 3, 1, 2, VRF_MODE_FIXED, views, NULL, 1, 1));
	assert (VRF_BuildRateMap (merged, sizeof (merged), 5, 1, 2, 1, 3, 1, 1, VRF_MODE_FIXED, views, NULL, 1, 1));
	for (int tile = 0; tile < 3; ++tile)
		assert (merged[tile] <= layered[tile] && merged[tile] <= layered[3 + tile]);

	/* A middle-ring tile stays full rate when the device lacks 2x2, even if
	 * 4x4 is available for tiles farther from gaze. */
	uint8_t no_2x2_map[21 * 21];
	views[0] = views[1] = fixture_view ();
	assert (VRF_BuildRateMap (no_2x2_map, sizeof (no_2x2_map), 21, 21, 1, 1, 21, 21, 1,
		VRF_MODE_FIXED, views, NULL, 0, 1));
	assert (no_2x2_map[10 * 21 + 12] == VRF_KHR_RATE_1X1);

	memset (map, 0xff, sizeof (map));
	assert (VRF_BuildRateMap (map, sizeof (map), 3, 12, 1, 1, 3, 12, 2, VRF_MODE_OFF, NULL, NULL, 0, 0));
	for (size_t i = 0; i < sizeof (map); ++i)
		assert (map[i] == 0);
	assert (!VRF_BuildRateMap (map, sizeof (map) - 1, 3, 12, 1, 1, 3, 12, 2, VRF_MODE_FIXED, views, NULL, 1, 1));
	for (size_t i = 0; i < sizeof (map); ++i)
		assert (map[i] == 0);
	return 0;
}
