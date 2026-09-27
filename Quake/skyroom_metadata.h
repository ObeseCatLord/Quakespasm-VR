/* Shared worldspawn skyroom value parser for the existing client and server
 * map owners. The first three values are origin; the fourth is parallax.
 * Four more optional values describe angular speed and its axis. */
#ifndef SKYROOM_METADATA_H
#define SKYROOM_METADATA_H

#include <math.h>
#include <stdlib.h>

static inline qboolean Skyroom_ParseMetadata (const char *text, float values[8], int *count)
{
	int parsed = 0;
	char *end;
	if (!text || !values)
		return false;
	for (int i = 0; i < 8; ++i)
		values[i] = 0.0f;
	while (1)
	{
		double value;
		while (*text == ' ' || *text == '\t')
			++text;
		if (!*text)
			break;
		if (parsed == 8)
			return false;
		value = strtod (text, &end);
		if (end == text || !isfinite (value) || !isfinite ((float)value))
			return false;
		values[parsed++] = (float)value;
		text = end;
	}
	if (parsed < 3)
		return false;
	if (count)
		*count = parsed;
	return true;
}

/* The inherited camera rule applies independently to the center view and
 * each eye: skyroom origin + parallax * viewer origin. Keep the server PVS
 * origin and future renderer view origins on this same rule. */
static inline qboolean Skyroom_ViewOrigin (const float skyroom[4], const float viewer[3], float out[3])
{
	if (!skyroom || !viewer || !out)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		if (!isfinite (skyroom[i]) || !isfinite (skyroom[3]) || !isfinite (viewer[i]))
			return false;
		out[i] = skyroom[i] + skyroom[3] * viewer[i];
		if (!isfinite (out[i]))
			return false;
	}
	return true;
}

#endif
