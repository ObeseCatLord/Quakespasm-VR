#include "r_vrik.h"

#include <assert.h>
#include <math.h>
#include <string.h>

int host_framecount;
static qmodel_t *fixture_model;
static md5liveinfo_t fixture_live;

void Sys_Error (const char *error, ...)
{
	(void)error;
	assert (!"unexpected mathlib error");
}

/* The fixture exercises only the pure palette path. */
qboolean Mod_GetMD5LiveData (qmodel_t *model, md5liveinfo_t *out)
{
	if (model != fixture_model)
		return false;
	*out = fixture_live;
	return true;
}

static void IdentityAt (float matrix[12], float x, float y, float z)
{
	memset (matrix, 0, 12 * sizeof(*matrix));
	matrix[0] = matrix[5] = matrix[10] = 1.0f;
	matrix[3] = x;
	matrix[7] = y;
	matrix[11] = z;
}

static void RotateYAt (float matrix[12], float x, float y, float z)
{
	IdentityAt (matrix, x, y, z);
	matrix[0] = matrix[10] = 0.0f;
	matrix[2] = 1.0f;
	matrix[8] = -1.0f;
}

static void BuildLeg (md5liveinfo_t *live, md5livejoint_t joints[4],
	float palette[4 * 12], aliashdr_t *surface)
{
	int index;

	memset (live, 0, sizeof(*live));
	memset (joints, 0, 4 * sizeof(*joints));
	memset (surface, 0, sizeof(*surface));
	for (index = 0; index < MD5_VRIK_JOINT_COUNT; index++)
		live->jointindex[index] = -1;
	/* Hip -> upper leg -> knee -> foot.  The upper socket shares the hip
	 * origin, matching common authored skeletons and testing descendant motion. */
	joints[0].parent = -1;
	joints[1].parent = 0;
	joints[2].parent = 1;
	joints[3].parent = 2;
	IdentityAt (joints[0].bind, 0, 0, 0);
	IdentityAt (joints[1].bind, 0, 0, 0);
	IdentityAt (joints[2].bind, 0, 0, -1);
	IdentityAt (joints[3].bind, 0, 0, -2);
	memcpy (palette, joints[0].bind, sizeof(joints[0].bind));
	memcpy (palette + 12, joints[1].bind, sizeof(joints[1].bind));
	memcpy (palette + 24, joints[2].bind, sizeof(joints[2].bind));
	memcpy (palette + 36, joints[3].bind, sizeof(joints[3].bind));
	live->firstsurface = surface;
	live->joints = joints;
	live->numbones = 4;
	live->compatible = true;
	live->from_rerelease = true;
	live->jointindex[MD5_VRIK_HIP] = 0;
	live->jointindex[MD5_VRIK_UPPERLEG_L] = 1;
	live->jointindex[MD5_VRIK_LOWERLEG_L] = 2;
	live->jointindex[MD5_VRIK_FOOT_L] = 3;
}

static float DistanceTo (const float matrix[12], float x, float y, float z)
{
	float dx = matrix[3] - x;
	float dy = matrix[7] - y;
	float dz = matrix[11] - z;
	return sqrtf (dx * dx + dy * dy + dz * dz);
}

static void AssertFinitePalette (const float *palette, int bones)
{
	int index;

	for (index = 0; index < bones * 12; index++)
		assert (isfinite (palette[index]));
}

static void BuildTwoLegs (md5liveinfo_t *live, md5livejoint_t joints[7],
	float palette[7 * 12], aliashdr_t *surface)
{
	int index;

	memset (live, 0, sizeof(*live));
	memset (joints, 0, 7 * sizeof(*joints));
	memset (surface, 0, sizeof(*surface));
	for (index = 0; index < MD5_VRIK_JOINT_COUNT; index++)
		live->jointindex[index] = -1;
	joints[0].parent = -1;
	joints[1].parent = joints[4].parent = 0;
	joints[2].parent = 1; joints[3].parent = 2;
	joints[5].parent = 4; joints[6].parent = 5;
	IdentityAt (joints[0].bind, 0, 0, 0);
	IdentityAt (joints[1].bind, 0, -1, 0);
	IdentityAt (joints[2].bind, 0, -1, -1);
	IdentityAt (joints[3].bind, 0, -1, -2);
	IdentityAt (joints[4].bind, 0, 1, 0);
	IdentityAt (joints[5].bind, 0, 1, -1);
	IdentityAt (joints[6].bind, 0, 1, -2);
	for (index = 0; index < 7; index++)
		memcpy (palette + index * 12, joints[index].bind,
			sizeof(joints[index].bind));
	live->firstsurface = surface;
	live->joints = joints;
	live->numbones = 7;
	live->compatible = true;
	live->from_rerelease = true;
	live->jointindex[MD5_VRIK_HIP] = 0;
	live->jointindex[MD5_VRIK_UPPERLEG_L] = 1;
	live->jointindex[MD5_VRIK_LOWERLEG_L] = 2;
	live->jointindex[MD5_VRIK_FOOT_L] = 3;
	live->jointindex[MD5_VRIK_UPPERLEG_R] = 4;
	live->jointindex[MD5_VRIK_LOWERLEG_R] = 5;
	live->jointindex[MD5_VRIK_FOOT_R] = 6;
}

static void TestLegSingularities (void)
{
	md5liveinfo_t live;
	md5livejoint_t joints[4];
	aliashdr_t surface;
	float palette[4 * 12];
	r_vrik_lowerbody_model_targets_t targets;

	/* Collapse the animated upper segment.  The solver must still keep the
	 * lower joint at its analytic knee rather than rotating around its old hip
	 * pivot, and clamp an unreachable foot to the two-bone reach. */
	BuildLeg (&live, joints, palette, &surface);
	IdentityAt (palette + 24, 0, 0, 0);
	IdentityAt (palette + 36, 0, 0, -1);
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT);
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][0] = 5.0f;
	assert (R_VRIKApplyLowerBody (&live, palette, &targets));
	AssertFinitePalette (palette, 4);
	assert (fabsf (DistanceTo (palette + 24, 0, 0, 0) - 1.0f) < 0.01f);
	assert (DistanceTo (palette + 24, 0.995f, 0, -0.099875f) < 0.02f);
	assert (DistanceTo (palette + 36, 1.99f, 0, 0) < 0.02f);

	/* A target exactly at the hip is another two-bone singularity. */
	BuildLeg (&live, joints, palette, &surface);
	IdentityAt (palette + 24, 0, 0, 0);
	IdentityAt (palette + 36, 0, 0, -1);
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT);
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	assert (R_VRIKApplyLowerBody (&live, palette, &targets));
	AssertFinitePalette (palette, 4);
	assert (fabsf (DistanceTo (palette + 24, 0, 0, 0) - 1.0f) < 0.01f);
	assert (DistanceTo (palette + 36, 0, 0, -0.01f) < 0.02f);
}

static void TestCrossLegIsolation (void)
{
	md5liveinfo_t live;
	md5livejoint_t joints[7];
	aliashdr_t surface;
	float palette[7 * 12], right_before[3 * 12];
	r_vrik_lowerbody_model_targets_t targets;

	BuildTwoLegs (&live, joints, palette, &surface);
	memcpy (right_before, palette + 4 * 12, sizeof(right_before));
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT);
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][0] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][1] = -1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][2] = -1.5f;
	assert (R_VRIKApplyLowerBody (&live, palette, &targets));
	AssertFinitePalette (palette, 7);
	assert (!memcmp (right_before, palette + 4 * 12, sizeof(right_before)));
}

static void TestMirroredPairedLegPoles (void)
{
	md5liveinfo_t live;
	md5livejoint_t joints[7];
	aliashdr_t surface;
	float palette[7 * 12], legacy_palette[7 * 12];
	r_vrik_lowerbody_model_targets_t targets;

	BuildTwoLegs (&live, joints, palette, &surface);
	/* Both animated knees are initially biased toward +lateral.  Pair mode
	 * must turn that into equal/opposite poles for symmetric roots/targets. */
	IdentityAt (palette + 2 * 12, 0, -0.5f, -0.8660254f);
	IdentityAt (palette + 3 * 12, 0, -0.5f, -1.8660254f);
	IdentityAt (palette + 5 * 12, 0, 1.5f, -0.8660254f);
	IdentityAt (palette + 6 * 12, 0, 1.5f, -1.8660254f);
	memcpy (legacy_palette, palette, sizeof(palette));
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT) |
		R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT);
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	targets.confidence[R_VRIK_LOWER_RIGHT_FOOT] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][0] = 0.6f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][1] = -1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][2] = -1.5f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][0] = 0.6f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][1] = 1.0f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][2] = -1.5f;
	assert (R_VRIKApplyLowerBodyWithPolePolicy (&live, palette, &targets,
		R_VRIK_LOWERBODY_POLES_MIRRORED_PAIR));
	AssertFinitePalette (palette, 7);
	assert (DistanceTo (palette + 3 * 12, 0.6f, -1.0f, -1.5f) < 0.01f);
	assert (DistanceTo (palette + 6 * 12, 0.6f, 1.0f, -1.5f) < 0.01f);
	assert (fabsf (palette[2 * 12 + 3] - palette[5 * 12 + 3]) < 0.01f);
	assert (fabsf (palette[2 * 12 + 7] + palette[5 * 12 + 7]) < 0.01f);
	assert (fabsf (palette[2 * 12 + 11] - palette[5 * 12 + 11]) < 0.01f);

	/* The explicit animated policy is exactly the legacy wrapper. */
	assert (R_VRIKApplyLowerBody (&live, legacy_palette, &targets));
	BuildTwoLegs (&live, joints, palette, &surface);
	IdentityAt (palette + 2 * 12, 0, -0.5f, -0.8660254f);
	IdentityAt (palette + 3 * 12, 0, -0.5f, -1.8660254f);
	IdentityAt (palette + 5 * 12, 0, 1.5f, -0.8660254f);
	IdentityAt (palette + 6 * 12, 0, 1.5f, -1.8660254f);
	assert (R_VRIKApplyLowerBodyWithPolePolicy (&live, palette, &targets,
		R_VRIK_LOWERBODY_POLES_ANIMATED));
	assert (!memcmp (legacy_palette, palette, sizeof(palette)));
}

static void TestPairedLegTargetsAndDegeneracy (void)
{
	md5liveinfo_t live;
	md5livejoint_t joints[7];
	aliashdr_t surface;
	float palette[7 * 12];
	r_vrik_lowerbody_model_targets_t targets;

	BuildTwoLegs (&live, joints, palette, &surface);
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT) |
		R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT);
	targets.orientation_mask = targets.usable_mask;
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	targets.confidence[R_VRIK_LOWER_RIGHT_FOOT] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][0] = 0.65f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][1] = -0.75f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][2] = -1.4f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][0] = -0.35f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][1] = 1.3f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][2] = -1.55f;
	RotateYAt (targets.orientation[R_VRIK_LOWER_LEFT_FOOT], 0, 0, 0);
	IdentityAt (targets.orientation[R_VRIK_LOWER_RIGHT_FOOT], 0, 0, 0);
	assert (R_VRIKApplyLowerBodyWithPolePolicy (&live, palette, &targets,
		R_VRIK_LOWERBODY_POLES_MIRRORED_PAIR));
	assert (DistanceTo (palette + 3 * 12, 0.65f, -0.75f, -1.4f) < 0.01f);
	assert (DistanceTo (palette + 6 * 12, -0.35f, 1.3f, -1.55f) < 0.01f);
	assert (fabsf (palette[3 * 12] - 0.0f) < 0.01f);
	assert (fabsf (palette[3 * 12 + 2] - 1.0f) < 0.01f);
	assert (fabsf (palette[3 * 12 + 8] + 1.0f) < 0.01f);
	assert (fabsf (palette[3 * 12 + 10] - 0.0f) < 0.01f);

	/* Both collapsed legs and foot-at-hip targets must choose finite, stable
	 * pair fallbacks rather than leaking a singularity into the palette. */
	BuildTwoLegs (&live, joints, palette, &surface);
	IdentityAt (palette + 2 * 12, 0, -1, 0);
	IdentityAt (palette + 3 * 12, 0, -1, -1);
	IdentityAt (palette + 5 * 12, 0, 1, 0);
	IdentityAt (palette + 6 * 12, 0, 1, -1);
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT) |
		R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT);
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	targets.confidence[R_VRIK_LOWER_RIGHT_FOOT] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][1] = -1.0f;
	targets.position[R_VRIK_LOWER_RIGHT_FOOT][1] = 1.0f;
	assert (R_VRIKApplyLowerBodyWithPolePolicy (&live, palette, &targets,
		R_VRIK_LOWERBODY_POLES_MIRRORED_PAIR));
	AssertFinitePalette (palette, 7);
}

static void TestCalibrationProjection (void)
{
	qmodel_t model;
	aliashdr_t surface;
	md5livejoint_t joints[10];
	r_vrik_calibration_projection_input_t input;
	r_vrik_calibration_projection_t output;
	r_vrik_calibration_reference_t reference;
	int index;

	memset (&model, 0, sizeof(model));
	memset (&surface, 0, sizeof(surface));
	memset (joints, 0, sizeof(joints));
	memset (&fixture_live, 0, sizeof(fixture_live));
	for (index = 0; index < MD5_VRIK_JOINT_COUNT; index++)
		fixture_live.jointindex[index] = -1;
	for (index = 0; index < 10; index++)
		joints[index].parent = index ? 0 : -1;
	/* The two feet establish a bind floor at -16, the head at +16. */
	IdentityAt (joints[0].bind, 0, 0, 0);
	IdentityAt (joints[1].bind, -0.4f, 0, 0);
	IdentityAt (joints[2].bind, -0.4f, 0, -8);
	IdentityAt (joints[3].bind, -0.4f, 0, -16);
	IdentityAt (joints[4].bind, 0.4f, 0, 0);
	IdentityAt (joints[5].bind, 0.4f, 0, -8);
	IdentityAt (joints[6].bind, 0.4f, 0, -16);
	IdentityAt (joints[7].bind, 0, 0, 16);
	IdentityAt (joints[8].bind, -1, 0, 12);
	IdentityAt (joints[9].bind, 1, 0, 12);
	joints[2].parent = 1; joints[3].parent = 2;
	joints[5].parent = 4; joints[6].parent = 5;
	fixture_live.firstsurface = &surface;
	fixture_live.joints = joints;
	fixture_live.numbones = 10;
	fixture_live.compatible = true;
	fixture_live.from_rerelease = true;
	fixture_live.jointindex[MD5_VRIK_HIP] = 0;
	fixture_live.jointindex[MD5_VRIK_UPPERLEG_L] = 1;
	fixture_live.jointindex[MD5_VRIK_LOWERLEG_L] = 2;
	fixture_live.jointindex[MD5_VRIK_FOOT_L] = 3;
	fixture_live.jointindex[MD5_VRIK_UPPERLEG_R] = 4;
	fixture_live.jointindex[MD5_VRIK_LOWERLEG_R] = 5;
	fixture_live.jointindex[MD5_VRIK_FOOT_R] = 6;
	fixture_live.jointindex[MD5_VRIK_HEAD] = 7;
	fixture_live.jointindex[MD5_VRIK_SHOULDER_L] = 8;
	fixture_live.jointindex[MD5_VRIK_SHOULDER_R] = 9;
	fixture_model = &model;
	R_VRIKResetSkinCaches ();
	memset (&input, 0, sizeof(input));
	input.hmd_position[2] = 1.7f;
	input.forward[1] = 1.0f;
	input.right[0] = 1.0f;
	input.up[2] = 1.0f;
	assert (R_VRIKGetCalibrationReference (&model, &reference));
	assert (R_VRIKProjectCalibrationReference (&model, &input, &output));
	assert (fabsf (output.metres_per_bind_unit - 0.053125f) < 0.001f);
	assert (fabsf (output.position[R_VRIK_LOWER_LEFT_FOOT][2]) < 0.001f);
	assert (fabsf (output.position[R_VRIK_LOWER_RIGHT_FOOT][2]) < 0.001f);
	assert (fabsf (output.orientation_wxyz[R_VRIK_LOWER_HIP][0]) <= 1.0f);
	fixture_model = NULL;
}

int main (void)
{
	md5liveinfo_t live;
	md5livejoint_t joints[4];
	aliashdr_t surface;
	float palette[4 * 12];
	r_vrik_lowerbody_model_targets_t targets;
	r_vrik_lowerbody_targets_t raw;

	BuildLeg (&live, joints, palette, &surface);
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT);
	targets.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][0] = 1.0f;
	targets.position[R_VRIK_LOWER_LEFT_FOOT][2] = -1.5f;
	assert (R_VRIKApplyLowerBody (&live, palette, &targets));
	assert (DistanceTo (palette + 36, 1.0f, 0.0f, -1.5f) < 0.01f);

	BuildLeg (&live, joints, palette, &surface);
	memset (&targets, 0, sizeof(targets));
	targets.usable_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP);
	targets.confidence[R_VRIK_LOWER_HIP] = 1.0f;
	targets.position[R_VRIK_LOWER_HIP][0] = 2.0f;
	assert (R_VRIKApplyLowerBody (&live, palette, &targets));
	assert (DistanceTo (palette + 36, 2.0f, 0.0f, -2.0f) < 0.001f);

	memset (&raw, 0, sizeof(raw));
	raw.present_mask = R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT);
	raw.tracked_mask = raw.present_mask;
	raw.confidence[R_VRIK_LOWER_LEFT_FOOT] = 1.0f;
	raw.position[R_VRIK_LOWER_LEFT_FOOT][0] = 5.0f;
	R_VRIKSetLowerBodyTargets (1, &raw);
	assert (R_VRIKGetLowerBodyTargets (1, &raw));
	assert (raw.present_mask == R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT));
	R_VRIKSetLowerBodyTargets (1, NULL);
	assert (!R_VRIKGetLowerBodyTargets (1, &raw));
	R_VRIKResetSkinCaches ();
	TestLegSingularities ();
	R_VRIKResetSkinCaches ();
	TestCrossLegIsolation ();
	R_VRIKResetSkinCaches ();
	TestMirroredPairedLegPoles ();
	R_VRIKResetSkinCaches ();
	TestPairedLegTargetsAndDegeneracy ();
	TestCalibrationProjection ();
	return 0;
}
