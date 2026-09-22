/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2021 QuakeSpasm developers
Copyright (C) 2016 Dominic Szablewski - phoboslab.org

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/* Pure arithmetic ported from QuakeSpasm OpenVR's VR_Move/controller-angle
 * path. Rotation-matrix conversion follows its GPL mathlib.c helpers; native
 * QuakeSpasm AngleVectors and R_ConcatRotations remain the shared primitives. */

#include "vr_locomotion.h"

#include "quakedef.h"
#include "vr_aim.h"

#define VR_LOCOMOTION_DENOM_EPSILON 0.000001f

static void VR_LocomotionZero (float out[3])
{
	out[0] = 0.0f;
	out[1] = 0.0f;
	out[2] = 0.0f;
}

static qboolean VR_LocomotionFiniteVec3 (const float value[3])
{
	return value && isfinite (value[0]) && isfinite (value[1]) && isfinite (value[2]);
}

static void VR_LocomotionRotMatFromAngles (const vec3_t angles, float mat[3][3])
{
	vec3_t mutable_angles;

	VectorCopy (angles, mutable_angles);
	AngleVectors (mutable_angles, mat[0], mat[1], mat[2]);

	/* Legacy helper flips Quake's right vector so zero angles are identity. */
	mat[1][0] *= -1.0f;
	mat[1][1] *= -1.0f;
	mat[1][2] *= -1.0f;
}

static qboolean VR_LocomotionAnglesFromRotMat (float mat[3][3], vec3_t angles)
{
	vec3_t unrolled;
	float  unrolled_mat[3][3];

	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			if (!isfinite (mat[row][column]))
				return false;

	angles[VR_AIM_YAW] = (float)(-atan2 (mat[0][0], mat[0][1]) / M_PI_DIV_180 + 90.0);
	angles[VR_AIM_PITCH] = (float)(atan2 (sqrt (mat[0][0] * mat[0][0] + mat[0][1] * mat[0][1]),
		mat[0][2]) / M_PI_DIV_180 - 90.0);
	angles[VR_AIM_ROLL] = 0.0f;

	VectorCopy (angles, unrolled);
	VR_LocomotionRotMatFromAngles (unrolled, unrolled_mat);
	angles[VR_AIM_ROLL] = (float)(-atan2 (DotProduct (unrolled_mat[1], mat[1]),
		DotProduct (unrolled_mat[2], mat[1])) / M_PI_DIV_180 + 90.0);

	return VR_LocomotionFiniteVec3 (angles);
}

qboolean VR_LocomotionHandAngles (const float matrix[3][4], float tracking_yaw,
	float gun_angle, float out[3])
{
	vec3_t controller_angles, gun_angles = {0.0f, 0.0f, 0.0f}, result;
	float  controller_mat[3][3], gun_mat[3][3], combined[3][3];

	if (!out)
		return false;
	if (!isfinite (tracking_yaw) || !isfinite (gun_angle) ||
		!VR_AimPoseAngles (matrix, tracking_yaw, controller_angles))
		goto fail;

	gun_angles[VR_AIM_PITCH] = gun_angle;
	VR_LocomotionRotMatFromAngles (gun_angles, gun_mat);
	VR_LocomotionRotMatFromAngles (controller_angles, controller_mat);
	R_ConcatRotations (gun_mat, controller_mat, combined);
	if (!VR_LocomotionAnglesFromRotMat (combined, result))
		goto fail;

	VectorCopy (result, out);
	return true;

fail:
	VR_LocomotionZero (out);
	return false;
}

qboolean VR_LocomotionAimOffsetToWorld (const float local[3],
	const float angles[3], float scale, float world[3])
{
	vec3_t local_copy, angles_copy;
	vec3_t forward, right, up;
	vec3_t mutable_angles;
	vec3_t result;

	if (!world)
		return false;
	if (local)
		VectorCopy (local, local_copy);
	if (angles)
		VectorCopy (angles, angles_copy);
	VR_LocomotionZero (world);
	if (!local || !angles || !VR_LocomotionFiniteVec3 (local_copy) ||
		!VR_LocomotionFiniteVec3 (angles_copy) || !isfinite (scale))
		return false;

	VectorCopy (angles_copy, mutable_angles);
	AngleVectors (mutable_angles, forward, right, up);
	result[0] = (right[0] * local_copy[0] + up[0] * local_copy[1] +
		forward[0] * local_copy[2]) * scale;
	result[1] = (right[1] * local_copy[0] + up[1] * local_copy[1] +
		forward[1] * local_copy[2]) * scale;
	result[2] = (right[2] * local_copy[0] + up[2] * local_copy[1] +
		forward[2] * local_copy[2]) * scale;
	if (!VR_LocomotionFiniteVec3 (result))
		return false;

	VectorCopy (result, world);
	return true;
}

qboolean VR_LocomotionHandBodyOffset (const float head[3], const float hand[3],
	float tracking_yaw, float units_per_metre, float head_eye_height, float out[3])
{
	vec3_t head_copy, hand_copy, result;
	float local_forward, local_side, yaw_radians, yaw_sin, yaw_cos;

	if (!out)
		return false;
	if (head)
		VectorCopy (head, head_copy);
	if (hand)
		VectorCopy (hand, hand_copy);
	VR_LocomotionZero (out);
	if (!head || !hand || !VR_LocomotionFiniteVec3 (head_copy) ||
		!VR_LocomotionFiniteVec3 (hand_copy) || !isfinite (tracking_yaw) ||
		!isfinite (units_per_metre) || units_per_metre <= 0.0f ||
		!isfinite (head_eye_height))
		return false;

	local_forward = -(hand_copy[2] - head_copy[2]) * units_per_metre;
	local_side = -(hand_copy[0] - head_copy[0]) * units_per_metre;
	yaw_radians = tracking_yaw * M_PI_DIV_180;
	yaw_sin = sinf (yaw_radians);
	yaw_cos = cosf (yaw_radians);
	result[0] = local_forward * yaw_cos - local_side * yaw_sin;
	result[1] = local_forward * yaw_sin + local_side * yaw_cos;
	result[2] = head_eye_height + (hand_copy[1] - head_copy[1]) * units_per_metre;
	if (!VR_LocomotionFiniteVec3 (result))
		return false;

	VectorCopy (result, out);
	return true;
}

qboolean VR_LocomotionMove (int mode, const float head[3], const float offhand[3],
	float forward_axis, float side_axis, float forward_speed, float up_speed,
	float out[3])
{
	const float *selected;
	vec3_t selected_angles, lfwd, lright, lup, result = {0.0f, 0.0f, 0.0f};
	float original_forward_z;

	if (!out)
		return false;
	if (mode != VR_MOVEMENT_MODE_FOLLOW_HEAD &&
		mode != VR_MOVEMENT_MODE_FOLLOW_HAND &&
		mode != VR_MOVEMENT_MODE_RAW_INPUT)
		goto fail;
	if (!isfinite (forward_axis) || !isfinite (side_axis) ||
		!isfinite (forward_speed) || !isfinite (up_speed))
		goto fail;

	selected = (mode == VR_MOVEMENT_MODE_FOLLOW_HEAD) ? head : offhand;
	if (!VR_LocomotionFiniteVec3 (selected))
		goto fail;
	VectorCopy (selected, selected_angles);
	AngleVectors (selected_angles, lfwd, lright, lup);
	if (!VR_LocomotionFiniteVec3 (lfwd) || !VR_LocomotionFiniteVec3 (lright) ||
		!VR_LocomotionFiniteVec3 (lup))
		goto fail;
	original_forward_z = lfwd[2];

	if (mode == VR_MOVEMENT_MODE_RAW_INPUT)
	{
		result[0] = forward_speed * forward_axis;
		result[1] = forward_speed * side_axis;
	}
	else
	{
		vec3_t vfwd, vright, vup;
		vec3_t player_yaw_only = {0.0f, selected_angles[VR_AIM_YAW], 0.0f};
		vec3_t move = {0.0f, 0.0f, 0.0f};
		float  factor, projected_forward, projected_right;

		AngleVectors (player_yaw_only, vfwd, vright, vup);

		if (fabsf (lfwd[2]) > 0.8f)
		{
			vec3_t swap;

			if (lfwd[2] < -0.8f)
			{
				lfwd[0] *= -1.0f;
				lfwd[1] *= -1.0f;
				lfwd[2] *= -1.0f;
			}
			else
			{
				lup[0] *= -1.0f;
				lup[1] *= -1.0f;
				lup[2] *= -1.0f;
			}

			VectorCopy (lup, swap);
			VectorCopy (lfwd, lup);
			VectorCopy (swap, lfwd);
		}

		if (!isfinite (lup[2]) || fabsf (lup[2]) <= VR_LOCOMOTION_DENOM_EPSILON)
			goto fail;
		factor = 1.0f / lup[2];
		for (int component = 0; component < 3; ++component)
		{
			lfwd[component] *= factor;
			lright[component] *= factor;
		}

		VectorMA (move, forward_axis, lfwd, move);
		VectorMA (move, side_axis, lright, move);
		projected_forward = DotProduct (move, vfwd);
		projected_right = DotProduct (move, vright);
		result[0] = forward_speed * projected_forward;
		result[1] = forward_speed * projected_right;
	}

	result[2] = up_speed * forward_axis * original_forward_z;
	if (!VR_LocomotionFiniteVec3 (result))
		goto fail;

	VectorCopy (result, out);
	return true;

fail:
	VR_LocomotionZero (out);
	return false;
}
