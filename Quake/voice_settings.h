#ifndef VOICE_SETTINGS_H
#define VOICE_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_SETTINGS_DEVICE_BYTES 512
#define VOICE_SETTINGS_MAX_KEYS 512

typedef struct {
	unsigned char transmit;
	unsigned char mode;
	char device[512];
} voice_settings_profile_t;

typedef struct {
	voice_settings_profile_t desktop;
	voice_settings_profile_t vr;
	unsigned char ptt_allowed[512];
} voice_settings_t;

void VoiceSettings_Defaults(voice_settings_t *settings);

/* Returns 1 for a valid file, 0 when path is missing, and -1 otherwise. */
int VoiceSettings_Load(const char *path, voice_settings_t *settings);

/* Returns nonzero only after the replacement is visible at path. */
int VoiceSettings_Save(const char *path, const voice_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif /* VOICE_SETTINGS_H */
