/*
 * Asynchronous OpenGL GPU timing support.
 *
 * This module intentionally has no renderer dependencies.  The video layer
 * supplies its OpenGL version, extension string, and procedure resolver once
 * after a context is current.  Callers may then bracket non-overlapping work
 * with R_GPUTimer_Begin/End and collect completed samples later with Poll.
 */

#ifndef R_GPUTIMER_H
#define R_GPUTIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#define R_GPUTIMER_NAME_MAX 64

typedef void *(*r_gputimer_proc_resolver_t)(const char *name);

typedef struct r_gputimer_config_s
{
	int enabled;
	int gl_version_major;
	int gl_version_minor;
	const char *extensions;
	r_gputimer_proc_resolver_t get_proc_address;
} r_gputimer_config_t;

typedef struct r_gputimer_result_s
{
	char name[R_GPUTIMER_NAME_MAX];
	unsigned int sample_id;
	double milliseconds;
} r_gputimer_result_t;

typedef struct r_gputimer_stats_s
{
	unsigned int submitted;
	unsigned int completed;
	unsigned int dropped;
	unsigned int invalid;
} r_gputimer_stats_t;

/*
 * Initializes the fixed-size query ring.  The module is a complete no-op if
 * disabled, unsupported, or any required GL entry point cannot be resolved.
 * A current OpenGL context is required for Init and Shutdown.
 */
void R_GPUTimer_Init (const r_gputimer_config_t *config);
void R_GPUTimer_Shutdown (void);

void R_GPUTimer_SetEnabled (int enabled);
int R_GPUTimer_IsAvailable (void);

/* Segments may not overlap or nest.  An unmatched/mismatched End is invalid. */
void R_GPUTimer_Begin (const char *name, unsigned int sample_id);
void R_GPUTimer_End (const char *name);

/*
 * Returns one oldest completed sample.  It never waits for GPU completion;
 * zero means no completed sample is currently available.
 */
int R_GPUTimer_Poll (r_gputimer_result_t *result);
void R_GPUTimer_GetStats (r_gputimer_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* R_GPUTIMER_H */
