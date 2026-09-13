/* Stateless free-water hand swimming.  PMove owns eligibility, liquid
 * categorization, native drag, and idle sinking; this only adds a bounded
 * physical-stroke velocity contribution. */
#ifndef VR_GORILLA_SWIM_H
#define VR_GORILLA_SWIM_H

#include <math.h>
#include "vr_gorilla_types.h"

#define VRG_SWIM_DEADZONE 2.6f       /* 0.1 m/s at 26.2467 Quake u/m */
#define VRG_SWIM_ACCEL_COEFF .1f     /* quadratic acceleration, 1/u */
#define VRG_SWIM_HAND_WEIGHT .5f
/* 6.5 m/s² is a deliberate comfort ceiling for the sum of both arms. */
#define VRG_SWIM_MAX_ACCEL 170.6f

static float VRGS_Dot(const float *a, const float *b) {
  return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
static float VRGS_Length(const float *v) { return sqrtf(VRGS_Dot(v,v)); }
static int VRGS_Finite(const float *v) {
  return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/* Input positions are not used for swimming, but reject malformed packets
 * here as well as malformed physical velocities. The caller has already made
 * any authority/VR eligibility decision. */
static int VRGS_InputValid(const vr_gorilla_input_t *in, float dt,
    float maxspeed, const float *velocity) {
  int hand;
  if (!in || !velocity || !isfinite(dt) || dt<=0 || dt>.125f ||
      !isfinite(maxspeed) || maxspeed<=0 || !VRGS_Finite(velocity) ||
      (in->flags & ~VR_GORILLA_FLAGS) || (in->flags & VR_GORILLA_RESET) ||
      (in->flags & VR_GORILLA_HANDS) != VR_GORILLA_HANDS ||
      !VRGS_Finite(in->head))
    return 0;
  for (hand=0; hand<2; ++hand)
    if (!VRGS_Finite(in->hand[hand]) || !VRGS_Finite(in->velocity[hand]) ||
        VRGS_Length(in->velocity[hand]) > VR_GORILLA_MAX_HAND_SPEED)
      return 0;
  return 1;
}

/* Scale only the newly requested contribution. Current native velocity is
 * never truncated. Above maxspeed, accept only a strict speed reduction;
 * checking the resulting magnitude (rather than merely dot(v,d)<0) rejects
 * an outward/tangential contribution that would still increase speed. */
static float VRGS_LimitContribution(const float *velocity, const float *delta,
    float maxspeed) {
  float current=VRGS_Length(velocity), target[3], a=VRGS_Dot(delta,delta),
      b=2*VRGS_Dot(velocity,delta), limit=fmaxf(maxspeed,current), root, disc;
  int j;
  for (j=0;j<3;++j) target[j]=velocity[j]+delta[j];
  if (VRGS_Length(target) <= limit)
    return current>maxspeed && VRGS_Length(target)>=current ? 0 : 1;
  if (a<=0)
    return 0;
  if (current>maxspeed) {
    if (b>=0)
      return 0;
    /* Here limit=current, so the positive crossing is -b/a. Half of it
     * remains a strict reduction instead of merely returning to the cap. */
    root=-b/a;
    return fminf(1,root*.5f);
  }
  /* Below/at cap, solve |v+t*d|=maxspeed. */
  disc=b*b-4*a*(current*current-limit*limit);
  if (disc<0)
    return 0;
  root=(-b+sqrtf(disc))/(2*a);
  if (!isfinite(root) || root<=0)
    return 0;
  return fminf(1,root);
}

/* Return nonzero for a qualifying physical stroke, even when the speed cap
 * rejects its contribution. liquidmask selects submerged hands; solidmask
 * excludes hands already contributing a solid Gorilla brace. */
static inline int VRG_SwimImpulse(const vr_gorilla_input_t *in,
    unsigned int liquidmask, unsigned int solidmask, float dt, float maxspeed,
    float *velocity) {
  float accel[3]={0,0,0}, delta[3], speed, excess, scale, magnitude;
  int hand,j,active=0;
  if (!VRGS_InputValid(in,dt,maxspeed,velocity))
    return 0;
  liquidmask &= VR_GORILLA_HANDS;
  solidmask &= VR_GORILLA_HANDS;
  for (hand=0; hand<2; ++hand) {
    if (!(liquidmask&(1u<<hand)) || (solidmask&(1u<<hand)))
      continue;
    speed=VRGS_Length(in->velocity[hand]);
    excess=speed-VRG_SWIM_DEADZONE;
    if (excess<=0)
      continue;
    active=1;
    scale=-VRG_SWIM_HAND_WEIGHT*VRG_SWIM_ACCEL_COEFF*excess*excess/speed;
    for (j=0;j<3;++j) accel[j]+=in->velocity[hand][j]*scale;
  }
  if (!active)
    return 0;
  magnitude=VRGS_Length(accel);
  if (magnitude>VRG_SWIM_MAX_ACCEL) {
    scale=VRG_SWIM_MAX_ACCEL/magnitude;
    for (j=0;j<3;++j) accel[j]*=scale;
  }
  for (j=0;j<3;++j) delta[j]=accel[j]*dt;
  scale=VRGS_LimitContribution(velocity,delta,maxspeed);
  for (j=0;j<3;++j) velocity[j]+=delta[j]*scale;
  return 1;
}
#endif
