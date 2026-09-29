/* Actual VR input continuity/roomscale owner, with prepared gameplay context
 * and mapping/UI boundaries. No headset, button dispatcher or renderer. */
#include "../Quake/vr_input.c"
#include <assert.h>

client_state_t cl;
client_static_t cls;
keydest_t key_dest = key_game;
enum m_state_e m_state = m_none;
cvar_t vr_aimmode = {"vr_aimmode", "7", CVAR_NONE};

qboolean M_WaitingForKeyBinding (void) { return false; }
qboolean Key_InputGrabActive (void) { return false; }
qboolean CL_AngleLocked (void) { return false; }
qboolean V_TrackedMappingYaw (float *yaw) { *yaw = 0; return true; }
float V_VRUnitsPerMetre (void) { return 10; }
void Con_DPrintf (const char *format, ...) {}
static int released_keys;
void Key_Event (int key, qboolean down)
{ assert ((key == K_LTRIGGER || key == K_RTRIGGER) && !down); ++released_keys; }

int main (void)
{
	vrxr_frame_t frame = {0};
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	frame.focused = frame.devices[0].valid = true;
	vr_input_context = VR_InputCurrentContext ();
	vr_input_context_valid = true;
	VR_InputAccumulateRoomscaleMove (&frame, &cl.pendingcmd);
	frame.devices[0].matrix[2][3] = -.1f;
	VR_InputAccumulateRoomscaleMove (&frame, &cl.pendingcmd);
	assert (fabsf (cl.pendingcmd.vr_roomscalemove[0] - 1) < .0001f);
	vr_input_move_wait_neutral = vr_input_turn_wait_neutral = false;
	vr_input_last_snap = 1; // a held snap-stick must not turn again on recovery
	vr_input_turn180_queued = true;
	cl.pendingcmd.vr_contact.flags = 1;
	cl.pendingcmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	VR_InputResetMotionContinuity ();
	assert (!cl.pendingcmd.vr_roomscalemove[0] && !cl.pendingcmd.vr_contact.flags &&
		!cl.pendingcmd.vr_gorilla.flags && !vr_input_turn180_queued &&
		!vr_input_move_wait_neutral && !vr_input_turn_wait_neutral &&
		vr_input_last_snap == 1);
	frame.devices[0].matrix[2][3] = -.3f;
	VR_InputAccumulateRoomscaleMove (&frame, &cl.pendingcmd);
	assert (!cl.pendingcmd.vr_roomscalemove[0]); // first fresh sample is a baseline
	frame.devices[0].matrix[2][3] = -.4f;
	VR_InputAccumulateRoomscaleMove (&frame, &cl.pendingcmd);
	assert (fabsf (cl.pendingcmd.vr_roomscalemove[0] - 1) < .0001f);
	/* Reusing the narrower operation must retain full focus-loss gating. */
	VR_InputInvalidateMotion ();
	assert (vr_input_move_wait_neutral && vr_input_turn_wait_neutral &&
		!vr_input_last_snap && !cl.pendingcmd.vr_roomscalemove[0]);
	frame.devices[0].matrix[2][3] = -.8f;
	VR_InputAccumulateRoomscaleMove (&frame, &cl.pendingcmd);
	assert (!cl.pendingcmd.vr_roomscalemove[0]);
	/* Same release/neutral helpers called by missing/unfocused frame handling.
	 * This does not execute the complete VR_InputCommands dispatcher. */
	vr_input_hands[0].owned[K_LTRIGGER] = true;
	vr_input_hands[1].owned[K_RTRIGGER] = true;
	vr_input_emitted[K_LTRIGGER] = vr_input_emitted[K_RTRIGGER] = true;
	vr_input_hands[0].trigger_down = vr_input_hands[1].trigger_down = true;
	assert (VR_InputGateAndReleaseAll (vr_input_dispatch_epoch));
	assert (released_keys == 2 && !vr_input_emitted[K_LTRIGGER] && !vr_input_emitted[K_RTRIGGER]);
	assert (vr_input_hands[0].wait_neutral && vr_input_hands[1].wait_neutral &&
		!vr_input_hands[0].trigger_down && !vr_input_hands[1].trigger_down);
	vrxr_input_t hand = {0}; hand.active = true; hand.trigger = 1;
	assert (!VR_InputNeutral (&hand));
	hand.trigger = 0; hand.stick[0] = 1;
	assert (!VR_InputNeutral (&hand));
	hand.stick[0] = 0;
	assert (VR_InputNeutral (&hand));
	puts ("VR_MOTION_CONTINUITY_PASSED fresh roomscale baseline; pending contact/Gorilla/turn discard; analog rearm/snap latches retained; full invalidation unchanged");
	return 0;
}
