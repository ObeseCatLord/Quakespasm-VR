/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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

// common.c -- misc functions used in client and server

#include "quakedef.h"
#include "sys.h"

#include "q_ctype.h"
#include "unicode_translit.h"
#include "filenames.h"
#include "steam.h"
#include "vr_weapon_calibration.h"
#include "vr_weapon_menu.h"
#include <errno.h>
#include <stdlib.h>

// Plug our allocators into miniz:
#define MZ_MALLOC(x)	 Mem_Alloc (x)
#define MZ_FREE(x)		 Mem_Free (x)
#define MZ_REALLOC(p, x) Mem_Realloc (p, x)

// include miniz stb-syle, directly in this compilation unit.
// (supported by miniz)
#include "miniz.c"

static char *largv[MAX_NUM_ARGVS + 1];
static char	 argvdummy[] = " ";

int safemode;

cvar_t registered = {"registered", "1", CVAR_ROM};				 /* set to correct value in COM_CheckRegistered() */
cvar_t cmdline = {"cmdline", "", CVAR_ROM /*|CVAR_SERVERINFO*/}; /* sending cmdline upon CCREQ_RULE_INFO is evil */

static qboolean com_modified; // set true if using non-id files

static qboolean multiuser;

static void COM_Path_f (void);

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT		339	  /* id1/pak0.pak - v1.0x */
#define PAK0_CRC_V100	13900 /* id1/pak0.pak - v1.00 */
#define PAK0_CRC_V101	62751 /* id1/pak0.pak - v1.01 */
#define PAK0_CRC_V106	32981 /* id1/pak0.pak - v1.06 */
#define PAK0_CRC		(PAK0_CRC_V106)
#define PAK0_COUNT_V091 308	  /* id1/pak0.pak - v0.91/0.92, not supported */
#define PAK0_CRC_V091	28804 /* id1/pak0.pak - v0.91/0.92, not supported */

#define PAK0_CRC_RERELEASE 20578
#define PAK0_COUNT_RERELEASE 1121
#define PAK0_SIZE_RERELEASE 180815252

THREAD_LOCAL char com_token[COM_PARSE_MAX_TOKEN_SIZE];
int				  com_argc;
char			**com_argv;

static char com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic, mg3;

extern const unsigned char vkquake_pak[];
extern const int		   vkquake_pak_size;
extern const int		   vkquake_pak_decompressed_size;

/*

All of Quake's data access is through a hierchal file system, but the contents
of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all
game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.
This can be overridden with the "-basedir" command line parm to allow code
debugging in a different directory.  It is selected during filesystem
initialization and remains the primary game-data root.

The active "game directory" is the highest-priority directory tree and the
destination for game-owned files (savegames, screenshots, demos and the game
config).  One or more game directories can be selected with "-game"; the final
one is active.  The local "game" command can replace that list while Quake runs.

The "cache directory" is only used during development to save network bandwidth,
especially over ISDN / T1 lines.  If there is a cache directory specified, when
a file is found by the normal search path, it will be mirrored into the cache
directory, then opened there.

FIXME:
The file "parms.txt" will be read out of the game directory and appended to the
current command line arguments to allow different games to initialize startup
parms differently.  This could be used to add a "-sspeed 22050" for the high
quality sound edition.  Because they are added at the end, they will not
override an explicit setting on the original command line.

*/

//============================================================================

// ClearLink is used for new headnodes
void ClearLink (link_t *l)
{
	l->prev = l->next = l;
}

void RemoveLink (link_t *l)
{
	l->next->prev = l->prev;
	l->prev->next = l->next;
}

void InsertLinkBefore (link_t *l, link_t *before)
{
	l->next = before;
	l->prev = before->prev;
	l->prev->next = l;
	l->next->prev = l;
}

void InsertLinkAfter (link_t *l, link_t *after)
{
	l->next = after->next;
	l->prev = after;
	l->prev->next = l;
	l->next->prev = l;
}

/*
============================================================================

							DYNAMIC VECTORS

============================================================================
*/

void Vec_Grow (void **pvec, size_t element_size, size_t count)
{
	vec_header_t header;
	if (*pvec)
		header = VEC_HEADER (*pvec);
	else
		header.size = header.capacity = 0;

	if (header.size + count > header.capacity)
	{
		void  *new_buffer;
		size_t total_size;

		header.capacity = header.size + count;
		header.capacity += header.capacity >> 1;
		if (header.capacity < 16)
			header.capacity = 16;
		total_size = sizeof (vec_header_t) + header.capacity * element_size;

		if (*pvec)
			new_buffer = Mem_Realloc (((vec_header_t *)*pvec) - 1, total_size);
		else
			new_buffer = Mem_AllocNonZero (total_size);
		if (!new_buffer)
			Sys_Error ("Vec_Grow: failed to allocate %lu bytes\n", (unsigned long)total_size);

		*pvec = 1 + (vec_header_t *)new_buffer;
		VEC_HEADER (*pvec) = header;
	}
}

void Vec_Append (void **pvec, size_t element_size, const void *data, size_t count)
{
	if (!count)
		return;
	Vec_Grow (pvec, element_size, count);
	memcpy ((byte *)*pvec + VEC_HEADER (*pvec).size * element_size, data, count * element_size);
	VEC_HEADER (*pvec).size += count;
}

void Vec_Clear (void **pvec)
{
	if (*pvec)
		VEC_HEADER (*pvec).size = 0;
}

void Vec_Free (void **pvec)
{
	if (*pvec)
	{
		Mem_Free (&VEC_HEADER (*pvec));
		*pvec = NULL;
	}
}

/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int q_strnaturalcmp (const char *s1, const char *s2)
{
	qboolean neg1, neg2, sign1, sign2;

	if (s1 == s2)
		return 0;

	neg1 = *s1 == '-';
	neg2 = *s2 == '-';
	sign1 = neg1 || *s1 == '+';
	sign2 = neg2 || *s2 == '+';

	// early out if strings start with different signs followed by digits
	if (neg1 != neg2 && q_isdigit (s1[sign1]) && q_isdigit (s1[sign2]))
		return neg2 - neg1;

skip_prefix:
	while (*s1 && !q_isdigit (*s1) && q_toupper (*s1) == q_toupper (*s2))
	{
		s1++;
		s2++;
		continue;
	}

	if (q_isdigit (*s1) && q_isdigit (*s2))
	{
		const char *begin1 = s1++;
		const char *begin2 = s2++;
		int			diff, sign;

		while (*begin1 == '0')
			begin1++;
		while (*begin2 == '0')
			begin2++;

		while (q_isdigit (*s1))
			s1++;
		while (q_isdigit (*s2))
			s2++;

		sign = neg1 ? -1 : 1;

		diff = (s1 - begin1) - (s2 - begin2);
		if (diff)
			return diff * sign;

		while (begin1 != s1)
		{
			diff = *begin1++ - *begin2++;
			if (diff)
				return diff * sign;
		}

		// We only support negative numbers at the beginning of strings so that
		// "-2" is sorted before "-1", but "file-2345.ext" *after* "file-1234.ext".
		neg1 = neg2 = false;

		goto skip_prefix;
	}

	return q_toupper (*s1) - q_toupper (*s2);
}

int q_strcasecmp (const char *s1, const char *s2)
{
	const char *p1 = s1;
	const char *p2 = s2;
	char		c1, c2;

	if (p1 == p2)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0')
			break;
	} while (c1 == c2);

	return (int)(c1 - c2);
}

int q_strncasecmp (const char *s1, const char *s2, size_t n)
{
	const char *p1 = s1;
	const char *p2 = s2;
	char		c1, c2;

	if (p1 == p2 || n == 0)
		return 0;

	do
	{
		c1 = q_tolower (*p1++);
		c2 = q_tolower (*p2++);
		if (c1 == '\0' || c1 != c2)
			break;
	} while (--n > 0);

	return (int)(c1 - c2);
}

char *q_strcasestr (const char *haystack, const char *needle)
{
	const size_t len = strlen (needle);

	if (!len)
		return (char *)haystack;

	while (*haystack)
	{
		if (!q_strncasecmp (haystack, needle, len))
			return (char *)haystack;

		++haystack;
	}

	return NULL;
}

/*
================
COM_TintSubstring
================
*/
char *COM_TintSubstring (const char *in, const char *substr, char *out, size_t outsize)
{
	int	  l;
	char *m = out;
	q_strlcpy (out, in, outsize);
	if (*substr)
	{
		while ((m = q_strcasestr (m, substr)))
		{
			for (l = 0; substr[l]; l++)
				if (m[l] > ' ')
					m[l] |= 0x80;
			m += l;
		}
	}
	return out;
}

char *q_strlwr (char *str)
{
	char *c;
	c = str;
	while (*c)
	{
		*c = q_tolower (*c);
		c++;
	}
	return str;
}

char *q_strupr (char *str)
{
	char *c;
	c = str;
	while (*c)
	{
		*c = q_toupper (*c);
		c++;
	}
	return str;
}

/*
==================
UTF8_WriteCodePoint

Writes the UTF-8 encoding of the given code point.
Returns the number of bytes written (up to 4),
or 0 on error (overflow or invalid code point)
==================
*/
size_t UTF8_WriteCodePoint (char *dst, size_t maxbytes, uint32_t codepoint)
{
	if (!maxbytes)
		return 0;

	if (codepoint < 0x80)
	{
		dst[0] = (char)codepoint;
		return 1;
	}

	if (codepoint < 0x800)
	{
		if (maxbytes < 2)
			return 0;
		dst[0] = 0xC0 | (codepoint >> 6);
		dst[1] = 0x80 | (codepoint & 63);
		return 2;
	}

	if (codepoint < 0x10000)
	{
		if (maxbytes < 3)
			return 0;
		dst[0] = 0xE0 | (codepoint >> 12);
		dst[1] = 0x80 | ((codepoint >> 6) & 63);
		dst[2] = 0x80 | (codepoint & 63);
		return 3;
	}

	if (codepoint < 0x110000)
	{
		if (maxbytes < 4)
			return 0;
		dst[0] = 0xF0 | (codepoint >> 18);
		dst[1] = 0x80 | ((codepoint >> 12) & 63);
		dst[2] = 0x80 | ((codepoint >> 6) & 63);
		dst[3] = 0x80 | (codepoint & 63);
		return 4;
	}

	return 0;
}

// clang-format off
#define UNICODE_UNKNOWN 0xFFFD
#define UNICODE_MAX 0x10FFFF
#define QCHAR_BOX 11
static char unicode_translit[65536][2];
static qboolean unicode_translit_init;

static const uint32_t qchar_to_unicode[256] =
{/*     0       1       2       3       4       5       6       7       8       9       10      11      12      13      14      15
      ----------------------------------------------------------------------------------------------------------------------------------
  0 */  0x00B7, 0,      0,      0,      0,      0x00B7, 0,      0,      0,      0,      '\n',   0x25A0, ' ',    0x25B6, 0x00B7, 0x00B7, /*
  1 */  0x301A, 0x301B, '0',    '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',    0x00B7, '-',    '-',    '-',    /*
  2 */  ' ',    '!',    '"',    '#',    '$',    '%',    '&',    '\'',   '(',    ')',    '*',    '+',    ',',    '-',    '.',    '/',    /*
  3 */  '0',    '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',    ':',    ';',    '<',    '=',    '>',    '?',    /*
  4 */  '@',    'A',    'B',    'C',    'D',    'E',    'F',    'G',    'H',    'I',    'J',    'K',    'L',    'M',    'N',    'O',    /*
  5 */  'P',    'Q',    'R',    'S',    'T',    'U',    'V',    'W',    'X',    'Y',    'Z',    '[',    '\\',   ']',    '^',    '_',    /*
  6 */  '`',    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'h',    'i',    'j',    'k',    'l',    'm',    'n',    'o',    /*
  7 */  'p',    'q',    'r',    's',    't',    'u',    'v',    'w',    'x',    'y',    'z',    '{',    '|',    '}',    '~',    0x2190, /*

  8 */  '-',    '-',    '-',    '-',    0,      0x2022, 0,      0,      0,      0,      '\n',   0x25A0, ' ',    0x25B6, 0x2022, 0x2022, /*
  9 */  0x301A, 0x301B, '0',    '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',    0x2022, '-',    '-',    '-',    /*
 10 */  ' ',    '!',    '"',    '#',    '$',    '%',    '&',    '\'',   '(',    ')',    '*',    '+',    ',',    '-',    '.',    '/',    /*
 11 */  '0',    '1',    '2',    '3',    '4',    '5',    '6',    '7',    '8',    '9',    ':',    ';',    '<',    '=',    '>',    '?',    /*
 12 */  '@',    'A',    'B',    'C',    'D',    'E',    'F',    'G',    'H',    'I',    'J',    'K',    'L',    'M',    'N',    'O',    /*
 13 */  'P',    'Q',    'R',    'S',    'T',    'U',    'V',    'W',    'X',    'Y',    'Z',    '[',    '\\',   ']',    '^',    '_',    /*
 14 */  '`',    'a',    'b',    'c',    'd',    'e',    'f',    'g',    'h',    'i',    'j',    'k',    'l',    'm',    'n',    'o',    /*
 15 */  'p',    'q',    'r',    's',    't',    'u',    'v',    'w',    'x',    'y',    'z',    '{',    '|',    '}',    '~',    0x2190, /*
      ----------------------------------------------------------------------------------------------------------------------------------
*/};
// clang-format on

/*
==================
UTF8_CodePointLength

Returns the number of bytes needed to encode the codepoint
using UTF-8 (max 4), or 0 for an invalid code point
==================
*/
size_t UTF8_CodePointLength (uint32_t codepoint)
{
	if (codepoint < 0x80)
		return 1;

	if (codepoint < 0x800)
		return 2;

	if (codepoint < 0x10000)
		return 3;

	if (codepoint < 0x110000)
		return 4;

	return 0;
}

/*
==================
UTF8_FromQuake

Converts a string from Quake encoding to UTF-8

Returns the number of written characters (including the NUL terminator)
if a valid output buffer is provided (dst is non-NULL, maxbytes > 0),
or the total amount of space necessary to encode the entire src string
if dst is NULL and maxbytes is 0.
==================
*/
size_t UTF8_FromQuake (char *dst, size_t maxbytes, const char *src)
{
	size_t i, j, written;

	if (!maxbytes)
	{
		if (dst)
			return 0; // error
		for (i = 0, j = 0; src[i]; i++)
		{
			uint32_t codepoint = qchar_to_unicode[(unsigned char)src[i]];
			if (codepoint)
				j += UTF8_CodePointLength (codepoint);
		}
		return j + 1; // include terminator
	}

	--maxbytes;

	for (i = 0, j = 0; j < maxbytes && src[i]; i++)
	{
		uint32_t codepoint = qchar_to_unicode[(unsigned char)src[i]];
		if (!codepoint)
			continue;
		written = UTF8_WriteCodePoint (dst + j, maxbytes - j, codepoint);
		if (!written)
			break;
		j += written;
	}

	dst[j++] = '\0';

	return j;
}

static uint32_t UTF8_ReadCodePoint (const char **src)
{
	const char *text = *src;
	uint32_t	code, mask, i;
	uint8_t		first, cont;

	first = text[0];
	if (!first)
		return 0;

	if (first < 128)
	{
		*src = text + 1;
		return first;
	}

	if ((first & 0xC0) != 0xC0)
	{
		*src = text + 1;
		return UNICODE_UNKNOWN;
	}

	mask = first << 1;
	code = 0;
	for (i = 1; i < 6 && (mask & 0x80) != 0; i++, mask <<= 1)
	{
		cont = text[i];
		if (!cont)
		{
			*src = text + i;
			return UNICODE_UNKNOWN;
		}
		if ((cont & 0xC0) != 0x80)
		{
			*src = text + i + 1;
			return UNICODE_UNKNOWN;
		}
		code = (code << 6) | (cont & 63);
	}

	mask = ((1 << (7 - i)) - 1);
	code |= (first & mask) << (6 * (i - 1));
	*src = text + i;

	if (code > UNICODE_MAX ||																 // out of range
		i > 4 ||																			 // out of range/overlong
		(i == 2 && code < 0x80) || (i == 3 && code < 0x800) || (i == 4 && code < 0x10000) || // overlong
		code - 0xD800 < 2048)																 // surrogate
	{
		code = UNICODE_UNKNOWN;
	}

	return code;
}

static size_t UTF8_ToQuake (char *dst, size_t maxbytes, const char *src)
{
	size_t	 i, j;
	uint32_t cp;

	if (!unicode_translit_init)
	{
		// precomputed single-character/two-character transliterations
		for (i = 0; i < countof (unicode_translit_src); i++)
		{
			unicode_translit[unicode_translit_src[i].code][0] = unicode_translit_src[i].remap[0];
			unicode_translit[unicode_translit_src[i].code][1] = unicode_translit_src[i].remap[1];
		}

		// Quake-specific characters: we process the list in reverse order
		// so that codepoints used for both colored and non-colored qchars
		// end up being remapped to the non-colored versions
		// Note: 0 is not included
		for (i = countof (qchar_to_unicode) - 1; i > 0; i--)
		{
			if (qchar_to_unicode[i] >= 128 && qchar_to_unicode[i] < countof (unicode_translit))
			{
				unicode_translit[qchar_to_unicode[i]][0] = (char)i;
				unicode_translit[qchar_to_unicode[i]][1] = '\0';
			}
		}

		// map ASCII characters to themselves
		for (i = 0; i < 128; i++)
		{
			unicode_translit[i][0] = (char)i;
			unicode_translit[i][1] = '\0';
		}

		// Map all other characters to QCHAR_BOX (unknown character)
		for (i = 0; i < countof (unicode_translit); i++)
		{
			if (!unicode_translit[i][0])
			{
				unicode_translit[i][0] = QCHAR_BOX;
				unicode_translit[i][1] = '\0';
			}
		}

		unicode_translit_init = true;
	}

	if (!maxbytes)
	{
		if (dst)
			return 0; // error

		// Determine necessary output buffer size
		for (i = 0, j = 0; *src; i++)
		{
			// ASCII fast path
			while (*src && (byte)*src < 0x80)
			{
				src++;
				j++;
			}

			if (!*src)
				break;

			// A codepoint maps to either one or two Quake characters
			cp = UTF8_ReadCodePoint (&src);
			if (cp < countof (unicode_translit))
				j += unicode_translit[cp][1] != '\0' ? 2 : 1;
			else
				j++;
		}

		return j + 1; // include terminator
	}

	--maxbytes;

	for (i = 0; i < maxbytes && *src; i++)
	{
		// ASCII fast path
		while (*src && i < maxbytes && (byte)*src < 0x80)
			dst[i++] = *src++;

		if (!*src || i >= maxbytes)
			break;

		cp = UTF8_ReadCodePoint (&src);
		if (cp < countof (unicode_translit))
		{
			char c0 = unicode_translit[cp][0];
			char c1 = unicode_translit[cp][1];
			dst[i] = c0;
			if (c1 && i + 1 < maxbytes)
				dst[++i] = c1;
		}
		else
			dst[i] = QCHAR_BOX;
	}

	dst[i++] = '\0';

	return i;
}

char *q_strtrim (char *str)
{
	char *end;

	while (q_isspace ((unsigned char)*str))
		str++;

	end = str + strlen (str);
	while (end > str && q_isspace ((unsigned char)end[-1]))
		end--;
	*end = '\0';

	return str;
}

static bool is_in_char_set (char single_char, const char *char_set)
{
	const size_t char_set_size = strlen (char_set);

	for (size_t char_index = 0; char_index < char_set_size; char_index++)
	{
		if (char_set[char_index] == single_char)
			return true;
	}

	return false;
}

char **q_strsplit (char *str, const char *sep_set, size_t *nb_substr)
{
	size_t nb_sub_strings_max_size = 8;
	// if the nb_substr is NULL, we are just interested in splitting str-on place by '\0' ,
	// and not in returning the token starts at all.
	char **sub_strings = (nb_substr ? Mem_Alloc (nb_sub_strings_max_size * sizeof (char *)) : NULL);
	int	   nb_sub_strings = 0;

	size_t start_str_index = 0;

	// special case, gobble the leading sep characters:
	while (is_in_char_set (str[start_str_index], sep_set))
	{
		str[start_str_index] = 0;
		start_str_index++;
	}
	// the real start of the string is here
	char *str_start = &str[start_str_index];

	// always return a valid memory although  nb_sub_strings = 0
	// so that the caller is not burdened with NULL checks.
	//  TODO: or more explicit if it would ?
	assert (nb_sub_strings == 0);
	if (!str_start)
		return sub_strings;

	const size_t initial_str_size = strlen (str_start);

	for (size_t char_index = 0; char_index < initial_str_size; char_index++)
	{
		// find the next sep
		if (is_in_char_set (str_start[char_index], sep_set))
		{
			// goble consecutive seps, if any
			while (is_in_char_set (str_start[char_index], sep_set))
			{
				// split the original string
				str_start[char_index] = '\0';
				char_index++;
			}
			//
			if (sub_strings && char_index <= initial_str_size)
			{
				// make room
				if (nb_sub_strings >= nb_sub_strings_max_size)
				{
					nb_sub_strings_max_size = nb_sub_strings_max_size * 2;
					sub_strings = Mem_Realloc (sub_strings, nb_sub_strings_max_size * sizeof (char *));
				}
				// we found the first split, meaning the string before this split is indeed the first sub-string
				if (nb_sub_strings == 0)
					sub_strings[nb_sub_strings++] = &str_start[0];

				if (char_index < initial_str_size)
					sub_strings[nb_sub_strings++] = &str_start[char_index];
			}
		}
	}

	// no split, return the original string stripped from its leadings seps
	if (sub_strings && nb_sub_strings == 0)
		sub_strings[nb_sub_strings++] = &str_start[0];

	if (nb_substr)
		*nb_substr = nb_sub_strings;

	return sub_strings;
}

char *q_strdup (const char *str)
{
	size_t len = strlen (str) + 1;
	char  *newstr = (char *)Mem_Alloc (len);
	memcpy (newstr, str, len);
	return newstr;
}

int q_vsnprintf (char *str, size_t size, const char *format, va_list args)
{
	int ret;

	ret = vsnprintf (str, size, format, args);

	if (ret < 0)
		ret = (int)size;
	if (size == 0) /* no buffer */
		return ret;
	if ((size_t)ret >= size)
		str[size - 1] = '\0';

	return ret;
}

int q_snprintf (char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list argptr;

	va_start (argptr, format);
	ret = q_vsnprintf (str, size, format, argptr);
	va_end (argptr);

	return ret;
}

char *q_vstrcatf (char *input_str, const char *format, va_list args)
{
#define MIN_SIZE_POW 8
#define MIN_SIZE	 (1 << MIN_SIZE_POW)

	char *output_str = NULL;

	if (input_str == NULL)
		input_str = (char *)Mem_Alloc (MIN_SIZE);

	// We allways construct the dynamically allocated buffer a way we can
	// get its current size from the current input_str_size: allocated size is the next power of 2 strictly.
	const size_t input_str_size = strlen (input_str);
	const size_t input_str_allocated_size = (input_str_size + 1 < MIN_SIZE) ? MIN_SIZE : Q_nextPow2_Strict (input_str_size + 1);

	// we can push remaining_size more characters (including null)
	size_t remaining_size = input_str_allocated_size - (input_str_size + 1);

	//  First try : attempt to sprintf and append into the current input_str
	va_list argptr_first;
	va_copy (argptr_first, args);

	// Note : we need C99 conformant vsnprintf, returning the number of chars that are written, or would have been written
	// This is OK for MSVC 2015+ and all other compilers out there
	int expected_append_size = vsnprintf ((char *)(input_str + input_str_size), remaining_size, format, argptr_first);
	va_end (argptr_first);

	// Something wrong happened, return the original, unmodified.
	if (expected_append_size < 0)
	{
		output_str = (char *)input_str;
		output_str[input_str_size] = '\0';
		return output_str;
	}
	// Fits into the remaining room
	if (expected_append_size < remaining_size)
	{
		output_str = (char *)input_str;
		// the C99 conformant vsnprintf should already do this, but anyway
		output_str[input_str_size + expected_append_size] = '\0';
		return output_str;
	}

	// Second try : do not fit, so reallocate to the next power of 2 for the final size
	const size_t output_str_size = input_str_size + expected_append_size;
	size_t		 output_str_allocated_size = Q_nextPow2_Strict (output_str_size + 1);

	va_list argptr_second;
	va_copy (argptr_second, args);

	output_str = (char *)Mem_Realloc ((void *)input_str, output_str_allocated_size);
	output_str[input_str_size] = '\0';

	remaining_size = output_str_allocated_size - (input_str_size + 1);

	if (expected_append_size == vsnprintf ((char *)(output_str + input_str_size), remaining_size, format, argptr_second))
	{
		output_str[input_str_size + expected_append_size] = '\0';
	}

	va_end (argptr_second);

	return output_str;

#undef MIN_SIZE_POW
#undef MIN_SIZE
}

char *q_strcatf (char *input_str, const char *format, ...)
{
	va_list argptr;
	va_start (argptr, format);
	char *output_buffer = q_vstrcatf (input_str, format, argptr);
	va_end (argptr);

	return output_buffer;
}

int wildcmp (const char *wild, const char *string)
{ // case-insensitive string compare with wildcards. returns true for a match.
	while (*string)
	{
		if (*wild == '*')
		{
			if (*string == '/' || *string == '\\')
			{
				//* terminates if we get a match on the char following it, or if its a \ or / char
				wild++;
				continue;
			}
			if (wildcmp (wild + 1, string))
				return true;
			string++;
		}
		else if ((q_tolower (*wild) == q_tolower (*string)) || (*wild == '?'))
		{
			// this char matches
			wild++;
			string++;
		}
		else
		{
			// failure
			return false;
		}
	}

	while (*wild == '*')
	{
		wild++;
	}
	return !*wild;
}

void Info_RemoveKey (char *info, const char *key)
{ // only shrinks, so no need for max size.
	size_t keylen = strlen (key);

	while (*info)
	{
		char *l = info;
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			// skip the key name
			info += keylen + 1;
			// this is the old value for the key. skip over it
			while (*info && *info != '\\')
				info++;

			// okay, we found it. strip it out now.
			memmove (l, info, strlen (info) + 1);
			return;
		}
		else
		{
			// skip the key
			while (*info && *info != '\\')
				info++;

			// validate that its a value now
			if (*info++ != '\\')
				break; // error
			// skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
}
/* QSS-M's rule-info iterator, bounded at the native infostring boundary. */
qboolean Info_FindNextKey (const char *info, const char *prevkey, char *outkey, size_t outkeysize, char *outval, size_t outvalsize)
{
	const char *p = info;
	qboolean found_prev = !*prevkey;
	if (!outkeysize || !outvalsize)
		return false;
	*outkey = *outval = 0;
	while (*p == '\\')
	{
		const char *keystart = ++p, *keyend, *valstart;
		size_t kl, vl;
		while (*p && *p != '\\')
			++p;
		keyend = p;
		if (*p != '\\')
			return false;
		valstart = ++p;
		while (*p && *p != '\\')
			++p;
		kl = keyend - keystart;
		vl = p - valstart;
		if (!kl || kl >= outkeysize || vl >= outvalsize)
			continue; // Never return a truncated key that cannot be resumed.
		if (found_prev)
		{
			memcpy (outkey, keystart, kl);
			outkey[kl] = 0;
			memcpy (outval, valstart, vl);
			outval[vl] = 0;
			return true;
		}
		if (strlen (prevkey) == kl && !memcmp (keystart, prevkey, kl))
			found_prev = true;
	}
	return false;
}

void Info_SetKey (char *info, size_t infosize, const char *key, const char *val)
{
	size_t keylen = strlen (key);
	size_t vallen = strlen (val);

	Info_RemoveKey (info, key);

	if (vallen)
	{
		char *o = info + strlen (info);
		char *e = info + infosize - 1;

		if (!*key || strchr (key, '\\') || strchr (val, '\\'))
			Con_Warning ("Info_SetKey(%s): invalid key/value\n", key);
		else if (o + 2 + keylen + vallen >= e)
			Con_Warning ("Info_SetKey(%s): length exceeds max\n", key);
		else
		{
			*o++ = '\\';
			memcpy (o, key, keylen);
			o += keylen;
			*o++ = '\\';
			memcpy (o, val, vallen);
			o += vallen;

			*o = 0;
		}
	}
}
const char *Info_GetKey (const char *info, const char *key, char *out, size_t outsize)
{
	const char *r = out;
	size_t		keylen = strlen (key);

	outsize--;

	while (*info)
	{
		if (*info++ != '\\')
			break; // error / end-of-string

		if (!strncmp (info, key, keylen) && info[keylen] == '\\')
		{
			// skip the key name
			info += keylen + 1;
			// this is the value for the key. copy it out
			while (*info && *info != '\\' && outsize-- > 0)
				*out++ = *info++;
			break;
		}
		else
		{
			// skip the key
			while (*info && *info != '\\')
				info++;

			// validate that its a value now
			if (*info++ != '\\')
				break; // error
			// skip the value
			while (*info && *info != '\\')
				info++;
		}
	}
	*out = 0;
	return r;
}

void Info_Enumerate (const char *info, void (*cb) (void *ctx, const char *key, const char *value), void *cbctx)
{
	char   key[SERVER_INFO_STRING_SIZE];
	char   val[SERVER_INFO_STRING_SIZE];
	size_t kl, vl;
	while (*info)
	{
		kl = vl = 0;
		if (*info++ != '\\')
			break; // error / end-of-string

		// skip the key
		while (*info && *info != '\\')
		{
			if (kl < sizeof (key) - 1)
				key[kl++] = *info;
			info++;
		}

		// validate that its a value now
		if (*info++ != '\\')
			break; // error
		// skip the value
		while (*info && *info != '\\')
		{
			if (vl < sizeof (val) - 1)
				val[vl++] = *info;
			info++;
		}

		key[kl] = 0;
		val[vl] = 0;
		cb (cbctx, key, val);
	}
}
static void Info_Print_Callback (void *ctx, const char *key, const char *val)
{
	Con_Printf ("%20s: %s\n", key, val);
}
void Info_Print (const char *info)
{
	Info_Enumerate (info, Info_Print_Callback, NULL);
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/

short ShortSwap (short l)
{
	byte b1, b2;

	b1 = l & 255;
	b2 = (l >> 8) & 255;

	return ((unsigned short)b1 << 8) + b2;
}

short ShortNoSwap (short l)
{
	return l;
}

int LongSwap (int l)
{
	byte b1, b2, b3, b4;

	b1 = l & 255;
	b2 = (l >> 8) & 255;
	b3 = (l >> 16) & 255;
	b4 = (l >> 24) & 255;

	return ((unsigned int)b1 << 24) + ((unsigned int)b2 << 16) + ((unsigned int)b3 << 8) + b4;
}

int LongNoSwap (int l)
{
	return l;
}

float FloatSwap (float f)
{
	union
	{
		float f;
		byte  b[4];
	} dat1, dat2;

	dat1.f = f;
	dat2.b[0] = dat1.b[3];
	dat2.b[1] = dat1.b[2];
	dat2.b[2] = dat1.b[1];
	dat2.b[3] = dat1.b[0];
	return dat2.f;
}

float FloatNoSwap (float f)
{
	return f;
}

short (*BigShort) (short l) = ShortSwap;
short (*LittleShort) (short l) = ShortNoSwap;

int (*BigLong) (int l) = LongSwap;
int (*LittleLong) (int l) = LongNoSwap;

float (*BigFloat) (float l) = FloatSwap;
float (*LittleFloat) (float l) = FloatNoSwap;

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

void MSG_WriteChar (sizebuf_t *sb, int c)
{
	byte *buf;

#if defined(DEBUG) || defined(_DEBUG)
	if (c < -128 || c > 127)
		Host_Error ("MSG_WriteChar: range error = %i not in -128..127", c);
#endif

	buf = (byte *)SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteByte (sizebuf_t *sb, int c)
{
	byte *buf;

#if defined(DEBUG) || defined(_DEBUG)
	if (c < 0 || c > 255)
		Host_Error ("MSG_WriteByte: range error = %i not in 0..255", c);
#endif

	buf = (byte *)SZ_GetSpace (sb, 1);
	buf[0] = c;
}

void MSG_WriteShort (sizebuf_t *sb, int c)
{
	byte *buf;

#if defined(DEBUG) || defined(_DEBUG)
	// it is apparently used to encode signed OR unsigned shorts...
	if (c < INT16_MIN || c > UINT16_MAX)
		Host_Error ("MSG_WriteShort: range error = %i not in -32768..65535", c);
#endif

	buf = (byte *)SZ_GetSpace (sb, 2);
	buf[0] = c & 0xff;
	buf[1] = c >> 8;
}

void MSG_WriteLong (sizebuf_t *sb, int c)
{
	byte *buf;

	buf = (byte *)SZ_GetSpace (sb, 4);
	buf[0] = c & 0xff;
	buf[1] = (c >> 8) & 0xff;
	buf[2] = (c >> 16) & 0xff;
	buf[3] = c >> 24;
}

void MSG_WriteUInt64 (sizebuf_t *sb, unsigned long long c)
{ // 0* 10*,*, 110*,*,* etc, up to 0xff followed by 8 continuation bytes
	byte			  *buf;
	int				   b = 0;
	unsigned long long l = 128;
	while (c > l - 1u)
	{ // count the extra bytes we need
		b++;
		l <<= 7; // each byte we add gains 8 bits, but we spend one on length.
	}
	buf = (byte *)SZ_GetSpace (sb, 1 + b);
	*buf++ = 0xffu << (8 - b) | (c >> (b * 8));
	while (b-- > 0)
		*buf++ = (c >> (b * 8)) & 0xff;
}
void MSG_WriteInt64 (sizebuf_t *sb, long long c)
{ // move the sign bit into the low bit and avoid sign extension for more efficient length coding.
	if (c < 0)
		MSG_WriteUInt64 (sb, ((unsigned long long)(-1 - c) << 1) | 1);
	else
		MSG_WriteUInt64 (sb, c << 1);
}

void MSG_WriteFloat (sizebuf_t *sb, float f)
{
	union
	{
		float f;
		int	  l;
	} dat;

	dat.f = f;
	dat.l = LittleLong (dat.l);

	SZ_Write (sb, &dat.l, 4);
}

void MSG_WriteDouble (sizebuf_t *sb, double f)
{
	union
	{
		double	f;
		int64_t l;
	} dat;
	byte *o = SZ_GetSpace (sb, sizeof (f));
	dat.f = f;

	o[0] = dat.l >> 0;
	o[1] = dat.l >> 8;
	o[2] = dat.l >> 16;
	o[3] = dat.l >> 24;
	o[4] = dat.l >> 32;
	o[5] = dat.l >> 40;
	o[6] = dat.l >> 48;
	o[7] = dat.l >> 56;
}

void MSG_WriteString (sizebuf_t *sb, const char *s)
{
	if (!s)
		SZ_Write (sb, "", 1);
	else
		SZ_Write (sb, s, strlen (s) + 1);
}
void MSG_WriteStringUnterminated (sizebuf_t *sb, const char *s)
{
	SZ_Write (sb, s, strlen (s));
}

// johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
void MSG_WriteCoord16 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, Q_rint (f * 8));
}

// johnfitz -- 16.8 fixed point coords, max range +-32768
void MSG_WriteCoord24 (sizebuf_t *sb, float f)
{
	MSG_WriteShort (sb, f);
	MSG_WriteByte (sb, (int)(f * 255) % 255);
}

// johnfitz -- 32-bit float coords
void MSG_WriteCoord32f (sizebuf_t *sb, float f)
{
	MSG_WriteFloat (sb, f);
}

void MSG_WriteCoord (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_INT32COORD)
		MSG_WriteLong (sb, Q_rint (f * 16));
	else if (flags & PRFL_24BITCOORD)
		MSG_WriteCoord24 (sb, f);
	else
		MSG_WriteCoord16 (sb, f);
}

void MSG_WriteAngle (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else if (flags & PRFL_SHORTANGLE)
		MSG_WriteShort (sb, Q_rint (f * 65536.0 / 360.0) & 65535);
	else
		MSG_WriteByte (sb, Q_rint (f * 256.0 / 360.0) & 255); // johnfitz -- use Q_rint instead of (int)	}
}

// johnfitz -- for PROTOCOL_FITZQUAKE
void MSG_WriteAngle16 (sizebuf_t *sb, float f, unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		MSG_WriteFloat (sb, f);
	else
		MSG_WriteShort (sb, Q_rint (f * 65536.0 / 360.0) & 65535);
}
// johnfitz

// spike -- for PEXT2_REPLACEMENTDELTAS
void MSG_WriteEntity (sizebuf_t *sb, unsigned int entnum, unsigned int pext2)
{
	// high short, low byte
	if (entnum > 0x7fff && (pext2 & PEXT2_REPLACEMENTDELTAS))
	{
		MSG_WriteShort (sb, 0x8000 | (entnum >> 8));
		MSG_WriteByte (sb, entnum & 0xff);
	}
	else
		MSG_WriteShort (sb, entnum);
}

//
// reading functions
//
int		 msg_readcount;
qboolean msg_badread;

void MSG_BeginReading (void)
{
	msg_readcount = 0;
	msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
int MSG_ReadChar (void)
{
	int c;

	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (signed char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadByte (void)
{
	int c;

	if (msg_readcount + 1 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (unsigned char)net_message.data[msg_readcount];
	msg_readcount++;

	return c;
}

int MSG_ReadShort (void)
{
	int c;

	if (msg_readcount + 2 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (short)(net_message.data[msg_readcount] + (net_message.data[msg_readcount + 1] << 8));

	msg_readcount += 2;

	return c;
}

int MSG_ReadLong (void)
{
	uint32_t c;

	if (msg_readcount + 4 > net_message.cursize)
	{
		msg_badread = true;
		return -1;
	}

	c = (uint32_t)net_message.data[msg_readcount] + ((uint32_t)(net_message.data[msg_readcount + 1]) << 8) +
		((uint32_t)(net_message.data[msg_readcount + 2]) << 16) + ((uint32_t)(net_message.data[msg_readcount + 3]) << 24);

	msg_readcount += 4;

	return c;
}

unsigned long long MSG_ReadUInt64 (void)
{ // 0* 10*,*, 110*,*,* etc, up to 0xff followed by 8 continuation bytes
	byte			   l = 0x80, v, b = 0;
	unsigned long long r;
	v = MSG_ReadByte ();
	for (; v & l; l >>= 1)
	{
		v -= l;
		b++;
	}
	r = v << (b * 8);
	while (b-- > 0)
		r |= MSG_ReadByte () << (b * 8);
	return r;
}
long long MSG_ReadInt64 (void)
{ // we do some fancy bit recoding for more efficient length coding.
	unsigned long long c = MSG_ReadUInt64 ();
	if (c & 1)
		return -1 - (long long)(c >> 1);
	else
		return (long long)(c >> 1);
}

float MSG_ReadFloat (void)
{
	union
	{
		byte  b[4];
		float f;
		int	  l;
	} dat;

	// Private command headers may end before the diagnostic time or angles.
	// Keep float reads inside the message just like the integer primitives.
	if (msg_readcount < 0 || msg_readcount > net_message.cursize || net_message.cursize - msg_readcount < 4)
	{
		msg_badread = true;
		return -1;
	}

	dat.b[0] = net_message.data[msg_readcount];
	dat.b[1] = net_message.data[msg_readcount + 1];
	dat.b[2] = net_message.data[msg_readcount + 2];
	dat.b[3] = net_message.data[msg_readcount + 3];
	msg_readcount += 4;

	dat.l = LittleLong (dat.l);

	return dat.f;
}
float MSG_ReadDouble (void)
{
	union
	{
		double	 f;
		uint64_t l;
	} dat;

	dat.l = ((uint64_t)net_message.data[msg_readcount] << 0) | ((uint64_t)net_message.data[msg_readcount + 1] << 8) |
			((uint64_t)net_message.data[msg_readcount + 2] << 16) | ((uint64_t)net_message.data[msg_readcount + 3] << 24) |
			((uint64_t)net_message.data[msg_readcount + 4] << 32) | ((uint64_t)net_message.data[msg_readcount + 5] << 40) |
			((uint64_t)net_message.data[msg_readcount + 6] << 48) | ((uint64_t)net_message.data[msg_readcount + 7] << 56);
	msg_readcount += 8;

	return dat.f;
}

const char *MSG_ReadStringBuffer (char *string, size_t string_size)
{
	int			c;
	size_t		l;

	if (!string || !string_size)
		Sys_Error ("MSG_ReadStringBuffer: invalid buffer");

	l = 0;
	do
	{
		c = MSG_ReadByte ();
		if (c == -1 || c == 0)
			break;
		if (l < string_size - 1)
			string[l++] = c;
	} while (1);

	string[l] = 0;

	return string;
}

const char *MSG_ReadString (void)
{
	static char string[MSG_READSTRING_SIZE];

	return MSG_ReadStringBuffer (string, sizeof (string));
}

// johnfitz -- original behavior, 13.3 fixed point coords, max range +-4096
float MSG_ReadCoord16 (void)
{
	return MSG_ReadShort () * (1.0 / 8);
}

// johnfitz -- 16.8 fixed point coords, max range +-32768
float MSG_ReadCoord24 (void)
{
	return MSG_ReadShort () + MSG_ReadByte () * (1.0 / 255);
}

// johnfitz -- 32-bit float coords
float MSG_ReadCoord32f (void)
{
	return MSG_ReadFloat ();
}

float MSG_ReadCoord (unsigned int flags)
{
	if (flags & PRFL_FLOATCOORD)
		return MSG_ReadFloat ();
	else if (flags & PRFL_INT32COORD)
		return MSG_ReadLong () * (1.0 / 16.0);
	else if (flags & PRFL_24BITCOORD)
		return MSG_ReadCoord24 ();
	else
		return MSG_ReadCoord16 ();
}

float MSG_ReadAngle (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat ();
	else if (flags & PRFL_SHORTANGLE)
		return MSG_ReadShort () * (360.0 / 65536);
	else
		return MSG_ReadChar () * (360.0 / 256);
}

// johnfitz -- for PROTOCOL_FITZQUAKE
float MSG_ReadAngle16 (unsigned int flags)
{
	if (flags & PRFL_FLOATANGLE)
		return MSG_ReadFloat (); // make sure
	else
		return MSG_ReadShort () * (360.0 / 65536);
}
// johnfitz

unsigned int MSG_ReadEntity (unsigned int pext2)
{
	unsigned int e = (unsigned short)MSG_ReadShort ();
	if (pext2 & PEXT2_REPLACEMENTDELTAS)
	{
		if (e & 0x8000)
		{
			e = (e & 0x7fff) << 8;
			e |= MSG_ReadByte ();
		}
	}
	return e;
}

//===========================================================================

void SZ_Alloc (sizebuf_t *buf, int startsize)
{
	if (startsize < 256)
		startsize = 256;
	buf->data = (byte *)Mem_Alloc (startsize);
	buf->maxsize = startsize;
	buf->cursize = 0;
}

void SZ_Free (sizebuf_t *buf)
{
	Mem_Free (buf->data);
	buf->data = NULL;
	buf->maxsize = 0;
	buf->cursize = 0;
}

void SZ_Clear (sizebuf_t *buf)
{
	buf->cursize = 0;
	buf->overflowed = false;
}

void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void *data;

	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Host_Error ("SZ_GetSpace: overflow without allowoverflow set"); // ericw -- made Host_Error to be less annoying

		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);

		Con_Printf ("SZ_GetSpace: overflow\n");
		SZ_Clear (buf);
		buf->overflowed = true;
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;

	return data;
}

void SZ_Write (sizebuf_t *buf, const void *data, int length)
{
	memcpy (SZ_GetSpace (buf, length), data, length);
}

void SZ_Print (sizebuf_t *buf, const char *data)
{
	int len = strlen (data) + 1;

	if (buf->data[buf->cursize - 1])
	{ /* no trailing 0 */
		memcpy ((byte *)SZ_GetSpace (buf, len), data, len);
	}
	else
	{ /* write over trailing 0 */
		memcpy ((byte *)SZ_GetSpace (buf, len - 1) - 1, data, len);
	}
}

//============================================================================

/*
============
COM_SkipPath
============
*/
const char *COM_SkipPath (const char *pathname)
{
	const char *last;

	last = pathname;
	while (*pathname)
	{
		if (*pathname == '/')
			last = pathname + 1;
		pathname++;
	}
	return last;
}

/*
============
COM_SkipSpace
============
*/
const char *COM_SkipSpace (const char *str)
{
	while (q_isspace ((unsigned char)*str))
		str++;
	return str;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension (const char *in, char *out, size_t outsize)
{
	int length;

	if (!*in)
	{
		*out = '\0';
		return;
	}
	if (in != out) /* copy when not in-place editing */
		q_strlcpy (out, in, outsize);
	length = (int)strlen (out) - 1;
	while (length > 0 && out[length] != '.')
	{
		--length;
		if (out[length] == '/' || out[length] == '\\')
			return; /* no extension */
	}
	if (length > 0)
		out[length] = '\0';
}

/*
============
COM_FileGetExtension - doesn't return NULL
============
*/
const char *COM_FileGetExtension (const char *in)
{
	const char *src;
	size_t		len;

	len = strlen (in);
	if (len < 2) /* nothing meaningful */
		return "";

	src = in + len - 1;
	while (src != in && src[-1] != '.')
		src--;
	if (src == in || strchr (src, '/') != NULL || strchr (src, '\\') != NULL)
		return ""; /* no extension, or parent directory has a dot */

	return src;
}

/*
============
COM_ExtractExtension
============
*/
void COM_ExtractExtension (const char *in, char *out, size_t outsize)
{
	const char *ext = COM_FileGetExtension (in);
	if (!*ext)
		*out = '\0';
	else
		q_strlcpy (out, ext, outsize);
}

/*
============
COM_FileBase
take 'somedir/otherdir/filename.ext',
write only 'filename' to the output
============
*/
void COM_FileBase (const char *in, char *out, size_t outsize)
{
	const char *dot, *slash, *s;

	s = in;
	slash = in;
	dot = NULL;
	while (*s)
	{
		if (*s == '/' || *s == '\\')
			slash = s + 1;
		if (*s == '.')
			dot = s;
		s++;
	}
	if (dot == NULL)
		dot = s;

	if (dot - slash < 2)
		q_strlcpy (out, "?model?", outsize);
	else
	{
		size_t len = dot - slash;
		if (len >= outsize)
			len = outsize - 1;
		memcpy (out, slash, len);
		out[len] = '\0';
	}
}

/*
==================
COM_DefaultExtension
if path doesn't have a .EXT, append extension
(extension should include the leading ".")
==================
*/
#if 0 /* can be dangerous */
void COM_DefaultExtension (char *path, const char *extension, size_t len)
{
	char	*src;

	if (!*path) return;
	src = path + strlen(path) - 1;

	while (*src != '/' && *src != '\\' && src != path)
	{
		if (*src == '.')
			return; // it has an extension
		src--;
	}

	q_strlcat(path, extension, len);
}
#endif

/*
==================
COM_AddExtension
if path extension doesn't match .EXT, append it
(extension should include the leading ".")
==================
*/
void COM_AddExtension (char *path, const char *extension, size_t len)
{
	if (strcmp (COM_FileGetExtension (path), extension + 1) != 0)
		q_strlcat (path, extension, len);
}

/*
==============
COM_ParseExBuffer

Parse a token out of a string into a caller-provided bounded buffer.
If supplied, parse_error distinguishes overflow/invalid storage from normal
end-of-input (including trailing comments), which also returns NULL.

The mode argument controls how overflow is handled:
- CPE_NOTRUNC:		return NULL (abort parsing)
- CPE_ALLOWTRUNC:	truncate token (ignore the extra characters in this token)
==============
*/
const char *COM_ParseExBufferSpan (const char *data, cpe_mode mode, char *token,
	size_t token_size, qboolean *parse_error, const char **token_start)
{
	int c;
	size_t len;

	len = 0;
	if (token_start)
		*token_start = NULL;
	if (parse_error)
		*parse_error = false;
	if (!token || !token_size)
		goto parseerror;
	token[0] = 0;

	if (!data)
		return NULL;

// skip whitespace
skipwhite:
	while ((c = *data) <= ' ')
	{
		if (c == 0)
			return NULL; // end of file
		data++;
	}

	// skip // comments
	if (c == '/' && data[1] == '/')
	{
		while (*data && *data != '\n')
			data++;
		goto skipwhite;
	}

	// skip /*..*/ comments
	if (c == '/' && data[1] == '*')
	{
		data += 2;
		while (*data && !(*data == '*' && data[1] == '/'))
			data++;
		if (*data)
			data += 2;
		goto skipwhite;
	}
	if (token_start)
		*token_start = data;

	// handle quoted strings specially
	if (c == '\"')
	{
		data++;
		while (1)
		{
			if ((c = *data) != 0)
				++data;
			if (c == '\"' || !c)
			{
				token[len] = 0;
				return data;
			}
			if (len < token_size - 1)
				token[len++] = c;
			else if (mode == CPE_NOTRUNC)
				goto parseerror;
		}
	}

	// parse single characters
	if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' || c == ':')
	{
		if (len < token_size - 1)
			token[len++] = c;
		else if (mode == CPE_NOTRUNC)
			goto parseerror;
		token[len] = 0;
		return data + 1;
	}

	// parse a regular word
	do
	{
		if (len < token_size - 1)
			token[len++] = c;
		else if (mode == CPE_NOTRUNC)
			goto parseerror;
		data++;
		c = *data;
		/* commented out the check for ':' so that ip:port works */
		if (c == '{' || c == '}' || c == '(' || c == ')' || c == '\'' /* || c == ':' */)
			break;
	} while (c > 32);

	token[len] = 0;
	return data;

parseerror:
	if (parse_error)
		*parse_error = true;
	return NULL;
}

const char *COM_ParseExBuffer (const char *data, cpe_mode mode, char *token,
	size_t token_size, qboolean *parse_error)
{
	return COM_ParseExBufferSpan (data, mode, token, token_size, parse_error,
		NULL);
}

const char *COM_ParseEx (const char *data, cpe_mode mode)
{
	// Preserve the historical global token capacity for existing callers.
	return COM_ParseExBuffer (data, mode, com_token, countof (com_token), NULL);
}

/*
==============
COM_Parse

Parse a token out of a string

Return NULL in case of overflow
==============
*/
const char *COM_Parse (const char *data)
{
	return COM_ParseEx (data, CPE_NOTRUNC);
}

/*
================
COM_ParseLine
================
*/
qboolean COM_ParseLine (const char **str, stringview_t *line)
{
	const char *p;

	if (!str || !*str)
		return false;

	p = *str;
	if (line)
		line->data = p;
	while (*p && *p != '\n')
		p++;
	if (line)
		line->len = p - line->data;

	*str = (*p == '\n') ? p + 1 : NULL;
	return true;
}

/*
================
COM_ParseMutableLine
================
*/
qboolean COM_ParseMutableLine (char **str, char **line)
{
	stringview_t view;

	if (!COM_ParseLine ((const char **)str, &view))
		return false;

	if (line)
	{
		char *result = (char *)view.data;
		result[view.len] = '\0';
		*line = result;
	}

	return true;
}

/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
int COM_CheckParmNext (int last, const char *parm)
{
	int i;

	for (i = last + 1; i < com_argc; i++)
	{
		if (!com_argv[i])
			continue; // NEXTSTEP sometimes clears appkit vars.
		if (!strcmp (parm, com_argv[i]))
			return i;
	}

	return 0;
}
int COM_CheckParm (const char *parm)
{
	return COM_CheckParmNext (0, parm);
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
static void COM_CheckRegistered (void)
{
	int h;
	int i;

	COM_OpenFile ("gfx/pop.lmp", &h, NULL);

	if (h == -1)
	{
		Cvar_SetROM ("registered", "0");
		Con_Printf ("Playing shareware version.\n");
		if (com_modified)
			Sys_Error (
				"You must have the registered version to use modified games.\n\n"
				"Basedir is: %s\n\n"
				"Check that this has an " GAMENAME " subdirectory containing pak0.pak and pak1.pak, "
				"or use the -basedir command-line option to specify another directory.",
				com_basedir);
		return;
	}

	COM_CloseFile (h);

	for (i = 0; com_cmdline[i]; i++)
	{
		if (com_cmdline[i] != ' ')
			break;
	}

	Cvar_SetROM ("cmdline", &com_cmdline[i]);
	Cvar_SetROM ("registered", "1");
	Con_Printf ("Playing registered version.\n");
}

/*
================
COM_InitArgv
================
*/
void COM_InitArgv (int argc, char **argv)
{
	int i, j, n;
	qboolean truncated = false;

	// reconstitute the command line for the cmdline externally visible cvar
	n = 0;

	for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++)
	{
		i = 0;

		while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i])
		{
			com_cmdline[n++] = argv[j][i++];
		}
		if (argv[j][i] || (n == CMDLINE_LENGTH - 1 && j + 1 < argc && j + 1 < MAX_NUM_ARGVS))
		{
			truncated = true;
			break;
		}

		if (n < (CMDLINE_LENGTH - 1))
			com_cmdline[n++] = ' ';
		else
			break;
	}

	com_cmdline[n] = 0;
	if (truncated)
	{
		// Never run a partly reconstructed +command. Option parsing retains argv.
		com_cmdline[0] = 0;
		Con_Warning ("Command line exceeds %d bytes; skipping +commands\n", CMDLINE_LENGTH - 1);
	}
	else if (n > 0 && com_cmdline[n - 1] == ' ')
		com_cmdline[n - 1] = 0; // johnfitz -- kill the trailing space

	Con_Printf ("Command line: %s\n", com_cmdline);

	for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc); com_argc++)
	{
		largv[com_argc] = argv[com_argc];
		if (!strcmp ("-safe", argv[com_argc]))
			safemode = 1;
	}

	largv[com_argc] = argvdummy;
	com_argv = largv;

	if (COM_CheckParm ("-rogue"))
	{
		rogue = true;
		standard_quake = false;
	}

	if (COM_CheckParm ("-hipnotic") || COM_CheckParm ("-quoth")) // johnfitz -- "-quoth" support
	{
		hipnotic = true;
		standard_quake = false;
	}
}

entity_state_t nullentitystate;
static void	   COM_SetupNullState (void)
{
	// the null state has some specific default values
	//	nullentitystate.drawflags = /*SCALE_ORIGIN_ORIGIN*/96;
	nullentitystate.colormod[0] = 32;
	nullentitystate.colormod[1] = 32;
	nullentitystate.colormod[2] = 32;
	//	nullentitystate.glowmod[0] = 32;
	//	nullentitystate.glowmod[1] = 32;
	//	nullentitystate.glowmod[2] = 32;
	nullentitystate.colormap = 0;
	nullentitystate.alpha = ENTALPHA_DEFAULT; // fte has 255 by default, with 0 for invisible. fitz uses 1 for invisible, 0 default, and 255=full alpha
	nullentitystate.scale = ENTSCALE_DEFAULT;
	//	nullentitystate.solidsize = 0;//ES_SOLID_BSP;
	nullentitystate.solidsize = ES_SOLID_NOT;
}

/*
================
COM_WordLength
================
*/
int COM_WordLength (const char *text)
{
	const char *start = text;
	while (*text && !q_isspace (*text))
		text++;
	return text - start;
}

/*
================
COM_AdvanceLineWrapped

Advances text by as much as possible until the maxchars limit is hit,
avoiding splitting words if possible.

Returns the length of the consumed text, excluding a potential trailing space or newline.
================
*/
int COM_AdvanceLineWrapped (const char **text, int maxchars)
{
	const char *str = *text;
	int			i;

	for (i = 0; i < maxchars && str[i]; /**/)
	{
		if (str[i] == '\n')
		{
			*text += i + 1;
			return i;
		}

		// new word
		if (!q_isspace (str[i]) && (i == 0 || q_isspace (str[i - 1])))
		{
			int len = COM_WordLength (str + i);
			// split word if longer than given limit
			if (len > maxchars)
			{
				*text += maxchars;
				return maxchars;
			}
			// not enough space left? push word to next line
			if (i + len > maxchars)
			{
				*text += i;
				return i;
			}
			// word fits, continue
			i += len;
		}
		else
			i++;
	}

	// avoid starting next line with a space
	*text += i + (q_isspace (str[i]) ? 1 : 0);

	return i;
}

/*
================
COM_WordWrap

Copies src to dst by word-wrapping lines longer than maxcols, preserving existing linefeeds.
If maxcols <= 0 no wrapping is performed (plain string copy).
dst is always NUL terminated if dstsize > 0.
================
*/
void COM_WordWrap (char *dst, const char *src, size_t dstsize, int maxcols)
{
	size_t ofs;

	if (maxcols <= 0)
	{
		q_strlcpy (dst, src, dstsize);
		return;
	}

	if (!dstsize)
		return;
	// reserve space for terminating NUL
	--dstsize;

	ofs = 0;
	while (*src)
	{
		const char *start = src;
		size_t		len = (size_t)COM_AdvanceLineWrapped (&src, maxcols);
		size_t		remaining = dstsize - ofs;
		len = q_min (len, remaining);
		memcpy (dst + ofs, start, len);
		ofs += len;
		if (ofs + 1 < dstsize && *src)
			dst[ofs++] = '\n';
	}

	dst[ofs++] = '\0';
}

/*
================
COM_Init
================
*/
void COM_Init (void)
{
	uint32_t uint_value = 0x12345678;
	uint8_t	 bytes[4];
	memcpy (bytes, &uint_value, sizeof (uint32_t));

	/*    U N I X */

	/*
	BE_ORDER:  12 34 56 78
		   U  N  I  X

	LE_ORDER:  78 56 34 12
		   X  I  N  U

	PDP_ORDER: 34 12 78 56
		   N  U  X  I
	*/
	if (bytes[0] != 0x78 || bytes[1] != 0x56 || bytes[2] != 0x34 || bytes[3] != 0x12)
		Sys_Error ("Unsupported endianism. Only little endian is supported");

	int validation_arg = COM_CheckParm ("-validation");
	if (!validation_arg)
		validation_arg = COM_CheckParm ("-v");
	if (validation_arg)
	{
		vulkan_globals.validation = 1;
		if (validation_arg < com_argc - 1 && q_isdigit (com_argv[validation_arg + 1][0]))
			vulkan_globals.validation = CLAMP (0, atoi (com_argv[validation_arg + 1]), 3);
	}

	if (COM_CheckParm ("-multiuser"))
		multiuser = true;

	COM_SetupNullState ();
}

/*
============
va

does a varargs printf into a temp buffer. Cycles between
VA_NUM_BUFFS different static buffers.
FIXME: make this buffer size safe someday
============
*/
#define VA_NUM_BUFFS 8
#if (MAX_OSPATH >= 1024)
#define VA_BUFFERLEN MAX_OSPATH
#else
#define VA_BUFFERLEN 1024
#endif

static char *get_va_buffer (void)
{
	static THREAD_LOCAL char va_buffers[VA_NUM_BUFFS][VA_BUFFERLEN];
	static THREAD_LOCAL int	 buffer_idx = 0;
	buffer_idx = (buffer_idx + 1) & (VA_NUM_BUFFS - 1);
	return va_buffers[buffer_idx];
}

char *va (const char *format, ...)
{
	va_list argptr;
	char   *va_buf;

	va_buf = get_va_buffer ();
	va_start (argptr, format);
	q_vsnprintf (va_buf, VA_BUFFERLEN, format, argptr);
	va_end (argptr);

	return va_buf;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

THREAD_LOCAL qfilesize_t com_filesize;

//
// on-disk pakfile
//
typedef struct
{
	char name[56];
	int	 filepos, filelen;
} dpackfile_t;

typedef struct
{
	char id[4];
	int	 dirofs;
	int	 dirlen;
} dpackheader_t;

#define MAX_FILES_IN_PACK 2048

static qboolean COM_ValidatePackDirectoryEntries (const dpackfile_t *directory,
	int count, qfilesize_t filesize)
{
	int i;

	for (i = 0; i < count; i++)
	{
		int filepos, filelen;

		if (!memchr (directory[i].name, '\0', sizeof (directory[i].name)))
			return false;
		filepos = LittleLong (directory[i].filepos);
		filelen = LittleLong (directory[i].filelen);
		if (filepos < 0 || filelen < 0 ||
			(qfilesize_t)filepos > filesize ||
			(qfilesize_t)filelen > filesize - (qfilesize_t)filepos)
			return false;
	}
	return true;
}

char			 com_gamenames[1024]; // eg: "hipnotic;quoth;warp" ... no id1
char			 com_gamedir[MAX_OSPATH];
char			 com_basedir[MAX_OSPATH];
char			 com_basedirs[MAX_BASEDIRS][MAX_OSPATH]; // all content roots in mount order: extras (e.g. the Nightdive
														 // add-on dir), the main basedir, the userdir (write target) last
int				 com_numbasedirs;
THREAD_LOCAL int file_from_pak; // ZOID: global indicating that file came from a pak
static char		 com_rerelease_localization_pack[MAX_OSPATH];

/* Keep the optional rerelease source outside the normal filesystem: only the
 * English localization table may be read from this pack, and the pack is never
 * mounted. */
static qboolean COM_SetRereleaseLocalizationPack (const char *root)
{
	char filename[MAX_OSPATH];
	int	 written;

	if (!root || !*root)
		return false;
	written = q_snprintf (filename, sizeof (filename), "%s/id1/pak0.pak", root);
	if (written < 0 || (size_t)written >= sizeof (filename) || Sys_FileType (filename) != FS_ENT_FILE)
		return false;

	q_strlcpy (com_rerelease_localization_pack, filename, sizeof (com_rerelease_localization_pack));
	return true;
}

/*
=================
COM_LoadRereleaseLocalization

Read only the English localization table from the separately selected
rerelease pack. Malformed optional input fails closed without mounting it.
=================
*/
static char *COM_LoadRereleaseLocalization (const char *filename)
{
	dpackheader_t header;
	dpackfile_t *directory = NULL;
	char		*data = NULL;
	qfilesize_t packsize;
	int		 handle = -1;
	int		 dirofs, dirlen, numfiles, i;

	if (!com_rerelease_localization_pack[0] || strcmp (filename, "localization/loc_english.txt"))
		return NULL;

	packsize = Sys_FileOpenRead (com_rerelease_localization_pack, &handle);
	if (packsize < (qfilesize_t)sizeof (header) ||
		Sys_FileRead (handle, &header, sizeof (header)) != sizeof (header) ||
		memcmp (header.id, "PACK", 4))
		goto done;

	dirofs = LittleLong (header.dirofs);
	dirlen = LittleLong (header.dirlen);
	if (dirofs < (int)sizeof (header) || dirlen <= 0 ||
		dirlen % (int)sizeof (dpackfile_t) ||
		dirlen > MAX_FILES_IN_PACK * (int)sizeof (dpackfile_t) ||
		(qfilesize_t)dirofs > packsize ||
		(qfilesize_t)dirlen > packsize - (qfilesize_t)dirofs)
		goto done;

	numfiles = dirlen / (int)sizeof (dpackfile_t);
	directory = (dpackfile_t *)Mem_AllocNonZero ((size_t)dirlen);
	if (!directory || Sys_FileSeek (handle, dirofs) != 0 ||
		Sys_FileRead (handle, directory, dirlen) != dirlen ||
		!COM_ValidatePackDirectoryEntries (directory, numfiles, packsize))
		goto done;

	for (i = 0; i < numfiles; i++)
	{
		int filepos, filelen;

		if (strcmp (directory[i].name, filename))
			continue;
		filepos = LittleLong (directory[i].filepos);
		filelen = LittleLong (directory[i].filelen);
		if (filelen <= 0 || filelen > 16 * 1024 * 1024)
			break;

		data = (char *)Mem_AllocNonZero ((size_t)filelen + 1);
		if (!data)
			break;
		if (Sys_FileSeek (handle, filepos) != 0 || Sys_FileRead (handle, data, filelen) != filelen)
		{
			Mem_Free (data);
			data = NULL;
			break;
		}
		data[filelen] = 0;
		break;
	}

done:
	if (directory)
		Mem_Free (directory);
	if (handle != -1)
		Sys_FileClose (handle);
	return data;
}

/*
=================
COM_AddBaseDir

Registers a content root; game directories are looked up in all roots,
with later-added roots taking precedence over earlier ones
=================
*/
void COM_AddBaseDir (const char *dir)
{
	int i;
	for (i = 0; i < com_numbasedirs; i++)
		if (!q_strcasecmp (com_basedirs[i], dir))
			return;
	if (com_numbasedirs == MAX_BASEDIRS)
		Sys_Error ("COM_AddBaseDir: too many base directories");
	q_strlcpy (com_basedirs[com_numbasedirs++], dir, sizeof (com_basedirs[0]));
}

searchpath_t *com_searchpaths;
searchpath_t *com_base_searchpaths;

/*
============
COM_Path_f
============
*/
static void COM_Path_f (void)
{
	searchpath_t *s;

	Con_Printf ("Current search path:\n");
	for (s = com_searchpaths; s; s = s->next)
	{
		if (s->pack)
		{
			Con_Printf ("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
		}
		else
			Con_Printf ("%s\n", s->filename);
	}
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile (const char *filename, const void *data, int len)
{
	int	 handle;
	char name[MAX_OSPATH];

	q_snprintf (name, sizeof (name), "%s/%s", com_gamedir, filename);

	handle = Sys_FileOpenWrite (name);
	if (handle == -1)
	{
		Sys_Printf ("COM_WriteFile: failed on %s\n", name);
		return;
	}

	Sys_Printf ("COM_WriteFile: %s\n", name);
	Sys_FileWrite (handle, data, len);
	Sys_FileClose (handle);
}

/*
================
COM_filelength
================
*/
static qfilesize_t COM_filelength (FILE *f)
{
	return Sys_filelength (f);
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
If neither of file or handle is set, this
can be used for detecting a file's presence.
===========
*/
/* A separately mounted rerelease pack must never supply gameplay files to a
 * classic installation. Its MD5 meshes, animations and indexed skins are a
 * lowest-priority replacement-model source only. */
qboolean COM_IsRereleaseModelAsset (const char *filename)
{
	const char *extension;

	if (q_strncasecmp (filename, "progs/", 6))
		return false;
	extension = COM_FileGetExtension (filename);
	return !q_strcasecmp (extension, "md5mesh") ||
		!q_strcasecmp (extension, "md5anim") ||
		!q_strcasecmp (extension, "lmp");
}

static qfilesize_t COM_FindFile (const char *filename, int *handle, FILE **file, unsigned int *path_id)
{
	searchpath_t *search;
	char		  netpath[MAX_OSPATH];
	pack_t		 *pak;
	int			  i;

	if (file && handle)
		Sys_Error ("COM_FindFile: both handle and file set");

	file_from_pak = 0;

	//
	// search through the path, one element at a time
	//
	for (search = com_searchpaths; search; search = search->next)
	{
		if (search->rerelease_models && !COM_IsRereleaseModelAsset (filename))
			continue;
		if (search->pack) /* look through all the pak file elements */
		{
			pak = search->pack;
			for (i = 0; i < pak->numfiles; i++)
			{
				if (strcmp (pak->files[i].name, filename) != 0)
					continue;
				// found it!
				com_filesize = pak->files[i].filelen;
				file_from_pak = 1;
				if (path_id)
					*path_id = search->path_id;
				if (handle)
				{
					// We can have concurrent reads to the pack (either as file or memory-based)
					// So we MUST duplicate the pak handle to allow independent reads and seeks.
					int new_handle = Sys_DuplicateHandle (pak->handle);
					if (new_handle < 0)
						Sys_Error ("COM_FindFile: couldn't reopen %s", pak->filename);
					Sys_FileSeek (new_handle, pak->files[i].filepos);
					*handle = new_handle;
					return com_filesize;
				}
				else if (file)
				{ /* open a new file on the pakfile */
					*file = Sys_fopen (pak->filename, "rb");
					if (*file)
						Sys_fseek (*file, pak->files[i].filepos, SEEK_SET);
					return com_filesize;
				}
				else /* for COM_FileExists() */
				{
					return com_filesize;
				}
			}
		}
		else /* check a file in the directory tree */
		{
			if (!registered.value)
			{ /* if not a registered version, don't ever go beyond base */
				if (strchr (filename, '/') || strchr (filename, '\\'))
					continue;
			}

			q_snprintf (netpath, sizeof (netpath), "%s/%s", search->filename, filename);
			if (!(Sys_FileType (netpath) & FS_ENT_FILE))
				continue;

			if (path_id)
				*path_id = search->path_id;
			if (handle)
			{
				com_filesize = Sys_FileOpenRead (netpath, &i);
				*handle = i;
				return com_filesize;
			}
			else if (file)
			{
				*file = Sys_fopen (netpath, "rb");
				com_filesize = (*file == NULL) ? -1 : COM_filelength (*file);
				return com_filesize;
			}
			else
			{
				return 0; /* dummy valid value for COM_FileExists() */
			}
		}
	}

	if (developer.value > 1)
	{
		Con_DPrintf ("FindFile: can't find %s\n", filename);
	}

	if (handle)
		*handle = -1;
	if (file)
		*file = NULL;
	com_filesize = -1;
	return com_filesize;
}

/*
===========
COM_FileExists

Returns whether the file is found in the quake filesystem.
===========
*/
qboolean COM_FileExists (const char *filename, unsigned int *path_id)
{
	qfilesize_t ret = COM_FindFile (filename, NULL, NULL, path_id);
	return (ret == -1) ? false : true;
}

/*
===========
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pak file
===========
*/
qfilesize_t COM_OpenFile (const char *filename, int *handle, unsigned int *path_id)
{
	return COM_FindFile (filename, handle, NULL, path_id);
}

/*
===========
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
qfilesize_t COM_FOpenFile (const char *filename, FILE **file, unsigned int *path_id)
{
	return COM_FindFile (filename, NULL, file, path_id);
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile (int h)
{
	if (h < 0)
		return;

	searchpath_t *s;

	for (s = com_searchpaths; s; s = s->next)
		if (s->pack && s->pack->handle == h)
			return;

	Sys_FileClose (h);
}

/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
byte *COM_LoadFile (const char *path, unsigned int *path_id)
{
	int			h, nread;
	byte	   *buf;
	qfilesize_t len;

	buf = NULL; // quiet compiler warning

	// look for it in the filesystem or pack files
	len = COM_OpenFile (path, &h, path_id);
	if (h == -1)
		return NULL;

	buf = (byte *)Mem_AllocNonZero (len + 1);

	if (!buf)
		Sys_Error ("COM_LoadFile: not enough space for %s", path);

	((byte *)buf)[len] = 0;

	nread = Sys_FileRead (h, buf, len);
	COM_CloseFile (h);
	if (nread != len)
		Sys_Error ("COM_LoadFile: Error reading %s", path);

	return buf;
}

byte *COM_LoadMallocFile_TextMode_OSPath (const char *path, long *len_out)
{
	FILE	   *f;
	byte	   *data;
	qfilesize_t len, actuallen;

	// ericw -- this is used by Host_Loadgame_f. Translate CRLF to LF on load games,
	// othewise multiline messages have a garbage character at the end of each line.
	// TODO: could handle in a way that allows loading CRLF savegames on mac/linux
	// without the junk characters appearing.
	f = Sys_fopen (path, "rt");
	if (f == NULL)
		return NULL;

	len = COM_filelength (f);
	if (len < 0)
	{
		fclose (f);
		return NULL;
	}

	data = (byte *)Mem_AllocNonZero (len + 1);
	if (data == NULL)
	{
		fclose (f);
		return NULL;
	}

	// (actuallen < len) if CRLF to LF translation was performed
	actuallen = fread (data, 1, len, f);
	if (ferror (f))
	{
		fclose (f);
		Mem_Free (data);
		return NULL;
	}
	data[actuallen] = '\0';

	if (len_out != NULL)
		*len_out = actuallen;
	fclose (f);
	return data;
}

const char *COM_ParseIntNewline (const char *buffer, int *value)
{
	int consumed = 0;
	sscanf (buffer, "%i\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseFloatNewline (const char *buffer, float *value)
{
	int consumed = 0;
	sscanf (buffer, "%f\n%n", value, &consumed);
	return buffer + consumed;
}

const char *COM_ParseStringNewline (const char *buffer)
{
	int consumed = 0;
	com_token[0] = '\0';
	sscanf (buffer, "%1023s\n%n", com_token, &consumed);
	return buffer + consumed;
}

size_t COM_SanitizeDescriptionString (char *dst, size_t dstsize, const char *src, bool remove_color)
{
	int srcpos, dstpos;

	if (!dstsize)
		return 0;

	for (srcpos = dstpos = 0; src[srcpos] && (size_t)dstpos + 1 < dstsize; srcpos++)
	{
		char c = src[srcpos] & (remove_color ? 0x7f : 0xFF); // remove_color

		// When reducing to plain ASCII, also strip control chars: colored glyphs can mask down to
		// scanf whitespace (e.g. 0x8b -> \v), which would split the savegame comment line on load
		if (remove_color && !q_isprint (c))
			c = ' ';
		else if (c == '\n' || c == '\r') // replace newlines with spaces
			c = ' ';
		else if (c == '\\' && src[srcpos + 1] == 'n') // replace '\\' followed by 'n' with space
		{
			c = ' ';
			srcpos++;
		}
		// remove leading spaces, replace consecutive spaces with single one
		if (c != ' ' || (dstpos > 0 && dst[dstpos - 1] != c))
			dst[dstpos++] = c;
	}
	// remove trailing space, if any
	if (dstpos > 0 && dst[dstpos - 1] == ' ')
		--dstpos;

	dst[dstpos] = '\0';
	return dstpos;
}

/*
=================
COM_LoadPackFile -- johnfitz -- modified based on topaz's tutorial

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
static pack_t *COM_LoadPackFile (const char *packfile, int packhandle, qfilesize_t filesize)
{
	dpackheader_t  header;
	int			   i;
	packfile_t	  *newfiles;
	int			   numpackfiles;
	pack_t		  *pack;
	unsigned short crc;

	// use global as temporary to prevent stack consumption,
	// fine because this is only called from the main loop.
	static dpackfile_t info[MAX_FILES_IN_PACK];

	if (filesize < (qfilesize_t)sizeof (header) || Sys_FileSeek (packhandle, 0) != 0 ||
		Sys_FileRead (packhandle, (void *)&header, (int)sizeof (header)) != (int)sizeof (header))
		Sys_Error ("Could not read packfile header from %s", packfile);
	if (header.id[0] != 'P' || header.id[1] != 'A' || header.id[2] != 'C' || header.id[3] != 'K')
		Sys_Error ("%s is not a packfile", packfile);

	header.dirofs = LittleLong (header.dirofs);
	header.dirlen = LittleLong (header.dirlen);

	if (header.dirlen < 0 || header.dirofs < (int)sizeof (header) ||
		header.dirlen % (int)sizeof (dpackfile_t) != 0 ||
		header.dirlen > (int)sizeof (info) ||
		(qfilesize_t)header.dirofs > filesize ||
		(qfilesize_t)header.dirlen > filesize - (qfilesize_t)header.dirofs)
	{
		Sys_Error ("Invalid packfile %s (dirlen: %i, dirofs: %i)", packfile, header.dirlen, header.dirofs);
	}

	numpackfiles = header.dirlen / (int)sizeof (dpackfile_t);
	if (numpackfiles > MAX_FILES_IN_PACK)
		Sys_Error ("%s has %i files", packfile, numpackfiles);
	if (!numpackfiles)
	{
		Sys_Printf ("WARNING: %s has no files, ignored\n", packfile);
		Sys_FileClose (packhandle);
		return NULL;
	}
	if (Sys_FileSeek (packhandle, header.dirofs) != 0 ||
		Sys_FileRead (packhandle, (void *)info, header.dirlen) != header.dirlen)
		Sys_Error ("Could not read packfile directory from %s", packfile);

	// crc the directory to check for modifications
	CRC_Init (&crc);
	for (i = 0; i < header.dirlen; i++)
		CRC_ProcessByte (&crc, ((byte *)info)[i]);
	if (crc != PAK0_CRC_V106 && crc != PAK0_CRC_V101 && crc != PAK0_CRC_V100)
		com_modified = true;

	// parse the directory
	if (!COM_ValidatePackDirectoryEntries (info, numpackfiles, filesize))
		Sys_Error ("Invalid packfile %s (bad directory entry)", packfile);

	if (numpackfiles != PAK0_COUNT)
		com_modified = true; // not the original file

	newfiles = (packfile_t *)Mem_Alloc (numpackfiles * sizeof (packfile_t));
	for (i = 0; i < numpackfiles; i++)
	{
		q_strlcpy (newfiles[i].name, info[i].name, sizeof (newfiles[i].name));
		newfiles[i].filepos = LittleLong (info[i].filepos);
		newfiles[i].filelen = LittleLong (info[i].filelen);
	}

	pack = (pack_t *)Mem_Alloc (sizeof (pack_t));
	q_strlcpy (pack->filename, packfile, sizeof (pack->filename));
	pack->handle = packhandle;
	pack->numfiles = numpackfiles;
	pack->files = newfiles;

	// Sys_Printf ("Added packfile %s (%i files)\n", packfile, numpackfiles);
	return pack;
}

/* Validate on-disk PACK files without adding them to the search path. */
static qboolean COM_ValidatePackFile (const char *path, qfilesize_t expected_size,
	qboolean require_files)
{
	dpackheader_t header;
	dpackfile_t *directory = NULL;
	FILE *file = NULL;
	qfilesize_t filesize;
	int dirofs, dirlen, count;
	qboolean valid = false;

	if (!path || expected_size < 0)
		return false;
	file = Sys_fopen (path, "rb");
	if (!file)
		return false;
	filesize = Sys_filelength (file);
	if (filesize != expected_size || filesize < (qfilesize_t)sizeof (header) ||
		Sys_fseek (file, 0, SEEK_SET) != 0 ||
		fread (&header, 1, sizeof (header), file) != sizeof (header) ||
		memcmp (header.id, "PACK", 4))
		goto done;

	dirofs = LittleLong (header.dirofs);
	dirlen = LittleLong (header.dirlen);
	if (dirofs < (int)sizeof (header) || dirlen < 0 ||
		(require_files && dirlen == 0) ||
		dirlen % (int)sizeof (dpackfile_t) != 0)
		goto done;
	count = dirlen / (int)sizeof (dpackfile_t);
	if ((require_files && count == 0) || count > MAX_FILES_IN_PACK ||
		dirlen > MAX_FILES_IN_PACK * (int)sizeof (dpackfile_t) ||
		(qfilesize_t)dirofs > filesize ||
		(qfilesize_t)dirlen > filesize - (qfilesize_t)dirofs)
		goto done;

	if (dirlen)
	{
		directory = (dpackfile_t *)malloc ((size_t)dirlen);
		if (!directory || Sys_fseek (file, dirofs, SEEK_SET) != 0 ||
			fread (directory, 1, (size_t)dirlen, file) != (size_t)dirlen)
			goto done;
	}
	if (!COM_ValidatePackDirectoryEntries (directory, count, filesize))
		goto done;
	valid = true;

done:
	free (directory);
	fclose (file);
	return valid;
}

qboolean COM_ValidateAddonPackFile (const char *path, int expected_size)
{
	/* Downloads must match the catalogue byte count and contain at least one
	 * file; existing numbered packs may validly contain an empty directory. */
	return expected_size > 0 &&
		COM_ValidatePackFile (path, (qfilesize_t)expected_size, true);
}

qboolean COM_ValidateAddonPackSequence (const char *dir)
{
	int i, handle;
	int path_length;
	char path[MAX_OSPATH];
	qfilesize_t filesize;

	if (!dir || !*dir)
		return false;

	/* pak0 is the validated temporary download that will be published next.
	 * Match COM_AddGameDirectoryRoot's consecutive pak1, pak2, ... walk. */
	for (i = 1; ; i++)
	{
		path_length = q_snprintf (path, sizeof (path), "%s/pak%i.pak", dir, i);
		if (path_length < 0 || (size_t)path_length >= sizeof (path))
			return false;
		filesize = Sys_FileOpenRead (path, &handle);
		if (filesize < 0)
			return true;
		Sys_FileClose (handle);
		if (!COM_ValidatePackFile (path, filesize, false) || i == INT_MAX)
			return false;
	}
}

/* Keep this opt-in source as narrow as the inherited product: the official
 * rerelease id1 pack supplies model companions, never maps or game code.
 * Validate the directory and Ranger bytes before handing it to the ordinary
 * pack loader, whose malformed-pack failures are intentionally fatal. */
static qboolean COM_VerifyRereleaseModelPack (int handle, qfilesize_t filesize)
{
	static const struct { const char *name; int length; mz_ulong crc; } required[] = {
		{"progs/player.md5mesh", 178658, 0x7911b9b0U},
		{"progs/player.md5anim", 331510, 0x0561e50aU}
	};
	dpackheader_t header;
	dpackfile_t *directory = NULL;
	byte buffer[8192];
	int offset, length;
	qboolean valid = false;

	if (filesize != PAK0_SIZE_RERELEASE)
		return false;
	Sys_FileSeek (handle, 0);
	if (Sys_FileRead (handle, &header, sizeof (header)) != sizeof (header) ||
		memcmp (header.id, "PACK", 4))
		return false;
	offset = LittleLong (header.dirofs);
	length = LittleLong (header.dirlen);
	if (offset < (int)sizeof (header) ||
		length != PAK0_COUNT_RERELEASE * (int)sizeof (dpackfile_t) ||
		(qfilesize_t)offset + length > filesize)
		return false;
	directory = (dpackfile_t *)Mem_Alloc (length);
	Sys_FileSeek (handle, offset);
	if (Sys_FileRead (handle, directory, length) != length ||
		CRC_Block ((const byte *)directory, length) != PAK0_CRC_RERELEASE)
		goto done;
	for (int i = 0; i < PAK0_COUNT_RERELEASE; ++i)
	{
		int filepos = LittleLong (directory[i].filepos);
		int filelen = LittleLong (directory[i].filelen);
		if (!memchr (directory[i].name, 0, sizeof (directory[i].name)) ||
			filepos < 0 || filelen < 0 ||
			(qfilesize_t)filepos + filelen > filesize)
			goto done;
	}
	for (size_t asset = 0; asset < sizeof (required) / sizeof (required[0]); ++asset)
	{
		int remaining = -1;
		mz_ulong crc = MZ_CRC32_INIT;
		for (int i = 0; i < PAK0_COUNT_RERELEASE; ++i)
			if (!strcmp (directory[i].name, required[asset].name))
			{
				if (LittleLong (directory[i].filelen) != required[asset].length)
					goto done;
				remaining = required[asset].length;
				Sys_FileSeek (handle, LittleLong (directory[i].filepos));
				break;
			}
		if (remaining < 0)
			goto done;
		while (remaining > 0)
		{
			int count = q_min (remaining, (int)sizeof (buffer));
			if (Sys_FileRead (handle, buffer, count) != count)
				goto done;
			crc = mz_crc32 (crc, buffer, count);
			remaining -= count;
		}
		if (crc != required[asset].crc)
			goto done;
	}
	valid = true;
done:
	Mem_Free (directory);
	Sys_FileSeek (handle, 0);
	return valid;
}

static void COM_AddRereleaseModelPack (const char *root)
{
	char filename[MAX_OSPATH];
	int handle;
	qfilesize_t filesize;
	pack_t *pak;
	searchpath_t *search, *tail;
	qboolean old_modified;

	if (!root || !*root ||
		(size_t)q_snprintf (filename, sizeof (filename), "%s/id1/pak0.pak", root) >= sizeof (filename))
	{
		Con_Warning ("-rerelease needs a path to the rerelease game root\n");
		return;
	}
	filesize = Sys_FileOpenRead (filename, &handle);
	if (filesize < 0)
	{
		Con_Warning ("-rerelease: cannot open %s\n", filename);
		return;
	}
	if (!COM_VerifyRereleaseModelPack (handle, filesize))
	{
		Con_Warning ("-rerelease: %s is not the verified model pack\n", filename);
		Sys_FileClose (handle);
		return;
	}
	old_modified = com_modified;
	pak = COM_LoadPackFile (filename, handle, filesize);
	com_modified = old_modified;
	if (!pak)
		return;
	search = (searchpath_t *)Mem_Alloc (sizeof (*search));
	search->path_id = 1;
	search->pack = pak;
	search->rerelease_models = true;
	search->next = NULL;
	if (!com_searchpaths)
		com_searchpaths = search;
	else
	{
		for (tail = com_searchpaths; tail->next; tail = tail->next)
			;
		tail->next = search;
	}
	Con_Printf ("Rerelease MD5 model companions enabled from %s\n", filename);
}

const char *COM_GetGameNames (qboolean full)
{
	if (full)
	{
		if (*com_gamenames)
			return va ("%s;%s", GAMENAME, com_gamenames);
		else
			return GAMENAME;
	}
	return com_gamenames;
	//	return COM_SkipPath(com_gamedir);
}

// if either contain id1 then that gets ignored
qboolean COM_GameDirMatches (const char *tdirs)
{
	int			gnl = strlen (GAMENAME);
	const char *odirs = COM_GetGameNames (false);

	// ignore any core paths.
	if (!strncmp (tdirs, GAMENAME, gnl) && (tdirs[gnl] == ';' || !tdirs[gnl]))
	{
		tdirs += gnl;
		if (*tdirs == ';')
			tdirs++;
	}
	if (!strncmp (odirs, GAMENAME, gnl) && (odirs[gnl] == ';' || !odirs[gnl]))
	{
		odirs += gnl;
		if (*odirs == ';')
			odirs++;
	}
	// skip any qw in there from quakeworld (remote servers should really be skipping this, unless its maybe the only one in the path).
	if (!strncmp (tdirs, "qw;", 3) || !strcmp (tdirs, "qw"))
	{
		tdirs += 2;
		if (*tdirs == ';')
			tdirs++;
	}
	if (!strncmp (odirs, "qw;", 3) || !strcmp (odirs, "qw")) // need to cope with ourselves setting it that way too, just in case.
	{
		odirs += 2;
		if (*odirs == ';')
			odirs++;
	}

	// okay, now check it properly
	if (!strcmp (odirs, tdirs))
		return true;
	return false;
}

/* Inherited OpenVR COM_FindNumberedPack policy, using the native enumerator.
 * Windows-authored packs may be PAK0.PAK or Pak0.pak. Keep exact lowercase
 * priority and choose other matches deterministically on case-sensitive hosts. */
static qboolean COM_FindNumberedPack (const char *directory, int number,
	char *path, size_t pathsize)
{
	char expected[32], best[MAX_QPATH] = "";
	int written;
	findfile_t *find;

	q_snprintf (expected, sizeof (expected), "pak%i.pak", number);
	written = q_snprintf (path, pathsize, "%s/%s", directory, expected);
	if (written < 0 || (size_t)written >= pathsize)
		return false;
	if (Sys_FileType (path) == FS_ENT_FILE)
		return true;

	for (find = Sys_FindFirst (directory, "pak"); find; find = Sys_FindNext (find))
	{
		if ((find->attribs & FA_DIRECTORY) || q_strcasecmp (find->name, expected))
			continue;
		if (!best[0] || strcmp (find->name, best) < 0)
			q_strlcpy (best, find->name, sizeof (best));
	}
	if (!best[0])
		return false;
	written = q_snprintf (path, pathsize, "%s/%s", directory, best);
	return written >= 0 && (size_t)written < pathsize &&
		Sys_FileType (path) == FS_ENT_FILE;
}

/*
=================
COM_AddGameDirectory -- johnfitz -- modified based on topaz's tutorial
=================
*/
static void COM_AddGameDirectoryRoot (const char *base, const char *dir, unsigned int path_id, qboolean add_embedded)
{
	int			  i, packhandle;
	qfilesize_t	  packfilesize;
	searchpath_t *search;
	pack_t		 *pak;
	char		  pakfile[MAX_OSPATH];
	static byte	 *vkquake_pak_extracted;

	q_strlcpy (com_gamedir, va ("%s/%s", base, dir), sizeof (com_gamedir));

	// add the directory to the search path
	search = (searchpath_t *)Mem_Alloc (sizeof (searchpath_t));
	search->path_id = path_id;
	q_strlcpy (search->filename, com_gamedir, sizeof (search->filename));
	q_strlcpy (search->dir, dir, sizeof (search->dir));
	search->next = com_searchpaths;
	com_searchpaths = search;

	// add any pak files in the format pak0.pak pak1.pak, ...
	for (i = 0;; i++)
	{
		if (!COM_FindNumberedPack (com_gamedir, i, pakfile, sizeof (pakfile)))
			break;
		packfilesize = Sys_FileOpenRead (pakfile, &packhandle);
		if (packfilesize < 0)
			break;
		pak = COM_LoadPackFile (pakfile, packhandle, packfilesize);
		if (pak)
		{
			search = (searchpath_t *)Mem_Alloc (sizeof (searchpath_t));
			search->path_id = path_id;
			search->pack = pak;
			q_strlcpy (search->dir, dir, sizeof (search->dir));
			search->next = com_searchpaths;
			com_searchpaths = search;
		}

		if ((i == 0) && (path_id == 1) && add_embedded)
		{
			size_t vkquake_pak_size_compressed = vkquake_pak_size, vkquake_pak_size_extracted = vkquake_pak_decompressed_size;
			if (!vkquake_pak_extracted)
			{
				tinfl_decompressor inflator;
				tinfl_init (&inflator);
				vkquake_pak_extracted = Mem_Alloc (vkquake_pak_size_extracted);

				tinfl_status decomp_status = tinfl_decompress (
					&inflator, vkquake_pak, &vkquake_pak_size_compressed, vkquake_pak_extracted, vkquake_pak_extracted, &vkquake_pak_size_extracted,
					TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);

				if (TINFL_STATUS_DONE != decomp_status)
					Sys_Error ("Error extracting embedded pack");
			}
			qboolean pak0_modified = com_modified;
			Sys_MemFileOpenRead (vkquake_pak_extracted, vkquake_pak_size_extracted, &packhandle);
			pak = COM_LoadPackFile ("vkquake.pak", packhandle,
				(qfilesize_t)vkquake_pak_size_extracted);
			search = (searchpath_t *)Mem_Alloc (sizeof (searchpath_t));
			search->path_id = path_id;
			search->pack = pak;
			q_strlcpy (search->dir, dir, sizeof (search->dir));
			search->next = com_searchpaths;
			com_searchpaths = search;
			com_modified = pak0_modified;
		}

		if (!pak)
			break;
	}
}

static void COM_AddGameDirectory (const char *dir)
{
	int			 i;
	unsigned int path_id;
	char		 path[MAX_OSPATH];

	if (*com_gamenames)
		q_strlcat (com_gamenames, ";", sizeof (com_gamenames));
	q_strlcat (com_gamenames, dir, sizeof (com_gamenames));

	// quakespasm enables mission pack flags automatically,
	// so e.g. -game rogue works without breaking the hud
	if (!q_strcasecmp (dir, "rogue"))
	{
		rogue = true;
		standard_quake = false;
	}
	if (!q_strcasecmp (dir, "hipnotic") || !q_strcasecmp (dir, "quoth"))
	{
		hipnotic = true;
		standard_quake = false;
	}

	if (!q_strcasecmp (dir, "mg3"))
	{
		mg3 = true;
	}

	// assign a path_id to this game directory; all roots share it
	if (com_searchpaths)
		path_id = com_searchpaths->path_id << 1;
	else
		path_id = 1U;

	// mount all roots in order: the extras sit below the main basedir (so it
	// takes precedence on conflicts), the userdir on top as the write target
	for (i = 0; i < com_numbasedirs; i++)
	{
		qboolean is_main = !q_strcasecmp (com_basedirs[i], com_basedir);
		qboolean is_user = (host_parms->userdir != host_parms->basedir) && !q_strcasecmp (com_basedirs[i], host_parms->userdir);

		q_snprintf (path, sizeof (path), "%s/%s", com_basedirs[i], dir);
		if (is_user)
			Sys_mkdir (path);
		else if (!is_main && Sys_FileType (path) != FS_ENT_DIRECTORY)
			continue;
		COM_AddGameDirectoryRoot (com_basedirs[i], dir, path_id, is_main);
	}
}

void COM_ResetGameDirectories (const char *newdirs)
{
	char		 *newgamedirs = q_strdup (newdirs);
	char		 *newpath, *path;
	searchpath_t *search;
	// Kill the extra game if it is loaded
	while (com_searchpaths != com_base_searchpaths)
	{
		if (com_searchpaths->pack)
		{
			Sys_FileClose (com_searchpaths->pack->handle);
			Mem_Free (com_searchpaths->pack->files);
			Mem_Free (com_searchpaths->pack);
		}
		search = com_searchpaths->next;
		Mem_Free (com_searchpaths);
		com_searchpaths = search;
	}
	hipnotic = false;
	rogue = false;
	mg3 = false;
	standard_quake = true;
	// wipe the list of mod gamedirs
	*com_gamenames = 0;
	// reset this too
	q_strlcpy (com_gamedir, va ("%s/%s", com_basedirs[com_numbasedirs - 1], GAMENAME), sizeof (com_gamedir));

	for (newpath = newgamedirs; newpath && *newpath;)
	{
		char *e = strchr (newpath, ';');
		if (e)
			*e++ = 0;

		if (!q_strcasecmp (GAMENAME, newpath))
			path = NULL;
		else
		{
			for (path = newgamedirs; path < newpath; path += strlen (path) + 1)
			{
				if (!q_strcasecmp (path, newpath))
					break;
			}
		}

		if (path == newpath) // not already loaded
			COM_AddGameDirectory (newpath);
		newpath = e;
	}
	Mem_Free (newgamedirs);
}

qboolean COM_ModForbiddenChars (const char *p)
{
	return !*p || !strcmp (p, ".") || strstr (p, "..") || strstr (p, "/") || strstr (p, "\\") || strstr (p, ":") || strstr (p, "\"") || strstr (p, ";");
}

qboolean COM_IsSafeGameDirName (const char *game)
{
	const unsigned char *p;

	if (!game || !*game || strlen (game) >= MAX_QPATH ||
		!strcmp (game, ".") || strstr (game, ".."))
		return false;

	for (p = (const unsigned char *)game; *p; ++p)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
			(*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
			return false;

	return true;
}

qboolean COM_IsSafeServerAddress (const char *server)
{
	const unsigned char *p;

	if (!server || !*server || strlen (server) >= NET_NAMELEN)
		return false;

	for (p = (const unsigned char *)server; *p; ++p)
		if (*p <= 32 || *p == '"' || *p == '\'' || *p == '\\' || *p == ';')
			return false;

	return true;
}

//==============================================================================
// johnfitz -- dynamic gamedir stuff -- modified by QuakeSpasm team.
//==============================================================================
void COM_SwitchGame (const char *paths)
{
	if (!q_strcasecmp (paths, COM_GetGameNames (true)))
	{
		Con_Printf ("\"game\" is already \"%s\"\n", COM_GetGameNames (true));
		return;
	}
	/* Retire the previous desktop end-render task before disconnect and model
	 * reset invalidate its CPU-side owners. */
	if (!isDedicated)
		GL_SynchronizeEndRenderingTask ();

	Host_SavegameDrain ();

	com_modified = true;

	// Kill the server
	CL_Disconnect ();
	SCR_EndStartupLoadingPlaque ();
	cls.demonum = -1;
	Host_ShutdownServer (true);
	/* Mod_ResetAll below zeroes the old model records. Frames rendered while
	 * the next game loads must not retain client references to those records. */
	cl.worldmodel = NULL;
	cl.viewent.model = NULL;
	memset (cl.model_precache, 0, sizeof (cl.model_precache));
	if (cl.entities)
		for (int i = 0; i < cl.num_entities && i < cl.max_edicts; ++i)
			cl.entities[i].model = NULL;

	SCR_CenterPrintClear ();

	// Write config file
	Host_WriteConfiguration ();

	// stop parsing map files before changing file system search paths
	ExtraMaps_Clear ();
	LOC_Shutdown ();

	Host_SavegameDrain ();
	COM_ResetGameDirectories (paths);

	// clear out and reload appropriate data
	Sky_ClearAll ();
	Mod_ResetAll ();
	if (!VR_WeaponCalibrationReloadGame ())
		Con_Warning ("VR: invalid weapon calibration schema for active game\n");
	VR_WeaponMenu_ReloadGame ();
	if (!isDedicated)
	{
		TexMgr_NewGame ();
		Draw_NewGame ();
		R_NewGame ();
		M_NewGame ();
	}
	ExtraMaps_NewGame ();
	Host_Resetdemos ();
	DemoList_Rebuild ();
	SaveList_Rebuild ();
	SkyList_Rebuild ();
	M_CheckMods ();
	S_ClearAll ();

	// 2026 update compat: enable scr_usekfont (for word wrapping) in case mg3 is used with original id1 data.
	Cvar_SetValueQuick (&scr_usekfont, mg3 ? 1.0f : 0.0f);

	Con_Printf ("\"game\" changed to \"%s\"\n", COM_GetGameNames (true));

	LOC_Load ();
	VID_Lock ();
	Cbuf_AddText ("exec quake.rc\n");
	Cbuf_AddText ("vid_unlock\n");
	Cmd_QueuePostConfigAfterGameChange ();
	if (!isDedicated)
		Cbuf_AddText ("vr_migrate_mod_bindings\n");
}

static qboolean COM_CurrentGameHasStartMap (void)
{
	unsigned int path_id;

	return com_searchpaths &&
		COM_FileExists ("maps/start.bsp", &path_id) &&
		path_id == com_searchpaths->path_id;
}

static void COM_Game_f (void)
{
	const qboolean play_after_change = !q_strcasecmp (Cmd_Argv (0), "playgame");
	const qboolean tracked_game_change = !play_after_change && V_TrackedSessionActive ();

	if (Cmd_Argc () > 1)
	{
		int	 i, pri;
		char paths[1024];

		if (!registered.value) // disable shareware quake
		{
			Con_Printf ("You must have the registered version to use modified games\n");
			return;
		}

		*paths = 0;
		q_strlcat (paths, GAMENAME, sizeof (paths));
		for (pri = 0; pri <= 1; pri++)
		{
			for (i = 1; i < Cmd_Argc (); i++)
			{
				const char *p = Cmd_Argv (i);
				if (!*p)
					p = GAMENAME;
				if (pri == 0)
				{
					if (*p != '-')
						continue;
					p++;
				}
				else if (*p == '-')
					continue;

				if (COM_ModForbiddenChars (p))
				{
					Con_Printf ("gamedir should be a single directory name, not a path\n");
					return;
				}

				if (!q_strcasecmp (p, GAMENAME))
					continue; // don't add id1, its not interesting enough.

				if (*paths)
					q_strlcat (paths, ";", sizeof (paths));
				q_strlcat (paths, p, sizeof (paths));
			}
		}

		CL_CancelAutoReconnect ();
		COM_SwitchGame (paths);
		if ((play_after_change || tracked_game_change) && COM_CurrentGameHasStartMap ())
			Cbuf_AddText ("map start\n");
		else if (play_after_change)
		{
			if (isDedicated)
				Con_Printf ("No start map supplied by the active game.\n");
			else
				Cbuf_AddText ("menu_maps\n");
		}
	}
	else // Diplay the current gamedir
		Con_Printf ("\"game\" is \"%s\"\n", COM_GetGameNames (true));
}

/*
=================
COM_IsValidFlavorDir

Returns true if the directory contains usable game data for the given
flavor: classic id1/pak0.pak or the rerelease QuakeEX.kpf (-1 accepts
either)
=================
*/
static qboolean COM_IsValidFlavorDir (const char *dir, int flavor)
{
	char path[MAX_OSPATH];

	if (flavor != QUAKE_FLAVOR_REMASTERED && (size_t)q_snprintf (path, sizeof (path), "%s/" GAMENAME "/pak0.pak", dir) < sizeof (path) &&
		Sys_FileType (path) == FS_ENT_FILE)
		return true;
	if (flavor != QUAKE_FLAVOR_ORIGINAL && (size_t)q_snprintf (path, sizeof (path), "%s/QuakeEX.kpf", dir) < sizeof (path) &&
		Sys_FileType (path) == FS_ENT_FILE)
		return true;

	return false;
}

/*
=================
COM_RequestedQuakeFlavor

Quake version requested on the command line, -1 if none
=================
*/
static int COM_RequestedQuakeFlavor (void)
{
	if (COM_CheckParm ("-prefremaster") || COM_CheckParm ("-remaster") || COM_CheckParm ("-remastered"))
		return QUAKE_FLAVOR_REMASTERED;
	if (COM_CheckParm ("-preforiginal") || COM_CheckParm ("-original"))
		return QUAKE_FLAVOR_ORIGINAL;
	return -1;
}

/*
=================
COM_FOpenPrefFile

Opens a file in the per-user preferences directory
(%APPDATA%\vkQuake on Windows)
=================
*/
FILE *COM_FOpenPrefFile (const char *filename, const char *mode)
{
	char *pref_path = SDL_GetPrefPath ("", "vkQuake");
	FILE *f = Sys_fopen (va ("%s/%s", pref_path, filename), mode);
	SDL_free (pref_path);
	return f;
}

/*
=================
COM_GetWriteRoot

Returns the root containing files shared by all games: the global config and
command history. This is com_basedir in portable mode and userdir otherwise.
=================
*/
const char *COM_GetWriteRoot (void)
{
	return host_parms->userdir == host_parms->basedir ? com_basedir : host_parms->userdir;
}

qboolean COM_GameDirExists (const char *dir)
{
	char path[MAX_OSPATH];
	int i;

	if (!COM_IsSafeGameDirName (dir))
		return false;

	for (i = 0; i < com_numbasedirs; ++i)
	{
		int written = q_snprintf (path, sizeof (path), "%s/%s", com_basedirs[i], dir);
		if (written >= 0 && (size_t)written < sizeof (path) &&
			(Sys_FileType (path) & FS_ENT_DIRECTORY))
			return true;
	}

	return false;
}

/* Catalogue installation state is based on the primary pak, not merely a
 * leftover game directory. Keep this query beside the filesystem root policy. */
qboolean COM_GameDirHasPak0 (const char *dir)
{
	char path[MAX_OSPATH];
	int i;

	if (!dir || COM_ModForbiddenChars (dir))
		return false;

	for (i = 0; i < com_numbasedirs; i++)
	{
		char directory[MAX_OSPATH];
		int written = q_snprintf (directory, sizeof (directory), "%s/%s", com_basedirs[i], dir);
		if (written >= 0 && (size_t)written < sizeof (directory) &&
			COM_FindNumberedPack (directory, 0, path, sizeof (path)))
			return true;
	}

	return false;
}

/*
=================
COM_FOpenConfigFile

Opens either the global config in the write root or the current game's config.
When reading a portable installation, the old id1 location remains a fallback
so existing global settings are carried into the new layout. Writes always use
the new location.
=================
*/
FILE *COM_FOpenConfigFile (qboolean global, const char *mode)
{
	FILE *f;

	if (!global)
		return Sys_fopen (va ("%s/" CONFIG_NAME, com_gamedir), mode);

	f = Sys_fopen (va ("%s/" CONFIG_NAME, COM_GetWriteRoot ()), mode);
	if (!f && mode[0] == 'r' && !strchr (mode, '+') && host_parms->userdir == host_parms->basedir)
		f = Sys_fopen (va ("%s/%s/" CONFIG_NAME, com_basedir, GAMENAME), mode);
	return f;
}

/*
=================
COM_LoadConfigFile

Loads an exec script. The default config aliases expand to the global config
followed by the game config; other scripts use the normal search path.
The caller owns the returned text.
=================
*/
char *COM_LoadConfigFile (const char *path)
{
	const char *name = path;
	FILE	   *files[2] = {NULL, NULL};
	qfilesize_t lengths[2] = {0, 0};
	char	   *buf;
	size_t		used = 0;
	qboolean	loaded = false;
	int			i;

	while (name[0] == '.' && (name[1] == '/' || name[1] == '\\'))
		name += 2;
	if (q_strcasecmp (name, "config.cfg") && q_strcasecmp (name, CONFIG_NAME))
		return (char *)COM_LoadFile (path, NULL);

	files[0] = COM_FOpenConfigFile (true, "rb");
	if (files[0])
		lengths[0] = Sys_filelength (files[0]);
	lengths[1] = COM_FOpenFile (CONFIG_NAME, &files[1], NULL);
	// The portable fallback may also be found by the game search.
	if (files[0] && files[1] && !file_from_pak && Sys_SameFile (files[0], files[1]))
	{
		fclose (files[1]);
		files[1] = NULL;
	}
	for (i = 0; i < 2; ++i)
	{
		if (files[i] && lengths[i] < 0)
		{
			fclose (files[i]);
			files[i] = NULL;
		}
		if (!files[i])
			lengths[i] = 0;
	}
	if (!files[0] && !files[1])
		return NULL;

	buf = Mem_Alloc (lengths[0] + lengths[1] + 3);
	for (i = 0; i < 2; ++i)
	{
		if (!files[i])
			continue;
		if (fread (buf + used, 1, lengths[i], files[i]) == lengths[i])
		{
			buf[used + lengths[i]] = 0;
			used += strlen (buf + used);
			buf[used++] = '\n';
			loaded = true;
		}
		fclose (files[i]);
	}
	buf[used] = 0;
	if (!loaded)
	{
		Mem_Free (buf);
		return NULL;
	}
	return buf;
}

/*
=================
COM_GetLegacySaveDir

Returns the read-only save directory used by older releases in multiuser mode.
New saves always go to com_gamedir. The legacy directory is returned only when
it exists and differs from the active game directory.
=================
*/
qboolean COM_GetLegacySaveDir (char *dst, size_t dstsize)
{
	char  *pref_root;
	size_t len;

	if (!multiuser)
		return false;

	pref_root = SDL_GetPrefPath ("", "vkQuake");
	if (!pref_root)
		return false;

	len = strlen (pref_root);
	while (len > 0 && IS_DIR_SEPARATOR (pref_root[len - 1]))
		pref_root[--len] = '\0';
	q_snprintf (dst, dstsize, "%s/%s", pref_root, COM_GetGameNames (true));
	SDL_free (pref_root);

	return q_strcasecmp (dst, com_gamedir) && Sys_FileType (dst) == FS_ENT_DIRECTORY;
}

/*
=================
COM_SetUserPrefDir

Makes the per-user preferences directory the userdir, i.e. the write
target for saves, configs, screenshots etc. and the top-priority
content root. No-op if a real userdir is already set up.
=================
*/
static void COM_SetUserPrefDir (void)
{
	static char userprefdir[MAX_OSPATH];
	char	   *pref_path;
	size_t		len;

	if (host_parms->userdir != host_parms->basedir)
		return;
	pref_path = SDL_GetPrefPath ("", "vkQuake");
	if (!pref_path)
		return;

	len = q_strlcpy (userprefdir, pref_path, sizeof (userprefdir));
	SDL_free (pref_path);
	len = q_min (len, sizeof (userprefdir) - 1);
	while (len > 0 && IS_DIR_SEPARATOR (userprefdir[len - 1]))
		userprefdir[--len] = '\0';

	host_parms->userdir = userprefdir;
	Sys_Printf ("Writing user files to %s\n", userprefdir);
}

#ifdef USE_SDL3
/*
=================
COM_LoadSelectedBaseDirs

Game folders the user picked in the folder dialog, kept in basedirs.txt
in the pref dir. A new pick is only written back once the engine is
fully initialized (COM_WriteSelectedBaseDir) so a folder with broken
data can't get remembered.
=================
*/
static char		com_storedbasedirs[2][MAX_OSPATH]; // indexed by quakeflavor_t
static qboolean com_pendingbasedirwrite;

static void COM_LoadSelectedBaseDirs (void)
{
	char  line[MAX_OSPATH + 16];
	FILE *f = COM_FOpenPrefFile ("basedirs.txt", "r");

	if (!f)
		return;

	while (fgets (line, sizeof (line), f))
	{
		char *path = strchr (line, ' ');
		if (!path)
			continue;
		*path++ = '\0';
		path[strcspn (path, "\r\n")] = '\0';
		if (!strcmp (line, "classic"))
			q_strlcpy (com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL], path, MAX_OSPATH);
		else if (!strcmp (line, "remastered"))
			q_strlcpy (com_storedbasedirs[QUAKE_FLAVOR_REMASTERED], path, MAX_OSPATH);
	}

	fclose (f);
}

/*
=================
COM_SelectBaseDir

Asks the user for a game folder until it contains data for the wanted
flavor (-1 accepts either), starting at the folder remembered from a
previous run. Exits cleanly when the user cancels the dialog; returns
false when no dialog could be shown so the caller falls through to
the regular missing-data error
=================
*/
static qboolean COM_SelectBaseDir (int flavor, char *dst, size_t dstsize)
{
	const char *title, *complaint, *default_location;
	int			result;

	switch (flavor)
	{
	case QUAKE_FLAVOR_ORIGINAL:
		title = "Select your classic Quake folder";
		complaint = "The selected folder does not contain " GAMENAME "/pak0.pak.";
		default_location = com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL];
		break;
	case QUAKE_FLAVOR_REMASTERED:
		title = "Select your remastered Quake folder";
		complaint = "The selected folder does not contain QuakeEX.kpf.";
		default_location = com_storedbasedirs[QUAKE_FLAVOR_REMASTERED];
		break;
	default:
		title = "Select your Quake folder";
		complaint = "The selected folder does not contain Quake game data (" GAMENAME "/pak0.pak or QuakeEX.kpf).";
		default_location =
			com_storedbasedirs[QUAKE_FLAVOR_REMASTERED][0] ? com_storedbasedirs[QUAKE_FLAVOR_REMASTERED] : com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL];
		break;
	}

	while ((result = Sys_SelectFolder (title, default_location, dst, dstsize)) > 0)
	{
		if (COM_IsValidFlavorDir (dst, flavor))
			return true;
		SDL_ShowSimpleMessageBox (SDL_MESSAGEBOX_WARNING, "vkQuake", complaint, NULL);
	}

	if (result == 0) // cancelled
	{
		SDL_Quit ();
		exit (0);
	}

	return false; // no dialog could be shown
}

static void COM_SetPendingBaseDir (int flavor, const char *dir)
{
	q_strlcpy (com_storedbasedirs[flavor], dir, MAX_OSPATH);
	com_pendingbasedirwrite = true;
}
#endif

/*
=================
COM_WriteSelectedBaseDir

Remembers the folder picked in the dialog; called once the engine is
fully initialized as proof the folder contains working game data
=================
*/
void COM_WriteSelectedBaseDir (void)
{
#ifdef USE_SDL3
	FILE *f;

	if (!com_pendingbasedirwrite)
		return;

	f = COM_FOpenPrefFile ("basedirs.txt", "w");
	if (!f)
		return;

	if (com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL][0])
		fprintf (f, "classic %s\n", com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL]);
	if (com_storedbasedirs[QUAKE_FLAVOR_REMASTERED][0])
		fprintf (f, "remastered %s\n", com_storedbasedirs[QUAKE_FLAVOR_REMASTERED]);

	fclose (f);
	com_pendingbasedirwrite = false;
#endif
}

/*
=================
COM_MountNightdiveUserDir

The official rerelease client downloads add-ons into its user dir
(e.g. Saved Games/Nightdive Studios/Quake); mount it as an extra
content root so they show up in the mods menu (like Ironwail does)
=================
*/
static char com_nightdivedir[MAX_OSPATH];

static void COM_MountNightdiveUserDir (void)
{
	if (!com_nightdivedir[0] || COM_CheckParm ("-nonightdive"))
		return;
	if (Sys_FileType (com_nightdivedir) != FS_ENT_DIRECTORY)
		return;

	COM_AddBaseDir (com_nightdivedir);
	Sys_Printf ("Mounted Nightdive add-on dir %s\n", com_nightdivedir);
}

/*
=================
COM_FindStoreBaseDir

Locates a Steam/GOG/Epic Games Store install of Quake and points
com_basedir at it (based on the Ironwail startup flow). Used when
the working directory has no game data and no -basedir was given.
Asks the user for the folder when the requested version isn't found,
starting at the previously picked folder.
=================
*/
static qboolean COM_FindStoreBaseDir (void)
{
	steamgame_t	  steamquake;
	char		  original[MAX_OSPATH] = {0};
	char		  remastered[MAX_OSPATH] = {0};
	quakeflavor_t flavor;
	int			  requested;
	qboolean	  force_steam = COM_CheckParm ("-steam") != 0;
	qboolean	  force_gog = COM_CheckParm ("-gog") != 0;
	qboolean	  force_egs = (COM_CheckParm ("-egs") || COM_CheckParm ("-epic")) != 0;
	qboolean	  forced = force_steam || force_gog || force_egs;

	if ((!forced || force_steam) && !COM_CheckParm ("-nosteam"))
	{
		if (Steam_FindGame (&steamquake, QUAKE_STEAM_APPID) && Steam_ResolvePath (original, sizeof (original), &steamquake))
		{
			if ((size_t)q_snprintf (remastered, sizeof (remastered), "%s/rerelease", original) >= sizeof (remastered))
				remastered[0] = '\0';
			else if (!Sys_GetNightdiveUserDir (com_nightdivedir, sizeof (com_nightdivedir), steamquake.library))
				com_nightdivedir[0] = '\0';
		}
	}

	if ((!forced || force_gog) && !COM_CheckParm ("-nogog"))
	{
		if (!original[0] && !Sys_GetGOGQuakeDir (original, sizeof (original)))
			original[0] = '\0';
		if (!remastered[0])
		{
			if (Sys_GetGOGQuakeEnhancedDir (remastered, sizeof (remastered)))
			{
				if (!com_nightdivedir[0] && !Sys_GetNightdiveUserDir (com_nightdivedir, sizeof (com_nightdivedir), NULL))
					com_nightdivedir[0] = '\0';
			}
			else
				remastered[0] = '\0';
		}
	}

	if ((!forced || force_egs) && !COM_CheckParm ("-noegs") && !COM_CheckParm ("-noepic"))
	{
		if (!remastered[0])
		{
			if (EGS_FindGame (remastered, sizeof (remastered), QUAKE_EGS_NAMESPACE, QUAKE_EGS_ITEM_ID, QUAKE_EGS_APP_NAME))
			{
				if (!com_nightdivedir[0] && !Sys_GetNightdiveUserDir (com_nightdivedir, sizeof (com_nightdivedir), NULL))
					com_nightdivedir[0] = '\0';
			}
			else
				remastered[0] = '\0';
		}
	}

	if (original[0] && !COM_IsValidFlavorDir (original, QUAKE_FLAVOR_ORIGINAL))
		original[0] = '\0';
	if (remastered[0] && !COM_IsValidFlavorDir (remastered, QUAKE_FLAVOR_REMASTERED))
		remastered[0] = com_nightdivedir[0] = '\0';

	requested = COM_RequestedQuakeFlavor ();

	if (!forced && !isDedicated)
	{
#ifdef USE_SDL3
		COM_LoadSelectedBaseDirs ();

		// use the folder picked in a previous run unless the user wants a new one
		if (!COM_CheckParm ("-select-basedir"))
		{
			if (!original[0] && com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL][0] &&
				COM_IsValidFlavorDir (com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL], QUAKE_FLAVOR_ORIGINAL))
				q_strlcpy (original, com_storedbasedirs[QUAKE_FLAVOR_ORIGINAL], sizeof (original));
			if (!remastered[0] && com_storedbasedirs[QUAKE_FLAVOR_REMASTERED][0] &&
				COM_IsValidFlavorDir (com_storedbasedirs[QUAKE_FLAVOR_REMASTERED], QUAKE_FLAVOR_REMASTERED))
				q_strlcpy (remastered, com_storedbasedirs[QUAKE_FLAVOR_REMASTERED], sizeof (remastered));
		}

		// still missing: ask for the folder, remember it only once it's usable
		if (requested == QUAKE_FLAVOR_ORIGINAL && !original[0])
		{
			if (COM_SelectBaseDir (QUAKE_FLAVOR_ORIGINAL, original, sizeof (original)))
				COM_SetPendingBaseDir (QUAKE_FLAVOR_ORIGINAL, original);
		}
		else if (requested == QUAKE_FLAVOR_REMASTERED && !remastered[0])
		{
			if (COM_SelectBaseDir (QUAKE_FLAVOR_REMASTERED, remastered, sizeof (remastered)))
				COM_SetPendingBaseDir (QUAKE_FLAVOR_REMASTERED, remastered);
		}
		else if (requested < 0 && !original[0] && !remastered[0])
		{
			char selected[MAX_OSPATH];
			if (COM_SelectBaseDir (-1, selected, sizeof (selected)))
			{
				if (COM_IsValidFlavorDir (selected, QUAKE_FLAVOR_REMASTERED))
				{
					q_strlcpy (remastered, selected, sizeof (remastered));
					COM_SetPendingBaseDir (QUAKE_FLAVOR_REMASTERED, selected);
				}
				else
				{
					q_strlcpy (original, selected, sizeof (original));
					COM_SetPendingBaseDir (QUAKE_FLAVOR_ORIGINAL, selected);
				}
			}
		}
#else
		// no folder picker without the SDL3 dialog API
		if (requested == QUAKE_FLAVOR_ORIGINAL && !original[0])
			Sys_Error ("Couldn't find the classic Quake folder. Use -basedir to specify it.");
		else if (requested == QUAKE_FLAVOR_REMASTERED && !remastered[0])
			Sys_Error ("Couldn't find the remastered Quake folder. Use -basedir to specify it.");
#endif
	}

	if (!original[0] && !remastered[0])
	{
		if (force_steam)
			Sys_Error ("Couldn't find Steam Quake");
		if (force_gog)
			Sys_Error ("Couldn't find GOG Quake");
		if (force_egs)
			Sys_Error ("Couldn't find Epic Games Store Quake");
		return false; // fall through to the regular missing-data error
	}

	if (requested == QUAKE_FLAVOR_REMASTERED && remastered[0])
		flavor = QUAKE_FLAVOR_REMASTERED;
	else if (requested == QUAKE_FLAVOR_ORIGINAL && original[0])
		flavor = QUAKE_FLAVOR_ORIGINAL;
	else if (original[0] && remastered[0])
		flavor = ChooseQuakeFlavor ();
	else
		flavor = remastered[0] ? QUAKE_FLAVOR_REMASTERED : QUAKE_FLAVOR_ORIGINAL;

	q_strlcpy (com_basedir, (flavor == QUAKE_FLAVOR_REMASTERED) ? remastered : original, sizeof (com_basedir));
	Sys_Printf ("Using Quake data from %s\n", com_basedir);

	if (flavor == QUAKE_FLAVOR_REMASTERED)
		COM_MountNightdiveUserDir ();
	else
		com_nightdivedir[0] = '\0';

	return true;
}

/*
=================
COM_IsPathPrefix

Compares path components case-insensitively, treating / and \ as equal
=================
*/
static qboolean COM_IsPathPrefix (const char *prefix, const char *path)
{
	size_t i, len = strlen (prefix);

	for (i = 0; i < len; i++)
	{
		char a = (prefix[i] == '\\') ? '/' : prefix[i];
		char b = (path[i] == '\\') ? '/' : path[i];
		if (q_tolower (a) != q_tolower (b))
			return false;
	}

	return path[len] == '\0' || path[len] == '/' || path[len] == '\\';
}

/*
=================
COM_InitSteamAPI

Enables Steam achievements and rich presence when the game data
comes from the Steam install (from Ironwail)
=================
*/
static void COM_InitSteamAPI (qboolean localization_fallback)
{
	steamgame_t steamquake;
	char		steampath[MAX_OSPATH], rerelease[MAX_OSPATH];
	int		 written;

	if (COM_CheckParm ("-nosteam"))
		return;
	if (!Steam_FindGame (&steamquake, QUAKE_STEAM_APPID) || !Steam_ResolvePath (steampath, sizeof (steampath), &steamquake))
		return;
	if (localization_fallback)
	{
		written = q_snprintf (rerelease, sizeof (rerelease), "%s/rerelease", steampath);
		if (written >= 0 && (size_t)written < sizeof (rerelease))
			COM_SetRereleaseLocalizationPack (rerelease);
	}
	if (!COM_IsPathPrefix (steampath, com_basedir))
		return;

	Steam_Init (&steamquake);
}

/*
=================
COM_AddArg
=================
*/
static void COM_AddArg (const char *arg)
{
	if (com_argc >= MAX_NUM_ARGVS)
		return;
	largv[com_argc++] = (char *)arg;
	largv[com_argc] = argvdummy;
}

/*
=================
COM_FindStartArgBaseDir

Looks for game data in the ancestors of the path passed as the only command-line argument
=================
*/
static qboolean COM_FindStartArgBaseDir (const char *startarg)
{
	char   dir[MAX_OSPATH];
	size_t i;

	q_strlcpy (dir, startarg, sizeof (dir));
	for (i = strlen (dir); i > 1; i--)
	{
		if (dir[i - 1] != '/' && dir[i - 1] != '\\')
			continue;
		dir[i - 1] = '\0';
		if (COM_IsValidFlavorDir (dir, COM_RequestedQuakeFlavor ()))
		{
			q_strlcpy (com_basedir, dir, sizeof (com_basedir));
			return true;
		}
	}

	return false;
}

/*
=================
COM_HandleStartArg

Turns a mod dir or a map/save/demo file passed as the only command-line argument
into -game and the matching command, so the executable can be associated with
these files or have them dropped onto it
=================
*/
static void COM_HandleStartArg (const char *fullpath)
{
	static char game[MAX_QPATH];
	char		qpath[MAX_QPATH];
	char		printpath[MAX_OSPATH];
	const char *relpath = NULL;
	const char *sep;
	const char *ext;
	int			type = Sys_FileType (fullpath);
	int			i;
	char	   *c;

	for (i = 0; i < com_numbasedirs && !relpath; i++)
	{
		if (COM_IsPathPrefix (com_basedirs[i], fullpath))
		{
			relpath = fullpath + strlen (com_basedirs[i]);
			while (*relpath == '/' || *relpath == '\\')
				++relpath;
		}
	}
	if (!relpath)
	{
		UTF8_ToQuake (printpath, sizeof (printpath), fullpath);
		Con_Printf ("\"%s\" does not belong to an existing Quake installation\n", printpath);
		return;
	}

	// game dir is the first component of the relative path
	for (sep = relpath; *sep && *sep != '/' && *sep != '\\'; sep++)
		;
	if ((size_t)(sep - relpath) >= sizeof (game))
	{
		UTF8_ToQuake (printpath, sizeof (printpath), relpath);
		Con_Printf ("\"%s\" is too long\n", printpath);
		return;
	}
	memcpy (game, relpath, sep - relpath);
	game[sep - relpath] = '\0';
	if (!*sep && type == FS_ENT_FILE)
		game[0] = '\0';

	if (game[0] && q_strcasecmp (game, GAMENAME))
	{
		COM_AddArg ("-game");
		COM_AddArg (game);
	}

	if (type == FS_ENT_DIRECTORY && !*sep)
		return;

	q_strlcpy (qpath, *sep ? sep + 1 : relpath, sizeof (qpath));
	for (c = qpath; *c; c++)
		if (*c == '\\')
			*c = '/';
	UTF8_ToQuake (printpath, sizeof (printpath), qpath);

	if (type == FS_ENT_DIRECTORY)
	{
		if (!q_strcasecmp (qpath, "maps"))
			Cbuf_AddText ("menu_maps\n");
		else
			Con_Printf ("subdir \"%s\" ignored\n", printpath);
		return;
	}

	if (!game[0])
	{
		Con_Printf ("File \"%s\" not in a mod dir, ignoring.\n", printpath);
		return;
	}

	ext = COM_FileGetExtension (qpath);
	if (!q_strcasecmp (ext, "bsp"))
	{
		if (q_strncasecmp (qpath, "maps/", 5))
		{
			Con_Printf ("Map \"%s\" not in the \"maps\" dir, ignoring.\n", printpath);
			return;
		}
		Cbuf_AddText (va ("menu_maps \"%s\"\n", qpath + 5));
	}
	else if (!q_strcasecmp (ext, "sav"))
		Cbuf_AddText (va ("load \"%s\"\n", qpath));
	else if (!q_strcasecmp (ext, "dem"))
		Cbuf_AddText (va ("playdemo \"%s\"\n", qpath));
	else
		Con_Printf ("Unsupported file type \"%s\", ignoring.\n", printpath);
}

/*
=================
COM_InitFilesystem
=================
*/
void COM_InitFilesystem (void) // johnfitz -- modified based on topaz's tutorial
{
	int			i, j;
	const char *p;
	qboolean	steam_localization_fallback = false;
	char		rerelease[MAX_OSPATH];
	const char *startarg = (com_argc == 2 && Sys_FileType (com_argv[1]) != FS_ENT_NONE) ? com_argv[1] : NULL;

	Cvar_RegisterVariable (&registered);
	Cvar_RegisterVariable (&cmdline);
	Cmd_AddCommand ("path", COM_Path_f);
	Cmd_AddCommand ("game", COM_Game_f); // johnfitz
	Cmd_AddCommand ("playgame", COM_Game_f);

	i = COM_CheckParm ("-basedir");
	if (i && i < com_argc - 1)
		q_strlcpy (com_basedir, com_argv[i + 1], sizeof (com_basedir));
	else if (!startarg || !COM_FindStartArgBaseDir (startarg))
		q_strlcpy (com_basedir, host_parms->basedir, sizeof (com_basedir));

	j = strlen (com_basedir);
	if (j < 1)
		Sys_Error ("Bad argument to -basedir");
	if ((com_basedir[j - 1] == '\\') || (com_basedir[j - 1] == '/'))
		com_basedir[j - 1] = 0;

	// no explicit -basedir: run store detection if the working directory has no
	// game data for the requested version (any version if none was requested),
	// or if a store was named explicitly on the command line
	qboolean store_install = false;
	if (!i && (!COM_IsValidFlavorDir (com_basedir, COM_RequestedQuakeFlavor ()) || COM_CheckParm ("-steam") || COM_CheckParm ("-gog") ||
			   COM_CheckParm ("-egs") || COM_CheckParm ("-epic")))
		store_install = COM_FindStoreBaseDir ();

	// keep all writes out of the game dirs: always for store installs (the
	// user didn't opt into writing there, and they might not even be
	// writable), otherwise only when -multiuser asks for it
	if (store_install || multiuser)
		COM_SetUserPrefDir ();

	/* Select the separate English localization source without mounting it.
	 * A supplied -rerelease operand owns the choice even when unusable; with no
	 * operand, automatic sibling/Steam selection follows the inherited policy. */
	com_rerelease_localization_pack[0] = 0;
	const int rerelease_parm = COM_CheckParm ("-rerelease");
	if (rerelease_parm && rerelease_parm < com_argc - 1)
		COM_SetRereleaseLocalizationPack (com_argv[rerelease_parm + 1]);
	else if (!COM_CheckParm ("-norerelease"))
	{
		j = q_snprintf (rerelease, sizeof (rerelease), "%s/rerelease", com_basedir);
		if (j < 0 || (size_t)j >= sizeof (rerelease) || !COM_SetRereleaseLocalizationPack (rerelease))
			steam_localization_fallback = true;
	}

	// achievements/rich presence if the game data comes from the Steam install,
	// no matter whether it was found by detection, -basedir or the working directory
	COM_InitSteamAPI (steam_localization_fallback);

	// register the remaining content roots: the main basedir above the extras
	// added so far, the userdir on top of everything as the write target
	COM_AddBaseDir (com_basedir);
	if (host_parms->userdir != host_parms->basedir)
		COM_AddBaseDir (host_parms->userdir);

	if (startarg)
		COM_HandleStartArg (startarg);

	i = COM_CheckParmNext (i, "-basegame");
	if (i)
	{ //-basegame:
		// a) replaces all hardcoded dirs (read: alternative to id1)
		// b) isn't flushed on normal gamedir switches (like id1).
		com_modified = true; // shouldn't be relevant when not using id content... but we don't really know.
		for (;; i = COM_CheckParmNext (i, "-basegame"))
		{
			if (!i || i >= com_argc - 1)
				break;

			p = com_argv[i + 1];
			if (COM_ModForbiddenChars (p))
				Sys_Error ("gamedir should be a single directory name, not a path\n");
			if (p != NULL)
				COM_AddGameDirectory (p);
		}
	}
	else
	{
		// start up with GAMENAME by default (id1)
		COM_AddGameDirectory (GAMENAME);
	}

	/* An explicit rerelease root contributes only low-priority MD5 model
	 * companions. It must not replace maps, progs.dat or the active id1 pack. */
	i = COM_CheckParm ("-rerelease");
	if (i && i < com_argc - 1)
		COM_AddRereleaseModelPack (com_argv[i + 1]);

	/* this is the end of our base searchpath:
	 * any set gamedirs, such as those from -game command line
	 * arguments or by the 'game' console command will be freed
	 * up to here upon a new game command. */
	com_base_searchpaths = com_searchpaths;
	COM_ResetGameDirectories ("");

	// add mission pack requests (only one should be specified)
	if (COM_CheckParm ("-rogue"))
		COM_AddGameDirectory ("rogue");
	if (COM_CheckParm ("-hipnotic"))
		COM_AddGameDirectory ("hipnotic");
	if (COM_CheckParm ("-quoth"))
		COM_AddGameDirectory ("quoth");

	for (i = 0;;)
	{
		i = COM_CheckParmNext (i, "-game");
		if (!i || i >= com_argc - 1)
			break;

		p = com_argv[i + 1];
		if (COM_ModForbiddenChars (p))
			Sys_Error ("gamedir should be a single directory name, not a path\n");
		com_modified = true;
		if (p != NULL)
			COM_AddGameDirectory (p);
	}

	COM_CheckRegistered ();
}

/* The following FS_*() stdio replacements are necessary if one is
 * to perform non-sequential reads on files reopened on pak files
 * because we need the bookkeeping about file start/end positions.
 * Allocating and filling in the fshandle_t structure is the users'
 * responsibility when the file is initially opened. */

size_t FS_fread (void *ptr, size_t size, size_t nmemb, fshandle_t *fh)
{
	qfilesize_t byte_size;
	qfilesize_t bytes_read;
	qfilesize_t nmemb_read;

	if (!fh)
	{
		errno = EBADF;
		return 0;
	}
	if (!ptr)
	{
		errno = EFAULT;
		return 0;
	}
	if (!size || !nmemb)
	{ /* no error, just zero bytes wanted */
		errno = 0;
		return 0;
	}

	byte_size = nmemb * size;
	if (byte_size > fh->length - fh->pos) /* just read to end */
		byte_size = fh->length - fh->pos;
	bytes_read = fread (ptr, 1, byte_size, fh->file);
	fh->pos += bytes_read;

	/* fread() must return the number of elements read,
	 * not the total number of bytes. */
	nmemb_read = bytes_read / size;
	/* even if the last member is only read partially
	 * it is counted as a whole in the return value. */
	if (bytes_read % size)
		nmemb_read++;

	return nmemb_read;
}

int FS_fseek (fshandle_t *fh, qfileofs_t offset, int whence)
{
	int ret;

	if (!fh)
	{
		errno = EBADF;
		return -1;
	}

	/* the relative file position shouldn't be smaller
	 * than zero or bigger than the filesize. */
	switch (whence)
	{
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += fh->pos;
		break;
	case SEEK_END:
		offset = fh->length + offset;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	if (offset < 0)
	{
		errno = EINVAL;
		return -1;
	}

	if (offset > fh->length) /* just seek to end */
		offset = fh->length;

	ret = Sys_fseek (fh->file, fh->start + offset, SEEK_SET);
	if (ret < 0)
		return ret;

	fh->pos = offset;
	return 0;
}

int FS_fclose (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return fclose (fh->file);
}

qfileofs_t FS_ftell (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return fh->pos;
}

void FS_rewind (fshandle_t *fh)
{
	if (!fh)
		return;
	clearerr (fh->file);
	Sys_fseek (fh->file, fh->start, SEEK_SET);
	fh->pos = 0;
}

int FS_feof (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	if (fh->pos >= fh->length)
		return -1;
	return 0;
}

int FS_ferror (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return ferror (fh->file);
}

int FS_fgetc (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return EOF;
	}
	if (fh->pos >= fh->length)
		return EOF;
	fh->pos += 1;
	return fgetc (fh->file);
}

char *FS_fgets (char *s, int size, fshandle_t *fh)
{
	char *ret;

	if (FS_feof (fh))
		return NULL;

	if (size > (fh->length - fh->pos) + 1)
		size = (fh->length - fh->pos) + 1;

	ret = fgets (s, size, fh->file);
	fh->pos = Sys_ftell (fh->file) - fh->start;

	return ret;
}

qfilesize_t FS_filelength (fshandle_t *fh)
{
	if (!fh)
	{
		errno = EBADF;
		return -1;
	}
	return fh->length;
}

// for compat with dpp7 protocols, and mods that cba to precache things.
void COM_Effectinfo_Enumerate (int (*cb) (const char *pname))
{
	int				   i;
	const char		  *f, *e;
	char			  *buf;
	static const char *dpnames[] = {"TE_GUNSHOT",		"TE_GUNSHOTQUAD",
									"TE_SPIKE",			"TE_SPIKEQUAD",
									"TE_SUPERSPIKE",	"TE_SUPERSPIKEQUAD",
									"TE_WIZSPIKE",		"TE_KNIGHTSPIKE",
									"TE_EXPLOSION",		"TE_EXPLOSIONQUAD",
									"TE_TAREXPLOSION",	"TE_TELEPORT",
									"TE_LAVASPLASH",	"TE_SMALLFLASH",
									"TE_FLAMEJET",		"EF_FLAME",
									"TE_BLOOD",			"TE_SPARK",
									"TE_PLASMABURN",	"TE_TEI_G3",
									"TE_TEI_SMOKE",		"TE_TEI_BIGEXPLOSION",
									"TE_TEI_PLASMAHIT", "EF_STARDUST",
									"TR_ROCKET",		"TR_GRENADE",
									"TR_BLOOD",			"TR_WIZSPIKE",
									"TR_SLIGHTBLOOD",	"TR_KNIGHTSPIKE",
									"TR_VORESPIKE",		"TR_NEHAHRASMOKE",
									"TR_NEXUIZPLASMA",	"TR_GLOWTRAIL",
									"SVC_PARTICLE",		NULL};

	buf = (char *)COM_LoadFile ("effectinfo.txt", NULL);
	if (!buf)
		return;

	for (i = 0; dpnames[i]; i++)
		cb (dpnames[i]);

	for (f = buf; f; f = e)
	{
		e = COM_Parse (f);
		if (!strcmp (com_token, "effect"))
		{
			e = COM_Parse (e);
			cb (com_token);
		}
		while (e && *e && *e != '\n')
			e++;
	}
	Mem_Free (buf);
}

/*
============================================================================
								LOCALIZATION
============================================================================
*/
typedef struct
{
	char *key;
	char *value;
} locentry_t;

typedef struct
{
	int			numentries;
	int			maxnumentries;
	int			numindices;
	unsigned   *indices;
	locentry_t *entries;
	char	   *text;
	char	   *fgdbuffer;
	qboolean	mg3_fallback;
} localization_t;

static localization_t localization;

/*
================
COM_HashString
Computes the FNV-1a hash of string str
================
*/
unsigned COM_HashString (const char *str)
{
	unsigned hash = 0x811c9dc5u;
	while (*str)
	{
		hash ^= *str++;
		hash *= 0x01000193u;
	}
	return hash;
}

/*
================
COM_HashBlock
Computes the FNV-1a hash of a memory block
================
*/
unsigned COM_HashBlock (const void *data, size_t size)
{
	const byte *ptr = (const byte *)data;
	unsigned	hash = 0x811c9dc5u;
	while (size--)
	{
		hash ^= *ptr++;
		hash *= 0x01000193u;
	}
	return hash;
}

static size_t mz_zip_file_read_func (void *opaque, mz_uint64 ofs, void *buf, size_t n)
{
#ifdef USE_SDL3
	if (SDL_SeekIO ((SDL_IOStream *)opaque, (Sint64)ofs, SDL_IO_SEEK_SET) < 0)
		return 0;
	return SDL_ReadIO ((SDL_IOStream *)opaque, buf, n);
#else
	if (SDL_RWseek ((SDL_RWops *)opaque, (Sint64)ofs, RW_SEEK_SET) < 0)
		return 0;
	return SDL_RWread ((SDL_RWops *)opaque, buf, 1, n);
#endif
}

static void LOC_BuildLookup (void)
{
	int i;
	localization.numindices = localization.numentries * 2; // 50% load factor
	if (!localization.numindices)
		return;

	localization.indices = (unsigned *)Mem_Realloc (localization.indices, localization.numindices * sizeof (*localization.indices));
	memset (localization.indices, 0, localization.numindices * sizeof (*localization.indices));

	for (i = 0; i < localization.numentries; i++)
	{
		locentry_t *entry = &localization.entries[i];
		unsigned	pos = COM_HashString (entry->key) % localization.numindices, end = pos;

		for (;;)
		{
			if (!localization.indices[pos])
			{
				localization.indices[pos] = i + 1;
				break;
			}

			++pos;
			if (pos == localization.numindices)
				pos = 0;

			if (pos == end)
				Sys_Error ("LOC_LoadFile failed");
		}
	}
}

static void LOC_AddOrReplaceFGDEntry (char *key, char *value, int protected_entries)
{
	int i;
	for (i = 0; i < localization.numentries; i++)
	{
		if (!strcmp (localization.entries[i].key, key))
		{
			if (i >= protected_entries)
				localization.entries[i].value = value;
			return;
		}
	}

	if (localization.numentries == localization.maxnumentries)
	{
		localization.maxnumentries += localization.maxnumentries >> 1;
		localization.maxnumentries = q_max (localization.maxnumentries, 32);
		localization.entries = (locentry_t *)Mem_Realloc (localization.entries, sizeof (*localization.entries) * localization.maxnumentries);
	}

	localization.entries[localization.numentries].key = key;
	localization.entries[localization.numentries].value = value;
	localization.numentries++;
}

static void LOC_DecodeFGDEscapes (char *string, char *end, int lineno, const char *file)
{
	char *src = string;
	char *dst = string;
	while (src != end)
	{
		if (*src == '\\' && src + 1 < end)
		{
			char c = src[1];
			src += 2;
			switch (c)
			{
			case 'n': *dst++ = '\n'; break;
			case 'r': *dst++ = '\r'; break;
			case 't': *dst++ = '\t'; break;
			case 'v': *dst++ = '\v'; break;
			case 'b': *dst++ = '\b'; break;
			case 'f': *dst++ = '\f'; break;
			case '\"':
			case '\'':
			case '\\':
				*dst++ = c;
				break;
			default:
				Con_Printf ("LOC_LoadFile: unrecognized escape sequence \\%c on line %d in '%s'\n", c, lineno, file);
				*dst++ = c;
				break;
			}
			continue;
		}

		*dst++ = *src++;
	}
	*dst = 0;
}

static void LOC_ParseLocalizationFGD (char *text, const char *file, int protected_entries)
{
	int lineno = 0;
	char *cursor = text;
	while (*cursor)
	{
		char *line = cursor;
		char *line_end = cursor;
		char *scan;
		char *key;
		char *key_end;
		char *value;
		char *value_end;

		lineno++;
		while (*line_end && *line_end != '\n')
			++line_end;

		if (*line_end)
			*line_end++ = 0;

		while (q_isblank (*line))
			++line;

		if (*line == 0 || *line == '/')
		{
			cursor = line_end;
			continue;
		}
		if (*line != '\"')
		{
			cursor = line_end;
			continue;
		}

		key = line + 1;
		key_end = key;
		while (key_end < line_end)
		{
			if (*key_end == '\\' && key_end + 1 < line_end)
			{
				key_end += 2;
				continue;
			}
			if (*key_end == '\"')
				break;
			++key_end;
		}
		if (key_end >= line_end)
		{
			cursor = line_end;
			continue;
		}

		*key_end = 0;
		LOC_DecodeFGDEscapes (key, key_end, lineno, file);
		if (*key == '$')
			++key;

		scan = key_end + 1;
		while (scan < line_end && q_isblank (*scan))
			++scan;
		if (scan >= line_end || *scan != ':')
		{
			cursor = line_end;
			continue;
		}
		++scan;
		while (scan < line_end && q_isblank (*scan))
			++scan;
		if (scan >= line_end || *scan != '\"')
		{
			cursor = line_end;
			continue;
		}

		value = scan + 1;
		value_end = value;
		while (value_end < line_end)
		{
			if (*value_end == '\\' && value_end + 1 < line_end)
			{
				value_end += 2;
				continue;
			}
			if (*value_end == '\"')
				break;
			++value_end;
		}
		if (value_end >= line_end)
		{
			cursor = line_end;
			continue;
		}

		*value_end = 0;
		LOC_DecodeFGDEscapes (value, value_end, lineno, file);
		UTF8_ToQuake (value, strlen (value) + 1, value);
		LOC_AddOrReplaceFGDEntry (key, value, protected_entries);

		cursor = line_end;
	}
}

/*
================
LOC_LoadFile
================
*/
static qboolean LOC_LoadFile (const char *file)
{
	char  path[1024];
	int	  lineno;
	char *cursor;

#ifdef USE_SDL3
	SDL_IOStream *rw = NULL;
#else
	SDL_RWops *rw = NULL;
#endif
	Sint64		   sz;
	mz_zip_archive archive;
	size_t		   size = 0;

	// clear existing data
	if (localization.text)
	{
		Mem_Free (localization.text);
		localization.text = NULL;
	}
	if (localization.fgdbuffer)
	{
		Mem_Free (localization.fgdbuffer);
		localization.fgdbuffer = NULL;
	}
	localization.mg3_fallback = false;
	localization.numentries = 0;
	localization.numindices = 0;

	if (!file || !*file)
		return false;

	localization.text = (char *)COM_LoadFile (file, NULL);
	if (localization.text)
		goto loaded;

	memset (&archive, 0, sizeof (archive));
	q_snprintf (path, sizeof (path), "%s/%s", com_basedir, file);
#ifdef USE_SDL3
	rw = SDL_IOFromFile (path, "rb");
#else
	rw = SDL_RWFromFile (path, "rb");
#endif
#if defined(DO_USERDIRS)
	if (!rw)
	{
		q_snprintf (path, sizeof (path), "%s/%s", host_parms->userdir, file);
#ifdef USE_SDL3
		rw = SDL_IOFromFile (path, "rb");
#else
		rw = SDL_RWFromFile (path, "rb");
#endif
	}
#endif
	if (!rw)
	{
		localization.text = COM_LoadRereleaseLocalization (file);
		if (localization.text)
			goto loaded;

		q_snprintf (path, sizeof (path), "%s/QuakeEX.kpf", com_basedir);
#ifdef USE_SDL3
		rw = SDL_IOFromFile (path, "rb");
#else
		rw = SDL_RWFromFile (path, "rb");
#endif
#if defined(DO_USERDIRS)
		if (!rw)
		{
			q_snprintf (path, sizeof (path), "%s/QuakeEX.kpf", host_parms->userdir);
#ifdef USE_SDL3
			rw = SDL_IOFromFile (path, "rb");
#else
			rw = SDL_RWFromFile (path, "rb");
#endif
		}
#endif
		if (!rw)
			goto fail;
#ifdef USE_SDL3
		sz = SDL_GetIOSize (rw);
#else
		sz = SDL_RWsize (rw);
#endif
		if (sz <= 0)
			goto fail;
		archive.m_pRead = mz_zip_file_read_func;
		archive.m_pIO_opaque = rw;
		if (!mz_zip_reader_init (&archive, sz, 0))
			goto fail;
		localization.text = (char *)mz_zip_reader_extract_file_to_heap (&archive, file, &size, 0);
		if (!localization.text)
			goto fail;
		mz_zip_reader_end (&archive);
#ifdef USE_SDL3
		SDL_CloseIO (rw);
#else
		SDL_RWclose (rw);
#endif
		localization.text = (char *)Mem_Realloc (localization.text, size + 1);
		localization.text[size] = 0;
	}
	else
	{
#ifdef USE_SDL3
		sz = SDL_GetIOSize (rw);
#else
		sz = SDL_RWsize (rw);
#endif
		if (sz <= 0)
			goto fail;
		localization.text = (char *)Mem_Alloc (sz + 1);
		if (!localization.text)
		{
		fail:
			mz_zip_reader_end (&archive);
			if (rw)
#ifdef USE_SDL3
				SDL_CloseIO (rw);
#else
				SDL_RWclose (rw);
#endif
			Con_Printf ("Couldn't load '%s'\nfrom '%s'\n", file, com_basedir);
			return false;
		}
#ifdef USE_SDL3
		SDL_ReadIO (rw, localization.text, sz);
		SDL_CloseIO (rw);
#else
		SDL_RWread (rw, localization.text, 1, sz);
		SDL_RWclose (rw);
#endif
	}
loaded:
	cursor = localization.text;

	// skip BOM
	if ((unsigned char)(cursor[0]) == 0xEF && (unsigned char)(cursor[1]) == 0xBB && (unsigned char)(cursor[2]) == 0xBF)
		cursor += 3;

	lineno = 0;
	while (*cursor)
	{
		char *line, *equals, *next;

		lineno++;

		// skip leading whitespace
		while (q_isblank (*cursor))
			++cursor;

		line = cursor;
		equals = NULL;
		// find line end and first equals sign, if any
		while (*cursor && *cursor != '\n')
		{
			if (*cursor == '=' && !equals)
				equals = cursor;
			cursor++;
		}

		next = *cursor ? cursor + 1 : cursor;

		if (line[0] == '/')
		{
			if (line[1] != '/')
				Con_DPrintf ("LOC_LoadFile: malformed comment on line %d\n", lineno);
		}
		else if (equals)
		{
			char	   *key_end = equals;
			qboolean	leading_quote;
			qboolean	trailing_quote;
			locentry_t *entry;
			char	   *value_src;
			char	   *value_dst;
			char	   *value;

			// trim whitespace before equals sign
			while (key_end != line && q_isspace (key_end[-1]))
				key_end--;
			*key_end = 0;

			value = equals + 1;
			// skip whitespace after equals sign
			while (value != cursor && q_isspace (*value))
				value++;

			leading_quote = (*value == '\"');
			trailing_quote = false;
			value += leading_quote;

			// transform escape sequences in-place
			value_src = value;
			value_dst = value;
			while (value_src != cursor)
			{
				if (*value_src == '\\' && value_src + 1 != cursor)
				{
					char c = value_src[1];
					value_src += 2;
					switch (c)
					{
					case 'n':
						*value_dst++ = '\n';
						break;
					case 't':
						*value_dst++ = '\t';
						break;
					case 'v':
						*value_dst++ = '\v';
						break;
					case 'b':
						*value_dst++ = '\b';
						break;
					case 'f':
						*value_dst++ = '\f';
						break;

					case '"':
					case '\'':
						*value_dst++ = c;
						break;

					default:
						Con_Printf ("LOC_LoadFile: unrecognized escape sequence \\%c on line %d\n", c, lineno);
						*value_dst++ = c;
						break;
					}
					continue;
				}

				if (*value_src == '\"')
				{
					trailing_quote = true;
					*value_dst = 0;
					break;
				}

				*value_dst++ = *value_src++;
			}

			// if not a quoted string, trim trailing whitespace
			if (!trailing_quote)
			{
				while (value_dst != value && q_isblank (value_dst[-1]))
					value_dst--;
			}

			*value_dst = 0;

			if (localization.numentries == localization.maxnumentries)
			{
				// grow by 50%
				localization.maxnumentries += localization.maxnumentries >> 1;
				localization.maxnumentries = q_max (localization.maxnumentries, 32);
				localization.entries = (locentry_t *)Mem_Realloc (localization.entries, sizeof (*localization.entries) * localization.maxnumentries);
			}

			entry = &localization.entries[localization.numentries++];
			entry->key = line;
			UTF8_ToQuake (value, strlen (value) + 1, value);
			entry->value = value;
		}

		*cursor = 0;
		cursor = next;
	}

	if (localization.numentries == 0)
	{
		Con_Printf ("No localized strings in file '%s'\n", file);
		return false;
	}

	LOC_BuildLookup ();

	Con_Printf ("Loaded %d strings from '%s'\n", localization.numentries, file);
	return true;
}

static void LOC_LoadFGD (int protected_entries)
{
	char *text;
	int	 oldnumentries = localization.numentries;

	localization.fgdbuffer = (char *)COM_LoadFile ("fgd/messages.fgd", NULL);
	if (!localization.fgdbuffer)
		return;

	text = localization.fgdbuffer;
	if ((unsigned char)(text[0]) == 0xEF && (unsigned char)(text[1]) == 0xBB && (unsigned char)(text[2]) == 0xBF)
		text += 3;

	LOC_ParseLocalizationFGD (text, "fgd/messages.fgd", protected_entries);
	if (localization.numentries > oldnumentries)
		LOC_BuildLookup ();
}
/*
================
LOC_Init
================
*/
cvar_t language = {"language", "auto", CVAR_ARCHIVE};

static const char *const knownlangs[][2] = {{"", "auto"}, {"en", "english"}, {"fr", "french"}, {"de", "german"}, {"it", "italian"}, {"es", "spanish"}};

static const char *LOC_GetSystemLanguage (void)
{
	const char *result = "english";
#ifdef USE_SDL3
	SDL_Locale **prefs = SDL_GetPreferredLocales (NULL);
#else
	SDL_Locale *prefs = SDL_GetPreferredLocales ();
#endif
	if (!prefs)
		return result;
	for (int i = 0;; i++)
	{
#ifdef USE_SDL3
		if (!prefs[i])
			break;
		const char *code = prefs[i]->language;
#else
		const char *code = prefs[i].language;
#endif
		if (!code)
			break;
		for (int j = 1; j < countof (knownlangs); j++)
			if (!q_strcasecmp (code, knownlangs[j][0]))
			{
				result = knownlangs[j][1];
				goto done;
			}
	}
done:
	SDL_free (prefs);
	return result;
}

void LOC_Load (void)
{
	const char *name = !q_strcasecmp (language.string, "auto") ? LOC_GetSystemLanguage () : language.string;
	char		path[MAX_QPATH];
	qboolean	loaded = false;
	qboolean	nonenglish = false;
	int		protected_entries;

	// A language is a name, not an arbitrary filesystem path.
	if (!COM_ModForbiddenChars (name) && q_snprintf (path, sizeof (path), "localization/loc_%s.txt", name) < sizeof (path))
	{
		loaded = LOC_LoadFile (path);
		if (loaded && q_strcasecmp (name, "english"))
			nonenglish = true;
	}
	if (!loaded)
		LOC_LoadFile ("localization/loc_english.txt");

	protected_entries = nonenglish ? localization.numentries : 0;
	localization.mg3_fallback = COM_FileExists ("fgd/quake_mg3.fgd", NULL);
	LOC_LoadFGD (protected_entries);
}

static void LOC_Language_f (cvar_t *var)
{
	LOC_Load ();
}

static void LOC_LanguageCompletion_f (cvar_t *var, const char *partial)
{
	for (int i = 0; i < countof (knownlangs); i++)
		Con_AddToTabList (knownlangs[i][1], partial, NULL);
}

void LOC_CycleLanguage (int dir)
{
	int i;
	for (i = 0; i < countof (knownlangs); i++)
		if (!q_strcasecmp (language.string, knownlangs[i][1]))
			break;
	Cvar_SetQuick (&language, knownlangs[(i + countof (knownlangs) + dir) % countof (knownlangs)][1]);
}

void LOC_Init (void)
{
	Cvar_RegisterVariable (&language);
	Cvar_SetCallback (&language, LOC_Language_f);
	Cvar_SetCompletion (&language, LOC_LanguageCompletion_f);
	LOC_Load ();
}

/*
================
LOC_Shutdown
================
*/
void LOC_Shutdown (void)
{
	Mem_Free (localization.indices);
	Mem_Free (localization.entries);
	Mem_Free (localization.text);
	Mem_Free (localization.fgdbuffer);
	memset (&localization, 0, sizeof (localization));
}

/*
================
LOC_GetRawString

Returns localized string if available, or NULL otherwise
================
*/
const char *LOC_GetRawString (const char *key)
{
	unsigned pos, end;

	if (!localization.numindices || !key || !*key || *key != '$')
		return NULL;
	key++;

	pos = COM_HashString (key) % localization.numindices;
	end = pos;

	do
	{
		unsigned	idx = localization.indices[pos];
		locentry_t *entry;
		if (!idx)
			return NULL;

		entry = &localization.entries[idx - 1];
		if (!strcmp (entry->key, key))
			return entry->value;

		++pos;
		if (pos == localization.numindices)
			pos = 0;
	} while (pos != end);

	return NULL;
}

typedef struct
{
	const char *key;
	const char *value;
} loc_fallback_t;

static const char *LOC_GetMG3Fallback (const char *key)
{
	static const loc_fallback_t fallbacks[] = {
		{"$mg3_qc_upgrade_success", "Upgrade successful: "},
		{"$mg3_qc_upgrade_fail", "You cannot use this upgrade.\n"},
		{"$mg3_qc_upgrade_health", "maximum health "},
		{"$mg3_qc_upgrade_shell", "maximum shells "},
		{"$mg3_qc_upgrade_nail", "maximum nails "},
		{"$mg3_qc_upgrade_rocket", "maximum rockets "},
		{"$mg3_qc_upgrade_cell", "maximum cells "},
		{"$mg3_qc_armor_shard_touch", "Armor shard acquired.\n"},
		{"$mg3_qc_axe_button", "Only the axe can activate this.\n"},
		{"$mg3_qc_hammer", "Hammer acquired.\n"},
		{"$mg3_qc_lavasuit", "Lava suit acquired.\n"},
		{"$mg3_qc_lavasuit_wearing_out", "Lava suit is wearing out!\n"},
		{"$mg3_qc_newgameplus_item", "New Game Plus item acquired.\n"},
		{"$mg3_qc_ring_of_insight", "Ring of Insight acquired.\n"},
		{"$mg3_qc_ring_of_oblivion", "Ring of Oblivion acquired.\n"},
		{"$mg3_qc_rune1", "Rune of Madness acquired.\n"},
		{"$mg3_qc_rune2", "Rune of Chaos acquired.\n"},
		{"$mg3_qc_rune3", "Rune of Sorrow acquired.\n"},
		{"$mg3_qc_rune4", "Rune of Sacrifice acquired.\n"},
		{"$mg3_qc_sacricie_count_1_more", "One more sacrifice remains.\n"},
		{"$mg3_qc_sacricie_count_2_more", "Two more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_3_more", "Three more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_4_more", "Four more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_5_more", "Five more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_6_more", "Six more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_7_more", "Seven more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_8_more", "Eight more sacrifices remain.\n"},
		{"$mg3_qc_sacricie_count_more", "More sacrifices are required.\n"},
		{"$mg3_qc_sacricie_count_complete", "The sacrifice is complete.\n"},
		{"$mg3_hub_selected_easy", "Easy difficulty selected.\n"},
		{"$mg3_hub_selected_normal", "Normal difficulty selected.\n"},
		{"$mg3_hub_selected_hard", "Hard difficulty selected.\n"},
		{"$mg3_hub_selected_nightmare", "Nightmare difficulty selected.\n"},
		{"$mg3_selected_bloody_nightmare", "Bloody Nightmare selected.\n"},
		{"$mg3_hub_rune1_hint_complete", "The Rune of Madness has been claimed.\n"},
		{"$mg3_hub_rune2_hint_complete", "The Rune of Chaos has been claimed.\n"},
		{"$mg3_hub_rune3_hint_complete", "The Rune of Sorrow has been claimed.\n"}
	};
	static char buffers[8][128];
	static int buffer_index;
	const char *source;
	char *out;
	int i, j;
	if (!localization.mg3_fallback || !key || q_strncasecmp (key, "$mg3_", 5))
		return NULL;
	for (i = 0; i < (int)(sizeof (fallbacks) / sizeof (fallbacks[0])); ++i)
		if (!q_strcasecmp (key, fallbacks[i].key))
			return fallbacks[i].value;

	source = key + 5;
	if (!q_strncasecmp (source, "qc_", 3))
		source += 3;
	buffer_index = (buffer_index + 1) % (int)(sizeof (buffers) / sizeof (buffers[0]));
	out = buffers[buffer_index];
	for (i = 0, j = 0; source[i] && j < (int)sizeof (buffers[0]) - 1; ++i, ++j)
		out[j] = source[i] == '_' ? ' ' : source[i];
	out[j] = 0;
	if (out[0] >= 'a' && out[0] <= 'z')
		out[0] -= 'a' - 'A';
	return out;
}

/*
================
LOC_GetString

Returns localized string if available, or input string otherwise
================
*/
const char *LOC_GetString (const char *key)
{
	const char *value = LOC_GetRawString (key);
	if (value)
		return value;
	value = LOC_GetMG3Fallback (key);
	if (value)
		return value;

	if (key && (key[0] == '$') && (key[1] == 'q' || key[1] == 'Q') && (key[2] == 'c' || key[2] == 'C') && (key[3] == '_'))
	{
		static char fallback_buffers[8][128];
		static int fallback_idx = 0;
		char *buf;
		int i, j;

		if (!q_strcasecmp (key, "$QC_Door"))
			return "This door is opened elsewhere.";

		fallback_idx = (fallback_idx + 1) % 8;
		buf = fallback_buffers[fallback_idx];
		for (i = 4, j = 0; key[i] && j < 127; i++, j++)
		{
			if (key[i] == '_')
				buf[j] = ' ';
			else
				buf[j] = key[i];
		}
		buf[j] = '\0';

		if (j > 0 && buf[0] >= 'a' && buf[0] <= 'z')
			buf[0] = buf[0] - ('a' - 'A');

		return buf;
	}

	return key;
}

/*
================
LOC_ParseArg

Returns argument index (>= 0) and advances the string if it starts with a placeholder ({} or {N}),
otherwise returns a negative value and leaves the pointer unchanged
================
*/
static int LOC_ParseArg (const char **pstr)
{
	int			arg;
	const char *str = *pstr;

	// opening brace
	if (*str != '{')
		return -1;
	++str;

	// optional index, defaulting to 0
	arg = 0;
	while (q_isdigit (*str))
		arg = arg * 10 + *str++ - '0';

	// closing brace
	if (*str != '}')
		return -1;
	*pstr = ++str;

	return arg;
}

/*
================
LOC_HasPlaceholders
================
*/
qboolean LOC_HasPlaceholders (const char *str)
{
	if (!localization.numindices)
		return false;
	while (*str)
	{
		if (LOC_ParseArg (&str) >= 0)
			return true;
		str++;
	}
	return false;
}

/*
================
LOC_Format

Replaces placeholders (of the form {} or {N}) with the corresponding arguments

Returns number of written chars, excluding the NUL terminator
If len > 0, output is always NUL-terminated
================
*/
size_t LOC_Format (const char *format, const char *(*getarg_fn) (int idx, void *userdata), void *userdata, char *out, size_t len)
{
	size_t written = 0;
	int	   numargs = 0;

	if (!len)
	{
		Con_DPrintf ("LOC_Format: no output space\n");
		return 0;
	}
	--len; // reserve space for the terminator

	while (*format && written < len)
	{
		const char *insert;
		size_t		space_left;
		size_t		insert_len;
		int			argindex = LOC_ParseArg (&format);

		if (argindex < 0)
		{
			out[written++] = *format++;
			continue;
		}

		insert = getarg_fn (argindex, userdata);
		space_left = len - written;
		insert_len = strlen (insert);

		if (insert_len > space_left)
		{
			Con_DPrintf ("LOC_Format: overflow at argument #%d\n", numargs);
			insert_len = space_left;
		}

		memcpy (out + written, insert, insert_len);
		written += insert_len;
	}

	if (*format)
		Con_DPrintf ("LOC_Format: overflow\n");

	out[written] = 0;

	return written;
}

/*
================
Initial state picked randomly, can't be 0.
================
*/
static uint32_t xorshiro_state[2] = {0xcdb38550, 0x720a8392};

/*
=================
COM_SeedRand
=================
*/
void COM_SeedRand (uint64_t seed)
{
	// SplitMix64
	uint64_t z = (seed + 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	uint64_t state = z ^ (z >> 31);
	xorshiro_state[0] = (uint32_t)state;
	xorshiro_state[1] = (uint32_t)(state >> 32);
}

/*
=================
COM_Rand
=================
*/
static inline uint32_t rotl (const uint32_t x, int k)
{
	return (x << k) | (x >> (32 - k));
}

int32_t COM_Rand ()
{
	// Xorshiro64**
	const uint32_t s0 = xorshiro_state[0];
	uint32_t	   s1 = xorshiro_state[1];
	const uint32_t result = rotl (s0 * 0x9E3779BB, 5) * 5;
	s1 ^= s0;
	xorshiro_state[0] = rotl (s0, 26) ^ s1 ^ (s1 << 9);
	xorshiro_state[1] = rotl (s1, 13);

	return (int32_t)(result & COM_RAND_MAX);
}

void COM_Assert_Failed (const char *expr, const char *file_path, int line)
{
	// only keep the simple file name, strip the directory part
	// we only want the short file name, not the full path:
	const char *last_sep = FIND_LAST_DIRSEP (file_path);

	const char *filename = (last_sep ? last_sep + 1 : file_path);

	if (Tasks_IsWorker ())
	{
		Sys_DebugBreak ();

		if (!Sys_IsInDebugger ())
		{
			const char *captured_stack_trace = Sys_StackTrace ();

			char *msg = q_strcatf (NULL, "%s:%d Assertion: '%s' failed\nSTACK TRACE:\n%s", filename, line, expr, captured_stack_trace);

			Mem_Free (captured_stack_trace);

			Sys_Printf ("%s\n", msg);
#if defined(_WIN32)
			// Only the Win32 MessageBox can safely be called from any thread.
			PL_ErrorDialog (msg);
#endif
			Mem_Free (msg);
		}
		abort ();
	}
	else // We are in the main thread, console is accessible, do Host_Error and we can recover.
		Host_Error ("%s:%d Assertion: '%s' failed", filename, line, expr);
}
