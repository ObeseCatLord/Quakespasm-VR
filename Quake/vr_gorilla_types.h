/* Optional, command-time VR locomotion state. No renderer/QuakeC ownership. */
#ifndef VR_GORILLA_TYPES_H
#define VR_GORILLA_TYPES_H

#define VR_GORILLA_HANDS 3u
#define VR_GORILLA_RESET 4u
#define VR_GORILLA_FLAGS 7u
#define VR_GORILLA_MAX_REACH 112.0f
#define VR_GORILLA_MAX_HAND_SPEED 600.0f
#define VR_GORILLA_RADIUS 3.0f
/* Stick walking keeps working at 20% speed while hand movement is active
 * (Ancient Dungeon VR monke mode).  Shared by server and prediction. */
#define VR_GORILLA_STICK_SCALE 0.2f
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
  float velocity[3];         /* reserved legacy filter slot in state/ACK layout;
                              * current launches do not consume its history */
  float origin[3];           /* last processed body baseline; reset on warp */
} vr_gorilla_state_t;

/* Owner-authoritative ("monke owner") body state, as Ancient Dungeon VR does
 * with its per-player state authority: the opted-in client simulates its own
 * body and reports the result; the server validates, applies it, and keeps
 * QuakeC/triggers.  generation is the server resync epoch the state descends
 * from; reports from an older epoch are ignored. */
#define VR_GORILLA_OWNER_ACTIVE 1
#define VR_GORILLA_OWNER_ONGROUND 2
#define VR_GORILLA_OWNER_FLAGS 3
#define VR_GORILLA_OWNER_MAX_SPEED 4096.0f
/* Seconds of owner movement the server lets accumulate (bursts, jitter). */
#define VR_GORILLA_OWNER_BURST 0.25f
#define VR_GORILLA_OWNER_MAX_COORD 262144.0f
typedef struct {
  unsigned int generation;
  unsigned char flags;
  float origin[3], velocity[3];
} vr_gorilla_motion_t;

#endif
