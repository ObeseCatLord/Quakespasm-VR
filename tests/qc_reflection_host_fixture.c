/* Test-only access to the unchanged native client-QC bootstrap. */
#include "../Quake/host.c"

void FixtureLoadCSProgsNative (void)
{
	CL_LoadCSProgs ();
}
