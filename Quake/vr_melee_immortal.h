/* Exact Immortal conventional axe/hammer entry metadata.  The direct leaves
 * remain authoritative for damage, breakables, blood and every native effect. */
#ifndef QS_VR_MELEE_IMMORTAL_H
#define QS_VR_MELEE_IMMORTAL_H

enum {
	SV_VR_IMMORTAL_NONE,
	SV_VR_IMMORTAL_AXE,
	SV_VR_IMMORTAL_HAMMER
};

typedef struct {
	int crc, statements, functions, globals;
	int selector, selector_first, selector_parm;
	int attack, attack_first, attack_parm, attack_locals;
	int frame, frame_first, frame_parm;
	int super_sound, super_sound_first, super_sound_parm;
	int axe, axe_first, axe_parm, axe_trace;
	int hammer, hammer_first, hammer_parm, hammer_trace;
	int stand, stand_first, stand_parm;
	int run, run_first, run_parm;
	int cheat, cheat_first, cheat_parm;
	int touch, touch_first, touch_parm;
	int item, item_first, item_parm;
} sv_vr_melee_immortal_descriptor_t;

static const sv_vr_melee_immortal_descriptor_t sv_vr_melee_immortal_descriptor = {
	35659, 50528, 4157, 12809,
	269, 7608, 2192,
	272, 7818, 2212, 1,
	293, 8609, 2294,
	294, 8619, 2295,
	229, 5824, 1910, 5834,
	228, 5735, 1901, 5745,
	347, 11967, 2812,
	348, 12018, 2813,
	284, 8371, 2251,
	2372, 30781, 7952,
	2373, 30848, 7962
};

static const sv_vr_melee_immortal_descriptor_t *SV_VRMeleeImmortalDescriptor(void)
{
	const sv_vr_melee_immortal_descriptor_t *d = &sv_vr_melee_immortal_descriptor;

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || qcvm->crc != d->crc ||
		qcvm->progs->numstatements != d->statements ||
		qcvm->progs->numfunctions != d->functions ||
		qcvm->progs->numglobals != d->globals)
		return NULL;
	if (!SV_VRMeleeFunctionPin(d->selector, "W_SetCurrentAmmo", d->selector_first,
		d->selector_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first,
			d->attack_parm, d->attack_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->frame, "W_WeaponFrame", d->frame_first,
			d->frame_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->super_sound, "SuperDamageSound",
			d->super_sound_first, d->super_sound_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->axe, "W_FireAxe", d->axe_first,
			d->axe_parm, 6, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->hammer, "W_FireHammer", d->hammer_first,
			d->hammer_parm, 6, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->stand, "player_stand1", d->stand_first,
			d->stand_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first,
			d->run_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->cheat, "HammerCheat", d->cheat_first,
			d->cheat_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->touch, "hammer_touch", d->touch_first,
			d->touch_parm, 5, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->item, "weapon_hammer", d->item_first,
			d->item_parm, 3, 0, NULL))
		return NULL;
	return d;
}

/* The model split is the installed selector's actual items2 branch.  A model
 * that merely resembles an axe/hammer cannot borrow this profile. */
static int SV_VRMeleeImmortalSelected(edict_t *player)
{
	eval_t *items2;
	const char *model;
	int subtype;

	if (!SV_VRMeleeImmortalDescriptor() || !player || player->free ||
		!isfinite(player->v.items) || player->v.weapon != 4096 ||
		!((int)player->v.items & 4096))
		return SV_VR_IMMORTAL_NONE;
	items2 = GetEdictFieldValueByName(player, "items2");
	if (!items2 || !isfinite(items2->_float) || items2->_float < 0 ||
		items2->_float > 16777215)
		return SV_VR_IMMORTAL_NONE;
	subtype = ((int)items2->_float & 8) ?
		SV_VR_IMMORTAL_HAMMER : SV_VR_IMMORTAL_AXE;
	model = subtype == SV_VR_IMMORTAL_HAMMER ?
		"progs/c_ham.mdl" : "progs/v_axe.mdl";
	return !strcmp(PR_GetString(player->v.weaponmodel), model) ? subtype :
		SV_VR_IMMORTAL_NONE;
}

/* Do not borrow pending player_axe* animation work.  The direct physical leaf
 * has no scheduler of its own, so only the normal stand/run loops are safe. */
static qboolean SV_VRMeleeImmortalIdle(edict_t *player)
{
	const sv_vr_melee_immortal_descriptor_t *d = SV_VRMeleeImmortalDescriptor();

	return d && SV_VRMeleeImmortalSelected(player) != SV_VR_IMMORTAL_NONE &&
		(player->v.think == d->stand || player->v.think == d->run);
}

/* Keep W_WeaponFrame's super sound followed by W_Attack's common axe/hammer
 * prelude, but not its authored player_axe animation chain. */
static qboolean SV_VRMeleeImmortalPrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_immortal_descriptor_t *d = SV_VRMeleeImmortalDescriptor();
	eval_t *hostile;

	if (!d || SV_VRMeleeImmortalSelected(player) == SV_VR_IMMORTAL_NONE ||
		!cooldown || !isfinite(cooldown->_float) || !isfinite(qcvm->time))
		return false;
	hostile = GetEdictFieldValueByName(player, "show_hostile");
	if (!hostile)
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[d->super_sound] - qcvm->functions);
	if (SV_VRMeleeImmortalSelected(player) == SV_VR_IMMORTAL_NONE)
		return false;
	hostile->_float = qcvm->time + 1;
	SV_StartSound(player, 1, "weapons/ax1.wav", 255, 1);
	cooldown->_float = qcvm->time + .5f;
	return true;
}

static dfunction_t *SV_VRMeleeImmortalLeafFunction(int subtype)
{
	const sv_vr_melee_immortal_descriptor_t *d = SV_VRMeleeImmortalDescriptor();

	if (!d || (subtype != SV_VR_IMMORTAL_AXE &&
		subtype != SV_VR_IMMORTAL_HAMMER))
		return NULL;
	return &qcvm->functions[subtype == SV_VR_IMMORTAL_HAMMER ? d->hammer : d->axe];
}

static int SV_VRMeleeImmortalTraceStatement(int subtype)
{
	const sv_vr_melee_immortal_descriptor_t *d = SV_VRMeleeImmortalDescriptor();

	if (!d)
		return -1;
	return subtype == SV_VR_IMMORTAL_HAMMER ? d->hammer_trace :
		subtype == SV_VR_IMMORTAL_AXE ? d->axe_trace : -1;
}

#endif /* QS_VR_MELEE_IMMORTAL_H */
