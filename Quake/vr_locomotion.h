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

#ifndef QUAKE_VR_LOCOMOTION_H
#define QUAKE_VR_LOCOMOTION_H

#include "q_stdinc.h"

#define VR_MOVEMENT_MODE_FOLLOW_HEAD 0
#define VR_MOVEMENT_MODE_FOLLOW_HAND 1
#define VR_MOVEMENT_MODE_RAW_INPUT 2

/* Convert a tracked controller pose to inherited Quake hand angles, including
 * the legacy vr_gunangle pitch transform. */
qboolean VR_LocomotionHandAngles (const float matrix[3][4], float tracking_yaw,
	float gun_angle, float out[3]);

/* Convert mapped hand angles to the held viewmodel convention. */
qboolean VR_LocomotionHandRotToViewmodelAngles (const float handrot[3],
	float viewmodel_angles[3], float gunmodelpitch);

/* Apply a per-hand held-model rotation correction to mapped hand angles.
 * The correction matrix is interpreted by columns; out is zeroed on failure. */
qboolean VR_LocomotionCorrectedViewmodelAngles (const float handrot[3],
	float gunmodelpitch, const float correction[3][3], float out[3]);

/* Roll the model about the tracked controller's forward axis, which differs
 * from the model's pitched local X. Signed roll preserves anatomical mirrors. */
qboolean VR_LocomotionControllerRollViewmodelAngles (const float handrot[3],
	float gunmodelpitch, float controller_roll, float out[3]);

/* Rotate and scale a local aim offset using inherited Quake angle vectors. */
qboolean VR_LocomotionAimOffsetToWorld (const float local[3],
	const float angles[3], float scale, float world[3]);

/* Checked inverse of the held viewmodel transform. Requires finite inputs and
 * a positive scale; local is zeroed on failure. Inputs may alias local. */
qboolean VR_LocomotionWorldToModelOffsetChecked (const float world[3],
	const float viewmodel_angles[3], float scale, qboolean mirrored,
	float local[3]);

/* Convert a source muzzle offset to world space, reflecting it through the
 * held viewmodel's Y plane for left-handed rendering. */
qboolean VR_LocomotionMuzzleOffsetToWorld (const float local[3],
	const float hand_angles[3], float gunmodelscale, float gunmodelpitch,
	qboolean left_handed, float world[3]);

/* Invert MuzzleOffsetToWorld, including the left-handed viewmodel reflection.
 * Inputs and angles must be finite and gunmodelscale nonzero; local is zeroed
 * on failure. Inputs may alias local. */
qboolean VR_LocomotionWorldToMuzzleOffset (const float world[3],
	const float hand_angles[3], float gunmodelscale, float gunmodelpitch,
	qboolean left_handed, float local[3]);

/* Convert OpenXR right/up/back tracking positions in metres to the inherited
 * body-relative Quake grip offset. tracking_yaw is a Quake yaw in degrees. */
qboolean VR_LocomotionHandBodyOffset (const float head[3], const float hand[3],
	float tracking_yaw, float units_per_metre, float head_eye_height, float out[3]);

/* Produce forward/side/up command contributions before vr_movement_speed and
 * the run multiplier. forward_speed intentionally scales both horizontal
 * command axes, matching the inherited VR movement path. */
qboolean VR_LocomotionMove (int mode, const float head[3], const float offhand[3],
	float forward_axis, float side_axis, float forward_speed, float up_speed,
	float out[3]);

#endif /* QUAKE_VR_LOCOMOTION_H */
