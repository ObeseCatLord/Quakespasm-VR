/* Exercise the production private-player makevectors adapter. */
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

static globalvars_t fixture_globals;
static edict_t fixture_edicts[3];
static client_t fixture_client;

static void AssertNear (float actual, float expected)
{
	assert (fabsf (actual - expected) < 0.0001f);
}

static void AssertRolledLateralBasis (void)
{
	/* At pitch/yaw zero, a 90-degree physical roll rotates right into -up
	 * and up into -right.  This is the basis used by pellet spread and by
	 * lateral/up authored muzzle offsets. */
	AssertNear (pr_global_struct->v_forward[0], 1.0f);
	AssertNear (pr_global_struct->v_forward[1], 0.0f);
	AssertNear (pr_global_struct->v_forward[2], 0.0f);
	AssertNear (pr_global_struct->v_right[0], 0.0f);
	AssertNear (pr_global_struct->v_right[1], 0.0f);
	AssertNear (pr_global_struct->v_right[2], -1.0f);
	AssertNear (pr_global_struct->v_up[0], 0.0f);
	AssertNear (pr_global_struct->v_up[1], -1.0f);
	AssertNear (pr_global_struct->v_up[2], 0.0f);
}

static void SetNativeBasis (const vec3_t angles)
{
	AngleVectors ((vec_t *)angles, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
}

int main (void)
{
	sv_vr_weapon_pose_scope_t scope;
	edict_t *player = &fixture_edicts[1];
	edict_t *other = &fixture_edicts[2];
	vec3_t matching = {0.0f, 0.0f, 0.0f};
	vec3_t world_angles = {0.0f, 90.0f, 0.0f};
	vec3_t source_forward, source_right, source_up;

	memset (&sv, 0, sizeof (sv));
	memset (&fixture_globals, 0, sizeof (fixture_globals));
	memset (fixture_edicts, 0, sizeof (fixture_edicts));
	memset (&fixture_client, 0, sizeof (fixture_client));
	memset (&scope, 0, sizeof (scope));
	qcvm = &sv.qcvm;
	pr_global_struct = &fixture_globals;
	qcvm->edicts = fixture_edicts;
	qcvm->edict_size = sizeof (fixture_edicts[0]);
	qcvm->num_edicts = 3;
	fixture_client.edict = player;
	scope.ent = player;
	scope.client = &fixture_client;
	scope.applied = true;
	scope.shot_basis_valid = true;
	VectorSet (scope.shot_angles, 0.0f, 0.0f, 90.0f);
	VectorClear (player->v.v_angle); /* QuakeC camera roll remains zero. */
	sv_vr_weapon_pose_scope = &scope;

	/* The production scope initializer and the source correction share this
	 * selector. At 90 degrees, authored right/up offsets become -Z/-Y. */
	assert (SV_VRWeaponScopeAngles (&scope, player) == scope.shot_angles);
	SV_VRWeaponSetScopedBasis (&scope, player);
	AssertRolledLateralBasis ();
	AngleVectors (SV_VRWeaponScopeAngles (&scope, player), source_forward,
		source_right, source_up);
	AssertNear (source_forward[0], 1.0f);
	AssertNear (source_right[2], -1.0f);
	AssertNear (source_up[1], -1.0f);

	pr_global_struct->self = EDICT_TO_PROG (player);
	SetNativeBasis (matching);
	assert (SV_VRWeaponShotBasis (matching));
	AssertRolledLateralBasis ();

	/* Stock and AD call makevectors in W_Attack and again in their shotgun
	 * leaf.  A second identical player call in this still-admitted scope must
	 * retain physical roll. */
	SetNativeBasis (matching);
	assert (SV_VRWeaponShotBasis (matching));
	AssertRolledLateralBasis ();

	/* The same vector from another self is ordinary QC/world math. */
	pr_global_struct->self = EDICT_TO_PROG (other);
	SetNativeBasis (matching);
	assert (!SV_VRWeaponShotBasis (matching));
	AssertNear (pr_global_struct->v_right[1], -1.0f);
	AssertNear (pr_global_struct->v_right[2], 0.0f);

	/* A player call with world angles must not borrow the weapon roll. */
	pr_global_struct->self = EDICT_TO_PROG (player);
	VectorCopy (world_angles, player->v.v_angle);
	SetNativeBasis (world_angles);
	assert (!SV_VRWeaponShotBasis (world_angles));
	AssertNear (pr_global_struct->v_right[0], 1.0f);
	AssertNear (pr_global_struct->v_right[1], 0.0f);
	AssertNear (pr_global_struct->v_right[2], 0.0f);

	/* A relocation invalidates this scope even if the player restores the
	 * same weapon angle before another makevectors call. */
	VectorCopy (matching, player->v.v_angle);
	scope.origin_relocated = true;
	SetNativeBasis (matching);
	assert (!SV_VRWeaponShotBasis (matching));
	AssertNear (pr_global_struct->v_right[1], -1.0f);
	AssertNear (pr_global_struct->v_right[2], 0.0f);

	puts ("VR shot roll scope: repeated player basis, world/invalidation rejection, 90-degree spread/source axes: ok");
	return 0;
}
