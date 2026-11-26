/*
 * The workshop's tools, client side (see sv_workshop.c):
 *  - the grapple's line, drawn as a string of Christmas lights from the thrower to the star hook;
 *  - in VR, flares carried on the weapon hand's wrist, taken with the other hand and thrown by
 *    letting go (after the VRG mod for Deep Rock Galactic, which keeps flares on an arm holster
 *    and throws with the hand's own velocity);
 *  - in VR, the grapple in a holster on the right hip: reach down with the weapon hand and grip
 *    to draw it, let go to put it back.
 * GPL-2.0-or-later, as the rest of the engine.
 */
#include "quakedef.h"
#include "terrain.h"
#include "vr_aim.h"
#include "vr_input.h"
#include "vr_openxr.h"

entity_t *CL_NewTempEntity (void);				// cl_tent.c
int		  Key_StringToKeynum (const char *str); // keys.c

#define BULB_MODEL		   "progs/workshop/bulb.mdl"
#define FLARE_MODEL		   "progs/workshop/flare.mdl"
#define WRIST_MODEL		   "progs/workshop/wristband.mdl"
#define GRAPPLE_MODEL	   "progs/workshop/v_grapple.mdl"
#define HOOK_MODEL		   "progs/workshop/starhook.mdl"
#define WORKSHOP_LIGHT_KEY 0x5752 // dlight keys for the light string

static cvar_t vr_workshop_holsters = {"vr_workshop_holsters", "1", CVAR_ARCHIVE};		  // VR wrist flares and the hip grapple
static cvar_t vr_workshop_throw_scale = {"vr_workshop_throw_scale", "2.5", CVAR_ARCHIVE}; // hand speed to flare speed
static cvar_t cl_workshop_lights = {"cl_workshop_lights", "1", CVAR_ARCHIVE};			  // draw the grapple line as lights

static int		cl_workshop_flares = -1; // flares the server says the wrist carries; -1: no workshop here
static qboolean holding_flare, holding_grapple, offhand_grip_was, mainhand_grip_was;
static struct
{
	vec3_t pos;
	double time;
} hand_track[8];
static int hand_track_head;

static qmodel_t *WS_ClientModel (const char *name)
{
	// only models the server precached can be drawn without a hitch; look them up by name
	for (int i = 1; i < MAX_MODELS && cl.model_precache[i]; ++i)
		if (!strcmp (cl.model_precache[i]->name, name))
			return cl.model_precache[i];
	return NULL;
}

/*
//workshop_flarecount <count>
*/
static cvar_t cl_workshop_binds = {"cl_workshop_binds", "1", CVAR_ARCHIVE}; // bind the workshop keys left free

/*
The desktop keys, on the first workshop map: 9 the grapple, 0 the platform gun and - the lava
sprayer (weapon slots after Quake's 1-8), F the flare. Only keys with nothing on them (or Quake's
no-op "impulse 0", or for - Quake's own "sizedown") are taken, so a player's own binds stay; the
saved config keeps them after that.
*/
static void CL_Workshop_DefaultBinds (void)
{
	static const char *const binds[][2] = {{"9", "impulse 71"}, {"0", "impulse 70"}, {"-", "impulse 72"}, {"f", "workshop_flare"}};
	static const char *const names[] = {"grapple", "platform gun", "lava sprayer", "flare"};
	static qboolean			 done;
	char					 taken[128] = "";
	if (done || !cl_workshop_binds.value)
		return;
	done = true;
	for (size_t i = 0; i < sizeof (binds) / sizeof (binds[0]); ++i)
	{
		const int	key = Key_StringToKeynum (binds[i][0]);
		const char *now = key >= 0 ? keybindings[key] : NULL;
		if (key < 0 || (now && *now && strcmp (now, "impulse 0") && (i != 2 || strcmp (now, "sizedown"))))
			continue;
		Key_SetBinding (key, binds[i][1]);
		q_strlcat (taken, taken[0] ? ", " : "", sizeof (taken));
		q_strlcat (taken, va ("%s %s", binds[i][0], names[i]), sizeof (taken));
	}
	if (taken[0])
		Con_Printf ("Workshop keys bound: %s (cl_workshop_binds 0 leaves keys alone)\n", taken);
}

static void CL_Workshop_Flares_f (void)
{
	cl_workshop_flares = Cmd_Argc () > 1 ? CLAMP (0, atoi (Cmd_Argv (1)), 16) : -1;
	if (cl_workshop_flares >= 0)
		CL_Workshop_DefaultBinds ();
}

void CL_Workshop_Clear (void)
{
	cl_workshop_flares = -1;
	holding_flare = holding_grapple = offhand_grip_was = mainhand_grip_was = false;
	hand_track_head = 0;
	memset (hand_track, 0, sizeof (hand_track));
}

/*
==============================================================================

THE LIGHT STRING

==============================================================================
*/

// Where a player's grapple line starts: the local player's gun (hand in VR), anyone else's chest.
static void LightStringStart (int owner, vec3_t start)
{
	if (owner == cl.viewentity)
	{
		vec3_t hand_angles, forward, right, up;
		if (V_TrackedViewmodelActive () && V_TrackedPresentationHandWorldPose (VR_InputDominantPhysicalHand (), start, hand_angles))
		{
			AngleVectors (hand_angles, forward, right, up);
			VectorMA (start, 10.0f, forward, start);
			return;
		}
		AngleVectors (r_refdef.viewangles, forward, right, up);
		VectorCopy (r_refdef.vieworg, start);
		VectorMA (start, 14.0f, forward, start);
		VectorMA (start, 5.0f, right, start);
		VectorMA (start, -7.0f, up, start);
		return;
	}
	VectorCopy (cl.entities[owner].origin, start);
	start[2] += 16.0f;
}

static void AddLightString (const entity_t *hook, int owner, int which)
{
	vec3_t start, end, span;
	LightStringStart (owner, start);
	VectorCopy (hook->origin, end);
	VectorSubtract (end, start, span);
	const float length = VectorLength (span);
	if (length < 8.0f)
		return;
	qmodel_t   *bulb = WS_ClientModel (BULB_MODEL);
	const int	count = q_min ((int)(length / 14.0f), 96); // temporary entities are shared with props and beams
	const float sag = q_min (length * 0.04f, 20.0f); // a little slack between the hands and the hook
	for (int i = 1; i <= count && bulb; ++i)
	{
		const float t = (float)i / (float)(count + 1);
		entity_t   *ent = CL_NewTempEntity ();
		if (!ent)
			break;
		ent->model = bulb;
		VectorMA (start, t, span, ent->origin);
		ent->origin[2] -= sinf (t * (float)M_PI) * sag;
		ent->skinnum = i % 4; // red, green, blue, gold
		// bulbs are a few units tall: farther down the string they are drawn bigger so it still reads
		vec3_t to_eye;
		VectorSubtract (ent->origin, r_refdef.vieworg, to_eye);
		ent->netstate.scale = ENTSCALE_ENCODE (CLAMP (1.0f, VectorLength (to_eye) / 160.0f, 3.0f));
		ent->angles[1] = (float)(i * 47 % 360);
		ent->angles[0] = 180.0f; // hanging down from the wire
	}
	// the string lights the cave around it a little
	static const float colors[3][3] = {{1.0f, 0.25f, 0.2f}, {0.25f, 1.0f, 0.3f}, {1.0f, 0.8f, 0.25f}};
	for (int k = 0; k < 3; ++k)
	{
		const float t = (float)(k + 1) / 4.0f;
		dlight_t   *dl = CL_AllocDlight (WORKSHOP_LIGHT_KEY + which * 3 + k);
		VectorMA (start, t, span, dl->origin);
		dl->origin[2] -= sinf (t * (float)M_PI) * sag;
		dl->radius = 90.0f;
		dl->die = cl.time + 0.1;
		VectorCopy (colors[k], dl->color);
	}
}

static void AddLightStrings (void)
{
	if (!cl_workshop_lights.value || !cl.entities)
		return;
	qmodel_t *hook = WS_ClientModel (HOOK_MODEL);
	if (!hook)
		hook = WS_ClientModel ("progs/hook.mdl");
	if (!hook)
		return;
	int which = 0;
	for (int i = 1; i < cl.num_entities && which < 8; ++i)
	{
		const entity_t *ent = &cl.entities[i];
		const int		owner = ent->netstate.colormap;
		if (ent->model != hook || ent->msgtime != cl.mtime[0] || owner < 1 || owner > cl.maxclients)
			continue;
		AddLightString (ent, owner, which++);
	}
}

/*
==============================================================================

VR WRIST FLARES AND HIP HOLSTER

==============================================================================
*/

static qboolean HandGrip (const vrxr_frame_t *frame, int hand)
{
	return frame && hand >= 0 && hand < 2 && frame->hands[hand].active && frame->hands[hand].grip > 0.6f;
}

static void AddModelAt (const char *name, const vec3_t origin, const vec3_t angles, int skin)
{
	qmodel_t *model = WS_ClientModel (name);
	entity_t *ent = model ? CL_NewTempEntity () : NULL;
	if (!ent)
		return;
	ent->model = model;
	VectorCopy (origin, ent->origin);
	VectorCopy (angles, ent->angles);
	ent->skinnum = skin;
}

// The wrist sits a hand's length back along the weapon hand's forearm (VRG: 21 cm), a little up.
static qboolean WristPose (int hand, vec3_t wrist, vec3_t angles, vec3_t right)
{
	vec3_t origin, forward, up;
	if (!V_TrackedPresentationHandWorldPose (hand, origin, angles))
		return false;
	const float m = V_VRUnitsPerMetre ();
	AngleVectors (angles, forward, right, up);
	VectorMA (origin, -0.21f * m, forward, wrist);
	VectorMA (wrist, 0.05f * m, up, wrist);
	return true;
}

// The grapple's holster: on the right hip, half a metre or so under the eyes, facing down.
static void HolsterPose (vec3_t hip, vec3_t angles)
{
	const float m = V_VRUnitsPerMetre ();
	vec3_t		yaw_only = {0, r_refdef.viewangles[YAW], 0}, forward, right, up;
	AngleVectors (yaw_only, forward, right, up);
	VectorCopy (r_refdef.vieworg, hip);
	hip[2] -= 0.62f * m;
	VectorMA (hip, 0.2f * m, right, hip);
	VectorMA (hip, 0.05f * m, forward, hip);
	angles[0] = 80.0f; // muzzle down
	angles[1] = r_refdef.viewangles[YAW];
	angles[2] = 0.0f;
}

static void TrackHand (const vec3_t pos)
{
	hand_track_head = (hand_track_head + 1) % 8;
	VectorCopy (pos, hand_track[hand_track_head].pos);
	hand_track[hand_track_head].time = realtime;
}

// The hand's velocity over the last ~60 ms of its track.
static void HandVelocity (vec3_t vel)
{
	const int now = hand_track_head;
	int		  then = now;
	for (int k = 1; k < 8; ++k)
	{
		const int i = (now - k + 8) % 8;
		if (hand_track[i].time <= 0.0 || realtime - hand_track[i].time > 0.06)
			break;
		then = i;
	}
	const double dt = hand_track[now].time - hand_track[then].time;
	if (then == now || dt < 1e-3)
	{
		VectorCopy (vec3_origin, vel);
		return;
	}
	VectorSubtract (hand_track[now].pos, hand_track[then].pos, vel);
	VectorScale (vel, (float)(1.0 / dt), vel);
}

static void VRHolsters (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	if (!vr_workshop_holsters.value || cl_workshop_flares < 0 || !frame || V_TrackedAimMode () != VR_AIMMODE_CONTROLLER || cls.signon != SIGNONS ||
		cl.intermission || cls.demoplayback || cl.stats[STAT_HEALTH] <= 0)
	{
		holding_flare = holding_grapple = false; // dead or out of VR: the server puts tools away itself
		return;
	}
	const int	main = VR_InputDominantPhysicalHand (), off = !main;
	const float m = V_VRUnitsPerMetre ();
	vec3_t		wrist, wrist_angles, wrist_right, off_pos, off_angles, main_pos, main_angles;
	if (!WristPose (main, wrist, wrist_angles, wrist_right) || !V_TrackedPresentationHandWorldPose (off, off_pos, off_angles) ||
		!V_TrackedPresentationHandWorldPose (main, main_pos, main_angles))
		return;
	TrackHand (off_pos);

	// flares clipped along the wrist, one fewer drawn while one is in the other hand
	const int shown = cl_workshop_flares - (holding_flare ? 1 : 0);
	AddModelAt (WRIST_MODEL, wrist, wrist_angles, 0);
	for (int i = 0; i < shown && i < 6; ++i)
	{
		vec3_t at;
		VectorMA (wrist, ((float)i - (float)(shown - 1) * 0.5f) * 0.035f * m, wrist_right, at);
		AddModelAt (FLARE_MODEL, at, wrist_angles, 0);
	}

	// the off hand takes a flare from the wrist with its grip, and throws it by letting go
	const qboolean off_grip = HandGrip (frame, off);
	vec3_t		   gap;
	VectorSubtract (off_pos, wrist, gap);
	if (off_grip && !offhand_grip_was && !holding_flare && cl_workshop_flares > 0 && VectorLength (gap) < 0.12f * m)
	{
		holding_flare = true;
		VRXR_Haptic (off, 0.04f, 0.6f);
	}
	else if (!off_grip && holding_flare)
	{
		vec3_t vel;
		HandVelocity (vel);
		VectorScale (vel, vr_workshop_throw_scale.value, vel);
		Cbuf_AddText (va ("cmd workshop_throwflare %.1f %.1f %.1f %.1f %.1f %.1f\n", off_pos[0], off_pos[1], off_pos[2], vel[0], vel[1], vel[2]));
		holding_flare = false;
	}
	offhand_grip_was = off_grip;
	if (holding_flare)
		AddModelAt (FLARE_MODEL, off_pos, off_angles, 0);

	// the grapple: drawn from the hip holster by the weapon hand's grip, put back by letting go
	vec3_t hip, hip_angles;
	HolsterPose (hip, hip_angles);
	const qboolean main_grip = HandGrip (frame, main);
	VectorSubtract (main_pos, hip, gap);
	if (main_grip && !mainhand_grip_was && !holding_grapple && VectorLength (gap) < 0.16f * m)
	{
		holding_grapple = true;
		VRXR_Haptic (main, 0.05f, 0.8f);
		Cbuf_AddText ("cmd workshop_tool grapple\n");
	}
	else if (!main_grip && holding_grapple)
	{
		holding_grapple = false;
		Cbuf_AddText ("cmd workshop_tool off\n");
	}
	mainhand_grip_was = main_grip;
	if (!holding_grapple)
		AddModelAt (GRAPPLE_MODEL, hip, hip_angles, 0);
}

// Once a frame, with the other temporary entities (cl_tent.c).
void CL_Workshop_AddEntities (void)
{
	AddLightStrings ();
	VRHolsters ();
}

void CL_Workshop_Init (void)
{
	Cvar_RegisterVariable (&vr_workshop_holsters);
	Cvar_RegisterVariable (&vr_workshop_throw_scale);
	Cvar_RegisterVariable (&cl_workshop_lights);
	Cvar_RegisterVariable (&cl_workshop_binds);
	Cmd_AddCommand_ServerCommand ("workshop_flarecount", CL_Workshop_Flares_f);
}
