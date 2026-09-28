/* The selected stock-QC PreThink handoff removes only movement already owned
 * by PMove. Keep the production helper in scope; discard unrelated server
 * sections at link time. */
#include "../Quake/sv_phys.c"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void near_velocity (const edict_t *player, float x, float y, float z)
{
	assert (fabsf (player->v.velocity[0] - x) < .001f);
	assert (fabsf (player->v.velocity[1] - y) < .001f);
	assert (fabsf (player->v.velocity[2] - z) < .001f);
}

static void callback_ownership (void)
{
	edict_t player = {0};
	client_t client = {0};
	float jump, waterjump;
	client.private_move_discontinuity_epoch = 5;
	player.v.flags = FL_WATERJUMP | FL_CLIENT | FL_JUMPRELEASED;
	player.v.teleport_time = 10;
	jump = .2f; waterjump = .7f;
	SV_PrivateWalkTrialWaterjumpCallbacks (&player, &client,
		(int)player.v.flags, 10, 5, &jump, &waterjump);
	assert (jump == .2f && waterjump == .7f && player.v.teleport_time == 10 &&
		((int)player.v.flags & FL_WATERJUMP));

	player.v.teleport_time = 11; // callback takes deadline ownership
	SV_PrivateWalkTrialWaterjumpCallbacks (&player, &client,
		(int)player.v.flags, 10, 5, &jump, &waterjump);
	assert (jump == .2f && waterjump == 0 && player.v.teleport_time == 11 &&
		(int)player.v.flags == (FL_CLIENT | FL_JUMPRELEASED));

	player.v.teleport_time = 10;
	waterjump = .7f; // flag-only callback releases the owned deadline
	SV_PrivateWalkTrialWaterjumpCallbacks (&player, &client,
		FL_WATERJUMP | FL_CLIENT, 10, 5, &jump, &waterjump);
	assert (jump == .2f && waterjump == 0 && player.v.teleport_time == 0);

	player.v.teleport_time = 10; player.v.flags = (int)player.v.flags | FL_WATERJUMP;
	waterjump = .7f; client.private_move_discontinuity_epoch = 6;
	SV_PrivateWalkTrialWaterjumpCallbacks (&player, &client,
		(int)player.v.flags, 10, 5, &jump, &waterjump);
	assert (jump == 0 && waterjump == 0 && player.v.teleport_time == 10 &&
		!((int)player.v.flags & FL_WATERJUMP)); // same-value authored teleport wins

	player.v.flags = (int)player.v.flags | FL_WATERJUMP;
	SV_PrivateWalkTrialWaterjumpCallbacks (&player, &client,
		FL_CLIENT, 10, 6, &jump, &waterjump);
	assert (waterjump == 0 && player.v.teleport_time == 10 &&
		!((int)player.v.flags & FL_WATERJUMP)); // a flag cannot manufacture a seed
}

int main (void)
{
	callback_ownership ();
	edict_t player = {0};
	const vec3_t before = {100, 0, 20};

	player.v.health = 100;
	VectorSet (player.v.velocity, 84, 0, 16.8f);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20);

	VectorSet (player.v.velocity, 89, 0, 23.8f);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 105, 0, 27); // retain an unrelated QC impulse

	VectorSet (player.v.velocity, 84, 0, 225);
	player.v.flags = FL_WATERJUMP;
	player.v.teleport_time = 102;
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20); // PMove owns the ledge impulse

	VectorClear (player.v.velocity);
	player.v.flags = 0;
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 0, 0, 0); // preserve a deliberate QC pause

	VectorSet (player.v.velocity, 92, 0, 18.4f);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 1,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20); // shallow-water drag is also QC-owned

	player.v.button2 = 1;
	player.v.watertype = CONTENTS_WATER;
	VectorSet (player.v.velocity, 84, 0, 100);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20); // remove overwrite, not imaginary z drag
	VectorSet (player.v.velocity, 89, 0, 107);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 105, 0, 27); // residual forces survive on both axes
	player.v.watertype = CONTENTS_SLIME;
	VectorSet (player.v.velocity, 76, 0, 80);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 3,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20);
	player.v.watertype = CONTENTS_LAVA;
	VectorSet (player.v.velocity, 76, 0, 50);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 3,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20);
	VectorClear (player.v.velocity);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 3,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 0, 0, 0); // swim input cannot undo a QC pause
	SV_PrivateWalkTrialReconcileQCWater (&player, vec3_origin, 0, 3,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 0, 0, 0); // including a player already at rest
	player.v.flags = FL_WATERJUMP;
	player.v.teleport_time = 102;
	VectorSet (player.v.velocity, 84, 0, 225);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f, player.v.velocity);
	near_velocity (&player, 100, 0, 20); // ledge takes precedence over swim

	puts ("Selected private water velocity handoff passed");
	return 0;
}
