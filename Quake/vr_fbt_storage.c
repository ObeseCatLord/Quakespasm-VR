#include "quakedef.h"
#include "vr_fbt_storage.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#define VR_FBT_STORAGE_PROFILE_DIR "vrik/profiles"
#define VR_FBT_STORAGE_SELECTED_FILE "vrik/selected.cfg"
#define VR_FBT_STORAGE_SELECTED_HEADER "VRFBT-SELECTED 1\nname "

#ifdef _WIN32
static int VR_FBT_StorageUTF8ToWide(const char *input, wchar_t *output,
	size_t output_count);
#endif

static int VR_FBT_StorageSyncContainingDirectory(const char *path,
	int ancestor_creation);

static void VR_FBT_StorageSetError(vr_fbt_storage_error_t *error,
	vr_fbt_storage_error_t value)
{
	if (error)
		*error = value;
}

#ifdef VR_FBT_STORAGE_TEST
static int vr_fbt_storage_test_fail_commit_directory_sync;
static int vr_fbt_storage_test_fail_ancestor_directory_sync;
static unsigned int vr_fbt_storage_test_ancestor_directory_sync_count;

void VR_FBT_StorageTestFailNextCommitDirectorySync(void)
{
	vr_fbt_storage_test_fail_commit_directory_sync = 1;
}

void VR_FBT_StorageTestFailNextAncestorDirectorySync(void)
{
	vr_fbt_storage_test_fail_ancestor_directory_sync = 1;
}

unsigned int VR_FBT_StorageTestGetAncestorDirectorySyncCount(void)
{
	return vr_fbt_storage_test_ancestor_directory_sync_count;
}
#endif

static int VR_FBT_StorageRootIsAbsolute(const char *root)
{
	if (!root || !root[0])
		return 0;
#ifdef _WIN32
	return (isalpha((unsigned char)root[0]) && root[1] == ':' &&
		(root[2] == '/' || root[2] == '\\')) ||
		(root[0] == '\\' && root[1] == '\\');
#else
	return root[0] == '/';
#endif
}

int VR_FBT_StorageNameIsSafe(const char *name)
{
	size_t length;
	if (!name || !name[0])
		return 0;
	for (length = 0; length < VR_FBT_PROFILE_NAME_MAX; ++length) {
		unsigned char c = (unsigned char)name[length];
		if (!c)
			return length > 0;
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 0;
}

static int VR_FBT_StorageBuildPath(const char *relative, char *path,
	size_t path_capacity, vr_fbt_storage_error_t *error)
{
	const char *root = COM_GetWriteRoot();
	int written;
	size_t root_length;
	if (!relative || !path || !path_capacity || !VR_FBT_StorageRootIsAbsolute(root)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_PATH);
		return 0;
	}
	root_length = strlen(root);
	while (root_length > 1 && (root[root_length - 1] == '/' ||
		root[root_length - 1] == '\\'))
		--root_length;
	if (root_length == 1 && root[0] == '/')
		written = q_snprintf(path, path_capacity, "/%s", relative);
	else
		written = q_snprintf(path, path_capacity, "%.*s/%s", (int)root_length,
			root, relative);
	if (written < 0 || (size_t)written >= path_capacity) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_PATH);
		return 0;
	}
	VR_FBT_StorageSetError(error, VR_FBT_STORAGE_OK);
	return 1;
}

/* Storage directories are created component-by-component and rejected if an
 * existing component is a link/reparse point.  This prevents safe profile
 * names from being redirected outside the write root by a pre-existing link.
 * The checks are deliberately local to the storage subtree; COM owns the
 * write root itself. */
static int VR_FBT_StorageEnsureDirectory(const char *path, int create)
{
#ifdef _WIN32
	wchar_t wpath[MAX_OSPATH];
	DWORD attributes;
	if (!VR_FBT_StorageUTF8ToWide(path, wpath, Q_COUNTOF(wpath)))
		return 0;
	attributes = GetFileAttributesW(wpath);
	if (attributes == INVALID_FILE_ATTRIBUTES) {
		if (!create || !CreateDirectoryW(wpath, NULL))
			return 0;
		attributes = GetFileAttributesW(wpath);
	}
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
	struct stat status;
	if (lstat(path, &status) != 0) {
		if (!create || errno != ENOENT || mkdir(path, 0700) != 0)
			return 0;
		if (lstat(path, &status) != 0)
			return 0;
	}
	return S_ISDIR(status.st_mode) && !S_ISLNK(status.st_mode);
#endif
}

static int VR_FBT_StoragePrepareParent(const char *path, int create,
	int *ancestor_sync_failed)
{
	const char *root = COM_GetWriteRoot();
	const char *cursor;
	const char *separator;
	const char *last_separator;
	char directory[MAX_OSPATH];
	size_t root_length;
	if (ancestor_sync_failed)
		*ancestor_sync_failed = 0;
	if (!path || !VR_FBT_StorageRootIsAbsolute(root))
		return 0;
	root_length = strlen(root);
	while (root_length > 1 && (root[root_length - 1] == '/' ||
		root[root_length - 1] == '\\'))
		--root_length;
	if (strncmp(path, root, root_length) != 0 || path[root_length] != '/')
		return 0;
	if (root_length >= sizeof(directory))
		return 0;
	memcpy(directory, root, root_length);
	directory[root_length] = 0;
	if (!VR_FBT_StorageEnsureDirectory(directory, 0))
		return 0;
	last_separator = strrchr(path, '/');
	if (!last_separator || last_separator <= path + root_length)
		return 0;
	cursor = path + root_length + 1;
	while (cursor < last_separator) {
		separator = strchr(cursor, '/');
		if (!separator || separator > last_separator)
			separator = last_separator;
		if (separator == cursor || (size_t)(separator - path) >= sizeof(directory))
			return 0;
		memcpy(directory, path, (size_t)(separator - path));
		directory[separator - path] = 0;
		if (!VR_FBT_StorageEnsureDirectory(directory, create))
			return 0;
		/* A prior attempt may have created this component but failed before its
		 * parent synced.  Re-sync every write-path component on every attempt. */
		if (create && !VR_FBT_StorageSyncContainingDirectory(directory, 1)) {
			if (ancestor_sync_failed)
				*ancestor_sync_failed = 1;
			return 0;
		}
		cursor = separator + 1;
	}
	return 1;
}

static int VR_FBT_StorageFinalPathIsSafe(const char *path)
{
#ifdef _WIN32
	wchar_t wpath[MAX_OSPATH];
	DWORD attributes;
	if (!VR_FBT_StorageUTF8ToWide(path, wpath, Q_COUNTOF(wpath)))
		return 0;
	attributes = GetFileAttributesW(wpath);
	return attributes == INVALID_FILE_ATTRIBUTES ||
		(attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
	struct stat status;
	return lstat(path, &status) != 0 || !S_ISLNK(status.st_mode);
#endif
}

static void VR_FBT_StorageParentPath(const char *path, char *parent,
	size_t parent_capacity)
{
	const char *separator = strrchr(path, '/');
	size_t length = separator ? (size_t)(separator - path) : 0;
	if (!parent || !parent_capacity || !separator || length >= parent_capacity) {
		if (parent && parent_capacity)
			parent[0] = 0;
		return;
	}
	memcpy(parent, path, length);
	parent[length] = 0;
}

int VR_FBT_StorageGetProfilePath(const char *name, char *path,
	size_t path_capacity, vr_fbt_storage_error_t *error)
{
	char relative[sizeof(VR_FBT_STORAGE_PROFILE_DIR) + VR_FBT_PROFILE_NAME_MAX + 5];
	int written;
	if (!VR_FBT_StorageNameIsSafe(name)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_NAME);
		return 0;
	}
	written = q_snprintf(relative, sizeof(relative), "%s/%s.cfg",
		VR_FBT_STORAGE_PROFILE_DIR, name);
	if (written < 0 || (size_t)written >= sizeof(relative)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_PATH);
		return 0;
	}
	return VR_FBT_StorageBuildPath(relative, path, path_capacity, error);
}

int VR_FBT_StorageGetSelectedPath(char *path, size_t path_capacity,
	vr_fbt_storage_error_t *error)
{
	return VR_FBT_StorageBuildPath(VR_FBT_STORAGE_SELECTED_FILE, path,
		path_capacity, error);
}

static int VR_FBT_StorageReadText(const char *path, char *text,
	size_t text_capacity, size_t *text_length, vr_fbt_storage_error_t *error)
{
	FILE *file;
	size_t count;
	int close_failed;
	if (!path || !text || text_capacity < 2 || !text_length) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_ARGUMENT);
		return 0;
	}
	if (!VR_FBT_StoragePrepareParent(path, 0, NULL) ||
		!VR_FBT_StorageFinalPathIsSafe(path)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_PATH);
		return 0;
	}
#ifdef _WIN32
	file = Sys_fopen(path, "rb");
#else
	{
		int descriptor = open(path, O_RDONLY | O_NOFOLLOW);
		file = descriptor < 0 ? NULL : fdopen(descriptor, "rb");
		if (!file && descriptor >= 0)
			close(descriptor);
	}
#endif
	if (!file) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_NOT_FOUND);
		return 0;
	}
	count = fread(text, 1, text_capacity, file);
	if (ferror(file)) {
		fclose(file);
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_IO);
		return 0;
	}
	close_failed = fclose(file) != 0;
	if (count == text_capacity) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_SIZE);
		return 0;
	}
	if (close_failed) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_IO);
		return 0;
	}
	text[count] = 0;
	*text_length = count;
	VR_FBT_StorageSetError(error, VR_FBT_STORAGE_OK);
	return 1;
}

static int VR_FBT_StorageFlushFile(FILE *file)
{
	if (!file || fflush(file) != 0)
		return 0;
#ifdef _WIN32
	{
		intptr_t os_handle = _get_osfhandle(_fileno(file));
		return os_handle != -1 && FlushFileBuffers((HANDLE)os_handle) != 0;
	}
#else
	return fsync(fileno(file)) == 0;
#endif
}

#ifdef _WIN32
static int VR_FBT_StorageUTF8ToWide(const char *input, wchar_t *output,
	size_t output_count)
{
	if (!input || !output || !output_count)
		return 0;
	return MultiByteToWideChar(CP_UTF8, 0, input, -1, output,
		(int)output_count) != 0;
}
#endif

static int VR_FBT_StorageReplaceFile(const char *temporary, const char *final)
{
#ifdef _WIN32
	wchar_t wtemporary[MAX_OSPATH];
	wchar_t wfinal[MAX_OSPATH];
	if (!VR_FBT_StorageUTF8ToWide(temporary, wtemporary, Q_COUNTOF(wtemporary)) ||
		!VR_FBT_StorageUTF8ToWide(final, wfinal, Q_COUNTOF(wfinal)))
		return 0;
	return MoveFileExW(wtemporary, wfinal,
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
	return rename(temporary, final) == 0;
#endif
}

static int VR_FBT_StorageCreateTemporary(const char *final, char *temporary,
	size_t temporary_capacity, FILE **file)
{
	char parent[MAX_OSPATH];
	if (!final || !temporary || !temporary_capacity || !file)
		return 0;
	VR_FBT_StorageParentPath(final, parent, sizeof(parent));
	if (!parent[0])
		return 0;
#ifdef _WIN32
	{
		wchar_t wparent[MAX_OSPATH];
		wchar_t wtemporary[MAX_OSPATH];
		int converted;
		if (!VR_FBT_StorageUTF8ToWide(parent, wparent, Q_COUNTOF(wparent)) ||
			!GetTempFileNameW(wparent, L"vft", 0, wtemporary))
			return 0;
		converted = WideCharToMultiByte(CP_UTF8, 0, wtemporary, -1,
			temporary, (int)temporary_capacity, NULL, NULL);
		if (!converted) {
			DeleteFileW(wtemporary);
			return 0;
		}
		*file = Sys_fopen(temporary, "wb");
		if (!*file)
			remove(temporary);
		return *file != NULL;
	}
#else
	int written;
	written = q_snprintf(temporary, temporary_capacity, "%s/.vrfbt.XXXXXX", parent);
	if (written < 0 || (size_t)written >= temporary_capacity)
		return 0;
	{
		int descriptor = mkstemp(temporary);
		if (descriptor < 0)
			return 0;
		*file = fdopen(descriptor, "wb");
		if (!*file) {
			close(descriptor);
			remove(temporary);
		}
		return *file != NULL;
	}
#endif
}

static int VR_FBT_StorageSyncContainingDirectory(const char *path,
	int ancestor_creation)
{
#ifdef _WIN32
	/* MoveFileExW(MOVEFILE_WRITE_THROUGH) is the available Windows durability
	 * request for this replacement.  Windows has no portable directory fsync. */
	(void)path;
	(void)ancestor_creation;
	return 1;
#else
	char parent[MAX_OSPATH];
	int descriptor;
	int synced;
	VR_FBT_StorageParentPath(path, parent, sizeof(parent));
	if (!parent[0])
		return 0;
#ifdef VR_FBT_STORAGE_TEST
	if (ancestor_creation)
		++vr_fbt_storage_test_ancestor_directory_sync_count;
	if ((ancestor_creation && vr_fbt_storage_test_fail_ancestor_directory_sync) ||
		(!ancestor_creation && vr_fbt_storage_test_fail_commit_directory_sync)) {
		if (ancestor_creation)
			vr_fbt_storage_test_fail_ancestor_directory_sync = 0;
		else
			vr_fbt_storage_test_fail_commit_directory_sync = 0;
		return 0;
	}
#endif
	descriptor = open(parent, O_RDONLY | O_DIRECTORY);
	if (descriptor < 0)
		return 0;
	synced = fsync(descriptor) == 0;
	if (close(descriptor) != 0)
		synced = 0;
	return synced;
#endif
}

static int VR_FBT_StorageWriteAtomic(const char *final, const char *text,
	size_t text_length, vr_fbt_storage_error_t *error)
{
	char temporary[MAX_OSPATH];
	FILE *file;
	int write_failed;
	int close_failed;
	int ancestor_sync_failed;
	if (!final || !text || text_length > VR_FBT_PROFILE_TEXT_MAX) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_ARGUMENT);
		return 0;
	}
	if (!VR_FBT_StoragePrepareParent(final, 1, &ancestor_sync_failed) ||
		!VR_FBT_StorageFinalPathIsSafe(final)) {
		VR_FBT_StorageSetError(error, ancestor_sync_failed ?
			VR_FBT_STORAGE_ERR_IO : VR_FBT_STORAGE_ERR_PATH);
		return 0;
	}
	if (!VR_FBT_StorageCreateTemporary(final, temporary, sizeof(temporary),
		&file)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_IO);
		return 0;
	}
	write_failed = fwrite(text, 1, text_length, file) != text_length ||
		!VR_FBT_StorageFlushFile(file);
	close_failed = fclose(file) != 0;
	if (write_failed || close_failed) {
		remove(temporary);
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_IO);
		return 0;
	}
	if (!VR_FBT_StorageReplaceFile(temporary, final)) {
		remove(temporary);
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_REPLACE);
		return 0;
	}
	if (!VR_FBT_StorageSyncContainingDirectory(final, 0)) {
		/* The replacement is visible, but cannot be reported durable. */
		VR_FBT_StorageSetError(error,
			VR_FBT_STORAGE_ERR_COMMITTED_NOT_DURABLE);
		return 0;
	}
	VR_FBT_StorageSetError(error, VR_FBT_STORAGE_OK);
	return 1;
}

int VR_FBT_StorageLoadProfile(const char *name, vr_fbt_profile_t *destination,
	vr_fbt_profile_error_t *profile_error, vr_fbt_storage_error_t *storage_error)
{
	char path[MAX_OSPATH];
	char text[VR_FBT_PROFILE_TEXT_MAX + 2];
	size_t text_length;
	vr_fbt_profile_t parsed;
	if (!destination) {
		VR_FBT_StorageSetError(storage_error, VR_FBT_STORAGE_ERR_ARGUMENT);
		return 0;
	}
	if (!VR_FBT_StorageGetProfilePath(name, path, sizeof(path), storage_error) ||
		!VR_FBT_StorageReadText(path, text, sizeof(text) - 1, &text_length,
			storage_error))
		return 0;
	if (!VR_FBT_ProfileParse(text, text_length, &parsed, profile_error)) {
		VR_FBT_StorageSetError(storage_error, VR_FBT_STORAGE_ERR_FORMAT);
		return 0;
	}
	if (strcmp(parsed.name, name) != 0) {
		if (profile_error)
			*profile_error = VR_FBT_PROFILE_ERR_FORMAT;
		VR_FBT_StorageSetError(storage_error, VR_FBT_STORAGE_ERR_FORMAT);
		return 0;
	}
	*destination = parsed;
	VR_FBT_StorageSetError(storage_error, VR_FBT_STORAGE_OK);
	return 1;
}

int VR_FBT_StorageSaveProfile(const vr_fbt_profile_t *profile,
	vr_fbt_profile_error_t *profile_error, vr_fbt_storage_error_t *storage_error)
{
	char path[MAX_OSPATH];
	char text[VR_FBT_PROFILE_TEXT_MAX];
	size_t text_length;
	if (!profile) {
		VR_FBT_StorageSetError(storage_error, VR_FBT_STORAGE_ERR_ARGUMENT);
		return 0;
	}
	if (!VR_FBT_StorageGetProfilePath(profile->name, path, sizeof(path),
		storage_error))
		return 0;
	if (!VR_FBT_ProfileSerialize(profile, text, sizeof(text), &text_length,
		profile_error)) {
		VR_FBT_StorageSetError(storage_error, VR_FBT_STORAGE_ERR_FORMAT);
		return 0;
	}
	return VR_FBT_StorageWriteAtomic(path, text, text_length, storage_error);
}

int VR_FBT_StorageLoadSelected(char *destination, size_t destination_capacity,
	vr_fbt_storage_error_t *error)
{
	char path[MAX_OSPATH];
	char text[VR_FBT_STORAGE_SELECTED_TEXT_MAX + 2];
	size_t text_length;
	size_t header_length = sizeof(VR_FBT_STORAGE_SELECTED_HEADER) - 1;
	char parsed[VR_FBT_PROFILE_NAME_MAX];
	size_t name_length;
	if (!destination || destination_capacity < VR_FBT_PROFILE_NAME_MAX) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_ARGUMENT);
		return 0;
	}
	if (!VR_FBT_StorageGetSelectedPath(path, sizeof(path), error) ||
		!VR_FBT_StorageReadText(path, text, sizeof(text) - 1, &text_length, error))
		return 0;
	if (text_length <= header_length + 1 ||
		memcmp(text, VR_FBT_STORAGE_SELECTED_HEADER, header_length) != 0 ||
		text[text_length - 1] != '\n') {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_FORMAT);
		return 0;
	}
	name_length = text_length - header_length - 1;
	if (name_length >= sizeof(parsed)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_FORMAT);
		return 0;
	}
	memcpy(parsed, text + header_length, name_length);
	parsed[name_length] = 0;
	if (!VR_FBT_StorageNameIsSafe(parsed)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_FORMAT);
		return 0;
	}
	memcpy(destination, parsed, name_length + 1);
	VR_FBT_StorageSetError(error, VR_FBT_STORAGE_OK);
	return 1;
}

int VR_FBT_StorageSaveSelected(const char *name, vr_fbt_storage_error_t *error)
{
	char path[MAX_OSPATH];
	char text[VR_FBT_STORAGE_SELECTED_TEXT_MAX];
	int written;
	if (!VR_FBT_StorageNameIsSafe(name)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_NAME);
		return 0;
	}
	if (!VR_FBT_StorageGetSelectedPath(path, sizeof(path), error))
		return 0;
	written = q_snprintf(text, sizeof(text), "%s%s\n",
		VR_FBT_STORAGE_SELECTED_HEADER, name);
	if (written < 0 || (size_t)written >= sizeof(text)) {
		VR_FBT_StorageSetError(error, VR_FBT_STORAGE_ERR_SIZE);
		return 0;
	}
	return VR_FBT_StorageWriteAtomic(path, text, (size_t)written, error);
}
