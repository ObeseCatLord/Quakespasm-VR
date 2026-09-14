/* Alicia-only proof: original GLB geometry/images, independent material draws,
 * and the existing avatar's solved palette. Assets are never redistributed.
 * Enabled only by USE_ALICIA_SPIKE=1 and -aliciavrm <inspected source file>.
 */
#include "quakedef.h"
#include "r_alicia_spike.h"
#include "custom_avatar.h"
#include <SDL.h>
#define CGLTF_IMPLEMENTATION
#include "../experiments/alicia/vendor/cgltf.h"

cvar_t r_alicia_direct = {"r_alicia_direct", "0", CVAR_NONE};
cvar_t r_alicia_preview = {"r_alicia_preview", "0", CVAR_NONE};
cvar_t r_alicia_pose = {"r_alicia_pose", "0", CVAR_NONE};
cvar_t r_alicia_props = {"r_alicia_props", "1", CVAR_NONE};
#define AS_VERTS 24000
#define AS_INDEXES 100000
#define AS_PARTS 20
#define AS_NODES 143

typedef struct {
 float local[4][3], normal[4][3], weights[4], uv[2];
 int joints[4];
} as_vertex_t;
typedef struct { float xyz[3], uv[2], color[4]; } as_drawvertex_t;
typedef struct {
 int first, count, material, image, queue;
 qboolean blend, double_sided;
 float factor[4];
} as_part_t;
typedef struct {
 as_drawvertex_t *vertices;
 int hostframe;
 unsigned int generation;
} as_cache_t;
static as_vertex_t *as_vertices;
static unsigned int *as_indexes;
static as_part_t as_parts[AS_PARTS];
static float as_bind[AS_NODES][12];
static gltexture_t *as_images[8];
static int as_numverts, as_numindexes, as_numparts, as_state;
static as_cache_t as_cache[MAX_SCOREBOARD+1];
static double as_skin_time, as_draw_time;
static unsigned int as_skins, as_draws;

static double AS_Time(void) { return (double)SDL_GetPerformanceCounter()/SDL_GetPerformanceFrequency(); }

static cgltf_accessor *AS_Attribute(cgltf_primitive *p, cgltf_attribute_type type)
{
 cgltf_size i;
 for(i=0;i<p->attributes_count;i++)
  if(p->attributes[i].type==type && p->attributes[i].index==0) return p->attributes[i].data;
 return NULL;
}
static void AS_Point(const float m[16], const float p[3], float out[3], float scale)
{
 int r;
 for(r=0;r<3;r++) out[r]=(m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r])*scale;
}
static qboolean AS_Load(void)
{
 cgltf_options options={0}; cgltf_data *data=NULL;
 FILE *f=NULL; byte *bytes=NULL; long length; int parm, n, k, m;
 cgltf_size ni, pi, ai; uint64_t fingerprint=UINT64_C(14695981039346656037);
 /* These queue/depth conventions are from the fingerprinted VRM0 MToon
  * materialProperties. All four BLEND materials author ZWrite=1. */
 static const int queues[12]={2000,2000,2000,2000,2000,2000,2501,2000,2550,2000,2501,2501};
 if(as_state) return as_state>0;
 as_state=-1;
 parm=COM_CheckParm("-aliciavrm");
 if(!parm || parm+1>=com_argc) return false;
 f=fopen(com_argv[parm+1],"rb");
 if(!f) goto fail;
 if(fseek(f,0,SEEK_END)!=0 || (length=ftell(f))!=7878712 || fseek(f,0,SEEK_SET)!=0) goto fail;
 bytes=(byte*)malloc((size_t)length);
 if(!bytes || fread(bytes,1,(size_t)length,f)!=(size_t)length) goto fail;
 fclose(f);f=NULL;
 /* A sample guard before parsing, not a network identity or general validator. */
 for(n=0;n<length;n++) fingerprint=(fingerprint^bytes[n])*UINT64_C(1099511628211);
 if(fingerprint!=UINT64_C(0x33732d416aedbed7)) goto fail;
 if(cgltf_parse(&options,bytes,(size_t)length,&data)!=cgltf_result_success ||
  data->nodes_count!=AS_NODES || data->buffers_count!=1 || data->buffers[0].uri ||
  data->images_count!=8 || data->materials_count!=12 || !data->bin ||
  data->buffers[0].size>data->bin_size) goto fail;
 data->buffers[0].data=(void*)data->bin;
 if(cgltf_validate(data)!=cgltf_result_success) goto fail;
 as_vertices=(as_vertex_t*)calloc(AS_VERTS,sizeof(*as_vertices));
 as_indexes=(unsigned int*)calloc(AS_INDEXES,sizeof(*as_indexes));
 if(!as_vertices || !as_indexes) goto fail;
 for(ni=0;ni<data->nodes_count;ni++) {
  float world[16];
  cgltf_node_transform_world(&data->nodes[ni],world);
  /* MD5 scaffold uses Q=(-z,-x,y), scaled translations, original local axes. */
  for(k=0;k<4;k++) {
   as_bind[ni][k]=-world[k*4+2];
   as_bind[ni][4+k]=-world[k*4];
   as_bind[ni][8+k]=world[k*4+1];
  }
  as_bind[ni][3]*=32;as_bind[ni][7]*=32;as_bind[ni][11]*=32;
 }
 for(ni=0;ni<data->nodes_count;ni++) {
  cgltf_node *node=&data->nodes[ni];
  if(!node->mesh) continue;
  if(!node->skin) goto fail;
  for(pi=0;pi<node->mesh->primitives_count;pi++) {
   cgltf_primitive *p=&node->mesh->primitives[pi];
   cgltf_accessor *pos=AS_Attribute(p,cgltf_attribute_type_position);
   cgltf_accessor *normal=AS_Attribute(p,cgltf_attribute_type_normal);
   cgltf_accessor *uv=AS_Attribute(p,cgltf_attribute_type_texcoord);
   cgltf_accessor *joints=AS_Attribute(p,cgltf_attribute_type_joints);
   cgltf_accessor *weights=AS_Attribute(p,cgltf_attribute_type_weights);
   cgltf_material *material=p->material; as_part_t *part; int *remap;
   if(as_numparts>=AS_PARTS || !pos || !normal || !uv || !joints || !weights ||
    !p->indices || p->type!=cgltf_primitive_type_triangles || !material ||
    pos->count>AS_VERTS || p->indices->count>AS_INDEXES-as_numindexes) goto fail;
   part=&as_parts[as_numparts++];part->first=as_numindexes;
   part->count=(int)p->indices->count;part->material=(int)(material-data->materials);
   part->queue=queues[part->material];part->blend=material->alpha_mode==cgltf_alpha_mode_blend;
   part->double_sided=material->double_sided;
   memcpy(part->factor,material->pbr_metallic_roughness.base_color_factor,sizeof(part->factor));
   part->image=(int)(material->pbr_metallic_roughness.base_color_texture.texture->image-data->images);
   remap=(int*)malloc(pos->count*sizeof(*remap));if(!remap) goto fail;
   for(ai=0;ai<pos->count;ai++) remap[ai]=-1;
   for(ai=0;ai<p->indices->count;ai++) {
    cgltf_size source=cgltf_accessor_read_index(p->indices,ai);
    if(source>=pos->count) {free(remap);goto fail;}
    if(remap[source]<0) {
     as_vertex_t *v;float xyz[3],nn[3],inverse[16];cgltf_uint ji[4];
     if(as_numverts>=AS_VERTS) {free(remap);goto fail;}
     remap[source]=as_numverts++;v=&as_vertices[remap[source]];
     if(!cgltf_accessor_read_float(pos,source,xyz,3) || !cgltf_accessor_read_float(normal,source,nn,3) ||
      !cgltf_accessor_read_float(uv,source,v->uv,2) || !cgltf_accessor_read_float(weights,source,v->weights,4) ||
      !cgltf_accessor_read_uint(joints,source,ji,4)) {free(remap);goto fail;}
     for(k=0;k<4;k++) {
      if(ji[k]>=node->skin->joints_count || !cgltf_accessor_read_float(node->skin->inverse_bind_matrices,ji[k],inverse,16)) {free(remap);goto fail;}
      v->joints[k]=(int)(node->skin->joints[ji[k]]-data->nodes);
      AS_Point(inverse,xyz,v->local[k],32);
      inverse[12]=inverse[13]=inverse[14]=0;AS_Point(inverse,nn,v->normal[k],1);
     }
    }
    as_indexes[as_numindexes++]=(unsigned int)remap[source];
   }
   free(remap);
   for(n=part->first;n<part->first+part->count;n+=3) {
    unsigned int tmp=as_indexes[n+1];as_indexes[n+1]=as_indexes[n+2];as_indexes[n+2]=tmp;
   }
  }
 }
 if(as_numverts!=21623 || as_numindexes!=95394 || as_numparts!=20) goto fail;
 for(n=0;n<as_numparts;n++) {
  int im=as_parts[n].image,width,height;byte *pixels;char name[64];cgltf_image *image;
  if(im<0 || im>=8) goto fail;
  if(as_images[im]) continue;
  image=&data->images[im];if(image->uri || !image->buffer_view) goto fail;
  pixels=Image_DecodeRGBA(cgltf_buffer_view_data(image->buffer_view),image->buffer_view->size,2048,&width,&height);
  if(!pixels) goto fail;
  q_snprintf(name,sizeof(name),"alicia-vrm-spike:image%d",im);
  as_images[im]=TexMgr_LoadOwnedRGBA(NULL,name,width,height,pixels,TEXPREF_MIPMAP|TEXPREF_ALPHA|TEXPREF_PERSIST|TEXPREF_NOPICMIP);
  if(!as_images[im]) goto fail;
 }
 cgltf_free(data);free(bytes);as_state=1;
 Con_Printf("Alicia direct VRM: %d vertices, %d triangles, %d materials ranges, six original PNGs (33 MiB RGBA).\n",as_numverts,as_numindexes/3,as_numparts);
 return true;
fail:
 if(f) fclose(f);
 if(data) cgltf_free(data);
 free(bytes);free(as_vertices);free(as_indexes);as_vertices=NULL;as_indexes=NULL;
 for(m=0;m<8;m++) if(as_images[m]) {TexMgr_FreeTexture(as_images[m]);as_images[m]=NULL;}
 Con_Printf("Alicia VRM spike rejected input; requires the exact inspected Alicia file. MD5 fallback retained.\n");
 return false;
}
static qboolean AS_Target(qmodel_t *model)
{
 md5liveinfo_t live;int i,j;
 if(!Mod_GetMD5LiveData(model,&live) || live.numbones!=AS_NODES) return false;
 for(i=0;i<AS_NODES;i++) {
  char name[32];q_snprintf(name,sizeof(name),"b%d",i);
  if(strcmp(live.joints[i].name,name)) return false;
  for(j=0;j<12;j++) if(fabsf(live.joints[i].bind[j]-as_bind[i][j])>0.001f) return false;
 }
 return true;
}
static void AS_Stats(void)
{
 Con_Printf("Alicia spike: loaded=%d verts=%d tris=%d draws=%u skins=%u\nCPU averages: skin+lighting %.3f ms, draw submission %.3f ms (not GPU timings).\n",
  as_state,as_numverts,as_numindexes/3,as_draws,as_skins,
  as_skins?1000*as_skin_time/as_skins:0,as_draws?1000*as_draw_time/as_draws:0);
}
void R_AliciaSpikeInit(void)
{
 Cvar_RegisterVariable(&r_alicia_props);Cvar_RegisterVariable(&r_alicia_pose);Cvar_RegisterVariable(&r_alicia_direct);Cvar_RegisterVariable(&r_alicia_preview);
 Cmd_AddCommand("alicia_spike_stats",AS_Stats);
}
qboolean R_AliciaSpikeDraw(qmodel_t *model,const r_vrik_skincache_t *pose,
 int slot,const float light[3],const float shade[3],float alpha)
{
 const custom_avatar_t *avatar=CustomAvatar_Get(pose->avatar_id);
 as_cache_t *cache;int i,j,k,r,order[AS_PARTS];float transforms[AS_NODES][12],depth[AS_PARTS],view[16];double start;
 /* Slot zero is shared by transient corpse substitutions. Their ordinary
  * MD5 cache is invalidated per body, but this sample's per-player cache has
  * no corpse identity. Keep corpses on MD5 rather than reuse another body's
  * pose/lighting within the same frame. */
 if(!r_alicia_direct.value || !avatar || strcmp(avatar->key,"alicia") || slot<=0 || slot>MAX_SCOREBOARD || !AS_Load() || !AS_Target(model)) return false;
 cache=&as_cache[slot];
 if(!cache->vertices) {cache->vertices=(as_drawvertex_t*)calloc(as_numverts,sizeof(*cache->vertices));cache->hostframe=-1;}
 if(!cache->vertices) return false;
 /* Geometry and lighting are cached across stereo eyes within one immutable
  * player pose generation and host frame. */
 start=AS_Time();
 if(cache->hostframe!=host_framecount || cache->generation!=pose->pose_generation) {
  for(i=0;i<AS_NODES;i++) R_ConcatTransforms((float(*)[4])pose->alicia_presentation,(float(*)[4])(pose->palette+i*12),(float(*)[4])transforms[i]);
  for(i=0;i<as_numverts;i++) {
   as_vertex_t *v=&as_vertices[i];as_drawvertex_t *out=&cache->vertices[i];float normal[3]={0,0,0};float dot;
   memset(out->xyz,0,sizeof(out->xyz));memcpy(out->uv,v->uv,sizeof(out->uv));
   for(k=0;k<4;k++) if(v->weights[k]>0) {
    const float *m=transforms[v->joints[k]];
    for(r=0;r<3;r++) {
     out->xyz[r]+=v->weights[k]*(m[r*4]*v->local[k][0]+m[r*4+1]*v->local[k][1]+m[r*4+2]*v->local[k][2]+m[r*4+3]);
     normal[r]+=v->weights[k]*(m[r*4]*v->normal[k][0]+m[r*4+1]*v->normal[k][1]+m[r*4+2]*v->normal[k][2]);
    }
   }
   VectorNormalize(normal);dot=DotProduct(normal,shade);dot=dot<0?1+dot*(13.0f/44.0f):1+dot;
   for(r=0;r<3;r++) out->color[r]=r_alicia_direct.value>=2?1:dot*light[r];
   out->color[3]=alpha;
  }
  cache->hostframe=host_framecount;cache->generation=pose->pose_generation;
  as_skin_time+=AS_Time()-start;as_skins++;
 }
 start=AS_Time();glGetFloatv(GL_MODELVIEW_MATRIX,view);
 for(i=0;i<as_numparts;i++) {
  float z=0;as_part_t *p=&as_parts[i];order[i]=i;
  for(j=p->first;j<p->first+p->count;j++) {float *v=cache->vertices[as_indexes[j]].xyz;z+=view[2]*v[0]+view[6]*v[1]+view[10]*v[2]+view[14];}
  depth[i]=z/p->count;
 }
 for(i=1;i<as_numparts;i++) {
  int entry=order[i];j=i;
  while(j>0 && (as_parts[order[j-1]].queue>as_parts[entry].queue ||
   (as_parts[order[j-1]].queue==as_parts[entry].queue && depth[order[j-1]]>depth[entry]))) {order[j]=order[j-1];j--;}
  order[j]=entry;
 }
 glPushAttrib(GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_POLYGON_BIT|GL_TEXTURE_BIT|GL_CURRENT_BIT);
 glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
 GL_BindBuffer(GL_ARRAY_BUFFER,0);GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
 glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
 glDisableClientState(GL_NORMAL_ARRAY);
 glVertexPointer(3,GL_FLOAT,sizeof(as_drawvertex_t),cache->vertices[0].xyz);
 glTexCoordPointer(2,GL_FLOAT,sizeof(as_drawvertex_t),cache->vertices[0].uv);
 glColorPointer(4,GL_FLOAT,sizeof(as_drawvertex_t),cache->vertices[0].color);
 glEnable(GL_TEXTURE_2D);glDisable(GL_ALPHA_TEST);glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);glFrontFace(GL_CW);
 for(i=0;i<as_numparts;i++) {
  as_part_t *p=&as_parts[order[i]];
  if(p->blend || alpha<1) {glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);} else glDisable(GL_BLEND);
  glDepthMask(alpha>=1?GL_TRUE:GL_FALSE); /* fingerprinted VRM0 authors ZWrite on all parts */
  if(p->double_sided) glDisable(GL_CULL_FACE);else glEnable(GL_CULL_FACE);
  GL_Bind(as_images[p->image]);
  /* The inspected file authors linear filtering without mipmap sampling. */
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glDrawElements(GL_TRIANGLES,p->count,GL_UNSIGNED_INT,as_indexes+p->first);
 }
 glPopClientAttrib();GL_ClearBufferBindings();glPopAttrib();
 /* GL_Bind tracks texture bindings outside glPushAttrib; synchronize by forcing
  * a known binding after the restore rather than leaving its cache stale. */
 GL_Bind(nulltexture);
 as_draw_time+=AS_Time()-start;as_draws++;
 return true;
}
