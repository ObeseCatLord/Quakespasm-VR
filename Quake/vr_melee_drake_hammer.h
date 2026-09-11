/* Shared Tershibboleth/Nyarlathotep/Something Wicked Mjolnir boundary.
 *
 * Direct physical contact must retain HIP_MjolnirDamage's native target
 * acquisition: that helper reads QC trace_ent and accepts only
 * (hit_origin, direction, damage_type). A contact bridge may substitute an
 * accepted trace at the exact native acquisition site, then call the helper.
 * It must not repurpose a temporal sweep fraction as HIP_FireMjolnir's second
 * down-trace fraction, which selects slam versus ordinary impact. */
#ifndef QS_VR_MELEE_DRAKE_HAMMER_H
#define QS_VR_MELEE_DRAKE_HAMMER_H

typedef struct {
	int crc, statements, functions, globals;
	int selector, selector_first, selector_parm;
	int weapon, weapon_first, weapon_parm;
	int attack, attack_first, attack_parm;
	int hostile, hostile_first, hostile_parm;
	int reload, reload_first, reload_parm, reload_locals;
	int frame1, frame1_first, frame4, frame4_first, frame4_fire_call;
	int root, root_first, root_parm, root_locals, root_slam_call, root_damage_call;
	int slam, slam_first, slam_parm, slam_locals;
	int damage, damage_first, damage_parm, damage_locals;
	int damage_hammer, damage_decap;
	int throw_fn, throw_first, throw_parm, throw_locals;
	int throw_damage_call, throw_tent_call;
	int tent, tent_first, tent_parm, tent_locals;
	int run, run_first, run_parm;
	const char *model;
} sv_vr_melee_drake_hammer_descriptor_t;

static const sv_vr_melee_drake_hammer_descriptor_t sv_vr_melee_drake_hammer_descriptor = {
	59905, 137577, 9725, 23562,
	841, 26541, 5213,
	1366, 48341, 8724,
	1743, 55058, 9402,
	1740, 55021, 9396,
	1742, 55054, 9398, 1,
	1514, 52706, 1517, 52725, 52729,
	1131, 38872, 7291, 6, 38902, 38910,
	1128, 38765, 7277, 1,
	1130, 38811, 7280, 10,
	3173, 3065,
	1132, 38932, 7298, 9, 38967, 38987,
	306, 10221, 2582, 4,
	1431, 51912, 8969,
	"progs/v_hammer.mdl"
};

static const sv_vr_melee_drake_hammer_descriptor_t *SV_VRMeleeDrakeHammerDescriptor(void)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d =
		&sv_vr_melee_drake_hammer_descriptor;
	dstatement_t *frame_call, *slam_call, *damage_call, *throw_damage_call,
		*throw_tent_call;
	static const byte scalar[] = {1};
	static const byte vector_vector_string[] = {3, 3, 1};
	static const byte scalar_vector[] = {1, 3};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || qcvm->crc != d->crc ||
		qcvm->progs->numstatements != d->statements ||
		qcvm->progs->numfunctions != d->functions ||
		qcvm->progs->numglobals != d->globals ||
		!SV_VRMeleeFunctionPin(d->selector, "War_SetCurrentAmmo",
			d->selector_first, d->selector_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->weapon, "weapon_mjolnir",
			d->weapon_first, d->weapon_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first,
			d->attack_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->hostile, "W_ShowHostile", d->hostile_first,
			d->hostile_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->reload, "W_Reload", d->reload_first,
			d->reload_parm, d->reload_locals, 1, scalar) ||
		!SV_VRMeleeFunctionPin(d->frame1, "player_mjolnir1", d->frame1_first,
			9082, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->frame4, "player_mjolnir4", d->frame4_first,
			9084, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->root, "HIP_FireMjolnir", d->root_first,
			d->root_parm, d->root_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->slam, "HIP_SlamMjolnir", d->slam_first,
			d->slam_parm, d->slam_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->damage, "HIP_MjolnirDamage", d->damage_first,
			d->damage_parm, d->damage_locals, 3, vector_vector_string) ||
		!SV_VRMeleeFunctionPin(d->throw_fn, "HIP_ThrowMjolnir", d->throw_first,
			d->throw_parm, d->throw_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->tent, "Tent_Point", d->tent_first,
			d->tent_parm, d->tent_locals, 2, scalar_vector) ||
		!SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first,
			d->run_parm, 0, 0, NULL))
		return NULL;
	frame_call = &qcvm->statements[d->frame4_fire_call];
	slam_call = &qcvm->statements[d->root_slam_call];
	damage_call = &qcvm->statements[d->root_damage_call];
	throw_damage_call = &qcvm->statements[d->throw_damage_call];
	throw_tent_call = &qcvm->statements[d->throw_tent_call];
	if (frame_call->op != OP_CALL0 ||
		G_INT((unsigned short)frame_call->a) != d->root ||
		slam_call->op != OP_CALL0 ||
		G_INT((unsigned short)slam_call->a) != d->slam ||
		damage_call->op != OP_CALL3 ||
		G_INT((unsigned short)damage_call->a) != d->damage ||
		throw_damage_call->op != OP_CALL3 ||
		G_INT((unsigned short)throw_damage_call->a) != d->damage ||
		throw_tent_call->op != OP_CALL2 ||
		G_INT((unsigned short)throw_tent_call->a) != d->tent ||
		strcmp(PR_GetString(G_INT(d->damage_hammer)), "hammer") ||
		strcmp(PR_GetString(G_INT(d->damage_decap)), "hammer_decap"))
		return NULL;
	return d;
}

/* weapon_mjolnir writes ammo_cells=60 and war=131072. W_Attack uses Mjolnir
 * frames at 25+ cells and ordinary hammer frames below that threshold; both
 * converge on the audited native direct helper. Require its selected model,
 * but do not strand the player when cells fall below 25. */
static qboolean SV_VRMeleeDrakeHammerSelected(edict_t *player)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();
	eval_t *war;

	if (!d || !player || player->free || !isfinite(player->v.ammo_cells) ||
		player->v.ammo_cells < 0 ||
		strcmp(PR_GetString(player->v.weaponmodel), d->model))
		return false;
	war = GetEdictFieldValueByName(player, "war");
	return war && isfinite(war->_float) && war->_float == 131072;
}

/* The direct physical path replaces player_mjolnir1..4, whose first action
 * after W_ShowHostile is W_Reload(.6). Retain those non-STATE effects; never
 * schedule/cancel an authored hammer frame. HIP_FireMjolnir's own two
 * presentation gates are checked before the sound/reload can spend state. */
static qboolean SV_VRMeleeDrakeHammerReady(edict_t *player)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();
	eval_t *cooldown;

	if (!d || !SV_VRMeleeDrakeHammerSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(qcvm->time) || player->v.think != d->run ||
		!isfinite(G_FLOAT(516)) || !isfinite(G_FLOAT(503)) ||
		G_FLOAT(516) != 0 || G_FLOAT(503) != 0)
		return false;
	cooldown = GetEdictFieldValueByName(player, "attack_finished");
	return cooldown && isfinite(cooldown->_float) &&
		cooldown->_float <= (float)qcvm->time;
}

static qboolean SV_VRMeleeDrakeHammerPrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();

	if (!d || !SV_VRMeleeDrakeHammerSelected(player) || !cooldown ||
		!isfinite(cooldown->_float) || !isfinite(qcvm->time))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[d->hostile] - qcvm->functions);
	if (!SV_VRMeleeDrakeHammerSelected(player))
		return false;
	G_FLOAT(OFS_PARM0) = .6f;
	qcvm->argc = 1;
	PR_ExecuteProgram(&qcvm->functions[d->reload] - qcvm->functions);
	return SV_VRMeleeDrakeHammerSelected(player) && isfinite(cooldown->_float);
}

/* HIP_MjolnirDamage ABI is (vector hit_origin, vector direction,
 * string damage_type); its victim and attacker are native trace_ent and self.
 * HIP_ThrowMjolnir supplies either the pinned "hammer" or "hammer_decap"
 * string according to its own trace-fraction branch. A physical bridge must
 * not put EDICT_TO_PROG(player) in parm2. */
static dfunction_t *SV_VRMeleeDrakeHammerDamageFunction(void)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();
	return d ? &qcvm->functions[d->damage] : NULL;
}

static int SV_VRMeleeDrakeHammerDamageTypeHammer(void)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();
	return d ? G_INT(d->damage_hammer) : 0;
}

static int SV_VRMeleeDrakeHammerDamageTypeDecap(void)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();
	return d ? G_INT(d->damage_decap) : 0;
}

/* HIP_ThrowMjolnir's non-damageable-impact branch calculates
 * org=trace_endpos-(v_forward*4), then sound(self,CHAN_WEAPON,
 * "hipweap/mjoltink.wav",1,ATTN_NORM) and Tent_Point(TE_GUNSHOT,org).
 * The contact caller uses this instead of Damage for every non-damageable
 * contact; an empty physical whiff deliberately does not enter the authored
 * thrown-hammer branch. The captured contact basis avoids mutable QC globals. */
static qboolean SV_VRMeleeDrakeHammerWall(edict_t *player, const vec3_t endpos,
	const vec3_t forward)
{
	const sv_vr_melee_drake_hammer_descriptor_t *d = SV_VRMeleeDrakeHammerDescriptor();
	vec3_t org;

	if (!d || !SV_VRMeleeDrakeHammerSelected(player) ||
		!SV_VRMeleeFiniteVector(endpos) ||
		!SV_VRMeleeFiniteVector(forward))
		return false;
	org[0] = endpos[0] - 4 * forward[0];
	org[1] = endpos[1] - 4 * forward[1];
	org[2] = endpos[2] - 4 * forward[2];
	SV_StartSound(player, 1, "hipweap/mjoltink.wav", 255, 1);
	G_FLOAT(OFS_PARM0) = TE_GUNSHOT;
	VectorCopy(org, G_VECTOR(OFS_PARM1));
	qcvm->argc = 2;
	PR_ExecuteProgram(&qcvm->functions[d->tent] - qcvm->functions);
	return SV_VRMeleeDrakeHammerSelected(player);
}

#endif /* QS_VR_MELEE_DRAKE_HAMMER_H */
