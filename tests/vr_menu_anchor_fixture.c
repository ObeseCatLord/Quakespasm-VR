#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../Quake/vr_menu_anchor.h"

static void near_value(float actual, float expected) {
  assert(fabsf(actual - expected) < 0.002f);
}

static void near_vec3(const float actual[3], const float expected[3]) {
  int i;
  for (i = 0; i < 3; ++i)
    near_value(actual[i], expected[i]);
}

static void test_initial_placement(void) {
  vr_menu_anchor_t state = {0};
  const float head[3] = {10, -20, 30};
  const float angles[3] = {60, 90, 12};
  float component = 64 * sqrtf(0.5f);

  assert(VR_MenuAnchorUpdate(&state, head, angles, 1.0, 1, 1, 64, 0));
  assert(state.valid && state.mode == 1);
  near_vec3(state.base, head);
  near_vec3(state.angles, (float[3]){45, 90, 0});
  near_vec3(state.center, (float[3]){10, -20 + component, 30 - component});
}

static void test_smooth_follow_and_same_time_repeat(void) {
  vr_menu_anchor_t state = {0};
  const float origin[3] = {0, 0, 0};
  const float moved_head[3] = {0, 32, 0};
  const float facing[3] = {0, 90, 0};
  float base_after_step[3], angles_after_step[3], center_after_step[3];
  double time_after_step;
  int moving_after_step;

  assert(VR_MenuAnchorUpdate(&state, origin, origin, 0.0, 1, 1, 64, 0));
  assert(VR_MenuAnchorUpdate(&state, moved_head, facing, 0.1, 1, 1, 64, 0));
  assert(!state.moving);
  assert(VR_MenuAnchorUpdate(&state, moved_head, facing, 0.61, 1, 1, 64, 0));
  assert(state.moving);
  assert(state.base[1] > 0 && state.base[1] < moved_head[1]);
  assert(state.angles[1] > 0 && state.angles[1] < facing[1]);
  assert(state.angles[0] == 0);

  near_value(state.base[1], moved_head[1] * (1 - expf(-4 * 0.05f)));
  near_value(state.angles[1], facing[1] * (1 - expf(-4 * 0.05f)));
  for (int i = 0; i < 3; ++i) {
    base_after_step[i] = state.base[i];
    angles_after_step[i] = state.angles[i];
    center_after_step[i] = state.center[i];
  }
  time_after_step = state.time;
  moving_after_step = state.moving;

  /* A duplicate same-state update has zero elapsed time and cannot smooth twice. */
  assert(VR_MenuAnchorUpdate(&state, moved_head, facing, 0.61, 1, 1, 64, 0));
  near_vec3(state.base, base_after_step);
  near_vec3(state.angles, angles_after_step);
  near_vec3(state.center, center_after_step);
  assert(state.time == time_after_step && state.moving == moving_after_step);
}

static void test_teleport_and_near_plane_resets(void) {
  vr_menu_anchor_t state = {0};
  const float origin[3] = {0, 0, 0};
  const float initial_angles[3] = {0, 0, 0};
  const float teleported[3] = {193, 0, 0};
  const float steep_angles[3] = {70, 30, 0};
  const float near_panel[3] = {49, 0, 0};

  assert(VR_MenuAnchorUpdate(&state, origin, initial_angles, 0.0, 1, 1, 64, 0));
  assert(VR_MenuAnchorUpdate(&state, teleported, steep_angles, 0.1, 1, 1, 64, 0));
  near_vec3(state.base, teleported);
  near_vec3(state.angles, (float[3]){45, 30, 0});

  state = (vr_menu_anchor_t){0};
  assert(VR_MenuAnchorUpdate(&state, origin, initial_angles, 0.0, 1, 1, 64, 0));
  assert(VR_MenuAnchorUpdate(&state, near_panel, initial_angles, 0.1, 1, 1, 64, 0));
  near_vec3(state.base, near_panel);
  near_vec3(state.center, (float[3]){113, 0, 0});
}

static void test_lost_tracking(void) {
  vr_menu_anchor_t state = {0};
  const float origin[3] = {0, 0, 0};
  const float next_head[3] = {10, 0, 0};

  assert(VR_MenuAnchorUpdate(&state, origin, origin, 0.0, 1, 1, 64, 0));
  assert(!VR_MenuAnchorUpdate(&state, origin, origin, 0.1, 0, 1, 64, 0));
  assert(!state.valid);
  assert(VR_MenuAnchorUpdate(&state, next_head, origin, 0.2, 1, 1, 64, 0));
  near_vec3(state.base, next_head);
  near_vec3(state.center, (float[3]){74, 0, 0});
}

int main(void) {
  test_initial_placement();
  test_smooth_follow_and_same_time_repeat();
  test_teleport_and_near_plane_resets();
  test_lost_tracking();
  puts("VR menu anchor fixture passed");
  return 0;
}
