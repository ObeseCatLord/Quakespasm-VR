/*
Copyright (C) 1996-1997 Id Software, Inc.

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

#ifndef QUAKE_PMOVE_H
#define QUAKE_PMOVE_H

#include "quakedef.h"
#include "vr_gorilla_types.h"

#ifndef CONTENTS_LADDER
#define CONTENTS_LADDER -16
#endif

#ifndef CONTENTMASK_FROMQ1
#define CONTENTMASK_FROMQ1(c) (1u << (-(c)))
#endif

#ifndef CONTENTMASK_ANYSOLID
#define CONTENTMASK_ANYSOLID (CONTENTMASK_FROMQ1(CONTENTS_SOLID) | CONTENTMASK_FROMQ1(CONTENTS_CLIP))
#endif

#define BUTTON_ATTACK 1
#define BUTTON_JUMP 2

typedef enum {
	PM_NORMAL,			// normal ground movement
	PM_OLD_SPECTATOR,	// fly, no clip to world (QW bug)
	PM_SPECTATOR,		// fly, no clip to world
	PM_DEAD,			// no acceleration
	PM_FLY,				// fly, bump into walls
	PM_NONE,			// can't move
	PM_FREEZE,			// can't move or look around (TODO)
	PM_WALLWALK,		// sticks to walls. on ground while near one
	PM_6DOF				// spaceship mode
} pmtype_t;

#define PMF_JUMP_HELD			1
#define PMF_LADDER				2	//pmove flags. seperate from flags

#define	MAX_PHYSENTS	64//2048
typedef struct
{
	vec3_t	origin;
	vec3_t	angles;
	qmodel_t	*model;		// only for bsp models
	vec3_t	mins, maxs;	// only for non-bsp models
	int	info;		// for client or server to identify
	unsigned int modelindex; // brush identity for optional local hand anchors
	unsigned int forcecontentsmask;	//set from .skin
} physent_t;

typedef struct
{
	// player state
	vec3_t		origin;
	vec3_t		safeorigin;	//valid when safeorigin_known. needed for extrasr4's ladders otherwise they bug out.
	vec3_t		angles;
	vec3_t		velocity;
	vec3_t		gravitydir;
	qboolean		jump_held;
	/* QuakeC owns jump/takeoff and release state across commands. */
	qboolean		qc_jump_owner;
	float			jump_secs;	// msec since last jump
	float		waterjumptime;
	/* Server-only QuakeC teleport deadline: native AirMove ignores back input. */
	qboolean		block_teleport_backmove;
	/* Selected server evaluated instant stop before QuakeC callbacks. */
	qboolean		vr_instant_stop_preapplied;
	int			pm_type;
	vec3_t		player_mins;
	vec3_t		player_maxs;

	// world state
	int			numphysent;
	physent_t	physents[MAX_PHYSENTS];	// 0 should be the world

	// input
	usercmd_t	cmd;
	/* Restored from the same accepted-command baseline as origin/velocity. */
	vr_gorilla_state_t gorilla;
	qboolean gorilla_allowed;
	qboolean gorilla_braced;
	qboolean gorilla_swim_stroke;
	qboolean gorilla_prepared; /* enclosing QC owner already consumed this pose */
	int gorilla_contact[2];
	/* Author only the hand contribution, before the native movement step. */
	qboolean gorilla_authoring;
	vr_gorilla_motion_t gorilla_authored_motion;

	qboolean onladder;
	qboolean safeorigin_known;

	// results
	int			skipent;
	int			numtouch;
	int			touchindex[MAX_PHYSENTS];
	vec3_t		touchvel[MAX_PHYSENTS];
	qboolean		onground;
	int			groundent;		// index in physents array, only valid
								// when onground is true
	int			waterlevel;
	int			watertype;
	qboolean		fluid_contacted; /* latched across command substeps for replay policy */

	struct world_s		*world;
} playermove_t;

typedef struct {
	//standard quakeworld
	float gravity;
	float stopspeed;
	float maxspeed;
	float spectatormaxspeed;
	float maxairspeed;
	float accelerate;
	float airaccelerate;
	float wateraccelerate;
	float friction;
	float waterfriction;
	float flyfriction;
	float entgravity;

	//extended stuff, sent via serverinfo
	float bunnyspeedcap;
	float watersinkspeed;
	float ktjump;
	float edgefriction; //default 2
	float jumpspeed;
	int	walljump;
	qboolean slidefix;
	qboolean airstep;
	qboolean pground;
	qboolean stepdown;
	qboolean slidyslopes;
	qboolean autobunny;
	qboolean bunnyfriction;	//force at least one frame of friction when bunnying.
	int stepheight;

	unsigned protocolflags;

	unsigned int	flags;
} movevars_t;

#define MOVEFLAG_VALID							0x80000000	//to signal that these are actually known. otherwise reserved.
//#define MOVEFLAG_Q2AIRACCELERATE				0x00000001
#define MOVEFLAG_NOGRAVITYONGROUND				0x00000002	//no slope sliding
//#define MOVEFLAG_GRAVITYUNAFFECTEDBYTICRATE	0x00000004	//apply half-gravity both before AND after the move, which better matches the curve
#define MOVEFLAG_QWEDGEBOX						0x00010000	//calculate edgefriction using tracebox and a buggy start pos
#define MOVEFLAG_USEAIRACCEL					0x00020000	//the vanilla qw pmove code used movevars.accelerate and NEVER movevars.airaccelerate, but different defaults make fixing this awkward.
#define MOVEFLAG_PM_SLIDEFIX					0x00040000
#define MOVEFLAG_PM_AIRSTEP						0x00080000
#define MOVEFLAG_PM_PGROUND						0x00100000
#define MOVEFLAG_PM_STEPDOWN					0x00200000
#define MOVEFLAG_PM_SLIDYSLOPES					0x00400000
#define MOVEFLAG_PM_AUTOBUNNY					0x00800000
#define MOVEFLAG_PM_BUNNYFRICTION				0x01000000
#define MOVEFLAG_PM_WALLJUMP_SHIFT				25
#define MOVEFLAG_PM_WALLJUMP_MASK				0x06000000
#define MOVEFLAG_VR_INSTANT_STOP				0x08000000 /* private QSVR movevars only */
#define MOVEFLAG_QWCOMPAT						(MOVEFLAG_NOGRAVITYONGROUND|MOVEFLAG_QWEDGEBOX)

#define MASK_PLAYERSOLID	CONTENTMASK_ANYSOLID
#define CONTENTBIT_EMPTY	CONTENTMASK_FROMQ1(CONTENTS_EMPTY)
#define CONTENTBIT_SOLID	CONTENTMASK_FROMQ1(CONTENTS_SOLID)
#define CONTENTBIT_WATER	CONTENTMASK_FROMQ1(CONTENTS_WATER)
#define CONTENTBIT_SLIME	CONTENTMASK_FROMQ1(CONTENTS_SLIME)
#define CONTENTBIT_LAVA		CONTENTMASK_FROMQ1(CONTENTS_LAVA)
#define CONTENTBIT_SKY		CONTENTMASK_FROMQ1(CONTENTS_SKY)
#define CONTENTBIT_CLIP		CONTENTMASK_FROMQ1(CONTENTS_CLIP)
#define CONTENTBIT_LADDER	CONTENTMASK_FROMQ1(CONTENTS_LADDER)
#define CONTENTBITS_FLUID	(CONTENTBIT_WATER|CONTENTBIT_SLIME|CONTENTBIT_LAVA)

extern	movevars_t		movevars;
extern	playermove_t	pmove;

void PM_PlayerMove (float gamespeed);
/* Zero effective WALK intent after native teleport backward suppression. */
qboolean PM_VRInstantStopNeutralInput (const usercmd_t *cmd,
	qboolean block_teleport_backmove);
/* Apply only the active command's room-scale displacement before QuakeC.
 * The caller must save/relink player state and dispatch touches outside this
 * helper, and clear pmove.cmd.vr_roomscalemove before a later PM_PlayerMove
 * so the same displacement is not applied twice. For movable types, command
 * angles are used during the sweep and the previous pmove.angles are restored. */
void PM_ApplyPreThinkRoomScale (void);
void PM_Init (void);
void PM_InitBoxHull (void);

void PM_CategorizePosition (void);
int PM_HullPointContents (hull_t *hull, int num, vec3_t p);

int PM_ExtraBoxContents (vec3_t p);	//Peeks for HL-style water.
int PM_PointContents (vec3_t point);
qboolean PM_TestPlayerPosition (vec3_t point);
#ifndef __cplusplus
trace_t PM_PlayerTrace (vec3_t start, vec3_t stop, unsigned int solidmask);
#endif

// Client snapshot adapter; server movement remains owned by world/QC integration.
void PMCL_AddEntities(vec3_t boxminmax[2]);
void PMCL_ClearMoveVars(void);
void PMCL_ServerinfoUpdated(void);
// False rejects unsupported dialects or invalid numeric stats. This does not
// establish complete-stat receipt or an authoritative replay snapshot.
qboolean PMCL_SetMoveVars(void);

/* Build and export server movement settings without selecting them globally. */
qboolean PMSV_BuildMoveVars(movevars_t *out, edict_t *player, unsigned int protocolflags);
/* fstat and istat must each hold at least MAX_CL_STATS entries. */
qboolean PMSV_ExportMoveStats(const movevars_t *vars, float *fstat, int *istat);
#define VectorClear(v) ((v)[0] = (v)[1] = (v)[2] = 0)
#define VectorSet(r,x,y,z) do{(r)[0] = x; (r)[1] = y;(r)[2] = z;}while(0)
#define Length VectorLength
#define VectorNegate(a,b)		((b)[0]=-(a)[0],(b)[1]=-(a)[1],(b)[2]=-(a)[2])

#endif /* QUAKE_PMOVE_H */
