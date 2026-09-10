/* Local, immutable cosmetic packages. Local IDs are never sent on the wire. */
#ifndef CUSTOM_AVATAR_H
#define CUSTOM_AVATAR_H

#include "r_avatar.h"

#define CUSTOM_AVATAR_MAX_PACKAGES 64
#define CUSTOM_AVATAR_MAX_VERTICES 16384
#define CUSTOM_AVATAR_MAX_TRIANGLES 32768
#define CUSTOM_AVATAR_MAX_INFLUENCES 4
#define CUSTOM_AVATAR_MAX_IMAGE_DIMENSION 2048
#define CUSTOM_AVATAR_MAX_IMAGE_BYTES (16u * 1024u * 1024u)

enum { CUSTOM_AVATAR_MANIFEST, CUSTOM_AVATAR_MESH,
	CUSTOM_AVATAR_SKIN, CUSTOM_AVATAR_GLOW, CUSTOM_AVATAR_FILE_COUNT };

typedef struct custom_avatar_s {
	int id;
	char key[32];
	char name[32];
	char digest[65];
	char directory[MAX_OSPATH];
	char model_name[MAX_QPATH];
	char bones[MD5_VRIK_JOINT_COUNT][32];
	r_avatar_profile_t profile;
} custom_avatar_t;

typedef struct custom_avatar_data_s {
	byte *bytes[CUSTOM_AVATAR_FILE_COUNT];
	size_t sizes[CUSTOM_AVATAR_FILE_COUNT];
} custom_avatar_data_t;

/* Startup only; immutable across map/mod changes. No asset IO on a server. */
void CustomAvatar_Init (void);
/* All the following selection helpers include the fixed builtin entries. */
int CustomAvatar_TotalCount (void);
int CustomAvatar_IdForKey (const char *key);
const char *CustomAvatar_KeyForId (int id);
const char *CustomAvatar_DisplayNameForId (int id);
/* Custom-only: NULL/-1 for builtin, absent or mismatched identities. */
const custom_avatar_t *CustomAvatar_Get (int id);
int CustomAvatar_Resolve (const char *key, const char *digest);
int CustomAvatar_IdForModelName (const char *name);
/* Bounded fixed-file snapshots, rehashed against the startup identity.
 * On success caller owns data and must FreeData. Failure leaves data empty. */
qboolean CustomAvatar_ReadData (int id, custom_avatar_data_t *data);
void CustomAvatar_FreeData (custom_avatar_data_t *data);
void CustomAvatar_MarkFailed (int id);
qboolean CustomAvatar_HasFailed (int id);

#endif
