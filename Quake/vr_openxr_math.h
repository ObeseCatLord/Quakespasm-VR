#ifndef QUAKE_VR_OPENXR_MATH_H
#define QUAKE_VR_OPENXR_MATH_H
#include <math.h>
#include <string.h>
#include "vr_openxr.h"

/* Tangent-space clip planes, inward normals in the renderer's world basis.
 * The same function serves a full asymmetric eye or a same-pose inset crop. */
static inline int VRXR_FrustumNormals(const float bounds[4], const float forward[3],
    const float right[3], const float up[3], float normals[4][3]) {
  for (int i=0;i<4;++i) if (!isfinite(bounds[i])) return 0;
  if (bounds[0] >= bounds[1] || bounds[2] >= bounds[3]) return 0;
  for (int j=0;j<3;++j) {
    normals[0][j]=right[j]-bounds[0]*forward[j];
    normals[1][j]=-right[j]+bounds[1]*forward[j];
    normals[2][j]=up[j]-bounds[2]*forward[j];
    normals[3][j]=-up[j]+bounds[3]*forward[j];
  }
  for (int i=0;i<4;++i) {
    float len=sqrtf(normals[i][0]*normals[i][0]+normals[i][1]*normals[i][1]+normals[i][2]*normals[i][2]);
    if (!isfinite(len) || len < 0.001f) return 0;
    for (int j=0;j<3;++j) normals[i][j]/=len;
  }
  return 1;
}

/* Preserve the controller basis used by existing weapon offsets on xrizer.
 * Transform facts verified against xrizer profile offset_grip_pose() at
 * 31319560c1bd0f1e5c16936a946bb1c7295dbfd9, using glam 0.30.9 to evaluate
 * inverse(rotationXYZ, translation). See docs/openxr-development.md.
 * Unknown/native Frame profiles keep the standard grip pose until qualified;
 * never guess a model-specific offset merely from its button layout. */
static inline void VRXR_LegacyGrip(vrxr_device_t *device, int profile) {
  static const float offsets[4][3][4] = {
    {{.999332845f,-.004492998f,.036244877f,-.004779228f},
     {-.005284870f,.964169741f,.265233517f,-.020017814f},
     {-.036137905f,-.265248090f,.963502765f,-.129234076f}},
    {{.999332845f,.004492998f,-.036244877f,.004779228f},
     {.005284870f,.964169741f,.265233517f,-.020017814f},
     {.036137905f,-.265248090f,.963502765f,-.129234076f}},
    {{1,0,0,-.007f},{0,.936059535f,.351841658f,-.034157187f},{0,-.351841658f,.936059535f,-.096073247f}},
    {{1,0,0,.007f},{0,.936059535f,.351841658f,-.034157187f},{0,-.351841658f,.936059535f,-.096073247f}}
  };
  if (!device->valid || device->hand < 0 || device->hand > 1 ||
      (profile != VRXR_PROFILE_INDEX && profile != VRXR_PROFILE_TOUCH)) return;
  const float (*offset)[4] = offsets[(profile == VRXR_PROFILE_TOUCH ? 2 : 0)+device->hand];
  float result[3][4], translation[3];
  for (int i=0;i<3;++i) {
    translation[i]=0;
    for (int j=0;j<3;++j) translation[i]+=device->matrix[i][j]*offset[j][3];
    for (int j=0;j<3;++j) {
      result[i][j]=0;
      for (int k=0;k<3;++k) result[i][j]+=device->matrix[i][k]*offset[k][j];
    }
    result[i][3]=device->matrix[i][3]+translation[i];
  }
  if (device->velocity_valid && device->angular_velocity_valid) {
    for (int i=0;i<3;++i) device->velocity[i]+=
        device->angular_velocity[(i+1)%3]*translation[(i+2)%3]-
        device->angular_velocity[(i+2)%3]*translation[(i+1)%3];
  } else if (device->velocity_valid) device->velocity_valid=0;
  memcpy(device->matrix,result,sizeof(result));
}
#endif
