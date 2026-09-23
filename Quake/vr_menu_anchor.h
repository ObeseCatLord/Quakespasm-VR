#ifndef VR_MENU_ANCHOR_H
#define VR_MENU_ANCHOR_H

#include <math.h>
#include <string.h>

/* Presentation only. Updated once per stereo pair, never a gameplay pose. */
typedef struct {
  float base[3], angles[3], center[3];
  double time, outside_since;
  int valid, moving, mode;
} vr_menu_anchor_t;

static inline float VR_MenuAngleDelta(float to, float from) {
  return remainderf(to - from, 360.0f);
}

static inline void VR_MenuAnchorCenter(vr_menu_anchor_t *s, float radius) {
  float pitch = s->angles[0] * 0.01745329252f;
  float yaw = s->angles[1] * 0.01745329252f;
  s->center[0] = s->base[0] + radius * cosf(pitch) * cosf(yaw);
  s->center[1] = s->base[1] + radius * cosf(pitch) * sinf(yaw);
  s->center[2] = s->base[2] - radius * sinf(pitch);
}

static inline int VR_MenuAnchorUpdate(vr_menu_anchor_t *s,
    const float head[3], const float angles[3], double now, int tracked,
    int mode, float radius, int pointing) {
  float travel2 = 0, distance2 = 0, front = 0, yaw_error, dt, amount;
  int i, reset;
  if (!tracked || !isfinite(now) || !isfinite(radius) || radius < 1) {
    s->valid = 0;
    return 0;
  }
  for (i = 0; i < 3; ++i)
    if (!isfinite(head[i]) || !isfinite(angles[i])) {
      s->valid = 0;
      return 0;
    }
  mode = mode == 0 ? 0 : mode == 2 ? 2 : 1;
  reset = !s->valid || mode != s->mode || now < s->time;
  if (!reset) {
    for (i = 0; i < 3; ++i) {
      float delta = head[i] - s->base[i];
      float to_panel = s->center[i] - head[i];
      travel2 += delta * delta;
      distance2 += to_panel * to_panel;
      front += to_panel * (s->center[i] - s->base[i]) / radius;
    }
    /* Teleport / walking through the panel overrides pointing and pinning. */
    reset = travel2 > 192 * 192 || distance2 < 16 * 16 || front < 8;
  }
  dt = s->valid ? (float)(now - s->time) : 0;
  if (dt > .05f) dt = .05f;
  if (dt < 0) dt = 0;
  s->time = now;
  if (reset || mode == 2) {
    memcpy(s->base, head, sizeof(s->base));
    s->angles[0] = mode == 2 ? angles[0] : fmaxf(-45, fminf(45, angles[0]));
    s->angles[1] = angles[1];
    s->angles[2] = 0;
    s->outside_since = -1;
    s->moving = 0;
    s->mode = mode;
    s->valid = 1;
  } else if (mode == 1) {
    yaw_error = VR_MenuAngleDelta(angles[1], s->angles[1]);
    if (pointing) {
      s->outside_since = -1;
      s->moving = 0;
    } else {
      if (!s->moving) {
        if (fabsf(yaw_error) > 50 || travel2 > 24 * 24) {
          if (s->outside_since < 0) s->outside_since = now;
          if (now - s->outside_since >= .5) s->moving = 1;
        } else s->outside_since = -1;
      }
      if (s->moving) {
        amount = 1 - expf(-4 * dt);
        s->angles[1] += yaw_error * amount;
        for (i = 0; i < 3; ++i)
          s->base[i] += (head[i] - s->base[i]) * amount;
        /* Pitch stays at its opening angle, even during automatic follow. */
        if (fabsf(yaw_error) < 1 && travel2 < 1) {
          s->moving = 0;
          s->outside_since = -1;
        }
      }
    }
  }
  /* Reconstruct the orbit: never interpolate across the user's head. */
  VR_MenuAnchorCenter(s, radius);
  distance2 = front = 0;
  for (i = 0; i < 3; ++i) {
    float to_panel = s->center[i] - head[i];
    distance2 += to_panel * to_panel;
    front += to_panel * (s->center[i] - s->base[i]) / radius;
  }
  /* The head need not be at the orbit center. Check the newly proposed pose
   * as well, before an orbit could sweep through a leaning user's head. */
  if (distance2 < 16 * 16 || front < 8) {
    memcpy(s->base, head, sizeof(s->base));
    s->angles[0] = fmaxf(-45, fminf(45, angles[0]));
    s->angles[1] = angles[1];
    s->outside_since = -1;
    s->moving = 0;
    VR_MenuAnchorCenter(s, radius);
  }
  return 1;
}
#endif
