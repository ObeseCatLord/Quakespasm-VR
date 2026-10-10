/* Production pure held-scale/profile lookup, with only the model-provenance
 * predicate mocked. The model owner's fixture must qualify actual provenance.
 * Compile with function/data GC; no model lookup/loading or cvar registration. */
#include "../Quake/vr_weapon_calibration.c"
#include <assert.h>
#include <strings.h>

static qboolean official;
static const aliashdr_t *selected;
qboolean Mod_IsRereleaseReplacementGeometry (const qmodel_t *model,
	const aliashdr_t *geometry)
{
	return official && model->extradata[PV_QUAKE1] && geometry==selected &&
		(geometry->poseverttype==PV_MD5 || geometry->poseverttype==PV_MD5_8);
}
const mod_held_melee_recipe_t *Mod_GetHeldMeleeRecipe (const char *name)
{ (void)name; return NULL; }
int q_strcasecmp (const char *a, const char *b) { return strcasecmp(a,b); }

int main (void)
{
	qmodel_t model={0};
	aliashdr_t geometry={0}, classic={0}, other={0};
	model.extradata[PV_QUAKE1]=(byte *)&classic;
	geometry.poseverttype=PV_MD5; selected=&geometry; official=true;
	strcpy(model.name,"progs/v_axe.mdl");
	assert (fabsf(VR_WeaponCalibrationModelOffsetScale(&model,&geometry)-1.0f/3.0f)<.000001f);
	strcpy(model.name,"progs/v_shot2.mdl");
	assert (VR_WeaponCalibrationModelOffsetScale(&model,&geometry)==1);
	strcpy(model.name,"progs/v_shot.mdl");
	assert (VR_WeaponCalibrationModelOffsetScale(&model,&geometry)==.5f);
	assert (VR_WeaponCalibrationModelOffsetScale(&model,&other)==1);
	assert (VR_WeaponCalibrationModelOffsetScale(&model,&classic)==1);
	official=false;
	assert (VR_WeaponCalibrationModelOffsetScale(&model,&geometry)==1);
	official=true; model.extradata[PV_QUAKE1]=NULL;
	assert (VR_WeaponCalibrationModelOffsetScale(&model,&geometry)==1);
	assert (VR_WeaponCalibrationModelOffsetScale(NULL,&geometry)==1);
	assert (VR_WeaponCalibrationModelOffsetScale(&model,NULL)==1);

	vr_melee_gesture_profile_t out;
	vr_weapon_calibration_initialized=true;
	assert (VR_WeaponCalibrationLookupMelee("progs/v_axe.mdl",&out));
	assert (out.speed==1.5f && !out.has_speed);
	VR_WeaponOffsetCvar(0,VR_WOFS_ID).string="progs/v_axe.mdl";
	const float speeds[]={.25f,1.25f,1.5f,3};
	for (size_t i=0;i<sizeof(speeds)/sizeof(speeds[0]);++i)
	{
		vr_weapon_calibration_slots[0].melee.has_speed=true;
		vr_weapon_calibration_slots[0].melee.speed=speeds[i];
		assert (VR_WeaponCalibrationLookupMelee("progs/v_axe.mdl",&out));
		assert (out.has_speed && out.speed==speeds[i]);
	}
	vr_weapon_calibration_slots[0].melee.has_enabled=true;
	vr_weapon_calibration_slots[0].melee.enabled=false;
	assert (!VR_WeaponCalibrationLookupMelee("progs/v_axe.mdl",&out));
	puts("Pure held provenance factor and exact authored/default melee speeds passed");
	return 0;
}
