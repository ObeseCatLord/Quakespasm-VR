/* Installed, unmodified AD QC: real artifact spawn/think/touch, normal client
 * spawn/begin and PlayerJump/ClientPowerups. Transport is the existing captured
 * native fixture boundary; this does not claim a connected network session.
 * Compile/link against main's coordinated graph; see the plan for run evidence. */
#define MIXED_NATIVE_FIXTURE_ENTRY ImportedBootsMixedMain
#include "mixed_native_fixture.c"

#define BOOTS_BIT 1048576
static const char *boots_config[] = {
	"jumpboots_finished", "jumpboots_time", "jumpboots_airmax",
	"jumpboots_height", "jumpboots_forward"};
static client_t *boots_peers[4];
static edict_t *observed_boots;
static int boots_touch_calls;
static edict_t *unrelated_boots_trigger;
static int unrelated_boots_effect;
static void StagedUnrelatedBootEffect (edict_t *actor);
void __real_PR_ExecuteProgram (func_t function);
void __wrap_PR_ExecuteProgram (func_t function)
{
	qboolean unrelated = unrelated_boots_trigger &&
		function == unrelated_boots_trigger->v.touch &&
		pr_global_struct->self == EDICT_TO_PROG(unrelated_boots_trigger);
	edict_t *actor = unrelated ? PROG_TO_EDICT(pr_global_struct->other) : NULL;
	if (observed_boots && function == observed_boots->v.touch &&
	    pr_global_struct->self == EDICT_TO_PROG(observed_boots))
		++boots_touch_calls;
	__real_PR_ExecuteProgram(function); /* Never replace the installed QC. */
	if (unrelated)
		StagedUnrelatedBootEffect(actor);
}

static eval_t *BootsFloat (edict_t *ent, const char *name)
{
	ddef_t *def = ED_FindField(name);
	assert(def && (def->type & ~DEF_SAVEGLOBAL) == ev_float);
	eval_t *value = GetEdictFieldValue(ent, def->ofs);
	assert(value);
	return value;
}

static qboolean HasBoots (edict_t *ent)
{
	return ((int)BootsFloat(ent, "moditems")->_float & BOOTS_BIT) != 0;
}

static void BootsClock (double now)
{
	qcvm->time = now;
	pr_global_struct->time = now;
	host_frametime = .01;
	pr_global_struct->frametime = .01;
}

static void BootsQC (const char *name, edict_t *ent)
{
	dfunction_t *function = ED_FindFunction(name);
	assert(function && function->first_statement > 0);
	int self = pr_global_struct->self, other = pr_global_struct->other;
	pr_global_struct->self = EDICT_TO_PROG(ent);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	PR_ExecuteProgram(function - qcvm->functions);
	pr_global_struct->self = self;
	pr_global_struct->other = other;
}

static void ClearBoots (edict_t *ent)
{
	/* Native expiry/reset owns bit removal. Parameters belong to this staged
	 * native call, not a generated or substituted QC pickup implementation. */
	float saved[6];
	int argc = qcvm->argc;
	memcpy(saved, &qcvm->globals[OFS_PARM0], sizeof(saved));
	G_INT(OFS_PARM0) = EDICT_TO_PROG(ent);
	G_FLOAT(OFS_PARM1) = 0;
	qcvm->argc = 2;
	BootsQC("ResetPowerJumpBoots", ent);
	qcvm->argc = argc;
	memcpy(&qcvm->globals[OFS_PARM0], saved, sizeof(saved));
	assert(!HasBoots(ent));
	for (int i = 0; i < countof(boots_config); ++i)
		BootsFloat(ent, boots_config[i])->_float = 0;
	BootsFloat(ent, "jumpboots_airlvl")->_float = 0;
	BootsFloat(ent, "jumpboots_onground")->_float = 0;
	BootsFloat(ent, "jumpboots_sound")->_float = 0;
}

static void StagedUnrelatedBootEffect (edict_t *actor)
{
	/* Compose actual installed warning/reset QC inside the native trigger
	 * transaction, without requiring a particular map hazard. SUB_Null itself
	 * still executes. This is a negative boundary case, not a QC pickup. */
	if (unrelated_boots_effect == 1)
		BootsQC("ClientPowerups", actor);
	else if (unrelated_boots_effect == 4)
	{
		/* Native ResetPowerJumpBoots never converts permanent boots to finite.
		 * Stage this otherwise unsupported mutation only to reject its grant. */
		BootsFloat(actor, "jumpboots_finished")->_float = qcvm->time + 1;
		BootsFloat(actor, "jumpboots_time")->_float = 1;
	}
	else if (unrelated_boots_effect)
	{
		float saved[6];
		int argc = qcvm->argc;
		memcpy(saved, &qcvm->globals[OFS_PARM0], sizeof(saved));
		G_INT(OFS_PARM0) = EDICT_TO_PROG(actor);
		G_FLOAT(OFS_PARM1) = unrelated_boots_effect == 2 ? 1 : 0;
		qcvm->argc = 2;
		BootsQC("ResetPowerJumpBoots", actor);
		qcvm->argc = argc;
		memcpy(&qcvm->globals[OFS_PARM0], saved, sizeof(saved));
	}
}

static void BootsCase (qboolean classic, const char *override)
{
	Cvar_SetQuick(&coop, "1");
	Cvar_SetQuick(&deathmatch, "0");
	Cvar_SetQuick(&sv_coop_classic, classic ? "1" : "0");
	Cvar_SetQuick(&sv_coop_shared_pickups, override);
	SV_CoopSharedResetState();
	for (int i = 0; i < countof(boots_peers); ++i)
		if (boots_peers[i])
		{
			edict_t *ent = boots_peers[i]->edict;
			ClearBoots(ent);
			ent->v.health = 100;
			ent->v.deadflag = DEAD_NO;
			ent->v.flags = FL_CLIENT | FL_JUMPRELEASED;
			ent->v.waterlevel = 0;
			VectorClear(ent->v.velocity);
		}
}

static edict_t *SpawnBoots (float duration, float tier, float height, float forward)
{
	edict_t *item = ED_Alloc();
	item->v.classname = PR_SetEngineString("item_artifact_jumpboots");
	item->v.spawnflags = 32; /* Native ITEM_FLOATING, avoiding a staged floor. */
	VectorCopy(boots_peers[0]->edict->v.origin, item->v.origin);
	BootsFloat(item, "cnt")->_float = duration;
	BootsFloat(item, "count")->_float = tier;
	BootsFloat(item, "height")->_float = height;
	BootsFloat(item, "distance")->_float = forward;
	sv.state = ss_loading; /* Native spawn precaches the installed resources. */
	BootsQC("item_artifact_jumpboots", item);
	sv.state = ss_active;
	assert(!item->free && HasBoots(item) && item->v.nextthink > qcvm->time);
	BootsClock(item->v.nextthink);
	/* Prepare the scheduled call boundary; SV_RunThink is private. The actual
	 * installed think performs item setup, model/bounds and world linking. */
	func_t think = item->v.think;
	item->v.nextthink = 0;
	BootsQC(PR_GetString(qcvm->functions[think].s_name), item);
	assert(!item->free && item->v.solid == SOLID_TRIGGER && item->v.touch);
	assert(!strcmp(PR_GetString(qcvm->functions[item->v.touch].s_name), "item_touch"));
	return item;
}

static void TouchBoots (edict_t *item)
{
	edict_t *actor = boots_peers[0]->edict;
	for (int i = 1; i < countof(boots_peers); ++i)
		if (boots_peers[i])
		{
			VectorCopy(item->v.origin, boots_peers[i]->edict->v.origin);
			boots_peers[i]->edict->v.origin[0] += 512 + i * 64;
			SV_LinkEdict(boots_peers[i]->edict, false);
		}
	VectorCopy(item->v.origin, actor->v.origin);
	observed_boots = item;
	boots_touch_calls = 0;
	SV_LinkEdict(actor, true); /* Production accepted-delta owner runs here. */
	assert(boots_touch_calls == 1); /* One actual pickup, never replay per peer. */
	observed_boots = NULL;
}

static void RetireBoots (edict_t *item)
{
	item->v.solid = SOLID_NOT;
	SV_LinkEdict(item, false);
	ED_Free(item);
}

static void AssertSameBoots (edict_t *a, edict_t *b)
{
	assert(HasBoots(a) && HasBoots(b));
	for (int i = 0; i < countof(boots_config); ++i)
		assert(BootsFloat(a, boots_config[i])->_float ==
			BootsFloat(b, boots_config[i])->_float);
}

static void AssertFreshPeer (edict_t *peer)
{
	AssertSameBoots(boots_peers[0]->edict, peer);
	assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 0);
}

static void FunctionalAirBootJump (edict_t *player, float height, float forward)
{
	float charges = BootsFloat(player, "jumpboots_airlvl")->_float;
	player->v.flags = FL_CLIENT | FL_JUMPRELEASED;
	player->v.button2 = 1;
	player->v.waterlevel = 0;
	VectorClear(player->v.velocity);
	VectorClear(player->v.v_angle);
	BootsQC("PlayerJump", player);
	assert(fabsf(player->v.velocity[2] - height) < .001f);
	assert(fabsf(player->v.velocity[0] - forward) < .001f);
	assert(BootsFloat(player, "jumpboots_airlvl")->_float == charges - 1);
}

static void FunctionalBootJump (edict_t *player, float height, float forward)
{
	/* Execute both native jump branches. Geometry/input state is staged; the
	 * installed movement QC supplies velocity and consumes the player's charge. */
	player->v.flags = FL_CLIENT | FL_ONGROUND | FL_JUMPRELEASED;
	player->v.button2 = 1;
	player->v.waterlevel = 0;
	VectorClear(player->v.velocity);
	VectorClear(player->v.v_angle);
	BootsQC("PlayerJump", player);
	float maximum = BootsFloat(player, "jumpboots_airmax")->_float;
	assert(!((int)player->v.flags & FL_ONGROUND));
	assert(BootsFloat(player, "jumpboots_airlvl")->_float == maximum);
	player->v.flags = (int)player->v.flags | FL_JUMPRELEASED;
	player->v.button2 = 1;
	VectorClear(player->v.velocity);
	BootsQC("PlayerJump", player);
	assert(fabsf(player->v.velocity[2] - height) < .001f);
	assert(fabsf(player->v.velocity[0] - forward) < .001f);
	assert(BootsFloat(player, "jumpboots_airlvl")->_float == maximum - 1);
}

static void ProfileCases (void)
{
	static const struct { qboolean classic; const char *override; qboolean share; } cases[] = {
		{false, "-1", true}, {true, "-1", false},
		{false, "0", false}, {true, "1", true},
	};
	for (int i = 0; i < countof(cases); ++i)
	{
		BootsCase(cases[i].classic, cases[i].override);
		edict_t *item = SpawnBoots(30, 2, 410, 190);
		TouchBoots(item);
		assert(HasBoots(boots_peers[0]->edict));
		assert(HasBoots(boots_peers[1]->edict) == cases[i].share);
		if (cases[i].share)
		{
			AssertFreshPeer(boots_peers[1]->edict);
			FunctionalBootJump(boots_peers[1]->edict, 410, 190);
			assert(BootsFloat(boots_peers[0]->edict, "jumpboots_airlvl")->_float == 0);
		}
		RetireBoots(item);
	}
}

static void TierAndSentinelCases (void)
{
	edict_t *actor = boots_peers[0]->edict, *peer = boots_peers[1]->edict;
	BootsCase(false, "-1");
	BootsFloat(peer, "jumpboots_airlvl")->_float = 7; /* stale fresh-player state */
	BootsFloat(peer, "jumpboots_onground")->_float = 1;
	BootsFloat(peer, "jumpboots_sound")->_float = 123;
	edict_t *item = SpawnBoots(-1, 2, 360, 200);
	TouchBoots(item);
	AssertFreshPeer(peer);
	assert(BootsFloat(peer, "jumpboots_finished")->_float == -1);
	assert(BootsFloat(peer, "jumpboots_onground")->_float == 1);
	assert(BootsFloat(peer, "jumpboots_sound")->_float == 123);
	RetireBoots(item);
	FunctionalBootJump(actor, 360, 200);
	FunctionalBootJump(peer, 360, 200);
	assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 1); /* Native partial use. */
	BootsFloat(peer, "jumpboots_onground")->_float = 0;
	BootsFloat(peer, "jumpboots_sound")->_float = 456;
	item = SpawnBoots(-1, 3, 450, 320); /* Same ownership bit, accepted tier change. */
	TouchBoots(item);
	AssertSameBoots(actor, peer);
	assert(BootsFloat(peer, "jumpboots_airmax")->_float == 3);
	assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 1);
	assert(BootsFloat(actor, "jumpboots_airlvl")->_float == 0);
	assert(BootsFloat(peer, "jumpboots_onground")->_float == 0);
	assert(BootsFloat(peer, "jumpboots_sound")->_float == 456);
	RetireBoots(item);
	FunctionalAirBootJump(peer, 450, 320);
	assert(BootsFloat(actor, "jumpboots_airlvl")->_float == 0);
	item = SpawnBoots(-1, -1, 520, 270);
	TouchBoots(item);
	AssertSameBoots(actor, peer);
	assert(BootsFloat(peer, "jumpboots_finished")->_float == -1);
	assert(BootsFloat(peer, "jumpboots_airmax")->_float == -1);
	RetireBoots(item);
	FunctionalBootJump(peer, 520, 270);
	for (int i = 0; i < 8; ++i)
	{
		peer->v.flags = (int)peer->v.flags | FL_JUMPRELEASED;
		peer->v.button2 = 1;
		peer->v.velocity[2] = -100;
		BootsQC("PlayerJump", peer);
		assert(peer->v.velocity[2] == 520);
	}
	item = SpawnBoots(30, 4, 480, 100);
	TouchBoots(item);
	RetireBoots(item);
	FunctionalBootJump(peer, 480, 100); /* Native finite refill and partial use. */
	assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 3);
	assert(BootsFloat(actor, "jumpboots_airlvl")->_float == 0);
	/* A subsequently accepted finite bundle must remain finite and coherent. */
	item = SpawnBoots(15, 1, 380, 0);
	TouchBoots(item);
	AssertSameBoots(actor, peer);
	assert(BootsFloat(peer, "jumpboots_finished")->_float > qcvm->time);
	assert(BootsFloat(peer, "jumpboots_airmax")->_float == 1);
	assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 3);
	assert(BootsFloat(actor, "jumpboots_airlvl")->_float == 0);
	RetireBoots(item);
	FunctionalAirBootJump(peer, 380, 0);
	assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 2);
	assert(BootsFloat(actor, "jumpboots_airlvl")->_float == 0);
}

static void RejectionAndNoopCases (void)
{
	edict_t *peer = boots_peers[1]->edict;
	BootsCase(false, "-1");
	edict_t *item = SpawnBoots(-1, 2, 400, 150);
	BootsFloat(item, "attack_finished")->_float = qcvm->time + 10; /* Native rejection. */
	TouchBoots(item);
	assert(!HasBoots(boots_peers[0]->edict) && !HasBoots(peer));
	RetireBoots(item);
	item = SpawnBoots(-1, 2, 400, 150);
	TouchBoots(item);
	RetireBoots(item);
	ClearBoots(peer);
	item = SpawnBoots(-1, 2, 400, 150); /* Native accepts, but tuple is unchanged. */
	TouchBoots(item);
	assert(!HasBoots(peer));
	RetireBoots(item);
	BootsFloat(boots_peers[0]->edict, "jumpboots_time")->_float = qcvm->time + 1;
	item = SpawnBoots(-1, 2, 400, 150); /* Only warning phase resets to 1. */
	TouchBoots(item);
	assert(!HasBoots(peer));
	RetireBoots(item);
	/* Incompatible reflection metadata is a negative ABI case only. Actual QC
	 * still executes; this does not manufacture a compatible AD program. */
	BootsCase(false, "-1");
	item = SpawnBoots(-1, 2, 400, 150);
	ddef_t *def = ED_FindField("jumpboots_height");
	int type = def->type;
	def->type = ev_string;
	TouchBoots(item);
	def->type = type;
	assert(HasBoots(boots_peers[0]->edict) && !HasBoots(peer));
	RetireBoots(item);
	for (int dm = 0; dm < 2; ++dm)
	{
		BootsCase(false, "-1");
		item = SpawnBoots(-1, 2, 400, 150);
		Cvar_SetQuick(dm ? &deathmatch : &coop, dm ? "1" : "0");
		TouchBoots(item);
		assert(!HasBoots(peer));
		RetireBoots(item);
	}
	Cvar_SetQuick(&coop, "1");
	Cvar_SetQuick(&deathmatch, "0");
}

static void UnrelatedTriggerCases (void)
{
	edict_t *actor = boots_peers[0]->edict, *peer = boots_peers[1]->edict;
	for (int effect = 0; effect <= 4; ++effect)
	{
		BootsCase(false, "-1");
		edict_t *item = SpawnBoots(effect == 4 ? -1 : 30, 2, 430, 170);
		TouchBoots(item);
		RetireBoots(item);
		float acquired[countof(boots_config)];
		for (int i = 0; i < countof(acquired); ++i)
			acquired[i] = BootsFloat(peer, boots_config[i])->_float;
		BootsFloat(peer, "jumpboots_airlvl")->_float = 1;
		BootsFloat(peer, "jumpboots_onground")->_float = 0;
		BootsFloat(peer, "jumpboots_sound")->_float = 987;
		if (effect != 4) BootsClock(acquired[0] - 2);
		edict_t *trigger = ED_Alloc();
		trigger->v.classname = PR_SetEngineString("fixture_unrelated_trigger");
		trigger->v.solid = SOLID_TRIGGER;
		trigger->v.touch = ED_FindFunction("SUB_Null") - qcvm->functions;
		VectorCopy(actor->v.origin, trigger->v.origin);
		for (int i = 0; i < 3; ++i)
		{
			trigger->v.mins[i] = -32;
			trigger->v.maxs[i] = 32;
		}
		SV_LinkEdict(trigger, false);
		unrelated_boots_trigger = trigger;
		unrelated_boots_effect = effect;
		TouchBoots(trigger);
		unrelated_boots_trigger = NULL;
		RetireBoots(trigger);
		if (effect == 1)
			assert(BootsFloat(actor, "jumpboots_time")->_float == (float)(qcvm->time + 1));
		if (effect == 2 || effect == 4)
			assert(BootsFloat(actor, "jumpboots_finished")->_float == (float)(qcvm->time + 1));
		if (effect == 3) assert(!HasBoots(actor));
		assert(HasBoots(peer));
		for (int i = 0; i < countof(acquired); ++i)
			assert(BootsFloat(peer, boots_config[i])->_float == acquired[i]);
		assert(BootsFloat(peer, "jumpboots_airlvl")->_float == 1);
		assert(BootsFloat(peer, "jumpboots_onground")->_float == 0);
		assert(BootsFloat(peer, "jumpboots_sound")->_float == 987);
		/* Consumption/warnings must not replace or invalidate team progress. */
		ClearBoots(peer);
		SV_CoopSharedApplyToJoiningClient(peer);
		for (int i = 0; i < countof(acquired); ++i)
			assert(BootsFloat(peer, boots_config[i])->_float == acquired[i]);
	}
}

static void LateJoinAndExpiryCases (const char *offer)
{
	BootsCase(false, "-1");
	edict_t *item = SpawnBoots(12, 2, 420, 160);
	TouchBoots(item);
	float finish = BootsFloat(boots_peers[0]->edict, "jumpboots_finished")->_float;
	RetireBoots(item);
	BootsClock(qcvm->time + 1);
	boots_peers[2] = SpawnPeer(2, offer, 0); /* Real spawn/begin grants level tuple. */
	AssertFreshPeer(boots_peers[2]->edict);
	assert(BootsFloat(boots_peers[2]->edict, "jumpboots_finished")->_float == finish);
	FunctionalBootJump(boots_peers[2]->edict, 420, 160);
	BootsClock(finish); /* Native boundary is strictly '< time', not '<='. */
	BootsQC("ClientPowerups", boots_peers[1]->edict);
	assert(HasBoots(boots_peers[1]->edict));
	BootsClock(finish + .01);
	for (int i = 0; i < 3; ++i)
	{
		BootsQC("ClientPowerups", boots_peers[i]->edict);
		assert(!HasBoots(boots_peers[i]->edict));
	}
	boots_peers[3] = SpawnPeer(3, offer, 0);
	assert(!HasBoots(boots_peers[3]->edict)); /* Expired cache cannot grant boots. */
	BootsCase(false, "-1");
	item = SpawnBoots(-1, -1, 510, 240);
	TouchBoots(item);
	RetireBoots(item);
	ClearBoots(boots_peers[3]->edict);
	BootsClock(qcvm->time + 100);
	SV_CoopSharedApplyToJoiningClient(boots_peers[3]->edict);
	AssertFreshPeer(boots_peers[3]->edict);
	BootsQC("ClientPowerups", boots_peers[3]->edict);
	assert(HasBoots(boots_peers[3]->edict));
	FunctionalBootJump(boots_peers[3]->edict, 510, 240);
}

static void RestoredBundleCases (void)
{
	/* Two genuine native acquisitions staged as distinct restored payloads.
	 * Reuse the real merge owner, retaining one complete bundle rather than
	 * combining a finite timer with infinite charges or another height. */
	BootsCase(false, "-1");
	edict_t *item = SpawnBoots(-1, -1, 350, 180);
	TouchBoots(item);
	RetireBoots(item);
	Cvar_SetQuick(&sv_coop_classic, "1");
	item = SpawnBoots(20, 2, 540, 360);
	TouchBoots(item);
	RetireBoots(item);
	assert(BootsFloat(boots_peers[0]->edict, "jumpboots_finished")->_float > qcvm->time);
	assert(BootsFloat(boots_peers[1]->edict, "jumpboots_finished")->_float == -1);
	const size_t bytes = qcvm->progs->entityfields * sizeof(float);
	void *payload[2];
	float config[2][countof(boots_config)];
	for (int i = 0; i < 2; ++i)
	{
		payload[i] = Mem_Alloc(bytes);
		memcpy(payload[i], &boots_peers[i]->edict->v, bytes);
		for (int j = 0; j < countof(boots_config); ++j)
			config[i][j] = BootsFloat(boots_peers[i]->edict, boots_config[j])->_float;
	}
	assert(config[0][0] > qcvm->time && config[0][2] == 2);
	assert(config[1][0] == -1 && config[1][2] == -1);
	assert(memcmp(config[0], config[1], sizeof(config[0])) != 0);
	Cvar_SetQuick(&sv_coop_classic, "0");
	for (int first = 1; first >= 0; --first)
	{
		int second = 1 - first;
		for (int i = 0; i < 2; ++i)
			memcpy(&boots_peers[i]->edict->v, payload[i], bytes);
		SV_CoopSharedResetState();
		SV_CoopSharedMergeRestoredClient(boots_peers[first]->edict);
		/* The first merge applies to every eligible client. Reinstate the
		 * distinct actual native acquisition before merging the second input. */
		memcpy(&boots_peers[second]->edict->v, payload[second], bytes);
		for (int j = 0; j < countof(boots_config); ++j)
		{
			assert(BootsFloat(boots_peers[first]->edict, boots_config[j])->_float == config[first][j]);
			assert(BootsFloat(boots_peers[second]->edict, boots_config[j])->_float == config[second][j]);
		}
		SV_CoopSharedMergeRestoredClient(boots_peers[second]->edict);
		for (int i = 0; i < countof(boots_peers); ++i)
		{
			assert(HasBoots(boots_peers[i]->edict));
			for (int j = 0; j < countof(boots_config); ++j)
				assert(BootsFloat(boots_peers[i]->edict, boots_config[j])->_float == config[first][j]);
		}
	}
	for (int i = 0; i < 2; ++i) Mem_Free(payload[i]);
}

int main (int argc, char **argv)
{
	const char *map = "start";
	for (int i = 1; i + 1 < argc; ++i)
		if (!strcmp(argv[i], "-fixture-map")) map = argv[i + 1];
	Fixture_InitNativeEngine(argc, argv, map, true);
	assert(svs.maxclients == 4);
	const int hasharg = COM_CheckParm("-fixture-qc-sha256");
	assert(hasharg && hasharg + 1 < argc && strlen(argv[hasharg + 1]) == 64);
	char loaded_hash[65];
	for (int i = 0; i < 32; ++i)
		snprintf(loaded_hash + i * 2, 3, "%02x", qcvm->progssha256[i]);
	assert(!strcmp(loaded_hash, argv[hasharg + 1]));
	if (!ED_FindFunction("item_artifact_jumpboots"))
	{
		for (int i = 0; i < countof(boots_config); ++i)
			assert(!ED_FindField(boots_config[i]));
		assert(!ED_FindField("jumpboots_airlvl") && !ED_FindField("jumpboots_onground") &&
			!ED_FindField("jumpboots_sound"));
		printf("AD_JUMPBOOTS_SHARED_NATIVE_NOT_APPLICABLE qc_sha256=%s "
			"reason=no-native-jumpboots-function-or-fields\n", loaded_hash);
		return 0; /* Actual installed program has no boot mechanic to exercise. */
	}
	assert(ED_FindFunction("artifact_touch"));
	ConfigurePrivateMovementFixture(false, false);
	char public_offer[1024];
	ClientOffer(QSVR_PROTOCOL_PINNED, false, public_offer, sizeof(public_offer));
	boots_peers[0] = SpawnPeer(0, public_offer, 0);
	boots_peers[1] = SpawnPeer(1, public_offer, 0);
	cls.signon = SIGNONS;
	cls.state = ca_connected;
	ProfileCases();
	TierAndSentinelCases();
	RejectionAndNoopCases();
	UnrelatedTriggerCases();
	LateJoinAndExpiryCases(public_offer);
	RestoredBundleCases();
	printf("AD_JUMPBOOTS_SHARED_NATIVE_PASSED qc_sha256=%s regular/classic/overrides "
		"finite/infinite/tier/independent-charges/rejected/noop/warning/hazard/typed/latejoin/expiry/restore/functional-jump\n",
		loaded_hash);
	return 0;
}
