/*
 * Editable SDF terrain: field, integer edit ops and a surface-nets mesher.
 * Engine-independent (stdint/libc only) so the server, the client and the
 * offline fixture share one implementation.
 *
 * This file is distributed under the same terms as the rest of the engine
 * (GPL-2.0-or-later).
 */
#ifndef TERRAIN_H
#define TERRAIN_H

#include <stdint.h>

#define TERRAIN_CELL		16 // Quake units per cell
#define TERRAIN_CHUNK		32 // cells per chunk edge
#define TERRAIN_CHUNK_SHIFT 5
#define TERRAIN_SUB			32 // fixed-point units per cell: field values, op centres and radii
#define TERRAIN_SDF_MAX		127
#define TERRAIN_CHUNK_CELLS (TERRAIN_CHUNK * TERRAIN_CHUNK * TERRAIN_CHUNK)
#define TERRAIN_CHUNK_UNITS (TERRAIN_CHUNK * TERRAIN_CELL)

enum
{
	TERRAIN_MAT_ROCK,
	TERRAIN_MAT_DIRT,
	TERRAIN_MAT_GOLD,
	TERRAIN_MAT_FILL,
	TERRAIN_MAT_COUNT
};

enum
{
	TERRAIN_OP_DIG,
	TERRAIN_OP_FILL
};

// Field values are signed distance in TERRAIN_SUB units, negative inside solid.
// A chunk with sdf == NULL is uniform (all solid or all open) and stores one value.
typedef struct
{
	int8_t	*sdf;
	uint8_t *mat;
	int8_t	 uniform_sdf;
	uint8_t	 dirty;
} terrain_chunk_t;

typedef struct
{
	int32_t			 origin[3]; // world position of sample (0,0,0), Quake units
	int32_t			 size[3];	// chunks per axis
	uint32_t		 seed;
	terrain_chunk_t *chunks;
} terrain_t;

// Centre and radius are in TERRAIN_SUB units relative to the region origin.
// Integer-only application so every peer replays an op to identical bytes.
typedef struct
{
	uint8_t	 kind;
	uint8_t	 material;
	uint16_t radius;
	int32_t	 center[3];
} terrain_op_t;

typedef struct
{
	float	pos[3];	   // world units
	float	normal[3]; // field gradient, points into open space
	uint8_t material;
	uint8_t ao; // 0 occluded .. 255 open
} terrain_vertex_t;

typedef struct
{
	terrain_vertex_t *verts;
	uint32_t		 *indices;
	int				  num_verts, num_indices;
	int				  cap_verts, cap_indices;
} terrain_mesh_t;

terrain_t *Terrain_Create (const int32_t origin[3], const int32_t size_chunks[3], uint32_t seed);
void	   Terrain_Free (terrain_t *t);
void	   Terrain_Generate (terrain_t *t); // carves the seed's cave with ordinary dig ops
int		   Terrain_ApplyOp (terrain_t *t, const terrain_op_t *op); // returns samples changed
int		   Terrain_Sample (const terrain_t *t, int x, int y, int z, uint8_t *mat); // lattice coordinates; outside is solid
float	   Terrain_SampleWorld (const terrain_t *t, const float p[3]); // trilinear distance in Quake units
uint64_t   Terrain_Hash (const terrain_t *t);
terrain_chunk_t *Terrain_Chunk (const terrain_t *t, int cx, int cy, int cz);
void	   Terrain_MeshChunk (const terrain_t *t, int cx, int cy, int cz, terrain_mesh_t *mesh);
void	   Terrain_MeshFree (terrain_mesh_t *mesh);

#endif
