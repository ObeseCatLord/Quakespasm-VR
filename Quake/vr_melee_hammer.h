/* Hipnotic/MG3 physical hammer/Mjolnir seam.  This header is included after
 * SV_VRMeleeFunctionPin and sv_vr_contact_call.  It intentionally owns only
 * the immediate forward acquisition: the authored down trace remains native. */
#ifndef QS_VR_MELEE_HAMMER_H
#define QS_VR_MELEE_HAMMER_H

enum {
	SV_VR_MELEE_HAMMER_NONE,
	SV_VR_MELEE_HAMMER_ORDINARY,
	SV_VR_MELEE_HAMMER_MJOLNIR
};

typedef struct {
	int crc, statements, functions, globals;
	int selector, selector_first;
	int attack, attack_first, attack_parm;
	int frame, frame_first, super_sound, super_sound_first;
	int axe_leaf, axe_leaf_first, axe_leaf_parm, axe_leaf_locals, axe_trace;
	int leaf, leaf_first, leaf_parm, leaf_locals;
	int primary_trace, secondary_trace;
	int lightning, lightning_first, lightning_parm, lightning_locals;
	int floor_base, floor_base_first, floor_base_parm, floor_base_locals;
	int stand, stand_first, run, run_first, run_parm, run_locals;
	float hammer_recovery, mjolnir_recovery;
	const char *model, *glow_model;
} sv_vr_melee_hammer_descriptor_t;

/* Both roots dispatch weapon 128 to player_hammer1 below 30 cells and
 * player_mjolnir1 at 30+.  MG3's selector changes the held mesh to glow at
 * 15 cells, independently of that attack dispatch. */
static const sv_vr_melee_hammer_descriptor_t sv_vr_melee_hammer_descriptors[] = {
	{48616, 35474, 2808, 5610,
		236, 6013, 240, 6271, 4847, 255, 7025, 256, 7035,
		199, 4818, 4725, 6, 4828,
		196, 4678, 4718, 7, 4691, 4705,
		195, 4654, 4717, 1, 194, 4606, 4716, 1,
		298, 10028, 299, 10079, 4899, 2, .7f, .7f,
		"progs/v_hammer.mdl", NULL},
	{15606, 42238, 3563, 7704,
		243, 6100, 247, 6343, 6400, 256, 7251, 257, 7261,
		208, 5040, 6273, 6, 5050,
		3559, 42099, 7674, 10, 42112, 42166,
		3558, 42089, 0, 0, 3557, 42041, 7673, 1,
		323, 10852, 324, 10890, 0, 0, .4f, .6f,
		"progs/v_hammer.mdl", "progs/v_hammer_glow.mdl"}
};

static qboolean SV_VRMeleeHammerDescriptorValid(
	const sv_vr_melee_hammer_descriptor_t *d)
{
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->crc != d->crc || qcvm->progs->numstatements != d->statements ||
		qcvm->progs->numfunctions != d->functions ||
		qcvm->progs->numglobals != d->globals)
		return false;
	return SV_VRMeleeFunctionPin(d->selector, "W_SetCurrentAmmo",
		d->selector_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first,
			d->attack_parm, 1, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->frame, "W_WeaponFrame", d->frame_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->super_sound, "SuperDamageSound",
			d->super_sound_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->axe_leaf, "W_FireAxe", d->axe_leaf_first,
			d->axe_leaf_parm, d->axe_leaf_locals, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->leaf, "HIP_FireMjolnir", d->leaf_first,
			d->leaf_parm, d->leaf_locals, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->lightning, "HIP_FireMjolnirLightning",
			d->lightning_first, d->lightning_parm, d->lightning_locals,
			0, NULL) &&
		SV_VRMeleeFunctionPin(d->floor_base, "HIP_SpawnMjolnirBase",
			d->floor_base_first, d->floor_base_parm, d->floor_base_locals,
			0, NULL) &&
		SV_VRMeleeFunctionPin(d->stand, "player_stand1", d->stand_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first,
			d->run_parm, d->run_locals, 0, NULL);
}

static const sv_vr_melee_hammer_descriptor_t *SV_VRMeleeHammerDescriptor(void)
{
	int i;

	for (i = 0; i < (int)countof(sv_vr_melee_hammer_descriptors); ++i)
		if (SV_VRMeleeHammerDescriptorValid(&sv_vr_melee_hammer_descriptors[i]))
			return &sv_vr_melee_hammer_descriptors[i];
	return NULL;
}

static int SV_VRMeleeHammerVariant(edict_t *player)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();
	const char *model;

	if (!d || !player || player->free || !isfinite(player->v.weapon) ||
		!isfinite(player->v.ammo_cells) || player->v.ammo_cells < 0 ||
		player->v.weapon != 128)
		return SV_VR_MELEE_HAMMER_NONE;
	model = PR_GetString(player->v.weaponmodel);
	if (strcmp(model, d->model) && (!d->glow_model ||
		strcmp(model, d->glow_model)))
		return SV_VR_MELEE_HAMMER_NONE;
	return player->v.ammo_cells >= 30 ? SV_VR_MELEE_HAMMER_MJOLNIR :
		SV_VR_MELEE_HAMMER_ORDINARY;
}

static qboolean SV_VRMeleeHammerSelected(edict_t *player)
{
	return SV_VRMeleeHammerVariant(player) != SV_VR_MELEE_HAMMER_NONE;
}

/* player_stand1 and player_run are self-looping native idle/locomotion
 * functions in both pinned VMs.  Any hammer/mjolnir frame is an authored
 * stroke and remains ineligible rather than being cancelled or borrowed. */
static qboolean SV_VRMeleeHammerReady(edict_t *player)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();
	int variant = SV_VRMeleeHammerVariant(player);
	eval_t *cooldown;

	if (!d || variant == SV_VR_MELEE_HAMMER_NONE || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(qcvm->time) ||
		(player->v.think != d->stand && player->v.think != d->run))
		return false;
	cooldown = GetEdictFieldValueByName(player, "attack_finished");
	return cooldown && isfinite(cooldown->_float) &&
		cooldown->_float <= (float)qcvm->time;
}

/* W_WeaponFrame calls SuperDamageSound before W_Attack.  W_Attack then sets
 * show_hostile=time+1 and enters player_mjolnir1.  Calling that frame
 * schedules a second strike, so retain only its exact non-scheduling effects.
 * Recovery is the original leaf's final clean-miss value: Hip is .7 for both
 * roots; MG3 is .4 ordinary / .6 Mjolnir. A retained direct leaf still
 * replaces it with its own hit value. No native hammer sound is skipped: its
 * first authored frame is only STATE/weaponframe. */
static qboolean SV_VRMeleeHammerPrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();
	eval_t *hostile;
	int variant = SV_VRMeleeHammerVariant(player);

	if (!d || variant == SV_VR_MELEE_HAMMER_NONE || !cooldown ||
		!isfinite(cooldown->_float) || !isfinite(qcvm->time))
		return false;
	hostile = GetEdictFieldValueByName(player, "show_hostile");
	if (!hostile || !isfinite(hostile->_float))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[d->super_sound] - qcvm->functions);
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || player->free ||
		SV_VRMeleeHammerVariant(player) != variant)
		return false;
	hostile->_float = qcvm->time + 1.f;
	cooldown->_float = qcvm->time + (variant == SV_VR_MELEE_HAMMER_MJOLNIR ?
		d->mjolnir_recovery : d->hammer_recovery);
	return true;
}

static dfunction_t *SV_VRMeleeHammerLeafForPlayer(edict_t *player)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();
	int variant = SV_VRMeleeHammerVariant(player);

	if (!d)
		return NULL;
	return variant == SV_VR_MELEE_HAMMER_MJOLNIR ? &qcvm->functions[d->leaf] :
		variant == SV_VR_MELEE_HAMMER_ORDINARY ? &qcvm->functions[d->axe_leaf] : NULL;
}

/* Called before the generic direct-leaf bridge.  It substitutes exactly one
 * physical acquisition and drops the scope immediately, so the later native
 * down trace (4705 / 42166) and all nested gameplay traces stay untouched. */
static qboolean SV_VRMeleeHammerPrimaryTrace(edict_t *ent, const vec3_t start,
	const vec3_t end, int nomonsters, trace_t *trace)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();
	int variant;
	dfunction_t *leaf;
	int statement;

	variant = SV_VRMeleeHammerVariant(sv_vr_contact_call.player);
	leaf = !d ? NULL : variant == SV_VR_MELEE_HAMMER_MJOLNIR ?
		&qcvm->functions[d->leaf] : variant == SV_VR_MELEE_HAMMER_ORDINARY ?
		&qcvm->functions[d->axe_leaf] : NULL;
	statement = !d ? -1 : variant == SV_VR_MELEE_HAMMER_MJOLNIR ?
		d->primary_trace : d->axe_trace;

	if (!d || !trace || !sv_vr_contact_call.active ||
		sv_vr_contact_call.force_miss || !sv_vr_contact_call.player ||
		!SV_VRMeleeFiniteVector(start) || !SV_VRMeleeFiniteVector(end) ||
		ent != sv_vr_contact_call.player || nomonsters ||
		!leaf || sv_vr_contact_call.function != leaf ||
		qcvm->xfunction != sv_vr_contact_call.function ||
		qcvm->depth != sv_vr_contact_call.depth ||
		qcvm->xstatement != statement ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
		return false;
	*trace = sv_vr_contact_call.trace;
	sv_vr_contact_call.active = false;
	return true;
}

/* Floor-route metadata only: the zero-argument lightning wrapper consumes
 * global trace_endpos and calls the zero-argument floor-base builder.  A
 * caller may use it only after setting trace_endpos to a verified physical
 * floor contact and preserving the accepted hand v_angle/v_forward scope.
 * The base alone is deliberately not an API: it skips the wrapper's native
 * cells/water policy. */
static qboolean SV_VRMeleeHammerFloorContact(edict_t *player,
	const trace_t *contact)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();

	return d && d->crc == 48616 &&
		SV_VRMeleeHammerVariant(player) == SV_VR_MELEE_HAMMER_MJOLNIR &&
		contact && contact->ent && !contact->ent->free &&
		contact->ent->v.solid == SOLID_BSP && isfinite(contact->fraction) &&
		contact->fraction >= 0 && contact->fraction < 1 &&
		SV_VRMeleeFiniteVector(contact->endpos) &&
		SV_VRMeleeFiniteVector(contact->plane.normal) &&
		contact->plane.normal[2] > .707f;
}

/* Called only after SV_VRMeleeHammerFloorContact.  The outcome seam already
 * writes the accepted trace globals inside the borrowed hand-angle context;
 * leave all native cells/water policy to this zero-argument wrapper. */
static dfunction_t *SV_VRMeleeHammerFloorFunction(void)
{
	const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();

	if (!d || d->crc != 48616)
		return NULL;
	return &qcvm->functions[d->lightning];
}

#endif
