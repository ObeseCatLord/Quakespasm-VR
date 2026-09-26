#include <assert.h>
#include <math.h>
#include <string.h>

#include "vr_foveation_policy.h"

static vrxr_gaze_t usable_gaze(void) {
  vrxr_gaze_t gaze;
  memset(&gaze, 0, sizeof(gaze));
  gaze.valid = gaze.tracked = gaze.sample_time_known = 1;
  gaze.direction[2] = -1;
  return gaze;
}

int main(void) {
  vrf_policy_state_t state = {0};
  vrf_policy_state_t runtime_state = {0};
  assert(!VRF_AdvanceEyeStability(&runtime_state, 1));
  assert(!VRF_AdvanceEyeStability(&runtime_state, 0));
  assert(runtime_state.stable_frames == 0);
  assert(!VRF_AdvanceEyeStability(&runtime_state, 1));
  assert(!VRF_AdvanceEyeStability(&runtime_state, 1));
  assert(VRF_AdvanceEyeStability(&runtime_state, 1));
  assert(VRF_AdvanceEyeStability(&runtime_state, 1));
  assert(!VRF_AdvanceEyeStability(&runtime_state, 0));
  assert(runtime_state.stable_frames == 0);
  vrxr_frame_t frame;
  memset(&frame, 0, sizeof(frame));
  frame.should_render = 1;
  frame.focused = 1;
  frame.gaze = usable_gaze();

  assert(VRF_RequestedMode(0) == VRF_MODE_OFF);
  assert(VRF_RequestedMode(1) == VRF_MODE_FIXED);
  assert(VRF_RequestedMode(2) == VRF_MODE_EYE_TRACKED);
  assert(VRF_RequestedMode(1.5) == VRF_MODE_OFF);
  assert(VRF_RequestedMode(NAN) == VRF_MODE_OFF);
  assert(VRF_RequestedMode(INFINITY) == VRF_MODE_OFF);
  assert(VRF_EyeTrackingEnabled(2));
  assert(!VRF_EyeTrackingEnabled(NAN));

  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 1);
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_EYE_TRACKED);
  assert(VRF_SelectMode(&state, 2, 0, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 0);
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  frame.focused = 0;
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(VRF_SelectMode(&state, 1, 0, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 0);
  frame.focused = 1;
  frame.gaze = usable_gaze();
  frame.gaze.sample_age_seconds = -0.05;
  assert(VRF_GazeUsable(&frame.gaze, 0.05));
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 1);
  frame.gaze.sample_age_seconds = 0.050001;
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 0);
  frame.gaze = usable_gaze();
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 1);
  frame.gaze.sample_age_seconds = -0.050001;
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 0);
  frame.gaze = usable_gaze();
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 1);
  frame.gaze.sample_age_seconds = 0.05;
  assert(VRF_GazeUsable(&frame.gaze, 0.05));
  frame.gaze.tracked = 0;
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 0);
  frame.gaze = usable_gaze();
  frame.should_render = 0;
  assert(VRF_SelectMode(&state, 2, 1, &frame) == VRF_MODE_OFF);
  assert(VRF_SelectMode(&state, 1, 0, &frame) == VRF_MODE_OFF);
  assert(state.stable_frames == 0);
  frame.should_render = 1;
  assert(VRF_SelectMode(&state, 1, 0, &frame) == VRF_MODE_FIXED);
  assert(VRF_SelectMode(&state, 2, 0, &frame) == VRF_MODE_OFF);
  assert(VRF_SelectMode(&state, 3, 1, &frame) == VRF_MODE_OFF);

  vrxr_view_t view;
  memset(&view, 0, sizeof(view));
  view.matrix[0][0] = view.matrix[1][1] = view.matrix[2][2] = 1;
  view.left = view.down = -1;
  view.right = view.up = 1;
  float direction[3];
  assert(VRF_EyeDirection(&view, &frame.gaze, 0, direction));
  assert(direction[0] == 0 && direction[1] == 0 && direction[2] == -1);
  /* Rotate eye +90 degrees about Y. App-space forward is then (-1,0,0);
   * the transpose must recover the eye-space forward ray. */
  view.matrix[0][0] = 0;
  view.matrix[0][2] = 1;
  view.matrix[2][0] = -1;
  view.matrix[2][2] = 0;
  frame.gaze.direction[0] = -1;
  frame.gaze.direction[2] = 0;
  assert(VRF_EyeDirection(&view, &frame.gaze, 0, direction));
  assert(direction[0] == 0 && direction[1] == 0 && direction[2] == -1);
  view.matrix[0][0] = view.matrix[2][2] = 1;
  view.matrix[0][2] = view.matrix[2][0] = 0;
  frame.gaze = usable_gaze();
  assert(VRF_TileRate(&view, direction, 0.5f, 0.5f, 0.01f, 0.01f, 5) == 0);
  assert(VRF_TileRate(&view, direction, 1, 0.5f, 0.01f, 0.01f, 5) == 2);
  assert(VRF_TileRate(&view, direction, 0.5f, 0.5f, 0.01f, 0.01f, NAN) == 0);
  view.right = NAN;
  assert(!VRF_EyeDirection(&view, &frame.gaze, 0, direction));
  assert(VRF_TileRate(&view, direction, 0.5f, 0.5f, 0.01f, 0.01f, 5) == 0);

  return 0;
}
