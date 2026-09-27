/* Exercise the inherited local reconnect and 32-bit loopback sequence path. */
#include "../Quake/net_loop.c"
#include <assert.h>

static qsocket_t sockets[2];
static int allocated;
static byte message_data[NET_MAXMESSAGE];
sizebuf_t net_message;

qsocket_t *NET_NewQSocket (void)
{
	assert (allocated < 2);
	return &sockets[allocated++];
}
void Con_Printf (const char *fmt, ...) { (void)fmt; }
void Sys_Error (const char *fmt, ...) { (void)fmt; abort (); }
void SZ_Clear (sizebuf_t *buf) { buf->cursize = 0; }
void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	assert (length >= 0 && buf->cursize + length <= buf->maxsize);
	memcpy (buf->data + buf->cursize, data, length);
	buf->cursize += length;
}

int main (void)
{
	qsocket_t *client, *server;
	byte payload = 0x42;
	sizebuf_t message = {0};

	net_message.data = message_data;
	net_message.maxsize = sizeof message_data;
	message.data = &payload;
	message.cursize = message.maxsize = 1;
	client = Loop_Connect ("local");
	assert (client == &sockets[0]);
	server = Loop_CheckNewConnections ();
	assert (server == &sockets[1]);
	client->unreliableSendSequence = 0x80000000u;
	assert (Loop_SendUnreliableMessage (client, &message) == 1);
	assert (Loop_GetMessage (server) == 2);
	assert (server->unreliableReceiveSequence == 0x80000001u);
	assert (net_message.cursize == 1 && net_message.data[0] == payload);

	client->ackSequence = server->sendSequence = 77;
	server->receiveSequence = client->unreliableReceiveSequence = 91;
	assert (Loop_Connect ("local") == client);
	assert (Loop_CheckNewConnections () == server);
	assert (!client->ackSequence && !client->sendSequence &&
		!client->unreliableSendSequence && !client->receiveSequence &&
		!client->unreliableReceiveSequence);
	assert (!server->ackSequence && !server->sendSequence &&
		!server->unreliableSendSequence && !server->receiveSequence &&
		!server->unreliableReceiveSequence);
	assert (client->receiveMessageLength == 0 && server->receiveMessageLength == 0);
	assert (Loop_SendMessage (client, &message) == 1 && !client->canSend);
	assert (Loop_GetMessage (server) == 1 && client->canSend);

	assert (Loop_Connect ("local") == client);
	Loop_Close (client);
	assert (Loop_CheckNewConnections () == NULL);
	assert (Loop_SendUnreliableMessage (client, &message) == -1);
	assert (client->unreliableSendSequence == 0);
	puts ("loopback sequence fixture: ok");
	return 0;
}
