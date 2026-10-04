/* Focused unit fixture for CAND-NET-005's hostile numeric boundary. The
 * engine-integrated selected-suite run must exercise CL_GetCSQCInputState's
 * zero/stale/future/empty tagged slots and pending preview, CSQC VM/entity
 * permission, non-finite entity/input values, failed PMCL_SetMoveVars, linked
 * CSQC plus engine-net collision, and nested #347 scratch restoration. */
#include "../Quake/quakedef.h"
#include "../Quake/csqc_prediction.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main (void)
{
	unsigned int sequence;
	int integer;

	assert (!CSQCPrediction_FloatToUInt (NAN, &sequence));
	assert (!CSQCPrediction_FloatToUInt (INFINITY, &sequence));
	assert (!CSQCPrediction_FloatToUInt (-1.0f, &sequence));
	assert (!CSQCPrediction_FloatToUInt (4.5f, &sequence));
	assert (!CSQCPrediction_FloatToInt (NAN, &integer));
	assert (!CSQCPrediction_FloatToInt (INFINITY, &integer));
	assert (!CSQCPrediction_FloatToInt (3.25f, &integer));

	assert (CSQCPrediction_FloatToUInt (0.0f, &sequence) && sequence == 0);
	assert (CSQCPrediction_FloatToUInt (99.0f, &sequence) && sequence == 99);

	puts ("CSQC prediction sequence and numeric guards passed");
	return 0;
}
