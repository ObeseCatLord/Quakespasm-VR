/* Shared real-BSP fixture lookup, extracted unchanged from the stock liquid
 * driver. Include after the production sv_phys.c owner. No QC/callbacks. */
#ifndef QSVR_NATIVE_LIQUID_FIXTURE_H
#define QSVR_NATIVE_LIQUID_FIXTURE_H

static qboolean FindLiquidPositionAtOrdinal (edict_t *player, int contents, int depth,
	unsigned ordinal, vec3_t found)
{
	qmodel_t *world = qcvm->worldmodel;
	vec3_t saved_origin;
	float saved_level = player->v.waterlevel, saved_type = player->v.watertype;
	qboolean located = false;
	VectorCopy (player->v.origin, saved_origin);
	for (int leafnum = 1; leafnum <= world->numleafs && !located; ++leafnum)
	{
		mleaf_t *leaf = &world->leafs[leafnum];
		if (leaf->contents != contents) continue;
		for (int z = 0; z < 9 && !located; ++z)
			for (int x = 1; x < 4 && !located; ++x)
				for (int y = 1; y < 4 && !located; ++y)
				{
					vec3_t position = {
						leaf->minmaxs[0] + (leaf->minmaxs[3] - leaf->minmaxs[0]) * x / 4.0f,
						leaf->minmaxs[1] + (leaf->minmaxs[4] - leaf->minmaxs[1]) * y / 4.0f,
						leaf->minmaxs[5] + 16.0f - z * 8.0f};
					VectorCopy (position, player->v.origin);
					SV_CheckWater (player);
					if (player->v.waterlevel != depth || player->v.watertype != contents || SV_TestEntityPosition (player))
						continue;
					if (ordinal)
					{
						--ordinal;
						continue;
					}
					VectorCopy (position, found);
					located = true;
				}
	}
	VectorCopy (saved_origin, player->v.origin);
	player->v.waterlevel = saved_level;
	player->v.watertype = saved_type;
	return located;
}

static qboolean FindLiquidPosition (edict_t *player, int contents, int depth, vec3_t found)
{
	return FindLiquidPositionAtOrdinal (player, contents, depth, 0, found);
}

/* Select a reachable neighbor of samples from the one shared BSP finder.
 * Starts are explicitly prepared airborne bodies, not grounded shoreline play.
 * The real auxiliary sweep must actually cross; a wet point alone is not proof. */
static inline qboolean FindRoomScaleLiquidEntryBounded (edict_t *player, client_t *client,
	usercmd_t *command, vec3_t dry, float max_distance)
{
	const size_t vars_size = qcvm->progs->entityfields * sizeof (float);
	void *vars = Mem_Alloc (vars_size);
	memcpy (vars, &player->v, vars_size);
	const usercmd_t saved = client->cmd;
	const double frame_time = host_frametime;
	const int liquids[] = {CONTENTS_WATER, CONTENTS_SLIME, CONTENTS_LAVA};
	const float distances[] = {8, 16, 32, 48, 64};
	qboolean located = false;
	unsigned wet_count = 0, dry_count = 0;
	for (int content = 0; content < countof (liquids) && !located; ++content)
		for (int depth = 1; depth <= 2 && !located; ++depth)
			for (unsigned ordinal = 0; ordinal < 2048 && !located; ++ordinal)
			{
				vec3_t wet;
				if (!FindLiquidPositionAtOrdinal (player, liquids[content], depth, ordinal, wet))
					break;
				++wet_count;
				for (int direction = 0; direction < 8 && !located; ++direction)
					for (int distance = 0; distance < countof (distances) && !located; ++distance)
					{
						if (distances[distance] > max_distance) continue;
						vec3_t axis, right, up;
						AngleVectors ((vec3_t){0, direction * 45.0f, 0}, axis, right, up);
						SV_UnlinkEdict (player);
						memcpy (&player->v, vars, vars_size);
						VectorMA (wet, distances[distance], axis, player->v.origin);
						VectorClear (player->v.velocity);
						player->v.flags = ((int)player->v.flags | FL_JUMPRELEASED) & ~(FL_ONGROUND | FL_WATERJUMP);
						player->v.groundentity = 0;
						SV_LinkEdict (player, false);
						SV_CheckWater (player);
						if (player->v.waterlevel || SV_TestEntityPosition (player))
							continue;
						++dry_count;
						VectorCopy (player->v.origin, dry);
						memset (command, 0, sizeof (*command));
						command->sequence = 1;
						command->msec = 8;
						command->seconds = .008f;
						command->vr_active = command->vr_handpos_relative = true;
						VectorCopy (player->v.v_angle, command->viewangles);
						VectorSubtract (wet, dry, command->vr_roomscalemove);
						client->cmd = *command;
						host_frametime = command->seconds;
						SV_ApplyPrivateRoomScaleMove (player, client);
						SV_CheckWater (player);
						if (player->v.waterlevel && player->v.watertype == liquids[content] &&
							!SV_TestEntityPosition (player))
						{
							located = true;
							printf ("Q30_ROOM_ENTRY_FOUND content=%d depth=%d ordinal=%u distance=%.0f dry=(%.1f %.1f %.1f) wet=(%.1f %.1f %.1f)\n",
								liquids[content], depth, ordinal, distances[distance], dry[0], dry[1], dry[2],
								player->v.origin[0], player->v.origin[1], player->v.origin[2]);
						}
					}
			}
	SV_UnlinkEdict (player);
	memcpy (&player->v, vars, vars_size);
	SV_LinkEdict (player, false);
	client->cmd = saved;
	host_frametime = frame_time;
	Mem_Free (vars);
	if (!located)
		printf ("Q30_ROOM_ENTRY_NOT_FOUND wet=%u dry=%u\n", wet_count, dry_count);
	return located;
}

static inline qboolean FindRoomScaleLiquidEntry (edict_t *player, client_t *client,
	usercmd_t *command, vec3_t dry)
{
	return FindRoomScaleLiquidEntryBounded (player, client, command, dry, 64);
}

#endif
