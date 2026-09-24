#ifndef SAVEGAME_DIALECT_H
#define SAVEGAME_DIALECT_H

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

typedef enum savegame_dialect_e
{
	SAVEGAME_DIALECT_MALFORMED,
	SAVEGAME_DIALECT_UNKNOWN,
	SAVEGAME_DIALECT_AMBIGUOUS,
	SAVEGAME_DIALECT_LEGACY5,
	SAVEGAME_DIALECT_KEX6,
	SAVEGAME_DIALECT_INHERITED6,
	SAVEGAME_DIALECT_INHERITED7
} savegame_dialect_t;

typedef struct savegame_header_line_s
{
	const char *data;
	size_t		length;
} savegame_header_line_t;

#define SAVEGAME_DIALECT_MAX_GAME_DIR_LENGTH 63

#define SAVEGAME_PREFLIGHT_SPAWN_PARMS 16
#define SAVEGAME_PREFLIGHT_LIGHTSTYLES 64
#define SAVEGAME_PREFLIGHT_MAX_MAP_LENGTH 54
#define SAVEGAME_PREFLIGHT_MAX_NAME_BYTES 31
#define SAVEGAME_PREFLIGHT_NUMBER_LINE_CAPACITY 128
/* COM_ParseStringNewline reads with %1023s into com_token. */
#define SAVEGAME_PREFLIGHT_STRING_LINE_CAPACITY 1024

typedef struct savegame_preflight_metadata_s
{
	int		version;
	int		saved_maxclients;
	size_t	map_offset;
	size_t	map_length;
	size_t	globals_brace_offset;
	size_t	fixed_header_end_offset;
} savegame_preflight_metadata_t;

static inline int Savegame_HeaderLine (const char **cursor, size_t *remaining, savegame_header_line_t *line)
{
	const char *end;
	size_t	  length;

	if (!*remaining)
		return 0;
	for (end = *cursor; end < *cursor + *remaining && *end != '\n'; end++)
		;
	if (end == *cursor + *remaining)
		return 0;

	length = (size_t)(end - *cursor);
	if (length && (*cursor)[length - 1] == '\r')
		length--;
	line->data = *cursor;
	line->length = length;
	length = (size_t)(end - *cursor) + 1;
	*cursor += length;
	*remaining -= length;
	return 1;
}

static inline int Savegame_HeaderInteger (savegame_header_line_t line, int *value)
{
	size_t i;
	int	 number = 0;

	if (!line.length)
		return 0;
	for (i = 0; i < line.length; i++)
	{
		unsigned char c = (unsigned char)line.data[i];
		int			 digit;

		if (c < '0' || c > '9')
			return 0;
		digit = c - '0';
		if (number > (INT_MAX - digit) / 10)
			return 0;
		number = number * 10 + digit;
	}
	*value = number;
	return 1;
}

static inline int Savegame_HeaderWordNoCase (savegame_header_line_t line, size_t offset, const char *word)
{
	size_t i, length = 0;

	while (word[length])
		length++;
	if (line.length - offset != length)
		return 0;
	for (i = 0; i < length; i++)
	{
		unsigned char c = (unsigned char)line.data[offset + i];
		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c + ('a' - 'A'));
		if (c != (unsigned char)word[i])
			return 0;
	}
	return 1;
}

static inline int Savegame_HeaderFloat (savegame_header_line_t line)
{
	size_t i = 0;
	int	 before = 0, after = 0;

	if (!line.length)
		return 0;
	if (line.data[i] == '+' || line.data[i] == '-')
		if (++i == line.length)
			return 0;
	if (Savegame_HeaderWordNoCase (line, i, "inf") || Savegame_HeaderWordNoCase (line, i, "infinity") ||
		Savegame_HeaderWordNoCase (line, i, "nan"))
		return 1;
	while (i < line.length && line.data[i] >= '0' && line.data[i] <= '9')
	{
		before = 1;
		i++;
	}
	if (i < line.length && line.data[i] == '.')
	{
		i++;
		while (i < line.length && line.data[i] >= '0' && line.data[i] <= '9')
		{
			after = 1;
			i++;
		}
	}
	if (!before && !after)
		return 0;
	if (i < line.length && (line.data[i] == 'e' || line.data[i] == 'E'))
	{
		int exponent_digits = 0;
		i++;
		if (i < line.length && (line.data[i] == '+' || line.data[i] == '-'))
			i++;
		while (i < line.length && line.data[i] >= '0' && line.data[i] <= '9')
		{
			exponent_digits = 1;
			i++;
		}
		if (!exponent_digits)
			return 0;
	}
	return i == line.length;
}

static inline int Savegame_HeaderComment (savegame_header_line_t line)
{
	size_t i;

	if (line.length != 39 || line.data[22] != 'k' || line.data[23] != 'i' || line.data[24] != 'l' ||
		line.data[25] != 'l' || line.data[26] != 's' || line.data[27] != ':')
		return 0;
	for (i = 0; i < line.length; i++)
		if ((unsigned char)line.data[i] < 32 || (unsigned char)line.data[i] > 126)
			return 0;
	return 1;
}

static inline int Savegame_HeaderGameDir (savegame_header_line_t line)
{
	size_t i;

	/* Match the KEX loader's MAX_QPATH-sized token and forbidden path syntax. */
	if (!line.length || line.length > SAVEGAME_DIALECT_MAX_GAME_DIR_LENGTH || (line.length == 1 && line.data[0] == '.'))
		return 0;
	for (i = 0; i < line.length; i++)
	{
		unsigned char c = (unsigned char)line.data[i];

		if (c <= 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '"' || c == ';')
			return 0;
		if (c == '.' && i + 1 < line.length && line.data[i + 1] == '.')
			return 0;
	}
	return 1;
}

static inline savegame_dialect_t Savegame_ClassifyHeaderLines (int version, savegame_header_line_t second_line, savegame_header_line_t third_line)
{
	int third_integer;
	int comment2 = Savegame_HeaderComment (second_line);
	int comment3 = Savegame_HeaderComment (third_line);
	int game2 = Savegame_HeaderGameDir (second_line);
	int integer3 = Savegame_HeaderInteger (third_line, &third_integer) && third_integer > 0;

	switch (version)
	{
	case 5:
		if (!comment2 || !Savegame_HeaderFloat (third_line))
			return SAVEGAME_DIALECT_MALFORMED;
		return SAVEGAME_DIALECT_LEGACY5;
	case 6:
		if ((comment2 && comment3) || (game2 && integer3))
			return SAVEGAME_DIALECT_AMBIGUOUS;
		if (comment2 && integer3)
			return SAVEGAME_DIALECT_INHERITED6;
		if (game2 && comment3)
			return SAVEGAME_DIALECT_KEX6;
		return SAVEGAME_DIALECT_UNKNOWN;
	case 7:
		if (comment2 && integer3)
			return SAVEGAME_DIALECT_INHERITED7;
		return SAVEGAME_DIALECT_UNKNOWN;
	default:
		return SAVEGAME_DIALECT_UNKNOWN;
	}
}

/* Read one complete line within [data, length), matching the inherited
 * loader's CRLF handling while never searching beyond the supplied buffer. */
static inline int Savegame_PreflightReadLine (const char *data, size_t length, size_t *offset, savegame_header_line_t *line)
{
	size_t start, end;

	if (!data || !offset || !line || *offset >= length)
		return 0;
	start = *offset;
	end = start;
	while (end < length && data[end] != '\n')
	{
		if (data[end] == '\0')
			return 0;
		end++;
	}
	if (end == length)
		return 0;
	line->data = data + start;
	line->length = end - start;
	if (line->length && line->data[line->length - 1] == '\r')
		line->length--;
	*offset = end + 1;
	return 1;
}

static inline int Savegame_PreflightWhitespace (unsigned char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* Match strtol(..., 10) plus the inherited helper's trailing SP/HT trim,
 * without accepting partial numbers or overflowing int. */
static inline int Savegame_PreflightInteger (savegame_header_line_t line, int *value)
{
	size_t i = 0;
	unsigned long magnitude = 0;
	unsigned long limit;
	int negative = 0, digits = 0;

	if (!value || line.length >= SAVEGAME_PREFLIGHT_NUMBER_LINE_CAPACITY)
		return 0;
	while (i < line.length && Savegame_PreflightWhitespace ((unsigned char)line.data[i]))
		i++;
	if (i < line.length && (line.data[i] == '+' || line.data[i] == '-'))
	{
		negative = line.data[i] == '-';
		i++;
	}
	limit = (unsigned long)INT_MAX + (negative ? 1UL : 0UL);
	while (i < line.length && line.data[i] >= '0' && line.data[i] <= '9')
	{
		unsigned long digit = (unsigned long)(line.data[i] - '0');
		if (magnitude > (limit - digit) / 10UL)
			return 0;
		magnitude = magnitude * 10UL + digit;
		digits = 1;
		i++;
	}
	if (!digits)
		return 0;
	while (i < line.length && (line.data[i] == ' ' || line.data[i] == '\t'))
		i++;
	if (i != line.length)
		return 0;
	if (negative)
		*value = magnitude == (unsigned long)INT_MAX + 1UL ? INT_MIN : -(int)magnitude;
	else
		*value = (int)magnitude;
	return 1;
}

/* Match the inherited strtof/full-token/finite checks, with its 128-byte
 * scratch-buffer bound. Preserve errno because validation is read-only. */
static inline int Savegame_PreflightFloat (savegame_header_line_t line)
{
	char token[SAVEGAME_PREFLIGHT_NUMBER_LINE_CAPACITY];
	char *end;
	float parsed;
	int saved_errno;
	size_t i;

	if (!line.length || line.length >= sizeof (token))
		return 0;
	for (i = 0; i < line.length; i++)
		if (line.data[i] == '\0')
			return 0;
	for (i = 0; i < line.length; i++)
		token[i] = line.data[i];
	token[line.length] = '\0';
	saved_errno = errno;
	errno = 0;
	parsed = strtof (token, &end);
	while (*end == ' ' || *end == '\t')
		end++;
	if (errno == ERANGE || end == token || *end || !isfinite (parsed))
	{
		errno = saved_errno;
		return 0;
	}
	errno = saved_errno;
	return 1;
}

static inline int Savegame_PreflightHexName (savegame_header_line_t line)
{
	size_t i;

	if (line.length > SAVEGAME_PREFLIGHT_MAX_NAME_BYTES * 2 || line.length % 2)
		return 0;
	for (i = 0; i < line.length; i++)
	{
		unsigned char c = (unsigned char)line.data[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return 0;
		if (!(i % 2) && line.data[i] == '0' && line.data[i + 1] == '0')
			return 0; /* the writer serializes C strings and cannot emit embedded NUL */
	}
	return 1;
}

static inline int Savegame_PreflightMap (savegame_header_line_t line)
{
	size_t i, component_start = 0;

	/* SV_SpawnServer builds "maps/<name>.bsp" in a MAX_QPATH buffer. Keep
	 * path components relative and printable before the caller can spawn it. */
	if (!line.length || line.length > SAVEGAME_PREFLIGHT_MAX_MAP_LENGTH)
		return 0;
	for (i = 0; i <= line.length; i++)
	{
		if (i < line.length)
		{
			unsigned char c = (unsigned char)line.data[i];
			if (c <= 32 || c == 127 || c == ':' || c == '\\')
				return 0;
		}
		if (i == line.length || line.data[i] == '/')
		{
			size_t component_length = i - component_start;
			if (!component_length ||
				(component_length == 1 && line.data[component_start] == '.') ||
				(component_length == 2 && line.data[component_start] == '.' && line.data[component_start + 1] == '.'))
				return 0;
			component_start = i + 1;
		}
	}
	return 1;
}

/* COM_Parse skips ASCII whitespace and // or slash-star comments before
 * returning a one-character brace token. Mirror only that bounded prefix. */
static inline int Savegame_PreflightFindGlobalsBrace (const char *data, size_t length, size_t *offset, size_t *brace_offset)
{
	size_t i = *offset;

	while (i < length)
	{
		unsigned char c = (unsigned char)data[i];
		if (c == '\0')
			return 0;
		if (c <= 32)
		{
			i++;
			continue;
		}
		if (c == '/' && i + 1 < length && data[i + 1] == '/')
		{
			i += 2;
			while (i < length && data[i] != '\n')
				i++;
			continue;
		}
		if (c == '/' && i + 1 < length && data[i + 1] == '*')
		{
			i += 2;
			while (i + 1 < length && !(data[i] == '*' && data[i + 1] == '/'))
				i++;
			if (i + 1 >= length)
				return 0;
			i += 2;
			continue;
		}
		if (c != '{')
			return 0;
		*brace_offset = i;
		*offset = i + 1;
		return 1;
	}
	return 0;
}

/* Validate the inherited v6/v7 fixed prefix only; never inspect or mutate the
 * QuakeC globals or edict body. Offsets are relative to data and are written
 * only after the entire fixed prefix passes. */
static inline int Savegame_ValidateInheritedHeader (const char *data, size_t length, int maxclients_limit, savegame_preflight_metadata_t *metadata)
{
	size_t offset = 0, i;
	size_t map_offset, map_length, brace_offset;
	savegame_header_line_t version_line, comment_line, maxclients_line, line;
	savegame_preflight_metadata_t result;
	savegame_dialect_t dialect;
	int version, maxclients, value;

	if (!data || maxclients_limit < 1 ||
		!Savegame_PreflightReadLine (data, length, &offset, &version_line) ||
		!Savegame_HeaderInteger (version_line, &version) ||
		!Savegame_PreflightReadLine (data, length, &offset, &comment_line) ||
		!Savegame_PreflightReadLine (data, length, &offset, &maxclients_line))
		return 0;
	dialect = Savegame_ClassifyHeaderLines (version, comment_line, maxclients_line);
	if (dialect != SAVEGAME_DIALECT_INHERITED6 && dialect != SAVEGAME_DIALECT_INHERITED7)
		return 0;
	if (!Savegame_PreflightInteger (maxclients_line, &maxclients) || maxclients < 1 || maxclients > maxclients_limit)
		return 0;

	for (i = 0; i < (size_t)maxclients; i++)
	{
		/* The inherited writer serializes client->active as exactly 0 or 1. */
		if (!Savegame_PreflightReadLine (data, length, &offset, &line) ||
			!Savegame_PreflightInteger (line, &value) || (value != 0 && value != 1))
			return 0;
		if (version == 7)
		{
			if (!Savegame_PreflightReadLine (data, length, &offset, &line) || !Savegame_PreflightHexName (line))
				return 0;
		}
		if (!Savegame_PreflightReadLine (data, length, &offset, &line) ||
			!Savegame_PreflightInteger (line, &value) ||
			!Savegame_PreflightReadLine (data, length, &offset, &line) ||
			!Savegame_PreflightInteger (line, &value))
			return 0;
		for (value = 0; value < SAVEGAME_PREFLIGHT_SPAWN_PARMS; value++)
			if (!Savegame_PreflightReadLine (data, length, &offset, &line) || !Savegame_PreflightFloat (line))
				return 0;
	}

	if (!Savegame_PreflightReadLine (data, length, &offset, &line) || !Savegame_PreflightFloat (line))
		return 0;
	if (!Savegame_PreflightReadLine (data, length, &offset, &line) || !Savegame_PreflightMap (line))
		return 0;
	map_offset = (size_t)(line.data - data);
	map_length = line.length;
	if (!Savegame_PreflightReadLine (data, length, &offset, &line) || !Savegame_PreflightFloat (line))
		return 0;
	for (i = 0; i < SAVEGAME_PREFLIGHT_LIGHTSTYLES; i++)
	{
		size_t j;
		if (!Savegame_PreflightReadLine (data, length, &offset, &line) || !line.length ||
			line.length >= SAVEGAME_PREFLIGHT_STRING_LINE_CAPACITY)
			return 0;
		/* The donor's COM_ParseStringNewline uses %s, so a whitespace byte
		 * would desynchronize the subsequent QuakeC-block parse. */
		for (j = 0; j < line.length; j++)
			if (Savegame_PreflightWhitespace ((unsigned char)line.data[j]))
				return 0;
	}
	if (!Savegame_PreflightFindGlobalsBrace (data, length, &offset, &brace_offset))
		return 0;

	result.version = version;
	result.saved_maxclients = maxclients;
	result.map_offset = map_offset;
	result.map_length = map_length;
	result.globals_brace_offset = brace_offset;
	result.fixed_header_end_offset = offset;
	if (metadata)
		*metadata = result;
	return 1;
}

/*
 * Classify only the bounded save header. This intentionally does not validate
 * the save body or implement another save loader.
 */
static inline savegame_dialect_t Savegame_ClassifyHeader (const char *data, size_t length, int *version_out)
{
	const char			  *cursor = data;
	size_t				   remaining = length;
	savegame_header_line_t version_line, second_line, third_line;
	int				   version;

	if (version_out)
		*version_out = -1;
	if (!data || !Savegame_HeaderLine (&cursor, &remaining, &version_line) ||
		!Savegame_HeaderInteger (version_line, &version))
		return SAVEGAME_DIALECT_MALFORMED;
	if (version_out)
		*version_out = version;
	if (!Savegame_HeaderLine (&cursor, &remaining, &second_line) || !Savegame_HeaderLine (&cursor, &remaining, &third_line))
		return SAVEGAME_DIALECT_MALFORMED;

	return Savegame_ClassifyHeaderLines (version, second_line, third_line);
}

#endif /* SAVEGAME_DIALECT_H */
