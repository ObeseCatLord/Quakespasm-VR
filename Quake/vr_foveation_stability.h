#ifndef QUAKE_VR_FOVEATION_STABILITY_H
#define QUAKE_VR_FOVEATION_STABILITY_H

enum { VRF_EYE_STABLE_FRAMES_REQUIRED = 3 };

typedef struct {
  unsigned int stable_frames;
} vrf_policy_state_t;

static inline void VRF_ResetPolicy(vrf_policy_state_t *state) {
  if (state) state->stable_frames = 0;
}

/* Advance once per rendering frame. Any invalid or missing sample immediately
 * drops eye foveation and requires a fresh sequence before reactivation. */
static inline int VRF_AdvanceEyeStability(vrf_policy_state_t *state, int sample_usable) {
  if (!state) return 0;
  if (state->stable_frames > VRF_EYE_STABLE_FRAMES_REQUIRED)
    state->stable_frames = 0;
  if (!sample_usable) {
    state->stable_frames = 0;
    return 0;
  }
  if (state->stable_frames < VRF_EYE_STABLE_FRAMES_REQUIRED)
    ++state->stable_frames;
  return state->stable_frames >= VRF_EYE_STABLE_FRAMES_REQUIRED;
}

#endif
