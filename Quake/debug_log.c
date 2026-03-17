#include "quakedef.h"
#include "debug_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE *debug_fp = NULL;

void DebugLog_Init(void)
{
	if (debug_fp)
		return;
	debug_fp = fopen("debug_mapchange.log", "a");
	if (debug_fp)
	{
		time_t t = time(NULL);
		fprintf(debug_fp, "\n=== Session started: %s", ctime(&t));
		fflush(debug_fp);
	}
}

void DebugLog_Close(void)
{
	if (debug_fp)
	{
		fclose(debug_fp);
		debug_fp = NULL;
	}
}

void DebugLog(const char *fmt, ...)
{
	va_list argptr;
	double timestamp;

	if (!debug_fp)
		DebugLog_Init();
	if (!debug_fp)
		return;

	timestamp = (host_initialized && realtime > 0) ? realtime : 0.0;
	fprintf(debug_fp, "[%10.3f] ", timestamp);

	va_start(argptr, fmt);
	vfprintf(debug_fp, fmt, argptr);
	va_end(argptr);

	fflush(debug_fp);
}
