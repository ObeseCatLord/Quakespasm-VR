/* Native OpenXR boundary. No runtime handles or gameplay policy cross it. */
#ifndef QUAKE_VR_OPENXR_H
#define QUAKE_VR_OPENXR_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VRXR_MAX_DEVICES 64
enum { VRXR_DEVICE_NONE, VRXR_DEVICE_HEAD, VRXR_DEVICE_HAND, VRXR_DEVICE_TRACKER };
enum { VRXR_PROFILE_SIMPLE, VRXR_PROFILE_TOUCH, VRXR_PROFILE_INDEX,
       VRXR_PROFILE_VIVE, VRXR_PROFILE_FRAME };
enum { VRXR_BUTTON_TRIGGER = 1, VRXR_BUTTON_GRIP = 2,
       VRXR_BUTTON_STICK = 4, VRXR_BUTTON_PAD = 8,
       VRXR_BUTTON_PRIMARY = 16, VRXR_BUTTON_SECONDARY = 32,
       VRXR_BUTTON_MENU = 64, VRXR_BUTTON_EXTRA1 = 128,
       VRXR_BUTTON_EXTRA2 = 256 };
typedef struct {
  float matrix[3][4]; /* right-handed metres, -Z forward, +Y up */
  float velocity[3], angular_velocity[3];
  int valid, tracked, connected, velocity_valid, angular_velocity_valid;
  int kind, hand; /* physical hand: 0 left, 1 right; -1 otherwise */
  char serial[256]; /* empty if no persistent runtime identity */
} vrxr_device_t;
typedef struct {
  int active, profile;
  uint32_t pressed, touched;
  float trigger, grip, stick[2], pad[2];
} vrxr_input_t;
typedef struct {
  float matrix[3][4];
  float left, right, up, down; /* tangent-space FOV bounds; down < up */
  unsigned int width, height;
} vrxr_view_t;
typedef struct {
  int valid, tracked, sample_time_known;
  double sample_age_seconds; /* display time minus expressed pose time, not capture age */
  float origin[3], direction[3]; /* in the same space as views */
} vrxr_gaze_t;
typedef struct {
  int should_render, focused, reference_changed;
  uint64_t sample_id; /* nonzero identity of one completed xrWaitFrame sample */
  /* App reference space has runtime floor semantics (STAGE or LOCAL_FLOOR), never LOCAL. */
  int floor_referenced;
  vrxr_device_t devices[VRXR_MAX_DEVICES];
  vrxr_input_t hands[2];
  vrxr_view_t views[2];
  vrxr_gaze_t gaze;
} vrxr_frame_t;

/* Vulkan initialization and image submission are declared in vr_openxr_vulkan.h. */
void VRXR_Shutdown(void);
/* Retained across teardown until fresh initialization or an explicit reattach attempt.
 * Loss/failure requires graphics compatibility to be re-established; EXITING
 * requests an end to XR and must never trigger automatic reenable. */
typedef enum {
  VRXR_STOP_NONE, VRXR_STOP_EXITING, VRXR_STOP_FAILURE,
  VRXR_STOP_SESSION_LOST, VRXR_STOP_INSTANCE_LOST
} vrxr_stop_reason_t;
vrxr_stop_reason_t VRXR_StopReason(void);
/* 1: frame begun (possibly should_render=false), 0: idle, -1: terminal failure. */
int VRXR_BeginFrame(vrxr_frame_t *frame);
void VRXR_EndFrame(void);
/* Safe from engine Host_Error/longjmp and nested refresh paths. */
void VRXR_AbortFrame(void);
/* Generic tracker poses are optional and disabled until the engine's archived
 * FBT preference enables them; the setting applies at the next frame locate. */
void VRXR_SetTrackerEnabled(int enabled);
void VRXR_Haptic(int physical_hand, float duration_seconds, float amplitude);
int VRXR_GetViewSize(int eye, unsigned int *width, unsigned int *height);
const char *VRXR_RuntimeName(void);
/* Runtime-owned system label, valid only while native OpenXR is initialized. */
const char *VRXR_SystemName(void);
/* Disabled by default; independent of ordinary head/controller tracking.
 * Takes effect at the next action sync; disabled gaze is never queried. */
void VRXR_SetGazeEnabled(int enabled);
int VRXR_GazeSupported(void);
/* Borrowed triangle-list XY positions at view-space Z=-1. Reproject with the
 * current eye FOV, never treat these as normalized display coordinates. */
uint32_t VRXR_GetHiddenAreaMesh(int eye, const float **vertices);
#ifdef __cplusplus
}
#endif
#endif
