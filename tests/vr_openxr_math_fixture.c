/* Compare production clip correction against direct eye-space projection.
 * Covers rotation, cant, asymmetric FOV, IPD, translation and reversed depth. */
#include "../Quake/vr_openxr_math.h"
#include <assert.h>
#include <stdio.h>

static void pose (float m[3][4], double yaw, double roll, double x, double y, double z)
{
	const double c = cos (yaw), s = sin (yaw), cr = cos (roll), sr = sin (roll);
	const double values[3][4] = {{c * cr, -c * sr, s, x}, {sr, cr, 0, y}, {-s * cr, s * sr, c, z}};
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < 4; ++j)
			m[i][j] = (float)values[i][j];
}
static void check (vrxr_frame_t *f)
{
	const double units = 26.246719, near = 4;
	float		 clip[2][16];
	assert (VRXR_StereoClip (f, units, near, clip));
	for (int sample = 0; sample < 40; ++sample)
	{
		/* Head-local point may be a vertex after any model transform. */
		double local[3] = {sin (sample * .7) * 13, cos (sample * .6) * 9, -5 - sample * 7};
		double center[4] = {local[0], -local[1], near, -local[2]}, world[3];
		for (int k = 0; k < 3; ++k)
		{
			world[k] = f->devices[0].matrix[k][3] * units;
			for (int j = 0; j < 3; ++j)
				world[k] += f->devices[0].matrix[k][j] * local[j];
		}
		for (int eye = 0; eye < 2; ++eye)
		{
			const vrxr_view_t *v = &f->views[eye];
			double			   p[3] = {0}, actual[4] = {0};
			for (int row = 0; row < 3; ++row)
				for (int k = 0; k < 3; ++k)
					p[row] += v->matrix[k][row] * (world[k] - v->matrix[k][3] * units);
			const double expected[4] = {
				(2 * p[0] + (v->right + v->left) * p[2]) / (v->right - v->left), (-2 * p[1] - (v->up + v->down) * p[2]) / (v->up - v->down), near, -p[2]};
			for (int row = 0; row < 4; ++row)
			{
				for (int k = 0; k < 4; ++k)
					actual[row] += clip[eye][k * 4 + row] * center[k];
				assert (fabs (actual[row] - expected[row]) < .0001 * (1 + fabs (expected[row])));
			}
		}
	}
}

static void check_hidden_area_projection (void)
{
	vrxr_view_t eyes[2] = {
		{.left = -1.2f, .right = .8f, .down = -.7f, .up = 1.1f},
		{.left = -.9f, .right = 1.3f, .down = -1.0f, .up = .8f}
	};
	const float source[2] = {0, 0};
	float result[2], other[2], sentinel[2] = {23, 24};
	for (int eye = 0; eye < 2; ++eye)
	{
		vrxr_view_t *view = &eyes[eye];
		const float corners[4][2] = {
			{view->left, view->up}, {view->right, view->up},
			{view->left, view->down}, {view->right, view->down}
		};
		const float expected[4][2] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
		for (int corner = 0; corner < 4; ++corner)
		{
			assert (VRXR_ProjectHiddenAreaVertex (view, corners[corner], result));
			assert (fabsf (result[0] - expected[corner][0]) < .000001f);
			assert (fabsf (result[1] - expected[corner][1]) < .000001f);
		}
		assert (VRXR_ProjectHiddenAreaVertex (view, source, result));
		const double direct_x = -((double)view->right + view->left) /
			((double)view->right - view->left);
		const double direct_y = ((double)view->up + view->down) /
			((double)view->up - view->down);
		assert (fabs (result[0] - direct_x) < .000001);
		assert (fabs (result[1] - direct_y) < .000001);
		if (!eye)
			memcpy (other, result, sizeof other);
		else
			assert (fabsf (result[0] - other[0]) > .1f);
	}
	memcpy (result, sentinel, sizeof result);
	eyes[0].up = NAN;
	assert (!VRXR_ProjectHiddenAreaVertex (&eyes[0], source, result));
	assert (!memcmp (result, sentinel, sizeof result));
	eyes[0].up = 1.1f;
	eyes[0].right = eyes[0].left;
	assert (!VRXR_ProjectHiddenAreaVertex (&eyes[0], source, result));
	assert (!memcmp (result, sentinel, sizeof result));
	eyes[0].right = .8f;
	const float invalid[2] = {INFINITY, 0};
	assert (!VRXR_ProjectHiddenAreaVertex (&eyes[0], invalid, result));
	assert (!VRXR_ProjectHiddenAreaVertex (NULL, source, result));
	assert (!VRXR_ProjectHiddenAreaVertex (&eyes[0], NULL, result));
	assert (!VRXR_ProjectHiddenAreaVertex (&eyes[0], source, NULL));
	assert (!memcmp (result, sentinel, sizeof result));
}

static void check_hidden_area_packing (void)
{
	const vrxr_view_t eyes[2] = {
		{.left = -1, .right = 1, .down = -1, .up = 1},
		{.left = -.8f, .right = 1.2f, .down = -1, .up = 1}
	};
	const float left[6] = {-1, 1, 1, 1, 0, -1};
	const float right[12] = {-1, 1, 1, 1, 0, -1,
		-.8f, -1, 1.2f, -1, 0, 1};
	const float *source[2] = {left, right};
	const uint32_t triangles[2] = {1, 2};
	float vertices[6][4] = {{0}};
	assert (VRXR_PackHiddenAreaVertices (eyes, source, triangles, vertices, 6) == 6);
	for (int i = 3; i < 6; ++i)
	{
		assert (vertices[i][0] == vertices[2][0]);
		assert (vertices[i][1] == vertices[2][1]);
	}
	assert (fabsf (vertices[3][2] + 1) < .000001f);
	assert (fabsf (vertices[3][3] - 1) < .000001f);
	assert (!VRXR_PackHiddenAreaVertices (eyes, source, triangles, vertices, 5));
	const uint32_t huge[2] = {UINT32_MAX, 1};
	assert (!VRXR_PackHiddenAreaVertices (eyes, source, huge, vertices, 6));
	const float *missing[2] = {left, NULL};
	assert (!VRXR_PackHiddenAreaVertices (eyes, missing, triangles, vertices, 6));
}
int main (void)
{
	check_hidden_area_projection ();
	check_hidden_area_packing ();
	vrxr_frame_t f = {0};
	f.devices[0].valid = 1;
	for (int config = 0; config < 4; ++config)
	{
		pose (f.devices[0].matrix, .17 * config, -.1 * config, 1.3, .9, -2.1);
		for (int eye = 0; eye < 2; ++eye)
		{
			vrxr_view_t *v = &f.views[eye];
			pose (v->matrix, .17 * config + (eye ? .07 : -.09), -.1 * config, 1.3 + (eye ? .033 : -.031), .91, -2.11);
			v->left = -1.1 + .03 * eye;
			v->right = .95 + .06 * eye;
			v->down = -.8;
			v->up = 1.2;
		}
		check (&f);
	}
	float output[2][16], sentinel[2][16];
	memset (sentinel, 0x5a, sizeof sentinel);
	memcpy (output, sentinel, sizeof output);
	f.views[1].up = NAN;
	assert (!VRXR_StereoClip (&f, 26, 4, output));
	assert (!memcmp (output, sentinel, sizeof output));
	f.views[1].up = 1;
	f.devices[0].valid = 0;
	assert (!VRXR_StereoClip (&f, 26, 4, output));
	f.devices[0].valid = 1;
	assert (!VRXR_StereoClip (&f, 0, 4, output));
	assert (!VRXR_StereoClip (&f, 26, 0, output));
	assert (!VRXR_StereoClip (NULL, 26, 4, output));
	assert (!VRXR_StereoClip (&f, 26, 4, NULL));
	puts ("OpenXR clip correction agrees with independent direct eye projection");
}
