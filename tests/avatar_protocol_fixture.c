#include "../Quake/player_avatar.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

int main(void)
{
	int id = -1;
	int slot = -1;
	int offered = 0;
	int cap_pending = 0;
	char command[64];

	CHECK(PLAYER_AVATAR_RANGER == 0);
	CHECK(PLAYER_AVATAR_SOLDIER == 1);
	CHECK(PLAYER_AVATAR_VORE == 10);
	CHECK(PLAYER_AVATAR_COUNT == 11);
	CHECK(!PlayerAvatar_IsValidId(-1));
	CHECK(PlayerAvatar_IsValidId(PLAYER_AVATAR_VORE));
	CHECK(!PlayerAvatar_IsValidId(PLAYER_AVATAR_COUNT));
	CHECK(!strcmp(PlayerAvatar_KeyForId(PLAYER_AVATAR_DEATH_KNIGHT),
		"death_knight"));
	CHECK(!strcmp(PlayerAvatar_DisplayNameForId(PLAYER_AVATAR_DEATH_KNIGHT),
		"Death Knight"));
	CHECK(PlayerAvatar_IdForKey("shambler") == PLAYER_AVATAR_SHAMBLER);
	CHECK(PlayerAvatar_IdForKey("spawn") < 0);
	CHECK(PlayerAvatar_IdForKey("scrag") < 0);
	CHECK(PlayerAvatar_IdForKey("Ranger") < 0);

	CHECK(PlayerAvatar_ParseProtocolOffer("avatar_protocol 1\n"));
	CHECK(!PlayerAvatar_ParseProtocolOffer("avatar_protocol 01"));
	CHECK(!PlayerAvatar_ParseProtocolOffer("avatar_protocol 2"));
	CHECK(!PlayerAvatar_ParseProtocolOffer("avatar_protocol 1 extra"));
	CHECK(PlayerAvatar_ParseCapabilityCommand("avatar_cap 1"));
	CHECK(!PlayerAvatar_ParseCapabilityCommand("avatar_cap 1x"));
	CHECK(PlayerAvatar_ParseSetCommand("avatar_set 10\r\n", &id));
	CHECK(id == PLAYER_AVATAR_VORE);
	CHECK(!PlayerAvatar_ParseSetCommand("avatar_set 11", &id));
	CHECK(!PlayerAvatar_ParseSetCommand("avatar_set -1", &id));
	CHECK(!PlayerAvatar_ParseSetCommand("avatar_set 01", &id));
	CHECK(!PlayerAvatar_ParseSetCommand("avatar_set 999999999999999999999", &id));
	CHECK(PlayerAvatar_ParseSlotCommand("avatar_slot 1 15 6\n", &slot, &id));
	CHECK(slot == 15 && id == PLAYER_AVATAR_DEATH_KNIGHT);
	CHECK(!PlayerAvatar_ParseSlotCommand("avatar_slot 1 16 0", &slot, &id));
	CHECK(!PlayerAvatar_ParseSlotCommand("avatar_slot 2 0 0", &slot, &id));
	CHECK(!PlayerAvatar_ParseSlotCommand("avatar_slot 1 0 99", &slot, &id));
	CHECK(!PlayerAvatar_ParseSlotCommand("avatar_slot 1 0 0 junk", &slot, &id));

	/* Reused slots reset to Ranger only for negotiated peers; this is the
	 * exact command SV_WriteAvatarSlot places on its reliable stream. */
	CHECK(!PlayerAvatar_BuildSlotCommand(0, command, sizeof(command), 4,
		PLAYER_AVATAR_RANGER));
	CHECK(PlayerAvatar_BuildSlotCommand(1, command, sizeof(command), 4,
		PLAYER_AVATAR_RANGER));
	CHECK(!strcmp(command, "//avatar_slot 1 4 0\n"));
	CHECK(PlayerAvatar_ParseSlotCommand(command + 2, &slot, &id));
	CHECK(slot == 4 && id == PLAYER_AVATAR_RANGER);
	CHECK(!PlayerAvatar_BuildSlotCommand(1, command, sizeof(command),
		PLAYER_AVATAR_MAX_SLOTS, PLAYER_AVATAR_RANGER));

	/* The offer arrives before the table. Duplicate offers keep the same
	 * pending state, while one received after the capability is sent is a no-op. */
	CHECK(PlayerAvatar_LatchProtocolOffer("avatar_protocol 1", &offered,
		&cap_pending, 0));
	CHECK(offered && cap_pending);
	CHECK(PlayerAvatar_LatchProtocolOffer("avatar_protocol 1", &offered,
		&cap_pending, 0));
	CHECK(offered && cap_pending);
	cap_pending = 0;
	CHECK(PlayerAvatar_LatchProtocolOffer("avatar_protocol 1", &offered,
		&cap_pending, 1));
	CHECK(offered && !cap_pending);

	return failures ? 1 : 0;
}
