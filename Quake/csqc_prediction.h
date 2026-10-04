/* CSQC prediction input validation shared by extension builtins and fixtures. */
#ifndef QUAKE_CSQC_PREDICTION_H
#define QUAKE_CSQC_PREDICTION_H

#include <limits.h>
#include <math.h>

static inline qboolean CSQCPrediction_FiniteVector (const vec3_t value)
{
	return isfinite (value[0]) && isfinite (value[1]) && isfinite (value[2]);
}

/* Do not cast a QC float until it is known to be an exact, representable
 * non-negative integer. QC can supply NaN/infinity and C float conversions
 * outside the destination range are undefined. */
static inline qboolean CSQCPrediction_FloatToUInt (float value, unsigned int *out)
{
	double number = value;

	if (!isfinite (number) || number < 0 || number > UINT_MAX || floor (number) != number)
		return false;
	*out = (unsigned int)number;
	return true;
}

static inline qboolean CSQCPrediction_FloatToInt (float value, int *out)
{
	double number = value;

	if (!isfinite (number) || number < INT_MIN || number > INT_MAX || floor (number) != number)
		return false;
	*out = (int)number;
	return true;
}

#endif /* QUAKE_CSQC_PREDICTION_H */
