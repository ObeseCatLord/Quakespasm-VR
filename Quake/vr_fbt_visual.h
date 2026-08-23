#ifndef VR_FBT_VISUAL_H
#define VR_FBT_VISUAL_H

/*
 * Calibration/debug drawing for full-body trackers.  The caller converts
 * OpenVR tracking coordinates to Quake world coordinates before calling
 * Prepare: world_axis[n] is the world-space displacement for one tracker-
 * local metre along n.  This keeps coordinate conversion and pose sampling
 * out of the per-eye renderer.
 */

#include <stdint.h>

#include "vr_fbt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VR_FBT_VISUAL_INVALID_DEVICE 0xffffffffU

typedef struct {
	vr_fbt_role_t role;
	unsigned int device_index;
	int draw_tracker;
	int draw_target;
	int draw_offset_line;
	float tracker_origin[3];
	float tracker_axis[3][3];
	float target_origin[3];
	float target_axis[3][3];
} vr_fbt_visual_packet_t;

/*
 * openvr_system and openvr_render_models are borrowed IVRSystem and
 * IVRRenderModels pointers respectively.  They are void pointers here so
 * this public seam remains usable by non-OpenVR tests.  Prepare never polls
 * OpenVR poses or events; it only may advance a cached render-model load.
 */
typedef struct {
	uint64_t host_frame_id;
	const void *openvr_system;
	const void *openvr_render_models;
	const vr_fbt_visual_packet_t *packets;
	unsigned int packet_count;
} vr_fbt_visual_prepare_t;

/* Call exactly once per host frame, before rendering either eye. */
void VR_FBT_VisualPrepare(const vr_fbt_visual_prepare_t *prepare);

/* Drop copied packets immediately; retained host-frame IDs must not keep a
 * calibration/debug overlay alive after FBT has been disabled. */
void VR_FBT_VisualClearPackets(void);

/* Call after the view model for each eye.  It only draws copied packets. */
void VR_FBT_VisualDraw(void);

/* Call while the GL/OpenVR context is still valid, before VR_Shutdown. */
void VR_FBT_VisualShutdown(const void *openvr_render_models);

#ifdef __cplusplus
}
#endif

#endif /* VR_FBT_VISUAL_H */
