/* Stable cosmetic-avatar identities shared by client, server, and renderer. */
#ifndef PLAYER_AVATAR_H
#define PLAYER_AVATAR_H

#include <stddef.h>

#define PLAYER_AVATAR_PROTOCOL_VERSION 1
#define PLAYER_AVATAR_MAX_SLOTS 16

typedef enum player_avatar_id_e {
	PLAYER_AVATAR_RANGER = 0,
	PLAYER_AVATAR_SOLDIER,
	PLAYER_AVATAR_ENFORCER,
	PLAYER_AVATAR_DOG,
	PLAYER_AVATAR_OGRE,
	PLAYER_AVATAR_KNIGHT,
	PLAYER_AVATAR_DEATH_KNIGHT,
	PLAYER_AVATAR_FIEND,
	PLAYER_AVATAR_SHAMBLER,
	PLAYER_AVATAR_ZOMBIE,
	PLAYER_AVATAR_VORE,
	PLAYER_AVATAR_COUNT
} player_avatar_id_t;

int PlayerAvatar_IsValidId(int id);
const char *PlayerAvatar_KeyForId(int id);
const char *PlayerAvatar_DisplayNameForId(int id);
int PlayerAvatar_IdForKey(const char *key);

/* Strict, complete command parsers.  Return nonzero only for canonical input. */
int PlayerAvatar_ParseProtocolOffer(const char *command);
/* Latches a valid offer once; a post-capability duplicate is a no-op. */
int PlayerAvatar_LatchProtocolOffer(const char *command, int *offered,
	int *cap_pending, int cap_sent);
int PlayerAvatar_ParseCapabilityCommand(const char *command);
int PlayerAvatar_ParseSetCommand(const char *command, int *id);
int PlayerAvatar_ParseSlotCommand(const char *command, int *slot, int *id);
/* Builds a capability-gated server slot update. Returns nonzero on success. */
int PlayerAvatar_BuildSlotCommand(int recipient_capable, char *buffer,
	size_t buffer_size, int slot, int id);

#endif /* PLAYER_AVATAR_H */
