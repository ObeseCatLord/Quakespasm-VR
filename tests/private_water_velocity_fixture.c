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

int main (void)
{
	edict_t player = {0};
	const vec3_t before = {100, 0, 20};

	player.v.health = 100;
	VectorSet (player.v.velocity, 84, 0, 16.8f);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f);
	near_velocity (&player, 100, 0, 20);

	VectorSet (player.v.velocity, 89, 0, 23.8f);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f);
	near_velocity (&player, 105, 0, 27); // retain an unrelated QC impulse

	VectorSet (player.v.velocity, 84, 0, 225);
	player.v.flags = FL_WATERJUMP;
	player.v.teleport_time = 102;
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f);
	near_velocity (&player, 100, 0, 20); // PMove owns the ledge impulse

	VectorClear (player.v.velocity);
	player.v.flags = 0;
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 2,
		100, 0, .1f);
	near_velocity (&player, 0, 0, 0); // preserve a deliberate QC pause

	VectorSet (player.v.velocity, 92, 0, 18.4f);
	SV_PrivateWalkTrialReconcileQCWater (&player, before, 0, 1,
		100, 0, .1f);
	near_velocity (&player, 100, 0, 20); // shallow-water drag is also QC-owned

	puts ("Selected private water velocity handoff passed");
	return 0;
}
