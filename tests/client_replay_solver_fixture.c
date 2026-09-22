/* Real PMove coverage for replay's zero-duration partial command.
 * Transport/input owners are stubbed; PM_PlayerMove and hull queries are production. */
#include "../Quake/quakedef.h"

#define PMCL_SetMoveVars FixtureUnused_PMCL_SetMoveVars
#define PMCL_AddEntities FixtureUnused_PMCL_AddEntities
#include "../Quake/pmove.c"
#undef PMCL_SetMoveVars
#undef PMCL_AddEntities

qboolean PMCL_SetMoveVars (void);
void PMCL_AddEntities (vec3_t boxminmax[2]);

#include "../Quake/cl_main.c"

#include <assert.h>
#include <stdarg.h>

cvar_t pr_checkextension;
extern cvar_t sv_fte_recursivehullckeck;
double realtime;

static int preview_calls;
static qmodel_t water_model;
static mplane_t water_plane;
static mclipnode_t water_node;
static entity_t entities[2];

void Con_DPrintf (const char *fmt, ...) {}

void Sys_Error (const char *fmt, ...)
{
	va_list args;
	va_start (args, fmt);
	vfprintf (stderr, fmt, args);
	va_end (args);
	abort ();
}

void CL_PreviewMove (usercmd_t *cmd)
{
	preview_calls++;
	memset (cmd, 0, sizeof(*cmd));
}

qboolean PMCL_SetMoveVars (void)
{
	memset (&movevars, 0, sizeof(movevars));
	movevars.gravity = 800;
	movevars.entgravity = 1;
	movevars.maxspeed = 320;
	movevars.maxairspeed = 30;
	movevars.accelerate = 10;
	movevars.airaccelerate = 10;
	movevars.wateraccelerate = 10;
	movevars.friction = 4;
	movevars.waterfriction = 4;
	movevars.flyfriction = 4;
	movevars.stopspeed = 100;
	movevars.edgefriction = 2;
	movevars.stepheight = 18;
	movevars.jumpspeed = 270;
	movevars.watersinkspeed = 60;
	movevars.bunnyfriction = true;
	movevars.slidefix = true;
	movevars.flags = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND;
	return true;
}

void PMCL_AddEntities (vec3_t boxminmax[2])
{
	(void)boxminmax;
	memset (pmove.physents, 0, sizeof(pmove.physents));
	pmove.physents[0].model = &water_model;
	pmove.numphysent = 1;
}

static void setup_water_world (void)
{
	memset (&water_model, 0, sizeof(water_model));
	memset (&water_plane, 0, sizeof(water_plane));
	memset (&water_node, 0, sizeof(water_node));

	water_model.type = mod_brush;
	VectorSet (water_model.mins, -4096, -4096, -4096);
	VectorSet (water_model.maxs, 4096, 4096, 4096);
	water_plane.normal[2] = 1;
	water_plane.type = 2;
	water_node.planenum = 0;
	water_node.children[0] = CONTENTS_WATER;
	water_node.children[1] = CONTENTS_WATER;
	for (int i = 0; i < 2; i++)
	{
		water_model.hulls[i].planes = &water_plane;
		water_model.hulls[i].clipnodes = &water_node;
		water_model.hulls[i].firstclipnode = 0;
		water_model.hulls[i].lastclipnode = 0;
	}
}

int main (void)
{
	vec3_t origin;

	memset (&cl, 0, sizeof(cl));
	memset (&cls, 0, sizeof(cls));
	memset (&pmove, 0, sizeof(pmove));
	memset (entities, 0, sizeof(entities));
	setup_water_world ();
	preview_calls = 0;

	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.worldmodel = &water_model;
	cl.entities = entities;
	cl.num_entities = 2;
	cl.viewentity = 1;
	cl.protocol_pext2 = PEXT2_PREDINFO;
	cl.stats[STAT_HEALTH] = 100;
	cl.ackedmovemessages = 3;
	cl.movemessages = 4; /* startseq == movemessages: no journal command to replay */
	cl.time = 5;
	cl.pendingcmd.servertime = 5; /* zero-duration partial */
	cl_nopred.value = 0;

	entities[1].netstate.pmovetype = MOVETYPE_WALK;
	VectorClear (entities[1].msg_origins[0]);

	assert (CL_ReplayPlayerMovement (&entities[1], origin));
	assert (preview_calls == 1);
	assert (pmove.cmd.seconds == 0);
	assert (pmove.cmd.msec == 0);
	assert (pmove.waterlevel == 3);
	assert (cl.inwater);

	puts ("client replay real PMove: empty history zero-duration preview categorizes underwater");
	return 0;
}
