#ifndef VR_FBT_STORAGE_H
#define VR_FBT_STORAGE_H

/*
 * Persistent storage for named full-body-tracker calibration profiles.
 *
 * This layer deliberately has no OpenVR or console dependency.  It addresses
 * exact files beneath COM_GetWriteRoot(), never Quake's virtual search path.
 */

#include <stddef.h>

#include "vr_fbt_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VR_FBT_STORAGE_SELECTED_TEXT_MAX 128

typedef enum {
	VR_FBT_STORAGE_OK = 0,
	VR_FBT_STORAGE_ERR_ARGUMENT,
	VR_FBT_STORAGE_ERR_NAME,
	VR_FBT_STORAGE_ERR_PATH,
	VR_FBT_STORAGE_ERR_NOT_FOUND,
	VR_FBT_STORAGE_ERR_IO,
	VR_FBT_STORAGE_ERR_SIZE,
	VR_FBT_STORAGE_ERR_FORMAT,
	VR_FBT_STORAGE_ERR_REPLACE,
	/* The replacement was renamed but its containing directory did not sync.
	 * Callers must treat the new file as visible but not crash-durable. */
	VR_FBT_STORAGE_ERR_COMMITTED_NOT_DURABLE
} vr_fbt_storage_error_t;

/* Names are [A-Za-z0-9_-]{1,32}; they are safe to interpolate into a path. */
int VR_FBT_StorageNameIsSafe(const char *name);

/* These are exact paths under COM_GetWriteRoot(), never virtual search paths. */
int VR_FBT_StorageGetProfilePath(const char *name, char *path,
	size_t path_capacity, vr_fbt_storage_error_t *error);
int VR_FBT_StorageGetSelectedPath(char *path, size_t path_capacity,
	vr_fbt_storage_error_t *error);

/* Loading leaves destination unchanged on every failure.  profile_error is
 * meaningful only when storage_error is VR_FBT_STORAGE_ERR_FORMAT. */
int VR_FBT_StorageLoadProfile(const char *name, vr_fbt_profile_t *destination,
	vr_fbt_profile_error_t *profile_error, vr_fbt_storage_error_t *storage_error);
/* A false save with ERR_COMMITTED_NOT_DURABLE means the replacement is already
 * visible and callers must not assume the previous on-disk version remains. */
int VR_FBT_StorageSaveProfile(const vr_fbt_profile_t *profile,
	vr_fbt_profile_error_t *profile_error, vr_fbt_storage_error_t *storage_error);

/* selected.cfg contains only a profile name.  A missing selected.cfg returns
 * ERR_NOT_FOUND; destination is unchanged on failure. */
int VR_FBT_StorageLoadSelected(char *destination, size_t destination_capacity,
	vr_fbt_storage_error_t *error);
/* This has the same ERR_COMMITTED_NOT_DURABLE contract as SaveProfile. */
int VR_FBT_StorageSaveSelected(const char *name, vr_fbt_storage_error_t *error);

/* Fixture-only fault injection for POSIX durability boundaries. */
#ifdef VR_FBT_STORAGE_TEST
void VR_FBT_StorageTestFailNextCommitDirectorySync(void);
void VR_FBT_StorageTestFailNextAncestorDirectorySync(void);
unsigned int VR_FBT_StorageTestGetAncestorDirectorySyncCount(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* VR_FBT_STORAGE_H */
