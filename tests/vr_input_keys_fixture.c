/* Keep native XR bind codes inside the fixed 256-key table without moving existing keys. */
#include <stdio.h>

typedef int qboolean;
#include "../Quake/keys.h"

#include <assert.h>
#include <string.h>

int Key_StringToKeynum (const char *str);

_Static_assert (K_LTHUMB == 207, "ordinary gamepad range moved");
_Static_assert (K_TOUCHPAD == 226, "ordinary gamepad range moved");
_Static_assert (K_LTHUMB_ALT == 227, "ALT gamepad range moved");
_Static_assert (K_TOUCHPAD_ALT == 246, "ALT gamepad range moved");
_Static_assert (K_PAUSE == 247, "existing pause key moved");
_Static_assert (K_VR_RIGHT_STICK_UP == 248, "unexpected VR stick-up key");
_Static_assert (K_VR_RIGHT_STICK_DOWN == 249, "unexpected VR stick-down key");
_Static_assert (K_VR_ALTFIRE == 250, "unexpected VR alt-fire key");
_Static_assert (NUM_KEYCODES == 251, "unexpected keycode count");
_Static_assert (NUM_KEYCODES <= MAX_KEYS, "native keycodes exceed key table capacity");
_Static_assert (K_VR_RIGHT_STICK_UP != K_VR_RIGHT_STICK_DOWN, "VR stick keys overlap");
_Static_assert (K_VR_RIGHT_STICK_UP != K_VR_ALTFIRE, "VR stick-up and alt-fire overlap");
_Static_assert (K_VR_RIGHT_STICK_DOWN != K_VR_ALTFIRE, "VR stick-down and alt-fire overlap");

static void assert_roundtrip (const char *name, int keynum)
{
	assert (Key_StringToKeynum (name) == keynum);
	assert (strcmp (Key_KeynumToString (keynum), name) == 0);
}

int main (void)
{
	assert_roundtrip ("VR_RIGHT_STICK_UP", K_VR_RIGHT_STICK_UP);
	assert_roundtrip ("VR_RIGHT_STICK_DOWN", K_VR_RIGHT_STICK_DOWN);
	assert_roundtrip ("VR_ALTFIRE", K_VR_ALTFIRE);
	puts ("VR native keycodes preserve existing gamepad ranges and MAX_KEYS capacity");
	return 0;
}
