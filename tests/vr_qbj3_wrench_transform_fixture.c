/* Exercise the production held matrix and controller-roll adapter. Runtime
 * device/model loading and Vulkan submission are outside this numeric test. */
#include "../Quake/quakedef.h"
#include "../Quake/vr_locomotion.h"
#include "../Quake/vr_weapon_calibration.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

client_state_t cl;
cvar_t vr_world_scale = {.value = 0.75f};
cvar_t vr_gunmodelscale = {.value = 1.0f};
cvar_t vr_gunmodely = {.value = 0.0f};
cvar_t cl_gun_fovscale, cl_gun_x, cl_gun_y, cl_gun_z;

static int physical_hand;
static float held_scale = 1.25f;
static vec3_t held_offset = {17.0f, -23.0f, 31.0f};
static const vec3_t grip = {158.428571f, 151.0f, 148.714286f};

qboolean V_UseTrackedView (void) { return true; }
int V_AkimboViewmodelHand (const entity_t *e) { (void)e; return -1; }
qboolean V_HeldMeleeRenderEntity (const entity_t *e) { (void)e; return false; }
int VR_InputDominantPhysicalHand (void) { return physical_hand; }

qboolean VR_WeaponCalibrationLookupHeld (const char *name, qboolean enhanced,
	qboolean multiplayer, vec3_t offset, float *scale)
{
	assert (!strcmp (name, cl.viewent.model->name));
	assert (!enhanced);
	(void)multiplayer;
	VectorCopy (held_offset, offset);
	*scale = held_scale;
	return true;
}

static void transform (const float matrix[16], const vec3_t point, vec3_t out)
{
	for (int axis = 0; axis < 3; ++axis)
		out[axis] = matrix[axis] * point[0] + matrix[axis + 4] * point[1] +
			matrix[axis + 8] * point[2] + matrix[axis + 12];
}

static void near_vec (const vec3_t actual, const vec3_t expected)
{
	for (int axis = 0; axis < 3; ++axis)
	{
		if (fabsf (actual[axis] - expected[axis]) >= .002f)
			fprintf (stderr, "hand %d axis %d: got %.9g expected %.9g\n",
				physical_hand, axis, actual[axis], expected[axis]);
		assert (fabsf (actual[axis] - expected[axis]) < .002f);
	}
}

/* Independent Rodrigues rotation: catch a correction applied about pitched
 * model X by comparing geometry, rather than Euler angles or helper output. */
static void expected_point (const vec3_t raw, const aliashdr_t *geometry,
	const vec3_t hand_angles, float pitch, const vec3_t origin, vec3_t out)
{
	vec3_t local, world, forward, right, up, crossed;
	vec3_t tracked, original = {-hand_angles[0] + pitch,
		hand_angles[1], hand_angles[2]};
	float rotation[16];
	const float roll = DEG2RAD (physical_hand == 0 ? -70.0f : 70.0f);
	const float c = vr_world_scale.value / .75f * vr_gunmodelscale.value;
	for (int axis = 0; axis < 3; ++axis)
		local[axis] = c * ((raw[axis] - grip[axis]) * geometry->scale[axis] *
			held_scale + (axis == 2 ? vr_gunmodely.value : 0.0f));
	if (physical_hand == 1)
		local[1] = -local[1];
	IdentityMatrix (rotation);
	R_RotateForEntity (rotation, vec3_origin, original, ENTSCALE_DEFAULT);
	transform (rotation, local, world);
	VectorCopy (hand_angles, tracked);
	AngleVectors (tracked, forward, right, up);
	CrossProduct (forward, world, crossed);
	for (int axis = 0; axis < 3; ++axis)
		out[axis] = origin[axis] + world[axis] * cosf (roll) +
			crossed[axis] * sinf (roll) + forward[axis] *
			DotProduct (forward, world) * (1.0f - cosf (roll));
}

int main (void)
{
	static const vec3_t wrists[] = {
		{0, 0, 0}, {31, 47, -29}, {89, 133, 18}, {-89, -43, 72}
	};
	static const float pitches[] = {0, 21, -37};
	qmodel_t model = {0};
	aliashdr_t geometry = {0};
	lerpdata_t pose = {0};
	float matrix[16], offset_changed[16];
	vec3_t point, actual, expected, aliased;

	strcpy (com_gamedir, "qbj3");
	strcpy (model.name, "progs/v_wrench.mdl");
	cl.viewent.model = &model;
	cl.viewent.netstate.scale = ENTSCALE_DEFAULT;
	geometry.poseverttype = PV_QUAKE1;
	VectorCopy (((vec3_t){.2f, .3f, .4f}), geometry.scale);
	VectorCopy (((vec3_t){-50, 40, -30}), geometry.scale_origin);
	VectorCopy (((vec3_t){101, -51, 27}), pose.origin);
	for (physical_hand = 0; physical_hand < 2; ++physical_hand)
		for (size_t wrist = 0; wrist < sizeof (wrists) / sizeof (wrists[0]); ++wrist)
			for (size_t pitch = 0; pitch < sizeof (pitches) / sizeof (pitches[0]); ++pitch)
			{
				const float roll = physical_hand == 0 ? -70.0f : 70.0f;
				assert (VR_LocomotionControllerRollViewmodelAngles (wrists[wrist],
					pitches[pitch], roll, pose.angles));
				VectorCopy (wrists[wrist], aliased);
				assert (VR_LocomotionControllerRollViewmodelAngles (aliased,
					pitches[pitch], roll, aliased));
				near_vec (aliased, pose.angles);
				assert (R_HeldMeleeMatrix (&cl.viewent, &geometry, &pose,
					matrix) == (physical_hand == 1));
				/* Source offsets must not displace the controller-centered grip. */
				transform (matrix, grip, actual);
				near_vec (actual, pose.origin);
				for (int axis = 0; axis < 3; ++axis)
				{
					VectorCopy (grip, point);
					point[axis] += 23.0f;
					transform (matrix, point, actual);
					expected_point (point, &geometry, wrists[wrist], pitches[pitch],
						pose.origin, expected);
					near_vec (actual, expected);
				}
				VectorScale (held_offset, -1.0f, held_offset);
				assert (R_HeldMeleeMatrix (&cl.viewent, &geometry, &pose,
					offset_changed) == (physical_hand == 1));
				for (int i = 0; i < 16; ++i)
					assert (matrix[i] == offset_changed[i]);
			}
	physical_hand = 1;
	vr_gunmodely.value = 3.0f;
	vr_world_scale.value = 1.0f;
	vr_gunmodelscale.value = .8f;
	assert (VR_LocomotionControllerRollViewmodelAngles (wrists[1], 21, 70, pose.angles));
	assert (R_HeldMeleeMatrix (&cl.viewent, &geometry, &pose, matrix) == 1);
	expected_point (grip, &geometry, wrists[1], 21, pose.origin, expected);
	transform (matrix, grip, actual);
	near_vec (actual, expected);
	held_scale = 0.0f;
	assert (R_HeldMeleeMatrix (&cl.viewent, &geometry, &pose, matrix) < 0);
	assert (!VR_LocomotionControllerRollViewmodelAngles (wrists[1], NAN, 70, aliased));
	near_vec (aliased, vec3_origin);
	assert (!VR_LocomotionControllerRollViewmodelAngles (wrists[1], 21, NAN, aliased));
	near_vec (aliased, vec3_origin);
	/* Enyo uses source offsets and ordinary right-authored geometry. It must
	 * not inherit the wrench's centered grip or opposite handed reflection. */
	strcpy (com_gamedir, "enyo");
	strcpy (model.name, "progs/ee_v_sword.mdl");
	const mod_held_melee_recipe_t *recipe = Mod_GetHeldMeleeRecipe (model.name);
	assert (recipe && recipe->contact_profile == VR_WEAPON_CONTACT_PROFILE_ENYO);
	assert (!recipe->centered_grip && !recipe->authored_left && recipe->controller_roll == 0);
	assert (recipe->ready_frame == 0 && recipe->edge_vertices[0] == 13 && recipe->edge_vertices[1] == 77);
	held_scale = 1.75f;
	for (physical_hand = 0; physical_hand < 2; ++physical_hand)
		for (size_t wrist = 0; wrist < countof (wrists); ++wrist)
			for (size_t pitch = 0; pitch < countof (pitches); ++pitch)
			{
				float rotation[16];
				vec3_t local;
				const float c = vr_world_scale.value / .75f * vr_gunmodelscale.value;
				assert (VR_LocomotionHandRotToViewmodelAngles (wrists[wrist], pose.angles, pitches[pitch]));
				assert (R_HeldMeleeMatrix (&cl.viewent, &geometry, &pose, matrix) == (physical_hand == 0));
				IdentityMatrix (rotation);
				R_RotateForEntity (rotation, pose.origin, pose.angles, ENTSCALE_DEFAULT);
				for (int axis = 0; axis < 3; ++axis)
					local[axis] = c * (grip[axis] * geometry.scale[axis] * held_scale +
						geometry.scale_origin[axis] + held_offset[axis] +
						(axis == 2 ? vr_gunmodely.value : 0.0f));
				if (physical_hand == 0)
					local[1] = -local[1];
				transform (rotation, local, expected);
				transform (matrix, grip, actual);
				near_vec (actual, expected);
			}
	assert (!Mod_GetHeldMeleeRecipe (recipe->held));
	strcpy (com_gamedir, "id1");
	assert (!Mod_GetHeldMeleeRecipe (model.name));
	assert (R_HeldMeleeMatrix (&cl.viewent, &geometry, &pose, matrix) < 0);
	puts ("QBJ3 wrench and Enyo katana held transforms, calibration and winding passed");
	return 0;
}
