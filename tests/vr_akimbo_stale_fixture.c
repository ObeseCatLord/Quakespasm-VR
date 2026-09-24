/* Regression fixture for rejecting an akimbo pair whose producer identity is stale. */
#include "../Quake/vr_input.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

keydest_t key_dest = key_game;
enum m_state_e m_state = m_none;
client_static_t cls;
client_state_t cl;
cvar_t vr_aimmode = {"vr_aimmode", "7", CVAR_ARCHIVE};
vec3_t vec3_origin = {0.0f, 0.0f, 0.0f};

static vrxr_frame_t fixture_frame;
static int unreachable_admission_calls;

const vrxr_frame_t *GL_OpenXRFrame (void)
{
	return &fixture_frame;
}

qboolean CL_AngleLocked (void)
{
	return false;
}

qboolean VR_WeaponCalibrationAdjustActive (void)
{
	return false;
}

qboolean V_TrackedSessionActive (void)
{
	return true;
}

qboolean M_WaitingForKeyBinding (void)
{
	return false;
}

qboolean Key_InputGrabActive (void)
{
	return false;
}

qboolean V_AkimboPairReady (void)
{
	++unreachable_admission_calls;
	return false;
}

void *Mod_Extradata_CheckSkin (qmodel_t *model, int skinnum)
{
	(void)model;
	(void)skinnum;
	++unreachable_admission_calls;
	return NULL;
}

qboolean Mod_GetStockAxeEdge (qmodel_t *model, int skinnum,
	stockaxe_edge_t *out)
{
	(void)model;
	(void)skinnum;
	(void)out;
	++unreachable_admission_calls;
	return false;
}

qboolean VR_WeaponCalibrationStockRangedViewmodel (const char *name)
{
	(void)name;
	++unreachable_admission_calls;
	return false;
}

static void assert_no_private_pose_or_akimbo (const usercmd_t *cmd)
{
	assert (!cmd->vr_active);
	assert (!cmd->vr_handpos_relative);
	assert (!cmd->vr_akimbo_active);
	assert (!cmd->vr_akimbo_berserk);
	for (int i = 0; i < 3; ++i)
	{
		assert (cmd->vr_handpos[i] == 0.0f);
		assert (cmd->vr_handrot[i] == 0.0f);
	}
	for (int hand = 0; hand < 2; ++hand)
		for (int axis = 0; axis < 3; ++axis)
		{
			assert (cmd->vr_akimbo_muzzle[hand][axis] == 0.0f);
			assert (cmd->vr_akimbo_angles[hand][axis] == 0.0f);
		}
}

static void assert_movement (const usercmd_t *cmd, float forward,
	float side, float up)
{
	assert (cmd->forwardmove == forward);
	assert (cmd->sidemove == side);
	assert (cmd->upmove == up);
}

int main (void)
{
	usercmd_t cmd;

	fixture_frame.focused = true;
	fixture_frame.sample_id = 41;
	fixture_frame.devices[0].valid = true;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.stats[STAT_HEALTH] = 100;
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;

	/* Exercise the actual static context capture used by the input adapter. */
	vr_input_context = VR_InputCurrentContext ();
	vr_input_context_valid = true;

	cl.pendingcmd.vr_active = true;
	cl.pendingcmd.vr_handpos_relative = true;
	cl.pendingcmd.vr_handpos[0] = 12.0f;
	cl.pendingcmd.vr_handpos[1] = -3.0f;
	cl.pendingcmd.vr_handpos[2] = 7.0f;
	cl.pendingcmd.vr_handrot[0] = 15.0f;
	cl.pendingcmd.vr_handrot[1] = 25.0f;
	cl.pendingcmd.vr_handrot[2] = 5.0f;
	cl.pendingcmd.vr_akimbo_active = true;
	for (int hand = 0; hand < 2; ++hand)
		for (int axis = 0; axis < 3; ++axis)
		{
			cl.pendingcmd.vr_akimbo_muzzle[hand][axis] = 20.0f + hand + axis;
			cl.pendingcmd.vr_akimbo_angles[hand][axis] = 30.0f + hand + axis;
		}
	/* The producer identity deliberately remains zero/invalid. */
	assert (!vr_input_pending_akimbo_identity.valid);
	cl.pendingcmd.vr_pending_move[0] = 5.0f;
	cl.pendingcmd.vr_pending_move[1] = -2.0f;
	cl.pendingcmd.vr_pending_move[2] = 1.0f;
	cl.pendingcmd.vr_pending_move_valid = true;

	memset (&cmd, 0, sizeof (cmd));
	cmd.forwardmove = 2.0f;
	cmd.sidemove = 3.0f;
	cmd.upmove = -1.0f;
	VR_InputApplyPending (&cmd);
	assert_no_private_pose_or_akimbo (&cmd);
	assert (VR_InputSuppressUncalibratedAttack (&cmd));
	assert_movement (&cmd, 7.0f, 1.0f, 0.0f);
	assert (!cl.pendingcmd.vr_active);
	assert (!cl.pendingcmd.vr_handpos_relative);
	assert (!cl.pendingcmd.vr_akimbo_active);
	assert (unreachable_admission_calls == 0);

	/* A second application of the retained movement sample cannot restore pose. */
	memset (&cmd, 0, sizeof (cmd));
	VR_InputApplyPending (&cmd);
	assert_no_private_pose_or_akimbo (&cmd);
	assert (VR_InputSuppressUncalibratedAttack (&cmd));
	assert_movement (&cmd, 5.0f, -2.0f, 1.0f);
	assert (cl.pendingcmd.vr_pending_move_valid);
	assert (unreachable_admission_calls == 0);

	puts ("stale QBJ3 akimbo identity suppresses private pose and attack while preserving movement");
	return 0;
}
