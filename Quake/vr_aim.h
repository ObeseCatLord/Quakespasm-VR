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

#ifndef QUAKE_VR_AIM_H
#define QUAKE_VR_AIM_H

#include <math.h>

/* Quake angle order. This header deliberately has no engine-type dependency. */
enum { VR_AIM_PITCH, VR_AIM_YAW, VR_AIM_ROLL };

#ifndef VR_AIMMODE_HEAD_MYAW
#define VR_AIMMODE_HEAD_MYAW 1
#define VR_AIMMODE_HEAD_MYAW_MPITCH 2
#define VR_AIMMODE_MOUSE_MYAW 3
#define VR_AIMMODE_MOUSE_MYAW_MPITCH 4
#define VR_AIMMODE_BLENDED 5
#define VR_AIMMODE_BLENDED_NOPITCH 6
#define VR_AIMMODE_CONTROLLER 7
#endif

/* Transforms a HMD Matrix34 to a Quaternion.
 * Function logic nicked from
 * https://github.com/Omnifinity/OpenVR-Tracking-Example
 *
 * Ported from Quakespasm OpenVR's Matrix34ToQuaternion and
 * QuatToYawPitchRoll helpers. The signs and Quake pitch/yaw/roll order are
 * intentional. */
static inline int VR_AimPoseAngles(const float matrix[3][4], float yaw_offset,
    float out[3])
{
  double q_w, q_x, q_y, q_z, square_w, square_x, square_y, square_z;
  double asin_argument, result[3];
  const double radians_to_degrees = 180.0 / 3.14159265358979323846;

  if (!matrix || !out || !isfinite(yaw_offset))
    return 0;
  for (int row = 0; row < 3; ++row)
    for (int column = 0; column < 4; ++column)
      if (!isfinite(matrix[row][column]))
        return 0;

  q_w = sqrt(fmax(0.0, 1.0 + matrix[0][0] + matrix[1][1] + matrix[2][2])) / 2.0;
  q_x = sqrt(fmax(0.0, 1.0 + matrix[0][0] - matrix[1][1] - matrix[2][2])) / 2.0;
  q_y = sqrt(fmax(0.0, 1.0 - matrix[0][0] + matrix[1][1] - matrix[2][2])) / 2.0;
  q_z = sqrt(fmax(0.0, 1.0 - matrix[0][0] - matrix[1][1] + matrix[2][2])) / 2.0;
  q_x = copysign(q_x, (double)matrix[2][1] - matrix[1][2]);
  q_y = copysign(q_y, (double)matrix[0][2] - matrix[2][0]);
  q_z = copysign(q_z, (double)matrix[1][0] - matrix[0][1]);
  if (!isfinite(q_w) || !isfinite(q_x) || !isfinite(q_y) || !isfinite(q_z))
    return 0;

  square_w = q_w * q_w;
  square_x = q_x * q_x;
  square_y = q_y * q_y;
  square_z = q_z * q_z;
  asin_argument = -2.0 * (q_y * q_z - q_w * q_x);
  if (asin_argument < -1.0)
    asin_argument = -1.0;
  else if (asin_argument > 1.0)
    asin_argument = 1.0;

  result[VR_AIM_ROLL] = -atan2(2.0 * (q_x * q_y + q_w * q_z),
      square_w - square_x + square_y - square_z) * radians_to_degrees;
  result[VR_AIM_PITCH] = -asin(asin_argument) * radians_to_degrees;
  result[VR_AIM_YAW] = atan2(2.0 * (q_x * q_z + q_w * q_y),
      square_w - square_x - square_y + square_z) * radians_to_degrees + yaw_offset;
  for (int component = 0; component < 3; ++component)
    if (!isfinite(result[component]))
      return 0;

  out[0] = (float)result[0];
  out[1] = (float)result[1];
  out[2] = (float)result[2];
  return 1;
}

/* Resolve one inherited aim update. aim and view are current in/out values;
 * previous_orientation and previous_aim are the prior frame values. A
 * controller_aim is already in Quake angles after any engine-owned weapon
 * transform. A null controller_aim preserves the current aim. */
static inline void VR_AimResolve(int mode, float deadzone,
    const float orientation[3], const float previous_orientation[3],
    const float previous_aim[3], const float controller_aim[3], float aim[3],
    float view[3])
{
  float resolved_aim[3], resolved_view[3];

  if (!orientation || !previous_orientation || !previous_aim || !aim || !view)
    return;
  for (int component = 0; component < 3; ++component) {
    resolved_aim[component] = aim[component];
    resolved_view[component] = view[component];
  }

  switch (mode) {
  default:
  case VR_AIMMODE_HEAD_MYAW:
    resolved_view[VR_AIM_PITCH] = resolved_aim[VR_AIM_PITCH] = orientation[VR_AIM_PITCH];
    resolved_aim[VR_AIM_YAW] += orientation[VR_AIM_YAW] - previous_orientation[VR_AIM_YAW];
    resolved_view[VR_AIM_YAW] = resolved_aim[VR_AIM_YAW];
    resolved_aim[VR_AIM_ROLL] = 0.0f;
    break;

  case VR_AIMMODE_HEAD_MYAW_MPITCH:
    resolved_aim[VR_AIM_PITCH] += orientation[VR_AIM_PITCH] - previous_orientation[VR_AIM_PITCH];
    resolved_view[VR_AIM_PITCH] = resolved_aim[VR_AIM_PITCH];
    resolved_aim[VR_AIM_YAW] += orientation[VR_AIM_YAW] - previous_orientation[VR_AIM_YAW];
    resolved_view[VR_AIM_YAW] = resolved_aim[VR_AIM_YAW];
    resolved_aim[VR_AIM_ROLL] = 0.0f;
    break;

  case VR_AIMMODE_MOUSE_MYAW:
    resolved_view[VR_AIM_PITCH] = orientation[VR_AIM_PITCH];
    resolved_view[VR_AIM_YAW] = resolved_aim[VR_AIM_YAW] + orientation[VR_AIM_YAW];
    resolved_aim[VR_AIM_ROLL] = 0.0f;
    break;

  case VR_AIMMODE_MOUSE_MYAW_MPITCH:
    resolved_view[VR_AIM_PITCH] = resolved_aim[VR_AIM_PITCH] + orientation[VR_AIM_PITCH];
    resolved_view[VR_AIM_YAW] = resolved_aim[VR_AIM_YAW] + orientation[VR_AIM_YAW];
    resolved_aim[VR_AIM_ROLL] = 0.0f;
    break;

  case VR_AIMMODE_BLENDED:
  case VR_AIMMODE_BLENDED_NOPITCH: {
    float difference_hmd_yaw = orientation[VR_AIM_YAW] - previous_orientation[VR_AIM_YAW];
    float difference_hmd_pitch = orientation[VR_AIM_PITCH] - previous_orientation[VR_AIM_PITCH];
    float difference_aim_yaw = resolved_aim[VR_AIM_YAW] - previous_aim[VR_AIM_YAW];
    float difference_yaw;

    resolved_view[VR_AIM_YAW] += difference_hmd_yaw;
    difference_yaw = resolved_view[VR_AIM_YAW] - resolved_aim[VR_AIM_YAW];
    if (fabsf(difference_yaw) > deadzone / 2.0f) {
      resolved_aim[VR_AIM_YAW] += difference_hmd_yaw;
      resolved_view[VR_AIM_YAW] += difference_aim_yaw;
    }
    if (mode == VR_AIMMODE_BLENDED)
      resolved_aim[VR_AIM_PITCH] += difference_hmd_pitch;
    resolved_view[VR_AIM_PITCH] = orientation[VR_AIM_PITCH];
    resolved_aim[VR_AIM_ROLL] = 0.0f;
    break;
  }

  case VR_AIMMODE_CONTROLLER:
    resolved_view[VR_AIM_PITCH] = orientation[VR_AIM_PITCH];
    resolved_view[VR_AIM_YAW] = orientation[VR_AIM_YAW];
    if (controller_aim)
      for (int component = 0; component < 3; ++component)
        resolved_aim[component] = controller_aim[component];
    break;
  }

  resolved_view[VR_AIM_ROLL] = orientation[VR_AIM_ROLL];
  for (int component = 0; component < 3; ++component) {
    aim[component] = resolved_aim[component];
    view[component] = resolved_view[component];
  }
}

#endif
