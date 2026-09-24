#include "voice_settings.h"

#ifdef USE_SDL3
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef USE_SDL3
typedef SDL_IOStream voice_settings_file_t;
static voice_settings_file_t *VoiceSettings_FileOpen(const char *path, const char *mode)
{
	return SDL_IOFromFile(path, mode);
}
static size_t VoiceSettings_FileRead(voice_settings_file_t *file, void *buffer,
	size_t bytes)
{
	return SDL_ReadIO(file, buffer, bytes);
}
#ifdef _WIN32
static size_t VoiceSettings_FileWrite(voice_settings_file_t *file,
	const void *buffer, size_t bytes)
{
	return SDL_WriteIO(file, buffer, bytes);
}
#endif
static int VoiceSettings_FileClose(voice_settings_file_t *file)
{
	return SDL_CloseIO(file) ? 1 : 0;
}
#else
typedef SDL_RWops voice_settings_file_t;
static voice_settings_file_t *VoiceSettings_FileOpen(const char *path, const char *mode)
{
	return SDL_RWFromFile(path, mode);
}
static size_t VoiceSettings_FileRead(voice_settings_file_t *file, void *buffer,
	size_t bytes)
{
	return SDL_RWread(file, buffer, 1, bytes);
}
#ifdef _WIN32
static size_t VoiceSettings_FileWrite(voice_settings_file_t *file,
	const void *buffer, size_t bytes)
{
	return SDL_RWwrite(file, buffer, 1, bytes);
}
#endif
static int VoiceSettings_FileClose(voice_settings_file_t *file)
{
	return SDL_RWclose(file) == 0;
}
#endif

/* Keep this independent of the in-memory struct layout and its padding. */
#define VOICE_SETTINGS_VERSION 3
#define VOICE_SETTINGS_MAGIC_BYTES 8
#define VOICE_SETTINGS_PROFILE_BYTES (2 + VOICE_SETTINGS_DEVICE_BYTES)
#define VOICE_SETTINGS_V1_BYTES (VOICE_SETTINGS_MAGIC_BYTES + 1 + \
	2 * VOICE_SETTINGS_PROFILE_BYTES + VOICE_SETTINGS_MAX_KEYS)
#define VOICE_SETTINGS_FILE_BYTES (VOICE_SETTINGS_V1_BYTES + 2)
#define VOICE_SETTINGS_PATH_BYTES 4096

static const unsigned char voice_settings_magic[VOICE_SETTINGS_MAGIC_BYTES] = {
	0, 'V', 'O', 'I', 'C', 'E', 'S', '1'
};

#ifdef _WIN32
static int VoiceSettings_UTF8ToWide(const char *input, wchar_t *output,
	size_t output_count);
static int VoiceSettings_PathMissing(const char *path);
#endif

static int VoiceSettings_ProfileValid(const voice_settings_profile_t *profile)
{
	const char *terminator;
	if (!profile || profile->transmit > 1 || profile->mode > 1 || profile->self_reverb > 1)
		return 0;
	terminator = (const char *)memchr(profile->device, 0,
		sizeof(profile->device));
	return terminator != NULL;
}

static int VoiceSettings_ProfileCanonical(const voice_settings_profile_t *profile)
{
	const unsigned char *padding;
	const unsigned char *end;
	if (!VoiceSettings_ProfileValid(profile))
		return 0;
	padding = (const unsigned char *)memchr(profile->device, 0,
		sizeof(profile->device)) + 1;
	end = (const unsigned char *)profile->device + sizeof(profile->device);
	while (padding < end)
		if (*padding++)
			return 0;
	return 1;
}

static int VoiceSettings_Valid(const voice_settings_t *settings)
{
	int key;
	if (!settings || !VoiceSettings_ProfileValid(&settings->desktop) ||
		!VoiceSettings_ProfileValid(&settings->vr))
		return 0;
	for (key = 0; key < VOICE_SETTINGS_MAX_KEYS; ++key)
		if (settings->ptt_allowed[key] > 1)
			return 0;
	return 1;
}

void VoiceSettings_Defaults(voice_settings_t *settings)
{
	if (!settings)
		return;
	memset(settings, 0, sizeof(*settings));
	/* Microphone capture always starts disabled until saved local consent. */
	settings->desktop.transmit = 0;
	settings->vr.transmit = 0;
}

static void VoiceSettings_WriteProfile(unsigned char *wire,
	const voice_settings_profile_t *profile)
{
	const char *terminator = (const char *)memchr(profile->device, 0,
		sizeof(profile->device));
	wire[0] = profile->transmit;
	wire[1] = profile->mode;
	memset(wire + 2, 0, sizeof(profile->device));
	memcpy(wire + 2, profile->device,
		(size_t)(terminator - profile->device));
}

static void VoiceSettings_ReadProfile(voice_settings_profile_t *profile,
	const unsigned char *wire)
{
	profile->transmit = wire[0];
	profile->mode = wire[1];
	memcpy(profile->device, wire + 2, sizeof(profile->device));
}

static void VoiceSettings_Serialize(const voice_settings_t *settings,
	unsigned char wire[VOICE_SETTINGS_FILE_BYTES])
{
	unsigned char *cursor = wire;
	memcpy(cursor, voice_settings_magic, sizeof(voice_settings_magic));
	cursor += sizeof(voice_settings_magic);
	*cursor++ = VOICE_SETTINGS_VERSION;
	VoiceSettings_WriteProfile(cursor, &settings->desktop);
	cursor += VOICE_SETTINGS_PROFILE_BYTES;
	VoiceSettings_WriteProfile(cursor, &settings->vr);
	cursor += VOICE_SETTINGS_PROFILE_BYTES;
	memcpy(cursor, settings->ptt_allowed, VOICE_SETTINGS_MAX_KEYS);
	cursor += VOICE_SETTINGS_MAX_KEYS;
	*cursor++ = settings->desktop.self_reverb;
	*cursor = settings->vr.self_reverb;
}

static int VoiceSettings_Deserialize(const unsigned char *wire, size_t bytes,
	voice_settings_t *settings)
{
	voice_settings_t loaded = {0};
	unsigned int version;
	const unsigned char *cursor = wire;
	if (bytes != VOICE_SETTINGS_V1_BYTES && bytes != VOICE_SETTINGS_FILE_BYTES) return 0;
	if (memcmp(cursor, voice_settings_magic, sizeof(voice_settings_magic)) != 0)
		return 0;
	cursor += sizeof(voice_settings_magic);
	version = *cursor++;
	if (!((version == 1 && bytes == VOICE_SETTINGS_V1_BYTES) ||
		((version == 2 || version == VOICE_SETTINGS_VERSION) &&
		bytes == VOICE_SETTINGS_FILE_BYTES))) return 0;
	VoiceSettings_ReadProfile(&loaded.desktop, cursor);
	cursor += VOICE_SETTINGS_PROFILE_BYTES;
	VoiceSettings_ReadProfile(&loaded.vr, cursor);
	cursor += VOICE_SETTINGS_PROFILE_BYTES;
	memcpy(loaded.ptt_allowed, cursor, sizeof(loaded.ptt_allowed));
	if (version >= 2) {
		cursor += VOICE_SETTINGS_MAX_KEYS;
		loaded.desktop.self_reverb = *cursor++;
		loaded.vr.self_reverb = *cursor;
	}
	/* Older files could contain the old default-on value. Require fresh local
	 * confirmation before trusting a persisted capture permission. */
	if (version < VOICE_SETTINGS_VERSION)
	{
		loaded.desktop.transmit = loaded.vr.transmit = 0;
		loaded.desktop.self_reverb = loaded.vr.self_reverb = 0;
	}
	if (!VoiceSettings_Valid(&loaded) ||
		!VoiceSettings_ProfileCanonical(&loaded.desktop) ||
		!VoiceSettings_ProfileCanonical(&loaded.vr))
		return 0;
	*settings = loaded;
	return 1;
}

int VoiceSettings_Load(const char *path, voice_settings_t *settings)
{
	voice_settings_file_t *file;
	unsigned char wire[VOICE_SETTINGS_FILE_BYTES];
	voice_settings_t loaded;
	int result = -1;
	size_t bytes;
	unsigned char extra;
	if (!path || !path[0] || !settings)
		return -1;
	errno = 0;
	file = VoiceSettings_FileOpen(path, "rb");
	if (!file) {
#ifdef _WIN32
		if (VoiceSettings_PathMissing(path))
			return 0;
#endif
		return errno == ENOENT ? 0 : -1;
	}
	bytes = VoiceSettings_FileRead(file, wire, sizeof(wire));
	if (VoiceSettings_FileRead(file, &extra, 1) == 0 &&
		VoiceSettings_Deserialize(wire, bytes, &loaded))
		result = 1;
	if (!VoiceSettings_FileClose(file))
		result = -1;
	if (result == 1)
		*settings = loaded;
	return result;
}

static int VoiceSettings_Parent(const char *path, char *parent,
	size_t parent_size)
{
	const char *slash = strrchr(path, '/');
#ifdef _WIN32
	const char *backslash = strrchr(path, '\\');
	if (!slash || (backslash && backslash > slash))
		slash = backslash;
#endif
	if (!slash) {
		if (parent_size < 2)
			return 0;
		parent[0] = '.';
		parent[1] = 0;
		return 1;
	}
	if (slash == path) {
		if (parent_size < 2)
			return 0;
		parent[0] = *slash;
		parent[1] = 0;
		return 1;
	}
#ifdef _WIN32
	if (slash == path + 2 && path[1] == ':') {
		if (parent_size < 4)
			return 0;
		memcpy(parent, path, 3);
		parent[3] = 0;
		return 1;
	}
#endif
	if ((size_t)(slash - path) >= parent_size)
		return 0;
	memcpy(parent, path, (size_t)(slash - path));
	parent[slash - path] = 0;
	return 1;
}

#ifdef _WIN32
static int VoiceSettings_UTF8ToWide(const char *input, wchar_t *output,
	size_t output_count)
{
	if (!input || !output || !output_count || output_count > 0x7fffffffU)
		return 0;
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
		output, (int)output_count) != 0;
}

static int VoiceSettings_PathMissing(const char *path)
{
	wchar_t wpath[VOICE_SETTINGS_PATH_BYTES];
	DWORD attributes;
	if (!VoiceSettings_UTF8ToWide(path, wpath,
		sizeof(wpath) / sizeof(*wpath)))
		return 0;
	attributes = GetFileAttributesW(wpath);
	return attributes == INVALID_FILE_ATTRIBUTES &&
		(GetLastError() == ERROR_FILE_NOT_FOUND ||
		GetLastError() == ERROR_PATH_NOT_FOUND);
}

static int VoiceSettings_WriteAtomic(const char *path,
	const unsigned char wire[VOICE_SETTINGS_FILE_BYTES])
{
	char parent[VOICE_SETTINGS_PATH_BYTES];
	char temporary[VOICE_SETTINGS_PATH_BYTES];
	wchar_t wparent[VOICE_SETTINGS_PATH_BYTES];
	wchar_t wtemporary[VOICE_SETTINGS_PATH_BYTES];
	wchar_t wpath[VOICE_SETTINGS_PATH_BYTES];
	voice_settings_file_t *file;
	int closed;
	int success = 0;
	if (!VoiceSettings_Parent(path, parent, sizeof(parent)) ||
		!VoiceSettings_UTF8ToWide(parent, wparent, sizeof(wparent) / sizeof(*wparent)) ||
		!GetTempFileNameW(wparent, L"vse", 0, wtemporary))
		return 0;
	if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wtemporary, -1,
		temporary, sizeof(temporary), NULL, NULL) ||
		!VoiceSettings_UTF8ToWide(path, wpath, sizeof(wpath) / sizeof(*wpath))) {
		DeleteFileW(wtemporary);
		return 0;
	}
	file = VoiceSettings_FileOpen(temporary, "wb");
	if (!file)
		goto done;
	if (VoiceSettings_FileWrite(file, wire, VOICE_SETTINGS_FILE_BYTES) !=
		VOICE_SETTINGS_FILE_BYTES) {
		VoiceSettings_FileClose(file);
		goto done;
	}
	closed = VoiceSettings_FileClose(file);
	if (closed)
		success = MoveFileExW(wtemporary, wpath,
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
done:
	if (!success)
		DeleteFileW(wtemporary);
	return success;
}
#else
static int VoiceSettings_WriteAtomic(const char *path,
	const unsigned char wire[VOICE_SETTINGS_FILE_BYTES])
{
	char parent[VOICE_SETTINGS_PATH_BYTES];
	char temporary[VOICE_SETTINGS_PATH_BYTES];
	int descriptor;
	FILE *file;
	int closed;
	int success = 0;
	int written;
	if (!VoiceSettings_Parent(path, parent, sizeof(parent)))
		return 0;
	written = snprintf(temporary, sizeof(temporary), "%s/.voice-settings-XXXXXX",
		parent);
	if (written < 0 || (size_t)written >= sizeof(temporary))
		return 0;
	descriptor = mkstemp(temporary);
	if (descriptor < 0)
		return 0;
	file = fdopen(descriptor, "wb");
	if (!file) {
		close(descriptor);
		goto done;
	}
	if (fwrite(wire, 1, VOICE_SETTINGS_FILE_BYTES, file) !=
		VOICE_SETTINGS_FILE_BYTES ||
		fflush(file) != 0 || fsync(descriptor) != 0) {
		fclose(file);
		goto done;
	}
	closed = fclose(file);
	if (closed == 0)
		success = rename(temporary, path) == 0;
done:
	if (!success)
		unlink(temporary);
	return success;
}
#endif

int VoiceSettings_Save(const char *path, const voice_settings_t *settings)
{
	unsigned char wire[VOICE_SETTINGS_FILE_BYTES];
	if (!path || !path[0] || !VoiceSettings_Valid(settings))
		return 0;
	VoiceSettings_Serialize(settings, wire);
	return VoiceSettings_WriteAtomic(path, wire);
}
