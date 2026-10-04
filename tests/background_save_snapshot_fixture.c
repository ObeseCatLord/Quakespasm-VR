/* Background-save native fixture. It reuses the real loopback server/QC
 * bootstrap, blocks only the worker's temporary-file open, and compares two
 * byte captures of unchanged v5 and v7 worlds. */
#define LOCAL_LOAD_NATIVE_FIXTURE_ENTRY BackgroundSaveImportedLocalMain
#include "local_load_native_fixture.c"
#undef LOCAL_LOAD_NATIVE_FIXTURE_ENTRY

static SDL_Semaphore *background_save_gate;
static atomic_uint32_t background_save_block_writes;
static atomic_uint32_t background_save_waiting;
static atomic_uint32_t background_save_fail_replace;
extern cvar_t sv_autosave, sv_autosave_interval;

int __real_Sys_rename (const char *from, const char *to);
int __wrap_Sys_rename (const char *from, const char *to)
{
	if (Atomic_LoadUInt32 (&background_save_fail_replace) && strstr (from, ".sav.tmp"))
		return -1;
	return __real_Sys_rename (from, to);
}

static qboolean BackgroundSaveFileExists (const char *path)
{
	return Sys_FileType (path) == FS_ENT_FILE;
}

FILE *__real_Sys_fopen (const char *path, const char *mode);
FILE *__wrap_Sys_fopen (const char *path, const char *mode)
{
	if (Atomic_LoadUInt32 (&background_save_block_writes) && strstr (path, ".sav.tmp"))
	{
		Atomic_IncrementUInt32 (&background_save_waiting);
		SDL_WaitSemaphore (background_save_gate);
	}
	return __real_Sys_fopen (path, mode);
}

static char *BackgroundSaveRead (const char *name)
{
	char path[MAX_OSPATH];
	FILE *file;
	long length;
	char *data;

	assert (q_snprintf (path, sizeof (path), "%s/%s.sav", com_gamedir, name) > 0);
	file = fopen (path, "rb");
	assert (file && fseek (file, 0, SEEK_END) == 0);
	length = ftell (file);
	assert (length > 0 && fseek (file, 0, SEEK_SET) == 0);
	data = malloc ((size_t)length + 1);
	assert (data && fread (data, 1, (size_t)length, file) == (size_t)length);
	data[length] = '\0';
	fclose (file);
	return data;
}

static void BackgroundSaveWait (const char *name)
{
	char path[MAX_OSPATH];

	assert (q_snprintf (path, sizeof (path), "%s/%s.sav", com_gamedir, name) > 0);
	for (int i = 0; i < 500 && !BackgroundSaveFileExists (path); i++)
	{
		Host_SavegamePoll ();
		SDL_Delay (1);
	}
	Host_SavegameDrain ();
	assert (BackgroundSaveFileExists (path));
}

static void BackgroundSaveWriteAndCompare (const char *reference,
	const char *captured, int version)
{
	char *reference_bytes;
	char *captured_bytes;

	Cmd_ExecuteString (va ("save %s", reference), src_command);
	Host_SavegameDrain ();
	Cmd_ExecuteString (va ("save %s", captured), src_command);
	Host_SavegameDrain ();
	reference_bytes = BackgroundSaveRead (reference);
	captured_bytes = BackgroundSaveRead (captured);
	assert (!strcmp (reference_bytes, captured_bytes));
	assert (atoi (reference_bytes) == version);
	free (reference_bytes);
	free (captured_bytes);
}

static qboolean BackgroundSaveCatalogued (const char *name)
{
	for (filelist_item_t *item = savelist; item; item = item->next)
		if (!strcmp (item->name, name)) return true;
	return false;
}

static void BackgroundSaveBlock (void)
{
	assert (!Host_IsSaving ());
	Atomic_StoreUInt32 (&background_save_waiting, 0);
	Atomic_StoreUInt32 (&background_save_block_writes, true);
}

static void BackgroundSaveAwaitBlocked (unsigned count)
{
	for (int i = 0; i < 500 && Atomic_LoadUInt32 (&background_save_waiting) != count; ++i)
	{
		Host_SavegamePoll ();
		SDL_Delay (1);
	}
	assert (Atomic_LoadUInt32 (&background_save_waiting) == count && Host_IsSaving ());
}

static void BackgroundSaveRelease (unsigned count)
{
	BackgroundSaveAwaitBlocked (count);
	Atomic_StoreUInt32 (&background_save_block_writes, false);
	for (unsigned i = 0; i < count; ++i) SDL_SignalSemaphore (background_save_gate);
	Host_SavegameDrain ();
	assert (!Host_IsSaving ());
}

static void BackgroundSavePurposeAndRetry (client_t *peer)
{
	char anchor[sizeof (sv.lastsave)];
	filelist_item_t *catalogue;
	Cvar_SetQuick (&sv_coop_autosave_slots, "0");
	sv.coop_autosave_next_slot = 3;
	q_strlcpy (anchor, sv.lastsave, sizeof (anchor));
	catalogue = savelist;
	BackgroundSaveBlock ();
	Cmd_ExecuteString ("save background-quiet 0", src_command);
	BackgroundSaveAwaitBlocked (1);
	Host_SavegamePoll ();
	assert (!strcmp (sv.lastsave, anchor) && savelist == catalogue &&
		!BackgroundSaveCatalogued ("background-quiet") && sv.coop_autosave_next_slot == 3);
	BackgroundSaveRelease (1);
	assert (!strcmp (sv.lastsave, "background-quiet") &&
		BackgroundSaveCatalogued ("background-quiet") && sv.coop_autosave_next_slot == 3);

	/* Native score/safety owner admits this stationary, healthy SP player.
	 * Completion consumes only the time/cheat captured with the immutable save. */
	Cvar_SetQuick (&sv_autosave, "1");
	Cvar_SetQuick (&sv_autosave_interval, "30");
	qcvm->time = realtime = 100;
	memset (&sv.autosave, 0, sizeof (sv.autosave));
	sv.autosave.cheat = 1.25;
	peer->edict->v.health = 100;
	peer->edict->v.movetype = MOVETYPE_WALK;
	peer->edict->v.flags = FL_ONGROUND;
	peer->edict->v.button0 = 0;
	VectorClear (peer->edict->v.velocity);
	catalogue = savelist;
	BackgroundSaveBlock ();
	Host_AutosaveFrame ();
	BackgroundSaveAwaitBlocked (1);
	assert (!sv.autosave.time && sv.autosave.cheat == 1.25 && savelist == catalogue &&
		!BackgroundSaveCatalogued ("autosave") && !strcmp (sv.lastsave, "background-quiet"));
	qcvm->time = 102;
	sv.autosave.cheat = 1.75;
	BackgroundSaveRelease (1);
	assert (sv.autosave.time == 100 && sv.autosave.cheat == .5 &&
		!sv.autosave.retry_realtime && !strcmp (sv.lastsave, "autosave") &&
		BackgroundSaveCatalogued ("autosave") && sv.coop_autosave_next_slot == 3);
	puts ("BACKGROUND_SAVE_PURPOSE_COMPLETION_PASSED manual-skipnotify slots=0 SP-captured-time/cheat catalogue-on-completion");

	/* Both real writer slots remain occupied. The scheduler backs off without
	 * advancing its successful-save clock, then admits a retry at the deadline. */
	qcvm->time = realtime = 200;
	catalogue = savelist;
	BackgroundSaveBlock ();
	Cmd_ExecuteString ("save background-busy-a", src_command);
	Cmd_ExecuteString ("save background-busy-b", src_command);
	BackgroundSaveAwaitBlocked (2);
	Host_AutosaveFrame ();
	assert (sv.autosave.time == 100 && sv.autosave.retry_realtime == 205 &&
		savelist == catalogue && !strcmp (sv.lastsave, "autosave"));
	realtime = 204;
	Host_AutosaveFrame ();
	assert (sv.autosave.retry_realtime == 205 && Atomic_LoadUInt32 (&background_save_waiting) == 2);
	BackgroundSaveRelease (2);
	realtime = 204.99;
	Host_AutosaveFrame ();
	assert (!Host_IsSaving () && sv.autosave.time == 100 && sv.autosave.retry_realtime == 205);
	realtime = 205.01;
	BackgroundSaveBlock ();
	Host_AutosaveFrame ();
	BackgroundSaveAwaitBlocked (1);
	assert (sv.autosave.time == 100 && sv.autosave.retry_realtime == 205);
	BackgroundSaveRelease (1);
	assert (sv.autosave.time == 200 && !sv.autosave.retry_realtime && !strcmp (sv.lastsave, "autosave"));
	puts ("BACKGROUND_SAVE_SP_BUSY_RETRY_PASSED blocked-two-writers deadline-before/after successful-retry");

	Cmd_ExecuteString ("save background-quiet 0", src_command);
	Host_SavegameDrain ();
	char *before = BackgroundSaveRead ("autosave");
	catalogue = savelist;
	qcvm->time = realtime = 300;
	Atomic_StoreUInt32 (&background_save_fail_replace, true);
	Host_AutosaveFrame ();
	Host_SavegameDrain ();
	char *after = BackgroundSaveRead ("autosave");
	assert (!strcmp (before, after) && !strcmp (sv.lastsave, "background-quiet") &&
		savelist == catalogue && BackgroundSaveCatalogued ("autosave") &&
		sv.autosave.time == 200 && sv.autosave.retry_realtime == 305 && sv.coop_autosave_next_slot == 3);
	Atomic_StoreUInt32 (&background_save_fail_replace, false);
	realtime = 304.99;
	Host_AutosaveFrame ();
	assert (!Host_IsSaving () && sv.autosave.retry_realtime == 305);
	realtime = 305.01;
	Host_AutosaveFrame ();
	Host_SavegameDrain ();
	assert (sv.autosave.time == 300 && !sv.autosave.retry_realtime &&
		!strcmp (sv.lastsave, "autosave") && BackgroundSaveCatalogued ("autosave"));
	free (before); free (after);
	Cvar_SetQuick (&sv_autosave, "0");
	puts ("BACKGROUND_SAVE_SP_FAILURE_CATALOGUE_PASSED failed-replace preserved-file/lastsave/catalogue successful-deadline-retry");
}

static client_t *BackgroundSaveRoundtrip (const char *name, int version)
{
	client_t *peer = &svs.clients[0], *second = &svs.clients[1];
	vec3_t origin, second_origin;
	VectorCopy (peer->edict->v.origin, origin);
	if (version == 7) VectorCopy (second->edict->v.origin, second_origin);
	const float shells = peer->edict->v.ammo_shells, health = peer->edict->v.health;
	const float second_shells = version == 7 ? second->edict->v.ammo_shells : 0;
	peer->edict->v.ammo_shells += 7;
	peer->edict->v.health = 23;
	peer->edict->v.origin[0] += 64;
	peer = LocalLoad (va ("load %s", name));
	for (int axis = 0; axis < 3; ++axis)
		assert (fabsf (origin[axis] - peer->edict->v.origin[axis]) < .001f);
	assert (peer->edict->v.ammo_shells == shells && peer->edict->v.health == health);
	if (version == 7)
	{
		assert (sv.loadgame_multiplayer && EDICT_NUM (2)->free);
		client_state_t first = cl;
		qsocket_t *connection = cls.netcon;
		net_driverlevel = 0;
		second = Negotiate (1, modern_offer, QSVR_PROTOCOL_PINNED);
		host_client = second; sv_player = second->edict;
		Cmd_ExecuteString ("name acceptance-peer", src_client);
		Cmd_ExecuteString ("spawn", src_client);
		Cmd_ExecuteString ("begin", src_client);
		assert (second->spawned && !sv.loadgame && second->edict->v.ammo_shells == second_shells);
		for (int axis = 0; axis < 3; ++axis)
			assert (fabsf (second_origin[axis] - second->edict->v.origin[axis]) < .001f);
		cl = first; cls.netcon = connection;
		SZ_Clear (&cls.message);
	}
	printf ("BACKGROUND_SAVE_ROUNDTRIP_PASSED version=%d native-load/sign-on restored-player-state\n", version);
	return peer;
}

int main (int argc, char **argv)
{
	client_t *peer;
	char path[MAX_OSPATH];

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
	Cvar_SetQuick (&sv_autosave, "0");
	network_buffer = net_message;
	host_frametime = .025;
	peer = LocalSignon (false, false);
	background_save_gate = SDL_CreateSemaphore (0);
	assert (background_save_gate);
	/* Keep two allocated client slots for admission, while the first captures
	 * use the production single-player v5 writer selection. */
	assert (svs.maxclients == 2 && svs.maxclientslimit >= 2);
	svs.maxclients = 1;

	/* Both accepted slots remain blocked in worker-only I/O. Polling and a
	 * real server frame continue, while alias and full-slot saves return. */
	Atomic_StoreUInt32 (&background_save_block_writes, true);
	Cmd_ExecuteString ("save background-a", src_command);
	Cmd_ExecuteString ("save background-b", src_command);
	Cmd_ExecuteString ("save BACKGROUND-A.SAV", src_command);
	Cmd_ExecuteString ("save background-full", src_command);
	LocalFrame ();
	Host_SavegamePoll ();
	assert (q_snprintf (path, sizeof (path), "%s/background-a.sav", com_gamedir) > 0);
	assert (!BackgroundSaveFileExists (path));
	BackgroundSaveAwaitBlocked (2);
	SDL_SignalSemaphore (background_save_gate);
	SDL_SignalSemaphore (background_save_gate);
	Atomic_StoreUInt32 (&background_save_block_writes, false);
	BackgroundSaveWait ("background-a");
	BackgroundSaveWait ("background-b");
	assert (q_snprintf (path, sizeof (path), "%s/background-full.sav", com_gamedir) > 0);
	assert (!BackgroundSaveFileExists (path));

	/* An unchanged single-player snapshot remains byte-for-byte equal across
	 * independent captures. Add a live v7 player before the co-op comparison. */
	BackgroundSaveWriteAndCompare ("background-v5-reference", "background-v5-captured", 5);
	peer = BackgroundSaveRoundtrip ("background-v5-captured", 5);
	BackgroundSavePurposeAndRetry (peer);
	svs.maxclients = 2;
	Cvar_SetQuick (&sv_save_multiplayer, "1");
	net_driverlevel = 0;
	peer = SpawnPeer (1, modern_offer, QSVR_PROTOCOL_PINNED);
	assert (peer->active && peer->spawned && peer->knowntoqc && !peer->edict->free);
	host_client = peer; sv_player = peer->edict;
	Cmd_ExecuteString ("name acceptance-peer", src_client);
	BackgroundSaveWriteAndCompare ("background-v7-reference", "background-v7-captured", 7);
	BackgroundSaveRoundtrip ("background-v7-captured", 7);

	SDL_DestroySemaphore (background_save_gate);
	puts ("BACKGROUND_SAVE_SNAPSHOT_PASSED blocked-two-slot I/O alias/full rejection v5/v7-byte-capture");
	return 0;
}
