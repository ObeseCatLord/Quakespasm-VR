/* Exercise the real virtual-client receive path with a scripted UDP boundary.
 * No game data, runtime, or performance measurements are required. */
#include "../Quake/net_dgrm.c"
#include <assert.h>

static byte message_data[128];
static struct {
	struct qsockaddr addr;
	unsigned int length;
	byte bytes[256];
} incoming[64];
static int in_count, in_next, writes;
static struct qsockaddr last_written;

qsocket_t *net_activeSockets;
net_landriver_t net_landrivers[1];
const int net_numlandrivers = 1;
int net_driverlevel = 1;
double net_time = 100;
sizebuf_t net_message;
int (*BigLong) (int) = NULL;
int messagesReceived, unreliableMessagesReceived;

static int identity (int value) { return value; }
static struct qsockaddr ipv4 (unsigned int ip, unsigned short port)
{
	struct qsockaddr result = {0};
	struct sockaddr_in *addr = (struct sockaddr_in *)&result;
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = htonl (ip);
	addr->sin_port = htons (port);
	return result;
}
static int compare_addr (struct qsockaddr *a, struct qsockaddr *b)
{
	if (a->qsa_family == AF_INET6 && b->qsa_family == AF_INET6)
	{
		struct sockaddr_in6 *left6 = (struct sockaddr_in6 *)a, *right6 = (struct sockaddr_in6 *)b;
		if (memcmp (&left6->sin6_addr, &right6->sin6_addr, sizeof left6->sin6_addr))
			return -1;
		return left6->sin6_port == right6->sin6_port ? 0 : 1;
	}
	struct sockaddr_in *left = (struct sockaddr_in *)a, *right = (struct sockaddr_in *)b;
	if (left->sin_family != right->sin_family || left->sin_addr.s_addr != right->sin_addr.s_addr)
		return -1;
	return left->sin_port == right->sin_port ? 0 : 1;
}
static const char *format_addr (struct qsockaddr *addr, qboolean masked)
{
	(void)addr;
	return masked ? "masked" : "address";
}
size_t q_strlcpy (char *dst, const char *src, size_t capacity)
{
	size_t length = strlen (src);
	if (capacity)
	{
		size_t copied = length < capacity - 1 ? length : capacity - 1;
		memcpy (dst, src, copied);
		dst[copied] = 0;
	}
	return length;
}
static int read_packet (sys_socket_t socket, byte *out, int capacity, struct qsockaddr *addr)
{
	assert (socket == 9);
	if (in_next == in_count)
		return 0;
	assert (incoming[in_next].length <= (unsigned int)capacity);
	*addr = incoming[in_next].addr;
	memcpy (out, incoming[in_next].bytes, incoming[in_next].length);
	return incoming[in_next++].length;
}
static int write_packet (sys_socket_t socket, byte *data, int length, struct qsockaddr *addr)
{
	assert (socket == 9 && data && length >= NET_HEADERSIZE);
	last_written = *addr;
	writes++;
	return length;
}
static void push (struct qsockaddr addr, unsigned int flags, unsigned int sequence, unsigned int payload)
{
	unsigned int header = flags | (NET_HEADERSIZE + payload);
	assert (in_count < (int)(sizeof incoming / sizeof incoming[0]));
	assert (NET_HEADERSIZE + payload <= sizeof incoming[0].bytes);
	incoming[in_count].addr = addr;
	incoming[in_count].length = NET_HEADERSIZE + payload;
	memcpy (incoming[in_count].bytes, &header, 4);
	memcpy (incoming[in_count].bytes + 4, &sequence, 4);
	memset (incoming[in_count].bytes + NET_HEADERSIZE, 'x', payload);
	in_count++;
}
void Con_Printf (const char *fmt, ...) { (void)fmt; }
void Con_DPrintf (const char *fmt, ...) { (void)fmt; }
void SZ_Clear (sizebuf_t *buf) { buf->cursize = 0; }
void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	assert (length >= 0 && buf->cursize + length <= buf->maxsize);
	memcpy (buf->data + buf->cursize, data, length);
	buf->cursize += length;
}

int main (void)
{
	qsocket_t a = {0}, b = {0};
	struct qsockaddr a_old = ipv4 (0x7f000001, 3001), a_new = ipv4 (0x7f000001, 4001);
	struct qsockaddr b_old = ipv4 (0x7f000001, 3002), b_new = ipv4 (0x7f000001, 4002);
	struct qsockaddr unknown = ipv4 (0x7f000001, 4999);
	sizebuf_t outgoing = {0};
	byte payload = 'o';

	BigLong = identity;
	net_message.data = message_data;
	net_message.maxsize = sizeof message_data;
	net_landrivers[0].initialized = true;
	net_landrivers[0].AddrCompare = compare_addr;
	net_landrivers[0].AddrToString = format_addr;
	net_landrivers[0].Read = read_packet;
	net_landrivers[0].Write = write_packet;
	a.next = &b;
	net_activeSockets = &a;
	a.isvirtual = b.isvirtual = true;
	a.driver = b.driver = 1;
	a.landriver = b.landriver = 0;
	a.socket = b.socket = 9;
	a.canSend = b.canSend = true;
	a.previous_addr_time = b.previous_addr_time = -1;
	a.addr = a_old;
	b.addr = b_old;
	b.unreliableReceiveSequence = 100;

	/* A per-client read must defer B's packet, then accept A's new port. */
	push (b_old, NETFLAG_UNRELIABLE, 100, 1);
	push (a_new, NETFLAG_UNRELIABLE, 0, 1);
	assert (Datagram_GetMessage (&a) == 2);
	assert (compare_addr (&a.addr, &a_new) == 0 && b.unreliableReceiveSequence == 100);
	assert (Datagram_GetMessage (&b) == 2 && b.unreliableReceiveSequence == 101);

	outgoing.data = &payload;
	outgoing.cursize = outgoing.maxsize = 1;
	assert (Datagram_SendUnreliableMessage (&a, &outgoing) == 1);
	assert (compare_addr (&last_written, &a_new) == 0);

	/* A delayed old-port packet can be read briefly but cannot undo the remap. */
	push (a_old, NETFLAG_UNRELIABLE, 1, 1);
	assert (Datagram_GetMessage (&a) == 2);
	assert (compare_addr (&a.addr, &a_new) == 0);
	net_time += 4;
	push (a_old, NETFLAG_UNRELIABLE, 2, 1);
	assert (Datagram_GetMessage (&a) == 0 && compare_addr (&a.addr, &a_new) == 0);

	/* A retransmitted reliable fragment identifies B's new port and is ACKed
	 * there without delivering the same application message twice. */
	push (b_old, NETFLAG_DATA | NETFLAG_EOM, 0, 1);
	assert (Datagram_GetMessage (&b) == 1 && b.receiveSequence == 1);
	a.receiveSequence = 5;
	push (b_new, NETFLAG_DATA | NETFLAG_EOM, 0, 1);
	assert (Datagram_GetMessage (&b) == 0 && b.receiveSequence == 1);
	assert (compare_addr (&b.addr, &b_new) == 0 && compare_addr (&last_written, &b_new) == 0);

	/* Both players could plausibly own this packet. No one gets moved. */
	a.receiveSequence = 1;
	push (unknown, NETFLAG_DATA | NETFLAG_EOM, 1, 1);
	assert (Datagram_GetMessage (&a) == 0);
	assert (compare_addr (&a.addr, &a_new) == 0 && compare_addr (&b.addr, &b_new) == 0);

	/* A malformed large payload cannot return the previous net_message. */
	push (a_new, NETFLAG_UNRELIABLE, 2, sizeof message_data + 1);
	assert (Datagram_GetMessage (&a) == 0 && a.unreliableReceiveSequence == 2);

	/* The deferred inbox is bounded and a closed owner leaves no packet behind. */
	for (unsigned int sequence = 101; sequence < 121; sequence++)
		push (b_new, NETFLAG_UNRELIABLE, sequence, 1);
	assert (Datagram_GetMessage (&a) == 0);
	{
		int pending = 0;
		for (int i = 0; i < MAX_PENDING_DATAGRAMS; i++)
			pending += pendingDatagrams[i].valid;
		assert (pending == MAX_PENDING_DATAGRAMS);
	}
	Datagram_DropQueuedPackets (&b);
	assert (Datagram_GetMessage (&b) == 0);

#ifdef IPPROTO_IPV6
	{
		struct qsockaddr first = {0}, second = {0};
		struct sockaddr_in6 *first6 = (struct sockaddr_in6 *)&first, *second6 = (struct sockaddr_in6 *)&second;
		first6->sin6_family = second6->sin6_family = AF_INET6;
		first6->sin6_scope_id = 1;
		second6->sin6_scope_id = 2;
		assert (!Datagram_SameHost (0, &first, &second));
	}
#endif

	puts ("datagram rebinding fixture: ok");
	return 0;
}
