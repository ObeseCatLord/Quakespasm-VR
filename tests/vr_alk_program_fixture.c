/* Admission of installed Alkaline/LimJam programs through the production pins. */
#include "../Quake/quakedef.h"
#include "../Quake/vr_melee_stock_qc.h"
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
	qs_sha256_t hash;
	unsigned short crc;
	const sv_vr_stock_axe_descriptor_t *descriptor;
	int function_offset, statement_offset, globals_offset, strings_offset;

	if (argc != 3 || !(file = fopen (argv[1], "rb")))
	{
		fprintf (stderr, "usage: %s /path/to/progs.dat alk|limjam\n", argv[0]);
		return 2;
	}
	assert (!strcmp (argv[2], "alk") || !strcmp (argv[2], "limjam"));
	assert (!fseek (file, 0, SEEK_END));
	bytes = ftell (file);
	assert (bytes > 60 && bytes < 4 * 1024 * 1024);
	rewind (file);
	code = malloc ((size_t)bytes);
	assert (code && fread (code, 1, (size_t)bytes, file) == (size_t)bytes);
	assert (!fclose (file));
	header.version = read_int (code);
	statement_offset = read_int (code + 8);
	header.numstatements = read_int (code + 12);
	function_offset = read_int (code + 32);
	header.numfunctions = read_int (code + 36);
	strings_offset = read_int (code + 40);
	header.numstrings = read_int (code + 44);
	globals_offset = read_int (code + 48);
	header.numglobals = read_int (code + 52);
	assert (header.version == PROG_VERSION && header.numfunctions > 5000 &&
		header.numfunctions < 10000 && header.numstatements > 70000 &&
		statement_offset >= 60 && function_offset >= 60 &&
		globals_offset >= 60 && strings_offset >= 60 &&
		(size_t)statement_offset + (size_t)header.numstatements * 8 <= (size_t)bytes &&
		(size_t)function_offset + (size_t)header.numfunctions * 36 <= (size_t)bytes &&
		(size_t)globals_offset + (size_t)header.numglobals * 4 <= (size_t)bytes &&
		(size_t)strings_offset + (size_t)header.numstrings <= (size_t)bytes);
	strings = (const char *)code + strings_offset;
	string_bytes = (size_t)header.numstrings;
	qcvm->progs = &header;
	qcvm->progssize = bytes;
	CRC_Init (&crc);
	for (long i = 0; i < bytes; ++i)
		CRC_ProcessByte (&crc, code[i]);
	qcvm->progscrc = crc;
	assert (crc == (!strcmp (argv[2], "alk") ? 30793 : 32416));
	qcvm->functions = calloc ((size_t)header.numfunctions, sizeof (dfunction_t));
	qcvm->statements = calloc ((size_t)header.numstatements, sizeof (dstatement_t));
	qcvm->globals = calloc ((size_t)header.numglobals, sizeof (float));
	assert (qcvm->functions && qcvm->statements && qcvm->globals);
	for (int i = 0; i < header.numfunctions; ++i)
	{
		const byte *row = code + function_offset + i * 36;
		dfunction_t *f = &qcvm->functions[i];
		f->first_statement = read_int (row);
		f->parm_start = read_int (row + 4);
		f->locals = read_int (row + 8);
		f->s_name = read_int (row + 16);
		f->numparms = read_int (row + 24);
		memcpy (f->parm_size, row + 28, MAX_PARMS);
	}
	for (int i = 0; i < header.numstatements; ++i)
		memcpy (&qcvm->statements[i], code + statement_offset + i * 8, 8);
	memcpy (qcvm->globals, code + globals_offset,
		(size_t)header.numglobals * 4);
	QS_SHA256Init (&hash);
	QS_SHA256Update (&hash, code, (size_t)bytes);
	QS_SHA256Final (&hash, qcvm->progssha256);
	descriptor = SV_VRStockAxeMeleeDescriptor ();
	assert (descriptor && descriptor->alkaline);
	assert (descriptor->trace_statement == (!strcmp (argv[2], "alk") ? 12167 : 13017));
	qcvm->progssha256[0] ^= 1;
	assert (!SV_VRStockAxeMeleeDescriptor ());
	qcvm->progssha256[0] ^= 1;
	qcvm->statements[descriptor->trace_statement].op = OP_DONE;
	assert (!SV_VRStockAxeMeleeDescriptor ());
	puts ("Alkaline family exact program admission passed");
	free (qcvm->globals);
	free (qcvm->statements);
	free (qcvm->functions);
	free (code);
	return 0;
}
