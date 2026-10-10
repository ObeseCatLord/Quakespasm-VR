#!/usr/bin/env python3
"""Run at coordinated end checks: numerical production lighting regression.

Extracts the real CPU atlas update/build/accumulate/store/dlight functions and
alias-lighting function. Only renderer resources, light sampling, and pose lookup
are stubbed. No engine, game data, Vulkan, headset or source-string assertions.
The scalar atlas arithmetic is production code; SIMD and GPU upload are excluded.
Compilation and execution occur only when this script is explicitly run.
"""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, signature):
    begin = source.index(signature)
    end = source.index("\n}", begin) + 2
    return source[begin:end]


def harness():
    brush = (ROOT / "Quake/r_brush.c").read_text()
    alias = (ROOT / "Quake/r_alias.c").read_text()
    functions = "\n\n".join(definition(brush, name) for name in (
        "void R_AddDynamicLights (msurface_t *surf)",
        "void R_AccumulateLightmap (byte *lightmap, unsigned scale, int texels)",
        "void R_StoreLightmap (byte *dest, int width, int height, int stride)",
        "void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)",
        "void R_RenderDynamicLightmaps (msurface_t *fa)",
    ))
    functions += "\n\n" + definition(alias, "static void R_SetupAliasLighting (entity_t *e,")
    return PRELUDE + functions + CHECKS


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char byte;
typedef int qboolean;
typedef float vec3_t[3];
typedef struct { float value; } cvar_t;
#define MAXLIGHTMAPS 4
#define MAX_DLIGHTS 4
#define LMBLOCK_WIDTH 16
#define LMBLOCK_HEIGHT 16
#define LIGHTMAP_BYTES 4
#define SURF_DRAWTILED 1
#define YAW 1
#define SHADEDOT_QUANT 16
#define q_min(a,b) ((a)<(b)?(a):(b))
#define q_max(a,b) ((a)>(b)?(a):(b))
#define CLAMP(lo,x,hi) q_min(q_max((lo),(x)),(hi))
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorSubtract(a,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]-(b)[k]; } while(0)
#define VectorMA(a,s,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]+(s)*(b)[k]; } while(0)
#define VectorScale(a,s,b) do { for(int k=0;k<3;++k) (b)[k]=(a)[k]*(s); } while(0)
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
static float VectorLength(const vec3_t v) { return sqrtf(DotProduct(v,v)); }
static float VectorNormalize(vec3_t v) {
    float length=VectorLength(v);
    if(length>0) for(int k=0;k<3;++k) v[k]/=length;
    return length;
}
typedef struct { vec3_t normal; float dist; } plane_t;
typedef struct { float vecs[2][4]; } mtexinfo_t;
typedef struct {
    int flags, extents[2], texturemins[2], lightmaptexturenum, light_s, light_t;
    int dlightframe;
    qboolean cached_dlight;
    byte styles[MAXLIGHTMAPS];
    int cached_light[MAXLIGHTMAPS];
    unsigned dlightbits[1];
    byte *samples;
    plane_t *plane;
    mtexinfo_t *texinfo;
} msurface_t;
typedef struct { int l,t,w,h; } glRect_t;
struct lightmap_s { qboolean modified[1]; glRect_t rectchange; byte *data; };
typedef struct { vec3_t maxs; byte *lightdata; } qmodel_t;
typedef struct { vec3_t origin,angles; qmodel_t *model; int lightcache; } entity_t;
typedef struct {
    vec3_t origin,color,cone_dir;
    float radius,minlight,cone_cos,kex_intensity;
    double die;
} dlight_t;
typedef struct { qboolean tracked_root_valid; float tracked_root_yaw; } r_vrik_prepared_palette_t;
static struct { double time; int maxclients; entity_t *entities; entity_t viewent; qmodel_t *worldmodel; } cl;
static entity_t entities[4];
static cvar_t r_dynamic={1};
static int r_framecount=1, d_lightstylevalue[256];
static dlight_t cl_dlights[MAX_DLIGHTS];
static vec3_t lightmap_dlight_origins[MAX_DLIGHTS];
static unsigned blocklights[12];
static byte atlas[LMBLOCK_WIDTH*LMBLOCK_HEIGHT*LIGHTMAP_BYTES];
static struct lightmap_s lightmaps[1];
static int Tasks_GetWorkerIndex(void) { return 0; }
static qboolean R_LightPoint(const vec3_t origin,float offset,int *cache,vec3_t *color) {
    (void)origin; (void)offset; (void)cache;
    (*color)[0]=40; (*color)[1]=50; (*color)[2]=60;
    return 1;
}
static int V_AkimboViewmodelHand(const entity_t *e) { (void)e; return -1; }
static qboolean V_HeldMeleeRenderEntity(const entity_t *e) { (void)e; return 0; }
static const r_vrik_prepared_palette_t *R_VRIKRenderLookup(const entity_t *e) {
    (void)e; return NULL;
}
'''


CHECKS = r'''
static byte samples[]={20,30,40,12,16,20};
static plane_t plane={{0,0,1},0};
static mtexinfo_t tex={{{1,0,0,0},{0,1,0,0}}};
static qmodel_t world={{0,0,16},samples};
static void clear_dirty(void) {
    lightmaps[0].modified[0]=0;
    lightmaps[0].rectchange=(glRect_t){LMBLOCK_WIDTH,LMBLOCK_HEIGHT,0,0};
}
static byte *pixel(const msurface_t *s) {
    return atlas+(s->light_t*LMBLOCK_WIDTH+s->light_s)*LIGHTMAP_BYTES;
}
static void expect_rgb(const msurface_t *s,int r,int g,int b) {
    const byte *p=pixel(s);
    assert(p[0]==r && p[1]==g && p[2]==b && p[3]==255);
}
static void cpu_atlas(void) {
    msurface_t lit={.light_s=2,.light_t=3,.samples=samples,.plane=&plane,.texinfo=&tex,
        .styles={0,1,255,255},.dlightframe=-1,.dlightbits={1}};
    msurface_t unlit=lit; unlit.light_s=9; unlit.light_t=7;
    memset(atlas,0xa5,sizeof(atlas));
    lightmaps[0].data=atlas; cl.worldmodel=&world;
    d_lightstylevalue[0]=256; d_lightstylevalue[1]=128; r_dynamic.value=1;
    R_BuildLightMap(&lit,pixel(&lit),LMBLOCK_WIDTH*LIGHTMAP_BYTES);
    R_BuildLightMap(&unlit,pixel(&unlit),LMBLOCK_WIDTH*LIGHTMAP_BYTES);
    expect_rgb(&lit,26,38,50); expect_rgb(&unlit,26,38,50);
    cl_dlights[0]=(dlight_t){.radius=24,.color={1,.5f,.25f},.origin={0,0,8},.cone_cos=-1,.die=10};
    VectorCopy(cl_dlights[0].origin,lightmap_dlight_origins[0]);
    lit.dlightframe=r_framecount;
    clear_dirty(); R_RenderDynamicLightmaps(&lit);
    expect_rgb(&lit,42,46,54); assert(lit.cached_dlight && lightmaps[0].modified[0]);
    // New off-state averages deliberately differ from the two cached scales.
    r_dynamic.value=0; d_lightstylevalue[0]=768; d_lightstylevalue[1]=64;
    clear_dirty(); R_RenderDynamicLightmaps(&lit);
    expect_rgb(&lit,26,38,50); assert(!lit.cached_dlight);
    assert(lit.cached_light[0]==256 && lit.cached_light[1]==128);
    assert(lightmaps[0].modified[0]);
    assert(lightmaps[0].rectchange.l==2 && lightmaps[0].rectchange.t==3);
    assert(lightmaps[0].rectchange.w==1 && lightmaps[0].rectchange.h==1);
    // Never-lit CPU surfaces remain frozen; current-frame marks cannot relight off.
    unlit.dlightframe=r_framecount;
    clear_dirty(); R_RenderDynamicLightmaps(&unlit);
    expect_rgb(&unlit,26,38,50); assert(!lightmaps[0].modified[0]);
    for(int frame=0;frame<3;++frame) {
        ++r_framecount; lit.dlightframe=r_framecount;
        R_RenderDynamicLightmaps(&lit);
        expect_rgb(&lit,26,38,50); assert(!lightmaps[0].modified[0]);
    }
    // Re-enable resumes the normal current baked scales plus the same live light.
    r_dynamic.value=1; R_RenderDynamicLightmaps(&lit);
    expect_rgb(&lit,79,102,129); assert(lit.cached_dlight);
    // Natural expiry while enabled retires dynamic contribution normally.
    ++r_framecount; R_RenderDynamicLightmaps(&lit);
    expect_rgb(&lit,63,94,125); assert(!lit.cached_dlight);
    // A direct build with no cached dlights initializes styles without adding light.
    r_dynamic.value=0; lit.dlightframe=r_framecount;
    R_BuildLightMap(&lit,pixel(&lit),LMBLOCK_WIDTH*LIGHTMAP_BYTES);
    expect_rgb(&lit,63,94,125); assert(!lit.cached_dlight);
    // Unrelated atlas bytes retain their sentinel through every single-texel write.
    for(size_t i=0;i<sizeof(atlas);++i) {
        size_t a=(size_t)(pixel(&lit)-atlas), b=(size_t)(pixel(&unlit)-atlas);
        if((i<a || i>=a+4) && (i<b || i>=b+4)) assert(atlas[i]==0xa5);
    }
    puts("CPU_ATLAS_ON_OFF_ON_FROZEN_STYLES_PASSED");
}
static void near(float a,float b) { assert(fabsf(a-b)<0.00001f); }
static void alias_lighting(void) {
    cl.entities=entities; cl.maxclients=0; cl.time=1;
    entities[2].model=&world;
    vec3_t shade,color;
    cl_dlights[0]=(dlight_t){.radius=10,.color={1,.5f,.25f},.cone_cos=-1,.die=10};
    for(int kex=0;kex<2;++kex) {
        // On-axis KEX cone exercises range-normalized intensity while enabled.
        cl_dlights[0].kex_intensity=kex ? 10.0f/128.0f : 0;
        cl_dlights[0].cone_cos=kex ? .5f : -1;
        cl_dlights[0].cone_dir[0]=1; cl_dlights[0].origin[0]=kex ? -1 : 0;
        cl_dlights[0].radius=kex ? 11 : 10;
        r_dynamic.value=1; R_SetupAliasLighting(&entities[2],&shade,&color);
        float add=kex ? 10.0f*10.0f/11.0f : 10.0f;
        near(color[0],(40+add)/200); near(color[1],(50+add*.5f)/200);
        near(color[2],(60+add*.25f)/200);
        r_dynamic.value=0; R_SetupAliasLighting(&entities[2],&shade,&color);
        near(color[0],.2f); near(color[1],.25f); near(color[2],.3f);
        r_dynamic.value=1; cl_dlights[0].die=0;
        R_SetupAliasLighting(&entities[2],&shade,&color);
        near(color[0],.2f); near(color[1],.25f); near(color[2],.3f);
        cl_dlights[0].die=10;
    }
    puts("ALIAS_DLIGHT_OFF_RETAINS_BAKED_PASSED");
}
int main(void) {
    cpu_atlas(); alias_lighting();
    puts("GRAPHICS_DYNAMIC_OFF_FROZEN_STYLES_PASSED");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="qsvr-graphics-dynamic-off-") as directory:
        source = Path(directory) / "fixture.c"
        binary = Path(directory) / "fixture"
        source.write_text(harness())
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                                  str(source), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
