/* Background-save native fixture. It reuses the real loopback server/QC
 * bootstrap, blocks only the worker's temporary-file open, and compares two
 * byte captures of unchanged v5 and v7 worlds. */
#define LOCAL_LOAD_NATIVE_FIXTURE_ENTRY BackgroundSaveImportedLocalMain
#include "local_load_native_fixture.c"
#undef LOCAL_LOAD_NATIVE_FIXTURE_ENTRY

static SDL_Semaphore *background_save_gate;
static atomic_uint32_t background_save_block_writes;

static qboolean BackgroundSaveFileExists (const char *path)
{
	return Sys_FileType (path) == FS_ENT_FILE;
}

FILE *__real_Sys_fopen (const char *path, const char *mode);
FILE *__wrap_Sys_fopen (const char *path, const char *mode)
{
	if (Atomic_LoadUInt32 (&background_save_block_writes) && strstr (path, ".sav.tmp"))
		SDL_WaitSemaphore (background_save_gate);
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

int main (int argc, char **argv)
{
	client_t *peer;
	char path[MAX_OSPATH];

	Fixture_InitNativeEngine (argc, argv, "e1m1", true);
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
	svs.maxclients = 2;
	Cvar_SetQuick (&sv_save_multiplayer, "1");
	peer = SpawnPeer (1, modern_offer, QSVR_PROTOCOL_PINNED);
	assert (peer->active && peer->spawned && peer->knowntoqc && !peer->edict->free);
	BackgroundSaveWriteAndCompare ("background-v7-reference", "background-v7-captured", 7);

	SDL_DestroySemaphore (background_save_gate);
	puts ("BACKGROUND_SAVE_SNAPSHOT_PASSED blocked-two-slot I/O alias/full rejection v5/v7-byte-capture");
	return 0;
}
