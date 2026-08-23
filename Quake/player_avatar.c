#include "player_avatar.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct player_avatar_info_s {
	const char *key;
	const char *display_name;
} player_avatar_info_t;

static const player_avatar_info_t player_avatar_info[PLAYER_AVATAR_COUNT] = {
	{"ranger", "Ranger"},
	{"soldier", "Soldier"},
	{"enforcer", "Enforcer"},
	{"dog", "Dog"},
	{"ogre", "Ogre"},
	{"knight", "Knight"},
	{"death_knight", "Death Knight"},
	{"fiend", "Fiend"},
	{"shambler", "Shambler"},
	{"zombie", "Zombie"},
	{"vore", "Vore"}
};

static const char *PlayerAvatar_SkipSpace(const char *text)
{
	while (*text == ' ' || *text == '\t')
		text++;
	return text;
}

static int PlayerAvatar_AtEnd(const char *text)
{
	text = PlayerAvatar_SkipSpace(text);
	while (*text == '\r' || *text == '\n')
		text++;
	return !*text;
}

static int PlayerAvatar_ParseCanonicalUnsigned(const char **text, int *value)
{
	const char *p = *text;
	int result = 0;

	if (*p < '0' || *p > '9')
		return 0;
	if (*p == '0' && p[1] >= '0' && p[1] <= '9')
		return 0;
	for (; *p >= '0' && *p <= '9'; p++)
	{
		int digit = *p - '0';
		if (result > INT_MAX / 10 ||
			(result == INT_MAX / 10 && digit > INT_MAX % 10))
			return 0;
		result = result * 10 + digit;
	}
	*text = p;
	*value = result;
	return 1;
}

static int PlayerAvatar_ParsePrefix(const char **text, const char *prefix)
{
	size_t length = strlen(prefix);

	if (strncmp(*text, prefix, length))
		return 0;
	*text += length;
	if (**text != ' ' && **text != '\t')
		return 0;
	*text = PlayerAvatar_SkipSpace(*text);
	return 1;
}

int PlayerAvatar_IsValidId(int id)
{
	return id >= PLAYER_AVATAR_RANGER && id < PLAYER_AVATAR_COUNT;
}

const char *PlayerAvatar_KeyForId(int id)
{
	return PlayerAvatar_IsValidId(id) ? player_avatar_info[id].key : NULL;
}

const char *PlayerAvatar_DisplayNameForId(int id)
{
	return PlayerAvatar_IsValidId(id) ? player_avatar_info[id].display_name : NULL;
}

int PlayerAvatar_IdForKey(const char *key)
{
	int id;

	if (!key)
		return -1;
	for (id = PLAYER_AVATAR_RANGER; id < PLAYER_AVATAR_COUNT; id++)
		if (!strcmp(key, player_avatar_info[id].key))
			return id;
	return -1;
}

int PlayerAvatar_ParseProtocolOffer(const char *command)
{
	const char *text = command;
	int version;

	if (!text || !PlayerAvatar_ParsePrefix(&text, "avatar_protocol") ||
		!PlayerAvatar_ParseCanonicalUnsigned(&text, &version) ||
		!PlayerAvatar_AtEnd(text))
		return 0;
	return version == PLAYER_AVATAR_PROTOCOL_VERSION;
}

int PlayerAvatar_LatchProtocolOffer(const char *command, int *offered,
	int *cap_pending, int cap_sent)
{
	if (!offered || !cap_pending || !PlayerAvatar_ParseProtocolOffer(command))
		return 0;
	if (!cap_sent)
	{
		*offered = 1;
		*cap_pending = 1;
	}
	return 1;
}

int PlayerAvatar_ParseCapabilityCommand(const char *command)
{
	const char *text = command;
	int version;

	if (!text || !PlayerAvatar_ParsePrefix(&text, "avatar_cap") ||
		!PlayerAvatar_ParseCanonicalUnsigned(&text, &version) ||
		!PlayerAvatar_AtEnd(text))
		return 0;
	return version == PLAYER_AVATAR_PROTOCOL_VERSION;
}

int PlayerAvatar_ParseSetCommand(const char *command, int *id)
{
	const char *text = command;
	int parsed_id;

	if (!text || !id || !PlayerAvatar_ParsePrefix(&text, "avatar_set") ||
		!PlayerAvatar_ParseCanonicalUnsigned(&text, &parsed_id) ||
		!PlayerAvatar_AtEnd(text) || !PlayerAvatar_IsValidId(parsed_id))
		return 0;
	*id = parsed_id;
	return 1;
}

int PlayerAvatar_ParseSlotCommand(const char *command, int *slot, int *id)
{
	const char *text = command;
	int version;
	int parsed_slot;
	int parsed_id;

	if (!text || !slot || !id || !PlayerAvatar_ParsePrefix(&text, "avatar_slot") ||
		!PlayerAvatar_ParseCanonicalUnsigned(&text, &version) ||
		version != PLAYER_AVATAR_PROTOCOL_VERSION ||
		(*text != ' ' && *text != '\t'))
		return 0;
	text = PlayerAvatar_SkipSpace(text);
	if (!PlayerAvatar_ParseCanonicalUnsigned(&text, &parsed_slot) ||
		parsed_slot < 0 || parsed_slot >= PLAYER_AVATAR_MAX_SLOTS ||
		(*text != ' ' && *text != '\t'))
		return 0;
	text = PlayerAvatar_SkipSpace(text);
	if (!PlayerAvatar_ParseCanonicalUnsigned(&text, &parsed_id) ||
		!PlayerAvatar_AtEnd(text) || !PlayerAvatar_IsValidId(parsed_id))
		return 0;
	*slot = parsed_slot;
	*id = parsed_id;
	return 1;
}

int PlayerAvatar_BuildSlotCommand(int recipient_capable, char *buffer,
	size_t buffer_size, int slot, int id)
{
	int written;

	if (!recipient_capable || !buffer || !buffer_size ||
		slot < 0 || slot >= PLAYER_AVATAR_MAX_SLOTS ||
		!PlayerAvatar_IsValidId(id))
		return 0;
	written = snprintf(buffer, buffer_size, "//avatar_slot %d %d %d\n",
		PLAYER_AVATAR_PROTOCOL_VERSION, slot, id);
	return written >= 0 && (size_t)written < buffer_size;
}
