#ifndef QUAKE_VR_OPENXR_MATH_H
#define QUAKE_VR_OPENXR_MATH_H
#include <float.h>
#include <math.h>
#include <string.h>
#include "vr_openxr.h"

/* XR_KHR_visibility_mask supplies XY on the view-space Z=-1 plane. Project
 * with this frame's asymmetric eye FOV and vkQuake's Vulkan-inverted Y. The
 * result is clip XY at W=1, suitable for a later per-eye mask draw. Do not
 * clamp it: masks may legitimately extend beyond the visible frustum. */
static inline int VRXR_ProjectHiddenAreaVertex(const vrxr_view_t *view,
    const float view_xy[2], float clip_xy[2]) {
  if (!view || !view_xy || !clip_xy ||
      !isfinite(view->left) || !isfinite(view->right) ||
      !isfinite(view->down) || !isfinite(view->up) ||
      !isfinite(view_xy[0]) || !isfinite(view_xy[1]) ||
      view->left>=view->right || view->down>=view->up) return 0;
  const double x=(2.0*view_xy[0]-view->right-view->left)/
      ((double)view->right-view->left);
  const double y=(-2.0*view_xy[1]+view->up+view->down)/
      ((double)view->up-view->down);
  if (!isfinite(x) || !isfinite(y) || fabs(x)>FLT_MAX || fabs(y)>FLT_MAX)
    return 0;
  clip_xy[0]=(float)x;
  clip_xy[1]=(float)y;
  return 1;
}

/* Retain donor per-model MVPs. Convert their symmetric 90-degree reversed-Z
 * center clip coordinates to each asymmetric eye, without subtracting large
 * world positions. Relative transforms use the runtime's metre-space poses. */
static inline int VRXR_StereoClip(const vrxr_frame_t *frame, float units_per_metre,
    float near_plane, float output[2][16]) {
  if (!frame || !output || !frame->devices[0].valid || !isfinite(units_per_metre) ||
      units_per_metre<=0 || !isfinite(near_plane) || near_plane<=0) return 0;
  const float (*head)[4]=frame->devices[0].matrix;
  float result[2][16];
  for (int eye=0;eye<2;++eye) {
    const vrxr_view_t *view=&frame->views[eye];
    if (!isfinite(view->left) || !isfinite(view->right) ||
        !isfinite(view->down) || !isfinite(view->up) ||
        view->left>=view->right || view->down>=view->up) return 0;
    float relative[16]={0}, projection[16]={0}, temporary[16]={0};
    for (int row=0;row<3;++row) {
      for (int col=0;col<3;++col)
        for (int k=0;k<3;++k)
          relative[col*4+row]+=view->matrix[k][row]*head[k][col];
      for (int k=0;k<3;++k)
        relative[12+row]+=view->matrix[k][row]*(head[k][3]-view->matrix[k][3])*units_per_metre;
    }
    relative[15]=1;
    projection[0]=2.f/(view->right-view->left);
    projection[5]=-2.f/(view->up-view->down);
    projection[8]=(view->right+view->left)/(view->right-view->left);
    projection[9]=-(view->up+view->down)/(view->up-view->down);
    projection[11]=-1;
    projection[14]=near_plane;
    for (int col=0;col<4;++col)
      for (int row=0;row<4;++row)
        for (int k=0;k<4;++k)
          temporary[col*4+row]+=projection[k*4+row]*relative[col*4+k];
    // inverse(center projection): (x,y,z,w) -> (x,-y,-w,z/near).
    for (int row=0;row<4;++row) {
      result[eye][row]=temporary[row];
      result[eye][4+row]=-temporary[4+row];
      result[eye][8+row]=temporary[12+row]/near_plane;
      result[eye][12+row]=-temporary[8+row];
    }
    for (int i=0;i<16;++i) if (!isfinite(result[eye][i])) return 0;
  }
  memcpy(output,result,sizeof(result));
  return 1;
}

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
