#include "quakedef.h"
#include "vr_weapon_schema.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
	const char *cursor;
} vr_schema_parser_t;

typedef struct
{
	float held_scale;
	vec3_t held_offset;
	vec3_t muzzle_offset;
	qboolean has_held_scale;
	qboolean has_held_offset;
	qboolean has_muzzle_offset;
} vr_schema_globals_t;

enum vr_schema_token_result
{
	VR_SCHEMA_TOKEN_ERROR = -1,
	VR_SCHEMA_TOKEN_EOF = 0,
	VR_SCHEMA_TOKEN_OK = 1
};

static enum vr_schema_token_result VR_SchemaNext(vr_schema_parser_t *parser,
													 char *token,
													 size_t token_size)
{
	qboolean parse_error = false;
	const char *next = COM_ParseExBuffer(parser->cursor, CPE_NOTRUNC,
											 token, token_size, &parse_error);

	if (parse_error)
		return VR_SCHEMA_TOKEN_ERROR;
	if (!next)
		return VR_SCHEMA_TOKEN_EOF;
	parser->cursor = next;
	return VR_SCHEMA_TOKEN_OK;
}

static qboolean VR_SchemaTokenIsBrace(const char *token)
{
	return !strcmp(token, "{") || !strcmp(token, "}");
}

static qboolean VR_SchemaEqualNoCase(const char *a, const char *b)
{
	while (*a && *b)
	{
		unsigned char ca = (unsigned char)*a++;
		unsigned char cb = (unsigned char)*b++;

		if (ca >= 'A' && ca <= 'Z')
			ca = (unsigned char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z')
			cb = (unsigned char)(cb - 'A' + 'a');
		if (ca != cb)
			return false;
	}
	return *a == *b;
}

static qboolean VR_SchemaParseInt(const char *token, int *value)
{
	char *end;
	long parsed;

	if (!token[0])
		return false;
	errno = 0;
	parsed = strtol(token, &end, 10);
	if (errno == ERANGE || end == token || *end || parsed < INT_MIN || parsed > INT_MAX)
		return false;
	*value = (int)parsed;
	return true;
}

static qboolean VR_SchemaParseFloat(const char *token, float *value)
{
	char *end;
	float parsed;

	if (!token[0])
		return false;
	errno = 0;
	parsed = strtof(token, &end);
	if (errno == ERANGE || end == token || *end || !isfinite(parsed))
		return false;
	*value = parsed;
	return true;
}

static int VR_SchemaParseStat(const char *token, int *default_max,
							 qboolean *valid)
{
	int stat;
	const char *digits = token;
	qboolean numeric = false;

	if (default_max)
		*default_max = 0;
	*valid = true;
	if (!token[0])
		return -1;

	if (*digits == '+' || *digits == '-')
		digits++;
	if (*digits >= '0' && *digits <= '9')
		numeric = true;
	else if (digits != token)
	{
		*valid = false;
		return -1;
	}
	if (numeric)
	{
		if (!VR_SchemaParseInt(token, &stat) || stat < 0 || stat >= MAX_CL_STATS)
		{
			*valid = false;
			return -1;
		}
		return stat;
	}

	if (VR_SchemaEqualNoCase(token, "ammo")) return STAT_AMMO;
	if (VR_SchemaEqualNoCase(token, "shells"))
	{
		if (default_max) *default_max = 100;
		return STAT_SHELLS;
	}
	if (VR_SchemaEqualNoCase(token, "nails"))
	{
		if (default_max) *default_max = 200;
		return STAT_NAILS;
	}
	if (VR_SchemaEqualNoCase(token, "rockets"))
	{
		if (default_max) *default_max = 100;
		return STAT_ROCKETS;
	}
	if (VR_SchemaEqualNoCase(token, "cells"))
	{
		if (default_max) *default_max = 100;
		return STAT_CELLS;
	}
	if (VR_SchemaEqualNoCase(token, "activeweapon")) return STAT_ACTIVEWEAPON;
	if (VR_SchemaEqualNoCase(token, "items")) return STAT_ITEMS;
	if (VR_SchemaEqualNoCase(token, "weapon")) return STAT_WEAPON;
	if (VR_SchemaEqualNoCase(token, "weapons")) return STAT_VR_WEAPONS;
	if (VR_SchemaEqualNoCase(token, "items2")) return STAT_VR_ITEMS2;
	if (VR_SchemaEqualNoCase(token, "moditems")) return STAT_VR_MODITEMS;
	if (VR_SchemaEqualNoCase(token, "weapon2")) return STAT_VR_WEAPON2;
	if (VR_SchemaEqualNoCase(token, "weapons2")) return STAT_VR_WEAPONS2;
	return -1;
}

static qboolean VR_SchemaReadValue(vr_schema_parser_t *parser,
									  char *token, size_t token_size)
{
	return VR_SchemaNext(parser, token, token_size) == VR_SCHEMA_TOKEN_OK &&
		!VR_SchemaTokenIsBrace(token);
}

static qboolean VR_SchemaReadVector(vr_schema_parser_t *parser,
									   const char *first_token, vec3_t vector)
{
	char token[COM_PARSE_MAX_TOKEN_SIZE];
	int i;

	if (!VR_SchemaParseFloat(first_token, &vector[0]))
		return false;
	for (i = 1; i < 3; i++)
	{
		if (!VR_SchemaReadValue(parser, token, sizeof(token)) ||
			!VR_SchemaParseFloat(token, &vector[i]))
			return false;
	}
	return true;
}

static qboolean VR_SchemaReadFloat(vr_schema_parser_t *parser, float *value)
{
	char token[COM_PARSE_MAX_TOKEN_SIZE];
	return VR_SchemaReadValue(parser, token, sizeof(token)) &&
		VR_SchemaParseFloat(token, value);
}

static qboolean VR_SchemaReadGlobalVector(vr_schema_parser_t *parser,
											 vec3_t vector)
{
	char first[COM_PARSE_MAX_TOKEN_SIZE];
	if (!VR_SchemaReadValue(parser, first, sizeof(first)))
		return false;
	return VR_SchemaReadVector(parser, first, vector);
}

static qboolean VR_SchemaIgnoreVector(vr_schema_parser_t *parser,
									 const char *first_token)
{
	vec3_t ignored;

	return VR_SchemaReadVector(parser, first_token, ignored);
}

static qboolean VR_SchemaIgnoreGlobalVector(vr_schema_parser_t *parser)
{
	vec3_t ignored;

	return VR_SchemaReadGlobalVector(parser, ignored);
}

static qboolean VR_SchemaIsLegacyMultiplayerVectorKey(const char *key)
{
	return !strcmp(key, "mp_held_offset") ||
		!strcmp(key, "mp_muzzle_offset") ||
		!strcmp(key, "enhanced_mp_held_offset") ||
		!strcmp(key, "enhanced_mp_muzzle_offset");
}

static qboolean VR_SchemaFinishEntry(vr_weapon_schema_entry_t *entry,
									 const vr_schema_globals_t *globals)
{
	if (!entry->has_held_scale && globals->has_held_scale)
	{
		entry->held_scale = globals->held_scale;
		entry->has_held_scale = true;
	}
	if (!entry->has_held_offset && globals->has_held_offset)
	{
		memcpy(entry->held_offset, globals->held_offset, sizeof(vec3_t));
		entry->has_held_offset = true;
	}
	if (!entry->has_muzzle_offset && globals->has_muzzle_offset)
	{
		memcpy(entry->muzzle_offset, globals->muzzle_offset, sizeof(vec3_t));
		entry->has_muzzle_offset = true;
	}
	if (!entry->viewmodel_path[0] && entry->model_path[0] &&
		(entry->has_held_scale || entry->has_held_offset ||
		 entry->has_muzzle_offset || entry->has_muzzle_source_offset ||
		 entry->has_muzzle_source_viewofs || entry->has_spawn_at_self_origin ||
		 entry->has_enhanced_held_offset || entry->has_enhanced_muzzle_offset ||
		 entry->melee.has_enabled || entry->melee.has_base ||
		 entry->melee.has_tip || entry->melee.has_speed ||
		 entry->melee.has_ready_frame))
		memcpy(entry->viewmodel_path, entry->model_path, sizeof(entry->model_path));
	if (!entry->model_path[0] && entry->viewmodel_path[0])
		memcpy(entry->model_path, entry->viewmodel_path, sizeof(entry->model_path));

	if (!entry->bitmask && entry->owned_stat < 0 && entry->active_stat >= 0)
	{
		entry->owned_stat = entry->active_stat;
		entry->owned_mask = entry->active_mask;
	}
	return true;
}

static qboolean VR_SchemaHasHeldPresentation(const vr_weapon_schema_entry_t *entry)
{
	return entry->viewmodel_path[0] &&
		(entry->has_held_scale || entry->has_held_offset ||
		 entry->has_muzzle_offset || entry->has_muzzle_source_offset ||
		 entry->has_muzzle_source_viewofs || entry->has_spawn_at_self_origin ||
		 entry->has_enhanced_held_offset || entry->has_enhanced_muzzle_offset ||
		 entry->melee.has_enabled || entry->melee.has_base ||
		 entry->melee.has_tip || entry->melee.has_speed ||
		 entry->melee.has_ready_frame);
}

static qboolean VR_SchemaParseEntry(vr_schema_parser_t *parser,
									 vr_weapon_schema_entry_t *entry,
									 const vr_schema_globals_t *globals)
{
	char key[COM_PARSE_MAX_TOKEN_SIZE];
	char value[COM_PARSE_MAX_TOKEN_SIZE];
	enum vr_schema_token_result result;

	memset(entry, 0, sizeof(*entry));
	entry->scale = 1.0f;
	entry->held_scale = 1.0f;
	entry->owned_stat = -1;
	entry->active_stat = -1;
	entry->ammo_stat = -1;

	for (;;)
	{
		result = VR_SchemaNext(parser, key, sizeof(key));
		if (result != VR_SCHEMA_TOKEN_OK || !key[0])
			return false;
		if (!strcmp(key, "}"))
			break;
		if (!strcmp(key, "{") || strlen(key) >= 64)
			return false;

		if (!strcmp(key, "offset") || !strcmp(key, "held_offset") ||
			VR_SchemaIsLegacyMultiplayerVectorKey(key) ||
			!strcmp(key, "muzzle_offset") ||
			!strcmp(key, "enhanced_held_offset") ||
			!strcmp(key, "enhanced_muzzle_offset") ||
			!strcmp(key, "melee_base") || !strcmp(key, "melee_tip") ||
			!strcmp(key, "muzzle_source_offset"))
		{
			if (!VR_SchemaReadValue(parser, value, sizeof(value)))
				return false;
			if (!strcmp(key, "offset"))
			{
				if (!VR_SchemaReadVector(parser, value, entry->offset)) return false;
				entry->has_offset = true;
			}
			else if (!strcmp(key, "held_offset"))
			{
				if (!VR_SchemaReadVector(parser, value, entry->held_offset)) return false;
				entry->has_held_offset = true;
			}
			else if (VR_SchemaIsLegacyMultiplayerVectorKey(key))
			{
				if (!VR_SchemaIgnoreVector(parser, value)) return false;
			}
			else if (!strcmp(key, "muzzle_offset"))
			{
				if (!VR_SchemaReadVector(parser, value, entry->muzzle_offset)) return false;
				entry->has_muzzle_offset = true;
			}
			else if (!strcmp(key, "enhanced_held_offset"))
			{
				if (!VR_SchemaReadVector(parser, value, entry->enhanced_held_offset)) return false;
				entry->has_enhanced_held_offset = true;
			}
			else if (!strcmp(key, "enhanced_muzzle_offset"))
			{
				if (!VR_SchemaReadVector(parser, value, entry->enhanced_muzzle_offset)) return false;
				entry->has_enhanced_muzzle_offset = true;
			}
			else if (!strcmp(key, "melee_base") || !strcmp(key, "melee_tip"))
			{
				const qboolean base = !strcmp(key, "melee_base");
				vec_t *point = base ? entry->melee.base : entry->melee.tip;
				if (!VR_SchemaReadVector(parser, value, point)) return false;
				for (int axis = 0; axis < 3; ++axis)
					if (fabsf(point[axis]) > 4096.0f) return false;
				if (base) entry->melee.has_base = true;
				else entry->melee.has_tip = true;
			}
			else
			{
				if (!VR_SchemaReadVector(parser, value, entry->muzzle_source_offset)) return false;
				entry->has_muzzle_source_offset = true;
			}
			continue;
		}

		if (!VR_SchemaReadValue(parser, value, sizeof(value)))
			return false;
		if (!strcmp(key, "bitmask"))
		{
			if (!VR_SchemaParseInt(value, &entry->bitmask)) return false;
		}
		else if (!strcmp(key, "model"))
		{
			size_t length = strlen(value);
			if (length >= sizeof(entry->model_path)) return false;
			memcpy(entry->model_path, value, length + 1);
		}
		else if (!strcmp(key, "viewmodel") || !strcmp(key, "held_model"))
		{
			size_t length = strlen(value);
			if (length >= sizeof(entry->viewmodel_path)) return false;
			memcpy(entry->viewmodel_path, value, length + 1);
		}
		else if (!strcmp(key, "impulse"))
		{
			if (!VR_SchemaParseInt(value, &entry->impulse)) return false;
		}
		else if (!strcmp(key, "scale"))
		{
			if (!VR_SchemaParseFloat(value, &entry->scale)) return false;
		}
		else if (!strcmp(key, "held_scale"))
		{
			if (!VR_SchemaParseFloat(value, &entry->held_scale)) return false;
			entry->has_held_scale = true;
		}
		else if (!strcmp(key, "muzzle_source_viewofs"))
		{
			int enabled;
			if (!VR_SchemaParseInt(value, &enabled)) return false;
			entry->muzzle_source_viewofs = enabled != 0;
			entry->has_muzzle_source_viewofs = true;
		}
		else if (!strcmp(key, "spawn_at_self_origin") ||
				 !strcmp(key, "muzzle_spawn_at_self_origin") ||
				 !strcmp(key, "projectile_spawn_at_self_origin"))
		{
			int enabled;
			if (!VR_SchemaParseInt(value, &enabled)) return false;
			entry->spawn_at_self_origin = enabled != 0;
			entry->has_spawn_at_self_origin = true;
		}
		else if (!strcmp(key, "melee"))
		{
			int enabled;
			if (!VR_SchemaParseInt(value, &enabled) || (enabled != 0 && enabled != 1)) return false;
			entry->melee.enabled = enabled != 0;
			entry->melee.has_enabled = true;
		}
		else if (!strcmp(key, "melee_speed"))
		{
			if (!VR_SchemaParseFloat(value, &entry->melee.speed) ||
				entry->melee.speed < 0.25f || entry->melee.speed > 10.0f) return false;
			entry->melee.has_speed = true;
		}
		else if (!strcmp(key, "melee_frame"))
		{
			if (!VR_SchemaParseInt(value, &entry->melee.ready_frame) ||
				entry->melee.ready_frame < 0 || entry->melee.ready_frame > 65535) return false;
			entry->melee.has_ready_frame = true;
		}
		else if (!strcmp(key, "owned_stat"))
		{
			qboolean valid;
			entry->owned_stat = VR_SchemaParseStat(value, NULL, &valid);
			if (!valid) return false;
		}
		else if (!strcmp(key, "owned_mask"))
		{
			if (!VR_SchemaParseInt(value, &entry->owned_mask)) return false;
		}
		else if (!strcmp(key, "active_stat"))
		{
			qboolean valid;
			entry->active_stat = VR_SchemaParseStat(value, NULL, &valid);
			if (!valid) return false;
		}
		else if (!strcmp(key, "active_mask"))
		{
			if (!VR_SchemaParseInt(value, &entry->active_mask)) return false;
		}
		else if (!strcmp(key, "ammo"))
		{
			int default_max;
			qboolean valid;
			entry->ammo_stat = VR_SchemaParseStat(value, &default_max, &valid);
			if (!valid) return false;
			if (!entry->ammo_max)
				entry->ammo_max = default_max;
		}
		else if (!strcmp(key, "ammo_stat"))
		{
			qboolean valid;
			entry->ammo_stat = VR_SchemaParseStat(value, NULL, &valid);
			if (!valid) return false;
		}
		else if (!strcmp(key, "ammo_max"))
		{
			if (!VR_SchemaParseInt(value, &entry->ammo_max)) return false;
		}
		/* Unknown entry keys intentionally consume exactly one scalar value. */
	}

	return VR_SchemaFinishEntry(entry, globals);
}

static qboolean VR_SchemaParseGlobal(vr_schema_parser_t *parser,
									 const char *key,
									 vr_schema_globals_t *globals)
{
	if (!strcmp(key, "global_held_scale"))
	{
		if (!VR_SchemaReadFloat(parser, &globals->held_scale)) return false;
		globals->has_held_scale = true;
	}
	else if (!strcmp(key, "global_held_offset"))
	{
		if (!VR_SchemaReadGlobalVector(parser, globals->held_offset)) return false;
		globals->has_held_offset = true;
	}
	else if (!strcmp(key, "global_muzzle_offset"))
	{
		if (!VR_SchemaReadGlobalVector(parser, globals->muzzle_offset)) return false;
		globals->has_muzzle_offset = true;
	}
	else if (!strcmp(key, "global_mp_held_offset"))
	{
		if (!VR_SchemaIgnoreGlobalVector(parser)) return false;
	}
	else if (!strcmp(key, "global_mp_muzzle_offset"))
	{
		if (!VR_SchemaIgnoreGlobalVector(parser)) return false;
	}
	return true;
}

qboolean VR_WeaponSchemaParse(const char *text,
							  vr_weapon_schema_entry_t *entries,
							  size_t capacity, size_t *count)
{
	vr_weapon_schema_entry_t staged[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_schema_globals_t globals;
	vr_schema_parser_t parser;
	char token[COM_PARSE_MAX_TOKEN_SIZE];
	size_t staged_count = 0;
	size_t blocks_seen = 0;
	enum vr_schema_token_result result;

	if (!count)
		return false;
	*count = 0;
	if (!text)
		return false;
	memset(&globals, 0, sizeof(globals));
	globals.held_scale = 1.0f;
	parser.cursor = text;

	for (;;)
	{
		result = VR_SchemaNext(&parser, token, sizeof(token));
		if (result == VR_SCHEMA_TOKEN_EOF)
			break;
		if (result == VR_SCHEMA_TOKEN_ERROR)
			return false;
		if (!strcmp(token, "}"))
			return false;
		if (!strcmp(token, "{"))
		{
			vr_weapon_schema_entry_t entry;
			blocks_seen++;
			if (blocks_seen > VR_WEAPON_SCHEMA_MAX_ENTRIES ||
				!VR_SchemaParseEntry(&parser, &entry, &globals))
				return false;
			if (!entry.bitmask && entry.owned_stat < 0 && entry.active_stat < 0 &&
				!VR_SchemaHasHeldPresentation(&entry))
				continue;
			if (staged_count >= VR_WEAPON_SCHEMA_MAX_ENTRIES ||
				staged_count >= capacity || !entries)
				return false;
			staged[staged_count++] = entry;
			continue;
		}
		if (!VR_SchemaParseGlobal(&parser, token, &globals))
			return false;
	}

	if (staged_count)
		memcpy(entries, staged, staged_count * sizeof(staged[0]));
	*count = staged_count;
	return true;
}
