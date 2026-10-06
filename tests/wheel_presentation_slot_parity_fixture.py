#!/usr/bin/env python3
"""Explicitly run after integration: native tests of the actual wheel helpers.

Extracts bounded static helpers from vr_weapon_menu.c; no engine/game/runtime
startup. Visibility is a controllable stub, so this does not qualify rendering
or real world traces. Compilation and execution happen only in main().
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, start):
    begin = source.index(start)
    end = source.index("\n}", begin) + 2
    end = source.index(";", end) + 1 if start.startswith("typedef") else end
    return source[begin:end]


def harness():
    source = (ROOT / "Quake/vr_weapon_menu.c").read_text()
    protocol = (ROOT / "Quake/protocol.h").read_text()
    constants = "\n".join(line for line in source.splitlines() if line.startswith(
        ("#define VR_WEAPON_MENU_PLAYSPACE_MESH_SCALE ",
         "#define VR_WEAPON_MENU_PLAYSPACE_ACTION_GLYPH ",
         "#define VR_WEAPON_MENU_PLAYSPACE_ACTION_OUTLINE ")))
    scales = "\n".join(line for line in protocol.splitlines() if line.startswith(
        ("#define ENTSCALE_DEFAULT", "#define ENTSCALE_ENCODE", "#define ENTSCALE_DECODE")))
    types = "\n".join(definition(source, start) for start in (
        "typedef enum {\n\tVR_WEAPON_MENU_ACTION_QUICK_SAVE",
        "typedef struct {\n\tvr_weapon_menu_action_kind_t kind;"))
    helpers = "\n".join(definition(source, start) for start in (
        "static void VR_WeaponMenu_LayoutPlayspaceActions (",
        "static void VR_WeaponMenu_SlotWorldOrigin (",
        "static qboolean VR_WeaponMenu_PanelBasis (",
        "static qboolean VR_WeaponMenu_CenteredModelOrigin (",
        "static qboolean VR_WeaponMenu_WorldRayPoint (",
        "static int VR_WeaponMenu_HitWorld ("))
    return PRELUDE + constants + "\n" + scales + "\n" + types + "\n" + helpers + CHECKS


PRELUDE = r'''
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef float vec3_t[3];
typedef int qboolean;
#define true 1
#define false 0
#define MAX_SCOREBOARDNAME 32
#define DEG2RAD(x) ((x) * (3.14159265358979323846f / 180.0f))
#define CLAMP(lo,x,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
#define q_snprintf snprintf
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
#define VectorSubtract(a,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]-(b)[k]; } while(0)
#define VectorMA(a,s,b,c) do { for(int k=0;k<3;++k) (c)[k]=(a)[k]+(s)*(b)[k]; } while(0)
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
static float VectorNormalize(vec3_t v) {
    float n=sqrtf(DotProduct(v,v));
    if(n>0) for(int k=0;k<3;++k) v[k]/=n;
    return n;
}
static int glwidth=1280, glheight=720;
typedef struct { vec3_t mins, maxs; } qmodel_t;
typedef struct { float model_scale; vec3_t model_offset; } vr_weapon_menu_entry_t;
typedef struct {
    const vr_weapon_menu_entry_t *entry;
    float center_x, center_y;
    qboolean selectable;
} vr_weapon_menu_visible_t;
typedef struct {
    int count;
    vr_weapon_menu_visible_t visible[3];
    qmodel_t *model[3];
    void *geometry[3];
} vr_weapon_menu_frame_t;
static float blocked_x=FLT_MAX;
static qboolean VR_WeaponMenu_WorldContactVisible(const vec3_t start, const vec3_t target) {
    (void)start;
    return target[0]!=blocked_x;
}
static void near(float a,float b) { assert(fabsf(a-b)<0.0001f); }
'''

CHECKS = r'''
static void centering(void) {
    qmodel_t model={{20,-12,-4},{40,-4,12}};
    vec3_t slot={2,3,4}, center, origin;
    const float schemas[]={0.03125f,1.0f,1.7f};
    const float yaws[]={0,90,123};
    for(int a=0;a<3;++a) for(int b=0;b<3;++b) for(int selected=0;selected<2;++selected) {
        float draw=ENTSCALE_DECODE(ENTSCALE_ENCODE((selected?0.40f:0.25f)*schemas[a]))*0.28f;
        assert(VR_WeaponMenu_CenteredModelOrigin(&model,yaws[b],draw,slot,center,origin));
        float c=cosf(DEG2RAD(yaws[b])), s=sinf(DEG2RAD(yaws[b]));
        near(origin[0]+(center[0]*c-center[1]*s)*draw,slot[0]);
        near(origin[1]+(center[0]*s+center[1]*c)*draw,slot[1]);
        near(origin[2]+center[2]*draw,slot[2]);
    }
    model.mins[0]=NAN;
    assert(!VR_WeaponMenu_CenteredModelOrigin(&model,0,0.07f,slot,center,origin));
}
static void actions(void) {
    for(int mode=0;mode<2;++mode) {
        glwidth=mode?2160:1280; glheight=mode?2160:720;
        float scale=mode?0.025f:0.01f;
        float radius=mode?15:5;
        vr_weapon_menu_action_t a[5]={0};
        a[0].kind=VR_WEAPON_MENU_ACTION_QUICK_SAVE;
        a[1].kind=VR_WEAPON_MENU_ACTION_QUICK_LOAD;
        a[2].kind=a[3].kind=VR_WEAPON_MENU_ACTION_COOP_PLAYER;
        strcpy(a[2].player_name,"Player one"); a[2].slot=3; a[2].id=30;
        strcpy(a[3].player_name,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcde"); a[3].slot=4; a[3].id=40;
        a[4].kind=VR_WEAPON_MENU_ACTION_COOP_SPAWN; a[4].id=50;
        VR_WeaponMenu_LayoutPlayspaceActions(a,5,radius/scale,scale);
        assert(a[2].kind==VR_WEAPON_MENU_ACTION_COOP_SPAWN && a[2].id==50);
        assert(a[3].slot==3 && a[3].id==30 && a[4].slot==4 && a[4].id==40);
        assert(strlen(a[4].label)==33); /* full name plus prefix */
        near((a[0].left+a[0].width-glwidth*0.5f)*scale-0.28f,-radius-6);
        near((a[3].left-glwidth*0.5f)*scale+0.28f,radius+6);
        near((a[1].top-a[0].top)*scale,3);
        near((a[3].top-a[2].top)*scale,3);
        near((a[4].top-a[3].top)*scale,3);
        for(int i=0;i<5;++i) {
            near(a[i].height*scale,1.44f+0.56f);
            near(a[i].width*scale,strlen(a[i].label)*1.44f+0.56f);
        }
        vr_weapon_menu_action_t spawn={0}; spawn.kind=VR_WEAPON_MENU_ACTION_COOP_SPAWN;
        VR_WeaponMenu_LayoutPlayspaceActions(&spawn,1,radius/scale,scale);
        near(spawn.top+spawn.height*0.5f,glheight*0.5f);
    }
}
static void picking(void) {
    glwidth=1280; glheight=720;
    float m[16]={0}; m[0]=6.4f; m[5]=3.6f; m[10]=0.01f; m[14]=10; m[15]=1;
    qmodel_t huge={{-100000,-100000,-100000},{100000,100000,100000}}, small={{-1,-1,-1},{1,1,1}};
    vr_weapon_menu_entry_t entries[2]={0}; entries[0].model_scale=100;
    vr_weapon_menu_frame_t f={0}; f.count=2;
    for(int i=0;i<2;++i) {
        f.visible[i].entry=&entries[i]; f.visible[i].selectable=true;
        f.visible[i].center_x=640+i*500; f.visible[i].center_y=360;
        f.geometry[i]=&small; f.model[i]=i?&small:&huge;
    }
    vec3_t start={0,0,0}, ray={5,0,10};
    assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==1);
    huge.maxs[0]=1e8f; entries[0].model_scale=0.001f;
    assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==1);
    ray[0]=2.4f; assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==0);
    blocked_x=0; assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==1);
    blocked_x=FLT_MAX; f.visible[0].selectable=false;
    assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==1);
    ray[0]=8; assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==-1);
    f.visible[0].selectable=true; entries[0].model_offset[0]=10;
    ray[0]=2.8f; assert(VR_WeaponMenu_HitWorld(&f,m,start,ray)==0);
    vec3_t right,down,forward,point;
    assert(VR_WeaponMenu_PanelBasis(m,right,down,forward));
    ray[0]=-30; assert(VR_WeaponMenu_WorldRayPoint(m,start,ray,forward,point));
    near(point[0],-30); /* labels outside canvas remain projectable */
    ray[2]=0; assert(!VR_WeaponMenu_WorldRayPoint(m,start,ray,forward,point));
    ray[2]=-10; assert(!VR_WeaponMenu_WorldRayPoint(m,start,ray,forward,point));
    ray[0]=NAN; assert(!VR_WeaponMenu_WorldRayPoint(m,start,ray,forward,point));
}
int main(void) { centering(); actions(); picking(); puts("WHEEL_PRESENTATION_SLOT_PARITY_PASS"); }
'''


def main():
    cc = shlex.split(os.environ.get("CC", "cc"))
    with tempfile.TemporaryDirectory(prefix="wheel-slot-parity-") as directory:
        path = Path(directory)
        cfile, binary = path / "fixture.c", path / "fixture"
        cfile.write_text(harness())
        subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            str(cfile), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
