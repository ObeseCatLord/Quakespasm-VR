/*
 * Asynchronous GPU timer-query ring used by renderer performance profiling.
 */

#include "quakedef.h"
#include "r_gputimer.h"

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif

#define R_GPUTIMER_RING_SIZE 64
#define R_GPUTIMER_FINALIZE_TIMEOUT_MS 25

typedef void (APIENTRY *r_gputimer_gen_queries_t)(GLsizei n, GLuint *ids);
typedef void (APIENTRY *r_gputimer_delete_queries_t)(GLsizei n, const GLuint *ids);
typedef void (APIENTRY *r_gputimer_begin_query_t)(GLenum target, GLuint id);
typedef void (APIENTRY *r_gputimer_end_query_t)(GLenum target);
typedef void (APIENTRY *r_gputimer_get_query_object_iv_t)(GLuint id, GLenum pname, GLint *params);
typedef void (APIENTRY *r_gputimer_get_query_object_ui64v_t)(GLuint id, GLenum pname, unsigned long long *params);
typedef void (APIENTRY *r_gputimer_flush_t)(void);

typedef struct r_gputimer_query_s
{
	GLuint id;
	char name[R_GPUTIMER_NAME_MAX];
	unsigned int sample_id;
} r_gputimer_query_t;

typedef struct r_gputimer_state_s
{
	int available;
	int enabled;
	int active;
	unsigned int head;
	unsigned int count;
	unsigned int active_slot;
	r_gputimer_query_t queries[R_GPUTIMER_RING_SIZE];
	r_gputimer_stats_t stats;
	r_gputimer_gen_queries_t GenQueries;
	r_gputimer_delete_queries_t DeleteQueries;
	r_gputimer_begin_query_t BeginQuery;
	r_gputimer_end_query_t EndQuery;
	r_gputimer_get_query_object_iv_t GetQueryObjectiv;
	r_gputimer_get_query_object_ui64v_t GetQueryObjectui64v;
	r_gputimer_flush_t Flush;
	r_gputimer_result_callback_t result_callback;
} r_gputimer_state_t;

static r_gputimer_state_t r_gputimer;

static int R_GPUTimer_HasExtension (const char *extensions, const char *name)
{
	const char *match;
	size_t name_len;

	if (!extensions || !name || !*name)
		return 0;

	name_len = strlen (name);
	for (match = strstr (extensions, name); match; match = strstr (match + name_len, name))
	{
		if ((match == extensions || match[-1] == ' ') &&
			(match[name_len] == '\0' || match[name_len] == ' '))
			return 1;
	}

	return 0;
}

static void *R_GPUTimer_GetProc (r_gputimer_proc_resolver_t resolver, const char *core_name, const char *arb_name)
{
	void *proc;

	proc = resolver (core_name);
	if (!proc && arb_name)
		proc = resolver (arb_name);
	return proc;
}

void R_GPUTimer_Init (const r_gputimer_config_t *config)
{
	int core_timer_queries;
	int arb_timer_queries;
	GLuint ids[R_GPUTIMER_RING_SIZE];
	int i;

	memset (&r_gputimer, 0, sizeof(r_gputimer));
	if (!config || !config->enabled || !config->get_proc_address)
		return;

	core_timer_queries = config->gl_version_major > 3 ||
		(config->gl_version_major == 3 && config->gl_version_minor >= 3);
	arb_timer_queries = R_GPUTimer_HasExtension (config->extensions, "GL_ARB_timer_query");
	if (!core_timer_queries && !arb_timer_queries)
		return;

	r_gputimer.GenQueries = (r_gputimer_gen_queries_t) R_GPUTimer_GetProc (config->get_proc_address, "glGenQueries", "glGenQueriesARB");
	r_gputimer.DeleteQueries = (r_gputimer_delete_queries_t) R_GPUTimer_GetProc (config->get_proc_address, "glDeleteQueries", "glDeleteQueriesARB");
	r_gputimer.BeginQuery = (r_gputimer_begin_query_t) R_GPUTimer_GetProc (config->get_proc_address, "glBeginQuery", "glBeginQueryARB");
	r_gputimer.EndQuery = (r_gputimer_end_query_t) R_GPUTimer_GetProc (config->get_proc_address, "glEndQuery", "glEndQueryARB");
	r_gputimer.GetQueryObjectiv = (r_gputimer_get_query_object_iv_t) R_GPUTimer_GetProc (config->get_proc_address, "glGetQueryObjectiv", "glGetQueryObjectivARB");
	r_gputimer.GetQueryObjectui64v = (r_gputimer_get_query_object_ui64v_t) R_GPUTimer_GetProc (config->get_proc_address, "glGetQueryObjectui64v", "glGetQueryObjectui64vEXT");
	r_gputimer.Flush = (r_gputimer_flush_t) R_GPUTimer_GetProc (config->get_proc_address, "glFlush", NULL);
	if (!r_gputimer.GenQueries || !r_gputimer.DeleteQueries || !r_gputimer.BeginQuery ||
		!r_gputimer.EndQuery || !r_gputimer.GetQueryObjectiv || !r_gputimer.GetQueryObjectui64v ||
		!r_gputimer.Flush)
	{
		memset (&r_gputimer, 0, sizeof(r_gputimer));
		return;
	}

	memset (ids, 0, sizeof(ids));
	r_gputimer.GenQueries (R_GPUTIMER_RING_SIZE, ids);
	for (i = 0; i < R_GPUTIMER_RING_SIZE; i++)
	{
		if (!ids[i])
		{
			r_gputimer.DeleteQueries (R_GPUTIMER_RING_SIZE, ids);
			memset (&r_gputimer, 0, sizeof(r_gputimer));
			return;
		}
		r_gputimer.queries[i].id = ids[i];
	}
	r_gputimer.available = 1;
	r_gputimer.enabled = 1;
}

static void R_GPUTimer_EmitAvailableResults (void)
{
	r_gputimer_result_t result;

	if (!r_gputimer.result_callback)
		return;

	while (R_GPUTimer_Poll (&result))
		r_gputimer.result_callback (&result);
}

void R_GPUTimer_Shutdown (void)
{
	GLuint ids[R_GPUTIMER_RING_SIZE];
	int i;

	if (!r_gputimer.available)
		return;

	if (r_gputimer.active)
	{
		if (r_gputimer.active_slot != R_GPUTIMER_RING_SIZE)
		{
			r_gputimer.EndQuery (GL_TIME_ELAPSED);
			r_gputimer.count++;
			r_gputimer.stats.submitted++;
		}
		r_gputimer.active = 0;
		r_gputimer.stats.invalid++;
	}

	/*
	 * Unlike a desktop swap, VR does not necessarily flush this context.  Give
	 * the oldest pending query a short, bounded opportunity to complete before
	 * deleting it so a capture ending immediately after its last eye still
	 * retains completed samples.  Normal frame polling never waits.
	 */
	R_GPUTimer_Flush ();
	if (r_gputimer.count && r_gputimer.result_callback)
	{
		double deadline = Sys_DoubleTime () +
			((double) R_GPUTIMER_FINALIZE_TIMEOUT_MS / 1000.0);

		do
		{
			unsigned int completed_before = r_gputimer.stats.completed;
			R_GPUTimer_EmitAvailableResults ();
			if (!r_gputimer.count || Sys_DoubleTime () >= deadline)
				break;
			if (r_gputimer.stats.completed == completed_before)
				Sys_Sleep (1);
		}
		while (r_gputimer.count);
	}

	for (i = 0; i < R_GPUTIMER_RING_SIZE; i++)
		ids[i] = r_gputimer.queries[i].id;
	r_gputimer.DeleteQueries (R_GPUTIMER_RING_SIZE, ids);
	memset (&r_gputimer, 0, sizeof(r_gputimer));
}

void R_GPUTimer_SetEnabled (int enabled)
{
	if (!r_gputimer.available)
		return;

	r_gputimer.enabled = enabled != 0;
}

int R_GPUTimer_IsAvailable (void)
{
	return r_gputimer.available;
}

void R_GPUTimer_SetResultCallback (r_gputimer_result_callback_t callback)
{
	r_gputimer.result_callback = callback;
}

void R_GPUTimer_Flush (void)
{
	if (r_gputimer.available && r_gputimer.Flush &&
		(r_gputimer.active || r_gputimer.count))
		r_gputimer.Flush ();
}

void R_GPUTimer_Begin (const char *name, unsigned int sample_id)
{
	unsigned int slot;

	if (!r_gputimer.available || !r_gputimer.enabled)
		return;
	if (r_gputimer.active)
	{
		r_gputimer.stats.invalid++;
		return;
	}
	if (!name || !*name)
	{
		r_gputimer.stats.invalid++;
		return;
	}
	if (r_gputimer.count == R_GPUTIMER_RING_SIZE)
	{
		r_gputimer.stats.dropped++;
		/* Consume the matching End without turning an expected drop into an error. */
		r_gputimer.active_slot = R_GPUTIMER_RING_SIZE;
		r_gputimer.active = 1;
		return;
	}

	slot = (r_gputimer.head + r_gputimer.count) % R_GPUTIMER_RING_SIZE;
	q_strlcpy (r_gputimer.queries[slot].name, name, sizeof(r_gputimer.queries[slot].name));
	r_gputimer.queries[slot].sample_id = sample_id;
	r_gputimer.BeginQuery (GL_TIME_ELAPSED, r_gputimer.queries[slot].id);
	r_gputimer.active_slot = slot;
	r_gputimer.active = 1;
}

void R_GPUTimer_End (const char *name)
{
	r_gputimer_query_t *query;

	if (!r_gputimer.available || (!r_gputimer.enabled && !r_gputimer.active))
		return;
	if (!r_gputimer.active)
	{
		r_gputimer.stats.invalid++;
		return;
	}

	if (r_gputimer.active_slot == R_GPUTIMER_RING_SIZE)
	{
		r_gputimer.active = 0;
		return;
	}

	query = &r_gputimer.queries[r_gputimer.active_slot];
	if (name && *name && strcmp (name, query->name))
		r_gputimer.stats.invalid++;

	r_gputimer.EndQuery (GL_TIME_ELAPSED);
	r_gputimer.active = 0;
	r_gputimer.count++;
	r_gputimer.stats.submitted++;
}

int R_GPUTimer_Poll (r_gputimer_result_t *result)
{
	r_gputimer_query_t *query;
	GLint available;
	unsigned long long nanoseconds;

	if (!r_gputimer.available || !result || !r_gputimer.count)
		return 0;

	query = &r_gputimer.queries[r_gputimer.head];
	r_gputimer.GetQueryObjectiv (query->id, GL_QUERY_RESULT_AVAILABLE, &available);
	if (!available)
		return 0;

	/* GL_QUERY_RESULT is safe only after GL_QUERY_RESULT_AVAILABLE is true. */
	r_gputimer.GetQueryObjectui64v (query->id, GL_QUERY_RESULT, &nanoseconds);
	q_strlcpy (result->name, query->name, sizeof(result->name));
	result->sample_id = query->sample_id;
	result->milliseconds = (double) nanoseconds / 1000000.0;
	r_gputimer.head = (r_gputimer.head + 1) % R_GPUTIMER_RING_SIZE;
	r_gputimer.count--;
	r_gputimer.stats.completed++;
	return 1;
}

void R_GPUTimer_GetStats (r_gputimer_stats_t *stats)
{
	if (stats)
		*stats = r_gputimer.stats;
}
