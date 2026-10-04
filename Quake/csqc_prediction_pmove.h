/* Stateless CSQC adapter for the shared PMove scratch state. */
#ifndef QUAKE_CSQC_PREDICTION_PMOVE_H
#define QUAKE_CSQC_PREDICTION_PMOVE_H

#include "csqc_prediction.h"

static qboolean csqc_prediction_pmove_active;

static inline qboolean CSQCPrediction_ValidBounds (const vec3_t mins, const vec3_t maxs)
{
	return CSQCPrediction_FiniteVector (mins) && CSQCPrediction_FiniteVector (maxs) &&
		mins[0] <= maxs[0] && mins[1] <= maxs[1] && mins[2] <= maxs[2];
}

static inline qboolean CSQCPrediction_ValidInputGlobals (void)
{
	unsigned int ignored_unsigned;
	int ignored_int;

	if ((qcvm->extglobals.input_sequence &&
		 !CSQCPrediction_FloatToUInt (*qcvm->extglobals.input_sequence, &ignored_unsigned)) ||
		(qcvm->extglobals.input_servertime && !isfinite (*qcvm->extglobals.input_servertime)) ||
		(qcvm->extglobals.input_timelength &&
		 (!isfinite (*qcvm->extglobals.input_timelength) || *qcvm->extglobals.input_timelength < 0 ||
		  *qcvm->extglobals.input_timelength > 0.5f)) ||
		(qcvm->extglobals.input_angles && !CSQCPrediction_FiniteVector (qcvm->extglobals.input_angles)) ||
		(qcvm->extglobals.input_movevalues && !CSQCPrediction_FiniteVector (qcvm->extglobals.input_movevalues)) ||
		(qcvm->extglobals.input_buttons &&
		 !CSQCPrediction_FloatToUInt (*qcvm->extglobals.input_buttons, &ignored_unsigned)) ||
		(qcvm->extglobals.input_impulse &&
		 !CSQCPrediction_FloatToUInt (*qcvm->extglobals.input_impulse, &ignored_unsigned)) ||
		(qcvm->extglobals.input_cursor_screen &&
		 (!isfinite (qcvm->extglobals.input_cursor_screen[0]) ||
		  !isfinite (qcvm->extglobals.input_cursor_screen[1]))) ||
		(qcvm->extglobals.input_cursor_trace_start &&
		 !CSQCPrediction_FiniteVector (qcvm->extglobals.input_cursor_trace_start)) ||
		(qcvm->extglobals.input_cursor_trace_endpos &&
		 !CSQCPrediction_FiniteVector (qcvm->extglobals.input_cursor_trace_endpos)) ||
		(qcvm->extglobals.input_cursor_entitynumber &&
		 !CSQCPrediction_FloatToInt (*qcvm->extglobals.input_cursor_entitynumber, &ignored_int)))
		return false;
	return true;
}

static inline qboolean CSQCPrediction_ValidEntityReference (int reference)
{
	return reference >= 0 && qcvm->edict_size > 0 &&
		reference % qcvm->edict_size == 0 && reference / qcvm->edict_size < qcvm->num_edicts;
}

static qboolean CSQCPrediction_AddLinkedSolids (edict_t *ignore, areanode_t *node,
	const vec3_t bounds[2])
{
	link_t *link, *next;

	if (!node || node->axis < -1 || node->axis > 2)
		return false;
	for (link = node->solid_edicts.next; link != &node->solid_edicts; link = next)
	{
		edict_t *other = EDICT_FROM_AREA (link);
		physent_t *phys;
		int solid, modelindex;

		next = link->next;
		if (other == ignore || other->free)
			continue;
		if (!CSQCPrediction_FloatToInt (other->v.solid, &solid) || !isfinite (other->v.skin))
			return false;
		if (solid == SOLID_NOT || solid == SOLID_TRIGGER)
			continue;
		if (solid != SOLID_BBOX && solid != SOLID_SLIDEBOX && solid != SOLID_BSP)
			return false;
		if (!CSQCPrediction_FiniteVector (other->v.origin) ||
			!CSQCPrediction_ValidBounds (other->v.mins, other->v.maxs) ||
			!CSQCPrediction_FiniteVector (other->v.angles) ||
			!CSQCPrediction_ValidBounds (other->v.absmin, other->v.absmax))
			return false;
		if (bounds[0][0] > other->v.absmax[0] || bounds[0][1] > other->v.absmax[1] ||
			bounds[0][2] > other->v.absmax[2] || bounds[1][0] < other->v.absmin[0] ||
			bounds[1][1] < other->v.absmin[1] || bounds[1][2] < other->v.absmin[2])
			continue;
		if (ignore->v.size[0] && !other->v.size[0])
			continue;
		if ((CSQCPrediction_ValidEntityReference (other->v.owner) &&
			 PROG_TO_EDICT (other->v.owner) == ignore) ||
			(CSQCPrediction_ValidEntityReference (ignore->v.owner) &&
			 PROG_TO_EDICT (ignore->v.owner) == other))
			continue;
		if (pmove.numphysent >= MAX_PHYSENTS)
			return false;
		phys = &pmove.physents[pmove.numphysent];
		memset (phys, 0, sizeof (*phys));
		phys->info = NUM_FOR_EDICT (other);
		if (solid == SOLID_BSP)
		{
			if (!CSQCPrediction_FloatToInt (other->v.modelindex, &modelindex) ||
				modelindex < 1 || modelindex >= MAX_MODELS || !qcvm->GetModel)
				return false;
			phys->model = qcvm->GetModel (modelindex);
			if (!phys->model || phys->model->needload || phys->model->type != mod_brush)
				return false;
			phys->modelindex = (unsigned int)modelindex;
		}
		VectorCopy (other->v.origin, phys->origin);
		VectorCopy (other->v.mins, phys->mins);
		VectorCopy (other->v.maxs, phys->maxs);
		VectorCopy (other->v.angles, phys->angles);
		if (other->v.skin == CONTENTS_WATER) phys->forcecontentsmask = CONTENTBIT_WATER;
		else if (other->v.skin == CONTENTS_LAVA) phys->forcecontentsmask = CONTENTBIT_LAVA;
		else if (other->v.skin == CONTENTS_SLIME) phys->forcecontentsmask = CONTENTBIT_SLIME;
		else if (other->v.skin == CONTENTS_SKY) phys->forcecontentsmask = CONTENTBIT_SKY;
		else if (other->v.skin == CONTENTS_CLIP) phys->forcecontentsmask = CONTENTBIT_CLIP;
		else if (other->v.skin == CONTENTS_LADDER) phys->forcecontentsmask = CONTENTBIT_LADDER;
		pmove.numphysent++;
	}
	if (node->axis == -1)
		return true;
	if (bounds[1][node->axis] >= node->dist &&
		(!node->children[0] || !CSQCPrediction_AddLinkedSolids (ignore, node->children[0], bounds)))
		return false;
	if (bounds[0][node->axis] <= node->dist &&
		(!node->children[1] || !CSQCPrediction_AddLinkedSolids (ignore, node->children[1], bounds)))
		return false;
	return true;
}

/* PMCL_AddEntities marks engine-network contacts negative. Keep those out of
 * QC while retaining each positive CSQC entity before any callback can free
 * it or make its numbered slot reusable. */
static qboolean CSQCPrediction_CaptureContacts (edict_t *entity,
	edict_t *contacts[MAX_PHYSENTS], int contact_numbers[MAX_PHYSENTS],
	int *contact_count)
{
	int i;

	if (pmove.numtouch < 0 || pmove.numtouch > MAX_PHYSENTS ||
		pmove.numphysent < 1 || pmove.numphysent > MAX_PHYSENTS)
		return false;
	for (i = 0; i < pmove.numtouch; ++i)
	{
		int index = pmove.touchindex[i];
		int number;
		edict_t *other;

		if (index < 0 || index >= pmove.numphysent)
			return false;
		number = pmove.physents[index].info;
		if (number <= 0) /* world and engine-network entities have no CSQC edict */
			continue;
		if (number >= qcvm->num_edicts || *contact_count >= MAX_PHYSENTS)
			return false;
		other = EDICT_NUM (number);
		if (other == entity || other->free)
			continue;
		contacts[*contact_count] = other;
		contact_numbers[*contact_count] = number;
		ED_Retain (other);
		++*contact_count;
	}
	return true;
}

static qboolean CSQCPrediction_RunPMove (edict_t *entity)
{
	playermove_t saved_pmove;
	movevars_t saved_movevars;
	vec3_t bounds[2];
	edict_t *contacts[MAX_PHYSENTS];
	int contact_numbers[MAX_PHYSENTS];
	eval_t *pmflags;
	int flags, movetype, pmflag_bits = 0, number, entity_number, contact_count = 0;
	qboolean mover_retained = false;
	qboolean result = false;

	if (csqc_prediction_pmove_active || qcvm != &cl.qcvm || !entity || entity->free ||
		!qcvm->worldmodel || qcvm->worldmodel->needload || qcvm->worldmodel->type != mod_brush ||
		!cl.worldmodel || cl.worldmodel->needload || cl.worldmodel->type != mod_brush ||
		qcvm->numareanodes <= 0 || qcvm->numareanodes > AREA_NODES ||
		!CSQCPrediction_FiniteVector (entity->v.origin) ||
		!CSQCPrediction_FiniteVector (entity->v.velocity) ||
		!CSQCPrediction_FiniteVector (entity->v.angles) ||
		!CSQCPrediction_ValidBounds (entity->v.mins, entity->v.maxs) ||
		!isfinite (entity->v.teleport_time) || !isfinite (qcvm->time) ||
		!CSQCPrediction_FloatToInt (entity->v.flags, &flags) ||
		!CSQCPrediction_FloatToInt (entity->v.movetype, &movetype) ||
		!CSQCPrediction_ValidInputGlobals ())
		return false;
	for (int axis = 0; axis < 3; axis++)
	{
		bounds[0][axis] = entity->v.origin[axis] + entity->v.mins[axis] - 256.0f;
		bounds[1][axis] = entity->v.origin[axis] + entity->v.maxs[axis] + 256.0f;
	}
	if (!CSQCPrediction_ValidBounds (bounds[0], bounds[1]))
		return false;
	pmflags = GetEdictFieldValue (entity, qcvm->extfields.pmove_flags);
	if (pmflags && !CSQCPrediction_FloatToInt (pmflags->_float, &pmflag_bits))
		return false;
	entity_number = NUM_FOR_EDICT (entity);

	saved_pmove = pmove;
	saved_movevars = movevars;
	csqc_prediction_pmove_active = true;
	memset (&pmove, 0, sizeof (pmove));
	if (!PMCL_SetMoveVars ())
		goto done;
	PMCL_AddEntities (bounds);
	pmove.skipent = NUM_FOR_EDICT (entity);
	if (!CSQCPrediction_AddLinkedSolids (entity, qcvm->areanodes, bounds))
		goto done;
	PR_GetSetInputs (&pmove.cmd, false);
	VectorCopy (entity->v.mins, pmove.player_mins);
	VectorCopy (entity->v.maxs, pmove.player_maxs);
	VectorCopy (entity->v.origin, pmove.origin);
	VectorCopy (entity->v.velocity, pmove.velocity);
	VectorCopy (entity->v.angles, pmove.angles);
	VectorClear (pmove.gravitydir);
	pmove.waterjumptime = entity->v.teleport_time > qcvm->time ?
		entity->v.teleport_time - qcvm->time : 0.0f;
	pmove.jump_held = (pmflag_bits & PMF_JUMP_HELD) != 0;
	pmove.onladder = (pmflag_bits & PMF_LADDER) != 0;
	pmove.onground = (flags & FL_ONGROUND) != 0;
	switch (movetype)
	{
	case MOVETYPE_WALK: pmove.pm_type = PM_NORMAL; break;
	case MOVETYPE_BOUNCE: pmove.pm_type = PM_DEAD; break;
	case MOVETYPE_FLY: pmove.pm_type = PM_FLY; break;
	case MOVETYPE_NOCLIP: pmove.pm_type = PM_SPECTATOR; break;
	default: pmove.pm_type = PM_NONE; break;
	}
	PM_PlayerMove (1.0f);
	if (!CSQCPrediction_FiniteVector (pmove.origin) || !CSQCPrediction_FiniteVector (pmove.velocity) ||
		!isfinite (pmove.waterjumptime) || pmove.waterjumptime < 0 ||
		(pmove.onground && (pmove.groundent < 0 || pmove.groundent >= pmove.numphysent)))
		goto done;
	ED_Retain (entity);
	mover_retained = true;
	if (!CSQCPrediction_CaptureContacts (entity, contacts, contact_numbers, &contact_count))
		goto done;
	VectorCopy (entity->v.origin, entity->v.oldorigin);
	VectorCopy (pmove.origin, entity->v.origin);
	VectorCopy (pmove.velocity, entity->v.velocity);
	entity->v.teleport_time = pmove.waterjumptime > 0.0f ? qcvm->time + pmove.waterjumptime : 0.0f;
	if (pmove.jump_held && movevars.autobunny)
		entity->v.flags = flags | FL_JUMPRELEASED;
	if (pmove.onground)
	{
		entity->v.flags = (int)entity->v.flags | FL_ONGROUND;
		number = pmove.physents[pmove.groundent].info;
		if (number > 0 && number >= qcvm->num_edicts)
			goto done;
		entity->v.groundentity = number > 0 ? EDICT_TO_PROG (EDICT_NUM (number)) : 0;
	}
	else
	{
		entity->v.flags = (int)entity->v.flags & ~FL_ONGROUND;
		entity->v.groundentity = 0;
	}
	entity->v.waterlevel = pmove.waterlevel;
	entity->v.watertype = (pmove.watertype & CONTENTBIT_SOLID) ? CONTENTS_SOLID :
		(pmove.watertype & CONTENTBIT_SKY) ? CONTENTS_SKY :
		(pmove.watertype & CONTENTBIT_LAVA) ? CONTENTS_LAVA :
		(pmove.watertype & CONTENTBIT_SLIME) ? CONTENTS_SLIME :
		(pmove.watertype & CONTENTBIT_WATER) ? CONTENTS_WATER : CONTENTS_EMPTY;
	if (pmflags)
		pmflags->_float = (pmflag_bits & ~(PMF_JUMP_HELD | PMF_LADDER)) |
			(pmove.jump_held ? PMF_JUMP_HELD : 0) | (pmove.onladder ? PMF_LADDER : 0);
	result = true;
done:
	pmove = saved_pmove;
	movevars = saved_movevars;
	csqc_prediction_pmove_active = false;
	/* Donor #347 links and dispatches triggers after movement. Do it only after
	 * restoring the shared scratch state: a touch callback may legally recurse
	 * into #347, and it must never inherit this call's PMove context. */
	if (result && !entity->free)
	{
		SV_LinkEdict (entity, true);
		if (!entity->free)
			SV_DispatchCSQCPMoveImpacts (entity, entity_number, contacts,
				contact_numbers, contact_count);
	}
	for (int i = contact_count - 1; i >= 0; --i)
		ED_Release (contacts[i]);
	if (mover_retained)
		ED_Release (entity);
	return result;
}

#endif /* QUAKE_CSQC_PREDICTION_PMOVE_H */
