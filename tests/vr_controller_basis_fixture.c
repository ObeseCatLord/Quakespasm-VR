/* Per-profile controller basis. Links production vr_locomotion.c and native
 * mathlib.c with function/data GC; VRXR_HandAimToGrip is the production
 * header the OpenXR backend applies to tracked hands.
 *
 * Every profile that delivers an OpenXR grip pose must land in the Touch
 * basis (the grip itself); Index keeps its qualified conversion to the
 * OpenVR controller pose; a tracked hand's aim ray, after the default
 * vr_gunangle, must leave the weapon along the ray. */
#include "../Quake/vr_locomotion.h"
#include "../Quake/vr_aim.h"
#include "../Quake/vr_openxr_math.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_GUN_ANGLE 32.0f

static void multiply (const double a[3][3], const double b[3][3], double out[3][3])
{
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 3; ++c)
		{
			out[r][c] = 0;
			for (int k = 0; k < 3; ++k) out[r][c] += a[r][k] * b[k][c];
		}
}

/* Rz*Ry*Rx, degrees. */
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

static vrxr_frame_t frame_with (int hand, int profile, const double rotation[3][3])
{
	vrxr_frame_t frame;
	const float position[3] = {.2f, 1.3f, -.4f};
	memset (&frame, 0, sizeof (frame));
	vrxr_device_t *device = &frame.devices[hand + 1];
	device->valid = device->tracked = device->connected = 1;
	device->velocity_valid = device->angular_velocity_valid = 1;
	device->kind = VRXR_DEVICE_HAND;
	device->hand = hand;
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c) device->matrix[r][c] = (float)rotation[r][c];
		device->matrix[r][3] = position[r];
		device->velocity[r] = .1f * (r + 1);
		device->angular_velocity[r] = -.3f * (r + 1);
	}
	frame.hands[hand].profile = profile;
	return frame;
}

/* -Z of the converted pose: the controller's forward axis in XR space. */
static void forward_of (const vrxr_device_t *device, double out[3])
{
	for (int r = 0; r < 3; ++r) out[r] = -device->matrix[r][2];
}

static double angle_between (const double a[3], const double b[3])
{
	double dot = 0, la = 0, lb = 0;
	for (int i = 0; i < 3; ++i) { dot += a[i]*b[i]; la += a[i]*a[i]; lb += b[i]*b[i]; }
	dot /= sqrt (la * lb);
	if (dot > 1) dot = 1;
	if (dot < -1) dot = -1;
	return acos (dot) * 180.0 / acos (-1.0);
}

static double angle_delta (double a, double b)
{
	double d = fmod (a - b, 360.0);
	if (d > 180) d -= 360;
	if (d < -180) d += 360;
	return fabs (d);
}

static void check_grip_profiles (int hand, int sample)
{
	/* Spread orientations, avoiding gimbal-lock pitches for the angle checks. */
	double grip[3][3];
	euler (sample * 9.0 - 40.0, sample * 23.0 - 170.0, sample * -7.0 + 30.0, grip);
	vrxr_frame_t touch_frame = frame_with (hand, VRXR_PROFILE_TOUCH, grip);
	vrxr_device_t touch, out;
	double touch_forward[3], forward[3];
	float touch_angles[3], angles[3];

	assert (VR_LocomotionControllerDevice (&touch_frame, hand, &touch));
	/* Touch is the verified grip basis: an exact passthrough. */
	assert (!memcmp (&touch, &touch_frame.devices[hand + 1], sizeof (touch)));
	forward_of (&touch, touch_forward);
	assert (VR_LocomotionHandAngles (touch.matrix, 0, DEFAULT_GUN_ANGLE, touch_angles));

	/* Every grip-delivering profile (SIMPLE covers khr/simple and unknown
	 * profiles; TOUCH also covers Touch Plus/Pro; GENERIC covers WMR, HP,
	 * Odyssey, Cosmos, Focus 3 and Pico) shares the Touch result. */
	const int grip_profiles[] = {VRXR_PROFILE_SIMPLE, VRXR_PROFILE_TOUCH,
		VRXR_PROFILE_VIVE, VRXR_PROFILE_FRAME, VRXR_PROFILE_GENERIC};
	for (size_t i = 0; i < sizeof (grip_profiles) / sizeof (grip_profiles[0]); ++i)
	{
		vrxr_frame_t frame = frame_with (hand, grip_profiles[i], grip);
		assert (VR_LocomotionControllerDevice (&frame, hand, &out));
		forward_of (&out, forward);
		assert (angle_between (forward, touch_forward) < 1e-4);
		assert (VR_LocomotionHandAngles (out.matrix, 0, DEFAULT_GUN_ANGLE, angles));
		for (int a = 0; a < 3; ++a) assert (angle_delta (angles[a], touch_angles[a]) < 1e-3);
		for (int r = 0; r < 3; ++r) assert (out.matrix[r][3] == touch.matrix[r][3]);
	}

	/* Index keeps Monado's qualified grip offset: about 15.5 degrees from the
	 * grip forward (15.392 pitch, -2.071 yaw), the same for both hands. */
	{
		vrxr_frame_t frame = frame_with (hand, VRXR_PROFILE_INDEX, grip);
		assert (VR_LocomotionControllerDevice (&frame, hand, &out));
		forward_of (&out, forward);
		const double index_offset = angle_between (forward, touch_forward);
		assert (index_offset > 15.0 && index_offset < 16.0);
	}
}

static void check_hand_profile (int hand, int sample)
{
	/* A tracked hand's aim ray: -Z is where the hand points. */
	double aim[3][3];
	euler (sample * 11.0 - 60.0, sample * 29.0 - 170.0, sample * -13.0 + 40.0, aim);
	vrxr_frame_t frame = frame_with (hand, VRXR_PROFILE_HAND, aim);
	const vrxr_frame_t located = frame;
	vrxr_device_t out;
	float ray_angles[3], weapon_angles[3];

	/* Velocities survive without angular velocity: no lever to add. */
	frame.devices[hand + 1].angular_velocity_valid = sample & 1;
	VRXR_HandAimToGrip (&frame.devices[hand + 1]);
	assert (VR_LocomotionControllerDevice (&frame, hand, &out));
	assert (!memcmp (&out, &frame.devices[hand + 1], sizeof (out)));
	assert (out.valid && out.tracked && out.velocity_valid);
	/* Pure rotation: position and velocity are untouched. */
	for (int r = 0; r < 3; ++r)
	{
		assert (out.matrix[r][3] == located.devices[hand + 1].matrix[r][3]);
		assert (out.velocity[r] == located.devices[hand + 1].velocity[r]);
	}
	/* The default gun angle brings the weapon back onto the ray. */
	assert (VR_AimPoseAngles (located.devices[hand + 1].matrix, 0, ray_angles));
	assert (VR_LocomotionHandAngles (out.matrix, 0, DEFAULT_GUN_ANGLE, weapon_angles));
	assert (angle_delta (weapon_angles[VR_AIM_PITCH], ray_angles[VR_AIM_PITCH]) < .01);
	assert (angle_delta (weapon_angles[VR_AIM_YAW], ray_angles[VR_AIM_YAW]) < .01);
	assert (angle_delta (weapon_angles[VR_AIM_ROLL], ray_angles[VR_AIM_ROLL]) < .01);
	/* The converted basis matches a controller grip held the same way: pitched
	 * up by the default gun angle about the hand's own X axis. */
	{
		double forward[3], ray[3];
		forward_of (&out, forward);
		for (int r = 0; r < 3; ++r) ray[r] = -aim[r][2];
		assert (fabs (angle_between (forward, ray) - DEFAULT_GUN_ANGLE) < 1e-3);
		double up_dot = 0;
		for (int r = 0; r < 3; ++r) up_dot += forward[r] * aim[r][1];
		assert (up_dot > 0);
	}
}

/* The Quest 3 report: weapons straight up or down. A bare-hand grip (what a
 * runtime hands over as khr/simple when no hand profile is suggested) has -Z
 * along the knuckle line; with the hand pointing level, that is vertical. */
static void check_bare_hand_grip_regression (void)
{
	/* Hand pointing level at -Z, palm facing +X: the knuckle line points up.
	 * Grip axes: -Z up, +Y forward (thumb side towards the ray), X across. */
	const double grip[3][3] = {{1,0,0},{0,0,-1},{0,1,0}};
	const double aim[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
	vrxr_device_t out;
	float angles[3];

	vrxr_frame_t simple = frame_with (1, VRXR_PROFILE_SIMPLE, grip);
	assert (VR_LocomotionControllerDevice (&simple, 1, &out));
	assert (VR_LocomotionHandAngles (out.matrix, 0, DEFAULT_GUN_ANGLE, angles));
	assert (fabs (angles[VR_AIM_PITCH]) > 50.0);

	vrxr_frame_t hand = frame_with (1, VRXR_PROFILE_HAND, aim);
	VRXR_HandAimToGrip (&hand.devices[2]);
	assert (VR_LocomotionControllerDevice (&hand, 1, &out));
	assert (VR_LocomotionHandAngles (out.matrix, 0, DEFAULT_GUN_ANGLE, angles));
	assert (fabs (angles[VR_AIM_PITCH]) < .01);
	assert (angle_delta (angles[VR_AIM_YAW], 0) < .01 ||
		angle_delta (angles[VR_AIM_YAW], 90) < .01);
}

int main (void)
{
	for (int hand = 0; hand < 2; ++hand)
		for (int sample = 0; sample < 16; ++sample)
		{
			check_grip_profiles (hand, sample);
			check_hand_profile (hand, sample);
		}
	check_bare_hand_grip_regression ();
	puts ("Controller basis: grip profiles share Touch, Index offset kept, hand ray aligned");
	return 0;
}
