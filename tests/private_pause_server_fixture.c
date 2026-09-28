/* Execute the production selected pause transition and reliable marker parser.
 * The network, QuakeC and physics boundaries are intentionally absent here. */
#include "../Quake/sv_user.c"
#include <assert.h>
#include <stdio.h>

server_t sv;
server_static_t svs;
client_t *host_client;
keydest_t key_dest;
double realtime;
qcvm_t *qcvm;
sizebuf_t net_message;
cvar_t sv_gameplayfix_elevators;
static int contact_resets;

void Sys_Printf (const char *format, ...) { assert (0); }
void Sys_Error (const char *format, ...) { assert (0); }
void Host_Error (const char *format, ...) { assert (0); }
void Con_Printf (const char *format, ...) { assert (0); }

qboolean SV_PrivateWalkTrialTerminalState (client_t *client)
{
	return false;
}

const char *SV_PrivateWalkTrialFrameStateError (edict_t *ent, client_t *client,
	const usercmd_t *cmd)
{
	return NULL; // physics/state qualification is outside this pause-only fixture
}

qboolean SV_PrivateWalkTrialSelected (client_t *client)
{
	return client && client->private_pmove_walk_selected;
}

void SV_ResetPrivateVRContactState (client_t *client)
{
	contact_resets++;
	client->private_vr_contact_previous_valid = false;
}

eval_t *GetEdictFieldValue (edict_t *ent, int field)
{
	return NULL;
}

static qboolean send_move (unsigned int sequence, unsigned int buttons,
	unsigned int impulse)
{
	byte bytes[64];
	memset (&net_message, 0, sizeof (net_message));
	net_message.data = bytes;
	net_message.maxsize = sizeof (bytes);
	MSG_WriteShort (&net_message, sequence & 0xffff);
	MSG_WriteFloat (&net_message, 4.0f);
	MSG_WriteByte (&net_message, 15);
	for (int axis = 0; axis < 3; ++axis)
		MSG_WriteShort (&net_message, 0);
	for (int axis = 0; axis < 3; ++axis)
		MSG_WriteShort (&net_message, 0);
	MSG_WriteByte (&net_message, buttons);
	MSG_WriteByte (&net_message, impulse);
	MSG_WriteByte (&net_message, MOVEEXT_VR | MOVEEXT_VR_RELATIVE);
	for (int axis = 0; axis < 3; ++axis)
		MSG_WriteFloat (&net_message, 0); /* relative hand position */
	for (int axis = 0; axis < 3; ++axis)
		MSG_WriteFloat (&net_message, 0); /* hand rotation */
	for (int axis = 0; axis < 3; ++axis)
		MSG_WriteFloat (&net_message, axis == 0 ? 4.0f : 0);
	MSG_BeginReading ();
	return SV_ReadPrivateClientMove () && !msg_badread &&
		msg_readcount == net_message.cursize;
}

int main (void)
{
	client_t client = {0};
	edict_t owner = {0};
	qcvm = &sv.qcvm;
	host_client = &client;
	client.edict = &owner;
	client.active = true;
	client.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	owner.v.movetype = MOVETYPE_WALK;
	owner.v.solid = SOLID_SLIDEBOX;
	client.private_pmove_walk_selected = true;
	client.private_completed_move = 100;
	client.lastmovemessage = 104;
	client.private_pmove_last_cmd_valid = true;
	client.private_pmove_credit_msec = 87;
	client.private_cmd_queue_count = 1;
	client.private_cmd_queue_msec = 15;
	client.private_cmd_queue[0].sequence = 104;
	client.cmd.buttons = 1;
	client.cmd.impulse = 7;
	client.cmd.vr_roomscalemove[0] = 4;
	owner.v.button0 = 1;
	owner.v.impulse = 7;
	svs.maxclients = 2;
	key_dest = key_game;
	realtime = 5;

	sv.paused = true;
	SV_PrivateSyncPauseState (&client);
	assert (client.private_input_phase == PRIVATE_INPUT_SUSPENDED);
	assert (client.private_completed_move == 100 &&
		client.private_discarded_move == 104 &&
		!client.private_cmd_queue_count && !client.private_cmd_queue_msec);
	assert (!client.private_pmove_last_cmd_valid &&
		!client.private_pmove_credit_msec && !client.cmd.buttons &&
		!client.cmd.impulse && !client.cmd.vr_roomscalemove[0] &&
		!owner.v.button0 && !owner.v.impulse && contact_resets == 1);
	SV_PrivateSyncPauseState (&client);
	assert (contact_resets == 1 &&
		client.private_move_discontinuity_epoch == 0);
	assert (send_move (105, 1, 7) && client.lastmovemessage == 105 &&
		!client.private_cmd_queue_count && client.private_completed_move == 100);

	sv.paused = false;
	SV_PrivateSyncPauseState (&client);
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
		client.private_move_discontinuity_epoch == 1 &&
		client.private_move_discontinuity_reason == MOVEACK_DISCONTINUITY_GAP);
	SV_PrivateSyncPauseState (&client);
	assert (client.private_move_discontinuity_epoch == 1);
	assert (send_move (106, 1, 7) && client.lastmovemessage == 106 &&
		!client.private_cmd_queue_count);
	assert (SV_HandlePrivateResumeMarker ("qsvr_resume 0 107"));
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_MARKER);
	assert (SV_HandlePrivateResumeMarker ("qsvr_resume 1 107x"));
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_MARKER);
	assert (SV_HandlePrivateResumeMarker ("qsvr_resume 1 108"));
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
		client.private_resume_first_sequence == 108 &&
		client.lastmovemessage == 107 &&
		client.private_completed_move == 100);
	assert (send_move (108, 1, 7) && client.private_cmd_queue_count == 1 &&
		client.private_cmd_queue[0].sequence == 108 &&
		client.private_cmd_queue[0].vr_roomscalemove[0] == 4.0f &&
		client.private_completed_move == 100);

	/* A second pause while waiting for completion discards that generation. */
	client.private_cmd_queue_count = 1;
	client.private_cmd_queue[0].sequence = 108;
	sv.paused = true;
	SV_PrivateSyncPauseState (&client);
	assert (client.private_input_phase == PRIVATE_INPUT_SUSPENDED &&
		!client.private_cmd_queue_count);
	sv.paused = false;
	SV_PrivateSyncPauseState (&client);
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_MARKER &&
		client.private_move_discontinuity_epoch == 2);
	/* A relocation before the marker retains its teleport reason. The
	 * snapshot's separate RESUME_PENDING flag keeps the handshake alive. */
	svs.clients = &client;
	svs.maxclients = 1;
	SV_PrivatePlayerTeleported (&owner);
	svs.maxclients = 2;
	assert (client.private_move_discontinuity_epoch == 3 &&
		client.private_move_discontinuity_reason ==
			MOVEACK_DISCONTINUITY_RESET_TELEPORT);
	assert (SV_HandlePrivateResumeMarker ("qsvr_resume 1 109"));
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_MARKER);
	assert (SV_HandlePrivateResumeMarker ("qsvr_resume 2 109"));
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_MARKER);
	assert (SV_HandlePrivateResumeMarker ("qsvr_resume 3 109"));
	assert (client.private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
		client.lastmovemessage == 108 && client.private_discarded_move == 108);
	puts ("Private selected pause: discard, marker, repeat and ACK cursor checks passed");
	return 0;
}
