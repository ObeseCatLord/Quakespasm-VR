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

#endif
