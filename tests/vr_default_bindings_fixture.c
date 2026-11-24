/* Exercise the production VR-only fill-in without loading the renderer. */
#include "../Quake/vr_input.c"

#include <assert.h>
#include <stdio.h>
#include <string.h>

char *keybindings[MAX_KEYS];
static qboolean fixture_vr_active;
static int fixture_bind_count;

int q_strcasecmp (const char *left, const char *right) { return strcasecmp (left, right); }

qboolean V_TrackedSessionActive (void)
{
	return fixture_vr_active;
}

void Key_SetBinding (int keynum, const char *binding)
{
	assert (keynum >= 0 && keynum < MAX_KEYS);
	keybindings[keynum] = (char *)binding;
	++fixture_bind_count;
}

/* A runtime can complete its first XR sample before the startup command
 * buffer runs default.cfg (unbindall) and config.cfg. Filling must wait for
 * the end of that chain, and a game change must arm it again. */
static void startup_order (void)
{
	vrxr_frame_t frame;
	memset (&frame, 0, sizeof (frame));
	frame.sample_id = 1;
	memset (keybindings, 0, sizeof (keybindings));
	vr_default_bindings_phase = VR_DEFAULT_BINDINGS_STARTUP_PENDING;
	vr_default_bindings_configs_ready = false;

	VR_InputConfigsPending ();
	VR_InputFillPendingDefaults (&frame);
	assert (!keybindings[K_VR_RTRIGGER]); /* the chain has not run yet */
	keybindings[K_VR_LTRIGGER] = "+moveup"; /* config.cfg choice */
	VR_InputConfigsExecuted_f ();
	VR_InputFillPendingDefaults (NULL);
	assert (!keybindings[K_VR_RTRIGGER]); /* still needs an XR sample */
	VR_InputFillPendingDefaults (&frame);
	assert (!strcmp (keybindings[K_VR_RTRIGGER], "+attack"));
	assert (!strcmp (keybindings[K_VR_LTRIGGER], "+moveup"));

	VR_InputConfigsPending (); /* game change */
	memset (keybindings, 0, sizeof (keybindings)); /* the new chain's unbindall */
	VR_InputFillPendingDefaults (&frame);
	assert (!keybindings[K_VR_RTRIGGER]);
	VR_InputConfigsExecuted_f ();
	VR_InputFillPendingDefaults (&frame);
	assert (!strcmp (keybindings[K_VR_RTRIGGER], "+attack"));
	assert (!strcmp (keybindings[K_VR_LTRIGGER], "+jump"));
}

/* The command buffer's unbindall: Key_Unbindall_f notifies the VR fill. */
static void chain_unbindall (void)
{
	memset (keybindings, 0, sizeof (keybindings));
	VR_InputDefaultBindingsCleared ();
}

static void assert_all_defaults_filled (int except_key)
{
	for (size_t i = 0; i < countof (vr_default_bindings); ++i)
	{
		const int key = vr_default_bindings[i].key;
		if (key == except_key)
			continue;
		assert (keybindings[key] && !strcmp (keybindings[key], vr_default_bindings[i].binding));
	}
}

/* An in-process game change runs with the XR session already attached, so
 * default.cfg's own vr_defaultbindings executes while a session is active.
 * The configuration chain then continues with config.cfg and a post-config,
 * both of which may begin with unbindall and carry no XR binds. Filling must
 * still happen after the whole chain, for every control the chain left
 * unbound, and must respect what the chain did bind. */
static void game_change_with_active_session (void)
{
	vrxr_frame_t frame;
	memset (&frame, 0, sizeof (frame));
	frame.sample_id = 7;
	fixture_vr_active = true;

	/* Previous game: settled and filled. */
	memset (keybindings, 0, sizeof (keybindings));
	vr_default_bindings_phase = VR_DEFAULT_BINDINGS_APPLIED;
	vr_default_bindings_configs_ready = true;
	vr_default_bindings_excluded = 0;
	VR_InputApplyDefaultBindings (0);
	assert_all_defaults_filled (-1);

	VR_InputConfigsPending ();			/* COM_SwitchGame */
	chain_unbindall ();					/* default.cfg */
	VR_InputDefaultBindings_f ();		/* default.cfg: vr_defaultbindings */
	VR_InputFillPendingDefaults (&frame); /* an XR frame mid-chain */
	chain_unbindall ();					/* config.cfg */
	Key_SetBinding (K_VR_LTRIGGER, "+moveup");
	VR_InputDefaultBindingChanged (K_VR_LTRIGGER);
	VR_InputFillPendingDefaults (&frame);
	assert (!keybindings[K_VR_RTRIGGER]); /* still inside the chain */
	chain_unbindall ();					/* post-config without XR binds */
	Key_SetBinding (K_VR_LTRIGGER, "+moveup");
	VR_InputDefaultBindingChanged (K_VR_LTRIGGER);
	VR_InputConfigsExecuted_f ();
	VR_InputFillPendingDefaults (&frame);
	assert (vr_default_bindings_phase == VR_DEFAULT_BINDINGS_APPLIED);
	assert_all_defaults_filled (K_VR_LTRIGGER);
	assert (!strcmp (keybindings[K_VR_LTRIGGER], "+moveup"));

	/* The options menu reset runs after the chain: it applies at once. */
	memset (keybindings, 0, sizeof (keybindings));
	VR_InputDefaultBindings_f ();
	assert_all_defaults_filled (-1);

	/* A detached reset still pending at a game change becomes the chain's
	 * fill; the chain's unbindall must not exclude every default. */
	fixture_vr_active = false;
	memset (keybindings, 0, sizeof (keybindings));
	VR_InputDefaultBindings_f ();
	assert (vr_default_bindings_phase == VR_DEFAULT_BINDINGS_EXPLICIT_PENDING);
	VR_InputConfigsPending ();
	chain_unbindall ();
	VR_InputDefaultBindings_f ();
	chain_unbindall ();
	fixture_vr_active = true;
	VR_InputConfigsExecuted_f ();
	VR_InputFillPendingDefaults (&frame);
	assert_all_defaults_filled (-1);

	/* The same at startup when the session attaches before quake.rc runs. */
	vr_default_bindings_phase = VR_DEFAULT_BINDINGS_STARTUP_PENDING;
	vr_default_bindings_configs_ready = false;
	memset (keybindings, 0, sizeof (keybindings));
	VR_InputConfigsPending ();
	chain_unbindall ();
	VR_InputDefaultBindings_f ();
	chain_unbindall ();
	VR_InputConfigsExecuted_f ();
	VR_InputFillPendingDefaults (&frame);
	assert_all_defaults_filled (-1);
}

int main (void)
{
	/* The explicit requests below arrive after the startup chain settled. */
	vr_default_bindings_phase = VR_DEFAULT_BINDINGS_APPLIED;
	vr_default_bindings_configs_ready = true;
	keybindings[K_VR_RTRIGGER] = "+custom_attack";
	keybindings[K_RTRIGGER] = "+desktop_attack";
	keybindings[K_RTHUMB] = "+desktop_wheel";
	keybindings[K_VR_LSHOULDER] = "impulse 12";
	VR_InputDefaultBindings_f ();
	assert (fixture_bind_count == 0); /* Desktop stays on vkQuake binds. */

	fixture_vr_active = true;
	VR_InputDefaultBindings_f ();
	assert (!strcmp (keybindings[K_VR_RTRIGGER], "+custom_attack"));
	assert (!strcmp (keybindings[K_RTRIGGER], "+desktop_attack"));
	assert (!strcmp (keybindings[K_RTHUMB], "+desktop_wheel"));
	assert (!strcmp (keybindings[K_VR_LSHOULDER], "impulse 12"));
	assert (!strcmp (keybindings[K_VR_LTRIGGER], "+jump"));
	assert (!strcmp (keybindings[K_VR_BBUTTON], "impulse 10"));
	assert (!strcmp (keybindings[K_VR_LTHUMB], "+speed"));
	assert (!strcmp (keybindings[K_VR_RTHUMB], "+vr_weaponmenu"));
	assert (!strcmp (keybindings[K_VR_ALTFIRE], "+button3"));
	assert (!strcmp (keybindings[K_VR_RSHOULDER], "+showscores"));
	assert (!strcmp (keybindings[K_VR_ABUTTON], "+showscores"));
	assert (!strcmp (keybindings[K_VR_XBUTTON], "impulse 12"));
	assert (!keybindings[K_VR_RIGHT_STICK_UP]);
	assert (!keybindings[K_VR_RIGHT_STICK_DOWN]);
	assert (fixture_bind_count == 8);
	VR_InputDefaultBindings_f ();
	assert (fixture_bind_count == 8); /* No replacement on repeat. */
	/* Defaults fill absent controls, rather than enforcing policy over choices. */
	keybindings[K_VR_RTHUMB] = "+jump";
	keybindings[K_VR_RIGHT_STICK_UP] = "+vr_weaponmenu";
	keybindings['t'] = "vr_turn180";
	VR_InputDefaultBindings_f ();
	assert (!strcmp (keybindings[K_VR_RTHUMB], "+jump"));
	assert (!strcmp (keybindings[K_VR_RIGHT_STICK_UP], "+vr_weaponmenu"));
	assert (!strcmp (keybindings['t'], "vr_turn180"));
	assert (fixture_bind_count == 8);
	startup_order ();
	game_change_with_active_session ();
	puts ("VR default bindings: ok");
	return 0;
}
