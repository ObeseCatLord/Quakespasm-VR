/* Offline diagnostic: current engine retargeter vs fixed bind-local offsets.
 * The latter isolates translation error; it is NOT a complete retargeter.
 * Input generated privately by retarget_probe.py, supplied via include path.
 */
#define main original_fixture_main
#include "../../tests/avatar_retarget_fixture.c"
#undef main
typedef struct {const char *name; md5livejoint_t *joints; int count; const char *semantics[19];} probe_model_t;
#include "retarget_input.h"

static float length_between(const float *a, const float *b)
{
    float sum=0; int k;
    for(k=3;k<12;k+=4)sum+=(a[k]-b[k])*(a[k]-b[k]);
    return sqrtf(sum);
}

int main(void)
{
    md5liveinfo_t sl={0}; r_avatar_rig_t sr;
    int model,frame,chain,j,k,group;
    static const int ends[][2]={{6,7},{7,8},{10,11},{11,12},{13,14},{14,15},{16,17},{17,18}};
    static const char *labels[]={"upperarm_L","forearm_L","upperarm_R","forearm_R","thigh_L","shin_L","thigh_R","shin_R"};
    sl.joints=ranger_joints; sl.numbones=sizeof(ranger_joints)/sizeof(*ranger_joints);
    assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER),&sl,&sr));
    puts("model,scope,segment,bind_units,min_ratio,max_ratio,worst_frame,max_fixed_relative_error");
    for(model=0;model<(int)(sizeof(models)/sizeof(*models));model++) for(group=0;group<2;group++) {
        probe_model_t *m=&models[model]; md5liveinfo_t tl={0};
        r_avatar_profile_t profile={0};r_avatar_rig_t tr;r_avatar_presentation_context_t ctx;
        float minimum[8],maximum[8]={0},error[8]={0},bindlen[8];int worst[8]={0};
        float output[MAX_MD5_JOINTS*12],fixed[MAX_MD5_JOINTS*12];
        tl.joints=m->joints;tl.numbones=m->count;profile.display_scale=1;
        profile.capabilities=R_AVATAR_CAP_RETARGET|R_AVATAR_CAP_HEAD|R_AVATAR_CAP_ARMS|R_AVATAR_CAP_LEGS;
        for(j=0;j<19;j++)profile.joint[j].name=m->semantics[j];
        assert(R_AvatarResolveRig(&profile,&tl,&tr));
        assert(R_AvatarBuildPresentationContext(&sr,&tr,&ctx));
        for(chain=0;chain<8;chain++) {
            bindlen[chain]=length_between(tl.joints[tr.joint[ends[chain][0]]].bind,tl.joints[tr.joint[ends[chain][1]]].bind);
            assert(bindlen[chain]>0);minimum[chain]=1e9;
        }
        for(frame=0;frame<(int)(sizeof(frames)/sizeof(*frames));frame++) {
            /* QBJ3 player.qc: death slots 41..102; retain all other actions. */
            if(group && frame>=41 && frame<=102)continue;
            assert(R_AvatarRetargetPaletteWithContext(&sr,&tr,&ctx,frames[frame],output));
            if(model==0)for(j=0;j<sl.numbones;j++) {
                int mapped=0;
                for(k=0;k<19;k++)if(sr.joint[k]==j)mapped=1;
                if(mapped)for(k=0;k<12;k++)assert(fabsf(output[j*12+k]-frames[frame][j*12+k])<.001f);
            }
            memcpy(fixed,output,tl.numbones*12*sizeof(float));
            /* Keep the engine's rotations and Hip animation, but reconstruct
             * every other local translation from the target bind hierarchy. */
            for(j=0;j<tl.numbones;j++) {
                int parent=tl.joints[j].parent;float inv[12],local[12],desired[12];
                if(parent<0||j==tr.joint[MD5_VRIK_HIP])continue;
                inverse(tl.joints[parent].bind,inv);multiply(inv,tl.joints[j].bind,local);
                multiply(fixed+parent*12,local,desired);
                for(k=3;k<12;k+=4)fixed[j*12+k]=desired[k];
            }
            for(chain=0;chain<8;chain++) {
                int a=tr.joint[ends[chain][0]],b=tr.joint[ends[chain][1]];
                float ratio=length_between(output+a*12,output+b*12)/bindlen[chain];
                float corrected=length_between(fixed+a*12,fixed+b*12)/bindlen[chain];
                assert(fabsf(corrected-1)<.0001f);
                if(ratio<minimum[chain])minimum[chain]=ratio;
                if(ratio>maximum[chain]){maximum[chain]=ratio;worst[chain]=frame;}
                error[chain]=fmaxf(error[chain],fabsf(corrected-1));
            }
        }
        for(chain=0;chain<8;chain++)printf("%s,%s,%s,%.6f,%.6f,%.6f,%d,%.9g\n",m->name,group?"living":"all",labels[chain],bindlen[chain],minimum[chain],maximum[chain],worst[chain],error[chain]);
    }
    return 0;
}
