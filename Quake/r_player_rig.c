/* Code-defined VR body rigs for MDL player models.
 *
 * A vertex-animated MDL has no skeleton, so VRIK cannot pose it. For a player
 * mesh listed in vr_player_rigs.h (matched by VR_WeaponFamilySignature) this
 * module builds an MD5 avatar at load time from the mod's own model file:
 *
 * - The rerelease Ranger skeleton supplies the joint hierarchy and bind
 *   orientations, so the existing bind-relative Ranger retargeting applies.
 * - The table's joint positions, placed on one standing frame of the model,
 *   give the bone lengths. Each bone is turned from its direction in that frame
 *   to the Ranger bind direction, which moves the mesh into the Ranger bind
 *   pose.
 * - Vertex weights come from the distance to each bone segment in the
 *   reference frame, blended with the adjacent bone near joints.
 *
 * The result is registered as an internal custom avatar; no file is written
 * and nothing is networked. */
#include "quakedef.h"
#include "r_player_rig.h"
#include "custom_avatar.h"
#include "r_avatar.h"
#include "vr_weapon_calibration.h"
#include "vr_player_rigs.h"

#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>

#define PR_MAX_MODELS 32
#define PR_JOINTS (2 + MD5_VRIK_GUN) /* SuperRoot, Root and the 19 body joints */
#define PR_BODY MD5_VRIK_GUN

enum { PR_UNKNOWN = 0, PR_NONE, PR_READY };

typedef struct pr_model_s
{
	const qmodel_t *model;
	char name[MAX_QPATH];
	int state;
	int avatar_id;
	const vr_player_rig_t *rig;
	int numframes;
	short *canonical; /* NULL: identity */
} pr_model_t;

static pr_model_t pr_models[PR_MAX_MODELS];
static int pr_model_count;
static char pr_gamedir[MAX_OSPATH];

static const char *const pr_joint_names[PR_JOINTS] = {
	"SuperRoot", "Root", "Hip", "Spine1", "Spine2", "Neck", "Head",
	"Shoulder_L", "UpperArm_L", "LowerArm_L", "Hand_L",
	"Shoulder_R", "UpperArm_R", "LowerArm_R", "Hand_R",
	"UpperLeg_L", "LowerLeg_L", "Foot_L", "UpperLeg_R", "LowerLeg_R", "Foot_R"};
/* Semantic parents (MD5_VRIK_*), -1 for the Hip. */
static const int pr_parent[PR_BODY] = {
	-1, MD5_VRIK_HIP, MD5_VRIK_SPINE1, MD5_VRIK_SPINE2, MD5_VRIK_NECK,
	MD5_VRIK_SPINE2, MD5_VRIK_SHOULDER_L, MD5_VRIK_UPPERARM_L, MD5_VRIK_LOWERARM_L,
	MD5_VRIK_SPINE2, MD5_VRIK_SHOULDER_R, MD5_VRIK_UPPERARM_R, MD5_VRIK_LOWERARM_R,
	MD5_VRIK_HIP, MD5_VRIK_UPPERLEG_L, MD5_VRIK_LOWERLEG_L,
	MD5_VRIK_HIP, MD5_VRIK_UPPERLEG_R, MD5_VRIK_LOWERLEG_R};
/* The joint each bone segment runs to; -1 for end bones. */
static const int pr_child[PR_BODY] = {
	MD5_VRIK_SPINE1, MD5_VRIK_SPINE2, MD5_VRIK_NECK, MD5_VRIK_HEAD, -1,
	MD5_VRIK_UPPERARM_L, MD5_VRIK_LOWERARM_L, MD5_VRIK_HAND_L, -1,
	MD5_VRIK_UPPERARM_R, MD5_VRIK_LOWERARM_R, MD5_VRIK_HAND_R, -1,
	MD5_VRIK_LOWERLEG_L, MD5_VRIK_FOOT_L, -1,
	MD5_VRIK_LOWERLEG_R, MD5_VRIK_FOOT_R, -1};
/* MD5 joint order: SuperRoot, Root, then the body in pr_joint_names order. */
static const int pr_md5_index[PR_BODY] = {
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

static const char *const pr_stock_frames[] = {
	"axrun1", "axrun2", "axrun3", "axrun4", "axrun5", "axrun6", "rockrun1", "rockrun2",
	"rockrun3", "rockrun4", "rockrun5", "rockrun6", "stand1", "stand2", "stand3", "stand4",
	"stand5", "axstnd1", "axstnd2", "axstnd3", "axstnd4", "axstnd5", "axstnd6", "axstnd7",
	"axstnd8", "axstnd9", "axstnd10", "axstnd11", "axstnd12", "axpain1", "axpain2",
	"axpain3", "axpain4", "axpain5", "axpain6", "pain1", "pain2", "pain3", "pain4", "pain5",
	"pain6", "axdeth1", "axdeth2", "axdeth3", "axdeth4", "axdeth5", "axdeth6", "axdeth7",
	"axdeth8", "axdeth9", "deatha1", "deatha2", "deatha3", "deatha4", "deatha5", "deatha6",
	"deatha7", "deatha8", "deatha9", "deatha10", "deatha11", "deathb1", "deathb2",
	"deathb3", "deathb4", "deathb5", "deathb6", "deathb7", "deathb8", "deathb9", "deathc1",
	"deathc2", "deathc3", "deathc4", "deathc5", "deathc6", "deathc7", "deathc8", "deathc9",
	"deathc10", "deathc11", "deathc12", "deathc13", "deathc14", "deathc15", "deathd1",
	"deathd2", "deathd3", "deathd4", "deathd5", "deathd6", "deathd7", "deathd8", "deathd9",
	"deathe1", "deathe2", "deathe3", "deathe4", "deathe5", "deathe6", "deathe7", "deathe8",
	"deathe9", "nailatt1", "nailatt2", "light1", "light2", "rockatt1", "rockatt2",
	"rockatt3", "rockatt4", "rockatt5", "rockatt6", "shotatt1", "shotatt2", "shotatt3",
	"shotatt4", "shotatt5", "shotatt6", "axatt1", "axatt2", "axatt3", "axatt4", "axatt5",
	"axatt6", "axattb1", "axattb2", "axattb3", "axattb4", "axattb5", "axattb6", "axattc1",
	"axattc2", "axattc3", "axattc4", "axattc5", "axattc6", "axattd1", "axattd2", "axattd3",
	"axattd4", "axattd5", "axattd6"};
#define PR_STOCK_FRAMES ((int)countof (pr_stock_frames))

/* ---------------------------------------------------------------- MDL reading */

typedef struct pr_mdl_s
{
	const byte *data;
	size_t length;
	float scale[3], origin[3];
	int skinwidth, skinheight, numverts, numtris, numframes;
	const byte *skin;  /* first skin's first image */
	const byte *st;    /* numverts * (onseam, s, t) */
	const byte *tris;  /* numtris * (facesfront, v0, v1, v2) */
	const byte **frame_verts; /* first pose of each frame group */
	const char **frame_names;
} pr_mdl_t;

static int PR_I32 (const byte *p)
{
	return (int)((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
		((unsigned)p[3] << 24));
}

static float PR_F32 (const byte *p)
{
	union { unsigned u; float f; } v;
	v.u = (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
		((unsigned)p[3] << 24);
	return v.f;
}

static qboolean PR_Need (size_t pos, size_t count, size_t size, size_t length)
{
	return size == 0 || (count <= (length - (pos > length ? length : pos)) / size && pos <= length);
}

static void PR_FreeMDL (pr_mdl_t *m)
{
	free ((void *)m->frame_verts);
	free ((void *)m->frame_names);
	m->frame_verts = NULL;
	m->frame_names = NULL;
}

static qboolean PR_ParseMDL (const byte *data, size_t length, pr_mdl_t *m)
{
	size_t pos = 84, skin_bytes;
	int i, numskins;

	memset (m, 0, sizeof (*m));
	if (!data || length < 84 || memcmp (data, "IDPO", 4) || PR_I32 (data + 4) != 6)
		return false;
	m->data = data;
	m->length = length;
	for (i = 0; i < 3; ++i)
	{
		m->scale[i] = PR_F32 (data + 8 + 4 * i);
		m->origin[i] = PR_F32 (data + 20 + 4 * i);
		if (!isfinite (m->scale[i]) || !isfinite (m->origin[i]))
			return false;
	}
	numskins = PR_I32 (data + 48);
	m->skinwidth = PR_I32 (data + 52);
	m->skinheight = PR_I32 (data + 56);
	m->numverts = PR_I32 (data + 60);
	m->numtris = PR_I32 (data + 64);
	m->numframes = PR_I32 (data + 68);
	if (numskins < 1 || numskins > 256 || m->skinwidth <= 0 || m->skinheight <= 0 ||
		m->skinwidth > CUSTOM_AVATAR_MAX_IMAGE_DIMENSION ||
		m->skinheight > CUSTOM_AVATAR_MAX_IMAGE_DIMENSION || (m->skinwidth & 1) ||
		m->numverts <= 0 || m->numverts > CUSTOM_AVATAR_MAX_VERTICES / 2 ||
		m->numtris <= 0 || m->numtris > CUSTOM_AVATAR_MAX_TRIANGLES ||
		m->numframes <= 0 || m->numframes > 1024)
		return false;
	skin_bytes = (size_t)m->skinwidth * (size_t)m->skinheight;
	for (i = 0; i < numskins; ++i)
	{
		int type, count;
		if (!PR_Need (pos, 1, 4, length))
			return false;
		type = PR_I32 (data + pos);
		pos += 4;
		if (type == 0)
		{
			if (!PR_Need (pos, 1, skin_bytes, length))
				return false;
			if (!m->skin)
				m->skin = data + pos;
			pos += skin_bytes;
			continue;
		}
		if (!PR_Need (pos, 1, 4, length))
			return false;
		count = PR_I32 (data + pos);
		pos += 4;
		if (count <= 0 || count > 256 || !PR_Need (pos, (size_t)count, 4, length))
			return false;
		pos += 4 * (size_t)count;
		if (!PR_Need (pos, (size_t)count, skin_bytes, length))
			return false;
		if (!m->skin)
			m->skin = data + pos;
		pos += (size_t)count * skin_bytes;
	}
	if (!PR_Need (pos, (size_t)m->numverts, 12, length))
		return false;
	m->st = data + pos;
	pos += 12 * (size_t)m->numverts;
	if (!PR_Need (pos, (size_t)m->numtris, 16, length))
		return false;
	m->tris = data + pos;
	pos += 16 * (size_t)m->numtris;
	m->frame_verts = (const byte **)calloc ((size_t)m->numframes, sizeof (*m->frame_verts));
	m->frame_names = (const char **)calloc ((size_t)m->numframes, sizeof (*m->frame_names));
	if (!m->frame_verts || !m->frame_names)
		goto fail;
	for (i = 0; i < m->numframes; ++i)
	{
		const size_t pose_bytes = 24 + 4 * (size_t)m->numverts;
		int type, count = 1;
		if (!PR_Need (pos, 1, 4, length))
			goto fail;
		type = PR_I32 (data + pos);
		pos += 4;
		if (type)
		{
			if (!PR_Need (pos, 1, 12, length))
				goto fail;
			count = PR_I32 (data + pos);
			pos += 12;
			if (count <= 0 || count > 1024 || !PR_Need (pos, (size_t)count, 4, length))
				goto fail;
			pos += 4 * (size_t)count;
		}
		if (!PR_Need (pos, (size_t)count, pose_bytes, length))
			goto fail;
		m->frame_names[i] = (const char *)data + pos + 8;
		m->frame_verts[i] = data + pos + 24;
		pos += (size_t)count * pose_bytes;
	}
	for (i = 0; i < m->numtris; ++i)
		for (int corner = 1; corner < 4; ++corner)
		{
			const int v = PR_I32 (m->tris + 16 * i + 4 * corner);
			if (v < 0 || v >= m->numverts)
				goto fail;
		}
	for (i = 0; i < m->numverts; ++i)
	{
		const int s = PR_I32 (m->st + 12 * i + 4), t = PR_I32 (m->st + 12 * i + 8);
		if (s < 0 || t < 0 || s >= m->skinwidth || t >= m->skinheight)
			goto fail;
	}
	return m->skin != NULL;
fail:
	PR_FreeMDL (m);
	return false;
}

static void PR_FrameVertex (const pr_mdl_t *m, int frame, int vertex, float out[3])
{
	const byte *v = m->frame_verts[frame] + 4 * vertex;
	for (int i = 0; i < 3; ++i)
		out[i] = m->origin[i] + m->scale[i] * v[i];
}

/* ------------------------------------------------------------------- math */

static float PR_Dot (const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void PR_Cross (const float a[3], const float b[3], float out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

static qboolean PR_Normalize (float v[3])
{
	const float length = sqrtf (PR_Dot (v, v));
	if (!(length > 1e-6f) || !isfinite (length))
		return false;
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
	return true;
}

/* Row-major 3x3. */
static void PR_MatVec (const float m[9], const float v[3], float out[3])
{
	const float x = v[0], y = v[1], z = v[2];
	out[0] = m[0] * x + m[1] * y + m[2] * z;
	out[1] = m[3] * x + m[4] * y + m[5] * z;
	out[2] = m[6] * x + m[7] * y + m[8] * z;
}

static void PR_MatTVec (const float m[9], const float v[3], float out[3])
{
	const float x = v[0], y = v[1], z = v[2];
	out[0] = m[0] * x + m[3] * y + m[6] * z;
	out[1] = m[1] * x + m[4] * y + m[7] * z;
	out[2] = m[2] * x + m[5] * y + m[8] * z;
}

static void PR_Identity (float m[9])
{
	memset (m, 0, 9 * sizeof (float));
	m[0] = m[4] = m[8] = 1.0f;
}

/* Orthonormal frame with columns (a, b', a x b') from a primary and a secondary
 * direction; rotation between two such frames is F_to * F_from^T. */
static qboolean PR_Frame (const float primary[3], const float secondary[3], float f[9])
{
	float a[3] = {primary[0], primary[1], primary[2]}, b[3], c[3];
	if (!PR_Normalize (a))
		return false;
	{
		const float d = PR_Dot (secondary, a);
		b[0] = secondary[0] - d * a[0];
		b[1] = secondary[1] - d * a[1];
		b[2] = secondary[2] - d * a[2];
	}
	if (!PR_Normalize (b))
		return false;
	PR_Cross (a, b, c);
	f[0] = a[0]; f[1] = b[0]; f[2] = c[0];
	f[3] = a[1]; f[4] = b[1]; f[5] = c[1];
	f[6] = a[2]; f[7] = b[2]; f[8] = c[2];
	return true;
}

static void PR_Rotation (const float from_frame[9], const float to_frame[9], float r[9])
{
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			r[i * 3 + j] = to_frame[i * 3 + 0] * from_frame[j * 3 + 0] +
				to_frame[i * 3 + 1] * from_frame[j * 3 + 1] +
				to_frame[i * 3 + 2] * from_frame[j * 3 + 2];
}

/* Shortest-arc rotation taking direction a to direction b. */
static qboolean PR_Arc (const float a_in[3], const float b_in[3], float r[9])
{
	float a[3] = {a_in[0], a_in[1], a_in[2]}, b[3] = {b_in[0], b_in[1], b_in[2]};
	float axis[3], helper[3] = {0.0f, 0.0f, 1.0f};
	if (!PR_Normalize (a) || !PR_Normalize (b))
		return false;
	PR_Cross (a, b, axis);
	if (PR_Dot (axis, axis) < 1e-8f)
	{
		if (PR_Dot (a, b) > 0.0f)
		{
			PR_Identity (r);
			return true;
		}
		if (fabsf (a[2]) > 0.9f)
			helper[0] = 1.0f, helper[2] = 0.0f;
	}
	else
	{
		helper[0] = axis[0];
		helper[1] = axis[1];
		helper[2] = axis[2];
	}
	{
		float from[9], to[9];
		/* Both frames share the rotation axis as their second column. */
		if (!PR_Frame (a, helper, from))
			return false;
		{
			float axis_n[3] = {from[1], from[4], from[7]};
			if (!PR_Frame (b, axis_n, to))
				return false;
		}
		PR_Rotation (from, to, r);
	}
	return true;
}

/* Rotation part (row-major 3x3) and origin of a 3x4 skeleton matrix after the
 * MD5 to MDL axis change (x_mdl = y_md5, y_mdl = -x_md5). */
static void PR_RangerBind (const float bind[12], float rotation[9], float origin[3])
{
	const float row0[3] = {bind[0], bind[1], bind[2]};
	const float row1[3] = {bind[4], bind[5], bind[6]};
	const float row2[3] = {bind[8], bind[9], bind[10]};
	rotation[0] = row1[0]; rotation[1] = row1[1]; rotation[2] = row1[2];
	rotation[3] = -row0[0]; rotation[4] = -row0[1]; rotation[5] = -row0[2];
	rotation[6] = row2[0]; rotation[7] = row2[1]; rotation[8] = row2[2];
	origin[0] = bind[7];
	origin[1] = -bind[3];
	origin[2] = bind[11];
}

/* MD5 stores x, y, z of a unit quaternion whose w is derived as negative. */
static void PR_Quaternion (const float m[9], float q[3])
{
	float w, x, y, z;
	const float trace = m[0] + m[4] + m[8];
	if (trace > 0.0f)
	{
		const float s = sqrtf (trace + 1.0f) * 2.0f;
		w = 0.25f * s;
		x = (m[7] - m[5]) / s;
		y = (m[2] - m[6]) / s;
		z = (m[3] - m[1]) / s;
	}
	else if (m[0] > m[4] && m[0] > m[8])
	{
		const float s = sqrtf (1.0f + m[0] - m[4] - m[8]) * 2.0f;
		w = (m[7] - m[5]) / s;
		x = 0.25f * s;
		y = (m[1] + m[3]) / s;
		z = (m[2] + m[6]) / s;
	}
	else if (m[4] > m[8])
	{
		const float s = sqrtf (1.0f + m[4] - m[0] - m[8]) * 2.0f;
		w = (m[2] - m[6]) / s;
		x = (m[1] + m[3]) / s;
		y = 0.25f * s;
		z = (m[5] + m[7]) / s;
	}
	else
	{
		const float s = sqrtf (1.0f + m[8] - m[0] - m[4]) * 2.0f;
		w = (m[3] - m[1]) / s;
		x = (m[2] + m[6]) / s;
		y = (m[5] + m[7]) / s;
		z = 0.25f * s;
	}
	if (w > 0.0f)
		x = -x, y = -y, z = -z;
	q[0] = x;
	q[1] = y;
	q[2] = z;
}

/* ----------------------------------------------------------------- output */

typedef struct pr_text_s
{
	char *data;
	size_t length, capacity;
	qboolean failed;
} pr_text_t;

static void PR_Append (pr_text_t *t, const char *format, ...) FUNC_PRINTF (2, 3);
static void PR_Append (pr_text_t *t, const char *format, ...)
{
	va_list args;
	int n;
	if (t->failed)
		return;
	if (!t->data)
	{
		t->capacity = 256 * 1024;
		t->length = 0;
		if (!(t->data = (char *)malloc (t->capacity)))
		{
			t->failed = true;
			return;
		}
	}
	for (;;)
	{
		const size_t room = t->capacity - t->length;
		va_start (args, format);
		n = q_vsnprintf (t->data + t->length, room, format, args);
		va_end (args);
		if (n < 0)
		{
			t->failed = true;
			return;
		}
		if ((size_t)n < room)
		{
			t->length += (size_t)n;
			return;
		}
		{
			const size_t capacity = t->capacity * 2;
			char *grown;
			if (capacity > 8u * 1024u * 1024u || !(grown = (char *)realloc (t->data, capacity)))
			{
				t->failed = true;
				return;
			}
			t->data = grown;
			t->capacity = capacity;
		}
	}
}

static byte *PR_TGA (const pr_mdl_t *m, qboolean glow, size_t *size)
{
	const int w = m->skinwidth, h = m->skinheight;
	byte *out;
	qboolean any = false;
	*size = 18 + 3 * (size_t)w * (size_t)h;
	out = (byte *)calloc (1, *size);
	if (!out)
		return NULL;
	out[2] = 2;
	out[12] = (byte)(w & 255);
	out[13] = (byte)(w >> 8);
	out[14] = (byte)(h & 255);
	out[15] = (byte)(h >> 8);
	out[16] = 24;
	out[17] = 0x20; /* top-left origin: rows in MDL skin order */
	for (size_t i = 0; i < (size_t)w * (size_t)h; ++i)
	{
		const int index = m->skin[i];
		const unsigned rgba = d_8to24table[index];
		byte *p = out + 18 + 3 * i;
		if (glow && index < 224)
			continue;
		if (glow && index < 255)
			any = true;
		p[0] = (byte)((rgba >> 16) & 255);
		p[1] = (byte)((rgba >> 8) & 255);
		p[2] = (byte)(rgba & 255);
	}
	if (glow && !any)
	{
		free (out);
		*size = 0;
		return NULL;
	}
	return out;
}

/* ------------------------------------------------------------- rig build */

typedef struct pr_skeleton_s
{
	float ref[PR_BODY][3];       /* joint positions in the reference frame */
	float bind[PR_BODY][3];      /* joint positions in the Ranger bind pose */
	float rotation[PR_BODY][9];  /* reference -> bind, per bone */
	float orient[PR_JOINTS][9];  /* Ranger bind orientation, MDL axes */
	float origin[PR_JOINTS][3];  /* MD5 bind origins */
	float segment_end[PR_BODY][3];
	float height;
} pr_skeleton_t;

static int PR_FindJoint (const md5_skeleton_view_t *view, const char *name)
{
	for (size_t i = 0; i < view->joint_count; ++i)
		if (!strcmp (view->joints[i].name, name))
			return (int)i;
	return -1;
}

static qboolean PR_BuildSkeleton (const vr_player_rig_t *rig, const md5_skeleton_view_t *ranger,
	float zmin, float zmax, pr_skeleton_t *s)
{
	float ranger_origin[PR_JOINTS][3];
	int i;

	memset (s, 0, sizeof (*s));
	s->height = zmax - zmin;
	if (!(s->height > 1.0f))
		return false;
	for (i = 0; i < PR_JOINTS; ++i)
	{
		const int joint = PR_FindJoint (ranger, pr_joint_names[i]);
		if (joint < 0)
			return false;
		PR_RangerBind (ranger->joints[joint].bind, s->orient[i], ranger_origin[i]);
	}
	for (i = 0; i < PR_BODY; ++i)
		memcpy (s->ref[i], rig->joint[i], sizeof (s->ref[i]));

	/* Per-bone rotation from the reference direction to the Ranger bind one. */
	for (i = 0; i < PR_BODY; ++i)
	{
		float from[3], to[3];
		const int child = pr_child[i];
		if (i == MD5_VRIK_HIP || i == MD5_VRIK_SPINE2)
		{
			const int left = i == MD5_VRIK_HIP ? MD5_VRIK_UPPERLEG_L : MD5_VRIK_UPPERARM_L;
			const int right = i == MD5_VRIK_HIP ? MD5_VRIK_UPPERLEG_R : MD5_VRIK_UPPERARM_R;
			float ref_up[3], ref_side[3], bind_up[3], bind_side[3], f_ref[9], f_bind[9];
			for (int k = 0; k < 3; ++k)
			{
				ref_up[k] = s->ref[child][k] - s->ref[i][k];
				ref_side[k] = s->ref[left][k] - s->ref[right][k];
				bind_up[k] = ranger_origin[pr_md5_index[child]][k] - ranger_origin[pr_md5_index[i]][k];
				bind_side[k] = ranger_origin[pr_md5_index[left]][k] - ranger_origin[pr_md5_index[right]][k];
			}
			if (!PR_Frame (ref_up, ref_side, f_ref) || !PR_Frame (bind_up, bind_side, f_bind))
				return false;
			PR_Rotation (f_ref, f_bind, s->rotation[i]);
			continue;
		}
		if (child < 0)
			continue;
		for (int k = 0; k < 3; ++k)
		{
			from[k] = s->ref[child][k] - s->ref[i][k];
			to[k] = ranger_origin[pr_md5_index[child]][k] - ranger_origin[pr_md5_index[i]][k];
		}
		if (!PR_Arc (from, to, s->rotation[i]))
			return false;
	}
	/* End bones keep their pose relative to the parent bone. */
	memcpy (s->rotation[MD5_VRIK_HEAD], s->rotation[MD5_VRIK_NECK], sizeof (s->rotation[0]));
	memcpy (s->rotation[MD5_VRIK_HAND_L], s->rotation[MD5_VRIK_LOWERARM_L], sizeof (s->rotation[0]));
	memcpy (s->rotation[MD5_VRIK_HAND_R], s->rotation[MD5_VRIK_LOWERARM_R], sizeof (s->rotation[0]));
	memcpy (s->rotation[MD5_VRIK_FOOT_L], s->rotation[MD5_VRIK_LOWERLEG_L], sizeof (s->rotation[0]));
	memcpy (s->rotation[MD5_VRIK_FOOT_R], s->rotation[MD5_VRIK_LOWERLEG_R], sizeof (s->rotation[0]));

	/* Bind positions: the model's bone lengths along the Ranger directions. */
	memcpy (s->bind[MD5_VRIK_HIP], s->ref[MD5_VRIK_HIP], sizeof (s->bind[0]));
	for (i = 1; i < PR_BODY; ++i) /* parents precede children in semantic order */
	{
		const int parent = pr_parent[i];
		float offset[3], turned[3];
		for (int k = 0; k < 3; ++k)
			offset[k] = s->ref[i][k] - s->ref[parent][k];
		PR_MatVec (s->rotation[parent], offset, turned);
		for (int k = 0; k < 3; ++k)
			s->bind[i][k] = s->bind[parent][k] + turned[k];
	}
	for (i = 0; i < 2; ++i)
		memcpy (s->origin[i], ranger_origin[i], sizeof (s->origin[0]));
	for (i = 0; i < PR_BODY; ++i)
		memcpy (s->origin[pr_md5_index[i]], s->bind[i], sizeof (s->origin[0]));

	/* Weight segments in the reference frame; end bones extend past the joint. */
	for (i = 0; i < PR_BODY; ++i)
	{
		float *end = s->segment_end[i];
		const int child = pr_child[i], parent = pr_parent[i];
		if (child >= 0)
			memcpy (end, s->ref[child], sizeof (s->segment_end[0]));
		else if (i == MD5_VRIK_HEAD)
		{
			memcpy (end, s->ref[i], sizeof (s->segment_end[0]));
			end[2] += 0.14f * s->height;
		}
		else if (i == MD5_VRIK_FOOT_L || i == MD5_VRIK_FOOT_R)
		{
			end[0] = s->ref[i][0] + 0.08f * s->height;
			end[1] = s->ref[i][1];
			end[2] = s->ref[i][2] - 0.03f * s->height;
		}
		else
			for (int k = 0; k < 3; ++k)
				end[k] = s->ref[i][k] + 0.45f * (s->ref[i][k] - s->ref[parent][k]);
	}
	return true;
}

static float PR_SegmentDistance (const float p[3], const float a[3], const float b[3])
{
	float d[3], v[3], t, length2;
	for (int k = 0; k < 3; ++k)
	{
		d[k] = b[k] - a[k];
		v[k] = p[k] - a[k];
	}
	length2 = PR_Dot (d, d);
	t = length2 > 1e-9f ? PR_Dot (v, d) / length2 : 0.0f;
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	for (int k = 0; k < 3; ++k)
		v[k] -= t * d[k];
	return sqrtf (PR_Dot (v, v));
}

static qboolean PR_LeftLimb (int bone)
{
	return (bone >= MD5_VRIK_SHOULDER_L && bone <= MD5_VRIK_HAND_L) ||
		(bone >= MD5_VRIK_UPPERLEG_L && bone <= MD5_VRIK_FOOT_L);
}

static qboolean PR_RightLimb (int bone)
{
	return (bone >= MD5_VRIK_SHOULDER_R && bone <= MD5_VRIK_HAND_R) ||
		(bone >= MD5_VRIK_UPPERLEG_R && bone <= MD5_VRIK_FOOT_R);
}

typedef struct pr_influence_s
{
	int bone[2];
	float weight[2];
} pr_influence_t;

static void PR_Weigh (const pr_skeleton_t *s, const float p[3], pr_influence_t *out)
{
	float distance[PR_BODY];
	const float side = 0.02f * s->height, blend = 0.08f * s->height;
	const float centre = s->ref[MD5_VRIK_HIP][1];
	int best = MD5_VRIK_SPINE1, second = -1;
	for (int i = 0; i < PR_BODY; ++i)
	{
		distance[i] = PR_SegmentDistance (p, s->ref[i], s->segment_end[i]);
		if ((PR_LeftLimb (i) && p[1] < centre - side) || (PR_RightLimb (i) && p[1] > centre + side))
			distance[i] += 1e4f;
		if (distance[i] < distance[best])
			best = i;
	}
	for (int i = 0; i < PR_BODY; ++i)
		if (i != best && (pr_parent[i] == best || pr_parent[best] == i) &&
			(second < 0 || distance[i] < distance[second]))
			second = i;
	out->bone[0] = best;
	out->weight[0] = 1.0f;
	out->bone[1] = -1;
	out->weight[1] = 0.0f;
	if (second >= 0 && blend > 0.0f)
	{
		float w = 0.5f * (1.0f - (distance[second] - distance[best]) / blend);
		if (w > 0.01f)
		{
			out->bone[1] = second;
			out->weight[1] = w;
			out->weight[0] = 1.0f - w;
		}
	}
}

static int PR_Root (int *parent, int v)
{
	while (parent[v] != v)
		v = parent[v] = parent[parent[v]];
	return v;
}

/* A held weapon modelled as a separate piece (smaller than a quarter of the
 * model, mostly on one hand or touching both forearms) follows its majority
 * bone rigidly instead of stretching between the arms. Body pieces keep their
 * blended weights so they bend at the joints. */
static qboolean PR_RigidPieces (const pr_mdl_t *m, pr_influence_t *influence)
{
	int *parent = (int *)malloc ((size_t)m->numverts * sizeof (*parent));
	int *size = (int *)calloc ((size_t)m->numverts, sizeof (*size));
	float (*votes)[PR_BODY] = (float (*)[PR_BODY])calloc ((size_t)m->numverts, sizeof (*votes));
	int i;
	if (!parent || !size || !votes)
	{
		free (parent);
		free (size);
		free (votes);
		return false;
	}
	for (i = 0; i < m->numverts; ++i)
		parent[i] = i;
	for (i = 0; i < m->numtris; ++i)
	{
		const byte *tri = m->tris + 16 * i;
		const int a = PR_Root (parent, PR_I32 (tri + 4));
		const int b = PR_Root (parent, PR_I32 (tri + 8));
		const int c = PR_Root (parent, PR_I32 (tri + 12));
		parent[b] = a;
		parent[PR_Root (parent, c)] = a;
	}
	for (i = 0; i < m->numverts; ++i)
	{
		const int root = PR_Root (parent, i);
		++size[root];
		for (int k = 0; k < 2 && influence[i].bone[k] >= 0; ++k)
			votes[root][influence[i].bone[k]] += influence[i].weight[k];
	}
	for (i = 0; i < m->numverts; ++i)
	{
		const int root = PR_Root (parent, i);
		int best = 0;
		if (size[root] * 4 > m->numverts)
			continue;
		{
			/* Held weapons: mostly on one hand, or touching both forearms. */
			const float left = votes[root][MD5_VRIK_LOWERARM_L] + votes[root][MD5_VRIK_HAND_L];
			const float right = votes[root][MD5_VRIK_LOWERARM_R] + votes[root][MD5_VRIK_HAND_R];
			float total = 0.0f;
			for (int b = 0; b < PR_BODY; ++b)
				total += votes[root][b];
			/* An arm piece reaches up to the elbow and shoulder; a weapon does not. */
			if ((left + right) < 0.85f * total ||
				(!(left > 0.0f && right > 0.0f) &&
				 !(votes[root][MD5_VRIK_HAND_L] * 2.0f > total) &&
				 !(votes[root][MD5_VRIK_HAND_R] * 2.0f > total)))
				continue;
		}
		for (int b = 1; b < PR_BODY; ++b)
			if (votes[root][b] > votes[root][best])
				best = b;
		influence[i].bone[0] = best;
		influence[i].weight[0] = 1.0f;
		influence[i].bone[1] = -1;
		influence[i].weight[1] = 0.0f;
	}
	free (parent);
	free (size);
	free (votes);
	return true;
}

/* Bind-pose position of a reference-frame vertex (linear blend). */
static void PR_BindPosition (const pr_skeleton_t *s, const float p[3],
	const pr_influence_t *influence, float out[3])
{
	out[0] = out[1] = out[2] = 0.0f;
	for (int k = 0; k < 2 && influence->bone[k] >= 0; ++k)
	{
		const int bone = influence->bone[k];
		float local[3], turned[3];
		for (int c = 0; c < 3; ++c)
			local[c] = p[c] - s->ref[bone][c];
		PR_MatVec (s->rotation[bone], local, turned);
		for (int c = 0; c < 3; ++c)
			out[c] += influence->weight[k] * (s->bind[bone][c] + turned[c]);
	}
}

static qboolean PR_BuildMesh (const pr_mdl_t *m, const vr_player_rig_t *rig,
	const md5_skeleton_view_t *ranger, pr_text_t *t)
{
	pr_skeleton_t s;
	float (*ref)[3] = NULL;
	pr_influence_t *influence = NULL;
	int *md5_vertex = NULL, numverts = 0, numweights = 0;
	float zmin = FLT_MAX, zmax = -FLT_MAX;
	qboolean ok = false;
	int i;

	if (rig->ref_frame < 0 || rig->ref_frame >= m->numframes)
		return false;
	ref = (float (*)[3])malloc ((size_t)m->numverts * sizeof (*ref));
	influence = (pr_influence_t *)malloc ((size_t)m->numverts * sizeof (*influence));
	md5_vertex = (int *)malloc ((size_t)m->numverts * 2 * sizeof (*md5_vertex));
	if (!ref || !influence || !md5_vertex)
		goto done;
	for (i = 0; i < m->numverts; ++i)
	{
		PR_FrameVertex (m, rig->ref_frame, i, ref[i]);
		zmin = q_min (zmin, ref[i][2]);
		zmax = q_max (zmax, ref[i][2]);
	}
	if (!PR_BuildSkeleton (rig, ranger, zmin, zmax, &s))
		goto done;
	for (i = 0; i < m->numverts; ++i)
		PR_Weigh (&s, ref[i], &influence[i]);
	if (!PR_RigidPieces (m, influence))
		goto done;

	/* One MD5 vertex per MDL vertex and skin side used by a triangle. */
	for (i = 0; i < 2 * m->numverts; ++i)
		md5_vertex[i] = -1;
	for (i = 0; i < m->numtris; ++i)
	{
		const byte *tri = m->tris + 16 * i;
		const qboolean front = PR_I32 (tri) != 0;
		for (int corner = 1; corner < 4; ++corner)
		{
			const int v = PR_I32 (tri + 4 * corner);
			const qboolean back = !front && PR_I32 (m->st + 12 * v) != 0;
			if (md5_vertex[2 * v + back] < 0)
				md5_vertex[2 * v + back] = numverts++;
		}
	}

	PR_Append (t, "MD5Version 10\ncommandline \"generated VR rig: %s\"\n", rig->name);
	PR_Append (t, "numJoints %d\nnumMeshes 1\njoints {\n", PR_JOINTS);
	for (i = 0; i < PR_JOINTS; ++i)
	{
		float q[3];
		int parent = i - 1;
		if (i >= 2)
		{
			const int semantic = i - 2;
			parent = pr_parent[semantic] < 0 ? 1 : pr_md5_index[pr_parent[semantic]];
		}
		PR_Quaternion (s.orient[i], q);
		PR_Append (t, "\t\"%s\" %d ( %.6f %.6f %.6f ) ( %.9f %.9f %.9f )\n", pr_joint_names[i],
			parent, s.origin[i][0], s.origin[i][1], s.origin[i][2], q[0], q[1], q[2]);
	}
	PR_Append (t, "}\nmesh {\n\tshader \"skin\"\n\tnumverts %d\n", numverts);
	{
		/* Emit vertices in MD5 index order. */
		int *order = (int *)malloc ((size_t)numverts * sizeof (*order));
		if (!order)
			goto done;
		for (i = 0; i < 2 * m->numverts; ++i)
			if (md5_vertex[i] >= 0)
				order[md5_vertex[i]] = i;
		for (i = 0; i < numverts; ++i)
		{
			const int v = order[i] / 2, back = order[i] & 1;
			const int count = influence[v].bone[1] >= 0 ? 2 : 1;
			const float s_coord = (float)PR_I32 (m->st + 12 * v + 4) + 0.5f +
				(back ? 0.5f * (float)m->skinwidth : 0.0f);
			const float t_coord = (float)PR_I32 (m->st + 12 * v + 8) + 0.5f;
			PR_Append (t, "\tvert %d ( %.6f %.6f ) %d %d\n", i, s_coord / (float)m->skinwidth,
				t_coord / (float)m->skinheight, numweights, count);
			numweights += count;
		}
		{
			/* Classic meshes contain zero-area triangles; avatars may not. */
			int kept = 0, numkept = 0;
			byte *keep = (byte *)calloc ((size_t)m->numtris, 1);
			if (!keep)
			{
				free (order);
				goto done;
			}
			for (i = 0; i < m->numtris; ++i)
			{
				const byte *tri = m->tris + 16 * i;
				float p[3][3], u[3], w[3], c[3];
				for (int corner = 0; corner < 3; ++corner)
				{
					const int v = PR_I32 (tri + 4 + 4 * corner);
					PR_BindPosition (&s, ref[v], &influence[v], p[corner]);
				}
				for (int k = 0; k < 3; ++k)
				{
					u[k] = p[1][k] - p[0][k];
					w[k] = p[2][k] - p[0][k];
				}
				PR_Cross (u, w, c);
				keep[i] = PR_Dot (c, c) > 1e-8f;
				/* A face joining parts that move apart (a weapon handle spanning
				 * both hands) stretches into a sheet once the pose changes. */
				for (int e = 0; e < 3 && keep[i]; ++e)
				{
					const int a = PR_I32 (tri + 4 + 4 * e), b = PR_I32 (tri + 4 + 4 * ((e + 1) % 3));
					float dr[3], db[3];
					for (int k = 0; k < 3; ++k)
					{
						dr[k] = ref[a][k] - ref[b][k];
						db[k] = p[e][k] - p[(e + 1) % 3][k];
					}
					if (sqrtf (PR_Dot (db, db)) > 2.5f * sqrtf (PR_Dot (dr, dr)) + 0.05f * s.height)
						keep[i] = false;
				}
				numkept += keep[i];
			}
			PR_Append (t, "\tnumtris %d\n", numkept);
			for (i = 0; i < m->numtris; ++i)
			{
				const byte *tri = m->tris + 16 * i;
				const qboolean front = PR_I32 (tri) != 0;
				int out[3];
				if (!keep[i])
					continue;
				for (int corner = 0; corner < 3; ++corner)
				{
					const int v = PR_I32 (tri + 4 + 4 * corner);
					const qboolean back = !front && PR_I32 (m->st + 12 * v) != 0;
					out[corner] = md5_vertex[2 * v + back];
				}
				PR_Append (t, "\ttri %d %d %d %d\n", kept++, out[0], out[1], out[2]);
			}
			free (keep);
		}
		PR_Append (t, "\tnumweights %d\n", numweights);
		numweights = 0;
		for (i = 0; i < numverts; ++i)
		{
			const int v = order[i] / 2;
			for (int k = 0; k < 2 && influence[v].bone[k] >= 0; ++k)
			{
				const int bone = influence[v].bone[k];
				const int joint = pr_md5_index[bone];
				float local[3], turned[3], offset[3];
				for (int c = 0; c < 3; ++c)
					local[c] = ref[v][c] - s.ref[bone][c];
				PR_MatVec (s.rotation[bone], local, turned);
				PR_MatTVec (s.orient[joint], turned, offset);
				PR_Append (t, "\tweight %d %d %.6f ( %.6f %.6f %.6f )\n", numweights++, joint,
					influence[v].weight[k], offset[0], offset[1], offset[2]);
			}
		}
		free (order);
	}
	PR_Append (t, "}\n");
	ok = !t->failed;
done:
	free (ref);
	free (influence);
	free (md5_vertex);
	return ok;
}

/* -------------------------------------------------------- frame mapping */

static short *PR_CanonicalMap (const pr_mdl_t *m)
{
	int named = 0, moved = 0;
	short *map;
	for (int i = 0; i < m->numframes; ++i)
	{
		char name[17];
		memcpy (name, m->frame_names[i], 16);
		name[16] = 0;
		for (int j = 0; j < PR_STOCK_FRAMES; ++j)
			if (!strcmp (name, pr_stock_frames[j]))
			{
				++named;
				moved += j != i;
				break;
			}
	}
	/* Stock names in a different order (Zerstorer's added chainsaw frames). */
	if (named * 5 < m->numframes * 3 || moved * 10 < named)
		return NULL;
	map = (short *)malloc ((size_t)m->numframes * sizeof (*map));
	if (!map)
		return NULL;
	for (int i = 0; i < m->numframes; ++i)
	{
		char name[17];
		memcpy (name, m->frame_names[i], 16);
		name[16] = 0;
		map[i] = -1;
		for (int j = 0; j < PR_STOCK_FRAMES; ++j)
			if (!strcmp (name, pr_stock_frames[j]))
			{
				map[i] = (short)j;
				break;
			}
	}
	return map;
}

/* ------------------------------------------------------------- registry */

static void PR_ResetForGame (void)
{
	if (!strcmp (pr_gamedir, com_gamedir))
		return;
	for (int i = 0; i < pr_model_count; ++i)
		free (pr_models[i].canonical);
	memset (pr_models, 0, sizeof (pr_models));
	pr_model_count = 0;
	q_strlcpy (pr_gamedir, com_gamedir, sizeof (pr_gamedir));
}

static pr_model_t *PR_Find (const qmodel_t *model)
{
	if (!model)
		return NULL;
	for (int i = 0; i < pr_model_count; ++i)
		if (pr_models[i].model == model && !strcmp (pr_models[i].name, model->name))
			return &pr_models[i];
	return NULL;
}

static int PR_Build (pr_model_t *entry, const qmodel_t *model, const md5_skeleton_view_t *ranger)
{
	unsigned int path_id = 0;
	byte *file;
	size_t length;
	unsigned long long signature;
	vec3_t origin;
	pr_mdl_t mdl;
	pr_text_t text = {0};
	custom_avatar_data_t data;
	char key[32], manifest[96];
	int id = -1;

	file = COM_LoadFile (model->name, &path_id);
	if (!file)
		return -1;
	length = (size_t)com_filesize;
	if (!VR_WeaponFamilySignature (file, length, &signature, origin))
		goto done;
	for (size_t i = 0; i < countof (vr_player_rigs); ++i)
		if (vr_player_rigs[i].signature == signature)
			entry->rig = &vr_player_rigs[i];
	if (!entry->rig || !PR_ParseMDL (file, length, &mdl))
		goto done;
	if (PR_BuildMesh (&mdl, entry->rig, ranger, &text))
	{
		memset (&data, 0, sizeof (data));
		q_snprintf (manifest, sizeof (manifest),
			"version 1\nname \"%s\"\nscale 1\nequipment native\n", entry->rig->name);
		data.sizes[CUSTOM_AVATAR_MANIFEST] = strlen (manifest);
		data.bytes[CUSTOM_AVATAR_MANIFEST] = (byte *)malloc (data.sizes[CUSTOM_AVATAR_MANIFEST]);
		if (data.bytes[CUSTOM_AVATAR_MANIFEST])
			memcpy (data.bytes[CUSTOM_AVATAR_MANIFEST], manifest, data.sizes[CUSTOM_AVATAR_MANIFEST]);
		data.bytes[CUSTOM_AVATAR_MESH] = (byte *)text.data;
		data.sizes[CUSTOM_AVATAR_MESH] = text.length;
		text.data = NULL;
		data.bytes[CUSTOM_AVATAR_SKIN] = PR_TGA (&mdl, false, &data.sizes[CUSTOM_AVATAR_SKIN]);
		data.bytes[CUSTOM_AVATAR_GLOW] = PR_TGA (&mdl, true, &data.sizes[CUSTOM_AVATAR_GLOW]);
		entry->numframes = mdl.numframes;
		entry->canonical = PR_CanonicalMap (&mdl);
		/* The key names the mesh and the skin, so a reskin gets its own avatar. */
		{
			unsigned skin_hash = 2166136261u;
			for (size_t i = 0; i < (size_t)mdl.skinwidth * (size_t)mdl.skinheight; ++i)
				skin_hash = (skin_hash ^ mdl.skin[i]) * 16777619u;
			q_snprintf (key, sizeof (key), "@rig-%016llx-%08x", signature, skin_hash);
		}
		if (data.bytes[CUSTOM_AVATAR_MANIFEST] && data.bytes[CUSTOM_AVATAR_SKIN])
			id = CustomAvatar_RegisterGenerated (key, entry->rig->name, &data);
		else
			CustomAvatar_FreeData (&data);
	}
	free (text.data);
	PR_FreeMDL (&mdl);
done:
	Mem_Free (file);
	return id;
}

int R_PlayerRigEnsure (qmodel_t *model, const md5_skeleton_view_t *ranger)
{
	pr_model_t *entry;
	if (!model || model->needload || model->type != mod_alias || !ranger || isDedicated)
		return -1;
	PR_ResetForGame ();
	entry = PR_Find (model);
	if (entry)
		return entry->state == PR_READY ? entry->avatar_id : -1;
	if (pr_model_count >= PR_MAX_MODELS)
		return -1;
	entry = &pr_models[pr_model_count++];
	memset (entry, 0, sizeof (*entry));
	entry->model = model;
	q_strlcpy (entry->name, model->name, sizeof (entry->name));
	entry->avatar_id = PR_Build (entry, model, ranger);
	entry->state = entry->avatar_id >= 0 ? PR_READY : PR_NONE;
	if (entry->state == PR_READY)
		Con_DPrintf ("VR body rig: %s uses \"%s\"\n", model->name, entry->rig->name);
	return entry->avatar_id;
}

qboolean R_PlayerRigSeen (const qmodel_t *model)
{
	return !strcmp (pr_gamedir, com_gamedir) && PR_Find (model) != NULL;
}

int R_PlayerRigLookup (const qmodel_t *model)
{
	const pr_model_t *entry;
	if (strcmp (pr_gamedir, com_gamedir))
		return -1;
	entry = PR_Find (model);
	return entry && entry->state == PR_READY ? entry->avatar_id : -1;
}

int R_PlayerRigCanonicalFrame (const qmodel_t *model, int frame)
{
	const pr_model_t *entry = strcmp (pr_gamedir, com_gamedir) ? NULL : PR_Find (model);
	if (!entry || entry->state != PR_READY || !entry->canonical)
		return frame;
	return frame >= 0 && frame < entry->numframes ? entry->canonical[frame] : -1;
}

static const char *const pr_dump_files[CUSTOM_AVATAR_FILE_COUNT] = {
	"avatar.cfg", "model.md5mesh", "skin.tga", "skin_glow.tga"};

/* Writes each built rig as a player_models-style package for inspection. */
static void PR_Dump (void)
{
	char directory[MAX_OSPATH], path[MAX_OSPATH];
	int written = 0;
	q_snprintf (directory, sizeof (directory), "%s/vr_player_rigs", COM_GetWriteRoot ());
	Sys_mkdir (directory);
	for (int i = 0; i < pr_model_count; ++i)
	{
		const pr_model_t *e = &pr_models[i];
		custom_avatar_data_t data;
		char folder[MAX_OSPATH];
		if (e->state != PR_READY || !CustomAvatar_ReadData (e->avatar_id, &data))
			continue;
		q_snprintf (folder, sizeof (folder), "%s/%s", directory, e->rig->key);
		Sys_mkdir (folder);
		for (int k = 0; k < CUSTOM_AVATAR_FILE_COUNT; ++k)
		{
			FILE *f;
			if (!data.bytes[k])
				continue;
			q_snprintf (path, sizeof (path), "%s/%s", folder, pr_dump_files[k]);
			f = fopen (path, "wb");
			if (f)
			{
				fwrite (data.bytes[k], 1, data.sizes[k], f);
				fclose (f);
			}
		}
		CustomAvatar_FreeData (&data);
		Con_Printf ("Wrote %s\n", folder);
		++written;
	}
	if (!written)
		Con_Printf ("No VR body rig has been built yet.\n");
}

/* Builds the rig of a named model without a tracked player (testing aid). */
static void PR_BuildNamed (const char *name)
{
	md5_skeleton_view_t ranger, skeleton;
	qmodel_t *source, *model, *avatar;
	int id;
	source = Mod_GetAvatarBuiltinModel (PLAYER_AVATAR_RANGER);
	if (!source || !Mod_GetMD5Skeleton (source, &ranger) || !ranger.from_rerelease)
	{
		Con_Printf ("VR body rigs need the official rerelease player model.\n");
		return;
	}
	model = Mod_ForName (name, false);
	if (!model || model->type != mod_alias)
	{
		Con_Printf ("%s is not an alias model.\n", name);
		return;
	}
	id = R_PlayerRigEnsure (model, &ranger);
	if (id < 0)
	{
		Con_Printf ("%s: no VR body rig for this mesh.\n", name);
		return;
	}
	avatar = Mod_GetAvatarCustomModel (id);
	if (!avatar || !Mod_GetMD5Skeleton (avatar, &skeleton))
	{
		Con_Printf ("%s: the built rig was rejected.\n", name);
		return;
	}
	Con_Printf ("%s: built \"%s\" (%d joints).\n", name, CustomAvatar_DisplayNameForId (id),
		(int)skeleton.joint_count);
}

static void R_PlayerRigs_f (void)
{
	int shown = 0;
	if (Cmd_Argc () >= 3 && !q_strcasecmp (Cmd_Argv (1), "build"))
	{
		PR_ResetForGame ();
		PR_BuildNamed (Cmd_Argv (2));
		return;
	}
	if (Cmd_Argc () >= 2 && !q_strcasecmp (Cmd_Argv (1), "dump"))
	{
		PR_Dump ();
		return;
	}
	Con_Printf ("Code-defined VR body rigs: %d meshes known.\n", (int)countof (vr_player_rigs));
	for (int i = 0; i < pr_model_count && !strcmp (pr_gamedir, com_gamedir); ++i)
	{
		const pr_model_t *e = &pr_models[i];
		Con_Printf ("  %-24s %s\n", e->name, e->state == PR_READY ? e->rig->name :
			(e->rig ? "rig failed" : "no rig"));
		++shown;
	}
	if (!shown)
		Con_Printf ("  No player model has been seen with VR body tracking in this game yet.\n");
	Con_Printf ("vr_player_rigs build <model> builds one; vr_player_rigs dump writes them out.\n");
}

void R_PlayerRigInit (void)
{
	Cmd_AddCommand ("vr_player_rigs", R_PlayerRigs_f);
}
