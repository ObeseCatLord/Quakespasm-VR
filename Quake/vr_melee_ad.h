/* AD and Ravenkeep retain their native axe, reflection and corpse-gib leaf.
 * This is selection/entry metadata, not a replacement damage implementation. */
#ifndef QS_VR_MELEE_AD_H
#define QS_VR_MELEE_AD_H

typedef struct {
	int crc, statements, functions, globals;
	int leaf, first, parm;
	int selector, selector_first, selector_parm;
	int attack, attack_first, frame, frame_first;
	int stand, stand_first, run, run_first;
	int gib, gib_first, gib_parm;
	int intermission, cinematic, corpse_field;
	int find_first, find_next, normalize, trace, debug_trace;
	const char *base_model, *upgrade_model;
} sv_vr_melee_ad_descriptor_t;

static const sv_vr_melee_ad_descriptor_t sv_vr_melee_ad_descriptors[] = {
	{10963, 176943, 10135, 22799, 1323, 68946, 20350,
		1340, 70260, 20487, 1343, 70486, 1347, 70998,
		1369, 71215, 1370, 71256, 812, 32477, 19298,
		490, 694, 701, 68984, 69014, 68999, 69023, 68962,
		"progs/v_shadaxe0.mdl", "progs/v_shadaxe3.mdl"},
	{10710, 169020, 11142, 21750, 1071, 53779, 19224,
		1088, 55045, 19363, 1092, 55412, 1096, 55817,
		1118, 56034, 1119, 56075, 612, 21169, 18256,
		485, 660, 640, 53817, 53847, 53832, 53856, 53795,
		"progs/v_longsword.mdl", "progs/v_longswordred.mdl"}
};

static const sv_vr_melee_ad_descriptor_t *SV_VRMeleeADDescriptor(void)
{
	static const byte entity_args[] = {1, 1};
	int i;
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs)
		return NULL;
	for (i = 0; i < (int)countof(sv_vr_melee_ad_descriptors); i++) {
		const sv_vr_melee_ad_descriptor_t *d = &sv_vr_melee_ad_descriptors[i];
		if (qcvm->crc != d->crc || qcvm->progs->numstatements != d->statements ||
			qcvm->progs->numfunctions != d->functions ||
			qcvm->progs->numglobals != d->globals)
			continue;
		if (SV_VRMeleeFunctionPin(d->leaf, "W_FireAxe", d->first, d->parm,
				13, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->selector, "W_SetCurrentAmmo", d->selector_first,
				d->selector_parm, 1, 1, entity_args) &&
			SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->frame, "W_WeaponFrame", d->frame_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->stand, "player_stand1", d->stand_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first, 0, 0, 0, NULL) &&
			SV_VRMeleeFunctionPin(d->gib, "monster_flrbody_gib", d->gib_first,
				d->gib_parm, 4, 2, entity_args))
			return d;
	}
	return NULL;
}

static qboolean SV_VRMeleeADSelected(edict_t *player)
{
	const sv_vr_melee_ad_descriptor_t *d = SV_VRMeleeADDescriptor();
	eval_t *items = GetEdictFieldValueByName(player, "moditems");
	int bits;
	if (!d || !items || !isfinite(items->_float) || items->_float < 0 ||
		items->_float > 16777215 || player->v.weapon != IT_AXE)
		return false;
	bits = (int)items->_float;
	/* The grapple replaces axe input entirely. Do not suppress its trigger
	 * even if a transitional viewmodel still resembles an axe. */
	if (bits & 128)
		return false;
	return !strcmp(PR_GetString(player->v.weaponmodel),
		(bits & 4096) ? d->upgrade_model : d->base_model);
}

static qboolean SV_VRMeleeADReady(edict_t *player)
{
	const sv_vr_melee_ad_descriptor_t *d = SV_VRMeleeADDescriptor();
	return d && isfinite(G_FLOAT(d->intermission)) &&
		isfinite(G_FLOAT(d->cinematic)) && G_FLOAT(d->intermission) <= 0 &&
		G_FLOAT(d->cinematic) <= 0 &&
		(player->v.think == d->stand || player->v.think == d->run);
}

static qboolean SV_VRMeleeADPrelude(edict_t *player, eval_t *cooldown)
{
	eval_t *powerup_sound = GetEdictFieldValueByName(player, "powerup_sound");
	eval_t *super_sound = GetEdictFieldValueByName(player, "super_sound");
	eval_t *quad = GetEdictFieldValueByName(player, "super_damage_finished");
	eval_t *hostile = GetEdictFieldValueByName(player, "show_hostile");
	if (!powerup_sound || !super_sound || !quad || !hostile ||
		!isfinite(powerup_sound->_float) || !isfinite(super_sound->_float) ||
		!isfinite(quad->_float))
		return false;
	/* W_WeaponFrame's axe-applicable inline powerup prelude. Other powerup
	 * sounds belong to ranged selections and must not be played here. */
	if (quad->_float > 0 && powerup_sound->_float < qcvm->time &&
		super_sound->_float < qcvm->time) {
		super_sound->_float = powerup_sound->_float = qcvm->time + 1;
		SV_StartSound(player, 4, "items/damage3.wav", 255, 1);
	}
	hostile->_float = qcvm->time + 1;
	SV_StartSound(player, 1, (rand() & 0x7fff) < 0x4000 ?
		"weapons/axe_swoosh1.wav" : "weapons/axe_swoosh2.wav", 255, 1);
	cooldown->_float = qcvm->time + .5;
	return true;
}

static qboolean SV_VRMeleeADScoped(void)
{
	const sv_vr_melee_ad_descriptor_t *d;
	if (!sv_vr_contact_call.active || !sv_vr_contact_call.player)
		return false;
	d = SV_VRMeleeADDescriptor();
	return d && sv_vr_contact_call.function == &qcvm->functions[d->leaf] &&
		qcvm->xfunction == sv_vr_contact_call.function &&
		pr_global_struct->self == EDICT_TO_PROG(sv_vr_contact_call.player);
}

#endif
