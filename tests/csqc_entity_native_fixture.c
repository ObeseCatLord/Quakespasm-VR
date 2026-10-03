/* Loaded SSQC/CSQC entity transport lifecycle over the native datagram owners.
 * See run_csqc_entity_native.py for the disposable program and captured-link
 * boundary. This is transport evidence, not a live socket/client claim. */
#ifdef NDEBUG
#error "CSQC entity transport fixture requires assertions"
#endif

#define MIXED_FIXTURE_CUSTOM_UNRELIABLE
#define MIXED_NATIVE_FIXTURE_ENTRY MixedNativeFixture_UnusedEntry
#include "mixed_native_fixture.c"

#include <math.h>
#include <stdint.h>

#define CSQC_FIXTURE_MAX_PACKETS 64
#define CSQC_FIXTURE_PAYLOAD_SIZE 56

typedef struct
{
	byte data[NET_MAXMESSAGE];
	int length;
	int sequence;
} csqc_fixture_packet_t;

static csqc_fixture_packet_t server_packets[CSQC_FIXTURE_MAX_PACKETS];
static int server_packet_count;
static csqc_fixture_packet_t client_packets[CSQC_FIXTURE_MAX_PACKETS];
static int client_packet_count;
static qsocket_t *fixture_server_socket;
static qsocket_t *fixture_client_socket;

int __wrap_NET_SendUnreliableMessage (qsocket_t *socket, sizebuf_t *message)
{
	csqc_fixture_packet_t *packet;
	assert (socket && message && message->cursize > 0 && message->cursize <= NET_MAXMESSAGE);
	if (socket == fixture_server_socket)
	{
		assert (server_packet_count < CSQC_FIXTURE_MAX_PACKETS);
		packet = &server_packets[server_packet_count++];
		packet->sequence = NET_QSocketGetSequenceOut (socket);
		memcpy (packet->data, message->data, message->cursize);
		packet->length = message->cursize;
		/* Match the native unreliable sender's sequence owner while holding
		 * the emitted bytes for explicit later delivery or loss. */
		socket->unreliableSendSequence++;
		return 1;
	}
	if (socket == fixture_client_socket)
	{
		assert (client_packet_count < CSQC_FIXTURE_MAX_PACKETS);
		packet = &client_packets[client_packet_count++];
		packet->sequence = NET_QSocketGetSequenceOut (socket);
		memcpy (packet->data, message->data, message->cursize);
		packet->length = message->cursize;
		socket->unreliableSendSequence++;
		return 1;
	}
	assert (!"unexpected unreliable endpoint in bounded capture");
	return -1;
}

static qcvm_t *FixtureSwitchVM (qcvm_t *vm)
{
	qcvm_t *old = qcvm;
	PR_SwitchQCVM (NULL);
	if (vm)
		PR_SwitchQCVM (vm);
	return old;
}

static ddef_t *FixtureGlobal (qcvm_t *vm, const char *name)
{
	qcvm_t *old = FixtureSwitchVM (vm);
	ddef_t *global = ED_FindGlobal (name);
	assert (global && (global->type & ~DEF_SAVEGLOBAL) == ev_float);
	FixtureSwitchVM (old);
	return global;
}

static float FixtureQCFloat (qcvm_t *vm, const char *name)
{
	qcvm_t *old = FixtureSwitchVM (vm);
	float value = G_FLOAT (FixtureGlobal (vm, name)->ofs);
	FixtureSwitchVM (old);
	return value;
}

static void FixtureSetQCFloat (qcvm_t *vm, const char *name, float value)
{
	qcvm_t *old = FixtureSwitchVM (vm);
	G_FLOAT (FixtureGlobal (vm, name)->ofs) = value;
	FixtureSwitchVM (old);
}

static eval_t *FixtureField (qcvm_t *vm, edict_t *ed, const char *name)
{
	qcvm_t *old = FixtureSwitchVM (vm);
	ddef_t *field = ED_FindField (name);
	assert (field);
	eval_t *value = GetEdictFieldValue (ed, field->ofs);
	assert (value);
	FixtureSwitchVM (old);
	return value;
}

static int FixtureEdictNum (edict_t *ed)
{
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	int number = NUM_FOR_EDICT (ed);
	FixtureSwitchVM (old);
	return number;
}

static edict_t *FixtureServerEdict (int number)
{
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	edict_t *ed = EDICT_NUM (number);
	FixtureSwitchVM (old);
	return ed;
}

static void FixtureLoadClientProgs (void)
{
	qcvm_t *old_vm = qcvm;
	qmodel_t *world = sv.qcvm.worldmodel;
	FixtureSwitchVM (NULL);
	cl.worldmodel = world;
	cl.model_precache[1] = sv.models[1];
	FixtureSwitchVM (&cl.qcvm);
	assert (PR_LoadProgs ("csprogs.dat", true, PROGHEADER_CRC,
		pr_csqcbuiltins, pr_csqcnumbuiltins));
	assert (!memcmp (sv.qcvm.progssha256, qcvm->progssha256,
		sizeof (sv.qcvm.progssha256)));
	qcvm->worldmodel = world;
	qcvm->max_edicts = q_min ((int)max_edicts.value, MAX_EDICTS);
	assert (qcvm->max_edicts >= MIN_EDICTS);
	qcvm->edicts = Mem_Alloc ((size_t)qcvm->max_edicts * qcvm->edict_size);
	assert (qcvm->edicts);
	qcvm->num_edicts = qcvm->reserved_edicts = 1;
#if defined(DEBUG) || defined(_DEBUG)
	for (int i = 0; i < qcvm->max_edicts; ++i)
	{
		edict_t *ed = EDICT_NUM_NO_CHECK (i);
		ed->qcvm_owner = qcvm;
		ed->edict_ptr = ed;
		ed->edict_num = i;
	}
#endif
	assert (qcvm->extfields.entnum >= 0 && qcvm->extfuncs.CSQC_Ent_Update &&
		qcvm->extfuncs.CSQC_Ent_Remove);
	FixtureSwitchVM (old_vm);
}

static void FixturePrepareClientTransport (client_t *peer)
{
	qsocket_t *socket = NET_NewQSocket ();
	assert (socket);
	fixture_server_socket = peer->netconnection;
	fixture_client_socket = socket;
	cl.protocol_pext1 = peer->protocol_pext1 | PEXT1_CSQC;
	cl.protocol_pext2 = peer->protocol_pext2;
	cl.protocol_qsvr = peer->protocol_qsvr;
	cl.protocolflags = sv.protocolflags;
	cl.viewentity = NUM_FOR_EDICT (peer->edict);
	cl.num_entities = 1;
	cl.max_edicts = MAX_EDICTS;
	cl.entities = Mem_Alloc ((size_t)cl.max_edicts * sizeof (*cl.entities));
	assert (cl.entities);
	cl.ackframes_count = 0;
	cl.net_snapshot_have = false;
	cl.time = 0;
	cl.mtime[0] = cl.mtime[1] = 0;
	cls.netcon = socket;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.demoplayback = false;
	assert (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		(cl.protocol_pext2 & PEXT2_REPLACEMENTDELTAS) &&
		(sv_protocol_pext1 & PEXT1_CSQC));
	assert (SV_CSQCTransportAllowed (peer));
	peer->limit_unreliable = 128;
	peer->limit_entities = q_min (peer->limit_entities, (unsigned int)cl.max_edicts);
}

static void FixtureClientString (const char *command)
{
	byte bytes[128];
	sizebuf_t message = {0};
	message.data = bytes;
	message.maxsize = sizeof (bytes);
	MSG_WriteByte (&message, clc_stringcmd);
	MSG_WriteString (&message, command);
	assert (!message.overflowed && message.cursize > 1);
	host_client = &svs.clients[0];
	sv_player = host_client->edict;
	net_message = message;
	assert (SV_ReadClientMessage ());
	SZ_Clear (&host_client->message);
}

static void FixtureEnableClientCSQC (void)
{
	cl.csqc_enable_pending = true;
	memset (&cls.message, 0, sizeof (cls.message));
	cls.message.data = Mem_Alloc (128);
	cls.message.maxsize = 128;
	CL_TryEnableCSQCEntities ();
	assert (!cl.csqc_enable_pending && cls.message.cursize > 1);
	host_client = &svs.clients[0];
	sv_player = host_client->edict;
	net_message = cls.message;
	assert (SV_ReadClientMessage ());
	assert (host_client->csqcactive);
	Mem_Free (cls.message.data);
	memset (&cls.message, 0, sizeof (cls.message));
}

static void FixtureAttachSSQCSendEntity (edict_t *ed, int payload, qboolean send_enabled)
{
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	dfunction_t *send = ED_FindFunction ("fixture_lifecycle_send_entity");
	assert (send);
	FixtureField (&sv.qcvm, ed, "SendEntity")->function = (func_t)(send - qcvm->functions);
	FixtureField (&sv.qcvm, ed, "SendFlags")->_float = send_enabled ? 1 : 0;
	FixtureField (&sv.qcvm, ed, "fixture_lifecycle_payload")->_float = payload;
	eval_t *pvsflags = GetEdictFieldValue (ed, qcvm->extfields.pvsflags);
	assert (pvsflags);
	pvsflags->_float = PVSF_IGNOREPVS;
	VectorCopy (svs.clients[0].edict->v.origin, ed->v.origin);
	ed->v.solid = SOLID_NOT;
	ed->v.movetype = MOVETYPE_NONE;
	FixtureSwitchVM (old);
}

static edict_t *FixtureNewSSQCEntity (int payload)
{
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	edict_t *ed = ED_Alloc ();
	assert (ed && !ed->free);
	FixtureSwitchVM (old);
	FixtureAttachSSQCSendEntity (ed, payload, true);
	return ed;
}

static void FixtureSetSSQCPayload (edict_t *ed, int payload, qboolean send)
{
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	FixtureField (&sv.qcvm, ed, "fixture_lifecycle_payload")->_float = payload;
	FixtureField (&sv.qcvm, ed, "SendFlags")->_float = send ? 1 : 0;
	FixtureSwitchVM (old);
}

static unsigned int FixtureFrameBits (client_t *peer, int sequence, int entnum)
{
	struct deltaframe_s *frame = &peer->frames[sequence & (peer->numframes - 1)];
	assert (frame->sequence == sequence);
	for (int i = 0; i < frame->numents; ++i)
		if (frame->ents[i].num == (size_t)entnum)
			return frame->ents[i].csqcbits;
	return 0;
}

static int FixtureFindPacket (client_t *peer, int entnum, unsigned int mask)
{
	for (int i = 0; i < server_packet_count; ++i)
		if (FixtureFrameBits (peer, server_packets[i].sequence, entnum) & mask)
			return i;
	return -1;
}

typedef struct
{
	int entnum;
	int kind; /* 1=new, 2=update, 3=remove */
	int payload;
} csqc_fixture_event_t;

static void FixturePacketEvents (client_t *peer, const csqc_fixture_packet_t *packet,
	csqc_fixture_event_t *events, int *count)
{
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	struct deltaframe_s *frame = &peer->frames[packet->sequence & (peer->numframes - 1)];
	assert (frame->sequence == packet->sequence);
	*count = 0;
	for (int i = 0; i < frame->numents; ++i)
	{
		unsigned int bits = frame->ents[i].csqcbits;
		int entnum = (int)frame->ents[i].num;
		if (bits & SENDFLAG_REMOVE)
		{
			assert (*count < 8);
			events[*count] = (csqc_fixture_event_t){entnum, 3, 0};
			(*count)++;
		}
		if (bits & SENDFLAG_USABLE)
		{
			assert (*count < 8);
			edict_t *source = EDICT_NUM (entnum);
			assert (!source->free);
			events[*count] = (csqc_fixture_event_t){entnum, 0,
				(int)FixtureField (&sv.qcvm, source, "fixture_lifecycle_payload")-> _float};
			(*count)++;
		}
	}
	FixtureSwitchVM (old);
}

static edict_t *FixtureClientMappedEdict (int entnum)
{
	qcvm_t *old = FixtureSwitchVM (&cl.qcvm);
	edict_t *ed = entnum < (int)cl.ssqc_to_csqc_max ? cl.ssqc_to_csqc[entnum] : NULL;
	FixtureSwitchVM (old);
	return ed;
}

static int FixtureClientEntityPayload (edict_t *ed)
{
	assert (ed);
	return (int)FixtureField (&cl.qcvm, ed, "fixture_lifecycle_value")-> _float;
}

static int FixtureTraceCode (int entnum, int kind)
{
	return entnum * 4 + kind;
}

static void FixtureParsePacket (client_t *peer, const csqc_fixture_packet_t *packet,
	qboolean deliver_ack)
{
	csqc_fixture_event_t events[8];
	int count, expected_updates = 0, expected_new = 0, expected_removes = 0;
	int expected_trace = 0, expected_last_id = 0, expected_last_kind = 0, expected_last_payload = 0;
	assert (packet->sequence >= 0 && packet->length > 0);
	FixturePacketEvents (peer, packet, events, &count);
	assert (count <= 2); /* 128-byte bound and 56-byte actual QC payload. */
	for (int i = 0; i < count; ++i)
	{
		csqc_fixture_event_t *event = &events[i];
		edict_t *current = FixtureClientMappedEdict (event->entnum);
		if (event->kind == 3)
		{
			assert (current);
			event->payload = FixtureClientEntityPayload (current);
			expected_removes++;
			expected_trace = expected_trace * 2048 + FixtureTraceCode (event->entnum, 3);
			expected_last_kind = 3;
		}
		else
		{
			qboolean isnew = !current || (i && events[i - 1].entnum == event->entnum &&
				events[i - 1].kind == 3);
			event->kind = isnew ? 1 : 2;
			expected_updates++;
			expected_new += isnew;
			event->payload = (int)FixtureField (&sv.qcvm, FixtureServerEdict (event->entnum),
				"fixture_lifecycle_payload")-> _float;
			expected_trace = expected_trace * 2048 + FixtureTraceCode (event->entnum, event->kind);
			expected_last_kind = event->kind;
		}
		expected_last_id = event->entnum;
		expected_last_payload = event->payload;
	}
	int before_updates = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count");
	int before_new = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_new_count");
	int before_removes = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_remove_count");
	FixtureSetQCFloat (&cl.qcvm, "fixture_lifecycle_trace", 0);
	fixture_client_socket->unreliableReceiveSequence = (unsigned int)packet->sequence + 1;
	net_message.data = (byte *)packet->data;
	net_message.cursize = packet->length;
	net_message.maxsize = packet->length;
	CL_ParseServerMessage ();
	/* CL_ParseServerMessage probes one byte past a clean end to receive -1. */
	assert (msg_readcount == net_message.cursize);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count") == before_updates + expected_updates);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_new_count") == before_new + expected_new);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_remove_count") == before_removes + expected_removes);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_trace") == expected_trace);
	if (count)
	{
		assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_id") == expected_last_id);
		if (expected_last_kind == 3)
		{
			assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_removed_id") == expected_last_id);
			assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_removed_payload") == expected_last_payload);
			assert (!FixtureClientMappedEdict (expected_last_id));
		}
		else
		{
			edict_t *mapped_edict = FixtureClientMappedEdict (expected_last_id);
			assert (mapped_edict && FixtureClientEntityPayload (mapped_edict) == expected_last_payload);
			assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_payload") == expected_last_payload);
			assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_new") == (expected_last_kind == 1));
		}
	}
	if (deliver_ack)
	{
		client_packet_count = 0;
		CL_FlushAckFrames ();
		assert (client_packet_count == 1);
		net_message.data = client_packets[0].data;
		net_message.cursize = client_packets[0].length;
		net_message.maxsize = client_packets[0].length;
		host_client = peer;
		sv_player = peer->edict;
		assert (SV_ReadClientMessage ()); /* production clc_ackframe -> SVFTE_Ack */
	}
}

static void FixtureSendFrame (client_t *peer)
{
	server_packet_count = 0;
	SZ_Clear (&sv.datagram);
	assert (SV_PresendClientDatagram (peer));
	MSG_WriteChar (&sv.datagram, svc_nop);
	host_client = peer;
	sv_player = peer->edict;
	assert (SV_SendClientDatagram (peer));
	assert (server_packet_count > 0);
	assert (peer->snapshotresume >= peer->numpendingentities);
	assert (peer->csqcsnapshotresume == peer->numpendingcsqcentities);
}

static void FixtureDeliverAll (client_t *peer, int skip_index)
{
	int delivered_after_skip = 0;
	for (int i = 0; i < server_packet_count; ++i)
	{
		if (i == skip_index)
			continue; /* An actual emitted packet is withheld as network loss. */
		if (skip_index >= 0 && i > skip_index)
			delivered_after_skip++;
		if (i == server_packet_count - 1)
			assert (server_packets[i].data[server_packets[i].length - 1] == svc_nop);
		FixtureParsePacket (peer, &server_packets[i], true);
	}
	if (skip_index >= 0)
		assert (delivered_after_skip > 0);
}

static void FixtureCheckEntityValue (int entnum, int payload)
{
	edict_t *ed = FixtureClientMappedEdict (entnum);
	assert (ed && FixtureClientEntityPayload (ed) == payload);
}

static edict_t *FixtureAllocateRecycledIdAfterAck (int entnum)
{
	edict_t *drained[MAX_EDICTS];
	int count = 0;
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	while (count < MAX_EDICTS)
	{
		edict_t *ed = ED_Alloc ();
		assert (ed && !ed->free);
		if (NUM_FOR_EDICT (ed) == entnum)
		{
			for (int i = 0; i < count; ++i)
				ED_Free (drained[i]);
			FixtureSwitchVM (old);
			return ed;
		}
		drained[count++] = ed;
	}
	assert (!"retired SSQC edict was absent from normal allocator after removal ACK");
	FixtureSwitchVM (old);
	return NULL;
}

static void FixtureLifecycle (client_t *peer)
{
	edict_t *entities[3];
	int ids[3];
	for (int i = 0; i < 3; ++i)
	{
		entities[i] = FixtureNewSSQCEntity (10 + i * 10);
		ids[i] = FixtureEdictNum (entities[i]);
	}

	FixtureSendFrame (peer);
	assert (server_packet_count >= 3);
	FixtureDeliverAll (peer, -1);
	for (int i = 0; i < 3; ++i)
	{
		FixtureCheckEntityValue (ids[i], 10 + i * 10);
		FixtureSetSSQCPayload (entities[i], 10 + i * 10, false);
	}
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count") == 3);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_new_count") == 3);
	puts ("CSQC_ENTITY_NATIVE_CREATE_PASSED loaded SSQC SendEntity -> captured split datagrams -> loaded CSQC Ent_Update; exact per-frame order/payload/current edicts; native ACKs");

	for (int i = 0; i < 3; ++i)
		FixtureSetSSQCPayload (entities[i], 11 + i * 10, true);
	FixtureSendFrame (peer);
	assert (server_packet_count >= 3);
	int lost_update = FixtureFindPacket (peer, ids[1], SENDFLAG_USABLE);
	assert (lost_update > 0 && lost_update < server_packet_count - 1);
	FixtureDeliverAll (peer, lost_update);
	assert (FixtureClientEntityPayload (FixtureClientMappedEdict (ids[0])) == 11);
	assert (FixtureClientEntityPayload (FixtureClientMappedEdict (ids[1])) == 20);
	assert (FixtureClientEntityPayload (FixtureClientMappedEdict (ids[2])) == 31);
	for (int i = 0; i < 3; ++i)
		FixtureSetSSQCPayload (entities[i], 11 + i * 10, false);
	int prior_updates = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count");
	FixtureSendFrame (peer);
	int resent = FixtureFindPacket (peer, ids[1], SENDFLAG_USABLE);
	assert (resent >= 0);
	FixtureDeliverAll (peer, -1);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count") == prior_updates + 1);
	for (int i = 0; i < 3; ++i)
		FixtureCheckEntityValue (ids[i], 11 + i * 10);
	printf ("CSQC_ENTITY_NATIVE_SPLIT_UPDATE_ACK_PASSED withheld_seq=%d later_ack_gap_requeued_entity=%d recovered_payload=21 callbacks=%d\n",
		server_packets[lost_update].sequence, ids[1], prior_updates + 1);

	FixtureClientString ("disablecsqc");
	assert (!peer->csqcactive);
	int before_disable = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count");
	FixtureSendFrame (peer);
	FixtureDeliverAll (peer, -1);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count") == before_disable);
	FixtureEnableClientCSQC ();
	assert (peer->csqcactive);
	FixtureSendFrame (peer);
	FixtureDeliverAll (peer, -1);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_update_count") == before_disable + 3);
	for (int i = 0; i < 3; ++i)
		FixtureCheckEntityValue (ids[i], 11 + i * 10);
	puts ("CSQC_ENTITY_NATIVE_REENABLE_PASSED actual client enablecsqc writer/server command parser; retained mappings updated once each");

	int remove_id = ids[0];
	FixtureSetSSQCPayload (entities[1], 52, true);
	FixtureSetSSQCPayload (entities[2], 62, true);
	qcvm_t *old = FixtureSwitchVM (&sv.qcvm);
	ED_Free (entities[0]);
	FixtureSwitchVM (old);
	FixtureSendFrame (peer);
	assert (server_packet_count >= 2);
	int lost_remove = FixtureFindPacket (peer, remove_id, SENDFLAG_REMOVE);
	assert (lost_remove >= 0 && lost_remove < server_packet_count - 1);
	int lost_remove_sequence = server_packets[lost_remove].sequence;
	FixtureDeliverAll (peer, lost_remove);
	assert (FixtureClientMappedEdict (remove_id));
	assert (peer->pendingcsqcentities_bits[remove_id] & SENDFLAG_REMOVEWAIT);
	assert (peer->pendingcsqcentities_bits[remove_id] & SENDFLAG_REMOVE);
	FixtureSetSSQCPayload (entities[1], 52, false);
	FixtureSetSSQCPayload (entities[2], 62, false);
	/* ACKs for the later split packets caused the native sequence-gap owner
	 * to requeue the withheld removal for the next emitted frame. */
	FixtureSendFrame (peer);
	int remove_replay = FixtureFindPacket (peer, remove_id, SENDFLAG_REMOVE);
	assert (remove_replay >= 0);
	FixtureDeliverAll (peer, -1);
	assert (!FixtureClientMappedEdict (remove_id));
	assert (!(peer->pendingcsqcentities_bits[remove_id] & SENDFLAG_REMOVEWAIT));
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_remove_count") == 1);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_removed_payload") == 11);
	printf ("CSQC_ENTITY_NATIVE_REMOVE_ACK_PASSED withheld_seq=%d replay_seq=%d exactly_one_remove old_payload=11 removewait_cleared\n",
		lost_remove_sequence, server_packets[remove_replay].sequence);

	/* Do not ask the allocator for this retired number until its own removal
	 * callback has been parsed and the native ACK owner clears REMOVEWAIT. */
	int prior_new_count = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_new_count");
	edict_t *reused = FixtureAllocateRecycledIdAfterAck (remove_id);
	assert (FixtureEdictNum (reused) == remove_id);
	FixtureAttachSSQCSendEntity (reused, 41, true);
	FixtureSendFrame (peer);
	FixtureDeliverAll (peer, -1);
	FixtureCheckEntityValue (remove_id, 41);
	int reused_new_count = (int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_new_count");
	assert (reused_new_count == prior_new_count + 1);
	assert ((int)FixtureQCFloat (&cl.qcvm, "fixture_lifecycle_last_new") == 1);
	printf ("CSQC_ENTITY_NATIVE_REUSE_PASSED normal allocator reused acknowledged id; exactly_one_new=%d payload=41\n",
		reused_new_count - prior_new_count);
}

int main (int argc, char **argv)
{
	char offer[1024];
	client_t *peer;
	setvbuf (stdout, NULL, _IONBF, 0);
	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	assert (COM_CheckParm ("-csqc-entity-native"));
	assert (svs.maxclients >= 2);
	FixtureLoadClientProgs ();
	Cvar_SetQuick (&sv_qsvr_private, "1");
	Cvar_SetQuick (&sv_private_pmove_walk, "0");
	ClientOffer (0, false, offer, sizeof (offer));
	peer = SpawnPeer (0, offer, QSVR_PROTOCOL_PINNED);
	FixturePrepareClientTransport (peer);
	FixtureEnableClientCSQC ();
	assert (peer->csqcactive);
	FixtureLifecycle (peer);
	CL_ClearCSQCEntities (false);
	PR_ClearProgs (&cl.qcvm);
	Mem_Free (cl.entities);
	cl.entities = NULL;
	cl.max_edicts = cl.num_entities = 0;
	puts ("CSQC_ENTITY_NATIVE_PASSED actual SV_PresendClientDatagram/SVFTE_BeginFrame/SVFTE_WriteCSQCEntitiesToClient/CL_ParseServerMessage and native ACK owners; captured unreliable boundary");
	return 0;
}
