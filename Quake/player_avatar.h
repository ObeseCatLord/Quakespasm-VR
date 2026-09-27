/* Stable cosmetic-avatar identities shared by client, server, and renderer. */
#ifndef PLAYER_AVATAR_H
#define PLAYER_AVATAR_H

#include <stddef.h>

#define PLAYER_AVATAR_PROTOCOL_VERSION 1
#define PLAYER_AVATAR_MAX_SLOTS 16
#define PLAYER_AVATAR_CUSTOM_PROTOCOL_VERSION 1
#define PLAYER_AVATAR_CUSTOM_KEY_MAX 31
#define PLAYER_AVATAR_CUSTOM_DIGEST_MAX 64

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
/* Strict portable custom-package identities.  Builtin keys are never custom. */
int PlayerAvatar_ValidCustomKey(const char *key);
int PlayerAvatar_ValidCustomDigest(const char *digest);

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

int PlayerAvatar_ParseCustomProtocolOffer(const char *command);
int PlayerAvatar_LatchCustomProtocolOffer(const char *command, int *offered,
	int *cap_pending, int cap_sent);
int PlayerAvatar_ParseCustomCapabilityCommand(const char *command);
int PlayerAvatar_ParseCustomSetCommand(const char *command, char *key,
	size_t key_size, char *digest, size_t digest_size);
/* A custom slot clear is represented by canonical "- -" tokens. */
int PlayerAvatar_ParseCustomSlotCommand(const char *command, int *slot,
	char *key, size_t key_size, char *digest, size_t digest_size, int *clear);
int PlayerAvatar_BuildCustomSlotCommand(int recipient_capable, char *buffer,
	size_t buffer_size, int slot, const char *key, const char *digest);

#endif /* PLAYER_AVATAR_H */
