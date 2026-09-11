/* Mjolnir AoA Blood Mace: narrowly pin the root prelude and the one-target
 * hit leaf. The authored radius/wall chain stays out of physical contact. */
#ifndef QS_VR_MELEE_MACE_H
#define QS_VR_MELEE_MACE_H

typedef struct {
	int root, root_first, root_parm, root_locals, root_swing_call;
	int swing1, swing1_first, swing1_reload_call;
	int hit, hit_first, hit_parm, hit_locals;
	int swing, swing_first, swing_wall_call;
	int tent, tent_first, tent_parm, tent_locals;
	int stand, stand_first, run, run_first;
	const char *model;
} sv_vr_melee_mace_descriptor_t;

static const sv_vr_melee_mace_descriptor_t sv_vr_melee_mace_descriptor = {
	2618, 115439, 34390, 6, 115439,
	2597, 115166, 115176,
	2619, 115474, 34390, 5,
	2620, 115524, 115571,
	1241, 43346, 34390, 4,
	2781, 124783, 2782, 124840,
	"progs/aoa/v_mace.mdl"
};

/* The whole-VM identity, W_SetCurrentAmmo, W_Attack, W_Reload and shared
 * locomotion pins come from the existing Mjolnir descriptor.  These pins add
 * only Mace's exact root/prelude/hit ABI and direct CALL operands. */
static const sv_vr_melee_mace_descriptor_t *SV_VRMeleeMaceDescriptor(void)
{
	const sv_vr_melee_mace_descriptor_t *d = &sv_vr_melee_mace_descriptor;
	dstatement_t *root_call, *reload_call, *wall_call;
	static const byte vector_entity[] = {3, 1};
	static const byte scalar_vector[] = {1, 3};

	if (!SV_VRMeleeMjolnirDescriptor() ||
		!SV_VRMeleeFunctionPin(d->root, "W_FireMace", d->root_first,
			d->root_parm, d->root_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->swing1, "MaceSwing1", d->swing1_first,
			0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->hit, "MaceSwingHits", d->hit_first,
			d->hit_parm, d->hit_locals, 2, vector_entity) ||
		!SV_VRMeleeFunctionPin(d->swing, "MaceSwing", d->swing_first,
			34390, 5, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->tent, "Tent_Point", d->tent_first,
			d->tent_parm, d->tent_locals, 2, scalar_vector))
		return NULL;
	root_call = &qcvm->statements[d->root_swing_call];
	reload_call = &qcvm->statements[d->swing1_reload_call];
	wall_call = &qcvm->statements[d->swing_wall_call];
	if (root_call->op != OP_CALL0 ||
		G_INT((unsigned short)root_call->a) != d->swing1 ||
		reload_call->op != OP_CALL1 ||
		G_INT((unsigned short)reload_call->a) !=
			SV_VRMeleeMjolnirDescriptor()->reload ||
		wall_call->op != OP_CALL2 ||
		G_INT((unsigned short)wall_call->a) != d->tent)
		return NULL;
	return d;
}

/* IT_MACE resides in Mjolnir's mod-item (hack) bank, unlike the conventional
 * axe. Requiring both the selected bank and ownership bit fences collision
 * from a coincidental weapon number/model pairing. */
static qboolean SV_VRMeleeMaceSelected(edict_t *player)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();
	eval_t *bank, *moditems;

	if (!d || !player || player->free || player->v.weapon != 8 ||
		strcmp(PR_GetString(player->v.weaponmodel), d->model))
		return false;
	bank = GetEdictFieldValueByName(player, "weaponismoditems");
	moditems = GetEdictFieldValueByName(player, "moditems");
	return bank && moditems && isfinite(bank->_float) &&
		isfinite(moditems->_float) && bank->_float == 1 &&
		moditems->_float >= 0 && moditems->_float <= 16777215 &&
		((int)moditems->_float & 8);
}

/* Match Gungnir's harmless locomotion/context gate. Cooldown is deliberately
 * checked at the admitted W_WeaponFrame/W_Attack seam: W_FireMace itself has
 * no cooldown guard and MaceSwing1 is what owns W_Reload(.6). */
static qboolean SV_VRMeleeMaceIdle(edict_t *player)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();
	eval_t *vehicle, *tether;

	if (!d || !SV_VRMeleeMaceSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(player->v.nextthink) ||
		(player->v.think != d->stand && player->v.think != d->run) ||
		!isfinite(G_FLOAT(SV_VRMeleeMjolnirDescriptor()->cinematic)) ||
		!isfinite(G_FLOAT(SV_VRMeleeMjolnirDescriptor()->cutscene)) ||
		G_FLOAT(SV_VRMeleeMjolnirDescriptor()->cinematic) > 0 ||
		G_FLOAT(SV_VRMeleeMjolnirDescriptor()->cutscene) > 0)
		return false;
	vehicle = GetEdictFieldValueByName(player, "in_a_vehicle");
	tether = GetEdictFieldValueByName(player, "tethered");
	return vehicle && tether && isfinite(vehicle->_float) &&
		isfinite(tether->_float) && vehicle->_float == 0 && tether->_float == 0;
}

/* This is exactly the non-STATE portion of MaceSwing1: no frame write and no
 * authored callback is scheduled. The caller has already admitted the native
 * W_FireMace root, which continues into its original ground/air/macejump
 * aftermath. W_Reload keeps Haste, slow, and clamp policy QC-owned. */
static qboolean SV_VRMeleeMaceReload(edict_t *player)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();
	eval_t *cooldown;

	if (!d || !SV_VRMeleeMaceSelected(player) || !isfinite(qcvm->time))
		return false;
	cooldown = GetEdictFieldValueByName(player, "attack_finished");
	if (!cooldown || !isfinite(cooldown->_float))
		return false;
	SV_StartSound(player, 1, "kurok/weapons/ax1.wav", 255, 1);
	G_FLOAT(OFS_PARM0) = .6f;
	qcvm->argc = 1;
	PR_ExecuteProgram(&qcvm->functions[SV_VRMeleeMjolnirDescriptor()->reload] -
		qcvm->functions);
	return SV_VRMeleeMaceSelected(player) && isfinite(cooldown->_float);
}

static dfunction_t *SV_VRMeleeMaceRootFunction(void)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();
	return d ? &qcvm->functions[d->root] : NULL;
}

static dfunction_t *SV_VRMeleeMaceHitFunction(void)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();
	return d ? &qcvm->functions[d->hit] : NULL;
}

/* MaceSwing's no-victim wall branch is exactly Tent_Point(TE_GUNSHOT,
 * trace_endpos) followed by player/axhit2.wav. Reuse Tent_Point rather than
 * hand-encoding a temp-entity packet: its QC writes the installed VM's
 * MSG_BROADCAST/SVC_TEMPENTITY coordinates and preserves protocol behavior. */
static qboolean SV_VRMeleeMaceWall(edict_t *player, const vec3_t point)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();

	if (!d || !SV_VRMeleeMaceSelected(player) || !SV_VRMeleeFiniteVector(point))
		return false;
	SV_StartSound(player, 1, "player/axhit2.wav", 255, 1);
	G_FLOAT(OFS_PARM0) = TE_GUNSHOT;
	VectorCopy(point, G_VECTOR(OFS_PARM1));
	qcvm->argc = 2;
	PR_ExecuteProgram(&qcvm->functions[d->tent] - qcvm->functions);
	return SV_VRMeleeMaceSelected(player);
}

/* Main's QC-call hook may consume only the native root's exact first call.
 * It must leave W_FireMace executing so its lunge/touch/macejump branch stays
 * authored. The caller owns the separate active physical-scope, self, and
 * depth proof because physical contact deliberately calls W_FireMace directly
 * rather than through the desktop W_Attack root. */
static qboolean SV_VRMeleeMaceSwing1Call(int function)
{
	const sv_vr_melee_mace_descriptor_t *d = SV_VRMeleeMaceDescriptor();

	return d && function == d->swing1 && qcvm->xstatement == d->root_swing_call &&
		qcvm->xfunction == &qcvm->functions[d->root];
}

#endif /* QS_VR_MELEE_MACE_H */
