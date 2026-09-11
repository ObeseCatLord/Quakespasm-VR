/* Mjolnir's Ghost Rapier keeps its native root/projectile combo.  A physical
 * contact may replace only RapierAttack's immediate trace and the root's
 * authored first animation state; all ammo, ghost, Tome and later combo work
 * remains in the installed QC. */
#ifndef QS_VR_MELEE_RAPIER_H
#define QS_VR_MELEE_RAPIER_H

typedef struct {
	int root, root_first, root_parm, root_locals;
	int animation, animation_first;
	int swipe, swipe_first;
	int attack, attack_first, attack_parm, attack_locals, attack_trace;
	int root_reload_call, root_animation_call;
	int attack_root_call;
} sv_vr_melee_rapier_descriptor_t;

static const sv_vr_melee_rapier_descriptor_t sv_vr_melee_rapier_descriptor = {
	2499, 113560, 34390, 1,
	2479, 113212,
	653, 10881,
	2494, 113296, 34010, 9, 113308,
	113561, 113562,
	121332
};

static qboolean SV_VRMeleeRapierProgs(void)
{
	const sv_vr_melee_rapier_descriptor_t *d = &sv_vr_melee_rapier_descriptor;

	if (!SV_VRMeleeMjolnirDescriptor() ||
		!SV_VRMeleeFunctionPin(d->root, "W_FireRapier", d->root_first,
			d->root_parm, d->root_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->animation, "RapierAnimation1",
			d->animation_first, 0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->swipe, "SwordSwipeSound", d->swipe_first,
			0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->attack, "RapierAttack", d->attack_first,
			d->attack_parm, d->attack_locals, 0, NULL))
		return false;
	return qcvm->statements[d->root_reload_call].op == OP_CALL1 &&
		G_INT((unsigned short)qcvm->statements[d->root_reload_call].a) == 2673 &&
		qcvm->statements[d->root_animation_call].op == OP_CALL0 &&
		G_INT((unsigned short)qcvm->statements[d->root_animation_call].a) ==
			d->animation &&
		qcvm->statements[d->animation_first + 3].op == OP_CALL0 &&
		G_INT((unsigned short)qcvm->statements[d->animation_first + 3].a) ==
			d->swipe &&
		qcvm->statements[d->attack_trace].op == OP_CALL4 &&
		G_INT((unsigned short)qcvm->statements[d->attack_trace].a) == 14 &&
		qcvm->statements[d->attack_root_call].op == OP_CALL0 &&
		G_INT((unsigned short)qcvm->statements[d->attack_root_call].a) == d->root;
}

static qboolean SV_VRMeleeRapierSelected(edict_t *player)
{
	eval_t *bank;

	if (!SV_VRMeleeRapierProgs() || !player || player->free ||
		player->v.weapon != 1024 ||
		strcmp(PR_GetString(player->v.weaponmodel), "progs/aoa/v_rapier.mdl"))
		return false;
	bank = GetEdictFieldValueByName(player, "weaponismoditems");
	return bank && bank->_float == 0;
}

/* Retain the same narrow normal-player/cinematic admission as the adjacent
 * Scimitar adapter.  Tome stays legal: it changes the native root's ghost
 * ammo gate and combo, not whether the held Rapier is selected. */
static qboolean SV_VRMeleeRapierIdle(edict_t *player)
{
	eval_t *vehicle, *tether, *blood, *tome;

	if (!SV_VRMeleeRapierSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(player->v.nextthink) ||
		(player->v.think != 2781 && player->v.think != 2782) ||
		!isfinite(G_FLOAT(1489)) || !isfinite(G_FLOAT(2575)) ||
		G_FLOAT(1489) > 0 || G_FLOAT(2575) > 0)
		return false;
	vehicle = GetEdictFieldValueByName(player, "in_a_vehicle");
	tether = GetEdictFieldValueByName(player, "tethered");
	blood = GetEdictFieldValueByName(player, "ammo_bloodcrystals");
	tome = GetEdictFieldValueByName(player, "tome_finished");
	return vehicle && vehicle->_float == 0 && tether && tether->_float == 0 &&
		blood && tome && isfinite(blood->_float) && isfinite(tome->_float);
}

static dfunction_t *SV_VRMeleeRapierRootFunction(void)
{
	return SV_VRMeleeRapierProgs() ?
		&qcvm->functions[sv_vr_melee_rapier_descriptor.root] : NULL;
}

static dfunction_t *SV_VRMeleeRapierAttackFunction(void)
{
	return SV_VRMeleeRapierProgs() ?
		&qcvm->functions[sv_vr_melee_rapier_descriptor.attack] : NULL;
}

static dfunction_t *SV_VRMeleeRapierFirstSwipeFunction(void)
{
	return SV_VRMeleeRapierProgs() ?
		&qcvm->functions[sv_vr_melee_rapier_descriptor.swipe] : NULL;
}

/* The root calls RapierAnimation1 only to STATE/weaponframe and play this
 * first swipe.  The central CALL hook may execute FirstSwipeFunction and
 * consume this CALL, leaving W_Reload, ammo use and ghost creation native. */
static qboolean SV_VRMeleeRapierAnimationCall(int function)
{
	const sv_vr_melee_rapier_descriptor_t *d = &sv_vr_melee_rapier_descriptor;

	return SV_VRMeleeRapierProgs() && sv_vr_contact_call.active &&
		sv_vr_contact_call.player && function == d->animation &&
		sv_vr_contact_call.function == &qcvm->functions[d->root] &&
		qcvm->xfunction == sv_vr_contact_call.function &&
		qcvm->depth == sv_vr_contact_call.depth &&
		qcvm->xstatement == d->root_animation_call &&
		pr_global_struct->self == EDICT_TO_PROG(sv_vr_contact_call.player);
}

/* RapierAttack is called directly only after the root completed.  Substitute
 * its one immediate traceline; blood, Shambler modifier, damage and wall FX
 * stay in the native function. */
static qboolean SV_VRMeleeRapierAttackTrace(edict_t *ent, const vec3_t start,
	const vec3_t end, int nomonsters, trace_t *trace)
{
	const sv_vr_melee_rapier_descriptor_t *d = &sv_vr_melee_rapier_descriptor;

	if (!SV_VRMeleeRapierProgs() || !trace || !sv_vr_contact_call.active ||
		!sv_vr_contact_call.player ||
		ent != sv_vr_contact_call.player || nomonsters ||
		sv_vr_contact_call.function != &qcvm->functions[d->attack] ||
		qcvm->xfunction != sv_vr_contact_call.function ||
		qcvm->depth != sv_vr_contact_call.depth ||
		qcvm->xstatement != d->attack_trace ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
		return false;
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite(start[axis]) || !isfinite(end[axis]))
			return false;
	if (sv_vr_contact_call.force_miss) {
		memset(trace, 0, sizeof(*trace));
		VectorCopy(end, trace->endpos);
		trace->fraction = 1;
		trace->ent = qcvm->edicts;
	} else
		*trace = sv_vr_contact_call.trace;
	sv_vr_contact_call.active = false;
	return true;
}

#endif /* QS_VR_MELEE_RAPIER_H */
