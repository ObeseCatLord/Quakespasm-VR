/* Independent decoded ready-pose landmarks from the installed Peril MDLs.
 * Compute expectations from each actual header origin, rather than copying
 * the built-in offset tuples. Included after FixtureReadModel is defined. */
static const struct
{
	const char *name;
	float		scale;
	vec3_t		grip, muzzle;
	qboolean	has_muzzle;
} fixture_peril_landmarks[] = {
	{"v_shadaxe0", 0.25f, {31.000000000f, -16.700000000f, -16.500000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
	{"v_shadaxe3", 0.25f, {21.000000000f, -26.500000000f, -29.000000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
	{"v_ghook", 0.5f, {21.000000000f, 0.000000000f, -17.000000000f}, {35.000000000f, 0.000000000f, -16.300000000f}, true},
	{"v_shot", 0.5f, {19.000000000f, 1.000000000f, -12.500000000f}, {26.900000000f, 0.000000000f, -12.500000000f}, true},
	{"v_shot2", 0.5f, {20.000000000f, -1.000000000f, -14.000000000f}, {31.500000000f, -5.250000000f, -10.700000000f}, true},
	{"v_shot3", 0.5f, {19.000000000f, -1.000000000f, -14.500000000f}, {34.400000000f, -5.200000000f, -14.700000000f}, true},
	{"v_nail", 0.5f, {22.390492700f, -11.246959500f, -15.674507400f}, {39.005790100f, -8.258900900f, -5.848483800f}, true},
	{"v_nail2", 0.5f, {22.000000000f, -8.000000000f, -16.000000000f}, {34.400000000f, 0.360000000f, -13.800000000f}, true},
	{"v_rock", 0.5f, {11.000000000f, -3.000000000f, -12.000000000f}, {23.000000000f, -3.000000000f, -10.400000000f}, true},
	{"v_rock2", 0.5f, {17.000000000f, -7.000000000f, -11.000000000f}, {26.500000000f, 0.000000000f, -9.900000000f}, true},
	{"v_light", 0.5f, {24.000000000f, -5.000000000f, -18.000000000f}, {37.000000000f, -2.500000000f, -8.000000000f}, true},
	{"v_plasma", 0.5f, {22.000000000f, 0.000000000f, -14.000000000f}, {39.000000000f, 0.000000000f, -10.500000000f}, true},
	{"v_axe", 0.33f, {25.000000000f, -10.000000000f, -25.000000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
	{"v_nail3", 0.4f, {27.000000000f, -8.000000000f, -16.000000000f}, {59.000000000f, -3.000000000f, -10.000000000f}, true},
	{"v_rock3", 0.5f, {19.000000000f, 0.000000000f, -15.000000000f}, {30.000000000f, 0.000000000f, -12.000000000f}, true},
	{"v_zershot", 0.5f, {12.000000000f, 0.000000000f, -14.000000000f}, {19.000000000f, 1.000000000f, -8.000000000f}, true},
	{"v_shadaxe1", 0.25f, {21.000000000f, -26.500000000f, -29.000000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
	{"v_shadaxe2", 0.25f, {21.000000000f, -26.500000000f, -29.000000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
	{"v_shadaxe4", 0.25f, {21.000000000f, -26.500000000f, -29.000000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
	{"v_shadaxe5", 0.25f, {21.000000000f, -26.500000000f, -29.000000000f}, {0.000000000f, 0.000000000f, 0.000000000f}, false},
};

static int FixturePerilLandmarkIndex (const char *path)
{
	for (size_t i = 0; i < countof (fixture_peril_landmarks); ++i)
	{
		char expected[MAX_QPATH];
		snprintf (expected, sizeof (expected), "progs/%s.mdl", fixture_peril_landmarks[i].name);
		if (!strcmp (path, expected))
			return (int)i;
	}
	return -1;
}

static void FixtureAssertPerilProfile (const char *path)
{
	int	   index = FixturePerilLandmarkIndex (path);
	size_t size;
	vec3_t origin, held, muzzle, expected, local;
	float  scale;
	assert (index >= 0);
	byte *model = FixtureReadModel (path, &size);
	assert (size >= 84 && !memcmp (model, "IDPO", 4));
	for (int axis = 0; axis < 3; ++axis)
	{
		uint32_t bits = FixtureLE32 (model + 20 + 4 * axis);
		memcpy (&origin[axis], &bits, sizeof (bits));
		expected[axis] =
			-fixture_peril_landmarks[index].scale * fixture_peril_landmarks[index].grip[axis] - (1 - fixture_peril_landmarks[index].scale) * origin[axis];
		local[axis] = fixture_peril_landmarks[index].scale * (fixture_peril_landmarks[index].muzzle[axis] - fixture_peril_landmarks[index].grip[axis]) / .75f;
	}
	free (model);
	size_t before = asset_reads;
	assert (VR_WeaponCalibrationLookupHeld (path, false, held, &scale));
	AssertVector (held, expected[0], expected[1], expected[2]);
	assert (fabsf (scale - fixture_peril_landmarks[index].scale) < .00001f);
	assert (VR_WeaponCalibrationLookupMuzzle (path, false, muzzle));
	if (fixture_peril_landmarks[index].has_muzzle)
		AssertVector (muzzle, -local[1], local[2], local[0]);
	else
		AssertVector (muzzle, 0, 0, 0);
	assert (asset_reads == before);
}

static void FixtureAssertAllPerilProfiles (void)
{
	for (size_t i = 0; i < countof (fixture_peril_landmarks); ++i)
	{
		char path[MAX_QPATH];
		snprintf (path, sizeof (path), "progs/%s.mdl", fixture_peril_landmarks[i].name);
		FixtureAssertPerilProfile (path);
	}
}
