#include <assert.h>
#include <stdio.h>
#include "../Quake/vr_mdl_split.h"
static unsigned char *readfile (const char *path, size_t *size)
{
	FILE *f = fopen (path, "rb");
	assert (f);
	assert (!fseek (f, 0, SEEK_END));
	long n = ftell (f);
	assert (n > 0);
	rewind (f);
	unsigned char *b = malloc (n);
	assert (b);
	assert (fread (b, 1, n, f) == (size_t)n);
	fclose (f);
	*size = n;
	return b;
}
int main (int argc, char **argv)
{
	if (argc != 4)
		return 77;
	size_t		   n;
	unsigned char *b = readfile (argv[1], &n);
	for (int hand = 0; hand < 2; hand++)
	{
		unsigned char *out = NULL;
		size_t		   len = 0, glen;
		assert (QBJ3_MDL_Split (b, n, PERIL_MDL_WEAPON_SMG, hand, &out, &len) == 1);
		const char	  *gold = argv[hand + 2];
		unsigned char *g = readfile (gold, &glen);
		assert (len == glen && !memcmp (g, out, len));
		assert (QBJ3_MDL_ReadLE32 (out + 60) == 146);
		assert (QBJ3_MDL_ReadLE32 (out + 64) == 129);
		assert (QBJ3_MDL_ReadLE32 (out + 68) == 9);
		free (g);
		free (out);
	}
	unsigned char *out = (void *)1;
	size_t		   len = 1;
	assert (!QBJ3_MDL_Split (b, n - 1, PERIL_MDL_WEAPON_SMG, 0, &out, &len) && !out && !len);
	b[88] ^= 1;
	assert (!QBJ3_MDL_Split (b, n, PERIL_MDL_WEAPON_SMG, 0, &out, &len) && !out && !len);
	b[88] ^= 1;
	assert (!QBJ3_MDL_Split (b, n, PERIL_MDL_WEAPON_SMG, QBJ3_MDL_SIDE_DOMINANT, &out, &len) && !out && !len);
	free (b);
	puts ("PERIL_SPLIT_PASS independent oracle, all geometry/frames/skin preserved, corrupt/truncated/wrong-side rejected");
	return 0;
}
