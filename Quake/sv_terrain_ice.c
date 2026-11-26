/*
 * Falling ice for the editable terrain prototype: server side. GPL-2.0-or-later, as the rest of
 * the engine.
 *
 * After a dig, terrain_ice.c finds the solid islands it cut loose; each becomes a rigid body here.
 * A body is a point shell (surface-net vertices of its brick) tested against the terrain's signed
 * distance field: gravity, then speculative sequential-impulse contacts with restitution and
 * Coulomb friction, then a fixed-step integration. A body that stays slow for a while is stamped
 * back into the field as solid, so it is cave again. Moving bodies hurt and shove the edicts they
 * overlap. All field changes go through the op log (DETACH, STAMP), so a replay reproduces them.
 */
#include "quakedef.h"
#include "terrain.h"

extern cvar_t sv_gravity;

#define ICE_MAX_BODIES 32
#define ICE_MAX_SHELL  128
#define ICE_STEP	   (1.0f / 144.0f)
#define ICE_ITERS	   8
#define ICE_TARGET	   (-0.5f) // shell points may sink this far into the surface, so a resting chunk overlaps the rock it sits on

static cvar_t terrain_ice_bodies = {"terrain_ice_bodies", "1", CVAR_NONE};		  // 0: digging never detaches anything
static cvar_t terrain_ice_max = {"terrain_ice_max", "16", CVAR_NONE};			  // live bodies; past this a cut-loose island stays in the field
static cvar_t terrain_ice_restitution = {"terrain_ice_restitution", "0.25", CVAR_NONE};
static cvar_t terrain_ice_friction = {"terrain_ice_friction", "0.7", CVAR_NONE};
static cvar_t terrain_ice_damage = {"terrain_ice_damage", "1", CVAR_NONE};		  // multiplies mass * speed / 500; 0 turns crushing off
static cvar_t terrain_ice_shell = {"terrain_ice_shell", "64", CVAR_NONE};		  // shell points per body
static cvar_t terrain_ice_sleep = {"terrain_ice_sleep", "1.0", CVAR_NONE};		  // seconds of calm before a body is set back into the field
static cvar_t terrain_ice_angdamp = {"terrain_ice_angdamp", "0.6", CVAR_NONE};	  // 1/s, doubled in contact
static cvar_t terrain_ice_hurt_speed = {"terrain_ice_hurt_speed", "150", CVAR_NONE}; // least speed that hurts

typedef struct
{
	float r[3], n[3], d;
	float vnmin, inv_kn, acc_n, acc_t[3];
} contact_t;

typedef struct
{
	int				 active, id;
	const terrain_brick_t *brick;
	float			 pos[3], q[4], vel[3], w[3];
	float			 prev_pos[3], prev_q[4];
	float			 inv_mass, inv_ib[9];
	float			 shell[ICE_MAX_SHELL][3]; // relative to the centre of mass, body frame
	int				 nshell;
	double			 calm, age;
	terrain_mesh_t	 mesh;
	int				 contacts, hard;
	float			 vmax, zmin, zstart, com_dmin, d_worst, p_worst[3];
	double			 hit_at[16];
	int				 hit_ent[16];
} body_t;

static body_t  bodies[ICE_MAX_BODIES];
static double  accum;
static float   alpha;
static struct
{
	int	   spawned, stamped, vanished, lost, hits, kills, bounces, hard_impacts;
	double ms_total, ms_frame_max, ms_last;
	long   substeps, body_steps;
	float  rest_d, rest_field; // at the last stamp: field at the centre of mass before, solid test after
	float  rest_pos[3];
	int	   rest_valid;
	float  mid_pre, mid_post;  // field at the pillar's middle when it was built, and right after the cut
	int	   mid_valid;
	int	   dummy;
	float  first_start_z, first_min_z, first_vmax, first_com_dmin;
	int	   first_valid;
	int	   bench;
} st;
static struct
{
	int	  valid;
	float root[3], axis[2], floor_z, length, mid[3];
} pillar;
static terrain_mesh_t scratch;

/* ---- small math ---- */

static void Cross (const float a[3], const float b[3], float o[3])
{
	o[0] = a[1] * b[2] - a[2] * b[1];
	o[1] = a[2] * b[0] - a[0] * b[2];
	o[2] = a[0] * b[1] - a[1] * b[0];
}
static float Dot (const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
static void MatVec (const float m[9], const float v[3], float o[3])
{
	o[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
	o[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
	o[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}
static void MatTVec (const float m[9], const float v[3], float o[3])
{
	o[0] = m[0] * v[0] + m[3] * v[1] + m[6] * v[2];
	o[1] = m[1] * v[0] + m[4] * v[1] + m[7] * v[2];
	o[2] = m[2] * v[0] + m[5] * v[1] + m[8] * v[2];
}
static void MatMul (const float a[9], const float b[9], float o[9])
{
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			o[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
}
static int MatInvert (const float m[9], float o[9])
{
	const float c0 = m[4] * m[8] - m[5] * m[7], c1 = m[5] * m[6] - m[3] * m[8], c2 = m[3] * m[7] - m[4] * m[6];
	const float det = m[0] * c0 + m[1] * c1 + m[2] * c2;
	if (fabsf (det) < 1e-12f)
		return 0;
	const float id = 1.0f / det;
	o[0] = c0 * id;
	o[1] = (m[2] * m[7] - m[1] * m[8]) * id;
	o[2] = (m[1] * m[5] - m[2] * m[4]) * id;
	o[3] = c1 * id;
	o[4] = (m[0] * m[8] - m[2] * m[6]) * id;
	o[5] = (m[2] * m[3] - m[0] * m[5]) * id;
	o[6] = c2 * id;
	o[7] = (m[1] * m[6] - m[0] * m[7]) * id;
	o[8] = (m[0] * m[4] - m[1] * m[3]) * id;
	return 1;
}
static float Len (const float a[3])
{
	return sqrtf (Dot (a, a));
}

/* ---- bodies ---- */

static void MeshAppend (terrain_mesh_t *dst, const terrain_mesh_t *src)
{
	dst->verts = (terrain_vertex_t *)realloc (dst->verts, (dst->num_verts + src->num_verts) * sizeof (*dst->verts));
	dst->indices = (uint32_t *)realloc (dst->indices, (dst->num_indices + src->num_indices) * sizeof (*dst->indices));
	memcpy (dst->verts + dst->num_verts, src->verts, src->num_verts * sizeof (*dst->verts));
	for (int i = 0; i < src->num_indices; ++i)
		dst->indices[dst->num_indices + i] = src->indices[i] + (uint32_t)dst->num_verts;
	dst->num_verts += src->num_verts;
	dst->num_indices += src->num_indices;
}

/*
The mesher closes the lattice edge with a wall, which is no part of the ice. Drops every triangle with
a vertex outside the brick's own sample box and compacts the vertices.
*/
static void MeshCropToBrick (terrain_mesh_t *m, const terrain_brick_t *br)
{
	int *remap = (int *)malloc ((m->num_verts ? m->num_verts : 1) * sizeof (int));
	int	 nv = 0, ni = 0;
	terrain_vertex_t *out = (terrain_vertex_t *)malloc ((m->num_verts ? m->num_verts : 1) * sizeof (*out));
	for (int i = 0; i < m->num_verts; ++i)
		remap[i] = -1;
	for (int i = 0; i + 2 < m->num_indices; i += 3)
	{
		int inside = 1;
		for (int k = 0; k < 3 && inside; ++k)
			for (int a = 0; a < 3; ++a)
			{
				const float v = m->verts[m->indices[i + k]].pos[a];
				if (v < -0.5f || v > (float)(br->dim[a] - 1) * TERRAIN_CELL + 0.5f)
					inside = 0;
			}
		if (!inside)
			continue;
		for (int k = 0; k < 3; ++k)
		{
			const uint32_t vi = m->indices[i + k];
			if (remap[vi] < 0)
			{
				out[nv] = m->verts[vi];
				remap[vi] = nv++;
			}
			m->indices[ni++] = (uint32_t)remap[vi];
		}
	}
	free (m->verts);
	m->verts = out;
	m->num_verts = nv;
	m->num_indices = ni;
	free (remap);
}

static void FreeBody (body_t *b)
{
	free (b->mesh.verts);
	free (b->mesh.indices);
	memset (b, 0, sizeof (*b));
}

static int LiveBodies (void)
{
	int n = 0;
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
		n += bodies[i].active;
	return n;
}

static body_t *BodyCreate (int id, const float push_from[3])
{
	terrain_t			  *t = terrain_active;
	const terrain_brick_t *br = Terrain_Brick (t, id);
	if (!br)
		return NULL;
	body_t *b = NULL;
	for (int i = 0; i < ICE_MAX_BODIES && !b; ++i)
		if (!bodies[i].active)
			b = &bodies[i];
	if (!b)
		return NULL;
	memset (b, 0, sizeof (*b));
	b->active = 1;
	b->id = id;
	b->brick = br;
	for (int a = 0; a < 3; ++a)
		b->pos[a] = (float)t->origin[a] + (float)br->lat_min[a] * TERRAIN_CELL + br->com[a];
	b->q[0] = 1.0f;
	memcpy (b->prev_pos, b->pos, sizeof (b->pos));
	memcpy (b->prev_q, b->q, sizeof (b->q));
	b->inv_mass = 1.0f / br->mass;
	if (!MatInvert (br->inertia, b->inv_ib))
		b->inv_mass = 0.0f; // degenerate brick: it sits where it is
	// mesh the brick once
	for (int cz = 0; cz < br->field->size[2]; ++cz)
		for (int cy = 0; cy < br->field->size[1]; ++cy)
			for (int cx = 0; cx < br->field->size[0]; ++cx)
			{
				Terrain_MeshChunk (br->field, cx, cy, cz, &scratch);
				if (scratch.num_indices)
					MeshAppend (&b->mesh, &scratch);
			}
	MeshCropToBrick (&b->mesh, br);
	// shell: farthest-point sampling of the surface vertices, starting from the one farthest out
	const int want = CLAMP (8, (int)terrain_ice_shell.value, ICE_MAX_SHELL);
	const int nv = b->mesh.num_verts;
	float	 *mind = (float *)malloc ((nv ? nv : 1) * sizeof (float));
	int		  pick = 0;
	float	  best = -1.0f;
	for (int i = 0; i < nv; ++i)
	{
		const float *p = b->mesh.verts[i].pos;
		const float	 d[3] = {p[0] - br->com[0], p[1] - br->com[1], p[2] - br->com[2]};
		mind[i] = 1e30f;
		if (Dot (d, d) > best)
		{
			best = Dot (d, d);
			pick = i;
		}
	}
	while (nv && b->nshell < want)
	{
		const float *p = b->mesh.verts[pick].pos;
		for (int a = 0; a < 3; ++a)
			b->shell[b->nshell][a] = p[a] - br->com[a];
		++b->nshell;
		best = -1.0f;
		int next = -1;
		for (int i = 0; i < nv; ++i)
		{
			const float *v = b->mesh.verts[i].pos;
			const float	 d[3] = {v[0] - p[0], v[1] - p[1], v[2] - p[2]};
			const float	 d2 = Dot (d, d);
			if (d2 < mind[i])
				mind[i] = d2;
			if (mind[i] > best)
			{
				best = mind[i];
				next = i;
			}
		}
		if (next < 0 || best < 36.0f) // nothing left that is 6 units from a chosen point
			break;
		pick = next;
	}
	free (mind);
	if (push_from)
	{
		float d[3] = {b->pos[0] - push_from[0], b->pos[1] - push_from[1], b->pos[2] - push_from[2]};
		const float l = Len (d);
		if (l > 1.0f)
			for (int a = 0; a < 3; ++a)
				b->vel[a] = d[a] / l * 40.0f;
	}
	{
		float mx = 0, vmx = 0;
		for (int i = 0; i < b->nshell; ++i)
			mx = q_max (mx, Len (b->shell[i]));
		for (int i = 0; i < nv; ++i)
		{
			const float d[3] = {b->mesh.verts[i].pos[0] - br->com[0], b->mesh.verts[i].pos[1] - br->com[1], b->mesh.verts[i].pos[2] - br->com[2]};
			vmx = q_max (vmx, Len (d));
		}
		Con_DPrintf ("terrain_ice: body shell %d of %d verts, max shell r %.0f max vert r %.0f brick r %.0f com %.0f %.0f %.0f dim %d %d %d\n", b->nshell, nv, mx, vmx, br->radius, br->com[0], br->com[1], br->com[2], br->dim[0], br->dim[1], br->dim[2]);
	}
	b->zstart = b->pos[2];
	b->zmin = b->pos[2];
	b->com_dmin = 1e9f;
	++st.spawned;
	return b;
}

static void Gradient (const float p[3], float g[3])
{
	const float h = 6.0f;
	for (int a = 0; a < 3; ++a)
	{
		float lo[3], hi[3];
		memcpy (lo, p, sizeof (lo));
		memcpy (hi, p, sizeof (hi));
		lo[a] -= h;
		hi[a] += h;
		g[a] = Terrain_SampleWorld (terrain_active, hi) - Terrain_SampleWorld (terrain_active, lo);
	}
}

static void BodyWorld (const body_t *b, float R[9], float iw[9])
{
	float ib_rt[9], rt[9], tmp[9];
	Terrain_QuatMatrix (b->q, R);
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 3; ++j)
			rt[i * 3 + j] = R[j * 3 + i];
	MatMul (R, b->inv_ib, tmp);
	MatMul (tmp, rt, ib_rt);
	memcpy (iw, ib_rt, sizeof (ib_rt));
}

// One fixed step. Returns 1 when the body should leave the simulation (out of bounds).
static int BodyStep (body_t *b, float dt)
{
	const float gravity = sv_gravity.value;
	const float mu = terrain_ice_friction.value, restitution = terrain_ice_restitution.value;
	float		R[9], iw[9];
	static contact_t cs[ICE_MAX_SHELL];
	int			nc = 0;

	memcpy (b->prev_pos, b->pos, sizeof (b->pos));
	memcpy (b->prev_q, b->q, sizeof (b->q));
	b->vel[2] -= gravity * dt;
	BodyWorld (b, R, iw);

	const float speed = Len (b->vel);
	const float margin = 6.0f + speed * dt * 1.5f;
	for (int i = 0; i < b->nshell; ++i)
	{
		contact_t *c = &cs[nc];
		float	   p[3];
		MatVec (R, b->shell[i], c->r);
		for (int a = 0; a < 3; ++a)
			p[a] = b->pos[a] + c->r[a];
		c->d = Terrain_SampleWorld (terrain_active, p);
		if (i == 0)
			b->d_worst = 1e9f;
		if (c->d < b->d_worst)
		{
			b->d_worst = c->d;
			memcpy (b->p_worst, p, sizeof (p));
		}
		if (c->d > margin)
			continue;
		Gradient (p, c->n);
		float gl = Len (c->n);
		if (gl < 1e-3f)
		{
			// deep inside the saturated part of the field: no direction there; go back the way it came
			if (speed < 1.0f)
				continue;
			for (int a = 0; a < 3; ++a)
				c->n[a] = -b->vel[a];
			gl = Len (c->n);
		}
		for (int a = 0; a < 3; ++a)
			c->n[a] /= gl;
		float rn[3], irn[3], k[3];
		Cross (c->r, c->n, rn);
		MatVec (iw, rn, irn);
		Cross (irn, c->r, k);
		c->inv_kn = 1.0f / (b->inv_mass + Dot (k, c->n));
		float vp[3], wr[3];
		Cross (b->w, c->r, wr);
		for (int a = 0; a < 3; ++a)
			vp[a] = b->vel[a] + wr[a];
		const float vn = Dot (vp, c->n);
		const float gap = c->d - ICE_TARGET;
		if (gap > 0.0f)
			c->vnmin = -gap / dt; // may close this much and no more
		else
			c->vnmin = q_min (-gap * 0.2f / dt, 60.0f); // push out gently
		if (vn < -120.0f && gap <= -vn * dt)
		{
			c->vnmin = q_max (c->vnmin, -restitution * vn);
			++st.bounces;
		}
		c->acc_n = 0.0f;
		c->acc_t[0] = c->acc_t[1] = c->acc_t[2] = 0.0f;
		++nc;
	}
	b->contacts = nc;
	float impulse_sum = 0.0f;
	for (int it = 0; it < ICE_ITERS && nc; ++it)
		for (int i = 0; i < nc; ++i)
		{
			contact_t *c = &cs[i];
			float	   vp[3], wr[3], P[3], rp[3], dw[3];
			Cross (b->w, c->r, wr);
			for (int a = 0; a < 3; ++a)
				vp[a] = b->vel[a] + wr[a];
			const float vn = Dot (vp, c->n);
			float		dj = (c->vnmin - vn) * c->inv_kn;
			const float nacc = q_max (c->acc_n + dj, 0.0f);
			dj = nacc - c->acc_n;
			c->acc_n = nacc;
			for (int a = 0; a < 3; ++a)
			{
				P[a] = c->n[a] * dj;
				b->vel[a] += P[a] * b->inv_mass;
			}
			Cross (c->r, P, rp);
			MatVec (iw, rp, dw);
			for (int a = 0; a < 3; ++a)
				b->w[a] += dw[a];
			// friction on what is left of the tangential velocity
			Cross (b->w, c->r, wr);
			float vt[3];
			for (int a = 0; a < 3; ++a)
				vt[a] = b->vel[a] + wr[a];
			const float vn2 = Dot (vt, c->n);
			for (int a = 0; a < 3; ++a)
				vt[a] -= vn2 * c->n[a];
			const float vtl = Len (vt);
			if (vtl > 1e-3f && c->acc_n > 0.0f)
			{
				float t[3] = {vt[0] / vtl, vt[1] / vtl, vt[2] / vtl}, rt[3], irt[3], kt[3];
				Cross (c->r, t, rt);
				MatVec (iw, rt, irt);
				Cross (irt, c->r, kt);
				const float jt = -vtl / (b->inv_mass + Dot (kt, t));
				float		nt[3], lim = mu * c->acc_n, nl;
				for (int a = 0; a < 3; ++a)
					nt[a] = c->acc_t[a] + jt * t[a];
				nl = Len (nt);
				if (nl > lim)
					for (int a = 0; a < 3; ++a)
						nt[a] *= lim / nl;
				for (int a = 0; a < 3; ++a)
				{
					P[a] = nt[a] - c->acc_t[a];
					c->acc_t[a] = nt[a];
					b->vel[a] += P[a] * b->inv_mass;
				}
				Cross (c->r, P, rp);
				MatVec (iw, rp, dw);
				for (int a = 0; a < 3; ++a)
					b->w[a] += dw[a];
			}
		}
	for (int i = 0; i < nc; ++i)
		impulse_sum += cs[i].acc_n;
	if (impulse_sum * b->inv_mass > 220.0f) // velocity change from this step's contacts: a hard hit
	{
		++b->hard;
		++st.hard_impacts;
		float org[3] = {b->pos[0], b->pos[1], b->pos[2]};
		for (int i = 0; i < nc; ++i)
			if (cs[i].acc_n > 0.0f)
			{
				for (int a = 0; a < 3; ++a)
					org[a] = b->pos[a] + cs[i].r[a];
				break;
			}
		vec3_t dir = {0, 0, 24};
		SV_StartParticle (org, dir, 14, 18);
	}

	// damping, then integrate
	const float ad = terrain_ice_angdamp.value * (nc ? 2.0f : 1.0f);
	const float wscale = 1.0f / (1.0f + ad * dt), vscale = 1.0f / (1.0f + 0.05f * dt);
	for (int a = 0; a < 3; ++a)
	{
		b->w[a] *= wscale;
		b->vel[a] *= vscale;
		b->pos[a] += b->vel[a] * dt;
	}
	const float wl = Len (b->w);
	if (wl > 1e-6f)
	{
		const float h = 0.5f * dt;
		const float dq[4] = {-(b->w[0] * b->q[1] + b->w[1] * b->q[2] + b->w[2] * b->q[3]) * h, (b->w[0] * b->q[0] + b->w[1] * b->q[3] - b->w[2] * b->q[2]) * h,
							 (b->w[1] * b->q[0] + b->w[2] * b->q[1] - b->w[0] * b->q[3]) * h, (b->w[2] * b->q[0] + b->w[0] * b->q[2] - b->w[1] * b->q[1]) * h};
		float		l = 0.0f;
		for (int a = 0; a < 4; ++a)
		{
			b->q[a] += dq[a];
			l += b->q[a] * b->q[a];
		}
		l = 1.0f / sqrtf (l);
		for (int a = 0; a < 4; ++a)
			b->q[a] *= l;
	}
	const float sp = Len (b->vel);
	b->vmax = q_max (b->vmax, sp);
	b->zmin = q_min (b->zmin, b->pos[2]);
	const float cd = Terrain_SampleWorld (terrain_active, b->pos);
	b->com_dmin = q_min (b->com_dmin, cd);
	if (nc && sp < 8.0f && wl < 0.2f)
		b->calm += dt;
	else
		b->calm = 0.0;
	b->age += dt;
	++st.body_steps;
	const terrain_t *t = terrain_active;
	for (int a = 0; a < 3; ++a)
		if (b->pos[a] < (float)t->origin[a] + 8.0f || b->pos[a] > (float)(t->origin[a] + t->size[a] * TERRAIN_CHUNK_UNITS) - 8.0f)
			return 1;
	return 0;
}

static void BodyRelease (body_t *b, int stamped)
{
	if (!st.first_valid && stamped >= 0)
	{
		st.first_valid = 1;
		st.first_start_z = b->zstart;
		st.first_min_z = b->zmin;
		st.first_vmax = b->vmax;
		st.first_com_dmin = b->com_dmin;
	}
	FreeBody (b);
}

// Sets a calm body back into the field at a quantized pose.
static void BodyRest (body_t *b)
{
	terrain_t *t = terrain_active;
	terrain_op_t op;
	memset (&op, 0, sizeof (op));
	op.kind = TERRAIN_OP_STAMP;
	op.radius = (uint16_t)b->id;
	for (int a = 0; a < 3; ++a)
		op.center[a] = (int32_t)floorf ((b->pos[a] - (float)t->origin[a]) * TERRAIN_SUB / TERRAIN_CELL + 0.5f);
	op.aux = Terrain_PackQuat (b->q);
	st.rest_d = Terrain_SampleWorld (t, b->pos);
	memcpy (st.rest_pos, b->pos, sizeof (b->pos));
	const int changed = SV_Terrain_PushOp (&op);
	st.rest_field = Terrain_SampleWorld (t, b->pos);
	st.rest_valid = 1;
	if (changed)
		++st.stamped;
	BodyRelease (b, 1);
}

static void HurtEdict (edict_t *targ, float dmg)
{
	dfunction_t *td = ED_FindFunction ("T_Damage");
	if (!td)
	{
		targ->v.health -= dmg;
		return;
	}
	const int self = pr_global_struct->self, other = pr_global_struct->other;
	pr_global_struct->self = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	G_INT (OFS_PARM0) = EDICT_TO_PROG (targ);
	G_INT (OFS_PARM1) = EDICT_TO_PROG (qcvm->edicts);
	G_INT (OFS_PARM2) = EDICT_TO_PROG (qcvm->edicts);
	G_FLOAT (OFS_PARM3) = dmg;
	PR_ExecuteProgram (td - qcvm->functions);
	pr_global_struct->self = self;
	pr_global_struct->other = other;
}

// Moving ice hurts and shoves what it overlaps. The edict's box is tested against the brick's own field.
static void BodyCrush (body_t *b)
{
	const float speed = Len (b->vel);
	if (terrain_ice_damage.value <= 0.0f || speed < terrain_ice_hurt_speed.value)
		return;
	float R[9], iw[9];
	BodyWorld (b, R, iw);
	const terrain_brick_t *br = b->brick;
	const float			   lim[3] = {(float)(br->dim[0] - 1) * TERRAIN_CELL, (float)(br->dim[1] - 1) * TERRAIN_CELL, (float)(br->dim[2] - 1) * TERRAIN_CELL};
	for (int i = 1; i < qcvm->num_edicts; ++i)
	{
		edict_t *e = EDICT_NUM (i);
		if (e->free || e->v.takedamage == DAMAGE_NO || e->v.solid == SOLID_NOT || e->v.health <= 0)
			continue;
		const float reach = br->radius + 64.0f;
		const float c[3] = {e->v.origin[0] + (e->v.mins[0] + e->v.maxs[0]) * 0.5f, e->v.origin[1] + (e->v.mins[1] + e->v.maxs[1]) * 0.5f,
							e->v.origin[2] + (e->v.mins[2] + e->v.maxs[2]) * 0.5f};
		const float dd[3] = {c[0] - b->pos[0], c[1] - b->pos[1], c[2] - b->pos[2]};
		if (Dot (dd, dd) > reach * reach)
			continue;
		const float half_w = q_max (e->v.maxs[0], e->v.maxs[1]), half_h = (e->v.maxs[2] - e->v.mins[2]) * 0.5f;
		qboolean	hit = false;
		for (int k = 0; k < 3 && !hit; ++k)
		{
			float w[3] = {c[0], c[1], c[2] + (k - 1) * half_h * 0.8f}, d[3], pb[3];
			for (int a = 0; a < 3; ++a)
				d[a] = w[a] - b->pos[a];
			MatTVec (R, d, pb);
			for (int a = 0; a < 3; ++a)
				pb[a] += br->com[a];
			if (pb[0] < 0 || pb[1] < 0 || pb[2] < 0 || pb[0] > lim[0] || pb[1] > lim[1] || pb[2] > lim[2])
				continue;
			hit = Terrain_SampleWorld (br->field, pb) < half_w * 0.9f;
		}
		if (!hit)
			continue;
		int slot = -1;
		for (int k = 0; k < 16; ++k)
			if (b->hit_ent[k] == i)
				slot = k;
		if (slot >= 0 && qcvm->time < b->hit_at[slot] + 0.4)
			continue;
		if (slot < 0)
		{
			slot = 0;
			for (int k = 1; k < 16; ++k)
				if (b->hit_at[k] < b->hit_at[slot])
					slot = k;
		}
		b->hit_ent[slot] = i;
		b->hit_at[slot] = qcvm->time;
		const float dmg = q_min (terrain_ice_damage.value * br->mass * speed / 500.0f, 400.0f);
		float		dir[3] = {dd[0], dd[1], 0.0f};
		const float dl = Len (dir);
		if (dl > 1.0f)
			for (int a = 0; a < 3; ++a)
				dir[a] /= dl;
		const float push = q_min (speed * 0.7f, 600.0f);
		const float before = e->v.health;
		e->v.velocity[0] += dir[0] * push;
		e->v.velocity[1] += dir[1] * push;
		e->v.velocity[2] += 120.0f + push * 0.2f;
		e->v.flags = (int)e->v.flags & ~FL_ONGROUND;
		HurtEdict (e, dmg);
		++st.hits;
		if (e->v.health <= 0 && before > 0)
			++st.kills;
		Con_DPrintf ("terrain_ice: body %d hit edict %d for %.0f at %.0f u/s\n", b->id, i, dmg, speed);
	}
}

void SV_Terrain_IceReset (void)
{
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
		if (bodies[i].active)
			FreeBody (&bodies[i]);
	memset (&st, 0, sizeof (st));
	memset (&pillar, 0, sizeof (pillar));
	accum = 0.0;
}

void SV_Terrain_IceFrame (void)
{
	if (!terrain_active || !sv.active)
	{
		if (LiveBodies ())
			SV_Terrain_IceReset ();
		return;
	}
	if (st.bench || !LiveBodies ())
		return;
	const double start = Sys_DoubleTime ();
	accum += CLAMP (0.0, host_frametime, 0.1);
	int steps = 0;
	while (accum >= ICE_STEP && steps < 24)
	{
		accum -= ICE_STEP;
		++steps;
		++st.substeps;
		for (int i = 0; i < ICE_MAX_BODIES; ++i)
		{
			body_t *b = &bodies[i];
			if (!b->active)
				continue;
			// never move more than about a cell per step: speed splits the step
			const float move = (Len (b->vel) + Len (b->w) * b->brick->radius) * ICE_STEP;
			const int	sub = CLAMP (1, (int)ceilf (move / 12.0f), 8);
			int			gone = 0;
			for (int s = 0; s < sub && !gone; ++s)
				gone = BodyStep (b, ICE_STEP / sub);
			if (gone)
			{
				++st.lost;
				BodyRelease (b, 0);
			}
			else if (b->calm >= terrain_ice_sleep.value || b->age > 60.0)
				BodyRest (b);
		}
	}
	alpha = CLAMP (0.0f, (float)(accum / ICE_STEP), 1.0f);
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
		if (bodies[i].active)
			BodyCrush (&bodies[i]);
	st.ms_last = (Sys_DoubleTime () - start) * 1000.0;
	st.ms_total += st.ms_last;
	st.ms_frame_max = q_max (st.ms_frame_max, st.ms_last);
}

void SV_Terrain_IceEdited (const terrain_op_t *op)
{
	terrain_t *t = terrain_active;
	if (!t || !terrain_ice_bodies.value || op->kind != TERRAIN_OP_DIG)
		return;
	terrain_op_t found[8];
	const int	 n = Terrain_IceFindIslands (t, op, found, 8);
	float		 from[3];
	for (int a = 0; a < 3; ++a)
		from[a] = (float)t->origin[a] + (float)op->center[a] * TERRAIN_CELL / TERRAIN_SUB;
	for (int i = 0; i < n; ++i)
	{
		if (LiveBodies () >= CLAMP (1, (int)terrain_ice_max.value, ICE_MAX_BODIES))
			break;
		const int changed = SV_Terrain_PushOp (&found[i]);
		if (!changed)
			continue;
		const int id = Terrain_IceLastBrick (t);
		if (id < 0)
		{
			++st.vanished;
			continue;
		}
		if (pillar.valid && !st.mid_valid)
		{
			st.mid_post = Terrain_SampleWorld (t, pillar.mid);
			st.mid_valid = 1;
		}
		BodyCreate (id, from);
	}
}

int SV_Terrain_IceDrawCount (void)
{
	return LiveBodies ();
}

int SV_Terrain_IceDraw (int index, const terrain_mesh_t **mesh, float pos[3], float rot[9], float com[3])
{
	int n = 0;
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
	{
		const body_t *b = &bodies[i];
		if (!b->active || n++ != index)
			continue;
		float q[4], l = 0.0f;
		const float dot = b->prev_q[0] * b->q[0] + b->prev_q[1] * b->q[1] + b->prev_q[2] * b->q[2] + b->prev_q[3] * b->q[3];
		for (int a = 0; a < 4; ++a)
		{
			q[a] = b->prev_q[a] * (1.0f - alpha) * (dot < 0 ? -1.0f : 1.0f) + b->q[a] * alpha;
			l += q[a] * q[a];
		}
		for (int a = 0; a < 4; ++a)
			q[a] /= sqrtf (l);
		Terrain_QuatMatrix (q, rot);
		for (int a = 0; a < 3; ++a)
		{
			pos[a] = b->prev_pos[a] + (b->pos[a] - b->prev_pos[a]) * alpha;
			com[a] = b->brick->com[a];
		}
		*mesh = &b->mesh;
		return 1;
	}
	return 0;
}

/* ---- console ---- */

static qboolean Ready (void)
{
	if (!terrain_active || !sv.active)
	{
		Con_Printf ("terrain_ice: needs a terrain on a local server\n");
		return false;
	}
	return true;
}

static float FieldAt (float x, float y, float z)
{
	const float p[3] = {x, y, z};
	return Terrain_SampleWorld (terrain_active, p);
}

/*
terrain_ice_pillar [length]
Builds an icicle with fill ops from the ceiling above where you aim: a thin neck at the root, a
bulge below it, a tapering tip. terrain_ice_cut then digs through the neck.
*/
static void Pillar_f (void)
{
	if (!Ready ())
		return;
	PR_SwitchQCVM (&sv.qcvm);
	edict_t *pl = EDICT_NUM (1);
	vec3_t	 fwd, right, up, eye;
	AngleVectors (pl->v.v_angle, fwd, right, up);
	VectorAdd (pl->v.origin, pl->v.view_ofs, eye);
	float travelled = 0.0f, hit[3] = {0, 0, 0};
	int	  found = 0;
	while (travelled < 4096.0f)
	{
		VectorMA (eye, travelled, fwd, hit);
		const float d = Terrain_SampleWorld (terrain_active, hit);
		if (d <= 1.0f)
		{
			found = 1;
			break;
		}
		travelled += q_max (d * 0.8f, 2.0f);
	}
	if (!found)
	{
		Con_Printf ("terrain_ice_pillar: nothing in view\n");
		PR_SwitchQCVM (NULL);
		return;
	}
	float len = Cmd_Argc () > 1 ? (float)atof (Cmd_Argv (1)) : 260.0f;
	len = CLAMP (150.0f, len, 400.0f);
	/* the aimed spot may sit beside an existing icicle: try nearby axes until a column with clear air around it fits */
	float x = hit[0], y = hit[1], ceil_z = 0.0f, fz = 0.0f;
	int	  fit = 0;
	for (int cand = 0; cand < 49 && !fit; ++cand)
	{
		const float ang = (float)(cand % 8) * (float)M_PI * 0.25f, ring = 56.0f * (float)((cand + 7) / 8);
		const float cx = hit[0] + cosf (ang) * ring, cy = hit[1] + sinf (ang) * ring;
		float		z = hit[2] + 2.0f;
		while (z < hit[2] + 1500.0f && FieldAt (cx, cy, z) >= 0.0f)
			z += 4.0f;
		float f = hit[2] - 2.0f;
		while (f > z - 1500.0f && FieldAt (cx, cy, f) >= 0.0f)
			f -= 4.0f;
		const float l = q_min (len, (z - f) - 40.0f);
		if (l < 90.0f)
			continue;
		int clear = 1;
		// the root needs real rock around it, or the cut leaves the pillar hanging from a neighbour
		for (float up = 8.0f; up <= 72.0f && clear; up += 8.0f)
		{
			clear = FieldAt (cx, cy, z + up) < 0.0f;
			for (int k = 0; k < 8 && clear; ++k)
				clear = FieldAt (cx + cosf ((float)k * (float)M_PI * 0.25f) * 44.0f, cy + sinf ((float)k * (float)M_PI * 0.25f) * 44.0f, z + up) < 0.0f;
		}
		for (float dz = 70.0f; dz <= l && clear; dz += 20.0f)
			for (int k = 0; k < 8 && clear; ++k)
				clear = FieldAt (cx + cosf ((float)k * (float)M_PI * 0.25f) * 48.0f, cy + sinf ((float)k * (float)M_PI * 0.25f) * 48.0f, z - dz) > 0.0f;
		if (!clear)
			continue;
		x = cx;
		y = cy;
		ceil_z = z;
		fz = f;
		len = l;
		fit = 1;
	}
	if (!fit)
	{
		Con_Printf ("terrain_ice_pillar: no free column near that spot\n");
		PR_SwitchQCVM (NULL);
		return;
	}
	int ops = 0;
	for (float dz = -24.0f; dz <= len; dz += 10.0f)
	{
		const float tt = dz / len;
		float		r;
		if (tt < 0.0f)
			r = 22.0f;
		else if (tt < 0.1f)
			r = 16.0f;
		else if (tt < 0.55f)
			r = 16.0f + 18.0f * sinf ((tt - 0.1f) / 0.45f * (float)M_PI * 0.5f);
		else
			r = 34.0f - 28.0f * powf ((tt - 0.55f) / 0.45f, 1.2f);
		const float p[3] = {x, y, ceil_z - dz};
		ops += SV_Terrain_Edit (TERRAIN_OP_FILL, p, q_max (r, 8.0f)) > 0;
	}
	pillar.valid = 1;
	pillar.root[0] = x;
	pillar.root[1] = y;
	pillar.root[2] = ceil_z - 10.0f;
	pillar.axis[0] = x;
	pillar.axis[1] = y;
	pillar.floor_z = fz;
	pillar.length = len;
	pillar.mid[0] = x;
	pillar.mid[1] = y;
	pillar.mid[2] = ceil_z - len * 0.35f;
	st.mid_pre = Terrain_SampleWorld (terrain_active, pillar.mid);
	st.mid_valid = 0;
	st.first_valid = 0;
	Con_Printf ("terrain_ice_pillar: %.0f units long at %.0f %.0f from ceiling z %.0f (floor %.0f), %d fill ops, field at middle %.1f\n", len, x, y, ceil_z, fz, ops, st.mid_pre);
	PR_SwitchQCVM (NULL);
}

// terrain_ice_cut [radius]: dig through the neck of the last pillar, the same call a rocket makes
static void Cut_f (void)
{
	if (!Ready ())
		return;
	if (!pillar.valid)
	{
		Con_Printf ("terrain_ice_cut: build a pillar first\n");
		return;
	}
	PR_SwitchQCVM (&sv.qcvm);
	const float r = Cmd_Argc () > 1 ? (float)atof (Cmd_Argv (1)) : 40.0f;
	const int	before = st.spawned;
	int			changed = SV_Terrain_Edit (TERRAIN_OP_DIG, pillar.root, r);
	// a ceiling that bulges around the neck can need a second shot, as in play
	for (int again = 1; again <= 2 && st.spawned == before; ++again)
		changed += SV_Terrain_Edit (TERRAIN_OP_DIG, pillar.root, r + 12.0f * (float)again);
	Con_Printf ("terrain_ice_cut: dig r=%.0f changed %d samples, %d new bodies, field at middle was %.1f now %.1f\n", r, changed, st.spawned - before, st.mid_pre, st.mid_valid ? st.mid_post : Terrain_SampleWorld (terrain_active, pillar.mid));
	PR_SwitchQCVM (NULL);
}

/*
terrain_ice_cam pillar|body|rest [distance]
Puts the camera (noclip) at a clear spot that looks at the last pillar, the first live body or the last
resting place, for screenshots in tests.
*/
static void Cam_f (void)
{
	if (!Ready ())
		return;
	const char *what = Cmd_Argc () > 1 ? Cmd_Argv (1) : "pillar";
	const float dist = Cmd_Argc () > 2 ? (float)atof (Cmd_Argv (2)) : 300.0f;
	float		tgt[3] = {0, 0, 0};
	if (!strcmp (what, "body"))
	{
		for (int i = 0; i < ICE_MAX_BODIES; ++i)
			if (bodies[i].active)
			{
				memcpy (tgt, bodies[i].pos, sizeof (tgt));
				break;
			}
	}
	else if (!strcmp (what, "rest"))
		memcpy (tgt, st.rest_pos, sizeof (tgt));
	else
		memcpy (tgt, pillar.mid, sizeof (tgt));
	for (int tries = 0; tries < 16; ++tries)
	{
		const float ang = (float)tries * (float)M_PI * 0.125f, rise = (tries & 1) ? 40.0f : 0.0f;
		for (float dd = dist; dd >= 100.0f; dd -= 50.0f)
		{
			const float v[3] = {tgt[0] + cosf (ang) * dd, tgt[1] + sinf (ang) * dd, tgt[2] + rise};
			int			clear = FieldAt (v[0], v[1], v[2]) > 16.0f;
			for (int k = 1; k < 18 && clear; ++k)
			{
				const float f = (float)k / 25.0f;
				clear = FieldAt (v[0] + (tgt[0] - v[0]) * f, v[1] + (tgt[1] - v[1]) * f, v[2] + (tgt[2] - v[2]) * f) > 6.0f;
			}
			if (!clear)
				continue;
			const float dx = tgt[0] - v[0], dy = tgt[1] - v[1], dz = tgt[2] - v[2];
			const float yaw = atan2f (dy, dx) * 180.0f / (float)M_PI, pitch = -atan2f (dz, sqrtf (dx * dx + dy * dy)) * 180.0f / (float)M_PI;
			Cbuf_InsertText (va ("setpos %.0f %.0f %.0f %.0f %.0f 0\n", v[0], v[1], v[2], pitch, yaw));
			return;
		}
	}
	Con_Printf ("terrain_ice_cam: no clear view of %s\n", what);
}

// terrain_ice_dummy: puts a monster under the last pillar's tip, at full health 400
static void Dummy_f (void)
{
	if (!Ready () || !pillar.valid)
		return;
	PR_SwitchQCVM (&sv.qcvm);
	edict_t *mon = NULL;
	for (int i = 2; i < qcvm->num_edicts && !mon; ++i)
	{
		edict_t *e = EDICT_NUM (i);
		if (!e->free && ((int)e->v.flags & FL_MONSTER) && e->v.health > 0)
			mon = e;
	}
	if (!mon)
	{
		Con_Printf ("terrain_ice_dummy: no monster to move\n");
		PR_SwitchQCVM (NULL);
		return;
	}
	mon->v.origin[0] = pillar.axis[0];
	mon->v.origin[1] = pillar.axis[1];
	mon->v.origin[2] = pillar.floor_z - mon->v.mins[2] + 2.0f;
	mon->v.velocity[0] = mon->v.velocity[1] = mon->v.velocity[2] = 0.0f;
	mon->v.health = mon->v.max_health = 400.0f;
	mon->v.movetype = MOVETYPE_NONE; // a still target: the point is the damage path, not the monster's AI
	mon->v.nextthink = 1.0e9f;
	SV_LinkEdict (mon, false);
	st.dummy = NUM_FOR_EDICT (mon);
	Con_Printf ("terrain_ice_dummy: edict %d (%s) at %.0f %.0f %.0f, health %.0f\n", NUM_FOR_EDICT (mon), PR_GetString (mon->v.classname), mon->v.origin[0], mon->v.origin[1], mon->v.origin[2], mon->v.health);
	PR_SwitchQCVM (NULL);
}

// terrain_ice_ramp: a wedge of fill under the last pillar's tip, rising 35 degrees along +x, for a body to tumble down
static void Ramp_f (void)
{
	if (!Ready () || !pillar.valid)
		return;
	PR_SwitchQCVM (&sv.qcvm);
	int ops = 0;
	for (float s = -120.0f; s <= 120.0f; s += 14.0f)
	{
		const float p[3] = {pillar.axis[0] + s, pillar.axis[1], pillar.floor_z + 26.0f + (s + 120.0f) * 0.7f * 0.5f - 20.0f};
		ops += SV_Terrain_Edit (TERRAIN_OP_FILL, p, 34.0f) > 0;
	}
	Con_Printf ("terrain_ice_ramp: %d fill ops\n", ops);
	PR_SwitchQCVM (NULL);
}

// terrain_ice_toss vx vy vz [wx wy wz]: sets the newest body's velocities
static void Toss_f (void)
{
	body_t *b = NULL;
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
		if (bodies[i].active)
			b = &bodies[i];
	if (!b)
	{
		Con_Printf ("terrain_ice_toss: no live body\n");
		return;
	}
	for (int a = 0; a < 3; ++a)
	{
		b->vel[a] = Cmd_Argc () > 1 + a ? (float)atof (Cmd_Argv (1 + a)) : b->vel[a];
		b->w[a] = Cmd_Argc () > 4 + a ? (float)atof (Cmd_Argv (4 + a)) : b->w[a];
	}
}

static const char *Verdict (int ok)
{
	return ok ? "PASS" : "FAIL";
}

static void Info_f (void)
{
	if (!Ready ())
		return;
	Con_Printf (
		"terrain_ice_info: live %d spawned %d stamped %d vanished %d lost %d bricks %d hits %d kills %d hard_impacts %d bounces %d\n", LiveBodies (), st.spawned, st.stamped, st.vanished, st.lost,
		Terrain_BrickCount (terrain_active), st.hits, st.kills, st.hard_impacts, st.bounces);
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
	{
		const body_t *b = &bodies[i];
		if (!b->active)
			continue;
		Con_Printf (
			"terrain_ice_info: body %d brick %d mass %.0f shell %d pos %.0f %.0f %.0f speed %.0f spin %.2f contacts %d calm %.2f age %.1f zmin %.0f vmax %.0f com_d %.1f hard %d worst %.1f at %.0f %.0f %.0f\n", i, b->id, b->brick->mass, b->nshell,
			b->pos[0], b->pos[1], b->pos[2], Len (b->vel), Len (b->w), b->contacts, b->calm, b->age, b->zmin, b->vmax, Terrain_SampleWorld (terrain_active, b->pos), b->hard, b->d_worst, b->p_worst[0], b->p_worst[1], b->p_worst[2]);
	}
	const double per = st.body_steps ? st.ms_total * 1000.0 / st.body_steps : 0.0;
	Con_Printf (
		"terrain_ice_info: cost last %.3f ms max %.3f ms total %.1f ms over %ld substeps (%ld body steps, %.1f us per body step incl. crush tests)\n", st.ms_last, st.ms_frame_max, st.ms_total, st.substeps,
		st.body_steps, per);
	if (st.mid_valid)
		Con_Printf ("terrain_ice_info: CHECK old pillar location air after cut %s (middle was %.1f now %.1f)\n", Verdict (st.mid_pre < 0 && st.mid_post > 0), st.mid_pre, st.mid_post);
	if (st.first_valid)
		Con_Printf (
			"terrain_ice_info: CHECK first body fell %s (start z %.0f lowest z %.0f vmax %.0f) not buried %s (lowest com field %.1f)\n", Verdict (st.first_start_z - st.first_min_z > 20.0f && st.first_vmax > 100.0f),
			st.first_start_z, st.first_min_z, st.first_vmax, Verdict (st.first_com_dmin > -TERRAIN_CELL), st.first_com_dmin);
	if (st.dummy)
	{
		PR_SwitchQCVM (&sv.qcvm);
		const edict_t *m = EDICT_NUM (st.dummy);
		Con_Printf ("terrain_ice_info: CHECK monster under it hurt %s (health %.0f of 400 at %.0f %.0f %.0f, %d hits, %d kills)\n", Verdict (m->v.health < 400.0f), m->v.health, m->v.origin[0], m->v.origin[1], m->v.origin[2], st.hits, st.kills);
		PR_SwitchQCVM (NULL);
	}
	if (st.rest_valid)
		Con_Printf (
			"terrain_ice_info: CHECK rest stamp %s at %.0f %.0f %.0f (field before %.1f, after %.1f)\n", Verdict (st.rest_field < 0 && st.rest_d > -TERRAIN_CELL), st.rest_pos[0], st.rest_pos[1], st.rest_pos[2], st.rest_d,
			st.rest_field);
}

// terrain_ice_verify: rebuilds the region from its seed and replays the whole op log, then compares hashes
static void Verify_f (void)
{
	if (!Ready ())
		return;
	const terrain_t		*a = terrain_active;
	const terrain_op_t *ops;
	const int			n = SV_Terrain_OpLog (&ops);
	terrain_t		   *r = Terrain_Create (a->origin, a->size, a->seed);
	const double		start = Sys_DoubleTime ();
	Terrain_Generate (r);
	if (Cvar_VariableValue ("terrain_ice"))
		Terrain_GenerateIce (r);
	int kinds[4] = {0, 0, 0, 0};
	for (int i = 0; i < n; ++i)
	{
		Terrain_ApplyOp (r, &ops[i]);
		++kinds[ops[i].kind & 3];
	}
	const uint64_t h0 = Terrain_Hash (a), h1 = Terrain_Hash (r);
	Con_Printf (
		"terrain_ice_verify: %d ops (%d dig, %d fill, %d detach, %d stamp), %d bricks live and %d replayed, hash %016llx vs %016llx, replay %.0f ms: %s\n", n, kinds[0], kinds[1], kinds[2], kinds[3],
		Terrain_BrickCount (a), Terrain_BrickCount (r), (unsigned long long)h0, (unsigned long long)h1, (Sys_DoubleTime () - start) * 1000.0, h0 == h1 ? "MATCH" : "MISMATCH");
	Terrain_Free (r);
}

/*
terrain_ice_bench <bodies> [steps]
Drops that many copies of the newest brick (from the last cut) near the player and times the contact
step: the whole run, and the last quarter, when most have landed. Nothing is stamped.
*/
static void Bench_f (void)
{
	if (!Ready ())
		return;
	const int count = CLAMP (1, Cmd_Argc () > 1 ? atoi (Cmd_Argv (1)) : 1, ICE_MAX_BODIES);
	const int steps = CLAMP (50, Cmd_Argc () > 2 ? atoi (Cmd_Argv (2)) : 400, 4000);
	const int id = Terrain_BrickCount (terrain_active) - 1;
	if (id < 0)
	{
		Con_Printf ("terrain_ice_bench: cut a pillar first so there is a brick\n");
		return;
	}
	PR_SwitchQCVM (&sv.qcvm);
	body_t	   saved[ICE_MAX_BODIES];
	memcpy (saved, bodies, sizeof (saved));
	memset (bodies, 0, sizeof (bodies));
	st.bench = 1;
	const float *o = EDICT_NUM (1)->v.origin;
	for (int i = 0; i < count; ++i)
	{
		body_t *b = BodyCreate (id, NULL);
		if (!b)
			break;
		const float yaw = (float)i * 2.4f;
		b->pos[0] = o[0] + 60.0f * cosf (yaw) * (1 + i % 3);
		b->pos[1] = o[1] + 60.0f * sinf (yaw) * (1 + i % 3);
		b->pos[2] = o[2] + 60.0f + (float)(i % 4) * 30.0f;
		b->q[1] = 0.3f * sinf (yaw);
		b->q[2] = 0.5f;
		b->q[3] = 0.2f * cosf (yaw);
		const float l = sqrtf (b->q[0] * b->q[0] + b->q[1] * b->q[1] + b->q[2] * b->q[2] + b->q[3] * b->q[3]);
		for (int a = 0; a < 4; ++a)
			b->q[a] /= l;
	}
	double total = 0.0, tail = 0.0;
	long   tail_steps = 0, steps_done = 0;
	int	   in_contact = 0;
	for (int s = 0; s < steps; ++s)
	{
		const double t0 = Sys_DoubleTime ();
		int			 n = 0;
		for (int i = 0; i < ICE_MAX_BODIES; ++i)
			if (bodies[i].active)
			{
				BodyStep (&bodies[i], ICE_STEP);
				++n;
			}
		const double dt = Sys_DoubleTime () - t0;
		total += dt;
		steps_done += n;
		if (s >= steps * 3 / 4)
		{
			tail += dt;
			tail_steps += n;
		}
	}
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
		in_contact += bodies[i].active && bodies[i].contacts > 0;
	Con_Printf (
		"terrain_ice_bench: %d bodies x %d steps: %.1f us per body step overall, %.1f us in the last quarter (%d of them touching), %.3f ms per fixed step for all\n", count, steps,
		steps_done ? total * 1e6 / steps_done : 0.0, tail_steps ? tail * 1e6 / tail_steps : 0.0, in_contact, total * 1000.0 / steps);
	for (int i = 0; i < ICE_MAX_BODIES; ++i)
		if (bodies[i].active)
			FreeBody (&bodies[i]);
	memcpy (bodies, saved, sizeof (saved));
	st.bench = 0;
	PR_SwitchQCVM (NULL);
}

void SV_Terrain_IceInit (void)
{
	cvar_t *all[] = {&terrain_ice_bodies, &terrain_ice_max, &terrain_ice_restitution, &terrain_ice_friction, &terrain_ice_damage, &terrain_ice_shell, &terrain_ice_sleep, &terrain_ice_angdamp, &terrain_ice_hurt_speed};
	for (size_t i = 0; i < sizeof (all) / sizeof (all[0]); ++i)
		Cvar_RegisterVariable (all[i]);
	Cmd_AddCommand ("terrain_ice_pillar", Pillar_f);
	Cmd_AddCommand ("terrain_ice_cut", Cut_f);
	Cmd_AddCommand ("terrain_ice_dummy", Dummy_f);
	Cmd_AddCommand ("terrain_ice_cam", Cam_f);
	Cmd_AddCommand ("terrain_ice_ramp", Ramp_f);
	Cmd_AddCommand ("terrain_ice_toss", Toss_f);
	Cmd_AddCommand ("terrain_ice_info", Info_f);
	Cmd_AddCommand ("terrain_ice_verify", Verify_f);
	Cmd_AddCommand ("terrain_ice_bench", Bench_f);
}
