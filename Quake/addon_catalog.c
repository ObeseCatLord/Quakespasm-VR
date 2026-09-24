/*
 * Based on Ironwail's add-on catalogue design: content.json, a background
 * libcurl transfer, temporary-file install, and a caller-driven workflow.
 * Unlike the original implementation, this module does not fetch at startup,
 * follows no redirects, bounds all input, and rejects unsafe install paths.
 */
#include "quakedef.h"
#include "addon_catalog.h"
#include "json.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_CURL
#include <curl/curl.h>
#endif

#ifdef USE_SDL3
typedef SDL_AtomicInt addon_atomic_t;
#define AddonAtomicGet SDL_GetAtomicInt
#define AddonAtomicSet SDL_SetAtomicInt
#define AddonAtomicAdd SDL_AddAtomicInt
#else
typedef SDL_atomic_t addon_atomic_t;
#define AddonAtomicGet SDL_AtomicGet
#define AddonAtomicSet SDL_AtomicSet
#define AddonAtomicAdd SDL_AtomicAdd
#endif

#define ADDON_DEFAULT_URL	"https://kexquake.s3.amazonaws.com"
#define ADDON_MANIFEST	"content.json"
#define ADDON_MAX_MANIFEST	(1024 * 1024)
#define ADDON_MAX_PACKAGE	(512 * 1024 * 1024)

static cvar_t cl_addon_catalog_url = {"cl_addon_catalog_url", ADDON_DEFAULT_URL, CVAR_ARCHIVE};
static addon_catalog_entry_t addon_entries[ADDON_CATALOG_MAX_ENTRIES];
static int addon_count;
static SDL_Thread *addon_refresh_thread;
static SDL_Thread *addon_install_thread;
static SDL_Mutex *addon_mutex;
static addon_atomic_t addon_operation_id;
static addon_atomic_t addon_cancelled_operation;
static addon_atomic_t addon_state;
static addon_atomic_t addon_progress;
static unsigned int addon_operation_sequence;
static unsigned int addon_install_committing_operation;
static char addon_message[160];
static char addon_message_snapshot[160];
static addon_catalog_entry_t addon_entry_snapshot;
#ifdef USE_CURL
static char addon_base_url[MAX_OSPATH];
#endif

static void AddonCatalog_SetMessage (const char *message)
{
	SDL_LockMutex (addon_mutex);
	q_strlcpy (addon_message, message, sizeof(addon_message));
	SDL_UnlockMutex (addon_mutex);
}

#ifdef USE_CURL
static unsigned int AddonCatalog_NextOperationId (void)
{
	return addon_operation_sequence >= INT_MAX ? 1 : addon_operation_sequence + 1;
}

static qboolean AddonCatalog_IsCancelled (unsigned int operation_id)
{
	return operation_id != 0 &&
		AddonAtomicGet (&addon_cancelled_operation) == (int)operation_id;
}

static void AddonCatalog_SetOperationResultLocked (unsigned int operation_id,
	addon_catalog_state_t state, const char *message, const char *cancel_message)
{
	if (AddonAtomicGet (&addon_operation_id) != (int)operation_id)
		return;
	if (AddonCatalog_IsCancelled (operation_id))
	{
		state = ADDON_CATALOG_ERROR;
		message = cancel_message;
	}
	q_strlcpy (addon_message, message, sizeof(addon_message));
	AddonAtomicSet (&addon_state, state);
}

static void AddonCatalog_FinishOperation (unsigned int operation_id,
	addon_catalog_state_t state, const char *message, const char *cancel_message)
{
	SDL_LockMutex (addon_mutex);
	AddonCatalog_SetOperationResultLocked (operation_id, state, message, cancel_message);
	SDL_UnlockMutex (addon_mutex);
}

static void AddonCatalog_FinishInstallOperation (unsigned int operation_id,
	const char *gamedir, qboolean installed, addon_catalog_state_t state,
	const char *message, const char *cancel_message)
{
	int i;

	SDL_LockMutex (addon_mutex);
	if (AddonAtomicGet (&addon_operation_id) == (int)operation_id)
	{
		if (installed && !AddonCatalog_IsCancelled (operation_id))
		{
			for (i = 0; i < addon_count; i++)
				if (!q_strcasecmp (addon_entries[i].gamedir, gamedir))
					addon_entries[i].installed = true;
		}
		AddonCatalog_SetOperationResultLocked (operation_id, state, message, cancel_message);
		if (addon_install_committing_operation == operation_id)
			addon_install_committing_operation = 0;
	}
	SDL_UnlockMutex (addon_mutex);
}
#endif

static qboolean AddonCatalog_ApprovedStringEqual (const char *current,
	const char *expected, size_t field_size)
{
	const char *current_end = (const char *)memchr (current, '\0', field_size);
	const char *expected_end = (const char *)memchr (expected, '\0', field_size);
	size_t current_length, expected_length;

	if (!current_end || !expected_end)
		return false;
	current_length = (size_t)(current_end - current);
	expected_length = (size_t)(expected_end - expected);
	return current_length == expected_length &&
		memcmp (current, expected, current_length) == 0;
}

qboolean AddonCatalog_EntryMatchesApproved (const addon_catalog_entry_t *current,
	const addon_catalog_entry_t *expected)
{
	return current && expected &&
		AddonCatalog_ApprovedStringEqual (current->gamedir, expected->gamedir,
			sizeof (current->gamedir)) &&
		AddonCatalog_ApprovedStringEqual (current->name, expected->name,
			sizeof (current->name)) &&
		AddonCatalog_ApprovedStringEqual (current->author, expected->author,
			sizeof (current->author)) &&
		AddonCatalog_ApprovedStringEqual (current->description, expected->description,
			sizeof (current->description)) &&
		AddonCatalog_ApprovedStringEqual (current->download, expected->download,
			sizeof (current->download)) &&
		current->size == expected->size &&
		current->verified == expected->verified;
}

#ifdef USE_CURL
static qboolean AddonCatalog_IsSafeGameDir (const char *name)
{
	const unsigned char *p;

	if (!name || !*name || strlen(name) >= sizeof(addon_entries[0].gamedir) ||
		!q_strcasecmp(name, GAMENAME) || !strcmp(name, ".") || strstr(name, ".."))
		return false;
	for (p = (const unsigned char *)name; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
			return false;
	return true;
}

static qboolean AddonCatalog_IsSafeDownload (const char *path)
{
	const unsigned char *p;

	if (!path || !*path || strlen(path) >= sizeof(addon_entries[0].download) ||
		path[0] == '/' || path[0] == '\\' || strstr(path, "..") ||
		strstr(path, "//") || strchr(path, '\\') || strchr(path, ':') ||
		strchr(path, '?') || strchr(path, '#'))
		return false;
	for (p = (const unsigned char *)path; *p; p++)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
			*p == '.' || *p == '/'))
			return false;
	return true;
}

static qboolean AddonCatalog_IsSafeBaseURL (const char *url, char *out, size_t outsize)
{
	size_t len;

	if (!url || q_strncasecmp(url, "https://", 8) || strchr(url, '?') || strchr(url, '#'))
		return false;
	if (q_strlcpy (out, url, outsize) >= outsize)
		return false;
	len = strlen(out);
	while (len && out[len - 1] == '/')
		out[--len] = 0;
	return len > 8;
}

static qboolean AddonCatalog_IsInstalled (const char *gamedir)
{
	return COM_GameDirHasPak0(gamedir);
}

static const char *AddonCatalog_InstallRoot (void)
{
	return COM_GetWriteRoot();
}

static qboolean AddonCatalog_AppendJSON (json_t *json, unsigned int operation_id)
{
	const jsonentry_t *addons, *entry;
	addon_catalog_entry_t parsed[ADDON_CATALOG_MAX_ENTRIES];
	int count = 0;

	addons = JSON_Find (json->root, "addons", JSON_ARRAY);
	if (!addons)
		return false;

	for (entry = addons->firstchild; entry && count < ADDON_CATALOG_MAX_ENTRIES; entry = entry->next)
	{
		const char *gamedir, *download, *name, *author, *description;
		const double *size;
		addon_catalog_entry_t item;

		if (entry->type != JSON_OBJECT)
			continue;
		gamedir = JSON_FindString (entry, "gamedir");
		download = JSON_FindString (entry, "download");
		if (!AddonCatalog_IsSafeGameDir(gamedir) || !AddonCatalog_IsSafeDownload(download))
			continue;
		size = JSON_FindNumber (entry, "size");
		if (!size || *size <= 0.0 || *size > ADDON_MAX_PACKAGE || *size != floor(*size))
			continue;

		memset (&item, 0, sizeof(item));
		name = JSON_FindString (entry, "name");
		author = JSON_FindString (entry, "author");
		description = JSON_FindString (JSON_Find(entry, "description", JSON_OBJECT), "en");
		q_strlcpy (item.gamedir, gamedir, sizeof(item.gamedir));
		q_strlcpy (item.download, download, sizeof(item.download));
		q_strlcpy (item.name, name && *name ? name : gamedir, sizeof(item.name));
		q_strlcpy (item.author, author ? author : "", sizeof(item.author));
		q_strlcpy (item.description, description ? description : "", sizeof(item.description));
		item.size = (int)*size;
		item.installed = AddonCatalog_IsInstalled(item.gamedir);
		/* Ironwail's current schema has no verifiable digest field. */
		item.verified = false;

		parsed[count] = item;
		count++;
	}
	if (!count)
		return false;
	SDL_LockMutex (addon_mutex);
	if (AddonAtomicGet (&addon_operation_id) != (int)operation_id ||
		AddonCatalog_IsCancelled (operation_id))
	{
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	memcpy (addon_entries, parsed, sizeof(parsed[0]) * count);
	addon_count = count;
	q_snprintf (addon_message, sizeof(addon_message), "%d add-ons available", count);
	AddonAtomicSet (&addon_state, ADDON_CATALOG_READY);
	SDL_UnlockMutex (addon_mutex);
	return true;
}

typedef struct addon_buffer_s
{
	byte	*data;
	size_t	size;
	size_t	limit;
	unsigned int operation_id;
} addon_buffer_t;

static size_t AddonCatalog_WriteMemory (void *data, size_t size, size_t count, void *userdata)
{
	addon_buffer_t *buffer = (addon_buffer_t *)userdata;
	size_t bytes;
	byte *grown;

	if (AddonCatalog_IsCancelled(buffer->operation_id) || !size || count > SIZE_MAX / size)
		return 0;
	bytes = size * count;
	if (buffer->size > buffer->limit || bytes > buffer->limit - buffer->size)
		return 0;
	grown = (byte *)realloc(buffer->data, buffer->size + bytes + 1);
	if (!grown)
		return 0;
	buffer->data = grown;
	memcpy(buffer->data + buffer->size, data, bytes);
	buffer->size += bytes;
	buffer->data[buffer->size] = 0;
	return bytes;
}

static int AddonCatalog_ProgressCallback (void *unused, curl_off_t dltotal,
	curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	const unsigned int *operation_id = (const unsigned int *)unused;
	(void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
	return AddonCatalog_IsCancelled (*operation_id) ? 1 : 0;
}

static qboolean AddonCatalog_Download (const char *url,
	size_t (*writefn)(void *, size_t, size_t, void *), void *userdata,
	unsigned int operation_id, long *http_status, const char **error)
{
	CURL *curl;
	CURLcode result;

	*http_status = 0;
	*error = NULL;
	curl = curl_easy_init ();
	if (!curl)
	{
		*error = "curl initialization failed";
		return false;
	}
	curl_easy_setopt (curl, CURLOPT_URL, url);
	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, writefn);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, userdata);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt (curl, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt (curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt (curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
	curl_easy_setopt (curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 0L);
	curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 2L);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt (curl, CURLOPT_TIMEOUT, 180L);
	curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, AddonCatalog_ProgressCallback);
	curl_easy_setopt (curl, CURLOPT_XFERINFODATA, &operation_id);
	result = curl_easy_perform (curl);
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, http_status);
	if (result != CURLE_OK)
		*error = curl_easy_strerror(result);
	curl_easy_cleanup (curl);
	return result == CURLE_OK && *http_status == 200 &&
		!AddonCatalog_IsCancelled (operation_id);
}

typedef struct addon_refresh_s
{
	unsigned int operation_id;
	char base_url[MAX_OSPATH];
} addon_refresh_t;

static int AddonCatalog_RefreshThread (void *unused)
{
	char base[MAX_OSPATH], url[MAX_OSPATH];
	addon_buffer_t buffer;
	addon_refresh_t *job = (addon_refresh_t *)unused;
	unsigned int operation_id = job->operation_id;
	long status;
	const char *error;
	json_t *json;

	q_strlcpy (base, job->base_url, sizeof(base));
	free (job);
	memset (&buffer, 0, sizeof(buffer));
	buffer.limit = ADDON_MAX_MANIFEST;
	buffer.operation_id = operation_id;
	if (!base[0] || q_snprintf(url, sizeof(url), "%s/%s", base, ADDON_MANIFEST) >= sizeof(url))
	{
		AddonCatalog_FinishOperation (operation_id, ADDON_CATALOG_ERROR,
			"Invalid HTTPS catalogue URL", "Catalogue refresh cancelled");
		return 1;
	}
	if (!AddonCatalog_Download(url, AddonCatalog_WriteMemory, &buffer, operation_id,
		&status, &error))
	{
		AddonCatalog_FinishOperation (operation_id, ADDON_CATALOG_ERROR,
			error ? error : va("Catalogue HTTP status %ld", status),
			"Catalogue refresh cancelled");
		free(buffer.data);
		return 1;
	}
	json = JSON_Parse ((const char *)buffer.data);
	free(buffer.data);
	if (!json || !AddonCatalog_AppendJSON(json, operation_id))
	{
		if (json) JSON_Free(json);
		AddonCatalog_FinishOperation (operation_id, ADDON_CATALOG_ERROR,
			"Catalogue has no safe add-ons", "Catalogue refresh cancelled");
		return 1;
	}
	JSON_Free(json);
	return 0;
}

typedef struct addon_install_s
{
	addon_catalog_entry_t entry;
	char base_url[MAX_OSPATH];
	char install_root[MAX_OSPATH];
	unsigned int operation_id;
} addon_install_t;

typedef struct addon_file_output_s
{
	FILE *file;
	unsigned int operation_id;
} addon_file_output_t;

static size_t AddonCatalog_WriteFile (void *data, size_t size, size_t count, void *userdata)
{
	addon_file_output_t *output = (addon_file_output_t *)userdata;
	int progress;
	size_t bytes;

	if (AddonCatalog_IsCancelled (output->operation_id) || !size || count > SIZE_MAX / size)
		return 0;
	bytes = size * count;
	progress = AddonAtomicGet (&addon_progress);
	if (progress < 0 || progress > ADDON_MAX_PACKAGE ||
		bytes > ADDON_MAX_PACKAGE - (size_t)progress)
		return 0;
	bytes = fwrite (data, 1, bytes, output->file);
	AddonAtomicAdd (&addon_progress, (int)bytes);
	return bytes;
}

static int AddonCatalog_InstallThread (void *userdata)
{
	addon_install_t *job = (addon_install_t *)userdata;
	char base[MAX_OSPATH], url[MAX_OSPATH], dir[MAX_OSPATH], tmp[MAX_OSPATH], final[MAX_OSPATH];
	FILE *file;
	addon_file_output_t output;
	long status;
	const char *error;
	qboolean ok;
	qboolean final_exists;

	q_strlcpy(base, job->base_url, sizeof(base));
	if (!base[0] || q_snprintf(url, sizeof(url), "%s/%s", base, job->entry.download) >= sizeof(url) ||
		q_snprintf(dir, sizeof(dir), "%s/%s", job->install_root, job->entry.gamedir) >= sizeof(dir) ||
		q_snprintf(tmp, sizeof(tmp), "%s/pak0.catalog.tmp", dir) >= sizeof(tmp) ||
		q_snprintf(final, sizeof(final), "%s/pak0.pak", dir) >= sizeof(final))
	{
		AddonCatalog_FinishOperation (job->operation_id, ADDON_CATALOG_ERROR,
			"Unsafe add-on install path", "Add-on download cancelled");
		free(job);
		return 1;
	}
	/* Sys_fopen preserves UTF-8 paths on Windows and creates parent folders. */
	file = Sys_fopen(tmp, "wb");
	if (!file)
	{
		AddonCatalog_FinishOperation (job->operation_id, ADDON_CATALOG_ERROR,
			"Could not create add-on temporary file", "Add-on download cancelled");
		free(job);
		return 1;
	}
	output.file = file;
	output.operation_id = job->operation_id;
	AddonAtomicSet (&addon_progress, 0);
	ok = AddonCatalog_Download (url, AddonCatalog_WriteFile, &output,
		job->operation_id, &status, &error);
	if (fclose(file) != 0)
		ok = false;
	if (!ok || AddonCatalog_IsCancelled (job->operation_id) ||
		AddonAtomicGet (&addon_progress) != job->entry.size)
	{
		Sys_remove (tmp);
		AddonCatalog_FinishOperation (job->operation_id, ADDON_CATALOG_ERROR,
			error ? error : "Add-on download failed size check", "Add-on download cancelled");
		free(job);
		return 1;
	}
	if (!COM_ValidateAddonPackFile (tmp, job->entry.size))
	{
		Sys_remove (tmp);
		AddonCatalog_FinishOperation (job->operation_id, ADDON_CATALOG_ERROR,
			"Downloaded add-on is not a valid Quake PACK file", "Add-on download cancelled");
		free(job);
		return 1;
	}
	final_exists = (Sys_FileType (final) & FS_ENT_FILE) != 0;

	/* Commit point: after this marker, CancelOperation ignores this operation.
	 * Complete cleanup or rename outside the UI mutex before publishing a result.
	 */
	SDL_LockMutex (addon_mutex);
	if (AddonAtomicGet (&addon_operation_id) != (int)job->operation_id ||
		AddonCatalog_IsCancelled (job->operation_id))
	{
		SDL_UnlockMutex (addon_mutex);
		Sys_remove (tmp);
		AddonCatalog_FinishOperation (job->operation_id, ADDON_CATALOG_ERROR,
			"Add-on install cancelled", "Add-on download cancelled");
		free(job);
		return 1;
	}
	addon_install_committing_operation = job->operation_id;
	SDL_UnlockMutex (addon_mutex);

	if (final_exists)
	{
		Sys_remove (tmp);
		AddonCatalog_FinishInstallOperation (job->operation_id, job->entry.gamedir,
			true, ADDON_CATALOG_READY, "Add-on is already installed",
			"Add-on download cancelled");
		free(job);
		return 0;
	}

	if (Sys_rename (tmp, final) != 0)
	{
		Sys_remove (tmp);
		AddonCatalog_FinishInstallOperation (job->operation_id, job->entry.gamedir,
			false, ADDON_CATALOG_ERROR, "Could not finalize add-on install",
			"Add-on download cancelled");
		free(job);
		return 1;
	}
	AddonCatalog_FinishInstallOperation (job->operation_id, job->entry.gamedir,
		true, ADDON_CATALOG_READY, "Add-on installed; select it from Mods",
		"Add-on download cancelled");
	free(job);
	return 0;
}
#endif /* USE_CURL */

void AddonCatalog_Init (void)
{
	addon_mutex = SDL_CreateMutex ();
	if (!addon_mutex)
		return;
	addon_operation_sequence = 0;
	addon_install_committing_operation = 0;
	AddonAtomicSet (&addon_operation_id, 0);
	AddonAtomicSet (&addon_cancelled_operation, 0);
	AddonAtomicSet (&addon_progress, 0);
	Cvar_RegisterVariable (&cl_addon_catalog_url);
	Cmd_AddCommand ("addon_refresh", AddonCatalog_Refresh);
	Cmd_AddCommand ("addon_cancel", AddonCatalog_Cancel);
#ifdef USE_CURL
	curl_global_init (CURL_GLOBAL_DEFAULT);
	AddonAtomicSet(&addon_state, ADDON_CATALOG_IDLE);
	AddonCatalog_SetMessage("Catalogue ready to refresh");
#else
	AddonAtomicSet(&addon_state, ADDON_CATALOG_UNAVAILABLE);
	AddonCatalog_SetMessage("Catalogue downloads disabled in this build");
#endif
}

void AddonCatalog_Shutdown (void)
{
	AddonCatalog_Cancel ();
	if (addon_refresh_thread)
		SDL_WaitThread(addon_refresh_thread, NULL);
	if (addon_install_thread)
		SDL_WaitThread(addon_install_thread, NULL);
	addon_refresh_thread = addon_install_thread = NULL;
#ifdef USE_CURL
	curl_global_cleanup ();
#endif
	if (addon_mutex)
		SDL_DestroyMutex(addon_mutex);
	addon_mutex = NULL;
}

void AddonCatalog_Poll (void)
{
	addon_catalog_state_t state = (addon_catalog_state_t)AddonAtomicGet(&addon_state);
	if (addon_refresh_thread && state != ADDON_CATALOG_REFRESHING)
	{
		SDL_WaitThread(addon_refresh_thread, NULL);
		addon_refresh_thread = NULL;
	}
	if (addon_install_thread && state != ADDON_CATALOG_INSTALLING)
	{
		SDL_WaitThread(addon_install_thread, NULL);
		addon_install_thread = NULL;
	}
}

void AddonCatalog_Refresh (void)
{
#ifdef USE_CURL
	char base[MAX_OSPATH];
	addon_refresh_t *job;
	unsigned int operation_id;
	addon_catalog_state_t state;

	AddonCatalog_Poll ();
	if (!addon_mutex)
		return;
	SDL_LockMutex (addon_mutex);
	state = (addon_catalog_state_t)AddonAtomicGet (&addon_state);
	if (addon_refresh_thread || addon_install_thread ||
		state == ADDON_CATALOG_REFRESHING || state == ADDON_CATALOG_INSTALLING)
	{
		SDL_UnlockMutex (addon_mutex);
		return;
	}
	if (!AddonCatalog_IsSafeBaseURL (cl_addon_catalog_url.string, base, sizeof(base)))
	{
		AddonAtomicSet (&addon_state, ADDON_CATALOG_ERROR);
		q_strlcpy (addon_message, "Invalid HTTPS catalogue URL", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return;
	}
	job = (addon_refresh_t *)malloc (sizeof (*job));
	if (!job)
	{
		q_strlcpy (addon_message, "Could not allocate catalogue worker", sizeof(addon_message));
		AddonAtomicSet (&addon_state, ADDON_CATALOG_ERROR);
		SDL_UnlockMutex (addon_mutex);
		return;
	}
	operation_id = AddonCatalog_NextOperationId ();
	job->operation_id = operation_id;
	q_strlcpy (job->base_url, base, sizeof(job->base_url));
	AddonAtomicSet (&addon_cancelled_operation, 0);
	AddonAtomicSet (&addon_state, ADDON_CATALOG_REFRESHING);
	q_strlcpy (addon_message, "Refreshing add-on catalogue...", sizeof(addon_message));
	addon_refresh_thread = SDL_CreateThread (AddonCatalog_RefreshThread,
		"Addon catalogue", job);
	if (!addon_refresh_thread)
	{
		free (job);
		AddonAtomicSet (&addon_state, ADDON_CATALOG_ERROR);
		q_strlcpy (addon_message, "Could not start catalogue worker", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return;
	}
	addon_operation_sequence = operation_id;
	q_strlcpy (addon_base_url, base, sizeof(addon_base_url));
	AddonAtomicSet (&addon_operation_id, (int)operation_id);
	SDL_UnlockMutex (addon_mutex);
#else
	if (addon_mutex)
		AddonCatalog_SetMessage("Catalogue downloads disabled in this build");
#endif
}

void AddonCatalog_Cancel (void)
{
	AddonCatalog_CancelOperation (AddonCatalog_OperationId ());
}

unsigned int AddonCatalog_OperationId (void)
{
	return addon_mutex ? (unsigned int)AddonAtomicGet (&addon_operation_id) : 0;
}

void AddonCatalog_CancelOperation (unsigned int id)
{
	addon_catalog_state_t state;

	if (!addon_mutex || !id || id > INT_MAX)
		return;
	SDL_LockMutex (addon_mutex);
	state = (addon_catalog_state_t)AddonAtomicGet (&addon_state);
	if (AddonAtomicGet (&addon_operation_id) == (int)id &&
		(state == ADDON_CATALOG_REFRESHING || state == ADDON_CATALOG_INSTALLING) &&
		addon_install_committing_operation != id)
		AddonAtomicSet (&addon_cancelled_operation, (int)id);
	SDL_UnlockMutex (addon_mutex);
}

addon_catalog_state_t AddonCatalog_State (void)
{
	return (addon_catalog_state_t)AddonAtomicGet(&addon_state);
}

const char *AddonCatalog_Message (void)
{
	if (!addon_mutex)
		return "Catalogue unavailable";
	SDL_LockMutex (addon_mutex);
	q_strlcpy (addon_message_snapshot, addon_message, sizeof(addon_message_snapshot));
	SDL_UnlockMutex (addon_mutex);
	return addon_message_snapshot;
}

int AddonCatalog_Count (void)
{
	int count;
	if (!addon_mutex)
		return 0;
	SDL_LockMutex (addon_mutex);
	count = addon_count;
	SDL_UnlockMutex (addon_mutex);
	return count;
}

const addon_catalog_entry_t *AddonCatalog_Entry (int index)
{
	if (!addon_mutex)
		return NULL;
	SDL_LockMutex (addon_mutex);
	if (index < 0 || index >= addon_count)
	{
		SDL_UnlockMutex (addon_mutex);
		return NULL;
	}
	addon_entry_snapshot = addon_entries[index];
	SDL_UnlockMutex (addon_mutex);
	return &addon_entry_snapshot;
}

int AddonCatalog_FindGameDir (const char *gamedir,
	addon_catalog_entry_t *entry)
{
	int i, found = -1;

	if (!addon_mutex || !gamedir || !*gamedir)
		return -1;
	SDL_LockMutex (addon_mutex);
	for (i = 0; i < addon_count; i++)
	{
		if (q_strcasecmp(addon_entries[i].gamedir, gamedir))
			continue;
		if (entry)
			*entry = addon_entries[i];
		found = i;
		break;
	}
	SDL_UnlockMutex (addon_mutex);
	return found;
}

static qboolean AddonCatalog_StartInstallInternal (int index,
	const addon_catalog_entry_t *expected, qboolean allow_unverified)
{
#ifdef USE_CURL
	addon_install_t *job;
	addon_catalog_entry_t *entry;
	unsigned int operation_id;
	addon_catalog_state_t state;

	AddonCatalog_Poll ();
	if (!addon_mutex)
		return false;
	SDL_LockMutex (addon_mutex);
	state = (addon_catalog_state_t)AddonAtomicGet (&addon_state);
	if (addon_install_thread || addon_refresh_thread || state != ADDON_CATALOG_READY)
	{
		q_strlcpy (addon_message, "Catalogue is not ready; refresh before installing",
			sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	if (index < 0 || index >= addon_count)
	{
		q_strlcpy (addon_message, "Catalogue entry is no longer available",
			sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	entry = &addon_entries[index];
	if (expected && !AddonCatalog_EntryMatchesApproved (entry, expected))
	{
		q_strlcpy (addon_message, "Catalogue entry changed; select it again",
			sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	if (entry->installed)
	{
		q_strlcpy (addon_message, "Add-on is already installed", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	if (!entry->verified && !allow_unverified)
	{
		q_strlcpy (addon_message, "Unverified: select again to confirm", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	job = (addon_install_t *)malloc (sizeof(*job));
	if (!job)
	{
		q_strlcpy (addon_message, "Could not allocate add-on installer", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	job->entry = *entry;
	q_strlcpy (job->base_url, addon_base_url, sizeof(job->base_url));
	if (q_strlcpy (job->install_root, AddonCatalog_InstallRoot(),
		sizeof(job->install_root)) >= sizeof(job->install_root))
	{
		free (job);
		q_strlcpy (addon_message, "Add-on install root is too long", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	operation_id = AddonCatalog_NextOperationId ();
	job->operation_id = operation_id;
	addon_install_committing_operation = 0;
	AddonAtomicSet (&addon_cancelled_operation, 0);
	AddonAtomicSet (&addon_progress, 0);
	AddonAtomicSet (&addon_state, ADDON_CATALOG_INSTALLING);
	q_strlcpy (addon_message, "Downloading add-on...", sizeof(addon_message));
	addon_install_thread = SDL_CreateThread (AddonCatalog_InstallThread,
		"Addon install", job);
	if (!addon_install_thread)
	{
		free (job);
		AddonAtomicSet (&addon_state, ADDON_CATALOG_ERROR);
		q_strlcpy (addon_message, "Could not start add-on installer", sizeof(addon_message));
		SDL_UnlockMutex (addon_mutex);
		return false;
	}
	addon_operation_sequence = operation_id;
	AddonAtomicSet (&addon_operation_id, (int)operation_id);
	SDL_UnlockMutex (addon_mutex);
	return true;
#else
	(void)index; (void)expected; (void)allow_unverified;
	if (addon_mutex)
		AddonCatalog_SetMessage("Catalogue downloads disabled in this build");
	return false;
#endif
}

qboolean AddonCatalog_StartInstall (int index, qboolean allow_unverified)
{
	return AddonCatalog_StartInstallInternal (index, NULL, allow_unverified);
}

qboolean AddonCatalog_StartInstallApproved (int index,
	const addon_catalog_entry_t *expected, qboolean allow_unverified)
{
	if (!expected)
	{
		if (addon_mutex)
			AddonCatalog_SetMessage ("Catalogue approval entry is missing");
		return false;
	}
	return AddonCatalog_StartInstallInternal (index, expected, allow_unverified);
}

float AddonCatalog_Progress (void)
{
	return (float)AddonAtomicGet(&addon_progress);
}
