#!/usr/bin/env python3
"""End check of production VR pointer preparation, trace adapters and drawing.

Compiles extracted production helpers against typed input, synthetic hull and
Vulkan seams. The real brush cache/rotation and CL_TraceLine selection run here;
BSP traversal, renderer resources and headset pixels require integration checks.
No engine build, platform tests or source changes occur when this fixture runs.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, signature):
    begin = source.index(signature)
    return source[begin:source.index("\n}", begin) + 2]


def harness():
    renderer = (ROOT / "Quake/gl_rmain.c").read_text()
    particles = (ROOT / "Quake/r_part_fte.c").read_text()
    menu = (ROOT / "Quake/menu.c").read_text()
    begin = renderer.rfind("typedef struct\n{", 0,
                           renderer.index("} vr_crosshair_frame_t;"))
    end = renderer.index("} vr_crosshair_frame_t;", begin) + len("} vr_crosshair_frame_t;")
    frame = renderer[begin:end] + "\nstatic vr_crosshair_frame_t vr_crosshair_frame;\n"
    begin = particles.index("typedef struct trace_line_bounds_s")
    end = particles.index("// rebuilds the brush entity list", begin)
    trace_types = particles[begin:end]
    trace_helpers = "\n".join(definition(particles, signature) for signature in (
        "static void CL_PrepareTraceLineEntities (void)",
        "float CL_TraceWorldLine (", "float CL_TraceLine ("))
    pointer_helpers = "\n".join(definition(renderer, signature) for signature in (
        "void R_PrepareVRCrosshair (void)", "static void R_FillDebugVertex (",
        "static void R_EmitVRCrosshairQuad (", "static float R_VRCrosshairHalfExtent (",
        "static qboolean R_VRCrosshairWorldPathReady (",
        "static void R_DrawVRCrosshairRay (", "static void R_DrawVRCrosshair ("))
    begin = menu.index('\tM_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * VR_OPT_CROSSHAIR_DEPTH,')
    row = menu[begin:menu.index('\n\tM_Print (cbx, MENU_LABEL_X, top + CHARACTER_SIZE * VR_OPT_CROSSHAIR_SIZE,', begin)]
    clamp = definition(menu, "static float M_VROptions_ClampFinite (")
    draw_begin = menu.index("static void M_VROptions_Draw (cb_context_t *cbx)\n{")
    state_begin = menu.index("\tconst float crosshair_depth =", draw_begin)
    state = menu[state_begin:menu.index(";", state_begin) + 1]
    menu_helper = clamp + "\nstatic void draw_range_row(cb_context_t *cbx) {\nconst int top=MENU_TOP;\n" + state + "\n" + row + "\n}\n"
    enum_begin = menu.index("enum\n{\n\tVR_OPT_FOVEATION,")
    menu_enum = menu[enum_begin:menu.index("};", enum_begin) + 2]
    menu_header = (ROOT / "Quake/menu.h").read_text()
    draw_header = (ROOT / "Quake/draw.h").read_text()
    menu_constants = "\n".join(line for line in (menu_header + draw_header).splitlines()
                               if line.startswith(("#define MENU_TOP", "#define MENU_LABEL_X",
                                                   "#define MENU_VALUE_X", "#define CHARACTER_SIZE")))
    return (PRELUDE + trace_types + trace_helpers + frame + pointer_helpers +
            "\n" + menu_constants + "\n" + menu_enum + "\n" + MENU_SEAMS + menu_helper + CHECKS)


PRELUDE = r'''
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef float vec3_t[3];
typedef int qboolean;
typedef unsigned char byte;
#define true 1
#define false 0
#define countof(x) ((int)(sizeof(x)/sizeof((x)[0])))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define q_min(a,b) ((a)<(b)?(a):(b))
#define CLAMP(lo,x,hi) q_max(lo,q_min(x,hi))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorSet(v,x,y,z) do { (v)[0]=x; (v)[1]=y; (v)[2]=z; } while(0)
#define VectorAdd(a,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]+(b)[k]; } while(0)
#define VectorSubtract(a,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]-(b)[k]; } while(0)
#define VectorMA(a,s,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]+(s)*(b)[k]; } while(0)
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
static float VectorLength(const vec3_t v) { return sqrtf(DotProduct(v,v)); }
static float VectorNormalize(vec3_t v) {
    float length=VectorLength(v);
    if(length) for(int k=0;k<3;++k) v[k]/=length;
    return length;
}
static void CrossProduct(const vec3_t a,const vec3_t b,vec3_t out) {
    out[0]=a[1]*b[2]-a[2]*b[1]; out[1]=a[2]*b[0]-a[0]*b[2]; out[2]=a[0]*b[1]-a[1]*b[0];
}
/* Controlled yaw-only poses, using Quake's right-axis convention. */
static void AngleVectors(vec3_t angles,vec3_t forward,vec3_t right,vec3_t up) {
    float yaw=angles[1]*0.017453292519943295f;
    VectorSet(forward,cosf(yaw),sinf(yaw),0);
    VectorSet(right,sinf(yaw),-cosf(yaw),0); VectorSet(up,0,0,1);
}
enum { SIGNONS=4, STAT_VIEWHEIGHT=0, STAT_HEALTH=1, IT_INVISIBILITY=128,
       VR_AIMMODE_CONTROLLER=7, mod_brush=1, SUBPASS_MAIN=1,
       PIPELINE_BASIC_NOTEX_BLEND=3, PIPELINE_COOP_NAMETAG=4,
       VK_SHADER_STAGE_ALL_GRAPHICS=31, VK_PIPELINE_BIND_POINT_GRAPHICS=0 };
typedef struct { float value; } cvar_t;
static cvar_t vr_crosshair,vr_crosshair_size,vr_crosshair_alpha,vr_crosshair_depth,vr_crosshairy,vr_aimmode;
static cvar_t r_drawviewmodel,r_drawentities,chase_active,scr_viewsize;
typedef struct { int firstclipnode,enabled; vec3_t normal; float distance; } hull_t;
typedef struct { int needload,type; hull_t hulls[1]; vec3_t mins,maxs,rmins,rmaxs; } qmodel_t;
typedef struct { qmodel_t *model; vec3_t origin,angles; } entity_t;
typedef struct { float fraction; vec3_t endpos; struct { vec3_t normal; } plane; } trace_t;
static struct { qmodel_t *worldmodel; entity_t *entities; int num_entities,intermission,items,stats[2];
                entity_t viewent; vec3_t viewangles; } cl;
static struct { int signon; } cls;
static int host_framecount,r_trace_line_cache_counter;
static qmodel_t world,door;
static entity_t entities[2];
static vec3_t seen_start[8],seen_end[8];
static int traces,hull_fault;
enum { FAULT_NONE,FAULT_ALLSOLID,FAULT_ZERO,FAULT_NAN_FRACTION,FAULT_NEGATIVE,
       FAULT_NAN_IMPACT,FAULT_NAN_NORMAL,FAULT_ZERO_NORMAL,FAULT_HUGE_NORMAL,FAULT_REVERSED_NORMAL };
static void *Mem_Realloc(void *memory,size_t size) { void *out=realloc(memory,size); assert(out); return out; }
/* Infinite synthetic solid half-space; production trace selection/transforms
 * are preserved, while the BSP recursive solver is deliberately controlled. */
static qboolean Q1BSP_RecursiveHullCheck(hull_t *hull,int node,float lo,float hi,
                                       vec3_t start,vec3_t end,trace_t *trace) {
    assert(node==0 && lo==0 && hi==1);
    if(hull==&world.hulls[0]) {
        assert(traces<countof(seen_start)); VectorCopy(start,seen_start[traces]);
        VectorCopy(end,seen_end[traces++]);
        if(hull_fault==FAULT_ALLSOLID) return false; /* Existing API can report this as a miss. */
        if(hull_fault==FAULT_ZERO) { trace->fraction=0; return false; }
        if(hull_fault==FAULT_NAN_FRACTION) { trace->fraction=NAN; return false; }
        if(hull_fault==FAULT_NEGATIVE) { trace->fraction=-1; return false; }
    }
    if(!hull->enabled) return true;
    float from=DotProduct(start,hull->normal)-hull->distance;
    float to=DotProduct(end,hull->normal)-hull->distance;
    if(from<=0 && to>=0 && to>from) {
        trace->fraction=-from/(to-from);
        vec3_t delta; VectorSubtract(end,start,delta);
        VectorMA(start,trace->fraction,delta,trace->endpos);
        for(int k=0;k<3;++k) trace->plane.normal[k]=-hull->normal[k];
        if(hull==&world.hulls[0]) {
            if(hull_fault==FAULT_NAN_IMPACT) trace->endpos[0]=NAN;
            if(hull_fault==FAULT_NAN_NORMAL) trace->plane.normal[0]=NAN;
            if(hull_fault==FAULT_ZERO_NORMAL) VectorSet(trace->plane.normal,0,0,0);
            if(hull_fault==FAULT_HUGE_NORMAL) trace->plane.normal[0]=FLT_MAX;
            if(hull_fault==FAULT_REVERSED_NORMAL) trace->plane.normal[0]=2;
        }
        return false;
    }
    return true;
}
typedef uintptr_t VkBuffer,VkDeviceSize,VkDescriptorSet;
typedef struct { vec3_t position; float texcoord[2]; byte color[4]; } basicvertex_t;
typedef struct { VkDescriptorSet descriptor_set; } gltexture_t;
typedef struct { int cb,subpass_type; struct { struct { uintptr_t handle; } layout; } current_pipeline; } cb_context_t;
static struct { qboolean stereo_active; float view_projection_matrix[16]; } vulkan_globals;
static gltexture_t white={79}, *whitetexture=&white;
static int glwidth=1280;
static struct { int width; } vid={640};
static struct { int width,height; } r_scene_vrect={1280,720};
static vec3_t r_origin,vpn={1,0,0},vright={0,-1,0},vup={0,0,1};
static vec3_t aim_starts[2],aim_forwards[2],prediction,muzzle_cue={40,0,0};
static int aim_count=1,aim_calls,units_calls,prediction_calls;
static float units=32;
static qboolean cue_active,calibration_active,prediction_missing;
static qboolean VR_WeaponCalibrationAdjustMuzzleCue(vec3_t out) { VectorCopy(muzzle_cue,out); return cue_active; }
static qboolean VR_WeaponCalibrationAdjustActive(void) { return calibration_active; }
static int VR_InputCrosshairAimRays(vec3_t starts[2],vec3_t forwards[2]) {
    ++aim_calls; memcpy(starts,aim_starts,sizeof(aim_starts)); memcpy(forwards,aim_forwards,sizeof(aim_forwards)); return aim_count;
}
static float V_VRUnitsPerMetre(void) { ++units_calls; return units; }
static const float *V_GetPredictionViewOffset(void) { ++prediction_calls; return prediction_missing?NULL:prediction; }
static qboolean V_UseTrackedView(void) { return true; }
static qboolean V_TrackedViewmodelShouldHide(void) { return false; }
static basicvertex_t drawn_vertices[12];
static int draw_calls,pipeline_calls,descriptor_calls,push_calls,fog_calls,vertex_calls,last_pipeline;
static void *R_VertexAllocate(size_t size,VkBuffer *buffer,VkDeviceSize *offset) {
    assert(size==sizeof(drawn_vertices)); *buffer=45; *offset=64; return drawn_vertices;
}
static void R_BindGraphicsPipeline(cb_context_t *cbx,int pipeline) {
    assert(cbx->subpass_type==SUBPASS_MAIN); ++pipeline_calls; last_pipeline=pipeline;
    cbx->current_pipeline.layout.handle=pipeline==PIPELINE_COOP_NAMETAG?91:92;
}
static void vkCmdBindDescriptorSets(int cb,int bind,uintptr_t layout,unsigned first,unsigned count,
                                    const VkDescriptorSet *sets,unsigned offsets,const unsigned *dynamic) {
    assert(cb==17 && bind==VK_PIPELINE_BIND_POINT_GRAPHICS && layout==91);
    assert(first==0 && count==1 && sets[0]==white.descriptor_set && offsets==0 && dynamic==NULL); ++descriptor_calls;
}
static void R_PushConstants(cb_context_t *cbx,int stage,unsigned offset,size_t size,const void *data) {
    assert(cbx->cb==17 && stage==VK_SHADER_STAGE_ALL_GRAPHICS && offset==0);
    assert(size==16*sizeof(float) && data==vulkan_globals.view_projection_matrix); ++push_calls;
}
static void Fog_DisableGFog(cb_context_t *cbx) { assert(cbx->cb==17); ++fog_calls; }
static void vkCmdBindVertexBuffers(int cb,unsigned first,unsigned count,const VkBuffer *buffers,const VkDeviceSize *offsets) {
    assert(cb==17 && first==0 && count==1 && buffers[0]==45 && offsets[0]==64); ++vertex_calls;
}
static void vkCmdDraw(int cb,unsigned count,unsigned instances,unsigned first,unsigned base) {
    assert(cb==17 && count==12 && instances==1 && first==0 && base==0);
    for(int j=0;j<12;++j) {
        for(int k=0;k<3;++k) assert(isfinite(drawn_vertices[j].position[k]));
        assert(drawn_vertices[j].color[0]==255 && drawn_vertices[j].color[1]==0 && drawn_vertices[j].color[2]==0);
        assert(drawn_vertices[j].texcoord[0]==0 && drawn_vertices[j].texcoord[1]==0);
    }
    ++draw_calls;
}
'''

MENU_SEAMS = r'''
static int vr_options_cursor=VR_OPT_CROSSHAIR_DEPTH;
static char menu_label[64],menu_value[64],menu_help[64];
static const char *va(const char *format,...) {
    static char value[64]; va_list args; va_start(args,format);
    vsnprintf(value,sizeof(value),format,args); va_end(args); return value;
}
static void M_Print(cb_context_t *cbx,int x,int y,const char *text) {
    assert(cbx->cb==17 && y==MENU_TOP+CHARACTER_SIZE*VR_OPT_CROSSHAIR_DEPTH);
    assert(x==MENU_LABEL_X || x==MENU_VALUE_X);
    snprintf(x==MENU_LABEL_X?menu_label:menu_value,64,"%s",text);
}
static void M_PrintWhite(cb_context_t *cbx,int x,int y,const char *text) {
    assert(cbx->cb==17 && x==16 && y==192); snprintf(menu_help,64,"%s",text);
}
'''

CHECKS = r'''
static cb_context_t cbx={.cb=17,.subpass_type=SUBPASS_MAIN};
static void near(float a,float b) { assert(isfinite(a) && fabsf(a-b)<0.0005f); }
static void reset(void) {
    memset(&world,0,sizeof(world)); memset(&door,0,sizeof(door)); memset(entities,0,sizeof(entities));
    memset(&cl,0,sizeof(cl)); memset(aim_starts,0,sizeof(aim_starts)); memset(aim_forwards,0,sizeof(aim_forwards));
    memset(prediction,0,sizeof(prediction)); memset(&vr_crosshair_frame,0,sizeof(vr_crosshair_frame));
    ++host_framecount; ++r_trace_line_cache_counter;
    cl.worldmodel=&world; cl.entities=entities; cl.num_entities=2; cls.signon=SIGNONS; cl.stats[STAT_HEALTH]=100;
    world.type=door.type=mod_brush; world.hulls[0].enabled=1; world.hulls[0].normal[0]=1; world.hulls[0].distance=100;
    vr_crosshair.value=1; vr_crosshair_size.value=3; vr_crosshair_alpha.value=0.25f;
    vr_crosshair_depth.value=0; vr_crosshairy.value=0; vr_aimmode.value=VR_AIMMODE_CONTROLLER;
    r_drawviewmodel.value=r_drawentities.value=1; chase_active.value=0; scr_viewsize.value=100;
    aim_forwards[0][0]=aim_forwards[1][0]=1; aim_count=1; units=32; vulkan_globals.stereo_active=true;
    cue_active=calibration_active=prediction_missing=false; VectorSet(muzzle_cue,40,0,0);
    traces=hull_fault=aim_calls=units_calls=prediction_calls=0;
    draw_calls=pipeline_calls=descriptor_calls=push_calls=fog_calls=vertex_calls=0; whitetexture=&white;
}
static void draw_pointer(int count,int pipeline) {
    int before=draw_calls,trace_before=traces,aim_before=aim_calls;
    R_DrawVRCrosshair(&cbx);
    assert(draw_calls-before==count && traces==trace_before && aim_calls==aim_before);
    if(count) assert(last_pipeline==pipeline && pipeline_calls==draw_calls && push_calls==draw_calls && fog_calls==draw_calls && vertex_calls==draw_calls);
}
static void surfaces_and_range(void) {
    reset(); R_PrepareVRCrosshair(); assert(traces==1 && vr_crosshair_frame.valid);
    near(seen_end[0][0],4096); near(vr_crosshair_frame.impact[0][0],100);
    near(vr_crosshair_frame.render_impact[0][0],100-1.0f/32);
    draw_pointer(1,PIPELINE_COOP_NAMETAG); assert(descriptor_calls==1);
    near(drawn_vertices[0].position[0],vr_crosshair_frame.render_impact[0][0]);
    assert(drawn_vertices[0].color[3]==64);
    reset(); vr_crosshair_depth.value=5; world.hulls[0].distance=2;
    R_PrepareVRCrosshair(); assert(traces==1 && units_calls==1 && vr_crosshair_frame.valid);
    near(seen_end[0][0],160); near(vr_crosshair_frame.impact[0][0],2); draw_pointer(1,PIPELINE_COOP_NAMETAG);
    reset(); vr_crosshair_depth.value=1; world.hulls[0].distance=64;
    R_PrepareVRCrosshair(); assert(traces==1 && !vr_crosshair_frame.valid && vr_crosshair_frame.ray_count==0); draw_pointer(0,0);
    reset(); world.hulls[0].enabled=0; R_PrepareVRCrosshair(); assert(traces==1 && !vr_crosshair_frame.valid); draw_pointer(0,0);
    reset(); vr_crosshair_depth.value=0.5f; vr_crosshairy.value=12; world.hulls[0].distance=6;
    R_PrepareVRCrosshair(); near(VectorLength(seen_end[0]),16); near(vr_crosshair_frame.impact[0][0],6); near(vr_crosshair_frame.impact[0][2],4.5f);
    reset(); vr_crosshairy.value=16; R_PrepareVRCrosshair(); near(vr_crosshair_frame.impact[0][2],100*16.0f/4096);
    reset(); vr_crosshair.value=2; vr_crosshair_depth.value=0.001f; vr_crosshairy.value=2;
    VectorSet(prediction,3,4,5); R_PrepareVRCrosshair(); assert(traces==1 && units_calls==0 && prediction_calls==1);
    near(seen_start[0][0],0); near(seen_end[0][2],20); near(vr_crosshair_frame.impact[0][2],100*20.0f/4096);
    near(vr_crosshair_frame.start[0][0],3); near(vr_crosshair_frame.start[0][2],5); draw_pointer(1,PIPELINE_COOP_NAMETAG);
    reset(); vr_crosshair.value=2; world.hulls[0].enabled=0; R_PrepareVRCrosshair();
    assert(vr_crosshair_frame.valid); near(vr_crosshair_frame.impact[0][0],4096); draw_pointer(1,PIPELINE_COOP_NAMETAG);
}
static void door_hits(void) {
    reset(); entities[1].model=&door; entities[1].origin[0]=20;
    door.hulls[0].enabled=1; door.hulls[0].normal[0]=1; door.hulls[0].distance=4;
    VectorSet(door.mins,-8,-8,-8); VectorSet(door.maxs,8,8,8);
    VectorSet(door.rmins,-8,-8,-8); VectorSet(door.rmaxs,8,8,8);
    R_PrepareVRCrosshair(); near(vr_crosshair_frame.impact[0][0],24);
    entities[1].origin[0]=30; ++host_framecount;
    R_PrepareVRCrosshair(); near(vr_crosshair_frame.impact[0][0],34);
    entities[1].angles[1]=90; VectorSet(door.hulls[0].normal,0,-1,0); door.hulls[0].distance=-4; ++host_framecount;
    R_PrepareVRCrosshair(); near(vr_crosshair_frame.impact[0][0],26);
    near(vr_crosshair_frame.render_impact[0][0],26-1.0f/32); draw_pointer(1,PIPELINE_COOP_NAMETAG);
    world.hulls[0].distance=10; ++host_framecount;
    R_PrepareVRCrosshair(); near(vr_crosshair_frame.impact[0][0],10);
}
static void compaction(void) {
    reset(); aim_count=2; aim_forwards[0][0]=-1; aim_starts[1][1]=7;
    R_PrepareVRCrosshair(); assert(traces==2 && vr_crosshair_frame.ray_count==1 && vr_crosshair_frame.valid);
    near(vr_crosshair_frame.start[0][1],7); near(vr_crosshair_frame.impact[0][1],7); draw_pointer(1,PIPELINE_COOP_NAMETAG);
    reset(); aim_count=2; aim_starts[1][1]=7; R_PrepareVRCrosshair();
    assert(traces==2 && vr_crosshair_frame.ray_count==2); draw_pointer(2,PIPELINE_COOP_NAMETAG); assert(descriptor_calls==2);
    reset(); aim_count=2; aim_forwards[1][0]=-1; R_PrepareVRCrosshair(); assert(vr_crosshair_frame.ray_count==1);
    reset(); aim_count=2; aim_starts[0][0]=NAN; R_PrepareVRCrosshair(); assert(traces==1 && vr_crosshair_frame.ray_count==1);
    reset(); aim_count=2; aim_forwards[0][0]=-1; aim_forwards[1][0]=-1;
    R_PrepareVRCrosshair(); assert(traces==2 && !vr_crosshair_frame.valid); draw_pointer(0,0);
}
static void guards_and_calibration(void) {
    for(int fault=FAULT_ALLSOLID;fault<=FAULT_HUGE_NORMAL;++fault) {
        reset(); hull_fault=fault; R_PrepareVRCrosshair(); assert(traces==1 && !vr_crosshair_frame.valid); draw_pointer(0,0);
    }
    reset(); hull_fault=FAULT_REVERSED_NORMAL; R_PrepareVRCrosshair();
    near(vr_crosshair_frame.impact[0][0],100); near(vr_crosshair_frame.render_impact[0][0],100-1.0f/32);
    reset(); aim_forwards[0][0]=0; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    reset(); aim_forwards[0][0]=INFINITY; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    reset(); aim_forwards[0][0]=FLT_MAX; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    reset(); vr_crosshair_depth.value=FLT_MAX; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    const float invalid_units[]={0,-1,NAN,INFINITY};
    for(int i=0;i<countof(invalid_units);++i) { reset(); units=invalid_units[i]; vr_crosshair_depth.value=2; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid); }
    reset(); vr_crosshair_depth.value=NAN; vr_crosshairy.value=NAN;
    R_PrepareVRCrosshair(); assert(traces==1 && vr_crosshair_frame.valid); near(seen_end[0][0],4096); near(seen_end[0][2],0);
    reset(); vr_crosshair.value=2; vr_crosshairy.value=FLT_MAX; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    reset(); vr_crosshair.value=2; prediction[0]=NAN; R_PrepareVRCrosshair(); assert(traces==1 && !vr_crosshair_frame.valid);
    reset(); vr_crosshair.value=2; prediction_missing=true; R_PrepareVRCrosshair(); assert(traces==1 && !vr_crosshair_frame.valid);
    reset(); cue_active=true; calibration_active=true; vr_crosshair.value=0; world.hulls[0].distance=2;
    R_PrepareVRCrosshair(); assert(!traces && !aim_calls && vr_crosshair_frame.overlay && vr_crosshair_frame.valid);
    near(vr_crosshair_frame.impact[0][0],40); draw_pointer(1,PIPELINE_BASIC_NOTEX_BLEND); assert(!descriptor_calls);
    cue_active=false; calibration_active=false; vr_crosshair.value=1; R_PrepareVRCrosshair();
    assert(traces==1 && !vr_crosshair_frame.overlay); draw_pointer(1,PIPELINE_COOP_NAMETAG); assert(descriptor_calls==1);
    reset(); cue_active=true; muzzle_cue[0]=NAN; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    reset(); calibration_active=true; R_PrepareVRCrosshair(); assert(!traces && !vr_crosshair_frame.valid);
    reset(); R_PrepareVRCrosshair(); whitetexture=NULL; draw_pointer(0,0);
    reset(); R_PrepareVRCrosshair(); vulkan_globals.stereo_active=false; R_PrepareVRCrosshair();
    assert(traces==1 && !vr_crosshair_frame.valid); draw_pointer(0,0);
    reset(); vr_aimmode.value=0; cl.stats[STAT_VIEWHEIGHT]=22; R_PrepareVRCrosshair();
    assert(traces==1 && !aim_calls); near(seen_start[0][2],-12); near(vr_crosshair_frame.impact[0][0],100);
    near(R_VRCrosshairHalfExtent(FLT_MAX,FLT_MAX,1),0);
}
static void menu_state(void) {
    reset(); draw_range_row(&cbx); assert(!strcmp(menu_label,"Dot Range") && !strcmp(menu_value,"auto"));
    assert(!strcmp(menu_help,"0 auto; >0 max metres; misses hide"));
    vr_crosshair_depth.value=2.5f; draw_range_row(&cbx); assert(!strcmp(menu_value,"max 2.5 m"));
    vr_crosshair_depth.value=NAN; draw_range_row(&cbx); assert(!strcmp(menu_value,"auto"));
    vr_options_cursor=0; menu_help[0]=0; draw_range_row(&cbx); assert(!menu_help[0]);
}
int main(void) {
    surfaces_and_range(); door_hits(); compaction(); guards_and_calibration(); menu_state();
    free(trace_line_bounds); free(trace_line_ents);
    puts("VR crosshair surfaces: production prep/trace/draw/menu checks passed"); return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="vr-crosshair-surface-") as directory:
        source = Path(directory) / "fixture.c"
        binary = Path(directory) / "fixture"
        source.write_text(harness())
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror", "-O0",
                                   str(source), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
