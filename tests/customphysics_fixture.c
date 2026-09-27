/* Run the production customphysics adapter and QuakeC interpreter. The
 * bytecode moves a body and clears its callback; entity allocation and field
 * lookup are fixture boundaries, not a real-map/network proof. */
#include "../Quake/sv_phys.c"
#include "../Quake/pr_exec.c"
#include <assert.h>

server_t sv;
server_static_t svs;
qcvm_t *qcvm;
globalvars_t *pr_global_struct;
cvar_t coop;
static edict_t entities[4];
static client_t clients[3];
static float globals[256];
static dprograms_t program;
static dfunction_t functions[3];
static dstatement_t statements[6];
static qboolean remove_self;
static int observed;

enum { FIELD_CUSTOM = 200, G_CUSTOM = 150, G_ORIGIN, G_BODY = 160,
	G_POINTER = 170, G_ZERO, G_OBSERVE };

eval_t *GetEdictFieldValue (edict_t *ent, int field)
{
	if (field < 0)
		return NULL;
	assert (field == FIELD_CUSTOM);
	return (eval_t *)&ent->v.think;
}

edict_t *EDICT_NUM (int n)
{
	assert (n >= 0 && n < qcvm->num_edicts);
	return EDICT_NUM_NO_CHECK (n);
}

ddef_t *ED_FindField (const char *name) { return NULL; }
dfunction_t *ED_FindFunction (const char *name) { return NULL; }
int ED_FindFieldOffset (const char *name) { return -1; }

void ED_Retain (edict_t *ent)
{
	assert (!ent->free);
	ent->retain_count++;
}

void ED_Release (edict_t *ent)
{
	assert (ent->retain_count);
	ent->retain_count--;
}

void Con_Printf (const char *fmt, ...) { assert (0); }
void Con_Warning (const char *fmt, ...) { assert (0); }
void Host_Error (const char *fmt, ...) { assert (0); }
void ED_Print (edict_t *ent) { assert (0); }
const char *PR_GetString (int index) { return "fixture"; }
const char *PR_GlobalString (int index) { return "fixture"; }
const char *PR_GlobalStringNoContents (int index) { return "fixture"; }

static void observe_callback (void)
{
	edict_t *self = PROG_TO_EDICT (pr_global_struct->self);
	assert (self == &entities[1] && self->retain_count == 1);
	assert (pr_global_struct->time == 7.25f);
	assert (entities[2].v.takedamage == DAMAGE_NO);
	assert (entities[3].v.takedamage == DAMAGE_NO);
	assert (self->v.origin[0] == 37 && self->v.origin[1] == 11 &&
		self->v.origin[2] == 24);
	observed++;
	if (remove_self)
		self->free = true;
}

int main (void)
{
	qcvm = &sv.qcvm;
	qcvm->edicts = entities;
	qcvm->num_edicts = 4;
	qcvm->edict_size = sizeof (edict_t);
	qcvm->time = 7.25;
	qcvm->globals = globals;
	qcvm->progs = &program;
	program.numfunctions = 3;
	program.numglobals = countof (globals);
	qcvm->functions = functions;
	qcvm->statements = statements;
	pr_global_struct = (globalvars_t *)globals;
	qcvm->builtins[1] = observe_callback;
	qcvm->numbuiltins = 2;
	functions[1].first_statement = 0;
	functions[2].first_statement = -1;
	/* self.origin = body; self.think = 0; observe(); return; */
	statements[0] = (dstatement_t){OP_ADDRESS,
		offsetof (globalvars_t, self) / sizeof (float), G_ORIGIN, G_POINTER};
	statements[1] = (dstatement_t){OP_STOREP_V, G_BODY, G_POINTER, 0};
	statements[2] = (dstatement_t){OP_ADDRESS,
		offsetof (globalvars_t, self) / sizeof (float), G_CUSTOM, G_POINTER};
	statements[3] = (dstatement_t){OP_STOREP_FNC, G_ZERO, G_POINTER, 0};
	statements[4] = (dstatement_t){OP_CALL0, G_OBSERVE, 0, 0};
	statements[5] = (dstatement_t){OP_DONE, G_ZERO, 0, 0};
	G_INT (G_ORIGIN) = offsetof (entvars_t, origin) / sizeof (float);
	G_INT (G_CUSTOM) = offsetof (entvars_t, think) / sizeof (float);
	G_INT (G_OBSERVE) = 2;
	VectorSet (G_VECTOR (G_BODY), 37, 11, 24);
	svs.clients = clients;
	svs.maxclients = 3;
	for (int i = 0; i < 3; ++i)
	{
		clients[i].active = true;
		clients[i].edict = &entities[i + 1];
		entities[i + 1].v.takedamage = DAMAGE_AIM;
	}
	coop.value = sv_nofriendlyfire.value = 1;
	pr_global_struct->teamplay = 2;

	qcvm->extfields.customphysics = -1;
	assert (!SV_RunCustomPhysics (&entities[1]));
	qcvm->extfields.customphysics = FIELD_CUSTOM;
	assert (!SV_RunCustomPhysics (&entities[1]));
	entities[1].v.think = 1;
	assert (SV_RunCustomPhysics (&entities[1]));
	assert (observed == 1 && entities[1].v.think == 0);
	assert (!SV_RunCustomPhysics (&entities[1]));
	assert (entities[1].retain_count == 0);
	assert (entities[2].v.takedamage == DAMAGE_AIM &&
		entities[3].v.takedamage == DAMAGE_AIM);
	assert (pr_global_struct->teamplay == 2 && !ff_active);

	remove_self = true;
	entities[1].v.think = 1;
	assert (SV_RunCustomPhysics (&entities[1]));
	assert (observed == 2 && entities[1].free &&
		entities[1].retain_count == 0 && !ff_active);
	assert (entities[2].v.takedamage == DAMAGE_AIM &&
		entities[3].v.takedamage == DAMAGE_AIM);
	puts ("QuakeC customphysics adapter passed");
	return 0;
}
