/*
 * snd_sdl.c - SDL audio driver for Hexen II: Hammer of Thyrion (uHexen2)
 * based on implementations found in the quakeforge and ioquake3 projects.
 *
 * Copyright (C) 1999-2005 Id Software, Inc.
 * Copyright (C) 2005-2012 O.Sezer <sezero@users.sourceforge.net>
 * Copyright (C) 2010-2014 QuakeSpasm developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"
#include "voice.h"

#ifdef USE_SDL3

static int				buffersize;
static SDL_AudioStream *audio_stream = NULL;

static void SDLCALL paint_audio (void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
	int pos, remaining, spans;
	Uint8 mixed[8192];

	if (!shm || !shm->buffer || buffersize <= 0 || additional_amount <= 0)
		return;

	pos = (shm->samplepos * (shm->samplebits / 8));
	if (pos >= buffersize)
		shm->samplepos = pos = 0;

	/* SDL can request more than one ring's worth after an underrun.  Copy
	 * each span separately so neither the first nor a later wrap reads past
	 * the DMA allocation. */
	remaining = additional_amount;
	for (spans = 0; remaining > 0 && spans < 4; ++spans)
	{
		int len = q_min (remaining, buffersize - pos);
		int span_remaining = len;
		while (span_remaining > 0)
		{
			int chunk = q_min (span_remaining, (int)sizeof (mixed));
			int frame_bytes = shm->channels * (shm->samplebits / 8);
			if (frame_bytes > 0)
				chunk -= chunk % frame_bytes;
			if (chunk <= 0)
				break;
			memcpy (mixed, shm->buffer + pos, chunk);
			Voice_MixAudio (mixed, chunk, shm->samplebits, shm->channels,
				shm->speed, shm->signed8);
			if (!SDL_PutAudioStreamData (stream, mixed, chunk))
			{
				shm->samplepos = pos / (shm->samplebits / 8);
				return;
			}
			remaining -= chunk;
			span_remaining -= chunk;
			pos += chunk;
		}
		if (span_remaining > 0)
			break;
		if (pos >= buffersize)
			pos = 0;
	}
	shm->samplepos = pos / (shm->samplebits / 8);
}

qboolean SNDDMA_Init (dma_t *dma)
{
	SDL_AudioSpec spec;
	char		  drivername[128];

	if (!SDL_InitSubSystem (SDL_INIT_AUDIO))
	{
		Con_Printf ("Couldn't init SDL audio: %s\n", SDL_GetError ());
		return false;
	}

	/* Set up the desired format */
	spec.freq = snd_mixspeed.value;
	spec.format = (loadas8bit.value) ? SDL_AUDIO_U8 : SDL_AUDIO_S16;
	spec.channels = 2;

	/* Open the audio device with callback */
	audio_stream = SDL_OpenAudioDeviceStream (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, paint_audio, NULL);
	if (!audio_stream)
	{
		Con_Printf ("Couldn't open SDL audio: %s\n", SDL_GetError ());
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		return false;
	}

	memset ((void *)dma, 0, sizeof (dma_t));
	shm = dma;

	/* Fill the audio DMA information block */
	shm->samplebits = SDL_AUDIO_BITSIZE (spec.format);
	shm->signed8 = (spec.format == SDL_AUDIO_S8);
	shm->speed = spec.freq;
	shm->channels = spec.channels;

	/* Calculate buffer size - aim for ~100ms of audio */
	int num_samples = (spec.channels * spec.freq) / 10;
	num_samples = Q_nextPow2 (num_samples);

	shm->samples = num_samples;
	shm->samplepos = 0;
	shm->submission_chunk = 1;

	Con_Printf ("SDL audio spec  : %d Hz, %d channels\n", spec.freq, spec.channels);
	{
		const char *driver = SDL_GetCurrentAudioDriver ();
		const char *device = SDL_GetAudioDeviceName (SDL_GetAudioStreamDevice (audio_stream));
		q_snprintf (drivername, sizeof (drivername), "%s - %s", driver != NULL ? driver : "(UNKNOWN)", device != NULL ? device : "(UNKNOWN)");
	}
	buffersize = shm->samples * (shm->samplebits / 8);
	Con_Printf ("SDL audio driver: %s, %d bytes buffer\n", drivername, buffersize);

	shm->buffer = (unsigned char *)Mem_Alloc (buffersize);
	if (!shm->buffer)
	{
		SDL_DestroyAudioStream (audio_stream);
		audio_stream = NULL;
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		shm = NULL;
		Con_Printf ("Failed allocating memory for SDL audio\n");
		return false;
	}

	SDL_ResumeAudioStreamDevice (audio_stream);

	Con_Printf ("SDL audio initialized: samples=%d, samplebits=%d, channels=%d\n", shm->samples, shm->samplebits, shm->channels);

	return true;
}

int SNDDMA_GetDMAPos (void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown (void)
{
	if (shm)
	{
		Con_Printf ("Shutting down SDL sound\n");
		SDL_DestroyAudioStream (audio_stream);
		audio_stream = NULL;
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		if (shm->buffer)
			Mem_Free (shm->buffer);
		shm->buffer = NULL;
		shm = NULL;
	}
}

void SNDDMA_LockBuffer (void)
{
	if (audio_stream)
		SDL_LockAudioStream (audio_stream);
}

void SNDDMA_Submit (void)
{
	/* In callback model, unlock is all we need to do */
	if (audio_stream)
		SDL_UnlockAudioStream (audio_stream);
}

void SNDDMA_BlockSound (void)
{
	if (audio_stream)
		SDL_PauseAudioStreamDevice (audio_stream);
}

void SNDDMA_UnblockSound (void)
{
	if (audio_stream)
		SDL_ResumeAudioStreamDevice (audio_stream);
}

#endif
