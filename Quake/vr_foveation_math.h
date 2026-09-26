#ifndef QUAKE_VR_FOVEATION_MATH_H
#define QUAKE_VR_FOVEATION_MATH_H

#include <math.h>
#include <stdint.h>
#include <limits.h>
#include "vr_openxr.h"

/* META centers are normalized to [-1,1]. Vulkan density offsets are signed
 * framebuffer pixels, rounded to the device's required granularity. */
static inline int VRF_DensityOffset(float center, uint32_t extent,
                                    uint32_t granularity, int32_t *out) {
  if (!out || !isfinite(center) || center < -1.f || center > 1.f ||
      !extent || !granularity) return 0;
  const double offset = round((double)center * 0.5 * extent / granularity) *
                        granularity;
  if (!isfinite(offset) || offset < INT32_MIN || offset > INT32_MAX)
    return 0;
  *out = (int32_t)offset;
  return 1;
}

/* OpenXR time describes the expressed pose (possibly predicted), not capture
 * time. Runtime tracking flags remain the quality authority. */
static inline const char *VRF_GazeRejection(const vrxr_gaze_t *gaze,
                                             double max_age) {
  if (!gaze || !gaze->valid) return "no valid gaze pose";
  if (!gaze->tracked) return "gaze quality degraded or tracking lost";
  if (!gaze->sample_time_known) return "runtime gaze pose time unavailable";
  if (!isfinite(max_age) || max_age < 0 ||
      !isfinite(gaze->sample_age_seconds))
    return "invalid gaze timing";
  if (gaze->sample_age_seconds < -0.05)
    return "gaze pose too far in the future";
  if (gaze->sample_age_seconds > max_age)
    return "gaze pose too old for display time";

  double length_squared = 0;
  for (int i = 0; i < 3; ++i) {
    if (!isfinite(gaze->direction[i]) || !isfinite(gaze->origin[i]))
      return "non-finite gaze ray";
    length_squared += (double)gaze->direction[i] * gaze->direction[i];
  }
  return length_squared > 0.9 && length_squared < 1.1
             ? NULL : "invalid gaze ray length";
}

static inline int VRF_GazeUsable(const vrxr_gaze_t *gaze, double max_age) {
  return VRF_GazeRejection(gaze, max_age) == NULL;
}

static inline int VRF_ViewFovUsable(const vrxr_view_t *view) {
  return view && isfinite(view->left) && isfinite(view->right) &&
         isfinite(view->up) && isfinite(view->down) &&
         view->left < view->right && view->down < view->up &&
         isfinite(view->right - view->left) &&
         isfinite(view->up - view->down);
}

/* Return an optical-eye ray, preserving asymmetric/canted view projection.
 * A combined ray has no reliable vergence: use its direction at infinity. */
static inline int VRF_EyeDirection(const vrxr_view_t *view,
                                   const vrxr_gaze_t *gaze, int fixed,
                                   float direction[3]) {
  if (!direction) return 0;
  direction[0] = direction[1] = direction[2] = 0;
  if (!VRF_ViewFovUsable(view)) return 0;

  float result[3];
  if (fixed) {
    result[0] = result[1] = 0;
    result[2] = -1;
  } else {
    if (!VRF_GazeUsable(gaze, 0.05)) return 0;
    for (int row = 0; row < 3; ++row) {
      for (int col = 0; col < 3; ++col)
        if (!isfinite(view->matrix[row][col])) return 0;
      /* The view matrix maps eye to app space. Apply its transpose to the
       * app-space gaze ray, as the donor renderer did. */
      result[row] = view->matrix[0][row] * gaze->direction[0] +
                    view->matrix[1][row] * gaze->direction[1] +
                    view->matrix[2][row] * gaze->direction[2];
    }
  }

  const float length = sqrtf(result[0] * result[0] +
                             result[1] * result[1] +
                             result[2] * result[2]);
  if (!isfinite(length) || length < 0.01f || result[2] >= -0.01f)
    return 0;
  for (int i = 0; i < 3; ++i) {
    result[i] /= length;
    if (!isfinite(result[i])) return 0;
  }
  direction[0] = result[0];
  direction[1] = result[1];
  direction[2] = result[2];
  return 1;
}

/* Angular radius in degrees. Include each tile's angular footprint so a tile
 * crossing the protected region receives the finest rate. Invalid math
 * conservatively returns rate 0 (finest). u/w and half_u/half_w are normalized
 * tile coordinates and half extents. */
static inline unsigned char VRF_TileRate(const vrxr_view_t *view,
    const float gaze[3], float u, float w, float half_u, float half_w,
    float radius_degrees) {
  if (!VRF_ViewFovUsable(view) || !gaze ||
      !isfinite(u) || !isfinite(w) || u < 0 || u > 1 || w < 0 || w > 1 ||
      !isfinite(half_u) || !isfinite(half_w) || half_u < 0 || half_w < 0 ||
      !isfinite(radius_degrees) || radius_degrees < 0)
    return 0;

  double gaze_length_squared = 0;
  for (int i = 0; i < 3; ++i) {
    if (!isfinite(gaze[i])) return 0;
    gaze_length_squared += (double)gaze[i] * gaze[i];
  }
  if (gaze_length_squared <= 0.9 || gaze_length_squared >= 1.1 ||
      gaze[2] >= -0.01f)
    return 0;

  const float span_x = view->right - view->left;
  const float span_y = view->up - view->down;
  const float x = view->left + u * span_x;
  const float y = view->down + w * span_y;
  const float length = sqrtf(x * x + y * y + 1.0f);
  float dot = (x * gaze[0] + y * gaze[1] - gaze[2]) / length;
  const float guard = hypotf(half_u * span_x, half_w * span_y);
  const float radius = radius_degrees *
                       (3.14159265358979323846f / 180.0f);
  if (!isfinite(x) || !isfinite(y) || !isfinite(length) ||
      !isfinite(dot) || !isfinite(guard) || !isfinite(radius))
    return 0;
  if (dot > 1) dot = 1;
  if (dot < -1) dot = -1;

  const float angle = acosf(dot) - guard;
  if (!isfinite(angle) || !isfinite(angle + 0.14f)) return 0;
  return angle <= radius ? 0 : angle <= radius + 0.14f ? 1 : 2;
}

#endif
