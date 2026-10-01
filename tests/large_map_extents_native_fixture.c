/* Entire native loader owner; test helper invoked only after CPU draw join.
 * See docs/large-map-extents-final-2.0-plan.md. Normal client/main is retained. */
#ifdef NDEBUG
#error "Large-map extent fixture requires assertions"
#endif
#include "../Quake/gl_model.c"

int Fixture_VerifyWorldExtents (const char *expected_name)
{
	qmodel_t *model = cl.worldmodel;
	assert (!Tasks_IsWorker () && model && expected_name);
	assert (!strcmp (model->name, expected_name));
	assert (model->surfaces && model->numsurfaces > 1024);
	assert ((size_t)model->numsurfaces <= SIZE_MAX / (4 * sizeof (short)));
	short (*saved)[4] = Mem_Alloc ((size_t)model->numsurfaces * sizeof (*saved));
	for (int i = 0; i < model->numsurfaces; ++i)
	{
		msurface_t *surface = &model->surfaces[i];
		for (int axis = 0; axis < 2; ++axis)
		{
			saved[i][axis] = surface->texturemins[axis];
			saved[i][axis + 2] = surface->extents[axis];
			surface->texturemins[axis] = surface->extents[axis] = SHRT_MIN;
		}
		Mod_CalcSurfaceExtentsTask (i, &model);
		for (int axis = 0; axis < 2; ++axis)
		{
			assert (surface->texturemins[axis] == saved[i][axis]);
			assert (surface->extents[axis] == saved[i][axis + 2]);
		}
	}
	Mem_Free (saved);
	printf ("LARGE_MAP_EXTENTS_SERIAL_PASSED map=%s surfaces=%d\n", model->name, model->numsurfaces);
	return model->numsurfaces;
}
