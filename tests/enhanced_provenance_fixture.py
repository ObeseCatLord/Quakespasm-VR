#!/usr/bin/env python3
"""Coordinated end check for the bounded enhanced-model provenance adapter.

Extracts production VFS lookup, mount metadata, companion selection, memory
reset, geometry query and the calibration owner's pure factor. Synthetic packs,
file I/O, fingerprint result, GPU resources and model decoders are stubs. This
qualifies metadata propagation/selection, not official asset fingerprints or
runtime rendering. Payload proof is a decoder seam here; real loaded bytes
and ready skinning are covered by md5_weapon_geometry_fixture.py.
No compilation or execution occurs before an explicit invocation.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(source, signature):
    # Definitions may have an earlier prototype; require a brace before ';'.
    begin = -1
    while True:
        begin = source.index(signature, begin + 1)
        brace = source.index("{", begin)
        if ";" not in source[begin:brace]:
            break
    return source[begin:source.index("\n}", brace) + 2]


def harness():
    common = (ROOT / "Quake/common.c").read_text()
    header = (ROOT / "Quake/common.h").read_text()
    model = (ROOT / "Quake/gl_model.c").read_text()
    calibration = (ROOT / "Quake/vr_weapon_calibration.c").read_text()
    types = header[header.index("// QUAKEFS") : header.index("extern searchpath_t *com_searchpaths;")]
    disk_types = common[common.index("typedef struct\n{\n\tchar name[56];") : common.index("static qboolean COM_ValidatePackDirectoryEntries")]
    constants = "\n".join(line for line in common.splitlines() if line.startswith("#define PAK0_"))
    vfs = "\n\n".join(definition(common, name) for name in (
        "qboolean COM_IsRereleaseModelAsset (", "static qfilesize_t COM_FindFile (",
        "qboolean COM_FileExists (", "qboolean COM_FileExistsEx (",
        "static qboolean COM_ValidatePackDirectoryEntries (", "static pack_t *COM_LoadPackFile (",
        "static void COM_AddRereleaseModelPack ("))
    models = "\n\n".join(definition(model, name) for name in (
        "static void Mod_ResetMD5WeaponGeometry (",
        "static void Mod_FreeModelMemory (", "static qmodel_t *Mod_LoadModel (",
        "void *Mod_Extradata_CheckSkin (", "qboolean Mod_IsRereleaseReplacementGeometry ("))
    factor = definition(calibration, "float VR_WeaponCalibrationModelOffsetScale(")
    return PRELUDE + types + disk_types + constants + IO_STUBS + vfs + MODEL_STUBS + models + factor + CHECKS


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <math.h>
typedef unsigned char byte;
typedef int qboolean;
typedef long long qfilesize_t;
#define MAX_QPATH 64
#define MAX_OSPATH 256
#define FS_ENT_FILE 1
static int q_snprintf(char *str,size_t size,const char *format,...)
    __attribute__((format(printf,3,4)));
static int q_snprintf(char *str,size_t size,const char *format,...) {
    va_list args;
    va_start(args,format);
    int ret=vsnprintf(str,size,format,args);
    va_end(args);
    if(ret<0) ret=(int)size;
    if(size && (size_t)ret>=size) str[size-1]='\0';
    return ret;
}
#define q_strcasecmp strcasecmp
#define q_strncasecmp strncasecmp
#define LittleLong(x) (x)
#define Mem_Free free
#define SAFE_FREE(p) do { free(p); (p)=NULL; } while(0)
static void *Mem_Alloc(size_t size) { void *p=calloc(1,size); assert(p); return p; }
static void q_strlcpy(char *out,const char *in,size_t size) { assert(size); snprintf(out,size,"%s",in); }
static const char *COM_FileGetExtension(const char *name) { const char *p=strrchr(name,'.'); return p?p+1:""; }
static void COM_StripExtension(const char *in,char *out,size_t size) {
    q_strlcpy(out,in,size); char *dot=strrchr(out,'.'); if(dot) *dot=0;
}
static void COM_AddExtension(char *name,const char *extension,size_t size) {
    assert(strlen(name)+strlen(extension)<size); strcat(name,extension);
}
static void COM_FileBase(const char *name,char *out,size_t size) { q_strlcpy(out,name,size); }
static void Sys_Error(const char *format,...) { (void)format; abort(); }
#define Host_Error Sys_Error
#define Sys_Printf(...) ((void)0)
#define Con_DPrintf(...) ((void)0)
#define Con_Warning(...) ((void)0)
#define Con_Printf(...) ((void)0)
static struct { float value; } registered={1},developer={0};
qboolean COM_FileExistsEx(const char *,unsigned int *,qboolean *);
'''

IO_STUBS = r'''
static searchpath_t *com_searchpaths;
static qfilesize_t com_filesize;
static int file_from_pak,io_calls,verify_calls,closed_handles;
static qboolean com_modified,verified_result=true,loose_md5=false;
static dpackheader_t disk_header={{'P','A','C','K'},12,64};
static dpackfile_t disk_entry={"progs/v_shot.md5mesh",100,4};
static int disk_cursor;
static int Sys_FileSeek(int handle,int offset) { (void)handle; ++io_calls; disk_cursor=offset; return 0; }
static int Sys_FileRead(int handle,void *out,int count) {
    (void)handle; ++io_calls;
    if(disk_cursor==0 && count==(int)sizeof(disk_header)) memcpy(out,&disk_header,count);
    else if(disk_cursor==12 && count==(int)sizeof(disk_entry)) memcpy(out,&disk_entry,count);
    else abort();
    disk_cursor+=count; return count;
}
static void Sys_FileClose(int handle) { (void)handle; ++closed_handles; }
static qfilesize_t Sys_FileOpenRead(const char *name,int *handle) {
    (void)name; ++io_calls; *handle=1; return PAK0_SIZE_RERELEASE;
}
static int Sys_DuplicateHandle(int handle) { ++io_calls; return handle+1; }
static FILE *Sys_fopen(const char *name,const char *mode) { (void)name; (void)mode; ++io_calls; return NULL; }
static int Sys_fseek(FILE *file,int offset,int whence) { (void)file; (void)offset; (void)whence; ++io_calls; return 0; }
static qfilesize_t COM_filelength(FILE *file) { (void)file; ++io_calls; return 4; }
static int Sys_FileType(const char *name) {
    ++io_calls; return loose_md5 && !strcmp(name,"fixture/progs/v_shot.md5mesh") ? FS_ENT_FILE : 0;
}
static void CRC_Init(unsigned short *crc) { *crc=0; }
static void CRC_ProcessByte(unsigned short *crc,byte value) { (void)crc; (void)value; }
static qboolean COM_VerifyRereleaseModelPack(int handle,qfilesize_t size) {
    assert(handle==1 && size==PAK0_SIZE_RERELEASE); ++verify_calls; return verified_result;
}
'''

MODEL_STUBS = r'''
typedef enum { PV_QUAKE1,PV_QUAKE3,PV_MD5,PV_MD5_8,PV_SIZE } poseverttype_t;
typedef struct { poseverttype_t poseverttype; int numskins; } aliashdr_t;
typedef void msprite_t;
enum { mod_alias,mod_sprite,mod_brush };
enum { IDPOLYHEADER=1,IDSPRITEHEADER=2,IDMD5HEADER=3,IDMD3HEADER=4 };
typedef struct { void *polys; } fixture_surface_t;
typedef struct {
    char name[MAX_QPATH]; unsigned int path_id;
    qboolean needload,is_generated_akimbo_half,rerelease_md5_companion,avatar_builtin;
    qboolean rerelease_md5_weapon_payload;
    int type,avatar_custom_id,qbj3_palm_pose_count,numtextures,numsurfaces;
    int numsubmodels,numplanes,numleafs,numvertexes,numedges,numnodes,numtexinfo;
    int numsurfedges,numclipnodes,nummarksurfaces,used_water_surfs,water_surfs_specials;
    byte *extradata[PV_SIZE],*avatar_custom_rgba[2];
    void *avatar_bind_surfaces,*avatar_prop_gpu,*avatar_props,*qbj3_palm_centroids;
    void **textures; fixture_surface_t *surfaces;
    struct { void *clipnodes; } hulls[1];
    void *submodels,*planes,*leafs,*vertexes,*edges,*nodes,*texinfo,*surfedges;
    void *clipnodes,*marksurfaces,*soa_leafbounds,*surfvis,*stereo_vis,*soa_surfplanes;
    void *visdata,*lightdata,*entities,*md5_skeleton,*water_surfs;
    size_t lightdata_bytes; int stockaxe_edge,md5_stockaxe_edge;
} qmodel_t;
static struct { float value; } r_enhancedmodels={1},r_allow_replacement_md5models={1},r_allow_replacement_md3models={1};
static qboolean isDedicated=true,fail_md5=false;
/* Metadata-only decoder seam; the separate payload fixture uses real bytes. */
static qboolean payload_proof=true;
static void Mod_ResetMD5WeaponGeometry(qmodel_t *m);
static int decoder_calls;
static byte *COM_LoadFile(const char *name,unsigned int *path_id) {
    if(COM_FindFile(name,NULL,NULL,path_id,NULL)<0) return NULL;
    int magic=!strcmp(COM_FileGetExtension(name),"md5mesh")?IDMD5HEADER:
        !strcmp(COM_FileGetExtension(name),"md3")?IDMD3HEADER:IDPOLYHEADER;
    byte *data=Mem_Alloc(sizeof(int)); memcpy(data,&magic,sizeof(int)); com_filesize=sizeof(int); return data;
}
static void Mod_FreeAvatarBindSurfaces(void *p) { free(p); }
static void Mod_FreeAvatarPropGPU(void *p) { free(p); }
static void Mod_FreeAvatarProps(void *p) { free(p); }
static void TexMgr_FreeTexturesForOwner(qmodel_t *m) { (void)m; }
static void Mod_FreeSpriteMemory(void *p) { (void)p; }
static void GLMesh_DeleteMeshBuffers(aliashdr_t *h) { (void)h; }
static void InvalidateTraceLineCache(void) {}
static byte *Mod_GenerateVRHeldModel(const char *n,unsigned int *p,size_t *s,const char **skin) {
    (void)n;(void)p;(void)s;(void)skin; return NULL;
}
static void *Mod_AkimboPairForHalf(const char *n,void *h) { (void)n;(void)h; return NULL; }
static void *Mod_HeldMeleeRecipeForName(const char *n) { (void)n; return NULL; }
static aliashdr_t *new_header(poseverttype_t format) {
    aliashdr_t *h=Mem_Alloc(sizeof(*h)); h->poseverttype=format; h->numskins=1; return h;
}
static qboolean Mod_LoadMD5MeshModel(qmodel_t *m,const void *b,const char *n,qfilesize_t s,const void *a,int as) {
    (void)b;(void)n;(void)s;(void)a;(void)as; ++decoder_calls;
    Mod_ResetMD5WeaponGeometry(m);
    /* Nested skin/animation lookups must not clobber the retained mesh source. */
    COM_FileExists("progs/missing_skin.lmp",NULL);
    if(fail_md5) return false;
    m->rerelease_md5_weapon_payload=payload_proof;
    m->type=mod_alias; m->extradata[PV_MD5]=(byte *)new_header(PV_MD5); return true;
}
static void Mod_LoadMD3Model(qmodel_t *m,const void *b,qfilesize_t s) {
    (void)b;(void)s; ++decoder_calls; m->type=mod_alias; m->extradata[PV_QUAKE3]=(byte *)new_header(PV_QUAKE3);
}
static void Mod_LoadAliasModel(qmodel_t *m,void *b,qfilesize_t s,const char *skin) {
    (void)b;(void)s;(void)skin; ++decoder_calls; m->type=mod_alias; m->extradata[PV_QUAKE1]=(byte *)new_header(PV_QUAKE1);
}
static void Mod_LoadSpriteModel(qmodel_t *m,void *b) { (void)b; m->type=mod_sprite; }
static void Mod_LoadBrushModel(qmodel_t *m,const char *n,void *b,qfilesize_t s) {
    (void)n;(void)b;(void)s; m->type=mod_brush;
}
'''

CHECKS = r'''
static packfile_t official_files[]={
    {"progs/v_shot.mdl",100,4}, {"progs/v_shot.md5mesh",104,4}, {"maps/start.bsp",108,4}
};
static packfile_t custom_files[]={ {"progs/v_shot.md5mesh",100,4} };
static pack_t official={.handle=1,.numfiles=3,.files=official_files,.rerelease_model_source=true};
static pack_t custom={.handle=2,.numfiles=1,.files=custom_files,.rerelease_model_source=false};
static searchpath_t base={.path_id=1,.pack=&official};
static searchpath_t higher={.path_id=1,.pack=&custom,.next=&base};
static searchpath_t loose={.path_id=1,.filename="fixture",.next=&base};
static qmodel_t weapon(void) {
    qmodel_t m={.needload=true,.type=mod_alias}; q_strlcpy(m.name,"progs/v_shot.mdl",sizeof(m.name)); return m;
}
static void retire_pack(pack_t *p) { Sys_FileClose(p->handle); free(p->files); free(p); }
static void mounts(void) {
    verify_calls=0; verified_result=true;
    pack_t *p=COM_LoadPackFile("synthetic",1,PAK0_SIZE_RERELEASE,false);
    assert(p && p->rerelease_model_source && verify_calls==1); retire_pack(p);
    verified_result=false;
    p=COM_LoadPackFile("synthetic",1,PAK0_SIZE_RERELEASE,false);
    assert(p && !p->rerelease_model_source && verify_calls==2); retire_pack(p);
    p=COM_LoadPackFile("custom",1,512,false);
    assert(p && !p->rerelease_model_source && verify_calls==2); retire_pack(p);
    com_searchpaths=NULL; verified_result=true; com_modified=false; verify_calls=0;
    COM_AddRereleaseModelPack("synthetic-root");
    assert(com_searchpaths && com_searchpaths->pack->rerelease_model_source);
    assert(com_searchpaths->rerelease_models && verify_calls==1 && !com_modified);
    searchpath_t *search=com_searchpaths; retire_pack(search->pack); free(search); com_searchpaths=NULL;
    verified_result=false; int before=closed_handles;
    COM_AddRereleaseModelPack("synthetic-root");
    assert(!com_searchpaths && closed_handles==before+1 && verify_calls==2);
}
static void lookup(void) {
    qboolean source=false; unsigned int path=0;
    com_searchpaths=&base;
    assert(COM_FileExistsEx("progs/v_shot.md5mesh",&path,&source) && source && path==1);
    com_searchpaths=&higher;
    assert(COM_FileExistsEx("progs/v_shot.md5mesh",&path,&source) && !source);
    com_searchpaths=&loose; loose_md5=true;
    assert(COM_FileExistsEx("progs/v_shot.md5mesh",&path,&source) && !source);
    assert(COM_FileExists("progs/v_shot.md5mesh",NULL));
    loose_md5=false;
    assert(COM_FileExistsEx("progs/v_shot.md5mesh",NULL,&source) && source);
    assert(!COM_FileExistsEx("missing",NULL,&source) && !source);
    base.rerelease_models=true;
    assert(!COM_FileExistsEx("maps/start.bsp",NULL,&source) && !source);
    assert(COM_FileExistsEx("progs/v_shot.md5mesh",NULL,&source) && source);
    base.rerelease_models=false;
}
static void pure_geometry_and_factor(qmodel_t *m) {
    aliashdr_t *md5=(aliashdr_t *)m->extradata[PV_MD5];
    assert(md5);
    int io_before=io_calls,decode_before=decoder_calls,verify_before=verify_calls;
    assert(Mod_IsRereleaseReplacementGeometry(m,md5));
    m->rerelease_md5_weapon_payload=false;
    assert(!Mod_IsRereleaseReplacementGeometry(m,md5));
    assert(VR_WeaponCalibrationModelOffsetScale(m,md5)==1.0f);
    m->rerelease_md5_weapon_payload=true;
    assert(!Mod_IsRereleaseReplacementGeometry(m,(aliashdr_t *)m->extradata[PV_QUAKE1]));
    aliashdr_t foreign=*md5; assert(!Mod_IsRereleaseReplacementGeometry(m,&foreign));
    assert(!Mod_IsRereleaseReplacementGeometry(NULL,md5));
    assert(!Mod_IsRereleaseReplacementGeometry(m,NULL));
    assert(VR_WeaponCalibrationModelOffsetScale(m,md5)==0.5f);
    q_strlcpy(m->name,"progs/v_axe.mdl",sizeof(m->name));
    assert(fabsf(VR_WeaponCalibrationModelOffsetScale(m,md5)-1.0f/3.0f)<1e-7f);
    q_strlcpy(m->name,"PROGS/V_SHOT2.MDL",sizeof(m->name));
    assert(VR_WeaponCalibrationModelOffsetScale(m,md5)==1.0f);
    assert(VR_WeaponCalibrationModelOffsetScale(m,(aliashdr_t *)m->extradata[PV_QUAKE1])==1.0f);
    m->needload=true; assert(!Mod_IsRereleaseReplacementGeometry(m,md5)); m->needload=false;
    m->type=mod_brush; assert(!Mod_IsRereleaseReplacementGeometry(m,md5)); m->type=mod_alias;
    byte *classic=m->extradata[PV_QUAKE1]; m->extradata[PV_QUAKE1]=NULL;
    assert(!Mod_IsRereleaseReplacementGeometry(m,md5)); m->extradata[PV_QUAKE1]=classic;
    md5->poseverttype=PV_MD5_8; assert(Mod_IsRereleaseReplacementGeometry(m,md5));
    md5->poseverttype=PV_QUAKE3; assert(!Mod_IsRereleaseReplacementGeometry(m,md5)); md5->poseverttype=PV_MD5;
    assert(io_calls==io_before && decoder_calls==decode_before && verify_calls==verify_before);
}
static void selection_and_reset(void) {
    com_searchpaths=&base; qmodel_t m=weapon(); assert(Mod_LoadModel(&m,true)==&m);
    assert(m.rerelease_md5_companion && m.path_id==1); pure_geometry_and_factor(&m);
    r_enhancedmodels.value=0;
    assert(!Mod_IsRereleaseReplacementGeometry(&m,Mod_Extradata_CheckSkin(&m,0)));
    r_enhancedmodels.value=1;
    assert(Mod_IsRereleaseReplacementGeometry(&m,Mod_Extradata_CheckSkin(&m,0)));
    assert(!Mod_IsRereleaseReplacementGeometry(&m,Mod_Extradata_CheckSkin(&m,1))); /* skin fallback */
    m.md5_stockaxe_edge=123;
    Mod_FreeModelMemory(&m);
    assert(!m.rerelease_md5_companion && !m.extradata[PV_MD5] &&
        !m.rerelease_md5_weapon_payload && !m.md5_stockaxe_edge);
    com_searchpaths=&base; m=weapon(); payload_proof=false;
    assert(Mod_LoadModel(&m,true) && m.rerelease_md5_companion && !m.rerelease_md5_weapon_payload);
    assert(VR_WeaponCalibrationModelOffsetScale(&m,Mod_Extradata_CheckSkin(&m,0))==1.0f);
    Mod_FreeModelMemory(&m); payload_proof=true;
    com_searchpaths=&higher; m=weapon(); assert(Mod_LoadModel(&m,true));
    assert(!m.rerelease_md5_companion && VR_WeaponCalibrationModelOffsetScale(&m,Mod_Extradata_CheckSkin(&m,0))==1);
    Mod_FreeModelMemory(&m);
    com_searchpaths=&loose; loose_md5=true; m=weapon(); assert(Mod_LoadModel(&m,true));
    assert(!m.rerelease_md5_companion); Mod_FreeModelMemory(&m); loose_md5=false;
    com_searchpaths=&base; m=weapon(); r_enhancedmodels.value=0; m.rerelease_md5_companion=true;
    assert(Mod_LoadModel(&m,true) && !m.rerelease_md5_companion && !m.extradata[PV_MD5]);
    Mod_FreeModelMemory(&m); r_enhancedmodels.value=1;
    m=weapon(); q_strlcpy(m.name,"progs/v_shot.md5mesh",sizeof(m.name));
    assert(Mod_LoadModel(&m,true) && !m.rerelease_md5_companion && !m.extradata[PV_QUAKE1]);
    assert(VR_WeaponCalibrationModelOffsetScale(&m,Mod_Extradata_CheckSkin(&m,0))==1);
    Mod_FreeModelMemory(&m);
    custom_files[0]=(packfile_t){"progs/v_shot.md3",100,4};
    com_searchpaths=&higher; m=weapon(); assert(Mod_LoadModel(&m,true));
    assert(!m.rerelease_md5_companion && m.extradata[PV_QUAKE3] && !m.extradata[PV_MD5]);
    Mod_FreeModelMemory(&m); custom_files[0]=(packfile_t){"progs/v_shot.md5mesh",100,4};
    higher.path_id=1; base.path_id=2; com_searchpaths=&higher; m=weapon(); assert(Mod_LoadModel(&m,true));
    assert(!m.rerelease_md5_companion && !m.extradata[PV_MD5]); Mod_FreeModelMemory(&m); base.path_id=1;
    com_searchpaths=&base; m=weapon(); fail_md5=true; assert(Mod_LoadModel(&m,true));
    assert(!m.rerelease_md5_companion && !m.extradata[PV_MD5] && !m.rerelease_md5_weapon_payload);
    Mod_FreeModelMemory(&m); fail_md5=false;
    com_searchpaths=NULL; m=weapon(); m.rerelease_md5_companion=true;
    m.rerelease_md5_weapon_payload=true; m.md5_stockaxe_edge=123;
    assert(!Mod_LoadModel(&m,false) && !m.rerelease_md5_companion && m.needload &&
        !m.rerelease_md5_weapon_payload && !m.md5_stockaxe_edge);
}
int main(void) {
    mounts(); lookup(); selection_and_reset();
    puts("ENHANCED_PROVENANCE_PASS winning source, once-per-mount proof, companion selection, resets, pure factors");
    return 0;
}
'''


def main():
    cc = shlex.split(os.environ.get("CC", "cc"))
    with tempfile.TemporaryDirectory(prefix="enhanced-provenance-") as directory:
        path = Path(directory)
        source, binary = path / "fixture.c", path / "fixture"
        source.write_text(harness())
        subprocess.run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                             "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             str(source), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
