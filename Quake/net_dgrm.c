/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// This is enables a simple IP banning mechanism
#define BAN_TEST

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"
#include "net_defs.h"
#include "net_dgrm.h"

// these two macros are to make the code more readable
#define sfunc	net_landrivers[sock->landriver]
#define dfunc	net_landrivers[net_landriverlevel]

static int net_landriverlevel;

/* statistic counters */
static int packetsSent = 0;
static int packetsReSent = 0;
static int packetsReceived = 0;
static int receivedDuplicateCount = 0;
static int shortPacketCount = 0;
static int droppedDatagrams;

static struct
{
	unsigned int	length;
	unsigned int	sequence;
	byte	data[MAX_DATAGRAM];
} packetBuffer;

static int myDriverLevel;
cvar_t net_lagdebug = {"net_lagdebug", "0", CVAR_NONE};
cvar_t net_lagdebug_threshold = {"net_lagdebug_threshold", "0.25", CVAR_NONE};
cvar_t net_lagdebug_frame_threshold = {"net_lagdebug_frame_threshold", "0.05", CVAR_NONE};
static cvar_t net_sameip_stale_timeout = {"net_sameip_stale_timeout", "3.0", CVAR_NONE};
static cvar_t cl_netport = {"cl_netport", "0", CVAR_ARCHIVE};
cvar_t cl_portpingprobe_enable = {"cl_portpingprobe_enable", "0", CVAR_ARCHIVE};
static cvar_t cl_portpingprobe_probes = {"cl_portpingprobe_probes", "6", CVAR_ARCHIVE};
static cvar_t cl_portpingprobe_delay = {"cl_portpingprobe_delay", "0.20", CVAR_ARCHIVE};
static int net_lagdebug_unmatched_suppressed;
static double net_lagdebug_unmatched_time;
static qboolean net_server_receive_path;

#define MAX_PENDING_DATAGRAMS	64
#define MAX_PENDING_CONTROL_DATAGRAMS	32
#define MAX_PORTPING_PROBES	16

typedef struct
{
	qboolean	valid;
	qsocket_t	*owner;
	unsigned int	order;
	int		landriver;
	sys_socket_t	socket;
	struct qsockaddr addr;
	unsigned int	wireLength;
	double		queuedTime;
	struct
	{
		unsigned int	length;
		unsigned int	sequence;
		byte	data[MAX_DATAGRAM];
	} packet;
} pending_datagram_t;

static pending_datagram_t pendingDatagrams[MAX_PENDING_DATAGRAMS];
static unsigned int pendingDatagramOrder;

typedef struct
{
	qboolean	valid;
	int		landriver;
	sys_socket_t	socket;
	struct qsockaddr addr;
	int		length;
	double		queuedTime;
	byte		data[NET_MAXMESSAGE];
} pending_control_datagram_t;

static pending_control_datagram_t pendingControlDatagrams[MAX_PENDING_CONTROL_DATAGRAMS];

extern qboolean m_return_onerror;
extern char m_return_reason[32];

static int Datagram_ProcessPacket (qsocket_t *sock, struct qsockaddr *readaddr, unsigned int wireLength);

static char *StrAddr (struct qsockaddr *addr)
{
	static char buf[34];
	byte *p = (byte *)addr;
	int n;

	for (n = 0; n < 16; n++)
		sprintf (buf + n * 2, "%02x", *p++);
	return buf;
}

static void Datagram_QueuePacket (qsocket_t *sock, struct qsockaddr *addr, unsigned int wireLength)
{
	int i, slot;
	unsigned int oldestOrder;

	slot = -1;
	oldestOrder = 0;
	for (i = 0; i < MAX_PENDING_DATAGRAMS; i++)
	{
		if (!pendingDatagrams[i].valid)
		{
			slot = i;
			break;
		}
		if (slot < 0 || pendingDatagrams[i].order < oldestOrder)
		{
			slot = i;
			oldestOrder = pendingDatagrams[i].order;
		}
	}

	if (pendingDatagrams[slot].valid && net_lagdebug.value)
	{
		Con_Printf("net_lagdebug: pending datagram queue full, dropping oldest packet for %s\n",
			pendingDatagrams[slot].owner ? pendingDatagrams[slot].owner->address : "<unknown>");
	}

	pendingDatagrams[slot].valid = true;
	pendingDatagrams[slot].owner = sock;
	pendingDatagrams[slot].order = ++pendingDatagramOrder;
	if (!pendingDatagramOrder)
		pendingDatagramOrder = pendingDatagrams[slot].order = 1;
	pendingDatagrams[slot].landriver = sock->landriver;
	pendingDatagrams[slot].socket = sock->socket;
	pendingDatagrams[slot].addr = *addr;
	pendingDatagrams[slot].wireLength = wireLength;
	pendingDatagrams[slot].queuedTime = net_time;
	Q_memcpy(&pendingDatagrams[slot].packet, &packetBuffer, sizeof(packetBuffer));
}

static qboolean Datagram_QueueControlPacket (sys_socket_t socket, struct qsockaddr *addr, byte *data, int length)
{
	int i, slot;

	if (length <= 0 || length > NET_MAXMESSAGE)
		return false;

	slot = -1;
	for (i = 0; i < MAX_PENDING_CONTROL_DATAGRAMS; i++)
	{
		if (!pendingControlDatagrams[i].valid)
		{
			slot = i;
			break;
		}
	}

	if (slot < 0)
	{
		if (net_lagdebug.value)
			Con_Printf("net_lagdebug: pending control datagram queue full, leaving later packets queued in OS\n");
		return false;
	}

	pendingControlDatagrams[slot].valid = true;
	pendingControlDatagrams[slot].landriver = net_landriverlevel;
	pendingControlDatagrams[slot].socket = socket;
	pendingControlDatagrams[slot].addr = *addr;
	pendingControlDatagrams[slot].length = length;
	pendingControlDatagrams[slot].queuedTime = net_time;
	Q_memcpy(pendingControlDatagrams[slot].data, data, length);
	return true;
}

static qboolean Datagram_DequeueControlPacket (sys_socket_t *socket, struct qsockaddr *addr, int *length)
{
	int i;

	for (i = 0; i < MAX_PENDING_CONTROL_DATAGRAMS; i++)
	{
		if (!pendingControlDatagrams[i].valid)
			continue;
		if (pendingControlDatagrams[i].landriver != net_landriverlevel)
			continue;

		*socket = pendingControlDatagrams[i].socket;
		*addr = pendingControlDatagrams[i].addr;
		*length = pendingControlDatagrams[i].length;
		Q_memcpy(net_message.data, pendingControlDatagrams[i].data, pendingControlDatagrams[i].length);
		pendingControlDatagrams[i].valid = false;
		return true;
	}

	return false;
}

static void Datagram_DropQueuedPackets (qsocket_t *sock)
{
	int i;

	if (!sock->isvirtual)
		return;

	for (i = 0; i < MAX_PENDING_DATAGRAMS; i++)
	{
		if (!pendingDatagrams[i].valid)
			continue;
		if (pendingDatagrams[i].owner != sock)
			continue;
		pendingDatagrams[i].valid = false;
	}
}

static int Datagram_ProcessServerPacket (qsocket_t *sock,
	struct qsockaddr *readaddr, unsigned int wireLength)
{
	int ret;
	qboolean saved_server_path;

	saved_server_path = net_server_receive_path;
	net_server_receive_path = true;
	ret = Datagram_ProcessPacket (sock, readaddr, wireLength);
	net_server_receive_path = saved_server_path;
	return ret;
}

static double Datagram_SocketLastInboundTime (const qsocket_t *sock)
{
	if (sock->lastDatagramTime > 0)
		return sock->lastDatagramTime;
	if (sock->lastMessageTime > 0)
		return sock->lastMessageTime;
	return sock->connecttime;
}

static qboolean Datagram_SocketIsStaleSameIpCandidate (const qsocket_t *sock)
{
	double timeout;
	double lastTime;

	timeout = net_sameip_stale_timeout.value;
	if (timeout <= 0)
		return false;

	lastTime = Datagram_SocketLastInboundTime(sock);
	if (lastTime <= 0)
		return false;

	return net_time - lastTime > timeout;
}

static qboolean Datagram_CloseClientSocket (qsocket_t *sock, const char *reason,
	struct qsockaddr *incoming)
{
	client_t *saved_host_client;
	client_t *client;
	int i;

	saved_host_client = host_client;
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active || client->netconnection != sock)
			continue;

		if (net_lagdebug.value)
		{
			Con_Printf("net_lagdebug: dropping stale same-IP client for reconnect (%s)\n",
				reason);
			Con_Printf("  client:   %s\n", client->name[0] ? client->name : "<unnamed>");
			Con_Printf("  existing: %s age=%.3f\n", StrAddr (&sock->addr),
				net_time - Datagram_SocketLastInboundTime(sock));
			Con_Printf("  incoming: %s\n", StrAddr (incoming));
		}

		host_client = client;
		SV_DropClient(false);
		host_client = saved_host_client;
		return true;
	}

	if (net_lagdebug.value)
	{
		Con_Printf("net_lagdebug: closing stale same-IP qsocket for reconnect (%s)\n",
			reason);
		Con_Printf("  existing: %s age=%.3f\n", StrAddr (&sock->addr),
			net_time - Datagram_SocketLastInboundTime(sock));
		Con_Printf("  incoming: %s\n", StrAddr (incoming));
	}

	NET_Close(sock);
	host_client = saved_host_client;
	return true;
}

static qboolean Datagram_PacketPlausibleForSocket (qsocket_t *sock, unsigned int wireLength)
{
	unsigned int length;
	unsigned int flags;
	unsigned int sequence;

	if (wireLength < NET_HEADERSIZE)
		return false;

	length = BigLong(packetBuffer.length);
	flags = length & (~NETFLAG_LENGTH_MASK);
	length &= NETFLAG_LENGTH_MASK;

	if ((flags & NETFLAG_CTL) || length < NET_HEADERSIZE || length > wireLength)
		return false;

	sequence = BigLong(packetBuffer.sequence);
	if (flags & NETFLAG_UNRELIABLE)
		return sequence >= sock->unreliableReceiveSequence;
	if (flags & NETFLAG_ACK)
		return sequence == sock->ackSequence &&
			sequence == sock->sendSequence - 1;
	if (flags & NETFLAG_DATA)
		return sequence == sock->receiveSequence ||
			sequence + 1 == sock->receiveSequence;

	return false;
}

static qsocket_t *Datagram_FindVirtualSocketForPacket (sys_socket_t socket, struct qsockaddr *addr, unsigned int wireLength)
{
	qsocket_t *s;
	qsocket_t *sameIp = NULL;
	net_landriver_t *ld;
	int sameIpCount = 0;
	int cmp;

	ld = &net_landrivers[net_landriverlevel];
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->disconnected || !s->isvirtual)
			continue;
		if (s->driver != net_driverlevel || s->landriver != net_landriverlevel || s->socket != socket)
			continue;

		cmp = ld->AddrCompare(addr, &s->addr);
		if (cmp == 0)
			return s;
		if (cmp > 0 && Datagram_PacketPlausibleForSocket(s, wireLength))
		{
			sameIp = s;
			sameIpCount++;
		}
	}

	// Let the normal per-client receive path validate and commit the NAT remap,
	// but only when same-IP routing is unambiguous and the packet sequence is
	// plausible for exactly one socket.
	if (sameIpCount == 1)
		return sameIp;

	return NULL;
}

static qboolean Datagram_QueueAcceptedPacket (sys_socket_t socket, struct qsockaddr *addr, byte *data, unsigned int wireLength)
{
	qsocket_t *sock;
	unsigned int copyLength;

	copyLength = q_min(wireLength, (unsigned int)sizeof(packetBuffer));
	memset(&packetBuffer, 0, sizeof(packetBuffer));
	Q_memcpy(&packetBuffer, data, copyLength);

	sock = Datagram_FindVirtualSocketForPacket(socket, addr, copyLength);
	if (!sock)
		return false;

	Datagram_QueuePacket(sock, addr, copyLength);
	return true;
}

static qboolean Datagram_QueueIfForAnotherSocket (qsocket_t *sock, struct qsockaddr *addr, unsigned int wireLength)
{
	qsocket_t *s;
	qsocket_t *sameIp = NULL;
	net_landriver_t *ld;
	int sameIpCount = 0;
	int cmp;

	if (!sock->isvirtual)
		return false;

	ld = &net_landrivers[sock->landriver];
	cmp = ld->AddrCompare(addr, &sock->addr);
	if (cmp == 0)
		return false;

	for (s = net_activeSockets; s; s = s->next)
	{
		if (s == sock || s->disconnected || !s->isvirtual)
			continue;
		if (s->driver != sock->driver || s->landriver != sock->landriver || s->socket != sock->socket)
			continue;
		if (ld->AddrCompare(addr, &s->addr) == 0)
		{
			Datagram_QueuePacket(s, addr, wireLength);
			return true;
		}
	}

	// Same IP/different port is handled as a NAT remap for the current qsocket.
	if (cmp > 0 && Datagram_PacketPlausibleForSocket(sock, wireLength))
		return false;

	for (s = net_activeSockets; s; s = s->next)
	{
		if (s == sock || s->disconnected || !s->isvirtual)
			continue;
		if (s->driver != sock->driver || s->landriver != sock->landriver || s->socket != sock->socket)
			continue;
		if (ld->AddrCompare(addr, &s->addr) > 0 &&
			Datagram_PacketPlausibleForSocket(s, wireLength))
		{
			sameIp = s;
			sameIpCount++;
		}
	}
	if (sameIpCount == 1)
	{
		Datagram_QueuePacket(sameIp, addr, wireLength);
		return true;
	}

	return false;
}

static void Datagram_PruneStaleSameIpSockets (sys_socket_t socket)
{
	qsocket_t *sock;
	qsocket_t *next;
	qsocket_t *other;
	net_landriver_t *ld;

	if (!sv.active || net_sameip_stale_timeout.value <= 0)
		return;

	ld = &net_landrivers[net_landriverlevel];
	for (sock = net_activeSockets; sock; sock = next)
	{
		next = sock->next;
		if (sock->disconnected || !sock->isvirtual)
			continue;
		if (sock->driver != net_driverlevel || sock->landriver != net_landriverlevel ||
			sock->socket != socket)
			continue;
		if (!Datagram_SocketIsStaleSameIpCandidate(sock))
			continue;

		for (other = net_activeSockets; other; other = other->next)
		{
			if (other == sock || other->disconnected || !other->isvirtual)
				continue;
			if (other->driver != sock->driver || other->landriver != sock->landriver ||
				other->socket != sock->socket)
				continue;
			if (ld->AddrCompare(&sock->addr, &other->addr) > 0 ||
				ld->AddrCompare(&other->addr, &sock->addr) > 0)
			{
				Datagram_CloseClientSocket(sock, "stale same-IP duplicate",
					&other->addr);
				break;
			}
		}
	}
}

static qboolean Datagram_DequeuePacket (qsocket_t *sock, unsigned int *wireLength, struct qsockaddr *addr)
{
	int i;
	int slot;

	if (!sock->isvirtual)
		return false;

	slot = -1;
	for (i = 0; i < MAX_PENDING_DATAGRAMS; i++)
	{
		if (!pendingDatagrams[i].valid)
			continue;
		if (pendingDatagrams[i].owner != sock)
			continue;
		if (slot < 0 || pendingDatagrams[i].order < pendingDatagrams[slot].order)
			slot = i;
	}

	if (slot < 0)
		return false;

	*wireLength = pendingDatagrams[slot].wireLength;
	*addr = pendingDatagrams[slot].addr;
	Q_memcpy(&packetBuffer, &pendingDatagrams[slot].packet, sizeof(packetBuffer));
	if (net_lagdebug.value && pendingDatagrams[slot].queuedTime > 0 &&
		net_time - pendingDatagrams[slot].queuedTime > net_lagdebug_frame_threshold.value)
	{
		Con_Printf("net_lagdebug: queued datagram delay %.3f sec for %s len=%u\n",
			net_time - pendingDatagrams[slot].queuedTime,
			sock->address, pendingDatagrams[slot].wireLength);
	}
	pendingDatagrams[slot].valid = false;
	return true;
}


#ifdef BAN_TEST

static struct in_addr	banAddr;
static struct in_addr	banMask;

static void NET_Ban_f (void)
{
	char	addrStr [32];
	char	maskStr [32];
	void	(*print_fn)(const char *fmt, ...) FUNCP_PRINTF(1,2);

	if (cmd_source == src_command)
	{
		if (!sv.active)
		{
			Cmd_ForwardToServer ();
			return;
		}
		print_fn = Con_Printf;
	}
	else
	{
		if (pr_global_struct->deathmatch)
			return;
		print_fn = SV_ClientPrintf;
	}

	switch (Cmd_Argc ())
	{
	case 1:
		if (banAddr.s_addr != INADDR_ANY)
		{
			Q_strcpy(addrStr, inet_ntoa(banAddr));
			Q_strcpy(maskStr, inet_ntoa(banMask));
			print_fn("Banning %s [%s]\n", addrStr, maskStr);
		}
		else
			print_fn("Banning not active\n");
		break;

	case 2:
		if (q_strcasecmp(Cmd_Argv(1), "off") == 0)
			banAddr.s_addr = INADDR_ANY;
		else
			banAddr.s_addr = inet_addr(Cmd_Argv(1));
		banMask.s_addr = INADDR_NONE;
		break;

	case 3:
		banAddr.s_addr = inet_addr(Cmd_Argv(1));
		banMask.s_addr = inet_addr(Cmd_Argv(2));
		break;

	default:
		print_fn("BAN ip_address [mask]\n");
		break;
	}
}
#endif	// BAN_TEST


int Datagram_SendMessage (qsocket_t *sock, sizebuf_t *data)
{
	unsigned int	packetLen;
	unsigned int	dataLen;
	unsigned int	eom;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error("Datagram_SendMessage: zero length message");

	if (data->cursize > NET_MAXMESSAGE)
		Sys_Error("Datagram_SendMessage: message too big: %u", data->cursize);

	if (sock->canSend == false)
		Sys_Error("SendMessage: called with canSend == false");
#endif

	Q_memcpy(sock->sendMessage, data->data, data->cursize);
	sock->sendMessageLength = data->cursize;
	sock->max_datagram = sock->pending_max_datagram;

	// Chunk reliable messages at the qsocket MSS so each fragment fits in a
	// single IP packet on the wire. Keep the active chunk size fixed until the
	// current reliable message is fully ACKed; changing it mid-resend would
	// desynchronize the receiver's reliable stream.
	if (data->cursize <= sock->max_datagram)
	{
		dataLen = data->cursize;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence++);
	Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->canSend = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}


static int SendMessageNext (qsocket_t *sock)
{
	unsigned int	packetLen;
	unsigned int	dataLen;
	unsigned int	eom;

	if (sock->sendMessageLength <= sock->max_datagram)
	{
		dataLen = sock->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence++);
	Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsSent++;
	return 1;
}


static int ReSendMessage (qsocket_t *sock)
{
	unsigned int	packetLen;
	unsigned int	dataLen;
	unsigned int	eom;

	if (sock->sendMessageLength <= sock->max_datagram)
	{
		dataLen = sock->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = sock->max_datagram;
		eom = 0;
	}
	packetLen = NET_HEADERSIZE + dataLen;

	packetBuffer.length = BigLong(packetLen | (NETFLAG_DATA | eom));
	packetBuffer.sequence = BigLong(sock->sendSequence - 1);
	Q_memcpy (packetBuffer.data, sock->sendMessage, dataLen);

	sock->sendNext = false;

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	sock->lastSendTime = net_time;
	packetsReSent++;
	return 1;
}


qboolean Datagram_CanSendMessage (qsocket_t *sock)
{
	if (sock->sendNext)
		SendMessageNext (sock);

	return sock->canSend;
}


qboolean Datagram_CanSendUnreliableMessage (qsocket_t *sock)
{
	return true;
}


int Datagram_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data)
{
	int	packetLen;

#ifdef DEBUG
	if (data->cursize == 0)
		Sys_Error("Datagram_SendUnreliableMessage: zero length message");

	if (data->cursize > MAX_DATAGRAM)
		Sys_Error("Datagram_SendUnreliableMessage: message too big: %u", data->cursize);
#endif

	packetLen = NET_HEADERSIZE + data->cursize;

	packetBuffer.length = BigLong(packetLen | NETFLAG_UNRELIABLE);
	packetBuffer.sequence = BigLong(sock->unreliableSendSequence++);
	Q_memcpy (packetBuffer.data, data->data, data->cursize);

	if (sfunc.Write (sock->socket, (byte *)&packetBuffer, packetLen, &sock->addr) == -1)
		return -1;

	packetsSent++;
	return 1;
}


static int Datagram_ProcessPacket (qsocket_t *sock, struct qsockaddr *readaddr, unsigned int wireLength)
{
	unsigned int	length;
	unsigned int	flags;
	unsigned int	sequence;
	unsigned int	count;
	qboolean		pendingRemap;

	{
		pendingRemap = false;
		{
			int cmp = sfunc.AddrCompare(readaddr, &sock->addr);
			if (cmp < 0)
			{
				// Different IP entirely - genuine forged/misdirected packet.
				if (net_lagdebug.value)
				{
					Con_Printf("net_lagdebug: ignoring packet (different address)\n");
					Con_Printf("  expected: %s\n", StrAddr (&sock->addr));
					Con_Printf("  received: %s\n", StrAddr (readaddr));
				}
				return 0;
			}
			if (cmp > 0)
			{
				// Same IP, different port. Two possibilities:
				//   (a) genuine NAT remap to a new external port
				//   (b) delayed straggler from a recently abandoned port -
				//       a packet that was already in flight when the NAT
				//       last remapped, arriving after we've moved on.
				// Distinguish via the prevAddr memory: if this readaddr
				// matches the port we just moved away from, AND we moved
				// away from it recently, treat as a straggler and process
				// the packet without redirecting sock->addr backward.
				if (sock->prevAddrTime != 0.0 &&
					(net_time - sock->prevAddrTime) < 3.0 &&
					sfunc.AddrCompare(readaddr, &sock->prevAddr) == 0)
				{
					if (net_lagdebug.value)
					{
						Con_Printf("net_lagdebug: straggler from recently abandoned port - processing without remap\n");
						Con_Printf("  abandoned: %s (%.3fs ago)\n",
							StrAddr (&sock->prevAddr),
							net_time - sock->prevAddrTime);
					}
					// leave pendingRemap = false; the packet still gets
					// processed in its respective path, sock->addr unchanged.
				}
				else
				{
					pendingRemap = true;
				}
			}
		}

		if (wireLength < NET_HEADERSIZE)
		{
			shortPacketCount++;
			return 0;
		}

		length = BigLong(packetBuffer.length);
		flags = length & (~NETFLAG_LENGTH_MASK);
		length &= NETFLAG_LENGTH_MASK;

		if (flags & NETFLAG_CTL)
			return 0;
		if (length < NET_HEADERSIZE || length > wireLength)
		{
			shortPacketCount++;
			if (net_lagdebug.value)
				Con_Printf("net_lagdebug: invalid datagram length header len=%u wire=%u flags=0x%x\n",
					length, wireLength, flags);
			return 0;
		}

		sequence = BigLong(packetBuffer.sequence);
		packetsReceived++;
		if (net_lagdebug.value && sock->lastDatagramTime > 0 &&
			net_time - sock->lastDatagramTime > net_lagdebug_threshold.value)
		{
			Con_Printf("net_lagdebug: %.3f sec datagram gap from %s (seq=%u flags=0x%x len=%u)\n",
				net_time - sock->lastDatagramTime, sock->address, sequence, flags, length);
		}
		sock->lastDatagramTime = net_time;

		if (flags & NETFLAG_UNRELIABLE)
		{
			if (sequence < sock->unreliableReceiveSequence)
			{
				Con_DPrintf("Got a stale datagram\n");
				return 0;
			}
			count = 0;

			// Valid unreliable - safe to commit the NAT remap now.
			if (pendingRemap)
			{
				if (net_lagdebug.value)
				{
					Con_Printf("net_lagdebug: NAT remap on valid unreliable (same IP, different port)\n");
					Con_Printf("  was:  %s\n", StrAddr (&sock->addr));
					Con_Printf("  now:  %s\n", StrAddr (readaddr));
				}
				sock->prevAddr = sock->addr;
				sock->prevAddrTime = net_time;
				sock->addr = *readaddr;
			}

			if (sequence != sock->unreliableReceiveSequence)
			{
				count = sequence - sock->unreliableReceiveSequence;
				droppedDatagrams += count;
				Con_DPrintf("Dropped %u datagram(s) for %s\n", count, sock->address);
				if (net_lagdebug.value)
					Con_Printf("net_lagdebug: dropped %u unreliable datagram(s) from %s seq=%u expected=%u\n",
						count, sock->address, sequence, sock->unreliableReceiveSequence);
			}
			NET_QSocketRecordUnreliableReceive (sock, count);
			sock->unreliableReceiveSequence = sequence + 1;

			length -= NET_HEADERSIZE;
			if (length > (unsigned int)net_message.maxsize)
			{
				Con_Printf("Over-sized unreliable\n");
				return -1;
			}

			SZ_Clear (&net_message);
			SZ_Write (&net_message, packetBuffer.data, length);

			return 2;
		}

		if (flags & NETFLAG_ACK)
		{
			if (sequence != (sock->sendSequence - 1))
			{
				Con_DPrintf("Stale ACK received\n");
				return 0;
			}
			if (sequence == sock->ackSequence)
			{
				sock->ackSequence++;
				if (sock->ackSequence != sock->sendSequence)
					Con_DPrintf("ack sequencing error\n");
			}
			else
			{
				Con_DPrintf("Duplicate ACK received\n");
				return 0;
			}
			// Valid in-order ACK - safe to commit the NAT remap now.
			if (pendingRemap)
			{
				if (net_lagdebug.value)
				{
					Con_Printf("net_lagdebug: NAT remap on valid ACK (same IP, different port)\n");
					Con_Printf("  was:  %s\n", StrAddr (&sock->addr));
					Con_Printf("  now:  %s\n", StrAddr (readaddr));
				}
				sock->prevAddr = sock->addr;
				sock->prevAddrTime = net_time;
				sock->addr = *readaddr;
			}
			sock->sendMessageLength -= sock->max_datagram;
			if (sock->sendMessageLength > 0)
			{
				memmove (sock->sendMessage, sock->sendMessage + sock->max_datagram, sock->sendMessageLength);
				sock->sendNext = true;
			}
			else
			{
				sock->sendMessageLength = 0;
				sock->canSend = true;
			}
			return 0;
		}

		if (flags & NETFLAG_DATA)
		{
			// The ACK goes to readaddr regardless, so a duplicate from a
			// remapped port still gets ACK'd at its real source.
			packetBuffer.length = BigLong(NET_HEADERSIZE | NETFLAG_ACK);
			packetBuffer.sequence = BigLong(sequence);
			sfunc.Write (sock->socket, (byte *)&packetBuffer, NET_HEADERSIZE, readaddr);

			// Commit the NAT remap here - before the duplicate-sequence check
			// rather than after it. The ACK we just sent proves the new port
			// is live; if this is a retransmission from a NAT-remapped client
			// (the original ACK was sent to the old, now-dead mapping and
			// lost), the duplicate-skip would otherwise stop the client
			// retransmitting while our future sends still target the dead
			// port. Stragglers from a recently abandoned port were already
			// filtered out above by the prevAddr/prevAddrTime check, so
			// pendingRemap here is a genuine new-port signal.
			if (pendingRemap)
			{
				if (net_lagdebug.value)
				{
					Con_Printf("net_lagdebug: NAT remap on DATA (same IP, different port)\n");
					Con_Printf("  was:  %s\n", StrAddr (&sock->addr));
					Con_Printf("  now:  %s\n", StrAddr (readaddr));
				}
				sock->prevAddr = sock->addr;
				sock->prevAddrTime = net_time;
				sock->addr = *readaddr;
			}

			if (sequence != sock->receiveSequence)
			{
				receivedDuplicateCount++;
				return 0;
			}
			sock->receiveSequence++;

			length -= NET_HEADERSIZE;

			if (flags & NETFLAG_EOM)
			{
				if (sock->receiveMessageLength + length > (unsigned int)net_message.maxsize)
				{
					Con_Printf("Over-sized reliable\n");
					sock->receiveMessageLength = 0;
					return -1;
				}
				SZ_Clear(&net_message);
				SZ_Write(&net_message, sock->receiveMessage, sock->receiveMessageLength);
				SZ_Write(&net_message, packetBuffer.data, length);
				sock->receiveMessageLength = 0;

				return 1;
			}

			if (sock->receiveMessageLength + length > sizeof(sock->receiveMessage))
			{
				Con_Printf("Over-sized reliable\n");
				sock->receiveMessageLength = 0;
				return -1;
			}
			Q_memcpy(sock->receiveMessage + sock->receiveMessageLength, packetBuffer.data, length);
			sock->receiveMessageLength += length;
			return 0;
		}
	}

	return 0;
}


int	Datagram_GetMessage (qsocket_t *sock)
{
	int				ret = 0;
	int				readLength;
	struct qsockaddr readaddr;
	unsigned int	queuedLength;

	if (!sock->canSend)
		if ((net_time - sock->lastSendTime) > 1.0)
			ReSendMessage (sock);

	while (Datagram_DequeuePacket(sock, &queuedLength, &readaddr))
	{
		ret = Datagram_ProcessPacket(sock, &readaddr, queuedLength);
		if (ret)
			goto done;
	}

	while (1)
	{
		readLength = sfunc.Read(sock->socket, (byte *)&packetBuffer,
							NET_DATAGRAMSIZE, &readaddr);

	//	if ((rand() & 255) > 220)
	//		continue;

		if (readLength == 0)
			break;

		if (readLength == -1)
		{
			Con_Printf("Read error\n");
			return -1;
		}

		if (Datagram_QueueIfForAnotherSocket(sock, &readaddr, (unsigned int)readLength))
			continue;

		ret = Datagram_ProcessPacket(sock, &readaddr, (unsigned int)readLength);
		if (ret)
			break;
	}

done:
	if (sock->sendNext)
		SendMessageNext (sock);

	return ret;
}


static void Datagram_ServerMessageResult (qsocket_t *sock, int ret, void (*callback)(qsocket_t *sock))
{
	if (ret < 0)
	{
		NET_Close(sock);
		return;
	}

	if (ret == 0 || sock->disconnected)
		return;

	sock->lastMessageTime = net_time;
	if (ret == 1)
		messagesReceived++;
	else if (ret == 2)
		unreliableMessagesReceived++;
	callback(sock);
}


static qboolean Datagram_ProcessNonConnectControlPacket (sys_socket_t acceptsock, struct qsockaddr *clientaddr, byte *data, int len)
{
	struct qsockaddr newaddr;
	int control;
	int command;

	if (len < (int)sizeof(int) || len > net_message.maxsize)
		return true;

	SZ_Clear(&net_message);
	Q_memcpy(net_message.data, data, len);
	net_message.cursize = len;

	MSG_BeginReading ();
	control = BigLong(*((int *)net_message.data));
	MSG_ReadLong();
	if (control == -1)
		return true;
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		return false;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		return true;

	command = MSG_ReadByte();
	if (command == CCREQ_CONNECT)
		return false;

	if (command == CCREQ_SERVER_INFO)
	{
		if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
			return true;

		SZ_Clear(&net_message);
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
		dfunc.GetSocketAddr(acceptsock, &newaddr);
		MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
		MSG_WriteString(&net_message, hostname.string);
		MSG_WriteString(&net_message, sv.name);
		MSG_WriteByte(&net_message, net_activeconnections);
		MSG_WriteByte(&net_message, svs.maxclients);
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);
		return true;
	}

	if (command == CCREQ_PLAYER_INFO)
	{
		int playerNumber;
		int activeNumber;
		int clientNumber;
		client_t *client;

		playerNumber = MSG_ReadByte();
		activeNumber = -1;

		for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
		{
			if (client->active)
			{
				activeNumber++;
				if (activeNumber == playerNumber)
					break;
			}
		}

		if (clientNumber == svs.maxclients)
			return true;

		SZ_Clear(&net_message);
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
		MSG_WriteByte(&net_message, playerNumber);
		MSG_WriteString(&net_message, client->name);
		MSG_WriteLong(&net_message, client->colors);
		MSG_WriteLong(&net_message, (int)client->edict->v.frags);
		MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
		MSG_WriteString(&net_message, client->netconnection->address);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);
		return true;
	}

	if (command == CCREQ_RULE_INFO)
	{
		const char *prevCvarName;
		cvar_t *var;

		prevCvarName = MSG_ReadString();
		var = Cvar_FindVarAfter (prevCvarName, CVAR_SERVERINFO);

		SZ_Clear(&net_message);
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_RULE_INFO);
		if (var)
		{
			MSG_WriteString(&net_message, var->name);
			MSG_WriteString(&net_message, var->string);
		}
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, clientaddr);
		SZ_Clear(&net_message);
		return true;
	}

	return true;
}


void Datagram_GetAnyMessages (void (*callback)(qsocket_t *sock))
{
	qsocket_t		*sock;
	qsocket_t		*next;
	sys_socket_t		acceptsock;
	sys_socket_t		prunesock;
	struct qsockaddr	readaddr;
	unsigned int		queuedLength;
	int			readLength;
	int			ret;
	unsigned int		header;

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		prunesock = INVALID_SOCKET;

		for (sock = net_activeSockets; sock; sock = next)
		{
			next = sock->next;
			if (sock->disconnected || !sock->isvirtual)
				continue;
			if (sock->driver != net_driverlevel || sock->landriver != net_landriverlevel)
				continue;

			while (Datagram_DequeuePacket(sock, &queuedLength, &readaddr))
			{
				ret = Datagram_ProcessServerPacket(sock, &readaddr, queuedLength);
				Datagram_ServerMessageResult(sock, ret, callback);
				if (sock->disconnected)
					break;
			}
		}

		while ((acceptsock = dfunc.CheckNewConnections()) != INVALID_SOCKET)
		{
			prunesock = acceptsock;
			readLength = dfunc.Read(acceptsock, (byte *)&packetBuffer, NET_DATAGRAMSIZE, &readaddr);
			if (readLength == 0)
				break;
			if (readLength == -1)
			{
				Con_Printf("Read error\n");
				break;
			}
			if (readLength < (int)sizeof(int))
				continue;

			header = BigLong(packetBuffer.length);
			if (header & NETFLAG_CTL)
			{
				if (Datagram_ProcessNonConnectControlPacket(acceptsock, &readaddr, (byte *)&packetBuffer, readLength))
					continue;
				if (!Datagram_QueueControlPacket(acceptsock, &readaddr, (byte *)&packetBuffer, readLength))
					break;
				continue;
			}

			sock = Datagram_FindVirtualSocketForPacket(acceptsock, &readaddr, (unsigned int)readLength);
			if (!sock)
			{
				if (net_lagdebug.value)
				{
					net_lagdebug_unmatched_suppressed++;
					if (net_time - net_lagdebug_unmatched_time >= 1.0)
					{
						Con_Printf("net_lagdebug: ignored unmatched shared-socket datagrams suppressed=%d latest=%s\n",
							net_lagdebug_unmatched_suppressed, dfunc.AddrToString(&readaddr));
						net_lagdebug_unmatched_suppressed = 0;
						net_lagdebug_unmatched_time = net_time;
					}
				}
				continue;
			}

			ret = Datagram_ProcessServerPacket(sock, &readaddr, (unsigned int)readLength);
			Datagram_ServerMessageResult(sock, ret, callback);
		}

		if (prunesock != INVALID_SOCKET)
			Datagram_PruneStaleSameIpSockets(prunesock);

		for (sock = net_activeSockets; sock; sock = next)
		{
			next = sock->next;
			if (sock->disconnected || !sock->isvirtual)
				continue;
			if (sock->driver != net_driverlevel || sock->landriver != net_landriverlevel)
				continue;
			if (sock->sendNext)
				SendMessageNext(sock);
			if (!sock->canSend && (net_time - sock->lastSendTime) > 1.0)
				ReSendMessage(sock);
		}
	}
}


static void PrintStats(qsocket_t *s)
{
	Con_Printf("canSend = %4u   \n", s->canSend);
	Con_Printf("sendSeq = %4u   ", s->sendSequence);
	Con_Printf("recvSeq = %4u   \n", s->receiveSequence);
	Con_Printf("\n");
}

static void NET_Stats_f (void)
{
	qsocket_t	*s;

	if (Cmd_Argc () == 1)
	{
		Con_Printf("unreliable messages sent   = %i\n", unreliableMessagesSent);
		Con_Printf("unreliable messages recv   = %i\n", unreliableMessagesReceived);
		Con_Printf("reliable messages sent     = %i\n", messagesSent);
		Con_Printf("reliable messages received = %i\n", messagesReceived);
		Con_Printf("packetsSent                = %i\n", packetsSent);
		Con_Printf("packetsReSent              = %i\n", packetsReSent);
		Con_Printf("packetsReceived            = %i\n", packetsReceived);
		Con_Printf("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
		Con_Printf("shortPacketCount           = %i\n", shortPacketCount);
		Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
	}
	else if (Q_strcmp(Cmd_Argv(1), "*") == 0)
	{
		for (s = net_activeSockets; s; s = s->next)
			PrintStats(s);
		for (s = net_freeSockets; s; s = s->next)
			PrintStats(s);
	}
	else
	{
		for (s = net_activeSockets; s; s = s->next)
		{
			if (q_strcasecmp(Cmd_Argv(1), s->address) == 0)
				break;
		}

		if (s == NULL)
		{
			for (s = net_freeSockets; s; s = s->next)
			{
				if (q_strcasecmp(Cmd_Argv(1), s->address) == 0)
					break;
			}
		}

		if (s == NULL)
			return;

		PrintStats(s);
	}
}


// recognize ip:port (based on ProQuake)
static const char *Strip_Port (const char *host)
{
	static char	noport[MAX_QPATH];
			/* array size as in Host_Connect_f() */
	char		*p;
	int		port;

	if (!host || !*host)
		return host;
	q_strlcpy (noport, host, sizeof(noport));
	if ((p = Q_strrchr(noport, ':')) == NULL)
		return host;
	*p++ = '\0';
	port = Q_atoi (p);
	if (port > 0 && port < 65536 && port != net_hostport)
	{
		net_hostport = port;
		Con_Printf("Port set to %d\n", net_hostport);
	}
	return noport;
}


static qboolean testInProgress = false;
static int		testPollCount;
static int		testDriver;
static sys_socket_t	testSocket;

static void Test_Poll (void *);
static PollProcedure	testPollProcedure = {NULL, 0.0, Test_Poll};

static void Test_Poll (void *unused)
{
	struct qsockaddr clientaddr;
	int		control;
	int		len;
	char	name[32];
	char	address[64];
	int		colors;
	int		frags;
	int		connectTime;

	net_landriverlevel = testDriver;

	while (1)
	{
		len = dfunc.Read (testSocket, net_message.data, net_message.maxsize, &clientaddr);
		if (len < (int) sizeof(int))
			break;

		net_message.cursize = len;

		MSG_BeginReading ();
		control = BigLong(*((int *)net_message.data));
		MSG_ReadLong();
		if (control == -1)
			break;
		if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
			break;
		if ((control & NETFLAG_LENGTH_MASK) != len)
			break;

		if (MSG_ReadByte() != CCREP_PLAYER_INFO)
			Sys_Error("Unexpected response to Player Info request\n");

		MSG_ReadByte(); /* playerNumber */
		Q_strcpy(name, MSG_ReadString());
		colors = MSG_ReadLong();
		frags = MSG_ReadLong();
		connectTime = MSG_ReadLong();
		Q_strcpy(address, MSG_ReadString());

		Con_Printf("%s\n  frags:%3i  colors:%d %d  time:%d\n  %s\n", name, frags, colors >> 4, colors & 0x0f, connectTime / 60, address);
	}

	testPollCount--;
	if (testPollCount)
	{
		SchedulePollProcedure(&testPollProcedure, 0.1);
	}
	else
	{
		dfunc.Close_Socket(testSocket);
		testInProgress = false;
	}
}

static void Test_f (void)
{
	const char	*host;
	int		n;
	int		maxusers = MAX_SCOREBOARD;
	struct qsockaddr sendaddr;

	if (testInProgress)
		return;

	host = Strip_Port (Cmd_Argv(1));

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				maxusers = hostcache[n].maxusers;
				Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
				break;
			}
		}

		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
			break;
	}

	if (net_landriverlevel == net_numlandrivers)
	{
		Con_Printf("Could not resolve %s\n", host);
		return;
	}

JustDoIt:
	testSocket = dfunc.Open_Socket(0);
	if (testSocket == INVALID_SOCKET)
		return;

	testInProgress = true;
	testPollCount = 20;
	testDriver = net_landriverlevel;

	for (n = 0; n < maxusers; n++)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_PLAYER_INFO);
		MSG_WriteByte(&net_message, n);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (testSocket, net_message.data, net_message.cursize, &sendaddr);
	}
	SZ_Clear(&net_message);
	SchedulePollProcedure(&testPollProcedure, 0.1);
}


static qboolean test2InProgress = false;
static int		test2Driver;
static sys_socket_t	test2Socket;

static void Test2_Poll (void *);
static PollProcedure	test2PollProcedure = {NULL, 0.0, Test2_Poll};

static void Test2_Poll (void *unused)
{
	struct qsockaddr clientaddr;
	int		control;
	int		len;
	char	name[256];
	char	value[256];

	net_landriverlevel = test2Driver;
	name[0] = 0;

	len = dfunc.Read (test2Socket, net_message.data, net_message.maxsize, &clientaddr);
	if (len < (int) sizeof(int))
		goto Reschedule;

	net_message.cursize = len;

	MSG_BeginReading ();
	control = BigLong(*((int *)net_message.data));
	MSG_ReadLong();
	if (control == -1)
		goto Error;
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		goto Error;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		goto Error;

	if (MSG_ReadByte() != CCREP_RULE_INFO)
		goto Error;

	Q_strcpy(name, MSG_ReadString());
	if (name[0] == 0)
		goto Done;
	Q_strcpy(value, MSG_ReadString());

	Con_Printf("%-16.16s  %-16.16s\n", name, value);

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
	MSG_WriteString(&net_message, name);
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (test2Socket, net_message.data, net_message.cursize, &clientaddr);
	SZ_Clear(&net_message);

Reschedule:
	SchedulePollProcedure(&test2PollProcedure, 0.05);
	return;

Error:
	Con_Printf("Unexpected response to Rule Info request\n");
Done:
	dfunc.Close_Socket(test2Socket);
	test2InProgress = false;
	return;
}

static void Test2_f (void)
{
	const char	*host;
	int		n;
	struct qsockaddr sendaddr;

	if (test2InProgress)
		return;

	host = Strip_Port (Cmd_Argv(1));

	if (host && hostCacheCount)
	{
		for (n = 0; n < hostCacheCount; n++)
		{
			if (q_strcasecmp (host, hostcache[n].name) == 0)
			{
				if (hostcache[n].driver != myDriverLevel)
					continue;
				net_landriverlevel = hostcache[n].ldriver;
				Q_memcpy(&sendaddr, &hostcache[n].addr, sizeof(struct qsockaddr));
				break;
			}
		}

		if (n < hostCacheCount)
			goto JustDoIt;
	}

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (!net_landrivers[net_landriverlevel].initialized)
			continue;

		// see if we can resolve the host name
		if (dfunc.GetAddrFromName(host, &sendaddr) != -1)
			break;
	}

	if (net_landriverlevel == net_numlandrivers)
	{
		Con_Printf("Could not resolve %s\n", host);
		return;
	}

JustDoIt:
	test2Socket = dfunc.Open_Socket(0);
	if (test2Socket == INVALID_SOCKET)
		return;

	test2InProgress = true;
	test2Driver = net_landriverlevel;

	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_RULE_INFO);
	MSG_WriteString(&net_message, "");
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (test2Socket, net_message.data, net_message.cursize, &sendaddr);
	SZ_Clear(&net_message);
	SchedulePollProcedure(&test2PollProcedure, 0.05);
}


int Datagram_Init (void)
{
	int	i, num_inited;
	sys_socket_t	csock;

#ifdef BAN_TEST
	banAddr.s_addr = INADDR_ANY;
	banMask.s_addr = INADDR_NONE;
#endif
	myDriverLevel = net_driverlevel;

	Cmd_AddCommand ("net_stats", NET_Stats_f);
	Cvar_RegisterVariable (&net_lagdebug);
	Cvar_RegisterVariable (&net_lagdebug_threshold);
	Cvar_RegisterVariable (&net_lagdebug_frame_threshold);
	Cvar_RegisterVariable (&net_sameip_stale_timeout);
	Cvar_RegisterVariable (&cl_netport);
	Cvar_RegisterVariable (&cl_portpingprobe_enable);
	Cvar_RegisterVariable (&cl_portpingprobe_probes);
	Cvar_RegisterVariable (&cl_portpingprobe_delay);

	if (safemode || COM_CheckParm("-nolan"))
		return -1;

	num_inited = 0;
	for (i = 0; i < net_numlandrivers; i++)
	{
		csock = net_landrivers[i].Init ();
		if (csock == INVALID_SOCKET)
			continue;
		net_landrivers[i].initialized = true;
		net_landrivers[i].controlSock = csock;
		num_inited++;
	}

	if (num_inited == 0)
		return -1;

#ifdef BAN_TEST
	Cmd_AddCommand ("ban", NET_Ban_f);
#endif
	Cmd_AddCommand ("test", Test_f);
	Cmd_AddCommand ("test2", Test2_f);

	return 0;
}


void Datagram_Shutdown (void)
{
	int i;

//
// shutdown the lan drivers
//
	for (i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
		{
			net_landrivers[i].Shutdown ();
			net_landrivers[i].initialized = false;
		}
	}
}


void Datagram_Close (qsocket_t *sock)
{
	Datagram_DropQueuedPackets(sock);
	if (!sock->isvirtual)
		sfunc.Close_Socket(sock->socket);
}


void Datagram_Listen (qboolean state)
{
	int i;

	for (i = 0; i < net_numlandrivers; i++)
	{
		if (net_landrivers[i].initialized)
			net_landrivers[i].Listen (state);
	}
}


static qsocket_t *_Datagram_CheckNewConnections (void)
{
	struct qsockaddr clientaddr;
	struct qsockaddr newaddr;
	sys_socket_t		newsock;
	sys_socket_t		acceptsock;
	qsocket_t	*sock;
	qsocket_t	*s;
	int			len;
	int			command;
	int			control;
	int			ret;

	SZ_Clear(&net_message);

	if (!Datagram_DequeueControlPacket(&acceptsock, &clientaddr, &len))
	{
		acceptsock = dfunc.CheckNewConnections();
		if (acceptsock == INVALID_SOCKET)
			return NULL;

		len = dfunc.Read (acceptsock, net_message.data, net_message.maxsize, &clientaddr);
		if (len < (int) sizeof(int))
			return NULL;
	}
	net_message.cursize = len;

	MSG_BeginReading ();
	control = BigLong(*((int *)net_message.data));
	MSG_ReadLong();
	if (control == -1)
		return NULL;
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
	{
		Datagram_QueueAcceptedPacket(acceptsock, &clientaddr, net_message.data, (unsigned int)len);
		return NULL;
	}
	if ((control & NETFLAG_LENGTH_MASK) != len)
		return NULL;

	command = MSG_ReadByte();
	if (command == CCREQ_SERVER_INFO)
	{
		if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
			return NULL;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
		dfunc.GetSocketAddr(acceptsock, &newaddr);
		MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
		MSG_WriteString(&net_message, hostname.string);
		MSG_WriteString(&net_message, sv.name);
		MSG_WriteByte(&net_message, net_activeconnections);
		MSG_WriteByte(&net_message, svs.maxclients);
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return NULL;
	}

	if (command == CCREQ_PLAYER_INFO)
	{
		int			playerNumber;
		int			activeNumber;
		int			clientNumber;
		client_t	*client;

		playerNumber = MSG_ReadByte();
		activeNumber = -1;

		for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
		{
			if (client->active)
			{
				activeNumber++;
				if (activeNumber == playerNumber)
					break;
			}
		}

		if (clientNumber == svs.maxclients)
			return NULL;

		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
		MSG_WriteByte(&net_message, playerNumber);
		MSG_WriteString(&net_message, client->name);
		MSG_WriteLong(&net_message, client->colors);
		MSG_WriteLong(&net_message, (int)client->edict->v.frags);
		MSG_WriteLong(&net_message, (int)(net_time - client->netconnection->connecttime));
		MSG_WriteString(&net_message, client->netconnection->address);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);

		return NULL;
	}

	if (command == CCREQ_RULE_INFO)
	{
		const char	*prevCvarName;
		cvar_t			*var;

		// find the search start location
		prevCvarName = MSG_ReadString();
		var = Cvar_FindVarAfter (prevCvarName, CVAR_SERVERINFO);

		// send the response
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_RULE_INFO);
		if (var)
		{
			MSG_WriteString(&net_message, var->name);
			MSG_WriteString(&net_message, var->string);
		}
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);

		return NULL;
	}

	if (command != CCREQ_CONNECT)
		return NULL;

	if (Q_strcmp(MSG_ReadString(), "QUAKE") != 0)
		return NULL;

	if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_REJECT);
		MSG_WriteString(&net_message, "Incompatible version.\n");
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return NULL;
	}

#ifdef BAN_TEST
	// check for a ban
	if (clientaddr.qsa_family == AF_INET)
	{
		in_addr_t	testAddr;
		testAddr = ((struct sockaddr_in *)&clientaddr)->sin_addr.s_addr;
		if ((testAddr & banMask.s_addr) == banAddr.s_addr)
		{
			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREP_REJECT);
			MSG_WriteString(&net_message, "You have been banned.\n");
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
			SZ_Clear(&net_message);
			return NULL;
		}
	}
#endif

	// see if this guy is already connected
RestartDuplicateScan:
	for (s = net_activeSockets; s; s = s->next)
	{
		if (s->driver != net_driverlevel)
			continue;
		ret = dfunc.AddrCompare(&clientaddr, &s->addr);
		if (ret >= 0)
		{
			if (ret > 0)
			{
				if (s->isvirtual && Datagram_SocketIsStaleSameIpCandidate(s))
				{
					Datagram_CloseClientSocket(s, "same-IP different-port connect",
						&clientaddr);
					goto RestartDuplicateScan;
				}
				if (net_lagdebug.value)
				{
					Con_Printf("net_lagdebug: allowing same-IP client on a different port\n");
					Con_Printf("  existing: %s\n", StrAddr (&s->addr));
					Con_Printf("  incoming: %s\n", StrAddr (&clientaddr));
				}
				continue;
			}
			// is this a duplicate connection reqeust?
			if (net_time - s->connecttime < 2.0)
			{
				// yes, so send a duplicate reply
				SZ_Clear(&net_message);
				// save space for the header, filled in later
				MSG_WriteLong(&net_message, 0);
				MSG_WriteByte(&net_message, CCREP_ACCEPT);
				dfunc.GetSocketAddr(s->socket, &newaddr);
				MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
				if (s->isvirtual)
				{
					MSG_WriteByte(&net_message, MOD_PROQUAKE);
					MSG_WriteByte(&net_message, 30);
					MSG_WriteByte(&net_message, PQF_IGNOREPORT);
				}
				*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
				dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
				SZ_Clear(&net_message);
				return NULL;
			}
			// it's somebody coming back in from a crash/disconnect
			// so close the old qsocket and let their retry get them back in
			NET_Close(s);
			return NULL;
		}
	}

	// allocate a QSocket
	sock = NET_NewQSocket ();
	if (sock == NULL)	// no room; try to let him know
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREP_REJECT);
		MSG_WriteString(&net_message, "Server is full.\n");
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
		SZ_Clear(&net_message);
		return NULL;
	}

	newsock = acceptsock;

	// everything is allocated, just fill in the details
	sock->socket = newsock;
	sock->isvirtual = true;
	sock->landriver = net_landriverlevel;
	sock->addr = clientaddr;
	Q_strcpy(sock->address, dfunc.AddrToString(&clientaddr));

	// send him back the info about the server connection he has been allocated
	SZ_Clear(&net_message);
	// save space for the header, filled in later
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREP_ACCEPT);
	dfunc.GetSocketAddr(newsock, &newaddr);
	MSG_WriteLong(&net_message, dfunc.GetSocketPort(&newaddr));
	MSG_WriteByte(&net_message, MOD_PROQUAKE);
	MSG_WriteByte(&net_message, 30);
	MSG_WriteByte(&net_message, PQF_IGNOREPORT);
//	MSG_WriteString(&net_message, dfunc.AddrToString(&newaddr));
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write (acceptsock, net_message.data, net_message.cursize, &clientaddr);
	SZ_Clear(&net_message);

	return sock;
}

qsocket_t *Datagram_CheckNewConnections (void)
{
	qsocket_t *ret = NULL;

	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (net_landrivers[net_landriverlevel].initialized)
		{
			if ((ret = _Datagram_CheckNewConnections ()) != NULL)
				break;
		}
	}
	return ret;
}


static void _Datagram_SearchForHosts (qboolean xmit)
{
	int		ret;
	int		n;
	int		i;
	struct qsockaddr readaddr;
	struct qsockaddr myaddr;
	int		control;

	dfunc.GetSocketAddr (dfunc.controlSock, &myaddr);
	if (xmit)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Broadcast(dfunc.controlSock, net_message.data, net_message.cursize);
		SZ_Clear(&net_message);
	}

	while ((ret = dfunc.Read (dfunc.controlSock, net_message.data, net_message.maxsize, &readaddr)) > 0)
	{
		if (ret < (int) sizeof(int))
			continue;
		net_message.cursize = ret;

		// don't answer our own query
		if (dfunc.AddrCompare(&readaddr, &myaddr) >= 0)
			continue;

		// is the cache full?
		if (hostCacheCount == HOSTCACHESIZE)
			continue;

		MSG_BeginReading ();
		control = BigLong(*((int *)net_message.data));
		MSG_ReadLong();
		if (control == -1)
			continue;
		if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
			continue;
		if ((control & NETFLAG_LENGTH_MASK) != ret)
			continue;

		if (MSG_ReadByte() != CCREP_SERVER_INFO)
			continue;

		dfunc.GetAddrFromName(MSG_ReadString(), &readaddr);
		// search the cache for this server
		for (n = 0; n < hostCacheCount; n++)
		{
			if (dfunc.AddrCompare(&readaddr, &hostcache[n].addr) == 0)
				break;
		}

		// is it already there?
		if (n < hostCacheCount)
			continue;

		// add it
		hostCacheCount++;
		Q_strcpy(hostcache[n].name, MSG_ReadString());
		Q_strcpy(hostcache[n].map, MSG_ReadString());
		hostcache[n].users = MSG_ReadByte();
		hostcache[n].maxusers = MSG_ReadByte();
		if (MSG_ReadByte() != NET_PROTOCOL_VERSION)
		{
			Q_strcpy(hostcache[n].cname, hostcache[n].name);
			hostcache[n].cname[14] = 0;
			Q_strcpy(hostcache[n].name, "*");
			Q_strcat(hostcache[n].name, hostcache[n].cname);
		}
		Q_memcpy(&hostcache[n].addr, &readaddr, sizeof(struct qsockaddr));
		hostcache[n].driver = net_driverlevel;
		hostcache[n].ldriver = net_landriverlevel;
		Q_strcpy(hostcache[n].cname, dfunc.AddrToString(&readaddr));

		// check for a name conflict
		for (i = 0; i < hostCacheCount; i++)
		{
			if (i == n)
				continue;
			if (q_strcasecmp (hostcache[n].name, hostcache[i].name) == 0)
			{
				i = Q_strlen(hostcache[n].name);
				if (i < 15 && hostcache[n].name[i-1] > '8')
				{
					hostcache[n].name[i] = '0';
					hostcache[n].name[i+1] = 0;
				}
				else
					hostcache[n].name[i-1]++;

				i = -1;
			}
		}
	}
}

void Datagram_SearchForHosts (qboolean xmit)
{
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (hostCacheCount == HOSTCACHESIZE)
			break;
		if (net_landrivers[net_landriverlevel].initialized)
			_Datagram_SearchForHosts (xmit);
	}
}


static int Datagram_ClientPort (void)
{
	int i, port;

	i = COM_CheckParm("-clientport");
	if (i && i < com_argc - 1)
	{
		port = Q_atoi(com_argv[i + 1]);
		if (port > 0 && port < 65536)
			return port;
	}

	port = (int)cl_netport.value;
	if (port > 0 && port < 65536)
		return port;

	return 0;
}


static sys_socket_t Datagram_OpenClientSocket (void)
{
	int port;
	sys_socket_t sock;

	port = Datagram_ClientPort();
	if (port > 0)
	{
		sock = dfunc.Open_Socket(port);
		if (sock != INVALID_SOCKET)
			return sock;
		Con_Printf("Unable to bind client UDP port %d, using an ephemeral port\n", port);
	}

	return dfunc.Open_Socket(0);
}


static void Datagram_SendServerInfoProbe (sys_socket_t sock, struct qsockaddr *sendaddr)
{
	SZ_Clear(&net_message);
	MSG_WriteLong(&net_message, 0);
	MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
	MSG_WriteString(&net_message, "QUAKE");
	MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
	*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
	dfunc.Write(sock, net_message.data, net_message.cursize, sendaddr);
	SZ_Clear(&net_message);
}


static qboolean Datagram_IsServerInfoReply (int len, struct qsockaddr *readaddr, struct qsockaddr *sendaddr)
{
	int control;

	if (len < (int)sizeof(int))
		return false;
	if (dfunc.AddrCompare(readaddr, sendaddr) != 0)
		return false;

	net_message.cursize = len;
	MSG_BeginReading();
	control = BigLong(*((int *)net_message.data));
	MSG_ReadLong();
	if (control == -1)
		return false;
	if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
		return false;
	if ((control & NETFLAG_LENGTH_MASK) != len)
		return false;
	if (MSG_ReadByte() != CCREP_SERVER_INFO)
		return false;

	return true;
}


static sys_socket_t Datagram_OpenProbedClientSocket (struct qsockaddr *sendaddr)
{
	sys_socket_t sockets[MAX_PORTPING_PROBES];
	struct qsockaddr readaddr;
	int probes, i, chosen, len;
	double deadline;

	if (Datagram_ClientPort() > 0 || cl_portpingprobe_enable.value == 0)
		return Datagram_OpenClientSocket();

	probes = (int)cl_portpingprobe_probes.value;
	probes = CLAMP(1, probes, MAX_PORTPING_PROBES);

	for (i = 0; i < probes; i++)
	{
		sockets[i] = dfunc.Open_Socket(0);
		if (sockets[i] != INVALID_SOCKET)
			Datagram_SendServerInfoProbe(sockets[i], sendaddr);
	}

	chosen = -1;
	deadline = Sys_DoubleTime() + CLAMP(0.02, cl_portpingprobe_delay.value, 1.0);
	while (Sys_DoubleTime() < deadline && chosen < 0)
	{
		for (i = 0; i < probes; i++)
		{
			if (sockets[i] == INVALID_SOCKET)
				continue;
			len = dfunc.Read(sockets[i], net_message.data, net_message.maxsize, &readaddr);
			if (len > 0 && Datagram_IsServerInfoReply(len, &readaddr, sendaddr))
			{
				chosen = i;
				break;
			}
		}
		if (chosen < 0)
			Sys_Sleep(1);
	}

	if (chosen < 0)
	{
		for (i = 0; i < probes; i++)
		{
			if (sockets[i] != INVALID_SOCKET)
			{
				chosen = i;
				break;
			}
		}
	}

	for (i = 0; i < probes; i++)
	{
		if (sockets[i] != INVALID_SOCKET && i != chosen)
			dfunc.Close_Socket(sockets[i]);
	}

	if (chosen < 0)
		return INVALID_SOCKET;

	return sockets[chosen];
}


static qsocket_t *_Datagram_Connect (const char *host)
{
	struct qsockaddr sendaddr;
	struct qsockaddr readaddr;
	qsocket_t	*sock;
	sys_socket_t		newsock;
	int			ret;
	int			reps;
	double		start_time;
	int			control;
	int			reply;
	const char		*reason;

	// see if we can resolve the host name
	if (dfunc.GetAddrFromName(host, &sendaddr) == -1)
	{
		Con_Printf("Could not resolve %s\n", host);
		return NULL;
	}

	newsock = Datagram_OpenProbedClientSocket (&sendaddr);
	if (newsock == INVALID_SOCKET)
		return NULL;

	sock = NET_NewQSocket ();
	if (sock == NULL)
		goto ErrorReturn2;
	sock->socket = newsock;
	sock->landriver = net_landriverlevel;

	// connect to the host
	if (dfunc.Connect (newsock, &sendaddr) == -1)
		goto ErrorReturn;

	// send the connection request
	Con_Printf("trying...\n");
	SCR_UpdateScreen ();
	start_time = net_time;
	reply = 0;

	for (reps = 0; reps < 3; reps++)
	{
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_CONNECT);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		dfunc.Write (newsock, net_message.data, net_message.cursize, &sendaddr);
		SZ_Clear(&net_message);
		do
		{
			ret = dfunc.Read (newsock, net_message.data, net_message.maxsize, &readaddr);
			// if we got something, validate it
			if (ret > 0)
			{
				// is it from the right place?
				if (sfunc.AddrCompare(&readaddr, &sendaddr) != 0)
				{
					Con_Printf("wrong reply address\n");
					Con_Printf("Expected: %s | %s\n", dfunc.AddrToString (&sendaddr), StrAddr(&sendaddr));
					Con_Printf("Received: %s | %s\n", dfunc.AddrToString (&readaddr), StrAddr(&readaddr));
					SCR_UpdateScreen ();
					ret = 0;
					continue;
				}

				if (ret < (int) sizeof(int))
				{
					ret = 0;
					continue;
				}

				net_message.cursize = ret;
				MSG_BeginReading ();

				control = BigLong(*((int *)net_message.data));
				MSG_ReadLong();
				if (control == -1)
				{
					ret = 0;
					continue;
				}
				if ((control & (~NETFLAG_LENGTH_MASK)) != (int)NETFLAG_CTL)
				{
					ret = 0;
					continue;
				}
				if ((control & NETFLAG_LENGTH_MASK) != ret)
				{
					ret = 0;
					continue;
				}

				reply = MSG_ReadByte();
				if (reply == CCREP_SERVER_INFO)
				{
					ret = 0;
					continue;
				}
				if (reply != CCREP_ACCEPT && reply != CCREP_REJECT)
				{
					ret = 0;
					continue;
				}
			}
		}
		while (ret == 0 && (SetNetTime() - start_time) < 2.5);

		if (ret)
			break;

		Con_Printf("still trying...\n");
		SCR_UpdateScreen ();
		start_time = SetNetTime();
	}

	if (ret == 0)
	{
		reason = "No Response";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	if (ret == -1)
	{
		reason = "Network Error";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	if (reply == CCREP_REJECT)
	{
		reason = MSG_ReadString();
		Con_Printf("%s\n", reason);
		q_strlcpy(m_return_reason, reason, sizeof(m_return_reason));
		goto ErrorReturn;
	}

	if (reply == CCREP_ACCEPT)
	{
		int acceptPort;
		qboolean ignorePort = false;

		Q_memcpy(&sock->addr, &sendaddr, sizeof(struct qsockaddr));
		acceptPort = MSG_ReadLong();
		if (msg_readcount + 3 <= net_message.cursize)
		{
			int mod = MSG_ReadByte();
			MSG_ReadByte();	// ProQuake version, ignored.
			if (mod == MOD_PROQUAKE && (MSG_ReadByte() & PQF_IGNOREPORT))
				ignorePort = true;
		}
		if (!ignorePort)
			dfunc.SetSocketPort (&sock->addr, acceptPort);
	}
	else
	{
		reason = "Bad Response";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	dfunc.GetNameFromAddr (&sendaddr, sock->address);

	Con_Printf ("Connection accepted\n");
	sock->lastMessageTime = SetNetTime();

	// switch the connection to the specified address
	if (dfunc.Connect (newsock, &sock->addr) == -1)
	{
		reason = "Connect to Game failed";
		Con_Printf("%s\n", reason);
		Q_strcpy(m_return_reason, reason);
		goto ErrorReturn;
	}

	m_return_onerror = false;
	return sock;

ErrorReturn:
	NET_FreeQSocket(sock);
ErrorReturn2:
	dfunc.Close_Socket(newsock);
	if (m_return_onerror)
	{
		IN_Deactivate(modestate == MS_WINDOWED);
		key_dest = key_menu;
		m_state = m_return_state;
		m_return_onerror = false;
	}
	return NULL;
}

qsocket_t *Datagram_Connect (const char *host)
{
	qsocket_t *ret = NULL;

	host = Strip_Port (host);
	for (net_landriverlevel = 0; net_landriverlevel < net_numlandrivers; net_landriverlevel++)
	{
		if (net_landrivers[net_landriverlevel].initialized)
		{
			if ((ret = _Datagram_Connect (host)) != NULL)
				break;
		}
	}
	return ret;
}
