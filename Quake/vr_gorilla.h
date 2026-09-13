/*
 * Hand locomotion inspired by Another Axiom's GorillaLocomotion (Player.cs)
 * and Duncan Carroll's GorillaQuake. Shared by authoritative and predicted
 * movement. The normal Quake body hull and native gameplay remain external.
 *
 * Unlike the original render-frame velocity ring, this uses a command-time
 * exponential filter: replay and different headset refresh rates agree.
 *
 * Original GorillaLocomotion algorithm: Copyright (c) 2021 Another-Axiom.
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef VR_GORILLA_H
#define VR_GORILLA_H
#include <math.h>
#include <string.h>
#include "vr_gorilla_types.h"
#include "vr_gorilla_swim.h" /* shared bounded native-velocity contribution */

/* GorillaLocomotion's 0.4m/s velocityLimit and 6.5m/s maxJumpSpeed at
 * Quake's default 26.2467 units/metre.  Keep a small tracker quantization
 * allowance on a single command; anchored hand motion is otherwise bounded
 * by the reported physical velocity and command duration. */
#define VRG_LAUNCH_THRESHOLD 10.5f
#define VRG_LAUNCH_MAX_SPEED 170.6f
#define VRG_TRACK_TOLERANCE 2.0f

typedef struct {
  float fraction, end[3], normal[3];
  int startsolid, allsolid, entity; /* world0; brush>0; miss -1 */
} vr_gorilla_trace_t;

/* body!=0 uses the caller's unchanged player hull; otherwise POINT hull0,
 * world/brushes only. The callback must not invoke QC or mutate shared bounds. */
typedef vr_gorilla_trace_t (*vr_gorilla_trace_fn)(void *, const float *,
                                                const float *, int body);
/* A positive entity is a movable brush.  to_world validates the input token
 * in *model and transforms point(local) to out(world); world->local returns
 * the current token in *model.  It must not run QC or move the player. */
typedef int (*vr_gorilla_surface_fn)(void *ctx, int entity,
    unsigned int *model, const float *point, float *out, int to_world);
typedef struct {
  int braced, launched, stepped; /* stepped excludes reset/initial samples */
  int contact[2];
  float palm[2][3];
} vr_gorilla_result_t;

static float VRG_Dot(const float *a, const float *b) {
  return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static float VRG_Length(const float *a) { return sqrtf(VRG_Dot(a,a)); }
static int VRG_Finite(const float *a) {
  return isfinite(a[0]) && isfinite(a[1]) && isfinite(a[2]);
}
static void VRG_Reset(vr_gorilla_state_t *s) { memset(s,0,sizeof(*s)); }
static int VRG_InputValid(const vr_gorilla_input_t *in) {
  int h, j;
  float arm[3];
  if ((in->flags & ~VR_GORILLA_FLAGS) ||
      (in->flags & VR_GORILLA_HANDS) != VR_GORILLA_HANDS ||
      !VRG_Finite(in->head) || VRG_Length(in->head) > 160)
    return 0;
  for (h=0; h<2; ++h) {
    if (!VRG_Finite(in->hand[h]) || !VRG_Finite(in->velocity[h]) ||
        VRG_Length(in->velocity[h]) > VR_GORILLA_MAX_HAND_SPEED)
      return 0;
    for (j=0; j<3; ++j) arm[j]=in->hand[h][j]-in->head[j];
    if (VRG_Length(arm) > VR_GORILLA_MAX_REACH)
      return 0;
  }
  return 1;
}

/* Seven swept points approximate the palm radius without selecting Quake's
 * baked 32-unit player hull for a nominal six-unit hand box. Final body
 * collision is independent and always retains the normal full hull. */
static vr_gorilla_trace_t VRG_HandTrace(void *ctx, vr_gorilla_trace_fn trace,
    const float *start, const float *end, float radius) {
  vr_gorilla_trace_t best = trace(ctx,start,end,0), t;
  float a[3], b[3];
  int axis, sign, j, startsolid=best.startsolid, allsolid=best.allsolid;
  for (axis=0; axis<3; ++axis) for (sign=-1; sign<=1; sign+=2) {
    for (j=0; j<3; ++j) { a[j]=start[j]; b[j]=end[j]; }
    a[axis]+=sign*radius*.995f; b[axis]+=sign*radius*.995f;
    t=trace(ctx,a,b,0);
    if (t.startsolid || t.allsolid) {
      startsolid|=t.startsolid;
      allsolid|=t.allsolid;
      continue;
    }
    if (t.fraction < best.fraction) {
      best=t;
      best.end[axis]-=sign*radius*.995f;
    }
  }
  best.startsolid|=startsolid;
  best.allsolid|=allsolid;
  return best;
}

/* A lowered tracked posture can seed a palm inside the floor before any
 * ordinary sweep has a clear start. Sweep from the known-clear head and
 * recover only an upward floor entry;
 * the caller plants it without turning the correction into first-frame energy. */
static int VRG_SeedEmbeddedPalm(void *ctx, vr_gorilla_trace_fn trace,
    const float *head, const float *palm, float *resolved, int *entity) {
  vr_gorilla_trace_t t;
  int j;
  t=VRG_HandTrace(ctx,trace,head,palm,VR_GORILLA_RADIUS);
  if (t.startsolid || t.allsolid || t.fraction>=1 || t.normal[2]<.5f)
    return 0;
  for (j=0;j<3;++j) resolved[j]=t.end[j]+t.normal[j]*.04f;
  *entity=t.entity;
  return 1;
}

static int VRG_ResolveHand(void *ctx, vr_gorilla_trace_fn trace,
    const float *start, const float *end, float slide,
    float *resolved, int *entity) {
  vr_gorilla_trace_t t=VRG_HandTrace(ctx,trace,start,end,VR_GORILLA_RADIUS);
  float target[3], remainder[3], first[3], dot;
  int j;
  if (t.allsolid || t.startsolid) {
    /* Embedded/invalid anchors are discarded, never converted into debt. */
    memcpy(resolved,end,3*sizeof(float));
    *entity=-1;
    return 0;
  }
  if (t.fraction >= 1) {
    memcpy(resolved,end,3*sizeof(float));
    *entity=-1;
    return 0;
  }
  *entity=t.entity;
  for (j=0; j<3; ++j) {
    first[j]=t.end[j]+t.normal[j]*.04f;
    remainder[j]=end[j]-first[j];
  }
  dot=VRG_Dot(remainder,t.normal);
  for (j=0; j<3; ++j)
    target[j]=first[j]+(remainder[j]-dot*t.normal[j])*slide;
  t=VRG_HandTrace(ctx,trace,first,target,VR_GORILLA_RADIUS);
  memcpy(resolved, t.startsolid || t.allsolid ? first : t.end,3*sizeof(float));
  return 1;
}

static void VRG_ResetHand(vr_gorilla_state_t *s, int hand,
    const float *world) {
  s->touching &= ~(1u<<hand);
  s->recovering &= ~(1u<<hand);
  s->surface[hand]=0;
  s->surface_model[hand]=0;
  memcpy(s->anchor[hand],world,3*sizeof(float));
  memset(s->recovery_offset[hand],0,3*sizeof(float));
}

/* A released/launching palm that was seeded below geometry must retain its
 * virtual reference until the physical palm itself is clear.  It no longer
 * has a surface binding or brace, so this cannot bank or spend contact debt. */
static void VRG_UnbindRecoveryHand(vr_gorilla_state_t *s, int hand,
    const float *raw) {
  s->touching &= ~(1u<<hand);
  s->surface[hand]=0;
  s->surface_model[hand]=0;
  memcpy(s->anchor[hand],raw,3*sizeof(float));
}

static void VRG_SeedRecovery(vr_gorilla_state_t *s, int hand,
    const float *raw, const float *resolved) {
  int j;
  for (j=0;j<3;++j) s->recovery_offset[hand][j]=resolved[j]-raw[j];
  s->recovering |= 1u<<hand;
}

/* Stored movable-brush anchors are local.  Resolve them once per command so
 * ordinary hand/body math stays entirely in the command's world coordinates. */
static int VRG_AnchorToWorld(vr_gorilla_state_t *s, int hand, void *ctx,
    vr_gorilla_surface_fn surface, float *world) {
  unsigned int model;
  if (s->surface[hand] <= 0) {
    memcpy(world,s->anchor[hand],3*sizeof(float));
    return VRG_Finite(world);
  }
  if (!surface)
    return 0;
  model=s->surface_model[hand];
  if (!model || !surface(ctx,s->surface[hand],&model,s->anchor[hand],world,1)
      || model != s->surface_model[hand] || !VRG_Finite(world))
    return 0;
  return 1;
}

/* Store only final brush contacts in brush-local space.  A failed binding is
 * an unavailable/replaced surface, not permission to retain a world anchor. */
static int VRG_StoreAnchor(vr_gorilla_state_t *s, int hand, int entity,
    const float *world, void *ctx, vr_gorilla_surface_fn surface) {
  unsigned int model=0;
  float local[3];
  if (entity <= 0) {
    s->surface[hand]=0;
    s->surface_model[hand]=0;
    memcpy(s->anchor[hand],world,3*sizeof(float));
    return 1;
  }
  if (!surface || !surface(ctx,entity,&model,world,local,0) || !model ||
      !VRG_Finite(local))
    return 0;
  s->surface[hand]=entity;
  s->surface_model[hand]=model;
  memcpy(s->anchor[hand],local,3*sizeof(float));
  return 1;
}

/* A constrained displacement, not another gravity/acceleration integrator. */
static void VRG_MoveBody(void *ctx, vr_gorilla_trace_fn trace,
    float *origin, const float *move) {
  float remaining[3], target[3], dot;
  int iteration, j;
  memcpy(remaining,move,3*sizeof(float));
  for (iteration=0; iteration<4; ++iteration) {
    vr_gorilla_trace_t t;
    if (VRG_Length(remaining) < .0001f) break;
    for (j=0; j<3; ++j) target[j]=origin[j]+remaining[j];
    t=trace(ctx,origin,target,1);
    if (t.startsolid || t.allsolid) break;
    memcpy(origin,t.end,3*sizeof(float));
    if (t.fraction >= 1) break;
    for (j=0; j<3; ++j) remaining[j]*=1-t.fraction;
    dot=VRG_Dot(remaining,t.normal);
    for (j=0; j<3; ++j) remaining[j]-=dot*t.normal[j];
  }
}

/* Called once per accepted command, before ordinary gravity. On a brace the
 * caller skips ordinary translation/gravity but retains categorization/QC.
 * Unbraced velocity/gravity, water/ladder policies and gameplay impulses stay
 * native; eligible Gorilla mode still suppresses stick propulsion externally. */
static vr_gorilla_result_t VRG_Step(vr_gorilla_state_t *s,
    const vr_gorilla_input_t *in, float *origin, float *velocity,
    float dt, float gravity, void *ctx, vr_gorilla_trace_fn trace,
    vr_gorilla_surface_fn surface) {
  vr_gorilla_result_t result;
  float hands[2][3], raw_hands[2][3], head[3], desired[3], resolved[3], movement[2][3]={{0}},
      anchors[2][3], hit_anchor[2][3], move[3], before[3], actual[3], delta[3];
  int h,j,contact[2]={0}, seeded[2]={0}, suspended[2]={0},
      hit_surface[2]={-1,-1};
  float alpha, speed, actual_speed, intent=0;
  memset(&result,0,sizeof(result));
  result.contact[0]=result.contact[1]=-1;
  if (!VRG_InputValid(in) || !VRG_Finite(origin) || !VRG_Finite(velocity) ||
      !isfinite(dt) || dt<=0 || dt>.125f || !isfinite(gravity)) {
    VRG_Reset(s);
    return result;
  }
  for (j=0;j<3;++j) {
    head[j]=origin[j]+in->head[j];
    delta[j]=origin[j]-s->origin[j];
    before[j]=origin[j];
    for(h=0;h<2;++h) raw_hands[h][j]=origin[j]+in->hand[h][j];
  }
  if (!s->initialized || (in->flags & VR_GORILLA_RESET) ||
      (s->recovering & ~VR_GORILLA_HANDS) || !VRG_Finite(s->origin) ||
      VRG_Length(delta)>64) {
    VRG_Reset(s);
    s->initialized=1;
    memcpy(s->anchor,raw_hands,sizeof(raw_hands));
    memcpy(s->origin,origin,3*sizeof(float));
    memcpy(result.palm,raw_hands,sizeof(raw_hands));
    return result;
  }
  for(h=0;h<2;++h) {
    if ((s->recovering&(1u<<h)) && !VRG_Finite(s->recovery_offset[h]))
      VRG_ResetHand(s,h,raw_hands[h]);
    for(j=0;j<3;++j)
      hands[h][j]=raw_hands[h][j]+((s->recovering&(1u<<h)) ?
          s->recovery_offset[h][j] : 0);
  }
  for(h=0;h<2;++h) {
    vr_gorilla_trace_t reach;
    /* Keep a seeded palm virtual while its raw position remains embedded.
     * Do not resolve it as a new contact: that would replant after launch or
     * release and zero native flight velocity.  Once the raw palm and its
     * one-command gravity reach are wholly clear, discard the bias and skip
     * this command to avoid a transition impulse; a later physical contact
     * may plant normally. */
    if ((s->recovering&(1u<<h)) && !(s->touching&(1u<<h))) {
      memcpy(desired,raw_hands[h],sizeof(desired));
      desired[2]-=2*fmaxf(0,gravity)*dt*dt;
      reach=VRG_HandTrace(ctx,trace,head,desired,VR_GORILLA_RADIUS);
      if (reach.startsolid || reach.allsolid || reach.fraction<1) {
        VRG_UnbindRecoveryHand(s,h,raw_hands[h]);
        suspended[h]=1;
        continue;
      }
      VRG_ResetHand(s,h,raw_hands[h]);
      memcpy(hands[h],raw_hands[h],3*sizeof(float));
      suspended[h]=1;
      continue;
    }
    if (!VRG_AnchorToWorld(s,h,ctx,surface,anchors[h])) {
      VRG_ResetHand(s,h,raw_hands[h]);
      memcpy(anchors[h],raw_hands[h],3*sizeof(float));
      continue;
    }
    for(j=0;j<3;++j) delta[j]=hands[h][j]-anchors[h][j];
    if (((s->touching&(1u<<h)) &&
        VRG_Length(delta) > VRG_Length(in->velocity[h])*dt + VRG_TRACK_TOLERANCE) ||
        VRG_Length(delta)>64) {
      VRG_ResetHand(s,h,raw_hands[h]);
      memcpy(anchors[h],raw_hands[h],3*sizeof(float));
      continue;
    }
    if (!(s->touching&(1u<<h)) &&
        VRG_SeedEmbeddedPalm(ctx,trace,head,hands[h],resolved,&hit_surface[h])) {
      s->surface[h]=0;
      s->surface_model[h]=0;
      memcpy(s->anchor[h],resolved,3*sizeof(float));
      memcpy(anchors[h],resolved,3*sizeof(float));
      memcpy(hit_anchor[h],resolved,3*sizeof(float));
      VRG_SeedRecovery(s,h,raw_hands[h],resolved);
      memcpy(hands[h],resolved,3*sizeof(float));
      seeded[h]=contact[h]=1;
    }
    /* A hand cannot push from the far side of a wall. Contact points may
     * touch the wall, so require only the clamped anchor's clear head path. */
    reach=trace(ctx,head,anchors[h],0);
    if (reach.startsolid || reach.allsolid || reach.fraction<.999f) {
      VRG_ResetHand(s,h,raw_hands[h]);
      memcpy(anchors[h],raw_hands[h],3*sizeof(float));
      continue;
    }
    memcpy(desired,hands[h],sizeof(desired));
    desired[2]-=2*fmaxf(0,gravity)*dt*dt;
    if (!seeded[h]) {
      contact[h]=VRG_ResolveHand(ctx,trace,anchors[h],desired,
          s->touching==3 ? .03f : .001f,resolved,&hit_surface[h]);
      if (contact[h])
        memcpy(hit_anchor[h],resolved,3*sizeof(float));
    }
    if (contact[h]) {
      if (!seeded[h]) {
        for(j=0;j<3;++j)
          movement[h][j]=((s->touching&(1u<<h)) ? anchors[h][j] : resolved[j])-hands[h][j];
        /* Anchors may lag native wind, knockback or carriage. Only a real
         * physical stroke may spend that correction: stationary hands must
         * never pull the body back against externally supplied movement. */
        {
          float length=VRG_Length(movement[h]);
          float travel=length>0 ?
              fmaxf(0,-VRG_Dot(movement[h],in->velocity[h])/length)*dt : 0;
          if (length>travel)
            for(j=0;j<3;++j) movement[h][j]*=travel/length;
        }
        intent=fmaxf(intent,VRG_Length(in->velocity[h]));
      }
    }
  }
  for(j=0;j<3;++j) {
    move[j]=movement[0][j]+movement[1][j];
    if ((contact[0] || (s->touching&1)) &&
        (contact[1] || (s->touching&2))) move[j]*=.5f;
  }
  /* Reject tracker/anchor discontinuities rather than banking a large push. */
  if (!VRG_Finite(move) || VRG_Length(move)>64) {
    VRG_Reset(s);
    return result;
  }
  VRG_MoveBody(ctx,trace,origin,move);
  for(j=0;j<3;++j) {
    actual[j]=origin[j]-before[j];
    for(h=0;h<2;++h) {
      hands[h][j]+=actual[j];
      raw_hands[h][j]+=actual[j];
    }
  }
  s->touching=0;
  for(h=0;h<2;++h) {
    int final_surface=-1;
    if (suspended[h]) {
      memcpy(result.palm[h],hands[h],3*sizeof(float));
      continue;
    }
    int final_contact=VRG_ResolveHand(ctx,trace,anchors[h],hands[h],
        contact[0] && contact[1] ? .03f : .001f,resolved,&final_surface);
    memcpy(result.palm[h],resolved,3*sizeof(float));
    if (contact[h] || final_contact) {
      int stored_surface=final_contact ? final_surface : hit_surface[h];
      if (seeded[h]) {
        if (!VRG_StoreAnchor(s,h,hit_surface[h],hit_anchor[h],ctx,surface)) {
          VRG_ResetHand(s,h,raw_hands[h]);
          continue;
        }
        s->touching|=1u<<h;
      } else if (final_contact &&
          !VRG_StoreAnchor(s,h,final_surface,resolved,ctx,surface)) {
        VRG_ResetHand(s,h,raw_hands[h]);
        continue;
      } else if (!final_contact && contact[h]) {
        if (!VRG_StoreAnchor(s,h,hit_surface[h],hit_anchor[h],ctx,surface)) {
          VRG_ResetHand(s,h,raw_hands[h]);
          continue;
        }
        s->touching|=1u<<h;
      }
      result.contact[h]=stored_surface;
      result.braced=1;
      if (final_contact && !seeded[h])
        s->touching|=1u<<h;
    } else {
      if (s->recovering&(1u<<h))
        VRG_UnbindRecoveryHand(s,h,raw_hands[h]);
      else
        VRG_ResetHand(s,h,raw_hands[h]);
    }
  }
  alpha=dt/(.04f+dt);
  for(j=0;j<3;++j) {
    actual[j]/=dt;
    s->velocity[j]+=(actual[j]-s->velocity[j])*alpha;
  }
  actual_speed=VRG_Length(actual);
  speed=VRG_Length(s->velocity);
  if (actual_speed<1) memset(s->velocity,0,sizeof(s->velocity));
  if (result.braced && intent>=VRG_LAUNCH_THRESHOLD &&
      actual_speed>=VRG_LAUNCH_THRESHOLD && speed>=VRG_LAUNCH_THRESHOLD &&
      VRG_Dot(actual,s->velocity)>0) {
    float gain=1.1f;
    if (speed*gain>VRG_LAUNCH_MAX_SPEED) gain=VRG_LAUNCH_MAX_SPEED/speed;
    if (s->velocity[2]*gain>VRG_LAUNCH_MAX_SPEED)
      gain=VRG_LAUNCH_MAX_SPEED/s->velocity[2];
    float addition[3], scale;
    for(j=0;j<3;++j) addition[j]=s->velocity[j]*gain;
    scale=VRGS_LimitContribution(velocity,addition,VRG_LAUNCH_MAX_SPEED);
    for(j=0;j<3;++j) velocity[j]+=addition[j]*scale;
    result.launched=1;
    result.braced=0;
    for(h=0;h<2;++h) {
      if (s->recovering&(1u<<h))
        VRG_UnbindRecoveryHand(s,h,raw_hands[h]);
      else
        VRG_ResetHand(s,h,raw_hands[h]);
    }
  }
  memcpy(s->origin,origin,3*sizeof(float));
  result.stepped=1;
  return result;
}
#endif
