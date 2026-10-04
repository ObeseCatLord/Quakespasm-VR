/* Exercise the production private-player weapon-pose and makevectors adapters. */
#include "../Quake/sv_phys.c"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

server_t sv;
server_static_t svs;
client_t *host_client;
qcvm_t *qcvm;
globalvars_t *pr_global_struct;
double realtime;
char com_gamedir[MAX_OSPATH];

static globalvars_t fixture_globals;
static edict_t fixture_edicts[3];
static client_t fixture_client;
static int fixture_trace_calls;
static int fixture_calibration_calls;
static vec3_t fixture_calibration_angles;

static void AssertNear (float actual, float expected)
{
	assert (fabsf (actual - expected) < 0.0001f);
}

/* The pose path needs only an unobstructed world trace. */
trace_t SV_Move (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end,
	int type, edict_t *passedict)
{
	trace_t trace;
	(void)mins;
	(void)maxs;
	(void)type;
	(void)passedict;
	fixture_trace_calls++;
	assert (start != NULL);
	memset (&trace, 0, sizeof (trace));
	trace.fraction = 1.0f;
	VectorCopy (end, trace.endpos);
	return trace;
}

/* This fixture never admits a Dwell pair; record that production asks. */
qboolean SV_VRDwellBerserkMeleeEnabled (void)
{
	return false;
}

const char *COM_SkipPath (const char *pathname)
{
	return pathname;
}

const char *PR_GetString (int num)
{
	(void)num;
	return "progs/v_shot.mdl";
}

int q_strcasecmp (const char *s1, const char *s2)
{
	(void)s1;
	(void)s2;
	return 1;
}

ddef_t *ED_FindField (const char *name)
{
	(void)name;
	return NULL;
}

int ED_FindFieldOffset (const char *name)
{
	(void)name;
	return 0;
}

dfunction_t *ED_FindFunction (const char *fn_name)
{
	(void)fn_name;
	return NULL;
}

eval_t *GetEdictFieldValue (edict_t *ed, int fldofs)
{
	(void)ed;
	(void)fldofs;
	return NULL;
}

void SV_LinkEdict (edict_t *ent, qboolean touch_triggers)
{
	(void)ent;
	(void)touch_triggers;
}

/* Keep the calibration seam typed while verifying the raw rolled source axis
 * that Begin passes to its production calibration boundary. */
void VR_WeaponCalibrationProjectileSourceOffset (const char *viewmodel,
	int weapon_bit, const vec3_t angles, float viewheight, vec3_t out)
{
	vec3_t forward, right, up;
	assert (!strcmp (viewmodel, "progs/v_shot.mdl"));
	assert (weapon_bit == IT_SHOTGUN);
	AssertNear (viewheight, 22.0f);
	fixture_calibration_calls++;
	VectorCopy (angles, fixture_calibration_angles);
	AngleVectors ((vec_t *)angles, forward, right, up);
	VectorScale (forward, 8.0f, out);
	VectorMA (out, 2.0f, right, out);
	VectorMA (out, 3.0f, up, out);
}

static void AssertVector (const vec3_t actual, float x, float y, float z)
{
	AssertNear (actual[0], x);
	AssertNear (actual[1], y);
	AssertNear (actual[2], z);
}

static void AssertRolledLateralBasis (void)
{
	/* At pitch/yaw zero, a 90-degree physical roll rotates right into -up
	 * and up into -right.  This is the basis used by pellet spread and by
	 * lateral/up authored muzzle offsets. */
	AssertVector (pr_global_struct->v_forward, 1.0f, 0.0f, 0.0f);
	AssertVector (pr_global_struct->v_right, 0.0f, 0.0f, -1.0f);
	AssertVector (pr_global_struct->v_up, 0.0f, -1.0f, 0.0f);
}

static void SetNativeBasis (const vec3_t angles)
{
	AngleVectors ((vec_t *)angles, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
}

static void SetFixtureGlobals (void)
{
	memset (&sv, 0, sizeof (sv));
	memset (&fixture_globals, 0, sizeof (fixture_globals));
	memset (fixture_edicts, 0, sizeof (fixture_edicts));
	memset (&fixture_client, 0, sizeof (fixture_client));
	memset (com_gamedir, 0, sizeof (com_gamedir));
	qcvm = &sv.qcvm;
	pr_global_struct = &fixture_globals;
	qcvm->edicts = fixture_edicts;
	qcvm->edict_size = sizeof (fixture_edicts[0]);
	qcvm->num_edicts = 3;
	fixture_client.edict = &fixture_edicts[1];
	fixture_client.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	fixture_client.lastmovetime = 100.0;
	realtime = 100.0;
	sv_vr_weapon_pose_scope = NULL;
	fixture_trace_calls = 0;
	fixture_calibration_calls = 0;
}

static void SetAdmittedCommand (usercmd_t *cmd)
{
	memset (cmd, 0, sizeof (*cmd));
	cmd->vr_active = true;
	cmd->vr_handpos_relative = true;
	VectorSet (cmd->vr_handpos, 10.0f, 20.0f, 30.0f);
	VectorSet (cmd->vr_handrot, 0.0f, 0.0f, 90.0f);
}

int main (void)
{
	sv_vr_weapon_pose_scope_t parent, child, relocated_parent, relocated_child;
	edict_t *player = &fixture_edicts[1];
	edict_t *other = &fixture_edicts[2];
	usercmd_t cmd;
	vec3_t matching = {0.0f, 0.0f, 0.0f};
	vec3_t world_angles = {0.0f, 90.0f, 0.0f};
	vec3_t saved_forward = {4.0f, 5.0f, 6.0f};
	vec3_t saved_right = {7.0f, 8.0f, 9.0f};
	vec3_t saved_up = {10.0f, 11.0f, 12.0f};

	SetFixtureGlobals ();
	SetAdmittedCommand (&cmd);
	VectorSet (player->v.origin, 100.0f, 200.0f, 300.0f);
	VectorSet (player->v.v_angle, 4.0f, 5.0f, 6.0f);
	VectorSet (player->v.view_ofs, 0.0f, 0.0f, 22.0f);
	player->v.weapon = IT_SHOTGUN;
	VectorCopy (saved_forward, pr_global_struct->v_forward);
	VectorCopy (saved_right, pr_global_struct->v_right);
	VectorCopy (saved_up, pr_global_struct->v_up);

	SV_BeginPrivateVRWeaponPose (player, &fixture_client, &cmd, &parent);
	assert (parent.applied);
	assert (parent.shot_basis_valid);
	assert (sv_vr_weapon_pose_scope == &parent);
	AssertVector (parent.body_origin, 100.0f, 200.0f, 300.0f);
	AssertVector (parent.shot_angles, 0.0f, 0.0f, 90.0f);
	AssertVector (player->v.v_angle, 0.0f, 0.0f, 0.0f);
	AssertRolledLateralBasis ();
	assert (fixture_trace_calls == 1);
	assert (fixture_calibration_calls == 1);
	AssertVector (fixture_calibration_angles, 0.0f, 0.0f, 90.0f);
	/* hand origin (110,220,330) minus rolled 8-forward/2-right/3-up source */
	AssertVector (player->v.origin, 102.0f, 223.0f, 332.0f);

	pr_global_struct->self = EDICT_TO_PROG (player);
	SetNativeBasis (matching);
	assert (SV_VRWeaponShotBasis (matching));
	AssertRolledLateralBasis ();
	/* Stock and AD call makevectors in W_Attack and their firing leaves. */
	SetNativeBasis (matching);
	assert (SV_VRWeaponShotBasis (matching));
	AssertRolledLateralBasis ();

	/* A different self and a world-angle call retain ordinary native axes. */
	pr_global_struct->self = EDICT_TO_PROG (other);
	SetNativeBasis (matching);
	assert (!SV_VRWeaponShotBasis (matching));
	AssertVector (pr_global_struct->v_right, 0.0f, -1.0f, 0.0f);
	pr_global_struct->self = EDICT_TO_PROG (player);
	VectorCopy (world_angles, player->v.v_angle);
	SetNativeBasis (world_angles);
	assert (!SV_VRWeaponShotBasis (world_angles));
	AssertVector (pr_global_struct->v_right, 1.0f, 0.0f, 0.0f);
	VectorClear (player->v.v_angle);

	/* A nested same-player pose remains usable for source reconstruction, but
	 * cannot capture a new generic basis and therefore masks its parent. */
	SV_BeginPrivateVRWeaponPose (player, &fixture_client, &cmd, &child);
	assert (child.applied);
	assert (!child.shot_basis_valid);
	assert (child.previous == &parent);
	assert (sv_vr_weapon_pose_scope == &child);
	SetNativeBasis (matching);
	assert (!SV_VRWeaponShotBasis (matching));
	SV_EndPrivateVRWeaponPoseGuarded (player, &child, true);
	assert (sv_vr_weapon_pose_scope == &parent);
	AssertVector (player->v.origin, 102.0f, 223.0f, 332.0f);
	AssertVector (pr_global_struct->v_forward, 0.0f, 1.0f, 0.0f);
	AssertVector (pr_global_struct->v_right, 1.0f, 0.0f, 0.0f);
	AssertVector (pr_global_struct->v_up, 0.0f, 0.0f, 1.0f);
	SetNativeBasis (matching);
	assert (SV_VRWeaponShotBasis (matching));

	/* Normal cleanup restores the body, camera angle, and pre-pose globals. */
	SV_EndPrivateVRWeaponPoseGuarded (player, &parent, true);
	assert (sv_vr_weapon_pose_scope == NULL);
	AssertVector (player->v.origin, 100.0f, 200.0f, 300.0f);
	AssertVector (player->v.v_angle, 4.0f, 5.0f, 6.0f);
	AssertVector (pr_global_struct->v_forward, 4.0f, 5.0f, 6.0f);
	AssertVector (pr_global_struct->v_right, 7.0f, 8.0f, 9.0f);
	AssertVector (pr_global_struct->v_up, 10.0f, 11.0f, 12.0f);

	/* Relocating the parent invalidates its basis and is inherited by a child;
	 * guarded cleanup preserves the relocated origin while restoring angles and
	 * globals. */
	SV_BeginPrivateVRWeaponPose (player, &fixture_client, &cmd,
		&relocated_parent);
	VectorSet (player->v.origin, 400.0f, 500.0f, 600.0f);
	SV_VRWeaponPoseSetOrigin (player);
	assert (relocated_parent.origin_relocated);
	assert (relocated_parent.akimbo_invalidated);
	SetNativeBasis (matching);
	assert (!SV_VRWeaponShotBasis (matching));
	SV_BeginPrivateVRWeaponPose (player, &fixture_client, &cmd,
		&relocated_child);
	assert (relocated_child.akimbo_invalidated);
	assert (!relocated_child.shot_basis_valid);
	SV_EndPrivateVRWeaponPoseGuarded (player, &relocated_child, true);
	SV_EndPrivateVRWeaponPoseGuarded (player, &relocated_parent, true);
	assert (sv_vr_weapon_pose_scope == NULL);
	AssertVector (player->v.origin, 400.0f, 500.0f, 600.0f);
	AssertVector (player->v.v_angle, 4.0f, 5.0f, 6.0f);
	AssertVector (pr_global_struct->v_forward, 4.0f, 5.0f, 6.0f);
	AssertVector (pr_global_struct->v_right, 7.0f, 8.0f, 9.0f);
	AssertVector (pr_global_struct->v_up, 10.0f, 11.0f, 12.0f);

	puts ("VR shot roll scope: production pose, nested masking, restoration, and relocation invalidation: ok");
	return 0;
}
