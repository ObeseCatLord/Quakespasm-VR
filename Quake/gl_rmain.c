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
// r_main.c

#include "quakedef.h"
#include "vr_openxr_math.h"
#include <float.h>
#include "r_ssao.h"
#include "tasks.h"
#include "atomics.h"
#include "vr_aim.h"
#include "vr_input.h"
#include "vr_weapon_calibration.h"
#include "r_vrik_render.h"
#include "vr_weapon_menu.h"
#include "voice.h"

#include <float.h>
#include <math.h>

int r_visframecount; // bumped when going to a new PVS
int r_framecount;	 // used for dlight push checking

mplane_t frustum[4];

qboolean render_warp;

// johnfitz -- rendering statistics
atomic_uint32_t rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
atomic_uint32_t rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses;
atomic_uint32_t rs_blas_builds, rs_blas_refits, rs_blas_pose_reuses;
uint32_t		rs_cputime_us, rs_gputime_us;
uint32_t		rs_gpuwaittime_us, rs_gpuwaitaccum_us;
double			rs_frame_starttime;
char			rs_display_lines[4][40];
int				rs_display_numlines;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

float r_fovx, r_fovy; // johnfitz -- rendering fov may be different becuase of r_waterwarp

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

int d_lightstylevalue[MAX_LIGHTSTYLES]; // 8.8 fraction of base light value

extern cvar_t vr_aimmode;
extern cvar_t vr_crosshair;
extern cvar_t vr_crosshair_depth;
extern cvar_t vr_crosshair_size;
extern cvar_t vr_crosshair_alpha;
extern cvar_t vr_crosshairy;

typedef struct
{
	qboolean valid;
	int mode;
	float size_pixels;
	float alpha;
	vec3_t start;
	vec3_t impact;
} vr_crosshair_frame_t;

/* Prepared by SCR_UpdateScreen on the main owner, then read-only in the scene task. */
static vr_crosshair_frame_t vr_crosshair_frame;

/* Filled beside the stereo view in frame setup, then read-only from the debug
 * draw task. These world points are detached from mutable FBT state. */
static struct
{
	unsigned int role_mask;
	vec3_t tracker_world[VR_FBT_ROLE_COUNT];
	vec3_t target_world[VR_FBT_ROLE_COUNT];
} vr_fbt_visual_frame;

cvar_t r_drawentities = {"r_drawentities", "1", CVAR_NONE};
cvar_t cl_coop_nametags = {"cl_coop_nametags", "1", CVAR_ARCHIVE};
cvar_t r_drawviewmodel = {"r_drawviewmodel", "1", CVAR_NONE};
cvar_t scr_speeds = {"scr_speeds", "0", CVAR_NONE};
cvar_t r_pos = {"r_pos", "0", CVAR_NONE};
cvar_t r_fullbright = {"r_fullbright", "0", CVAR_NONE};
cvar_t r_lightmap = {"r_lightmap", "0", CVAR_NONE};
cvar_t r_wateralpha = {"r_wateralpha", "1", CVAR_ARCHIVE_GAME};
cvar_t r_oit = {"r_oit", "1", CVAR_ARCHIVE};
cvar_t r_dynamic = {"r_dynamic", "1", CVAR_ARCHIVE};
cvar_t r_novis = {"r_novis", "0", CVAR_ARCHIVE};
#if defined(USE_SIMD)
cvar_t r_simd = {"r_simd", "1", CVAR_ARCHIVE};
#endif
cvar_t r_alphasort = {"r_alphasort", "1", CVAR_ARCHIVE};

cvar_t gl_finish = {"gl_finish", "0", CVAR_NONE};
cvar_t gl_polyblend = {"gl_polyblend", "1", CVAR_NONE};
cvar_t gl_nocolors = {"gl_nocolors", "0", CVAR_NONE};

// johnfitz -- new cvars
cvar_t r_clearcolor = {"r_clearcolor", "2", CVAR_ARCHIVE};
cvar_t r_fastclear = {"r_fastclear", "1", CVAR_ARCHIVE};
cvar_t r_flatlightstyles = {"r_flatlightstyles", "0", CVAR_NONE};
cvar_t r_lerplightstyles = {"r_lerplightstyles", "1", CVAR_ARCHIVE_GAME}; // 0=off; 1=skip abrupt transitions; 2=always lerp
cvar_t gl_fullbrights = {"gl_fullbrights", "1", CVAR_ARCHIVE_GAME};
cvar_t gl_farclip = {"gl_farclip", "16384", CVAR_ARCHIVE};
cvar_t r_oldskyleaf = {"r_oldskyleaf", "0", CVAR_NONE};
cvar_t r_drawworld = {"r_drawworld", "1", CVAR_NONE};
cvar_t r_showtris = {"r_showtris", "0", CVAR_NONE};
cvar_t r_showskel = {"r_showskel", "0", CVAR_NONE};
cvar_t r_showbboxes = {"r_showbboxes", "0", CVAR_NONE};
cvar_t r_showbboxes_think = {"r_showbboxes_think", "0", CVAR_NONE};	  // 0=show all; 1=thinkers only; -1=non-thinkers only
cvar_t r_showbboxes_health = {"r_showbboxes_health", "0", CVAR_NONE}; // 0=show all; 1=healthy only; -1=non-healthy only
cvar_t r_showbboxes_links = {"r_showbboxes_links", "3", CVAR_NONE};	  // 0=off; 1=outgoing only; 2=incoming only; 3=incoming+outgoing
cvar_t r_showbboxes_targets = {"r_showbboxes_targets", "1", CVAR_NONE};
cvar_t r_showfields = {"r_showfields", "0", CVAR_NONE};
cvar_t r_showfields_align = {"r_showfields_align", "1", CVAR_ARCHIVE}; // 0=entity pos; 1=bottom-right
cvar_t r_lerpmodels = {"r_lerpmodels", "1", CVAR_ARCHIVE_GAME};
cvar_t r_lerpmove = {"r_lerpmove", "1", CVAR_ARCHIVE_GAME};
cvar_t r_lerpturn = {"r_lerpturn", "1", CVAR_ARCHIVE_GAME};
cvar_t r_nolerp_list = {
	"r_nolerp_list",
	"progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/"
	"v_xfist.mdl,progs/h2stuff/newfire.mdl",
	CVAR_NONE};

extern cvar_t r_vfog;
// johnfitz

cvar_t gl_zfix = {"gl_zfix", "1", CVAR_ARCHIVE}; // QuakeSpasm z-fighting fix

cvar_t r_lavaalpha = {"r_lavaalpha", "0", CVAR_NONE};
cvar_t r_telealpha = {"r_telealpha", "0", CVAR_NONE};
cvar_t r_slimealpha = {"r_slimealpha", "0", CVAR_NONE};

float map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
float map_fallbackalpha;

qboolean r_drawworld_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe; // johnfitz

cvar_t r_gpulightmapupdate = {"r_gpulightmapupdate", "1", CVAR_NONE};
cvar_t r_rtshadows = {"r_rtshadows", "2", CVAR_ARCHIVE};

cvar_t r_tasks = {"r_tasks", "1", CVAR_NONE};

cvar_t			r_indirect = {"r_indirect", "1", CVAR_NONE};
extern qboolean indirect_ready;

extern SDL_Mutex *draw_qcvm_mutex;

static atomic_uint32_t next_visedict;

/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int		  i;
	mplane_t *p;
	byte	  signbits;
	float	  vec[3];
	for (i = 0; i < 4; i++)
	{
		p = frustum + i;
		signbits = p->signbits;
		vec[0] = ((signbits & 1) ? emins : emaxs)[0];
		vec[1] = ((signbits & 2) ? emins : emaxs)[1];
		vec[2] = ((signbits & 4) ? emins : emaxs)[2];
		if (p->normal[0] * vec[0] + p->normal[1] * vec[1] + p->normal[2] * vec[2] < p->dist)
			return true;
	}
	return false;
}
/* The selected animated mesh can extend well beyond progs/player.mdl. Share
 * the conservative GPU-skinning bound between frustum and alpha sorting. */
static qboolean R_PreparedAvatarWorldBounds (const entity_t *e,
	const r_vrik_prepared_palette_t *tracked, vec3_t mins, vec3_t maxs)
{
	const aliashdr_t *header = tracked->geometry;
	if (!tracked->tracked_cull_valid || !header || !isfinite (tracked->tracked_cull_local_bound) ||
		tracked->tracked_cull_local_bound < 0.0)
		return false;
	double origin_radius_squared = 0.0;
	double max_header_scale = 0.0;
	for (int axis = 0; axis < 3; ++axis)
	{
		const double scale_origin = header->scale_origin[axis];
		const double header_scale = header->scale[axis];
		if (!isfinite (tracked->tracked_cull_origin[axis]) || !isfinite (scale_origin) || !isfinite (header_scale))
			return false;
		origin_radius_squared += scale_origin * scale_origin;
		if (max_header_scale < fabs (header_scale))
			max_header_scale = fabs (header_scale);
	}
	const double entity_scale = fabs ((double)ENTSCALE_DECODE (e->netstate.scale));
	if (!isfinite (entity_scale) || !isfinite (origin_radius_squared))
		return false;
	const double world_radius = entity_scale *
		(sqrt (origin_radius_squared) + max_header_scale * tracked->tracked_cull_local_bound);
	if (!isfinite (world_radius) || world_radius < 0.0)
		return false;
	/* Cover float rounding in GPU skinning, model transforms, and large BSP2 coordinates. */
	double origin_magnitude_squared = 0.0;
	for (int axis = 0; axis < 3; ++axis)
		origin_magnitude_squared += (double)tracked->tracked_cull_origin[axis] * tracked->tracked_cull_origin[axis];
	if (!isfinite (origin_magnitude_squared))
		return false;
	const double outward_radius = world_radius +
		64.0 * FLT_EPSILON * (1.0 + sqrt (origin_magnitude_squared) + world_radius);
	if (!isfinite (outward_radius) || outward_radius < 0.0 || outward_radius > FLT_MAX)
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		const double lower = (double)tracked->tracked_cull_origin[axis] - outward_radius;
		const double upper = (double)tracked->tracked_cull_origin[axis] + outward_radius;
		if (!isfinite (lower) || !isfinite (upper) || lower < -FLT_MAX || upper > FLT_MAX)
			return false;
		mins[axis] = nextafterf ((float)lower, -INFINITY);
		maxs[axis] = nextafterf ((float)upper, INFINITY);
		if (!isfinite (mins[axis]) || !isfinite (maxs[axis]))
			return false;
	}
	return true;
}

/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;
	vec_t  scalefactor, *minbounds, *maxbounds;
	const r_vrik_prepared_palette_t *tracked = R_VRIKRenderLookup (e);
	const qboolean prepared_geometry_matches = tracked && tracked->model && e->model &&
		e->model->type == mod_alias && tracked->model->type == mod_alias &&
		(tracked->alternate_avatar ?
			tracked->geometry == (const aliashdr_t *)tracked->model->extradata[PV_MD5] :
			tracked->model == e->model &&
			tracked->geometry == (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, e->skinnum));
	if (prepared_geometry_matches)
		return R_PreparedAvatarWorldBounds (e, tracked, mins, maxs) ? R_CullBox (mins, maxs) : false;

	if (e->angles[0] || e->angles[2]) // pitch or roll
	{
		minbounds = e->model->rmins;
		maxbounds = e->model->rmaxs;
	}
	else if (e->angles[1]) // yaw
	{
		minbounds = e->model->ymins;
		maxbounds = e->model->ymaxs;
	}
	else // no rotation
	{
		minbounds = e->model->mins;
		maxbounds = e->model->maxs;
	}

	scalefactor = ENTSCALE_DECODE (e->netstate.scale);
	if (scalefactor != 1.0f)
	{
		VectorMA (e->origin, scalefactor, minbounds, mins);
		VectorMA (e->origin, scalefactor, maxbounds, maxs);
	}
	else
	{
		VectorAdd (e->origin, minbounds, mins);
		VectorAdd (e->origin, maxbounds, maxs);
	}

	return R_CullBox (mins, maxs);
}

/*
===============
R_RotateForEntity -- johnfitz -- modified to take origin and angles instead of pointer to entity
===============
*/
void R_RotateForEntity (float matrix[16], vec3_t origin, vec3_t angles, unsigned char scale)
{
	float translation_matrix[16];
	TranslationMatrix (translation_matrix, origin[0], origin[1], origin[2]);
	MatrixMultiply (matrix, translation_matrix);

	float rotation_matrix[16];
	RotationMatrix (rotation_matrix, DEG2RAD (angles[1]), 0, 0, 1);
	MatrixMultiply (matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (-angles[0]), 0, 1, 0);
	MatrixMultiply (matrix, rotation_matrix);
	RotationMatrix (rotation_matrix, DEG2RAD (angles[2]), 1, 0, 0);
	MatrixMultiply (matrix, rotation_matrix);

	float scalefactor = ENTSCALE_DECODE (scale);
	if (scalefactor != 1.0f)
	{
		float mscale_matrix[16];
		ScaleMatrix (mscale_matrix, scalefactor, scalefactor, scalefactor);
		MatrixMultiply (matrix, mscale_matrix);
	}
}

//==============================================================================
//
// SETUP FRAME
//
//==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	int bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j = 0; j < 3; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1 << j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD(a) ((a) * M_PI_DIV_180)
static void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos (DEG2RAD (angle));
	scale_side = sin (DEG2RAD (angle));

	out[0] = scale_forward * forward[0] + scale_side * side[0];
	out[1] = scale_forward * forward[1] + scale_side * side[1];
	out[2] = scale_forward * forward[2] + scale_side * side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (float fovx, float fovy)
{
	int i;

	TurnVector (frustum[0].normal, vpn, vright, fovx / 2 - 90); // right plane
	TurnVector (frustum[1].normal, vpn, vright, 90 - fovx / 2); // left plane
	TurnVector (frustum[2].normal, vpn, vup, 90 - fovy / 2);	// bottom plane
	TurnVector (frustum[3].normal, vpn, vup, fovy / 2 - 90);	// top plane

	for (i = 0; i < 4; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); // FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_FrustumMatrix
=============
*/
#define NEARCLIP 4
static void GL_FrustumMatrix (float matrix[16], float fovx, float fovy)
{
	const float w = 1.0f / tanf (fovx * 0.5f);
	const float h = 1.0f / tanf (fovy * 0.5f);

	// reduce near clip distance at high FOV's to avoid seeing through walls
	const float d = 12.f * q_min (w, h);
	const float n = CLAMP (0.5f, d, NEARCLIP);

	memset (matrix, 0, 16 * sizeof (float));

	// First column
	matrix[0 * 4 + 0] = w;

	// Second column
	matrix[1 * 4 + 1] = -h;

	// Third column
	matrix[2 * 4 + 2] = 0.0f;
	matrix[2 * 4 + 3] = -1.0f;

	// Fourth column
	matrix[3 * 4 + 2] = n;
}

/*
=============
R_SetupMatrices
=============
*/
vec3_t r_stereo_origins[2];
float r_stereo_radius;
static vec3_t stereo_forward, stereo_right, stereo_up;
static float stereo_bounds[4];
static qboolean stereo_frustum_valid;
static qboolean stereo_view_adjusted;
static vec3_t stereo_base_origin, stereo_base_angles;
static qboolean stereo_have_reference;
static vec3_t stereo_reference_position;
static vec3_t stereo_tracking_forward, stereo_tracking_right, stereo_tracking_up;
static qboolean stereo_tracking_basis_valid;

static void R_SetupMatrices (void);
static qboolean R_VectorIsFinite (const vec3_t vector);

#define R_FBT_VISUAL_WORLD_MIN (-32768.0f)
#define R_FBT_VISUAL_WORLD_MAX 32767.0f

static qboolean R_FBTVisualWorldPoint (const vec3_t root_point,
	const vec3_t head_root, float body_yaw, float units_per_metre,
	vec3_t world_point)
{
	const float radians = body_yaw * M_PI_DIV_180;
	const float cosine = cosf (radians), sine = sinf (radians);
	vec3_t delta;
	if (!root_point || !head_root || !world_point || !R_VectorIsFinite (root_point) ||
		!R_VectorIsFinite (head_root) || !isfinite (body_yaw) ||
		!isfinite (units_per_metre) || units_per_metre <= 0.0f ||
		!R_VectorIsFinite (r_refdef.vieworg))
		return false;
	for (int axis = 0; axis < 3; ++axis)
		delta[axis] = (root_point[axis] - head_root[axis]) * units_per_metre;
	world_point[0] = r_refdef.vieworg[0] + delta[0] * cosine - delta[1] * sine;
	world_point[1] = r_refdef.vieworg[1] + delta[0] * sine + delta[1] * cosine;
	world_point[2] = r_refdef.vieworg[2] + delta[2];
	if (!R_VectorIsFinite (world_point))
		return false;
	for (int axis = 0; axis < 3; ++axis)
		if (world_point[axis] < R_FBT_VISUAL_WORLD_MIN ||
			world_point[axis] > R_FBT_VISUAL_WORLD_MAX)
			return false;
	return true;
}

static void R_PrepareFBTVisualFrame (const vrxr_frame_t *frame)
{
	vr_input_fbt_visual_snapshot_t snapshot;
	const float units_per_metre = V_VRUnitsPerMetre ();
	memset (&vr_fbt_visual_frame, 0, sizeof (vr_fbt_visual_frame));
	if (!vulkan_globals.stereo_active ||
		!VR_InputFBTCalibrationVisualSnapshot (frame, &snapshot))
		return;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		if (!(snapshot.role_mask & (1u << (unsigned int)role)))
			continue;
		if (!R_FBTVisualWorldPoint (snapshot.tracker_root_metres[role],
			snapshot.head_root_metres, snapshot.body_yaw_degrees,
			units_per_metre, vr_fbt_visual_frame.tracker_world[role]) ||
			!R_FBTVisualWorldPoint (snapshot.target_root_metres[role],
			snapshot.head_root_metres, snapshot.body_yaw_degrees,
			units_per_metre, vr_fbt_visual_frame.target_world[role]))
		{
			memset (&vr_fbt_visual_frame, 0, sizeof (vr_fbt_visual_frame));
			return;
		}
	}
	vr_fbt_visual_frame.role_mask = snapshot.role_mask;
}

static void R_InitializeStereoReference (const vrxr_frame_t *frame)
{
	const float (*head)[4] = frame->devices[0].matrix;
	if (!stereo_have_reference || frame->reference_changed)
	{
		for (int i = 0; i < 3; ++i)
			stereo_reference_position[i] = head[i][3];
		stereo_have_reference = true;
	}
}

void R_InvalidateStereoReference (void)
{
	// Keep invalidation across skipped frames until a valid rendered pose.
	stereo_have_reference = false;
	stereo_tracking_basis_valid = false;
	V_RebaseTrackedAim ();
}

void R_RestoreStereoView (void)
{
	stereo_tracking_basis_valid = false;
	if (!stereo_view_adjusted)
		return;
	VectorCopy (stereo_base_origin, r_refdef.vieworg);
	VectorCopy (stereo_base_angles, r_refdef.viewangles);
	stereo_view_adjusted = false;
}

static void R_XRVectorToWorld (const float vector[3], const vec3_t forward, const vec3_t right, const vec3_t up, vec3_t result)
{
	for (int i = 0; i < 3; ++i)
		result[i] = right[i] * vector[0] + up[i] * vector[1] - forward[i] * vector[2];
}

static qboolean R_VectorIsFinite (const vec3_t vector)
{
	return isfinite (vector[0]) && isfinite (vector[1]) && isfinite (vector[2]);
}

static qboolean R_NormalizeFiniteVector (vec3_t vector)
{
	double length;
	if (!R_VectorIsFinite (vector))
		return false;
	length = sqrt ((double)vector[0] * vector[0] + (double)vector[1] * vector[1] +
		(double)vector[2] * vector[2]);
	if (!isfinite (length) || length <= 0.0)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		vector[i] = (float)(vector[i] / length);
		if (!isfinite (vector[i]))
			return false;
	}
	return true;
}

static qboolean R_XRPoseIsFinite (const float matrix[3][4])
{
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 4; ++column)
			if (!isfinite (matrix[row][column]))
				return false;
	return true;
}

qboolean R_TrackedControllerBasis (int physical_hand, vec3_t origin, vec3_t right,
	vec3_t up, vec3_t forward)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const vrxr_device_t *head, *hand;
	float units_per_metre;
	vec3_t local, offset, pose_origin, pose_right, pose_up, pose_forward;

	if (origin)
		VectorCopy (vec3_origin, origin);
	if (right)
		VectorCopy (vec3_origin, right);
	if (up)
		VectorCopy (vec3_origin, up);
	if (forward)
		VectorCopy (vec3_origin, forward);
	if (!origin || !right || !up || !forward || physical_hand < 0 || physical_hand > 1 ||
		!stereo_tracking_basis_valid || !stereo_view_adjusted || !frame || !frame->should_render)
		return false;

	head = &frame->devices[0];
	hand = &frame->devices[physical_hand + 1];
	if (!head->valid || !head->tracked || head->kind != VRXR_DEVICE_HEAD || head->hand != -1 ||
		!hand->valid || !hand->tracked || hand->kind != VRXR_DEVICE_HAND || hand->hand != physical_hand ||
		!R_XRPoseIsFinite (head->matrix) || !R_XRPoseIsFinite (hand->matrix))
		return false;

	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		const float head_position = head->matrix[i][3];
		const float hand_position = hand->matrix[i][3];
		if (!isfinite (head_position) || !isfinite (hand_position) || !isfinite (r_refdef.vieworg[i]))
			return false;
		local[i] = (hand_position - head_position) * units_per_metre;
		if (!isfinite (local[i]))
			return false;
	}
	R_XRVectorToWorld (local, stereo_tracking_forward, stereo_tracking_right, stereo_tracking_up, offset);
	for (int i = 0; i < 3; ++i)
	{
		pose_origin[i] = r_refdef.vieworg[i] + offset[i];
		local[i] = hand->matrix[i][0];
		if (!isfinite (offset[i]) || !isfinite (pose_origin[i]) || !isfinite (local[i]))
			return false;
	}
	R_XRVectorToWorld (local, stereo_tracking_forward, stereo_tracking_right, stereo_tracking_up, pose_right);
	for (int i = 0; i < 3; ++i)
		local[i] = hand->matrix[i][1];
	R_XRVectorToWorld (local, stereo_tracking_forward, stereo_tracking_right, stereo_tracking_up, pose_up);
	for (int i = 0; i < 3; ++i)
		local[i] = -hand->matrix[i][2];
	R_XRVectorToWorld (local, stereo_tracking_forward, stereo_tracking_right, stereo_tracking_up, pose_forward);
	if (!R_NormalizeFiniteVector (pose_right) || !R_NormalizeFiniteVector (pose_up) ||
		!R_NormalizeFiniteVector (pose_forward))
		return false;
	VectorCopy (pose_origin, origin);
	VectorCopy (pose_right, right);
	VectorCopy (pose_up, up);
	VectorCopy (pose_forward, forward);
	return true;
}

qboolean R_TrackedControllerRay (int physical_hand, vec3_t origin, vec3_t direction)
{
	vec3_t right, up;
	if (origin)
		VectorCopy (vec3_origin, origin);
	if (direction)
		VectorCopy (vec3_origin, direction);
	if (!origin || !direction)
		return false;
	return R_TrackedControllerBasis (physical_hand, origin, right, up, direction);
}

qboolean R_TrackedHeadEyeHeight (float base_viewheight, float *out_height)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	float units_per_metre, height;
	const float (*head)[4];

	if (out_height)
		*out_height = 0;
	if (!out_height || !frame || !frame->should_render || !frame->devices[0].valid || !isfinite (base_viewheight))
		return false;
	head = frame->devices[0].matrix;
	for (int i = 0; i < 3; ++i)
		if (!isfinite (head[i][3]))
			return false;
	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0)
		return false;
	R_InitializeStereoReference (frame);
	if (frame->floor_referenced)
		height = V_VRFloorOffset () + head[1][3] * units_per_metre;
	else
		height = base_viewheight + V_VRFloorOffset () + 16.f +
			(head[1][3] - stereo_reference_position[1]) * units_per_metre;
	if (!isfinite (height))
		return false;
	*out_height = height;
	return true;
}

/* Return the renderer's canonical horizontal HMD offset from the player
 * body, in Quake world axes. CL_SendMove consumes the retained completed XR
 * frame before the next render starts, so initialize/rebase from that same
 * sample just as R_PrepareStereoFrame does. */
qboolean R_TrackedHeadBodyOffset (vec3_t world_offset)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const vrxr_device_t *head;
	const float (*matrix)[4];
	float presentation_yaw, units_per_metre;
	vec3_t local, forward, right, up, result;

	if (world_offset)
		VectorCopy (vec3_origin, world_offset);
	if (!world_offset || !frame || !frame->should_render || !frame->focused)
		return false;

	head = &frame->devices[0];
	if (!head->valid || !head->tracked || head->kind != VRXR_DEVICE_HEAD ||
		head->hand != -1 || !R_XRPoseIsFinite (head->matrix))
		return false;

	/* This is the same lazy reference initialization used by stereo rendering;
	 * frame->reference_changed rebases both paths from this retained sample. */
	R_InitializeStereoReference (frame);
	if (!stereo_have_reference || !V_TrackedPresentationYaw (&presentation_yaw))
		return false;
	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0.0f)
		return false;

	matrix = head->matrix;
	for (int axis = 0; axis < 3; ++axis)
		local[axis] = (matrix[axis][3] - stereo_reference_position[axis]) * units_per_metre;
	if (V_TrackedBodyOwnsRoomscale ())
		local[0] = local[2] = 0.0f;

	{
		vec3_t yaw_angles = {0.0f, presentation_yaw, 0.0f};
		AngleVectors (yaw_angles, forward, right, up);
	}
	R_XRVectorToWorld (local, forward, right, up, result);
	if (!R_VectorIsFinite (result))
		return false;
	result[2] = 0.0f;
	VectorCopy (result, world_offset);
	return true;
}

void R_PrepareStereoFrame (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	stereo_tracking_basis_valid = false;
	if (!frame)
	{
		memset (&vr_fbt_visual_frame, 0, sizeof (vr_fbt_visual_frame));
		stereo_have_reference = false;
		r_stereo_radius = 0;
		return;
	}
	VectorCopy (r_refdef.vieworg, stereo_base_origin);
	VectorCopy (r_refdef.viewangles, stereo_base_angles);
	stereo_view_adjusted = true;
	const vrxr_device_t *head_device = &frame->devices[0];
	const float (*head)[4] = head_device->matrix;
	const float units_per_metre = V_VRUnitsPerMetre ();
	vec3_t base_forward, base_right, base_up, local, offset;
	float tracking_yaw = stereo_base_angles[YAW];
	const qboolean resolved_view = V_ApplyTrackedView (r_refdef.viewangles, &tracking_yaw);
	AngleVectors (r_refdef.viewangles, base_forward, base_right, base_up);
	if (resolved_view)
	{
		// The visual view already contains the head. Map tracking through the
		// desired view basis * inverse(raw head), not through the head twice.
		vec3_t columns[3];
		for (int column = 0; column < 3; ++column)
			for (int i = 0; i < 3; ++i)
				columns[column][i] = base_right[i] * head[column][0] + base_up[i] * head[column][1] - base_forward[i] * head[column][2];
		VectorCopy (columns[0], base_right);
		VectorCopy (columns[1], base_up);
		VectorScale (columns[2], -1, base_forward);
	}
	if (frame->should_render && head_device->valid && head_device->tracked &&
		head_device->kind == VRXR_DEVICE_HEAD && head_device->hand == -1 &&
		R_XRPoseIsFinite (head) &&
		isfinite (units_per_metre) && units_per_metre > 0 &&
		R_VectorIsFinite (base_forward) && R_VectorIsFinite (base_right) && R_VectorIsFinite (base_up))
	{
		VectorCopy (base_forward, stereo_tracking_forward);
		VectorCopy (base_right, stereo_tracking_right);
		VectorCopy (base_up, stereo_tracking_up);
		stereo_tracking_basis_valid = true;
	}
	R_InitializeStereoReference (frame);
	for (int i = 0; i < 3; ++i)
		local[i] = (head[i][3] - stereo_reference_position[i]) * units_per_metre;
	if (V_TrackedBodyOwnsRoomscale ())
	{
		/* The command/prediction path now moves the player by this HMD step.
		 * Keep the inherited player eye at that collision-resolved body origin,
		 * including when a wall prevents the requested roomscale move. */
		local[0] = local[2] = 0;
	}
	float viewheight;
	if (V_TrackedPlayerBase (&viewheight))
	{
		if (frame->floor_referenced)
		{
			// Inherited standing-space formula replaces desktop eye height. Keep
			// node bias and stair smoothing already applied to the saved view base.
			local[1] = head[1][3] * units_per_metre;
			r_refdef.vieworg[2] += V_VRFloorOffset () - viewheight;
		}
		else
			// LOCAL has no known floor: retain relative height at the default offset.
			r_refdef.vieworg[2] += V_VRFloorOffset () + 16.f;
	}
	vec3_t yaw_angles = {0, tracking_yaw, 0};
	vec3_t yaw_forward, yaw_right, yaw_up;
	AngleVectors (yaw_angles, yaw_forward, yaw_right, yaw_up);
	R_XRVectorToWorld (local, yaw_forward, yaw_right, yaw_up, offset);
	VectorAdd (r_refdef.vieworg, offset, r_refdef.vieworg);
	for (int i = 0; i < 3; ++i) local[i] = -head[i][2];
	R_XRVectorToWorld (local, base_forward, base_right, base_up, stereo_forward);
	for (int i = 0; i < 3; ++i) local[i] = head[i][0];
	R_XRVectorToWorld (local, base_forward, base_right, base_up, stereo_right);
	for (int i = 0; i < 3; ++i) local[i] = head[i][1];
	R_XRVectorToWorld (local, base_forward, base_right, base_up, stereo_up);
	VectorAngles (stereo_forward, stereo_up, r_refdef.viewangles);
	stereo_bounds[0] = stereo_bounds[2] = FLT_MAX;
	stereo_bounds[1] = stereo_bounds[3] = -FLT_MAX;
	stereo_frustum_valid = true;
	r_stereo_radius = 0;
	for (int eye = 0; eye < 2; ++eye)
	{
		const vrxr_view_t *view = &frame->views[eye];
		for (int i = 0; i < 3; ++i)
			local[i] = (view->matrix[i][3] - head[i][3]) * units_per_metre;
		R_XRVectorToWorld (local, base_forward, base_right, base_up, offset);
		VectorCopy (offset, vulkan_globals.stereo_eye_offset[eye]);
		vulkan_globals.stereo_eye_offset[eye][3] = 0;
		VectorAdd (r_refdef.vieworg, offset, r_stereo_origins[eye]);
		r_stereo_radius = q_max (r_stereo_radius, VectorLength (offset));
		for (int corner = 0; corner < 4; ++corner)
		{
			const float x = (corner & 1) ? view->right : view->left;
			const float y = (corner & 2) ? view->up : view->down;
			vec3_t ray;
			for (int i = 0; i < 3; ++i)
				local[i] = view->matrix[i][0] * x + view->matrix[i][1] * y - view->matrix[i][2];
			for (int i = 0; i < 3; ++i)
				ray[i] = head[0][i] * local[0] + head[1][i] * local[1] + head[2][i] * local[2];
			if (ray[2] >= -0.001f)
				stereo_frustum_valid = false;
			else
			{
				const float tx = ray[0] / -ray[2], ty = ray[1] / -ray[2];
				stereo_bounds[0] = q_min (stereo_bounds[0], tx);
				stereo_bounds[1] = q_max (stereo_bounds[1], tx);
				stereo_bounds[2] = q_min (stereo_bounds[2], ty);
				stereo_bounds[3] = q_max (stereo_bounds[3], ty);
			}
		}
	}
	struct { float clip[2][16]; float offset[2][4]; } uniform;
	memcpy (uniform.clip, vulkan_globals.stereo_clip_from_center, sizeof (uniform.clip));
	memcpy (uniform.offset, vulkan_globals.stereo_eye_offset, sizeof (uniform.offset));
	VkBuffer buffer;
	void *data = R_UniformAllocate (sizeof (uniform), &buffer, &vulkan_globals.stereo_uniform_offset, &vulkan_globals.stereo_descriptor_set);
	memcpy (data, &uniform, sizeof (uniform));

	// Worldless stereo frames skip R_SetupViewBeforeMark, which normally prepares these.
	if (con_forcedup)
		R_SetupMatrices ();
	R_PrepareFBTVisualFrame (frame);
}

static void R_SetStereoFrustum (void)
{
	float normals[4][3];
	if (!stereo_frustum_valid || !VRXR_FrustumNormals (stereo_bounds, vpn, vright, vup, normals))
	{
		// A canted configuration extending behind the center view cannot be
		// enclosed by four forward planes. Keep visibility conservative.
		memset (frustum, 0, sizeof (frustum));
		for (int i = 0; i < 4; ++i) frustum[i].dist = -1;
		return;
	}
	for (int i = 0; i < 4; ++i)
	{
		VectorCopy (normals[i], frustum[i].normal);
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = q_min (DotProduct (r_stereo_origins[0], normals[i]), DotProduct (r_stereo_origins[1], normals[i])) - DIST_EPSILON;
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

static void R_SetupMatrices (void)
{
	// Projection matrix
	GL_FrustumMatrix (vulkan_globals.projection_matrix, DEG2RAD (vulkan_globals.stereo_active ? 90 : r_fovx),
		DEG2RAD (vulkan_globals.stereo_active ? 90 : r_fovy));

	if (vulkan_globals.stereo_active)
	{
		IdentityMatrix (vulkan_globals.view_matrix);
		for (int i = 0; i < 3; ++i)
		{
			vulkan_globals.view_matrix[i * 4] = stereo_right[i];
			vulkan_globals.view_matrix[i * 4 + 1] = stereo_up[i];
			vulkan_globals.view_matrix[i * 4 + 2] = -stereo_forward[i];
		}
		vulkan_globals.view_matrix[12] = -DotProduct (stereo_right, r_refdef.vieworg);
		vulkan_globals.view_matrix[13] = -DotProduct (stereo_up, r_refdef.vieworg);
		vulkan_globals.view_matrix[14] = DotProduct (stereo_forward, r_refdef.vieworg);
	}
	else
	{
		// View matrix
		float rotation_matrix[16];
		RotationMatrix (vulkan_globals.view_matrix, -M_PI / 2.0f, 1.0f, 0.0f, 0.0f);
		RotationMatrix (rotation_matrix, M_PI / 2.0f, 0.0f, 0.0f, 1.0f);
		MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);
		RotationMatrix (rotation_matrix, DEG2RAD (-r_refdef.viewangles[2]), 1.0f, 0.0f, 0.0f);
		MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);
		RotationMatrix (rotation_matrix, DEG2RAD (-r_refdef.viewangles[0]), 0.0f, 1.0f, 0.0f);
		MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);
		RotationMatrix (rotation_matrix, DEG2RAD (-r_refdef.viewangles[1]), 0.0f, 0.0f, 1.0f);
		MatrixMultiply (vulkan_globals.view_matrix, rotation_matrix);

		float translation_matrix[16];
		TranslationMatrix (translation_matrix, -r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);
		MatrixMultiply (vulkan_globals.view_matrix, translation_matrix);
	}

	// View projection matrix
	memcpy (vulkan_globals.view_projection_matrix, vulkan_globals.projection_matrix, 16 * sizeof (float));
	MatrixMultiply (vulkan_globals.view_projection_matrix, vulkan_globals.view_matrix);
}

/*
=============
R_SetupContext
=============
*/
vrect_t r_scene_vrect;

static void R_SceneViewport (cb_context_t *cbx, float min_depth)
{
	const VkViewport viewport = {r_scene_vrect.x, r_scene_vrect.y, r_scene_vrect.width, r_scene_vrect.height, min_depth, 1.0f};
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
}

static void R_SetupContext (cb_context_t *cbx)
{
	R_SceneViewport (cbx, 0.0f);
	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), vulkan_globals.view_projection_matrix);
}

static void R_PrepareDebugEntityInfo (void);

/*
===============
R_SetupViewBeforeMark
===============
*/
static void R_SetupViewBeforeMark (void *unused)
{
	// Map both edges so the scene viewport and AO use exactly the same pixels.
	const int view_y = vid.height - glheight + r_refdef.vrect.y;
	r_scene_vrect.x = r_refdef.vrect.x * vid.render_width / vid.width;
	r_scene_vrect.y = view_y * vid.render_height / vid.height;
	r_scene_vrect.width = (r_refdef.vrect.x + r_refdef.vrect.width) * vid.render_width / vid.width - r_scene_vrect.x;
	r_scene_vrect.height = (view_y + r_refdef.vrect.height) * vid.render_height / vid.height - r_scene_vrect.y;

	// must happen here: in indirect mode draw_world only depends on this task, latching
	// bmodel_instances_index any later would race the read in R_DrawIndirectBrushes
	if (indirect)
		R_ClearBModelInstanceClaims ();

	// Need to do those early because we now update dynamic light maps during R_MarkSurfaces
	if (!r_gpulightmapupdate.value)
		R_PushDlights ();
	R_AnimateLight ();

	// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);
	if (vulkan_globals.stereo_active)
	{
		VectorCopy (stereo_forward, vpn);
		VectorCopy (stereo_right, vright);
		VectorCopy (stereo_up, vup);
		r_scene_vrect = (vrect_t){0, 0, vid.render_width, vid.render_height, NULL};
	}

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	// johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;
	render_warp = false;

	if (r_waterwarp.value)
	{
		int contents = r_viewleaf->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			if (r_waterwarp.value == 1)
				render_warp = true;
			else
			{
				// variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.
				// what a mess!
				r_fovx = atan (tan (DEG2RAD (r_refdef.fov_x) / 2) * (0.97 + sin (cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
				r_fovy = atan (tan (DEG2RAD (r_refdef.fov_y) / 2) * (1.03 - sin (cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			}
		}
	}
	// johnfitz

	if (vulkan_globals.stereo_active)
		R_SetStereoFrustum ();
	else
		R_SetFrustum (r_fovx, r_fovy); // johnfitz -- use r_fov* vars
	R_SetupMatrices ();
	R_PrepareDebugEntityInfo ();

	// johnfitz -- cheat-protect some draw modes
	r_fullbright_cheatsafe = false;
	r_lightmap_cheatsafe = false;
	r_drawworld_cheatsafe = true;
	if (cl.maxclients == 1)
	{
		if (!r_drawworld.value)
			r_drawworld_cheatsafe = false;
		if (r_lightmap.value)
			r_lightmap_cheatsafe = true;
		else if (r_fullbright.value)
			r_fullbright_cheatsafe = true;
	}
	if (!cl.worldmodel->lightdata)
	{
		r_fullbright_cheatsafe = true;
		r_lightmap_cheatsafe = false;
	}
	// johnfitz
}

//==============================================================================
//
// RENDER VIEW
//
//==============================================================================

/*
=============
R_IsEntityTransparent
=============
*/
static qboolean R_IsEntityTransparent (entity_t *e, qboolean *opaque_with_transparent_water)
{
	qboolean transparent = ENTALPHA_DECODE (e->alpha) != 1;
	*opaque_with_transparent_water =
		(!transparent && e->model->type == mod_brush && e->model->used_specials & SURF_DRAWTURB && e->alpha == ENTALPHA_DEFAULT &&
		 ((e->model->used_specials & SURF_DRAWLAVA && GL_WaterAlphaForTextureType (TEXTYPE_LAVA) != 1) ||
		  (e->model->used_specials & SURF_DRAWTELE && GL_WaterAlphaForTextureType (TEXTYPE_TELE) != 1) ||
		  (e->model->used_specials & SURF_DRAWSLIME && GL_WaterAlphaForTextureType (TEXTYPE_SLIME) != 1) ||
		  (e->model->used_specials & SURF_DRAWWATER && GL_WaterAlphaForTextureType (TEXTYPE_WATER) != 1)));
	return transparent;
}

/*
=============
R_DrawEntitiesOnList

alphapass 0 for opaque, 1 for transparent overwater, 2 for transpatent underwater
=============
*/
void R_DrawEntitiesOnList (cb_context_t *cbx, int alphapass, int chain, qboolean use_tasks) // johnfitz -- added parameter
{
	int i = -1;

	if (!r_drawentities.value)
		return;

	int brushpolys = 0;
	int brushpasses = 0;
	int aliaspolys = 0;
	int aliaspasses = 0;

	const int		 total = !alphapass ? cl_numvisedicts : alphapass == 1 ? cl_numvisedicts_alpha_overwater : cl_numvisedicts_alpha_underwater;
	entity_t **const list = !alphapass ? cl_visedicts : alphapass == 1 ? cl_visedicts_alpha : cl_visedicts_alpha + cl_numvisedicts_alpha_overwater;

	R_BeginDebugUtilsLabel (cbx, alphapass ? "Entities Alpha Pass" : "Entities");
	// johnfitz -- sprites are not a special case
	while (true)
	{
		if (use_tasks)
			i = Atomic_IncrementUInt32 (&next_visedict);
		else
			i += 1;

		if (i >= total)
			break;

		entity_t *currententity = list[i];

		qboolean opaque_with_transparent_water;
		qboolean transparent = R_IsEntityTransparent (currententity, &opaque_with_transparent_water);

		// johnfitz -- if alphapass is true, draw only alpha entites this time
		// if alphapass is false, draw only nonalpha entities this time
		if (transparent != !!alphapass && !opaque_with_transparent_water)
			continue;

		// johnfitz -- chasecam
		if (currententity == &cl.entities[cl.viewentity])
			currententity->angles[0] *= 0.3;
		// johnfitz

		// spike -- this would be more efficient elsewhere, but its more correct here.
		if (currententity->eflags & EFLAGS_EXTERIORMODEL)
			continue;

		switch (currententity->model->type)
		{
		case mod_alias:
			R_DrawAliasModel (cbx, currententity, &aliaspolys);
			++aliaspasses;
			break;
		case mod_brush:
			R_DrawBrushModel (
				cbx, currententity, chain, &brushpolys, alphapass && R_UseAlphaSort (), !alphapass && opaque_with_transparent_water,
				alphapass && opaque_with_transparent_water);
			++brushpasses;
			break;
		case mod_sprite:
			R_DrawSpriteModel (cbx, currententity);
			break;
		}
	}
	R_EndDebugUtilsLabel (cbx);

	Atomic_AddUInt32 (&rs_brushpolys, brushpolys);
	Atomic_AddUInt32 (&rs_brushpasses, brushpasses);
	Atomic_AddUInt32 (&rs_aliaspolys, aliaspolys);
	Atomic_AddUInt32 (&rs_aliaspasses, aliaspasses);
}

/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
static void R_DrawVRCrosshair (cb_context_t *cbx);

void R_PrepareVRCrosshair (void)
{
	vr_crosshair_frame_t prepared;
	vec3_t forward, right, up, end, muzzle_cue;
	float size, alpha, depth, vertical_offset;

	memset (&vr_crosshair_frame, 0, sizeof (vr_crosshair_frame));
	/* Show the fixed point the controller must be moved onto while recentering
	 * the muzzle. Reuse the existing stereo crosshair snapshot/draw path. */
	if (vulkan_globals.stereo_active && glwidth > 0 && vid.width > 0 &&
		VR_WeaponCalibrationAdjustMuzzleCue (muzzle_cue))
	{
		memset (&prepared, 0, sizeof (prepared));
		prepared.mode = 1;
		prepared.valid = true;
		prepared.size_pixels = q_max (12.0f * (float)glwidth /
			(float)vid.width, 8.0f);
		prepared.alpha = 1.0f;
		VectorCopy (cl.viewent.origin, prepared.start);
		VectorCopy (muzzle_cue, prepared.impact);
		vr_crosshair_frame = prepared;
		return;
	}
	if (!vulkan_globals.stereo_active || VR_WeaponCalibrationAdjustActive () ||
		!cl.worldmodel || cls.signon != SIGNONS || cl.intermission ||
		!isfinite (vr_crosshair.value) ||
		(vr_crosshair.value != 1.0f && vr_crosshair.value != 2.0f) ||
		!isfinite (vr_crosshair_size.value) || !isfinite (vr_crosshair_alpha.value))
		return;

	prepared.mode = (int)vr_crosshair.value;
	size = CLAMP (0.0f, vr_crosshair_size.value, 32.0f);
	alpha = CLAMP (0.0f, vr_crosshair_alpha.value, 1.0f);
	if (size <= 0 || alpha <= 0 || glwidth <= 0 || vid.width <= 0)
		return;
	prepared.size_pixels = q_max (size * (float)glwidth / (float)vid.width, 8.0f);
	prepared.alpha = alpha;
	if (!isfinite (prepared.size_pixels))
		return;

	if (vr_aimmode.value == (float)VR_AIMMODE_CONTROLLER)
	{
		if (!VR_InputCrosshairAimRay (prepared.start, forward))
			return;
	}
	else
	{
		/* The target branch publishes gameplay aim through cl.viewangles. */
		VectorCopy (cl.viewent.origin, prepared.start);
		prepared.start[2] -= cl.stats[STAT_VIEWHEIGHT] - 10.0f;
		AngleVectors (cl.viewangles, forward, right, up);
	}
	for (int i = 0; i < 3; ++i)
		if (!isfinite (prepared.start[i]) || !isfinite (forward[i]))
			return;

	vertical_offset = isfinite (vr_crosshairy.value) ? vr_crosshairy.value : 0.0f;
	depth = isfinite (vr_crosshair_depth.value) ? vr_crosshair_depth.value : 0.0f;
	if (prepared.mode == 1 && depth > 0)
	{
		const float units_per_metre = V_VRUnitsPerMetre ();
		if (!isfinite (units_per_metre) || units_per_metre <= 0)
			return;
		VectorMA (prepared.start, depth * units_per_metre, forward, prepared.impact);
	}
	else
	{
		VectorMA (prepared.start, 4096.0f, forward, end);
		if (prepared.mode == 1)
			end[2] += vertical_offset;
		TraceLine (prepared.start, end, prepared.impact);
		if (prepared.mode == 2)
			prepared.impact[2] += vertical_offset * 10.0f;
	}
	for (int i = 0; i < 3; ++i)
		if (!isfinite (prepared.impact[i]))
			return;

	prepared.valid = true;
	vr_crosshair_frame = prepared;
}

static qboolean R_AkimboPairDrawReady (void)
{
	/* The main-thread frame setup publishes a complete immutable pair before
	 * draw tasks start. Do not consult mutable input admission from a worker. */
	if (!V_AkimboPairEntity (0) || !V_AkimboPairEntity (1))
		return false;
	for (int hand = 0; hand < 2; ++hand)
	{
		entity_t *entity = V_AkimboPairEntity (hand);
		aliashdr_t *geometry;
		lerpdata_t lerpdata;
		float matrix[16];
		if (!entity || !entity->model || entity->model->type != mod_alias ||
			entity->model->needload)
			return false;
		geometry = (aliashdr_t *)entity->model->extradata[PV_QUAKE1];
		if (!geometry || geometry->poseverttype != PV_QUAKE1)
			return false;
		R_SetupAliasFrame (entity, geometry, &lerpdata);
		R_GetEntityLerpedTransform (entity, lerpdata.origin, lerpdata.angles);
		if (R_AliasModelMatrix (entity, geometry, &lerpdata, matrix) < 0)
			return false;
	}
	return true;
}

void R_DrawViewModel (cb_context_t *cbx)
{
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
		return;

	const qboolean tracked_view = V_UseTrackedView ();
	if (scr_viewsize.value >= 130 && !tracked_view)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0 ||
		V_TrackedViewmodelShouldHide ())
		return;

	R_DrawVRCrosshair (cbx);

	entity_t *currententity = &cl.viewent;
	if (!currententity->model)
		return;

	// johnfitz -- this fixes a crash
	if (currententity->model->type != mod_alias)
		return;
	// johnfitz

	R_BeginDebugUtilsLabel (cbx, "View Model");

	// hack the depth range to prevent view model from poking into walls
	if (!tracked_view)
		R_SceneViewport (cbx, 0.7f);

	int aliaspolys = 0;
	if (R_AkimboPairDrawReady ())
	{
		for (int hand = 0; hand < 2; ++hand)
		{
			R_DrawAliasModel (cbx, V_AkimboPairEntity (hand), &aliaspolys);
			Atomic_IncrementUInt32 (&rs_aliaspasses);
		}
	}
	else
	{
		entity_t *held = V_HeldMeleeEntity ();
		if (held)
			currententity = held;
		R_DrawAliasModel (cbx, currententity, &aliaspolys);
		Atomic_IncrementUInt32 (&rs_aliaspasses);
	}
	Atomic_AddUInt32 (&rs_aliaspolys, aliaspolys);

	R_SceneViewport (cbx, 0.0f);

	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_FillDebugVertex
================
*/
static void R_FillDebugVertex (basicvertex_t *vertex, const vec3_t position, uint32_t color)
{
	VectorCopy (position, vertex->position);
	vertex->texcoord[0] = vertex->texcoord[1] = 0.0f;
	vertex->color[0] = (byte)(color >> 24);
	vertex->color[1] = (byte)(color >> 16);
	vertex->color[2] = (byte)(color >> 8);
	vertex->color[3] = (byte)color;
}

static void R_EmitVRCrosshairQuad (cb_context_t *cbx, const vec3_t corners[4], float alpha)
{
	static const int order[12] = {0, 1, 2, 0, 2, 3, 2, 1, 0, 3, 2, 0};
	VkBuffer vertex_buffer;
	VkDeviceSize vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (countof (order) * sizeof (*vertices),
		&vertex_buffer, &vertex_buffer_offset);
	const uint32_t color = 0xff000000u | (uint32_t)(CLAMP (0.0f, alpha, 1.0f) * 255.0f + 0.5f);

	for (int i = 0; i < countof (order); ++i)
		R_FillDebugVertex (&vertices[i], corners[order[i]], color);

	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_NOTEX_BLEND);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, 16 * sizeof (float), vulkan_globals.view_projection_matrix);
	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vkCmdDraw (cbx->cb, countof (order), 1, 0, 0);
}

static float R_VRCrosshairHalfExtent (float depth, float pixels, float fov, float viewport)
{
	if (!isfinite (depth) || depth <= 0 || !isfinite (pixels) || pixels <= 0 ||
		!isfinite (fov) || fov <= 0 || fov >= 179 || !isfinite (viewport) || viewport <= 0)
		return 0;
	/* Center-FOV sizing is approximate after XR applies asymmetric per-eye clip correction. */
	return depth * tanf (DEG2RAD (fov) * 0.5f) * pixels / viewport;
}

static qboolean R_VRCrosshairWorldPathReady (void)
{
	vec3_t delta;
	const float width = (float)r_scene_vrect.width;
	const float height = (float)r_scene_vrect.height;

	if (!vulkan_globals.stereo_active || !vr_crosshair_frame.valid || width <= 0 || height <= 0 ||
		!r_drawviewmodel.value || !r_drawentities.value || chase_active.value ||
		(scr_viewsize.value >= 130 && !V_UseTrackedView ()) ||
		(cl.items & IT_INVISIBILITY) || cl.stats[STAT_HEALTH] <= 0 ||
		V_TrackedViewmodelShouldHide ())
		return false;

	if (vr_crosshair_frame.mode == 1)
	{
		VectorSubtract (vr_crosshair_frame.impact, r_origin, delta);
		const float depth = DotProduct (delta, vpn);
		return R_VRCrosshairHalfExtent (depth, vr_crosshair_frame.size_pixels, r_fovx, width) > 0 &&
			R_VRCrosshairHalfExtent (depth, vr_crosshair_frame.size_pixels, r_fovy, height) > 0;
	}
	if (vr_crosshair_frame.mode == 2)
	{
		VectorSubtract (vr_crosshair_frame.impact, vr_crosshair_frame.start, delta);
		if (VectorLength (delta) <= 0)
			return false;
		VectorSubtract (vr_crosshair_frame.impact, r_origin, delta);
		return R_VRCrosshairHalfExtent (DotProduct (delta, vpn),
			vr_crosshair_frame.size_pixels * 2.0f, r_fovy, height) > 0;
	}
	return false;
}

static void R_DrawVRCrosshair (cb_context_t *cbx)
{
	vec3_t corners[4], direction, side, from_camera;
	float half_width, half_height;
	const float width = (float)r_scene_vrect.width;
	const float height = (float)r_scene_vrect.height;

	if (!R_VRCrosshairWorldPathReady ())
		return;

	if (vr_crosshair_frame.mode == 1)
	{
		VectorSubtract (vr_crosshair_frame.impact, r_origin, from_camera);
		half_width = R_VRCrosshairHalfExtent (DotProduct (from_camera, vpn),
			vr_crosshair_frame.size_pixels, r_fovx, width);
		half_height = R_VRCrosshairHalfExtent (DotProduct (from_camera, vpn),
			vr_crosshair_frame.size_pixels, r_fovy, height);
		if (half_width <= 0 || half_height <= 0)
			return;
		for (int i = 0; i < 3; ++i)
		{
			corners[0][i] = vr_crosshair_frame.impact[i] - vright[i] * half_width + vup[i] * half_height;
			corners[1][i] = vr_crosshair_frame.impact[i] + vright[i] * half_width + vup[i] * half_height;
			corners[2][i] = vr_crosshair_frame.impact[i] + vright[i] * half_width - vup[i] * half_height;
			corners[3][i] = vr_crosshair_frame.impact[i] - vright[i] * half_width - vup[i] * half_height;
		}
	}
	else if (vr_crosshair_frame.mode == 2)
	{
		VectorSubtract (vr_crosshair_frame.impact, vr_crosshair_frame.start, direction);
		if (VectorNormalize (direction) <= 0)
			return;
		CrossProduct (direction, vpn, side);
		if (VectorNormalize (side) < 0.001f)
			VectorCopy (vright, side);
		VectorSubtract (vr_crosshair_frame.start, r_origin, from_camera);
		half_width = R_VRCrosshairHalfExtent (DotProduct (from_camera, vpn),
			vr_crosshair_frame.size_pixels * 2.0f, r_fovy, height);
		VectorSubtract (vr_crosshair_frame.impact, r_origin, from_camera);
		half_height = R_VRCrosshairHalfExtent (DotProduct (from_camera, vpn),
			vr_crosshair_frame.size_pixels * 2.0f, r_fovy, height);
		for (int i = 0; i < 3; ++i)
		{
			corners[0][i] = vr_crosshair_frame.start[i] + side[i] * half_width;
			corners[1][i] = vr_crosshair_frame.impact[i] + side[i] * half_height;
			corners[2][i] = vr_crosshair_frame.impact[i] - side[i] * half_height;
			corners[3][i] = vr_crosshair_frame.start[i] - side[i] * half_width;
		}
	}
	else
		return;

	R_EmitVRCrosshairQuad (cbx, corners, vr_crosshair_frame.alpha);
}

/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
static void R_EmitWirePoint (cb_context_t *cbx, const vec3_t origin, uint32_t color)
{
	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);
	const float	   size = 8.0f;
	vec3_t		   positions[6];

	VectorCopy (origin, positions[0]);
	VectorCopy (origin, positions[1]);
	VectorCopy (origin, positions[2]);
	VectorCopy (origin, positions[3]);
	VectorCopy (origin, positions[4]);
	VectorCopy (origin, positions[5]);
	positions[0][0] -= size;
	positions[1][0] += size;
	positions[2][1] -= size;
	positions[3][1] += size;
	positions[4][2] -= size;
	positions[5][2] += size;

	for (int i = 0; i < countof (positions); i++)
		R_FillDebugVertex (&vertices[i], positions[i], color);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
static void
R_EmitWireBox (cb_context_t *cbx, const vec3_t mins, const vec3_t maxs, VkBuffer box_index_buffer, VkDeviceSize box_index_buffer_offset, uint32_t color)
{
	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (8 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

	for (int i = 0; i < 8; ++i)
	{
		vertices[i].position[0] = ((i % 2) < 1) ? mins[0] : maxs[0];
		vertices[i].position[1] = ((i % 4) < 2) ? mins[1] : maxs[1];
		vertices[i].position[2] = ((i % 8) < 4) ? mins[2] : maxs[2];
		vertices[i].texcoord[0] = vertices[i].texcoord[1] = 0.0f;
		vertices[i].color[0] = (byte)(color >> 24);
		vertices[i].color[1] = (byte)(color >> 16);
		vertices[i].color[2] = (byte)(color >> 8);
		vertices[i].color[3] = (byte)color;
	}

	vulkan_globals.vk_cmd_bind_index_buffer (cbx->cb, box_index_buffer, box_index_buffer_offset, VK_INDEX_TYPE_UINT16);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw_indexed (cbx->cb, 24, 1, 0, 0, 0);
}

static uint16_t box_indices[24] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 4, 1, 5, 2, 6, 3, 7, 0, 2, 1, 3, 4, 6, 5, 7};

/*
================
R_RayVsBox
================
*/
static qboolean R_RayVsBox (const vec3_t org, const vec3_t rcpdelta, const vec3_t mins, const vec3_t maxs, float *frac)
{
	float enter, exit;

	if (frac)
		*frac = 1.0f;

	enter = 0.0f;
	exit = 1.0f;

	for (int i = 0; i < 3; i++)
	{
		const float t0 = (mins[i] - org[i]) * rcpdelta[i];
		const float t1 = (maxs[i] - org[i]) * rcpdelta[i];
		const float tmin = q_min (t0, t1);
		const float tmax = q_max (t0, t1);
		enter = q_max (enter, tmin);
		exit = q_min (exit, tmax);
	}

	if (enter > exit)
		return false;

	if (frac)
		*frac = enter;

	return true;
}

/*
================
R_EmitArrow
================
*/
static void R_EmitArrow (cb_context_t *cbx, const vec3_t from, const vec3_t to, uint32_t color)
{
	VkBuffer	   vertex_buffer;
	VkDeviceSize   vertex_buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);
	float		   frac, len;
	vec3_t		   center, dir, side, tmp;
	vec3_t		   positions[6];

	VectorCopy (from, positions[0]);
	VectorCopy (to, positions[1]);

	VectorSubtract (to, from, dir);
	len = VectorNormalize (dir);
	if (len < 1e-2f)
	{
		VectorCopy (vup, dir);
		VectorCopy (vright, side);
	}
	else
	{
		VectorSubtract (from, r_origin, tmp);
		CrossProduct (dir, tmp, side);
		VectorNormalize (side);
	}

	frac = (float)(realtime - floor (realtime));
	for (int i = 0; i < 3; i++)
		center[i] = from[i] + (to[i] - from[i]) * frac;

	VectorMA (center, 8.0f, side, tmp);
	VectorMA (tmp, -8.0f, dir, tmp);
	VectorCopy (tmp, positions[2]);
	VectorCopy (center, positions[3]);

	VectorMA (tmp, -16.0f, side, tmp);
	VectorCopy (tmp, positions[4]);
	VectorCopy (center, positions[5]);

	for (int i = 0; i < countof (positions); i++)
		R_FillDebugVertex (&vertices[i], positions[i], color);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
R_EdictCenter
================
*/
static void R_EdictCenter (const edict_t *ed, vec3_t center)
{
	VectorCopy (ed->v.origin, center);
	if (!VectorCompare (ed->v.mins, ed->v.maxs))
	{
		VectorMA (center, 0.5f, ed->v.mins, center);
		VectorMA (center, 0.5f, ed->v.maxs, center);
	}
}

/*
================
R_EmitEdictLink
================
*/
static void R_EmitEdictLink (cb_context_t *cbx, const edict_t *from, const edict_t *to, showbboxflags_t flags)
{
	vec3_t vec_from, vec_to;

	if (!flags)
		return;

	R_EdictCenter (from, vec_from);
	R_EdictCenter (to, vec_to);

	if (flags == SHOWBBOX_LINK_BOTH)
	{
		VkBuffer	   vertex_buffer;
		VkDeviceSize   vertex_buffer_offset;
		basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (2 * sizeof (basicvertex_t), &vertex_buffer, &vertex_buffer_offset);

		R_FillDebugVertex (&vertices[0], vec_from, 0x7f7f3f7fu);
		R_FillDebugVertex (&vertices[1], vec_to, 0x7f7f3f7fu);

		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &vertex_buffer, &vertex_buffer_offset);
		vulkan_globals.vk_cmd_draw (cbx->cb, 2, 1, 0, 0);
	}
	else if (flags == SHOWBBOX_LINK_OUTGOING)
		R_EmitArrow (cbx, vec_from, vec_to, 0x7f7f3f3fu);
	else if (flags == SHOWBBOX_LINK_INCOMING)
		R_EmitArrow (cbx, vec_to, vec_from, 0x7f3f3f7fu);
}

/*
================
R_ShowBoundingBoxesFilter

r_showbboxes_filter "artifact,=trigger_secret"
================
 */
char	*r_showbboxes_filter_strings = NULL;
qboolean r_showbboxes_filter_byindex = false;

static qboolean R_ShowBoundingBoxesFilter (edict_t *ed)
{
	char		entnum[16] = "";
	const char *classname = NULL;
	const char *filter_p = r_showbboxes_filter_strings;

	if (!r_showbboxes_filter_strings || !r_showbboxes_filter_strings[0])
		return true;

	if (r_showbboxes_filter_byindex)
		q_snprintf (entnum, sizeof (entnum), "%d", NUM_FOR_EDICT (ed));

	if (ed->v.classname)
		classname = PR_GetString (ed->v.classname);

	for (filter_p = r_showbboxes_filter_strings; *filter_p; filter_p += strlen (filter_p) + 1)
	{
		if (*filter_p == '#')
		{
			if (!strcmp (entnum, filter_p + 1))
				return true;
			continue;
		}

		if (!classname)
			continue;

		if (*filter_p == '=')
		{
			if (!strcmp (classname, filter_p + 1))
				return true;
			continue;
		}

		if (strstr (classname, filter_p) != NULL)
			return true;
	}

	return false;
}

static edict_t **bbox_edicts = NULL;
edict_t		   **bbox_linked = NULL;
static edict_t	*bbox_focused = NULL;

void R_ClearDebugEntityInfo (void)
{
	VEC_CLEAR (bbox_edicts);
	VEC_CLEAR (bbox_linked);
	bbox_focused = NULL;
}

/*
================
R_AddHighlightedEntity
================
*/
static void R_AddHighlightedEntity (edict_t *ed, showbboxflags_t flags)
{
	if (ed->showbboxframe != r_framecount)
	{
		ed->showbboxframe = r_framecount;
		ed->showbboxflags = SHOWBBOX_LINK_NONE;
		VEC_PUSH (bbox_edicts, ed);
	}

	if (!(ed->showbboxflags & flags) && (int)r_showbboxes_links.value & flags)
	{
		VEC_PUSH (bbox_linked, ed);
		ed->showbboxflags |= flags;
	}
}

/*
================
R_PrepareDebugEntityInfo

Find entities and links used by r_showbboxes/r_showfields. This runs before the
parallel draw tasks so the 3D debug pass and GUI overlay read stable state.
================
*/
static void R_PrepareDebugEntityInfo (void)
{
	extern edict_t *sv_player;
	vec3_t			mins, maxs, rcpdelta;
	edict_t		   *ed;
	byte		   *pvs;
	int				i, j, mode;
	float			dist, bestdist;

	R_ClearDebugEntityInfo ();

	mode = abs ((int)r_showbboxes.value);
	if ((!mode && !r_showfields.value) || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	SDL_LockMutex (draw_qcvm_mutex);
	PR_SwitchQCVM (&sv.qcvm);

	if (mode >= 2 || mode == 0)
	{
		vec3_t org;
		VectorAdd (sv_player->v.origin, sv_player->v.view_ofs, org);
		pvs = SV_FatPVS (org, qcvm->worldmodel);
	}
	else
	{
		pvs = NULL;
	}

	for (i = 0; i < 3; i++)
		rcpdelta[i] = 1.0f / (gl_farclip.value * vpn[i]);

	bestdist = FLT_MAX;
	for (i = 1, ed = NEXT_EDICT (qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT (ed))
	{
		if (ed == sv_player || ed->free)
			continue;

		if (r_showbboxes_think.value && (ed->v.nextthink <= 0) == (r_showbboxes_think.value > 0))
			continue;

		if (r_showbboxes_health.value && (ed->v.health <= 0) == (r_showbboxes_health.value > 0))
			continue;

		for (j = 0; j < 3; j++)
		{
			const float extend = VectorCompare (ed->v.mins, ed->v.maxs) ? 8.0f : 0.0f;
			mins[j] = ed->v.origin[j] + ed->v.mins[j] - extend;
			maxs[j] = ed->v.origin[j] + ed->v.maxs[j] + extend;
		}

		if (R_CullBox (mins, maxs))
			continue;

		if (!R_ShowBoundingBoxesFilter (ed))
			continue;

		if (pvs)
		{
			qboolean inpvs = ed->num_leafs ? SV_EdictInPVS (ed, pvs) : SV_BoxInPVS (ed->v.absmin, ed->v.absmax, pvs, qcvm->worldmodel);
			if (!inpvs)
				continue;
		}

		if (R_RayVsBox (r_origin, rcpdelta, mins, maxs, &dist) && dist > 0.0f && dist < bestdist)
		{
			bestdist = dist;
			bbox_focused = ed;
		}

		R_AddHighlightedEntity (ed, SHOWBBOX_LINK_NONE);
	}

	if (bbox_focused)
		VEC_PUSH (bbox_linked, bbox_focused);

	if (bbox_focused && r_showbboxes_links.value)
	{
		if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_OUTGOING)
		{
			for (i = 0; i < qcvm->numentityfields; i++)
			{
				eval_t *val = (eval_t *)((char *)&bbox_focused->v + qcvm->entityfieldofs[i]);
				if (qcvm->entityfieldofs[i] == offsetof (entvars_t, chain) || !val->edict)
					continue;
				ed = PROG_TO_EDICT (val->edict);
				if (ed == bbox_focused || ed->free || ed == sv_player)
					continue;
				R_AddHighlightedEntity (ed, SHOWBBOX_LINK_OUTGOING);
			}
		}

		if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_INCOMING || r_showbboxes_targets.value)
		{
			const char *focus_target = PR_GetString (bbox_focused->v.target);
			const char *focus_targetname = PR_GetString (bbox_focused->v.targetname);

			for (i = 1, ed = NEXT_EDICT (qcvm->edicts); i < qcvm->num_edicts; i++, ed = NEXT_EDICT (ed))
			{
				if (ed == sv_player || ed->free || ed == bbox_focused)
					continue;

				if (r_showbboxes_targets.value && (*focus_target || *focus_targetname))
				{
					const char *target = PR_GetString (ed->v.target);
					const char *targetname = PR_GetString (ed->v.targetname);

					if (*focus_targetname && !strcmp (focus_targetname, target))
						R_AddHighlightedEntity (ed, SHOWBBOX_LINK_INCOMING);
					if (*focus_target && !strcmp (focus_target, targetname))
						R_AddHighlightedEntity (ed, SHOWBBOX_LINK_OUTGOING);
				}

				if ((int)r_showbboxes_links.value & SHOWBBOX_LINK_INCOMING)
				{
					for (j = 0; j < qcvm->numentityfields; j++)
					{
						eval_t *val = (eval_t *)((char *)&ed->v + qcvm->entityfieldofs[j]);
						if (qcvm->entityfieldofs[j] == offsetof (entvars_t, chain) || !val->edict)
							continue;
						if (PROG_TO_EDICT (val->edict) == bbox_focused)
							R_AddHighlightedEntity (ed, SHOWBBOX_LINK_INCOMING);
					}
				}
			}
		}
	}

	PR_SwitchQCVM (NULL);
	SDL_UnlockMutex (draw_qcvm_mutex);
}

/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
static void R_ShowBoundingBoxes (cb_context_t *cbx)
{
	vec3_t	 mins, maxs, center;
	edict_t *ed;
	int		 i, j, pass, mode;
	uint32_t color;

	mode = abs ((int)r_showbboxes.value);
	if ((!mode && !r_showfields.value) || VEC_SIZE (bbox_edicts) == 0)
		return;

	R_BeginDebugUtilsLabel (cbx, "show bboxes");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.debug_lines_pipeline[cbx->pipeline_variant]);

	VkBuffer	 box_index_buffer;
	VkDeviceSize box_index_buffer_offset;
	uint16_t	*indices = (uint16_t *)R_IndexAllocate (24 * sizeof (uint16_t), &box_index_buffer, &box_index_buffer_offset);
	memcpy (indices, box_indices, 24 * sizeof (uint16_t));

	if (bbox_focused && r_showbboxes_links.value)
		for (j = 0; j < (int)VEC_SIZE (bbox_linked); j++)
			R_EmitEdictLink (cbx, bbox_focused, bbox_linked[j], bbox_linked[j]->showbboxflags);

	for (pass = 0; pass < 2; pass++) // two passes (0 = lines, 1 = text) to avoid switching pipelines for every edict and so that the text is on top
	{
		if (pass == 1 && r_showbboxes.value < 0)
			continue;
		for (i = 0; i < (int)VEC_SIZE (bbox_edicts); i++)
		{
			ed = bbox_edicts[i];
			if (ed == bbox_focused)
				color = 0xffffffffu;
			else if (ed->showbboxflags)
				color = 0xaaaaaaaau;
			else if (r_showbboxes.value > 0.0f)
			{
				int modelindex = (int)ed->v.modelindex;
				color = 0x7f800080u;
				if (modelindex >= 0 && modelindex < MAX_MODELS && sv.models[modelindex])
				{
					switch (sv.models[modelindex]->type)
					{
					case mod_brush:
						color = 0x7fff8080u;
						break;
					case mod_alias:
						color = 0x7f408080u;
						break;
					case mod_sprite:
						color = 0x7f4040ffu;
						break;
					default:
						break;
					}
				}
				if (ed->v.health > 0)
					color = 0x7f0000ffu;
			}
			else if (r_showbboxes.value < 0.0f)
				color = 0x7fffffffu;
			else
				color = 0x5f7f7f7fu;

			if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
			{
				// point entity
				if (pass == 0)
				{
					R_EmitWirePoint (cbx, ed->v.origin, color);
				}
				else
				{
					VectorCopy (ed->v.origin, center);
					center[2] += 16; // show a bit above
				}
			}
			else
			{
				// box entity
				VectorAdd (ed->v.mins, ed->v.origin, mins);
				VectorAdd (ed->v.maxs, ed->v.origin, maxs);
				if (pass == 0)
				{
					R_EmitWireBox (cbx, mins, maxs, box_index_buffer, box_index_buffer_offset, color);
				}
				else
				{
					VectorAdd (mins, maxs, center);
					for (int axis = 0; axis < 3; axis++)
						center[axis] /= 2;
				}
			}

			if (pass == 1)
			{
				char text[16];
				q_snprintf (text, sizeof (text), "%i", i);
				for (char *c = text; *c; c++)
					*c |= 0x80; // the lines are already white so gold is more legible
				Draw_String_3D (cbx, center, 8, text);
			}
		}
	}

	R_EndDebugUtilsLabel (cbx);
}

/*
===============
R_ShowPointFile
===============
*/
static void R_ShowPointFile (cb_context_t *cbx)
{
	size_t i;

	if (VEC_SIZE (r_pointfile) == 0)
		return;

	R_BeginDebugUtilsLabel (cbx, "pointfile");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.debug_lines_pipeline[cbx->pipeline_variant]);
	for (i = 1; i < VEC_SIZE (r_pointfile); i++)
		R_EmitArrow (cbx, r_pointfile[i - 1], r_pointfile[i], 0xff3f3f7fu);
	R_EndDebugUtilsLabel (cbx);
}

static void R_ShowViewModelTris (cb_context_t *cbx)
{
	entity_t *currententity = &cl.viewent;
	if (r_drawentities.value && r_drawviewmodel.value && !chase_active.value && cl.stats[STAT_HEALTH] > 0 && !(cl.items & IT_INVISIBILITY) && currententity->model &&
		currententity->model->type == mod_alias && (V_UseTrackedView () || scr_viewsize.value < 130))
	{
		if (R_AkimboPairDrawReady ())
			for (int hand = 0; hand < 2; ++hand)
				R_DrawAliasModel_ShowTris (cbx, V_AkimboPairEntity (hand));
		else
		{
			entity_t *held = V_HeldMeleeEntity ();
			R_DrawAliasModel_ShowTris (cbx, held ? held : currententity);
		}
	}
}

/*
================
R_ShowTris -- johnfitz
================
 */
void R_ShowTris (cb_context_t *cbx)
{
	extern cvar_t r_particles;
	int			  i;

	if (r_showtris.value < 1 || r_showtris.value > 2 || cl.maxclients > 1 || !vulkan_globals.non_solid_fill)
		return;

	R_BeginDebugUtilsLabel (cbx, "show tris");
	if (indirect)
		R_DrawIndirectBrushes_ShowTris (cbx);
	else if (r_drawworld.value)
		R_DrawWorld_ShowTris (cbx);

	if (r_drawentities.value)
	{
		for (i = 0; i < cl_numvisedicts; i++)
		{
			entity_t *currententity = cl_visedicts[i];

			if (currententity == &cl.entities[cl.viewentity]) // chasecam
				currententity->angles[0] *= 0.3;

			switch (currententity->model->type)
			{
			case mod_brush:
				R_DrawBrushModel_ShowTris (cbx, currententity);
				break;
			case mod_alias:
				R_DrawAliasModel_ShowTris (cbx, currententity);
				break;
			case mod_sprite:
				R_DrawSpriteModel_ShowTris (cbx, currententity);
				break;
			default:
				break;
			}
		}

		// In view-anchored wheel mode the actual viewmodel is deferred with the
		// wheel, so defer its diagnostic wireframe too.
		if (!VR_WeaponMenu_UsesForegroundDepth ())
			R_ShowViewModelTris (cbx);
	}

	if (r_particles.value)
	{
		R_DrawParticles_ShowTris (cbx);
		PScript_DrawParticles_ShowTris (cbx);
	}

	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_ShowSkeletons
================
*/
static void R_ShowSkeletons (cb_context_t *cbx)
{
	if (!r_showskel.value || cl.maxclients > 1 || !r_drawentities.value)
		return;

	R_BeginDebugUtilsLabel (cbx, "show skeletons");
	for (int i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *currententity = cl_visedicts[i];
		if (currententity->model->type == mod_alias)
			R_DrawAliasModel_ShowSkel (cbx, currententity);
	}
	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_DrawWorldTask
================
*/
static void R_DrawWorldChunk (int context, int index, void *use_tasks, r_world_draw_filter_t filter, qboolean depth_only, qboolean skip_draw)
{
	cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[context][index];
	cbx->depth_only = depth_only;
	R_SetupContext (cbx);
	if (skip_draw)
		return;
	Fog_EnableGFog (cbx);
	if (indirect)
		R_DrawIndirectBrushesFiltered (cbx, false, false, false, use_tasks ? index : -1, filter);
	else
		R_DrawWorldFiltered (cbx, index, filter);
}

static void R_DrawWorldTask (int index, void *use_tasks)
{
	if (vulkan_globals.openxr_fragment_density_map_active)
	{
		const qboolean coarse = vulkan_globals.openxr_fragment_density_frame_active;
		// A lost gaze uses the runtime's full-rate profile. Keep the compiled
		// pass topology and record its secondary buffers, but draw the world
		// once in the ordinary scene instead of replaying its depth.
		R_DrawWorldChunk (SCBX_DENSITY_WORLD, index, use_tasks, R_WORLD_DRAW_FOVEATION_ELIGIBLE, false, !coarse);
		R_DrawWorldChunk (SCBX_WORLD_DEPTH_REPLAY, index, use_tasks, R_WORLD_DRAW_FOVEATION_ELIGIBLE, true, !coarse);
		// When coarse, replay completes before protected color can depth-test.
		R_DrawWorldChunk (SCBX_WORLD, index, use_tasks, coarse ? R_WORLD_DRAW_FOVEATION_PROTECTED : R_WORLD_DRAW_ALL, false, false);
	}
	else
		R_DrawWorldChunk (SCBX_WORLD, index, use_tasks, R_WORLD_DRAW_ALL, false, false);
}

/*
================
R_DrawSkyTask
================
*/
static void R_DrawSkyTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_SKY];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx);
	R_DrawWorld_Water (cbx, false); // draw opaque water before sky (more likely to occlude)
	Sky_DrawSky (cbx);
}

/*
================
R_DrawWaterTask
================
*/
static void R_DrawWaterTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_WATER];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx);
	R_DrawWorld_Water (cbx, true); // transparent worldmodel water only

	if (R_UseMBOIT ())
	{
		cbx = vulkan_globals.secondary_cb_contexts[SCBX_MBOIT_COMPOSITE_WATER];
		R_SetupContext (cbx);
		Fog_EnableGFog (cbx);
		R_DrawWorld_Water (cbx, true);
	}
}

/*
================
R_SortAlphaEntitiesTask
================
*/
static void R_SortAlphaEntitiesTask (void *unused)
{
	typedef struct
	{
		int		 visedict;
		unsigned sortkey;
	} transp_sort;
	const qboolean sort_alpha = R_UseAlphaSort ();
	cl_numvisedicts_alpha_overwater = cl_numvisedicts_alpha_underwater = 0;
	TEMP_ALLOC_COND (transp_sort, edicts, cl_numvisedicts * 2, sort_alpha);
	int sort_bins[3][128];
	if (sort_alpha)
		memset (sort_bins, 0, sizeof (sort_bins));
	for (int i = 0; i < cl_numvisedicts; ++i)
	{
		entity_t *currententity = cl_visedicts[i];

		qboolean opaque_with_transparent_water;
		qboolean transparent = R_IsEntityTransparent (currententity, &opaque_with_transparent_water);

		if (!transparent && !opaque_with_transparent_water)
			continue;
		if (currententity->eflags & EFLAGS_EXTERIORMODEL)
			continue;
		// box culling here is not safe (R_DrawAliasModel updates lerp information)

		if (!sort_alpha)
		{
			cl_visedicts_alpha[cl_numvisedicts_alpha_overwater++] = cl_visedicts[i];
			continue;
		}

		vec3_t		center;
		vec3_t		avatar_mins, avatar_maxs;
		const r_vrik_prepared_palette_t *avatar = R_VRIKRenderLookup (currententity);
		const qboolean avatar_bounds = avatar && avatar->alternate_avatar && avatar->model &&
			avatar->model->type == mod_alias &&
			avatar->geometry == (const aliashdr_t *)avatar->model->extradata[PV_MD5] &&
			R_PreparedAvatarWorldBounds (currententity, avatar, avatar_mins, avatar_maxs);
		const float scalefactor = ENTSCALE_DECODE (currententity->netstate.scale);
		float		dist_squared = 0;
		for (int j = 0; j < 3; ++j)
		{
			const float mins = avatar_bounds ? avatar_mins[j] :
				currententity->origin[j] + scalefactor * currententity->model->mins[j];
			const float maxs = avatar_bounds ? avatar_maxs[j] :
				currententity->origin[j] + scalefactor * currententity->model->maxs[j];
			center[j] = (mins + maxs) / 2;
			const float dist = q_max (0.0f, q_max (mins - r_refdef.vieworg[j], r_refdef.vieworg[j] - maxs));
			dist_squared += dist * dist;
		}
		int contents;
		if (currententity->contentscache < 0 && memcmp (currententity->contentscache_origin, center, sizeof (vec3_t)) == 0)
		{
			contents = currententity->contentscache;
		}
		else
		{
			currententity->contentscache = contents = Mod_PointInLeaf (center, cl.worldmodel)->contents;
			memcpy (currententity->contentscache_origin, center, sizeof (vec3_t));
		}
		const qboolean underwater = contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA;
		const unsigned dist = sqrtf (dist_squared) * 2.0f;
		const unsigned sortkey = !underwater << 20 | q_min (dist, (1 << 20) - 1);
		sort_bins[2][(sortkey >> 14)] += 1;
		sort_bins[1][(sortkey >> 7) % 128] += 1;
		sort_bins[0][(sortkey >> 0) % 128] += 1;
		transp_sort *const edict = &edicts[cl_numvisedicts_alpha_overwater + cl_numvisedicts_alpha_underwater];
		edict->visedict = i;
		edict->sortkey = sortkey;
		if (underwater)
			++cl_numvisedicts_alpha_underwater;
		else
			++cl_numvisedicts_alpha_overwater;
	}

	if (!sort_alpha)
		return;

	const int highest = cl_numvisedicts_alpha_underwater + cl_numvisedicts_alpha_overwater - 1;
	for (int pass = 0; pass < 3; ++pass)
	{
		transp_sort *from = pass % 2 ? edicts + cl_numvisedicts : edicts;
		transp_sort *to = pass % 2 ? edicts : edicts + cl_numvisedicts;
		for (int i = 1; i < 128; ++i)
			sort_bins[pass][i] += sort_bins[pass][i - 1];
		for (int i = highest; i >= 0; --i)
		{
			int key = (from[i].sortkey >> 7 * pass) % 128;
			sort_bins[pass][key] -= 1;
			if (pass < 2)
				to[sort_bins[pass][key]] = from[i];
			else
				cl_visedicts_alpha[highest - sort_bins[pass][key]] = cl_visedicts[from[i].visedict];
		}
	}

	TEMP_FREE (edicts);
}

/*
================
R_DrawEntitiesTask
================
*/
static void R_DrawEntitiesTask (int index, void *use_tasks)
{
	cb_context_t *cbx = &vulkan_globals.secondary_cb_contexts[SCBX_ENTITIES][index];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx); // johnfitz
	R_DrawEntitiesOnList (cbx, false, index + chain_model_0, use_tasks ? true : false);
}

/*
================
R_DrawAlphaEntitiesTask
================
*/
static void R_DrawAlphaEntitiesTask (int index, void *use_tasks)
{
	const int	   contents = r_viewleaf->contents;
	const qboolean underwater = R_UseAlphaSort () && (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA);
	for (int i = use_tasks ? index : 0; i <= (use_tasks ? index : 1); ++i)
	{
		cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[i ? SCBX_ALPHA_ENTITIES : SCBX_ALPHA_ENTITIES_ACROSS_WATER];
		R_SetupContext (cbx);
		Fog_EnableGFog (cbx);
		R_DrawEntitiesOnList (cbx, underwater ? 1 + i : 2 - i, i ? chain_alpha_model : chain_alpha_model_across_water, false);

		if (R_UseMBOIT ())
		{
			cbx = vulkan_globals.secondary_cb_contexts[i ? SCBX_MBOIT_COMPOSITE_ALPHA_ENTITIES : SCBX_MBOIT_COMPOSITE_ALPHA_ENTITIES_ACROSS_WATER];
			R_SetupContext (cbx);
			Fog_EnableGFog (cbx);
			R_DrawEntitiesOnList (cbx, underwater ? 1 + i : 2 - i, i ? chain_alpha_model : chain_alpha_model_across_water, false);
		}
	}
}

/*
================
R_DrawParticlesTask
================
*/
static void R_DrawCoopNametags (cb_context_t *cbx);
static void R_DrawCoopFilledSilhouettes (cb_context_t *cbx);
static void R_DrawCoopPlayerOutlines (cb_context_t *cbx);
static void R_DrawCoopWheelSelectedSilhouette (cb_context_t *cbx);

static void R_DrawParticlesTask (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_PARTICLES];
	R_SetupContext (cbx);
	Fog_EnableGFog (cbx); // johnfitz
	R_DrawParticles (cbx);

	if (R_UseMBOIT ())
	{
		// MBOIT needs all transparent geometry a second time in the composite pass
		cb_context_t *composite_cbx = vulkan_globals.secondary_cb_contexts[SCBX_MBOIT_COMPOSITE_PARTICLES];
		R_SetupContext (composite_cbx);
		Fog_EnableGFog (composite_cbx);
		R_DrawParticles (composite_cbx);
	}
	cb_context_t *fte_blend_cbx = vulkan_globals.secondary_cb_contexts[SCBX_FTE_PARTICLES_BLEND];
	R_SceneViewport (fte_blend_cbx, 0.0f);
	PScript_DrawParticles (fte_blend_cbx);
	R_SceneViewport (fte_blend_cbx, 0.0f);
	R_DrawCoopFilledSilhouettes (fte_blend_cbx);
	if (!vulkan_globals.stereo_active)
		R_DrawCoopNametags (fte_blend_cbx);
}

static void R_CoopPlayerShirtColor (int player, float boost, float min_peak, vec3_t color)
{
	const byte *rgb = (const byte *)&d_8to24table[((cl.scores[player].colors >> 4) & 15) * 16 + 8];
	for (int axis = 0; axis < 3; ++axis)
		color[axis] = rgb[axis] / 255.0f;
	const float peak = q_max (color[0], q_max (color[1], color[2]));
	const float scale = peak > 0.0f ? q_max (boost, min_peak / peak) : 0.0f;
	for (int axis = 0; axis < 3; ++axis)
		color[axis] = peak > 0.0f ? color[axis] * scale : min_peak;
}

static qboolean R_CoopOverlayPlayerValid (int entity_index)
{
	return entity_index > 0 && entity_index <= cl.maxclients &&
		entity_index <= MAX_SCOREBOARD && entity_index < cl.num_entities &&
		entity_index != cl.viewentity && cl.scores[entity_index - 1].name[0] &&
		cl.entities[entity_index].model && cl.entities[entity_index].model->type == mod_alias;
}

static void R_DrawCoopFilledSilhouettes (cb_context_t *cbx)
{
	if (vulkan_globals.stereo_active || !Sbar_IsShowingScores () ||
		cl.gametype != GAME_COOP || !cl.scores || !r_drawentities.value)
		return;
	R_BeginDebugUtilsLabel (cbx, "co-op filled silhouettes");
	for (int entity_index = 1; entity_index <= cl.maxclients &&
		entity_index <= MAX_SCOREBOARD && entity_index < cl.num_entities; ++entity_index)
	{
		if (!R_CoopOverlayPlayerValid (entity_index))
			continue;
		vec3_t color;
		R_CoopPlayerShirtColor (entity_index - 1, 1.5f, 0.45f, color);
		R_DrawAliasCoopOverlay (cbx, &cl.entities[entity_index], color, 0.35f, 1.04f, false);
	}
	R_EndDebugUtilsLabel (cbx);
}

static void R_DrawCoopWheelSelectedSilhouette (cb_context_t *cbx)
{
	if (!vulkan_globals.stereo_active || cl.gametype != GAME_COOP ||
		!cl.scores || !r_drawentities.value)
		return;
	const int player = VR_WeaponMenu_HoveredCoopPlayer ();
	if (player < 0 || !R_CoopOverlayPlayerValid (player + 1))
		return;
	vec3_t color;
	R_CoopPlayerShirtColor (player, 1.8f, 0.5f, color);
	R_BeginDebugUtilsLabel (cbx, "co-op wheel-selected silhouette");
	R_DrawAliasCoopOverlay (cbx, &cl.entities[player + 1], color, 0.52f, 1.04f, false);
	R_EndDebugUtilsLabel (cbx);
}

static void R_DrawCoopPlayerOutlines (cb_context_t *cbx)
{
	if (!vulkan_globals.stereo_active || !Sbar_IsShowingScores () ||
		cl.gametype != GAME_COOP || !cl.scores || !r_drawentities.value)
		return;
	int eligible = 0;
	for (int entity_index = 1; entity_index <= cl.maxclients &&
		entity_index <= MAX_SCOREBOARD && entity_index < cl.num_entities; ++entity_index)
		eligible += R_CoopOverlayPlayerValid (entity_index);
	if (!eligible)
		return;

	/* Clear only stencil. Multiview broadcasts the one-layer clear rect to
	 * both eyes. Unique references preserve each overlapping player's ring. */
	const VkClearAttachment clear = {
		.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
		.clearValue.depthStencil = {.depth = 0.0f, .stencil = 0},
	};
	const VkClearRect rect = {
		.rect = {{0, 0}, {vid.render_width, vid.render_height}},
		.baseArrayLayer = 0,
		.layerCount = 1,
	};
	vkCmdClearAttachments (cbx->cb, 1, &clear, 1, &rect);
	R_BeginDebugUtilsLabel (cbx, "co-op player outlines");
	for (int entity_index = 1; entity_index <= cl.maxclients &&
		entity_index <= MAX_SCOREBOARD && entity_index < cl.num_entities; ++entity_index)
	{
		if (!R_CoopOverlayPlayerValid (entity_index))
			continue;
		vec3_t color;
		R_CoopPlayerShirtColor (entity_index - 1, 1.0f, 0.0f, color);
		vkCmdSetStencilReference (cbx->cb, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)entity_index);
		R_DrawAliasCoopOverlay (cbx, &cl.entities[entity_index], color, 0.7f, 1.05f, true);
	}
	R_EndDebugUtilsLabel (cbx);
}

static void R_DrawFBTCalibrationVisuals (cb_context_t *cbx)
{
	static const char *const labels[VR_FBT_ROLE_COUNT] = {
		"hip", "L foot", "R foot"
	};
	if (!cbx || !vulkan_globals.stereo_active || !vr_fbt_visual_frame.role_mask)
		return;
	R_BeginDebugUtilsLabel (cbx, "FBT calibration targets");
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		vec3_t label_origin;
		char label[12];
		if (!(vr_fbt_visual_frame.role_mask & (1u << (unsigned int)role)))
			continue;
		R_EmitWirePoint (cbx, vr_fbt_visual_frame.tracker_world[role], 0xff40dfffu);
		R_EmitWirePoint (cbx, vr_fbt_visual_frame.target_world[role], 0xffff7f40u);
		R_EmitArrow (cbx, vr_fbt_visual_frame.tracker_world[role],
			vr_fbt_visual_frame.target_world[role], 0xffffbf40u);
		VectorCopy (vr_fbt_visual_frame.target_world[role], label_origin);
		label_origin[2] += 12.0f;
		q_strlcpy (label, labels[role], sizeof (label));
		for (char *character = label; *character; ++character)
			*character |= 0x80;
		Draw_String_3D (cbx, label_origin, 8.0f, label);
	}
	R_EndDebugUtilsLabel (cbx);
}

/* The source co-op tags follow translucent effects and test scene depth.
 * Reuse the existing late particle subpass; its stereo basic vertex shader
 * projects the same world glyphs independently into both OpenXR views. */
static void R_DrawCoopNametags (cb_context_t *cbx)
{
	extern gltexture_t *char_texture;
	const vec3_t black = {0, 0, 0};
	if (!cl_coop_nametags.value || cl.gametype != GAME_COOP || !r_drawentities.value ||
		!cl.scores || !char_texture)
		return;

	R_BeginDebugUtilsLabel (cbx, "co-op nametags");
	for (int entity_index = 1; entity_index <= cl.maxclients &&
		entity_index <= MAX_SCOREBOARD && entity_index < cl.num_entities; ++entity_index)
	{
		const int player = entity_index - 1;
		entity_t *entity = &cl.entities[entity_index];
		vec3_t origin, shadow_origin, color;
		char label[MAX_SCOREBOARDNAME + 5];
		float scale;
		if (entity_index == cl.viewentity || !entity->model ||
			entity->model->type != mod_alias || !cl.scores[player].name[0])
			continue;

		scale = ENTSCALE_DECODE (entity->netstate.scale);
		if (!isfinite (scale) || scale <= 0 || !isfinite (entity->model->maxs[2]))
			continue;
		VectorCopy (entity->origin, origin);
		origin[2] += entity->model->maxs[2] * scale + 7.0f;
		if (!isfinite (origin[0]) || !isfinite (origin[1]) || !isfinite (origin[2]))
			continue;
		if (Voice_SpeakerTalking (player))
			q_snprintf (label, sizeof (label), "((%s))", cl.scores[player].name);
		else
			q_strlcpy (label, cl.scores[player].name, sizeof (label));

		R_CoopPlayerShirtColor (player, 1.65f, 0.55f, color);

		VectorMA (origin, 0.35f, vright, shadow_origin);
		VectorMA (shadow_origin, -0.35f, vup, shadow_origin);
		Draw_String_3DColor (cbx, shadow_origin, vright, vup, 3.0f, label, black, 0.65f);
		Draw_String_3DColor (cbx, origin, vright, vup, 3.0f, label, color, 1.0f);
	}
	R_EndDebugUtilsLabel (cbx);
}

/*
================
R_DrawViewModelTask
================
*/
static void R_DrawViewModelTask (void *unused)
{
	const qboolean foreground_wheel = VR_WeaponMenu_UsesForegroundDepth ();
	const qboolean late_vr_weapon = vulkan_globals.stereo_active &&
		(foreground_wheel || VR_WeaponMenu_IsOpenVR () ||
			(cl.gametype == GAME_COOP && Sbar_IsShowingScores ()));
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_VIEW_MODEL];
	R_SetupContext (cbx);
	if (!late_vr_weapon)
		R_DrawViewModel (cbx); // ordinary stereo and desktop retain vkQuake's depth/transparency order
	if (!vulkan_globals.stereo_active && !foreground_wheel)
	{
		/* The wheel is scene geometry, independent of the held weapon's hide gates. */
		const int wheel_polys = VR_WeaponMenu_DrawModels (cbx);
		if (wheel_polys)
		{
			Atomic_AddUInt32 (&rs_aliaspolys, wheel_polys);
			Atomic_IncrementUInt32 (&rs_aliaspasses);
		}
	}
	R_ShowTris (cbx);	   // johnfitz
	R_ShowSkeletons (cbx);
	R_ShowBoundingBoxes (cbx); // johnfitz
	R_ShowPointFile (cbx);
	R_DrawFBTCalibrationVisuals (cbx);

	if (vulkan_globals.stereo_active)
	{
		cbx = vulkan_globals.secondary_cb_contexts[SCBX_WHEEL_FOREGROUND];
		R_SetupContext (cbx);
		R_DrawCoopPlayerOutlines (cbx);
		R_DrawCoopWheelSelectedSilhouette (cbx);
		R_DrawCoopNametags (cbx);
		if (foreground_wheel)
		{
			const VkClearAttachment depth_clear = {
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.clearValue.depthStencil = {.depth = 0.0f, .stencil = 0},
			};
			const VkClearRect clear_rect = {
				.rect = {{0, 0}, {vid.render_width, vid.render_height}},
				.baseArrayLayer = 0,
				.layerCount = 1,
			};
			vkCmdClearAttachments (cbx->cb, 1, &depth_clear, 1, &clear_rect);

		}
		/* Through-wall overlays precede the wheel, which precedes the held
		 * weapon. A playspace wheel keeps scene depth; the foreground wheel
		 * clears depth only for its existing presentation mode. */
		const int wheel_polys = VR_WeaponMenu_DrawModels (cbx);
		if (wheel_polys)
		{
			Atomic_AddUInt32 (&rs_aliaspolys, wheel_polys);
			Atomic_IncrementUInt32 (&rs_aliaspasses);
		}
		if (late_vr_weapon)
			R_DrawViewModel (cbx);
		if (foreground_wheel && r_showtris.value >= 1 && r_showtris.value <= 2 &&
			cl.maxclients <= 1 && vulkan_globals.non_solid_fill)
		{
			R_BeginDebugUtilsLabel (cbx, "show viewmodel tris");
			R_ShowViewModelTris (cbx);
			R_EndDebugUtilsLabel (cbx);
		}
	}
}

/*
================
R_PrintStats
================
*/
static void R_PrintStats (qboolean draw_stats_ready)
{
	// johnfitz -- modified scr_speeds output
	const double cpu_ms = (double)rs_cputime_us / 1000.0;
	const double gpu_ms = (double)rs_gputime_us / 1000.0;
	const double gpu_wait_ms = (double)rs_gpuwaittime_us / 1000.0;
	rs_display_numlines = 0;
	if (r_pos.value)
		Con_Printf (
			"x %i y %i z %i (pitch %i yaw %i roll %i)\n", (int)cl.entities[cl.viewentity].origin[0], (int)cl.entities[cl.viewentity].origin[1],
			(int)cl.entities[cl.viewentity].origin[2], (int)cl.viewangles[PITCH], (int)cl.viewangles[YAW], (int)cl.viewangles[ROLL]);
	else if (scr_speeds.value)
	{
		q_snprintf (rs_display_lines[0], sizeof (rs_display_lines[0]), "cpu%6.2f gpu%6.2f wait%6.2f ms", cpu_ms, gpu_ms, gpu_wait_ms);
		if (scr_speeds.value == 3 || !draw_stats_ready)
			rs_display_numlines = 1;
		else
		{
			double lms = r_gpulightmapupdate.value
						 ? (double)Atomic_LoadUInt32 (&rs_dynamiclightmaps) / (LMBLOCK_HEIGHT / LM_CULL_BLOCK_H * LMBLOCK_WIDTH / LM_CULL_BLOCK_W)
						 : Atomic_LoadUInt32 (&rs_dynamiclightmaps);
			if (scr_speeds.value == 2)
			{
				q_snprintf (
					rs_display_lines[1], sizeof (rs_display_lines[1]), "%4u/%u wpoly %4u/%u epoly", rs_brushpolys, rs_brushpasses, rs_aliaspolys, rs_aliaspasses);
				q_snprintf (rs_display_lines[2], sizeof (rs_display_lines[2]), "%5.3g lmap %4u skypoly", lms, rs_skypolys);
				q_snprintf (rs_display_lines[3], sizeof (rs_display_lines[3]), "BLAS %u build %u refit %u reuse",
					Atomic_LoadUInt32 (&rs_blas_builds), Atomic_LoadUInt32 (&rs_blas_refits), Atomic_LoadUInt32 (&rs_blas_pose_reuses));
				rs_display_numlines = 4;
			}
			else
			{
				q_snprintf (rs_display_lines[1], sizeof (rs_display_lines[1]), "%4u wpoly %4u epoly %5.3g lmap", rs_brushpolys, rs_aliaspolys, lms);
				rs_display_numlines = 2;
			}
		}
	}
	// johnfitz
}

/*
================
R_RenderView
================
*/
void R_RenderView (
	qboolean use_tasks, task_handle_t begin_rendering_task, task_handle_t setup_frame_task, task_handle_t draw_done_task, task_handle_t draw_gui_task)
{
	static qboolean stats_ready;
	static qboolean draw_stats_ready;

	indirect = r_indirect.value && indirect_ready && r_gpulightmapupdate.value && (!scr_speeds.value || scr_speeds.value == 3);

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	if (scr_speeds.value)
		rs_frame_starttime = Sys_DoubleTime ();

	if (use_tasks && (r_pos.value || stats_ready))
		R_PrintStats (draw_stats_ready); // stats and frame times of the last completed frame

	if (scr_speeds.value && scr_speeds.value != 3)
	{
		// johnfitz -- rendering statistics
		Atomic_StoreUInt32 (&rs_brushpolys, 0u);
		Atomic_StoreUInt32 (&rs_aliaspolys, 0u);
		Atomic_StoreUInt32 (&rs_skypolys, 0u);
		Atomic_StoreUInt32 (&rs_particles, 0u);
		Atomic_StoreUInt32 (&rs_fogpolys, 0u);
		Atomic_StoreUInt32 (&rs_dynamiclightmaps, 0u);
		Atomic_StoreUInt32 (&rs_aliaspasses, 0u);
		Atomic_StoreUInt32 (&rs_brushpasses, 0u);
		Atomic_StoreUInt32 (&rs_blas_builds, 0u);
		Atomic_StoreUInt32 (&rs_blas_refits, 0u);
		Atomic_StoreUInt32 (&rs_blas_pose_reuses, 0u);
	}
	stats_ready = scr_speeds.value != 0;
	draw_stats_ready = scr_speeds.value != 0 && scr_speeds.value != 3;

	if (use_tasks)
	{
		task_handle_t before_mark = Task_AllocateAndAssignFunc (R_SetupViewBeforeMark, NULL, 0);
		Task_AddDependency (setup_frame_task, before_mark);
		if (draw_gui_task != INVALID_TASK_HANDLE)
			Task_AddDependency (before_mark, draw_gui_task);

		task_handle_t store_efrags = INVALID_TASK_HANDLE;
		task_handle_t cull_surfaces = INVALID_TASK_HANDLE;
		task_handle_t chain_surfaces = INVALID_TASK_HANDLE;
		R_MarkSurfaces (use_tasks, before_mark, &store_efrags, &cull_surfaces, &chain_surfaces);
		/* Model/BLAS setup in efrag collection may touch the same player models.
		 * Prepare one immutable tracked palette after that work and the frame-slot
		 * fence, before either visible or ray-shadow consumers record commands. */
		task_handle_t prepare_vrik_palettes_task = Task_AllocateAndAssignFunc (GL_PrepareVRIKRenderTask, NULL, 0);
		Task_AddDependency (store_efrags, prepare_vrik_palettes_task);
		Task_AddDependency (begin_rendering_task, prepare_vrik_palettes_task);

		task_handle_t update_warp_textures = Task_AllocateAndAssignFunc ((task_func_t)R_UpdateWarpTextures, NULL, 0);
		Task_AddDependency (cull_surfaces, update_warp_textures);
		if (store_efrags != cull_surfaces)
			Task_AddDependency (store_efrags, update_warp_textures);
		Task_AddDependency (begin_rendering_task, update_warp_textures);
		Task_AddDependency (update_warp_textures, draw_done_task);

		task_handle_t draw_world_task = Task_AllocateAndAssignIndexedFunc (R_DrawWorldTask, NUM_WORLD_CBX, &use_tasks, sizeof (use_tasks));
		if (indirect)
			Task_AddDependency (before_mark, draw_world_task);
		else
			Task_AddDependency (chain_surfaces, draw_world_task);
		Task_AddDependency (begin_rendering_task, draw_world_task);
		Task_AddDependency (draw_world_task, draw_done_task);

		task_handle_t sort_transparents = Task_AllocateAndAssignFunc (R_SortAlphaEntitiesTask, NULL, 0);
		Task_AddDependency (store_efrags, sort_transparents);
		Task_AddDependency (prepare_vrik_palettes_task, sort_transparents);

		task_handle_t draw_sky_task = Task_AllocateAndAssignFunc (R_DrawSkyTask, NULL, 0);
		Task_AddDependency (store_efrags, draw_sky_task);
		Task_AddDependency (chain_surfaces, draw_sky_task);
		Task_AddDependency (begin_rendering_task, draw_sky_task);
		Task_AddDependency (draw_sky_task, draw_done_task);

		task_handle_t draw_water_task = Task_AllocateAndAssignFunc (R_DrawWaterTask, NULL, 0);
		Task_AddDependency (chain_surfaces, draw_water_task);
		Task_AddDependency (begin_rendering_task, draw_water_task);
		Task_AddDependency (draw_water_task, draw_done_task);

		task_handle_t draw_view_model_task = Task_AllocateAndAssignFunc (R_DrawViewModelTask, NULL, 0);
		Task_AddDependency (before_mark, draw_view_model_task);
		Task_AddDependency (begin_rendering_task, draw_view_model_task);
		Task_AddDependency (prepare_vrik_palettes_task, draw_view_model_task);
		Task_AddDependency (draw_view_model_task, draw_done_task);

		Atomic_StoreUInt32 (&next_visedict, 0u);
		if (R_SSAOEnabled ())
		{
			task_handle_t draw_ssao_task = Task_AllocateAndAssignFunc (R_DrawSSAOTask, NULL, 0);
			Task_AddDependency (before_mark, draw_ssao_task);
			Task_AddDependency (begin_rendering_task, draw_ssao_task);
			Task_AddDependency (draw_ssao_task, draw_done_task);
			Task_Submit (draw_ssao_task);
		}

		task_handle_t draw_entities_task = Task_AllocateAndAssignIndexedFunc (R_DrawEntitiesTask, NUM_ENTITIES_CBX, &use_tasks, sizeof (use_tasks));
		Task_AddDependency (store_efrags, draw_entities_task);
		Task_AddDependency (begin_rendering_task, draw_entities_task);
		Task_AddDependency (prepare_vrik_palettes_task, draw_entities_task);

		task_handle_t draw_alpha_entities_task = Task_AllocateAndAssignIndexedFunc (R_DrawAlphaEntitiesTask, 2, &use_tasks, sizeof (use_tasks));
		Task_AddDependency (sort_transparents, draw_alpha_entities_task);
		Task_AddDependency (begin_rendering_task, draw_alpha_entities_task);
		Task_AddDependency (prepare_vrik_palettes_task, draw_alpha_entities_task);

		// dlights queued by last frame's deferred effect spawns; must run before
		// anything reads cl_dlights and before layout refills the queues
		task_handle_t flush_dlights_task = Task_AllocateAndAssignFunc (PScript_FlushDlightsTask, NULL, 0);
		Task_AddDependency (flush_dlights_task, draw_view_model_task);
		Task_AddDependency (flush_dlights_task, draw_entities_task);
		Task_AddDependency (flush_dlights_task, draw_alpha_entities_task);

		task_handle_t update_particles_setup_task = Task_AllocateAndAssignFunc (PScript_UpdateParticlesSetupTask, NULL, 0);
		Task_AddDependency (before_mark, update_particles_setup_task);

		task_handle_t update_particles_task = Task_AllocateAndAssignIndexedFunc (PScript_UpdateParticlesTask, Tasks_NumWorkers (), NULL, 0);
		Task_AddDependency (update_particles_setup_task, update_particles_task);

		// layout is the first task that writes the double buffered vertex/index buffers, it
		// must wait for begin_rendering so the GPU is done reading them from two frames ago
		task_handle_t layout_particles_task = Task_AllocateAndAssignFunc (PScript_LayoutParticlesTask, NULL, 0);
		Task_AddDependency (update_particles_task, layout_particles_task);
		Task_AddDependency (begin_rendering_task, layout_particles_task);
		Task_AddDependency (flush_dlights_task, layout_particles_task);

		task_handle_t emit_particles_task = Task_AllocateAndAssignIndexedFunc (PScript_EmitParticlesTask, Tasks_NumWorkers (), NULL, 0);
		Task_AddDependency (layout_particles_task, emit_particles_task);

		task_handle_t draw_particles_task = Task_AllocateAndAssignFunc (R_DrawParticlesTask, NULL, 0);
		Task_AddDependency (before_mark, draw_particles_task);
		Task_AddDependency (emit_particles_task, draw_particles_task);
		Task_AddDependency (begin_rendering_task, draw_particles_task);
		Task_AddDependency (prepare_vrik_palettes_task, draw_particles_task);
		Task_AddDependency (draw_particles_task, draw_done_task);

		task_handle_t build_tlas_task = Task_AllocateAndAssignFunc (R_BuildTopLevelAccelerationStructure, NULL, 0);
		Task_AddDependency (store_efrags, build_tlas_task);
		Task_AddDependency (begin_rendering_task, build_tlas_task);
		Task_AddDependency (prepare_vrik_palettes_task, build_tlas_task);
		Task_AddDependency (build_tlas_task, draw_done_task);

		task_handle_t update_lightmaps_task = Task_AllocateAndAssignFunc (R_UpdateLightmapsAndIndirect, NULL, 0);
		Task_AddDependency (cull_surfaces, update_lightmaps_task);
		Task_AddDependency (draw_entities_task, update_lightmaps_task);
		Task_AddDependency (draw_alpha_entities_task, update_lightmaps_task);
		Task_AddDependency (flush_dlights_task, update_lightmaps_task);
		Task_AddDependency (update_lightmaps_task, draw_done_task);

		if (r_showtris.value)
		{
			if (!indirect)
				Task_AddDependency (chain_surfaces, draw_view_model_task);

			Task_AddDependency (draw_entities_task, draw_view_model_task);		 // not dependent, but mutually exclusive
			Task_AddDependency (draw_alpha_entities_task, draw_view_model_task); // not dependent, but mutually exclusive

			Task_AddDependency (draw_particles_task, draw_view_model_task); // only scriptable particles are dependent
		}

		task_handle_t tasks[] = {
			before_mark,		   store_efrags,		  prepare_vrik_palettes_task, update_warp_textures, draw_world_task,		  sort_transparents,  draw_sky_task,
			draw_water_task,	   draw_view_model_task,  draw_entities_task,	draw_alpha_entities_task, flush_dlights_task, update_particles_setup_task,
			update_particles_task, layout_particles_task, emit_particles_task,	draw_particles_task,	  build_tlas_task,	  update_lightmaps_task};
		Tasks_Submit ((sizeof (tasks) / sizeof (task_handle_t)), tasks);
		if (cull_surfaces != chain_surfaces)
		{
			Task_Submit (cull_surfaces);
			Task_Submit (chain_surfaces);
		}
	}
	else
	{
		R_SetupViewBeforeMark (NULL);
		R_MarkSurfaces (use_tasks, INVALID_TASK_HANDLE, NULL, NULL, NULL); // johnfitz -- create texture chains from PVS
		GL_PrepareVRIKRenderTask (NULL);
		R_UpdateWarpTextures (NULL);
		R_DrawWorldTask (0, NULL);
		R_DrawSkyTask (NULL);
		R_DrawWaterTask (NULL);
		R_DrawEntitiesTask (0, NULL);
		if (R_SSAOEnabled ())
			R_DrawSSAOTask (NULL);
		R_SortAlphaEntitiesTask (NULL);
		R_DrawAlphaEntitiesTask (0, NULL);
		PScript_FlushDlightsTask (NULL); // no-op here (spawns run on the main thread), but keeps the queues drained across mode switches
		PScript_UpdateParticlesSetupTask (NULL);
		for (int pi = 0; pi < q_max (Tasks_NumWorkers (), 1); pi++)
			PScript_UpdateParticlesTask (pi, NULL);
		PScript_LayoutParticlesTask (NULL);
		for (int pi = 0; pi < q_max (Tasks_NumWorkers (), 1); pi++)
			PScript_EmitParticlesTask (pi, NULL);
		R_DrawParticlesTask (NULL);
		R_DrawViewModelTask (NULL);
		if (r_gpulightmapupdate.value)
		{
			R_BuildTopLevelAccelerationStructure (NULL);
			R_UpdateLightmapsAndIndirect (NULL);
		}
		R_PrintStats (draw_stats_ready);
	}
}
