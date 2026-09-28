/* Shared offline bootstrap for production server/QC fixtures. Only test
 * executables link the Loop_Init wrapper; no sockets or clients are created. */
#ifndef QSVR_NATIVE_ENGINE_FIXTURE_H
#define QSVR_NATIVE_ENGINE_FIXTURE_H

int __wrap_Loop_Init (void) { return 0; }

static void Fixture_InitNativeEngine (int argc, char **argv, const char *map,
	qboolean cooperative)
{
	static quakeparms_t parms;
	parms.basedir = ".";
	parms.argc = argc;
	parms.argv = argv;
	host_parms = &parms;
	COM_InitArgv (argc, argv);
	isDedicated = COM_CheckParm ("-dedicated") != 0;
	assert (isDedicated && COM_CheckParm ("-noudp"));
	assert (SDL_Init (0));
	Sys_Init ();
	Host_Init ();
	Cvar_SetQuick (&sv_coop_autosave, "0");
	if (cooperative)
	{
		Cvar_SetQuick (&coop, "1");
		Cvar_SetQuick (&deathmatch, "0");
	}
	PR_SwitchQCVM (&sv.qcvm);
	SV_SpawnServer (map);
	assert (sv.active && qcvm == &sv.qcvm);
}

#endif
