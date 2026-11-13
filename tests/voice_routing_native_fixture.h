/* Native preference and desktop capture routing through real voice owners. */
static void Voice_RoutingChecks(void)
{
	voice_settings_t disk, prepared;
	voice_menu_state_t menu;
	static const char missing[] = "routing fixture missing recording device";
	int key;

	assert(SDL_GetCurrentAudioDriver() &&
		!strcmp(SDL_GetCurrentAudioDriver(), "dummy"));
	assert(voice_initialized && !voice_profile_vr && voice_settings_path[0]);
	assert(VoiceSettings_Load(voice_settings_path, &disk) == 0);
	assert(!voice_settings.desktop.transmit && voice_settings.vr.transmit);
	assert(!voice_settings.desktop.mode && !voice_settings.vr.mode);
	assert(!voice_settings.desktop.device[0] && !voice_settings.vr.device[0]);
	assert(!voice_capture_device && !Voice_CaptureReady());

	Voice_SetVRTransmitEnabled(false);
	assert(!Voice_VRTransmitEnabled());
	Voice_LoadSettings();
	assert(!Voice_VRTransmitEnabled() && !voice_settings.vr.transmit);
	assert(!voice_settings.desktop.transmit && !voice_capture_device &&
		!Voice_CaptureReady());
	Voice_GetMenuState(&menu);
	assert(menu.available && menu.device_available && !menu.transmit &&
		!menu.vr_profile && !Voice_TransmitEnabled());
	Voice_SetVRTransmitEnabled(true);
	Voice_LoadSettings();
	assert(Voice_VRTransmitEnabled() && voice_settings.vr.transmit);
	assert(!voice_settings.desktop.transmit && !voice_capture_device &&
		!Voice_CaptureReady());
	Voice_GetMenuState(&menu);
	assert(menu.available && menu.device_available && !menu.transmit &&
		!menu.vr_profile && !Voice_TransmitEnabled());

	Voice_SetTransmitEnabled(true);
	assert(voice_settings.desktop.transmit && voice_capture_device &&
		Voice_CaptureReady());
	Voice_SetMode(1);
	Voice_GetMenuState(&menu);
	assert(menu.available && menu.device_available && menu.transmit &&
		menu.mode == 1 && !menu.vr_profile && !menu.device[0]);
	Voice_LoadSettings();
	assert(voice_settings.desktop.transmit && voice_settings.desktop.mode == 1 &&
		!voice_settings.desktop.device[0] && voice_capture_device &&
		Voice_CaptureReady());

	Voice_CycleInputDevice(1);
	assert(voice_settings.desktop.device[0] &&
		Voice_DeviceIsUnique(voice_settings.desktop.device));
	assert(voice_capture_device && Voice_CaptureReady());
	Voice_CycleInputDevice(-1);
	assert(!voice_settings.desktop.device[0] && voice_capture_device &&
		Voice_CaptureReady());

	assert(!Voice_DeviceIsUnique(missing));
	prepared = voice_settings;
	q_strlcpy(prepared.desktop.device, missing,
		sizeof(prepared.desktop.device));
	assert(VoiceSettings_Save(voice_settings_path, &prepared));
	Voice_LoadSettings();
	Voice_RefreshCapture(true);
	Voice_GetMenuState(&menu);
	assert(voice_settings.desktop.transmit &&
		!strcmp(voice_settings.desktop.device, missing));
	assert(!voice_capture_device && !Voice_CaptureReady());
	assert(menu.available && !menu.device_available);

	Voice_CycleInputDevice(1);
	assert(voice_settings.desktop.device[0] &&
		Voice_DeviceIsUnique(voice_settings.desktop.device));
	assert(voice_capture_device && Voice_CaptureReady());
	Voice_CycleInputDevice(-1);
	assert(!voice_settings.desktop.device[0] && voice_capture_device &&
		Voice_CaptureReady());

	Cmd_ExecuteString("voice_revoke", src_command);
	assert(!voice_settings.desktop.transmit && !voice_capture_device &&
		!Voice_CaptureReady());
	Voice_LoadSettings();
	assert(!voice_settings.desktop.transmit && voice_settings.vr.transmit);
	Voice_SetTransmitEnabled(true);
	Voice_SetMode(1);
	Voice_SetVRTransmitEnabled(true);
	Voice_GetMenuState(&menu);
	assert(voice_settings.desktop.transmit && voice_settings.desktop.mode == 1 &&
		!voice_settings.desktop.device[0] && voice_settings.vr.transmit);
	assert(menu.available && menu.device_available && menu.transmit &&
		menu.mode == 1 && !menu.vr_profile && !menu.device[0]);
	assert(voice_capture_device && Voice_CaptureReady() &&
		voice_pending_action == VOICE_PENDING_NONE && !voice_ptt);
	for (key = 0; key < MAX_KEYS; ++key)
		assert(!voice_ptt_keys[key]);
}
