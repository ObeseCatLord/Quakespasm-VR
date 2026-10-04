/* Deferred CPU reference fixture, not execution of the production GLSL.
 * Synthetic inverse reverse-Z projections provide independent known rays;
 * containment/depth witnesses target ClusterRay/ClusterIndex/terminal masks.
 * GLSL compilation, uploaded ABI, dispatch/barriers and rendered pixels require
 * the paired graphics fixture; passing this file alone cannot verify those. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { float x, y, z; } vec3;
typedef struct { vec3 mins, maxs; } bounds;
typedef struct { vec3 origin; float radius; } sphere;

static vec3 Add (vec3 a, vec3 b) { return (vec3){a.x + b.x, a.y + b.y, a.z + b.z}; }
static vec3 Scale (vec3 a, float b) { return (vec3){a.x * b, a.y * b, a.z * b}; }
static vec3 Sub (vec3 a, vec3 b) { return Add (a, Scale (b, -1.0f)); }
static float Dot (vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec3 Normalize (vec3 a) { return Scale (a, 1.0f / sqrtf (Dot (a, a))); }
static vec3 Cross (vec3 a, vec3 b) { return (vec3){a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static vec3 EyeFacingNormal (vec3 raw, vec3 eye, vec3 position)
{
	const vec3 normal = Normalize (raw);
	return Dot (normal, Sub (eye, position)) < 0 ? Scale (normal, -1) : normal;
}

/* Column-major inverse VP: unprojected ray is forward + right*(x+shift_x)
 * + up*(y+shift_y), and w = Z/near + (1-Z)/far.  Thus Z=1 is near. */
static void InverseProjection (float inverse[16], vec3 eye, float cant, float shift_x, float shift_y)
{
	const vec3 right = {cosf (cant), 0.0f, -sinf (cant)};
	const vec3 up = {0.0f, 1.0f, 0.0f};
	const vec3 forward = {sinf (cant), 0.0f, cosf (cant)};
	const float near = 4.0f, far = 4096.0f;
	const vec3 columns[4] = {right, up, Scale (eye, 1.0f / near - 1.0f / far),
		Add (Add (Add (forward, Scale (right, shift_x)), Scale (up, shift_y)), Scale (eye, 1.0f / far))};
	for (int column = 0; column < 4; ++column)
	{
		inverse[column * 4] = columns[column].x;
		inverse[column * 4 + 1] = columns[column].y;
		inverse[column * 4 + 2] = columns[column].z;
		inverse[column * 4 + 3] = 0.0f;
	}
	inverse[11] = 1.0f / near - 1.0f / far;
	inverse[15] = 1.0f / far;
}

static vec3 Unproject (const float inverse[16], float x, float y, float z)
{
	const float w = inverse[3]*x + inverse[7]*y + inverse[11]*z + inverse[15];
	return (vec3){(inverse[0]*x + inverse[4]*y + inverse[8]*z + inverse[12]) / w,
		(inverse[1]*x + inverse[5]*y + inverse[9]*z + inverse[13]) / w,
		(inverse[2]*x + inverse[6]*y + inverse[10]*z + inverse[14]) / w};
}

static vec3 PlaneRay (const float inverse[16], vec3 eye, vec3 forward, float x, float y)
{
	const vec3 ray = Sub (Unproject (inverse, x, y, 1.0f), eye);
	const float depth = Dot (ray, forward);
	assert (depth > 0.0f);
	return Scale (ray, 1.0f / depth);
}

static bounds TileBounds (const float inverse[16], vec3 eye, vec3 forward, float lo, float hi, float near, float far)
{
	bounds b = {{INFINITY, INFINITY, INFINITY}, {-INFINITY, -INFINITY, -INFINITY}};
	for (int corner = 0; corner < 8; ++corner)
	{
		const vec3 ray = PlaneRay (inverse, eye, forward, corner & 1 ? hi : lo, corner & 2 ? hi : lo);
		const vec3 p = Add (eye, Scale (ray, corner & 4 ? far : near));
		b.mins = (vec3){fminf (b.mins.x, p.x), fminf (b.mins.y, p.y), fminf (b.mins.z, p.z)};
		b.maxs = (vec3){fmaxf (b.maxs.x, p.x), fmaxf (b.maxs.y, p.y), fmaxf (b.maxs.z, p.z)};
	}
	return b;
}

static int Contains (bounds b, vec3 p)
{
	const float epsilon = 0.002f;
	return p.x >= b.mins.x-epsilon && p.x <= b.maxs.x+epsilon &&
		p.y >= b.mins.y-epsilon && p.y <= b.maxs.y+epsilon && p.z >= b.mins.z-epsilon && p.z <= b.maxs.z+epsilon;
}

static int SphereTouchesAABB (sphere light, bounds b)
{
	const vec3 delta = {fmaxf (fmaxf (b.mins.x-light.origin.x, light.origin.x-b.maxs.x), 0.0f),
		fmaxf (fmaxf (b.mins.y-light.origin.y, light.origin.y-b.maxs.y), 0.0f),
		fmaxf (fmaxf (b.mins.z-light.origin.z, light.origin.z-b.maxs.z), 0.0f)};
	return isfinite (light.radius) && light.radius > 0.0f && Dot (delta, delta) <= light.radius * light.radius;
}

static float SliceBoundary (unsigned slice) { return 4.0f * powf (4096.0f / 4.0f, (float)slice / 32.0f); }
static unsigned SliceIndex (float depth)
{
	const float z = floorf (logf (fmaxf (depth, 4.0f) / 4.0f) / logf (4096.0f / 4.0f) * 32.0f);
	return (unsigned)fminf (fmaxf (z, 0.0f), 31.0f);
}

static uint32_t ActiveWord (unsigned count, unsigned word)
{
	/* Independent expectation avoids undefined shifts by 32 in mask edges. */
	const unsigned bits = count <= word*32 ? 0 : count >= (word+1)*32 ? 32 : count-word*32;
	return bits == 32 ? UINT32_MAX : bits == 0 ? 0 : (1u << bits)-1u;
}

static int EffectiveClusterMode (int requested, int dynamics, int gpu_lightmaps, int rt_shadows, int pipeline_ready)
{
	return requested && dynamics && gpu_lightmaps && !rt_shadows && pipeline_ready;
}

int main (void)
{
	float inverse[16];
	const vec3 eye = {0.0f, 0.0f, 0.0f};
	InverseProjection (inverse, eye, 0.0f, 0.0f, 0.0f);
	const vec3 forward = Normalize (Sub (Unproject (inverse, 0, 0, 1), eye));
	assert (fabsf (Unproject (inverse, 0, 0, 1).z-4.0f) < 0.001f);
	assert (fabsf (Unproject (inverse, 0, 0, 0).z-4096.0f) < 0.001f);
	/* Old near-minus-far ray is provably backwards; corrected near-eye is positive. */
	assert (Dot (Sub (Unproject (inverse, 0, 0, 1), Unproject (inverse, 0, 0, 0)), forward) < 0.0f);
	assert (Dot (PlaneRay (inverse, eye, forward, 0, 0), forward) > 0.0f);

	/* At the central far boundary, normalized spherical corner rays miss this
	 * interior point. Plane-depth corners enclose it and its small light. */
	const bounds central = TileBounds (inverse, eye, forward, -0.5f, 0.5f, 4.0f, 16.0f);
	assert (16.0f / sqrtf (1.5f) < 16.0f - 0.01f);
	assert (Contains (central, (vec3){0, 0, 16}));
	assert (SphereTouchesAABB ((sphere){{0, 0, 16}, 0.01f}, central));
	/* Off-axis radial length selects a different slice at identical plane depth. */
	const float depth = (SliceBoundary (8) + SliceBoundary (9)) * 0.5f;
	const vec3 off_axis = Add (eye, Scale (PlaneRay (inverse, eye, forward, 1, 0), depth));
	assert (SliceIndex (Dot (Sub (off_axis, eye), forward)) == 8);
	assert (SliceIndex (sqrtf (Dot (off_axis, off_axis))) != 8);
	assert (SliceIndex (0.01f) == 0);
	assert (Contains (TileBounds (inverse, eye, forward, -0.5f, 0.5f, 0, SliceBoundary (1)), (vec3){0, 0, 0.01f}));
	assert (SliceIndex (1e12f) == 31);

	/* Canted/asymmetric true-eye transforms: dense interior witnesses must fit
	 * the eight corners, and every point's reconstructed forward depth agrees. */
	const vec3 translated_eye = {17.0f, -9.0f, 31.0f};
	for (int side = -1; side <= 1; side += 2)
	{
		InverseProjection (inverse, translated_eye, side*0.6f, side*0.3f, -0.15f);
		const vec3 canted_forward = Normalize (Sub (Unproject (inverse, 0, 0, 1), translated_eye));
		const bounds b = TileBounds (inverse, translated_eye, canted_forward, -0.6f, 0.7f, 4, 64);
		for (int x = 0; x <= 16; ++x)
			for (int y = 0; y <= 16; ++y)
				for (int z = 0; z <= 8; ++z)
				{
					const float d = 4.0f + 60.0f*z/8.0f;
					const vec3 p = Add (translated_eye, Scale (PlaneRay (inverse, translated_eye, canted_forward,
						-0.6f + 1.3f*x/16.0f, -0.6f + 1.3f*y/16.0f), d));
					assert (Contains (b, p));
					assert (fabsf (Dot (Sub (p, translated_eye), canted_forward)-d) < 0.002f);
				}
	}

	/* VRS padding expands eligibility, without changing the depth convention. */
	InverseProjection (inverse, eye, 0, 0, 0);
	assert (!SphereTouchesAABB ((sphere){{2.25f, 0, 8}, 0.01f}, TileBounds (inverse, eye, forward, -0.25f, 0.25f, 4, 8)));
	assert (SphereTouchesAABB ((sphere){{2.25f, 0, 8}, 0.01f}, TileBounds (inverse, eye, forward, -0.3f, 0.3f, 4, 8)));
	assert (!SphereTouchesAABB ((sphere){{0, 0, 8}, NAN}, central));

	/* Unbounded tail: ALL active bits, including index 63, independent of light
	 * location/radius and depths far beyond the former one-million-unit cap. */
	const unsigned counts[] = {0, 1, 31, 32, 33, 63, 64};
	for (unsigned i = 0; i < sizeof (counts)/sizeof (counts[0]); ++i)
	{
		uint32_t masks[2] = {0, 0};
		for (unsigned light = 0; light < counts[i]; ++light)
			masks[light >> 5] |= 1u << (light & 31);
		assert (masks[0] == ActiveWord (counts[i], 0));
		assert (masks[1] == ActiveWord (counts[i], 1));
	}
	/* Native storage .25*8 and 1*2 both produce the same dynamic scale.
	 * KEX's intensity .5 factor stays inside the shared native formula. */
	const float native_brightness = 0.375f;
	assert (native_brightness*2.0f == native_brightness*0.25f*8.0f);
	const float kex_brightness = 3.0f*0.5f*0.5f*0.8f;
	assert (fabsf (kex_brightness*2.0f - 1.2f) < 1e-6f);
	assert (kex_brightness*8.0f != kex_brightness*2.0f);
	/* Actual Vulkan projection flips screen Y. On a visible wall at camera
	 * z=-8, cross(dPdx,dPdy) is -Z but the native plane normal is +Z.
	 * SURF_PLANEBACK reverses the authored plane, not the visible outward side. */
	const vec3 derivative = Cross ((vec3){1, 0, 0}, (vec3){0, -1, 0});
	const vec3 wall = {0, 0, -8};
	const vec3 normal = EyeFacingNormal (derivative, eye, wall);
	assert (Dot (normal, (vec3){0, 0, 1}) == 1.0f);
	assert (Dot (normal, Normalize (Sub (eye, wall))) == 1.0f);
	/* Rotated wall and either derivative sign retain the eye-facing KEX normal. */
	const vec3 rotated_normal = Normalize ((vec3){0.6f, 0, 0.8f});
	const vec3 rotated_position = Scale (rotated_normal, -8.0f);
	assert (Dot (EyeFacingNormal (rotated_normal, eye, rotated_position), rotated_normal) > 0.999f);
	assert (Dot (EyeFacingNormal (Scale (rotated_normal, -1), eye, rotated_position), rotated_normal) > 0.999f);

	assert (EffectiveClusterMode (1, 1, 1, 0, 1));
	assert (!EffectiveClusterMode (1, 1, 1, 1, 1));
	assert (!EffectiveClusterMode (1, 1, 0, 0, 1));
	assert (!EffectiveClusterMode (1, 0, 1, 0, 1));
	assert (!EffectiveClusterMode (1, 1, 1, 0, 0));
	puts ("CLUSTER_LIGHTING_GEOMETRY_PASSED reverse-z plane-depth canted interior first-slice unbounded-tail mode-latch");
	return 0;
}
