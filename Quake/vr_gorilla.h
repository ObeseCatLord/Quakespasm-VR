/*
 * Hand ("monke") locomotion. Shared by authoritative and predicted movement;
 * the normal Quake body hull and native gameplay remain external.
 *
 * The per-command algorithm follows Another Axiom's GorillaLocomotion
 * (Player.cs) as tuned by Ancient Dungeon VR's "monke" movement mode:
 * planted palms move the body 1:1, a small virtual gravity keeps resting
 * palms in contact, large per-step jumps are treated as tracking/teleport
 * discontinuities, and while any palm touches a surface the body's fling
 * velocity is reset to 1.25x its smoothed recent velocity (capped).  That
 * fling outruns the pushing hand, so the palm detaches at peak speed.
 * Ancient Dungeon's tuning (arm 1.75m, unstick 1m, history ~6 frames,
 * jump x1.25 capped at 7m/s, min fling 0.8m/s, gravity 9m/s^2) is converted
 * to Quake units; launch speeds are scaled by sqrt(g_quake/g_ad) so arcs keep
 * the same shape under Quake's much stronger gravity.
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

#define VRG_UNITS_PER_METRE 26.2467f /* V_VRUnitsPerMetre() at world scale 1 */
#define VRG_AD_GRAVITY (9.0f*VRG_UNITS_PER_METRE)
#define VRG_ARM_LENGTH (1.75f*VRG_UNITS_PER_METRE)
#define VRG_UNSTICK_DISTANCE (1.0f*VRG_UNITS_PER_METRE)
#define VRG_VELOCITY_LIMIT (0.05f*VRG_UNITS_PER_METRE)
#define VRG_JUMP_MULTIPLIER 1.25f
#define VRG_MAX_JUMP_SPEED (7.0f*VRG_UNITS_PER_METRE)
#define VRG_MIN_FLING (0.8f*VRG_UNITS_PER_METRE)
#define VRG_SLIDE_SINGLE .001f
#define VRG_SLIDE_BOTH .02f
/* AD treats 0.2m in one frame as a teleport.  Longer commands scale by speed;
 * 30m/s leaves room for gravity-scaled flings plus a fast hand. */
#define VRG_DISCONTINUITY (0.2f*VRG_UNITS_PER_METRE)
#define VRG_DISCONTINUITY_SPEED (30.0f*VRG_UNITS_PER_METRE)
/* Exponential stand-in for the six-frame (~67ms) velocity average. */
#define VRG_VELOCITY_TAU .033f

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

/* Physical palms are limited to arm's length from the head. */
static void VRG_ArmClamp(const float *head, const float *palm, float *out) {
  float arm[3], length;
  int j;
  for (j=0;j<3;++j) arm[j]=palm[j]-head[j];
  length=VRG_Length(arm);
  if (length <= VRG_ARM_LENGTH || length <= 0) {
    memcpy(out,palm,3*sizeof(float));
    return;
  }
  for (j=0;j<3;++j) out[j]=head[j]+arm[j]*(VRG_ARM_LENGTH/length);
}

/* Launch speeds are tuned against Ancient Dungeon's 9m/s^2.  Preserving the
 * trajectory shape under a different gravity scales speed by sqrt(g/g_ad). */
static float VRG_GravityScale(float gravity) {
  float k=gravity>0 ? sqrtf(gravity/VRG_AD_GRAVITY) : 1;
  return fminf(fmaxf(k,.5f),3.f);
}

/* Restart from the current pose with no motion and no remembered velocity. */
static void VRG_Rebase(vr_gorilla_state_t *s, const float hands[2][3],
    const float *origin) {
  VRG_Reset(s);
  s->initialized=1;
  memcpy(s->anchor,hands,2*3*sizeof(float));
  memcpy(s->origin,origin,3*sizeof(float));
}

/* Called once per accepted command, before ordinary gravity.  While braced
 * the caller skips ordinary gravity; native translation, water/ladder
 * policies and gameplay impulses stay native.  velocity is replaced by the
 * fling (or stopped) while any palm touches, as Ancient Dungeon does. */
static vr_gorilla_result_t VRG_Step(vr_gorilla_state_t *s,
    const vr_gorilla_input_t *in, float *origin, float *velocity,
    float dt, float gravity, void *ctx, vr_gorilla_trace_fn trace,
    vr_gorilla_surface_fn surface) {
  vr_gorilla_result_t result;
  float head[3], cur[2][3], anchors[2][3], desired[3], resolved[3],
      hand_move[2][3]={{0}}, move[3], before[3], actual[3], native[3],
      sample[3], k, drop, limit, alpha;
  int h, j, contact[2]={0}, seeded[2]={0}, hit_surface[2]={-1,-1},
      was, pair, discontinuity=0;
  memset(&result,0,sizeof(result));
  result.contact[0]=result.contact[1]=-1;
  if (!VRG_InputValid(in) || !VRG_Finite(origin) || !VRG_Finite(velocity) ||
      !isfinite(dt) || dt<=0 || dt>.125f || !isfinite(gravity)) {
    VRG_Reset(s);
    return result;
  }
  k=VRG_GravityScale(gravity);
  for (j=0;j<3;++j) {
    head[j]=origin[j]+in->head[j];
    native[j]=origin[j]-s->origin[j];
    before[j]=origin[j];
  }
  for (h=0;h<2;++h) {
    float raw[3];
    for (j=0;j<3;++j) raw[j]=origin[j]+in->hand[h][j];
    VRG_ArmClamp(head,raw,cur[h]);
  }
  if (!s->initialized || (in->flags & VR_GORILLA_RESET) || s->recovering ||
      !VRG_Finite(s->origin) || !VRG_Finite(s->velocity) ||
      VRG_Length(native)>64) {
    VRG_Rebase(s,(const float (*)[3])cur,origin);
    memcpy(result.palm,cur,sizeof(cur));
    return result;
  }
  drop=2*fmaxf(0,gravity)*dt*dt;
  limit=fmaxf(VRG_DISCONTINUITY,VRG_DISCONTINUITY_SPEED*dt);
  was=s->touching;

  /* Anchors in world space; a palm cannot brace from the far side of a wall. */
  for (h=0;h<2;++h) {
    vr_gorilla_trace_t reach;
    if (!VRG_AnchorToWorld(s,h,ctx,surface,anchors[h])) {
      VRG_ResetHand(s,h,cur[h]);
      memcpy(anchors[h],cur[h],3*sizeof(float));
      was&=~(1u<<h);
      continue;
    }
    reach=trace(ctx,head,anchors[h],0);
    if (reach.startsolid || reach.allsolid || reach.fraction<.999f) {
      VRG_ResetHand(s,h,cur[h]);
      memcpy(anchors[h],cur[h],3*sizeof(float));
      was&=~(1u<<h);
    }
  }

  /* Pass 1: sweep each palm (with a little virtual gravity) for contact. */
  for (h=0;h<2;++h) {
    float mv[3];
    memcpy(desired,cur[h],sizeof(desired));
    desired[2]-=drop;
    for (j=0;j<3;++j) mv[j]=desired[j]-anchors[h][j];
    if (VRG_Length(mv)>limit) {
      discontinuity=1;
      break;
    }
    if (VRG_ResolveHand(ctx,trace,anchors[h],desired,VRG_SLIDE_SINGLE,
        resolved,&hit_surface[h])) {
      contact[h]=1;
      for (j=0;j<3;++j)
        hand_move[h][j]=((was&(1u<<h)) ? anchors[h][j] : resolved[j])-cur[h][j];
    } else if (!(was&(1u<<h))) {
      /* A palm that starts inside geometry (e.g. reached through the floor
       * between samples) plants on the surface it entered, without motion. */
      vr_gorilla_trace_t inside=VRG_HandTrace(ctx,trace,anchors[h],
          anchors[h],VR_GORILLA_RADIUS);
      if ((inside.startsolid || inside.allsolid) &&
          VRG_SeedEmbeddedPalm(ctx,trace,head,cur[h],resolved,&hit_surface[h])) {
        memcpy(anchors[h],resolved,3*sizeof(float));
        contact[h]=seeded[h]=1;
      }
    }
  }
  pair=!((!contact[0] && !(was&1)) || (!contact[1] && !(was&2)));
  for (j=0;j<3;++j) {
    move[j]=hand_move[0][j]+hand_move[1][j];
    if (pair) move[j]*=.5f;
  }
  if (discontinuity || !VRG_Finite(move) || VRG_Length(move)>limit) {
    VRG_Rebase(s,(const float (*)[3])cur,origin);
    memcpy(result.palm,cur,sizeof(cur));
    return result;
  }
  VRG_MoveBody(ctx,trace,origin,move);
  for (j=0;j<3;++j) {
    actual[j]=origin[j]-before[j];
    for (h=0;h<2;++h) cur[h][j]+=actual[j];
  }

  /* Pass 2: carry each palm to its new position, stopping at surfaces. */
  for (h=0;h<2;++h) {
    int surf=-1;
    if (seeded[h])
      continue;
    if (VRG_ResolveHand(ctx,trace,anchors[h],cur[h],
        pair ? VRG_SLIDE_BOTH : VRG_SLIDE_SINGLE,resolved,&surf)) {
      memcpy(anchors[h],resolved,3*sizeof(float));
      contact[h]=1;
      hit_surface[h]=surf;
    } else {
      memcpy(anchors[h],cur[h],3*sizeof(float));
      if (contact[h])
        hit_surface[h]=0; /* free point: never bind it to a moving brush */
    }
  }

  /* A palm dragged too far from its anchor lets go when the arm is clear. */
  for (h=0;h<2;++h) {
    float d[3];
    vr_gorilla_trace_t clear;
    if (!contact[h]) continue;
    for (j=0;j<3;++j) d[j]=cur[h][j]-anchors[h][j];
    if (VRG_Length(d) <= VRG_UNSTICK_DISTANCE) continue;
    clear=VRG_HandTrace(ctx,trace,head,cur[h],VR_GORILLA_RADIUS);
    if (clear.startsolid || clear.allsolid || clear.fraction<1) continue;
    memcpy(anchors[h],cur[h],3*sizeof(float));
    contact[h]=0;
  }

  s->touching=0;
  for (h=0;h<2;++h) {
    if (contact[h] &&
        VRG_StoreAnchor(s,h,hit_surface[h],anchors[h],ctx,surface)) {
      s->touching|=1u<<h;
      result.contact[h]=hit_surface[h]>0 ? hit_surface[h] : 0;
    } else {
      VRG_ResetHand(s,h,contact[h] ? cur[h] : anchors[h]);
      memcpy(anchors[h],s->anchor[h],3*sizeof(float));
    }
    memcpy(result.palm[h],anchors[h],3*sizeof(float));
  }

  /* Smoothed body velocity in Ancient Dungeon terms: ballistic motion since
   * the previous command is divided back out of the launch scale; motion made
   * while braced is mostly corrected by the anchors and is taken as is. */
  alpha=dt/(VRG_VELOCITY_TAU+dt);
  for (j=0;j<3;++j) {
    sample[j]=(actual[j]+native[j]/((was) ? 1 : k))/dt;
    s->velocity[j]+=alpha*(sample[j]-s->velocity[j]);
  }

  if (s->touching) {
    float speed=VRG_Length(s->velocity), fling=0;
    result.braced=1;
    if (speed>VRG_VELOCITY_LIMIT)
      fling=fminf(speed*VRG_JUMP_MULTIPLIER,VRG_MAX_JUMP_SPEED);
    if (fling>VRG_MIN_FLING) {
      for (j=0;j<3;++j) velocity[j]=s->velocity[j]*(fling/speed)*k;
      result.launched=1;
    } else
      velocity[0]=velocity[1]=velocity[2]=0;
  }
  memcpy(s->origin,origin,3*sizeof(float));
  result.stepped=1;
  return result;
}
#endif
