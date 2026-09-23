#ifndef SAVEGAME_DIALECT_H
#define SAVEGAME_DIALECT_H

#include <limits.h>
#include <stddef.h>

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

/*
 * Classify only the bounded save header. This intentionally does not validate
 * the save body or implement another save loader.
 */
static inline savegame_dialect_t Savegame_ClassifyHeader (const char *data, size_t length, int *version_out)
{
	const char			  *cursor = data;
	size_t				   remaining = length;
	savegame_header_line_t version_line, second_line, third_line;
	int				   version, third_integer;
	int				   comment2, comment3, game2, integer3;

	if (version_out)
		*version_out = -1;
	if (!data || !Savegame_HeaderLine (&cursor, &remaining, &version_line) ||
		!Savegame_HeaderInteger (version_line, &version))
		return SAVEGAME_DIALECT_MALFORMED;
	if (version_out)
		*version_out = version;
	if (!Savegame_HeaderLine (&cursor, &remaining, &second_line) || !Savegame_HeaderLine (&cursor, &remaining, &third_line))
		return SAVEGAME_DIALECT_MALFORMED;

	comment2 = Savegame_HeaderComment (second_line);
	comment3 = Savegame_HeaderComment (third_line);
	game2 = Savegame_HeaderGameDir (second_line);
	integer3 = Savegame_HeaderInteger (third_line, &third_integer) && third_integer > 0;

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

#endif /* SAVEGAME_DIALECT_H */
