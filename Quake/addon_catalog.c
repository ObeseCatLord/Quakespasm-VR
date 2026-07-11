/*
 * Based on Ironwail's add-on catalogue design: content.json, a background
 * libcurl transfer, temporary-file install, and a menu-driven workflow.
 * Unlike the original implementation, this module does not fetch at startup,
 * follows no redirects, bounds all input, and rejects unsafe install paths.
 */
#include "quakedef.h"
#include "addon_catalog.h"
#include "json.h"

#ifdef USE_CURL
#include <curl/curl.h>
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
static SDL_mutex *addon_mutex;
static SDL_atomic_t addon_cancel;
static SDL_atomic_t addon_state;
static SDL_atomic_t addon_progress;
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
	q_strlcpy (out, url, outsize);
	len = strlen(out);
	while (len && out[len - 1] == '/')
		out[--len] = 0;
	return len > 8;
}

static qboolean AddonCatalog_IsInstalled (const char *gamedir)
{
	char path[MAX_OSPATH];
	q_snprintf (path, sizeof(path), "%s/%s/pak0.pak", com_basedir, gamedir);
	return Sys_FileType(path) & FS_ENT_FILE;
}

static qboolean AddonCatalog_AppendJSON (json_t *json)
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
	memcpy (addon_entries, parsed, sizeof(parsed));
	addon_count = count;
	SDL_UnlockMutex (addon_mutex);
	return true;
}

typedef struct addon_buffer_s
{
	byte	*data;
	size_t	size;
	size_t	limit;
} addon_buffer_t;

static size_t AddonCatalog_WriteMemory (void *data, size_t size, size_t count, void *userdata)
{
	addon_buffer_t *buffer = (addon_buffer_t *)userdata;
	size_t bytes;
	byte *grown;

	if (SDL_AtomicGet(&addon_cancel) || !size || count > SIZE_MAX / size)
		return 0;
	bytes = size * count;
	if (bytes > buffer->limit - buffer->size)
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
	(void)unused; (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
	return SDL_AtomicGet(&addon_cancel) ? 1 : 0;
}

static qboolean AddonCatalog_Download (const char *url,
	size_t (*writefn)(void *, size_t, size_t, void *), void *userdata,
	long *http_status, const char **error)
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
	result = curl_easy_perform (curl);
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, http_status);
	if (result != CURLE_OK)
		*error = curl_easy_strerror(result);
	curl_easy_cleanup (curl);
	return result == CURLE_OK && *http_status == 200 && !SDL_AtomicGet(&addon_cancel);
}

static int AddonCatalog_RefreshThread (void *unused)
{
	char base[MAX_OSPATH], url[MAX_OSPATH];
	addon_buffer_t buffer;
	long status;
	const char *error;
	json_t *json;

	(void)unused;
	memset (&buffer, 0, sizeof(buffer));
	buffer.limit = ADDON_MAX_MANIFEST;
	q_strlcpy(base, addon_base_url, sizeof(base));
	if (!base[0] || q_snprintf(url, sizeof(url), "%s/%s", base, ADDON_MANIFEST) >= sizeof(url))
	{
		AddonCatalog_SetMessage("Invalid HTTPS catalogue URL");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		return 1;
	}
	if (!AddonCatalog_Download(url, AddonCatalog_WriteMemory, &buffer, &status, &error))
	{
		AddonCatalog_SetMessage(error ? error : va("Catalogue HTTP status %ld", status));
		free(buffer.data);
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		return 1;
	}
	json = JSON_Parse ((const char *)buffer.data);
	free(buffer.data);
	if (!json || !AddonCatalog_AppendJSON(json))
	{
		if (json) JSON_Free(json);
		AddonCatalog_SetMessage("Catalogue has no safe add-ons");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		return 1;
	}
	JSON_Free(json);
	AddonCatalog_SetMessage(va("%d add-ons available", AddonCatalog_Count()));
	SDL_AtomicSet(&addon_state, ADDON_CATALOG_READY);
	return 0;
}

typedef struct addon_install_s
{
	addon_catalog_entry_t entry;
	char base_url[MAX_OSPATH];
} addon_install_t;

static size_t AddonCatalog_WriteFile (void *data, size_t size, size_t count, void *userdata)
{
	FILE *file = (FILE *)userdata;
	size_t bytes;

	if (SDL_AtomicGet(&addon_cancel) || !size || count > SIZE_MAX / size)
		return 0;
	bytes = size * count;
	if (bytes > ADDON_MAX_PACKAGE - (size_t)SDL_AtomicGet(&addon_progress))
		return 0;
	bytes = fwrite(data, 1, bytes, file);
	SDL_AtomicAdd(&addon_progress, (int)bytes);
	return bytes;
}

static int AddonCatalog_InstallThread (void *userdata)
{
	addon_install_t *job = (addon_install_t *)userdata;
	char base[MAX_OSPATH], url[MAX_OSPATH], dir[MAX_OSPATH], tmp[MAX_OSPATH], final[MAX_OSPATH];
	FILE *file;
	long status;
	const char *error;
	qboolean ok;

	q_strlcpy(base, job->base_url, sizeof(base));
	if (!base[0] || q_snprintf(url, sizeof(url), "%s/%s", base, job->entry.download) >= sizeof(url) ||
		q_snprintf(dir, sizeof(dir), "%s/%s", com_basedir, job->entry.gamedir) >= sizeof(dir) ||
		q_snprintf(tmp, sizeof(tmp), "%s/pak0.catalog.tmp", dir) >= sizeof(tmp) ||
		q_snprintf(final, sizeof(final), "%s/pak0.pak", dir) >= sizeof(final))
	{
		AddonCatalog_SetMessage("Unsafe add-on install path");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		free(job);
		return 1;
	}
	if (Sys_FileType(final) & FS_ENT_FILE)
	{
		AddonCatalog_SetMessage("Add-on is already installed");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_READY);
		free(job);
		return 1;
	}
	Sys_mkdir(dir);
	file = fopen(tmp, "wb");
	if (!file)
	{
		AddonCatalog_SetMessage("Could not create add-on temporary file");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		free(job);
		return 1;
	}
	SDL_AtomicSet(&addon_progress, 0);
	ok = AddonCatalog_Download(url, AddonCatalog_WriteFile, file, &status, &error);
	fclose(file);
	if (!ok || SDL_AtomicGet(&addon_progress) != job->entry.size)
	{
		remove(tmp);
		AddonCatalog_SetMessage(error ? error : "Add-on download failed size check");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		free(job);
		return 1;
	}
	if (rename(tmp, final) != 0)
	{
		remove(tmp);
		AddonCatalog_SetMessage("Could not finalize add-on install");
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		free(job);
		return 1;
	}
	SDL_LockMutex (addon_mutex);
	for (int i = 0; i < addon_count; i++)
		if (!q_strcasecmp(addon_entries[i].gamedir, job->entry.gamedir))
			addon_entries[i].installed = true;
	SDL_UnlockMutex (addon_mutex);
	AddonCatalog_SetMessage("Add-on installed; select it from Mods");
	SDL_AtomicSet(&addon_state, ADDON_CATALOG_READY);
	free(job);
	return 0;
}
#endif /* USE_CURL */

void AddonCatalog_Init (void)
{
	addon_mutex = SDL_CreateMutex ();
	if (!addon_mutex)
		return;
	Cvar_RegisterVariable (&cl_addon_catalog_url);
	Cmd_AddCommand ("addon_refresh", AddonCatalog_Refresh);
	Cmd_AddCommand ("addon_cancel", AddonCatalog_Cancel);
#ifdef USE_CURL
	curl_global_init (CURL_GLOBAL_DEFAULT);
	SDL_AtomicSet(&addon_state, ADDON_CATALOG_IDLE);
	AddonCatalog_SetMessage("Press F1 to refresh add-on catalogue");
#else
	SDL_AtomicSet(&addon_state, ADDON_CATALOG_UNAVAILABLE);
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
	addon_catalog_state_t state = (addon_catalog_state_t)SDL_AtomicGet(&addon_state);
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
	AddonCatalog_Poll ();
	if (!addon_mutex || addon_refresh_thread || addon_install_thread)
		return;
	if (!AddonCatalog_IsSafeBaseURL(cl_addon_catalog_url.string, addon_base_url,
		sizeof(addon_base_url)))
	{
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		AddonCatalog_SetMessage("Invalid HTTPS catalogue URL");
		return;
	}
	SDL_AtomicSet(&addon_cancel, 0);
	SDL_AtomicSet(&addon_state, ADDON_CATALOG_REFRESHING);
	AddonCatalog_SetMessage("Refreshing add-on catalogue...");
	addon_refresh_thread = SDL_CreateThread(AddonCatalog_RefreshThread, "Addon catalogue", NULL);
	if (!addon_refresh_thread)
	{
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		AddonCatalog_SetMessage("Could not start catalogue worker");
	}
#else
	AddonCatalog_SetMessage("Catalogue downloads disabled in this build");
#endif
}

void AddonCatalog_Cancel (void)
{
	SDL_AtomicSet(&addon_cancel, 1);
}

addon_catalog_state_t AddonCatalog_State (void)
{
	return (addon_catalog_state_t)SDL_AtomicGet(&addon_state);
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

qboolean AddonCatalog_StartInstall (int index, qboolean allow_unverified)
{
#ifdef USE_CURL
	addon_install_t *job;
	const addon_catalog_entry_t *entry;

	AddonCatalog_Poll ();
	if (addon_install_thread || addon_refresh_thread || AddonCatalog_State() != ADDON_CATALOG_READY)
		return false;
	entry = AddonCatalog_Entry(index);
	if (!entry || entry->installed)
		return false;
	if (!entry->verified && !allow_unverified)
	{
		AddonCatalog_SetMessage("Unverified package: press Enter again to confirm");
		return false;
	}
	job = (addon_install_t *)malloc(sizeof(*job));
	if (!job)
		return false;
	job->entry = *entry;
	q_strlcpy(job->base_url, addon_base_url, sizeof(job->base_url));
	SDL_AtomicSet(&addon_cancel, 0);
	SDL_AtomicSet(&addon_progress, 0);
	SDL_AtomicSet(&addon_state, ADDON_CATALOG_INSTALLING);
	AddonCatalog_SetMessage("Downloading add-on...");
	addon_install_thread = SDL_CreateThread(AddonCatalog_InstallThread, "Addon install", job);
	if (!addon_install_thread)
	{
		free(job);
		SDL_AtomicSet(&addon_state, ADDON_CATALOG_ERROR);
		AddonCatalog_SetMessage("Could not start add-on installer");
		return false;
	}
	return true;
#else
	(void)index; (void)allow_unverified;
	AddonCatalog_SetMessage("Catalogue downloads disabled in this build");
	return false;
#endif
}

float AddonCatalog_Progress (void)
{
	return (float)SDL_AtomicGet(&addon_progress);
}
