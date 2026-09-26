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
#include "miniz.h"
#include "vr_mdl_split.h"

/* miniz.h keeps this declaration disabled in the QuakeSpasm amalgamation,
 * while common.c still links the exported implementation from miniz.c. */
extern mz_ulong mz_crc32 (mz_ulong crc, const unsigned char *ptr, size_t buf_len);

static void		 Mod_LoadSpriteModel (qmodel_t *mod, void *buffer);
static void		 Mod_LoadBrushModel (qmodel_t *mod, const char *loadname, void *buffer);
static void		 Mod_LoadAliasModel (qmodel_t *mod, void *buffer,
	qfilesize_t source_size, const char *skin_source);
static qboolean	 Mod_LoadMD5MeshModel (qmodel_t *mod, const void *buffer,
	const char *asset_name, qfilesize_t asset_size);
static void		 Mod_LoadMD3Model (qmodel_t *mod, const void *buffer,
	qfilesize_t source_size);
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash);
static void		 Mod_FreeModelMemory (qmodel_t *mod);
static qboolean	 Mod_CheckedSizeMul (size_t a, size_t b, size_t *result);
static qboolean	 Mod_CheckedSizeAdd (size_t a, size_t b, size_t *result);
static qboolean	 Mod_CheckedMD3Span (size_t offset, size_t span, size_t limit);
static qboolean	 Mod_CheckedMD3RelativeSpan (size_t base, size_t base_span,
	int relative_offset, size_t span, size_t limit, size_t *absolute_offset);

cvar_t external_ents = {"external_ents", "1", CVAR_ARCHIVE_GAME};
cvar_t external_vis = {"external_vis", "1", CVAR_ARCHIVE_GAME};

// wad_external_textures = 1 enable loading of external WAD textures, 0 to forbid it for debug purposes.
cvar_t wad_external_textures = {"wad_external_textures", "1", CVAR_NONE};

// mdl_external_textures = 1 enable loading of external MDL textures, 0 to forbid it for debug purposes.
cvar_t mdl_external_textures = {"mdl_external_textures", "1", CVAR_NONE};

struct md5_skeleton_data_s
{
	size_t allocation_size;
	size_t joint_count;
	size_t pose_count;
	size_t poses_offset;
	qboolean from_rerelease;
	md5_skeleton_joint_t joints[];
};

/* Donor common.c verifies the official rerelease Ranger mesh and animation
 * by exact length and CRC32.  Verify the bytes selected by this loader so
 * same-named custom replacements never inherit lower-body provenance. */
static qboolean Mod_IsVerifiedRereleaseRangerAsset (const char *asset_name,
	const char *expected_name, const void *buffer, qfilesize_t asset_size,
	size_t expected_size, unsigned int expected_crc)
{
	if (!asset_name || q_strcasecmp (asset_name, expected_name) || !buffer ||
		asset_size < 0 || (uint64_t)asset_size != (uint64_t)expected_size)
		return false;
	return (unsigned int)mz_crc32 (MZ_CRC32_INIT,
		(const unsigned char *)buffer, expected_size) == expected_crc;
}

qboolean Mod_GetMD5Skeleton (const qmodel_t *mod, md5_skeleton_view_t *out)
{
	const md5_skeleton_data_t *data;
	size_t					joints_offset, joints_bytes, matrix_count, matrix_bytes;
	md5_skeleton_view_t	view;

	if (!out)
		return false;
	memset (out, 0, sizeof (*out));
	if (!mod || !mod->extradata[PV_MD5] || !mod->md5_skeleton)
		return false;

	data = mod->md5_skeleton;
	joints_offset = offsetof (md5_skeleton_data_t, joints);
	if (!data->joint_count || data->allocation_size < joints_offset ||
		data->joint_count > (SIZE_MAX - joints_offset) / sizeof (data->joints[0]))
		return false;
	joints_bytes = data->joint_count * sizeof (data->joints[0]);
	if (data->poses_offset != joints_offset + joints_bytes ||
		data->poses_offset > data->allocation_size ||
		(data->pose_count && data->joint_count > SIZE_MAX / data->pose_count))
		return false;
	matrix_count = data->joint_count * data->pose_count;
	if (matrix_count > SIZE_MAX / sizeof (float[12]))
		return false;
	matrix_bytes = matrix_count * sizeof (float[12]);
	if (matrix_bytes != data->allocation_size - data->poses_offset)
		return false;

	view.joints = data->joints;
	view.absolute_poses = data->pose_count
		? (const float (*)[12])((const byte *)data + data->poses_offset)
		: NULL;
	view.joint_count = data->joint_count;
	view.pose_count = data->pose_count;
	view.from_rerelease = data->from_rerelease;
	*out = view;
	return true;
}

// r_allow_replacement_md5models = 1 allow loading of MD5 replacement models if available, 0 to forbid it for debug purposes.
cvar_t r_allow_replacement_md5models = {"r_allow_replacement_md5models", "1", CVAR_NONE};

// r_allow_replacement_md3models = 1 allow loading of MD3 replacement models if available, 0 to forbid it for debug purposes.
cvar_t r_allow_replacement_md3models = {"r_allow_replacement_md3models", "1", CVAR_NONE};

cvar_t r_enhancedmodels = {"r_enhancedmodels", "1", CVAR_ARCHIVE_GAME}; // controlled in Menu with Models: enhanced (1) / classic (0)

static byte *mod_novis;
static int	 mod_novis_capacity;

static byte *mod_decompressed;
static int	 mod_decompressed_capacity;

qmodel_t mod_known[MAX_MODELS];
int		 mod_numknown;

texture_t *r_notexture_mip;	 // johnfitz -- moved here from r_main.c
texture_t *r_notexture_mip2; // johnfitz -- used for non-lightmapped surfs with a missing texture

/*
===============
ReadShortUnaligned
===============
*/
static short ReadShortUnaligned (byte *ptr)
{
	short temp;
	memcpy (&temp, ptr, sizeof (short));
	return LittleShort (temp);
}

/*
===============
ReadLongUnaligned
===============
*/
static int ReadLongUnaligned (byte *ptr)
{
	int temp;
	memcpy (&temp, ptr, sizeof (int));
	return LittleLong (temp);
}

/*
===============
ReadFloatUnaligned
===============
*/
static float ReadFloatUnaligned (byte *ptr)
{
	float temp;
	memcpy (&temp, ptr, sizeof (float));
	return LittleFloat (temp);
}

/*
===============
Mod_RefreshSkins_f
===============
*/
void Mod_RefreshSkins_f (cvar_t *var)
{
	for (int i = 0; i < cl.maxclients; ++i)
		R_TranslateNewPlayerSkin (i);
}

/*
===============
Mod_EnhancedModels_f
===============
*/
static void Mod_EnhancedModels_f (cvar_t *var)
{
	int		  i;
	qmodel_t *mod;

	R_FreeAllEntityBLASes ();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (mod->type != mod_alias)
			continue;

		for (int j = 0; j < PV_SIZE; ++j)
			GLMesh_DeleteMeshBuffers ((aliashdr_t *)mod->extradata[j]);
		Mod_FreeModelMemory (mod);
		mod->needload = true;
	}

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (mod->type == mod_alias)
			Mod_LoadModel (mod, false);
	}

	Mod_RefreshSkins_f (var);
	R_RebuildAllEfrags ();
	InvalidateTraceLineCache ();
}

/*
===============
Mod_Init
===============
*/
void Mod_Init (void)
{
	Cvar_RegisterVariable (&external_vis);
	Cvar_RegisterVariable (&external_ents);
	Cvar_RegisterVariable (&wad_external_textures);
	Cvar_RegisterVariable (&mdl_external_textures);
	Cvar_RegisterVariable (&r_allow_replacement_md5models);
	Cvar_RegisterVariable (&r_allow_replacement_md3models);
	Cvar_RegisterVariable (&r_enhancedmodels);
	Cvar_SetCallback (&r_enhancedmodels, Mod_EnhancedModels_f);

	// johnfitz -- create notexture miptex
	r_notexture_mip = (texture_t *)Mem_Alloc (sizeof (texture_t));
	strcpy (r_notexture_mip->name, "notexture");
	r_notexture_mip->height = r_notexture_mip->width = 32;

	r_notexture_mip2 = (texture_t *)Mem_Alloc (sizeof (texture_t));
	strcpy (r_notexture_mip2->name, "notexture2");
	r_notexture_mip2->height = r_notexture_mip2->width = 32;
	// johnfitz
}

const char *MODEL_TYPE_STR (poseverttype_t kind)
{
	switch (kind)
	{
	case PV_QUAKE1:
		return "MDL";
		break;
	case PV_MD5:
	case PV_MD5_8:
		return "MD5";
		break;
	case PV_QUAKE3:
		return "MD3";
		break;
	default:
		return "(invalid)";
	}
}
/*
===============
Mod_Extradata_CheckSkin

Caches the data if needed
===============
*/
void *Mod_Extradata_CheckSkin (qmodel_t *mod, int skinnum)
{
	Mod_LoadModel (mod, true);

	if (mod->type != mod_alias)
		return mod->extradata[PV_QUAKE1];

	poseverttype_t valid_models_with_prio[PV_SIZE] = {0};
	int			   id_models_with_prio_size = 0;

	// 1. fill valid_models_with_prio in the order (MD3, MD5, MDL) selecting non-null extradata
	// there are probably smarter things to do but let's not over-engeneer this and trust the compiler instead
	for (size_t i = 0; i < PV_SIZE; i++)
	{
		if (mod->extradata[i] && (i == PV_QUAKE3))
		{
			valid_models_with_prio[id_models_with_prio_size++] = PV_QUAKE3;
			break;
		}
	}
	for (size_t i = 0; i < PV_SIZE; i++)
	{
		if (mod->extradata[i] && (i == PV_MD5))
		{
			valid_models_with_prio[id_models_with_prio_size++] = PV_MD5;
			break;
		}
	}
	for (size_t i = 0; i < PV_SIZE; i++)
	{
		if (mod->extradata[i] && (i == PV_QUAKE1))
		{
			valid_models_with_prio[id_models_with_prio_size++] = PV_QUAKE1;
			break;
		}
	}

	// by construction of Mod_LoadModel we only have 2 models at most
	assert (id_models_with_prio_size <= 2);

	byte *mdx_extradata = mod->extradata[valid_models_with_prio[0]];

	// 2. Only one model, return it whatever its kind.
	if (id_models_with_prio_size == 1)
		return mdx_extradata;

	// 3. Apply the dynamic rule MDL vs. MDX now:
	if (r_enhancedmodels.value && skinnum < ((aliashdr_t *)mdx_extradata)->numskins)
		return mdx_extradata;
	//
	return mod->extradata[PV_QUAKE1];
}

/*
===============
Mod_Extradata

Caches the data if needed
===============
*/
void *Mod_Extradata (qmodel_t *mod)
{
	return Mod_Extradata_CheckSkin (mod, 0);
}

static qboolean Mod_GetPinnedAxeEdge (qmodel_t *mod, int skinnum,
	const char *name, stockaxe_edge_t *out)
{
	aliashdr_t *selected;

	if (!out)
		return false;
	memset (out, 0, sizeof (*out));
	if (!mod || skinnum < 0 || !Mod_LoadModel (mod, false) ||
		mod->type != mod_alias || strcmp (mod->name, name))
		return false;

	selected = (aliashdr_t *)Mod_Extradata_CheckSkin (mod, skinnum);
	if (!selected || selected != (aliashdr_t *)mod->extradata[PV_QUAKE1] ||
		selected->poseverttype != PV_QUAKE1)
		return false;

	/* Only the source-pinned loaders set valid. Reload/free clears this record. */
	if (!mod->stockaxe_edge.valid)
		return false;

	*out = mod->stockaxe_edge;
	return true;
}

qboolean Mod_GetStockAxeEdge (qmodel_t *mod, int skinnum, stockaxe_edge_t *out)
{
	return Mod_GetPinnedAxeEdge (mod, skinnum, "progs/v_axe.mdl", out);
}

qboolean Mod_GetAlkalineAxeEdge (qmodel_t *mod, int skinnum, stockaxe_edge_t *out)
{
	return Mod_GetPinnedAxeEdge (mod, skinnum, "progs/v_alkaxe20fps.mdl", out);
}

qboolean Mod_GetQBJ3BerserkPalmCentroid (const qmodel_t *mod, int hand,
	int pose, vec3_t out)
{
	if (out)
		VectorCopy (vec3_origin, out);
	if (!out || !mod || mod->needload || mod->type != mod_alias ||
		strcmp (mod->name, "progs/v_berserk.mdl") ||
		!mod->qbj3_palm_centroids || hand < 0 || hand > 1 ||
		pose < 0 || pose >= mod->qbj3_palm_pose_count ||
		!mod->extradata[PV_QUAKE1])
		return false;
	VectorCopy (mod->qbj3_palm_centroids[hand * mod->qbj3_palm_pose_count + pose], out);
	return true;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *Mod_PointInLeaf (float *p, qmodel_t *model)
{
	mnode_t	 *node;
	float	  d;
	mplane_t *plane;

	if (!model || !model->nodes)
		Sys_Error ("Mod_PointInLeaf: bad model");

	node = model->nodes;
	while (1)
	{
		if (node->contents < 0)
			return (mleaf_t *)node;
		plane = node->plane;
		d = DotProduct (p, plane->normal) - plane->dist;
		if (d > 0)
			node = node->children[0];
		else
			node = node->children[1];
	}

	return NULL; // never reached
}

/*
===================
Mod_DecompressVis
===================
*/
byte *Mod_DecompressVis (byte *in, qmodel_t *model)
{
	int	  c;
	byte *out;
	byte *outend;
	int	  row;

	row = (model->numleafs + 31) / 8;
	if (mod_decompressed == NULL || row > mod_decompressed_capacity)
	{
		mod_decompressed_capacity = row;
		mod_decompressed = (byte *)Mem_Realloc (mod_decompressed, mod_decompressed_capacity);
		if (!mod_decompressed)
			Sys_Error ("Mod_DecompressVis: realloc() failed on %d bytes", mod_decompressed_capacity);
	}
	out = mod_decompressed;
	outend = mod_decompressed + row;

	if (!in)
	{ // no vis info, so make all visible
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
			c = row -
				(out -
				 mod_decompressed); // now that we're dynamically allocating pvs buffers, we have to be more careful to avoid heap overflows with buggy maps.
		while (c)
		{
			if (out == outend)
			{
				if (!model->viswarn)
				{
					model->viswarn = true;
					Con_Warning ("Mod_DecompressVis: output overrun on model \"%s\"\n", model->name);
				}
				return mod_decompressed;
			}
			*out++ = 0;
			c--;
		}
	} while (out - mod_decompressed < row);

	return mod_decompressed;
}

/*
===================
Mod_LeafPVS
===================
*/
byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model)
{
	if (leaf == model->leafs)
		return Mod_NoVisPVS (model);
	return Mod_DecompressVis (leaf->compressed_vis, model);
}

/*
===================
Mod_NoVisPVS
===================
*/
byte *Mod_NoVisPVS (qmodel_t *model)
{
	int pvsbytes;

	pvsbytes = (model->numleafs + 31) / 8;
	if (mod_novis == NULL || pvsbytes > mod_novis_capacity)
	{
		mod_novis_capacity = pvsbytes;
		mod_novis = (byte *)Mem_Realloc (mod_novis, mod_novis_capacity);
		if (!mod_novis)
			Sys_Error ("Mod_NoVisPVS: realloc() failed on %d bytes", mod_novis_capacity);
	}
	memset (mod_novis, 0xff, mod_novis_capacity);
	return mod_novis;
}

/*
===================
Mod_FreeSpriteMemory
===================
*/
static void Mod_FreeSpriteMemory (msprite_t *psprite)
{
	for (int i = 0; i < psprite->numframes; ++i)
	{
		if (psprite->frames[i].type == SPR_SINGLE)
		{
			SAFE_FREE (psprite->frames[i].frameptr);
		}
		else
		{
			mspritegroup_t *group = (mspritegroup_t *)psprite->frames[i].frameptr;
			for (int j = 0; j < group->numframes; ++j)
			{
				SAFE_FREE (group->frames[i]);
			}
			SAFE_FREE (psprite->frames[i].frameptr);
		}
	}
	psprite->numframes = 0;
}

/*
===================
Mod_FreeModelMemory
===================
*/
static void Mod_FreeModelMemory (qmodel_t *mod)
{
	mod->is_generated_akimbo_half = false;
	memset (&mod->stockaxe_edge, 0, sizeof (mod->stockaxe_edge));
	SAFE_FREE (mod->qbj3_palm_centroids);
	mod->qbj3_palm_pose_count = 0;

	if (mod->name[0] != '*')
	{
		if ((mod->type == mod_sprite) && (mod->extradata[PV_QUAKE1]))
			Mod_FreeSpriteMemory ((msprite_t *)mod->extradata[PV_QUAKE1]);
		// Last two ones are dummy textures
		for (int i = 0; i < mod->numtextures - 2; ++i)
			SAFE_FREE (mod->textures[i]);
		for (int i = 0; i < mod->numsurfaces; ++i)
			SAFE_FREE (mod->surfaces[i].polys);
		SAFE_FREE (mod->hulls[0].clipnodes);
		SAFE_FREE (mod->submodels);
		mod->numsubmodels = 0;
		SAFE_FREE (mod->planes);
		mod->numplanes = 0;
		SAFE_FREE (mod->leafs);
		mod->numleafs = 0;
		SAFE_FREE (mod->vertexes);
		mod->numvertexes = 0;
		SAFE_FREE (mod->edges);
		mod->numedges = 0;
		SAFE_FREE (mod->nodes);
		mod->numnodes = 0;
		SAFE_FREE (mod->texinfo);
		mod->numtexinfo = 0;
		SAFE_FREE (mod->surfaces);
		mod->numsurfaces = 0;
		SAFE_FREE (mod->surfedges);
		mod->numsurfedges = 0;
		SAFE_FREE (mod->clipnodes);
		mod->numclipnodes = 0;
		SAFE_FREE (mod->marksurfaces);
		mod->nummarksurfaces = 0;
		SAFE_FREE (mod->soa_leafbounds);
		SAFE_FREE (mod->surfvis);
		SAFE_FREE (mod->stereo_vis);
		SAFE_FREE (mod->soa_surfplanes);
		SAFE_FREE (mod->textures);
		mod->numtextures = 0;
		SAFE_FREE (mod->visdata);
		SAFE_FREE (mod->lightdata);
		SAFE_FREE (mod->entities);
		for (int i = 0; i < PV_SIZE; ++i)
			SAFE_FREE (mod->extradata[i]);
		SAFE_FREE (mod->md5_skeleton);
		SAFE_FREE (mod->water_surfs);
		mod->used_water_surfs = 0;
		mod->water_surfs_specials = 0;
	}
	else
		SAFE_FREE (mod->textures);

	if (!isDedicated)
		TexMgr_FreeTexturesForOwner (mod);
}

/*
===================
Mod_ClearAll
===================
*/
void Mod_ClearAll (void)
{
	int		  i;
	qmodel_t *mod;
	GL_DeleteBModelAccelerationStructures ();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (mod->type != mod_alias)
		{
			mod->needload = true;
			Mod_FreeModelMemory (mod); // johnfitz
		}
	}

	InvalidateTraceLineCache ();
}

/*
===================
Mod_ResetAll
===================
*/
void Mod_ResetAll (void)
{
	int		  i;
	qmodel_t *mod;

	// ericw -- free alias model VBOs
	GLMesh_DeleteAllMeshBuffers ();
	GL_DeleteBModelAccelerationStructures ();

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->needload) // otherwise Mod_ClearAll() did it already
			Mod_FreeModelMemory (mod);

		memset (mod, 0, sizeof (qmodel_t));
	}
	mod_numknown = 0;

	InvalidateTraceLineCache ();
}

/*
==================
Mod_FindName

==================
*/
qmodel_t *Mod_FindName (const char *name)
{
	int		  i;
	qmodel_t *mod;

	if (!name[0])
		Sys_Error ("Mod_FindName: NULL name"); // johnfitz -- was "Mod_ForName"

	//
	// search the currently loaded models
	//
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
		if (!strcmp (mod->name, name))
			break;

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MODELS)
			Sys_Error ("mod_numknown == MAX_MODELS");
		q_strlcpy (mod->name, name, MAX_QPATH);
		mod->needload = true;
		mod->is_generated_akimbo_half = false;
		mod_numknown++;
		InvalidateTraceLineCache ();
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
	Mod_FindName (name);
}

typedef struct
{
	mod_akimbo_pair_recipe_t recipe;
	qbj3_mdl_weapon_t weapon;
} mod_akimbo_pair_t;

static const mod_akimbo_pair_t mod_akimbo_pairs[] = {
	{{"qbj3", "progs/v_tnailgun.mdl",
		{"vr/qbj3/progs/v_tnailgun_vr_left.mdl", "vr/qbj3/progs/v_tnailgun_vr_right.mdl"},
		19, 1968, {988, 980},
		{{54.75913167f, 10.28241703f, -16.05048694f},
		 {54.75913167f, -10.49037877f, -16.05048694f}}}, QBJ3_MDL_WEAPON_NAIL},
	{{"qbj3", "progs/v_berserk.mdl",
		{"vr/qbj3/progs/v_berserk_vr_left.mdl", "vr/qbj3/progs/v_berserk_vr_right.mdl"},
		101, 894, {447, 447},
		{{25.58522001f, 14.90800858f, -4.91985899f},
		 {25.58522001f, -15.31243134f, -4.91985899f}}}, QBJ3_MDL_WEAPON_BERSERK},
	{{"enyo", "progs/ee_v_smgs.mdl",
		{"vr/enyo/progs/ee_v_smgs_vr_left.mdl", "vr/enyo/progs/ee_v_smgs_vr_right.mdl"},
		17, 984, {492, 492},
		{{64.10965419f, 19.31388339f, -13.71730390f},
		 {64.10965419f, -19.51840544f, -13.71730390f}}}, ENYO_MDL_WEAPON_SMG},
	{{"dwell", "progs/v_axeb.mdl",
		{"vr/dwell/progs/v_axeb_vr_left.mdl", "vr/dwell/progs/v_axeb_vr_right.mdl"},
		51, 304, {152, 152},
		{{33.219191864f, 14.249626011f, -17.862335034f},
		 {37.226840504f, -13.465485394f, -18.066500630f}}}, DWELL_MDL_WEAPON_BERSERK}
};

/* Recipes copied from the OpenVR donor's vr_immersive_melee_profiles.
 * Both meshes keep their source QC identity and reuse the existing splitter. */
static const mod_held_melee_recipe_t mod_held_melee_recipes[] = {
	{"qbj3", "progs/v_wrench.mdl", "vr/qbj3/progs/v_wrench_vr_dominant.mdl",
	 VR_WEAPON_CONTACT_PROFILE_QBJ3, QBJ3_MDL_WEAPON_WRENCH_DOMINANT,
	 872, 868, 565, 540, 71, 10, 702244, 0x1bfff189u, {320, 358},
	 true, true, {158.428571f, 151.0f, 148.714286f}, 70.0f},
	{"enyo", "progs/ee_v_sword.mdl", "vr/enyo/progs/ee_v_sword_vr_dominant.mdl",
	 VR_WEAPON_CONTACT_PROFILE_ENYO, ENYO_MDL_WEAPON_KATANA_DOMINANT,
	 1021, 1039, 669, 679, 35, 0, 344020, 0xa707a071u, {13, 77},
	 false, false, {0.0f, 0.0f, 0.0f}, 0.0f}
};

const mod_held_melee_recipe_t *Mod_GetHeldMeleeRecipe (const char *source)
{
	for (size_t i = 0; source && i < countof (mod_held_melee_recipes); ++i)
		if (!strcmp (source, mod_held_melee_recipes[i].source) &&
			!q_strcasecmp (COM_SkipPath (com_gamedir), mod_held_melee_recipes[i].game))
			return &mod_held_melee_recipes[i];
	return NULL;
}

static const mod_held_melee_recipe_t *Mod_HeldMeleeRecipeForName (const char *name)
{
	for (size_t i = 0; name && i < countof (mod_held_melee_recipes); ++i)
		if (!strcmp (name, mod_held_melee_recipes[i].held) &&
			!q_strcasecmp (COM_SkipPath (com_gamedir), mod_held_melee_recipes[i].game))
			return &mod_held_melee_recipes[i];
	return NULL;
}

static const mod_akimbo_pair_t *Mod_AkimboPairForHalf (const char *name, int *hand_out)
{
	if (strncmp (name, "vr/", 3))
		return NULL;
	for (int weapon = 0; weapon < countof (mod_akimbo_pairs); ++weapon)
		for (int hand = 0; hand < 2; ++hand)
			if (!strcmp (name, mod_akimbo_pairs[weapon].recipe.halves[hand]))
			{
				if (hand_out)
					*hand_out = hand;
				return &mod_akimbo_pairs[weapon];
			}
	return NULL;
}

static qboolean Mod_AkimboPairGameMatches (const mod_akimbo_pair_t *pair)
{
	return !q_strcasecmp (COM_SkipPath (com_gamedir), pair->recipe.game) ||
		(pair->weapon == DWELL_MDL_WEAPON_BERSERK &&
			!q_strcasecmp (COM_SkipPath (com_gamedir), "dwellv2p2"));
}

const mod_akimbo_pair_recipe_t *Mod_GetAkimboPairRecipe (const char *source)
{
	if (!source)
		return NULL;
	for (int weapon = 0; weapon < countof (mod_akimbo_pairs); ++weapon)
	{
		const mod_akimbo_pair_t *pair = &mod_akimbo_pairs[weapon];
		if (!strcmp (source, pair->recipe.source) && Mod_AkimboPairGameMatches (pair))
			return &pair->recipe;
	}
	return NULL;
}

qboolean Mod_GetAkimboPairPaths (const char *source, const char *half_paths[2])
{
	if (!half_paths)
		return false;
	half_paths[0] = NULL;
	half_paths[1] = NULL;
	if (!source)
		return false;

	const mod_akimbo_pair_recipe_t *recipe = Mod_GetAkimboPairRecipe (source);
	if (!recipe)
		return false;
	half_paths[0] = recipe->halves[0];
	half_paths[1] = recipe->halves[1];
	return true;
}

qboolean Mod_AkimboPairUsesGeneratedHalves (const char *source)
{
	const char *half_paths[2];
	if (!Mod_GetAkimboPairPaths (source, half_paths))
		return false;
	/* Inspect overrides at the main-thread preparation boundary only. A
	 * topology-compatible custom half may need different contact anchors. */
	for (int hand = 0; hand < 2; ++hand)
	{
		unsigned int override_path_id = 0;
		if (COM_FileExists (half_paths[hand], &override_path_id) &&
			override_path_id > 1)
			return false;
	}
	return true;
}

/* Generate pairs and single held meshes with the same source/provenance rules. */
static byte *Mod_GenerateVRHeldModel (const char *name,
	unsigned int *source_path_id, size_t *generated_size,
	const char **skin_source)
{
	const mod_akimbo_pair_t *pair;
	const mod_held_melee_recipe_t *held = Mod_HeldMeleeRecipeForName (name);
	const char *source_name;
	unsigned int override_path_id = 0;
	unsigned int selected_source_path_id = 0;
	qfilesize_t source_file_size;
	qbj3_mdl_side_t side;
	byte *source, *output = NULL;
	size_t output_size = 0;
	int hand, result;

	*generated_size = 0;
	*skin_source = NULL;
	if (isDedicated)
		return NULL;

	pair = held ? NULL : Mod_AkimboPairForHalf (name, &hand);
	if (!held && (!pair || !Mod_AkimboPairGameMatches (pair)))
		return NULL;
	source_name = held ? held->source : pair->recipe.source;

	/* Files in a higher priority search path are explicit private-model overrides. */
	if (COM_FileExists (name, &override_path_id) && override_path_id > 1)
		return NULL;

	source = COM_LoadFile (source_name, &selected_source_path_id);
	if (!source)
		return NULL;
	source_file_size = com_filesize;
	if (source_file_size < 0 || (uint64_t)source_file_size > (uint64_t)SIZE_MAX)
	{
		Mem_Free (source);
		return NULL;
	}
	side = held ? QBJ3_MDL_SIDE_DOMINANT :
		(hand == 0 ? QBJ3_MDL_SIDE_LEFT : QBJ3_MDL_SIDE_RIGHT);
	result = QBJ3_MDL_Split (source, (size_t)source_file_size,
		held ? held->split_weapon : pair->weapon,
		side, &output, &output_size);
	Mem_Free (source);
	if (result != 1)
	{
		free (output);
		return NULL;
	}

	*source_path_id = selected_source_path_id;
	*generated_size = output_size;
	*skin_source = source_name;
	return output;
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
static qmodel_t *Mod_LoadModel (qmodel_t *mod, qboolean crash)
{
	int mod_type;
	byte *generated_buf;
	qboolean generated_model = false;
	size_t generated_size = 0;
	unsigned int generated_path_id = 0;
	const char *skin_source = NULL;

	if (!mod->needload)
		return mod;
	mod->is_generated_akimbo_half = false;
	SAFE_FREE (mod->qbj3_palm_centroids);
	mod->qbj3_palm_pose_count = 0;

	/* The copied edge belongs to this exact load of the source model. */
	memset (&mod->stockaxe_edge, 0, sizeof (mod->stockaxe_edge));

	InvalidateTraceLineCache ();

	if (mod->type == mod_alias)
	{
		for (int i = 0; i < PV_SIZE; ++i)
		{
			GLMesh_DeleteMeshBuffers ((aliashdr_t *)mod->extradata[i]);
		}
	}

	// load the model file, together with replacement overrides for .mdl, if they are available.
	// 0 is an invalid path_id, starts at 1 for existing files:
	unsigned int md5_enhanced_path_id = 0;
	unsigned int md3_enhanced_path_id = 0;

	byte *buf = NULL;
	qfilesize_t buf_filesize = -1;

	char md3_name[MAX_QPATH], md5_name[MAX_QPATH];

	// 1. Load the original model buffer:
	buf = COM_LoadFile (mod->name, &mod->path_id);
	buf_filesize = com_filesize;
	generated_buf = Mod_GenerateVRHeldModel (mod->name, &generated_path_id,
		&generated_size, &skin_source);
	if (generated_buf)
	{
		if (buf)
			Mem_Free (buf);
		buf = generated_buf;
		buf_filesize = (qfilesize_t)generated_size;
		mod->path_id = generated_path_id;
		generated_model = true;
	}

	if (!buf)
	{
		if (crash)
			Host_Error ("Mod_LoadModel: %s not found", mod->name); // johnfitz -- was "Mod_NumForName"
		return NULL;
	}

	const bool mod_is_mdl = (strcmp (COM_FileGetExtension (mod->name), "mdl") == 0);
	const bool load_enhanced_model = mod_is_mdl && r_enhancedmodels.value &&
		!Mod_AkimboPairForHalf (mod->name, NULL) &&
		!Mod_HeldMeleeRecipeForName (mod->name);

	// 2. Find MDL "enhanced" complementary models, if any:
	if (load_enhanced_model && r_allow_replacement_md3models.value)
	{
		// newname is the .mdl model with extension changed to .md3:
		COM_StripExtension (mod->name, md3_name, sizeof (md3_name));
		COM_AddExtension (md3_name, ".md3", sizeof (md3_name));

		// Search for the file but do not load it:
		//   look for it in the filesystem or pack files
		if (!COM_FileExists (md3_name, &md3_enhanced_path_id))
			md3_enhanced_path_id = 0; // file not found

		// this is a replacement only if its priority is >= MDL one, else discard it
		if (md3_enhanced_path_id < mod->path_id)
		{
			md3_enhanced_path_id = 0;
		}
	}

	if (load_enhanced_model && r_allow_replacement_md5models.value)
	{
		// newname is the .mdl model with extension changed to .md5mesh:
		COM_StripExtension (mod->name, md5_name, sizeof (md5_name));
		COM_AddExtension (md5_name, ".md5mesh", sizeof (md5_name));

		// Search for the file but do not load it:
		//   look for it in the filesystem or pack files
		if (!COM_FileExists (md5_name, &md5_enhanced_path_id))
			md5_enhanced_path_id = 0; // file not found

		// this is a replacement only if its priority is >= MDL one, else discard it
		if (md5_enhanced_path_id < mod->path_id)
		{
			md5_enhanced_path_id = 0;
		}
	}

	// 3. If there are multiple replacement models (MD3 + MD5) only keep the one with the highest prio
	//  in case of equality, MD3 wins.
	if (md3_enhanced_path_id && md5_enhanced_path_id)
	{
		if (md5_enhanced_path_id > md3_enhanced_path_id)
			md3_enhanced_path_id = 0;
		else
			md5_enhanced_path_id = 0;
	}

	// 4. Load the (unique) selected complementary model :
	if (md3_enhanced_path_id)
	{
		byte		*md3_buf = COM_LoadFile (md3_name, &md3_enhanced_path_id);
		qfilesize_t md3_size = com_filesize;
		// To assure that the external resources associated with MD3
		// are properly filtered/loaded, we need to set mod->path_id = md3_enhanced_path_id temporarilly
		unsigned int original_path_id = mod->path_id;
		mod->path_id = md3_enhanced_path_id;
		Mod_LoadMD3Model (mod, md3_buf, md3_size);
		mod->path_id = original_path_id;
		Mem_Free (md3_buf);
	}
	else if (md5_enhanced_path_id)
	{
		byte		*md5_buf = COM_LoadFile (md5_name, &md5_enhanced_path_id);
		qfilesize_t md5_size = com_filesize;
		// To assure that the external resources associated with MD5
		// are properly filtered/loaded, we need to set mod->path_id = md5_enhanced_path_id temporarilly
		unsigned int original_path_id = mod->path_id;
		mod->path_id = md5_enhanced_path_id;
		Mod_LoadMD5MeshModel (mod, md5_buf, md5_name, md5_size);
		mod->path_id = original_path_id;
		Mem_Free (md5_buf);
	}

	// 5. Finally, Load the original model, calling the appropriate loader:
	mod->needload = false;

	if (buf_filesize < (qfilesize_t)sizeof (int))
		Sys_Error ("Mod_LoadModel: %s has a truncated file header", mod->name);

	mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	switch (mod_type)
	{
	case IDPOLYHEADER:
		Mod_LoadAliasModel (mod, buf, buf_filesize, skin_source);
		if (generated_model && mod->type == mod_alias)
			/* Single held meshes share this provenance bit; consumers also
			 * check their exact recipe, topology and cached edge. */
			mod->is_generated_akimbo_half = true;
		break;

	case IDSPRITEHEADER:
		Mod_LoadSpriteModel (mod, buf);
		break;

	//
	case IDMD5HEADER:
	{
		// by construction this is a "native" MD5 model, NOT a .mdl replacement so md5_enhanced_path_id = 0 here
		assert (md5_enhanced_path_id == 0);
		if (!Mod_LoadMD5MeshModel (mod, (const void *)buf, mod->name,
			buf_filesize))
			Sys_Error ("Mod_LoadModel: failed to load %s", mod->name);
	}
	break;

	//
	case IDMD3HEADER:
	{
		// by construction this is a "native" MD3 model, NOT a .mdl replacement so md3_enhanced_path_id = 0 here
		assert (md3_enhanced_path_id == 0);
		Mod_LoadMD3Model (mod, (const void *)buf, buf_filesize);
	}
	break;

	default:
	{
		char loadname[MAX_QPATH];
		COM_FileBase (mod->name, loadname, sizeof (loadname));
		Mod_LoadBrushModel (mod, loadname, buf);
	}
	break;
	}

	if (generated_model)
		free (buf);
	else
		Mem_Free (buf);
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
	qmodel_t *mod;

	mod = Mod_FindName (name);

	return Mod_LoadModel (mod, crash);
}

/*
===============================================================================

					BRUSHMODEL LOADING

===============================================================================
*/

/*
=============
Mod_LoadWadFiles

load all of the wads listed in the worldspawn "wad" field
=============
*/
static wad_t *Mod_LoadWadFiles (qmodel_t *mod)
{
	char		key[128], value[4096];
	const char *data;

	if (!wad_external_textures.value)
		return NULL;

	// disregard if this isn't the world model
	if (strcmp (mod->name, sv.modelname))
		return NULL;

	data = COM_Parse (mod->entities);
	if (!data)
		return NULL; // error
	if (com_token[0] != '{')
		return NULL; // error
	while (1)
	{
		data = COM_Parse (data);
		if (!data)
			return NULL; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy (key, com_token + 1, sizeof (key));
		else
			q_strlcpy (key, com_token, sizeof (key));
		while (key[0] && key[strlen (key) - 1] == ' ') // remove trailing spaces
			key[strlen (key) - 1] = 0;
		data = COM_ParseEx (data, CPE_ALLOWTRUNC);
		if (!data)
			return NULL; // error
		q_strlcpy (value, com_token, sizeof (value));

		if (!strcmp ("wad", key))
		{
			return W_LoadWadList (value);
		}
	}
	return NULL;
}

/*
=================
Mod_LoadWadTexture

look for an external texture in any of the loaded map wads
=================
*/
static texture_t *Mod_LoadWadTexture (qmodel_t *mod, wad_t *wads, const char *name)
{
	int			   i, pixels;
	lumpinfo_t	  *info;
	wad_t		  *wad;
	miptex_t	   mt;
	texture_t	  *tx;
	qboolean	   pal;
	unsigned short colors;

	// look for the lump in any of the loaded wads
	info = W_GetLumpinfoList (wads, name, &wad);

	// ensure we're dealing with a miptex
	if (!info || (info->type != TYP_MIPTEX && (wad->id != WADID_VALVE || info->type != TYP_MIPTEX_PALETTE)))
	{
		Con_Warning ("Missing external texture '%s' in wads, using BSP\n", name);
		return NULL;
	}

	// override the texture from the bsp file
	FS_fseek (&wad->fh, info->filepos, SEEK_SET);
	FS_fread (&mt, 1, sizeof (miptex_t), &wad->fh);

	mt.width = LittleLong (mt.width);
	mt.height = LittleLong (mt.height);
	for (i = 0; i < MIPLEVELS; i++)
		mt.offsets[i] = LittleLong (mt.offsets[i]);

	if (mt.width == 0 || mt.height == 0)
	{
		Con_Warning ("Zero sized texture %s in %s!\n", mt.name, wad->name);
		return NULL;
	}

	pal = wad->id == WADID_VALVE && info->type == TYP_MIPTEX_PALETTE;

	pixels = mt.width * mt.height / 64 * 85;
	// valve textures have a color palette immediately following the pixels
	if (pal)
	{
		if ((pixels + 2) <= info->size)
		{
			// the palette is basically garunteed to be 256 colors but,
			// we might as well use the value since it *does* exist
			FS_fseek (&wad->fh, info->filepos + pixels, SEEK_SET);
			FS_fread (&colors, 1, 2, &wad->fh);
			colors = LittleShort (colors);
			// add space for the color palette
			pixels += colors * 3;
		}
		// add space for the color count
		pixels += 2;
	}
	tx = (texture_t *)Mem_Alloc (sizeof (texture_t) + pixels);

	memcpy (tx->name, mt.name, sizeof (tx->name));
	tx->width = mt.width;
	tx->height = mt.height;
	for (i = 0; i < MIPLEVELS; i++)
		tx->offsets[i] = mt.offsets[i] + sizeof (texture_t) - sizeof (miptex_t);
	// the pixels immediately follow the structures

	// check for pixels extending past the end of the lump
	if (pixels > info->size)
	{
		Con_DPrintf ("Texture %s extends past end of lump\n", mt.name);
		pixels = info->size;
	}
	tx->source_file[0] = 0;
	tx->source_offset = (src_offset_t)(tx + 1);

	Atomic_StoreUInt32 (&tx->update_warp, false); // johnfitz
	tx->warpimage = NULL;						  // johnfitz
	tx->fullbright = NULL;						  // johnfitz
	tx->shift = 0;								  // Q64 only
	tx->palette = pal;

	FS_fseek (&wad->fh, info->filepos + sizeof (miptex_t), SEEK_SET);
	FS_fread (tx + 1, 1, pixels, &wad->fh);

	return tx;
}

/*
=================
Mod_CheckFullbrights -- johnfitz
=================
*/
qboolean Mod_CheckFullbrights (byte *pixels, int count)
{
	int i;
	for (i = 0; i < count; i++)
		if (*pixels++ > 223)
			return true;
	return false;
}

/*
=================
Mod_CheckFullbrightsValve
=================
*/
static qboolean Mod_CheckFullbrightsValve (char *name, byte *pixels, int count)
{
	if (name[0] == '~' || (name[2] == '~' && name[0] == '+'))
		return Mod_CheckFullbrights (pixels, count);
	return false;
}

/*
=================
Mod_CheckAnimTextureArrayQ64

Quake64 bsp
Check if we have any missing textures in the array
=================
*/
qboolean Mod_CheckAnimTextureArrayQ64 (texture_t *anims[], int numTex)
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
================
Mod_TextureTypeFromName
================
*/
static textype_t Mod_TextureTypeFromName (const char *texname)
{
	if (texname[0] == '*' || texname[0] == '!')
	{
		if (!strncmp (texname + 1, "lava", 4))
			return TEXTYPE_LAVA;
		if (!strncmp (texname + 1, "slime", 5))
			return TEXTYPE_SLIME;
		if (!strncmp (texname + 1, "tele", 4))
			return TEXTYPE_TELE;
		return TEXTYPE_WATER;
	}

	if (texname[0] == '{')
		return TEXTYPE_CUTOUT;

	if (!q_strncasecmp (texname, "sky", 3))
		return TEXTYPE_SKY;

	return TEXTYPE_DEFAULT;
}

/*
=================
Mod_LoadTextureTask
=================
*/
static void Mod_LoadTextureTask (int i, qmodel_t **ppmod)
{
	qmodel_t  *mod = *ppmod;
	texture_t *tx = mod->textures[i];
	if (!tx)
		return;

	int	  pixels = tx->width * tx->height / 64 * 85;
	char  texturename[64];
	int	  fwidth, fheight;
	char  filename[MAX_OSPATH], mapname[MAX_OSPATH];
	byte *data = NULL;
	bool  fbright;

	// Only filter out external textures for static models, not the level:
	const unsigned int effective_min_path_id = (mod->is_worldmodel ? 0 : mod->path_id);

#ifdef BSP29_VALVE
	if (mod->bspversion != BSPVERSION_VALVE && !q_strncasecmp (tx->name, "sky", 3))
#else
	if (!q_strncasecmp (tx->name, "sky", 3)) // sky texture //also note -- was strncmp, changed to match qbsp
#endif
	{
		if (mod->bspversion == BSPVERSION_QUAKE64)
			Sky_LoadTextureQ64 (mod, tx, i);
		else
			Sky_LoadTexture (mod, tx, i);
	}
	else if (tx->name[0] == '*' || tx->name[0] == '!') // warping texture
	{
		// external textures -- first look in "textures/mapname/" then look in "textures/"
		COM_StripExtension (mod->name + 5, mapname, sizeof (mapname));
		q_snprintf (filename, sizeof (filename), "textures/%s/#%s", mapname, tx->name + 1); // this also replaces the '*' with a '#'
		enum srcformat fmt = SRC_RGBA;
		data = Image_LoadImage (filename, &fwidth, &fheight, &fmt, effective_min_path_id);
		if (!data)
		{
			q_snprintf (filename, sizeof (filename), "textures/#%s", tx->name + 1);
			data = Image_LoadImage (filename, &fwidth, &fheight, &fmt, effective_min_path_id);
		}

		// now load whatever we found
		if (data) // load external image
		{
			q_strlcpy (texturename, filename, sizeof (texturename));
			tx->gltexture = TexMgr_LoadImage (mod, texturename, fwidth, fheight, fmt, data, filename, 0, TEXPREF_NONE);
		}
		else // use the texture from the bsp file
		{
			q_snprintf (texturename, sizeof (texturename), "%s:%s", mod->name, tx->name);
			fmt = SRC_INDEXED;
			if (tx->palette)
				fmt = SRC_INDEXED_PALETTE;
			tx->gltexture = TexMgr_LoadImage (mod, texturename, tx->width, tx->height, fmt, (byte *)(tx + 1), tx->source_file, tx->source_offset, TEXPREF_NONE);
		}

		// now create the warpimage, using dummy data from the hunk to create the initial image
		q_snprintf (texturename, sizeof (texturename), "%s_warp", texturename);
		tx->warpimage = TexMgr_LoadImage (mod, texturename, WARPIMAGESIZE, WARPIMAGESIZE, SRC_RGBA, NULL, "", 0, TEXPREF_NOPICMIP | TEXPREF_WARPIMAGE);
		Atomic_StoreUInt32 (&tx->update_warp, true);
	}
	else // regular texture
	{
		// ericw -- fence textures
		int extraflags;

		extraflags = 0;
		if (tx->name[0] == '{')
			extraflags |= TEXPREF_ALPHA;
		// ericw

		// external textures -- first look in "textures/mapname/" then look in "textures/"
		COM_StripExtension (mod->name + 5, mapname, sizeof (mapname));
		q_snprintf (filename, sizeof (filename), "textures/%s/%s", mapname, tx->name);
		enum srcformat fmt = SRC_RGBA;
		data = Image_LoadImage (filename, &fwidth, &fheight, &fmt, effective_min_path_id);
		if (!data)
		{
			q_snprintf (filename, sizeof (filename), "textures/%s", tx->name);
			data = Image_LoadImage (filename, &fwidth, &fheight, &fmt, effective_min_path_id);
		}

		// now load whatever we found
		if (data) // load external image
		{
			char filename2[MAX_OSPATH];

			tx->gltexture = TexMgr_LoadImage (mod, filename, fwidth, fheight, fmt, data, filename, 0, TEXPREF_MIPMAP | extraflags);
			Mem_Free (data);

			// now try to load glow/luma image from the same place
			q_snprintf (filename2, sizeof (filename2), "%s_glow", filename);
			data = Image_LoadImage (filename2, &fwidth, &fheight, &fmt, effective_min_path_id);
			if (!data)
			{
				q_snprintf (filename2, sizeof (filename2), "%s_luma", filename);
				data = Image_LoadImage (filename2, &fwidth, &fheight, &fmt, effective_min_path_id);
			}

			if (data)
				tx->fullbright = TexMgr_LoadImage (mod, filename2, fwidth, fheight, fmt, data, filename2, 0, TEXPREF_MIPMAP | extraflags);
		}
		else // use the texture from the bsp file
		{
			q_snprintf (texturename, sizeof (texturename), "%s:%s", mod->name, tx->name);
			if (tx->palette)
			{
				fmt = SRC_INDEXED_PALETTE;
				fbright = Mod_CheckFullbrightsValve (tx->name, (byte *)(tx + 1), pixels);
			}
			else
			{
				fmt = SRC_INDEXED;
				fbright = Mod_CheckFullbrights ((byte *)(tx + 1), pixels);
			}
			if (fbright)
			{
				tx->gltexture = TexMgr_LoadImage (
					mod, texturename, tx->width, tx->height, fmt, (byte *)(tx + 1), tx->source_file, tx->source_offset,
					TEXPREF_MIPMAP | TEXPREF_NOBRIGHT | extraflags);
				q_snprintf (texturename, sizeof (texturename), "%s:%s_glow", mod->name, tx->name);
				tx->fullbright = TexMgr_LoadImage (
					mod, texturename, tx->width, tx->height, fmt, (byte *)(tx + 1), tx->source_file, tx->source_offset,
					TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT | extraflags);
			}
			else
			{
				tx->gltexture = TexMgr_LoadImage (
					mod, texturename, tx->width, tx->height, fmt, (byte *)(tx + 1), tx->source_file, tx->source_offset, TEXPREF_MIPMAP | extraflags);
			}
		}
	}
	Mem_Free (data);
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		   i, j, pixels, num, maxanim, altmax;
	miptex_t   mt;
	texture_t *tx, *tx2;
	texture_t *anims[10];
	texture_t *altanims[10];
	byte	  *m;
	byte	  *pixels_p;
	int		   nummiptex;
	int		   dataofs;
	wad_t	  *wads;
#ifdef BSP29_VALVE
	qboolean	   pal;
	unsigned short colors;
#endif

	// johnfitz -- don't return early if no textures; still need to create dummy texture
	if (!l->filelen)
	{
		Con_Printf ("Mod_LoadTextures: no textures in bsp file\n");
		nummiptex = 0;
		m = NULL; // avoid bogus compiler warning
	}
	else
	{
		m = mod_base + l->fileofs;
		nummiptex = ReadLongUnaligned (m + offsetof (dmiptexlump_t, nummiptex));
	}
	// johnfitz

	mod->numtextures = nummiptex + 2; // johnfitz -- need 2 dummy texture chains for missing textures
	mod->textures = (texture_t **)Mem_Alloc (mod->numtextures * sizeof (*mod->textures));

	// load any wads this map may need to load external textures from
	wads = Mod_LoadWadFiles (mod);

#ifdef BSP29_VALVE
	pal = mod->bspversion == BSPVERSION_VALVE;
#endif

	for (i = 0; i < nummiptex; i++)
	{
		dataofs = ReadLongUnaligned (m + offsetof (dmiptexlump_t, dataofs[i]));
		if (dataofs == -1)
			continue;
		memcpy (&mt, m + dataofs, sizeof (miptex_t));
		mt.width = LittleLong (mt.width);
		mt.height = LittleLong (mt.height);
		for (j = 0; j < MIPLEVELS; j++)
			mt.offsets[j] = LittleLong (mt.offsets[j]);

		if (mt.width == 0 || mt.height == 0)
		{
			Con_Warning ("Zero sized texture %s in %s!\n", mt.name, mod->name);
			continue;
		}

		// an offset of zero indicates an external texture
		if (mt.offsets[0] == 0)
		{
			mod->textures[i] = Mod_LoadWadTexture (mod, wads, mt.name);
			// Mod_LoadWadTexture trust the .wad name in bsp, but its loading may
			//  fail anyway, so try with regular internal .bsp texture loading as fallback:
			if (mod->textures[i])
			{
				// external texture loading success, skip the regular internal .bsp texture loading below:
				continue;
			}
		}

		pixels = mt.width * mt.height / 64 * 85;
		pixels_p = m + dataofs + sizeof (miptex_t);
#ifdef BSP29_VALVE
		// valve textures have a color palette immediately following the pixels
		if (pal)
		{
			if ((pixels_p + pixels + 2) <= (mod_base + l->fileofs + l->filelen))
			{
				// the palette is basically garunteed to be 256 colors but,
				// we might as well use the value since it *does* exist
				memcpy (&colors, pixels_p + pixels, 2);
				colors = LittleShort (colors);
				// add space for the color palette
				pixels += colors * 3;
			}
			// add space for the color count
			pixels += 2;
		}
#endif
		tx = (texture_t *)Mem_Alloc (sizeof (texture_t) + pixels);
		mod->textures[i] = tx;

		memcpy (tx->name, mt.name, sizeof (tx->name));
		tx->width = mt.width;
		tx->height = mt.height;
		tx->type = Mod_TextureTypeFromName (tx->name);
		for (j = 0; j < MIPLEVELS; j++)
			tx->offsets[j] = mt.offsets[j] + sizeof (texture_t) - sizeof (miptex_t);
		// the pixels immediately follow the structures

		// ericw -- check for pixels extending past the end of the lump.
		// appears in the wild; e.g. jam2_tronyn.bsp (func_mapjam2),
		// kellbase1.bsp (quoth), and can lead to a segfault if we read past
		// the end of the .bsp file buffer
		if ((pixels_p + pixels) > (mod_base + l->fileofs + l->filelen))
		{
			Con_DPrintf ("Texture %s extends past end of lump\n", mt.name);
			pixels = q_max (0, (mod_base + l->fileofs + l->filelen) - pixels_p);
		}
		q_strlcpy (tx->source_file, mod->name, sizeof (tx->source_file));
		tx->source_offset = (src_offset_t)pixels_p - (src_offset_t)mod_base;

		Atomic_StoreUInt32 (&tx->update_warp, false); // johnfitz
		tx->warpimage = NULL;						  // johnfitz
		tx->fullbright = NULL;						  // johnfitz
		tx->shift = 0;								  // Q64 only
#ifdef BSP29_VALVE
		tx->palette = pal;
#else
		tx->palette = false;
#endif

		if (mod->bspversion != BSPVERSION_QUAKE64)
		{
			memcpy (tx + 1, pixels_p, pixels);
		}
		else
		{ // Q64 bsp
			tx->shift = ReadLongUnaligned (m + dataofs + offsetof (miptex64_t, shift));
			memcpy (tx + 1, m + dataofs + sizeof (miptex64_t), pixels);
		}
	}

	// we no longer need the wads after this point
	W_FreeWadList (wads);

	if (!isDedicated)
	{
		if (!Tasks_IsWorker () && (nummiptex > 1))
		{
			task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_LoadTextureTask, nummiptex, &mod, sizeof (mod));
			Task_Join (task, TASK_TIMEOUT_INFINITE);
		}
		else
		{
			for (i = 0; i < nummiptex; i++)
				Mod_LoadTextureTask (i, &mod);
		}
	}

	// johnfitz -- last 2 slots in array should be filled with dummy textures
	mod->textures[mod->numtextures - 2] = r_notexture_mip;	// for lightmapped surfs
	mod->textures[mod->numtextures - 1] = r_notexture_mip2; // for SURF_DRAWTILED surfs

	//
	// sequence the animations
	//
	for (i = 0; i < nummiptex; i++)
	{
		tx = mod->textures[i];
		if (!tx || tx->name[0] != '+')
			continue;
		if (tx->anim_next)
			continue; // allready sequenced

		// find the number of frames in the animation
		memset (anims, 0, sizeof (anims));
		memset (altanims, 0, sizeof (altanims));

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

		for (j = i + 1; j < nummiptex; j++)
		{
			tx2 = mod->textures[j];
			if (!tx2 || tx2->name[0] != '+')
				continue;
			if (strcmp (tx2->name + 2, tx->name + 2))
				continue;

			num = tx2->name[1];
			if (num >= 'a' && num <= 'z')
				num -= 'a' - 'A';
			if (num >= '0' && num <= '9')
			{
				num -= '0';
				anims[num] = tx2;
				if (num + 1 > maxanim)
					maxanim = num + 1;
			}
			else if (num >= 'A' && num <= 'J')
			{
				num = num - 'A';
				altanims[num] = tx2;
				if (num + 1 > altmax)
					altmax = num + 1;
			}
			else
				Sys_Error ("Bad animating texture %s", tx->name);
		}

		if (mod->bspversion == BSPVERSION_QUAKE64 && !Mod_CheckAnimTextureArrayQ64 (anims, maxanim))
			continue; // Just pretend this is a normal texture

#define ANIM_CYCLE 2
		// link them all together
		for (j = 0; j < maxanim; j++)
		{
			tx2 = anims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s", j, tx->name);
			tx2->anim_total = maxanim * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = anims[(j + 1) % maxanim];
			if (altmax)
				tx2->alternate_anims = altanims[0];
		}
		for (j = 0; j < altmax; j++)
		{
			tx2 = altanims[j];
			if (!tx2)
				Sys_Error ("Missing frame %i of %s", j, tx->name);
			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = altanims[(j + 1) % altmax];
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
static void Mod_LoadLighting (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int			 i;
	byte		*in, *out, *data;
	byte		 d, q64_b0, q64_b1;
	char		 litfilename[MAX_OSPATH];
	unsigned int path_id;

	mod->lightdata = NULL;
	// LordHavoc: check for a .lit file
	q_strlcpy (litfilename, mod->name, sizeof (litfilename));
	COM_StripExtension (litfilename, litfilename, sizeof (litfilename));
	q_strlcat (litfilename, ".lit", sizeof (litfilename));
	data = (byte *)COM_LoadFile (litfilename, &path_id);
	if (data)
	{
		// use lit file only from the same gamedir as the map
		// itself or from a searchpath with higher priority.
		if (path_id < mod->path_id)
		{
			Con_DPrintf ("ignored %s from a gamedir with lower priority\n", litfilename);
		}
		else if (data[0] == 'Q' && data[1] == 'L' && data[2] == 'I' && data[3] == 'T')
		{
			i = ReadLongUnaligned (data + sizeof (int));
			if (i == 1)
			{
				if (8 + l->filelen * 3 == com_filesize)
				{
					Con_DPrintf2 ("%s loaded\n", litfilename);
					mod->lightdata = (byte *)Mem_AllocNonZero (l->filelen * 3);
					memcpy (mod->lightdata, data + 8, l->filelen * 3);
					Mem_Free (data);
					return;
				}
				Con_Printf ("Outdated .lit file (%s should be %u bytes, not %lld)\n", litfilename, 8 + l->filelen * 3, com_filesize);
			}
			else
			{
				Con_Printf ("Unknown .lit file version (%d)\n", i);
			}
		}
		else
		{
			Con_Printf ("Corrupt .lit file (old version?), ignoring\n");
		}

		Mem_Free (data);
	}
	// LordHavoc: no .lit found, expand the white lighting data to color
	if (!l->filelen)
		return;

	// Quake64 bsp lighmap data
	if (mod->bspversion == BSPVERSION_QUAKE64)
	{
		// RGB lightmap samples are packed in 16bits.
		// RRRRR GGGGG BBBBBB

		mod->lightdata = (byte *)Mem_Alloc ((l->filelen / 2) * 3);
		in = mod_base + l->fileofs;
		out = mod->lightdata;

		for (i = 0; i < (l->filelen / 2); i++)
		{
			q64_b0 = *in++;
			q64_b1 = *in++;

			*out++ = q64_b0 & 0xf8;									  /* 0b11111000 */
			*out++ = ((q64_b0 & 0x07) << 5) + ((q64_b1 & 0xc0) >> 5); /* 0b00000111, 0b11000000 */
			*out++ = (q64_b1 & 0x3f) << 2;							  /* 0b00111111 */
		}
		return;
	}

#ifdef BSP29_VALVE
	if (mod->bspversion == BSPVERSION_VALVE)
	{
		// lightmap samples are already stored as rgb
		mod->lightdata = (byte *)Mem_Alloc (l->filelen);
		memcpy (mod->lightdata, mod_base + l->fileofs, l->filelen);
		return;
	}
#endif

	mod->lightdata = (byte *)Mem_Alloc (l->filelen * 3);
	in = mod->lightdata + l->filelen * 2; // place the file at the end, so it will not be overwritten until the very last write
	out = mod->lightdata;
	memcpy (in, mod_base + l->fileofs, l->filelen);
	for (i = 0; i < l->filelen; i++)
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
static void Mod_LoadVisibility (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	mod->viswarn = false;
	if (!l->filelen)
	{
		mod->visdata = NULL;
		return;
	}
	mod->visdata = (byte *)Mem_Alloc (l->filelen);
	memcpy (mod->visdata, mod_base + l->fileofs, l->filelen);
}

/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	char		 basemapname[MAX_QPATH];
	char		 entfilename[MAX_QPATH];
	char		*ents = NULL;
	unsigned int path_id;
	qboolean	 versioned = true;

	if (!external_ents.value)
		goto _load_embedded;

	if (l->filelen > 0)
	{
		mod->entities_crc = CRC_Block (mod_base + l->fileofs, l->filelen - 1);
	}
	else
	{
		mod->entities_crc = 0;
	}

	q_strlcpy (basemapname, mod->name, sizeof (basemapname));
	COM_StripExtension (basemapname, basemapname, sizeof (basemapname));

	q_snprintf (entfilename, sizeof (entfilename), "%s@%04x.ent", basemapname, mod->entities_crc);
	Con_DPrintf2 ("trying to load %s\n", entfilename);
	ents = (char *)COM_LoadFile (entfilename, &path_id);

	if (!ents)
	{
		q_snprintf (entfilename, sizeof (entfilename), "%s.ent", basemapname);
		Con_DPrintf2 ("trying to load %s\n", entfilename);
		ents = (char *)COM_LoadFile (entfilename, &path_id);
		versioned = false;
	}

	if (ents)
	{
		// use ent file only from the same gamedir as the map
		// itself or from a searchpath with higher priority
		// unless we got a CRC match
		if (versioned == false && path_id < mod->path_id)
		{
			Con_DPrintf ("ignored %s from a gamedir with lower priority\n", entfilename);
		}
		else
		{
			mod->entities = ents;
			Con_DPrintf ("Loaded external entity file %s\n", entfilename);
			return;
		}
	}

_load_embedded:
	if (!l->filelen)
	{
		Mem_Free (mod->entities);
		mod->entities = NULL;
		return;
	}
	// The BSP entity lump is a text-based lump intended to be read
	// by COM_Parse(), which expects a valid null-terminated string.
	// However l->filelen is the character (byte) length of the lump
	// not including the null-character, which, as it happens, can be absent from the BSP
	// in some cases.
	// To properly terminate the text-blob and prevent buffer overflows in COM_Parse, over-allocate + 1 byte
	// using Mem_Alloc (which 0-initialize)
	// The external .ent files are safe because COM_LoadFile also overallocate
	// with a 0-byte at the end by default.
	mod->entities = (char *)Mem_Alloc (l->filelen + 1);
	memcpy (mod->entities, mod_base + l->fileofs, l->filelen);
	Mem_Free (ents);
}

/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	byte	  *in;
	mvertex_t *out;
	int		   i, count;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dvertex_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dvertex_t);
	out = (mvertex_t *)Mem_Alloc (count * sizeof (*out));

	mod->vertexes = out;
	mod->numvertexes = count;

	for (i = 0; i < count; i++, in += sizeof (dvertex_t), out++)
	{
		out->position[0] = ReadFloatUnaligned (in + offsetof (dvertex_t, point[0]));
		out->position[1] = ReadFloatUnaligned (in + offsetof (dvertex_t, point[1]));
		out->position[2] = ReadFloatUnaligned (in + offsetof (dvertex_t, point[2]));
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	medge_t *out;
	int		 i, count;

	if (bsp2)
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (dledge_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dledge_t);
		out = (medge_t *)Mem_Alloc ((count + 1) * sizeof (*out));

		mod->edges = out;
		mod->numedges = count;

		for (i = 0; i < count; i++, in += sizeof (dledge_t), out++)
		{
			out->v[0] = ReadLongUnaligned (in + offsetof (dledge_t, v[0]));
			out->v[1] = ReadLongUnaligned (in + offsetof (dledge_t, v[1]));
		}
	}
	else
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (dsedge_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dsedge_t);
		out = (medge_t *)Mem_Alloc ((count + 1) * sizeof (*out));

		mod->edges = out;
		mod->numedges = count;

		for (i = 0; i < count; i++, in += sizeof (dsedge_t), out++)
		{
			out->v[0] = (unsigned short)ReadShortUnaligned (in + offsetof (dsedge_t, v[0]));
			out->v[1] = (unsigned short)ReadShortUnaligned (in + offsetof (dsedge_t, v[1]));
		}
	}
}

/*
=================
Mod_LoadTexinfo
=================
*/
static void Mod_LoadTexinfo (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	byte	   *in;
	mtexinfo_t *out;
	int			i, j, count, miptex;
	int			missing = 0; // johnfitz

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (texinfo_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (texinfo_t);
	out = (mtexinfo_t *)Mem_Alloc (count * sizeof (*out));

	mod->texinfo = out;
	mod->numtexinfo = count;

	for (i = 0; i < count; i++, in += sizeof (texinfo_t), out++)
	{
		for (j = 0; j < 4; j++)
		{
			out->vecs[0][j] = ReadFloatUnaligned (in + offsetof (texinfo_t, vecs[0][j]));
			out->vecs[1][j] = ReadFloatUnaligned (in + offsetof (texinfo_t, vecs[1][j]));
		}

		miptex = ReadLongUnaligned (in + offsetof (texinfo_t, miptex));
		out->flags = ReadLongUnaligned (in + offsetof (texinfo_t, flags));

		// johnfitz -- rewrote this section
		if (miptex >= mod->numtextures - 1 || !mod->textures[miptex])
		{
			if (out->flags & TEX_SPECIAL)
				out->texture = mod->textures[mod->numtextures - 1];
			else
				out->texture = mod->textures[mod->numtextures - 2];
			out->flags |= TEX_MISSING;
			missing++;
			out->tex_idx = -1;
		}
		else
		{
			out->texture = mod->textures[miptex];
			out->tex_idx = miptex;
		}
		// johnfitz
	}

	// johnfitz: report missing textures
	if (missing && mod->numtextures > 1)
		Con_Printf ("Mod_LoadTexinfo: %d texture(s) missing from BSP file\n", missing);
	// johnfitz
}

/*
================
CalcSurfaceExtents

Fills in s->texturemins[] and s->extents[]
================
*/
static void CalcSurfaceExtents (qmodel_t *mod, msurface_t *s)
{
	float		mins[2], maxs[2], val;
	int			i, j, e;
	mvertex_t  *v;
	mtexinfo_t *tex;
	int			bmins[2], bmaxs[2];

	mins[0] = mins[1] = FLT_MAX;
	maxs[0] = maxs[1] = -FLT_MAX;

	tex = s->texinfo;

	const double tex_vecs[2][4] = {
		{tex->vecs[0][0], tex->vecs[0][1], tex->vecs[0][2], tex->vecs[0][3]},
		{tex->vecs[1][0], tex->vecs[1][1], tex->vecs[1][2], tex->vecs[1][3]},
	};

	for (i = 0; i < s->numedges; i++)
	{
		e = mod->surfedges[s->firstedge + i];
		if (e >= 0)
			v = &mod->vertexes[mod->edges[e].v[0]];
		else
			v = &mod->vertexes[mod->edges[-e].v[1]];

		for (j = 0; j < 2; j++)
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
			val = ((double)v->position[0] * tex_vecs[j][0]) + ((double)v->position[1] * tex_vecs[j][1]) + ((double)v->position[2] * tex_vecs[j][2]) +
				  tex_vecs[j][3];

			mins[j] = q_min (mins[j], val);
			maxs[j] = q_max (maxs[j], val);
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor (mins[i] / 16);
		bmaxs[i] = ceil (maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;

		if (!(tex->flags & TEX_SPECIAL) && s->extents[i] > 2000) // johnfitz -- was 512 in glquake, 256 in winquake
			Sys_Error ("Bad surface extents");
	}
}

/*
================
Mod_PolyForUnlitSurface -- johnfitz -- creates polys for unlightmapped surfaces (sky and water)

TODO: merge this into BuildSurfaceDisplayList?
================
*/
static void Mod_PolyForUnlitSurface (qmodel_t *mod, msurface_t *fa)
{
	const int numverts = fa->numedges;
	int		  i, lindex;
	float	 *vec;
	glpoly_t *poly;
	float	  texscale;

	if (fa->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		texscale = (1.0 / 128.0); // warp animation repeats every 128
	else
		texscale = (1.0 / 32.0); // to match r_notexture_mip

	// create the poly
	poly = (glpoly_t *)Mem_Alloc (sizeof (glpoly_t) + (numverts - 4) * VERTEXSIZE * sizeof (float));
	poly->next = NULL;
	fa->polys = poly;
	poly->numverts = numverts;
	for (i = 0; i < numverts; i++)
	{
		lindex = mod->surfedges[fa->firstedge + i];
		vec = (lindex > 0) ? mod->vertexes[mod->edges[lindex].v[0]].position : mod->vertexes[mod->edges[-lindex].v[1]].position;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = DotProduct (vec, fa->texinfo->vecs[0]) * texscale;
		poly->verts[i][4] = DotProduct (vec, fa->texinfo->vecs[1]) * texscale;
	}
}

/*
================
Mod_CalcSurfaceExtents
================
*/
static void Mod_CalcSurfaceExtentsTask (int surfnum, qmodel_t **mod_ptr)
{
	qmodel_t *mod = *mod_ptr;
	CalcSurfaceExtents (mod, &mod->surfaces[surfnum]);
}

/*
=================
Mod_LoadFaces
=================
*/
static void Mod_LoadFaces (qmodel_t *mod, byte *mod_base, lump_t *l, qboolean bsp2)
{
	byte	   *ins;
	byte	   *inl;
	msurface_t *out;
	int			i, count, surfnum, lofs;
	int			planenum, side, texinfon;

	if (bsp2)
	{
		ins = NULL;
		inl = mod_base + l->fileofs;
		if (l->filelen % sizeof (dlface_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
		count = l->filelen / sizeof (dlface_t);
	}
	else
	{
		ins = mod_base + l->fileofs;
		inl = NULL;
		if (l->filelen % sizeof (dsface_t))
			Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
		count = l->filelen / sizeof (dsface_t);
	}
	out = (msurface_t *)Mem_AllocNonZero (count * sizeof (*out));

	// johnfitz -- warn mappers about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i faces exceeds standard limit of 32767.\n", count);
	// johnfitz

	mod->surfaces = out;
	mod->numsurfaces = count;

	for (surfnum = 0; surfnum < count; surfnum++, out++)
	{
		if (bsp2)
		{
			out->firstedge = ReadLongUnaligned (inl + offsetof (dlface_t, firstedge));
			out->numedges = ReadLongUnaligned (inl + offsetof (dlface_t, numedges));
			planenum = ReadLongUnaligned (inl + offsetof (dlface_t, planenum));
			side = ReadLongUnaligned (inl + offsetof (dlface_t, side));
			texinfon = ReadLongUnaligned (inl + offsetof (dlface_t, texinfo));
			for (i = 0; i < MAXLIGHTMAPS; i++)
			{
				out->styles[i] = *(inl + offsetof (dlface_t, styles[i]));
				if (out->styles[i] >= MAX_LIGHTSTYLES && out->styles[i] != 255)
				{
					Con_Warning ("Invalid lightstyle %d\n", out->styles[i]);
					out->styles[i] = 0;
				}
				byte j = out->styles[i];
				if (j < 255)
					out->styles_bitmap |= 1 << (j < 16 ? j : j % 16 + 16);
			}
			lofs = ReadLongUnaligned (inl + offsetof (dlface_t, lightofs));
			inl += sizeof (dlface_t);
		}
		else
		{
			out->firstedge = ReadLongUnaligned (ins + offsetof (dsface_t, firstedge));
			out->numedges = ReadShortUnaligned (ins + offsetof (dsface_t, numedges));
			planenum = ReadShortUnaligned (ins + offsetof (dsface_t, planenum));
			side = ReadShortUnaligned (ins + offsetof (dsface_t, side));
			texinfon = ReadShortUnaligned (ins + offsetof (dsface_t, texinfo));
			for (i = 0; i < MAXLIGHTMAPS; i++)
			{
				out->styles[i] = *(ins + offsetof (dsface_t, styles[i]));
				if (out->styles[i] >= MAX_LIGHTSTYLES && out->styles[i] != 255)
				{
					Con_Warning ("Invalid lightstyle %d\n", out->styles[i]);
					out->styles[i] = 0;
				}
				byte j = out->styles[i];
				if (j < 255)
					out->styles_bitmap |= 1 << (j < 16 ? j : j % 16 + 16);
			}
			lofs = ReadLongUnaligned (ins + offsetof (dsface_t, lightofs));
			ins += sizeof (dsface_t);
		}

		if (!out->styles_bitmap)
			out->styles_bitmap = 1;

		out->flags = 0;
		out->polys = NULL;

		if (side)
			out->flags |= SURF_PLANEBACK;

		out->plane = mod->planes + planenum;

		out->texinfo = mod->texinfo + texinfon;

		// lighting info
		if (mod->bspversion == BSPVERSION_QUAKE64)
			lofs /= 2; // Q64 samples are 16bits instead 8 in normal Quake

		if (lofs == -1)
			out->samples = NULL;
#ifdef BSP29_VALVE
		else if (mod->bspversion == BSPVERSION_VALVE)
			out->samples = mod->lightdata + lofs; // accounts for RGB light data
#endif
		else
			out->samples = mod->lightdata + (lofs * 3); // johnfitz -- lit support via lordhavoc (was "+ i")

		// johnfitz -- this section rewritten
		out->lightmaptexturenum = -1;
		if (out->texinfo->texture->type == TEXTYPE_SKY) // sky surface //also note -- was strncmp, changed to match qbsp
		{
			out->flags |= (SURF_DRAWSKY | SURF_DRAWTILED);
			Mod_PolyForUnlitSurface (mod, out); // no more subdivision
		}
		else if (TEXTYPE_ISLIQUID (out->texinfo->texture->type)) // warp surface
		{
			out->flags |= SURF_DRAWTURB;

			if (out->texinfo->flags & TEX_SPECIAL)
				out->flags |= SURF_DRAWTILED; // unlit water

			// detect special liquid types
			if (out->texinfo->texture->type == TEXTYPE_LAVA)
				out->flags |= SURF_DRAWLAVA;
			else if (out->texinfo->texture->type == TEXTYPE_SLIME)
				out->flags |= SURF_DRAWSLIME;
			else if (out->texinfo->texture->type == TEXTYPE_TELE)
				out->flags |= SURF_DRAWTELE;
			else
				out->flags |= SURF_DRAWWATER;

			if (out->flags & SURF_DRAWTILED)
				Mod_PolyForUnlitSurface (mod, out);
		}
		else if (out->texinfo->texture->type == TEXTYPE_CUTOUT) // ericw -- fence textures
		{
			out->flags |= SURF_DRAWFENCE;
		}
		else if (out->texinfo->flags & TEX_MISSING) // texture is missing from bsp
		{
			out->flags |= SURF_NOTEXTURE;
			qboolean missing_samples = !out->samples && out->styles[0] != 255;
			qboolean unlit_texture = out->texinfo->flags & TEX_SPECIAL;

			if (!unlit_texture && missing_samples)
			{
				// unlit surf in a lit texture (mod->numtextures - 2: r_notexture_mip instead of r_notexture_mip2)
				Con_Warning ("Mod_LoadFaces: TEX_MISSING without TEX_SPECIAL missing lightmap samples");
				out->lightmaptexturenum = 0; // set a lightmaptexturenum to at least avoid a crash
			}

			if (unlit_texture || missing_samples) // not lightmapped
			{
				out->flags |= SURF_DRAWTILED;
				Mod_PolyForUnlitSurface (mod, out);
			}
		}
		// johnfitz
	}

	if (!isDedicated)
	{
		if (!Tasks_IsWorker () && (count > 1))
		{
			task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_CalcSurfaceExtentsTask, count, &mod, sizeof (qmodel_t *));
			Task_Join (task, TASK_TIMEOUT_INFINITE);
		}
		else
		{
			for (i = 0; i < count; i++)
				Mod_CalcSurfaceExtentsTask (i, &mod);
		}
	}
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes_S (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		 i, j, count, p;
	byte	*in;
	mnode_t *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dsnode_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dsnode_t);
	out = (mnode_t *)Mem_Alloc (count * sizeof (*out));

	// johnfitz -- warn mappers about exceeding old limits
	if (count > 32767)
		Con_DWarning ("%i nodes exceeds standard limit of 32767.\n", count);
	// johnfitz

	mod->nodes = out;
	mod->numnodes = count;

	for (i = 0; i < count; i++, in += sizeof (dsnode_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dsnode_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dsnode_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dsnode_t, planenum));
		out->plane = mod->planes + p;

		out->firstsurface = (unsigned short)ReadShortUnaligned (in + offsetof (dsnode_t, firstface)); // johnfitz -- explicit cast as unsigned short
		out->numsurfaces = (unsigned short)ReadShortUnaligned (in + offsetof (dsnode_t, numfaces));	  // johnfitz -- explicit cast as unsigned short

		for (j = 0; j < 2; j++)
		{
			// johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = (unsigned short)ReadShortUnaligned (in + offsetof (dsnode_t, children[j]));
			if (p < count)
				out->children[j] = mod->nodes + p;
			else
			{
				p = 65535 - p; // note this uses 65535 intentionally, -1 is leaf 0
				if (p < mod->numleafs)
					out->children[j] = (mnode_t *)(mod->leafs + p);
				else
				{
					Con_Printf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->numleafs);
					out->children[j] = (mnode_t *)(mod->leafs); // map it to the solid leaf
				}
			}
			// johnfitz
		}
	}
}

static void Mod_LoadNodes_L1 (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		 i, j, count, p;
	byte	*in;
	mnode_t *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dl1node_t))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dl1node_t);
	out = (mnode_t *)Mem_Alloc (count * sizeof (*out));

	mod->nodes = out;
	mod->numnodes = count;

	for (i = 0; i < count; i++, in += sizeof (dl1node_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dl1node_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dl1node_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl1node_t, planenum));
		out->plane = mod->planes + p;

		out->firstsurface = ReadLongUnaligned (in + offsetof (dl1node_t, firstface)); // johnfitz -- explicit cast as unsigned short
		out->numsurfaces = ReadLongUnaligned (in + offsetof (dl1node_t, numfaces));	  // johnfitz -- explicit cast as unsigned short

		for (j = 0; j < 2; j++)
		{
			// johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = ReadLongUnaligned (in + offsetof (dl1node_t, children[j]));
			if (p >= 0 && p < count)
				out->children[j] = mod->nodes + p;
			else
			{
				p = 0xffffffff - p; // note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < mod->numleafs)
					out->children[j] = (mnode_t *)(mod->leafs + p);
				else
				{
					Con_Printf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->numleafs);
					out->children[j] = (mnode_t *)(mod->leafs); // map it to the solid leaf
				}
			}
			// johnfitz
		}
	}
}

static void Mod_LoadNodes_L2 (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		 i, j, count, p;
	byte	*in;
	mnode_t *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dl2node_t))
		Sys_Error ("Mod_LoadNodes: funny lump size in %s", mod->name);

	count = l->filelen / sizeof (dl2node_t);
	out = (mnode_t *)Mem_Alloc (count * sizeof (*out));

	mod->nodes = out;
	mod->numnodes = count;

	for (i = 0; i < count; i++, in += sizeof (dl2node_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadFloatUnaligned (in + offsetof (dl2node_t, mins[j]));
			out->minmaxs[3 + j] = ReadFloatUnaligned (in + offsetof (dl2node_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl2node_t, planenum));
		out->plane = mod->planes + p;

		out->firstsurface = ReadLongUnaligned (in + offsetof (dl2node_t, firstface)); // johnfitz -- explicit cast as unsigned short
		out->numsurfaces = ReadLongUnaligned (in + offsetof (dl2node_t, numfaces));	  // johnfitz -- explicit cast as unsigned short

		for (j = 0; j < 2; j++)
		{
			// johnfitz -- hack to handle nodes > 32k, adapted from darkplaces
			p = ReadLongUnaligned (in + offsetof (dl2node_t, children[j]));
			if (p > 0 && p < count)
				out->children[j] = mod->nodes + p;
			else
			{
				p = 0xffffffff - p; // note this uses 65535 intentionally, -1 is leaf 0
				if (p >= 0 && p < mod->numleafs)
					out->children[j] = (mnode_t *)(mod->leafs + p);
				else
				{
					Con_Printf ("Mod_LoadNodes: invalid leaf index %i (file has only %i leafs)\n", p, mod->numleafs);
					out->children[j] = (mnode_t *)(mod->leafs); // map it to the solid leaf
				}
			}
			// johnfitz
		}
	}
}

static void Mod_LoadNodes (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	if (bsp2 == 2)
		Mod_LoadNodes_L2 (mod, mod_base, l);
	else if (bsp2)
		Mod_LoadNodes_L1 (mod, mod_base, l);
	else
		Mod_LoadNodes_S (mod, mod_base, l);
}

static void Mod_ProcessLeafs_S (qmodel_t *mod, byte *in, int filelen)
{
	mleaf_t *out;
	int		 i, j, count, p;

	if (filelen % sizeof (dsleaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", mod->name);
	count = filelen / sizeof (dsleaf_t);
	out = (mleaf_t *)Mem_Alloc (count * sizeof (*out));

	// johnfitz
	if (count > 32767)
		Host_Error ("Mod_LoadLeafs: %i leafs exceeds limit of 32767.", count);
	// johnfitz

	mod->leafs = out;
	mod->numleafs = count;

	for (i = 0; i < count; i++, in += sizeof (dsleaf_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dsleaf_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dsleaf_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dsleaf_t, contents));
		out->contents = p;

		out->firstmarksurface =
			mod->marksurfaces + (unsigned short)ReadShortUnaligned (in + offsetof (dsleaf_t, firstmarksurface)); // johnfitz -- unsigned short
		out->nummarksurfaces = (unsigned short)ReadShortUnaligned (in + offsetof (dsleaf_t, nummarksurfaces));	 // johnfitz -- unsigned short

		p = ReadLongUnaligned (in + offsetof (dsleaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = (mod->visdata != NULL) ? (mod->visdata + p) : NULL;
		out->efrags = NULL;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = *(in + offsetof (dsleaf_t, ambient_level[j]));

		// johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

static void Mod_ProcessLeafs_L1 (qmodel_t *mod, byte *in, int filelen)
{
	mleaf_t *out;
	int		 i, j, count, p;

	if (filelen % sizeof (dl1leaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", mod->name);

	count = filelen / sizeof (dl1leaf_t);

	out = (mleaf_t *)Mem_Alloc (count * sizeof (*out));

	mod->leafs = out;
	mod->numleafs = count;

	for (i = 0; i < count; i++, in += sizeof (dl1leaf_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadShortUnaligned (in + offsetof (dl1leaf_t, mins[j]));
			out->minmaxs[3 + j] = ReadShortUnaligned (in + offsetof (dl1leaf_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl1leaf_t, contents));
		out->contents = p;

		out->firstmarksurface = mod->marksurfaces + ReadLongUnaligned (in + offsetof (dl1leaf_t, firstmarksurface)); // johnfitz -- unsigned short
		out->nummarksurfaces = ReadLongUnaligned (in + offsetof (dl1leaf_t, nummarksurfaces));						 // johnfitz -- unsigned short

		p = ReadLongUnaligned (in + offsetof (dl1leaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = mod->visdata + p;
		out->efrags = NULL;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = *(in + offsetof (dl1leaf_t, ambient_level[j]));

		// johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

static void Mod_ProcessLeafs_L2 (qmodel_t *mod, byte *in, int filelen)
{
	mleaf_t *out;
	int		 i, j, count, p;

	if (filelen % sizeof (dl2leaf_t))
		Sys_Error ("Mod_ProcessLeafs: funny lump size in %s", mod->name);

	count = filelen / sizeof (dl2leaf_t);

	out = (mleaf_t *)Mem_Alloc (count * sizeof (*out));

	mod->leafs = out;
	mod->numleafs = count;

	for (i = 0; i < count; i++, in += sizeof (dl2leaf_t), out++)
	{
		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = ReadFloatUnaligned (in + offsetof (dl2leaf_t, mins[j]));
			out->minmaxs[3 + j] = ReadFloatUnaligned (in + offsetof (dl2leaf_t, maxs[j]));
		}

		p = ReadLongUnaligned (in + offsetof (dl2leaf_t, contents));
		out->contents = p;

		out->firstmarksurface = mod->marksurfaces + ReadLongUnaligned (in + offsetof (dl2leaf_t, firstmarksurface)); // johnfitz -- unsigned short
		out->nummarksurfaces = ReadLongUnaligned (in + offsetof (dl2leaf_t, nummarksurfaces));						 // johnfitz -- unsigned short

		p = ReadLongUnaligned (in + offsetof (dl2leaf_t, visofs));
		if (p == -1)
			out->compressed_vis = NULL;
		else
			out->compressed_vis = mod->visdata + p;
		out->efrags = NULL;

		for (j = 0; j < 4; j++)
			out->ambient_sound_level[j] = *(in + offsetof (dl2leaf_t, ambient_level[j]));

		// johnfitz -- removed code to mark surfaces as SURF_UNDERWATER
	}
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	void *in = (void *)(mod_base + l->fileofs);

	if (bsp2 == 2)
		Mod_ProcessLeafs_L2 (mod, in, l->filelen);
	else if (bsp2)
		Mod_ProcessLeafs_L1 (mod, in, l->filelen);
	else
		Mod_ProcessLeafs_S (mod, in, l->filelen);
}

/*
=================
Mod_CheckWaterVis
=================
*/
static void Mod_CheckWaterVis (qmodel_t *mod)
{
	mleaf_t	   *leaf, *other;
	msurface_t *surf;
	int			i, j, k;
	int			numclusters = mod->submodels[0].visleafs;
	int			contentfound = 0;
	int			contenttransparent = 0;
	int			contenttype;
	unsigned	hascontents = 0;

	if (r_novis.value)
	{ // all can be
		mod->contentstransparent = (SURF_DRAWWATER | SURF_DRAWTELE | SURF_DRAWSLIME | SURF_DRAWLAVA);
		return;
	}

	// pvs is 1-based. leaf 0 sees all (the solid leaf).
	// leaf 0 has no pvs, and does not appear in other leafs either, so watch out for the biases.
	for (i = 0, leaf = mod->leafs + 1; i < numclusters - 1; i++, leaf++)
	{
		byte *vis;
		if (leaf->contents < 0) // err... wtf?
			hascontents = 0;
		if (leaf->contents == CONTENTS_WATER)
		{
			if ((contenttransparent & (SURF_DRAWWATER | SURF_DRAWTELE)) == (SURF_DRAWWATER | SURF_DRAWTELE))
				continue;
			// this check is somewhat risky, but we should be able to get away with it.
			for (contenttype = 0, j = 0; j < leaf->nummarksurfaces; j++)
			{
				surf = &mod->surfaces[leaf->firstmarksurface[j]];
				if (surf->flags & (SURF_DRAWWATER | SURF_DRAWTELE))
				{
					contenttype = surf->flags & (SURF_DRAWWATER | SURF_DRAWTELE);
					break;
				}
			}
			// its possible that this leaf has absolutely no surfaces in it, turb or otherwise.
			if (contenttype == 0)
				continue;
		}
		else if (leaf->contents == CONTENTS_SLIME)
			contenttype = SURF_DRAWSLIME;
		else if (leaf->contents == CONTENTS_LAVA)
			contenttype = SURF_DRAWLAVA;
		// fixme: tele
		else
			continue;
		if (contenttransparent & contenttype)
		{
		nextleaf:
			continue; // found one of this type already
		}
		contentfound |= contenttype;
		vis = Mod_DecompressVis (leaf->compressed_vis, mod);
		for (j = 0; j < (numclusters + 7) / 8; j++)
		{
			if (vis[j])
			{
				for (k = 0; k < 8; k++)
				{
					if (vis[j] & (1u << k))
					{
						other = &mod->leafs[(j << 3) + k + 1];
						if (leaf->contents != other->contents)
						{
							//							Con_Printf("%p:%i sees %p:%i\n", leaf, leaf->contents, other, other->contents);
							contenttransparent |= contenttype;
							goto nextleaf;
						}
					}
				}
			}
		}
	}

	if (!contenttransparent)
	{ // no water leaf saw a non-water leaf
		// but only warn when there's actually water somewhere there...
		if (hascontents & ((1 << -CONTENTS_WATER) | (1 << -CONTENTS_SLIME) | (1 << -CONTENTS_LAVA)))
			Con_DPrintf ("%s is not watervised\n", mod->name);
	}
	else
	{
		Con_DPrintf2 ("%s is vised for transparent", mod->name);
		if (contenttransparent & SURF_DRAWWATER)
			Con_DPrintf2 (" water");
		if (contenttransparent & SURF_DRAWTELE)
			Con_DPrintf2 (" tele");
		if (contenttransparent & SURF_DRAWLAVA)
			Con_DPrintf2 (" lava");
		if (contenttransparent & SURF_DRAWSLIME)
			Con_DPrintf2 (" slime");
		Con_DPrintf2 ("\n");
	}
	// any types that we didn't find are assumed to be transparent.
	// this allows submodels to work okay (eg: ad uses func_illusionary teleporters for some reason).
	mod->contentstransparent = contenttransparent | (~contentfound & (SURF_DRAWWATER | SURF_DRAWTELE | SURF_DRAWSLIME | SURF_DRAWLAVA));
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes (qmodel_t *mod, byte *mod_base, lump_t *l, qboolean bsp2)
{
	byte *ins;
	byte *inl;

	mclipnode_t *out; // johnfitz -- was dclipnode_t
	int			 i, count;
	hull_t		*hull;

	if (bsp2)
	{
		ins = NULL;
		inl = mod_base + l->fileofs;
		if (l->filelen % sizeof (dlclipnode_t))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dlclipnode_t);
	}
	else
	{
		ins = mod_base + l->fileofs;
		inl = NULL;
		if (l->filelen % sizeof (dsclipnode_t))
			Sys_Error ("Mod_LoadClipnodes: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (dsclipnode_t);
	}
	out = (mclipnode_t *)Mem_Alloc (count * sizeof (*out));

	// johnfitz -- warn about exceeding old limits
	if (count > 32767 && !bsp2)
		Con_DWarning ("%i clipnodes exceeds standard limit of 32767.\n", count);
	// johnfitz

	mod->clipnodes = out;
	mod->numclipnodes = count;

	hull = &mod->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->planes;
	hull->clip_mins[0] = -16;
	hull->clip_mins[1] = -16;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 16;
	hull->clip_maxs[1] = 16;
	hull->clip_maxs[2] = 32;

	hull = &mod->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->planes;
	hull->clip_mins[0] = -32;
	hull->clip_mins[1] = -32;
	hull->clip_mins[2] = -24;
	hull->clip_maxs[0] = 32;
	hull->clip_maxs[1] = 32;
	hull->clip_maxs[2] = 64;

	if (bsp2)
	{
		for (i = 0; i < count; i++, out++, inl += sizeof (dlclipnode_t))
		{
			out->planenum = ReadLongUnaligned (inl + offsetof (dlclipnode_t, planenum));

			// johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= mod->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			// johnfitz

			out->children[0] = ReadLongUnaligned (inl + offsetof (dlclipnode_t, children[0]));
			out->children[1] = ReadLongUnaligned (inl + offsetof (dlclipnode_t, children[1]));
			// Spike: FIXME: bounds check
		}
	}
	else
	{
		for (i = 0; i < count; i++, out++, ins += sizeof (dsclipnode_t))
		{
			out->planenum = ReadLongUnaligned (ins + offsetof (dsclipnode_t, planenum));

			// johnfitz -- bounds check
			if (out->planenum < 0 || out->planenum >= mod->numplanes)
				Host_Error ("Mod_LoadClipnodes: planenum out of bounds");
			// johnfitz

			// johnfitz -- support clipnodes > 32k
			out->children[0] = (unsigned short)ReadShortUnaligned (ins + offsetof (dsclipnode_t, children[0]));
			out->children[1] = (unsigned short)ReadShortUnaligned (ins + offsetof (dsclipnode_t, children[1]));

			if (out->children[0] >= count)
				out->children[0] -= 65536;
			if (out->children[1] >= count)
				out->children[1] -= 65536;
			// johnfitz
		}
	}
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0 (qmodel_t *mod)
{
	mnode_t		*in, *child;
	mclipnode_t *out; // johnfitz -- was dclipnode_t
	int			 i, j, count;
	hull_t		*hull;

	hull = &mod->hulls[0];

	in = mod->nodes;
	count = mod->numnodes;
	out = (mclipnode_t *)Mem_Alloc (count * sizeof (*out));

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = mod->planes;

	for (i = 0; i < count; i++, out++, in++)
	{
		out->planenum = in->plane - mod->planes;
		for (j = 0; j < 2; j++)
		{
			child = in->children[j];
			if (child->contents < 0)
				out->children[j] = child->contents;
			else
				out->children[j] = child - mod->nodes;
		}
	}
}

/*
=================
Mod_LoadMarksurfaces
=================
*/
static void Mod_LoadMarksurfaces (qmodel_t *mod, byte *mod_base, lump_t *l, int bsp2)
{
	int	 i, j, count;
	int *out;
	if (bsp2)
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (unsigned int))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (unsigned int);
		out = (int *)Mem_Alloc (count * sizeof (*out));

		mod->marksurfaces = out;
		mod->nummarksurfaces = count;

		for (i = 0; i < count; i++)
		{
			j = ReadLongUnaligned (in + (i * sizeof (int)));
			if (j >= mod->numsurfaces)
				Host_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = j;
		}
	}
	else
	{
		byte *in = mod_base + l->fileofs;

		if (l->filelen % sizeof (short))
			Host_Error ("Mod_LoadMarksurfaces: funny lump size in %s", mod->name);

		count = l->filelen / sizeof (short);
		out = (int *)Mem_Alloc (count * sizeof (*out));

		mod->marksurfaces = out;
		mod->nummarksurfaces = count;

		// johnfitz -- warn mappers about exceeding old limits
		if (count > 32767)
			Con_DWarning ("%i marksurfaces exceeds standard limit of 32767.\n", count);
		// johnfitz

		for (i = 0; i < count; i++)
		{
			j = (unsigned short)ReadShortUnaligned (in + (i * sizeof (short))); // johnfitz -- explicit cast as unsigned short
			if (j >= mod->numsurfaces)
				Sys_Error ("Mod_LoadMarksurfaces: bad surface number");
			out[i] = j;
		}
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
static void Mod_LoadSurfedges (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int	  i, count;
	byte *in;
	int	 *out;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (int))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (int);
	out = (int *)Mem_Alloc (count * sizeof (int));

	mod->surfedges = out;
	mod->numsurfedges = count;

	for (i = 0; i < count; i++)
	{
		out[i] = ReadLongUnaligned (in + (i * sizeof (int)));
	}
}

/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	int		  i, j;
	mplane_t *out;
	byte	 *in;
	int		  count;
	int		  bits;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dplane_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dplane_t);
	out = (mplane_t *)Mem_Alloc (count * 2 * sizeof (*out));

	mod->planes = out;
	mod->numplanes = count;

	for (i = 0; i < count; i++, in += sizeof (dplane_t), out++)
	{
		bits = 0;
		for (j = 0; j < 3; j++)
		{
			out->normal[j] = ReadFloatUnaligned (in + offsetof (dplane_t, normal[j]));
			if (out->normal[j] < 0)
				bits |= 1 << j;
		}

		out->dist = ReadFloatUnaligned (in + offsetof (dplane_t, dist));
		out->type = ReadLongUnaligned (in + offsetof (dplane_t, type));
		out->signbits = bits;
	}
}

/*
=================
RadiusFromBounds
=================
*/
float RadiusFromBounds (vec3_t mins, vec3_t maxs)
{
	int	   i;
	vec3_t corner;

	for (i = 0; i < 3; i++)
	{
		corner[i] = fabs (mins[i]) > fabs (maxs[i]) ? fabs (mins[i]) : fabs (maxs[i]);
	}

	return VectorLength (corner);
}

/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels (qmodel_t *mod, byte *mod_base, lump_t *l)
{
	byte	 *in;
	dmodel_t *out;
	int		  i, j, count;

	in = mod_base + l->fileofs;
	if (l->filelen % sizeof (dmodel_t))
		Sys_Error ("MOD_LoadBmodel: funny lump size in %s", mod->name);
	count = l->filelen / sizeof (dmodel_t);
	out = (dmodel_t *)Mem_Alloc (count * sizeof (*out));

	mod->submodels = out;
	mod->numsubmodels = count;

	for (i = 0; i < count; i++, in += sizeof (dmodel_t), out++)
	{
		for (j = 0; j < 3; j++)
		{ // spread the mins / maxs by a pixel
			out->mins[j] = ReadFloatUnaligned (in + offsetof (dmodel_t, mins[j])) - 1;
			out->maxs[j] = ReadFloatUnaligned (in + offsetof (dmodel_t, maxs[j])) + 1;
			out->origin[j] = ReadFloatUnaligned (in + offsetof (dmodel_t, origin[j]));
		}
		for (j = 0; j < MAX_MAP_HULLS; j++)
		{
			out->headnode[j] = ReadLongUnaligned (in + offsetof (dmodel_t, headnode[j]));
		}
		out->visleafs = ReadLongUnaligned (in + offsetof (dmodel_t, visleafs));
		out->firstface = ReadLongUnaligned (in + offsetof (dmodel_t, firstface));
		out->numfaces = ReadLongUnaligned (in + offsetof (dmodel_t, numfaces));
	}

	// johnfitz -- check world visleafs -- adapted from bjp
	out = mod->submodels;

	if (out->visleafs > 8192)
		Con_DWarning ("%i visleafs exceeds standard limit of 8192.\n", out->visleafs);
	// johnfitz
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
#if 0  /* disabled for now -- see in Mod_SetupSubmodels()  */
static void Mod_BoundsFromClipNode (qmodel_t *mod, int hull, int nodenum)
{
	mplane_t    *plane;
	mclipnode_t *node;

	if (nodenum < 0)
		return; // hit a leafnode

	node = &mod->clipnodes[nodenum];
	plane = mod->hulls[hull].planes + node->planenum;
	switch (plane->type)
	{

	case PLANE_X:
		if (plane->signbits == 1)
			mod->clipmins[0] = q_min (mod->clipmins[0], -plane->dist - mod->hulls[hull].clip_mins[0]);
		else
			mod->clipmaxs[0] = q_max (mod->clipmaxs[0], plane->dist - mod->hulls[hull].clip_maxs[0]);
		break;
	case PLANE_Y:
		if (plane->signbits == 2)
			mod->clipmins[1] = q_min (mod->clipmins[1], -plane->dist - mod->hulls[hull].clip_mins[1]);
		else
			mod->clipmaxs[1] = q_max (mod->clipmaxs[1], plane->dist - mod->hulls[hull].clip_maxs[1]);
		break;
	case PLANE_Z:
		if (plane->signbits == 4)
			mod->clipmins[2] = q_min (mod->clipmins[2], -plane->dist - mod->hulls[hull].clip_mins[2]);
		else
			mod->clipmaxs[2] = q_max (mod->clipmaxs[2], plane->dist - mod->hulls[hull].clip_maxs[2]);
		break;
	default:
		// skip nonaxial planes; don't need them
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
	char mapname[32];
	int	 filelen; // length of data after header (VIS+Leafs)
} vispatch_t;
#define VISPATCH_HEADER_LEN 36

static FILE *Mod_FindVisibilityExternal (qmodel_t *mod, const char *loadname)
{
	vispatch_t	 header;
	char		 visfilename[MAX_QPATH];
	const char	*shortname;
	unsigned int path_id;
	FILE		*f;
	long		 pos;
	size_t		 r;

	q_snprintf (visfilename, sizeof (visfilename), "maps/%s.vis", loadname);
	if (COM_FOpenFile (visfilename, &f, &path_id) < 0)
	{
		Con_DPrintf ("%s not found, trying ", visfilename);
		q_snprintf (visfilename, sizeof (visfilename), "%s.vis", COM_SkipPath (com_gamedir));
		Con_DPrintf ("%s\n", visfilename);
		if (COM_FOpenFile (visfilename, &f, &path_id) < 0)
		{
			Con_DPrintf ("external vis not found\n");
			return NULL;
		}
	}
	if (path_id < mod->path_id)
	{
		fclose (f);
		Con_DPrintf ("ignored %s from a gamedir with lower priority\n", visfilename);
		return NULL;
	}

	Con_DPrintf ("Found external VIS %s\n", visfilename);

	shortname = COM_SkipPath (mod->name);
	pos = 0;
	while ((r = fread (&header, 1, VISPATCH_HEADER_LEN, f)) == VISPATCH_HEADER_LEN)
	{
		header.filelen = LittleLong (header.filelen);
		if (header.filelen <= 0)
		{ /* bad entry -- don't trust the rest. */
			fclose (f);
			return NULL;
		}
		if (!q_strcasecmp (header.mapname, shortname))
			break;
		pos += header.filelen + VISPATCH_HEADER_LEN;
		Sys_fseek (f, pos, SEEK_SET);
	}
	if (r != VISPATCH_HEADER_LEN)
	{
		fclose (f);
		Con_DPrintf ("%s not found in %s\n", shortname, visfilename);
		return NULL;
	}

	return f;
}

static byte *Mod_LoadVisibilityExternal (FILE *f)
{
	int	  filelen;
	byte *visdata;

	filelen = 0;
	if (fread (&filelen, 1, 4, f) != 4)
		return NULL;
	filelen = LittleLong (filelen);
	if (filelen <= 0)
		return NULL;
	Con_DPrintf ("...%d bytes visibility data\n", filelen);
	visdata = (byte *)Mem_Alloc (filelen);
	if (fread (visdata, filelen, 1, f) != 1)
		return NULL;
	return visdata;
}

static void Mod_LoadLeafsExternal (qmodel_t *mod, FILE *f)
{
	int	  filelen;
	void *in;

	filelen = 0;
	if (fread (&filelen, 1, 4, f) != 4)
		Sys_Error ("Invalid leaf");
	filelen = LittleLong (filelen);
	if (filelen <= 0)
		return;
	Con_DPrintf ("...%d bytes leaf data\n", filelen);
	in = Mem_Alloc (filelen);
	if (fread (in, filelen, 1, f) != 1)
		return;
	Mod_ProcessLeafs_S (mod, (byte *)in, filelen);
}

/*
================
Mod_CalcSpecialsAndTextures
================
*/
static int Mod_TextureIndexForSurface (qmodel_t *model, msurface_t *surf)
{
	texture_t *texture = surf->texinfo->texture;

	if (surf->texinfo->tex_idx >= 0 && surf->texinfo->tex_idx < model->numtextures && model->textures[surf->texinfo->tex_idx] == texture)
		return surf->texinfo->tex_idx;

	for (int i = 0; i < model->numtextures; i++)
	{
		if (model->textures[i] == texture)
			return i;
	}

	return -1;
}

static void Mod_CalcSpecialsAndTextures (qmodel_t *model)
{
	qboolean is_submodel = model->name[0] == '*';

	model->used_specials = 0;

	TEMP_ALLOC_ZEROED (byte, used_tex, model->numtextures);

	for (int i = 0; i < model->nummodelsurfaces; i++)
	{
		msurface_t *psurf = &model->surfaces[model->firstmodelsurface] + i;
		model->used_specials |= (SURF_DRAWSKY | SURF_DRAWTURB | SURF_DRAWWATER | SURF_DRAWLAVA | SURF_DRAWSLIME | SURF_DRAWTELE) & psurf->flags;

		if (is_submodel && psurf->texinfo->tex_idx >= 0)
		{
			if (psurf->texinfo->tex_idx < model->numtextures)
			{
				used_tex[psurf->texinfo->tex_idx] = true;
			}
			else
			{
				TEMP_FREE (used_tex);
				// Can we incounter invalid indices tex_idx >= model->numtextures
				Host_Error ("Mod_CalcSpecialsAndTextures: %s invalid tex_idx %i", model->name, (int)psurf->texinfo->tex_idx);
			}
		}
	}

	if (is_submodel)
	{
		int total = 0, placed = 0;
		for (int i = 0; i < model->numtextures; i++)
			if (used_tex[i])
				++total;

		texture_t **orig_textures = model->textures;
		model->textures = (texture_t **)Mem_AllocNonZero (total * sizeof (*model->textures));
		model->numtextures = total;

		for (int i = 0; placed < total; i++)
		{
			if (used_tex[i])
				model->textures[placed++] = orig_textures[i];
		}
	}

	memset (used_tex, 0, temp_alloc_used_tex_size);
	TEMP_ALLOC_ZEROED (int, tex_counts, TEXTYPE_COUNT);
	TEMP_ALLOC_ZEROED (int, tex_offsets, TEXTYPE_COUNT);

	for (int i = 0; i < model->nummodelsurfaces; i++)
	{
		msurface_t *psurf = &model->surfaces[model->firstmodelsurface] + i;
		const int	tex_index = Mod_TextureIndexForSurface (model, psurf);
		if (tex_index >= 0)
			used_tex[tex_index] = true;
	}

	for (int i = 0; i < model->numtextures; i++)
	{
		texture_t *texture = model->textures[i];
		if (texture && used_tex[i])
			++tex_counts[texture->type];
	}

	int total = 0;
	for (int i = 0; i < TEXTYPE_COUNT; i++)
	{
		model->texofs[i] = tex_offsets[i] = total;
		total += tex_counts[i];
	}
	model->texofs[TEXTYPE_COUNT] = total;

	model->usedtextures = total ? (int *)Mem_Alloc (total * sizeof (*model->usedtextures)) : NULL;
	for (int i = 0; i < model->numtextures; i++)
	{
		texture_t *texture = model->textures[i];
		if (texture && used_tex[i])
			model->usedtextures[tex_offsets[texture->type]++] = i;
	}

	TEMP_FREE (tex_offsets);
	TEMP_FREE (tex_counts);
	TEMP_FREE (used_tex);
}

/*
=================
Mod_SetupSubmodels
set up the submodels (FIXME: this is confusing)
=================
*/
static void Mod_SetupSubmodels (qmodel_t *mod)
{
	texture_t **const orig_textures = mod->textures;
	int const		  orig_numtextures = mod->numtextures;

	int		  i, j;
	float	  radius;
	dmodel_t *bm;

	// johnfitz -- okay, so that i stop getting confused every time i look at this loop, here's how it works:
	// we're looping through the submodels starting at 0.  Submodel 0 is the main model, so we don't have to
	// worry about clobbering data the first time through, since it's the same data.  At the end of the loop,
	// we create a new copy of the data to use the next time through.
	for (i = 0; i < mod->numsubmodels; i++)
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for (j = 1; j < MAX_MAP_HULLS; j++)
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
			mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
		}

		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy (bm->maxs, mod->maxs);
		VectorCopy (bm->mins, mod->mins);

		// johnfitz -- calculate rotate bounds and yaw bounds
		radius = RadiusFromBounds (mod->mins, mod->maxs);
		mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = mod->ymaxs[0] = mod->ymaxs[1] = mod->ymaxs[2] = radius;
		mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = mod->ymins[0] = mod->ymins[1] = mod->ymins[2] = -radius;
		// johnfitz

		// johnfitz -- correct physics cullboxes so that outlying clip brushes on doors and stuff are handled right
		if (i > 0 || strcmp (mod->name, sv.modelname) != 0) // skip submodel 0 of sv.worldmodel, which is the actual world
		{
			// start with the hull0 bounds
			VectorCopy (mod->maxs, mod->clipmaxs);
			VectorCopy (mod->mins, mod->clipmins);

			// process hull1 (we don't need to process hull2 becuase there's
			// no such thing as a brush that appears in hull2 but not hull1)
			// Mod_BoundsFromClipNode (mod, 1, mod->hulls[1].firstclipnode); // (disabled for now becuase it fucks up on rotating models)
		}
		// johnfitz

		mod->numleafs = bm->visleafs;

		mod->textures = orig_textures;
		mod->numtextures = orig_numtextures;
		Mod_CalcSpecialsAndTextures (mod);

		if (i < mod->numsubmodels - 1)
		{ // duplicate the basic information
			char name[12];

			q_snprintf (name, sizeof (name), "*%i", i + 1);
			qmodel_t *submodel = Mod_FindName (name);
			*submodel = *mod;
			strcpy (submodel->name, name);
			// Need to NULL this otherwise we double delete in PScript_ClearSurfaceParticles
			submodel->skytrimem = NULL;
			mod = submodel;
		}
	}
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel (qmodel_t *mod, const char *loadname, void *buffer)
{
	int		   i;
	int		   bsp2;
	dheader_t *header;

	mod->type = mod_brush;
	mod->is_worldmodel = (sv.modelname[0] && !q_strcasecmp (loadname, sv.name));

	header = (dheader_t *)buffer;

	mod->bspversion = LittleLong (header->version);

	switch (mod->bspversion)
	{
	case BSPVERSION:
		bsp2 = false;
		break;
#ifdef BSP29_VALVE
	case BSPVERSION_VALVE:
		bsp2 = false;
		break;
#endif
	case BSP2VERSION_2PSB:
		bsp2 = 1; // first iteration
		break;
	case BSP2VERSION_BSP2:
		bsp2 = 2; // sanitised revision
		break;
	case BSPVERSION_QUAKE64:
		bsp2 = false;
		break;
	default:
		Sys_Error ("Mod_LoadBrushModel: %s has unsupported version number (%i)", mod->name, mod->bspversion);
		break;
	}

	// swap all the lumps
	byte *mod_base = (byte *)header;

	for (i = 0; i < (int)sizeof (dheader_t) / 4; i++)
		((int *)header)[i] = LittleLong (((int *)header)[i]);

	// load into heap
	Mod_LoadVertexes (mod, mod_base, &header->lumps[LUMP_VERTEXES]);
	Mod_LoadEdges (mod, mod_base, &header->lumps[LUMP_EDGES], bsp2);
	Mod_LoadSurfedges (mod, mod_base, &header->lumps[LUMP_SURFEDGES]);
	Mod_LoadEntities (mod, mod_base, &header->lumps[LUMP_ENTITIES]);
	Mod_LoadTextures (mod, mod_base, &header->lumps[LUMP_TEXTURES]);
	Mod_LoadLighting (mod, mod_base, &header->lumps[LUMP_LIGHTING]);
	Mod_LoadPlanes (mod, mod_base, &header->lumps[LUMP_PLANES]);
	Mod_LoadTexinfo (mod, mod_base, &header->lumps[LUMP_TEXINFO]);
	Mod_LoadFaces (mod, mod_base, &header->lumps[LUMP_FACES], bsp2);
	Mod_LoadMarksurfaces (mod, mod_base, &header->lumps[LUMP_MARKSURFACES], bsp2);

	if (mod->bspversion == BSPVERSION && external_vis.value && mod->is_worldmodel)
	{
		FILE *fvis;
		Con_DPrintf ("trying to open external vis file\n");
		fvis = Mod_FindVisibilityExternal (mod, loadname);
		if (fvis)
		{
			mod->leafs = NULL;
			mod->numleafs = 0;
			Con_DPrintf ("found valid external .vis file for map\n");
			mod->visdata = Mod_LoadVisibilityExternal (fvis);
			if (mod->visdata)
			{
				Mod_LoadLeafsExternal (mod, fvis);
			}
			fclose (fvis);
			if (mod->visdata && mod->leafs && mod->numleafs)
			{
				goto visdone;
			}
			Con_DPrintf ("External VIS data failed, using standard vis.\n");
		}
	}

	Mod_LoadVisibility (mod, mod_base, &header->lumps[LUMP_VISIBILITY]);
	Mod_LoadLeafs (mod, mod_base, &header->lumps[LUMP_LEAFS], bsp2);
visdone:
	Mod_LoadNodes (mod, mod_base, &header->lumps[LUMP_NODES], bsp2);
	Mod_LoadClipnodes (mod, mod_base, &header->lumps[LUMP_CLIPNODES], bsp2);
	Mod_LoadSubmodels (mod, mod_base, &header->lumps[LUMP_MODELS]);

	Mod_MakeHull0 (mod);

	mod->numframes = 2; // regular and alternate animation

	Mod_CheckWaterVis (mod);
	Mod_SetupSubmodels (mod);
}

/*
=================
Mod_SanitizeMapDescription

Cleans up map descriptions:
- removes colors
- replaces newlines with spaces
- replaces consecutive spaces with single one
- removes leading/trailing spaces

Returns dst string length (excluding NUL terminator)
=================
*/
size_t Mod_SanitizeMapDescription (char *dst, size_t dstsize, const char *src)
{
	int srcpos, dstpos;

	if (!dstsize)
		return 0;

	for (srcpos = dstpos = 0; src[srcpos] && (size_t)dstpos + 1 < dstsize; srcpos++)
	{
		char c = src[srcpos] & 0x7f; // remove color
		if (c == '\n' || c == '\r')	 // replace newlines with spaces
			c = ' ';
		else if (c == '\\' && src[srcpos + 1] == 'n') // replace '\\' followed by 'n' with space
		{
			c = ' ';
			srcpos++;
		}
		// remove leading spaces, replace consecutive spaces with single one
		if (c != ' ' || (dstpos > 0 && dst[dstpos - 1] != c))
			dst[dstpos++] = c;
	}
	// remove trailing space, if any
	if (dstpos > 0 && dst[dstpos - 1] == ' ')
		--dstpos;

	dst[dstpos] = '\0';
	return dstpos;
}

/*
=================
Mod_LoadMapDescription

Parses the entity lump in the given map to find its worldspawn message
Writes at most maxchars bytes to dest, including the NUL terminator
Returns true if map is playable, false otherwise
=================
*/
qboolean Mod_LoadMapDescription (char *desc, size_t maxchars, const char *map)
{
	char		buf[4 * 1024];
	char		path[MAX_QPATH];
	const char *data;
	FILE	   *f;
	lump_t	   *entlump;
	dheader_t	header;
	int			i;
	qfileofs_t	filesize;
	qboolean	ret = false;

	if (!maxchars)
		return false;
	*desc = '\0';

	if ((size_t)q_snprintf (path, sizeof (path), "maps/%s.bsp", map) >= sizeof (path))
		return false;

	filesize = COM_FOpenFile (path, &f, NULL);
	if (filesize <= (qfileofs_t)sizeof (header))
	{
		if (filesize != -1)
			fclose (f);
		return false;
	}

	if (fread (&header, sizeof (header), 1, f) != 1)
	{
		fclose (f);
		return false;
	}

	header.version = LittleLong (header.version);

	switch (header.version)
	{
	case BSPVERSION:
	case BSP2VERSION_2PSB:
	case BSP2VERSION_BSP2:
	case BSPVERSION_QUAKE64:
		break;
	default:
		fclose (f);
		return false;
	}

	for (i = 1; i < (int)(sizeof (header) / sizeof (int)); i++)
		((int *)&header)[i] = LittleLong (((int *)&header)[i]);

	entlump = &header.lumps[LUMP_ENTITIES];
	if (entlump->filelen < 0 || entlump->filelen >= filesize || entlump->fileofs < 0 || entlump->fileofs + entlump->filelen > filesize)
	{
		fclose (f);
		return false;
	}

	// if the entity lump is large enough we assume the map is playable
	// and only try to parse the first entity (worldspawn) for the map title
	if (entlump->filelen >= (int)sizeof (buf))
	{
		ret = true;
		entlump->filelen = sizeof (buf) - 1;
	}

	Sys_fseek (f, (qfileofs_t)entlump->fileofs - sizeof (header), SEEK_CUR);
	i = fread (buf, 1, entlump->filelen, f);
	fclose (f);

	if (i <= 0)
		return false;
	buf[i] = '\0';

	for (i = 0, data = buf; data; i++)
	{
		data = COM_Parse (data);
		if (!data || com_token[0] != '{')
			return ret;

		while (1)
		{
			qboolean is_message;
			qboolean is_classname;

			// parse key
			data = COM_Parse (data);
			if (!data)
				return ret;
			if (com_token[0] == '}')
				break;

			is_message = i == 0 && !strcmp (com_token, "message");
			is_classname = i != 0 && !strcmp (com_token, "classname");

			// parse value
			data = COM_ParseEx (data, CPE_ALLOWTRUNC);
			if (!data)
				return ret;

			if (is_message)
			{
				Mod_SanitizeMapDescription (desc, maxchars, com_token);
				if (ret)
					return true;
			}
			else if (is_classname)
			{
#define CLASSNAME_STARTS_WITH(str) (!strncmp (com_token, str, strlen (str)))
#define CLASSNAME_IS(str)		   (!strcmp (com_token, str))

				if (CLASSNAME_STARTS_WITH ("info_player_") || CLASSNAME_STARTS_WITH ("ammo_") || CLASSNAME_STARTS_WITH ("weapon_") ||
					CLASSNAME_STARTS_WITH ("monster_") || CLASSNAME_IS ("trigger_changelevel"))
				{
					return true;
				}

#undef CLASSNAME_IS
#undef CLASSNAME_STARTS_WITH
			}
		}
	}

	return ret;
}

/*
==============================================================================

ALIAS MODELS

==============================================================================
*/

stvert_t stverts[MAXALIASVERTS];

mtriangle_t	 *triangles = NULL;
static size_t triangles_size = 0;

// a pose is a single set of vertexes.  a frame may be
// an animating sequence of poses
trivertx_t *poseverts[MAXALIASFRAMES];
static int	posenum;

/*
=================
Mod_LoadAliasFrame
=================
*/
void *Mod_LoadAliasFrame (void *pin, aliashdr_t *pheader, const int index)
{
	maliasframedesc_t *frame = &pheader->frames[index];
	trivertx_t		  *pinframe;
	int				   i;
	daliasframe_t	  *pdaliasframe;

	if (posenum >= MAXALIASFRAMES)
		Sys_Error ("posenum >= MAXALIASFRAMES");

	pdaliasframe = (daliasframe_t *)pin;

	q_strlcpy (frame->name, pdaliasframe->name, sizeof (frame->name));
	frame->firstpose = posenum;
	frame->numposes = 1;

	for (i = 0; i < 3; i++)
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
void *Mod_LoadAliasGroup (void *pin, aliashdr_t *pheader, const int index)
{
	assert (pheader->poseverttype == PV_QUAKE1);

	maliasframedesc_t *frame = &pheader->frames[index];
	daliasgroup_t	  *pingroup;
	int				   i, numframes;
	daliasinterval_t  *pin_intervals;
	void			  *ptemp;

	pingroup = (daliasgroup_t *)pin;

	numframes = LittleLong (pingroup->numframes);

	frame->firstpose = posenum;
	frame->numposes = numframes;

	for (i = 0; i < 3; i++)
	{
		// these are byte values, so we don't have to worry about endianness
		frame->bboxmin.v[i] = pingroup->bboxmin.v[i];
		frame->bboxmax.v[i] = pingroup->bboxmax.v[i];
	}

	pin_intervals = (daliasinterval_t *)(pingroup + 1);

	frame->interval = LittleFloat (pin_intervals->interval);

	pin_intervals += numframes;

	ptemp = (void *)pin_intervals;

	for (i = 0; i < numframes; i++)
	{
		if (posenum >= MAXALIASFRAMES)
			Sys_Error ("posenum >= MAXALIASFRAMES");

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
	short x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP(off, dx, dy)                           \
	do                                                        \
	{                                                         \
		if (pos[off] == fillcolor)                            \
		{                                                     \
			pos[off] = 255;                                   \
			fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
			inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;          \
		}                                                     \
		else if (pos[off] != 255)                             \
			fdc = pos[off];                                   \
	} while (0)

static void Mod_FloodFillSkin (byte *skin, int skinwidth, int skinheight)
{
	byte fillcolor = *skin; // assume this is the pixel to fill
	int	 inpt = 0, outpt = 0;
	int	 filledcolor = -1;
	int	 i;

	TEMP_ALLOC (floodfill_t, fifo, FLOODFILL_FIFO_SIZE);

	if (filledcolor == -1)
	{
		filledcolor = 0;
		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		// printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int	  x = fifo[outpt].x, y = fifo[outpt].y;
		int	  fdc = filledcolor;
		byte *pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)
			FLOODFILL_STEP (-1, -1, 0);
		if (x < skinwidth - 1)
			FLOODFILL_STEP (1, 1, 0);
		if (y > 0)
			FLOODFILL_STEP (-skinwidth, 0, -1);
		if (y < skinheight - 1)
			FLOODFILL_STEP (skinwidth, 0, 1);
		skin[x + skinwidth * y] = fdc;
	}

	TEMP_FREE (fifo);
}

static gltexture_t *Mod_LoadFullbrightTexture (qmodel_t *mod, aliashdr_t *surf, const char *texname)
{
	// make a safe copy of texname to manage va() trensient usage
	char texname_copy[MAX_QPATH];
	q_strlcpy (texname_copy, texname, MAX_QPATH);

	// try to find matching glow texture :
	unsigned int fb_width = 0;
	unsigned int fb_height = 0;

	// unsupported format by default
	enum srcformat fb_fmt = SRC_INDEXED;

	void *fb_data = Image_LoadImage (texname_copy, (int *)&fb_width, (int *)&fb_height, &fb_fmt, mod->path_id);

	// fb texture found:
	if (fb_data)
	{
		if (fb_fmt != SRC_RGBA)
		{
			Con_Warning ("%s fbrights not RGBA, skipped.\n", texname_copy);
			Mem_Free (fb_data);
			return NULL;
		}

		// Normalize pixels for additive blending as in INDEXED: fullbright pixels have alpha > 0 => force alpha = 255 anyway.
		// otherwhise for transparent pixels (alpha = 0) => force alapha = 255 AND force color = black.
		for (size_t pixel_index = 0; pixel_index < (size_t)fb_width * (size_t)fb_height; pixel_index++)
		{
			uint32_t *rgba_pixel = (uint32_t *)fb_data + pixel_index;
			byte	 *rgba_component = (byte *)rgba_pixel;

			// not transparent pixels are the fulbright ones, otherwise they are the mask.
			if (rgba_component[3] == 0)
			{
				// transparent pixels / mask are forced to black.
				rgba_component[0] = 0;
				rgba_component[1] = 0;
				rgba_component[2] = 0;
			}

			// always force alpha = 255 for all pixels
			rgba_component[3] = 255;
		}

		gltexture_t *loaded_texture =
			TexMgr_LoadImage (mod, texname_copy, fb_width, fb_height, SRC_RGBA, (byte *)fb_data, texname_copy, 0, TEXPREF_ALPHA | TEXPREF_MIPMAP);
		Mem_Free (fb_data);

		return loaded_texture;
	}

	return NULL;
}

/*
===============
Mod_LoadSkinTask
===============
*/
typedef struct load_skin_task_args_s
{
	aliashdr_t *pheader;
	qmodel_t   *mod;
	byte	   *mod_base;
	byte	  **ppskintypes;
	const char *skin_source;
} load_skin_task_args_t;

static int Mod_AliasSkinSize (const aliashdr_t *pheader, const char *model_name)
{
	size_t size;

	if (pheader->skinwidth <= 0 || pheader->skinheight <= 0 ||
		!Mod_CheckedSizeMul ((size_t)pheader->skinwidth, (size_t)pheader->skinheight, &size) || size > INT_MAX)
		Sys_Error ("model %s has invalid skin dimensions (%d x %d)", model_name, pheader->skinwidth, pheader->skinheight);

	return (int)size;
}

static void Mod_LoadSkinTask (int i, load_skin_task_args_t *args)
{
	int			 j, k, size, groupskins;
	char		 name[MAX_QPATH];
	byte		*skin, *texels;
	byte		*pskintype = args->ppskintypes[i];
	byte		*pinskingroup;
	byte		*pinskinintervals;
	char		 fbr_mask_name[MAX_QPATH]; // johnfitz -- added for fullbright support
	src_offset_t offset;				   // johnfitz
	unsigned int texflags = TEXPREF_PAD;
	qmodel_t	*mod = args->mod;
	byte		*mod_base = args->mod_base;
	aliashdr_t	*pheader = args->pheader;
	const char	*skin_source = args->skin_source ? args->skin_source : mod->name;

	size = Mod_AliasSkinSize (pheader, mod->name);

	if (mod->flags & MF_HOLEY)
		texflags |= TEXPREF_ALPHA;

	if (ReadLongUnaligned (pskintype + offsetof (daliasskintype_t, type)) == ALIAS_SKIN_SINGLE)
	{
		skin = pskintype + sizeof (daliasskintype_t);
		Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);

		// save 8 bit texels for the player model to remap
		texels = (byte *)Mem_Alloc (size);
		pheader->texels[i] = texels;
		memcpy (texels, skin, size);

		pheader->gltextures[i][0] = NULL;
		pheader->fbtextures[i][0] = NULL;

		// try to load external textures first, if enabled.
		if (mdl_external_textures.value > 0.0f)
		{
			unsigned int   fwidth = 0;
			unsigned int   fheight = 0;
			void		  *data = NULL;
			// unsupported format by default
			enum srcformat fmt = SRC_INDEXED;

			if (!data)
				data = Image_LoadImage (va ("%s_%i", mod->name, i), (int *)&fwidth, (int *)&fheight, &fmt, mod->path_id);

			if (!data)
				data = Image_LoadImage (va ("progs/%s_%i", mod->name, i), (int *)&fwidth, (int *)&fheight, &fmt, mod->path_id);

			if (!data)
				data = Image_LoadImage (va ("textures/%s_%i", mod->name, i), (int *)&fwidth, (int *)&fheight, &fmt, mod->path_id);

			if (data)
			{
				if (fmt == SRC_RGBA)
				{
					pheader->gltextures[i][0] = TexMgr_LoadImage (
						mod, va ("%s_%i", mod->name, i), fwidth, fheight, fmt, data, va ("%s_%i", mod->name, i), 0, TEXPREF_ALPHA | TEXPREF_MIPMAP);

#define TRY_LOAD_FULLBRIGHTS(tex_name)                                                      \
	do                                                                                      \
	{                                                                                       \
		if (!pheader->fbtextures[i][0])                                                     \
			pheader->fbtextures[i][0] = Mod_LoadFullbrightTexture (mod, pheader, tex_name); \
	} while (0);

					// try to load the external fullbright texture, if any.
					assert (pheader->fbtextures[i][0] == NULL);

					TRY_LOAD_FULLBRIGHTS (va ("%s_%i_glow", mod->name, i));
					TRY_LOAD_FULLBRIGHTS (va ("%s_%i_luma", mod->name, i));
					TRY_LOAD_FULLBRIGHTS (va ("progs/%s_%i_glow", mod->name, i));
					TRY_LOAD_FULLBRIGHTS (va ("progs/%s_%i_luma", mod->name, i));
					TRY_LOAD_FULLBRIGHTS (va ("textures/%s_%i_glow", mod->name, i));
					TRY_LOAD_FULLBRIGHTS (va ("textures/%s_%i_luma", mod->name, i));
				}
				else
				{
					Con_Warning ("%s skin not RGBA, skipped.\n", va ("%s_%i", mod->name, i));
				}
			}

			Mem_Free (data);
		} // end if mdl_external_textures

		if (!pheader->gltextures[i][0])
		{
			// johnfitz -- rewritten
			q_snprintf (name, sizeof (name), "%s:frame%i", mod->name, i);
			offset = (src_offset_t)(skin) - (src_offset_t)mod_base;
			if (Mod_CheckFullbrights (skin, size))
			{
				pheader->gltextures[i][0] = TexMgr_LoadImage (
					mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, skin_source, offset, texflags | TEXPREF_MIPMAP | TEXPREF_NOBRIGHT);
				q_snprintf (fbr_mask_name, sizeof (fbr_mask_name), "%s:frame%i_glow", mod->name, i);
				pheader->fbtextures[i][0] = TexMgr_LoadImage (
					mod, fbr_mask_name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, skin_source, offset,
					texflags | TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT);
			}
			else
			{
				pheader->gltextures[i][0] =
					TexMgr_LoadImage (mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, skin_source, offset, texflags | TEXPREF_MIPMAP);
				pheader->fbtextures[i][0] = NULL;
			}
		}

		pheader->gltextures[i][3] = pheader->gltextures[i][2] = pheader->gltextures[i][1] = pheader->gltextures[i][0];
		pheader->fbtextures[i][3] = pheader->fbtextures[i][2] = pheader->fbtextures[i][1] = pheader->fbtextures[i][0];
		// johnfitz
	}
	else
	{
		// animating skin group.  yuck.
		pinskingroup = pskintype + sizeof (daliasskintype_t);
		groupskins = ReadLongUnaligned (pinskingroup + offsetof (daliasskingroup_t, numskins));
		pinskinintervals = pinskingroup + sizeof (daliasskingroup_t);
		skin = pinskinintervals + (groupskins * sizeof (daliasskininterval_t));

		for (j = 0; j < groupskins; j++)
		{
			Mod_FloodFillSkin (skin, pheader->skinwidth, pheader->skinheight);
			if (j == 0)
			{
				texels = (byte *)Mem_Alloc (size);
				pheader->texels[i] = texels;
				memcpy (texels, skin, size);
			}

			// johnfitz -- rewritten
			q_snprintf (name, sizeof (name), "%s:frame%i_%i", mod->name, i, j);
			offset = (src_offset_t)(skin) - (src_offset_t)mod_base; // johnfitz
			if (Mod_CheckFullbrights (skin, size))
			{
				pheader->gltextures[i][j & 3] = TexMgr_LoadImage (
					mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, skin_source, offset, texflags | TEXPREF_MIPMAP | TEXPREF_NOBRIGHT);
				q_snprintf (fbr_mask_name, sizeof (fbr_mask_name), "%s:frame%i_%i_glow", mod->name, i, j);
				pheader->fbtextures[i][j & 3] = TexMgr_LoadImage (
					mod, fbr_mask_name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, skin_source, offset,
					texflags | TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT);
			}
			else
			{
				pheader->gltextures[i][j & 3] =
					TexMgr_LoadImage (mod, name, pheader->skinwidth, pheader->skinheight, SRC_INDEXED, skin, skin_source, offset, texflags | TEXPREF_MIPMAP);
				pheader->fbtextures[i][j & 3] = NULL;
			}
			// johnfitz

			skin += size;
		}
		k = j;
		for (/**/; j < 4; j++)
			pheader->gltextures[i][j & 3] = pheader->gltextures[i][j - k];
	}
#undef TRY_LOAD_FULLBRIGHTS
}

/*
===============
Mod_LoadAllSkins
===============
*/
void *Mod_LoadAllSkins (aliashdr_t *pheader, qmodel_t *mod, byte *mod_base,
	int numskins, byte *pskintype, const char *skin_source)
{
	assert (pheader->poseverttype == PV_QUAKE1);

	if (numskins < 1 || numskins > MAX_SKINS)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of skins: %d", numskins);

	TEMP_ALLOC (byte *, ppskintypes, numskins);
	int size = Mod_AliasSkinSize (pheader, mod->name);
	for (int i = 0; i < numskins; i++)
	{
		ppskintypes[i] = pskintype;
		if (ReadLongUnaligned (pskintype + offsetof (daliasskintype_t, type)) == ALIAS_SKIN_SINGLE)
		{
			pskintype += sizeof (daliasskintype_t) + size;
		}
		else
		{
			// animating skin group.  yuck.
			byte *pinskingroup = pskintype + sizeof (daliasskintype_t);
			int	  groupskins = ReadLongUnaligned (pinskingroup + offsetof (daliasskingroup_t, numskins));
			byte *pinskinintervals = pinskingroup + sizeof (daliasskingroup_t);
			byte *skin = pinskinintervals + (groupskins * sizeof (daliasskininterval_t));
			pskintype = skin + (groupskins * size);
		}
	}

	load_skin_task_args_t args = {
		.pheader = pheader,
		.mod = mod,
		.mod_base = mod_base,
		.ppskintypes = ppskintypes,
		.skin_source = skin_source,
	};
	if (!Tasks_IsWorker () && (numskins > 1))
	{
		task_handle_t task = Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_LoadSkinTask, numskins, &args, sizeof (args));
		Task_Join (task, TASK_TIMEOUT_INFINITE);
	}
	else
	{
		for (int i = 0; i < numskins; i++)
		{
			Mod_LoadSkinTask (i, &args);
		}
	}

	TEMP_FREE (ppskintypes);
	return (void *)pskintype;
}

//=========================================================================

/*
=================
Mod_CalcAliasBounds -- johnfitz -- calculate bounds of alias model for nonrotated, yawrotated, and fullrotated cases
=================
*/
static void Mod_CalcAliasBounds (qmodel_t *mod, aliashdr_t *a, int numvertexes, byte *vertexes)
{
	float  dist, yawradius, radius;
	vec3_t v;

	// clear out all data
	for (int i = 0; i < 3; i++)
	{
		mod->mins[i] = mod->ymins[i] = mod->rmins[i] = FLT_MAX;
		mod->maxs[i] = mod->ymaxs[i] = mod->rmaxs[i] = -FLT_MAX;
		radius = yawradius = 0;
	}

	switch (a->poseverttype)
	{
	case PV_QUAKE1:
	{
		// process verts
		for (int i = 0; i < a->numposes; i++)
		{
			for (int j = 0; j < a->numverts; j++)
			{
				for (int k = 0; k < 3; k++)
					v[k] = poseverts[i][j].v[k] * a->scale[k] + a->scale_origin[k];

				for (int k = 0; k < 3; k++)
				{
					mod->mins[k] = q_min (mod->mins[k], v[k]);
					mod->maxs[k] = q_max (mod->maxs[k], v[k]);
				}
				dist = v[0] * v[0] + v[1] * v[1];
				if (yawradius < dist)
					yawradius = dist;
				dist += v[2] * v[2];
				if (radius < dist)
					radius = dist;
			}
		}
	}
	break;
	case PV_MD5:
	{
		// process verts : (vertexes;numvertexes) is all the vertices from all the poses/frames
		// for all the surfaces of a model.
		md5vert_t *pv = (md5vert_t *)vertexes;
		for (int j = 0; j < numvertexes; j++)
		{
			for (int k = 0; k < 3; k++)
				v[k] = pv[j].xyz[k];

			for (int k = 0; k < 3; k++)
			{
				mod->mins[k] = q_min (mod->mins[k], v[k]);
				mod->maxs[k] = q_max (mod->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}
	}
	break;
	case PV_MD5_8:
	{
		// process verts : (vertexes;numvertexes) is all the vertices from all the poses/frames
		// for all the surfaces of a model.
		md5vert8_t *pv = (md5vert8_t *)vertexes;
		for (int j = 0; j < numvertexes; j++)
		{
			for (int k = 0; k < 3; k++)
				v[k] = pv[j].xyz[k];

			for (int k = 0; k < 3; k++)
			{
				mod->mins[k] = q_min (mod->mins[k], v[k]);
				mod->maxs[k] = q_max (mod->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}
	}
	break;
	case PV_QUAKE3:
	{
		// process verts : (vertexes;numvertexes) is all the vertices from all the poses/frames
		// for all the surfaces of a model.
		md3XyzNormal_t *pv = (md3XyzNormal_t *)vertexes;
		for (int j = 0; j < numvertexes; j++)
		{
			for (int k = 0; k < 3; k++)
				v[k] = pv[j].xyz[k] * MD3_XYZ_SCALE;

			for (int k = 0; k < 3; k++)
			{
				mod->mins[k] = q_min (mod->mins[k], v[k]);
				mod->maxs[k] = q_max (mod->maxs[k], v[k]);
			}
			dist = v[0] * v[0] + v[1] * v[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += v[2] * v[2];
			if (radius < dist)
				radius = dist;
		}
	}
	break;
	default:
		assert (false);
	}

	// rbounds will be used when entity has nonzero pitch or roll
	radius = sqrtf (radius);
	mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = -radius;
	mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = radius;

	// ybounds will be used when entity has nonzero yaw
	yawradius = sqrtf (yawradius);
	mod->ymins[0] = mod->ymins[1] = -yawradius;
	mod->ymaxs[0] = mod->ymaxs[1] = yawradius;
	mod->ymins[2] = mod->mins[2];
	mod->ymaxs[2] = mod->maxs[2];
}

static qboolean nameInList (const char *list, const char *name)
{
	const char *s;
	char		tmp[MAX_QPATH];
	int			i;

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
		// compare it to the model name
		if (!strcmp (name, tmp))
		{
			return true;
		}
		// search forwards to the next comma or end of string
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
	extern cvar_t r_nolerp_list;

	if (!mod)
		return;

	mod->flags &= (0xFF | MF_HOLEY); // only preserve first byte, plus MF_HOLEY

	if (mod->type == mod_alias)
	{
		// nolerp flag
		if (nameInList (r_nolerp_list.string, mod->name))
			mod->flags |= MOD_NOLERP;

		// fullbright hack (TODO: make this a cvar list)
		if (!strcmp (mod->name, "progs/flame2.mdl") || !strcmp (mod->name, "progs/flame.mdl") || !strcmp (mod->name, "progs/boss.mdl"))
			mod->flags |= MOD_FBRIGHTHACK;
	}

	PScript_UpdateModelEffects (mod);
}

static void check_tris_size (size_t numtris)
{
	// 1. assure that numtris < trinagles_size, else realloc
	if (numtris > triangles_size)
	{
		size_t new_trinagles_size = q_max (triangles_size * 2, numtris);

		triangles = Mem_Realloc (triangles, new_trinagles_size * sizeof (mtriangle_t));

		triangles_size = new_trinagles_size;
	}
}

static void Mod_CacheStockAxeEdge (qmodel_t *mod, byte *mod_base,
	qfilesize_t source_size, aliashdr_t *pheader)
{
	static const float expected_scale[3] = {
		0.2244189084f, 0.2454846501f, 0.2942478061f
	};
	const int ready_frame = 0;
	const int base_vertex = 83;
	const int tip_vertex = 82;
	stockaxe_edge_t edge;
	uint32_t source_crc32;
	qboolean connected = false;

	if (strcmp (mod->name, "progs/v_axe.mdl") || source_size != 57908)
		return;

	source_crc32 = (uint32_t)mz_crc32 (MZ_CRC32_INIT, mod_base,
		(size_t)source_size);
	if (source_crc32 != 0x2aa03605u ||
		ReadLongUnaligned (mod_base + offsetof (mdl_t, ident)) != IDPOLYHEADER ||
		ReadLongUnaligned (mod_base + offsetof (mdl_t, version)) != ALIAS_VERSION ||
		pheader->poseverttype != PV_QUAKE1 || pheader->numverts != 98 ||
		pheader->numtris != 184 || pheader->numframes != 9 ||
		pheader->frames[ready_frame].numposes != 1 ||
		pheader->frames[ready_frame].firstpose != 0 || pheader->numposes < 1 ||
		!poseverts[pheader->frames[ready_frame].firstpose])
		return;

	for (int axis = 0; axis < 3; ++axis)
	{
		float lower = expected_scale[axis] - 0.0000001f;
		float upper = expected_scale[axis] + 0.0000001f;
		if (!(pheader->scale[axis] > lower && pheader->scale[axis] < upper))
			return;
	}

	for (int i = 0; i < pheader->numtris && !connected; ++i)
	{
		qboolean has_base = false, has_tip = false;
		for (int j = 0; j < 3; ++j)
		{
			has_base |= triangles[i].vertindex[j] == base_vertex;
			has_tip |= triangles[i].vertindex[j] == tip_vertex;
		}
		connected = has_base && has_tip;
	}
	if (!connected)
		return;

	memset (&edge, 0, sizeof (edge));
	for (int axis = 0; axis < 3; ++axis)
	{
		edge.base[axis] = poseverts[0][base_vertex].v[axis] *
			pheader->scale[axis] + pheader->scale_origin[axis];
		edge.tip[axis] = poseverts[0][tip_vertex].v[axis] *
			pheader->scale[axis] + pheader->scale_origin[axis];
	}
	edge.valid = true;
	mod->stockaxe_edge = edge;
}

/* Alkaline and LimJam ship identical v_alkaxe20fps.mdl bytes. In frame 0,
 * vertices 74 and 77 span the adjacent blade triangles 79 and 80. */
static void Mod_CacheAlkalineAxeEdge (qmodel_t *mod, byte *mod_base,
	qfilesize_t source_size, const aliashdr_t *pheader)
{
	stockaxe_edge_t edge;

	if (strcmp (mod->name, "progs/v_alkaxe20fps.mdl") || source_size != 96428 ||
		(uint32_t)mz_crc32 (MZ_CRC32_INIT, mod_base, (size_t)source_size) != 0x3003ca78u ||
		ReadLongUnaligned (mod_base + offsetof (mdl_t, ident)) != IDPOLYHEADER ||
		ReadLongUnaligned (mod_base + offsetof (mdl_t, version)) != ALIAS_VERSION ||
		pheader->poseverttype != PV_QUAKE1 || pheader->numverts != 205 ||
		pheader->numtris != 246 || pheader->numframes != 53 ||
		pheader->frames[0].numposes != 1 ||
		pheader->frames[0].firstpose != 0 || pheader->numposes < 1 ||
		!poseverts[0] ||
		triangles[79].facesfront != 1 ||
		triangles[79].vertindex[0] != 81 ||
		triangles[79].vertindex[1] != 75 ||
		triangles[79].vertindex[2] != 74 ||
		triangles[80].facesfront != 1 ||
		triangles[80].vertindex[0] != 81 ||
		triangles[80].vertindex[1] != 77 ||
		triangles[80].vertindex[2] != 75)
		return;

	memset (&edge, 0, sizeof (edge));
	for (int axis = 0; axis < 3; ++axis)
	{
		edge.base[axis] = poseverts[0][74].v[axis] *
			pheader->scale[axis] + pheader->scale_origin[axis];
		edge.tip[axis] = poseverts[0][77].v[axis] *
			pheader->scale[axis] + pheader->scale_origin[axis];
	}
	edge.valid = true;
	mod->stockaxe_edge = edge;
}

/* A dominant split has no retained CPU pose stream after upload.
 * Reuse the existing two-point cache while the verified ready pose is live. */
static void Mod_CacheHeldMeleeEdge (qmodel_t *mod, const byte *mod_base,
	qfilesize_t source_size, const aliashdr_t *pheader)
{
	const mod_held_melee_recipe_t *recipe = Mod_HeldMeleeRecipeForName (mod->name);
	int pose;
	stockaxe_edge_t edge;

	if (!recipe || source_size < 0 || (size_t)source_size != recipe->generated_size ||
		(uint32_t)mz_crc32 (MZ_CRC32_INIT, mod_base,
			(size_t)source_size) != recipe->generated_crc ||
		pheader->poseverttype != PV_QUAKE1 || pheader->numverts != recipe->vertices ||
		pheader->numtris != recipe->triangles || pheader->numframes != recipe->frames)
		return;
	pose = pheader->frames[recipe->ready_frame].firstpose;
	if (pheader->frames[recipe->ready_frame].numposes != 1 ||
		pose < 0 || pose >= pheader->numposes || !poseverts[pose])
		return;

	memset (&edge, 0, sizeof (edge));
	for (int axis = 0; axis < 3; ++axis)
	{
		edge.base[axis] = poseverts[pose][recipe->edge_vertices[0]].v[axis] *
			pheader->scale[axis] + pheader->scale_origin[axis];
		edge.tip[axis] = poseverts[pose][recipe->edge_vertices[1]].v[axis] *
			pheader->scale[axis] + pheader->scale_origin[axis];
	}
	edge.valid = true;
	mod->stockaxe_edge = edge;
}

/* The splitter pins these source bytes. Keep only the donor's eight-palm
 * centroid for each hand/pose; GL_MakeAliasModelDisplayLists discards the
 * original CPU vertices after upload. Coordinates remain compressed MDL units
 * so the shared alias matrix supplies scale, rotation and held calibration. */
static void Mod_CacheQBJ3BerserkPalms (qmodel_t *mod, const byte *mod_base,
	qfilesize_t source_size, const aliashdr_t *pheader)
{
	static const int palms[2][8] = {
		{499, 497, 498, 496, 501, 490, 424, 446},
		{213, 210, 211, 209, 214, 203, 137, 159}
	};
	vec3_t *means;

	if (strcmp (mod->name, "progs/v_berserk.mdl") ||
		q_strcasecmp (COM_SkipPath (com_gamedir), "qbj3") ||
		source_size != (qfilesize_t)QBJ3_MDL_BERSERK_SOURCE_SIZE ||
		(uint32_t)mz_crc32 (MZ_CRC32_INIT, mod_base, (size_t)source_size) != 0xc3af3566u ||
		pheader->poseverttype != PV_QUAKE1 || pheader->numverts != 894 ||
		pheader->numtris != 1240 || pheader->numframes != 101 ||
		pheader->numposes != 101)
		return;
	for (int pose = 0; pose < pheader->numposes; ++pose)
		if (pheader->frames[pose].numposes != 1 ||
			pheader->frames[pose].firstpose != pose || !poseverts[pose])
			return;

	means = Mem_Alloc (2 * (size_t)pheader->numposes * sizeof (*means));
	for (int hand = 0; hand < 2; ++hand)
		for (int pose = 0; pose < pheader->numposes; ++pose)
			for (int axis = 0; axis < 3; ++axis)
			{
				float sum = 0.0f;
				for (int vertex = 0; vertex < 8; ++vertex)
					sum += poseverts[pose][palms[hand][vertex]].v[axis];
				means[hand * pheader->numposes + pose][axis] = sum / 8.0f;
			}
	mod->qbj3_palm_centroids = means;
	mod->qbj3_palm_pose_count = pheader->numposes;
}

/*
=================
Mod_LoadAliasModel
=================
*/
static void Mod_LoadAliasModel (qmodel_t *mod, void *buffer,
	qfilesize_t source_size, const char *skin_source)
{
	int	  i, j;
	byte *pinstverts;
	byte *pintriangles;
	int	  version, numframes;
	size_t header_size, frame_desc_bytes;
	byte *pframetype;
	byte *pskintype;
	byte *mod_base = (byte *)buffer; // johnfitz

	if (source_size < (qfilesize_t)sizeof (mdl_t))
		Sys_Error ("model %s is shorter than an MDL header", mod->name);

	version = ReadLongUnaligned (mod_base + offsetof (mdl_t, version));
	if (version != ALIAS_VERSION)
		Sys_Error ("%s has wrong version number (%i should be %i)", mod->name, version, ALIAS_VERSION);
	numframes = ReadLongUnaligned (mod_base + offsetof (mdl_t, numframes));
	if (numframes < 1 || numframes > MAXALIASFRAMES)
		Sys_Error ("Mod_LoadAliasModel: Invalid # of frames: %d", numframes);

	//
	// allocate space for a working header, plus all the data except the frames,
	// skin and group info
	//
	if (!Mod_CheckedSizeMul ((size_t)(numframes - 1), sizeof (maliasframedesc_t), &frame_desc_bytes) ||
		!Mod_CheckedSizeAdd (sizeof (aliashdr_t), frame_desc_bytes, &header_size))
		Sys_Error ("Mod_LoadAliasModel: Header allocation is too large for %s", mod->name);
	aliashdr_t *pheader = (aliashdr_t *)Mem_Alloc (header_size);
	pheader->poseverttype = PV_QUAKE1;

	mod->flags = ReadLongUnaligned (mod_base + offsetof (mdl_t, flags));

	//
	// endian-adjust and copy the data, starting with the alias model header
	//
	pheader->boundingradius = ReadLongUnaligned (mod_base + offsetof (mdl_t, boundingradius));
	pheader->numskins = ReadLongUnaligned (mod_base + offsetof (mdl_t, numskins));
	pheader->skinwidth = ReadLongUnaligned (mod_base + offsetof (mdl_t, skinwidth));
	pheader->skinheight = ReadLongUnaligned (mod_base + offsetof (mdl_t, skinheight));

	if (pheader->skinheight > MAX_LBM_HEIGHT)
		Con_DWarning ("model %s has a skin taller than %d", mod->name, MAX_LBM_HEIGHT);

	pheader->numverts = ReadLongUnaligned (mod_base + offsetof (mdl_t, numverts));

	if (pheader->numverts <= 0)
		Sys_Error ("model %s has no vertices", mod->name);

	if (pheader->numverts > MAXALIASVERTS)
		Sys_Error ("model %s has too many vertices (%d; max = %d)", mod->name, pheader->numverts, MAXALIASVERTS);

	if (pheader->numverts > MAXALIASVERTS_QS)
		Con_DWarning ("model %s vertex count of %d exceeds QS limit of %d\n", mod->name, pheader->numverts, MAXALIASVERTS_QS);

	pheader->numtris = ReadLongUnaligned (mod_base + offsetof (mdl_t, numtris));

	if (pheader->numtris <= 0)
		Sys_Error ("model %s has no triangles", mod->name);

	if (pheader->numtris > MAXALIASTRIS_QS)
		Con_DWarning ("model %s triangle count of %d exceeds QS limit of %d\n", mod->name, pheader->numtris, MAXALIASTRIS_QS);

	check_tris_size (pheader->numtris);

	pheader->numframes = numframes;

	pheader->size = ReadFloatUnaligned (mod_base + offsetof (mdl_t, size)) * ALIAS_BASE_SIZE_RATIO;
	mod->synctype = (synctype_t)ReadLongUnaligned (mod_base + offsetof (mdl_t, synctype));
	mod->numframes = pheader->numframes;

	for (i = 0; i < 3; i++)
	{
		pheader->scale[i] = ReadFloatUnaligned (mod_base + offsetof (mdl_t, scale[i]));
		pheader->scale_origin[i] = ReadFloatUnaligned (mod_base + offsetof (mdl_t, scale_origin[i]));
		pheader->eyeposition[i] = ReadFloatUnaligned (mod_base + offsetof (mdl_t, eyeposition[i]));
	}

	//
	// load the skins
	//
	pskintype = mod_base + sizeof (mdl_t);
	pskintype = Mod_LoadAllSkins (pheader, mod, mod_base, pheader->numskins,
		pskintype, skin_source);

	//
	// load base s and t vertices
	//
	pinstverts = pskintype;

	for (i = 0; i < pheader->numverts; i++)
	{
		stverts[i].onseam = ReadLongUnaligned (pinstverts + offsetof (stvert_t, onseam));
		stverts[i].s = ReadLongUnaligned (pinstverts + offsetof (stvert_t, s));
		stverts[i].t = ReadLongUnaligned (pinstverts + offsetof (stvert_t, t));
		pinstverts += sizeof (stvert_t);
	}

	//
	// load triangle lists
	//
	pintriangles = pinstverts;

	for (i = 0; i < pheader->numtris; i++)
	{
		triangles[i].facesfront = ReadLongUnaligned (pintriangles + offsetof (dtriangle_t, facesfront));

		for (j = 0; j < 3; j++)
		{
			triangles[i].vertindex[j] = ReadLongUnaligned (pintriangles + offsetof (dtriangle_t, vertindex[j]));
		}
		pintriangles += sizeof (dtriangle_t);
	}

	//
	// load the frames
	//
	posenum = 0;
	pframetype = pintriangles;

	for (i = 0; i < numframes; i++)
	{
		aliasframetype_t frametype;
		frametype = (aliasframetype_t)ReadLongUnaligned (pframetype + offsetof (daliasframetype_t, type));
		if (frametype == ALIAS_SINGLE)
			pframetype = Mod_LoadAliasFrame (pframetype + sizeof (daliasframetype_t), pheader, i);
		else
			pframetype = Mod_LoadAliasGroup (pframetype + sizeof (daliasframetype_t), pheader, i);
	}

	pheader->numposes = posenum;

	/* Copy only the pinned ready-pose edge while the source pose is live. */
	Mod_CacheStockAxeEdge (mod, mod_base, source_size, pheader);
	Mod_CacheAlkalineAxeEdge (mod, mod_base, source_size, pheader);
	Mod_CacheHeldMeleeEdge (mod, mod_base, source_size, pheader);
	Mod_CacheQBJ3BerserkPalms (mod, mod_base, source_size, pheader);

	mod->type = mod_alias;

	Mod_SetExtraFlags (mod); // johnfitz

	Mod_CalcAliasBounds (mod, pheader, 0, NULL); // johnfitz

	//
	// build the draw lists
	//
	GL_MakeAliasModelDisplayLists (mod, pheader);

	//
	// move the complete, relocatable alias model to the cache
	//
	mod->extradata[PV_QUAKE1] = (byte *)pheader;
}

//=============================================================================

/*
=================
Mod_LoadSpriteFrame
=================
*/
static void *Mod_LoadSpriteFrame (qmodel_t *mod, byte *mod_base, void *pin, mspriteframe_t **ppframe, int framenum)
{
	dspriteframe_t inframe;
	mspriteframe_t *pspriteframe;
	byte			*frame_data;
	int				width, height, size, origin[2];
	char			name[64];
	src_offset_t	offset; // johnfitz

	memcpy (&inframe, pin, sizeof (inframe));

	width = LittleLong (inframe.width);
	height = LittleLong (inframe.height);
	size = width * height;

	pspriteframe = (mspriteframe_t *)Mem_Alloc (sizeof (mspriteframe_t));
	*ppframe = pspriteframe;

	pspriteframe->width = width;
	pspriteframe->height = height;
	origin[0] = LittleLong (inframe.origin[0]);
	origin[1] = LittleLong (inframe.origin[1]);

	pspriteframe->up = origin[1];
	pspriteframe->down = origin[1] - height;
	pspriteframe->left = origin[0];
	pspriteframe->right = width + origin[0];

	pspriteframe->smax = 1;
	pspriteframe->tmax = 1;

	q_snprintf (name, sizeof (name), "%s:frame%i", mod->name, framenum);
	frame_data = (byte *)pin + sizeof (inframe);
	offset = (src_offset_t)frame_data - (src_offset_t)mod_base; // johnfitz
	pspriteframe->gltexture = TexMgr_LoadImage (
		mod, name, width, height, SRC_INDEXED, frame_data, mod->name, offset,
		TEXPREF_PAD | TEXPREF_ALPHA | TEXPREF_NOPICMIP); // johnfitz -- TexMgr

	return frame_data + size;
}

/*
=================
Mod_LoadSpriteGroup
=================
*/
static void *Mod_LoadSpriteGroup (qmodel_t *mod, byte *mod_base, void *pin, mspriteframe_t **ppframe, int framenum, spriteframetype_t type)
{
	dspritegroup_t	  ingroup;
	mspritegroup_t	  *pspritegroup;
	int				   i, numframes;
	byte			  *cursor;
	float			  *poutintervals;
	void			  *ptemp;

	memcpy (&ingroup, pin, sizeof (ingroup));

	numframes = LittleLong (ingroup.numframes);
	if (type == SPR_ANGLED && numframes != 8)
		Sys_Error ("Mod_LoadSpriteGroup: Bad # of frames: %d", numframes);

	pspritegroup = (mspritegroup_t *)Mem_Alloc (sizeof (mspritegroup_t) + (numframes - 1) * sizeof (pspritegroup->frames[0]));

	pspritegroup->numframes = numframes;

	*ppframe = (mspriteframe_t *)pspritegroup;

	cursor = (byte *)pin + sizeof (ingroup);

	poutintervals = (float *)Mem_Alloc (numframes * sizeof (float));

	pspritegroup->intervals = poutintervals;

	for (i = 0; i < numframes; i++)
	{
		dspriteinterval_t ininterval;

		memcpy (&ininterval, cursor, sizeof (ininterval));
		*poutintervals = LittleFloat (ininterval.interval);
		if (*poutintervals <= 0.0)
			Sys_Error ("Mod_LoadSpriteGroup: interval<=0");

		poutintervals++;
		cursor += sizeof (ininterval);
	}

	ptemp = cursor;

	for (i = 0; i < numframes; i++)
	{
		ptemp = Mod_LoadSpriteFrame (mod, mod_base, ptemp, &pspritegroup->frames[i], framenum * 100 + i);
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
	dsprite_t			inheader;
	msprite_t		   *psprite;
	int					numframes;
	int					size;
	byte				*cursor;

	memcpy (&inheader, buffer, sizeof (inheader));
	byte *mod_base = (byte *)buffer; // johnfitz

	version = LittleLong (inheader.version);
	if (version != SPRITE_VERSION)
		Sys_Error (
			"%s has wrong version number "
			"(%i should be %i)",
			mod->name, version, SPRITE_VERSION);

	numframes = LittleLong (inheader.numframes);
	if (numframes < 1)
		Sys_Error ("Mod_LoadSpriteModel: Invalid # of frames: %d", numframes);

	size = sizeof (msprite_t) + (numframes - 1) * sizeof (psprite->frames);

	psprite = (msprite_t *)Mem_Alloc (size);

	mod->extradata[PV_QUAKE1] = (byte *)psprite;

	psprite->type = LittleLong (inheader.type);
	psprite->maxwidth = LittleLong (inheader.width);
	psprite->maxheight = LittleLong (inheader.height);
	mod->synctype = (synctype_t)LittleLong (inheader.synctype);
	psprite->numframes = numframes;

	mod->mins[0] = mod->mins[1] = -psprite->maxwidth / 2;
	mod->maxs[0] = mod->maxs[1] = psprite->maxwidth / 2;
	mod->mins[2] = -psprite->maxheight / 2;
	mod->maxs[2] = psprite->maxheight / 2;

	//
	// load the frames
	//
	mod->numframes = numframes;

	cursor = (byte *)buffer + sizeof (inheader);

	for (i = 0; i < numframes; i++)
	{
		dspriteframetype_t inframetype;
		spriteframetype_t frametype;

		memcpy (&inframetype, cursor, sizeof (inframetype));
		frametype = (spriteframetype_t)LittleLong (inframetype.type);
		cursor += sizeof (inframetype);
		psprite->frames[i].type = frametype;

		if (frametype == SPR_SINGLE)
		{
			cursor = (byte *)Mod_LoadSpriteFrame (mod, mod_base, cursor, &psprite->frames[i].frameptr, i);
		}
		else
		{
			cursor = (byte *)Mod_LoadSpriteGroup (mod, mod_base, cursor, &psprite->frames[i].frameptr, i, frametype);
		}
	}

	mod->type = mod_sprite;
}

/*
=================================================================
MD5 Models, for compat with the rerelease and NOT doom3.
=================================================================
md5mesh:
MD5Version 10
commandline ""
numJoints N
numMeshes N
joints {
	"name" ParentIdx ( Pos_X Y Z ) ( Quat_X Y Z )
}
mesh {
	shader "name"	//file-relative path, with _%02d_%02d postfixed for skin/framegroup support. unlike doom3.
	numverts N
	vert # ( S T ) FirstWeight count
	numtris N
	tri # A B C
	numweights N
	weight # JointIdx Scale ( X Y Z )
}

md5anim:
MD5Version 10
commandline ""
numFrames N
numJoints N
frameRate FPS
numAnimatedComponents N	//joints*6ish
hierachy {
	"name" ParentIdx Flags DataStart
}
bounds {
	( X Y Z ) ( X Y Z )
}
baseframe {
	( pos_X Y Z ) ( quad_X Y Z )
}
frame # {
	RAW ...
}

We'll unpack the animation to separate framegroups (one-pose per, for consistency with most q1 models).
*/

/*
================
MD5_ParseCheck
================
*/
static qboolean MD5_ParseCheck (const char *s, const void **buffer)
{
	if (strcmp (com_token, s))
		return false;
	*buffer = COM_Parse (*buffer);
	return true;
}

/*
================
MD5_ParseUInt
================
*/
static size_t MD5_ParseUInt (const void **buffer)
{
	size_t i = strtoull (com_token, NULL, 0);
	*buffer = COM_Parse (*buffer);
	return i;
}

/*
================
MD5_ParseSInt
================
*/
static long MD5_ParseSInt (const void **buffer)
{
	long i = strtol (com_token, NULL, 0);
	*buffer = COM_Parse (*buffer);
	return i;
}

/*
================
MD5_ParseFloat
================
*/
static double MD5_ParseFloat (const void **buffer)
{
	double i = strtod (com_token, NULL);
	*buffer = COM_Parse (*buffer);
	return i;
}

static size_t MD5_CountAnimatedComponents (unsigned int flags)
{
	size_t count = 0;
	for (unsigned int bit = 1; bit <= 32; bit <<= 1)
		if (flags & bit)
			count++;
	return count;
}

#define MD5ERROR(...)              \
	do                             \
	{                              \
		Con_Warning (__VA_ARGS__); \
		goto error;                \
	} while (0)

#define MD5EXPECT(s)                                                                                     \
	do                                                                                                   \
	{                                                                                                    \
		if (strcmp (com_token, s))                                                                       \
			MD5ERROR ("Mod_LoadMD5MeshModel(%s): expected \"%s\", found \"%s\"\n", fname, s, com_token); \
		buffer = COM_Parse (buffer);                                                                     \
	} while (0)
#define MD5UINT()	MD5_ParseUInt (&buffer)
#define MD5SINT()	MD5_ParseSInt (&buffer)
#define MD5FLOAT()	MD5_ParseFloat (&buffer)
#define MD5CHECK(s) MD5_ParseCheck (s, &buffer)
#define MD5IGNORE() buffer = COM_Parse (buffer)

/*
================
md5vertinfo_s
================
*/
typedef struct md5vertinfo_s
{
	size_t		 firstweight;
	unsigned int count;
	float		 st[2];
} md5vertinfo_t;

/*
================
md5weightinfo_s
================
*/
typedef struct md5weightinfo_s
{
	size_t joint_index;
	vec4_t pos;
} md5weightinfo_t;

/*
================
jointinfo_s
================
*/
typedef struct jointinfo_s
{
	ssize_t		parent; //-1 for a root joint
	ssize_t		poseparent;
	char		name[32];
	jointpose_t inverse;
} jointinfo_t;

typedef struct md5animjoint_s
{
	unsigned int flags, offset;
	ssize_t		 parent;
	ssize_t		 mesh_index;
	vec3_t		 basepos;
	vec4_t		 basequat;
} md5animjoint_t;

static qboolean MD5_ResolveMappedMeshParent (
	jointinfo_t *joints, size_t numjoints, const ssize_t *mesh_to_anim, ssize_t *mapped_mesh_parent, byte *mapped_mesh_parent_state, size_t mesh_index)
{
	ssize_t parent;

	if (mapped_mesh_parent_state[mesh_index] == 2)
		return true;
	if (mapped_mesh_parent_state[mesh_index] == 1)
		return false;

	mapped_mesh_parent_state[mesh_index] = 1;
	parent = joints[mesh_index].parent;
	if (parent < 0)
		mapped_mesh_parent[mesh_index] = -1;
	else if ((size_t)parent >= numjoints)
		return false;
	else if (mesh_to_anim[parent] >= 0)
		mapped_mesh_parent[mesh_index] = parent;
	else
	{
		if (!MD5_ResolveMappedMeshParent (joints, numjoints, mesh_to_anim, mapped_mesh_parent, mapped_mesh_parent_state, (size_t)parent))
			return false;
		mapped_mesh_parent[mesh_index] = mapped_mesh_parent[parent];
	}

	mapped_mesh_parent_state[mesh_index] = 2;
	return true;
}

/*
================
Matrix3x4_RM_Transform4
================
*/
static void Matrix3x4_RM_Transform4 (const float *matrix, const float *vector, float *product)
{
	product[0] = matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2] + matrix[3] * vector[3];
	product[1] = matrix[4] * vector[0] + matrix[5] * vector[1] + matrix[6] * vector[2] + matrix[7] * vector[3];
	product[2] = matrix[8] * vector[0] + matrix[9] * vector[1] + matrix[10] * vector[2] + matrix[11] * vector[3];
}

/*
================
GenMatrixPosQuat4Scale
================
*/
static void GenMatrixPosQuat4Scale (const vec3_t pos, const vec4_t quat, const vec3_t scale, float result[12])
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

	result[0 * 4 + 0] = scale[0] * (1.0f - (yy + zz));
	result[1 * 4 + 0] = scale[0] * (xy + zw);
	result[2 * 4 + 0] = scale[0] * (xz - yw);

	result[0 * 4 + 1] = scale[1] * (xy - zw);
	result[1 * 4 + 1] = scale[1] * (1.0f - (xx + zz));
	result[2 * 4 + 1] = scale[1] * (yz + xw);

	result[0 * 4 + 2] = scale[2] * (xz + yw);
	result[1 * 4 + 2] = scale[2] * (yz - xw);
	result[2 * 4 + 2] = scale[2] * (1.0f - (xx + yy));

	result[0 * 4 + 3] = pos[0];
	result[1 * 4 + 3] = pos[1];
	result[2 * 4 + 3] = pos[2];
}

/*
================
Matrix3x4_Invert_Simple
================
*/
static void Matrix3x4_Invert_Simple (const float *in1, float *out)
{
	// we only support uniform scaling, so assume the first row is enough
	// (note the lack of sqrt here, because we're trying to undo the scaling,
	// this means multiplying by the inverse scale twice - squaring it, which
	// makes the sqrt a waste of time)
	const double scale = 1.0 / ((double)in1[0] * (double)in1[0] + (double)in1[1] * (double)in1[1] + (double)in1[2] * (double)in1[2]);

	// invert the rotation by transposing and multiplying by the squared
	// reciprocal of the input matrix scale as described above
	double temp[12];
	temp[0] = in1[0] * scale;
	temp[1] = in1[4] * scale;
	temp[2] = in1[8] * scale;
	temp[4] = in1[1] * scale;
	temp[5] = in1[5] * scale;
	temp[6] = in1[9] * scale;
	temp[8] = in1[2] * scale;
	temp[9] = in1[6] * scale;
	temp[10] = in1[10] * scale;

	// invert the translate
	temp[3] = -(in1[3] * temp[0] + in1[7] * temp[1] + in1[11] * temp[2]);
	temp[7] = -(in1[3] * temp[4] + in1[7] * temp[5] + in1[11] * temp[6]);
	temp[11] = -(in1[3] * temp[8] + in1[7] * temp[9] + in1[11] * temp[10]);

	for (int i = 0; i < 12; ++i)
		out[i] = (float)temp[i];
}

/*
================
MD5_BakeInfluences
================
*/
static qboolean MD5_BakeInfluences (
	const char *fname, jointpose_t *outposes, byte *vertexes, poseverttype_t poseverttype, struct md5vertinfo_s *vinfo, struct md5weightinfo_s *weight,
	size_t numverts, size_t numweights)
{
	struct md5weightinfo_s *w;
	vec3_t					pos;
	float					scale;
	unsigned int			maxinfluences = 0;
	float					scaleimprecision = 1;
	const int				stored_influences = (poseverttype == PV_MD5_8) ? NUM_JOINT_INFLUENCES_8_WEIGHT : NUM_JOINT_INFLUENCES_4_WEIGHT;
	const size_t			vertex_size = (poseverttype == PV_MD5_8) ? sizeof (md5vert8_t) : sizeof (md5vert_t);

	for (size_t v = 0; v < numverts; v++, vinfo++)
	{
		byte	   *vertex = vertexes + v * vertex_size;
		md5vert_t  *vert = (md5vert_t *)vertex;
		md5vert8_t *vert8 = (md5vert8_t *)vertex;
		float		weights[NUM_JOINT_INFLUENCES_8_WEIGHT] = {0.0f};
		size_t		joint_indices[NUM_JOINT_INFLUENCES_8_WEIGHT] = {0};
		vec4_t		joint_positions[NUM_JOINT_INFLUENCES_8_WEIGHT] = {{0.0f}};

		memset (vertex, 0, vertex_size);
		vert->st[0] = vinfo->st[0];
		vert->st[1] = vinfo->st[1];

		if (vinfo->firstweight > numweights || (size_t)vinfo->count > numweights - vinfo->firstweight)
		{
			Con_Warning ("%s: weight index out of bounds\n", fname);
			return false;
		}
		if (maxinfluences < vinfo->count)
			maxinfluences = vinfo->count;
		w = weight + vinfo->firstweight;
		for (unsigned int i = 0; i < vinfo->count; i++, w++)
		{
			Matrix3x4_RM_Transform4 (outposes[w->joint_index].mat, w->pos, pos);
			VectorAdd (vert->xyz, pos, vert->xyz);

			if (i < NUM_JOINT_INFLUENCES_4_WEIGHT)
			{
				weights[i] = w->pos[3];
				joint_indices[i] = w->joint_index;
				Vector4Copy (w->pos, joint_positions[i]);
			}
			else if (i < NUM_JOINT_INFLUENCES_8_WEIGHT)
			{
				weights[i] = w->pos[3];
				joint_indices[i] = w->joint_index;
				Vector4Copy (w->pos, joint_positions[i]);
			}
			else
			{
				// obnoxious code to find the lowest of the current possible joint indexes.
				float  lowval = weights[0];
				size_t lowidx = 0;
				for (size_t k = 1; k < NUM_JOINT_INFLUENCES_8_WEIGHT; ++k)
				{
					if (weights[k] < lowval)
					{
						lowval = weights[k];
						lowidx = k;
					}
				}
				if (weights[lowidx] < w->pos[3])
				{ // found a lower/unset weight, replace it.
					weights[lowidx] = w->pos[3];
					joint_indices[lowidx] = w->joint_index;
					Vector4Copy (w->pos, joint_positions[lowidx]);
				}
			}
		}

		// normalize in case we dropped some weights.
		scale = 0;
		for (int k = 0; k < stored_influences; ++k)
			scale += weights[k];
		if (scale > 0)
		{
			if (scaleimprecision < scale)
				scaleimprecision = scale;
			scale = 1 / scale;
			for (int k = 0; k < stored_influences; ++k)
			{
				weights[k] *= scale;
				for (int c = 0; c < 4; ++c)
					joint_positions[k][c] *= scale;
			}
		}
		else // something bad...
		{
			weights[0] = 1;
			for (int k = 1; k < stored_influences; ++k)
				weights[k] = 0;
			joint_positions[0][3] = 1;
		}

		for (int j = 0; j < stored_influences; ++j)
		{
			if (poseverttype == PV_MD5_8)
			{
				vert8->joint_weights[j] = (byte)(CLAMP (0.0f, weights[j], 1.0f) * 255.0f);
				vert8->joint_indices[j] = joint_indices[j];
				vert8->joint_position_x[j] = joint_positions[j][0];
				vert8->joint_position_y[j] = joint_positions[j][1];
				vert8->joint_position_z[j] = joint_positions[j][2];
			}
			else
			{
				vert->joint_weights[j] = (byte)(CLAMP (0.0f, weights[j], 1.0f) * 255.0f);
				vert->joint_indices[j] = joint_indices[j];
				vert->joint_position_x[j] = joint_positions[j][0];
				vert->joint_position_y[j] = joint_positions[j][1];
				vert->joint_position_z[j] = joint_positions[j][2];
			}
		}
	}
	if (maxinfluences > NUM_JOINT_INFLUENCES_8_WEIGHT)
		Con_DWarning ("%s uses up to %u influences per vertex (weakest: %g)\n", fname, maxinfluences, scaleimprecision);
	return true;
}

/*
================
MD5_ComputeNormals
================
*/
static void MD5_ComputeNormals (byte *vertexes, size_t vertex_size, size_t numverts, unsigned short *indexes, size_t numindexes)
{
	hash_map_t *pos_to_normal_map = HashMap_Create (vec3_t, vec3_t, &HashVec3, NULL);
	HashMap_Reserve (pos_to_normal_map, numverts);

	for (size_t v = 0; v < numverts; v++)
	{
		md5vert_t *vert = (md5vert_t *)(vertexes + v * vertex_size);
		vert->norm[0] = vert->norm[1] = vert->norm[2] = 0;
	}
	for (size_t t = 0; t < numindexes; t += 3)
	{
		md5vert_t *verts[3] = {
			(md5vert_t *)(vertexes + indexes[t + 0] * vertex_size), (md5vert_t *)(vertexes + indexes[t + 1] * vertex_size),
			(md5vert_t *)(vertexes + indexes[t + 2] * vertex_size)};

		vec3_t d1, d2;
		VectorSubtract (verts[2]->xyz, verts[0]->xyz, d1);
		VectorSubtract (verts[1]->xyz, verts[0]->xyz, d2);
		VectorNormalize (d1);
		VectorNormalize (d2);

		vec3_t norm;
		CrossProduct (d1, d2, norm);
		VectorNormalize (norm);

		const float angle = acos (DotProduct (d1, d2));
		VectorScale (norm, angle, norm);

		vec3_t *found_normal;
		for (int i = 0; i < 3; ++i)
		{
			if ((found_normal = HashMap_Lookup (vec3_t, pos_to_normal_map, &verts[i]->xyz)))
				VectorAdd (norm, *found_normal, *found_normal);
			else
				HashMap_Insert (pos_to_normal_map, &verts[i]->xyz, &norm);
		}
	}

	const uint32_t map_size = HashMap_Size (pos_to_normal_map);
	for (uint32_t i = 0; i < map_size; ++i)
	{
		vec3_t *norm = HashMap_GetValue (vec3_t, pos_to_normal_map, i);
		VectorNormalize (*norm);
	}

	for (size_t v = 0; v < numverts; v++)
	{
		md5vert_t *vert = (md5vert_t *)(vertexes + v * vertex_size);
		vec3_t	  *norm = HashMap_Lookup (vec3_t, pos_to_normal_map, &vert->xyz);
		if (norm)
			VectorCopy (*norm, vert->norm);
	}

	HashMap_Destroy (pos_to_normal_map);
}

/*
================
MD5_HackyModelFlags
================
*/
static unsigned int MD5_HackyModelFlags (const char *name)
{
	unsigned int ret = 0;
	char		 oldmodel[MAX_QPATH];
	mdl_t		*f;
	COM_StripExtension (name, oldmodel, sizeof (oldmodel));
	COM_AddExtension (oldmodel, ".mdl", sizeof (oldmodel));

	f = (mdl_t *)COM_LoadFile (oldmodel, NULL);
	if (f)
	{
		if (com_filesize >= sizeof (*f) && LittleLong (f->ident) == IDPOLYHEADER && LittleLong (f->version) == ALIAS_VERSION)
			ret = f->flags;
		Mem_Free (f);
	}
	return ret;
}

/*
================
md5animctx_s
================
*/
typedef struct md5animctx_s
{
	void		*animfile;
	const void	*buffer;
	char		 fname[MAX_QPATH];
	qfilesize_t	filesize;
	size_t		 numposes;
	size_t		 numjoints;
	jointpose_t *posedata;
} md5animctx_t;

static const char *MD5Anim_ParseToken (const char *buffer, char *token)
{
	return COM_ParseExBuffer (buffer, CPE_NOTRUNC, token, COM_PARSE_MAX_TOKEN_SIZE, NULL);
}

static qboolean MD5Anim_ParseCheck (const char *s, const char **buffer, char *token)
{
	if (strcmp (token, s))
		return false;
	*buffer = MD5Anim_ParseToken (*buffer, token);
	return true;
}

static size_t MD5Anim_ParseUInt (const char **buffer, char *token)
{
	size_t i = strtoull (token, NULL, 0);
	*buffer = MD5Anim_ParseToken (*buffer, token);
	return i;
}

static long MD5Anim_ParseSInt (const char **buffer, char *token)
{
	long i = strtol (token, NULL, 0);
	*buffer = MD5Anim_ParseToken (*buffer, token);
	return i;
}

static double MD5Anim_ParseFloat (const char **buffer, char *token)
{
	double i = strtod (token, NULL);
	*buffer = MD5Anim_ParseToken (*buffer, token);
	return i;
}

#define MD5ANIMEXPECT(s)                                                                                     \
	do                                                                                                         \
	{                                                                                                          \
		if (strcmp (token, s))                                                                                   \
			MD5ERROR ("Mod_LoadMD5MeshModel(%s): expected \"%s\", found \"%s\"\n", fname, s, token);        \
		buffer = MD5Anim_ParseToken (buffer, token);                                                             \
	} while (0)
#define MD5ANIMUINT()  MD5Anim_ParseUInt (&buffer, token)
#define MD5ANIMSINT()  MD5Anim_ParseSInt (&buffer, token)
#define MD5ANIMFLOAT() MD5Anim_ParseFloat (&buffer, token)
#define MD5ANIMCHECK(s) MD5Anim_ParseCheck (s, &buffer, token)
#define MD5ANIMIGNORE() buffer = MD5Anim_ParseToken (buffer, token)

/*
================
MD5Anim_Begin
This is split into two because aliashdr_t has silly trailing framegroup info.
================
*/
static qboolean MD5Anim_Begin (md5animctx_t *ctx, const char *fname)
{
	// Load an md5anim into it, if we can.
	COM_StripExtension (fname, ctx->fname, sizeof (ctx->fname));
	COM_AddExtension (ctx->fname, ".md5anim", sizeof (ctx->fname));
	fname = ctx->fname;
	ctx->animfile = COM_LoadFile (fname, NULL);
	ctx->filesize = ctx->animfile ? com_filesize : -1;
	ctx->numposes = 0;

	if (ctx->animfile)
	{
		const void *buffer = COM_Parse (ctx->animfile);
		MD5EXPECT ("MD5Version");
		MD5EXPECT ("10");
		if (MD5CHECK ("commandline"))
			buffer = COM_Parse (buffer);
		MD5EXPECT ("numFrames");
		ctx->numposes = MD5UINT ();
		MD5EXPECT ("numJoints");
		ctx->numjoints = MD5UINT ();
		MD5EXPECT ("frameRate"); // irrelevant here

		if (ctx->numposes <= 0)
			MD5ERROR ("%s has no poses\n", fname);

		ctx->buffer = buffer;
	}
	return true;

error:
	SAFE_FREE (ctx->animfile);
	ctx->buffer = NULL;
	ctx->numposes = 0;
	ctx->numjoints = 0;
	return false;
}

/*
================
MD5Anim_Load
================
*/
static qboolean MD5Anim_Load (md5animctx_t *ctx, jointinfo_t *joints, jointpose_t *joint_poses, size_t numjoints)
{
	const char	*fname = ctx->fname;
	char		token[COM_PARSE_MAX_TOKEN_SIZE];
	size_t		 rawcount;
	float		*r;
	jointpose_t *outposes = NULL;
	const char	*buffer = MD5Anim_ParseToken (ctx->buffer, token);
	size_t		 animjoints = ctx->numjoints;
	size_t		 j;
	TEMP_ALLOC_DECL (md5animjoint_t, ab);
	TEMP_ALLOC_DECL (ssize_t, mesh_to_anim);
	TEMP_ALLOC_DECL (ssize_t, mapped_mesh_parent);
	TEMP_ALLOC_DECL (byte, mapped_mesh_parent_state);
	TEMP_ALLOC_DECL (jointpose_t, bindposes);
	TEMP_ALLOC_DECL (float, raw);

	if (!buffer)
	{
		SAFE_FREE (ctx->animfile);
		return true;
	}

	MD5ANIMEXPECT ("numAnimatedComponents");
	rawcount = MD5ANIMUINT ();

	TEMP_ALLOC_ASSIGN_ZEROED (raw, rawcount + 6);
	TEMP_ALLOC_ASSIGN_ZEROED_COND (ab, animjoints, animjoints > 0);
	TEMP_ALLOC_ASSIGN (mesh_to_anim, numjoints);
	TEMP_ALLOC_ASSIGN (mapped_mesh_parent, numjoints);
	TEMP_ALLOC_ASSIGN_ZEROED (mapped_mesh_parent_state, numjoints);
	TEMP_ALLOC_ASSIGN (bindposes, numjoints);

	for (j = 0; j < numjoints; j++)
	{
		mesh_to_anim[j] = -1;
		joints[j].poseparent = joints[j].parent;
	}

	for (j = 0; j < numjoints; j++)
	{
		if (joints[j].parent < 0)
			memcpy (bindposes[j].mat, joint_poses[j].mat, sizeof (bindposes[j].mat));
		else
			R_ConcatTransforms ((void *)joints[joints[j].parent].inverse.mat, (void *)joint_poses[j].mat, (void *)bindposes[j].mat);
	}

	ctx->posedata = outposes = Mem_Alloc (sizeof (*outposes) * numjoints * ctx->numposes);
	if (numjoints)
		for (j = 0; j < ctx->numposes; j++)
			memcpy (outposes + j * numjoints, bindposes, sizeof (*bindposes) * numjoints);

	MD5ANIMEXPECT ("hierarchy");
	MD5ANIMEXPECT ("{");
	for (j = 0; j < animjoints; j++)
	{
		char		anim_name[sizeof (joints[0].name)];
		ssize_t		mesh_index = -1;
		const char *name = token;

		q_strlcpy (anim_name, name, sizeof (anim_name));
		buffer = MD5Anim_ParseToken (buffer, token);
		ab[j].parent = MD5ANIMSINT ();
		if (ab[j].parent < -1 || ab[j].parent >= (ssize_t)j)
			MD5ERROR ("%s: joint has bad parent order\n", fname);
		// new info
		ab[j].flags = MD5ANIMUINT ();
		if (ab[j].flags & ~63)
			MD5ERROR ("%s: joint has unsupported flags\n", fname);
		ab[j].offset = MD5ANIMUINT ();
		if (ab[j].offset + MD5_CountAnimatedComponents (ab[j].flags) > rawcount)
			MD5ERROR ("%s: joint has bad offset\n", fname);
		ab[j].mesh_index = -1;

		for (size_t k = 0; k < numjoints; k++)
		{
			if (!strcmp (joints[k].name, anim_name))
			{
				mesh_index = (ssize_t)k;
				break;
			}
		}

		if (mesh_index < 0)
			continue;
		if (mesh_to_anim[mesh_index] >= 0)
			MD5ERROR ("%s: duplicate joint \"%s\"\n", fname, anim_name);

		ab[j].mesh_index = mesh_index;
		mesh_to_anim[mesh_index] = (ssize_t)j;
	}
	MD5ANIMEXPECT ("}");

	for (j = 0; j < numjoints; j++)
	{
		if (!MD5_ResolveMappedMeshParent (joints, numjoints, mesh_to_anim, mapped_mesh_parent, mapped_mesh_parent_state, j))
			MD5ERROR ("%s: joint \"%s\" has bad parent chain\n", fname, joints[j].name);
	}

	for (j = 0; j < animjoints; j++)
	{
		ssize_t mesh_index = ab[j].mesh_index;
		ssize_t mesh_parent, anim_parent;

		if (mesh_index < 0)
			continue;

		mesh_parent = mapped_mesh_parent[mesh_index];
		if (ab[j].parent >= 0)
		{
			anim_parent = ab[ab[j].parent].mesh_index;
			if (anim_parent < 0)
				MD5ERROR ("%s: joint \"%s\" has unmapped parent\n", fname, joints[mesh_index].name);
		}
		else
			anim_parent = -1;
		if (mesh_parent != anim_parent)
			MD5ERROR ("%s: joint \"%s\" has wrong parent\n", fname, joints[mesh_index].name);
		joints[mesh_index].poseparent = anim_parent;
	}

	MD5ANIMEXPECT ("bounds");
	MD5ANIMEXPECT ("{");
	while (MD5ANIMCHECK ("("))
	{
		MD5ANIMIGNORE ();
		MD5ANIMIGNORE ();
		MD5ANIMIGNORE ();
		MD5ANIMEXPECT (")");

		MD5ANIMEXPECT ("(");
		MD5ANIMIGNORE ();
		MD5ANIMIGNORE ();
		MD5ANIMIGNORE ();
		MD5ANIMEXPECT (")");
	}
	MD5ANIMEXPECT ("}");

	MD5ANIMEXPECT ("baseframe");
	MD5ANIMEXPECT ("{");
	for (j = 0; j < animjoints; j++)
	{
		MD5ANIMEXPECT ("(");
		ab[j].basepos[0] = MD5ANIMFLOAT ();
		ab[j].basepos[1] = MD5ANIMFLOAT ();
		ab[j].basepos[2] = MD5ANIMFLOAT ();
		MD5ANIMEXPECT (")");

		MD5ANIMEXPECT ("(");
		ab[j].basequat[0] = MD5ANIMFLOAT ();
		ab[j].basequat[1] = MD5ANIMFLOAT ();
		ab[j].basequat[2] = MD5ANIMFLOAT ();
		ab[j].basequat[3] = 1 - DotProduct (ab[j].basequat, ab[j].basequat);
		if (ab[j].basequat[3] < 0)
			ab[j].basequat[3] = 0;
		ab[j].basequat[3] = -sqrtf (ab[j].basequat[3]);
		MD5ANIMEXPECT (")");
	}
	MD5ANIMEXPECT ("}");

	while (MD5ANIMCHECK ("frame"))
	{
		size_t idx = MD5ANIMUINT ();
		if (idx >= ctx->numposes)
			MD5ERROR ("%s: invalid pose index\n", fname);
		MD5ANIMEXPECT ("{");
		for (j = 0; j < rawcount; j++)
			raw[j] = MD5ANIMFLOAT ();
		MD5ANIMEXPECT ("}");

		// okay, we have our raw info, unpack the actual joint info.
		for (j = 0; j < animjoints; j++)
		{
			vec3_t		  pos;
			static vec3_t scale = {1, 1, 1};
			vec4_t		  quat;
			ssize_t		  mesh_index = ab[j].mesh_index;

			if (mesh_index < 0)
				continue;
			VectorCopy (ab[j].basepos, pos);
			Vector4Copy (ab[j].basequat, quat);
			r = raw + ab[j].offset;
			if (ab[j].flags & 1)
				pos[0] = *r++;
			if (ab[j].flags & 2)
				pos[1] = *r++;
			if (ab[j].flags & 4)
				pos[2] = *r++;

			if (ab[j].flags & 8)
				quat[0] = *r++;
			if (ab[j].flags & 16)
				quat[1] = *r++;
			if (ab[j].flags & 32)
				quat[2] = *r++;

			quat[3] = 1 - DotProduct (quat, quat);
			if (quat[3] < 0)
				quat[3] = 0; // we have no imagination.
			quat[3] = -sqrtf (quat[3]);

			GenMatrixPosQuat4Scale (pos, quat, scale, outposes[idx * numjoints + mesh_index].mat);
		}
	}
	TEMP_FREE (raw);
	TEMP_FREE (ab);
	TEMP_FREE (mesh_to_anim);
	TEMP_FREE (mapped_mesh_parent);
	TEMP_FREE (mapped_mesh_parent_state);
	TEMP_FREE (bindposes);
	SAFE_FREE (ctx->animfile);
	ctx->numjoints = numjoints;
	return true;

error:
	TEMP_FREE (raw);
	TEMP_FREE (ab);
	TEMP_FREE (mesh_to_anim);
	TEMP_FREE (mapped_mesh_parent);
	TEMP_FREE (mapped_mesh_parent_state);
	TEMP_FREE (bindposes);
	SAFE_FREE (ctx->posedata);
	SAFE_FREE (ctx->animfile);
	return false;
}

#undef MD5ANIMIGNORE
#undef MD5ANIMCHECK
#undef MD5ANIMFLOAT
#undef MD5ANIMSINT
#undef MD5ANIMUINT
#undef MD5ANIMEXPECT

/*
===============
Mod_LoadMDXSkinTask
===============
*/
typedef struct load_skin_MDX_task_args_s
{
	qmodel_t   *mod;
	aliashdr_t *surf;
	skin_def_t *skins; // skins * framegroups table
} load_skin_MDX_task_args_t;

static void Mod_LoadMDXSkinTask (int i, load_skin_MDX_task_args_t *args)
{
	qmodel_t   *mod = args->mod;
	aliashdr_t *surf = args->surf;
	skin_def_t *skins = args->skins;

	assert (surf->poseverttype != PV_QUAKE1);

	const int skin_index = i / MAX_FRAMEGROUPS;
	const int f = i - skin_index * MAX_FRAMEGROUPS;

	const char *basic_texname = skins[skin_index].framegroups[f].c_str;

	if (!basic_texname)
		return;

#define TRY_LOAD_FULLBRIGHTS(tex_name)                                                         \
	do                                                                                         \
	{                                                                                          \
		if (!surf->fbtextures[skin_index][f])                                                  \
		{                                                                                      \
			surf->fbtextures[skin_index][f] = Mod_LoadFullbrightTexture (mod, surf, tex_name); \
		}                                                                                      \
	} while (0);

	unsigned int fwidth, fheight;

	void *data;
	char  texname[MAX_QPATH] = {0};
	q_snprintf (texname, sizeof (texname), "%s", basic_texname);

	enum srcformat fmt = SRC_RGBA;

	data = Image_LoadImage (texname, (int *)&fwidth, (int *)&fheight, &fmt, mod->path_id);

	if (!data)
	{
		q_snprintf (texname, sizeof (texname), "progs/%s", basic_texname);
		data = Image_LoadImage (texname, (int *)&fwidth, (int *)&fheight, &fmt, mod->path_id);
	}
	if (!data)
	{
		q_snprintf (texname, sizeof (texname), "textures/%s", basic_texname);
		data = Image_LoadImage (texname, (int *)&fwidth, (int *)&fheight, &fmt, mod->path_id);
	}

	if (data) // load external image
	{
		surf->gltextures[skin_index][f] =
			TexMgr_LoadImage (mod, texname, fwidth, fheight, fmt, data, texname, 0, TEXPREF_ALPHA | TEXPREF_NOBRIGHT | TEXPREF_MIPMAP);

		// no fullbrights by default.
		assert (surf->fbtextures[skin_index][f] == NULL);

		// initialize skinsizes:
		if (i == 0)
		{
			surf->skinwidth = surf->gltextures[0][0] ? surf->gltextures[0][0]->width : 1;
			surf->skinheight = surf->gltextures[0][0] ? surf->gltextures[0][0]->height : 1;
		}

		if (fmt == SRC_INDEXED)
		{
			if (f == 0)
			{
				size_t size = fwidth * fheight;
				byte  *texels = (byte *)Mem_Alloc (size);
				surf->texels[surf->numskins] = texels;
				memcpy (texels, data, size);
			}
			// 8bit base texture. use it for fullbrights.
			for (size_t j = 0; j < fwidth * fheight; j++)
			{
				if (((byte *)data)[j] > 223)
				{
					surf->fbtextures[skin_index][f] = TexMgr_LoadImage (
						mod, va ("%s_luma", basic_texname), fwidth, fheight, SRC_INDEXED, data, texname, 0,
						TEXPREF_ALPHA | TEXPREF_MIPMAP | TEXPREF_FULLBRIGHT);
					break;
				}
			}
		}
		else
		{
			// we found a 32bit base texture, try to fetch the fullbrights counterparts
			// Same as skins, try first the same location as the model, then 'progs/', then 'textures/' if not found.
			assert (surf->fbtextures[skin_index][f] == NULL);

			TRY_LOAD_FULLBRIGHTS (va ("%s_glow", basic_texname));
			TRY_LOAD_FULLBRIGHTS (va ("%s_luma", basic_texname));
			TRY_LOAD_FULLBRIGHTS (va ("progs/%s_glow", basic_texname));
			TRY_LOAD_FULLBRIGHTS (va ("progs/%s_luma", basic_texname));
			TRY_LOAD_FULLBRIGHTS (va ("textures/%s_glow", basic_texname));
			TRY_LOAD_FULLBRIGHTS (va ("textures/%s_luma", basic_texname));
		}

		Mem_Free (data);
	}

#undef TRY_LOAD_FULLBRIGHTS
}
/*
=====================
Mod_LoadMDXSkinsByIndex : generic method to load skins for MD3/MD5 exploring standard search paths,
parametrized by skin and framegroup index and skin_texture_pattern_fn.
returns the number of successfully loaded (i.e. up to numskins) skins for surf.
=====================
*/
typedef void (*skin_base_name_fn) (
	qmodel_t *mod, aliashdr_t *surf, all_surfaces_def_t *surf_defs, int surf_index, size_t numsurfaces, int skin_index, int framegroup_index,
	const char *basename, char output_name[MAX_QPATH]);

#define SKIN_PATTERN_FUNC_DEF(signature)                                                                                                          \
	static void signature (                                                                                                                       \
		qmodel_t *mod, aliashdr_t *surf, all_surfaces_def_t *surf_defs, int surf_index, size_t numsurfaces, int skin_index, int framegroup_index, \
		const char *basename, char output_name[MAX_QPATH])

static size_t Mod_LoadMDXSkinsByIndex (
	qmodel_t *mod, aliashdr_t *surf, all_surfaces_def_t *surf_defs, int surf_index, size_t numsurfaces, size_t numskins, const char *basename,
	skin_base_name_fn skin_pattern_func)
{
	// for each skin:
	size_t nb_loaded_skins = 0;

	TEMP_ALLOC_ZEROED (skin_def_t, skin_table, numskins);

	int effective_num_skins = 0;

	// Populate basic texture names:
	for (int skin_index = 0; skin_index < numskins; skin_index++)
	{
		for (int f = 0; f < countof (surf->gltextures[0]); f++)
		{
			// Generate basic skin name:
			skin_pattern_func (mod, surf, surf_defs, surf_index, numsurfaces, skin_index, f, basename, skin_table[skin_index].framegroups[f].c_str);

			if (strlen (skin_table[skin_index].framegroups[f].c_str) == 0)
			{
				// No more framegroup
				break;
			}
			skin_table[skin_index].numframegroups++;
		}
		if (skin_table[skin_index].numframegroups)
		{
			effective_num_skins++;
		}
		else
			break;
	}

	// load textures: (concurrently)
	load_skin_MDX_task_args_t args = {.mod = mod, .surf = surf, .skins = skin_table};

	if (!Tasks_IsWorker () && effective_num_skins > 1)
	{
		task_handle_t task =
			Task_AllocateAssignIndexedFuncAndSubmit ((task_indexed_func_t)Mod_LoadMDXSkinTask, effective_num_skins * MAX_FRAMEGROUPS, &args, sizeof (args));
		Task_Join (task, TASK_TIMEOUT_INFINITE);
	}
	else
	{
		for (int i = 0; i < effective_num_skins * MAX_FRAMEGROUPS; i++)
		{
			Mod_LoadMDXSkinTask (i, &args);
		}
	}

	// normalize skin definitions:
	// fills out animation array based on existing data (from Ironwail Mod_LoadMD3_PopulateAnimation)
	for (int skin_index = 0; skin_index < effective_num_skins; skin_index++)
	{
		int frame_count = 0;
		for (int framegroup_index = 0; framegroup_index < skin_table[skin_index].numframegroups; framegroup_index++)
		{
			if (surf->gltextures[skin_index][framegroup_index])
				frame_count++;
		}

		if (frame_count)
			nb_loaded_skins++;

		switch (frame_count)
		{
		case 1:
			surf->gltextures[skin_index][1] = surf->gltextures[skin_index][0];
			surf->fbtextures[skin_index][1] = surf->fbtextures[skin_index][0];
			surf->gltextures[skin_index][2] = surf->gltextures[skin_index][0];
			surf->fbtextures[skin_index][2] = surf->fbtextures[skin_index][0];
			surf->gltextures[skin_index][3] = surf->gltextures[skin_index][0];
			surf->fbtextures[skin_index][3] = surf->fbtextures[skin_index][0];
			break;
		case 2:
			surf->gltextures[skin_index][2] = surf->gltextures[skin_index][0];
			surf->fbtextures[skin_index][2] = surf->fbtextures[skin_index][0];
			surf->gltextures[skin_index][3] = surf->gltextures[skin_index][1];
			surf->fbtextures[skin_index][3] = surf->fbtextures[skin_index][1];
			break;
		case 3:
			surf->gltextures[skin_index][3] = surf->gltextures[skin_index][0];
			surf->fbtextures[skin_index][3] = surf->fbtextures[skin_index][0];
			break;
		default: // either full or impossible situation, either way nothing to do really
			break;
		}
	}

	TEMP_FREE (skin_table);

	return nb_loaded_skins;
}

/*
=====================
Mod_LoadMD5MeshModel
=====================
*/
SKIN_PATTERN_FUNC_DEF (MD5_Skin_Name)
{
	q_snprintf (output_name, MAX_QPATH, "%s_%02u_%02u", basename, skin_index, framegroup_index);
}

static qboolean Mod_LoadMD5MeshModelData (qmodel_t *mod, const void *buffer,
	size_t numjoints, size_t nummeshes, qboolean verified_rerelease_mesh)
{
	const char *fname = mod->name;

	aliashdr_t *outhdr = NULL, *surf;
	md5_skeleton_data_t *retained_skeleton = NULL;
	size_t		hdrsize = 0;
	size_t		retained_joints_offset, retained_joint_bytes, retained_matrix_count;
	size_t		retained_pose_bytes, retained_allocation_size;

	md5animctx_t anim = {NULL};
	float		 dist, radius = 0, yawradius = 0;
	TEMP_ALLOC_DECL (md5vertinfo_t, vinfo);
	TEMP_ALLOC_DECL (byte, poutvertexes);
	TEMP_ALLOC_DECL (unsigned short, poutindexes);
	TEMP_ALLOC_DECL (unsigned short, skeleton_indexes);
	TEMP_ALLOC_DECL (md5weightinfo_t, weight);
	TEMP_ALLOC_DECL (jointinfo_t, joint_infos);
	TEMP_ALLOC_DECL (jointpose_t, joint_poses);
	TEMP_ALLOC_DECL (jointpose_t, skinning_joints);
	TEMP_ALLOC_DECL (jointpose_t, concat_joints);

	if (!MD5Anim_Begin (&anim, fname))
		return false;
	verified_rerelease_mesh = verified_rerelease_mesh &&
		Mod_IsVerifiedRereleaseRangerAsset (anim.fname,
			"progs/player.md5anim", anim.animfile, anim.filesize,
			331510, 0x0561e50aU);
	buffer = COM_Parse (buffer);
	if (numjoints > (size_t)INT_MAX / 2 || nummeshes > INT_MAX ||
		anim.numposes > INT_MAX || anim.numjoints > INT_MAX ||
		numjoints > SIZE_MAX / sizeof (jointinfo_t) || numjoints > SIZE_MAX / sizeof (jointpose_t))
		MD5ERROR ("%s: MD5 skeleton dimensions are too large\n", fname);

	if (anim.numposes > (SIZE_MAX - (sizeof (*outhdr) - sizeof (outhdr->frames))) / sizeof (outhdr->frames))
		MD5ERROR ("%s: MD5 frame data is too large\n", fname);
	hdrsize = sizeof (*outhdr) - sizeof (outhdr->frames) + sizeof (outhdr->frames) * anim.numposes;
	if (nummeshes > SIZE_MAX / hdrsize)
		MD5ERROR ("%s: MD5 mesh data is too large\n", fname);

	if (anim.numposes && numjoints > SIZE_MAX / anim.numposes)
		MD5ERROR ("%s: MD5 pose matrix count overflows\n", fname);
	retained_matrix_count = numjoints * anim.numposes;
	if (retained_matrix_count > SIZE_MAX / sizeof (jointpose_t))
		MD5ERROR ("%s: MD5 pose matrices are too large\n", fname);
	retained_pose_bytes = retained_matrix_count * sizeof (jointpose_t);
	retained_joints_offset = offsetof (md5_skeleton_data_t, joints);
	if (numjoints > (SIZE_MAX - retained_joints_offset) / sizeof (md5_skeleton_joint_t))
		MD5ERROR ("%s: MD5 retained skeleton is too large\n", fname);
	retained_joint_bytes = numjoints * sizeof (md5_skeleton_joint_t);
	if (retained_joint_bytes > SIZE_MAX - retained_joints_offset ||
		retained_pose_bytes > SIZE_MAX - retained_joints_offset - retained_joint_bytes)
		MD5ERROR ("%s: MD5 retained skeleton is too large\n", fname);
	retained_allocation_size = retained_joints_offset + retained_joint_bytes + retained_pose_bytes;

	// alloc all aliashdr_t and their chained nextsurface, a.k.a nummeshes, in one array
	// all aliashdr_t are zero-initialized by Mem_Alloc
	outhdr = (aliashdr_t *)Mem_Alloc (hdrsize * nummeshes);

	TEMP_ALLOC_ASSIGN_ZEROED (joint_infos, numjoints);
	TEMP_ALLOC_ASSIGN_ZEROED (joint_poses, numjoints);
	TEMP_ALLOC_ASSIGN_ZEROED (skinning_joints, retained_matrix_count);
	TEMP_ALLOC_ASSIGN_ZEROED (concat_joints, numjoints);

	for (size_t j = 0; j < 3; j++)
	{
		mod->mins[j] = mod->ymins[j] = mod->rmins[j] = FLT_MAX;
		mod->maxs[j] = mod->ymaxs[j] = mod->rmaxs[j] = -FLT_MAX;
	}

	// 1. Load joints
	MD5EXPECT ("{");
	for (size_t j = 0; j < numjoints; j++)
	{
		vec3_t		  pos;
		static vec3_t scale = {1, 1, 1};
		vec4_t		  quat;
		q_strlcpy (joint_infos[j].name, com_token, sizeof (joint_infos[j].name));
		buffer = COM_Parse (buffer);
		joint_infos[j].parent = MD5SINT ();
		if (joint_infos[j].parent < -1 || joint_infos[j].parent >= (ssize_t)numjoints)
			MD5ERROR ("%s: joint index out of bounds\n", fname);
		joint_infos[j].poseparent = joint_infos[j].parent;
		MD5EXPECT ("(");
		pos[0] = MD5FLOAT ();
		pos[1] = MD5FLOAT ();
		pos[2] = MD5FLOAT ();
		MD5EXPECT (")");
		MD5EXPECT ("(");
		quat[0] = MD5FLOAT ();
		quat[1] = MD5FLOAT ();
		quat[2] = MD5FLOAT ();
		quat[3] = 1 - DotProduct (quat, quat);
		if (quat[3] < 0)
			quat[3] = 0; // we have no imagination.
		quat[3] = -sqrtf (quat[3]);
		MD5EXPECT (")");

		GenMatrixPosQuat4Scale (pos, quat, scale, joint_poses[j].mat);
		Matrix3x4_Invert_Simple (joint_poses[j].mat, joint_infos[j].inverse.mat); // absolute, so we can just invert now.
	}

	if (strcmp (com_token, "}"))
		MD5ERROR ("Mod_LoadMD5MeshModel(%s): expected \"%s\", found \"%s\"\n", fname, "}", com_token);
	if (!MD5Anim_Load (&anim, joint_infos, joint_poses, numjoints))
		goto error;
	buffer = COM_Parse (buffer);

	int num_skeleton_indexes = 0;
	if (numjoints > (size_t)UINT16_MAX + 1)
		MD5ERROR ("%s has too many joints for skeleton debug\n", fname);
	for (size_t j = 0; j < numjoints; j++)
		if (joint_infos[j].parent >= 0)
			num_skeleton_indexes += 2;
	TEMP_ALLOC_ASSIGN_COND (skeleton_indexes, num_skeleton_indexes, num_skeleton_indexes > 0);
	for (size_t j = 0, out_index = 0; j < numjoints; j++)
	{
		if (joint_infos[j].parent < 0)
			continue;
		skeleton_indexes[out_index++] = (unsigned short)joint_infos[j].parent;
		skeleton_indexes[out_index++] = (unsigned short)j;
	}

	// 2. Compute absolute animation joints:
	for (size_t pose_index = 0; pose_index < anim.numposes; ++pose_index)
	{
		const jointpose_t *in_pose = anim.posedata + (pose_index * anim.numjoints);
		jointpose_t		  *out_pose = skinning_joints + (pose_index * anim.numjoints);
		for (size_t joint_index = 0; joint_index < anim.numjoints; ++joint_index)
		{
			ssize_t poseparent = joint_infos[joint_index].poseparent;

			if (poseparent >= (ssize_t)joint_index)
				MD5ERROR ("%s: joint has bad pose parent order\n", fname);
			// concat it onto the parent (relative->abs)
			if (poseparent < 0)
				memcpy (concat_joints[joint_index].mat, in_pose[joint_index].mat, sizeof (jointpose_t));
			else
				R_ConcatTransforms ((void *)concat_joints[poseparent].mat, (void *)in_pose[joint_index].mat, (void *)concat_joints[joint_index].mat);
			memcpy (out_pose[joint_index].mat, concat_joints[joint_index].mat, sizeof (jointpose_t));
		}
	}
	SAFE_FREE (anim.posedata);

	// 3. each mesh has its own aliashdr_t : load vertices, triangles, textures...etc. and upload to GPU each surface:

	for (int m = 0; m < nummeshes; m++)
	{
		MD5EXPECT ("mesh");
		MD5EXPECT ("{");

		// go to the  surf, a.k.a mesh, chaining the next nextsurface
		surf = (aliashdr_t *)((byte *)outhdr + m * hdrsize);
		if (m + 1 < nummeshes)
			surf->nextsurface = (aliashdr_t *)((byte *)outhdr + (m + 1) * hdrsize);
		else
			surf->nextsurface = NULL;

		for (size_t j = 0; j < 3; j++)
		{
			surf->scale_origin[j] = 0;
			surf->scale[j] = 1.0;
		}

		surf->numjoints = numjoints;

		if (anim.numposes)
		{
			for (size_t j = 0; j < anim.numposes; j++)
			{
				surf->frames[j].firstpose = j;
				surf->frames[j].numposes = 1;
				surf->frames[j].interval = 0.1;
			}
			surf->numframes = anim.numposes;
		}

		//"shader" is the texture of the surf
		MD5EXPECT ("shader");
		char shader_name[MAX_QPATH];
		q_strlcpy (shader_name, (const char *)com_token, sizeof (shader_name));

		// MD5 have only 1 surface pose, meaning 1 vertex-like "pose" (not to ne mixed with md5animctx_t anim poses !)
		//  because it uses skeletal animation instead of displaying/interpolating different frames/poses of vertices
		surf->numposes = 1;

		buffer = COM_Parse (buffer);
		MD5EXPECT ("numverts");
		size_t mesh_numverts = MD5UINT ();
		if (!mesh_numverts || mesh_numverts > (size_t)UINT16_MAX + 1 ||
			mesh_numverts > SIZE_MAX / sizeof (md5vertinfo_t) || mesh_numverts > SIZE_MAX / sizeof (md5vert8_t))
			MD5ERROR ("%s: mesh vertex count is invalid or too large\n", fname);
		surf->numverts_vbo = surf->numverts = (int)mesh_numverts;

		TEMP_ALLOC_ASSIGN_ZEROED (vinfo, surf->numverts);
		unsigned int max_mesh_influences = 0;

		while (MD5CHECK ("vert"))
		{
			size_t idx = MD5UINT ();
			if (idx >= (size_t)surf->numverts)
				MD5ERROR ("%s: vertex index out of bounds\n", fname);
			MD5EXPECT ("(");
			vinfo[idx].st[0] = MD5FLOAT ();
			vinfo[idx].st[1] = MD5FLOAT ();
			MD5EXPECT (")");
			vinfo[idx].firstweight = MD5UINT ();
			vinfo[idx].count = MD5UINT ();
			max_mesh_influences = q_max (max_mesh_influences, vinfo[idx].count);
		}

		surf->poseverttype = (max_mesh_influences > NUM_JOINT_INFLUENCES_4_WEIGHT) ? PV_MD5_8 : PV_MD5;
		const size_t md5_vertex_size = (surf->poseverttype == PV_MD5_8) ? sizeof (md5vert8_t) : sizeof (md5vert_t);
		if ((size_t)surf->numverts > SIZE_MAX / md5_vertex_size)
			MD5ERROR ("%s: mesh vertex data is too large\n", fname);
		TEMP_ALLOC_ASSIGN_ZEROED (poutvertexes, surf->numverts * md5_vertex_size);

		// MD5 violation: the skin is a single material. adding prefixes/postfixes here is the wrong thing to do.
		// but we do so anyway, because rerelease compat.
		surf->numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, NULL, m, nummeshes, MAX_SKINS, shader_name, MD5_Skin_Name);

		if (surf->numskins == 0)
			Con_Warning ("MD5: %s, no skins found for surf '%s' (%d)\n", fname, shader_name, m);

		MD5EXPECT ("numtris");
		size_t mesh_numtris = MD5UINT ();
		if (mesh_numtris > INT_MAX / 3)
			MD5ERROR ("%s: mesh triangle count is invalid or too large\n", fname);
		surf->numtris = (int)mesh_numtris;
		surf->numindexes = surf->numtris * 3;
		TEMP_ALLOC_ASSIGN_ZEROED (poutindexes, surf->numindexes);

		while (MD5CHECK ("tri"))
		{
			size_t idx = MD5UINT ();
			if (idx >= (size_t)surf->numtris)
				MD5ERROR ("%s: triangle index out of bounds\n", fname);
			idx *= 3;
			for (size_t j = 0; j < 3; j++)
			{
				size_t t = MD5UINT ();
				if (t >= (size_t)surf->numverts)
					MD5ERROR ("%s: vertex index out of bounds\n", fname);
				poutindexes[idx + j] = t;
			}
		}

		// md5 is a gpu-unfriendly interchange format. :(
		MD5EXPECT ("numweights");
		size_t numweights = MD5UINT ();
		if (numweights > SIZE_MAX / sizeof (*weight))
			MD5ERROR ("%s: weight count is too large\n", fname);
		TEMP_ALLOC_ASSIGN_ZEROED (weight, numweights);

		while (MD5CHECK ("weight"))
		{
			size_t idx = MD5UINT ();
			if (idx >= numweights)
				MD5ERROR ("%s: weight index out of bounds\n", fname);

			weight[idx].joint_index = MD5UINT ();
			if (weight[idx].joint_index >= numjoints)
				MD5ERROR ("%s: joint index out of bounds\n", fname);
			weight[idx].pos[3] = MD5FLOAT ();
			MD5EXPECT ("(");
			weight[idx].pos[0] = MD5FLOAT () * weight[idx].pos[3];
			weight[idx].pos[1] = MD5FLOAT () * weight[idx].pos[3];
			weight[idx].pos[2] = MD5FLOAT () * weight[idx].pos[3];
			MD5EXPECT (")");
		}

		MD5EXPECT ("}");

		// so make it gpu-friendly.
		if (!MD5_BakeInfluences (fname, joint_poses, poutvertexes, surf->poseverttype, vinfo, weight, surf->numverts, numweights))
			goto error;
		// and now make up the normals that the format lacks. we'll still probably have issues from seams, but then so did qme, so at least its faithful...
		// :P
		MD5_ComputeNormals (poutvertexes, md5_vertex_size, surf->numverts, poutindexes, surf->numindexes);

		for (size_t j = 0; j < (size_t)surf->numverts; j++)
		{
			md5vert_t *poutvertex = (md5vert_t *)(poutvertexes + j * md5_vertex_size);
			for (size_t k = 0; k < 3; k++)
			{
				mod->mins[k] = q_min (mod->mins[k], poutvertex->xyz[k]);
				mod->maxs[k] = q_max (mod->maxs[k], poutvertex->xyz[k]);
			}
			dist = poutvertex->xyz[0] * poutvertex->xyz[0] + poutvertex->xyz[1] * poutvertex->xyz[1];
			if (yawradius < dist)
				yawradius = dist;
			dist += poutvertex->xyz[2] * poutvertex->xyz[2];
			if (radius < dist)
				radius = dist;
		}

		TEMP_FREE (weight);
		TEMP_FREE (vinfo);

		// Upload to GPU that surface/mesh m:
		GLMesh_UploadBuffers (
			mod, surf, poutindexes, (byte *)poutvertexes, NULL, skinning_joints, m == 0 ? skeleton_indexes : NULL, m == 0 ? num_skeleton_indexes : 0);

		TEMP_FREE (poutvertexes);
		TEMP_FREE (poutindexes);

	} // end foreach mesh

	if (!isDedicated)
	{
		if (!(retained_skeleton = Mem_Alloc (retained_allocation_size)))
			MD5ERROR ("%s: couldn't allocate retained MD5 skeleton\n", fname);
		retained_skeleton->allocation_size = retained_allocation_size;
		retained_skeleton->joint_count = numjoints;
		retained_skeleton->pose_count = anim.numposes;
		retained_skeleton->poses_offset = retained_joints_offset + retained_joint_bytes;
		retained_skeleton->from_rerelease = verified_rerelease_mesh;
		for (size_t joint = 0; joint < numjoints; ++joint)
		{
			md5_skeleton_joint_t *out_joint = &retained_skeleton->joints[joint];
			q_strlcpy (out_joint->name, joint_infos[joint].name, sizeof (out_joint->name));
			out_joint->parent = (int)joint_infos[joint].parent;
			out_joint->poseparent = (int)joint_infos[joint].poseparent;
			memcpy (out_joint->bind, joint_poses[joint].mat, sizeof (out_joint->bind));
		}
		if (retained_pose_bytes)
			memcpy ((byte *)retained_skeleton + retained_skeleton->poses_offset, skinning_joints, retained_pose_bytes);
	}

	// the MD5 format does not have its own modelflags, yet we still need to know about trails and rotating etc
	mod->flags = MD5_HackyModelFlags (mod->name);

	mod->synctype = ST_FRAMETIME; // keep MD5 animations synced to when .frame is changed. framegroups are otherwise not very useful.
	mod->type = mod_alias;
	mod->extradata[PV_MD5] = (byte *)outhdr;
	mod->md5_skeleton = retained_skeleton;
	retained_skeleton = NULL;

	radius = sqrtf (radius);
	mod->rmins[0] = mod->rmins[1] = mod->rmins[2] = -radius;
	mod->rmaxs[0] = mod->rmaxs[1] = mod->rmaxs[2] = radius;

	yawradius = sqrtf (yawradius);
	mod->ymins[0] = mod->ymins[1] = -yawradius;
	mod->ymaxs[0] = mod->ymaxs[1] = yawradius;
	mod->ymins[2] = mod->mins[2];
	mod->ymaxs[2] = mod->maxs[2];

	TEMP_FREE (concat_joints);
	TEMP_FREE (skinning_joints);
	TEMP_FREE (skeleton_indexes);

	TEMP_FREE (joint_poses);
	TEMP_FREE (joint_infos);

	return true;

error:
	// Recoverable replacement-model failures fall back to the MDL, so release
	// any partial MD5 state that Sys_Error used to abandon by terminating.
	TEMP_FREE (weight);
	TEMP_FREE (vinfo);
	TEMP_FREE (poutvertexes);
	TEMP_FREE (poutindexes);
	TEMP_FREE (skeleton_indexes);
	SAFE_FREE (retained_skeleton);
	SAFE_FREE (anim.posedata);
	SAFE_FREE (anim.animfile);
	if (outhdr)
	{
		for (size_t surface_index = 0; surface_index < nummeshes; surface_index++)
		{
			aliashdr_t *hdr = (aliashdr_t *)((byte *)outhdr + surface_index * hdrsize);
			for (int skin_index = 0; skin_index < MAX_SKINS; skin_index++)
				SAFE_FREE (hdr->texels[skin_index]);
		}
		GLMesh_DeleteMeshBuffers (outhdr);
		Mem_Free (outhdr);
	}
	TEMP_FREE (concat_joints);
	TEMP_FREE (skinning_joints);
	TEMP_FREE (joint_poses);
	TEMP_FREE (joint_infos);
	return false;
}

static qboolean Mod_LoadMD5MeshModel (qmodel_t *mod, const void *buffer,
	const char *asset_name, qfilesize_t asset_size)
{
	const char *fname = mod->name;
	size_t		numjoints;
	size_t		nummeshes;
	qboolean	verified_rerelease_mesh;

	verified_rerelease_mesh = Mod_IsVerifiedRereleaseRangerAsset (asset_name,
		"progs/player.md5mesh", buffer, asset_size, 178658, 0x7911b9b0U);

	buffer = COM_Parse (buffer);

	MD5EXPECT ("MD5Version");
	MD5EXPECT (MD5_VERSION);
	if (MD5CHECK ("commandline"))
		buffer = COM_Parse (buffer);
	MD5EXPECT ("numJoints");
	numjoints = MD5UINT ();
	MD5EXPECT ("numMeshes");
	nummeshes = MD5UINT ();

	if (numjoints <= 0)
		MD5ERROR ("%s has no joints\n", mod->name);
	if (nummeshes <= 0)
		MD5ERROR ("%s has no meshes\n", mod->name);

	if (strcmp (com_token, "joints"))
		MD5ERROR ("Mod_LoadMD5MeshModel(%s): expected \"%s\", found \"%s\"\n", fname, "joints", com_token);

	return Mod_LoadMD5MeshModelData (mod, buffer, numjoints, nummeshes,
		verified_rerelease_mesh);

error:
	return false;
}

/*
=====================
Mod_AppendMD3SkinFile:
if expected_skin_index >= 0 assume file_contents is for skin index expected_skin_index,
else we assume it is for a .skin file containing all surfaces definitions for all skins.
=====================
*/
static void Mod_AppendMD3SkinFile (char *file_contents, int expected_skin_index, all_surfaces_def_t *surf_defs)
{
	// Read .skin file contents, appending to surf_defs
	//  a line is made of:
	//  surface_name,skin0, skin1,  skin2, ...

	// split by lines:
	size_t nb_lines = 0;
	char **lines = q_strsplit (file_contents, "\n\r", &nb_lines);

	// parse line by line
	for (size_t line_index = 0; line_index < nb_lines; line_index++)
	{
		// This line must be split by its ',' and stripping whitespaces of the resulting sub-tokens
		size_t nb_fields = 0;
		char **fields = q_strsplit (q_strtrim (lines[line_index]), ",", &nb_fields);

		// there should be at least a "surface name, skin"
		// butno more than "surface name, skin0, skin1, ..." upto MAX_SKINS
		if ((nb_fields < 2) || (nb_fields > MAX_SKINS + 1))
		{
			Mem_Free (fields);
			continue;
		}

		// token 0 is the surface name
		char *surface_name = q_strtrim (fields[0]);

		// find the surface index in surf_defs matching surface_name:
		int found_surface_index = -1;

		for (size_t surface_index = 0; surface_index < surf_defs->numsurfaces; surface_index++)
		{
			if (!strcmp (surf_defs->surfaces[surface_index].surfname.c_str, surface_name))
			{
				found_surface_index = surface_index;
			}
		}

		// if not found, this is a new surface, add it:
		if (found_surface_index < 0)
		{
			if (surf_defs->numsurfaces >= MAX_SURFACES)
			{
				// too many surfaces, skip
				Mem_Free (fields);
				continue;
			}

			q_strlcpy (surf_defs->surfaces[surf_defs->numsurfaces].surfname.c_str, surface_name, MAX_QPATH);
			found_surface_index = surf_defs->numsurfaces;
			surf_defs->numsurfaces++;
		}

		//  Different line formats:
		// 1) New format    : if expected_skin_index < 0 a line lists all skins for a given framegroup in one go.
		// 2) Legacy format : else if expected_skin_index >= 0 we only expect 1 skin name per-line, whose index
		// is expected_skin_index.
		for (int field_index = 1; field_index < nb_fields; field_index++)
		{
			int skin_index = ((expected_skin_index < 0) ? field_index - 1 : expected_skin_index);

			char *skin_name = q_strtrim (fields[field_index]);

			int current_nb_framegroups = surf_defs->surfaces[found_surface_index].skins[skin_index].numframegroups;

			// we are incrementing the frame group count for each skin:
			if (current_nb_framegroups < MAX_FRAMEGROUPS)
			{
				q_strlcpy (surf_defs->surfaces[found_surface_index].skins[skin_index].framegroups[current_nb_framegroups].c_str, skin_name, MAX_QPATH);
				surf_defs->surfaces[found_surface_index].skins[skin_index].numframegroups++;
			}
		}
		// update skin counts:
		int current_num_skins = surf_defs->surfaces[found_surface_index].numskins;

		current_num_skins = q_max (current_num_skins, ((expected_skin_index < 0) ? nb_fields - 1 : expected_skin_index + 1));

		surf_defs->surfaces[found_surface_index].numskins = current_num_skins;

		Mem_Free (fields);
	} // for each line

	Mem_Free (lines);
}

/*
=====================
Mod_LoadMD3SkinDefinitions
=====================
*/
static void Mod_LoadMD3SkinDefinitions (qmodel_t *mod, all_surfaces_def_t *surf_defs)
{
	memset ((void *)surf_defs, 0x0, sizeof (*surf_defs));

	char basename[MAX_QPATH];
	COM_StripExtension (mod->name, basename, sizeof (basename));

	bool loading_complete = false;

	// file_number = -1 is special case = no numbering suffix
	// else the numbering is based in skin indices
	for (int file_number = -1; !loading_complete && file_number < MAX_SKINS; file_number++)
	{
		// version with .md3 suffix has priority over the non-suffix one.
		for (int has_md3_suffix = 1; has_md3_suffix >= 0; has_md3_suffix--)
		{
			char skinfile_name[MAX_QPATH];
			// build a .skin file name:
			q_snprintf (
				skinfile_name, MAX_QPATH, "%s%s%s%s", basename, (has_md3_suffix ? ".md3" : ""), ((file_number >= 0) ? va ("_%d", file_number) : ""), ".skin");

			unsigned int opened_file_path_id = 0;
			// Load the file as binary blob, to be parsed in Mod_AppendMD3SkinFile.
			char		*file_contents = (char *)COM_LoadFile (skinfile_name, &opened_file_path_id);

			if (file_contents)
			{
				if (opened_file_path_id >= mod->path_id)
				{
					// Read contents:
					Mod_AppendMD3SkinFile (file_contents, file_number, surf_defs);
					Mem_Free (file_contents);

					// if file_number = -1 i.e. no numbering suffix variant was loaded successfully,
					// we assumes it contains all surface definitions, so skip the numbered ones entirely.
					if (file_number == -1)
						loading_complete = true;

					// if a .md3 suffix variant was found, skip the non-md3 one.
					if (has_md3_suffix)
						break;
				}
				else
				{
					Con_DPrintf ("MD3 skfile: ignored %s from a gamedir with lower priority\n", skinfile_name);
					Mem_Free (file_contents);

					// no more skins, stop searching for more files
					if ((file_number >= 0) && (has_md3_suffix == 0))
					{
						loading_complete = true;
						break;
					}
				}
			}
			else
			{ // no more skins, stop searching for more files
				if ((file_number >= 0) && (has_md3_suffix == 0))
				{
					loading_complete = true;
					break;
				}
			}
		}
	}

	// List contents if developer >= 1
	if (developer.value >= 1)
	{
		if (!surf_defs->numsurfaces)
		{
			Con_DPrintf ("MD3 skfile: %s, no surfaces found.\n", mod->name);
		}
		else
		{
			for (size_t surf_index = 0; surf_index < surf_defs->numsurfaces; surf_index++)
				for (size_t skin_index = 0; skin_index < surf_defs->surfaces[surf_index].numskins; skin_index++)
					for (size_t framegrp_index = 0; framegrp_index < surf_defs->surfaces[surf_index].skins[skin_index].numframegroups; framegrp_index++)
						Con_DPrintf (
							"MD3 skfile:%s|surf %s(%d)|%d-%d|%s\n", mod->name, surf_defs->surfaces[surf_index].surfname.c_str, (int)surf_index, (int)skin_index,
							(int)framegrp_index, surf_defs->surfaces[surf_index].skins[skin_index].framegroups[framegrp_index].c_str);
		}
	}
}

//
SKIN_PATTERN_FUNC_DEF (MD3_Skinfile)
{
	// BEWARE : surf_index here designates the mesh index in the MD3 file
	// which has nothing to do with the index of the surface in surf_defs.
	// So we have to search for the right surface by name using 'basename' which is in this case
	// is the MD3 surface name to look for.
	int skinfile_surf_index = -1;

	for (size_t i = 0; i < surf_defs->numsurfaces; i++)
	{
		if (!strcmp (basename, surf_defs->surfaces[i].surfname.c_str))
		{
			skinfile_surf_index = i;
			break;
		}
	}

	if (skinfile_surf_index == -1)
		return;

	if (skin_index >= surf_defs->surfaces[skinfile_surf_index].numskins)
		return;

	if (framegroup_index >= surf_defs->surfaces[skinfile_surf_index].skins[skin_index].numframegroups)
		return;

	// strip extension:
	char skin_name[MAX_QPATH];
	q_strlcpy (skin_name, surf_defs->surfaces[skinfile_surf_index].skins[skin_index].framegroups[framegroup_index].c_str, MAX_QPATH);
	COM_StripExtension (skin_name, output_name, MAX_QPATH);
}
// skin name : surfacename.ext (1 skin, 1 framgroup)
SKIN_PATTERN_FUNC_DEF (MD3_Surf_Name_Legacy_Single)
{
	q_snprintf (output_name, MAX_QPATH, "%s", basename);
}

// skin name : surfacename_X.ext (0..X-1 skin, 1 framgroup)
SKIN_PATTERN_FUNC_DEF (MD3_Surf_Name_Legacy_One_Framegroup)
{
	q_snprintf (output_name, MAX_QPATH, "%s_%d", basename, skin_index);
}

// skin name : surfacename_X_Y.ext (0..X-1 skin, 0..Y-1 framgroup)
SKIN_PATTERN_FUNC_DEF (MD3_Surf_Name_Legacy)
{
	q_snprintf (output_name, MAX_QPATH, "%s_%d_%d", basename, skin_index, framegroup_index);
}

// skin name : model_name.md3_S_X_Y.ext (0..S-1 surfaces, 0..X-1 skin, 0..Y-1 framgroup) using Legacy conventions (%d), using the model name as prefix
SKIN_PATTERN_FUNC_DEF (MD3_Model_Name_Legacy_Full)
{
	char newname[MAX_QPATH];
	COM_StripExtension (basename, newname, sizeof (newname));
	COM_AddExtension (newname, ".md3", sizeof (newname));
	q_snprintf (output_name, MAX_QPATH, "%s_%d_%d_%d", newname, surf_index, skin_index, framegroup_index);
}

/*
=====================
Mod_LoadMD3SurfaceSkins
=====================
*/
static int Mod_LoadMD3SurfaceSkins (
	qmodel_t *mod, aliashdr_t *surf, all_surfaces_def_t *surfaces_def, const char *surface_name, int surface_index, size_t numsurfs, size_t numskins)
{
	// 1. Try to load first from existing surfaces_def built from .skin files:
	int surf_numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, surfaces_def, surface_index, numsurfs, numskins, surface_name, MD3_Skinfile);

	// 2. Try to load the "legacy" MD3 namings from the existing Quake 3 ecosystem :
	// skin name : surfacename_X_Y.ext (0..X-1 skins, 0..Y-1 framgroups)
	if (!surf_numskins)
		surf_numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, NULL, surface_index, numsurfs, numskins, surface_name, MD3_Surf_Name_Legacy);

	// skin name : surfacename_X.ext (0..X-1 skins, 1 framgroup)
	if (!surf_numskins)
		surf_numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, NULL, surface_index, numsurfs, numskins, surface_name, MD3_Surf_Name_Legacy_One_Framegroup);

	//  skin name : surfacename.ext (1 skin, 1 framgroup)
	if (!surf_numskins)
		surf_numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, NULL, surface_index, numsurfs, 1, surface_name, MD3_Surf_Name_Legacy_Single);

	// 3. Model-based names:
	//   skin name : model_name.md3_S_X_Y.ext (0..S-1 surfaces, 0..X-1 skin, 0..Y-1 framgroup) using Legacy conventions (%d), using the model name as prefix
	if (!surf_numskins)
		surf_numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, NULL, surface_index, numsurfs, numskins, mod->name, MD3_Model_Name_Legacy_Full);

	// 4. MD5-like naming conventions:
	// skin name : surfacename_X_Y.ext (0..X-1 skins, 0..Y-1 framgroups) with %s_%02u_%02u pattern
	if (!surf_numskins)
		surf_numskins = (int)Mod_LoadMDXSkinsByIndex (mod, surf, NULL, surface_index, numsurfs, numskins, surface_name, MD5_Skin_Name);

	return surf_numskins;
}

#undef SKIN_PATTERN_FUNC_DEF

static qboolean Mod_CheckedSizeMul (size_t a, size_t b, size_t *result)
{
	if (a && b > SIZE_MAX / a)
		return false;
	*result = a * b;
	return true;
}

static qboolean Mod_CheckedSizeAdd (size_t a, size_t b, size_t *result)
{
	if (b > SIZE_MAX - a)
		return false;
	*result = a + b;
	return true;
}

static qboolean Mod_CheckedMD3Span (size_t offset, size_t span, size_t limit)
{
	size_t end;

	return Mod_CheckedSizeAdd (offset, span, &end) && end <= limit;
}

static qboolean Mod_CheckedMD3RelativeSpan (size_t base, size_t base_span,
	int relative_offset, size_t span, size_t limit, size_t *absolute_offset)
{
	size_t relative, absolute;

	if (relative_offset < 0)
		return false;
	relative = (size_t)relative_offset;
	if (!Mod_CheckedMD3Span (relative, span, base_span) ||
		!Mod_CheckedSizeAdd (base, relative, &absolute) ||
		!Mod_CheckedMD3Span (absolute, span, limit))
		return false;

	*absolute_offset = absolute;
	return true;
}

/*
=====================
Mod_LoadMD3Model
=====================
*/
static void Mod_LoadMD3Model (qmodel_t *mod, const void *buffer,
	qfilesize_t source_size)
{
	aliashdr_t	   *outhdr, *surf;
	md3Header_t	   *pinheader;
	md3Frame_t	   *pinframes;
	md3Triangle_t  *pintriangle;
	md3XyzNormal_t *pinvertexes;
	md3St_t		   *pinst;
	size_t			hdrsize, hdrbase_size, hdrframes_size, hdrallocation_size;
	size_t			source_bytes, file_end, frames_offset, frames_bytes;
	size_t			surface_offset;
	int				numsurfs;
	int				numframes;

	if (!buffer || source_size < 0 ||
		(uint64_t)source_size > (uint64_t)SIZE_MAX ||
		(uint64_t)source_size < sizeof (md3Header_t))
		Sys_Error ("MD3: %s has a truncated file header", mod->name);
	source_bytes = (size_t)source_size;
	pinheader = (md3Header_t *)buffer;
	if (LittleLong (pinheader->ident) != IDMD3HEADER)
		Sys_Error ("MD3: %s has an invalid file ident", mod->name);

	int version = LittleLong (pinheader->version);
	if (version != MD3_VERSION)
		Sys_Error ("MD3: %s has wrong version number (%d should be %d)", mod->name, version, MD3_VERSION);

	numsurfs = LittleLong (pinheader->numSurfaces);
	numframes = LittleLong (pinheader->numFrames);
	int ofs_frames = LittleLong (pinheader->ofsFrames);
	int ofs_surfaces = LittleLong (pinheader->ofsSurfaces);
	int ofs_end = LittleLong (pinheader->ofsEnd);

	if (numframes <= 0)
		Sys_Error ("MD3: %s has no frames", mod->name);

	if (numframes > MAXALIASFRAMES)
		Sys_Error ("MD3: %s has too many frames (%i vs %i)", mod->name, numframes, MAXALIASFRAMES);

	if (numsurfs <= 0)
		Sys_Error ("MD3: %s has no surfaces", mod->name);

	if (numsurfs > MAX_SURFACES)
		Sys_Error ("MD3: %s has too many surfaces : %d (max %d)", mod->name, numsurfs, MAX_SURFACES);
	if (ofs_end < (int)sizeof (md3Header_t) || (size_t)ofs_end > source_bytes)
		Sys_Error ("MD3: %s has an invalid or truncated file span", mod->name);
	file_end = (size_t)ofs_end;
	if (ofs_frames < (int)sizeof (md3Header_t) ||
		!Mod_CheckedSizeMul ((size_t)numframes, sizeof (md3Frame_t), &frames_bytes) ||
		!Mod_CheckedMD3Span ((size_t)ofs_frames, frames_bytes, file_end))
		Sys_Error ("MD3: %s has a truncated or invalid frame span", mod->name);
	if (ofs_surfaces < (int)sizeof (md3Header_t) ||
		!Mod_CheckedMD3Span ((size_t)ofs_surfaces, sizeof (md3Surface_t), file_end))
		Sys_Error ("MD3: %s has a truncated or invalid surface span", mod->name);

	// Collect the skin definitions from .skin files, if any;
	all_surfaces_def_t *surf_def = Mem_Alloc (sizeof (all_surfaces_def_t));

	Mod_LoadMD3SkinDefinitions (mod, surf_def);

	frames_offset = (size_t)ofs_frames;
	pinframes = (md3Frame_t *)((byte *)buffer + frames_offset);

	hdrbase_size = sizeof (*outhdr) - sizeof (outhdr->frames);
	if (!Mod_CheckedSizeMul (sizeof (outhdr->frames), (size_t)numframes, &hdrframes_size) ||
		!Mod_CheckedSizeAdd (hdrbase_size, hdrframes_size, &hdrsize) ||
		!Mod_CheckedSizeMul (hdrsize, (size_t)numsurfs, &hdrallocation_size))
		Sys_Error ("MD3: %s header allocation is too large", mod->name);

	// alloc all aliashdr_t and their chained nextsurface, a.k.a numsurfs, in one array
	outhdr = (aliashdr_t *)Mem_Alloc (hdrallocation_size);

	// total_numverts and total_vertexes accumulate all vertices of the surface,
	// just to be able to Mod_CalcAliasBounds at the end.
	size_t			total_numverts = 0;
	md3XyzNormal_t *total_vertexes = NULL;

	// for each of the surfaces :
	surface_offset = (size_t)ofs_surfaces;
	for (int m = 0; m < numsurfs; m++)
	{
		if (!Mod_CheckedMD3Span (surface_offset, sizeof (md3Surface_t), file_end))
			Sys_Error ("MD3: %s surface chain is truncated at surface %d", mod->name, m);
		md3Surface_t *pinsurface = (md3Surface_t *)((byte *)buffer + surface_offset);
		int surface_end = LittleLong (pinsurface->ofsEnd);
		if (surface_end < (int)sizeof (md3Surface_t) ||
			!Mod_CheckedMD3Span (surface_offset, (size_t)surface_end, file_end))
			Sys_Error ("MD3: %s surface chain has an invalid span at surface %d", mod->name, m);
		size_t surface_span = (size_t)surface_end;

		if (LittleLong (pinsurface->ident) != IDMD3HEADER)
			Sys_Error ("MD3: %s corrupt surface ident", mod->name);
		if (LittleLong (pinsurface->numFrames) != numframes)
			Sys_Error ("MD3: %s mismatched framecounts", mod->name);

		// go to the surf, chaining the next nextsurface
		surf = (aliashdr_t *)((byte *)outhdr + m * hdrsize);
		if (m + 1 < numsurfs)
			surf->nextsurface = (aliashdr_t *)((byte *)outhdr + (m + 1) * hdrsize);
		else
			surf->nextsurface = NULL;

		surf->poseverttype = PV_QUAKE3;

		// the number of vertices per-frame:
		int numverts = LittleLong (pinsurface->numVerts);
		if (numverts <= 0)
			Sys_Error ("MD3: %s surface %d has no vertices", mod->name, m);
		if ((size_t)numverts > (size_t)UINT16_MAX + 1)
			Sys_Error ("MD3: %s surface %d has too many vertices for 16-bit indexes", mod->name, m);
		surf->numverts_vbo = surf->numverts = numverts;

		surf->numtris = LittleLong (pinsurface->numTriangles);
		if (surf->numtris < 0)
			Sys_Error ("MD3: %s surface %d has an invalid triangle count", mod->name, m);
		if (surf->numtris > INT_MAX / 3)
			Sys_Error ("MD3: %s surface %d has too many triangles", mod->name, m);
		surf->numindexes = surf->numtris * 3;

		size_t surface_vertex_count, surface_vertex_bytes, surface_st_bytes, surface_index_bytes;
		size_t surface_st_source_bytes, surface_triangle_bytes;
		size_t vertex_offset, triangle_offset, st_offset;
		if (!Mod_CheckedSizeMul ((size_t)numframes, (size_t)numverts, &surface_vertex_count) ||
			!Mod_CheckedSizeMul (surface_vertex_count, sizeof (md3XyzNormal_t), &surface_vertex_bytes) ||
			!Mod_CheckedSizeMul ((size_t)numverts, sizeof (aliasmesh_t), &surface_st_bytes) ||
			!Mod_CheckedSizeMul ((size_t)surf->numindexes, sizeof (unsigned short), &surface_index_bytes) ||
			!Mod_CheckedSizeMul ((size_t)numverts, sizeof (md3St_t), &surface_st_source_bytes) ||
			!Mod_CheckedSizeMul ((size_t)surf->numtris, sizeof (md3Triangle_t), &surface_triangle_bytes))
			Sys_Error ("MD3: %s surface %d allocation is too large", mod->name, m);
		if (!Mod_CheckedMD3RelativeSpan (surface_offset, surface_span,
				LittleLong (pinsurface->ofsXyzNormals), surface_vertex_bytes,
				file_end, &vertex_offset) ||
			!Mod_CheckedMD3RelativeSpan (surface_offset, surface_span,
				LittleLong (pinsurface->ofsTriangles), surface_triangle_bytes,
				file_end, &triangle_offset) ||
			!Mod_CheckedMD3RelativeSpan (surface_offset, surface_span,
				LittleLong (pinsurface->ofsSt), surface_st_source_bytes,
				file_end, &st_offset))
			Sys_Error ("MD3: %s surface %d has a truncated or invalid vertex, triangle, or st span", mod->name, m);

		// All the vertices for this surface, concat of the vertices of each of the numframes, one frame after another:
		pinvertexes = (md3XyzNormal_t *)((byte *)buffer + vertex_offset);

		md3XyzNormal_t *poutvertexes = (md3XyzNormal_t *)Mem_Alloc (surface_vertex_bytes);
		// keep track of the original poutvertexes, because we are going to pointer arithmetic below...
		md3XyzNormal_t *poutvertexes_start = poutvertexes;

		// Load skins for that surface m:
		surf->numskins = Mod_LoadMD3SurfaceSkins (mod, surf, surf_def, q_strtrim (pinsurface->name), m, numsurfs, MAX_SKINS);

		if (surf->numskins == 0)
			Con_Warning ("MD3: %s, no skins found for surf '%s' (%d)\n", mod->name, pinsurface->name, m);

		// for each frame:
		// only 1 pose for MD3, it have frames instead
		surf->numposes = 1;

		for (int ival = 0; ival < numframes; ival++)
		{
			surf->frames[ival].firstpose = ival;
			surf->frames[ival].numposes = 1;
			surf->frames[ival].interval = 0.1;

			q_strlcpy (surf->frames[ival].name, pinframes->name, sizeof (surf->frames[ival].name));

			for (int j = 0; j < 3; j++)
			{ // fixme...
				surf->frames[ival].bboxmin.v[j] = 0;
				surf->frames[ival].bboxmax.v[j] = 255;
			}

			for (int j = 0; j < surf->numverts; j++)
				poutvertexes[j] = pinvertexes[j];

			poutvertexes += surf->numverts;
			pinvertexes += surf->numverts;
		}
		surf->numframes = numframes;

		pintriangle = (md3Triangle_t *)((byte *)buffer + triangle_offset);

		unsigned short *poutindexes = (unsigned short *)Mem_Alloc (surface_index_bytes);
		// keep track of the original poutindexes, because we are going to pointer arithmetic below...
		unsigned short *poutindexes_start = poutindexes;

		for (int ival = 0; ival < surf->numtris; ival++, pintriangle++, poutindexes += 3)
		{
			for (int j = 0; j < 3; j++)
			{
				int vertex_index = LittleLong (pintriangle->indexes[j]);
				if (vertex_index < 0 || vertex_index >= numverts)
					Sys_Error ("MD3: %s surface %d has an invalid vertex index", mod->name, m);
				poutindexes[j] = (unsigned short)vertex_index;
			}
		}

		for (int j = 0; j < 3; j++)
		{
			surf->scale_origin[j] = 0;
			surf->scale[j] = MD3_XYZ_SCALE;
		}

		// TODO: What to do with the shaders ?
		// int numshaders = pinsurface->numShaders;
		// md3Shader_t	   * pinshader = (md3Shader_t *)((byte *)pinsurface + LittleLong (pinsurface->ofsShaders));

		// and figure out the texture coords properly, now we know the actual sizes.
		pinst = (md3St_t *)((byte *)buffer + st_offset);

		aliasmesh_t *poutst = (aliasmesh_t *)Mem_Alloc (surface_st_bytes);

		for (int j = 0; j < surf->numverts; j++)
		{
			poutst[j].vertindex = j; // how is this useful?
			poutst[j].st[0] = pinst[j].s;
			poutst[j].st[1] = pinst[j].t;
		}

		// Upload to GPU that surface/mesh m:
		GLMesh_UploadBuffers (mod, surf, poutindexes_start, (byte *)poutvertexes_start, poutst, NULL, NULL, 0);

		// concat surface vertices to total_vertexes
		size_t new_total_numverts, total_vertex_bytes, bounds_vertex_bytes;
		if (!Mod_CheckedSizeAdd (total_numverts, (size_t)surf->numverts, &new_total_numverts) ||
			!Mod_CheckedSizeMul (new_total_numverts, sizeof (*poutvertexes), &total_vertex_bytes) ||
			!Mod_CheckedSizeMul ((size_t)surf->numverts, sizeof (*poutvertexes), &bounds_vertex_bytes))
			Sys_Error ("MD3: %s combined vertex data is too large", mod->name);
		total_vertexes = (md3XyzNormal_t *)Mem_Realloc (total_vertexes, total_vertex_bytes);
		memcpy ((void *)(total_vertexes + total_numverts), (const void *)poutvertexes_start, bounds_vertex_bytes);
		total_numverts = new_total_numverts;

		Mem_Free (poutst);
		Mem_Free (poutvertexes_start);
		Mem_Free (poutindexes_start);

		// go to the next surface:
		if (!Mod_CheckedSizeAdd (surface_offset, surface_span, &surface_offset))
			Sys_Error ("MD3: %s surface chain offset overflows", mod->name);

	} // end for surface

	// small violation of the spec, but it seems like noone else uses it.
	mod->flags = LittleLong (pinheader->flags);

	mod->type = mod_alias;
	mod->extradata[PV_QUAKE3] = (byte *)outhdr;

	// calc alias bounds of the whole surfaces model :
	Mod_CalcAliasBounds (mod, outhdr, total_numverts, (byte *)total_vertexes); // johnfitz

	Mem_Free (total_vertexes);
	Mem_Free (surf_def);
}

//=============================================================================

/*
================
Mod_Print
================
*/
void Mod_Print (void)
{
	int		  i;
	qmodel_t *mod;

	Con_SafePrintf ("Cached models:\n"); // johnfitz -- safeprint instead of print
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		Con_SafePrintf (
			"MDL:%s || MD5:%s || MD3:%s  -  %s\n", (mod->extradata[PV_QUAKE1]) ? "YES" : " no", (mod->extradata[PV_MD5]) ? "YES" : " no",
			(mod->extradata[PV_QUAKE3]) ? "YES" : " no", mod->name); // johnfitz -- safeprint instead of print
	}
	Con_Printf ("%i models\n", mod_numknown); // johnfitz -- print the total too
}
