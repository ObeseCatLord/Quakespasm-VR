#ifndef QUAKE_VR_FOVEATION_POLICY_H
#define QUAKE_VR_FOVEATION_POLICY_H

#include "vr_foveation_math.h"

enum {
  VRF_MODE_OFF = 0,
  VRF_MODE_FIXED = 1,
  VRF_MODE_EYE_TRACKED = 2,
  VRF_EYE_STABLE_FRAMES_REQUIRED = 3
};

typedef struct {
  unsigned int stable_frames;
} vrf_policy_state_t;

/* Cvar values arrive as floating point. Accept only the three exact integral
 * modes; NaN, infinity, fractions, and out-of-range values fail closed. */
static inline int VRF_RequestedMode(double requested_mode) {
  if (!isfinite(requested_mode)) return VRF_MODE_OFF;
  if (requested_mode == VRF_MODE_OFF) return VRF_MODE_OFF;
  if (requested_mode == VRF_MODE_FIXED) return VRF_MODE_FIXED;
  if (requested_mode == VRF_MODE_EYE_TRACKED) return VRF_MODE_EYE_TRACKED;
  return VRF_MODE_OFF;
}

static inline int VRF_EyeTrackingEnabled(double enabled) {
  return isfinite(enabled) && enabled > 0.0;
}

static inline void VRF_ResetPolicy(vrf_policy_state_t *state) {
  if (state) state->stable_frames = 0;
}

/* Call once per display frame. Eye mode requires three consecutive focused
 * frames with enabled tracking and a valid, tracked gaze ray no older than
 * 50ms; expressed-pose times up to 50ms in the future are accepted. Any gap
 * resets acquisition. Eye mode never degrades to fixed mode. */
static inline int VRF_SelectMode(vrf_policy_state_t *state,
    double requested_mode, double eye_tracking_enabled,
    const vrxr_frame_t *frame) {
  const int mode = VRF_RequestedMode(requested_mode);
  const int tracking_enabled = VRF_EyeTrackingEnabled(eye_tracking_enabled);
  if (!state) return VRF_MODE_OFF;
  if (state->stable_frames > VRF_EYE_STABLE_FRAMES_REQUIRED)
    state->stable_frames = 0;

  const int eye_frame_ready = mode == VRF_MODE_EYE_TRACKED &&
      tracking_enabled && frame && frame->should_render == 1 &&
      frame->focused == 1 &&
      VRF_GazeUsable(&frame->gaze, 0.05);
  if (!eye_frame_ready)
    state->stable_frames = 0;

  if (mode == VRF_MODE_OFF) return VRF_MODE_OFF;
  if (mode == VRF_MODE_FIXED)
    return frame && frame->should_render == 1 && frame->focused == 1
               ? VRF_MODE_FIXED : VRF_MODE_OFF;
  if (!eye_frame_ready) return VRF_MODE_OFF;

  if (state->stable_frames < VRF_EYE_STABLE_FRAMES_REQUIRED)
    ++state->stable_frames;
  return state->stable_frames >= VRF_EYE_STABLE_FRAMES_REQUIRED
             ? VRF_MODE_EYE_TRACKED : VRF_MODE_OFF;
}

#endif
