/* Append dedicated XR bind codes without moving existing controller keys. */
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
_Static_assert (K_VR_ABUTTON == 251, "unexpected dedicated VR A key");
_Static_assert (K_VR_BBUTTON == 252, "unexpected dedicated VR B key");
_Static_assert (K_VR_XBUTTON == 253, "unexpected dedicated VR X key");
_Static_assert (K_VR_YBUTTON == 254, "unexpected dedicated VR Y key");
_Static_assert (K_VR_LTHUMB == 255, "unexpected dedicated VR left thumb key");
_Static_assert (K_VR_RTHUMB == 256, "unexpected dedicated VR right thumb key");
_Static_assert (K_VR_LSHOULDER == 257, "unexpected dedicated VR left shoulder key");
_Static_assert (K_VR_RSHOULDER == 258, "unexpected dedicated VR right shoulder key");
_Static_assert (K_VR_LTRIGGER == 259, "unexpected dedicated VR left trigger key");
_Static_assert (K_VR_RTRIGGER == 260, "unexpected dedicated VR right trigger key");
_Static_assert (NUM_KEYCODES == 261, "unexpected keycode count");
_Static_assert (MAX_KEYS == 512, "dedicated XR keys require the expanded key table");
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
	assert_roundtrip ("VR_ABUTTON", K_VR_ABUTTON);
	assert_roundtrip ("VR_BBUTTON", K_VR_BBUTTON);
	assert_roundtrip ("VR_XBUTTON", K_VR_XBUTTON);
	assert_roundtrip ("VR_YBUTTON", K_VR_YBUTTON);
	assert_roundtrip ("VR_LTHUMB", K_VR_LTHUMB);
	assert_roundtrip ("VR_RTHUMB", K_VR_RTHUMB);
	assert_roundtrip ("VR_LSHOULDER", K_VR_LSHOULDER);
	assert_roundtrip ("VR_RSHOULDER", K_VR_RSHOULDER);
	assert_roundtrip ("VR_LTRIGGER", K_VR_LTRIGGER);
	assert_roundtrip ("VR_RTRIGGER", K_VR_RTRIGGER);
	puts ("VR native keycodes preserve physical ranges and dedicated XR bindings");
	return 0;
}
