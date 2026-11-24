/* Playback of a remote tracked pose under realistic arrival bunching.

   cc -O2 -Wall -Werror -o vrik_interp_fixture tests/vrik_interp_fixture.c -lm && ./vrik_interp_fixture

   A sender moving a hand at constant speed samples its pose every fourth
   72 Hz command (the 20 Hz gate). The server relays at its next 25.5 ms frame
   and the viewer draws at 90 Hz. A viewer that plays the pose back correctly
   shows a constant speed, so the spread of the drawn per-frame speed is the
   judder. The previous two-pose, arrival-time, fixed 75 ms playback is kept
   here as the reference the timeline has to beat. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../Quake/vrik_interp.h"

#define SPEED 100.0 /* units per second */

typedef struct
{
	double cv, stall, jump, worst;
} result_t;

static unsigned int rng_state;

static double rng (void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return (rng_state & 0xffffff) / (double)0x1000000;
}

/* previous behaviour: newest two poses, arrival times, fixed delay */
static double legacy_sample (const double *value, const double *arrival, int count, double now)
{
	double blend;
	if (count < 2 || arrival[0] <= arrival[1])
		return value[0];
	blend = ((now - 0.075) - arrival[1]) / (arrival[0] - arrival[1]);
	if (blend < 0)
		blend = 0;
	if (blend > 1)
		blend = 1;
	return value[1] + (value[0] - value[1]) * blend;
}

static result_t measure (const double *shown, int frames, double frame_dt)
{
	result_t r;
	double	 sum = 0, sum2 = 0;
	int		 n = 0, stall = 0, jump = 0, i;

	memset (&r, 0, sizeof (r));
	for (i = 1; i < frames; i++)
	{
		double v = (shown[i] - shown[i - 1]) / frame_dt;
		sum += v;
		sum2 += v * v;
		n++;
		if (v < 0.3 * SPEED)
			stall++;
		if (v > 1.7 * SPEED)
			jump++;
		if (fabs (v - SPEED) / SPEED > r.worst)
			r.worst = fabs (v - SPEED) / SPEED;
	}
	sum /= n;
	r.cv = sqrt (fmax (0, sum2 / n - sum * sum)) / sum;
	r.stall = 100.0 * stall / n;
	r.jump = 100.0 * jump / n;
	return r;
}

static void run (const char *name, double uplink_jitter, double downlink_jitter, double loss, result_t *legacy, result_t *timeline_result)
{
	const double command_dt = 1.0 / 72, server_dt = 0.0255, frame_dt = 1.0 / 90, seconds = 60;
	enum
	{
		MAX_POSES = 2048,
		MAX_FRAMES = 8192
	};
	static double pose_value[MAX_POSES], pose_arrival[MAX_POSES];
	static int	  pose_sequence[MAX_POSES];
	static double shown_legacy[MAX_FRAMES], shown_timeline[MAX_FRAMES];
	int			  poses = 0, frames = 0, next = 0, sequence = 0, last_sequence = -1, i;
	double		  t, next_send = 0, last_arrival = -1, last_relay = -1;
	vrik_timeline_t timeline;
	double		  kept_value[VRIK_TIMELINE_SAMPLES] = {0};
	double		  legacy_value[2] = {0}, legacy_arrival[2] = {0};
	int			  legacy_count = 0;

	rng_state = 12345;
	/* sender and server */
	for (t = 0; t < seconds && poses < MAX_POSES; t += command_dt)
	{
		double at_server, relay, arrival;
		if (t < next_send)
			continue;
		next_send = t + 0.05;
		sequence++;
		if (rng () < loss)
			continue;
		at_server = t + 0.03 + rng () * uplink_jitter;
		relay = ceil (at_server / server_dt) * server_dt;
		if (relay - last_relay < 0.025) /* the server accepts one pose per 25 ms */
			continue;
		last_relay = relay;
		arrival = relay + 0.04 + rng () * downlink_jitter;
		if (arrival <= last_arrival) /* one path keeps the order */
			arrival = last_arrival + 0.0001;
		last_arrival = arrival;
		pose_value[poses] = SPEED * t;
		pose_arrival[poses] = arrival;
		pose_sequence[poses] = sequence;
		poses++;
	}

	/* viewer */
	VRIK_TimelineClear (&timeline);
	for (t = 1.0; t < seconds - 1 && frames < MAX_FRAMES; t += frame_dt)
	{
		int	  older, newer;
		float blend;

		while (next < poses && pose_arrival[next] <= t)
		{
			legacy_value[1] = legacy_value[0];
			legacy_arrival[1] = legacy_arrival[0];
			legacy_value[0] = pose_value[next];
			legacy_arrival[0] = t; /* stamped with the frame that read it, as the engine does */
			if (legacy_count < 2)
				legacy_count++;

			for (i = VRIK_TIMELINE_SAMPLES - 1; i > 0; i--)
				kept_value[i] = kept_value[i - 1];
			kept_value[0] = pose_value[next];
			VRIK_TimelinePush (&timeline, last_sequence < 0 ? 0 : pose_sequence[next] - last_sequence, t);
			last_sequence = pose_sequence[next];
			next++;
		}
		if (t < 3.0 || !legacy_count)
			continue; /* let both settle */
		shown_legacy[frames] = legacy_sample (legacy_value, legacy_arrival, legacy_count, t);
		assert (VRIK_TimelineSample (&timeline, t, &older, &newer, &blend));
		assert (older >= newer && older < timeline.count && blend >= 0 && blend <= 1);
		shown_timeline[frames] = kept_value[older] + (kept_value[newer] - kept_value[older]) * blend;
		frames++;
	}
	*legacy = measure (shown_legacy, frames, frame_dt);
	*timeline_result = measure (shown_timeline, frames, frame_dt);
	printf (
		"%-28s two-pose: cv %.3f stall %4.1f%% jump %4.1f%% worst %.2f | timeline: cv %.3f stall %4.1f%% jump %4.1f%% worst %.2f (period %.1f ms)\n", name,
		legacy->cv, legacy->stall, legacy->jump, legacy->worst, timeline_result->cv, timeline_result->stall, timeline_result->jump, timeline_result->worst,
		timeline.period * 1000);
}

int main (void)
{
	result_t legacy, timeline;
	vrik_timeline_t empty;
	int		 older, newer;
	float	 blend;

	VRIK_TimelineClear (&empty);
	assert (!VRIK_TimelineSample (&empty, 1.0, &older, &newer, &blend));
	VRIK_TimelinePush (&empty, 0, 1.0);
	assert (VRIK_TimelineSample (&empty, 1.0, &older, &newer, &blend) && older == 0 && newer == 0);
	VRIK_TimelinePush (&empty, 1, 1.055);
	assert (empty.count == 2);
	VRIK_TimelinePush (&empty, 1, 3.0); /* a long silence starts over */
	assert (empty.count == 1);

	run ("clean link", 0, 0, 0, &legacy, &timeline);
	assert (timeline.cv < 0.08 && timeline.stall == 0 && timeline.jump == 0);
	assert (legacy.cv > 4 * timeline.cv);

	run ("5 ms jitter each way", 0.005, 0.005, 0, &legacy, &timeline);
	assert (timeline.cv < 0.12 && timeline.stall < 0.5 && timeline.jump < 0.5);
	assert (legacy.cv > 3 * timeline.cv);

	run ("15 ms jitter each way", 0.015, 0.015, 0, &legacy, &timeline);
	assert (timeline.cv < 0.25 && timeline.cv < legacy.cv);

	run ("10 ms jitter, 5% loss", 0.010, 0.010, 0.05, &legacy, &timeline);
	assert (timeline.cv < legacy.cv && timeline.stall < legacy.stall + 0.5);

	puts ("vrik_interp_fixture: ok");
	return 0;
}
