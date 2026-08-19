/*
Copyright (C) 2026 Quakespasm VR contributors

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

steam.c -- Steam libraryfolders.vdf and appmanifest_2310.acf discovery
*/
#include "quakedef.h"
#include "q_ctype.h"
#include "steam.h"

typedef struct vdf_context_s vdf_context_t;
typedef void (*vdf_callback_t) (vdf_context_t *ctx, const char *key, const char *value);

struct vdf_context_s
{
	void			*userdata;
	vdf_callback_t	callback;
	int			depth;
	const char		*path[64];
};

static void VDF_SkipSpace (char **buf)
{
	while (**buf && q_isspace ((unsigned char)**buf))
		++*buf;
}

static char *VDF_ParseString (char **buf)
{
	char *out, *start;

	VDF_SkipSpace (buf);
	if (**buf != '"')
		return NULL;

	start = out = ++*buf;
	while (**buf && **buf != '"')
	{
		if (**buf == '\\')
		{
			++*buf;
			if (!**buf)
				return NULL;
			/* Steam VDF needs only quoted paths; preserve unknown escapes safely. */
			if (**buf == 'n') *out++ = '\n';
			else if (**buf == 'r') *out++ = '\r';
			else if (**buf == 't') *out++ = '\t';
			else *out++ = **buf;
			++*buf;
		}
		else
			*out++ = *(*buf)++;
	}
	if (**buf != '"')
		return NULL;
	++*buf;
	*out = 0;
	return start;
}

static qboolean VDF_ParseEntries (char **buf, vdf_context_t *ctx, qboolean nested)
{
	for (;;)
	{
		char *key, *value;
		VDF_SkipSpace (buf);
		if (!**buf)
			return !nested;
		if (**buf == '}')
		{
			if (!nested)
				return false;
			++*buf;
			return true;
		}

		key = VDF_ParseString (buf);
		if (!key)
			return false;
		VDF_SkipSpace (buf);
		if (**buf == '"')
		{
			value = VDF_ParseString (buf);
			if (!value)
				return false;
			ctx->callback (ctx, key, value);
			continue;
		}
		if (**buf != '{' || ctx->depth >= (int)countof(ctx->path))
			return false;
		++*buf;
		ctx->path[ctx->depth++] = key;
		if (!VDF_ParseEntries (buf, ctx, true))
			return false;
		--ctx->depth;
	}
}

static qboolean VDF_Parse (char *buf, vdf_callback_t callback, void *userdata)
{
	vdf_context_t ctx;
	size_t len;
	if (!buf || !callback)
		return false;
	len = strlen(buf);
	if (len >= 3 && (unsigned char)buf[0] == 0xef && (unsigned char)buf[1] == 0xbb &&
		(unsigned char)buf[2] == 0xbf)
		buf += 3;
	memset (&ctx, 0, sizeof(ctx));
	ctx.userdata = userdata;
	ctx.callback = callback;
	return VDF_ParseEntries (&buf, &ctx, false);
}

#define STEAM_MAX_LIBRARIES 32
typedef struct
{
	char paths[STEAM_MAX_LIBRARIES][MAX_OSPATH];
	int count;
} steam_libraries_t;

static qboolean Steam_IsLibraryPath (vdf_context_t *ctx)
{
	return ctx->depth >= 1 && !q_strcasecmp(ctx->path[0], "libraryfolders");
}

static void Steam_AddLibrary (steam_libraries_t *libraries, const char *path)
{
	int i;
	if (!path || !*path || libraries->count == STEAM_MAX_LIBRARIES)
		return;
	for (i = 0; i < libraries->count; ++i)
		if (!q_strcasecmp(libraries->paths[i], path))
			return;
	q_strlcpy (libraries->paths[libraries->count++], path,
			sizeof(libraries->paths[0]));
}

static void Steam_OnLibraryProperty (vdf_context_t *ctx, const char *key, const char *value)
{
	steam_libraries_t *libraries = ctx->userdata;
	char *end;
	if (!Steam_IsLibraryPath(ctx))
		return;
	/* Current VDF: libraryfolders/<n>/path.  Old VDF: libraryfolders/<n>. */
	if (ctx->depth == 2 && !q_strcasecmp(key, "path"))
		Steam_AddLibrary (libraries, value);
	else if (ctx->depth == 1 && strtol(key, &end, 10) >= 0 && end != key && !*end)
		Steam_AddLibrary (libraries, value);
}

typedef struct
{
	char installdir[MAX_OSPATH];
	qboolean appid_ok;
} steam_manifest_t;

static qboolean Steam_IsSafeInstallDir (const char *path)
{
	return path && *path && !strstr(path, "..") && !strchr(path, '/') && !strchr(path, '\\');
}

static void Steam_OnManifestProperty (vdf_context_t *ctx, const char *key, const char *value)
{
	steam_manifest_t *manifest = ctx->userdata;
	if (ctx->depth != 1 || q_strcasecmp(ctx->path[0], "AppState"))
		return;
	if (!q_strcasecmp(key, "appid") && !strcmp(value, "2310"))
		manifest->appid_ok = true;
	else if (!q_strcasecmp(key, "installdir") && Steam_IsSafeInstallDir(value))
		q_strlcpy(manifest->installdir, value, sizeof(manifest->installdir));
}

qboolean Steam_FindQuakeInstall (steam_quake_install_t *install)
{
	char steamdir[MAX_OSPATH], vdfpath[MAX_OSPATH], manifestpath[MAX_OSPATH];
	char *vdf;
	steam_libraries_t libraries;
	int i;

	if (!install || !Sys_GetSteamDir(steamdir, sizeof(steamdir)))
		return false;
	memset(install, 0, sizeof(*install));
	if (q_snprintf(vdfpath, sizeof(vdfpath), "%s/config/libraryfolders.vdf", steamdir) >= (int)sizeof(vdfpath))
		return false;
	vdf = (char *)COM_LoadMallocFile_TextMode_OSPath(vdfpath, NULL);
	if (!vdf)
		return false;
	memset(&libraries, 0, sizeof(libraries));
	if (!VDF_Parse(vdf, Steam_OnLibraryProperty, &libraries))
	{
		free(vdf);
		return false;
	}
	free(vdf);

	for (i = 0; i < libraries.count; ++i)
	{
		char *acf;
		steam_manifest_t manifest;
		if (q_snprintf(manifestpath, sizeof(manifestpath),
			"%s/steamapps/appmanifest_%d.acf", libraries.paths[i], QUAKE_STEAM_APPID) >= (int)sizeof(manifestpath))
			continue;
		acf = (char *)COM_LoadMallocFile_TextMode_OSPath(manifestpath, NULL);
		if (!acf)
			continue;
		memset(&manifest, 0, sizeof(manifest));
		if (VDF_Parse(acf, Steam_OnManifestProperty, &manifest) && manifest.appid_ok && manifest.installdir[0] &&
			q_snprintf(install->path, sizeof(install->path), "%s/steamapps/common/%s", libraries.paths[i], manifest.installdir) < (int)sizeof(install->path) &&
			(Sys_FileType(install->path) & FS_ENT_DIRECTORY))
		{
			q_strlcpy(install->library, libraries.paths[i], sizeof(install->library));
			free(acf);
			return true;
		}
		free(acf);
	}
	return false;
}
