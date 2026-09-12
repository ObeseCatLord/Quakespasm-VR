/*
 * Bounded native melee reach extraction.  This is deliberately a recognizer
 * for the already pinned acquisition statements, not a QuakeC interpreter:
 * it accepts only straight-line vector copy/add/sub/basis*immutable-scalar
 * expressions.  A new compiler layout, runtime temporary, or unknown branch
 * therefore fails closed and leaves assist disabled for that subtype.
 */
#ifndef QS_VR_MELEE_REACH_H
#define QS_VR_MELEE_REACH_H

#define SV_VR_MELEE_REACH_MAX_SITES 8

typedef struct {
	float origin, viewofs, forward, right, up;
	vec3_t literal;
} sv_vr_melee_reach_expr_t;

typedef struct {
	qcvm_t *vm;
	dprograms_t *progs;
	int crc, statements, functions, globals;
	qboolean known[SV_VR_MELEE_MJOLNIR_RAPIER + 1];
	qboolean valid[SV_VR_MELEE_MJOLNIR_RAPIER + 1];
	int variant[SV_VR_MELEE_MJOLNIR_RAPIER + 1];
	float radius[SV_VR_MELEE_MJOLNIR_RAPIER + 1];
	sv_vr_melee_reach_expr_t source[SV_VR_MELEE_MJOLNIR_RAPIER + 1];
	byte *writable;
	int writable_count;
} sv_vr_melee_reach_cache_t;

static sv_vr_melee_reach_cache_t sv_vr_melee_reach_cache;

void SV_VRMeleeReachReset(void)
{
	free(sv_vr_melee_reach_cache.writable);
	memset(&sv_vr_melee_reach_cache, 0, sizeof(sv_vr_melee_reach_cache));
}

static unsigned int SV_VRMeleeReachA(const dstatement_t *s)
{
	return (unsigned short)s->a;
}

static unsigned int SV_VRMeleeReachB(const dstatement_t *s)
{
	return (unsigned short)s->b;
}

static unsigned int SV_VRMeleeReachC(const dstatement_t *s)
{
	return (unsigned short)s->c;
}

static void SV_VRMeleeReachExprClear(sv_vr_melee_reach_expr_t *value)
{
	memset(value, 0, sizeof(*value));
}

static void SV_VRMeleeReachExprAdd(sv_vr_melee_reach_expr_t *out,
	const sv_vr_melee_reach_expr_t *a, const sv_vr_melee_reach_expr_t *b,
	float sign)
{
	out->origin = a->origin + sign * b->origin;
	out->viewofs = a->viewofs + sign * b->viewofs;
	out->forward = a->forward + sign * b->forward;
	out->right = a->right + sign * b->right;
	out->up = a->up + sign * b->up;
	for (int i = 0; i < 3; ++i)
		out->literal[i] = a->literal[i] + sign * b->literal[i];
}

static void SV_VRMeleeReachExprScale(sv_vr_melee_reach_expr_t *out,
	const sv_vr_melee_reach_expr_t *a, float scale)
{
	out->origin = a->origin * scale;
	out->viewofs = a->viewofs * scale;
	out->forward = a->forward * scale;
	out->right = a->right * scale;
	out->up = a->up * scale;
	for (int i = 0; i < 3; ++i)
		out->literal[i] = a->literal[i] * scale;
}

static int SV_VRMeleeReachFunctionLast(int first)
{
	int last = qcvm->progs->numstatements;

	for (int i = 0; i < qcvm->progs->numfunctions; ++i) {
		int candidate = qcvm->functions[i].first_statement;
		if (candidate > first && candidate < last)
			last = candidate;
	}
	return last;
}

static int SV_VRMeleeReachFunctionForStatement(int statement)
{
	int result = -1, first = -1;

	for (int i = 0; i < qcvm->progs->numfunctions; ++i)
		if (qcvm->functions[i].first_statement >= 0 &&
			qcvm->functions[i].first_statement <= statement &&
			qcvm->functions[i].first_statement > first) {
			first = qcvm->functions[i].first_statement;
			result = i;
		}
	return result >= 0 && statement < SV_VRMeleeReachFunctionLast(first) ? result : -1;
}

/* A literal is a static global cell which no statement directly writes.  The
 * compiler is free to pool it under a named symbol (for example a value also
 * called EF_CANDLELIGHT), so names are intentionally not part of this proof. */
static qboolean SV_VRMeleeReachWriteRange(const dstatement_t *s,
	unsigned int *first_out, unsigned int *words_out)
{
	unsigned int first, words;

	/* Stores name their destination in b.  Every non-store expression writes
	 * c; vector expressions/load/store cover three consecutive words. */
	if (s->op >= OP_STORE_F && s->op <= OP_STORE_FNC) {
		first = SV_VRMeleeReachB(s);
		words = s->op == OP_STORE_V ? 3 : 1;
	} else if (s->op == OP_MUL_FV || s->op == OP_MUL_VF ||
		s->op == OP_ADD_V || s->op == OP_SUB_V ||
		s->op == OP_LOAD_V) {
		first = SV_VRMeleeReachC(s);
		words = 3;
	} else if ((s->op >= OP_MUL_F && s->op <= OP_ADDRESS) ||
		(s->op >= OP_NOT_F && s->op <= OP_NOT_FNC) ||
		(s->op >= OP_AND && s->op <= OP_BITOR)) {
		first = SV_VRMeleeReachC(s);
		words = 1;
	} else
		return false;
	*first_out = first;
	*words_out = words;
	return true;
}

static qboolean SV_VRMeleeReachBuildWritable(void)
{
	int count;

	if (sv_vr_melee_reach_cache.writable)
		return sv_vr_melee_reach_cache.writable_count == qcvm->progs->numglobals;
	count = qcvm->progs->numglobals;
	if (count <= 0 || !(sv_vr_melee_reach_cache.writable = calloc(count, 1)))
		return false;
	sv_vr_melee_reach_cache.writable_count = count;
	/* QC need not assign time, trace results, input state, etc. The engine
	 * writes them. They are never immutable numeric pool entries. */
	memset(sv_vr_melee_reach_cache.writable, 1,
		q_min(count, (int)(sizeof(globalvars_t) / sizeof(float))));
#define QCEXTGLOBAL_FLOAT(n) if (qcvm->extglobals.n) { \
		ptrdiff_t at = (float *)qcvm->extglobals.n - qcvm->globals; \
		if (at >= 0 && at < count) sv_vr_melee_reach_cache.writable[at] = true; }
#define QCEXTGLOBAL_INT(n) QCEXTGLOBAL_FLOAT(n)
#define QCEXTGLOBAL_VECTOR(n) if (qcvm->extglobals.n) { \
		ptrdiff_t at = qcvm->extglobals.n - qcvm->globals; \
		for (int j = 0; j < 3; ++j) if (at + j >= 0 && at + j < count) \
			sv_vr_melee_reach_cache.writable[at + j] = true; }
	QCEXTGLOBALS_CSQC
	QCEXTGLOBALS_GAME
#undef QCEXTGLOBAL_FLOAT
#undef QCEXTGLOBAL_INT
#undef QCEXTGLOBAL_VECTOR
	for (int i = 0; i < qcvm->progs->numglobaldefs; ++i) {
		ddef_t *d = &qcvm->globaldefs[i];
		if (strncmp(PR_GetString(d->s_name), "autocvar_", 8)) continue;
		for (int j = 0; j < ((d->type & ~DEF_SAVEGLOBAL) == ev_vector ? 3 : 1); ++j)
			if (d->ofs + j < count) sv_vr_melee_reach_cache.writable[d->ofs + j] = true;
	}
	for (int i = 0; i < qcvm->progs->numfunctions; ++i) {
		dfunction_t *f = &qcvm->functions[i];
		int parameters = 0;
		if (f->parm_start < 0 || f->locals < 0)
			continue;
		/* EnterFunction writes parameters, but only saves/restores the rest
		 * of the local window. Some qcc builds pool literals in that window.
		 * Such cells remain constant if neither QC nor the engine writes them. */
		for (int j = 0; j < f->numparms && j < MAX_PARMS; ++j)
			parameters += f->parm_size[j];
		for (int j = 0; j < parameters && f->parm_start + j < count; ++j)
			sv_vr_melee_reach_cache.writable[f->parm_start + j] = true;
	}
	for (int i = 0; i < qcvm->progs->numstatements; ++i) {
		unsigned int first, words;
		if (!SV_VRMeleeReachWriteRange(&qcvm->statements[i], &first, &words))
			continue;
		for (unsigned int j = 0; j < words && first + j < (unsigned int)count; ++j)
			sv_vr_melee_reach_cache.writable[first + j] = true;
	}
	return true;
}

static qboolean SV_VRMeleeReachStaticFloat(unsigned int offset, float *value)
{
	if (!value || offset >= (unsigned int)qcvm->progs->numglobals ||
		offset < RESERVED_OFS || !isfinite(qcvm->globals[offset]))
		return false;
	if (!SV_VRMeleeReachBuildWritable() || sv_vr_melee_reach_cache.writable[offset])
		return false;
	*value = qcvm->globals[offset];
	return true;
}

static qboolean SV_VRMeleeReachStaticVector(unsigned int offset, vec3_t value)
{
	for (int i = 0; i < 3; ++i)
		if (!SV_VRMeleeReachStaticFloat(offset + i, &value[i]))
			return false;
	return true;
}

static qboolean SV_VRMeleeReachControlFlow(const dstatement_t *s)
{
	return s->op == OP_IF || s->op == OP_IFNOT ||
		s->op == OP_GOTO || s->op == OP_RETURN ||
		s->op == OP_DONE || s->op == OP_STATE;
}

/* The nearest previous write is usable only in its uninterrupted local block.
 * This intentionally does not follow aliases, calls, or predecessor blocks. */
enum { SV_VR_MELEE_REACH_NO_WRITER = -1, SV_VR_MELEE_REACH_BLOCKED = -2 };

/* Inspect edges, never execute or choose a branch. A predecessor outside
 * the definition/use interval may enter at the definition, but not past it.
 * This includes later loops jumping directly to the use itself. */
static qboolean SV_VRMeleeReachSingleEntry(int function, int writer, int use)
{
	int first = qcvm->functions[function].first_statement;
	int last = SV_VRMeleeReachFunctionLast(first);
	for (int i = first; i < last; ++i) {
		dstatement_t *s = &qcvm->statements[i];
		int jump;
		if (s->op == OP_GOTO) jump = s->a;
		else if (s->op == OP_IF || s->op == OP_IFNOT) jump = s->b;
		else continue;
		if (!jump || i + jump < first || i + jump >= last ||
			((i < writer || i >= use) && i + jump > writer && i + jump <= use))
			return false;
	}
	return true;
}

/* Pinned attacks may retain a local source across their acquisition calls.
 * This is the sole non-immediate reuse accepted here. Prove one
 * whole-vector writer, reject any overlapping write, and require every
 * earlier branch target to converge no later than that writer.  Thus no path
 * reaching a later acquisition can skip the definition. Nested QC calls
 * save/restore their local windows, including any overlap with this local. */
static qboolean SV_VRMeleeReachSharedSource(int function,
	unsigned int offset, int before, int *writer_out)
{
	dfunction_t *f;
	int first, writer = -1;

	if (!writer_out)
		return false;
	f = &qcvm->functions[function];
	if (f->parm_start < RESERVED_OFS || offset < (unsigned int)f->parm_start ||
		offset + 3 > (unsigned int)f->parm_start + f->locals)
		return false;
	first = f->first_statement;
	for (int i = first; i < before; ++i) {
		unsigned int write_first, words;

		if (!SV_VRMeleeReachWriteRange(&qcvm->statements[i], &write_first,
			&words) || write_first + words <= offset ||
			write_first >= offset + 3)
			continue;
		if (write_first != offset || words != 3 || writer >= 0)
			return false;
		writer = i;
	}
	if (writer < first || writer >= before ||
		!SV_VRMeleeReachSingleEntry(function, writer, before))
		return false;
	*writer_out = writer;
	return true;
}

static int SV_VRMeleeReachWriter(int function, int before, unsigned int offset)
{
	int first = qcvm->functions[function].first_statement;
	qboolean blocked = false;

	for (int i = before - 1; i >= first; --i) {
		dstatement_t *s = &qcvm->statements[i];
		unsigned int write_first, words;
		/* Calls may change arbitrary globals.  Do not reuse even a caller
		 * local without a separately proved, pinned dominance rule. */
		if (SV_VRMeleeReachControlFlow(s) ||
			(s->op >= OP_CALL0 && s->op <= OP_CALL8))
			blocked = true;
		if (!SV_VRMeleeReachWriteRange(s, &write_first, &words) ||
			write_first + words <= offset || write_first >= offset + 3)
			continue;
		/* A scalar write into any component, or a vector range which begins
		 * elsewhere, makes the previous vector value ambiguous. */
		if (write_first != offset || words != 3)
			return SV_VR_MELEE_REACH_BLOCKED;
		if (s->op == OP_STORE_V || s->op == OP_ADD_V || s->op == OP_SUB_V ||
			s->op == OP_MUL_FV || s->op == OP_MUL_VF || s->op == OP_LOAD_V) {
			if (!SV_VRMeleeReachSingleEntry(function, i, before)) blocked = true;
			return blocked ? SV_VR_MELEE_REACH_BLOCKED : i;
		}
		return SV_VR_MELEE_REACH_BLOCKED;
	}
	/* No assignment is a literal candidate; calls/branches cannot turn an
	 * independently proven immutable global cell into a temporary. */
	return SV_VR_MELEE_REACH_NO_WRITER;
}

static unsigned int SV_VRMeleeReachGlobalOffset(const void *field)
{
	return (unsigned int)((const float *)field - qcvm->globals);
}

static qboolean SV_VRMeleeReachScalar(int function, unsigned int offset, float *value)
{
	/* Drake's direct hammer chooses one of two literal ranges before its
	 * shared trace. Reuse that audited branch, but read the number from QC. */
	if (offset == 7301 && SV_VRMeleeDrakeHammerDescriptor() && function == 1132) {
		dstatement_t *s = &qcvm->statements[
			sv_vr_melee_reach_cache.variant[SV_VR_MELEE_DRAKE_HAMMER] ? 38947 : 38944];
		return s->op == OP_STORE_F && SV_VRMeleeReachB(s) == offset &&
			SV_VRMeleeReachStaticFloat(SV_VRMeleeReachA(s), value);
	}
	return SV_VRMeleeReachStaticFloat(offset, value);
}

static qboolean SV_VRMeleeReachVector(int function, int before,
	unsigned int offset, int depth, sv_vr_melee_reach_expr_t *out)
{
	dstatement_t *s;
	sv_vr_melee_reach_expr_t a, b;
	float scalar;
	vec3_t literal;
	int writer;

	if (!out || depth > 20 || offset + 2 >= (unsigned int)qcvm->progs->numglobals)
		return false;
	SV_VRMeleeReachExprClear(out);
	/* Drake's axe helper receives its direction as a vector argument. The
	 * existing adapter pins its sole melee caller; inspect that argument at
	 * the call boundary instead of reading a stale parameter register. */
	if (function == 919 && offset == 5833 && SV_VRMeleeDrakeDescriptor()) {
		const sv_vr_melee_drake_descriptor_t *d = SV_VRMeleeDrakeDescriptor();
		return SV_VRMeleeReachVector(d->leaf, d->root_call, OFS_PARM1,
			depth + 1, out);
	}
	if (offset == SV_VRMeleeReachGlobalOffset(pr_global_struct->v_forward)) {
		out->forward = 1;
		return true;
	}
	if (offset == SV_VRMeleeReachGlobalOffset(pr_global_struct->v_right)) {
		out->right = 1;
		return true;
	}
	if (offset == SV_VRMeleeReachGlobalOffset(pr_global_struct->v_up)) {
		out->up = 1;
		return true;
	}
	writer = SV_VRMeleeReachWriter(function, before, offset);
	if (writer == SV_VR_MELEE_REACH_BLOCKED) {
		int shared_writer;
		if (!SV_VRMeleeReachSharedSource(function, offset, before,
			&shared_writer))
			return false;
		return SV_VRMeleeReachVector(function, shared_writer + 1, offset,
			depth + 1, out);
	}
	if (writer == SV_VR_MELEE_REACH_NO_WRITER) {
		if (!SV_VRMeleeReachStaticVector(offset, literal))
			return false;
		VectorCopy(literal, out->literal);
		return true;
	}
	s = &qcvm->statements[writer];
	switch (s->op) {
	case OP_STORE_V:
		return SV_VRMeleeReachVector(function, writer, SV_VRMeleeReachA(s),
			depth + 1, out);
	case OP_ADD_V:
	case OP_SUB_V:
		if (!SV_VRMeleeReachVector(function, writer, SV_VRMeleeReachA(s),
			depth + 1, &a) || !SV_VRMeleeReachVector(function, writer,
			SV_VRMeleeReachB(s), depth + 1, &b))
			return false;
		SV_VRMeleeReachExprAdd(out, &a, &b, s->op == OP_ADD_V ? 1 : -1);
		return true;
	case OP_MUL_FV:
		if (!SV_VRMeleeReachScalar(function, SV_VRMeleeReachA(s), &scalar) ||
			!SV_VRMeleeReachVector(function, writer, SV_VRMeleeReachB(s),
				depth + 1, &a))
			return false;
		SV_VRMeleeReachExprScale(out, &a, scalar);
		return true;
	case OP_MUL_VF:
		if (!SV_VRMeleeReachScalar(function, SV_VRMeleeReachB(s), &scalar) ||
			!SV_VRMeleeReachVector(function, writer, SV_VRMeleeReachA(s),
				depth + 1, &a))
			return false;
		SV_VRMeleeReachExprScale(out, &a, scalar);
		return true;
	case OP_LOAD_V:
		/* Only the pinned player fields are semantic inputs. */
		if (SV_VRMeleeReachA(s) != SV_VRMeleeReachGlobalOffset(&pr_global_struct->self) ||
			!SV_VRMeleeReachStaticFloat(SV_VRMeleeReachB(s), &scalar))
			return false;
		if (G_INT(SV_VRMeleeReachB(s)) ==
			offsetof(entvars_t, origin) / sizeof(float)) {
			out->origin = 1;
			return true;
		}
		if (G_INT(SV_VRMeleeReachB(s)) ==
			offsetof(entvars_t, view_ofs) / sizeof(float)) {
			out->viewofs = 1;
			return true;
		}
		return false;
	default:
		return false;
	}
}

static qboolean SV_VRMeleeReachCall(int statement, int expected_index,
	const char *callee)
{
	dstatement_t *s;
	int index;

	/* Builtin dfunctions intentionally have no s_name.  The pinned numeric
	 * function target is the proof here; a name check would reject #14. */
	(void)callee;
	if (statement < 0 || statement >= qcvm->progs->numstatements)
		return false;
	s = &qcvm->statements[statement];
	if (s->op < OP_CALL0 || s->op > OP_CALL8 ||
		SV_VRMeleeReachA(s) >= (unsigned int)qcvm->progs->numglobals)
		return false;
	memcpy(&index, &qcvm->globals[SV_VRMeleeReachA(s)], sizeof(index));
	return index > 0 && index < qcvm->progs->numfunctions &&
		(expected_index < 0 ? qcvm->functions[index].first_statement == expected_index :
		index == expected_index);
}

static qboolean SV_VRMeleeReachAt(int statement,
	sv_vr_melee_reach_expr_t *source, float *radius)
{
	sv_vr_melee_reach_expr_t start, end, delta;
	int function = SV_VRMeleeReachFunctionForStatement(statement);

	if (function < 0 || !source || !radius ||
		!SV_VRMeleeReachVector(function, statement, OFS_PARM0, 0, &start) ||
		!SV_VRMeleeReachVector(function, statement, OFS_PARM1, 0, &end))
		return false;
	SV_VRMeleeReachExprAdd(&delta, &end, &start, -1);
	if (fabsf(start.origin - 1) > .0001f || fabsf(delta.origin) > .0001f ||
		fabsf(delta.viewofs) > .0001f || fabsf(delta.literal[0]) > .0001f ||
		fabsf(delta.literal[1]) > .0001f || fabsf(delta.literal[2]) > .0001f)
		return false;
	*source = start;
	*radius = sqrtf(delta.forward * delta.forward + delta.right * delta.right +
		delta.up * delta.up);
	return isfinite(*radius) && *radius > 0 && isfinite(start.viewofs) &&
		isfinite(start.forward) && isfinite(start.right) && isfinite(start.up) &&
		SV_VRMeleeFiniteVector(start.literal);
}

static qboolean SV_VRMeleeReachPinnedSites(edict_t *player,
	sv_vr_melee_subtype_t subtype, int sites[SV_VR_MELEE_REACH_MAX_SITES],
	int *count, int *callee_index, const char **callee)
{
	const sv_vr_melee_stock_descriptor_t *stock;
	const sv_vr_melee_qbj3_descriptor_t *qbj3;
	dfunction_t *root;

	*count = 0;
	*callee_index = -16; /* Direct traceline builtin, independent of function index. */
	*callee = NULL;
	if (subtype == SV_VR_MELEE_STOCK_AXE &&
		(stock = SV_VRMeleeStockDescriptor()) != NULL) {
		sites[(*count)++] = stock->trace_statement;
		*callee_index = -16;
		*callee = "traceline";
	} else if ((subtype == SV_VR_MELEE_QBJ3_WRENCH ||
		subtype == SV_VR_MELEE_QBJ3_BERSERK) &&
		(qbj3 = SV_VRMeleeQBJ3Descriptor()) != NULL) {
		root = &qcvm->functions[subtype == SV_VR_MELEE_QBJ3_WRENCH ?
			qbj3->wrench_index : qbj3->berserk_index];
		/* Exact immediate acquisition fan offsets in both pinned revisions. */
		static const int wrench[] = {17, 33, 64, 97, 130};
		static const int fists[] = {37, 53, 84, 117, 150};
		const int *offsets = subtype == SV_VR_MELEE_QBJ3_WRENCH ? wrench : fists;
		for (int i = 0; i < 5; ++i)
			sites[(*count)++] = root->first_statement + offsets[i];
		*callee_index = 424;
		*callee = "traceline2";
	} else if (subtype == SV_VR_MELEE_ENYO_KATANA && SV_VRMeleeEnyoProgs()) {
		/* Exact non-uniform offsets are pinned by W_FireSword's ABI. */
		sites[(*count)++] = 12980; sites[(*count)++] = 12996;
		sites[(*count)++] = 13027; sites[(*count)++] = 13060;
		sites[(*count)++] = 13093;
		*callee_index = -16;
		*callee = "traceline";
	} else if (subtype == SV_VR_MELEE_BONK_HAMMER && SV_VRMeleeBonkProgs()) {
		/* W_SwingHammer's five charge fan acquisitions. */
		static const int bonk[] = {13531, 13547, 13578, 13616, 13649};
		for (int i = 0; i < (int)countof(bonk); ++i)
			sites[(*count)++] = bonk[i];
		*callee_index = -16;
		*callee = "traceline";
	} else if (subtype == SV_VR_MELEE_DWELL_AXE ||
		subtype == SV_VR_MELEE_DWELL_BERSERK) {
		if (!SV_VRMeleeDwellProgs())
			return false;
		/* These are the existing selected-model/expiry branches, not a
		 * speculative branch walk: Berserk is 96, normal is 64. */
		sites[(*count)++] = subtype == SV_VR_MELEE_DWELL_BERSERK ? 14578 : 14586;
		*callee_index = 396;
		*callee = "traceline2";
	} else if (subtype == SV_VR_MELEE_IMMORTAL_AXE ||
		subtype == SV_VR_MELEE_IMMORTAL_HAMMER) {
		int trace = SV_VRMeleeImmortalTraceStatement(subtype ==
			SV_VR_MELEE_IMMORTAL_AXE ? SV_VR_IMMORTAL_AXE : SV_VR_IMMORTAL_HAMMER);
		if (trace < 0)
			return false;
		sites[(*count)++] = trace;
		*callee_index = -16;
		*callee = "traceline";
	} else if (subtype == SV_VR_MELEE_MJOLNIR_RAPIER && SV_VRMeleeRapierProgs()) {
		sites[(*count)++] = sv_vr_melee_rapier_descriptor.attack_trace;
		*callee_index = -16;
		*callee = "traceline";
	} else if (subtype == SV_VR_MELEE_MJOLNIR_HAMMER && SV_VRMeleeMjolnirHammerProgs()) {
		sites[(*count)++] = 117777;
		*callee_index = -16;
		*callee = "traceline";
	} else if (subtype == SV_VR_MELEE_MJOLNIR_GUNGNIR && SV_VRMeleeGungnirProgs()) {
		sites[(*count)++] = 116259;
		*callee_index = -16;
		*callee = "traceline";
	} else if (subtype == SV_VR_MELEE_HONEY_AXE && SV_VRMeleeHoneyProgs()) {
		sites[(*count)++] = 4712;
	} else if (subtype == SV_VR_MELEE_AD_AXE && SV_VRMeleeADDescriptor()) {
		sites[(*count)++] = SV_VRMeleeADDescriptor()->trace;
	} else if (subtype == SV_VR_MELEE_COPPER_AXE && SV_VRMeleeCopperDescriptor()) {
		sites[(*count)++] = SV_VRMeleeCopperDescriptor()->root_helper_call;
		*callee_index = SV_VRMeleeCopperDescriptor()->helper;
	} else if (subtype == SV_VR_MELEE_ALK_AXE && SV_VRMeleeALKDescriptor()) {
		sites[(*count)++] = SV_VRMeleeALKDescriptor()->trace;
	} else if (subtype == SV_VR_MELEE_DRAKE_AXE && SV_VRMeleeDrakeDescriptor()) {
		sites[(*count)++] = SV_VRMeleeDrakeDescriptor()->trace;
	} else if (subtype == SV_VR_MELEE_MJOLNIR_AXE && SV_VRMeleeMjolnirDescriptor()) {
		sites[(*count)++] = SV_VRMeleeMjolnirAxeTraceStatement();
	} else if (subtype == SV_VR_MELEE_HAMMER && SV_VRMeleeHammerDescriptor()) {
		const sv_vr_melee_hammer_descriptor_t *d = SV_VRMeleeHammerDescriptor();
		sites[(*count)++] = player->v.ammo_cells >= 30 ? d->primary_trace : d->axe_trace;
	} else if (subtype == SV_VR_MELEE_DRAKE_HAMMER && SV_VRMeleeDrakeHammerDescriptor()) {
		sites[(*count)++] = 38955;
	} else if (subtype == SV_VR_MELEE_MJOLNIR_SCIMITAR && SV_VRMeleeScimitarProgs()) {
		sites[(*count)++] = 112997;
		*callee_index = -22;
	} else if (subtype == SV_VR_MELEE_MJOLNIR_MACE && SV_VRMeleeMaceDescriptor()) {
		sites[(*count)++] = 115535;
		*callee_index = -22;
	} else
		return false;
	return true;
}

static qboolean SV_VRMeleeReachExtract(edict_t *player,
	sv_vr_melee_subtype_t subtype, sv_vr_melee_reach_expr_t *source,
	float *radius)
{
	int sites[SV_VR_MELEE_REACH_MAX_SITES], count, callee_index;
	const char *callee;
	float max_radius = 0;
	sv_vr_melee_reach_expr_t candidate, selected;

	if (!player || player->free || !source || !radius ||
		!SV_VRMeleeReachPinnedSites(player, subtype, sites, &count, &callee_index,
			&callee) ||
		count <= 0 || count > SV_VR_MELEE_REACH_MAX_SITES)
		return false;
	for (int i = 0; i < count; ++i) {
		float value;
		if (!SV_VRMeleeReachCall(sites[i], callee_index, callee))
			return false;
		if (callee_index == -22) {
			int function = SV_VRMeleeReachFunctionForStatement(sites[i]);
			dstatement_t *arg = &qcvm->statements[sites[i] - 1];
			if (function < 0 || arg->op != OP_STORE_F ||
				SV_VRMeleeReachB(arg) != OFS_PARM1 ||
				!SV_VRMeleeReachStaticFloat(SV_VRMeleeReachA(arg), &value) || value <= 0 ||
				!SV_VRMeleeReachVector(function, sites[i], OFS_PARM0, 0, &candidate) ||
				candidate.origin != 1)
				return false;
		} else if (!SV_VRMeleeReachAt(sites[i], &candidate, &value))
			return false;
		/* A single envelope represents a fan only if every ray shares its
		 * source. Do not silently discard an independently centered attack. */
		if (i && memcmp(&candidate, &selected, sizeof(candidate)))
			return false;
		if (i == 0 || value > max_radius) {
			selected = candidate;
			max_radius = value;
		}
	}
	*source = selected;
	*radius = max_radius;
	return true;
}

/* aim is the accepted hand/akimbo Euler basis used by the native-adapter
 * context.  source_offset is relative to player origin; callers must not
 * silently substitute viewofs if extraction returns false. */
static qboolean SV_VRMeleeNativeReach(edict_t *player,
	sv_vr_melee_subtype_t subtype, const vec3_t aim, float *radius,
	vec3_t source_offset)
{
	sv_vr_melee_reach_expr_t *source;
	vec3_t angles, forward, right, up;
	int variant;

	if (!player || !radius || !source_offset || !SV_VRMeleeFiniteVector(aim) ||
		subtype <= SV_VR_MELEE_NONE || subtype > SV_VR_MELEE_MJOLNIR_RAPIER ||
		!qcvm || qcvm != &sv.qcvm || !qcvm->progs)
		return false;
	if (sv_vr_melee_reach_cache.vm != qcvm ||
		sv_vr_melee_reach_cache.progs != qcvm->progs ||
		sv_vr_melee_reach_cache.crc != qcvm->crc ||
		sv_vr_melee_reach_cache.statements != qcvm->progs->numstatements ||
		sv_vr_melee_reach_cache.functions != qcvm->progs->numfunctions ||
		sv_vr_melee_reach_cache.globals != qcvm->progs->numglobals) {
		SV_VRMeleeReachReset();
		sv_vr_melee_reach_cache.vm = qcvm;
		sv_vr_melee_reach_cache.progs = qcvm->progs;
		sv_vr_melee_reach_cache.crc = qcvm->crc;
		sv_vr_melee_reach_cache.statements = qcvm->progs->numstatements;
		sv_vr_melee_reach_cache.functions = qcvm->progs->numfunctions;
		sv_vr_melee_reach_cache.globals = qcvm->progs->numglobals;
	}
	variant = subtype == SV_VR_MELEE_HAMMER ? player->v.ammo_cells >= 30 :
		subtype == SV_VR_MELEE_DRAKE_HAMMER ?
		(!player->v.button0 && player->v.ammo_cells >= 25) : 0;
	if (!sv_vr_melee_reach_cache.known[subtype] ||
		sv_vr_melee_reach_cache.variant[subtype] != variant) {
		sv_vr_melee_reach_cache.variant[subtype] = variant;
		sv_vr_melee_reach_cache.known[subtype] = true;
		sv_vr_melee_reach_cache.valid[subtype] = SV_VRMeleeReachExtract(player,
			subtype, &sv_vr_melee_reach_cache.source[subtype],
			&sv_vr_melee_reach_cache.radius[subtype]);
	}
	if (!sv_vr_melee_reach_cache.valid[subtype])
		return false;
	source = &sv_vr_melee_reach_cache.source[subtype];
	VectorCopy(aim, angles);
	AngleVectors(angles, forward, right, up);
	for (int i = 0; i < 3; ++i)
		source_offset[i] = source->viewofs * player->v.view_ofs[i] +
			source->literal[i] + source->forward * forward[i] +
			source->right * right[i] + source->up * up[i];
	*radius = sv_vr_melee_reach_cache.radius[subtype];
	return isfinite(*radius) && SV_VRMeleeFiniteVector(source_offset);
}

#endif /* QS_VR_MELEE_REACH_H */
