#include "vr_fbt_storage.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char test_root[] = "/tmp/qs-vr-fbt-storage.XXXXXX";

const char *COM_GetWriteRoot(void)
{
	return test_root;
}

int q_snprintf(char *text, size_t capacity, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vsnprintf(text, capacity, format, arguments);
	va_end(arguments);
	if (capacity)
		text[capacity - 1] = 0;
	return result;
}

FILE *Sys_fopen(const char *path, const char *mode)
{
	char directory[4096];
	char *cursor;
	if (strchr(mode, 'w')) {
		assert(strlen(path) < sizeof(directory));
		strcpy(directory, path);
		for (cursor = directory + 1; *cursor; ++cursor) {
			if (*cursor != '/')
				continue;
			*cursor = 0;
			assert(mkdir(directory, 0700) == 0 || errno == EEXIST);
			*cursor = '/';
		}
	}
	return fopen(path, mode);
}

static vr_fbt_profile_t TestProfile(void)
{
	vr_fbt_profile_t profile;
	memset(&profile, 0, sizeof(profile));
	strcpy(profile.name, "session_one");
	profile.schema_version = VR_FBT_PROFILE_SCHEMA_VERSION;
	profile.calibration_algorithm = VR_FBT_PROFILE_CALIBRATION_ALGORITHM;
	strcpy(profile.avatar_fingerprint, VR_FBT_PROFILE_AVATAR_FINGERPRINT);
	strcpy(profile.hmd_serial, "hmd.1");
	profile.hmd_height_metres = 1.7;
	profile.body_forward[2] = 1.0;
	profile.roles[VR_FBT_ROLE_LEFT_FOOT].present = 1;
	strcpy(profile.roles[VR_FBT_ROLE_LEFT_FOOT].serial, "left.1");
	profile.roles[VR_FBT_ROLE_LEFT_FOOT].device_to_anatomical.orientation[0] = 1.0;
	return profile;
}

int main(void)
{
	vr_fbt_profile_t saved = TestProfile();
	vr_fbt_profile_t loaded;
	vr_fbt_profile_t unchanged;
	vr_fbt_profile_error_t profile_error;
	vr_fbt_storage_error_t storage_error;
	char profile_path[4096];
	char selected_path[4096];
	char selected[VR_FBT_PROFILE_NAME_MAX];
	FILE *file;

	assert(mkdtemp(test_root));
	assert(VR_FBT_StorageNameIsSafe("valid-name_2"));
	assert(!VR_FBT_StorageNameIsSafe("../escape"));
	assert(!VR_FBT_StorageNameIsSafe(""));
	assert(!VR_FBT_StorageGetProfilePath("../escape", profile_path,
		sizeof(profile_path), &storage_error));
	assert(storage_error == VR_FBT_STORAGE_ERR_NAME);
	assert(VR_FBT_StorageGetProfilePath(saved.name, profile_path,
		sizeof(profile_path), &storage_error));

#ifndef _WIN32
	/* A newly-created vrik directory is synced before profiles is created, so
	 * failure here cannot commit a profile. */
	assert(VR_FBT_StorageTestGetAncestorDirectorySyncCount() == 0);
	VR_FBT_StorageTestFailNextAncestorDirectorySync();
	assert(!VR_FBT_StorageSaveProfile(&saved, &profile_error, &storage_error));
	assert(storage_error == VR_FBT_STORAGE_ERR_IO);
	assert(access(profile_path, F_OK) != 0);
	assert(VR_FBT_StorageTestGetAncestorDirectorySyncCount() == 1);
#endif
	assert(VR_FBT_StorageSaveProfile(&saved, &profile_error, &storage_error));
#ifndef _WIN32
	/* Retry re-syncs the existing vrik parent, then syncs the new profiles
	 * parent: the root sync count is therefore retried rather than skipped. */
	assert(VR_FBT_StorageTestGetAncestorDirectorySyncCount() == 3);
#endif
	assert(VR_FBT_StorageGetProfilePath(saved.name, profile_path,
		sizeof(profile_path), &storage_error));
	assert(!strncmp(profile_path, test_root, strlen(test_root)));
	memset(&loaded, 0, sizeof(loaded));
	assert(VR_FBT_StorageLoadProfile(saved.name, &loaded, &profile_error,
		&storage_error));
	assert(!memcmp(&saved, &loaded, sizeof(saved)));

#ifndef _WIN32
	/* rename precedes the final directory sync: a sync failure reports failure
	 * even though the replacement is already visible. */
	saved.hmd_height_metres = 1.8;
	VR_FBT_StorageTestFailNextCommitDirectorySync();
	assert(!VR_FBT_StorageSaveProfile(&saved, &profile_error, &storage_error));
	assert(storage_error == VR_FBT_STORAGE_ERR_COMMITTED_NOT_DURABLE);
	assert(VR_FBT_StorageLoadProfile(saved.name, &loaded, &profile_error,
		&storage_error));
	assert(loaded.hmd_height_metres == saved.hmd_height_metres);
#endif
	assert(VR_FBT_StorageGetSelectedPath(selected_path, sizeof(selected_path),
		&storage_error));
	assert(symlink(profile_path, selected_path) == 0);
	assert(!VR_FBT_StorageSaveSelected(saved.name, &storage_error));
	assert(storage_error == VR_FBT_STORAGE_ERR_PATH);
	assert(unlink(selected_path) == 0);
	assert(VR_FBT_StorageSaveSelected(saved.name, &storage_error));
	assert(VR_FBT_StorageLoadSelected(selected, sizeof(selected), &storage_error));
	assert(!strcmp(selected, saved.name));
	file = fopen(profile_path, "wb");
	assert(file);
	assert(fputs("bad\n", file) >= 0);
	assert(fclose(file) == 0);
	unchanged = loaded;
	assert(!VR_FBT_StorageLoadProfile(saved.name, &loaded, &profile_error,
		&storage_error));
	assert(storage_error == VR_FBT_STORAGE_ERR_FORMAT);
	assert(!memcmp(&unchanged, &loaded, sizeof(loaded)));
	assert(unlink(profile_path) == 0);
	assert(unlink(selected_path) == 0);
	{
		char profile_directory[4096];
		assert(strlen(test_root) + sizeof("/vrik/profiles") < sizeof(profile_directory));
		strcpy(profile_directory, test_root);
		strcat(profile_directory, "/vrik/profiles");
		assert(rmdir(profile_directory) == 0);
		strcpy(profile_directory, test_root);
		strcat(profile_directory, "/vrik");
		assert(rmdir(profile_directory) == 0);
		assert(symlink("/tmp", profile_directory) == 0);
		assert(!VR_FBT_StorageSaveSelected(saved.name, &storage_error));
		assert(storage_error == VR_FBT_STORAGE_ERR_PATH);
		assert(unlink(profile_directory) == 0);
	}
	assert(rmdir(test_root) == 0);
	return 0;
}
