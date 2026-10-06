#!/usr/bin/env python3
"""Focused MD5 weapon payload/cache end check; nothing runs on import.

Run explicitly; add --assets PATH to the official rerelease pak0.pak for
positive payload proof and equal-length mesh/animation override cases. Extracts
production qualifiers, load-envelope calls, skinning/cache math, reset and pure
getters. Mesh parsing/GPU preparation are stubs: this checks actual loaded
payload bytes and publication boundaries, not complete decoding or rendering.
The asset pack stays external; no game payload is committed.
"""
import argparse
import binascii
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
STEMS = ("axe", "light", "nail", "nail2", "rock", "rock2", "shot", "shot2")


def definition(source, signature):
    start = -1
    while True:
        start = source.index(signature, start + 1)
        brace = source.index("{", start)
        if ";" not in source[start:brace]:
            return source[start:source.index("\n}", brace) + 2]


def harness():
    source = (ROOT / "Quake/gl_model.c").read_text()
    header = (ROOT / "Quake/gl_model.h").read_text()
    avatar = (ROOT / "Quake/r_avatar.h").read_text()
    joint_limit = next(line for line in avatar.splitlines()
                       if line.startswith("#define R_AVATAR_MAX_JOINTS "))
    enums = header[header.index("typedef enum\n{\n\tPV_QUAKE1"):header.index("#define MAX_FRAMEGROUPS")]
    vertices = header[header.index("#define NUM_JOINT_INFLUENCES_4_WEIGHT"):header.index("/* Model-owned CPU skeleton")]
    pins = source[source.index("typedef struct md5_weapon_payload_pin_s"):source.index("static const md5_weapon_payload_pin_t *Mod_VerifiedMD5WeaponMesh")]
    functions = "\n\n".join(definition(source, signature) for signature in (
        "static qboolean Mod_IsVerifiedRereleaseRangerAsset (",
        "static const md5_weapon_payload_pin_t *Mod_VerifiedMD5WeaponMesh (",
        "static qboolean Mod_IsVerifiedMD5WeaponAnimation (",
        "static void Mod_ResetMD5WeaponGeometry (",
        "qboolean Mod_IsRereleaseReplacementGeometry (",
        "qboolean Mod_GetMD5StockAxeEdge (",
        "static qboolean MD5_ReadyPoint (",
        "static void MD5_CacheStockAxeEdge ("))
    loader = definition(source, "static qboolean Mod_LoadMD5MeshModel (")
    data = definition(source, "static qboolean Mod_LoadMD5MeshModelData (")
    # Use the actual load-time selected animation call and actual publication.
    anim_start = data.index("\tverified_weapon_payload = Mod_IsVerifiedMD5WeaponAnimation")
    anim_call = data[anim_start:data.index(";", anim_start) + 1]
    publish_start = data.index("\tmod->rerelease_md5_weapon_payload =")
    publish = data[publish_start:data.index(";", data.index("\tmod->md5_stockaxe_edge =", publish_start)) + 1]
    cache_start = data.index("\t\tif (verified_weapon_payload && nummeshes == 1")
    cache_call = data[cache_start:data.index(";", cache_start) + 1]
    # Wiring checks complement the executable functions; no mirrored VFS policy.
    assert loader.index("Mod_VerifiedMD5WeaponMesh") < loader.index("buffer = COM_Parse")
    assert anim_start < data.index("if (!MD5Anim_Load")
    assert cache_start < data.index("TEMP_FREE (poutvertexes)")
    assert publish_start > data.index("couldn't allocate retained MD5 skeleton")
    for signature in ("static void Mod_FreeModelMemory (", "static qmodel_t *Mod_LoadModel ("):
        assert "Mod_ResetMD5WeaponGeometry (mod);" in definition(source, signature)
    stub = LOAD_STUB.replace("@ANIM@", anim_call).replace("@CACHE@", cache_call).replace("@PUBLISH@", publish)
    return PRELUDE + joint_limit + "\n" + enums + vertices + TYPES + pins + functions + stub + loader + CHECKS


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
typedef unsigned char byte;
typedef int qboolean;
typedef long long qfilesize_t;
typedef float vec3_t[3];
#define countof(x) (sizeof(x)/sizeof((x)[0]))
#define q_strcasecmp strcasecmp
#define COMPILE_TIME_ASSERT(name,cond) _Static_assert(cond,#name)
#define VectorCopy(a,b) memcpy((b),(a),sizeof(vec3_t))
static const vec3_t vec3_origin={0,0,0};
#define MZ_CRC32_INIT 0
static int crc_calls;
static unsigned long mz_crc32(unsigned long initial,const byte *bytes,size_t n) {
    uint32_t crc=(uint32_t)initial ^ 0xffffffffu; ++crc_calls;
    for(size_t i=0;i<n;++i) {
        crc^=bytes[i];
        for(int bit=0;bit<8;++bit) crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);
    }
    return crc^0xffffffffu;
}
'''

TYPES = r'''
enum { mod_alias, mod_brush };
typedef struct aliashdr_s {
    poseverttype_t poseverttype;
    struct aliashdr_s *nextsurface;
    int numverts,numverts_vbo,numtris,numframes,numposes,numjoints;
    struct { int firstpose,numposes; } frames[9];
    vec3_t scale,scale_origin;
} aliashdr_t;
typedef struct {
    char name[64]; int type; qboolean needload,rerelease_md5_companion;
    qboolean rerelease_md5_weapon_payload;
    byte *extradata[PV_SIZE]; stockaxe_edge_t stockaxe_edge;
    md5stockaxe_edge_t md5_stockaxe_edge;
} qmodel_t;
'''

LOAD_STUB = r'''
static const char *anim_name;
static const byte *selected_animation;
static qfilesize_t selected_animation_size;
static qboolean fail_header,fail_data;
static const byte *original_mesh;
static aliashdr_t geometry,classic;
_Alignas(md5vert8_t) static byte points[540*sizeof(md5vert8_t)];
static jointpose_t joints[2];
static int com_token_dummy;
static const char *com_token="joints";
static void *mod_custom_avatar;
#define MD5_VERSION "10"
#define MD5EXPECT(x) ((void)(x))
#define MD5CHECK(x) ((void)(x),false)
#define MD5UINT() (fail_header ? 0u : 1u)
static void fixture_error(const char *format,...) { (void)format; }
#define MD5ERROR(...) do { fixture_error(__VA_ARGS__); goto error; } while(0)
static const void *COM_Parse(const void *p) { ++com_token_dummy; return (const byte *)p+1; }
static qboolean Mod_LoadMD5MeshModelData(qmodel_t *mod,const void *buffer,
    size_t numjoints,size_t nummeshes,qboolean verified_rerelease_mesh,
    const md5_weapon_payload_pin_t *weapon_pin,const void *anim_override,
    qfilesize_t anim_override_size,qfilesize_t mesh_size) {
    (void)numjoints; (void)nummeshes; (void)verified_rerelease_mesh; (void)mesh_size;
    assert(buffer==original_mesh+1); /* wrapper qualified before parsing */
    assert(!mod->rerelease_md5_weapon_payload && !mod->md5_stockaxe_edge.edge.valid);
    struct { const char *fname; const void *animfile; qfilesize_t filesize; size_t numposes; } anim;
    anim.fname=anim_name;
    anim.animfile=anim_override ? anim_override : selected_animation;
    anim.filesize=anim_override ? anim_override_size : selected_animation_size;
    anim.numposes=9;
    qboolean verified_weapon_payload;
    md5stockaxe_edge_t weapon_edge={0};
    aliashdr_t *surf=&geometry;
    const byte *poutvertexes=points;
    const jointpose_t *skinning_joints=joints;
    @ANIM@
    @CACHE@
    if(fail_data) return false;
    @PUBLISH@
    mod->extradata[PV_MD5]=(byte *)&geometry;
    return true;
}
'''

CHECKS = r'''
static byte *read_bytes(const char *directory,int index,const char *ext,size_t expected) {
    char path[4096]; snprintf(path,sizeof(path),"%s/%d.%s",directory,index,ext);
    FILE *f=fopen(path,"rb"); assert(f);
    byte *p=malloc(expected); assert(p && fread(p,1,expected,f)==expected);
    assert(fgetc(f)==EOF); fclose(f); return p;
}
static void near(float actual,float expected) { assert(fabsf(actual-expected)<0.0001f); }
static void setup_points(poseverttype_t format) {
    memset(points,0,sizeof(points)); memset(&geometry,0,sizeof(geometry));
    memset(joints,0,sizeof(joints));
    geometry=(aliashdr_t){.poseverttype=format,.numverts=540,.numverts_vbo=540,
        .numtris=756,.numframes=9,.numposes=1,.numjoints=2,
        .scale={2,3,4},.scale_origin={7,8,9}};
    geometry.frames[0].numposes=1;
    joints[0]=(jointpose_t){{1,0,0,10, 0,1,0,20, 0,0,1,30}};
    joints[1]=(jointpose_t){{0,-1,0,-10, 1,0,0,5, 0,0,1,2}};
    for(int p=0;p<2;++p) {
        int v=p?54:55; float mul=p?2:1;
        if(format==PV_MD5) {
            md5vert_t *a=(md5vert_t *)(points+v*sizeof(md5vert_t));
            a->joint_weights[0]=100; a->joint_weights[1]=50; a->joint_indices[1]=1;
            a->joint_position_x[0]=mul; a->joint_position_y[0]=2*mul; a->joint_position_z[0]=3*mul;
            a->joint_position_x[1]=4*mul; a->joint_position_y[1]=5*mul; a->joint_position_z[1]=6*mul;
        } else {
            md5vert8_t *a=(md5vert8_t *)(points+v*sizeof(md5vert8_t));
            a->joint_weights[0]=100; a->joint_weights[7]=50; a->joint_indices[7]=1;
            a->joint_position_x[0]=mul; a->joint_position_y[0]=2*mul; a->joint_position_z[0]=3*mul;
            a->joint_position_x[7]=4*mul; a->joint_position_y[7]=5*mul; a->joint_position_z[7]=6*mul;
        }
    }
}
static void math_and_cache(void) {
    for(int f=PV_MD5;f<=PV_MD5_8;++f) {
        setup_points(f); md5stockaxe_edge_t c={0};
        MD5_CacheStockAxeEdge(&geometry,points,joints,9,&c); assert(c.edge.valid);
        /* Independent hand calculation: rotation acts on weighted XYZ,
         * translation on 100/150 and 50/150, then scale and origin. */
        near(c.edge.base[0],17.0f/3); near(c.edge.base[1],71); near(c.edge.base[2],383.0f/3);
        near(c.edge.tip[0],-7.0f/3); near(c.edge.tip[1],89); near(c.edge.tip[2],491.0f/3);
        geometry.nextsurface=&classic; MD5_CacheStockAxeEdge(&geometry,points,joints,9,&c); assert(!c.edge.valid);
        geometry.nextsurface=NULL; geometry.frames[0].numposes=2;
        MD5_CacheStockAxeEdge(&geometry,points,joints,9,&c); assert(!c.edge.valid);
        geometry.frames[0].numposes=1; geometry.numverts=539;
        MD5_CacheStockAxeEdge(&geometry,points,joints,9,&c); assert(!c.edge.valid);
        geometry.numverts=540; MD5_CacheStockAxeEdge(&geometry,points,joints,0,&c); assert(!c.edge.valid);
    }
    setup_points(PV_MD5); vec3_t out;
    md5vert_t *v=(md5vert_t *)(points+55*sizeof(md5vert_t));
    v->joint_indices[1]=2; assert(!MD5_ReadyPoint((byte *)v,PV_MD5,joints,2,out));
    v->joint_indices[1]=1; v->joint_position_x[0]=NAN;
    assert(!MD5_ReadyPoint((byte *)v,PV_MD5,joints,2,out));
    v->joint_position_x[0]=1; memset(v->joint_weights,0,sizeof(v->joint_weights));
    assert(!MD5_ReadyPoint((byte *)v,PV_MD5,joints,2,out));
    assert(!MD5_ReadyPoint((byte *)v,PV_QUAKE3,joints,2,out));
}
static void pure_getter(qmodel_t *m) {
    stockaxe_edge_t out; int before=crc_calls;
    assert(Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out));
    near(out.base[0],17.0f/3); assert(out.source_crc32==0x82833cbfu);
    assert(!Mod_GetMD5StockAxeEdge(m,&geometry,1,0,&out) && !out.valid);
    assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,1,&out));
    assert(!Mod_GetMD5StockAxeEdge(m,&geometry,-1,0,&out));
    assert(!Mod_GetMD5StockAxeEdge(m,&classic,0,0,&out));
    aliashdr_t foreign=geometry; assert(!Mod_GetMD5StockAxeEdge(m,&foreign,0,0,&out));
    geometry.poseverttype=PV_MD5_8; assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out));
    geometry.poseverttype=PV_QUAKE3; assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out));
    geometry.poseverttype=PV_MD5;
    m->needload=true; assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out)); m->needload=false;
    m->rerelease_md5_companion=false; assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out)); m->rerelease_md5_companion=true;
    m->extradata[PV_QUAKE1]=NULL; assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out)); m->extradata[PV_QUAKE1]=(byte *)&classic;
    assert(!Mod_GetMD5StockAxeEdge(NULL,&geometry,0,0,&out));
    assert(!Mod_GetMD5StockAxeEdge(m,NULL,0,0,&out));
    assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,NULL));
    assert(crc_calls==before); /* query cannot qualify or re-skin anything */
    m->stockaxe_edge=(stockaxe_edge_t){.valid=true,.source_crc32=0x2aa03605u,.base={999,999,999}};
    assert(Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out)); near(out.base[0],17.0f/3);
    Mod_ResetMD5WeaponGeometry(m);
    assert(!Mod_GetMD5StockAxeEdge(m,&geometry,0,0,&out) && !out.valid);
    assert(m->stockaxe_edge.valid); /* independent classic record */
}
int main(int argc,char **argv) {
    assert(argc==1 || argc==2); math_and_cache();
    setup_points(PV_MD5);
    qmodel_t ready={.type=mod_alias,.rerelease_md5_companion=true,
        .rerelease_md5_weapon_payload=true}; /* pure getter metadata seam */
    snprintf(ready.name,sizeof(ready.name),"progs/v_axe.mdl");
    ready.extradata[PV_QUAKE1]=(byte *)&classic; ready.extradata[PV_MD5]=(byte *)&geometry;
    MD5_CacheStockAxeEdge(&geometry,points,joints,9,&ready.md5_stockaxe_edge);
    pure_getter(&ready);
    setup_points(PV_MD5_8); ready.rerelease_md5_weapon_payload=true;
    MD5_CacheStockAxeEdge(&geometry,points,joints,9,&ready.md5_stockaxe_edge);
    stockaxe_edge_t eight_edge;
    assert(Mod_GetMD5StockAxeEdge(&ready,&geometry,0,0,&eight_edge)); near(eight_edge.tip[2],491.0f/3);
    geometry.poseverttype=PV_MD5;
    assert(!Mod_GetMD5StockAxeEdge(&ready,&geometry,0,0,&eight_edge) && !eight_edge.valid);
    Mod_ResetMD5WeaponGeometry(&ready);
    if(argc==1) {
        for(size_t i=0;i<countof(md5_weapon_payload_pins);++i) {
            const md5_weapon_payload_pin_t *p=&md5_weapon_payload_pins[i];
            qmodel_t m={0}; snprintf(m.name,sizeof(m.name),"%s",p->model_name);
            byte *mesh=calloc(1,p->mesh_size),*animation=calloc(1,p->anim_size); assert(mesh && animation);
            assert(!Mod_VerifiedMD5WeaponMesh(&m,p->mesh_name,mesh,p->mesh_size));
            assert(!Mod_IsVerifiedMD5WeaponAnimation(p,p->anim_name,animation,p->anim_size));
            assert(!Mod_IsVerifiedMD5WeaponAnimation(NULL,p->anim_name,animation,p->anim_size));
            free(mesh); free(animation);
        }
        puts("MD5_WEAPON_GEOMETRY_PASS packed ready math, pure format/pose getter, reset, wrong-payload rejection; real payload cases require --assets");
        return 0;
    }
    for(size_t i=0;i<countof(md5_weapon_payload_pins);++i) {
        const md5_weapon_payload_pin_t *p=&md5_weapon_payload_pins[i];
        byte *mesh=read_bytes(argv[1],(int)i,"mesh",p->mesh_size);
        byte *animation=read_bytes(argv[1],(int)i,"anim",p->anim_size);
        byte *override=malloc(p->anim_size); assert(override); memcpy(override,animation,p->anim_size);
        qmodel_t m={.type=mod_alias,.rerelease_md5_companion=true};
        snprintf(m.name,sizeof(m.name),"%s",p->model_name);
        m.extradata[PV_QUAKE1]=(byte *)&classic;
        original_mesh=mesh; selected_animation=animation; selected_animation_size=p->anim_size; anim_name=p->anim_name;
        setup_points(PV_MD5);
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1));
        assert(Mod_IsRereleaseReplacementGeometry(&m,&geometry));
        if(i==0) pure_getter(&m); else assert(!m.md5_stockaxe_edge.edge.valid);
        /* Same name and length are insufficient, independently for both bytes. */
        mesh[p->mesh_size/2]^=1;
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1));
        assert(!Mod_IsRereleaseReplacementGeometry(&m,&geometry)); mesh[p->mesh_size/2]^=1;
        override[p->anim_size/2]^=1;
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,override,p->anim_size));
        assert(!m.rerelease_md5_weapon_payload && !m.md5_stockaxe_edge.edge.valid);
        override[p->anim_size/2]^=1;
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,override,p->anim_size));
        assert(m.rerelease_md5_weapon_payload); /* exact selected override is fine */
        selected_animation=NULL; selected_animation_size=-1;
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1));
        assert(!m.rerelease_md5_weapon_payload); selected_animation=animation; selected_animation_size=p->anim_size;
        assert(!Mod_VerifiedMD5WeaponMesh(&m,p->mesh_name,mesh,p->mesh_size-1));
        assert(!Mod_VerifiedMD5WeaponMesh(&m,p->mesh_name,NULL,p->mesh_size));
        assert(!Mod_VerifiedMD5WeaponMesh(&m,"progs/custom.md5mesh",mesh,p->mesh_size));
        assert(!Mod_IsVerifiedMD5WeaponAnimation(p,"progs/custom.md5anim",animation,p->anim_size));
        const md5_weapon_payload_pin_t *other=&md5_weapon_payload_pins[(i+1)%countof(md5_weapon_payload_pins)];
        assert(!Mod_IsVerifiedMD5WeaponAnimation(other,p->anim_name,animation,p->anim_size));
        snprintf(m.name,sizeof(m.name),"progs/custom.mdl");
        assert(!Mod_VerifiedMD5WeaponMesh(&m,p->mesh_name,mesh,p->mesh_size));
        snprintf(m.name,sizeof(m.name),"%s",p->model_name);
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1));
        fail_data=true; assert(!Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1)); fail_data=false;
        assert(!m.rerelease_md5_weapon_payload && !m.md5_stockaxe_edge.edge.valid);
        assert(Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1));
        fail_header=true; assert(!Mod_LoadMD5MeshModel(&m,mesh,p->mesh_name,p->mesh_size,NULL,-1)); fail_header=false;
        assert(!m.rerelease_md5_weapon_payload && !m.md5_stockaxe_edge.edge.valid);
        free(mesh); free(animation); free(override);
    }
    puts("MD5_WEAPON_GEOMETRY_PASS selected payloads, overrides, packed ready math, pure format/pose getter, reset");
    return 0;
}
'''


def extract_payloads(pack, destination):
    with pack.open("rb") as stream:
        magic, offset, length = struct.unpack("<4sii", stream.read(12))
        assert magic == b"PACK" and pack.stat().st_size == 180815252
        assert length == 1121 * 64 and 0 <= offset <= 180815252 - length
        stream.seek(offset)
        directory = stream.read(length)
        assert binascii.crc_hqx(directory, 0xffff) == 20578
        entries = {}
        for index in range(0, length, 64):
            name, position, size = struct.unpack("<56sii", directory[index:index + 64])
            name = name.split(b"\0", 1)[0].decode("ascii")
            assert name not in entries and 0 <= position <= 180815252 - size and size >= 0
            entries[name] = (position, size)

        def read(name):
            position, size = entries[name]
            stream.seek(position)
            data = stream.read(size)
            assert len(data) == size
            return data

        for name, size, crc in (("progs/player.md5mesh", 178658, 0x7911B9B0),
                                ("progs/player.md5anim", 331510, 0x0561E50A)):
            data = read(name)
            assert len(data) == size and zlib.crc32(data) == crc
        for index, stem in enumerate(STEMS):
            for kind, extension in (("mesh", "md5mesh"), ("anim", "md5anim")):
                (destination / f"{index}.{kind}").write_bytes(read(f"progs/v_{stem}.{extension}"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pak", "--assets", dest="pak", type=Path,
                        help="external official rerelease pak0.pak")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="md5-weapon-geometry-") as directory:
        path = Path(directory)
        if args.pak:
            extract_payloads(args.pak, path)
        source, binary = path / "fixture.c", path / "fixture"
        source.write_text(harness())
        subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(source), "-lm", "-o", str(binary)], check=True)
        subprocess.run([str(binary)] + ([str(path)] if args.pak else []), check=True)


if __name__ == "__main__":
    main()
