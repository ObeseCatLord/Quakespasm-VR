/* Exercise the reusable inherited VR aim arithmetic directly. */
#include "../Quake/vr_aim.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

static void near_value(float actual, float expected)
{
  assert(fabsf(actual - expected) < 0.001f);
}

static void near_angles(const float actual[3], const float expected[3])
{
  for (int component = 0; component < 3; ++component)
    near_value(actual[component], expected[component]);
}

static void test_pose_angles(void)
{
  const float identity[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
  const float yaw_right[3][4] = {{0.8660254f, 0, 0.5f, 0}, {0, 1, 0, 0},
      {-0.5f, 0, 0.8660254f, 0}};
  const float pitch_up[3][4] = {{1, 0, 0, 0}, {0, 0.8660254f, -0.5f, 0},
      {0, 0.5f, 0.8660254f, 0}};
  const float roll_right[3][4] = {{0.7071068f, -0.7071068f, 0, 0},
      {0.7071068f, 0.7071068f, 0, 0}, {0, 0, 1, 0}};
  float angles[3] = {0};

  assert(VR_AimPoseAngles(identity, 12, angles));
  near_angles(angles, (float[3]){0, 12, 0});
  assert(VR_AimPoseAngles(yaw_right, 10, angles));
  near_angles(angles, (float[3]){0, 40, 0});
  assert(VR_AimPoseAngles(pitch_up, 0, angles));
  near_angles(angles, (float[3]){-30, 0, 0});
  assert(VR_AimPoseAngles(roll_right, 0, angles));
  near_angles(angles, (float[3]){0, 0, -45});

  float invalid[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, NAN, 1, 0}};
  float untouched[3] = {31, 41, 59};
  assert(!VR_AimPoseAngles(invalid, 0, untouched));
  near_angles(untouched, (float[3]){31, 41, 59});
}

static void test_head_and_mouse_modes(void)
{
  const float orientation[3] = {7, 30, 4};
  const float previous_orientation[3] = {2, 10, 0};
  const float previous_aim[3] = {0, 0, 0};
  float aim[3] = {80, 100, 9};
  float view[3] = {0, 0, 0};

  VR_AimResolve(VR_AIMMODE_HEAD_MYAW, 0, orientation, previous_orientation,
      previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){7, 120, 0});
  near_angles(view, (float[3]){7, 120, 4});

  aim[0] = 80; aim[1] = 100; aim[2] = 9;
  VR_AimResolve(VR_AIMMODE_HEAD_MYAW_MPITCH, 0, orientation,
      previous_orientation, previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){85, 120, 0});
  near_angles(view, (float[3]){85, 120, 4});

  aim[0] = 70; aim[1] = 80; aim[2] = 9;
  VR_AimResolve(VR_AIMMODE_MOUSE_MYAW, 0, orientation, previous_orientation,
      previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){70, 80, 0});
  near_angles(view, (float[3]){7, 110, 4});

  aim[0] = 70; aim[1] = 80; aim[2] = 9;
  VR_AimResolve(VR_AIMMODE_MOUSE_MYAW_MPITCH, 0, orientation,
      previous_orientation, previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){70, 80, 0});
  near_angles(view, (float[3]){77, 110, 4});
}

static void test_blended_and_controller_modes(void)
{
  const float orientation[3] = {10, 40, 6};
  const float previous_orientation[3] = {5, 30, 0};
  const float previous_aim[3] = {40, 17, 0};
  const float controller_aim[3] = {-11, 22, 33};
  float aim[3] = {40, 20, 9};
  float view[3] = {2, 50, 1};

  VR_AimResolve(VR_AIMMODE_BLENDED, 20, orientation, previous_orientation,
      previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){45, 30, 0});
  near_angles(view, (float[3]){10, 63, 6});

  aim[0] = 40; aim[1] = 20; aim[2] = 9;
  view[0] = 2; view[1] = 50; view[2] = 1;
  VR_AimResolve(VR_AIMMODE_BLENDED_NOPITCH, 20, orientation,
      previous_orientation, previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){40, 30, 0});
  near_angles(view, (float[3]){10, 63, 6});

  aim[0] = 1; aim[1] = 2; aim[2] = 3;
  VR_AimResolve(VR_AIMMODE_CONTROLLER, 0, orientation, previous_orientation,
      previous_aim, NULL, aim, view);
  near_angles(aim, (float[3]){1, 2, 3});
  near_angles(view, (float[3]){10, 40, 6});

  VR_AimResolve(VR_AIMMODE_CONTROLLER, 0, orientation, previous_orientation,
      previous_aim, controller_aim, aim, view);
  near_angles(aim, controller_aim);
  near_angles(view, (float[3]){10, 40, 6});
}

int main(void)
{
  test_pose_angles();
  test_head_and_mouse_modes();
  test_blended_and_controller_modes();
  puts("Inherited VR aim arithmetic preserves pose signs and modes");
}
