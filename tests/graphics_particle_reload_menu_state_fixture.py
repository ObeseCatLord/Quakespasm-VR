#!/usr/bin/env python3
"""Explicit end check of production particle callbacks/finalizer and menu format.

Resource synchronization, config-file parsing/loading and rendering are stubbed.
The real init, callbacks, type finalizer, cvar declarations and foveation formatter
are compiled and executed. This does not qualify the real config save/reload path,
particle pixels, runtime gaze, renderer resources or headset appearance.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, signature):
    begin = source.index(signature)
    return source[begin:source.index("\n}", begin) + 2]


def declaration(source, name):
    return re.search(r"^(?:static\s+)?cvar_t\s+" + re.escape(name) +
                     r"\s*=\s*\{[^\n]+?\};", source, re.MULTILINE).group(0)


def harness():
    source = (ROOT / "Quake/r_part_fte.c").read_text()
    menu = (ROOT / "Quake/menu.c").read_text()
    sky = (ROOT / "Quake/gl_sky.c").read_text()
    policy = (ROOT / "Quake/vr_foveation_policy.h").read_text()
    header = (ROOT / "Quake/cvar.h").read_text()
    begin = header.index("typedef enum\n{")
    flags = header[begin:header.index("} cvarflags_t;", begin) + len("} cvarflags_t;")]
    init = definition(source, "void PScript_InitParticles (void)")
    names = re.findall(r"Cvar_RegisterVariable \(&([a-z_]+)\)", init)
    cvars = "\n".join(declaration(source, name) for name in names)
    cvars += "\n" + "\n".join(declaration(sky, name) for name in (
        "r_fastsky", "r_skyalpha", "r_skyfog"))
    finalizer = definition(source, "static void FinishParticleType (part_type_t *ptype)\n{")
    callbacks = "\n".join(definition(source, signature) for signature in (
        "static void R_ParticleLooks_Callback (struct cvar_s *var)\n{",
        "static void R_ParticleDesc_Callback (struct cvar_s *var)\n{",
    ))
    formatter = definition(menu, "static void M_VROptions_FoveationValue (char *text,")
    parser = definition(policy, "static inline int VRF_RequestedMode(double requested_mode)")
    enum_begin = menu.index("typedef enum\n{\n\tGFX_GAMMA")
    options = menu[enum_begin:menu.index("} graphics_option_t;", enum_begin) + len("} graphics_option_t;")]
    sliders = definition(menu, "static qboolean M_GraphicsOptionIsSlider (graphics_option_t option)")
    adjust_begin = menu.index("\tcase GFX_FOV:\n", menu.index("static qboolean M_GraphicsAdjust ("))
    fov_case = menu[adjust_begin:menu.index("\tcase GFX_PALETTE:", adjust_begin)]
    fov_adjust = "\nstatic qboolean adjust_fov(int dir,qboolean mouse,float clamped_mouse) {\nqboolean changed=false;\nswitch(GFX_FOV) {\n" + fov_case + "}\nreturn changed;\n}\n"
    return PRELUDE + flags + CVAR_TYPE + cvars + SUPPORT + finalizer + LOADER + callbacks + init + parser + "\n" + formatter + options + sliders + fov_adjust + CHECKS


PRELUDE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int qboolean;
#define true 1
#define false 0
#define q_snprintf snprintf
#define DotProduct(a,b) ((a)[0]*(b)[0]+(a)[1]*(b)[1]+(a)[2]*(b)[2])
static size_t q_strlcpy(char *out,const char *text,size_t size) {
    size_t length=strlen(text), copied=size ? (length<size ? length : size-1) : 0;
    if(size) { memcpy(out,text,copied); out[copied]=0; }
    return length;
}
'''

CVAR_TYPE = r'''
struct cvar_s;
typedef void (*cvarcallback_t)(struct cvar_s *);
typedef struct cvar_s {
    const char *name, *string;
    cvarflags_t flags;
    float value;
    cvarcallback_t callback;
} cvar_t;
'''

SUPPORT = r'''
enum { PT_NORMAL,PT_SPARK,PT_TEXTUREDSPARK,PT_SPARKFAN,PT_BEAM,PT_INVISIBLE };
enum { PT_VELOCITY=1,PT_WORLDSPACERAND=2,PT_FRICTION=4,RAMP_NONE=0 };
typedef struct {
    float gravity,veladd,spawnvel,spawnvelvert,flurry,friction[3];
    float velwrand[3],velbias[3],orgwrand[3],dl_decay[4],dl_time,dl_radius[2];
    float scale,scalerand;
    struct { int type; float scalefactor,invscalefactor,stretch; } looks;
    int flags,rampmode;
    void *ramp;
    const char *config,*name;
} part_type_t;
static void P_LoadTexture(part_type_t *type,qboolean force) { (void)type; (void)force; }
static void Con_Printf(const char *format,...) { (void)format; }
static qboolean r_plooksdirty;
static part_type_t types[4];
static unsigned synchronizations,kills,loads,rebinds;
static qboolean synchronized;
static char loaded[3][64],com_token[1024];
static struct { int state; } cls;
typedef struct { const char *name; } qmodel_t;
static struct { qmodel_t *model_precache[2]; } cl;
enum { ca_connected=2 };
static void GL_SynchronizeEndRenderingTask(void) { ++synchronizations; synchronized=true; }
static void R_Particles_KillAllEffects(void) {
    assert(synchronized); ++kills; memset(types,0,sizeof(types));
}
static const char *COM_Parse(const char *text) {
    while(text && *text==' ') ++text;
    if(!text || !*text) return NULL;
    size_t length=0;
    while(*text && *text!=' ') com_token[length++]=*text++;
    com_token[length]=0;
    return text;
}
static void COM_FileBase(const char *path,char *out,size_t size) {
    const char *base=strrchr(path,'/'); base=base ? base+1 : path;
    size_t length=strcspn(base,"."); assert(length<size);
    memcpy(out,base,length); out[length]=0;
}
static void CL_RegisterParticles(void) { ++rebinds; synchronized=false; }
static void Cvar_RegisterVariable(cvar_t *var) {
    var->flags|=CVAR_REGISTERED; var->value=strtof(var->string,NULL);
}
static void Cvar_SetCallback(cvar_t *var,cvarcallback_t callback) {
    var->callback=callback;
    if(callback) var->flags|=CVAR_CALLBACK; else var->flags&=~CVAR_CALLBACK;
}
static void P_PartRedirect_f(void) {}
static void P_PartInfo_f(void) {}
static void P_BeamInfo_f(void) {}
static void Cmd_AddCommand(const char *name,void (*callback)(void)) { (void)name; (void)callback; }
enum { VRF_MODE_OFF,VRF_MODE_FIXED,VRF_MODE_EYE_TRACKED };
static cvar_t vr_foveation;
static struct {
    qboolean stereo_active,openxr_fragment_shading_rate_active,openxr_fragment_density_map_active;
} vulkan_globals;
static unsigned slider_calls;
static float desktop_fov=95;
static qboolean M_GraphicsSetSlider(const char *name,float minimum,float maximum,float step,
    qboolean inverted,qboolean mouse,float position,int direction) {
    assert(!strcmp(name,"fov") && minimum==80 && maximum==130 && step==5 && !inverted);
    (void)mouse; (void)position; ++slider_calls; desktop_fov+=direction*step; return true;
}
'''

LOADER = r'''
static qboolean P_LoadParticleSet(char *name,qboolean implicit,qboolean warning) {
    assert(!implicit);
    assert(warning == (strcmp(name,"map_start")!=0));
    snprintf(loaded[loads%3],sizeof(loaded[0]),"%s",name); ++loads;
    // Stub file contents supply original type definitions; production finalizer
    // determines their effective kind under the actual current cvar values.
    const int original[]={PT_SPARK,PT_BEAM,PT_TEXTUREDSPARK,PT_SPARKFAN};
    for(int i=0;i<4;++i) {
        types[i]=(part_type_t){.looks={.type=original[i],.scalefactor=1},.config="fixture",.name="type"};
        FinishParticleType(&types[i]);
    }
    return true;
}
'''

CHECKS = r'''
static void change(cvar_t *var,float value) {
    var->value=value; var->string=value ? "1" : "0";
    assert(var->callback); var->callback(var);
}
static void archive_preferences(void) {
    cvar_t *global[]={&r_part_rain,&r_particledesc,&r_part_rain_quantity,
        &r_part_sparks,&r_part_beams,&r_part_density,&r_fastsky};
    for(size_t i=0;i<sizeof(global)/sizeof(global[0]);++i) {
        assert(global[i]->flags&CVAR_ARCHIVE);
        assert(!(global[i]->flags&CVAR_ARCHIVE_GAME));
    }
    assert(r_fteparticles.flags&CVAR_ARCHIVE_GAME);
    assert(!(r_fteparticles.flags&CVAR_ARCHIVE));
    assert(r_skyalpha.flags&CVAR_ARCHIVE_GAME);
    assert(r_skyfog.flags&CVAR_ARCHIVE_GAME);
    assert(!(r_skyalpha.flags&CVAR_ARCHIVE) && !(r_skyfog.flags&CVAR_ARCHIVE));
    assert(!(r_part_sparks_textured.flags&(CVAR_ARCHIVE|CVAR_ARCHIVE_GAME)));
    assert(!(r_part_sparks_trifan.flags&(CVAR_ARCHIVE|CVAR_ARCHIVE_GAME)));
    puts("GRAPHICS_PARTICLE_SKY_ARCHIVE_FLAGS_PASSED");
}
static void particle_reload(void) {
    PScript_InitParticles();
    assert(r_part_sparks.callback && r_part_beams.callback);
    assert(!r_part_sparks_textured.callback && !r_part_sparks_trifan.callback);
    change(&r_part_sparks,0); assert(!synchronizations && !loads);
    qmodel_t world={"maps/start.bsp"}; cl.model_precache[1]=&world; cls.state=ca_connected;
    Cvar_SetCallback(&r_particledesc,R_ParticleDesc_Callback);
    r_particledesc.string="classic custom";
    change(&r_part_sparks,1);
    assert(types[0].looks.type==PT_SPARK && types[1].looks.type==PT_BEAM);
    change(&r_part_sparks,0);
    assert(types[0].looks.type==PT_INVISIBLE && types[1].looks.type==PT_BEAM);
    change(&r_part_beams,0);
    assert(types[0].looks.type==PT_INVISIBLE && types[1].looks.type==PT_INVISIBLE);
    change(&r_part_sparks,1);
    assert(types[0].looks.type==PT_SPARK && types[1].looks.type==PT_INVISIBLE);
    change(&r_part_beams,1);
    assert(types[0].looks.type==PT_SPARK && types[1].looks.type==PT_BEAM);
    assert(types[2].looks.type==PT_TEXTUREDSPARK && types[3].looks.type==PT_SPARKFAN);
    assert(synchronizations==5 && kills==5 && rebinds==5 && loads==15);
    assert(!strcmp(loaded[0],"classic") && !strcmp(loaded[1],"custom") && !strcmp(loaded[2],"map_start"));
    assert(!strcmp(r_particledesc.string,"classic custom") && r_plooksdirty);
    Cvar_SetCallback(&r_particledesc,NULL); change(&r_part_beams,0);
    assert(synchronizations==5 && loads==15);
    archive_preferences();
    puts("PARTICLE_TOGGLE_PRESET_RELOAD_ON_OFF_ON_PASSED");
}
static void foveation_value(void) {
    const char *expected_off[]={"off","fixed (off)","eye (off)"};
    const char *expected_configured[]={"off","fixed (paused)","eye (paused)"};
    for(int stereo=0;stereo<2;++stereo) for(int backend=0;backend<3;++backend) for(int mode=0;mode<3;++mode) {
        vulkan_globals.stereo_active=stereo;
        vulkan_globals.openxr_fragment_shading_rate_active=backend==1;
        vulkan_globals.openxr_fragment_density_map_active=backend==2;
        vr_foveation.value=mode;
        char value[16]; M_VROptions_FoveationValue(value,sizeof(value));
        assert(!strcmp(value,stereo && backend ? expected_configured[mode] : expected_off[mode]));
        assert(strlen(value)<=14);
    }
    const double invalid[]={NAN,INFINITY,-1,.5,3};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        vr_foveation.value=invalid[i];
        char value[16]; M_VROptions_FoveationValue(value,sizeof(value));
        assert(!strcmp(value,"off"));
    }
    puts("FOVEATION_MENU_REQUEST_CURRENT_STATE_PASSED");
}
static void fov_control(void) {
    vulkan_globals.stereo_active=0;
    assert(M_GraphicsOptionIsSlider(GFX_FOV));
    assert(adjust_fov(1,false,0)); assert(desktop_fov==100 && slider_calls==1);
    vulkan_globals.stereo_active=1;
    assert(!M_GraphicsOptionIsSlider(GFX_FOV));
    assert(!adjust_fov(-1,false,0)); assert(!adjust_fov(1,true,250));
    assert(desktop_fov==100 && slider_calls==1);
    vulkan_globals.stereo_active=0;
    assert(adjust_fov(-1,false,0)); assert(desktop_fov==95 && slider_calls==2);
    puts("FOV_RUNTIME_OWNED_DESKTOP_VALUE_PRESERVED_PASSED");
}
int main(void) {
    particle_reload(); foveation_value(); fov_control();
    puts("GRAPHICS_PARTICLE_RELOAD_MENU_STATE_PASSED"); return 0;
}
'''


def main():
    source_text = harness()
    with tempfile.TemporaryDirectory(prefix="qsvr-graphics-particle-menu-") as directory:
        source = Path(directory) / "fixture.c"
        binary = Path(directory) / "fixture"
        source.write_text(source_text)
        compiler = shlex.split(os.environ.get("CC", "cc"))
        subprocess.run(compiler + ["-std=c11", "-O0", "-Wall", "-Wextra", "-Werror",
                                  "-Wno-missing-field-initializers", str(source), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
