/* Link production vr_locomotion.c and native mathlib.c with function/data GC.
 * Oracle independently composes double-precision Monado Euler rotations. */
#include "../Quake/vr_locomotion.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void multiply (const double a[3][3], const double b[3][3], double out[3][3])
{
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
		{
			out[r][c] = 0;
			for (int k = 0; k < 3; ++k) out[r][c] += a[r][k] * b[k][c];
		}
}

static void euler (double x, double y, double z, double out[3][3])
{
	const double d = acos (-1.0) / 180.0;
	x *= d; y *= d; z *= d;
	const double rx[3][3] = {{1,0,0},{0,cos(x),-sin(x)},{0,sin(x),cos(x)}};
	const double ry[3][3] = {{cos(y),0,sin(y)},{0,1,0},{-sin(y),0,cos(y)}};
	const double rz[3][3] = {{cos(z),-sin(z),0},{sin(z),cos(z),0},{0,0,1}};
	double yx[3][3];
	multiply (ry, rx, yx);
	multiply (rz, yx, out);
}

static void close_value (double actual, double expected, double tolerance)
{
	assert (fabs (actual - expected) < tolerance);
}

static void oracle_position (const vrxr_device_t *grip, int hand, double out[3])
{
	double c[3][3];
	const double t[3] = {0, -.015, .13};
	euler (15.392, hand ? 2.071 : -2.071, hand ? -.303 : .303, c);
	for (int r = 0; r < 3; ++r)
	{
		out[r] = grip->matrix[r][3];
		for (int j = 0; j < 3; ++j)
			for (int k = 0; k < 3; ++k)
				out[r] -= grip->matrix[r][k] * c[j][k] * t[j];
	}
}

static void check_hand (int hand, int sample)
{
	vrxr_frame_t frame = {0}, saved;
	vrxr_device_t controller, advanced, out;
	double c[3][3], raw[3][3], grip[3][3], t[3] = {0,-.015,.13};
	double p[3] = {.7,-.2,1.1}, lever[3];
	const double linear[3] = {.3,-.6,.2}, omega[3] = {.8,-1.2,.4};
	euler (15.392, hand ? 2.071 : -2.071, hand ? -.303 : .303, c);
	euler (sample * 7.0, sample * -11.0, sample * 17.0, raw);
	multiply (raw, c, grip);
	controller = (vrxr_device_t){.valid=1,.tracked=1,.connected=1,
		.velocity_valid=1,.angular_velocity_valid=1,.kind=VRXR_DEVICE_HAND,.hand=hand};
	strcpy (controller.serial, "fixture-index");
	for (int r = 0; r < 3; ++r)
	{
		lever[r] = 0;
		for (int k = 0; k < 3; ++k)
		{
			controller.matrix[r][k] = (float)grip[r][k];
			lever[r] -= raw[r][k] * t[k];
		}
		controller.matrix[r][3] = (float)(p[r] - lever[r]);
		controller.angular_velocity[r] = (float)omega[r];
	}
	for (int r = 0; r < 3; ++r)
	{
		const int a=(r+1)%3, b=(r+2)%3;
		controller.velocity[r] = (float)(linear[r] - omega[a]*lever[b] + omega[b]*lever[a]);
	}
	frame.devices[hand+1] = controller;
	frame.hands[hand].profile = VRXR_PROFILE_INDEX;
	saved = frame;
	assert (VR_LocomotionControllerDevice (&frame, hand, &out));
	assert (!memcmp (&frame, &saved, sizeof (frame)));
	assert (out.valid && out.velocity_valid && out.tracked && out.connected);
	assert (!strcmp (out.serial, controller.serial));
	for (int r = 0; r < 3; ++r)
	{
		close_value (out.matrix[r][3], p[r], 0.000002);
		close_value (out.velocity[r], linear[r], 0.000002);
		close_value (out.angular_velocity[r], omega[r], 0.000002);
		for (int k = 0; k < 3; ++k) close_value (out.matrix[r][k], raw[r][k], 0.000002);
	}
	/* Advance the grip rotation about world omega and its reference-space
	 * position about vgrip; inverse-C position differences witness lever speed. */
	const double dt = .0005, magnitude = sqrt (2.24), theta = magnitude * dt;
	double axis[3], spin[3][3], advanced_rotation[3][3], before[3], after[3];
	for (int r = 0; r < 3; ++r) axis[r] = omega[r] / magnitude;
	for (int r = 0; r < 3; ++r)
		for (int k = 0; k < 3; ++k)
			spin[r][k] = (r==k ? cos(theta) : 0) + axis[r]*axis[k]*(1-cos(theta));
	spin[0][1] -= axis[2]*sin(theta); spin[0][2] += axis[1]*sin(theta);
	spin[1][0] += axis[2]*sin(theta); spin[1][2] -= axis[0]*sin(theta);
	spin[2][0] -= axis[1]*sin(theta); spin[2][1] += axis[0]*sin(theta);
	multiply (spin, grip, advanced_rotation);
	advanced = controller;
	for (int r = 0; r < 3; ++r)
	{
		advanced.matrix[r][3] += (float)(dt * controller.velocity[r]);
		for (int k = 0; k < 3; ++k) advanced.matrix[r][k] = (float)advanced_rotation[r][k];
	}
	oracle_position (&controller, hand, before);
	oracle_position (&advanced, hand, after);
	for (int r = 0; r < 3; ++r) close_value (out.velocity[r], (after[r]-before[r])/dt, .0005);
	frame.devices[hand+1].angular_velocity_valid = 0;
	assert (VR_LocomotionControllerDevice (&frame, hand, &out));
	assert (out.valid && !out.velocity_valid);
	frame.devices[hand+1].angular_velocity_valid = 1;
	frame.devices[hand+1].angular_velocity[0] = NAN;
	assert (VR_LocomotionControllerDevice (&frame, hand, &out));
	assert (out.valid && !out.velocity_valid);
	for (int profile=VRXR_PROFILE_SIMPLE; profile<=VRXR_PROFILE_FRAME; ++profile)
	{
		if (profile == VRXR_PROFILE_INDEX) continue;
		frame.hands[hand].profile = profile;
		assert (VR_LocomotionControllerDevice (&frame, hand, &out));
		assert (!memcmp (&out, &frame.devices[hand+1], sizeof (out)));
	}
	frame.hands[hand].profile = VRXR_PROFILE_INDEX;
	frame.devices[hand+1].matrix[0][0] = NAN;
	assert (!VR_LocomotionControllerDevice (&frame, hand, &out));
	for (int k=0; k<3; ++k) frame.devices[hand+1].matrix[0][k] = FLT_MAX;
	assert (!VR_LocomotionControllerDevice (&frame, hand, &out));
}

int main (void)
{
	for (int hand=0; hand<2; ++hand)
		for (int sample=0; sample<18; ++sample) check_hand (hand, sample);
	vrxr_device_t out;
	assert (!VR_LocomotionControllerDevice (NULL, 0, &out));
	puts ("Index inverse grip transform, lever velocity, finite differences and immutable passthrough passed");
	return 0;
}
