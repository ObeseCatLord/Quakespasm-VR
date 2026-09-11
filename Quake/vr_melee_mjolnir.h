/* Exact Mjolnir conventional axe/shadow-axe metadata.  Chainsaw and every
 * hybrid weapon stay outside this adapter; W_FireAxe remains authoritative for
 * corpse gibs, damage, Tome effects, blood and material effects. */
#ifndef QS_VR_MELEE_MJOLNIR_H
#define QS_VR_MELEE_MJOLNIR_H

enum {
	SV_VR_MJOLNIR_AXE_NONE,
	SV_VR_MJOLNIR_AXE_BASE,
	SV_VR_MJOLNIR_AXE_SHADOW
};

typedef struct {
	int crc, statements, functions, globals;
	int selector, selector_first, selector_parm, selector_locals;
	int attack, attack_first, attack_parm, attack_locals;
	int reload, reload_first, reload_parm;
	int leaf, leaf_first, leaf_parm, leaf_debug_trace, leaf_trace, leaf_gib_call;
	int gib, gib_first, gib_parm, gib_locals;
	int gib_find_first, gib_find_next, gib_normalize, gib_onflr, corpse_field;
	int stand, stand_first, run, run_first, cinematic, cutscene;
	const char *base_model, *old_base_model, *shadow_model;
} sv_vr_melee_mjolnir_descriptor_t;

static const sv_vr_melee_mjolnir_descriptor_t sv_vr_melee_mjolnir_descriptor = {
	43865, 331832, 21537, 34608,
	2708, 120280, 34390, 2,
	2713, 120949, 34390, 1,
	2673, 117052, 34390,
	2687, 118334, 34085, 118352, 118387, 118378,
	2680, 117482, 34390, 13,
	117485, 117538, 117500, 34400, 892,
	2781, 124783, 2782, 124840, 1489, 2575,
	"progs/v_axe.mdl", "progs/ad171/v_shadaxe0.mdl",
	"progs/ad171/v_shadaxe3.mdl"
};

static const sv_vr_melee_mjolnir_descriptor_t *SV_VRMeleeMjolnirDescriptor(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = &sv_vr_melee_mjolnir_descriptor;
	static const byte scalar[] = {1};
	static const byte entity_scalar[] = {1, 1};
	static const byte vector_scalar[] = {3, 1};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || qcvm->crc != d->crc ||
		qcvm->progs->numstatements != d->statements ||
		qcvm->progs->numfunctions != d->functions ||
		qcvm->progs->numglobals != d->globals)
		return NULL;
	if (!SV_VRMeleeFunctionPin(d->selector, "W_SetCurrentAmmo", d->selector_first,
		d->selector_parm, d->selector_locals, 2, entity_scalar) ||
		!SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first,
			d->attack_parm, d->attack_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->reload, "W_Reload", d->reload_first,
			d->reload_parm, 1, 1, scalar) ||
		!SV_VRMeleeFunctionPin(2674, "W_ShowHostile", 117073, 0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->leaf, "W_FireAxe", d->leaf_first,
			d->leaf_parm, 4, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->gib, "GibFloorBody", d->gib_first,
			d->gib_parm, d->gib_locals, 2, vector_scalar) ||
		!SV_VRMeleeFunctionPin(d->stand, "player_stand1", d->stand_first,
			0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first,
			0, 0, 0, NULL))
		return NULL;
	return d;
}

/* W_SetCurrentAmmo chooses these models after checking weapon bank.  The
 * chainsaw overrides either axe model and dispatches W_FireSaw, so it is an
 * explicit rejection even while self.weapon remains IT_AXE. */
static int SV_VRMeleeMjolnirAxeVariant(edict_t *player)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	eval_t *hackbank, *moditems, *perms;
	const char *model;
	int upgrade;

	if (!d || !player || player->free || player->v.weapon != 2048)
		return SV_VR_MJOLNIR_AXE_NONE;
	hackbank = GetEdictFieldValueByName(player, "weaponismoditems");
	moditems = GetEdictFieldValueByName(player, "moditems");
	perms = GetEdictFieldValueByName(player, "perms");
	if (!hackbank || !moditems || !perms || !isfinite(hackbank->_float) ||
		!isfinite(moditems->_float) || !isfinite(perms->_float) ||
		hackbank->_float != 0 || moditems->_float < 0 ||
		moditems->_float > 16777215 || perms->_float < 0 ||
		perms->_float > 16777215 || ((int)perms->_float & 2097152))
		return SV_VR_MJOLNIR_AXE_NONE;
	upgrade = ((int)moditems->_float & 2048) != 0;
	model = PR_GetString(player->v.weaponmodel);
	if (upgrade)
		return !strcmp(model, d->shadow_model) ? SV_VR_MJOLNIR_AXE_SHADOW :
			SV_VR_MJOLNIR_AXE_NONE;
	return !strcmp(model, d->base_model) || !strcmp(model, d->old_base_model) ?
		SV_VR_MJOLNIR_AXE_BASE : SV_VR_MJOLNIR_AXE_NONE;
}

static qboolean SV_VRMeleeMjolnirAxeReady(edict_t *player)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	eval_t *cooldown;
	float qctime;

	if (!d || SV_VRMeleeMjolnirAxeVariant(player) == SV_VR_MJOLNIR_AXE_NONE ||
		player->v.health <= 0 || player->v.deadflag || !isfinite(qcvm->time) ||
		(player->v.think != d->stand && player->v.think != d->run))
		return false;
	/* W_Attack itself refuses both camera/cutscene states before it schedules
	 * or plays an axe swing. Direct contact calls W_FireAxe, so retain those
	 * exact root guards rather than borrowing unrelated frame policy. */
	if (!isfinite(G_FLOAT(d->cinematic)) || !isfinite(G_FLOAT(d->cutscene)) ||
		G_FLOAT(d->cinematic) > 0 || G_FLOAT(d->cutscene) > 0)
		return false;
	cooldown = GetEdictFieldValueByName(player, "attack_finished");
	qctime = (float)qcvm->time;
	return cooldown && isfinite(cooldown->_float) && cooldown->_float <= qctime;
}

/* W_Attack's axe branch plays its swipe before W_Reload(.5), or .25 while
 * tome_finished is truthy.  Use the same rand threshold as the existing AD
 * adapter (and PF_random) then native W_Reload, preserving the chosen sound,
 * Haste, slow_finished and minimum delay without entering player_axe*. */
static qboolean SV_VRMeleeMjolnirAxePrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	eval_t *tome;
	float reload;

	if (!d || SV_VRMeleeMjolnirAxeVariant(player) == SV_VR_MJOLNIR_AXE_NONE ||
		!cooldown || !isfinite(cooldown->_float) || !isfinite(qcvm->time))
		return false;
	tome = GetEdictFieldValueByName(player, "tome_finished");
	if (!tome || !isfinite(tome->_float))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	SV_StartSound(player, 1, (rand() & 0x7fff) < 0x4000 ?
		"ad171/weapons/axe_swoosh1.wav" : "ad171/weapons/axe_swoosh2.wav",
		255, 1);
	reload = tome->_float != 0 ? .25f : .5f;
	G_FLOAT(OFS_PARM0) = reload;
	qcvm->argc = 1;
	PR_ExecuteProgram(&qcvm->functions[d->reload] - qcvm->functions);
	return SV_VRMeleeMjolnirAxeVariant(player) != SV_VR_MJOLNIR_AXE_NONE &&
		isfinite(cooldown->_float);
}

static dfunction_t *SV_VRMeleeMjolnirAxeLeafFunction(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	return d ? &qcvm->functions[d->leaf] : NULL;
}

static int SV_VRMeleeMjolnirAxeTraceStatement(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	return d ? d->leaf_trace : -1;
}

static int SV_VRMeleeMjolnirAxeDebugTraceStatement(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	return d ? d->leaf_debug_trace : -1;
}

/* Main may add these two predicates to its existing PF_Find/PF_normalize
 * contact gates.  They fence only W_FireAxe's immediate GibFloorBody prelude:
 * find(world,.bodyonflr,"TRUE") at 117485/117538 and normalize at
 * 117500, called from leaf statement 118378.  Thus an accepted contact can
 * select its own floor corpse, while nested medic/death queries stay native. */
static qboolean SV_VRMeleeMjolnirAxeGibScoped(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	prstack_t *caller;

	if (!d || !sv_vr_contact_call.active || !sv_vr_contact_call.player ||
		(sv_vr_contact_call.function != &qcvm->functions[d->leaf] &&
		 sv_vr_contact_call.function != &qcvm->functions[2682]) ||
		qcvm->xfunction != &qcvm->functions[d->gib] || qcvm->depth <= 0 ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
		return false;
	caller = &qcvm->stack[qcvm->depth - 1];
	return caller->f == sv_vr_contact_call.function &&
		((caller->f == &qcvm->functions[d->leaf] && caller->s == d->leaf_gib_call) ||
		 (caller->f == &qcvm->functions[2682] && caller->s == 117783 &&
		  qcvm->depth == sv_vr_contact_call.depth + 1));
}

static qboolean SV_VRMeleeMjolnirAxeGibFindSite(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();
	return d && SV_VRMeleeMjolnirAxeGibScoped() &&
		(qcvm->xstatement == d->gib_find_first || qcvm->xstatement == d->gib_find_next);
}

static qboolean SV_VRMeleeMjolnirAxeGibNormalizeSite(void)
{
	const sv_vr_melee_mjolnir_descriptor_t *d = SV_VRMeleeMjolnirDescriptor();

	/* GibFloorBody's local entity onflr is the vector+scalar argument footprint
	 * (four words) plus local vec/vec (six words): parm_start + 10 == 34400.
	 * Confirm it still is the accepted candidate before replacing only its
	 * desktop-facing normalize result. */
	return d && SV_VRMeleeMjolnirAxeGibScoped() &&
		qcvm->xstatement == d->gib_normalize && sv_vr_contact_call.trace.ent &&
		!sv_vr_contact_call.trace.ent->free &&
		G_INT(d->gib_onflr) == EDICT_TO_PROG(sv_vr_contact_call.trace.ent);
}

#endif /* QS_VR_MELEE_MJOLNIR_H */
