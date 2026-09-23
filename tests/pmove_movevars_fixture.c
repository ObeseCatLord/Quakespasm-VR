/* Real command tokenizer, serverinfo callbacks, Info_* and PM selection.
 * Callback dispatch is explicit; this is not network replay. */
#include "../Quake/cl_main.c"
#include <assert.h>
#include <math.h>
#include <stdio.h>

void Con_Warning (const char *fmt, ...) {}

static void full_info (const char *info)
{
	char command[COM_PARSE_MAX_TOKEN_SIZE + 64];
	assert (snprintf (command, sizeof(command), "fullserverinfo \"%s\"", info) < (int)sizeof(command));
	Cmd_TokenizeString (command);
	CL_ServerExtension_FullServerinfo_f ();
}

static void key_info (const char *key, const char *value)
{
	char command[COM_PARSE_MAX_TOKEN_SIZE + 128];
	assert (snprintf (command, sizeof(command), "serverinfo \"%s\" \"%s\"", key, value) < (int)sizeof(command));
	Cmd_TokenizeString (command);
	CL_ServerExtension_ServerinfoUpdate_f ();
}

static void near_value (float actual, float expected)
{
	assert (isfinite (actual) && fabsf (actual - expected) < .001f);
}

int main (void)
{
	memset (&cl, 0, sizeof(cl));
	PMCL_ClearMoveVars ();
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 800);
	near_value (movevars.maxspeed, 320);
	near_value (movevars.jumpspeed, 270);
	assert (!movevars.slidefix && !movevars.bunnyfriction);
	assert (movevars.flags & MOVEFLAG_QWEDGEBOX);

	// Previously Cmd_AddArg cut this valid fullserverinfo after gravity's 8.
	char long_info[COM_PARSE_MAX_TOKEN_SIZE + 1];
	memcpy (long_info, "\\metadata\\", 10);
	memset (long_info + 10, 'x', 1000);
	strcpy (long_info + 1010, "\\sv_gravity\\800");
	full_info (long_info);
	assert (!strcmp (cl.serverinfo, long_info));
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 800);
	// COM_Parse rejects overflow; the callbacks must not replace or erase
	// existing settings when the required argument is consequently absent.
	full_info ("\\sv_gravity\\600");
	memset (long_info, '8', COM_PARSE_MAX_TOKEN_SIZE);
	long_info[COM_PARSE_MAX_TOKEN_SIZE] = 0;
	full_info (long_info);
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 600);
	key_info ("sv_gravity", long_info);
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 600);
	key_info ("pm_slidefix", "0.5");
	assert (PMCL_SetMoveVars () && movevars.slidefix);

	/* Short input also checks bounded replacement in the real callback. */
	full_info ("\\sv_gravity\\600\\sv_maxspeed\\450\\pm_slidefix\\1\\pm_bunnyfriction\\1"
		"\\pm_ktjump\\12\\*pm_watersinkspeed\\50\\*pm_flyfriction\\7");
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 600);
	near_value (movevars.maxspeed, 450);
	near_value (movevars.watersinkspeed, 50);
	near_value (movevars.flyfriction, 7);
	assert (movevars.slidefix && movevars.bunnyfriction);
	key_info ("sv_gravity", "400");
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 400);
	key_info ("pm_edgefriction", "3");
	assert (PMCL_SetMoveVars ());
	assert (!(movevars.flags & MOVEFLAG_QWEDGEBOX));
	/* QSS-M uses the unstarred key to choose the edge-box policy, but the
	 * starred key supplies the friction value. Preserve that source contract. */
	near_value (movevars.edgefriction, 2);
	key_info ("*pm_edgefriction", "5");
	assert (PMCL_SetMoveVars ());
	near_value (movevars.edgefriction, 5);

	const char *invalid[] = {"nan", "inf", "1e50", "123garbage", "garbage"};
	for (unsigned i = 0; i < countof(invalid); i++)
	{
		key_info ("sv_gravity", invalid[i]);
		assert (PMCL_SetMoveVars ());
		near_value (movevars.gravity, 800);
	}
	key_info ("pm_stepheight", "1e30");
	key_info ("pm_walljump", "-1e30");
	assert (PMCL_SetMoveVars ());
	assert (movevars.stepheight == 18 && movevars.walljump == 0);
	/* Re-read protocol flags even when the cached serverinfo is unchanged. */
	cl.protocolflags = PRFL_FLOATCOORD;
	assert (PMCL_SetMoveVars ());
	assert (movevars.protocolflags == PRFL_FLOATCOORD);

	cl.stats[STAT_MOVEFLAGS] = MOVEFLAG_VALID | MOVEFLAG_NOGRAVITYONGROUND;
	cl.statsf[STAT_MOVEVARS_GRAVITY] = 350;
	cl.statsf[STAT_MOVEVARS_STEPHEIGHT] = 24;
	cl.statsf[STAT_MOVEVARS_JUMPVELOCITY] = 297;
	cl.statsf[STAT_MOVEVARS_ENTGRAVITY] = 0;
	cl.statsf[STAT_MOVEVARS_KTJUMP] = 99;
	/* Stats cannot take over an unrelated, non-PREDINFO connection. */
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 800);
	cl.protocol_pext2 = PEXT2_PREDINFO;
	assert (PMCL_SetMoveVars ());
	near_value (movevars.gravity, 350);
	near_value (movevars.entgravity, 0);
	near_value (movevars.ktjump, 12);
	assert (movevars.slidefix && movevars.bunnyfriction);

	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	assert (!PMCL_SetMoveVars ()); // Missing the private prerequisites.
	cl.protocol_pext2 = QSVR_PEXT2_REQUIRED;
	cl.stats[STAT_MOVEFLAGS] &= ~MOVEFLAG_VALID;
	assert (!PMCL_SetMoveVars ()); // Private replay cannot use serverinfo before movement stats arrive.
	cl.stats[STAT_MOVEFLAGS] |= MOVEFLAG_VALID;
	assert (PMCL_SetMoveVars ());
	near_value (movevars.ktjump, 99);
	near_value (movevars.entgravity, 1);
	assert (!movevars.slidefix && !movevars.bunnyfriction);
	cl.stats[STAT_MOVEFLAGS] |= MOVEFLAG_PM_SLIDEFIX | MOVEFLAG_PM_BUNNYFRICTION |
		(2u << MOVEFLAG_PM_WALLJUMP_SHIFT);
	assert (PMCL_SetMoveVars ());
	assert (movevars.slidefix && movevars.bunnyfriction && movevars.walljump == 2);
	cl.statsf[STAT_MOVEVARS_STEPHEIGHT] = INFINITY;
	assert (!PMCL_SetMoveVars ());
	cl.statsf[STAT_MOVEVARS_STEPHEIGHT] = 1e30f;
	assert (!PMCL_SetMoveVars ());
	cl.statsf[STAT_MOVEVARS_STEPHEIGHT] = 24;
	cl.statsf[STAT_MOVEVARS_KTJUMP] = NAN;
	assert (!PMCL_SetMoveVars ());
	cl.protocol_qsvr = 0;
	assert (PMCL_SetMoveVars ()); // Public protocol doesn't consume private stats.
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED + 1;
	assert (!PMCL_SetMoveVars ());

	/* Exercise cache invalidation separately from engine resource teardown. */
	memset (&cl, 0, sizeof(cl));
	PMCL_ClearMoveVars ();
	assert (PMCL_SetMoveVars ());
	near_value (movevars.maxspeed, 320);
	assert (!movevars.slidefix && movevars.protocolflags == 0);
	full_info ("\\sv_maxspeed\\200");
	assert (PMCL_SetMoveVars ());
	near_value (movevars.maxspeed, 200);
	puts ("PM client serverinfo callbacks, defaults, refresh/reset, public/private stats and numeric rejection passed");
	return 0;
}
