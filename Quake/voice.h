/* Optional client voice capture and playback. */
#ifndef VOICE_H
#define VOICE_H

#include "voice_protocol.h"

#define VOICE_MENU_DEVICE_BYTES 512

typedef struct voice_menu_state_s
{
	qboolean available;
	qboolean transmit;
	qboolean self_reverb;
	qboolean device_available;
	qboolean capture_failed;
	qboolean vr_profile;
	int mode;
	char device[VOICE_MENU_DEVICE_BYTES];
} voice_menu_state_t;

#ifdef USE_VOICECHAT

void Voice_Init(void);
void Voice_Shutdown(void);
void Voice_Frame(void);
void Voice_ResetConnection(void);
void Voice_UpdateSpatialization(const float *origin, const float *right);
void Voice_MixAudio(unsigned char *stream, int bytes, int samplebits,
	int channels, int rate, qboolean signed8);
void Voice_ReceivePacket(int source_slot, uint32_t generation,
	const voice_packet_t *packet);
qboolean Voice_ConfirmKeyEvent(int key, qboolean down);
void Voice_PTTKeyEvent(int key, qboolean down);
qboolean Voice_SpeakerTalking(int source_slot);
qboolean Voice_TransmitEnabled(void);
/* Atomic UI snapshot and local input action for the saved VR preference. */
qboolean Voice_VRTransmitEnabled(void);
void Voice_SetVRTransmitEnabled(qboolean enabled);
void Voice_GetMenuState(voice_menu_state_t *state);
void Voice_SetTransmitEnabled(qboolean enabled);
void Voice_SetMode(int mode);
void Voice_CycleInputDevice(int direction);
void Voice_SetSelfReverb(qboolean enabled);
qboolean Voice_CaptureReady(void);
qboolean Voice_IsTransmitting(void);
float Voice_InputLevel(void);
qboolean Voice_HUDEnabled(void);

#else

#define Voice_Init() ((void)0)
#define Voice_Shutdown() ((void)0)
#define Voice_Frame() ((void)0)
#define Voice_ResetConnection() ((void)0)
#define Voice_UpdateSpatialization(origin, right) ((void)0)
#define Voice_MixAudio(stream, bytes, samplebits, channels, rate, signed8) ((void)0)
#define Voice_ReceivePacket(source_slot, generation, packet) ((void)0)
#define Voice_ConfirmKeyEvent(key, down) 0
#define Voice_PTTKeyEvent(key, down) ((void)0)
#define Voice_SpeakerTalking(source_slot) 0
#define Voice_TransmitEnabled() 0
#define Voice_VRTransmitEnabled() 0
#define Voice_SetVRTransmitEnabled(enabled) ((void)0)
static inline void Voice_GetMenuState(voice_menu_state_t *state)
{
	if (!state)
		return;
	state->available = false;
	state->transmit = false;
	state->self_reverb = false;
	state->device_available = false;
	state->capture_failed = false;
	state->vr_profile = false;
	state->mode = 0;
	state->device[0] = '\0';
}
#define Voice_SetTransmitEnabled(enabled) ((void)0)
#define Voice_SetMode(mode) ((void)0)
#define Voice_CycleInputDevice(direction) ((void)0)
#define Voice_SetSelfReverb(enabled) ((void)0)
#define Voice_CaptureReady() 0
#define Voice_IsTransmitting() 0
#define Voice_InputLevel() 0.0f
#define Voice_HUDEnabled() 0

#endif /* USE_VOICECHAT */

#endif /* VOICE_H */
