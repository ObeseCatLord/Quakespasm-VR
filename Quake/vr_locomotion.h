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

/* Produce forward/side/up command contributions before vr_movement_speed and
 * the run multiplier. forward_speed intentionally scales both horizontal
 * command axes, matching the inherited VR movement path. */
qboolean VR_LocomotionMove (int mode, const float head[3], const float offhand[3],
	float forward_axis, float side_axis, float forward_speed, float up_speed,
	float out[3]);

#endif /* QUAKE_VR_LOCOMOTION_H */
