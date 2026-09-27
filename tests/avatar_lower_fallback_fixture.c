/* Run the real Dog profile through independent upper and lower repairs. */
#define main avatar_retarget_fixture_main
#include "avatar_retarget_fixture.c"
#undef main

static void set_source_goal(const r_avatar_presentation_context_t *context,
	float source_palette[][12],int joint,const float target[3])
{
	float canonical[3];
	R_AvatarPresentationPoint(context,target,canonical);
	for(int axis=0;axis<3;++axis)source_palette[joint][axis*4+3]=canonical[axis];
}

static void matrix_origin(const float matrix[12],float out[3])
{
	for(int axis=0;axis<3;++axis)out[axis]=matrix[axis*4+3];
}

int main(void)
{
	fixture_t source,target;
	r_avatar_rig_t sr,tr;
	r_avatar_presentation_context_t context;
	float source_palette[R_AVATAR_MAX_JOINTS][12];
	float target_palette[R_AVATAR_MAX_JOINTS][12],before[R_AVATAR_MAX_JOINTS][12];
	float prepared_palette[R_AVATAR_MAX_JOINTS][12];
	float without_hip[R_AVATAR_MAX_JOINTS][12],turn[12];
	float goal[3];
	int left,right,head,tail,mapped_child;
	const r_avatar_profile_t *dog=R_AvatarProfileForId(PLAYER_AVATAR_DOG);

	ranger(&source,1);
	named_profile(&target,dog);
	{
		int count=target.live.joint_count;
		int mapped_leg=-1;
		add(&target,&count,"TailTest",0,0,-1,0);
		tail=count-1;
		for(int joint=0;joint<count;++joint)
			if(!strcmp(target.joints[joint].name,
				dog->joint[MD5_VRIK_UPPERLEG_R].name))mapped_leg=joint;
		assert(mapped_leg>=0);
		add(&target,&count,"LegChildTest",mapped_leg,0,1,0);
		mapped_child=count-1;
		target.live.joint_count=count;
	}
	/* The generic named fixture has its feet above its Hip; give this
	 * synthetic Dog the quadruped's feet-below-Hip presentation basis. */
	for(int side=0;side<2;++side){
		int foot=side?MD5_VRIK_FOOT_R:MD5_VRIK_FOOT_L;
		int joint;
		for(joint=0;joint<target.live.joint_count;++joint)
			if(!strcmp(target.joints[joint].name,dog->joint[foot].name))break;
		assert(joint<target.live.joint_count);
		target.joints[joint].bind[11]=-3;
	}
	assert(R_AvatarResolveRig(R_AvatarProfileForId(PLAYER_AVATAR_RANGER),
		&source.live,&sr));
	assert(R_AvatarResolveRig(dog,&target.live,&tr));
	assert(R_AvatarBuildPresentationContext(&sr,&tr,&context));
	for(int joint=0;joint<source.live.joint_count;++joint)
		memcpy(source_palette[joint],source.joints[joint].bind,sizeof(source_palette[joint]));
	assert(R_AvatarRetargetPalette(&sr,&tr,(float *)source_palette,(float *)target_palette));
	memcpy(before,target_palette,target.live.joint_count*sizeof(before[0]));
	left=tr.joint[MD5_VRIK_FOOT_L];
	right=tr.joint[MD5_VRIK_FOOT_R];
	head=tr.joint[MD5_VRIK_HEAD];
	/* Left target equals its path root, so its solve must fail. Right foot
	 * and head request nearby, observable movements. */
	matrix_origin(target_palette[tr.joint[MD5_VRIK_UPPERLEG_L]],goal);
	set_source_goal(&context,source_palette,sr.joint[MD5_VRIK_FOOT_L],goal);
	matrix_origin(target_palette[right],goal);
	goal[0]+=.2f;
	set_source_goal(&context,source_palette,sr.joint[MD5_VRIK_FOOT_R],goal);
	matrix_origin(target_palette[head],goal);
	goal[2]+=.3f;
	set_source_goal(&context,source_palette,sr.joint[MD5_VRIK_HEAD],goal);
	assert(!R_AvatarRefineBuiltinPalette(&sr,&tr,true,
		(const float (*)[12])source_palette,0,
		R_AVATAR_TRACKED_FOOT_L|R_AVATAR_TRACKED_FOOT_R,
		target_palette,R_AVATAR_MAX_JOINTS));
	assert(!memcmp(target_palette[left],before[left],sizeof(before[left])));
	assert(fabsf(target_palette[right][3]-before[right][3])>.05f);
	assert(fabsf(target_palette[head][11]-before[head][11])>.05f);
	/* Frame staging supplies this floor-corrected context. The direct
	 * caller rebuilds it; both routes must preserve the same partial
	 * upper/lower rollback and final joint transforms. */
	{
		r_avatar_presentation_context_t staged=context;
		qboolean rebuilt,reused;
		R_AvatarPresentationAddCanonicalZ(&staged,1.25f);
		memcpy(target_palette,before,target.live.joint_count*sizeof(before[0]));
		memcpy(prepared_palette,before,target.live.joint_count*sizeof(before[0]));
		rebuilt=R_AvatarRefineBuiltinPalette(&sr,&tr,true,
			(const float (*)[12])source_palette,1.25f,
			R_AVATAR_TRACKED_FOOT_L|R_AVATAR_TRACKED_FOOT_R,
			target_palette,R_AVATAR_MAX_JOINTS);
		reused=R_AvatarRefineBuiltinPaletteWithContext(&sr,&tr,true,
			(const float (*)[12])source_palette,1.25f,
			R_AVATAR_TRACKED_FOOT_L|R_AVATAR_TRACKED_FOOT_R,
			prepared_palette,R_AVATAR_MAX_JOINTS,&staged);
		assert(rebuilt==reused);
		assert(!memcmp(target_palette,prepared_palette,
			target.live.joint_count*sizeof(target_palette[0])));
	}

	/* The Hip tracker turns an unmapped child; semantic rear legs retain
	 * their already retargeted global transforms. */
	for(int joint=0;joint<source.live.joint_count;++joint)
		memcpy(source_palette[joint],source.joints[joint].bind,sizeof(source_palette[joint]));
	rotation_z(turn);
	for(int axis=0;axis<3;++axis)
		for(int column=0;column<3;++column)
			source_palette[sr.joint[MD5_VRIK_HIP]][axis*4+column]=turn[axis*4+column];
	assert(R_AvatarRetargetPalette(&sr,&tr,(float *)source_palette,(float *)target_palette));
	memcpy(without_hip,target_palette,target.live.joint_count*sizeof(without_hip[0]));
	memcpy(prepared_palette,target_palette,target.live.joint_count*sizeof(prepared_palette[0]));
	R_AvatarRefineBuiltinPalette(&sr,&tr,true,(const float (*)[12])source_palette,
		0,0,without_hip,R_AVATAR_MAX_JOINTS);
	R_AvatarRefineBuiltinPalette(&sr,&tr,true,(const float (*)[12])source_palette,
		0,R_AVATAR_TRACKED_HIP,target_palette,R_AVATAR_MAX_JOINTS);
	R_AvatarRefineBuiltinPaletteWithContext(&sr,&tr,true,
		(const float (*)[12])source_palette,0,R_AVATAR_TRACKED_HIP,
		prepared_palette,R_AVATAR_MAX_JOINTS,&context);
	assert(!memcmp(target_palette,prepared_palette,
		target.live.joint_count*sizeof(target_palette[0])));
	assert(fabsf(target_palette[tail][3]-without_hip[tail][3])+
		fabsf(target_palette[tail][7]-without_hip[tail][7])>.1f);
	assert(!memcmp(target_palette[tr.joint[MD5_VRIK_UPPERLEG_R]],
		without_hip[tr.joint[MD5_VRIK_UPPERLEG_R]],sizeof(float)*12));
	assert(!memcmp(target_palette[mapped_child],without_hip[mapped_child],
		sizeof(float)*12));

	/* A failed hand path rolls back the upper transaction, while the right
	 * foot still reaches its independent lower goal. */
	for(int joint=0;joint<source.live.joint_count;++joint)
		memcpy(source_palette[joint],source.joints[joint].bind,sizeof(source_palette[joint]));
	memcpy(target.joints[tr.joint[MD5_VRIK_LOWERARM_L]].bind,
		target.joints[tr.joint[MD5_VRIK_UPPERARM_L]].bind,sizeof(float)*12);
	assert(R_AvatarRetargetPalette(&sr,&tr,(float *)source_palette,(float *)target_palette));
	memcpy(before,target_palette,target.live.joint_count*sizeof(before[0]));
	matrix_origin(target_palette[right],goal);
	goal[0]+=.2f;
	set_source_goal(&context,source_palette,sr.joint[MD5_VRIK_FOOT_R],goal);
	assert(!R_AvatarRefineBuiltinPalette(&sr,&tr,true,
		(const float (*)[12])source_palette,0,R_AVATAR_TRACKED_FOOT_R,
		target_palette,R_AVATAR_MAX_JOINTS));
	assert(!memcmp(target_palette[head],before[head],sizeof(before[head])));
	assert(fabsf(target_palette[right][3]-before[right][3])>.05f);
	puts("avatar lower fallback fixture: ok");
	return 0;
}
