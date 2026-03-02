// VR Input System for IronwailVR
// Input action binding and polling is handled in vr_openxr.c (VR_OpenXR_InitActions,
// VR_UpdatePoses). This file is reserved for Phase 5 when dedicated input processing
// (gesture recognition, button remapping, etc.) is factored out.

#include "quakedef.h"

#ifdef VR_ENABLED

// Intentional stub — see vr_openxr.c for current input implementation.
void VR_Input_Reserved (void) {}

#endif // VR_ENABLED
