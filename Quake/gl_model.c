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
// models.c -- model loading and caching

// models are the only shared resource between a client and server running
// on the same machine.

#include "quakedef.h"
#include "debug_log.h"

static qmodel_t*	loadmodel;
static char	loadname[32];	// for hunk tags

static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer);
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer);
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer);
static qboolean Mod_LoadMD3Model (qmodel_t *mod, const byte *buffer, size_t filesize);
static qboolean Mod_LoadMD5MeshModel (qmodel_t *mod, const byte *buffer, size_t filesize);
static qmodel_t *Mod_LoadVerifiedRereleasePlayerMD5 (void);
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash);
static void Mod_EnhancedModels_f (cvar_t *var);
static void Mod_MigrateEnhancedModels_f (void);

static void Mod_Print (void);

static cvar_t	external_ents = {"external_ents", "1", CVAR_ARCHIVE};
static cvar_t	external_vis = {"external_vis", "1", CVAR_ARCHIVE};
/* Allow map worldspawns to supply texture pixels from sanitized WAD2/WAD3 names. */
static cvar_t	wad_external_textures = {"wad_external_textures", "1", CVAR_NONE};
static cvar_t	mdl_external_textures = {"mdl_external_textures", "1", CVAR_NONE};
static cvar_t	r_allow_replacement_md3models = {"r_allow_replacement_md3models", "1", CVAR_NONE};
static cvar_t	r_allow_replacement_md5models = {"r_allow_replacement_md5models", "1", CVAR_NONE};
cvar_t		r_enhancedmodels = {"r_enhancedmodels", "0", CVAR_ARCHIVE};
static cvar_t r_enhancedmodels_migration_version = {
    "r_enhancedmodels_migration_version", "0", CVAR_ARCHIVE};

#define R_ENHANCEDMODELS_MIGRATION_VERSION 1

/*
 * Alias cache blocks contain a small directory followed by one or more
 * relocatable payloads.  Keeping both variants in the existing final
 * cache_user_t is important: Cache_Free intentionally derives qmodel_t from
 * that final member when releasing model-owned GL textures.
 */
#define MOD_ALIAS_CACHE_MAGIC	0x4d414331U /* "MAC1" */
typedef struct mod_alias_cache_s
{
	unsigned int	magic;
	int		mdl_offset;
	int		md3_offset;
	int		md5_offset;
	qboolean	md3_from_rerelease;
	qboolean	md5_from_rerelease;
} mod_alias_cache_t;

#define MOD_ALIAS_CACHE_DATA_OFFSET \
	((sizeof(mod_alias_cache_t) + sizeof(intptr_t) - 1) & ~(sizeof(intptr_t) - 1))

typedef struct mod_alias_build_s
{
	qboolean	active;
	int		startmark;
	byte		*base;
	int		mdl_offset;
	int		md3_offset;
	int		md5_offset;
	qboolean	md3_from_rerelease;
	qboolean	md5_from_rerelease;
	vec3_t		md3mins;
	vec3_t		md3maxs;
	vec3_t		md5mins;
	vec3_t		md5maxs;
} mod_alias_build_t;

static mod_alias_build_t mod_alias_build;
static qboolean mod_md5_rerelease_only;

static byte	*mod_novis;
static int	mod_novis_capacity;

static byte	*mod_decompressed;
static int	mod_decompressed_capacity;

#define	MAX_MOD_KNOWN	8192 /*spike -- QSS-M needs this for large maps with many inline models */
static qmodel_t	mod_known[MAX_MOD_KNOWN];
static int		mod_numknown;

texture_t	*r_notexture_mip; //johnfitz -- moved here from r_main.c
texture_t	*r_notexture_mip2; //johnfitz -- used for non-lightmapped surfs with a missing texture

/*
 * Classic and replacement MD3 models can have different pose layouts.  Reset
 * animation interpolation when the selection changes so no entity carries an
 * MDL pose index into the MD3 (or vice versa) for a frame.
 */
static void Mod_EnhancedModels_f (cvar_t *var)
{
	int i;

	(void)var;
	if (cl.entities)
		for (i = 0; i < cl.num_entities; i++)
			cl.entities[i].lerpflags |= LERP_RESETANIM;
	cl.viewent.lerpflags |= LERP_RESETANIM;
}

static qboolean Mod_CommandLineSetsCvar (const char *name)
{
	int i;

	for (i = 1; i < com_argc; i++)
	{
		const char *arg = com_argv[i];

		if (!arg || arg[0] != '+')
			continue;
		if (!q_strcasecmp (arg + 1, name))
			return true;
		if ((!q_strcasecmp (arg + 1, "set") ||
			 !q_strcasecmp (arg + 1, "seta")) &&
			i + 1 < com_argc && com_argv[i + 1] &&
			!q_strcasecmp (com_argv[i + 1], name))
			return true;
	}

	return false;
}

/*
 * The MD3/MD5 replacement renderer was introduced with its initial default
 * enabled.  On dense mods such as QBJ3, the fixed-function replacement path
 * can be substantially slower than the native MDL batcher.  Reset that old
 * default once after config.cfg has loaded, but leave explicit launch and
 * postcfg choices intact.
 */
static void Mod_MigrateEnhancedModels_f (void)
{
	if ((int)r_enhancedmodels_migration_version.value >=
		R_ENHANCEDMODELS_MIGRATION_VERSION)
		return;

	if (!Mod_CommandLineSetsCvar ("r_enhancedmodels") &&
		r_enhancedmodels.value != 0)
	{
		Cvar_SetQuick (&r_enhancedmodels, "0");
		Con_Printf ("Enhanced model replacements were disabled after upgrade "
			"to preserve performance.\n");
	}

	Cvar_SetQuick (&r_enhancedmodels_migration_version,
		va("%d", R_ENHANCEDMODELS_MIGRATION_VERSION));
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&gl_subdivide_size);
	Cvar_RegisterVariable (&external_vis);
	Cvar_RegisterVariable (&external_ents);
	Cvar_RegisterVariable (&wad_external_textures);
	Cvar_RegisterVariable (&mdl_external_textures);
	Cvar_RegisterVariable (&r_allow_replacement_md3models);
	Cvar_RegisterVariable (&r_allow_replacement_md5models);
	Cvar_RegisterVariable (&r_enhancedmodels);
	Cvar_RegisterVariable (&r_enhancedmodels_migration_version);
	Cvar_SetCallback (&r_enhancedmodels, Mod_EnhancedModels_f);

	Cmd_AddCommand ("mcache", Mod_Print);
	Cmd_AddCommand ("mod_migrate_enhancedmodels", Mod_MigrateEnhancedModels_f);

	//johnfitz -- create notexture miptex
	r_notexture_mip = (texture_t *) Hunk_AllocName (sizeof(texture_t), "r_notexture_mip");
	strcpy (r_notexture_mip->name, "notexture");
	r_notexture_mip->height = r_notexture_mip->width = 32;

	r_notexture_mip2 = (texture_t *) Hunk_AllocName (sizeof(texture_t), "r_notexture_mip2");
	strcpy (r_notexture_mip2->name, "notexture2");
	r_notexture_mip2->height = r_notexture_mip2->width = 32;
	//johnfitz
}

/*
===============
Mod_BeginAliasBuild

Build MDL plus optional MD3/MD5 payloads on the hunk, then pack them into one
cache allocation. The two one-byte anchors make the byte range copyable
without reaching into zone.c's private hunk header layout.
===============
*/
static void Mod_BeginAliasBuild (void)
{
	memset (&mod_alias_build, 0, sizeof(mod_alias_build));
	mod_alias_build.active = true;
	mod_alias_build.startmark = Hunk_LowMark ();
	mod_alias_build.base = (byte *)Hunk_Alloc (1);
}

static void Mod_RegisterAliasBuild (aliashdr_t *hdr, qboolean md3,
	const vec3_t mins, const vec3_t maxs)
{
	if (!mod_alias_build.active)
		Sys_Error ("Mod_RegisterAliasBuild: no active build");

	if (md3)
	{
		mod_alias_build.md3_offset = (int)((byte *)hdr - mod_alias_build.base);
		VectorCopy (mins, mod_alias_build.md3mins);
		VectorCopy (maxs, mod_alias_build.md3maxs);
	}
	else
		mod_alias_build.mdl_offset = (int)((byte *)hdr - mod_alias_build.base);
}

static void Mod_RegisterMD5AliasBuild (aliashdr_t *hdr,
	const vec3_t mins, const vec3_t maxs)
{
	if (!mod_alias_build.active)
		Sys_Error ("Mod_RegisterMD5AliasBuild: no active build");

	mod_alias_build.md5_offset = (int)((byte *)hdr - mod_alias_build.base);
	VectorCopy (mins, mod_alias_build.md5mins);
	VectorCopy (maxs, mod_alias_build.md5maxs);
}

static void Mod_ExpandAliasBoundsForEnhanced (qmodel_t *mod)
{
	float yawradius = 0.0f, radius = 0.0f;
	vec3_t v;
	int i, j, k;

	if (!mod_alias_build.md3_offset && !mod_alias_build.md5_offset)
		return;

	for (i = 0; i < 3; i++)
	{
		if (mod_alias_build.md3_offset)
		{
			mod->mins[i] = q_min (mod->mins[i], mod_alias_build.md3mins[i]);
			mod->maxs[i] = q_max (mod->maxs[i], mod_alias_build.md3maxs[i]);
		}
		if (mod_alias_build.md5_offset)
		{
			mod->mins[i] = q_min (mod->mins[i], mod_alias_build.md5mins[i]);
			mod->maxs[i] = q_max (mod->maxs[i], mod_alias_build.md5maxs[i]);
		}
	}

	/* Conservatively derive yaw/pitch bounds from the eight expanded corners. */
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
			for (k = 0; k < 2; k++)
			{
				v[0] = i ? mod->maxs[0] : mod->mins[0];
				v[1] = j ? mod->maxs[1] : mod->mins[1];
				v[2] = k ? mod->maxs[2] : mod->mins[2];
				yawradius = q_max (yawradius, v[0] * v[0] + v[1] * v[1]);
				radius = q_max (radius, yawradius + v[2] * v[2]);
			}

	yawradius = sqrtf (yawradius);
	radius = sqrtf (radius);
	mod->ymins[0] = mod->ymins[1] = -yawradius;
	mod->ymaxs[0] = mod->ymaxs[1] = yawradius;
	mod->ymins[2] = mod->mins[2];
	mod->ymaxs[2] = mod->maxs[2];
	mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = -radius;
	mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = radius;
}

static void Mod_FinishAliasBuild (qmodel_t *mod)
{
	mod_alias_cache_t *cache;
	byte *end;
	int rawsize;
	int endmark;

	if (!mod_alias_build.active)
		Sys_Error ("Mod_FinishAliasBuild: no active build");

	end = (byte *)Hunk_Alloc (1);
	endmark = Hunk_LowMark ();
	if (!Hunk_IsContiguous (mod_alias_build.startmark, endmark))
		Sys_Error ("Mod_LoadModel: %s spans multiple hunk segments (try a larger -heapsize)", mod->name);

	rawsize = (int)(end - mod_alias_build.base);
	if (rawsize <= 0 || rawsize > INT_MAX - (int)MOD_ALIAS_CACHE_DATA_OFFSET)
		Sys_Error ("Mod_LoadModel: invalid cache size for %s", mod->name);

	cache = (mod_alias_cache_t *)Cache_Alloc (&mod->cache,
		MOD_ALIAS_CACHE_DATA_OFFSET + rawsize, loadname);
	cache->magic = MOD_ALIAS_CACHE_MAGIC;
	cache->mdl_offset = mod_alias_build.mdl_offset ?
		(int)MOD_ALIAS_CACHE_DATA_OFFSET + mod_alias_build.mdl_offset : 0;
	cache->md3_offset = mod_alias_build.md3_offset ?
		(int)MOD_ALIAS_CACHE_DATA_OFFSET + mod_alias_build.md3_offset : 0;
	cache->md5_offset = mod_alias_build.md5_offset ?
		(int)MOD_ALIAS_CACHE_DATA_OFFSET + mod_alias_build.md5_offset : 0;
	cache->md3_from_rerelease = mod_alias_build.md3_from_rerelease;
	cache->md5_from_rerelease = mod_alias_build.md5_from_rerelease;
	memcpy ((byte *)cache + MOD_ALIAS_CACHE_DATA_OFFSET, mod_alias_build.base, rawsize);

	Hunk_FreeToLowMark (mod_alias_build.startmark);
	memset (&mod_alias_build, 0, sizeof(mod_alias_build));
}

static mod_alias_cache_t *Mod_GetAliasCache (qmodel_t *mod)
{
	mod_alias_cache_t *cache;

	cache = (mod_alias_cache_t *)Cache_Check (&mod->cache);
	if (!cache)
	{
		Mod_LoadModel (mod, true);
		cache = (mod_alias_cache_t *)Cache_Check (&mod->cache);
	}

	if (!cache || cache->magic != MOD_ALIAS_CACHE_MAGIC)
		Sys_Error ("Mod_Extradata: invalid alias cache for %s", mod->name);

	return cache;
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (qmodel_t *mod)
{
	mod_alias_cache_t *cache = Mod_GetAliasCache (mod);

	if (cache->mdl_offset)
		return (byte *)cache + cache->mdl_offset;
	if (cache->md3_offset)
		return (byte *)cache + cache->md3_offset;
	if (cache->md5_offset)
		return (byte *)cache + cache->md5_offset;

	Sys_Error ("Mod_Extradata: no alias data for %s", mod->name);
	return NULL;
}

aliashdr_t *Mod_GetMD3Extradata (qmodel_t *mod)
{
	mod_alias_cache_t *cache = Mod_GetAliasCache (mod);

	if (!cache->md3_offset)
		return NULL;
	return (aliashdr_t *)((byte *)cache + cache->md3_offset);
}

aliashdr_t *Mod_GetMD5Extradata (qmodel_t *mod)
{
	mod_alias_cache_t *cache = Mod_GetAliasCache (mod);

	if (!cache->md5_offset)
		return NULL;
	return (aliashdr_t *)((byte *)cache + cache->md5_offset);
}

static const char *const md5_vrik_joint_names[MD5_VRIK_JOINT_COUNT] =
{
	"Hip", "Spine1", "Spine2", "Neck", "Head",
	"Shoulder_L", "UpperArm_L", "LowerArm_L", "Hand_L",
	"Shoulder_R", "UpperArm_R", "LowerArm_R", "Hand_R",
	"UpperLeg_L", "LowerLeg_L", "Foot_L",
	"UpperLeg_R", "LowerLeg_R", "Foot_R", "Gun", "Axe",
	"small_flame", "big_flame"
};

static qboolean Mod_MD5LiveSurfaceValid (const aliashdr_t *surface,
	int numbones)
{
	const md5livevertex_t *vertices;
	const md5liveweight_t *weights;
	int vert;

	if (!surface || surface->poseverttype != ALIAS_POSE_MD5 ||
		surface->numverts < 1 || surface->numindexes < 3 ||
		surface->numindexes % 3 || surface->md5_numliveweights < 1 ||
		surface->md5_livevertices <= 0 || surface->md5_liveweights <= 0 ||
		surface->indexes <= 0)
		return false;

	vertices = (const md5livevertex_t *)((const byte *)surface +
		surface->md5_livevertices);
	weights = (const md5liveweight_t *)((const byte *)surface +
		surface->md5_liveweights);
	for (vert = 0; vert < surface->numverts; vert++)
	{
		unsigned int first = vertices[vert].firstweight;
		unsigned int count = vertices[vert].numweights;
		unsigned int weight;

		if (!count || first > (unsigned int)surface->md5_numliveweights ||
			count > (unsigned int)surface->md5_numliveweights - first)
			return false;
		for (weight = 0; weight < count; weight++)
			if (weights[first + weight].joint < 0 ||
				weights[first + weight].joint >= numbones)
				return false;
	}

	return true;
}

/*
 * Return retained MD5 inputs without selecting it as the normal renderer's
 * replacement.  This intentionally bypasses r_enhancedmodels: VRIK decides
 * per remote player whether it needs this data.
 */
qboolean Mod_GetMD5LiveData (qmodel_t *mod, md5liveinfo_t *out)
{
	mod_alias_cache_t *cache;
	const aliashdr_t *surface;
	const md5livejoint_t *joints;
	int joint, required, surfacecount;

	if (!out)
		return false;
	memset (out, 0, sizeof(*out));
	for (joint = 0; joint < MD5_VRIK_JOINT_COUNT; joint++)
		out->jointindex[joint] = -1;

	if (!mod || mod->type != mod_alias)
		return false;

	cache = Mod_GetAliasCache (mod);
	if (!cache->md5_offset)
		return false;
	surface = (const aliashdr_t *)((byte *)cache + cache->md5_offset);
	if (surface->poseverttype != ALIAS_POSE_MD5 ||
		surface->md5_numbones < 1 || surface->md5_numbones > MAX_MD5_JOINTS ||
		surface->numposes < 1 || surface->md5_livejoints <= 0 ||
		surface->md5_boneposes <= 0)
		return false;

	joints = (const md5livejoint_t *)((const byte *)surface +
		surface->md5_livejoints);
	for (joint = 0; joint < surface->md5_numbones; joint++)
		if (joints[joint].parent < -1 || joints[joint].parent >= joint)
			return false;

	out->firstsurface = surface;
	out->joints = joints;
	out->boneposes = (const float *)((const byte *)surface +
		surface->md5_boneposes);
	out->numbones = surface->md5_numbones;
	out->numposes = surface->numposes;
	out->from_rerelease = cache->md5_from_rerelease;
	for (joint = 0; joint < surface->md5_numbones; joint++)
	{
		int semantic;
		for (semantic = 0; semantic < MD5_VRIK_JOINT_COUNT; semantic++)
			if (!q_strcasecmp (joints[joint].name,
				md5_vrik_joint_names[semantic]))
			{
				out->jointindex[semantic] = joint;
				break;
			}
	}

	/* The upper body is required; legs and equipment are optional overrides. */
	out->compatible = true;
	for (required = MD5_VRIK_HIP; required <= MD5_VRIK_HAND_R; required++)
		if (out->jointindex[required] < 0)
			out->compatible = false;
	for (surface = out->firstsurface, surfacecount = 0;
		surface && surfacecount < MAX_MD5_SURFACES;
		surface = surface->nextsurface ?
		(const aliashdr_t *)((const byte *)surface + surface->nextsurface) : NULL,
		surfacecount++)
		if (!Mod_MD5LiveSurfaceValid (surface, out->numbones))
		{
			out->compatible = false;
			break;
		}
	if (surfacecount == MAX_MD5_SURFACES && surface)
		out->compatible = false;

	return true;
}

qboolean Mod_IsVRIKCompatible (qmodel_t *mod)
{
	md5liveinfo_t live;

	/* The current CPU skinning path deforms one combined surface.  Treat a
	 * multi-surface rig as unsupported instead of advertising VRIK and then
	 * silently drawing its ordinary animation. */
	return Mod_GetMD5LiveData (mod, &live) && live.compatible &&
		!live.firstsurface->nextsurface;
}

qboolean Mod_GetMD5LiveSurface (const md5liveinfo_t *info, int surfaceindex,
	md5livesurface_t *out)
{
	const aliashdr_t *surface;
	int index;

	if (!out)
		return false;
	memset (out, 0, sizeof(*out));
	if (!info || !info->firstsurface || surfaceindex < 0 ||
		surfaceindex >= MAX_MD5_SURFACES)
		return false;

	surface = info->firstsurface;
	for (index = 0; index < surfaceindex && surface; index++)
		surface = surface->nextsurface ?
			(const aliashdr_t *)((const byte *)surface + surface->nextsurface) : NULL;
	if (!surface || !Mod_MD5LiveSurfaceValid (surface, info->numbones))
		return false;

	out->header = surface;
	out->vertices = (const md5livevertex_t *)((const byte *)surface +
		surface->md5_livevertices);
	out->weights = (const md5liveweight_t *)((const byte *)surface +
		surface->md5_liveweights);
	out->indexes = (const unsigned short *)((const byte *)surface + surface->indexes);
	out->numverts = surface->numverts;
	out->numweights = surface->md5_numliveweights;
	out->numindexes = surface->numindexes;
	return true;
}

qmodel_t *Mod_GetRereleasePlayerMD5Model (void)
{
	return Mod_LoadVerifiedRereleasePlayerMD5 ();
}

qboolean Mod_GetRereleasePlayerMD5LiveData (md5liveinfo_t *out)
{
	qmodel_t *mod;

	if (!out)
		return false;
	mod = Mod_GetRereleasePlayerMD5Model ();
	if (!mod || !Mod_GetMD5LiveData (mod, out) || !out->from_rerelease)
	{
		memset (out, 0, sizeof(*out));
		return false;
	}
	return true;
}

qboolean Mod_UseMD3Model (qmodel_t *mod, int skinnum)
{
	mod_alias_cache_t *cache;
	aliashdr_t *surface;

	if (!mod || mod->type != mod_alias)
		return false;

	cache = Mod_GetAliasCache (mod);
	if (!cache->md3_offset)
		return false;

	/* A native .md3 has no classic variant to fall back to. */
	if (cache->mdl_offset && !r_enhancedmodels.value)
		return false;

	surface = (aliashdr_t *)((byte *)cache + cache->md3_offset);
	while (surface)
	{
		if (skinnum < 0 || skinnum >= surface->numskins ||
			!surface->gltextures[skinnum][0])
			return cache->mdl_offset ? false : true;
		surface = surface->nextsurface ?
			(aliashdr_t *)((byte *)surface + surface->nextsurface) : NULL;
	}

	return true;
}

qboolean Mod_UseMD5Model (qmodel_t *mod, int skinnum)
{
	mod_alias_cache_t *cache;
	aliashdr_t *surface;

	if (!mod || mod->type != mod_alias)
		return false;

	cache = Mod_GetAliasCache (mod);
	if (!cache->md5_offset)
		return false;

	/* A native .md5mesh has no classic variant to fall back to. */
	if (cache->mdl_offset && !r_enhancedmodels.value)
		return false;

	surface = (aliashdr_t *)((byte *)cache + cache->md5_offset);
	while (surface)
	{
		if (skinnum < 0 || skinnum >= surface->numskins ||
			!surface->gltextures[skinnum][0])
			return cache->mdl_offset ? false : true;
		surface = surface->nextsurface ?
			(aliashdr_t *)((byte *)surface + surface->nextsurface) : NULL;
	}

	return true;
}

/*
=================
Mod_UseMD3ModelForFrame / Mod_UseMD5ModelForFrame

Replacement models must not reinterpret a valid classic-only frame. This is
important for map decorations such as crucified zombies: their frame numbers
are part of the QuakeC contract, not merely animation detail. Native enhanced
models retain the regular frame-0 fallback because they have no MDL variant.
=================
*/
qboolean Mod_UseMD3ModelForFrame (qmodel_t *mod, int skinnum, int frame)
{
	mod_alias_cache_t *cache;
	aliashdr_t *surface;

	if (!Mod_UseMD3Model (mod, skinnum))
		return false;

	cache = Mod_GetAliasCache (mod);
	if (!cache->mdl_offset)
		return true;

	surface = (aliashdr_t *)((byte *)cache + cache->md3_offset);
	return frame >= 0 && frame < surface->numframes;
}

qboolean Mod_UseMD5ModelForFrame (qmodel_t *mod, int skinnum, int frame)
{
	mod_alias_cache_t *cache;
	aliashdr_t *surface;

	if (!Mod_UseMD5Model (mod, skinnum))
		return false;

	cache = Mod_GetAliasCache (mod);
	if (!cache->mdl_offset)
		return true;

	surface = (aliashdr_t *)((byte *)cache + cache->md5_offset);
	return frame >= 0 && frame < surface->numframes;
}

/*
=================
Mod_UseEnhancedReplacementForFrame

True only when the enhanced-model option is actively replacing a classic MDL
for this frame. Native MD3/MD5-only models are not enhanced-option replacements.
=================
*/
qboolean Mod_UseEnhancedReplacementForFrame (qmodel_t *mod, int skinnum,
							 int frame)
{
	mod_alias_cache_t *cache;

	if (!mod || mod->type != mod_alias || !r_enhancedmodels.value)
		return false;

	cache = Mod_GetAliasCache (mod);
	if (!cache->mdl_offset)
		return false;

	return Mod_UseMD3ModelForFrame (mod, skinnum, frame) ||
		Mod_UseMD5ModelForFrame (mod, skinnum, frame);
}

qboolean Mod_UseRereleaseReplacementForFrame (qmodel_t *mod, int skinnum,
							 int frame)
{
	mod_alias_cache_t *cache;

	if (!Mod_UseEnhancedReplacementForFrame (mod, skinnum, frame))
		return false;

	cache = Mod_GetAliasCache (mod);
	if (Mod_UseMD3ModelForFrame (mod, skinnum, frame))
		return cache->md3_from_rerelease;
	if (Mod_UseMD5ModelForFrame (mod, skinnum, frame))
		return cache->md5_from_rerelease;

	return false;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (vec3_t p, qmodel_t *model)
{
	mnode_t		*node;
	float		d;
	mplane_t	*plane;

	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL;	// never reached
}


/*
===================
Mod_DecompressVis
===================
*/
static byte *Mod_DecompressVis (byte *in, qmodel_t *model)
{
	int		c;
	byte	*out;
	byte	*outend;
	int		row;

	row = (model->numleafs+7)>>3;
	if (mod_decompressed == NULL || row > mod_decompressed_capacity)
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *) realloc (mod_decompressed, mod_decompressed_capacity);
		if (!mod_decompressed)
			Sys_Error ("Mod_DecompressVis: realloc() failed on %d bytes", mod_decompressed_capacity);
	}
	out = mod_decompressed;
	outend = mod_decompressed + row;

	if (!in)
	{	// no vis info, so make all visible
		while (row)
		{
			*out++ = 0xff;
			row--;
		}
		return mod_decompressed;
	}

	do
	{
		if (*in)
		{
			*out++ = *in++;
			continue;
		}

		c = in[1];
		in += 2;
		if (c > row - (out - mod_decompressed))
			c = row - (out - mod_decompressed);
		while (c)
		{
			if (out == outend)
			{
				if(!model->viswarn) {
					model->viswarn = true;
					DebugLog("Mod_DecompressVis: output overrun on model \"%s\"\n", model->name);
				}
				return mod_decompressed;
			}
			*out++ = 0;
			c--;
		}
	} while (out - mod_decompressed < row);

	return mod_decompressed;
}

byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model)
{
	if (leaf == model->leafs)
		return Mod_NoVisPVS (model);
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

byte *Mod_NoVisPVS (qmodel_t *model)
{
	int pvsbytes;
 
	pvsbytes = (model->numleafs+7)>>3;
	if (mod_novis == NULL || pvsbytes > mod_novis_capacity)
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *) realloc (mod_novis, mod_novis_capacity);
		if (!mod_novis)
			Sys_Error ("Mod_NoVisPVS: realloc() failed on %d bytes", mod_novis_capacity);
		
		memset(mod_novis, 0xff, mod_novis_capacity);
	}
	return mod_novis;
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		i;
	qmodel_t	*mod;

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (mod->type != mod_alias)
		{
			mod->needload = true;
			TexMgr_FreeTexturesForOwner (mod); //johnfitz
#ifdef PSET_SCRIPT
			PScript_ClearSurfaceParticles (mod);
#endif
		}
	}
}

void Mod_ResetAll (void)
{
	int		i;
	qmodel_t	*mod;

	//ericw -- free alias model VBOs
	GLMesh_DeleteVertexBuffers ();

	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!mod->needload) //otherwise Mod_ClearAll() did it already
		{
			TexMgr_FreeTexturesForOwner (mod);
#ifdef PSET_SCRIPT
			PScript_ClearSurfaceParticles (mod);
#endif
		}
		memset(mod, 0, sizeof(qmodel_t));
	}
	mod_numknown = 0;
}

void Mod_ForEachModel (void (*callback)(qmodel_t *mod))
{
	int		i;
	qmodel_t	*mod;

	for (i=0, mod=mod_known; i<mod_numknown; i++, mod++)
		callback (mod);
}

/*
==================
Mod_FindName

==================
*/
static qmodel_t *Mod_FindName (const char *name)
{
	int		i;
	qmodel_t	*mod;

	if (!name[0])
		Sys_Error ("Mod_FindName: NULL name"); //johnfitz -- was "Mod_ForName"

//
// search the currently loaded models
//
	for (i=0 , mod=mod_known ; i<mod_numknown ; i++, mod++)
	{
		if (!strcmp (mod->name, name) )
			break;
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
			Sys_Error ("mod_numknown == MAX_MOD_KNOWN");
		q_strlcpy (mod->name, name, MAX_QPATH);
		mod->needload = true;
		mod_numknown++;
	}

	return mod;
}

/*
==================
Mod_TouchModel

==================
*/
void Mod_TouchModel (const char *name)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
			Cache_Check (&mod->cache);
	}
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash)
{
	byte	*buf;
	byte	stackbuf[1024];		// avoid dirtying the cache heap
	int	mod_type;
	int	model_filesize;

	if (!mod->needload)
	{
		if (mod->type == mod_alias)
		{
			if (Cache_Check (&mod->cache))
				return mod;
		}
		else
			return mod;		// not cached at all
	}

//
// because the world is so huge, load it one piece at a time
//
	if (!crash)
	{

	}

//
// load the file
//
	buf = COM_LoadStackFile (mod->name, stackbuf, sizeof(stackbuf), & mod->path_id);
	if (!buf)
	{
		if (crash)
			Host_Error ("Mod_LoadModel: %s not found", mod->name); //johnfitz -- was "Mod_NumForName"
		return NULL;
	}
	model_filesize = com_filesize;

//
// allocate a new model
//
	COM_FileBase (mod->name, loadname, sizeof(loadname));

	loadmodel = mod;

//
// fill it in
//

	// call the appropriate loader
	mod->needload = false;

	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	switch (mod_type)
	{
	case IDPOLYHEADER:
	{
		char md3_name[MAX_QPATH], md5_name[MAX_QPATH];
		byte *md3buf = NULL;
		byte *md5buf = NULL;
		unsigned int md3_path_id = 0;
		unsigned int md5_path_id = 0;
		qboolean md3_from_rerelease = false;
		qboolean md5_from_rerelease = false;

		Mod_BeginAliasBuild ();

		/*
		 * Keep a same-name MD3 alongside the source MDL when it comes from
		 * the same or a higher-priority game directory.  It is optional: bad
		 * files and missing/invalid skins simply leave the classic model active.
		 */
		if (r_allow_replacement_md3models.value &&
			!q_strcasecmp (COM_FileGetExtension (mod->name), "mdl"))
		{
			COM_StripExtension (mod->name, md3_name, sizeof(md3_name));
			COM_AddExtension (md3_name, ".md3", sizeof(md3_name));
			if (!COM_FileExistsEx (md3_name, &md3_path_id, &md3_from_rerelease) ||
				md3_path_id < mod->path_id)
				md3_path_id = 0;
		}

		if (r_allow_replacement_md5models.value &&
			!q_strcasecmp (COM_FileGetExtension (mod->name), "mdl"))
		{
			COM_StripExtension (mod->name, md5_name, sizeof(md5_name));
			COM_AddExtension (md5_name, ".md5mesh", sizeof(md5_name));
			if (!COM_FileExistsEx (md5_name, &md5_path_id, &md5_from_rerelease) ||
				md5_path_id < mod->path_id)
				md5_path_id = 0;
		}

		/* Match vkQuake's deterministic policy: highest path wins, MD3 wins ties. */
		if (md3_path_id && md5_path_id)
		{
			if (md5_path_id > md3_path_id)
				md3_path_id = 0;
			else
				md5_path_id = 0;
		}

		/* The old MD3 load block must only run after the MD5 priority decision. */
		if (md3_path_id)
		{
			int md3_filesize;
			unsigned int original_path_id = mod->path_id;

			md3buf = COM_LoadMallocFile (md3_name, &md3_path_id);
			md3_filesize = com_filesize;
			if (md3buf)
			{
				mod_alias_build.md3_from_rerelease = md3_from_rerelease;
				/* Load companion skins against the replacement's search path. */
				mod->path_id = md3_path_id;
				Mod_LoadMD3Model (mod, md3buf, (size_t)md3_filesize);
				mod->path_id = original_path_id;
				free (md3buf);
			}
		}
		else if (md5_path_id)
		{
			int md5_filesize;
			unsigned int original_path_id = mod->path_id;

			md5buf = COM_LoadMallocFile (md5_name, &md5_path_id);
			md5_filesize = com_filesize;
			if (md5buf)
			{
				mod_alias_build.md5_from_rerelease = md5_from_rerelease;
				mod->path_id = md5_path_id;
				Mod_LoadMD5MeshModel (mod, md5buf, (size_t)md5_filesize);
				mod->path_id = original_path_id;
				free (md5buf);
			}
		}

		Mod_LoadAliasModel (mod, buf);
		Mod_ExpandAliasBoundsForEnhanced (mod);
		Mod_FinishAliasBuild (mod);
		break;
	}

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	case (('I' << 0) | ('D' << 8) | ('P' << 16) | ('3' << 24)):
		Mod_BeginAliasBuild ();
		if (!Mod_LoadMD3Model (mod, buf, (size_t)model_filesize))
		{
			Hunk_FreeToLowMark (mod_alias_build.startmark);
			memset (&mod_alias_build, 0, sizeof(mod_alias_build));
			/* Keep a non-crashing failed load retryable. */
			mod->needload = true;
			if (crash)
				Host_Error ("Mod_LoadModel: invalid MD3 %s", mod->name);
			return NULL;
		}
		Mod_FinishAliasBuild (mod);
		break;

	case (('M' << 0) | ('D' << 8) | ('5' << 16) | ('V' << 24)):
		Mod_BeginAliasBuild ();
		if (!Mod_LoadMD5MeshModel (mod, buf, (size_t)model_filesize))
		{
			Hunk_FreeToLowMark (mod_alias_build.startmark);
			memset (&mod_alias_build, 0, sizeof(mod_alias_build));
			mod->needload = true;
			if (crash)
				Host_Error ("Mod_LoadModel: invalid MD5 %s", mod->name);
			return NULL;
		}
		Mod_FinishAliasBuild (mod);
		break;

	default:
		Mod_LoadBrushModel (mod, buf);
		break;
	}

#ifdef PSET_SCRIPT
	PScript_UpdateModelEffects (mod);
#endif

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
qmodel_t *Mod_ForName (const char *name, qboolean crash)
{
	qmodel_t	*mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}


/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

static byte	*mod_base;

#define MAPWAD_MAX_TEXTURE_DIMENSION 4096

/*
=================
Mod_CheckFullbrights -- johnfitz
=================
*/
static qboolean Mod_CheckFullbrights (byte *pixels, int count)
{
	int i;
	for (i = 0; i < count; i++)
	{
		if (*pixels++ > 223)
			return true;
	}
	return false;
}

/*
=================
Mod_LoadMapWadFiles

Only map BSPs have a worldspawn WAD key. The helper in wad.c accepts only
sanitized basenames and opens them through the current game filesystem; it
never changes search paths or has any effect on server simulation/networking.
=================
*/
static mapwad_t *Mod_LoadMapWadFiles (qmodel_t *mod)
{
	char		key[128], value[4096];
	const char	*data;

	if (isDedicated || !wad_external_textures.value || !mod->entities ||
		q_strncasecmp (mod->name, "maps/", 5))
		return NULL;

	data = COM_Parse (mod->entities);
	if (!data || com_token[0] != '{')
		return NULL;

	while (1)
	{
		data = COM_Parse (data);
		if (!data || com_token[0] == '}')
			break;

		if (com_token[0] == '_')
			q_strlcpy (key, com_token + 1, sizeof(key));
		else
			q_strlcpy (key, com_token, sizeof(key));
		while (key[0] && key[strlen(key) - 1] == ' ')
			key[strlen(key) - 1] = 0;

		data = COM_ParseEx (data, CPE_ALLOWTRUNC);
		if (!data)
			break;
		q_strlcpy (value, com_token, sizeof(value));

		if (!q_strcasecmp (key, "wad"))
			return W_LoadMapWadList (value);
	}

	return NULL;
}

/*
=================
Mod_LoadMapWadTexture

Copy a strictly validated missing miptex into the map hunk. WAD3's private
palette is expanded to RGBA here so every texture retains its own colours
instead of being lossy-remapped through Quake's global palette.
=================
*/
static texture_t *Mod_LoadMapWadTexture (mapwad_t *wads, const char *name,
	qboolean *is_rgba, qboolean *has_alpha)
{
	mapwad_t	*wad;
	lumpinfo_t	*info;
	miptex_t	mt;
	texture_t	*tx;
	byte		*data, *dst;
	byte		palette[256 * 3];
	char		cleanname[16];
	char		sourcename[17];
	unsigned short	colors;
	int		i, width, height, mipofs[MIPLEVELS], mipsizes[MIPLEVELS];
	int		mipwidth, mipheight, paletteofs, pixels, datasize;
	qboolean	wad3palette;

	*is_rgba = false;
	*has_alpha = false;

	/* texture_t stores a 15-character C string, so reject unsafe source names. */
	memcpy (sourcename, name, sizeof(cleanname));
	sourcename[sizeof(cleanname)] = 0;
	if (!memchr (name, 0, sizeof(cleanname)))
	{
		Con_Warning ("External WAD texture %.16s has an unsupported 16-byte name\n",
			name);
		return NULL;
	}

	info = W_GetMapWadLumpInfo (wads, sourcename, &wad);
	if (!info)
		return NULL;

	if (info->compression != CMP_NONE || info->size != info->disksize ||
		(info->type != TYP_MIPTEX &&
			(wad->id != WADID_VALVE || info->type != TYP_MIPTEX_PALETTE)) ||
		info->size < (int)sizeof(mt))
	{
		Con_Warning ("External WAD texture %s is not an uncompressed miptex\n", name);
		return NULL;
	}

	if (FS_fseek (&wad->fh, info->filepos, SEEK_SET) < 0 ||
		FS_fread (&mt, 1, sizeof(mt), &wad->fh) != sizeof(mt))
	{
		Con_Warning ("External WAD texture %s could not be read\n", name);
		return NULL;
	}

	W_CleanupName (mt.name, cleanname);
	/* WAD names are fixed 16-byte fields and may fill the whole field. */
	if (memcmp(cleanname, info->name, sizeof(cleanname)))
	{
		Con_Warning ("External WAD texture %s has a mismatched name\n", name);
		return NULL;
	}

	width = LittleLong ((int)mt.width);
	height = LittleLong ((int)mt.height);
	if (width < 16 || height < 16 || width > MAPWAD_MAX_TEXTURE_DIMENSION ||
		height > MAPWAD_MAX_TEXTURE_DIMENSION || (width & 15) || (height & 15))
	{
		Con_Warning ("External WAD texture %s has invalid dimensions\n", name);
		return NULL;
	}

	for (i = 0; i < MIPLEVELS; i++)
	{
		mipwidth = width >> i;
		mipheight = height >> i;
		mipsizes[i] = mipwidth * mipheight;
		mipofs[i] = LittleLong ((int)mt.offsets[i]);
		if (mipofs[i] < (int)sizeof(mt) || mipofs[i] > info->size ||
			mipsizes[i] > info->size - mipofs[i] ||
			(i && mipofs[i] < mipofs[i - 1] + mipsizes[i - 1]))
		{
			Con_Warning ("External WAD texture %s has invalid mip data\n", name);
			return NULL;
		}
	}

	pixels = mipsizes[0];
	wad3palette = wad->id == WADID_VALVE;
	if (wad3palette)
	{
		paletteofs = mipofs[MIPLEVELS - 1] + mipsizes[MIPLEVELS - 1];
		if (paletteofs > info->size - (int)sizeof(colors) ||
			FS_fseek (&wad->fh, info->filepos + paletteofs, SEEK_SET) < 0 ||
			FS_fread (&colors, 1, sizeof(colors), &wad->fh) != sizeof(colors))
		{
			Con_Warning ("External WAD3 texture %s has no valid palette\n", name);
			return NULL;
		}
		colors = LittleShort (colors);
		if (!colors || colors > 256 ||
			(int)colors * 3 > info->size - paletteofs - (int)sizeof(colors) ||
			FS_fread (palette, 1, (size_t)colors * 3, &wad->fh) != (size_t)colors * 3)
		{
			Con_Warning ("External WAD3 texture %s has an invalid palette\n", name);
			return NULL;
		}

	}

	data = (byte *)malloc (pixels);
	if (!data)
	{
		Con_Warning ("External WAD texture %s could not allocate pixels\n", name);
		return NULL;
	}
	if (FS_fseek (&wad->fh, info->filepos + mipofs[0], SEEK_SET) < 0 ||
		FS_fread (data, 1, pixels, &wad->fh) != (size_t)pixels)
	{
		Con_Warning ("External WAD texture %s could not read pixels\n", name);
		free (data);
		return NULL;
	}

	datasize = pixels;
	if (wad3palette)
	{
		for (i = 0; i < pixels; i++)
			if (data[i] >= colors)
			{
				Con_Warning ("External WAD3 texture %s has invalid palette indices\n", name);
				free (data);
				return NULL;
			}
			else if (sourcename[0] == '{' && data[i] == 255)
				*has_alpha = true;

		if (pixels > INT_MAX / 4)
		{
			Con_Warning ("External WAD3 texture %s is too large\n", name);
			free (data);
			return NULL;
		}
		datasize = pixels * 4;
	}

	tx = (texture_t *) Hunk_AllocName (sizeof(*tx) + datasize, loadname);
	/* Keep the BSP spelling so loose replacement lookup keeps its precedence. */
	q_strlcpy (tx->name, sourcename, sizeof(tx->name));
	tx->width = width;
	tx->height = height;
	tx->update_warp = false;
	tx->warpimage = NULL;
	tx->fullbright = NULL;
	tx->shift = 0;
	dst = (byte *)(tx + 1);
	if (wad3palette)
	{
		for (i = 0; i < pixels; i++)
		{
			int index = data[i];
			dst[i * 4 + 0] = palette[index * 3 + 0];
			dst[i * 4 + 1] = palette[index * 3 + 1];
			dst[i * 4 + 2] = palette[index * 3 + 2];
			/* Index 255 is transparent only for GoldSrc masked textures. */
			dst[i * 4 + 3] = sourcename[0] == '{' && index == 255 ? 0 : 255;
		}
		*is_rgba = true;
	}
	else
	{
		memcpy (dst, data, pixels);
	}
	free (data);

	return tx;
}

/*
=================
Mod_CheckAnimTextureArrayQ64

Quake64 bsp
Check if we have any missing textures in the array
=================
*/
static qboolean Mod_CheckAnimTextureArrayQ64(texture_t *anims[], int numTex)
{
	int i;

	for (i = 0; i < numTex; i++)
	{
		if (!anims[i])
			return false;
	}
	return true;
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures (lump_t *l)
{
	int		i, j, pixels, num, maxanim, altmax;
	miptex_t	*mt;
	texture_t	*tx, *tx2;
	texture_t	*anims[10];
	texture_t	*altanims[10];
	dmiptexlump_t	*m;
//johnfitz -- more variables
	char		texturename[64];
	int			nummiptex;
	src_offset_t		offset;
	int			mark, fwidth, fheight;
	char		filename[MAX_OSPATH], mapname[MAX_OSPATH];
	byte		*data, *dummy;
	mapwad_t		*wads;
	const char		*sourcefile;
	qboolean		wadtexture, wad3texture, wad3alpha;
//johnfitz
	unsigned int	flags;

	//johnfitz -- don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		Con_Printf ("Mod_LoadTextures: no textures in bsp file\n");
		nummiptex = 0;
		m = NULL; // avoid bogus compiler warning
	}
	else
	{
		m = (dmiptexlump_t *)(mod_base + l->fileofs);
		m->nummiptex = LittleLong (m->nummiptex);
		nummiptex = m->nummiptex;
	}
	//johnfitz

	loadmodel->numtextures = nummiptex + 2; //johnfitz -- need 2 dummy texture chains for missing textures
	loadmodel->textures = (texture_t **) Hunk_AllocName (loadmodel->numtextures * sizeof(*loadmodel->textures) , loadname);
	wads = Mod_LoadMapWadFiles (loadmodel);

	for (i=0 ; i<nummiptex ; i++)
	{
		wadtexture = false;
		wad3texture = false;
		wad3alpha = false;
		sourcefile = loadmodel->name;
		m->dataofs[i] = LittleLong(m->dataofs[i]);
		if (m->dataofs[i] == -1)
			continue;
		mt = (miptex_t *)((byte *)m + m->dataofs[i]);
		mt->width = LittleLong (mt->width);
		mt->height = LittleLong (mt->height);
		for (j=0 ; j<MIPLEVELS ; j++)
			mt->offsets[j] = LittleLong (mt->offsets[j]);

		if (mt->width == 0 || mt->height == 0)
		{
			Con_Warning ("Zero sized texture %s in %s!\n", mt->name, loadmodel->name);
			continue;
		}

		if ( (mt->width & 15) || (mt->height & 15) )
		{
			if (loadmodel->bspversion != BSPVERSION_QUAKE64)
				Con_Warning ("Texture %s (%d x %d) is not 16 aligned\n", mt->name, mt->width, mt->height);
		}

		/* A zero first mip offset means the BSP expects an external WAD. */
		if (mt->offsets[0] == 0 && wads)
		{
			tx = Mod_LoadMapWadTexture (wads, mt->name, &wad3texture, &wad3alpha);
			if (tx)
			{
				loadmodel->textures[i] = tx;
				pixels = tx->width * tx->height;
				offset = (src_offset_t)(tx + 1);
				sourcefile = "";
				wadtexture = true;
			}
		}

		if (!wadtexture)
		{
			pixels = mt->width*mt->height; // only copy the first mip, the rest are auto-generated
			tx = (texture_t *) Hunk_AllocName (sizeof(texture_t) +pixels, loadname );
			loadmodel->textures[i] = tx;

			memcpy (tx->name, mt->name, sizeof(tx->name));
			tx->width = mt->width;
			tx->height = mt->height;
			// the pixels immediately follow the structures

			// ericw -- check for pixels extending past the end of the lump.
			// appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
			// kellbase1.bsp (quoth), and can lead to a segfault if we read past
			// the end of the .bsp file buffer
			if (((byte*)(mt+1) + pixels) > (mod_base + l->fileofs + l->filelen))
			{
				Con_DPrintf("Texture %s extends past end of lump\n", mt->name);
				pixels = q_max(0L, (long)((mod_base + l->fileofs + l->filelen) - (byte*)(mt+1)));
			}

			tx->update_warp = false; //johnfitz
			tx->warpimage = NULL; //johnfitz
			tx->fullbright = NULL; //johnfitz
			tx->shift = 0;	// Q64 only
			offset = (src_offset_t)(mt+1) - (src_offset_t)mod_base;

			if (loadmodel->bspversion != BSPVERSION_QUAKE64)
			{
				memcpy ( tx+1, mt+1, pixels);
			}
			else
			{ // Q64 bsp
				miptex64_t *mt64 = (miptex64_t *)mt;
				tx->shift = LittleLong (mt64->shift);
				memcpy ( tx+1, mt64+1, pixels);
			}
		}

		//johnfitz -- lots of changes
		if (!isDedicated) //no texture uploading for dedicated server
		{
			if (!q_strncasecmp(tx->name,"sky",3)) //sky texture //also note -- was Q_strncmp, changed to match qbsp
			{
				if (wad3texture)
					Sky_LoadTextureRGBA (loadmodel, tx);
				else if (loadmodel->bspversion == BSPVERSION_QUAKE64)
					Sky_LoadTextureQ64 (loadmodel, tx);
				else
					Sky_LoadTexture (loadmodel, tx);
			}
			else if (tx->name[0] == '*') //warping texture
			{
				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark();
				COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf (filename, sizeof(filename), "textures/%s/#%s", mapname, tx->name+1); //this also replaces the '*' with a '#'
				data = Image_LoadImage (filename, &fwidth, &fheight);
				if (!data)
				{
					q_snprintf (filename, sizeof(filename), "textures/#%s", tx->name+1);
					data = Image_LoadImage (filename, &fwidth, &fheight);
				}

				//now load whatever we found
				if (data) //load external image
				{
					q_strlcpy (texturename, filename, sizeof(texturename));
					tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, fwidth, fheight,
						SRC_RGBA, data, filename, 0, TEXPREF_NONE);
				}
				else //use the texture from the bsp file
				{
					q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
						wad3texture ? SRC_RGBA : SRC_INDEXED, (byte *)(tx+1), sourcefile, offset,
						wad3alpha ? TEXPREF_ALPHA : TEXPREF_NONE);
				}

				//now create the warpimage with deterministic dummy data; it will be updated before drawing
				Hunk_FreeToLowMark (mark);
				mark = Hunk_LowMark ();
				dummy = (byte *) Hunk_Alloc (gl_warpimagesize*gl_warpimagesize*4);
				memset (dummy, 0, gl_warpimagesize*gl_warpimagesize*4);
				q_snprintf (texturename, sizeof(texturename), "%s_warp", texturename);
				flags = TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE;
				if (GL_GenerateMipmap)
					flags |= TEXPREF_MIPMAP;
				tx->warpimage = TexMgr_LoadImage (loadmodel, texturename, gl_warpimagesize,
					gl_warpimagesize, SRC_RGBA, dummy, "", (src_offset_t)dummy, flags);
				Hunk_FreeToLowMark (mark);
				tx->update_warp = true;
			}
			else //regular texture
			{
				// ericw -- fence textures
				int	extraflags;

				extraflags = 0;
				if (tx->name[0] == '{')
					extraflags |= TEXPREF_ALPHA;
				if (wad3alpha)
					extraflags |= TEXPREF_ALPHA;
				// ericw

				//external textures -- first look in "textures/mapname/" then look in "textures/"
				mark = Hunk_LowMark ();
				COM_StripExtension (loadmodel->name + 5, mapname, sizeof(mapname));
				q_snprintf (filename, sizeof(filename), "textures/%s/%s", mapname, tx->name);
				data = Image_LoadImage (filename, &fwidth, &fheight);
				if (!data)
				{
					q_snprintf (filename, sizeof(filename), "textures/%s", tx->name);
					data = Image_LoadImage (filename, &fwidth, &fheight);
				}

				//now load whatever we found
				if (data) //load external image
				{
					char filename2[MAX_OSPATH];
					tx->gltexture = TexMgr_LoadImage (loadmodel, filename, fwidth, fheight,
						SRC_RGBA, data, filename, 0, TEXPREF_MIPMAP | extraflags );

					//now try to load glow/luma image from the same place
					Hunk_FreeToLowMark (mark);
					q_snprintf (filename2, sizeof(filename2), "%s_glow", filename);
					data = Image_LoadImage (filename2, &fwidth, &fheight);
					if (!data)
					{
						q_snprintf (filename2, sizeof(filename2), "%s_luma", filename);
						data = Image_LoadImage (filename2, &fwidth, &fheight);
					}

					if (data)
						tx->fullbright = TexMgr_LoadImage (loadmodel, filename2, fwidth, fheight,
							SRC_RGBA, data, filename2, 0, TEXPREF_MIPMAP | extraflags );
				}
				else //use the texture from the bsp file
				{
					q_snprintf (texturename, sizeof(texturename), "%s:%s", loadmodel->name, tx->name);
					if (!wad3texture && Mod_CheckFullbrights ((byte *)(tx+1), pixels))
					{
						tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
							SRC_INDEXED, (byte *)(tx+1), sourcefile, offset, TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
						q_snprintf (texturename, sizeof(texturename), "%s:%s_glow", loadmodel->name, tx->name);
						tx->fullbright = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
							SRC_INDEXED, (byte *)(tx+1), sourcefile, offset, TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
					}
					else
					{
						tx->gltexture = TexMgr_LoadImage (loadmodel, texturename, tx->width, tx->height,
							wad3texture ? SRC_RGBA : SRC_INDEXED, (byte *)(tx+1), sourcefile, offset, TEXPREF_MIPMAP | extraflags);
					}
				}
				Hunk_FreeToLowMark (mark);
			}
		}
		//johnfitz
	}
	W_FreeMapWadList (wads);

	//johnfitz -- last 2 slots in array should be filled with dummy textures
	loadmodel->textures[loadmodel->numtextures-2] = r_notexture_mip; //for lightmapped surfs
	loadmodel->textures[loadmodel->numtextures-1] = r_notexture_mip2; //for SURF_DRAWTILED surfs

//
// sequence the animations
//
	for (i=0 ; i<nummiptex ; i++)
	{
		tx = loadmodel->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue;	// already sequenced

	// find the number of frames in the animation
		memset (anims, 0, sizeof(anims));
		memset (altanims, 0, sizeof(altanims));

		maxanim = tx->name[1];
		altmax = 0;
		if (maxanim >= 'a' && maxanim <= 'z')
			maxanim -= 'a' - 'A';
		if (maxanim >= '0' && maxanim <= '9')
		{
			maxanim -= '0';
			altmax = 0;
			anims[maxanim] = tx;
			maxanim++;
		}
		else if (maxanim >= 'A' && maxanim <= 'J')
		{
			altmax = maxanim - 'A';
			maxanim = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else
			Sys_Error ("Bad animating texture %s", tx->name);

		for (j=i+1 ; j<nummiptex ; j++)
		{
			tx2 = loadmodel->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name+2, tx->name+2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num+1 > maxanim)
					maxanim = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num+1 > altmax)
					altmax = num+1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

		if (loadmodel->bspversion == BSPVERSION_QUAKE64 && !Mod_CheckAnimTextureArrayQ64(anims, maxanim))
			continue; // Just pretend this is a normal texture

#define	ANIM_CYCLE	2
	// link them all together
		for (j=0 ; j<maxanim ; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = maxanim * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = anims[ (j+1)%maxanim ];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j=0 ; j<altmax ; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s",j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[ (j+1)%altmax ];
			if (maxanim)
				tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadLighting -- johnfitz -- replaced with lit support code via lordhavoc
=================
*/
static void Mod_LoadLighting (lump_t *l)
{
	int i, mark;
	byte *in, *out, *data;
	byte d, q64_b0, q64_b1;
	char litfilename[MAX_OSPATH];
	unsigned int path_id;

	loadmodel->lightdata = NULL;
	// LordHavoc: check for a .lit file
	q_strlcpy(litfilename, loadmodel->name, sizeof(litfilename));
	COM_StripExtension(litfilename, litfilename, sizeof(litfilename));
	q_strlcat(litfilename, ".lit", sizeof(litfilename));
	mark = Hunk_LowMark();
	data = (byte*) COM_LoadHunkFile (litfilename, &path_id);
	if (data)
	{
		// use lit file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", litfilename);
		}
		else
		if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = LittleLong(((int *)data)[1]);
			if (i == 1)
			{
				if (8+l->filelen*3 == com_filesize)
				{
					Con_DPrintf2("%s loaded\n", litfilename);
					loadmodel->lightdata = data + 8;
					return;
				}
				Hunk_FreeToLowMark(mark);
				Con_Printf("Outdated .lit file (%s should be %u bytes, not %u)\n", litfilename, 8+l->filelen*3, com_filesize);
			}
			else
			{
				Hunk_FreeToLowMark(mark);
				Con_Printf("Unknown .lit file version (%d)\n", i);
			}
		}
		else
		{
			Hunk_FreeToLowMark(mark);
			Con_Printf("Corrupt .lit file (old version?), ignoring\n");
		}
	}
	// LordHavoc: no .lit found, expand the white lighting data to color
	if (!l->filelen)
		return;

	// Quake64 bsp lighmap data
	if (loadmodel->bspversion == BSPVERSION_QUAKE64)
	{
		// RGB lightmap samples are packed in 16bits.
		// RRRRR GGGGG BBBBBB

		loadmodel->lightdata = (byte *) Hunk_AllocName ( (l->filelen / 2)*3, litfilename);
		in = mod_base + l->fileofs;
		out = loadmodel->lightdata;

		for (i = 0;i < (l->filelen / 2) ;i++)
		{
			q64_b0 = *in++;
			q64_b1 = *in++;

			*out++ = q64_b0 & 0xf8;/* 0b11111000 */
			*out++ = ((q64_b0 & 0x07) << 5) + ((q64_b1 & 0xc0) >> 5);/* 0b00000111, 0b11000000 */
			*out++ = (q64_b1 & 0x3f) << 2;/* 0b00111111 */
		}
		return;
	}

	loadmodel->lightdata = (byte *) Hunk_AllocName ( l->filelen*3, litfilename);
	in = loadmodel->lightdata + l->filelen*2; // place the file at the end, so it will not be overwritten until the very last write
	out = loadmodel->lightdata;
	memcpy (in, mod_base + l->fileofs, l->filelen);
	for (i = 0;i < l->filelen;i++)
	{
		d = *in++;
		*out++ = d;
		*out++ = d;
		*out++ = d;
	}
}


/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility (lump_t *l)
{
	loadmodel->viswarn = false;
	if (!l->filelen)
	{
		loadmodel->visdata = NULL;
		return;
	}
	loadmodel->visdata = (byte *) Hunk_AllocName ( l->filelen, loadname);
	memcpy (loadmodel->visdata, mod_base + l->fileofs, l->filelen);
}


/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities (lump_t *l)
{
	char	basemapname[MAX_QPATH];
	char	entfilename[MAX_QPATH];
	char		*ents;
	int		mark;
	unsigned int	path_id;
	unsigned int	crc = 0;

	if (! external_ents.value)
		goto _load_embedded;

	mark = Hunk_LowMark();
	if (l->filelen > 0) {
		crc = CRC_Block(mod_base + l->fileofs, l->filelen - 1);
	}

	q_strlcpy(basemapname, loadmodel->name, sizeof(basemapname));
	COM_StripExtension(basemapname, basemapname, sizeof(basemapname));

	q_snprintf(entfilename, sizeof(entfilename), "%s@%04x.ent", basemapname, crc);
	Con_DPrintf2("trying to load %s\n", entfilename);
	ents = (char *) COM_LoadHunkFile (entfilename, &path_id);

	if (!ents)
	{
		q_snprintf(entfilename, sizeof(entfilename), "%s.ent", basemapname);
		Con_DPrintf2("trying to load %s\n", entfilename);
		ents = (char *) COM_LoadHunkFile (entfilename, &path_id);
	}

	if (ents)
	{
		// use ent file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < loadmodel->path_id)
		{
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("ignored %s from a gamedir with lower priority\n", entfilename);
		}
		else
		{
			loadmodel->entities = ents;
			Con_DPrintf("Loaded external entity file %s\n", entfilename);
			return;
		}
	}

_load_embedded:
	if (!l->filelen)
	{
		loadmodel->entities = NULL;
		return;
	}
	// Note: some BSPs don't contain a NUL terminator, e.g.
	// https://www.quakeone.com/qrack/maps/Mcmdm04.bsp
	// https://www.quakeone.com/qrack/maps/Jvoxdm3.bsp
	loadmodel->entities = (char *) Hunk_AllocName (l->filelen + 1, loadname);
	memcpy (loadmodel->entities, mod_base + l->fileofs, l->filelen);
	loadmodel->entities[l->filelen] = '\0';
}


/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes (lump_t *l)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int			i, count;

	in = (dvertex_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mvertex_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->vertexes = out;
	loadmodel->numvertexes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		out->position[0] = LittleFloat (in->point[0]);
		out->position[1] = LittleFloat (in->point[1]);
		out->position[2] = LittleFloat (in->point[2]);
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges (lump_t *l, int bsp2)
{
	medge_t *out;
	int 	i, count;

	if (bsp2)
	{
		dledge_t *in = (dledge_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i=0 ; i<count ; i++, in++, out++)
		{
			out->v[0] = LittleLong(in->v[0]);
			out->v[1] = LittleLong(in->v[1]);
		}
	}
	else
	{
		dsedge_t *in = (dsedge_t *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (medge_t *) Hunk_AllocName ( (count + 1) * sizeof(*out), loadname);

		loadmodel->edges = out;
		loadmodel->numedges = count;

		for (i=0 ; i<count ; i++, in++, out++)
		{
			out->v[0] = (unsigned short)LittleShort(in->v[0]);
			out->v[1] = (unsigned short)LittleShort(in->v[1]);
		}
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
static void Mod_LoadTexinfo (lump_t *l)
{
	texinfo_t *in;
	mtexinfo_t *out;
	int	i, j, count, miptex;
	int missing = 0; //johnfitz

	in = (texinfo_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mtexinfo_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<4 ; j++)
		{
			out->vecs[0][j] = LittleFloat (in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat (in->vecs[1][j]);
		}

		miptex = LittleLong (in->miptex);
		out->flags = LittleLong (in->flags);

		//johnfitz -- rewrote this section
		if (miptex >= loadmodel->numtextures-1 || !loadmodel->textures[miptex])
		{
			if (out->flags & TEX_SPECIAL)
				out->texture = loadmodel->textures[loadmodel->numtextures-1];
			else
				out->texture = loadmodel->textures[loadmodel->numtextures-2];
			out->flags |= TEX_MISSING;
			missing++;
		}
		else
		{
			out->texture = loadmodel->textures[miptex];
		}
		//johnfitz
	}

	//johnfitz: report missing textures
	if (missing && loadmodel->numtextures > 1)
		Con_Printf ("Mod_LoadTexinfo: %d texture(s) missing from BSP file\n", missing);
	//johnfitz
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
static void CalcSurfaceExtents (msurface_t *s)
{
	float	mins[2], maxs[2], val;
	int		i,j, e;
	mvertex_t	*v;
	mtexinfo_t	*tex;
	int		bmins[2], bmaxs[2];

	mins[0] = mins[1] = FLT_MAX;
	maxs[0] = maxs[1] = -FLT_MAX;

	tex = s->texinfo;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for (j=0 ; j<2 ; j++)
		{
			/* The following calculation is sensitive to floating-point
			 * precision.  It needs to produce the same result that the
			 * light compiler does, because R_BuildLightMap uses surf->
			 * extents to know the width/height of a surface's lightmap,
			 * and incorrect rounding here manifests itself as patches
			 * of "corrupted" looking lightmaps.
			 * Most light compilers are win32 executables, so they use
			 * x87 floating point.  This means the multiplies and adds
			 * are done at 80-bit precision, and the result is rounded
			 * down to 32-bits and stored in val.
			 * Adding the casts to double seems to be good enough to fix
			 * lighting glitches when Quakespasm is compiled as x86_64
			 * and using SSE2 floating-point.  A potential trouble spot
			 * is the hallway at the beginning of mfxsp17.  -- ericw
			 */
			val =	((double)v->position[0] * (double)tex->vecs[j][0]) +
				((double)v->position[1] * (double)tex->vecs[j][1]) +
				((double)v->position[2] * (double)tex->vecs[j][2]) +
				(double)tex->vecs[j][3];

			if (val < mins[j])
				mins[j] = val;
			if (val > maxs[j])
				maxs[j] = val;
		}
	}

	for (i=0 ; i<2 ; i++)
	{
		bmins[i] = floor(mins[i]/16);
		bmaxs[i] = ceil(maxs[i]/16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if ( !(tex->flags & TEX_SPECIAL) && s->extents[i] > 2000) //johnfitz -- was 512 in glquake, 256 in winquake
			Sys_Error ("Bad surface extents");
	}
}

/*
================
Mod_PolyForUnlitSurface -- johnfitz -- creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into BuildSurfaceDisplayList?
================
*/
static void Mod_PolyForUnlitSurface (msurface_t *fa)
{
	const int	numverts = fa->numedges;
	int		i, lindex;
	float		*vec;
	glpoly_t	*poly;
	float		texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0f/128.0f); //warp animation repeats every 128
	else
		texscale = (1.0f/32.0f); //to match r_notexture_mip

	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;
	for (i=0; i<numverts; i++)
	{
		lindex = loadmodel->surfedges[fa->firstedge + i];
		vec = (lindex > 0) ?
			loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position :
			loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = DotProduct(vec, fa->texinfo->vecs[0]) * texscale;
		poly->verts[i][4] = DotProduct(vec, fa->texinfo->vecs[1]) * texscale;
	}
}

/*
=================
Mod_CalcSurfaceBounds -- johnfitz -- calculate bounding box for per-surface frustum culling
=================
*/
static void Mod_CalcSurfaceBounds (msurface_t *s)
{
	int			i, e;
	mvertex_t	*v;

	s->mins[0] = s->mins[1] = s->mins[2] = FLT_MAX;
	s->maxs[0] = s->maxs[1] = s->maxs[2] = -FLT_MAX;

	for (i=0 ; i<s->numedges ; i++)
	{
		e = loadmodel->surfedges[s->firstedge+i];
		if (e >= 0)
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		if (s->mins[0] > v->position[0])
			s->mins[0] = v->position[0];
		if (s->mins[1] > v->position[1])
			s->mins[1] = v->position[1];
		if (s->mins[2] > v->position[2])
			s->mins[2] = v->position[2];

		if (s->maxs[0] < v->position[0])
			s->maxs[0] = v->position[0];
		if (s->maxs[1] < v->position[1])
			s->maxs[1] = v->position[1];
		if (s->maxs[2] < v->position[2])
			s->maxs[2] = v->position[2];
	}
}

/*
=================
Mod_LoadFaces
=================
*/
static void Mod_LoadFaces (lump_t *l, qboolean bsp2)
{
	dsface_t	*ins;
	dlface_t	*inl;
	msurface_t 	*out;
	int			i, count, surfnum, lofs;
	int			planenum, side, texinfon;

	if (bsp2)
	{
		ins = NULL;
		inl = (dlface_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*inl))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(*inl);
	}
	else
	{
		ins = (dsface_t *)(mod_base + l->fileofs);
		inl = NULL;
		if (l->filelen % sizeof(*ins))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
		count = l->filelen / sizeof(*ins);
	}
	out = (msurface_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn mappers about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i faces exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for (surfnum=0 ; surfnum<count ; surfnum++, out++)
	{
		if (bsp2)
		{
			out->firstedge = LittleLong(inl->firstedge);
			out->numedges = LittleLong(inl->numedges);
			planenum = LittleLong(inl->planenum);
			side = LittleLong(inl->side);
			texinfon = LittleLong (inl->texinfo);
			for (i=0 ; i<MAXLIGHTMAPS ; i++)
				out->styles[i] = inl->styles[i];
			lofs = LittleLong(inl->lightofs);
			inl++;
		}
		else
		{
			out->firstedge = LittleLong(ins->firstedge);
			out->numedges = LittleShort(ins->numedges);
			planenum = LittleShort(ins->planenum);
			side = LittleShort(ins->side);
			texinfon = LittleShort (ins->texinfo);
			for (i=0 ; i<MAXLIGHTMAPS ; i++)
				out->styles[i] = ins->styles[i];
			lofs = LittleLong(ins->lightofs);
			ins++;
		}

		out->flags = 0;
		if (out->numedges < 3)
			DebugLog("Mod_LoadFaces: %s surfnum %d has bad numedges %d\n", loadmodel->name, surfnum, out->numedges);

		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = loadmodel->planes + planenum;

		out->texinfo = loadmodel->texinfo + texinfon;

		CalcSurfaceExtents (out);

		Mod_CalcSurfaceBounds (out); //johnfitz -- for per-surface frustum culling

	// lighting info
		if (loadmodel->bspversion == BSPVERSION_QUAKE64)
			lofs /= 2; // Q64 samples are 16bits instead 8 in normal Quake 

		if (lofs == -1)
			out->samples = NULL;
		else
			out->samples = loadmodel->lightdata + (lofs * 3); //johnfitz -- lit support via lordhavoc (was "+ i")

		//johnfitz -- this section rewritten
		if (!q_strncasecmp(out->texinfo->texture->name,"sky",3)) // sky surface //also note -- was Q_strncmp, changed to match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			Mod_PolyForUnlitSurface (out); //no more subdivision
		}
		else if (out->texinfo->texture->name[0] == '*') // warp surface
		{
			out->flags |= SURF_DRAWTURB;
			if (out->texinfo->flags & TEX_SPECIAL)
				out->flags |= SURF_DRAWTILED;
			else if (out->samples && !loadmodel->haslitwater)
			{
				Con_DPrintf ("Map has lit water\n");
				loadmodel->haslitwater = true;
			}

		// detect special liquid types
			if (!strncmp (out->texinfo->texture->name, "*lava", 5))
				out->flags |= SURF_DRAWLAVA;
			else if (!strncmp (out->texinfo->texture->name, "*slime", 6))
				out->flags |= SURF_DRAWSLIME;
			else if (!strncmp (out->texinfo->texture->name, "*tele", 5))
				out->flags |= SURF_DRAWTELE;
			else out->flags |= SURF_DRAWWATER;

			// polys are only created for unlit water here.
			// lit water is handled in BuildSurfaceDisplayList
			if (out->flags & SURF_DRAWTILED)
			{
				Mod_PolyForUnlitSurface (out);
				GL_SubdivideSurface (out);
			}
		}
		else if (out->texinfo->texture->name[0] == '{') // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
		}
		else if (out->texinfo->flags & TEX_MISSING) // texture is missing from bsp
		{
			if (out->samples) //lightmapped
			{
				out->flags |= SURF_NOTEXTURE;
			}
			else // not lightmapped
			{
				out->flags |= (SURF_NOTEXTURE | SURF_DRAWTILED);
				Mod_PolyForUnlitSurface (out);
			}
		}
		//johnfitz
	}
}


/*
=================
Mod_SetParent
=================
*/
static void Mod_SetParent (mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents < 0)
		return;
	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes_S (lump_t *l)
{
	int			i, j, count, p;
	dsnode_t	*in;
	mnode_t		*out;

	in = (dsnode_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn mappers about exceeding old limits
	if (count > 32767)
		Con_DWarning ("%i nodes exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = (unsigned short)LittleShort (in->firstface); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = (unsigned short)LittleShort (in->numfaces); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = (unsigned short)LittleShort(in->children[j]);
			if (p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 65535 - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

static void Mod_LoadNodes_L1 (lump_t *l)
{
	int			i, j, count, p;
	dl1node_t	*in;
	mnode_t		*out;

	in = (dl1node_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleLong (in->firstface); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = LittleLong (in->numfaces); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = LittleLong(in->children[j]);
			if (p >= 0 && p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 0xffffffff - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

static void Mod_LoadNodes_L2 (lump_t *l)
{
	int			i, j, count, p;
	dl2node_t	*in;
	mnode_t		*out;

	in = (dl2node_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s",loadmodel->name);

	count = l->filelen / sizeof(*in);
	out = (mnode_t *)Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->nodes = out;
	loadmodel->numnodes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleFloat (in->mins[j]);
			out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
		}

		p = LittleLong(in->planenum);
		out->plane = loadmodel->planes + p;

		out->firstsurface = LittleLong (in->firstface); //johnfitz -- explicit cast as unsigned short
		out->numsurfaces = LittleLong (in->numfaces); //johnfitz -- explicit cast as unsigned short

		for (j=0 ; j<2 ; j++)
		{
			//johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = LittleLong(in->children[j]);
			if (p > 0 && p < count)
				out->children[j] = loadmodel->nodes + p;
			else
			{
				p = 0xffffffff - p; //note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < loadmodel->numleafs)
					out->children[j] = (mnode_t *)(loadmodel->leafs + p);
				else
				{
					Con_Printf("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, loadmodel->numleafs);
					out->children[j] = (mnode_t *)(loadmodel->leafs); //map it to the solid leaf
				}
			}
			//johnfitz
		}
	}
}

static void Mod_LoadNodes (lump_t *l, int bsp2)
{
	if (bsp2 == 2)
		Mod_LoadNodes_L2(l);
	else if (bsp2)
		Mod_LoadNodes_L1(l);
	else
		Mod_LoadNodes_S(l);

	Mod_SetParent (loadmodel->nodes, NULL);	// sets nodes and leafs
}

static void Mod_ProcessLeafs_S (dsleaf_t *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);
	count = filelen / sizeof(*in);
	out = (mleaf_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz
	if (count > 32767)
		Host_Error ("Mod_LoadLeafs: %i leafs exceeds limit of 32767.", count);
	//johnfitz

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + (unsigned short)LittleShort(in->firstmarksurface); //johnfitz -- unsigned short
		out->nummarksurfaces = (unsigned short)LittleShort(in->nummarksurfaces); //johnfitz -- unsigned short

		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		//johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

static void Mod_ProcessLeafs_L1 (dl1leaf_t *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(*in);

	out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), loadname);

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleShort (in->mins[j]);
			out->minmaxs[3+j] = LittleShort (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + LittleLong(in->firstmarksurface); //johnfitz -- unsigned short
		out->nummarksurfaces = LittleLong(in->nummarksurfaces); //johnfitz -- unsigned short

		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		//johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

static void Mod_ProcessLeafs_L2 (dl2leaf_t *in, int filelen)
{
	mleaf_t		*out;
	int			i, j, count, p;

	if (filelen % sizeof(*in))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", loadmodel->name);

	count = filelen / sizeof(*in);

	out = (mleaf_t *) Hunk_AllocName (count * sizeof(*out), loadname);

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->minmaxs[j] = LittleFloat (in->mins[j]);
			out->minmaxs[3+j] = LittleFloat (in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->firstmarksurface = loadmodel->marksurfaces + LittleLong(in->firstmarksurface); //johnfitz -- unsigned short
		out->nummarksurfaces = LittleLong(in->nummarksurfaces); //johnfitz -- unsigned short

		p = LittleLong(in->visofs);
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = loadmodel->visdata + p;
		out->efrags = NULL;

		for (j=0 ; j<4 ; j++)
			out->ambient_sound_level[j] = in->ambient_level[j];

		//johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs (lump_t *l, int bsp2)
{
	void *in = (void *)(mod_base + l->fileofs);

	if (bsp2 == 2)
		Mod_ProcessLeafs_L2 ((dl2leaf_t *)in, l->filelen);
	else if (bsp2)
		Mod_ProcessLeafs_L1 ((dl1leaf_t *)in, l->filelen);
	else
		Mod_ProcessLeafs_S  ((dsleaf_t *) in, l->filelen);
}

/*
=================
Mod_CheckWaterVis
=================
*/
static void Mod_CheckWaterVis (void)
{
	mleaf_t *leaf, *other;
	msurface_t *surf;
	int i, j, k;
	int numclusters = loadmodel->submodels[0].visleafs;
	int contentfound = 0;
	int contenttransparent = 0;
	int contenttype;
	unsigned int hascontents = 0;

	if (r_novis.value)
	{
		loadmodel->contentstransparent = SURF_DRAWWATER | SURF_DRAWTELE | SURF_DRAWSLIME | SURF_DRAWLAVA;
		return;
	}

	for (i = 0, leaf = loadmodel->leafs + 1; i < numclusters; i++, leaf++)
	{
		byte *vis;

		if (leaf->contents < 0 && -leaf->contents < 32)
			hascontents |= 1u << -leaf->contents;
		if (leaf->contents == CONTENTS_WATER)
		{
			if ((contenttransparent & (SURF_DRAWWATER | SURF_DRAWTELE)) == (SURF_DRAWWATER | SURF_DRAWTELE))
				continue;
			for (contenttype = 0, j = 0; j < leaf->nummarksurfaces; j++)
			{
				surf = leaf->firstmarksurface[j];
				if (surf->flags & (SURF_DRAWWATER | SURF_DRAWTELE))
				{
					contenttype = surf->flags & (SURF_DRAWWATER | SURF_DRAWTELE);
					break;
				}
			}
			if (!contenttype)
				continue;
		}
		else if (leaf->contents == CONTENTS_SLIME)
			contenttype = SURF_DRAWSLIME;
		else if (leaf->contents == CONTENTS_LAVA)
			contenttype = SURF_DRAWLAVA;
		else
			continue;

		if (contenttransparent & contenttype)
		{
nextleaf:
			continue;
		}
		contentfound |= contenttype;
		vis = Mod_DecompressVis(leaf->compressed_vis, loadmodel);
		for (j = 0; j < (numclusters + 7) / 8; j++)
		{
			if (!vis[j])
				continue;
			for (k = 0; k < 8; k++)
			{
				int othercluster = (j << 3) + k;

				if (othercluster >= numclusters)
					continue;
				if (!(vis[j] & (1u << k)))
					continue;
				other = &loadmodel->leafs[othercluster + 1];
				if (leaf->contents != other->contents)
				{
					contenttransparent |= contenttype;
					goto nextleaf;
				}
			}
		}
	}

	if (!contenttransparent)
	{
		if (hascontents & ((1u << -CONTENTS_WATER) | (1u << -CONTENTS_SLIME) | (1u << -CONTENTS_LAVA)))
			Con_DPrintf("%s is not watervised\n", loadmodel->name);
	}
	else
	{
		Con_DPrintf2("%s is vised for transparent", loadmodel->name);
		if (contenttransparent & SURF_DRAWWATER)
			Con_DPrintf2(" water");
		if (contenttransparent & SURF_DRAWTELE)
			Con_DPrintf2(" tele");
		if (contenttransparent & SURF_DRAWLAVA)
			Con_DPrintf2(" lava");
		if (contenttransparent & SURF_DRAWSLIME)
			Con_DPrintf2(" slime");
		Con_DPrintf2("\n");
	}

	loadmodel->contentstransparent = contenttransparent |
		(~contentfound & (SURF_DRAWWATER | SURF_DRAWTELE | SURF_DRAWSLIME | SURF_DRAWLAVA));
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes (lump_t *l, qboolean bsp2)
{
	dsclipnode_t *ins;
	dlclipnode_t *inl;

	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, count;
	hull_t		*hull;

	if (bsp2)
	{
		ins = NULL;
		inl = (dlclipnode_t *)(mod_base + l->fileofs);
		if (l->filelen % sizeof(*inl))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*inl);
	}
	else
	{
		ins = (dsclipnode_t *)(mod_base + l->fileofs);
		inl = NULL;
		if (l->filelen % sizeof(*ins))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*ins);
	}
	out = (mclipnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	//johnfitz -- warn about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i clipnodes exceeds standard limit of 32767.\n", count);
	//johnfitz

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	if (bsp2)
	{
		for (i=0 ; i<count ; i++, out++, inl++)
		{
			out->planenum = LittleLong(inl->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			out->children[0] = LittleLong(inl->children[0]);
			out->children[1] = LittleLong(inl->children[1]);
			//Spike: FIXME: bounds check
		}
	}
	else
	{
		for (i=0 ; i<count ; i++, out++, ins++)
		{
			out->planenum = LittleLong(ins->planenum);

			//johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= loadmodel->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			//johnfitz

			//johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)LittleShort(ins->children[0]);
			out->children[1] = (unsigned short)LittleShort(ins->children[1]);

			if (out->children[0] >= count)
				out->children[0] -= 65536;
			if (out->children[1] >= count)
				out->children[1] -= 65536;
			//johnfitz
		}
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0 (void)
{
	mnode_t		*in, *child;
	mclipnode_t *out; //johnfitz -- was dclipnode_t
	int			i, j, count;
	hull_t		*hull;

	hull = &loadmodel->hulls[0];

	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = (mclipnode_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count-1;
	hull->planes = loadmodel->planes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planenum = in->plane - loadmodel->planes;
		for (j=0 ; j<2 ; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
static void Mod_LoadMarksurfaces (lump_t *l, int bsp2)
{
	int		i, j, count;
	msurface_t **out;
	if (bsp2)
	{
		unsigned int *in = (unsigned int *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (msurface_t **)Hunk_AllocName ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		for (i=0 ; i<count ; i++)
		{
			j = LittleLong(in[i]);
			if (j >= loadmodel->numsurfaces)
				Host_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = loadmodel->surfaces + j;
		}
	}
	else
	{
		short *in = (short *)(mod_base + l->fileofs);

		if (l->filelen % sizeof(*in))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s",loadmodel->name);

		count = l->filelen / sizeof(*in);
		out = (msurface_t **)Hunk_AllocName ( count*sizeof(*out), loadname);

		loadmodel->marksurfaces = out;
		loadmodel->nummarksurfaces = count;

		//johnfitz -- warn mappers about exceeding old limits
		if (count > 32767)
			Con_DWarning ("%i marksurfaces exceeds standard limit of 32767.\n", count);
		//johnfitz

		for (i=0 ; i<count ; i++)
		{
			j = (unsigned short)LittleShort(in[i]); //johnfitz -- explicit cast as unsigned short
			if (j >= loadmodel->numsurfaces)
				Sys_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = loadmodel->surfaces + j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
static void Mod_LoadSurfedges (lump_t *l)
{
	int		i, count;
	int		*in, *out;

	in = (int *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (int *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->surfedges = out;
	loadmodel->numsurfedges = count;

	for (i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}


/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes (lump_t *l)
{
	int			i, j;
	mplane_t	*out;
	dplane_t 	*in;
	int			count;
	int			bits;

	in = (dplane_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (mplane_t *) Hunk_AllocName ( count*2*sizeof(*out), loadname);

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
static float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int		i;
	vec3_t	corner;

	for (i=0 ; i<3 ; i++)
	{
		corner[i] = fabs(mins[i]) > fabs(maxs[i]) ? fabs(mins[i]) : fabs(maxs[i]);
	}

	return VectorLength (corner);
}

/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels (lump_t *l)
{
	dmodel_t	*in;
	dmodel_t	*out;
	int			i, j, count;

	in = (dmodel_t *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s",loadmodel->name);
	count = l->filelen / sizeof(*in);
	out = (dmodel_t *) Hunk_AllocName ( count*sizeof(*out), loadname);

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
			out->origin[j] = LittleFloat (in->origin[j]);
		}
		for (j=0 ; j<MAX_MAP_HULLS ; j++)
			out->headnode[j] = LittleLong (in->headnode[j]);
		out->visleafs = LittleLong (in->visleafs);
		out->firstface = LittleLong (in->firstface);
		out->numfaces = LittleLong (in->numfaces);
	}

	// johnfitz -- check world visleafs -- adapted from bjp
	out = loadmodel->submodels;

	if (out->visleafs > 8192)
		Con_DWarning ("%i visleafs exceeds standard limit of 8192.\n", out->visleafs);
	//johnfitz
}

/*
=================
Mod_BoundsFromClipNode -- johnfitz

update the model's clipmins and clipmaxs based on each node's plane.

This works because of the way brushes are expanded in hull generation.
Each brush will include all six axial planes, which bound that brush.
Therefore, the bounding box of the hull can be constructed entirely
from axial planes found in the clipnodes for that hull.
=================
*/
#if 0 /* disabled for now -- see in Mod_SetupSubmodels()  */
static void Mod_BoundsFromClipNode (qmodel_t *mod, int hull, int nodenum)
{
	mplane_t	*plane;
	mclipnode_t	*node;

	if (nodenum < 0)
		return; //hit a leafnode

	node = &mod->clipnodes[nodenum];
	plane = mod->hulls[hull].planes + node->planenum;
	switch (plane->type)
	{

	case PLANE_X:
		if (plane->signbits == 1)
			mod->clipmins[0] = q_min(mod->clipmins[0], -plane->dist - mod->hulls[hull].clip_mins[0]);
		else
			mod->clipmaxs[0] = q_max(mod->clipmaxs[0], plane->dist - mod->hulls[hull].clip_maxs[0]);
		break;
	case PLANE_Y:
		if (plane->signbits == 2)
			mod->clipmins[1] = q_min(mod->clipmins[1], -plane->dist - mod->hulls[hull].clip_mins[1]);
		else
			mod->clipmaxs[1] = q_max(mod->clipmaxs[1], plane->dist - mod->hulls[hull].clip_maxs[1]);
		break;
	case PLANE_Z:
		if (plane->signbits == 4)
			mod->clipmins[2] = q_min(mod->clipmins[2], -plane->dist - mod->hulls[hull].clip_mins[2]);
		else
			mod->clipmaxs[2] = q_max(mod->clipmaxs[2], plane->dist - mod->hulls[hull].clip_maxs[2]);
		break;
	default:
		//skip nonaxial planes; don't need them
		break;
	}

	Mod_BoundsFromClipNode (mod, hull, node->children[0]);
	Mod_BoundsFromClipNode (mod, hull, node->children[1]);
}
#endif /* #if 0 */

/* EXTERNAL VIS FILE SUPPORT:
 */
typedef struct vispatch_s
{
	char	mapname[32];
	int	filelen;	// length of data after header (VIS+Leafs)
} vispatch_t;
#define VISPATCH_HEADER_LEN 36

static FILE *Mod_FindVisibilityExternal(void)
{
	vispatch_t header;
	char visfilename[MAX_QPATH];
	const char* shortname;
	unsigned int path_id;
	FILE *f;
	long pos;
	size_t r;

	q_snprintf(visfilename, sizeof(visfilename), "maps/%s.vis", loadname);
	if (COM_FOpenFile(visfilename, &f, &path_id) < 0)
	{
		Con_DPrintf("%s not found, trying ", visfilename);
		q_snprintf(visfilename, sizeof(visfilename), "%s.vis", COM_SkipPath(com_gamedir));
		Con_DPrintf("%s\n", visfilename);
		if (COM_FOpenFile(visfilename, &f, &path_id) < 0)
		{
			Con_DPrintf("external vis not found\n");
			return NULL;
		}
	}
	if (path_id < loadmodel->path_id)
	{
		fclose(f);
		Con_DPrintf("ignored %s from a gamedir with lower priority\n", visfilename);
		return NULL;
	}

	Con_DPrintf("Found external VIS %s\n", visfilename);

	shortname = COM_SkipPath(loadmodel->name);
	pos = 0;
	while ((r = fread(&header, 1, VISPATCH_HEADER_LEN, f)) == VISPATCH_HEADER_LEN)
	{
		header.filelen = LittleLong(header.filelen);
		if (header.filelen <= 0) {	/* bad entry -- don't trust the rest. */
			fclose(f);
			return NULL;
		}
		if (!q_strcasecmp(header.mapname, shortname))
			break;
		pos += header.filelen + VISPATCH_HEADER_LEN;
		fseek(f, pos, SEEK_SET);
	}
	if (r != VISPATCH_HEADER_LEN) {
		fclose(f);
		Con_DPrintf("%s not found in %s\n", shortname, visfilename);
		return NULL;
	}

	return f;
}

static byte *Mod_LoadVisibilityExternal(FILE* f)
{
	int	filelen;
	byte*	visdata;

	filelen = 0;
	if (!fread(&filelen, 4, 1, f)) return NULL;
	filelen = LittleLong(filelen);
	if (filelen <= 0) return NULL;
	Con_DPrintf("...%d bytes visibility data\n", filelen);
	visdata = (byte *) Hunk_AllocName(filelen, "EXT_VIS");
	if (!fread(visdata, filelen, 1, f))
		return NULL;
	return visdata;
}

static void Mod_LoadLeafsExternal(FILE* f)
{
	int	filelen;
	void*	in;

	filelen = 0;
	if (!fread(&filelen, 4, 1, f)) return;
	filelen = LittleLong(filelen);
	if (filelen <= 0) return;
	Con_DPrintf("...%d bytes leaf data\n", filelen);
	in = Hunk_AllocName(filelen, "EXT_LEAF");
	if (!fread(in, filelen, 1, f))
		return;
	Mod_ProcessLeafs_S((dsleaf_t *)in, filelen);
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel (qmodel_t *mod, void *buffer)
{
	int			i, j;
	int			bsp2;
	dheader_t	*header;
	dmodel_t 	*bm;
	float		radius; //johnfitz

	loadmodel->type = mod_brush;

	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);

	switch(mod->bspversion)
	{
	case BSPVERSION:
		bsp2 = false;
		break;
	case BSP2VERSION_2PSB:
		bsp2 = 1;	//first iteration
		break;
	case BSP2VERSION_BSP2:
		bsp2 = 2;	//sanitised revision
		break;
	case BSPVERSION_QUAKE64:
		bsp2 = false;
		break;
	default:
		Sys_Error ("Mod_LoadBrushModel: %s has unsupported version number (%i)", mod->name, mod->bspversion);
		break;
	}

// swap all the lumps
	mod_base = (byte *)header;

	for (i = 0; i < (int) sizeof(dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong ( ((int *)header)[i]);

// load into heap

	Mod_LoadVertexes (&header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (&header->lumps[LUMP_EDGES], bsp2);
	Mod_LoadSurfedges (&header->lumps[LUMP_SURFEDGES]);
	Mod_LoadEntities (&header->lumps[LUMP_ENTITIES]);
	Mod_LoadTextures (&header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (&header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (&header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (&header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (&header->lumps[LUMP_FACES], bsp2);
	Mod_LoadMarksurfaces (&header->lumps[LUMP_MARKSURFACES], bsp2);

	if (mod->bspversion == BSPVERSION && external_vis.value && sv.modelname[0] && !q_strcasecmp(loadname, sv.name))
	{
		FILE* fvis;
		Con_DPrintf("trying to open external vis file\n");
		fvis = Mod_FindVisibilityExternal();
		if (fvis) {
			int mark = Hunk_LowMark();
			loadmodel->leafs = NULL;
			loadmodel->numleafs = 0;
			Con_DPrintf("found valid external .vis file for map\n");
			loadmodel->visdata = Mod_LoadVisibilityExternal(fvis);
			if (loadmodel->visdata) {
				Mod_LoadLeafsExternal(fvis);
			}
			fclose(fvis);
			if (loadmodel->visdata && loadmodel->leafs && loadmodel->numleafs) {
				goto visdone;
			}
			Hunk_FreeToLowMark(mark);
			Con_DPrintf("External VIS data failed, using standard vis.\n");
		}
	}

	Mod_LoadVisibility (&header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (&header->lumps[LUMP_LEAFS], bsp2);
	if (bsp2 && !loadmodel->visdata)
		DebugLog("perf: %s is BSP2 with no VIS data; renderer may scan very large leaf/surface sets (leafs=%d surfaces=%d marksurfaces=%d)\n",
			loadmodel->name, loadmodel->numleafs, loadmodel->numsurfaces,
			loadmodel->nummarksurfaces);
	visdone:
		Mod_LoadNodes (&header->lumps[LUMP_NODES], bsp2);
	Mod_LoadClipnodes (&header->lumps[LUMP_CLIPNODES], bsp2);
	Mod_LoadSubmodels (&header->lumps[LUMP_MODELS]);
	Mod_CheckWaterVis ();

	Mod_MakeHull0 ();

	mod->numframes = 2;		// regular and alternate animation

//
// set up the submodels (FIXME: this is confusing)
//

	// johnfitz -- okay, so that i stop getting confused every time i look at this loop, here's how it works:
	// we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
	// worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
	// we create a new copy of the data to use the next time through.
	for (i=0 ; i<mod->numsubmodels ; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j=1 ; j<MAX_MAP_HULLS ; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes-1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		//johnfitz -- calculate rotate bounds and yaw bounds
		radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;
		//johnfitz

		//johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		if (i > 0 || strcmp(mod->name, sv.modelname) != 0) //skip submodel 0 of sv.worldmodel, which is the actual world
		{
			// start with the hull0 bounds
			VectorCopy (mod->maxs, mod->clipmaxs);
			VectorCopy (mod->mins, mod->clipmins);

			// process hull1 (we don't need to process hull2 becuase there's
			// no such thing as a brush that appears in hull2 but not hull1)
			//Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
		}
		//johnfitz

		mod->numleafs = bm->visleafs;

		if (i < mod->numsubmodels-1)
		{	// duplicate the basic information
			char	name[12];

			sprintf (name, "*%i", i+1);
			loadmodel = Mod_FindName (name);
			*loadmodel = *mod;
			strcpy (loadmodel->name, name);
			mod = loadmodel;
		}
	}
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

aliashdr_t	*pheader;

stvert_t	stverts[MAXALIASVERTS];
mtriangle_t	triangles[MAXALIASTRIS];

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t	*poseverts[MAXALIASFRAMES];
static int		posenum;

/*
=================
Mod_LoadAliasFrame
=================
*/
static void *Mod_LoadAliasFrame (void * pin, maliasframedesc_t *frame)
{
	trivertx_t		*pinframe;
	int				i;
	daliasframe_t	*pdaliasframe;

	if (posenum >= MAXALIASFRAMES)
		Sys_Error ("posenum >= MAXALIASFRAMES");

	pdaliasframe = (daliasframe_t *)pin;

	q_strlcpy (frame->name, pdaliasframe->name, sizeof (frame->name));
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about
		// endianness
		frame->bboxmin.v[i] = pdaliasframe->bboxmin.v[i];
		frame->bboxmax.v[i] = pdaliasframe->bboxmax.v[i];
	}

	pinframe = (trivertx_t *)(pdaliasframe + 1);

	poseverts[posenum] = pinframe;
	posenum++;

	pinframe += pheader->numverts;

	return (void *)pinframe;
}


/*
=================
Mod_LoadAliasGroup
=================
*/
static void *Mod_LoadAliasGroup (void * pin,  maliasframedesc_t *frame)
{
	daliasgroup_t		*pingroup;
	int					i, numframes;
	daliasinterval_t	*pin_intervals;
	void				*ptemp;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i=0 ; i<3 ; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		if (posenum >= MAXALIASFRAMES) Sys_Error ("posenum >= MAXALIASFRAMES");

		poseverts[posenum] = (trivertx_t *)((daliasframe_t *)ptemp + 1);
		posenum++;

		ptemp = (trivertx_t *)((daliasframe_t *)ptemp + 1) + pheader->numverts;
	}

	return ptemp;
}

//=========================================================


/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct
{
	short		x, y;
} floodfill_t;

// must be a power of 2
#define	FLOODFILL_FIFO_SIZE		0x1000
#define	FLOODFILL_FIFO_MASK		(FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy )				\
do {								\
	if (pos[off] == fillcolor)				\
	{							\
		pos[off] = 255;					\
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;	\
	}							\
	else if (pos[off] != 255) fdc = pos[off];		\
} while (0)

static void Mod_FloodFillSkin( byte *skin, int skinwidth, int skinheight )
{
	byte		fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t	fifo[FLOODFILL_FIFO_SIZE];
	int			inpt = 0, outpt = 0;
	int			filledcolor = -1;
	int			i;

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
		{
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
		}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte		*pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP( -1, -1, 0 );
		if (x < skinwidth - 1)	FLOODFILL_STEP( 1, 1, 0 );
		if (y > 0)				FLOODFILL_STEP( -skinwidth, 0, -1 );
		if (y < skinheight - 1)	FLOODFILL_STEP( skinwidth, 0, 1 );
		skin[x + skinwidth * y] = fdc;
	}
}

/*
===============
Mod_LoadExternalAliasSkin

Loads a replacement MDL skin using the DarkPlaces/QSS/vkQuake naming scheme,
for example progs/ogre.mdl_0.png and progs/ogre.mdl_0_glow.png.
===============
*/
static byte *Mod_LoadExternalAliasSkin (qmodel_t *mod, int skinnum,
	const char *suffix, const char *fallbacksuffix,
	char *filename, size_t filenamesize,
	int *width, int *height)
{
	static const char *const prefixes[] = {"", "progs/", "textures/"};
	byte *data;
	int i;

	for (i = 0; i < (int)Q_COUNTOF(prefixes); i++)
	{
		q_snprintf (filename, filenamesize, "%s%s_%i%s",
			prefixes[i], mod->name, skinnum, suffix);
		data = Image_LoadImageWithPath (filename, width, height, mod->path_id);
		if (data)
			return data;

		if (fallbacksuffix)
		{
			q_snprintf (filename, filenamesize, "%s%s_%i%s",
				prefixes[i], mod->name, skinnum, fallbacksuffix);
			data = Image_LoadImageWithPath (filename, width, height, mod->path_id);
			if (data)
				return data;
		}
	}

	filename[0] = '\0';
	return NULL;
}

/*
===============
Mod_NormalizeExternalFullbright

The alias shader adds the fullbright RGB directly. Make transparent mask
pixels black and keep the uploaded texture opaque, matching indexed masks.
===============
*/
static void Mod_NormalizeExternalFullbright (byte *data, int width, int height)
{
	size_t i, pixels = (size_t)width * (size_t)height;

	for (i = 0; i < pixels; i++, data += 4)
	{
		if (data[3] == 0)
			data[0] = data[1] = data[2] = 0;
		data[3] = 255;
	}
}

/*
===============
Mod_LoadAllSkins
===============
*/
static void *Mod_LoadAllSkins (int numskins, daliasskintype_t *pskintype)
{
	int			i, j, k, size, groupskins;
	char			name[MAX_QPATH];
	byte			*skin, *texels;
	daliasskingroup_t	*pinskingroup;
	daliasskininterval_t	*pinskinintervals;
	char			fbr_mask_name[MAX_QPATH]; //johnfitz -- added for fullbright support
	src_offset_t		offset; //johnfitz
	unsigned int		texflags = TEXPREF_PAD;

	skin = (byte *)(pskintype + 1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d", numskins);

	size = pheader->skinwidth * pheader->skinheight;

	if (loadmodel->flags & MF_HOLEY)
		texflags |= TEXPREF_ALPHA;

	for (i=0 ; i<numskins ; i++)
	{
		if (pskintype->type == ALIAS_SKIN_SINGLE)
		{
			Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );

			// save 8 bit texels for the player model to remap
			texels = (byte *) Hunk_AllocName(size, loadname);
			pheader->texels[i] = texels - (byte *)pheader;
			memcpy (texels, (byte *)(pskintype + 1), size);

			pheader->gltextures[i][0] = NULL;
			pheader->fbtextures[i][0] = NULL;

			if (!isDedicated && mdl_external_textures.value)
			{
				char filename[MAX_QPATH], fbfilename[MAX_QPATH];
				byte *data, *fbdata;
				int fwidth = 0, fheight = 0;
				int mark = Hunk_LowMark ();

				data = Mod_LoadExternalAliasSkin (loadmodel, i, "", NULL,
					filename, sizeof(filename), &fwidth, &fheight);
				if (data)
				{
					pheader->gltextures[i][0] = TexMgr_LoadImage (loadmodel, filename,
						fwidth, fheight, SRC_RGBA, data, filename, 0,
						TEXPREF_ALPHA | TEXPREF_MIPMAP);

					fbdata = Mod_LoadExternalAliasSkin (loadmodel, i, "_glow", "_luma",
						fbfilename, sizeof(fbfilename), &fwidth, &fheight);
					if (fbdata)
					{
						Mod_NormalizeExternalFullbright (fbdata, fwidth, fheight);
						pheader->fbtextures[i][0] = TexMgr_LoadImage (loadmodel,
							fbfilename, fwidth, fheight, SRC_RGBA, fbdata,
							fbfilename, 0, TEXPREF_ALPHA | TEXPREF_MIPMAP);
					}
				}

				Hunk_FreeToLowMark (mark);
			}

			if (!pheader->gltextures[i][0])
			{
				//johnfitz -- rewritten
				q_snprintf (name, sizeof(name), "%s:frame%i", loadmodel->name, i);
				offset = (src_offset_t)(pskintype+1) - (src_offset_t)mod_base;
				if (Mod_CheckFullbrights ((byte *)(pskintype+1), size))
				{
					pheader->gltextures[i][0] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, texflags | TEXPREF_NOBRIGHT);
					q_snprintf (fbr_mask_name, sizeof(fbr_mask_name), "%s:frame%i_glow", loadmodel->name, i);
					pheader->fbtextures[i][0] = TexMgr_LoadImage (loadmodel, fbr_mask_name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, texflags | TEXPREF_FULLBRIGHT);
				}
				else
				{
					pheader->gltextures[i][0] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype+1), loadmodel->name, offset, texflags);
				}
			}

			pheader->gltextures[i][3] = pheader->gltextures[i][2] = pheader->gltextures[i][1] = pheader->gltextures[i][0];
			pheader->fbtextures[i][3] = pheader->fbtextures[i][2] = pheader->fbtextures[i][1] = pheader->fbtextures[i][0];
			//johnfitz

			pskintype = (daliasskintype_t *)((byte *)(pskintype+1) + size);
		}
		else
		{
			// animating skin group.  yuck.
			pskintype++;
			pinskingroup = (daliasskingroup_t *)pskintype;
			groupskins = LittleLong (pinskingroup->numskins);
			pinskinintervals = (daliasskininterval_t *)(pinskingroup + 1);

			pskintype = (daliasskintype_t *)(pinskinintervals + groupskins);

			for (j=0 ; j<groupskins ; j++)
			{
				Mod_FloodFillSkin( skin, pheader->skinwidth, pheader->skinheight );
				if (j == 0) {
					texels = (byte *) Hunk_AllocName(size, loadname);
					pheader->texels[i] = texels - (byte *)pheader;
					memcpy (texels, (byte *)(pskintype), size);
				}

				//johnfitz -- rewritten
				q_snprintf (name, sizeof(name), "%s:frame%i_%i", loadmodel->name, i,j);
				offset = (src_offset_t)(pskintype) - (src_offset_t)mod_base; //johnfitz
				if (Mod_CheckFullbrights ((byte *)(pskintype), size))
				{
					pheader->gltextures[i][j&3] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype), loadmodel->name, offset, texflags | TEXPREF_NOBRIGHT);
					q_snprintf (fbr_mask_name, sizeof(fbr_mask_name), "%s:frame%i_%i_glow", loadmodel->name, i,j);
					pheader->fbtextures[i][j&3] = TexMgr_LoadImage (loadmodel, fbr_mask_name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype), loadmodel->name, offset, texflags | TEXPREF_FULLBRIGHT);
				}
				else
				{
					pheader->gltextures[i][j&3] = TexMgr_LoadImage (loadmodel, name, pheader->skinwidth, pheader->skinheight,
						SRC_INDEXED, (byte *)(pskintype), loadmodel->name, offset, texflags);
					pheader->fbtextures[i][j&3] = NULL;
				}
				//johnfitz

				pskintype = (daliasskintype_t *)((byte *)(pskintype) + size);
			}
			k = j;
			for (/**/; j < 4; j++)
				pheader->gltextures[i][j&3] = pheader->gltextures[i][j - k];
		}
	}

	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_CalcAliasBounds -- johnfitz -- calculate bounds of alias model for nonrotated, yawrotated, and fullrotated cases
=================
*/
static void Mod_CalcAliasBounds (aliashdr_t *a)
{
	int			i,j,k;
	float		dist, yawradius, radius;
	vec3_t		v;

	//clear out all data
	for (i=0; i<3;i++)
	{
		loadmodel->mins[i] = loadmodel->ymins[i] = loadmodel->rmins[i] = FLT_MAX;
		loadmodel->maxs[i] = loadmodel->ymaxs[i] = loadmodel->rmaxs[i] = -FLT_MAX;
		radius = yawradius = 0;
	}

	//process verts
	for (i=0 ; i<a->numposes; i++)
		for (j=0; j<a->numverts; j++)
		{
			for (k=0; k<3;k++)
				v[k] = poseverts[i][j].v[k] * pheader->scale[k] + pheader->scale_origin[k];

			for (k=0; k<3;k++)
			{
				loadmodel->mins[k] = q_min(loadmodel->mins[k], v[k]);
				loadmodel->maxs[k] = q_max(loadmodel->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}

	//rbounds will be used when entity has nonzero pitch or roll
	radius = sqrt(radius);
	loadmodel->rmins[0] = loadmodel->rmins[1] = loadmodel->rmins[2] = -radius;
	loadmodel->rmaxs[0] = loadmodel->rmaxs[1] = loadmodel->rmaxs[2] = radius;

	//ybounds will be used when entity has nonzero yaw
	yawradius = sqrt(yawradius);
	loadmodel->ymins[0] = loadmodel->ymins[1] = -yawradius;
	loadmodel->ymaxs[0] = loadmodel->ymaxs[1] = yawradius;
	loadmodel->ymins[2] = loadmodel->mins[2];
	loadmodel->ymaxs[2] = loadmodel->maxs[2];
}

static qboolean
nameInList(const char *list, const char *name)
{
	const char *s;
	char tmp[MAX_QPATH];
	int i;

	s = list;

	while (*s)
	{
		// make a copy until the next comma or end of string
		i = 0;
		while (*s && *s != ',')
		{
			if (i < MAX_QPATH - 1)
				tmp[i++] = *s;
			s++;
		}
		tmp[i] = '\0';
		//compare it to the model name
		if (!strcmp(name, tmp))
		{
			return true;
		}
		//search forwards to the next comma or end of string
		while (*s && *s == ',')
			s++;
	}
	return false;
}

/*
=================
Mod_SetExtraFlags -- johnfitz -- set up extra flags that aren't in the mdl
=================
*/
void Mod_SetExtraFlags (qmodel_t *mod)
{
	extern cvar_t r_nolerp_list, r_noshadow_list;

	if (!mod || mod->type != mod_alias)
		return;

	mod->flags &= (0xFF | MF_HOLEY); //only preserve first byte, plus MF_HOLEY

	// nolerp flag
	if (nameInList(r_nolerp_list.string, mod->name))
		mod->flags |= MOD_NOLERP;

	// noshadow flag
	if (nameInList(r_noshadow_list.string, mod->name))
		mod->flags |= MOD_NOSHADOW;

	// fullbright hack (TODO: make this a cvar list)
	if (!strcmp (mod->name, "progs/flame2.mdl") ||
		!strcmp (mod->name, "progs/flame.mdl") ||
		!strcmp (mod->name, "progs/boss.mdl"))
	{
		mod->flags |= MOD_FBRIGHTHACK;
	}
}

/*
=================
Mod_LoadAliasModel
=================
*/
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer)
{
	int					i, j;
	mdl_t				*pinmodel;
	stvert_t			*pinstverts;
	dtriangle_t			*pintriangles;
	int					version, numframes;
	int					size;
	daliasframetype_t	*pframetype;
	daliasskintype_t	*pskintype;

	pinmodel = (mdl_t *)buffer;
	mod_base = (byte *)buffer; //johnfitz

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)",
				 mod->name, version, ALIAS_VERSION);

//
// allocate space for a working header, plus all the data except the frames,
// skin and group info
//
	size	= sizeof(aliashdr_t) +
		 (LittleLong (pinmodel->numframes) - 1) * sizeof (pheader->frames[0]);
	pheader = (aliashdr_t *) Hunk_AllocName (size, loadname);

	mod->flags = LittleLong (pinmodel->flags);

//
// endian-adjust and copy the data, starting with the alias model header
//
	pheader->boundingradius = LittleFloat (pinmodel->boundingradius);
	pheader->numskins = LittleLong (pinmodel->numskins);
	pheader->skinwidth = LittleLong (pinmodel->skinwidth);
	pheader->skinheight = LittleLong (pinmodel->skinheight);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Con_DWarning ("model %s has a skin taller than %d", mod->name,
				   MAX_LBM_HEIGHT);

	pheader->numverts = LittleLong (pinmodel->numverts);

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);
	else if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices (%d; max = %d)", mod->name, pheader->numverts, MAXALIASVERTS);
	else if (pheader->numverts > MAXALIASVERTS_QS && (developer.value || map_checks.value))
		Con_Warning ("model %s vertex count of %d exceeds QS limit of %d\n", mod->name, pheader->numverts, MAXALIASVERTS_QS);

	pheader->numtris = LittleLong (pinmodel->numtris);

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);
	else if (pheader->numtris > MAXALIASTRIS)
		Sys_Error ("model %s has too many triangles (%d; max = %d)", mod->name, pheader->numtris, MAXALIASTRIS);
	else if (pheader->numtris > MAXALIASTRIS_QS && (developer.value || map_checks.value))
		Con_Warning ("model %s triangle count of %d exceeds QS limit of %d\n", mod->name, pheader->numtris, MAXALIASTRIS_QS);

	pheader->numframes = LittleLong (pinmodel->numframes);
	numframes = pheader->numframes;
	if (numframes < 1)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d", numframes);

	pheader->size = LittleFloat (pinmodel->size) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t) LittleLong (pinmodel->synctype);
	mod->numframes = pheader->numframes;

	for (i=0 ; i<3 ; i++)
	{
		pheader->scale[i] = LittleFloat (pinmodel->scale[i]);
		pheader->scale_origin[i] = LittleFloat (pinmodel->scale_origin[i]);
		pheader->eyeposition[i] = LittleFloat (pinmodel->eyeposition[i]);
	}

	VectorCopy(pheader->scale, pheader->original_scale);
	VectorCopy(pheader->scale_origin, pheader->original_scale_origin);

//
// load the skins
//
	pskintype = (daliasskintype_t *)&pinmodel[1];
	pskintype = (daliasskintype_t *) Mod_LoadAllSkins (pheader->numskins, pskintype);

//
// load base s and t vertices
//
	pinstverts = (stvert_t *)pskintype;

	for (i=0 ; i<pheader->numverts ; i++)
	{
		stverts[i].onseam = LittleLong (pinstverts[i].onseam);
		stverts[i].s = LittleLong (pinstverts[i].s);
		stverts[i].t = LittleLong (pinstverts[i].t);
	}

//
// load triangle lists
//
	pintriangles = (dtriangle_t *)&pinstverts[pheader->numverts];

	for (i=0 ; i<pheader->numtris ; i++)
	{
		triangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		for (j=0 ; j<3 ; j++)
		{
			triangles[i].vertindex[j] =
					LittleLong (pintriangles[i].vertindex[j]);
		}
	}

//
// load the frames
//
	posenum = 0;
	pframetype = (daliasframetype_t *)&pintriangles[pheader->numtris];

	for (i=0 ; i<numframes ; i++)
	{
		aliasframetype_t	frametype;
		frametype = (aliasframetype_t) LittleLong (pframetype->type);
		if (frametype == ALIAS_SINGLE)
			pframetype = (daliasframetype_t *) Mod_LoadAliasFrame (pframetype + 1, &pheader->frames[i]);
		else
			pframetype = (daliasframetype_t *) Mod_LoadAliasGroup (pframetype + 1, &pheader->frames[i]);
	}

	pheader->numposes = posenum;
	pheader->poseverttype = ALIAS_POSE_MDL;

	mod->type = mod_alias;

	Mod_SetExtraFlags (mod); //johnfitz

	Mod_CalcAliasBounds (pheader); //johnfitz

	//
	// build the draw lists
	//
	GL_MakeAliasModelDisplayLists (mod, pheader);

	GLMesh_LoadVertexBuffer (mod, pheader);
	Mod_RegisterAliasBuild (pheader, false, vec3_origin, vec3_origin);
}

/*
==============================================================================

					MD3 MODEL LOADING

==============================================================================
*/

#define IDMD3HEADER	(('I' << 0) | ('D' << 8) | ('P' << 16) | ('3' << 24))
#define MAX_MD3_TRIANGLES	1048576
#define MAX_MD3_DECODED_BYTES	(64u * 1024u * 1024u)

typedef struct md3header_s
{
	int	ident;
	int	version;
	char	name[64];
	int	flags;
	int	numframes;
	int	numtags;
	int	numsurfaces;
	int	numskins;
	int	ofsframes;
	int	ofstags;
	int	ofssurfaces;
	int	ofsend;
} md3header_t;

typedef struct md3frame_s
{
	vec3_t	bounds[2];
	vec3_t	localorigin;
	float	radius;
	char	name[16];
} md3frame_t;

typedef struct md3surface_s
{
	int	ident;
	char	name[64];
	int	flags;
	int	numframes;
	int	numshaders;
	int	numverts;
	int	numtriangles;
	int	ofstriangles;
	int	ofsshaders;
	int	ofsst;
	int	ofsxyznormals;
	int	ofsend;
} md3surface_t;

typedef struct md3triangle_s
{
	int	indexes[3];
} md3triangle_t;

typedef struct md3st_s
{
	float	s;
	float	t;
} md3st_t;

typedef struct md3shader_s
{
	char	name[64];
	int	shaderindex;
} md3shader_t;

static qboolean Mod_MD3Range (size_t filesize, size_t offset, size_t size)
{
	return offset <= filesize && size <= filesize - offset;
}

static qboolean Mod_MD3Multiply (size_t a, size_t b, size_t *result)
{
	if (a && b > SIZE_MAX / a)
		return false;
	*result = a * b;
	return true;
}

static qboolean Mod_MD3AddDecodedBytes (size_t *total, size_t bytes)
{
	if (bytes > MAX_MD3_DECODED_BYTES || *total > MAX_MD3_DECODED_BYTES - bytes)
		return false;
	*total += bytes;
	return true;
}

static qboolean Mod_MD3SurfaceRange (int surface_size, int offset, size_t size)
{
	return offset >= 0 && (size_t)offset <= (size_t)surface_size &&
		size <= (size_t)surface_size - (size_t)offset;
}

static qboolean Mod_MD3Warning (const qmodel_t *mod, const char *reason)
{
	Con_Warning ("MD3: %s: %s\n", mod->name, reason);
	return false;
}

static void Mod_MD3CopyName (char *out, size_t outsize, const char *in, size_t insize)
{
	size_t len = 0;

	while (len < insize && in[len])
		len++;
	if (len >= outsize)
		len = outsize - 1;
	memcpy (out, in, len);
	out[len] = '\0';
}

static char *Mod_LoadMD3SkinFile (qmodel_t *mod, const char *name)
{
	unsigned int path_id;
	char *contents;

	if (!COM_FileExists (name, &path_id) || path_id < mod->path_id)
		return NULL;

	contents = (char *)COM_LoadMallocFile (name, &path_id);
	if (!contents || path_id < mod->path_id)
	{
		free (contents);
		return NULL;
	}

	return contents;
}

static int Mod_LoadMD3SkinFiles (qmodel_t *mod, char *skinfiles[MAX_SKINS])
{
	char base[MAX_QPATH], filename[MAX_QPATH];
	int i, count = 0;

	COM_StripExtension (mod->name, base, sizeof(base));
	for (i = 0; i < MAX_SKINS; i++)
	{
		q_snprintf (filename, sizeof(filename), "%s.md3_%d.skin", base, i);
		skinfiles[i] = Mod_LoadMD3SkinFile (mod, filename);
		if (!skinfiles[i])
		{
			q_snprintf (filename, sizeof(filename), "%s_%d.skin", base, i);
			skinfiles[i] = Mod_LoadMD3SkinFile (mod, filename);
		}
		if (!skinfiles[i] && i == 0)
		{
			q_snprintf (filename, sizeof(filename), "%s.md3.skin", base);
			skinfiles[i] = Mod_LoadMD3SkinFile (mod, filename);
			if (!skinfiles[i])
			{
				q_snprintf (filename, sizeof(filename), "%s.skin", base);
				skinfiles[i] = Mod_LoadMD3SkinFile (mod, filename);
			}
		}
		if (skinfiles[i])
			count = i + 1;
	}

	return count;
}

static qboolean Mod_MD3SkinShaderForSurface (const char *skinfile,
	const char *surfacename, char *out, size_t outsize)
{
	const char *line = skinfile;

	while (*line)
	{
		const char *end = line;
		const char *comma;
		const char *left, *right;
		size_t leftlen, rightlen;

		while (*end && *end != '\n')
			end++;
		comma = (const char *)memchr (line, ',', (size_t)(end - line));
		if (comma)
		{
			left = line;
			while (left < comma && (*left == ' ' || *left == '\t'))
				left++;
			leftlen = (size_t)(comma - left);
			while (leftlen && (left[leftlen - 1] == ' ' || left[leftlen - 1] == '\t'))
				leftlen--;
			if (strlen(surfacename) == leftlen && !q_strncasecmp (left, surfacename, leftlen))
			{
				right = comma + 1;
				while (right < end && (*right == ' ' || *right == '\t'))
					right++;
				rightlen = (size_t)(end - right);
				while (rightlen && (right[rightlen - 1] == '\r' || right[rightlen - 1] == ' ' || right[rightlen - 1] == '\t'))
					rightlen--;
				if (rightlen && rightlen < outsize)
				{
					memcpy (out, right, rightlen);
					out[rightlen] = '\0';
					return true;
				}
			}
		}
		line = *end ? end + 1 : end;
	}

	return false;
}

static byte *Mod_LoadMD3ImageCandidate (qmodel_t *mod, const char *name,
	int *width, int *height)
{
	return Image_LoadImageWithPath (name, width, height, mod->path_id);
}

static byte *Mod_LoadMD3Image (qmodel_t *mod, const char *input,
	char *usedname, size_t usednamesize, int *width, int *height)
{
	char stripped[MAX_QPATH], directory[MAX_QPATH], candidates[4][MAX_QPATH];
	char *slash;
	byte *data;
	int i, count = 0;

	COM_StripExtension (input, stripped, sizeof(stripped));
	for (i = 0; stripped[i]; i++)
		if (stripped[i] == '\\')
			stripped[i] = '/';

	if (strchr(stripped, '/'))
		q_strlcpy (candidates[count++], stripped, sizeof(candidates[0]));
	else
	{
		COM_StripExtension (mod->name, directory, sizeof(directory));
		slash = strrchr (directory, '/');
		if (slash)
		{
			slash[1] = '\0';
			q_snprintf (candidates[count++], sizeof(candidates[0]), "%s%s", directory, stripped);
		}
		q_snprintf (candidates[count++], sizeof(candidates[0]), "progs/%s", stripped);
		q_snprintf (candidates[count++], sizeof(candidates[0]), "textures/%s", stripped);
		q_strlcpy (candidates[count++], stripped, sizeof(candidates[0]));
	}

	for (i = 0; i < count; i++)
	{
		data = Mod_LoadMD3ImageCandidate (mod, candidates[i], width, height);
		if (data)
		{
			q_strlcpy (usedname, candidates[i], usednamesize);
			return data;
		}
	}

	usedname[0] = '\0';
	return NULL;
}

/* Image_LoadImage* returns RGBA data. Do not retain alpha-test state for an
 * opaque true-colour replacement skin. */
static unsigned Mod_TrueColorTextureFlags (const byte *data, int width, int height,
	unsigned flags)
{
	size_t i, pixels;

	if (!(flags & TEXPREF_ALPHA) || !data || width <= 0 || height <= 0)
		return flags;
	if ((size_t)width > SIZE_MAX / (size_t)height)
		return flags;
	pixels = (size_t)width * (size_t)height;
	for (i = 0; i < pixels; i++)
		if (data[i * 4 + 3] != 255)
			return flags;
	return flags & ~TEXPREF_ALPHA;
}

static void Mod_LoadMD3SurfaceTexture (qmodel_t *mod, aliashdr_t *surface,
	int skin, const char *name)
{
	char basename[MAX_QPATH], glowname[MAX_QPATH];
	byte *data;
	int width, height;
	int mark;

	mark = Hunk_LowMark ();
	data = Mod_LoadMD3Image (mod, name, basename, sizeof(basename), &width, &height);
	if (!data)
	{
		Hunk_FreeToLowMark (mark);
		return;
	}

	surface->gltextures[skin][0] = TexMgr_LoadImage (mod, basename, width, height,
		SRC_RGBA, data, basename, 0,
		Mod_TrueColorTextureFlags (data, width, height, TEXPREF_ALPHA | TEXPREF_MIPMAP));
	if (surface->gltextures[skin][0])
	{
		surface->skinwidth = width;
		surface->skinheight = height;
	}
	Hunk_FreeToLowMark (mark);

	mark = Hunk_LowMark ();
	q_snprintf (glowname, sizeof(glowname), "%s_glow", basename);
	data = Mod_LoadMD3ImageCandidate (mod, glowname, &width, &height);
	if (!data)
	{
		q_snprintf (glowname, sizeof(glowname), "%s_luma", basename);
		data = Mod_LoadMD3ImageCandidate (mod, glowname, &width, &height);
	}
	if (data)
	{
		Mod_NormalizeExternalFullbright (data, width, height);
		surface->fbtextures[skin][0] = TexMgr_LoadImage (mod, glowname, width, height,
			SRC_RGBA, data, glowname, 0,
			Mod_TrueColorTextureFlags (data, width, height, TEXPREF_ALPHA | TEXPREF_MIPMAP));
	}
	Hunk_FreeToLowMark (mark);

	surface->gltextures[skin][1] = surface->gltextures[skin][2] = surface->gltextures[skin][3] = surface->gltextures[skin][0];
	surface->fbtextures[skin][1] = surface->fbtextures[skin][2] = surface->fbtextures[skin][3] = surface->fbtextures[skin][0];
}

static void Mod_SetMD3Bounds (qmodel_t *mod, const vec3_t mins, const vec3_t maxs)
{
	float yawradius = 0.0f, radius = 0.0f;
	vec3_t v;
	int i, j, k;

	VectorCopy (mins, mod->mins);
	VectorCopy (maxs, mod->maxs);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++)
			for (k = 0; k < 2; k++)
			{
				v[0] = i ? maxs[0] : mins[0];
				v[1] = j ? maxs[1] : mins[1];
				v[2] = k ? maxs[2] : mins[2];
				yawradius = q_max (yawradius, v[0] * v[0] + v[1] * v[1]);
				radius = q_max (radius, yawradius + v[2] * v[2]);
			}
	yawradius = sqrtf (yawradius);
	radius = sqrtf (radius);
	mod->ymins[0] = mod->ymins[1] = -yawradius;
	mod->ymaxs[0] = mod->ymaxs[1] = yawradius;
	mod->ymins[2] = mins[2];
	mod->ymaxs[2] = maxs[2];
	mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = -radius;
	mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = radius;
}

/*
===============
Mod_LoadMD3Model

The file is fully bounds-checked before allocating hunk data. A malformed
optional replacement therefore cleanly leaves its source MDL intact.
===============
*/
static qboolean Mod_LoadMD3Model (qmodel_t *mod, const byte *buffer, size_t filesize)
{
	const md3header_t *inheader;
	const md3frame_t *inframes;
	const md3surface_t *insurface;
	aliashdr_t *surfaces[MAX_MD3_SURFACES];
	char *skinfiles[MAX_SKINS] = {NULL};
	char modelbase[MAX_QPATH];
	vec3_t mins, maxs;
	int numframes, numsurfaces, numskins;
	int surfaceofs, surf, frame, vert, tri, skin, k;
	size_t bytes, decodedbytes = 0;

	if (filesize < sizeof(*inheader))
		return Mod_MD3Warning (mod, "file is shorter than its header");

	inheader = (const md3header_t *)buffer;
	if (LittleLong (inheader->ident) != IDMD3HEADER)
		return Mod_MD3Warning (mod, "bad header ident");
	if (LittleLong (inheader->version) != MD3_VERSION)
		return Mod_MD3Warning (mod, "unsupported version");

	numframes = LittleLong (inheader->numframes);
	numsurfaces = LittleLong (inheader->numsurfaces);
	if (numframes < 1 || numframes > MAXALIASFRAMES)
		return Mod_MD3Warning (mod, "invalid frame count");
	if (numsurfaces < 1 || numsurfaces > MAX_MD3_SURFACES)
		return Mod_MD3Warning (mod, "invalid surface count");
	if (!Mod_MD3Multiply ((size_t)numframes, sizeof(*inframes), &bytes) ||
		LittleLong (inheader->ofsframes) < 0 ||
		!Mod_MD3Range (filesize, (size_t)LittleLong (inheader->ofsframes), bytes))
		return Mod_MD3Warning (mod, "invalid frame range");

	surfaceofs = LittleLong (inheader->ofssurfaces);
	if (surfaceofs < 0 || !Mod_MD3Range (filesize, (size_t)surfaceofs, sizeof(*insurface)))
		return Mod_MD3Warning (mod, "invalid surface range");

	/* Validate every variable-length source span and every triangle first. */
	for (surf = 0; surf < numsurfaces; surf++)
	{
		int surfaceend, surfaceframes, surfaceverts, surfacetriangles, surfaceshaders;
		const md3triangle_t *triangles_in;
		size_t surfacebytes = 0;

		if (!Mod_MD3Range (filesize, (size_t)surfaceofs, sizeof(*insurface)))
			return Mod_MD3Warning (mod, "truncated surface header");
		insurface = (const md3surface_t *)(buffer + surfaceofs);
		if (LittleLong (insurface->ident) != IDMD3HEADER)
			return Mod_MD3Warning (mod, "bad surface ident");
		surfaceend = LittleLong (insurface->ofsend);
		if (surfaceend < (int)sizeof(*insurface) || !Mod_MD3Range (filesize, (size_t)surfaceofs, (size_t)surfaceend))
			return Mod_MD3Warning (mod, "invalid surface size");
		surfaceframes = LittleLong (insurface->numframes);
		surfaceverts = LittleLong (insurface->numverts);
		surfacetriangles = LittleLong (insurface->numtriangles);
		surfaceshaders = LittleLong (insurface->numshaders);
		if (surfaceframes != numframes || surfaceverts < 1 || surfaceverts > MAX_MD3_VERTICES ||
			surfacetriangles < 1 || surfacetriangles > MAX_MD3_TRIANGLES || surfaceshaders < 0)
			return Mod_MD3Warning (mod, "invalid surface counts");
		if (!Mod_MD3Multiply ((size_t)surfacetriangles, sizeof(*triangles_in), &bytes) ||
			!Mod_MD3SurfaceRange (surfaceend, LittleLong(insurface->ofstriangles), bytes) ||
			!Mod_MD3Multiply ((size_t)surfaceshaders, sizeof(md3shader_t), &bytes) ||
			!Mod_MD3SurfaceRange (surfaceend, LittleLong(insurface->ofsshaders), bytes) ||
			!Mod_MD3Multiply ((size_t)surfaceverts, sizeof(md3st_t), &bytes) ||
			!Mod_MD3SurfaceRange (surfaceend, LittleLong(insurface->ofsst), bytes) ||
			!Mod_MD3Multiply ((size_t)numframes * (size_t)surfaceverts, sizeof(md3vertex_t), &bytes) ||
			!Mod_MD3SurfaceRange (surfaceend, LittleLong(insurface->ofsxyznormals), bytes))
			return Mod_MD3Warning (mod, "invalid surface data range");

		/* Bound the persistent decoded payload before any Hunk allocation. */
		if (!Mod_MD3Multiply ((size_t)(numframes - 1), sizeof(maliasframedesc_t), &bytes) ||
			!Mod_MD3AddDecodedBytes (&surfacebytes, sizeof(aliashdr_t)) ||
			!Mod_MD3AddDecodedBytes (&surfacebytes, bytes) ||
			!Mod_MD3Multiply ((size_t)numframes * (size_t)surfaceverts, sizeof(md3vertex_t), &bytes) ||
			!Mod_MD3AddDecodedBytes (&surfacebytes, bytes) ||
			!Mod_MD3Multiply ((size_t)surfacetriangles * 3, sizeof(unsigned short), &bytes) ||
			!Mod_MD3AddDecodedBytes (&surfacebytes, bytes) ||
			!Mod_MD3Multiply ((size_t)surfaceverts, sizeof(meshst_t), &bytes) ||
			!Mod_MD3AddDecodedBytes (&surfacebytes, bytes) ||
			!Mod_MD3AddDecodedBytes (&decodedbytes, surfacebytes))
			return Mod_MD3Warning (mod, "decoded data exceeds 64 MiB limit");

		triangles_in = (const md3triangle_t *)((const byte *)insurface + LittleLong(insurface->ofstriangles));
		for (tri = 0; tri < surfacetriangles; tri++)
			for (k = 0; k < 3; k++)
				if (LittleLong(triangles_in[tri].indexes[k]) < 0 ||
					LittleLong(triangles_in[tri].indexes[k]) >= surfaceverts)
					return Mod_MD3Warning (mod, "triangle index outside surface");

		if (surfaceofs > INT_MAX - surfaceend)
			return Mod_MD3Warning (mod, "surface offset overflow");
		surfaceofs += surfaceend;
	}

	inframes = (const md3frame_t *)(buffer + LittleLong(inheader->ofsframes));
	COM_StripExtension (mod->name, modelbase, sizeof(modelbase));
	numskins = Mod_LoadMD3SkinFiles (mod, skinfiles);
	if (!numskins)
		numskins = 1;

	for (k = 0; k < 3; k++)
		mins[k] = FLT_MAX, maxs[k] = -FLT_MAX;
	surfaceofs = LittleLong (inheader->ofssurfaces);
	for (surf = 0; surf < numsurfaces; surf++)
	{
		const md3vertex_t *vertices_in;
		const md3triangle_t *triangles_in;
		const md3st_t *st_in;
		const md3shader_t *shaders_in;
		aliashdr_t *out;
		md3vertex_t *vertices_out;
		meshst_t *st_out;
		unsigned short *indexes_out;
		char surfacename[65], texturename[MAX_QPATH], shadername[65];
		int surfaceverts, surfacetriangles, surfaceshaders, surfaceend;
		size_t headersize;

		insurface = (const md3surface_t *)(buffer + surfaceofs);
		surfaceverts = LittleLong (insurface->numverts);
		surfacetriangles = LittleLong (insurface->numtriangles);
		surfaceshaders = LittleLong (insurface->numshaders);
		surfaceend = LittleLong (insurface->ofsend);
		headersize = sizeof(*out) + (size_t)(numframes - 1) * sizeof(out->frames[0]);
		out = (aliashdr_t *)Hunk_AllocName ((int)headersize, loadname);
		surfaces[surf] = out;
		out->poseverttype = ALIAS_POSE_MD3;
		out->numframes = out->numposes = numframes;
		out->numverts = out->numverts_vbo = surfaceverts;
		out->numtris = surfacetriangles;
		out->numindexes = surfacetriangles * 3;
		out->numskins = numskins;
		out->skinwidth = out->skinheight = 1;
		out->scale[0] = out->scale[1] = out->scale[2] = MD3_XYZ_SCALE;
		VectorCopy (out->scale, out->original_scale);
		VectorCopy (out->scale_origin, out->original_scale_origin);

		for (frame = 0; frame < numframes; frame++)
		{
			out->frames[frame].firstpose = frame;
			out->frames[frame].numposes = 1;
			out->frames[frame].interval = 0.1f;
			Mod_MD3CopyName (out->frames[frame].name, sizeof(out->frames[frame].name),
				inframes[frame].name, sizeof(inframes[frame].name));
		}

		vertices_in = (const md3vertex_t *)((const byte *)insurface + LittleLong(insurface->ofsxyznormals));
		vertices_out = (md3vertex_t *)Hunk_Alloc ((int)((size_t)numframes * (size_t)surfaceverts * sizeof(*vertices_out)));
		out->vertexes = (intptr_t)((byte *)vertices_out - (byte *)out);
		for (frame = 0; frame < numframes; frame++)
			for (vert = 0; vert < surfaceverts; vert++)
			{
				md3vertex_t *dst = &vertices_out[frame * surfaceverts + vert];
				const md3vertex_t *src = &vertices_in[frame * surfaceverts + vert];
				for (k = 0; k < 3; k++)
				{
					dst->xyz[k] = LittleShort (src->xyz[k]);
					mins[k] = q_min (mins[k], dst->xyz[k] * MD3_XYZ_SCALE);
					maxs[k] = q_max (maxs[k], dst->xyz[k] * MD3_XYZ_SCALE);
				}
				dst->latlong[0] = src->latlong[0];
				dst->latlong[1] = src->latlong[1];
			}

		triangles_in = (const md3triangle_t *)((const byte *)insurface + LittleLong(insurface->ofstriangles));
		indexes_out = (unsigned short *)Hunk_Alloc ((int)((size_t)out->numindexes * sizeof(*indexes_out)));
		out->indexes = (intptr_t)((byte *)indexes_out - (byte *)out);
		for (tri = 0; tri < surfacetriangles; tri++)
			for (k = 0; k < 3; k++)
				indexes_out[tri * 3 + k] = (unsigned short)LittleLong(triangles_in[tri].indexes[k]);

		st_in = (const md3st_t *)((const byte *)insurface + LittleLong(insurface->ofsst));
		st_out = (meshst_t *)Hunk_Alloc ((int)((size_t)surfaceverts * sizeof(*st_out)));
		out->meshdesc = (intptr_t)((byte *)st_out - (byte *)out);
		for (vert = 0; vert < surfaceverts; vert++)
		{
			st_out[vert].st[0] = LittleFloat (st_in[vert].s);
			st_out[vert].st[1] = LittleFloat (st_in[vert].t);
		}

		Mod_MD3CopyName (surfacename, sizeof(surfacename), insurface->name, sizeof(insurface->name));
		shaders_in = (const md3shader_t *)((const byte *)insurface + LittleLong(insurface->ofsshaders));
		for (skin = 0; skin < numskins; skin++)
		{
			texturename[0] = '\0';
			if (skinfiles[skin])
				Mod_MD3SkinShaderForSurface (skinfiles[skin], surfacename, texturename, sizeof(texturename));
			if (!texturename[0] && skin < surfaceshaders)
			{
				Mod_MD3CopyName (shadername, sizeof(shadername), shaders_in[skin].name, sizeof(shaders_in[skin].name));
				q_strlcpy (texturename, shadername, sizeof(texturename));
			}
			if (!texturename[0])
				q_snprintf (texturename, sizeof(texturename), "%s_%d", modelbase, skin);
			if (q_strcasecmp (texturename, "*off"))
				Mod_LoadMD3SurfaceTexture (mod, out, skin, texturename);
		}

		surfaceofs += surfaceend;
	}

	for (surf = 0; surf < numsurfaces - 1; surf++)
		surfaces[surf]->nextsurface = (intptr_t)((byte *)surfaces[surf + 1] - (byte *)surfaces[surf]);

	for (skin = 0; skin < MAX_SKINS; skin++)
		free (skinfiles[skin]);

	mod->type = mod_alias;
	mod->numframes = numframes;
	mod->flags = LittleLong (inheader->flags);
	Mod_SetExtraFlags (mod);
	Mod_SetMD3Bounds (mod, mins, maxs);
	Mod_RegisterAliasBuild (surfaces[0], true, mins, maxs);
	GLMesh_LoadVertexBuffer (mod, surfaces[0]);
	return true;
}

/*
==============================================================================

					MD5 MODEL LOADING

The official 2021 rerelease uses a constrained MD5 v10 dialect for its
enhanced id1 models.  The renderer below stores a CPU-baked pose stream rather
than importing a second skeletal GPU path: it keeps the loader compatible with
OpenVR's fixed-function renderer while still providing genuine animated MD5
models and ordinary alias-frame interpolation at draw time.
==============================================================================
*/

#define MAX_MD5_VERTICES		65535
#define MAX_MD5_TRIANGLES		1048576
#define MAX_MD5_WEIGHTS		1048576
#define MAX_MD5_ANIM_COMPONENTS	65536
#define MAX_MD5_DECODED_BYTES		(64u * 1024u * 1024u)

typedef struct md5joint_s
{
	char	name[32];
	int	parent;
	float	bind[12];
} md5joint_t;

typedef struct md5vertinfo_s
{
	size_t	firstweight;
	size_t	count;
	float	st[2];
} md5vertinfo_t;

typedef struct md5weight_s
{
	int	joint;
	vec4_t	position;
} md5weight_t;

typedef struct md5parser_s
{
	const char	*cursor;
	const char	*error;
} md5parser_t;

typedef struct md5animbase_s
{
	unsigned int	flags;
	size_t		offset;
} md5animbase_t;

static qboolean Mod_MD5Warning (const qmodel_t *mod, const char *reason)
{
	Con_Warning ("MD5: %s: %s\n", mod->name, reason);
	return false;
}

static qboolean Mod_MD5Fail (md5parser_t *parser, const char *error)
{
	parser->error = error;
	return false;
}

static qboolean Mod_MD5Token (md5parser_t *parser)
{
	parser->cursor = COM_Parse (parser->cursor);
	if (!parser->cursor)
		return Mod_MD5Fail (parser, "truncated text data");
	return true;
}

static qboolean Mod_MD5Expect (md5parser_t *parser, const char *expected)
{
	if (!Mod_MD5Token (parser))
		return false;
	if (strcmp (com_token, expected))
		return Mod_MD5Fail (parser, "unexpected token");
	return true;
}

static qboolean Mod_MD5Size (md5parser_t *parser, size_t *value)
{
	char *end;
	unsigned long long parsed;

	if (!Mod_MD5Token (parser))
		return false;
	parsed = strtoull (com_token, &end, 10);
	if (end == com_token || *end || parsed > SIZE_MAX)
		return Mod_MD5Fail (parser, "invalid unsigned number");
	*value = (size_t)parsed;
	return true;
}

static qboolean Mod_MD5Int (md5parser_t *parser, int *value)
{
	char *end;
	long parsed;

	if (!Mod_MD5Token (parser))
		return false;
	parsed = strtol (com_token, &end, 10);
	if (end == com_token || *end || parsed < INT_MIN || parsed > INT_MAX)
		return Mod_MD5Fail (parser, "invalid signed number");
	*value = (int)parsed;
	return true;
}

static qboolean Mod_MD5Float (md5parser_t *parser, float *value)
{
	char *end;
	double parsed;

	if (!Mod_MD5Token (parser))
		return false;
	parsed = strtod (com_token, &end);
	if (end == com_token || *end || !isfinite(parsed) ||
		parsed < -FLT_MAX || parsed > FLT_MAX)
		return Mod_MD5Fail (parser, "invalid float");
	*value = (float)parsed;
	return true;
}

static qboolean Mod_MD5Vec3 (md5parser_t *parser, vec3_t value)
{
	return Mod_MD5Expect (parser, "(") &&
		Mod_MD5Float (parser, &value[0]) &&
		Mod_MD5Float (parser, &value[1]) &&
		Mod_MD5Float (parser, &value[2]) &&
		Mod_MD5Expect (parser, ")");
}

static void Mod_MD5QuaternionW (vec4_t quaternion)
{
	float w2 = 1.0f - DotProduct (quaternion, quaternion);
	quaternion[3] = -sqrtf (q_max (0.0f, w2));
}

static void Mod_MD5Matrix (const vec3_t pos, const vec4_t quat, float out[12])
{
	const float x2 = quat[0] + quat[0];
	const float y2 = quat[1] + quat[1];
	const float z2 = quat[2] + quat[2];
	const float xx = quat[0] * x2;
	const float xy = quat[0] * y2;
	const float xz = quat[0] * z2;
	const float yy = quat[1] * y2;
	const float yz = quat[1] * z2;
	const float zz = quat[2] * z2;
	const float xw = quat[3] * x2;
	const float yw = quat[3] * y2;
	const float zw = quat[3] * z2;

	/*
	 * MD5 matrices are row-major 3x4 transforms.  Keep this layout aligned
	 * with R_ConcatTransforms and the vkQuake/Ironwail MD5 loaders: the
	 * opposite cross-term arrangement is the transpose/inverse rotation and
	 * puts rerelease bind and animation joints in the wrong coordinate frame.
	 */
	out[0] = 1.0f - (yy + zz);
	out[1] = xy - zw;
	out[2] = xz + yw;
	out[3] = pos[0];
	out[4] = xy + zw;
	out[5] = 1.0f - (xx + zz);
	out[6] = yz - xw;
	out[7] = pos[1];
	out[8] = xz - yw;
	out[9] = yz + xw;
	out[10] = 1.0f - (xx + yy);
	out[11] = pos[2];
}

static void Mod_MD5Transform4 (const float matrix[12], const vec4_t input, vec3_t output)
{
	output[0] = matrix[0] * input[0] + matrix[1] * input[1] + matrix[2] * input[2] + matrix[3] * input[3];
	output[1] = matrix[4] * input[0] + matrix[5] * input[1] + matrix[6] * input[2] + matrix[7] * input[3];
	output[2] = matrix[8] * input[0] + matrix[9] * input[1] + matrix[10] * input[2] + matrix[11] * input[3];
}

static qboolean Mod_MD5AddBytes (size_t *total, size_t count, size_t element)
{
	if (count && element > SIZE_MAX / count)
		return false;
	count *= element;
	if (count > MAX_MD5_DECODED_BYTES || *total > MAX_MD5_DECODED_BYTES - count)
		return false;
	*total += count;
	return true;
}

/*
 * MD5 does not carry normals.  Match the rerelease loaders by welding UV
 * split vertices that occupy the same position before accumulating them.
 * This keeps lighting continuous across texture seams without changing the
 * mesh's UV/index layout used by the renderer.
 */
static unsigned int Mod_MD5HashVertex (const vec3_t vertex)
{
	unsigned int hash = 2166136261u;
	int i;

	for (i = 0; i < 3; i++)
	{
		const float value = vertex[i] == 0.0f ? 0.0f : vertex[i];
		const byte *bytes = (const byte *)&value;
		int byteindex;

		for (byteindex = 0; byteindex < (int)sizeof(value); byteindex++)
		{
			hash ^= bytes[byteindex];
			hash *= 16777619u;
		}
	}

	return hash;
}

static qboolean Mod_MD5VertexEqual (const vec3_t first, const vec3_t second)
{
	return first[0] == second[0] && first[1] == second[1] && first[2] == second[2];
}

static unsigned short *Mod_MD5BuildWeldTable (const md5vertex_t *vertices,
	int numverts)
{
	size_t hashsize;
	int *hashmap;
	unsigned short *weld;
	int vert;

	if (numverts < 1)
		return NULL;

	hashsize = (size_t)numverts * 2;
	hashmap = (int *)calloc (hashsize, sizeof(*hashmap));
	weld = (unsigned short *)malloc ((size_t)numverts * sizeof(*weld));
	if (!hashmap || !weld)
	{
		free (weld);
		free (hashmap);
		return NULL;
	}

	for (vert = 0; vert < numverts; vert++)
	{
		size_t slot = Mod_MD5HashVertex (vertices[vert].xyz) % hashsize;
		const size_t firstslot = slot;

		weld[vert] = (unsigned short)vert;
		do
		{
			if (!hashmap[slot])
			{
				hashmap[slot] = vert + 1;
				break;
			}

			if (Mod_MD5VertexEqual (vertices[hashmap[slot] - 1].xyz,
				vertices[vert].xyz))
			{
				weld[vert] = weld[hashmap[slot] - 1];
				break;
			}

			slot++;
			if (slot == hashsize)
				slot = 0;
		} while (slot != firstslot);
	}

	free (hashmap);
	return weld;
}

static void Mod_MD5ComputeNormals (md5vertex_t *vertices, int numverts,
	const unsigned short *indexes, int numindexes)
{
	unsigned short *weld;
	int i;

	for (i = 0; i < numverts; i++)
		vertices[i].normal[0] = vertices[i].normal[1] = vertices[i].normal[2] = 0.0f;
	weld = Mod_MD5BuildWeldTable (vertices, numverts);

	for (i = 0; i < numindexes; i += 3)
	{
		const int i0 = weld ? weld[indexes[i + 0]] : indexes[i + 0];
		const int i1 = weld ? weld[indexes[i + 1]] : indexes[i + 1];
		const int i2 = weld ? weld[indexes[i + 2]] : indexes[i + 2];
		md5vertex_t *v0 = &vertices[i0];
		md5vertex_t *v1 = &vertices[i1];
		md5vertex_t *v2 = &vertices[i2];
		vec3_t d1, d2, normal;

		VectorSubtract (v1->xyz, v0->xyz, d1);
		VectorSubtract (v2->xyz, v0->xyz, d2);
		/* MD5 uses the same triangle winding as the QSS rerelease loader. */
		CrossProduct (d1, d2, normal);
		VectorAdd (v0->normal, normal, v0->normal);
		VectorAdd (v1->normal, normal, v1->normal);
		VectorAdd (v2->normal, normal, v2->normal);
	}

	for (i = 0; i < numverts; i++)
	{
		if (!weld || weld[i] == i)
		{
			if (VectorNormalize (vertices[i].normal) == 0.0f)
			{
				vertices[i].normal[0] = 0.0f;
				vertices[i].normal[1] = 0.0f;
				vertices[i].normal[2] = 1.0f;
			}
		}
		else
			VectorCopy (vertices[weld[i]].normal, vertices[i].normal);
	}

	free (weld);
}

static void Mod_MD5BuildSkinnedPose (md5vertex_t *output, int numverts,
	const md5vertinfo_t *vertinfo, const md5weight_t *weights,
	const float *matrices)
{
	int vert;

	for (vert = 0; vert < numverts; vert++)
	{
		const md5vertinfo_t *info = &vertinfo[vert];
		int weight;

		output[vert].xyz[0] = output[vert].xyz[1] = output[vert].xyz[2] = 0.0f;
		output[vert].st[0] = info->st[0];
		output[vert].st[1] = info->st[1];
		for (weight = 0; weight < (int)info->count; weight++)
		{
			const md5weight_t *source = &weights[info->firstweight + weight];
			vec3_t transformed;

			Mod_MD5Transform4 (matrices + (size_t)source->joint * 12,
				source->position, transformed);
			VectorAdd (output[vert].xyz, transformed, output[vert].xyz);
		}
	}
}

static byte *Mod_MD5LoadImage (qmodel_t *mod, const char *shader,
	const char *suffix, char *usedname, size_t usednamesize,
	int *width, int *height)
{
	static const char *const prefixes[] = {"", "progs/", "textures/"};
	byte *data;
	int i;

	/* The verified VRIK avatar must not acquire a same-named mod texture.
	 * Its official indexed skin is loaded through the source-restricted LMP
	 * fallback below. */
	if (mod_md5_rerelease_only)
	{
		usedname[0] = '\0';
		return NULL;
	}

	for (i = 0; i < (int)Q_COUNTOF(prefixes); i++)
	{
		q_snprintf (usedname, usednamesize, "%s%s%s", prefixes[i], shader, suffix);
		data = Image_LoadImageWithPath (usedname, width, height, mod->path_id);
		if (data)
			return data;
	}

	usedname[0] = '\0';
	return NULL;
}

static byte *Mod_MD5LoadSkinImage (qmodel_t *mod, const char *shader,
	int skin, int frame, const char *suffix, char *usedname,
	size_t usednamesize, int *width, int *height)
{
	char framesuffix[16];

	q_snprintf (framesuffix, sizeof(framesuffix), "_%02d_%02d%s", skin, frame, suffix);
	return Mod_MD5LoadImage (mod, shader, framesuffix, usedname, usednamesize,
		width, height);
}

static byte *Mod_MD5LoadLMP (qmodel_t *mod, const char *shader,
	const char *suffix, char *filename, size_t filenamesize,
	unsigned int *path_id)
{
	static const char *const prefixes[] = {"", "progs/", "textures/"};
	byte *data;
	int i;

	for (i = 0; i < (int)Q_COUNTOF(prefixes); i++)
	{
		q_snprintf (filename, filenamesize, "%s%s%s.lmp", prefixes[i], shader, suffix);
		data = mod_md5_rerelease_only ?
			COM_LoadMallocFileFromRerelease (filename, path_id) :
			COM_LoadMallocFile (filename, path_id);
		if (data && *path_id >= mod->path_id)
			return data;
		free (data);
	}

	filename[0] = '\0';
	return NULL;
}

static byte *Mod_MD5LoadSkinLMP (qmodel_t *mod, const char *shader,
	int skin, int frame, char *filename, size_t filenamesize,
	unsigned int *path_id)
{
	char framesuffix[16];

	q_snprintf (framesuffix, sizeof(framesuffix), "_%02d_%02d", skin, frame);
	return Mod_MD5LoadLMP (mod, shader, framesuffix, filename, filenamesize, path_id);
}

static qboolean Mod_MD5LoadSkinFrame (qmodel_t *mod, aliashdr_t *surface,
	int skin, int frame, const char *shader)
{
	char shaderbase[MAX_QPATH], basename[MAX_QPATH], lmpname[MAX_QPATH], glowname[MAX_QPATH];
	byte *data;
	unsigned int path_id;
	int width, height, headerwidth, headerheight, i;
	int mark;
	int lmplength;
	qboolean bare_shader = false;

	COM_StripExtension (shader, shaderbase, sizeof(shaderbase));
	for (i = 0; shaderbase[i]; i++)
		if (shaderbase[i] == '\\')
			shaderbase[i] = '/';

	mark = Hunk_LowMark ();
	data = Mod_MD5LoadSkinImage (mod, shaderbase, skin, frame, "", basename,
		sizeof(basename), &width, &height);
	if (!data && skin == 0 && frame == 0)
	{
		/* Standard MD5 shaders name one texture directly; rerelease names win. */
		data = Mod_MD5LoadImage (mod, shaderbase, "", basename, sizeof(basename),
			&width, &height);
		bare_shader = data != NULL;
	}
	if (data)
	{
		surface->gltextures[skin][frame] = TexMgr_LoadImage (mod, basename, width, height,
			SRC_RGBA, data, basename, 0,
			Mod_TrueColorTextureFlags (data, width, height,
				TEXPREF_ALPHA | TEXPREF_MIPMAP | TEXPREF_PAD));
		if (skin == 0 && frame == 0)
		{
			surface->skinwidth = width;
			surface->skinheight = height;
		}
		Hunk_FreeToLowMark (mark);

		mark = Hunk_LowMark ();
		if (bare_shader)
			data = Mod_MD5LoadImage (mod, shaderbase, "_glow", glowname,
				sizeof(glowname), &width, &height);
		else
			data = Mod_MD5LoadSkinImage (mod, shaderbase, skin, frame, "_glow", glowname,
				sizeof(glowname), &width, &height);
		if (!data)
		{
			if (bare_shader)
				data = Mod_MD5LoadImage (mod, shaderbase, "_luma", glowname,
					sizeof(glowname), &width, &height);
			else
				data = Mod_MD5LoadSkinImage (mod, shaderbase, skin, frame, "_luma", glowname,
					sizeof(glowname), &width, &height);
		}
		if (data)
		{
			Mod_NormalizeExternalFullbright (data, width, height);
			surface->fbtextures[skin][frame] = TexMgr_LoadImage (mod, glowname, width, height,
				SRC_RGBA, data, glowname, 0,
				Mod_TrueColorTextureFlags (data, width, height,
					TEXPREF_ALPHA | TEXPREF_MIPMAP | TEXPREF_PAD));
		}
		Hunk_FreeToLowMark (mark);
	}
	else
	{
		Hunk_FreeToLowMark (mark);
		data = Mod_MD5LoadSkinLMP (mod, shaderbase, skin, frame, lmpname,
			sizeof(lmpname), &path_id);
		if (!data && skin == 0 && frame == 0)
			data = Mod_MD5LoadLMP (mod, shaderbase, "", lmpname,
				sizeof(lmpname), &path_id);
		lmplength = com_filesize;
		if (data && lmplength >= 8)
		{
			memcpy (&headerwidth, data, sizeof(headerwidth));
			memcpy (&headerheight, data + 4, sizeof(headerheight));
			width = LittleLong (headerwidth);
			height = LittleLong (headerheight);
			if (width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
				width <= INT_MAX / height && lmplength >= 8 + width * height)
			{
				surface->gltextures[skin][frame] = TexMgr_LoadImage (mod, lmpname, width, height,
					SRC_INDEXED, data + 8, lmpname, 8,
					TEXPREF_ALPHA | TEXPREF_MIPMAP | TEXPREF_PAD | TEXPREF_NOBRIGHT);
				if (Mod_CheckFullbrights (data + 8, width * height))
					surface->fbtextures[skin][frame] = TexMgr_LoadImage (mod,
						va("%s_luma", lmpname), width, height, SRC_INDEXED, data + 8,
						lmpname, 8, TEXPREF_ALPHA | TEXPREF_MIPMAP | TEXPREF_PAD | TEXPREF_FULLBRIGHT);
				if (skin == 0 && frame == 0)
				{
					surface->skinwidth = width;
					surface->skinheight = height;
				}
			}
		}
		free (data);
	}

	return surface->gltextures[skin][frame] != NULL;
}

static qboolean Mod_MD5LoadSkin (qmodel_t *mod, aliashdr_t *surface,
	const char *shader)
{
	int skin, frame, numframes;

	/* The rerelease encodes skin and animation frame as _SS_FF. */
	for (skin = 0; skin < MAX_SKINS; skin++)
	{
		for (frame = 0; frame < 4; frame++)
			if (!Mod_MD5LoadSkinFrame (mod, surface, skin, frame, shader))
				break;
		if (!frame)
			break;
		numframes = frame;

		/* A single still or a short group repeats through the alias animator. */
		for (; frame < 4; frame++)
		{
			surface->gltextures[skin][frame] = surface->gltextures[skin][frame % numframes];
			surface->fbtextures[skin][frame] = surface->fbtextures[skin][frame % numframes];
		}
	}

	surface->numskins = skin;
	return surface->numskins > 0;
}

static qboolean Mod_MD5LoadAnimation (qmodel_t *mod, const md5joint_t *joints,
	int numjoints, float **outposes, int *outnumposes, float *outinterval)
{
	char filename[MAX_QPATH];
	byte *filedata = NULL;
	unsigned int path_id;
	md5parser_t parser;
	md5animbase_t *animbase = NULL;
	vec3_t *basepos = NULL;
	vec4_t *basequat = NULL;
	float *raw = NULL, *poses = NULL, *local = NULL;
	byte *seen = NULL;
	size_t numframes, animationjoints, rawcount;
	int frame, joint;
	float framerate;
	qboolean valid = false;

	*outposes = NULL;
	*outnumposes = 1;
	*outinterval = 0.1f;
	COM_StripExtension (mod->name, filename, sizeof(filename));
	COM_AddExtension (filename, ".md5anim", sizeof(filename));
	filedata = mod_md5_rerelease_only ?
		COM_LoadMallocFileFromRerelease (filename, &path_id) :
		COM_LoadMallocFile (filename, &path_id);
	if (!filedata || path_id < mod->path_id)
	{
		free (filedata);
		return true; /* static companion: bind pose is a valid fallback */
	}

	parser.cursor = (const char *)filedata;
	parser.error = NULL;
	if (!Mod_MD5Expect (&parser, "MD5Version") || !Mod_MD5Expect (&parser, "10") ||
		!Mod_MD5Token (&parser))
		goto done;
	if (!strcmp (com_token, "commandline") &&
		(!Mod_MD5Token (&parser) || !Mod_MD5Token (&parser)))
		goto done;
	if (strcmp (com_token, "numFrames") || !Mod_MD5Size (&parser, &numframes) ||
		numframes < 1 || numframes > MAXALIASFRAMES ||
		!Mod_MD5Expect (&parser, "numJoints") || !Mod_MD5Size (&parser, &animationjoints) ||
		animationjoints != (size_t)numjoints || !Mod_MD5Expect (&parser, "frameRate") ||
		!Mod_MD5Float (&parser, &framerate) || framerate <= 0.0f ||
		!Mod_MD5Expect (&parser, "numAnimatedComponents") || !Mod_MD5Size (&parser, &rawcount) ||
		rawcount > MAX_MD5_ANIM_COMPONENTS)
	{
		Mod_MD5Fail (&parser, "invalid animation header");
		goto done;
	}

	if (numframes > SIZE_MAX / (size_t)numjoints / 12 ||
		numframes * (size_t)numjoints * 12 * sizeof(float) > MAX_MD5_DECODED_BYTES)
	{
		Mod_MD5Fail (&parser, "animation data exceeds 64 MiB limit");
		goto done;
	}
	animbase = (md5animbase_t *)calloc ((size_t)numjoints, sizeof(*animbase));
	basepos = (vec3_t *)calloc ((size_t)numjoints, sizeof(*basepos));
	basequat = (vec4_t *)calloc ((size_t)numjoints, sizeof(*basequat));
	raw = (float *)calloc (rawcount ? rawcount : 1, sizeof(*raw));
	local = (float *)calloc ((size_t)numjoints * 12, sizeof(*local));
	seen = (byte *)calloc (numframes, sizeof(*seen));
	poses = (float *)malloc (numframes * (size_t)numjoints * 12 * sizeof(*poses));
	if (!animbase || !basepos || !basequat || !raw || !local || !seen || !poses)
	{
		Mod_MD5Fail (&parser, "out of memory");
		goto done;
	}

	if (!Mod_MD5Expect (&parser, "hierarchy") || !Mod_MD5Expect (&parser, "{"))
		goto done;
	for (joint = 0; joint < numjoints; joint++)
	{
		size_t offset;
		int parent;

		if (!Mod_MD5Token (&parser) || strcmp(com_token, joints[joint].name) ||
			!Mod_MD5Int (&parser, &parent) || parent != joints[joint].parent ||
			!Mod_MD5Size (&parser, &offset) || !Mod_MD5Size (&parser, &animbase[joint].offset))
		{
			Mod_MD5Fail (&parser, "invalid animation hierarchy");
			goto done;
		}
		animbase[joint].flags = (unsigned int)offset;
		if (animbase[joint].flags & ~63u || animbase[joint].offset > rawcount)
		{
			Mod_MD5Fail (&parser, "invalid animated component range");
			goto done;
		}
	}
	if (!Mod_MD5Expect (&parser, "}"))
		goto done;

	if (!Mod_MD5Expect (&parser, "bounds") || !Mod_MD5Expect (&parser, "{"))
		goto done;
	for (frame = 0; frame < (int)numframes; frame++)
	{
		vec3_t ignored;
		if (!Mod_MD5Vec3 (&parser, ignored) || !Mod_MD5Vec3 (&parser, ignored))
			goto done;
	}
	if (!Mod_MD5Expect (&parser, "}"))
		goto done;

	if (!Mod_MD5Expect (&parser, "baseframe") || !Mod_MD5Expect (&parser, "{"))
		goto done;
	for (joint = 0; joint < numjoints; joint++)
	{
		if (!Mod_MD5Vec3 (&parser, basepos[joint]) || !Mod_MD5Vec3 (&parser, basequat[joint]))
			goto done;
	}
	if (!Mod_MD5Expect (&parser, "}"))
		goto done;

	for (;;)
	{
		const char *next = COM_Parse (parser.cursor);
		size_t index, value;

		if (!next)
			break;
		parser.cursor = next;
		if (strcmp (com_token, "frame") || !Mod_MD5Size (&parser, &index) || index >= numframes || seen[index] ||
			!Mod_MD5Expect (&parser, "{"))
		{
			Mod_MD5Fail (&parser, "invalid animation frame");
			goto done;
		}
		seen[index] = true;
		for (value = 0; value < rawcount; value++)
			if (!Mod_MD5Float (&parser, &raw[value]))
				goto done;
		if (!Mod_MD5Expect (&parser, "}"))
			goto done;

		for (joint = 0; joint < numjoints; joint++)
		{
			vec3_t pos;
			vec4_t quat;
			float *source = raw + animbase[joint].offset;
			unsigned int flags = animbase[joint].flags;
			unsigned int components = 0, testflags = flags;
			float matrix[12];

			while (testflags) { components += testflags & 1u; testflags >>= 1; }
			if (components > rawcount - animbase[joint].offset)
			{
				Mod_MD5Fail (&parser, "invalid animation component range");
				goto done;
			}
			VectorCopy (basepos[joint], pos);
			VectorCopy (basequat[joint], quat);
			if (flags & 1) pos[0] = *source++;
			if (flags & 2) pos[1] = *source++;
			if (flags & 4) pos[2] = *source++;
			if (flags & 8) quat[0] = *source++;
			if (flags & 16) quat[1] = *source++;
			if (flags & 32) quat[2] = *source++;
			Mod_MD5QuaternionW (quat);
			Mod_MD5Matrix (pos, quat, matrix);
			if (joints[joint].parent < 0)
				memcpy (local + (size_t)joint * 12, matrix, sizeof(matrix));
			else
				R_ConcatTransforms ((float (*)[4])(local + (size_t)joints[joint].parent * 12),
					(float (*)[4])matrix, (float (*)[4])(local + (size_t)joint * 12));
		}
		memcpy (poses + index * (size_t)numjoints * 12, local,
			(size_t)numjoints * 12 * sizeof(*poses));
	}

	for (frame = 0; frame < (int)numframes; frame++)
		if (!seen[frame])
		{
			Mod_MD5Fail (&parser, "missing animation frame");
			goto done;
		}

	*outposes = poses;
	*outnumposes = (int)numframes;
	*outinterval = 1.0f / framerate;
	poses = NULL;
	valid = true;

done:
	if (!valid && parser.error)
		Con_Warning ("MD5 animation: %s: %s; using bind pose\n", filename, parser.error);
	free (poses);
	free (seen);
	free (local);
	free (raw);
	free (basequat);
	free (basepos);
	free (animbase);
	free (filedata);
	return true;
}

static qboolean Mod_LoadMD5MeshModel (qmodel_t *mod, const byte *buffer, size_t filesize)
{
	md5parser_t parser;
	md5joint_t *joints = NULL;
	aliashdr_t *surfaces[MAX_MD5_SURFACES];
	float *animation = NULL, *bindposes = NULL;
	vec3_t mins, maxs;
	size_t numjoints, numsurfaces, decodedbytes = 0;
	int numposes = 1;
	float frameinterval = 0.1f;
	int hunkmark, joint, surface, frame, vert, k;
	qboolean valid = false;

	if (filesize < 12)
		return Mod_MD5Warning (mod, "file is shorter than an MD5 header");

	memset (surfaces, 0, sizeof(surfaces));
	parser.cursor = (const char *)buffer;
	parser.error = NULL;
	hunkmark = Hunk_LowMark ();

	if (!Mod_MD5Expect (&parser, "MD5Version") || !Mod_MD5Expect (&parser, "10") ||
		!Mod_MD5Token (&parser))
		goto done;
	if (!strcmp (com_token, "commandline") &&
		(!Mod_MD5Token (&parser) || !Mod_MD5Token (&parser)))
		goto done;
	if (strcmp (com_token, "numJoints") || !Mod_MD5Size (&parser, &numjoints) ||
		numjoints < 1 || numjoints > MAX_MD5_JOINTS ||
		!Mod_MD5Expect (&parser, "numMeshes") || !Mod_MD5Size (&parser, &numsurfaces) ||
		numsurfaces < 1 || numsurfaces > MAX_MD5_SURFACES ||
		!Mod_MD5Expect (&parser, "joints") || !Mod_MD5Expect (&parser, "{"))
	{
		Mod_MD5Fail (&parser, "invalid mesh header");
		goto done;
	}

	joints = (md5joint_t *)calloc (numjoints, sizeof(*joints));
	if (!joints)
	{
		Mod_MD5Fail (&parser, "out of memory");
		goto done;
	}

	for (joint = 0; joint < (int)numjoints; joint++)
	{
		vec3_t pos;
		vec4_t quat;
		int parent;

		if (!Mod_MD5Token (&parser))
			goto done;
		q_strlcpy (joints[joint].name, com_token, sizeof(joints[joint].name));
		if (!joints[joint].name[0] || !Mod_MD5Int (&parser, &parent) ||
			parent < -1 || parent >= joint || !Mod_MD5Vec3 (&parser, pos) ||
			!Mod_MD5Expect (&parser, "(") || !Mod_MD5Float (&parser, &quat[0]) ||
			!Mod_MD5Float (&parser, &quat[1]) || !Mod_MD5Float (&parser, &quat[2]) ||
			!Mod_MD5Expect (&parser, ")"))
		{
			Mod_MD5Fail (&parser, "invalid joint");
			goto done;
		}
		joints[joint].parent = parent;
		Mod_MD5QuaternionW (quat);
		/* MD5 mesh joints are model-space bind transforms. Companion .md5anim
		 * frames are local and are concatenated in Mod_MD5LoadAnimation; doing
		 * that here as well double-transforms hierarchy children. */
		Mod_MD5Matrix (pos, quat, joints[joint].bind);
	}
	if (!Mod_MD5Expect (&parser, "}"))
		goto done;
	bindposes = (float *)malloc (numjoints * 12 * sizeof(*bindposes));
	if (!bindposes)
	{
		Mod_MD5Fail (&parser, "out of memory");
		goto done;
	}
	for (joint = 0; joint < (int)numjoints; joint++)
		memcpy (bindposes + (size_t)joint * 12, joints[joint].bind,
			sizeof(joints[joint].bind));

	/* A missing or invalid companion animation is safely rendered as bind pose. */
	Mod_MD5LoadAnimation (mod, joints, (int)numjoints, &animation, &numposes, &frameinterval);
	if (numposes < 1 || numposes > MAXALIASFRAMES)
	{
		Mod_MD5Fail (&parser, "invalid animation pose count");
		goto done;
	}
	/* Keep the source skeleton and animation matrices for optional VRIK CPU
	 * skinning.  Dedicated servers retain their previous no-renderer path. */
	if (!isDedicated &&
		(!Mod_MD5AddBytes (&decodedbytes, numjoints, sizeof(md5livejoint_t)) ||
		 !Mod_MD5AddBytes (&decodedbytes, numjoints, sizeof(int)) ||
		 !Mod_MD5AddBytes (&decodedbytes, (size_t)numposes * numjoints * 12,
			sizeof(float))))
	{
		Mod_MD5Fail (&parser, "decoded data exceeds 64 MiB limit");
		goto done;
	}

	for (k = 0; k < 3; k++)
		mins[k] = FLT_MAX, maxs[k] = -FLT_MAX;

	for (surface = 0; surface < (int)numsurfaces; surface++)
	{
		char shader[MAX_QPATH];
		md5vertinfo_t *vertinfo = NULL;
		md5weight_t *weights = NULL;
		unsigned short *indexes = NULL;
		byte *vertseen = NULL, *triseen = NULL, *weightseen = NULL;
			aliashdr_t *out;
			md5vertex_t *vertices;
			size_t numverts, numtris, numweights, numindexes;
			size_t surfacebytes = 0, headersize;
		if (!Mod_MD5Expect (&parser, "mesh") || !Mod_MD5Expect (&parser, "{") ||
			!Mod_MD5Expect (&parser, "shader") || !Mod_MD5Token (&parser))
			goto surface_done;
		q_strlcpy (shader, com_token, sizeof(shader));
		if (!shader[0] || !Mod_MD5Expect (&parser, "numverts") || !Mod_MD5Size (&parser, &numverts) ||
			numverts < 1 || numverts > MAX_MD5_VERTICES)
		{
			Mod_MD5Fail (&parser, "invalid mesh vertex count");
			goto surface_done;
		}

		vertinfo = (md5vertinfo_t *)calloc (numverts, sizeof(*vertinfo));
		vertseen = (byte *)calloc (numverts, sizeof(*vertseen));
		if (!vertinfo || !vertseen)
		{
			Mod_MD5Fail (&parser, "out of memory");
			goto surface_done;
		}
		if (!Mod_MD5Token (&parser))
			goto surface_done;
		while (!strcmp (com_token, "vert"))
		{
			size_t index;

			if (!Mod_MD5Size (&parser, &index) || index >= numverts || vertseen[index] ||
				!Mod_MD5Expect (&parser, "(") || !Mod_MD5Float (&parser, &vertinfo[index].st[0]) ||
				!Mod_MD5Float (&parser, &vertinfo[index].st[1]) || !Mod_MD5Expect (&parser, ")") ||
				!Mod_MD5Size (&parser, &vertinfo[index].firstweight) ||
				!Mod_MD5Size (&parser, &vertinfo[index].count) || !vertinfo[index].count)
			{
				Mod_MD5Fail (&parser, "invalid mesh vertex");
				goto surface_done;
			}
			vertseen[index] = true;
			if (!Mod_MD5Token (&parser))
				goto surface_done;
		}
		if (strcmp (com_token, "numtris") || !Mod_MD5Size (&parser, &numtris) ||
			numtris < 1 || numtris > MAX_MD5_TRIANGLES || numtris > SIZE_MAX / 3)
		{
			Mod_MD5Fail (&parser, "invalid mesh triangle count");
			goto surface_done;
		}
		numindexes = numtris * 3;
		indexes = (unsigned short *)malloc (numindexes * sizeof(*indexes));
		triseen = (byte *)calloc (numtris, sizeof(*triseen));
		if (!indexes || !triseen)
		{
			Mod_MD5Fail (&parser, "out of memory");
			goto surface_done;
		}
		if (!Mod_MD5Token (&parser))
			goto surface_done;
		while (!strcmp (com_token, "tri"))
		{
			size_t index;
			int corner;

			if (!Mod_MD5Size (&parser, &index) || index >= numtris || triseen[index])
			{
				Mod_MD5Fail (&parser, "invalid mesh triangle");
				goto surface_done;
			}
			triseen[index] = true;
			for (corner = 0; corner < 3; corner++)
			{
				size_t vertex;
				if (!Mod_MD5Size (&parser, &vertex) || vertex >= numverts)
				{
					Mod_MD5Fail (&parser, "triangle index outside mesh");
					goto surface_done;
				}
				indexes[index * 3 + corner] = (unsigned short)vertex;
			}
			if (!Mod_MD5Token (&parser))
				goto surface_done;
		}
		if (strcmp (com_token, "numweights") || !Mod_MD5Size (&parser, &numweights) ||
			numweights < 1 || numweights > MAX_MD5_WEIGHTS)
		{
			Mod_MD5Fail (&parser, "invalid mesh weight count");
			goto surface_done;
		}
		weights = (md5weight_t *)calloc (numweights, sizeof(*weights));
		weightseen = (byte *)calloc (numweights, sizeof(*weightseen));
		if (!weights || !weightseen)
		{
			Mod_MD5Fail (&parser, "out of memory");
			goto surface_done;
		}
		if (!Mod_MD5Token (&parser))
			goto surface_done;
		while (!strcmp (com_token, "weight"))
		{
			size_t index, bone;
			float bias;

			if (!Mod_MD5Size (&parser, &index) || index >= numweights || weightseen[index] ||
				!Mod_MD5Size (&parser, &bone) || bone >= numjoints ||
				!Mod_MD5Float (&parser, &bias) || bias < 0.0f || bias > 1.0f ||
				!Mod_MD5Expect (&parser, "(") || !Mod_MD5Float (&parser, &weights[index].position[0]) ||
				!Mod_MD5Float (&parser, &weights[index].position[1]) || !Mod_MD5Float (&parser, &weights[index].position[2]) ||
				!Mod_MD5Expect (&parser, ")"))
			{
				Mod_MD5Fail (&parser, "invalid mesh weight");
				goto surface_done;
			}
			weightseen[index] = true;
			weights[index].joint = (int)bone;
			weights[index].position[0] *= bias;
			weights[index].position[1] *= bias;
			weights[index].position[2] *= bias;
			weights[index].position[3] = bias;
			if (!Mod_MD5Token (&parser))
				goto surface_done;
		}
		if (strcmp (com_token, "}"))
		{
			Mod_MD5Fail (&parser, "unterminated mesh");
			goto surface_done;
		}
		for (vert = 0; vert < (int)numverts; vert++)
			if (!vertseen[vert] || vertinfo[vert].firstweight > numweights ||
				vertinfo[vert].count > numweights - vertinfo[vert].firstweight)
			{
				Mod_MD5Fail (&parser, "invalid vertex weight range");
				goto surface_done;
			}
		for (vert = 0; vert < (int)numtris; vert++)
			if (!triseen[vert])
			{
				Mod_MD5Fail (&parser, "missing mesh triangle");
				goto surface_done;
			}
		for (vert = 0; vert < (int)numweights; vert++)
			if (!weightseen[vert])
			{
				Mod_MD5Fail (&parser, "missing mesh weight");
				goto surface_done;
			}

		if ((size_t)numposes > SIZE_MAX / numverts ||
			!Mod_MD5AddBytes (&surfacebytes, 1, sizeof(*out)) ||
			!Mod_MD5AddBytes (&surfacebytes, (size_t)(numposes - 1), sizeof(out->frames[0])) ||
			!Mod_MD5AddBytes (&surfacebytes, (size_t)numposes * numverts, sizeof(*vertices)) ||
			!Mod_MD5AddBytes (&surfacebytes, numindexes, sizeof(*indexes)) ||
			(!isDedicated &&
			 (!Mod_MD5AddBytes (&surfacebytes, numverts, sizeof(md5livevertex_t)) ||
			  !Mod_MD5AddBytes (&surfacebytes, numweights, sizeof(md5liveweight_t)))) ||
			decodedbytes > MAX_MD5_DECODED_BYTES - surfacebytes)
		{
			Mod_MD5Fail (&parser, "decoded data exceeds 64 MiB limit");
			goto surface_done;
		}
		decodedbytes += surfacebytes;

		headersize = sizeof(*out) + (size_t)(numposes - 1) * sizeof(out->frames[0]);
		if (headersize > INT_MAX)
		{
			Mod_MD5Fail (&parser, "frame header exceeds hunk allocation limit");
			goto surface_done;
		}
		out = (aliashdr_t *)Hunk_AllocName ((int)headersize, loadname);
		out->poseverttype = ALIAS_POSE_MD5;
		out->numframes = out->numposes = numposes;
		out->numverts = out->numverts_vbo = (int)numverts;
		out->numtris = (int)numtris;
		out->numindexes = (int)numindexes;
			out->numskins = isDedicated ? 1 : 0;
		out->skinwidth = out->skinheight = 1;
		out->scale[0] = out->scale[1] = out->scale[2] = 1.0f;
		VectorCopy (out->scale, out->original_scale);
		out->scale_origin[0] = out->scale_origin[1] = out->scale_origin[2] = 0.0f;
		out->original_scale_origin[0] = out->original_scale_origin[1] = out->original_scale_origin[2] = 0.0f;
		for (frame = 0; frame < numposes; frame++)
		{
			out->frames[frame].firstpose = frame;
			out->frames[frame].numposes = 1;
			out->frames[frame].interval = frameinterval;
		}

		vertices = (md5vertex_t *)Hunk_Alloc ((int)((size_t)numposes * numverts * sizeof(*vertices)));
		out->vertexes = (intptr_t)((byte *)vertices - (byte *)out);
		for (frame = 0; frame < numposes; frame++)
		{
			const float *matrices = animation ? animation + (size_t)frame * numjoints * 12 : bindposes;
			md5vertex_t *pose = vertices + (size_t)frame * numverts;

			Mod_MD5BuildSkinnedPose (pose, (int)numverts, vertinfo, weights, matrices);
			Mod_MD5ComputeNormals (pose, (int)numverts, indexes, (int)numindexes);
			for (vert = 0; vert < (int)numverts; vert++)
				for (k = 0; k < 3; k++)
				{
					mins[k] = q_min (mins[k], pose[vert].xyz[k]);
					maxs[k] = q_max (maxs[k], pose[vert].xyz[k]);
				}
		}
		{
			unsigned short *persistent = (unsigned short *)Hunk_Alloc ((int)(numindexes * sizeof(*persistent)));
			memcpy (persistent, indexes, numindexes * sizeof(*persistent));
			out->indexes = (intptr_t)((byte *)persistent - (byte *)out);
		}
		if (!isDedicated)
		{
			md5livevertex_t *persistentverts;
			md5liveweight_t *persistentweights;

			persistentverts = (md5livevertex_t *)Hunk_Alloc ((int)(numverts * sizeof(*persistentverts)));
			persistentweights = (md5liveweight_t *)Hunk_Alloc ((int)(numweights * sizeof(*persistentweights)));
			for (vert = 0; vert < (int)numverts; vert++)
			{
				persistentverts[vert].firstweight = (unsigned int)vertinfo[vert].firstweight;
				persistentverts[vert].numweights = (unsigned int)vertinfo[vert].count;
				persistentverts[vert].st[0] = vertinfo[vert].st[0];
				persistentverts[vert].st[1] = vertinfo[vert].st[1];
			}
			for (vert = 0; vert < (int)numweights; vert++)
			{
				persistentweights[vert].joint = weights[vert].joint;
				memcpy (persistentweights[vert].position, weights[vert].position,
					sizeof(persistentweights[vert].position));
			}
			out->md5_numliveweights = (int)numweights;
			out->md5_livevertices = (intptr_t)((byte *)persistentverts - (byte *)out);
			out->md5_liveweights = (intptr_t)((byte *)persistentweights - (byte *)out);
		}
		if (!isDedicated)
			Mod_MD5LoadSkin (mod, out, shader);
		surfaces[surface] = out;

		free (weightseen); weightseen = NULL;
		free (weights); weights = NULL;
		free (triseen); triseen = NULL;
		free (indexes); indexes = NULL;
		free (vertseen); vertseen = NULL;
		free (vertinfo); vertinfo = NULL;
		continue;

	surface_done:
		free (weightseen);
		free (weights);
		free (triseen);
		free (indexes);
		free (vertseen);
		free (vertinfo);
		if (parser.error)
			goto done;
		Mod_MD5Fail (&parser, "invalid mesh data");
		goto done;
	}

	for (surface = 0; surface < (int)numsurfaces - 1; surface++)
		surfaces[surface]->nextsurface = (intptr_t)((byte *)surfaces[surface + 1] - (byte *)surfaces[surface]);

	/* The renderer normally only needs skinned vertices, but retaining the
	 * evaluated joint matrices lets r_showskel display exactly the same pose.
	 * Keep the model-space bind joints too, so VRIK can skin an adjusted
	 * palette without changing the ordinary baked-pose renderer.  Store this
	 * once on the first surface: Mod_GetMD5Extradata returns it. */
	if (!isDedicated)
	{
		md5livejoint_t *livejoints = (md5livejoint_t *)Hunk_Alloc (
			(int)(numjoints * sizeof (*livejoints)));
		int *parents = (int *)Hunk_Alloc ((int)(numjoints * sizeof (*parents)));
		float *poses = (float *)Hunk_Alloc ((int)((size_t)numposes * numjoints * 12 * sizeof (*poses)));
		const float *source = animation ? animation : bindposes;

		for (joint = 0; joint < (int)numjoints; joint++)
		{
			q_strlcpy (livejoints[joint].name, joints[joint].name,
				sizeof(livejoints[joint].name));
			livejoints[joint].parent = joints[joint].parent;
			memcpy (livejoints[joint].bind, joints[joint].bind,
				sizeof(livejoints[joint].bind));
			parents[joint] = joints[joint].parent;
		}
		memcpy (poses, source, (size_t)numposes * numjoints * 12 * sizeof (*poses));
		surfaces[0]->md5_numbones = (int)numjoints;
		surfaces[0]->md5_boneparents = (intptr_t)((byte *)parents - (byte *)surfaces[0]);
		surfaces[0]->md5_boneposes = (intptr_t)((byte *)poses - (byte *)surfaces[0]);
		surfaces[0]->md5_livejoints = (intptr_t)((byte *)livejoints - (byte *)surfaces[0]);
	}

	mod->type = mod_alias;
	mod->numframes = numposes;
	mod->synctype = ST_FRAMETIME;
	mod->flags = 0;
	Mod_SetExtraFlags (mod);
	Mod_SetMD3Bounds (mod, mins, maxs);
	Mod_RegisterMD5AliasBuild (surfaces[0], mins, maxs);
	GLMesh_LoadVertexBuffer (mod, surfaces[0]);
	valid = true;

done:
	if (!valid)
	{
		if (parser.error)
			Mod_MD5Warning (mod, parser.error);
		Hunk_FreeToLowMark (hunkmark);
		if (!isDedicated)
			TexMgr_FreeTexturesForOwner (mod);
	}
	free (bindposes);
	free (animation);
	free (joints);
	return valid;
}

/*
===============================
Mod_LoadVerifiedRereleasePlayerMD5

Load the official player MD5 into its own cache directly from a pack whose
complete rerelease signature was verified by common.c.  This is used to check
whether the VRIK feature's required assets are installed without replacing the
active game's player model; rendering only deforms the compatible model that
the game actually assigned to the entity.
===============================
*/
static qmodel_t *Mod_LoadVerifiedRereleasePlayerMD5 (void)
{
	static const char cache_name[] = "progs/@vrik_rerelease_player.md5mesh";
	qmodel_t *mod;
	byte *buffer;
	unsigned int path_id = 0;
	int filesize;
	char savedname[MAX_QPATH];
	qboolean loaded;

	if (isDedicated)
		return NULL;
	mod = Mod_FindName (cache_name);
	if (!mod->needload && Cache_Check (&mod->cache))
		return mod;

	buffer = COM_LoadMallocFileFromRerelease ("progs/player.md5mesh", &path_id);
	if (!buffer)
		return NULL;
	filesize = com_filesize;
	q_strlcpy (savedname, mod->name, sizeof(savedname));
	q_strlcpy (mod->name, "progs/player.md5mesh", sizeof(mod->name));
	mod->path_id = path_id;
	mod->needload = false;
	loadmodel = mod;
	q_strlcpy (loadname, "vrikplayer", sizeof(loadname));

	Mod_BeginAliasBuild ();
	mod_md5_rerelease_only = true;
	loaded = Mod_LoadMD5MeshModel (mod, buffer, (size_t)filesize);
	mod_md5_rerelease_only = false;
	free (buffer);
	q_strlcpy (mod->name, savedname, sizeof(mod->name));

	if (!loaded)
	{
		Hunk_FreeToLowMark (mod_alias_build.startmark);
		memset (&mod_alias_build, 0, sizeof(mod_alias_build));
		mod->needload = true;
		return NULL;
	}
	mod_alias_build.md5_from_rerelease = true;
	Mod_FinishAliasBuild (mod);
	return mod;
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
static void *Mod_LoadSpriteFrame (void * pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t		*pinframe;
	mspriteframe_t		*pspriteframe;
	int					width, height, size, origin[2];
	char				name[64];
	src_offset_t			offset; //johnfitz

	pinframe = (dspriteframe_t *)pin;

	width = LittleLong (pinframe->width);
	height = LittleLong (pinframe->height);
	size = width * height;

	pspriteframe = (mspriteframe_t *) Hunk_AllocName (sizeof (mspriteframe_t),loadname);
	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (pinframe->origin[0]);
	origin[1] = LittleLong (pinframe->origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	//johnfitz -- image might be padded
	pspriteframe->smax = (float)width/(float)TexMgr_PadConditional(width);
	pspriteframe->tmax = (float)height/(float)TexMgr_PadConditional(height);
	//johnfitz

	q_snprintf (name, sizeof(name), "%s:frame%i", loadmodel->name, framenum);
	offset = (src_offset_t)(pinframe+1) - (src_offset_t)mod_base; //johnfitz
	pspriteframe->gltexture =
		TexMgr_LoadImage (loadmodel, name, width, height, SRC_INDEXED,
				  (byte *)(pinframe + 1), loadmodel->name, offset,
				  TEXPREF_PAD | TEXPREF_ALPHA | TEXPREF_NOPICMIP); //johnfitz -- TexMgr

	return (void *)((byte *)pinframe + sizeof (dspriteframe_t) + size);
}


/*
=================
Mod_LoadSpriteGroup
=================
*/
static void *Mod_LoadSpriteGroup (void * pin, mspriteframe_t **ppframe, int framenum, spriteframetype_t type)
{
	dspritegroup_t		*pingroup;
	mspritegroup_t		*pspritegroup;
	int					i, numframes;
	dspriteinterval_t	*pin_intervals;
	float				*poutintervals;
	void				*ptemp;

	pingroup = (dspritegroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);
	if (type == SPR_ANGLED && numframes != 8)
		Sys_Error ("Mod_LoadSpriteGroup: Bad # of frames: %d", numframes);

	pspritegroup = (mspritegroup_t *) Hunk_AllocName (sizeof (mspritegroup_t) +
				(numframes - 1) * sizeof (pspritegroup->frames[0]), loadname);

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	pin_intervals = (dspriteinterval_t *)(pingroup + 1);

	poutintervals = (float *) Hunk_AllocName (numframes * sizeof (float), loadname);

	pspritegroup->intervals = poutintervals;

	for (i=0 ; i<numframes ; i++)
	{
		*poutintervals = LittleFloat (pin_intervals->interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		pin_intervals++;
	}

	ptemp = (void *)pin_intervals;

	for (i=0 ; i<numframes ; i++)
	{
		ptemp = Mod_LoadSpriteFrame (ptemp, &pspritegroup->frames[i], framenum * 100 + i);
	}

	return ptemp;
}


/*
=================
Mod_LoadSpriteModel
=================
*/
static void Mod_LoadSpriteModel (qmodel_t *mod, void *buffer)
{
	int					i;
	int					version;
	dsprite_t			*pin;
	msprite_t			*psprite;
	int					numframes;
	int					size;
	dspriteframetype_t	*pframetype;

	pin = (dsprite_t *)buffer;
	mod_base = (byte *)buffer; //johnfitz

	version = LittleLong (pin->version);
	if (version != SPRITE_VERSION)
		Sys_Error ("%s has wrong version number "
				 "(%i should be %i)", mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (pin->numframes);

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *) Hunk_AllocName (size, loadname);

	mod->cache.data = psprite;

	psprite->type = LittleLong (pin->type);
	psprite->maxwidth = LittleLong (pin->width);
	psprite->maxheight = LittleLong (pin->height);
	psprite->beamlength = LittleFloat (pin->beamlength);
	mod->synctype = (synctype_t) LittleLong (pin->synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth/2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth/2;
	mod->mins[2] = -psprite->maxheight/2;
	mod->maxs[2] = psprite->maxheight/2;

//
// load the frames
//
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d", numframes);

	mod->numframes = numframes;

	pframetype = (dspriteframetype_t *)(pin + 1);

	for (i=0 ; i<numframes ; i++)
	{
		spriteframetype_t	frametype;

		frametype = (spriteframetype_t) LittleLong (pframetype->type);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteFrame (pframetype + 1, &psprite->frames[i].frameptr, i);
		}
		else
		{
			pframetype = (dspriteframetype_t *)
					Mod_LoadSpriteGroup (pframetype + 1, &psprite->frames[i].frameptr, i, frametype);
		}
	}

	mod->type = mod_sprite;
}

//=============================================================================

/*
================
Mod_Print
================
*/
static void Mod_Print (void)
{
	int		i;
	qmodel_t	*mod;

	Con_SafePrintf ("Cached models:\n"); //johnfitz -- safeprint instead of print
	for (i=0, mod=mod_known ; i < mod_numknown ; i++, mod++)
	{
		const char *variant = "";
		if (mod->type == mod_alias && mod->cache.data)
		{
			const mod_alias_cache_t *cache = (const mod_alias_cache_t *)mod->cache.data;
			if (cache->magic == MOD_ALIAS_CACHE_MAGIC)
			{
				if (cache->mdl_offset && cache->md3_offset)
					variant = " [MDL+MD3]";
				else if (cache->mdl_offset && cache->md5_offset)
					variant = " [MDL+MD5]";
				else if (cache->md3_offset)
					variant = " [MD3]";
				else if (cache->md5_offset)
					variant = " [MD5]";
				else if (cache->mdl_offset)
					variant = " [MDL]";
			}
		}
		Con_SafePrintf ("%8p : %s%s\n", mod->cache.data, mod->name, variant); //johnfitz -- safeprint instead of print
	}
	Con_Printf ("%i models\n",mod_numknown); //johnfitz -- print the total too
}
