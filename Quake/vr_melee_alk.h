/* Alkaline/LimJam axe only. Chainsaws intentionally retain native trigger
 * operation, timing and release behavior; weapon collision is independent. */
#ifndef QS_VR_MELEE_ALK_H
#define QS_VR_MELEE_ALK_H

typedef struct {
	int crc, statements, functions, globals;
	int leaf, first, parm, trace;
	int selector, selector_first, selector_parm, selector_args;
	int attack, attack_first, quad, quad_first, frame, frame_first;
	int stand, stand_first, run, run_first, run_parm;
} sv_vr_melee_alk_descriptor_t;

static const sv_vr_melee_alk_descriptor_t sv_vr_melee_alk_descriptors[] = {
	{30793, 78671, 5767, 12608, 330, 12154, 9431, 12167,
		371, 13985, 0, 0, 376, 14321, 377, 14446, 408, 16031,
		469, 19607, 470, 19687, 9771},
	{32416, 85097, 6131, 13769, 354, 13004, 10242, 13017,
		399, 15159, 10506, 1, 404, 15475, 405, 15600, 438, 17324,
		499, 21050, 500, 21130, 10627}
};

static const sv_vr_melee_alk_descriptor_t *SV_VRMeleeALKDescriptor(void)
{
	static const byte scalar[] = {1};
	int i;
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs)
		return NULL;
	for (i = 0; i < (int)countof(sv_vr_melee_alk_descriptors); ++i) {
		const sv_vr_melee_alk_descriptor_t *d = &sv_vr_melee_alk_descriptors[i];
		if (qcvm->crc != d->crc || qcvm->progs->numstatements != d->statements ||
			qcvm->progs->numfunctions != d->functions || qcvm->progs->numglobals != d->globals)
			continue;
		if (SV_VRMeleeFunctionPin(d->leaf, "W_FireAxe", d->first, d->parm, 6, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->selector, "W_SetCurrentAmmo", d->selector_first,
				d->selector_parm, d->selector_args, d->selector_args, scalar) &&
			SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->quad, "SuperDamageSound", d->quad_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->frame, "W_WeaponFrame", d->frame_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->stand, "player_stand1", d->stand_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first, d->run_parm, 1, 0, NULL))
			return d;
	}
	return NULL;
}

static qboolean SV_VRMeleeALKSelected(edict_t *player)
{
	return SV_VRMeleeALKDescriptor() && player->v.weapon == IT_AXE &&
		!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_alkaxe20fps.mdl");
}

static qboolean SV_VRMeleeALKIdle(edict_t *player)
{
	const sv_vr_melee_alk_descriptor_t *d = SV_VRMeleeALKDescriptor();
	return d && (player->v.think == d->stand || player->v.think == d->run);
}

static qboolean SV_VRMeleeALKPrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_alk_descriptor_t *d = SV_VRMeleeALKDescriptor();
	eval_t *hostile = GetEdictFieldValueByName(player, "show_hostile");
	if (!d || !hostile)
		return false;
	/* Unlike the base game, the axe explicitly clears show_hostile before
	 * its swing. Preserve that quiet-weapon behavior, and the original order
	 * of swing sound, recovery assignment and final quad-sound helper. */
	hostile->_float = 0;
	SV_StartSound(player, 1, "weapons/ax1.wav", 255, 1);
	cooldown->_float = qcvm->time + .5;
	qcvm->argc = 0;
	PR_ExecuteProgram(d->quad);
	return true;
}

#endif
