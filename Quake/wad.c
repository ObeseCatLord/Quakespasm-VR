/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// wad.c

#include "quakedef.h"

int				wad_numlumps;
lumpinfo_t		*wad_lumps;
byte			*wad_base = NULL;

#define MAPWAD_MAX_LUMPS 65536
#define MAPWAD_MAX_FILES 32

void SwapPic (qpic_t *pic);

/*
==================
W_CleanupName

Lowercases name and pads with spaces and a terminating 0 to the length of
lumpinfo_t->name.
Used so lumpname lookups can proceed rapidly by comparing 4 chars at a time
Space padding is so names can be printed nicely in tables.
Can safely be performed in place.
==================
*/
void W_CleanupName (const char *in, char *out)
{
	int		i;
	int		c;

	for (i=0 ; i<16 ; i++ )
	{
		c = in[i];
		if (!c)
			break;

		if (c >= 'A' && c <= 'Z')
			c += ('a' - 'A');
		out[i] = c;
	}

	for ( ; i< 16 ; i++ )
		out[i] = 0;
}

/*
====================
W_LoadWadFile
====================
*/
void W_LoadWadFile (void) //johnfitz -- filename is now hard-coded for honesty
{
	lumpinfo_t		*lump_p;
	wadinfo_t		*header;
	int			i;
	int			infotableofs;
	const char		*filename = WADFILENAME;

	//johnfitz -- modified to use malloc
	//TODO: use cache_alloc
	if (wad_base)
		free (wad_base);
	wad_base = COM_LoadMallocFile (filename, NULL);
	if (!wad_base)
		Sys_Error ("W_LoadWadFile: couldn't load %s\n\n"
			   "Basedir is: %s\n\n"
			   "Check that this has an " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
			   "or use the -basedir command-line option to specify another directory.",
			   filename, com_basedir);

	header = (wadinfo_t *)wad_base;

	if (header->identification[0] != 'W' || header->identification[1] != 'A'
	 || header->identification[2] != 'D' || header->identification[3] != '2')
		Sys_Error ("Wad file %s doesn't have WAD2 id\n",filename);

	wad_numlumps = LittleLong(header->numlumps);
	infotableofs = LittleLong(header->infotableofs);
	wad_lumps = (lumpinfo_t *)(wad_base + infotableofs);

	for (i=0, lump_p = wad_lumps ; i<wad_numlumps ; i++,lump_p++)
	{
		lump_p->filepos = LittleLong(lump_p->filepos);
		lump_p->size = LittleLong(lump_p->size);
		W_CleanupName (lump_p->name, lump_p->name);	// CAUTION: in-place editing!!!
		if (lump_p->type == TYP_QPIC)
			SwapPic ( (qpic_t *)(wad_base + lump_p->filepos));
	}
}


/*
=============
W_GetLumpinfo
=============
*/
lumpinfo_t	*W_GetLumpinfo (const char *name)
{
	int		i;
	lumpinfo_t	*lump_p;
	char	clean[16];

	W_CleanupName (name, clean);

	for (lump_p=wad_lumps, i=0 ; i<wad_numlumps ; i++,lump_p++)
	{
		if (!strcmp(clean, lump_p->name))
			return lump_p;
	}

	Con_SafePrintf ("W_GetLumpinfo: %s not found\n", name); //johnfitz -- was Sys_Error
	return NULL;
}

void *W_GetLumpName (const char *name, lumpinfo_t **out_info)	//Spike: so caller can verify that the qpic was written properly.
{
	lumpinfo_t	*lump;

	lump = W_GetLumpinfo (name);

	if (!lump) return NULL; //johnfitz

	if (out_info)
		*out_info = lump;
	return (void *)(wad_base + lump->filepos);
}

void *W_GetLumpNum (int num)
{
	lumpinfo_t	*lump;

	if (num < 0 || num > wad_numlumps)
		Sys_Error ("W_GetLumpNum: bad number: %i", num);

	lump = wad_lumps + num;

	return (void *)(wad_base + lump->filepos);
}

/*
=================
W_OpenMapWadFile

Map WADs are opened through the regular, already-approved game search paths.
They deliberately remain separate from the global filesystem search path.
=================
*/
static qboolean W_OpenMapWadFile (const char *filename, fshandle_t *fh)
{
	FILE	*f;
	int	length;

	f = NULL;
	length = COM_FOpenFile (filename, &f, NULL);
	if (length < 0 || !f)
		return false;

	fh->file = f;
	fh->start = ftell (f);
	fh->pos = 0;
	fh->length = length;
	fh->pak = file_from_pak;

	if (fh->start < 0)
	{
		fclose (f);
		fh->file = NULL;
		return false;
	}

	return true;
}

/*
=================
W_ExtractMapWadBasename

The worldspawn WAD value comes from map content. Keep only a conservative,
extensionless basename before constructing a path below, so it cannot request
an arbitrary file or escape the normal game search paths.
=================
*/
static qboolean W_ExtractMapWadBasename (const char *in, size_t inlen,
	char *out, size_t outsize)
{
	const char	*base, *end, *p, *dot;
	size_t		len;

	while (inlen && (*in == ' ' || *in == '\t'))
	{
		in++;
		inlen--;
	}
	while (inlen && (in[inlen - 1] == ' ' || in[inlen - 1] == '\t'))
		inlen--;
	if (!inlen)
		return false;

	base = in;
	end = in + inlen;
	for (p = in; p < end; p++)
		if (*p == '/' || *p == '\\')
			base = p + 1;
	if (base == end)
		return false;

	dot = NULL;
	for (p = base; p < end; p++)
		if (*p == '.')
			dot = p;
	if (dot)
	{
		if ((size_t)(end - dot) != 4 || q_strncasecmp (dot, ".wad", 4))
			return false;
		end = dot;
	}

	len = (size_t)(end - base);
	if (!len || len >= outsize || (len == 1 && base[0] == '.') ||
		(len == 2 && base[0] == '.' && base[1] == '.'))
		return false;

	for (p = base; p < end; p++)
	{
		unsigned char c = (unsigned char)*p;

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '_' || c == '-' ||
			c == '+' || c == ' '))
			return false;
	}

	memcpy (out, base, len);
	out[len] = 0;
	return true;
}

/*
=================
W_AddMapWadFile

Validate the complete directory before retaining the handle. The texture
loader performs another, miptex-specific validation before it reads pixels.
=================
*/
static mapwad_t *W_AddMapWadFile (const char *name, fshandle_t *fh)
{
	wadinfo_t	header;
	mapwad_t	*wad;
	lumpinfo_t	*info;
	int		i, id, numlumps, infotableofs;
	size_t		dirsize;

	if (fh->length < (long)sizeof(header) ||
		FS_fread (&header, 1, sizeof(header), fh) != sizeof(header))
	{
		Con_Warning ("WAD file %s is too short\n", name);
		return NULL;
	}

	id = (unsigned char)header.identification[0] |
		((unsigned char)header.identification[1] << 8) |
		((unsigned char)header.identification[2] << 16) |
		((unsigned char)header.identification[3] << 24);
	if (id != WADID && id != WADID_VALVE)
	{
		Con_Warning ("WAD file %s is not a WAD2 or WAD3 file\n", name);
		return NULL;
	}

	numlumps = LittleLong (header.numlumps);
	infotableofs = LittleLong (header.infotableofs);
	if (numlumps <= 0 || numlumps > MAPWAD_MAX_LUMPS ||
		infotableofs < (int)sizeof(header) || infotableofs > fh->length)
	{
		Con_Warning ("WAD file %s has an invalid directory\n", name);
		return NULL;
	}

	dirsize = (size_t)numlumps * sizeof(lumpinfo_t);
	if (dirsize > (size_t)(fh->length - infotableofs))
	{
		Con_Warning ("WAD file %s directory extends past end of file\n", name);
		return NULL;
	}

	wad = (mapwad_t *)malloc (sizeof(*wad));
	if (!wad)
	{
		Con_Warning ("WAD file %s could not allocate directory\n", name);
		return NULL;
	}
	memset (wad, 0, sizeof(*wad));
	wad->lumps = (lumpinfo_t *)malloc (dirsize);
	if (!wad->lumps)
	{
		Con_Warning ("WAD file %s could not allocate directory\n", name);
		free (wad);
		return NULL;
	}

	if (FS_fseek (fh, infotableofs, SEEK_SET) < 0 ||
		FS_fread (wad->lumps, 1, dirsize, fh) != dirsize)
	{
		Con_Warning ("WAD file %s has an unreadable directory\n", name);
		free (wad->lumps);
		free (wad);
		return NULL;
	}

	for (i = 0, info = wad->lumps; i < numlumps; i++, info++)
	{
		info->filepos = LittleLong (info->filepos);
		info->disksize = LittleLong (info->disksize);
		info->size = LittleLong (info->size);
		W_CleanupName (info->name, info->name);

		if (info->filepos < 0 || info->disksize < 0 || info->size < 0 ||
			info->filepos > fh->length ||
			info->disksize > fh->length - info->filepos)
		{
			Con_Warning ("WAD file %s has an invalid lump directory\n", name);
			free (wad->lumps);
			free (wad);
			return NULL;
		}
	}

	q_strlcpy (wad->name, name, sizeof(wad->name));
	wad->id = id;
	wad->fh = *fh;
	wad->numlumps = numlumps;
	return wad;
}

/*
=================
W_LoadMapWadList

Load only WAD basenames listed by a map's worldspawn. The list is temporary
and is freed as soon as the missing texture pixels have been copied to hunk
memory by gl_model.c.
=================
*/
mapwad_t *W_LoadMapWadList (const char *names)
{
	const char	*name, *end;
	char		basename[MAX_QPATH], filename[MAX_QPATH];
	fshandle_t	fh;
	mapwad_t	*wad, *wads;
	qboolean	opened;
	int		count;

	wads = NULL;
	count = 0;
	for (name = names; name && *name; )
	{
		if (count >= MAPWAD_MAX_FILES)
		{
			Con_Warning ("Map WAD list has more than %d entries; ignoring the rest\n",
				MAPWAD_MAX_FILES);
			break;
		}
		end = strchr (name, ';');
		if (!end)
			end = name + strlen(name);

		if (W_ExtractMapWadBasename (name, (size_t)(end - name),
			basename, sizeof(basename)) &&
			strlen(basename) + sizeof(".wad") <= sizeof(filename))
		{
			q_snprintf (filename, sizeof(filename), "%s.wad", basename);
			opened = W_OpenMapWadFile (filename, &fh);
			if (!opened && strlen(basename) + sizeof("gfx/.wad") <= sizeof(filename))
			{
				q_snprintf (filename, sizeof(filename), "gfx/%s.wad", basename);
				opened = W_OpenMapWadFile (filename, &fh);
			}

			if (opened)
			{
				wad = W_AddMapWadFile (filename, &fh);
				if (wad)
				{
					/* Later names override earlier ones, matching map compiler behavior. */
					wad->next = wads;
					wads = wad;
					count++;
				}
				else
					FS_fclose (&fh);
			}
		}

		if (!*end)
			break;
		name = end + 1;
	}

	return wads;
}

void W_FreeMapWadList (mapwad_t *wads)
{
	mapwad_t	*next;

	while (wads)
	{
		next = wads->next;
		FS_fclose (&wads->fh);
		free (wads->lumps);
		free (wads);
		wads = next;
	}
}

lumpinfo_t *W_GetMapWadLumpInfo (mapwad_t *wads, const char *name,
	mapwad_t **out_wad)
{
	char		clean[16];
	int		i;
	lumpinfo_t	*info;

	if (out_wad)
		*out_wad = NULL;
	W_CleanupName (name, clean);

	for (; wads; wads = wads->next)
	{
		for (i = 0, info = wads->lumps; i < wads->numlumps; i++, info++)
		{
			/* WAD names are fixed 16-byte fields and need not be NUL-terminated. */
			if (!memcmp(clean, info->name, sizeof(clean)))
			{
				if (out_wad)
					*out_wad = wads;
				return info;
			}
		}
	}

	return NULL;
}

/*
=============================================================================

automatic byte swapping

=============================================================================
*/

void SwapPic (qpic_t *pic)
{
	pic->width = LittleLong(pic->width);
	pic->height = LittleLong(pic->height);
}
