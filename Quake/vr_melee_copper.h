/* Exact Copper axe descriptors.  This header is included after
 * SV_VRMeleeFunctionPin and the shared sv_vr_contact_call declaration.  It
 * owns only the narrow native traceline2 bridge; original W_FireAxe remains
 * responsible for damage, knockback, leech, sounds and other mod effects. */
#ifndef QS_VR_MELEE_COPPER_H
#define QS_VR_MELEE_COPPER_H

typedef struct {
	int crc, statements, functions, globals;
	int set_ammo, set_ammo_first;
	int attack, attack_first, attack_parm, attack_locals;
	int frame, frame_first;
	int super_sound, super_sound_first;
	int whiff_sound, whiff_sound_first;
	int swing, swing_first, think, think_first, reverse, reverse_first;
	int cycle, cycle_first, cycle_parm;
	int leaf, leaf_first, leaf_parm;
	int helper, helper_first, helper_parm;
	int trace_statement, root_helper_call, helper_return;
	int stand, stand_first, run, run_first;
	const char *model;
} sv_vr_melee_copper_descriptor_t;

/* Values identify the effective mounted programs, not directory names.  The
 * helper's final local fraction is helper_parm + 15 in every reviewed VM;
 * the input ABI is (start, end, ignore, flags), so ignore/flags are +6/+7. */
static const sv_vr_melee_copper_descriptor_t sv_vr_melee_copper_descriptors[] = {
	{51436, 38590, 2705, 6440, 392, 9285, 399, 9508, 0, 0,
		403, 9840, 390, 9265, 404, 9852, 409, 9920, 407, 9910,
		408, 9915, 406, 9876, 5382, 410, 9937, 5383, 372, 8738,
		5266, 8778, 9951, 8899, 504, 14774, 505, 14811,
		"progs/v_axe2.mdl"},
	{51481, 41464, 3234, 7323, 392, 9425, 399, 9648, 0, 0,
		403, 9980, 390, 9405, 404, 9992, 409, 10060, 407, 10050,
		408, 10055, 406, 10016, 6188, 410, 10077, 6189, 368, 8748,
		6058, 8788, 10091, 8909, 504, 14871, 505, 14908,
		"progs/v_axe.mdl"},
	{50649, 42890, 3015, 7073, 395, 9763, 403, 10017, 0, 0,
		409, 10235, 393, 9738, 410, 10247, 415, 10315, 413, 10305,
		414, 10310, 412, 10271, 5783, 416, 10332, 5784, 375, 9211,
		5664, 9251, 10347, 9372, 512, 15238, 513, 15275,
		"progs/v_axe2.mdl"},
	{51614, 54290, 3389, 7777, 393, 9755, 400, 9978, 0, 0,
		404, 10338, 391, 9735, 405, 10350, 410, 10418, 408, 10408,
		409, 10413, 407, 10374, 6321, 411, 10435, 6322, 370, 8966,
		6186, 9006, 10449, 9127, 524, 15824, 525, 15861,
		"progs/v_axe2.mdl"},
	{41717, 43246, 2800, 6784, 396, 9626, 405, 9925, 5575, 1,
		411, 10165, 394, 9601, 412, 10177, 417, 10245, 415, 10235,
		416, 10240, 414, 10201, 5585, 418, 10262, 5586, 378, 9104,
		5479, 9142, 10277, 9263, 518, 15413, 519, 15450,
		"progs/v_axe2.mdl"}
};

static qboolean SV_VRMeleeCopperDescriptorValid(
	const sv_vr_melee_copper_descriptor_t *d)
{
	static const byte scalar[] = {1};
	static const byte trace_args[] = {3, 3, 1, 1};

	return qcvm && qcvm == &sv.qcvm && qcvm->progs &&
		qcvm->crc == d->crc && qcvm->progs->numstatements == d->statements &&
		qcvm->progs->numfunctions == d->functions &&
		qcvm->progs->numglobals == d->globals &&
		SV_VRMeleeFunctionPin(d->set_ammo, "W_SetCurrentAmmo", d->set_ammo_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first,
			d->attack_parm, d->attack_locals, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->frame, "W_WeaponFrame", d->frame_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->super_sound, "SuperDamageSound",
			d->super_sound_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->whiff_sound, "W_AxeWhiffSound",
			d->whiff_sound_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->swing, "W_AxeSwing", d->swing_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->think, "W_AxeThink", d->think_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->reverse, "W_AxeThinkReverse", d->reverse_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->cycle, "W_AxeCycle", d->cycle_first,
			d->cycle_parm, 1, 1, scalar) &&
		SV_VRMeleeFunctionPin(d->leaf, "W_FireAxe", d->leaf_first,
			d->leaf_parm, 9, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->helper, "traceline2", d->helper_first,
			d->helper_parm, 24, 4, trace_args) &&
		SV_VRMeleeFunctionPin(d->stand, "player_stand1", d->stand_first,
			0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first,
			0, 0, 0, NULL);
}

static const sv_vr_melee_copper_descriptor_t *SV_VRMeleeCopperDescriptor(void)
{
	int i;

	for (i = 0; i < (int)countof(sv_vr_melee_copper_descriptors); ++i)
		if (SV_VRMeleeCopperDescriptorValid(&sv_vr_melee_copper_descriptors[i]))
			return &sv_vr_melee_copper_descriptors[i];
	return NULL;
}

static qboolean SV_VRMeleeCopperSelected(edict_t *player)
{
	const sv_vr_melee_copper_descriptor_t *d = SV_VRMeleeCopperDescriptor();
	eval_t *customflags;

	if (!d || !player || player->free || !isfinite(player->v.items) ||
		player->v.weapon != 4096 || !((int)player->v.items & 4096))
		return false;
	customflags = GetEdictFieldValueByName(player, "customflags");
	return customflags && isfinite(customflags->_float) &&
		!((int)customflags->_float & 2112) &&
		!strcmp(PR_GetString(player->v.weaponmodel), d->model);
}

/* Standing and running loops are the only reviewed harmless states.  In
 * particular, a due W_AxeThink/W_AxeCycle remains an authored attack and is
 * never cancelled or borrowed for a physical outcome. */
static qboolean SV_VRMeleeCopperReady(edict_t *player)
{
	const sv_vr_melee_copper_descriptor_t *d = SV_VRMeleeCopperDescriptor();
	eval_t *cooldown;
	float qctime;

	if (!d || !SV_VRMeleeCopperSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(qcvm->time) ||
		(player->v.think != d->stand && player->v.think != d->run))
		return false;
	cooldown = GetEdictFieldValueByName(player, "attack_finished");
	qctime = (float)qcvm->time;
	return cooldown && isfinite(cooldown->_float) &&
		cooldown->_float <= qctime;
}

/* Keep W_WeaponFrame's quad sound and W_AxeSwing's whiff in their original
 * order, but do not call W_AxeSwing: it installs an authored animation/think
 * that would produce a second strike. */
static qboolean SV_VRMeleeCopperPrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_copper_descriptor_t *d = SV_VRMeleeCopperDescriptor();
	eval_t *hostile;

	if (!d || !SV_VRMeleeCopperSelected(player) || !cooldown ||
		!isfinite(cooldown->_float))
		return false;
	hostile = GetEdictFieldValueByName(player, "show_hostile");
	if (!hostile || !isfinite(qcvm->time))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[d->super_sound] - qcvm->functions);
	if (!SV_VRMeleeCopperSelected(player))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[d->whiff_sound] - qcvm->functions);
	if (!SV_VRMeleeCopperSelected(player))
		return false;
	hostile->_float = qcvm->time + 1;
	cooldown->_float = qcvm->time + .49f;
	return true;
}

static dfunction_t *SV_VRMeleeCopperLeafFunction(void)
{
	const sv_vr_melee_copper_descriptor_t *d = SV_VRMeleeCopperDescriptor();

	return d ? &qcvm->functions[d->leaf] : NULL;
}

/* This is called before the generic root-function trace path.  It accepts only
 * the initial W_FireAxe -> traceline2 native acquisition.  Helper filtering
 * stays entirely in QC: a filtered result gets a well-formed miss for each
 * qualified retry rather than tracing through the physical contact. */
static qboolean SV_VRMeleeCopperTrace(edict_t *ent, const vec3_t start,
	const vec3_t end, int nomonsters, trace_t *trace)
{
	const sv_vr_melee_copper_descriptor_t *d = SV_VRMeleeCopperDescriptor();
	prstack_t *caller;

	if (!d || !trace || !sv_vr_contact_call.active ||
		!sv_vr_contact_call.player || !SV_VRMeleeFiniteVector(start) ||
		!SV_VRMeleeFiniteVector(end) || nomonsters ||
		sv_vr_contact_call.function != &qcvm->functions[d->leaf] ||
		qcvm->xfunction != &qcvm->functions[d->helper] ||
		qcvm->xstatement != d->trace_statement || qcvm->depth <= 0 ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
		return false;
	caller = &qcvm->stack[qcvm->depth - 1];
	if (caller->f != sv_vr_contact_call.function ||
		caller->s != d->root_helper_call)
		return false;
	/* The native helper's initial ABI is (start, end, ignore, flags).  Its
	 * retries intentionally replace ignore with a filtered entity. */
	if (!sv_vr_contact_call.force_miss &&
		(G_INT(d->helper_parm + 6) != pr_global_struct->self ||
		G_FLOAT(d->helper_parm + 7) != 0 || ent != sv_vr_contact_call.player))
		return false;
	if (!sv_vr_contact_call.force_miss) {
		*trace = sv_vr_contact_call.trace;
		sv_vr_contact_call.force_miss = true;
		return true;
	}
	memset(trace, 0, sizeof(*trace));
	trace->fraction = 1;
	trace->inopen = true;
	trace->ent = qcvm->edicts;
	VectorCopy(end, trace->endpos);
	return true;
}

/* Call from the existing PR_LeaveFunction seam before helper locals restore.
 * Copper's helper converts an accepted physical endpoint to a desktop-ray
 * distance fraction.  Restore only the physical fraction after native QC has
 * kept that same entity/endpoint and its own final local says no retry. */
static qboolean SV_VRMeleeCopperLeaveFunction(void)
{
	const sv_vr_melee_copper_descriptor_t *d = SV_VRMeleeCopperDescriptor();
	const trace_t *contact;
	prstack_t *caller;

	if (!d || !sv_vr_contact_call.active || !sv_vr_contact_call.force_miss ||
		!sv_vr_contact_call.player || qcvm->xfunction != &qcvm->functions[d->helper] ||
		qcvm->xstatement != d->helper_return || qcvm->depth <= 0 ||
		sv_vr_contact_call.function != &qcvm->functions[d->leaf])
		return false;
	caller = &qcvm->stack[qcvm->depth - 1];
	if (caller->f != sv_vr_contact_call.function ||
		caller->s != d->root_helper_call ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player) ||
		G_INT(d->helper_parm + 6) != pr_global_struct->self ||
		G_FLOAT(d->helper_parm + 7) != 0 ||
		G_FLOAT(d->helper_parm + 15) != 1)
		return false;
	contact = &sv_vr_contact_call.trace;
	if (!contact->ent || contact->ent->free || !isfinite(contact->fraction) ||
		contact->fraction < 0 || contact->fraction >= 1 ||
		!SV_VRMeleeFiniteVector(contact->endpos) ||
		pr_global_struct->trace_startsolid || pr_global_struct->trace_allsolid ||
		pr_global_struct->trace_ent != EDICT_TO_PROG(contact->ent) ||
		pr_global_struct->trace_endpos[0] != contact->endpos[0] ||
		pr_global_struct->trace_endpos[1] != contact->endpos[1] ||
		pr_global_struct->trace_endpos[2] != contact->endpos[2])
		return false;
	pr_global_struct->trace_fraction = contact->fraction;
	return true;
}

#endif /* QS_VR_MELEE_COPPER_H */
