/*
 * Exact identity gate for the first stock-axe physical-melee vertical.
 * Keep this narrow: a familiar function name or weapon bit is not enough to
 * authorize a server-side QC trace substitution.
 */
#ifndef QS_VR_MELEE_STOCK_QC_H
#define QS_VR_MELEE_STOCK_QC_H

typedef struct
{
	int progscrc;
	int statements, functions, globals;
	int leaf_index, leaf_statement, leaf_parm_start;
	int sound_index, sound_statement;
	int attack_index, attack_statement, attack_parm_start;
	int frame_index, frame_statement;
	int trace_statement;
	int stand_index, stand_statement;
	int run_index, run_statement, run_parm_start, run_locals;
} sv_vr_stock_axe_descriptor_t;

/* Verified against the packed progs.dat identities. Rogue is accepted only
 * when its native rune helper ABI is also intact. */
static const sv_vr_stock_axe_descriptor_t sv_vr_stock_axe_descriptors[] = {
	{3064, 21118, 2116, 4171,
		182, 3428, 3714, 227, 5009, 218, 4538, 3828, 226, 4997,
		3438, 268, 7605, 269, 7643, 0, 0},
	{48616, 35474, 2808, 5610,
		199, 4818, 4725, 256, 7035, 240, 6271, 4847, 255, 7025,
		4828, 298, 10028, 299, 10079, 4899, 2},
	{54028, 38916, 3316, 6420,
		239, 7793, 5625, 284, 10018, 275, 9046, 5739, 283, 9992,
		7803, 326, 13158, 327, 13196, 0, 0}
};

static qboolean SV_VRStockAxeFunctionPin(int index, const char *name,
	int first_statement, int parm_start, int locals, int numparms,
	const byte *parm_sizes)
{
	dfunction_t *function;
	int i;

	if (!qcvm || !qcvm->progs || index < 0 ||
		index >= qcvm->progs->numfunctions || numparms < 0 ||
		numparms > MAX_PARMS)
		return false;
	function = &qcvm->functions[index];
	if (strcmp(PR_GetString(function->s_name), name) ||
		function->first_statement != first_statement ||
		function->parm_start != parm_start || function->locals != locals ||
		function->numparms != numparms)
		return false;
	for (i = 0; i < numparms; ++i)
		if (!parm_sizes || function->parm_size[i] != parm_sizes[i])
			return false;
	return true;
}

static qboolean SV_VRStockAxeFunctionStatementPin(
	const sv_vr_stock_axe_descriptor_t *descriptor)
{
	static const byte rune_args[] = {1, 1};
	dfunction_t *leaf;
	dfunction_t *traceline;
	dstatement_t *trace_statement;
	unsigned int trace_function, function_global_index;

	if (!SV_VRStockAxeFunctionPin(descriptor->leaf_index, "W_FireAxe",
		descriptor->leaf_statement, descriptor->leaf_parm_start, 6, 0, NULL) ||
		!SV_VRStockAxeFunctionPin(descriptor->sound_index, "SuperDamageSound",
		descriptor->sound_statement, 0, 0, 0, NULL) ||
		!SV_VRStockAxeFunctionPin(descriptor->attack_index, "W_Attack",
		descriptor->attack_statement, descriptor->attack_parm_start, 1, 0, NULL) ||
		!SV_VRStockAxeFunctionPin(descriptor->frame_index, "W_WeaponFrame",
		descriptor->frame_statement, 0, 0, 0, NULL) ||
		!SV_VRStockAxeFunctionPin(descriptor->stand_index, "player_stand1",
		descriptor->stand_statement, 0, 0, 0, NULL) ||
		!SV_VRStockAxeFunctionPin(descriptor->run_index, "player_run",
		descriptor->run_statement, descriptor->run_parm_start,
		descriptor->run_locals, 0, NULL))
		return false;

	if (descriptor->progscrc == 54028 &&
		(!SV_VRStockAxeFunctionPin(122, "RuneApplyBlackNoise", 3287, 5410,
			1, 1, rune_args) ||
		 !SV_VRStockAxeFunctionPin(124, "RuneApplyHell", 3310, 5413,
			2, 2, rune_args)))
		return false;

	if (descriptor->trace_statement <= descriptor->leaf_statement ||
		descriptor->trace_statement >= qcvm->progs->numstatements)
		return false;
	leaf = &qcvm->functions[descriptor->leaf_index];
	trace_statement = &qcvm->statements[descriptor->trace_statement];
	if (trace_statement->op != OP_CALL3)
		return false;
	function_global_index = (unsigned short)trace_statement->a;
	if (function_global_index >= (unsigned int)qcvm->progs->numglobals)
		return false;
	/* QuakeC stores function references as integer bits in the globals array,
	 * not as a numeric float. Match the VM's eval_t interpretation. */
	trace_function = ((eval_t *)&qcvm->globals[function_global_index])->function;
	if (trace_function >= (unsigned int)qcvm->progs->numfunctions)
		return false;
	traceline = ED_FindFunction("traceline");
	return traceline && traceline->first_statement < 0 &&
		trace_function == (unsigned int)(traceline - qcvm->functions) &&
		descriptor->trace_statement > leaf->first_statement;
}

static const sv_vr_stock_axe_descriptor_t *SV_VRStockAxeDescriptor(void)
{
	int i;

	if (!qcvm || qcvm != &sv.qcvm || !qcvm->progs)
		return NULL;
	for (i = 0; i < (int)countof(sv_vr_stock_axe_descriptors); ++i)
	{
		const sv_vr_stock_axe_descriptor_t *descriptor =
			&sv_vr_stock_axe_descriptors[i];
		if (qcvm->progscrc == descriptor->progscrc &&
			qcvm->progs->numstatements == descriptor->statements &&
			qcvm->progs->numfunctions == descriptor->functions &&
			qcvm->progs->numglobals == descriptor->globals &&
			SV_VRStockAxeFunctionStatementPin(descriptor))
			return descriptor;
	}
	return NULL;
}

#endif /* QS_VR_MELEE_STOCK_QC_H */
