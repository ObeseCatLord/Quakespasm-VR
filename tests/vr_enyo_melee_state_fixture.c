/* Production program admission/readiness against the installed, original-index
 * Enyo function table. This does not execute QC damage or server sweeps. */
#include "../Quake/sv_phys.c"
#include "../Quake/sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

server_t sv;
qcvm_t *qcvm = &sv.qcvm;

static const char *strings;
static size_t string_bytes;

const char *PR_GetString (int offset)
{
	assert (offset >= 0 && (size_t)offset < string_bytes);
	assert (memchr (strings + offset, 0, string_bytes - offset));
	return strings + offset;
}

dfunction_t *ED_FindFunction (const char *name)
{
	for (int i = 0; i < qcvm->progs->numfunctions; ++i)
		if (!strcmp (PR_GetString (qcvm->functions[i].s_name), name))
			return &qcvm->functions[i];
	return NULL;
}

static int read_int (const byte *bytes)
{
	return (int32_t)((uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
		(uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24);
}

int main (int argc, char **argv)
{
	byte *code;
	long bytes;
	FILE *file;
	dprograms_t header = {0};
	edict_t player = {0};
	qs_sha256_t hash;
	client_t *client;
	int functions_offset, strings_offset;

	if (argc != 2 || !(file = fopen (argv[1], "rb")))
	{
		fprintf (stderr, "usage: %s /path/to/installed-enyo-progs.dat\n", argv[0]);
		return 2;
	}
	assert (!fseek (file, 0, SEEK_END));
	bytes = ftell (file);
	assert (bytes >= 60 && bytes < 4 * 1024 * 1024);
	rewind (file);
	code = malloc ((size_t)bytes);
	assert (code && fread (code, 1, (size_t)bytes, file) == (size_t)bytes);
	assert (!fclose (file));
	header.version = read_int (code);
	header.numstatements = read_int (code + 12);
	functions_offset = read_int (code + 32);
	header.numfunctions = read_int (code + 36);
	strings_offset = read_int (code + 40);
	header.numstrings = read_int (code + 44);
	header.numglobals = read_int (code + 52);
	assert (header.version == PROG_VERSION && header.numfunctions > 572 &&
		header.numfunctions < 10000 && functions_offset >= 60 &&
		(size_t)functions_offset + (size_t)header.numfunctions * 36 <= (size_t)bytes);
	assert (strings_offset >= 60 && header.numstrings > 0 &&
		(size_t)strings_offset + (size_t)header.numstrings <= (size_t)bytes);
	strings = (const char *)code + strings_offset;
	string_bytes = (size_t)header.numstrings;
	qcvm->progs = &header;
	qcvm->progssize = bytes;
	qcvm->functions = calloc ((size_t)header.numfunctions, sizeof (dfunction_t));
	assert (qcvm->functions);
	for (int i = 0; i < header.numfunctions; ++i)
	{
		const byte *row = code + functions_offset + i * 36;
		dfunction_t *f = &qcvm->functions[i];
		f->first_statement = read_int (row);
		f->parm_start = read_int (row + 4);
		f->locals = read_int (row + 8);
		f->s_name = read_int (row + 16);
		f->s_file = read_int (row + 20);
		f->numparms = read_int (row + 24);
		memcpy (f->parm_size, row + 28, MAX_PARMS);
	}
	QS_SHA256Init (&hash);
	QS_SHA256Update (&hash, code, (size_t)bytes);
	QS_SHA256Final (&hash, qcvm->progssha256);
	strcpy (com_gamedir, "enyo");
	assert (SV_VREnyoMeleeContactProfile () == VR_WEAPON_CONTACT_PROFILE_ENYO);
	qcvm->progssha256[0] ^= 1;
	assert (SV_VREnyoMeleeContactProfile () == VR_WEAPON_CONTACT_PROFILE_NONE);
	qcvm->progssha256[0] ^= 1;
	qcvm->functions[347].parm_size[1] = 1;
	assert (!SV_EnyoMeleeProgramLoaded ());
	qcvm->functions[347].parm_size[1] = 3;
	strcpy (com_gamedir, "id1");
	assert (!SV_EnyoMeleeProgramLoaded ());
	strcpy (com_gamedir, "enyo");
	assert (SV_EnyoMeleeProgramLoaded ());

	/* Positive times include overdue native attacks; neither may be stolen.
	 * The genuine hit aftermath remains callable without adding recovery. */
	qcvm->time = 5;
	for (int due = 0; due < 2; ++due)
	{
		player.v.nextthink = due ? .1f : 10.0f;
		for (int think = 551; think <= 572; ++think)
		{
			player.v.think = think;
			assert (SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD) ==
				(think >= 560));
		}
		for (int think = 506; think <= 507; ++think)
		{
			player.v.think = think;
			assert (SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD));
		}
	}
	player.v.think = 568;
	qcvm->functions[568].first_statement++;
	assert (!SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD));
	qcvm->functions[568].first_statement--;
	player.v.think = 551;
	player.v.nextthink = 0;
	assert (SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD));
	player.v.nextthink = -1;
	assert (SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD));
	player.v.nextthink = NAN;
	assert (!SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD));
	player.v.nextthink = 10;
	player.v.think = 404;
	assert (!SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_ENYO_SWORD));

	/* The shared owner must retain QBJ3's stricter terminal-draw policy. */
	player.v.think = 575;
	player.v.weaponframe = 9;
	assert (!SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_QBJ3_WRENCH));
	player.v.weaponframe = 10;
	assert (SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_QBJ3_WRENCH));
	player.v.think = 565;
	player.v.nextthink = 0;
	assert (!SV_VRDirectMeleeIdle (&player, SV_VR_DIRECT_MELEE_QBJ3_BERSERK));
	client = calloc (1, sizeof (*client));
	assert (client);
	client->private_vr_direct_melee_authorized[0] = true;
	client->private_vr_direct_melee_subtype[0] = SV_VR_DIRECT_MELEE_ENYO_SWORD;
	client->private_vr_direct_melee_hit_count[0] = 2;
	client->private_vr_direct_melee_deadline[0] = 12;
	SV_ResetPrivateVRDirectMeleeStroke (client, 0);
	assert (!client->private_vr_direct_melee_authorized[0] &&
		client->private_vr_direct_melee_subtype[0] == SV_VR_DIRECT_MELEE_NONE &&
		client->private_vr_direct_melee_hit_count[0] == 0 &&
		client->private_vr_direct_melee_deadline[0] == 0);
	free (client);
	free (qcvm->functions);
	free (code);
	puts ("Enyo program pins, native attack/aftermath readiness and QBJ3 reset regression passed");
	return 0;
}
