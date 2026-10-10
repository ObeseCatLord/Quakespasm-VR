/* Actual production recognizer/merge; link locomotion, aim and native math with
 * function/data GC. No hardware, QC dispatch or production input lifecycle. */
#include "../Quake/vr_input.c"
#include <assert.h>

client_state_t cl;
client_static_t cls;
keydest_t key_dest = key_game;
enum m_state_e m_state = m_none;
cvar_t vr_aimmode = {.value=7};
cvar_t vr_gunmodelscale = {.value=1};
cvar_t vr_gunmodelpitch = {.value=0};
cvar_t vr_gunmodely = {.value=0};
static vrxr_frame_t frame;
static qmodel_t model, world, held_model, other_held_model;
static entity_t entities[2];
static entity_t held_entity;
static aliashdr_t geometry, enhanced_geometry, held_geometry, other_geometry;
static aliashdr_t *selected_geometry;
static vr_melee_gesture_profile_t profile;
static float units = 100;
static int controller_profile = VRXR_PROFILE_TOUCH;
static qboolean cached_blade, prepared_held, raw_held_available, matrix_available, muzzle_available;
static float wrist_angle[2];
static int matrix_calls[2], raw_held_calls;
static const stockaxe_edge_t blade = {.valid=true,.base={12,4,0},.tip={30,4,0}};
static const stockaxe_edge_t held_blade = {.valid=true,.base={15,3,0},.tip={32,3,0}};
static const mod_held_melee_recipe_t held_recipe = {
	.source="progs/v_axe.mdl",.contact_profile=VR_WEAPON_CONTACT_PROFILE_QBJ3};
static const mod_akimbo_pair_recipe_t pair_recipe = {
	.game="dwell",.source="progs/v_axeb.mdl"};

const vrxr_frame_t *GL_OpenXRFrame (void) { return &frame; }
qboolean V_TrackedSessionActive (void) { return true; }
qboolean M_WaitingForKeyBinding (void) { return false; }
qboolean Key_InputGrabActive (void) { return false; }
qboolean CL_AngleLocked (void) { return false; }
qboolean VR_WeaponMenu_IsOpenVR (void) { return false; }
qboolean VR_WeaponCalibrationAdjustActive (void) { return false; }
qboolean V_TrackedPresentationYaw (float *yaw) { *yaw=0; return true; }
float V_VRUnitsPerMetre (void) { return units; }
double Cvar_VariableValue (const char *name) { (void)name; return 32; }
qboolean V_TrackedPresentationHandAngles (int hand, vec3_t angles)
{
	vrxr_device_t device;
	return VR_LocomotionControllerDevice (&frame, hand, &device) &&
		VR_LocomotionHandAngles (device.matrix, 0, 32, angles);
}
const mod_held_melee_recipe_t *Mod_GetHeldMeleeRecipe (const char *name)
{ return prepared_held && !strcmp(name,held_recipe.source) ? &held_recipe : NULL; }
const mod_akimbo_pair_recipe_t *Mod_GetAkimboPairRecipe (const char *name)
{ return !strcmp(name,pair_recipe.source) ? &pair_recipe : NULL; }
void *Mod_Extradata_CheckSkin (qmodel_t *m, int skin)
{ return m==cl.viewent.model && skin==cl.viewent.skinnum ? selected_geometry : NULL; }
static qboolean classic_edge (qmodel_t *m, int skin, uint32_t crc,
	stockaxe_edge_t *out)
{
	memset(out,0,sizeof(*out));
	if (!cached_blade || m->stockaxe_edge.source_crc32!=crc ||
		!Mod_Extradata_CheckSkin(m,skin) || selected_geometry->poseverttype!=PV_QUAKE1)
		return false;
	*out=blade; out->source_crc32=crc; return true;
}
qboolean Mod_GetStockAxeEdge (qmodel_t *m, int skin, stockaxe_edge_t *out)
{
	memset(out,0,sizeof(*out));
	return !strcmp(m->name,"progs/v_axe.mdl") && classic_edge(m,skin,0x2aa03605u,out);
}
qboolean Mod_GetAlkalineAxeEdge (qmodel_t *m, int skin, stockaxe_edge_t *out)
{
	memset(out,0,sizeof(*out));
	return !strcmp(m->name,"progs/v_alkaxe20fps.mdl") && classic_edge(m,skin,0x3003ca78u,out);
}
qboolean Mod_GetCopperAxeEdge (qmodel_t *m, int skin, stockaxe_edge_t *out)
{
	memset(out,0,sizeof(*out));
	return (!strcmp(m->name,"progs/v_axe.mdl") || !strcmp(m->name,"progs/v_axe2.mdl")) &&
		classic_edge(m,skin,0xf5d8df1bu,out);
}
qboolean Mod_GetMD5StockAxeEdge (const qmodel_t *m, const aliashdr_t *g,
	int pose0, int pose1, stockaxe_edge_t *out)
{
	memset(out,0,sizeof(*out));
	if (!cached_blade || strcmp(m->name,"progs/v_axe.mdl") || g!=selected_geometry ||
		(g->poseverttype!=PV_MD5 && g->poseverttype!=PV_MD5_8) || pose0!=0 || pose1!=0)
		return false;
	*out=blade; VectorScale(out->base,3,out->base); VectorScale(out->tip,3,out->tip);
	return true;
}
qboolean VR_WeaponCalibrationLookupMelee (const char *name, vr_melee_gesture_profile_t *out)
{ (void)name; *out=profile; return profile.enabled; }
qboolean VR_WeaponCalibrationLookupHeld (const char *name, qboolean enhanced, vec3_t out, float *scale)
{ (void)name; (void)enhanced; VectorClear(out); *scale=1; return true; }
qboolean VR_WeaponCalibrationCurrentMuzzle (vec3_t out)
{ VectorClear(out); out[2]=fminf(1.41f*units,90); return muzzle_available; }
qboolean V_HeldMeleeEdgeOffsets (vec3_t base, vec3_t tip, vec3_t delta)
{ (void)base; (void)tip; (void)delta; return false; }
int R_AliasViewmodelHandMatrix (entity_t *e, const aliashdr_t *g,
	lerpdata_t *lerp, float matrix[16], int hand)
{
	vec3_t forward, right, up;
	const float format_scale=(g->poseverttype==PV_MD5 || g->poseverttype==PV_MD5_8) ? 1.0f/3 : 1;
	const float scale=units/100*format_scale*vr_gunmodelscale.value*ENTSCALE_DECODE(e->netstate.scale);
	assert(e==&cl.viewent && g==selected_geometry && hand>=0 && hand<2);
	assert(VectorLength(lerp->origin)==0); /* Raw offsets have no render translation. */
	matrix_calls[hand]++;
	if (!matrix_available) return -1;
	AngleVectors(lerp->angles,forward,right,up);
	memset(matrix,0,16*sizeof(*matrix)); matrix[15]=1;
	for(int axis=0;axis<3;++axis)
	{
		const float mirror=hand==0 ? -1 : 1;
		matrix[axis]=scale*forward[axis]*g->scale[0];
		matrix[4+axis]=-scale*right[axis]*g->scale[1]*mirror;
		matrix[8+axis]=scale*up[axis]*g->scale[2];
		matrix[12+axis]=scale*(forward[axis]*g->scale_origin[0]-
			right[axis]*g->scale_origin[1]*mirror+up[axis]*g->scale_origin[2]);
	}
	return hand==0;
}
entity_t *V_HeldMeleeEntity (void)
{ return prepared_held ? &held_entity : NULL; }
qboolean V_HeldMeleeRawEdgeOffsets (const vec3_t angles, vec3_t base, vec3_t tip)
{
	vec3_t model_angles, forward, right, up;
	raw_held_calls++;
	if (!prepared_held || !raw_held_available ||
		!VR_LocomotionHandRotToViewmodelAngles(angles,model_angles,vr_gunmodelpitch.value))
		return false;
	AngleVectors(model_angles,forward,right,up);
	for(int point=0;point<2;++point)
	{
		const float *local=point ? held_blade.tip : held_blade.base;
		float *out=point ? tip : base;
		for(int axis=0;axis<3;++axis)
			out[axis]=units/100*(local[0]*forward[axis]-local[1]*right[axis]*
				(VR_InputDominantPhysicalHand()==0 ? -1 : 1)+local[2]*up[axis]);
	}
	return true;
}

static void reset (int hand, qboolean authored, float speed)
{
	memset (&cl,0,sizeof(cl)); memset (&cls,0,sizeof(cls));
	memset (&frame,0,sizeof(frame)); memset (vr_input_hands,0,sizeof(vr_input_hands));
	memset (vr_input_generic_melee,0,sizeof(vr_input_generic_melee));
	memset (&model,0,sizeof(model)); memset (&geometry,0,sizeof(geometry));
	memset(wrist_angle,0,sizeof(wrist_angle)); memset(matrix_calls,0,sizeof(matrix_calls));
	cached_blade=matrix_available=raw_held_available=muzzle_available=true;
	prepared_held=false; raw_held_calls=0;
	selected_geometry=&geometry;
	vr_lefthanded.value = hand==0;
	vr_immersive_melee.value = 1;
	cls.state=ca_connected; cls.signon=SIGNONS;
	cl.stats[STAT_HEALTH]=100; cl.stats[STAT_WEAPON]=1; cl.stats[STAT_ACTIVEWEAPON]=1;
	cl.worldmodel=&world; cl.entities=entities; cl.viewentity=1; cl.num_entities=2;
	cl.model_precache[1]=&model; cl.viewent.model=&model;
	cl.viewent.netstate.scale=ENTSCALE_DEFAULT;
	model.type=mod_alias; strcpy(model.name,"progs/v_axe.mdl");
	model.stockaxe_edge=blade; model.stockaxe_edge.source_crc32=0x2aa03605u;
	geometry.poseverttype=PV_QUAKE1; geometry.numframes=geometry.numposes=1;
	for(int axis=0;axis<3;++axis) { geometry.scale[axis]=2; geometry.scale_origin[axis]=axis+1; }
	enhanced_geometry=geometry; enhanced_geometry.poseverttype=PV_MD5;
	held_geometry=other_geometry=geometry;
	model.extradata[PV_QUAKE1]=(byte *)&geometry;
	model.extradata[PV_MD5]=(byte *)&enhanced_geometry;
	held_model.extradata[PV_QUAKE1]=(byte *)&held_geometry;
	other_held_model.extradata[PV_QUAKE1]=(byte *)&other_geometry;
	held_entity=(entity_t){.model=&held_model};
	frame.focused=frame.should_render=1; frame.sample_id=1; frame.sample_time_seconds=1;
	for (int i=0;i<3;++i) frame.devices[0].matrix[i][i]=1;
	frame.devices[0].valid=frame.devices[0].tracked=1;
	frame.devices[0].kind=VRXR_DEVICE_HEAD; frame.devices[0].hand=-1;
	for (int h=0;h<2;++h)
	{
		vrxr_device_t *device=&frame.devices[h+1];
		for (int i=0;i<3;++i) device->matrix[i][i]=1;
		device->valid=device->tracked=device->velocity_valid=device->angular_velocity_valid=1;
		device->kind=VRXR_DEVICE_HAND; device->hand=h;
		frame.hands[h].active=1; frame.hands[h].profile=controller_profile;
		vr_input_hands[h].identity_valid=true;
		vr_input_hands[h].role=VR_InputRoleForPhysicalHand(h);
		vr_input_hands[h].profile=frame.hands[h].profile;
	}
	profile=(vr_melee_gesture_profile_t){.enabled=true,.has_speed=authored,.speed=speed};
	vr_input_context=VR_InputCurrentContext(); vr_input_context_valid=true;
	VR_InputPrepareGenericMelee (&frame,hand);
}

static void step (int hand, float dx, float head_dx, float speed)
{
	frame.devices[hand+1].matrix[0][3]+=dx;
	frame.devices[0].matrix[0][3]+=head_dx;
	frame.devices[hand+1].velocity[0]=speed;
	frame.sample_id++; frame.sample_time_seconds+=.02;
	VR_InputPrepareGenericMelee (&frame,hand);
}

/* Rotate the actual XR grip about reference-space up, with matching omega.
 * Production Index correction contributes its own translated-grip velocity. */
static void wrist_step (int hand, float radians)
{
	vrxr_device_t *device=&frame.devices[hand+1];
	wrist_angle[hand]+=radians;
	const float cosine=cosf(wrist_angle[hand]), sine=sinf(wrist_angle[hand]);
	device->matrix[0][0]=device->matrix[2][2]=cosine;
	device->matrix[0][2]=sine; device->matrix[2][0]=-sine;
	VectorClear(device->velocity); VectorClear(device->angular_velocity);
	device->angular_velocity[1]=radians/.02f;
	frame.sample_id++; frame.sample_time_seconds+=.02;
	VR_InputPrepareGenericMelee(&frame,hand);
}

static void check_angular_blade (int hand, qboolean enhanced, qboolean held)
{
	reset(hand,false,1.5f);
	if (enhanced) selected_geometry=&enhanced_geometry;
	prepared_held=held;
	muzzle_available=false; /* Known endpoints do not require fallback calibration. */
	VR_InputPrepareGenericMelee(&frame,hand);
	assert(vr_input_generic_melee[hand].valid);
	assert(vr_input_generic_melee[hand].edge_source==(held ? 2 : 1));
	assert(!cl.vr_weapon_contact_mode); /* Geometry does not need combat capabilities. */
	for(int i=0;i<40;++i) wrist_step(hand,i%2 ? -.10f : .10f);
	assert(!vr_input_generic_melee[hand].pending);
	assert(vr_input_generic_melee[hand].valid);
	for(int i=0;i<4;++i) wrist_step(hand,.20f);
	assert(vr_input_generic_melee[hand].pending);
	assert(VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK);
	assert(held ? raw_held_calls>0 : matrix_calls[hand]>0);
	assert(matrix_calls[1-hand]==0);
	/* Continuous angular travel remains consumed until rest or reversal. */
	wrist_step(hand,.20f);
	assert(!(VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK));
	wrist_step(hand,-.20f);
	assert(!vr_input_generic_melee[hand].consumed && !vr_input_generic_melee[hand].pending);
	for(int i=0;i<4;++i) wrist_step(hand,-.20f);
	assert(VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK);
	if (units==40)
	{
		reset(hand,false,1.5f); cached_blade=false;
		strcpy(model.name,"progs/unknown.mdl");
		VR_InputPrepareGenericMelee(&frame,hand);
		assert(vr_input_generic_melee[hand].valid && vr_input_generic_melee[hand].edge_source==0);
		wrist_step(hand,.10f);
		assert(vr_input_generic_melee[hand].pending); /* Same wiggle with fictitious lever. */
	}
}

static void assert_rebaseline (int hand, int source)
{
	step(hand,.04f,0,2);
	assert(vr_input_generic_melee[hand].valid && vr_input_generic_melee[hand].edge_source==source);
	assert(vr_input_generic_melee[hand].arc==0 && !vr_input_generic_melee[hand].pending &&
		!vr_input_generic_melee[hand].consumed);
}

static void check_sources (int hand)
{
	qmodel_t alternate_model;
	reset(hand,false,1.5f); step(hand,.04f,0,2);
	alternate_model=model; cl.model_precache[1]=cl.viewent.model=&alternate_model;
	assert_rebaseline(hand,1);
	step(hand,.04f,0,2); geometry.numposes=2; geometry.frames[0].firstpose=1;
	assert_rebaseline(hand,0);
	step(hand,.04f,0,2); geometry.frames[0].firstpose=0; assert_rebaseline(hand,1);
	step(hand,.04f,0,2);
	prepared_held=true; assert_rebaseline(hand,2);
	step(hand,.04f,0,2); held_entity.model=&other_held_model; assert_rebaseline(hand,2);
	step(hand,.04f,0,2); held_entity.frame=1; assert_rebaseline(hand,2);
	step(hand,.04f,0,2); other_geometry.poseverttype=PV_MD5; assert_rebaseline(hand,2);
	step(hand,.04f,0,2); raw_held_available=false; assert_rebaseline(hand,1);
	step(hand,.04f,0,2); prepared_held=false; cached_blade=false; assert_rebaseline(hand,0);
	step(hand,.04f,0,2); cached_blade=true; assert_rebaseline(hand,1);
	step(hand,.04f,0,2); selected_geometry=&enhanced_geometry; assert_rebaseline(hand,1);
	step(hand,.04f,0,2); enhanced_geometry.poseverttype=PV_MD5_8; assert_rebaseline(hand,1);
	/* Effective selected pose changes, including getter rejection of non-ready poses. */
	step(hand,.04f,0,2); enhanced_geometry.frames[0].firstpose=1;
	assert_rebaseline(hand,0);
	step(hand,.04f,0,2); enhanced_geometry.frames[0].firstpose=0;
	assert_rebaseline(hand,1);
	/* Final merge independently rejects a source change after a pending strike. */
	for(int i=0;i<3;++i) step(hand,.04f,0,2);
	assert(vr_input_generic_melee[hand].pending);
	cached_blade=false;
	assert(!(VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK));
	assert(!vr_input_generic_melee[hand].valid);
	reset(hand,false,1.5f); matrix_available=false;
	step(hand,.04f,0,2); assert(!vr_input_generic_melee[hand].valid);
}

static void check_authored_endpoints (int hand, qboolean held)
{
	vec3_t known[2], render[2], authored[2];
	vr_input_generic_melee_t identity;
	stockaxe_edge_t edge={.valid=true,.base={7,2,1},.tip={21,5,3}};
	reset(hand,false,1.5f); prepared_held=held;
	assert(VR_InputGenericMeleeIdentity(&frame,hand,&identity,known));
	assert(VR_InputStockAxeRenderEdgeOffsets(hand,selected_geometry,&edge,authored[0],authored[1]));
	for(int mask=1;mask<=3;++mask)
	{
		profile.has_base=mask&1; profile.has_tip=!!(mask&2);
		VectorCopy(edge.base,profile.base); VectorCopy(edge.tip,profile.tip);
		assert(VR_InputGenericMeleeIdentity(&frame,hand,&identity,render));
		for(int point=0;point<2;++point)
			for(int axis=0;axis<3;++axis)
				assert(fabsf(render[point][axis]-((mask&(1<<point)) ? authored[point][axis] : known[point][axis]))<.0001f);
		assert_rebaseline(hand,mask==3 ? 0 : (held ? 2 : 1));
	}
}

static void check_classic_selection (int hand)
{
	const char *names[]={"progs/v_axe.mdl","progs/v_axe.mdl","progs/v_axe2.mdl",
		"progs/v_alkaxe20fps.mdl","progs/v_axe.mdl"};
	const uint32_t crcs[]={0x2aa03605u,0xf5d8df1bu,0xf5d8df1bu,0x3003ca78u,0xdeadbeefu};
	const unsigned int contact_profiles[]={0,VR_WEAPON_CONTACT_PROFILE_STOCK,
		VR_WEAPON_CONTACT_PROFILE_COPPER,VR_WEAPON_CONTACT_PROFILE_ALK};
	vr_input_generic_melee_t identity;
	vec3_t render[2];
	for(size_t model_case=0;model_case<sizeof(crcs)/sizeof(crcs[0]);++model_case)
	{
		reset(hand,false,1.5f);
		strcpy(model.name,names[model_case]); model.stockaxe_edge.source_crc32=crcs[model_case];
		for(size_t p=0;p<sizeof(contact_profiles)/sizeof(contact_profiles[0]);++p)
		{
			cl.vr_weapon_contact_profile=contact_profiles[p];
			assert(!cl.vr_weapon_contact_mode);
			assert(VR_InputGenericMeleeIdentity(&frame,hand,&identity,render));
			assert(identity.edge_source==(model_case==4 ? 0 : 1));
		}
	}
	/* Authored ready frame 1 maps to effective pose 1, outside every classic cache. */
	reset(hand,false,1.5f);
	aliashdr_t *poses=calloc(1,sizeof(*poses)+sizeof(poses->frames[0]));
	assert(poses);
	*poses=geometry; poses->numframes=poses->numposes=2; poses->frames[1].firstpose=1;
	selected_geometry=poses; model.extradata[PV_QUAKE1]=(byte *)poses;
	profile.ready_frame=1; profile.has_ready_frame=true;
	assert(VR_InputGenericMeleeIdentity(&frame,hand,&identity,render));
	assert(identity.edge_source==0 && identity.edge_pose==1 && VectorLength(render[0])==0);
	/* A partial authored tip remains authoritative; the missing base uses grip. */
	profile.has_tip=true; VectorCopy(blade.tip,profile.tip); muzzle_available=false;
	assert(VR_InputGenericMeleeIdentity(&frame,hand,&identity,render));
	assert(identity.edge_source==0 && VectorLength(render[0])==0 && VectorLength(render[1])>0);
	profile.has_base=true; VectorCopy(blade.base,profile.base);
	assert(VR_InputGenericMeleeIdentity(&frame,hand,&identity,render));
	assert(identity.edge_source==0 && VectorLength(render[0])>0);
	selected_geometry=&geometry; model.extradata[PV_QUAKE1]=(byte *)&geometry;
	free(poses);
}

static void check_pass_through (int hand)
{
	const unsigned int buttons=BUTTON_ATTACK|(1u<<4)|(1u<<2);
	reset(hand,false,1.5f); profile.enabled=false;
	assert(VR_InputMergeMeleeAttack(buttons,true)==buttons);
	profile.enabled=true; vr_immersive_melee.value=0;
	assert(VR_InputMergeMeleeAttack(buttons,true)==buttons);
	vr_immersive_melee.value=1; cls.state=ca_disconnected;
	assert(VR_InputMergeMeleeAttack(buttons,true)==buttons);
	reset(hand,false,1.5f); strcpy(model.name,pair_recipe.source); cached_blade=false;
	for(int h=0;h<2;++h)
	{
		VR_InputPrepareGenericMelee(&frame,h);
		for(int i=0;i<3;++i) step(h,.04f,0,2);
		assert(vr_input_generic_melee[h].pending);
	}
	assert(VR_InputMergeMeleeAttack(buttons,true)==(BUTTON_ATTACK|(1u<<2)));
	assert(!vr_input_generic_melee[0].pending && !vr_input_generic_melee[1].pending);
	assert(VR_InputMergeMeleeAttack(buttons,true)==(1u<<2));
}

static void check_recognizer (int hand)
{
	reset(hand,false,1.5f);
	for (int i=0;i<40;++i) step(hand,i%2 ? -.02f : .02f,0,2);
	assert (!vr_input_generic_melee[hand].pending);
	reset(hand,false,1.5f);
	step(hand,.04f,0,2); step(hand,.04f,0,2);
	assert (!vr_input_generic_melee[hand].pending); /* Implicit needs 10 cm. */
	reset(hand,true,1.25f);
	step(hand,.035f,0,1.75f); step(hand,.035f,0,1.75f);
	assert (vr_input_generic_melee[hand].pending); /* Authored keeps 6 cm. */
	reset(hand,false,1.5f);
	for (int i=0;i<3;++i) step(hand,.04f,.04f,2);
	assert (vr_input_generic_melee[hand].pending);
	assert (VR_InputMergeMeleeAttack(0,false)&BUTTON_ATTACK);
	assert (vr_input_generic_melee[hand].pending);
	assert (VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK);
	assert (!vr_input_generic_melee[hand].pending);
	step(hand,.04f,-.1f,2);
	assert (!(VR_InputMergeMeleeAttack(BUTTON_ATTACK,true)&BUTTON_ATTACK));
	/* A large reversing sample rearms but cannot earn a strike. */
	step(hand,-.15f,0,5);
	assert (!vr_input_generic_melee[hand].pending && !vr_input_generic_melee[hand].consumed);
	step(hand,-.04f,0,2); step(hand,-.04f,0,2); step(hand,-.04f,0,2);
	assert (VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK);
	reset(hand,false,1.5f);
	for (int i=0;i<6;++i) step(hand,0,.08f,0);
	assert (!vr_input_generic_melee[hand].pending);
	reset(hand,true,.25f);
	for (int i=0;i<14;++i) step(hand,.005f,0,.25f);
	assert (vr_input_generic_melee[hand].pending && profile.speed==.25f);
	frame.focused=0;
	assert (!(VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK));
	assert (!vr_input_generic_melee[hand].valid);
	reset(hand,false,1.5f);
	step(hand,.04f,0,2); step(hand,.04f,0,2); step(hand,.04f,0,2);
	frame.sample_time_seconds+=.11;
	assert (!(VR_InputMergeMeleeAttack(0,true)&BUTTON_ATTACK));
	reset(hand,false,1.5f);
	step(hand,.04f,0,2);
	frame.hands[hand].profile=controller_profile == VRXR_PROFILE_INDEX ?
		VRXR_PROFILE_TOUCH : VRXR_PROFILE_INDEX;
	VR_InputPrepareGenericMelee(&frame,hand);
	assert (!vr_input_generic_melee[hand].valid);
}

static void check_stroke (void)
{
	vr_input_generic_melee_t state={0};
	state.profile.speed=1.5f;
	vec3_t movement[2]={{4,0,0},{1,0,0}};
	float speed[2]={2,4};
	VR_InputAdvanceMeleeStroke(&state,movement,speed,100,1);
	assert (state.endpoint==0);
	movement[0][0]=1; movement[1][0]=4; speed[0]=.5f;
	for(int i=0;i<10;++i) VR_InputAdvanceMeleeStroke(&state,movement,speed,100,1.1+i*.02);
	assert (!state.pending); /* Other endpoint's high speed never qualifies it. */
	state=(vr_input_generic_melee_t){0}; state.profile.speed=1.5f;
	movement[0][0]=8; movement[1][0]=0; speed[0]=2;
	VR_InputAdvanceMeleeStroke(&state,movement,speed,100,1);
	movement[0][0]=1; speed[0]=.2f;
	VR_InputAdvanceMeleeStroke(&state,movement,speed,100,1.02);
	assert (state.arc>.08f && !state.pending);
	movement[0][0]=3; speed[0]=2;
	VR_InputAdvanceMeleeStroke(&state,movement,speed,100,1.04);
	assert (state.pending);
	state.pending=false; speed[0]=.2f; movement[0][0]=15;
	VR_InputAdvanceMeleeStroke(&state,movement,speed,100,1.06);
	assert (!state.pending && !state.consumed && state.arc==0);
}

int main (void)
{
	check_stroke();
	for(int index=0;index<2;++index)
	{
		controller_profile=index ? VRXR_PROFILE_INDEX : VRXR_PROFILE_TOUCH;
		for(int scale=0;scale<2;++scale)
		{
			units=scale ? 200 : 40;
			for(int hand=0;hand<2;++hand)
			{
				check_recognizer(hand);
				check_angular_blade(hand,false,false);
				check_angular_blade(hand,true,false);
				check_angular_blade(hand,false,true);
				check_sources(hand);
				check_authored_endpoints(hand,false);
				check_authored_endpoints(hand,true);
				check_classic_selection(hand);
				check_pass_through(hand);
			}
		}
	}
	puts("Real blade/held endpoints, angular strokes, source rebasing, authored precedence, both hands/Index and native intent passed");
	return 0;
}
