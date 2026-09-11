/* Mjolnir's Gungnir is the explicit projectile-trigger / physical-stab
 * exception. Retain native reload, projectile, damage and water movement. */
#ifndef QS_VR_MELEE_GUNGNIR_H
#define QS_VR_MELEE_GUNGNIR_H

static qboolean SV_VRMeleeGungnirProgs(void)
{
	dstatement_t *branch;
	if (!SV_VRMeleeMjolnirDescriptor() ||
		!SV_VRMeleeFunctionPin(2652, "W_FireGungnir", 116178, 0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(2621, "GungnirAttack1", 115579, 0, 0, 0, NULL) ||
		!SV_VRMeleeFunctionPin(2654, "GungnirPrimary", 116233, 34033, 11, 0, NULL))
		return false;
	branch = &qcvm->statements[116237];
	return branch->op == OP_IFNOT && (unsigned short)branch->a == 34547 &&
		branch->b == 8 && qcvm->statements[116184].op == OP_CALL0 &&
		G_INT((unsigned short)qcvm->statements[116184].a) == 2621 &&
		qcvm->statements[121357].op == OP_CALL0 &&
		G_INT((unsigned short)qcvm->statements[121357].a) == 2652 &&
		qcvm->statements[116259].op == OP_CALL4 &&
		G_INT((unsigned short)qcvm->statements[116259].a) == 14;
}

static qboolean SV_VRMeleeGungnirSelected(edict_t *player)
{
	eval_t *bank, *items;
	if (!SV_VRMeleeGungnirProgs() || !player || player->free ||
		player->v.weapon != 128 ||
		strcmp(PR_GetString(player->v.weaponmodel), "progs/aoa/v_gungnir.mdl"))
		return false;
	bank = GetEdictFieldValueByName(player, "weaponismoditems");
	items = GetEdictFieldValueByName(player, "moditems");
	return bank && bank->_float == 1 && items && isfinite(items->_float) &&
		items->_float >= 0 && items->_float <= 16777215 &&
		((int)items->_float & 128);
}

/* Cooldown is checked separately: at the trigger seam native W_FireGungnir
 * has already written its preliminary .8 reload. Never accept a pending
 * animation as idle just because its nextthink is overdue or unscheduled. */
static qboolean SV_VRMeleeGungnirIdle(edict_t *player)
{
	eval_t *vehicle, *tether;
	if (!SV_VRMeleeGungnirSelected(player) || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(player->v.nextthink) ||
		(player->v.think != 2781 && player->v.think != 2782) ||
		!isfinite(G_FLOAT(1489)) || !isfinite(G_FLOAT(2575)) ||
		G_FLOAT(1489) > 0 || G_FLOAT(2575) > 0)
		return false;
	vehicle = GetEdictFieldValueByName(player, "in_a_vehicle");
	tether = GetEdictFieldValueByName(player, "tethered");
	return vehicle && vehicle->_float == 0 && tether && tether->_float == 0;
}

static void SV_VRMeleeGungnirReload(edict_t *player)
{
	SV_StartSound(player, 0, "aoa/swing2.wav", 255, 1);
	G_FLOAT(OFS_PARM0) = .9f;
	qcvm->argc = 1;
	PR_ExecuteProgram(2673); /* Original Haste, minimum, maximum and slow policy. */
}

#endif
