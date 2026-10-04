/* Reuse the bounded XR input fixture seams; exercise the production Bonk adapter.
 * The generated-model identity is covered separately with all installed assets. */
#define main prior_input_main
#define Mod_GetHeldMeleeRecipe prior_no_recipe
#define V_HeldMeleeEdgeOffsets prior_no_held_edge
#define VR_WeaponCalibrationLookupMelee prior_no_melee_profile
#include "vr_input_fixture.c"
#undef main
#undef Mod_GetHeldMeleeRecipe
#undef V_HeldMeleeEdgeOffsets
#undef VR_WeaponCalibrationLookupMelee

static qboolean bonk_edge_ready = true;
static const mod_held_melee_recipe_t bonk_recipe = {
	.game = "bonkjam", .source = "progs/v_hammer_default.mdl",
	.contact_profile = VR_WEAPON_CONTACT_PROFILE_BONK,
	.source_vertices = 1004, .source_triangles = 1274, .frames = 255,
	.vertices = 714, .triangles = 877, .ready_frame = 0
};
const mod_held_melee_recipe_t *Mod_GetHeldMeleeRecipe (const char *name)
{ return name && !strcmp (name, bonk_recipe.source) ? &bonk_recipe : NULL; }
qboolean V_HeldMeleeEdgeOffsets (vec3_t base, vec3_t tip, vec3_t collision)
{
	fixture_clear_vec (base); fixture_clear_vec (tip); fixture_clear_vec (collision);
	tip[0] = 20;
	return bonk_edge_ready;
}
qboolean VR_WeaponCalibrationLookupMelee (const char *name, vr_melee_gesture_profile_t *out)
{
	if (!out || !Mod_GetHeldMeleeRecipe (name)) return false;
	memset (out, 0, sizeof (*out));
	out->enabled = true; out->speed = 1.25f; out->has_ready_frame = true; out->ready_frame = 0;
	return true;
}
/* Include once to expose the existing preparation boundary; no alternate adapter. */
#include "../Quake/vr_input.c"

int main (void)
{
	prior_input_main (); // unchanged input/movement/menu regression reference
	vrxr_frame_t frame = neutral_frame ();
	qmodel_t model = {0}, world = {0};
	aliashdr_t geometry = {0};
	entity_t entities[2] = {0};
	strcpy (model.name, bonk_recipe.source); model.type = mod_alias;
	geometry.poseverttype = PV_QUAKE1; geometry.numverts = 1004;
	geometry.numtris = 1274; geometry.numframes = 255; geometry.numskins = 1;
	model.extradata[PV_QUAKE1] = (byte *)&geometry;
	cl.model_precache[1] = &model; cl.viewent.model = &model; cl.viewent.skinnum = 0;
	cl.worldmodel = &world; cl.entities = entities; cl.num_entities = 2; cl.viewentity = 1;
	cl.stats[STAT_WEAPON] = 1; cl.stats[STAT_ACTIVEWEAPON] = 4096; cl.stats[STAT_HEALTH] = 100;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED; cl.vr_weapon_contact_mode = 6;
	cl.vr_weapon_contact_profile = VR_WEAPON_CONTACT_PROFILE_BONK;
	cls.state = ca_connected; cls.signon = SIGNONS; cls.demoplayback = false;
	cl.paused = cl.intermission = false; key_dest = key_game;
	input_grab_active = waiting_for_binding = angle_locked = false;
	fixture_frame = &frame; fixture_body_offset_available = fixture_muzzle_available = true;
	set_cvar ("vr_immersive_melee", 1); set_cvar ("vr_lefthanded", 0);
	vr_aimmode.value = 7;
	for (int i = 0; i < 3; ++i)
	{
		frame.devices[i].valid = frame.devices[i].tracked = true;
		frame.devices[i].velocity_valid = frame.devices[i].angular_velocity_valid = true;
		frame.devices[i].kind = i ? VRXR_DEVICE_HAND : VRXR_DEVICE_HEAD;
		frame.devices[i].hand = i - 1;
	}
	frame.should_render = true;
	VR_InputClear (); reset_events ();
	VR_InputCommands (&frame); VR_InputCommands (&frame);
	assert (!VR_InputPhysicalMeleeAllowed () && VR_InputBonkPhysicalAllowed ());
	for (int mode = 0; mode < 3; ++mode)
	{
		set_cvar ("vr_movement_mode", mode);
		fixture_head_angles[YAW] = 30; fixture_hand_angles[0][YAW] = 90; fixture_hand_angles[1][YAW] = 150;
		fixture_turn_yaw = 45; // post-turn mapping, distinct from selected command basis
		frame.sample_id++;
		cl.pendingcmd = (usercmd_t){0};
		cl.pendingcmd.vr_active = cl.pendingcmd.vr_handpos_relative = true;
		vec3_t grip = {8, 0, 22}, base = {0, -16, 0}, tip = {0, 16, 0};
		assert (VR_InputPrepareMeleeContact (&cl.pendingcmd, &frame, 1, grip, 1, &model, 0, &geometry, NULL, true, base, tip, fixture_turn_yaw));
		assert (cl.pendingcmd.vr_contact.flags == (2 | 4 | 8));
		near_motion (cl.pendingcmd.vr_contact.head_angles[YAW], 75);
		assert (VR_InputPendingContactAccepted (&cl.pendingcmd, &frame, false));
		usercmd_t cmd = {0};
		cmd.viewangles[YAW] = 75 + mode * 60;
		cmd.forwardmove = -177; cmd.sidemove = 33; cmd.upmove = -19;
		usercmd_t before = cmd;
		assert (VR_InputSuppressBonkAttack (&cmd)); // final post-CSQC filter helper
		assert (!memcmp (&before, &cmd, sizeof cmd));
		assert (VR_InputMergeMeleeAttack (BUTTON_ATTACK | BUTTON_JUMP, true) == BUTTON_JUMP);
		cl.vr_weapon_contact_mode = 2;
		assert (!VR_InputSuppressBonkAttack (&cmd));
		assert (!VR_InputGestureMeleeActive ()); // old peer/missing mandatory capability
		cl.vr_weapon_contact_mode = 6;
		bonk_edge_ready = false;
		assert (!VR_InputSuppressBonkAttack (&cmd));
		assert (VR_InputMergeMeleeAttack (BUTTON_ATTACK, true) == BUTTON_ATTACK);
		bonk_edge_ready = true;
		geometry.numverts--;
		assert (!VR_InputSuppressBonkAttack (&cmd));
		geometry.numverts++;
		frame.sample_id++;
		assert (!VR_InputSuppressBonkAttack (&cmd)); // pending identity is command-correlated
	}
	cl.protocol_qsvr = 0;
	assert (!VR_InputBonkPhysicalAllowed ());
	assert (VR_InputMergeMeleeAttack (BUTTON_ATTACK, true) == BUTTON_ATTACK);
	for (unsigned profile = 1; profile <= 7; ++profile)
	{
		cl.vr_weapon_contact_profile = profile;
		assert (!VR_InputPhysicalMeleeAllowed ());
	}
	puts ("BONK_INPUT_HEAD_POSTTURN_THREE_MODES_GATES_TRIGGER_PRESERVATION_PASSED");
	return 0;
}
