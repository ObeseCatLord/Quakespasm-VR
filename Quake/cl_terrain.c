/*
 * Client copy of the editable terrain (see sv_terrain.c, NETSYNC). The server sends the region's
 * parameters, the client generates the same cave from the seed, then replays the server's op log
 * as it arrives. Rendering and the client's player prediction use this copy, so a listen server's
 * own client and a remote one see the cave the same way.
 * GPL-2.0-or-later, as the rest of the engine.
 */
#include "quakedef.h"
#include "terrain.h"

static int cl_terrain_ops_applied;

void CL_Terrain_Clear (void)
{
	CL_Workshop_Clear ();
	R_Terrain_FinishMeshing ();
	if (terrain_client)
	{
		Terrain_Free (terrain_client);
		terrain_client = NULL;
		++terrain_client_epoch;
	}
	cl_terrain_ops_applied = 0;
}

/*
//terrain_init <seed> <origin x y z> <size x y z> <ice> <walk_normal>
//terrain_init none
*/
static void CL_Terrain_Init_f (void)
{
	CL_Terrain_Clear ();
	if (Cmd_Argc () != 10)
		return; // "none": this map has no cave
	int32_t origin[3], size[3];
	for (int a = 0; a < 3; ++a)
	{
		origin[a] = atoi (Cmd_Argv (2 + a));
		size[a] = atoi (Cmd_Argv (5 + a));
		if (size[a] < 1 || size[a] > 16 || abs (origin[a]) > 65536)
		{
			Con_Warning ("terrain_init: bad region size from server\n");
			return;
		}
	}
	const uint32_t seed = (uint32_t)strtoul (Cmd_Argv (1), NULL, 10);
	terrain_t	  *t = Terrain_Create (origin, size, seed);
	Terrain_Generate (t);
	if (atoi (Cmd_Argv (8)))
		Terrain_GenerateIce (t);
	t->walk_normal = CLAMP (0.0f, (float)atof (Cmd_Argv (9)), 1.0f);
	terrain_client = t;
	++terrain_client_epoch;
}

static int HexNibble (char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

// What the server itself can produce: a known kind, a known material, the server's largest radius,
// a centre inside the region (with room for that radius).
static qboolean CL_Terrain_OpSane (const terrain_op_t *op)
{
	if (op->kind > TERRAIN_OP_DISK || op->material >= TERRAIN_MAT_COUNT || op->radius > 512 * TERRAIN_SUB / TERRAIN_CELL ||
		op->thickness > 120 * TERRAIN_SUB / TERRAIN_CELL)
		return false;
	for (int a = 0; a < 3; ++a)
	{
		const int32_t extent = terrain_client->size[a] * TERRAIN_CHUNK * TERRAIN_SUB;
		if (op->center[a] < -(int32_t)op->radius || op->center[a] > extent + (int32_t)op->radius)
			return false;
	}
	return true;
}

/*
//terrain_ops <index of the first op> <bytes per op> <ops as hex>
*/
static void CL_Terrain_Ops_f (void)
{
	if (!terrain_client || Cmd_Argc () != 4)
		return;
	const int	first = atoi (Cmd_Argv (1)), bytes = atoi (Cmd_Argv (2));
	const char *hex = Cmd_Argv (3);
	const int	len = (int)strlen (hex);
	if (bytes != TERRAIN_OP_BYTES || len % (bytes * 2))
	{
		Con_Warning ("terrain_ops: unknown op format from server\n");
		return;
	}
	if (first != cl_terrain_ops_applied)
	{
		Con_Warning ("terrain_ops: expected op %d, got %d; the cave may differ from the server's\n", cl_terrain_ops_applied, first);
		if (first < cl_terrain_ops_applied)
			return; // already have these
	}
	R_Terrain_FinishMeshing (); // workers may be reading the field
	for (int at = 0; at < len; at += bytes * 2)
	{
		uint8_t packed[TERRAIN_OP_BYTES];
		for (int b = 0; b < bytes; ++b)
		{
			const int hi = HexNibble (hex[at + b * 2]), lo = HexNibble (hex[at + b * 2 + 1]);
			if (hi < 0 || lo < 0)
				return;
			packed[b] = (uint8_t)(hi << 4 | lo);
		}
		terrain_op_t op;
		Terrain_OpUnpack (packed, &op);
		if (!CL_Terrain_OpSane (&op))
		{
			Con_Warning ("terrain_ops: op %d from the server is out of range\n", first + at / (bytes * 2));
			continue;
		}
		Terrain_ApplyOp (terrain_client, &op);
	}
	cl_terrain_ops_applied = first + len / (bytes * 2);
}

/*
terrain_clhash
Prints the client copy's hash next to the server's, when this process runs both.
*/
static void CL_Terrain_Hash_f (void)
{
	Con_Printf (
		"terrain_clhash: client %016llx ops %d, server %016llx\n", terrain_client ? (unsigned long long)Terrain_Hash (terrain_client) : 0ull,
		cl_terrain_ops_applied, terrain_active ? (unsigned long long)Terrain_Hash (terrain_active) : 0ull);
}

void CL_Terrain_Init (void)
{
	Cmd_AddCommand_ServerCommand ("terrain_init", CL_Terrain_Init_f);
	Cmd_AddCommand_ServerCommand ("terrain_ops", CL_Terrain_Ops_f);
	Cmd_AddCommand ("terrain_clhash", CL_Terrain_Hash_f);
	CL_Workshop_Init ();
}
