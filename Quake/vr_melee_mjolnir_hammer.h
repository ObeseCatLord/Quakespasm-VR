/* Mjolnir's current hammer: keep its native contact/proc/ground effects.
 * Throwing remains a trigger action, never the result of a physical whiff. */
#ifndef QS_VR_MELEE_MJOLNIR_HAMMER_H
#define QS_VR_MELEE_MJOLNIR_HAMMER_H

static qboolean SV_VRMeleeMjolnirHammerProgs(void)
{
	return SV_VRMeleeMjolnirDescriptor() &&
		SV_VRMeleeFunctionPin(2682, "W_FireMjolnir", 117758, 34073, 12, 0, NULL) &&
		SV_VRMeleeFunctionPin(2923, "player_mjolnir1original", 126163, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(2912, "player_mjolnir2", 126101, 0, 0, 0, NULL) &&
		qcvm->statements[126163].op == OP_STATE &&
		G_INT((unsigned short)qcvm->statements[126163].b) == 2912 &&
		qcvm->statements[117777].op == OP_CALL4 &&
		G_INT((unsigned short)qcvm->statements[117777].a) == 14 &&
		qcvm->statements[117783].op == OP_CALL2 &&
		G_INT((unsigned short)qcvm->statements[117783].a) == 2680;
}

static qboolean SV_VRMeleeMjolnirHammerSelected(edict_t *player)
{
	eval_t *bank, *expiry, *options;
	const char *model;
	if (!SV_VRMeleeMjolnirHammerProgs() || !player || player->free ||
		player->v.weapon != 262144)
		return false;
	bank = GetEdictFieldValueByName(player, "weaponismoditems");
	expiry = GetEdictFieldValueByName(player, "hammer_finished");
	options = GetEdictFieldValueByName(qcvm->edicts, "hipnoticoptions");
	if (!bank || bank->_float != 0 || !expiry || !isfinite(expiry->_float) ||
		expiry->_float >= (float)qcvm->time || !options ||
		!isfinite(options->_float) || options->_float < 0 ||
		options->_float > 16777215)
		return false;
	model = PR_GetString(player->v.weaponmodel);
	return !strcmp(model, "progs/violentrumble/v_hammer.mdl") ||
		!strcmp(model, "progs/violentrumble/v_hammerpw.mdl");
}

static qboolean SV_VRMeleeMjolnirHammerIdle(edict_t *player)
{
	eval_t *vehicle, *tether;
	if (!SV_VRMeleeMjolnirHammerSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(player->v.nextthink) ||
		(player->v.think != 2781 && player->v.think != 2782) ||
		!isfinite(G_FLOAT(1489)) || !isfinite(G_FLOAT(2575)) ||
		G_FLOAT(1489) > 0 || G_FLOAT(2575) > 0)
		return false;
	vehicle = GetEdictFieldValueByName(player, "in_a_vehicle");
	tether = GetEdictFieldValueByName(player, "tethered");
	return vehicle && vehicle->_float == 0 && tether && tether->_float == 0;
}

static void SV_VRMeleeMjolnirHammerReload(void)
{
	/* The installed old-option entry also enters the new attack chain (pinned
	 * above), but pays .8 instead of .4. Do not invoke the dormant old leaf,
	 * whose Tome branch reads inherited trace globals before acquiring a hit. */
	eval_t *options = GetEdictFieldValueByName(qcvm->edicts, "hipnoticoptions");
	G_FLOAT(OFS_PARM0) = ((int)options->_float & 1) ? .8f : .4f;
	qcvm->argc = 1;
	PR_ExecuteProgram(2673);
}

#endif
