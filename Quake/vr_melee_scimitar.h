/* The Fire Scimitar retains its authored trigger combo. Physical strokes
 * borrow only the native first-strike prelude and contact effects. */
#ifndef QS_VR_MELEE_SCIMITAR_H
#define QS_VR_MELEE_SCIMITAR_H

static qboolean SV_VRMeleeScimitarProgs(void)
{

	static const byte scalar[] = {1};
	dstatement_t *cursor;
	if (!SV_VRMeleeMjolnirDescriptor() ||
		!SV_VRMeleeFunctionPin(2461, "W_FireScimitar", 112921, 34390, 3, 0, NULL) ||
		!SV_VRMeleeFunctionPin(2443, "sword1Attack1", 112576, 0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(2462, "SwordSwing", 112964, 33999, 11, 1, scalar) ||
		!SV_VRMeleeFunctionPin(2464, "SwordMagic", 113099, 34390, 5, 1, scalar))
		return false;
	cursor = &qcvm->statements[113032];
	return qcvm->statements[112962].op == OP_CALL0 &&
		G_INT((unsigned short)qcvm->statements[112962].a) == 2443 &&
		qcvm->statements[112997].op == OP_CALL2 &&
		G_INT((unsigned short)qcvm->statements[112997].a) == 21 &&
		qcvm->functions[21].first_statement == -22 &&
		cursor->op == OP_LOAD_ENT && (unsigned short)cursor->a == 34008 &&
		(unsigned short)cursor->b == 153 && (unsigned short)cursor->c == 34008 &&
		G_INT(153) == 60 && qcvm->statements[113038].op == OP_CALL4 &&
		G_INT((unsigned short)qcvm->statements[113038].a) == 14;
}

static qboolean SV_VRMeleeScimitarSelected(edict_t *player)
{
	eval_t *bank;
	if (!SV_VRMeleeScimitarProgs() || !player || player->free ||
		player->v.weapon != 512 ||
		strcmp(PR_GetString(player->v.weaponmodel), "progs/aoa/v_scimitar.mdl"))
		return false;
	bank = GetEdictFieldValueByName(player, "weaponismoditems");
	return bank && bank->_float == 0;
}

static qboolean SV_VRMeleeScimitarIdle(edict_t *player)
{
	eval_t *vehicle, *tether, *tome;
	if (!SV_VRMeleeScimitarSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(player->v.nextthink) ||
		(player->v.think != 2781 && player->v.think != 2782) ||
		!isfinite(G_FLOAT(1489)) || !isfinite(G_FLOAT(2575)) ||
		G_FLOAT(1489) > 0 || G_FLOAT(2575) > 0)
		return false;
	vehicle = GetEdictFieldValueByName(player, "in_a_vehicle");
	tether = GetEdictFieldValueByName(player, "tethered");
	tome = GetEdictFieldValueByName(player, "tome_finished");
	return vehicle && vehicle->_float == 0 && tether && tether->_float == 0 &&
		tome && isfinite(tome->_float);
}

/* Once admitted, ownership does not depend on mutable QC self/target state.
 * A death callback can remove the target, mutate .chain, or leave self changed. */
static qboolean SV_VRMeleeScimitarSwingScope(void)
{
	return sv_vr_contact_call.active &&
		sv_vr_contact_call.function == &qcvm->functions[2462] &&
		qcvm->xfunction == sv_vr_contact_call.function &&
		qcvm->depth == sv_vr_contact_call.depth;
}

#endif
