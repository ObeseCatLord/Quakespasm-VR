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
	TERRAIN_MAT_GINGER, // platform gun disks
	TERRAIN_MAT_COUNT
};

enum
{
	TERRAIN_OP_DIG,
	TERRAIN_OP_FILL,
	TERRAIN_OP_DISK,   // fills a flat disk: radius across, thickness through, facing normal
	TERRAIN_OP_DETACH, // terrain_ice.c: remove the small solid island holding a lattice cell (center = cell, radius = cell cap)
	TERRAIN_OP_STAMP,  // terrain_ice.c: set a detached brick back into the field (radius = brick id, center = pose position, aux = pose)
	TERRAIN_OP_KINDS
};
#define TERRAIN_OP_IS_ICE(kind) ((kind) == TERRAIN_OP_DETACH || (kind) == TERRAIN_OP_STAMP)

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
	float			 walk_normal; // least floor normal z that counts as ground on this terrain (set by the server, sent to clients)
	terrain_chunk_t *chunks;
	void			*ice; // bricks of detached chunks (terrain_ice.c)
} terrain_t;

// Centre and radius are in TERRAIN_SUB units relative to the region origin.
// Integer-only application so every peer replays an op to identical bytes.
typedef struct
{
	uint8_t	 kind;
	uint8_t	 material;
	uint16_t radius;
	int32_t	 center[3];
	int8_t	 normal[3]; // TERRAIN_OP_DISK: the disk's axis, any length up to 127
	uint8_t	 thickness; // TERRAIN_OP_DISK: half the disk's thickness, TERRAIN_SUB units
	uint32_t aux;		// TERRAIN_OP_STAMP: packed orientation (Terrain_PackQuat); on the wire it takes the bytes of normal and thickness
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
int		   Terrain_GenerateIce (terrain_t *t); // optional: ice pillars and icicles grown with fill ops; returns how many
int		   Terrain_ApplyOp (terrain_t *t, const terrain_op_t *op); // returns samples changed
int		   Terrain_Sample (const terrain_t *t, int x, int y, int z, uint8_t *mat); // lattice coordinates; outside is solid
float	   Terrain_SampleWorld (const terrain_t *t, const float p[3]); // trilinear distance in Quake units
uint64_t   Terrain_Hash (const terrain_t *t);
terrain_chunk_t *Terrain_Chunk (const terrain_t *t, int cx, int cy, int cz);
void	   Terrain_SetSample (terrain_t *t, int x, int y, int z, int sdf, int mat); // mat < 0 keeps the material; marks meshes dirty
void	   Terrain_MeshChunk (const terrain_t *t, int cx, int cy, int cz, terrain_mesh_t *mesh);
void	   Terrain_MeshFree (terrain_mesh_t *mesh);

/*
 * Falling ice (terrain_ice.c). A DETACH op removes a small solid island from the field and keeps its
 * samples as a brick: a little field of its own, origin 0, lattice spacing TERRAIN_CELL, with air
 * around it. A STAMP op later sets the brick back at a quantized pose. Both replay exactly from the
 * op log, because the brick table is rebuilt by replaying DETACH ops in order.
 */
typedef struct
{
	terrain_t *field;	   // brick frame; sample (i,j,k) sits at TERRAIN_CELL * (i,j,k)
	int		   lat_min[3]; // world lattice coordinate of brick sample (0,0,0)
	int		   dim[3];	   // lattice samples used per axis
	int		   cells;	   // solid samples
	float	   mass;	   // sum of sample weights (a full solid sample is 1)
	float	   com[3];	   // centre of mass, brick frame
	float	   inertia[9]; // about com, brick frame, unit density
	float	   mins[3], maxs[3]; // box round the solid, brick frame
	float	   radius;	   // from com to the farthest solid sample
} terrain_brick_t;

const terrain_brick_t *Terrain_Brick (const terrain_t *t, int id);
int	   Terrain_BrickCount (const terrain_t *t);
int	   Terrain_IceApplyOp (terrain_t *t, const terrain_op_t *op); // DETACH and STAMP; called by Terrain_ApplyOp
int	   Terrain_IceLastBrick (const terrain_t *t);				  // id made by the latest DETACH, or -1
int	   Terrain_IceFindIslands (const terrain_t *t, const terrain_op_t *dig, terrain_op_t *out, int max_out); // DETACH ops for what a dig cut loose
void   Terrain_IceOpBounds (const terrain_t *t, const terrain_op_t *op, int lo[3], int hi[3]); // lattice box a DETACH or STAMP may change
void   Terrain_IceFree (terrain_t *t);
uint32_t Terrain_PackQuat (const float q[4]); // w x y z, 10 bits per kept component
void   Terrain_UnpackQuat (uint32_t v, float q[4]);
void   Terrain_QuatMatrix (const float q[4], float m[9]); // row-major, body to world

typedef struct
{
	float fraction;	 // 0..1 along the move; 1 when nothing was hit
	float normal[3]; // plane the box stopped against, facing the box
	int	  startsolid; // 1: a zero-length test overlapped the surface; 2: the box centre began inside solid
} terrain_hit_t;

// Sweeps an axis-aligned box (mins/maxs relative to the moving origin) through the terrain.
// Returns nonzero when the move is blocked or starts solid.
int Terrain_TraceBox (const terrain_t *t, const float start[3], const float mins[3], const float maxs[3], const float end[3], terrain_hit_t *hit);
int Terrain_BoxOverlaps (const terrain_t *t, const float origin[3], const float mins[3], const float maxs[3]);
int Terrain_PointSolid (const terrain_t *t, const float p[3]);

// The one terrain region the running game collides with, or NULL. Owned by the client module for now.
// Walking navigation grid over a region (see terrain.c). Cells are TERRAIN_NAV_CELL units.
#define TERRAIN_NAV_CELL	 32
#define TERRAIN_NAV_CLASSES	 2 // 0: a body up to a player's size; 1: wide and tall (shambler)
#define TERRAIN_NAV_FAR		 0xffff
#define TERRAIN_NAV_MAX_DROP 4 // cells a body may drop off a ledge (one way)
#define TERRAIN_NAV_MAX_CLIMB 4 // most levels max_climb may allow
enum
{
	TERRAIN_NAV_AIR = 1,	 // centre is open
	TERRAIN_NAV_BLOCKED = 2, // closed by something other than the field (set by the caller)
	TERRAIN_NAV_STAND = 4,	 // shifted left by the class: a body of that class can stand here
};

typedef struct
{
	int32_t	 origin[3];
	int		 dim[3], count;
	uint8_t	 *flags;
	int		  max_climb; // levels a body may jump up onto a ledge; 0 or 1: none (set by the caller)
	uint64_t *heap;		 // flood scratch
	int		  heap_len, heap_cap;
} terrain_nav_t;

void Terrain_NavInit (terrain_nav_t *nav, const terrain_t *t); // allocates; flags start clear
void Terrain_NavFree (terrain_nav_t *nav);
void Terrain_NavRefresh (terrain_nav_t *nav, const terrain_t *t, const int lo[3], const int hi[3]); // inclusive cell box
void Terrain_NavRefreshAll (terrain_nav_t *nav, const terrain_t *t);
void Terrain_NavOpBox (const terrain_nav_t *nav, const terrain_t *t, const terrain_op_t *op, int lo[3], int hi[3]); // the cells an op can change (unclamped; ice ops: clamped)
void Terrain_NavRefreshOp (terrain_nav_t *nav, const terrain_t *t, const terrain_op_t *op); // after Terrain_ApplyOp
void Terrain_NavSetBlocked (terrain_nav_t *nav, int cell, int blocked);						// takes effect on the next refresh
void Terrain_NavCellCenter (const terrain_nav_t *nav, int cell, float out[3]);
int	 Terrain_NavFindStand (const terrain_nav_t *nav, int cls, const float feet[3], int up, int down); // -1 when none near
void Terrain_NavFlood (terrain_nav_t *nav, int cls, const int *seeds, int num_seeds, uint16_t *dist); // the whole grid

// A distance field that can be flooded only around its target: a near flood settles cells up to a
// cost and clears just the cells its last flood reached, so it is cheap enough to redo every time
// the cave or the target changes. Cells beyond the bound stay TERRAIN_NAV_FAR.
typedef struct
{
	uint16_t *dist;
	int		 *touched;
	int		  num_touched, cap_touched;
} terrain_navfield_t;
void Terrain_NavFieldInit (const terrain_nav_t *nav, terrain_navfield_t *field);
void Terrain_NavFieldFree (terrain_navfield_t *field);
void Terrain_NavFloodNear (terrain_nav_t *nav, int cls, const int *seeds, int num_seeds, terrain_navfield_t *field, int max_cost);
int	 Terrain_NavNext (const terrain_nav_t *nav, int cls, const uint16_t *dist, int cell); // one step closer, or -1
int	 Terrain_NavIsDrop (const terrain_nav_t *nav, int from, int to);					  // that step is off a ledge
int	 Terrain_NavIsClimb (const terrain_nav_t *nav, int from, int to);					  // that step is a jump up onto a ledge
int	 Terrain_NavStepCost (const terrain_nav_t *nav, int from, int to);

// Op wire form (netsync): kind, material, radius, centre, then normal and thickness (DISK) or aux
// (STAMP) in the last four bytes, little-endian.
#define TERRAIN_OP_BYTES 20
void Terrain_OpPack (const terrain_op_t *op, uint8_t out[TERRAIN_OP_BYTES]);
void Terrain_OpUnpack (const uint8_t in[TERRAIN_OP_BYTES], terrain_op_t *op);

extern terrain_t *terrain_active; // the server's region
extern unsigned	  terrain_epoch; // changes whenever terrain_active is replaced or freed
extern terrain_t *terrain_client;		// the client's copy, built from the server's seed and op log (cl_terrain.c); what is drawn
extern unsigned	  terrain_client_epoch; // changes whenever terrain_client is replaced or freed
extern terrain_t *terrain_pmove;		// whichever of the two the running player move collides with
float			  Terrain_PmoveWalkNormal (void);
void			  CL_Terrain_Init (void);
void			  CL_Terrain_Clear (void);
void			  CL_Workshop_Init (void);
void			  CL_Workshop_Clear (void);
void			  CL_Workshop_AddEntities (void); // cl_workshop.c: light strings, VR wrist flares and holster
struct client_s;
void SV_Terrain_SendToClient (struct client_s *client);

/* Server side (sv_terrain.c). Engine code only; the offline fixture does not link these. */
struct edict_s;
void SV_Terrain_Init (void);
void SV_Terrain_NewMap (void);
void SV_Terrain_PlaceEntities (void);
void SV_Terrain_Clear (void);
void SV_Terrain_SpawnAt (const float center[3], uint32_t seed, const int32_t size[3]);
int	 SV_Terrain_Edit (int kind, const float pos[3], float radius);
int	 SV_Terrain_Disk (const float pos[3], const float normal[3], float radius, float half_thickness); // gingerbread
int	 SV_Terrain_OpLog (const terrain_op_t **ops);
void SV_Terrain_PlayerFrame (struct edict_s *ent, int num);
int	 SV_Terrain_ThrowFlare (struct edict_s *player, const float origin[3], const float velocity[3]); // 0: no flare model on this map

/* The workshop's tools (sv_workshop.c): platform gun, festive grapple, thrown flares. */
void	   SV_Workshop_Init (void);
void	   SV_Workshop_Precache (void); // while a drg* map loads
void	   SV_Workshop_Clear (void);
void SV_Workshop_LoadingSave (void); // a saved game is being loaded over this map
void	   SV_Workshop_PlayerFrame (struct edict_s *ent, int num);										// once a frame, before the player's commands
void	   SV_Workshop_PreThink (struct edict_s *ent);													// per command, right before PlayerPreThink
void	   SV_Workshop_Frame (void);																	// once a server frame, after physics
int		   SV_Workshop_ViewModel (struct client_s *client, int *modelindex, float *frame, float *ammo); // 1 when a tool replaces the weapon on screen
void SV_Terrain_SniffWrite (int dest, int kind, float value);
int		   SV_Terrain_BoxInBrushes (const float origin[3], const float mins[3], const float maxs[3]); // corners or centre inside the map's solid
float SV_Terrain_WalkNormal (void); // least floor normal z that counts as ground; steeper on terrain than BSP's 0.7
int	 SV_Terrain_CrawlerFrame (struct edict_s *ent); // 1 when the edict is a surface crawler and was moved here
int	 SV_Terrain_IsCrawler (struct edict_s *ent);
int	 SV_Terrain_RelaxedSteps (void);
int		   SV_Terrain_MoveToGoal (struct edict_s *ent, struct edict_s *goal, float dist); // 1 when it stepped the monster along a route
extern int sv_terrain_step_stats[6]; // monster steps: ok, blocked, edge, bad footing
void SV_Terrain_WormInit (void);
void SV_Terrain_WormReset (void);
void SV_Terrain_WormMapLoad (const float start[3]); // precache; spawn one when terrain_worm is set
void SV_Terrain_WormFrame (void);					// once per server frame, before entities run
int	 SV_Terrain_WormPoints (float (*out)[3], int max); // segment centres head first, 0 when there is no worm (debug view)
void SV_Terrain_IceInit (void);
void SV_Terrain_IceReset (void);
void SV_Terrain_IceFrame (void);							   // once per server frame
void SV_Terrain_IceEdited (const terrain_op_t *op);			   // after a logged edit: detach what it cut loose
int	 SV_Terrain_PushOp (const terrain_op_t *op);			   // applies and logs any op kind (SV_Terrain_Edit builds sphere ops on it)
int	 SV_Terrain_IceDrawCount (void);
int	 SV_Terrain_IceDraw (int i, const terrain_mesh_t **mesh, float pos[3], float rot[9], float com[3]); // interpolated pose of a live body
void SV_Terrain_LavaInit (void);
void SV_Terrain_LavaReset (void);
void SV_Terrain_LavaFrame (void); // once per server frame, after the worm
typedef struct // read-only view of the lava particles for drawing (listen server only)
{
	int			 count;
	const float *pos[3], *heat;
	float		 radius;
	int			 num_lights;
	const float (*lights)[4]; // x y z radius
} terrain_lava_view_t;
const terrain_lava_view_t *SV_Terrain_LavaView (void); // NULL when nothing to draw
void R_Terrain_Update (void);
void	   R_Terrain_FinishMeshing (void); // waits for chunks meshing on worker threads; call before changing or freeing terrain_client

#endif
