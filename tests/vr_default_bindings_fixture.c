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

int main (void)
{
	keybindings[K_RTRIGGER] = "+custom_attack";
	keybindings[K_LSHOULDER] = "impulse 12";
	VR_InputDefaultBindings_f ();
	assert (fixture_bind_count == 0); /* Desktop stays on vkQuake binds. */

	fixture_vr_active = true;
	VR_InputDefaultBindings_f ();
	assert (!strcmp (keybindings[K_RTRIGGER], "+custom_attack"));
	assert (!strcmp (keybindings[K_LSHOULDER], "impulse 12"));
	assert (!strcmp (keybindings[K_LTRIGGER], "+jump"));
	assert (!strcmp (keybindings[K_BBUTTON], "impulse 10"));
	assert (!strcmp (keybindings[K_LTHUMB], "+speed"));
	assert (!strcmp (keybindings[K_RTHUMB], "+vr_weaponmenu"));
	assert (!strcmp (keybindings[K_VR_ALTFIRE], "+button3"));
	assert (!strcmp (keybindings[K_RSHOULDER], "+showscores"));
	assert (!strcmp (keybindings[K_ABUTTON], "+showscores"));
	assert (!strcmp (keybindings[K_XBUTTON], "impulse 12"));
	assert (!keybindings[K_VR_RIGHT_STICK_UP]);
	assert (!keybindings[K_VR_RIGHT_STICK_DOWN]);
	assert (fixture_bind_count == 8);
	VR_InputDefaultBindings_f ();
	assert (fixture_bind_count == 8); /* No replacement on repeat. */
	/* Defaults fill absent controls, rather than enforcing policy over choices. */
	keybindings[K_RTHUMB] = "+jump";
	keybindings[K_VR_RIGHT_STICK_UP] = "+vr_weaponmenu";
	keybindings['t'] = "vr_turn180";
	VR_InputDefaultBindings_f ();
	assert (!strcmp (keybindings[K_RTHUMB], "+jump"));
	assert (!strcmp (keybindings[K_VR_RIGHT_STICK_UP], "+vr_weaponmenu"));
	assert (!strcmp (keybindings['t'], "vr_turn180"));
	assert (fixture_bind_count == 8);
	puts ("VR default bindings: ok");
	return 0;
}
