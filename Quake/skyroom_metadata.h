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

#endif
