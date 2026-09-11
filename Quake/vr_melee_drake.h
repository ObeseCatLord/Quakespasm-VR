/* Shared Tershibboleth/Nyarlathotep/Something Wicked axe bridge.  The single
 * pinned VM selects melee through .war==2, not stock weapon bits.  A map's
 * weapon_sword constructor aliases weapon_axe, so this deliberately owns no
 * second sword subtype.  Form/shadow selection remains outside this header. */
#ifndef QS_VR_MELEE_DRAKE_H
#define QS_VR_MELEE_DRAKE_H

typedef struct {
	int crc, statements, functions, globals;
	int set_ammo, set_ammo_first, set_ammo_parm;
	int attack, attack_first, attack_parm;
	int hostile, hostile_first, hostile_parm;
	int animation, animation_first, animation_parm, animation_locals;
	int leaf, leaf_first, leaf_parm;
	int nested, nested_first, nested_parm, nested_locals;
	int frame, frame_first, frame_parm;
	int run, run_first, run_parm;
	int trace, root_call;
} sv_vr_melee_drake_descriptor_t;

static const sv_vr_melee_drake_descriptor_t sv_vr_melee_drake_descriptor = {
	/* effective VM e6d0e49b…c62f09f0d */
	59905, 137577, 9725, 23562,
	841, 26541, 5213,
	1743, 55058, 9402,
	1740, 55021, 9396,
	1504, 52579, 9063, 4,
	920, 30514, 5846,
	919, 30367, 5832, 13,
	1771, 57098, 9539,
	1431, 51912, 8969,
	30381, 30521
};

static const sv_vr_melee_drake_descriptor_t *SV_VRMeleeDrakeDescriptor(void)
{
	static const byte nested_args[] = {1, 3};
	const sv_vr_melee_drake_descriptor_t *d = &sv_vr_melee_drake_descriptor;

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || qcvm->crc != d->crc ||
		qcvm->progs->numstatements != d->statements ||
		qcvm->progs->numfunctions != d->functions ||
		qcvm->progs->numglobals != d->globals)
		return NULL;
	/* Full VM CRC/counts plus every directly used function/ABI pin are the
	 * gate; names or the visible v_axe asset alone never enable this bridge. */
	if (!SV_VRMeleeFunctionPin(d->set_ammo, "War_SetCurrentAmmo",
		d->set_ammo_first, d->set_ammo_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->attack, "W_Attack", d->attack_first,
		d->attack_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->hostile, "W_ShowHostile", d->hostile_first,
		d->hostile_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->animation, "player_axe", d->animation_first,
		d->animation_parm, d->animation_locals, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->leaf, "W_FireAxe", d->leaf_first,
		d->leaf_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->nested, "Attack_Axe", d->nested_first,
		d->nested_parm, d->nested_locals, 2, nested_args) ||
		!SV_VRMeleeFunctionPin(d->frame, "W_WeaponFrame", d->frame_first,
		d->frame_parm, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(d->run, "player_run", d->run_first,
		d->run_parm, 0, 0, NULL))
		return NULL;
	return d;
}

/* weapon_sword calls weapon_axe, which sets .war=2.  Require the selected
 * model as well: installed v_axe2 is not a proven runnable player mode. */
static qboolean SV_VRMeleeDrakeSelected(edict_t *player)
{
	eval_t *war;

	if (!SV_VRMeleeDrakeDescriptor() || !player || player->free)
		return false;
	war = GetEdictFieldValueByName(player, "war");
	return war && isfinite(war->_float) && war->_float == 2 &&
		!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_axe.mdl");
}

/* The VM has no player_stand1.  player_run is the only pinned harmless
 * player locomotion entry; every animation/attack state is rejected. */
static qboolean SV_VRMeleeDrakeReady(edict_t *player)
{
	const sv_vr_melee_drake_descriptor_t *d = SV_VRMeleeDrakeDescriptor();
	eval_t *cooldown;
	float qctime;

	if (!d || !SV_VRMeleeDrakeSelected(player) || !isfinite(qcvm->time) ||
		player->v.health <= 0 || player->v.deadflag || player->v.think != d->run)
		return false;
	/* W_FireAxe itself exits before makevectors/Attack_Axe in either authored
	 * presentation gate. Check them before the physical prelude can spend a
	 * recovery or play the native swing sound. */
	if (!isfinite(G_FLOAT(516)) || !isfinite(G_FLOAT(503)) ||
		G_FLOAT(516) != 0 || G_FLOAT(503) != 0)
		return false;
	cooldown = GetEdictFieldValueByName(player, "attack_finished");
	qctime = (float)qcvm->time;
	return cooldown && isfinite(cooldown->_float) && cooldown->_float <= qctime;
}

/* W_Attack orders W_ShowHostile, player_axe, attack_finished=time+.55, then
 * this sound.  Physical contact replaces only player_axe's deferred pose;
 * it must not synthesize button0. Attack_Axe observes the actual field:
 * held/encore is 24 damage, while the unheld branch is 30 and adds .2 to the
 * prelude recovery. */
static qboolean SV_VRMeleeDrakePrelude(edict_t *player, eval_t *cooldown)
{
	const sv_vr_melee_drake_descriptor_t *d = SV_VRMeleeDrakeDescriptor();

	if (!d || !SV_VRMeleeDrakeSelected(player) || !cooldown ||
		!isfinite(cooldown->_float) || !isfinite(qcvm->time))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[d->hostile] - qcvm->functions);
	if (!SV_VRMeleeDrakeSelected(player))
		return false;
	cooldown->_float = qcvm->time + .55f;
	SV_StartSound(player, 1, "weapons/sawatck.wav", 255, 1);
	return true;
}

static dfunction_t *SV_VRMeleeDrakeLeafFunction(void)
{
	const sv_vr_melee_drake_descriptor_t *d = SV_VRMeleeDrakeDescriptor();
	return d ? &qcvm->functions[d->leaf] : NULL;
}

/* Called from SV_VRContactTrace before its root-function-only fallback.  The
 * accepted contact replaces exactly W_FireAxe -> Attack_Axe's one initial
 * traceline.  The latter's other native effects and all later nested traces
 * remain QC-owned. */
static qboolean SV_VRMeleeDrakeTrace(edict_t *ent, const vec3_t start,
	const vec3_t end, int nomonsters, trace_t *trace)
{
	const sv_vr_melee_drake_descriptor_t *d = SV_VRMeleeDrakeDescriptor();
	prstack_t *caller;

	if (!d || !trace || !sv_vr_contact_call.active ||
		!sv_vr_contact_call.player || !SV_VRMeleeFiniteVector(start) ||
		!SV_VRMeleeFiniteVector(end) || nomonsters ||
		sv_vr_contact_call.function != &qcvm->functions[d->leaf] ||
		qcvm->xfunction != &qcvm->functions[d->nested] ||
		qcvm->xstatement != d->trace || qcvm->depth <= 0 ||
		ent != sv_vr_contact_call.player ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
		return false;
	caller = &qcvm->stack[qcvm->depth - 1];
	if (caller->f != sv_vr_contact_call.function || caller->s != d->root_call)
		return false;
	*trace = sv_vr_contact_call.trace;
	sv_vr_contact_call.active = false;
	return true;
}

#endif /* QS_VR_MELEE_DRAKE_H */
