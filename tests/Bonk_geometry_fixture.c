/* Actual installed bytes -> production held recipe and existing guarded splitter.
 * Arguments are extracted privately by Bonk_verify.py, never committed assets. */
#include "../Quake/quakedef.h"
#include "../Quake/vr_mdl_split.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void Host_Error (const char *text, ...) { (void)text; abort (); }
void Sys_Error (const char *text, ...) { (void)text; abort (); }
void Con_Printf (const char *text, ...) { (void)text; }

int main (int argc, char **argv)
{
	assert (argc == 30);
	q_strlcpy (com_gamedir, "fixture/bonkjam", sizeof com_gamedir);
	for (int i = 1; i < argc; ++i)
	{
		const char *name = strstr (argv[i], "progs/");
		assert (name);
		const mod_held_melee_recipe_t *recipe = Mod_GetHeldMeleeRecipe (name);
		assert (recipe && recipe->contact_profile == VR_WEAPON_CONTACT_PROFILE_BONK);
		FILE *file = fopen (argv[i], "rb");
		assert (file && !fseek (file, 0, SEEK_END));
		long size = ftell (file);
		assert (size > 84 && !fseek (file, 0, SEEK_SET));
		byte *source = malloc (size), *mesh = NULL;
		assert (source && fread (source, size, 1, file) == 1);
		fclose (file);
		size_t generated = 0;
		assert (QBJ3_MDL_Split (source, size, recipe->split_weapon, QBJ3_MDL_SIDE_RIGHT, &mesh, &generated) == 1);
		assert (generated == recipe->generated_size && QBJ3_MDL_CRC32 (mesh, generated) == recipe->generated_crc);
		assert (QBJ3_MDL_ReadLE32 (source + 60) == (unsigned)recipe->source_vertices);
		assert (QBJ3_MDL_ReadLE32 (source + 64) == (unsigned)recipe->source_triangles);
		assert (QBJ3_MDL_ReadLE32 (mesh + 60) == (unsigned)recipe->vertices);
		assert (QBJ3_MDL_ReadLE32 (mesh + 64) == (unsigned)recipe->triangles);
		assert (QBJ3_MDL_ReadLE32 (mesh + 68) == 255 && recipe->frames == 255 && recipe->ready_frame == 0);
		assert (!recipe->centered_grip && !recipe->authored_left && recipe->controller_roll == 0);
		size_t frame = 88 + QBJ3_MDL_ReadLE32 (mesh + 52) * QBJ3_MDL_ReadLE32 (mesh + 56) + recipe->vertices * 12 + recipe->triangles * 16;
		vec3_t edge[2];
		for (int end = 0; end < 2; ++end)
		{
			assert (recipe->edge_vertices[end] >= 0 && recipe->edge_vertices[end] < recipe->vertices);
			for (int axis = 0; axis < 3; ++axis)
			{
				float scale, origin;
				memcpy (&scale, mesh + 8 + axis * 4, 4);
				memcpy (&origin, mesh + 20 + axis * 4, 4);
				edge[end][axis] = mesh[frame + 28 + recipe->edge_vertices[end] * 4 + axis] * scale + origin;
				assert (isfinite (edge[end][axis]) && recipe->grip_raw[axis] == 0);
			}
		}
		assert (sqrtf ((edge[0][0]-edge[1][0])*(edge[0][0]-edge[1][0]) + (edge[0][1]-edge[1][1])*(edge[0][1]-edge[1][1]) + (edge[0][2]-edge[1][2])*(edge[0][2]-edge[1][2])) > .001f);
		source[0] ^= 1; // one-byte model mutation must fail identity even with same dimensions
		byte *bad = NULL;
		size_t badsize = 0;
		assert (QBJ3_MDL_Split (source, size, recipe->split_weapon, QBJ3_MDL_SIDE_RIGHT, &bad, &badsize) == 0);
		assert (!bad);
		free (mesh);
		free (source);
	}
	q_strlcpy (com_gamedir, "fixture/id1", sizeof com_gamedir);
	assert (!Mod_GetHeldMeleeRecipe ("progs/v_hammer_default.mdl"));
	puts ("BONK_GEOMETRY_29_IDENTITIES_READY_EDGES_AND_MUTATION_PASSED");
	return 0;
}
