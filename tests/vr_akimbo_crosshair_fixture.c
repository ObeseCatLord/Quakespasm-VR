/* Exercise the production per-hand crosshair rays with a prepared pair. */
#include "../Quake/vr_input.c"

#include <assert.h>
#include <math.h>
#include <stdio.h>

client_static_t cls;
client_state_t cl;
cvar_t vr_aimmode = {"vr_aimmode", "7", CVAR_ARCHIVE};
cvar_t vr_gunmodelscale = {"vr_gunmodelscale", "1", CVAR_ARCHIVE};
cvar_t vr_gunmodelpitch = {"vr_gunmodelpitch", "0", CVAR_ARCHIVE};

static vrxr_frame_t fixture_frame;
static entity_t fixture_entities[2];
static entity_t fixture_pair[2];
static qboolean fixture_pair_ready;
static qboolean fixture_hands_valid = true;

const vrxr_frame_t *GL_OpenXRFrame (void) { return &fixture_frame; }
qboolean V_AkimboPairReady (void) { return fixture_pair_ready; }
entity_t *V_AkimboPairEntity (int hand) { return hand >= 0 && hand < 2 ? &fixture_pair[hand] : NULL; }

qboolean V_TrackedPresentationHandBodyOffset (int hand, vec3_t out)
{
	if (!fixture_hands_valid || hand < 0 || hand > 1) return false;
	out[0] = out[1] = out[2] = 0.0f;
	return true;
}

qboolean V_TrackedPresentationHandAngles (int hand, vec3_t out)
{
	if (!fixture_hands_valid || hand < 0 || hand > 1) return false;
	out[0] = out[1] = out[2] = 0.0f;
	out[YAW] = hand ? 90.0f : 0.0f;
	return true;
}

qboolean V_TrackedPresentationHandWorldPose (int hand, vec3_t origin, vec3_t angles)
{
	if (!fixture_hands_valid || hand < 0 || hand > 1) return false;
	origin[0] = origin[1] = origin[2] = 0.0f;
	origin[0] = hand ? 20.0f : 10.0f;
	angles[0] = angles[1] = angles[2] = 0.0f;
	return true;
}

qboolean V_AkimboTransformAnchor (int hand, const vec3_t angles, vec3_t out)
{
	(void)angles;
	out[0] = out[1] = out[2] = 0.0f;
	out[0] = hand ? 2.0f : 1.0f;
	return true;
}

void V_AkimboPairCollisionOffset (int hand, vec3_t out)
{
	out[0] = out[1] = out[2] = 0.0f;
	out[1] = hand ? 4.0f : 3.0f;
}

entity_t *V_HeldMeleeEntity (void) { return NULL; }
qboolean VR_WeaponCalibrationCurrentMuzzle (vec3_t out)
{
	out[0] = out[1] = out[2] = 0.0f;
	out[0] = 5.0f;
	return true;
}
qboolean VR_LocomotionMuzzleOffsetToWorld (const vec3_t offset,
	const vec3_t angles, float scale, float pitch, qboolean left, vec3_t out)
{
	(void)angles; (void)scale; (void)pitch; (void)left;
	VectorCopy (offset, out);
	return true;
}
qboolean V_TrackedWeaponCollisionPresentation (vec3_t origin, vec3_t delta)
{
	(void)origin; (void)delta;
	return false;
}

void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	const float yaw = angles[YAW] * (float)M_PI / 180.0f;
	forward[0] = cosf (yaw); forward[1] = sinf (yaw); forward[2] = 0.0f;
	if (right) right[0] = right[1] = right[2] = 0.0f;
	if (up) up[0] = up[1] = up[2] = 0.0f;
}

int main (void)
{
	vec3_t starts[2] = {{0}}, forwards[2] = {{0}};
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.entities = fixture_entities;
	cl.viewentity = 1;
	cl.num_entities = 2;
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	fixture_frame.should_render = true;
	fixture_pair_ready = true;
	assert (VR_InputCrosshairAimRays (starts, forwards) == 2);
	assert (starts[0][0] == 11.0f && starts[0][1] == 3.0f);
	assert (starts[1][0] == 22.0f && starts[1][1] == 4.0f);
	assert (forwards[0][0] > 0.99f && forwards[1][1] > 0.99f);
	assert (VR_InputCrosshairAimRay (starts[0], forwards[0]));
	assert (starts[0][0] == 22.0f); /* Right hand is dominant by default. */
	vr_lefthanded.value = 1.0f;
	assert (VR_InputCrosshairAimRay (starts[0], forwards[0]));
	assert (starts[0][0] == 11.0f);
	vr_lefthanded.value = 0.0f;
	fixture_hands_valid = false;
	assert (VR_InputCrosshairAimRays (starts, forwards) == 0);
	fixture_hands_valid = true;
	fixture_pair_ready = false;
	assert (VR_InputCrosshairAimRays (starts, forwards) == 1);
	assert (starts[0][0] == 5.0f);
	fixture_frame.should_render = false;
	assert (VR_InputCrosshairAimRays (starts, forwards) == 0);
	puts ("VR akimbo crosshair rays: ok");
	return 0;
}
