/*
Copyright (C) 2026 Quakespasm VR contributors

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#ifndef VRIK_INTERP_H
#define VRIK_INTERP_H

/* Playback timeline for a remote player's tracked pose.

A pose stream is sampled by its sender at a steady rate, but it reaches a
viewer through the sender's command packets and the server's frames, so the
arrivals are bunched: two server frames apart, then three. Interpolating
between the newest two poses by arrival time makes a hand speed up, slow down
and step even when it moved evenly.

The timeline keeps the last few poses and gives each a time on the sender's
steady grid, recovered from the sequence numbers and a slow average of the
arrivals. Playback then runs a fixed distance behind that grid, far enough
that the next pose has normally arrived before it is needed.

No engine dependencies: the same code is driven by a test with synthetic arrivals. */

#define VRIK_TIMELINE_SAMPLES	  4
#define VRIK_TIMELINE_PERIOD	  0.055 // initial guess: 20 Hz gate on a 72 Hz command stream
#define VRIK_TIMELINE_MIN_PERIOD  0.02
#define VRIK_TIMELINE_MAX_PERIOD  0.2
#define VRIK_TIMELINE_SLACK		  0.03 // one server frame of bunching plus a little jitter
#define VRIK_TIMELINE_RESYNC	  0.15 // arrival this far from the grid starts a new grid
#define VRIK_TIMELINE_GRID_GAIN	  0.05 // how fast the grid follows the arrivals
#define VRIK_TIMELINE_PERIOD_GAIN 0.05

typedef struct
{
	int	   count;							// valid entries in time[], newest first
	double time[VRIK_TIMELINE_SAMPLES];		// sender-grid time of each kept pose, in the viewer's clock
	double arrival[VRIK_TIMELINE_SAMPLES];	// when it actually arrived
	double period;							// estimated spacing of the sender's poses
} vrik_timeline_t;

static inline void VRIK_TimelineClear (vrik_timeline_t *timeline)
{
	timeline->count = 0;
	timeline->period = VRIK_TIMELINE_PERIOD;
}

/* Record a new pose. steps is how far its sequence number advanced (1 when
nothing was lost). The caller shifts its pose storage the same way: entry 0
is the newest and entries beyond VRIK_TIMELINE_SAMPLES fall off. */
static inline void VRIK_TimelinePush (vrik_timeline_t *timeline, int steps, double arrival)
{
	double gridtime = arrival;
	int	   i;

	if (timeline->period < VRIK_TIMELINE_MIN_PERIOD || timeline->period > VRIK_TIMELINE_MAX_PERIOD)
		timeline->period = VRIK_TIMELINE_PERIOD;
	if (timeline->count > 0 && steps >= 1 && steps <= 8)
	{
		double gap = (arrival - timeline->arrival[0]) / steps;
		double expected;

		if (gap >= VRIK_TIMELINE_MIN_PERIOD && gap <= VRIK_TIMELINE_MAX_PERIOD)
			timeline->period += (gap - timeline->period) * VRIK_TIMELINE_PERIOD_GAIN;
		expected = timeline->time[0] + steps * timeline->period;
		if (arrival - expected > VRIK_TIMELINE_RESYNC || expected - arrival > VRIK_TIMELINE_RESYNC)
			timeline->count = 0; // a pause or a clock step: nothing to carry over
		else
			gridtime = expected + (arrival - expected) * VRIK_TIMELINE_GRID_GAIN;
	}
	else
		timeline->count = 0;

	for (i = VRIK_TIMELINE_SAMPLES - 1; i > 0; i--)
	{
		timeline->time[i] = timeline->time[i - 1];
		timeline->arrival[i] = timeline->arrival[i - 1];
	}
	timeline->time[0] = gridtime;
	timeline->arrival[0] = arrival;
	if (timeline->count < VRIK_TIMELINE_SAMPLES)
		timeline->count++;
}

/* Playback distance behind the sender grid. */
static inline double VRIK_TimelineDelay (const vrik_timeline_t *timeline)
{
	return timeline->period + VRIK_TIMELINE_SLACK;
}

/* Choose the two kept poses that straddle the playback time and the blend
from older (0) to newer (1). With a single pose, or outside the kept range,
both indices are the same and blend is 0. Returns false when there is nothing. */
static inline int VRIK_TimelineSample (const vrik_timeline_t *timeline, double now, int *older, int *newer, float *blend)
{
	double playtime;
	int	   i;

	if (timeline->count < 1)
		return 0;
	playtime = now - VRIK_TimelineDelay (timeline);
	if (timeline->count < 2 || playtime >= timeline->time[0])
	{
		*older = *newer = 0;
		*blend = 0;
		return 1;
	}
	for (i = 0; i + 1 < timeline->count; i++)
	{
		if (playtime >= timeline->time[i + 1])
		{
			double span = timeline->time[i] - timeline->time[i + 1];
			*older = i + 1;
			*newer = i;
			*blend = span > 0 ? (float)((playtime - timeline->time[i + 1]) / span) : 1.0f;
			return 1;
		}
	}
	*older = *newer = timeline->count - 1;
	*blend = 0;
	return 1;
}

#endif /* VRIK_INTERP_H */
