/* Native full spatial mixer probe using owned synthetic samples only. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../Quake/snd_steamaudio.c"
static IPLBinauralEffect bad_effect;
static int fault, injected, applies;
static float invalid;
IPLAudioEffectState __real_iplBinauralEffectApply(IPLBinauralEffect,IPLBinauralEffectParams*,IPLAudioBuffer*,IPLAudioBuffer*);
IPLAudioEffectState __real_iplBinauralEffectGetTail(IPLBinauralEffect,IPLAudioBuffer*);
IPLAudioEffectState __wrap_iplBinauralEffectApply(IPLBinauralEffect e,IPLBinauralEffectParams*p,IPLAudioBuffer*in,IPLAudioBuffer*out) {
 float n=p->direction.x*p->direction.x+p->direction.y*p->direction.y+p->direction.z*p->direction.z;
 assert(isfinite(n)&&fabsf(n-1)<.0001f); ++applies;
 IPLAudioEffectState s=__real_iplBinauralEffectApply(e,p,in,out);
 if(fault && e==bad_effect){out->data[1][SA_BLOCK-1]=invalid;++injected;}
 return s;
}
IPLAudioEffectState __wrap_iplBinauralEffectGetTail(IPLBinauralEffect e,IPLAudioBuffer*out) {
 IPLAudioEffectState s=__real_iplBinauralEffectGetTail(e,out);
 if(fault && e==bad_effect){out->data[1][SA_BLOCK-1]=invalid;++injected;}
 return s;
}
int main(void) {
 sa_renderer_t*r=SA_Create(2,0);assert(r);bad_effect=r->playback[0].effect;
 sa_settings_t settings={.hrtf=1,.underwater_alpha=1};SA_SetSettings(r,&settings);
 float pcm[SA_BLOCK];for(int i=0;i<SA_BLOCK;i++)pcm[i]=.2f;
 sa_sample_t sample={pcm,SA_BLOCK,0};
 sa_source_t source={.sample=&sample,.generation=1,.active=1,.kind=SA_POSITIONAL,.position_valid=1,.origin={8,184,-224},.gain=1};SA_SetSource(r,0,&source);
 source.kind=SA_DRY;source.gain=.75f;SA_SetSource(r,1,&source);
 sa_listener_t listener={.timestamp=1};
 /* Startup/invalidation must preserve the already-owned canonical listener. */
 SA_SetListener(r,&listener);assert(r->listener.forward[0]==1&&r->listener.right[1]==-1&&r->listener.up[2]==1);
 listener.origin[0]=NAN;SA_SetListener(r,&listener);assert(isfinite(r->listener.origin[0]));
 listener.origin[0]=0;listener.forward[0]=100;listener.right[1]=-2;listener.up[2]=3;
 SA_SetListener(r,&listener);
 float out[SA_BLOCK*2],music[SA_BLOCK*2];for(int i=0;i<SA_BLOCK*2;i++)music[i]=.05f;
 for(int i=0;i<4;i++)SA_Render(r,out,SA_BLOCK);
 unsigned long long before=r->stats.nonfinite;
 for(int mode=0;mode<3;mode++)for(int v=0;v<3;v++){
  settings.hrtf=mode!=1;SA_SetSettings(r,&settings);
  source.kind=mode==2?SA_DRY:SA_POSITIONAL;source.gain=1;SA_SetSource(r,0,&source);
  fault=0;for(int n=0;n<4;n++)SA_Render(r,out,SA_BLOCK);
  invalid=v==0?NAN:v==1?INFINITY:-INFINITY;fault=1;
  for(int n=0;n<8;n++){
   assert(SA_WriteMusic(r,music,SA_BLOCK)==SA_BLOCK);SA_Render(r,out,SA_BLOCK);
   for(int i=0;i<SA_BLOCK*2;i++)assert(isfinite(out[i]));
   if(mode!=1)for(int i=0;i<SA_BLOCK*2;i++)assert(fabsf(out[i]-.4f)<.00001f);
  }
 }
 settings.hrtf=1;SA_SetSettings(r,&settings);source.kind=SA_POSITIONAL;source.gain=2;SA_SetSource(r,0,&source);
 fault=0;for(int n=0;n<4;n++)SA_Render(r,out,SA_BLOCK);
 invalid=2e38f;fault=1;
 for(int n=0;n<8;n++){
  assert(SA_WriteMusic(r,music,SA_BLOCK)==SA_BLOCK);SA_Render(r,out,SA_BLOCK);
  for(int i=0;i<SA_BLOCK*2;i++)assert(isfinite(out[i])&&fabsf(out[i]-.6f)<.00001f);
 }
 /* Tail failure still preserves the independent dry source and music. */
 source.gain=1;source.sample=NULL;SA_SetSource(r,0,&source);r->playback[0].tail=1;invalid=NAN;
 assert(SA_WriteMusic(r,music,SA_BLOCK)==SA_BLOCK);SA_Render(r,out,SA_BLOCK);
 for(int i=0;i<SA_BLOCK*2;i++)assert(isfinite(out[i])&&fabsf(out[i]-.2f)<.00001f);
 assert(applies>0&&injected==81&&r->stats.nonfinite==before);
 assert(r->stats.binaural_nonfinite_blocks==81&&r->stats.rt_allocations==0);
 printf("BINAURAL_CONTAINMENT_PASSED fault_blocks=%d finite_dry_music=exact allocations=%llu\n",injected,(unsigned long long)r->stats.rt_allocations);
 SA_Destroy(r);return 0;
}
