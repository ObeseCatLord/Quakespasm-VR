/* Optional, command-time VR locomotion state. No renderer/QuakeC ownership. */
#ifndef VR_GORILLA_TYPES_H
#define VR_GORILLA_TYPES_H

#define VR_GORILLA_HANDS 3u
#define VR_GORILLA_RESET 4u
#define VR_GORILLA_FLAGS 7u
#define VR_GORILLA_MAX_REACH 112.0f
#define VR_GORILLA_MAX_HAND_SPEED 600.0f
#define VR_GORILLA_RADIUS 3.0f
#define VR_GORILLA_EYE_HEIGHT 28.0f /* above unchanged hull feet at activation */

typedef struct {
  unsigned char flags;
  float head[3];             /* body-relative, unmodified tracked head */
  float hand[2][3];          /* body-relative physical palms, logical off/main */
  float velocity[2][3];      /* physical tracking velocity, game units/second */
} vr_gorilla_input_t;

typedef struct {
  unsigned char initialized;
  unsigned char touching;
  unsigned char recovering;  /* bits: seeded virtual-palm offset is active;
                              * may remain active while unbound/touching=0 */
  float anchor[2][3];        /* world-space, or surface-local if surface>0 */
  float recovery_offset[2][3]; /* world-space raw-palm -> virtual-palm bias */
  int surface[2];            /* canonical entity number; world/unbound=0 */
  unsigned int surface_model[2]; /* model identity; reject replaced surfaces */
  float velocity[3];         /* command-time launch filter, not body velocity */
  float origin[3];           /* last processed body baseline; reset on warp */
} vr_gorilla_state_t;

/* Command-owned result of local hand locomotion. This is deliberately not
 * total body velocity/position: native forces and collision remain native. */
#define VR_GORILLA_MOTION_ACTIVE 1
#define VR_GORILLA_MOTION_BRACED 2
#define VR_GORILLA_MOTION_SWIM 4
#define VR_GORILLA_MOTION_LAUNCHED 8
#define VR_GORILLA_MOTION_FLAGS 15
typedef struct {
  unsigned int generation;
  unsigned char flags;
  float displacement[3], impulse[3];
  int contact[2];
  unsigned int contact_model[2];
} vr_gorilla_motion_t;

#endif
