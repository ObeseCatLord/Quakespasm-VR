/* Private native SDK boundary probe; no proprietary assets. */
#include <phonon.h>
#include <SDL3/SDL.h>
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../Quake/snd_room.c"
static int fault, injected, reset_count;
static float bad;
IPLAudioEffectState __real_iplReflectionEffectApply(IPLReflectionEffect,IPLReflectionEffectParams*,IPLAudioBuffer*,IPLAudioBuffer*,IPLReflectionMixer);
IPLAudioEffectState __real_iplReflectionEffectGetTail(IPLReflectionEffect,IPLAudioBuffer*,IPLReflectionMixer);
IPLAudioEffectState __real_iplAmbisonicsDecodeEffectApply(IPLAmbisonicsDecodeEffect,IPLAmbisonicsDecodeEffectParams*,IPLAudioBuffer*,IPLAudioBuffer*);
void __real_iplReflectionEffectReset(IPLReflectionEffect);
void __wrap_iplReflectionEffectReset(IPLReflectionEffect e) { ++reset_count; __real_iplReflectionEffectReset(e); }
void __real_iplAmbisonicsDecodeEffectReset(IPLAmbisonicsDecodeEffect);
void __wrap_iplAmbisonicsDecodeEffectReset(IPLAmbisonicsDecodeEffect e) { ++reset_count; __real_iplAmbisonicsDecodeEffectReset(e); }
static void invalidate(IPLAudioBuffer *out) { out->data[out->numChannels-1][out->numSamples-1]=bad; ++injected; }
IPLAudioEffectState __wrap_iplReflectionEffectApply(IPLReflectionEffect e,IPLReflectionEffectParams*p,IPLAudioBuffer*in,IPLAudioBuffer*out,IPLReflectionMixer m) {
 IPLAudioEffectState s=__real_iplReflectionEffectApply(e,p,in,out,m);
 if ((fault==1 && out->numChannels==4) || (fault==3 && out->numChannels==1) || fault==6) invalidate(out);
 return s;
}
IPLAudioEffectState __wrap_iplReflectionEffectGetTail(IPLReflectionEffect e,IPLAudioBuffer*out,IPLReflectionMixer m) {
 IPLAudioEffectState s=__real_iplReflectionEffectGetTail(e,out,m);
 if (fault==4) invalidate(out);
 return s;
}
IPLAudioEffectState __wrap_iplAmbisonicsDecodeEffectApply(IPLAmbisonicsDecodeEffect e,IPLAmbisonicsDecodeEffectParams*p,IPLAudioBuffer*in,IPLAudioBuffer*out) {
 IPLAudioEffectState s=__real_iplAmbisonicsDecodeEffectApply(e,p,in,out);
 if (fault==2 || fault==5) {
  if(fault==5) { ++injected; for(int c=0;c<2;c++)for(int i=0;i<SA_BLOCK;i++)out->data[c][i]=FLT_MAX; }
  else invalidate(out);
 }
 return s;
}
int main(void) {
 IPLContext context=NULL; IPLHRTF hrtf=NULL;
 IPLContextSettings cs={0}; cs.version=STEAMAUDIO_VERSION; assert(iplContextCreate(&cs,&context)==IPL_STATUS_SUCCESS);
 IPLAudioSettings audio={SA_RATE,SA_BLOCK}; IPLHRTFSettings hs={0}; hs.type=IPL_HRTFTYPE_DEFAULT; hs.volume=1;
 assert(iplHRTFCreate(context,&audio,&hs,&hrtf)==IPL_STATUS_SUCCESS);
 sa_geometry_t g={0};
 float vertices[]={-4,-1,-4,4,-1,-4,4,-1,4,-4,-1,4,-4,3,-4,4,3,-4,4,3,4,-4,3,4};
 int triangles[]={0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,1,5,6,1,6,2,2,6,7,2,7,3,3,7,4,3,4,0};
 g.num_vertices=8;g.num_triangles=12;g.vertices=malloc(sizeof vertices);g.triangles=malloc(sizeof triangles);g.materials=calloc(12,sizeof(int));assert(g.vertices&&g.triangles&&g.materials);memcpy(g.vertices,vertices,sizeof vertices);memcpy(g.triangles,triangles,sizeof triangles);
 sa_room_t*r=SAR_Create(context,hrtf,&g);assert(r);
 sa_listener_t listener={.forward={1,0,0},.right={0,-1,0},.up={0,0,1},.timestamp=1};
 sa_settings_t settings={.hrtf=1,.reverb=.25f,.voice_reverb=.12f,.room_mode=2,.room_rays=256,.room_bounces=2};
 SAR_Update(r,&listener,&settings);
 for(int i=0;i<200;i++){sa_room_stats_t s={0};SAR_Stats(r,&s);if(s.ready)break;SDL_Delay(10);}
 assert(r->stats.ready);
 /* Freeze only this fixture's worker after the real SDK has published an IR. */
 sa_atomic_set(&r->quit,1);SDL_WaitThread(r->thread,NULL);r->thread=NULL;
 float sfx[SA_BLOCK],voice[SA_BLOCK],mix[SA_BLOCK*2];
 for(int i=0;i<SA_BLOCK;i++){sfx[i]=.1f*sinf(i*.1f);voice[i]=.07f*cosf(i*.12f);}
 int cases=0;double max_ms=0;
 for(int mode=1;mode<=2;mode++)for(int type=1;type<=6;type++)for(int v=0;v<3;v++){
  settings.room_mode=mode;fault=0;SAR_Reset(r,0);r->mode=mode;
  for(int i=0;i<SA_BLOCK*2;i++)mix[i]=.25f;
  SAR_Render(r,sfx,voice,mix,&listener,&settings);
  fault=type;bad=v==0?NAN:v==1?INFINITY:-INFINITY;injected=0;int before=reset_count;
  if(type==4){r->sfx_tail=1;r->voice_tail=1;memset(sfx,0,sizeof sfx);memset(voice,0,sizeof voice);}
  for(int repeat=0;repeat<8;repeat++){
   if(type==4){r->sfx_tail=1;r->voice_tail=1;}
   for(int i=0;i<SA_BLOCK*2;i++)mix[i]=type==5?FLT_MAX:.25f;
   uint64_t begin=SDL_GetPerformanceCounter();
   unsigned result=SAR_Render(r,sfx,voice,mix,&listener,&settings);
   double ms=(SDL_GetPerformanceCounter()-begin)*1000.0/SDL_GetPerformanceFrequency();if(ms>max_ms)max_ms=ms;
   assert(result!=0);for(int i=0;i<SA_BLOCK*2;i++)assert(isfinite(mix[i]));
   if(type==6)for(int i=0;i<SA_BLOCK*2;i++)assert(mix[i]==.25f);
   if(type==5)for(int i=0;i<SA_BLOCK*2;i++)assert(mix[i]==FLT_MAX);
  }
  assert(injected>0&&reset_count>before);++cases;
  fault=0;for(int i=0;i<SA_BLOCK;i++){sfx[i]=.1f*sinf(i*.1f);voice[i]=.07f*cosf(i*.12f);}
  for(int i=0;i<SA_BLOCK*2;i++)mix[i]=.25f;
  SAR_Render(r,sfx,voice,mix,&listener,&settings);for(int i=0;i<SA_BLOCK*2;i++)assert(isfinite(mix[i]));
 }
 printf("ROOM_CONTAINMENT_PASSED cases=%d fault_blocks=%d max_fault_ms=%.3f\n",cases,cases*8,max_ms);
 SAR_Destroy(r);iplHRTFRelease(&hrtf);iplContextRelease(&context);return 0;
}
