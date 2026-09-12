/*
 * Narrow physical-contact QC adapter.  Include after SV_FriendlyFireBegin/End
 * and the existing sv_vr_contact_call declaration in sv_phys.c.  This owns no
 * stroke, network or client state: callers supply one already accepted trace.
 */
#ifndef QS_VR_MELEE_QC_H
#define QS_VR_MELEE_QC_H

typedef enum {
	SV_VR_MELEE_NONE,
	SV_VR_MELEE_STOCK_AXE,
	SV_VR_MELEE_QBJ3_WRENCH,
	SV_VR_MELEE_QBJ3_BERSERK,
	SV_VR_MELEE_ENYO_KATANA,
	SV_VR_MELEE_BONK_HAMMER,
	SV_VR_MELEE_DWELL_AXE,
	SV_VR_MELEE_DWELL_BERSERK,
	SV_VR_MELEE_HONEY_AXE,
	SV_VR_MELEE_AD_AXE,
	SV_VR_MELEE_COPPER_AXE,
	SV_VR_MELEE_ALK_AXE,
	SV_VR_MELEE_IMMORTAL_AXE,
	SV_VR_MELEE_IMMORTAL_HAMMER,
	SV_VR_MELEE_DRAKE_AXE,
	SV_VR_MELEE_MJOLNIR_AXE,
	SV_VR_MELEE_MJOLNIR_GUNGNIR,
	SV_VR_MELEE_MJOLNIR_SCIMITAR,
	SV_VR_MELEE_MJOLNIR_MACE,
	SV_VR_MELEE_HAMMER,
	SV_VR_MELEE_MJOLNIR_HAMMER,
	SV_VR_MELEE_DRAKE_HAMMER,
	SV_VR_MELEE_MJOLNIR_RAPIER
} sv_vr_melee_subtype_t;

enum {
	SV_VR_MELEE_FAMILY_NONE,
	SV_VR_MELEE_FAMILY_STOCK,
	SV_VR_MELEE_FAMILY_QBJ3,
	SV_VR_MELEE_FAMILY_ENYO,
	SV_VR_MELEE_FAMILY_BONK,
	SV_VR_MELEE_FAMILY_DWELL,
	SV_VR_MELEE_FAMILY_HONEY,
	SV_VR_MELEE_FAMILY_AD,
	SV_VR_MELEE_FAMILY_COPPER,
	SV_VR_MELEE_FAMILY_ALK,
	SV_VR_MELEE_FAMILY_IMMORTAL,
	SV_VR_MELEE_FAMILY_DRAKE,
	SV_VR_MELEE_FAMILY_MJOLNIR
};

#define SV_VR_MELEE_QBJ3_CRC 35566
#define SV_VR_MELEE_QBJ3_FOUNDRY_CRC 15169
#define SV_VR_MELEE_ENYO_CRC 22413
#define SV_VR_MELEE_BONK_CRC 23056
#define SV_VR_MELEE_BONK_HAMMER_SKIN_GLOBAL 873
#define SV_VR_MELEE_DWELL_CRC 505

static qboolean SV_VRMeleeFiniteVector(const vec3_t value)
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static qboolean SV_VRMeleeFunctionPin(int index, const char *name,
	int first_statement, int parm_start, int locals, int numparms,
	const byte *parm_sizes)
{
	dfunction_t *function;
	int i;

	if (!qcvm || !qcvm->progs || index < 0 || index >= qcvm->progs->numfunctions)
		return false;
	function = &qcvm->functions[index];
	if (strcmp(PR_GetString(function->s_name), name) ||
		function->first_statement != first_statement ||
		function->parm_start != parm_start || function->locals != locals ||
		function->numparms != numparms)
		return false;
	for (i = 0; i < numparms; ++i)
		if (function->parm_size[i] != parm_sizes[i])
			return false;
	return true;
}

#include "vr_melee_ad.h"
#include "vr_melee_copper.h"
#include "vr_melee_alk.h"
#include "vr_melee_immortal.h"
#include "vr_melee_drake.h"
#include "vr_melee_mjolnir.h"
#include "vr_melee_gungnir.h"
#include "vr_melee_scimitar.h"
#include "vr_melee_mace.h"
#include "vr_melee_hammer.h"
#include "vr_melee_mjolnir_hammer.h"
#include "vr_melee_drake_hammer.h"
#include "vr_melee_rapier.h"

typedef struct {
	int crc, statements, functions, globals;
	int leaf_index, leaf_first, leaf_parm_start;
	int sound_index, sound_first;
	int attack_index, attack_first, attack_parm_start;
	int frame_index, frame_first;
	int trace_statement;
	int stand_index, stand_first;
	int run_index, run_first, run_parm_start, run_locals;
	int weapon;
} sv_vr_melee_stock_descriptor_t;

/* These whole-VM identities have a conventional axe (Rogue uses bit 2048) and
 * progs/v_axe.mdl.  They retain their own QC; only the immediate W_FireAxe
 * traceline is borrowed by the physical-contact hook. */
static const sv_vr_melee_stock_descriptor_t sv_vr_melee_stock_descriptors[] = {
	{3064, 21118, 2116, 4171, 182, 3428, 3714, 227, 5009,
		218, 4538, 3828, 226, 4997, 3438, 268, 7605, 269, 7643, 0, 0, 4096},
	{57713, 28809, 2317, 5263, 207, 4834, 4500, 255, 6592,
		245, 6036, 4627, 254, 6582, 4844, 317, 9896, 318, 9934, 0, 0, 4096},
	{41041, 51977, 3281, 27669, 310, 7630, 5948, 363, 9575,
		354, 9098, 6078, 362, 9563, 7641, 454, 14689, 455, 14727, 0, 0, 4096},
	{48616, 35474, 2808, 5610, 199, 4818, 4725, 256, 7035,
		240, 6271, 4847, 255, 7025, 4828, 298, 10028, 299, 10079, 4899, 2, 4096},
	{15606, 42238, 3563, 7704, 208, 5040, 6273, 257, 7261,
		247, 6343, 6400, 256, 7251, 5050, 323, 10852, 324, 10890, 0, 0, 4096},
	{54028, 38916, 3316, 6420, 239, 7793, 5625, 284, 10018,
		275, 9046, 5739, 283, 9992, 7803, 326, 13158, 327, 13196, 0, 0, 2048},
	{23181, 58254, 3805, 31992, 315, 7954, 7534, 385, 11165,
		372, 10521, 7757, 384, 11153, 7965, 480, 16919, 481, 16957, 0, 0, 4096}
};

static qboolean SV_VRMeleeStockDescriptorValid(
	const sv_vr_melee_stock_descriptor_t *descriptor)
{
	static const byte rune_args[] = {1, 1};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->crc != descriptor->crc ||
		qcvm->progs->numstatements != descriptor->statements ||
		qcvm->progs->numfunctions != descriptor->functions ||
		qcvm->progs->numglobals != descriptor->globals)
		return false;
	/* Rogue's attack prelude modifies rune timers and its recovery interval.
	 * Pin both native calls rather than flattening its axe to stock timing. */
	if (descriptor->crc == 54028 &&
		(!SV_VRMeleeFunctionPin(122, "RuneApplyBlackNoise", 3287, 5410, 1,
			1, rune_args) ||
		 !SV_VRMeleeFunctionPin(124, "RuneApplyHell", 3310, 5413, 2,
			2, rune_args)))
		return false;
	return SV_VRMeleeFunctionPin(descriptor->leaf_index, "W_FireAxe",
		descriptor->leaf_first, descriptor->leaf_parm_start, 6, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->sound_index, "SuperDamageSound",
		descriptor->sound_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->attack_index, "W_Attack",
		descriptor->attack_first, descriptor->attack_parm_start, 1, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->frame_index, "W_WeaponFrame",
		descriptor->frame_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->stand_index, "player_stand1",
		descriptor->stand_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->run_index, "player_run",
		descriptor->run_first, descriptor->run_parm_start,
		descriptor->run_locals, 0, NULL);
}

static const sv_vr_melee_stock_descriptor_t *SV_VRMeleeStockDescriptor(void)
{
	int i;

	for (i = 0; i < (int)countof(sv_vr_melee_stock_descriptors); ++i)
		if (SV_VRMeleeStockDescriptorValid(&sv_vr_melee_stock_descriptors[i]))
			return &sv_vr_melee_stock_descriptors[i];
	return NULL;
}

static qboolean SV_VRMeleeStockProgs(void)
{
	return SV_VRMeleeStockDescriptor() != NULL;
}

/* Immediate-use accessors for the server hook.  Callers must not retain the
 * returned VM function pointer across a progs reload. */
static dfunction_t *SV_VRMeleeStockLeafFunction(void)
{
	const sv_vr_melee_stock_descriptor_t *descriptor = SV_VRMeleeStockDescriptor();

	return descriptor ? &qcvm->functions[descriptor->leaf_index] : NULL;
}

static dfunction_t *SV_VRMeleeStockSoundFunction(void)
{
	const sv_vr_melee_stock_descriptor_t *descriptor = SV_VRMeleeStockDescriptor();

	return descriptor ? &qcvm->functions[descriptor->sound_index] : NULL;
}

static int SV_VRMeleeStockTraceStatement(void)
{
	const sv_vr_melee_stock_descriptor_t *descriptor = SV_VRMeleeStockDescriptor();

	return descriptor ? descriptor->trace_statement : -1;
}

typedef struct {
	int crc, statements, functions, globals;
	int berserk_helper_index, berserk_helper_first, berserk_helper_parm_start;
	int wrench_index, wrench_first, wrench_parm_start;
	int wrench_leaf_index, wrench_leaf_first, wrench_leaf_parm_start;
	int berserk_index, berserk_first, berserk_parm_start;
	int berserk_leaf_index, berserk_leaf_first, berserk_leaf_parm_start;
	int idle_index, idle_first;
	int draw_index, draw_first;
	int sound_index, sound_first;
	int skillbutton_index, skillbutton_first;
} sv_vr_melee_qbj3_descriptor_t;

/* Both distributed and locally optimized QBJ3 programs retain the native
 * wrench/berserk ABI, but compiler layout changes move statements and locals.
 * Keep the complete VM identity and each borrowed entry point revision-pinned. */
static const sv_vr_melee_qbj3_descriptor_t sv_vr_melee_qbj3_descriptors[] = {
	{SV_VR_MELEE_QBJ3_CRC, 65612, 3850, 7502,
		145, 3264, 7382, 484, 14871, 7065, 485, 15056, 7382,
		571, 18810, 7127, 572, 19015, 7382, 569, 18757, 575, 19438,
		595, 20248, 882, 29999},
	{SV_VR_MELEE_QBJ3_FOUNDRY_CRC, 66172, 3850, 10635,
		145, 3317, 7450, 484, 15033, 8394, 485, 15218, 8411,
		571, 18996, 8722, 572, 19201, 8739, 569, 18942, 575, 19624,
		595, 20453, 882, 30448}
};

static qboolean SV_VRMeleeQBJ3DescriptorValid(
	const sv_vr_melee_qbj3_descriptor_t *descriptor)
{
	static const byte scalar[] = {1};
	static const byte entity_vector_vector[] = {1, 3, 3};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->crc != descriptor->crc ||
		qcvm->progs->numstatements != descriptor->statements ||
		qcvm->progs->numfunctions != descriptor->functions ||
		qcvm->progs->numglobals != descriptor->globals)
		return false;
	return SV_VRMeleeFunctionPin(descriptor->berserk_helper_index, "has_berserk",
			descriptor->berserk_helper_first, descriptor->berserk_helper_parm_start,
			1, 1, scalar) &&
		SV_VRMeleeFunctionPin(descriptor->wrench_index, "W_FireWrench",
			descriptor->wrench_first, descriptor->wrench_parm_start, 17, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->wrench_leaf_index, "hitwrench",
			descriptor->wrench_leaf_first, descriptor->wrench_leaf_parm_start, 11, 3,
			entity_vector_vector) &&
		SV_VRMeleeFunctionPin(descriptor->berserk_index, "W_Fire_Berserker_Multi",
			descriptor->berserk_first, descriptor->berserk_parm_start, 17, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->berserk_leaf_index, "hit_berserker_punch",
			descriptor->berserk_leaf_first, descriptor->berserk_leaf_parm_start, 11, 3,
			entity_vector_vector) &&
		SV_VRMeleeFunctionPin(descriptor->idle_index, "weaponanim_idle_melee_loop",
			descriptor->idle_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->draw_index, "weaponanim_draw_loop",
			descriptor->draw_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->sound_index, "SuperDamageSound",
			descriptor->sound_first, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(descriptor->skillbutton_index, "skillbutton_touch",
			descriptor->skillbutton_first, 0, 0, 0, NULL);
}

static const sv_vr_melee_qbj3_descriptor_t *SV_VRMeleeQBJ3Descriptor(void)
{
	int i;

	for (i = 0; i < (int)countof(sv_vr_melee_qbj3_descriptors); ++i)
		if (SV_VRMeleeQBJ3DescriptorValid(&sv_vr_melee_qbj3_descriptors[i]))
			return &sv_vr_melee_qbj3_descriptors[i];
	return NULL;
}

static qboolean SV_VRMeleeQBJ3Progs(void)
{
	return SV_VRMeleeQBJ3Descriptor() != NULL;
}

static dfunction_t *SV_VRMeleeQBJ3LeafFunction(sv_vr_melee_subtype_t subtype)
{
	const sv_vr_melee_qbj3_descriptor_t *descriptor = SV_VRMeleeQBJ3Descriptor();

	if (!descriptor)
		return NULL;
	if (subtype == SV_VR_MELEE_QBJ3_WRENCH)
		return &qcvm->functions[descriptor->wrench_leaf_index];
	if (subtype == SV_VR_MELEE_QBJ3_BERSERK)
		return &qcvm->functions[descriptor->berserk_leaf_index];
	return NULL;
}

static dfunction_t *SV_VRMeleeQBJ3SoundFunction(void)
{
	const sv_vr_melee_qbj3_descriptor_t *descriptor = SV_VRMeleeQBJ3Descriptor();

	return descriptor ? &qcvm->functions[descriptor->sound_index] : NULL;
}

static qboolean SV_VRMeleeQBJ3IdleThink(int think)
{
	const sv_vr_melee_qbj3_descriptor_t *descriptor = SV_VRMeleeQBJ3Descriptor();

	return descriptor && think == descriptor->idle_index;
}

static qboolean SV_VRMeleeQBJ3DrawThink(int think)
{
	const sv_vr_melee_qbj3_descriptor_t *descriptor = SV_VRMeleeQBJ3Descriptor();

	return descriptor && think == descriptor->draw_index;
}

static qboolean SV_VRMeleeQBJ3SkillButtonTouch(int touch)
{
	const sv_vr_melee_qbj3_descriptor_t *descriptor = SV_VRMeleeQBJ3Descriptor();

	return descriptor && touch == descriptor->skillbutton_index;
}

static qboolean SV_VRMeleeEnyoProgs(void)
{
	static const byte scalar[] = {1};
	static const byte entity_vector_vector[] = {1, 3, 3};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->crc != SV_VR_MELEE_ENYO_CRC ||
		qcvm->progs->numstatements != 60325 ||
		qcvm->progs->numfunctions != 3607 || qcvm->progs->numglobals != 9635)
		return false;
	return SV_VRMeleeFunctionPin(399, "W_SetCurrentAmmo", 15623, 7368, 1, 1,
		scalar) &&
		SV_VRMeleeFunctionPin(404, "W_Attack", 15927, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(346, "W_FireSword", 12963, 7000, 17, 0, NULL) &&
		SV_VRMeleeFunctionPin(347, "hitsword", 13148, 7017, 7, 3,
			entity_vector_vector) &&
		SV_VRMeleeFunctionPin(551, "player_sword1", 22098, 7491, 3, 0, NULL) &&
		SV_VRMeleeFunctionPin(554, "player_sword4", 22145, 0, 0, 0, NULL);
}

static qboolean SV_VRMeleeBonkProgs(void)
{
	static const byte scalar[] = {1};
	static const byte hammer_args[] = {1, 3, 3, 1, 1};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->crc != SV_VR_MELEE_BONK_CRC ||
		qcvm->progs->numstatements != 49492 ||
		qcvm->progs->numfunctions != 3306 || qcvm->progs->numglobals != 7652)
		return false;
	return SV_VRMeleeFunctionPin(412, "W_ResetWeaponState", 10722, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(409, "SuperDamageSound", 10635, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(420, "W_Attack", 11111, 6345, 1, 0, NULL) &&
		SV_VRMeleeFunctionPin(426, "W_WeaponFrame", 11363, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(460, "Hammer_Whiff_Sound", 12613, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(470, "W_SwingHammer", 13514, 6513, 19, 1,
			scalar) &&
		SV_VRMeleeFunctionPin(471, "hithammer", 13746, 6532, 13, 5,
			hammer_args) &&
		SV_VRMeleeFunctionPin(474, "saf", 13998, 6547, 1, 1, scalar);
}

/* Dwell's axe leaf reaches the native trace through its pinned traceline2
 * helper.  The complete pin set deliberately includes the harmless idle
 * states and all retained prelude calls, so a matching model alone cannot
 * enable this adapter on a nearby Copper VM. */
static qboolean SV_VRMeleeDwellProgs(void)
{
	static const byte scalar[] = {1};
	static const byte trace_args[] = {3, 3, 1, 1};

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->crc != SV_VR_MELEE_DWELL_CRC ||
		qcvm->progs->numstatements != 57592 ||
		qcvm->progs->numfunctions != 4068 || qcvm->progs->numglobals != 10026)
		return false;
	return SV_VRMeleeFunctionPin(129, "has_haste", 4038, 7022, 1, 1,
		scalar) &&
		SV_VRMeleeFunctionPin(396, "traceline2", 12883, 7674, 24, 4,
			trace_args) &&
		SV_VRMeleeFunctionPin(416, "SuperDamageSound", 13513, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(417, "BerserkSound", 13535, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(432, "W_AxeWhiffSound", 14468, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(438, "W_FireAxe", 14560, 7805, 9, 0, NULL) &&
		SV_VRMeleeFunctionPin(549, "player_stand1", 20531, 0, 0, 0,
			NULL) &&
		SV_VRMeleeFunctionPin(550, "player_run", 20567, 0, 0, 0, NULL);
}

static qboolean SV_VRMeleeHoneyProgs(void)
{
	static const byte entity_args[] = {1, 1};
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || qcvm->crc != 25769 ||
		qcvm->progs->numstatements != 25728 ||
		qcvm->progs->numfunctions != 2252 || qcvm->progs->numglobals != 4776)
		return false;
	return SV_VRMeleeFunctionPin(218, "W_FireAxe", 4664, 4136, 12, 0, NULL) &&
		SV_VRMeleeFunctionPin(146, "infront", 1804, 4049, 5, 1, entity_args) &&
		SV_VRMeleeFunctionPin(168, "Killed", 2436, 4088, 3, 2, entity_args) &&
		SV_VRMeleeFunctionPin(262, "SuperDamageSound", 6346, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(253, "W_Attack", 5876, 4257, 1, 0, NULL) &&
		SV_VRMeleeFunctionPin(261, "W_WeaponFrame", 6336, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(318, "player_stand1", 9029, 0, 0, 0, NULL) &&
		SV_VRMeleeFunctionPin(319, "player_run", 9067, 0, 0, 0, NULL);
}

/* Cache only the scalar family key.  It is invalidated by every VM identity
 * component and deliberately never retains a dfunction pointer across reload. */
static int SV_VRMeleeFamily(void)
{
	static qcvm_t *cached_vm;
	static dprograms_t *cached_progs;
	static int cached_crc, cached_statements, cached_functions, cached_globals;
	static int cached_family = -1;
	int family;

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs)
		return SV_VR_MELEE_FAMILY_NONE;
	if (cached_family >= 0 && cached_vm == qcvm && cached_progs == qcvm->progs &&
		cached_crc == qcvm->crc &&
		cached_statements == qcvm->progs->numstatements &&
		cached_functions == qcvm->progs->numfunctions &&
		cached_globals == qcvm->progs->numglobals)
		return cached_family;
	if (SV_VRMeleeStockProgs())
		family = SV_VR_MELEE_FAMILY_STOCK;
	else if (SV_VRMeleeADDescriptor())
		family = SV_VR_MELEE_FAMILY_AD;
	else if (SV_VRMeleeCopperDescriptor())
		family = SV_VR_MELEE_FAMILY_COPPER;
	else if (SV_VRMeleeALKDescriptor())
		family = SV_VR_MELEE_FAMILY_ALK;
	else if (SV_VRMeleeImmortalDescriptor())
		family = SV_VR_MELEE_FAMILY_IMMORTAL;
	else if (SV_VRMeleeDrakeDescriptor())
		family = SV_VR_MELEE_FAMILY_DRAKE;
	else if (SV_VRMeleeMjolnirDescriptor())
		family = SV_VR_MELEE_FAMILY_MJOLNIR;
	else if (SV_VRMeleeQBJ3Progs())
		family = SV_VR_MELEE_FAMILY_QBJ3;
	else if (SV_VRMeleeEnyoProgs())
		family = SV_VR_MELEE_FAMILY_ENYO;
	else if (SV_VRMeleeBonkProgs())
		family = SV_VR_MELEE_FAMILY_BONK;
	else if (SV_VRMeleeDwellProgs())
		family = SV_VR_MELEE_FAMILY_DWELL;
	else if (SV_VRMeleeHoneyProgs())
		family = SV_VR_MELEE_FAMILY_HONEY;
	else
		family = SV_VR_MELEE_FAMILY_NONE;
	cached_vm = qcvm;
	cached_progs = qcvm->progs;
	cached_crc = qcvm->crc;
	cached_statements = qcvm->progs->numstatements;
	cached_functions = qcvm->progs->numfunctions;
	cached_globals = qcvm->progs->numglobals;
	cached_family = family;
	return family;
}

static qboolean SV_VRMeleeQBJ3BerserkActive(edict_t *player)
{
	eval_t *items_qbj = GetEdictFieldValueByName(player, "items_qbj");
	eval_t *finished = GetEdictFieldValueByName(player, "berserk_finished");
	int bits;

	if (!items_qbj || !finished || !isfinite(items_qbj->_float) ||
		!isfinite(finished->_float))
		return false;
	bits = (int)items_qbj->_float;
	/* Compare at QuakeC precision, just like its `finished > time` test.
	 * A timer written this tick must not become future solely by float rounding. */
	return (bits & 4) != 0 || finished->_float > (float)qcvm->time;
}

static qboolean SV_VRMeleeBonkModel(edict_t *player)
{
	static const char *const skins[] = {
		"default", "default_bloody", "alkaline_axe", "baseball",
		"moving_past_it", "mailbox", "heavy_rocket", "brown_brick", "sblade",
		"burger", "buster_sword", "guitar", "pickaxe", "dwarven", "jester_mallet",
		"error", "sailor_sceptre", "floyd", "kebby_gears", "squeaky", "sentinel",
		"katana", "pirate_skull", "default_gold", "default_gold_bloody",
		"blocky_axe", "mace", "stop_sign", "copper_axe"
	};
	float choice;
	const char *skin = "error";
	char model[MAX_QPATH];

	if (SV_VR_MELEE_BONK_HAMMER_SKIN_GLOBAL >= qcvm->progs->numglobals ||
		!isfinite(qcvm->globals[SV_VR_MELEE_BONK_HAMMER_SKIN_GLOBAL]))
		return false;
	choice = qcvm->globals[SV_VR_MELEE_BONK_HAMMER_SKIN_GLOBAL];
	if (choice >= 1 && choice <= countof(skins) && choice == floorf(choice))
		skin = skins[(int)choice - 1];
	/* Every cosmetic uses the same QC charge/outcome. The client independently
	 * requires its exact mesh recipe; do not mistake a cosmetic for a different
	 * gameplay weapon or let a stale model borrow the currently selected skin. */
	q_snprintf(model, sizeof(model), "progs/v_hammer_%s.mdl", skin);
	return !strcmp(PR_GetString(player->v.weaponmodel), model);
}

static qboolean SV_VRMeleeBonkTier(float tier)
{
	return isfinite(tier) && (tier == .2f || tier == 2.f || tier == 4.f);
}

static qboolean SV_VRMeleeDwellBlocked(edict_t *player)
{
	eval_t *customflags = GetEdictFieldValueByName(player, "customflags");

	return !customflags || !isfinite(customflags->_float) ||
		((int)customflags->_float & 2112) != 0;
}

/* The direct leaf's range branch is intentionally stricter than has_berserk:
 * selection is the active split model plus a future expiry.  The leaf still
 * calls its own has_berserk helper for damage/effects, preserving that wider
 * item-bit-or-expiry rule. */
static qboolean SV_VRMeleeDwellBerserkSelected(edict_t *player)
{
	eval_t *finished = GetEdictFieldValueByName(player, "berserk_finished");

	return finished && isfinite(finished->_float) &&
		finished->_float > qcvm->time;
}

static sv_vr_melee_subtype_t SV_VRMeleeWeapon(edict_t *player)
{
	const sv_vr_melee_stock_descriptor_t *stock;
	int family = SV_VRMeleeFamily();

	if (!player || player->free)
		return SV_VR_MELEE_NONE;
	if (family == SV_VR_MELEE_FAMILY_STOCK) {
		if (SV_VRMeleeHammerSelected(player))
			return SV_VR_MELEE_HAMMER;
		stock = SV_VRMeleeStockDescriptor();
		return stock && player->v.weapon == stock->weapon &&
			!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_axe.mdl") ?
			SV_VR_MELEE_STOCK_AXE : SV_VR_MELEE_NONE;
	}
	/* Drake's authoritative selector is .war; its wire .weapon is 1 for axe. */
	if (family == SV_VR_MELEE_FAMILY_DRAKE)
		return SV_VRMeleeDrakeHammerSelected(player) ? SV_VR_MELEE_DRAKE_HAMMER :
			SV_VRMeleeDrakeSelected(player) ? SV_VR_MELEE_DRAKE_AXE : SV_VR_MELEE_NONE;
	if (family == SV_VR_MELEE_FAMILY_MJOLNIR)
		return SV_VRMeleeGungnirSelected(player) ? SV_VR_MELEE_MJOLNIR_GUNGNIR :
			SV_VRMeleeScimitarSelected(player) ? SV_VR_MELEE_MJOLNIR_SCIMITAR :
			SV_VRMeleeMaceSelected(player) ? SV_VR_MELEE_MJOLNIR_MACE :
			SV_VRMeleeRapierSelected(player) ? SV_VR_MELEE_MJOLNIR_RAPIER :
			SV_VRMeleeMjolnirHammerSelected(player) ? SV_VR_MELEE_MJOLNIR_HAMMER :
			SV_VRMeleeMjolnirAxeVariant(player) != SV_VR_MJOLNIR_AXE_NONE ?
			SV_VR_MELEE_MJOLNIR_AXE : SV_VR_MELEE_NONE;
	if (player->v.weapon != IT_AXE)
		return SV_VR_MELEE_NONE;
	switch (family) {
	case SV_VR_MELEE_FAMILY_IMMORTAL:
		switch (SV_VRMeleeImmortalSelected(player)) {
		case SV_VR_IMMORTAL_AXE: return SV_VR_MELEE_IMMORTAL_AXE;
		case SV_VR_IMMORTAL_HAMMER: return SV_VR_MELEE_IMMORTAL_HAMMER;
		default: return SV_VR_MELEE_NONE;
		}
	case SV_VR_MELEE_FAMILY_ALK:
		return SV_VRMeleeALKSelected(player) ? SV_VR_MELEE_ALK_AXE : SV_VR_MELEE_NONE;
	case SV_VR_MELEE_FAMILY_COPPER:
		return SV_VRMeleeCopperSelected(player) ? SV_VR_MELEE_COPPER_AXE : SV_VR_MELEE_NONE;
	case SV_VR_MELEE_FAMILY_AD:
		return SV_VRMeleeADSelected(player) ? SV_VR_MELEE_AD_AXE : SV_VR_MELEE_NONE;
	case SV_VR_MELEE_FAMILY_HONEY:
		return !strcmp(PR_GetString(player->v.weaponmodel), "progs/v_axe.mdl") ?
			SV_VR_MELEE_HONEY_AXE : SV_VR_MELEE_NONE;
	case SV_VR_MELEE_FAMILY_QBJ3:
		if (!GetEdictFieldValueByName(player, "items_qbj") ||
			!GetEdictFieldValueByName(player, "berserk_finished"))
			return SV_VR_MELEE_NONE;
		if (!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_berserk.mdl") &&
			SV_VRMeleeQBJ3BerserkActive(player))
			return SV_VR_MELEE_QBJ3_BERSERK;
		if (!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_wrench.mdl") &&
			!SV_VRMeleeQBJ3BerserkActive(player))
			return SV_VR_MELEE_QBJ3_WRENCH;
		return SV_VR_MELEE_NONE;
	case SV_VR_MELEE_FAMILY_ENYO:
		return !strcmp(PR_GetString(player->v.weaponmodel), "progs/ee_v_sword.mdl") ?
			SV_VR_MELEE_ENYO_KATANA : SV_VR_MELEE_NONE;
	case SV_VR_MELEE_FAMILY_BONK:
	{
		eval_t *customflags = GetEdictFieldValueByName(player, "customflags");
		return isfinite(player->v.items) && ((int)player->v.items & IT_AXE) &&
			customflags && isfinite(customflags->_float) &&
			!((int)customflags->_float & 2112) && SV_VRMeleeBonkModel(player) ?
			SV_VR_MELEE_BONK_HAMMER : SV_VR_MELEE_NONE;
	}
	case SV_VR_MELEE_FAMILY_DWELL:
		if (SV_VRMeleeDwellBlocked(player))
			return SV_VR_MELEE_NONE;
		if (!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_axeb.mdl") &&
			SV_VRMeleeDwellBerserkSelected(player))
			return SV_VR_MELEE_DWELL_BERSERK;
		if (!strcmp(PR_GetString(player->v.weaponmodel), "progs/v_axe2.mdl") &&
			!SV_VRMeleeDwellBerserkSelected(player))
			return SV_VR_MELEE_DWELL_AXE;
		return SV_VR_MELEE_NONE;
	default:
		return SV_VR_MELEE_NONE;
	}
}

/* Exact harmless idle/recovery thinks, never pending draw or attack work.
 * The original cooldown still applies; visual aftermath must not add another
 * cooldown, and is not cancelled when accepting a physical strike. */
static qboolean SV_VRMeleeLocomotionThink(edict_t *player)
{
	const sv_vr_melee_stock_descriptor_t *stock;
	int think, family;

	family = SV_VRMeleeFamily();
	if (family == SV_VR_MELEE_FAMILY_MJOLNIR && player)
		return SV_VRMeleeGungnirIdle(player) || SV_VRMeleeScimitarIdle(player) ||
			SV_VRMeleeRapierIdle(player) ||
			SV_VRMeleeMaceIdle(player) || SV_VRMeleeMjolnirHammerIdle(player) ||
			SV_VRMeleeMjolnirAxeReady(player);
	if (family == SV_VR_MELEE_FAMILY_DRAKE && player)
		return SV_VRMeleeDrakeHammerReady(player) || SV_VRMeleeDrakeReady(player);
	if (family == SV_VR_MELEE_FAMILY_IMMORTAL && player)
		return SV_VRMeleeImmortalIdle(player);
	if (family == SV_VR_MELEE_FAMILY_ALK && player)
		return SV_VRMeleeALKIdle(player);
	if (family == SV_VR_MELEE_FAMILY_AD && player)
		return SV_VRMeleeADReady(player);
	if (family == SV_VR_MELEE_FAMILY_COPPER && player)
		return SV_VRMeleeCopperReady(player);
	if ((family != SV_VR_MELEE_FAMILY_STOCK &&
		family != SV_VR_MELEE_FAMILY_QBJ3 &&
		family != SV_VR_MELEE_FAMILY_ENYO &&
		family != SV_VR_MELEE_FAMILY_BONK &&
		family != SV_VR_MELEE_FAMILY_DWELL &&
		family != SV_VR_MELEE_FAMILY_HONEY) || !player)
		return false;
	think = player->v.think;
	if (family == SV_VR_MELEE_FAMILY_HONEY)
		return think == 318 || think == 319;
	if (family == SV_VR_MELEE_FAMILY_QBJ3) {
		if (SV_VRMeleeQBJ3IdleThink(think))
			return true;
		/* The native draw loop deliberately leaves .think installed after
		 * its frame>=10 early return. Unlike a due attack, calling this
		 * terminal draw again cannot strike or schedule another callback. */
		if (isfinite(player->v.weaponframe) && player->v.weaponframe >= 10 &&
			SV_VRMeleeQBJ3DrawThink(think))
			return true;
		return false;
	}
	if (family == SV_VR_MELEE_FAMILY_STOCK) {
		stock = SV_VRMeleeStockDescriptor();
		return stock && (think == stock->stand_index || think == stock->run_index);
	}
	if (family == SV_VR_MELEE_FAMILY_BONK) {
		if (think >= 553 && think <= 568) {
			char name[32];
			int first = think < 561 ? 553 : 561;
			q_snprintf(name, sizeof(name), "p_hammer_%s_%02d",
				think < 561 ? "autoanim" : "cooldown", think - first + 1);
			return SV_VRMeleeFunctionPin(think, name,
				think < 561 ? 14736 + 7 * (think - 553) :
				14792 + 6 * (think - 561), 0, 0, 0, NULL);
		}
		return (think == 667 && SV_VRMeleeFunctionPin(667, "player_stand1",
			19532, 0, 0, 0, NULL)) ||
			(think == 668 && SV_VRMeleeFunctionPin(668, "player_run",
				19569, 0, 0, 0, NULL));
	}
	if (family == SV_VR_MELEE_FAMILY_DWELL)
		return (think == 549 && SV_VRMeleeFunctionPin(549, "player_stand1",
			20531, 0, 0, 0, NULL)) ||
			(think == 550 && SV_VRMeleeFunctionPin(550, "player_run",
				20567, 0, 0, 0, NULL));
	if ((think == 506 && SV_VRMeleeFunctionPin(506, "player_stand1",
		21638, 0, 0, 0, NULL)) ||
		(think == 507 && SV_VRMeleeFunctionPin(507, "player_run",
		21699, 7490, 1, 0, NULL)))
		return true;
	/* Enyo's exact non-striking sword aftermath is player_swordhit1..5,
	 * then player_sword10..17 before player_run.  Do not admit sword1..9:
	 * those are the authored attack frames. */
	if (think >= 560 && think <= 572) {
		static const char *const names[] = {
			"player_sword10", "player_sword11", "player_sword12",
			"player_sword13", "player_sword14", "player_sword15",
			"player_sword16", "player_sword17", "player_swordhit1",
			"player_swordhit2", "player_swordhit3", "player_swordhit4",
			"player_swordhit5"
		};
		return SV_VRMeleeFunctionPin(think, names[think - 560],
			22188 + (think - 560) * 7, 0, 0, 0, NULL);
	}
	return false;
}

static qboolean SV_VRMeleeNativeTrigger(sv_vr_melee_subtype_t subtype)
{
	return subtype == SV_VR_MELEE_MJOLNIR_GUNGNIR ||
		subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR ||
		subtype == SV_VR_MELEE_MJOLNIR_MACE || subtype == SV_VR_MELEE_HAMMER ||
		subtype == SV_VR_MELEE_MJOLNIR_HAMMER || subtype == SV_VR_MELEE_DRAKE_HAMMER ||
		subtype == SV_VR_MELEE_MJOLNIR_RAPIER;
}

static qboolean SV_VRMeleeReady(edict_t *player, sv_vr_melee_subtype_t subtype,
	qboolean first_outcome)
{
	eval_t *cooldown;
	float qctime;

	if (SV_VRMeleeWeapon(player) != subtype || player->v.health <= 0 ||
		player->v.deadflag || !isfinite(qcvm->time))
		return false;
	if (subtype == SV_VR_MELEE_MJOLNIR_GUNGNIR &&
		(!first_outcome || !SV_VRMeleeGungnirIdle(player)))
		return false;
	if (subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR &&
		(!first_outcome || !SV_VRMeleeScimitarIdle(player)))
		return false;
	if (subtype == SV_VR_MELEE_MJOLNIR_MACE &&
		(!first_outcome || !SV_VRMeleeMaceIdle(player)))
		return false;
	if (subtype == SV_VR_MELEE_HAMMER &&
		(!first_outcome || !SV_VRMeleeHammerReady(player)))
		return false;
	if (subtype == SV_VR_MELEE_MJOLNIR_HAMMER &&
		(!first_outcome || !SV_VRMeleeMjolnirHammerIdle(player)))
		return false;
	if (subtype == SV_VR_MELEE_DRAKE_HAMMER &&
		(!first_outcome || !SV_VRMeleeDrakeHammerReady(player)))
		return false;
	if (subtype == SV_VR_MELEE_MJOLNIR_RAPIER &&
		(!first_outcome || !SV_VRMeleeRapierIdle(player)))
		return false;
	if (!first_outcome)
		return true; /* follow-ups only revalidate the live subtype */
	/* QC timing fields are floats while qcvm->time is engine precision.  Compare
	 * at the VM precision so a value written from this exact tick cannot become
	 * spuriously future by a double/float round-up. */
	qctime = (float)qcvm->time;
	if (subtype == SV_VR_MELEE_MJOLNIR_AXE && !SV_VRMeleeMjolnirAxeReady(player))
		return false;
	if (subtype == SV_VR_MELEE_DRAKE_AXE && !SV_VRMeleeDrakeReady(player))
		return false;
	if ((subtype == SV_VR_MELEE_IMMORTAL_AXE ||
		subtype == SV_VR_MELEE_IMMORTAL_HAMMER) && !SV_VRMeleeImmortalIdle(player))
		return false;
	if (subtype == SV_VR_MELEE_ALK_AXE && !SV_VRMeleeALKIdle(player))
		return false;
	if (subtype == SV_VR_MELEE_AD_AXE && !SV_VRMeleeADReady(player))
		return false;
	if (subtype == SV_VR_MELEE_COPPER_AXE && !SV_VRMeleeCopperReady(player))
		return false;
	if (subtype == SV_VR_MELEE_BONK_HAMMER && player->v.think &&
		!SV_VRMeleeLocomotionThink(player))
		return false;
	/* Dwell's axe scheduler can remain due after an authored input.  It may
	 * still call W_FireAxe, so reject every non-locomotion state regardless of
	 * nextthink rather than racing it or cancelling it. */
	if ((subtype == SV_VR_MELEE_HONEY_AXE ||
		subtype == SV_VR_MELEE_DWELL_AXE ||
		subtype == SV_VR_MELEE_DWELL_BERSERK) &&
		!SV_VRMeleeLocomotionThink(player))
		return false;
	if (!isfinite(player->v.nextthink) ||
		/* Positive nextthink includes overdue callbacks which SV_RunThink
		 * will still execute. Zero/negative means unscheduled: ordinary QC
		 * may leave .think installed after completing an attack or draw. */
		(player->v.think && player->v.nextthink > 0 &&
			!SV_VRMeleeLocomotionThink(player)))
		return false;
	cooldown = GetEdictFieldValueByName(player,
		subtype == SV_VR_MELEE_BONK_HAMMER ? "attack_finished_hammer" :
		"attack_finished");
	return cooldown && isfinite(cooldown->_float) && cooldown->_float <= qctime;
}

typedef struct {
	globalvars_t globals;
	qcvm_t *vm;
	dprograms_t *progs;
	int argc;
	float call_globals[OFS_PARM7 + 3 - OFS_RETURN];
	vec3_t angles;
} sv_vr_melee_qc_context_t;

static qboolean SV_VRMeleeContextBegin(sv_vr_melee_qc_context_t *saved,
	edict_t *player, const vec3_t angles, int argc)
{
	if (!SV_VRMeleeFiniteVector(angles) || !qcvm || !qcvm->progs)
		return false;
	saved->globals = *pr_global_struct;
	saved->vm = qcvm;
	saved->progs = qcvm->progs;
	saved->argc = qcvm->argc;
	memcpy(saved->call_globals, qcvm->globals + OFS_RETURN,
		sizeof(saved->call_globals));
	VectorCopy(player->v.v_angle, saved->angles);
	VectorCopy(angles, player->v.v_angle);
	qcvm->argc = argc;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	AngleVectors(player->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	return true;
}

static void SV_VRMeleeContextEnd(const sv_vr_melee_qc_context_t *saved,
	edict_t *player)
{
	if (qcvm != saved->vm || qcvm->progs != saved->progs)
		return;
	qcvm->argc = saved->argc;
	memcpy(qcvm->globals + OFS_RETURN, saved->call_globals,
		sizeof(saved->call_globals));
	/* Borrowed context only.  Damage, player movement, animation and think
	 * consequences produced by QC intentionally remain live. */
	if (!player->free)
		VectorCopy(saved->angles, player->v.v_angle);
	pr_global_struct->self = saved->globals.self;
	pr_global_struct->other = saved->globals.other;
	pr_global_struct->time = saved->globals.time;
	VectorCopy(saved->globals.v_forward, pr_global_struct->v_forward);
	VectorCopy(saved->globals.v_right, pr_global_struct->v_right);
	VectorCopy(saved->globals.v_up, pr_global_struct->v_up);
	pr_global_struct->trace_allsolid = saved->globals.trace_allsolid;
	pr_global_struct->trace_startsolid = saved->globals.trace_startsolid;
	pr_global_struct->trace_fraction = saved->globals.trace_fraction;
	pr_global_struct->trace_inwater = saved->globals.trace_inwater;
	pr_global_struct->trace_inopen = saved->globals.trace_inopen;
	pr_global_struct->trace_plane_dist = saved->globals.trace_plane_dist;
	pr_global_struct->trace_ent = saved->globals.trace_ent;
	VectorCopy(saved->globals.trace_endpos, pr_global_struct->trace_endpos);
	VectorCopy(saved->globals.trace_plane_normal, pr_global_struct->trace_plane_normal);
}

/* Bonk's tracer invokes saf(.4) after its root/leaf result.  Re-establish
 * player self because the retained QC outcome is allowed to change globals. */
static qboolean SV_VRMeleeBonkRecover(const sv_vr_melee_qc_context_t *saved,
	edict_t *player)
{
	if (qcvm != saved->vm || qcvm->progs != saved->progs || player->free)
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->time = qcvm->time;
	G_FLOAT(OFS_PARM0) = .4f;
	qcvm->argc = 1;
	PR_ExecuteProgram(&qcvm->functions[474] - qcvm->functions);
	return qcvm == saved->vm && qcvm->progs == saved->progs;
}

static void SV_VRMeleeTraceGlobals(const trace_t *contact)
{
	edict_t *hit = contact->ent ? contact->ent : (edict_t *)qcvm->edicts;

	pr_global_struct->trace_allsolid = contact->allsolid;
	pr_global_struct->trace_startsolid = contact->startsolid;
	pr_global_struct->trace_fraction = contact->fraction;
	pr_global_struct->trace_inwater = contact->inwater;
	pr_global_struct->trace_inopen = contact->inopen;
	pr_global_struct->trace_plane_dist = contact->plane.dist;
	pr_global_struct->trace_ent = EDICT_TO_PROG(hit);
	VectorCopy(contact->endpos, pr_global_struct->trace_endpos);
	VectorCopy(contact->plane.normal, pr_global_struct->trace_plane_normal);
}

/* Called by SV_VRContactTrace before its normal root-function gate.  Dwell's
 * W_FireAxe reaches traceline through #396, and the helper can retry after it
 * filters the acquired entity.  Match the immediate saved #438 caller, not
 * merely the helper, so later CanDamage/gameplay traces stay native.  The
 * existing scoped force_miss bit is a one-call phase: physical acquisition
 * first, then clean helper retries (or an initial whiff). */
static qboolean SV_VRMeleeDwellTrace(edict_t *ent, const vec3_t start,
	const vec3_t end, int nomonsters, trace_t *trace)
{
	dfunction_t *root, *helper;
	prstack_t *caller;
	int callsite;

	if (!trace || !sv_vr_contact_call.active || !sv_vr_contact_call.player ||
		!SV_VRMeleeDwellProgs() || !SV_VRMeleeFiniteVector(start) ||
		!SV_VRMeleeFiniteVector(end) || nomonsters)
		return false;
	root = &qcvm->functions[438];
	helper = &qcvm->functions[396];
	if (sv_vr_contact_call.function != root || qcvm->xfunction != helper ||
		qcvm->xstatement != 12923 || qcvm->depth <= 0 ||
		pr_global_struct->self != EDICT_TO_PROG(sv_vr_contact_call.player))
		return false;
	caller = &qcvm->stack[qcvm->depth - 1];
	callsite = caller->s;
	if (caller->f != root || (callsite != 14578 && callsite != 14586))
		return false;
	if ((callsite == 14578 && SV_VRMeleeWeapon(sv_vr_contact_call.player) !=
		SV_VR_MELEE_DWELL_BERSERK) ||
		(callsite == 14586 && SV_VRMeleeWeapon(sv_vr_contact_call.player) !=
		SV_VR_MELEE_DWELL_AXE))
		return false;
	/* The original first helper call uses ignore=self.  Its retry deliberately
	 * changes ignore to the filtered entity, so do not apply this gate again. */
	if (!sv_vr_contact_call.force_miss && ent != sv_vr_contact_call.player)
		return false;
	if (!sv_vr_contact_call.force_miss) {
		*trace = sv_vr_contact_call.trace;
		sv_vr_contact_call.force_miss = true;
		return true;
	}
	memset(trace, 0, sizeof(*trace));
	trace->fraction = 1;
	trace->inopen = true;
	trace->ent = qcvm->edicts;
	VectorCopy(end, trace->endpos);
	return true;
}

static void SV_VRMeleeLeafArgs(edict_t *target, const vec3_t org, const vec3_t dir)
{
	G_INT(OFS_PARM0) = EDICT_TO_PROG(target);
	VectorCopy(org, G_VECTOR(OFS_PARM1));
	VectorCopy(dir, G_VECTOR(OFS_PARM2));
}

static qboolean SV_VRMeleeDwellHaste(edict_t *player, qboolean *haste)
{
	if (!haste || !SV_VRMeleeDwellProgs() || player->free)
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	G_INT(OFS_PARM0) = EDICT_TO_PROG(player);
	qcvm->argc = 1;
	PR_ExecuteProgram(&qcvm->functions[129] - qcvm->functions);
	if (!isfinite(G_FLOAT(OFS_RETURN)))
		return false;
	*haste = G_FLOAT(OFS_RETURN) != 0;
	return true;
}

/* Retain the non-scheduling portions of W_WeaponFrame/W_Attack/W_AxeSwing in
 * their original order.  W_AxeSwing itself must never be called here: it
 * installs the delayed axe think and would create a second authored strike. */
static qboolean SV_VRMeleeDwellPrelude(edict_t *player, eval_t *cooldown)
{
	qboolean haste;

	if (!cooldown || !isfinite(cooldown->_float) || !SV_VRMeleeDwellProgs())
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[416] - qcvm->functions);
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || player->free)
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[417] - qcvm->functions);
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || player->free)
		return false;
	if (!SV_VRMeleeDwellHaste(player, &haste))
		return false;
	pr_global_struct->self = EDICT_TO_PROG(player);
	pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram(&qcvm->functions[432] - qcvm->functions);
	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs || player->free)
		return false;
	cooldown->_float = qcvm->time + .49f * (haste ? .6f : 1.f);
	return true;
}

static qboolean SV_VRMeleeDwellFire(edict_t *player,
	sv_vr_contact_outcome_t outcome, const trace_t *contact, qboolean *ff)
{
	dfunction_t *leaf;

	if (!ff || (outcome == VR_CONTACT_HIT && !contact) ||
		!SV_VRMeleeDwellProgs())
		return false;
	leaf = &qcvm->functions[438];
	sv_vr_contact_call.player = player;
	sv_vr_contact_call.function = leaf;
	if (contact)
		sv_vr_contact_call.trace = *contact;
	else
		memset(&sv_vr_contact_call.trace, 0, sizeof(sv_vr_contact_call.trace));
	sv_vr_contact_call.active = true;
	/* A whiff enters the same qualified helper path, but its very first native
	 * acquisition is already a clean miss.  Parry never invokes this root. */
	sv_vr_contact_call.force_miss = outcome == VR_CONTACT_WHIFF;
	*ff = SV_FriendlyFireBegin(player);
	qcvm->argc = 0;
	PR_ExecuteProgram(leaf - qcvm->functions);
	sv_vr_contact_call.active = false;
	sv_vr_contact_call.force_miss = false;
	return qcvm && qcvm == &sv.qcvm && qcvm->progs && !player->free;
}

/* Return a successful original recovery deadline for an accepted first
 * outcome.  VR_CONTACT_WHIFF and VR_CONTACT_PARRIED deliberately retain only
 * the safe prelude; neither may be inferred from a NULL contact. */
static qboolean SV_VRMeleeOutcome(edict_t *player, sv_vr_melee_subtype_t subtype,
	sv_vr_contact_outcome_t outcome, const trace_t *contact,
	const vec3_t accepted_hand_angles, float tier,
	int anatomical_hand, qboolean first_outcome, float *recovery_deadline)
{
	sv_vr_melee_qc_context_t saved;
	eval_t *cooldown, *switchblock, *hostile, *berserk_sound;
	dfunction_t *leaf, *quad;
	vec3_t forward, right, up, org, dir;
	float recovery;
	qboolean ff = false;
	vec3_t muzzle;

	if (recovery_deadline)
		*recovery_deadline = 0;
	if (!SV_VRMeleeReady(player, subtype, first_outcome) ||
		(outcome != VR_CONTACT_HIT && outcome != VR_CONTACT_WHIFF &&
			outcome != VR_CONTACT_PARRIED) ||
		(outcome == VR_CONTACT_HIT && !contact) ||
		!SV_VRMeleeFiniteVector(accepted_hand_angles) || !isfinite(tier) ||
		(subtype == SV_VR_MELEE_BONK_HAMMER && !SV_VRMeleeBonkTier(tier)) ||
		(anatomical_hand != 0 && anatomical_hand != 1) ||
		(contact && (!contact->ent || contact->ent->free ||
			!isfinite(contact->fraction) || !SV_VRMeleeFiniteVector(contact->endpos) ||
			contact->fraction < 0 || contact->fraction >= 1)))
		return false;
	if (!SV_VRMeleeContextBegin(&saved, player, accepted_hand_angles, 0))
		return false;
	VectorCopy(sv_vr_contact_call.muzzle, muzzle);
	if ((subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR &&
		GetEdictFieldValueByName(player, "tome_finished")->_float != 0) ||
		(subtype == SV_VR_MELEE_MJOLNIR_RAPIER && outcome != VR_CONTACT_PARRIED)) {
		if (!sv_vr_contact_call.has_muzzle ||
			!SV_VRMeleeFiniteVector(sv_vr_contact_call.muzzle)) {
			SV_VRMeleeContextEnd(&saved, player);
			return false;
		}
	}
	VectorCopy(pr_global_struct->v_forward, forward);
	VectorCopy(pr_global_struct->v_right, right);
	VectorCopy(pr_global_struct->v_up, up);
	cooldown = GetEdictFieldValueByName(player,
		subtype == SV_VR_MELEE_BONK_HAMMER ? "attack_finished_hammer" :
		"attack_finished");
	hostile = GetEdictFieldValueByName(player, "show_hostile");
	if (!cooldown || !hostile) {
		SV_VRMeleeContextEnd(&saved, player);
		return false;
	}
	recovery = subtype == SV_VR_MELEE_QBJ3_WRENCH ? .8f :
		subtype == SV_VR_MELEE_ENYO_KATANA ? .4f : .5f;
	if (first_outcome) {
		if (subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR ||
			subtype == SV_VR_MELEE_MJOLNIR_RAPIER ||
			subtype == SV_VR_MELEE_MJOLNIR_HAMMER) {
			/* These base-bank weapons call this before their original root;
			 * Mace/Gungnir's hack-bank entries deliberately do not. */
			qcvm->argc = 0;
			PR_ExecuteProgram(2674);
		}
		if (subtype == SV_VR_MELEE_ENYO_KATANA) {
			switchblock = GetEdictFieldValueByName(player, "switchblock_finished");
			if (!switchblock || !isfinite(switchblock->_float)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_QBJ3_BERSERK) {
			berserk_sound = GetEdictFieldValueByName(player, "berserk_sound");
			if (!berserk_sound || !isfinite(berserk_sound->_float)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		}
		if (subtype == SV_VR_MELEE_MJOLNIR_RAPIER) {
			if (outcome == VR_CONTACT_PARRIED) {
				G_FLOAT(OFS_PARM0) = 1;
				qcvm->argc = 1;
				PR_ExecuteProgram(2673);
				qcvm->argc = 0;
				PR_ExecuteProgram(SV_VRMeleeRapierFirstSwipeFunction() - qcvm->functions);
			} else {
				vec3_t body, source;
				/* This root only reloads, sounds and creates its ghost. Borrow
				 * origin for that call alone; no body motion or new setorigin
				 * hook. The accepted command already carries the user's muzzle. */
				VectorCopy(player->v.origin, body);
				SV_ClampVRMuzzleToWorld(player, muzzle);
				VectorScale(forward, 15, source);
				source[2] += 25;
				VectorSubtract(muzzle, source, player->v.origin);
				sv_vr_contact_call.player = player;
				sv_vr_contact_call.function = SV_VRMeleeRapierRootFunction();
				sv_vr_contact_call.depth = qcvm->depth + 1;
				sv_vr_contact_call.active = true;
				qcvm->argc = 0;
				PR_ExecuteProgram(sv_vr_contact_call.function - qcvm->functions);
				sv_vr_contact_call.active = false;
				VectorCopy(body, player->v.origin);
			}
		} else if (subtype == SV_VR_MELEE_DRAKE_HAMMER) {
			if (!SV_VRMeleeDrakeHammerPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_HAMMER) {
			if (!SV_VRMeleeHammerPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_MJOLNIR_HAMMER) {
			SV_VRMeleeMjolnirHammerReload();
			if (outcome == VR_CONTACT_WHIFF) {
				/* Retain the native miss recovery/sound, but throwing is a
				 * deliberate trigger action, not an accidental physical miss. */
				cooldown->_float += .1f;
				SV_StartSound(player, 1, "knight/sword1.wav", 255, 1);
			}
		} else if (subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR ||
			subtype == SV_VR_MELEE_MJOLNIR_MACE) {
			/* Keep native movement and reload; consume only the root's exact
			 * animation-entry call. No attack think is installed then cancelled. */
			sv_vr_contact_call.player = player;
			sv_vr_contact_call.function = subtype == SV_VR_MELEE_MJOLNIR_MACE ?
				SV_VRMeleeMaceRootFunction() : &qcvm->functions[2461];
			sv_vr_contact_call.depth = qcvm->depth + 1;
			sv_vr_contact_call.active = true;
			PR_ExecuteProgram(sv_vr_contact_call.function - qcvm->functions);
			sv_vr_contact_call.active = false;
		} else if (subtype == SV_VR_MELEE_MJOLNIR_GUNGNIR) {
			SV_VRMeleeGungnirReload(player);
		} else if (subtype == SV_VR_MELEE_MJOLNIR_AXE) {
			if (!SV_VRMeleeMjolnirAxePrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_DRAKE_AXE) {
			if (!SV_VRMeleeDrakePrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_IMMORTAL_AXE || subtype == SV_VR_MELEE_IMMORTAL_HAMMER) {
			if (!SV_VRMeleeImmortalPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_ALK_AXE) {
			if (!SV_VRMeleeALKPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_COPPER_AXE) {
			if (!SV_VRMeleeCopperPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_AD_AXE) {
			if (!SV_VRMeleeADPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_DWELL_AXE ||
			subtype == SV_VR_MELEE_DWELL_BERSERK) {
			if (!SV_VRMeleeDwellPrelude(player, cooldown)) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
		} else if (subtype == SV_VR_MELEE_BONK_HAMMER) {
			/* W_WeaponFrame calls SuperDamageSound before W_Attack. Retain
			 * that side effect without entering the desktop charge scheduler;
			 * release then plays the whiff sound before resolving its trace. */
			hostile->_float = qcvm->time + 1;
			PR_ExecuteProgram(&qcvm->functions[409] - qcvm->functions);
			PR_ExecuteProgram(&qcvm->functions[460] - qcvm->functions);
		} else {
			cooldown->_float = qcvm->time + recovery;
		}
		if (subtype == SV_VR_MELEE_ENYO_KATANA) {
			switchblock->_float = qcvm->time + recovery;
			SV_StartSound(player, 1, "weapons/sword1.wav", 128, 1);
		} else if (subtype == SV_VR_MELEE_QBJ3_WRENCH) {
			quad = SV_VRMeleeQBJ3SoundFunction();
			if (!quad) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
			PR_ExecuteProgram(quad - qcvm->functions);
			SV_StartSound(player, 6, "impact/wrench_swing.wav", 255, 1);
		} else if (subtype == SV_VR_MELEE_QBJ3_BERSERK) {
			SV_StartSound(player, 6, "impact/wrench_swing.wav", 255, 1);
			if (berserk_sound->_float < qcvm->time) {
				berserk_sound->_float = qcvm->time + 1;
				SV_StartSound(player, 0, "items/berserk_fire.wav", 255, 1);
			}
		} else if (subtype == SV_VR_MELEE_STOCK_AXE ||
			subtype == SV_VR_MELEE_HONEY_AXE) {
			quad = subtype == SV_VR_MELEE_HONEY_AXE ?
				&qcvm->functions[262] : SV_VRMeleeStockSoundFunction();
			if (!quad) {
				SV_VRMeleeContextEnd(&saved, player);
				return false;
			}
			hostile->_float = qcvm->time + 1;
			PR_ExecuteProgram(quad - qcvm->functions);
			if (subtype == SV_VR_MELEE_STOCK_AXE && qcvm->crc == 54028) {
				G_INT(OFS_PARM0) = EDICT_TO_PROG(player);
				qcvm->argc = 1;
				PR_ExecuteProgram(122); /* RuneApplyBlackNoise(self) */
			}
			SV_StartSound(player, 1, "weapons/ax1.wav", 255, 1);
			if (subtype == SV_VR_MELEE_STOCK_AXE && qcvm->crc == 54028) {
				G_FLOAT(OFS_PARM0) = .5f;
				G_INT(OFS_PARM1) = EDICT_TO_PROG(player);
				qcvm->argc = 2;
				PR_ExecuteProgram(124); /* RuneApplyHell(.5, self) */
				cooldown->_float = qcvm->time + G_FLOAT(OFS_RETURN);
			}
		}
	}
	if ((subtype == SV_VR_MELEE_DWELL_AXE ||
		subtype == SV_VR_MELEE_DWELL_BERSERK) &&
		outcome != VR_CONTACT_PARRIED) {
		/* W_FireAxe retains its original range, hit effects, damage and
		 * berserk branch.  Its only acquisition is narrowly substituted by
		 * SV_VRMeleeDwellTrace; no animation/cycle think is installed here. */
		if (!SV_VRMeleeDwellFire(player, outcome, contact, &ff)) {
			if (ff)
				SV_FriendlyFireEnd();
			SV_VRMeleeContextEnd(&saved, player);
			return false;
		}
		if (ff)
			SV_FriendlyFireEnd();
		if (recovery_deadline && qcvm == saved.vm && qcvm->progs == saved.progs)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return qcvm == saved.vm && qcvm->progs == saved.progs;
	}
	if (outcome == VR_CONTACT_WHIFF && subtype == SV_VR_MELEE_BONK_HAMMER) {
		/* Run W_SwingHammer(tier), then retain the tracer's exact saf(.4)
		 * recovery. Only its five audited range-acquisition rays are forced to
		 * well-formed misses by SV_VRContactTrace; floor and nested gameplay
		 * traces stay inside the original QC root. */
		leaf = &qcvm->functions[470];
		sv_vr_contact_call.player = player;
		sv_vr_contact_call.function = leaf;
		sv_vr_contact_call.active = true;
		sv_vr_contact_call.force_miss = true;
		G_FLOAT(OFS_PARM0) = tier;
		qcvm->argc = 1;
		PR_ExecuteProgram(leaf - qcvm->functions);
		sv_vr_contact_call.active = false;
		sv_vr_contact_call.force_miss = false;
		if (!SV_VRMeleeBonkRecover(&saved, player)) {
			SV_VRMeleeContextEnd(&saved, player);
			return false;
		}
		if (recovery_deadline && qcvm == saved.vm && qcvm->progs == saved.progs)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return qcvm == saved.vm && qcvm->progs == saved.progs;
	}
	if (subtype == SV_VR_MELEE_MJOLNIR_RAPIER) {
		if (outcome != VR_CONTACT_PARRIED) {
			sv_vr_contact_call.player = player;
			sv_vr_contact_call.function = SV_VRMeleeRapierAttackFunction();
			sv_vr_contact_call.depth = qcvm->depth + 1;
			sv_vr_contact_call.active = true;
			sv_vr_contact_call.force_miss = outcome == VR_CONTACT_WHIFF;
			if (contact)
				sv_vr_contact_call.trace = *contact;
			ff = SV_FriendlyFireBegin(player);
			qcvm->argc = 0;
			PR_ExecuteProgram(sv_vr_contact_call.function - qcvm->functions);
			sv_vr_contact_call.active = false;
			sv_vr_contact_call.force_miss = false;
			if (ff)
				SV_FriendlyFireEnd();
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	if (subtype == SV_VR_MELEE_DRAKE_HAMMER) {
		if (outcome == VR_CONTACT_HIT) {
			if (contact->ent->v.takedamage) {
				/* Native direct impact takes a damage-type STRING, not an
				 * attacker. Both authored ranges decap at <=32 from origin+Z16.
				 * Use actual contact distance, never the sweep's temporal fraction. */
				VectorSubtract(contact->endpos, player->v.origin, dir);
				dir[2] -= 16;
				SV_VRMeleeTraceGlobals(contact);
				VectorMA(contact->endpos, -4, forward, G_VECTOR(OFS_PARM0));
				VectorCopy(forward, G_VECTOR(OFS_PARM1));
				G_INT(OFS_PARM2) = DotProduct(dir, dir) <= 32.f * 32.f ?
					SV_VRMeleeDrakeHammerDamageTypeDecap() : SV_VRMeleeDrakeHammerDamageTypeHammer();
				qcvm->argc = 3;
				ff = SV_FriendlyFireBegin(player);
				PR_ExecuteProgram(SV_VRMeleeDrakeHammerDamageFunction() - qcvm->functions);
				if (ff)
					SV_FriendlyFireEnd();
			} else {
				/* Brush entities are impacts too. Never run blood, damage or
				 * Tome victim effects for a nondamageable contact. */
				SV_VRMeleeDrakeHammerWall(player, contact->endpos, forward);
			}
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	if (subtype == SV_VR_MELEE_HAMMER || subtype == SV_VR_MELEE_MJOLNIR_HAMMER) {
		if (outcome == VR_CONTACT_HIT) {
			leaf = subtype == SV_VR_MELEE_HAMMER ? SV_VRMeleeHammerLeafForPlayer(player) :
				&qcvm->functions[2682];
			SV_VRMeleeTraceGlobals(contact);
			/* Hip's floor lightning helper has no acquisition of its own.
			 * Supply the real accepted floor point, not a sweep's time fraction
			 * disguised as the authored downray. MG3 retains its repeat-victim
			 * branch in the original root instead of gaining a new floor attack. */
			if (subtype == SV_VR_MELEE_HAMMER &&
				SV_VRMeleeHammerFloorContact(player, contact))
				leaf = SV_VRMeleeHammerFloorFunction();
			sv_vr_contact_call.player = player;
			sv_vr_contact_call.function = leaf;
			sv_vr_contact_call.trace = *contact;
			sv_vr_contact_call.depth = qcvm->depth + 1;
			sv_vr_contact_call.force_miss = false;
			sv_vr_contact_call.active = true;
			ff = SV_FriendlyFireBegin(player);
			qcvm->argc = 0;
			PR_ExecuteProgram(leaf - qcvm->functions);
			sv_vr_contact_call.active = false;
			if (ff)
				SV_FriendlyFireEnd();
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	if (subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR) {
		if (outcome != VR_CONTACT_PARRIED) {
			sv_vr_contact_call.player = player;
			sv_vr_contact_call.function = &qcvm->functions[2462];
			sv_vr_contact_call.depth = qcvm->depth + 1;
			sv_vr_contact_call.active = true;
			sv_vr_contact_call.force_miss = outcome == VR_CONTACT_WHIFF;
			if (contact) {
				sv_vr_contact_call.trace = *contact;
				VectorCopy(contact->ent->v.origin, sv_vr_contact_call.target_origin);
				sv_vr_contact_call.target_modelindex = contact->ent->v.modelindex;
			}
			ff = SV_FriendlyFireBegin(player);
			G_FLOAT(OFS_PARM0) = 1;
			qcvm->argc = 1;
			PR_ExecuteProgram(2462);
			sv_vr_contact_call.active = false;
			sv_vr_contact_call.force_miss = false;
			if (ff)
				SV_FriendlyFireEnd();
			/* The native first-strike Tome effect also occurs on a miss.
			 * Only this projectile call borrows the accepted hand source. */
			if (!player->free && player->v.health > 0 && sv_vr_contact_call.has_muzzle &&
				GetEdictFieldValueByName(player, "tome_finished")->_float != 0) {
				vec3_t body, source;
				VectorCopy(player->v.origin, body);
				VectorCopy(accepted_hand_angles, player->v.v_angle);
				pr_global_struct->self = EDICT_TO_PROG(player);
				pr_global_struct->other = 0;
				AngleVectors(player->v.v_angle, pr_global_struct->v_forward,
					pr_global_struct->v_right, pr_global_struct->v_up);
				SV_ClampVRMuzzleToWorld(player, muzzle);
				VectorScale(pr_global_struct->v_forward, 8, source);
				VectorMA(source, 3, pr_global_struct->v_right, source);
				VectorMA(source, 20, pr_global_struct->v_up, source);
				VectorSubtract(muzzle, source, player->v.origin);
				G_FLOAT(OFS_PARM0) = 1;
				qcvm->argc = 1;
				PR_ExecuteProgram(2464);
				VectorCopy(body, player->v.origin);
				SV_StartSound(player, 1, "ad171/weapons/axe_swoosh2.wav", 255, 1);
			}
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	if (subtype == SV_VR_MELEE_MJOLNIR_MACE) {
		if (outcome == VR_CONTACT_HIT && contact->ent != player &&
			contact->ent->v.takedamage && contact->ent->v.health > 0) {
			VectorCopy(contact->endpos, G_VECTOR(OFS_PARM0));
			G_INT(OFS_PARM1) = EDICT_TO_PROG(contact->ent);
			qcvm->argc = 2;
			ff = SV_FriendlyFireBegin(player);
			PR_ExecuteProgram(SV_VRMeleeMaceHitFunction() - qcvm->functions);
			if (ff)
				SV_FriendlyFireEnd();
		} else if (outcome == VR_CONTACT_HIT) {
			SV_VRMeleeMaceWall(player, contact->endpos);
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	if (subtype == SV_VR_MELEE_MJOLNIR_GUNGNIR) {
		/* Original Primary owns underwater movement even on a whiff. A parry
		 * spends recovery without allowing either acquisition or movement. */
		if (outcome != VR_CONTACT_PARRIED) {
			sv_vr_contact_call.player = player;
			sv_vr_contact_call.function = &qcvm->functions[2654];
			sv_vr_contact_call.depth = qcvm->depth + 1;
			sv_vr_contact_call.active = true;
			sv_vr_contact_call.force_miss = outcome == VR_CONTACT_WHIFF;
			if (contact)
				sv_vr_contact_call.trace = *contact;
			ff = SV_FriendlyFireBegin(player);
			qcvm->argc = 0;
			PR_ExecuteProgram(2654);
			sv_vr_contact_call.active = false;
			sv_vr_contact_call.force_miss = false;
			if (ff)
				SV_FriendlyFireEnd();
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	if (outcome != VR_CONTACT_HIT) {
		if (subtype == SV_VR_MELEE_BONK_HAMMER && first_outcome &&
			!SV_VRMeleeBonkRecover(&saved, player)) {
			SV_VRMeleeContextEnd(&saved, player);
			return false;
		}
		if (recovery_deadline)
			*recovery_deadline = cooldown->_float;
		SV_VRMeleeContextEnd(&saved, player);
		return true;
	}
	/* Direct leaves retain the original explicit contact convention. */
	VectorCopy(contact->endpos, org);
	VectorMA(org, -4, forward, org);
	/* QBJ3's forward fan backs its sparks away along aim. A physical wrench
	 * or fist can hit sideways while aim points away from the wall, placing
	 * that same effect inside solid. Keep its native effect/damage leaf, but
	 * offset nondamageable brush impacts along the accepted surface normal. */
	if ((subtype == SV_VR_MELEE_QBJ3_WRENCH ||
		subtype == SV_VR_MELEE_QBJ3_BERSERK) &&
		!contact->ent->v.takedamage && contact->ent->v.solid == SOLID_BSP &&
		SV_VRMeleeFiniteVector(contact->plane.normal) &&
		DotProduct(contact->plane.normal, contact->plane.normal) > .5f &&
		DotProduct(contact->plane.normal, contact->plane.normal) < 1.5f)
		VectorMA(contact->endpos, 4, contact->plane.normal, org);
	VectorScale(right, 20, dir);
	VectorMA(dir, -5, up, dir);
	SV_VRMeleeTraceGlobals(contact); /* QBJ3 hitwrench reads trace_ent too. */
	if (subtype == SV_VR_MELEE_STOCK_AXE || subtype == SV_VR_MELEE_HONEY_AXE ||
		subtype == SV_VR_MELEE_AD_AXE || subtype == SV_VR_MELEE_COPPER_AXE ||
		subtype == SV_VR_MELEE_ALK_AXE || subtype == SV_VR_MELEE_IMMORTAL_AXE ||
		subtype == SV_VR_MELEE_IMMORTAL_HAMMER || subtype == SV_VR_MELEE_DRAKE_AXE ||
		subtype == SV_VR_MELEE_MJOLNIR_AXE) {
		leaf = subtype == SV_VR_MELEE_MJOLNIR_AXE ? SV_VRMeleeMjolnirAxeLeafFunction() :
			subtype == SV_VR_MELEE_DRAKE_AXE ? SV_VRMeleeDrakeLeafFunction() :
			subtype == SV_VR_MELEE_IMMORTAL_AXE ?
			SV_VRMeleeImmortalLeafFunction(SV_VR_IMMORTAL_AXE) :
			subtype == SV_VR_MELEE_IMMORTAL_HAMMER ?
			SV_VRMeleeImmortalLeafFunction(SV_VR_IMMORTAL_HAMMER) :
			subtype == SV_VR_MELEE_ALK_AXE ?
			&qcvm->functions[SV_VRMeleeALKDescriptor()->leaf] :
			subtype == SV_VR_MELEE_COPPER_AXE ? SV_VRMeleeCopperLeafFunction() :
			subtype == SV_VR_MELEE_AD_AXE ?
			&qcvm->functions[SV_VRMeleeADDescriptor()->leaf] :
			subtype == SV_VR_MELEE_HONEY_AXE ?
			&qcvm->functions[218] : SV_VRMeleeStockLeafFunction();
		if (!leaf) {
			SV_VRMeleeContextEnd(&saved, player);
			return false;
		}
		sv_vr_contact_call.player = player;
		sv_vr_contact_call.function = leaf;
		sv_vr_contact_call.trace = *contact;
		sv_vr_contact_call.active = true;
		/* Honey's downed zombie is deliberately non-solid. Its original
		 * prelude may gib it, but the ordinary tail trace cannot damage it,
		 * including when original range/facing qualification rejected it. */
		sv_vr_contact_call.force_miss = (subtype == SV_VR_MELEE_HONEY_AXE ||
			subtype == SV_VR_MELEE_AD_AXE || subtype == SV_VR_MELEE_MJOLNIR_AXE) &&
			contact->ent->v.solid == SOLID_NOT;
		ff = SV_FriendlyFireBegin(player);
		PR_ExecuteProgram(leaf - qcvm->functions);
		sv_vr_contact_call.active = false;
		sv_vr_contact_call.force_miss = false;
	} else {
		leaf = (subtype == SV_VR_MELEE_QBJ3_WRENCH ||
			subtype == SV_VR_MELEE_QBJ3_BERSERK) ?
			SV_VRMeleeQBJ3LeafFunction(subtype) :
			&qcvm->functions[subtype == SV_VR_MELEE_BONK_HAMMER ? 471 : 347];
		if (!leaf) {
			SV_VRMeleeContextEnd(&saved, player);
			return false;
		}
		if (!first_outcome || contact) /* Direct leaves need hostile on every hit. */
			hostile->_float = qcvm->time + 1;
		SV_VRMeleeLeafArgs(contact->ent, org, dir);
		if (subtype == SV_VR_MELEE_BONK_HAMMER) {
			G_FLOAT(OFS_PARM3) = tier;
			G_FLOAT(OFS_PARM4) = NUM_FOR_EDICT(contact->ent) == 0 &&
				contact->plane.normal[2] > .71f;
			qcvm->argc = 5;
		} else {
			qcvm->argc = 3;
		}
		ff = SV_FriendlyFireBegin(player);
		PR_ExecuteProgram(leaf - qcvm->functions);
	}
	if (ff)
		SV_FriendlyFireEnd();
	if (subtype == SV_VR_MELEE_BONK_HAMMER && first_outcome &&
		!SV_VRMeleeBonkRecover(&saved, player)) {
		SV_VRMeleeContextEnd(&saved, player);
		return false;
	}
	if (recovery_deadline && qcvm == saved.vm && qcvm->progs == saved.progs)
		*recovery_deadline = cooldown->_float;
	SV_VRMeleeContextEnd(&saved, player);
	return qcvm == saved.vm && qcvm->progs == saved.progs;
}

#endif /* QS_VR_MELEE_QC_H */
